/**
 * Meteor effect - Falling meteor with fading tail
 */

#include "../core/effect_registry.h"

namespace lume {

void effectMeteor(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Meteor size relative to segment
    uint8_t meteorSize = max(2, len / 20);
    
    // Speed controls meteor fall rate
    uint16_t pos = (frame * params.speed / 32) % (len + meteorSize * 2);
    
    // Fade all pixels
    view.fade(64);
    
    // Draw meteor
    for (uint8_t i = 0; i < meteorSize; i++) {
        if (pos >= i && (pos - i) < len) {
            view[pos - i] = params.primaryColor;
        }
    }
    
    // Random trail sparkle
    for (uint16_t i = 0; i < len; i++) {
        if (random8() < 20) {
            view.raw()[i].fadeToBlackBy(random8(20, 60));
        }
    }
}

REGISTER_EFFECT_FULL(effectMeteor, "meteor", "Meteor", Moving, false, true, false, true, true, 0, 1);

} // namespace lume
