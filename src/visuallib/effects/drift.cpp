#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// Drift — the lava lamp.
//
// Three big soft blobs (1D metaballs) of neighbouring warm hues slowly stretch,
// merge, and split along the strip. Movement so slow it's only noticeable if you
// stare — which people do. Each blob is a smooth field bump; where fields overlap
// they merge into one glow, then part again as the centres drift on slow sines.
// Phase accumulator (speed = drift rate).
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace drift {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(driftSchema,
    ParamDesc::Int("speed", "Drift Speed", 30, 1, 255)
);

struct DriftState { uint32_t phase; };

// Neighbouring warm hues the blobs live in (deep red → orange → rose).
static const CRGB16 kCool = CRGB16::fromRGB8(120, 20, 30);   // dim rose-red (field low)
static const CRGB16 kWarm = CRGB16::fromRGB8(255, 110, 30);  // warm orange (field high)

void effectDrift(SegmentView& view, const ParamValues& params,
                 uint32_t frame, bool firstFrame) {
    (void)frame;
    DriftState* s = view.getScratchpad<DriftState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    uint8_t speed = params.getInt(drift::SPEED);
    s->phase += (uint32_t)speed;

    float n = (float)view.size();
    float t = (float)s->phase / 30000.0f;   // very slow
    float w = n * 0.16f;                     // blob half-width
    if (w < 1.0f) w = 1.0f;

    // Three blob centres drifting on slow, incommensurate sines (so they never
    // lock into a pattern) — they stretch/merge/split as they cross.
    float c0 = n * (0.5f + 0.42f * sinf(t * 0.70f));
    float c1 = n * (0.5f + 0.42f * sinf(t * 1.13f + 2.1f));
    float c2 = n * (0.5f + 0.42f * sinf(t * 0.51f + 4.2f));

    for (uint16_t i = 0; i < n; i++) {
        float fi = (float)i;
        float d0 = (fi - c0) / w, d1 = (fi - c1) / w, d2 = (fi - c2) / w;
        // Smooth metaball field: each blob is 1/(1+d^2); sum and soft-clamp.
        float field = 1.0f / (1.0f + d0 * d0) + 1.0f / (1.0f + d1 * d1)
                    + 1.0f / (1.0f + d2 * d2);
        float f = field > 1.0f ? 1.0f : field;          // 0..1 glow amount
        // Warm hue rises with the field; brightness too (dim between blobs).
        uint16_t mix = (uint16_t)(f * 65535.0f);
        CRGB16 col = blend16(kCool, kWarm, mix);
        float lum = 0.10f + 0.90f * f;
        view[i] = scale(col, (uint8_t)(lum * 255.0f));
    }
}

REGISTER_EFFECT_SCHEMA(effectDrift, "drift", "Drift", Animated,
                       driftSchema, sizeof(DriftState));

}  // namespace lume
