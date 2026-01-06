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

// Drop state (will move to scratchpad in Phase 1)
constexpr uint8_t MAX_DROPS = 10;
static uint8_t dropBrightness[MAX_DROPS] = {0};
static uint16_t dropPosition[MAX_DROPS] = {0};

void effectRain(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)params;
    
    CRGB color = paramValues.getColor(rain::COLOR);
    uint8_t speed = paramValues.getInt(rain::SPEED);
    uint8_t intensity = paramValues.getInt(rain::INTENSITY);
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Reset state on first frame
    if (firstFrame) {
        memset(dropBrightness, 0, sizeof(dropBrightness));
        memset(dropPosition, 0, sizeof(dropPosition));
    }
    
    uint8_t density = intensity > 0 ? intensity / 5 : 10;
    uint8_t dropSpeed = max((uint8_t)1, (uint8_t)(speed / 30));
    
    // Fade existing
    view.fade(50);
    
    // Update and draw drops
    for (uint8_t d = 0; d < MAX_DROPS; d++) {
        if (dropBrightness[d] > 0) {
            dropPosition[d] += dropSpeed;
            
            if (dropPosition[d] < len) {
                // Drops fall from top (high index) to bottom (low index)
                uint16_t pixelPos = len - 1 - dropPosition[d];
                CRGB colorScaled = color;
                colorScaled.nscale8(dropBrightness[d]);
                view[pixelPos] = colorScaled;
            } else {
                // Drop reached bottom
                dropBrightness[d] = 0;
            }
        }
    }
    
    // Spawn new drops
    if (random8() < density) {
        for (uint8_t d = 0; d < MAX_DROPS; d++) {
            if (dropBrightness[d] == 0) {
                dropBrightness[d] = random8(150, 255);
                dropPosition[d] = 0;
                break;
            }
        }
    }
}

REGISTER_EFFECT_SCHEMA(effectRain, "rain", "Rain", Moving, rainSchema, 0);

} // namespace lume
