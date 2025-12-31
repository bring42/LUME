/**
 * prompt.cpp - AI Prompt API implementation
 */

#include "prompt.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../anthropic_client.h"
#include "../core/controller.h"
#include <ArduinoJson.h>

// Request body buffers for async handling
static String promptBodyBuffer;
static String applyBodyBuffer;

void handleApiPrompt(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        promptBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    // Rate limiting for prompt endpoint
    static unsigned long lastPromptRequest = 0;
    if (index == 0 && millis() - lastPromptRequest < PROMPT_RATE_LIMIT_MS) {
        request->send(429, "application/json", "{\"error\":\"Rate limited. Please wait before submitting another prompt.\"}");
        return;
    }
    
    promptBodyBuffer += String((char*)data).substring(0, len);
    
    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, promptBodyBuffer);
        
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        String prompt = doc["prompt"] | "";
        if (prompt.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"Missing prompt\"}");
            return;
        }
        
        // Check if job already running
        if (openRouterClient.isJobRunning()) {
            request->send(409, "application/json", "{\"error\":\"Job already running\"}");
            return;
        }
        
        // Get API key (from request or config)
        String apiKey = doc["apiKey"] | config.apiKey;
        if (apiKey.length() == 0 || apiKey.startsWith("****")) {
            apiKey = config.apiKey;
        }
        
        if (apiKey.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"API key not configured\"}");
            return;
        }
        
        // Build request
        PromptRequest req;
        req.prompt = prompt;
        req.apiKey = apiKey;
        req.model = doc["model"] | config.openRouterModel;
        
        // Include current LED state for context
        JsonDocument ledDoc;
        controllerStateToJson(ledDoc);  // v2 adapter
        serializeJson(ledDoc, req.currentLedStateJson);
        
        // Submit job
        if (openRouterClient.submitPrompt(req)) {
            lastPromptRequest = millis();  // Update rate limit timestamp
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Job started\"}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to start job\"}");
        }
    }
}

void handleApiPromptStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    PromptJobResult& result = openRouterClient.getJobResult();
    
    switch (result.state) {
        case PromptJobState::IDLE:    doc["state"] = "idle"; break;
        case PromptJobState::QUEUED:  doc["state"] = "queued"; break;
        case PromptJobState::RUNNING: doc["state"] = "running"; break;
        case PromptJobState::DONE:    doc["state"] = "done"; break;
        case PromptJobState::ERROR:   doc["state"] = "error"; break;
    }
    
    doc["message"] = result.message;
    
    // Debug info
    if (result.prompt.length() > 0) {
        doc["prompt"] = result.prompt;
    }
    if (result.rawResponse.length() > 0) {
        doc["rawResponse"] = result.rawResponse;
    }
    
    if (result.state == PromptJobState::DONE && result.effectSpec.length() > 0) {
        doc["lastSpec"] = result.effectSpec;
    }
    
    if (result.startTime > 0) {
        unsigned long elapsed = (result.endTime > 0 ? result.endTime : millis()) - result.startTime;
        doc["elapsed"] = elapsed;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiPromptApply(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    if (index == 0) {
        applyBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }
    
    // Properly append data with explicit length (safer than substring)
    for (size_t i = 0; i < len; i++) {
        applyBodyBuffer += (char)data[i];
    }
    
    if (index + len >= total) {
        LOG_DEBUG(LogTag::WEB, "Apply body received (%d bytes)", applyBodyBuffer.length());
        
        String specJson;
        
        // Check if spec provided in body
        if (applyBodyBuffer.length() > 2) {
            JsonDocument bodyDoc;
            DeserializationError err = deserializeJson(bodyDoc, applyBodyBuffer);
            
            if (err) {
                LOG_WARN(LogTag::WEB, "Failed to parse apply body: %s", err.c_str());
            }
            
            if (err == DeserializationError::Ok && bodyDoc["spec"].is<const char*>()) {
                specJson = bodyDoc["spec"].as<String>();
                LOG_DEBUG(LogTag::WEB, "Extracted spec from body (%d chars)", specJson.length());
            } else if (err == DeserializationError::Ok) {
                // Maybe spec is an object, not a string - try to serialize it
                if (bodyDoc["spec"].is<JsonObject>()) {
                    LOG_DEBUG(LogTag::WEB, "Spec is an object, serializing...");
                    serializeJson(bodyDoc["spec"], specJson);
                }
            }
        }
        
        // If no spec in body, use last generated
        if (specJson.length() == 0) {
            PromptJobResult& result = openRouterClient.getJobResult();
            if (result.state == PromptJobState::DONE && result.effectSpec.length() > 0) {
                specJson = result.effectSpec;
            }
        }
        
        if (specJson.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"No effect specification to apply\"}");
            return;
        }
        
        // Parse and apply
        JsonDocument specDoc;
        DeserializationError err = deserializeJson(specDoc, specJson);
        
        if (err) {
            LOG_WARN(LogTag::LED, "Failed to parse spec JSON: %s", err.c_str());
            request->send(400, "application/json", "{\"error\":\"Invalid effect specification\"}");
            return;
        }
        
        LOG_DEBUG(LogTag::LED, "Attempting to apply effect spec");
        if (LOG_LEVEL <= LogLevel::DEBUG) {
            serializeJsonPretty(specDoc, Serial);
            Serial.println();
        }
        
        String errorMsg;
        if (applyEffectSpec(specDoc, errorMsg)) {  // Use local adapter function
            // Save to storage
            PromptSpec spec;
            spec.jsonSpec = specJson;
            spec.timestamp = millis();
            spec.valid = true;
            storage.savePromptSpec(spec);
            
            // Also save LED state
            JsonDocument saveDoc;
            controllerStateToJson(saveDoc);
            storage.saveLedState(saveDoc);
            
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            LOG_WARN(LogTag::LED, "Failed to apply effect: %s", errorMsg.c_str());
            String error = "{\"error\":\"" + errorMsg + "\"}";
            request->send(400, "application/json", error);
        }
    }
}
