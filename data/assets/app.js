/* ============================================================
   LUME — web UI ("Gallery")
   Vanilla, offline, no build step. Talks to the v2 API; mutations
   return 202 and are reconciled via the /ws state push + GET refetch.
   ============================================================ */

// ---- state ----
let ws = null;
let effectMetadata = {};   // id -> {name, params, hasSchema, ...}
let effectsList = [];      // ordered [{id, name}] for the rail
let activeSegmentId = 0;
let powerOn = true;

const PALETTE_PRESETS = { rainbow:0, lava:1, ocean:2, party:3, forest:4, cloud:5, heat:6 };

// ---- tiny helpers ----
function $(id) { return document.getElementById(id); }

function showToast(message, type = 'info') {
  const t = $('toast');
  t.textContent = message;
  t.className = 'toast show ' + type;
  clearTimeout(showToast._t);
  showToast._t = setTimeout(() => t.classList.remove('show'), 2800);
}

function hexToRgb(hex) {
  const m = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  return m ? [parseInt(m[1],16), parseInt(m[2],16), parseInt(m[3],16)] : [0,0,255];
}
function rgbToHex(r,g,b) { return '#' + [r,g,b].map(x => x.toString(16).padStart(2,'0')).join(''); }

async function api(endpoint, method = 'GET', body = null) {
  const opt = { method, headers: { 'Content-Type': 'application/json' } };
  if (body) opt.body = JSON.stringify(body);
  const r = await fetch('/api' + endpoint, opt);
  if (!r.ok) {
    try { const e = await r.json(); throw new Error(e.error || `HTTP ${r.status}`); }
    catch (e) { if (e.message && !e.message.startsWith('HTTP')) throw e; throw new Error(`HTTP ${r.status}`); }
  }
  return r.json();
}
async function apiV2(path, method = 'GET', body = null) {
  const opt = { method, headers: { 'Content-Type': 'application/json' } };
  if (body) opt.body = JSON.stringify(body);
  const r = await fetch('/api/v2' + path, opt);
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json();
}

// segment display names (client-side only; API has no name field)
function getSegmentName(id) { return (JSON.parse(localStorage.getItem('segmentNames') || '{}'))[id] || null; }
function setSegmentName(id, name) {
  const names = JSON.parse(localStorage.getItem('segmentNames') || '{}');
  if (name && name.trim()) names[id] = name.trim(); else delete names[id];
  localStorage.setItem('segmentNames', JSON.stringify(names));
}

// ============================================================
//  WebSocket — reconcile from the ~1s state push (optional)
// ============================================================
function connectWebSocket() {
  try {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(`${proto}://${location.host}/ws`);
    ws.onclose = () => setTimeout(connectWebSocket, 2000);
    ws.onmessage = (evt) => {
      try {
        const msg = JSON.parse(evt.data);
        if (msg.type !== 'state') return;
        if (msg.controller) applyControllerToUI(msg.controller);
        if (msg.segments) {
          const seg = msg.segments.find(s => s.id === activeSegmentId);
          if (seg) applySegmentToUI(seg);
        }
      } catch (e) { /* ignore malformed frames */ }
    };
  } catch (e) { /* WS optional */ }
}

function applyControllerToUI(c) {
  if (!c) return;
  setPower(c.power !== false, false);
  // brightness stays user-controlled; only reflect on explicit loads
}

function applySegmentToUI(seg) {
  if (!seg) return;
  $('effect').value = seg.effect || '';
  selectEffectTile(seg.effect);
  updateReadout();
  if (seg.params) writeParamValues(seg.params);
}

// write server param values into existing schema controls (no rebuild)
function writeParamValues(params) {
  Object.keys(params).forEach(pid => {
    const input = document.querySelector(`#param_${pid}`);
    if (!input) return;
    const v = params[pid];
    if (input.type === 'checkbox') input.checked = v;
    else if (input.type === 'color') input.value = typeof v === 'string' ? v : rgbToHex(v[0], v[1], v[2]);
    else if (input.type === 'hidden' && pid === 'palette') selectPaletteTile(pid, v);
    else {
      input.value = v;
      const disp = $(input.id + '_value');
      if (disp) disp.textContent = v;
      if (input.type === 'range') input.style.setProperty('--pct', pctOf(input) + '%');
    }
  });
}

