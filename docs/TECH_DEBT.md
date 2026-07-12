# LUME — Technical Debt Register

Current state as of PR #26. The structural audit that this register grew out of has
been **worked off**: every **P0** (correctness / memory-safety / races) and every
**P1** (2D pre-req seam) item is resolved.

> The original six-auditor audit (full P0/P1/P2/P3 detail, kept for its historical
> value) is archived at **`archive/TECH_DEBT.md`**. The P-numbers referenced from
> other docs (e.g. `API_V2.md`, the RFCs) point into that archived register.

## Resolved — the whole P0/P1 backlog

Confirmed in-tree (grep/read against current `src/`):

| # | Item | Where it landed |
|---|---|---|
| P0.1 | Command bus wired (was dead) — mutations enqueue, applied on the loop task | `enqueueCommand` has ~13 producers; `controller.cpp` processes them |
| P0.2 | `ledCount` clamped to `[1, MAX_LED_COUNT]` | `storage.cpp:203` (and `:31`) |
| P0.3 | Concurrent request bodies rejected (409), no shared static buffers | `segments.cpp:213/292/495`, `pixels/nightlight/config/prompt` |
| P0.4 | Anthropic call offloaded to a FreeRTOS worker, returns 202 | `prompt.cpp:255-264` (`ensureAiWorker`) |
| P0.5 | `getSegmentByIndex()` added; serialize by `getId()` | `controller.cpp:346`, used in `server.cpp` / `segments.cpp` |
| P0.6 | `setColor(colorIdx, …)` maps to the correct slot | `segment.h:156-163` |
| P0.7 | `/api/prompt` rate-limited (429) | `prompt.cpp:284` |
| P0.9 | Length-aware body read (`String(data, len)`) | config/segment handlers |
| P0.10 | Native Unity suite exists | `[env:native]`; 7 suites, **37 cases green** |
| P1.1 | Single param codec | `core/param_codec.h` |
| P1.2 | `EffectDims { OneD, TwoD, Any }` on `EffectInfo` | `effect_registry.h:65-90` |
| P1.3 | `Region` seam extracted from `Segment` | `core/region.h` |
| P1.4 | Primitives via `operator[]`; `raw()` off the effect contract | (PR #22) |
| P1.5 | Tiered scratchpad — shared workbuffer for large/2D state | `core/segment.h` (640 B pad + workbuffer, RFC 0002) |
| P1.7 | One shared segment serializer | `core/segment_serializer.h` |
| P1.8 | `effects_handler.cpp` deleted | — |

The old P0.8 (`setLedCount` re-bind / teardown race) is addressed via the
`reconfigureProtocols` command run on the loop task (`controller.cpp:281`).

---

## Recently resolved (this branch)

- **Scenes — removed.** The dead end-to-end half-build (Storage scene backend,
  the inert `SaveScene`/`LoadScene` command stubs, and the 404-ing scenes UI) was
  deleted rather than wired — the legacy `jsonSpec` backend predated the
  command/`EffectSpec` model, so a future preset feature would be rebuilt on the
  current model anyway. See `DEAD_CODE.md`.
- **Extended palettes — exposed.** `/api/v2/palettes` now lists all 12 built-in
  palettes, driven by a single `PALETTE_NAMES` table in `core/effect_params.h` with
  a `static_assert` that fails the build if the enum and the name table drift. The
  old hardcoded `for i in 0..7` in `api/segments.cpp` is gone.

## Still open

### MQTT bypasses the command bus (consistency, not a race)
`mqtt.cpp:314-336` mutates `getSegment(0)->setEffect/setSpeed/setIntensity` plus
`controller_->setPower/setBrightness` **directly** instead of enqueuing a command —
and only ever the first segment ("apply to first segment for now").
This is **safe today**: `mqtt.update()` runs on the loop task (`main.cpp:254`), same
task as the renderer, so there is no data race — but it is the one input path that
sidesteps the mutation spine. **Fix:** route it through `enqueueCommand` for
consistency (and to gain multi-segment addressing) when MQTT is next touched.

---

## Minor / opportunistic (P2–P3, still true but low priority)

- **Effect LED-caps** — `fire` bails above 600 LEDs (`fire.cpp:37`, `heat[600]`);
  `fireup`/`twinkle` cap at 300 (`fireup.cpp:24`, `twinkle.cpp:27`). None reaches
  `MAX_LED_COUNT`. Clamp+fill or surface the cap.
- **`clearUncoveredLeds()` is O(ledCount × segments)/frame** — nested scan every
  frame (`controller.cpp:491-506`). Replace with a gap sweep or recompute on layout
  change only.
- **sACN joins only the first universe** — `joinAllMulticast()` despite its name
  joins a single group (`sacn.cpp:384-388`); multi-universe multicast is incomplete
  (it warns and suggests unicast). Join all groups or document as unicast-only for
  multi-universe.
- **`saveLedState` silent failure** — `main.cpp:266` ignores the `false` return when
  a layout exceeds the NVS cap. Log/surface it.
- **`platformio.ini` per-env flag/lib_deps duplication** — hoist shared config into
  a base `[env]`.

Several older P2 doc-hygiene items are **already fixed** in source: `main.cpp`
comments now say "GPIO 2" and "Anthropic Claude" (not GPIO 21 / OpenRouter), and the
scratchpad is documented as 640 B (`segment.h:24`).

---

## What's genuinely solid (protect these)

The render core remains the strong part: pure `(view, params, frame, firstFrame)`
effect functions with compile-time scratchpad guards; the `IProtocol` /
`SegmentView` seams; a web-agnostic `core/` + `visuallib/` with no upward
dependencies; debounced versioned persistence; and well-validated sACN parsing.
