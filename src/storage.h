#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// RAII guard for Storage's NVS access. The single Preferences object is shared
// across tasks (saveLastEffect from the web/AI task vs saveLedState from the
// render loop) — concurrent begin()/end() corrupts the handle. Recursive because
// getSceneCount()/listScenes() call loadScene() while already holding it.
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

// Scene storage (saved AI-generated effects)
#define MAX_SCENES 10

struct Scene {
    String name;
    String jsonSpec;
    
    Scene() : name(""), jsonSpec("") {}
    bool isEmpty() const { return name.length() == 0; }
};

class Storage {
public:
    Storage();
    
    // Initialize storage
    bool begin();
    
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
    
    // Scene operations
    bool saveScene(uint8_t slot, const Scene& scene);
    bool loadScene(uint8_t slot, Scene& scene);
    bool deleteScene(uint8_t slot);
    int getSceneCount();
    bool listScenes(JsonDocument& doc);
    
private:
    Preferences prefs;
    SemaphoreHandle_t mutex_;   // guards all prefs access (see StorageLock)
    static const char* NAMESPACE_CONFIG;
    static const char* NAMESPACE_LED;
    static const char* NAMESPACE_SCENES;
};

extern Storage storage;

#endif // STORAGE_H
