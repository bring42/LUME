# LUME Roadmap

Current direction and what's left. For the full cross-PR history of how the codebase
got here, see `docs/HARDENING_LOG.md`.

**Direction: evolve LUME, do not rebuild.** After a deep multi-agent audit (merged
≈2026-06-30), the verdict held: the render core is solid (single-writer loop,
pure-function effect contract, scratchpad guards, `SegmentView`/`IProtocol` seams).
The debt was concentrated at the web/API boundary — scar tissue from a mid-project
"core swap" — and has since been paid down.

**Planning docs (source of truth — this file is just the map):**
`docs/TECH_DEBT.md`, `docs/DEAD_CODE.md`, `docs/FUTURE_ARCHITECTURE.md` (evolve along
a "Photon" path: command-bus-as-API + platform-agnostic core behind FastLED-math +
transport HAL + normalized-coordinate canvas), `docs/rfcs/0001-command-bus.md`,
`docs/rfcs/0002-scratchpad-strategy.md`, `docs/ARCHITECTURE.md`.

## Done and merged to `main`

(Don't re-do — see git log / PRs and `docs/HARDENING_LOG.md` for detail.)

- **All P0 races and safety bugs** — every live mutation path enqueues onto the
  single-writer command bus; ledCount OOB, body reentrancy, blocking AI call,
  enumeration, setColor, rate-limit, reboot-defer, config over-read all fixed.
- **P1.1–P1.7** — one canonical `serializeSegment` (hex colors everywhere), params
  routed through `param_codec`. **ILedOutput** HAL (`src/core/led_output.h`) so the
  controller drives LEDs only via a swappable `ILedOutput*`.
- **2D-canvas pre-reqs P1.2–P1.5** — `EffectDims` metadata, `core/region.h` geometry
  seam, de-`raw()`'d canvas, tiered scratchpad + shared workbuffer. 2D itself is now
  "a weekend on a sound base."
- **v1 `GET /api/segments` route removed** (#28) — v1 is gone, not just frozen.
- **Web UI redesign shipped** (#29) — console skin at `/` on one schema-driven
  engine. (A second `/euclid/` plate skin shipped then was later removed to reclaim flash.)
- **Hardware flash-verify DONE** — LILYGO T-Display S3 two-boot
  save→reboot→restore proven on-device; MQTT→Home Assistant verified against a local
  broker; Seeed XIAO ESP32-C3 hardware-runs the UI. Reboot still has no HTTP endpoint
  — use esptool over USB.

## What's left (no P0s remain)

- **Presets (formerly "Scenes")** — deferred future feature, not started. The day-one
  scene half-build (Storage backend + 404'ing UI) was orphaned by the mid-project core
  swap and removed in #27; a real version gets rebuilt on the command/`EffectSpec` model
  (+ eased crossfades once the easing engine lands). No scene code exists today.
- **P2/P3 cleanups** — `removeSegment` copy-by-value scratchpad aliasing is now
  defused in place (each shifted survivor re-binds its view to its own inline pad via
  `Segment::rebindScratchpad`, regression-tested in `test_command_bus`); making
  `Segment` non-copyable/handle-based remains an optional deeper cleanup. Still open:
  effect LED-caps; `clearUncoveredLeds` O(n·segs).
- **Matter** stays bridge-first (MQTT→Home Assistant, $0, Arduino); native on-device
  Matter deferred. A WIP HA branch is parked.

## Tooling

Native unit tests run host-side with `pio test -e native` (no hardware; CI runs them
before board builds). Each change touching a testable core seam ships with a guarding
test; extend the stubs in `test/stubs` as new symbols are needed.
