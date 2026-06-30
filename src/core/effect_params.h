#ifndef LUME_EFFECT_PARAMS_H
#define LUME_EFFECT_PARAMS_H

#include <FastLED.h>

namespace lume {

/**
 * Built-in palette definitions
 */
enum class PalettePreset : uint8_t {
    Rainbow = 0,
    Lava,
    Ocean,
    Party,
    Forest,
    Cloud,
    Heat,
    Sunset,
    Autumn,
    Retro,
    Ice,
    Pink,
    Custom,  // User-defined
    COUNT
};

// Get a CRGBPalette16 from a preset
inline CRGBPalette16 getPalette(PalettePreset preset) {
    switch (preset) {
        case PalettePreset::Rainbow: return RainbowColors_p;
        case PalettePreset::Lava:    return LavaColors_p;
        case PalettePreset::Ocean:   return OceanColors_p;
        case PalettePreset::Party:   return PartyColors_p;
        case PalettePreset::Forest:  return ForestColors_p;
        case PalettePreset::Cloud:   return CloudColors_p;
        case PalettePreset::Heat:    return HeatColors_p;
        
        // Custom palettes
        case PalettePreset::Sunset:
            return CRGBPalette16(
                CRGB(255, 100, 0), CRGB(255, 50, 0),
                CRGB(200, 0, 50), CRGB(100, 0, 100)
            );
        case PalettePreset::Autumn:
            return CRGBPalette16(
                CRGB(255, 100, 0), CRGB(200, 50, 0),
                CRGB(150, 20, 0), CRGB(100, 0, 0)
            );
        case PalettePreset::Retro:
            return CRGBPalette16(
                CRGB(255, 0, 100), CRGB(0, 255, 255),
                CRGB(255, 255, 0), CRGB(255, 0, 255)
            );
        case PalettePreset::Ice:
            return CRGBPalette16(
                CRGB(0, 0, 50), CRGB(0, 50, 100),
                CRGB(50, 100, 200), CRGB(200, 220, 255)
            );
        case PalettePreset::Pink:
            return CRGBPalette16(
                CRGB(255, 100, 150), CRGB(255, 50, 100),
                CRGB(200, 50, 150), CRGB(150, 0, 100)
            );
            
        default:
            return RainbowColors_p;
    }
}

} // namespace lume

#endif // LUME_EFFECT_PARAMS_H
