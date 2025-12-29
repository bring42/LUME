#ifndef LUME_SEGMENT_H
#define LUME_SEGMENT_H

#include "segment_view.h"
#include "effect_params.h"
#include "effect_registry.h"

namespace lume {

// Forward declare for friend access
class LumeController;

/**
 * Effect state storage
 * 
 * Some effects need persistent state across frames (e.g., fire cooling array).
 * This union provides space for effect-specific state without heap allocation.
 */
union EffectState {
    // Fire effect cooling data (up to 64 LEDs per segment with state)
    uint8_t cooling[64];
    
    // Generic state bytes
    uint8_t bytes[64];
    
    // Numeric state
    struct {
        uint16_t position;
        uint16_t counter;
        uint8_t phase;
        uint8_t direction;
    } motion;
    
    EffectState() { memset(bytes, 0, sizeof(bytes)); }
};

/**
 * Segment - A controllable region of the LED strip
 * 
 * Each segment has:
 * - A view into the LED array (start position, length)
 * - An assigned effect function
 * - Effect parameters (colors, speed, palette)
 * - Optional effect-specific state
 */
class Segment {
public:
    Segment()
        : view()
        , effect(nullptr)
        , effectName(nullptr)
        , params()
        , state()
        , brightness(255)
        , blendMode(BlendMode::Replace)
        , active(false)
        , id(0) {}
    
    // --- Configuration ---
    
    // Set the LED range for this segment
    void setRange(CRGB* leds, uint16_t start, uint16_t length, bool reversed = false) {
        view = SegmentView(leds, start, length, reversed);
        active = true;
    }
    
    // Set effect by function pointer
    void setEffect(EffectFn fn) {
        effect = fn;
        effectName = nullptr;
        state = EffectState(); // Reset state
    }
    
    // Set effect by name (looks up in registry)
    bool setEffect(const char* name) {
        EffectFn fn = effects().find(name);
        if (fn) {
            effect = fn;
            effectName = name;
            state = EffectState();
            return true;
        }
        return false;
    }
    
    // Get current effect name
    const char* getEffectName() const {
        return effectName ? effectName : "none";
    }
    
    // --- Parameter accessors ---
    
    void setPrimaryColor(CRGB color) { params.primaryColor = color; }
    void setSecondaryColor(CRGB color) { params.secondaryColor = color; }
    void setSpeed(uint8_t speed) { params.speed = speed; }
    void setIntensity(uint8_t intensity) { params.intensity = intensity; }
    void setPalette(CRGBPalette16 palette) { params.palette = palette; }
    void setPalette(PalettePreset preset) { params.palette = getPalette(preset); }
    
    CRGB getPrimaryColor() const { return params.primaryColor; }
    CRGB getSecondaryColor() const { return params.secondaryColor; }
    uint8_t getSpeed() const { return params.speed; }
    uint8_t getIntensity() const { return params.intensity; }
    
    void setBrightness(uint8_t bri) { brightness = bri; }
    uint8_t getBrightness() const { return brightness; }
    
    void setBlendMode(BlendMode mode) { blendMode = mode; }
    BlendMode getBlendMode() const { return blendMode; }
    
    // --- State ---
    
    bool isActive() const { return active && view.valid(); }
    void setActive(bool a) { active = a; }
    
    uint8_t getId() const { return id; }
    
    uint16_t getStart() const { 
        // Note: We can't know absolute start from view alone
        // Controller tracks this
        return 0; 
    }
    uint16_t getLength() const { return view.size(); }
    bool isReversed() const { return view.reversed; }
    
    // Direct access to view (for advanced effects)
    SegmentView& getView() { return view; }
    const SegmentView& getView() const { return view; }
    
    // Direct access to params
    EffectParams& getParams() { return params; }
    const EffectParams& getParams() const { return params; }
    
    // Direct access to effect state
    EffectState& getState() { return state; }
    
    // --- Update ---
    
    // Run the effect for this frame
    void update(uint32_t frame) {
        if (!active || !view.valid() || !effect) {
            return;
        }
        
        // Apply per-segment brightness by scaling params
        // (Alternative: post-process LEDs, but this is simpler)
        effect(view, params, frame);
        
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
    EffectFn effect;
    const char* effectName;
    EffectParams params;
    EffectState state;
    
    uint8_t brightness;
    BlendMode blendMode;
    bool active;
    uint8_t id;
};

} // namespace lume

#endif // LUME_SEGMENT_H
