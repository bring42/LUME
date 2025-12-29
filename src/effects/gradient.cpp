/**
 * Gradient effect - Static gradient from primary to secondary color
 */

#include "../core/effect_registry.h"

namespace lume {

void effectGradient(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    
    view.gradient(params.primaryColor, params.secondaryColor);
}

REGISTER_EFFECT_COLORS(effectGradient, "gradient", "Gradient");

} // namespace lume
