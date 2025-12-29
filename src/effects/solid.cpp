/**
 * Solid effect - All LEDs same color
 */

#include "../core/effect_registry.h"

namespace lume {

void effectSolid(SegmentView& view, const EffectParams& params, uint32_t frame) {
    (void)frame;  // Unused
    view.fill(params.primaryColor);
}

REGISTER_EFFECT("solid", "Solid Color", effectSolid, false, false);

} // namespace lume
