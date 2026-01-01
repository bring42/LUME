/**
 * prompt.cpp - AI prompt API implementation using Anthropic Claude
 */

#include "prompt.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../core/controller.h"
#include "../core/effect_registry.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Request body buffer for async handling
static String promptBodyBuffer;

extern Config config;
extern bool checkAuth(AsyncWebServerRequest* request);
extern void sendUnauthorized(AsyncWebServerRequest* request);

namespace {

// Anthropic API endpoint
const char* ANTHROPIC_API_URL = "https://api.anthropic.com/v1/messages";
const char* ANTHROPIC_API_VERSION = "2023-06-01";

// Build system prompt with available effects and current state
String buildSystemPrompt() {
    String prompt = "You are an LED lighting controller assistant. You control LED strips by selecting effects, colors, and parameters.\n\n";
    
    prompt += "Available effects:\n";
    lume::EffectRegistry& registry = lume::effects();
    for (uint8_t i = 0; i < registry.getCount(); i++) {
        const lume::EffectInfo* info = registry.getByIndex(i);
        if (info) {
            prompt += "- " + String(info->id) + ": " + String(info->displayName) + "\n";
        }
    }
    
    prompt += "\nYour task: Parse the user's natural language request and respond with a JSON object that specifies:\n";
    prompt += "{\n";
    prompt += "  \"effect\": \"effect_id\",\n";
    prompt += "  \"speed\": 100,  // 1-200\n";
    prompt += "  \"intensity\": 128,  // 0-255\n";
    prompt += "  \"primaryColor\": [255, 0, 0],  // RGB\n";
    prompt += "  \"secondaryColor\": [0, 0, 255],  // RGB\n";
    prompt += "  \"brightness\": 128  // 0-255, optional\n";
    prompt += "}\n\n";
    prompt += "Match user intent to the most appropriate effect. For colors, interpret descriptions like 'warm', 'cool', 'cozy' into RGB values.\n";
    prompt += "Respond ONLY with the JSON object, no other text.";
    
    return prompt;
}

// Call Anthropic API
bool callAnthropicAPI(const String& userPrompt, String& response, String& error) {
    if (config.aiApiKey.length() == 0) {
        error = "AI API key not configured";
        return false;
    }
    
    WiFiClientSecure client;
    client.setInsecure(); // For simplicity - in production, should verify cert
    
    HTTPClient http;
    http.begin(client, ANTHROPIC_API_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", config.aiApiKey);
    http.addHeader("anthropic-version", ANTHROPIC_API_VERSION);
    http.setTimeout(30000); // 30 second timeout
    
    // Build request
    JsonDocument requestDoc;
    requestDoc["model"] = config.aiModel;
    requestDoc["max_tokens"] = 1024;
    
    JsonArray messages = requestDoc["messages"].to<JsonArray>();
    JsonObject message = messages.add<JsonObject>();
    message["role"] = "user";
    
    String systemPrompt = buildSystemPrompt();
    String fullPrompt = systemPrompt + "\n\nUser request: " + userPrompt;
    message["content"] = fullPrompt;
    
    String requestBody;
    serializeJson(requestDoc, requestBody);
    
    LOG_DEBUG(LogTag::WEB, "Calling Anthropic API...");
    
    int httpCode = http.POST(requestBody);
    
    if (httpCode == 200) {
        String payload = http.getString();
        
        JsonDocument responseDoc;
        DeserializationError parseError = deserializeJson(responseDoc, payload);
        
        if (parseError) {
            error = "Failed to parse API response";
            LOG_ERROR(LogTag::WEB, "JSON parse error: %s", parseError.c_str());
            http.end();
            return false;
        }
        
        // Extract text from response
        if (responseDoc["content"][0]["text"].is<const char*>()) {
            response = responseDoc["content"][0]["text"].as<String>();
            http.end();
            return true;
        } else {
            error = "Invalid response format";
            http.end();
            return false;
        }
    } else {
        // Get detailed error information
        String payload = "";
        if (httpCode > 0) {
            payload = http.getString();
            LOG_ERROR(LogTag::WEB, "Anthropic API error %d: %s", httpCode, payload.c_str());
            
            // Try to parse error details from response
            JsonDocument errorDoc;
            if (deserializeJson(errorDoc, payload) == DeserializationError::Ok) {
                if (errorDoc["error"]["message"].is<const char*>()) {
                    String errorMsg = errorDoc["error"]["message"].as<String>();
                    error = "API error (" + String(httpCode) + "): " + errorMsg;
                } else {
                    error = "API error: " + String(httpCode) + " - " + payload.substring(0, 100);
                }
            } else {
                error = "API error: " + String(httpCode) + " - " + payload.substring(0, 100);
            }
        } else {
            LOG_ERROR(LogTag::WEB, "HTTP request failed: %d", httpCode);
            error = "HTTP request failed: " + String(httpCode);
        }
        http.end();
        return false;
    }
}

// Apply the AI-generated spec to the controller
bool applySpec(const JsonDocument& spec, String& error) {
    lume::Segment* seg = lume::controller.getSegment(0);
    if (!seg) {
        error = "No active segment";
        return false;
    }
    
    // Apply effect
    if (spec["effect"].is<const char*>()) {
        const char* effectId = spec["effect"].as<const char*>();
        if (!seg->setEffect(effectId)) {
            LOG_WARN(LogTag::WEB, "Unknown effect: %s", effectId);
        }
    }
    
    // Apply speed
    if (spec["speed"].is<int>()) {
        seg->setSpeed(constrain(spec["speed"].as<int>(), 1, 200));
    }
    
    // Apply intensity
    if (spec["intensity"].is<int>()) {
        seg->setIntensity(constrain(spec["intensity"].as<int>(), 0, 255));
    }
    
    // Apply colors
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
    
    // Apply brightness
    if (spec["brightness"].is<int>()) {
        lume::controller.setBrightness(constrain(spec["brightness"].as<int>(), 0, 255));
    }
    
    return true;
}

} // namespace

void handleApiPromptPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }
    
    // Body size validation
    if (index == 0) {
        if (total > MAX_REQUEST_BODY_SIZE) {
            request->send(413, "application/json", "{\"error\":\"Request too large\"}");
            return;
        }
        promptBodyBuffer = "";
        promptBodyBuffer.reserve(total);
    }
    
    // Accumulate body chunks
    promptBodyBuffer += String((char*)data, len);
    
    // Only process when complete
    if (index + len < total) {
        return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, promptBodyBuffer);
    
    if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    
    if (!doc["prompt"].is<const char*>()) {
        request->send(400, "application/json", "{\"error\":\"Missing 'prompt' field\"}");
        return;
    }
    
    String userPrompt = doc["prompt"].as<String>();
    LOG_INFO(LogTag::WEB, "AI Prompt: %s", userPrompt.c_str());
    
    // Call Anthropic API
    String aiResponse;
    String apiError;
    
    if (!callAnthropicAPI(userPrompt, aiResponse, apiError)) {
        JsonDocument response;
        response["success"] = false;
        response["error"] = apiError;
        
        String output;
        serializeJson(response, output);
        request->send(500, "application/json", output);
        return;
    }
    
    LOG_DEBUG(LogTag::WEB, "AI Response: %s", aiResponse.c_str());
    
    // Parse AI response as JSON spec
    JsonDocument specDoc;
    DeserializationError specError = deserializeJson(specDoc, aiResponse);
    
    if (specError) {
        JsonDocument response;
        response["success"] = false;
        response["error"] = "AI returned invalid format";
        
        String output;
        serializeJson(response, output);
        request->send(500, "application/json", output);
        return;
    }
    
    // Apply the spec
    String applyError;
    if (!applySpec(specDoc, applyError)) {
        JsonDocument response;
        response["success"] = false;
        response["error"] = applyError;
        
        String output;
        serializeJson(response, output);
        request->send(500, "application/json", output);
        return;
    }
    
    // Success
    JsonDocument response;
    response["success"] = true;
    response["message"] = "Lights updated successfully!";
    response["spec"] = specDoc;
    
    String output;
    serializeJson(response, output);
    request->send(200, "application/json", output);
    
    LOG_INFO(LogTag::WEB, "AI prompt applied successfully");
}
