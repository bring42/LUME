#ifndef LUME_PARAM_SCHEMA_H
#define LUME_PARAM_SCHEMA_H

#include <FastLED.h>

namespace lume {

// ============================================
// Parameter Types (for schema declaration)
// ============================================

enum class ParamType :  uint8_t {
    Int,        // 0-255 slider
    Float,      // 0.0-1.0 slider
    Color,      // RGB picker
    Palette,    // Palette selector
    Bool,       // Toggle
    Enum,       // Dropdown
};

/**
 * Single parameter descriptor - lives in flash, no heap allocation. 
 * 
 * Effects declare an array of these to describe their parameters.
 * The API serializes this to JSON for the UI.
 */
struct ParamDesc {
    const char* id;           // Machine name:  "speed" (for API/storage)
    const char* name;         // Human name: "Speed" (for UI)
    ParamType type;           // Controls UI widget type
    
    // Type-specific constraints (union would save space but complicate init)
    uint8_t defaultInt;       // Default for Int/Bool/Enum
    uint8_t minInt;           // Min for Int
    uint8_t maxInt;           // Max for Int
    float defaultFloat;       // Default for Float
    float minFloat;           // Min for Float  
    float maxFloat;           // Max for Float
    CRGB defaultColor;        // Default for Color
    const char* enumOptions;  // For Enum:  "option1|option2|option3"
    
    // Convenience constructors (C++11 aggregate init works too)
    static constexpr ParamDesc Int(const char* id, const char* name, 
                                    uint8_t def, uint8_t min = 0, uint8_t max = 255) {
        return {id, name, ParamType:: Int, def, min, max, 0, 0, 0, CRGB:: Black, nullptr};
    }
    
    static constexpr ParamDesc Float(const char* id, const char* name,
                                      float def, float min = 0.0f, float max = 1.0f) {
        return {id, name, ParamType::Float, 0, 0, 0, def, min, max, CRGB:: Black, nullptr};
    }
    
    static constexpr ParamDesc Color(const char* id, const char* name, CRGB def = CRGB:: Red) {
        return {id, name, ParamType::Color, 0, 0, 0, 0, 0, 0, def, nullptr};
    }
    
    static constexpr ParamDesc Bool(const char* id, const char* name, bool def = false) {
        return {id, name, ParamType::Bool, def ?  (uint8_t)1 : (uint8_t)0, 0, 1, 0, 0, 0, CRGB::Black, nullptr};
    }
    
    static constexpr ParamDesc Enum(const char* id, const char* name, 
                                     const char* options, uint8_t def = 0) {
        return {id, name, ParamType::Enum, def, 0, 0, 0, 0, 0, CRGB::Black, options};
    }
    
    static constexpr ParamDesc PaletteSelect(const char* id, const char* name) {
        return {id, name, ParamType::Palette, 0, 0, 0, 0, 0, 0, CRGB::Black, nullptr};
    }
};

/**
 * Schema is a static array of descriptors. 
 * Points to flash memory, no runtime allocation.
 */
struct ParamSchema {
    const ParamDesc* params;
    uint8_t count;
    
    // Find param by id (for runtime lookups)
    const ParamDesc* find(const char* id) const {
        if (! params || !id) return nullptr;
        for (uint8_t i = 0; i < count; i++) {
            if (strcmp(params[i]. id, id) == 0) return &params[i];
        }
        return nullptr;
    }
    
    // Find param index by id (for slot-based access)
    int8_t indexOf(const char* id) const {
        if (!params || ! id) return -1;
        for (uint8_t i = 0; i < count; i++) {
            if (strcmp(params[i]. id, id) == 0) return i;
        }
        return -1;
    }
    
    // Empty schema singleton
    static const ParamSchema empty;
};

// ============================================
// Runtime Values Storage
// ============================================

// Maximum parameters per effect (keeps struct size predictable)
constexpr uint8_t MAX_EFFECT_PARAMS = 8;

/**
 * Runtime parameter values - what effects actually read. 
 * 
 * Effects access by slot index (compile-time constant).
 * Schema defines what each slot means.
 */
struct ParamValues {
    // Slot storage - each slot can hold any type
    union Slot {
        uint8_t intVal;
        float floatVal;
        CRGB colorVal;
        bool boolVal;
        uint8_t enumVal;
    };
    
    Slot slots[MAX_EFFECT_PARAMS];
    CRGBPalette16 palette;  // Palette is special (too big for slot)
    
    // Typed accessors by slot
    uint8_t getInt(uint8_t slot) const { return slots[slot].intVal; }
    float getFloat(uint8_t slot) const { return slots[slot].floatVal; }
    CRGB getColor(uint8_t slot) const { return slots[slot].colorVal; }
    bool getBool(uint8_t slot) const { return slots[slot].boolVal; }
    uint8_t getEnum(uint8_t slot) const { return slots[slot].enumVal; }
    const CRGBPalette16& getPalette() const { return palette; }
    
    void setInt(uint8_t slot, uint8_t v) { slots[slot].intVal = v; }
    void setFloat(uint8_t slot, float v) { slots[slot].floatVal = v; }
    void setColor(uint8_t slot, CRGB v) { slots[slot]. colorVal = v; }
    void setBool(uint8_t slot, bool v) { slots[slot].boolVal = v; }
    void setEnum(uint8_t slot, uint8_t v) { slots[slot].enumVal = v; }
    void setPalette(const CRGBPalette16& p) { palette = p; }
    
    // Initialize from schema defaults
    void applyDefaults(const ParamSchema& schema) {
        for (uint8_t i = 0; i < schema. count && i < MAX_EFFECT_PARAMS; i++) {
            const ParamDesc& p = schema.params[i];
            switch (p.type) {
                case ParamType::Int: 
                    slots[i].intVal = p.defaultInt;
                    break;
                case ParamType::Float: 
                    slots[i].floatVal = p.defaultFloat;
                    break;
                case ParamType::Color: 
                    slots[i].colorVal = p.defaultColor;
                    break;
                case ParamType::Bool: 
                    slots[i].boolVal = p.defaultInt != 0;
                    break;
                case ParamType:: Enum:
                    slots[i]. enumVal = p. defaultInt;
                    break;
                case ParamType:: Palette:
                    // Palette defaults handled separately
                    break;
            }
        }
    }
};

} // namespace lume

#endif // LUME_PARAM_SCHEMA_H