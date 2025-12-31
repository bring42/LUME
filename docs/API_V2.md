# LUME v2 API Documentation

**Base URL:** `http://lume.local` (or device IP address)

The v2 API provides multi-segment LED control with full access to the segment-based architecture.

## Authentication

All endpoints support optional token-based authentication:
- Header: `Authorization: Bearer YOUR_TOKEN`
- Header: `X-API-Key: YOUR_TOKEN`
- Query param: `?token=YOUR_TOKEN`

---

## Update Semantics

All PUT endpoints use partial-update semantics. Any field you omit from the request body keeps its prior value on the device. This matches the current embedded UI behavior and avoids the need for a separate PATCH verb in constrained environments.

---

## Error Handling

All endpoints return JSON on failure so clients can safely introspect errors:

```json
{
  "error": "validation_error",
  "field": "speed",
  "message": "Must be between 1 and 255"
}
```

| Status | Usage | Notes |
| --- | --- | --- |
| `200` | Successful reads and updates | Responses include the full resource payload. |
| `201` | Successful creates | Returns the created segment object. |
| `400` | Validation or JSON parsing errors | `field` points to the offending attribute when applicable. |
| `404` | Unknown segment IDs | Returned when a referenced segment does not exist. |
| `413` | Payload too large | Body exceeded `MAX_REQUEST_BODY_SIZE` (16KB). |
| `500` | Internal creation failures | Rare; indicates controller could not allocate a segment. |

Authentication failures continue to return `401` via the shared `sendUnauthorized()` helper.

---

## Payload Schemas & Examples

### Controller Payload

| Field | Type / Range | Required | Notes |
| --- | --- | --- | --- |
| `power` | `bool` | optional | `true` enables LED output. |
| `brightness` | `uint8 (0-255)` | optional | Global brightness applied across all segments. |
| `ledCount` | `uint16 (1-1024)` | read-only | Returned by GET responses; not writable via API. |

**Response shape (GET / PUT):**
```json
{
  "power": true,
  "brightness": 128,
  "ledCount": 160
}
```

### Segment Payload

| Field | Type / Range | Required (POST) | Notes |
| --- | --- | --- | --- |
| `id` | `uint8 (0-7)` | auto | Assigned sequentially, returned in responses. |
| `start` | `uint16` | ✅ | Starting LED index. |
| `length` | `uint16` | ✅ | Number of LEDs in the segment. |
| `stop` | `uint16` | auto | Derived from `start + length - 1` in responses. |
| `effect` | `string` | optional | Must match an ID from `/api/v2/effects`. |
| `speed` | `uint8 (1-255)` | optional | Animation speed scalar. |
| `intensity` | `uint8 (0-255)` | optional | Effect-specific secondary scalar. |
| `primaryColor` | `[uint8,uint8,uint8]` | optional | RGB triplet. |
| `secondaryColor` | `[uint8,uint8,uint8]` | optional | RGB triplet. |
| `palette` | `uint8 (0-6)` | optional | Palette preset index; omitted in responses due to storage limitation. |
| `reverse` | `bool` | optional | Only honored at creation time. |

**Segment response shape (GET / POST / PUT):**
```json
{
  "id": 0,
  "start": 0,
  "stop": 79,
  "length": 80,
  "effect": "fire",
  "speed": 180,
  "intensity": 150,
  "primaryColor": [255, 80, 0],
  "secondaryColor": [255, 0, 0],
  "reverse": false
}
```

---

## Controller Endpoints

### GET /api/v2/controller

Get controller-level state (power, brightness, LED count).

**Response:**
```json
{
  "power": true,
  "brightness": 200,
  "ledCount": 160
}
```

### PUT /api/v2/controller

Update controller-level state.

**Request:**
```json
{
  "power": true,
  "brightness": 200
}
```

**Response:** Same as GET - returns updated state.

---

## Segment Endpoints

### GET /api/v2/segments

List all segments with controller state.

**Response:**
```json
{
  "power": true,
  "brightness": 128,
  "ledCount": 160,
  "segments": [
    {
      "id": 0,
      "start": 0,
      "stop": 159,
      "length": 160,
      "effect": "rainbow",
      "speed": 128,
      "intensity": 128,
      "primaryColor": [0, 0, 255],
      "secondaryColor": [128, 0, 128],
      "reverse": false
    }
  ]
}
```

