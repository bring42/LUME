#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

// ─────────────────────────────────────────────────────────────────────────────
// Nocturne — moonlight.
//
// Very dim cool white with slow "clouds" passing: broad luminance dips glide
// along the strip on low-frequency noise, morphing as they go (never a rigid
// uniform scroll — that's the cheap tell). It lives entirely in the bottom
// brightness codes, so it's the dithering showpiece — the 16-bit descent is what
// keeps those near-black levels smooth instead of steppy.
//
// Note the tension with the global dim-to-warm: at these low levels the warm pull
// is strong, so the base colour is pushed deliberately cool/blue to still read as
// moonlight after the pipeline. Phase accumulator (speed = drift rate).
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace nocturne {
    constexpr uint8_t SPEED = 0;
    constexpr uint8_t LEVEL = 1;
}

DEFINE_EFFECT_SCHEMA(nocturneSchema,
    // How fast the clouds drift. Slow by default.
    ParamDesc::Int("speed", "Cloud Drift", 30,  1, 255),
    // Overall moonlight level (it stays dim — this rides the bottom codes).
    ParamDesc::Int("level", "Moonlight",   128, 1, 255)
);

struct NocturneState {
    uint32_t phase;
};

// Cool blue-white moon. Pushed cool on purpose to survive the low-end dim-to-warm.
static const CRGB16 kMoon = CRGB16::fromRGB8(150, 190, 255);

// inoise16 clusters ~[12000,53000]; stretch to 0..65535 for real cloud contrast.
static inline uint16_t stretchNoise(uint32_t n) {
    int32_t v = ((int32_t)n - 12000) * 65535 / 41000;
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    return (uint16_t)v;
}

void effectNocturne(SegmentView& view, const ParamValues& params,
                    uint32_t frame, bool firstFrame) {
    (void)frame;
    NocturneState* s = view.getScratchpad<NocturneState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    uint8_t speed = params.getInt(nocturne::SPEED);
    uint8_t level = params.getInt(nocturne::LEVEL);
    s->phase += (uint32_t)speed * 4u;

    // Moonlight brightness range (deliberately low — bottom codes). `level` sets
    // the peak; clouds dip to ~a quarter of it.
    float kMax = 0.06f + ((float)level / 255.0f) * 0.12f;   // ~6–18 % peak
    float kMin = kMax * 0.25f;

    for (uint16_t i = 0; i < view.size(); i++) {
        // Broad clouds (small spatial scale) that GLIDE (+phase on x) and MORPH
        // (phase>>3 on y) — motion without a rigid uniform scroll.
        uint32_t x = (uint32_t)i * 1400u + s->phase;
        float nf = (float)stretchNoise(inoise16(x, s->phase >> 3)) / 65535.0f;
        float lum = kMin + (kMax - kMin) * nf;
        view[i] = scale(kMoon, (uint8_t)(lum * 255.0f));
    }
}

// Broad clouds along the strip — 1D adjacency.
REGISTER_EFFECT_SCHEMA(effectNocturne, "nocturne", "Nocturne", Animated,
                       nocturneSchema, sizeof(NocturneState));

}  // namespace lume
