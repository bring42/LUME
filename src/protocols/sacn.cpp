/**
 * SacnProtocol - Self-contained sACN/E1.31 protocol implementation
 * 
 * This is a complete rewrite that handles UDP directly, following
 * the Protocol interface and single-writer architecture.
 */

#include "sacn.h"
#include "../logging.h"
#include <WiFi.h>

namespace lume {

// Global instance
SacnProtocol sacnProtocol;

// ACN packet identifier (bytes 4-15)
static const uint8_t ACN_ID[] = {0x41, 0x53, 0x43, 0x2d, 0x45, 0x31, 0x2e, 0x31, 0x37, 0x00, 0x00, 0x00};

SacnProtocol::SacnProtocol()
    : startUniverse_(1)
    , universeCount_(1)
    , unicastMode_(false)
    , startChannel_(1)
    , enabled_(false)
    , initialized_(false)
    , acceptPreview_(false)
    , totalPacketCount_(0)
    , lastAnyPacketTime_(0)
    , ledCount_(0)
    , active_(false) {
    memset(packetBuffer_, 0, sizeof(packetBuffer_));
    memset(universes_, 0, sizeof(universes_));
    memset(sources_, 0, sizeof(sources_));
    memset(workBuffer_, 0, sizeof(workBuffer_));
}

void SacnProtocol::configure(uint16_t startUniverse, uint8_t universeCount,
                              bool unicastMode, uint16_t startChannel) {
    startUniverse_ = startUniverse;
    universeCount_ = min(universeCount, SACN_MAX_UNIVERSES);
    unicastMode_ = unicastMode;
    startChannel_ = max((uint16_t)1, min(startChannel, (uint16_t)512));
    
    // Calculate max LEDs based on universe count
    uint16_t firstUniLeds = (SACN_MAX_CHANNELS - (startChannel_ - 1)) / 3;
    ledCount_ = firstUniLeds + (universeCount_ - 1) * 170;
    ledCount_ = min(ledCount_, (uint16_t)MAX_LED_COUNT);
    
    LOG_DEBUG(LogTag::SACN, "Configured: uni %d-%d, ch %d, max %d LEDs",
              startUniverse_, startUniverse_ + universeCount_ - 1,
              startChannel_, ledCount_);
}

bool SacnProtocol::begin() {
    if (universeCount_ == 0 || universeCount_ > SACN_MAX_UNIVERSES) {
        LOG_ERROR(LogTag::SACN, "Invalid universe count: %d", universeCount_);
        return false;
    }
    
    if (startUniverse_ == 0 || startUniverse_ > 63999) {
        LOG_ERROR(LogTag::SACN, "Invalid start universe: %d", startUniverse_);
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        LOG_WARN(LogTag::SACN, "WiFi not connected");
        return false;
    }
    
    // Initialize universe structures
    for (uint8_t i = 0; i < universeCount_; i++) {
        universes_[i].universe = startUniverse_ + i;
        memset(universes_[i].dmxData, 0, SACN_MAX_CHANNELS);
        universes_[i].channelCount = 0;
        universes_[i].activePriority = 0;
        universes_[i].activeSourceIndex = 0xFF;
        universes_[i].lastPacketTime = 0;
        universes_[i].packetCount = 0;
        universes_[i].hasData = false;
    }
    
    // Clear source tracking
    for (uint8_t i = 0; i < SACN_MAX_SOURCES; i++) {
        sources_[i].active = false;
    }
    
    totalPacketCount_ = 0;
    lastAnyPacketTime_ = 0;
    
    // Start UDP listener
    if (!udp_.begin(SACN_PORT)) {
        LOG_ERROR(LogTag::SACN, "Failed to start UDP on port %d", SACN_PORT);
        return false;
    }
    
    // Join multicast groups (unless unicast mode)
    if (!unicastMode_) {
        joinAllMulticast();
    }
    
    initialized_ = true;
    enabled_ = true;
    
    LOG_INFO(LogTag::SACN, "Started: universes %d-%d, mode=%s",
             startUniverse_, startUniverse_ + universeCount_ - 1,
             unicastMode_ ? "unicast" : "multicast");
    
    return true;
}

void SacnProtocol::stop() {
    if (initialized_) {
        if (!unicastMode_) {
            leaveAllMulticast();
        }
        udp_.stop();
        initialized_ = false;
        active_ = false;
        LOG_INFO(LogTag::SACN, "Stopped");
    }
}

void SacnProtocol::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled) {
        for (uint8_t i = 0; i < universeCount_; i++) {
            universes_[i].hasData = false;
        }
        active_ = false;
    }
    LOG_INFO(LogTag::SACN, "%s", enabled ? "Enabled" : "Disabled");
}

