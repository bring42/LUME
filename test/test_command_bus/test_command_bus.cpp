// Native (host-compiled) tests for the command bus (RFC 0001 §3 step 1).
// Proves the single-writer path: a producer enqueues a self-contained command,
// and the mutation only takes effect when the render loop drains the queue in
// update() — never on the producer's task. Runs in CI via `pio test -e native`.
#include <unity.h>
#include <ArduinoJson.h>

#include "core/controller.cpp"

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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_create_is_deferred_then_applied);
    RUN_TEST(test_update_changes_params);
    RUN_TEST(test_remove_is_deferred_then_applied);
    return UNITY_END();
}
