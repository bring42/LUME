/**
 * Scanner effect - Larson scanner / Cylon eye
 * 
 * A single dot bounces back and forth with a fading tail
 * 
 * TODO: Migrate static state to scratchpad for multi-segment support
 */

#include "../core/effect_registry.h"

namespace lume {

// Static position and direction (will move to scratchpad in Phase 1)
static int16_t scannerPos = 0;
static int8_t scannerDir = 1;

void effectScanner(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)frame;
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    // Reset position on first frame (effect change)
    if (firstFrame) {
        scannerPos = 0;
        scannerDir = 1;
    }
    
    uint8_t tailLength = params.intensity > 0 ? params.intensity / 4 : 20;
    
    // Fade existing
    view.fade(40);
    
    // Main dot
    if (scannerPos >= 0 && scannerPos < (int16_t)len) {
        view[scannerPos] = params.primaryColor;
    }
    
    // Tail behind the dot
    for (uint8_t i = 1; i <= tailLength; i++) {
        int16_t tailPos = scannerPos - (scannerDir * i);
        if (tailPos >= 0 && tailPos < (int16_t)len) {
            uint8_t fade = 255 - (i * 255 / tailLength);
            CRGB tailColor = params.primaryColor;
            tailColor.nscale8(fade);
            view[tailPos] = tailColor;
        }
    }
    
    // Move scanner
    scannerPos += scannerDir;
    
    // Bounce at edges
    if (scannerPos >= (int16_t)len || scannerPos < 0) {
        scannerDir = -scannerDir;
        scannerPos = constrain(scannerPos, 0, len - 1);
    }
}

REGISTER_EFFECT_MOVING(effectScanner, "scanner", "Scanner");

} // namespace lume
