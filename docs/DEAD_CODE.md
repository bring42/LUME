# LUME — Dead Code & Migration Residue

Current state as of PR #26. The "core swap" scar tissue and the v1→v2 migration
residue that this document used to track have been **removed** — the DELETE-NOW
cut-list was executed across PRs #7–#24 and the command bus is now the live mutation
spine, not dead code.

> The original point-in-time audit (an exhaustive symbol-level sweep, kept for its
> historical value) is archived at **`archive/DEAD_CODE.md`**. Do not treat it as
> current — most of what it lists is already gone.

## What was cleared (no longer in the tree)

Verified absent from `src/` by grep:

- `api/effects_handler.cpp` — deleted (was an unrouted duplicate serializer).
- `extern CommandQueue commandQueue` (dangling extern), `ParamSchema::empty`,
  `handleApiSegments` / `handleApiSegmentsPost` decls — gone.
- `paletteName()` / `parsePalette()`, the `EffectRegistry::find/getIds/getByCategory`
  helpers, `EffectInfo::minLeds` — gone.
- The whole blend path: `blendSegment`, `BlendMode`, `Segment::set/getBlendMode` — gone.
- `PromptSpec` storage (`save/load/clearPromptSpec`, the struct, `NAMESPACE_PROMPT`) — gone.
- Dead leaf accessors on Controller / SegmentView / MQTT / sACN / ProtocolBuffer,
  and `Segment::setActive`(as public dead API)/`getScratchpadRaw` — gone.
- The dead constants block (`SYSTEM_PROMPT_BUFFER_SIZE`, `MAX_JSON_STATE_SIZE`,
  `LEDS_PER_UNIVERSE`, `HTTP_CLIENT_TIMEOUT_MS`, `OTA_PORT`, `WEB_SERVER_PORT`,
  `NIGHTLIGHT_MIN_DURATION`, duplicate sACN/WiFi constants) and the dead
  `#include "effect_registry.h"` in `command_queue.h` — gone.

Two constants the old audit flagged as dead are now **live** (this was the audit
being written before the AI-offload + rate-limit work landed):

- `ANTHROPIC_TASK_STACK_SIZE/PRIORITY/CORE` — used by the AI worker task
  (`api/prompt.cpp:263-264`).
- `PROMPT_RATE_LIMIT_MS` — enforced on `/api/prompt` (`api/prompt.cpp:284`).

The command bus is likewise no longer dead: `enqueueCommand` has ~13 producers
across `api/` and `network/server.cpp` and is the required path for state mutation.

## Still open

Nothing. The last remaining dead-code item — the **Scenes** half-build — was
**removed**: the `Storage` scene backend (`saveScene`/`loadScene`/`deleteScene`/
`getSceneCount`/`listScenes`, `struct Scene`, `MAX_SCENES`, `NAMESPACE_SCENES`), the
inert `CommandType::SaveScene`/`LoadScene` enum values and their `controller.cpp`
stub case, and the dead scenes UI (`app.js`/`app.css` + regenerated `.gz`) are all
deleted. No route ever existed, so nothing 404s anymore.

There is no unreferenced/dead symbol worth cutting at this time.
