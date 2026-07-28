/* ==========================================================================
   LUME — shared UI engine  (window.LumeEngine)
   --------------------------------------------------------------------------
   ONE engine, strictly separated from the view. The page loads this file
   first, then the view script (app.js). The view NEVER talks to the network
   directly — it calls engine methods and re-renders from `engine.state` on the
   "change" event. All device-API correctness lives here, in one place, so the
   look-and-feel layer cannot drift from the real firmware contract again.
   (See docs/ENGINE_API.md for the contract the view builds against.)

   Contract this engine enforces (see docs/API_V2.md):
     • Effects are SCHEMA-DRIVEN. Params are per-effect, discovered at runtime
       from GET /api/v2/effects — never a fixed speed/intensity/primary/secondary
       set. Each param has {id, type, default, min/max | options}.
     • Writes are ASYNC + fire-and-forget. Mutations return 202 {"status":
       "accepted"} with NO resulting state. We reconcile from the WebSocket
       /ws state push (authoritative, ~1 Hz + on connect) or a follow-up GET.
     • PUT /segments params are applied WHOLE against the effect schema — any
       omitted key resets to its default. So to change one param we always send
       the COMPLETE params object. The engine owns this; skins call setParam().
     • Palette is a TOP-LEVEL integer preset index (GET /api/v2/palettes), NOT a
       member of `params`. The device does not echo it back, so we track it
       client-side per segment (_paletteIndex).
     • Colors are #rrggbb hex strings on the wire.
   ========================================================================== */
