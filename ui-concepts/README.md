# LUME UI concepts

A functional UI skin over **one shared engine**. The engine owns every
device-API detail; the skin is pure look-and-feel + DOM wiring. This split is
deliberate: the previous UIs drifted from the firmware because API logic was
copy-pasted into each concept. Don't reintroduce that — API changes belong in
the engine only.

```
_engine/
  engine.js        ← THE controller. All fetch/WebSocket/param logic lives here.
  ENGINE_API.md    ← the contract the skin builds against (read this first)
console-euclid-live/ ← "lighting console / rack" skin → deployed to / (root)
```

## How it works
- The skin's `index.html` loads `../_engine/engine.js` first, then its own
  `app.js`. The skin calls `engine.*` methods and re-renders from `engine.state`
  on the `change` event. **The skin never calls `fetch`, never opens a WebSocket,
  and never builds `/api/` payloads.**
- The engine is **schema-driven**: it fetches `/api/v2/effects` and the skin
  builds effect controls by iterating each effect's `params` array (int/float/
  color/bool/enum/palette). There is no fixed speed/intensity/primary/secondary
  control set — that was the old, removed model.
- Writes are async (`202`, no body); the engine reconciles from the `/ws` state
  push. It also handles whole-`params` semantics, palette-as-top-level-int
  (tracked client-side — the device can't report it), and `#rrggbb` colors.
- Opened via `file://` with no device, the engine falls back to **demo mode**
  seeded with realistic, schema-correct data, so the concepts render standalone.

## Deploying to the device
Edit the sources here, then sync into `data/` (what the firmware serves):

```
python3 scripts/sync_web.py     # _engine + skin → data/
pio run -t uploadfs             # gzips data/ and flashes the filesystem
```

`sync_web.py` copies the shared engine to `data/assets/engine.js` and rewrites
the console skin's asset paths to `/assets/*`. **Do not hand-edit `data/` —
always edit here and re-run the sync**, or the deployed copy drifts from the
source again.

## Verifying
`node --check` the skin's `app.js` and `_engine/engine.js`. For end-to-end
checks, point the engine at a mock device that mirrors the firmware semantics
(202-async + `/ws` push + whole-`params`) and assert the wire payloads; the
skin was validated this way (all 23 effects, every control type, correct
writes) before shipping.