bool SacnProtocol::isEnabled() const {
    return enabled_;
}

bool SacnProtocol::update() {
    if (!initialized_ || !enabled_) {
        return false;
    }
    
    // Periodically clean up stale sources
    static uint32_t lastCleanup = 0;
    if (millis() - lastCleanup > 1000) {
        cleanupStaleSources();
        lastCleanup = millis();
    }
    
    bool receivedAny = false;
    
    // Process all available packets (handle burst traffic)
    int maxPacketsPerUpdate = 10;
    while (maxPacketsPerUpdate-- > 0) {
        int packetSize = udp_.parsePacket();
        if (packetSize == 0) {
            break;
        }
        
        if (packetSize < SACN_HEADER_SIZE) {
            udp_.flush();
            continue;
        }
        
        if (packetSize > (int)sizeof(packetBuffer_)) {
            udp_.flush();
            continue;
        }
        
        int bytesRead = udp_.read(packetBuffer_, sizeof(packetBuffer_));
        udp_.flush();
        
        if (bytesRead < SACN_HEADER_SIZE) {
            continue;
        }
        
        if (parsePacket(bytesRead)) {
            receivedAny = true;
        }
    }
    
    if (receivedAny) {
        // Assemble multi-universe data into working buffer
        assembleLeds();
        
        // Write to thread-safe buffer
        buffer_.write(workBuffer_, ledCount_);
        active_ = true;
    }
    
    // Check for timeout
    if (active_ && hasTimedOut(5000)) {
        LOG_INFO(LogTag::SACN, "Timeout - releasing control");
        active_ = false;
    }
    
    return receivedAny;
}

