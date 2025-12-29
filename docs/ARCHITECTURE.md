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

## Core Invariants (v2.0)

**These three decisions are locked. They remove 80% of migration risk.**

### Invariant 1: Segments are contiguous and non-overlapping

For v2 migration, segments:
- Map to a contiguous range of LEDs (`start` to `start + length - 1`)
- Cannot overlap with other segments
- Are validated on creation/modification

**Rationale:** Overlapping segments require a composition engine (which segment "wins"? how do they blend?). WLED's segment blending is a complexity sink. We enforce non-overlap now and can add proper blending layers later with a dedicated composition pass.

**Future:** Non-contiguous mapping (matrix, serpentine, grouping) can be added via `SegmentView::operator[]` without changing this invariant.

### Invariant 2: Single-writer model via command queue

All state mutations flow through a single point:
- Web handlers, protocols, and AI enqueue **commands**
- The main loop dequeues and executes commands
- Effects read state, never write shared state

```cpp
// Command types (simple discriminated union)
enum class CommandType { SetEffect, SetBrightness, SetColors, CreateSegment, ... };

struct Command {
    CommandType type;
    uint8_t segmentId;
    union { ... };  // Command-specific data
};

// Thread-safe queue (FreeRTOS xQueue or simple ring buffer)
static constexpr size_t COMMAND_QUEUE_SIZE = 16;
QueueHandle_t commandQueue;

// Producers (any context)
void enqueueCommand(const Command& cmd);

// Consumer (loop only)
void processCommands();  // Called at start of each frame
```

**Overflow policy:** Queue is fixed-size (16 commands). On overflow, **newest wins**—oldest command is dropped.

```cpp
// Correct "newest wins" implementation with FreeRTOS
bool enqueueCommand(const Command& cmd) {
    if (xQueueSend(commandQueue, &cmd, 0) != pdTRUE) {
        // Queue full: drop oldest, then send new
        Command discard;
        xQueueReceive(commandQueue, &discard, 0);  // Drop oldest
        xQueueSend(commandQueue, &cmd, 0);         // Add newest
    }
    return true;
}
```

> **Note:** Using `xQueueSendToBack` with `xQueueOverwrite` won't work here—that's for single-item queues. The receive-then-send pattern is the correct way to implement "newest wins" on a multi-item queue.

**Future consideration:** Coalescing spammy commands (brightness, speed) could reduce queue pressure, but not required for v2.

**Rationale:** ESP32 + AsyncWebServer means handlers run on different tasks. Without a single-writer model, we get heisenbugs. Command queue keeps effects deterministic and mutations predictable.

### Invariant 3: Fixed scratchpad for effect state

Effects that need persistent state (fire, meteors) use a fixed-size scratchpad per segment:

> **Memory budget:** 512 bytes × 8 segments = 4 KB worst-case. Trivial on ESP32-S3 with PSRAM.

```cpp
// In Segment
static constexpr size_t SCRATCHPAD_SIZE = 512;  // Tunable
uint8_t scratchpad[SCRATCHPAD_SIZE];
uint8_t scratchpadVersion;  // Incremented when effect changes

// Helper for effects
template<typename T>
T* getScratchpad(Segment& seg) {
    static_assert(sizeof(T) <= SCRATCHPAD_SIZE);
    return reinterpret_cast<T*>(seg.scratchpad);
}
```

**Rules:**
- Effects interpret the scratchpad bytes however they need
* Segment clears/zeros scratchpad when effect changes (version bump)
* No heap allocation for effect state
* `EffectInfo.stateSize` must be ≤ `SCRATCHPAD_SIZE` (validated at registration)

**firstFrame derivation (no desync):**
```cpp
// In Segment
uint8_t lastSeenVersion = 0;

void update(uint32_t frame) {
    bool firstFrame = (lastSeenVersion != scratchpadVersion);
    if (firstFrame) {
        lastSeenVersion = scratchpadVersion;
    }
    effect->fn(view, params, frame, firstFrame);
}
```

**Validation at setEffect:**
```cpp
void setEffect(const EffectInfo* newEffect) {
    if (newEffect->stateSize > SCRATCHPAD_SIZE) {
        LOG_ERROR(LogTag::LED, "Effect %s requires %d bytes, max is %d",
                  newEffect->id, newEffect->stateSize, SCRATCHPAD_SIZE);
        return;  // Refuse invalid effect
    }
    effect = newEffect;
    scratchpadVersion++;
    memset(scratchpad, 0, SCRATCHPAD_SIZE);  // Or just stateSize
}
```

