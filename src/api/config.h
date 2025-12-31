#pragma once

#include <ESPAsyncWebServer.h>

// System configuration management
void handleApiConfig(AsyncWebServerRequest* request);
void handleApiConfigPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
