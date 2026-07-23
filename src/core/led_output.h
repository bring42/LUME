#ifndef LUME_LED_OUTPUT_H
#define LUME_LED_OUTPUT_H

#include <FastLED.h>  // CRGB

namespace lume {

// The LED-output HAL seam (RFC 0001 §6).
//
// The render core depends only on this interface for pushing pixels; the
// concrete driver is pluggable per platform — FastLED's RMT driver today, and
// (later) ESP-IDF `led_strip` / I2S / SPI, RP2350 PIO, or a WASM emulator's
// `present()`. FastLED's *math* (CRGB / palette / noise / blur) stays universal
// and platform-independent; only the output *packaging* is abstracted here, so
// the Matter-over-WiFi RMT-jitter question can later be answered by swapping the
// driver without touching effects or the controller.
class ILedOutput {
public:
    virtual ~ILedOutput() = default;

    // Bind the controller-owned pixel buffer + length (once, at startup).
    virtual void begin(CRGB* leds, uint16_t count) = 0;

    // Push the current buffer contents to the physical strip.
    virtual void show() = 0;

    // Set the global output brightness (0-255).
    virtual void setBrightness(uint8_t brightness) = 0;

    // Set a global color-temperature tint (CRGB scalar, 0xFFFFFF = neutral).
    // Used by dim-to-warm. Default no-op so alternate/mock outputs need not care.
    virtual void setTemperature(CRGB /*temp*/) {}

    // Set the per-channel color-correction (CRGB scalar, 0xFFFFFF = none). Driven
    // per-frame so the low-end floor can ramp toward uncorrected white and keep
    // dim white from collapsing to red. Default no-op for alternate/mock outputs.
    virtual void setCorrection(CRGB /*correction*/) {}

    // Blank the output.
    virtual void clear() = 0;
};

} // namespace lume

#endif // LUME_LED_OUTPUT_H
