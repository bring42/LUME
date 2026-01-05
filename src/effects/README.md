# Effects

Self-registering LED effects using FastLED.

## Adding a New Effect

Create a file `myeffect.cpp`:

```cpp
#include "../core/effect_registry.h"

namespace lume {

void effectMyEffect(SegmentView& view, const EffectParams& params, 
                    uint32_t frame, bool firstFrame) {
    // firstFrame = true when scratchpad was reset
    // Use frame for timing: beatsin8(params.speed, 0, 255, 0, frame)
    
    for (uint16_t i = 0; i < view.size(); i++) {
        uint8_t hue = (i * 10) + (frame / 2);
        view[i] = CHSV(hue, 255, 255);
    }
}

// Choose registration macro based on parameters needed:
REGISTER_EFFECT_ANIMATED(effectMyEffect, "myeffect", "My Effect");

} // namespace lume
```

## Registration Macros

| Macro | Parameters | UI Controls |
|-------|-----------|-------------|
| `REGISTER_EFFECT_PALETTE` | palette, speed | Palette picker, speed slider |
| `REGISTER_EFFECT_COLORS` | color1, color2, speed | Two color pickers, speed |
| `REGISTER_EFFECT_ANIMATED` | speed, intensity | Speed & intensity sliders |
| `REGISTER_EFFECT_SIMPLE` | None | No controls |

## Effect State

Use the segment's scratchpad for persistent state:

```cpp
struct MyState {
    uint8_t position;
    uint16_t counter;
};

void effectStateful(SegmentView& view, const EffectParams& params,
                    uint32_t frame, bool firstFrame) {
    auto* state = view.getScratchpad<MyState>();
    
    if (firstFrame) {
        state->position = 0;
        state->counter = 0;
    }
    
    // Use state->position, state->counter...
}
```

## Best Practices

- Use `beatsin8()`, `sin8()`, etc. for smooth animation
- Respect `params.speed` for timing
- Use `ColorFromPalette()` with `params.palette`
- Never use static variables (breaks with multiple segments)
- Check `firstFrame` to initialize scratchpad state
