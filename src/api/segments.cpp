#include "segments.h"
#include "../main.h"
#include "../storage.h"
#include "../logging.h"
#include "../constants.h"
#include "../core/controller.h"
#include "../core/effect_registry.h"
#include <ArduinoJson.h>

// External globals
extern Config config;
extern bool checkAuth(AsyncWebServerRequest* request);
extern void sendUnauthorized(AsyncWebServerRequest* request);

// Request body buffers
static String segmentCreateBuffer;
static String segmentUpdateBuffer;
static String controllerUpdateBuffer;

namespace {

void sendJsonError(AsyncWebServerRequest* request, int status, const char* code, const char* message, const char* field = nullptr) {
    JsonDocument doc;
    doc["error"] = code;
    doc["message"] = message;
    if (field && field[0] != '\0') {
        doc["field"] = field;
    }

    String output;
    serializeJson(doc, output);
    request->send(status, "application/json", output);
}

}  // namespace

// ===========================================================================
// Helper: Serialize segment to JSON
// ===========================================================================
void segmentToJson(JsonObject& obj, lume::Segment* segment, uint8_t id) {
    obj["id"] = id;
    obj["start"] = segment->getStart();
    obj["stop"] = segment->getStart() + segment->getLength() - 1;  // Calculate stop from start + length
    obj["length"] = segment->getLength();
    obj["effect"] = segment->getEffectId();
    obj["speed"] = segment->getSpeed();
    obj["intensity"] = segment->getIntensity();
    
    // Colors
    CRGB primary = segment->getPrimaryColor();
    CRGB secondary = segment->getSecondaryColor();
    
    JsonArray primaryArr = obj["primaryColor"].to<JsonArray>();
    primaryArr.add(primary.r);
    primaryArr.add(primary.g);
    primaryArr.add(primary.b);
    
    JsonArray secondaryArr = obj["secondaryColor"].to<JsonArray>();
    secondaryArr.add(secondary.r);
    secondaryArr.add(secondary.g);
    secondaryArr.add(secondary.b);
    
    // Note: Palette preset is not included in response because segments store the
    // converted CRGBPalette16, not the PalettePreset enum. Once converted, we can't
    // determine which preset was originally used. Could be fixed by adding preset
    // tracking to Segment class, but current implementation allows custom palettes.
    
    // Reverse flag
    obj["reverse"] = segment->isReversed();
}

// ===========================================================================
// GET /api/v2/segments - List all segments
// ===========================================================================
void handleApiV2SegmentsList(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    JsonDocument doc;
    
    // Controller state
    doc["power"] = lume::controller.getPower();
    doc["brightness"] = lume::controller.getBrightness();
    doc["ledCount"] = lume::controller.getLedCount();
    
    // List all segments
    JsonArray segments = doc["segments"].to<JsonArray>();
    for (uint8_t i = 0; i < 8; i++) {  // Max 8 segments
        lume::Segment* seg = lume::controller.getSegment(i);
        if (seg) {
            JsonObject segObj = segments.add<JsonObject>();
            segmentToJson(segObj, seg, i);
        }
    }
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ===========================================================================
// GET /api/v2/segments/{id} - Get specific segment
// ===========================================================================
void handleApiV2SegmentGet(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Parse segment ID from URL path
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    uint8_t id = path.substring(lastSlash + 1).toInt();
    
    if (id > 7) {
        sendJsonError(request, 400, "validation_error", "Segment ID must be between 0 and 7", "id");
        return;
    }
    
    lume::Segment* seg = lume::controller.getSegment(id);
    if (!seg) {
        sendJsonError(request, 404, "not_found", "Segment not found", "id");
        return;
    }
    
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    segmentToJson(obj, seg, id);
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ===========================================================================
// POST /api/v2/segments - Create new segment
// ===========================================================================
void handleApiV2SegmentCreate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Validate size at first chunk
    if (index == 0) {
        segmentCreateBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            sendJsonError(request, 413, "payload_too_large", "Request body exceeds MAX_REQUEST_BODY_SIZE");
            return;
        }
    }
    
    // Accumulate chunks
    for (size_t i = 0; i < len; i++) {
        segmentCreateBuffer += (char)data[i];
    }
    
    // Process when complete
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, segmentCreateBuffer);
        
        if (error) {
            LOG_ERROR(LogTag::WEB, "JSON parse error: %s", error.c_str());
            sendJsonError(request, 400, "invalid_json", "Unable to parse JSON payload");
            return;
        }
        
        // Validate required fields
        if (!doc["start"].is<int>() || !doc["length"].is<int>()) {
            sendJsonError(request, 400, "validation_error", "Fields 'start' and 'length' are required", "start");
            return;
        }
        
        uint16_t start = doc["start"].as<uint16_t>();
        uint16_t length = doc["length"].as<uint16_t>();
        bool reversed = doc["reverse"].is<bool>() ? doc["reverse"].as<bool>() : false;
        
        // Create segment
        lume::Segment* seg = lume::controller.createSegment(start, length, reversed);
        if (!seg) {
            sendJsonError(request, 500, "creation_failed", "Failed to create segment");
            return;
        }
        
        // Find segment ID
        uint8_t segmentId = 0;
        for (uint8_t i = 0; i < 8; i++) {
            if (lume::controller.getSegment(i) == seg) {
                segmentId = i;
                break;
            }
        }
        
        // Apply optional settings
        if (doc["effect"].is<const char*>()) {
            seg->setEffect(doc["effect"].as<const char*>());
        }
        if (doc["speed"].is<int>()) {
            seg->setSpeed(doc["speed"].as<uint8_t>());
        }
        if (doc["intensity"].is<int>()) {
            seg->setIntensity(doc["intensity"].as<uint8_t>());
        }
        // Note: Reverse flag is set at creation time via setRange(), not changeable after
        
        // Colors
        if (doc["primaryColor"].is<JsonArrayConst>()) {
            JsonArrayConst arr = doc["primaryColor"].as<JsonArrayConst>();
            if (arr.size() >= 3) {
                seg->setPrimaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
            }
        }
        if (doc["secondaryColor"].is<JsonArrayConst>()) {
            JsonArrayConst arr = doc["secondaryColor"].as<JsonArrayConst>();
            if (arr.size() >= 3) {
                seg->setSecondaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
            }
        }
        
        // Palette - use enum value or preset name
        if (doc["palette"].is<int>()) {
            seg->setPalette(static_cast<lume::PalettePreset>(doc["palette"].as<int>()));
        }
        
        // Return created segment
        JsonDocument responseDoc;
        JsonObject obj = responseDoc.to<JsonObject>();
        segmentToJson(obj, seg, segmentId);
        
        String output;
        serializeJson(responseDoc, output);
        request->send(201, "application/json", output);
        
        LOG_INFO(LogTag::LED, "Created segment %d: start=%d length=%d", segmentId, start, length);
    }
}

