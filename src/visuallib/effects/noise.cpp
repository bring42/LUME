/**
 * Noise effect - Perlin noise visualization
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace noise {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(noiseSchema,
    ParamDesc::PaletteSelect("palette", "Palette"),
    ParamDesc::Int("speed", "Speed", 128, 1, 255)
);

void effectNoise(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    const CRGBPalette16& palette = params.getPalette();
    uint8_t speed = params.getInt(noise::SPEED);
    
    uint16_t len = view.size();
    
    // Scale and speed for noise
    uint16_t scale = 30;
    
    // Time offset based on frame and speed
    uint32_t timeOffset = frame * speed;
    
    for (uint16_t i = 0; i < len; i++) {
        // Get noise value for this position and time
        uint8_t noiseVal = inoise8(i * scale, timeOffset >> 6);
        
        // Map noise to palette color
        view[i] = ColorFromPalette(palette, noiseVal, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_SCHEMA(effectNoise, "noise", "Noise", Animated, noiseSchema, 0);

} // namespace lume
