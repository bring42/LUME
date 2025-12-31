#pragma once

// Setup WiFi in AP+STA mode
void setupWiFi();

// WiFi reconnection and status monitoring (call from main loop)
void handleWifiMaintenance();
