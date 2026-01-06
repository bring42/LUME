/**
 * Pacifica effect - Ocean waves
 * 
 * Gentle oceanic waves in blues and greens
 * Inspired by Mark Kriegsman's Pacifica
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace pacifica {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(pacificaSchema,
    ParamDesc::Int("speed", "Wave Speed", 128, 1, 255)
);

// Ocean color palettes
DEFINE_GRADIENT_PALETTE(pacifica1) {
    0,   0,  5, 20,
   25,   0, 10, 40,
   50,   0, 15, 60,
  127,   0, 40,100,
  200,   0, 80,160,
  255,   0,100,200
};

DEFINE_GRADIENT_PALETTE(pacifica2) {
    0,   0, 20, 50,
  127,   0, 60,120,
  255,  20,100,180
};

DEFINE_GRADIENT_PALETTE(pacifica3) {
    0,   0, 50, 80,
  127,  40,100,150,
  255, 100,180,220
};

void effectPacifica(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    uint8_t speed = paramValues.getInt(pacifica::SPEED);
    
    uint16_t len = view.size();
    
    CRGBPalette16 pal1 = pacifica1;
    CRGBPalette16 pal2 = pacifica2;
    CRGBPalette16 pal3 = pacifica3;
    
    // Multiple wave layers
    uint32_t speed1 = frame * speed / 32;
    uint32_t speed2 = frame * speed / 24;
    uint32_t speed3 = frame * speed / 40;
    
    for (uint16_t i = 0; i < len; i++) {
        // Layer 1 - slow deep waves
        uint8_t wave1 = inoise8(i * 20, speed1);
        CRGB color = ColorFromPalette(pal1, wave1);
        
        // Layer 2 - medium waves
        uint8_t wave2 = inoise8(i * 30 + 1000, speed2);
        color += ColorFromPalette(pal2, wave2);
        
        // Layer 3 - fast surface ripples
        uint8_t wave3 = inoise8(i * 50 + 2000, speed3);
        CRGB ripple = ColorFromPalette(pal3, wave3);
        ripple.nscale8(64);  // Subtle ripples
        color += ripple;
        
        // Normalize
        color.nscale8(180);
        
        view[i] = color;
    }
}

REGISTER_EFFECT_SCHEMA(effectPacifica, "pacifica", "Pacifica", Animated, pacificaSchema, 0);

} // namespace lume
