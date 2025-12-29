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

#include "storage.h"
#include "anthropic_client.h"
#include "web_ui.h"
#include "sacn_receiver.h"
#include "constants.h"
#include "logging.h"
#include <esp_task_wdt.h>

// v2 architecture
#include "core/controller.h"
#include "effects/effects.h"

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

// Forward declarations
void setupWiFi();
void setupOTA();
void setupServer();
void handleRoot(AsyncWebServerRequest* request);
void handleApiStatus(AsyncWebServerRequest* request);
void handleApiConfig(AsyncWebServerRequest* request);
void handleApiConfigPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiLed(AsyncWebServerRequest* request);
void handleApiLedPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiPrompt(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiPromptStatus(AsyncWebServerRequest* request);
void handleApiPromptApply(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiPixels(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiScenesGet(AsyncWebServerRequest* request);
void handleApiSceneGet(AsyncWebServerRequest* request);
void handleApiScenePost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiSceneDelete(AsyncWebServerRequest* request);
void handleApiSceneApply(AsyncWebServerRequest* request);
void handleApiNightlightGet(AsyncWebServerRequest* request);
void handleApiNightlightPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// Request body buffers (for async body handling)
String configBodyBuffer;
String ledBodyBuffer;
String promptBodyBuffer;
String applyBodyBuffer;
String nightlightBodyBuffer;
String pixelsBodyBuffer;
String sceneBodyBuffer;

// Helper function: Validate RGB color array
bool validateRgbArray(JsonArrayConst arr) {
    return arr.size() >= 3 && 
           arr[0].is<int>() && 
           arr[1].is<int>() && 
           arr[2].is<int>();
}

// ===========================================================================
// V2 Controller Adapter - Bridge old API format to new segment-based controller
// ===========================================================================

// Get primary segment (segment 0) - creates if needed
lume::Segment* getMainSegment() {
    lume::Segment* seg = lume::controller.getSegment(0);
    if (!seg) {
        seg = lume::controller.createFullStrip();
    }
    return seg;
}

// Map old effect names to new effect IDs
const char* mapOldEffectToNew(const char* oldEffect) {
    if (!oldEffect) return "rainbow";
    
    // Case-insensitive comparison helper
    String eff = oldEffect;
    eff.toLowerCase();
    
    if (eff == "solid") return "solid";
    if (eff == "rainbow") return "rainbow";
    if (eff == "confetti") return "confetti";
    if (eff == "fire" || eff == "fire2012") return "fire";
    if (eff == "gradient") return "gradient";
    if (eff == "pulse") return "pulse";
    // Effects that aren't ported yet default to rainbow
    return "rainbow";
}

// Map old palette names to new PalettePreset
lume::PalettePreset mapOldPaletteToNew(const char* oldPalette) {
    if (!oldPalette) return lume::PalettePreset::Rainbow;
    
    String pal = oldPalette;
    pal.toLowerCase();
    
    if (pal == "rainbow") return lume::PalettePreset::Rainbow;
    if (pal == "lava") return lume::PalettePreset::Lava;
    if (pal == "ocean") return lume::PalettePreset::Ocean;
    if (pal == "party") return lume::PalettePreset::Party;
    if (pal == "forest") return lume::PalettePreset::Forest;
    if (pal == "cloud") return lume::PalettePreset::Cloud;
    if (pal == "heat") return lume::PalettePreset::Heat;
    
    return lume::PalettePreset::Rainbow;
}

// Serialize controller state to old API format (for backward compat with web UI)
void controllerStateToJson(JsonDocument& doc) {
    doc["power"] = lume::controller.getPower();
    doc["brightness"] = lume::controller.getBrightness();
    
    lume::Segment* seg = getMainSegment();
    if (seg) {
        // Effect name
        doc["effect"] = seg->getEffectId();
        
        // Speed/intensity as "speed" for old API
        doc["speed"] = seg->getSpeed();
        
        // Colors
        CRGB primary = seg->getPrimaryColor();
        CRGB secondary = seg->getSecondaryColor();
        
        JsonArray primaryArr = doc["primaryColor"].to<JsonArray>();
        primaryArr.add(primary.r);
        primaryArr.add(primary.g);
        primaryArr.add(primary.b);
        
        JsonArray secondaryArr = doc["secondaryColor"].to<JsonArray>();
        secondaryArr.add(secondary.r);
        secondaryArr.add(secondary.g);
        secondaryArr.add(secondary.b);
        
        // Palette - convert enum to string
        doc["palette"] = "rainbow";  // TODO: Add palette name tracking
    }
    
    // Nightlight - not implemented in v2 yet
    JsonObject nightlight = doc["nightlight"].to<JsonObject>();
    nightlight["active"] = false;
}

// Apply old API format to new controller
void controllerStateFromJson(const JsonDocument& doc) {
    lume::Segment* seg = getMainSegment();
    if (!seg) return;
    
    // Power
    if (doc["power"].is<bool>()) {
        lume::controller.setPower(doc["power"].as<bool>());
    }
    
    // Brightness
    if (doc["brightness"].is<int>()) {
        lume::controller.setBrightness(constrain(doc["brightness"].as<int>(), 0, 255));
    }
    
    // Effect
    if (doc["effect"].is<const char*>()) {
        const char* effectId = mapOldEffectToNew(doc["effect"].as<const char*>());
        seg->setEffect(effectId);
    }
    
    // Speed (maps to segment speed)
    if (doc["speed"].is<int>()) {
        seg->setSpeed(constrain(doc["speed"].as<int>(), 1, 255));
    }
    
    // Palette
    if (doc["palette"].is<const char*>()) {
        lume::PalettePreset preset = mapOldPaletteToNew(doc["palette"].as<const char*>());
        seg->setPalette(preset);
    }
    
    // Primary color
    JsonVariantConst primaryVar = doc["primaryColor"];
    if (primaryVar.is<JsonArrayConst>()) {
        JsonArrayConst arr = primaryVar.as<JsonArrayConst>();
        if (arr.size() >= 3) {
            seg->setPrimaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
        }
    } else if (primaryVar.is<const char*>()) {
        String hex = primaryVar.as<String>();
        if (hex.startsWith("#") && hex.length() == 7) {
            long color = strtol(hex.substring(1).c_str(), NULL, 16);
            seg->setPrimaryColor(CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF));
        }
    }
    
    // Secondary color  
    JsonVariantConst secondaryVar = doc["secondaryColor"];
    if (secondaryVar.is<JsonArrayConst>()) {
        JsonArrayConst arr = secondaryVar.as<JsonArrayConst>();
        if (arr.size() >= 3) {
            seg->setSecondaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
        }
    } else if (secondaryVar.is<const char*>()) {
        String hex = secondaryVar.as<String>();
        if (hex.startsWith("#") && hex.length() == 7) {
            long color = strtol(hex.substring(1).c_str(), NULL, 16);
            seg->setSecondaryColor(CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF));
        }
    }
}

// Apply effect spec from AI/prompts - supports both "effect" and "pixels" modes
bool applyEffectSpec(const JsonDocument& spec, String& errorMsg) {
    // Check for mode field
    String mode = "effect";  // default
    if (spec["mode"].is<const char*>()) {
        mode = spec["mode"].as<String>();
        mode.toLowerCase();
    }
    
    CRGB* leds = lume::controller.getLeds();
    uint16_t ledCount = lume::controller.getLedCount();
    
    // Handle pixels mode - direct LED control (bypass effects)
    if (mode == "pixels") {
        if (!spec["pixels"].is<JsonObjectConst>()) {
            errorMsg = "Mode 'pixels' requires 'pixels' object";
            return false;
        }
        JsonObjectConst pixels = spec["pixels"].as<JsonObjectConst>();
        
        // Fill all with single color
        if (pixels["fill"].is<JsonArrayConst>()) {
            JsonArrayConst fill = pixels["fill"].as<JsonArrayConst>();
            if (fill.size() >= 3) {
                CRGB color(fill[0].as<uint8_t>(), fill[1].as<uint8_t>(), fill[2].as<uint8_t>());
                fill_solid(leds, ledCount, color);
                FastLED.show();
                lume::controller.setPower(true);
                errorMsg = "";
                return true;
            }
        }
        
        // Gradient
        if (pixels["gradient"].is<JsonObjectConst>()) {
            JsonObjectConst grad = pixels["gradient"].as<JsonObjectConst>();
            JsonArrayConst from = grad["from"].as<JsonArrayConst>();
            JsonArrayConst to = grad["to"].as<JsonArrayConst>();
            
            if (from.size() >= 3 && to.size() >= 3) {
                CRGB startColor(from[0].as<uint8_t>(), from[1].as<uint8_t>(), from[2].as<uint8_t>());
                CRGB endColor(to[0].as<uint8_t>(), to[1].as<uint8_t>(), to[2].as<uint8_t>());
                fill_gradient_RGB(leds, 0, startColor, ledCount - 1, endColor);
                FastLED.show();
                lume::controller.setPower(true);
                errorMsg = "";
                return true;
            }
        }
        
        // Individual pixels array
        if (pixels["pixels"].is<JsonArrayConst>()) {
            JsonArrayConst arr = pixels["pixels"].as<JsonArrayConst>();
            uint16_t count = min((uint16_t)arr.size(), ledCount);
            
            for (uint16_t i = 0; i < count; i++) {
                JsonArrayConst pixel = arr[i].as<JsonArrayConst>();
                if (pixel.size() >= 3) {
                    leds[i].r = pixel[0].as<uint8_t>();
                    leds[i].g = pixel[1].as<uint8_t>();
                    leds[i].b = pixel[2].as<uint8_t>();
                }
            }
            FastLED.show();
            lume::controller.setPower(true);
            errorMsg = "";
            return true;
        }
        
        errorMsg = "No valid pixel data in 'pixels' object";
        return false;
    }
    
    // Effect mode - validate required fields
    if (!spec["effect"].is<const char*>()) {
        errorMsg = "Missing 'effect' field";
        return false;
    }
    
    // Apply the spec using our adapter
    controllerStateFromJson(spec);
    lume::controller.setPower(true);
    
    errorMsg = "";
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    LOG_INFO(LogTag::MAIN, "=== AI LED Strip Controller v%s ===", FIRMWARE_VERSION);
    LOG_INFO(LogTag::MAIN, "Initializing...");
    
    // Initialize storage
    storage.begin();
    
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
    String devKey = DEV_API_KEY;
    
    if (devSsid.length() > 0) config.wifiSSID = devSsid;
    if (devPass.length() > 0) config.wifiPassword = devPass;
    if (devKey.length() > 0) config.apiKey = devKey;
    config.openRouterModel = DEV_OPENROUTER_MODEL;
    // Note: LED pin is hardcoded to GPIO 21 in led_controller.cpp
    config.ledCount = DEV_LED_COUNT;
    config.defaultBrightness = DEV_DEFAULT_BRIGHTNESS;
#endif
    
    // Initialize v2 LED controller
    LOG_INFO(LogTag::LED, "Initializing LED controller...");
    lume::controller.begin(config.ledCount);
    lume::controller.setBrightness(config.defaultBrightness);
    
    // Create full-strip segment and set default effect
    lume::Segment* mainSegment = lume::controller.createFullStrip();
    if (mainSegment) {
        mainSegment->setEffect("rainbow");  // Default effect
        LOG_INFO(LogTag::LED, "Created main segment (0-%d) with rainbow effect", config.ledCount - 1);
    }
    
    // NOTE: Old ledController removed - using new v2 controller exclusively
    
    // Initialize OpenRouter client
    openRouterClient.begin();
    
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

// Helper function for WiFi reconnection and status monitoring
void handleWifiMaintenance() {
    // WiFi reconnection logic
    if (!wifiConnected && config.wifiSSID.length() > 0) {
        if (millis() - lastWifiAttempt > WIFI_RETRY_INTERVAL) {
            lastWifiAttempt = millis();
            LOG_INFO(LogTag::WIFI, "Attempting WiFi reconnection...");
            WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());
        }
    }
    
    // Check WiFi status change
    static bool lastWifiState = false;
    bool currentWifiState = (WiFi.status() == WL_CONNECTED);
    if (currentWifiState != lastWifiState) {
        lastWifiState = currentWifiState;
        wifiConnected = currentWifiState;
        if (currentWifiState) {
            LOG_INFO(LogTag::WIFI, "Connected! IP: %s", WiFi.localIP().toString().c_str());
            // Setup OTA when WiFi connects
            setupOTA();
            // Start sACN receiver if enabled
            if (config.sacnEnabled) {
                sacnReceiver.setUnicastMode(config.sacnUnicast);
                sacnReceiver.begin(config.sacnUniverse, config.sacnUniverseCount);
            }
        } else {
            LOG_WARN(LogTag::WIFI, "WiFi disconnected");
            sacnReceiver.stop();
        }
    }
}

void loop() {
    // Handle OTA updates
    ArduinoOTA.handle();
    
    // Check for sACN data and apply if enabled
    static bool sacnActive = false;
    bool skipNormalEffects = false;
    
    if (config.sacnEnabled && sacnReceiver.isEnabled()) {
        if (sacnReceiver.update()) {
            // New sACN data received - apply to LEDs (using v2 controller's LED array)
            sacnReceiver.applyToLeds(lume::controller.getLeds(), lume::controller.getLedCount(), config.sacnStartChannel);
            FastLED.show();
            sacnActive = true;
        } else if (sacnActive && sacnReceiver.hasTimedOut(5000)) {
            // sACN timed out, return to normal effects
            LOG_INFO(LogTag::SACN, "Timeout - returning to normal effects");
            sacnActive = false;
        }
        
        // If sACN is active, skip normal effect updates
        skipNormalEffects = sacnActive;
    }
    
    // Non-blocking LED update (normal effects) - skip if sACN is active
    if (!skipNormalEffects) {
        lume::controller.update();
    }
    
    // WiFi maintenance (reconnection, status monitoring)
    handleWifiMaintenance();
    
    // Reset watchdog - proves loop is still running
    esp_task_wdt_reset();
    
    // Small delay to prevent watchdog issues
    yield();
}

void setupWiFi() {
    // Always start AP mode for initial access
    WiFi.mode(WIFI_AP_STA);
    
    // Start Access Point
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    LOG_INFO(LogTag::WIFI, "AP started: %s", AP_SSID);
    LOG_DEBUG(LogTag::WIFI, "AP IP: %s", WiFi.softAPIP().toString().c_str());
    
    // Try to connect to configured WiFi
    if (config.wifiSSID.length() > 0) {
        LOG_INFO(LogTag::WIFI, "Connecting to WiFi: %s", config.wifiSSID.c_str());
        WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());
        
        // Wait for connection (with timeout)
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");  // Keep dots for visual feedback
            attempts++;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            LOG_INFO(LogTag::WIFI, "Connected! IP: %s", WiFi.localIP().toString().c_str());
        } else {
            LOG_WARN(LogTag::WIFI, "Connection failed, AP mode active");
        }
    } else {
        LOG_INFO(LogTag::WIFI, "No WiFi configured, AP mode only");
    }
    
    lastWifiAttempt = millis();
}

