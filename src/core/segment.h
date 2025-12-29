#ifndef LUME_SEGMENT_H
#define LUME_SEGMENT_H

#include "segment_view.h"
#include "effect_params.h"
#include "effect_registry.h"

namespace lume {

// Forward declare for friend access
class LumeController;

/**
 * Segment capabilities - cached "what's meaningful right now" for UI/AI
 * 
 * Updated automatically when effect changes:
 * - UI: Only show sliders for params the effect actually uses
 * - AI: Constrain prompt understanding to meaningful params
 */
struct SegmentCapabilities {
    bool hasBrightness = true;   // Always true for segments
    bool hasSpeed;               // Effect responds to speed
    bool hasIntensity;           // Effect responds to intensity
    bool hasPalette;             // Effect uses palette
    bool hasSecondaryColor;      // Effect uses colors[1]
};

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
        , params()
        , caps()
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
        view = SegmentView(leds, start, length, reversed);
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
        
        // Update cached capabilities from effect metadata
        caps.hasSpeed = info->usesSpeed;
        caps.hasIntensity = info->usesIntensity;
        caps.hasPalette = info->usesPalette;
        caps.hasSecondaryColor = info->usesSecondaryColor;
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
    
    // Get cached capabilities
    const SegmentCapabilities& getCapabilities() const { return caps; }
    
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
    
    uint16_t getStart() const { return view.getStart(); }
    uint16_t getLength() const { return view.size(); }
    bool isReversed() const { return view.reversed; }
    
    // Direct access to view (for advanced effects)
    SegmentView& getView() { return view; }
    const SegmentView& getView() const { return view; }
    
    // Direct access to params
    EffectParams& getParams() { return params; }
    const EffectParams& getParams() const { return params; }
    
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
        effect->fn(view, params, frame, firstFrame);
        
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
    EffectParams params;
    SegmentCapabilities caps;
    
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
