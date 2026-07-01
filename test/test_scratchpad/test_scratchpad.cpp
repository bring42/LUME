// Native (host-compiled) tests for the P1.5 scratchpad strategy: a fixed
// per-segment pad for the common 1D case, plus a single shared workbuffer that
// one canvas-spanning segment borrows for large / 2D state.
//
// Built with -DLUME_WORKBUFFER_SIZE=2048 (see platformio.ini [env:native]) so
// the borrow path is actually exercised. Guards: the runtime capacity check, the
// relaxed registry ceiling, borrow/persist/release, borrow refusals, and that a
// layout change drops the borrow. Compiled against the host stubs in test/stubs.
#include <unity.h>
#include <ArduinoJson.h>

#include "core/controller.cpp"

using namespace lume;

// --- Linker-satisfying definitions for the FastLED stub globals ---
CFastLED FastLED;
const CRGBPalette16 RainbowColors_p;
const CRGBPalette16 LavaColors_p;
const CRGBPalette16 OceanColors_p;
const CRGBPalette16 PartyColors_p;
const CRGBPalette16 ForestColors_p;
const CRGBPalette16 CloudColors_p;
const CRGBPalette16 HeatColors_p;

// State structs straddling the inline pad (640 B) and the workbuffer (2048 B).
struct SmallState { uint32_t v; };            // 4 B  -> fits the inline pad
struct HugeState  { uint8_t grid[1024]; };    // 1024 B -> needs the workbuffer

// A big-state effect. With the workbuffer enabled, MAX_EFFECT_STATE == 2048, so
// this registers (it would be refused on a pure 1D build).
static void bigfx(SegmentView&, const ParamValues&, uint32_t, bool) {}
DEFINE_EFFECT_SCHEMA(kBigSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255)
);
REGISTER_EFFECT_SCHEMA(bigfx, "bigfx2d", "Big 2D FX", Special, kBigSchema, sizeof(HugeState));

void setUp() {}
void tearDown() {}

// The fixed inline pad accepts small state and refuses (gracefully, via nullptr)
// anything larger than SCRATCHPAD_SIZE.
void test_inline_pad_runtime_guard() {
    LumeController c;
    c.begin(64);
    Segment* s = c.createSegment(0, 32);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NOT_NULL(s->getView().getScratchpadChecked<SmallState>());
    TEST_ASSERT_NULL(s->getView().getScratchpadChecked<HugeState>());
}

// The registry ceiling is lifted to the workbuffer size, so a big-state effect
// is registrable when a workbuffer exists.
void test_registry_accepts_big_effect() {
    TEST_ASSERT_EQUAL_UINT(2048, (unsigned)MAX_EFFECT_STATE);
    TEST_ASSERT_NOT_NULL(effects().getInfo("bigfx2d"));
}

// Borrowing the workbuffer lets a segment hold state bigger than the inline pad,
// and that state persists (it's a real buffer, not a per-frame scratch).
void test_borrow_enables_and_persists_large_state() {
    LumeController c;
    c.begin(64);
    Segment* s = c.createSegment(0, 32);
    TEST_ASSERT_EQUAL_UINT16(2048, c.workbufferCapacity());

    TEST_ASSERT_TRUE(c.borrowWorkbuffer(0, sizeof(HugeState)));
    TEST_ASSERT_TRUE(s->usesExternalScratchpad());

    HugeState* hs = s->getView().getScratchpadChecked<HugeState>();
    TEST_ASSERT_NOT_NULL(hs);
    hs->grid[1000] = 42;  // index 1000 would be out of bounds in the 640 B pad
    TEST_ASSERT_EQUAL_UINT8(42, s->getView().getScratchpadChecked<HugeState>()->grid[1000]);
}

// setEffect enforces the ACTIVE pad: a 1024 B effect is refused on the inline pad
// but accepted once the segment has borrowed the workbuffer.
void test_seteffect_honors_active_capacity() {
    LumeController c;
    c.begin(64);
    Segment* s = c.createSegment(0, 32);

    s->setEffect("bigfx2d");
    TEST_ASSERT_NULL(s->getEffect());  // refused: 1024 > inline 640

    TEST_ASSERT_TRUE(c.borrowWorkbuffer(0, sizeof(HugeState)));
    s->setEffect("bigfx2d");
    TEST_ASSERT_NOT_NULL(s->getEffect());
    TEST_ASSERT_EQUAL_STRING("bigfx2d", s->getEffectId());
}

// Releasing returns the segment to the fixed inline pad.
void test_release_restores_inline_pad() {
    LumeController c;
    c.begin(64);
    Segment* s = c.createSegment(0, 32);
    TEST_ASSERT_TRUE(c.borrowWorkbuffer(0, sizeof(HugeState)));
    TEST_ASSERT_NOT_NULL(s->getView().getScratchpadChecked<HugeState>());

    c.releaseWorkbuffer();
    TEST_ASSERT_FALSE(s->usesExternalScratchpad());
    TEST_ASSERT_NULL(s->getView().getScratchpadChecked<HugeState>());
}

// The workbuffer is single-owner and capacity-bounded.
void test_borrow_refusals() {
    LumeController c;
    c.begin(64);
    c.createSegment(0, 16);
    c.createSegment(16, 16);

    TEST_ASSERT_FALSE(c.borrowWorkbuffer(0, 4096));  // exceeds capacity
    TEST_ASSERT_FALSE(c.borrowWorkbuffer(9, 100));   // bad index
    TEST_ASSERT_TRUE(c.borrowWorkbuffer(0, 100));
    TEST_ASSERT_FALSE(c.borrowWorkbuffer(1, 100));   // already lent to slot 0
    c.releaseWorkbuffer();
    TEST_ASSERT_TRUE(c.borrowWorkbuffer(1, 100));    // now free
}

// A layout change drops the borrow so no segment is left pointing at the shared
// buffer through the copy-by-value slot shift (TECH_DEBT P2).
void test_layout_change_drops_borrow() {
    LumeController c;
    c.begin(64);
    Segment* s0 = c.createSegment(0, 16);
    Segment* s1 = c.createSegment(16, 16);
    TEST_ASSERT_TRUE(c.borrowWorkbuffer(0, 100));
    TEST_ASSERT_TRUE(s0->usesExternalScratchpad());

    c.removeSegment(s1->getId());
    TEST_ASSERT_FALSE(s0->usesExternalScratchpad());  // borrow released
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_inline_pad_runtime_guard);
    RUN_TEST(test_registry_accepts_big_effect);
    RUN_TEST(test_borrow_enables_and_persists_large_state);
    RUN_TEST(test_seteffect_honors_active_capacity);
    RUN_TEST(test_release_restores_inline_pad);
    RUN_TEST(test_borrow_refusals);
    RUN_TEST(test_layout_change_drops_borrow);
    return UNITY_END();
}