**Notes:**
- `stop` is calculated as `start + length - 1` (inclusive end position)
- `palette` field is omitted (see limitations below)

### POST /api/v2/segments

Create a new segment.

**Request:**
```json
{
  "start": 0,
  "length": 80,
  "effect": "fire",
  "speed": 180,
  "intensity": 150,
  "primaryColor": [255, 80, 0],
  "secondaryColor": [255, 0, 0],
  "palette": 1,
  "reverse": false
}
```

**Required fields:**
- `start` (uint16) - Starting LED index
- `length` (uint16) - Number of LEDs in segment

**Optional fields:**
- `effect` (string) - Effect ID (default: none)
- `speed` (uint8, 1-255) - Animation speed
- `intensity` (uint8, 0-255) - Effect intensity
- `primaryColor` (array [r,g,b]) - Primary color
- `secondaryColor` (array [r,g,b]) - Secondary color
- `palette` (int 0-6) - Palette preset (0=Rainbow, 1=Lava, 2=Ocean, 3=Party, 4=Forest, 5=Cloud, 6=Heat)
- `reverse` (bool) - Reverse LED direction (set at creation only)

**Response:** Returns created segment object with assigned `id`.

### GET /api/v2/segments/{id}

Get a specific segment by ID.

**Response:**
```json
{
  "id": 0,
  "start": 0,
  "stop": 79,
  "length": 80,
  "effect": "fire",
  "speed": 180,
  "intensity": 150,
  "primaryColor": [255, 80, 0],
  "secondaryColor": [255, 0, 0],
  "reverse": false
}
```

### PUT /api/v2/segments/{id}

Update an existing segment.

**Partial update example:**
```bash
curl -X PUT http://lume.local/api/v2/segments/0 \
  -H "Content-Type: application/json" \
  -d '{
    "effect": "rainbow",
    "speed": 120
  }'
```

**Behavior:**
- `primaryColor`, `secondaryColor`, `palette`, and any omitted numeric fields remain unchanged.
- `reverse` cannot be updated (creation-time only); supplying it on PUT is ignored.
- Response returns the full segment object reflecting the new values.

**Response:** Returns updated segment object (see schema above).

### DELETE /api/v2/segments/{id}

Delete a segment by ID.

---

## Metadata Endpoints

### GET /api/v2/effects

List all available effects with metadata.

**Response:**
```json
{
  "effects": [
    {
      "id": "fire",
      "name": "Fire",
      "category": 1,
      "usesPalette": true,
      "usesPrimaryColor": false,
      "usesSecondaryColor": false,
      "usesSpeed": true,
      "usesIntensity": true
    },
    {
      "id": "gradient",
      "name": "Gradient",
      "category": 1,
      "usesPalette": false,
      "usesPrimaryColor": true,
      "usesSecondaryColor": true,
      "usesSpeed": true,
      "usesIntensity": false
    }
  ]
}
```

**Categories:**
- `0` = Solid (static, no animation)
- `1` = Animated (motion/animation)
- `2` = Moving (positional movement)
- `3` = Special (complex or unique)

**Parameter Flags:**
- `usesPalette` - Effect responds to palette changes
- `usesPrimaryColor` - Effect uses primary color picker
- `usesSecondaryColor` - Effect uses secondary color picker
- `usesSpeed` - Effect responds to speed parameter
- `usesIntensity` - Effect responds to intensity parameter

These flags enable dynamic UI - only showing controls that are relevant for each effect.

### GET /api/v2/palettes

List all available color palettes.

**Response:**
```json
{
  "palettes": [
    {"id": 0, "name": "Rainbow"},
    {"id": 1, "name": "Lava"},
    {"id": 2, "name": "Ocean"},
    {"id": 3, "name": "Party"},
    {"id": 4, "name": "Forest"},
    {"id": 5, "name": "Cloud"},
    {"id": 6, "name": "Heat"}
  ]
}
```

### GET /api/v2/info

Lightweight metadata endpoint for UIs to discover firmware details and capability limits.

