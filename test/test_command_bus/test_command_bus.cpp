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

// A recording effect: captures the param values it is actually rendered with,
// so a test can prove the loop feeds it *interpolated* params during a
// transition (slot 0 = Int level, slot 1 = Color).
static uint8_t g_recInt = 0;
static CRGB    g_recColor;
static void recfx(SegmentView&, const ParamValues& p, uint32_t, bool) {
    g_recInt = p.getInt(0);
    g_recColor = p.getColor(1);
}
DEFINE_EFFECT_SCHEMA(kRecSchema,
    ParamDesc::Int("level", "Level", 0, 0, 255),
    ParamDesc::Color("color", "Color", CRGB(0, 0, 0))
);
REGISTER_EFFECT_SCHEMA(recfx, "recfx", "Rec FX", Animated, kRecSchema, 0);

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

// "Nightlight" is no longer a bus command or controller state — it decomposes
// into an eased brightness fade plus a power-off-at-zero rider. A fade to zero
// carrying the rider powers the strip off once it settles.
void test_fade_to_zero_rider_powers_off() {
    LumeController c;
    c.begin(60);
    TEST_ASSERT_TRUE(c.getPower());

    c.enqueueCommand(Command::setGlobalBrightness(0)
                         .withTransition(3000).withPowerOffAtZero(true));
    c.update();
    TEST_ASSERT_TRUE(c.isBrightnessFading());   // fading, not yet settled
    TEST_ASSERT_TRUE(c.getPower());             // still on mid-fade

    for (int i = 0; i < 50 && c.isBrightnessFading(); i++) c.update();
    TEST_ASSERT_FALSE(c.isBrightnessFading());
    TEST_ASSERT_FALSE(c.getPower());            // powered off once settled
}

// Eased power-off keeps the strip logically on (rendering, dimming) until the
// fade envelope settles, then flips it off. The brightness level is preserved.
void test_power_off_fade_defers_then_settles() {
    LumeController c;
    c.begin(60);
    TEST_ASSERT_TRUE(c.getPower());

    c.enqueueCommand(Command::setPower(false).withTransition(3000));
    c.update();
    TEST_ASSERT_TRUE(c.isPowerFading());       // envelope animating
    TEST_ASSERT_TRUE(c.getPower());            // still logically on mid-fade

    for (int i = 0; i < 50 && c.isPowerFading(); i++) c.update();
    TEST_ASSERT_FALSE(c.getPower());           // flipped off once settled
    TEST_ASSERT_EQUAL_UINT8(255, c.getBrightness());  // level preserved across toggle
}

// Eased power-on flips the strip logically on immediately (so it renders while
// fading up from black) rather than deferring like power-off.
void test_power_on_fade_is_immediate_logical() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::setPower(false));   // start off (instant)
    c.update();
    TEST_ASSERT_FALSE(c.getPower());

    c.enqueueCommand(Command::setPower(true).withTransition(3000));
    c.update();
    TEST_ASSERT_TRUE(c.getPower());            // on immediately, envelope rising
}

// A fade to a dim, non-zero target is an ordinary eased change: it must land on
// the target and leave the strip on (no rider).
void test_fade_to_dim_stays_on() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::setGlobalBrightness(50)
                         .withTransition(3000).withPowerOffAtZero(false));
    c.update();                                  // apply the command -> fade starts
    TEST_ASSERT_TRUE(c.isBrightnessFading());
    for (int i = 0; i < 50 && c.isBrightnessFading(); i++) c.update();
    TEST_ASSERT_FALSE(c.isBrightnessFading());
    TEST_ASSERT_TRUE(c.getPower());
    TEST_ASSERT_EQUAL_UINT8(50, c.getBrightness());
}

// A manual brightness set mid-fade overrides the fade AND clears the pending
// power-off rider — the strip must not switch off behind the user.
void test_manual_set_clears_power_off_rider() {
    LumeController c;
    c.begin(60);
    c.enqueueCommand(Command::setGlobalBrightness(0)
                         .withTransition(5000).withPowerOffAtZero(true));
    c.update();
    TEST_ASSERT_TRUE(c.isBrightnessFading());

    c.enqueueCommand(Command::setGlobalBrightness(200));  // instant manual set
    for (int i = 0; i < 50 && c.isBrightnessFading(); i++) c.update();
    TEST_ASSERT_TRUE(c.getPower());             // never powered off
    TEST_ASSERT_EQUAL_UINT8(200, c.getBrightness());
}

// An eased ApplyEffectSpec update interpolates continuous params/colors over the
// transition window rather than snapping — the effect is rendered with values
// strictly between old and new before landing exactly on the target.
void test_param_transition_eases_then_lands() {
    LumeController c;
    c.begin(60);

    // Create a full-strip segment running recfx (level defaults to 0, black).
    EffectSpec create = {};
    create.create = true; create.start = 0; create.length = 60;
    create.hasEffect = true; std::strcpy(create.effectId, "recfx");
    c.enqueueCommand(Command::applyEffectSpec(255, create));
    c.update();
    TEST_ASSERT_EQUAL_UINT8(0, g_recInt);   // starts at the default

    // Eased update: level 0 -> 200, color black -> red, over 4s.
    EffectSpec upd = {};
    upd.hasParams = true;
    upd.slots[0].intVal = 200;
    upd.slots[1].colorVal = CRGB(255, 0, 0);
    c.enqueueCommand(Command::applyEffectSpec(0, upd).withTransition(4000));
    c.update();  // apply the command -> transition starts
    TEST_ASSERT_TRUE(c.getSegment(0)->isParamsTransitioning());

    bool sawIntermediate = false;
    for (int i = 0; i < 80 && c.getSegment(0)->isParamsTransitioning(); i++) {
        c.update();
        if (g_recInt > 0 && g_recInt < 200) sawIntermediate = true;
    }

    TEST_ASSERT_TRUE(sawIntermediate);          // eased, did not snap
    TEST_ASSERT_EQUAL_UINT8(200, g_recInt);     // landed exactly
    TEST_ASSERT_EQUAL_UINT8(255, g_recColor.r); // color reached red
    TEST_ASSERT_EQUAL_UINT8(0,   g_recColor.g);
    TEST_ASSERT_EQUAL_UINT8(0,   g_recColor.b);
}