// ===========================================================================
// PUT /api/v2/segments/{id} - Update segment
// ===========================================================================
void handleApiV2SegmentUpdate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Parse segment ID from URL path
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    uint8_t id = path.substring(lastSlash + 1).toInt();
    
    if (id > 7) {
        sendJsonError(request, 400, "validation_error", "Segment ID must be between 0 and 7", "id");
        return;
    }
    
    // Validate size at first chunk
    if (index == 0) {
        segmentUpdateBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            sendJsonError(request, 413, "payload_too_large", "Request body exceeds MAX_REQUEST_BODY_SIZE");
            return;
        }
    }
    
    // Accumulate chunks
    for (size_t i = 0; i < len; i++) {
        segmentUpdateBuffer += (char)data[i];
    }
    
    // Process when complete
    if (index + len >= total) {
        lume::Segment* seg = lume::controller.getSegment(id);
        if (!seg) {
            sendJsonError(request, 404, "not_found", "Segment not found", "id");
            return;
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, segmentUpdateBuffer);
        
        if (error) {
            sendJsonError(request, 400, "invalid_json", "Unable to parse JSON payload");
            return;
        }
        
        // Update fields if present
        if (doc["effect"].is<const char*>()) {
            seg->setEffect(doc["effect"].as<const char*>());
        }
        if (doc["speed"].is<int>()) {
            seg->setSpeed(doc["speed"].as<uint8_t>());
        }
        if (doc["intensity"].is<int>()) {
            seg->setIntensity(doc["intensity"].as<uint8_t>());
        }
        // Note: Reverse flag is set at creation time, not changeable after
        
        if (doc["primaryColor"].is<JsonArrayConst>()) {
            JsonArrayConst arr = doc["primaryColor"].as<JsonArrayConst>();
            if (arr.size() >= 3) {
                seg->setPrimaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
            }
        }
        if (doc["secondaryColor"].is<JsonArrayConst>()) {
            JsonArrayConst arr = doc["secondaryColor"].as<JsonArrayConst>();
            if (arr.size() >= 3) {
                seg->setSecondaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
            }
        }
        // Palette - use enum value
        if (doc["palette"].is<int>()) {
            seg->setPalette(static_cast<lume::PalettePreset>(doc["palette"].as<int>()));
        }
        
        // Return updated segment
        JsonDocument responseDoc;
        JsonObject obj = responseDoc.to<JsonObject>();
        segmentToJson(obj, seg, id);
        
        String output;
        serializeJson(responseDoc, output);
        request->send(200, "application/json", output);
        
        LOG_INFO(LogTag::LED, "Updated segment %d", id);
    }
}

// ===========================================================================
// DELETE /api/v2/segments/{id} - Remove segment
// ===========================================================================
void handleApiV2SegmentDelete(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Parse segment ID from URL path
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    uint8_t id = path.substring(lastSlash + 1).toInt();
    
    if (id > 7) {
        sendJsonError(request, 400, "validation_error", "Segment ID must be between 0 and 7", "id");
        return;
    }
    
    if (!lume::controller.getSegment(id)) {
        sendJsonError(request, 404, "not_found", "Segment not found", "id");
        return;
    }
    
    lume::controller.removeSegment(id);
    request->send(200, "application/json", "{\"success\":true}");
    LOG_INFO(LogTag::LED, "Deleted segment %d", id);
}

