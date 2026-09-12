#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

// ─────────────────────────────────────────────────────────────────────────────
// Breathe — a single hue on a slow, asymmetric breath.
//
// ~10–14 s cycle: a quicker rise, a slight hold at the top, then a long lazy
// fall — the cadence of a calm breath, not a sine. Luminance floats between
// ~20 % and ~60 %; it never reaches zero (so it never "blinks") and never hits
// full. The period drifts slightly on slow noise so it never feels metronomic.
// Uniform across the strip — the whole point is the envelope, so it lives or
// dies on the shape and the never-quite-repeating timing.
//
// Phase is an accumulator (like Hearth): speed sets the breath RATE, so changing
// it is a smooth cadence change, never a jump.
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace breathe {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
}

DEFINE_EFFECT_SCHEMA(breatheSchema,
    ParamDesc::Color("color", "Color", CRGB(255, 120, 40)),   // warm single hue
    // Breath rate. Default ~12 s; higher = quicker breaths.
    ParamDesc::Int("speed", "Breath Speed", 128, 1, 255)
);

struct BreatheState {
    uint32_t phase;   // 16.16-ish breath phase; low 16 bits are the position in-cycle
};

static inline float smoothstep(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

// Asymmetric breath envelope from a 0..1 position in the cycle: quick smooth
// rise, brief hold at the top, long lazy fall. Returns 0..1.
static float breathEnvelope(float p) {
    const float RISE = 0.28f;   // quicker rise
    const float HOLD = 0.10f;   // slight hold at the top
    if (p < RISE)         return smoothstep(p / RISE);
    if (p < RISE + HOLD)  return 1.0f;
    return smoothstep(1.0f - (p - RISE - HOLD) / (1.0f - RISE - HOLD));  // long fall
}

void effectBreathe(SegmentView& view, const ParamValues& params,
                   uint32_t frame, bool firstFrame) {
    (void)frame;
    BreatheState* s = view.getScratchpad<BreatheState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    CRGB   color = params.getColor(breathe::COLOR);
    uint8_t speed = params.getInt(breathe::SPEED);

    // Base advance sets the period: ~12 s at the default speed (speed 128). The
    // top end is steepened (speeds above 160 accelerate) so max is a brisk ~4 s
    // breath, while the default and slow end are unchanged. A slow-noise drift
    // factor (~±¼) makes each breath a little different so it never metronomes.
    uint32_t base = (uint32_t)speed * 3u / 4u;
    if (speed > 160) base += (uint32_t)(speed - 160);   // faster max
    if (base == 0) base = 1;
    uint16_t dn = inoise16(s->phase >> 2, 7777u);           // slowly varying 0..65535
    uint32_t advance = base + ((base * (uint32_t)dn) >> 18); // + up to ~base/4
    s->phase += advance;

    float p   = (float)(s->phase & 0xFFFF) / 65535.0f;      // position in the cycle
    float env = breathEnvelope(p);
    // Float between ~20 % and ~60 % — never zero, never full.
    uint8_t level = (uint8_t)((0.20f + 0.40f * env) * 255.0f);

    view.fill(scale(SegmentView::c16(color), level));
}

// Uniform luminance is dimension-agnostic and remap-safe.
REGISTER_EFFECT_SCHEMA_DIMS(effectBreathe, "breathe", "Breathe", Animated,
                            breatheSchema, sizeof(BreatheState), Any);

}  // namespace lume
