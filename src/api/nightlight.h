/**
 * nightlight.h - Nightlight API handlers
 */

#pragma once

#include <cstddef>
#include <cstdint>

// Forward declaration
class AsyncWebServerRequest;

// Nightlight API handlers
void handleApiNightlightGet(AsyncWebServerRequest* request);
void handleApiNightlightPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
