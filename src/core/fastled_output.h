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
        // Baseline correction; the render loop re-drives this every frame
        // (setCorrection) and ramps it toward uncorrected white near the floor.
        FastLED.setCorrection(TypicalLEDStrip);
        FastLED.setMaxPowerInVoltsAndMilliamps(LED_VOLTAGE, LED_MAX_MILLIAMPS);
        // Temporal dithering (BINARY_DITHER, FastLED's default) DOES run on the
        // ESP32 RMT driver — idf5_rmt.cpp calls loadAndScaleRGB()+stepDithering()
        // per pixel, and the dither counter advances each show(), which is why the
        // loop refreshes every pass. It genuinely smooths *mid-range* dim content.
        // It does NOT fix dim white going red: the dither is qadd8(sourceByte, d)
        // applied before the per-channel scale, and a white source byte (255)
        // saturates — no channel gains headroom. The white→red floor is handled
        // deterministically by the output-brightness floor + correction ramp in
        // LumeController::update() (see LED_MIN_OUTPUT), not by dithering.
        FastLED.setDither(BINARY_DITHER);
    }
    void show() override { FastLED.show(); }
    void setBrightness(uint8_t brightness) override { FastLED.setBrightness(brightness); }
    void setTemperature(CRGB temp) override { FastLED.setTemperature(temp); }
    void setCorrection(CRGB correction) override { FastLED.setCorrection(correction); }
    void clear() override { FastLED.clear(); }
};

} // namespace lume

#endif // LUME_FASTLED_OUTPUT_H
