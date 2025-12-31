#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cstdint>
#include <cstddef>

// ============================================
// Project-wide Constants
// ============================================

// --- Timeouts (milliseconds) ---
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t SACN_DATA_TIMEOUT_MS = 5000;
constexpr uint32_t SACN_SOURCE_TIMEOUT_MS = 2500;
constexpr uint32_t HTTP_CLIENT_TIMEOUT_MS = 30000;

// --- Buffer Sizes ---
constexpr size_t MAX_REQUEST_BODY_SIZE = 16384;  // 16KB max for POST bodies
constexpr size_t MAX_JSON_STATE_SIZE = 4000;     // NVS limit for state storage
constexpr size_t SYSTEM_PROMPT_BUFFER_SIZE = 2048;

// --- Network Ports ---
constexpr uint16_t WEB_SERVER_PORT = 80;
constexpr uint16_t OTA_PORT = 3232;
constexpr uint16_t SACN_PORT = 5568;

// --- LED Configuration ---
#define LED_DATA_PIN 21                 // GPIO pin for LED data (change this for your wiring)

constexpr uint16_t MAX_LED_COUNT = 300;
constexpr uint16_t LEDS_PER_UNIVERSE = 170;

// --- Power Management ---
constexpr uint8_t LED_VOLTAGE = 5;              // LED strip voltage (5V)
constexpr uint16_t LED_MAX_MILLIAMPS = 2000;    // Max power draw (2A default, adjust for your PSU)

// --- Nightlight ---
constexpr uint16_t NIGHTLIGHT_MIN_DURATION = 1;       // Minimum duration (seconds)
constexpr uint16_t NIGHTLIGHT_MAX_DURATION = 3600;    // Maximum 1 hour (seconds)
constexpr uint16_t NIGHTLIGHT_DEFAULT_DURATION = 900; // Default 15 minutes (seconds)
constexpr uint8_t NIGHTLIGHT_DEFAULT_TARGET = 0;      // Default target brightness (0 = off)

// --- mDNS ---
#define MDNS_HOSTNAME "lume"  // Access via http://lume.local

// --- Task Configuration ---
constexpr size_t ANTHROPIC_TASK_STACK_SIZE = 16384;
constexpr uint8_t ANTHROPIC_TASK_PRIORITY = 1;
constexpr uint8_t ANTHROPIC_TASK_CORE = 0;

// --- Rate Limiting ---
constexpr uint32_t PROMPT_RATE_LIMIT_MS = 3000;  // Min time between prompt requests

// --- Watchdog ---
constexpr uint32_t WATCHDOG_TIMEOUT_SEC = 30;  // Watchdog timeout in seconds

// --- Version Info ---
#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_NAME "LUME"

#ifndef FIRMWARE_BUILD_HASH
#define FIRMWARE_BUILD_HASH "dev"
#endif

#ifndef FIRMWARE_BUILD_TIMESTAMP
#define FIRMWARE_BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

#endif // CONSTANTS_H
