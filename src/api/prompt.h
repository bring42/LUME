/**
 * prompt.h - AI prompt API handlers
 */

#pragma once

#include <ESPAsyncWebServer.h>

// AI prompt API handlers
void handleApiPromptPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// GET /api/prompt/status — last worker outcome, so the UI can report the real
// result of an accepted (202) prompt instead of assuming success.
void handleApiPromptStatus(AsyncWebServerRequest* request);

// Create the background AI worker task + queue (call once from setup()). The
// blocking Anthropic call runs there, not on the AsyncTCP task (P0.4).
void initAiPromptWorker();
