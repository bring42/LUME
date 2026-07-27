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

// Run each time WiFi comes up (initial connect or reconnect): start OTA/mDNS
// and (re)configure protocols that need the network. setupOTA() is idempotent,
// so calling this again on reconnect won't re-register mDNS services.
// Apply sACN + MQTT live config from the persisted global `config`. MUST run on
// the loop task — called at boot (onWifiConnected) and from the
// ReconfigureProtocols command handler. Never call from the web task: it opens/
// closes sockets and swaps the MQTT config struct that processProtocols()/
// mqtt.update() read every frame (P0.8).
void applyProtocolConfig() {
    // sACN
    if (config.sacnEnabled && wifiConnected) {
        lume::sacnProtocol.stop();
        lume::sacnProtocol.configure(config.sacnUniverse, config.sacnUniverseCount,
                                     config.sacnUnicast, config.sacnStartChannel);
        lume::sacnProtocol.begin();
    } else {
        lume::sacnProtocol.stop();
    }

    // MQTT — setConfig only swaps the struct; (re)connect happens in mqtt.update().
    // The controller pointer was retained by the boot-time mqtt.begin().
    if (config.mqttEnabled && config.mqttBroker.length() > 0 && wifiConnected) {
        lume::MqttConfig mqttConfig;
        mqttConfig.enabled = true;
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
}

static void onWifiConnected() {
    LOG_INFO(LogTag::WIFI, "Connected! IP: %s", WiFi.localIP().toString().c_str());
    setupOTA();
    applyProtocolConfig();
}

void setupWiFi() {
    // Always start AP mode for initial access
    WiFi.mode(WIFI_AP_STA);
    // No modem power-save: this is a mains-powered controller, and WiFi sleep makes
    // the SoftAP + web server sluggish/unreliable (slow loads, dropped connections).
    WiFi.setSleep(false);

    // Own STA (re)connection entirely from handleWifiMaintenance(). The IDF's built-in
    // auto-reconnect scans the single radio behind our back, and every scan channel-hops
    // the SoftAP off its channel — which kills a provisioning client's DHCP handshake
    // (the "169.254 link-local / jumping addresses" symptom when the saved network is
    // out of range). persistent(false) also stops the IDF from auto-connecting a stale
    // SSID out of its own NVS before we decide to; our creds live in Preferences.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    // Start Access Point
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    LOG_INFO(LogTag::WIFI, "AP started: %s", AP_SSID);
    LOG_DEBUG(LogTag::WIFI, "AP IP: %s", WiFi.softAPIP().toString().c_str());
    
    // Kick off the station connect but DON'T block on it. The AP + web server must
    // be reachable immediately — otherwise 192.168.4.1 is dead for up to ~10s while
    // an unavailable saved network times out (the "moved the device somewhere new"
    // case). handleWifiMaintenance() runs every loop, detects the false->true
    // connect transition, and runs onWifiConnected() (mDNS/OTA/protocols) once the
    // link is actually up.
    if (config.wifiSSID.length() > 0) {
        LOG_INFO(LogTag::WIFI, "Connecting to WiFi in background: %s", config.wifiSSID.c_str());
        WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());
    } else {
        LOG_INFO(LogTag::WIFI, "No WiFi configured, AP mode only");
    }

    lastWifiAttempt = millis();
}

// Helper function for WiFi reconnection and status monitoring
void handleWifiMaintenance() {
    // WiFi reconnection logic. While a client is connected to the SoftAP, SKIP the
    // reconnect entirely: WiFi.begin() channel-hops the single radio to scan, which
    // drops the AP client mid-DHCP — the exact provisioning failure this addresses (an
    // unreachable saved network otherwise scans every retry and makes the setup AP
    // unusable). Once the client leaves (station count back to 0) the retry resumes, so
    // an idle phone parked on the AP can't wedge the device offline forever and the
    // link still self-heals when the saved network (or a freshly-provisioned one)
    // returns.
    if (!wifiConnected && config.wifiSSID.length() > 0 &&
        WiFi.softAPgetStationNum() == 0) {
        if (millis() - lastWifiAttempt > WIFI_RETRY_INTERVAL_MS) {
            lastWifiAttempt = millis();
            LOG_INFO(LogTag::WIFI, "Attempting WiFi reconnection...");
            WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());
        }
    }
    
    // Detect the STA connect as a false->true edge. Seed from the current
    // wifiConnected (false at boot, since setupWiFi() now connects in the
    // background), so the first real connect is caught here and onWifiConnected()
    // runs exactly once per edge (no duplicate mDNS/OTA init).
    static bool lastWifiState = wifiConnected;
    bool currentWifiState = (WiFi.status() == WL_CONNECTED);
    if (currentWifiState != lastWifiState) {
        lastWifiState = currentWifiState;
        wifiConnected = currentWifiState;
        if (currentWifiState) {
            // WiFi came back after a drop: restart OTA/protocols.
            onWifiConnected();
            // MQTT will auto-reconnect in its update() cycle
        } else {
            LOG_WARN(LogTag::WIFI, "WiFi disconnected");
            lume::sacnProtocol.stop();
        }
    }
}
