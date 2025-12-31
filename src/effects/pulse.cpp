/**
 * Pulse/Breathe effect - All LEDs fade in and out
 */

#include "../core/effect_registry.h"

namespace lume {

void effectPulse(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    // Use beatsin8 for smooth sine wave
    // Speed controls frequency (higher = faster)
    uint8_t bpm = params.speed / 4;  // Map 1-255 to reasonable BPM
    if (bpm < 10) bpm = 10;
    
    uint8_t brightness = beatsin8(bpm, 20, 255);
    
    // Fill with primary color, scaled by brightness
    CRGB color = params.primaryColor;
    color.nscale8(brightness);
    
    view.fill(color);
}

// Animated effect using primary color and speed
REGISTER_EFFECT_FULL(effectPulse, "pulse", "Pulse", Animated, false, true, false, true, false, 0, 1);

} // namespace lume
