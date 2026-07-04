#include "mqtt.h"
#include "../logging.h"
#include "../constants.h"
#include <WiFi.h>

namespace lume {

// Singleton instance for callback routing
MqttProtocol* MqttProtocol::instance_ = nullptr;
MqttProtocol mqtt;

// Slot index of the first Color param in the effect's schema, or -1 if the
// effect has none (e.g. palette-driven effects like rainbow).
static int8_t firstColorParamIndex(const EffectInfo* info) {
    if (!info || !info->hasSchema()) return -1;
    for (uint8_t i = 0; i < info->schema->count && i < MAX_EFFECT_PARAMS; i++) {
        if (info->schema->params[i].type == ParamType::Color) return i;
    }
    return -1;
}

MqttProtocol::MqttProtocol() : client_(wifiClient_) {
    instance_ = this;
    client_.setCallback(messageCallback);
    client_.setBufferSize(MQTT_BUFFER_SIZE);
}

void MqttProtocol::begin(const MqttConfig& config, LumeController* controller) {
    config_ = config;
    controller_ = controller;
    
    if (!config_.isValid()) {
        LOG_INFO(LogTag::MAIN, "MQTT disabled");
        return;
    }
    
    // Generate client ID if not set
    if (config_.clientId.isEmpty()) {
        config_.clientId = "lume-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    }
    
    client_.setServer(config_.broker.c_str(), config_.port);
    client_.setKeepAlive(config_.keepAlive);
    
    LOG_INFO(LogTag::MAIN, "MQTT configured: %s:%d", config_.broker.c_str(), config_.port);
}

void MqttProtocol::end() {
    if (client_.connected()) {
        // Publish offline status before disconnecting
        String statusTopic = buildTopic("status");
        client_.publish(statusTopic.c_str(), "offline", true);
        client_.disconnect();
    }
    config_.enabled = false;
}

void MqttProtocol::update() {
    if (!config_.isValid() || !WiFi.isConnected()) {
        return;
    }
    
    // Handle reconnection
    if (!client_.connected()) {
        if (wasConnected_) {
            LOG_WARN(LogTag::MAIN, "MQTT disconnected");
            wasConnected_ = false;
        }
        reconnect();
    } else {
        if (!wasConnected_) {
            wasConnected_ = true;
        }
        
        // Process incoming messages
        client_.loop();
        
        // Periodic state publish
        if (millis() - lastStatePublish_ >= STATE_PUBLISH_INTERVAL_MS) {
            publishState();
        }
        
        // Publish on state change
        if (stateChanged()) {
            publishState();
        }
    }
}

void MqttProtocol::setConfig(const MqttConfig& config) {
    bool wasEnabled = config_.enabled && client_.connected();

    config_ = config;

    // Same as begin(): runtime enables via the web UI land here without ever
    // having had a client ID generated
    if (config_.clientId.isEmpty()) {
        config_.clientId = "lume-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    }

    if (wasEnabled && !config_.isValid()) {
        end();
    } else if (config_.isValid()) {
        if (client_.connected()) {
            client_.disconnect();
        }
        client_.setServer(config_.broker.c_str(), config_.port);
        client_.setKeepAlive(config_.keepAlive);
    }
}

bool MqttProtocol::connect() {
    if (!config_.isValid()) return false;
    
    String statusTopic = buildTopic("status");
    
    bool connected = false;
    if (config_.username.length() > 0) {
        connected = client_.connect(
            config_.clientId.c_str(),
            config_.username.c_str(),
            config_.password.c_str(),
            statusTopic.c_str(),  // LWT topic
            0,                     // QoS
            true,                  // Retain
            "offline"              // LWT message
        );
    } else {
        connected = client_.connect(
            config_.clientId.c_str(),
            statusTopic.c_str(),
            0,
            true,
            "offline"
        );
    }
    
    if (connected) {
        LOG_INFO(LogTag::MAIN, "MQTT connected to %s", config_.broker.c_str());
        publishAvailability();
        subscribe();
        publishState();
        publishDiscovery();
        return true;
    }
    
    LOG_WARN(LogTag::MAIN, "MQTT connect failed, rc=%d", client_.state());
    return false;
}

void MqttProtocol::disconnect() {
    if (client_.connected()) {
        String statusTopic = buildTopic("status");
        client_.publish(statusTopic.c_str(), "offline", true);
        client_.disconnect();
    }
}

void MqttProtocol::reconnect() {
    unsigned long now = millis();
    
    if (now - lastConnectAttempt_ < RECONNECT_INTERVAL_MS) {
        return;
    }
    
    lastConnectAttempt_ = now;
    reconnectCount_++;
    
    LOG_DEBUG(LogTag::MAIN, "MQTT reconnecting (attempt %d)", reconnectCount_);
    connect();
}

String MqttProtocol::buildTopic(const char* suffix) const {
    return config_.topicPrefix + "/" + suffix;
}

void MqttProtocol::subscribe() {
    // Subscribe to command topics
    client_.subscribe(buildTopic("set").c_str());
    client_.subscribe(buildTopic("brightness/set").c_str());
    client_.subscribe(buildTopic("effect/set").c_str());
    client_.subscribe(buildTopic("power/set").c_str());
    
    LOG_DEBUG(LogTag::MAIN, "MQTT subscribed to command topics");
}

void MqttProtocol::publishAvailability() {
    String topic = buildTopic("status");
    client_.publish(topic.c_str(), "online", true);
}

void MqttProtocol::publishState() {
    if (!client_.connected() || !controller_) return;
    
    JsonDocument doc;

    // Home Assistant JSON-schema light state ("state" key is required by HA);
    // "power" is kept for pre-existing non-HA consumers.
    doc["state"] = controller_->getPower() ? "ON" : "OFF";
    doc["power"] = controller_->getPower();
    doc["brightness"] = controller_->getBrightness();

    // Current effect (from first segment)
    if (controller_->getSegmentCount() > 0) {
        const auto* seg = controller_->getSegment(0);
        if (seg) {
            doc["effect"] = seg->getEffectId();

            const EffectInfo* info = seg->getEffect();
            int8_t colorIdx = firstColorParamIndex(info);
            if (colorIdx >= 0) {
                CRGB c = seg->getParamValues().getColor(colorIdx);
                doc["color_mode"] = "rgb";
                JsonObject color = doc["color"].to<JsonObject>();
                color["r"] = c.r;
                color["g"] = c.g;
                color["b"] = c.b;
            }

            if (info && info->hasSchema()) {
                int8_t speedIdx = info->schema->indexOf("speed");
                if (speedIdx >= 0) doc["speed"] = seg->getParamValues().getInt(speedIdx);
                int8_t intensityIdx = info->schema->indexOf("intensity");
                if (intensityIdx >= 0) doc["intensity"] = seg->getParamValues().getInt(intensityIdx);
            }
        }
    }

    // System info
    doc["uptime"] = millis() / 1000;
    doc["heap_free"] = ESP.getFreeHeap();
    doc["ip"] = WiFi.localIP().toString();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = buildTopic("state");
    client_.publish(topic.c_str(), payload.c_str(), true);
    
    lastStatePublish_ = millis();
    updateStateHash();
    
    LOG_DEBUG(LogTag::MAIN, "MQTT state published");
}

void MqttProtocol::publishDiscovery() {
    // Home Assistant MQTT Discovery
    // https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
    
    if (!client_.connected() || !controller_) return;
    
    String deviceId = config_.clientId;
    deviceId.replace("-", "_");
    
    // Light entity discovery
    JsonDocument doc;
    
    doc["name"] = "LUME";
    doc["unique_id"] = deviceId + "_light";
    doc["schema"] = "json";
    
    doc["state_topic"] = buildTopic("state");
    doc["command_topic"] = buildTopic("set");
    doc["availability_topic"] = buildTopic("status");
    
    doc["brightness"] = true;
    doc["brightness_scale"] = 255;
    doc["supported_color_modes"].to<JsonArray>().add("rgb");
    doc["effect"] = true;
    
    // List available effects
    JsonArray effects = doc["effect_list"].to<JsonArray>();
    auto& registry = EffectRegistry::instance();
    for (size_t i = 0; i < registry.getCount(); i++) {
        effects.add(registry.getByIndex(i)->id);
    }
    
    // Device info for HA
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"].to<JsonArray>().add(deviceId);
    device["name"] = "LUME LED Controller";
    device["model"] = "ESP32-S3";
    device["manufacturer"] = "LUME";
    device["sw_version"] = FIRMWARE_VERSION;
    
    String payload;
    serializeJson(doc, payload);
    
    // Discovery topic: homeassistant/light/{device_id}/config
    String discoveryTopic = "homeassistant/light/" + deviceId + "/config";
    if (client_.publish(discoveryTopic.c_str(), payload.c_str(), true)) {
        LOG_INFO(LogTag::MAIN, "MQTT HA discovery published (%u bytes)", payload.length());
    } else {
        // PubSubClient drops payloads larger than its buffer without an error code
        LOG_WARN(LogTag::MAIN, "MQTT HA discovery publish failed (%u bytes, buffer %u)",
                 payload.length(), MQTT_BUFFER_SIZE);
    }
}

void MqttProtocol::messageCallback(char* topic, byte* payload, unsigned int length) {
    if (!instance_) return;
    
    String topicStr(topic);
    String payloadStr;
    payloadStr.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }
    
    instance_->handleMessage(topicStr, payloadStr);
}

void MqttProtocol::handleMessage(const String& topic, const String& payload) {
    LOG_DEBUG(LogTag::MAIN, "MQTT recv: %s", topic.c_str());
    
    String prefix = config_.topicPrefix + "/";
    
    if (!topic.startsWith(prefix)) {
        return;
    }
    
    String suffix = topic.substring(prefix.length());
    
    if (suffix == "set") {
        // JSON command
        JsonDocument doc;
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
            handleSetCommand(doc);
        }
    } else if (suffix == "brightness/set") {
        handleBrightnessSet(payload);
    } else if (suffix == "effect/set") {
        handleEffectSet(payload);
    } else if (suffix == "power/set") {
        handlePowerSet(payload);
    }
}

void MqttProtocol::handleSetCommand(const JsonDocument& doc) {
    if (!controller_) return;
    
    // Power state
    if (doc["state"].is<const char*>()) {
        String state = doc["state"].as<String>();
        state.toUpperCase();
        controller_->setPower(state == "ON" || state == "TRUE" || state == "1");
    }
    
    // Brightness
    if (doc["brightness"].is<int>()) {
        controller_->setBrightness(doc["brightness"].as<uint8_t>());
    }
    
    // RGB color (HA json schema: {"color":{"r":255,"g":0,"b":0}})
    if (doc["color"].is<JsonObjectConst>()) {
        JsonObjectConst color = doc["color"];
        if (controller_->getSegmentCount() > 0) {
            auto* seg = controller_->getSegment(0);
            // No-op if the current effect has no color param (palette-driven)
            if (seg) {
                seg->setColor(0, CRGB(color["r"] | 0, color["g"] | 0, color["b"] | 0));
            }
        }
    }

    // Effect
    if (doc["effect"].is<const char*>()) {
        String effect = doc["effect"].as<String>();
        // Apply to first segment for now
        if (controller_->getSegmentCount() > 0) {
            auto* seg = controller_->getSegment(0);
            if (seg) seg->setEffect(effect.c_str());
        }
    }
    
    // Speed
    if (doc["speed"].is<int>()) {
        if (controller_->getSegmentCount() > 0) {
            auto* seg = controller_->getSegment(0);
            if (seg) seg->setSpeed(doc["speed"].as<uint8_t>());
        }
    }
    
    // Intensity
    if (doc["intensity"].is<int>()) {
        if (controller_->getSegmentCount() > 0) {
            auto* seg = controller_->getSegment(0);
            if (seg) seg->setIntensity(doc["intensity"].as<uint8_t>());
        }
    }
    
    // Immediate state publish to confirm
    publishState();
}

void MqttProtocol::handleBrightnessSet(const String& payload) {
    if (!controller_) return;
    
    int brightness = payload.toInt();
    if (brightness >= 0 && brightness <= 255) {
        controller_->setBrightness(brightness);
        publishState();
    }
}

void MqttProtocol::handleEffectSet(const String& payload) {
    if (!controller_ || controller_->getSegmentCount() == 0) return;
    
    auto* seg = controller_->getSegment(0);
    if (seg) {
        seg->setEffect(payload.c_str());
        publishState();
    }
}

void MqttProtocol::handlePowerSet(const String& payload) {
    if (!controller_) return;
    
    String state = payload;
    state.toUpperCase();
    state.trim();
    
    bool power = (state == "ON" || state == "TRUE" || state == "1");
    controller_->setPower(power);
    publishState();
}

uint32_t MqttProtocol::computeStateHash() const {
    if (!controller_) return 0;

    // Simple hash of key state values
    uint32_t hash = 0;
    hash ^= controller_->getPower() ? 1 : 0;
    hash ^= controller_->getBrightness() << 8;

    if (controller_->getSegmentCount() > 0) {
        const auto* seg = controller_->getSegment(0);
        if (seg) {
            // Hash effect name
            const char* effectId = seg->getEffectId();
            if (effectId) {
                while (*effectId) {
                    hash = hash * 31 + *effectId++;
                }
            }

            // Hash primary color so out-of-band changes (web UI) reach HA
            int8_t colorIdx = firstColorParamIndex(seg->getEffect());
            if (colorIdx >= 0) {
                CRGB c = seg->getParamValues().getColor(colorIdx);
                hash = hash * 31 + (((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b);
            }
        }
    }

    return hash;
}

void MqttProtocol::updateStateHash() {
    lastStateHash_ = computeStateHash();
}

bool MqttProtocol::stateChanged() const {
    if (!controller_) return false;
    return computeStateHash() != lastStateHash_;
}

}  // namespace lume