**Response:**
```json
{
  "firmware": {
    "name": "LUME",
    "version": "1.0.0",
    "buildHash": "dev",
    "buildTimestamp": "2025-12-30 18:42:10"
  },
  "limits": {
    "maxLeds": 300,
    "maxSegments": 8,
    "maxRequestBody": 16384
  },
  "features": {
    "segmentsV2": true,
    "directPixels": true,
    "sacn": true,
    "mqtt": true,
    "aiPrompts": true,
    "ota": true
  },
  "controller": {
    "ledCount": 160,
    "power": true
  }
}
```

---

## Known Limitations

### 1. Palette Retrieval

**Issue:** Segments store converted `CRGBPalette16` objects, not the `PalettePreset` enum value that was set.

**Impact:**
- ✅ Can SET palette via preset ID
- ❌ Cannot GET which preset is currently active

**Workaround:** Track palette preset client-side if needed.

**Future Fix:** Add palette preset tracking field to Segment class.

### 2. Reverse Flag Immutable

**Issue:** The `reverse` flag can only be set during segment creation via `createSegment(start, length, reversed)`.

**Impact:**
- ✅ Can set reverse during creation
- ❌ Cannot change reverse after creation

**Workaround:** Delete and recreate segment with new reverse value.

**Architectural:** By design - reverse is part of the SegmentView setup.

---

## System & Status Endpoints

### GET /health

Quick health check endpoint.

**Response:**
```json
{
  "status": "ok",
  "uptime": 12345,
  "freeHeap": 234567,
  "wifiRSSI": -45
}
```

### GET /api/status

Full system status.

**Response:**
```json
{
  "online": true,
  "ip": "192.168.1.100",
  "uptime": 12345,
  "wifi": {
    "ssid": "MyNetwork",
    "rssi": -45,
    "connected": true
  },
  "led": {
    "count": 160,
    "power": true,
    "brightness": 200,
    "fps": 60
  },
  "protocols": {
    "sacn": {"enabled": false},
    "mqtt": {"enabled": true, "connected": true}
  }
}
```

### GET /api/config

Get device configuration (passwords/API keys masked).

**Response:**
```json
{
  "wifiSSID": "MyNetwork",
  "ledCount": 160,
  "aiApiKey": "****",
  "aiApiKeySet": true,
  "aiModel": "claude-3-5-sonnet-20241022",
  "sacnEnabled": false,
  "mqttEnabled": true,
  "mqttBroker": "192.168.1.10",
  "mqttPort": 1883
}
```

### POST /api/config

Update device configuration.

**Request:**
```json
{
  "wifiSSID": "NewNetwork",
  "wifiPassword": "newpass",
  "ledCount": 160,
  "aiApiKey": "sk-ant-...",
  "aiModel": "claude-3-5-sonnet-20241022",
  "sacnEnabled": true,
  "mqttEnabled": true,
  "mqttBroker": "192.168.1.10"
}
```

**Notes:**
- Omit password fields to leave unchanged
- Device restarts after WiFi changes

### POST /api/pixels

Direct pixel control (bypasses effects).

**Request:**
```json
{
  "pixels": [
    [255, 0, 0],
    [0, 255, 0],
    [0, 0, 255]
  ]
}
```

**Notes:**
- Array of [r,g,b] triplets
- Maps directly to LED positions
- Overrides active effects until next effect update

---

## AI & Automation Endpoints

### POST /api/prompt

Send natural language prompt to AI for LED control.

**Request:**
```json
{
  "prompt": "cozy warm fireplace"
}
```

**Response:**
```json
{
  "success": true,
  "message": "Lights updated successfully!",
  "spec": {
    "effect": "fire",
    "speed": 180,
    "intensity": 200,
    "primaryColor": [255, 80, 0],
    "secondaryColor": [255, 40, 0]
  }
}
```

**Notes:**
- Requires AI API key configured in settings
- Uses Anthropic Claude API
- Automatically selects best effect and colors from natural language
- Applied to segment 0

### GET /api/nightlight

Get nightlight status.

**Response:**
```json
{
  "active": false,
  "progress": 0.0
}
```

**Response (active):**
```json
{
  "active": true,
  "progress": 45.5
}
```

