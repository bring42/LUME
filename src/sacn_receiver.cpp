#include "sacn_receiver.h"
#include "logging.h"

SacnReceiver sacnReceiver;

SacnReceiver::SacnReceiver() :
    startUniverse(1),
    universeCount(1),
    enabled(false),
    initialized(false),
    unicastMode(false),
    acceptPreview(false),
    totalPacketCount(0),
    lastAnyPacketTime(0)
{
    memset(packetBuffer, 0, sizeof(packetBuffer));
    memset(universes, 0, sizeof(universes));
    memset(sources, 0, sizeof(sources));
}

bool SacnReceiver::begin(uint16_t startUni, uint8_t uniCount) {
    // Validate parameters
    if (uniCount == 0 || uniCount > SACN_MAX_UNIVERSES) {
        LOG_ERROR(LogTag::SACN, "Invalid universe count: %d (max %d)", uniCount, SACN_MAX_UNIVERSES);
        return false;
    }
    
    if (startUni == 0 || startUni > 63999) {
        LOG_ERROR(LogTag::SACN, "Invalid start universe (must be 1-63999)");
        return false;
    }
    
    startUniverse = startUni;
    universeCount = uniCount;
    
    if (WiFi.status() != WL_CONNECTED) {
        LOG_WARN(LogTag::SACN, "WiFi not connected, cannot start");
        return false;
    }
    
    // Initialize universe structures
    for (uint8_t i = 0; i < universeCount; i++) {
        universes[i].universe = startUniverse + i;
        memset(universes[i].dmxData, 0, SACN_MAX_CHANNELS);
        universes[i].channelCount = 0;
        universes[i].activePriority = 0;
        universes[i].activeSourceIndex = 0xFF;
        universes[i].lastPacketTime = 0;
        universes[i].packetCount = 0;
        universes[i].hasData = false;
    }
    
    // Clear source tracking
    for (uint8_t i = 0; i < SACN_MAX_SOURCES; i++) {
        sources[i].active = false;
    }
    
    totalPacketCount = 0;
    lastAnyPacketTime = 0;
    
    // Start UDP listener
    if (!udp.begin(SACN_PORT)) {
        LOG_ERROR(LogTag::SACN, "Failed to start UDP on port 5568");
        return false;
    }
    
    // Join multicast groups for all universes (unless unicast mode)
    if (!unicastMode) {
        joinAllMulticast();
    }
    
    initialized = true;
    enabled = true;
    
    LOG_INFO(LogTag::SACN, "Receiver started: universes %d-%d (%d total)", 
             startUniverse, startUniverse + universeCount - 1, universeCount);
    LOG_DEBUG(LogTag::SACN, "Mode: %s, Preview: %s", 
              unicastMode ? "Unicast" : "Multicast",
              acceptPreview ? "Accept" : "Reject");
    LOG_DEBUG(LogTag::SACN, "Max LEDs supported: %d RGB", universeCount * 170);
    
    return true;
}

void SacnReceiver::stop() {
    if (initialized) {
        if (!unicastMode) {
            leaveAllMulticast();
        }
        udp.stop();
        initialized = false;
        LOG_INFO(LogTag::SACN, "Receiver stopped");
    }
}

void SacnReceiver::setEnabled(bool en) {
    enabled = en;
    if (!en) {
        // Clear receiving state for all universes
        for (uint8_t i = 0; i < universeCount; i++) {
            universes[i].hasData = false;
        }
    }
    LOG_INFO(LogTag::SACN, "%s", en ? "Enabled" : "Disabled");
}

void SacnReceiver::setUnicastMode(bool unicast) {
    if (initialized && unicastMode != unicast) {
        // Mode change while running - update multicast subscriptions
        if (unicast) {
            leaveAllMulticast();
        } else {
            joinAllMulticast();
        }
    }
    unicastMode = unicast;
    LOG_DEBUG(LogTag::SACN, "Mode: %s", unicast ? "Unicast" : "Multicast");
}

void SacnReceiver::setPreviewMode(bool preview) {
    acceptPreview = preview;
    LOG_DEBUG(LogTag::SACN, "Preview packets: %s", preview ? "Accept" : "Reject");
}

IPAddress SacnReceiver::getMulticastIP(uint16_t uni) {
    // E1.31 multicast address: 239.255.{universe_high}.{universe_low}
    return IPAddress(239, 255, (uni >> 8) & 0xFF, uni & 0xFF);
}

