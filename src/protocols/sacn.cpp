/**
 * SacnProtocol - Protocol adapter for sACN/E1.31
 * 
 * Provides thread-safe buffering between the sACN UDP receiver
 * (which runs on network task) and the main loop.
 */

#include "sacn.h"
#include "../logging.h"

namespace lume {

// Global instance
SacnProtocol sacnProtocol;

SacnProtocol::SacnProtocol()
    : startUniverse_(1)
    , universeCount_(1)
    , unicastMode_(false)
    , startChannel_(1)
    , ledCount_(0)
    , active_(false) {
    memset(workBuffer_, 0, sizeof(workBuffer_));
}

void SacnProtocol::configure(uint16_t startUniverse, uint8_t universeCount,
                              bool unicastMode, uint16_t startChannel) {
    startUniverse_ = startUniverse;
    universeCount_ = universeCount;
    unicastMode_ = unicastMode;
    startChannel_ = startChannel;
    
    // Calculate max LEDs based on universe count
    // First universe: (512 - startChannel) / 3 LEDs
    // Subsequent universes: 170 LEDs each
    uint16_t firstUniLeds = (SACN_MAX_CHANNELS - (startChannel_ - 1)) / 3;
    ledCount_ = firstUniLeds + (universeCount_ - 1) * 170;
    ledCount_ = min(ledCount_, (uint16_t)MAX_LED_COUNT);
    
    LOG_DEBUG(LogTag::SACN, "Protocol configured: uni %d-%d, ch %d, max %d LEDs",
              startUniverse_, startUniverse_ + universeCount_ - 1,
              startChannel_, ledCount_);
}

bool SacnProtocol::begin() {
    // Configure the underlying receiver
    sacnReceiver.setUnicastMode(unicastMode_);
    
    // Start the receiver
    if (!sacnReceiver.begin(startUniverse_, universeCount_)) {
        LOG_ERROR(LogTag::SACN, "Protocol failed to start receiver");
        return false;
    }
    
    LOG_INFO(LogTag::SACN, "Protocol started");
    return true;
}

void SacnProtocol::stop() {
    sacnReceiver.stop();
    active_ = false;
    LOG_INFO(LogTag::SACN, "Protocol stopped");
}

void SacnProtocol::setEnabled(bool enabled) {
    sacnReceiver.setEnabled(enabled);
    if (!enabled) {
        active_ = false;
    }
}

bool SacnProtocol::isEnabled() const {
    return sacnReceiver.isEnabled();
}

bool SacnProtocol::update() {
    // Update the underlying receiver (processes UDP packets)
    if (sacnReceiver.update()) {
        // New data received - assemble into working buffer
        assembleLeds();
        
        // Write to thread-safe buffer
        buffer_.write(workBuffer_, ledCount_);
        active_ = true;
        return true;
    }
    
    // Check for timeout
    if (active_ && sacnReceiver.hasTimedOut(5000)) {
        LOG_INFO(LogTag::SACN, "Protocol timeout - releasing control");
        active_ = false;
    }
    
    return false;
}

bool SacnProtocol::hasTimedOut(uint32_t timeoutMs) const {
    return buffer_.hasTimedOut(timeoutMs);
}

bool SacnProtocol::isActive() const {
    return active_;
}

bool SacnProtocol::hasFrameReady() const {
    return buffer_.isReady();
}

const CRGB* SacnProtocol::getBuffer() const {
    return buffer_.getBuffer();
}

uint16_t SacnProtocol::getBufferSize() const {
    return buffer_.getLedCount();
}

void SacnProtocol::clearFrameReady() {
    buffer_.clearReady();
}

uint32_t SacnProtocol::getPacketCount() const {
    return sacnReceiver.getPacketCount();
}

uint32_t SacnProtocol::getLastPacketTime() const {
    return sacnReceiver.getLastPacketTime();
}

const char* SacnProtocol::getActiveSourceName() const {
    return sacnReceiver.getActiveSourceName(0);
}

uint8_t SacnProtocol::getActivePriority() const {
    return sacnReceiver.getActivePriority(0);
}

void SacnProtocol::assembleLeds() {
    // Assemble LED data from potentially multiple universes
    // This mirrors the logic in SacnReceiver::applyToLeds but writes to our buffer
    
    uint16_t startChannelOffset = startChannel_ - 1;  // Convert to 0-indexed
    uint16_t ledsPerUniverse = (SACN_MAX_CHANNELS - startChannelOffset) / 3;
    
    uint16_t ledIndex = 0;
    
    for (uint8_t uniIdx = 0; uniIdx < universeCount_ && ledIndex < ledCount_; uniIdx++) {
        uint8_t* dmxData = sacnReceiver.getDmxData(uniIdx);
        uint16_t channelCount = sacnReceiver.getChannelCount(uniIdx);
        
        if (!dmxData || channelCount == 0) {
            // No data for this universe yet, skip its LED range
            if (uniIdx == 0) {
                ledIndex += ledsPerUniverse;
            } else {
                ledIndex += 170;
            }
            continue;
        }
        
        // First universe uses startChannel offset, subsequent start at 0
        uint16_t chOffset = (uniIdx == 0) ? startChannelOffset : 0;
        uint16_t ledsThisUni = (uniIdx == 0) ? ledsPerUniverse : 170;
        
        for (uint16_t i = 0; i < ledsThisUni && ledIndex < ledCount_; i++) {
            uint16_t rChannel = chOffset + (i * 3);
            uint16_t gChannel = rChannel + 1;
            uint16_t bChannel = rChannel + 2;
            
            if (bChannel < SACN_MAX_CHANNELS && bChannel < channelCount) {
                workBuffer_[ledIndex].r = dmxData[rChannel];
                workBuffer_[ledIndex].g = dmxData[gChannel];
                workBuffer_[ledIndex].b = dmxData[bChannel];
            }
            ledIndex++;
        }
    }
}

} // namespace lume
