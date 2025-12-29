/**
 * Comet effect - Moving comet with colored tail
 */

#include "../core/effect_registry.h"

namespace lume {

void effectComet(SegmentView& view, const EffectParams& params, uint32_t frame) {
    uint16_t len = view.size();
    if (len == 0) return;
    
    uint8_t tailLength = params.intensity > 0 ? params.intensity / 4 : 30;
    bool goingDown = params.custom1 != 0;  // Direction from custom param
    
    // Fade existing
    view.fade(30);
    
    // Calculate head position
    uint16_t cycle = len + tailLength;
    uint16_t pos = (frame * params.speed / 32) % cycle;
    
    int16_t headPos;
    if (goingDown) {
        headPos = len - 1 - pos;
    } else {
        headPos = pos;
    }
    
    // Draw comet head
    if (headPos >= 0 && headPos < (int16_t)len) {
        view[headPos] = params.primaryColor;
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
            CRGB tailColor = blend(params.secondaryColor, params.primaryColor, fade);
            tailColor.nscale8(fade);
            view[tailPos] = tailColor;
        }
    }
}

REGISTER_EFFECT_COLORS("comet", "Comet", effectComet);

} // namespace lume
