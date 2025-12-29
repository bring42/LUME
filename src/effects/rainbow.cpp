/**
 * Rainbow effect - Smooth cycling rainbow across all LEDs
 */

#include "../core/effect_registry.h"

namespace lume {

void effectRainbow(SegmentView& view, const EffectParams& params, uint32_t frame) {
    // Speed controls how fast the rainbow moves
    uint8_t hue = (frame * params.speed) >> 6;
    
    // Delta hue spreads the rainbow across the segment
    uint8_t deltaHue = 255 / max((uint16_t)1, view.size());
    
    view.rainbow(hue, deltaHue);
}

REGISTER_EFFECT_SIMPLE("rainbow", effectRainbow);

} // namespace lume
