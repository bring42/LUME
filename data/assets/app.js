/* ==========================================================================
   LUME — Console × Byrne (LIVE, engine-bound)
   Same lighting-console structure/interactions as the console-euclid-live
   look (channel rack, drag fader, rotary knobs, live LED bar), rewired to
   the shared LumeEngine. This file never talks to the network directly —
   it renders `engine.state` and calls `engine.*` methods; the engine owns
   all device-API correctness (schema-driven params, 202-async writes, WS
   reconciliation, palette-as-top-level-int, hex colors, demo-mode fallback).
   ========================================================================== */

(() => {
"use strict";

const engine = window.LumeEngine.create();

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const hexToRgb = (hex) => {
  const m = String(hex || "#000000").replace("#", "").match(/.{1,2}/g);
  return m ? m.map((h) => parseInt(h, 16)) : [0, 0, 0];
};

/* ---------------------------------------------------------------------
   Palette swatch preview stops — the device only returns {id, name} for
   palettes (no color data), so this local table renders swatch previews
   for the well-known preset ids. Purely cosmetic; falls back to greys.
   -------------------------------------------------------------------- */
const PALETTE_STOPS = {
  0: ["#ff3b3b", "#ffb23b", "#f4ff3b", "#3bff6a", "#3bd0ff", "#5a3bff", "#ff3bd0"],
  1: ["#1a0500", "#5c0f00", "#b31a00", "#ff5100", "#ff9d00", "#ffd23b"],
  2: ["#001220", "#003355", "#0066a3", "#00a3cc", "#5be3e8", "#c8fff5"],
  3: ["#ff2e88", "#ff9d3d", "#f4ff3b", "#3bff8f", "#3bc8ff", "#a13bff"],
  4: ["#08210f", "#134a1e", "#2c7a2c", "#5ea83b", "#a6c94a", "#e0e88a"],
  5: ["#0d1420", "#243a56", "#5878a3", "#9db6d6", "#dfe9f7", "#ffffff"],
  6: ["#000000", "#3a0a00", "#8a1400", "#d94d00", "#ff9d00", "#fff2c8"],
  7: ["#2a1a5e", "#7b2a8a", "#d94a6a", "#ff8c42", "#ffd23b"], // Sunset
  8: ["#3a1a08", "#7a2e12", "#b5451f", "#d9812e", "#e8c24a"], // Autumn
  9: ["#1f6f6b", "#3aa89b", "#e8b13b", "#e2622e", "#f2e2c4"], // Retro
  10: ["#062a4a", "#0f6aa3", "#4fc3e8", "#b8ecff", "#ffffff"], // Ice
  11: ["#7a0f4a", "#c81f7a", "#ff5aa8", "#ff9dc8", "#ffd6ea"], // Pink
};
const FALLBACK_STOPS = ["#555555", "#999999"];
function paletteStops(id) { return PALETTE_STOPS[id] || FALLBACK_STOPS; }
function paletteCss(id) { return `linear-gradient(90deg, ${paletteStops(id).join(",")})`; }

const CAT_COLOR = { Solid: "#16130E55", Animated: "#D8492C", Moving: "#2E6DB4", Special: "#F2B233" };

/* ---------------------------------------------------------------------
   Nav / view transitions
   -------------------------------------------------------------------- */

function showView(name) {
  const main = $("#viewMain"), settings = $("#viewSettings");
  const navMain = $("#navMain"), navSettings = $("#navSettings");
  if (name === "main") {
    settings.classList.remove("active");
    main.classList.add("active");
    navMain.classList.add("active");
    navSettings.classList.remove("active");
  } else {
    main.classList.remove("active");
    settings.classList.add("active");
    navSettings.classList.add("active");
    navMain.classList.remove("active");
    loadSettingsView();
  }
}
$("#navMain").addEventListener("click", () => showView("main"));
$("#navSettings").addEventListener("click", () => showView("settings"));

/* ---------------------------------------------------------------------
   Toast
   -------------------------------------------------------------------- */

let toastTimer = null;
function showToast(msg) {
  const t = $("#toast");
  t.textContent = msg;
  t.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove("show"), 1800);
}
engine.on("toast", showToast);

/* ---------------------------------------------------------------------
   Link status chip
   -------------------------------------------------------------------- */

function renderLinkStatus() {
  const dot = $("#linkDot"), label = $("#linkLabel");
  const demo = engine.state.demo;
  const connected = engine.state.connected;
  dot.classList.toggle("demo", demo);
  dot.classList.toggle("off", !demo && !connected);
  label.textContent = demo ? "Demo" : (connected ? "Linked" : "Offline");
}

/* ---------------------------------------------------------------------
   Power
   -------------------------------------------------------------------- */

function renderPower() {
  const on = engine.state.controller.power;
  const btn = $("#powerBtn");
  btn.classList.toggle("on", on);
  btn.setAttribute("aria-checked", on ? "true" : "false");
  $("#powerState").textContent = on ? "On" : "Standby";
  $("#powerState").classList.toggle("live", on);
  $("#powerBtnState").textContent = on ? "ON" : "OFF";
  $("#vizStage").style.transition = `opacity var(--t-slow) var(--ease)`;
  $("#vizStage").style.opacity = on ? "1" : "0.35";
}
$("#powerBtn").addEventListener("click", () => {
  engine.setPower(!engine.state.controller.power);
  showToast(engine.state.controller.power ? "Power on" : "Standby");
});

/* ---------------------------------------------------------------------
   Master brightness fader (drag-based, horizontal)
   -------------------------------------------------------------------- */

const faderEl = $("#brightFader");
const faderCap = $("#brightCap");
const faderFill = $("#brightFill");

function faderPosFromValue(v) {
  const pct = v / 255;
  const w = faderEl.clientWidth;
  const pad = 9;
  return pad + pct * (w - pad * 2);
}

function renderFader(animate) {
  const v = engine.state.controller.brightness;
  const x = faderPosFromValue(v);
  faderCap.style.transition = animate ? `left var(--t-fast) var(--ease)` : "none";
  faderCap.style.left = x + "px";
  faderFill.style.width = x + "px";
  $("#brightReadout").innerHTML = v + "<small>/255</small>";
}

let faderDragging = false;
function setFaderFromClientX(clientX) {
  const rect = faderEl.getBoundingClientRect();
  const pad = 9;
  const pct = clamp((clientX - rect.left - pad) / (rect.width - pad * 2), 0, 1);
  engine.setBrightness(Math.round(pct * 255));
}
faderEl.addEventListener("pointerdown", (e) => {
  faderDragging = true;
  faderEl.classList.add("dragging");
  faderEl.setPointerCapture(e.pointerId);
  setFaderFromClientX(e.clientX);
});
faderEl.addEventListener("pointermove", (e) => {
  if (!faderDragging) return;
  setFaderFromClientX(e.clientX);
});
function endFaderDrag() {
  if (!faderDragging) return;
  faderDragging = false;
  faderEl.classList.remove("dragging");
}
faderEl.addEventListener("pointerup", endFaderDrag);
faderEl.addEventListener("pointercancel", endFaderDrag);
window.addEventListener("resize", () => renderFader(false));

/* ---------------------------------------------------------------------
   Nightlight
   -------------------------------------------------------------------- */

// Local UI-only fields (target brightness / duration in minutes) that get
// converted to engine.startNightlight(seconds, targetBrightness) on commit.
const nightUi = { target: 20, durationMin: 20 };

function renderNightlight() {
  const active = engine.state.nightlight.active;
  $("#nightSwitch").classList.toggle("on", active);
  $("#nightBody").classList.toggle("collapsed", !active);
  $("#nightTargetVal").textContent = nightUi.target;
  $("#nightDurationVal").textContent = nightUi.durationMin + "m";
  $("#nightEta").textContent = active
    ? `Fading to ${nightUi.target} over ${nightUi.durationMin} min…`
    : `Fades to ${nightUi.target} over ${nightUi.durationMin} min once armed.`;
}
$("#nightSwitch").addEventListener("click", () => {
  const active = engine.state.nightlight.active;
  if (active) {
    engine.stopNightlight();
    showToast("Nightlight disarmed");
  } else {
    engine.startNightlight(nightUi.durationMin * 60, nightUi.target);
    showToast("Nightlight armed");
  }
  renderNightlight();
});
function reArmNightlightIfActive() {
  if (engine.state.nightlight.active) {
    engine.startNightlight(nightUi.durationMin * 60, nightUi.target);
  }
}
$("#nightTarget").addEventListener("input", (e) => {
  nightUi.target = +e.target.value;
  renderNightlight();
});
$("#nightTarget").addEventListener("change", reArmNightlightIfActive);
$("#nightDuration").addEventListener("input", (e) => {
  nightUi.durationMin = +e.target.value;
  renderNightlight();
});
$("#nightDuration").addEventListener("change", reArmNightlightIfActive);

/* ---------------------------------------------------------------------
   Channel rack — segment tabs + add/delete
   -------------------------------------------------------------------- */

function segSwatchCss(seg) {
  const eff = engine.effectById(seg.effect);
  if (!eff) return "#555";
  if (eff.usesPalette) return paletteCss(seg._paletteIndex != null ? seg._paletteIndex : 0);
  const colorParams = eff.params.filter((p) => p.type === "color");
  if (colorParams.length >= 2) {
    return `linear-gradient(90deg, ${seg.params[colorParams[0].id]}, ${seg.params[colorParams[1].id]})`;
  }
  if (colorParams.length === 1) return seg.params[colorParams[0].id];
  return "#555";
}

function renderTabs() {
  const wrap = $("#segTabs");
  wrap.innerHTML = "";
  engine.state.segments.forEach((seg) => {
    const eff = engine.effectById(seg.effect);
    const tab = document.createElement("button");
    tab.className = "strip-tab" + (seg.id === engine.state.selectedId ? " active" : "");
    tab.type = "button";
    tab.innerHTML = `
      <div class="tab-id"><span>CH ${seg.id + 1}</span><span>${seg.length}px</span></div>
      <div class="tab-name">${eff ? eff.name : seg.effect}</div>
      <div class="tab-bar"><i style="background:${segSwatchCss(seg)}"></i></div>
    `;
    tab.addEventListener("click", () => engine.selectSegment(seg.id));
    wrap.appendChild(tab);
  });

  // Add-channel control — appended after the last segment, within ledCount.
  const addBtn = document.createElement("button");
  addBtn.className = "rack-add";
  addBtn.type = "button";
  addBtn.textContent = "+ Channel";
  addBtn.addEventListener("click", addChannel);
  wrap.appendChild(addBtn);

  $("#segCountChip").textContent = engine.state.segments.length + " CH";
}

function addChannel() {
  const segs = engine.state.segments;
  const ledCount = engine.state.controller.ledCount || 300;
  const lastEnd = segs.reduce((m, s) => Math.max(m, s.stop != null ? s.stop : (s.start + s.length - 1)), -1);
  const start = lastEnd + 1;
  if (start >= ledCount) {
    showToast("No room left — increase LED count in Settings");
    return;
  }
  const length = Math.max(1, Math.min(50, ledCount - start));
  engine.createSegment({ start: start, length: length, effect: "solid" });
  showToast("Channel added");
}

function deleteActiveChannel() {
  const seg = engine.selectedSegment();
  if (!seg) return;
  if (engine.state.segments.length <= 1) {
    showToast("At least one channel is required");
    return;
  }
  engine.deleteSegment(seg.id);
  showToast(`CH ${seg.id + 1} removed`);
}
$("#deleteChannelBtn").addEventListener("click", deleteActiveChannel);

/* ---------------------------------------------------------------------
   Visualizer — literal LED bar rendering an approximation of live color
   -------------------------------------------------------------------- */

let vizLedCount = -1;
let vizRaf = null;

function buildVizLeds(count) {
  const row = $("#vizLedRow");
  row.innerHTML = "";
  const frag = document.createDocumentFragment();
  for (let i = 0; i < count; i++) {
    const d = document.createElement("div");
    d.className = "viz-led";
    frag.appendChild(d);
  }
  row.appendChild(frag);
}

function dim(hex, factor) {
  const [r, g, b] = hexToRgb(hex);
  return `rgb(${Math.round(r * factor)},${Math.round(g * factor)},${Math.round(b * factor)})`;
}
function lerpHex(h1, h2, u) {
  const a = hexToRgb(h1), b = hexToRgb(h2);
  const r = Math.round(a[0] + (b[0] - a[0]) * u);
  const g = Math.round(a[1] + (b[1] - a[1]) * u);
  const bl = Math.round(a[2] + (b[2] - a[2]) * u);
  return `rgb(${r},${g},${bl})`;
}

// Approximate per-pixel coloring for the animated LED row. Not a faithful
// per-effect simulation — just a tasteful stand-in driven by whatever
// color/palette/speed-like params the selected segment actually has.
function colorForPixel(seg, eff, i, n, t) {
  const colorParams = eff.params.filter((p) => p.type === "color");
  const c0 = colorParams[0] ? seg.params[colorParams[0].id] : "#8a8a8a";
  const c1 = colorParams[1] ? seg.params[colorParams[1].id] : "#101010";
  const speedParam = eff.params.find((p) => p.id === "speed");
  const speedNorm = speedParam ? (seg.params.speed || 128) / 255 : 0.5;
  const usesPalette = eff.usesPalette;
  const sampleFromPalette = (u) => {
    const stops = paletteStops(seg._paletteIndex != null ? seg._paletteIndex : 0);
    const scaled = ((u % 1) + 1) % 1 * stops.length;
    return stops[Math.floor(scaled) % stops.length];
  };

  if (eff.id === "solid") return c0;
  if (usesPalette) {
    const u = (i / n + t * speedNorm * 0.4) % 1;
    return sampleFromPalette(u);
  }
  if (colorParams.length >= 2) {
    const u = i / n;
    return lerpHex(c0, c1, u);
  }
  if (colorParams.length === 1) {
    const b = (Math.sin(t * (1 + speedNorm * 3) + i * 0.2) * 0.5 + 0.5) * 0.7 + 0.3;
    return dim(c0, b);
  }
  const u = (i / n + t * speedNorm * 0.4) % 1;
  return lerpHex("#062534", "#7fe8e0", (Math.sin(u * 6.28) * 0.5 + 0.5));
}

function renderVisualizer() {
  const seg = engine.selectedSegment();
  if (!seg) return;
  const eff = engine.effectById(seg.effect);
  const name = eff ? eff.name : seg.effect;
  $("#vizLabel").textContent = `CH ${seg.id + 1} — ${name}`;
  const start = seg.start, end = seg.stop != null ? seg.stop : (seg.start + seg.length - 1);
  $("#vizPixels").textContent = `${start}–${end}`;
  $("#vizLength").textContent = seg.length;
  $("#rangeStart").textContent = start;
  $("#rangeLength").textContent = seg.length;
  $("#rangeEnd").textContent = end;

  const count = clamp(Math.round(seg.length / 1.4), 40, 140);
  if (count !== vizLedCount) {
    buildVizLeds(count);
    vizLedCount = count;
  }
  const glowColor = segSwatchCssSolid(seg, eff);
  $("#vizGlow").style.background = `radial-gradient(ellipse at 50% 50%, ${glowColor}, transparent 70%)`;
}

// A single representative solid color for glow purposes (first color param,
// else a mid-palette stop, else a neutral grey).
function segSwatchCssSolid(seg, eff) {
  if (!eff) return "#555";
  const colorParams = eff.params.filter((p) => p.type === "color");
  if (colorParams.length) return seg.params[colorParams[0].id];
  if (eff.usesPalette) return paletteStops(seg._paletteIndex != null ? seg._paletteIndex : 0)[2] || "#555";
  return "#555";
}

function vizTick(ts) {
  const seg = engine.selectedSegment();
  if (seg) {
    const eff = engine.effectById(seg.effect);
    const t = ts / 1000;
    const leds = $("#vizLedRow").children;
    const n = leds.length;
    const powered = engine.state.controller.power;
    const bright = engine.state.controller.brightness / 255;

    if (powered && n && eff) {
      for (let i = 0; i < n; i++) {
        const c = colorForPixel(seg, eff, i, n, t);
        leds[i].style.background = bright < 0.999 ? mixWithBlack(c, bright) : c;
      }
    } else if (n) {
      for (let i = 0; i < n; i++) leds[i].style.background = "#050505";
    }
    const fps = 42 + Math.round(Math.sin(ts / 900) * 3);
    $("#vizFps").textContent = fps;
  }
  vizRaf = requestAnimationFrame(vizTick);
}
function mixWithBlack(cssColor, factor) {
  let r, g, b;
  if (cssColor.startsWith("#")) {
    [r, g, b] = hexToRgb(cssColor);
  } else {
    const m = cssColor.match(/[\d.]+/g).map(Number);
    [r, g, b] = m;
  }
  return `rgb(${Math.round(r * factor)},${Math.round(g * factor)},${Math.round(b * factor)})`;
}

/* ---------------------------------------------------------------------
   Effect grid
   -------------------------------------------------------------------- */

function renderEffectGrid() {
  const grid = $("#effectGrid");
  grid.innerHTML = "";
  const seg = engine.selectedSegment();
  if (!seg) return;
  engine.state.effects.forEach((eff) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "effect-pill" + (eff.id === seg.effect ? " active" : "");
    btn.innerHTML = `<span class="effect-cat-dot" style="background:${CAT_COLOR[eff.category] || "#888"}"></span>${eff.name}`;
    btn.addEventListener("click", () => {
      engine.setEffect(seg.id, eff.id);
      showToast(`CH ${seg.id + 1} → ${eff.name}`);
    });
    grid.appendChild(btn);
  });
}

