#ifndef API_SEGMENTS_H
#define API_SEGMENTS_H

#include <ESPAsyncWebServer.h>

// ===========================================================================
// V2 Segment API - Multi-segment LED control
// ===========================================================================
// Modern API supporting multiple LED segments with independent effects
// Replaces the v1 single-strip /api/led compatibility layer

void handleApiV2SegmentsList(AsyncWebServerRequest* request);
void handleApiV2SegmentGet(AsyncWebServerRequest* request);
void handleApiV2SegmentCreate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiV2SegmentUpdate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void handleApiV2SegmentDelete(AsyncWebServerRequest* request);

void handleApiV2EffectsList(AsyncWebServerRequest* request);
void handleApiV2PalettesList(AsyncWebServerRequest* request);
void handleApiV2Info(AsyncWebServerRequest* request);

void handleApiV2ControllerGet(AsyncWebServerRequest* request);
void handleApiV2ControllerUpdate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

#endif // API_SEGMENTS_H
