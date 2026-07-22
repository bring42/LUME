# LUME Hardening Log

A narrative record of the P0/P1 hardening-and-evolution arc (≈2026-06-30 → 07-22).
It intentionally overlaps with git history, but keeps the *why* and the cross-PR
story in one readable place. For live direction and what's still open, see
`docs/TECH_DEBT.md`, `docs/FUTURE_ARCHITECTURE.md`, and the RFCs under `docs/rfcs/`.

## Direction (the founding decision)

After a deep multi-agent audit (merged to `main` in PR #6, ≈2026-06-30), the
decision was: **evolve LUME, do not rebuild.** The render core is genuinely solid
(single-writer loop, pure-function effect contract, scratchpad guards,
`SegmentView`/`IProtocol` seams); the real debt and danger were concentrated at the
**web/API boundary** — most of it scar tissue from a mid-project "core swap" (commit
`3d91bbb`) that calcified because there were no tests to make deletion safe.

Planning docs that drove the work: `docs/TECH_DEBT.md` (prioritized P0–P3),
`docs/DEAD_CODE.md` (~650 LOC cut-list + safe deletion order),
`docs/FUTURE_ARCHITECTURE.md` (the "Photon" path: command-bus-as-API +
platform-agnostic core behind FastLED-math + a transport HAL + normalized-coordinate
canvas), `docs/rfcs/0001-command-bus.md`, `docs/ARCHITECTURE.md`.

## P0 bug fixes — ALL cleared (PRs #7–#16, ≈2026-07-01)

First wave (PRs #7–#11): P1.1 routed `segments.cpp` through `param_codec.h`; added
the `serializeSegments`↔`restoreSegments` round-trip test; DEAD_CODE deletions
items 1–6 (~368 LOC, kept `ANTHROPIC_TASK_*`/`PROMPT_RATE_LIMIT_MS` for P0.4/P0.7).
RFC command-bus steps 1 & 3 landed: `Command` carries a self-contained `EffectSpec`,
and segment CRUD + `/controller` (power/brightness) + nightlight + AI `applySpec`
enqueue instead of mutating from the AsyncTCP task. Migrated mutations return
**202 Accepted** (the UI ignores bodies and reconciles via GET/WS).

Full P0 map:
- **P0.1 races** — every live mutation path (segment CRUD #7, controller/nightlight/AI
  #8, protocols + pixels #15) now enqueues onto the single-writer bus.
- **P0.2** ledCount OOB clamp+guard #9.
- **P0.3** request-body reentrancy — a single-owner busy-guard with stale-reclaim,
  pure/host-tested `core/body_guard.h` #16.
- **P0.4** blocking AI call → FreeRTOS worker + 202 #14.
- **P0.5** slot-vs-id enumeration → `getSegmentByIndex` #11.
- **P0.6** `setColor` maps colorIdx→Nth color param #10.
- **P0.7** prompt rate-limit #9.
- **P0.8** ledCount reboot-defer #12 + sACN/MQTT reconfig via a payload-free
  `ReconfigureProtocols` command #15.
- **P0.9** config over-read #9.
- **Bonus** (agent-surfaced): Storage NVS recursive-mutex #13.

A 4-agent parallel investigation (P0.3/P0.4/P0.8/pixels) fed the last batch — the
pattern worked well. Native suite grew to **18 tests** (param_codec / persistence /
command_bus / body_guard).

## P1.7 + P1.1 — canonical serializer (PR #17)

One canonical `serializeSegment` in `src/core/segment_serializer.h`
(id/start/stop/length/reverse/brightness/effect-string/params-as-hex) now backs the
v2 endpoints and the WS broadcast, so the UI-consumed segment shape is singular and
colors are hex everywhere (the WS used to emit `[r,g,b]`; `app.js` accepts both). The
old v1 `/api/segments` was left frozen at the time as its own richer shape (effect
object + capabilities, UI-unused) but its param switch also routed through
`paramsToJson`, so no hand-rolled param-value serialization remained (P1.1 fully
closed; only the schema-*descriptor* metadata `schemaToJson` is hand-written, which is
correct). The versioned persistence serializer (`LumeController::serializeSegments`)
is a separate save format, unchanged. (v1 was later removed entirely in #28.)

## ILedOutput HAL (PR #18)

`src/core/led_output.h` (interface) + `fastled_output.h` (default RMT driver). The
controller drives LEDs only via `output_` (an `ILedOutput*`, default `FastLedOutput`),
swappable with `setLedOutput()`. FastLED *math* stays; only output packaging is
abstracted — this unblocks a later Matter RMT→I2S/SPI swap.

## LILYGO flash-verify DONE (≈2026-07-01) — the long-owed two-boot proof

Flashed clean over native USB, boots healthy (heap ~208 KB, no panic). Full HTTP
verify once on the same LAN (device @192.168.0.177 / lume.local, weak ~−72 dBm so
curl-with-retries was used): created a distinctive segment via `POST /api/v2/segments`
(→ `202 accepted`, the async bus response), confirmed it applied on the render loop,
waited for the debounced autosave, hard-reset via esptool, confirmed a REAL reboot
(uptime dropped 94s→33s), and the segment SURVIVED → **save→reboot→restore proven on
hardware.** Also validated on-device: the command bus end-to-end and the P1.7
canonical serializer (hex colors, `stop`, per-seg `brightness`). Reboot still has no
HTTP endpoint — hard-reset is via esptool over USB.

## 2D-canvas pre-reqs P1.2–P1.5 DONE (≈2026-07-01, PRs #20–#23)

Each shipped as its own tested PR.
- **P1.2 (#20)** — `EffectDims{OneD,TwoD,Any}` on `EffectInfo`, defaults to OneD via
  aggregate value-init (the board toolchain is gnu++11, so a default member
  initializer would stop `EffectInfo` being an aggregate and break the REGISTER
  macro); `REGISTER_EFFECT_SCHEMA_DIMS` variant + `runsOn()`/`dimsName()`; `dims` on
  `/api/v2/effects`.
- **P1.3 (#21)** — `core/region.h`: a `Region{start,length}` value type
  (contains/stop/end/size/empty), a Range today → Rect tomorrow. `SegmentView` holds
  a `Region` (not scalars); `reversed` stays in the view (pure geometry). Coverage +
  persistence + canonical serializer all route through it; JSON keys unchanged.
- **P1.4 (#22)** — de-`raw()`'d the canvas: the fill/fade/clear/gradient/rainbow
  primitives reimplemented via `operator[]` (the audit found contamination was almost
  all inside the wrappers; only 2 direct `raw()` sites remained — brightness pass + a
  meteor line). `raw()` DELETED from `SegmentView`; gradient/rainbow drop the manual
  reversed-swap since `operator[]` handles it.
- **P1.5 (#23)** — tiered scratchpad: kept the 640 B inline pad and added a
  controller-owned shared **workbuffer** that one canvas-spanning segment BORROWS
  (`borrowWorkbuffer`/`release`, single-owner, dropped on layout change);
  `getScratchpadChecked<T>()` runtime guard; `LUME_WORKBUFFER_SIZE` defaults 0 =
  feature off (1D pays a 1-byte placeholder), matrix builds set
  `-DLUME_WORKBUFFER_SIZE=<bytes>`; `MAX_EFFECT_STATE=max(pad,workbuffer)` relaxes the
  registry cap. Decision doc: `docs/rfcs/0002-scratchpad-strategy.md`.

Native suite reached **37 tests** (added test_effect_registry / test_region /
test_scratchpad). A two-agent parallel change-surface mapping (Region, raw()) fed
P1.3/P1.4.

## MQTT → Home Assistant verified on hardware (≈2026-07-02)

The bridge-first HA path was made real: fixed the HA json-schema state bug (published
`power` bool, HA needs `state:"ON"/"OFF"`), added RGB color (discovery
`supported_color_modes`, `color` in set/state via the first Color param slot),
speed/intensity in state, color in the change-detection hash, buffer 1024→1536 +
publish-failure warn. Found and fixed a nastier latent bug on hardware: MQTT enabled
at runtime via the web UI never published state/discovery — `mqtt.begin()` (which sets
`controller_` and generates clientId) was only called at boot when MQTT was already
enabled in NVS; `setConfig()` set neither. Fix: `main.cpp` always calls `begin()`
(even disabled), and `setConfig()` also generates the clientId. Verified end-to-end
against a local mosquitto (`allow_anonymous true` + `mosquitto_sub -t '#' -v` — a solid
recipe for future MQTT bench tests): discovery + state + OFF/ON/brightness/color/effect
commands all round-trip. Device MQTT config was restored to disabled afterwards (the
test broker was temporary). This work is parked on a WIP branch as a starting point,
not a finished feature.

## Web UI redesign shipped (≈2026-07-22, PR #29)

Two finished UI skins over one shared, schema-driven engine — console skin at `/`,
euclid skin at `/euclid/`. Merged alongside #28 (legacy v1 route removal) and #30
(Seeed XIAO ESP32-C3 board env + single-core C3 FastLED boot-loop fix), with #31 a
rotary-knob follow-up. See the project's UI notes for the engine contract and the
edit-`ui-concepts/`-not-`data/` workflow.

## Remaining (P1/P2/P3 — no P0s left)

- **P1.6 Scenes** — dead backend + a 404'ing UI; implement or remove.
- **P2/P3 cleanups** — `removeSegment` copy-by-value scratchpad aliasing (make
  `Segment` non-copyable/handle-based; the P1.5 borrow model sidesteps but doesn't fix
  it); effect LED-caps; `clearUncoveredLeds` O(n·segs).
- **2D itself** is now "a weekend on a sound base" — the four keystone seams
  (dims / Region / de-raw / workbuffer) are in.
- **Matter** stays bridge-first (MQTT → Home Assistant, $0, Arduino); native on-device
  Matter deferred.

## Tooling

Native unit tests run host-side with `pio test -e native` (no hardware; CI runs them
before the board builds) — **37 tests** across param_codec / persistence / command_bus
/ body_guard / effect_registry / region / scratchpad. Each change touching a testable
core seam ships with a guarding test; extend the stubs in `test/stubs` as new symbols
are needed.
