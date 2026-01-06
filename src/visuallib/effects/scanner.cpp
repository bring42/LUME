/**
 * Scanner effect - Larson scanner / Cylon eye
 * 
 * A single dot bounces back and forth with a fading tail
 * 
 * TODO: Migrate static state to scratchpad for multi-segment support
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace scanner {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
    constexpr uint8_t INTENSITY = 2;
}

DEFINE_EFFECT_SCHEMA(scannerSchema,
    ParamDesc::Color("color", "Color", CRGB::Red),
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Int("intensity", "Tail Length", 80, 1, 255)
);

// Static position and direction (will move to scratchpad in Phase 1)
static int16_t scannerPos = 0;
static int8_t scannerDir = 1;

// Static for frame skip timing
static uint8_t scannerFrameCount = 0;

void effectScanner(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)params;
    
    CRGB color = paramValues.getColor(scanner::COLOR);
    uint8_t speed = paramValues.getInt(scanner::SPEED);
    uint8_t intensity = paramValues.getInt(scanner::INTENSITY);
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Reset position on first frame (effect change)
    if (firstFrame) {
        scannerPos = 0;
        scannerDir = 1;
        scannerFrameCount = 0;
    }
    
    uint8_t tailLength = intensity > 0 ? intensity / 4 : 20;
    
    // Speed controls movement rate (higher = faster)
    // At speed 255, move every frame. At speed 1, move every ~4 frames (faster)
    uint8_t frameSkip = map(speed, 1, 255, 4, 1);
    
    // Fade existing
    view.fade(40);
    
    // Main dot
    if (scannerPos >= 0 && scannerPos < (int16_t)len) {
        view[scannerPos] = color;
    }
    
    // Tail behind the dot
    for (uint8_t i = 1; i <= tailLength; i++) {
        int16_t tailPos = scannerPos - (scannerDir * i);
        if (tailPos >= 0 && tailPos < (int16_t)len) {
            uint8_t fade = 255 - (i * 255 / tailLength);
            CRGB tailColor = color;
            tailColor.nscale8(fade);
            view[tailPos] = tailColor;
        }
    }
    
    // Move scanner based on speed
    scannerFrameCount++;
    if (scannerFrameCount >= frameSkip) {
        scannerFrameCount = 0;
        scannerPos += scannerDir;
    }
    
    // Bounce at edges
    if (scannerPos >= (int16_t)len || scannerPos < 0) {
        scannerDir = -scannerDir;
        scannerPos = constrain(scannerPos, 0, len - 1);
    }
}

REGISTER_EFFECT_SCHEMA(effectScanner, "scanner", "Scanner", Moving, scannerSchema, 0);

} // namespace lume
