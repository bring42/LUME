/**
 * nightlight.cpp - Nightlight API implementation
 */

#include "nightlight.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../core/controller.h"
#include <ArduinoJson.h>

// Request body buffer for async handling
static String nightlightBodyBuffer;

void handleApiNightlightGet(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["active"] = lume::controller.isNightlightActive();
    doc["progress"] = lume::controller.getNightlightProgress();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiNightlightPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Body size validation
    if (index == 0) {
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request too large\"}");
            return;
        }
        nightlightBodyBuffer = "";
        nightlightBodyBuffer.reserve(total);
    }
    
    // Accumulate body chunks
    nightlightBodyBuffer += String((char*)data, len);
    
    // Only process when complete
    if (index + len < total) {
        return;
    }
    
    LOG_DEBUG(LogTag::WEB, "Nightlight request: %s", nightlightBodyBuffer.c_str());
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, nightlightBodyBuffer);
    
    if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    
    // Get duration in seconds (default: 15 minutes = 900 seconds)
    uint16_t duration = doc["duration"] | NIGHTLIGHT_DEFAULT_DURATION;
    
    // Validate duration (between 1 second and max)
    if (duration < 1 || duration > NIGHTLIGHT_MAX_DURATION) {
        JsonDocument response;
        response["error"] = "Duration must be between 1 and " + String(NIGHTLIGHT_MAX_DURATION) + " seconds";
        String responseStr;
        serializeJson(response, responseStr);
        request->send(400, "application/json", responseStr);
        return;
    }
    
    // Get target brightness (default: 0 = fade to off)
    uint8_t targetBrightness = doc["targetBrightness"] | NIGHTLIGHT_DEFAULT_TARGET;
    
    // Start nightlight
    lume::controller.startNightlight(duration, targetBrightness);
    
    // Return status
    JsonDocument response;
    response["success"] = true;
    response["duration"] = duration;
    response["targetBrightness"] = targetBrightness;
    response["startBrightness"] = lume::controller.getBrightness();
    
    String responseStr;
    serializeJson(response, responseStr);
    request->send(200, "application/json", responseStr);
    
    LOG_INFO(LogTag::WEB, "Nightlight started: %ds fade to %d", duration, targetBrightness);
}