void setupOTA() {
    // Only enable OTA if WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        LOG_DEBUG(LogTag::OTA, "Waiting for WiFi connection");
        return;
    }
    
    // Start mDNS service for OTA discovery and easy access
    if (!MDNS.begin(MDNS_HOSTNAME)) {
        LOG_ERROR(LogTag::OTA, "Error starting mDNS");
        return;
    }
    // Add HTTP service for service discovery
    MDNS.addService("http", "tcp", 80);
    LOG_INFO(LogTag::OTA, "mDNS started: %s.local", MDNS_HOSTNAME);
    
    // Set OTA hostname
    ArduinoOTA.setHostname(MDNS_HOSTNAME);
    
    // Set OTA port (default is 3232)
    ArduinoOTA.setPort(3232);
    
    // Set OTA password (use auth token if set, otherwise fall back to AP password)
    if (config.authToken.length() > 0) {
        ArduinoOTA.setPassword(config.authToken.c_str());
    } else {
        ArduinoOTA.setPassword(AP_PASSWORD);
    }
    
    // OTA callbacks for status updates
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_SPIFFS
            type = "filesystem";
        }
        LOG_INFO(LogTag::OTA, "Starting update (%s)", type.c_str());
        // Disable watchdog during OTA (can take >30s)
        esp_task_wdt_delete(NULL);
        // Turn off LEDs during update
        lume::controller.setPower(false);
    });
    
    ArduinoOTA.onEnd([]() {
        LOG_INFO(LogTag::OTA, "Update complete!");
        // Re-enable watchdog after OTA
        esp_task_wdt_add(NULL);
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t lastPercent = 255;
        uint8_t percent = progress / (total / 100);
        if (percent != lastPercent) {
            lastPercent = percent;
            Serial.printf("OTA Progress: %u%%\r", percent);  // Keep inline progress
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        const char* errStr = "Unknown";
        if (error == OTA_AUTH_ERROR) errStr = "Auth Failed";
        else if (error == OTA_BEGIN_ERROR) errStr = "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) errStr = "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) errStr = "Receive Failed";
        else if (error == OTA_END_ERROR) errStr = "End Failed";
        LOG_ERROR(LogTag::OTA, "Error[%u]: %s", error, errStr);
        // Re-enable watchdog after OTA error
        esp_task_wdt_add(NULL);
    });
    
    ArduinoOTA.begin();
    
    // Add mDNS service for OTA
    MDNS.addService("arduino", "tcp", 3232);
    
    LOG_INFO(LogTag::OTA, "Ready (Hostname: lume.local, Port: 3232)");
}

