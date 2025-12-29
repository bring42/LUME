# API Reference

The AI LED Strip Controller exposes a REST API for control and configuration. All endpoints return JSON.

**Base URL:** `http://<device-ip>` or `http://lume.local`

---

## Authentication

API authentication is **optional**. Set an auth token in Settings to protect write endpoints.

When enabled, include the token in requests using one of:

```bash
# Bearer token (recommended)
curl -H "Authorization: Bearer YOUR_TOKEN" http://lume.local/api/led -X POST -d '...'

# X-API-Key header
curl -H "X-API-Key: YOUR_TOKEN" http://lume.local/api/led -X POST -d '...'

# Query parameter (for browser testing)
curl "http://lume.local/api/led?token=YOUR_TOKEN" -X POST -d '...'
```

**Protected endpoints:** All POST/DELETE operations (`/api/led`, `/api/config`, `/api/prompt`, `/api/pixels`, `/api/scenes`, `/api/nightlight`)

**Unprotected endpoints:** GET requests (`/api/status`, `/api/led`, `/health`) - read-only access is always allowed

**OTA updates:** Also protected by the auth token when set.

---

## Quick Reference

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | System health & diagnostics |
| `/api/status` | GET | Device status |
| `/api/config` | GET/POST | Configuration |
| `/api/segments` | GET | List segments & effects |
| `/api/led` | GET/POST | LED state control (legacy) |
| `/api/prompt` | POST | AI effect generation |
| `/api/prompt/status` | GET | AI job status |
| `/api/prompt/apply` | POST | Apply generated effect |
| `/api/pixels` | POST | Direct pixel control |
| `/api/nightlight` | GET/POST | Nightlight mode |
| `/api/scenes` | GET/POST | Scene management |

---

## Health Check

```http
GET /health
```

Lightweight endpoint for monitoring. Great for uptime checks and diagnostics.

**Response:**
```json
{
  "status": "healthy",
  "uptime": 3600,
  "version": "1.0.0",
  "memory": {
    "heap_free": 245000,
    "heap_min": 220000,
    "heap_max_block": 110000,
    "psram_free": 8000000,
    "fragmentation": 55
  },
  "network": {
    "wifi_connected": true,
    "wifi_rssi": -52,
    "ip": "192.168.1.100",
    "ap_clients": 0
  },
  "components": {
    "led_controller": true,
    "storage": true,
    "sacn_enabled": false,
    "sacn_receiving": false,
    "ai_last_state": "idle"
  }
}
```

---

## Status

```http
GET /api/status
```

Returns device status including uptime, WiFi state, IP, heap memory, and LED state.

---

## Segments

```http
GET /api/segments
```

Returns all segments, available effects with metadata, and device capabilities.

**Response:**
```json
{
  "segments": [
    {
      "id": 0,
      "start": 0,
      "length": 160,
      "effect": "rainbow",
      "speed": 100,
      "intensity": 128,
      "palette": "rainbow",
      "colors": [[255,0,0], [0,255,0]],
      "reversed": false,
      "mirror": false
    }
  ],
  "effects": [
    {
      "id": "rainbow",
      "name": "Rainbow",
      "category": "moving",
      "stateSize": 0
    }
  ],
  "capabilities": {
    "maxSegments": 8,
    "totalLeds": 160,
    "scratchpadSize": 512
  }
}
```

---

## Configuration

```http
GET /api/config
```

Returns current configuration (API key is masked).

```http
POST /api/config
Content-Type: application/json
```

**Request Body:**
```json
{
  "wifiSSID": "YourNetwork",
  "wifiPassword": "YourPassword",
  "apiKey": "sk-or-...",
  "openRouterModel": "anthropic/claude-3-haiku",
  "ledPin": 21,
  "ledCount": 160,
  "sacnEnabled": false,
  "sacnUniverse": 1,
  "sacnStartChannel": 1
}
```

---

## LED Control

```http
GET /api/led
```

Returns current LED state.

```http
POST /api/led
Content-Type: application/json
```

**Request Body:**
```json
{
  "power": true,
  "brightness": 128,
  "effect": "rainbow",
  "palette": "rainbow",
  "speed": 100,
  "primaryColor": [0, 0, 255],
  "secondaryColor": [128, 0, 128]
}
```

### Available Effects (23 total)

`solid`, `rainbow`, `confetti`, `fire`, `colorwaves`, `theater`, `gradient`, `sparkle`, `pulse`, `noise`, `meteor`, `twinkle`, `sinelon`, `candle`, `breathe`, `dots`, `juggle`, `bpm`, `larson`, `cylon`, `lightning`, `ripple`, `pacifica`