// ============================================================
//  Effects — rail built from the API, schema-driven params
// ============================================================
async function loadEffectMetadata() {
  try {
    const data = await apiV2('/effects');
    (data.effects || []).forEach(e => {
      effectMetadata[e.id] = { name: e.name, params: e.params || [], hasSchema: !!(e.params && e.params.length) };
    });
    effectsList = (data.effects || []).map(e => ({ id: e.id, name: e.name }));
    buildEffectRail();
  } catch (e) { console.error('effects load failed', e); }
}

function buildEffectRail() {
  const rail = $('effectTiles');
  rail.innerHTML = '';
  effectsList.forEach(e => {
    const tile = document.createElement('button');
    tile.className = 'chip';
    tile.dataset.value = e.id;
    tile.type = 'button';
    tile.innerHTML = `<div class="swatch" style="background-image:${effectGradientCss(e.id)}"></div><div class="nm">${e.name}</div>`;
    tile.onclick = () => selectEffect(e.id);
    rail.appendChild(tile);
  });
}

function selectEffect(id) {
  $('effect').value = id;
  selectEffectTile(id);
  updateEffectControls(id);
  updateReadout();
  applySegmentState();
}

function selectEffectTile(id) {
  document.querySelectorAll('#effectTiles .chip').forEach(t => t.classList.toggle('sel', t.dataset.value === id));
}

// every effect exposes a schema -> render its controls
function updateEffectControls(id) {
  const meta = effectMetadata[id];
  const c = $('schemaControls');
  c.innerHTML = '';
  if (!meta || !meta.hasSchema) return;
  meta.params.forEach(p => { const el = createSchemaControl(p); if (el) c.appendChild(el); });
}

function pctOf(input) {
  const min = +input.min || 0, max = +input.max || 255;
  return Math.round(((+input.value - min) / (max - min)) * 100);
}

function createSchemaControl(param) {
  const wrap = document.createElement('div');
  wrap.className = 'ctl reveal';
  wrap.dataset.paramId = param.id;
  const pid = 'param_' + param.id;

  if (param.type === 'int') {
    const dflt = param.default ?? 128;
    wrap.innerHTML =
      `<div class="row"><label class="lbl" for="${pid}">${param.name}</label><span class="val" id="${pid}_value">${dflt}</span></div>`;
    const input = document.createElement('input');
    input.type = 'range'; input.id = pid; input.dataset.paramId = param.id;
    input.min = param.min ?? 0; input.max = param.max ?? 255; input.value = dflt;
    input.style.marginTop = '10px';
    input.style.setProperty('--pct', pctOf(input) + '%');
    input.addEventListener('input', () => {
      $(pid + '_value').textContent = input.value;
      input.style.setProperty('--pct', pctOf(input) + '%');
    });
    ['pointerup','touchend'].forEach(ev => input.addEventListener(ev, applySegmentState));
    input.addEventListener('keyup', applySegmentState);
    wrap.appendChild(input);
    return wrap;
  }

  if (param.type === 'float') {
    wrap.innerHTML = `<label class="ctl-label lbl" for="${pid}">${param.name}</label>`;
    const input = document.createElement('input');
    input.type = 'number'; input.id = pid; input.dataset.paramId = param.id;
    input.step = '0.01'; input.min = param.min ?? 0; input.max = param.max ?? 1; input.value = param.default ?? 0.5;
    input.addEventListener('change', applySegmentState);
    wrap.appendChild(input);
    return wrap;
  }

  if (param.type === 'color') {
    wrap.innerHTML =
      `<label class="ctl-label lbl">${param.name}</label>
       <div class="color-field"><span class="color-cap">${param.name}</span></div>`;
    const input = document.createElement('input');
    input.type = 'color'; input.id = pid; input.dataset.paramId = param.id;
    input.value = param.default || '#ff0000';
    input.addEventListener('input', updateReadout);
    input.addEventListener('change', applySegmentState);
    wrap.querySelector('.color-field').appendChild(input);
    return wrap;
  }

  if (param.type === 'bool') {
    wrap.classList.add('ctl-toggle');
    wrap.innerHTML = `<label class="lbl">${param.name}</label>`;
    const lab = document.createElement('label'); lab.className = 'toggle';
    const input = document.createElement('input');
    input.type = 'checkbox'; input.id = pid; input.dataset.paramId = param.id; input.checked = !!param.default;
    input.addEventListener('change', applySegmentState);
    const sl = document.createElement('span'); sl.className = 'toggle-slider';
    lab.appendChild(input); lab.appendChild(sl); wrap.appendChild(lab);
    return wrap;
  }

  if (param.type === 'enum') {
    wrap.innerHTML = `<label class="ctl-label lbl" for="${pid}">${param.name}</label>`;
    const sel = document.createElement('select');
    sel.id = pid; sel.dataset.paramId = param.id;
    (param.options || '').split('|').forEach((opt, i) => {
      const o = document.createElement('option'); o.value = i; o.textContent = opt; sel.appendChild(o);
    });
    sel.value = param.default ?? 0;
    sel.addEventListener('change', applySegmentState);
    wrap.appendChild(sel);
    return wrap;
  }

  if (param.type === 'palette') {
    wrap.innerHTML = `<label class="ctl-label lbl">${param.name}</label>`;
    const row = document.createElement('div'); row.className = 'pal-row'; row.id = 'paletteTiles_' + param.id;
    const hidden = document.createElement('input');
    hidden.type = 'hidden'; hidden.id = pid; hidden.dataset.paramId = param.id;
    hidden.value = param.default || 'rainbow';
    Object.keys(PALETTE_PRESETS).forEach(name => {
      const tile = document.createElement('div');
      tile.className = 'pal-tile' + (name === hidden.value ? ' sel' : '');
      tile.dataset.value = name;
      tile.innerHTML = `<div class="pal-swatch" style="background:${paletteGradientCss(name)}"></div><div class="pal-nm">${name[0].toUpperCase()+name.slice(1)}</div>`;
      tile.onclick = () => {
        hidden.value = name;
        row.querySelectorAll('.pal-tile').forEach(t => t.classList.toggle('sel', t === tile));
        applySegmentState();
      };
      row.appendChild(tile);
    });
    wrap.appendChild(row); wrap.appendChild(hidden);
    return wrap;
  }

  return null;
}