void setupServer() {
    // Serve main page
    server.on("/", HTTP_GET, handleRoot);
    
    // Health check endpoint - lightweight for monitoring
    server.on("/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        // Core health indicators
        doc["status"] = "healthy";
        doc["uptime"] = millis() / 1000;
        doc["version"] = FIRMWARE_VERSION;
        
        // Memory health
        JsonObject memory = doc["memory"].to<JsonObject>();
        memory["heap_free"] = ESP.getFreeHeap();
        memory["heap_min"] = ESP.getMinFreeHeap();
        memory["heap_max_block"] = ESP.getMaxAllocHeap();
        memory["psram_free"] = ESP.getFreePsram();
        
        // Calculate heap fragmentation percentage
        uint32_t heapFree = ESP.getFreeHeap();
        uint32_t maxBlock = ESP.getMaxAllocHeap();
        if (heapFree > 0) {
            memory["fragmentation"] = 100 - (maxBlock * 100 / heapFree);
        }
        
        // Network health
        JsonObject network = doc["network"].to<JsonObject>();
        network["wifi_connected"] = wifiConnected;
        network["wifi_rssi"] = wifiConnected ? WiFi.RSSI() : 0;
        network["ip"] = wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        network["ap_clients"] = WiFi.softAPgetStationNum();
        
        // Component health
        JsonObject components = doc["components"].to<JsonObject>();
        components["led_controller"] = lume::controller.getLedCount() > 0;
        components["storage"] = true;  // Would fail at boot if broken
        components["sacn_enabled"] = config.sacnEnabled;
        components["sacn_receiving"] = sacnReceiver.isReceiving();
        
        // AI client status
        PromptJobResult& aiResult = openRouterClient.getJobResult();
        components["ai_last_state"] = 
            aiResult.state == PromptJobState::IDLE ? "idle" :
            aiResult.state == PromptJobState::QUEUED ? "queued" :
            aiResult.state == PromptJobState::RUNNING ? "running" :
            aiResult.state == PromptJobState::DONE ? "done" : "error";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // API endpoints
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/config", HTTP_GET, handleApiConfig);
    server.on("/api/led", HTTP_GET, handleApiLed);
    server.on("/api/prompt/status", HTTP_GET, handleApiPromptStatus);
    
    // POST handlers with body
    server.on("/api/config", HTTP_POST, 
        [](AsyncWebServerRequest* request) {}, 
        NULL,
        handleApiConfigPost
    );
    
    server.on("/api/led", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiLedPost
    );
    
    // Register more specific route first!
    server.on("/api/prompt/apply", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiPromptApply
    );
    
    server.on("/api/prompt", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiPrompt
    );
    
    // Direct pixel control endpoint
    server.on("/api/pixels", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiPixels
    );
    
    // Scene endpoints
    server.on("/api/scenes", HTTP_GET, handleApiScenesGet);
    
    // Scene CRUD - use regex-like handling
    server.on("^\\/api\\/scenes\\/([0-9]+)$", HTTP_GET, handleApiSceneGet);
    server.on("^\\/api\\/scenes\\/([0-9]+)$", HTTP_DELETE, handleApiSceneDelete);
    server.on("^\\/api\\/scenes\\/([0-9]+)\\/apply$", HTTP_POST, handleApiSceneApply);
    
    server.on("/api/scenes", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiScenePost
    );
    
    // Segment management endpoints (v2 architecture)
    server.on("/api/segments", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        // Global state
        doc["power"] = lume::controller.getPower();
        doc["brightness"] = lume::controller.getBrightness();
        doc["ledCount"] = lume::controller.getLedCount();
        
        // List all segments
        JsonArray segArr = doc["segments"].to<JsonArray>();
        uint8_t segCount = lume::controller.getSegmentCount();
        
        for (uint8_t i = 0; i < segCount; i++) {
            lume::Segment* seg = lume::controller.getSegment(i);
            if (!seg) continue;
            
            JsonObject segObj = segArr.add<JsonObject>();
            segObj["id"] = seg->getId();
            segObj["start"] = seg->getStart();
            segObj["length"] = seg->getLength();
            segObj["reverse"] = seg->isReversed();
            segObj["brightness"] = seg->getBrightness();
            segObj["speed"] = seg->getSpeed();
            segObj["intensity"] = seg->getParams().intensity;
            
            // Effect info
            JsonObject effectObj = segObj["effect"].to<JsonObject>();
            effectObj["id"] = seg->getEffectId();
            effectObj["name"] = seg->getEffectName();
            if (seg->getEffect()) {
                effectObj["category"] = seg->getEffect()->categoryName();
            }
            
            // Colors
            CRGB primary = seg->getPrimaryColor();
            CRGB secondary = seg->getSecondaryColor();
            
            JsonArray colors = segObj["colors"].to<JsonArray>();
            char hex1[8], hex2[8];
            snprintf(hex1, sizeof(hex1), "#%02x%02x%02x", primary.r, primary.g, primary.b);
            snprintf(hex2, sizeof(hex2), "#%02x%02x%02x", secondary.r, secondary.g, secondary.b);
            colors.add(hex1);
            colors.add(hex2);
            
            // Capabilities
            const lume::SegmentCapabilities& caps = seg->getCapabilities();
            JsonObject capsObj = segObj["capabilities"].to<JsonObject>();
            capsObj["hasSpeed"] = caps.hasSpeed;
            capsObj["hasIntensity"] = caps.hasIntensity;
            capsObj["hasPalette"] = caps.hasPalette;
            capsObj["hasSecondaryColor"] = caps.hasSecondaryColor;
        }
        
        // Available effects
        JsonArray effectsArr = doc["effects"].to<JsonArray>();
        auto& registry = lume::effects();
        for (uint8_t i = 0; i < registry.getCount(); i++) {
            const lume::EffectInfo* info = registry.getByIndex(i);
            if (!info) continue;
            
            JsonObject effObj = effectsArr.add<JsonObject>();
            effObj["id"] = info->id;
            effObj["name"] = info->displayName;
            effObj["category"] = info->categoryName();  // Use helper method
            effObj["usesSpeed"] = info->usesSpeed;
            effObj["usesIntensity"] = info->usesIntensity;
            effObj["usesPalette"] = info->usesPalette;
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Nightlight endpoints
    server.on("/api/nightlight", HTTP_GET, handleApiNightlightGet);
    server.on("/api/nightlight", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiNightlightPost
    );
    server.on("/api/nightlight/stop", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        // TODO: Implement nightlight in v2 controller
        request->send(200, "application/json", "{\"success\":true}");
    });
    
    // Handle CORS preflight
    server.on("/api/*", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key");
        request->send(response);
    });
    
    // 404 handler
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    });
    
    // Start server
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    server.begin();
    LOG_INFO(LogTag::WEB, "Web server started on port 80");
}

