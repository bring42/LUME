// Native (host-compiled) tests for the Region geometry seam (P1.3).
//
// Region is the one value type coverage, persistence, and the view all speak.
// These guard its arithmetic (contains/stop/end/empty) and prove SegmentView
// addresses pixels through it correctly — including reversal, which stays in the
// view, not the Region. Runs in CI via `pio test -e native`, compiled against
// the host stubs in test/stubs (FastLED).
#include <unity.h>

#include "core/region.h"
#include "core/segment_view.h"

using namespace lume;

void setUp() {}
void tearDown() {}

// The interval arithmetic the coverage loop and serializer depend on.
void test_region_interval_math() {
    Region r(3, 10);           // pixels [3, 13)
    TEST_ASSERT_EQUAL_UINT16(3, r.start);
    TEST_ASSERT_EQUAL_UINT16(10, r.size());
    TEST_ASSERT_EQUAL_UINT16(13, r.end());   // exclusive upper bound
    TEST_ASSERT_EQUAL_UINT16(12, r.stop());  // inclusive last index (matches serializer)
    TEST_ASSERT_FALSE(r.empty());
}

// contains() is the clearUncoveredLeds membership primitive: half-open [start,end).
void test_region_contains_is_half_open() {
    Region r(3, 10);           // covers 3..12
    TEST_ASSERT_FALSE(r.contains(2));   // just below
    TEST_ASSERT_TRUE(r.contains(3));    // first covered
    TEST_ASSERT_TRUE(r.contains(12));   // last covered
    TEST_ASSERT_FALSE(r.contains(13));  // end() is exclusive
}

// An empty region covers nothing and must not underflow stop() to 65535.
void test_empty_region() {
    Region r;
    TEST_ASSERT_TRUE(r.empty());
    TEST_ASSERT_EQUAL_UINT16(0, r.size());
    TEST_ASSERT_EQUAL_UINT16(0, r.stop());   // guarded: not start-1
    TEST_ASSERT_FALSE(r.contains(0));
}

// SegmentView exposes its Region and addresses pixels through it.
void test_view_reports_its_region() {
    CRGB16 buf[32];
    SegmentView view(buf, /*start=*/5, /*len=*/8, /*reversed=*/false);
    const Region& r = view.getRegion();
    TEST_ASSERT_EQUAL_UINT16(5, r.start);
    TEST_ASSERT_EQUAL_UINT16(8, r.size());
    TEST_ASSERT_EQUAL_UINT16(5, view.getStart());
    TEST_ASSERT_EQUAL_UINT16(8, view.size());
    TEST_ASSERT_TRUE(view.valid());
    TEST_ASSERT_EQUAL_PTR(&buf[5], &view[0]);  // first logical pixel maps to base + start
}

// Reversal lives in the view, not the Region: the region is unchanged, but
// operator[] maps forward index -> reversed physical pixel.
void test_view_reversal_maps_through_region() {
    CRGB16 buf[32];
    // Forward view over [10,14): view[0] -> buf[10], view[3] -> buf[13].
    SegmentView fwd(buf, 10, 4, /*reversed=*/false);
    TEST_ASSERT_EQUAL_PTR(&buf[10], &fwd[0]);
    TEST_ASSERT_EQUAL_PTR(&buf[13], &fwd[3]);

    // Reversed view over the same region: view[0] -> buf[13], view[3] -> buf[10].
    SegmentView rev(buf, 10, 4, /*reversed=*/true);
    TEST_ASSERT_EQUAL_UINT16(10, rev.getRegion().start);  // region identical...
    TEST_ASSERT_EQUAL_UINT16(4, rev.getRegion().size());
    TEST_ASSERT_EQUAL_PTR(&buf[13], &rev[0]);             // ...only indexing flips
    TEST_ASSERT_EQUAL_PTR(&buf[10], &rev[3]);
}

// P1.4: the canvas primitives must write through operator[], not a flat raw()
// pointer. A reversed view proves it: logical index i -> physical (len-1-i), so a
// partial fill of the logical head lands on the physical tail. Had fill() used a
// raw contiguous pointer (the removed escape hatch) it would ignore reversal and
// paint the physical head instead.
void test_fill_is_remap_safe() {
    CRGB16 buf[5] = {};
    SegmentView rev(buf, /*start=*/0, /*len=*/5, /*reversed=*/true);
    rev.fill(CRGB16(65535, 0, 0), /*offset=*/0, /*count=*/2);  // logical pixels 0,1
    TEST_ASSERT_EQUAL_UINT16(65535, buf[4].r);  // logical 0 -> physical 4
    TEST_ASSERT_EQUAL_UINT16(65535, buf[3].r);  // logical 1 -> physical 3
    TEST_ASSERT_EQUAL_UINT16(0, buf[2].r);      // logical 2 untouched
    TEST_ASSERT_EQUAL_UINT16(0, buf[0].r);      // physical head untouched
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_region_interval_math);
    RUN_TEST(test_region_contains_is_half_open);
    RUN_TEST(test_empty_region);
    RUN_TEST(test_view_reports_its_region);
    RUN_TEST(test_view_reversal_maps_through_region);
    RUN_TEST(test_fill_is_remap_safe);
    return UNITY_END();
}
