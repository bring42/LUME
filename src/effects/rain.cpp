/**
 * Rain effect - Drops falling from top
 * 
 * TODO: Migrate static state to scratchpad for multi-segment support
 */

#include "../core/effect_registry.h"

namespace lume {

// Drop state (will move to scratchpad in Phase 1)
constexpr uint8_t MAX_DROPS = 10;
static uint8_t dropBrightness[MAX_DROPS] = {0};
static uint16_t dropPosition[MAX_DROPS] = {0};

void effectRain(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Reset state on first frame
    if (firstFrame) {
        memset(dropBrightness, 0, sizeof(dropBrightness));
        memset(dropPosition, 0, sizeof(dropPosition));
    }
    
    uint8_t density = params.intensity > 0 ? params.intensity / 5 : 10;
    uint8_t speed = max((uint8_t)1, (uint8_t)(params.speed / 30));
    
    // Fade existing
    view.fade(50);
    
    // Update and draw drops
    for (uint8_t d = 0; d < MAX_DROPS; d++) {
        if (dropBrightness[d] > 0) {
            dropPosition[d] += speed;
            
            if (dropPosition[d] < len) {
                // Drops fall from top (high index) to bottom (low index)
                uint16_t pixelPos = len - 1 - dropPosition[d];
                CRGB color = params.primaryColor;
                color.nscale8(dropBrightness[d]);
                view[pixelPos] = color;
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

REGISTER_EFFECT_FULL(effectRain, "rain", "Rain", Moving, false, true, false, true, true, 0, 1);

} // namespace lume
