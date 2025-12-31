#ifndef LUME_EFFECT_REGISTRY_H
#define LUME_EFFECT_REGISTRY_H

#include "segment_view.h"
#include "effect_params.h"

namespace lume {

/**
 * Effect function signature
 * 
 * All effects are pure functions with this signature:
 *   void effectName(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame)
 * 
 * - view: The segment to render to (LED array slice)
 * - params: Colors, speed, palette, custom values
 * - frame: Global frame counter (for timing, use with beatsin8 etc.)
 * - firstFrame: True when scratchpad was just reset (effect change)
 * 
 * Effects should:
 * - Write colors to view[0..view.size()-1]
 * - Use frame for animation timing (not millis())
 * - Initialize scratchpad state when firstFrame is true
 * - Avoid global/static state - use segment scratchpad instead
 * - Be deterministic given the same inputs
 */
using EffectFn = void (*)(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame);

/**
 * Effect categories for UI grouping and filtering
 */
enum class EffectCategory : uint8_t {
    Solid = 0,      // Static, non-animated effects
    Animated,       // Effects with motion/animation
    Moving,         // Effects with positional movement
    Special         // Complex or unique effects
};

/**
 * Effect metadata - enables rich UI/AI integration
 */
struct EffectInfo {
    const char* id;           // Machine name: "fire" (lowercase, no spaces)
    const char* displayName;  // Human-readable name: "Fire"
    EffectCategory category;  // For UI grouping
    
    // Parameter support flags (enables smart UI)
    bool usesPalette;         // Responds to palette changes
    bool usesPrimaryColor;    // Uses params.colors[0]
    bool usesSecondaryColor;  // Uses params.colors[1]
    bool usesSpeed;           // Responds to speed param
    bool usesIntensity;       // Responds to intensity param
    
    // Resource hints
    uint16_t stateSize;       // Bytes needed in scratchpad (0 = stateless)
    uint16_t minLeds;         // Minimum LEDs for effect to look good
    
    EffectFn fn;              // The actual effect function
    
    // Helper to get category name
    const char* categoryName() const {
        switch (category) {
            case EffectCategory::Solid:    return "Solid";
            case EffectCategory::Animated: return "Animated";
            case EffectCategory::Moving:   return "Moving";
            case EffectCategory::Special:  return "Special";
            default:                       return "Unknown";
        }
    }
};

// Maximum number of registered effects
constexpr uint8_t MAX_EFFECTS = 32;

// Scratchpad size for stateful effects (per segment)
constexpr size_t SCRATCHPAD_SIZE = 512;

/**
 * EffectRegistry - Singleton registry of all available effects
 * 
 * Effects register themselves at startup using REGISTER_EFFECT macro.
 * Runtime code looks up effects by name.
 */
class EffectRegistry {
public:
    static EffectRegistry& instance() {
        static EffectRegistry registry;
        return registry;
    }
    
    // Register an effect with full metadata (called by REGISTER_EFFECT macro)
    bool add(const EffectInfo& info) {
        if (count >= MAX_EFFECTS) return false;
        
        // Validate stateSize doesn't exceed scratchpad
        if (info.stateSize > SCRATCHPAD_SIZE) {
            return false;  // Effect requires too much state
        }
        
        effects[count] = info;
        count++;
        return true;
    }
    
    // Find effect function by id
    EffectFn find(const char* id) const {
        const EffectInfo* info = getInfo(id);
        return info ? info->fn : nullptr;
    }
    
    // Get effect info by id
    const EffectInfo* getInfo(const char* id) const {
        if (!id) return nullptr;
        for (uint8_t i = 0; i < count; i++) {
            if (strcmp(effects[i].id, id) == 0) {
                return &effects[i];
            }
        }
        return nullptr;
    }
    
    // Get effect by index
    const EffectInfo* getByIndex(uint8_t index) const {
        if (index >= count) return nullptr;
        return &effects[index];
    }
    
