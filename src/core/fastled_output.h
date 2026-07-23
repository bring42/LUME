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
        FastLED.setCorrection(TypicalLEDStrip);
        FastLED.setMaxPowerInVoltsAndMilliamps(LED_VOLTAGE, LED_MAX_MILLIAMPS);
        // Temporal dithering recovers the low-end bit depth WS2812B lacks (8-bit
        // per channel dims into a red/near-black floor). It only works when the
        // controller refreshes far faster than the animation rate — the render
        // loop calls show() every pass for exactly this reason. BINARY_DITHER is
        // FastLED's default; set it explicitly so the intent is on the record.
        FastLED.setDither(BINARY_DITHER);
    }
    void show() override { FastLED.show(); }
    void setBrightness(uint8_t brightness) override { FastLED.setBrightness(brightness); }
    void setTemperature(CRGB temp) override { FastLED.setTemperature(temp); }
    void clear() override { FastLED.clear(); }
};

} // namespace lume

#endif // LUME_FASTLED_OUTPUT_H
