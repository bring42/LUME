#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "constants.h"   // LED_GAMMA (seed for the persisted gamma default)

// RAII guard for Storage's NVS access. The single Preferences object is shared
// across tasks (saveLastEffect from the web/AI task vs saveLedState from the
// render loop) — concurrent begin()/end() corrupts the handle. The mutex is
// recursive so a locked method may safely call another locked method.
class StorageLock {
public:
    explicit StorageLock(SemaphoreHandle_t m) : m_(m) {
        if (m_) xSemaphoreTakeRecursive(m_, portMAX_DELAY);
    }
    ~StorageLock() { if (m_) xSemaphoreGiveRecursive(m_); }
    StorageLock(const StorageLock&) = delete;
    StorageLock& operator=(const StorageLock&) = delete;
private:
    SemaphoreHandle_t m_;
};

// Configuration structure
struct Config {
    String wifiSSID;
    String wifiPassword;
    String aiApiKey;              // Anthropic API key
    String aiModel;               // AI model selection
    String authToken;             // Optional API auth token (empty = no auth)
    uint16_t ledCount;
    uint8_t defaultBrightness;
    float gamma;                  // Perceptual-dimming exponent (see LED_GAMMA)
    // sACN (E1.31) settings
    bool sacnEnabled;
    uint16_t sacnUniverse;        // Starting universe
    uint8_t sacnUniverseCount;    // Number of universes (1-8, for >170 LEDs)
    uint16_t sacnStartChannel;
    bool sacnUnicast;             // true = unicast mode, false = multicast
    
    // MQTT settings
    bool mqttEnabled;
    String mqttBroker;            // Hostname or IP
    uint16_t mqttPort;
    String mqttUsername;
    String mqttPassword;
    String mqttTopicPrefix;       // Base topic (e.g., "lume")
    
    Config() : 
        wifiSSID(""),
        wifiPassword(""),
        aiApiKey(""),
        aiModel("claude-3-5-haiku-20241022"),
        authToken(""),
        ledCount(160),
        defaultBrightness(128),
        gamma(LED_GAMMA),
        sacnEnabled(false),
        sacnUniverse(1),
        sacnUniverseCount(1),
        sacnStartChannel(1),
        sacnUnicast(false),
        mqttEnabled(false),
        mqttBroker(""),
        mqttPort(1883),
        mqttUsername(""),
        mqttPassword(""),
        mqttTopicPrefix("lume") {}
};

class Storage {
public:
    Storage();
    
    // Initialize storage
    bool begin();

    // Probe whether persistent storage (NVS) is actually reachable.
    // Real health signal for /health — NVS is opened lazily, so begin() alone proves nothing.
    bool isReady();
    
    // Config operations
    bool loadConfig(Config& config);
    bool saveConfig(const Config& config);
    bool clearConfig();
    
    // LED state operations
    bool saveLedState(const JsonDocument& state);
    bool loadLedState(JsonDocument& state);
    
    // Effect persistence (for restoring last effect after reboot)
    bool saveLastEffect(const char* effectId);
    bool loadLastEffect(String& effectId);

    // Export config to JSON (with optional API key masking)
    void configToJson(const Config& config, JsonDocument& doc, bool maskApiKey = true);
    
    // Import config from JSON
    bool configFromJson(Config& config, const JsonDocument& doc);

private:
    Preferences prefs;
    SemaphoreHandle_t mutex_;   // guards all prefs access (see StorageLock)
    static const char* NAMESPACE_CONFIG;
    static const char* NAMESPACE_LED;
};

extern Storage storage;

#endif // STORAGE_H
