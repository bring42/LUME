#include "storage.h"
#include "constants.h"  // MAX_LED_COUNT (ledCount clamp, P0.2)

const char* Storage::NAMESPACE_CONFIG = "config";
const char* Storage::NAMESPACE_LED = "ledstate";

Storage storage;

Storage::Storage() {
    mutex_ = xSemaphoreCreateRecursiveMutex();
}

bool Storage::begin() {
    StorageLock lock(mutex_);
    return true; // Preferences doesn't need explicit init
}

bool Storage::loadConfig(Config& config) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_CONFIG, true)) { // read-only
        return false;
    }
    
    config.wifiSSID = prefs.getString("ssid", "");
    config.wifiPassword = prefs.getString("pass", "");
    config.aiApiKey = prefs.getString("ai_apikey", "");
    config.aiModel = prefs.getString("ai_model", "claude-3-5-haiku-20241022");
    config.authToken = prefs.getString("authtoken", "");
    // Clamp on load too, in case NVS holds an out-of-range value (P0.2).
    config.ledCount = constrain((int)prefs.getUShort("ledcount", 160), 1, (int)MAX_LED_COUNT);
    config.defaultBrightness = prefs.getUChar("brightness", 128);
    config.sacnEnabled = prefs.getBool("sacn_en", false);
    config.sacnUniverse = prefs.getUShort("sacn_uni", 1);
    config.sacnUniverseCount = prefs.getUChar("sacn_ucnt", 1);
    config.sacnStartChannel = prefs.getUShort("sacn_ch", 1);
    config.sacnUnicast = prefs.getBool("sacn_uc", false);
    
    // MQTT settings
    config.mqttEnabled = prefs.getBool("mqtt_en", false);
    config.mqttBroker = prefs.getString("mqtt_broker", "");
    config.mqttPort = prefs.getUShort("mqtt_port", 1883);
    config.mqttUsername = prefs.getString("mqtt_user", "");
    config.mqttPassword = prefs.getString("mqtt_pass", "");
    config.mqttTopicPrefix = prefs.getString("mqtt_prefix", "lume");
    
    prefs.end();
    return true;
}

bool Storage::saveConfig(const Config& config) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_CONFIG, false)) { // read-write
        return false;
    }
    
    prefs.putString("ssid", config.wifiSSID);
    prefs.putString("pass", config.wifiPassword);
    prefs.putString("ai_apikey", config.aiApiKey);
    prefs.putString("ai_model", config.aiModel);
    prefs.putString("authtoken", config.authToken);
    prefs.putUShort("ledcount", config.ledCount);
    prefs.putUChar("brightness", config.defaultBrightness);
    prefs.putBool("sacn_en", config.sacnEnabled);
    prefs.putUShort("sacn_uni", config.sacnUniverse);
    prefs.putUChar("sacn_ucnt", config.sacnUniverseCount);
    prefs.putUShort("sacn_ch", config.sacnStartChannel);
    prefs.putBool("sacn_uc", config.sacnUnicast);
    
    // MQTT settings
    prefs.putBool("mqtt_en", config.mqttEnabled);
    prefs.putString("mqtt_broker", config.mqttBroker);
    prefs.putUShort("mqtt_port", config.mqttPort);
    prefs.putString("mqtt_user", config.mqttUsername);
    prefs.putString("mqtt_pass", config.mqttPassword);
    prefs.putString("mqtt_prefix", config.mqttTopicPrefix);
    
    prefs.end();
    return true;
}

bool Storage::clearConfig() {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_CONFIG, false)) {
        return false;
    }
    prefs.clear();
    prefs.end();
    return true;
}

bool Storage::saveLedState(const JsonDocument& state) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_LED, false)) {
        return false;
    }
    
    String jsonStr;
    serializeJson(state, jsonStr);
    
    // NVS has limits, so check size
    if (jsonStr.length() > 4000) {
        prefs.end();
        return false;
    }
    
    prefs.putString("state", jsonStr);
    prefs.end();
    return true;
}

bool Storage::loadLedState(JsonDocument& state) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_LED, true)) {
        return false;
    }
    
    String jsonStr = prefs.getString("state", "{}");
    prefs.end();
    
    DeserializationError err = deserializeJson(state, jsonStr);
    return err == DeserializationError::Ok;
}

bool Storage::saveLastEffect(const char* effectId) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_LED, false)) {
        return false;
    }
    
    prefs.putString("lasteffect", effectId);
    prefs.end();
    return true;
}

bool Storage::loadLastEffect(String& effectId) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_LED, true)) {
        return false;
    }
    
    effectId = prefs.getString("lasteffect", "rainbow");  // Default to rainbow
    prefs.end();
    return true;
}

