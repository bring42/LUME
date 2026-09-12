// Native (host-compiled) tests for the runtime LED hardware catalog
// (core/led_hardware.h): the strip chipset / colour order / data pin selection
// that the web UI and POST /api/config write, NVS persists, and the FastLED
// backend binds at boot. Pure data + parsing, so it runs without FastLED.
#include <unity.h>

#include "core/led_hardware.h"

using namespace lume;

void setUp() {}
void tearDown() {}

// Every catalog entry round-trips through its key: what the API emits is
// exactly what it accepts back, and what NVS stores survives a reload.
void test_chipset_keys_round_trip() {
    size_t n;
    const LedChipsetInfo* cat = ledChipsetCatalog(n);
    TEST_ASSERT_EQUAL_size_t((size_t)LedChipset::COUNT, n);
    for (size_t i = 0; i < n; i++) {
        LedChipset parsed = LedChipset::COUNT;
        TEST_ASSERT_TRUE_MESSAGE(parseLedChipset(cat[i].key, parsed), cat[i].key);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)cat[i].id, (uint8_t)parsed);
        TEST_ASSERT_EQUAL_STRING(cat[i].key, ledChipsetKey(cat[i].id));
        // Every enumerator appears exactly once, in enum order.
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, (uint8_t)cat[i].id);
    }
}

// Timings are what feed the RMT driver: none may be zero, and the well-known
// ones must match FastLED's chipsets.h (WS2812B 250/625/375, SK6812 300/300/600).
void test_chipset_timings_are_sane_and_match_fastled() {
    size_t n;
    const LedChipsetInfo* cat = ledChipsetCatalog(n);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_TRUE_MESSAGE(cat[i].t1Ns > 0 && cat[i].t2Ns > 0 && cat[i].t3Ns > 0, cat[i].key);
    }
    const LedChipsetInfo& ws = ledChipsetInfo(LedChipset::WS2812B);
    TEST_ASSERT_EQUAL_UINT16(250, ws.t1Ns);
    TEST_ASSERT_EQUAL_UINT16(625, ws.t2Ns);
    TEST_ASSERT_EQUAL_UINT16(375, ws.t3Ns);
    TEST_ASSERT_FALSE(ws.rgbw);
    const LedChipsetInfo& sk = ledChipsetInfo(LedChipset::SK6812_RGBW);
    TEST_ASSERT_EQUAL_UINT16(300, sk.t1Ns);
    TEST_ASSERT_EQUAL_UINT16(300, sk.t2Ns);
    TEST_ASSERT_EQUAL_UINT16(600, sk.t3Ns);
    TEST_ASSERT_TRUE(sk.rgbw);
    // The RGB SK6812 shares timing with the RGBW part; only the wire width differs.
    TEST_ASSERT_FALSE(ledChipsetInfo(LedChipset::SK6812).rgbw);
}

// Unknown / empty / case-mismatched keys are rejected and leave the output alone
// (the caller keeps its current value or default — never a garbage enum).
void test_chipset_parse_rejects_unknown() {
    LedChipset c = LedChipset::WS2811;
    TEST_ASSERT_FALSE(parseLedChipset("WS9999", c));
    TEST_ASSERT_FALSE(parseLedChipset("", c));
    TEST_ASSERT_FALSE(parseLedChipset(nullptr, c));
    TEST_ASSERT_FALSE(parseLedChipset("ws2812b", c));   // ids, not prose
    TEST_ASSERT_EQUAL_UINT8((uint8_t)LedChipset::WS2811, (uint8_t)c);
    // An out-of-range enum value resolves to the catalog's first row (WS2812B)
    // rather than reading past the table.
    TEST_ASSERT_EQUAL_STRING("WS2812B", ledChipsetKey(LedChipset::COUNT));
}

void test_color_order_keys_round_trip() {
    const char* keys[] = { "RGB", "RBG", "GRB", "GBR", "BRG", "BGR" };
    for (uint8_t i = 0; i < 6; i++) {
        LedColorOrder o = LedColorOrder::COUNT;
        TEST_ASSERT_TRUE(parseLedColorOrder(keys[i], o));
        TEST_ASSERT_EQUAL_UINT8(i, (uint8_t)o);
        TEST_ASSERT_EQUAL_STRING(keys[i], ledColorOrderKey(o));
    }
    LedColorOrder o = LedColorOrder::BRG;
    TEST_ASSERT_FALSE(parseLedColorOrder("RGBW", o));
    TEST_ASSERT_FALSE(parseLedColorOrder("grb", o));
    TEST_ASSERT_FALSE(parseLedColorOrder(nullptr, o));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)LedColorOrder::BRG, (uint8_t)o);
    TEST_ASSERT_EQUAL_STRING("GRB", ledColorOrderKey(LedColorOrder::COUNT));  // out of range -> GRB
}

