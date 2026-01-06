/**
 * Comet effect - Moving comet with colored tail
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace comet {
    constexpr uint8_t COLOR_HEAD = 0;
    constexpr uint8_t COLOR_TAIL = 1;
    constexpr uint8_t SPEED = 2;
    constexpr uint8_t INTENSITY = 3;
    constexpr uint8_t DIRECTION = 4;
}

DEFINE_EFFECT_SCHEMA(cometSchema,
    ParamDesc::Color("colorHead", "Head Color", CRGB::White),
    ParamDesc::Color("colorTail", "Tail Color", CRGB::Blue),
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Int("intensity", "Tail Length", 120, 1, 255),
    ParamDesc::Enum("direction", "Direction", "Up|Down", 0)
);

void effectComet(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    (void)params;
    
    CRGB colorHead = paramValues.getColor(comet::COLOR_HEAD);
    CRGB colorTail = paramValues.getColor(comet::COLOR_TAIL);
    uint8_t speed = paramValues.getInt(comet::SPEED);
    uint8_t intensity = paramValues.getInt(comet::INTENSITY);
    bool goingDown = paramValues.getEnum(comet::DIRECTION) != 0;
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    uint8_t tailLength = intensity > 0 ? intensity / 4 : 30;
    
    // Fade existing
    view.fade(30);
    
    // Calculate head position
    uint16_t cycle = len + tailLength;
    uint16_t pos = (frame * speed / 32) % cycle;
    
    int16_t headPos;
    if (goingDown) {
        headPos = len - 1 - pos;
    } else {
        headPos = pos;
    }
    
    // Draw comet head
    if (headPos >= 0 && headPos < (int16_t)len) {
        view[headPos] = colorHead;
    }
    
    // Draw tail
    for (uint8_t i = 1; i <= tailLength; i++) {
        int16_t tailPos;
        if (goingDown) {
            tailPos = headPos + i;
        } else {
            tailPos = headPos - i;
        }
        
        if (tailPos >= 0 && tailPos < (int16_t)len) {
            uint8_t fade = 255 - (i * 255 / tailLength);
            CRGB color = blend(colorTail, colorHead, fade);
            color.nscale8(fade);
            view[tailPos] = color;
        }
    }
}

REGISTER_EFFECT_SCHEMA(effectComet, "comet", "Comet", Moving, cometSchema, 0);

} // namespace lume
