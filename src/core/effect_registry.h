#ifndef LUME_EFFECT_REGISTRY_H
#define LUME_EFFECT_REGISTRY_H

#include "segment_view.h"
#include "effect_params.h"

namespace lume {

/**
 * Effect function signature
 * 
 * All effects are pure functions with this signature:
 *   void effectName(SegmentView& view, const EffectParams& params, uint32_t frame)
 * 
 * - view: The segment to render to (LED array slice)
 * - params: Colors, speed, palette, custom values
 * - frame: Global frame counter (for timing, use with beatsin8 etc.)
 * 
 * Effects should:
 * - Write colors to view[0..view.size()-1]
 * - Use frame for animation timing (not millis())
 * - Avoid global/static state where possible
 * - Be deterministic given the same inputs
 */
using EffectFn = void (*)(SegmentView& view, const EffectParams& params, uint32_t frame);

/**
 * Effect metadata
 */
struct EffectInfo {
    const char* name;        // Effect name (lowercase, no spaces)
    const char* displayName; // Human-readable name
    EffectFn fn;             // The effect function
    bool usesSecondaryColor; // Does effect use secondary color?
    bool usesPalette;        // Does effect use palette?
};

// Maximum number of registered effects
constexpr uint8_t MAX_EFFECTS = 32;

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
    
    // Register an effect (called by REGISTER_EFFECT macro)
    bool add(const char* name, const char* displayName, EffectFn fn,
             bool usesSecondary = false, bool usesPalette = false) {
        if (count >= MAX_EFFECTS) return false;
        
        effects[count].name = name;
        effects[count].displayName = displayName;
        effects[count].fn = fn;
        effects[count].usesSecondaryColor = usesSecondary;
        effects[count].usesPalette = usesPalette;
        count++;
        return true;
    }
    
    // Find effect by name
    EffectFn find(const char* name) const {
        if (!name) return nullptr;
        for (uint8_t i = 0; i < count; i++) {
            if (strcmp(effects[i].name, name) == 0) {
                return effects[i].fn;
            }
        }
        return nullptr;
    }
    
    // Get effect info by name
    const EffectInfo* getInfo(const char* name) const {
        if (!name) return nullptr;
        for (uint8_t i = 0; i < count; i++) {
            if (strcmp(effects[i].name, name) == 0) {
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
    
    // Get all effect names (for API)
    void getNames(const char** names, uint8_t maxCount) const {
        for (uint8_t i = 0; i < count && i < maxCount; i++) {
            names[i] = effects[i].name;
        }
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
    EffectRegistrar(const char* name, const char* displayName, EffectFn fn,
                    bool usesSecondary = false, bool usesPalette = false) {
        EffectRegistry::instance().add(name, displayName, fn, usesSecondary, usesPalette);
    }
    
    // Convenience overload with same name for display
    EffectRegistrar(const char* name, EffectFn fn,
                    bool usesSecondary = false, bool usesPalette = false) {
        EffectRegistry::instance().add(name, name, fn, usesSecondary, usesPalette);
    }
};

/**
 * REGISTER_EFFECT macro
 * 
 * Usage:
 *   void effectFire(SegmentView& view, const EffectParams& params, uint32_t frame) {
 *       // effect implementation
 *   }
 *   REGISTER_EFFECT("fire", "Fire", effectFire, false, false);
 * 
 * Or with defaults (no secondary color, no palette):
 *   REGISTER_EFFECT_SIMPLE("fire", effectFire);
 */
#define REGISTER_EFFECT(name, displayName, fn, usesSecondary, usesPalette) \
    static lume::EffectRegistrar _registrar_##fn(name, displayName, fn, usesSecondary, usesPalette)

#define REGISTER_EFFECT_SIMPLE(name, fn) \
    static lume::EffectRegistrar _registrar_##fn(name, fn)

#define REGISTER_EFFECT_PALETTE(name, displayName, fn) \
    static lume::EffectRegistrar _registrar_##fn(name, displayName, fn, false, true)

#define REGISTER_EFFECT_COLORS(name, displayName, fn) \
    static lume::EffectRegistrar _registrar_##fn(name, displayName, fn, true, false)

// Convenience function to get registry
inline EffectRegistry& effects() {
    return EffectRegistry::instance();
}

} // namespace lume

#endif // LUME_EFFECT_REGISTRY_H
