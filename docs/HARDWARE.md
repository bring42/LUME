# Hardware Setup Guide

## Supported Hardware

**Primary Target:**
- **LILYGO T-Display S3** (ESP32-S3 with 8MB PSRAM)

**Should Also Work:**
- Generic ESP32-S3 DevKit boards
- ESP32-S2 (single core, less RAM)

**Not Supported:**
- ESP8266 (too limited)
- Original ESP32 (prefer S3 for PSRAM/USB-OTG)

---

## Required Components

| Component | Specification | Notes |
|-----------|---------------|-------|
| LILYGO T-Display S3 | ESP32-S3 based | Built-in display, USB-C |
| WS2812B LED Strip | Any length | Default: 160 LEDs |
| 5V Power Supply | ~60mA per LED at full white | See power calculation |
| Wire | 18-22 AWG | For data and ground |

---

## Wiring Diagram

```
                    ┌─────────────────┐
                    │  T-Display S3   │
                    │                 │
        ┌───────────┤ GPIO 21 (Data)  │
        │           │                 │
        │    ┌──────┤ GND             │
        │    │      │                 │
        │    │      └─────────────────┘
        │    │
        │    │      ┌─────────────────┐
        │    │      │   WS2812B Strip │
        │    └──────┤ GND             │
        └───────────┤ DIN (Data In)   │
             ┌──────┤ +5V             │
             │      └─────────────────┘
             │
             │      ┌─────────────────┐
             └──────┤ 5V Power Supply │
                    │ (+ terminal)    │
        ┌───────────┤ (- terminal/GND)│
        │           └─────────────────┘
        │
        └── Connect to common ground
```

### Critical Connections

1. **Data Line (GPIO 21 → DIN):** Keep short, add 330Ω resistor if > 1m
2. **Common Ground:** ESP32 GND must connect to LED strip GND
3. **Power Injection:** For strips > 150 LEDs, inject power at both ends

---

## Power Calculation

Each WS2812B LED draws up to **60mA at full white brightness**.

| LED Count | Max Current | Recommended PSU |
|-----------|-------------|-----------------|
| 30 | 1.8A | 5V 2A |
| 60 | 3.6A | 5V 5A |
| 100 | 6A | 5V 10A |
| 160 | 9.6A | 5V 10A |
| 300 | 18A | 5V 20A |

**Good news:** The firmware includes automatic power limiting. Configure your PSU capacity in [constants.h](../src/constants.h):

```cpp
constexpr uint8_t LED_VOLTAGE = 5;           // 5V LEDs
constexpr uint16_t LED_MAX_MILLIAMPS = 2000; // Your PSU limit in mA
```

FastLED will automatically dim LEDs to stay within the power budget.

---

## GPIO Pin Configuration

**Default data pin: GPIO 21**

To use a different GPIO pin:

1. Edit [constants.h](../src/constants.h)
2. Change `LED_DATA_PIN` to your desired GPIO:
   ```cpp
   #define LED_DATA_PIN 16  // Example: use GPIO 16 instead
   ```
3. Rebuild and flash: `pio run -t upload`

> **Note:** The pin is set at compile time (FastLED requirement). Changing it requires reflashing.

### Recommended GPIOs

| GPIO | T-Display S3 | Notes |
|------|--------------|-------|
| 21 | ✅ Default | Works great |
| 16 | ✅ Available | Alternative |
| 17 | ✅ Available | Alternative |
| 18 | ✅ Available | Alternative |

**Avoid:** GPIOs used for flash, display, touch, or boot strapping.

---

## LED Strip Types

The firmware is configured for **WS2812B (GRB)** strips. To use other types:

Edit [led_controller.cpp](../src/led_controller.cpp):

```cpp
// WS2812B (most common, GRB order)
FastLED.addLeds<WS2812B, 21, GRB>(leds, ledCount);

// WS2811 (12V strips, RGB order)
FastLED.addLeds<WS2811, 21, RGB>(leds, ledCount);

// SK6812 (RGBW - note: W channel not yet supported)
FastLED.addLeds<SK6812, 21, GRB>(leds, ledCount);

// APA102/SK9822 (2-wire, needs clock pin)
FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds, ledCount);
```

---

## First Power-On

1. **Connect USB-C** to T-Display S3
2. **Flash firmware** via PlatformIO:
   ```bash
   pio run -t upload
   ```
3. **Connect to WiFi** `LUME-Setup` (password: `ledcontrol`)
4. **Open** `http://192.168.4.1`
5. **Configure** your home WiFi in Settings
6. **Done!** Access via `http://lume.local`

---

## Troubleshooting

### LEDs not lighting up

1. ✅ Check 5V power is connected
2. ✅ Verify GND is shared between ESP32 and LED strip
3. ✅ Confirm data wire is on GPIO 21 (or your configured pin)
4. ✅ Try a shorter data wire or add 330Ω resistor
5. ✅ Check serial monitor for errors

### Only first few LEDs light up

- Data signal integrity issue
- Add a 330-470Ω resistor between GPIO and first LED
- Ensure good solder joints
- Try shorter data cable

### Colors are wrong

- Check color order (GRB vs RGB)
- Edit `FastLED.addLeds<WS2812B, 21, GRB>` → try `RGB` or `BRG`

### Random flickering

- Add 1000µF capacitor across power lines
- Ensure adequate power supply
- Check for loose connections
- Reduce brightness if PSU is undersized

### Device keeps rebooting

- Power supply can't handle load
- Reduce `LED_MAX_MILLIAMPS` in constants.h
- Check for short circuits

---

## Advanced: Parallel Output

For very long strips (500+ LEDs), you can reduce latency by using multiple GPIO pins in parallel. This requires firmware modifications:

```cpp
// Example: 4 parallel outputs
FastLED.addLeds<WS2812B, 21>(leds, 0, 125);          // LEDs 0-124
FastLED.addLeds<WS2812B, 16>(leds, 125, 125);        // LEDs 125-249
FastLED.addLeds<WS2812B, 17>(leds, 250, 125);        // LEDs 250-374
FastLED.addLeds<WS2812B, 18>(leds, 375, 125);        // LEDs 375-499
```

This is not enabled by default—modify source if needed.
