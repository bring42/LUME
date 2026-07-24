#pragma once

#include <stdint.h>
#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// render16.h — the 16-bit-per-channel render substrate for premium light modes.
//
// The premium modes spec is uncompromising: "nothing may ever visibly step,
// snap, or scroll uniformly." Plain 8-bit content can't honour that — at low
// brightness the codes are too coarse (1→0 is a visible jump) and per-pixel
// motion lands on whole-LED steps. So modes render into a *linear* 16-bit field
// and this header owns the final descent to the 8-bit WS2812B wire:
//
//     compose (content×brightness×warmth)  →  IIR smooth  →  gamma16  →  dither 16→8
//
// The IIR sits in LINEAR (pre-gamma) space and runs at show() rate, not the
// effect-logic tick — smoothing in gamma space makes fades feel subtly wrong,
// and tying it to the animation rate defeats the "nothing steps" guarantee.
//
// Everything here is pure integer math with NO FastLED dependency (mirroring
// core/transition.h), so it is fully host-tested in `pio test -e native`
// (test_render16) with zero hardware. Hardware-specific final steps — the
// WS2812B colour correction and the LED_MIN_OUTPUT red-floor — stay in the
// output driver, downstream of the 8-bit values this produces.
//
// Design notes:
//  • Linear space. Content, compositing, master brightness, and dim-to-warm all
//    happen in LINEAR 16-bit. Gamma is applied ONCE, per-pixel, right before the
//    dither — never twice, never on an 8-bit LUT. (γ(a)·γ(b) ≠ γ(a·b), so the
//    old global gamma-on-brightness must NOT also run on this path; that
//    reconciliation lives in the controller.) Gamma is pinned at 0 exactly and
//    reaches full within a few 16-bit codes at the very top (8-bit interpolation
//    fraction) — imperceptible, and full-off/full-on both read clean.
//  • Smoothing is the "nothing steps" guarantee: every output channel
//    exponentially approaches its target every show() (hundreds of Hz), so mode
//    changes, power ramps, and any residual quantisation all melt.
//  • Dither diffuses the fractional 16→8 remainder across frames (temporal);
//    the caller seeds a per-pixel phase so it also averages spatially.
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

// 16-bit-per-channel linear colour. 0 = off, 65535 = full.
struct CRGB16 {
    uint16_t r = 0;
    uint16_t g = 0;
    uint16_t b = 0;

    CRGB16() = default;
    CRGB16(uint16_t r_, uint16_t g_, uint16_t b_) : r(r_), g(g_), b(b_) {}

    // Expand an 8-bit channel to full 16-bit range: 0→0, 255→65535 (×257).
    static uint16_t expand8(uint8_t c) { return (uint16_t)((c << 8) | c); }
    static CRGB16 fromRGB8(uint8_t r_, uint8_t g_, uint8_t b_) {
        return CRGB16(expand8(r_), expand8(g_), expand8(b_));
    }

    bool operator==(const CRGB16& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const CRGB16& o) const { return !(*this == o); }
};

// Scale a 16-bit channel by an 8-bit factor: 0 → 0 (full off), 255 → ~identity
// (within 1 code). This is the per-pixel-per-channel hot path at show() rate, so
// it must avoid division: (v·s·257)>>16 approximates v·s/255 (257/65536 ≈ 1/255)
// with a multiply and a shift. 0 stays exactly 0 (full dark matters); the top
// lands at most one 16-bit code short — imperceptible, and the IIR settles it.
inline uint16_t scale16by8(uint16_t v, uint8_t s) {
    return (uint16_t)(((uint32_t)v * s * 257u + 32768u) >> 16);
}

// Scale a 16-bit channel by a 16-bit factor (0..65535), 65535 == identity.
inline uint16_t scale16(uint16_t v, uint16_t s) {
    return (uint16_t)(((uint32_t)v * (s + 1)) >> 16);
}

// Linear interpolate two 16-bit channels; frac 0 → a, 65535 → b.
inline uint16_t lerp16(uint16_t a, uint16_t b, uint16_t frac) {
    return (int32_t)a + (((int32_t)b - (int32_t)a) * (int32_t)frac >> 16);
}

inline CRGB16 blend16(const CRGB16& a, const CRGB16& b, uint16_t frac) {
    return CRGB16(lerp16(a.r, b.r, frac), lerp16(a.g, b.g, frac), lerp16(a.b, b.b, frac));
}

