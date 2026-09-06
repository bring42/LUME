#include "wifi.h"
#include "ota.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../protocols/sacn.h"
#include "../protocols/mqtt.h"
#include <WiFi.h>
#include <atomic>

// External globals
extern Config config;
extern bool wifiConnected;
extern unsigned long lastWifiAttempt;

// One-shot STA connect request, set from the web task after a credential save
// and consumed on the loop task. Atomic so the flag (and, via its ordering, the
// config Strings written before it) is safely visible across tasks.
static std::atomic<bool> staConnectRequested{false};

void requestWifiConnect() {
    staConnectRequested.store(true);
}

// Access Point settings
#define AP_SSID "LUME-Setup"
#define AP_PASSWORD "ledcontrol"

// --- WiFi observability -----------------------------------------------------
// The IDF reports exactly why a connection attempt failed; without these logs
// the firmware swallowed every outcome and a failing link just looked idle.
// Pure logging: no state is mutated here (connect edges are still handled by
// the handleWifiMaintenance() poll on the loop task). Runs on the WiFi/event
// task — keep it to log lines only.

// Human-readable names for the disconnect reasons we actually see in the field.
static const char* wifiReasonName(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:            return "AUTH_EXPIRE";
        case WIFI_REASON_AUTH_LEAVE:             return "AUTH_LEAVE";
        case WIFI_REASON_ASSOC_EXPIRE:           return "ASSOC_EXPIRE";
        case WIFI_REASON_ASSOC_LEAVE:            return "ASSOC_LEAVE";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT (wrong password or weak signal)";
        case WIFI_REASON_BEACON_TIMEOUT:         return "BEACON_TIMEOUT (lost the AP / weak signal)";
        case WIFI_REASON_NO_AP_FOUND:            return "NO_AP_FOUND (SSID not visible from here)";
        case WIFI_REASON_AUTH_FAIL:              return "AUTH_FAIL (wrong password)";
        case WIFI_REASON_ASSOC_FAIL:             return "ASSOC_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:      return "HANDSHAKE_TIMEOUT";
        case WIFI_REASON_CONNECTION_FAIL:        return "CONNECTION_FAIL";
        default:                                 return "(see esp_wifi_types.h)";
    }
}

static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            LOG_INFO(LogTag::WIFI, "STA started");
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            LOG_INFO(LogTag::WIFI, "Associated with AP (channel %u) — waiting for IP",
                     (unsigned)info.wifi_sta_connected.channel);
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            LOG_INFO(LogTag::WIFI, "Got IP: %s (RSSI %d dBm)",
                     IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
                     (int)WiFi.RSSI());
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            LOG_WARN(LogTag::WIFI, "Lost IP (DHCP lease gone)");
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            LOG_WARN(LogTag::WIFI, "STA disconnected: reason %u %s",
                     (unsigned)info.wifi_sta_disconnected.reason,
                     wifiReasonName(info.wifi_sta_disconnected.reason));
            break;
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            LOG_INFO(LogTag::WIFI, "Setup-AP client joined (%u client(s)) — STA retries pause while any client is parked here",
                     (unsigned)WiFi.softAPgetStationNum());
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            LOG_INFO(LogTag::WIFI, "Setup-AP client left (%u client(s) remain)",
                     (unsigned)WiFi.softAPgetStationNum());
            break;
        default:
            break;
    }
}

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
    // Observability first, so even the earliest events are captured.
    WiFi.onEvent(onWifiEvent);

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

// Diagnostic scan while the STA can't connect: every ~95 s, run an async scan
// and log every BSSID broadcasting the target SSID (channel, RSSI, auth mode).
// This is the device's own radio's view — it distinguishes "SSID not actually
// visible from here" / "signal too weak" / "wrong auth mode" / "mesh node that
// beacons but won't answer auth" in a way no router UI can. Same SoftAP-client
// guard as the reconnect: a scan channel-hops the AP off the air (see #40).
static const char* wifiAuthModeName(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2_WPA3_PSK";
        default:                        return "?";
    }
}

