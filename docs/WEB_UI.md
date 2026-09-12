# LUME web UI

One UI (the "Console × Byrne" lighting-console surface, served at `/`) over one
shared engine. **`data/` is the single source of truth** — what you edit is what
the firmware serves. There is no separate source tree and no sync step; the old
`ui-concepts/` + `sync_web.py` pipeline was dissolved when the second (euclid)
skin was removed.

```
data/
  index.html          ← the page (asset URLs carry ?v=<hash> cache-busters)
  assets/
    engine.js         ← THE controller. All fetch/WebSocket/param logic lives here.
    app.js            ← the view: DOM wiring + look-and-feel only
    app.css           ← styles
```

The engine contract the view builds against is `docs/ENGINE_API.md` — read that
first when touching UI code.

## The one architectural rule

The view (`app.js`) **never calls `fetch`, never opens a WebSocket, and never
builds `/api/` payloads.** It calls `engine.*` methods and re-renders from
`engine.state` on the `change` event. All device-API correctness — schema-driven
params, 202-async writes, `/ws` reconciliation, whole-`params` semantics,
palette-as-top-level-int, `#rrggbb` colors, demo mode — lives in `engine.js`,
in one place. The previous generation of UIs drifted from the firmware precisely
because API logic was copy-pasted into each view; don't reintroduce that.

Key engine behaviors (details in `ENGINE_API.md`):
- **Schema-driven controls**: effects come from `GET /api/v2/effects`; the view
  builds controls by iterating each effect's `params` array. There is no fixed
  speed/intensity/primary/secondary set.
- **Async writes**: mutations return `202` with no body; the engine reconciles
  from the `/ws` state push (~1 Hz + on connect) or a follow-up GET.
- **Demo mode**: with no device reachable, the engine seeds schema-correct local
  state so the UI runs standalone (see dev server below).

## Editing workflow

1. Edit `data/` directly.
2. Preview against the mock device: `node scripts/dev_server.js` →
   http://localhost:8791 (mirrors the firmware's API semantics faithfully:
   202-async, `/ws` push, whole-`params`).
3. Flash: `pio run -t uploadfs`. The pre-action (`scripts/gzip_web_files.py`)
   automatically re-stamps the `?v=<content-hash>` asset cache-busters in
   `index.html` and gzips everything — no manual steps.

If you edited assets and want the committed `index.html` stamps updated without
building, run `python3 scripts/gzip_web_files.py` from the repo root. Commit any
resulting `index.html` stamp change with your edit. (The `.gz` files it writes
are gitignored build artifacts.)

Why the stamps exist: `/assets/` is served with a week-long `max-age`, so every
asset URL embeds an 8-hex sha1 of the file's bytes — the URL changes exactly when
the bytes do, and browsers can never pin stale JS/CSS. HTML entry points are
served `Cache-Control: no-cache`. See `docs/TECH_DEBT.md` ("Stale-asset cache").

## Verifying

`node --check data/assets/app.js data/assets/engine.js` for syntax. For
end-to-end checks, run the UI against `scripts/dev_server.js` and assert the
wire payloads it logs — it mirrors the firmware semantics the engine encodes.
