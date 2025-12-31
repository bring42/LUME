/**
 * Theater Chase effect - Classic theater marquee chase
 */

#include "../core/effect_registry.h"

namespace lume {

void effectTheaterChase(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    uint16_t len = view.size();
    
    // Speed controls chase rate
    uint16_t scaledFrame = (frame * params.speed) >> 6;
    
    // Fade background
    view.fade(100);
    
    // Draw every 3rd LED, offset by scaled frame
    for (uint16_t i = 0; i < len; i += 3) {
        uint16_t idx = (i + scaledFrame) % len;
        uint8_t hue = (scaledFrame + i * 4) & 0xFF;
        view[idx] = ColorFromPalette(params.palette, hue, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_PALETTE(effectTheaterChase, "theater", "Theater Chase");

} // namespace lume