function selectPaletteTile(pid, value) {
  const hidden = $('param_' + pid); if (hidden) hidden.value = value;
  const row = $('paletteTiles_' + pid);
  if (row) row.querySelectorAll('.pal-tile').forEach(t => t.classList.toggle('sel', t.dataset.value === value));
}

// ============================================================
//  Apply state (fire-and-forget; reconcile via WS/GET)
// ============================================================
async function applyControllerState() {
  try {
    await apiV2('/controller', 'PUT', { power: powerOn, brightness: parseInt($('brightness').value) });
  } catch (e) { console.error('controller apply failed', e); }
}

async function applySegmentState() {
  const effectId = $('effect').value;
  if (!effectId) return;
  const segment = { effect: effectId };
  const meta = effectMetadata[effectId];
  if (meta && meta.hasSchema) {
    const params = {};
    let palette = null;
    document.querySelectorAll('#schemaControls > .ctl').forEach(group => {
      const pid = group.dataset.paramId;
      let input = group.querySelector('input') || group.querySelector('select');
      if (!input) return;
      let value;
      if (input.type === 'checkbox') value = input.checked;
      else if (input.type === 'number') value = parseFloat(input.value);
      else if (input.type === 'range') value = parseInt(input.value);
      else if (input.type === 'color') value = input.value;
      else if (input.tagName === 'SELECT') value = parseInt(input.value);
      else value = input.value;
      if (pid === 'palette') palette = PALETTE_PRESETS[input.value] ?? 0;
      else params[pid] = value;
    });
    if (Object.keys(params).length) segment.params = params;
    if (palette !== null) segment.palette = palette;
  }
  try {
    if (activeSegmentId >= 0) { await apiV2(`/segments/${activeSegmentId}`, 'PUT', segment); showToast('Applied', 'success'); }
    else showToast('No segment selected', 'error');
  } catch (e) { showToast('Could not apply', 'error'); }
  updateReadout();
}

