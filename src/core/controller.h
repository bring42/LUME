#ifndef LUME_CONTROLLER_H
#define LUME_CONTROLLER_H

#include <FastLED.h>
#include <atomic>
#include <ArduinoJson.h>
#include "segment.h"
#include "command_queue.h"
#include "transition.h"              // premium easing engine (eased brightness)
#include "led_output.h"              // ILedOutput HAL seam (RFC 0001 §6)
#include "../protocols/protocol.h"   // ProtocolBuffer (direct-pixel staging)
#include "../constants.h"

// Forward declare IProtocol interface
namespace lume { class IProtocol; }

// LUME_WORKBUFFER_SIZE (the shared-workbuffer size for large / 2D effect state,
// TECH_DEBT P1.5) is defined in segment_view.h — the low-level header the effect
// registry and this controller both include — so they agree on the ceiling.
// Default 0 = feature off; a matrix build sets e.g. -DLUME_WORKBUFFER_SIZE=2048.
// See docs/rfcs/0002-scratchpad-strategy.md.

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

    // Swap the LED-output driver (RFC 0001 §6). Defaults to FastLED; call before
    // begin() to inject a different ILedOutput (ESP-IDF led_strip, emulator, a
    // test mock, ...).
    void setLedOutput(ILedOutput* output) { output_ = output; }

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

    // --- Shared workbuffer for large / 2D effect state (P1.5) ---
    // Lend the single controller-owned workbuffer to one canvas-spanning segment
    // so it can hold state larger than the fixed per-segment pad. At most one
    // borrower at a time. Returns false if the feature is off
    // (LUME_WORKBUFFER_SIZE == 0), the index is out of range, the buffer is
    // already lent to a different segment, or bytesNeeded exceeds its capacity.
    // The borrow is dropped on any layout change (remove/clear); the 2D setup
    // re-borrows afterwards. See docs/rfcs/0002-scratchpad-strategy.md.
    bool borrowWorkbuffer(uint8_t segmentIndex, uint16_t bytesNeeded);
    void releaseWorkbuffer();
    uint16_t workbufferCapacity() const { return kWorkbufferSize; }

    // Get active segment count
    uint8_t getSegmentCount() const;
    
    // --- Convenience: Single-segment mode ---
    
    // Create one segment spanning all LEDs (clears existing)
    Segment* createFullStrip();
    
    // --- Global controls ---
    
    // Instant power (transitionTime == 0). Snaps the on/off envelope so the
    // output matches immediately and any in-flight power fade is cancelled.
    void setPower(bool on) {
        power = on;
        powerOffWhenFadeSettles_ = false;
        powerFade_.snap(on ? 255 : 0);
    }
    bool getPower() const { return power; }

    // Eased power on/off (the premium path). Fade-on flips the strip logically
    // on and eases the envelope 0 -> full; fade-off eases full -> 0 and flips
    // off once it settles (so it keeps rendering, dimming, until dark). The
    // brightness level is untouched, so toggling never loses it. durationMs == 0
    // falls back to instant. Single-writer: call on the loop.
    void setPowerEased(bool on, uint32_t durationMs) {
        if (durationMs == 0) { setPower(on); return; }
        if (on) {
            power = true;                 // render while we fade up from black
            powerOffWhenFadeSettles_ = false;
            powerFade_.start(255, durationMs, millis());
        } else {
            powerOffWhenFadeSettles_ = true;  // stay on until the fade reaches 0
            powerFade_.start(0, durationMs, millis());
        }
    }
    // True while an on/off power fade is animating (for UI, mirrors brightness).
    bool isPowerFading() const { return powerFade_.isActive(); }
    
    // Hard-set global brightness (transitionTime == 0). Keeps the easing engine
    // in sync by snapping it, so a subsequent eased change starts from here and
    // an in-flight fade is cancelled cleanly. A manual set also clears any
    // pending power-off rider, so the strip never switches off behind the user.
    void setBrightness(uint8_t bri) {
        powerOffWhenSettled_ = false;
        globalBrightness = bri;
        output_->setBrightness(bri);
        brightnessTransition_.snap(bri);
    }
    uint8_t getBrightness() const { return globalBrightness; }

    // Ease global brightness toward `target` over `durationMs` (the premium
    // path). `powerOffAtZero` powers the strip off once the fade settles — the
    // one bit that turns a plain fade into a "nightlight". The render loop
    // advances the interpolation each frame; a zero duration falls back to an
    // immediate set. Single-writer: call on the loop.
    void setBrightnessEased(uint8_t target, uint32_t durationMs,
                            bool powerOffAtZero = false) {
        if (durationMs == 0) {
            setBrightness(target);                 // snaps, clears the rider
            if (powerOffAtZero && target == 0) setPower(false);
            return;
        }
        powerOffWhenSettled_ = powerOffAtZero;
        brightnessTransition_.start(target, durationMs, millis());
    }

    // --- Brightness-fade status (what "nightlight" reporting reduces to) ---

    // True while an eased brightness fade is in progress (a nightlight is just
    // a long one). The UI shows a countdown/cancel while this holds.
    bool isBrightnessFading() const { return brightnessTransition_.isActive(); }
    // Linear fraction of the fade's time window elapsed, for a UI countdown.
    float brightnessFadeProgress() const { return brightnessTransition_.progress(millis()); }

    // --- Protocol management ---

    // Register a protocol (called at startup)
    void registerProtocol(IProtocol* protocol);

    // Hook that re-applies sACN/MQTT config from the persisted global config.
    // Set by main during setup so the ReconfigureProtocols command can run
    // protocol reconfig on the loop task without the controller depending on
    // mqtt.h/sacn.h (which would break the native test build). See P0.8.
    void (*reconfigureProtocolsFn)() = nullptr;

    // --- Direct LED access (for protocols like sACN) ---

    CRGB* getLeds() { return leds; }
    const CRGB* getLeds() const { return leds; }
    uint16_t getLedCount() const { return ledCount; }

    // Stage a full frame of pixels for the /api/pixels debug endpoint. Callable
    // from the web task (double-buffer + atomic ready flag); the render loop
    // drains it in update() so leds[] is only ever written on the loop (P0.1).
    void stageDirectPixels(const CRGB* data, uint16_t count) {
        directPixels_.write(data, count);
    }

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

    // Direct-pixel overlay staged by /api/pixels, drained on the loop (P0.1).
    ProtocolBuffer<MAX_LED_COUNT> directPixels_;

    // LED output driver (RFC 0001 §6). Defaults to a FastLED impl in the ctor.
    ILedOutput* output_;

    // Segments
    Segment segments[MAX_SEGMENTS];
    uint8_t segmentCount;
    uint8_t nextSegmentId;

    // Shared workbuffer for large / 2D effect state (P1.5). One borrower at a
    // time; workbufferOwner_ is the borrowing slot index, or -1 when free. When
    // the feature is off (size 0) the array is a 1-byte placeholder and borrow
    // requests are refused, so a 1D build carries no real cost.
    static constexpr uint16_t kWorkbufferSize = LUME_WORKBUFFER_SIZE;
    alignas(SCRATCHPAD_ALIGN) uint8_t workbuffer_[kWorkbufferSize > 0 ? kWorkbufferSize : 1];
    int8_t workbufferOwner_;
    
    // Command queue
    CommandQueue commandQueue;
    
    // State
    bool power;
    uint8_t globalBrightness;

    // Premium easing engine for global brightness. Owned + advanced only on the
    // render loop; snapped whenever brightness is hard-set so the two never
    // disagree. See core/transition.h.
    EasedU8 brightnessTransition_;
    
    // The only nightlight-specific state left in the core: when a brightness
    // fade carrying the power-off rider settles, the loop switches the strip
    // off. Everything else about "nightlight" (its name, trigger, duration,
    // progress reporting) lives at the API boundary or falls out of the shared
    // brightnessTransition_. Cleared by any manual brightness set.
    bool powerOffWhenSettled_;

    // Power on/off fade envelope (0..255), composed onto the output brightness
    // so the user's level stays pristine across a toggle. powerOffWhenFadeSettles_
    // flips `power` false once a fade-out envelope reaches zero.
    EasedU8 powerFade_;
    bool powerOffWhenFadeSettles_;
    
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
