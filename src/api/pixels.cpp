#include "pixels.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../lume.h"
#include <memory>

// Static body buffer for async request handling
static String pixelsBodyBuffer;

// GET /api/v2/pixels — live viz readback. Returns the strip's current
// perceptual output (see LumeController::copyVizPixels for exactly what that
// means and why it's NOT leds[]) as {"count":N,"rgb":"rrggbb..."} — one hex
// triplet per pixel, physical strip order.
//
// Auth-gated like every other /api/v2 read (segments list/get, info,
// controller). It shipped unauthenticated on the claim that it "matches the
// other GETs" — but the unauthenticated ones are the v1 routes (/api/status,
// /api/config, /api/nightlight) plus effects/palettes. On a device with an
// authToken set this was the one v2 read leaking live state (the strip's
// output at ~10 Hz) to anyone on the LAN. checkAuth is a pass-through when no
// token is configured, so the default setup is unaffected.
//
// The web UI polls this while its channel viz is visible, so it stays
// allocation-light. Peak transient at MAX_LED_COUNT (1000): a 3 KB pixel
// frame + a ~6 KB reserved String + the ~6 KB copy AsyncWebServer makes of
// that String in request->send() ≈ 15 KB, plus TCP buffers. Comfortable
// against the C3's ~93 KB free heap, and only ~2.5 KB at the 160-LED default.
void handleApiPixelsGet(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    uint16_t ledCount = lume::controller.getLedCount();
    std::unique_ptr<uint8_t[]> px(new uint8_t[ledCount > 0 ? (size_t)ledCount * 3 : 1]);
    uint16_t n = lume::controller.copyVizPixels(px.get(), ledCount);

    String out;
    out.reserve((size_t)n * 6 + 32);
    out += "{\"count\":";
    out += n;
    out += ",\"rgb\":\"";
    static const char HEX_CHARS[] = "0123456789abcdef";
    for (size_t i = 0; i < (size_t)n * 3; i++) {
        out += HEX_CHARS[px[i] >> 4];
        out += HEX_CHARS[px[i] & 0x0F];
    }
    out += "\"}";
    request->send(200, "application/json", out);
}

// Direct pixel control handler
// Accepts: { "pixels": [[r,g,b], [r,g,b], ...], "brightness": 255 }
// Or compact: { "rgb": [r,g,b,r,g,b,...], "brightness": 255 }
void handleApiPixels(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        if (!beginBody(request)) {  // P0.3: one body at a time
            request->send(409, "application/json", "{\"error\":\"Busy, retry\"}");
            return;
        }
        pixelsBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            endBody(request);
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }

    // Append data safely
    for (size_t i = 0; i < len; i++) {
        pixelsBodyBuffer += (char)data[i];
    }

    if (index + len >= total) {
        endBody(request);   // P0.3: body fully assembled; release the slot
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, pixelsBodyBuffer);
        
        if (err) {
            LOG_WARN(LogTag::WEB, "Pixels JSON parse error: %s", err.c_str());
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        uint16_t ledCount = lume::controller.getLedCount();

        // Brightness routes through the bus (single writer), not a direct call.
        if (doc["brightness"].is<int>()) {
            lume::controller.enqueueCommand(
                lume::Command::setGlobalBrightness(constrain(doc["brightness"].as<int>(), 0, 255)));
        }

        if (ledCount == 0) {
            request->send(200, "application/json", "{\"success\":true}");
            return;
        }

        // Build the frame locally, then hand it to the render loop to display —
        // this (AsyncTCP) task never touches leds[]/FastLED directly, so it no
        // longer races the loop's writes/show() (P0.1). Heap-allocated: a full
        // MAX_LED_COUNT frame is too large for the async task stack. The staged
        // frame is a one-frame overlay; unset pixels are black, and segments
        // resume the next frame (a behavior change vs the old sticky writes,
        // acceptable for this debug endpoint).
        std::unique_ptr<CRGB[]> frame(new CRGB[ledCount]);
        memset(frame.get(), 0, ledCount * sizeof(CRGB));

        // Method 1: Array of [r,g,b] arrays
        if (doc["pixels"].is<JsonArray>()) {
            JsonArray pixels = doc["pixels"].as<JsonArray>();
            uint16_t count = min((uint16_t)pixels.size(), ledCount);
            for (uint16_t i = 0; i < count; i++) {
                JsonArray pixel = pixels[i].as<JsonArray>();
                if (pixel.size() >= 3) {
                    frame[i].r = pixel[0].as<uint8_t>();
                    frame[i].g = pixel[1].as<uint8_t>();
                    frame[i].b = pixel[2].as<uint8_t>();
                }
            }
            lume::controller.stageDirectPixels(frame.get(), ledCount);

            JsonDocument response;
            response["success"] = true;
            response["pixelsSet"] = count;
            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            return;
        }

        // Method 2: Flat array [r,g,b,r,g,b,...]
        if (doc["rgb"].is<JsonArray>()) {
            JsonArray rgb = doc["rgb"].as<JsonArray>();
            uint16_t count = min((uint16_t)(rgb.size() / 3), ledCount);
            for (uint16_t i = 0; i < count; i++) {
                frame[i].r = rgb[i * 3].as<uint8_t>();
                frame[i].g = rgb[i * 3 + 1].as<uint8_t>();
                frame[i].b = rgb[i * 3 + 2].as<uint8_t>();
            }
            lume::controller.stageDirectPixels(frame.get(), ledCount);

            JsonDocument response;
            response["success"] = true;
            response["pixelsSet"] = count;
            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            return;
        }

        // Method 3: Fill all with single color
        if (doc["fill"].is<JsonArray>()) {
            JsonArray fill = doc["fill"].as<JsonArray>();
            if (!validateRgbArray(fill)) {
                request->send(400, "application/json", "{\"error\":\"Fill requires array of [r,g,b] with 3 integer values (0-255)\"}");
                return;
            }
            CRGB color(fill[0].as<uint8_t>(), fill[1].as<uint8_t>(), fill[2].as<uint8_t>());
            fill_solid(frame.get(), ledCount, color);
            lume::controller.stageDirectPixels(frame.get(), ledCount);

            request->send(200, "application/json", "{\"success\":true,\"filled\":true}");
            return;
        }

        // Method 4: Gradient between two colors
        if (doc["gradient"].is<JsonObject>()) {
            JsonObject grad = doc["gradient"].as<JsonObject>();
            JsonArray from = grad["from"].as<JsonArray>();
            JsonArray to = grad["to"].as<JsonArray>();

            if (!validateRgbArray(from) || !validateRgbArray(to)) {
                request->send(400, "application/json", "{\"error\":\"Gradient requires 'from' and 'to' with [r,g,b] arrays\"}");
                return;
            }

            CRGB startColor(from[0].as<uint8_t>(), from[1].as<uint8_t>(), from[2].as<uint8_t>());
            CRGB endColor(to[0].as<uint8_t>(), to[1].as<uint8_t>(), to[2].as<uint8_t>());
            // ledCount > 0 guaranteed above (P0.2 guard preserved).
            fill_gradient_RGB(frame.get(), 0, startColor, ledCount - 1, endColor);
            lume::controller.stageDirectPixels(frame.get(), ledCount);

            request->send(200, "application/json", "{\"success\":true,\"gradient\":true}");
            return;
        }

        request->send(400, "application/json", "{\"error\":\"No valid pixel data. Use 'pixels', 'rgb', 'fill', or 'gradient'\"}");
    }
}
