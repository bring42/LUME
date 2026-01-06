#ifndef LUME_EFFECT_REGISTRY_H
#define LUME_EFFECT_REGISTRY_H

#include "segment_view.h"
#include "effect_params.h"
#include "param_schema.h"

namespace lume {

/**
 * Effect function signature
 * 
 * All effects are pure functions with this signature:
 *   void effectName(SegmentView& view, const ParamValues& params,
 *                   uint32_t frame, bool firstFrame)
 * 
 * - view: The segment to render to (LED array slice with scratchpad access)
 * - params: Schema-aware typed parameter values (includes palette via getPalette())
 * - frame: Global frame counter (for timing, use with beatsin8 etc.)
 * - firstFrame: True when scratchpad was just reset (effect change)
 * 
 * Effects should:
 * - Write colors to view[0..view.size()-1]
 * - Use frame for animation timing (not millis())
 * - Initialize scratchpad state when firstFrame is true via view.getScratchpad<T>()
 * - Avoid global/static state - use segment scratchpad instead
 * - Be deterministic given the same inputs
 */
using EffectFn = void (*)(SegmentView& view, const ParamValues& params,
                          uint32_t frame, bool firstFrame);

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
    
    // Schema pointer (all effects must have schema now)
    const ParamSchema* schema;
    
    // Resource hints
    uint16_t stateSize;       // Bytes needed in scratchpad (0 = stateless)
    uint16_t minLeds;         // Minimum LEDs for effect to look good
    
    EffectFn fn;              // The actual effect function
    
    // Helper: has schema
    bool hasSchema() const { return schema != nullptr && schema->count > 0; }
    
    // Helper: check if effect uses palette parameter
    bool usesPalette() const {
        return hasSchema() && schema->find("palette") != nullptr;
    }
    
    // Helper: count color parameters
    uint8_t colorCount() const {
        if (!hasSchema()) return 0;
        uint8_t count = 0;
        for (uint8_t i = 0; i < schema->count; i++) {
            if (schema->params[i].type == ParamType::Color) {
                count++;
            }
        }
        return count;
    }
    
    // Helper: check if parameter exists
    bool hasParam(const char* id) const {
        return hasSchema() && schema->find(id) != nullptr;
    }
    
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

// Schema-aware registration macro
#define REGISTER_EFFECT_SCHEMA(fn, idStr, dispName, cat, schemaRef, stateSz) \
    static lume::EffectRegistrar _registrar_##fn({ \
        idStr, dispName, lume::EffectCategory::cat, \
        &schemaRef, \
        stateSz, 1, fn \
    })

// Convenience macro to define schema inline
#define DEFINE_EFFECT_SCHEMA(name, ...) \
    static const lume::ParamDesc name##_params[] = { __VA_ARGS__ }; \
    static const lume::ParamSchema name = { \
        name##_params, \
        sizeof(name##_params) / sizeof(name##_params[0]) \
    }

// Convenience function to get registry
inline EffectRegistry& effects() {
    return EffectRegistry::instance();
}

} // namespace lume

#endif // LUME_EFFECT_REGISTRY_H
