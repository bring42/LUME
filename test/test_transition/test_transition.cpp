// Native (host-compiled) tests for the premium easing engine (core/transition.h).
//
// transition.h is the clock-free interpolator the render loop uses to ease
// control-plane changes over a Matter-shaped transitionTime instead of snapping.
// Because every method takes `nowMs` as a parameter, the whole curve can be
// driven deterministically here with no hardware. These guard: the snap path
// (transitionTime == 0), monotonic eased progress that hits the exact target,
// the ease-in-out shape (slow-start/slow-finish, midpoint == halfway), and
// mid-flight re-targeting without a jump. Runs in CI via `pio test -e native`.
#include <unity.h>

#include "core/transition.h"

using namespace lume;

void setUp() {}
void tearDown() {}

// Matter transitionTime is tenths of a second; the loop works in ms.
void test_tenths_to_ms() {
    TEST_ASSERT_EQUAL_UINT32(0, transitionTenthsToMs(0));
    TEST_ASSERT_EQUAL_UINT32(100, transitionTenthsToMs(1));   // 0.1s
    TEST_ASSERT_EQUAL_UINT32(3000, transitionTenthsToMs(30)); // 3.0s
}

// The curve is pinned at the endpoints and symmetric about its midpoint.
void test_ease_curve_shape() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, easeInOutCubic(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, easeInOutCubic(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, easeInOutCubic(0.5f)); // midpoint halfway
    TEST_ASSERT_EQUAL_FLOAT(0.0f, easeInOutCubic(-1.0f));         // clamped
    TEST_ASSERT_EQUAL_FLOAT(1.0f, easeInOutCubic(2.0f));          // clamped
    // Ease-in: first quarter covers less ground than a linear ramp would.
    TEST_ASSERT_TRUE(easeInOutCubic(0.25f) < 0.25f);
    // Ease-out: symmetric — f(t) + f(1-t) == 1.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f,
        easeInOutCubic(0.25f) + easeInOutCubic(0.75f));
}

// transitionTime == 0 must behave exactly like the old direct-apply path.
void test_zero_duration_snaps() {
    EasedU8 b;
    b.snap(10);
    b.start(200, /*durationMs=*/0, /*nowMs=*/1000);
    TEST_ASSERT_FALSE(b.isActive());
    TEST_ASSERT_EQUAL_UINT8(200, b.value());
    TEST_ASSERT_EQUAL_UINT8(200, b.advance(5000)); // stays put
}

// A target equal to the current value is a no-op, not a spurious transition.
void test_same_target_is_noop() {
    EasedU8 b;
    b.snap(128);
    b.start(128, 1000, 0);
    TEST_ASSERT_FALSE(b.isActive());
    TEST_ASSERT_EQUAL_UINT8(128, b.value());
}

// The core contract: eased, monotonic, and lands *exactly* on the target.
void test_eased_progress_is_monotonic_and_exact() {
    EasedU8 b;
    b.snap(0);
    b.start(255, /*durationMs=*/1000, /*nowMs=*/0);
    TEST_ASSERT_TRUE(b.isActive());

    uint8_t prev = 0;
    for (uint32_t t = 0; t <= 1000; t += 50) {
        uint8_t v = b.advance(t);
        TEST_ASSERT_TRUE(v >= prev);      // never goes backwards
        prev = v;
    }
    // At/after the window it is pinned to the target and inactive.
    TEST_ASSERT_EQUAL_UINT8(255, b.advance(1000));
    TEST_ASSERT_FALSE(b.isActive());
    TEST_ASSERT_EQUAL_UINT8(255, b.advance(1200)); // past the end, still exact
}

// Fading down works symmetrically and clamps at 0.
void test_fade_down_hits_zero() {
    EasedU8 b;
    b.snap(200);
    b.start(0, 1000, 0);
    uint8_t prev = 200;
    for (uint32_t t = 0; t <= 1000; t += 100) {
        uint8_t v = b.advance(t);
        TEST_ASSERT_TRUE(v <= prev);      // monotonically down
        prev = v;
    }
    TEST_ASSERT_EQUAL_UINT8(0, b.advance(1000));
    TEST_ASSERT_FALSE(b.isActive());
}

// Ease-in-out midpoint: halfway through the window we are ~halfway in value.
void test_midpoint_is_halfway() {
    EasedU8 b;
    b.snap(0);
    b.start(200, 1000, 0);
    uint8_t mid = b.advance(500);
    TEST_ASSERT_INT_WITHIN(2, 100, mid);  // ~half of 200
}

