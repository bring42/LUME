#include "server.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../lume.h"
#include "../storage.h"
#include "../protocols/sacn.h"
#include "../protocols/mqtt.h"
#include "../api/status.h"
#include "../api/config.h"
#include "../api/firmware.h"
#include "../core/segment_serializer.h"   // canonical serializeSegment (P1.7)
#include "../api/pixels.h"
#include "updater.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

// From api/nightlight.h
extern void handleApiNightlightGet(AsyncWebServerRequest* request);
extern void handleApiNightlightPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// From api/prompt.h
extern void handleApiPromptPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// From api/segments.h (v2 multi-segment API)
extern void handleApiV2SegmentsList(AsyncWebServerRequest* request);
extern void handleApiV2SegmentGet(AsyncWebServerRequest* request);
extern void handleApiV2SegmentCreate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
extern void handleApiV2SegmentUpdate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
extern void handleApiV2SegmentDelete(AsyncWebServerRequest* request);
extern void handleApiV2EffectsList(AsyncWebServerRequest* request);
extern void handleApiV2PalettesList(AsyncWebServerRequest* request);
extern void handleApiV2Info(AsyncWebServerRequest* request);
extern void handleApiV2ControllerGet(AsyncWebServerRequest* request);
extern void handleApiV2ControllerUpdate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// External globals
extern AsyncWebServer server;
extern Config config;
extern bool wifiConnected;
extern bool webUiAvailable;

static AsyncWebSocket ws("/ws");
static unsigned long lastWsBroadcast = 0;
constexpr uint32_t WS_BROADCAST_INTERVAL_MS = 1000;

static String contentTypeFromPath(const String& path) {
    if (path.endsWith(".html")) return "text/html; charset=utf-8";
    if (path.endsWith(".css")) return "text/css; charset=utf-8";
    if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".json")) return "application/json; charset=utf-8";
    if (path.endsWith(".txt")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static void buildControllerState(JsonDocument& doc) {
    doc["type"] = "state";

    JsonObject controllerJson = doc["controller"].to<JsonObject>();
    // Report the *target* power/brightness (where a fade is heading), not the
    // mid-fade values — otherwise this ~1Hz push snaps a just-moved slider/toggle
    // back to a transient level, then corrects (the flicker). See controller.h.
    controllerJson["power"] = lume::controller.getTargetPower();
    controllerJson["brightness"] = lume::controller.getTargetBrightness();
    controllerJson["ledCount"] = lume::controller.getLedCount();

    JsonArray segmentsArr = doc["segments"].to<JsonArray>();
    uint8_t segCount = lume::controller.getSegmentCount();
    for (uint8_t i = 0; i < segCount; i++) {
        lume::Segment* seg = lume::controller.getSegmentByIndex(i);
        if (!seg) {
            continue;
        }
        JsonObject segObj = segmentsArr.add<JsonObject>();
        lume::serializeSegment(segObj, seg);   // one canonical shape (P1.7)
    }
}

static bool buildUiStatePayload(String& payload) {
    JsonDocument doc;
    buildControllerState(doc);
    payload.clear();
    serializeJson(doc, payload);
    return payload.length() > 0;
}

static void sendStateToClient(AsyncWebSocketClient* client) {
    if (!client) {
        return;
    }
    String payload;
    if (buildUiStatePayload(payload)) {
        client->text(payload);
    }
}

static void broadcastUiState() {
    if (ws.count() == 0) {
        return;
    }
    String payload;
    if (buildUiStatePayload(payload)) {
        ws.textAll(payload);
    }
}

static void handleWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) {
        sendStateToClient(client);
    }
}

