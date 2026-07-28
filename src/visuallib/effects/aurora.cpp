#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

// ─────────────────────────────────────────────────────────────────────────────
// Aurora — curtains of desaturated teal, green, and violet sliding through each
// other on layered inoise16, occasionally swelling brighter. The one saturated-ish
// mode — but pastel, never pure channel colours. One noise layer chooses which
// curtain colour a pixel leans toward; a second, slower layer drives brightness
// swells that roll along the strip. Phase accumulator (speed = slide rate).
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace aurora {
    constexpr uint8_t SPEED = 0;
}

DEFINE_EFFECT_SCHEMA(auroraSchema,
    ParamDesc::Int("speed", "Slide Speed", 45, 1, 255)
);

struct AuroraState { uint32_t phase; };

// Pastel curtain colours (desaturated, never pure R/G/B).
static const CRGB16 kTeal   = CRGB16::fromRGB8( 70, 210, 180);
static const CRGB16 kGreen  = CRGB16::fromRGB8( 90, 210, 120);
static const CRGB16 kViolet = CRGB16::fromRGB8(150, 110, 220);

void effectAurora(SegmentView& view, const ParamValues& params,
                  uint32_t frame, bool firstFrame) {
    (void)frame;
    AuroraState* s = view.getScratchpad<AuroraState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    uint8_t speed = params.getInt(aurora::SPEED);
    s->phase += (uint32_t)speed * 3u;

    for (uint16_t i = 0; i < view.size(); i++) {
        // Colour-selection layer: slides one way, picks teal↔green↔violet.
        uint16_t hn = inoise16((uint32_t)i * 1100u + s->phase, 1234u);
        // Brightness-swell layer: slower, slides the other way.
        uint16_t bn = inoise16((uint32_t)i * 700u, (s->phase >> 1) + 55555u);

        // Map hn across the three curtains (0..0.5 teal→green, 0.5..1 green→violet).
        float h = (float)hn / 65535.0f;
        CRGB16 col = (h < 0.5f) ? blend16(kTeal, kGreen, (uint16_t)(h * 2.0f * 65535.0f))
                                : blend16(kGreen, kViolet, (uint16_t)((h - 0.5f) * 2.0f * 65535.0f));

        // Pastel, dim base with occasional brighter swells.
        float b   = (float)bn / 65535.0f;
        float lum = 0.12f + 0.55f * (b * b);   // squared → swells are rarer/brighter
        view[i] = scale(col, (uint8_t)(lum * 255.0f));
    }
}

REGISTER_EFFECT_SCHEMA(effectAurora, "aurora", "Aurora", Animated,
                       auroraSchema, sizeof(AuroraState));

}  // namespace lume
