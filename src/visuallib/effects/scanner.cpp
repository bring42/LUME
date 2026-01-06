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

// Scanner state structure for scratchpad
struct ScannerState {
    int16_t pos;
    int8_t dir;
    uint8_t frameCount;
};

void effectScanner(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    // Access scratchpad state
    ScannerState* state = view.getScratchpad<ScannerState>();
    if (!state) return;
    
    CRGB color = params.getColor(scanner::COLOR);
    uint8_t speed = params.getInt(scanner::SPEED);
    uint8_t intensity = params.getInt(scanner::INTENSITY);
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Reset position on first frame (effect change)
    if (firstFrame) {
        state->pos = 0;
        state->dir = 1;
        state->frameCount = 0;
    }
    
    uint8_t tailLength = intensity > 0 ? intensity / 4 : 20;
    
    // Speed controls movement rate (higher = faster)
    // At speed 255, move every frame. At speed 1, move every ~4 frames (faster)
    uint8_t frameSkip = map(speed, 1, 255, 4, 1);
    
    // Fade existing
    view.fade(40);
    
    // Main dot
    if (state->pos >= 0 && state->pos < (int16_t)len) {
        view[state->pos] = color;
    }
    
    // Tail behind the dot
    for (uint8_t i = 1; i <= tailLength; i++) {
        int16_t tailPos = state->pos - (state->dir * i);
        if (tailPos >= 0 && tailPos < (int16_t)len) {
            uint8_t fade = 255 - (i * 255 / tailLength);
            CRGB tailColor = color;
            tailColor.nscale8(fade);
            view[tailPos] = tailColor;
        }
    }
    
    // Move scanner based on speed
    state->frameCount++;
    if (state->frameCount >= frameSkip) {
        state->frameCount = 0;
        state->pos += state->dir;
    }
    
    // Bounce at edges
    if (state->pos >= (int16_t)len || state->pos < 0) {
        state->dir = -state->dir;
        state->pos = constrain(state->pos, 0, len - 1);
    }
}

REGISTER_EFFECT_SCHEMA(effectScanner, "scanner", "Scanner", Moving, scannerSchema, sizeof(ScannerState));

} // namespace lume
