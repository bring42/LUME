#include "storage.h"
#include "constants.h"  // MAX_LED_COUNT (ledCount clamp, P0.2)
#include "logging.h"

const char* Storage::NAMESPACE_CONFIG = "config";
const char* Storage::NAMESPACE_LED = "ledstate";

Storage storage;

void normalizeAiModel(String& model) {
    // This feature has died twice by model retirement: claude-3-5-sonnet-20241022,
    // then claude-3-5-haiku-20241022 (both 404 at the API today).
    if (model.length() == 0 ||
        model == "claude-3-5-sonnet-20241022" ||
        model == "claude-3-5-haiku-20241022") {
        model = AI_MODEL_DEFAULT;
    }
}

Storage::Storage() {
    mutex_ = xSemaphoreCreateRecursiveMutex();
}

bool Storage::begin() {
    StorageLock lock(mutex_);
    return true; // Preferences doesn't need explicit init
}

bool Storage::isReady() {
    StorageLock lock(mutex_);
    // Probe NVS by opening the config namespace read-WRITE: this succeeds whenever
    // NVS is healthy (creating the namespace if it doesn't exist yet) and fails only
    // when the partition is genuinely broken. A read-only open would false-negative
    // on a factory-fresh device whose config namespace no save has created yet.
    if (!prefs.begin(NAMESPACE_CONFIG, false)) {
        return false;
    }
    prefs.end();
    return true;
}

bool Storage::loadConfig(Config& config) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_CONFIG, true)) { // read-only
        return false;
    }
    
    config.wifiSSID = prefs.getString("ssid", "");
    config.wifiPassword = prefs.getString("pass", "");
    config.aiApiKey = prefs.getString("ai_apikey", "");
    config.aiModel = prefs.getString("ai_model", AI_MODEL_DEFAULT);
    // Any config save writes ALL fields, so a device that ever saved (ledCount,
    // WiFi, ...) has the then-default model pinned in NVS; without this a new
    // default could never heal an existing device.
    normalizeAiModel(config.aiModel);
    config.authToken = prefs.getString("authtoken", "");
    // Clamp on load too, in case NVS holds an out-of-range value (P0.2).
    config.ledCount = constrain((int)prefs.getUShort("ledcount", 160), 1, (int)MAX_LED_COUNT);
    // LED hardware: stored as the catalog KEYS (not enum ordinals) so a future
    // reorder of the catalog can't silently retarget a strip. Unknown/invalid
    // values fall back to the compile-time defaults field-by-field.
    {
        lume::LedHardware hw = lume::LedHardware::defaults();
        String type = prefs.getString("ledtype", "");
        if (type.length() > 0 && !lume::parseLedChipset(type.c_str(), hw.chipset)) {
            LOG_WARN(LogTag::STORAGE, "Unknown LED type '%s' in NVS; using %s",
                     type.c_str(), lume::ledChipsetKey(hw.chipset));
        }
        String order = prefs.getString("ledorder", "");
        if (order.length() > 0 && !lume::parseLedColorOrder(order.c_str(), hw.order)) {
            LOG_WARN(LogTag::STORAGE, "Unknown LED colour order '%s' in NVS; using %s",
                     order.c_str(), lume::ledColorOrderKey(hw.order));
        }
        int pin = prefs.getUChar("ledpin", hw.pin);
        if (lume::isUsableLedPin(pin)) {
            hw.pin = (uint8_t)pin;
        } else {
            LOG_WARN(LogTag::STORAGE, "LED pin %d in NVS not usable on this chip; using GPIO %u", pin, hw.pin);
        }
        config.ledHardware = hw;
    }
    config.defaultBrightness = prefs.getUChar("brightness", 128);
    // Clamp on load too, in case NVS holds an out-of-range value.
    config.gamma = constrain(prefs.getFloat("gamma", LED_GAMMA), LED_GAMMA_MIN, LED_GAMMA_MAX);
    config.warmth = constrain(prefs.getFloat("warmth", LED_WARMTH_DEFAULT), LED_WARMTH_MIN, LED_WARMTH_MAX);
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
    prefs.putString("ledtype", lume::ledChipsetKey(config.ledHardware.chipset));
    prefs.putString("ledorder", lume::ledColorOrderKey(config.ledHardware.order));
    prefs.putUChar("ledpin", config.ledHardware.pin);
    prefs.putUChar("brightness", config.defaultBrightness);
    prefs.putFloat("gamma", config.gamma);
    prefs.putFloat("warmth", config.warmth);
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
    
    effectId = prefs.getString("lasteffect", "curator");  // Default to curator (premium)
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
    doc["ledType"] = lume::ledChipsetKey(config.ledHardware.chipset);
    doc["ledColorOrder"] = lume::ledColorOrderKey(config.ledHardware.order);
    doc["ledPin"] = config.ledHardware.pin;
    doc["defaultBrightness"] = config.defaultBrightness;
    doc["gamma"] = config.gamma;
    doc["warmth"] = config.warmth;
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

bool Storage::configFromJson(Config& config, const JsonDocument& doc, String* error) {
    // LED hardware is validated up front, before ANY field is applied: a bad
    // strip type must not leave half a request applied in RAM with nothing
    // persisted. The pin is checked against this chip's usable-pin policy
    // (never the flash/USB pins). All three apply on the next reboot.
    lume::LedHardware hw = config.ledHardware;
    if (!doc["ledType"].isNull()) {
        if (!doc["ledType"].is<const char*>() || !lume::parseLedChipset(doc["ledType"].as<const char*>(), hw.chipset)) {
            if (error) *error = "ledType: unknown strip type";
            return false;
        }
    }
    if (!doc["ledColorOrder"].isNull()) {
        if (!doc["ledColorOrder"].is<const char*>() || !lume::parseLedColorOrder(doc["ledColorOrder"].as<const char*>(), hw.order)) {
            if (error) *error = "ledColorOrder: expected one of RGB RBG GRB GBR BRG BGR";
            return false;
        }
    }
    if (!doc["ledPin"].isNull()) {
        if (!doc["ledPin"].is<int>() || !lume::isUsableLedPin(doc["ledPin"].as<int>())) {
            if (error) *error = "ledPin: not a usable LED data pin on this chip";
            return false;
        }
        hw.pin = (uint8_t)doc["ledPin"].as<int>();
    }
    config.ledHardware = hw;

    // Only update fields that are present
    // "wifiSSID" is the one canonical key, and the spelling is load-bearing:
    // ArduinoJson is case-sensitive, and a UI sending anything else (PR #29
    // shipped "wifiSsid") silently drops the SSID here. Keep the web UI
    // (data/) matched to this exact key.
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
        normalizeAiModel(config.aiModel);   // empty/retired -> default now, not at next boot
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
    if (doc["gamma"].is<float>()) {
        // Clamp to the sane runtime range; the controller clamps again on apply.
        config.gamma = constrain(doc["gamma"].as<float>(), LED_GAMMA_MIN, LED_GAMMA_MAX);
    }
    if (doc["warmth"].is<float>()) {
        config.warmth = constrain(doc["warmth"].as<float>(), LED_WARMTH_MIN, LED_WARMTH_MAX);
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
