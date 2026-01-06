/**
 * Confetti effect - Random colored pixels flashing
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace confetti {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(confettiSchema,
    ParamDesc::PaletteSelect("palette", "Palette"),
    ParamDesc::Int("speed", "Spawn Rate", 128, 1, 255)
);

void effectConfetti(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    
    const CRGBPalette16& palette = params.getPalette();
    uint8_t speed = params.getInt(confetti::SPEED);
    
    // Fade all pixels slightly
    view.fade(10);
    
    // Add random colored pixels
    uint8_t chance = speed / 8;  // More speed = more confetti
    
    if (random8() < chance + 20) {
        uint16_t pos = random16(view.size());
        view[pos] += ColorFromPalette(palette, random8(), 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_SCHEMA(effectConfetti, "confetti", "Confetti", Animated, confettiSchema, 0);

} // namespace lume
