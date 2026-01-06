
# Copilot Prompt for New Effects (in VS Code)

Want to add a new effect or port one from WLED? Paste this prompt into GitHub Copilot (in VS Code), or any AI assistant (ChatGPT, Claude, etc), to generate a LUME-compatible effect:

---
**Prompt:**

Write a new LED effect for the LUME firmware. Use the following template and conventions:

- The effect function signature must be:
    void effectNAME(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame)
- Define a schema with DEFINE_EFFECT_SCHEMA to describe parameters
- Use view.getScratchpad<T>() for stateful effects (no static/global variables)
- Use only FastLED-compatible code and LUME's SegmentView API
- Register with REGISTER_EFFECT_SCHEMA macro
- Example effect name: "fireup", "rainbowtwinkle", etc.
- If porting from WLED, convert millis() to frame, and replace global state with scratchpad

Example template:
```cpp
namespace myeffect {
    constexpr uint8_t SPEED = 0;
    constexpr uint8_t COLOR = 1;
}

DEFINE_EFFECT_SCHEMA(myeffectSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Color("color", "Color", CRGB::Blue)
);

void effectMyEffect(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame) {
    uint8_t speed = params.getInt(myeffect::SPEED);
    CRGB color = params.getColor(myeffect::COLOR);
    // Your effect code here
}

REGISTER_EFFECT_SCHEMA(effectMyEffect, "myeffect", "My Effect", Animated, myeffectSchema, 0);
```

---

See the rest of this file for parameter types and complete examples.

# Adding New Effects

Quick guide to creating custom LED effects for LUME.

## File Structure

Create a new file in `src/visuallib/effects/youreffect.cpp`:

```cpp
#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"

namespace lume {

// Define parameter slot indices
namespace youreffect {
    constexpr uint8_t SPEED = 0;
    constexpr uint8_t COLOR = 1;
}

// Define parameter schema
DEFINE_EFFECT_SCHEMA(youreffectSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Color("color", "Color", CRGB::Blue)
);

void effectYourEffect(SegmentView& view, const ParamValues& params, 
                      uint32_t frame, bool firstFrame) {
    // Access parameters by slot
    uint8_t speed = params.getInt(youreffect::SPEED);
    CRGB color = params.getColor(youreffect::COLOR);
    
    // Your effect code here
    for (uint16_t i = 0; i < view.size(); i++) {
        view[i] = color;
    }
}

// Register: function, id, name, category, schema, stateSize
REGISTER_EFFECT_SCHEMA(effectYourEffect, "youreffect", "Your Effect", Animated, youreffectSchema, 0);

} // namespace lume
```

## Parameter Types

All effects now use `ParamSchema` to define parameters. Available types:

### Int (uint8_t slider, 0-255)
```cpp
ParamDesc::Int("speed", "Speed", 128, 1, 255)
//             id       name     default min max
```

### Float (float slider, 0.0-1.0)
```cpp
ParamDesc::Float("density", "Density", 0.5f, 0.0f, 1.0f)
```

### Color (RGB picker)
```cpp
ParamDesc::Color("color", "Color", CRGB::Red)
//               id       name     default
```

### Bool (toggle switch)
```cpp
ParamDesc::Bool("reversed", "Reversed", false)
```

### Enum (dropdown)
```cpp
ParamDesc::Enum("direction", "Direction", "Up|Down|Left|Right", 0)
//              id           name          options              default_index
```

### Palette (palette selector)
```cpp
ParamDesc::PaletteSelect("palette", "Palette")
```

## Effect Categories

Choose the category that best describes your effect:

- `Solid` - Static, non-animated effects
- `Animated` - Effects with motion/animation
- `Moving` - Effects with positional movement
- `Special` - Complex or unique effects

## Accessing Parameters

```cpp
void effectExample(SegmentView& view, const ParamValues& params, 
                   uint32_t frame, bool firstFrame) {
    
    // Access by slot index (defined in namespace above)
    uint8_t speed = params.getInt(example::SPEED);
    float density = params.getFloat(example::DENSITY);
    CRGB color = params.getColor(example::COLOR);
    bool reversed = params.getBool(example::REVERSED);
    uint8_t direction = params.getEnum(example::DIRECTION);
    const CRGBPalette16& palette = params.getPalette();
    
    // Timing
    uint32_t currentFrame = frame;     // Use for beatsin8(), etc.
    bool isFirstFrame = firstFrame;    // True on effect change
    
    // LED access
    uint16_t ledCount = view.size();
    view[0] = CRGB::Red;               // Set individual LED
    view.fill(CRGB::Blue);             // Fill all LEDs
    view.gradient(color, CRGB::Red);   // Create gradient
}
```

## Stateful Effects (Using Scratchpad)

For effects that need to remember state between frames:

```cpp
// Define state structure
struct FireState {
    uint8_t heat[600];
};

void effectFire(SegmentView& view, const ParamValues& params, 
                uint32_t frame, bool firstFrame) {
    // Access scratchpad
    FireState* state = view.getScratchpad<FireState>();
    if (!state) return;
    
    // Initialize on first frame
    if (firstFrame) {
        memset(state->heat, 0, sizeof(state->heat));
    }
    
    // Use state
    state->heat[0] = 255;
    // ... effect logic
}

// Register with state size
REGISTER_EFFECT_SCHEMA(effectFire, "fire", "Fire", Animated, 
                       fireSchema, sizeof(FireState));
```

