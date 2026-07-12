# LUME — Technical Debt Register

> **⚠️ HISTORICAL SNAPSHOT — archived.** This is the six-auditor structural audit as
> it stood at **PR #26**, *before* the cleanup landed. Every **P0** and every **P1**
> item below has since been resolved (PRs #7–#24) — the command bus is wired, all
> safety fixes shipped, the native test suite exists (37 cases), and the 2D pre-req
> seams (param codec, `EffectDims`, `Region`, de-`raw()`, tiered scratchpad,
> segment serializer) are all in place. Kept for historical record. **For what is
> still genuinely open see `docs/TECH_DEBT.md`.**

A structural audit before adding new features (notably 2D matrix). Produced by six
independent auditors, each owning one lens (seams/coupling, canvas/2D-readiness,
concurrency/state, memory/perf, robustness, code-health), with the headline
findings re-verified by hand. Confidence is noted as **(N agents)** where multiple
auditors independently reached the same finding.

## The meta-finding

**The render core is genuinely well-built; the debt and danger concentrate at the
API / web boundary.** Every auditor's "what's solid" list praised the same things —
the effect contract, the scratchpad's compile-time guards, the `SegmentView` and
`IProtocol` seams, the web-agnostic core, the debounced persistence. Every
auditor's *critical* findings were in `api/` and `network/`.

That is exactly the modularity thesis playing out: **the seams that were drawn
(effects, protocols, scratchpad, command queue *as a mechanism*) are clean; the one
seam that was never drawn — the web/input layer — is where entropy accumulated.**
The single most important finding is that the project's central decoupling seam (the
command queue) was *built but never wired up*, so the clean architecture is real on
paper and bypassed in practice.

---

## P0 — Fix before ANY new feature (correctness / memory safety / races)

### P0.1 — Command queue is dead code → cross-task data races **(4 agents + verified)**
`enqueueCommand` has zero producers (only its definition + a README diagram).
Every web/MQTT/AI handler mutates `segments[]`, effects, and `leds[]` **directly
from the AsyncTCP task**, concurrently with the render loop on the main task.
`removeSegment` shifts the segment array mid-iteration (`controller.cpp:307`);
`setEffect` `memset`s the scratchpad mid-`effect->fn()` (`segment.h:63`);
`/api/pixels` and the loop can both call `FastLED.show()`.
- **Decision required:** route all mutations through `enqueueCommand` (the queue is
  correct, but `Command` can't yet express schema params — that's *why* handlers
  bypass it; extend it), **or** delete the queue and add an explicit loop-task
  "apply pending" step. Do not ship 2D with both the dead queue and live races — 2D
  widens the window. **Effort: L**

