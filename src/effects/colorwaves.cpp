/**
 * Color Waves effect - Smooth waves of palette colors
 */

#include "../core/effect_registry.h"

namespace lume {

void effectColorWaves(SegmentView& view, const EffectParams& params, uint32_t frame) {
    uint16_t len = view.size();
    
    // Speed controls wave movement
    uint16_t offset = (frame * params.speed) >> 4;
    
    for (uint16_t i = 0; i < len; i++) {
        // Calculate palette index with wave motion
        uint8_t colorIndex = (i * 256 / len) + offset;
        
        // Optional: Add sine wave modulation for more organic look
        colorIndex += sin8(i * 4 + (frame >> 2)) >> 2;
        
        view[i] = ColorFromPalette(params.palette, colorIndex, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_PALETTE("colorwaves", "Color Waves", effectColorWaves);

} // namespace lume
