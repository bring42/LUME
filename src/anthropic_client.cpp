#include "anthropic_client.h"

AnthropicClient openRouterClient;

// Anthropic API endpoint
static const char* ANTHROPIC_HOST = "api.anthropic.com";
static const char* ANTHROPIC_PATH = "/v1/messages";
static const int ANTHROPIC_PORT = 443;
static const char* ANTHROPIC_VERSION = "2023-06-01";

// Effect JSON schema for LLM instruction
static const char EFFECT_SCHEMA[] PROGMEM = R"json({
  "mode": "effect|pixels",
  
  "effect": "solid|rainbow|confetti|fire|colorwaves|theater|gradient|sparkle|pulse|noise|meteor|twinkle|sinelon|candle|breathe|custom",
  "palette": "rainbow|lava|ocean|party|forest|cloud",
  "brightness": 0-255 (INTEGER),
  "speed": 1-200 (INTEGER),
  "primaryColor": [r,g,b] (ARRAY of 3 integers 0-255),
  "secondaryColor": [r,g,b] (ARRAY of 3 integers 0-255),
  "notes": "description",
  "custom": {
    "type": "wave_up|wave_down|wave_center|breathe|scanner|comet|rain|fire_up",
    "param1": 0-255 (INTEGER),
    "param2": 0-255 (INTEGER),
    "param3": 0-255 (INTEGER),
    "param4": 0-255 (INTEGER)
  },
  
  "pixels": {
    "fill": [r,g,b],
    "gradient": {"from": [r,g,b], "to": [r,g,b]},
    "pixels": [[r,g,b], [r,g,b], ...] (array for each LED)
  }
})json";

// System prompt template
static const char SYSTEM_PROMPT[] PROGMEM = R"prompt(You are an LED strip effect controller for a WS2812B strip with 160 LEDs arranged VERTICALLY (pixel 0 = bottom, pixel 159 = top).

Respond with ONLY valid JSON. NO markdown, NO explanation, just the JSON object.

CRITICAL JSON RULES:
- Numbers must be numbers: "brightness": 150 NOT "brightness": "150"
- Arrays must be arrays: "primaryColor": [255,0,0] NOT "primaryColor": "[255,0,0]"
- All values must match the types shown below exactly

SCHEMA:
%s

=== MODE: "effect" ===
Use for animated/continuous effects. Runs on-device.

BUILT-IN EFFECTS (what they ACTUALLY do):
- solid: All LEDs same color (uses primaryColor)
- rainbow: Smooth cycling rainbow, all LEDs shift through spectrum
- confetti: Random pixels flash random colors briefly
- fire: Realistic fire flicker simulation (orange/red/yellow)
- colorwaves: Smooth waves of palette colors moving through strip
- theater: Classic theater chase (groups of LEDs moving)
- gradient: STATIC gradient from primaryColor (bottom) to secondaryColor (top)
- sparkle: Random white sparkles on primaryColor background
- pulse: All LEDs breathe/fade primaryColor in and out together
- noise: Organic Perlin noise movement using palette colors

CUSTOM EFFECTS (effect: "custom", set custom.type):
- wave_up: A band of primaryColor rises from bottom to top, fades behind
- wave_down: A band falls from top to bottom
- wave_center: Waves expand outward from center
- breathe: Smooth breathing, param1: 0=all together, 1=rising, 2=falling
- scanner: Knight Rider/Cylon - single dot bounces back and forth with tail
- comet: Comet with tail, param1: 0=upward, 1=downward
- rain: Drops falling from top (like rain)
- fire_up: Fire flames rising upward (inverted fire)

Speed affects animation rate (1=slow, 200=fast).
Palette affects multi-color effects (colorwaves, noise, confetti).

=== MODE: "pixels" ===
Use for CUSTOM STATIC FRAMES - direct pixel control. Better for:
- Complex color patterns that don't fit built-in effects
- Specific artistic designs
- Scenes that need precise pixel placement

Options (use ONE):
- "fill": [r,g,b] - fill all 160 pixels with one color
- "gradient": {"from": [r,g,b], "to": [r,g,b]} - smooth gradient bottom to top
- "pixels": array of 160 [r,g,b] values for each LED (0=bottom, 159=top)

