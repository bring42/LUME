/**
 * Meteor effect - Falling meteor with fading tail
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace meteor {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
}

DEFINE_EFFECT_SCHEMA(meteorSchema,
    ParamDesc::Color("color", "Meteor Color", CRGB::White),
    ParamDesc::Int("speed", "Fall Speed", 128, 1, 255)
);

void effectMeteor(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    CRGB color = params.getColor(meteor::COLOR);
    uint8_t speed = params.getInt(meteor::SPEED);
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Meteor size relative to segment
    uint8_t meteorSize = max(2, len / 20);
    
    // Speed controls meteor fall rate
    uint16_t pos = (frame * speed / 32) % (len + meteorSize * 2);
    
    // Fade all pixels
    view.fade(64);
    
    // Draw meteor
    for (uint8_t i = 0; i < meteorSize; i++) {
        if (pos >= i && (pos - i) < len) {
            view[pos - i] = color;
        }
    }
    
    // Random trail sparkle
    for (uint16_t i = 0; i < len; i++) {
        if (random8() < 20) {
            view.raw()[i].fadeToBlackBy(random8(20, 60));
        }
    }
}

REGISTER_EFFECT_SCHEMA(effectMeteor, "meteor", "Meteor", Moving, meteorSchema, 0);

} // namespace lume
