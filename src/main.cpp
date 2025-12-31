/**
 * AI LED Strip Controller for LILYGO T-Display S3
 * 
 * Features:
 * - WiFi-enabled web server with modern UI
 * - WS2812B LED strip control via FastLED
 * - AI-powered effect generation via OpenRouter API
 * - Multiple built-in effects and color palettes
 * - Persistent configuration storage
 * 
 * Hardware:
 * - LILYGO T-Display S3
 * - WS2812B LED strip (default 160 LEDs on GPIO 21)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

#include "main.h"
#include "storage.h"
#include "constants.h"
#include "logging.h"
#include <esp_task_wdt.h>

// v2 architecture
#include "core/controller.h"
#include "effects/effects.h"
#include "protocols/sacn.h"
#include "protocols/mqtt.h"

// API handlers (modular route implementations)
#include "api/nightlight.h"  // Nightlight fade-to-sleep handlers
#include "api/pixels.h"       // Direct pixel control
#include "api/config.h"       // System configuration management
#include "api/status.h"       // Root & system status endpoints
#include "api/prompt.h"       // AI prompt processing

// Network setup
#include "network/server.h"   // Web server route registration
#include "network/ota.h"      // OTA updates and mDNS
#include "network/wifi.h"     // WiFi AP+STA setup

// Optional development secrets (create src/secrets.h from secrets.h.example)
#ifdef __has_include
#  if __has_include("secrets.h")
#    include "secrets.h"
#    define HAS_SECRETS_H
#  endif
#endif

// Access Point settings for initial setup
#define AP_SSID "LUME-Setup"
#define AP_PASSWORD "ledcontrol"

// Web server
AsyncWebServer server(80);

// Global configuration
Config config;

// WiFi state
bool wifiConnected = false;
unsigned long lastWifiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 30000;

// Web UI filesystem state
bool webUiAvailable = false;

// Auth helper - returns true if request is authorized
bool checkAuth(AsyncWebServerRequest* request) {
    // If no auth token configured, allow all requests
    if (config.authToken.length() == 0) {
        return true;
    }
    
    // Check Authorization header
    if (request->hasHeader("Authorization")) {
        String authHeader = request->header("Authorization");
        // Support both "Bearer <token>" and plain "<token>"
        if (authHeader.startsWith("Bearer ")) {
            authHeader = authHeader.substring(7);
        }
        if (authHeader == config.authToken) {
            return true;
        }
    }
    
    // Check X-API-Key header (alternative)
    if (request->hasHeader("X-API-Key")) {
        if (request->header("X-API-Key") == config.authToken) {
            return true;
        }
    }
    
    // Check query parameter (for easy browser testing)
    if (request->hasParam("token")) {
        if (request->getParam("token")->value() == config.authToken) {
            return true;
        }
    }
    
    return false;
}

// Send 401 Unauthorized response
void sendUnauthorized(AsyncWebServerRequest* request) {
    request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
}

// Helper function: Validate RGB color array
bool validateRgbArray(JsonArrayConst arr) {
    return arr.size() >= 3 && 
           arr[0].is<int>() && 
           arr[1].is<int>() && 
           arr[2].is<int>();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    LOG_INFO(LogTag::MAIN, "=== AI LED Strip Controller v%s ===", FIRMWARE_VERSION);
    LOG_INFO(LogTag::MAIN, "Initializing...");
    
    // Initialize storage
    storage.begin();

    // Mount LittleFS for serving the web UI
    if (LittleFS.begin(true)) {
        webUiAvailable = true;
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        LOG_INFO(LogTag::WEB, "LittleFS mounted (%u KB free)", static_cast<unsigned int>((total - used) / 1024));
    } else {
        LOG_ERROR(LogTag::WEB, "Failed to mount LittleFS; web UI unavailable");
    }
    
    // Load configuration
    if (!storage.loadConfig(config)) {
        LOG_WARN(LogTag::STORAGE, "No config found, using defaults");
    } else {
        LOG_INFO(LogTag::STORAGE, "Configuration loaded");
        LOG_DEBUG(LogTag::STORAGE, "WiFi SSID: %s", config.wifiSSID.c_str());
        LOG_DEBUG(LogTag::STORAGE, "LED Count: %d", config.ledCount);
    }
    
    // Override with development secrets if available
#ifdef HAS_SECRETS_H
    LOG_DEBUG(LogTag::MAIN, "Using development secrets");
    String devSsid = DEV_WIFI_SSID;
    String devPass = DEV_WIFI_PASSWORD;
    String devKey = DEV_AI_API_KEY;
    
    if (devSsid.length() > 0) config.wifiSSID = devSsid;
    if (devPass.length() > 0) config.wifiPassword = devPass;
    if (devKey.length() > 0) config.aiApiKey = devKey;
    config.aiModel = DEV_AI_MODEL;
    // Note: LED pin is configured in constants.h (LED_DATA_PIN)
    config.ledCount = DEV_LED_COUNT;
    config.defaultBrightness = DEV_DEFAULT_BRIGHTNESS;
#endif
    
    // Initialize v2 LED controller
    LOG_INFO(LogTag::LED, "Initializing LED controller...");
    lume::controller.begin(config.ledCount);
    lume::controller.setBrightness(config.defaultBrightness);
    
    // Register protocols with controller
    lume::controller.registerProtocol(&lume::sacnProtocol);
    
    // Initialize MQTT if configured
    if (config.mqttEnabled && config.mqttBroker.length() > 0) {
        lume::MqttConfig mqttConfig;
        mqttConfig.enabled = config.mqttEnabled;
        mqttConfig.broker = config.mqttBroker;
        mqttConfig.port = config.mqttPort;
        mqttConfig.username = config.mqttUsername;
        mqttConfig.password = config.mqttPassword;
        mqttConfig.topicPrefix = config.mqttTopicPrefix;
        lume::mqtt.begin(mqttConfig, &lume::controller);
    }
    
    // Create full-strip segment and set default effect
    lume::Segment* mainSegment = lume::controller.createFullStrip();
    if (mainSegment) {
        mainSegment->setEffect("rainbow");  // Default effect
        LOG_INFO(LogTag::LED, "Created main segment (0-%d) with rainbow effect", config.ledCount - 1);
    }
    
    // Setup WiFi
    setupWiFi();
    
    // Setup OTA updates
    setupOTA();
    
    // Setup web server
    setupServer();
    
    LOG_INFO(LogTag::MAIN, "Setup complete!");
    logMemoryStats(LogTag::MAIN, "at startup");
    
    // Initialize watchdog timer (older ESP-IDF API)
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);  // timeout in seconds, panic on timeout
    esp_task_wdt_add(NULL);  // Add current task (loop)
    LOG_INFO(LogTag::MAIN, "Watchdog initialized (%ds timeout)", WATCHDOG_TIMEOUT_SEC);
}

void loop() {
    // Handle OTA updates
    ArduinoOTA.handle();
    
    // Update controller (handles effects and registered protocols internally)
    // Protocol handling is now integrated into the controller's update cycle
    lume::controller.update();
    
    // MQTT update (handles reconnection and message processing)
    lume::mqtt.update();

    // Web server maintenance (WebSocket broadcasts/cleanup)
    loopServer();
    
    // WiFi maintenance (reconnection, status monitoring)
    handleWifiMaintenance();
    
    // Reset watchdog - proves loop is still running
    esp_task_wdt_reset();
    
    // Small delay to prevent watchdog issues
    yield();
}

