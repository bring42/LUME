#include "storage.h"

const char* Storage::NAMESPACE_CONFIG = "config";
const char* Storage::NAMESPACE_LED = "ledstate";
const char* Storage::NAMESPACE_PROMPT = "prompt";
const char* Storage::NAMESPACE_SCENES = "scenes";

Storage storage;

Storage::Storage() {}

bool Storage::begin() {
    return true; // Preferences doesn't need explicit init
}

bool Storage::loadConfig(Config& config) {
    if (!prefs.begin(NAMESPACE_CONFIG, true)) { // read-only
        return false;
    }
    
    config.wifiSSID = prefs.getString("ssid", "");
    config.wifiPassword = prefs.getString("pass", "");
    config.apiKey = prefs.getString("apikey", "");
    config.openRouterModel = prefs.getString("model", "claude-sonnet-4-5-20250929");
    config.ledPin = prefs.getUChar("ledpin", 21);
    config.ledCount = prefs.getUShort("ledcount", 160);
    config.defaultBrightness = prefs.getUChar("brightness", 128);
    config.sacnEnabled = prefs.getBool("sacn_en", false);
    config.sacnUniverse = prefs.getUShort("sacn_uni", 1);
    config.sacnUniverseCount = prefs.getUChar("sacn_ucnt", 1);
    config.sacnStartChannel = prefs.getUShort("sacn_ch", 1);
    config.sacnUnicast = prefs.getBool("sacn_uc", false);
    
    prefs.end();
    return true;
}

bool Storage::saveConfig(const Config& config) {
    if (!prefs.begin(NAMESPACE_CONFIG, false)) { // read-write
        return false;
    }
    
    prefs.putString("ssid", config.wifiSSID);
    prefs.putString("pass", config.wifiPassword);
    prefs.putString("apikey", config.apiKey);
    prefs.putString("model", config.openRouterModel);
    prefs.putUChar("ledpin", config.ledPin);
    prefs.putUShort("ledcount", config.ledCount);
    prefs.putUChar("brightness", config.defaultBrightness);
    prefs.putBool("sacn_en", config.sacnEnabled);
    prefs.putUShort("sacn_uni", config.sacnUniverse);
    prefs.putUChar("sacn_ucnt", config.sacnUniverseCount);
    prefs.putUShort("sacn_ch", config.sacnStartChannel);
    prefs.putBool("sacn_uc", config.sacnUnicast);
    
    prefs.end();
    return true;
}

bool Storage::clearConfig() {
    if (!prefs.begin(NAMESPACE_CONFIG, false)) {
        return false;
    }
    prefs.clear();
    prefs.end();
    return true;
}

bool Storage::saveLedState(const JsonDocument& state) {
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
    if (!prefs.begin(NAMESPACE_LED, true)) {
        return false;
    }
    
    String jsonStr = prefs.getString("state", "{}");
    prefs.end();
    
    DeserializationError err = deserializeJson(state, jsonStr);
    return err == DeserializationError::Ok;
}

bool Storage::savePromptSpec(const PromptSpec& spec) {
    if (!prefs.begin(NAMESPACE_PROMPT, false)) {
        return false;
    }
    
    // Truncate if too long
    String jsonToSave = spec.jsonSpec.substring(0, 3900);
    String promptToSave = spec.prompt.substring(0, 500);
    
    prefs.putString("json", jsonToSave);
    prefs.putString("prompt", promptToSave);
    prefs.putULong("ts", spec.timestamp);
    prefs.putBool("valid", spec.valid);
    
    prefs.end();
    return true;
}

bool Storage::loadPromptSpec(PromptSpec& spec) {
    if (!prefs.begin(NAMESPACE_PROMPT, true)) {
        return false;
    }
    
    spec.jsonSpec = prefs.getString("json", "");
    spec.prompt = prefs.getString("prompt", "");
    spec.timestamp = prefs.getULong("ts", 0);
    spec.valid = prefs.getBool("valid", false);
    
    prefs.end();
    return true;
}

