# LUME - AI LED Strip Controller

ESP32-S3 + FastLED firmware with REST API, sACN/E1.31, MQTT, and segment-based LED control.

**Access:** `http://lume.local` | **AP Mode:** `LUME-Setup` (password: `ledcontrol`)

## Architecture Overview

**Single-Writer Model:** `LumeController` owns the LED buffer (`CRGB leds[]`) and is the **only**
writer of segment/LED state. Every input is a thin adapter that enqueues a typed command:
- Main loop calls `controller.update()` at ~60 FPS — drains the command bus, renders, shows
- Web/MQTT/AI handlers **enqueue commands** and return `202 Accepted` (no direct mutation); sACN writes an atomic double-buffer
- Effects are pure functions writing to their segment's `SegmentView`
- The finished frame is presented through the `ILedOutput` HAL (FastLED RMT by default)

**Data Flow:**
```
Web UI → JSON POST → API handler → enqueueCommand() → [bus] → controller.update() → Segment → Effect
                                 → 202 accepted; client reconciles via WebSocket state push / GET
sACN → ProtocolBuffer (atomic double-buffer) → controller.update() → ILedOutput
```

**Key Components:**
- [src/core/controller.h](src/core/controller.h) - Orchestrates segments, frame timing, protocols, the shared workbuffer
- [src/core/segment.h](src/core/segment.h) - `Region` slice + effect binding + 640-byte scratchpad
- [src/core/region.h](src/core/region.h) - `Region{start,length}` geometry value type (P1.3)
- [src/core/effect_registry.h](src/core/effect_registry.h) - Self-registering effects with schema + `EffectDims` metadata
- [src/core/param_schema.h](src/core/param_schema.h) / [param_codec.h](src/core/param_codec.h) - Typed params + the one shared (de)serializer
- [src/core/led_output.h](src/core/led_output.h) - `ILedOutput` HAL; `fastled_output.h` is the default backend
- [src/network/server.cpp](src/network/server.cpp) - Route registration and WebSocket state sync

## Adding New Effects

Effects are **schema-based**: a pure function plus a `ParamSchema` that declares its typed
parameters (the schema drives both `GET /api/v2/effects` and the web UI controls). Create
`src/visuallib/effects/youreffect.cpp`:

```cpp
#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"

namespace lume {

namespace youreffect { constexpr uint8_t SPEED = 0; constexpr uint8_t COLOR = 1; }

DEFINE_EFFECT_SCHEMA(youreffectSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Color("color", "Color", CRGB::Blue)
);

// Current signature — no legacy EffectParams argument:
void effectYourEffect(SegmentView& view, const ParamValues& params,
                      uint32_t frame, bool firstFrame) {
    CRGB color = params.getColor(youreffect::COLOR);
    // Touch pixels only via view[i] / the remap-safe primitives — raw() was removed (P1.4).
    for (uint16_t i = 0; i < view.size(); i++) view[i] = color;
}

// Register: fn, id, name, category, schema, stateSize. Defaults to a 1D (strip) effect;
// use REGISTER_EFFECT_SCHEMA_DIMS(..., Any/TwoD) to mark it remap-safe / matrix-native (P1.2).
REGISTER_EFFECT_SCHEMA(effectYourEffect, "youreffect", "Your Effect", Animated, youreffectSchema, 0);

} // namespace lume
```

Stateful effects use `view.getScratchpad<T>()` (640 B pad, `sizeof(State)` passed to the macro);
large/2D state borrows the workbuffer via `view.getScratchpadChecked<T>()`. Full guide:
[docs/ADDING_EFFECTS.md](docs/ADDING_EFFECTS.md).

## API Handler Pattern

Body handlers in `src/api/` claim the single-owner body guard (`beginBody`/`endBody`, P0.3),
accumulate byte-wise, then **enqueue a command and reply `202`** — never mutating state on the
web task:

