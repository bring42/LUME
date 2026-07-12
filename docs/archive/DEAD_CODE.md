# LUME — Dead Code & Migration Residue

> **⚠️ HISTORICAL SNAPSHOT — archived.** This is the point-in-time cut-list as it
> stood at **PR #26**, *before* the cleanup landed. The cleanup has since shipped
> (PRs #7–#24): the command bus was wired up, every DELETE-NOW symbol was removed,
> `api/effects_handler.cpp` was deleted, and the two constants listed here as "dead"
> (`ANTHROPIC_TASK_*`, `PROMPT_RATE_LIMIT_MS`) are now **live**. Kept for historical
> record — the six-auditor sweep it came from has archival value. **For the current
> state see `docs/DEAD_CODE.md`.**

A precise cut-list from an exhaustive symbol-level sweep, cross-checked against the
routes in `network/server.cpp` and what `data/assets/app.js` actually calls. The
build compiles **all** of `src/` (no `build_src_filter`), so every unreferenced
`.cpp` is compiled-but-dead.

**Provenance:** most of this is *scar tissue from the "core swap"* (commit `3d91bbb`,
"Sorry, I changed my mind and swapped the core haha") and the v1→v2 API migration —
a clean re-layering of a clean core that kept the old surface alive "for now." The
reason it calcified: **there were no tests, so deleting anything felt unsafe.** That
is why scrubbing it is gated on adding tests (`TECH_DEBT.md` P0.10).

**Estimated dead total: ~650–700 LOC** (≈336 from the two dead files alone).

---

## DELETE-NOW (zero live refs — behavior-preserving)

| Symbol / unit | Location | Evidence |
|---|---|---|
| `api/effects_handler.cpp` (whole file) | `src/api/effects_handler.cpp` | `/api/effects` never routed; duplicates `segments.cpp` serializers; only ref is a README. **91 LOC** |
| `extern CommandQueue commandQueue;` | `command_queue.h:241` | Declared, never defined (dangling extern) |
| `LumeController::enqueueCommand` | `controller.h:136` | 0 callers (only a README diagram) |
| `static const ParamSchema empty;` | `param_schema.h:98` | Declared, never defined/used |
| `handleApiSegments` / `handleApiSegmentsPost` | `main.h:49-50` | Stale v1 decls; never defined |
| `paletteName()` / `parsePalette()` | `effect_params.h:83,103` | 0 callers |
| `blendSegment` stub + `BlendMode` enum + `Segment::set/getBlendMode` | `controller.cpp:460`, `effect_params.h:11`, `segment.h:142` | Blend path is inert; overlap is already last-writer-wins |
| `PromptSpec` storage (`save/load/clearPromptSpec` + struct + `NAMESPACE_PROMPT`) | `storage.cpp:136-175` | Superseded by segment-layout autosave (`ffcf8f2`) |
| `EffectRegistry::find / getIds / getByCategory` | `effect_registry.h:129-170` | Lookups go through `getInfo` |
| `EffectInfo::minLeds` (+ the hardcoded `1` in the macro) | `effect_registry.h:54,194` | Field never read |
| Controller dead API: `show / getFrame / getActualFps / getTargetFps / setTargetFps / setColorCorrection / setMaxPower / isProtocolActive / getActiveProtocolName` | `controller.h` / `controller.cpp:501` | 0 external callers |
| `Segment::setActive / getScratchpadRaw` | `segment.h:148,182` | 0 callers |
| `SegmentView::blur / blend / map / map8 / fillFromPalette` | `segment_view.h:90-149` | 0 callers anywhere in `visuallib/` |
| `MqttProtocol::getConfig / getLastConnectAttempt / getReconnectCount` | `mqtt.h:48-59` | 0 refs |
| `SacnProtocol::getStartUniverse / getUniverseCount / isUnicastMode` | `sacn.h:96-98` | 0 callers |
| `ProtocolBuffer::writeRGB / getLastWriteTime` | `protocol.h:145-164` | 0 callers |
| Dead constants: `ANTHROPIC_TASK_STACK_SIZE/PRIORITY/CORE`, `SYSTEM_PROMPT_BUFFER_SIZE`, `MAX_JSON_STATE_SIZE`, `PROMPT_RATE_LIMIT_MS`, `LEDS_PER_UNIVERSE`, `HTTP_CLIENT_TIMEOUT_MS`, `OTA_PORT`, `WEB_SERVER_PORT`, `NIGHTLIGHT_MIN_DURATION` | `constants.h` | 0 refs each (~12 constants) |
| Duplicate sACN/WiFi constants: `SACN_PORT`, `SACN_SOURCE_TIMEOUT_MS`, `SACN_DATA_TIMEOUT_MS` (constants.h dupes of sacn.h), `WIFI_RETRY_INTERVAL` (main.cpp:70 dupe of `_MS`) | `constants.h:40,47,48`, `main.cpp:70` | Scoped copies win; these are dead dupes |
| Dead `#include "effect_registry.h"` | `command_queue.h:7` | References no symbol from it |

---

## DECISION-REQUIRED (dead today, but implies a feature)

1. **Command-queue subsystem** (`command_queue.h` ~245 LOC + `processCommands`/`executeCommand`).
   Built, wired into `update()`, runs **empty forever** — no producers. Handlers
   mutate state directly. **Recommendation: decide the mutation model
   (`TECH_DEBT.md` P0.1) first.** Either wire it (extend `Command` to carry schema
   params, route all handlers through it — fixes the live races) or remove it. Don't
   leave it half-wired.

2. **Scenes** (presets). Storage backend + `struct Scene` + NVS `"scenes"` exist;
   `app.js:1217/1247/1270` calls `GET/POST/DELETE /api/scenes` — **but no route
   exists**, so the UI 404s. **Adopt or remove as one unit** (wire the routes, or
   delete the Storage methods *and* the UI control together).

3. **Extended palettes** `Sunset/Autumn/Retro/Ice/Pink` — implemented in
   `getPalette()` but `/api/v2/palettes` and the UI stop at index 6, so 5 palettes
   are unreachable. **Adopt** (add 5 names to the UI + `paletteNames[]`) **or remove**
   the arms. Low stakes. (README claims "12 palettes" — see `TECH_DEBT.md` P2.)

---

## Notable false positives (ruled OUT — don't re-chase)

- `processCommands`/`executeCommand` are **reachable** (run every frame) — they're
  just fed nothing. The dead end is `enqueueCommand`, not these.
- `setLedCount` is **live** (`config.cpp:61`); only its internal no-op loop is dead.
- `segments.cpp`'s `schemaToJson`/`paramTypeToString` are **live** (anon-namespace,
  no ODR clash with the dead `effects_handler.cpp` copies).
