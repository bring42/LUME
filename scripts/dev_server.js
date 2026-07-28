/* LUME UI dev server — a mock device for previewing the web UI without hardware.
 *
 *   node scripts/dev_server.js         # then open http://localhost:8791
 *
 * Serves data/ (the web UI source of truth) and mocks the firmware API
 * faithfully enough to develop against: async 202 writes, the /ws state
 * push, whole-`params` semantics, palette-as-top-level-int, hex colors. The
 * effect catalogue is pulled straight from data/assets/engine.js so it
 * matches what the UI expects. This is a dev tool only — the real device is the
 * ESP32 firmware.
 */
const http = require("http");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

const ROOT = path.join(__dirname, "..");
const DATA = path.join(ROOT, "data");
const PORT = process.env.PORT ? Number(process.env.PORT) : 8791;

global.window = { location: {} };
require(path.join(ROOT, "data/assets/engine.js"));
const EFFECTS = window.LumeEngine.FALLBACK_EFFECTS;
const PALETTES = window.LumeEngine.FALLBACK_PALETTES;
const effById = (id) => EFFECTS.find((e) => e.id === id);

function defaultParams(effId, overlay) {
  const e = effById(effId), out = {};
  if (e) for (const p of e.params) { if (p.type === "palette") continue; out[p.id] = p.type === "bool" ? !!p.default : p.default; }
  if (overlay) for (const k of Object.keys(overlay)) if (k in out) out[k] = overlay[k];
  return out;
}

const dev = {
  controller: { power: true, brightness: 180, ledCount: 300 },
  segments: [
    { id: 0, start: 0, length: 100, reverse: false, brightness: 255, effect: "fire", params: defaultParams("fire"), paletteIndex: 6 },
    { id: 1, start: 100, length: 120, reverse: false, brightness: 255, effect: "rainbow", params: defaultParams("rainbow"), paletteIndex: 0 },
    { id: 2, start: 220, length: 80, reverse: false, brightness: 220, effect: "twinkle", params: defaultParams("twinkle"), paletteIndex: 5 },
  ],
  nextId: 3,
  nightlight: { active: false, progress: 0 },
};

const serializeSeg = (s) => ({ id: s.id, start: s.start, stop: s.start + s.length - 1, length: s.length, reverse: s.reverse, brightness: s.brightness, effect: s.effect, params: { ...s.params } });
const stateMsg = () => ({ type: "state", controller: { ...dev.controller }, segments: dev.segments.map(serializeSeg) });

const clients = new Set();
function wsFrame(str) {
  const payload = Buffer.from(str, "utf8"), len = payload.length; let header;
  if (len < 126) header = Buffer.from([0x81, len]);
  else if (len < 65536) { header = Buffer.alloc(4); header[0] = 0x81; header[1] = 126; header.writeUInt16BE(len, 2); }
  else { header = Buffer.alloc(10); header[0] = 0x81; header[1] = 127; header.writeBigUInt64BE(BigInt(len), 2); }
  return Buffer.concat([header, payload]);
}
const broadcast = () => { const f = wsFrame(JSON.stringify(stateMsg())); for (const s of clients) { try { s.write(f); } catch (e) {} } };
const readBody = (req) => new Promise((res) => { let d = ""; req.on("data", (c) => (d += c)); req.on("end", () => { try { res(d ? JSON.parse(d) : {}); } catch (e) { res({}); } }); });
function send(res, code, obj, type) {
  const body = Buffer.isBuffer(obj) ? obj : typeof obj === "string" ? obj : JSON.stringify(obj);
  res.writeHead(code, { "Content-Type": type || "application/json", "Access-Control-Allow-Origin": "*" });
  res.end(body);
}
const accepted = (res) => send(res, 202, { status: "accepted" });
const MIME = { ".html": "text/html", ".js": "application/javascript", ".css": "text/css", ".json": "application/json", ".svg": "image/svg+xml", ".ico": "image/x-icon" };
function serveStatic(req, res) {
  let p = decodeURIComponent(req.url.split("?")[0]);
  if (p === "/") p = "/index.html";
  if (p.endsWith("/")) p += "index.html";
  const file = path.join(DATA, p);
  if (!file.startsWith(DATA) || !fs.existsSync(file) || !fs.statSync(file).isFile()) return send(res, 404, "not found", "text/plain");
  send(res, 200, fs.readFileSync(file), MIME[path.extname(file)] || "application/octet-stream");
}

