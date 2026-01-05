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
LED range + effect binding + 512-byte scratchpad for effect state.

```cpp
Segment* seg = controller.getSegment(0);
seg->setEffect("fire");
seg->setSpeed(150);
seg->setPalette(HeatColors_p);
```

### EffectRegistry ([effect_registry.h](effect_registry.h))
Self-registering effect system. Effects register themselves at compile time.

```cpp
// In your effect file
REGISTER_EFFECT_PALETTE(effectFire, "fire", "Fire");
```

### SegmentView ([segment_view.h](segment_view.h))
Safe, bounded view into LED array for effects to write to.

```cpp
void effectSolid(SegmentView& view, const EffectParams& params, 
                 uint32_t frame, bool firstFrame) {
    for (uint16_t i = 0; i < view.size(); i++) {
        view[i] = params.color1;
    }
}
```

## Architecture

**Single-Writer Model**: Only `controller.update()` writes to LEDs.
- Effects write to their segment's view
- Protocols write to atomic buffers
- Controller copies when ready
