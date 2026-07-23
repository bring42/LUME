// Minimal host-side FastLED stub for native unit tests.
// Defines just enough of CRGB / palettes / draw primitives / the CFastLED
// driver to *compile* the pure-logic headers and controller.cpp off-device.
// NOT a real driver: drawing functions and the FastLED object are no-ops; the
// persistence/codec tests never render, they only exercise C++ logic.
#ifndef LUME_TEST_FASTLED_STUB_H
#define LUME_TEST_FASTLED_STUB_H

#include <cstdint>
#include <cstring>
#include "Arduino.h"  // real FastLED.h pulls in Arduino.h (min/max/millis); mirror that

typedef uint8_t  fract8;
typedef uint16_t fract16;

struct CHSV;  // fwd-declare: CRGB has a converting ctor from CHSV (P1.4 rainbow())

struct CRGB {
    uint8_t r, g, b;

    CRGB() = default;  // trivial -> usable as a union member (matches FastLED)
    constexpr CRGB(uint8_t ir, uint8_t ig, uint8_t ib) : r(ir), g(ig), b(ib) {}
    CRGB(const CHSV&);  // defined out-of-line once CHSV is complete (below)
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

    CRGB& nscale8(uint8_t) { return *this; }
    CRGB& fadeToBlackBy(uint8_t) { return *this; }  // per-pixel fade (P1.4 fade())
    CRGB& operator+=(const CRGB&) { return *this; }
    CRGB& operator|=(const CRGB&) { return *this; }
};

struct CHSV {
    uint8_t h, s, v;
    CHSV() = default;
    constexpr CHSV(uint8_t ih, uint8_t is, uint8_t iv) : h(ih), s(is), v(iv) {}
};

// CHSV -> CRGB conversion (no-op values; tests never render). Defined here now
// that CHSV is complete.
inline CRGB::CRGB(const CHSV&) : r(0), g(0), b(0) {}

struct CRGBPalette16 {
    CRGB entries[16];
    CRGBPalette16() {}  // user-provided so `const CRGBPalette16 X;` globals are legal
    // FastLED builds a palette from up to 16 anchor colors; the core's
    // getPalette() uses the 4-color form. Defaulted args cover 1..16.
    CRGBPalette16(CRGB c00, CRGB c01 = CRGB(), CRGB c02 = CRGB(), CRGB c03 = CRGB(),
                  CRGB c04 = CRGB(), CRGB c05 = CRGB(), CRGB c06 = CRGB(), CRGB c07 = CRGB(),
                  CRGB c08 = CRGB(), CRGB c09 = CRGB(), CRGB c10 = CRGB(), CRGB c11 = CRGB(),
                  CRGB c12 = CRGB(), CRGB c13 = CRGB(), CRGB c14 = CRGB(), CRGB c15 = CRGB()) {
        entries[0] = c00;
        (void)c01; (void)c02; (void)c03; (void)c04; (void)c05; (void)c06; (void)c07;
        (void)c08; (void)c09; (void)c10; (void)c11; (void)c12; (void)c13; (void)c14; (void)c15;
    }
};

// FastLED predefined palettes (declared only; never ODR-used by the tests).
extern const CRGBPalette16 RainbowColors_p;
extern const CRGBPalette16 LavaColors_p;
extern const CRGBPalette16 OceanColors_p;
extern const CRGBPalette16 PartyColors_p;
extern const CRGBPalette16 ForestColors_p;
extern const CRGBPalette16 CloudColors_p;
extern const CRGBPalette16 HeatColors_p;

enum TBlendType { NOBLEND = 0, LINEARBLEND = 1 };

// --- Draw primitives referenced by segment_view.h inline methods. No-ops:
//     they only need to exist for name lookup / linking; tests don't render. ---
inline void fill_solid(CRGB*, int, CRGB) {}
inline void fadeToBlackBy(CRGB*, int, uint8_t) {}
inline void blur1d(CRGB*, int, fract8) {}
inline void nblend(CRGB*, CRGB*, int, fract8) {}
inline void fill_gradient_RGB(CRGB*, int, CRGB, CRGB) {}
inline void fill_rainbow(CRGB*, int, uint8_t, int = 5) {}
inline CRGB ColorFromPalette(const CRGBPalette16&, uint8_t, uint8_t = 255,
                             TBlendType = LINEARBLEND) { return CRGB(); }
inline CRGB HeatColor(uint8_t) { return CRGB(); }
inline CRGB blend(const CRGB&, const CRGB&, fract8) { return CRGB(); }
inline uint16_t scale16by8(uint16_t i, uint8_t scale) {
    return (uint16_t)(((uint32_t)i * scale) >> 8);
}
inline uint8_t scale8(uint8_t i, uint8_t scale) {
    // Match real FastLED's default (SCALE8_FIXED): the (scale + 1) term makes
    // scale8(i, 255) == i exactly, so this fake never disagrees with on-device
    // math by one. Without it, scale8(42, 255) would be 41.
    return (uint8_t)(((uint16_t)i * (uint16_t)(scale + 1)) >> 8);
}
inline uint8_t map8(uint8_t in, uint8_t rangeStart, uint8_t rangeEnd) {
    return rangeStart + scale8(in, rangeEnd - rangeStart);
}

// Perceptual gamma encode, matching FastLED's colorutils applyGamma_video:
// (b/255)^gamma * 255, never dropping a positive input to zero.
inline uint8_t applyGamma_video(uint8_t brightness, float gamma) {
    float adj = powf((float)brightness / 255.0f, gamma) * 255.0f;
    uint8_t result = (uint8_t)adj;
    if (brightness > 0 && result == 0) result = 1;
    return result;
}

// --- Driver: chipset templates, color correction, and the CFastLED object. ---
enum EOrder { RGB = 0x012, RBG = 0x021, GRB = 0x102, GBR = 0x120, BRG = 0x201, BGR = 0x210 };

enum LEDColorCorrection : uint32_t {
    TypicalLEDStrip = 0xFFB0F0,
    UncorrectedColor = 0xFFFFFF,
};

template <uint8_t DATA_PIN, EOrder RGB_ORDER = GRB> class WS2812B {};

class CLEDController {};

class CFastLED {
public:
    template <template <uint8_t, EOrder> class CHIPSET, uint8_t DATA_PIN, EOrder RGB_ORDER>
    CLEDController& addLeds(CRGB*, int) { static CLEDController c; return c; }

    void setBrightness(uint8_t) {}
    template <typename T> void setCorrection(T) {}  // accepts CRGB or a correction enum
    void setMaxPowerInVoltsAndMilliamps(uint8_t, uint32_t) {}
    void clear(bool = false) {}
    void show() {}
};

extern CFastLED FastLED;

#endif // LUME_TEST_FASTLED_STUB_H
