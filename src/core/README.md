# Core System

The heart of LUME - orchestration, segments, and effects.

## Key Components

### LumeController ([controller.h](controller.h))
The main orchestrator. Owns the LED buffer and coordinates everything.

```cpp
// Global singleton
extern LumeController controller;

// Typical usage
controller.begin(150);  // Initialize with 150 LEDs
Segment* seg = controller.createSegment(0, 50);
seg->setEffect("rainbow");
controller.update();  // Call in loop() at ~60 FPS
```

### Segment ([segment.h](segment.h))
A [`Region`](region.h) slice of the LED array + effect binding + a 640-byte scratchpad for
effect state (P1.3/P1.5). Mutations happen on the render loop via the command bus, not by
calling setters from other tasks.

### Region ([region.h](region.h))
`Region{start, length}` geometry value type (`stop()`/`contains()`/`size()`/`empty()`) — a
range today, room for a rect tomorrow (P1.3). `SegmentView` holds one.

### EffectRegistry ([effect_registry.h](effect_registry.h))
Self-registering effect system. Effects register at startup with a **schema** and optional
**`EffectDims`** metadata:

```cpp
// In your effect file (see visuallib/README.md)
REGISTER_EFFECT_SCHEMA(effectFire, "fire", "Fire", Animated, fireSchema, sizeof(FireState));
// ...or REGISTER_EFFECT_SCHEMA_DIMS(..., Any) to mark it remap-safe (P1.2).
```

### SegmentView ([segment_view.h](segment_view.h))
Safe, bounded, remap-aware view into the LED array. Effects touch pixels only through
`view[i]` and the primitives (`fill`/`fade`/`clear`/`gradient`/`rainbow`) — all remap-safe;
there is no raw-pointer escape hatch (`raw()` removed, P1.4).

```cpp
void effectSolid(SegmentView& view, const ParamValues& params,
                 uint32_t frame, bool firstFrame) {
    view.fill(params.getColor(0));
}
```

### Params ([param_schema.h](param_schema.h), [param_codec.h](param_codec.h))
`ParamDesc`/`ParamSchema` describe an effect's typed parameters; `ParamValues` holds the
runtime values effects read by slot. `param_codec.h` is the single schema-aware
(de)serializer shared by the API and persistence (P1.1).

### Output HAL ([led_output.h](led_output.h), [fastled_output.h](fastled_output.h))
The controller presents finished frames through the `ILedOutput` interface; `FastLedOutput`
(RMT) is the default backend (RFC 0001 §6).

## Architecture

**Single-Writer Model**: only `controller.update()` (the render loop) writes segment/LED state.
- Every input (HTTP, MQTT, AI, WebSocket) enqueues a command on the bus
- sACN writes an atomic double-buffer; the loop copies it when ready
- The loop shows the frame through the `ILedOutput` HAL