// ============================================================
//  Segments
// ============================================================
async function loadSegments() {
  try {
    const data = await apiV2('/segments');
    const sel = $('segmentSelector');
    if (data.segments && data.segments.length) {
      sel.innerHTML = data.segments.map(s => {
        const nm = getSegmentName(s.id);
        const label = nm ? `${nm} · LEDs ${s.start}–${s.stop}` : `Segment ${s.id} · LEDs ${s.start}–${s.stop}`;
        return `<option value="${s.id}">${label}</option>`;
      }).join('');
      activeSegmentId = data.segments[0].id;
      sel.value = activeSegmentId;
      await loadSegmentState(activeSegmentId);
    } else {
      sel.innerHTML = '<option value="-1">No segments — add one in Settings</option>';
      activeSegmentId = -1;
    }
  } catch (e) { console.error('segments load failed', e); }
}

async function switchSegment(id) { activeSegmentId = id; await loadSegmentState(id); }

async function loadSegmentState(id) {
  try {
    const seg = await apiV2(`/segments/${id}`);
    $('effect').value = seg.effect || '';
    selectEffectTile(seg.effect);
    updateEffectControls(seg.effect);
    if (seg.params) writeParamValues(seg.params);
    updateReadout();
  } catch (e) { console.error('segment state load failed', e); }
}

async function loadLedState() {
  try {
    const state = await apiV2('/segments');
    setPower(state.power !== false, false);
    $('brightness').value = state.brightness ?? 128;
    $('brightnessValue').textContent = state.brightness ?? 128;
    $('brightness').style.setProperty('--pct', pctOf($('brightness')) + '%');
    await loadSegments();
  } catch (e) { console.error('led state load failed', e); }
}

// ============================================================
//  Power + brightness
// ============================================================
function setPower(on, push) {
  powerOn = on;
  const sw = $('powerSwitch');
  sw.classList.toggle('on', on);
  sw.setAttribute('aria-checked', on ? 'true' : 'false');
  $('device').classList.toggle('off', !on);
  updateReadout();
  if (push) applyControllerState();
}

function togglePower() { setPower(!powerOn, true); }

$('powerSwitch').addEventListener('click', togglePower);
$('powerSwitch').addEventListener('keydown', e => {
  if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); togglePower(); }
});

// brightness slider — reflect while dragging, apply on release
(function bindBrightness() {
  const input = $('brightness'), disp = $('brightnessValue');
  input.style.setProperty('--pct', pctOf(input) + '%');
  input.addEventListener('input', () => {
    disp.textContent = input.value;
    input.style.setProperty('--pct', pctOf(input) + '%');
    updateReadout();
  });
  ['pointerup','touchend'].forEach(ev => input.addEventListener(ev, applyControllerState));
  input.addEventListener('keyup', applyControllerState);
})();

function updateReadout() {
  const id = $('effect').value;
  const name = effectMetadata[id]?.name || (id ? id : '—');
  const pct = Math.round((parseInt($('brightness').value) / 255) * 100);
  $('readoutName').textContent = powerOn ? `${name} · ${pct}%` : 'Standby';
}

// ============================================================
//  Status
// ============================================================
async function loadStatus() {
  try {
    const s = await api('/status');
    $('wifiDot').className = 'dot';
    $('wifiStatus').textContent = s.wifi || 'Connected';
    $('ipAddress').textContent = s.ip || '—';
    const up = s.uptime || 0;
    $('uptime').textContent = `${Math.floor(up/3600)}h ${Math.floor((up%3600)/60)}m`;

    const sacn = s.sacn || {}, sd = $('sacnDot'), st = $('sacnStatusText');
    if (!sacn.enabled) { sd.className = 'dot offline'; st.textContent = 'Not enabled'; }
    else if (sacn.receiving) { sd.className = 'dot'; st.textContent = `Receiving · universe ${sacn.universe}`; }
    else { sd.className = 'dot loading'; st.textContent = `Waiting for data · universe ${sacn.universe}`; }

    const mqtt = s.mqtt || {}, md = $('mqttDot'), mt = $('mqttStatusText');
    if (!mqtt.enabled) { md.className = 'dot offline'; mt.textContent = 'Not enabled'; }
    else if (mqtt.connected) { md.className = 'dot'; mt.textContent = `Connected · ${mqtt.broker}`; }
    else { md.className = 'dot loading'; mt.textContent = 'Connecting…'; }
  } catch (e) {
    $('wifiDot').className = 'dot offline';
    $('wifiStatus').textContent = 'Offline';
  }
}