void handleRoot(AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", INDEX_HTML);
}

void handleApiStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    doc["uptime"] = millis() / 1000;
    doc["wifi"] = wifiConnected ? "Connected" : "AP Mode";
    doc["ip"] = wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["heap"] = ESP.getFreeHeap();
    doc["ledCount"] = lume::controller.getLedCount();
    doc["power"] = lume::controller.getPower();
    
    // sACN status
    JsonObject sacn = doc["sacn"].to<JsonObject>();
    sacn["enabled"] = config.sacnEnabled;
    sacn["universe"] = config.sacnUniverse;
    sacn["universeCount"] = config.sacnUniverseCount;
    sacn["startChannel"] = config.sacnStartChannel;
    sacn["unicast"] = config.sacnUnicast;
    sacn["receiving"] = sacnReceiver.isReceiving();
    sacn["packets"] = sacnReceiver.getPacketCount();
    sacn["source"] = sacnReceiver.getActiveSourceName();
    sacn["priority"] = sacnReceiver.getActivePriority();
    if (sacnReceiver.isReceiving()) {
        sacn["lastPacketMs"] = millis() - sacnReceiver.getLastPacketTime();
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiConfig(AsyncWebServerRequest* request) {
    JsonDocument doc;
    storage.configToJson(config, doc, true);
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiConfigPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        configBodyBuffer = "";
        // Validate total size
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    configBodyBuffer += String((char*)data).substring(0, len);
    
    if (index + len >= total) {
        // Body complete, process
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, configBodyBuffer);
        
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        // Update config
        storage.configFromJson(config, doc);
        
        // Save to storage
        if (storage.saveConfig(config)) {
            // Apply changes that can be applied without restart
            lume::controller.setLedCount(config.ledCount);
            
            // Handle sACN enable/disable
            if (config.sacnEnabled && wifiConnected) {
                sacnReceiver.stop();
                sacnReceiver.setUnicastMode(config.sacnUnicast);
                sacnReceiver.begin(config.sacnUniverse, config.sacnUniverseCount);
            } else {
                sacnReceiver.stop();
            }
            
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save\"}");
        }
    }
}

