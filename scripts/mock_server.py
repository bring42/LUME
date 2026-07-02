#!/usr/bin/env python3
"""
Dev-only mock of the LUME device HTTP API, for previewing the web UI without
hardware. Serves the REAL data/index.html + data/assets/* and fakes just enough
of the v2 API (effects/palettes/segments/controller/status/config/nightlight/
prompt) for the UI to run. NOT shipped to the device. See docs/API_V2.md.

    python3 scripts/mock_server.py [port]   # default 8080
"""
import json, sys, os, re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.join(os.path.dirname(__file__), "..", "data")

# --- in-memory device state (mutations reflect on subsequent GETs) ---
STATE = {
    "power": True,
    "brightness": 180,
    "ledCount": 160,
    "segments": [
        {"id": 0, "start": 0, "stop": 89, "length": 90, "reverse": False,
         "brightness": 255, "effect": "fire", "params": {"cooling": 55, "sparking": 120, "reversed": False}},
        {"id": 1, "start": 90, "stop": 159, "length": 70, "reverse": False,
         "brightness": 255, "effect": "wave",
         "params": {"color": "#1050ff", "speed": 128, "intensity": 160, "direction": 0}},
    ],
}
NIGHTLIGHT = {"active": False, "progress": 0.0}

# Real param schemas (mirrors src/visuallib/effects/*.cpp DEFINE_EFFECT_SCHEMA).
def C(id, name, d): return {"id": id, "name": name, "type": "color", "default": d}
def I(id, name, d, lo, hi): return {"id": id, "name": name, "type": "int", "default": d, "min": lo, "max": hi}
def F(id, name, d, lo, hi): return {"id": id, "name": name, "type": "float", "default": d, "min": lo, "max": hi}
def B(id, name, d): return {"id": id, "name": name, "type": "bool", "default": d}
def E(id, name, opts, d): return {"id": id, "name": name, "type": "enum", "options": opts, "default": d}
def P(id, name): return {"id": id, "name": name, "type": "palette", "default": "rainbow"}

# id, name, category, dims, params
EFFECTS = [
    ("solid", "Solid", 0, "1d", [C("color", "Color", "#e7a552")]),
    ("gradient", "Gradient", 0, "1d", [C("colorStart", "Start Color", "#0000ff"), C("colorEnd", "End Color", "#ff0000")]),
    ("rainbow", "Rainbow", 1, "1d", [I("speed", "Speed", 128, 1, 255)]),
    ("fire", "Fire", 1, "1d", [I("cooling", "Cooling", 55, 20, 100), I("sparking", "Sparking", 120, 50, 200), B("reversed", "Reversed", False)]),
    ("fireup", "Fire Up", 1, "1d", [I("cooling", "Cooling", 55, 20, 100), I("sparking", "Sparking", 120, 50, 200)]),
    ("colorwaves", "Color Waves", 1, "1d", [P("palette", "Palette"), I("speed", "Speed", 128, 1, 255)]),
    ("wave", "Wave", 2, "1d", [C("color", "Wave Color", "#0000ff"), I("speed", "Wave Speed", 128, 1, 255), I("intensity", "Wave Width", 160, 1, 255), E("direction", "Direction", "Up|Down|Center", 0)]),
    ("theater", "Theater", 2, "1d", [C("color", "Color", "#ffffff"), I("speed", "Speed", 128, 1, 255)]),
    ("sparkle", "Sparkle", 1, "1d", [C("color", "Color", "#ffffff"), I("intensity", "Amount", 128, 0, 255)]),
    ("pulse", "Pulse", 1, "1d", [C("color", "Color", "#ff0040"), I("speed", "Speed", 128, 1, 255)]),
    ("breathe", "Breathe", 1, "1d", [C("color", "Color", "#0000ff"), I("speed", "Speed", 128, 1, 255)]),
    ("noise", "Noise", 3, "1d", [P("palette", "Palette"), I("speed", "Speed", 128, 1, 255), F("scale", "Scale", 0.3, 0.0, 1.0)]),
    ("meteor", "Meteor", 2, "1d", [C("color", "Meteor Color", "#ffffff"), I("speed", "Fall Speed", 128, 1, 255)]),
    ("comet", "Comet", 2, "1d", [C("color", "Color", "#e7a552"), I("speed", "Speed", 128, 1, 255)]),
    ("rain", "Rain", 2, "1d", [C("color", "Drop Color", "#4090ff"), I("intensity", "Density", 128, 0, 255)]),
    ("twinkle", "Twinkle", 1, "1d", [P("palette", "Palette"), I("intensity", "Amount", 128, 0, 255)]),
    ("strobe", "Strobe", 3, "1d", [C("color", "Color", "#ffffff"), I("speed", "Rate", 128, 1, 255)]),
    ("sinelon", "Sinelon", 2, "1d", [P("palette", "Palette"), I("speed", "Speed", 128, 1, 255)]),
    ("scanner", "Scanner", 2, "1d", [C("color", "Color", "#ff0000"), I("speed", "Speed", 128, 1, 255)]),
    ("candle", "Candle", 1, "1d", [C("color", "Flame", "#ff9030"), I("intensity", "Flicker", 128, 0, 255)]),
    ("pride", "Pride", 3, "1d", [I("speed", "Speed", 128, 1, 255)]),
    ("pacifica", "Pacifica", 3, "1d", [I("speed", "Speed", 128, 1, 255)]),
    ("confetti", "Confetti", 1, "1d", [P("palette", "Palette"), I("intensity", "Amount", 128, 0, 255)]),
]
PALETTES = [{"id": i, "name": n} for i, n in enumerate(
    ["Rainbow", "Lava", "Ocean", "Party", "Forest", "Cloud", "Heat"])]