// ============================================================
//  Nightlight
// ============================================================
let nightlightPoll = null;
let updatingNightlightUI = false;

function toggleNightlightSection() {
  const c = $('nightlightControls');
  const open = !c.classList.contains('open');
  c.classList.toggle('open', open);
  if (open) c.scrollIntoView({ behavior: 'smooth', block: 'center' });
}

async function loadNightlightStatus() {
  try { updateNightlightUI(await api('/nightlight')); } catch (e) { /* ignore */ }
}

function updateNightlightUI(status) {
  const active = status.active;
  updatingNightlightUI = true; $('nightlightToggle').checked = active; updatingNightlightUI = false;
  $('startNightlightBtn').style.display = active ? 'none' : '';
  $('stopNightlightBtn').style.display = active ? '' : 'none';
  $('nightlightProgress').style.display = active ? '' : 'none';
  $('nightlightControls').classList.toggle('open', active);
  if (active) {
    const p = Math.round((status.progress || 0) * 100);
    $('nightlightProgressBar').style.width = p + '%';
    $('nightlightProgressText').textContent = p + '%';
    if (!nightlightPoll) nightlightPoll = setInterval(loadNightlightStatus, 2000);
  } else if (nightlightPoll) { clearInterval(nightlightPoll); nightlightPoll = null; }
}

async function startNightlight() {
  try {
    await api('/nightlight', 'POST', {
      duration: parseInt($('nightlightDuration').value) * 60,
      targetBrightness: parseInt($('nightlightTarget').value)
    });
    showToast('Nightlight started', 'success');
    setTimeout(loadNightlightStatus, 200);
  } catch (e) {
    showToast('Could not start nightlight', 'error');
    updatingNightlightUI = true; $('nightlightToggle').checked = false; updatingNightlightUI = false;
  }
}

async function stopNightlight() {
  try {
    await api('/nightlight/stop', 'POST', {});
    showToast('Nightlight stopped', 'success');
    if (nightlightPoll) { clearInterval(nightlightPoll); nightlightPoll = null; }
    updatingNightlightUI = true; $('nightlightToggle').checked = false; updatingNightlightUI = false;
    $('startNightlightBtn').style.display = '';
    $('stopNightlightBtn').style.display = 'none';
    $('nightlightProgress').style.display = 'none';
  } catch (e) { showToast('Could not stop nightlight', 'error'); }
}

$('nightlightToggle').addEventListener('change', async function () {
  if (updatingNightlightUI) return;
  if (this.checked) $('nightlightControls').classList.add('open');
  else await stopNightlight();
});
$('nightlightDuration').addEventListener('input', function () { $('nightlightDurationValue').textContent = this.value + ' min'; this.style.setProperty('--pct', pctOf(this) + '%'); });
$('nightlightTarget').addEventListener('input', function () { const v = +this.value; $('nightlightTargetValue').textContent = v === 0 ? 'Off' : v; this.style.setProperty('--pct', pctOf(this) + '%'); });

// ============================================================
//  Settings modal
// ============================================================
function openConfigModal() { $('configModal').classList.add('show'); loadSegmentsConfig(); }
function closeConfigModal() { $('configModal').classList.remove('show'); }
document.addEventListener('click', e => { if (e.target === $('configModal')) closeConfigModal(); });

function toggleSettings(id, show) { $(id).classList.toggle('open', show); }

async function loadConfig() {
  try {
    const c = await api('/config');
    $('wifiSSID').value = c.wifiSSID || '';
    $('ledCount').value = c.ledCount || 160;
    $('aiApiKey').value = '';
    $('aiModel').value = c.aiModel || 'claude-3-5-haiku-20241022';
    const sacn = c.sacnEnabled || false;
    $('sacnEnabled').checked = sacn;
    $('sacnUniverse').value = c.sacnUniverse || 1;
    $('sacnStartChannel').value = c.sacnStartChannel || 1;
    toggleSettings('sacnSettings', sacn);
    const mqtt = c.mqttEnabled || false;
    $('mqttEnabled').checked = mqtt;
    $('mqttBroker').value = c.mqttBroker || '';
    $('mqttPort').value = c.mqttPort || 1883;
    $('mqttUsername').value = (c.mqttUsername && c.mqttUsername !== '****') ? c.mqttUsername : '';
    $('mqttPassword').value = '';
    $('mqttTopicPrefix').value = c.mqttTopicPrefix || 'lume';
    toggleSettings('mqttSettings', mqtt);
  } catch (e) { console.error('config load failed', e); }
}

