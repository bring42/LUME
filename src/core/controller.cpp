/**
 * LumeController implementation
 */

#include "controller.h"
#include "../protocols/protocol.h"
#include "../logging.h"

namespace lume {

// Global instance
LumeController controller;

LumeController::LumeController()
    : ledCount(0)
    , segmentCount(0)
    , nextSegmentId(0)
    , power(true)
    , globalBrightness(255)
    , protocolCount_(0)
    , protocolActive_(false)
    , activeProtocol_(nullptr)
    , targetFps(DEFAULT_FPS)
    , frameCounter(0)
    , lastFrameTime(0)
    , actualFps(0)
    , fpsUpdateTime(0)
    , fpsFrameCount(0) {
    
    memset(leds, 0, sizeof(leds));
    memset(protocols_, 0, sizeof(protocols_));
}

void LumeController::begin(uint16_t count) {
    ledCount = min(count, (uint16_t)MAX_LED_COUNT);
    
    // Initialize command queue
    if (!commandQueue.begin()) {
        LOG_ERROR(LogTag::LED, "Failed to initialize command queue");
    }
    
    // Initialize FastLED
    // Note: LED_DATA_PIN is defined in constants.h
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, ledCount);
    FastLED.setBrightness(globalBrightness);
    FastLED.setCorrection(TypicalLEDStrip);
    FastLED.setMaxPowerInVoltsAndMilliamps(LED_VOLTAGE, LED_MAX_MILLIAMPS);
    
    FastLED.clear();
    FastLED.show();
    
    lastFrameTime = millis();
    fpsUpdateTime = millis();
}

void LumeController::setLedCount(uint16_t count) {
    ledCount = min(count, (uint16_t)MAX_LED_COUNT);
    
    // Clear any LEDs beyond new count
    for (uint16_t i = ledCount; i < MAX_LED_COUNT; i++) {
        leds[i] = CRGB::Black;
    }
    
    // Invalidate segments that extend beyond new count
    for (uint8_t i = 0; i < segmentCount; i++) {
        // Segments would need to be reconfigured by user
    }
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
    
    // Clear or handle power off
    if (!power) {
        FastLED.clear();
        FastLED.show();
        return;
    }
    
    // Check protocols for incoming data
    processProtocols();
    
    // If a protocol is active, it has already written to LEDs - just show
    if (protocolActive_) {
        FastLED.show();
        frameCounter++;
        return;
    }
    
    // Clear LED array before rendering segments
    // (Alternative: only clear if segments don't cover everything)
    FastLED.clear();
    
    // Update all active segments
    for (uint8_t i = 0; i < segmentCount; i++) {
        if (segments[i].isActive()) {
            segments[i].update(frameCounter);
            
            // Handle blending if not Replace mode
            if (segments[i].getBlendMode() != BlendMode::Replace) {
                blendSegment(segments[i]);
            }
        }
    }
    
    // Show the result
    FastLED.show();
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
                    seg->setSecondaryColor(cmd.data.color.toCRGB());
                } else {
                    seg->setPrimaryColor(cmd.data.color.toCRGB());
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
            setPower(cmd.data.power);
            LOG_INFO(LogTag::LED, "Power -> %s", cmd.data.power ? "ON" : "OFF");
            break;
            
        case CommandType::SetGlobalBrightness:
            setBrightness(cmd.data.value8);
            break;
            
        case CommandType::ApplyEffectSpec:
        case CommandType::SaveScene:
        case CommandType::LoadScene:
            // TODO: Implement in later phase
            LOG_WARN(LogTag::LED, "Command type %d not yet implemented", static_cast<int>(cmd.type));
            break;
    }
}

void LumeController::show() {
    FastLED.show();
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
    
    // Find first inactive slot or use next slot
    Segment* seg = &segments[segmentCount];
    seg->setRange(leds, start, actualLength, reversed);
    seg->id = nextSegmentId++;
    segmentCount++;
    
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

bool LumeController::removeSegment(uint8_t id) {
    for (uint8_t i = 0; i < segmentCount; i++) {
        if (segments[i].getId() == id) {
            // Shift remaining segments down
            for (uint8_t j = i; j < segmentCount - 1; j++) {
                segments[j] = segments[j + 1];
            }
            segmentCount--;
            
            // Clear the removed slot
            segments[segmentCount] = Segment();
            return true;
        }
    }
    return false;
}

void LumeController::clearSegments() {
    for (uint8_t i = 0; i < MAX_SEGMENTS; i++) {
        segments[i] = Segment();
    }
    segmentCount = 0;
}

uint8_t LumeController::getSegmentCount() const {
    return segmentCount;
}

Segment* LumeController::createFullStrip() {
    clearSegments();
    return createSegment(0, ledCount, false);
}

void LumeController::blendSegment(Segment& seg) {
    // For overlapping segments with non-Replace blend modes
    // This would need a secondary buffer to work properly
    // For now, we just support Replace mode (direct write)
    
    // TODO: Implement proper blending with secondary buffer if needed
    // BlendMode mode = seg.getBlendMode();
    // switch (mode) {
    //     case BlendMode::Add:
    //         // Additive blending
    //         break;
    //     case BlendMode::Average:
    //         // 50/50 blend
    //         break;
    //     case BlendMode::Max:
    //         // Take maximum
    //         break;
    //     default:
    //         break;
    // }
}

// --- Protocol management ---

void LumeController::registerProtocol(Protocol* protocol) {
    if (!protocol) return;
    
    if (protocolCount_ >= MAX_PROTOCOLS) {
        LOG_WARN(LogTag::LED, "Max protocols reached, cannot register %s", protocol->getName());
        return;
    }
    
    protocols_[protocolCount_++] = protocol;
    LOG_INFO(LogTag::LED, "Registered protocol: %s", protocol->getName());
}

const char* LumeController::getActiveProtocolName() const {
    if (activeProtocol_) {
        return activeProtocol_->getName();
    }
    return nullptr;
}

void LumeController::processProtocols() {
    // Check each registered protocol for incoming data
    for (uint8_t i = 0; i < protocolCount_; i++) {
        Protocol* proto = protocols_[i];
        if (!proto || !proto->isEnabled()) continue;
        
        // Update protocol (processes incoming packets)
        proto->update();
        
        // Check if this protocol has a frame ready
        if (proto->hasFrameReady()) {
            // Copy protocol buffer to LED array
            const CRGB* buffer = proto->getBuffer();
            uint16_t count = min(proto->getBufferSize(), ledCount);
            
            memcpy(leds, buffer, count * sizeof(CRGB));
            proto->clearFrameReady();
            
            protocolActive_ = true;
            activeProtocol_ = proto;
            return;  // Only one protocol can be active at a time
        }
    }
    
    // Check if active protocol has timed out
    if (protocolActive_ && activeProtocol_) {
        if (activeProtocol_->hasTimedOut(PROTOCOL_TIMEOUT_MS)) {
            LOG_INFO(LogTag::LED, "Protocol %s timeout - returning to effects", 
                     activeProtocol_->getName());
            protocolActive_ = false;
            activeProtocol_ = nullptr;
        }
    }
}

} // namespace lume
