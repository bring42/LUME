/**
 * prompt.h - AI Prompt API handlers
 */

#pragma once

#include <ESPAsyncWebServer.h>

// AI Prompt API handlers
void handleApiPrompt(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiPromptStatus(AsyncWebServerRequest* request);
void handleApiPromptApply(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