```cpp
static String bodyBuffer;

void handleEndpointPost(AsyncWebServerRequest* request, uint8_t* data,
                        size_t len, size_t index, size_t total) {
    if (index == 0 && !checkAuth(request)) { sendUnauthorized(request); return; }
    if (index == 0) {
        if (!beginBody(request)) { request->send(409, ...); return; }  // one body at a time
        bodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) { endBody(request); /* 413 */ return; }
    }
    for (size_t i = 0; i < len; i++) bodyBuffer += (char)data[i];
    if (index + len >= total) {
        endBody(request);
        lume::controller.enqueueCommand(/* typed Command */);
        request->send(202, "application/json", "{\"status\":\"accepted\"}");
    }
}
```

Routes are registered in [src/network/server.cpp](src/network/server.cpp) with the `.onBody()` pattern for POST/PUT requests.

## Build & Deploy

```bash
pio run -t upload                            # USB upload (first flash)
pio run -t upload --upload-port lume.local   # OTA after initial flash
pio run -t uploadfs                          # Upload LittleFS web UI assets
```

**Configuration:** Hardware pin/limits in [src/constants.h](src/constants.h). Dev credentials in `src/secrets.h` (copy from `secrets.h.example`).

**OTA password:** Default `ledcontrol`, or the auth token if configured in web UI.

## Web UI Development

Frontend assets live in `data/` and are served via LittleFS:
- Edit `data/index.html`, `data/assets/app.js`, `data/assets/app.css`
- Run `pio run -t uploadfs` to push changes to device
- Firmware must be flashed first; `uploadfs` only updates the filesystem partition

## Critical Conventions

- **Namespace:** All core code in `namespace lume {}`
- **Global singleton:** `lume::controller` - access via `extern` declarations
- **Config persistence:** `Storage` class wraps NVS. Use `storage.saveConfig()`
- **Thread safety:** Protocol data uses `ProtocolBuffer` with atomic flags
- **Effect state:** Use segment scratchpad via `getScratchpad<T>()`, not static variables
- **Logging:** Use macros from [src/logging.h](src/logging.h): `LOG_INFO()`, `LOG_ERROR()`, etc.
- **Constants:** All magic numbers in [src/constants.h](src/constants.h) with `_MS`, `_SIZE` suffixes
- **Route order:** Register specific routes FIRST (`/api/prompt/apply` before `/api/prompt`)

## Hardware & Runtime Notes

- **LED Pin:** `LED_DATA_PIN` in constants.h (compile-time only, FastLED requirement)
- **Power:** External 5V supply (~60mA/LED at full white). ESP32 GND must connect to strip GND.
- **Watchdog:** 30s timeout auto-resets if main loop hangs; disabled during OTA
- **sACN Priority:** When protocol data flows, effects are skipped. 5s timeout returns to effects.

## Development Workflow

**Test immediately after implementation.** Use curl or the shell scripts in `test/` to verify changes before moving on. Update or add docs when functionality is confirmed working.

```bash
# Quick connectivity check
curl http://lume.local/health

# Test effect change (params are per-effect and nested; → 202 accepted)
curl -X PUT http://lume.local/api/v2/segments/0 \
  -H "Content-Type: application/json" \
  -d '{"effect":"fire","params":{"cooling":55,"sparking":120}}'
```

> Mutations return `202 {"status":"accepted"}` and apply next frame — read back the result via
> `GET /api/v2/segments/0` or the WebSocket state push, not the response body. A `params` object
> is applied whole against the effect schema: **omitted params reset to their schema defaults.**

See [test/](test/) for shell scripts covering API endpoints.

## Native Tests

Core logic has a host-side unit suite — run it before board builds (CI does too):

```bash
pio test -e native   # 37 tests: param_codec / persistence / command_bus / body_guard / region / scratchpad / effect_registry
```

Add or extend a `test_<area>` suite (and the stubs in `test/stubs/`) when you change a testable core seam.

## Known Residuals

None outstanding. The last one — the day-one scene half-build (Storage backend + 404'ing UI),
orphaned by the mid-project core swap — was removed in #27, not wired. Presets/scenes remain a
deferred future feature to be rebuilt on the command/`EffectSpec` model; no scene code exists
today, so don't assume any does.
