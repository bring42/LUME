/**
 * prompt.h - AI prompt API handlers
 */

#pragma once

#include <ESPAsyncWebServer.h>

// AI prompt API handlers
void handleApiPromptPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
