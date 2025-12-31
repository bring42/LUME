#include "config.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../lume.h"
#include "../protocols/sacn.h"
#include "../protocols/mqtt.h"

// External globals
extern Config config;
extern Storage storage;
extern bool wifiConnected;

// Static body buffer for async request handling
static String configBodyBuffer;

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
            
            // Handle sACN enable/disable (using new protocol system)
            if (config.sacnEnabled && wifiConnected) {
                lume::sacnProtocol.stop();
                lume::sacnProtocol.configure(config.sacnUniverse, config.sacnUniverseCount,
                                              config.sacnUnicast, config.sacnStartChannel);
                lume::sacnProtocol.begin();
            } else {
                lume::sacnProtocol.stop();
            }
            
            // Handle MQTT enable/disable
            if (config.mqttEnabled && config.mqttBroker.length() > 0 && wifiConnected) {
                lume::MqttConfig mqttConfig;
                mqttConfig.enabled = config.mqttEnabled;
                mqttConfig.broker = config.mqttBroker;
                mqttConfig.port = config.mqttPort;
                mqttConfig.username = config.mqttUsername;
                mqttConfig.password = config.mqttPassword;
                mqttConfig.topicPrefix = config.mqttTopicPrefix;
                lume::mqtt.setConfig(mqttConfig);
            } else {
                lume::MqttConfig disabledConfig;
                disabledConfig.enabled = false;
                lume::mqtt.setConfig(disabledConfig);
            }
            
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save\"}");
        }
    }
}