    // Get number of registered effects
    uint8_t getCount() const { return count; }
    
    // Get all effect ids (for API)
    void getIds(const char** ids, uint8_t maxCount) const {
        for (uint8_t i = 0; i < count && i < maxCount; i++) {
            ids[i] = effects[i].id;
        }
    }
    
    // Get effects by category
    uint8_t getByCategory(EffectCategory cat, const EffectInfo** results, uint8_t maxResults) const {
        uint8_t found = 0;
        for (uint8_t i = 0; i < count && found < maxResults; i++) {
            if (effects[i].category == cat) {
                results[found++] = &effects[i];
            }
        }
        return found;
    }
    
private:
    EffectRegistry() : count(0) {}
    
    EffectInfo effects[MAX_EFFECTS];
    uint8_t count;
};

/**
 * Helper class for static registration
 */
class EffectRegistrar {
public:
    EffectRegistrar(const EffectInfo& info) {
        EffectRegistry::instance().add(info);
    }
};

/**
 * REGISTER_EFFECT_FULL macro - All parameters explicit
 * 
 * Usage:
 *   REGISTER_EFFECT_FULL(effectFire, "fire", "Fire", Animated,
 *       false, false, false, true, true, sizeof(FireState), 10);
 *       // usesPal, usesPrimColor, usesSecColor, usesSpd, usesInt, stateSize, minLeds
 */
#define REGISTER_EFFECT_FULL(fn, idStr, dispName, cat, usesPal, usesPrimColor, usesSecColor, usesSpd, usesInt, stateSz, minL) \
    static lume::EffectRegistrar _registrar_##fn({ \
        idStr, dispName, lume::EffectCategory::cat, \
        usesPal, usesPrimColor, usesSecColor, usesSpd, usesInt, \
        stateSz, minL, fn \
    })

// Simple effect: animated, uses speed only
#define REGISTER_EFFECT_SIMPLE(fn, idStr) \
    REGISTER_EFFECT_FULL(fn, idStr, idStr, Animated, false, false, false, true, false, 0, 1)

// Simple effect with display name
#define REGISTER_EFFECT_SIMPLE_NAMED(fn, idStr, dispName) \
    REGISTER_EFFECT_FULL(fn, idStr, dispName, Animated, false, false, false, true, false, 0, 1)

// Static solid-type effect: no animation
#define REGISTER_EFFECT_SOLID(fn, idStr, dispName) \
    REGISTER_EFFECT_FULL(fn, idStr, dispName, Solid, false, true, false, false, false, 0, 1)

// Animated effect with speed + intensity
#define REGISTER_EFFECT_ANIMATED(fn, idStr, dispName) \
    REGISTER_EFFECT_FULL(fn, idStr, dispName, Animated, false, false, false, true, true, 0, 1)

// Moving effect with speed + intensity
#define REGISTER_EFFECT_MOVING(fn, idStr, dispName) \
    REGISTER_EFFECT_FULL(fn, idStr, dispName, Moving, false, false, false, true, true, 0, 1)

// Palette-based effect: uses palette and speed
#define REGISTER_EFFECT_PALETTE(fn, idStr, dispName) \
    REGISTER_EFFECT_FULL(fn, idStr, dispName, Animated, true, false, false, true, false, 0, 1)

// Effect using primary + secondary colors
#define REGISTER_EFFECT_COLORS(fn, idStr, dispName) \
    REGISTER_EFFECT_FULL(fn, idStr, dispName, Animated, false, true, true, true, false, 0, 1)

// Stateful effect: requires scratchpad state
#define REGISTER_EFFECT_STATEFUL(fn, idStr, dispName, stateType) \
    REGISTER_EFFECT_FULL(fn, idStr, dispName, Animated, false, false, false, true, true, sizeof(stateType), 1)

// Convenience function to get registry
inline EffectRegistry& effects() {
    return EffectRegistry::instance();
}

} // namespace lume

#endif // LUME_EFFECT_REGISTRY_H
