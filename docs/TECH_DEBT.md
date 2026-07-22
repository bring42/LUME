# LUME — Technical Debt Register

Current state as of PR #26. The structural audit that this register grew out of has
been **worked off**: every **P0** (correctness / memory-safety / races) and every
**P1** (2D pre-req seam) item is resolved.

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

### ~~MQTT bypasses the command bus~~ (RESOLVED)
`mqtt.cpp` now routes every control mutation through `enqueueCommand` (single-writer
spine): power/brightness → `setPower`/`setGlobalBrightness`, effect/speed/intensity →
one bundled `applyEffectSpec(0, …)`. The trailing `publishState()` calls were dropped
because commands drain on the *next* `controller.update()` (which runs before
`mqtt.update()` in `main.cpp`), so an immediate publish would emit stale pre-change
state; `update()`'s `stateChanged()` poll republishes the fresh state after the command
applies. **Follow-up:** still segment-0-only. Multi-segment was deferred deliberately —
not a band-aid `segment` field on `lume/set`. The right design is **configure-once /
projection**: the device's segment layout is the single source of truth, and every
integration surface (REST, MQTT, HA discovery) *projects* from it rather than
re-declaring it. Concretely: HA auto-discovery publishes one light entity per segment
and re-publishes (with cleanup) when the layout changes — so pairing = "flash → pair →
done", never "configure → pair → configure again". Do all three layers (JSON field +
per-segment topics + discovery entity lifecycle) in one pass when picked up.

### Firmware OTA — hardening follow-ups (app path HARDWARE-VALIDATED 2026-07-22; FS path untested)
Pull-based GitHub OTA (app + FS, split, HTTPS + SHA-256 verify-before-commit). The **app**
update is proven end-to-end on a Seeed XIAO ESP32-C3: v1.0.0 self-updated to v1.1.0
(download over HTTPS from the GitHub release → SHA-256 verify → flash the inactive slot →
clean reboot into the new image, healthy). The **FS** update path (`/api/firmware/update/fs`)
is implemented but not yet exercised on hardware. Known hardening path, in priority order:
1. **Self-heal rollback** — replace the unconditional `esp_ota_mark_app_valid_cancel_rollback()`
   on boot with a **self-test gate** (WiFi up? render loop ticking? storage reads?); on
   failure call the rollback-and-reboot sibling → bootloader reverts to the prior known-good
   slot with no re-download. Requires `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` in a custom
   sdkconfig (can only be enabled via a USB flash, not OTA). This closes the "checksum proves
   it downloaded, not that it boots" gap.
2. **C3 flash headroom** — app slot ~90% full; if it ever bites, ship a custom partition CSV
   trading FS size for larger app slots.
3. **Security / LAN trust** — `setInsecure()` (no TLS cert validation) + checksum-only, and the
   flash endpoints are UNAUTHENTICATED when no auth token is set (the default). Per the
   2026-07-23 audit, a single on-path LAN attacker can *trigger* an update **and** MITM it (serve
   a malicious image + a matching manifest checksum) → arbitrary firmware, no credentials.
   **Interim mitigation: set an auth token** — that gates `/api/firmware/update/*` like the rest
   of the API. Real fix: TLS cert pinning + code signing / secure boot. Treat AP/LAN as trusted
   until then.
4. ~~**FS-flash-while-serving**~~ (RESOLVED 2026-07-23) — `serveStatic("/assets/")` now carries a
   `setFilter([]{ return !updaterInProgress(); })`, so asset reads fall through to the `onNotFound`
   503 guard during an FS flash instead of reading a partition mid-erase.

**Deferred from the 2026-07-23 branch audit (LOW / nits):** manifest fetched via unbounded
`getString()` (MITM-gated OOM risk on the ~130 KB-heap C3 — add a size cap); the `applyTarget`
poller can misreport a suspiciously-fast reboot as "failed" (`sawFlashing` never set — cosmetic,
bounded); effect/palette **names** are rendered via `innerHTML` in both skins (pre-existing, NOT
this branch — device is the same-origin trust root, but worth moving to `textContent`).

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
