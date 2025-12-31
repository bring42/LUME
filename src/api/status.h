#pragma once

#include <ESPAsyncWebServer.h>

// Root route - serves main web UI
void handleRoot(AsyncWebServerRequest* request);

// System status endpoint - returns JSON with uptime, WiFi, memory, etc.
void handleApiStatus(AsyncWebServerRequest* request);