void handleApiLed(AsyncWebServerRequest* request) {
    JsonDocument doc;
    controllerStateToJson(doc);  // Use v2 adapter
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiLedPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        ledBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    ledBodyBuffer += String((char*)data).substring(0, len);
    
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, ledBodyBuffer);
        
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        // Apply state using v2 adapter
        controllerStateFromJson(doc);
        
        // Save state to storage
        JsonDocument saveDoc;
        controllerStateToJson(saveDoc);
        storage.saveLedState(saveDoc);
        
        request->send(200, "application/json", "{\"success\":true}");
    }
}

void handleApiPrompt(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        promptBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    // Rate limiting for prompt endpoint
    static unsigned long lastPromptRequest = 0;
    if (index == 0 && millis() - lastPromptRequest < PROMPT_RATE_LIMIT_MS) {
        request->send(429, "application/json", "{\"error\":\"Rate limited. Please wait before submitting another prompt.\"}");
        return;
    }
    
    promptBodyBuffer += String((char*)data).substring(0, len);
    
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, promptBodyBuffer);
        
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        String prompt = doc["prompt"] | "";
        if (prompt.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"Missing prompt\"}");
            return;
        }
        
        // Check if job already running
        if (openRouterClient.isJobRunning()) {
            request->send(409, "application/json", "{\"error\":\"Job already running\"}");
            return;
        }
        
        // Get API key (from request or config)
        String apiKey = doc["apiKey"] | config.apiKey;
        if (apiKey.length() == 0 || apiKey.startsWith("****")) {
            apiKey = config.apiKey;
        }
        
        if (apiKey.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"API key not configured\"}");
            return;
        }
        
        // Build request
        PromptRequest req;
        req.prompt = prompt;
        req.apiKey = apiKey;
        req.model = doc["model"] | config.openRouterModel;
        
        // Include current LED state for context
        JsonDocument ledDoc;
        controllerStateToJson(ledDoc);  // v2 adapter
        serializeJson(ledDoc, req.currentLedStateJson);
        
        // Submit job
        if (openRouterClient.submitPrompt(req)) {
            lastPromptRequest = millis();  // Update rate limit timestamp
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Job started\"}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to start job\"}");
        }
    }
}

void handleApiPromptStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    PromptJobResult& result = openRouterClient.getJobResult();
    
    switch (result.state) {
        case PromptJobState::IDLE:    doc["state"] = "idle"; break;
        case PromptJobState::QUEUED:  doc["state"] = "queued"; break;
        case PromptJobState::RUNNING: doc["state"] = "running"; break;
        case PromptJobState::DONE:    doc["state"] = "done"; break;
        case PromptJobState::ERROR:   doc["state"] = "error"; break;
    }
    
    doc["message"] = result.message;
    
    // Debug info
    if (result.prompt.length() > 0) {
        doc["prompt"] = result.prompt;
    }
    if (result.rawResponse.length() > 0) {
        doc["rawResponse"] = result.rawResponse;
    }
    
    if (result.state == PromptJobState::DONE && result.effectSpec.length() > 0) {
        doc["lastSpec"] = result.effectSpec;
    }
    
    if (result.startTime > 0) {
        unsigned long elapsed = (result.endTime > 0 ? result.endTime : millis()) - result.startTime;
        doc["elapsed"] = elapsed;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiPromptApply(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        applyBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    // Properly append data with explicit length (safer than substring)
    for (size_t i = 0; i < len; i++) {
        applyBodyBuffer += (char)data[i];
    }
    
    if (index + len >= total) {
        LOG_DEBUG(LogTag::WEB, "Apply body received (%d bytes)", applyBodyBuffer.length());
        
        String specJson;
        
        // Check if spec provided in body
        if (applyBodyBuffer.length() > 2) {
            JsonDocument bodyDoc;
            DeserializationError err = deserializeJson(bodyDoc, applyBodyBuffer);
            
            if (err) {
                LOG_WARN(LogTag::WEB, "Failed to parse apply body: %s", err.c_str());
            }
            
            if (err == DeserializationError::Ok && bodyDoc["spec"].is<const char*>()) {
                specJson = bodyDoc["spec"].as<String>();
                LOG_DEBUG(LogTag::WEB, "Extracted spec from body (%d chars)", specJson.length());
            } else if (err == DeserializationError::Ok) {
                // Maybe spec is an object, not a string - try to serialize it
                if (bodyDoc["spec"].is<JsonObject>()) {
                    LOG_DEBUG(LogTag::WEB, "Spec is an object, serializing...");
                    serializeJson(bodyDoc["spec"], specJson);
                }
            }
        }
        
        // If no spec in body, use last generated
        if (specJson.length() == 0) {
            PromptJobResult& result = openRouterClient.getJobResult();
            if (result.state == PromptJobState::DONE && result.effectSpec.length() > 0) {
                specJson = result.effectSpec;
            }
        }
        
        if (specJson.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"No effect specification to apply\"}");
            return;
        }
        
        // Parse and apply
        JsonDocument specDoc;
        DeserializationError err = deserializeJson(specDoc, specJson);
        
        if (err) {
            LOG_WARN(LogTag::LED, "Failed to parse spec JSON: %s", err.c_str());
            request->send(400, "application/json", "{\"error\":\"Invalid effect specification\"}");
            return;
        }
        
        LOG_DEBUG(LogTag::LED, "Attempting to apply effect spec");
        if (LOG_LEVEL <= LogLevel::DEBUG) {
            serializeJsonPretty(specDoc, Serial);
            Serial.println();
        }
        
        String errorMsg;
        if (applyEffectSpec(specDoc, errorMsg)) {  // Use local adapter function
            // Save to storage
            PromptSpec spec;
            spec.jsonSpec = specJson;
            spec.timestamp = millis();
            spec.valid = true;
            storage.savePromptSpec(spec);
            
            // Also save LED state
            JsonDocument saveDoc;
            controllerStateToJson(saveDoc);
            storage.saveLedState(saveDoc);
            
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            LOG_WARN(LogTag::LED, "Failed to apply effect: %s", errorMsg.c_str());
            String error = "{\"error\":\"" + errorMsg + "\"}";
            request->send(400, "application/json", error);
        }
    }
}

