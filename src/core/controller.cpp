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
    // Frame rate limiting
    uint32_t now = millis();
    uint32_t frameInterval = 1000 / targetFps;
    
    if (now - lastFrameTime < frameInterval) {
        return;  // Not time for next frame yet
    }
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

    // Advance the premium easing engine (eased global brightness — the user's
    // level). Single-writer: the loop steps it toward its target; idle is a
    // cheap no-op. Output brightness is applied below, composed with the power
    // envelope, so this only updates the stored level.
    if (brightnessTransition_.isActive()) {
        globalBrightness = brightnessTransition_.advance(now);
    }

    // Power-off rider: when a brightness fade carrying the "power off at zero"
    // policy settles, switch the strip off. This is all that remains of
    // "nightlight" in the core — the fade itself is the shared easing engine
    // (advanced above), which has already left globalBrightness at the target.
    if (powerOffWhenSettled_ && !brightnessTransition_.isActive()) {
        powerOffWhenSettled_ = false;
        setPower(false);
        LOG_INFO(LogTag::LED, "Brightness fade settled -> power off");
    }

    // Compose the perceptual output level: the user's level scaled by the power
    // fade envelope (255 = fully on, 0 = off), all still in perceptual space.
    // globalBrightness stays pristine, so a power toggle never loses the level.
    uint8_t powerEnv = powerFade_.advance(now);
    uint8_t perceptual = (powerEnv == 255) ? globalBrightness
                                           : scale8(globalBrightness, powerEnv);
    // Gamma-encode to PWM just before the driver so that equal steps in the
    // (linearly-eased) perceptual level produce equal *perceived* steps — the
    // difference between a fade that reads dead at the top and one that glides.
    output_->setBrightness(applyGamma_video(perceptual, LED_GAMMA));

    // Flip logical power off once a fade-out envelope reaches zero — this is
    // when we stop rendering (below) instead of burning cycles on a black frame.
    if (powerOffWhenFadeSettles_ && !powerFade_.isActive()) {
        powerOffWhenFadeSettles_ = false;
        power = false;
        LOG_INFO(LogTag::LED, "Power fade settled -> off");
    }

    // Clear or handle power off
    if (!power) {
        output_->clear();
        output_->show();
        return;
    }
    
    // Check protocols for incoming data
    processProtocols();
    
    // If a protocol is active, it has already written to LEDs - just show
    if (protocolActive_) {
        output_->show();
        frameCounter++;
        return;
    }

    // Drain a staged direct-pixel frame (debug /api/pixels). Single-writer: the
    // web task only wrote directPixels_ (atomic ready flag); leds[] is touched
    // here, on the loop, then shown once. One-frame overlay — segments resume
    // next frame (P0.1).
    if (directPixels_.isReady()) {
        uint16_t count = min(directPixels_.getLedCount(), ledCount);
        memcpy(leds, directPixels_.getBuffer(), count * sizeof(CRGB));
        directPixels_.clearReady();
        output_->show();
        frameCounter++;
        return;
    }

    // Clear only the LEDs not owned by an active segment. Effects own their
    // canvas (fill or fade) and many build fade-trails by reading the previous
    // frame (sinelon, wave, comet...). A blanket FastLED.clear() here
    // would wipe that history every frame; clearing only gaps keeps it intact
    // while still blacking out uncovered pixels (gaps, removed segments).
    clearUncoveredLeds();

    // Update all active segments. `now` drives per-segment param/color easing.
    for (uint8_t i = 0; i < segmentCount; i++) {
        if (segments[i].isActive()) {
            segments[i].update(frameCounter, now);
        }
    }
    
    // Show the result
    output_->show();
    frameCounter++;
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
    // Drop any borrow before the slots shift — indices (and the inline-pad
    // aliasing of the copy-by-value move, TECH_DEBT P2) would otherwise dangle.
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
