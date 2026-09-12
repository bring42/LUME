#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// Silk — the premium answer to meteor.
//
// A single soft-edged luminance swell glides the strip's length over ~20 s with
// subpixel motion and a long eased tail, then a quiet pause of random length
// before the next pass. The swell's position is tracked in floating point, so it
// eases across the strip instead of stepping LED-to-LED; the shape carries its
// own asymmetric tail (short soft head, long trailing fade) so the tail reads at
// any speed rather than depending on frame smear.
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace silk {
    constexpr uint8_t COLOR = 0;
    constexpr uint8_t SPEED = 1;
}

DEFINE_EFFECT_SCHEMA(silkSchema,
    ParamDesc::Color("color", "Color", CRGB(255, 180, 120)),   // warm swell
    ParamDesc::Int("speed", "Glide Speed", 40, 1, 255)         // default ~20s pass
);

struct SilkState {
    float    pos;     // swell centre, in pixels (subpixel)
    uint32_t rng;     // pause-length randomness
    uint16_t pause;   // frames left in the quiet pause
    uint8_t  mode;    // 0 = gliding, 1 = paused
};

static inline uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }

void effectSilk(SegmentView& view, const ParamValues& params,
                uint32_t frame, bool firstFrame) {
    SilkState* s = view.getScratchpad<SilkState>();
    if (!s) return;

    float n = (float)view.size();
    float tailLen = n * 0.16f + 1.0f;   // long trailing fade
    float headW   = 1.8f;               // short soft leading edge

    if (firstFrame) {
        s->pos   = -tailLen - 2.0f;
        s->rng   = frame * 2654435761u + 12345u;
        s->pause = 0;
        s->mode  = 0;
    }

    CRGB   color = params.getColor(silk::COLOR);
    uint8_t speed = params.getInt(silk::SPEED);
    CRGB16 col16 = SegmentView::c16(color);

    view.clear();   // black base; the swell shape carries its own tail

    if (s->mode == 0) {
        // Draw the asymmetric swell at the current subpixel position.
        for (uint16_t i = 0; i < view.size(); i++) {
            float d = (float)i - s->pos;      // <0 behind (tail), >0 ahead (head)
            float g;
            if (d <= 0.0f) {
                g = expf(d / tailLen);        // long trailing fade
            } else {
                float h = d / headW;
                g = expf(-h * h * 0.5f);      // short soft head
            }
            if (g > 0.004f) view[i] = scale(col16, (uint8_t)(g * 255.0f));
        }
        // Glide ~20s across the full travel at the default speed.
        s->pos += 0.008f + (float)speed / 255.0f * 0.2f;
        if (s->pos > n + headW + 2.0f) {
            s->mode  = 1;
            s->pause = (uint16_t)(40u + (lcg(s->rng) % 180u));   // ~0.7–3.8 s
        }
    } else {
        // Quiet pause — strip stays dark until the next pass.
        if (s->pause == 0) { s->mode = 0; s->pos = -tailLen - 2.0f; }
        else               { s->pause--; }
    }
}

REGISTER_EFFECT_SCHEMA(effectSilk, "silk", "Silk", Moving,
                       silkSchema, sizeof(SilkState));

}  // namespace lume
