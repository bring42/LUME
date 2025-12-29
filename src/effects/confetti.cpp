/**
 * Confetti effect - Random colored pixels flashing
 */

#include "../core/effect_registry.h"

namespace lume {

void effectConfetti(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    
    // Fade all pixels slightly
    view.fade(10);
    
    // Add random colored pixels
    uint8_t chance = params.speed / 8;  // More speed = more confetti
    
    if (random8() < chance + 20) {
        uint16_t pos = random16(view.size());
        view[pos] += ColorFromPalette(params.palette, random8(), 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_PALETTE(effectConfetti, "confetti", "Confetti");

} // namespace lume
