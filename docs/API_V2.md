# LUME v2 API Documentation

**Base URL:** `http://lume.local` (or device IP address)

The v2 API provides multi-segment LED control with full access to the segment-based
architecture. Effect parameters are **schema-driven and per-effect** — discovered at
runtime from `GET /api/v2/effects` — not a fixed set of top-level scalars.

## Authentication

All endpoints support optional token-based authentication:
- Header: `Authorization: Bearer YOUR_TOKEN`
- Header: `X-API-Key: YOUR_TOKEN`
- Query param: `?token=YOUR_TOKEN`

---

## Write model: asynchronous, single-writer

LUME runs a **single-writer command bus** (see the [command-bus RFC](rfcs/0001-command-bus.md)).
Every mutating request — segment `POST`/`PUT`/`DELETE`, controller `PUT`, `POST /api/pixels`,
and `POST /api/prompt` — does **not** apply on the web task. It validates the body, enqueues a
typed command, and returns immediately:

```
HTTP/1.1 202 Accepted
Content-Type: application/json

{"status":"accepted"}
```

The render loop drains the queue and applies the command on the **next frame** (single writer,
no locks, no cross-task races). Consequences for clients:

- **The 202 body carries no resulting state.** Do not parse the mutated resource out of the
  response — there isn't one. Reconcile by reading the state the loop actually applied:
  either the **WebSocket state push** (below) or a follow-up `GET`.
- Reads (`GET`) are synchronous and return the live state directly with `200`.

### WebSocket state push

Connect to `ws://lume.local/ws`. On connect, and after every applied mutation, the loop
broadcasts the full UI state:

```json
{
  "type": "state",
  "controller": { "power": true, "brightness": 128, "ledCount": 160 },
  "segments": [ /* canonical segment objects — see below */ ]
}
```

This is the authoritative post-apply state and uses the **same canonical segment shape** as
the REST endpoints (one serializer backs both — TECH_DEBT P1.7).

---

## Update semantics (read this before sending `params`)

A `PUT /api/v2/segments/{id}` is **not** a field-level merge for effect parameters.

- **Top-level fields** you omit (`effect`, `palette`, `brightness`) keep their prior value.
- **The `params` object is applied whole against the effect's schema.** When a request
  includes a `params` object, the handler first loads that effect's **schema defaults**, then
  overlays the keys you sent. **Any param you omit from `params` is reset to its schema
  default — not left at its prior value.** (Confirmed on hardware.)
- If you also change `effect` in the same request, `params` resolve against the **new**
  effect's schema (and its defaults).
- Omitting the `params` object entirely leaves the segment's existing param values untouched.

**Rule of thumb:** to tweak one parameter, send the *complete* `params` object for that effect
(all the keys you care about keeping), not just the one you're changing. Read the current values
from `GET /api/v2/segments/{id}` first if you need to preserve them.

Unknown param keys, wrong-typed values, and malformed colors are ignored defensively; integer
params are clamped to their schema `[min, max]`.

---

## Error Handling

Mutations that pass validation return `202 Accepted` (see above). Failures return JSON so
clients can introspect:

```json
{
  "error": "validation_error",
  "field": "start",
  "message": "Fields 'start' and 'length' are required"
}
```

| Status | Usage | Notes |
| --- | --- | --- |
| `200` | Successful reads (`GET`) | Returns the live resource payload. |
| `202` | Accepted mutation | Body is `{"status":"accepted"}`; the loop applies it next frame. |
| `400` | Validation / JSON parse errors | `field` points to the offending attribute when applicable. |
| `404` | Unknown segment ID | Referenced segment does not exist. |
| `409` | Busy / capacity | Another request body is mid-assembly (`beginBody` guard, P0.3), max segments reached, or an AI request is already in flight. |
| `413` | Payload too large | Body exceeded `MAX_REQUEST_BODY_SIZE` (16 KB). |
| `429` | Rate limited | `POST /api/prompt` only (min gap `PROMPT_RATE_LIMIT_MS`). |

Authentication failures return `401` via the shared `sendUnauthorized()` helper.

---

## Payload Schemas & Examples

### Controller Payload

| Field | Type / Range | Writable | Notes |
| --- | --- | --- | --- |
| `power` | `bool` | ✅ (PUT) | `true` enables LED output. |
| `brightness` | `uint8 (0-255)` | ✅ (PUT) | Global brightness applied across all segments. |
| `ledCount` | `uint16` | read-only | Returned by GET; changed via `POST /api/config`, applied on reboot. |

**Response shape (GET):**
```json
{ "power": true, "brightness": 128, "ledCount": 160 }
```

### Segment Payload — the canonical shape

One serializer produces the segment shape everywhere (v2 REST **and** the WebSocket push).

