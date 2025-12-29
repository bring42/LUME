/**
 * Candle effect - Realistic candle flicker
 * 
 * All LEDs flicker together with slight variations for a cozy look
 * 
 * TODO: Migrate static state to scratchpad for multi-segment support
 */

#include "../core/effect_registry.h"

namespace lume {

// Static state for smooth flicker (will move to scratchpad in Phase 1)
static uint8_t candleBase = 200;
static uint8_t candleTarget = 200;
static uint32_t lastFlickerChange = 0;

void effectCandle(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    // Reset state on first frame
    if (firstFrame) {
        candleBase = 200;
        candleTarget = 200;
        lastFlickerChange = 0;
    }
    
    uint32_t now = millis();
    
    // Occasionally pick a new flicker target
    if (now - lastFlickerChange > random8(50, 150)) {
        lastFlickerChange = now;
        
        // Mostly stay bright, occasionally dip
        if (random8() < 30) {
            // Bigger dip
            candleTarget = random8(100, 160);
        } else if (random8() < 100) {
            // Small dip
            candleTarget = random8(180, 220);
        } else {
            // Stay bright
            candleTarget = random8(220, 255);
        }
    }
    
    // Smooth transition to target
    if (candleBase < candleTarget) candleBase += 3;
    if (candleBase > candleTarget) candleBase -= 5;  // Faster dim than brighten
    
    // Default warm candle color if primary is orange-ish
    CRGB baseColor = params.primaryColor;
    bool useWarmCandle = (baseColor.r > 200 && baseColor.g < 150 && baseColor.b < 100);
    
    // Apply to all LEDs with tiny per-LED variation
    for (uint16_t i = 0; i < view.size(); i++) {
        uint8_t variation = random8(0, 15);
        uint8_t brightness = qadd8(candleBase, variation) - 7;
        
        CRGB color;
        if (useWarmCandle) {
            // Classic warm candle
            color = CRGB(255, 100 + random8(30), 10);
        } else {
            color = baseColor;
        }
        
        color.nscale8(brightness);
        view[i] = color;
    }
}

REGISTER_EFFECT_ANIMATED(effectCandle, "candle", "Candle");

} // namespace lume
