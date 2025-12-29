/**
 * Twinkle effect - Random LEDs fade in and out
 * 
 * Creates a cozy twinkling starfield effect
 */

#include "../core/effect_registry.h"

namespace lume {

// Twinkle state - per-LED fade state
// 0 = off, 1-127 = fading in, 128-255 = fading out
static uint8_t twinkleState[300] = {0};

void effectTwinkle(SegmentView& view, const EffectParams& params, uint32_t frame) {
    (void)frame;
    
    uint16_t len = min(view.size(), (uint16_t)300);
    uint8_t spawnChance = map(params.speed, 1, 255, 5, 40);
    
    for (uint16_t i = 0; i < len; i++) {
        if (twinkleState[i] == 0) {
            // Maybe start a new twinkle
            if (random8() < spawnChance) {
                twinkleState[i] = 1;
            }
            view[i] = CRGB::Black;
        } else if (twinkleState[i] < 128) {
            // Fading in
            twinkleState[i] += 4;
            if (twinkleState[i] >= 128) twinkleState[i] = 128;
            
            uint8_t bri = twinkleState[i] * 2;
            CRGB color = params.primaryColor;
            color.nscale8(bri);
            view[i] = color;
        } else {
            // Fading out
            twinkleState[i] += 2;
            
            uint8_t bri = (255 - twinkleState[i]) * 2;
            CRGB color = params.primaryColor;
            color.nscale8(bri);
            view[i] = color;
            
            if (twinkleState[i] >= 254) twinkleState[i] = 0;
        }
    }
}

REGISTER_EFFECT("twinkle", "Twinkle", effectTwinkle, false, false);

} // namespace lume
