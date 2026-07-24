/**
 * LumeController implementation
 */

#include "controller.h"
#include "param_codec.h"
#include "fastled_output.h"
#include "../protocols/protocol.h"
#include "../logging.h"

namespace lume {

// Default LED-output driver (RFC 0001 §6). Stateless; the controller points its
// output_ at this unless setLedOutput() injects another.
static FastLedOutput g_fastLedOutput;

// Global instance
LumeController controller;

LumeController::LumeController()
    : ledCount(0)
    , segmentCount(0)
    , nextSegmentId(0)
    , workbufferOwner_(-1)
    , power(true)
    , globalBrightness(255)
    , ledGamma_(LED_GAMMA)          // runtime gamma, seeded from the compile default
    , warmth_(LED_WARMTH_DEFAULT)   // dim-to-warm strength, seeded from the default

    , powerOffWhenSettled_(false)
    , powerOffWhenFadeSettles_(false)
    , protocolCount_(0)
    , protocolActive_(false)
    , activeProtocol_(nullptr)
    , targetFps(DEFAULT_FPS)
    , frameCounter(0)
    , lastFrameTime(0)
    , actualFps(0)
    , fpsUpdateTime(0)
    , fpsFrameCount(0)
    , segmentsDirty_(false)
    , suppressDirty_(false)
    , lastSegmentChange_(0)
    , output_(&g_fastLedOutput) {

    memset(leds, 0, sizeof(leds));
    memset(protocols_, 0, sizeof(protocols_));
}

void LumeController::begin(uint16_t count) {
    ledCount = min(count, (uint16_t)MAX_LED_COUNT);
    
    // Initialize command queue
    if (!commandQueue.begin()) {
        LOG_ERROR(LogTag::LED, "Failed to initialize command queue");
    }
    
    // Initialize the LED output driver (FastLED by default; see RFC 0001 §6).
    output_->begin(leds, ledCount);
    output_->setBrightness(globalBrightness);
    output_->clear();
    output_->show();

    // Seed the easing engine with the current brightness so the first eased
    // change starts from the real value, not zero.
    brightnessTransition_.snap(globalBrightness);
    // Seed the power envelope to match the initial power state.
    powerFade_.snap(power ? 255 : 0);

    lastFrameTime = millis();
    fpsUpdateTime = millis();
}

void LumeController::update() {
    uint32_t now = millis();
    uint32_t frameInterval = 1000 / targetFps;

    // Rendering (command processing, transition advance, effect draw) is frame-
    // rate limited. But output_->show() runs on EVERY call — see the end. FastLED
    // temporal dithering advances its counter each show(), so refreshing faster
    // than the animation rate lets it smooth *mid-range* dim content. It does not
    // fix dim white going red (saturated source bytes gain no dither headroom);
    // that is handled by the output floor + correction ramp below. So: draw at
    // targetFps, refresh as fast as the loop allows.
    if (now - lastFrameTime >= frameInterval) {
        lastFrameTime = now;

        // Process any pending commands (single-writer model)
        processCommands();

        // FPS calculation
        fpsFrameCount++;
        if (now - fpsUpdateTime >= 1000) {
            actualFps = fpsFrameCount;
            fpsFrameCount = 0;
            fpsUpdateTime = now;
        }

        // Advance the premium easing engine (eased global brightness — the
        // user's level). Output brightness is composed + applied below; this
        // only steps the stored level toward its target.
        if (brightnessTransition_.isActive()) {
            globalBrightness = brightnessTransition_.advance(now);
        }

        // Power-off rider: when a brightness fade carrying the "power off at
        // zero" policy settles, switch the strip off (all that remains of
        // "nightlight" in the core).
        if (powerOffWhenSettled_ && !brightnessTransition_.isActive()) {
            powerOffWhenSettled_ = false;
            setPower(false);
            LOG_INFO(LogTag::LED, "Brightness fade settled -> power off");
        }

        // Compose the perceptual output level (user level scaled by the power
        // fade envelope, still perceptual) and gamma-encode to PWM. Equal steps
        // in the linearly-eased level then read as equal *perceived* steps.
        uint8_t powerEnv = powerFade_.advance(now);
        uint8_t perceptual = (powerEnv == 255) ? globalBrightness
                                               : scale8(globalBrightness, powerEnv);
        uint8_t encoded = applyGamma_video(perceptual, ledGamma_);

        // Low-end floor (deterministic; see LED_MIN_OUTPUT). Below a few PWM
        // levels the green/blue channels of a white pixel round to 0 before red,
        // so dim white collapses to (1,0,0) RED. Never emit a non-zero level
        // inside that broken zone: hold the lowest clean level and let a true
        // zero still cut to black. This is the white→red→black fix, and it needs
        // no eyeball tuning.
        if (perceptual > 0 && encoded < LED_MIN_OUTPUT) encoded = LED_MIN_OUTPUT;
        output_->setBrightness(encoded);

        // Correction ramp: the red bias *is* the color correction. TypicalLEDStrip
        // (255,176,240) pulls green/blue down, so near the floor they die first.
        // Ramp the applied correction from full TypicalLEDStrip (at/above
        // LED_CORRECTION_FULL_AT) toward uncorrected white (255,255,255) as the
        // output approaches the floor, so the dim end reads as clean white — the
        // strip's normal white balance above the knee is untouched.
        uint8_t k = (encoded >= LED_CORRECTION_FULL_AT)
                        ? 255
                        : (uint8_t)((uint16_t)encoded * 255 / LED_CORRECTION_FULL_AT);
        float kf = (float)k / 255.0f;
        output_->setCorrection(CRGB(lerpU8(255, LED_CORRECTION_R, kf),
                                    lerpU8(255, LED_CORRECTION_G, kf),
                                    lerpU8(255, LED_CORRECTION_B, kf)));

        // Dim-to-warm: blend the output color temperature from neutral white
        // toward the warm target as the perceptual level drops, so the low end
        // glides to a designed amber instead of the accidental red PWM floor.
        // Quadratic in (1 - level): near-neutral through the upper range, warm
        // only down low. `t` in [0,1] scaled by the runtime warmth_ strength;
        // warmth_ == 0 yields neutral (0xFFFFFF), i.e. no tint.
        float dim = 1.0f - (float)perceptual / 255.0f;
        float t = warmth_ * dim * dim;
        output_->setTemperature(CRGB(lerpU8(255, LED_WARM_TARGET_R, t),
                                     lerpU8(255, LED_WARM_TARGET_G, t),
                                     lerpU8(255, LED_WARM_TARGET_B, t)));

        // Flip logical power off once a fade-out envelope reaches zero.
        if (powerOffWhenFadeSettles_ && !powerFade_.isActive()) {
            powerOffWhenFadeSettles_ = false;
            power = false;
            LOG_INFO(LogTag::LED, "Power fade settled -> off");
        }

        // Draw this frame into leds[] from the active source (mutually exclusive).
        if (!power) {
            output_->clear();
        } else {
            processProtocols();
            if (protocolActive_) {
                // A protocol (sACN) has already written leds[]; nothing to draw.
            } else if (directPixels_.isReady()) {
                // Drain a staged direct-pixel frame (debug /api/pixels). Single-
                // writer: the web task only wrote directPixels_ (atomic ready
                // flag); leds[] is touched here, on the loop. One-frame overlay.
                uint16_t count = min(directPixels_.getLedCount(), ledCount);
                memcpy(leds, directPixels_.getBuffer(), count * sizeof(CRGB));
                directPixels_.clearReady();
            } else {
                // Clear only the LEDs not owned by an active segment. Effects
                // own their canvas and many build fade-trails by reading the
                // previous frame (sinelon, wave, comet...); a blanket clear
                // would wipe that history. Clearing only gaps keeps it intact.
                clearUncoveredLeds();
                for (uint8_t i = 0; i < segmentCount; i++) {
                    if (segments[i].isActive()) {
                        segments[i].update(frameCounter, now);
                    }
                }
            }
        }
        frameCounter++;
    }

    // Push the frame on EVERY call. Repeated refreshes of the same buffer drive
    // FastLED's temporal dithering for mid-range content (see the note above);
    // the physical WS2812 transmission time naturally caps the rate.
    output_->show();
}

void LumeController::processCommands() {
    Command cmd;
    // Process all pending commands this frame
    while (commandQueue.dequeue(cmd)) {
        executeCommand(cmd);
    }
}

void LumeController::executeCommand(const Command& cmd) {
    Segment* seg = nullptr;
    
    // Get target segment if needed
    if (cmd.segmentId != 255) {
        seg = getSegment(cmd.segmentId);
        if (!seg && cmd.type != CommandType::CreateSegment) {
            LOG_WARN(LogTag::LED, "Command targets unknown segment %d", cmd.segmentId);
            return;
        }
    }
    
    switch (cmd.type) {
        case CommandType::SetEffect:
            if (seg && cmd.data.effectId) {
                seg->setEffect(cmd.data.effectId);
                LOG_DEBUG(LogTag::LED, "Segment %d effect -> %s", cmd.segmentId, cmd.data.effectId);
            }
            break;
            
        case CommandType::SetBrightness:
            if (seg) {
                seg->setBrightness(cmd.data.value8);
            }
            break;
            
        case CommandType::SetSpeed:
            if (seg) {
                seg->setSpeed(cmd.data.value8);
            }
            break;
            
        case CommandType::SetIntensity:
            if (seg) {
                seg->setIntensity(cmd.data.value8);
            }
            break;
            
        case CommandType::SetColor:
            if (seg) {
                if (cmd.data.color.isSecondary) {
                    seg->setColor(1, cmd.data.color.toCRGB());
                } else {
                    seg->setColor(0, cmd.data.color.toCRGB());
                }
            }
            break;
            
        case CommandType::SetPalette:
            if (seg) {
                seg->setPalette(static_cast<PalettePreset>(cmd.data.value8));
            }
            break;
            
        case CommandType::CreateSegment:
            createSegment(cmd.data.segment.start, cmd.data.segment.length, cmd.data.segment.reversed);
            break;
            
        case CommandType::RemoveSegment:
            removeSegment(cmd.segmentId);
            break;
            
        case CommandType::SetPower:
            // Eased when the command carries a transition window, instant
            // otherwise (setPowerEased falls back to setPower at 0).
            setPowerEased(cmd.data.power, cmd.transitionMs);
            LOG_INFO(LogTag::LED, "Power -> %s%s", cmd.data.power ? "ON" : "OFF",
                     cmd.transitionMs ? " (fade)" : "");
            break;
            
        case CommandType::SetGlobalBrightness:
            // Eased when the command carries a transition window, instant
            // otherwise (setBrightnessEased falls back to setBrightness at 0).
            // The powerOffAtZero rider makes a fade-to-zero a "nightlight".
            setBrightnessEased(cmd.data.value8, cmd.transitionMs, cmd.powerOffAtZero);
            break;

        case CommandType::SetGamma:
            // Runtime perceptual-dimming gamma. Applied on the loop (single
            // writer) so update()'s output-encode never reads a torn value.
            // setGamma() clamps to [LED_GAMMA_MIN, LED_GAMMA_MAX].
            setGamma(cmd.data.valueFloat);
            LOG_INFO(LogTag::LED, "Gamma -> %.2f", ledGamma_);
            break;

        case CommandType::SetWarmth:
            // Runtime dim-to-warm strength. Applied on the loop; setWarmth()
            // clamps to [LED_WARMTH_MIN, LED_WARMTH_MAX].
            setWarmth(cmd.data.valueFloat);
            LOG_INFO(LogTag::LED, "Warmth -> %.2f", warmth_);
            break;

        case CommandType::ApplyEffectSpec: {
            // Compound segment mutation (create/update) — the single-writer path
            // that replaces direct handler mutation (RFC 0001 §3).
            const EffectSpec& spec = cmd.data.spec;
            Segment* target = seg;
            if (spec.create) {
                target = createSegment(spec.start, spec.length, spec.reversed);
                if (!target) {
                    LOG_WARN(LogTag::LED, "ApplyEffectSpec: create failed (start=%d len=%d)",
                             spec.start, spec.length);
                    break;
                }
            }
            if (!target) break;  // update targeting an unknown segment

            // Editable boundaries: a non-create update carrying geometry resizes
            // the target in place, clamped to the strip (start < ledCount, length
            // in [1, ledCount - start]). Overlaps are deliberately allowed — the
            // frame composites segments in order into leds[] — so we do NOT check
            // for or prevent overlap. setRange re-points the view (and drops any
            // borrowed workbuffer), which resets effect scratchpad state; that is
            // acceptable for a resize. The trailing markSegmentsDirty() in
            // executeCommand() schedules the debounced layout save, so a resized
            // layout round-trips through serialize/restoreSegments.
            if (!spec.create && spec.hasGeometry && ledCount > 0) {
                uint16_t start = spec.start;
                if (start >= ledCount) start = ledCount - 1;
                uint16_t maxLen = ledCount - start;   // >= 1 since start < ledCount
                uint16_t length = spec.length;
                if (length > maxLen) length = maxLen;
                if (length < 1)      length = 1;
                target->setRange(leds, start, length, spec.reversed);
            }

            // Ease the param/color change when the command asks for it — but
            // only for an in-place update of an existing effect. A create or an
            // effect *change* has nothing coherent to ease from (new schema /
            // freshly-defaulted params), so those snap. setEffect() below also
            // cancels any transition, so snapshot BEFORE it.
            bool easeParams = cmd.transitionMs > 0 && !spec.create && !spec.hasEffect;
            uint32_t nowMs = millis();
            if (easeParams) target->snapshotParamsForTransition(nowMs);

            if (spec.hasEffect)     target->setEffect(spec.effectId);
            if (spec.hasParams)     target->getParamValues().setSlots(spec.slots);
            // Semantic params (AI path) resolve param names on the loop.
            if (spec.hasSpeed)      target->setSpeed(spec.speed);
            if (spec.hasIntensity)  target->setIntensity(spec.intensity);
            for (uint8_t i = 0; i < spec.colorCount && i < 3; i++) {
                target->setColor(i, spec.colors[i]);
            }
            if (spec.hasPalette)    target->setPalette(static_cast<PalettePreset>(spec.palette));
            if (spec.hasBrightness) target->setBrightness(spec.brightness);

            if (easeParams) target->startParamTransition(cmd.transitionMs, nowMs);
            break;
        }

        case CommandType::ReconfigureProtocols:
            // Applied on the loop (single writer) so protocol socket/config
            // teardown no longer races processProtocols()/mqtt.update() (P0.8).
            if (reconfigureProtocolsFn) reconfigureProtocolsFn();
            break;
    }

    // A processed command may have changed the layout or params; schedule a save.
    markSegmentsDirty();
}

Segment* LumeController::createSegment(uint16_t start, uint16_t length, bool reversed) {
    if (segmentCount >= MAX_SEGMENTS) {
        return nullptr;
    }
    
    // Bounds check
    if (start >= ledCount) {
        return nullptr;
    }
    
    uint16_t actualLength = min(length, (uint16_t)(ledCount - start));
    if (actualLength == 0) {
        return nullptr;
    }
    
    // Find lowest available ID (reuse deleted IDs)
    bool usedIds[MAX_SEGMENTS] = {false};
    for (uint8_t i = 0; i < segmentCount; i++) {
        uint8_t id = segments[i].getId();
        if (id < MAX_SEGMENTS) {
            usedIds[id] = true;
        }
    }
    
    uint8_t newId = 0;
    for (uint8_t i = 0; i < MAX_SEGMENTS; i++) {
        if (!usedIds[i]) {
            newId = i;
            break;
        }
    }
    
    // Add segment to next available slot
    Segment* seg = &segments[segmentCount];
    seg->setRange(leds, start, actualLength, reversed);
    seg->id = newId;
    segmentCount++;

    markSegmentsDirty();
    return seg;
}

Segment* LumeController::getSegment(uint8_t id) {
    for (uint8_t i = 0; i < segmentCount; i++) {
        if (segments[i].getId() == id) {
            return &segments[i];
        }
    }
    return nullptr;
}

Segment* LumeController::getSegmentByIndex(uint8_t index) {
    if (index >= segmentCount) return nullptr;
    return &segments[index];
}

bool LumeController::borrowWorkbuffer(uint8_t segmentIndex, uint16_t bytesNeeded) {
    if (kWorkbufferSize == 0) return false;                 // feature disabled
    if (segmentIndex >= segmentCount) return false;
    if (bytesNeeded > kWorkbufferSize) return false;        // won't fit
    if (workbufferOwner_ >= 0 && workbufferOwner_ != (int8_t)segmentIndex) {
        return false;                                       // already lent out
    }
    segments[segmentIndex].attachScratchpad(workbuffer_, kWorkbufferSize);
    workbufferOwner_ = (int8_t)segmentIndex;
    return true;
}

void LumeController::releaseWorkbuffer() {
    if (workbufferOwner_ >= 0 && workbufferOwner_ < (int8_t)segmentCount) {
        segments[workbufferOwner_].detachScratchpad();
    }
    workbufferOwner_ = -1;
}

bool LumeController::removeSegment(uint8_t id) {
    // Drop any borrow before the slots shift — the borrowed index would otherwise
    // dangle. (The copy-by-value inline-pad aliasing, TECH_DEBT P2, is repaired
    // below by rebindScratchpad after the shift.)
    releaseWorkbuffer();
    for (uint8_t i = 0; i < segmentCount; i++) {
        if (segments[i].getId() == id) {
            // Shift remaining segments down
            for (uint8_t j = i; j < segmentCount - 1; j++) {
                segments[j] = segments[j + 1];
            }
            segmentCount--;

            // Clear the removed slot
            segments[segmentCount] = Segment();

            // Each copy-assignment above duplicated a segment's inline scratchpad
            // bytes correctly but left its SegmentView.scratchpad aliasing the
            // SOURCE slot's pad (TECH_DEBT P2). Re-point every shifted survivor
            // back at its OWN inline pad, so two segments never render through one
            // physical scratchpad. Slots below i were untouched by the shift.
            for (uint8_t j = i; j < segmentCount; j++) {
                segments[j].rebindScratchpad();
            }

            markSegmentsDirty();
            return true;
        }
    }
    return false;
}

void LumeController::clearSegments() {
    releaseWorkbuffer();  // owner is about to be reset; drop the borrow first
    for (uint8_t i = 0; i < MAX_SEGMENTS; i++) {
        segments[i] = Segment();
    }
    segmentCount = 0;
    markSegmentsDirty();
}

uint8_t LumeController::getSegmentCount() const {
    return segmentCount;
}

Segment* LumeController::createFullStrip() {
    clearSegments();
    return createSegment(0, ledCount, false);
}

void LumeController::markSegmentsDirty() {
    if (suppressDirty_) return;
    segmentsDirty_ = true;
    lastSegmentChange_ = millis();
}

bool LumeController::takeSegmentSaveDue() {
    if (!segmentsDirty_) return false;
    if (millis() - lastSegmentChange_ < SEGMENT_SAVE_DEBOUNCE_MS) return false;
    segmentsDirty_ = false;
    return true;
}

void LumeController::serializeSegments(JsonDocument& doc) const {
    doc["v"] = 1;
    doc["ledCount"] = ledCount;
    doc["power"] = power;
    doc["brightness"] = globalBrightness;

    JsonArray segs = doc["segments"].to<JsonArray>();
    for (uint8_t i = 0; i < segmentCount; i++) {
        const Segment& seg = segments[i];
        if (!seg.isActive()) continue;

        JsonObject o = segs.add<JsonObject>();
        const Region& r = seg.getRegion();
        o["start"]      = r.start;
        o["length"]     = r.size();
        o["reverse"]    = seg.isReversed();
        o["brightness"] = seg.getBrightness();
        o["effect"]     = seg.getEffectId();

        const EffectInfo* info = seg.getEffect();
        if (info && info->hasSchema()) {
            JsonObject params = o["params"].to<JsonObject>();
            paramsToJson(params, *info->schema, seg.getParamValues());
        }
    }
}

bool LumeController::restoreSegments(const JsonDocument& doc) {
    JsonArrayConst segs = doc["segments"].as<JsonArrayConst>();
    if (segs.isNull() || segs.size() == 0) {
        return false;
    }

    suppressDirty_ = true;  // don't let the rebuild schedule a redundant save
    clearSegments();

    if (doc["power"].is<bool>())     setPower(doc["power"].as<bool>());
    if (doc["brightness"].is<int>()) setBrightness(doc["brightness"].as<uint8_t>());

    uint8_t restored = 0;
    for (JsonObjectConst s : segs) {
        uint16_t start  = s["start"].as<uint16_t>();
        uint16_t length = s["length"].as<uint16_t>();
        bool reverse    = s["reverse"].is<bool>() ? s["reverse"].as<bool>() : false;

        Segment* seg = createSegment(start, length, reverse);
        if (!seg) continue;

        if (s["effect"].is<const char*>()) {
            seg->setEffect(s["effect"].as<const char*>());
        }
        if (s["brightness"].is<int>()) {
            seg->setBrightness(s["brightness"].as<uint8_t>());
        }

        if (s["params"].is<JsonObjectConst>()) {
            const EffectInfo* info = seg->getEffect();
            if (info && info->hasSchema()) {
                paramsFromJson(seg->getParamValues(), *info->schema,
                               s["params"].as<JsonObjectConst>());
            }
        }
        restored++;
    }

    suppressDirty_ = false;
    return restored > 0;
}

void LumeController::clearUncoveredLeds() {
    for (uint16_t i = 0; i < ledCount; i++) {
        bool covered = false;
        for (uint8_t s = 0; s < segmentCount; s++) {
            const Segment& seg = segments[s];
            if (!seg.isActive()) continue;
            if (seg.getRegion().contains(i)) {
                covered = true;
                break;
            }
        }
        if (!covered) {
            leds[i] = CRGB::Black;
        }
    }
}

// --- Protocol management ---

void LumeController::registerProtocol(IProtocol* protocol) {
    if (!protocol) return;
    
    if (protocolCount_ >= MAX_PROTOCOLS) {
        LOG_WARN(LogTag::LED, "Max protocols reached, cannot register %s", protocol->name());
        return;
    }
    
    protocols_[protocolCount_++] = protocol;
    LOG_INFO(LogTag::LED, "Registered protocol: %s", protocol->name());
}

void LumeController::processProtocols() {
    // Check each registered protocol for incoming data
    for (uint8_t i = 0; i < protocolCount_; i++) {
        IProtocol* proto = protocols_[i];
        if (!proto || !proto->isEnabled()) continue;
        
        // Update protocol (processes incoming packets)
        proto->loop();
        
        // Check if this protocol has a frame ready
        if (proto->hasData()) {
            // Copy protocol buffer to LED array
            const CRGB* buffer = proto->getBuffer();
            uint16_t count = min(proto->getBufferSize(), ledCount);
            
            memcpy(leds, buffer, count * sizeof(CRGB));
            proto->clearData();
            
            protocolActive_ = true;
            activeProtocol_ = proto;
            return;  // Only one protocol can be active at a time
        }
    }
    
    // Check if active protocol has timed out (no longer active)
    if (protocolActive_ && activeProtocol_) {
        if (!activeProtocol_->isActive()) {
            LOG_INFO(LogTag::LED, "Protocol %s timeout - returning to effects", 
                     activeProtocol_->name());
            protocolActive_ = false;
            activeProtocol_ = nullptr;
        }
    }
}

} // namespace lume
