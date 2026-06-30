# LUME Architecture

How the firmware is put together and the invariants that keep it correct. For
day-to-day building and contributing, see [DEVELOPMENT.md](DEVELOPMENT.md); for
adding an effect, see [ADDING_EFFECTS.md](ADDING_EFFECTS.md).

> A longer pre-migration design proposal lives in
> [archive/ARCHITECTURE.md](archive/ARCHITECTURE.md). This document describes the
> system as it is actually built today.

---

## Big picture

```
   Web UI / REST / MQTT / AI prompt        sACN / E1.31 (network task)
                │                                    │
                ▼ enqueue Command                    ▼ write to ProtocolBuffer
        ┌───────────────────┐                ┌──────────────────┐
        │   CommandQueue     │                │   IProtocol(s)    │
        │ (FreeRTOS, 1 slot  │                │  ready-flag +     │
        │  writer = loop)    │                │  double buffer    │
        └─────────┬─────────┘                 └────────┬─────────┘
                  │  (consumed on the loop task only)   │
                  ▼                                      ▼
        ┌──────────────────────────  LumeController  ──────────────────────────┐
        │  update(): commands → nightlight → protocols → render segments → show │
        └───────────────────────────────────┬──────────────────────────────────┘
                                             ▼
                              Segment[] → SegmentView → effect fn → CRGB leds[]
```

Everything that mutates LED state funnels onto the loop task. Web handlers and
protocols never touch `leds[]` or segment state directly — they hand work to the
loop, which is the single writer.

---

## Core invariants

These are load-bearing. Breaking one reintroduces a class of bug.

### Invariant 1 — Segments are contiguous ranges

A `Segment` maps to a contiguous `[start, start+length)` slice of the LED array,
accessed through a [`SegmentView`](../src/core/segment_view.h). The view hides
position and reversal so effects address pixels as `view[0 .. size()-1]` and never
do raw pointer math — leaving room for non-contiguous mapping (matrix, serpentine)
later without touching effect code.

Overlap is *allowed* but not blended: overlapping pixels resolve last-writer-wins
(segments render in array order). True compositing waits on blend modes
(`BlendMode::Add/Average/Max` are defined but `blendSegment()` is still a stub).

### Invariant 2 — Single-writer model via a command queue

All state mutations flow through one point
([`command_queue.h`](../src/core/command_queue.h)). Web handlers, protocols, and
the AI path enqueue a fixed-size `Command`; the loop task dequeues and applies it
at the top of each frame. Effects *read* state and write only their own pixels;
they never mutate shared controller state.

The queue is a FreeRTOS queue (16 deep) with a **newest-wins** overflow policy:
when full, the oldest command is dropped so a flood of slider updates can't stall
fresh input. This is why effects stay deterministic despite AsyncWebServer running
handlers on a different task.

### Invariant 3 — Fixed, aligned per-segment scratchpad

Stateful effects (fire, rain, scanner, …) keep frame-to-frame state in a
fixed-size scratchpad that lives *inside each `Segment`*
([`segment.h`](../src/core/segment.h)), so the same effect on two segments never
shares state. Properties:

- **Size:** `SCRATCHPAD_SIZE` bytes per segment (see
  [`segment_view.h`](../src/core/segment_view.h)). `EffectInfo::stateSize` must be
  `<= SCRATCHPAD_SIZE`; `SegmentView::getScratchpad<T>()` enforces both the size
  and the **alignment** (`alignof(T) <= SCRATCHPAD_ALIGN`) at compile time. The
  buffer is declared `alignas(SCRATCHPAD_ALIGN)` so reinterpreting it as a struct
  with `uint32_t` fields can't fault on Xtensa/RISC-V.
- **Reset:** changing a segment's effect bumps `scratchpadVersion` and zeroes the
  buffer. The effect is told via the `firstFrame` flag, derived from a version
  mismatch (`lastSeenVersion != scratchpadVersion`) — no separate "did I reset?"
  bookkeeping to desync.
- **Cost:** `SCRATCHPAD_SIZE × MAX_SEGMENTS` of static RAM, no heap.

---

## The effect contract

An effect is a pure function:

```cpp
void effectName(SegmentView& view, const ParamValues& params,
                uint32_t frame, bool firstFrame);
```

- **Own your canvas.** Each frame, an effect must establish *all* of its pixels —
  either fully fill the view (`view.fill`, `view.rainbow`, a full loop) or fade the
  previous frame (`view.fade`) and draw on top. The render loop does **not**
  pre-clear a segment's pixels; it only blacks out LEDs *not* covered by any active
  segment (`clearUncoveredLeds()`). This is what lets fade-trail effects (confetti,
  sinelon, comet, …) accumulate history across frames.
- **Use `frame` for timing**, initialize state when `firstFrame` is true, and keep
  output deterministic for a given input.
- **Register** with `REGISTER_EFFECT_SCHEMA(...)`, passing the real
  `sizeof(State)` so the registry can validate it fits the scratchpad.

See [ADDING_EFFECTS.md](ADDING_EFFECTS.md) for a worked example.

---

## Render pipeline (`LumeController::update`)

1. **Frame gate** — bail unless enough time has passed for the target FPS.
2. **Process commands** — drain the queue (single-writer mutations).
3. **Nightlight** — ramp global brightness if a fade is active.
4. **Power / protocol** — if powered off, clear & show; if a protocol is active
   (data within its 5 s timeout), it already wrote `leds[]`, so just show.
5. **Render segments** — clear only uncovered LEDs, then `segment.update(frame)`
   for each active segment in array order; per-segment brightness is applied after
   the effect runs.
6. **`FastLED.show()`** and advance the frame counter.

---

## Components

| Area | Files | Role |
|------|-------|------|
| Controller | `core/controller.*` | Owns `leds[]`, segments, timing, protocol registry |
| Segment / view | `core/segment.h`, `core/segment_view.h` | A strip region + its scratchpad and effect binding |
| Effects | `core/effect_registry.h`, `visuallib/effects/*` | Self-registering effect functions + schema metadata |
| Params / schema | `core/param_schema.h`, `core/effect_params.h` | Typed, schema-driven effect parameters and palettes |
| Commands | `core/command_queue.h` | Thread-safe single-writer mutation channel |
| Protocols | `protocols/*` | sACN/E1.31 as temporary sole writers behind `IProtocol` |
| Network/API | `network/*`, `api/*` | WiFi/OTA/mDNS, REST endpoints, web server |
| Storage | `storage.*` | NVS-backed config and last-effect persistence |

---

## Concurrency notes

- The loop task is the only writer of `leds[]` and segment state.
- Protocol receive callbacks run on **network tasks** — they write a
  `ProtocolBuffer` and set an atomic ready flag; the loop copies it into `leds[]`.
  Writing `leds[]` from a callback would tear frames and break Invariant 2.
- OTA/mDNS are set up once, when WiFi first comes up, via an idempotent path
  (`setupOTA()` guards re-registration), so reconnects don't duplicate services.
