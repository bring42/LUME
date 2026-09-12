#ifndef LUME_FASTLED_OUTPUT_H
#define LUME_FASTLED_OUTPUT_H

#include <FastLED.h>
#include "led_output.h"
#include "led_hardware.h"
#include "../constants.h"
#include "../logging.h"

// On the ESP32 family FastLED's clockless driver is the RMT peripheral, and its
// RmtController (idf4_rmt.h) takes the data pin and the three bit timings as
// plain constructor arguments — the chipset/pin template parameters of
// FastLED.addLeds<...>() exist only to feed it those numbers. So the runtime
// strip selection needs no template explosion: one small CPixelLEDController
// per colour order (six, a few hundred bytes each) wraps an RmtController built
// from the persisted LedHardware at boot.
#if defined(ESP32) && !FASTLED_RMT5
#define LUME_RUNTIME_LED_HARDWARE 1
#else
#define LUME_RUNTIME_LED_HARDWARE 0
#endif

namespace lume {

#if LUME_RUNTIME_LED_HARDWARE
// Mirror of FastLED's ESP32 ClocklessController (idf4_clockless_rmt_esp32.h),
// minus the compile-time pin/timing: same RMT backend, same showPixels path,
// same FASTLED_ESP32_FLASH_LOCK / FASTLED_RMT_MAX_CHANNELS behaviour.
template <EOrder RGB_ORDER>
class RuntimeRmtController : public CPixelLEDController<RGB_ORDER> {
public:
    RuntimeRmtController(int pin, int t1, int t2, int t3)
        : rmt_(pin, t1, t2, t3, FASTLED_RMT_MAX_CHANNELS, FASTLED_RMT_BUILTIN_DRIVER) {}
    void init() override {}
    uint16_t getMaxRefreshRate() const override { return 400; }
protected:
    void showPixels(PixelController<RGB_ORDER>& pixels) override {
        PixelIterator iterator = pixels.as_iterator(this->getRgbw());
        rmt_.showPixels(iterator);
    }
private:
    RmtController rmt_;
};

template <EOrder RGB_ORDER>
static CLEDController* makeRuntimeRmt(int pin, int t1, int t2, int t3) {
    // Allocated once at boot and bound into FastLED's controller list, which
    // has no remove — hence "hardware changes apply on reboot".
    return new RuntimeRmtController<RGB_ORDER>(pin, t1, t2, t3);
}

inline CLEDController* makeRuntimeRmt(LedColorOrder order, int pin, int t1, int t2, int t3) {
    switch (order) {
        case LedColorOrder::RGB: return makeRuntimeRmt<RGB>(pin, t1, t2, t3);
        case LedColorOrder::RBG: return makeRuntimeRmt<RBG>(pin, t1, t2, t3);
        case LedColorOrder::GBR: return makeRuntimeRmt<GBR>(pin, t1, t2, t3);
        case LedColorOrder::BRG: return makeRuntimeRmt<BRG>(pin, t1, t2, t3);
        case LedColorOrder::BGR: return makeRuntimeRmt<BGR>(pin, t1, t2, t3);
        case LedColorOrder::GRB:
        default:                 return makeRuntimeRmt<GRB>(pin, t1, t2, t3);
    }
}
#endif // LUME_RUNTIME_LED_HARDWARE

// The default ILedOutput: FastLED's RMT driver (RFC 0001 §6). The strip type /
// pin / colour order come from the persisted config (LedHardware); the power
// limits from constants.h. This is the only place the render core touches the
// FastLED *driver* (as opposed to its math); swapping platforms means providing
// a different ILedOutput.
class FastLedOutput : public ILedOutput {
public:
    void begin(CRGB* leds, uint16_t count, const LedHardware& hw) override {
        LedHardware bound = hw;
#if LUME_RUNTIME_LED_HARDWARE
        if (!isUsableLedPin(bound.pin)) {
            // Persisted config is validated on every write path, so this only
            // fires for NVS written by an older/foreign build. Never bind the
            // flash or USB pins: fall back to the compile-time default.
            LOG_ERROR(LogTag::LED, "LED data pin %u is not usable on this chip; using GPIO %d",
                      bound.pin, (int)LED_DATA_PIN);
            bound.pin = static_cast<uint8_t>(LED_DATA_PIN);
        }
        const LedChipsetInfo& chip = ledChipsetInfo(bound.chipset);
        // C_NS(): ns -> CPU cycles, exactly what FastLED's own chipset classes
        // hand the RMT controller (it converts back to RMT ticks internally).
        CLEDController* ctrl = makeRuntimeRmt(bound.order, bound.pin,
                                              C_NS(chip.t1Ns), C_NS(chip.t2Ns), C_NS(chip.t3Ns));
        if (chip.rgbw) {
            // 4-byte pixels; white is derived from the RGB triplet by FastLED's
            // default Rgbw mode (W = min(R,G,B), subtracted from the colour).
            ctrl->setRgbw(RgbwDefault());
        }
        FastLED.addLeds(ctrl, leds, count);
        LOG_INFO(LogTag::LED, "LED output: %s, %s order, GPIO %u, %u px",
                 chip.name, ledColorOrderKey(bound.order), bound.pin, count);
#else
        // Non-ESP32 (host tests; other MCUs): FastLED's driver here needs the
        // pin and chipset at compile time, so the build-flag defaults apply.
        (void)bound;
        FastLED.addLeds<LED_STRIP_TYPE, LED_DATA_PIN, LED_COLOR_MODE>(leds, count);
#endif
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
