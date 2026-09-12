#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

// ─────────────────────────────────────────────────────────────────────────────
// Hearth — dying-coals glow.
//
// A deep warm ember base with slow-drifting patches of slightly hotter amber
// where the noise runs high. No rhythm — pure spatial drift, everything well
// below half brightness. The second premium mode after Curator: same 16-bit
// warm-white discipline, but now the colour varies *along the strip* on 2D
// noise (position × slow time) instead of being uniform. Reads like coals
// breathing heat, not an animation.
//
// Built on the substrate (16-bit content → dim-to-warm → IIR → gamma → dither),
// so it inherits the clean warm low end and never-steps smoothing for free.
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace hearth {
    constexpr uint8_t SPEED     = 0;
    constexpr uint8_t INTENSITY = 1;
}

DEFINE_EFFECT_SCHEMA(hearthSchema,
    // Slow by design — coals drift, they don't flicker. `speed` sets the drift
    // RATE (see the phase accumulator below), so changing it is smooth, not a jump.
    ParamDesc::Int("speed",     "Drift Speed", 40,  1, 255),
    // Higher = hotter (more of the strip glows to the hot amber). 0 = coolest,
    // just the deep dying-coal base.
    ParamDesc::Int("intensity", "Heat",        130, 1, 255)
);

// Deep dying-coal base and the hotter amber a patch glows to. Both intentionally
// dim (perceptual, pre-gamma) so even at full master brightness it stays coal-like.
static const CRGB16 kEmberBase = CRGB16::fromRGB8( 84, 16,  2);  // deep dark ember
static const CRGB16 kEmberHot  = CRGB16::fromRGB8(185, 66, 10);  // hotter amber patch

// Persistent drift phase: advancing it by `speed` each frame means speed controls
// the RATE, not an absolute position. Changing speed then just changes how fast
// the field drifts — no phase jump (the old `frame * speed` teleported the noise
// whenever speed changed) — and the drift is continuous, never frozen.
struct HearthState {
    uint32_t phase;
};

// inoise16 doesn't use the full 0..65535 range — it clusters roughly in
// [~12000, ~53000]. Stretch that band to 0..65535 so patches have real contrast.
static inline uint16_t stretchNoise(uint32_t n) {
    int32_t v = ((int32_t)n - 12000) * 65535 / 41000;
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    return (uint16_t)v;
}

void effectHearth(SegmentView& view, const ParamValues& params,
                  uint32_t frame, bool firstFrame) {
    (void)frame;
    HearthState* s = view.getScratchpad<HearthState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    uint8_t speed = params.getInt(hearth::SPEED);
    uint8_t heat  = params.getInt(hearth::INTENSITY);

    // Advance the drift by the current speed (×6 lands the default at a gentle but
    // clearly-alive coal drift; raw speed crawled imperceptibly at anything but max).
    s->phase += (uint32_t)speed * 6u;

    // Heat is a temperature OFFSET (inverted so higher Heat reads as hotter — the
    // deep saturated ember-glow end, which the eye reads as the hottest coal; the
    // pale amber end reads as cooler/ashen). The noise adds *slight* patch
    // variation on top (±¼ range). Heat down = pale patchy embers; Heat up = a
    // deep even ember glow.
    uint8_t h = (uint8_t)(255 - heat);
    int32_t heatOffset = (int32_t)(((uint16_t)h << 8) | h);   // 0..65535

    for (uint16_t i = 0; i < view.size(); i++) {
        // 2D noise: position along the strip × drifting phase → moving patches.
        uint16_t n = stretchNoise(inoise16((uint32_t)i * 3000u, s->phase));
        int32_t frac = heatOffset + (((int32_t)n - 32768) >> 1);   // offset + patch
        if (frac < 0) frac = 0;
        if (frac > 65535) frac = 65535;
        view[i] = blend16(kEmberBase, kEmberHot, (uint16_t)frac);
    }
}

// Logic assumes 1D adjacency (patches along the strip) — the honest default.
REGISTER_EFFECT_SCHEMA(effectHearth, "hearth", "Hearth", Animated, hearthSchema, sizeof(HearthState));

}  // namespace lume
