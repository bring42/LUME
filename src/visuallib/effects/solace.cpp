#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// Solace — the one you put on after a rough day.
//
// A very dim amber-rose that breathes in SPACE rather than brightness: a soft
// warm core swells outward from a point along the strip and slowly recedes over
// ~30 s, like the light is holding the room. The core's brightness barely
// changes — what breathes is how far the glow reaches. Never brightens above
// ~35 %. One slow, tidal gesture; distinct from Breathe (a luminance pulse in
// place) and Hearth (rhythmless drift).
//
// Phase is an accumulator (speed = tide RATE), so speed changes stay smooth.
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace solace {
    constexpr uint8_t COLOR    = 0;
    constexpr uint8_t SPEED    = 1;
    constexpr uint8_t POSITION = 2;
}

DEFINE_EFFECT_SCHEMA(solaceSchema,
    ParamDesc::Color("color", "Color", CRGB(255, 90, 70)),   // dim amber-rose
    // Tide rate. Default ~30 s swell/recede; higher = quicker tide.
    ParamDesc::Int("speed",    "Tide Speed", 60,  1, 255),
    // Where the core swells from, along the strip (0 = start, 255 = end).
    ParamDesc::Int("position", "Center",     128, 0, 255)
);

struct SolaceState {
    uint32_t phase;
};

static inline float smoothstep(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

void effectSolace(SegmentView& view, const ParamValues& params,
                  uint32_t frame, bool firstFrame) {
    (void)frame;
    SolaceState* s = view.getScratchpad<SolaceState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    CRGB    color = params.getColor(solace::COLOR);
    uint8_t speed = params.getInt(solace::SPEED);
    uint8_t pos   = params.getInt(solace::POSITION);

    // ~30 s tide at the default speed (speed 60). Accumulate so speed = rate.
    s->phase += (uint32_t)speed * 5u / 8u;

    // Symmetric tidal swell: 0 (contracted) → 1 (fully swelled) → 0, smooth.
    float p   = (float)(s->phase & 0xFFFF) / 65535.0f;
    float env = 0.5f * (1.0f - cosf(6.2831853f * p));

    uint16_t n = view.size();
    float centerPix = (n <= 1) ? 0.0f : (float)pos * (float)(n - 1) / 255.0f;

    // What breathes is the REACH (half-width), from a tight core to most of the
    // strip. Core brightness is ~constant; the base keeps a dim glow everywhere.
    const float kMinWidth  = 2.0f;
    const float kBaseLevel = 0.08f;   // dim amber-rose floor
    const float kCoreAmp   = 0.27f;   // + core → ~0.35 peak (the cap)
    float width = kMinWidth + (0.6f * (float)n - kMinWidth) * env;
    if (width < 1.0f) width = 1.0f;

    for (uint16_t i = 0; i < n; i++) {
        float d = fabsf((float)i - centerPix);
        float g = (d < width) ? smoothstep(1.0f - d / width) : 0.0f;   // soft core
        float level = kBaseLevel + kCoreAmp * g;
        if (level > 0.35f) level = 0.35f;
        view[i] = scale(SegmentView::c16(color), (uint8_t)(level * 255.0f));
    }
}

// Logic uses linear position (a point on the strip) — 1D adjacency.
REGISTER_EFFECT_SCHEMA(effectSolace, "solace", "Solace", Animated,
                       solaceSchema, sizeof(SolaceState));

}  // namespace lume
