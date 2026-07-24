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
    // Very slow by design — the whole point is that you never catch it moving.
    ParamDesc::Int("speed",  "Drift Speed", 40,  1, 255),
    // 0 = warmest (~2400 K candlelight), 255 = coolest (~3400 K soft white).
    // Stays in the warm-white family; Curator is cozy, never cold-blue.
    ParamDesc::Int("warmth", "Warmth",      128, 0, 255)
);

// The two warm-white anchors the temperature wanders between. Both are warm —
// the drift is a subtle breath around the chosen point, never a colour show.
static const CRGB16 kWarmAnchor = CRGB16::fromRGB8(255, 160,  85);  // ~2400 K
static const CRGB16 kCoolAnchor = CRGB16::fromRGB8(255, 210, 160);  // ~3400 K

// Blend the two anchors by an 8-bit warmth position (0 → warm, 255 → cool).
static inline CRGB16 whitePoint(uint8_t w) {
    return blend16(kWarmAnchor, kCoolAnchor, (uint16_t)((w << 8) | w));
}

void effectCurator(SegmentView& view, const ParamValues& params,
                   uint32_t frame, bool firstFrame) {
    (void)firstFrame;
    uint8_t speed  = params.getInt(curator::SPEED);
    uint8_t warmth = params.getInt(curator::WARMTH);

    // Slow noise phase. `speed` is scaled well down so the default drifts over
    // minutes; two decorrelated samples drive temperature and brightness.
    uint32_t phase = (frame * (uint32_t)speed) >> 4;
    uint16_t nTemp = inoise16(phase);
    uint16_t nBri  = inoise16(phase + 40000u);

    // Temperature: wander ~±12 warmth-codes around the chosen point (~±100 K).
    int16_t off    = (int16_t)(((int32_t)nTemp - 32768) * 12 / 32768);
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
