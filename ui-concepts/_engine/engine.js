/* ==========================================================================
   LUME — shared UI engine  (window.LumeEngine)
   --------------------------------------------------------------------------
   ONE engine, many skins. Every skin (euclid, console-euclid, …) loads this
   file first, then its own view script. The view NEVER talks to the network
   directly — it calls engine methods and re-renders from `engine.state` on the
   "change" event. All device-API correctness lives here, in one place, so the
   look-and-feel skins cannot drift from the real firmware contract again.

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

  var FALLBACK_EFFECTS = [
    fx("solid", "Solid Color", "Solid", [C("color", "Color", "#ff0000")]),
    fx("gradient", "Gradient", "Solid", [C("colorStart", "Start Color", "#0000ff"), C("colorEnd", "End Color", "#ff0000")]),
    fx("breathe", "Breathe", "Animated", [C("color", "Color", "#0000ff"), I("speed", "Speed", 128, 1, 255)]),
    fx("candle", "Candle", "Animated", [C("color", "Color", "#ff8c28"), I("speed", "Flicker Speed", 128, 1, 255), I("intensity", "Flicker Intensity", 128, 1, 255)]),
    fx("colorwaves", "Color Waves", "Animated", [P("palette", "Palette"), I("speed", "Speed", 128, 1, 255)]),
    fx("comet", "Comet", "Moving", [C("colorHead", "Head Color", "#ffffff"), C("colorTail", "Tail Color", "#0000ff"), I("speed", "Speed", 128, 1, 255), I("intensity", "Tail Length", 120, 1, 255), E("direction", "Direction", "Up|Down", 0)]),
    fx("fire", "Fire", "Animated", [I("cooling", "Cooling", 55, 20, 100), I("sparking", "Sparking", 120, 50, 200), B("reversed", "Reversed", false)]),
    fx("fireup", "Fire Up", "Animated", [I("speed", "Sparking", 120, 1, 255), I("intensity", "Cooling", 55, 1, 255)]),
    fx("meteor", "Meteor", "Moving", [C("color", "Meteor Color", "#ffffff"), I("speed", "Fall Speed", 128, 1, 255)]),
    fx("noise", "Noise", "Animated", [P("palette", "Palette"), I("speed", "Speed", 128, 1, 255)]),
    fx("pacifica", "Pacifica", "Animated", [I("speed", "Wave Speed", 128, 1, 255)]),
    fx("pride", "Pride", "Animated", [I("speed", "Scroll Speed", 128, 1, 255)]),
    fx("pulse", "Pulse", "Animated", [C("color", "Color", "#ff0000"), I("speed", "Speed", 128, 1, 255)]),
    fx("rain", "Rain", "Moving", [C("color", "Drop Color", "#0000ff"), I("speed", "Fall Speed", 128, 1, 255), I("intensity", "Drop Density", 128, 1, 255)]),
    fx("rainbow", "Rainbow", "Animated", [I("speed", "Speed", 128, 1, 255), I("density", "Density", 85, 1, 255)]),
    fx("scanner", "Scanner", "Moving", [C("color", "Color", "#ff0000"), I("speed", "Speed", 128, 1, 255), I("intensity", "Tail Length", 80, 1, 255)]),
    fx("sinelon", "Sinelon", "Moving", [P("palette", "Palette"), I("speed", "Speed", 128, 1, 255)]),
    fx("sparkle", "Sparkle", "Animated", [C("color", "Background Color", "#0000ff"), I("speed", "Sparkle Density", 128, 1, 255)]),
    fx("twinkle", "Twinkle", "Animated", [C("color", "Color", "#ffffff"), I("speed", "Twinkle Rate", 128, 1, 255)]),
    fx("wave", "Wave", "Moving", [C("color", "Wave Color", "#0000ff"), I("speed", "Wave Speed", 128, 1, 255), I("intensity", "Wave Width", 160, 1, 255), E("direction", "Direction", "Up|Down|Center", 0)])
  ];

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

    function applyControllerAndSegments(payload) {
      if (!payload) return;
      if (payload.controller) {
        state.controller.power = !!payload.controller.power;
        state.controller.brightness = clampInt(payload.controller.brightness, 0, 255);
        state.controller.ledCount = Number(payload.controller.ledCount) || state.controller.ledCount;
      } else {
        // GET /api/v2/segments returns power/brightness/ledCount at top level
        if (typeof payload.power === "boolean") state.controller.power = payload.power;
        if (payload.brightness != null) state.controller.brightness = clampInt(payload.brightness, 0, 255);
        if (payload.ledCount != null) state.controller.ledCount = Number(payload.ledCount);
      }
      if (Array.isArray(payload.segments)) {
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
      notify();
      fireWrite("/api/v2/segments/" + id, "PUT", { palette: index });
    }

    function setSegmentBrightness(id, v) {
      var seg = segmentById(id);
      if (!seg) return;
      v = clampInt(v, 0, 255);
      seg.brightness = v;
      notify();
      fireWrite("/api/v2/segments/" + id, "PUT", { brightness: v });
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
      if (state.demo) {
        var nid = state.segments.reduce(function (m, s) { return Math.max(m, s.id); }, -1) + 1;
        state.segments.push(normalizeSegment({
          id: nid, start: body.start, stop: body.start + body.length - 1, length: body.length,
          reverse: !!cfg.reverse, brightness: 255, effect: cfg.effect || "solid",
          params: body.params || defaultParamsFor(cfg.effect || "solid"),
          _paletteIndex: cfg.palette != null ? cfg.palette : null
        }));
        state.selectedId = nid;
        notify();
        return Promise.resolve();
      }
      return writeJson("/api/v2/segments", "POST", body).then(scheduleRefresh, scheduleRefresh);
    }

    function deleteSegment(id) {
      if (state.demo) {
        state.segments = state.segments.filter(function (s) { return s.id !== id; });
        if (state.selectedId === id) state.selectedId = state.segments.length ? state.segments[0].id : null;
        notify();
        return Promise.resolve();
      }
      return writeJson("/api/v2/segments/" + id, "DELETE", undefined).then(scheduleRefresh, scheduleRefresh);
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

    // Two independent apply actions (mirror `pio run -t upload` / `-t uploadfs`).
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
