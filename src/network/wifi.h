#pragma once

// Setup WiFi in AP+STA mode
void setupWiFi();

// WiFi reconnection and status monitoring (call from main loop)
void handleWifiMaintenance();

// Re-apply sACN/MQTT config from the persisted global config. MUST run on the
// loop task (registered as controller.reconfigureProtocolsFn). See P0.8.
void applyProtocolConfig();
