#ifndef LUME_COMMAND_QUEUE_H
#define LUME_COMMAND_QUEUE_H

#include <Arduino.h>
#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "param_schema.h"   // ParamValues::Slot / MAX_EFFECT_PARAMS for EffectSpec
#include "../logging.h"

namespace lume {

// Max bytes (incl. NUL) for an effect id copied into a queued command. Effect
// ids are short machine names ("rainbow", "fire"); 24 is comfortable headroom.
constexpr uint8_t MAX_EFFECT_ID_LEN = 24;

/**
 * Command types for the single-writer command queue
 * 
 * All state mutations flow through commands:
 * - Web handlers, protocols, and AI enqueue commands
 * - The main loop dequeues and executes commands
 * - Effects read state, never write shared state
 */
enum class CommandType : uint8_t {
    // Segment effect control
    SetEffect,          // Change effect on a segment
    SetBrightness,      // Set segment or global brightness
    SetSpeed,           // Set effect speed
    SetIntensity,       // Set effect intensity
    SetColor,           // Set primary/secondary color
    SetPalette,         // Set palette preset
    
    // Segment management
    CreateSegment,      // Create new segment
    RemoveSegment,      // Remove existing segment
    
    // Global control
    SetPower,           // Power on/off
    SetGlobalBrightness,// Global brightness

    // Nightlight
    StartNightlight,    // Begin a timed fade
    StopNightlight,     // Cancel an active nightlight

    // Advanced
    ApplyEffectSpec,    // Apply AI-generated effect spec
    SaveScene,          // Persist current state
    LoadScene           // Load saved state
};

/**
 * Color specification for commands
 */
struct ColorData {
    uint8_t r, g, b;
    bool isSecondary;   // false = primary, true = secondary
    
    CRGB toCRGB() const { return CRGB(r, g, b); }
};

/**
 * Segment creation data
 */
struct SegmentData {
    uint16_t start;
    uint16_t length;
    bool reversed;
};

/**
 * Nightlight start data
 */
struct NightlightData {
    uint16_t durationSec;
    uint8_t  targetBrightness;
};

/**
 * EffectSpec - a bounded, self-contained "make this segment look like X".
 *
 * This is the payload that lets handlers stop mutating segments directly
 * (RFC 0001 §3): a single compound change — optional create + effect + params +
 * palette + brightness — carried inline with NO borrowed pointers (effectId is
 * copied; params are pre-resolved to typed slots against the effect's static
 * schema on the producer's task). The render loop applies it as pure data.
 */
// Trivial aggregate (no in-class initializers) so it stays usable as a union
// member — producers zero-init with `EffectSpec spec = {};` then set fields.
struct EffectSpec {
    // Geometry — only used when `create` is true (segmentId is then 255).
    bool     create;
    uint16_t start;
    uint16_t length;
    bool     reversed;

    // Effect change (effectId already validated against the registry).
    bool     hasEffect;
    char     effectId[MAX_EFFECT_ID_LEN];

    // Param values — slot-indexed (defaults+overrides), resolved by the producer
    // against the effect schema; applied verbatim after the effect is set.
    bool                hasParams;
    ParamValues::Slot   slots[MAX_EFFECT_PARAMS];

    // Semantic params (AI path): applied via the name-resolving setters on the
    // loop (setSpeed/setIntensity/setColor), so the producer needn't know the
    // effect's slot layout.
    bool     hasSpeed;
    uint8_t  speed;
    bool     hasIntensity;
    uint8_t  intensity;
    uint8_t  colorCount;        // 0..3, applied via setColor(i, colors[i])
    CRGB     colors[3];

    // Palette preset (PalettePreset enum value).
    bool     hasPalette;
    uint8_t  palette;

    // Per-segment brightness.
    bool     hasBrightness;
    uint8_t  brightness;
};

/**
 * Command - Discriminated union for state mutations
 *
 * Fixed-size to work with FreeRTOS queue.
 * Union holds command-specific data.
 */
struct Command {
    CommandType type;
    uint8_t segmentId;      // Target segment (255 = all/global)

    union {
        // SetEffect
        const char* effectId;

        // SetBrightness, SetSpeed, SetIntensity, SetPalette
        uint8_t value8;

        // SetColor
        ColorData color;

        // CreateSegment
        SegmentData segment;

        // SetPower
        bool power;

        // Generic 32-bit value
        uint32_t value32;

        // StartNightlight
        NightlightData nightlight;

        // ApplyEffectSpec — the compound segment mutation (create/update)
        EffectSpec spec;
    } data;
    
    // Constructors for common commands
    static Command setEffect(uint8_t segId, const char* effectId) {
        Command cmd;
        cmd.type = CommandType::SetEffect;
        cmd.segmentId = segId;
        cmd.data.effectId = effectId;
        return cmd;
    }
    
    static Command setBrightness(uint8_t segId, uint8_t brightness) {
        Command cmd;
        cmd.type = CommandType::SetBrightness;
        cmd.segmentId = segId;
        cmd.data.value8 = brightness;
        return cmd;
    }
    
