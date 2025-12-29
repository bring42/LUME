/**
 * Solid effect - All LEDs same color
 */

#include "../core/effect_registry.h"

namespace lume {

void effectSolid(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    view.fill(params.primaryColor);
}

REGISTER_EFFECT_SOLID(effectSolid, "solid", "Solid Color");

} // namespace lume
