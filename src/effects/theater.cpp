/**
 * Theater Chase effect - Classic theater marquee chase
 */

#include "../core/effect_registry.h"

namespace lume {

void effectTheaterChase(SegmentView& view, const EffectParams& params, uint32_t frame) {
    uint16_t len = view.size();
    
    // Fade background
    view.fade(100);
    
    // Draw every 3rd LED, offset by frame
    for (uint16_t i = 0; i < len; i += 3) {
        uint16_t idx = (i + frame) % len;
        uint8_t hue = (frame + i * 4) & 0xFF;
        view[idx] = ColorFromPalette(params.palette, hue, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_PALETTE("theater", "Theater Chase", effectTheaterChase);

} // namespace lume
