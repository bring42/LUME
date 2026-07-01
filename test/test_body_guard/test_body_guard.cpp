// Native (host-compiled) tests for the chunked-body assembly guard (P0.3).
// The guard is pure logic (no Arduino/AsyncWebServer), so it unit-tests directly.
#include <unity.h>
#include "core/body_guard.h"

using namespace lume;

static const void* A = reinterpret_cast<const void*>(1);
static const void* B = reinterpret_cast<const void*>(2);

void setUp() {}
void tearDown() {}

// The owner may re-claim across chunks; end() frees the slot.
void test_single_owner_and_reentry() {
    BodyGuard g;
    TEST_ASSERT_TRUE(g.begin(A, 1000));   // claim on first chunk
    TEST_ASSERT_TRUE(g.begin(A, 1000));   // same request, next chunk -> still granted
    g.end(A);
    TEST_ASSERT_TRUE(g.begin(A, 2000));   // free again after end
}

// A second concurrent body is rejected (caller sends 409) until the first ends.
void test_second_owner_rejected() {
    BodyGuard g;
    TEST_ASSERT_TRUE(g.begin(A, 1000));
    TEST_ASSERT_FALSE(g.begin(B, 1000));  // busy
    g.end(B);                             // wrong token -> no-op
    TEST_ASSERT_FALSE(g.begin(B, 1000));  // still owned by A
    g.end(A);
    TEST_ASSERT_TRUE(g.begin(B, 1000));   // now free
}

// A client that disconnects mid-body never calls end(); the stale claim is
// reclaimed after the timeout instead of wedging the slot forever.
void test_stale_claim_reclaimed() {
    BodyGuard g;
    TEST_ASSERT_TRUE(g.begin(A, 1000));                                  // A claims, then vanishes
    TEST_ASSERT_FALSE(g.begin(B, 1000 + BodyGuard::kTimeoutMs - 1));     // not stale yet
    TEST_ASSERT_TRUE(g.begin(B, 1000 + BodyGuard::kTimeoutMs));          // stale -> reclaimed by B
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_single_owner_and_reentry);
    RUN_TEST(test_second_owner_rejected);
    RUN_TEST(test_stale_claim_reclaimed);
    return UNITY_END();
}
