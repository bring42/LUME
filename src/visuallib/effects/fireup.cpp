/**
 * Fire Up effect - Fire flames rising upward (inverted fire)
 * 
 * TODO: Migrate static heat array to scratchpad for multi-segment support
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace fireup {
    constexpr uint8_t SPEED = 0;
    constexpr uint8_t INTENSITY = 1;
}

DEFINE_EFFECT_SCHEMA(fireupSchema,
    ParamDesc::Int("speed", "Sparking", 120, 1, 255),
    ParamDesc::Int("intensity", "Cooling", 55, 1, 255)
);

// Heat array structure for scratchpad
struct FireUpState {
    uint8_t heat[300];
};

void effectFireUp(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    // Access scratchpad state
    FireUpState* state = view.getScratchpad<FireUpState>();
    if (!state) return;
    
    uint8_t sparking = params.getInt(fireup::SPEED);
    uint8_t intensity = params.getInt(fireup::INTENSITY);
    
    uint16_t len = min(view.size(), (uint16_t)300);
    if (len == 0) return;
    
    // Reset heat on first frame
    if (firstFrame) {
        memset(state->heat, 0, sizeof(state->heat));
    }
    
    uint8_t cooling = intensity > 0 ? intensity / 4 : 55;
    
    // Cool down every cell
    for (uint16_t i = 0; i < len; i++) {
        state->heat[i] = qsub8(state->heat[i], random8(0, ((cooling * 10) / len) + 2));
    }
    
    // Heat drifts up (toward index 0, opposite of normal fire)
    for (uint16_t k = 0; k < len - 2; k++) {
        state->heat[k] = (state->heat[k + 1] + state->heat[k + 2] + state->heat[k + 2]) / 3;
    }
    
    // Randomly ignite new sparks at TOP (high index)
    if (random8() < sparking) {
        uint8_t y = len - 1 - random8(7);
        if (y < len) {
            state->heat[y] = qadd8(state->heat[y], random8(160, 255));
        }
    }
    
    // Map heat to LED colors
    for (uint16_t j = 0; j < len; j++) {
        uint8_t colorIndex = scale8(state->heat[j], 240);
        view[j] = ColorFromPalette(HeatColors_p, colorIndex);
    }
}

REGISTER_EFFECT_SCHEMA(effectFireUp, "fireup", "Fire Up", Animated, fireupSchema, sizeof(FireUpState));

} // namespace lume
