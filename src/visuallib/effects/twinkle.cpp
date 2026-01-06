/**
 * Twinkle effect - Random LEDs fade in and out
 * 
 * Creates a cozy twinkling starfield effect
 * 
 * TODO: Migrate static state to scratchpad for multi-segment support
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace twinkle {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
}

DEFINE_EFFECT_SCHEMA(twinkleSchema,
    ParamDesc::Color("color", "Color", CRGB::White),
    ParamDesc::Int("speed", "Twinkle Rate", 128, 1, 255)
);

// Twinkle state structure for scratchpad
// 0 = off, 1-127 = fading in, 128-255 = fading out
struct TwinkleState {
    uint8_t state[300];
};

void effectTwinkle(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    // Access scratchpad state
    TwinkleState* state = view.getScratchpad<TwinkleState>();
    if (!state) return;
    
    CRGB color = params.getColor(twinkle::COLOR);
    uint8_t speed = params.getInt(twinkle::SPEED);
    
    uint16_t len = min(view.size(), (uint16_t)300);
    
    // Reset state on first frame
    if (firstFrame) {
        memset(state->state, 0, sizeof(state->state));
    }
    
    uint8_t spawnChance = map(speed, 1, 255, 5, 40);
    
    for (uint16_t i = 0; i < len; i++) {
        if (state->state[i] == 0) {
            // Maybe start a new twinkle
            if (random8() < spawnChance) {
                state->state[i] = 1;
            }
            view[i] = CRGB::Black;
        } else if (state->state[i] < 128) {
            // Fading in
            state->state[i] += 4;
            if (state->state[i] >= 128) state->state[i] = 128;
            
            uint8_t bri = state->state[i] * 2;
            CRGB colorScaled = color;
            colorScaled.nscale8(bri);
            view[i] = colorScaled;
        } else {
            // Fading out
            state->state[i] += 2;
            
            uint8_t bri = (255 - state->state[i]) * 2;
            CRGB colorScaled = color;
            colorScaled.nscale8(bri);
            view[i] = colorScaled;
            
            if (state->state[i] >= 254) state->state[i] = 0;
        }
    }
}

REGISTER_EFFECT_SCHEMA(effectTwinkle, "twinkle", "Twinkle", Animated, twinkleSchema, sizeof(TwinkleState));

} // namespace lume
