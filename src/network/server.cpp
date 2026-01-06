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
#include "../api/pixels.h"
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

static void appendColorArray(JsonArray& arr, const CRGB& color) {
    arr.add(color.r);
    arr.add(color.g);
    arr.add(color.b);
}

static void buildControllerState(JsonDocument& doc) {
    doc["type"] = "state";

    JsonObject controllerJson = doc["controller"].to<JsonObject>();
    controllerJson["power"] = lume::controller.getPower();
    controllerJson["brightness"] = lume::controller.getBrightness();
    controllerJson["ledCount"] = lume::controller.getLedCount();

    JsonArray segmentsArr = doc["segments"].to<JsonArray>();
    uint8_t segCount = lume::controller.getSegmentCount();
    for (uint8_t i = 0; i < segCount; i++) {
        lume::Segment* seg = lume::controller.getSegment(i);
        if (!seg) {
            continue;
        }

        JsonObject segObj = segmentsArr.add<JsonObject>();
        segObj["id"] = seg->getId();
        segObj["start"] = seg->getStart();
        segObj["length"] = seg->getLength();
        segObj["reverse"] = seg->isReversed();
        segObj["effect"] = seg->getEffectId();

        // Serialize schema-based params if effect has schema
        const lume::EffectInfo* effectInfo = seg->getEffect();
        if (effectInfo && effectInfo->hasSchema()) {
            const lume::ParamSchema* schema = effectInfo->schema;
            const lume::ParamValues& paramValues = seg->getParamValues();
            
            JsonObject paramsObj = segObj["params"].to<JsonObject>();
            for (uint8_t i = 0; i < schema->count && i < lume::MAX_EFFECT_PARAMS; i++) {
                const lume::ParamDesc& desc = schema->params[i];
                
                switch (desc.type) {
                    case lume::ParamType::Int:
                        paramsObj[desc.id] = paramValues.getInt(i);
                        break;
                    case lume::ParamType::Float:
                        paramsObj[desc.id] = paramValues.getFloat(i);
                        break;
                    case lume::ParamType::Color: {
                        CRGB c = paramValues.getColor(i);
                        JsonArray colorArr = paramsObj[desc.id].to<JsonArray>();
                        colorArr.add(c.r);
                        colorArr.add(c.g);
                        colorArr.add(c.b);
                        break;
                    }
                    case lume::ParamType::Bool:
                        paramsObj[desc.id] = paramValues.getBool(i);
                        break;
                    case lume::ParamType::Enum:
                        paramsObj[desc.id] = paramValues.getEnum(i);
                        break;
                    case lume::ParamType::Palette:
                        break;
                }
            }
        }
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
            .setCacheControl("public, max-age=604800");
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
        components["storage"] = true;  // Would fail at boot if broken
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
    
    // Segment management endpoints (v2 architecture)
    server.on("/api/segments", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        // Global state
        doc["power"] = lume::controller.getPower();
        doc["brightness"] = lume::controller.getBrightness();
        doc["ledCount"] = lume::controller.getLedCount();
        
        // List all segments
        JsonArray segArr = doc["segments"].to<JsonArray>();
        uint8_t segCount = lume::controller.getSegmentCount();
        
        for (uint8_t i = 0; i < segCount; i++) {
            lume::Segment* seg = lume::controller.getSegment(i);
            if (!seg) continue;
            
            JsonObject segObj = segArr.add<JsonObject>();
            segObj["id"] = seg->getId();
            segObj["start"] = seg->getStart();
            segObj["length"] = seg->getLength();
            segObj["reverse"] = seg->isReversed();
            segObj["brightness"] = seg->getBrightness();
            
            // Effect info
            JsonObject effectObj = segObj["effect"].to<JsonObject>();
            effectObj["id"] = seg->getEffectId();
            effectObj["name"] = seg->getEffectName();
            if (seg->getEffect()) {
                effectObj["category"] = seg->getEffect()->categoryName();
            }
            
            // Schema-based params
            const lume::EffectInfo* effectInfo = seg->getEffect();
            if (effectInfo && effectInfo->hasSchema()) {
                const lume::ParamSchema* schema = effectInfo->schema;
                const lume::ParamValues& paramValues = seg->getParamValues();
                
                JsonObject paramsObj = segObj["params"].to<JsonObject>();
                for (uint8_t i = 0; i < schema->count && i < lume::MAX_EFFECT_PARAMS; i++) {
                    const lume::ParamDesc& desc = schema->params[i];
                    
                    switch (desc.type) {
                        case lume::ParamType::Int:
                            paramsObj[desc.id] = paramValues.getInt(i);
                            break;
                        case lume::ParamType::Float:
                            paramsObj[desc.id] = paramValues.getFloat(i);
                            break;
                        case lume::ParamType::Color: {
                            CRGB c = paramValues.getColor(i);
                            char hex[8];
                            snprintf(hex, sizeof(hex), "#%02x%02x%02x", c.r, c.g, c.b);
                            paramsObj[desc.id] = hex;
                            break;
                        }
                        case lume::ParamType::Bool:
                            paramsObj[desc.id] = paramValues.getBool(i);
                            break;
                        case lume::ParamType::Enum:
                            paramsObj[desc.id] = paramValues.getEnum(i);
                            break;
                        case lume::ParamType::Palette:
                            break;
                    }
                }
            }
            
            // Capabilities - derived from effect schema
            JsonObject capsObj = segObj["capabilities"].to<JsonObject>();
            const lume::EffectInfo* effect = seg->getEffect();
            if (effect) {
                capsObj["hasSpeed"] = effect->hasParam("speed");
                capsObj["hasIntensity"] = effect->hasParam("intensity");
                capsObj["hasPalette"] = effect->usesPalette();
                capsObj["colorCount"] = effect->colorCount();
            } else {
                capsObj["hasSpeed"] = false;
                capsObj["hasIntensity"] = false;
                capsObj["hasPalette"] = false;
                capsObj["colorCount"] = 0;
            }
        }
        
        // Available effects
        JsonArray effectsArr = doc["effects"].to<JsonArray>();
        auto& registry = lume::effects();
        for (uint8_t i = 0; i < registry.getCount(); i++) {
            const lume::EffectInfo* info = registry.getByIndex(i);
            if (!info) continue;
            
            JsonObject effObj = effectsArr.add<JsonObject>();
            effObj["id"] = info->id;
            effObj["name"] = info->displayName;
            effObj["category"] = info->categoryName();  // Use helper method
            effObj["usesSpeed"] = info->usesSpeed;
            effObj["usesIntensity"] = info->usesIntensity;
            effObj["usesPalette"] = info->usesPalette;
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
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
        lume::controller.stopNightlight();
        request->send(200, "application/json", "{\"success\":true}");
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
