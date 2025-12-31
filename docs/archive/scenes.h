/**
 * scenes.h - Scene Management API handlers
 */

#pragma once

#include <ESPAsyncWebServer.h>

// Scene Management API handlers
void handleApiScenesGet(AsyncWebServerRequest* request);
void handleApiSceneGet(AsyncWebServerRequest* request);
void handleApiScenePost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiSceneDelete(AsyncWebServerRequest* request);
void handleApiSceneApply(AsyncWebServerRequest* request);
