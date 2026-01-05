# Protocols

Network protocols that provide LED data (sACN, MQTT control, etc.).

## Architecture

Protocols implement the `IProtocol` interface for controller decoupling:

```cpp
class IProtocol {
    virtual const char* name() = 0;
    virtual void begin() = 0;
    virtual void loop() = 0;
    virtual bool isActive() = 0;
    virtual bool hasData() = 0;
    virtual const CRGB* getBuffer() = 0;
    // ...
};
```

Controller only depends on `IProtocol`, not specific implementations.

## Protocol Classes

### IProtocol ([protocol.h](protocol.h))
Minimal interface for controller interaction. Controller only knows this.

### Protocol ([protocol.h](protocol.h))
Base class with full functionality (buffer management, timeouts, etc.).

### SacnProtocol ([sacn.h](sacn.h))
E1.31 (sACN) streaming ACN implementation.

```cpp
// Configure universes
sacnProtocol.configure(1, 1, false);  // Universe 1, 1 universe, multicast
controller.registerProtocol(&sacnProtocol);
```

### MqttProtocol ([mqtt.h](mqtt.h))
MQTT command/control protocol (not registered with controller).

```cpp
MqttConfig config;
config.broker = "192.168.1.100";
mqtt.begin(config, &controller);
```

## Adding a Protocol

1. Inherit from `Protocol` (or implement `IProtocol` directly)
2. Implement required methods
3. Use `ProtocolBuffer<N>` for thread-safe LED data
4. Register with controller: `controller.registerProtocol(&myProtocol)`

Example:

```cpp
class ArtNetProtocol : public Protocol {
    bool begin_impl() override;
    void loop() override { udp.parsePacket(); }
    bool isActive_impl() const override { return !buffer_.hasTimedOut(5000); }
    // ... buffer access methods
private:
    ProtocolBuffer<MAX_LED_COUNT> buffer_;
};
```

## Thread Safety

Protocols may receive data on network tasks (not main loop):
- Write to `ProtocolBuffer` with atomic flags
- Controller checks `hasData()` and copies when ready
- Never write directly to controller's LED array
