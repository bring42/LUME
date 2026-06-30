# LUME — Future Architecture Study

A from-first-principles exploration of the best structure for a modular,
Project-ARA-inspired, open-source WLED successor — deliberately not anchored to
LUME's current shape, then compared back. Prior art was checked against WLED /
MoonModules, **Pixelblaze**, LedFx, ESPHome, Hyperion/FPP, and **FastLED's WASM +
ScreenMap** work. See `ARCHITECTURE.md` for what exists today and `TECH_DEBT.md` /
`DEAD_CODE.md` for the debt this would resolve.

> **Two facts confirmed against primary sources:** Pixelblaze compiles patterns to
> bytecode run by an on-device VM; FastLED has a maintained WASM/Emscripten browser
> target with hot-reload. **Two facts flagged as memory, worth a check before
> committing:** WASM-per-pixel-at-60fps viability on today's ESP32, and FastLED
> ScreenMap maturity.

---

## Design space (the load-bearing decisions)

**1. Effect model.** Native function-pointer registry (LUME/WLED) = max perf, but
adding an effect needs a reflash. Embedded scripting VM (Pixelblaze) = proven
live-edit + emulator parity, but you own a language forever. WASM modules =
language-agnostic + OTA-of-effects + true device/browser parity, *but* interpreted
WASM is ~3–15× slower per-pixel on Xtensa (AOT closes it but reintroduces a
toolchain) — **not proven for per-pixel at 60fps**. DSL = tiny + safe but a low
expressiveness ceiling.
→ **Winner: a two-tier model** — keep the native tier (perf, FastLED-backed) for
built-ins; add a **portable tier** for community contribution + emulator parity.
Start the portable tier **scripted** (Pixelblaze-proven), behind a host-ABI seam
that WASM can target later.

**2. Canvas / pixel mapping.** 1D interval + bolted-on XY (WLED/LUME) vs. a
**decoupled pixel-map layer** where effects address **normalized coordinates
`(x,y[,z]) ∈ [0,1]`** and a mapper owns the wiring (Pixelblaze mapper + FastLED
ScreenMap prove this) vs. a layered compositor with blend modes.
→ **Winner:** normalized-coordinate canvas + pixel-map layer + thin compositor.
This is "1D/2D/3D for free" instead of bolted on. LUME's `SegmentView` is *already*
the right seam — the gap is that `raw()` + FastLED passthrough (15/23 effects)
bypass it (`TECH_DEBT.md` P1.4).

