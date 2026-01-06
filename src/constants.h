#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cstdint>
#include <cstddef>

// ═══════════════════════════════════════════════════════════════════════════
// ⚠️  LED CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════
// Settings used to configure FastLED in controller.cpp
// Customize these for your specific hardware setup

// NOTE: ESP32-C3 has GPIO 0-21. Safe pins: 0-1, 4-7, 10, 18-21
// Avoid: GPIO 2-3 (strapping), 8-9 (flash), 11-17 (flash)
#define LED_DATA_PIN                3               // GPIO8 - adjust for your board
#define LED_STRIP_TYPE              WS2812B         // Common addressable RGB LED
#define LED_COLOR_MODE              GRB             // Byte order (GRB for WS2812B)

// Strip Dimensions
// MAX_LED_COUNT sets compile-time buffer size (3 bytes per LED)
// Recommended: 1000 LEDs for smooth 60 FPS performance (FastLED RMT timing limit)
// Memory supports much more: ~10,000 LEDs (S3) or ~5,000 (C3)
// sACN protocol limit: 8 universes × 170 LEDs = 1,360 LEDs max
// See FastLED docs for parallel output to drive more strips simultaneously
constexpr uint16_t MAX_LED_COUNT            = 1000;
constexpr uint16_t LEDS_PER_UNIVERSE        = 170;   // 512 DMX channels ÷ 3 bytes/LED

// Power Management
constexpr uint8_t  LED_VOLTAGE              = 5;     // LED strip voltage
constexpr uint16_t LED_MAX_MILLIAMPS        = 2000;  // Max current (adjust for PSU)

// ═══════════════════════════════════════════════════════════════════════════
// NETWORK CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

// Network Ports
constexpr uint16_t WEB_SERVER_PORT          = 80;
constexpr uint16_t OTA_PORT                 = 3232;
constexpr uint16_t SACN_PORT                = 5568;

// mDNS Hostname (Access via http://lume.local)
#define MDNS_HOSTNAME "lume"

// Timeouts (milliseconds)
constexpr uint32_t WIFI_RETRY_INTERVAL_MS   = 30000;
constexpr uint32_t SACN_DATA_TIMEOUT_MS     = 5000;
constexpr uint32_t SACN_SOURCE_TIMEOUT_MS   = 2500;
constexpr uint32_t HTTP_CLIENT_TIMEOUT_MS   = 30000;

// ═══════════════════════════════════════════════════════════════════════════
// SYSTEM LIMITS & BUFFERS
// ═══════════════════════════════════════════════════════════════════════════

// Memory Buffers
constexpr size_t MAX_REQUEST_BODY_SIZE      = 16384;  // 16KB max POST body
constexpr size_t MAX_JSON_STATE_SIZE        = 4000;   // NVS storage limit
constexpr size_t SYSTEM_PROMPT_BUFFER_SIZE  = 2048;

// Task Configuration
constexpr size_t   ANTHROPIC_TASK_STACK_SIZE = 16384;
constexpr uint8_t  ANTHROPIC_TASK_PRIORITY   = 1;
constexpr uint8_t  ANTHROPIC_TASK_CORE       = 0;

// System Timing
constexpr uint32_t WATCHDOG_TIMEOUT_SEC     = 30;     // Auto-reset timeout
constexpr uint32_t PROMPT_RATE_LIMIT_MS     = 3000;   // Min time between prompts

// ═══════════════════════════════════════════════════════════════════════════
// FEATURE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

// Nightlight Settings
constexpr uint16_t NIGHTLIGHT_MIN_DURATION      = 1;     // Minimum (seconds)
constexpr uint16_t NIGHTLIGHT_MAX_DURATION      = 3600;  // Maximum (1 hour)
constexpr uint16_t NIGHTLIGHT_DEFAULT_DURATION  = 900;   // Default (15 min)
constexpr uint8_t  NIGHTLIGHT_DEFAULT_TARGET    = 0;     // Target brightness (off)

// ═══════════════════════════════════════════════════════════════════════════
// FIRMWARE METADATA
// ═══════════════════════════════════════════════════════════════════════════

#define FIRMWARE_NAME    "LUME"
#define FIRMWARE_VERSION "1.0.0"

#ifndef FIRMWARE_BUILD_HASH
#define FIRMWARE_BUILD_HASH "dev"
#endif

#ifndef FIRMWARE_BUILD_TIMESTAMP
#define FIRMWARE_BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

#endif // CONSTANTS_H
