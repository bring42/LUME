/**
 * scenes.cpp - Scene Management API implementation
 * 
 * Note: Scenes are deprecated in v2 architecture. They stored v1 API format.
 * Future implementation should store segment configurations directly.
 * This is kept for backward compatibility but marked for redesign.
 */

#include "scenes.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../lume.h"
#include <ArduinoJson.h>

// Request body buffer for async handling
static String sceneBodyBuffer;

// Helper: Apply v1 scene spec to segment 0 (temporary - needs v2 redesign)
static bool applyV1SceneSpec(const JsonDocument& spec) {
    lume::Segment* seg = lume::controller.getSegment(0);
    if (!seg) {
        seg = lume::controller.createFullStrip();
        if (!seg) return false;
    }
    
    // Apply basic properties to segment 0
    if (spec["power"].is<bool>()) {
        lume::controller.setPower(spec["power"].as<bool>());
    }
    if (spec["brightness"].is<int>()) {
        lume::controller.setBrightness(constrain(spec["brightness"].as<int>(), 0, 255));
    }
    if (spec["effect"].is<const char*>()) {
        seg->setEffect(spec["effect"].as<const char*>());
    }
    if (spec["speed"].is<int>()) {
        seg->setSpeed(constrain(spec["speed"].as<int>(), 1, 255));
    }
    
    // Colors
    if (spec["primaryColor"].is<JsonArrayConst>()) {
        JsonArrayConst arr = spec["primaryColor"].as<JsonArrayConst>();
        if (arr.size() >= 3) {
            seg->setPrimaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
        }
    }
    if (spec["secondaryColor"].is<JsonArrayConst>()) {
        JsonArrayConst arr = spec["secondaryColor"].as<JsonArrayConst>();
        if (arr.size() >= 3) {
            seg->setSecondaryColor(CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>()));
        }
    }
    
    return true;
}

void handleApiScenesGet(AsyncWebServerRequest* request) {
    JsonDocument doc;
    storage.listScenes(doc);
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiSceneGet(AsyncWebServerRequest* request) {
    // Extract ID from URL path
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    int slotId = path.substring(lastSlash + 1).toInt();
    
    Scene scene;
    if (storage.loadScene(slotId, scene) && !scene.isEmpty()) {
        JsonDocument doc;
        doc["id"] = slotId;
        doc["name"] = scene.name;
        doc["spec"] = scene.jsonSpec;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    } else {
        request->send(404, "application/json", "{\"error\":\"Scene not found\"}");
    }
}

void handleApiScenePost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Accumulate body data
    if (index == 0) {
        sceneBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    for (size_t i = 0; i < len; i++) {
        sceneBodyBuffer += (char)data[i];
    }
    
    // Process when complete
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, sceneBodyBuffer);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        String name = doc["name"] | "";
        String spec = doc["spec"] | "";
        
        if (name.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"Scene name required\"}");
            return;
        }
        
        if (spec.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"Scene spec required\"}");
            return;
        }
        
        // Find empty slot or slot specified by 'id'
        int slot = -1;
        if (doc["id"].is<int>()) {
            slot = doc["id"].as<int>();
            if (slot < 0 || slot >= MAX_SCENES) {
                request->send(400, "application/json", "{\"error\":\"Invalid slot ID\"}");
                return;
            }
        } else {
            // Find first empty slot
            for (int i = 0; i < MAX_SCENES; i++) {
                Scene existing;
                if (!storage.loadScene(i, existing) || existing.isEmpty()) {
                    slot = i;
                    break;
                }
            }
            
            if (slot < 0) {
                request->send(400, "application/json", "{\"error\":\"No empty slots. Delete a scene first.\"}");
                return;
            }
        }
        
        Scene scene;
        scene.name = name;
        scene.jsonSpec = spec;
        
        if (storage.saveScene(slot, scene)) {
            JsonDocument response;
            response["success"] = true;
            response["id"] = slot;
            response["name"] = name;
            
            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save scene\"}");
        }
    }
}

void handleApiSceneDelete(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Extract ID from URL path
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    int slotId = path.substring(lastSlash + 1).toInt();
    
    if (slotId < 0 || slotId >= MAX_SCENES) {
        request->send(400, "application/json", "{\"error\":\"Invalid slot ID\"}");
        return;
    }
    
    if (storage.deleteScene(slotId)) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to delete scene\"}");
    }
}

void handleApiSceneApply(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Extract ID from URL path  /api/scenes/{id}/apply
    String path = request->url();
    
    // Remove "/apply" from end
    path = path.substring(0, path.lastIndexOf('/'));
    int lastSlash = path.lastIndexOf('/');
    int slotId = path.substring(lastSlash + 1).toInt();
    
    Scene scene;
    if (!storage.loadScene(slotId, scene) || scene.isEmpty()) {
        request->send(404, "application/json", "{\"error\":\"Scene not found\"}");
        return;
    }
    
    // Parse and apply the scene spec
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, scene.jsonSpec);
    
    if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid scene spec\"}");
        return;
    }
    
    String errorMsg;
    if (!applyV1SceneSpec(doc)) {
        request->send(500, "application/json", "{\"error\":\"Failed to apply scene\"}");
        return;
    }
    
    // Save the new LED state
    // TODO: v2 should save segment configurations, not v1 format
    request->send(200, "application/json", "{\"success\":true}");
}
