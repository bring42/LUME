// Native (host-compiled) tests for the 16-bit render substrate (core/render16.h).
//
// render16.h owns the final descent from a linear 16-bit content field to the
// 8-bit WS2812B wire — gamma16 → one-pole IIR smooth → temporal dither — and is
// pure integer math with no FastLED dependency, so the whole pipeline is driven
// deterministically here. These guard the properties the premium-modes spec
// leans on: gamma is pinned at the endpoints and monotonic, the smoothing filter
// always SETTLES exactly (never stalls a code short, never overshoots), and the
// dither's frame-average equals the true sub-8-bit value (so "nothing steps" at
// the bottom codes actually holds). Runs in CI via `pio test -e native`.
#include <unity.h>

#include "core/render16.h"

using namespace lume;

void setUp() {}
void tearDown() {}

// 8→16 expansion pins both ends: 0→0, 255→65535 (full range, ×257).
void test_expand8_endpoints() {
    TEST_ASSERT_EQUAL_UINT16(0, CRGB16::expand8(0));
    TEST_ASSERT_EQUAL_UINT16(65535, CRGB16::expand8(255));
    TEST_ASSERT_EQUAL_UINT16(0x8080, CRGB16::expand8(128));
}

void test_scale_and_lerp() {
    TEST_ASSERT_UINT16_WITHIN(2, 65535, scale16by8(65535, 255)); // ~identity (mul-shift)
    TEST_ASSERT_EQUAL_UINT16(0, scale16by8(65535, 0));           // 0 stays exactly 0
    TEST_ASSERT_UINT16_WITHIN(2, 32895, scale16by8(65535, 128)); // 128/255 ≈ 0.502, not ½
    TEST_ASSERT_EQUAL_UINT16(0, lerp16(0, 65535, 0));         // frac 0 → a, exact
    TEST_ASSERT_UINT16_WITHIN(2, 65535, lerp16(0, 65535, 65535)); // frac max → ~b (fixed-point)
    TEST_ASSERT_UINT16_WITHIN(2, 32768, lerp16(0, 65535, 32768)); // halfway
}

// Gamma is pinned at 0 and full, and monotonically non-decreasing across the
// whole 16-bit domain (a non-monotonic curve would band).
void test_gamma_endpoints_and_monotonic() {
    Gamma16 g(2.2f);
    TEST_ASSERT_EQUAL_UINT16(0, g.apply((uint16_t)0));            // full off, exact
    TEST_ASSERT_UINT16_WITHIN(8, 65535, g.apply((uint16_t)65535)); // full on within a few codes
    uint16_t prev = 0;
    for (uint32_t v = 0; v <= 65535; v += 251) {   // 251: prime stride, samples widely
        uint16_t y = g.apply((uint16_t)v);
        TEST_ASSERT_TRUE(y >= prev);
        prev = y;
    }
    // γ > 1 pulls the mid below the linear line (perceptual dimming).
    TEST_ASSERT_TRUE(g.apply((uint16_t)32768) < 32768);
}

// γ == 1.0 is (near) identity — a useful sanity anchor for the LUT + interp.
void test_gamma_unity_is_identity() {
    Gamma16 g(1.0f);
    for (uint32_t v = 0; v <= 65535; v += 517) {
        TEST_ASSERT_UINT16_WITHIN(64, (uint16_t)v, g.apply((uint16_t)v));
    }
}

// The one-pole filter converges to the target EXACTLY (no stall one code short),
// monotonically, and without overshoot.
void test_iir_settles_exactly() {
    uint16_t s = 0;
    const uint16_t target = 40000;
    uint16_t prev = 0;
    for (int i = 0; i < 2000; i++) {
        iirStep(s, target, 4);
        TEST_ASSERT_TRUE(s >= prev);      // monotonic up
        TEST_ASSERT_TRUE(s <= target);    // never overshoots
        prev = s;
    }
    TEST_ASSERT_EQUAL_UINT16(target, s);  // exact settle
}