/* ---------------------------------------------------------------------
   Dynamic per-effect controls area (replaces the old hardcoded
   #blockColors / #blockPalette / #blockKnobs). Built fresh from
   eff.params each time the selected segment or its effect changes;
   individual value updates (knob drag, color change) mutate the DOM
   in-place without a full rebuild via the data-param-id hooks below.
   -------------------------------------------------------------------- */

const controlsWrap = $("#dynamicControls");
let controlsBuiltForKey = null; // `${segId}:${effectId}` — rebuild only when this changes

function controlsKeyFor(seg) { return seg ? (seg.id + ":" + seg.effect) : null; }

function renderControls(force) {
  const seg = engine.selectedSegment();
  if (!seg) { controlsWrap.innerHTML = ""; controlsBuiltForKey = null; return; }
  const eff = engine.effectById(seg.effect);
  const key = controlsKeyFor(seg);

  if (force || key !== controlsBuiltForKey) {
    buildControls(seg, eff);
    controlsBuiltForKey = key;
  } else {
    updateControlValues(seg, eff);
  }
}

function buildControls(seg, eff) {
  controlsWrap.innerHTML = "";
  if (!eff) return;

  const colorParams = [];
  const knobParams = [];
  const boolParams = [];
  const enumParams = [];
  let hasPalette = false;

  eff.params.forEach((p) => {
    if (p.type === "palette") { hasPalette = true; return; }
    if (p.type === "color") colorParams.push(p);
    else if (p.type === "int" || p.type === "float") knobParams.push(p);
    else if (p.type === "bool") boolParams.push(p);
    else if (p.type === "enum") enumParams.push(p);
  });

  if (colorParams.length) {
    const block = document.createElement("div");
    block.className = "field-block";
    block.innerHTML = `<span class="field-label">Color</span>`;
    const row = document.createElement("div");
    row.className = "color-row";
    colorParams.forEach((p) => row.appendChild(buildColorUnit(seg, p)));
    block.appendChild(row);
    controlsWrap.appendChild(block);
  }

  if (hasPalette) {
    const block = document.createElement("div");
    block.className = "field-block";
    block.innerHTML = `<span class="field-label">Palette</span>`;
    const row = document.createElement("div");
    row.className = "palette-row";
    row.id = "paletteRow";
    block.appendChild(row);
    controlsWrap.appendChild(block);
    renderPaletteRow(seg);
  }

  if (knobParams.length) {
    const block = document.createElement("div");
    block.className = "field-block";
    block.innerHTML = `<span class="field-label">Dynamics</span>`;
    const row = document.createElement("div");
    row.className = "knob-row";
    knobParams.forEach((p) => row.appendChild(buildKnobUnit(seg, p)));
    block.appendChild(row);
    controlsWrap.appendChild(block);
  }

  if (enumParams.length) {
    enumParams.forEach((p) => controlsWrap.appendChild(buildEnumUnit(seg, p)));
  }

  if (boolParams.length) {
    const block = document.createElement("div");
    block.className = "field-block";
    block.innerHTML = `<span class="field-label">Options</span>`;
    const row = document.createElement("div");
    row.className = "bool-row";
    boolParams.forEach((p) => row.appendChild(buildBoolUnit(seg, p)));
    block.appendChild(row);
    controlsWrap.appendChild(block);
  }
}

