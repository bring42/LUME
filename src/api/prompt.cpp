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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>

// Request body buffer for async handling
static String promptBodyBuffer;

// Timestamp of the last accepted prompt, for rate limiting (P0.7). Combined with
// the blocking upstream call, an unthrottled /api/prompt is a DoS + billing burn.
static uint32_t lastPromptMs = 0;

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
    prompt += "  \"colors\": [[255, 0, 0], [0, 0, 255], [0, 255, 0]],  // RGB array [primary, secondary, tertiary]\n";
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

// Translate the AI-generated spec into bus commands (single-writer path). The
// segment/global mutations are applied by the render loop, not this task.
bool applySpec(const JsonDocument& spec, String& error) {
    // Existence check only (read); the AI targets segment 0.
    if (!lume::controller.getSegment(0)) {
        error = "No active segment";
        return false;
    }

    lume::EffectSpec es = {};

    // Effect (validated against the registry; effectId is copied into the spec).
    if (spec["effect"].is<const char*>()) {
        const char* effectId = spec["effect"].as<const char*>();
        if (lume::effects().getInfo(effectId)) {
            es.hasEffect = true;
            strncpy(es.effectId, effectId, lume::MAX_EFFECT_ID_LEN - 1);
            es.effectId[lume::MAX_EFFECT_ID_LEN - 1] = '\0';
            storage.saveLastEffect(effectId);  // Persist for next reboot
        } else {
            LOG_WARN(LogTag::WEB, "Unknown effect: %s", effectId);
        }
    }

    // Semantic params — resolved to slots on the loop via the name-aware setters.
    if (spec["speed"].is<int>()) {
        es.hasSpeed = true;
        es.speed = constrain(spec["speed"].as<int>(), 1, 200);
    }
    if (spec["intensity"].is<int>()) {
        es.hasIntensity = true;
        es.intensity = constrain(spec["intensity"].as<int>(), 0, 255);
    }

    // Colors (WLED format): up to 3, applied via setColor(i, ...) on the loop.
    if (spec["colors"].is<JsonArrayConst>()) {
        JsonArrayConst colorsArr = spec["colors"].as<JsonArrayConst>();
        for (uint8_t i = 0; i < 3 && i < colorsArr.size(); i++) {
            if (colorsArr[i].is<JsonArrayConst>()) {
                JsonArrayConst arr = colorsArr[i].as<JsonArrayConst>();
                if (arr.size() >= 3) {
                    es.colors[es.colorCount++] =
                        CRGB(arr[0].as<uint8_t>(), arr[1].as<uint8_t>(), arr[2].as<uint8_t>());
                }
            }
        }
    }

    lume::controller.enqueueCommand(lume::Command::applyEffectSpec(0, es));

    // Brightness is global.
    if (spec["brightness"].is<int>()) {
        lume::controller.enqueueCommand(
            lume::Command::setGlobalBrightness(constrain(spec["brightness"].as<int>(), 0, 255)));
    }

    return true;
}

// --- P0.4: offload the blocking Anthropic call off the AsyncTCP task ---
//
// Running callAnthropicAPI (a ~30s HTTPS request) on the async web task froze the
// whole server and put a large TLS arena on the 8KB async stack. Because applySpec
// now only enqueues bus commands (thread-safe), the work is trivially relocatable:
// a persistent worker task does the call + applySpec, and the handler returns 202.

constexpr size_t AI_PROMPT_MAX_LEN = 512;

struct AiJob {
    char prompt[AI_PROMPT_MAX_LEN];
};

QueueHandle_t       aiJobQueue  = nullptr;
TaskHandle_t        aiTaskHandle = nullptr;
std::atomic<bool>   aiBusy{false};   // one AI request in flight at a time