bool SacnProtocol::parsePacket(int packetSize) {
    // Check ACN packet identifier (bytes 4-15)
    if (memcmp(&packetBuffer_[4], ACN_ID, 12) != 0) {
        return false;
    }
    
    // Check root vector (E1.31 data packet = 0x00000004)
    uint32_t rootVector = ((uint32_t)packetBuffer_[18] << 24) |
                          ((uint32_t)packetBuffer_[19] << 16) |
                          ((uint32_t)packetBuffer_[20] << 8) |
                          packetBuffer_[21];
    if (rootVector != SACN_VECTOR_ROOT) {
        return false;
    }
    
    // Check frame vector (DMP = 0x00000002)
    uint32_t frameVector = ((uint32_t)packetBuffer_[40] << 24) |
                           ((uint32_t)packetBuffer_[41] << 16) |
                           ((uint32_t)packetBuffer_[42] << 8) |
                           packetBuffer_[43];
    if (frameVector != SACN_VECTOR_FRAME) {
        return false;
    }
    
    // Extract source info
    const uint8_t* cid = &packetBuffer_[22];
    const char* sourceName = (const char*)&packetBuffer_[44];
    uint8_t priority = packetBuffer_[108];
    uint8_t sequence = packetBuffer_[111];
    uint8_t options = packetBuffer_[112];
    
    // Check options
    if ((options & SACN_OPT_PREVIEW) && !acceptPreview_) {
        return false;
    }
    
    if (options & SACN_OPT_STREAM_TERM) {
        return false;
    }
    
    // Get universe from packet
    uint16_t packetUniverse = ((uint16_t)packetBuffer_[113] << 8) | packetBuffer_[114];
    
    // Check if this universe is in our range
    int uniIndex = getUniverseIndex(packetUniverse);
    if (uniIndex < 0) {
        return false;
    }
    
    SacnUniverse& uni = universes_[uniIndex];
    
    // Find or create source entry
    int sourceIndex = findOrCreateSource(cid, sourceName, priority);
    if (sourceIndex < 0) {
        return false;
    }
    
    SacnSource& source = sources_[sourceIndex];
    
    // Sequence check for this source
    if (uni.packetCount > 0 && uni.activeSourceIndex == (uint8_t)sourceIndex) {
        int8_t diff = (int8_t)(sequence - source.lastSequence);
        if (diff < 0 && diff > -20) {
            return false;  // Out of order packet
        }
    }
    source.lastSequence = sequence;
    
    // Priority check
    if (uni.activeSourceIndex != 0xFF && uni.activeSourceIndex != (uint8_t)sourceIndex) {
        if (priority < uni.activePriority) {
            return false;  // Lower priority source
        }
        if (priority > uni.activePriority) {
            LOG_INFO(LogTag::SACN, "Universe %d: source change (priority %d > %d)",
                     packetUniverse, priority, uni.activePriority);
        }
    }
    
    // Check DMP vector
    if (packetBuffer_[117] != SACN_VECTOR_DMP) {
        return false;
    }
    
    // Check start code (should be 0 for DMX512)
    if (packetBuffer_[125] != 0x00) {
        return false;
    }
    
    // Get property value count
    uint16_t propCount = ((uint16_t)packetBuffer_[123] << 8) | packetBuffer_[124];
    if (propCount < 2) {
        return false;
    }
    
    // Number of DMX channels
    uni.channelCount = min((uint16_t)(propCount - 1), (uint16_t)SACN_MAX_CHANNELS);
    
    // Copy DMX data (starts after start code at byte 126)
    int dmxBytes = min((int)uni.channelCount, packetSize - 126);
    if (dmxBytes > 0) {
        memcpy(uni.dmxData, &packetBuffer_[126], dmxBytes);
    }
    
    // Update universe state
    uni.lastPacketTime = millis();
    uni.packetCount++;
    uni.hasData = true;
    uni.activePriority = priority;
    uni.activeSourceIndex = (uint8_t)sourceIndex;
    
    // Update global stats
    totalPacketCount_++;
    lastAnyPacketTime_ = millis();
    
    return true;
}

int SacnProtocol::findOrCreateSource(const uint8_t* cid, const char* name, uint8_t priority) {
    uint32_t now = millis();
    int emptySlot = -1;
    int oldestSlot = 0;
    uint32_t oldestTime = UINT32_MAX;
    
    for (int i = 0; i < SACN_MAX_SOURCES; i++) {
        if (sources_[i].active) {
            if (memcmp(sources_[i].cid, cid, 16) == 0) {
                // Found existing source
                sources_[i].priority = priority;
                sources_[i].lastSeen = now;
                strncpy(sources_[i].name, name, 63);
                sources_[i].name[63] = '\0';
                return i;
            }
            if (sources_[i].lastSeen < oldestTime) {
                oldestTime = sources_[i].lastSeen;
                oldestSlot = i;
            }
        } else if (emptySlot < 0) {
            emptySlot = i;
        }
    }
    
    int slot = (emptySlot >= 0) ? emptySlot : oldestSlot;
    
    memcpy(sources_[slot].cid, cid, 16);
    strncpy(sources_[slot].name, name, 63);
    sources_[slot].name[63] = '\0';
    sources_[slot].priority = priority;
    sources_[slot].lastSequence = 0;
    sources_[slot].lastSeen = now;
    sources_[slot].active = true;
    
    LOG_INFO(LogTag::SACN, "New source: %s (priority %d)", sources_[slot].name, priority);
    return slot;
}

