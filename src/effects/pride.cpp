/**
 * Pride effect - Pride flag rainbow
 * 
 * Classic pride colors flowing smoothly
 */

#include "../core/effect_registry.h"

namespace lume {

// Pride palette - classic rainbow pride colors
DEFINE_GRADIENT_PALETTE(prideGradient) {
    0,   255,   0,   0,    // Red
   42,   255, 127,   0,    // Orange
   84,   255, 255,   0,    // Yellow
  127,     0, 255,   0,    // Green
  170,     0,   0, 255,    // Blue
  212,   139,   0, 255,    // Purple
  255,   255,   0,   0     // Back to red
};

void effectPride(SegmentView& view, const EffectParams& params, uint32_t frame) {
    uint16_t len = view.size();
    CRGBPalette16 pridePalette = prideGradient;
    
    // Speed controls movement
    uint16_t offset = (frame * params.speed) >> 4;
    
    for (uint16_t i = 0; i < len; i++) {
        uint8_t colorIndex = (i * 256 / len) + offset;
        view[i] = ColorFromPalette(pridePalette, colorIndex, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_SIMPLE("pride", effectPride);

} // namespace lume