**3. Concurrency.** Single-writer command/event loop (LUME's *design*) is
textbook-correct; the actor model is overkill for one render loop (but right for
*off-loop* work — audio FFT, the blocking AI call).
→ **Winner: keep single-writer.** LUME's issue is non-architectural: the queue is
built but unwired. Task layout: render task (sole `leds[]` writer) · net task
(enqueues, never touches pixels) · audio task (FFT double-buffer) · protocol task
(pixel double-buffer + ready flag — LUME's existing `IProtocol` pattern).

**4. Portability / HAL (the ESP lock).** Stay ESP-first (accept the
`ESPAsyncWebServer` wall) vs. a **platform-agnostic `core/` + thin HALs**
(`ILedOutput`, `ITransport`, `IStorage`, `IClock`) vs. host-side compute / dumb
device (LedFx/Hyperion).
→ **Winner: agnostic `core/` + HALs — and it's cheaper than it looks**, because
**FastLED already abstracts LED output across ESP RMT/I2S, RP2350 PIO, Teensy, and
WASM.** So lean on FastLED as the output HAL; the only net-new work is a *transport*
seam to replace AsyncWebServer. RP2350/Pi then becomes "implement two interfaces,"
not a fork.

**5. Transport / API.** The embedded HTTP server *being* the API (WLED/LUME) couples
the contract to the web framework and forces the emulator to re-implement HTTP.
→ **Winner: the command bus *is* the API; transports (HTTP/WS/MQTT/serial/the
emulator's JS bridge) are leaf adapters.** This kills three LUME debts at once (dead
queue, 3 divergent JSON shapes, web-coupling) *and* makes the emulator and device
share one contract by construction — they share a command schema, not an HTTP server.

**6. Distribution / ecosystem.** Compile-time usermods (WLED) = full power, high
friction. **Config-as-data** (presets/palettes/mappings as data) + a **portable
effect-package format** = a marketplace that's just a file repo, with OTA-of-effects.
→ **Winner:** config-as-data now; effect packages (scripted, WASM later) as the
portable tier matures.

---

## Candidate architectures

- **C1 — "LUME-Evolved":** finish the seams in place, stay ESP-locked. *Too timid* —
  leaves you at WLED's dead-end (a fine effect engine welded to AsyncWebServer and a
  1D mental model; every ambition fights the grain).
- **C2 — "Photon":** platform-agnostic `core/` (no Arduino/ESP types) = render loop +
  command-bus-as-API + normalized-coordinate canvas + pixel-map + compositor +
  registry; thin HALs (FastLED for output, transport + storage interfaces); two-tier
  effects; the browser emulator **is the WASM port of the same core**. **← the pick.**
- **C3 — "Conductor":** thin smart-device + a companion host service (host-side FFT à
  la LedFx, AI generation, multi-device sync à la FPP). Most capable, most expensive,
  arguably a different *product*. The 3-year horizon, not the next move.

## Comparison (●●● strong · ● weak; migration-cost is *from LUME*)

| Criterion | C1 Evolved | C2 Photon | C3 Conductor |
|---|---|---|---|
| Modularity | ●● | ●●● | ●●● |
| Portability (ESP/RP2350/Pi/browser) | ● | ●●● | ●●● |
| Contributor-friendliness | ●● | ●●● | ●●● |
| Perf on MCU | ●●● | ●●● native / ●● script | ●●● (offload) |
| 2D/3D | ●● | ●●● | ●●● |
| Audio-reactive | ● | ●● | ●●● |
| Emulator parity | ● | ●●● | ●●● |
| Migration cost from LUME | ●●● (cheap) | ●● (bounded) | ● (large) |

C1 caps out exactly where the goals live (portability, parity, audio). C3 is a
different product. **C2 is the only column with no ● in the capability rows that
matter — at a bounded cost, precisely because FastLED already solved the hardest
sub-problem (LED output across MCUs *and* WASM).**

---

## Verdict

**Evolve — do not re-found — but along the Photon (C2) trajectory, not C1's
"harden in place."**

Keep, untouched (every auditor confirmed these are near-optimal):
- the **single-writer render loop** (just *wire* it),
- the **pure-function effect contract** `(view, params, frame, firstFrame)` — names
  no dimension, survives every candidate,
- the **scratchpad** (alignment-guarded; resize/strategize for 2D),
- the **`SegmentView` / `IProtocol`** seams.

Adopt, because materially better:
1. **command bus *as* the API** (kills dead queue + divergent JSON + web-coupling; one
   contract for device and emulator),
2. **normalized-coordinate canvas + pixel-map layer** (1D/2D/3D for free),
3. **platform-agnostic `core/` behind FastLED + a transport HAL** (portability at
   bounded cost),
4. **a portable scripted effect tier** behind a host-ABI seam whose browser target
   *is* the emulator.

**Why not re-found:** the hardest thing to get right — the real-time render core,
the effect contract, the concurrency model — LUME *already got right*. A rewrite
discards the actual asset to re-derive the same loop. The debt that exists is
concentrated at the web/API boundary, **which the Photon path replaces anyway** — so
the rewrite you'd fear is mostly the rewrite you'd want. **Why not C1:** staying
ESP-locked forfeits every stated ambition (RP2350/Pi, browser emulator, true 2D/3D,
audio) to fight the grain forever.

### Suggested sequence (each step pays for itself)
1. Wire the command queue → make it the API (resolves the P0 races + the dead queue).
2. Add native tests (the unlock for safely scrubbing the residue).
3. Consolidate serialization + extract a platform-agnostic `core/` behind FastLED +
   a transport HAL.
4. Replace the segment-interval model with a normalized-coordinate canvas + pixel-map.
5. Add the portable scripted effect tier; its browser target becomes the emulator.
6. *Then* 2D/3D and audio land on a foundation built for them.
