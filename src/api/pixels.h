#pragma once

#include <ESPAsyncWebServer.h>

// Direct pixel control - set individual LED colors via API
void handleApiPixels(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
