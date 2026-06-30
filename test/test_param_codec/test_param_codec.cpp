// Native (host-compiled) unit tests for the schema-aware param codec.
// Runs in CI via `pio test -e native` — no ESP32 hardware required.
#include <unity.h>
#include <ArduinoJson.h>
#include "core/param_codec.h"

using namespace lume;

// A representative schema covering every round-trippable ParamType.
static const ParamDesc kParams[] = {
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Float("scale", "Scale", 0.5f, 0.0f, 1.0f),
    ParamDesc::Color("color", "Color", CRGB(0, 0, 0)),
    ParamDesc::Bool("reverse", "Reverse", false),
    ParamDesc::Enum("dir", "Direction", "Up|Down", 0),
};
static const ParamSchema kSchema = { kParams, 5 };

void setUp() {}
void tearDown() {}

// Every typed value survives a JSON serialize -> deserialize round-trip.
void test_roundtrip_preserves_all_values() {
    ParamValues a;
    a.applyDefaults(kSchema);
    a.setInt(0, 200);
    a.setFloat(1, 0.75f);
    a.setColor(2, CRGB(0x12, 0x34, 0x56));
    a.setBool(3, true);
    a.setEnum(4, 1);

    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    paramsToJson(o, kSchema, a);

    ParamValues b;
    paramsFromJson(b, kSchema, doc.as<JsonObjectConst>());

    TEST_ASSERT_EQUAL_UINT8(200, b.getInt(0));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, b.getFloat(1));
    CRGB c = b.getColor(2);
    TEST_ASSERT_EQUAL_UINT8(0x12, c.r);
    TEST_ASSERT_EQUAL_UINT8(0x34, c.g);
    TEST_ASSERT_EQUAL_UINT8(0x56, c.b);
    TEST_ASSERT_TRUE(b.getBool(3));
    TEST_ASSERT_EQUAL_UINT8(1, b.getEnum(4));
}

// Colors serialize as lowercase, zero-padded #rrggbb (the fix for the
// effects_handler.cpp unpadded-hex bug, now in one place).
void test_color_serializes_as_padded_hex() {
    ParamValues a;
    a.setColor(2, CRGB(0xAB, 0x00, 0xFF));
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    paramToJson(o, kParams[2], a, 2);
    TEST_ASSERT_EQUAL_STRING("#ab00ff", o["color"].as<const char*>());
}

// Unknown keys, malformed hex, and out-of-range ints are handled defensively.
void test_bad_input_is_ignored_or_clamped() {
    ParamValues b;
    b.applyDefaults(kSchema);  // color slot starts at (0,0,0)

    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["nope"]  = 5;       // unknown key -> ignored
    o["color"] = "bad";   // malformed hex -> ignored
    o["speed"] = 999;     // out of [1,255] -> clamped to 255

    paramsFromJson(b, kSchema, doc.as<JsonObjectConst>());

    TEST_ASSERT_EQUAL_UINT8(255, b.getInt(0));  // clamped, not truncated to 231
    CRGB c = b.getColor(2);
    TEST_ASSERT_EQUAL_UINT8(0, c.r);            // bad hex left default untouched
    TEST_ASSERT_EQUAL_UINT8(0, c.g);
    TEST_ASSERT_EQUAL_UINT8(0, c.b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_preserves_all_values);
    RUN_TEST(test_color_serializes_as_padded_hex);
    RUN_TEST(test_bad_input_is_ignored_or_clamped);
    return UNITY_END();
}
