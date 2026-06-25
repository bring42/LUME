#include "ota.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../lume.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

// External globals
extern Config config;

// Access Point password for OTA fallback
#define AP_PASSWORD "ledcontrol"

void setupOTA() {
    // Only enable OTA if WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        LOG_DEBUG(LogTag::OTA, "Waiting for WiFi connection");
        return;
    }

    // Idempotent: mDNS/OTA only need registering once. Guards against duplicate
    // MDNS.addService() calls (which log "Failed adding service") when WiFi
    // reconnects or this is reached more than once during boot.
    static bool otaStarted = false;
    if (otaStarted) {
        return;
    }

    // Start mDNS service for OTA discovery and easy access
    if (!MDNS.begin(MDNS_HOSTNAME)) {
        LOG_ERROR(LogTag::OTA, "Error starting mDNS");
        return;
    }
    // Add HTTP service for service discovery
    MDNS.addService("http", "tcp", 80);
    LOG_INFO(LogTag::OTA, "mDNS started: %s.local", MDNS_HOSTNAME);
    
    // Set OTA hostname
    ArduinoOTA.setHostname(MDNS_HOSTNAME);
    
    // Set OTA port (default is 3232)
    ArduinoOTA.setPort(3232);
    
    // Set OTA password (use auth token if set, otherwise fall back to AP password)
    if (config.authToken.length() > 0) {
        ArduinoOTA.setPassword(config.authToken.c_str());
    } else {
        ArduinoOTA.setPassword(AP_PASSWORD);
    }
    
    // OTA callbacks for status updates
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_SPIFFS
            type = "filesystem";
        }
        LOG_INFO(LogTag::OTA, "Starting update (%s)", type.c_str());
        // Disable watchdog during OTA (can take >30s)
        esp_task_wdt_delete(NULL);
        // Turn off LEDs during update
        lume::controller.setPower(false);
    });
    
    ArduinoOTA.onEnd([]() {
        LOG_INFO(LogTag::OTA, "Update complete!");
        // Re-enable watchdog after OTA
        esp_task_wdt_add(NULL);
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t lastPercent = 255;
        uint8_t percent = progress / (total / 100);
        if (percent != lastPercent) {
            lastPercent = percent;
            Serial.printf("OTA Progress: %u%%\r", percent);  // Keep inline progress
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        const char* errStr = "Unknown";
        if (error == OTA_AUTH_ERROR) errStr = "Auth Failed";
        else if (error == OTA_BEGIN_ERROR) errStr = "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) errStr = "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) errStr = "Receive Failed";
        else if (error == OTA_END_ERROR) errStr = "End Failed";
        LOG_ERROR(LogTag::OTA, "Error[%u]: %s", error, errStr);
        // Re-enable watchdog after OTA error
        esp_task_wdt_add(NULL);
    });
    
    ArduinoOTA.begin();
    // ArduinoOTA.begin() already advertises the _arduino._tcp service over mDNS,
    // so adding it again here is redundant (and logged "Failed adding service").

    otaStarted = true;  // mark success only after full setup
    LOG_INFO(LogTag::OTA, "Ready (Hostname: lume.local, Port: 3232)");
}
