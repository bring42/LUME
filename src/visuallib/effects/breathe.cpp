/**
 * Breathe effect - Entire strip pulses together
 * 
 * Simple, uniform breathing - what most people expect
 */

#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"

namespace lume {

// Define parameter slots
namespace breathe {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
}

// Define schema
DEFINE_EFFECT_SCHEMA(breatheSchema,
    ParamDesc::Color("color", "Color", CRGB::Blue),
    ParamDesc::Int("speed", "Speed", 128, 1, 255)
);

void effectBreathe(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    
    // Read parameters
    CRGB color = paramValues.getColor(breathe::COLOR);
    uint8_t speed = paramValues.getInt(breathe::SPEED);
    
    // Map speed to BPM (breaths per minute)
    uint8_t bpm = map(speed, 1, 255, 5, 30);
    
    // Sine wave breathing - never fully off (looks weird)
    uint8_t breath = beatsin8(bpm, 20, 255);
    
    // Apply color at breathing brightness
    color.nscale8(breath);
    view.fill(color);
}

REGISTER_EFFECT_SCHEMA(effectBreathe, "breathe", "Breathe", Animated, breatheSchema, 0);

} // namespace lume
