#pragma once

#include <ESPAsyncWebServer.h>

// Pull-based firmware auto-update API (see src/network/updater.*).
//
//   POST /api/firmware/check       -> 202; async GitHub check (both images).
//   GET  /api/firmware/status      -> 200; updater state + last check result.
//   POST /api/firmware/update      -> 202; ATOMIC: flash whatever is behind
//                                    (filesystem + firmware), one reboot. UI path.
//   POST /api/firmware/update/app  -> 202; flash the firmware image only (recovery).
//   POST /api/firmware/update/fs   -> 202; flash the filesystem image only (recovery).
//
// A single check reports whether the app and/or fs are behind. The UI drives the
// atomic /update (app and fs are one versioned release, so they update together
// and can't half-update). The per-image endpoints remain for recovery/debug.
// Check/update are asynchronous because they do a blocking HTTPS transfer that
// must not run on the AsyncTCP task (same rationale as /api/prompt). The UI
// triggers, then polls /status (whose "stage" field says which target is active).

void handleApiFirmwareCheck(AsyncWebServerRequest* request);
void handleApiFirmwareStatus(AsyncWebServerRequest* request);
void handleApiFirmwareUpdate(AsyncWebServerRequest* request);
void handleApiFirmwareUpdateApp(AsyncWebServerRequest* request);
void handleApiFirmwareUpdateFs(AsyncWebServerRequest* request);
