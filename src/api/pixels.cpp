#include "pixels.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../lume.h"

// Static body buffer for async request handling
static String pixelsBodyBuffer;

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
        pixelsBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    // Append data safely
    for (size_t i = 0; i < len; i++) {
        pixelsBodyBuffer += (char)data[i];
    }
    
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, pixelsBodyBuffer);
        
        if (err) {
            LOG_WARN(LogTag::WEB, "Pixels JSON parse error: %s", err.c_str());
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        CRGB* leds = lume::controller.getLeds();
        uint16_t ledCount = lume::controller.getLedCount();
        
        // Handle brightness if provided
        if (doc["brightness"].is<int>()) {
            lume::controller.setBrightness(constrain(doc["brightness"].as<int>(), 0, 255));
        }
        
        // Method 1: Array of [r,g,b] arrays
        if (doc["pixels"].is<JsonArray>()) {
            JsonArray pixels = doc["pixels"].as<JsonArray>();
            uint16_t count = min((uint16_t)pixels.size(), ledCount);
            
            for (uint16_t i = 0; i < count; i++) {
                JsonArray pixel = pixels[i].as<JsonArray>();
                if (pixel.size() >= 3) {
                    leds[i].r = pixel[0].as<uint8_t>();
                    leds[i].g = pixel[1].as<uint8_t>();
                    leds[i].b = pixel[2].as<uint8_t>();
                }
            }
            
            FastLED.show();
            
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
                leds[i].r = rgb[i * 3].as<uint8_t>();
                leds[i].g = rgb[i * 3 + 1].as<uint8_t>();
                leds[i].b = rgb[i * 3 + 2].as<uint8_t>();
            }
            
            FastLED.show();
            
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
            fill_solid(leds, ledCount, color);
            FastLED.show();
            
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
            
            fill_gradient_RGB(leds, 0, startColor, ledCount - 1, endColor);
            FastLED.show();
            
            request->send(200, "application/json", "{\"success\":true,\"gradient\":true}");
            return;
        }
        
        request->send(400, "application/json", "{\"error\":\"No valid pixel data. Use 'pixels', 'rgb', 'fill', or 'gradient'\"}");
    }
}
