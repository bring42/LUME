#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// Configuration structure
struct Config {
    String wifiSSID;
    String wifiPassword;
    String apiKey;
    String openRouterModel;
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
        apiKey(""),
        openRouterModel("claude-sonnet-4-5-20250929"),
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

// Last generated effect spec storage
struct PromptSpec {
    String jsonSpec;
    String prompt;
    unsigned long timestamp;
    bool valid;
    
    PromptSpec() : jsonSpec(""), prompt(""), timestamp(0), valid(false) {}
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
    
    // Prompt spec operations
    bool savePromptSpec(const PromptSpec& spec);
    bool loadPromptSpec(PromptSpec& spec);
    bool clearPromptSpec();
    
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
    static const char* NAMESPACE_CONFIG;
    static const char* NAMESPACE_LED;
    static const char* NAMESPACE_PROMPT;
    static const char* NAMESPACE_SCENES;
};

extern Storage storage;

#endif // STORAGE_H