// Cheap in-place refresh (no rebuild) for reconciled values coming back
// from the device via WS — keeps drag interactions from being clobbered
// mid-gesture while still staying in sync at rest.
function updateControlValues(seg, eff) {
  if (!eff) return;
  eff.params.forEach((p) => {
    if (p.type === "palette") return;
    const val = seg.params[p.id];
    if (p.type === "color") {
      const btn = controlsWrap.querySelector(`.color-unit[data-param="${p.id}"]`);
      if (btn) {
        const hex = val || "#000000";
        btn.querySelector(".swatch-btn").style.background = hex;
        btn.querySelector(".color-unit-hex").textContent = hex.toUpperCase();
        const native = btn.querySelector(".color-native");
        if (native && document.activeElement !== native) native.value = hex;
      }
    } else if (p.type === "int" || p.type === "float") {
      const unit = controlsWrap.querySelector(`.knob-unit[data-param="${p.id}"]`);
      if (unit && !unit._dragging) unit._render && unit._render();
    } else if (p.type === "bool") {
      const sw = controlsWrap.querySelector(`.switch[data-param="${p.id}"]`);
      if (sw) sw.classList.toggle("on", !!val);
    } else if (p.type === "enum") {
      const seg2 = controlsWrap.querySelector(`.enum-selector[data-param="${p.id}"]`);
      if (seg2) {
        Array.from(seg2.children).forEach((btn, i) => btn.classList.toggle("active", i === val));
      }
    }
  });
  if (eff.usesPalette) renderPaletteRow(seg);
}