**Rationale:** A union-based approach becomes fragile as effects grow. A fixed scratchpad with versioning is simpler, works for multi-segment, and avoids heap allocation.

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
- Overlapping segments in v2 (add composition layer later)

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
    CRGB* base;           // Pointer to controller's LED array
    uint16_t start;       // First LED index in segment
    uint16_t length;      // Number of LEDs in segment
    bool reversed;        // Run effect backwards?
    
    // Indexed access (handles reversal + offset)
    CRGB& operator[](uint16_t i) {
        uint16_t idx = reversed ? (length - 1 - i) : i;
        return base[start + idx];
    }
    
    // Raw access for FastLED functions that need CRGB*
    CRGB* raw() { return base + start; }
    
    // Convenience methods (delegate to FastLED)
    void fill(CRGB color);
    void fade(uint8_t amount);
    void blur(uint8_t amount);
};
```

**Why `base + start` instead of `CRGB* leds`:**
- **Contiguous today:** For now, `operator[]` just does `base[start + idx]`—zero overhead
- **Non-contiguous tomorrow:** Can add matrix mapping, serpentine, grouping without changing effect code
- **Per-segment transforms:** Opens door for per-segment gamma or color correction
- **Debug-friendly:** Can bounds-check in debug builds

**Design principle:** The view is a mapping abstraction. Effects use `view[i]`, not pointer arithmetic.

### Segment Class

Owns a view, effect binding, and scratchpad state:

```cpp
// Cached "what's meaningful right now" for UI/Matter/AI
struct SegmentCapabilities {
    bool hasBrightness = true;   // Always true for segments
    bool hasSpeed;               // Effect responds to speed
    bool hasIntensity;           // Effect responds to intensity
    bool hasPalette;             // Effect uses palette
    bool hasSecondaryColor;      // Effect uses colors[1]
};

class Segment {
public:
    // Identity (stable across deletions)
    uint32_t id;                // Monotonic, never reused
    
    // View into main LED array
    SegmentView view;
    
    // Effect binding
    const EffectInfo* effect;   // Current effect (includes fn pointer)
    EffectParams params;        // Effect parameters
    SegmentCapabilities caps;   // Cached from effect, updated on setEffect()
    
    // Per-segment state
    uint8_t brightness = 255;
    CRGB colors[2];             // Primary, secondary
    CRGBPalette16 palette;
    
    // Scratchpad for stateful effects (see Invariant 3)
    static constexpr size_t SCRATCHPAD_SIZE = 512;
    uint8_t scratchpad[SCRATCHPAD_SIZE];
    uint8_t scratchpadVersion = 0;
    
    void setEffect(const EffectInfo* newEffect) {
        effect = newEffect;
        scratchpadVersion++;    // Signals scratchpad reset
        memset(scratchpad, 0, SCRATCHPAD_SIZE);
        
        // Update cached capabilities from effect metadata
        caps.hasSpeed = newEffect->usesSpeed;
        caps.hasIntensity = newEffect->usesIntensity;
        caps.hasPalette = newEffect->usesPalette;
        caps.hasSecondaryColor = newEffect->usesSecondaryColor;
    }
    
    template<typename T>
    T* getScratchpad() {
        static_assert(sizeof(T) <= SCRATCHPAD_SIZE);
        return reinterpret_cast<T*>(scratchpad);
    }
    
    void update(uint32_t frame);
};
```

**Note on IDs:** Segment IDs are monotonic counters, not array indices. This means:
- Deleting segment 2 doesn't renumber segment 3
- UI can reliably target segments by ID
- Internal storage can still be array-based for efficiency

**Note on Capabilities:** `SegmentCapabilities` caches "what's meaningful right now" from `EffectInfo`:
- **UI:** Only show sliders for params the effect actually uses
- **Matter:** Map only relevant attributes to the segment
- **AI:** Constrain prompt understanding to meaningful params
- Updated automatically when effect changes—no manual sync needed

### Effect System

Effects are pure functions with rich metadata, registered by name:

```cpp
// Effect function signature
// Note: firstFrame is true when scratchpad was just reset (effect change)
using EffectFn = void (*)(    SegmentView& view,
    const EffectParams& params,
    uint32_t frame,
    bool firstFrame          // True when scratchpad just reset
);