| Field | Type | Notes |
| --- | --- | --- |
| `id` | `uint8 (0-7)` | Assigned sequentially, stable across the segment's life. |
| `start` | `uint16` | Starting LED index. |
| `stop` | `uint16` | Inclusive last index (`start + length - 1`). Response-only (derived). |
| `length` | `uint16` | Number of LEDs in the segment. |
| `reverse` | `bool` | Run the effect reversed. **Creation-time only** (ignored on PUT). |
| `brightness` | `uint8 (0-255)` | Per-segment brightness. |
| `effect` | `string` | Effect ID; must match an `id` from `GET /api/v2/effects`. |
| `params` | `object` | Per-effect parameters keyed by param `id` (see below). Colors are `#rrggbb` hex. |

**Canonical segment object (GET / WebSocket):**
```json
{
  "id": 0,
  "start": 0,
  "stop": 79,
  "length": 80,
  "reverse": false,
  "brightness": 255,
  "effect": "fire",
  "params": {
    "cooling": 55,
    "sparking": 120,
    "reversed": false
  }
}
```

The `params` keys, their types, defaults, and ranges are **defined by the effect**, not by
this API. Fetch them from `GET /api/v2/effects` (each effect declares a `params` array). A few
examples of what a given effect exposes:

- `gradient` → `colorStart`, `colorEnd` (both `color`, `#rrggbb` hex)
- `fire` → `cooling` (int), `sparking` (int), `reversed` (bool)
- `wave` → a `direction` **enum** plus speed/color params
- palette effects → a `palette` param (type `palette`)

**Color params are hex strings** (`"#rrggbb`") on both input and output. (The WebSocket used to
emit `[r,g,b]` arrays; the bundled web UI still accepts either form, but hex is what the device
emits and expects.)

> **Palette caveat:** a segment's palette is set via a top-level `palette` integer (a preset
> index, see `GET /api/v2/palettes`), not inside `params`. The device stores the resolved
> `CRGBPalette16`, so the active preset index is **not** echoed back in GET/state. Track it
> client-side if you need it.

---

## Controller Endpoints

### GET /api/v2/controller

Get controller-level state (power, brightness, LED count).

```json
{ "power": true, "brightness": 200, "ledCount": 160 }
```

### PUT /api/v2/controller

Update controller-level state. Only `power` and `brightness` are honored.

**Request:**
```json
{ "power": true, "brightness": 200 }
```

**Response:** `202 {"status":"accepted"}`. Read back via `GET` or the WebSocket push.

---

## Segment Endpoints

### GET /api/v2/segments

List all segments with controller state. Segments are enumerated by index, so survivors of a
middle delete are never dropped (P0.5).

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
      "reverse": false,
      "brightness": 255,
      "effect": "rainbow",
      "params": { "speed": 128 }
    }
  ]
}
```

### POST /api/v2/segments

Create a new segment.

**Request:**
```json
{
  "start": 0,
  "length": 80,
  "reverse": false,
  "effect": "fire",
  "palette": 1,
  "params": { "cooling": 55, "sparking": 120 }
}
```

**Required:** `start` (uint16), `length` (uint16).
**Optional:** `reverse` (bool, creation-only), `effect` (string), `palette` (int preset index),
`params` (object; keys resolved against the effect's schema — omitted keys take the schema
default).

**Response:** `202 {"status":"accepted"}`. The new segment (with its assigned `id`) appears in
the next WebSocket push and in `GET /api/v2/segments`.

### GET /api/v2/segments/{id}

Get a single segment by ID (canonical shape above). `404` if it doesn't exist; `400` if `id > 7`.

### PUT /api/v2/segments/{id}

Update an existing segment.

```bash
curl -X PUT http://lume.local/api/v2/segments/0 \
  -H "Content-Type: application/json" \
  -d '{ "effect": "rainbow", "params": { "speed": 120 } }'
