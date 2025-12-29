#ifndef LUME_PROTOCOL_SACN_H
#define LUME_PROTOCOL_SACN_H

#include "protocol.h"
#include "../sacn_receiver.h"
#include "../constants.h"

namespace lume {

/**
 * SacnProtocol - Protocol adapter for sACN/E1.31
 * 
 * Wraps the existing SacnReceiver and provides:
 * - Thread-safe buffering (receiver writes to buffer, main loop reads)
 * - Protocol interface compliance
 * - Timeout handling for returning control to effects
 * 
 * Usage:
 *   SacnProtocol sacn;
 *   sacn.configure(1, 1, false);  // Universe 1, 1 universe, multicast
 *   sacn.begin();
 *   
 *   // In loop:
 *   sacn.update();
 *   if (sacn.hasFrameReady()) {
 *       memcpy(leds, sacn.getBuffer(), sacn.getBufferSize() * sizeof(CRGB));
 *       sacn.clearFrameReady();
 *   }
 */
class SacnProtocol : public Protocol {
public:
    SacnProtocol();
    
    // --- Configuration (call before begin) ---
    
    void configure(uint16_t startUniverse, uint8_t universeCount, 
                   bool unicastMode, uint16_t startChannel = 1);
    
    // --- Protocol interface ---
    
    bool begin() override;
    void stop() override;
    
    void setEnabled(bool enabled) override;
    bool isEnabled() const override;
    
    bool update() override;
    bool hasTimedOut(uint32_t timeoutMs = 5000) const override;
    bool isActive() const override;
    
    bool hasFrameReady() const override;
    const CRGB* getBuffer() const override;
    uint16_t getBufferSize() const override;
    void clearFrameReady() override;
    
    const char* getName() const override { return "sACN"; }
    uint32_t getPacketCount() const override;
    uint32_t getLastPacketTime() const override;
    
    // --- sACN-specific accessors ---
    
    uint16_t getStartUniverse() const { return startUniverse_; }
    uint8_t getUniverseCount() const { return universeCount_; }
    bool isUnicastMode() const { return unicastMode_; }
    const char* getActiveSourceName() const;
    uint8_t getActivePriority() const;

private:
    // Configuration
    uint16_t startUniverse_;
    uint8_t universeCount_;
    bool unicastMode_;
    uint16_t startChannel_;
    
    // Thread-safe buffer for received data
    ProtocolBuffer<MAX_LED_COUNT> buffer_;
    
    // Working buffer for multi-universe assembly
    CRGB workBuffer_[MAX_LED_COUNT];
    uint16_t ledCount_;
    
    // State
    bool active_;
    
    // Assemble multi-universe data into working buffer
    void assembleLeds();
};

// Global instance
extern SacnProtocol sacnProtocol;

} // namespace lume

#endif // LUME_PROTOCOL_SACN_H
