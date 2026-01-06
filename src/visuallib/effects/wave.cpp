/**
 * Wave effect - Rising/falling wave of color
 */

#include "../../core/effect_registry.h"

namespace lume {

namespace wave {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
    constexpr uint8_t INTENSITY = 2;
    constexpr uint8_t DIRECTION = 3;
}

DEFINE_EFFECT_SCHEMA(waveSchema,
    ParamDesc::Color("color", "Wave Color", CRGB::Blue),
    ParamDesc::Int("speed", "Wave Speed", 128, 1, 255),
    ParamDesc::Int("intensity", "Wave Width", 160, 1, 255),
    ParamDesc::Enum("direction", "Direction", "Up|Down|Center", 0)
);

void effectWave(SegmentView& view, const EffectParams& params, const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    (void)params;
    
    CRGB color = paramValues.getColor(wave::COLOR);
    uint8_t speed = paramValues.getInt(wave::SPEED);
    uint8_t intensity = paramValues.getInt(wave::INTENSITY);
    uint8_t direction = paramValues.getEnum(wave::DIRECTION);
    
    uint16_t len = view.size();
    if (len == 0) return;
    
    uint8_t waveWidth = intensity > 0 ? intensity / 4 : 40;
    
    // Fade existing
    view.fade(20);
    
    // Calculate wave position
    uint16_t cycle = len + waveWidth;
    uint16_t pos = (frame * speed / 16) % cycle;
    
    if (direction == 2) {
        // Center: wave expands outward from middle
        uint16_t center = len / 2;
        uint16_t halfCycle = len / 2 + waveWidth;
        uint16_t halfPos = (frame * speed / 16) % halfCycle;
        
        for (uint8_t i = 0; i < waveWidth; i++) {
            uint8_t brightness = sin8((i * 255) / waveWidth);
            CRGB colorScaled = color;
            colorScaled.nscale8(brightness);
            
            // Outward from center - both directions
            int16_t posUp = center + halfPos - waveWidth + i;
            int16_t posDown = center - halfPos + waveWidth - i - 1;
            
            if (posUp >= 0 && posUp < (int16_t)len) {
                view[posUp] = colorScaled;
            }
            if (posDown >= 0 && posDown < (int16_t)len) {
                view[posDown] = colorScaled;
            }
        }
    } else {
        // Up or down
        for (uint8_t i = 0; i < waveWidth; i++) {
            uint8_t brightness = sin8((i * 255) / waveWidth);
            CRGB colorScaled = color;
            colorScaled.nscale8(brightness);
            
            int16_t pixelPos;
            if (direction == 0) {
                // Up
                pixelPos = pos - waveWidth + i;
            } else {
                // Down
                pixelPos = len - pos + waveWidth - i - 1;
            }
            
            if (pixelPos >= 0 && pixelPos < (int16_t)len) {
                view[pixelPos] = colorScaled;
            }
        }
    }
}

REGISTER_EFFECT_SCHEMA(effectWave, "wave", "Wave", Moving, waveSchema, 0);

} // namespace lume
