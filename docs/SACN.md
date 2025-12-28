# sACN (E1.31) Protocol Guide

The controller can receive DMX data over the network using the sACN (E1.31) protocol. This allows integration with professional lighting software like QLC+, xLights, TouchDesigner, and more.

---

## Quick Start

1. Open the web UI → Configuration
2. Enable **sACN (E1.31) Input**
3. Set **Start Universe** (typically 1)
4. Set **Universe Count** (based on LED count)
5. Save Configuration
6. Configure your lighting software to send to the device

---

## Configuration Options

| Setting | Range | Description |
|---------|-------|-------------|
| Start Universe | 1-63999 | First DMX universe to receive |
| Universe Count | 1-8 | Number of consecutive universes |
| Start Channel | 1-512 | First channel within universe |
| Unicast Mode | on/off | Direct IP vs multicast |

---

## Multi-Universe Support

Each DMX universe supports 512 channels = 170 RGB LEDs. For longer strips:

| LEDs | Universes Needed |
|------|------------------|
| 1-170 | 1 |
| 171-340 | 2 |
| 341-510 | 3 |
| 511-680 | 4 |
| 681-850 | 5 |
| 851-1020 | 6 |
| 1021-1190 | 7 |
| 1191-1360 | 8 (max) |

Configure your lighting software to send consecutive universes (e.g., universes 1-3 for 500 LEDs).

---

## Unicast vs Multicast

### Multicast (Default)
- Standard sACN behavior
- Works with most software out of the box
- Requires network support for multicast
- Multiple receivers can listen to same universe

### Unicast
- Send directly to device IP
- More reliable, especially on complex networks
- Better for multi-universe setups
- Required if multicast is blocked

**To use unicast:**
1. Enable "Unicast Mode" in device config
2. In lighting software, set destination IP to your device's address

---

## Priority System

E1.31 supports priority levels (0-200) for multi-source scenarios:

- **Higher priority wins** when multiple sources transmit
- **Equal priority:** last source takes over
- **Sources timeout** after 2.5 seconds of inactivity
- **Default priority:** 100

This allows backup sources or priority overrides.

---

## Technical Specifications

| Specification | Value |
|---------------|-------|
| Protocol | E1.31 (ANSI E1.31-2018) |
| Transport | UDP port 5568 |
| Multicast Address | `239.255.{hi}.{lo}` |
| Channels per LED | 3 (RGB) |
| Max Channels/Universe | 512 |
| Max Universes | 8 |
| Max Tracked Sources | 4 |
| Source Timeout | 2.5 seconds |
| Data Timeout | 5 seconds (falls back to effects) |

### E1.31 Feature Support

| Feature | Supported | Notes |
|---------|-----------|-------|
| Multicast reception | ✅ | Standard E1.31 |
| Unicast reception | ✅ | Direct IP |
| Multi-universe | ✅ | Up to 8 consecutive |
| Priority handling | ✅ | 0-200, highest wins |
| Source tracking | ✅ | Up to 4 simultaneous |
| Sequence checking | ✅ | Out-of-order rejection |
| Preview flag | ✅ | Accept/reject preview |
| Stream termination | ✅ | Via timeout |
| Per-address priority | ❌ | — |
| Sync packets | ❌ | — |
| Universe discovery | ❌ | — |

---

## Compatible Software

| Software | Platform | Notes |
|----------|----------|-------|
| [QLC+](https://www.qlcplus.org/) | Win/Mac/Linux | Free, full featured |
| [xLights](https://xlights.org/) | Win/Mac/Linux | Free, great for sequences |
| [TouchDesigner](https://derivative.ca/) | Win/Mac | Visual programming |
| [Resolume](https://resolume.com/) | Win/Mac | VJ software |
| [MagicQ](https://chamsys.co.uk/) | Win/Mac/Linux | Professional lighting |
| Python `sacn` library | Any | `pip install sacn` |

---

## Python Examples

### Single Universe

```python
import sacn
import time

sender = sacn.sACNsender()
sender.start()
sender[1].multicast = True  # Universe 1

# Set all 160 LEDs to red
sender[1].dmx_data = [255, 0, 0] * 160
sender.activate_output(1)

time.sleep(5)
sender.stop()
```

### Multi-Universe with Unicast

```python
import sacn
import time

sender = sacn.sACNsender()
sender.start()

# Configure for 400 LEDs across 3 universes
DEVICE_IP = "192.168.1.100"
NUM_LEDS = 400

for uni in range(1, 4):
    sender[uni].multicast = False
    sender[uni].destination = DEVICE_IP
    sender[uni].priority = 100
    sender.activate_output(uni)

# Create RGB data for all LEDs
pixels = [(255, i % 256, 0) for i in range(NUM_LEDS)]  # Orange gradient

# Split across universes (170 LEDs each, 510 channels)
for uni_idx in range(3):
    start_led = uni_idx * 170
    end_led = min(start_led + 170, NUM_LEDS)
    
    dmx_data = []
    for led in range(start_led, end_led):
        if led < len(pixels):
            dmx_data.extend(pixels[led])
    
    # Pad to 512 channels
    dmx_data.extend([0] * (512 - len(dmx_data)))
    sender[uni_idx + 1].dmx_data = dmx_data

time.sleep(5)
sender.stop()
```

### Real-time Animation

```python
import sacn
import time
import math

sender = sacn.sACNsender()
sender.start()
sender[1].multicast = True
sender.activate_output(1)

NUM_LEDS = 160

try:
    while True:
        t = time.time()
        dmx_data = []
        
        for i in range(NUM_LEDS):
            # Moving rainbow
            hue = (i / NUM_LEDS + t * 0.5) % 1.0
            r = int((math.sin(hue * 6.28) * 0.5 + 0.5) * 255)
            g = int((math.sin(hue * 6.28 + 2.09) * 0.5 + 0.5) * 255)
            b = int((math.sin(hue * 6.28 + 4.19) * 0.5 + 0.5) * 255)
            dmx_data.extend([r, g, b])
        
        sender[1].dmx_data = dmx_data
        time.sleep(1/60)  # 60 fps

except KeyboardInterrupt:
    sender.stop()
```

---

## Troubleshooting

### No data received

1. Check that sACN is enabled in device config
2. Verify universe numbers match between software and device
3. Try unicast mode if multicast isn't working
4. Check firewall isn't blocking UDP port 5568

### Flickering or glitches

1. Enable unicast mode for more reliable delivery
2. Reduce frame rate in lighting software (30-44 fps is plenty)
3. Check for network congestion
4. Verify only one source is sending (or priorities are set correctly)

### Only first universe works

1. Increase "Universe Count" in device config
2. Verify lighting software is configured for multiple universes
3. Check that universes are consecutive (1, 2, 3... not 1, 5, 10)

### sACN overrides my effects

This is intentional! When sACN data is flowing, it takes priority. Normal effects resume after 5 seconds of no sACN data.
