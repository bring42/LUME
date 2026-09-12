#ifndef LUME_LED_HARDWARE_H
#define LUME_LED_HARDWARE_H

#include <cstdint>
#include <cstring>
#include "../constants.h"   // LED_STRIP_TYPE / LED_COLOR_MODE / LED_DATA_PIN (compile-time defaults)

// Runtime-selectable LED hardware: strip chipset, colour byte order and data
// pin. This header is pure data + parsing (no FastLED, no Arduino) so the
// catalog is host-testable; the FastLED backend (fastled_output.h) maps a
// LedHardware onto the driver at boot.
//
// The catalog mirrors FastLED's ESP32 clockless timings (chipsets.h, the
// `C_NS(...)` branch) in nanoseconds. On the ESP32 the RMT driver takes pin and
// bit timings as plain constructor arguments, so every chipset here is one row
// of data rather than one template instantiation — that is what makes the
// selection a runtime setting instead of a build flag.
namespace lume {

enum class LedChipset : uint8_t {
    WS2812B = 0,   // also WS2812, WS2852, GS1903, APA104-800 (same timing)
    WS2811,        // 12 V "bullet"/string pixels, usually RGB order
    WS2813,        // WS2812B + backup data line
    WS2815,        // 12 V WS28xx, backup data line
    SK6812,        // RGB variant
    SK6812_RGBW,   // 4-channel: white derived from RGB (FastLED Rgbw default)
    SK6822,        // also APA106
    WS2811_400,    // 400 kHz WS2811
    UCS1903,       // 400 kHz
    UCS1904,
    SM16703,
    TM1809,
    PL9823,
    COUNT
};

enum class LedColorOrder : uint8_t {
    RGB = 0, RBG, GRB, GBR, BRG, BGR,
    COUNT
};

struct LedChipsetInfo {
    LedChipset  id;
    const char* key;      // stable id used in JSON / NVS (never renumber-sensitive)
    const char* name;     // human label
    uint16_t    t1Ns;     // bit timing, ns (FastLED T1/T2/T3 convention)
    uint16_t    t2Ns;
    uint16_t    t3Ns;
    bool        rgbw;     // 4 bytes per pixel on the wire
};

// Order matters only for presentation (the UI lists them in this order).
// Timings copied from FastLED 3.9.4 chipsets.h (ESP32 / C_NS branch).
inline const LedChipsetInfo* ledChipsetCatalog(size_t& count) {
    static const LedChipsetInfo kCatalog[] = {
        { LedChipset::WS2812B,     "WS2812B",     "WS2812B / WS2812 (800 kHz)", 250,  625, 375, false },
        { LedChipset::WS2811,      "WS2811",      "WS2811 (800 kHz, 12 V)",     320,  320, 640, false },
        { LedChipset::WS2813,      "WS2813",      "WS2813",                     320,  320, 640, false },
        { LedChipset::WS2815,      "WS2815",      "WS2815 (12 V)",              250, 1090, 550, false },
        { LedChipset::SK6812,      "SK6812",      "SK6812 (RGB)",               300,  300, 600, false },
        { LedChipset::SK6812_RGBW, "SK6812_RGBW", "SK6812 RGBW",                300,  300, 600, true  },
        { LedChipset::SK6822,      "SK6822",      "SK6822 / APA106",            375, 1000, 375, false },
        { LedChipset::WS2811_400,  "WS2811_400",  "WS2811 (400 kHz)",           800,  800, 900, false },
        { LedChipset::UCS1903,     "UCS1903",     "UCS1903 (400 kHz)",          500, 1500, 500, false },
        { LedChipset::UCS1904,     "UCS1904",     "UCS1904",                    400,  400, 450, false },
        { LedChipset::SM16703,     "SM16703",     "SM16703",                    300,  600, 300, false },
        { LedChipset::TM1809,      "TM1809",      "TM1809",                     350,  350, 450, false },
        { LedChipset::PL9823,      "PL9823",      "PL9823",                     350, 1010, 350, false },
    };
    count = sizeof(kCatalog) / sizeof(kCatalog[0]);
    return kCatalog;
}

inline const LedChipsetInfo& ledChipsetInfo(LedChipset id) {
    size_t n;
    const LedChipsetInfo* cat = ledChipsetCatalog(n);
    for (size_t i = 0; i < n; i++) {
        if (cat[i].id == id) return cat[i];
    }
    return cat[0];   // WS2812B — the catalog always has it first
}

inline const char* ledChipsetKey(LedChipset id) { return ledChipsetInfo(id).key; }

// Parse a JSON/NVS key ("WS2812B", "SK6812_RGBW", ...). Case-sensitive on
// purpose: these are ids, not user prose. Returns false and leaves `out`
// untouched on an unknown key.
inline bool parseLedChipset(const char* key, LedChipset& out) {
    if (!key) return false;
    size_t n;
    const LedChipsetInfo* cat = ledChipsetCatalog(n);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(cat[i].key, key) == 0) { out = cat[i].id; return true; }
    }
    return false;
}