// The compile-time defaults (constants.h, unless a build flag overrides them)
// seed a fresh board and every invalid-value fallback.
void test_defaults_come_from_constants() {
    LedHardware hw = LedHardware::defaults();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)LedChipset::LED_STRIP_TYPE, (uint8_t)hw.chipset);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)LedColorOrder::LED_COLOR_MODE, (uint8_t)hw.order);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)LED_DATA_PIN, hw.pin);
    LedHardware other = hw;
    TEST_ASSERT_TRUE(hw == other);
    other.pin = (uint8_t)(hw.pin + 1);
    TEST_ASSERT_TRUE(hw != other);
}

// ESP32-S3: flash (26-32), octal PSRAM (33-37), USB (19-20) and the unbonded
// 22-25 are refused; ordinary GPIOs — strapping ones included — are allowed.
void test_pin_policy_esp32_s3() {
    const int refused[] = { -1, 19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 49, 100 };
    for (int p : refused) TEST_ASSERT_FALSE_MESSAGE(isUsableLedPinEsp32S3(p), "S3 refused pin accepted");
    const int allowed[] = { 0, 1, 2, 3, 4, 5, 8, 12, 16, 18, 21, 38, 45, 46, 47, 48 };
    for (int p : allowed) TEST_ASSERT_TRUE_MESSAGE(isUsableLedPinEsp32S3(p), "S3 usable pin refused");
    TEST_ASSERT_TRUE(isStrappingLedPinEsp32S3(0));
    TEST_ASSERT_TRUE(isStrappingLedPinEsp32S3(45));
    TEST_ASSERT_FALSE(isStrappingLedPinEsp32S3(2));
}

// ESP32-C3: only GPIO 0-21 exist; 11-17 are the flash bus, 18-19 the USB pair.
void test_pin_policy_esp32_c3() {
    const int refused[] = { -1, 11, 12, 13, 14, 15, 16, 17, 18, 19, 22, 48 };
    for (int p : refused) TEST_ASSERT_FALSE_MESSAGE(isUsableLedPinEsp32C3(p), "C3 refused pin accepted");
    const int allowed[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21 };
    for (int p : allowed) TEST_ASSERT_TRUE_MESSAGE(isUsableLedPinEsp32C3(p), "C3 usable pin refused");
    TEST_ASSERT_TRUE(isStrappingLedPinEsp32C3(8));
    TEST_ASSERT_TRUE(isStrappingLedPinEsp32C3(9));
    TEST_ASSERT_FALSE(isStrappingLedPinEsp32C3(4));
}

// Classic ESP32: 6-11 flash, 34-39 input-only, and the never-bonded numbers.
void test_pin_policy_esp32_classic() {
    const int refused[] = { -1, 6, 7, 8, 9, 10, 11, 20, 24, 28, 34, 35, 36, 39 };
    for (int p : refused) TEST_ASSERT_FALSE_MESSAGE(isUsableLedPinEsp32Classic(p), "ESP32 refused pin accepted");
    const int allowed[] = { 0, 2, 4, 5, 12, 13, 16, 17, 18, 21, 22, 23, 25, 26, 27, 32, 33 };
    for (int p : allowed) TEST_ASSERT_TRUE_MESSAGE(isUsableLedPinEsp32Classic(p), "ESP32 usable pin refused");
}

// The compile-time default pin must pass the policy it is validated against —
// otherwise a fresh board would "fall back" from its own default.
void test_default_pin_is_usable() {
    TEST_ASSERT_TRUE(isUsableLedPin(LED_DATA_PIN));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_chipset_keys_round_trip);
    RUN_TEST(test_chipset_timings_are_sane_and_match_fastled);
    RUN_TEST(test_chipset_parse_rejects_unknown);
    RUN_TEST(test_color_order_keys_round_trip);
    RUN_TEST(test_defaults_come_from_constants);
    RUN_TEST(test_pin_policy_esp32_s3);
    RUN_TEST(test_pin_policy_esp32_c3);
    RUN_TEST(test_pin_policy_esp32_classic);
    RUN_TEST(test_default_pin_is_usable);
    return UNITY_END();
}
