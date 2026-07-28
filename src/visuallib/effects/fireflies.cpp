#include "../../core/effect_registry.h"
#include "../../core/param_schema.h"
#include "../../core/render16.h"

#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// Fireflies — the premium answer to sparkle.
//
// Rare, individual soft blooms over a barely-lit warm base: each one eases in
// over ~2 s, holds, and eases out over ~4 s, so it swells and fades like a breath
// rather than blinking. Never two at once nearby, and the spawn rate is low
// enough that each bloom feels like an event. Positions are floating point
// (subpixel), blooms are soft gaussians drawn additively over the base.
// ─────────────────────────────────────────────────────────────────────────────

namespace lume {

namespace fireflies {
    constexpr uint8_t RATE  = 0;
    constexpr uint8_t COLOR = 1;
}

DEFINE_EFFECT_SCHEMA(firefliesSchema,
    // Higher = blooms appear more often (still sparse). Low = truly rare.
    ParamDesc::Int("rate", "Frequency", 120, 1, 255),
    ParamDesc::Color("color", "Color", CRGB(255, 200, 120))   // warm firefly
);

constexpr int   kMaxFlies = 4;
constexpr uint16_t kInFrames  = 116;   // ~2 s ease-in  (at ~58 fps)
constexpr uint16_t kOutFrames = 232;   // ~4 s ease-out
constexpr uint16_t kLife = kInFrames + kOutFrames;

struct Fly { float pos; uint16_t age; uint8_t active; };
struct FirefliesState {
    uint32_t rng;
    uint16_t spawn;      // frames until the next spawn attempt
    Fly      flies[kMaxFlies];
};

static inline uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
static inline float smoothstep(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}
// Bloom envelope: ease in over kInFrames, ease out over kOutFrames.
static float bloomEnv(uint16_t age) {
    if (age < kInFrames) return smoothstep((float)age / (float)kInFrames);
    return smoothstep(1.0f - (float)(age - kInFrames) / (float)kOutFrames);
}

static const CRGB16 kBase = CRGB16::fromRGB8(70, 26, 8);   // barely-lit warm floor

void effectFireflies(SegmentView& view, const ParamValues& params,
                     uint32_t frame, bool firstFrame) {
    FirefliesState* s = view.getScratchpad<FirefliesState>();
    if (!s) return;
    if (firstFrame) {
        s->rng = frame * 2654435761u + 999u;
        s->spawn = 30;
        for (int f = 0; f < kMaxFlies; f++) s->flies[f].active = 0;
    }

    uint8_t rate  = params.getInt(fireflies::RATE);
    CRGB    color = params.getColor(fireflies::COLOR);
    CRGB16  col16 = SegmentView::c16(color);
    float   n     = (float)view.size();

    // Barely-lit warm base.
    view.fill(scale(kBase, 40));

    // Spawn attempt: pick a spot that isn't near an active bloom.
    if (s->spawn == 0) {
        int slot = -1;
        for (int f = 0; f < kMaxFlies; f++) if (!s->flies[f].active) { slot = f; break; }
        if (slot >= 0) {
            float cand = (float)(lcg(s->rng) % (uint32_t)(view.size() ? view.size() : 1));
            float minDist = n * 0.28f + 1.0f;
            bool ok = true;
            for (int f = 0; f < kMaxFlies; f++)
                if (s->flies[f].active && fabsf(s->flies[f].pos - cand) < minDist) { ok = false; break; }
            if (ok) { s->flies[slot].pos = cand; s->flies[slot].age = 0; s->flies[slot].active = 1; }
        }
        // Next attempt: rarer at low rate. ~1–10 s plus jitter.
        s->spawn = (uint16_t)(60u + (uint32_t)(255 - rate) * 2u + (lcg(s->rng) % 90u));
    } else {
        s->spawn--;
    }

    // Advance + draw each active bloom (soft gaussian, additive over the base).
    const float width = 2.4f;
    for (int f = 0; f < kMaxFlies; f++) {
        Fly& fly = s->flies[f];
        if (!fly.active) continue;
        fly.age++;
        if (fly.age >= kLife) { fly.active = 0; continue; }
        float env = bloomEnv(fly.age);
        int lo = (int)floorf(fly.pos - width * 3.0f), hi = (int)ceilf(fly.pos + width * 3.0f);
        for (int i = lo; i <= hi; i++) {
            if (i < 0 || i >= (int)view.size()) continue;
            float d = ((float)i - fly.pos) / width;
            float g = expf(-d * d * 0.5f) * env;
            if (g > 0.004f) view[i] = addSat(view[i], scale(col16, (uint8_t)(g * 255.0f)));
        }
    }
}

REGISTER_EFFECT_SCHEMA(effectFireflies, "fireflies", "Fireflies", Animated,
                       firefliesSchema, sizeof(FirefliesState));

}  // namespace lume
