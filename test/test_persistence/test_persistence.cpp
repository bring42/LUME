// Native (host-compiled) round-trip tests for segment-layout persistence.
// Guards LumeController::serializeSegments <-> restoreSegments: the software
// twin of the on-device save->reboot->restore flow. Runs in CI via
// `pio test -e native` with no ESP32 hardware.
//
// The controller's implementation is pulled in directly (test_build_src = no),
// compiled against the host stubs in test/stubs (FastLED, Arduino).
#include <unity.h>
#include <ArduinoJson.h>

#include "core/controller.cpp"

using namespace lume;

// --- Linker-satisfying definitions for the FastLED stub globals that
//     controller.cpp ODR-uses (the object) or could (the palettes). ---
CFastLED FastLED;
const CRGBPalette16 RainbowColors_p;
const CRGBPalette16 LavaColors_p;
const CRGBPalette16 OceanColors_p;
const CRGBPalette16 PartyColors_p;
const CRGBPalette16 ForestColors_p;
const CRGBPalette16 CloudColors_p;
const CRGBPalette16 HeatColors_p;

// --- A test-only effect with a schema covering the round-trippable types. ---
static void testfx(SegmentView&, const ParamValues&, uint32_t, bool) {}

DEFINE_EFFECT_SCHEMA(kTestSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255),
    ParamDesc::Color("color", "Color", CRGB(0, 0, 0)),
    ParamDesc::Enum("dir", "Direction", "Up|Down", 0)
);
REGISTER_EFFECT_SCHEMA(testfx, "testfx", "Test FX", Animated, kTestSchema, 0);

void setUp() {}
void tearDown() {}

// A populated two-segment layout survives serialize -> restore unchanged:
// per-segment geometry, brightness, effect id, schema params, and the
// controller-level power/brightness all round-trip.
void test_layout_roundtrip_preserves_everything() {
    LumeController src;
    src.begin(60);
    src.setPower(false);
    src.setBrightness(200);

    Segment* s0 = src.createSegment(0, 30, /*reversed=*/false);
    TEST_ASSERT_NOT_NULL(s0);
    s0->setEffect("testfx");          // applies schema defaults...
    s0->setBrightness(123);
    ParamValues& p0 = s0->getParamValues();
    p0.setInt(0, 200);                // ...then we override them
    p0.setColor(1, CRGB(0x12, 0x34, 0x56));
    p0.setEnum(2, 1);

    Segment* s1 = src.createSegment(30, 20, /*reversed=*/true);
    TEST_ASSERT_NOT_NULL(s1);
    s1->setEffect("testfx");
    s1->setBrightness(64);

    JsonDocument doc;
    src.serializeSegments(doc);

    // Restore into a fresh controller.
    LumeController dst;
    dst.begin(60);
    TEST_ASSERT_TRUE(dst.restoreSegments(doc));

    TEST_ASSERT_EQUAL_UINT8(2, dst.getSegmentCount());
    TEST_ASSERT_FALSE(dst.getPower());
    TEST_ASSERT_EQUAL_UINT8(200, dst.getBrightness());

    Segment* r0 = dst.getSegment(0);
    TEST_ASSERT_NOT_NULL(r0);
    TEST_ASSERT_EQUAL_UINT16(0, r0->getStart());
    TEST_ASSERT_EQUAL_UINT16(30, r0->getLength());
    TEST_ASSERT_FALSE(r0->isReversed());
    TEST_ASSERT_EQUAL_UINT8(123, r0->getBrightness());
    TEST_ASSERT_EQUAL_STRING("testfx", r0->getEffectId());
    const ParamValues& rp0 = r0->getParamValues();
    TEST_ASSERT_EQUAL_UINT8(200, rp0.getInt(0));
    CRGB c = rp0.getColor(1);
    TEST_ASSERT_EQUAL_UINT8(0x12, c.r);
    TEST_ASSERT_EQUAL_UINT8(0x34, c.g);
    TEST_ASSERT_EQUAL_UINT8(0x56, c.b);
    TEST_ASSERT_EQUAL_UINT8(1, rp0.getEnum(2));

    Segment* r1 = dst.getSegment(1);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_EQUAL_UINT16(30, r1->getStart());
    TEST_ASSERT_EQUAL_UINT16(20, r1->getLength());
    TEST_ASSERT_TRUE(r1->isReversed());
    TEST_ASSERT_EQUAL_UINT8(64, r1->getBrightness());
}

// An empty / segment-less document must not pretend to restore (callers rely on
// the false return to fall back to a default layout).
void test_restore_empty_document_returns_false() {
    LumeController dst;
    dst.begin(60);
    JsonDocument empty;
    TEST_ASSERT_FALSE(dst.restoreSegments(empty));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_layout_roundtrip_preserves_everything);
    RUN_TEST(test_restore_empty_document_returns_false);
    return UNITY_END();
}