def effects_payload():
    out = []
    for id, name, cat, dims, params in EFFECTS:
        out.append({
            "id": id, "name": name, "category": cat, "dims": dims,
            "usesPalette": any(p["type"] == "palette" for p in params),
            "usesPrimaryColor": any(p["type"] == "color" for p in params),
            "usesSecondaryColor": sum(p["type"] == "color" for p in params) > 1,
            "usesSpeed": any(p["id"] == "speed" for p in params),
            "usesIntensity": any(p["id"] == "intensity" for p in params),
            "params": params,
        })
    return {"effects": out}


def seg(id):
    return next((s for s in STATE["segments"] if s["id"] == id), None)


class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def _send(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _static(self, path):
        rel = "index.html" if path == "/" else path.lstrip("/")
        fp = os.path.normpath(os.path.join(ROOT, rel))
        if not fp.startswith(os.path.normpath(ROOT)) or not os.path.isfile(fp):
            return self._send(404, {"error": "not_found"})
        ct = {"html": "text/html", "css": "text/css", "js": "application/javascript"}.get(fp.rsplit(".", 1)[-1], "text/plain")
        with open(fp, "rb") as f:
            self._send(200, f.read(), ct)

    def _body(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        try: return json.loads(self.rfile.read(n) or b"{}")
        except Exception: return {}

    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/api/v2/effects": return self._send(200, effects_payload())
        if p == "/api/v2/palettes": return self._send(200, {"palettes": PALETTES})
        if p == "/api/v2/segments":
            return self._send(200, {k: STATE[k] for k in ("power", "brightness", "ledCount", "segments")})
        m = re.match(r"^/api/v2/segments/(\d+)$", p)
        if m:
            s = seg(int(m.group(1)))
            return self._send(200, s) if s else self._send(404, {"error": "not_found"})
        if p == "/api/v2/controller":
            return self._send(200, {k: STATE[k] for k in ("power", "brightness", "ledCount")})
        if p == "/api/status":
            return self._send(200, {"online": True, "ip": "192.168.0.177", "uptime": 4212,
                "wifi": "MockNet", "sacn": {"enabled": False},
                "mqtt": {"enabled": True, "connected": True, "broker": "192.168.0.10"}})
        if p == "/health":
            return self._send(200, {"status": "ok", "uptime": 4212, "freeHeap": 208000, "wifiRSSI": -63})
        if p == "/api/config":
            return self._send(200, {"wifiSSID": "MockNet", "ledCount": STATE["ledCount"],
                "aiApiKey": "****", "aiApiKeySet": True, "aiModel": "claude-3-5-haiku-20241022",
                "sacnEnabled": False, "sacnUniverse": 1, "sacnStartChannel": 1,
                "mqttEnabled": True, "mqttBroker": "192.168.0.10", "mqttPort": 1883,
                "mqttUsername": "", "mqttTopicPrefix": "lume"})
        if p == "/api/nightlight": return self._send(200, NIGHTLIGHT)
        if p == "/api/scenes": return self._send(200, [])
        return self._static(p)

    def do_PUT(self):
        p = self.path.split("?")[0]
        b = self._body()
        if p == "/api/v2/controller":
            if "power" in b: STATE["power"] = bool(b["power"])
            if "brightness" in b: STATE["brightness"] = int(b["brightness"])
            return self._send(202, {"accepted": True})
        m = re.match(r"^/api/v2/segments/(\d+)$", p)
        if m:
            s = seg(int(m.group(1)))
            if not s: return self._send(404, {"error": "not_found"})
            for k in ("effect", "params", "brightness"):
                if k in b: s[k] = b[k]
            return self._send(202, {"accepted": True})
        return self._send(404, {"error": "not_found"})

    def do_POST(self):
        p = self.path.split("?")[0]
        b = self._body()
        if p == "/api/v2/segments":
            nid = max([s["id"] for s in STATE["segments"]], default=-1) + 1
            start, length = int(b.get("start", 0)), int(b.get("length", 1))
            STATE["segments"].append({"id": nid, "start": start, "stop": start + length - 1,
                "length": length, "reverse": bool(b.get("reverse", False)), "brightness": 255,
                "effect": b.get("effect", "solid"), "params": b.get("params", {})})
            return self._send(202, {"accepted": True, "id": nid})
        if p == "/api/config": return self._send(200, {"success": True})
        if p == "/api/nightlight":
            NIGHTLIGHT.update(active=True, progress=0.0)
            return self._send(200, {"success": True})
        if p == "/api/nightlight/stop":
            NIGHTLIGHT.update(active=False, progress=0.0)
            return self._send(200, {"success": True})
        if p == "/api/prompt":
            return self._send(200, {"success": True, "message": "Lights updated.",
                "spec": {"effect": "fire"}})
        if p == "/api/pixels": return self._send(202, {"accepted": True})
        return self._send(404, {"error": "not_found"})

    def do_DELETE(self):
        m = re.match(r"^/api/v2/segments/(\d+)$", self.path.split("?")[0])
        if m:
            STATE["segments"] = [s for s in STATE["segments"] if s["id"] != int(m.group(1))]
            return self._send(202, {"accepted": True})
        return self._send(404, {"error": "not_found"})


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"LUME mock device → http://localhost:{port}  (serving {os.path.normpath(ROOT)})")
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
