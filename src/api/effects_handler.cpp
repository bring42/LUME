#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "../core/effect_registry.h"
#include "../core/param_schema.h"
#include "../logging.h"

namespace lume {

// Serialize ParamType to string
const char* paramTypeToString(lume::ParamType type) {
    switch (type) {
        case lume::ParamType:: Int:      return "int";
        case lume::ParamType::Float:   return "float";
        case lume::ParamType::Color:    return "color";
        case lume::ParamType:: Palette: return "palette";
        case lume::ParamType::Bool:    return "bool";
        case lume:: ParamType:: Enum:    return "enum";
        default:                       return "unknown";
    }
}

// Serialize schema to JSON
void schemaToJson(JsonArray& paramsArray, const lume::ParamSchema* schema) {
    if (!schema || schema->count == 0) return;
    
    for (uint8_t i = 0; i < schema->count; i++) {
        const lume::ParamDesc& p = schema->params[i];
        JsonObject param = paramsArray.add<JsonObject>();
        
        param["id"] = p. id;
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
            case lume::ParamType:: Color:
                param["default"] = String("#") + 
                    String(p.defaultColor.r, HEX) + 
                    String(p.defaultColor.g, HEX) + 
                    String(p.defaultColor. b, HEX);
                break;
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

// GET /api/effects - Updated to include schema
void handleGetEffects(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray effects = doc["effects"]. to<JsonArray>();
    
    auto& registry = lume::effects();
    for (uint8_t i = 0; i < registry.getCount(); i++) {
        const lume:: EffectInfo* info = registry.getByIndex(i);
        if (! info) continue;
        
        JsonObject effect = effects.add<JsonObject>();
        effect["id"] = info->id;
        effect["name"] = info->displayName;
        effect["category"] = info->categoryName();
        
        // Include schema if available
        JsonArray params = effect["params"]. to<JsonArray>();
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
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

} // namespace lume