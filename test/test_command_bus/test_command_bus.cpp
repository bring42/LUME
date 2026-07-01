// Native (host-compiled) tests for the command bus (RFC 0001 §3 step 1).
// Proves the single-writer path: a producer enqueues a self-contained command,
// and the mutation only takes effect when the render loop drains the queue in
// update() — never on the producer's task. Runs in CI via `pio test -e native`.
#include <unity.h>
#include <ArduinoJson.h>

#include "core/controller.cpp"
#include "core/segment_serializer.h"

using namespace lume;

// --- Linker-satisfying definitions for the FastLED stub globals. ---
CFastLED FastLED;
const CRGBPalette16 RainbowColors_p;
const CRGBPalette16 LavaColors_p;
const CRGBPalette16 OceanColors_p;
const CRGBPalette16 PartyColors_p;
const CRGBPalette16 ForestColors_p;
const CRGBPalette16 CloudColors_p;
const CRGBPalette16 HeatColors_p;

// --- Test-only effect with a schema (slot 0 = an Int we can round-trip). ---
static void testfx(SegmentView&, const ParamValues&, uint32_t, bool) {}

DEFINE_EFFECT_SCHEMA(kBusSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Color("color", "Color", CRGB(0, 0, 0))
);
REGISTER_EFFECT_SCHEMA(testfx, "testfx", "Test FX", Animated, kBusSchema, 0);

// A two-color effect for exercising setColor's index -> ordered-slot mapping.
static void twocolorfx(SegmentView&, const ParamValues&, uint32_t, bool) {}
DEFINE_EFFECT_SCHEMA(kTwoColorSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Color("colorStart", "Start", CRGB(0, 0, 0)),   // slot 1
    ParamDesc::Color("colorEnd",   "End",   CRGB(0, 0, 0))    // slot 2
);
REGISTER_EFFECT_SCHEMA(twocolorfx, "twocolor", "Two Color", Animated, kTwoColorSchema, 0);

// Build the spec a web handler would for a create.
static EffectSpec makeCreateSpec(uint16_t start, uint16_t length, uint8_t speed) {
    EffectSpec spec = {};
    spec.create = true;
    spec.start = start;
    spec.length = length;
    spec.hasEffect = true;
    std::strcpy(spec.effectId, "testfx");
    spec.hasParams = true;
    spec.slots[0].intVal = speed;
    return spec;
}

void setUp() {}
void tearDown() {}

// A queued create is applied only on update(), and carries effect + params.
void test_create_is_deferred_then_applied() {
    LumeController c;
    c.begin(60);

    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0, 10, 200)));

    // Single-writer: nothing has changed on the producer side yet.
    TEST_ASSERT_EQUAL_UINT8(0, c.getSegmentCount());

    c.update();  // render loop drains the queue

    TEST_ASSERT_EQUAL_UINT8(1, c.getSegmentCount());
    Segment* s = c.getSegment(0);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_UINT16(0, s->getStart());
    TEST_ASSERT_EQUAL_UINT16(10, s->getLength());
    TEST_ASSERT_EQUAL_STRING("testfx", s->getEffectId());
    TEST_ASSERT_EQUAL_UINT8(200, s->getParamValues().getInt(0));
}

// An update command targets an existing segment by id and changes its params.
void test_update_changes_params() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0, 10, 50)));
    c.update();
    TEST_ASSERT_EQUAL_UINT8(50, c.getSegment(0)->getParamValues().getInt(0));

    EffectSpec upd = {};
    upd.hasEffect = true;
    std::strcpy(upd.effectId, "testfx");
    upd.hasParams = true;
    upd.slots[0].intVal = 222;
    c.enqueueCommand(Command::applyEffectSpec(0, upd));
    c.update();

    TEST_ASSERT_EQUAL_UINT8(1, c.getSegmentCount());
    TEST_ASSERT_EQUAL_UINT8(222, c.getSegment(0)->getParamValues().getInt(0));
}

// A queued remove is likewise deferred to the loop (the P0.1 array-shift race
// only ever happens on the single writer now).
void test_remove_is_deferred_then_applied() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0, 10, 128)));
    c.update();
    TEST_ASSERT_EQUAL_UINT8(1, c.getSegmentCount());

    c.enqueueCommand(Command::removeSegment(0));
    TEST_ASSERT_EQUAL_UINT8(1, c.getSegmentCount());  // not yet
    c.update();
    TEST_ASSERT_EQUAL_UINT8(0, c.getSegmentCount());
}

