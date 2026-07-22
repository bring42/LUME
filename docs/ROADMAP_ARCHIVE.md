# Roadmap — Archived Original (memory snapshot, ≈2026-07-11)

> This is the original `lume-roadmap` agent-memory note, preserved verbatim before it
> was condensed. Kept as a historical record — the live/current versions are
> `docs/ROADMAP.md` (direction + what's left) and `docs/HARDENING_LOG.md` (the arc).
> `[[double-bracket]]` names below were links to sibling memory notes, not repo files.

---

After a deep multi-agent audit (merged to `main` in PR #6, ~2026-06-30), the
decision is: **evolve LUME, do not rebuild.** The render core is genuinely solid
(single-writer loop, pure-function effect contract, scratchpad guards, SegmentView/
IProtocol seams); all the real debt + danger is concentrated at the **web/API
boundary** — most of it scar tissue from a mid-project "core swap" (commit `3d91bbb`)
that calcified because there were no tests to make deletion safe.

**Read these in-repo docs first (all on `main`):** `docs/TECH_DEBT.md` (prioritized
P0–P3 findings), `docs/DEAD_CODE.md` (~650 LOC cut-list + safe deletion order),
`docs/FUTURE_ARCHITECTURE.md` (verdict: evolve along a "Photon" path — command-bus-as-
API + platform-agnostic core behind FastLED-math + a transport HAL + normalized-
coordinate canvas), `docs/rfcs/0001-command-bus.md` (the keystone RFC, Matter-native),
`docs/ARCHITECTURE.md`.

**Progress (session ~2026-06-30/07-01, merged to `main` in PRs #7–#11):** done —
P1.1 (segments.cpp routed through `param_codec.h`); the `serializeSegments`↔
`restoreSegments` round-trip test; DEAD_CODE deletions items 1–6 (~368 LOC, kept
`ANTHROPIC_TASK_*`/`PROMPT_RATE_LIMIT_MS` for P0.4/P0.7); **RFC command-bus steps 1
& 3** — `Command` carries a self-contained `EffectSpec`, and segment CRUD +
`/controller` (power/brightness) + nightlight + AI `applySpec` now enqueue instead of
mutating from the AsyncTCP task (P0.1 races killed for all live UI paths); P0.2
(ledCount OOB clamp+guard), P0.9 (config body over-read), P0.7 (prompt rate limit),
P0.6 (`setColor` maps colorIdx→Nth color param), P0.5 (enumerate segments by index
via new `getSegmentByIndex`, not by probing the id space). Native suite now 13 tests
(`test_command_bus`/`test_persistence`/`test_param_codec`); responses for migrated
mutations are **202 Accepted** (UI ignores bodies, reconciles via GET/WS).

**ALL P0s are now cleared** (PRs #7–#16, merged ~2026-07-01). Full map: P0.1 races —
every live mutation path (segment CRUD #7, controller/nightlight/AI #8, protocols +
pixels #15) now enqueues onto the single-writer bus; P0.2 ledCount OOB #9; P0.3
request-body reentrancy — a single-owner busy-guard with stale-reclaim, pure/host-
tested `core/body_guard.h` #16; P0.4 blocking AI call → FreeRTOS worker + 202 #14;
P0.5 slot-vs-id enumeration → `getSegmentByIndex` #11; P0.6 setColor #10; P0.7 prompt
rate-limit #9; P0.8 ledCount reboot-defer #12 + sACN/MQTT reconfig via a payload-free
`ReconfigureProtocols` command #15; P0.9 config over-read #9. Bonus (agent-surfaced):
Storage NVS recursive-mutex #13. Native suite is now **18 tests** (param_codec /
persistence / command_bus / body_guard). A 4-agent parallel investigation (P0.3/P0.4/
P0.8/pixels) fed the last batch — see the pattern worked well.

**P1.7 + P1.1 done** (PR #17): one canonical `serializeSegment` in
`src/core/segment_serializer.h` (id/start/stop/length/reverse/brightness/effect-
string/params-as-hex) now backs the v2 endpoints + the WS broadcast, so the UI-
consumed segment shape is singular and colors are hex everywhere (the WS used to
emit `[r,g,b]`; app.js accepts both). v1 `/api/segments` is left frozen as its own
richer shape (effect object + capabilities, UI-unused) but its param switch also
routes through `paramsToJson`, so **no hand-rolled param-value serialization
remains** (P1.1 fully closed; only the schema-*descriptor* metadata `schemaToJson`
is hand-written, which is correct). The versioned persistence serializer
(`LumeController::serializeSegments`) is a separate save format, unchanged.

**ILedOutput done** (PR #18): `src/core/led_output.h` (interface) + `fastled_output.h`
(default RMT driver); the controller drives LEDs only via `output_` (an `ILedOutput*`,
default `FastLedOutput`), swappable with `setLedOutput()`. FastLED *math* stays; only
output packaging is abstracted (unblocks the Matter RMT→I2S/SPI swap later).

**LILYGO flash-verify DONE (~2026-07-01) — the long-owed two-boot proof is complete.**
Flashed clean (native USB, `upload_speed=115200`+`--no-stub`+`upload_port=/dev/
cu.usbmodem1101`, temp ini edit then `git checkout`), boots healthy (heap ~208 KB, no
panic). Full HTTP verify once on the same LAN (device @192.168.0.177 / lume.local,
weak ~−72 dBm so use curl with retries — python urllib flaked): created a distinctive
segment via POST /api/v2/segments (→`202 accepted`, the async bus response), confirmed
it applied on the render loop (GET showed it), waited for the debounced autosave, hard-
reset via esptool (`--before usb_reset --after hard_reset`), confirmed a REAL reboot
(uptime dropped 94s→33s), and the segment SURVIVED → save→reboot→restore proven on
hardware. Also validated on-device: the command bus end-to-end and the P1.7 canonical
serializer (hex colors, `stop`, per-seg `brightness`). NOTE: an earlier session
claimed `mklittlefs` was "x86-broken on this Mac" and couldn't `uploadfs` — that was
WRONG (owner confirmed 2026-07-05): a dramatic agent had grabbed/installed a wrong-arch
`mklittlefs`; the correct one works fine and `uploadfs` flashes the web UI normally. Do
NOT treat mklittlefs as broken. Reboot still has no HTTP endpoint — use esptool over USB.

**2D-canvas pre-reqs P1.2–P1.5 DONE (~2026-07-01, PRs #20–#23, each its own
tested PR).** P1.2 (#20): `EffectDims{OneD,TwoD,Any}` on `EffectInfo`, defaults to
OneD via aggregate value-init (board toolchain is gnu++11, so NO default member
initializer or EffectInfo stops being an aggregate and the REGISTER macro breaks);
`REGISTER_EFFECT_SCHEMA_DIMS` variant + `runsOn()`/`dimsName()`; `dims` on
`/api/v2/effects`. P1.3 (#21): `core/region.h` — `Region{start,length}` value type
(contains/stop/end/size/empty), a Range today→Rect tomorrow; `SegmentView` holds a
`Region` (not scalars), `reversed` stays in the view (pure geometry, per user's
"pick the scalable one"); coverage+persistence+canonical serializer all route
through it; JSON keys unchanged. P1.4 (#22): de-`raw()`'d the canvas — the
fill/fade/clear/gradient/rainbow primitives reimplemented via `operator[]` (audit
found contamination was ~all inside the wrappers; only 2 direct `raw()` sites:
brightness pass + a meteor line); `raw()` DELETED from `SegmentView`; gradient/
rainbow drop the manual reversed-swap since `operator[]` handles it. P1.5 (#23):
tiered scratchpad — kept the 640 B inline pad; added a controller-owned shared
**workbuffer** one canvas-spanning segment BORROWS (`borrowWorkbuffer`/`release`,
single-owner, dropped on layout change); `getScratchpadChecked<T>()` runtime guard;
`LUME_WORKBUFFER_SIZE` (in segment_view.h) defaults 0 = feature off (1D pays a
1-byte placeholder), matrix build sets `-DLUME_WORKBUFFER_SIZE=<bytes>`;
`MAX_EFFECT_STATE=max(pad,workbuffer)` relaxes the registry cap. Decision doc:
`docs/rfcs/0002-scratchpad-strategy.md`. Native suite now **37 tests** (added
test_effect_registry / test_region / test_scratchpad). Two-agent parallel
change-surface mapping (Region, raw()) fed P1.3/P1.4 — pattern still pays off.

**MQTT→Home Assistant verified on hardware (~2026-07-02, branch
`claude/heuristic-payne-4f118e`).** The bridge-first HA path is now real: fixed the
HA json-schema state bug (published `power` bool, HA needs `state:"ON"/"OFF"`), added
RGB color (discovery `supported_color_modes`, `color` in set/state via first Color
param slot), speed/intensity in state, color in the change-detection hash, buffer
1024→1536 + publish-failure warn. Found+fixed a nastier latent bug on hardware: MQTT
enabled at runtime via web UI never published state/discovery — `mqtt.begin()` (which
sets `controller_` and generates clientId) was only called at boot when MQTT was
already enabled in NVS; `setConfig()` set neither. Fix: main.cpp always calls begin()
(even disabled), setConfig() also generates clientId. Verified end-to-end against a
local mosquitto (brew-installed on this Mac; run with `allow_anonymous true` conf +
`mosquitto_sub -t '#' -v` — a solid recipe for future MQTT bench tests): discovery +
state + OFF/ON/brightness/color/effect commands all round-trip. Device MQTT config
was restored to disabled afterwards (test broker was temporary).

**Remaining (P1/P2/P3 — no P0s left):** **P1.6** (Scenes: dead backend + 404'ing UI
— implement or remove); freeze/redirect the v1 routes; the P2/P3 cleanups (notably
P2 `removeSegment` copy-by-value scratchpad aliasing → make `Segment` non-copyable/
handle-based, which the P1.5 borrow model sidesteps but doesn't fix; effect LED-caps;
`clearUncoveredLeds` O(n·segs)). **2D itself is now "a weekend on a sound base"** —
the four keystone seams (dims/Region/de-raw/workbuffer) are in. Matter stays
**bridge-first** (MQTT→Home Assistant, $0, Arduino); native on-device Matter deferred.

**Tooling:** native unit tests run host-side with `pio test -e native` (no hardware;
CI runs them before the board builds) — now **37 tests** across param_codec /
persistence / command_bus / body_guard / effect_registry / region / scratchpad. Each
change with a testable core seam ships with a guarding test; extend the stubs in
`test/stubs` as new symbols are needed. See [[lume-lilygo-flash]] and
[[lume-c3-toolchain]] for hardware/build gotchas. (The LILYGO two-boot flash-verify
is DONE — see the section above; nothing hardware-side is currently owed.)