/* ---- color unit ---- */
function buildColorUnit(seg, p) {
  const unit = document.createElement("div");
  unit.className = "color-unit";
  unit.dataset.param = p.id;
  const hex = seg.params[p.id] || "#000000";
  unit.innerHTML = `
    <button class="swatch-btn" type="button" style="background:${hex}">
      <input type="color" class="color-native" value="${hex}" />
    </button>
    <div class="color-unit-text">
      <span class="color-unit-label">${p.name}</span>
      <span class="color-unit-hex">${hex.toUpperCase()}</span>
    </div>
  `;
  const native = unit.querySelector(".color-native");
  const swatchBtn = unit.querySelector(".swatch-btn");
  const hexLabel = unit.querySelector(".color-unit-hex");
  native.addEventListener("input", (e) => {
    swatchBtn.style.background = e.target.value;
    hexLabel.textContent = e.target.value.toUpperCase();
  });
  native.addEventListener("change", (e) => {
    engine.setParam(seg.id, p.id, e.target.value);
  });
  return unit;
}

/* ---- knob unit (int/float) ---- */
function buildKnobUnit(seg, p) {
  const unit = document.createElement("div");
  unit.className = "knob-unit";
  unit.dataset.param = p.id;
  const uid = "knob_" + p.id + "_" + seg.id;
  unit.innerHTML = `
    <div class="knob" id="${uid}">
      <svg class="knob-ring" width="72" height="72" viewBox="0 0 72 72">
        <circle cx="36" cy="36" r="32" fill="none" stroke="#000" stroke-opacity="0.4" stroke-width="3"/>
        <circle class="knob-fill" cx="36" cy="36" r="32" fill="none" stroke="#ff9d3d" stroke-width="3" stroke-linecap="round" stroke-dasharray="0 201.06" stroke-dashoffset="0" transform="rotate(135 36 36)"/>
      </svg>
      <div class="knob-indicator"></div>
    </div>
    <span class="knob-value"></span>
    <span class="knob-caption">${p.name}</span>
  `;
  const knobEl = unit.querySelector(".knob");
  const fillEl = unit.querySelector(".knob-fill");
  const indicatorEl = unit.querySelector(".knob-indicator");
  const valueEl = unit.querySelector(".knob-value");
  const min = p.min != null ? p.min : 0;
  const max = p.max != null ? p.max : 255;

  const CIRC = 2 * Math.PI * 32;
  const ANGLE_MIN = -135, ANGLE_MAX = 135;

  function currentVal() {
    const seg2 = engine.segmentById(seg.id);
    return seg2 ? (seg2.params[p.id] != null ? seg2.params[p.id] : p.default) : p.default;
  }
  function render(vOverride) {
    const v = vOverride != null ? vOverride : currentVal();
    const pct = clamp((v - min) / (max - min), 0, 1);
    const angle = ANGLE_MIN + pct * (ANGLE_MAX - ANGLE_MIN);
    indicatorEl.style.transform = `rotate(${angle}deg)`;
    // 270° gauge: draw one contiguous arc of length pct*0.75*CIRC, then an all-
    // consuming gap. (The old single-value dasharray+dashoffset trick wrapped the
    // circle and split the fill into two disjoint pieces — see the "off" knobs bug.)
    fillEl.style.strokeDasharray = `${pct * 0.75 * CIRC} ${CIRC}`;
    valueEl.textContent = (p.type === "float") ? Number(v).toFixed(2) : Math.round(v);
  }
  unit._render = render;

  let dragging = false, startY = 0, startVal = 0;
  knobEl.addEventListener("pointerdown", (e) => {
    dragging = true;
    unit._dragging = true;
    startY = e.clientY;
    startVal = currentVal();
    knobEl.setPointerCapture(e.pointerId);
    knobEl.style.cursor = "grabbing";
  });
  knobEl.addEventListener("pointermove", (e) => {
    if (!dragging) return;
    const dy = startY - e.clientY;
    const range = max - min;
    const sensitivity = range / 140;
    const v = clamp(startVal + dy * sensitivity, min, max);
    render(v);
    unit._pendingVal = v;
  });
  function endDrag() {
    if (!dragging) return;
    dragging = false;
    unit._dragging = false;
    knobEl.style.cursor = "grab";
    if (unit._pendingVal != null) {
      engine.setParam(seg.id, p.id, unit._pendingVal);
      unit._pendingVal = null;
    }
  }
  knobEl.addEventListener("pointerup", endDrag);
  knobEl.addEventListener("pointercancel", endDrag);

  let wheelCommitTimer = null;
  knobEl.addEventListener("wheel", (e) => {
    e.preventDefault();
    const range = max - min;
    const delta = (e.deltaY < 0 ? 1 : -1) * range * 0.02;
    const v = clamp(currentVal() + delta, min, max);
    render(v);
    unit._pendingVal = v;
    clearTimeout(wheelCommitTimer);
    wheelCommitTimer = setTimeout(() => {
      if (unit._pendingVal != null) {
        engine.setParam(seg.id, p.id, unit._pendingVal);
        unit._pendingVal = null;
      }
    }, 300);
  }, { passive: false });

  render();
  return unit;
}

