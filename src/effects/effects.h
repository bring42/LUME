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
 * 2. Implement the effect function
 * 3. Use REGISTER_EFFECT macro
 * 4. Add declaration here (optional but good for IDE)
 * 5. That's it - no other changes needed
 */

#include "../core/effect_registry.h"

namespace lume {

// === Effect function declarations ===
// Defined in individual .cpp files, auto-registered via macros

// Basic effects
void effectSolid(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectRainbow(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectGradient(SegmentView& view, const EffectParams& params, uint32_t frame);

// Animated effects
void effectFire(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectFireUp(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectConfetti(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectColorWaves(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectNoise(SegmentView& view, const EffectParams& params, uint32_t frame);

// Pulse/breathing effects
void effectPulse(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectBreathe(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectCandle(SegmentView& view, const EffectParams& params, uint32_t frame);

// Sparkle/twinkle effects
void effectSparkle(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectTwinkle(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectStrobe(SegmentView& view, const EffectParams& params, uint32_t frame);

// Moving effects
void effectMeteor(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectComet(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectScanner(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectSinelon(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectTheaterChase(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectWave(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectRain(SegmentView& view, const EffectParams& params, uint32_t frame);

// Special effects
void effectPride(SegmentView& view, const EffectParams& params, uint32_t frame);
void effectPacifica(SegmentView& view, const EffectParams& params, uint32_t frame);

} // namespace lume

#endif // LUME_EFFECTS_H