// Direct pixel control handler
// Accepts: { "pixels": [[r,g,b], [r,g,b], ...], "brightness": 255 }
// Or compact: { "rgb": [r,g,b,r,g,b,...], "brightness": 255 }
void handleApiPixels(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        pixelsBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    // Append data safely
    for (size_t i = 0; i < len; i++) {
        pixelsBodyBuffer += (char)data[i];
    }
    
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, pixelsBodyBuffer);
        
        if (err) {
            LOG_WARN(LogTag::WEB, "Pixels JSON parse error: %s", err.c_str());
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        CRGB* leds = lume::controller.getLeds();
        uint16_t ledCount = lume::controller.getLedCount();
        
        // Handle brightness if provided
        if (doc["brightness"].is<int>()) {
            lume::controller.setBrightness(constrain(doc["brightness"].as<int>(), 0, 255));
        }
        
        // Method 1: Array of [r,g,b] arrays
        if (doc["pixels"].is<JsonArray>()) {
            JsonArray pixels = doc["pixels"].as<JsonArray>();
            uint16_t count = min((uint16_t)pixels.size(), ledCount);
            
            for (uint16_t i = 0; i < count; i++) {
                JsonArray pixel = pixels[i].as<JsonArray>();
                if (pixel.size() >= 3) {
                    leds[i].r = pixel[0].as<uint8_t>();
                    leds[i].g = pixel[1].as<uint8_t>();
                    leds[i].b = pixel[2].as<uint8_t>();
                }
            }
            
            FastLED.show();
            
            JsonDocument response;
            response["success"] = true;
            response["pixelsSet"] = count;
            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            return;
        }
        
        // Method 2: Flat array [r,g,b,r,g,b,...]
        if (doc["rgb"].is<JsonArray>()) {
            JsonArray rgb = doc["rgb"].as<JsonArray>();
            uint16_t count = min((uint16_t)(rgb.size() / 3), ledCount);
            
            for (uint16_t i = 0; i < count; i++) {
                leds[i].r = rgb[i * 3].as<uint8_t>();
                leds[i].g = rgb[i * 3 + 1].as<uint8_t>();
                leds[i].b = rgb[i * 3 + 2].as<uint8_t>();
            }
            
            FastLED.show();
            
            JsonDocument response;
            response["success"] = true;
            response["pixelsSet"] = count;
            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            return;
        }
        
        // Method 3: Fill all with single color
        if (doc["fill"].is<JsonArray>()) {
            JsonArray fill = doc["fill"].as<JsonArray>();
            if (!validateRgbArray(fill)) {
                request->send(400, "application/json", "{\"error\":\"Fill requires array of [r,g,b] with 3 integer values (0-255)\"}");
                return;
            }
            CRGB color(fill[0].as<uint8_t>(), fill[1].as<uint8_t>(), fill[2].as<uint8_t>());
            fill_solid(leds, ledCount, color);
            FastLED.show();
            
            request->send(200, "application/json", "{\"success\":true,\"filled\":true}");
            return;
        }
        
        // Method 4: Gradient between two colors
        if (doc["gradient"].is<JsonObject>()) {
            JsonObject grad = doc["gradient"].as<JsonObject>();
            JsonArray from = grad["from"].as<JsonArray>();
            JsonArray to = grad["to"].as<JsonArray>();
            
            if (!validateRgbArray(from) || !validateRgbArray(to)) {
                request->send(400, "application/json", "{\"error\":\"Gradient requires 'from' and 'to' with [r,g,b] arrays\"}");
                return;
            }
            
            CRGB startColor(from[0].as<uint8_t>(), from[1].as<uint8_t>(), from[2].as<uint8_t>());
            CRGB endColor(to[0].as<uint8_t>(), to[1].as<uint8_t>(), to[2].as<uint8_t>());
            
            fill_gradient_RGB(leds, 0, startColor, ledCount - 1, endColor);
            FastLED.show();
            
            request->send(200, "application/json", "{\"success\":true,\"gradient\":true}");
            return;
        }
        
        request->send(400, "application/json", "{\"error\":\"No valid pixel data. Use 'pixels', 'rgb', 'fill', or 'gradient'\"}");
    }
}

