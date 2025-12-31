# LUME - AI LED Strip Controller

ESP32-S3 + FastLED firmware with REST API, sACN/E1.31, MQTT, and segment-based LED control.

**Access:** `http://lume.local` | **AP Mode:** `LUME-Setup` (password: `ledcontrol`)

## Architecture Overview

**Single-Writer Model:** `LumeController` owns the LED buffer (`CRGB leds[]`). All mutations flow through it:
- Main loop calls `controller.update()` at ~60 FPS
- Web handlers/protocols enqueue commands or use atomic buffers
- Effects are pure functions writing to their segment's `SegmentView`

**Data Flow:**
```
Web UI → JSON POST → API handlers → controller.enqueueCommand() → Segment → Effect
sACN/MQTT → Protocol implementations → ProtocolBuffer (atomic) → controller.update()
```

**Key Components:**
- [src/core/controller.h](src/core/controller.h) - Orchestrates segments, frame timing, protocols
- [src/core/segment.h](src/core/segment.h) - LED range + effect binding + 512-byte scratchpad
- [src/core/effect_registry.h](src/core/effect_registry.h) - Self-registering effects with metadata
- [src/network/server.cpp](src/network/server.cpp) - Route registration and WebSocket state sync

## Adding New Effects

Create `src/effects/youreffect.cpp`:

```cpp
#include "../core/effect_registry.h"

namespace lume {

void effectYourEffect(SegmentView& view, const EffectParams& params, 
                      uint32_t frame, bool firstFrame) {
    // frame for timing (use with beatsin8, etc.)
    // firstFrame = true when scratchpad was reset
    for (uint16_t i = 0; i < view.size(); i++) {
        view[i] = ColorFromPalette(params.palette, i + frame);
    }
}

// Choose macro based on parameter usage:
// REGISTER_EFFECT_PALETTE  - palette + speed
// REGISTER_EFFECT_COLORS   - primary + secondary colors + speed  
// REGISTER_EFFECT_ANIMATED - speed + intensity
REGISTER_EFFECT_PALETTE(effectYourEffect, "youreffect", "Your Effect");

} // namespace lume
```

Registration macros set `usesSpeed`, `usesPalette`, etc. flags that the Web UI reads to show/hide controls. See [docs/ADDING_EFFECTS.md](docs/ADDING_EFFECTS.md) for full macro table.

## API Handler Pattern

API handlers in `src/api/` follow this structure:

```cpp
// Static buffer for async body accumulation
static String bodyBuffer;

void handleEndpointPost(AsyncWebServerRequest* request, uint8_t* data, 
                        size_t len, size_t index, size_t total) {
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    // Accumulate body, process when complete
}
```

Routes are registered in [src/network/server.cpp](src/network/server.cpp) with the `.onBody()` pattern for POST requests.

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

# Test effect change
curl -X PUT http://lume.local/api/v2/segments/0 \
  -H "Content-Type: application/json" \
  -d '{"effect":"fire","speed":150}'
```

See [test/](test/) for shell scripts covering API endpoints.

## Known Residuals

Some v1 artifacts remain (e.g., `src/api/scenes.cpp` uses old format, some constants reference removed `anthropic_client`). These are marked with TODOs and will be cleaned up as features are reimplemented. Check `docs/archive/` for historical reference.