bool SacnReceiver::joinMulticast(uint16_t universe) {
    IPAddress multicastIP = getMulticastIP(universe);
    return udp.beginMulticast(multicastIP, SACN_PORT);
}

void SacnReceiver::joinAllMulticast() {
    // ESP32 limitation: WiFiUDP can only join one multicast group per socket
    // For multi-universe, we need to use unicast mode OR rely on the 
    // sender to send to the device IP directly
    if (universeCount > 1) {
        LOG_WARN(LogTag::SACN, "Multi-universe works best with unicast mode");
    }
    
    // Join first universe multicast group
    IPAddress multicastIP = getMulticastIP(startUniverse);
    if (udp.beginMulticast(multicastIP, SACN_PORT)) {
        LOG_INFO(LogTag::SACN, "Joined multicast: %s (universe %d)", 
                 multicastIP.toString().c_str(), startUniverse);
    }
    
    // For additional universes, try to join their groups too
    // (may not work on all ESP32 network stacks)
    for (uint8_t i = 1; i < universeCount; i++) {
        IPAddress ip = getMulticastIP(startUniverse + i);
        // Use igmp_joingroup if available, otherwise unicast is recommended
        LOG_DEBUG(LogTag::SACN, "Universe %d multicast: %s (may require unicast)",
                  startUniverse + i, ip.toString().c_str());
    }
}

void SacnReceiver::leaveAllMulticast() {
    // ESP32 leaves multicast when UDP is stopped/restarted
}

int SacnReceiver::getUniverseIndex(uint16_t universe) {
    if (universe < startUniverse || universe >= startUniverse + universeCount) {
        return -1;
    }
    return universe - startUniverse;
}

bool SacnReceiver::update() {
    if (!initialized || !enabled) {
        return false;
    }
    
    // Periodically clean up stale sources
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > 1000) {
        cleanupStaleSources();
        lastCleanup = millis();
    }
    
    bool receivedAny = false;
    
    // Process all available packets (handle burst traffic)
    int maxPacketsPerUpdate = 10; // Prevent blocking too long
    while (maxPacketsPerUpdate-- > 0) {
        int packetSize = udp.parsePacket();
        if (packetSize == 0) {
            break;
        }
        
        if (packetSize < SACN_HEADER_SIZE) {
            udp.flush();
            continue;
        }
        
        if (packetSize > (int)sizeof(packetBuffer)) {
            udp.flush();
            continue;
        }
        
        int bytesRead = udp.read(packetBuffer, sizeof(packetBuffer));
        udp.flush();
        
        if (bytesRead < SACN_HEADER_SIZE) {
            continue;
        }
        
        if (parsePacket(bytesRead)) {
            receivedAny = true;
        }
    }
    
    return receivedAny;
}

int SacnReceiver::findOrCreateSource(const uint8_t* cid, const char* name, uint8_t priority) {
    unsigned long now = millis();
    int emptySlot = -1;
    int oldestSlot = 0;
    unsigned long oldestTime = ULONG_MAX;
    
    // Look for existing source or empty slot
    for (int i = 0; i < SACN_MAX_SOURCES; i++) {
        if (sources[i].active) {
            if (memcmp(sources[i].cid, cid, 16) == 0) {
                // Found existing source - update it
                sources[i].priority = priority;
                sources[i].lastSeen = now;
                strncpy(sources[i].name, name, 63);
                sources[i].name[63] = '\0';
                return i;
            }
            // Track oldest for potential replacement
            if (sources[i].lastSeen < oldestTime) {
                oldestTime = sources[i].lastSeen;
                oldestSlot = i;
            }
        } else if (emptySlot < 0) {
            emptySlot = i;
        }
    }
    
    // Use empty slot or replace oldest inactive source
    int slot = (emptySlot >= 0) ? emptySlot : oldestSlot;
    
    memcpy(sources[slot].cid, cid, 16);
    strncpy(sources[slot].name, name, 63);
    sources[slot].name[63] = '\0';
    sources[slot].priority = priority;
    sources[slot].lastSequence = 0;
    sources[slot].lastSeen = now;
    sources[slot].active = true;
    
    LOG_INFO(LogTag::SACN, "New source: %s (priority %d)", sources[slot].name, priority);
    return slot;
}

