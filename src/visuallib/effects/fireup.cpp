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

// Heat array for fire simulation (will move to scratchpad in Phase 1)
static uint8_t fireUpHeat[300];

void effectFireUp(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)params;
    
    uint8_t sparking = paramValues.getInt(fireup::SPEED);
    uint8_t intensity = paramValues.getInt(fireup::INTENSITY);
    
    uint16_t len = min(view.size(), (uint16_t)300);
    if (len == 0) return;
    
    // Reset heat on first frame
    if (firstFrame) {
        memset(fireUpHeat, 0, sizeof(fireUpHeat));
    }
    
    uint8_t cooling = intensity > 0 ? intensity / 4 : 55;
    
    // Cool down every cell
    for (uint16_t i = 0; i < len; i++) {
        fireUpHeat[i] = qsub8(fireUpHeat[i], random8(0, ((cooling * 10) / len) + 2));
    }
    
    // Heat drifts up (toward index 0, opposite of normal fire)
    for (uint16_t k = 0; k < len - 2; k++) {
        fireUpHeat[k] = (fireUpHeat[k + 1] + fireUpHeat[k + 2] + fireUpHeat[k + 2]) / 3;
    }
    
    // Randomly ignite new sparks at TOP (high index)
    if (random8() < sparking) {
        uint8_t y = len - 1 - random8(7);
        if (y < len) {
            fireUpHeat[y] = qadd8(fireUpHeat[y], random8(160, 255));
        }
    }
    
    // Map heat to LED colors
    for (uint16_t j = 0; j < len; j++) {
        uint8_t colorIndex = scale8(fireUpHeat[j], 240);
        view[j] = ColorFromPalette(HeatColors_p, colorIndex);
    }
}

REGISTER_EFFECT_SCHEMA(effectFireUp, "fireup", "Fire Up", Animated, fireupSchema, 0);

} // namespace lume
