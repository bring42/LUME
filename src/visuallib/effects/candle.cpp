/**
 * Candle effect - Realistic candle flicker
 * 
 * All LEDs flicker together with slight variations for a cozy look
 * 
 * TODO: Migrate static state to scratchpad for multi-segment support
 */

#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"

namespace lume {

// Define parameter slots
namespace candle {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
    constexpr uint8_t INTENSITY = 2;
}

// Define schema
DEFINE_EFFECT_SCHEMA(candleSchema,
    ParamDesc::Color("color", "Color", CRGB(255, 140, 40)),  // Warm candle color
    ParamDesc::Int("speed", "Flicker Speed", 128, 1, 255),
    ParamDesc::Int("intensity", "Flicker Intensity", 128, 1, 255)
);

// Static state for smooth flicker (will move to scratchpad in Phase 1)
static uint8_t candleBase = 200;
static uint8_t candleTarget = 200;
static uint32_t lastFlickerChange = 0;

void effectCandle(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    // Read parameters
    CRGB baseColor = paramValues.getColor(candle::COLOR);
    uint8_t speed = paramValues.getInt(candle::SPEED);
    uint8_t intensity = paramValues.getInt(candle::INTENSITY);
    
    // Reset state on first frame
    if (firstFrame) {
        candleBase = 200;
        candleTarget = 200;
        lastFlickerChange = 0;
    }
    
    uint32_t now = millis();
    
    // Speed affects flicker frequency
    uint32_t flickerDelay = map(speed, 1, 255, 150, 10);
    
    // Occasionally pick a new flicker target
    if (now - lastFlickerChange > flickerDelay) {
        lastFlickerChange = now;
        
        // Intensity affects dip depth (more sensitive)
        uint8_t maxDip = map(intensity, 1, 255, 220, 50);
        
        // More random flickering
        if (random8() < 50) {
            // Bigger dip
            candleTarget = random8(maxDip - 80, maxDip - 30);
        } else if (random8() < 120) {
            // Small dip
            candleTarget = random8(maxDip - 30, maxDip);
        } else {
            // Stay bright
            candleTarget = random8(200, 255);
        }
    }
    
    // Smooth transition to target
    if (candleBase < candleTarget) candleBase += 3;
    if (candleBase > candleTarget) candleBase -= 5;  // Faster dim than brighten
    
    // Apply to all LEDs with tiny per-LED variation
    for (uint16_t i = 0; i < view.size(); i++) {
        uint8_t variation = random8(0, 15);
        uint8_t brightness = qadd8(candleBase, variation) - 7;
        
        CRGB color = baseColor;
        color.nscale8(brightness);
        view[i] = color;
    }
}

REGISTER_EFFECT_SCHEMA(effectCandle, "candle", "Candle", Animated, candleSchema, 0);

} // namespace lume
