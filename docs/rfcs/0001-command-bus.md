# RFC 0001 — Command Bus as the API (Matter-native)

**Status:** Draft · **Supersedes:** the unwired command-queue scaffolding ·
**Related:** `TECH_DEBT.md` (P0.1, P0.5, P1.1, P1.7), `DEAD_CODE.md` (decision #1),
`FUTURE_ARCHITECTURE.md` (the "Photon" direction)

## Summary

Make a single typed **command/attribute bus** the one true API. Every input —
HTTP, WebSocket, MQTT, the AI path, and (later) Matter and the browser emulator —
becomes a thin *adapter* that translates to/from one envelope. The render loop is
the sole consumer (single writer). This resolves five P0/P1 debt items at once and
makes the system Matter-shaped at zero extra cost.

## Problem

The audit found that the project's central decoupling seam — the command queue — was
**built but never wired** (`enqueueCommand` has zero producers). Every web handler
mutates `segments[]`/effects/`leds[]` **directly from the AsyncTCP task**, racing the
render loop (P0.1, real dual-core data races). On top of that:
- "A segment as JSON" has **three divergent shapes** (v1 inline, v2, WS broadcast),
  and the param codec is duplicated 4–6× (P1.1, P1.7).
- Segment enumeration confuses **slot index with ID** (P0.5).
- The whole API is welded to `ESPAsyncWebServer` — the ESP lock and the thing that
  makes the emulator a reimplementation instead of a shared contract.
- There is **no on-ramp to Matter**, which is on the roadmap.

These are one problem wearing five hats: *there is no single, typed, transport-
independent way to express "change this thing."*

## Goals / Non-goals

**Goals:** one mutation path (race-free, testable headless); one segment
representation; transport independence; **Matter-native by construction**; an
incremental migration with no big-bang rewrite.

**Non-goals (now):** shipping on-device Matter, leaving the ESP family, the 2D canvas
(RFC 0002), audio. This RFC makes those *cheap later*; it doesn't do them.

## Proposal

### 1. The envelope

Every mutation/query is one shape, deliberately isomorphic to Matter's
`(Endpoint, Cluster, Attribute|Command)` model:

```
BusMessage {
  endpoint : u16          // 0 = device/root; 1..N = segments
  cluster  : u16          // standard (On/Off 0x0006, Level 0x0008, Color 0x0300)
                          //   or manufacturer (LumeEffect 0x_FC10)
  op       : Read | Write | Invoke | Subscribe
  field    : u16          // attribute id or command id
  args     : payload      // typed, bounded
}
```

The **segment id IS the endpoint id** — one namespace, not two. This forces stable
IDs (fixes P0.5: enumerate by a real id, never by array slot).

### 2. Segment = Matter "Extended Color Light" (0x010D)

Each segment exposes the standard lighting clusters, which become the **rendered-state
projection** of whatever effect is running — so any controller (Apple/Google/Alexa,
the web UI, the emulator) sees a normal light:

| Concept | Cluster / attribute | Range |
|---|---|---|
| power | On/Off `OnOff` (0x0006/0x0000) | bool |
| brightness | Level Control `CurrentLevel` (0x0008/0x0000) | 1–254 (UI shows 0–100%) |
| color (HS) | Color Control `CurrentHue`/`CurrentSaturation` (0x0300) | 0–254 |
| color (XY) | `CurrentX`/`CurrentY` | 0–65279 |
| white | `ColorTemperatureMireds` + `ColorMode` | mireds |

The full effect engine (effect id, speed, intensity, palette, colors, reverse) lives
in **one manufacturer-specific cluster** `LumeEffect` (id in the `0xFC00–0xFFFE`
range, full code `0xVVVV_FC10`; dev VID `0xFFF1`). Standard controllers ignore the
unknown cluster gracefully (they enumerate via the Descriptor cluster) — so on/off/
dim/color stay fully interoperable while effects ride alongside, first-class in our
own adapters.

### 3. The `Command` carries an effect spec — the mutation-model decision

The reason handlers bypassed the queue: `Command` couldn't express a full
effect+params change. **Decision: wire the queue; extend `Command` to carry a bounded,
self-contained effect spec** (no dangling pointers — fixes the lifetime trap the audit
flagged):

```
SetEffectSpec {
  segmentId : u8
  effectId  : char[24]            // copied, not a borrowed const char*
  params    : ParamValue[8]       // fixed, typed, inline — no heap, queue-safe
  // brightness/power/color reuse the existing typed commands
}
```

All handlers (and MQTT, and AI `applySpec`) call `enqueueCommand(...)`; the loop is
the only mutator. ~40–50 B/Command × a 16-deep queue is a trivial RAM cost for
race-freedom. (Alternative considered: a small ref-counted spec pool — rejected as
more complexity than the fixed inline payload for 8 params.)

### 4. Attribute/state projection (subscribe, don't poll)

Pair commands with a **per-endpoint attribute snapshot** the loop publishes on change.
This is the single source of truth for: the WS broadcast, `GET` reads, Matter
attribute subscriptions, and the emulator. It replaces the three divergent serializers
with **one** `projectState(endpoint) -> attributes` (fixes P1.1/P1.7). `serializeSegments`
(persistence) becomes one more reader of this projection.

### 5. Transports are thin adapters

Each adapter only encodes/decodes the envelope:
- **HTTP:** `POST /endpoints/{id}/clusters/{cluster}/commands/{cmd}`, `GET/PUT …/attributes/{attr}`
- **WS:** the envelope as JSON frames; subscriptions stream attribute deltas
- **MQTT:** `lume/{endpoint}/{cluster}/{field}` (HA-discoverable)
- **Matter (later):** the *least* translation — the bus already speaks its model
- **Emulator:** a JS adapter speaking the same envelope over a bridge → the emulator
  shares the device's contract instead of reimplementing it

### 6. LED-HAL seam (from the FastLED-on-IDF analysis)

Effects keep using **FastLED's math primitives** (CRGB/palette/noise/blur) — universal
and platform-independent. Output goes through a thin interface so the driver is
pluggable per platform:

```
ILedOutput { void present(const CRGB* buf, size_t n); }
```

- Arduino/ESP builds: FastLED RMT5 controller.
- ESP-IDF/Matter build: IDF `led_strip` / I2S / SPI (I2S/SPI also dodges the
  **RMT-vs-WiFi jitter** that Matter-over-WiFi provokes).
- RP2350 (PIO), Teensy, WASM (the emulator): their own `present()`.

This keeps "FastLED math everywhere" (its real strength) without betting the Matter
firmware on FastLED's IDF *output* packaging.

## How this pays down the debt

| Resolves | How |
|---|---|
| P0.1 races | One writer; handlers enqueue, never mutate |
| P0.5 slot-vs-id | endpoint id = stable segment id |
| P1.1 / P1.7 dup + dual API | one `projectState` + one envelope; v1 routes become deprecated adapters |
| DEAD_CODE #1 | the queue stops being dead — it becomes the spine |
| Web lock (P3) | API defined by the bus; AsyncWebServer is one adapter |

## Migration plan (incremental — no big-bang)

1. **Extend `Command` + wire one path.** Add `SetEffectSpec`; route the segment
   create/update handlers through `enqueueCommand`. Prove the race is gone.
2. **Add native tests** (TECH_DEBT P0.10) for the bus + param codec round-trip — the
   safety net that lets the rest proceed.
3. **Migrate the remaining handlers** (pixels, config, nightlight, prompt, MQTT) onto
   the bus; collapse the three serializers into `projectState`.
4. **Introduce `ILedOutput`**; keep FastLED-RMT5 as the default impl.
5. **Freeze/redirect v1 routes**; delete the now-redundant duplicate serializers
   (DEAD_CODE deletion order).

Each step ships independently and pays for itself.

## Roadmap gates (explicit, from the Matter briefing)

- **Now (S3/C3, WiFi):** the bus + HTTP/WS/MQTT adapters, Matter-shaped. **Free.**
- **On-device Matter:** an **ESP-IDF migration** (Matter is IDF-native, ~1.5 MB flash,
  tight heap, its own task), Matter-over-**WiFi only**, **test VID `0xFFF1`**, and an
  explicit **RMT→I2S/SPI** output choice to survive WiFi jitter.
- **Thread / "Matter Certified":** a **hardware** gate (ESP32-C6, or H2 co-processor —
  S3/C3 have no 802.15.4) **and** a **process** gate (CSA membership + real VID +
  certification). Separately scoped; not incremental.

## Matter strategy: bridge-first (free), native-later (finances permitting)

You do **not** need on-device Matter — or ESP-IDF — to reach Matter ecosystems today.
Two tiers, same Matter-shaped bus:

- **Tier 0 — Bridge (now, $0, stays on Arduino).** Keep the Arduino build and expose
  the bus over **MQTT with Home Assistant auto-discovery** (LUME already has the MQTT
  plumbing). HA then bridges LUME to Apple Home / Google / Alexa via its HomeKit/Matter
  bridge. No `esp-matter`, no IDF migration, no CSA fees, no flash/heap hit — the only
  cost is "needs a hub," which most target users already run. Because the bus mirrors
  the cluster model (segment = endpoint; on/off/level/color + a `LumeEffect`
  namespace), the MQTT/HTTP semantics already line up. **This is the prep.**
- **Tier 1 — Native Matter (later).** A device-side Matter adapter on the same bus.
  This is the tier that costs: the ESP-IDF migration, ~1.5 MB flash + heap, and — to
  ship "Matter Certified" — CSA membership + a real VID + certification. Deferred
  behind the bus seam, it's a thin, well-scoped addition, not a rewrite.

**Net:** be a great MQTT/HTTP citizen now (Arduino, light, free); native Matter becomes
an optional upgrade, not a prerequisite. **Avoiding ESP-IDF = choosing Tier 0** — and
the Matter-shaped bus makes that choice free of regret.

## Risks / open questions

- **RMT + WiFi jitter** under Matter-over-WiFi — mitigate via I2S/SPI output; verify on
  hardware.
- **Endpoint count:** one endpoint/segment is spec-correct but ecosystems are tested
  ~≤8; keep zone counts modest, consider a Groups/aggregator framing for many zones.
- **`Command` size vs queue depth** — confirm the fixed param payload is acceptable; if
  effects grow params, revisit the spec-pool alternative.
- **Composed-device UX** varies by ecosystem (Apple rough, Google good) — don't rely on
  any one app's grouping.

## Decision required

Adopt the bus as the **single mutation path** with the envelope above, and commit to
**wiring (not deleting)** the command queue with an extended `Command`. Everything else
in this RFC follows from that one decision.
