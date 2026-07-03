# API Handlers

REST API endpoint implementations for the LUME web interface.

## Structure

Each file implements handlers for a related set of endpoints:

- **config.cpp** - Device configuration (WiFi, LED count, protocols)
- **segments.cpp** - Segment CRUD + the `effects`/`palettes`/`info` metadata endpoints
- **pixels.cpp** - Direct pixel manipulation (debug overlay)
- **status.cpp** - System status and diagnostics
- **nightlight.cpp** - Nightlight timer functionality
- **prompt.cpp** - AI prompt processing (worker task, returns 202)

## Handler Pattern

Body handlers claim the single-owner body guard (`beginBody`/`endBody`, P0.3), accumulate
byte-wise, then **enqueue a command and reply `202`** — no segment/LED mutation on the web task:

```cpp
static String bodyBuffer;

void handleEndpointPost(AsyncWebServerRequest* request, uint8_t* data,
                        size_t len, size_t index, size_t total) {
    if (index == 0 && !checkAuth(request)) { sendUnauthorized(request); return; }

    if (index == 0) {
        if (!beginBody(request)) {                       // another body mid-assembly
            request->send(409, "application/json", "{\"error\":\"Busy, retry\"}");
            return;
        }
        bodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) { endBody(request); /* 413 */ return; }
    }

    for (size_t i = 0; i < len; i++) bodyBuffer += (char)data[i];

    if (index + len >= total) {                          // body complete
        endBody(request);
        // parse JSON, enqueue a Command, then:
        request->send(202, "application/json", "{\"status\":\"accepted\"}");
    }
}
```

Reads (`GET`) are synchronous and return `200` with the live payload.

## Registration

Handlers are registered in [src/network/server.cpp](../network/server.cpp) with route configuration.

## Data Flow

```
HTTP mutation → Handler → enqueueCommand() → [bus] → render loop → Segment → Effect
              → 202 accepted (client reconciles via WebSocket push / GET)
```