=== GUIDELINES ===
1. If user asks for something the built-in effects can do, use mode:"effect"
2. If user asks for a complex custom pattern, use mode:"pixels" with gradient or pixels array
3. Be HONEST - if something isn't possible with these options, use the closest match
4. "Glitchy" = confetti or sparkle with high speed
5. "Breathing" = pulse or custom breathe
6. "Futuristic" = cyan/purple colors, scanner or wave effects
7. "Cozy/warm" = fire or gradient with warm colors

Current state: %s)prompt";

AnthropicClient::AnthropicClient() :
    taskHandle(nullptr),
    cancelRequested(false)
{
    jobMutex = xSemaphoreCreateMutex();
}

void AnthropicClient::begin() {
    // Initialize is done in constructor
}

bool AnthropicClient::submitPrompt(const PromptRequest& request) {
    if (xSemaphoreTake(jobMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    
    // Check if job is already running
    if (jobResult.state == PromptJobState::RUNNING || 
        jobResult.state == PromptJobState::QUEUED) {
        xSemaphoreGive(jobMutex);
        return false;
    }
    
    // Store request
    currentRequest = request;
    
    // Reset result
    jobResult.reset();
    jobResult.state = PromptJobState::QUEUED;
    jobResult.message = "Job queued";
    jobResult.startTime = millis();
    
    cancelRequested = false;
    
    xSemaphoreGive(jobMutex);
    
    // Create task to process the job
    BaseType_t result = xTaskCreatePinnedToCore(
        anthropicTaskFunction,
        "AnthropicTask",
        16384,  // Stack size (16KB for HTTPS)
        this,
        1,      // Priority
        &taskHandle,
        0       // Core 0 (leave Core 1 for LED updates)
    );
    
    if (result != pdPASS) {
        xSemaphoreTake(jobMutex, portMAX_DELAY);
        jobResult.state = PromptJobState::ERROR;
        jobResult.message = "Failed to create task";
        xSemaphoreGive(jobMutex);
        return false;
    }
    
    return true;
}

PromptJobResult& AnthropicClient::getJobResult() {
    return jobResult;
}

bool AnthropicClient::isJobRunning() {
    return jobResult.state == PromptJobState::RUNNING || 
           jobResult.state == PromptJobState::QUEUED;
}

void AnthropicClient::cancelJob() {
    cancelRequested = true;
}

const char* AnthropicClient::getEffectSchema() {
    return EFFECT_SCHEMA;
}

String AnthropicClient::buildSystemPrompt() {
    char buffer[2048];
    snprintf(buffer, sizeof(buffer), SYSTEM_PROMPT, EFFECT_SCHEMA, "%s");
    return String(buffer);
}

void AnthropicClient::processJob() {
    xSemaphoreTake(jobMutex, portMAX_DELAY);
    jobResult.state = PromptJobState::RUNNING;
    jobResult.message = "Processing...";
    jobResult.prompt = currentRequest.prompt;  // Store prompt for debugging
    PromptRequest req = currentRequest;  // Copy request
    xSemaphoreGive(jobMutex);
    
    String response;
    String errorMsg;
    
    bool success = callAnthropic(req, response, errorMsg);
    
    xSemaphoreTake(jobMutex, portMAX_DELAY);
    jobResult.endTime = millis();
    
    if (cancelRequested) {
        jobResult.state = PromptJobState::IDLE;
        jobResult.message = "Cancelled";
    } else if (success) {
        jobResult.rawResponse = response;
        
        // Parse the effect JSON from response
        JsonDocument effectDoc;
        String parseError;
        if (parseEffectFromResponse(response, effectDoc, parseError)) {
            // Serialize back to clean JSON
            serializeJson(effectDoc, jobResult.effectSpec);
            jobResult.state = PromptJobState::DONE;
            jobResult.message = "Success";
        } else {
            jobResult.state = PromptJobState::ERROR;
            jobResult.message = "Parse error: " + parseError;
        }
    } else {
        jobResult.state = PromptJobState::ERROR;
        jobResult.message = errorMsg;
    }
    
    xSemaphoreGive(jobMutex);
}

bool AnthropicClient::callAnthropic(const PromptRequest& request, String& response, String& errorMsg) {
    if (request.apiKey.length() == 0) {
        errorMsg = "API key not set";
        return false;
    }
    
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate validation (for simplicity)
    
    HTTPClient http;
    
    String url = "https://" + String(ANTHROPIC_HOST) + String(ANTHROPIC_PATH);
    
    if (!http.begin(client, url)) {
        errorMsg = "Failed to connect";
        return false;
    }
    
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", request.apiKey);
    http.addHeader("anthropic-version", ANTHROPIC_VERSION);
    http.setTimeout(30000);  // 30 second timeout
    
    String body = buildRequestBody(request);
    
    int httpCode = http.POST(body);
    
    if (httpCode <= 0) {
        errorMsg = "HTTP error: " + String(http.errorToString(httpCode));
        http.end();
        return false;
    }
    
    if (httpCode != 200) {
        String errorBody = http.getString();
        errorMsg = "API error " + String(httpCode) + ": " + errorBody.substring(0, 200);
        http.end();
        return false;
    }
    
    response = http.getString();
    http.end();
    
    return true;
}

String AnthropicClient::buildRequestBody(const PromptRequest& request) {
    JsonDocument doc;
    
    doc["model"] = request.model;
    doc["max_tokens"] = 1024;
    
    // Build system prompt with current state
    char sysContent[2048];
    snprintf(sysContent, sizeof(sysContent), SYSTEM_PROMPT, EFFECT_SCHEMA, request.currentLedStateJson.c_str());
    doc["system"] = sysContent;
    
    JsonArray messages = doc["messages"].to<JsonArray>();
    
    // User message
    JsonObject userMsg = messages.add<JsonObject>();
    userMsg["role"] = "user";
    userMsg["content"] = request.prompt;
    
    String output;
    serializeJson(doc, output);
    return output;
}

bool AnthropicClient::parseEffectFromResponse(const String& response, JsonDocument& effectDoc, String& errorMsg) {
    // First parse the Anthropic response
    JsonDocument responseDoc;
    DeserializationError err = deserializeJson(responseDoc, response);
    
    if (err) {
        errorMsg = "Failed to parse API response: " + String(err.c_str());
        return false;
    }
    
    // Extract content from content[0].text
    if (!responseDoc["content"].is<JsonArray>() || responseDoc["content"].size() == 0) {
        errorMsg = "No content in response";
        return false;
    }
    
    JsonObject contentBlock = responseDoc["content"][0];
    if (!contentBlock["text"].is<const char*>()) {
        errorMsg = "No text in response";
        return false;
    }
    
    String content = contentBlock["text"].as<String>();
    
    // Clean up content - remove markdown code blocks if present
    content.trim();
    if (content.startsWith("```json")) {
        content = content.substring(7);
    } else if (content.startsWith("```")) {
        content = content.substring(3);
    }
    if (content.endsWith("```")) {
        content = content.substring(0, content.length() - 3);
    }
    content.trim();
    
    // Parse the effect JSON
    err = deserializeJson(effectDoc, content);
    if (err) {
        errorMsg = "Failed to parse effect JSON: " + String(err.c_str());
        return false;
    }
    
    // Validate required fields
    if (!effectDoc["effect"].is<const char*>()) {
        errorMsg = "Missing 'effect' field";
        return false;
    }
    
    // Validate and clamp values
    if (effectDoc["brightness"].is<int>()) {
        int brightness = effectDoc["brightness"].as<int>();
        effectDoc["brightness"] = constrain(brightness, 0, 255);
    }
    if (effectDoc["speed"].is<int>()) {
        int speed = effectDoc["speed"].as<int>();
        effectDoc["speed"] = constrain(speed, 1, 200);
    }
    
    return true;
}

// FreeRTOS task function
void anthropicTaskFunction(void* parameter) {
    AnthropicClient* client = (AnthropicClient*)parameter;
    client->processJob();
    
    // Delete task when done
    client->taskHandle = nullptr;
    vTaskDelete(NULL);
}
