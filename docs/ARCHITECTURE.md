# LUME Architecture Report

**Date:** 2024-12-29  
**Status:** Planning / Pre-Migration  
**Version:** 2.0 (Proposed)

---

## Executive Summary

LUME is being repositioned from a "custom LED controller" to a **modern FastLED framework**—a clean, extensible foundation for LED projects that could eventually serve as a WLED alternative.

This document outlines a proposed architecture refactor focused on:
1. **SegmentView abstraction** — Virtual ranges over the LED array
2. **Effect function registry** — Lightweight, extensible effect system
3. **Full FastLED access** — Wrap without hiding, complement without replacing
4. **Protocol-agnostic design** — sACN, Art-Net, Matter (future) as plugins

---

## Goals & Non-Goals

### Goals
- Clean abstraction over FastLED that enables, not restricts
- Segment support (independent zones on a strip)
- Easy effect authoring (one function = one effect)
- Protocol extensibility (lighting protocols as plugins)
- Maintainable codebase (<3000 lines for core)
- ESP32-S3 as first-class target
- Matter-ready architecture (future integration)

### Non-Goals
- Porting WLED's code or patterns
- Supporting every ESP32 variant immediately
- Abstracting away FastLED's API (we expose it)
- Complex effect state machines (keep effects simple)

---

## Current State Analysis

### Codebase Metrics (v1.x)
| File | Lines | Role |
|------|-------|------|
| `main.cpp` | 1,287 | WiFi, web server, handlers |
| `led_controller.cpp` | 1,150 | Effects, state, FastLED calls |
| `web_ui.h` | 1,621 | Embedded HTML/CSS/JS |
| `sacn_receiver.cpp` | 511 | E1.31 protocol |
| `anthropic_client.cpp` | 362 | AI effect generation |
| `storage.cpp` | 286 | NVS persistence |
| **Total** | ~5,958 | |

### Current Limitations
1. **No segments** — Single contiguous strip assumption
2. **Monolithic effect switch** — Hard to extend, 20+ cases
3. **Tight coupling** — Effects embedded in `LedController`
4. **Limited FastLED exposure** — Only using subset of library

### What's Working Well
- Component separation (storage, protocols, AI)
- Async web server choice
- Centralized constants
- Structured logging
- sACN implementation

---

## Proposed Architecture (v2.0)

### Core Abstraction: SegmentView

The fundamental insight: effects should operate on a **view** of the LED array, not the array itself.

```cpp
struct SegmentView {
    CRGB* leds;           // Pointer to first LED in segment
    uint16_t length;      // Number of LEDs in segment
    bool reversed;        // Run effect backwards?
    
    // Indexed access (handles reversal)
    CRGB& operator[](uint16_t i);
    
    // Convenience methods (delegate to FastLED)
    void fill(CRGB color);
    void fade(uint8_t amount);
    void blur(uint8_t amount);
};
```

**Why this matters:**
- Effects don't know their position in the strip
- Same effect code works on any segment
- Reversal handled transparently
- Full FastLED access via `view.leds`

### Segment Class

Owns a view plus runtime state:

```cpp
class Segment {
public:
    SegmentView view;           // View into main LED array
    
    // Effect binding
    EffectFn effect;            // Current effect function
    EffectParams params;        // Effect parameters
    
    // Per-segment state
    uint8_t brightness = 255;
    uint8_t speed = 128;
    CRGB colors[2];             // Primary, secondary
    CRGBPalette16 palette;
    
    // Blending mode when segments overlap
    BlendMode blendMode = BlendMode::Replace;
    
    void update(uint32_t frame);
};
```

### Effect System

Effects are pure functions, registered by name:

```cpp
// Effect function signature
using EffectFn = void (*)(
    SegmentView& view,
    const EffectParams& params,
    uint32_t frame
);

// Registration (evaluated at startup)
#define REGISTER_EFFECT(name, fn) \
    static EffectRegistrar _reg_##fn(name, fn)

// Example effect implementation
void effectFire(SegmentView& view, const EffectParams& p, uint32_t frame) {
    // Full access to FastLED functions
    // Operates only on this segment's LEDs
}
REGISTER_EFFECT("fire", effectFire);
```

**Benefits:**
- No virtual function overhead
- Effects are testable in isolation
- Adding effects = adding files, no core changes
- Clear contract (inputs → LED colors)

### Controller

Owns the LED array and orchestrates updates:

