#ifndef LUME_PROTOCOL_SACN_H
#define LUME_PROTOCOL_SACN_H

#include "protocol.h"
#include <WiFiUdp.h>
#include "../constants.h"

namespace lume {

// E1.31 (sACN) Constants
constexpr uint16_t SACN_PORT = 5568;
constexpr uint16_t SACN_HEADER_SIZE = 126;
constexpr uint16_t SACN_MAX_CHANNELS = 512;
constexpr uint8_t SACN_MAX_UNIVERSES = 8;
constexpr uint8_t SACN_MAX_SOURCES = 4;
constexpr uint32_t SACN_SOURCE_TIMEOUT_MS = 2500;

// sACN packet vectors
constexpr uint32_t SACN_VECTOR_ROOT = 0x00000004;
constexpr uint32_t SACN_VECTOR_FRAME = 0x00000002;
constexpr uint8_t SACN_VECTOR_DMP = 0x02;

// Options flags
constexpr uint8_t SACN_OPT_PREVIEW = 0x80;
constexpr uint8_t SACN_OPT_STREAM_TERM = 0x40;

// Source tracking for priority handling
struct SacnSource {
    uint8_t cid[16];              // Unique source identifier
    char name[64];                // Human-readable source name
    uint8_t priority;             // 0-200, higher wins
    uint8_t lastSequence;
    uint32_t lastSeen;
    bool active;
};

// Per-universe data
struct SacnUniverse {
    uint16_t universe;
    uint8_t dmxData[SACN_MAX_CHANNELS];
    uint16_t channelCount;
    uint8_t activePriority;
    uint8_t activeSourceIndex;
    uint32_t lastPacketTime;
    uint32_t packetCount;
    bool hasData;
};

/**
 * SacnProtocol - Self-contained sACN/E1.31 protocol implementation
 * 
 * Handles:
 * - UDP socket management
 * - Multicast group join/leave
 * - E1.31 packet parsing
 * - Multi-universe support
 * - Source priority handling
 * - Thread-safe buffer for main loop consumption
 * 
 * Follows the Protocol interface and single-writer architecture:
 * - UDP callback writes to internal buffer
 * - Main loop reads via getBuffer()
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
    uint32_t getPacketCount() const override { return totalPacketCount_; }
    uint32_t getLastPacketTime() const override { return lastAnyPacketTime_; }
    
    // --- sACN-specific accessors ---
    
    uint16_t getStartUniverse() const { return startUniverse_; }
    uint8_t getUniverseCount() const { return universeCount_; }
    bool isUnicastMode() const { return unicastMode_; }
    const char* getActiveSourceName() const;
    uint8_t getActivePriority() const;

private:
    // UDP socket
    WiFiUDP udp_;
    uint8_t packetBuffer_[SACN_HEADER_SIZE + SACN_MAX_CHANNELS];
    
    // Configuration
    uint16_t startUniverse_;
    uint8_t universeCount_;
    bool unicastMode_;
    uint16_t startChannel_;
    
    // Universe management
    SacnUniverse universes_[SACN_MAX_UNIVERSES];
    
    // Source tracking
    SacnSource sources_[SACN_MAX_SOURCES];
    
    // State
    bool enabled_;
    bool initialized_;
    bool acceptPreview_;
    uint32_t totalPacketCount_;
    uint32_t lastAnyPacketTime_;
    
    // Thread-safe output buffer
    ProtocolBuffer<MAX_LED_COUNT> buffer_;
    
    // Working buffer for multi-universe assembly
    CRGB workBuffer_[MAX_LED_COUNT];
    uint16_t ledCount_;
    bool active_;
    
    // Packet parsing
    bool parsePacket(int packetSize);
    
    // Source management
    int findOrCreateSource(const uint8_t* cid, const char* name, uint8_t priority);
    void cleanupStaleSources();
    
    // Multicast management
    void joinAllMulticast();
    void leaveAllMulticast();
    IPAddress getMulticastIP(uint16_t universe);
    
    // Universe helpers
    int getUniverseIndex(uint16_t universe);
    void assembleLeds();
};

// Global instance
extern SacnProtocol sacnProtocol;

} // namespace lume

#endif // LUME_PROTOCOL_SACN_H