void SacnReceiver::cleanupStaleSources() {
    unsigned long now = millis();
    for (int i = 0; i < SACN_MAX_SOURCES; i++) {
        if (sources[i].active && (now - sources[i].lastSeen > SACN_SOURCE_TIMEOUT)) {
            LOG_INFO(LogTag::SACN, "Source timeout: %s", sources[i].name);
            sources[i].active = false;
            
            // Check if any universe was using this source
            for (uint8_t u = 0; u < universeCount; u++) {
                if (universes[u].activeSourceIndex == i) {
                    universes[u].activePriority = 0;
                    universes[u].activeSourceIndex = 0xFF;
                }
            }
        }
    }
}

bool SacnReceiver::parsePacket(int packetSize) {
    // E1.31 packet structure - see header comments for full layout
    
    // Check ACN packet identifier (bytes 4-15)
    const uint8_t ACN_ID[] = {0x41, 0x53, 0x43, 0x2d, 0x45, 0x31, 0x2e, 0x31, 0x37, 0x00, 0x00, 0x00};
    if (memcmp(&packetBuffer[4], ACN_ID, 12) != 0) {
        return false;
    }
    
    // Check root vector (E1.31 data packet = 0x00000004)
    uint32_t rootVector = ((uint32_t)packetBuffer[18] << 24) | 
                          ((uint32_t)packetBuffer[19] << 16) |
                          ((uint32_t)packetBuffer[20] << 8) | 
                          packetBuffer[21];
    if (rootVector != SACN_VECTOR_ROOT) {
        return false;
    }
    
    // Check frame vector (DMP = 0x00000002)
    uint32_t frameVector = ((uint32_t)packetBuffer[40] << 24) | 
                           ((uint32_t)packetBuffer[41] << 16) |
                           ((uint32_t)packetBuffer[42] << 8) | 
                           packetBuffer[43];
    if (frameVector != SACN_VECTOR_FRAME) {
        return false;
    }
    
    // Extract source info
    const uint8_t* cid = &packetBuffer[22];           // 16-byte UUID
    const char* sourceName = (const char*)&packetBuffer[44];  // 64-byte name
    uint8_t priority = packetBuffer[108];             // Priority (0-200)
    uint8_t sequence = packetBuffer[111];             // Sequence number
    uint8_t options = packetBuffer[112];              // Options flags
    
    // Check options
    if ((options & SACN_OPT_PREVIEW) && !acceptPreview) {
        return false;  // Preview packet and we don't accept them
    }
    
    if (options & SACN_OPT_STREAM_TERM) {
        // Stream terminated - could handle this specially
        // For now, just let the timeout handle it
        return false;
    }
    
    // Get universe from packet
    uint16_t packetUniverse = ((uint16_t)packetBuffer[113] << 8) | packetBuffer[114];
    
    // Check if this universe is in our range
    int uniIndex = getUniverseIndex(packetUniverse);
    if (uniIndex < 0) {
        return false;  // Not a universe we care about
    }
    
    SacnUniverse& uni = universes[uniIndex];
    
    // Find or create source entry
    int sourceIndex = findOrCreateSource(cid, sourceName, priority);
    if (sourceIndex < 0) {
        return false;
    }
    
    SacnSource& source = sources[sourceIndex];
    
    // Sequence check for this source
    if (uni.packetCount > 0 && uni.activeSourceIndex == (uint8_t)sourceIndex) {
        int8_t diff = (int8_t)(sequence - source.lastSequence);
        if (diff < 0 && diff > -20) {
            // Out of order packet from same source, skip
            return false;
        }
    }
    source.lastSequence = sequence;
    
    // Priority check - only accept if priority >= current active priority
    // If priorities are equal, accept (allows source takeover)
    // If new priority is higher, switch to new source
    if (uni.activeSourceIndex != 0xFF && uni.activeSourceIndex != (uint8_t)sourceIndex) {
        if (priority < uni.activePriority) {
            // Lower priority source, ignore
            return false;
        }
        if (priority > uni.activePriority) {
            LOG_INFO(LogTag::SACN, "Universe %d: Source change %s -> %s (priority %d > %d)",
                     packetUniverse, 
                     sources[uni.activeSourceIndex].name, 
                     source.name,
                     priority, uni.activePriority);
        }
    }
    
    // Check DMP vector
    if (packetBuffer[117] != SACN_VECTOR_DMP) {
        return false;
    }
    
    // Check start code (should be 0 for DMX512)
    if (packetBuffer[125] != 0x00) {
        return false;
    }
    
    // Get property value count (includes start code)
    uint16_t propCount = ((uint16_t)packetBuffer[123] << 8) | packetBuffer[124];
    if (propCount < 2) {
        return false;
    }
    
    // Number of DMX channels (subtract 1 for start code)
    uni.channelCount = min((uint16_t)(propCount - 1), (uint16_t)SACN_MAX_CHANNELS);
    
    // Copy DMX data (starts after start code at byte 126)
    int dmxBytes = min((int)uni.channelCount, packetSize - 126);
    if (dmxBytes > 0) {
        memcpy(uni.dmxData, &packetBuffer[126], dmxBytes);
    }
    
    // Update universe state
    uni.lastPacketTime = millis();
    uni.packetCount++;
    uni.hasData = true;
    uni.activePriority = priority;
    uni.activeSourceIndex = (uint8_t)sourceIndex;
    
    // Update global stats
    totalPacketCount++;
    lastAnyPacketTime = millis();
    
    // Debug: Log every 100 packets per universe
    if (uni.packetCount % 100 == 0) {
        LOG_DEBUG(LogTag::SACN, "Uni %d: %lu pkts, seq=%d, ch=%d, src=%s, pri=%d", 
                  packetUniverse, uni.packetCount, sequence, 
                  uni.channelCount, source.name, priority);
    }
    
    return true;
}