void SacnProtocol::cleanupStaleSources() {
    uint32_t now = millis();
    for (int i = 0; i < SACN_MAX_SOURCES; i++) {
        if (sources_[i].active && (now - sources_[i].lastSeen > SACN_SOURCE_TIMEOUT_MS)) {
            LOG_INFO(LogTag::SACN, "Source timeout: %s", sources_[i].name);
            sources_[i].active = false;
            
            for (uint8_t u = 0; u < universeCount_; u++) {
                if (universes_[u].activeSourceIndex == i) {
                    universes_[u].activePriority = 0;
                    universes_[u].activeSourceIndex = 0xFF;
                }
            }
        }
    }
}

IPAddress SacnProtocol::getMulticastIP(uint16_t universe) {
    return IPAddress(239, 255, (universe >> 8) & 0xFF, universe & 0xFF);
}

void SacnProtocol::joinAllMulticast() {
    if (universeCount_ > 1) {
        LOG_WARN(LogTag::SACN, "Multi-universe works best with unicast mode");
    }
    
    // Join first universe multicast group
    IPAddress multicastIP = getMulticastIP(startUniverse_);
    if (udp_.beginMulticast(multicastIP, SACN_PORT)) {
        LOG_INFO(LogTag::SACN, "Joined multicast: %s", multicastIP.toString().c_str());
    }
}

void SacnProtocol::leaveAllMulticast() {
    // ESP32 leaves multicast when UDP is stopped
}

int SacnProtocol::getUniverseIndex(uint16_t universe) {
    if (universe < startUniverse_ || universe >= startUniverse_ + universeCount_) {
        return -1;
    }
    return universe - startUniverse_;
}

void SacnProtocol::assembleLeds() {
    uint16_t channelOffset = startChannel_ - 1;
    uint16_t ledsPerUniverse = (SACN_MAX_CHANNELS - channelOffset) / 3;
    uint16_t ledIndex = 0;
    
    for (uint8_t uniIdx = 0; uniIdx < universeCount_ && ledIndex < ledCount_; uniIdx++) {
        SacnUniverse& uni = universes_[uniIdx];
        
        if (!uni.hasData) {
            if (uniIdx == 0) {
                ledIndex += ledsPerUniverse;
            } else {
                ledIndex += 170;
            }
            continue;
        }
        
        uint16_t chOffset = (uniIdx == 0) ? channelOffset : 0;
        uint16_t ledsThisUni = (uniIdx == 0) ? ledsPerUniverse : 170;
        
        for (uint16_t i = 0; i < ledsThisUni && ledIndex < ledCount_; i++) {
            uint16_t rChannel = chOffset + (i * 3);
            uint16_t gChannel = rChannel + 1;
            uint16_t bChannel = rChannel + 2;
            
            if (bChannel < SACN_MAX_CHANNELS && bChannel < uni.channelCount) {
                workBuffer_[ledIndex].r = uni.dmxData[rChannel];
                workBuffer_[ledIndex].g = uni.dmxData[gChannel];
                workBuffer_[ledIndex].b = uni.dmxData[bChannel];
            }
            ledIndex++;
        }
    }
}

bool SacnProtocol::hasTimedOut(uint32_t timeoutMs) const {
    if (lastAnyPacketTime_ == 0) {
        return false;  // Never received
    }
    return (millis() - lastAnyPacketTime_) > timeoutMs;
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

const char* SacnProtocol::getActiveSourceName() const {
    if (universeCount_ == 0) return "N/A";
    uint8_t srcIdx = universes_[0].activeSourceIndex;
    if (srcIdx >= SACN_MAX_SOURCES || !sources_[srcIdx].active) {
        return "None";
    }
    return sources_[srcIdx].name;
}

uint8_t SacnProtocol::getActivePriority() const {
    if (universeCount_ == 0) return 0;
    return universes_[0].activePriority;
}

} // namespace lume