async function saveConfig() {
  const config = {
    wifiSSID: $('wifiSSID').value, wifiPassword: $('wifiPassword').value,
    ledCount: parseInt($('ledCount').value),
    aiApiKey: $('aiApiKey').value, aiModel: $('aiModel').value,
    sacnEnabled: $('sacnEnabled').checked, sacnUniverse: parseInt($('sacnUniverse').value), sacnStartChannel: parseInt($('sacnStartChannel').value),
    mqttEnabled: $('mqttEnabled').checked, mqttBroker: $('mqttBroker').value, mqttPort: parseInt($('mqttPort').value),
    mqttUsername: $('mqttUsername').value, mqttPassword: $('mqttPassword').value, mqttTopicPrefix: $('mqttTopicPrefix').value
  };
  if (config.wifiPassword === '') delete config.wifiPassword;
  if (config.mqttPassword === '') delete config.mqttPassword;
  if (config.aiApiKey === '') delete config.aiApiKey;
  try { await api('/config', 'POST', config); showToast('Settings saved', 'success'); loadConfig(); }
  catch (e) { showToast('Could not save settings', 'error'); }
}

async function loadSegmentsConfig() {
  try {
    const data = await apiV2('/segments');
    const box = $('segmentsList');
    if (!data.segments || !data.segments.length) { box.innerHTML = '<p style="color:var(--ink-3);font-size:13px">No segments configured</p>'; return; }
    box.innerHTML = data.segments.map(s => {
      const nm = getSegmentName(s.id) || `Segment ${s.id}`;
      return `<div class="seg-item">
        <div class="seg-info">
          <div class="seg-name">${nm}</div>
          <div class="seg-meta">LEDs ${s.start}–${s.stop} · ${s.length} LEDs${s.effect ? ' · ' + s.effect : ''}</div>
        </div>
        <button class="btn btn-outline btn-sm" onclick="editSegmentName(${s.id})">Rename</button>
        ${s.length > 1 ? `<button class="btn btn-outline btn-sm" onclick="splitSegment(${s.id}, ${s.start}, ${s.length})">Split</button>` : ''}
        <button class="btn btn-outline btn-sm" onclick="deleteSegmentConfig(${s.id})">Delete</button>
      </div>`;
    }).join('');
  } catch (e) { console.error('segments config load failed', e); }
}

async function editSegmentName(id) {
  const cur = getSegmentName(id) || '';
  const nm = prompt(`Name for segment ${id}:`, cur);
  if (nm !== null) { setSegmentName(id, nm); await loadSegmentsConfig(); await loadSegments(); }
}

async function splitSegment(id, start, length) {
  const at = prompt(`Split segment ${id} at LED position? (${start + 1}–${start + length - 1})`);
  if (!at) return;
  const pos = parseInt(at);
  if (isNaN(pos) || pos <= start || pos >= start + length) { showToast('Invalid split position', 'error'); return; }
  try {
    await fetch('/api/v2/segments/' + id, { method: 'DELETE' });
    await apiV2('/segments', 'POST', { start, length: pos - start });
    await apiV2('/segments', 'POST', { start: pos, length: length - (pos - start) });
    showToast('Segment split', 'success'); loadSegmentsConfig();
  } catch (e) { showToast('Could not split segment', 'error'); }
}

async function addSegment() {
  const start = parseInt($('newSegmentStart').value), length = parseInt($('newSegmentLength').value);
  if (isNaN(start) || isNaN(length) || start < 0 || length < 1) { showToast('Invalid segment range', 'error'); return; }
  try {
    await apiV2('/segments', 'POST', { start, length });
    showToast('Segment created', 'success'); loadSegmentsConfig();
    $('newSegmentStart').value = start + length;
  } catch (e) { showToast('Could not create segment', 'error'); }
}