void Storage::configToJson(const Config& config, JsonDocument& doc, bool maskApiKey) {
    doc["wifiSSID"] = config.wifiSSID;
    doc["wifiPassword"] = ""; // Never expose password
    doc["aiApiKey"] = maskApiKey ? (config.aiApiKey.length() > 0 ? "****" + config.aiApiKey.substring(config.aiApiKey.length() - 4) : "") : config.aiApiKey;
    doc["aiApiKeySet"] = config.aiApiKey.length() > 0;
    doc["aiModel"] = config.aiModel;
    doc["authToken"] = config.authToken.length() > 0 ? "****" : "";
    doc["authEnabled"] = config.authToken.length() > 0;
    doc["ledCount"] = config.ledCount;
    doc["defaultBrightness"] = config.defaultBrightness;
    doc["sacnEnabled"] = config.sacnEnabled;
    doc["sacnUniverse"] = config.sacnUniverse;
    doc["sacnUniverseCount"] = config.sacnUniverseCount;
    doc["sacnStartChannel"] = config.sacnStartChannel;
    doc["sacnUnicast"] = config.sacnUnicast;
    
    // MQTT settings
    doc["mqttEnabled"] = config.mqttEnabled;
    doc["mqttBroker"] = config.mqttBroker;
    doc["mqttPort"] = config.mqttPort;
    doc["mqttUsername"] = config.mqttUsername.length() > 0 ? "****" : "";
    doc["mqttPassword"] = config.mqttPassword.length() > 0 ? "****" : "";
    doc["mqttTopicPrefix"] = config.mqttTopicPrefix;
}

bool Storage::configFromJson(Config& config, const JsonDocument& doc) {
    // Only update fields that are present
    if (doc["wifiSSID"].is<const char*>()) {
        config.wifiSSID = doc["wifiSSID"].as<String>();
    }
    if (doc["wifiPassword"].is<const char*>()) {
        String pass = doc["wifiPassword"].as<String>();
        if (pass.length() > 0) {
            config.wifiPassword = pass;
        }
    }
    if (doc["aiApiKey"].is<const char*>()) {
        String key = doc["aiApiKey"].as<String>();
        // Don't overwrite with masked value
        if (key.length() > 0 && !key.startsWith("****")) {
            config.aiApiKey = key;
        }
    }
    if (doc["aiModel"].is<const char*>()) {
        config.aiModel = doc["aiModel"].as<String>();
    }
    if (doc["authToken"].is<const char*>()) {
        String token = doc["authToken"].as<String>();
        // Don't overwrite with masked value, empty string clears the token
        if (!token.startsWith("****")) {
            config.authToken = token;
        }
    }
    if (doc["ledCount"].is<int>()) {
        // Constrain to [1, MAX_LED_COUNT]: 0 underflows the pixels gradient
        // (ledCount-1 -> 65535) into a heap OOB write; >MAX overruns leds[] (P0.2).
        config.ledCount = constrain(doc["ledCount"].as<int>(), 1, (int)MAX_LED_COUNT);
    }
    if (doc["defaultBrightness"].is<int>()) {
        config.defaultBrightness = doc["defaultBrightness"].as<uint8_t>();
    }
    if (doc["sacnEnabled"].is<bool>()) {
        config.sacnEnabled = doc["sacnEnabled"].as<bool>();
    }
    if (doc["sacnUniverse"].is<int>()) {
        config.sacnUniverse = constrain(doc["sacnUniverse"].as<int>(), 1, 63999);
    }
    if (doc["sacnUniverseCount"].is<int>()) {
        config.sacnUniverseCount = constrain(doc["sacnUniverseCount"].as<int>(), 1, 8);
    }
    if (doc["sacnStartChannel"].is<int>()) {
        config.sacnStartChannel = constrain(doc["sacnStartChannel"].as<int>(), 1, 512);
    }
    if (doc["sacnUnicast"].is<bool>()) {
        config.sacnUnicast = doc["sacnUnicast"].as<bool>();
    }
    
    // MQTT settings
    if (doc["mqttEnabled"].is<bool>()) {
        config.mqttEnabled = doc["mqttEnabled"].as<bool>();
    }
    if (doc["mqttBroker"].is<const char*>()) {
        config.mqttBroker = doc["mqttBroker"].as<String>();
    }
    if (doc["mqttPort"].is<int>()) {
        config.mqttPort = doc["mqttPort"].as<uint16_t>();
    }
    if (doc["mqttUsername"].is<const char*>()) {
        String user = doc["mqttUsername"].as<String>();
        if (!user.startsWith("****")) {
            config.mqttUsername = user;
        }
    }
    if (doc["mqttPassword"].is<const char*>()) {
        String pass = doc["mqttPassword"].as<String>();
        if (!pass.startsWith("****")) {
            config.mqttPassword = pass;
        }
    }
    if (doc["mqttTopicPrefix"].is<const char*>()) {
        config.mqttTopicPrefix = doc["mqttTopicPrefix"].as<String>();
    }
    
    return true;
}
