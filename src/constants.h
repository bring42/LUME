#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cstdint>
#include <cstddef>

// ═══════════════════════════════════════════════════════════════════════════
// ⚠️  LED CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════
// Settings used to configure FastLED in controller.cpp
// Customize these for your specific hardware setup

// Pick a free GPIO for the LED data line (board-dependent):
//   ESP32-C3: safe 0-1, 4-7, 10, 18-21; avoid 2-3, 8-9 (strapping), 11-17 (flash)
//   ESP32-S3: most GPIOs are free; avoid 0/3/45/46 (strapping) and 26-32 (flash)
// Override per-env with -DLUME_LED_DATA_PIN=<gpio> in platformio.ini's build_flags
// (e.g. the C3 boards need a non-strapping pin; GPIO2 is fine on S3/T-Display S3).
#ifdef LUME_LED_DATA_PIN
#define LED_DATA_PIN                LUME_LED_DATA_PIN
#else
#define LED_DATA_PIN                2               // GPIO2 (T-Display S3: broken out, not a strapping pin)
#endif
#define LED_STRIP_TYPE              WS2812B         // Common addressable RGB LED
#define LED_COLOR_MODE              GRB             // Byte order (GRB for WS2812B)

// Strip Dimensions
// MAX_LED_COUNT sets compile-time buffer size (3 bytes per LED)
// Recommended: 1000 LEDs for smooth 60 FPS performance (FastLED RMT timing limit)
// Memory supports much more: ~10,000 LEDs (S3) or ~5,000 (C3)
// sACN protocol limit: 8 universes × 170 LEDs = 1,360 LEDs max
// See FastLED docs for parallel output to drive more strips simultaneously
constexpr uint16_t MAX_LED_COUNT            = 1000;

// Power Management
constexpr uint8_t  LED_VOLTAGE              = 5;     // LED strip voltage
constexpr uint16_t LED_MAX_MILLIAMPS        = 2000;  // Max current (adjust for PSU)

// Perceptual dimming. Global brightness is treated as a *perceptual* level and
// eased linearly; the output is gamma-encoded (output = (level/255)^GAMMA) just
// before FastLED so equal steps in the fade produce equal *perceived* steps.
// Human brightness perception is ~logarithmic, so without this a value-linear
// fade looks dead at the top and rushes at the bottom. ~2.2 = sRGB/eye-ish;
// 2.6–2.8 dims "deeper". Tune on hardware.
constexpr float    LED_GAMMA                = 2.2f;

// ═══════════════════════════════════════════════════════════════════════════
// NETWORK CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

// mDNS Hostname (Access via http://lume.local)
#define MDNS_HOSTNAME "lume"

// Timeouts (milliseconds)
constexpr uint32_t WIFI_RETRY_INTERVAL_MS   = 30000;
// Slower reconnect cadence while a client is parked on the SoftAP: keeps a scan's
// brief AP disruption rare during setup, while still letting the device recover on
// its own if a saved network returns while an idle client is holding the AP.
constexpr uint32_t WIFI_RETRY_INTERVAL_AP_BUSY_MS = 120000;

// ═══════════════════════════════════════════════════════════════════════════
// SYSTEM LIMITS & BUFFERS
// ═══════════════════════════════════════════════════════════════════════════

// Memory Buffers
constexpr size_t MAX_REQUEST_BODY_SIZE      = 16384;  // 16KB max POST body

// Task Configuration
constexpr size_t   ANTHROPIC_TASK_STACK_SIZE = 16384;
constexpr uint8_t  ANTHROPIC_TASK_PRIORITY   = 1;
constexpr uint8_t  ANTHROPIC_TASK_CORE       = 0;

// Firmware updater worker (pull-based OTA from GitHub Releases). A generous
// stack: the worker runs a TLS download + streaming SHA-256 + Update.write.
constexpr size_t   UPDATER_TASK_STACK_SIZE   = 16384;
constexpr uint8_t  UPDATER_TASK_PRIORITY     = 1;
constexpr uint8_t  UPDATER_TASK_CORE         = 0;
// Chunk size for streaming the downloaded image into Update.write + the hasher.
constexpr size_t   UPDATER_CHUNK_SIZE        = 2048;
// How long the updater waits on a stalled HTTP read before aborting the download.
constexpr uint32_t UPDATER_HTTP_TIMEOUT_MS   = 20000;

// System Timing
constexpr uint32_t WATCHDOG_TIMEOUT_SEC     = 30;     // Auto-reset timeout
constexpr uint32_t PROMPT_RATE_LIMIT_MS     = 3000;   // Min time between prompts

// ═══════════════════════════════════════════════════════════════════════════
// FEATURE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

// Nightlight Settings
constexpr uint16_t NIGHTLIGHT_MAX_DURATION      = 3600;  // Maximum (1 hour)
constexpr uint16_t NIGHTLIGHT_DEFAULT_DURATION  = 900;   // Default (15 min)
constexpr uint8_t  NIGHTLIGHT_DEFAULT_TARGET    = 0;     // Target brightness (off)

// ═══════════════════════════════════════════════════════════════════════════
// FIRMWARE METADATA
// ═══════════════════════════════════════════════════════════════════════════

#define FIRMWARE_NAME    "LUME"

// FIRMWARE_VERSION is normally injected at build time from the git tag by
// scripts/version.py (e.g. -DFIRMWARE_VERSION=\"1.2.0\"). This fallback is what
// a plain local `pio run` (no tag) compiles with.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

// Short git hash, injected by scripts/version.py; "dev" for un-tagged builds.
#ifndef FIRMWARE_BUILD_HASH
#define FIRMWARE_BUILD_HASH "dev"
#endif

#ifndef FIRMWARE_BUILD_TIMESTAMP
#define FIRMWARE_BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

// ═══════════════════════════════════════════════════════════════════════════
// FIRMWARE AUTO-UPDATE (pull-based OTA from GitHub Releases)
// ═══════════════════════════════════════════════════════════════════════════

// GitHub repo that publishes the release assets (manifest.json + per-board
// firmware/filesystem images). Override at build time if you fork.
#ifndef LUME_GH_OWNER
#define LUME_GH_OWNER "bring42"
#endif
#ifndef LUME_GH_REPO
#define LUME_GH_REPO "LUME"
#endif

// Which release asset set belongs to THIS build. Injected per-env by
// scripts/version.py (maps the PlatformIO env name to the release asset id).
// Must match the keys CI writes into manifest.json ("boards": { <id>: ... }).
#ifndef LUME_BOARD_ID
#define LUME_BOARD_ID "esp32-s3-devkitc"
#endif

#endif // CONSTANTS_H