(function (global) {
  "use strict";

  var API_TIMEOUT_MS = 3000;
  var WS_RETRY_MS = 2500;
  var REFRESH_AFTER_WRITE_MS = 350; // nudge a GET after set-changing writes

  /* ---------------------------------------------------------------------
     Fallback catalogue — mirrors the real firmware effect registry
     (src/visuallib/effects/*.cpp) so demo mode (no device) still renders
     correct, schema-driven controls. Overwritten by GET /api/v2/effects
     the moment a real device answers.
     -------------------------------------------------------------------- */
  function I(id, name, def, min, max) { return { id: id, name: name, type: "int", default: def, min: min, max: max }; }
  function C(id, name, def) { return { id: id, name: name, type: "color", default: def }; }
  function B(id, name, def) { return { id: id, name: name, type: "bool", default: !!def }; }
  function E(id, name, options, def) { return { id: id, name: name, type: "enum", default: def, options: options }; }
  function P(id, name) { return { id: id, name: name, type: "palette" }; }

  function fx(id, name, category, params) {
    var colorCount = 0, usesPalette = false, usesSpeed = false, usesIntensity = false;
    for (var i = 0; i < params.length; i++) {
      var p = params[i];
      if (p.type === "color") colorCount++;
      if (p.type === "palette") usesPalette = true;
      if (p.id === "speed") usesSpeed = true;
      if (p.id === "intensity") usesIntensity = true;
    }
    return {
      id: id, name: name, category: category, dims: "1d", params: params,
      usesPalette: usesPalette, colorCount: colorCount,
      usesSpeed: usesSpeed, usesIntensity: usesIntensity
    };
  }

  // Demo/fallback effect catalog (used only when no device answers GET
  // /api/v2/effects). Emptied during the premium-modes rebuild — the old 20
  // built-ins were purged; this repopulates mode-by-mode as each premium mode
  // (Curator, Hearth, Breathe, …) lands. The live list always comes from the device.
  var FALLBACK_EFFECTS = [];

  // All 12 built-in palettes (firmware PALETTE_NAMES, exposed in PR #27). The
  // live list still comes from GET /api/v2/palettes; this is the demo/fallback.
  var FALLBACK_PALETTES = [
    { id: 0, name: "Rainbow" }, { id: 1, name: "Lava" }, { id: 2, name: "Ocean" },
    { id: 3, name: "Party" }, { id: 4, name: "Forest" }, { id: 5, name: "Cloud" },
    { id: 6, name: "Heat" }, { id: 7, name: "Sunset" }, { id: 8, name: "Autumn" },
    { id: 9, name: "Retro" }, { id: 10, name: "Ice" }, { id: 11, name: "Pink" }
  ];

  /* ---------------------------------------------------------------------
     Small utilities
     -------------------------------------------------------------------- */
  function clampInt(v, min, max) {
    v = Math.round(Number(v));
    if (isNaN(v)) v = min;
    if (v < min) v = min;
    if (v > max) v = max;
    return v;
  }
  function normalizeHex(hex) {
    if (typeof hex !== "string") return "#000000";
    var m = /^#?([0-9a-fA-F]{6})$/.exec(hex.trim());
    return m ? "#" + m[1].toLowerCase() : "#000000";
  }
  function rgbArrayToHex(a) {
    if (!Array.isArray(a) || a.length < 3) return "#000000";
    return "#" + a.slice(0, 3).map(function (v) {
      return clampInt(v, 0, 255).toString(16).padStart(2, "0");
    }).join("");
  }
  // Colors may arrive as hex (device) or legacy [r,g,b] (older WS). Normalize.
  function coerceColor(v) {
    if (Array.isArray(v)) return rgbArrayToHex(v);
    return normalizeHex(v);
  }

  /* ---------------------------------------------------------------------
     Engine factory
     -------------------------------------------------------------------- */
  function create(opts) {
    opts = opts || {};

    var listeners = { change: [], connection: [], toast: [], nightlight: [] };

    var state = {
      ready: false,        // bootstrap finished (success or demo)
      demo: false,         // no device reachable → local-only
      connected: false,    // WebSocket currently open
      info: null,          // GET /api/v2/info
      status: null,        // GET /api/status
      config: null,        // GET /api/config (settings view)
      effects: FALLBACK_EFFECTS.slice(),
      palettes: FALLBACK_PALETTES.slice(),
      controller: { power: true, brightness: 180, ledCount: 300 },
      segments: [],        // canonical shape + client-only _paletteIndex
      nightlight: { active: false, progress: 0 },
      selectedId: null     // client-only: which segment the UI is editing
    };

    var ws = null;
    var wsRetryTimer = null;
    var refreshTimer = null;
    var nightlightTimer = null;

    /* ---- optimistic lock (kills the "slider jumps back for a frame" flicker) --
       Writes are async + fire-and-forget; the ~1 Hz WebSocket push lags, so a
       state snapshot taken *before* a local change arrives afterward and would
       overwrite the value the user just set — for one frame, until the next push
       corrects it. A debounce only makes that race rarer. The real fix: while a
       field has a pending local write, ignore incoming reconciles for it until
       the device echoes our value back (device caught up) or a timeout elapses
       (so external changes — MQTT, another client — still win eventually). */
    var LOCK_MS = 1500;
    var pending = {};  // field -> { value, until }  (fields: "brightness","power")
    var segmentsDirtyUntil = 0;  // array-wide time lock after a local segment edit
    function lockSegments() { segmentsDirtyUntil = nowMs() + LOCK_MS; }
    function lockField(field, value) {
      pending[field] = { value: value, until: nowMs() + LOCK_MS };
    }
    // True if the incoming reconciled value should be applied for `field`.
    function acceptField(field, incoming) {
      var p = pending[field];
      if (!p) return true;                                   // not locked
      if (nowMs() >= p.until) { delete pending[field]; return true; }  // expired → external wins
      if (incoming === p.value) { delete pending[field]; return true; } // device confirmed us
      return false;                                          // stale/mid-fade → keep optimistic
    }
    function nowMs() { return (typeof performance !== "undefined" && performance.now) ? performance.now() : Date.now(); }

    /* ---- events ---- */
    function on(evt, fn) {
      if (!listeners[evt]) listeners[evt] = [];
      listeners[evt].push(fn);
      return function off() {
        var a = listeners[evt], i = a.indexOf(fn);
        if (i >= 0) a.splice(i, 1);
      };
    }
    function emit(evt, payload) {
      var a = listeners[evt];
      if (!a) return;
      for (var i = 0; i < a.length; i++) {
        try { a[i](payload); } catch (e) { console.error("[LumeEngine] listener error", e); }
      }
    }
    function notify() { emit("change", state); }
    function toast(msg) { emit("toast", msg); }

    /* ---- catalogue helpers ---- */
    function effectById(id) {
      for (var i = 0; i < state.effects.length; i++) {
        if (state.effects[i].id === id) return state.effects[i];
      }
      return null;
    }
    function paletteName(index) {
      for (var i = 0; i < state.palettes.length; i++) {
        if (state.palettes[i].id === index) return state.palettes[i].name;
      }
      return null;
    }
    function segmentById(id) {
      for (var i = 0; i < state.segments.length; i++) {
        if (state.segments[i].id === id) return state.segments[i];
      }
      return null;
    }
    function selectedSegment() {
      return segmentById(state.selectedId);
    }
    // Mirror the firmware's id assignment (src/core/controller.cpp): the lowest
    // unused slot, NOT max+1. Matching its scheme for the optimistic temp id
    // means the device usually hands back the SAME id on reconcile, so the
    // freshly-selected new channel keeps its selection across the id swap.
    function nextSegmentId() {
      var used = {};
      for (var i = 0; i < state.segments.length; i++) used[state.segments[i].id] = true;
      var id = 0;
      while (used[id]) id++;
      return id;
    }
    // Build a complete, schema-valid params object for an effect from its
    // defaults, optionally overlaying known values. Palette-type params are
    // intentionally EXCLUDED — palette is a top-level field, not a param.
    function defaultParamsFor(effectId, overlay) {
      var eff = effectById(effectId);
      var out = {};
      if (eff) {
        for (var i = 0; i < eff.params.length; i++) {
          var p = eff.params[i];
          if (p.type === "palette") continue;
          if (p.type === "color") out[p.id] = normalizeHex(p.default);
          else if (p.type === "bool") out[p.id] = !!p.default;
          else out[p.id] = p.default;
        }
      }
      if (overlay) {
        for (var k in overlay) {
          if (Object.prototype.hasOwnProperty.call(overlay, k) && out.hasOwnProperty(k)) {
            out[k] = overlay[k];
          }
        }
      }
      return out;
    }
    // Normalize a segment's params to the current effect schema so the view can
    // rely on every declared key being present and correctly typed.
    function normalizeSegment(seg) {
      var eff = effectById(seg.effect);
      var params = seg.params || {};
      var clean = {};
      if (eff) {
        for (var i = 0; i < eff.params.length; i++) {
          var p = eff.params[i];
          if (p.type === "palette") continue;
          var v = params[p.id];
          if (p.type === "color") clean[p.id] = (v == null) ? normalizeHex(p.default) : coerceColor(v);
          else if (p.type === "bool") clean[p.id] = (v == null) ? !!p.default : !!v;
          else if (p.type === "int") clean[p.id] = (v == null) ? p.default : clampInt(v, p.min, p.max);
          else clean[p.id] = (v == null) ? p.default : v;
        }
      } else {
        clean = params;
      }
      seg.params = clean;
      return seg;
    }

    /* ---- network ---- */
    function apiFetch(path, options) {
      options = options || {};
      var controller = new AbortController();
      var timer = setTimeout(function () { controller.abort(); }, API_TIMEOUT_MS);
      options.signal = controller.signal;
      return fetch(path, options).then(function (res) {
        clearTimeout(timer);
        var ct = res.headers.get("content-type") || "";
        var bodyP = ct.indexOf("application/json") >= 0
          ? res.json().catch(function () { return null; })
          : res.text().catch(function () { return null; });
        return bodyP.then(function (body) {
          if (!res.ok) {
            var msg = res.status + (body && body.message ? ": " + body.message : "");
            var err = new Error(path + " → " + msg);
            err.status = res.status;
            err.body = body;
            throw err;
          }
          return body; // 200 read payload, or 202 {"status":"accepted"} (ignored)
        });
      }, function (err) {
        clearTimeout(timer);
        throw err;
      });
    }

    // Fire-and-forget mutation: send, ignore the 202 body, surface failures as
    // a toast. Reconciliation happens via the WS push / GET refresh.
    function writeJson(path, method, obj) {
      if (state.demo) return Promise.resolve(); // local-only in demo mode
      return apiFetch(path, {
        method: method,
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(obj)
      }).catch(function (err) {
        console.error("[LumeEngine] write failed", path, err);
        toast("Device didn't accept the change");
        throw err;
      });
    }

    // Fire-and-forget variant for the optimistic UI setters: writeJson already
    // logs + toasts on failure, so swallow the rethrow here to avoid an
    // unhandled promise rejection. (create/delete/config keep the rejection —
    // they act on the result.)
    function fireWrite(path, method, obj) {
      writeJson(path, method, obj).catch(function () {});
    }

    function scheduleRefresh() {
      if (state.demo) return;
      if (refreshTimer) clearTimeout(refreshTimer);
      refreshTimer = setTimeout(function () { refreshSegments(); }, REFRESH_AFTER_WRITE_MS);
    }

    // For structural changes (create/delete) we hold an optimistic segments lock
    // so a lagging reconcile can't drop the just-added/removed channel for a
    // frame. That means a GET fired at REFRESH_AFTER_WRITE_MS would be skipped by
    // the lock — so schedule the reconcile to land JUST AFTER the lock releases.
    // This guarantees device truth (real IDs) replaces the optimistic structure
    // even when the WS feed is down (offline-but-not-demo).
    function scheduleRefreshAfterLock() {
      if (state.demo) return;
      if (refreshTimer) clearTimeout(refreshTimer);
      var delay = Math.max(REFRESH_AFTER_WRITE_MS, (segmentsDirtyUntil - nowMs()) + 50);
      refreshTimer = setTimeout(function () { refreshSegments(); }, delay);
    }

    function applyControllerAndSegments(payload) {
      if (!payload) return;
      // Both shapes carry power/brightness/ledCount: WS nests under .controller,
      // GET /api/v2/segments has them at the top level. Apply through the
      // optimistic lock so a lagging snapshot can't clobber a value we just set.
      var cs = payload.controller ? payload.controller : payload;
      if (typeof cs.power === "boolean" && acceptField("power", cs.power)) {
        state.controller.power = cs.power;
      }
      if (cs.brightness != null) {
        var b = clampInt(cs.brightness, 0, 255);
        if (acceptField("brightness", b)) state.controller.brightness = b;
      }
      if (cs.ledCount != null) state.controller.ledCount = Number(cs.ledCount) || state.controller.ledCount;
      // Segment optimistic lock: after a local field edit (effect/param/palette/
      // brightness/range — all applied in place on state.segments), skip the
      // segments reconcile until the device catches up, so a lagging snapshot
      // can't revert the edit for a frame (same flicker as brightness, on
      // effects). Structural changes (create/delete) hold the lock too so their
      // optimistic add/remove survives a lagging snapshot; scheduleRefreshAfterLock
      // (or WS) reconciles device truth once the lock releases.
      if (Array.isArray(payload.segments) && nowMs() >= segmentsDirtyUntil) {
        state.segments = payload.segments.map(function (s) {
          var prev = segmentById(s.id);
          var seg = {
            id: s.id, start: s.start, stop: s.stop, length: s.length,
            reverse: !!s.reverse, brightness: clampInt(s.brightness, 0, 255),
            effect: s.effect, params: s.params || {},
            // palette is not echoed by the device → keep our tracked value
            _paletteIndex: prev ? prev._paletteIndex : null
          };
          return normalizeSegment(seg);
        });
        if (state.selectedId == null || !segmentById(state.selectedId)) {
          state.selectedId = state.segments.length ? state.segments[0].id : null;
        }
      }
    }

    function refreshSegments() {
      return apiFetch("/api/v2/segments").then(function (payload) {
        applyControllerAndSegments(payload);
        notify();
      }).catch(function () { /* WS will catch us up */ });
    }

    function refreshStatus() {
      return apiFetch("/api/status").then(function (s) {
        state.status = s;
        notify();
      }).catch(function () {});
    }

    /* ---- WebSocket reconciliation feed ---- */
    function connectWs() {
      if (state.demo || !global.location || !global.location.host) return;
      try {
        var proto = global.location.protocol === "https:" ? "wss:" : "ws:";
        ws = new WebSocket(proto + "//" + global.location.host + "/ws");
      } catch (e) { return; }

      ws.onopen = function () {
        state.connected = true;
        emit("connection", { connected: true });
        notify();
      };
      ws.onmessage = function (evt) {
        var msg;
        try { msg = JSON.parse(evt.data); } catch (e) { return; }
        if (msg && msg.type === "state") {
          applyControllerAndSegments(msg);
          notify();
        }
      };
      ws.onclose = function () {
        state.connected = false;
        emit("connection", { connected: false });
        notify();
        if (!state.demo) {
          if (wsRetryTimer) clearTimeout(wsRetryTimer);
          wsRetryTimer = setTimeout(connectWs, WS_RETRY_MS);
        }
      };
      ws.onerror = function () { try { ws.close(); } catch (e) {} };
    }

    /* ---- demo seed (device unreachable) ---- */
    function seedDemo() {
      state.demo = true;
      state.connected = false;
      state.controller = { power: true, brightness: 180, ledCount: 300 };
      state.segments = [
        normalizeSegment({ id: 0, start: 0, stop: 99, length: 100, reverse: false, brightness: 255, effect: "fire", params: { cooling: 55, sparking: 120, reversed: false }, _paletteIndex: 6 }),
        normalizeSegment({ id: 1, start: 100, stop: 219, length: 120, reverse: false, brightness: 255, effect: "rainbow", params: { speed: 90, density: 85 }, _paletteIndex: 0 }),
        normalizeSegment({ id: 2, start: 220, stop: 299, length: 80, reverse: false, brightness: 220, effect: "twinkle", params: { color: "#ffc978", speed: 60 }, _paletteIndex: 5 })
      ];
      state.selectedId = 0;
      state.info = { firmware: { name: "LUME", version: "demo", buildHash: "demo" }, limits: { maxLeds: 1000, maxSegments: 8 }, features: {} };
    }

    /* ---- bootstrap ---- */
    function start() {
      // Metadata + initial state in parallel; tolerate partial failures.
      var tasks = [
        apiFetch("/api/v2/info").then(function (r) { state.info = r; }, function () {}),
        apiFetch("/api/v2/effects").then(function (r) { if (r && r.effects) state.effects = r.effects; }, function () {}),
        apiFetch("/api/v2/palettes").then(function (r) { if (r && r.palettes) state.palettes = r.palettes; }, function () {}),
        apiFetch("/api/status").then(function (r) { state.status = r; }, function () {})
      ];
      // The segments GET decides reachability — it's the one we can't fake.
      var segP = apiFetch("/api/v2/segments").then(function (payload) {
        applyControllerAndSegments(payload);
        return true;
      }, function () { return false; });

      return Promise.all(tasks.concat([segP])).then(function (results) {
        var reachable = results[results.length - 1];
        if (!reachable) {
          seedDemo();
        } else {
          state.demo = false;
          // re-normalize segments now that real effect schemas are loaded
          state.segments.forEach(normalizeSegment);
          connectWs();
        }
        state.ready = true;
        notify();
        emit("connection", { connected: state.connected });
        return state;
      });
    }

    /* =================================================================
       Public mutations — optimistic local update + fire-and-forget write.
       ================================================================= */

    // Premium easing durations (ms). Nothing in the UI should hard-snap:
    //   DRAG   — short follow while a control is being dragged; because the
    //            device re-eases from its current value, rapid re-targets read
    //            as the light chasing your finger with a little inertia.
    //   SETTLE — graceful ease when a control is released / a value committed.
    //   POWER  — a touch heavier for on/off, so it feels deliberate.
    // Tunable in one place; skins reference engine.TRANSITION. These are the
    // "feel" of the whole UI — the Tuning settings section lets the user adjust
    // them live via setTransition() below, persisted client-side (localStorage,
    // per-browser: they shape interaction feel, not device state, so they belong
    // with the client, not in NVS).
    var TRANSITION = { DRAG: 150, SETTLE: 300, POWER: 400 };
    var TRANSITION_LS_KEY = "lume.transition";
    var TRANSITION_MAX_MS = 5000; // sane upper bound so a fat-fingered value can't wedge the UI

    // Restore any saved duration overrides (best-effort; ignore malformed/absent).
    (function loadTransitionOverrides() {
      try {
        var raw = global.localStorage && global.localStorage.getItem(TRANSITION_LS_KEY);
        if (!raw) return;
        var saved = JSON.parse(raw);
        ["DRAG", "SETTLE", "POWER"].forEach(function (k) {
          if (saved && typeof saved[k] === "number" && isFinite(saved[k])) {
            TRANSITION[k] = clampInt(saved[k], 0, TRANSITION_MAX_MS);
          }
        });
      } catch (e) { /* corrupt/blocked storage → keep defaults */ }
    })();

    // Live-tune a transition duration (ms). Updates engine.TRANSITION in place so
    // every subsequent interaction uses the new feel immediately, and persists the
    // set to localStorage. `key` is "DRAG" | "SETTLE" | "POWER".
    function setTransition(key, ms) {
      if (!TRANSITION.hasOwnProperty(key)) return;
      TRANSITION[key] = clampInt(ms, 0, TRANSITION_MAX_MS);
      try {
        if (global.localStorage) {
          global.localStorage.setItem(TRANSITION_LS_KEY, JSON.stringify(TRANSITION));
        }
      } catch (e) { /* storage blocked → in-memory only, still applies this session */ }
    }

    // Optional transitionMs eases the strip on/off on-device (premium fade); the
    // brightness level is preserved across the toggle. Sent as Matter tenths.
    // Defaults to a weighted power fade so a bare setPower() still eases; pass 0
    // to force an instant snap.
    function setPower(on, transitionMs) {
      on = !!on;
      if (transitionMs === undefined) transitionMs = TRANSITION.POWER;
      state.controller.power = on;
      lockField("power", on);   // hold our value until the device confirms it
      notify();
      var body = { power: on };
      if (transitionMs > 0) body.transition = Math.round(transitionMs / 100);
      fireWrite("/api/v2/controller", "PUT", body);
    }

    // Optional transitionMs eases the change on-device (premium fade) instead of
    // snapping. The device speaks Matter-shaped tenths-of-a-second, so convert.
    function setBrightness(v, transitionMs) {
      v = clampInt(v, 0, 255);
      state.controller.brightness = v;
      lockField("brightness", v);   // hold our value until the device confirms it
      notify();
      var body = { brightness: v };
      if (transitionMs > 0) body.transition = Math.round(transitionMs / 100);
      fireWrite("/api/v2/controller", "PUT", body);
    }

    function selectSegment(id) {
      if (segmentById(id)) {
        state.selectedId = id;
        notify();
      }
    }

    // Change the effect on a segment. Params reset to the new effect's schema
    // defaults (matching device behaviour). Palette is preserved if the new
    // effect uses one and we have a tracked index.
    function setEffect(id, effectId) {
      var seg = segmentById(id);
      if (!seg || !effectById(effectId)) return;
      var eff = effectById(effectId);
      seg.effect = effectId;
      seg.params = defaultParamsFor(effectId);
      lockSegments();
      notify();
      var body = { effect: effectId, params: seg.params };
      if (eff.usesPalette && seg._paletteIndex != null) body.palette = seg._paletteIndex;
      fireWrite("/api/v2/segments/" + id, "PUT", body);
    }

    // Change ONE param. We send the COMPLETE params object (whole-object
    // semantics) so no sibling param is reset to its default. Optional
    // transitionMs eases continuous params/colors on-device (skins opt in, e.g.
    // on slider release; discrete params snap regardless).
    function setParam(id, key, value, transitionMs) {
      var seg = segmentById(id);
      if (!seg) return;
      var eff = effectById(seg.effect);
      var desc = null;
      if (eff) {
        for (var i = 0; i < eff.params.length; i++) {
          if (eff.params[i].id === key) { desc = eff.params[i]; break; }
        }
      }
      if (desc) {
        if (desc.type === "color") value = coerceColor(value);
        else if (desc.type === "bool") value = !!value;
        else if (desc.type === "int") value = clampInt(value, desc.min, desc.max);
        else if (desc.type === "enum") value = clampInt(value, 0, String(desc.options || "").split("|").length - 1);
      }
      var params = {};
      for (var k in seg.params) if (Object.prototype.hasOwnProperty.call(seg.params, k)) params[k] = seg.params[k];
      params[key] = value;
      seg.params = params;
      lockSegments();
      notify();
      var body = { params: params };
      if (transitionMs > 0) body.transition = Math.round(transitionMs / 100);
      fireWrite("/api/v2/segments/" + id, "PUT", body);
    }

    // Palette is a top-level integer, tracked client-side (device never echoes).
    function setPalette(id, index) {
      var seg = segmentById(id);
      if (!seg) return;
      index = clampInt(index, 0, Math.max(0, state.palettes.length - 1));
      seg._paletteIndex = index;
      lockSegments();
      notify();
      fireWrite("/api/v2/segments/" + id, "PUT", { palette: index });
    }

    function setSegmentBrightness(id, v) {
      var seg = segmentById(id);
      if (!seg) return;
      v = clampInt(v, 0, 255);
      seg.brightness = v;
      lockSegments();
      notify();
      fireWrite("/api/v2/segments/" + id, "PUT", { brightness: v });
    }

    // Resize a segment's boundaries (editable zones). Sends {start,length,reverse}
    // to PUT /api/v2/segments/{id}; the device resizes on its render loop and
    // clamps to the strip, so we clamp locally to match (start in [0,ledCount-1],
    // length in [1, ledCount-start]). Overlaps are allowed on-device. Optimistic
    // local update + fire-and-forget, like the other setters. Only the fields
    // present in `geom` are changed; the others keep the segment's current value.
    function setSegmentRange(id, geom) {
      var seg = segmentById(id);
      if (!seg) return;
      geom = geom || {};
      var ledCount = Number(state.controller.ledCount) || 1;
      var body = {};
      if (geom.start != null) {
        seg.start = clampInt(geom.start, 0, Math.max(0, ledCount - 1));
        body.start = seg.start;
      }
      if (geom.length != null) {
        var maxLen = Math.max(1, ledCount - seg.start);
        seg.length = clampInt(geom.length, 1, maxLen);
        body.length = seg.length;
      } else if (geom.start != null) {
        // Start moved without an explicit length: re-clamp the existing length so
        // the segment can't optimistically run off the end (stop > strip). Send
        // the corrected length too, matching the device's own clamp.
        var maxLenS = Math.max(1, ledCount - seg.start);
        if (seg.length > maxLenS) { seg.length = maxLenS; body.length = seg.length; }
      }
      if (geom.reverse != null) {
        seg.reverse = !!geom.reverse;
        body.reverse = seg.reverse;
      }
      // Keep `stop` consistent so range readouts stay correct without a refetch.
      seg.stop = seg.start + seg.length - 1;
      lockSegments();
      notify();
      fireWrite("/api/v2/segments/" + id, "PUT", body);
    }

    function createSegment(cfg) {
      cfg = cfg || {};
      var body = {
        start: clampInt(cfg.start, 0, 65535),
        length: clampInt(cfg.length != null ? cfg.length : 1, 1, 65535)
      };
      if (cfg.reverse != null) body.reverse = !!cfg.reverse;
      if (cfg.effect) { body.effect = cfg.effect; body.params = defaultParamsFor(cfg.effect); }
      if (cfg.palette != null) body.palette = clampInt(cfg.palette, 0, Math.max(0, state.palettes.length - 1));

      // Optimistic add (both demo AND live device): push a normalized segment so
      // the new channel appears in the UI INSTANTLY, then select it. Previously
      // the live path only fired the POST and waited on the ~350 ms GET / ~1 Hz
      // WS reconcile, so an add could appear to "do nothing" until reconcile
      // caught up. The temp id mirrors the device's assignment (lowest unused
      // slot); the real device-assigned id lands on reconcile.
      var nid = nextSegmentId();
      state.segments.push(normalizeSegment({
        id: nid, start: body.start, stop: body.start + body.length - 1, length: body.length,
        reverse: !!cfg.reverse, brightness: 255, effect: cfg.effect || "solid",
        params: body.params || defaultParamsFor(cfg.effect || "solid"),
        _paletteIndex: cfg.palette != null ? cfg.palette : null
      }));
      state.selectedId = nid;
      // Hold the optimistic structure until the device catches up, so a reconcile
      // snapshot taken BEFORE the device applied the create (WS push / GET in
      // flight) can't drop the just-added channel for a frame. Device truth
      // replaces it after the lock (scheduleRefreshAfterLock / WS). This is the
      // same mechanism the field-edit setters use — create/delete used to clear
      // the lock, which left the add at the mercy of reconcile timing.
      lockSegments();
      notify();
      if (state.demo) return Promise.resolve();
      return writeJson("/api/v2/segments", "POST", body).then(scheduleRefreshAfterLock, scheduleRefreshAfterLock);
    }

    function deleteSegment(id) {
      // Optimistic delete (both demo AND live device): drop it locally and fix
      // the selection immediately, so the removal shows at once instead of
      // waiting on reconcile. Lock held like create so a lagging snapshot can't
      // resurrect the segment for a frame; device truth reconciles after.
      state.segments = state.segments.filter(function (s) { return s.id !== id; });
      if (state.selectedId === id) state.selectedId = state.segments.length ? state.segments[0].id : null;
      lockSegments();
      notify();
      if (state.demo) return Promise.resolve();
      return writeJson("/api/v2/segments/" + id, "DELETE", undefined).then(scheduleRefreshAfterLock, scheduleRefreshAfterLock);
    }

    /* ---- nightlight ---- */
    function startNightlight(durationSec, targetBrightness) {
      var body = {
        duration: clampInt(durationSec, 1, 3600),
        targetBrightness: clampInt(targetBrightness, 0, 255)
      };
      state.nightlight.active = true;
      notify();
      if (state.demo) return Promise.resolve();
      return writeJson("/api/nightlight", "POST", body).then(function () {
        pollNightlight();
      });
    }
    function stopNightlight() {
      state.nightlight.active = false;
      state.nightlight.progress = 0;
      notify();
      if (nightlightTimer) { clearInterval(nightlightTimer); nightlightTimer = null; }
      if (state.demo) return Promise.resolve();
      return apiFetch("/api/nightlight/stop", { method: "POST" }).catch(function () {});
    }
    function pollNightlight() {
      if (state.demo) return;
      if (nightlightTimer) clearInterval(nightlightTimer);
      nightlightTimer = setInterval(function () {
        apiFetch("/api/nightlight").then(function (n) {
          if (!n) return;
          state.nightlight.active = !!n.active;
          state.nightlight.progress = Number(n.progress) || 0;
          emit("nightlight", state.nightlight);
          notify();
          if (!n.active && nightlightTimer) { clearInterval(nightlightTimer); nightlightTimer = null; }
        }).catch(function () {});
      }, 2000);
    }

    /* ---- AI prompt ---- (returns a structured result the skin can present) */
    function sendPrompt(text) {
      text = String(text || "").trim();
      if (!text) return Promise.resolve({ ok: false, reason: "empty" });
      if (state.demo) return Promise.resolve({ ok: true, demo: true });
      return apiFetch("/api/prompt", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ prompt: text })
      }).then(function () {
        scheduleRefresh();
        return { ok: true };
      }, function (err) {
        var reason = "error";
        if (err.status === 429) reason = "rate_limited";
        else if (err.status === 409) reason = "busy";
        else if (err.status === 400) reason = "bad_request";
        return { ok: false, reason: reason, status: err.status, body: err.body };
      });
    }

    /* ---- config (settings view) ---- */
    function getConfig() {
      return apiFetch("/api/config").then(function (c) { state.config = c; notify(); return c; },
        function () { return null; });
    }
    function saveConfig(body) {
      body = body || {};
      // Reflect saved values into local state so the settings view updates
      // immediately (device applies ledCount on reboot; demo has no device).
      if (state.config) { for (var k in body) if (k !== "wifiPassword") state.config[k] = body[k]; }
      if (body.ledCount != null) { state.controller.ledCount = clampInt(body.ledCount, 1, 1000); notify(); }
      if (state.demo) return Promise.resolve({ ok: true, demo: true });
      return writeJson("/api/config", "POST", body).then(function () { return { ok: true }; },
        function (err) { return { ok: false, status: err.status }; });
    }

    /* ---- gamma (perceptual dimming curve) ---- */
    // Gamma is device state (persisted in NVS, applied on the render loop), so
    // unlike the client-side TRANSITION durations it flows through /api/config.
    // The one source of truth is state.config.gamma, populated by getConfig().
    var GAMMA_MIN = 1.0, GAMMA_MAX = 3.5, GAMMA_DEFAULT = 2.2;

    // Current gamma from device config, or the firmware default if unknown yet.
    function getGamma() {
      var g = state.config && state.config.gamma;
      return (typeof g === "number" && isFinite(g)) ? g : GAMMA_DEFAULT;
    }
    // Set gamma: clamp to the firmware's sane range and persist via /api/config.
    // The device re-applies it live on the render loop (single-writer command
    // bus); resolves with { ok } like saveConfig. Snaps local state immediately.
    function setGamma(v) {
      v = Number(v);
      if (!isFinite(v)) v = GAMMA_DEFAULT;
      if (v < GAMMA_MIN) v = GAMMA_MIN;
      if (v > GAMMA_MAX) v = GAMMA_MAX;
      v = Math.round(v * 10) / 10; // 0.1 steps
      return saveConfig({ gamma: v });
    }

    // Dim-to-warm strength [0..1] — device state, same flow as gamma (/api/config,
    // applied live on the render loop). 0 = neutral/off.
    var WARMTH_MIN = 0.0, WARMTH_MAX = 1.0, WARMTH_DEFAULT = 0.6;
    function getWarmth() {
      var w = state.config && state.config.warmth;
      return (typeof w === "number" && isFinite(w)) ? w : WARMTH_DEFAULT;
    }
    function setWarmth(v) {
      v = Number(v);
      if (!isFinite(v)) v = WARMTH_DEFAULT;
      if (v < WARMTH_MIN) v = WARMTH_MIN;
      if (v > WARMTH_MAX) v = WARMTH_MAX;
      v = Math.round(v * 20) / 20; // 0.05 steps
      return saveConfig({ warmth: v });
    }

    /* ---- firmware auto-update (pull-based OTA) ---- */
    // The device does check/apply asynchronously (a worker runs the blocking
    // HTTPS transfer), so the flow is: trigger, then poll /api/firmware/status.
    function currentVersion() {
      return (state.info && state.info.firmware && state.info.firmware.version) ||
             (state.status && state.status.version) || "";
    }

    function firmwareStatus() {
      if (state.demo) return Promise.resolve(null);
      return apiFetch("/api/firmware/status").catch(function () { return null; });
    }

    // Poll status until `isDone(status)` returns true (or we run out of tries).
    // `onTick` is called with each status. Resolves with the final status.
    function pollFirmware(isDone, onTick, tries, intervalMs) {
      return new Promise(function (resolve) {
        var left = tries;
        function step() {
          firmwareStatus().then(function (s) {
            if (typeof onTick === "function" && s) onTick(s);
            if (s && isDone(s)) { resolve(s); return; }
            if (--left <= 0) { resolve(s || { phase: "error", error: "Timed out" }); return; }
            setTimeout(step, intervalMs);
          });
        }
        step();
      });
    }

    // Trigger a check and resolve once it settles (up_to_date | available | error).
    function checkFirmware() {
      if (state.demo) {
        return Promise.resolve({ phase: "up_to_date", updateAvailable: false,
          current: currentVersion(), latest: currentVersion(), demo: true });
      }
      return apiFetch("/api/firmware/check", { method: "POST" }).then(function () {
        return pollFirmware(function (s) {
          return s.phase === "up_to_date" || s.phase === "available" || s.phase === "error";
        }, null, 30, 1000);
      }, function (err) {
        return { phase: "error", error: (err && err.status === 409) ? "Updater busy" : "Check failed" };
      });
    }

    // Trigger an apply for ONE target and poll progress. `onProgress(status)`
    // fires each tick. Resolves when the device reboots (or reports an error). A
    // dropped connection mid-poll is treated as the expected reboot. The two
    // targets are fully independent on the device — this only picks the route.
    function applyTarget(path, onProgress) {
      if (state.demo) {
        return Promise.resolve({ phase: "error", error: "Not available in demo mode" });
      }
      return apiFetch(path, { method: "POST" }).then(function () {
        var sawFlashing = false;
        return pollFirmware(function (s) {
          if (!s) return sawFlashing; // connection dropped after flashing == reboot
          if (s.phase === "downloading" || s.phase === "verifying" || s.phase === "flashing") sawFlashing = true;
          return s.phase === "rebooting" || s.phase === "error";
        }, onProgress, 240, 1000);
      }, function (err) {
        return { phase: "error", error: (err && err.status === 409) ? "Updater busy"
          : (err && err.status === 400) ? "No update available" : "Update failed" };
      });
    }

    // The normal path: one atomic update that flashes whatever is behind
    // (filesystem + firmware) and reboots once — the device can't half-update.
    function applyUpdate(onProgress)    { return applyTarget("/api/firmware/update", onProgress); }
    // Low-level per-image applies, kept for recovery/debug.
    function updateFirmware(onProgress) { return applyTarget("/api/firmware/update/app", onProgress); }
    function updateWebUi(onProgress)    { return applyTarget("/api/firmware/update/fs", onProgress); }

    return {
      state: state,
      on: on,
      start: start,
      TRANSITION: TRANSITION,   // premium easing durations (ms); tune here
      setTransition: setTransition,  // live-tune + persist a TRANSITION duration
      GAMMA: { MIN: GAMMA_MIN, MAX: GAMMA_MAX, DEFAULT: GAMMA_DEFAULT }, // gamma bounds for UI
      // catalogue helpers
      effectById: effectById,
      paletteName: paletteName,
      segmentById: segmentById,
      selectedSegment: selectedSegment,
      defaultParamsFor: defaultParamsFor,
      // mutations
      setPower: setPower,
      setBrightness: setBrightness,
      selectSegment: selectSegment,
      setEffect: setEffect,
      setParam: setParam,
      setPalette: setPalette,
      setSegmentBrightness: setSegmentBrightness,
      setSegmentRange: setSegmentRange,
      createSegment: createSegment,
      deleteSegment: deleteSegment,
      startNightlight: startNightlight,
      stopNightlight: stopNightlight,
      sendPrompt: sendPrompt,
      getConfig: getConfig,
      saveConfig: saveConfig,
      getGamma: getGamma,
      setGamma: setGamma,
      getWarmth: getWarmth,
      setWarmth: setWarmth,
      checkFirmware: checkFirmware,
      applyUpdate: applyUpdate,
      updateFirmware: updateFirmware,
      updateWebUi: updateWebUi,
      firmwareStatus: firmwareStatus,
      refreshSegments: refreshSegments,
      refreshStatus: refreshStatus,
      // color helpers (shared by skins)
      util: { normalizeHex: normalizeHex, coerceColor: coerceColor, rgbArrayToHex: rgbArrayToHex, clampInt: clampInt }
    };
  }

  global.LumeEngine = { create: create, FALLBACK_EFFECTS: FALLBACK_EFFECTS, FALLBACK_PALETTES: FALLBACK_PALETTES };
})(typeof window !== "undefined" ? window : this);
