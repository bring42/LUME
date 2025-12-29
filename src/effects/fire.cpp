/**
 * Fire effect - Realistic fire simulation
 * 
 * Uses the classic Fire2012 algorithm adapted for segments
 * 
 * TODO: Migrate static heat array to scratchpad for multi-segment support
 */

#include "../core/effect_registry.h"

namespace lume {

// Fire parameters
constexpr uint8_t FIRE_COOLING = 55;
constexpr uint8_t FIRE_SPARKING = 120;

// Static heat array (will move to scratchpad in Phase 1)
static uint8_t heat[300];

void effectFire(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Use intensity for cooling, speed for sparking
    uint8_t cooling = params.intensity > 0 ? params.intensity : FIRE_COOLING;
    uint8_t sparking = params.speed > 0 ? params.speed : FIRE_SPARKING;
    
    // Limit to array size
    len = min(len, (uint16_t)300);
    
    // Reset heat on first frame
    if (firstFrame) {
        memset(heat, 0, sizeof(heat));
    }
    
    // Step 1: Cool down every cell a little
    for (uint16_t i = 0; i < len; i++) {
        heat[i] = qsub8(heat[i], random8(0, ((cooling * 10) / len) + 2));
    }
    
    // Step 2: Heat from each cell drifts up and diffuses
    for (uint16_t k = len - 1; k >= 2; k--) {
        heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    }
    
    // Step 3: Randomly ignite new sparks near bottom
    if (random8() < sparking) {
        uint8_t y = random8(7);
        if (y < len) {
            heat[y] = qadd8(heat[y], random8(160, 255));
        }
    }
    
    // Step 4: Map heat to LED colors
    for (uint16_t j = 0; j < len; j++) {
        // Scale heat value to 0-255
        uint8_t colorIndex = scale8(heat[j], 240);
        
        // Use HeatColors palette
        CRGB color = ColorFromPalette(HeatColors_p, colorIndex);
        
        // Write to segment (respects reversal)
        view[j] = color;
    }
}

REGISTER_EFFECT_ANIMATED(effectFire, "fire", "Fire");

} // namespace lume
