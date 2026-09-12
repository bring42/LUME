#ifndef LUME_FASTLED_OUTPUT_H
#define LUME_FASTLED_OUTPUT_H

#include <FastLED.h>
#include "led_output.h"
#include "../constants.h"

namespace lume {

// The default ILedOutput: FastLED's RMT driver for WS2812B (RFC 0001 §6). The
// strip type / pin / color order / power limits come from constants.h. This is
// the only place the render core touches the FastLED *driver* (as opposed to its
// math); swapping platforms means providing a different ILedOutput.
class FastLedOutput : public ILedOutput {
public:
    void begin(CRGB* leds, uint16_t count) override {
        FastLED.addLeds<LED_STRIP_TYPE, LED_DATA_PIN, LED_COLOR_MODE>(leds, count);
        FastLED.setMaxPowerInVoltsAndMilliamps(LED_VOLTAGE, LED_MAX_MILLIAMPS);
        // The premium 16-bit render pipeline (render16.h, driven in
        // LumeController::update()) owns ALL signal shaping: gamma, WS2812B white-
        // balance correction, dim-to-warm, master brightness, and a single
        // temporal+spatial error-diffusion dither — each applied once, in 16-bit,
        // in the right order. So FastLED itself must do NOTHING that would double-
        // process the already-final 8-bit bytes: no brightness scaling
        // (controller pins it to 255), neutral correction, and dithering DISABLED
        // (FastLED's 8-bit BINARY_DITHER would fight our error diffusion).
        FastLED.setCorrection(CRGB(255, 255, 255));
        FastLED.setDither(DISABLE_DITHER);
    }
    void show() override { FastLED.show(); }
    void setBrightness(uint8_t brightness) override { FastLED.setBrightness(brightness); }
    void setTemperature(CRGB temp) override { FastLED.setTemperature(temp); }
    void setCorrection(CRGB correction) override { FastLED.setCorrection(correction); }
    void clear() override { FastLED.clear(); }
};

} // namespace lume

#endif // LUME_FASTLED_OUTPUT_H
