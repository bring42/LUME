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

// Candle state structure for scratchpad
struct CandleState {
    uint8_t base;
    uint8_t target;
    uint32_t lastFlickerChange;
};

void effectCandle(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    // Access scratchpad state
    CandleState* state = view.getScratchpad<CandleState>();
    if (!state) return;
    
    // Read parameters
    CRGB baseColor = params.getColor(candle::COLOR);
    uint8_t speed = params.getInt(candle::SPEED);
    uint8_t intensity = params.getInt(candle::INTENSITY);
    
    // Reset state on first frame
    if (firstFrame) {
        state->base = 200;
        state->target = 200;
        state->lastFlickerChange = 0;
    }
    
    uint32_t now = millis();
    
    // Speed affects flicker frequency
    uint32_t flickerDelay = map(speed, 1, 255, 150, 10);
    
    // Occasionally pick a new flicker target
    if (now - state->lastFlickerChange > flickerDelay) {
        state->lastFlickerChange = now;
        
        // Intensity affects dip depth (more sensitive)
        uint8_t maxDip = map(intensity, 1, 255, 220, 50);
        
        // More random flickering
        if (random8() < 50) {
            // Bigger dip
            state->target = random8(maxDip - 80, maxDip - 30);
        } else if (random8() < 120) {
            // Small dip
            state->target = random8(maxDip - 30, maxDip);
        } else {
            // Stay bright
            state->target = random8(200, 255);
        }
    }
    
    // Smooth transition to target
    if (state->base < state->target) state->base += 3;
    if (state->base > state->target) state->base -= 5;  // Faster dim than brighten
    
    // Apply to all LEDs with tiny per-LED variation
    for (uint16_t i = 0; i < view.size(); i++) {
        uint8_t variation = random8(0, 15);
        uint8_t brightness = qadd8(state->base, variation) - 7;
        
        CRGB color = baseColor;
        color.nscale8(brightness);
        view[i] = color;
    }
}

REGISTER_EFFECT_SCHEMA(effectCandle, "candle", "Candle", Animated, candleSchema, sizeof(CandleState));

} // namespace lume
