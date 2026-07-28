#!/usr/bin/env python3
"""
Sync the UI concept sources (ui-concepts/) into the device deployment (data/).

This is the single, drift-proof path from the editable concept sources to what
the firmware actually serves. Edit the skins in ui-concepts/*-live/ and the
shared engine in ui-concepts/_engine/, then run this. (The old UIs drifted from
the API precisely because the deployed copy was hand-maintained — don't do that.)

Deployment layout on the device (LittleFS):
    /                → console-euclid-live  (the main console UI)
      index.html
      /assets/engine.js   (ONE shared engine, loaded by both pages)
      /assets/app.css     (= console styles.css)
      /assets/app.js      (= console app.js)
    /euclid/         → euclid-live  (the Byrne/Euclid plate UI)
      index.html
      style.css
      app.js
      (loads /assets/engine.js)

Run:  python3 scripts/sync_web.py        (from repo root)
Gzip is applied separately at `pio run -t uploadfs` by scripts/gzip_web_files.py.
"""
import hashlib
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "ui-concepts"
DATA = ROOT / "data"

ENGINE_SRC = SRC / "_engine" / "engine.js"
ENGINE_DEVICE_PATH = "/assets/engine.js"


def vtag(path: Path) -> str:
    """8-hex content hash used as the ?v= cache-buster on asset URLs. Changes
    exactly when the served bytes change, so /assets/ keeps its week-long
    max-age without ever pinning a browser to stale JS/CSS."""
    return hashlib.sha1(path.read_bytes()).hexdigest()[:8]


def write(dst: Path, text: str):
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(text, encoding="utf-8")
    print(f"  → {dst.relative_to(ROOT)}")


def copy(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    print(f"  → {dst.relative_to(ROOT)}")


def sync_console():
    """console-euclid-live → root. Assets are rewritten to /assets/*?v=<hash>."""
    print("console-euclid-live → data/ (root)")
    skin = SRC / "console-euclid-live"
    html = (skin / "index.html").read_text(encoding="utf-8")
    html = html.replace("../_engine/engine.js",
                        f"{ENGINE_DEVICE_PATH}?v={vtag(ENGINE_SRC)}")
    html = re.sub(r'href="styles\.css"',
                  f'href="/assets/app.css?v={vtag(skin / "styles.css")}"', html)
    html = re.sub(r'src="app\.js"',
                  f'src="/assets/app.js?v={vtag(skin / "app.js")}"', html)
    write(DATA / "index.html", html)
    copy(skin / "styles.css", DATA / "assets" / "app.css")
    copy(skin / "app.js", DATA / "assets" / "app.js")


def sync_euclid():
    """euclid-live → /euclid/. style.css + app.js stay relative; engine absolute.
    All refs get ?v=<hash> stamps too — /euclid/ isn't long-cached today, but the
    stamps make that safe to change and defeat any heuristic caching."""
    print("euclid-live → data/euclid/")
    skin = SRC / "euclid-live"
    html = (skin / "index.html").read_text(encoding="utf-8")
    html = html.replace("../_engine/engine.js",
                        f"{ENGINE_DEVICE_PATH}?v={vtag(ENGINE_SRC)}")
    html = re.sub(r'href="style\.css"',
                  f'href="style.css?v={vtag(skin / "style.css")}"', html)
    html = re.sub(r'src="app\.js"',
                  f'src="app.js?v={vtag(skin / "app.js")}"', html)
    write(DATA / "euclid" / "index.html", html)
    copy(skin / "style.css", DATA / "euclid" / "style.css")
    copy(skin / "app.js", DATA / "euclid" / "app.js")


def sync_engine():
    print("shared engine → data/assets/engine.js")
    copy(ENGINE_SRC, DATA / "assets" / "engine.js")


def main():
    if not SRC.exists():
        raise SystemExit(f"ui-concepts/ not found at {SRC}")
    sync_engine()
    sync_console()
    sync_euclid()
    print("Done. Remember: `pio run -t uploadfs` gzips + flashes data/.")


if __name__ == "__main__":
    main()