/* ---- bool switch ---- */
function buildBoolUnit(seg, p) {
  const wrap = document.createElement("div");
  wrap.className = "toggle-field bool-unit";
  const on = !!seg.params[p.id];
  wrap.innerHTML = `
    <div class="toggle-field-text">
      <span class="toggle-field-title">${p.name}</span>
    </div>
    <button class="switch${on ? " on" : ""}" type="button" data-param="${p.id}" aria-label="Toggle ${p.name}"></button>
  `;
  const sw = wrap.querySelector(".switch");
  sw.addEventListener("click", () => {
    const next = !sw.classList.contains("on");
    sw.classList.toggle("on", next);
    engine.setParam(seg.id, p.id, next);
  });
  return wrap;
}

/* ---- enum segmented selector ---- */
function buildEnumUnit(seg, p) {
  const block = document.createElement("div");
  block.className = "field-block";
  const options = String(p.options || "").split("|");
  const current = seg.params[p.id] != null ? seg.params[p.id] : 0;
  block.innerHTML = `<span class="field-label">${p.name}</span>`;
  const selector = document.createElement("div");
  selector.className = "enum-selector";
  selector.dataset.param = p.id;
  options.forEach((label, i) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "enum-option" + (i === current ? " active" : "");
    btn.textContent = label;
    btn.addEventListener("click", () => {
      Array.from(selector.children).forEach((b, j) => b.classList.toggle("active", j === i));
      engine.setParam(seg.id, p.id, i);
    });
    selector.appendChild(btn);
  });
  block.appendChild(selector);
  return block;
}