### POST /api/nightlight

Start nightlight fade timer.

**Request:**
```json
{
  "duration": 900,
  "targetBrightness": 0
}
```

**Parameters:**
- `duration` (uint16, 1-3600) - Fade duration in seconds
- `targetBrightness` (uint8, 0-255) - Target brightness (0 = turn off)

**Response:**
```json
{
  "success": true,
  "duration": 900,
  "targetBrightness": 0,
  "startBrightness": 200
}
```

### POST /api/nightlight/stop

Cancel active nightlight.

**Response:**
```json
{
  "success": true
}
```

---

## Examples

### Create Two Segments with Different Effects

```bash
# First half - fire effect
curl -X POST http://lume.local/api/v2/segments \
  -H "Content-Type: application/json" \
  -d '{
    "start": 0,
    "length": 80,
    "effect": "fire",
    "speed": 200,
    "primaryColor": [255, 60, 0]
  }'

# Second half - rainbow effect  
curl -X POST http://lume.local/api/v2/segments \
  -H "Content-Type: application/json" \
  -d '{
    "start": 80,
    "length": 80,
    "effect": "rainbow",
    "speed": 150
  }'
```

### Update Controller Brightness

```bash
curl -X PUT http://lume.local/api/v2/controller \
  -H "Content-Type: application/json" \
  -d '{"brightness": 200}'
```

### List All Available Effects

```bash
curl http://lume.local/api/v2/effects | jq '.effects[] | .name'
```

---

## Implementation Notes

- All JSON uses ArduinoJson library
- Request bodies limited to 16KB (`MAX_REQUEST_BODY_SIZE`)
- Async body handling with chunk accumulation
- All endpoints return JSON (except 404s from routing issues)
- Segment IDs are stable (0-7, assigned sequentially)
- Non-overlapping segments enforced by controller
- Color arrays are `[r, g, b]` format, values 0-255

**Routing Implementation:**

ESPAsyncWebServer has **very limited regex support**. Patterns like `^\\/api\\/v2\\/segments\\/([0-9]+)$` don't work as expected.

**Solution:** Manual path inspection using lambda handlers:

```cpp
// GET - handles both /api/v2/segments and /api/v2/segments/{id}
server.on("/api/v2/segments", HTTP_GET, [](AsyncWebServerRequest* request) {
    String path = request->url();
    if (path.startsWith("/api/v2/segments/") && path.length() > 17) {
        handleApiV2SegmentGet(request);  // Has ID
    } else {
        handleApiV2SegmentsList(request);  // No ID
    }
});

// PUT - validates path before delegating to body handler
server.on("/api/v2/segments", HTTP_PUT,
    [](AsyncWebServerRequest* request) {
        String path = request->url();
        if (!path.startsWith("/api/v2/segments/") || path.length() <= 17) {
            request->send(400, "application/json", "{\"error\":\"Segment ID required\"}");
        }
    },
    NULL,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        String path = request->url();
        if (path.startsWith("/api/v2/segments/") && path.length() > 17) {
            handleApiV2SegmentUpdate(request, data, len, index, total);
        }
    }
);
```

The handler functions then extract the ID using `lastIndexOf('/')` and `substring()`.

---

## Migration from v1 API

The v1 API (`/api/led`) remains available for backward compatibility with the existing web UI:

| v1 Endpoint | v2 Equivalent |
|-------------|---------------|
| `GET /api/led` | `GET /api/v2/segments` (segment 0) |
| `POST /api/led` | `PUT /api/v2/segments/0` (when fixed) |
| `GET /api/segments` | `GET /api/v2/segments` |

**Key Differences:**
- v2 supports multiple segments natively
- v2 exposes effect metadata and capabilities
- v2 has controller-level endpoints separate from segments
- v1 uses compatibility layer that translates to segment 0

---

## Status: Stable

✅ **Working:**
- Controller state management
- Listing all segments
- Creating new segments
- Getting individual segments by ID
- Updating segments by ID
- Deleting segments by ID
- Effects and palettes metadata

⚠️ **Known Limitations:**
- Palette preset retrieval (architectural - stores converted palettes)
- Reverse flag immutable after creation (by design)