static void handleWifiDiagnosticScan() {
    static unsigned long lastScanStart = 0;
    static bool scanPending = false;

    if (scanPending) {
        int16_t n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        scanPending = false;
        if (n < 0) { LOG_WARN(LogTag::WIFI, "Diagnostic scan failed (%d)", n); return; }
        uint8_t matches = 0;
        for (int16_t i = 0; i < n; i++) {
            if (WiFi.SSID(i) == config.wifiSSID) {
                matches++;
                LOG_INFO(LogTag::WIFI, "  %s: bssid %s ch %d rssi %d dBm auth %s",
                         config.wifiSSID.c_str(), WiFi.BSSIDstr(i).c_str(),
                         (int)WiFi.channel(i), (int)WiFi.RSSI(i),
                         wifiAuthModeName(WiFi.encryptionType(i)));
            }
        }
        LOG_INFO(LogTag::WIFI, "Diagnostic scan: %d network(s) visible, %u broadcasting \"%s\"",
                 (int)n, (unsigned)matches, config.wifiSSID.c_str());
        if (matches == 0) {
            // The target wasn't found — log what the radio CAN hear. This is the
            // line that separates "the AP is gone/renamed" (many networks listed,
            // just not ours) from "this radio is deaf" (one or two weak entries,
            // e.g. the external u.FL antenna knocked off), which the match-only
            // log above cannot distinguish. Capped so a dense band can't spam.
            const int16_t kMaxListed = 8;
            for (int16_t i = 0; i < n && i < kMaxListed; i++) {
                LOG_INFO(LogTag::WIFI, "  heard: \"%s\" ch %d rssi %d dBm",
                         WiFi.SSID(i).c_str(), (int)WiFi.channel(i), (int)WiFi.RSSI(i));
            }
        }
        WiFi.scanDelete();
        return;
    }

    if (!wifiConnected && config.wifiSSID.length() > 0 &&
        WiFi.softAPgetStationNum() == 0 &&
        millis() - lastScanStart > 95000) {
        // Offset from the 30 s reconnect cadence so the scan and a begin() rarely
        // collide (a collision just aborts the scan — retried next cycle).
        lastScanStart = millis();
        scanPending = true;
        LOG_INFO(LogTag::WIFI, "Starting diagnostic scan for \"%s\"...", config.wifiSSID.c_str());
        WiFi.scanNetworks(true /*async*/, true /*include hidden*/);
    }
}

// Helper function for WiFi reconnection and status monitoring
void handleWifiMaintenance() {
    handleWifiDiagnosticScan();
    // User-initiated connect: WiFi credentials were just saved. Fire immediately,
    // even with a client parked on the SoftAP — this is the one scan provisioning
    // NEEDS. The brief AP blip is deliberate; the alternative (waiting for the
    // phone to leave the AP before ever trying) is a setup flow that never
    // visibly completes. disconnect() first so a switch away from a currently
    // connected network takes effect too.
    if (staConnectRequested.exchange(false) && config.wifiSSID.length() > 0) {
        LOG_INFO(LogTag::WIFI, "Credentials changed; connecting to %s", config.wifiSSID.c_str());
        WiFi.disconnect();
        WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());
        lastWifiAttempt = millis();
    }

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
            LOG_INFO(LogTag::WIFI, "Attempting WiFi reconnection to %s...",
                     config.wifiSSID.c_str());
            WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());
        }
    } else if (!wifiConnected && config.wifiSSID.length() > 0 &&
               millis() - lastWifiAttempt > WIFI_RETRY_INTERVAL_MS) {
        // Retry is due but deliberately held off: a client is parked on the
        // setup AP, and a STA scan would channel-hop it off the air (see the
        // block comment above). Was previously silent — the #1 "it just won't
        // connect and says nothing" trap. Log it, rate-limited to the retry
        // interval. lastWifiAttempt is intentionally NOT reset here, so the
        // retry still fires the moment the AP client leaves.
        static unsigned long lastSkipLog = 0;
        if (millis() - lastSkipLog > WIFI_RETRY_INTERVAL_MS) {
            lastSkipLog = millis();
            LOG_INFO(LogTag::WIFI,
                     "Reconnect to %s is due but paused: %u client(s) on the setup AP (a scan would drop them)",
                     config.wifiSSID.c_str(), (unsigned)WiFi.softAPgetStationNum());
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
