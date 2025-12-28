#ifndef SACN_RECEIVER_H
#define SACN_RECEIVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <FastLED.h>
#include "constants.h"

// E1.31 (sACN) Constants
#define SACN_HEADER_SIZE 126
#define SACN_MAX_CHANNELS 512
#define SACN_MAX_UNIVERSES 8      // Support up to 8 universes (8 * 170 = 1360 RGB LEDs)
#define SACN_SOURCE_TIMEOUT 2500  // Source considered offline after 2.5s (E1.31 spec)
#define SACN_MAX_SOURCES 4        // Track up to 4 sources for priority handling

// sACN packet vectors
#define SACN_VECTOR_ROOT 0x00000004
#define SACN_VECTOR_FRAME 0x00000002
#define SACN_VECTOR_DMP 0x02

// Options flags (byte 112)
#define SACN_OPT_PREVIEW 0x80     // Preview data - don't output
#define SACN_OPT_STREAM_TERM 0x40 // Stream terminated

// Source tracking for priority handling
struct SacnSource {
    uint8_t cid[16];              // Unique source identifier
    char name[64];                // Human-readable source name
    uint8_t priority;             // 0-200, higher wins
    uint8_t lastSequence;
    unsigned long lastSeen;
    bool active;
};

// Per-universe data
struct SacnUniverse {
    uint16_t universe;
    uint8_t dmxData[SACN_MAX_CHANNELS];
    uint16_t channelCount;
    uint8_t activePriority;       // Current winning priority
    uint8_t activeSourceIndex;    // Which source is currently winning
    unsigned long lastPacketTime;
    unsigned long packetCount;
    bool hasData;
};

class SacnReceiver {
public:
    SacnReceiver();
    
    // Initialize receiver with universe range
    // For >170 RGB LEDs, use multiple consecutive universes
    bool begin(uint16_t startUniverse = 1, uint8_t universeCount = 1);
    
    // Stop receiver
    void stop();
    
    // Check for new data - call this in loop()
    // Returns true if new DMX data was received on any universe
    bool update();
    
    // Apply received DMX data to LED array (handles multi-universe automatically)
    // Maps consecutive universes to consecutive LED ranges
    // leds: pointer to CRGB array
    // numLeds: total number of LEDs
    // startChannel: first DMX channel in first universe (1-512)
    void applyToLeds(CRGB* leds, uint16_t numLeds, uint16_t startChannel = 1);
    
    // Get raw DMX data for specific universe (0-indexed from start universe)
    uint8_t* getDmxData(uint8_t universeIndex = 0);
    uint16_t getChannelCount(uint8_t universeIndex = 0);
    
    // Configuration
    void setEnabled(bool en);
    void setUnicastMode(bool unicast);  // true = unicast only, false = multicast
    void setPreviewMode(bool preview);  // true = accept preview packets
    
    // Status
    bool isReceiving();
    bool isEnabled() { return enabled; }
    bool isUnicast() { return unicastMode; }
    uint8_t getUniverseCount() { return universeCount; }
    uint16_t getStartUniverse() { return startUniverse; }
    
    // Get active source info
    const char* getActiveSourceName(uint8_t universeIndex = 0);
    uint8_t getActivePriority(uint8_t universeIndex = 0);
    
    // Stats (for compatibility, returns totals/first universe)
    unsigned long getPacketCount() { return getTotalPacketCount(); }
    unsigned long getLastPacketTime() { return lastAnyPacketTime; }
    uint16_t getUniverse() { return startUniverse; }
    
    unsigned long getTotalPacketCount();
    
    // Timeout - if no packets for this long, consider connection lost
    bool hasTimedOut(unsigned long timeoutMs = 5000);

private:
    WiFiUDP udp;
    uint8_t packetBuffer[SACN_HEADER_SIZE + SACN_MAX_CHANNELS];
    
    // Universe management
    SacnUniverse universes[SACN_MAX_UNIVERSES];
    uint16_t startUniverse;
    uint8_t universeCount;
    
    // Source tracking (shared across universes)
    SacnSource sources[SACN_MAX_SOURCES];
    
    // State
    bool enabled;
    bool initialized;
    bool unicastMode;
    bool acceptPreview;
    unsigned long totalPacketCount;
    unsigned long lastAnyPacketTime;
    
    // Parse incoming sACN packet
    bool parsePacket(int packetSize);
    
    // Source management
    int findOrCreateSource(const uint8_t* cid, const char* name, uint8_t priority);
    void cleanupStaleSources();
    
    // Multicast management
    bool joinMulticast(uint16_t universe);
    void joinAllMulticast();
    void leaveAllMulticast();
    IPAddress getMulticastIP(uint16_t universe);
    
    // Get universe index from universe number (-1 if not in range)
    int getUniverseIndex(uint16_t universe);
};

extern SacnReceiver sacnReceiver;

#endif // SACN_RECEIVER_H
