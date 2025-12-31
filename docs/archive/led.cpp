#include "led.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../lume.h"

// External globals
extern Config config;
extern Storage storage;

// Static body buffer for async request handling
static String ledBodyBuffer;

// ===========================================================================
// v1 API Compatibility Helpers (local to this file)
// ===========================================================================

// Get/create main segment (segment 0)
static lume::Segment* getMainSegment() {
    lume::Segment* seg = lume::controller.getSegment(0);
    if (!seg) {
        seg = lume::controller.createFullStrip();
    }
    return seg;
}

// Map v1 palette names to v2 enum
static lume::PalettePreset mapPaletteName(const char* name) {
    if (!name) return lume::PalettePreset::Rainbow;
    String pal = name;
    pal.toLowerCase();
    if (pal == "lava") return lume::PalettePreset::Lava;
    if (pal == "ocean") return lume::PalettePreset::Ocean;
    if (pal == "party") return lume::PalettePreset::Party;
    if (pal == "forest") return lume::PalettePreset::Forest;
    if (pal == "cloud") return lume::PalettePreset::Cloud;
    if (pal == "heat") return lume::PalettePreset::Heat;
    return lume::PalettePreset::Rainbow;
}

// Serialize segment 0 to v1 API format
static void segmentToV1Json(JsonDocument& doc) {
    doc["power"] = lume::controller.getPower();
    doc["brightness"] = lume::controller.getBrightness();
    
    lume::Segment* seg = getMainSegment();
    if (seg) {
        doc["effect"] = seg->getEffectId();
        doc["speed"] = seg->getSpeed();
        
        CRGB primary = seg->getPrimaryColor();
        CRGB secondary = seg->getSecondaryColor();
        
        JsonArray primaryArr = doc["primaryColor"].to<JsonArray>();
        primaryArr.add(primary.r);
        primaryArr.add(primary.g);
        primaryArr.add(primary.b);
        
        JsonArray secondaryArr = doc["secondaryColor"].to<JsonArray>();
        secondaryArr.add(secondary.r);
        secondaryArr.add(secondary.g);
        secondaryArr.add(secondary.b);
        
        doc["palette"] = "rainbow";  // v2 doesn't track preset name
    }
}

// Apply v1 JSON to segment 0
static void v1JsonToSegment(const JsonDocument& doc) {
    lume::Segment* seg = getMainSegment();
    if (!seg) return;
    
    if (doc["power"].is<bool>()) {
        lume::controller.setPower(doc["power"].as<bool>());
    }
    if (doc["brightness"].is<int>()) {
        lume::controller.setBrightness(constrain(doc["brightness"].as<int>(), 0, 255));
    }
    if (doc["effect"].is<const char*>()) {
        seg->setEffect(doc["effect"].as<const char*>());
    }
    if (doc["speed"].is<int>()) {
        seg->setSpeed(constrain(doc["speed"].as<int>(), 1, 255));
    }
    if (doc["palette"].is<const char*>()) {
        seg->setPalette(mapPaletteName(doc["palette"].as<const char*>()));
    }
    
    // Primary color
    if (doc["primaryColor"].is<JsonArrayConst>()) {
        JsonArrayConst arr = doc["primaryColor"].as<JsonArrayConst>();
        if (arr.size() >= 3) {
            seg->setPrimaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
        }
    }
    
    // Secondary color
    if (doc["secondaryColor"].is<JsonArrayConst>()) {
        JsonArrayConst arr = doc["secondaryColor"].as<JsonArrayConst>();
        if (arr.size() >= 3) {
            seg->setSecondaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
        }
    }
}

// ===========================================================================
// v1 API Endpoints (deprecated - use /api/v2/segments instead)
// ===========================================================================

void handleApiLed(AsyncWebServerRequest* request) {
    JsonDocument doc;
    segmentToV1Json(doc);
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiLedPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        ledBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    ledBodyBuffer += String((char*)data).substring(0, len);
    
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, ledBodyBuffer);
        
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        // Apply state to segment 0
        v1JsonToSegment(doc);
        
        // Save state to storage
        JsonDocument saveDoc;
        segmentToV1Json(saveDoc);
        storage.saveLedState(saveDoc);
        
        request->send(200, "application/json", "{\"success\":true}");
    }
}
