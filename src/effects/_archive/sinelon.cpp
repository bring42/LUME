/**
 * Sinelon effect - Bouncing dot with fading trail
 */

#include "../core/effect_registry.h"

namespace lume {

void effectSinelon(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Fade the trail
    view.fade(20);
    
    // Calculate bouncing position using sine wave
    uint8_t bpm = params.speed / 10 + 5;
    uint16_t pos = beatsin16(bpm, 0, len - 1);
    
    // Color cycles with frame
    uint8_t hue = frame & 0xFF;
    view[pos] += ColorFromPalette(params.palette, hue, 255, LINEARBLEND);
}

REGISTER_EFFECT_PALETTE(effectSinelon, "sinelon", "Sinelon");

} // namespace lume
