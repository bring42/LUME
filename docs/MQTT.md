# MQTT Integration Guide

LUME supports MQTT for integration with home automation systems like Home Assistant, Node-RED, and others.

---

## Quick Start

1. Open the web UI → Configuration
2. Enable **MQTT**
3. Enter your **Broker** hostname/IP (e.g., `192.168.1.100`)
4. Set **Port** (default: `1883`)
5. Add **Username/Password** if required
6. Set **Topic Prefix** (default: `lume`)
7. Save Configuration

The device will automatically connect and publish state.

---

## Topic Structure

All topics are prefixed with your configured prefix (default: `lume`).

| Topic | Direction | Description |
|-------|-----------|-------------|
| `{prefix}/status` | → Broker | Online/offline (LWT) |
| `{prefix}/state` | → Broker | JSON state (published on change) |
| `{prefix}/set` | ← Broker | JSON commands |
| `{prefix}/brightness/set` | ← Broker | Brightness (0-255) |
| `{prefix}/effect/set` | ← Broker | Effect name |
| `{prefix}/power/set` | ← Broker | Power (ON/OFF) |

---

## State Message

Published to `{prefix}/state` on change and every 30 seconds:

```json
{
  "power": true,
  "brightness": 128,
  "effect": "rainbow",
  "speed": 100,
  "intensity": 128,
  "uptime": 3600,
  "heap_free": 245000,
  "ip": "192.168.1.100"
}
```

---

## Command Messages

### JSON Command (to `{prefix}/set`)

Full control via JSON:

```json
{
  "state": "ON",
  "brightness": 200,
  "effect": "fire",
  "speed": 150,
  "intensity": 200
}
```

All fields are optional. Only included fields are applied.

### Simple Commands

| Topic | Payload | Effect |
|-------|---------|--------|
| `{prefix}/brightness/set` | `128` | Set brightness to 128 |
| `{prefix}/effect/set` | `rainbow` | Change to rainbow effect |
| `{prefix}/power/set` | `ON` or `OFF` | Power on/off |

---

## Home Assistant Integration

### Auto-Discovery

LUME publishes Home Assistant MQTT discovery messages automatically. After connecting, the device should appear in Home Assistant under **Devices & Services → MQTT**.

Entity created:
- **Light** with brightness and effect support

### Manual Configuration

If auto-discovery is disabled, add to `configuration.yaml`:

```yaml
mqtt:
  light:
    - name: "LUME"
      unique_id: "lume_light"
      state_topic: "lume/state"
      command_topic: "lume/set"
      availability_topic: "lume/status"
      payload_available: "online"
      payload_not_available: "offline"
      brightness: true
      brightness_scale: 255
      effect: true
      effect_list:
        - solid
        - rainbow
        - fire
        - confetti
        - gradient
        - pulse
        - meteor
        - twinkle
        - candle
        - breathe
      schema: json
```

### Lovelace Card Example

```yaml
type: light
entity: light.lume
name: LED Strip
```

---

## Node-RED Integration

### Read State

```
[mqtt in] → topic: lume/state → [json] → [function: msg.payload.brightness]
```

### Control Brightness

```
[inject: 200] → [function: return {payload: {brightness: msg.payload}}] → [json] → [mqtt out: lume/set]
```

### Toggle Power

```
[inject] → [function: return {payload: "ON"}] → [mqtt out: lume/power/set]
```

---

## Python Examples

### Subscribe to State

```python
import paho.mqtt.client as mqtt
import json

def on_message(client, userdata, msg):
    state = json.loads(msg.payload)
    print(f"Brightness: {state['brightness']}, Effect: {state['effect']}")

client = mqtt.Client()
client.connect("192.168.1.100", 1883)
client.subscribe("lume/state")
client.on_message = on_message
client.loop_forever()
```

### Set Effect

```python
import paho.mqtt.client as mqtt
import json

client = mqtt.Client()
client.connect("192.168.1.100", 1883)

# JSON command
client.publish("lume/set", json.dumps({
    "brightness": 200,
    "effect": "fire"
}))

# Simple command
client.publish("lume/effect/set", "rainbow")

client.disconnect()
```

---

## Available Effects

All 23 built-in effects can be set via MQTT:

`solid`, `rainbow`, `confetti`, `fire`, `colorwaves`, `theater`, `gradient`, `sparkle`, `pulse`, `noise`, `meteor`, `twinkle`, `sinelon`, `candle`, `breathe`, `dots`, `juggle`, `bpm`, `larson`, `cylon`, `lightning`, `ripple`, `pacifica`

---

## Connection Details

| Setting | Default | Description |
|---------|---------|-------------|
| Port | 1883 | Standard MQTT port |
| Keep-Alive | 60s | Ping interval |
| Reconnect | 5s | Delay between reconnection attempts |
| Client ID | `lume-{mac}` | Auto-generated from MAC address |
| LWT Topic | `{prefix}/status` | Last Will and Testament |
| LWT Message | `offline` | Published if connection lost |

---

## Troubleshooting

### Not Connecting

1. Verify broker IP/hostname is reachable
2. Check firewall allows port 1883
3. Confirm username/password if broker requires authentication
4. Check broker logs for connection attempts

### State Not Updating

1. MQTT connected? Check `/health` endpoint or web UI status
2. Subscribed to correct topic? Use MQTT Explorer to verify
3. State publishes on change and every 30 seconds

### Home Assistant Not Discovering

1. Ensure HA MQTT integration is set up
2. Check discovery topic: `homeassistant/light/lume_XXXX/config`
3. Restart Home Assistant after device connects
4. Check HA logs for MQTT discovery messages

### Commands Not Working

1. Publishing to correct topic? (e.g., `lume/set`, not `lume/state`)
2. JSON valid? Use a JSON validator
3. Effect name correct? Check available effects list
4. Check serial monitor for incoming MQTT messages

---

## Security Considerations

- **Use authentication** - Configure username/password on your broker
- **TLS not supported** - MQTT traffic is unencrypted; use on trusted networks only
- **Firewall** - Block MQTT port from external access if not needed