void aiWorkerTask(void*) {
    AiJob job;
    for (;;) {
        if (xQueueReceive(aiJobQueue, &job, portMAX_DELAY) != pdTRUE) continue;

        // The submitting request was already answered (202) and may be destroyed.
        // Touch NOTHING request/AsyncWebServer-related here — only the copied prompt.
        String userPrompt(job.prompt);
        String aiResponse, apiError;

        if (callAnthropicAPI(userPrompt, aiResponse, apiError)) {
            JsonDocument specDoc;
            if (deserializeJson(specDoc, aiResponse) == DeserializationError::Ok) {
                String applyError;
                if (!applySpec(specDoc, applyError)) {
                    LOG_WARN(LogTag::WEB, "AI applySpec failed: %s", applyError.c_str());
                }
            } else {
                LOG_WARN(LogTag::WEB, "AI returned invalid format");
            }
        } else {
            LOG_ERROR(LogTag::WEB, "AI call failed: %s", apiError.c_str());
        }
        aiBusy.store(false);   // single release point for the busy flag
    }
}

// Lazily create the worker task + queue (idempotent).
bool ensureAiWorker() {
    if (aiTaskHandle) return true;
    if (!aiJobQueue) {
        aiJobQueue = xQueueCreate(1, sizeof(AiJob));
        if (!aiJobQueue) { LOG_ERROR(LogTag::WEB, "AI job queue create failed"); return false; }
    }
    BaseType_t ok = xTaskCreatePinnedToCore(
        aiWorkerTask, "ai_prompt", ANTHROPIC_TASK_STACK_SIZE, nullptr,
        ANTHROPIC_TASK_PRIORITY, &aiTaskHandle, ANTHROPIC_TASK_CORE);
    if (ok != pdPASS) { LOG_ERROR(LogTag::WEB, "AI task create failed"); aiTaskHandle = nullptr; return false; }
    return true;
}

} // namespace

void initAiPromptWorker() { ensureAiWorker(); }

void handleApiPromptPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Auth check at start of request
    if (index == 0 && !checkAuth(request)) {
        sendUnauthorized(request);
        return;
    }

    // Rate limit at the start of a new request (P0.7). Auth is optional, so this
    // is the only throttle on the expensive upstream call.
    if (index == 0) {
        uint32_t now = millis();
        if (lastPromptMs != 0 && (now - lastPromptMs) < PROMPT_RATE_LIMIT_MS) {
            request->send(429, "application/json", "{\"error\":\"Rate limited, try again shortly\"}");
            return;
        }
        lastPromptMs = now;
    }

    // Body size validation. beginBody() is claimed AFTER the rate-limit check so
    // a throttled (429) request never touches the body slot (P0.3).
    if (index == 0) {
        if (!beginBody(request)) {
            request->send(409, "application/json", "{\"error\":\"Busy, retry\"}");
            return;
        }
        if (total > MAX_REQUEST_BODY_SIZE) {
            endBody(request);
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
    endBody(request);   // P0.3: body fully assembled; release the slot

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

    if (userPrompt.length() >= AI_PROMPT_MAX_LEN) {
        request->send(413, "application/json", "{\"error\":\"Prompt too long\"}");
        return;
    }

    // Ensure the worker exists (lazy, in case the setup() hook didn't run).
    if (!ensureAiWorker()) {
        request->send(503, "application/json", "{\"error\":\"AI worker unavailable\"}");
        return;
    }

    // One AI request at a time. The ~30s upstream call outlasts the P0.7 rate
    // window (3s), so the rate limit alone can't prevent overlap.
    bool expected = false;
    if (!aiBusy.compare_exchange_strong(expected, true)) {
        request->send(409, "application/json", "{\"error\":\"AI request already in progress\"}");
        return;
    }

    AiJob job;
    strncpy(job.prompt, userPrompt.c_str(), AI_PROMPT_MAX_LEN - 1);
    job.prompt[AI_PROMPT_MAX_LEN - 1] = '\0';

    if (xQueueSend(aiJobQueue, &job, 0) != pdTRUE) {
        aiBusy.store(false);   // release on enqueue failure
        request->send(503, "application/json", "{\"error\":\"AI queue full\"}");
        return;
    }

    // Accepted. The worker does the blocking call + applySpec (which enqueues bus
    // commands); the UI reconciles via the ~1s WebSocket state push.
    request->send(202, "application/json", "{\"status\":\"accepted\"}");
    LOG_INFO(LogTag::WEB, "AI prompt enqueued");
}
