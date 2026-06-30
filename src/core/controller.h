#ifndef LUME_CONTROLLER_H
#define LUME_CONTROLLER_H

#include <FastLED.h>
#include <atomic>
#include <ArduinoJson.h>
#include "segment.h"
#include "command_queue.h"
#include "../constants.h"

// Forward declare IProtocol interface
namespace lume { class IProtocol; }

namespace lume {

// Maximum segments (can be adjusted)
constexpr uint8_t MAX_SEGMENTS = 8;

// Default frame rate target
constexpr uint16_t DEFAULT_FPS = 60;

/**
 * LumeController - The main orchestrator
 * 
 * Owns:
 * - The physical LED array
 * - All segments
 * - Frame timing
 * - Global brightness
 * 
 * Responsibilities:
 * - Initialize FastLED
 * - Update all segments each frame
 * - Call FastLED.show()
 * - Handle segment overlap blending
 */
class LumeController {
public:
    LumeController();
    
    // --- Initialization ---
    
    // Initialize with LED count (uses LED_DATA_PIN from constants.h)
    void begin(uint16_t count);
    
    // Reconfigure LED count at runtime
    void setLedCount(uint16_t count);
    
    // --- Frame update ---
    
    // Call this in loop() - handles timing and updates all segments
    void update();

    // --- Segment management ---
    
    // Create a new segment (returns nullptr if max segments reached)
    Segment* createSegment(uint16_t start, uint16_t length, bool reversed = false);
    
    // Get segment by ID
    Segment* getSegment(uint8_t id);

    // Get the index-th active segment (0..getSegmentCount()-1). Use this to
    // enumerate — IDs are non-contiguous after a middle delete, so looping
    // getSegment(0..count) by ID silently drops survivors (P0.5).
    Segment* getSegmentByIndex(uint8_t index);

    // Remove segment by ID
    bool removeSegment(uint8_t id);
    
    // Remove all segments
    void clearSegments();
    
    // Get active segment count
    uint8_t getSegmentCount() const;
    
    // --- Convenience: Single-segment mode ---
    
    // Create one segment spanning all LEDs (clears existing)
    Segment* createFullStrip();
    
    // --- Global controls ---
    
    void setPower(bool on) { power = on; }
    bool getPower() const { return power; }
    
    void setBrightness(uint8_t bri) { 
        globalBrightness = bri; 
        FastLED.setBrightness(bri);
    }
    uint8_t getBrightness() const { return globalBrightness; }
    
    // --- Nightlight ---
    
    void startNightlight(uint16_t durationSeconds, uint8_t targetBrightness);
    void stopNightlight();
    bool isNightlightActive() const { return nightlightActive; }
    float getNightlightProgress() const;
    
    // --- Protocol management ---
    
    // Register a protocol (called at startup)
    void registerProtocol(IProtocol* protocol);
    
    // --- Direct LED access (for protocols like sACN) ---
    
    CRGB* getLeds() { return leds; }
    const CRGB* getLeds() const { return leds; }
    uint16_t getLedCount() const { return ledCount; }
    
    // --- Command queue access (for handlers) ---
    
    // Enqueue a command (thread-safe)
    bool enqueueCommand(const Command& cmd) {
        return commandQueue.enqueue(cmd);
    }

    // --- Segment-layout persistence ---

    // Serialize the current segment layout (+ power/brightness) to JSON.
    void serializeSegments(JsonDocument& doc) const;

    // Rebuild segments from a previously serialized layout. Returns true if at
    // least one segment was restored (false => caller falls back to default).
    bool restoreSegments(const JsonDocument& doc);

    // Flag the layout as changed; a debounced save then becomes due.
    void markSegmentsDirty();

    // Returns true once when a save is due (dirty + debounce elapsed), clearing
    // the flag. Call from loop() and persist the layout when it returns true.
    bool takeSegmentSaveDue();

private:
    // Process pending commands (called at start of each frame)
    void processCommands();
    
    // Execute a single command
    void executeCommand(const Command& cmd);
    
    // Process registered protocols (check for incoming data)
    void processProtocols();
    
    // LED array
    CRGB leds[MAX_LED_COUNT];
    uint16_t ledCount;
    
    // Segments
    Segment segments[MAX_SEGMENTS];
    uint8_t segmentCount;
    uint8_t nextSegmentId;
    
    // Command queue
    CommandQueue commandQueue;
    
    // State
    bool power;
    uint8_t globalBrightness;
    
    // Nightlight state
    bool nightlightActive;
    uint32_t nightlightStartTime;
    uint16_t nightlightDuration;  // in seconds
    uint8_t nightlightStartBrightness;
    uint8_t nightlightTargetBrightness;
    
    // Protocol handling
    static constexpr uint8_t MAX_PROTOCOLS = 4;
    IProtocol* protocols_[MAX_PROTOCOLS];
    uint8_t protocolCount_;
    bool protocolActive_;
    IProtocol* activeProtocol_;
    static constexpr uint32_t PROTOCOL_TIMEOUT_MS = 5000;
    
    // Timing
    uint16_t targetFps;
    uint32_t frameCounter;
    uint32_t lastFrameTime;
    uint16_t actualFps;
    uint32_t fpsUpdateTime;
    uint16_t fpsFrameCount;

    // Segment-layout persistence (debounced autosave)
    bool segmentsDirty_;
    bool suppressDirty_;          // set while restoring, so restore doesn't re-save
    uint32_t lastSegmentChange_;
    static constexpr uint32_t SEGMENT_SAVE_DEBOUNCE_MS = 2000;

    // Black out only the LEDs not owned by any active segment. Effects manage
    // their own canvas and many rely on the buffer persisting between frames
    // (fade-trails), so the render loop must not wipe covered pixels.
    void clearUncoveredLeds();
};

// Global controller instance
extern LumeController controller;

} // namespace lume

#endif // LUME_CONTROLLER_H
