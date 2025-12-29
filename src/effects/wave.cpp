/**
 * Wave effect - Rising/falling wave of color
 * 
 * Uses custom1 for direction: 0=up, 1=down, 2=center
 */

#include "../core/effect_registry.h"

namespace lume {

void effectWave(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    uint8_t waveWidth = params.intensity > 0 ? params.intensity / 4 : 40;
    uint8_t direction = params.custom1;  // 0=up, 1=down, 2=center
    
    // Fade existing
    view.fade(20);
    
    // Calculate wave position
    uint16_t cycle = len + waveWidth;
    uint16_t pos = (frame * params.speed / 16) % cycle;
    
    if (direction == 2) {
        // Center: wave expands outward from middle
        uint16_t center = len / 2;
        uint16_t halfCycle = len / 2 + waveWidth;
        uint16_t halfPos = (frame * params.speed / 16) % halfCycle;
        
        for (uint8_t i = 0; i < waveWidth; i++) {
            uint8_t brightness = sin8((i * 255) / waveWidth);
            CRGB color = params.primaryColor;
            color.nscale8(brightness);
            
            // Outward from center - both directions
            int16_t posUp = center + halfPos - waveWidth + i;
            int16_t posDown = center - halfPos + waveWidth - i - 1;
            
            if (posUp >= 0 && posUp < (int16_t)len) {
                view[posUp] = color;
            }
            if (posDown >= 0 && posDown < (int16_t)len) {
                view[posDown] = color;
            }
        }
    } else {
        // Up or down
        for (uint8_t i = 0; i < waveWidth; i++) {
            uint8_t brightness = sin8((i * 255) / waveWidth);
            CRGB color = params.primaryColor;
            color.nscale8(brightness);
            
            int16_t pixelPos;
            if (direction == 0) {
                // Up
                pixelPos = pos - waveWidth + i;
            } else {
                // Down
                pixelPos = len - pos + waveWidth - i - 1;
            }
            
            if (pixelPos >= 0 && pixelPos < (int16_t)len) {
                view[pixelPos] = color;
            }
        }
    }
}

REGISTER_EFFECT_MOVING(effectWave, "wave", "Wave");

} // namespace lume