```

**Behavior:**
- `effect`, `palette`, and `brightness` you omit keep their prior value.
- **`params` is applied whole against the effect schema — omitted params reset to their
  defaults** (see [Update semantics](#update-semantics-read-this-before-sending-params)).
- `reverse` is creation-time only; supplying it on PUT is ignored.

**Response:** `202 {"status":"accepted"}`. Reconcile via `GET` / WebSocket.

### DELETE /api/v2/segments/{id}

Delete a segment by ID. **Response:** `202 {"status":"accepted"}`.

---

## Metadata Endpoints

### GET /api/v2/effects

List all available effects with their parameter schemas. **This is the source of truth for
what `params` a segment accepts.**

```json
{
  "effects": [
    {
      "id": "fire",
      "name": "Fire",
      "category": "Animated",
      "dims": "1d",
      "params": [
        { "id": "cooling",  "name": "Cooling",  "type": "int",  "default": 55,  "min": 20, "max": 100 },
        { "id": "sparking", "name": "Sparking", "type": "int",  "default": 120, "min": 50, "max": 200 },
        { "id": "reversed", "name": "Reversed", "type": "bool", "default": false }
      ],
      "usesPalette": false,
      "colorCount": 0,
      "usesSpeed": false,
      "usesIntensity": false
    },
    {
      "id": "gradient",
      "name": "Gradient",
      "category": "Solid",
      "dims": "1d",
      "params": [
        { "id": "colorStart", "name": "Start Color", "type": "color", "default": "#0000ff" },
        { "id": "colorEnd",   "name": "End Color",   "type": "color", "default": "#ff0000" }
      ],
      "usesPalette": false,
      "colorCount": 2,
      "usesSpeed": false,
      "usesIntensity": false
    }
  ]
}
```

**Effect fields:**
- `category` — string: `"Solid"`, `"Animated"`, `"Moving"`, or `"Special"`.
- `dims` — dimensionality (TECH_DEBT P1.2): `"1d"` (strip-only, the default), `"2d"`
  (matrix-only), or `"any"` (remap-safe on either). Inert on a 1D build.
- `params` — array of parameter descriptors. Each has an `id` (the key you use in a segment's
  `params` object), `name`, and a `type` ∈ `int` · `float` · `color` · `bool` · `enum` ·
  `palette`, plus type-specific fields:
  - `int` → `default`, `min`, `max` (0-255)
  - `float` → `default`, `min`, `max`
  - `color` → `default` as `#rrggbb`
  - `bool` → `default` (boolean)
  - `enum` → `default` (index) and `options` (a `"a|b|c"` pipe-delimited string)
  - `palette` → selector; the palette list comes from `GET /api/v2/palettes`
- `usesPalette`, `colorCount`, `usesSpeed`, `usesIntensity` — convenience flags derived from
  the schema, so a UI can lay out controls without inspecting every param.

### GET /api/v2/palettes

Built-in palette presets, generated from the firmware `PalettePreset` enum.
**This endpoint is the source of truth** — fetch the list at runtime rather than
hardcoding names or a count here. Each entry is `{ "id": int, "name": string }`
(example truncated):

```json
{
  "palettes": [
    {"id": 0, "name": "Rainbow"},
    {"id": 1, "name": "Lava"},
    {"id": 2, "name": "Ocean"}
  ]
}
```

### GET /api/v2/info

Lightweight metadata for UIs to discover firmware details and capability limits.

```json
{
  "firmware": {
    "name": "LUME",
    "version": "1.0.0",
    "buildHash": "dev",
    "buildTimestamp": "2026-01-15 18:42:10"
  },
  "limits": {
    "maxLeds": 1000,
    "maxSegments": 8,
    "maxRequestBody": 16384
  },
  "features": {
    "segmentsV2": true,
    "directPixels": true,
    "sacn": false,
    "mqtt": false,
    "aiPrompts": true,
    "ota": true
  },
  "controller": { "ledCount": 160, "power": true }
}
```

`limits.maxLeds` is the compile-time `MAX_LED_COUNT` (default 1000). `features.sacn` and
`features.mqtt` reflect the current config (whether each is enabled), not just build support.

---

## System & Status Endpoints

### GET /health

```json
{ "status": "ok", "uptime": 12345, "freeHeap": 234567, "wifiRSSI": -45 }
```

### GET /api/status

```json
{
  "online": true,
  "ip": "192.168.1.100",
  "uptime": 12345,
  "wifi": { "ssid": "MyNetwork", "rssi": -45, "connected": true },
  "led": { "count": 160, "power": true, "brightness": 200, "fps": 60 },
  "protocols": {
    "sacn": {"enabled": false},
    "mqtt": {"enabled": false, "connected": false}
  }
}
```

### GET /api/config

Get device configuration (passwords/API keys masked).

```json
{
  "wifiSSID": "MyNetwork",
  "ledCount": 160,
  "aiApiKey": "****",
  "aiApiKeySet": true,
  "aiModel": "claude-3-5-sonnet-20241022",
  "sacnEnabled": false,
  "mqttEnabled": false,
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
- Omit password fields to leave unchanged.
- Protocol changes (sACN/MQTT) are re-applied through the bus via a `ReconfigureProtocols`
  command on the render loop — not live from the web task.
- `ledCount` changes are deferred and take effect on reboot (P0.8). WiFi changes restart the
  device.

### POST /api/pixels

Direct pixel control (bypasses effects). Four input forms are accepted; an optional
`brightness` field is honored alongside any of them.

```json
// 1. Array of [r,g,b] triplets (maps to LED positions 0..N)
{ "pixels": [[255,0,0], [0,255,0], [0,0,255]] }

// 2. Flat RGB array
{ "rgb": [255,0,0, 0,255,0, 0,0,255] }

