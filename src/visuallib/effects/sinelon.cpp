/**
 * Sinelon effect - Bouncing dot with fading trail
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace sinelon {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(sinelonSchema,
    ParamDesc::PaletteSelect("palette", "Palette"),
    ParamDesc::Int("speed", "Speed", 128, 1, 255)
);

void effectSinelon(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    const CRGBPalette16& palette = params.getPalette();
    uint8_t speed = params.getInt(sinelon::SPEED);
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Fade the trail
    view.fade(20);
    
    // Calculate bouncing position using sine wave
    uint8_t bpm = speed / 10 + 5;
    uint16_t pos = beatsin16(bpm, 0, len - 1);
    
    // Color cycles with frame
    uint8_t hue = frame & 0xFF;
    view[pos] += ColorFromPalette(palette, hue, 255, LINEARBLEND);
}

REGISTER_EFFECT_SCHEMA(effectSinelon, "sinelon", "Sinelon", Moving, sinelonSchema, 0);

} // namespace lume
