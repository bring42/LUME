/**
 * Theater Chase effect - Classic theater marquee chase
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace theater {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(theaterSchema,
    ParamDesc::PaletteSelect("palette", "Palette"),
    ParamDesc::Int("speed", "Speed", 128, 1, 255)
);

void effectTheaterChase(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    (void)params;
    
    const CRGBPalette16& palette = paramValues.getPalette();
    uint8_t speed = paramValues.getInt(theater::SPEED);
    
    uint16_t len = view.size();
    
    // Speed controls chase rate
    uint16_t scaledFrame = (frame * speed) >> 7;
    
    // Fade background
    view.fade(100);
    
    // Draw every 3rd LED, offset by scaled frame
    for (uint16_t i = 0; i < len; i += 3) {
        uint16_t idx = (i + scaledFrame) % len;
        uint8_t hue = (scaledFrame + i * 4) & 0xFF;
        view[idx] = ColorFromPalette(palette, hue, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_SCHEMA(effectTheaterChase, "theater", "Theater Chase", Moving, theaterSchema, 0);

} // namespace lume
