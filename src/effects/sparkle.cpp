/**
 * Sparkle effect - Random white sparkles on colored background
 */

#include "../core/effect_registry.h"

namespace lume {

void effectSparkle(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    
    // Fill with primary color
    view.fill(params.primaryColor);
    
    // Add random white sparkles
    uint8_t numSparkles = max(1, params.speed / 32);
    
    for (uint8_t i = 0; i < numSparkles; i++) {
        uint16_t pos = random16(view.size());
        view[pos] = CRGB::White;
    }
}

REGISTER_EFFECT_FULL(effectSparkle, "sparkle", "Sparkle", Animated, false, true, false, true, false, 0, 1);

} // namespace lume
