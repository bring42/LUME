# Effects

Self-registering LED effects using FastLED. Each effect is a **pure function** with a
**schema** that declares its parameters; the schema drives both the REST API
(`GET /api/v2/effects`) and the web UI controls. There is one registration path — schema-based.
For the full authoring guide see [../../docs/ADDING_EFFECTS.md](../../docs/ADDING_EFFECTS.md).

## Adding an effect

```cpp
#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"

namespace lume {

// 1. Parameter slot indices (order matches the schema below)
namespace myeffect {
    constexpr uint8_t SPEED = 0;
    constexpr uint8_t COLOR = 1;
}

// 2. Schema (lives in flash)
DEFINE_EFFECT_SCHEMA(myEffectSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Color("color", "Color", CRGB::Blue)
);

// 3. Effect function — current signature (no legacy EffectParams argument)
void effectMyEffect(SegmentView& view, const ParamValues& params,
                    uint32_t frame, bool firstFrame) {
    uint8_t speed = params.getInt(myeffect::SPEED);
    CRGB color   = params.getColor(myeffect::COLOR);

    // Touch pixels only through view[i] and the remap-safe primitives.
    for (uint16_t i = 0; i < view.size(); i++) view[i] = color;
}

// 4. Register: fn, id, name, category, schema, stateSize
REGISTER_EFFECT_SCHEMA(effectMyEffect, "myeffect", "My Effect", Animated, myEffectSchema, 0);

} // namespace lume
```

The UI auto-renders controls from the schema (sliders, color pickers, toggles, dropdowns,
palette pickers) — no frontend changes needed.

### Dimensionality (P1.2)

`REGISTER_EFFECT_SCHEMA` registers a **1D (strip)** effect (the default). If an effect's logic
is dimension-agnostic or matrix-native, declare it so a 2D build can offer or refuse it:

```cpp
REGISTER_EFFECT_SCHEMA_DIMS(effectMyEffect, "myeffect", "My Effect", Animated, myEffectSchema, 0, Any);
// ...or TwoD for a matrix-only effect.
```

## Parameter types

```cpp
ParamDesc::Int("id", "Name", default, min, max)           // 0-255 slider
ParamDesc::Float("id", "Name", default, min, max)         // float slider (0.0-1.0)
ParamDesc::Color("id", "Name", CRGB::Red)                 // color picker (#rrggbb over the API)
ParamDesc::Bool("id", "Name", false)                      // toggle
ParamDesc::Enum("id", "Name", "opt1|opt2|opt3", default)  // dropdown
ParamDesc::PaletteSelect("id", "Name")                    // palette picker
```

Read them in the effect via `params.getInt/getFloat/getColor/getBool/getEnum(slot)` and
`params.getPalette()`.

## Effect state (scratchpad)

Stateful effects keep frame-to-frame state in the per-segment scratchpad — **640 bytes**
(`SCRATCHPAD_SIZE`), never in `static`/global variables (which would break multi-segment use):

```cpp
struct MyState { uint8_t position; uint16_t counter; };

void effectStateful(SegmentView& view, const ParamValues& params,
                    uint32_t frame, bool firstFrame) {
    MyState* s = view.getScratchpad<MyState>();   // compile-time size/alignment checked
    if (firstFrame) { /* initialise s */ }
    // ...use s...
}

REGISTER_EFFECT_SCHEMA(effectStateful, "stateful", "Stateful", Animated, statefulSchema, sizeof(MyState));
```

State larger than the 640 B pad (e.g. a 2D grid) uses the borrowed **workbuffer** via
`view.getScratchpadChecked<T>()` — see [ADDING_EFFECTS.md](../../docs/ADDING_EFFECTS.md) and
[rfcs/0002](../../docs/rfcs/0002-scratchpad-strategy.md).

## Best practices

- Use `frame` (not `millis()`) for animation timing; keep output deterministic
- Use `beatsin8()`, `sin8()`, `ColorFromPalette()` for smooth, cheap animation
- **Own your canvas:** each frame either fully fill the view or `view.fade()` and draw on top —
  the loop doesn't pre-clear covered pixels
- Never use `static`/global state (breaks multiple segments); use the scratchpad
- Never reach for a raw LED pointer — `SegmentView::raw()` was removed (P1.4)
