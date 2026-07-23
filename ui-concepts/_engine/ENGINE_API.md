# LUME UI Engine — API for skins

`engine.js` exposes `window.LumeEngine.create(opts) → engine`. A **skin** (view)
loads `engine.js` first, then its own `app.js`. The skin renders DOM from
`engine.state` and calls engine methods on user interaction. **The skin never
calls `fetch`, never touches the WebSocket, never sends `params` itself.** All
device-API correctness lives in the engine.

## Lifecycle

```js
const engine = window.LumeEngine.create();
engine.on("change", () => render());          // fires on every state change
engine.on("toast", (msg) => showToast(msg));  // engine surfaced a message
engine.on("connection", ({connected}) => …);  // WS open/closed
engine.on("nightlight", (nl) => …);           // progress ticks
engine.start();                               // bootstrap; resolves when ready
```

`start()` fetches `/api/v2/info`, `/effects`, `/palettes`, `/status`, and
`/api/v2/segments`. If the device is unreachable it enters **demo mode**
(`state.demo === true`) seeded with realistic, schema-correct segments so the
skin renders identically offline. When reachable it opens the `/ws` feed and
reconciles automatically (~1 Hz + on connect + after writes). **Just re-render
on `change`.** Do not poll.

## `engine.state` (read-only — never mutate directly)

```
ready         bool
demo          bool     // true when no device; writes are local-only
connected     bool     // WebSocket currently open
info          object   // /api/v2/info  (firmware, limits, features)
status        object   // /api/status
config        object   // /api/config (null until getConfig())
effects       [ EffectDescriptor ]   // THE source of truth for controls
palettes      [ {id, name} ]
controller    { power, brightness(0-255), ledCount }
segments      [ Segment ]
nightlight    { active, progress(0..1) }
selectedId    number|null   // which segment the UI is editing
```

### EffectDescriptor (from `/api/v2/effects`)
```
{ id, name, category:"Solid"|"Animated"|"Moving"|"Special", dims:"1d"|"2d"|"any",
  usesPalette, colorCount, usesSpeed, usesIntensity,
  params: [ ParamDescriptor ] }
```
### ParamDescriptor — **render controls by iterating this array**
```
{ id, name, type, ... }
  type "int"    → { default, min, max }        slider/knob, integer
  type "float"  → { default, min, max }        slider, float
  type "color"  → { default:"#rrggbb" }        color swatch/picker
  type "bool"   → { default }                  toggle
  type "enum"   → { default:index, options:"A|B|C" }  segmented picker
  type "palette"→ (no value here)              render the PALETTE control,
                  driven by state.palettes + segment._paletteIndex, NOT a param
```

### Segment (canonical device shape + client field)
```
{ id, start, stop, length, reverse, brightness(0-255),
  effect:"fire",
  params: { <paramId>: <value>, … },   // complete, normalized to the schema
  _paletteIndex: number|null }         // client-tracked; device never echoes it
```

**Rendering the controls for the selected segment:**
```js
const seg = engine.selectedSegment();
const eff = engine.effectById(seg.effect);
eff.params.forEach(p => {
  if (p.type === "palette") renderPaletteControl(seg._paletteIndex);
  else renderControl(p, seg.params[p.id]);   // value guaranteed present
});
```
There is **no** `primaryColor`/`secondaryColor`/`speed`/`intensity` on a segment.
An effect may have zero, one, or two colors (`colorCount`), a `speed`, an
`intensity`, an `enum` (`direction`), a `bool` (`reversed`), etc. — all discovered
from `eff.params`. Build controls generically from the descriptor; do not
hard-code a fixed control set per the old model.

## Methods (all optimistic: they update `state` and emit `change` immediately,
then write to the device fire-and-forget; the WS reconciles)

```
setPower(bool)
setBrightness(0-255, transitionMs?)   // transitionMs>0 = premium eased fade on-device
                                      // (sent as Matter tenths-of-a-second `transition`)
selectSegment(id)                     // client-only selection
setEffect(id, effectId)               // resets params to schema defaults
setParam(id, paramId, value)          // sends the WHOLE params object for you
setPalette(id, paletteIndex)          // top-level int; updates _paletteIndex
setSegmentBrightness(id, 0-255)
createSegment({start, length, effect?, palette?, reverse?})
deleteSegment(id)
startNightlight(durationSeconds, targetBrightness)   // duration in SECONDS
stopNightlight()
sendPrompt(text) → Promise<{ok, reason?}>  // reason: rate_limited|busy|bad_request|error
getConfig() → Promise<config>         // for the settings view
saveConfig(body) → Promise<{ok}>      // POST /api/config (see safety note)
refreshSegments(), refreshStatus()
util.normalizeHex, util.coerceColor, util.rgbArrayToHex, util.clampInt
```

### Notes / gotchas the engine already handles for you
- **Colors are `#rrggbb` hex.** Pass hex to `setParam`; read hex from
  `seg.params[id]`. `util.coerceColor` handles legacy `[r,g,b]`.
- **Palette** is NOT in `params`. Use `setPalette` + `seg._paletteIndex`, and
  render a `type:"palette"` descriptor as the palette picker.
- **Enum** value is an integer index; `options` is `"A|B|C"`. Pass the index to
  `setParam`.
- **Changing effect** wipes params to the new schema's defaults automatically.
- **Nightlight duration is seconds** (1–3600). Convert minutes in the UI.
- **Demo mode**: `state.demo === true` → writes stay local (still call the same
  methods; the UI updates, nothing hits the network).

### Settings write safety
`saveConfig` posts to `/api/config`. Wiring `ledCount`, `aiApiKey`, `aiModel`,
`mqttEnabled/mqttBroker/mqttPort`, `sacnEnabled` is safe (deferred/rebooted by
firmware). **WiFi credential changes restart the device** — put those behind an
explicit confirm, and never send a blank password (omit password fields to keep
the current one). Populate the settings view from `getConfig()` + `state.status`.