async function deleteSegmentConfig(id) {
  if (!confirm(`Delete segment ${id}?`)) return;
  try { await fetch('/api/v2/segments/' + id, { method: 'DELETE' }); showToast('Segment deleted', 'success'); loadSegmentsConfig(); }
  catch (e) { showToast('Could not delete segment', 'error'); }
}

$('sacnEnabled').addEventListener('change', function () { toggleSettings('sacnSettings', this.checked); });
$('mqttEnabled').addEventListener('change', function () { toggleSettings('mqttSettings', this.checked); });

// ============================================================
//  AI prompt
// ============================================================
async function sendAIPrompt() {
  const prompt = $('aiPrompt').value.trim();
  if (!prompt) { showToast('Enter a prompt first', 'error'); return; }
  const box = $('aiStatus'), txt = $('aiStatusText');
  box.classList.add('show'); txt.textContent = 'Working…';
  try {
    const r = await api('/prompt', 'POST', { prompt });
    if (r.success) {
      showToast('Lights updated', 'success');
      txt.textContent = r.message || 'Applied.';
      setTimeout(() => box.classList.remove('show'), 3000);
      await loadLedState();
    } else { showToast(r.error || 'Could not process prompt', 'error'); box.classList.remove('show'); }
  } catch (e) {
    showToast('Error: ' + (e.message || 'network'), 'error');
    txt.textContent = 'Error: ' + (e.message || 'network');
    setTimeout(() => box.classList.remove('show'), 5000);
  }
}
$('aiPrompt').addEventListener('keydown', e => { if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendAIPrompt(); } });

// ============================================================
//  Live strip render — an approximation of the lit surface.
//  (Exact per-pixel render is the future WASM emulator.)
// ============================================================
const GRADS = {
  amber:   [[0,[58,21,0]],[.5,[231,165,82]],[1,[255,201,126]]],
  ember:   [[0,[40,10,0]],[.5,[255,120,30]],[1,[255,190,90]]],
  fire:    [[0,[8,2,0]],[.35,[190,40,0]],[.7,[255,140,10]],[1,[255,230,140]]],
  spectrum:[[0,[230,20,60]],[.25,[255,180,0]],[.5,[40,200,90]],[.75,[0,140,230]],[1,[120,60,220]]],
  ocean:   [[0,[3,10,40]],[.5,[0,120,180]],[.8,[0,190,200]],[1,[150,240,230]]],
  forest:  [[0,[11,60,20]],[.5,[34,139,34]],[1,[144,238,144]]],
  ice:     [[0,[30,58,95]],[.5,[70,130,180]],[1,[240,248,255]]],
  magenta: [[0,[60,0,40]],[.5,[220,20,120]],[1,[255,120,200]]],
  white:   [[0,[70,70,70]],[.5,[220,220,220]],[1,[255,255,255]]],
  red:     [[0,[40,0,0]],[.5,[200,20,20]],[1,[255,120,90]]],
  blueRed: [[0,[0,0,180]],[1,[200,0,0]]],
};
// effect id -> [gradient, motion]
const EFF = {
  solid:['amber','static'], gradient:['blueRed','static'], rainbow:['spectrum','hue'],
  fire:['fire','flicker'], fireup:['fire','flicker'], colorwaves:['spectrum','scroll'],
  wave:['ocean','scroll'], theater:['white','scroll'], sparkle:['white','sparkle'],
  pulse:['magenta','breathe'], breathe:['ocean','breathe'], noise:['forest','scroll'],
  meteor:['white','comet'], comet:['amber','comet'], rain:['ocean','sparkle'],
  twinkle:['ice','sparkle'], strobe:['white','blink'], sinelon:['spectrum','scroll'],
  scanner:['red','comet'], candle:['ember','flicker'], pride:['spectrum','hue'],
  pacifica:['ocean','scroll'], confetti:['spectrum','sparkle'],
};
function sampleGrad(stops, x) {
  x = Math.max(0, Math.min(1, x));
  for (let i = 0; i < stops.length - 1; i++) {
    if (x >= stops[i][0] && x <= stops[i+1][0]) {
      const t = (x - stops[i][0]) / (stops[i+1][0] - stops[i][0]);
      const a = stops[i][1], b = stops[i+1][1];
      return [a[0]+(b[0]-a[0])*t, a[1]+(b[1]-a[1])*t, a[2]+(b[2]-a[2])*t];
    }
  }
  return stops[stops.length-1][1];
}
function hsv(h,s,v){let i=Math.floor(h*6),f=h*6-i,p=v*(1-s),q=v*(1-f*s),t=v*(1-(1-f)*s),r,g,b;
  switch(i%6){case 0:r=v;g=t;b=p;break;case 1:r=q;g=v;b=p;break;case 2:r=p;g=v;b=t;break;case 3:r=p;g=q;b=v;break;case 4:r=t;g=p;b=v;break;default:r=v;g=p;b=q;}
  return [r*255,g*255,b*255];}
