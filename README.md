<p align="center">
  <img src="https://github.com/user-attachments/assets/2dc66001-6dd6-4d1a-9cac-4835a006be74" alt="LUME Logo" width="800">
</p>

# LUME — AI LED Strip Controller

> Tell your LEDs what to do. In plain English.

ESP32-S3 + FastLED firmware with modern Web UI, API, sACN, OTA, and natural-language effects.

![Build](https://github.com/bring42/LUME/actions/workflows/build.yml/badge.svg)
![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange)
![License](https://img.shields.io/badge/license-MIT-blue)
![Status](https://img.shields.io/badge/status-active%20development-brightgreen)

<p align="center">
  <img src="https://github.com/user-attachments/assets/64ef8967-df5d-48bd-b54d-8d7a314b29e8" alt="LUME Web UI" width="400">
</p>


---

## 💡 Why LUME?


LUME brings **AI-powered control** to your LED strips without sacrificing flexibility. Whether you want to say "make it look like a campfire" or precisely configure sACN universes, LUME handles both.

- **Bring your favorites** — Port effects from WLED, write new ones, or use the built-in collection. You can also add new effects using the pre-written Copilot prompt (see [ADDING_EFFECTS.md](docs/ADDING_EFFECTS.md)).
- **API-first design** — Control via REST, MQTT, sACN, or natural language
- **Hackable** — Clean C++ codebase with effect registration macros and metadata that make each effect's parameters and UI behavior obvious from the code itself

---

## ✨ Features

| Category | What You Get |
|----------|--------------|
| 🤖 **AI Effects** | Describe effects in natural language via Anthropic Claude |
| 📱 **Modern Web UI** | Responsive, mobile-friendly, works offline |
| 🔲 **Segments** | Split your strip into independent zones with different effects |
| 🎨 **23 Built-in Effects** | Rainbow, Fire, Confetti, Meteor, Twinkle, Candle, Gradient, Pulse... |
| 🎨 **Color Palettes** | 12 palettes: Ocean, Lava, Sunset, Forest, Party... |
| 🔄 **OTA Updates** | Update firmware wirelessly — never unplug again! |
| 💾 **Persistent Storage** | Settings survive reboots and updates |
| 📡 **sACN/E1.31** | Professional DMX protocol for lighting software integration |
| 🏠 **MQTT** | (Untested) Home Assistant auto-discovery, full control via MQTT |
| 🔐 **Optional Auth** | Protect API & OTA with a token |
| ⚡ **Power Limiting** | Automatic current limiting protects your PSU |
| 🌙 **Nightlight Mode** | Gradual fade-to-sleep over configurable duration |
| 🔗 **mDNS** | Access via `http://lume.local` |

---

## 🚀 Quick Start

### What You Need

- **ESP32-S3 Board** (T-Display S3, DevKitC-1, or any S3 with PSRAM)
- **WS2812B LED Strip** (default: GPIO 21 — set `LED_DATA_PIN` in [constants.h](src/constants.h))
- **5V Power Supply** (sized for your LED count)

### Flash & Go

```bash
git clone https://github.com/bring42/LUME.git
cd LUME
pio run -t upload
```

That's it. No config files needed.

### First Boot

1. **Power on** your ESP32-S3 board
2. **Connect** to WiFi network `LUME-Setup` (password: `ledcontrol`)
3. **Open** `http://192.168.4.1`
4. **Configure** your home WiFi, LED count, and [Anthropic API key](https://console.anthropic.com/)
5. **Done!** Access via `http://lume.local`

> 💾 **Your settings are saved to flash memory** and survive firmware updates. You only need to configure once.

<details>
<summary>🛠️ <b>Dev Notes</b></summary>

**Skip the setup wizard:** Create `src/secrets.h` from [secrets.h.example](src/secrets.h.example) to auto-configure on boot. Useful when reflashing frequently during development.

```cpp
#define DEV_WIFI_SSID "YourNetwork"
#define DEV_WIFI_PASSWORD "YourPassword"
#define DEV_API_KEY "sk-or-..."
```

**Full erase:** To wipe all saved settings: `pio run -t erase`

</details>

---

## 🎮 Control Methods


### Web UI
Access the responsive web interface from any device on your network.

### AI Prompts
Tell the LEDs what you want:
- *"gentle ocean waves"*
- *"warm fireplace glow"*  
- *"rainbow that slowly shifts colors"*
- *"cozy sunset vibes"*

### API
Full REST API for automation and integration ([see full docs](docs/API_V2.md)):
```bash
# Set all LEDs to red
curl -X POST http://lume.local/api/pixels \
  -H "Content-Type: application/json" \
  -d '{"fill": [255, 0, 0]}'

# Generate an AI effect
curl -X POST http://lume.local/api/prompt \
  -H "Content-Type: application/json" \
  -d '{"prompt": "northern lights"}'
```

### sACN/E1.31
Connect professional lighting software like QLC+, xLights, or TouchDesigner.

### MQTT (Untested)
Integrate with Home Assistant or Node-RED using MQTT topics. See the [MQTT Guide](docs/MQTT.md) for topic structure and setup notes. This feature is available but not fully tested yet.

<p align="center">
  <img src="https://github.com/user-attachments/assets/d8b91c1c-d545-416c-94dd-74323f53be13" alt="LUME Settings Menu" width="400">
</p>

---

## 📖 Documentation

| Doc | What's Inside |
|-----|---------------|
| [Hardware Setup](docs/HARDWARE.md) | Wiring, power calculation, GPIO pins |
| [API Reference](docs/API_V2.md) | All REST endpoints with examples |
| [Adding Effects](docs/ADDING_EFFECTS.md) | Guide to creating custom LED effects |
| [sACN Guide](docs/SACN.md) | E1.31 protocol setup and Python examples |
| [MQTT Guide](docs/MQTT.md) | Home Assistant, Node-RED, topic structure |
| [Development](docs/DEVELOPMENT.md) | Architecture, building, contributing |

---

## 🔧 Configuration

Key settings in `src/constants.h`:

```cpp
#define LED_DATA_PIN 21               // GPIO for LED data line
constexpr uint16_t LED_MAX_MILLIAMPS = 2000;  // Match your PSU
constexpr uint16_t MAX_LED_COUNT = 300;       // Maximum LEDs
constexpr const char* MDNS_HOSTNAME = "lume";
```

See [Hardware Setup](docs/HARDWARE.md) for power calculations and GPIO configuration.

---

## � Contributing

**Love WLED effects?** Bring them over! Check out [ADDING_EFFECTS.md](docs/ADDING_EFFECTS.md) for the porting guide. The effect system is designed to be simple and self-documenting.

**Ideas for new features?** Open an issue or PR. This project is young and welcomes contributions!

**Found a bug?** Please report it. Include your board model, firmware version, and steps to reproduce.

---

## �🧪 Built With

- [FastLED](https://fastled.io/) - LED control library
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - Async web server
- [ArduinoJson](https://arduinojson.org/) - JSON parsing
- [Anthropic Claude](https://anthropic.com/) - AI natural language control

---


## 📏 Footprint & Performance

- **Firmware size:** ~1.2MB (ESP32-S3, including all features and web UI)
- **Web UI assets:** ~200KB (served from LittleFS)
- **RAM usage:** ~120KB at idle (with 150 LEDs, 2 segments, and web server active)
- **Max LEDs:** 300 (default, can be increased with more PSRAM)
- **Frame rate:** 60 FPS typical with up to 300 LEDs and most effects
- **Startup time:** <2s to web UI ready

Tested on LilyGo T-Display S3 and ESP32-S3 DevKitC-1. See [src/constants.h](src/constants.h) for hardware limits and tuning.

---
## 🗺️ What's Coming

This project is in active development. On the horizon:

- 📊 More effects and palettes
- 🔲 2D matrix support
- 🎛️ Physical button controls
- 🎬 Scene presets and scheduling
- 🔮 Matter/Thread support

---

## 📝 License

MIT License - do whatever you want with it.

---

<p align="center">
  <i>Made with 💡 and too many late nights</i>
</p>