    static Command setSpeed(uint8_t segId, uint8_t speed) {
        Command cmd;
        cmd.type = CommandType::SetSpeed;
        cmd.segmentId = segId;
        cmd.data.value8 = speed;
        return cmd;
    }
    
    static Command setIntensity(uint8_t segId, uint8_t intensity) {
        Command cmd;
        cmd.type = CommandType::SetIntensity;
        cmd.segmentId = segId;
        cmd.data.value8 = intensity;
        return cmd;
    }
    
    static Command setColor(uint8_t segId, uint8_t r, uint8_t g, uint8_t b, bool secondary = false) {
        Command cmd;
        cmd.type = CommandType::SetColor;
        cmd.segmentId = segId;
        cmd.data.color = {r, g, b, secondary};
        return cmd;
    }
    
    static Command setPalette(uint8_t segId, uint8_t paletteIndex) {
        Command cmd;
        cmd.type = CommandType::SetPalette;
        cmd.segmentId = segId;
        cmd.data.value8 = paletteIndex;
        return cmd;
    }
    
    static Command setPower(bool on) {
        Command cmd;
        cmd.type = CommandType::SetPower;
        cmd.segmentId = 255;  // Global
        cmd.data.power = on;
        return cmd;
    }
    
    static Command setGlobalBrightness(uint8_t brightness) {
        Command cmd;
        cmd.type = CommandType::SetGlobalBrightness;
        cmd.segmentId = 255;  // Global
        cmd.data.value8 = brightness;
        return cmd;
    }
    
    static Command createSegment(uint16_t start, uint16_t length, bool reversed = false) {
        Command cmd;
        cmd.type = CommandType::CreateSegment;
        cmd.segmentId = 255;  // ID assigned by controller
        cmd.data.segment = {start, length, reversed};
        return cmd;
    }
    
    static Command removeSegment(uint8_t segId) {
        Command cmd;
        cmd.type = CommandType::RemoveSegment;
        cmd.segmentId = segId;
        return cmd;
    }

    // Compound segment mutation (create or update). segId == 255 with
    // spec.create == true creates a new segment; otherwise segId targets one.
    static Command applyEffectSpec(uint8_t segId, const EffectSpec& spec) {
        Command cmd;
        cmd.type = CommandType::ApplyEffectSpec;
        cmd.segmentId = segId;
        cmd.data.spec = spec;
        return cmd;
    }

    static Command startNightlight(uint16_t durationSec, uint8_t targetBrightness) {
        Command cmd;
        cmd.type = CommandType::StartNightlight;
        cmd.segmentId = 255;
        cmd.data.nightlight = { durationSec, targetBrightness };
        return cmd;
    }

    static Command stopNightlight() {
        Command cmd;
        cmd.type = CommandType::StopNightlight;
        cmd.segmentId = 255;
        return cmd;
    }
};

/**
 * CommandQueue - Thread-safe command queue with "newest wins" overflow
 * 
 * Uses FreeRTOS queue for thread safety (AsyncWebServer runs on different task).
 * Fixed size (16 commands). On overflow, oldest command is dropped.
 */
class CommandQueue {
public:
    static constexpr size_t QUEUE_SIZE = 16;
    
    CommandQueue() : queue(nullptr) {}
    
    // Initialize the queue (call once at startup)
    bool begin() {
        queue = xQueueCreate(QUEUE_SIZE, sizeof(Command));
        if (!queue) {
            LOG_ERROR(LogTag::MAIN, "Failed to create command queue");
            return false;
        }
        return true;
    }
    
    // Enqueue a command (thread-safe, called from any context)
    // Returns true if enqueued, false if queue was full (oldest dropped)
    bool enqueue(const Command& cmd) {
        if (!queue) return false;
        
        if (xQueueSend(queue, &cmd, 0) != pdTRUE) {
            // Queue full: drop oldest, then send new (newest wins)
            Command discard;
            xQueueReceive(queue, &discard, 0);
            xQueueSend(queue, &cmd, 0);
            LOG_WARN(LogTag::MAIN, "Command queue overflow, dropped oldest");
            return false;
        }
        return true;
    }
    
    // Dequeue a command (called from main loop only)
    // Returns true if a command was available
    bool dequeue(Command& cmd) {
        if (!queue) return false;
        return xQueueReceive(queue, &cmd, 0) == pdTRUE;
    }
    
    // Check if queue has pending commands
    bool hasPending() const {
        if (!queue) return false;
        return uxQueueMessagesWaiting(queue) > 0;
    }
    
    // Get number of pending commands
    size_t pendingCount() const {
        if (!queue) return 0;
        return uxQueueMessagesWaiting(queue);
    }
    
    // Clear all pending commands
    void clear() {
        if (!queue) return;
        xQueueReset(queue);
    }
    
private:
    QueueHandle_t queue;
};

} // namespace lume

#endif // LUME_COMMAND_QUEUE_H
