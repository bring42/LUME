# API Handlers

REST API endpoint implementations for the LUME web interface.

## Structure

Each file implements handlers for a related set of endpoints:

- **config.cpp** - Device configuration (WiFi, LED count, etc.)
- **segments.cpp** - Segment CRUD operations
- **pixels.cpp** - Direct pixel manipulation
- **status.cpp** - System status and diagnostics
- **nightlight.cpp** - Nightlight timer functionality
- **prompt.cpp** - AI prompt processing (legacy)

## Handler Pattern

All handlers follow this pattern for POST requests with body data:

```cpp
static String bodyBuffer;

void handleEndpointPost(AsyncWebServerRequest* request, uint8_t* data, 
                        size_t len, size_t index, size_t total) {
    // Auth check on first chunk
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Accumulate body
    if (index == 0) bodyBuffer = "";
    bodyBuffer.concat((const char*)data, len);
    
    // Process when complete
    if (index + len == total) {
        // Parse JSON and handle request
        sendJsonResponse(request, 200, response);
    }
}
```

## Registration

Handlers are registered in [src/network/server.cpp](../network/server.cpp) with route configuration.

## Data Flow

```
HTTP Request → Handler → controller.enqueueCommand() → Segment → Effect
```
