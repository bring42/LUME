/**
 * Color Waves effect - Smooth waves of palette colors
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace colorwaves {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(colorwavesSchema,
    ParamDesc::PaletteSelect("palette", "Palette"),
    ParamDesc::Int("speed", "Speed", 128, 1, 255)
);

void effectColorWaves(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    (void)params;
    
    const CRGBPalette16& palette = paramValues.getPalette();
    uint8_t speed = paramValues.getInt(colorwaves::SPEED);
    
    uint16_t len = view.size();
    
    // Speed controls wave movement (scale down to reasonable range)
    uint16_t offset = (frame * speed) >> 6;
    
    for (uint16_t i = 0; i < len; i++) {
        // Calculate palette index with wave motion
        uint8_t colorIndex = (i * 256 / len) + offset;
        
        // Optional: Add sine wave modulation for more organic look
        colorIndex += sin8(i * 4 + (frame >> 2)) >> 2;
        
        view[i] = ColorFromPalette(palette, colorIndex, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_SCHEMA(effectColorWaves, "colorwaves", "Color Waves", Animated, colorwavesSchema, 0);

} // namespace lume
