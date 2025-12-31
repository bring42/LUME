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
│   ├── controller.*      # LumeController - owns LED array, segments, protocols
│   ├── segment.*         # Segment class with effect binding, scratchpad
│   ├── segment_view.h    # SegmentView - virtual range over LED array
│   ├── effect_registry.h # Effect function registry with metadata
│   ├── effect_params.h   # Common effect parameters
│   └── command_queue.h   # Thread-safe command queue
├── api/
│   ├── segments.*        # v2 multi-segment API handlers
│   ├── config.*          # Configuration endpoints
│   ├── status.*          # Status and health endpoints
│   ├── pixels.*          # Direct pixel control
│   ├── prompt.*          # AI natural language control (Anthropic)
│   └── nightlight.*      # Nightlight mode endpoints
├── network/
│   ├── server.*          # Web server setup and route registration
│   ├── wifi.*            # WiFi connection management
│   └── ota.*             # Over-the-air update handling
├── effects/
│   ├── effects.h         # All effect declarations
│   ├── solid.cpp         # Solid color effect
│   ├── rainbow.cpp       # Rainbow chase effect
│   ├── fire.cpp          # Fire simulation
│   ├── confetti.cpp      # Random confetti sparkles
│   ├── gradient.cpp      # Static color gradient
│   ├── pulse.cpp         # Color pulsing
│   ├── meteor.cpp        # Meteor shower
│   ├── twinkle.cpp       # Twinkling stars
│   ├── candle.cpp        # Candle flicker
│   ├── breathe.cpp       # Breathing pulse
│   └── ... (23 total)    # See effects.h for full list
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
Web UI → JSON POST → api/* handlers → lume::controller → Segment → Effect
AI Prompt → api/prompt → Anthropic API → JSON spec → controller → Segment
sACN network → SacnProtocol (UDP, multicast, E1.31) → ProtocolBuffer → controller.update() → FastLED
MQTT → MqttProtocol → controller commands → Segment → Effect
```

### Concurrency & Single-Writer Model

The LED buffer (`CRGB leds_[]`) is owned by `LumeController`. All mutations flow through a single writer:

- **Main loop** calls `controller.update()` ~60 times/sec
- **Web handlers** and **protocols** enqueue commands or use atomic buffers
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

Baud rate: 115200

### Continuous Integration

Every push and PR to `main` triggers a GitHub Actions build. See [.github/workflows/build.yml](../.github/workflows/build.yml).

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
constexpr uint16_t MAX_LED_COUNT = 300;

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

ESP async web server requires manual body buffering. Every POST handler MUST:

```cpp
// 1. Validate size at start
if (index == 0) {
    if (total > MAX_REQUEST_BODY_SIZE) {
        request->send(413, "application/json", 
            "{\"error\":\"Request body too large\"}");
        return;
    }
    bodyBuffer = "";
}

// 2. Accumulate chunks
bodyBuffer += String((char*)data).substring(0, len);

// 3. Process only when complete
if (index + len >= total) {
    // Parse JSON and handle request
}
```

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

2. Implement the handler:
   ```cpp
   void handleMyEndpoint(AsyncWebServerRequest* request, 
                         uint8_t* data, size_t len, 
                         size_t index, size_t total) {
       // Follow the body buffering pattern above
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

- **SRAM:** ~180KB available, LED buffer uses ~4.8KB for 300 LEDs
- **PSRAM:** 8MB available on T-Display S3 (`-DBOARD_HAS_PSRAM`)
- **Flash:** ~4MB, code uses ~180KB

Use `logMemoryStats()` to check heap status:

```cpp
logMemoryStats(LogTag::MAIN, "after wifi connect");
```

---

## AI Integration

The AI prompt feature uses Anthropic's Claude API via synchronous HTTPS requests:

- Configure API key and model in web UI settings
- Requests are handled directly in the web handler (no background tasks)
- System prompt includes available effects and their parameters
- Response is parsed as JSON and applied to the controller

See [api/prompt.cpp](../src/api/prompt.cpp) for implementation.

---

## Testing

### Test Body Size Limits

```bash
curl -X POST http://lume.local/api/pixels \
  -H "Content-Type: application/json" \
  -d "$(head -c 20000 /dev/zero | tr '\0' 'a')"
# Should return 413 error
```

### Test Rate Limiting

```bash
curl -X POST http://lume.local/api/prompt \
  -H "Content-Type: application/json" \
  -d '{"prompt":"rainbow"}'

# Immediate second request should fail with 429
curl -X POST http://lume.local/api/prompt \
  -H "Content-Type: application/json" \
  -d '{"prompt":"fire"}'
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
3. **POST Handlers:** Always validate body size at `index == 0`
4. **Effect State:** Use member variables, not static locals
5. **Memory:** Check heap after major operations with `logMemoryStats()`

---

## Resources

- [FastLED Documentation](https://fastled.io/)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- [ArduinoJson](https://arduinojson.org/)
- [E1.31/sACN Specification](https://tsp.esta.org/tsp/documents/docs/ANSI_E1-31-2018.pdf)
- [Anthropic API](https://docs.anthropic.com/)
