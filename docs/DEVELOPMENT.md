# Development Guide

This guide covers architecture, building, debugging, and contributing.

---

## Project Structure

```
src/
├── main.cpp              # WiFi, web server, OTA, event loop
├── led_controller.*      # FastLED wrapper, effects, palettes
├── anthropic_client.*    # FreeRTOS task for async LLM calls
├── sacn_receiver.*       # E1.31 DMX protocol handler
├── storage.*             # NVS persistence layer
├── web_ui.h              # Embedded HTML/CSS/JS (PROGMEM)
├── constants.h           # All configurable values
├── logging.h             # Structured logging system
├── secrets.h             # Your credentials (gitignored)
└── secrets.h.example     # Template for secrets
```

### Architecture Overview

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
│ LED    │ │ Anthropic  │ │   sACN      │ │  Storage   │ │ Web UI │
│ Control│ │  Client    │ │  Receiver   │ │  (NVS)     │ │(PROGMEM)│
└────────┘ └────────────┘ └─────────────┘ └────────────┘ └────────┘
```

**Data Flow:**
```
Web UI → JSON POST → main.cpp handler → ledController.stateFromJson() → FastLED
AI Prompt → anthropic_client task → JSON spec → ledController.applyEffectSpec()
sACN network → sacnReceiver.update() → DMX channels → CRGB array → FastLED
```

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

# Build and upload via USB
pio run -t upload

# Upload via OTA (after initial flash)
pio run -t upload --upload-port lume.local
```

### Serial Monitor

```bash
pio device monitor
```

Baud rate: 115200

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

1. Add enum value to `Effect` in [led_controller.h](../src/led_controller.h):
   ```cpp
   enum class Effect : uint8_t {
       // ...existing...
       METEOR,
   };
   ```

2. Add string mapping in `effectFromString()`:
   ```cpp
   if (str == "meteor") return Effect::METEOR;
   ```

3. Implement in `LedController::update()`:
   ```cpp
   case Effect::METEOR:
       effectMeteor();
       break;
   ```

4. Create the effect method:
   ```cpp
   void LedController::effectMeteor() {
       // Your effect code here
       // Use member variables for state, not static
   }
   ```

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

## FreeRTOS Tasks

| Task | Core | Priority | Stack | Purpose |
|------|------|----------|-------|---------|
| Main loop | 1 | 1 | Default | LED updates, web server |
| Anthropic | 0 | 5 | 16KB | Async HTTPS calls |

The AI client runs on Core 0 to avoid blocking LED updates on Core 1.

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
- [OpenRouter API](https://openrouter.ai/docs)