```cpp
class LumeController {
public:
    void begin(uint16_t ledCount, uint8_t pin);
    void update();  // Called in loop()
    
    // Segment management
    Segment* addSegment(uint16_t start, uint16_t length);
    void removeSegment(uint8_t index);
    Segment* getSegment(uint8_t index);
    
    // Global settings (FastLED passthrough)
    void setBrightness(uint8_t brightness);
    void setColorCorrection(CRGB correction);
    void setMaxPower(uint8_t volts, uint16_t milliamps);
    
    // Direct LED access for protocols (sACN, etc.)
    CRGB* getLeds();
    uint16_t getLedCount();
    
private:
    CRGB leds[MAX_LED_COUNT];
    Segment segments[MAX_SEGMENTS];
    uint8_t segmentCount;
    uint32_t frameCounter;
};
```

---

## FastLED Feature Coverage

### Fully Exposed (Direct Access)

| Category | Features | Access Method |
|----------|----------|---------------|
| **Color Types** | `CRGB`, `CHSV` | Direct in effects |
| **Color Math** | `blend`, `nblend`, `lerp8by8` | Direct in effects |
| **Noise** | `inoise8`, `inoise16`, `fill_noise8` | Direct in effects |
| **Palettes** | `ColorFromPalette`, `CRGBPalette16` | Per-segment or global |
| **Pixel Ops** | `fadeToBlackBy`, `blur1d`, `fill_solid` | Via `view.leds` |
| **Timing** | `beatsin8`, `beat8`, `BPM` | Direct in effects |
| **Math** | `scale8`, `ease8`, `triwave8` | Direct in effects |
| **Power** | `setMaxPowerInVoltsAndMilliamps` | Controller-level |
| **Correction** | `setCorrection`, `setTemperature` | Controller-level |

### Requires View Extension

| Category | Features | Proposed Solution |
|----------|----------|-------------------|
| **2D/Matrix** | `XY()` mapping, 2D effects | `MatrixView` struct |
| **Parallel Output** | Multiple strips on different pins | Multi-controller |
| **Different Strip Types** | WS2812B, APA102, etc. | Template or config |

### MatrixView (Future)

Same pattern as SegmentView, for 2D:

```cpp
struct MatrixView {
    CRGB* leds;
    uint8_t width, height;
    bool serpentine;
    XYMap mapFn;  // Custom mapping function
    
    CRGB& operator()(uint8_t x, uint8_t y);
    
    void fill(CRGB color);
    void drawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, CRGB color);
    // ... 2D primitives
};
```

---

## Proposed File Structure

```
src/
├── lume.h                    # Main include (pulls in core)
│
├── core/
│   ├── segment_view.h        # SegmentView struct
│   ├── segment.h             # Segment class
│   ├── effect_registry.h     # Effect function registry
│   ├── effect_params.h       # Common effect parameters
│   ├── controller.h          # LumeController
│   └── controller.cpp
│
├── effects/
│   ├── effects.h             # All effect declarations
│   ├── solid.cpp
│   ├── rainbow.cpp
│   ├── fire.cpp
│   ├── confetti.cpp
│   ├── gradient.cpp
│   ├── noise.cpp
│   ├── pulse.cpp
│   ├── sparkle.cpp
│   ├── meteor.cpp
│   ├── twinkle.cpp
│   └── ...                   # One file per effect
│
├── protocols/
│   ├── protocol.h            # Protocol interface
│   ├── sacn.h / .cpp         # sACN/E1.31
│   ├── artnet.h / .cpp       # Art-Net (future)
│   └── ddp.h / .cpp          # DDP (future)
│
├── api/
│   ├── web_server.h / .cpp   # REST API
│   ├── json_state.h / .cpp   # JSON serialization
│   └── websocket.h / .cpp    # Real-time sync (future)
│
├── ai/
│   ├── ai_client.h / .cpp    # OpenRouter/Anthropic client
│   └── prompt_parser.h       # Effect spec parsing
│
├── storage/
│   ├── storage.h / .cpp      # NVS wrapper
│   └── presets.h / .cpp      # Preset management
│
├── platform/
│   ├── board.h               # Board detection/config
│   └── pins.h                # Pin definitions
│
├── ui/
│   └── web_ui.h              # Embedded HTML
│
└── main.cpp                  # Setup + loop (minimal)
```

**Estimated core size:** ~400 lines  
**Estimated effects:** ~50-80 lines each  
**Total estimate:** ~2500-3000 lines (down from 6000)

---

## Migration Plan

### Phase 1: Core Foundation ✅ COMPLETE
**Goal:** Create new core without breaking existing code

- [x] Create `core/segment_view.h`
- [x] Create `core/effect_params.h`
- [x] Create `core/effect_registry.h`
- [x] Create `core/segment.h`
- [x] Create `core/controller.h` / `.cpp`
- [x] Create `lume.h` (main include)
- [x] Create 10 effects (solid, rainbow, fire, confetti, gradient, pulse, sparkle, colorwaves, noise, meteor)
- [x] Verify compilation succeeds

