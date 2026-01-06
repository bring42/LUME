#ifndef LUME_SEGMENT_H
#define LUME_SEGMENT_H

#include "segment_view.h"
#include "effect_params.h"
#include "effect_registry.h"
#include "param_schema.h"

namespace lume {

// Forward declare for friend access
class LumeController;

/**
 * Segment - A controllable region of the LED strip
 * 
 * Each segment has:
 * - A view into the LED array (start position, length)
 * - An assigned effect (with metadata)
 * - Effect parameters (colors, speed, palette)
 * - Fixed-size scratchpad for stateful effects
 * 
 * Scratchpad design (see ARCHITECTURE.md Invariant 3):
 * - 512 bytes per segment for effect state
 * - Cleared automatically when effect changes
 * - Effects use getScratchpad<T>() to access typed state
 * - firstFrame flag signals scratchpad reset
 */
class Segment {
public:
    Segment()
        : view()
        , effect(nullptr)
        , paramValues()
        , brightness(255)
        , blendMode(BlendMode::Replace)
        , active(false)
        , id(0)
        , scratchpadVersion(0)
        , lastSeenVersion(0) {
        memset(scratchpad, 0, SCRATCHPAD_SIZE);
    }
    
    // --- Configuration ---
    
    // Set the LED range for this segment
    void setRange(CRGB* leds, uint16_t start, uint16_t length, bool reversed = false) {
        view = SegmentView(leds, start, length, reversed, scratchpad);
        active = true;
    }
    
    // Set effect by EffectInfo pointer (preferred)
    void setEffect(const EffectInfo* info) {
        if (!info) return;
        
        // Validate stateSize fits in scratchpad
        if (info->stateSize > SCRATCHPAD_SIZE) {
            return;  // Refuse invalid effect
        }
        
        effect = info;
        scratchpadVersion++;  // Signal scratchpad reset
        memset(scratchpad, 0, SCRATCHPAD_SIZE);
        
        // Initialize ParamValues with defaults if effect has schema
        if (info->hasSchema()) {
            paramValues.applyDefaults(*info->schema);
        }
    }
    
    // Set effect by id (looks up in registry)
    bool setEffect(const char* id) {
        const EffectInfo* info = effects().getInfo(id);
        if (info) {
            setEffect(info);
            return true;
        }
        return false;
    }
    
    // Get current effect info
    const EffectInfo* getEffect() const { return effect; }
    
    // Get current effect id
    const char* getEffectId() const {
        return effect ? effect->id : "none";
    }
    
    // Get current effect display name
    const char* getEffectName() const {
        return effect ? effect->displayName : "None";
    }
    
    // Helper: check if effect has a specific parameter
    bool hasParam(const char* paramId) const {
        return effect && effect->hasParam(paramId);
    }
    
    // --- Parameter accessors ---
    
    // Palette accessors
    void setPalette(CRGBPalette16 palette) { 
        paramValues.setPalette(palette);
    }
    void setPalette(PalettePreset preset) { 
        CRGBPalette16 pal = getPalette(preset);
        paramValues.setPalette(pal);
    }
    
    // Transitional helpers for common params (map to schema if effect has it)
    void setSpeed(uint8_t speed) {
        if (effect && effect->hasSchema()) {
            int8_t idx = effect->schema->indexOf("speed");
            if (idx >= 0) paramValues.setInt(idx, speed);
        }
    }
    
    void setIntensity(uint8_t intensity) {
        if (effect && effect->hasSchema()) {
            int8_t idx = effect->schema->indexOf("intensity");
            if (idx >= 0) paramValues.setInt(idx, intensity);
        }
    }
    
    void setColor(uint8_t colorIdx, CRGB color) {
        if (effect && effect->hasSchema()) {
            // Try common color param names
            const char* names[] = {"color", "colorStart", "colorHead", "colorEnd", "colorTail"};
            for (const char* name : names) {
                int8_t idx = effect->schema->indexOf(name);
                if (idx >= 0) {
                    paramValues.setColor(idx, color);
                    return;
                }
            }
        }
    }
    
    void setBrightness(uint8_t bri) { brightness = bri; }
    uint8_t getBrightness() const { return brightness; }
    
    void setBlendMode(BlendMode mode) { blendMode = mode; }
    BlendMode getBlendMode() const { return blendMode; }
    
    // --- State ---
    
    bool isActive() const { return active && view.valid(); }
    void setActive(bool a) { active = a; }
    
    uint8_t getId() const { return id; }
    
    uint16_t getStart() const { return view.getStart(); }
    uint16_t getLength() const { return view.size(); }
    bool isReversed() const { return view.reversed; }
    
    // Direct access to view (for advanced effects)
    SegmentView& getView() { return view; }
    const SegmentView& getView() const { return view; }
    
    // Direct access to param values (schema-aware effects)
    ParamValues& getParamValues() { return paramValues; }
    const ParamValues& getParamValues() const { return paramValues; }
    
    // --- Scratchpad access for stateful effects ---
    
    // Get typed scratchpad pointer (compile-time size check)
    template<typename T>
    T* getScratchpad() {
        static_assert(sizeof(T) <= SCRATCHPAD_SIZE, "State type exceeds scratchpad size");
        return reinterpret_cast<T*>(scratchpad);
    }
    
    template<typename T>
    const T* getScratchpad() const {
        static_assert(sizeof(T) <= SCRATCHPAD_SIZE, "State type exceeds scratchpad size");
        return reinterpret_cast<const T*>(scratchpad);
    }
    
    // Raw scratchpad access
    uint8_t* getScratchpadRaw() { return scratchpad; }
    const uint8_t* getScratchpadRaw() const { return scratchpad; }
    
    // --- Update ---
    
    // Run the effect for this frame
    void update(uint32_t frame) {
        if (!active || !view.valid() || !effect || !effect->fn) {
            return;
        }
        
        // Derive firstFrame from version mismatch (no desync possible)
        bool firstFrame = (lastSeenVersion != scratchpadVersion);
        if (firstFrame) {
            lastSeenVersion = scratchpadVersion;
        }
        
        // Call the effect function
        effect->fn(view, paramValues, frame, firstFrame);
        
        // Apply segment brightness if not 255
        if (brightness < 255) {
            for (uint16_t i = 0; i < view.size(); i++) {
                view.raw()[i].nscale8(brightness);
            }
        }
    }
    
private:
    friend class LumeController;
    
    SegmentView view;
    const EffectInfo* effect;
    ParamValues paramValues;  // Schema-aware parameter values
    
    uint8_t brightness;
    BlendMode blendMode;
    bool active;
    uint8_t id;
    
    // Scratchpad for stateful effects (see ARCHITECTURE.md Invariant 3)
    uint8_t scratchpad[SCRATCHPAD_SIZE];
    uint8_t scratchpadVersion;   // Incremented when effect changes
    uint8_t lastSeenVersion;     // Tracks when effect last saw reset
};

} // namespace lume

#endif // LUME_SEGMENT_H
