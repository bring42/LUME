/**
 * Sparkle effect - Random white sparkles on colored background
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace sparkle {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
}

DEFINE_EFFECT_SCHEMA(sparkleSchema,
    ParamDesc::Color("color", "Background Color", CRGB::Blue),
    ParamDesc::Int("speed", "Sparkle Density", 128, 1, 255)
);

void effectSparkle(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    
    CRGB color = params.getColor(sparkle::COLOR);
    uint8_t speed = params.getInt(sparkle::SPEED);
    
    // Fill with background color
    view.fill(color);
    
    // Add random white sparkles
    uint8_t numSparkles = max(1, speed / 32);
    
    for (uint8_t i = 0; i < numSparkles; i++) {
        uint16_t pos = random16(view.size());
        view[pos] = CRGB::White;
    }
}

REGISTER_EFFECT_SCHEMA(effectSparkle, "sparkle", "Sparkle", Animated, sparkleSchema, 0);

} // namespace lume
