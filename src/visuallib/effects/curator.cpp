#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

// ─────────────────────────────────────────────────────────────────────────────
// Curator — the flagship "static mode that isn't."
//
// Tunable warm white that imperceptibly wanders on slow 16-bit noise: the colour
// temperature drifts ~±100 K and the brightness ~±3 % over minutes. Nobody ever
// catches it changing — it reads like daylight settling through a room instead of
// a lamp holding one value. It is deliberately the SIMPLEST premium mode: a
// uniform fill with no motion, so it validates the whole substrate (16-bit
// content → dim-to-warm → IIR → gamma → dither) at the bottom codes where any
// stepping or shimmer would show. If Curator feels analog, the substrate works.
//
// The wander uses inoise16 (not an 8-bit oscillator — those step at these
// speeds). The strip is filled with one 16-bit colour per frame; all gamma /
// brightness / dither happen downstream in the controller's output stage.
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace curator {
    constexpr uint8_t SPEED  = 0;
    constexpr uint8_t WARMTH = 1;
}

DEFINE_EFFECT_SCHEMA(curatorSchema,
    // Slow by design, but the range spans imperceptible → gently visible so the
    // control reads as doing something: low = you never catch it, high = a soft
    // wander you can watch.
    ParamDesc::Int("speed",  "Drift Speed", 24,  1, 255),
    // Higher = warmer: 0 = coolest (soft white), 255 = warmest (candlelight).
    // Stays in the warm-white family; Curator is cozy, never cold-blue.
    ParamDesc::Int("warmth", "Warmth",      200, 0, 255)
);

// The two warm-white anchors the temperature wanders between. Both are warm — the
// drift is a subtle breath around the chosen point, never a colour show. Pushed
// deliberately amber: the WS2812B correction preserves blue relative to green, so
// a nominally-warm RGB reads several hundred K cooler on the strip; these
// over-warm to land in cozy-white territory after the pipeline.
static const CRGB16 kWarmAnchor = CRGB16::fromRGB8(255, 122,  26);  // deep amber
static const CRGB16 kCoolAnchor = CRGB16::fromRGB8(255, 178, 108);  // warm soft white

// Blend the two anchors by an 8-bit warmth position: 0 → cool, 255 → warm, so a
// higher "Warmth" value reads as warmer (matches the label).
static inline CRGB16 whitePoint(uint8_t w) {
    return blend16(kCoolAnchor, kWarmAnchor, (uint16_t)((w << 8) | w));
}

void effectCurator(SegmentView& view, const ParamValues& params,
                   uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    uint8_t speed  = params.getInt(curator::SPEED);
    uint8_t warmth = params.getInt(curator::WARMTH);

    // Slow noise phase. Two decorrelated samples drive temperature and brightness;
    // `speed` sets how fast the phase advances (low = imperceptible over minutes,
    // high = a soft wander over a few seconds).
    uint32_t phase = frame * (uint32_t)speed;
    uint16_t nTemp = inoise16(phase);
    uint16_t nBri  = inoise16(phase + 40000u);

    // Temperature: wander ~±20 warmth-codes around the chosen point.
    int16_t off    = (int16_t)(((int32_t)nTemp - 32768) * 20 / 32768);
    int16_t center = (int16_t)warmth + off;
    if (center < 0)   center = 0;
    if (center > 255) center = 255;
    CRGB16 col = whitePoint((uint8_t)center);

    // Brightness: wander ~±3 % (gain 247..255) so it never quite sits still.
    uint8_t gain = (uint8_t)(247 + ((uint32_t)nBri * 9 >> 16));
    col = scale(col, gain);

    view.fill(col);
}

// Uniform fill is dimension-agnostic and remap-safe → runs on a strip or a matrix.
REGISTER_EFFECT_SCHEMA_DIMS(effectCurator, "curator", "Curator", Animated,
                            curatorSchema, 0, Any);

}  // namespace lume
