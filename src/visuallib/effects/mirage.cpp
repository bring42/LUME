#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// Mirage — light reflecting off water onto a wall.
//
// Two or three slow interfering sine/noise layers produce soft caustic ripples
// in desaturated aqua/white — bright thin lines where the layers align, drifting
// and beating against each other. Reads as "expensive hotel pool at night."
// Phase accumulator (speed = drift rate); Ripple Size sets the spatial scale.
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace mirage {
    constexpr uint8_t SPEED = 0;
    constexpr uint8_t SCALE = 1;
}

DEFINE_EFFECT_SCHEMA(mirageSchema,
    ParamDesc::Int("speed", "Drift Speed", 40,  1, 255),
    ParamDesc::Int("scale", "Ripple Size", 128, 1, 255)
);

struct MirageState { uint32_t phase; };

static const CRGB16 kAqua = CRGB16::fromRGB8(150, 225, 240);  // desaturated aqua-white

static inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

void effectMirage(SegmentView& view, const ParamValues& params,
                  uint32_t frame, bool firstFrame) {
    (void)frame;
    MirageState* s = view.getScratchpad<MirageState>();
    if (!s) return;
    if (firstFrame) s->phase = 0;

    uint8_t speed  = params.getInt(mirage::SPEED);
    uint8_t ripple = params.getInt(mirage::SCALE);
    s->phase += (uint32_t)speed * 3u;

    float t  = (float)s->phase / 9000.0f;                 // slow time
    float sf = 0.10f + (float)ripple / 255.0f * 0.55f;    // spatial frequency

    for (uint16_t i = 0; i < view.size(); i++) {
        float x = (float)i * sf;
        // Two drifting sine layers + a slow noise layer, interfering.
        float l1 = sinf(x * 0.90f + t);
        float l2 = sinf(x * 1.37f - t * 0.80f);
        float l3 = (float)inoise16((uint32_t)i * 900u, s->phase >> 2) / 32768.0f - 1.0f;
        float v  = (l1 + l2 + l3) / 3.0f;                 // -1..1
        // Sharpen the crests into caustic lines (soft power), keep a dim floor.
        float c   = clamp01((v + 1.0f) * 0.5f);
        float lum = 0.06f + 0.94f * (c * c * c);
        view[i] = scale(kAqua, (uint8_t)(lum * 255.0f));
    }
}

REGISTER_EFFECT_SCHEMA(effectMirage, "mirage", "Mirage", Animated,
                       mirageSchema, sizeof(MirageState));

}  // namespace lume
