#ifndef LUME_MQTT_H
#define LUME_MQTT_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "protocol.h"
#include "../core/controller.h"

namespace lume {

// MQTT configuration
struct MqttConfig {
    bool enabled = false;
    String broker;              // Hostname or IP
    uint16_t port = 1883;
    String username;
    String password;
    String topicPrefix = "lume"; // Base topic (e.g., "lume" -> "lume/state")
    String clientId;            // Auto-generated if empty
    uint16_t keepAlive = 60;    // Keep-alive interval in seconds
    
    bool isValid() const {
        return enabled && broker.length() > 0;
    }
};

// MQTT topic structure:
//   {prefix}/status        - Online/offline (LWT)
//   {prefix}/state         - JSON state (published on change)
//   {prefix}/set           - JSON commands (subscribed)
//   {prefix}/brightness/set - Brightness control
//   {prefix}/effect/set    - Effect name
//   {prefix}/power/set     - on/off

class MqttProtocol {
public:
    MqttProtocol();
    
    // Lifecycle
    void begin(const MqttConfig& config, LumeController* controller);
    void end();
    void update();  // Call from loop() for reconnection and message processing
    
    // Configuration
    void setConfig(const MqttConfig& config);
    const MqttConfig& getConfig() const { return config_; }
    
    // State publishing
    void publishState();           // Publish full state
    void publishAvailability();    // Publish online status
    void publishDiscovery();       // Home Assistant MQTT discovery
    
    // Status
    bool isConnected() { return client_.connected(); }
    bool isEnabled() const { return config_.enabled; }
    unsigned long getLastConnectAttempt() const { return lastConnectAttempt_; }
    uint32_t getReconnectCount() const { return reconnectCount_; }

private:
    // Connection management
    bool connect();
    void disconnect();
    void reconnect();
    
    // Topic helpers
    String buildTopic(const char* suffix) const;
    void subscribe();
    
    // Message handling
    static void messageCallback(char* topic, byte* payload, unsigned int length);
    void handleMessage(const String& topic, const String& payload);
    void handleSetCommand(const JsonDocument& doc);
    void handleBrightnessSet(const String& payload);
    void handleEffectSet(const String& payload);
    void handlePowerSet(const String& payload);
    
    // State tracking for change detection
    void updateStateHash();
    bool stateChanged() const;
    
    WiFiClient wifiClient_;
    PubSubClient client_;
    MqttConfig config_;
    LumeController* controller_ = nullptr;
    
    // Connection state
    unsigned long lastConnectAttempt_ = 0;
    unsigned long lastStatePublish_ = 0;
    uint32_t reconnectCount_ = 0;
    bool wasConnected_ = false;
    
    // State change detection
    uint32_t lastStateHash_ = 0;
    
    // Singleton for callback routing
    static MqttProtocol* instance_;
    
    // Constants
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;
    static constexpr uint32_t STATE_PUBLISH_INTERVAL_MS = 30000; // Periodic republish
    static constexpr uint16_t MQTT_BUFFER_SIZE = 1024;
};

// Global instance
extern MqttProtocol mqtt;

}  // namespace lume

#endif // LUME_MQTT_H