// 3. Fill all LEDs with one color
{ "fill": [255, 0, 0] }

// 4. Gradient between two colors
{ "gradient": { "from": [255,0,0], "to": [0,0,255] } }
```

**Notes:**
- This endpoint stages a **single overlay frame** for the render loop to show; it is **not
  sticky**. Any active segments resume drawing on the next frame (a behavior change from the
  old direct-write path — acceptable for this debug endpoint).
- `pixels`/`rgb` return `{"success":true,"pixelsSet":N}`; `fill`/`gradient` return a success
  flag. Malformed color arrays return `400`.

---

## AI & Automation Endpoints

### POST /api/prompt

Send a natural-language prompt for AI-driven LED control.

**Request:**
```json
{ "prompt": "cozy warm fireplace" }
```

**Response:** `202 {"status":"accepted"}`.

The blocking Anthropic call runs on a dedicated worker task (P0.4), so the request returns
immediately rather than holding the web task for ~30 s. When the AI responds, its effect spec
is applied to **segment 0** through the bus; observe the result via the WebSocket push or
`GET /api/v2/segments/0`.

**Notes:**
- Requires an AI API key configured in settings; otherwise the worker can't run.
- Rate limited: a second prompt within `PROMPT_RATE_LIMIT_MS` (3 s) returns `429`. One AI
  request runs at a time — an overlapping request returns `409`.

### GET /api/nightlight

```json
{ "active": false, "progress": 0.0 }
```

### POST /api/nightlight

Start a nightlight fade timer.

**Request:**
```json
{ "duration": 900, "targetBrightness": 0 }
```

- `duration` (uint16, 1-3600) — fade duration in seconds
- `targetBrightness` (uint8, 0-255) — target brightness (0 = off)

### POST /api/nightlight/stop

Cancel an active nightlight. Returns `{ "success": true }`.

---

## Examples

### Create two segments with different effects

```bash
# First half — fire
curl -X POST http://lume.local/api/v2/segments \
  -H "Content-Type: application/json" \
  -d '{ "start": 0, "length": 80, "effect": "fire",
        "params": { "cooling": 55, "sparking": 120 } }'   # → 202 accepted

# Second half — rainbow
curl -X POST http://lume.local/api/v2/segments \
  -H "Content-Type: application/json" \
  -d '{ "start": 80, "length": 80, "effect": "rainbow",
        "params": { "speed": 150 } }'                     # → 202 accepted
```

### Change one segment's effect

```bash
curl -X PUT http://lume.local/api/v2/segments/0 \
  -H "Content-Type: application/json" \
  -d '{ "effect": "gradient",
        "params": { "colorStart": "#ff6000", "colorEnd": "#000040" } }'
```

### Discover an effect's parameters

```bash
curl http://lume.local/api/v2/effects | jq '.effects[] | select(.id=="fire") | .params'
```

### Update controller brightness

```bash
curl -X PUT http://lume.local/api/v2/controller \
  -H "Content-Type: application/json" \
  -d '{"brightness": 200}'                                # → 202 accepted
```

---

## Implementation Notes

- All JSON uses ArduinoJson.
- Request bodies are limited to 16 KB (`MAX_REQUEST_BODY_SIZE`) and assembled one at a time; a
  concurrent body returns `409` (`beginBody`/`endBody` single-owner guard, P0.3).
- All mutations enqueue a command; the render loop is the sole writer of segment/LED state.
- Segment IDs are stable (0-7, assigned sequentially). Overlapping segments render last-writer-wins.
- Color values are `#rrggbb` hex in effect `params`; the `/api/pixels` debug endpoint takes
  `[r,g,b]` arrays.

**Routing:** ESPAsyncWebServer has very limited regex support, so `/api/v2/segments/{id}` is
handled by manual path inspection in `src/network/server.cpp` (`startsWith` + `substring`)
rather than a route pattern. Handlers extract the ID with `lastIndexOf('/')`.

---

## Migration from the v1 API

The v1 API has been **removed**. The legacy `GET /api/segments` (a richer effect-object
envelope) and the older `/api/led` compatibility routes no longer exist — requests now return
`404`. Use the v2 endpoints below.

| Removed v1 endpoint | Use instead |
|---------------------|-------------|
| `GET /api/led` | `GET /api/v2/segments` (segment 0) |
| `GET /api/segments` | `GET /api/v2/segments` |

**Key differences:** v2 supports multiple segments natively, exposes per-effect parameter
schemas (`GET /api/v2/effects`), and separates controller-level endpoints from segments.

---

## Known Limitations

- **Palette preset retrieval** — a segment stores the resolved `CRGBPalette16`, not the preset
  index that was set, so GET/state can't report the active preset. Track it client-side.
- **Reverse flag immutable** — `reverse` is fixed at creation (part of the `SegmentView` setup);
  delete and recreate to change it.
