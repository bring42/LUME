// Native (host-compiled) tests for EffectInfo dimensionality metadata (P1.2).
//
// Guards the EffectDims contract that a future 2D/matrix build relies on:
//  - REGISTER_EFFECT_SCHEMA leaves dims defaulted to OneD (the macro omits the
//    trailing aggregate member, so it MUST value-initialize to 0 == OneD).
//  - REGISTER_EFFECT_SCHEMA_DIMS sets dims explicitly.
//  - dimsName()/runsOn() report/filter correctly.
//
// Runs in CI via `pio test -e native` with no ESP32 hardware, compiled against
// the host stubs in test/stubs (FastLED, Arduino).
#include <unity.h>

#include "core/effect_registry.h"

using namespace lume;

// --- Test-only effects, registered at static-init time into the singleton. ---
static void fxDefault(SegmentView&, const ParamValues&, uint32_t, bool) {}
static void fxAny(SegmentView&, const ParamValues&, uint32_t, bool) {}
static void fxTwoD(SegmentView&, const ParamValues&, uint32_t, bool) {}

DEFINE_EFFECT_SCHEMA(kDimsSchema,
    ParamDesc::Int("speed", "Speed", 128, 1, 255)
);

// Default macro: dims must fall through to OneD without being named.
REGISTER_EFFECT_SCHEMA(fxDefault, "dims_default", "Dims Default", Animated, kDimsSchema, 0);
// Explicit-dims macro: Any and TwoD.
REGISTER_EFFECT_SCHEMA_DIMS(fxAny, "dims_any", "Dims Any", Animated, kDimsSchema, 0, Any);
REGISTER_EFFECT_SCHEMA_DIMS(fxTwoD, "dims_2d", "Dims 2D", Special, kDimsSchema, 0, TwoD);

void setUp() {}
void tearDown() {}

// The plain macro leaves an effect strip-only (the honest default for today's
// effects) — verified via the registry so we also prove the aggregate brace-init
// value-initializes the omitted member rather than leaving it indeterminate.
void test_default_macro_is_oned() {
    const EffectInfo* info = effects().getInfo("dims_default");
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL(EffectDims::OneD, info->dims);
    TEST_ASSERT_EQUAL_STRING("1d", info->dimsName());
}

void test_dims_macro_sets_any() {
    const EffectInfo* info = effects().getInfo("dims_any");
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL(EffectDims::Any, info->dims);
    TEST_ASSERT_EQUAL_STRING("any", info->dimsName());
}

void test_dims_macro_sets_twod() {
    const EffectInfo* info = effects().getInfo("dims_2d");
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL(EffectDims::TwoD, info->dims);
    TEST_ASSERT_EQUAL_STRING("2d", info->dimsName());
}

// runsOn() is the filter a 1D or 2D build applies to build its effect palette.
void test_runs_on_filtering() {
    const EffectInfo* oned = effects().getInfo("dims_default");
    const EffectInfo* any  = effects().getInfo("dims_any");
    const EffectInfo* twod = effects().getInfo("dims_2d");
    TEST_ASSERT_NOT_NULL(oned);
    TEST_ASSERT_NOT_NULL(any);
    TEST_ASSERT_NOT_NULL(twod);

    // A strip build accepts OneD + Any, refuses TwoD.
    TEST_ASSERT_TRUE(oned->runsOn(EffectDims::OneD));
    TEST_ASSERT_TRUE(any->runsOn(EffectDims::OneD));
    TEST_ASSERT_FALSE(twod->runsOn(EffectDims::OneD));

    // A matrix build accepts TwoD + Any, refuses OneD.
    TEST_ASSERT_TRUE(twod->runsOn(EffectDims::TwoD));
    TEST_ASSERT_TRUE(any->runsOn(EffectDims::TwoD));
    TEST_ASSERT_FALSE(oned->runsOn(EffectDims::TwoD));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_macro_is_oned);
    RUN_TEST(test_dims_macro_sets_any);
    RUN_TEST(test_dims_macro_sets_twod);
    RUN_TEST(test_runs_on_filtering);
    return UNITY_END();
}