// Scene API handlers
void handleApiScenesGet(AsyncWebServerRequest* request) {
    JsonDocument doc;
    storage.listScenes(doc);
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiSceneGet(AsyncWebServerRequest* request) {
    // Extract ID from URL path
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    int slotId = path.substring(lastSlash + 1).toInt();
    
    Scene scene;
    if (storage.loadScene(slotId, scene) && !scene.isEmpty()) {
        JsonDocument doc;
        doc["id"] = slotId;
        doc["name"] = scene.name;
        doc["spec"] = scene.jsonSpec;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    } else {
        request->send(404, "application/json", "{\"error\":\"Scene not found\"}");
    }
}

void handleApiScenePost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Accumulate body data
    if (index == 0) {
        sceneBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    for (size_t i = 0; i < len; i++) {
        sceneBodyBuffer += (char)data[i];
    }
    
    // Process when complete
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, sceneBodyBuffer);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        String name = doc["name"] | "";
        String spec = doc["spec"] | "";
        
        if (name.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"Scene name required\"}");
            return;
        }
        
        if (spec.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"Scene spec required\"}");
            return;
        }
        
        // Find empty slot or slot specified by 'id'
        int slot = -1;
        if (doc["id"].is<int>()) {
            slot = doc["id"].as<int>();
            if (slot < 0 || slot >= MAX_SCENES) {
                request->send(400, "application/json", "{\"error\":\"Invalid slot ID\"}");
                return;
            }
        } else {
            // Find first empty slot
            for (int i = 0; i < MAX_SCENES; i++) {
                Scene existing;
                if (!storage.loadScene(i, existing) || existing.isEmpty()) {
                    slot = i;
                    break;
                }
            }
            
            if (slot < 0) {
                request->send(400, "application/json", "{\"error\":\"No empty slots. Delete a scene first.\"}");
                return;
            }
        }
        
        Scene scene;
        scene.name = name;
        scene.jsonSpec = spec;
        
        if (storage.saveScene(slot, scene)) {
            JsonDocument response;
            response["success"] = true;
            response["id"] = slot;
            response["name"] = name;
            
            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save scene\"}");
        }
    }
}

void handleApiSceneDelete(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Extract ID from URL path
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    int slotId = path.substring(lastSlash + 1).toInt();
    
    if (slotId < 0 || slotId >= MAX_SCENES) {
        request->send(400, "application/json", "{\"error\":\"Invalid slot ID\"}");
        return;
    }
    
    if (storage.deleteScene(slotId)) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to delete scene\"}");
    }
}

void handleApiSceneApply(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Extract ID from URL path  /api/scenes/{id}/apply
    String path = request->url();
    
    // Remove "/apply" from end
    path = path.substring(0, path.lastIndexOf('/'));
    int lastSlash = path.lastIndexOf('/');
    int slotId = path.substring(lastSlash + 1).toInt();
    
    Scene scene;
    if (!storage.loadScene(slotId, scene) || scene.isEmpty()) {
        request->send(404, "application/json", "{\"error\":\"Scene not found\"}");
        return;
    }
    
    // Parse and apply the scene spec
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, scene.jsonSpec);
    
    if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid scene spec\"}");
        return;
    }
    
    String errorMsg;
    if (applyEffectSpec(doc, errorMsg)) {  // Use local adapter
        // Save the new LED state
        JsonDocument stateDoc;
        controllerStateToJson(stateDoc);
        storage.saveLedState(stateDoc);
        
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        JsonDocument response;
        response["error"] = errorMsg;
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(400, "application/json", responseStr);
    }
}

// ============================================================================
// Nightlight API handlers
// ============================================================================

void handleApiNightlightGet(AsyncWebServerRequest* request) {
    // TODO: Implement nightlight in v2 controller
    JsonDocument doc;
    doc["active"] = false;  // Nightlight not yet implemented in v2
    doc["progress"] = 0.0;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiNightlightPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Body size validation
    if (index == 0) {
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request too large\"}");
            return;
        }
        nightlightBodyBuffer = "";
        nightlightBodyBuffer.reserve(total);
    }
    
    // Accumulate body chunks
    nightlightBodyBuffer += String((char*)data, len);
    
    // Only process when complete
    if (index + len < total) {
        return;
    }
    
    LOG_DEBUG(LogTag::WEB, "Nightlight request: %s", nightlightBodyBuffer.c_str());
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, nightlightBodyBuffer);
    
    if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    
    // Get duration in seconds (default: 15 minutes = 900 seconds)
    uint16_t duration = doc["duration"] | NIGHTLIGHT_DEFAULT_DURATION;
    
    // Validate duration (between 1 second and max)
    if (duration < 1 || duration > NIGHTLIGHT_MAX_DURATION) {
        JsonDocument response;
        response["error"] = "Duration must be between 1 and " + String(NIGHTLIGHT_MAX_DURATION) + " seconds";
        String responseStr;
        serializeJson(response, responseStr);
        request->send(400, "application/json", responseStr);
        return;
    }
    
    // Get target brightness (default: 0 = fade to off)
    uint8_t targetBrightness = doc["targetBrightness"] | NIGHTLIGHT_DEFAULT_TARGET;
    
    // TODO: Implement nightlight in v2 controller
    // For now, just acknowledge the request
    JsonDocument response;
    response["success"] = true;
    response["duration"] = duration;
    response["targetBrightness"] = targetBrightness;
    response["startBrightness"] = lume::controller.getBrightness();
    response["note"] = "Nightlight not yet implemented in v2";
    
    String responseStr;
    serializeJson(response, responseStr);
    request->send(200, "application/json", responseStr);
}