// Effect metadata (enables rich UI/AI integration)
struct EffectInfo {
    const char* id;           // Machine name: "fire"
    const char* displayName;  // Human name: "Fire"
    const char* category;     // "Animated", "Solid", "Moving", "Special"
    
    // Parameter support flags
    bool usesPalette;         // Responds to palette changes
    bool usesSecondaryColor;  // Uses params.colors[1]
    bool usesSpeed;           // Responds to speed param
    bool usesIntensity;       // Responds to intensity param
    
    // Resource hints
    uint16_t stateSize;       // Bytes needed in scratchpad (0 = stateless)
    uint16_t minLeds;         // Minimum LEDs for effect to look good
    
    EffectFn fn;              // The actual function
};

// Registration with metadata
#define REGISTER_EFFECT(id, displayName, category, fn) \
    static EffectRegistrar _reg_##fn({ \
        .id = id, \
        .displayName = displayName, \
        .category = category, \
        /* ... defaults for flags ... */ \
        .fn = fn \
    })

// Example effect implementation
void effectFire(SegmentView& view, const EffectParams& p, uint32_t frame, bool firstFrame) {
    if (firstFrame) {
        // Initialize scratchpad state
    }
    // Full access to FastLED functions
    // Operates only on this segment's LEDs
}
REGISTER_EFFECT("fire", "Fire", "Animated", effectFire);
```

**Benefits:**
- No virtual function overhead
- Effects are testable in isolation
- Adding effects = adding files, no core changes
- Clear contract (inputs → LED colors)
- **Rich metadata for UI** (show relevant controls only)
- **AI prompt understanding** (knows which params matter)
- **Category filtering** (UI grouping, random within category)

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
| **Power** | `setMaxPowerInVoltsAndMilliamps` | Controller-level (global only) |
| **Correction** | `setCorrection`, `setTemperature` | Controller-level (global only) |

> **Design note:** Power limiting and color correction intentionally stay controller-global. Do not push these into segments—it adds complexity without real benefit and breaks FastLED's power calculation.

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

### Phase 0: Architecture Refinement 🔄 IN PROGRESS
**Goal:** Lock down invariants and refine core abstractions before API work

- [x] Document three core invariants (non-overlapping, single-writer, scratchpad)
- [x] Update SegmentView to use `base + start + length` mapping abstraction
- [x] Design EffectInfo metadata structure
- [x] Design scratchpad state model with versioning
- [x] Document command queue pattern
- [x] Document render pipeline
- [ ] **Implement** SegmentView refactor in code
- [ ] **Implement** EffectInfo metadata in registry
- [ ] **Implement** scratchpad state in Segment
- [ ] **Implement** command queue infrastructure
- [ ] Update effects to use new SegmentView API

**Status:** Design complete. Implementation pending.

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

**Status:** Compiles successfully. Core framework in place. **Needs Phase 0 refinements.**

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
  "id": 1,              // Stable monotonic ID (not array index!)
  "start": 0,
  "length": 80,
  "effect": {
    "id": "fire",
    "displayName": "Fire",
    "category": "Animated"
  },
  "brightness": 200,
  "speed": 150,
  "colors": ["#ff6600", "#ff0000"],
  "palette": "heat",
  "reverse": false,
  "capabilities": {     // Derived from effect, tells UI what to show
    "hasSpeed": true,
    "hasIntensity": true,
    "hasPalette": true,
    "hasSecondaryColor": false
  }
}
```

**Note:** Segment IDs are stable across deletions. If you delete segment 2, segment 3 keeps its ID. This makes UI state sync reliable.

### Full State

