# Hardware Setup Guide

## Board Configuration

LUME is configured for **ESP32-C3 DevKit boards** by default (compiles successfully, untested on hardware). It has been thoroughly tested on the **LILYGO T-Display S3** (ESP32-S3) and should work with most ESP32-S3 and ESP32-C3 boards. Here's how to set it up for your specific hardware:

> 💾 **PSRAM:** Optional for ESP32-S3. The default configuration (300 LEDs, 8 segments) uses only ~6KB of RAM. ESP32-C3 has 400KB built-in RAM (no PSRAM support). Both chips handle 300 LEDs comfortably.

### Step 1: Identify Your Board

**Have a generic ESP32-C3 or ESP32-S3 DevKit?** The default is currently ESP32-C3. Check `platformio.ini` and change if needed!

**Have a LILYGO T-Display S3?** (Currently the only fully tested board) You need to configure these files:

#### 1. Set Board Environment in `platformio.ini`

**Available pre-configured boards:**
- `esp32-c3-devkitm-1` — Generic ESP32-C3 DevKit (default, untested on hardware)
- `esp32-s3-devkitc-1` — Generic ESP32-S3 DevKit (untested on hardware)
- `lilygo-t-display-s3` — LILYGO T-Display S3 (tested ✅)

**To change board:**
```ini
[platformio]
default_envs = lilygo-t-display-s3    ; ← Change to your board
```

**For other specific boards:** Find your board identifier:
```bash
pio boards esp32-s3    # For ESP32-S3 boards
pio boards esp32-c3    # For ESP32-C3 boards
```

Then either:
- Use an existing environment if available, or
- Create a new `[env:yourboard]` section (copy from an existing one and change the `board =` line)

**Common ESP32-S3 board names:**
- `esp32-s3-devkitc-1` — Generic ESP32-S3 DevKit (most common)
- `esp32s3box` — ESP32-S3-BOX
- `adafruit_feather_esp32s3` — Adafruit Feather ESP32-S3
- `um_tinys3` — Unexpected Maker TinyS3
- `lolin_s3` — WEMOS/Lolin S3

**Common ESP32-C3 board names:**
- `esp32-c3-devkitm-1` — Generic ESP32-C3 DevKitM (most common)
- `esp32-c3-devkitc-02` — ESP32-C3-DevKitC-02
- `lolin_c3_mini` — WEMOS/Lolin C3 Mini
- `adafruit_qtpy_esp32c3` — Adafruit QT Py ESP32-C3

**Can't find your exact board?** Use `esp32-s3-devkitc-1` or `esp32-c3-devkitm-1` as generic fallbacks — they usually work.

#### 2. Set LED Data Pin in `src/constants.h`

Check your board's pinout diagram and choose a free GPIO pin:
```cpp
#define LED_DATA_PIN 8    // ← Default for ESP32-C3 (change to match your wiring)
```

**ESP32-C3 safe pins:** GPIO 0-1, 4-7, 10, 18-21 (avoid GPIO 2-3, 8-9, 11-17 used for flash/strapping)
**ESP32-S3 safe pins:** Most GPIO pins work, common choices: 2, 5, 16, 21, 48

**Safe GPIO choices for ESP32-S3:**
- ✅ Good: 2, 5, 12-14, 16-21, 35-37, 47-48
- ⚠️ Avoid: 0 (boot button), 19-20 (USB), 45-46 (strapping pins)

#### 3. Adjust Build Flags (If Needed)

Most boards work with the default settings. If you get compile errors, try removing these flags from `platformio.ini`:

```ini
build_flags = 
    ; -DBOARD_HAS_PSRAM         ; ← Optional: Only needed for >300 LEDs
    ; -DARDUINO_USB_CDC_ON_BOOT=1  ; ← Comment out for upload issues
```

---

## Supported Hardware

**Tested & Confirmed:**
- **LILYGO T-Display S3** (ESP32-S3, 8MB PSRAM available but not required) ✅

**Compiles Successfully & Should Work:**
- **Generic ESP32-S3 DevKit** (esp32-s3-devkitc-1) — Default configuration
- ESP32-S3-BOX
- Adafruit Feather ESP32-S3
- Other ESP32-S3 boards (with or without PSRAM)

> 🧪 **Help expand this list!** If you've successfully tested LUME on a different board, please [open an issue](https://github.com/bring42/LUME/issues) or PR to add it to the confirmed list.

**Not Supported:**
- ESP8266 (insufficient RAM/performance)
- Original ESP32 (use S3 for better performance and USB-C)
- ESP32-S2 (single core, might work but not recommended)

> **Got it working on a different board?** Please open a PR to add it to this list!

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
                    │   ESP32-S3      │
                    │     Board       │
        ┌───────────┤ GPIO 21* (Data) │  *Or your chosen pin
        │           │                 │   (set in constants.h)
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

### Logic Level Shifting

The ESP32-S3 outputs 3.3V logic, while WS2812B LEDs expect 5V signals. In practice, **most WS2812B strips work fine with 3.3V** because they typically recognize anything above ~2.5V as HIGH.

However, if you experience flickering, glitching, or random colors—especially with longer data wires—you may need a level shifter:

- **74AHCT125** (recommended) - Simple, fast, reliable
- **74HCT245** - Bidirectional, overkill for LEDs but common
- **Dedicated level shifter boards** - Many available on Amazon/AliExpress

> **Tip:** Before adding a level shifter, try keeping the data wire short (<30cm) and adding a 330Ω resistor in series. This solves most issues.

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

### Recommended GPIOs for ESP32-S3

| GPIO | Safe? | Notes |
|------|-------|-------|
| 2, 5 | ✅ Good | Commonly available, safe choices |
| 12-14 | ✅ Good | General purpose |
| 16-18 | ✅ Good | General purpose |
| 21 | ✅ Default | Current default pin |
| 35-37 | ✅ Good | General purpose |
| 47-48 | ✅ Good | General purpose, but check your board |
| 0 | ⚠️ Avoid | Boot button |
| 19-20 | ⚠️ Avoid | USB D- and D+ |
| 45-46 | ⚠️ Avoid | Strapping pins, can cause boot issues |

**Check your board's pinout!** Some boards use certain GPIOs for onboard peripherals (display, buttons, SD card, etc.). Consult your board's documentation or schematic.

---

## LED Strip Types

The firmware is configured for **WS2812B (GRB)** strips. To use other types:

Edit [core/controller.cpp](../src/core/controller.cpp):

```cpp
// WS2812B (most common, GRB order)
FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds_, ledCount_);

// WS2811 (12V strips, RGB order)
FastLED.addLeds<WS2811, LED_DATA_PIN, RGB>(leds_, ledCount_);

// SK6812 (RGBW - note: W channel not yet supported)
FastLED.addLeds<SK6812, LED_DATA_PIN, GRB>(leds_, ledCount_);

// APA102/SK9822 (2-wire, needs clock pin)
FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds_, ledCount_);
```

---

## First Power-On

1. **Connect USB-C or Micro-USB** to your ESP32-S3 board (cable must support data, not just charging)
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
