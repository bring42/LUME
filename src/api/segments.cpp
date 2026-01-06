#include "segments.h"
#include "../main.h"
#include "../storage.h"
#include "../logging.h"
#include "../constants.h"
#include "../core/controller.h"
#include "../core/effect_registry.h"
#include "../core/param_schema.h"
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

// Serialize ParamType to string
const char* paramTypeToString(lume::ParamType type) {
    switch (type) {
        case lume::ParamType::Int:      return "int";
        case lume::ParamType::Float:    return "float";
        case lume::ParamType::Color:    return "color";
        case lume::ParamType::Palette:  return "palette";
        case lume::ParamType::Bool:     return "bool";
        case lume::ParamType::Enum:     return "enum";
        default:                        return "unknown";
    }
}

// Serialize schema to JSON
void schemaToJson(JsonArray& paramsArray, const lume::ParamSchema* schema) {
    if (!schema || schema->count == 0) return;
    
    for (uint8_t i = 0; i < schema->count; i++) {
        const lume::ParamDesc& p = schema->params[i];
        JsonObject param = paramsArray.add<JsonObject>();
        
        param["id"] = p.id;
        param["name"] = p.name;
        param["type"] = paramTypeToString(p.type);
        
        switch (p.type) {
            case lume::ParamType::Int:
                param["default"] = p.defaultInt;
                param["min"] = p.minInt;
                param["max"] = p.maxInt;
                break;
            case lume::ParamType::Float:
                param["default"] = p.defaultFloat;
                param["min"] = p.minFloat;
                param["max"] = p.maxFloat;
                break;
            case lume::ParamType::Color: {
                char colorHex[8];
                snprintf(colorHex, sizeof(colorHex), "#%02x%02x%02x", 
                    p.defaultColor.r, p.defaultColor.g, p.defaultColor.b);
                param["default"] = colorHex;
                break;
            }
            case lume::ParamType::Bool:
                param["default"] = (bool)(p.defaultInt != 0);
                break;
            case lume::ParamType::Enum:
                param["default"] = p.defaultInt;
                param["options"] = p.enumOptions;  // "opt1|opt2|opt3"
                break;
            case lume::ParamType::Palette:
                // Palette list could be included or fetched separately
                break;
        }
    }
}

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
    
    // Serialize schema-based params if effect has schema
    const lume::EffectInfo* effectInfo = segment->getEffect();
    if (effectInfo && effectInfo->hasSchema()) {
        const lume::ParamSchema* schema = effectInfo->schema;
        const lume::ParamValues& paramValues = segment->getParamValues();
        
        JsonObject paramsObj = obj["params"].to<JsonObject>();
        for (uint8_t i = 0; i < schema->count && i < lume::MAX_EFFECT_PARAMS; i++) {
            const lume::ParamDesc& desc = schema->params[i];
            
            switch (desc.type) {
                case lume::ParamType::Int:
                    paramsObj[desc.id] = paramValues.getInt(i);
                    break;
                case lume::ParamType::Float:
                    paramsObj[desc.id] = paramValues.getFloat(i);
                    break;
                case lume::ParamType::Color: {
                    CRGB c = paramValues.getColor(i);
                    char hex[8];
                    snprintf(hex, sizeof(hex), "#%02x%02x%02x", c.r, c.g, c.b);
                    paramsObj[desc.id] = hex;
                    break;
                }
                case lume::ParamType::Bool:
                    paramsObj[desc.id] = paramValues.getBool(i);
                    break;
                case lume::ParamType::Enum:
                    paramsObj[desc.id] = paramValues.getEnum(i);
                    break;
                case lume::ParamType::Palette:
                    // Palette handled separately or as string
                    break;
            }
        }
    }
    
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
            const char* effectId = doc["effect"].as<const char*>();
            if (seg->setEffect(effectId)) {
                storage.saveLastEffect(effectId);  // Only save if effect exists
            }
        }
        
        // Palette - use enum value or preset name
        if (doc["palette"].is<int>()) {
            seg->setPalette(static_cast<lume::PalettePreset>(doc["palette"].as<int>()));
        }
        
        // Schema-aware parameters
        if (doc["params"].is<JsonObjectConst>()) {
            JsonObjectConst paramsObj = doc["params"].as<JsonObjectConst>();
            const lume::EffectInfo* effectInfo = seg->getEffect();
            
            if (effectInfo && effectInfo->hasSchema()) {
                lume::ParamValues& paramValues = seg->getParamValues();
                const lume::ParamSchema* schema = effectInfo->schema;
                
                for (JsonPairConst kv : paramsObj) {
                    const char* paramId = kv.key().c_str();
                    int8_t slotIdx = schema->indexOf(paramId);
                    
                    if (slotIdx >= 0 && slotIdx < lume::MAX_EFFECT_PARAMS) {
                        const lume::ParamDesc& desc = schema->params[slotIdx];
                        
                        switch (desc.type) {
                            case lume::ParamType::Int:
                                if (kv.value().is<int>()) {
                                    paramValues.setInt(slotIdx, kv.value().as<uint8_t>());
                                }
                                break;
                            case lume::ParamType::Float:
                                if (kv.value().is<float>()) {
                                    paramValues.setFloat(slotIdx, kv.value().as<float>());
                                }
                                break;
                            case lume::ParamType::Color:
                                if (kv.value().is<const char*>()) {
                                    const char* hex = kv.value().as<const char*>();
                                    if (hex[0] == '#' && strlen(hex) == 7) {
                                        uint32_t rgb = strtol(hex + 1, nullptr, 16);
                                        paramValues.setColor(slotIdx, CRGB(
                                            (rgb >> 16) & 0xFF,
                                            (rgb >> 8) & 0xFF,
                                            rgb & 0xFF
                                        ));
                                    }
                                }
                                break;
                            case lume::ParamType::Bool:
                                if (kv.value().is<bool>()) {
                                    paramValues.setBool(slotIdx, kv.value().as<bool>());
                                }
                                break;
                            case lume::ParamType::Enum:
                                if (kv.value().is<int>()) {
                                    paramValues.setEnum(slotIdx, kv.value().as<uint8_t>());
                                }
                                break;
                            case lume::ParamType::Palette:
                                break;
                        }
                    }
                }
            }
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
            const char* effectId = doc["effect"].as<const char*>();
            if (seg->setEffect(effectId)) {
                storage.saveLastEffect(effectId);  // Only save if effect exists
            }
        }
        
        // Palette - use enum value
        if (doc["palette"].is<int>()) {
            seg->setPalette(static_cast<lume::PalettePreset>(doc["palette"].as<int>()));
        }
        
        // Custom parameters (schema-aware effects)
        if (doc["params"].is<JsonObjectConst>()) {
            JsonObjectConst paramsObj = doc["params"].as<JsonObjectConst>();
            const lume::EffectInfo* effectInfo = seg->getEffect();
            
            if (effectInfo && effectInfo->hasSchema()) {
                lume::ParamValues& paramValues = seg->getParamValues();
                const lume::ParamSchema* schema = effectInfo->schema;
                
                // Iterate over provided params and update values
                for (JsonPairConst kv : paramsObj) {
                    const char* paramId = kv.key().c_str();
                    int8_t slotIdx = schema->indexOf(paramId);
                    
                    if (slotIdx >= 0 && slotIdx < lume::MAX_EFFECT_PARAMS) {
                        const lume::ParamDesc& desc = schema->params[slotIdx];
                        
                        switch (desc.type) {
                            case lume::ParamType::Int:
                                if (kv.value().is<int>()) {
                                    paramValues.setInt(slotIdx, kv.value().as<uint8_t>());
                                }
                                break;
                            case lume::ParamType::Float:
                                if (kv.value().is<float>()) {
                                    paramValues.setFloat(slotIdx, kv.value().as<float>());
                                }
                                break;
                            case lume::ParamType::Color:
                                if (kv.value().is<const char*>()) {
                                    // Parse hex color "#RRGGBB"
                                    const char* hex = kv.value().as<const char*>();
                                    if (hex[0] == '#' && strlen(hex) == 7) {
                                        uint32_t rgb = strtol(hex + 1, nullptr, 16);
                                        paramValues.setColor(slotIdx, CRGB(
                                            (rgb >> 16) & 0xFF,
                                            (rgb >> 8) & 0xFF,
                                            rgb & 0xFF
                                        ));
                                    }
                                }
                                break;
                            case lume::ParamType::Bool:
                                if (kv.value().is<bool>()) {
                                    paramValues.setBool(slotIdx, kv.value().as<bool>());
                                }
                                break;
                            case lume::ParamType::Enum:
                                if (kv.value().is<int>()) {
                                    paramValues.setEnum(slotIdx, kv.value().as<uint8_t>());
                                }
                                break;
                            case lume::ParamType::Palette:
                                // Palette handled separately above
                                break;
                        }
                    }
                }
            }
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
        effect["category"] = info->categoryName();
        
        // Include schema if available
        JsonArray params = effect["params"].to<JsonArray>();
        if (info->hasSchema()) {
            schemaToJson(params, info->schema);
        } else {
            // Legacy: generate params from flags
            if (info->usesSpeed) {
                JsonObject p = params.add<JsonObject>();
                p["id"] = "speed";
                p["name"] = "Speed";
                p["type"] = "int";
                p["min"] = 0;
                p["max"] = 255;
                p["default"] = 128;
            }
            if (info->usesIntensity) {
                JsonObject p = params.add<JsonObject>();
                p["id"] = "intensity";
                p["name"] = "Intensity";
                p["type"] = "int";
                p["min"] = 0;
                p["max"] = 255;
                p["default"] = 128;
            }
            // Generate color params based on colorCount
            for (uint8_t c = 0; c < info->colorCount; c++) {
                JsonObject p = params.add<JsonObject>();
                if (c == 0) {
                    p["id"] = "color";
                    p["name"] = "Color";
                } else {
                    char idBuf[16], nameBuf[16];
                    snprintf(idBuf, sizeof(idBuf), "color%d", c);
                    snprintf(nameBuf, sizeof(nameBuf), "Color %d", c + 1);
                    p["id"] = idBuf;
                    p["name"] = nameBuf;
                }
                p["type"] = "color";
                p["default"] = "#ff0000";
            }
            if (info->usesPalette) {
                JsonObject p = params.add<JsonObject>();
                p["id"] = "palette";
                p["name"] = "Palette";
                p["type"] = "palette";
            }
        }
        
        // Effect metadata
        effect["usesPalette"] = info->usesPalette;
        effect["colorCount"] = info->colorCount;
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
