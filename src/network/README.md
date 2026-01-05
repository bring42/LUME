# Network Services

WiFi, OTA updates, and web server configuration.

## Components

### WiFi ([wifi.cpp](wifi.cpp))
Manages WiFi connectivity with fallback AP mode.

```cpp
// Station mode (connect to existing network)
setupWiFi();  // Uses credentials from storage

// AP mode fallback
// SSID: LUME-Setup
// Password: ledcontrol
// IP: 192.168.4.1
```

### Web Server ([server.cpp](server.cpp))
AsyncWebServer setup and route registration.

Routes are registered in **specific-to-general order**:
```cpp
// Correct order - specific routes first
server.on("/api/prompt/apply", HTTP_POST, ...);
server.on("/api/prompt", HTTP_POST, ...);

// Wrong - general route would catch both
server.on("/api/prompt", HTTP_POST, ...);
server.on("/api/prompt/apply", HTTP_POST, ...);  // Never reached!
```

### OTA ([ota.cpp](ota.h))
Over-the-air firmware updates.

```bash
# After initial USB flash, use OTA
pio run -t upload --upload-port lume.local
```

Password: `ledcontrol` or configured auth token.

## Static Files

Web UI assets served from LittleFS:
- `data/index.html`
- `data/assets/app.js`
- `data/assets/app.css`

Upload with: `pio run -t uploadfs`

## mDNS

Device accessible at `http://lume.local` via mDNS/Bonjour.
