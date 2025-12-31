
# Copilot Prompt for New Effects (in VS Code)

Want to add a new effect or port one from WLED? Paste this prompt into GitHub Copilot (in VS Code), or any AI assistant (ChatGPT, Claude, etc), to generate a LUME-compatible effect:

---
**Prompt:**

Write a new LED effect for the LUME firmware. Use the following template and conventions:

- The effect function signature must be:
    void effectNAME(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame)
- Do not use static/global variables; use the segment scratchpad for state.
- Use only FastLED-compatible code and LUME's SegmentView API.
- Register the effect with the correct macro (see table below).
- Example effect name: "fireup", "rainbowtwinkle", etc.
- If porting from WLED, convert millis() to frame, and replace global state with scratchpad.

Example template:
```cpp
void effectMyEffect(SegmentView& view, const EffectParams& params, uint32_t frame, bool firstFrame) {
        // Your effect code here
}
REGISTER_EFFECT_PALETTE(effectMyEffect, "myeffect", "My Effect");
```

---

See the rest of this file for effect registration macros and parameter details.
# Adding New Effects

Quick guide to creating custom LED effects for LUME.

## File Structure

Create a new file in `src/effects/youreffect.cpp`:

```cpp
#include "../core/effect_registry.h"

namespace lume {

void effectYourEffect(SegmentView& view, const EffectParams& params, 
                      uint32_t frame, bool firstFrame) {
    // Your effect code here
    for (uint16_t i = 0; i < view.size(); i++) {
        view[i] = CRGB::Blue;  // Example: set all LEDs to blue
    }
}

REGISTER_EFFECT_PALETTE(effectYourEffect, "youreffect", "Your Effect");

} // namespace lume
```

## Registration Macros

Choose the macro that matches your effect's needs:

| Macro | Primary Color | Secondary Color | Palette | Speed | Intensity | Use Case |
|-------|---------------|-----------------|---------|-------|-----------|----------|
| `REGISTER_EFFECT_SOLID` | ✅ | ❌ | ❌ | ❌ | ❌ | Static color effects (no animation) |
| `REGISTER_EFFECT_SIMPLE_NAMED` | ❌ | ❌ | ❌ | ✅ | ❌ | Basic animated effects (speed only) |
| `REGISTER_EFFECT_PALETTE` | ❌ | ❌ | ✅ | ✅ | ❌ | Palette-based animations |
| `REGISTER_EFFECT_COLORS` | ✅ | ✅ | ❌ | ✅ | ❌ | Two-color effects (primary + secondary) |
| `REGISTER_EFFECT_ANIMATED` | ❌ | ❌ | ❌ | ✅ | ✅ | Animated with speed + intensity |
| `REGISTER_EFFECT_MOVING` | ❌ | ❌ | ❌ | ✅ | ✅ | Moving/traveling effects |

**Important:** Match your registration to what parameters your effect actually uses! If your effect code uses `params.primaryColor`, you must set `usesPrimaryColor=true` in the registration (use `REGISTER_EFFECT_FULL` or an appropriate convenience macro).

### Advanced Registration

For full control, use `REGISTER_EFFECT_FULL`:

```cpp
REGISTER_EFFECT_FULL(
    effectCustom,           // Function name
    "custom",               // ID (lowercase, no spaces)
    "Custom Effect",        // Display name
    Animated,               // Category: Solid, Animated, Moving, Special
    true,                   // usesPalette
    true,                   // usesPrimaryColor
    true,                   // usesSecondaryColor
    true,                   // usesSpeed
    true,                   // usesIntensity
    0,                      // State size (0 if stateless)
    1                       // Minimum LEDs
);
```

## Available Parameters

```cpp
void effectYourEffect(SegmentView& view, const EffectParams& params, 
                      uint32_t frame, bool firstFrame) {
    
    // Colors
    CRGB primary = params.primaryColor;      // Primary color
    CRGB secondary = params.secondaryColor;  // Secondary color
    
    // Animation controls
    uint8_t speed = params.speed;            // 1-200 (default: 100)
    uint8_t intensity = params.intensity;    // 0-255 (default: 128)
    
    // Palette (if registered with palette support)
    CRGBPalette16 palette = params.palette;
    
    // Timing
    uint32_t currentFrame = frame;           // Use for beatsin8(), etc.
    bool isFirstFrame = firstFrame;          // True on effect change
    
    // LED access
    uint16_t ledCount = view.size();
    view[0] = CRGB::Red;                     // Set individual LED
    view.fill(CRGB::Blue);                   // Fill all LEDs
    view.gradient(primary, secondary);        // Create gradient
}
```

## Examples

### Static Two-Color Gradient

```cpp
void effectGradient(SegmentView& view, const EffectParams& params, 
                    uint32_t frame, bool firstFrame) {
    view.gradient(params.primaryColor, params.secondaryColor);
}

REGISTER_EFFECT_COLORS(effectGradient, "gradient", "Gradient");
```

### Animated Rainbow

```cpp
void effectRainbow(SegmentView& view, const EffectParams& params, 
                   uint32_t frame, bool firstFrame) {
    uint8_t hue = (frame * params.speed) / 100;
    for (uint16_t i = 0; i < view.size(); i++) {
        view[i] = CHSV(hue + (i * 255 / view.size()), 255, 255);
    }
}

REGISTER_EFFECT_SIMPLE_NAMED(effectRainbow, "rainbow", "Rainbow");
```

### Palette-Based with Speed

```cpp
void effectFire(SegmentView& view, const EffectParams& params, 
                uint32_t frame, bool firstFrame) {
    uint8_t cooling = params.intensity > 0 ? params.intensity : 55;
    
    for (uint16_t i = 0; i < view.size(); i++) {
        uint8_t colorIndex = (frame + i * 10) % 255;
        view[i] = ColorFromPalette(params.palette, colorIndex);
    }
}

REGISTER_EFFECT_PALETTE(effectFire, "fire", "Fire");
```

## Adding to Build

1. Create your effect file in `src/effects/`
2. Add to `src/effects/effects.h`:
   ```cpp
   #include "youreffect.cpp"
   ```
3. Compile: `pio run -t upload`

## UI Integration

The web UI automatically adapts based on your registration macro:

- Palette selector appears if `usesPalette = true`
- Speed slider hidden if `usesSpeed = false`
- Intensity slider appears if `usesIntensity = true`
- Secondary color picker shows if `usesSecondaryColor = true`

**No frontend changes needed!** The metadata drives the UI.

## Best Practices

✅ **DO:**
- Use `frame` for timing (not `millis()`)
- Keep effects deterministic (same inputs = same output)
- Use `firstFrame` to initialize state
- Test with different LED counts

❌ **DON'T:**
- Use global variables or `static` state
- Call `delay()` or blocking functions
- Assume a specific LED count
- Use `millis()` for animation timing

## Debugging

Enable logging in your effect:

```cpp
#include "../logging.h"

void effectDebug(SegmentView& view, const EffectParams& params, 
                 uint32_t frame, bool firstFrame) {
    if (firstFrame) {
        LOG_INFO(LogTag::LED, "Effect started, %d LEDs", view.size());
    }
    // Effect code...
}
```

Monitor output: `pio device monitor`
