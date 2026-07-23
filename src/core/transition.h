#ifndef LUME_TRANSITION_H
#define LUME_TRANSITION_H

#include <stdint.h>

namespace lume {

/**
 * Premium easing engine — the eased-motion differentiator (see docs vision).
 *
 * This header is deliberately dependency-free: no Arduino, no FastLED, no
 * millis(). Every method takes the current time as a parameter (`nowMs`), so the
 * type carries no clock and is exercised host-side by `pio test -e native`. The
 * render loop is the single writer that owns and advances it.
 *
 * Control-plane commands carry a Matter/Zigbee-shaped `transitionTime` in tenths
 * of a second; the loop interpolates the target over that window with an
 * ease-in-out curve (slow start, slow finish) instead of snapping. A transition
 * time of 0 preserves the historical instant-apply behaviour.
 */

// Matter/Zigbee `transitionTime` is expressed in tenths of a second (a uint16,
// so up to ~109 min). Convert to the milliseconds the interpolator works in.
inline uint32_t transitionTenthsToMs(uint16_t tenths) {
    return static_cast<uint32_t>(tenths) * 100u;
}

/**
 * Cubic ease-in-out over t in [0,1]. Symmetric and C1-continuous — the premium
 * fade shape. Implemented without powf() so it is cheap on the MCU FPU and
 * bit-identical between device and host tests.
 */
inline float easeInOutCubic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.5f) return 4.0f * t * t * t;
    float f = -2.0f * t + 2.0f;
    return 1.0f - (f * f * f) * 0.5f;
}

/**
 * EasedU8 — an eased transition of a single uint8 value (e.g. brightness).
 *
 * Single-writer: construct/snap/start/advance are only ever called on the render
 * loop. Producers on other tasks never touch it — they enqueue a command and the
 * loop translates it into start()/snap().
 */
class EasedU8 {
public:
    EasedU8()
        : startValue_(0), targetValue_(0), current_(0),
          startMs_(0), durationMs_(0), active_(false) {}

    // Jump immediately to `value` (transitionTime == 0 — historical snap).
    void snap(uint8_t value) {
        current_ = targetValue_ = startValue_ = value;
        active_ = false;
    }

    /**
     * Begin an eased transition from the current value to `target` over
     * `durationMs`. A zero duration (or a target equal to the current value)
     * snaps. Re-targeting mid-flight eases from wherever we are *now*, so a
     * change of mind never produces a visible jump.
     */
    void start(uint8_t target, uint32_t durationMs, uint32_t nowMs) {
        if (durationMs == 0 || target == current_) {
            snap(target);
            return;
        }
        startValue_ = current_;
        targetValue_ = target;
        startMs_ = nowMs;
        durationMs_ = durationMs;
        active_ = true;
    }

    /**
     * Advance to `nowMs` and return the current interpolated value. Pins to the
     * target and deactivates once the window elapses. Safe to call every frame
     * whether or not a transition is active.
     */
    uint8_t advance(uint32_t nowMs) {
        if (!active_) return current_;

        // Guard against a nowMs captured before startMs_ (a start() applied
        // later in the same frame): clamp elapsed to 0 rather than underflow.
        uint32_t elapsed = (nowMs > startMs_) ? (nowMs - startMs_) : 0u;
        if (elapsed >= durationMs_) {
            current_ = targetValue_;
            active_ = false;
            return current_;
        }

        float t = static_cast<float>(elapsed) / static_cast<float>(durationMs_);
        float eased = easeInOutCubic(t);
        int32_t delta = static_cast<int32_t>(targetValue_) - static_cast<int32_t>(startValue_);
        // Round toward the target so every step makes progress (no stalling on
        // truncation) and the endpoints are exact.
        float rounding = (delta >= 0) ? 0.5f : -0.5f;
        int32_t v = static_cast<int32_t>(startValue_) +
                    static_cast<int32_t>(delta * eased + rounding);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        current_ = static_cast<uint8_t>(v);
        return current_;
    }

    uint8_t value() const { return current_; }
    uint8_t target() const { return targetValue_; }
    bool isActive() const { return active_; }

    /**
     * Linear fraction of the *time* window elapsed, in [0,1] — independent of
     * the eased value curve. This is what a UI countdown wants (e.g. a
     * nightlight progress bar): the value eases, but "how far through the timer
     * are we" is linear. Returns 1.0 when settled or zero-duration.
     */
    float progress(uint32_t nowMs) const {
        if (!active_ || durationMs_ == 0) return 1.0f;
        uint32_t elapsed = (nowMs > startMs_) ? (nowMs - startMs_) : 0u;
        if (elapsed >= durationMs_) return 1.0f;
        return static_cast<float>(elapsed) / static_cast<float>(durationMs_);
    }

private:
    uint8_t  startValue_;
    uint8_t  targetValue_;
    uint8_t  current_;
    uint32_t startMs_;
    uint32_t durationMs_;
    bool     active_;
};

} // namespace lume

#endif // LUME_TRANSITION_H
