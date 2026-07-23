#include "config.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../lume.h"

// External globals
extern Config config;
extern Storage storage;
extern bool wifiConnected;

// Static body buffer for async request handling
static String configBodyBuffer;

void handleApiConfig(AsyncWebServerRequest* request) {
    JsonDocument doc;
    storage.configToJson(config, doc, true);
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiConfigPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        if (!beginBody(request)) {  // P0.3: one body at a time
            request->send(409, "application/json", "{\"error\":\"Busy, retry\"}");
            return;
        }
        configBodyBuffer = "";
        // Validate total size
        if (total > MAX_REQUEST_BODY_SIZE) {
            endBody(request);
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }

    // Length-aware: the chunk isn't NUL-terminated, so String((char*)data) would
    // strlen past `len` into adjacent memory (P0.9 over-read).
    configBodyBuffer += String((char*)data, len);

    if (index + len >= total) {
        endBody(request);   // P0.3: body fully assembled; release the slot
        // Body complete, process
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, configBodyBuffer);
        
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        // Update config
        storage.configFromJson(config, doc);
        
        // Save to storage
        if (storage.saveConfig(config)) {
            // ledCount is NOT applied live: it's bound to FastLED at boot via
            // controller.begin(config.ledCount), so changing it live raced the
            // render loop and never re-ran addLeds anyway (P0.8). It now takes
            // effect on the next reboot, from the value just persisted above.

            // sACN/MQTT are NOT reconfigured from this (AsyncTCP) task either —
            // stop()/setConfig() tore down sockets / swapped Strings that
            // processProtocols()/mqtt.update() read every frame (P0.8). Enqueue a
            // command; the loop re-applies the config (re-read from the global
            // `config` persisted above) on its own task — single writer.
            lume::controller.enqueueCommand(lume::Command::reconfigureProtocols());

            // Gamma IS applied live: unlike ledCount it doesn't touch FastLED
            // bindings, just the output-encode exponent read every frame. Push it
            // through the bus so the render loop (single writer) adopts the value
            // just persisted above — never mutate controller state from this task.
            lume::controller.enqueueCommand(lume::Command::setGamma(config.gamma));
            // Same story for dim-to-warm strength: live, via the bus.
            lume::controller.enqueueCommand(lume::Command::setWarmth(config.warmth));

            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save\"}");
        }
    }
}
