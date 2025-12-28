#ifndef ANTHROPIC_CLIENT_H
#define ANTHROPIC_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Job states for async prompt processing
enum class PromptJobState : uint8_t {
    IDLE = 0,
    QUEUED,
    RUNNING,
    DONE,
    ERROR
};

// Job result structure
struct PromptJobResult {
    PromptJobState state;
    String message;
    String prompt;      // Original user prompt for debugging
    String rawResponse;
    String effectSpec;  // Parsed JSON effect specification
    unsigned long startTime;
    unsigned long endTime;
    
    PromptJobResult() :
        state(PromptJobState::IDLE),
        message(""),
        prompt(""),
        rawResponse(""),
        effectSpec(""),
        startTime(0),
        endTime(0) {}
    
    void reset() {
        state = PromptJobState::IDLE;
        message = "";
        prompt = "";
        rawResponse = "";
        effectSpec = "";
        startTime = 0;
        endTime = 0;
    }
};

// Request structure for the job queue
struct PromptRequest {
    String prompt;
    String apiKey;
    String model;
    String currentLedStateJson;  // Current LED state for context
    
    PromptRequest() :
        prompt(""),
        apiKey(""),
        model("claude-3-5-sonnet-20241022"),
        currentLedStateJson("{}") {}
};

class AnthropicClient {
public:
    AnthropicClient();
    
    // Initialize the client
    void begin();
    
    // Submit a prompt for processing (non-blocking, queues job)
    bool submitPrompt(const PromptRequest& request);
    
    // Get current job status
    PromptJobResult& getJobResult();
    
    // Check if a job is currently running
    bool isJobRunning();
    
    // Cancel current job (if possible)
    void cancelJob();
    
    // Get the effect specification JSON schema
    static const char* getEffectSchema();
    
    // Build system prompt for Anthropic
    static String buildSystemPrompt();
    
    // Parse assistant response and extract effect JSON
    bool parseEffectFromResponse(const String& response, JsonDocument& effectDoc, String& errorMsg);
    
    // Process job in FreeRTOS task (call from task function)
    void processJob();
    
    // Task handle for the background job
    TaskHandle_t taskHandle;
    
private:
    PromptJobResult jobResult;
    PromptRequest currentRequest;
    SemaphoreHandle_t jobMutex;
    volatile bool cancelRequested;
    
    // Synchronous HTTP call (runs in background task)
    bool callAnthropic(const PromptRequest& request, String& response, String& errorMsg);
    
    // Build request body JSON
    String buildRequestBody(const PromptRequest& request);
};

extern AnthropicClient openRouterClient;

// FreeRTOS task function declaration
void anthropicTaskFunction(void* parameter);

#endif // ANTHROPIC_CLIENT_H
