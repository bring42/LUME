# LUME UI concepts

Two functional UI skins over **one shared engine**. The engine owns every
device-API detail; the skins are pure look-and-feel + DOM wiring. This split is
deliberate: the previous UIs drifted from the firmware because API logic was
copy-pasted into each concept. Don't reintroduce that — API changes belong in
the engine only.

```
_engine/
  engine.js        ← THE controller. All fetch/WebSocket/param logic lives here.
  ENGINE_API.md    ← the contract skins build against (read this first)
euclid-live/       ← "Byrne / Euclid plate" skin      → deployed to /euclid/
console-euclid-live/ ← "lighting console / rack" skin → deployed to / (root)
euclid/, console-euclid/  ← original STATIC mockups (no JS wiring; reference art)
```

## How it works
- Each skin's `index.html` loads `../_engine/engine.js` first, then its own
  `app.js`. The skin calls `engine.*` methods and re-renders from `engine.state`
  on the `change` event. **Skins never call `fetch`, never open a WebSocket, and
  never build `/api/` payloads.**
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
python3 scripts/sync_web.py     # _engine + both skins → data/
pio run -t uploadfs             # gzips data/ and flashes the filesystem
```

`sync_web.py` copies the shared engine to `data/assets/engine.js` (loaded by
both pages) and rewrites the console skin's asset paths to `/assets/*`. **Do not
hand-edit `data/` — always edit here and re-run the sync**, or the deployed copy
drifts from the source again.

## Verifying
`node --check` each `app.js` and `_engine/engine.js`. For end-to-end checks,
point the engine at a mock device that mirrors the firmware semantics
(202-async + `/ws` push + whole-`params`) and assert the wire payloads; the two
skins were validated this way (all 23 effects, every control type, correct
writes) before shipping.