```json
{
  "power": true,
  "brightness": 255,
  "ledCount": 160,
  "nextSegmentId": 3,    // For generating stable IDs
  "segments": [
    {"id": 1, "start": 0, "length": 80, "effect": {"id": "fire", ...}, ...},
    {"id": 2, "start": 80, "length": 80, "effect": {"id": "rainbow", ...}, ...}
  ],
  "effects": [
    {"id": "solid", "displayName": "Solid", "category": "Solid", ...},
    {"id": "rainbow", "displayName": "Rainbow", "category": "Animated", ...},
    ...
  ],
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

| Approach | Pros | Cons |
|----------|------|------|
| Global/static per effect | Simple | Breaks multi-segment |
| Union in Segment | Type-safe | Grows fragile as effects increase |
| Fixed scratchpad | Flexible, segment-aware | Effects interpret raw bytes |

**Memory cost:** 512 bytes × MAX_SEGMENTS (8) = 4 KB. Acceptable on ESP32-S3 with PSRAM.

**Decision:** Fixed-size scratchpad (`uint8_t[512]`) per segment with version counter. Effects interpret bytes as needed. Version increments on effect change, triggering re-initialization. See [Invariant 3](#invariant-3-fixed-scratchpad-for-effect-state).

### How to Handle Concurrency?

ESP32 + AsyncWebServer means handlers run on different tasks/cores.

| Approach | Pros | Cons |
|----------|------|------|
| No protection | Simple | Race conditions, heisenbugs |
| Mutex/critical sections | Direct | Blocking, priority inversion risk |
| Command queue | Deterministic, non-blocking | Slightly more code |

**Decision:** Single-writer via command queue. All mutations (from web handlers, protocols, AI) enqueue commands. Main loop processes queue at frame start. See [Invariant 2](#invariant-2-single-writer-model-via-command-queue).

---

## Render Pipeline

Explicit ordering prevents "where does this logic belong?" confusion:

```
┌─────────────────────────────────────────────────────────┐
│                     Frame Start                         │
├─────────────────────────────────────────────────────────┤
│ 1. Process command queue (mutations from web/protocol)  │
├─────────────────────────────────────────────────────────┤
│ 2. Determine active mode                                │
│    ├─ Protocol override? (sACN data within timeout)     │
│    └─ Effects mode (default)                            │
├─────────────────────────────────────────────────────────┤
│ 3. Render                                               │
│    ├─ Protocol active: protocol writes directly to LEDs │
│    └─ Otherwise: for each segment (ordered by start):   │
│                  segment.update(frame, firstFrame)      │
├─────────────────────────────────────────────────────────┤
│ 4. Post-process (global brightness applied by FastLED)  │
├─────────────────────────────────────────────────────────┤
│ 5. FastLED.show()                                       │
├─────────────────────────────────────────────────────────┤
│ 6. Frame timing / yield                                 │
└─────────────────────────────────────────────────────────┘
```

**Protocol Semantics (protocols as writers, not modes):**
- Protocols (sACN, Art-Net, future Matter) are **temporary sole writers**
- When active, they have write priority over effects
- Timeout (5s without data) releases write priority back to effects
- This framing keeps the single-writer mental model consistent

**Critical: Protocol callbacks must not write LEDs directly.**

Protocol receive callbacks (UDP, etc.) may run on network tasks, not the main loop. To honor Invariant 2:

```cpp
// Protocol buffer (double-buffered or single + atomic flag)
CRGB protocolBuffer[MAX_LED_COUNT];
std::atomic<bool> protocolFrameReady{false};
uint32_t lastProtocolFrameMs = 0;

// In sACN callback (runs on network task)
void onSacnPacket(const uint8_t* dmxData, uint16_t length) {
    // Copy to protocol buffer, NOT to leds[]
    for (int i = 0; i < length/3 && i < ledCount; i++) {
        protocolBuffer[i] = CRGB(dmxData[i*3], dmxData[i*3+1], dmxData[i*3+2]);
    }
    protocolFrameReady.store(true, std::memory_order_release);
}

// In main loop (single writer)
void processProtocol() {
    if (protocolFrameReady.load(std::memory_order_acquire)) {
        memcpy(leds, protocolBuffer, sizeof(CRGB) * ledCount);
        lastProtocolFrameMs = millis();
        protocolFrameReady.store(false, std::memory_order_release);
    }
}
```

> **Why this matters:** Even if direct writes "seem fine," you will eventually see tearing or partial frames. The buffer + atomic flag pattern is the only way to truly honor single-writer.

**Segment Ordering:**
- Segments are rendered in order of increasing `start` index
- This is deterministic and intuitive
- Matters for future composition/transition features

---

## Open Questions

1. ~~**Overlapping segments**~~ — **RESOLVED:** Not supported in v2. Add composition layer later.
2. **Transition effects** — Fade between effects when switching? (Nice-to-have, not blocking)
3. **Preset format** — Store segment configs, or full state snapshots?
4. **Matrix mapping** — Multiple mapping strategies? Configurable? (SegmentView design supports this)
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
