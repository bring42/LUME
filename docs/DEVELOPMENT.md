# Development Guide

This guide covers architecture, building, debugging, and contributing.

---

## Project Structure

```
src/
├── main.cpp              # WiFi, web server, OTA, event loop
├── storage.*             # NVS persistence layer
├── constants.h           # All configurable values
├── logging.h             # Structured logging system
├── secrets.h             # Your credentials (gitignored)
├── secrets.h.example     # Template for secrets
├── core/
│   ├── controller.*      # LumeController - owns LED array, segments, protocols, workbuffer
│   ├── segment.*         # Segment class with effect binding, scratchpad
│   ├── segment_view.h    # SegmentView - Region-based view over the LED array
│   ├── region.h          # Region{start,length} geometry value type (P1.3)
│   ├── effect_registry.h # Effect registry with schema + EffectDims metadata
│   ├── param_schema.h    # ParamDesc / ParamSchema / ParamValues (typed params)
│   ├── param_codec.h     # One schema-aware param (de)serializer (P1.1)
│   ├── effect_params.h   # Palette presets + CRGBPalette16 lookup
│   ├── segment_serializer.h # Canonical segment→JSON (v2 REST + WebSocket, P1.7)
│   ├── led_output.h      # ILedOutput HAL seam (RFC 0001 §6)
│   ├── fastled_output.h  # Default FastLED/RMT output backend
│   ├── body_guard.h      # Single-owner request-body guard (P0.3)
│   └── command_queue.h   # Thread-safe single-writer command bus
├── api/
│   ├── segments.*        # v2 multi-segment API + effects/palettes/info metadata
│   ├── config.*          # Configuration endpoints
│   ├── status.*          # Status and health endpoints
│   ├── pixels.*          # Direct pixel control
│   ├── prompt.*          # AI natural language control (Anthropic, worker task)
│   └── nightlight.*      # Nightlight mode endpoints
├── network/
│   ├── server.*          # Web server, routes, WebSocket state push
│   ├── wifi.*            # WiFi connection management
│   └── ota.*             # Over-the-air update handling
├── visuallib/
│   ├── effects.h         # All effect declarations
│   └── effects/
│       ├── solid.cpp     # Solid color effect
│       ├── rainbow.cpp   # Rainbow chase effect
│       ├── fire.cpp      # Fire simulation
│       ├── gradient.cpp  # Static color gradient
│       └── ... (23 total, one .cpp per effect)
└── protocols/
    ├── protocol.h        # Protocol interface + ProtocolBuffer
    ├── sacn.*            # Self-contained sACN/E1.31 implementation
    └── mqtt.*            # MQTT protocol support

data/                     # LittleFS web UI (uploaded separately)
├── index.html            # Main web interface
└── assets/
    ├── app.js            # Client-side JavaScript
    └── app.css           # Styles
```

### Architecture Overview (v2)

```
┌─────────────────────────────────────────────────────────────────┐
│                         main.cpp                                 │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────┐  │
│  │ Web Server  │  │  WiFi/OTA    │  │    Event Loop          │  │
│  └─────┬───────┘  └──────────────┘  └────────────────────────┘  │
└────────┼────────────────────────────────────────────────────────┘
         │
    ┌────┴────┬──────────────┬───────────────┬────────────────┐
    ▼         ▼              ▼               ▼                ▼
┌────────┐ ┌────────────┐ ┌─────────────┐ ┌────────────┐ ┌────────┐
│ Lume   │ │ Anthropic  │ │   sACN      │ │  Storage   │ │ Web UI │
│Controller│ │  Client  │ │  Protocol   │ │  (NVS)     │ │(PROGMEM)│
└────────┘ └────────────┘ └─────────────┘ └────────────┘ └────────┘
     │
     ├── Segments (independent LED zones)
     │   └── Effects (pure functions with scratchpad state)
     └── Protocols (sACN, Art-Net, etc.)
```

**Data Flow:**
```
Web UI → JSON POST → api/* handlers → enqueueCommand() → [bus] → controller.update() → Segment → Effect
AI Prompt → api/prompt → 202 → worker task → Anthropic API → applySpec() → enqueueCommand() → [bus]
sACN network → SacnProtocol (UDP, multicast, E1.31) → ProtocolBuffer → controller.update() → ILedOutput
MQTT → MqttProtocol → enqueueCommand() → [bus] → Segment → Effect
```

