#ifndef LUME_EFFECTS_H
#define LUME_EFFECTS_H

/**
 * LUME Effects
 * 
 * Include this file to register all built-in effects.
 * Each effect is in its own .cpp file and self-registers.
 * 
 * To add a new effect:
 * 1. Create effects/my_effect.cpp
 * 2. Define schema with DEFINE_EFFECT_SCHEMA
 * 3. Implement the effect function with new signature
 * 4. Use REGISTER_EFFECT_SCHEMA macro
 * 5. That's it - no other changes needed
 * 
 * Effects are accessed via the effect registry, not by direct function calls.
 * Forward declarations below are for IDE support only.
 */

#include "../core/effect_registry.h"
#include "../core/param_schema.h"

namespace lume {

// === Effect function declarations ===
// Defined in individual .cpp files, auto-registered via macros
// Note: Effects use the new signature: (SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame)

// Basic effects
void effectSolid(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectRainbow(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectGradient(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);

// Animated effects
void effectFire(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectFireUp(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectConfetti(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectColorWaves(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectNoise(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);

// Pulse/breathing effects
void effectPulse(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectBreathe(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectCandle(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);

// Sparkle/twinkle effects
void effectSparkle(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectTwinkle(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectStrobe(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);

// Moving effects
void effectMeteor(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectComet(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectScanner(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectSinelon(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectTheaterChase(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectWave(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectRain(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);

// Special effects
void effectPride(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);
void effectPacifica(SegmentView& view, const ParamValues& params, uint32_t frame, bool firstFrame);

} // namespace lume

#endif // LUME_EFFECTS_H