bool Storage::clearPromptSpec() {
    if (!prefs.begin(NAMESPACE_PROMPT, false)) {
        return false;
    }
    prefs.clear();
    prefs.end();
    return true;
}

void Storage::configToJson(const Config& config, JsonDocument& doc, bool maskApiKey) {
    doc["wifiSSID"] = config.wifiSSID;
    doc["wifiPassword"] = ""; // Never expose password
    doc["apiKey"] = maskApiKey ? (config.apiKey.length() > 0 ? "****" + config.apiKey.substring(config.apiKey.length() - 4) : "") : config.apiKey;
    doc["apiKeySet"] = config.apiKey.length() > 0;
    doc["openRouterModel"] = config.openRouterModel;
    doc["ledPin"] = config.ledPin;
    doc["ledCount"] = config.ledCount;
    doc["defaultBrightness"] = config.defaultBrightness;
    doc["sacnEnabled"] = config.sacnEnabled;
    doc["sacnUniverse"] = config.sacnUniverse;
    doc["sacnUniverseCount"] = config.sacnUniverseCount;
    doc["sacnStartChannel"] = config.sacnStartChannel;
    doc["sacnUnicast"] = config.sacnUnicast;
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
    if (doc["apiKey"].is<const char*>()) {
        String key = doc["apiKey"].as<String>();
        // Don't overwrite with masked value
        if (key.length() > 0 && !key.startsWith("****")) {
            config.apiKey = key;
        }
    }
    if (doc["openRouterModel"].is<const char*>()) {
        config.openRouterModel = doc["openRouterModel"].as<String>();
    }
    if (doc["ledPin"].is<int>()) {
        config.ledPin = doc["ledPin"].as<uint8_t>();
    }
    if (doc["ledCount"].is<int>()) {
        config.ledCount = doc["ledCount"].as<uint16_t>();
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
    
    return true;
}

// Scene storage methods
bool Storage::saveScene(uint8_t slot, const Scene& scene) {
    if (slot >= MAX_SCENES) return false;
    
    if (!prefs.begin(NAMESPACE_SCENES, false)) {
        return false;
    }
    
    String nameKey = "n" + String(slot);
    String specKey = "s" + String(slot);
    
    prefs.putString(nameKey.c_str(), scene.name.substring(0, 32));
    prefs.putString(specKey.c_str(), scene.jsonSpec.substring(0, 1500));
    
    prefs.end();
    return true;
}

bool Storage::loadScene(uint8_t slot, Scene& scene) {
    if (slot >= MAX_SCENES) return false;
    
    if (!prefs.begin(NAMESPACE_SCENES, true)) {
        return false;
    }
    
    String nameKey = "n" + String(slot);
    String specKey = "s" + String(slot);
    
    scene.name = prefs.getString(nameKey.c_str(), "");
    scene.jsonSpec = prefs.getString(specKey.c_str(), "");
    
    prefs.end();
    return scene.name.length() > 0;
}

bool Storage::deleteScene(uint8_t slot) {
    if (slot >= MAX_SCENES) return false;
    
    if (!prefs.begin(NAMESPACE_SCENES, false)) {
        return false;
    }
    
    String nameKey = "n" + String(slot);
    String specKey = "s" + String(slot);
    
    prefs.remove(nameKey.c_str());
    prefs.remove(specKey.c_str());
    
    prefs.end();
    return true;
}

int Storage::getSceneCount() {
    int count = 0;
    Scene scene;
    for (int i = 0; i < MAX_SCENES; i++) {
        if (loadScene(i, scene) && !scene.isEmpty()) {
            count++;
        }
    }
    return count;
}

bool Storage::listScenes(JsonDocument& doc) {
    JsonArray arr = doc.to<JsonArray>();
    
    for (int i = 0; i < MAX_SCENES; i++) {
        Scene scene;
        if (loadScene(i, scene) && !scene.isEmpty()) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = i;
            obj["name"] = scene.name;
        }
    }
    return true;
}