// Re-targeting mid-flight eases from the *current* value — no visible jump.
void test_retarget_midflight_no_jump() {
    EasedU8 b;
    b.snap(0);
    b.start(200, 1000, 0);
    uint8_t mid = b.advance(500);          // somewhere around 100
    b.start(50, 1000, 500);                // change of mind, from `mid`
    TEST_ASSERT_EQUAL_UINT8(mid, b.value()); // starts exactly where we were
    TEST_ASSERT_EQUAL_UINT8(50, b.advance(1500));
    TEST_ASSERT_FALSE(b.isActive());
}

// progress() reports LINEAR time fraction (for a UI countdown), independent of
// the eased value curve — so it must differ from the eased value at the midpoint
// of an asymmetric segment, and read 1.0 once settled.
void test_progress_is_linear_time_not_eased_value() {
    EasedU8 b;
    b.snap(0);
    b.start(200, 1000, 0);
    b.advance(250);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, b.progress(250)); // linear: quarter of the time
    b.advance(750);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, b.progress(750));
    // Settled → 1.0, and an idle (snapped) interpolator also reads 1.0.
    b.advance(1000);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, b.progress(1000));
    EasedU8 idle;
    idle.snap(128);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, idle.progress(500));
}

// nowMs captured before startMs_ (a start() applied later in the same frame)
// must clamp elapsed to 0, not underflow into an instant completion.
void test_now_before_start_clamps() {
    EasedU8 b;
    b.snap(0);
    b.start(255, 1000, /*nowMs=*/1000);
    uint8_t v = b.advance(/*nowMs=*/900);  // 100ms "before" start
    TEST_ASSERT_EQUAL_UINT8(0, v);         // clamped, not completed
    TEST_ASSERT_TRUE(b.isActive());
}

// --- Transition (bare timer) + lerp helpers, used for param/color easing ---

void test_transition_timer_lifecycle() {
    Transition tr;
    TEST_ASSERT_FALSE(tr.isActive());
    tr.start(0, 0);                       // zero duration = snap, never active
    TEST_ASSERT_FALSE(tr.isActive());

    tr.start(1000, 0);
    TEST_ASSERT_TRUE(tr.isActive());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tr.eased(0));      // t=0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, tr.linearProgress(500));
    TEST_ASSERT_TRUE(tr.eased(500) < 0.5f + 0.001f);         // eased <= linear-ish
    // Reaching the end returns 1.0 and auto-deactivates.
    TEST_ASSERT_EQUAL_FLOAT(1.0f, tr.eased(1000));
    TEST_ASSERT_FALSE(tr.isActive());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, tr.eased(1200));           // idle -> 1.0
}

void test_transition_now_before_start_clamps() {
    Transition tr;
    tr.start(1000, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tr.eased(900));   // clamped, still active
    TEST_ASSERT_TRUE(tr.isActive());
}

void test_lerp_u8_endpoints_and_rounding() {
    TEST_ASSERT_EQUAL_UINT8(0,   lerpU8(0, 200, 0.0f));      // exact start
    TEST_ASSERT_EQUAL_UINT8(200, lerpU8(0, 200, 1.0f));      // exact end
    TEST_ASSERT_EQUAL_UINT8(100, lerpU8(0, 200, 0.5f));      // rounded midpoint
    TEST_ASSERT_EQUAL_UINT8(100, lerpU8(200, 0, 0.5f));      // symmetric downward
    TEST_ASSERT_EQUAL_UINT8(255, lerpU8(255, 255, 0.3f));    // no drift when equal
}

void test_lerp_f32() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lerpF32(0.0f, 1.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, lerpF32(0.0f, 1.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, lerpF32(0.0f, 1.0f, 0.25f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tenths_to_ms);
    RUN_TEST(test_ease_curve_shape);
    RUN_TEST(test_zero_duration_snaps);
    RUN_TEST(test_same_target_is_noop);
    RUN_TEST(test_eased_progress_is_monotonic_and_exact);
    RUN_TEST(test_fade_down_hits_zero);
    RUN_TEST(test_midpoint_is_halfway);
    RUN_TEST(test_retarget_midflight_no_jump);
    RUN_TEST(test_progress_is_linear_time_not_eased_value);
    RUN_TEST(test_now_before_start_clamps);
    RUN_TEST(test_transition_timer_lifecycle);
    RUN_TEST(test_transition_now_before_start_clamps);
    RUN_TEST(test_lerp_u8_endpoints_and_rounding);
    RUN_TEST(test_lerp_f32);
    return UNITY_END();
}
