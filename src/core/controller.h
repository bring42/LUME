#ifndef LUME_CONTROLLER_H
#define LUME_CONTROLLER_H

#include <FastLED.h>
#include "segment.h"
#include "command_queue.h"
#include "../constants.h"

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
    
    // Force immediate show (bypasses frame timing)
    void show();
    
    // --- Segment management ---
    
    // Create a new segment (returns nullptr if max segments reached)
    Segment* createSegment(uint16_t start, uint16_t length, bool reversed = false);
    
    // Get segment by ID
    Segment* getSegment(uint8_t id);
    
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
    
    void setTargetFps(uint16_t fps) { targetFps = fps; }
    uint16_t getTargetFps() const { return targetFps; }
    
    // --- FastLED passthrough ---
    
    void setColorCorrection(CRGB correction) {
        FastLED.setCorrection(correction);
    }
    
    void setMaxPower(uint8_t volts, uint16_t milliamps) {
        FastLED.setMaxPowerInVoltsAndMilliamps(volts, milliamps);
    }
    
    // --- Direct LED access (for protocols like sACN) ---
    
    CRGB* getLeds() { return leds; }
    const CRGB* getLeds() const { return leds; }
    uint16_t getLedCount() const { return ledCount; }
    
    // Get frame counter (for effects)
    uint32_t getFrame() const { return frameCounter; }
    
    // Get actual FPS (for diagnostics)
    uint16_t getActualFps() const { return actualFps; }
    
    // --- Command queue access (for handlers) ---
    
    // Enqueue a command (thread-safe)
    bool enqueueCommand(const Command& cmd) {
        return commandQueue.enqueue(cmd);
    }
    
private:
    // Process pending commands (called at start of each frame)
    void processCommands();
    
    // Execute a single command
    void executeCommand(const Command& cmd);
    
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
    
    // Timing
    uint16_t targetFps;
    uint32_t frameCounter;
    uint32_t lastFrameTime;
    uint16_t actualFps;
    uint32_t fpsUpdateTime;
    uint16_t fpsFrameCount;
    
    // Internal helpers
    void blendSegment(Segment& seg);
};

// Global controller instance
extern LumeController controller;

} // namespace lume

#endif // LUME_CONTROLLER_H
