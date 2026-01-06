/**
 * Strobe effect - Fast flashing
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace strobe {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
}

DEFINE_EFFECT_SCHEMA(strobeSchema,
    ParamDesc::Color("color", "Strobe Color", CRGB::White),
    ParamDesc::Int("speed", "Flash Rate", 128, 1, 255)
);

void effectStrobe(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    CRGB color = params.getColor(strobe::COLOR);
    uint8_t speed = params.getInt(strobe::SPEED);
    
    // Speed controls flash rate (higher = faster)
    uint8_t rate = (256 - speed) / 8;
    if (rate < 1) rate = 1;
    
    // Flash on/off based on frame
    bool on = (frame / rate) % 2 == 0;
    
    if (on) {
        view.fill(color);
    } else {
        view.clear();
    }
}

REGISTER_EFFECT_SCHEMA(effectStrobe, "strobe", "Strobe", Animated, strobeSchema, 0);

} // namespace lume
