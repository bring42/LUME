#pragma once

#include <ESPAsyncWebServer.h>

// Pull-based firmware auto-update API (see src/network/updater.*).
//
//   POST /api/firmware/check       -> 202; async GitHub check (both images).
//   GET  /api/firmware/status      -> 200; updater state + last check result.
//   POST /api/firmware/update/app  -> 202; flash the firmware image only (async).
//   POST /api/firmware/update/fs   -> 202; flash the filesystem image only (async).
//
// A single check reports availability for BOTH the app and fs images, but the
// apply step is split into two fully independent operations — flashing one never
// triggers the other. Check/update are asynchronous because they do a blocking
// HTTPS transfer that must not run on the AsyncTCP task (same rationale as
// /api/prompt). The UI triggers, then polls /status (whose "stage" field says
// which target, if any, is in progress).

void handleApiFirmwareCheck(AsyncWebServerRequest* request);
void handleApiFirmwareStatus(AsyncWebServerRequest* request);
void handleApiFirmwareUpdateApp(AsyncWebServerRequest* request);
void handleApiFirmwareUpdateFs(AsyncWebServerRequest* request);