/* ---- palette row ---- */
function renderPaletteRow(seg) {
  const wrap = $("#paletteRow");
  if (!wrap) return;
  wrap.innerHTML = "";
  engine.state.palettes.forEach((p) => {
    const chip = document.createElement("button");
    chip.type = "button";
    chip.className = "palette-chip" + (p.id === seg._paletteIndex ? " active" : "");
    chip.innerHTML = `
      <span class="swatch-strip">${paletteStops(p.id).map((c) => `<i style="background:${c}"></i>`).join("")}</span>
      <span class="p-name">${p.name}</span>
    `;
    chip.addEventListener("click", () => {
      engine.setPalette(seg.id, p.id);
      showToast(`Palette → ${p.name}`);
    });
    wrap.appendChild(chip);
  });
}

/* ---------------------------------------------------------------------
   AI Prompt
   -------------------------------------------------------------------- */

function runAiPrompt(promptText) {
  const text = String(promptText || "").trim();
  const statusEl = $("#aiStatus");
  const icon = $("#aiIcon");
  if (!text) return;

  icon.classList.add("thinking");
  statusEl.classList.remove("success");
  statusEl.textContent = "Interpreting prompt…";

  engine.sendPrompt(text).then((res) => {
    icon.classList.remove("thinking");
    if (res && res.ok) {
      statusEl.classList.add("success");
      statusEl.textContent = res.demo ? "Applied (demo mode)" : "Applied to the console";
    } else {
      statusEl.classList.remove("success");
      const reasonMsg = {
        rate_limited: "Rate limited — try again in a moment",
        busy: "Device is busy — try again shortly",
        bad_request: "Couldn't understand that prompt",
        error: "AI prompt failed",
        empty: "Type a prompt first",
      };
      const msg = (res && reasonMsg[res.reason]) || "AI prompt failed";
      statusEl.textContent = msg;
      showToast(msg);
    }
  });
}
$("#aiGo").addEventListener("click", () => runAiPrompt($("#aiInput").value));
$("#aiInput").addEventListener("keydown", (e) => {
  if (e.key === "Enter") runAiPrompt($("#aiInput").value);
});
$$(".ai-chip").forEach((chip) => {
  chip.addEventListener("click", () => {
    const p = chip.dataset.prompt;
    $("#aiInput").value = p;
    runAiPrompt(p);
  });
});

