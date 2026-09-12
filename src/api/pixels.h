#pragma once

#include <ESPAsyncWebServer.h>

// Direct pixel control - set individual LED colors via API
void handleApiPixels(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// GET /api/v2/pixels — live readback of the strip's current perceptual output
// (the channel viz mirrors the real strip through this; zero per-effect coupling)
void handleApiPixelsGet(AsyncWebServerRequest* request);
