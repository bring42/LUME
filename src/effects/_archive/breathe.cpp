/**
 * Breathe effect - Entire strip pulses together
 * 
 * Simple, uniform breathing - what most people expect
 */

#include "../core/effect_registry.h"

namespace lume {

void effectBreathe(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    
    // Map speed to BPM (breaths per minute)
    uint8_t bpm = map(params.speed, 1, 255, 5, 30);
    
    // Sine wave breathing - never fully off (looks weird)
    uint8_t breath = beatsin8(bpm, 20, 255);
    
    // Apply primary color at breathing brightness
    CRGB color = params.primaryColor;
    color.nscale8(breath);
    
    view.fill(color);
}

REGISTER_EFFECT_SIMPLE_NAMED(effectBreathe, "breathe", "Breathe");

} // namespace lume