/* ---------------------------------------------------------------------
   Settings — populated from engine.getConfig() + state.status/info
   -------------------------------------------------------------------- */

function wireEyeToggle(btnId, inputId) {
  const btn = $(btnId);
  if (!btn) return;
  btn.addEventListener("click", () => {
    const input = $(inputId);
    input.type = input.type === "password" ? "text" : "password";
  });
}
wireEyeToggle("#wifiEye", "#wifiPass");
wireEyeToggle("#aiKeyEye", "#aiKey");

function formatUptime(seconds) {
  if (seconds == null) return null;
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (d > 0) return `${d}d ${h}h`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m`;
}

let settingsLoaded = false;
function loadSettingsView() {
  renderSettingsFromState();
  if (settingsLoaded) return;
  settingsLoaded = true;
  engine.getConfig().then(renderSettingsFromState);
}

function renderSettingsFromState() {
  const info = engine.state.info;
  const status = engine.state.status;
  const config = engine.state.config;

  if (info && info.firmware && info.firmware.version) {
    $("#dFirmware").textContent = "v" + info.firmware.version;
  }
  if (status) {
    if (status.uptime != null) {
      const t = formatUptime(status.uptime);
      if (t) $("#dUptime").textContent = t;
    }
    if (status.ip) $("#dIp").textContent = status.ip;
    if (status.wifi && status.wifi.ssid) $("#wifiSsid").value = status.wifi.ssid;
    if (status.wifi && status.wifi.rssi != null) {
      $("#dRssi").textContent = status.wifi.rssi + " dBm";
      const pct = clamp((status.wifi.rssi + 90) / 60, 0, 1);
      const lit = Math.max(1, Math.round(pct * 4));
      Array.from($("#dSignalBars").children).forEach((bar, i) => bar.classList.toggle("lit", i < lit));
    }
  }
  if (engine.state.controller.ledCount != null) {
    $("#ledCount").value = engine.state.controller.ledCount;
  }

  if (config) {
    if (config.aiApiKey) $("#aiKey").value = config.aiApiKey;
    if (config.mqttEnabled != null) {
      $("#mqttSwitch").classList.toggle("on", !!config.mqttEnabled);
      $("#mqttBody").classList.toggle("collapsed", !config.mqttEnabled);
      $("#mqttLed").classList.toggle("on", !!config.mqttEnabled);
    }
    if (config.mqttBroker) $("#mqttHost").value = config.mqttBroker;
    if (config.mqttPort) $("#mqttPort").value = config.mqttPort;
    if (config.sacnEnabled != null) {
      $("#sacnSwitch").classList.toggle("on", !!config.sacnEnabled);
      $("#sacnBody").classList.toggle("collapsed", !config.sacnEnabled);
      $("#sacnLed").classList.toggle("on", !!config.sacnEnabled);
    }
    if (config.sacnUniverse) $("#sacnUniverse").value = config.sacnUniverse;
  }
}

$("#ledCount").addEventListener("change", (e) => {
  const v = clamp(Math.round(+e.target.value) || 1, 1, 1000);
  e.target.value = v;
  engine.saveConfig({ ledCount: v }).then((res) => {
    showToast(res.ok ? "LED count saved" : "Failed to save LED count");
  });
});

// WiFi changes restart the device — require an explicit confirm, and never
// send a blank password (omit it to keep the current one).
$("#wifiSave").addEventListener("click", () => {
  const ssid = $("#wifiSsid").value.trim();
  const pass = $("#wifiPass").value;
  if (!ssid) { showToast("SSID cannot be empty"); return; }
  const proceed = window.confirm("Apply Wi-Fi settings and restart the device now?");
  if (!proceed) return;
  const body = { wifiSsid: ssid };
  if (pass) body.wifiPassword = pass;
  engine.saveConfig(body).then((res) => {
    showToast(res.ok ? "Wi-Fi settings applied — device restarting" : "Failed to save Wi-Fi settings");
  });
});

$("#mqttSwitch").addEventListener("click", () => {
  const next = !$("#mqttSwitch").classList.contains("on");
  $("#mqttSwitch").classList.toggle("on", next);
  $("#mqttBody").classList.toggle("collapsed", !next);
  $("#mqttLed").classList.toggle("on", next);
  engine.saveConfig({ mqttEnabled: next }).then((res) => {
    if (!res.ok) showToast("Failed to save MQTT setting");
  });
});
$("#mqttHost").addEventListener("change", (e) => {
  engine.saveConfig({ mqttBroker: e.target.value.trim() }).then((res) => {
    if (!res.ok) showToast("Failed to save MQTT broker");
  });
});
$("#mqttPort").addEventListener("change", (e) => {
  engine.saveConfig({ mqttPort: +e.target.value }).then((res) => {
    if (!res.ok) showToast("Failed to save MQTT port");
  });
});

$("#sacnSwitch").addEventListener("click", () => {
  const next = !$("#sacnSwitch").classList.contains("on");
  $("#sacnSwitch").classList.toggle("on", next);
  $("#sacnBody").classList.toggle("collapsed", !next);
  $("#sacnLed").classList.toggle("on", next);
  engine.saveConfig({ sacnEnabled: next }).then((res) => {
    if (!res.ok) showToast("Failed to save sACN setting");
  });
});
$("#sacnUniverse").addEventListener("change", (e) => {
  engine.saveConfig({ sacnUniverse: +e.target.value }).then((res) => {
    if (!res.ok) showToast("Failed to save sACN universe");
  });
});

// OTA: real pull-based update, with the firmware and the web UI (filesystem) as
// two INDEPENDENT actions (mirroring `pio run -t upload` vs `-t uploadfs`). One
// unified check reveals what's available; each action then runs its own
// confirm → progress → reboot. The device does the work asynchronously (engine
// polls /api/firmware/status); this just drives the buttons/status/progress.
(function wireOta() {
  const btn = $("#otaBtn");
  if (!btn) return;
  const statusEl = $("#otaStatus");
  const actions = $("#otaActions");
  const btnApp = $("#otaBtnApp");
  const btnFs = $("#otaBtnFs");
  const wrap = $("#otaProgressWrap");
  const fill = $("#otaProgressFill");
  let busy = false;

  function setProgress(pct) {
    if (wrap) wrap.style.display = pct == null ? "none" : "block";
    if (fill && pct != null) fill.style.width = pct + "%";
  }
  function showActions(show) { if (actions) actions.style.display = show ? "flex" : "none"; }
  function setBusy(on) {
    busy = on;
    btn.disabled = on; if (btnApp) btnApp.disabled = on; if (btnFs) btnFs.disabled = on;
  }

  // Run one independent apply action (app or fs). `apply` is the engine method.
  function runApply(kind, apply, label) {
    setBusy(true);
    if (statusEl) statusEl.textContent = "Installing " + label + "…";
    setProgress(0);
    apply((st) => {
      if (!st) return;
      if (st.percent != null) setProgress(st.percent);
      if (statusEl) statusEl.textContent = "Installing " + label +
        (st.stage ? " (" + st.stage + ")" : "") + "… " + (st.percent || 0) + "%";
    }).then((final) => {
      if (!final || final.phase === "rebooting") {
        if (statusEl) statusEl.textContent = label + " installed — device rebooting. Reload in ~30 s.";
        setProgress(100);
        showToast(label + " updated — rebooting");
      } else {
        if (statusEl) statusEl.textContent = label + " update failed" + (final.error ? ": " + final.error : "");
        setProgress(null); setBusy(false);
      }
    });
  }

  btn.addEventListener("click", () => {
    if (busy) return;
    setBusy(true);
    showActions(false);
    if (statusEl) statusEl.textContent = "Checking for updates…";
    setProgress(null);

    engine.checkFirmware().then((s) => {
      setBusy(false);
      if (!s || s.phase === "error") {
        if (statusEl) statusEl.textContent = "Check failed" + (s && s.error ? ": " + s.error : "");
        return;
      }
      if (!s.appAvailable && !s.fsAvailable) {
        if (statusEl) statusEl.textContent = "Up to date (v" + (s.current || "?") + "). Last checked just now.";
        return;
      }
      if (statusEl) statusEl.textContent = "Update available: v" + s.latest +
        (s.notes ? " — " + s.notes : "") + ". Choose what to install.";
      if (btnApp) btnApp.disabled = !s.appAvailable;
      if (btnFs) btnFs.disabled = !s.fsAvailable;
      showActions(true);
    });
  });

  if (btnApp) btnApp.addEventListener("click", () => {
    if (busy || btnApp.disabled) return;
    if (!window.confirm("Update the device FIRMWARE now?\n\nThe device will reboot and be " +
      "briefly offline. Firmware updates are A/B-protected — a failed download can't brick it. " +
      "Do NOT power it off during the update.")) return;
    runApply("app", engine.updateFirmware, "Firmware");
  });

  if (btnFs) btnFs.addEventListener("click", () => {
    if (busy || btnFs.disabled) return;
    if (!window.confirm("Update the WEB UI (filesystem) now?\n\nThe device will reboot and be " +
      "briefly offline. If interrupted, the UI may need re-flashing (recoverable). " +
      "Do NOT power it off during the update.")) return;
    runApply("fs", engine.updateWebUi, "Web UI");
  });
})();

/* ---------------------------------------------------------------------
   Master render
   -------------------------------------------------------------------- */

function renderLedTotal() {
  const el = $("#ledTotalLabel");
  if (el) el.textContent = `${engine.state.controller.ledCount} LEDs total`;
}

function renderAll() {
  renderLinkStatus();
  renderPower();
  renderFader(false);
  renderNightlight();
  renderTabs();
  renderVisualizer();
  renderEffectGrid();
  renderControls(false);
  renderLedTotal();
  if ($("#viewSettings").classList.contains("active")) renderSettingsFromState();
}

engine.on("change", renderAll);
engine.on("connection", renderLinkStatus);

/* ---------------------------------------------------------------------
   Bootstrap
   -------------------------------------------------------------------- */

requestAnimationFrame(() => {
  engine.start().then(() => {
    if (engine.state.demo) {
      showToast("Demo mode — no device found, using sample data");
    }
    renderAll();
    vizRaf = requestAnimationFrame(vizTick);
  });
});

})();