void setupServer() {
    ws.onEvent(handleWsEvent);
    server.addHandler(&ws);

    if (webUiAvailable) {
        server.serveStatic("/assets/", LittleFS, "/assets/")
            .setCacheControl("public, max-age=604800")
            // During an OTA filesystem flash the LittleFS partition is mid-erase.
            // Disable static serving then, so the request falls through to the
            // updaterInProgress() 503 guard in onNotFound instead of reading a
            // partition being actively overwritten (garbage/fault -> FS corruption).
            .setFilter([](AsyncWebServerRequest*) { return !lume::updaterInProgress(); });
        LOG_INFO(LogTag::WEB, "Serving UI assets from LittleFS");
    } else {
        LOG_WARN(LogTag::WEB, "LittleFS not mounted; UI assets unavailable");
    }

    // Serve main page
    server.on("/", HTTP_GET, handleRoot);
    
    // Health check endpoint - lightweight for monitoring
    server.on("/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        // Core health indicators
        doc["status"] = "healthy";
        doc["uptime"] = millis() / 1000;
        doc["version"] = FIRMWARE_VERSION;
        
        // Memory health
        JsonObject memory = doc["memory"].to<JsonObject>();
        memory["heap_free"] = ESP.getFreeHeap();
        memory["heap_min"] = ESP.getMinFreeHeap();
        memory["heap_max_block"] = ESP.getMaxAllocHeap();
        memory["psram_free"] = ESP.getFreePsram();
        
        // Calculate heap fragmentation percentage
        uint32_t heapFree = ESP.getFreeHeap();
        uint32_t maxBlock = ESP.getMaxAllocHeap();
        if (heapFree > 0) {
            memory["fragmentation"] = 100 - (maxBlock * 100 / heapFree);
        }
        
        // Network health
        JsonObject network = doc["network"].to<JsonObject>();
        network["wifi_connected"] = wifiConnected;
        network["wifi_rssi"] = wifiConnected ? WiFi.RSSI() : 0;
        network["ip"] = wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        network["ap_clients"] = WiFi.softAPgetStationNum();
        
        // Component health
        JsonObject components = doc["components"].to<JsonObject>();
        components["led_controller"] = lume::controller.getLedCount() > 0;
        components["storage"] = storage.isReady();
        components["sacn_enabled"] = config.sacnEnabled;
        components["sacn_receiving"] = lume::sacnProtocol.isActive();
        components["mqtt_enabled"] = config.mqttEnabled;
        components["mqtt_connected"] = lume::mqtt.isConnected();
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // API endpoints
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/config", HTTP_GET, handleApiConfig);
    
    // POST handlers with body
    server.on("/api/config", HTTP_POST, 
        [](AsyncWebServerRequest* request) {}, 
        NULL,
        handleApiConfigPost
    );
    
    // Direct pixel control endpoint
    server.on("/api/pixels", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiPixels
    );
    
    // Nightlight endpoints
    server.on("/api/nightlight", HTTP_GET, handleApiNightlightGet);
    server.on("/api/nightlight", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiNightlightPost
    );
    server.on("/api/nightlight/stop", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        // Stop = freeze the fade where it is: snap brightness to its current
        // (reconciled) value with no transition, which also clears the pending
        // power-off rider. "Nightlight" needs no dedicated command anymore.
        lume::controller.enqueueCommand(
            lume::Command::setGlobalBrightness(lume::controller.getBrightness()));
        request->send(202, "application/json", "{\"status\":\"accepted\"}");
    });
    
    // AI Prompt endpoint
    server.on("/api/prompt", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiPromptPost
    );
    
    // ===========================================================================
    // V2 API - Multi-segment LED control
    // ===========================================================================
    
    // Controller-level endpoints (power, brightness)
    server.on("/api/v2/controller", HTTP_GET, handleApiV2ControllerGet);
    server.on("/api/v2/controller", HTTP_PUT,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiV2ControllerUpdate
    );
    
    // Segment management endpoints - URL path inspection for {id} parameter
    // GET - Can be either /api/v2/segments (list) or /api/v2/segments/{id} (get one)
    server.on("/api/v2/segments", HTTP_GET, [](AsyncWebServerRequest* request) {
        String path = request->url();
        if (path.startsWith("/api/v2/segments/") && path.length() > 17) {
            handleApiV2SegmentGet(request);
        } else {
            handleApiV2SegmentsList(request);
        }
    });
    
    // POST - Create new segment (only /api/v2/segments, not with ID)
    server.on("/api/v2/segments", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiV2SegmentCreate
    );
    
    // PUT - Update existing segment /api/v2/segments/{id}
    server.on("/api/v2/segments", HTTP_PUT,
        [](AsyncWebServerRequest* request) {
            String path = request->url();
            if (!path.startsWith("/api/v2/segments/") || path.length() <= 17) {
                request->send(400, "application/json", "{\"error\":\"Segment ID required\"}");
            }
        },
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            String path = request->url();
            if (path.startsWith("/api/v2/segments/") && path.length() > 17) {
                handleApiV2SegmentUpdate(request, data, len, index, total);
            }
        }
    );
    
    // DELETE - Remove segment /api/v2/segments/{id}
    server.on("/api/v2/segments", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        String path = request->url();
        if (path.startsWith("/api/v2/segments/") && path.length() > 17) {
            handleApiV2SegmentDelete(request);
        } else {
            request->send(400, "application/json", "{\"error\":\"Segment ID required\"}");
        }
    });
    
    // Effects and palettes metadata
    server.on("/api/v2/effects", HTTP_GET, handleApiV2EffectsList);
    server.on("/api/v2/palettes", HTTP_GET, handleApiV2PalettesList);
    server.on("/api/v2/info", HTTP_GET, handleApiV2Info);

    // Firmware auto-update (pull-based OTA from GitHub Releases). Check/update
    // are async (worker does the blocking HTTPS transfer); the UI polls status.
    // Check is unified (reports both images); apply is split into two fully
    // independent operations (firmware vs filesystem) — flashing one never
    // triggers the other.
    server.on("/api/firmware/check", HTTP_POST, handleApiFirmwareCheck);
    server.on("/api/firmware/status", HTTP_GET, handleApiFirmwareStatus);
    server.on("/api/firmware/update/app", HTTP_POST, handleApiFirmwareUpdateApp);
    server.on("/api/firmware/update/fs", HTTP_POST, handleApiFirmwareUpdateFs);

    // ===========================================================================
    
    // Handle CORS preflight
    server.on("/api/*", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key");
        request->send(response);
    });
    
    // 404 handler with SPA fallback
    server.onNotFound([](AsyncWebServerRequest* request) {
        String path = request->url();
        if (path.startsWith("/api/")) {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
            return;
        }

        if (!webUiAvailable) {
            request->send(404, "text/plain", "Not found");
            return;
        }

        // Don't read LittleFS while an OTA is overwriting the FS partition.
        if (lume::updaterInProgress()) {
            request->send(503, "text/plain", "Firmware update in progress");
            return;
        }

        if (path.length() == 0) {
            path = "/";
        }
        if (!path.startsWith("/")) {
            path = "/" + path;
        }
        if (path.endsWith("/")) {
            path += "index.html";
        }

        if (LittleFS.exists(path)) {
            request->send(LittleFS, path, contentTypeFromPath(path));
            return;
        }

        // SPA fallback: serve index for client-side routes without extensions
        if (path.indexOf('.') < 0 && LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html; charset=utf-8");
            return;
        }

        request->send(404, "text/plain", "Not found");
    });
    
    // Start server
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    server.begin();
    LOG_INFO(LogTag::WEB, "Web server started on port 80");
}

void loopServer() {
    ws.cleanupClients();

    if (ws.count() == 0) {
        return;
    }

    unsigned long now = millis();
    if (now - lastWsBroadcast >= WS_BROADCAST_INTERVAL_MS) {
        broadcastUiState();
        lastWsBroadcast = now;
    }
}