Every mutating input is a thin adapter that enqueues a typed command; the render loop is the
sole writer of segment/LED state, and it presents the finished frame through the `ILedOutput`
HAL (FastLED RMT by default). Mutating HTTP requests return **`202 Accepted`** and clients
reconcile via the WebSocket state push or a follow-up `GET`.

### Concurrency & Single-Writer Model

The LED buffer (`CRGB leds_[]`) is owned by `LumeController`. All mutations flow through a single writer:

- **Main loop** calls `controller.update()` ~60 times/sec — the only writer of `leds[]`/segment state
- **Web handlers** and **MQTT/AI** enqueue commands; **sACN** writes an atomic double-buffer
- Effects are pure functions that write to their segment's view

**Thread-safety patterns:**
1. **Protocol data:** Uses `ProtocolBuffer` with `std::atomic<bool>` flag. sACN implementation is self-contained with direct UDP socket management, multicast join/leave, E1.31 packet parsing, and source priority handling.
2. **Effect state:** Segment scratchpad is reset on effect change (version counter)
3. **sACN priority:** When protocol data flows, effects are skipped entirely

---

## Building

### Prerequisites

1. Install [PlatformIO IDE](https://platformio.org/install/ide)
2. Clone the repository
3. Copy `src/secrets.h.example` to `src/secrets.h`
4. Edit `src/secrets.h` with your credentials

### Build & Upload

```bash
# Build only
pio run

# Build and upload via USB (required for first flash)
pio run -t upload

# Upload web UI filesystem (do this after first firmware flash)
pio run -t uploadfs
```

> 📦 **Automatic Compression:** Web files are automatically gzipped before upload (~83% size reduction). ESPAsyncWebServer serves compressed versions transparently. See [scripts/gzip_web_files.py](../scripts/gzip_web_files.py) for implementation.

### Over-The-Air (OTA) Updates

Once you've done the initial USB flash, **never plug in a cable again!** Update wirelessly from anywhere on your network:

```bash
pio run -t upload --upload-port lume.local
```

That's it. PlatformIO will compile, find your device via mDNS, and push the update.

#### Enabling OTA in platformio.ini

For convenience, uncomment the OTA section in `platformio.ini`:

```ini
; === OTA UPLOAD ===
upload_protocol = espota
upload_port = lume.local          ; Or use IP like 192.168.1.100
upload_flags = 
    --port=3232
    --auth=ledcontrol             ; Default password, or your auth token if set
    --timeout=20
```

Then just `pio run -t upload` works wirelessly.

#### OTA Password

- **Default:** `ledcontrol`
- **Custom:** Set an **auth token** in the web UI Settings — it becomes your OTA password too

#### Why OTA is Great

- 📍 **Device can be anywhere** — mounted on a ceiling, behind furniture, in another room
- ⚡ **Faster iteration** — no unplugging, no walking to the device
- 🔄 **Settings persist** — your WiFi, API key, and LED config survive updates

#### Troubleshooting OTA

| Issue | Solution |
|-------|----------|
| "No response from device" | Check it's on WiFi (not AP mode), same network as you |
| "Auth failed" | Password is `ledcontrol` or your auth token |
| "Timeout" | Increase `--timeout=30`, check firewall allows UDP 3232 |
| mDNS not working | Use IP address instead of `lume.local` |

### Serial Monitor

```bash
pio device monitor
```

### Web UI Development

Frontend assets live in `data/` and are served via LittleFS:
- Edit `data/index.html`, `data/assets/app.js`, `data/assets/app.css`
- Run `pio run -t uploadfs` to push changes to device
- Firmware must be flashed first; `uploadfs` only updates the filesystem partition

**Automatic Gzip Compression:**
- Script: [scripts/gzip_web_files.py](../scripts/gzip_web_files.py)
- Trigger: Runs before `uploadfs` and `uploadfsota` commands
- Result: 88K uncompressed → 15K compressed (~83% reduction)
- Smart caching: Only recompresses files that have changed

ESPAsyncWebServer automatically serves `.gz` files when they exist, with proper `Content-Encoding` headers. Browsers decompress transparently.

Baud rate: 115200

### Continuous Integration

Every push and PR to `main` triggers GitHub Actions, which runs the **native host tests
(`pio test -e native`) first** and then the board builds — so a core-logic regression fails
before any firmware is compiled. See [.github/workflows/build.yml](../.github/workflows/build.yml).

The build badge in the README shows current status:

![Build](https://github.com/bring42/LUME/actions/workflows/build.yml/badge.svg)

---

## Configuration

All magic numbers live in [constants.h](../src/constants.h):

```cpp
// Timeouts
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t SACN_DATA_TIMEOUT_MS = 5000;

// Limits
constexpr size_t MAX_REQUEST_BODY_SIZE = 16384;
constexpr uint16_t MAX_LED_COUNT = 1000;

// Hardware
constexpr uint8_t LED_VOLTAGE = 5;
constexpr uint16_t LED_MAX_MILLIAMPS = 2000;

// Network
constexpr uint16_t WEB_SERVER_PORT = 80;
constexpr uint16_t SACN_PORT = 5568;
```

---

## Structured Logging

Use the logging macros instead of `Serial.print()`:

```cpp
#include "logging.h"

LOG_INFO(LogTag::WIFI, "Connected! IP: %s", WiFi.localIP().toString().c_str());
LOG_WARN(LogTag::LED, "Failed to apply effect: %s", errorMsg.c_str());
LOG_DEBUG(LogTag::SACN, "Uni %d: %d pkts", universe, count);
LOG_ERROR(LogTag::AI, "API request failed: %d", httpCode);
```

### Log Tags

`MAIN`, `WIFI`, `LED`, `AI`, `SACN`, `WEB`, `OTA`, `STORAGE`

### Log Levels

- `DEBUG` - Verbose debugging info
- `INFO` - Normal operational messages
- `WARN` - Warning conditions
- `ERROR` - Error conditions

### Compile-Time Filtering

In `platformio.ini`:
```ini
build_flags = 
    -D LOG_LEVEL=LogLevel::INFO   ; Hide DEBUG messages
```

### Output Format

```
[    1234] [I] [MAIN] === AI LED Strip Controller v1.0.0 ===
[    5678] [D] [SACN] Uni 1: 100 pkts, seq=42
[   10000] [W] [LED] Failed to apply effect: Invalid palette
```

---

## Async Body Handling Pattern

ESPAsyncWebServer delivers request bodies in chunks across multiple callback invocations, and
AsyncTCP can interleave two requests. Every POST/PUT body handler claims a **single-owner body
guard** (`beginBody`/`endBody`, backed by the pure/host-tested `core/body_guard.h`, P0.3) so
two bodies can't corrupt the shared static buffer:

```cpp
static String bodyBuffer;

void handleEndpointPost(AsyncWebServerRequest* req, uint8_t* data,
                        size_t len, size_t index, size_t total) {
    if (index == 0 && !checkAuth(req)) { sendUnauthorized(req); return; }

    // 1. Claim the body slot + validate size at the first chunk
    if (index == 0) {
        if (!beginBody(req)) {              // another body is mid-assembly
            req->send(409, "application/json", "{\"error\":\"Busy, retry\"}");
            return;
        }
        bodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            endBody(req);
            req->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }

    // 2. Accumulate chunks (append byte-wise; do NOT rely on NUL-terminated data)
    for (size_t i = 0; i < len; i++) bodyBuffer += (char)data[i];

    // 3. Release the slot and act only when the body is complete
    if (index + len >= total) {
        endBody(req);
        // parse JSON, enqueue a command, and reply 202 {"status":"accepted"}
    }
}
```

Mutations **enqueue a command and return `202`** — they never touch segment/LED state on the
web task.

---

## Adding a New Effect

See [ADDING_EFFECTS.md](ADDING_EFFECTS.md) for the complete guide to creating custom LED effects with registration macros, parameter usage, and best practices.

---

## Adding a New API Endpoint

1. Register the route in `main.cpp` setup:
   ```cpp
   server.on("/api/myendpoint", HTTP_POST, 
       [](AsyncWebServerRequest* request) {},
       NULL,
       handleMyEndpoint);
   ```

2. Implement the handler following the body-guard pattern above (claim `beginBody`, accumulate,
   `endBody`, then enqueue a command and reply `202`):
   ```cpp
   void handleMyEndpoint(AsyncWebServerRequest* request, 
                         uint8_t* data, size_t len, 
                         size_t index, size_t total) {
       // beginBody / accumulate / endBody, then:
       lume::controller.enqueueCommand(/* a typed Command */);
       request->send(202, "application/json", "{\"status\":\"accepted\"}");
   }
   ```

**Route Order:** Register more specific routes FIRST (`/api/prompt/apply` before `/api/prompt`).

---

## OTA Updates

After initial USB flash, enable OTA:

1. Edit `platformio.ini`:
   ```ini
   upload_protocol = espota
   upload_port = lume.local
   upload_flags = --auth=ledcontrol
   ```

2. Upload wirelessly:
   ```bash
   pio run -t upload
   ```

**Password:** `ledcontrol`

**Safety:** LEDs turn off during OTA, watchdog is disabled to prevent timeout.

---

## Watchdog Timer

The firmware includes a 30-second watchdog. If `loop()` doesn't run for 30 seconds, the device automatically reboots.

```cpp
// Disabled during OTA
ArduinoOTA.onStart([]() {
    esp_task_wdt_delete(NULL);
});

ArduinoOTA.onEnd([]() {
    esp_task_wdt_add(NULL);
});
```

---

## Memory Notes

- **SRAM:** ~180KB available; the LED buffer is 3 bytes/LED (~3KB at the 1000-LED `MAX_LED_COUNT`)
- **PSRAM:** Optional (8MB available on T-Display S3). Flag `-DBOARD_HAS_PSRAM` enables it if present. Default config uses only ~6KB of 512KB internal SRAM.
- **Flash:** ~4MB, code uses ~180KB

Use `logMemoryStats()` to check heap status:

```cpp
logMemoryStats(LogTag::MAIN, "after wifi connect");
```

---

## AI Integration

The AI prompt feature uses Anthropic's Claude API. The blocking ~30 s HTTPS call runs on a
dedicated FreeRTOS **worker task** (P0.4), never on the AsyncTCP web task:

- Configure API key and model in web UI settings
- `POST /api/prompt` validates + rate-limits, hands the prompt to the worker, and returns
  **`202 Accepted`** immediately (a second prompt within `PROMPT_RATE_LIMIT_MS` gets `429`;
  an overlapping in-flight request gets `409`)
- The worker calls the API, parses the JSON spec, and applies it via `applySpec()` — which
  **enqueues bus commands** (single-writer), so the worker never touches segment state directly
- The result lands on segment 0; clients observe it via the WebSocket push or a `GET`

See [api/prompt.cpp](../src/api/prompt.cpp) for implementation.

---

## Testing

### Native Host Tests (run these first)

The core logic has a **native unit-test suite** that runs on your host machine — no board, no
flashing:

```bash
pio test -e native
```

**37 tests** across `param_codec`, `persistence`, `command_bus`, `body_guard`, `region`,
`scratchpad`, and `effect_registry` (see [test/](../test/); stubs for Arduino/FastLED symbols
live in `test/stubs/`). CI runs this suite **before** the board builds, so a logic regression
fails fast without hardware. When you add or change a testable core seam, extend the matching
suite (or add a `test_<area>` folder) and the stubs as new symbols are needed.

### On-device / API smoke tests (curl)

The checks below need a running device.

### Test Body Size Limits

```bash
curl -X POST http://lume.local/api/pixels \
  -H "Content-Type: application/json" \
  -d "$(head -c 20000 /dev/zero | tr '\0' 'a')"
# Should return 413 error
```

### Test Rate Limiting

```bash
# First request is accepted (202) and handed to the AI worker task
curl -X POST http://lume.local/api/prompt \
  -H "Content-Type: application/json" \
  -d '{"prompt":"rainbow"}'
# → 202 {"status":"accepted"}

# Immediate second request is throttled
curl -X POST http://lume.local/api/prompt \
  -H "Content-Type: application/json" \
  -d '{"prompt":"fire"}'
# → 429 (within PROMPT_RATE_LIMIT_MS), or 409 if the first is still in flight
```

### Test Input Validation

```bash
# Missing color values
curl -X POST http://lume.local/api/pixels \
  -H "Content-Type: application/json" \
  -d '{"fill": [255]}'
# Should return 400 with helpful error
```

---

## Best Practices

1. **Constants:** Always use `constants.h`, never inline magic numbers
2. **Logging:** Use structured logging macros, not `Serial.print()`
3. **POST Handlers:** Claim `beginBody()` and validate body size at `index == 0`; reply `202`
4. **Effect State:** Use the segment scratchpad (`view.getScratchpad<T>()`), never `static`/global state
5. **Memory:** Check heap after major operations with `logMemoryStats()`

---

## Resources

- [FastLED Documentation](https://fastled.io/)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- [ArduinoJson](https://arduinojson.org/)
- [E1.31/sACN Specification](https://tsp.esta.org/tsp/documents/docs/ANSI_E1-31-2018.pdf)
- [Anthropic API](https://docs.anthropic.com/)
