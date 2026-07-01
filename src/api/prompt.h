/**
 * prompt.h - AI prompt API handlers
 */

#pragma once

#include <ESPAsyncWebServer.h>

// AI prompt API handlers
void handleApiPromptPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// Create the background AI worker task + queue (call once from setup()). The
// blocking Anthropic call runs there, not on the AsyncTCP task (P0.4).
void initAiPromptWorker();
