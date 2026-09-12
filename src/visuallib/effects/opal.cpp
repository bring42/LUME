#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

// ─────────────────────────────────────────────────────────────────────────────
// Opal — white with a secret.
//
// Reads as warm white from across the room, but a faint pastel iridescence
// (rose / mint / lilac at ~10 % saturation) drifts through it like mother-of-
// pearl. A slow noise layer chooses which pastel a pixel leans toward and how
// strongly (always faint), so the tint shimmers and migrates without the white
// ever obviously breaking into colour. Phase accumulator (speed = drift rate).
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace opal {
    constexpr uint8_t SPEED   = 0;
    constexpr uint8_t SHIMMER = 1;
}

DEFINE_EFFECT_SCHEMA(opalSchema,
    ParamDesc::Int("speed",   "Drift Speed",  30,  1, 255),
    // How strong the iridescence gets (kept faint — this is a *secret*).
    ParamDesc::Int("shimmer", "Iridescence", 110, 1, 255)
);

struct OpalState { uint32_t phase; };

static const CRGB16 kWhite = CRGB16::fromRGB8(255, 200, 150);  // warm white base
static const CRGB16 kRose  = CRGB16::fromRGB8(255, 200, 210);
static const CRGB16 kMint  = CRGB16::fromRGB8(200, 255, 220);
static const CRGB16 kLilac = CRGB16::fromRGB8(215, 205, 255);

void effectOpal(SegmentView& view, const ParamValues& params,
                uint32_t frame, bool firstFrame) {
    (void)frame;
    OpalState* s = view.getScratchpad<OpalState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    uint8_t speed   = params.getInt(opal::SPEED);
    uint8_t shimmer = params.getInt(opal::SHIMMER);
    s->phase += (uint32_t)speed * 2u;

    // Max tint blend stays low (~10–18 %) — the iridescence is faint by design.
    float maxTint = (float)shimmer / 255.0f * 0.18f;

    for (uint16_t i = 0; i < view.size(); i++) {
        // Which pastel, and how much, both drift on slow noise.
        uint16_t hn = inoise16((uint32_t)i * 1300u + s->phase, 900u);
        uint16_t sn = inoise16((uint32_t)i * 1300u, (s->phase >> 1) + 40000u);

        float h = (float)hn / 65535.0f;
        CRGB16 pastel = (h < 0.5f) ? blend16(kRose, kMint, (uint16_t)(h * 2.0f * 65535.0f))
                                   : blend16(kMint, kLilac, (uint16_t)((h - 0.5f) * 2.0f * 65535.0f));

        float tint = maxTint * ((float)sn / 65535.0f);       // 0..maxTint
        view[i] = blend16(kWhite, pastel, (uint16_t)(tint * 65535.0f));
    }
}

REGISTER_EFFECT_SCHEMA(effectOpal, "opal", "Opal", Animated,
                       opalSchema, sizeof(OpalState));

}  // namespace lume