void SacnReceiver::applyToLeds(CRGB* leds, uint16_t numLeds, uint16_t startChannel) {
    if (startChannel < 1 || startChannel > 512) {
        startChannel = 1;
    }
    
    // Convert to 0-indexed
    uint16_t channelOffset = startChannel - 1;
    
    // Calculate how many LEDs fit in first universe
    uint16_t ledsPerUniverse = (SACN_MAX_CHANNELS - channelOffset) / 3;
    
    uint16_t ledIndex = 0;
    
    for (uint8_t uniIdx = 0; uniIdx < universeCount && ledIndex < numLeds; uniIdx++) {
        SacnUniverse& uni = universes[uniIdx];
        
        if (!uni.hasData) {
            // No data for this universe yet, skip its LED range
            if (uniIdx == 0) {
                ledIndex += ledsPerUniverse;
            } else {
                ledIndex += 170;  // Standard 170 LEDs per universe after first
            }
            continue;
        }
        
        // First universe uses startChannel offset, subsequent universes start at channel 1
        uint16_t chOffset = (uniIdx == 0) ? channelOffset : 0;
        uint16_t ledsThisUni = (uniIdx == 0) ? ledsPerUniverse : 170;
        
        for (uint16_t i = 0; i < ledsThisUni && ledIndex < numLeds; i++) {
            uint16_t rChannel = chOffset + (i * 3);
            uint16_t gChannel = rChannel + 1;
            uint16_t bChannel = rChannel + 2;
            
            if (bChannel < SACN_MAX_CHANNELS && bChannel < uni.channelCount) {
                leds[ledIndex].r = uni.dmxData[rChannel];
                leds[ledIndex].g = uni.dmxData[gChannel];
                leds[ledIndex].b = uni.dmxData[bChannel];
            }
            ledIndex++;
        }
    }
}

bool SacnReceiver::isReceiving() {
    // Check if any universe has recent data
    for (uint8_t i = 0; i < universeCount; i++) {
        if (universes[i].hasData) {
            return true;
        }
    }
    return false;
}

bool SacnReceiver::hasTimedOut(unsigned long timeoutMs) {
    if (!isReceiving()) {
        return false;  // Never received, so not "timed out"
    }
    return (millis() - lastAnyPacketTime) > timeoutMs;
}

uint8_t* SacnReceiver::getDmxData(uint8_t universeIndex) {
    if (universeIndex >= universeCount) {
        return nullptr;
    }
    return universes[universeIndex].dmxData;
}

uint16_t SacnReceiver::getChannelCount(uint8_t universeIndex) {
    if (universeIndex >= universeCount) {
        return 0;
    }
    return universes[universeIndex].channelCount;
}

const char* SacnReceiver::getActiveSourceName(uint8_t universeIndex) {
    if (universeIndex >= universeCount) {
        return "N/A";
    }
    uint8_t srcIdx = universes[universeIndex].activeSourceIndex;
    if (srcIdx >= SACN_MAX_SOURCES || !sources[srcIdx].active) {
        return "None";
    }
    return sources[srcIdx].name;
}

uint8_t SacnReceiver::getActivePriority(uint8_t universeIndex) {
    if (universeIndex >= universeCount) {
        return 0;
    }
    return universes[universeIndex].activePriority;
}

unsigned long SacnReceiver::getTotalPacketCount() {
    return totalPacketCount;
}
