# Effects

Self-registering LED effects using FastLED.

## Adding a New Effect (Schema-based)

**Recommended approach** - creates dynamic UI controls:

```cpp
#include "../core/effect_registry.h"
#include "../core/param_schema.h"

namespace lume {

// 1. Define parameter slots
namespace myeffect {
    constexpr uint8_t SPEED = 0;
    constexpr uint8_t SIZE = 1;
    constexpr uint8_t COLOR = 2;
}

// 2. Define schema (lives in flash)
DEFINE_EFFECT_SCHEMA(myEffectSchema,
    ParamDesc::Int("speed", "Speed", 128, 0, 255),
    ParamDesc::Int("size", "Size", 50, 10, 100),
    ParamDesc::Color("color", "Color", CRGB::Blue)
);

// 3. Effect function reads from ParamValues
void effectMyEffect(SegmentView& view, const EffectParams& params,
                    const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)params; // Legacy params unused for schema effects
    
    // Read typed params via slots
    uint8_t speed = paramValues.getInt(myeffect::SPEED);
    uint8_t size = paramValues.getInt(myeffect::SIZE);
    CRGB color = paramValues.getColor(myeffect::COLOR);
    
    // Effect implementation using params...
    for (uint16_t i = 0; i < view.size(); i++) {
        view[i] = color;
    }
}

// 4. Register with schema
REGISTER_EFFECT_SCHEMA(effectMyEffect, "myeffect", "My Effect", 
                       Animated, myEffectSchema, 0);

} // namespace lume
```

UI will automatically render appropriate controls (sliders, color pickers, etc.)!

## Adding a Legacy Effect

**Simple approach** - uses standard speed/intensity/color controls:

```cpp
#include "../core/effect_registry.h"

namespace lume {

void effectMyEffect(SegmentView& view, const EffectParams& params, 
                    const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    (void)paramValues; // Unused for legacy effects
    
    // Use legacy params
    uint8_t speed = params.speed;
    CRGB color = params.primaryColor;
    
    for (uint16_t i = 0; i < view.size(); i++) {
        uint8_t hue = (i * 10) + (frame * speed / 64);
        view[i] = CHSV(hue, 255, 255);
    }
}

// Choose registration macro based on parameters needed:
REGISTER_EFFECT_ANIMATED(effectMyEffect, "myeffect", "My Effect");

} // namespace lume
```

## Registration Macros

### Schema-based (Recommended)
```cpp
REGISTER_EFFECT_SCHEMA(fn, id, name, category, schema, stateSize)
// - Automatically generates UI from schema
// - Supports custom parameter types
```

### Legacy Macros (Standard Controls)
| Macro | Parameters | UI Controls |
|-------|-----------|-------------|
| `REGISTER_EFFECT_PALETTE` | palette, speed | Palette picker, speed slider |
| `REGISTER_EFFECT_COLORS` | color1, color2, speed | Two color pickers, speed |
| `REGISTER_EFFECT_ANIMATED` | speed, intensity | Speed & intensity sliders |
| `REGISTER_EFFECT_SIMPLE` | speed | Speed slider only |

## Parameter Types (Schema-based)

```cpp
ParamDesc::Int("id", "Name", default, min, max)           // Slider (0-255)
ParamDesc::Float("id", "Name", default, min, max)         // Number input (0.0-1.0)
ParamDesc::Color("id", "Name", CRGB::Red)                 // Color picker
ParamDesc::Bool("id", "Name", false)                      // Toggle switch
ParamDesc::Enum("id", "Name", "opt1|opt2|opt3", default)  // Dropdown
ParamDesc::PaletteSelect("id", "Name")                    // Palette picker
```

## Effect State

For stateful effects, use segment scratchpad (512 bytes per segment):

```cpp
struct MyState {
    uint8_t position;
    uint16_t counter;
};

void effectStateful(SegmentView& view, const EffectParams& params,
                    const ParamValues& paramValues, uint32_t frame, bool firstFrame) {
    // Get scratchpad from segment (not available in SegmentView)
    // Access via segment->getScratchpad<MyState>() in controller
    
    if (firstFrame) {
        // Initialize state when effect changes
    }
    
    // Use state...
}

// Register with state size
REGISTER_EFFECT_SCHEMA(effectStateful, "stateful", "Stateful", 
                       Animated, statefulSchema, sizeof(MyState));
```

## Best Practices

- Use `beatsin8()`, `sin8()`, etc. for smooth animation
- Respect `params.speed` for timing
- Use `ColorFromPalette()` with `params.palette`
- Never use static variables (breaks with multiple segments)
- Check `firstFrame` to initialize scratchpad state
