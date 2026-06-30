#ifndef LUME_PARAM_CODEC_H
#define LUME_PARAM_CODEC_H

#include <ArduinoJson.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "param_schema.h"

// Single source of truth for schema-aware parameter (de)serialization.
//
// Previously this switch-on-ParamType logic was copy-pasted across the API
// handlers and the controller's persistence path (see docs/TECH_DEBT.md P1.1).
// Consolidating it here makes it host-testable and keeps every transport in
// agreement about what "a param" looks like as JSON.

namespace lume {

inline uint8_t clampU8(long v, uint8_t lo, uint8_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return static_cast<uint8_t>(v);
}

// --- Serialize: ParamValues -> JSON ---

inline void paramToJson(JsonObject& obj, const ParamDesc& d,
                        const ParamValues& pv, uint8_t slot) {
    switch (d.type) {
        case ParamType::Int:   obj[d.id] = pv.getInt(slot);   break;
        case ParamType::Float: obj[d.id] = pv.getFloat(slot); break;
        case ParamType::Bool:  obj[d.id] = pv.getBool(slot);  break;
        case ParamType::Enum:  obj[d.id] = pv.getEnum(slot);  break;
        case ParamType::Color: {
            CRGB c = pv.getColor(slot);
            char hex[8];
            snprintf(hex, sizeof(hex), "#%02x%02x%02x", c.r, c.g, c.b);
            obj[d.id] = hex;
            break;
        }
        case ParamType::Palette:
            break;  // palette is stored separately, not round-tripped here
    }
}

inline void paramsToJson(JsonObject& obj, const ParamSchema& schema,
                         const ParamValues& pv) {
    for (uint8_t i = 0; i < schema.count && i < MAX_EFFECT_PARAMS; i++) {
        paramToJson(obj, schema.params[i], pv, i);
    }
}

// --- Deserialize: JSON -> ParamValues ---
//
// Unknown keys, type mismatches, and malformed values are ignored (defensive).
// Int values are clamped to the schema's [min,max] rather than truncated.
// Returns true if a value was applied.

inline bool paramFromJson(ParamValues& pv, const ParamSchema& schema,
                          const char* key, JsonVariantConst val) {
    int8_t idx = schema.indexOf(key);
    if (idx < 0 || idx >= static_cast<int8_t>(MAX_EFFECT_PARAMS)) return false;
    const ParamDesc& d = schema.params[idx];
    switch (d.type) {
        case ParamType::Int:
            if (val.is<int>()) { pv.setInt(idx, clampU8(val.as<int>(), d.minInt, d.maxInt)); return true; }
            break;
        case ParamType::Float:
            if (val.is<float>()) { pv.setFloat(idx, val.as<float>()); return true; }
            break;
        case ParamType::Bool:
            if (val.is<bool>()) { pv.setBool(idx, val.as<bool>()); return true; }
            break;
        case ParamType::Enum:
            if (val.is<int>()) { pv.setEnum(idx, static_cast<uint8_t>(val.as<int>())); return true; }
            break;
        case ParamType::Color: {
            const char* hex = val.as<const char*>();
            if (hex && hex[0] == '#' && strlen(hex) == 7) {
                uint32_t rgb = static_cast<uint32_t>(strtol(hex + 1, nullptr, 16));
                pv.setColor(idx, CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
                return true;
            }
            break;
        }
        case ParamType::Palette:
            break;
    }
    return false;
}

inline void paramsFromJson(ParamValues& pv, const ParamSchema& schema,
                           JsonObjectConst obj) {
    for (JsonPairConst kv : obj) {
        paramFromJson(pv, schema, kv.key().c_str(), kv.value());
    }
}

} // namespace lume

#endif // LUME_PARAM_CODEC_H
