#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>

// ============================================
// Structured Logging System
// ============================================

// Log levels (can be filtered at compile time)
enum class LogLevel : uint8_t {
    DEBUG = 0,   // Verbose debugging info
    INFO = 1,    // Normal operational messages
    WARN = 2,    // Warning conditions
    ERROR = 3,   // Error conditions
    NONE = 4     // Disable all logging
};

// Set minimum log level (compile-time filter)
// Change this to filter out lower-priority logs
#ifndef LOG_LEVEL
#define LOG_LEVEL LogLevel::DEBUG
#endif

// Component tags for filtering/identification
namespace LogTag {
    constexpr const char* MAIN = "MAIN";
    constexpr const char* WIFI = "WIFI";
    constexpr const char* LED = "LED";
    constexpr const char* AI = "AI";
    constexpr const char* SACN = "SACN";
    constexpr const char* WEB = "WEB";
    constexpr const char* OTA = "OTA";
    constexpr const char* STORAGE = "NVS";
}

// Internal logging implementation
class Logger {
public:
    static void log(LogLevel level, const char* tag, const char* format, ...) {
        if (level < LOG_LEVEL) return;
        
        // Print timestamp
        Serial.printf("[%8lu] ", millis());
        
        // Print level indicator
        switch (level) {
            case LogLevel::DEBUG: Serial.print("[D] "); break;
            case LogLevel::INFO:  Serial.print("[I] "); break;
            case LogLevel::WARN:  Serial.print("[W] "); break;
            case LogLevel::ERROR: Serial.print("[E] "); break;
            default: break;
        }
        
        // Print tag
        Serial.printf("[%-4s] ", tag);
        
        // Print formatted message
        va_list args;
        va_start(args, format);
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        Serial.println(buffer);
    }
    
    // Log with hex dump (useful for debugging binary data)
    static void logHex(LogLevel level, const char* tag, const char* label, 
                       const uint8_t* data, size_t len, size_t maxLen = 32) {
        if (level < LOG_LEVEL) return;
        
        log(level, tag, "%s (%d bytes):", label, len);
        
        size_t printLen = min(len, maxLen);
        Serial.print("    ");
        for (size_t i = 0; i < printLen; i++) {
            Serial.printf("%02X ", data[i]);
            if ((i + 1) % 16 == 0 && i + 1 < printLen) {
                Serial.print("\n    ");
            }
        }
        if (len > maxLen) {
            Serial.printf("... (%d more bytes)", len - maxLen);
        }
        Serial.println();
    }
};

// Convenience macros for logging
#define LOG_DEBUG(tag, fmt, ...) Logger::log(LogLevel::DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  Logger::log(LogLevel::INFO, tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  Logger::log(LogLevel::WARN, tag, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) Logger::log(LogLevel::ERROR, tag, fmt, ##__VA_ARGS__)

// Hex dump macro
#define LOG_HEX(level, tag, label, data, len) Logger::logHex(level, tag, label, data, len)

// Memory stats helper
inline void logMemoryStats(const char* tag, const char* context = "") {
    LOG_DEBUG(tag, "Heap free: %u, largest block: %u %s", 
              ESP.getFreeHeap(), ESP.getMaxAllocHeap(), context);
}

#endif // LOGGING_H
