// Minimal host-side FastLED stub for native unit tests.
// Defines just enough of CRGB / CRGBPalette16 to compile the pure-logic
// headers (param_schema.h, param_codec.h) off-device. NOT a real driver.
#ifndef LUME_TEST_FASTLED_STUB_H
#define LUME_TEST_FASTLED_STUB_H

#include <cstdint>
#include <cstring>

struct CRGB {
    uint8_t r, g, b;

    CRGB() = default;  // trivial -> usable as a union member (matches FastLED)
    constexpr CRGB(uint8_t ir, uint8_t ig, uint8_t ib) : r(ir), g(ig), b(ib) {}
    constexpr CRGB(uint32_t code)
        : r((code >> 16) & 0xFF), g((code >> 8) & 0xFF), b(code & 0xFF) {}

    enum HTMLColorCode : uint32_t {
        Black = 0x000000, Red = 0xFF0000, Green = 0x008000,
        Blue = 0x0000FF, White = 0xFFFFFF,
    };
    constexpr CRGB(HTMLColorCode c)
        : r((static_cast<uint32_t>(c) >> 16) & 0xFF),
          g((static_cast<uint32_t>(c) >> 8) & 0xFF),
          b(static_cast<uint32_t>(c) & 0xFF) {}
};

struct CRGBPalette16 {
    CRGB entries[16];
};

#endif // LUME_TEST_FASTLED_STUB_H
