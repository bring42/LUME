/**
 * Pulse/Breathe effect - All LEDs fade in and out
 */

#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"

namespace lume {

// Define parameter slots
namespace pulse {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
}

// Define schema
DEFINE_EFFECT_SCHEMA(pulseSchema,
    ParamDesc::Color("color", "Color", CRGB::Red),
    ParamDesc::Int("speed", "Speed", 128, 1, 255)
);

void effectPulse(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    // Read parameters
    CRGB color = paramValues.getColor(pulse::COLOR);
    uint8_t speed = paramValues.getInt(pulse::SPEED);
    
    // Use beatsin8 for smooth sine wave
    uint8_t bpm = speed / 4;  // Map 1-255 to reasonable BPM
    if (bpm < 10) bpm = 10;
    
    uint8_t brightness = beatsin8(bpm, 20, 255);
    
    // Fill with color, scaled by brightness
    color.nscale8(brightness);
    view.fill(color);
}

REGISTER_EFFECT_SCHEMA(effectPulse, "pulse", "Pulse", Animated, pulseSchema, 0);

} // namespace lume