inline const char* ledColorOrderKey(LedColorOrder o) {
    static const char* const kKeys[] = { "RGB", "RBG", "GRB", "GBR", "BRG", "BGR" };
    uint8_t i = static_cast<uint8_t>(o);
    return i < static_cast<uint8_t>(LedColorOrder::COUNT) ? kKeys[i] : kKeys[2];
}

inline bool parseLedColorOrder(const char* key, LedColorOrder& out) {
    if (!key) return false;
    for (uint8_t i = 0; i < static_cast<uint8_t>(LedColorOrder::COUNT); i++) {
        LedColorOrder o = static_cast<LedColorOrder>(i);
        if (strcmp(ledColorOrderKey(o), key) == 0) { out = o; return true; }
    }
    return false;
}

// ── Data-pin policy ─────────────────────────────────────────────────────────
// Which GPIOs may carry the LED data line. Excluded outright: pins that don't
// exist, input-only pins, the SPI flash / PSRAM bus (touching those crashes the
// chip) and the native USB pair (the console + upload path with
// ARDUINO_USB_CDC_ON_BOOT=1). Strapping pins are ALLOWED — they work fine once
// booted — but the UI flags them so a user with a stubborn boot loop knows why.
// Each target's rule is its own function so the host tests can pin them down.

inline bool isUsableLedPinEsp32S3(int pin) {
    if (pin < 0 || pin > 48) return false;
    if (pin >= 22 && pin <= 25) return false;   // not bonded out
    if (pin >= 26 && pin <= 32) return false;   // SPI flash
    if (pin >= 33 && pin <= 37) return false;   // octal PSRAM/flash (N8R8, T-Display S3)
    if (pin == 19 || pin == 20) return false;   // USB D- / D+
    return true;
}

inline bool isUsableLedPinEsp32C3(int pin) {
    if (pin < 0 || pin > 21) return false;
    if (pin >= 11 && pin <= 17) return false;   // SPI flash (11 = VDD_SPI)
    if (pin == 18 || pin == 19) return false;   // USB D- / D+
    return true;
}

inline bool isUsableLedPinEsp32Classic(int pin) {
    if (pin < 0 || pin > 33) return false;      // 34-39 are input-only
    if (pin >= 6 && pin <= 11) return false;    // SPI flash
    if (pin == 20 || pin == 24 || pin == 28 || pin == 29 || pin == 30 || pin == 31) return false; // absent
    return true;
}

inline bool isStrappingLedPinEsp32S3(int pin) { return pin == 0 || pin == 3 || pin == 45 || pin == 46; }
inline bool isStrappingLedPinEsp32C3(int pin) { return pin == 2 || pin == 8 || pin == 9; }
inline bool isStrappingLedPinEsp32Classic(int pin) { return pin == 0 || pin == 2 || pin == 5 || pin == 12 || pin == 15; }

inline bool isUsableLedPin(int pin) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return isUsableLedPinEsp32S3(pin);
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return isUsableLedPinEsp32C3(pin);
#elif defined(CONFIG_IDF_TARGET_ESP32)
    return isUsableLedPinEsp32Classic(pin);
#else
    return isUsableLedPinEsp32S3(pin);   // host tests / unknown: the widest table
#endif
}

inline bool isStrappingLedPin(int pin) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return isStrappingLedPinEsp32S3(pin);
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return isStrappingLedPinEsp32C3(pin);
#elif defined(CONFIG_IDF_TARGET_ESP32)
    return isStrappingLedPinEsp32Classic(pin);
#else
    return isStrappingLedPinEsp32S3(pin);
#endif
}

// The selection the output driver is bound to at boot.
struct LedHardware {
    LedChipset    chipset;
    LedColorOrder order;
    uint8_t       pin;

    // Compile-time defaults (constants.h / build_flags): what a factory-fresh
    // board runs, and what an invalid persisted value falls back to.
    static LedHardware defaults() {
        LedHardware hw;
        hw.chipset = LedChipset::LED_STRIP_TYPE;
        hw.order   = LedColorOrder::LED_COLOR_MODE;
        hw.pin     = static_cast<uint8_t>(LED_DATA_PIN);
        return hw;
    }

    bool operator==(const LedHardware& o) const {
        return chipset == o.chipset && order == o.order && pin == o.pin;
    }
    bool operator!=(const LedHardware& o) const { return !(*this == o); }
};

} // namespace lume

#endif // LUME_LED_HARDWARE_H