// ===========================================================================
// GET /api/v2/effects - List available effects
// ===========================================================================
void handleApiV2EffectsList(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray effects = doc["effects"].to<JsonArray>();
    
    // Iterate through effect registry
    lume::EffectRegistry& registry = lume::effects();
    for (uint8_t i = 0; i < registry.getCount(); i++) {
        const lume::EffectInfo* info = registry.getByIndex(i);
        if (!info) continue;
        
        JsonObject effect = effects.add<JsonObject>();
        effect["id"] = info->id;
        effect["name"] = info->displayName;
        effect["category"] = static_cast<int>(info->category);
        effect["usesPalette"] = info->usesPalette;
        effect["usesPrimaryColor"] = info->usesPrimaryColor;
        effect["usesSecondaryColor"] = info->usesSecondaryColor;
        effect["usesSpeed"] = info->usesSpeed;
        effect["usesIntensity"] = info->usesIntensity;
    }
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ===========================================================================
// GET /api/v2/palettes - List available palettes
// ===========================================================================
void handleApiV2PalettesList(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray palettes = doc["palettes"].to<JsonArray>();
    
    // Map palette enum to names
    const char* paletteNames[] = {
        "Rainbow", "Lava", "Ocean", "Party", "Forest", "Cloud", "Heat"
    };
    
    for (int i = 0; i < 7; i++) {
        JsonObject pal = palettes.add<JsonObject>();
        pal["id"] = i;
        pal["name"] = paletteNames[i];
    }
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ===========================================================================
// GET /api/v2/info - Firmware & capability metadata
// ===========================================================================
void handleApiV2Info(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    JsonDocument doc;
    JsonObject firmware = doc["firmware"].to<JsonObject>();
    firmware["name"] = FIRMWARE_NAME;
    firmware["version"] = FIRMWARE_VERSION;
    firmware["buildHash"] = FIRMWARE_BUILD_HASH;
    firmware["buildTimestamp"] = FIRMWARE_BUILD_TIMESTAMP;
    
    JsonObject limits = doc["limits"].to<JsonObject>();
    limits["maxLeds"] = MAX_LED_COUNT;
    limits["maxSegments"] = lume::MAX_SEGMENTS;
    limits["maxRequestBody"] = MAX_REQUEST_BODY_SIZE;
    
    JsonObject features = doc["features"].to<JsonObject>();
    features["segmentsV2"] = true;
    features["directPixels"] = true;
    features["sacn"] = config.sacnEnabled;
    features["mqtt"] = config.mqttEnabled;
    features["aiPrompts"] = true;
    features["ota"] = true;
    
    JsonObject controllerInfo = doc["controller"].to<JsonObject>();
    controllerInfo["ledCount"] = lume::controller.getLedCount();
    controllerInfo["power"] = lume::controller.getPower();
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ===========================================================================
// GET /api/v2/controller - Get controller state
// ===========================================================================
void handleApiV2ControllerGet(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    JsonDocument doc;
    doc["power"] = lume::controller.getPower();
    doc["brightness"] = lume::controller.getBrightness();
    doc["ledCount"] = lume::controller.getLedCount();
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ===========================================================================
// PUT /api/v2/controller - Update controller state
// ===========================================================================
void handleApiV2ControllerUpdate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Validate size at first chunk
    if (index == 0) {
        controllerUpdateBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            sendJsonError(request, 413, "payload_too_large", "Request body exceeds MAX_REQUEST_BODY_SIZE");
            return;
        }
    }
    
    // Accumulate chunks
    for (size_t i = 0; i < len; i++) {
        controllerUpdateBuffer += (char)data[i];
    }
    
    // Process when complete
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, controllerUpdateBuffer);
        
        if (error) {
            sendJsonError(request, 400, "invalid_json", "Unable to parse JSON payload");
            return;
        }
        
        // Update power
        if (doc["power"].is<bool>()) {
            lume::controller.setPower(doc["power"].as<bool>());
            LOG_INFO(LogTag::LED, "Power set to %s", doc["power"].as<bool>() ? "ON" : "OFF");
        }
        
        // Update brightness
        if (doc["brightness"].is<int>()) {
            uint8_t bri = constrain(doc["brightness"].as<int>(), 0, 255);
            lume::controller.setBrightness(bri);
            LOG_INFO(LogTag::LED, "Brightness set to %d", bri);
        }
        
        // Return updated state
        JsonDocument responseDoc;
        responseDoc["power"] = lume::controller.getPower();
        responseDoc["brightness"] = lume::controller.getBrightness();
        responseDoc["ledCount"] = lume::controller.getLedCount();
        
        String output;
        serializeJson(responseDoc, output);
        request->send(200, "application/json", output);
    }
}
