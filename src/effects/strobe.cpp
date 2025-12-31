/**
 * Strobe effect - Fast flashing
 */

#include "../core/effect_registry.h"

namespace lume {

void effectStrobe(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    // Speed controls flash rate
    uint8_t rate = params.speed / 8;
    if (rate < 1) rate = 1;
    
    // Flash on/off based on frame
    bool on = (frame / rate) % 2 == 0;
    
    if (on) {
        view.fill(params.primaryColor);
    } else {
        view.clear();
    }
}

REGISTER_EFFECT_FULL(effectStrobe, "strobe", "Strobe", Animated, false, true, false, true, false, 0, 1);

} // namespace lume