// Scale a whole colour by one 8-bit factor (0 → black, 255 → identity).
inline CRGB16 scale(const CRGB16& c, uint8_t s) {
    return CRGB16(scale16by8(c.r, s), scale16by8(c.g, s), scale16by8(c.b, s));
}

// Per-channel 8-bit gain — used for dim-to-warm and WS2812B white-balance
// correction, both of which are per-channel scales.
inline CRGB16 scaleRGB(const CRGB16& c, uint8_t rs, uint8_t gs, uint8_t bs) {
    return CRGB16(scale16by8(c.r, rs), scale16by8(c.g, gs), scale16by8(c.b, bs));
}

// ── 16-bit gamma ────────────────────────────────────────────────────────────
// A 257-entry curve over the normalized domain, interpolated for a smooth
// 16-bit transfer. Built once when gamma changes (uses powf); lookup is a shift,
// a multiply, and an add — cheap enough for per-pixel-per-channel at show() rate.
class Gamma16 {
public:
    Gamma16() { build(2.2f); }
    explicit Gamma16(float gamma) { build(gamma); }

    void build(float gamma) {
        gamma_ = gamma;
        for (int i = 0; i <= 256; i++) {
            double n = (double)i / 256.0;
            double y = pow(n, (double)gamma);
            long v = lround(y * 65535.0);
            if (v < 0) v = 0;
            if (v > 65535) v = 65535;
            lut_[i] = (uint16_t)v;
        }
    }

    float gamma() const { return gamma_; }

    // Map a linear 16-bit value to its gamma-encoded 16-bit value.
    uint16_t apply(uint16_t v) const {
        uint8_t  hi = (uint8_t)(v >> 8);   // segment index 0..255
        uint8_t  lo = (uint8_t)(v & 0xFF); // interpolation fraction
        uint16_t a  = lut_[hi];
        uint16_t b  = lut_[hi + 1];
        return (uint16_t)(a + (((uint32_t)(b - a) * lo) >> 8));
    }

    CRGB16 apply(const CRGB16& c) const {
        return CRGB16(apply(c.r), apply(c.g), apply(c.b));
    }

private:
    float    gamma_ = 2.2f;
    uint16_t lut_[257];
};

// ── One-pole IIR smoothing ────────────────────────────────────────────────────
// state exponentially approaches target: state += (target - state) >> shift.
// Larger shift == slower/smoother. A guaranteed min step of 1 toward a non-equal
// target means the filter always SETTLES exactly (plain >>shift would stall one
// code short forever). This is the layer that makes the strip feel analog.
inline void iirStep(uint16_t& state, uint16_t target, uint8_t shift) {
    int32_t d = (int32_t)target - (int32_t)state;
    if (d == 0) return;
    int32_t step = d >> shift;          // arithmetic shift; toward target
    if (step == 0) step = (d > 0) ? 1 : -1;
    state = (uint16_t)((int32_t)state + step);
}

inline void iirStep(CRGB16& state, const CRGB16& target, uint8_t shift) {
    iirStep(state.r, target.r, shift);
    iirStep(state.g, target.g, shift);
    iirStep(state.b, target.b, shift);
}

// ── Temporal error-diffusion dither, 16-bit → 8-bit ───────────────────────────
// The low byte of the 16-bit value is the sub-code remainder. Accumulate it in
// `err` across frames; when it overflows a full code, bump the 8-bit output.
// Over many frames the average output equals the true 16-bit value / 256, so a
// value between two 8-bit codes reads as a stable in-between brightness instead
// of dead-flat quantisation. Seed `err` with a per-pixel phase (see ditherSeed)
// so neighbouring pixels flip on different frames — spatial averaging too.
inline uint8_t dither16to8(uint16_t v, uint16_t& err) {
    uint32_t x   = (uint32_t)v + (err & 0xFF);
    uint16_t out = (uint16_t)(x >> 8);
    err = (uint16_t)(x & 0xFF);
    return out > 255 ? 255 : (uint8_t)out;
}

// A decorrelated per-pixel starting phase for the dither accumulator, so the
// 16→8 flips are spread across space, not synchronized into a visible shimmer.
inline uint16_t ditherSeed(uint16_t pixelIndex) {
    return (uint16_t)((pixelIndex * 61u) & 0xFF);  // 61: small coprime-ish stride
}

}  // namespace lume
