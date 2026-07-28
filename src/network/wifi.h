#pragma once

// Setup WiFi in AP+STA mode
void setupWiFi();

// WiFi reconnection and status monitoring (call from main loop)
void handleWifiMaintenance();

// Ask the loop task to (re)connect the station with the current credentials in
// `config` on its next handleWifiMaintenance() pass. Safe to call from the web
// task — it only sets a flag; all radio work stays on the loop task. Used after
// a config save changes the WiFi credentials, so provisioning connects NOW
// instead of waiting for a retry that never fires while the provisioning phone
// is parked on the SoftAP.
void requestWifiConnect();

// Re-apply sACN/MQTT config from the persisted global config. MUST run on the
// loop task (registered as controller.reconfigureProtocolsFn). See P0.8.
void applyProtocolConfig();
