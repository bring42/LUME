# LUME Source Code

ESP32-S3 firmware for AI-powered LED strip control.

## Directory Structure

### [api/](api/)
REST API endpoint handlers for web UI and external control.

### [core/](core/)
Core system: controller, segments, effects registry, command queue.

### [visuallib/](visuallib/)
Self-registering LED effects library (rainbow, fire, sparkle, etc.).

### [network/](network/)
WiFi, web server, OTA updates, and mDNS configuration.

### [protocols/](protocols/)
Network protocols (sACN/E1.31, MQTT) with decoupled interface.

## Key Files

- **main.cpp** - Setup and main loop
- **constants.h** - Hardware config and compile-time constants
- **storage.cpp** - NVS configuration persistence
- **logging.h** - Logging macros by subsystem

## Data Flow

```
┌─────────────┐
│  Web UI     │
│  sACN/MQTT  │
└──────┬──────┘
       │
       ▼
┌─────────────┐     ┌──────────┐
│ API Handler │────▶│ Commands │
└─────────────┘     └────┬─────┘
                         │
       ┌─────────────────┘
       │
       ▼
┌──────────────┐    ┌──────────┐    ┌────────┐
│  Controller  │───▶│ Segments │───▶│ Effects│
│  (60 FPS)    │    │          │    │        │
└──────────────┘    └──────────┘    └────────┘
       │
       ▼
┌──────────────┐
│  FastLED     │
│  leds[]      │
└──────────────┘
```

## Build & Flash

```bash
# Initial flash via USB
pio run -t upload

# OTA after initial flash
pio run -t upload --upload-port lume.local

# Upload web UI files
pio run -t uploadfs
```

## Development Guidelines

- **Namespace**: All core code in `namespace lume {}`
- **Singleton**: `lume::controller` is the global instance
- **Thread safety**: Use atomic buffers for protocol data
- **Effect state**: Use segment scratchpad, not static variables
- **Logging**: Use macros from logging.h
- **Constants**: Define in constants.h with units (`_MS`, `_SIZE`)

See [../docs/DEVELOPMENT.md](../docs/DEVELOPMENT.md) for more details.
