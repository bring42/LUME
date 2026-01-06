/**
 * Pride effect - Pride flag rainbow
 * 
 * Classic pride colors flowing smoothly
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace pride {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(prideSchema,
    ParamDesc::Int("speed", "Scroll Speed", 128, 1, 255)
);

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

void effectPride(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    uint8_t speed = params.getInt(pride::SPEED);
    
    uint16_t len = view.size();
    CRGBPalette16 pridePalette = prideGradient;
    
    // Speed controls movement
    uint16_t offset = (frame * speed) >> 4;
    
    for (uint16_t i = 0; i < len; i++) {
        uint8_t colorIndex = (i * 256 / len) + offset;
        view[i] = ColorFromPalette(pridePalette, colorIndex, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_SCHEMA(effectPride, "pride", "Pride", Animated, prideSchema, 0);

} // namespace lume
