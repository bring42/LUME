#include "wifi.h"
#include "ota.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../protocols/sacn.h"
#include "../protocols/mqtt.h"
#include <WiFi.h>

// External globals
extern Config config;
extern bool wifiConnected;
extern unsigned long lastWifiAttempt;

// Access Point settings
#define AP_SSID "LUME-Setup"
#define AP_PASSWORD "ledcontrol"

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

// Helper function for WiFi reconnection and status monitoring
void handleWifiMaintenance() {
    // WiFi reconnection logic
    if (!wifiConnected && config.wifiSSID.length() > 0) {
        if (millis() - lastWifiAttempt > WIFI_RETRY_INTERVAL_MS) {
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
            // Start sACN protocol if enabled
            if (config.sacnEnabled) {
                lume::sacnProtocol.configure(config.sacnUniverse, config.sacnUniverseCount,
                                              config.sacnUnicast, config.sacnStartChannel);
                lume::sacnProtocol.begin();
            }
            // MQTT will auto-reconnect in its update() cycle
        } else {
            LOG_WARN(LogTag::WIFI, "WiFi disconnected");
            lume::sacnProtocol.stop();
        }
    }
}
