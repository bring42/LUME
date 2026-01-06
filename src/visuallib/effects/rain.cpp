/**
 * Rain effect - Drops falling from top
 * 
 * TODO: Migrate static state to scratchpad for multi-segment support
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace rain {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
    constexpr uint8_t INTENSITY = 2;
}

DEFINE_EFFECT_SCHEMA(rainSchema,
    ParamDesc::Color("color", "Drop Color", CRGB::Blue),
    ParamDesc::Int("speed", "Fall Speed", 128, 1, 255),
    ParamDesc::Int("intensity", "Drop Density", 128, 1, 255)
);

// Drop state structure for scratchpad
constexpr uint8_t MAX_DROPS = 10;
struct RainState {
    uint8_t dropBrightness[MAX_DROPS];
    uint16_t dropPosition[MAX_DROPS];
};

void effectRain(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    // Access scratchpad state
    RainState* state = view.getScratchpad<RainState>();
    if (!state) return;
    
    CRGB color = params.getColor(rain::COLOR);
    uint8_t speed = params.getInt(rain::SPEED);
    uint8_t intensity = params.getInt(rain::INTENSITY);
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Reset state on first frame
    if (firstFrame) {
        memset(state->dropBrightness, 0, sizeof(state->dropBrightness));
        memset(state->dropPosition, 0, sizeof(state->dropPosition));
    }
    
    uint8_t density = intensity > 0 ? intensity / 5 : 10;
    uint8_t dropSpeed = max((uint8_t)1, (uint8_t)(speed / 30));
    
    // Fade existing
    view.fade(50);
    
    // Update and draw drops
    for (uint8_t d = 0; d < MAX_DROPS; d++) {
        if (state->dropBrightness[d] > 0) {
            state->dropPosition[d] += dropSpeed;
            
            if (state->dropPosition[d] < len) {
                // Drops fall from top (high index) to bottom (low index)
                uint16_t pixelPos = len - 1 - state->dropPosition[d];
                CRGB colorScaled = color;
                colorScaled.nscale8(state->dropBrightness[d]);
                view[pixelPos] = colorScaled;
            } else {
                // Drop reached bottom
                state->dropBrightness[d] = 0;
            }
        }
    }
    
    // Spawn new drops
    if (random8() < density) {
        for (uint8_t d = 0; d < MAX_DROPS; d++) {
            if (state->dropBrightness[d] == 0) {
                state->dropBrightness[d] = random8(150, 255);
                state->dropPosition[d] = 0;
                break;
            }
        }
    }
}

REGISTER_EFFECT_SCHEMA(effectRain, "rain", "Rain", Moving, rainSchema, sizeof(RainState));

} // namespace lume
