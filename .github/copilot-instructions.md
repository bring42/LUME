# LUME - Copilot Instructions

## Project Overview

**LUME** is an ESP32-S3 LED controller (LILYGO T-Display S3) for WS2812B LED strips with AI-generated effects. Three control paths: web UI, AI prompts (OpenRouter API), and sACN/E1.31 DMX protocol. Built with PlatformIO/Arduino framework.

**Access:** `http://lume.local` | **AP Mode:** `LUME-Setup` (password: `ledcontrol`)

## Architecture

**Component Boundaries:**
- `main.cpp`: WiFi setup (dual AP/STA), async web server routes, OTA, event loop coordination, watchdog
- `led_controller.*`: FastLED wrapper, 10 built-in effects, palette management, frame updates (non-blocking)
- `anthropic_client.*`: FreeRTOS task for async LLM calls, JSON effect spec parsing
- `sacn_receiver.*`: UDP multicast listener for E1.31 packets, DMX-to-RGB mapping
- `storage.*`: NVS (Non-Volatile Storage) wrapper for config/scenes/LED state persistence
- `web_ui.h`: Single-file embedded HTML/CSS/JS (PROGMEM constant, ~1200 lines)
- `constants.h`: All magic numbers centralized (timeouts, buffer sizes, ports, limits)
- `logging.h`: Structured logging with levels (DEBUG/INFO/WARN/ERROR) and component tags

**Data Flow:**
```
Web UI → JSON POST → main.cpp handler → ledController.stateFromJson() → FastLED
AI Prompt → anthropic_client task → JSON spec → ledController.applyEffectSpec()
sACN network → sacnReceiver.update() → DMX channels → CRGB array → FastLED
```

**Critical: Async Body Handling Pattern**  
ESP async web server requires buffering request bodies manually. Every POST handler MUST:
1. Validate `total > MAX_REQUEST_BODY_SIZE` at `index == 0` → return 413
2. Accumulate chunks in global buffer (e.g., `configBodyBuffer`)
3. Process only when `index + len >= total`

See [handleApiLedPost](src/main.cpp#L560) for the canonical pattern.

## Build & Deployment

```bash
pio run -t upload      # First flash via USB
pio device monitor     # 115200 baud, includes ESP32 exception decoder
```

**OTA Updates:** Set `upload_port = <device-ip>` in `platformio.ini` (password: `ledcontrol`)

## Development Secrets

Create `src/secrets.h` (gitignored) from `secrets.h.example`:
```cpp
#define DEV_WIFI_SSID "YourNetwork"
#define DEV_WIFI_PASSWORD "YourPass"
#define DEV_API_KEY "sk-or-..."
#define DEV_OPENROUTER_MODEL "anthropic/claude-3-haiku"
#define DEV_LED_PIN 21
#define DEV_LED_COUNT 160
#define DEV_DEFAULT_BRIGHTNESS 128
```

## Key Patterns

**Logging:** Use structured macros, not `Serial.print()`:
```cpp
LOG_INFO(LogTag::WIFI, "Connected! IP: %s", WiFi.localIP().toString().c_str());
LOG_WARN(LogTag::LED, "Failed to apply effect: %s", errorMsg.c_str());
LOG_DEBUG(LogTag::SACN, "Uni %d: %d pkts", universe, count);
```
Tags: `MAIN`, `WIFI`, `LED`, `AI`, `SACN`, `WEB`, `OTA`, `STORAGE`

**Constants:** All values in `constants.h`. Never inline magic numbers:
```cpp
constexpr uint32_t SACN_DATA_TIMEOUT_MS = 5000;
constexpr size_t MAX_REQUEST_BODY_SIZE = 16384;
constexpr uint32_t PROMPT_RATE_LIMIT_MS = 3000;
```

**Rate Limiting:** `/api/prompt` has 3-second cooldown to prevent API key exhaustion.

**sACN Priority:** When `config.sacnEnabled` and data flows, LED updates skip normal effects. Timeout after 5s returns to effects.

**FastLED Pin Limitation:** Pin is compile-time only. To change: edit `addLeds<>` template AND rebuild.

**OTA Safety:** LEDs off during OTA, watchdog disabled to prevent timeout during long uploads.

**API Route Order:** More specific routes registered FIRST (`/api/prompt/apply` before `/api/prompt`).

## Common Tasks

**Add new effect:**
1. Add enum to `Effect` in `led_controller.h`
2. Implement in `LedController::update()` switch case
3. Update `effectFromString()` parser

**Add new constant:** Always in `constants.h` with descriptive name (e.g., `_MS`, `_SIZE` suffix)

**Add POST handler:** Follow the body buffering pattern with size validation at `index == 0`

## Hardware Notes

- **GPIO 21:** Default LED data pin (compile-time only, see FastLED limitation above)
- **Power:** External 5V supply (~60mA/LED). ESP32 GND must connect to strip GND.
- **Watchdog:** 30s timeout auto-resets if main loop hangs
- **PSRAM:** Enabled (`-DBOARD_HAS_PSRAM`)

## API Quick Reference

Base URL: `http://<device-ip>` (AP mode: `http://192.168.4.1`)

| Endpoint | Method | Body Example |
|----------|--------|--------------|
| `/api/pixels` | POST | `{"fill": [255,0,0]}` or `{"rgb": [r,g,b,...]}` |
| `/api/prompt` | POST | `{"prompt": "warm sunset fading to purple"}` |
| `/api/prompt/status` | GET | Returns: `idle\|queued\|running\|done\|error` |
| `/api/prompt/apply` | POST | Applies last generated spec |
| `/api/scenes` | POST | `{"name": "Sunset", "spec": "{...}"}` |
| `/api/scenes/{id}/apply` | POST | Load saved scene |
