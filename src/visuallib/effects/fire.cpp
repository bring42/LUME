/**
 * Fire effect - Realistic fire simulation (Schema-aware)
 * 
 * Uses the classic Fire2012 algorithm with configurable parameters
 * Migrated to ParamSchema for dynamic UI controls
 */

#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"

namespace lume {

// Define parameter slots as constants
namespace fire {
    constexpr uint8_t COOLING = 0;
    constexpr uint8_t SPARKING = 1;
    constexpr uint8_t REVERSED = 2;
}

// Define schema (lives in flash)
DEFINE_EFFECT_SCHEMA(fireSchema,
    ParamDesc::Int("cooling", "Cooling", 55, 20, 100),
    ParamDesc::Int("sparking", "Sparking", 120, 50, 200),
    ParamDesc::Bool("reversed", "Reversed", false)
);

// Heat array structure for scratchpad
struct FireState {
    uint8_t heat[600];
};

// Effect function
void effectFire(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    const uint16_t numLeds = view.size();
    if (numLeds == 0 || numLeds > 600) return;
    
    // Access scratchpad state
    FireState* state = view.getScratchpad<FireState>();
    if (!state) return;
    
    // Read from ParamValues slots (schema-aware)
    uint8_t cooling = params.getInt(fire::COOLING);
    uint8_t sparking = params.getInt(fire::SPARKING);
    bool reversed = params.getBool(fire::REVERSED);
    
    if (firstFrame) {
        memset(state->heat, 0, sizeof(state->heat));
    }
    
    // Fire simulation (standard algorithm)
    // Step 1: Cool down
    for (uint16_t i = 0; i < numLeds; i++) {
        state->heat[i] = qsub8(state->heat[i], random8(0, ((cooling * 10) / numLeds) + 2));
    }
    
    // Step 2: Heat diffuses upward
    for (uint16_t i = numLeds - 1; i >= 2; i--) {
        state->heat[i] = (state->heat[i - 1] + state->heat[i - 2] + state->heat[i - 2]) / 3;
    }
    
    // Step 3: Random sparks
    if (random8() < sparking) {
        uint8_t y = random8(7);
        if (y < numLeds) {
            state->heat[y] = qadd8(state->heat[y], random8(160, 255));
        }
    }
    
    // Step 4: Map heat to colors
    for (uint16_t i = 0; i < numLeds; i++) {
        uint16_t idx = reversed ? (numLeds - 1 - i) : i;
        view[idx] = HeatColor(state->heat[i]);
    }
}

// Register with schema - now with proper state size
REGISTER_EFFECT_SCHEMA(effectFire, "fire", "Fire", Animated, fireSchema, sizeof(FireState));

} // namespace lume
