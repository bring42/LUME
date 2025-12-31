/**
 * nightlight.h - Nightlight API handlers
 */

#pragma once

#include <ESPAsyncWebServer.h>

// Nightlight API handlers
void handleApiNightlightGet(AsyncWebServerRequest* request);
void handleApiNightlightPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