// Global power/brightness commands are likewise deferred to the loop.
void test_power_and_brightness_via_bus() {
    LumeController c;
    c.begin(60);
    TEST_ASSERT_TRUE(c.getPower());            // constructor defaults
    TEST_ASSERT_EQUAL_UINT8(255, c.getBrightness());

    c.enqueueCommand(Command::setPower(false));
    c.enqueueCommand(Command::setGlobalBrightness(40));
    TEST_ASSERT_TRUE(c.getPower());            // not applied yet (single writer)
    TEST_ASSERT_EQUAL_UINT8(255, c.getBrightness());

    c.update();
    TEST_ASSERT_FALSE(c.getPower());
    TEST_ASSERT_EQUAL_UINT8(40, c.getBrightness());
}

// Nightlight start/stop route through the bus and toggle the active flag.
void test_nightlight_start_stop_via_bus() {
    LumeController c;
    c.begin(60);
    TEST_ASSERT_FALSE(c.isNightlightActive());

    c.enqueueCommand(Command::startNightlight(/*durationSec=*/900, /*target=*/0));
    c.update();
    TEST_ASSERT_TRUE(c.isNightlightActive());

    c.enqueueCommand(Command::stopNightlight());
    c.update();
    TEST_ASSERT_FALSE(c.isNightlightActive());
}

// The AI path carries semantic params (speed/color) that resolve to schema slots
// on the loop: testfx maps "speed" -> slot 0, "color" -> slot 1.
void test_ai_semantic_params_via_bus() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0, 10, 128)));
    c.update();

    EffectSpec es = {};
    es.hasSpeed = true;
    es.speed = 77;
    es.colorCount = 1;
    es.colors[0] = CRGB(0x10, 0x20, 0x30);
    c.enqueueCommand(Command::applyEffectSpec(0, es));
    c.update();

    Segment* s = c.getSegment(0);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_UINT8(77, s->getParamValues().getInt(0));   // speed -> slot 0
    CRGB col = s->getParamValues().getColor(1);                   // color -> slot 1
    TEST_ASSERT_EQUAL_UINT8(0x10, col.r);
    TEST_ASSERT_EQUAL_UINT8(0x20, col.g);
    TEST_ASSERT_EQUAL_UINT8(0x30, col.b);
}

// setColor(i) maps to the i-th color param in schema order — not always the
// first (the P0.6 bug, which collapsed multi-color effects into one slot).
void test_setcolor_maps_index_to_ordered_color_slot() {
    Segment seg;
    TEST_ASSERT_TRUE(seg.setEffect("twocolor"));
    seg.setColor(0, CRGB(0x11, 0x22, 0x33));   // -> colorStart, slot 1
    seg.setColor(1, CRGB(0x44, 0x55, 0x66));   // -> colorEnd,   slot 2
    seg.setColor(2, CRGB(0x77, 0x88, 0x99));   // out of range -> no-op

    const ParamValues& pv = seg.getParamValues();
    CRGB start = pv.getColor(1);
    CRGB end   = pv.getColor(2);
    TEST_ASSERT_EQUAL_UINT8(0x11, start.r);    // index 0 did NOT get clobbered
    TEST_ASSERT_EQUAL_UINT8(0x22, start.g);
    TEST_ASSERT_EQUAL_UINT8(0x33, start.b);
    TEST_ASSERT_EQUAL_UINT8(0x44, end.r);
    TEST_ASSERT_EQUAL_UINT8(0x55, end.g);
    TEST_ASSERT_EQUAL_UINT8(0x66, end.b);
}

// After deleting a middle segment, IDs go non-contiguous ({0,2}); enumerating by
// index must still find every survivor, where the old by-id loop dropped id 2 (P0.5).
void test_enumeration_survives_middle_delete() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(0,  10, 100)));  // id 0
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(10, 10, 100)));  // id 1
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(20, 10, 100)));  // id 2
    c.update();
    TEST_ASSERT_EQUAL_UINT8(3, c.getSegmentCount());

    c.enqueueCommand(Command::removeSegment(1));  // delete the middle one
    c.update();
    TEST_ASSERT_EQUAL_UINT8(2, c.getSegmentCount());

    TEST_ASSERT_NULL(c.getSegment(1));            // by-id lookup of the gone id

    Segment* s0 = c.getSegmentByIndex(0);
    Segment* s1 = c.getSegmentByIndex(1);
    TEST_ASSERT_NOT_NULL(s0);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_EQUAL_UINT8(0, s0->getId());
    TEST_ASSERT_EQUAL_UINT8(2, s1->getId());      // survivor a by-id loop would drop
    TEST_ASSERT_NULL(c.getSegmentByIndex(2));     // out of range
}