const server = http.createServer(async (req, res) => {
  const url = req.url.split("?")[0], m = req.method;
  if (m === "GET" && url === "/api/v2/info") return send(res, 200, { firmware: { name: "LUME", version: "dev", buildHash: "dev" }, limits: { maxLeds: 1000, maxSegments: 8, maxRequestBody: 16384 }, features: { segmentsV2: true, aiPrompts: true, mqtt: false, sacn: false }, controller: { ledCount: dev.controller.ledCount, power: dev.controller.power } });
  if (m === "GET" && url === "/api/v2/effects") return send(res, 200, { effects: EFFECTS });
  if (m === "GET" && url === "/api/v2/palettes") return send(res, 200, { palettes: PALETTES });
  if (m === "GET" && url === "/api/v2/segments") return send(res, 200, { power: dev.controller.power, brightness: dev.controller.brightness, ledCount: dev.controller.ledCount, segments: dev.segments.map(serializeSeg) });
  if (m === "GET" && url.startsWith("/api/v2/segments/")) { const id = parseInt(url.split("/").pop(), 10); const s = dev.segments.find((x) => x.id === id); return s ? send(res, 200, serializeSeg(s)) : send(res, 404, { error: "not found" }); }
  if (m === "GET" && url === "/api/status") return send(res, 200, { online: true, ip: "192.168.1.42", uptime: 384720, wifi: { ssid: "LUME-Studio", rssi: -52, connected: true }, led: { count: dev.controller.ledCount, power: dev.controller.power, brightness: dev.controller.brightness, fps: 60 }, protocols: { sacn: { enabled: false }, mqtt: { enabled: false, connected: false } } });
  if (m === "GET" && url === "/api/config") return send(res, 200, { wifiSSID: "LUME-Studio", ledCount: dev.controller.ledCount, aiApiKey: "****7f2c", aiApiKeySet: true, aiModel: "claude-3-5-sonnet-20241022", sacnEnabled: false, sacnUniverse: 1, mqttEnabled: false, mqttBroker: "mqtt.local", mqttPort: 1883 });
  if (m === "GET" && url === "/api/nightlight") return send(res, 200, dev.nightlight);
  if (m === "GET" && url === "/api/v2/pixels") {
    // Mirrors GET /api/v2/pixels: perceptual bytes, brightness/power applied.
    // A drifting warm wave — enough motion to exercise the live viz path.
    const n = dev.controller.ledCount, t = Date.now() / 1000;
    const level = dev.controller.power ? dev.controller.brightness / 255 : 0;
    let hex = "";
    for (let i = 0; i < n; i++) {
      const w = Math.sin(t * 1.2 + i * 0.09) * 0.5 + 0.5;
      const r = Math.round((40 + 215 * w) * level), g = Math.round((18 + 130 * w) * level), b = Math.round((6 + 60 * w) * level);
      hex += ((1 << 24) | (r << 16) | (g << 8) | b).toString(16).slice(1);
    }
    return send(res, 200, { count: n, rgb: hex });
  }

  if (m === "PUT" && url === "/api/v2/controller") { const b = await readBody(req); if (typeof b.power === "boolean") dev.controller.power = b.power; if (b.brightness != null) dev.controller.brightness = Math.max(0, Math.min(255, b.brightness | 0)); setTimeout(broadcast, 120); return accepted(res); }
  if (m === "POST" && url === "/api/v2/segments") { const b = await readBody(req); if (b.start == null || b.length == null) return send(res, 400, { error: "validation_error", field: "start", message: "Fields 'start' and 'length' are required" }); dev.segments.push({ id: dev.nextId++, start: b.start | 0, length: b.length | 0, reverse: !!b.reverse, brightness: 255, effect: b.effect || "solid", params: defaultParams(b.effect || "solid", b.params), paletteIndex: b.palette != null ? b.palette : null }); setTimeout(broadcast, 120); return accepted(res); }
  if (m === "PUT" && url.startsWith("/api/v2/segments/")) { const id = parseInt(url.split("/").pop(), 10); const b = await readBody(req); const s = dev.segments.find((x) => x.id === id); if (!s) return send(res, 404, { error: "not found" }); if (typeof b.effect === "string" && effById(b.effect)) s.effect = b.effect; if (b.palette != null) s.paletteIndex = b.palette | 0; if (b.brightness != null) s.brightness = Math.max(0, Math.min(255, b.brightness | 0)); if (b.params && typeof b.params === "object") s.params = defaultParams(s.effect, b.params); setTimeout(broadcast, 120); return accepted(res); }
  if (m === "DELETE" && url.startsWith("/api/v2/segments/")) { const id = parseInt(url.split("/").pop(), 10); dev.segments = dev.segments.filter((x) => x.id !== id); setTimeout(broadcast, 120); return accepted(res); }
  if (m === "POST" && url === "/api/prompt") { await readBody(req); return accepted(res); }
  if (m === "POST" && url === "/api/config") { const b = await readBody(req); if (b.ledCount != null) dev.controller.ledCount = b.ledCount | 0; return send(res, 200, { success: true }); }
  if (m === "POST" && url === "/api/nightlight") { await readBody(req); dev.nightlight = { active: true, progress: 0 }; return accepted(res); }
  if (m === "POST" && url === "/api/nightlight/stop") { dev.nightlight = { active: false, progress: 0 }; return send(res, 200, { success: true }); }
  if (m === "OPTIONS") { res.writeHead(204, { "Access-Control-Allow-Origin": "*", "Access-Control-Allow-Methods": "GET,POST,PUT,DELETE,OPTIONS", "Access-Control-Allow-Headers": "Content-Type" }); return res.end(); }
  if (url.startsWith("/api/")) return send(res, 404, { error: "Not found" });
  return serveStatic(req, res);
});

server.on("upgrade", (req, socket) => {
  if (req.url !== "/ws") return socket.destroy();
  const accept = crypto.createHash("sha1").update(req.headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
  socket.write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n");
  clients.add(socket);
  socket.write(wsFrame(JSON.stringify(stateMsg())));
  socket.on("close", () => clients.delete(socket));
  socket.on("error", () => clients.delete(socket));
  socket.on("data", () => {});
});

setInterval(broadcast, 1000);
server.listen(PORT, () => {
  console.log("LUME UI dev server → http://localhost:" + PORT);
});