### Available Palettes

`rainbow`, `lava`, `ocean`, `party`, `forest`, `cloud`, `heat`, `sunset`, `autumn`, `retro`, `ice`, `pink`, `custom`

---

## AI Effect Generation

### Generate Effect

```http
POST /api/prompt
Content-Type: application/json
```

**Request Body:**
```json
{
  "prompt": "Warm sunset colors slowly fading between orange and purple"
}
```

**Rate Limited:** 3 second cooldown between requests.

### Check Generation Status

```http
GET /api/prompt/status
```

**Response:** `idle`, `queued`, `running`, `done`, or `error`

### Apply Generated Effect

```http
POST /api/prompt/apply
```

Applies the last generated effect specification to the LEDs.

---

## Direct Pixel Control

```http
POST /api/pixels
Content-Type: application/json
```

Control individual pixels directly—perfect for AI-generated patterns or custom integrations.

### Method 1: Array of RGB triplets

```json
{
  "pixels": [[255,0,0], [0,255,0], [0,0,255]],
  "brightness": 200
}
```

### Method 2: Flat RGB array (more compact)

```json
{
  "rgb": [255,0,0, 0,255,0, 0,0,255],
  "brightness": 200
}
```

### Method 3: Fill all pixels

```json
{
  "fill": [255, 100, 0]
}
```

### Method 4: Gradient

```json
{
  "gradient": {
    "from": [255, 0, 0],
    "to": [0, 0, 255]
  }
}
```

**Examples:**
```bash
# Fill all pixels red
curl -X POST http://lume.local/api/pixels \
  -H "Content-Type: application/json" \
  -d '{"fill": [255, 0, 0]}'

# Rainbow gradient
curl -X POST http://lume.local/api/pixels \
  -H "Content-Type: application/json" \
  -d '{"gradient": {"from": [255,0,0], "to": [0,0,255]}}'
```

---

## Nightlight Mode

Gradual brightness fade over a configurable duration—perfect for falling asleep with the lights on.

### Get Status

```http
GET /api/nightlight
```

**Response:**
```json
{
  "active": true,
  "progress": 0.35,
  "duration": 900,
  "targetBrightness": 0,
  "startBrightness": 200,
  "currentBrightness": 130
}
```

### Start Nightlight

```http
POST /api/nightlight
Content-Type: application/json
```

**Request Body:**
```json
{
  "duration": 900,
  "targetBrightness": 0
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `duration` | int | 900 | Fade duration in seconds (1-3600) |
| `targetBrightness` | int | 0 | Final brightness (0-255, 0 = off) |

**Examples:**
```bash
# 15-minute fade to off (default)
curl -X POST http://lume.local/api/nightlight -H "Content-Type: application/json" -d '{}'

# 30-minute fade to dim (brightness 20)
curl -X POST http://lume.local/api/nightlight -H "Content-Type: application/json" -d '{"duration": 1800, "targetBrightness": 20}'
```

### Cancel Nightlight

```http
POST /api/nightlight/stop
```

Cancels active nightlight mode, leaving brightness at current level.

---

## Scene Management

### List Scenes

```http
GET /api/scenes
```

### Save Scene

```http
POST /api/scenes
Content-Type: application/json
```

**Request Body:**
```json
{
  "name": "Sunset Vibes",
  "spec": "{...effect specification...}"
}
```

### Apply Scene

```http
POST /api/scenes/{id}/apply
```

---

## Effect Specification Schema

The AI generates effects matching this JSON schema:

```json
{
  "effect": "solid|rainbow|confetti|fire|colorwaves|theater|gradient|sparkle|pulse|noise|meteor|twinkle|sinelon|candle|breathe|dots|juggle|bpm|larson|cylon|lightning|ripple|pacifica|custom",
  "palette": "rainbow|lava|ocean|party|forest|cloud|heat|sunset|autumn|retro|ice|pink|custom",
  "brightness": 0-255,
  "speed": 1-200,
  "primaryColor": [r, g, b],
  "secondaryColor": [r, g, b],
  "notes": "Human-readable description",
  "custom": {
    "type": "gradient|sparkle|pulse|noise",
    "param1": 0-255,
    "param2": 0-255,
    "param3": 0-255,
    "param4": 0-255
  }
}
```

---

## Error Responses

| Code | Meaning |
|------|---------|
| 400 | Bad Request - Invalid JSON or missing fields |
| 413 | Payload Too Large - Request body exceeds 16KB |
| 429 | Too Many Requests - Rate limited (wait 3s) |
| 500 | Internal Server Error |

**Error Format:**
```json
{
  "error": "Description of what went wrong"
}
```