// P0.8: the ReconfigureProtocols command runs the registered hook on the loop.
static int g_reconfigCalls = 0;
static void fakeReconfig() { g_reconfigCalls++; }

void test_reconfigure_protocols_runs_hook_on_loop() {
    LumeController c;
    c.begin(60);
    c.reconfigureProtocolsFn = fakeReconfig;
    g_reconfigCalls = 0;

    c.enqueueCommand(Command::reconfigureProtocols());
    TEST_ASSERT_EQUAL_INT(0, g_reconfigCalls);   // deferred to the loop
    c.update();
    TEST_ASSERT_EQUAL_INT(1, g_reconfigCalls);   // applied once, on the loop task
}

// P0.1 (pixels): staged pixels are applied only on update(), never at stage time.
void test_direct_pixels_are_deferred_then_applied() {
    LumeController c;
    c.begin(60);
    CRGB* leds = c.getLeds();
    for (uint16_t i = 0; i < 60; i++) leds[i] = CRGB(0, 0, 0);   // known baseline

    CRGB frame[60];
    memset(frame, 0, sizeof(frame));
    frame[0] = CRGB(0x11, 0x22, 0x33);
    frame[5] = CRGB(0x44, 0x55, 0x66);
    c.stageDirectPixels(frame, 60);

    // Single-writer: staging (web task) did not touch leds[].
    TEST_ASSERT_EQUAL_UINT8(0, leds[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, leds[5].r);

    c.update();  // render loop drains the staged frame into leds[]
    TEST_ASSERT_EQUAL_UINT8(0x11, leds[0].r);
    TEST_ASSERT_EQUAL_UINT8(0x22, leds[0].g);
    TEST_ASSERT_EQUAL_UINT8(0x44, leds[5].r);
    TEST_ASSERT_EQUAL_UINT8(0x55, leds[5].g);
}

// P1.7: the one canonical projection — colors as #rrggbb hex (not [r,g,b]), a
// string `effect`, and stop/brightness present. Every transport shares this.
void test_serialize_segment_canonical_shape() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::applyEffectSpec(255, makeCreateSpec(3, 10, 128)));
    c.update();
    Segment* s = c.getSegment(0);
    TEST_ASSERT_NOT_NULL(s);
    s->getParamValues().setColor(1, CRGB(0x11, 0x22, 0x33));  // testfx slot 1 = "color"

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    serializeSegment(obj, s);

    TEST_ASSERT_EQUAL_INT(0, obj["id"].as<int>());
    TEST_ASSERT_EQUAL_INT(3, obj["start"].as<int>());
    TEST_ASSERT_EQUAL_INT(12, obj["stop"].as<int>());        // 3 + 10 - 1 (inclusive)
    TEST_ASSERT_EQUAL_INT(10, obj["length"].as<int>());
    TEST_ASSERT_EQUAL_INT(255, obj["brightness"].as<int>()); // default
    TEST_ASSERT_EQUAL_STRING("testfx", obj["effect"].as<const char*>());  // string, not object
    TEST_ASSERT_TRUE(obj["params"]["color"].is<const char*>());           // hex, not [r,g,b]
    TEST_ASSERT_FALSE(obj["params"]["color"].is<JsonArrayConst>());
    TEST_ASSERT_EQUAL_STRING("#112233", obj["params"]["color"].as<const char*>());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_serialize_segment_canonical_shape);
    RUN_TEST(test_create_is_deferred_then_applied);
    RUN_TEST(test_update_changes_params);
    RUN_TEST(test_remove_is_deferred_then_applied);
    RUN_TEST(test_power_and_brightness_via_bus);
    RUN_TEST(test_nightlight_start_stop_via_bus);
    RUN_TEST(test_ai_semantic_params_via_bus);
    RUN_TEST(test_setcolor_maps_index_to_ordered_color_slot);
    RUN_TEST(test_enumeration_survives_middle_delete);
    RUN_TEST(test_reconfigure_protocols_runs_hook_on_loop);
    RUN_TEST(test_direct_pixels_are_deferred_then_applied);
    return UNITY_END();
}