**Status:** Compiles successfully. Core framework in place.

### Phase 2: Effect Migration & Testing ✅ COMPLETE (Effects Ported)
**Goal:** Test effects on hardware, port remaining effects

- [x] Port all effects to new system (23 total)
- [ ] Test new controller on hardware (parallel to old system)
- [ ] Verify visual parity with old system

**Effects ported (23):**
- Basic: solid, rainbow, gradient
- Animated: fire, fireup, confetti, colorwaves, noise
- Pulse/Breathing: pulse, breathe, candle
- Sparkle: sparkle, twinkle, strobe
- Moving: meteor, comet, scanner, sinelon, theater, wave, rain
- Special: pride, pacifica

**Success criteria:** All effects work via new system

### Phase 3: API Migration
**Goal:** Wire web API to new controller

- [ ] Update REST handlers to use new controller
- [ ] Add segment CRUD endpoints
- [ ] Update JSON state format
- [ ] Test with existing web UI

**Success criteria:** Web UI controls new system

### Phase 4: Protocol Migration
**Goal:** Adapt sACN to new architecture

- [ ] Define protocol interface
- [ ] Adapt sACN receiver
- [ ] Test with lighting software

**Success criteria:** sACN works with segments

### Phase 5: Cleanup
**Goal:** Remove old code, polish

- [ ] Delete old `led_controller.*`
- [ ] Refactor `main.cpp`
- [ ] Update documentation
- [ ] Update AI prompt schema for segments

**Success criteria:** Clean codebase, all features working

---

## API Design (Proposed)

### Segment Endpoints

```
GET  /api/segments              # List all segments
POST /api/segments              # Create segment
GET  /api/segments/{id}         # Get segment
PUT  /api/segments/{id}         # Update segment
DELETE /api/segments/{id}       # Delete segment
POST /api/segments/{id}/effect  # Set segment effect
```

### Segment JSON Schema

```json
{
  "id": 0,
  "start": 0,
  "length": 80,
  "effect": "fire",
  "brightness": 200,
  "speed": 150,
  "colors": ["#ff6600", "#ff0000"],
  "palette": "heat",
  "reverse": false,
  "blend": "replace"
}
```

### Full State

```json
{
  "power": true,
  "brightness": 255,
  "ledCount": 160,
  "segments": [
    {"id": 0, "start": 0, "length": 80, "effect": "fire", ...},
    {"id": 1, "start": 80, "length": 80, "effect": "rainbow", ...}
  ],
  "effects": ["solid", "rainbow", "fire", "confetti", ...],
  "palettes": ["rainbow", "heat", "ocean", ...]
}
```

---

## Technical Decisions

### Why Function Pointers Over Virtual Classes?

| Approach | Pros | Cons |
|----------|------|------|
| Virtual classes | OOP-familiar | vtable overhead, heap allocation |
| Function pointers | Zero overhead, cache-friendly | Less "C++-ish" |
| `std::function` | Flexible | Heap allocation, big on ESP32 |

**Decision:** Function pointers. This is embedded; keep it lean.

### Why Not Separate .h/.cpp for Effects?

Each effect is small (~50 lines). Splitting adds:
- Build complexity
- More files to navigate
- No meaningful compilation benefit

**Decision:** One `.cpp` per effect, declaration in shared header.

### How to Handle Effect State?

Some effects need frame-persistent state (e.g., fire cooling array).

Options:
1. Global/static state per effect (simple, not segment-aware)
2. State stored in Segment (segment-aware, more complex)
3. Pass frame counter, derive state (stateless where possible)

**Decision:** Prefer stateless via frame counter. For effects that truly need state (fire), store in Segment's `effectState` union.

---

## Open Questions

1. **Overlapping segments** — How should they blend? Add? Max? Replace?
2. **Transition effects** — Fade between effects when switching?
3. **Preset format** — Store segment configs, or full state snapshots?
4. **Matrix mapping** — Multiple mapping strategies? Configurable?
5. **AI prompts** — How to express multi-segment intent?

---

## Success Metrics

After migration, LUME should:

- [ ] Run 160 LEDs at 60+ FPS with 4 active segments
- [ ] Add new effect in <20 lines of code
- [ ] Support strip subdivision via API
- [ ] Maintain sACN compatibility
- [ ] Have <3000 lines in core (excluding UI)
- [ ] Compile in <30 seconds
- [ ] Be understandable by new contributors

---

## Next Steps

1. Review this document, adjust goals/approach
2. Create Phase 1 core files
3. Test basic segment rendering
4. Iterate

---

*This is a living document. Update as decisions are made.*
