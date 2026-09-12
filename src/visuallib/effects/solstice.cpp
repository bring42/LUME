#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

// ─────────────────────────────────────────────────────────────────────────────
// Solstice — a slow circadian arc.
//
// The whole strip drifts along the blackbody curve over 10+ minutes: candlelight
// → warm white → cool daylight → back down. Colours travel the blackbody locus
// ONLY (a chain of real colour-temperature anchors, interpolated), so it never
// wanders through green or magenta. A subtle brightness lift toward daylight
// gives it the feel of a day rising and setting.
//
// Uniform across the strip; phase is an accumulator (speed = arc rate). The
// period is long enough that a full arc is 10+ minutes at the default — crank
// Arc Speed to preview it, then set it slow to live with.
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace solstice {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(solsticeSchema,
    // Arc rate. Default ~10 min for a full candle→day→candle cycle; higher is
    // quicker (down to ~1.5 min at max) — handy for previewing the arc.
    ParamDesc::Int("speed", "Arc Speed", 40, 1, 255)
);

struct SolsticeState {
    uint32_t phase;
};

// Blackbody anchors, warm → cool (approx sRGB for the colour temperature). Staying
// ON these keeps the arc on the locus — lerping adjacent anchors never detours
// into green/magenta.
static const CRGB16 kBody[] = {
    CRGB16::fromRGB8(255, 120,  20),   // ~1900 K candlelight
    CRGB16::fromRGB8(255, 160,  70),   // ~2500 K
    CRGB16::fromRGB8(255, 190, 120),   // ~3200 K warm white
    CRGB16::fromRGB8(255, 220, 180),   // ~4500 K
    CRGB16::fromRGB8(255, 240, 220),   // ~5800 K daylight
    CRGB16::fromRGB8(255, 250, 245),   // ~6800 K cool
};
static constexpr int kBodyN = sizeof(kBody) / sizeof(kBody[0]);

// Sample the blackbody chain at t in [0,1] (0 = warmest, 1 = coolest).
static CRGB16 blackbody(float t) {
    if (t <= 0.0f) return kBody[0];
    if (t >= 1.0f) return kBody[kBodyN - 1];
    float f = t * (float)(kBodyN - 1);
    int   i = (int)f;
    uint16_t frac = (uint16_t)((f - (float)i) * 65535.0f);
    return blend16(kBody[i], kBody[i + 1], frac);
}

// Full arc length in phase units. period_frames = kCycle / advance, and advance is
// the Arc Speed param; kCycle is sized so the default (speed 40) is ~10 min at ~58 fps.
static constexpr uint32_t kCycle = 1400000u;

void effectSolstice(SegmentView& view, const ParamValues& params,
                    uint32_t frame, bool firstFrame) {
    (void)frame;
    SolsticeState* s = view.getScratchpad<SolsticeState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    uint8_t speed = params.getInt(solstice::SPEED);
    s->phase += (speed == 0) ? 1u : (uint32_t)speed;

    float p   = (float)(s->phase % kCycle) / (float)kCycle;   // 0..1 around the day
    float arc = 0.5f * (1.0f - cosf(6.2831853f * p));         // warm → cool → warm

    CRGB16 col = blackbody(arc);
    // Subtle circadian brightness: dimmer at candlelight, up toward daylight.
    uint8_t level = (uint8_t)((0.75f + 0.25f * arc) * 255.0f);
    view.fill(scale(col, level));
}

// Uniform colour-temperature — dimension-agnostic and remap-safe.
REGISTER_EFFECT_SCHEMA_DIMS(effectSolstice, "solstice", "Solstice", Animated,
                            solsticeSchema, sizeof(SolsticeState), Any);

}  // namespace lume
