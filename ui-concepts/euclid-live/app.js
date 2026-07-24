// =============================================================================
// LUME — "Byrne" concept — engine-bound build
// All device communication goes through window.LumeEngine (see
// ui-concepts/_engine/engine.js + ENGINE_API.md). This file only renders DOM
// from engine.state and calls engine methods on interaction. It never calls
// fetch(), never touches a WebSocket, and never hand-builds an /api/ payload.
// =============================================================================
(function(){
  "use strict";

  var engine = window.LumeEngine.create();

  var SWATCH_PRESETS = ["#D8492C","#F2B233","#2E6DB4","#16130E","#E8DEC9","#3E8E4F"];
  var MAX_SEGMENTS = 8;

  var $ = function (sel, root) { return (root || document).querySelector(sel); };
  var $$ = function (sel, root) { return Array.from((root || document).querySelectorAll(sel)); };
  function clamp(v, min, max){ return Math.max(min, Math.min(max, v)); }

  function hexToRgb(hex){
    var m = String(hex || "#000000").replace("#", "").match(/.{1,2}/g);
    return m ? m.map(function (h) { return parseInt(h, 16); }) : [0, 0, 0];
  }

  // ---------------------------------------------------------------------
  // Toast
  // ---------------------------------------------------------------------
  var toastTimer = null;
  function toast(msg){
    var el = $("#toast");
    el.textContent = msg;
    el.classList.add("toast--show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { el.classList.remove("toast--show"); }, 2200);
  }
  engine.on("toast", toast);

  // =========================================================================
  // Category → representative color + glyph sample generator
  // =========================================================================
  var CATEGORY_ACCENT = { Solid: "var(--ink)", Animated: "var(--red)", Moving: "var(--blue)", Special: "var(--yellow)" };

  var GLYPH_SAMPLES = 32;
  function mulberry32(seed){
    var a = seed >>> 0;
    return function () {
      a |= 0; a = (a + 0x6D2B79F5) | 0;
      var t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  function seedFor(str){
    var h = 0;
    for (var i = 0; i < str.length; i++) { h = (h * 31 + str.charCodeAt(i)) | 0; }
    return h;
  }
  function clamp01(v){ return Math.max(0, Math.min(1, v)); }

  function sampleFlat(){ return new Array(GLYPH_SAMPLES).fill(0.62); }
  function sampleGradient(){
    return Array.from({ length: GLYPH_SAMPLES }, function (_, i) { return 0.12 + (i / (GLYPH_SAMPLES - 1)) * 0.8; });
  }
  function sampleBreathe(){
    return Array.from({ length: GLYPH_SAMPLES }, function (_, i) {
      var t = i / (GLYPH_SAMPLES - 1) * Math.PI * 2;
      return 0.5 + 0.42 * Math.sin(t - Math.PI / 2);
    });
  }
  function samplePulse(){
    return Array.from({ length: GLYPH_SAMPLES }, function (_, i) {
      var t = (i / GLYPH_SAMPLES) * 3;
      var frac = t - Math.floor(t);
      var tri = frac < 0.5 ? frac * 2 : 2 - frac * 2;
      return 0.1 + tri * 0.8;
    });
  }
  function sampleFlicker(seed, roughness){
    var rnd = mulberry32(seed);
    var vals = [];
    var v = 0.55;
    for (var i = 0; i < GLYPH_SAMPLES; i++) {
      v += (rnd() - 0.5) * roughness;
      v = clamp01(v * 0.85 + 0.55 * 0.15);
      vals.push(clamp01(v));
    }
    return vals;
  }
  function sampleSparse(seed, spikeChance){
    var rnd = mulberry32(seed);
    return Array.from({ length: GLYPH_SAMPLES }, function () {
      return rnd() < spikeChance ? 0.55 + rnd() * 0.4 : 0.06 + rnd() * 0.08;
    });
  }
  function sampleCyclingWave(seed){
    var rnd = mulberry32(seed);
    var driftRate = 0.6 + rnd() * 0.6;
    return Array.from({ length: GLYPH_SAMPLES }, function (_, i) {
      var t = i / (GLYPH_SAMPLES - 1);
      return 0.5 + 0.4 * Math.sin(t * Math.PI * 2 * driftRate + t * t * 3);
    });
  }
  function sampleTravelingBump(seed, width){
    var rnd = mulberry32(seed);
    var center = 0.15 + rnd() * 0.15;
    return Array.from({ length: GLYPH_SAMPLES }, function (_, i) {
      var x = i / (GLYPH_SAMPLES - 1);
      var d = (x - center) / width;
      return 0.08 + 0.85 * Math.exp(-d * d * 4);
    });
  }
  function sampleLayeredWave(seed){
    var rnd = mulberry32(seed);
    var f2 = 2 + Math.floor(rnd() * 2);
    return Array.from({ length: GLYPH_SAMPLES }, function (_, i) {
      var t = i / (GLYPH_SAMPLES - 1) * Math.PI * 2;
      var a = Math.sin(t);
      var b = Math.sin(t * f2 + 1.3) * 0.4;
      return clamp01(0.5 + (a + b) * 0.32);
    });
  }
  function samplePulseTrain(seed, density){
    var rnd = mulberry32(seed);
    return Array.from({ length: GLYPH_SAMPLES }, function () { return (rnd() < density ? 0.85 : 0.15); });
  }

  var GLYPH_GENERATORS = {
    solid: function () { return sampleFlat(); },
    gradient: function () { return sampleGradient(); },
    breathe: function () { return sampleBreathe(); },
    pulse: function () { return samplePulse(); },
    candle: function (fx) { return sampleFlicker(seedFor(fx.id), 0.32); },
    fire: function (fx) { return sampleFlicker(seedFor(fx.id), 0.5); },
    fireup: function (fx) { return sampleFlicker(seedFor(fx.id), 0.5); },
    noise: function (fx) { return sampleLayeredWave(seedFor(fx.id)); },
    twinkle: function (fx) { return sampleSparse(seedFor(fx.id), 0.28); },
    sparkle: function (fx) { return sampleSparse(seedFor(fx.id), 0.22); },
    rainbow: function (fx) { return sampleCyclingWave(seedFor(fx.id)); },
    pride: function (fx) { return sampleCyclingWave(seedFor(fx.id)); },
    colorwaves: function (fx) { return sampleCyclingWave(seedFor(fx.id)); },
    comet: function (fx) { return sampleTravelingBump(seedFor(fx.id), 0.14); },
    meteor: function (fx) { return sampleTravelingBump(seedFor(fx.id), 0.22); },
    scanner: function (fx) { return sampleTravelingBump(seedFor(fx.id), 0.1); },
    sinelon: function (fx) { return sampleTravelingBump(seedFor(fx.id), 0.17); },
    wave: function (fx) { return sampleLayeredWave(seedFor(fx.id)); },
    pacifica: function (fx) { return sampleLayeredWave(seedFor(fx.id)); },
    rain: function (fx) { return samplePulseTrain(seedFor(fx.id), 0.3); }
  };

  // Category-keyed fallbacks for any effect id the glyph table doesn't know
  // by name (keeps the specimen rail meaningful for future/renamed effects).
  var CATEGORY_GLYPH_FALLBACK = {
    Solid: function () { return sampleFlat(); },
    Animated: function (fx) { return sampleLayeredWave(seedFor(fx.id)); },
    Moving: function (fx) { return sampleTravelingBump(seedFor(fx.id), 0.16); },
    Special: function (fx) { return sampleSparse(seedFor(fx.id), 0.3); }
  };

  function samplesFor(effect){
    var gen = GLYPH_GENERATORS[effect.id];
    if (gen) return gen(effect);
    var catGen = CATEGORY_GLYPH_FALLBACK[effect.category];
    return catGen ? catGen(effect) : sampleFlat();
  }

  function glyphSvgFor(effect, isActive){
    var samples = samplesFor(effect);
    var w = 100, h = 54, padX = 4, padY = 6;
    var plotW = w - padX * 2, plotH = h - padY * 2;
    var stroke = isActive ? "var(--red)" : "var(--ink)";
    var pts = samples.map(function (v, i) {
      var x = padX + (i / (samples.length - 1)) * plotW;
      var y = padY + (1 - v) * plotH;
      return x.toFixed(2) + "," + y.toFixed(2);
    }).join(" ");
    var baseline = h - padY;
    return '<svg viewBox="0 0 ' + w + ' ' + h + '" preserveAspectRatio="none">' +
      '<line x1="' + padX + '" y1="' + baseline + '" x2="' + (w - padX) + '" y2="' + baseline + '" stroke="var(--ink-hair)" stroke-width="0.5"/>' +
      '<polyline points="' + pts + '" fill="none" stroke="' + stroke + '" stroke-width="1" stroke-linejoin="round" stroke-linecap="round"/>' +
      '</svg>';
  }

  // Pick a single representative swatch color for a segment: first color
  // param if the effect has one, else a palette swatch, else a neutral ink.
  function representativeColor(seg){
    var eff = engine.effectById(seg.effect);
    if (eff) {
      for (var i = 0; i < eff.params.length; i++) {
        if (eff.params[i].type === "color") return seg.params[eff.params[i].id];
      }
    }
    if (seg._paletteIndex != null) {
      var pal = engine.state.palettes.filter(function (p) { return p.id === seg._paletteIndex; })[0];
      if (pal) return paletteSwatchColor(pal.id);
    }
    return "#8a8a78";
  }

  // Static preview-only gradient stops per known device palette preset id,
  // purely for the strip/segment-tab swatch render (the device only returns
  // {id, name} for palettes, no color data).
  var PALETTE_STOPS = {
    0: ["#D8492C", "#F2B233", "#3E8E4F", "#2E6DB4", "#7A4FA3"],
    1: ["#16130E", "#7A1F0E", "#D8492C", "#F2B233"],
    2: ["#0E2A3D", "#164C6B", "#2E6DB4", "#7FB8D8"],
    3: ["#D8492C", "#F2B233", "#2E6DB4", "#B0357A"],
    4: ["#1E3B1E", "#3E8E4F", "#7BAA3C", "#C9C083"],
    5: ["#E8DEC9", "#C9C4B4", "#8FA9C2", "#2E6DB4"],
    6: ["#16130E", "#D8492C", "#F2B233", "#FCEBB0"],
    7: ["#7A4FA3", "#B0357A", "#D8492C", "#F2B233"], // Sunset
    8: ["#5C3A16", "#A65A24", "#D8492C", "#C9A83C"], // Autumn
    9: ["#2E6DB4", "#3E8E4F", "#F2B233", "#D8492C"], // Retro
    10: ["#0E2A3D", "#2E6DB4", "#7FB8D8", "#E8F2F7"], // Ice
    11: ["#5C1740", "#B0357A", "#E86FA6", "#F2C6DA"] // Pink
  };
  var FALLBACK_STOPS = ["#8a8478", "#5c574c"];
  function paletteStops(id){ return PALETTE_STOPS[id] || FALLBACK_STOPS; }
  function paletteSwatchColor(id){ return paletteStops(id)[0]; }

  // =========================================================================
  // STRIP DIAGRAM (FIG. 1)
  // =========================================================================
  var stripAnimHandle = null;
  var stripPhase = 0;
  var stripCells = [];
  var stripCellCount = 0;
  var stripScale = 1;
  var stripBuiltForLedCount = -1;

  function buildStripIfNeeded(){
    var total = engine.state.controller.ledCount;
    if (total === stripBuiltForLedCount) return;
    stripBuiltForLedCount = total;
    var container = $("#strip-render");
    container.innerHTML = "";
    stripCellCount = Math.max(1, Math.min(total, 150));
    stripScale = total / stripCellCount;
    stripCells = [];
    for (var i = 0; i < stripCellCount; i++) {
      var div = document.createElement("div");
      div.className = "strip-cell";
      container.appendChild(div);
      stripCells.push(div);
    }
    if (!stripAnimHandle) {
      var last = performance.now();
      var tick = function (now) {
        var dt = (now - last) / 1000;
        last = now;
        stripPhase += dt;
        paintStrip();
        stripAnimHandle = requestAnimationFrame(tick);
      };
      stripAnimHandle = requestAnimationFrame(tick);
    }
  }

  function segmentPreviewColors(seg){
    var eff = engine.effectById(seg.effect);
    if (eff && eff.usesPalette && seg._paletteIndex != null) return paletteStops(seg._paletteIndex);
    if (eff) {
      var colors = [];
      for (var i = 0; i < eff.params.length; i++) {
        if (eff.params[i].type === "color") colors.push(seg.params[eff.params[i].id]);
      }
      if (colors.length) return colors;
    }
    return ["#8a8a8a"];
  }

  function paintStrip(){
    var ctl = engine.state.controller;
    var brightnessFactor = ctl.power ? ctl.brightness / 255 : 0;
    var segs = engine.state.segments;
    var selectedId = engine.state.selectedId;
    for (var i = 0; i < stripCellCount; i++) {
      var ledIndex = Math.floor(i * stripScale);
      var seg = null;
      for (var s = 0; s < segs.length; s++) {
        if (ledIndex >= segs[s].start && ledIndex <= segs[s].stop) { seg = segs[s]; break; }
      }
      var color = "#0c0a07";
      if (seg) {
        var colors = segmentPreviewColors(seg);
        var eff = engine.effectById(seg.effect);
        var c;
        if (colors.length === 1) {
          c = colors[0];
        } else {
          var speedNorm = 40;
          if (eff) {
            for (var p = 0; p < eff.params.length; p++) {
              if (eff.params[p].id === "speed") { speedNorm = seg.params[eff.params[p].id]; break; }
            }
          }
          var t = (i / stripCellCount * colors.length * 2 + stripPhase * (speedNorm / 255) * 6) % colors.length;
          var idx = Math.floor(t);
          c = colors[((idx % colors.length) + colors.length) % colors.length];
        }
        var rgb = hexToRgb(c);
        var isActive = seg.id === selectedId;
        var dim = isActive ? 1 : 0.45;
        color = "rgb(" + Math.round(rgb[0] * brightnessFactor * dim) + ", " + Math.round(rgb[1] * brightnessFactor * dim) + ", " + Math.round(rgb[2] * brightnessFactor * dim) + ")";
      }
      stripCells[i].style.backgroundColor = color;
    }
  }

  function renderSegmentMarks(){
    var wrap = $("#strip-segments");
    wrap.innerHTML = "";
    var total = engine.state.controller.ledCount;
    var selectedId = engine.state.selectedId;
    engine.state.segments.forEach(function (seg) {
      var mark = document.createElement("div");
      mark.className = "strip-segment-mark" + (seg.id === selectedId ? " strip-segment-mark--active" : "");
      var pct = (seg.start / total) * 100;
      mark.style.left = pct + "%";
      mark.style.width = (seg.length / total * 100) + "%";
      mark.innerHTML = '<span class="mono-label">S' + seg.id + " &middot; " + seg.length + 'px</span>';
      mark.addEventListener("click", function (id) { return function () { engine.selectSegment(id); }; }(seg.id));
      wrap.appendChild(mark);
    });
  }

  function renderRuler(){
    $("#led-total-label").textContent = engine.state.controller.ledCount + " LED";
  }

  // =========================================================================
  // POWER + BRIGHTNESS (FIG. 1 controls)
  // =========================================================================
  function renderPower(){
    var btn = $("#btn-power");
    var on = engine.state.controller.power;
    btn.setAttribute("aria-pressed", String(on));
    $("#power-label").textContent = "LUX — " + (on ? "ON" : "OFF");
  }

  $("#btn-power").addEventListener("click", function () {
    engine.setPower(!engine.state.controller.power);
    toast(engine.state.controller.power ? "Power engaged" : "Power withdrawn");
  });

  // ---- generic scale-slider factory --------------------------------------
  function makeSlider(opts){
    var track = opts.track, plane = opts.plane, marker = opts.marker;
    var min = opts.min, max = opts.max, get = opts.get, set = opts.set, format = opts.format, onCommit = opts.onCommit;
    var dragging = false;

    function valToPct(v){ return (v - min) / (max - min); }

    function render(){
      var v = get();
      var pct = clamp(valToPct(v), 0, 1) * 100;
      plane.style.width = pct + "%";
      marker.style.left = pct + "%";
      track.setAttribute("aria-valuenow", Math.round(v));
      if (format) format(v);
    }

    function fromClientX(clientX){
      var rect = track.getBoundingClientRect();
      var pct = clamp((clientX - rect.left) / rect.width, 0, 1);
      var v = Math.round(min + pct * (max - min));
      set(v);
      render();
    }

    function onDown(e){
      dragging = true;
      track.classList.add("scale-slider--dragging");
      var x = e.touches ? e.touches[0].clientX : e.clientX;
      fromClientX(x);
      e.preventDefault();
    }
    function onMove(e){
      if (!dragging) return;
      var x = e.touches ? e.touches[0].clientX : e.clientX;
      fromClientX(x);
    }
    function onUp(){
      if (!dragging) return;
      dragging = false;
      track.classList.remove("scale-slider--dragging");
      if (onCommit) onCommit(get());
    }

    track.addEventListener("mousedown", onDown);
    track.addEventListener("touchstart", onDown, { passive: false });
    window.addEventListener("mousemove", onMove);
    window.addEventListener("touchmove", onMove, { passive: false });
    window.addEventListener("mouseup", onUp);
    window.addEventListener("touchend", onUp);

    track.addEventListener("keydown", function (e) {
      var v = get();
      var nv = v;
      if (e.key === "ArrowRight" || e.key === "ArrowUp") nv = clamp(v + 1, min, max);
      else if (e.key === "ArrowLeft" || e.key === "ArrowDown") nv = clamp(v - 1, min, max);
      else if (e.key === "Home") nv = min;
      else if (e.key === "End") nv = max;
      else return;
      e.preventDefault();
      set(nv);
      render();
      if (onCommit) onCommit(get());
    });

    render();
    return { render: render };
  }

  var brightnessSlider = makeSlider({
    track: $("#brightness-slider"), plane: $("#brightness-plane"), marker: $("#brightness-marker"),
    min: 0, max: 255,
    get: function () { return engine.state.controller.brightness; },
    set: function (v) { engine.state.controller.brightness = v; paintStrip(); },
    format: function (v) { $("#brightness-value").textContent = v + " / 255"; },
    onCommit: function (v) { engine.setBrightness(v); }
  });

  // =========================================================================
  // SEGMENTS (FIG. 2)
  // =========================================================================
  function renderSegmentTabs(){
    var rail = $("#segment-tabs");
    rail.innerHTML = "";
    var selectedId = engine.state.selectedId;
    engine.state.segments.forEach(function (seg) {
      var btn = document.createElement("button");
      btn.className = "segment-tab" + (seg.id === selectedId ? " segment-tab--active" : "");
      btn.setAttribute("role", "tab");
      btn.setAttribute("aria-selected", String(seg.id === selectedId));
      var color = representativeColor(seg);
      btn.innerHTML =
        '<span class="segment-tab__glyph">' +
          '<span class="segment-tab__glyph-fill" style="background:' + color + '"></span>' +
          '<span class="segment-tab__glyph-line"></span>' +
        '</span>' +
        '<span class="mono-label segment-tab__id">S' + seg.id + '</span>' +
        '<span class="mono-label segment-tab__range">' + seg.start + '&ndash;' + seg.stop + '</span>';
      btn.addEventListener("click", function (id) { return function () { engine.selectSegment(id); }; }(seg.id));
      rail.appendChild(btn);
    });
    $("#btn-add-segment").disabled = engine.state.segments.length >= MAX_SEGMENTS;
    $("#btn-delete-segment").disabled = engine.state.segments.length <= 1;
  }

  $("#btn-add-segment").addEventListener("click", function () {
    var segs = engine.state.segments;
    if (segs.length >= MAX_SEGMENTS) return;
    var total = engine.state.controller.ledCount;
    var usedEnd = segs.reduce(function (max, s) { return Math.max(max, s.stop + 1); }, 0);
    var remaining = total - usedEnd;
    if (remaining < 1) { toast("No room left on the locus"); return; }
    var length = Math.min(remaining, 30);
    engine.createSegment({ start: usedEnd, length: length, effect: "solid" });
    toast("Segment constructed");
  });

  $("#btn-delete-segment").addEventListener("click", function () {
    var seg = engine.selectedSegment();
    if (!seg) return;
    if (engine.state.segments.length <= 1) { toast("At least one segment must remain"); return; }
    var id = seg.id;
    engine.deleteSegment(id);
    toast("Segment " + id + " dissolved");
  });

  // =========================================================================
  // EFFECT SPECIMEN RAIL (FIG. 3)
  // =========================================================================
  function renderEffectRail(){
    var rail = $("#effect-rail");
    rail.innerHTML = "";
    var seg = engine.selectedSegment();
    if (!seg) return;
    engine.state.effects.forEach(function (fx, i) {
      var btn = document.createElement("button");
      var isActive = fx.id === seg.effect;
      btn.className = "specimen" + (isActive ? " specimen--active" : "");
      btn.innerHTML =
        '<span class="specimen__glyph">' +
          '<span class="specimen__cat" style="background:' + (CATEGORY_ACCENT[fx.category] || "var(--ink-hair)") + '"></span>' +
          glyphSvgFor(fx, isActive) +
        '</span>' +
        '<span class="specimen__cap">' +
          '<span class="mono-label specimen__idx">' + String(i + 1).padStart(2, "0") + '</span>' +
          '<span class="mono-label specimen__name">' + fx.name + '</span>' +
        '</span>';
      btn.title = fx.category;
      btn.addEventListener("click", function () { engine.setEffect(seg.id, fx.id); });
      rail.appendChild(btn);
    });
    var eff = engine.effectById(seg.effect);
    $("#effect-current-label").textContent = eff ? eff.name.toUpperCase() : seg.effect.toUpperCase();
  }

  // =========================================================================
  // PARAMS (FIG. 4) — schema-driven, rebuilt whenever segment/effect changes
  // =========================================================================
  var lastParamGridKey = null; // "<segId>:<effectId>" — rebuild DOM only when this changes

  function buildSwatchRow(container, currentHex, onPick){
    container.innerHTML = "";
    SWATCH_PRESETS.forEach(function (hex) {
      var b = document.createElement("button");
      b.className = "swatch";
      b.style.background = hex;
      b.type = "button";
      if (String(currentHex).toLowerCase() === hex.toLowerCase()) b.classList.add("swatch--active");
      b.addEventListener("click", function () { onPick(hex); });
      container.appendChild(b);
    });
    var custom = document.createElement("label");
    custom.className = "swatch swatch--custom";
    custom.title = "Custom color";
    var input = document.createElement("input");
    input.type = "color";
    input.value = currentHex;
    input.addEventListener("input", function (e) { onPick(e.target.value); });
    custom.appendChild(input);
    container.appendChild(custom);
  }

  function buildPaletteRow(container, currentId, onPick){
    container.innerHTML = "";
    engine.state.palettes.forEach(function (p) {
      var b = document.createElement("button");
      b.className = "palette-chip" + (p.id === currentId ? " palette-chip--active" : "");
      b.type = "button";
      var bar = document.createElement("span");
      bar.className = "palette-chip__bar";
      paletteStops(p.id).forEach(function (c) {
        var s = document.createElement("span");
        s.style.background = c;
        bar.appendChild(s);
      });
      b.appendChild(bar);
      var label = document.createElement("span");
      label.className = "mono-label";
      label.textContent = p.name;
      b.appendChild(label);
      b.addEventListener("click", function () { onPick(p.id); });
      container.appendChild(b);
    });
  }

  function buildEnumPicker(container, options, currentIndex, onPick){
    container.innerHTML = "";
    options.forEach(function (label, idx) {
      var b = document.createElement("button");
      b.type = "button";
      b.className = "enum-pick" + (idx === currentIndex ? " enum-pick--active" : "");
      var span = document.createElement("span");
      span.className = "mono-label";
      span.textContent = label;
      b.appendChild(span);
      b.addEventListener("click", function () { onPick(idx); });
      container.appendChild(b);
    });
  }

  function paramLabelFor(p){ return String(p.name || p.id).toUpperCase(); }

  function renderParams(){
    var seg = engine.selectedSegment();
    var grid = $("#param-grid");
    if (!seg) { grid.innerHTML = ""; lastParamGridKey = null; return; }
    var eff = engine.effectById(seg.effect);
    var params = eff ? eff.params : [];
    var key = seg.id + ":" + seg.effect;

    if (key !== lastParamGridKey) {
      lastParamGridKey = key;
      grid.innerHTML = "";

      params.forEach(function (p, idx) {
        var block = document.createElement("div");
        block.className = "param";
        block.dataset.paramId = p.id;

        if (p.type === "int" || p.type === "float") {
          var sliderId = "dyn-slider-" + idx;
          block.innerHTML =
            '<div class="param__head"><span class="mono-label">' + paramLabelFor(p) + '</span>' +
            '<span class="mono-label param__value" data-role="value"></span></div>' +
            '<div class="scale-slider scale-slider--h" data-role="track" role="slider" tabindex="0" ' +
            'aria-label="' + paramLabelFor(p) + '" aria-valuemin="' + p.min + '" aria-valuemax="' + p.max + '">' +
            '<div class="scale-slider__ticks" aria-hidden="true"></div>' +
            '<div class="scale-slider__plane ' + (idx % 2 ? "scale-slider__plane--accent-b" : "scale-slider__plane--accent-a") + '" data-role="plane"></div>' +
            '<div class="scale-slider__marker" data-role="marker"></div></div>';
          grid.appendChild(block);
          var track = block.querySelector('[data-role="track"]');
          var plane = block.querySelector('[data-role="plane"]');
          var marker = block.querySelector('[data-role="marker"]');
          var valueEl = block.querySelector('[data-role="value"]');
          makeSlider({
            track: track, plane: plane, marker: marker,
            min: p.min, max: p.max,
            get: function () { var s = engine.selectedSegment(); return s ? s.params[p.id] : p.default; },
            set: function (v) {
              var s = engine.selectedSegment();
              if (s) s.params[p.id] = (p.type === "float") ? v : Math.round(v);
              paintStrip();
            },
            format: function (v) { valueEl.textContent = (p.type === "float") ? Number(v).toFixed(2) : Math.round(v); },
            onCommit: function (v) { engine.setParam(seg.id, p.id, v); }
          });
        } else if (p.type === "color") {
          block.innerHTML =
            '<div class="param__head"><span class="mono-label">' + paramLabelFor(p) + '</span>' +
            '<span class="mono-label param__value" data-role="hex"></span></div>' +
            '<div class="swatch-row" data-role="swatches"></div>';
          grid.appendChild(block);
        } else if (p.type === "bool") {
          block.innerHTML =
            '<div class="param__head">' +
            '<label class="toggle-hair" data-role="toggle-wrap">' +
            '<input type="checkbox" data-role="checkbox" />' +
            '<span class="toggle-hair__box" aria-hidden="true"></span>' +
            '<span class="mono-label" data-role="toggle-label">' + paramLabelFor(p) + '</span>' +
            '</label></div>';
          grid.appendChild(block);
          var checkbox = block.querySelector('[data-role="checkbox"]');
          checkbox.addEventListener("change", function (e) {
            engine.setParam(seg.id, p.id, e.target.checked);
          });
        } else if (p.type === "enum") {
          block.innerHTML =
            '<div class="param__head"><span class="mono-label">' + paramLabelFor(p) + '</span>' +
            '<span class="mono-label param__value" data-role="enum-value"></span></div>' +
            '<div class="enum-picker" data-role="enum-picker"></div>';
          grid.appendChild(block);
        } else if (p.type === "palette") {
          block.innerHTML =
            '<div class="param__head"><span class="mono-label">GIVEN PALETTE</span>' +
            '<span class="mono-label param__value" data-role="palette-name"></span></div>' +
            '<div class="palette-row" data-role="palette-row"></div>';
          grid.appendChild(block);
        }
      });
    }

    // Refresh values for the (possibly just-built) controls each render pass.
    params.forEach(function (p) {
      var block = grid.querySelector('[data-param-id="' + cssEscape(p.id) + '"]');
      if (!block) return;
      var value = seg.params[p.id];

      if (p.type === "color") {
        var hex = value || p.default;
        block.querySelector('[data-role="hex"]').textContent = String(hex).toUpperCase();
        buildSwatchRow(block.querySelector('[data-role="swatches"]'), hex, function (newHex) {
          engine.setParam(seg.id, p.id, newHex);
        });
      } else if (p.type === "bool") {
        var checkbox = block.querySelector('[data-role="checkbox"]');
        checkbox.checked = !!value;
        block.querySelector('[data-role="toggle-label"]').textContent = paramLabelFor(p) + (value ? " · ON" : " · OFF");
      } else if (p.type === "enum") {
        var options = String(p.options || "").split("|");
        var idx = clamp(Number(value) || 0, 0, Math.max(0, options.length - 1));
        block.querySelector('[data-role="enum-value"]').textContent = options[idx] || "";
        buildEnumPicker(block.querySelector('[data-role="enum-picker"]'), options, idx, function (newIdx) {
          engine.setParam(seg.id, p.id, newIdx);
        });
      } else if (p.type === "palette") {
        var pid = seg._paletteIndex;
        var pal = engine.state.palettes.filter(function (x) { return x.id === pid; })[0];
        block.querySelector('[data-role="palette-name"]').textContent = pal ? pal.name.toUpperCase() : "—";
        buildPaletteRow(block.querySelector('[data-role="palette-row"]'), pid, function (newIdx) {
          engine.setPalette(seg.id, newIdx);
        });
      } else if (p.type === "int" || p.type === "float") {
        // slider control re-renders itself from engine.state on the next
        // makeSlider render() call; forcing a value display refresh here
        // covers the case where params changed without a rebuild (rare).
        var valueEl = block.querySelector('[data-role="value"]');
        if (valueEl && document.activeElement !== block.querySelector('[data-role="track"]')) {
          valueEl.textContent = (p.type === "float") ? Number(value).toFixed(2) : Math.round(value);
          var track = block.querySelector('[data-role="track"]');
          var plane = block.querySelector('[data-role="plane"]');
          var marker = block.querySelector('[data-role="marker"]');
          var pct = clamp((value - p.min) / (p.max - p.min), 0, 1) * 100;
          plane.style.width = pct + "%";
          marker.style.left = pct + "%";
          track.setAttribute("aria-valuenow", Math.round(value));
        }
      }
    });
  }

  function cssEscape(s){
    return String(s).replace(/[^a-zA-Z0-9_-]/g, function (c) { return "\\" + c; });
  }

  // =========================================================================
  // NIGHTLIGHT (FIG. 5)
  // =========================================================================
  var NIGHTLIGHT_DURATIONS_MIN = [5, 15, 30, 45, 60, 90];
  var nightlightTargetSliderCtl = null;
  var nightlightUiTarget = 18;
  var nightlightUiDurationMin = 30;

  function renderNightlightCurve(){
    var curvePath = $("#nightlight-curve");
    var fillPath = $("#nightlight-fill");
    var dot = $("#nightlight-target-dot");
    var targetY = 80 - (nightlightUiTarget / 255) * 68 - 4;
    var y = clamp(targetY, 6, 78);
    curvePath.setAttribute("d", "M 0 12 L 150 12 Q 190 12 220 " + y);
    fillPath.setAttribute("d", "M 0 12 L 150 12 Q 190 12 220 " + y + " L 220 80 L 0 80 Z");
    dot.setAttribute("cx", 220);
    dot.setAttribute("cy", y);
  }

  function renderNightlight(){
    var nl = engine.state.nightlight;
    $("#nightlight-toggle").checked = nl.active;
    $("#nightlight-toggle-label").textContent = nl.active ? "ARMED" : "ARM";
    $("#nightlight-body").style.opacity = nl.active ? "1" : ".45";
    renderNightlightCurve();
  }

  $("#nightlight-toggle").addEventListener("change", function (e) {
    if (e.target.checked) {
      engine.startNightlight(nightlightUiDurationMin * 60, nightlightUiTarget);
      toast("Nightlight armed — diminishing to " + nightlightUiTarget + " over " + nightlightUiDurationMin + "m");
    } else {
      engine.stopNightlight();
      toast("Nightlight disarmed");
    }
  });

  function reArmNightlightIfActive(){
    if (!engine.state.nightlight.active) return;
    engine.startNightlight(nightlightUiDurationMin * 60, nightlightUiTarget);
  }

  function renderDurationPicks(){
    var wrap = $("#nightlight-duration-picks");
    wrap.innerHTML = "";
    NIGHTLIGHT_DURATIONS_MIN.forEach(function (min) {
      var b = document.createElement("button");
      b.type = "button";
      b.className = "duration-pick" + (min === nightlightUiDurationMin ? " duration-pick--active" : "");
      b.textContent = min >= 60 ? (min / 60) + "H" : min + "M";
      b.addEventListener("click", function () {
        nightlightUiDurationMin = min;
        $("#nightlight-duration-value").textContent = (min >= 60 ? (min / 60) + "H" : min + " MIN");
        renderDurationPicks();
        reArmNightlightIfActive();
      });
      wrap.appendChild(b);
    });
  }

  // =========================================================================
  // ORACLE — /api/prompt via engine.sendPrompt
  // =========================================================================
  function runOracle(promptText){
    var status = $("#oracle-status");
    var submit = $("#oracle-submit");
    if (!promptText.trim()) return;
    submit.disabled = true;
    status.textContent = "Resolving construction…";
    status.style.color = "var(--blue)";

    engine.sendPrompt(promptText).then(function (res) {
      submit.disabled = false;
      if (!res || !res.ok) {
        var reasonMsg = {
          rate_limited: "Too many requests — wait a moment and try again.",
          busy: "Instrument is busy resolving another prompt.",
          bad_request: "The Oracle could not parse that construction.",
          error: "Resolution failed — device unreachable or no AI key configured.",
          empty: ""
        }[(res && res.reason) || "error"] || "Resolution failed.";
        status.textContent = reasonMsg;
        status.style.color = "var(--red)";
        if (reasonMsg) toast(reasonMsg);
        return;
      }
      var seg = engine.selectedSegment();
      status.textContent = res.demo ? "Construction resolved (demo)." : "Construction resolved.";
      status.style.color = "var(--blue)";
      toast(seg ? "Segment " + seg.id + " updated" : "Updated");
    });
  }

  $("#oracle-form").addEventListener("submit", function (e) {
    e.preventDefault();
    runOracle($("#oracle-input").value);
  });
  $$(".oracle__chip").forEach(function (chip) {
    chip.addEventListener("click", function () {
      var p = chip.dataset.prompt;
      $("#oracle-input").value = p;
      runOracle(p);
    });
  });

  // =========================================================================
  // SETTINGS VIEW
  // =========================================================================
  function showSettings(){
    $("#view-main").hidden = true;
    $("#view-settings").hidden = false;
    $("#view-settings").classList.remove("view--leaving");
    window.scrollTo({ top: 0, behavior: "instant" in window ? "instant" : "auto" });
  }
  function showMain(){
    $("#view-settings").hidden = true;
    $("#view-main").hidden = false;
    $("#view-main").classList.remove("view--leaving");
    window.scrollTo({ top: 0, behavior: "instant" in window ? "instant" : "auto" });
  }

  $("#btn-settings").addEventListener("click", function () {
    $("#view-main").classList.add("view--leaving");
    setTimeout(function () {
      showSettings();
      loadConfigIntoSettings();
    }, 200);
  });
  $("#btn-back").addEventListener("click", function () {
    $("#view-settings").classList.add("view--leaving");
    setTimeout(showMain, 200);
  });

  function formatUptime(seconds){
    if (seconds == null) return null;
    var d = Math.floor(seconds / 86400);
    var h = Math.floor((seconds % 86400) / 3600);
    var m = Math.floor((seconds % 3600) / 60);
    if (d > 0) return d + "D " + h + "H " + m + "M";
    if (h > 0) return h + "H " + m + "M";
    return m + "M";
  }

  var settingsLoaded = false;
  function loadConfigIntoSettings(){
    // LED count + firmware/uptime/ip/signal come from state already loaded
    // at bootstrap; getConfig() supplements with the writable settings.
    reflectDeviceReadouts();
    engine.getConfig().then(function (cfg) {
      if (!cfg) return; // demo mode or unreachable: leave static placeholders
      if (cfg.ledCount != null) $("#ledcount-value").textContent = cfg.ledCount;
      if (cfg.aiModel != null || cfg.aiApiKey != null) {
        // never populate the password field with a real secret value; just
        // leave it blank (placeholder communicates "unchanged if left blank")
      }
      if (cfg.mqttEnabled != null) {
        $("#mqtt-toggle").checked = !!cfg.mqttEnabled;
        $("#mqtt-toggle-label").textContent = cfg.mqttEnabled ? "ENABLED" : "DISABLED";
        $("#mqtt-fields").style.opacity = cfg.mqttEnabled ? "1" : ".4";
      }
      if (cfg.mqttBroker != null) $("#mqtt-broker").value = cfg.mqttBroker;
      if (cfg.mqttPort != null) $("#mqtt-port").value = cfg.mqttPort;
      if (cfg.sacnEnabled != null) {
        $("#dmx-toggle").checked = !!cfg.sacnEnabled;
        $("#dmx-toggle-label").textContent = cfg.sacnEnabled ? "ENABLED" : "DISABLED";
      }
      if (cfg.wifiSsid != null) $("#wifi-ssid").value = cfg.wifiSsid;
      settingsLoaded = true;
    });
  }

  function reflectDeviceReadouts(){
    var info = engine.state.info;
    var status = engine.state.status;
    $("#ledcount-value").textContent = engine.state.controller.ledCount;
    if (info && info.firmware) {
      var fw = "v" + info.firmware.version + (info.firmware.buildHash ? "—" + info.firmware.buildHash : "");
      $("#info-firmware").textContent = fw;
      var otaCurrent = $("#ota-current");
      if (otaCurrent) otaCurrent.textContent = fw;
    }
    if (status) {
      if (status.uptime != null) $("#info-uptime").textContent = formatUptime(status.uptime) || "—";
      if (status.ip) $("#info-ip").textContent = status.ip;
      var rssi = status.wifi && status.wifi.rssi;
      if (rssi != null) {
        var bars = $$("#info-signal-bars .signal-bars__bar");
        var pct = clamp((rssi + 90) / 60, 0, 1);
        var lit = Math.max(1, Math.round(pct * bars.length));
        bars.forEach(function (bar, i) { bar.classList.toggle("signal-bars__bar--on", i < lit); });
      }
    }
  }

  // LED count stepper — client-side only until explicitly saved.
  $("#ledcount-stepper").addEventListener("click", function (e) {
    var btn = e.target.closest(".stepper-btn");
    if (!btn) return;
    var dir = parseInt(btn.dataset.dir, 10);
    var current = parseInt($("#ledcount-value").textContent, 10) || engine.state.controller.ledCount;
    var next = clamp(current + dir * 10, 1, (engine.state.info && engine.state.info.limits && engine.state.info.limits.maxLeds) || 1000);
    $("#ledcount-value").textContent = next;
  });

  $("#btn-ledcount-save").addEventListener("click", function () {
    var ledCount = parseInt($("#ledcount-value").textContent, 10);
    if (!ledCount || ledCount < 1) return;
    engine.saveConfig({ ledCount: ledCount }).then(function (res) {
      toast(res && res.ok ? "LED count saved" : "Failed to save LED count");
    });
  });

  $("#btn-aikey-save").addEventListener("click", function () {
    var key = $("#ai-key").value.trim();
    if (!key) { toast("Enter a key to save"); return; }
    engine.saveConfig({ aiApiKey: key }).then(function (res) {
      toast(res && res.ok ? "Credential saved" : "Failed to save credential");
      if (res && res.ok) $("#ai-key").value = "";
    });
  });

  $("#mqtt-toggle").addEventListener("change", function (e) {
    $("#mqtt-toggle-label").textContent = e.target.checked ? "ENABLED" : "DISABLED";
    $("#mqtt-fields").style.opacity = e.target.checked ? "1" : ".4";
  });
  $("#btn-mqtt-save").addEventListener("click", function () {
    var body = {
      mqttEnabled: $("#mqtt-toggle").checked,
      mqttBroker: $("#mqtt-broker").value.trim(),
      mqttPort: parseInt($("#mqtt-port").value, 10) || 1883
    };
    engine.saveConfig(body).then(function (res) {
      toast(res && res.ok ? "MQTT settings saved" : "Failed to save MQTT settings");
    });
  });

  $("#dmx-toggle").addEventListener("change", function (e) {
    $("#dmx-toggle-label").textContent = e.target.checked ? "ENABLED" : "DISABLED";
    engine.saveConfig({ sacnEnabled: e.target.checked }).then(function (res) {
      if (!res || !res.ok) toast("Failed to save sACN setting");
    });
  });

  $("#btn-wifi-save").addEventListener("click", function () {
    var ssid = $("#wifi-ssid").value.trim();
    var password = $("#wifi-password").value;
    if (!ssid) { toast("SSID required"); return; }
    var msg = "Changing Wi-Fi credentials will restart the device. Continue?";
    if (!window.confirm(msg)) return;
    var body = { wifiSsid: ssid };
    if (password) body.wifiPassword = password; // omit when blank — keep current password
    engine.saveConfig(body).then(function (res) {
      toast(res && res.ok ? "Network settings saved — device restarting" : "Failed to save network settings");
      $("#wifi-password").value = "";
    });
  });

  // Real pull-based update, split into two INDEPENDENT actions (firmware vs web
  // UI / filesystem — mirroring `pio run -t upload` vs `-t uploadfs`). One check
  // reveals what's available; each action runs its own confirm → install →
  // reboot. The device runs transfers asynchronously; the engine polls status.
  (function wireEuclidOta() {
    var btnCheck = $("#btn-ota");
    var btnApp = $("#btn-ota-app");
    var btnFs = $("#btn-ota-fs");
    if (!btnCheck) return;
    var busy = false;

    function label(btn, t) {
      var el = btn.querySelector(".mono-label") || btn;
      el.textContent = t;
    }
    function setBusy(on) {
      busy = on;
      btnCheck.disabled = on;
      if (btnApp) btnApp.disabled = on;
      if (btnFs) btnFs.disabled = on;
    }
    function show(btn, on) { if (btn) btn.style.display = on ? "" : "none"; }

    // Run one independent apply action.
    function runApply(btn, apply, name) {
      setBusy(true);
      label(btn, "INSTALLING…");
      apply(function (st) {
        if (st && st.percent != null) {
          label(btn, "INSTALLING " + (st.stage ? st.stage.toUpperCase() + " " : "") + (st.percent || 0) + "%");
        }
      }).then(function (final) {
        if (!final || final.phase === "rebooting") {
          label(btn, "REBOOTING…");
          toast(name + " updated — rebooting");
        } else {
          toast(name + " update failed" + (final.error ? ": " + final.error : ""));
          label(btn, btn === btnApp ? "UPDATE FIRMWARE" : "UPDATE WEB UI");
          setBusy(false);
        }
      });
    }

    btnCheck.addEventListener("click", function () {
      if (busy) return;
      setBusy(true);
      show(btnApp, false); show(btnFs, false);
      label(btnCheck, "CHECKING…");

      engine.checkFirmware().then(function (s) {
        setBusy(false);
        label(btnCheck, "CHECK FOR UPDATE");
        if (!s || s.phase === "error") {
          toast("Check failed" + (s && s.error ? ": " + s.error : "")); return;
        }
        if (!s.appAvailable && !s.fsAvailable) {
          toast("Up to date (v" + (s.current || "?") + ")"); return;
        }
        toast("Update available: v" + s.latest);
        if (s.appAvailable) show(btnApp, true);
        if (s.fsAvailable) show(btnFs, true);
      });
    });

    if (btnApp) btnApp.addEventListener("click", function () {
      if (busy) return;
      if (!window.confirm("Update the device FIRMWARE now?\n\nThe instrument will reboot and be " +
        "briefly offline. Firmware updates are A/B-protected — a failed download can't brick it. " +
        "Do NOT power it off during the update.")) return;
      runApply(btnApp, engine.updateFirmware, "Firmware");
    });

    if (btnFs) btnFs.addEventListener("click", function () {
      if (busy) return;
      if (!window.confirm("Update the WEB UI (filesystem) now?\n\nThe instrument will reboot and be " +
        "briefly offline. If interrupted, the UI may need re-flashing (recoverable). " +
        "Do NOT power it off during the update.")) return;
      runApply(btnFs, engine.updateWebUi, "Web UI");
    });
  })();

  // =========================================================================
  // Connection / demo indicator (folded into the header eyebrow line)
  // =========================================================================
  function renderConnectionState(){
    var el = $("#eyebrow-text");
    if (!el) return;
    if (engine.state.demo) {
      el.textContent = "Lumen Systems · Instrument No. 1 — DEMO";
    } else if (engine.state.connected) {
      el.textContent = "Lumen Systems · Instrument No. 1 — LINKED";
    } else {
      el.textContent = "Lumen Systems · Instrument No. 1 — RECONNECTING";
    }
  }

  // =========================================================================
  // MASTER RENDER
  // =========================================================================
  function render(){
    renderConnectionState();
    renderRuler();
    buildStripIfNeeded();
    renderSegmentMarks();
    renderPower();
    brightnessSlider.render();
    renderSegmentTabs();
    renderEffectRail();
    renderParams();
    renderNightlight();
    paintStrip();
    if (!$("#view-settings").hidden) reflectDeviceReadouts();
  }

  engine.on("change", render);
  engine.on("connection", renderConnectionState);
  engine.on("nightlight", renderNightlight);

  // =========================================================================
  // INIT
  // =========================================================================
  nightlightTargetSliderCtl = makeSlider({
    track: $("#nightlight-target-slider"), plane: $("#nightlight-target-plane"), marker: $("#nightlight-target-marker"),
    min: 0, max: 255,
    get: function () { return nightlightUiTarget; },
    set: function (v) { nightlightUiTarget = v; renderNightlightCurve(); },
    format: function (v) { $("#nightlight-target-value").textContent = v + " / 255"; },
    onCommit: reArmNightlightIfActive
  });
  renderDurationPicks();
  $("#nightlight-duration-value").textContent = nightlightUiDurationMin + " MIN";

  engine.start().then(function () {
    if (engine.state.demo) toast("Demo mode — no device found, using sample data");
    render();
  });
})();
