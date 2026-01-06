#ifndef LUME_EFFECT_PARAMS_H
#define LUME_EFFECT_PARAMS_H

#include <FastLED.h>

namespace lume {

/**
 * BlendMode - How overlapping segments combine
 */
enum class BlendMode : uint8_t {
    Replace = 0,   // Overwrite (default)
    Add,           // Additive blending
    Average,       // 50/50 blend
    Max,           // Take brighter value
    Overlay        // Complex blend (multiply darks, screen lights)
};

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

// Palette name lookup
inline const char* paletteName(PalettePreset preset) {
    switch (preset) {
        case PalettePreset::Rainbow: return "rainbow";
        case PalettePreset::Lava:    return "lava";
        case PalettePreset::Ocean:   return "ocean";
        case PalettePreset::Party:   return "party";
        case PalettePreset::Forest:  return "forest";
        case PalettePreset::Cloud:   return "cloud";
        case PalettePreset::Heat:    return "heat";
        case PalettePreset::Sunset:  return "sunset";
        case PalettePreset::Autumn:  return "autumn";
        case PalettePreset::Retro:   return "retro";
        case PalettePreset::Ice:     return "ice";
        case PalettePreset::Pink:    return "pink";
        case PalettePreset::Custom:  return "custom";
        default: return "rainbow";
    }
}

// Parse palette name
inline PalettePreset parsePalette(const char* name) {
    if (!name) return PalettePreset::Rainbow;
    
    if (strcmp(name, "rainbow") == 0) return PalettePreset::Rainbow;
    if (strcmp(name, "lava") == 0)    return PalettePreset::Lava;
    if (strcmp(name, "ocean") == 0)   return PalettePreset::Ocean;
    if (strcmp(name, "party") == 0)   return PalettePreset::Party;
    if (strcmp(name, "forest") == 0)  return PalettePreset::Forest;
    if (strcmp(name, "cloud") == 0)   return PalettePreset::Cloud;
    if (strcmp(name, "heat") == 0)    return PalettePreset::Heat;
    if (strcmp(name, "sunset") == 0)  return PalettePreset::Sunset;
    if (strcmp(name, "autumn") == 0)  return PalettePreset::Autumn;
    if (strcmp(name, "retro") == 0)   return PalettePreset::Retro;
    if (strcmp(name, "ice") == 0)     return PalettePreset::Ice;
    if (strcmp(name, "pink") == 0)    return PalettePreset::Pink;
    if (strcmp(name, "custom") == 0)  return PalettePreset::Custom;
    
    return PalettePreset::Rainbow;
}

} // namespace lume

#endif // LUME_EFFECT_PARAMS_H