## Complete Examples

### Simple Color Effect
```cpp
namespace solid {
    constexpr uint8_t COLOR = 0;
}

DEFINE_EFFECT_SCHEMA(solidSchema,
    ParamDesc::Color("color", "Color", CRGB::Red)
);

void effectSolid(SegmentView& view, const ParamValues& params, 
                 uint32_t frame, bool firstFrame) {
    (void)frame;
    (void)firstFrame;
    
    CRGB color = params.getColor(solid::COLOR);
    view.fill(color);
}

REGISTER_EFFECT_SCHEMA(effectSolid, "solid", "Solid Color", Solid, solidSchema, 0);
```

### Palette-Based Animation
```cpp
namespace colorwaves {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(colorwavesSchema,
    ParamDesc::PaletteSelect("palette", "Palette"),
    ParamDesc::Int("speed", "Speed", 128, 1, 255)
);

void effectColorWaves(SegmentView& view, const ParamValues& params, 
                      uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    
    const CRGBPalette16& palette = params.getPalette();
    uint8_t speed = params.getInt(colorwaves::SPEED);
    
    uint16_t offset = (frame * speed) >> 6;
    
    for (uint16_t i = 0; i < view.size(); i++) {
        uint8_t colorIndex = (i * 256 / view.size()) + offset;
        view[i] = ColorFromPalette(palette, colorIndex, 255, LINEARBLEND);
    }
}

REGISTER_EFFECT_SCHEMA(effectColorWaves, "colorwaves", "Color Waves", 
                       Animated, colorwavesSchema, 0);
```

### Stateful Effect with Multiple Parameters
```cpp
namespace candle {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
    constexpr uint8_t INTENSITY = 2;
}

DEFINE_EFFECT_SCHEMA(candleSchema,
    ParamDesc::Color("color", "Color", CRGB(255, 140, 40)),
    ParamDesc::Int("speed", "Flicker Speed", 128, 1, 255),
    ParamDesc::Int("intensity", "Flicker Intensity", 128, 1, 255)
);

struct CandleState {
    uint8_t base;
    uint8_t target;
    uint32_t lastFlickerChange;
};

void effectCandle(SegmentView& view, const ParamValues& params, 
                  uint32_t frame, bool firstFrame) {
    (void)frame;
    
    CandleState* state = view.getScratchpad<CandleState>();
    if (!state) return;
    
    CRGB baseColor = params.getColor(candle::COLOR);
    uint8_t speed = params.getInt(candle::SPEED);
    uint8_t intensity = params.getInt(candle::INTENSITY);
    
    if (firstFrame) {
        state->base = 200;
        state->target = 200;
        state->lastFlickerChange = 0;
    }
    
    // Flicker logic...
    uint32_t now = millis();
    uint32_t flickerDelay = map(speed, 1, 255, 150, 10);
    
    if (now - state->lastFlickerChange > flickerDelay) {
        state->lastFlickerChange = now;
        state->target = random8(180, 255);
    }
    
    if (state->base < state->target) state->base += 3;
    if (state->base > state->target) state->base -= 5;
    
    for (uint16_t i = 0; i < view.size(); i++) {
        CRGB color = baseColor;
        color.nscale8(state->base);
        view[i] = color;
    }
}

REGISTER_EFFECT_SCHEMA(effectCandle, "candle", "Candle", Animated, 
                       candleSchema, sizeof(CandleState));
```

## UI Integration

The web UI automatically generates controls based on your schema:

- **Int/Float parameters** → Sliders
- **Color parameters** → Color pickers
- **Bool parameters** → Toggle switches
- **Enum parameters** → Dropdown menus
- **Palette parameter** → Palette selector

**No frontend changes needed!** The schema drives the UI.

## Best Practices

✅ **DO:**
- Use `frame` for timing (not `millis()` for animations)
- Keep effects deterministic (same inputs = same output)
- Use `firstFrame` to initialize scratchpad state
- Use slot constants (e.g., `effect::SPEED`) for readability
- Test with different LED counts
- Use `view.getScratchpad<T>()` for stateful effects

❌ **DON'T:**
- Use global variables or `static` state (breaks multi-segment)
- Call `delay()` or blocking functions
- Assume a specific LED count
- Exceed 512 bytes in state structure
- Mix up parameter slot indices

## Debugging

Enable logging in your effect:

```cpp
#include "../../logging.h"

void effectDebug(SegmentView& view, const ParamValues& params, 
                 uint32_t frame, bool firstFrame) {
    if (firstFrame) {
        LOG_INFO(LogTag::LED, "Effect started, %d LEDs", view.size());
    }
    // Effect code...
}
```

Monitor output: `pio device monitor`

## Migration from Legacy Effects

If updating an old effect that used `EffectParams`:

**Before:**
```cpp
void effectOld(SegmentView& view, const EffectParams& params, 
               const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    CRGB color = paramValues.getColor(0);
    view.fill(color);
}
```

**After:**
```cpp
void effectNew(SegmentView& view, const ParamValues& params,
               uint32_t frame, bool firstFrame) {
    CRGB color = params.getColor(0);
    view.fill(color);
}
```

Changes:
1. Remove `const EffectParams& params` parameter
2. Rename `paramValues` to `params`
3. Palette accessed via `params.getPalette()` instead of `params.palette`
4. Update static state to use `view.getScratchpad<T>()`
