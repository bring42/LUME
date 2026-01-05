#ifndef LUME_PROTOCOL_H
#define LUME_PROTOCOL_H

#include <Arduino.h>
#include <FastLED.h>
#include <atomic>

namespace lume {

/**
 * IProtocol - Minimal interface for protocol decoupling
 * 
 * Controller only depends on this interface, not specific implementations.
 * This allows protocols to be plugged in without controller knowing details.
 * 
 * The interface provides just enough functionality for the controller to:
 * - Initialize protocols (begin)
 * - Update them each frame (loop)
 * - Check if they're active (isActive)
 * - Access their LED buffer when they have data
 */
class IProtocol {
public:
    virtual ~IProtocol() = default;
    
    // Protocol identity
    virtual const char* name() = 0;
    
    // Lifecycle - controller calls these
    virtual void begin() = 0;
    virtual void loop() = 0;
    
    // Status checks
    virtual bool isActive() = 0;
    virtual bool isEnabled() = 0;
    
    // Buffer access (for protocols that provide LED data)
    virtual bool hasData() = 0;           // Check if protocol has new data ready
    virtual const CRGB* getBuffer() = 0;  // Get LED buffer (nullptr if no data)
    virtual uint16_t getBufferSize() = 0; // Get buffer size in LEDs
    virtual void clearData() = 0;         // Clear the data-ready flag
};

/**
 * Protocol - Base interface for lighting protocols
 * 
 * Protocols (sACN, Art-Net, DDP, future Matter) are "temporary sole writers"
 * to the LED array. When active, they have priority over effects.
 * 
 * Design principles (from ARCHITECTURE.md):
 * - Protocol callbacks run on network tasks, NOT the main loop
 * - Protocols must NOT write to the LED array directly
 * - Instead, they write to a buffer and set a "ready" flag
 * - Main loop checks flag and copies buffer to LEDs
 * 
 * This ensures single-writer semantics are maintained.
 */
class Protocol : public IProtocol {
public:
    virtual ~Protocol() = default;
    
    // --- IProtocol interface (controller-facing API) ---
    
    const char* name() override { return getName(); }
    void begin() override { begin_impl(); }
    void loop() override { update(); }
    bool isActive() override { return isActive_impl(); }
    bool isEnabled() override { return isEnabled_impl(); }
    
    // Buffer access through IProtocol interface
    bool hasData() override { return hasFrameReady(); }
    const CRGB* getBuffer() override { return getBufferInternal(); }
    uint16_t getBufferSize() override { return getBufferSizeInternal(); }
    void clearData() override { clearFrameReady(); }
    
    // --- Full Protocol interface (for subclasses and detailed control) ---
    
    // Initialize the protocol (e.g., start UDP listener)
    virtual bool begin_impl() = 0;
    
    // Stop the protocol
    virtual void stop() = 0;
    
    // --- Configuration ---
    
    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled_impl() const = 0;
    
    // --- Runtime ---
    
    // Process incoming data (called from network context)
    // Returns true if new data was received
    virtual bool update() = 0;
    
    // Check if protocol has timed out (no data for timeoutMs)
    virtual bool hasTimedOut(uint32_t timeoutMs = 5000) const = 0;
    
    // Check if protocol is currently active (receiving data)
    virtual bool isActive_impl() const = 0;
    
    // --- Protocol buffer access (internal versions) ---
    
    // Check if a new frame is ready in the buffer
    virtual bool hasFrameReady() const = 0;
    
    // Get pointer to the protocol's LED buffer
    virtual const CRGB* getBufferInternal() const = 0;
    
    // Get the number of LEDs in the buffer
    virtual uint16_t getBufferSizeInternal() const = 0;
    
    // Clear the frame-ready flag after copying
    virtual void clearFrameReady() = 0;
    
    // --- Diagnostics ---
    
    virtual const char* getName() const = 0;
    virtual uint32_t getPacketCount() const = 0;
    virtual uint32_t getLastPacketTime() const = 0;
};

/**
 * ProtocolBuffer - Thread-safe buffer for protocol data
 * 
 * This helper class implements the double-buffer pattern described
 * in the architecture doc. Protocols write to this, main loop reads.
 */
template<uint16_t MAX_LEDS>
class ProtocolBuffer {
public:
    ProtocolBuffer() : frameReady(false), ledCount(0), lastWriteTime(0) {
        memset(buffer, 0, sizeof(buffer));
    }
    
    // Write new frame data (called from protocol/network context)
    void write(const CRGB* data, uint16_t count) {
        count = min(count, MAX_LEDS);
        memcpy(buffer, data, count * sizeof(CRGB));
        ledCount = count;
        lastWriteTime = millis();
        frameReady.store(true, std::memory_order_release);
    }
    
    // Write from raw RGB data (DMX format)
    void writeRGB(const uint8_t* rgbData, uint16_t numLeds, uint16_t startChannel = 0) {
        numLeds = min(numLeds, MAX_LEDS);
        for (uint16_t i = 0; i < numLeds; i++) {
            uint16_t offset = startChannel + (i * 3);
            buffer[i] = CRGB(rgbData[offset], rgbData[offset + 1], rgbData[offset + 2]);
        }
        ledCount = numLeds;
        lastWriteTime = millis();
        frameReady.store(true, std::memory_order_release);
    }
    
    // Check if new frame is ready (called from main loop)
    bool isReady() const {
        return frameReady.load(std::memory_order_acquire);
    }
    
    // Get buffer for reading (main loop only)
    const CRGB* getBuffer() const { return buffer; }
    uint16_t getLedCount() const { return ledCount; }
    uint32_t getLastWriteTime() const { return lastWriteTime; }
    
    // Clear ready flag after copying
    void clearReady() {
        frameReady.store(false, std::memory_order_release);
    }
    
    // Check timeout
    bool hasTimedOut(uint32_t timeoutMs) const {
        if (lastWriteTime == 0) return true;
        return (millis() - lastWriteTime) > timeoutMs;
    }
    
private:
    CRGB buffer[MAX_LEDS];
    std::atomic<bool> frameReady;
    uint16_t ledCount;
    uint32_t lastWriteTime;
};

} // namespace lume

#endif // LUME_PROTOCOL_H
