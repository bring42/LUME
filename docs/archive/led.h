#pragma once

#include <ESPAsyncWebServer.h>

// LED state control - backward compatibility with v1 API
void handleApiLed(AsyncWebServerRequest* request);
void handleApiLedPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
