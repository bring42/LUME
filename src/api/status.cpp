#include "status.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../lume.h"
#include "../protocols/sacn.h"
#include "../protocols/mqtt.h"
#include "../network/updater.h"
#include <LittleFS.h>
#include <WiFi.h>

// External globals
extern Config config;
extern bool wifiConnected;
extern bool webUiAvailable;

void handleRoot(AsyncWebServerRequest* request) {
    // Don't read LittleFS while an OTA is overwriting the FS partition.
    if (lume::updaterInProgress()) {
        request->send(503, "text/plain", "Firmware update in progress");
        return;
    }
    if (webUiAvailable && LittleFS.exists("/index.html")) {
        AsyncWebServerResponse* response =
            request->beginResponse(LittleFS, "/index.html", "text/html; charset=utf-8");
        // no-cache: the page carries the ?v= cache-busted asset URLs, so a
        // cached copy would pin clients to an old asset set (the /assets/
        // files themselves are cached for a week by design).
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
        return;
    }
    request->send(503, "text/plain", "Web UI not available");
}

void handleApiStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    doc["version"] = FIRMWARE_VERSION;
    doc["buildHash"] = FIRMWARE_BUILD_HASH;
    doc["uptime"] = millis() / 1000;
    // `wifi` is an object — the shape API_V2.md documents and the web UI (and
    // the mock dev server) already read; the old string ("Connected"/"AP Mode")
    // never matched it. `ssid` is the *configured* SSID so the setup page can
    // prefill and show connect progress while the device is still AP-only.
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = wifiConnected;
    wifi["ssid"] = config.wifiSSID;
    // rssi only while connected: the UI treats its presence as "has signal"
    // (a literal 0 would render as a full-strength "0 dBm").
    if (wifiConnected) {
        wifi["rssi"] = WiFi.RSSI();
    }
    doc["ip"] = wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["heap"] = ESP.getFreeHeap();
    doc["ledCount"] = lume::controller.getLedCount();
    doc["power"] = lume::controller.getTargetPower();
    
    // sACN status (using new protocol system)
    JsonObject sacn = doc["sacn"].to<JsonObject>();
    sacn["enabled"] = config.sacnEnabled;
    sacn["universe"] = config.sacnUniverse;
    sacn["universeCount"] = config.sacnUniverseCount;
    sacn["startChannel"] = config.sacnStartChannel;
    sacn["unicast"] = config.sacnUnicast;
    sacn["receiving"] = lume::sacnProtocol.isActive();
    sacn["packets"] = lume::sacnProtocol.getPacketCount();
    sacn["source"] = lume::sacnProtocol.getActiveSourceName();
    sacn["priority"] = lume::sacnProtocol.getActivePriority();
    if (lume::sacnProtocol.isActive()) {
        sacn["lastPacketMs"] = millis() - lume::sacnProtocol.getLastPacketTime();
    }
    
    // MQTT status
    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["enabled"] = config.mqttEnabled;
    mqtt["broker"] = config.mqttBroker;
    mqtt["connected"] = lume::mqtt.isConnected();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}