### P0.2 — `ledCount = 0` → gradient out-of-bounds heap write **(verified)**
`config` accepts `ledCount` with no lower bound (`storage.cpp:231`, unlike the
`constrain`'d sACN fields); `/api/pixels` gradient does
`fill_gradient_RGB(leds, 0, …, ledCount - 1, …)` (`pixels.cpp:127`) where
`uint16_t 0 - 1 = 65535` → a 65k-entry write into `leds[1000]`. `constrain` ledCount
to `[1, MAX_LED_COUNT]` and guard the gradient branch. **Effort: S**

### P0.3 — Async request-body buffers are global statics → reentrancy corruption **(2 agents)**
`segmentCreateBuffer`/`configBodyBuffer`/`promptBodyBuffer`/… are file-scope
`static String`. AsyncWebServer interleaves chunks of concurrent requests → two
in-flight POSTs corrupt each other's body (a mixed config could be written to NVS).
Key the accumulator by request (`request->_tempObject`) or reject concurrent bodies
(409). **Effort: M**

### P0.4 — Blocking 30 s Anthropic HTTPS call on the AsyncTCP task **(2 agents)**
`callAnthropicAPI` runs synchronously inside the async handler (`prompt.cpp:240`),
freezing the entire web stack for up to 30 s and putting a large TLS arena on the
8 KB async stack. Offload to a FreeRTOS task (the `ANTHROPIC_TASK_*` constants
already exist) and return 202. **Effort: M**

### P0.5 — Segment enumeration confuses slot-index with ID **(verified)**
List endpoints + WS broadcast loop `getSegment(i)` for `i=0..7`, but `getSegment`
looks up by **ID** (`controller.cpp:298`); IDs are reused/compacted, so after
deleting a segment the survivors vanish or get mislabeled. This is the foundation 2D
builds on. Add `getSegmentByIndex()` / an iterator; serialize `seg->getId()`.
**Effort: S**

### P0.6 — `Segment::setColor(colorIdx, …)` ignores `colorIdx` **(verified)**
Writes every color to the *first* matching param slot and returns (`segment.h:125`).
AI multi-color (`prompt.cpp` loops `setColor(0..2)`) all clobber one slot. Map
`colorIdx` to an ordered color slot. **Effort: M**

### P0.7 — No rate-limit on `/api/prompt` → unauthenticated DoS + billing burn
`PROMPT_RATE_LIMIT_MS` is defined and never used; auth is optional (empty token →
open). Combined with P0.4, one client can monopolize the device and burn API
credits. Enforce it (429). **Effort: S**

### P0.8 — `setLedCount` doesn't re-bind FastLED + races the loop
`/api/config` changes `ledCount` but never re-runs `addLeds`; the strip is wrong
until reboot, the "invalidate out-of-range segments" loop is an empty no-op
(`controller.cpp:73`), and it writes `leds[]` / reconfigures sACN+MQTT sockets from
the web task. Require a reboot (or re-`addLeds`) and clamp segments. **Effort: M**

### P0.9 — `config.cpp` over-reads the request chunk
`configBodyBuffer += String((char*)data)` runs `strlen` past `len` (chunk isn't
NUL-terminated). Use the length-aware `String((char*)data, len)` like the other
handlers. **Effort: S**

### P0.10 — Zero automated tests; CI is build-only **(do early)**
`test/` is curl-against-a-live-device scripts that assert nothing; CI only compiles.
The most testable + most-broken logic (segment model, persistence round-trip, the
param codec, `setColor`, effect caps, sACN parse) is uncovered — several P0 bugs are
trivially unit-testable. Add a native Unity `[env:native]`; start with
segment-persistence round-trip + the param codec. **Effort: L (high ROI)**

---

## P1 — Fix before 2D specifically (the keystone seams)

### P1.1 — Param (de)serialization duplicated 4–6× **(2 agents)**
The `switch(ParamType)` JSON↔`ParamValues` loop is copy-pasted across
`segments.cpp` (×3), `controller.cpp` (×2), `server.cpp`, and the dead
`effects_handler.cpp` — and already drifting (Int truncated to `uint8_t`; Palette
skipped inconsistently). Extract `paramsToJson` / `paramsFromJson` into one codec
**before** adding a 2D coordinate param type. **Effort: M**

### P1.2 — No dimensionality metadata on `EffectInfo`
Add `EffectDims { OneD, TwoD, Any }` (with a default) so a matrix build can filter
effects and refuse 1D-only ones. Adding it later churns all 23 registration sites —
do it now with a default. **Effort: S**

### P1.3 — `Segment` is a 1D interval, fusing three concerns
Physical layout + logical canvas + region are fused into `start+length`. Introduce a
`Region` (a `Range` today, a `Rect` tomorrow) shared by coverage, persistence, and
the view. This is the seam that unblocks 2D **and** fixes "segments suck." **Effort: M**

### P1.4 — `raw()` + FastLED passthrough defeat the 2D abstraction
15 of 23 effects use `fade`/`blur`/`fill`/`gradient` via `raw()` = a flat pointer +
run-length over physically contiguous memory. Any serpentine/tiled remap breaks
them. `operator[]` is the *only* remap-safe path. Reimplement the primitives via
`operator[]` (or gate them to a 1D effect tier) and remove `raw()` from the
effect-facing contract. **Effort: M**

### P1.5 — Scratchpad (640 B) too small for 2D state
A 32×32 grid needs ≥1024 B; per-pixel 2D effects (2D fire, particles) blow the fixed
budget. Decide the 2D-effect state strategy (dims-gated larger pad, or a single
shared workbuffer a full-canvas matrix segment borrows) before promising matrix
effects. **(3 agents)** **Effort: M**

### P1.6 — Scenes feature dead end-to-end, but the shipped UI 404s **(verified)**
`Storage` has a full scene backend (unused); `app.js` POSTs `/api/scenes/{id}/apply`
(`app.js:1247`); no route exists; `SaveScene`/`LoadScene` commands are stubs.
Implement it or remove the UI control + Storage methods. **Effort: M**

### P1.7 — Dual v1/v2 API + 3 divergent segment JSON shapes **(2 agents)**
`/api/segments` (v1, inline in `server.cpp`), `/api/v2/segments`, and the WS
broadcast each serialize a segment differently. Pick v2, freeze/redirect v1, route
all of them through the single shared serializer from P1.1. **Effort: M**

### P1.8 — `effects_handler.cpp` is orphaned dead code **(2 agents)**
Compiled, never routed, a duplicate (and buggy — unpadded hex) schema serializer.
Delete it. **Effort: S**

---

## P2 — Clean up opportunistically

- **`clearUncoveredLeds()` is O(ledCount × segments)/frame** — replace with a gap
  sweep, or recompute only on layout change. **(2 agents)** S–M
- **Effect LED-caps** — `fire` renders black above 600 LEDs; `fireup`/`twinkle`
  freeze past 300; none matches `MAX_LED_COUNT`. Clamp+fill or surface the cap.
  **(4 agents)** M
- **`String` heap-fragmentation hardening** — `reserve(total)` + chunk-append in the
  segment handlers, stream `serializeJson` to the response, `snprintf` NVS keys.
  **(2 agents)** M
- **`saveLedState` silent failure** — `main.cpp:243` ignores the `false` return when
  a layout exceeds the 4000 B NVS cap; log/surface it. S
- **Palette API exposes 7 of 12** — `/api/v2/palettes` hardcodes 7; `paletteName()`
  is dead; README claims 12. Drive it from the enum. S
- **"1000 LEDs @ 60 fps" is ~2× optimistic** — single-pin WS2812B caps ~30 fps at
  1000 LEDs (60 fps only to ~500). Fix the doc or implement parallel output. S (doc)
  / L (parallel)
- **`platformio.ini` repeats every env's flags/lib_deps 3×** — hoist into a base
  `[env]`. S
- **Stale docs/comments** — `main.cpp:13` "GPIO 21" (actual 2), "OpenRouter" (actual
  Anthropic), module READMEs (512 scratchpad, `REGISTER_EFFECT_PALETTE`,
  `EffectParams`). S
- **`IProtocol`/`Protocol` double-decker boilerplate**; MQTT isn't even a `Protocol`.
  Collapse to one interface; reconsider MQTT as an *input* (behind the mutation
  path), not an LED-writing protocol. M
- **`blendSegment`/`BlendMode` is dead public surface** — implement (needed for 2D
  overlap anyway) or remove. S/M
- **Dead constants & fields** — `ANTHROPIC_TASK_*`, `SYSTEM_PROMPT_BUFFER_SIZE`,
  `MAX_JSON_STATE_SIZE`, `nextSegmentId`, the `setLedCount` no-op loop, `PromptSpec`
  storage, the dangling `extern commandQueue` (declared, never defined). S
- **`removeSegment` copy-by-value shifts** a 640 B scratchpad per segment — make
  `Segment` non-copyable / handle-based when the region model lands. M

---

## P3 — Accept / won't-fix (noted, with rationale)

- **Web/transport seam (the ESP lock)** — real, but L effort and only worth it if
  non-ESP (RP2350, etc.) becomes a goal. Defer until that need is concrete.
- **sACN multi-universe multicast only joins universe 0** — functional limit, not a
  crash; join all groups or document as unicast-only for multi-universe. S
- **Hex-color parse leniency**, **duplicate `fire`/`fireup`**, **sACN 1360 vs
  `MAX_LED_COUNT` 1000 inconsistency**, **cross-layer singletons** — minor; address
  if touched.

---

## What's genuinely solid (protect these)

- **Effect contract** — pure `(view, params, frame, firstFrame)` functions, no
  mutable global/static state, self-registering via `REGISTER_EFFECT_SCHEMA`. Adding
  an effect is one file. The *signature names no dimension* — it survives the 2D move.
- **Scratchpad safety** — `alignas` + compile-time `static_assert` on size *and*
  alignment in `getScratchpad<T>()`; the `firstFrame`-from-version-mismatch trick is
  desync-proof.
- **Seams that exist** — `IProtocol` (sACN follows the single-writer rule correctly,
  with a proper atomic double-buffer); the `SegmentView` indirection; the
  schema-driven param system.
- **Web-agnostic core** — `core/` and `visuallib/` have zero upward dependencies on
  web/network/storage. The dependency direction is correct; the violations are leaf
  layers reaching *down* past the (unused) queue.
- **Persistence** — debounced autosave, `suppressDirty_` guard, versioned schema,
  graceful fallback. No heap allocation in the render hot path.
- **sACN packet parsing** — the best-validated input path in the codebase.

---

## Recommended sequence

1. **Decide the mutation model (P0.1).** Wiring the command queue (or an explicit
   loop-task deferred-apply) dissolves ~5 race findings at once. This is the
   foundation; everything else is safer after it.
2. **Land the quick safety fixes** — P0.2 (ledCount bound + OOB), P0.9 (over-read),
   P0.7 (rate-limit), P0.5 (segment enumeration), P0.6 (`setColor`).
3. **Add native tests (P0.10)** for what you're about to refactor — persistence
   round-trip, the param codec, `setColor`. Cheap insurance against regressions.
4. **Consolidate serialization (P1.1)** and draw the 2D pre-req seams — `EffectDims`
   (P1.2), `Region` (P1.3), de-`raw()` the canvas (P1.4), scratchpad strategy (P1.5).
5. **Then 2D is a weekend on a sound base** rather than the first crack of entropy.
