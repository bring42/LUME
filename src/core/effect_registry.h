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
 * Effect dimensionality (TECH_DEBT P1.2)
 *
 * Declares the canvas shape an effect is written for, so a 2D/matrix build can
 * filter its palette (show only what will look right) and refuse to assign a
 * strip-only effect to a matrix segment. On a plain 1D strip this metadata is
 * inert — everything renders — so it costs nothing today.
 *
 * Values are ordered so OneD == 0: EffectInfo is an aggregate initialized by the
 * REGISTER_EFFECT_SCHEMA macro's brace-init, which omits the trailing `dims`
 * member. Under the board toolchain's C++11 aggregate rules the omitted member is
 * value-initialized to 0, so the default is OneD *without* a default member
 * initializer (which would make EffectInfo a non-aggregate and break the macro).
 *
 *  - OneD : written for a strip; reads/writes assume 1D contiguous order. This is
 *           the honest default for today's effects — most still touch pixels via
 *           SegmentView::raw() (see P1.4), which a serpentine/tiled remap breaks.
 *           An effect graduates to TwoD/Any once it is remap-safe (operator[]-only).
 *  - TwoD : needs a 2D canvas (uses row/column geometry); meaningless on a strip.
 *  - Any  : dimension-agnostic — remap-safe and correct on both a strip and a
 *           matrix (e.g. solid fill, a per-pixel palette wash).
 */
enum class EffectDims : uint8_t {
    OneD = 0,   // strip-only (default)
    TwoD,       // matrix-only
    Any         // runs correctly on either
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

    EffectFn fn;              // The actual effect function

    // Dimensionality (TECH_DEBT P1.2). MUST stay the last member: the
    // REGISTER_EFFECT_SCHEMA macro omits it, so it is value-initialized to
    // EffectDims::OneD (== 0). Use REGISTER_EFFECT_SCHEMA_DIMS to override.
    EffectDims dims;

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

    // Helper to get dimensionality name (API/UI exposure)
    const char* dimsName() const {
        switch (dims) {
            case EffectDims::OneD: return "1d";
            case EffectDims::TwoD: return "2d";
            case EffectDims::Any:  return "any";
            default:               return "1d";
        }
    }

    // Whether this effect may be assigned to a canvas of the given shape.
    // A 2D/matrix build calls runsOn(EffectDims::TwoD) to filter its palette;
    // a 1D build calls runsOn(EffectDims::OneD). `Any` runs on either; a
    // dimension-specific effect runs only on its own shape.
    bool runsOn(EffectDims canvas) const {
        return dims == EffectDims::Any || dims == canvas;
    }
};

// Maximum number of registered effects
constexpr uint8_t MAX_EFFECTS = 32;

// SCRATCHPAD_SIZE / SCRATCHPAD_ALIGN are defined in segment_view.h (included above).

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

// Schema-aware registration macro. Leaves `dims` defaulted to EffectDims::OneD
// (see EffectInfo::dims) — the honest default for today's strip effects.
#define REGISTER_EFFECT_SCHEMA(fn, idStr, dispName, cat, schemaRef, stateSz) \
    static lume::EffectRegistrar _registrar_##fn({ \
        idStr, dispName, lume::EffectCategory::cat, \
        &schemaRef, \
        stateSz, fn \
    })

// As above, but declares the effect's dimensionality explicitly (TECH_DEBT P1.2).
// Use this for an effect that is remap-safe (EffectDims::Any) or matrix-native
// (EffectDims::TwoD); `dimsEnum` is a bare EffectDims enumerator (OneD/TwoD/Any).
#define REGISTER_EFFECT_SCHEMA_DIMS(fn, idStr, dispName, cat, schemaRef, stateSz, dimsEnum) \
    static lume::EffectRegistrar _registrar_##fn({ \
        idStr, dispName, lume::EffectCategory::cat, \
        &schemaRef, \
        stateSz, fn, lume::EffectDims::dimsEnum \
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
