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
    // "Nightlight" is a name that lives here, at the API boundary. In the core
    // it is just an eased brightness fade, so its status maps onto the generic
    // brightness-fade state.
    JsonDocument doc;
    doc["active"] = lume::controller.isBrightnessFading();
    doc["progress"] = lume::controller.brightnessFadeProgress();

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
        if (!beginBody(request)) {  // P0.3: one body at a time
            request->send(409, "application/json", "{\"error\":\"Busy, retry\"}");
            return;
        }
        if (total > MAX_REQUEST_BODY_SIZE) {
            endBody(request);
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
    endBody(request);   // P0.3: body fully assembled; release the slot

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

    // Compose "nightlight" from the generic primitives: an eased brightness fade
    // over the requested duration, plus the power-off-at-zero rider. No bespoke
    // nightlight command or controller state — the name stops at this boundary.
    lume::controller.enqueueCommand(
        lume::Command::setGlobalBrightness(targetBrightness)
            .withTransition((uint32_t)duration * 1000u)
            .withPowerOffAtZero(targetBrightness == 0));

    request->send(202, "application/json", "{\"status\":\"accepted\"}");

    LOG_INFO(LogTag::WEB, "Nightlight start enqueued: %ds fade to %d", duration, targetBrightness);
}
