#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// Waltz — the gradient dance.
//
// A palette mapped along the strip, but the palette INDEX field is what moves:
// layered slow sines + noise stretch, compress, and fold the gradient into
// itself. It never scrolls uniformly (the cheap tell) and never shows the whole
// palette at once — a drifting window of it, so the composition keeps evolving.
// Colours flow into each other like slow ink in water. Palette-agnostic: one
// engine, a whole family of looks. Phase accumulator (speed = flow rate).
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace waltz {
    constexpr uint8_t PALETTE = 0;
    constexpr uint8_t SPEED   = 1;
    constexpr uint8_t SPREAD  = 2;
}

DEFINE_EFFECT_SCHEMA(waltzSchema,
    ParamDesc::PaletteSelect("palette", "Palette"),
    ParamDesc::Int("speed",  "Flow Speed",  60,  1, 255),
    // How much of the palette the window spans across the strip.
    ParamDesc::Int("spread", "Palette Span", 90, 1, 255)
);

struct WaltzState { uint32_t phase; };

void effectWaltz(SegmentView& view, const ParamValues& params,
                 uint32_t frame, bool firstFrame) {
    (void)frame;
    WaltzState* s = view.getScratchpad<WaltzState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    const CRGBPalette16& pal = params.getPalette();
    uint8_t speed  = params.getInt(waltz::SPEED);
    uint8_t spread = params.getInt(waltz::SPREAD);
    s->phase += (uint32_t)speed * 2u;

    float t   = (float)s->phase / 4000.0f;
    // Base slope = a narrow window of the palette across the strip (never all of it).
    float slope = (float)spread / 255.0f * 0.9f / (view.size() > 1 ? (float)view.size() : 1.0f);
    // A slow drift of the window's centre so it keeps evolving.
    float drift = t * 6.0f;

    for (uint16_t i = 0; i < view.size(); i++) {
        float x = (float)i;
        // Layered sines fold the index field — stretch/compress, not a rigid scroll.
        float fold = 22.0f * sinf(x * slope * 0.6f + t * 0.9f)
                   + 14.0f * sinf(x * slope * 1.7f - t * 0.5f);
        float idx = x * slope * 255.0f + drift + fold;
        uint8_t index8 = (uint8_t)((int32_t)idx & 0xFF);   // wraps → a drifting window
        CRGB c = ColorFromPalette(pal, index8, 255, LINEARBLEND);
        view[i] = SegmentView::c16(c);
    }
}

REGISTER_EFFECT_SCHEMA(effectWaltz, "waltz", "Waltz", Animated,
                       waltzSchema, sizeof(WaltzState));

}  // namespace lume