void test_iir_settles_downward() {
    uint16_t s = 65535;
    const uint16_t target = 1234;
    for (int i = 0; i < 4000; i++) iirStep(s, target, 5);
    TEST_ASSERT_EQUAL_UINT16(target, s);
    iirStep(s, target, 5);                 // at target → no-op
    TEST_ASSERT_EQUAL_UINT16(target, s);
}

// Dither endpoints: a value that is an exact multiple of 256 emits its top byte
// steadily with no wobble.
void test_dither_exact_codes_are_steady() {
    uint16_t err = 0;
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, dither16to8(0, err));          // full off
    }
    err = 0;
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL_UINT8(255, dither16to8(65535, err));    // ~full on
    }
    err = 0;
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL_UINT8(16, dither16to8(0x1000, err));    // 4096>>8 == 16 exact
    }
}

// The whole point: a value BETWEEN two 8-bit codes averages, over frames, to the
// true sub-code brightness — so the bottom of the range reads continuous.
void test_dither_frame_average_matches_truth() {
    // 0x0180 == 384 → true = 384/256 = 1.5. Over N frames a mix of 1s and 2s
    // must sum to ~1.5*N (integer check — Unity double asserts are off here).
    uint16_t err = ditherSeed(7);
    uint32_t sum = 0;
    const uint32_t N = 2560;
    for (uint32_t i = 0; i < N; i++) sum += dither16to8(0x0180, err);
    TEST_ASSERT_UINT32_WITHIN(4, 3840, sum);  // 1.5 * 2560 == 3840
}

// ditherSeed decorrelates neighbours: adjacent pixels don't start in lockstep.
void test_dither_seed_decorrelates() {
    TEST_ASSERT_TRUE(ditherSeed(0) != ditherSeed(1));
    TEST_ASSERT_TRUE(ditherSeed(10) != ditherSeed(11));
}

// Saturating add clamps at full instead of wrapping — additive drawing safety.
void test_qadd16_saturates() {
    TEST_ASSERT_EQUAL_UINT16(300, qadd16(100, 200));
    TEST_ASSERT_EQUAL_UINT16(65535, qadd16(65000, 1000));   // would wrap without clamp
    TEST_ASSERT_EQUAL_UINT16(65535, qadd16(65535, 65535));
    CRGB16 s = addSat(CRGB16(60000, 100, 0), CRGB16(10000, 50, 5));
    TEST_ASSERT_EQUAL_UINT16(65535, s.r);   // clamped
    TEST_ASSERT_EQUAL_UINT16(150, s.g);
    TEST_ASSERT_EQUAL_UINT16(5, s.b);
}

// 16-bit fractional scale (the Wu split weight): 0 → black, full → ~identity.
void test_scale16_fraction() {
    CRGB16 c(65535, 40000, 8000);
    CRGB16 z = scale16(c, (uint16_t)0);
    TEST_ASSERT_EQUAL_UINT16(0, z.r);
    CRGB16 full = scale16(c, (uint16_t)65535);
    TEST_ASSERT_UINT16_WITHIN(2, 65535, full.r);
    TEST_ASSERT_UINT16_WITHIN(2, 40000, full.g);
    CRGB16 half = scale16(c, (uint16_t)32768);
    TEST_ASSERT_UINT16_WITHIN(2, 32768, half.r);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_expand8_endpoints);
    RUN_TEST(test_scale_and_lerp);
    RUN_TEST(test_gamma_endpoints_and_monotonic);
    RUN_TEST(test_gamma_unity_is_identity);
    RUN_TEST(test_iir_settles_exactly);
    RUN_TEST(test_iir_settles_downward);
    RUN_TEST(test_dither_exact_codes_are_steady);
    RUN_TEST(test_dither_frame_average_matches_truth);
    RUN_TEST(test_dither_seed_decorrelates);
    RUN_TEST(test_qadd16_saturates);
    RUN_TEST(test_scale16_fraction);
    return UNITY_END();
}