function effectGradientCss(id) {
  const g = GRADS[(EFF[id] || ['amber'])[0]];
  return 'linear-gradient(90deg,' + g.map(s => `rgb(${s[1].map(Math.round).join(',')}) ${Math.round(s[0]*100)}%`).join(',') + ')';
}
function paletteGradientCss(name) {
  const map = { rainbow:'spectrum', lava:'fire', ocean:'ocean', party:'magenta', forest:'forest', cloud:'ice', heat:'ember' };
  const g = GRADS[map[name] || 'spectrum'];
  return 'linear-gradient(90deg,' + g.map(s => `rgb(${s[1].map(Math.round).join(',')}) ${Math.round(s[0]*100)}%`).join(',') + ')';
}

(function stripLoop() {
  const cv = $('glow'), ctx = cv.getContext('2d');
  const N = 100, W = cv.width, H = cv.height;
  let level = 1, t0 = null;
  function noise(i, t) { return (Math.sin(i*0.35+t*4)+Math.sin(i*0.13-t*2.7))*0.25+0.5; }
  function frame(ts) {
    if (t0 === null) t0 = ts;
    const t = (ts - t0) / 1000;
    level += ((powerOn ? 1 : 0) - level) * 0.08;
    cv.style.opacity = level.toFixed(3);
    const id = $('effect').value;
    const [gradName, motion] = EFF[id] || ['amber', 'static'];
    const stops = GRADS[gradName];
    const bright = parseInt($('brightness').value || 255) / 255;
    const head = ((t * 0.45) % 1) * N;
    const grad = ctx.createLinearGradient(0, 0, W, 0);
    for (let i = 0; i <= N; i++) {
      const x = i / N; let c, m = 1;
      switch (motion) {
        case 'hue':     c = hsv((x + t*0.05) % 1, 0.85, 1); break;
        case 'scroll':  c = sampleGrad(stops, (x + t*0.09) % 1); break;
        case 'flicker': c = sampleGrad(stops, noise(i, t)); break;
        case 'breathe': c = sampleGrad(stops, x); m = (Math.sin(t*1.3)+1)/2*0.8+0.2; break;
        case 'blink':   c = sampleGrad(stops, x); m = (Math.sin(t*11) > 0 ? 1 : 0.05); break;
        case 'comet':   { c = sampleGrad(stops, 0.9); let d = i - head; if (d > 0) d -= N; m = Math.max(0, 1 + d/20); m *= m; break; }
        case 'sparkle': { c = sampleGrad(stops, x); const s = (Math.sin(i*97.13 + Math.floor(t*6)*13.7) * 43758.5) % 1; m = (Math.abs(s) > 0.86 ? 1 : 0.12); break; }
        default:        c = sampleGrad(stops, x);
      }
      m *= bright;
      grad.addColorStop(x, `rgb(${Math.round(c[0]*m)},${Math.round(c[1]*m)},${Math.round(c[2]*m)})`);
    }
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = grad; ctx.fillRect(0, 0, W, H);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
})();

// ============================================================
//  Init
// ============================================================
async function initialize() {
  // paint slider fills that start from static markup values
  ['brightness', 'nightlightDuration', 'nightlightTarget'].forEach(id => {
    const el = $(id); el.style.setProperty('--pct', pctOf(el) + '%');
  });
  loadStatus();
  await loadEffectMetadata();
  await loadLedState();
  loadConfig();
  loadNightlightStatus();
  setInterval(loadStatus, 10000);
}
initialize();
connectWebSocket();