- `MqttProtocol::publishState/Availability/Discovery/end` are **live** (internal).
- v1 `GET /api/segments`, `POST /api/pixels`, `GET /api/v2/palettes` are registered
  but unused by the shipped UI — **keep** as external/debug API (but `GET
  /api/segments` ~100 LOC duplicates `handleApiV2SegmentsList` — consolidation target).

---

## Suggested deletion order (safest first)

1. **Dangling declarations** — `extern commandQueue`, `ParamSchema::empty`,
   `handleApiSegments*`. Zero risk (not even defined).
2. **Dead constants & duplicates** + the dead include. Pure deletions.
3. **`effects_handler.cpp`** — delete the file (−91 LOC).
4. **Dead leaf methods** — the SegmentView/MQTT/sACN/Segment/Controller accessors,
   `paletteName`/`parsePalette`, the EffectRegistry helpers, `minLeds`.
5. **`PromptSpec` storage** (superseded).
6. **Blend path** — stub + enum + accessors + the `controller.cpp:150` guard.
7. **Command-queue subsystem** — *after* the P0.1 mutation-model decision (biggest
   single win, ~280 LOC, touches `controller.{h,cpp}`).
8. **Scenes** — paired firmware+UI decision; do last (deleting one side alone leaves
   either 404s or dead NVS code).

Items 1–6 are behavior-preserving and independently shippable. **Do them right after
adding the first native tests** — the tests are what make the long-deferred deletions
finally safe.
