/**
 * Gradient effect - Static gradient from primary to secondary color
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace gradient {
    constexpr uint8_t COLOR_START = 0;
    constexpr uint8_t COLOR_END = 1;
}

DEFINE_EFFECT_SCHEMA(gradientSchema,
    ParamDesc::Color("colorStart", "Start Color", CRGB::Blue),
    ParamDesc::Color("colorEnd", "End Color", CRGB::Red)
);

void effectGradient(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    (void)params;
    
    CRGB colorStart = paramValues.getColor(gradient::COLOR_START);
    CRGB colorEnd = paramValues.getColor(gradient::COLOR_END);
    
    view.gradient(colorStart, colorEnd);
}

REGISTER_EFFECT_SCHEMA(effectGradient, "gradient", "Gradient", Solid, gradientSchema, 0);

} // namespace lume
