/**
 * Noise effect - Perlin noise visualization
 */

#include "../core/effect_registry.h"

namespace lume {

void effectNoise(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    uint16_t len = view.size();
    
    // Scale and speed for noise
    uint16_t scale = 30;
    uint16_t speed = params.speed;
    
    // Time offset based on frame and speed
    uint32_t timeOffset = frame * speed;
    
    for (uint16_t i = 0; i < len; i++) {
        // Get noise value for this position and time
        uint8_t noise = inoise8(i * scale, timeOffset >> 4);
        
        // Map noise to palette color
        view[i] = ColorFromPalette(params.palette, noise, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_PALETTE(effectNoise, "noise", "Noise");

} // namespace lume
