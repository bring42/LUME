#include "config.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../lume.h"
#include "../network/wifi.h"

// External globals
extern Config config;
extern Storage storage;
extern bool wifiConnected;

// Static body buffer for async request handling
static String configBodyBuffer;

void handleApiConfig(AsyncWebServerRequest* request) {
    JsonDocument doc;
    storage.configToJson(config, doc, true);

    // The selectable LED hardware for THIS build/chip, so the UI never has to
    // hardcode the catalog: strip types (with the RGBW flag), colour orders and
    // the data pins this chip may drive (strapping pins flagged, not hidden).
    JsonObject options = doc["ledOptions"].to<JsonObject>();
    JsonArray types = options["types"].to<JsonArray>();
    size_t n;
    const lume::LedChipsetInfo* cat = lume::ledChipsetCatalog(n);
    for (size_t i = 0; i < n; i++) {
        JsonObject t = types.add<JsonObject>();
        t["id"] = cat[i].key;
        t["name"] = cat[i].name;
        if (cat[i].rgbw) t["rgbw"] = true;
    }
    JsonArray orders = options["colorOrders"].to<JsonArray>();
    for (uint8_t i = 0; i < (uint8_t)lume::LedColorOrder::COUNT; i++) {
        orders.add(lume::ledColorOrderKey((lume::LedColorOrder)i));
    }
    JsonArray pins = options["pins"].to<JsonArray>();
    JsonArray strapping = options["strappingPins"].to<JsonArray>();
    for (int pin = 0; pin < 64; pin++) {
        if (!lume::isUsableLedPin(pin)) continue;
        pins.add(pin);
        if (lume::isStrappingLedPin(pin)) strapping.add(pin);
    }
    // What the output driver is ACTUALLY bound to right now (differs from the
    // persisted values above until the next reboot) — lets the UI say so.
    const lume::LedHardware& active = lume::controller.getLedHardware();
    JsonObject act = doc["ledActive"].to<JsonObject>();
    act["ledType"] = lume::ledChipsetKey(active.chipset);
    act["ledColorOrder"] = lume::ledColorOrderKey(active.order);
    act["ledPin"] = active.pin;
    act["ledCount"] = lume::controller.getLedCount();

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
        
        // Update config. Track whether the WiFi credentials change so the loop
        // task can kick off a connect attempt with them right away — nothing
        // else triggers one, and the periodic retry is (correctly) suppressed
        // while the provisioning phone sits on the SoftAP.
        String prevWifiSsid = config.wifiSSID;
        String prevWifiPass = config.wifiPassword;
        String fieldError;
        if (!storage.configFromJson(config, doc, &fieldError)) {
            // Rejected before anything was applied (LED hardware validation).
            JsonDocument errDoc;
            errDoc["error"] = fieldError;
            String body;
            serializeJson(errDoc, body);
            request->send(400, "application/json", body);
            return;
        }
        bool wifiCredsChanged = (config.wifiSSID != prevWifiSsid ||
                                 config.wifiPassword != prevWifiPass);

        // Save to storage
        if (storage.saveConfig(config)) {
            // ledCount and the LED hardware (ledType / ledColorOrder / ledPin)
            // are NOT applied live: they're bound to FastLED at boot via
            // controller.begin(count, hardware) — FastLED's controller list has
            // no remove, and re-binding live raced the render loop (P0.8). They
            // take effect on the next reboot (POST /api/restart), from the
            // values just persisted above.

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

            // New WiFi credentials: have the loop task connect with them now.
            // Radio calls must not happen on this (AsyncTCP) task, and the
            // maintenance retry alone would never fire during provisioning
            // (client parked on the SoftAP suppresses it).
            if (wifiCredsChanged && config.wifiSSID.length() > 0) {
                requestWifiConnect();
            }

            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save\"}");
        }
    }
}

void handleApiRestart(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    // Reply first, reboot from the loop task: ESP.restart() on this (AsyncTCP)
    // task would tear the socket down under the response.
    request->send(202, "application/json", "{\"status\":\"restarting\"}");
    requestRestart();
}
