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
// Strip type + byte order. Guarded so a board env (or local build) can retarget
// other hardware via build_flags without editing the defaults, e.g.:
//   build_flags = -DLED_STRIP_TYPE=SM16703 -DLED_COLOR_MODE=BRG
#ifndef LED_STRIP_TYPE
#define LED_STRIP_TYPE              WS2812B         // Common addressable RGB LED
#endif
#ifndef LED_COLOR_MODE
#define LED_COLOR_MODE              GRB             // Byte order (GRB for WS2812B)
#endif

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
//
// This is the compile-time DEFAULT (seed). Gamma is runtime-adjustable: the
// persisted config carries a `gamma` value, seeded from this at first boot, and
// the render loop applies the controller's runtime member (LumeController::
// setGamma) — so it can be tuned live from the web UI without a reflash.
constexpr float    LED_GAMMA                = 2.2f;
// Sane runtime range for live gamma tuning; clamped on both the API and the
// controller so an out-of-range value can never reach the output stage.
constexpr float    LED_GAMMA_MIN            = 1.0f;
constexpr float    LED_GAMMA_MAX            = 3.5f;

// Dim-to-warm. As the perceptual level drops, the output color temperature is
// blended from neutral white toward a warm target — mimicking how incandescent
// sources warm as they dim (premium-lighting-criteria.md). The blend follows a
// quadratic-in-(1-level) curve so the upper range stays neutral and only the
// low end glides warm (a smooth amber, not the accidental red PWM floor). Warmth
// is the *strength* [0..1] of the effect at the very bottom (0 = disabled) and
// is runtime-tunable via config, like gamma. The warm endpoint is Tungsten40W
// (2600 K, 255/197/143) — a cozy warm-white, not an orange.
constexpr float    LED_WARMTH_DEFAULT       = 0.6f;
constexpr float    LED_WARMTH_MIN           = 0.0f;
constexpr float    LED_WARMTH_MAX           = 1.0f;
constexpr uint8_t  LED_WARM_TARGET_R        = 255;   // Tungsten40W (2600 K)
constexpr uint8_t  LED_WARM_TARGET_G        = 197;
constexpr uint8_t  LED_WARM_TARGET_B        = 143;

// Low-end handling lives entirely in the 16-bit pipeline (render16.h +
// renderOutput16): error-diffusion dither carries dim content through the
// bottom codes, and dim-to-warm keeps the fade-out warm all the way down
// before cutting clean to black. The 8-bit era's LED_MIN_OUTPUT floor (clamp
// output to ≥4 so dim white couldn't collapse to red) and its companion
// correction ramp are GONE — the floor lifted green/blue back up in exactly
// the range dim-to-warm deliberately pulls them down, and the 16-bit dither
// made it unnecessary. See renderOutput16() step 4.
//
// WS2812B white-balance correction, applied post-gamma in 16-bit. Values
// mirror FastLED's TypicalLEDStrip (0xFFB0F0).
constexpr uint8_t  LED_CORRECTION_R          = 255;  // TypicalLEDStrip red
constexpr uint8_t  LED_CORRECTION_G          = 176;  // TypicalLEDStrip green
constexpr uint8_t  LED_CORRECTION_B          = 240;  // TypicalLEDStrip blue

// ═══════════════════════════════════════════════════════════════════════════
// NETWORK CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

// mDNS Hostname (Access via http://lume.local)
#define MDNS_HOSTNAME "lume"

// Timeouts (milliseconds)
// STA reconnect cadence. Reconnect is SKIPPED entirely while a client is parked on the
// SoftAP (see handleWifiMaintenance): the single-radio scan a reconnect triggers would
// channel-hop the AP and break the client's DHCP mid-provisioning.
constexpr uint32_t WIFI_RETRY_INTERVAL_MS   = 30000;

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
