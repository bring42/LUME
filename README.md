<p align="center">
  <img src="https://github.com/user-attachments/assets/24a7b730-099f-4a7c-b635-7a351b6ab7d1" alt="LUME Logo" width="400">
</p>

# LUME — AI LED Strip Controller

> Tell your LEDs what to do. In plain English.

ESP32-S3 + FastLED firmware with modern Web UI, API, sACN, OTA, and natural-language effects.

![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange)
![License](https://img.shields.io/badge/license-MIT-blue)
![Status](https://img.shields.io/badge/status-active%20development-brightgreen)

<p align="center">
  <img src="https://github.com/user-attachments/assets/64ef8967-df5d-48bd-b54d-8d7a314b29e8" alt="LUME Web UI" width="400">
</p>

---

## ✨ Features

| Category | What You Get |
|----------|--------------|
| 🤖 **AI Effects** | Describe effects in natural language via OpenRouter API |
| 🎨 **15 Built-in Effects** | Rainbow, Fire, Confetti, Meteor, Twinkle, Candle, Breathe... |
| 🎨 **Color Palettes** | 12 palettes: Ocean, Lava, Sunset, Forest, Party... |
| 📡 **sACN/E1.31** | Professional DMX protocol for lighting software integration |
| 📱 **Modern Web UI** | Responsive, mobile-friendly, works offline |
| 🌙 **Nightlight Mode** | Gradual fade-to-sleep over configurable duration |
| ⚡ **Power Limiting** | Automatic current limiting protects your PSU |
| 🔗 **mDNS** | Access via `http://lume.local` |
| 🔄 **OTA Updates** | Update firmware over WiFi |
| 💾 **Persistent Storage** | Settings survive reboots |

---

## 🚀 Quick Start

### What You Need

- **LILYGO T-Display S3** (or compatible ESP32-S3)
- **WS2812B LED Strip** (data wire to GPIO 21)
- **5V Power Supply** (sized for your LED count)

### Flash & Go

```bash
git clone https://github.com/bring42/LUME.git
cd LUME
pio run -t upload
```

That's it. No config files needed.

### First Boot

1. **Power on** the T-Display S3
2. **Connect** to WiFi network `LUME-Setup` (password: `ledcontrol`)
3. **Open** `http://192.168.4.1`
4. **Configure** your home WiFi, LED count, and [OpenRouter API key](https://openrouter.ai/)
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
Full REST API for automation and integration:
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

---

## 📖 Documentation

| Doc | What's Inside |
|-----|---------------|
| [Hardware Setup](docs/HARDWARE.md) | Wiring, power calculation, GPIO pins |
| [API Reference](docs/API.md) | All REST endpoints with examples |
| [sACN Guide](docs/SACN.md) | E1.31 protocol setup and Python examples |
| [Development](docs/DEVELOPMENT.md) | Architecture, building, contributing |

---

## 🔧 Configuration

Key settings in `src/constants.h`:

```cpp
constexpr uint16_t LED_MAX_MILLIAMPS = 2000;  // Match your PSU
constexpr uint16_t MAX_LED_COUNT = 300;       // Maximum LEDs
constexpr const char* MDNS_HOSTNAME = "lume";
```

See [Hardware Setup](docs/HARDWARE.md) for power calculations and GPIO configuration.

---

## 🧪 Built With

- [FastLED](https://fastled.io/) - LED control library
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - Async web server
- [ArduinoJson](https://arduinojson.org/) - JSON parsing
- [OpenRouter](https://openrouter.ai/) - AI API access

---

## 🗺️ What's Coming

This project is in active development. On the horizon:

- 📊 More effects (porting favorites from the community)
- 🔲 2D matrix support
- 🎛️ Physical button controls
- 🏠 MQTT for home automation
- 🔮 Matter/Thread support

---

## 📝 License

MIT License - do whatever you want with it.

---

<p align="center">
  <i>Made with 💡 and too many late nights</i>
</p>
