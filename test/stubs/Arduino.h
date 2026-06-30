// Minimal host-side Arduino stub for native unit tests.
// Provides the handful of Arduino/ESP globals the core + logging headers name
// (millis, min/max, Serial, ESP). All no-ops or trivial: the tests exercise
// logic, not timing or I/O.
#ifndef LUME_TEST_ARDUINO_STUB_H
#define LUME_TEST_ARDUINO_STUB_H

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>

using std::min;
using std::max;

// Monotonic clock that advances on each read, so a single LumeController::update()
// always clears its frame-rate gate and drains the command queue in tests. The
// step is far larger than any frame interval; tests don't assert on time.
inline uint32_t millis() {
    static uint32_t t = 0;
    t += 1000;
    return t;
}

// Serial: swallow everything. Logging routes here.
struct SerialStub {
    void begin(unsigned long) {}
    void print(const char*) {}
    void println(const char* = "") {}
    void printf(const char*, ...) {}
};
inline SerialStub Serial;

// ESP heap accessors used by logMemoryStats(); return plausible constants.
struct EspStub {
    uint32_t getFreeHeap() { return 0; }
    uint32_t getMaxAllocHeap() { return 0; }
    uint32_t getMinFreeHeap() { return 0; }
    uint32_t getFreePsram() { return 0; }
};
inline EspStub ESP;

#endif // LUME_TEST_ARDUINO_STUB_H