// An eased spec that also *changes the effect* must snap (new schema / defaults
// — nothing coherent to ease from), never leaving a transition running.
void test_effect_change_ignores_transition() {
    LumeController c;
    c.begin(60);
    EffectSpec create = {};
    create.create = true; create.start = 0; create.length = 60;
    create.hasEffect = true; std::strcpy(create.effectId, "recfx");
    c.enqueueCommand(Command::applyEffectSpec(255, create));
    c.update();

    EffectSpec chg = {};
    chg.hasEffect = true; std::strcpy(chg.effectId, "testfx");
    c.enqueueCommand(Command::applyEffectSpec(0, chg).withTransition(4000));
    c.update();
    TEST_ASSERT_FALSE(c.getSegment(0)->isParamsTransitioning());
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

// RFC 0001 §6: the LED output driver is pluggable via ILedOutput. A mock proves
// the controller drives the injected output, not FastLED directly.
struct MockOutput : ILedOutput {
    int begins = 0, shows = 0, clears = 0;
    uint8_t lastBrightness = 0;
    void begin(CRGB*, uint16_t) override { begins++; }
    void show() override { shows++; }
    void setBrightness(uint8_t b) override { lastBrightness = b; }
    void clear() override { clears++; }
};

void test_led_output_is_pluggable() {
    LumeController c;
    MockOutput mock;
    c.setLedOutput(&mock);
    c.begin(60);
    TEST_ASSERT_EQUAL_INT(1, mock.begins);        // begin() bound the injected driver
    TEST_ASSERT_TRUE(mock.shows >= 1);            // ...and pushed a frame

    int showsBefore = mock.shows;
    c.update();                                    // a render frame -> show() on the driver
    TEST_ASSERT_TRUE(mock.shows > showsBefore);

    // Brightness reaches the driver gamma-encoded (perceptual dimming): full
    // stays full, but a mid/low level is pulled down along the gamma curve.
    c.enqueueCommand(Command::setGlobalBrightness(255));
    c.update();
    TEST_ASSERT_EQUAL_UINT8(255, mock.lastBrightness);              // full = full
    c.enqueueCommand(Command::setGlobalBrightness(42));
    c.update();
    TEST_ASSERT_EQUAL_UINT8(applyGamma_video(42, LED_GAMMA), mock.lastBrightness);
    TEST_ASSERT_TRUE(mock.lastBrightness < 42);                     // gamma pulled it down
}

// Runtime gamma flows through the single-writer bus: an enqueued value is
// applied only when the loop drains the queue in update(), and out-of-range
// values are clamped to [LED_GAMMA_MIN, LED_GAMMA_MAX]. A higher gamma must also
// reach the output stage, pulling a mid level further down the curve.
void test_gamma_via_bus_and_clamps() {
    LumeController c;
    MockOutput mock;
    c.setLedOutput(&mock);
    c.begin(60);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_GAMMA, c.getGamma());   // seeded from the compile default

    c.enqueueCommand(Command::setGamma(2.8f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_GAMMA, c.getGamma());   // deferred (single writer)
    c.update();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.8f, c.getGamma());        // applied on the loop

    // A mid level is encoded with the *runtime* gamma now (2.8, deeper than 2.2).
    c.enqueueCommand(Command::setGlobalBrightness(128));
    c.update();
    TEST_ASSERT_EQUAL_UINT8(applyGamma_video(128, 2.8f), mock.lastBrightness);

    // Above range -> clamped to max; below range -> clamped to min.
    c.enqueueCommand(Command::setGamma(9.0f));
    c.update();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_GAMMA_MAX, c.getGamma());
    c.enqueueCommand(Command::setGamma(0.1f));
    c.update();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, LED_GAMMA_MIN, c.getGamma());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_led_output_is_pluggable);
    RUN_TEST(test_gamma_via_bus_and_clamps);
    RUN_TEST(test_serialize_segment_canonical_shape);
    RUN_TEST(test_create_is_deferred_then_applied);
    RUN_TEST(test_update_changes_params);
    RUN_TEST(test_remove_is_deferred_then_applied);
    RUN_TEST(test_power_and_brightness_via_bus);
    RUN_TEST(test_fade_to_zero_rider_powers_off);
    RUN_TEST(test_power_off_fade_defers_then_settles);
    RUN_TEST(test_power_on_fade_is_immediate_logical);
    RUN_TEST(test_fade_to_dim_stays_on);
    RUN_TEST(test_manual_set_clears_power_off_rider);
    RUN_TEST(test_param_transition_eases_then_lands);
    RUN_TEST(test_effect_change_ignores_transition);
    RUN_TEST(test_ai_semantic_params_via_bus);
    RUN_TEST(test_setcolor_maps_index_to_ordered_color_slot);
    RUN_TEST(test_enumeration_survives_middle_delete);
    RUN_TEST(test_reconfigure_protocols_runs_hook_on_loop);
    RUN_TEST(test_direct_pixels_are_deferred_then_applied);
    return UNITY_END();
}
