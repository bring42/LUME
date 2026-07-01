#include "segments.h"
#include "../main.h"
#include "../storage.h"
#include "../logging.h"
#include "../constants.h"
#include "../core/controller.h"
#include "../core/effect_registry.h"
#include "../core/param_schema.h"
#include "../core/param_codec.h"
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

// Populate the effect/palette/param fields of a (zero-initialized) EffectSpec
// from a parsed request body, on the web task. Params are resolved to typed
// slots against the *static* effect schema here, so the render loop applies pure
// data with no further lookups (RFC 0001 §3). `currentEffect` is the segment's
// existing effect (used to resolve params when the body doesn't change the
// effect); pass nullptr for create. Saves last-effect as a side effect, matching
// the prior handler behavior.
void populateEffectSpecFromBody(lume::EffectSpec& spec, JsonDocument& doc,
                                const lume::EffectInfo* currentEffect) {
    const lume::EffectInfo* paramEffect = currentEffect;

    if (doc["effect"].is<const char*>()) {
        const char* effectId = doc["effect"].as<const char*>();
        const lume::EffectInfo* info = lume::effects().getInfo(effectId);
        if (info) {  // only a valid effect changes state (matches setEffect's contract)
            spec.hasEffect = true;
            strncpy(spec.effectId, effectId, lume::MAX_EFFECT_ID_LEN - 1);
            spec.effectId[lume::MAX_EFFECT_ID_LEN - 1] = '\0';
            storage.saveLastEffect(effectId);
            paramEffect = info;  // params resolve against the NEW effect
        }
    }

    if (doc["palette"].is<int>()) {
        spec.hasPalette = true;
        spec.palette = static_cast<uint8_t>(doc["palette"].as<int>());
    }

    if (doc["params"].is<JsonObjectConst>() && paramEffect && paramEffect->hasSchema()) {
        lume::ParamValues tmp = {};
        tmp.applyDefaults(*paramEffect->schema);
        lume::paramsFromJson(tmp, *paramEffect->schema, doc["params"].as<JsonObjectConst>());
        for (uint8_t i = 0; i < lume::MAX_EFFECT_PARAMS; i++) spec.slots[i] = tmp.slots[i];
        spec.hasParams = true;
    }
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
        lume::paramsToJson(paramsObj, *schema, paramValues);
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
    
    // List all segments by index (not by probing the ID space) and report each
    // segment's real id, so survivors of a middle delete aren't dropped (P0.5).
    JsonArray segments = doc["segments"].to<JsonArray>();
    uint8_t segCount = lume::controller.getSegmentCount();
    for (uint8_t i = 0; i < segCount; i++) {
        lume::Segment* seg = lume::controller.getSegmentByIndex(i);
        if (seg) {
            JsonObject segObj = segments.add<JsonObject>();
            segmentToJson(segObj, seg, seg->getId());
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
        if (!beginBody(request)) {  // P0.3: one body at a time
            sendJsonError(request, 409, "busy", "Another request body is in progress");
            return;
        }
        segmentCreateBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            endBody(request);
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
        endBody(request);   // P0.3: body fully assembled; release the slot
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
        
        // Best-effort capacity check (the render loop is authoritative, but this
        // gives the client immediate feedback instead of a silent no-op).
        if (lume::controller.getSegmentCount() >= lume::MAX_SEGMENTS) {
            sendJsonError(request, 409, "max_segments", "Maximum number of segments reached");
            return;
        }

        // Build a self-contained spec and hand it to the render loop (single
        // writer). No segment mutation happens on this (AsyncTCP) task.
        lume::EffectSpec spec = {};
        spec.create = true;
        spec.start = doc["start"].as<uint16_t>();
        spec.length = doc["length"].as<uint16_t>();
        spec.reversed = doc["reverse"].is<bool>() ? doc["reverse"].as<bool>() : false;
        populateEffectSpecFromBody(spec, doc, /*currentEffect=*/nullptr);

        lume::controller.enqueueCommand(lume::Command::applyEffectSpec(255, spec));

        request->send(202, "application/json", "{\"status\":\"accepted\"}");
        LOG_INFO(LogTag::LED, "Enqueued segment create: start=%d length=%d", spec.start, spec.length);
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
        if (!beginBody(request)) {  // P0.3: one body at a time
            sendJsonError(request, 409, "busy", "Another request body is in progress");
            return;
        }
        segmentUpdateBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            endBody(request);
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
        endBody(request);   // P0.3: body fully assembled; release the slot
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

        // Resolve params against the segment's current effect when the body
        // doesn't change it; the spec then carries pure, pre-resolved data.
        lume::EffectSpec spec = {};
        populateEffectSpecFromBody(spec, doc, /*currentEffect=*/seg->getEffect());

        lume::controller.enqueueCommand(lume::Command::applyEffectSpec(id, spec));

        request->send(202, "application/json", "{\"status\":\"accepted\"}");
        LOG_INFO(LogTag::LED, "Enqueued segment %d update", id);
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
    
    lume::controller.enqueueCommand(lume::Command::removeSegment(id));
    request->send(202, "application/json", "{\"status\":\"accepted\"}");
    LOG_INFO(LogTag::LED, "Enqueued segment %d delete", id);
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
        
        // All effects now have schemas - serialize params from schema
        JsonArray params = effect["params"].to<JsonArray>();
        if (info->hasSchema()) {
            schemaToJson(params, info->schema);
        }
        
        // Effect metadata (derived from schema)
        effect["usesPalette"] = info->usesPalette();
        effect["colorCount"] = info->colorCount();
        effect["usesSpeed"] = info->hasParam("speed");
        effect["usesIntensity"] = info->hasParam("intensity");
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
        if (!beginBody(request)) {  // P0.3: one body at a time
            sendJsonError(request, 409, "busy", "Another request body is in progress");
            return;
        }
        controllerUpdateBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            endBody(request);
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
        endBody(request);   // P0.3: body fully assembled; release the slot
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, controllerUpdateBuffer);
        
        if (error) {
            sendJsonError(request, 400, "invalid_json", "Unable to parse JSON payload");
            return;
        }
        
        // Route global mutations through the bus (single writer); no direct
        // controller mutation on this (AsyncTCP) task.
        if (doc["power"].is<bool>()) {
            lume::controller.enqueueCommand(lume::Command::setPower(doc["power"].as<bool>()));
        }

        if (doc["brightness"].is<int>()) {
            uint8_t bri = constrain(doc["brightness"].as<int>(), 0, 255);
            lume::controller.enqueueCommand(lume::Command::setGlobalBrightness(bri));
        }

        request->send(202, "application/json", "{\"status\":\"accepted\"}");
    }
}
