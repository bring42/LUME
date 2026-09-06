#!/usr/bin/env python3
"""
Prepare data/ for the ESP32 filesystem image (runs pre buildfs/uploadfs):

1. Re-stamp the ?v=<content-hash> cache-busters on /assets/ URLs in the HTML
   entry points. /assets/ is served with a week-long max-age, so every asset
   reference carries an 8-hex sha1 of the file's bytes — the URL changes exactly
   when the served bytes change, and a cached page can never pin stale JS/CSS.
   (This used to live in scripts/sync_web.py; data/ is now the one source of
   truth for the web UI, so the stamps are maintained here, at image build.)
2. Gzip HTML/CSS/JS — ESPAsyncWebServer serves .gz automatically if present.

data/ is committed with its stamps in place; this hook rewrites them only when
an asset actually changed, so a clean tree stays clean. Also runs standalone
(`python3 scripts/gzip_web_files.py` from the repo root) to refresh the stamps
after editing data/ without doing a full image build.
"""
try:
    Import("env")  # noqa: F821 — provided by PlatformIO/SCons when run as a hook
    _PIO = True
except NameError:
    _PIO = False
import gzip
import hashlib
import re
import shutil
from pathlib import Path

# Matches an /assets/ reference with OR WITHOUT an existing ?v= stamp: a
# newly added <script src="/assets/new.js"> used to be silently skipped and
# then served with the week-long max-age, pinning users to a stale copy.
# Anchored on the closing quote so only whole attribute values match.
ASSET_REF = re.compile(r"""(?P<path>/assets/[\w./-]+?)(?:\?v=[0-9a-f]+)?(?=["'])""")

def stamp_asset_hashes(data_dir):
    """Rewrite ?v= stamps in data/*.html to each asset's current sha1[:8]."""
    for html_path in data_dir.rglob("*.html"):
        html = html_path.read_text(encoding="utf-8")

        def restamp(m):
            asset = data_dir / m.group("path").lstrip("/")
            if not asset.is_file():
                raise SystemExit(f"❌ {html_path} references missing asset {m.group('path')}")
            tag = hashlib.sha1(asset.read_bytes()).hexdigest()[:8]
            return f"{m.group('path')}?v={tag}"

        stamped = ASSET_REF.sub(restamp, html)
        if stamped != html:
            html_path.write_text(stamped, encoding="utf-8")
            print(f"✓ re-stamped asset hashes in {html_path}")

def gzip_web_files(source, target, env):
    """Compress HTML, CSS, and JS files in data/ directory"""
    data_dir = Path("data")

    if not data_dir.exists():
        print("⚠️  data/ directory not found, skipping gzip")
        return

    stamp_asset_hashes(data_dir)
    
    # File extensions to compress
    extensions = {".html", ".css", ".js", ".json", ".svg", ".xml"}
    
    files_compressed = 0
    total_saved = 0
    
    for file_path in data_dir.rglob("*"):
        if file_path.is_file() and file_path.suffix in extensions:
            gz_path = file_path.with_suffix(file_path.suffix + ".gz")
            
            # Skip if .gz is newer than source
            if gz_path.exists() and gz_path.stat().st_mtime > file_path.stat().st_mtime:
                continue
            
            original_size = file_path.stat().st_size
            
            with open(file_path, 'rb') as f_in:
                with gzip.open(gz_path, 'wb', compresslevel=9) as f_out:
                    shutil.copyfileobj(f_in, f_out)
            
            compressed_size = gz_path.stat().st_size
            saved = original_size - compressed_size
            total_saved += saved
            files_compressed += 1
            
            percent = (saved / original_size * 100) if original_size > 0 else 0
            print(f"✓ {file_path.name}: {original_size:,} → {compressed_size:,} bytes (-{percent:.0f}%)")
    
    if files_compressed > 0:
        print(f"📦 Compressed {files_compressed} files, saved {total_saved:,} bytes total")
    else:
        print("✓ All files already compressed")

# Register the callback to run before the filesystem IMAGE FILE is built.
#
# This MUST target the image file, not the "buildfs"/"uploadfs" aliases. An
# alias's pre-actions run after the alias's dependency — the image — has
# already been built, so `AddPreAction("buildfs", ...)` fires too late:
# verified by deleting data/*.gz and running `pio run -t buildfs`, which
# printed "Building FS image" BEFORE the stamp+gzip output. On a fresh
# checkout (CI has no committed .gz — they are gitignored) that packed
# uncompressed, unstamped assets into the published littlefs.bin.
#
# $BUILD_DIR/${ESP32_FS_IMAGE_NAME}.bin is the real target the espressif32
# builder produces (platform builder main.py), and uploadfs/uploadfsota
# depend on it too, so this one registration covers every path.
if _PIO:
    env.AddPreAction("$BUILD_DIR/${ESP32_FS_IMAGE_NAME}.bin", gzip_web_files)
else:
    gzip_web_files(None, None, None)
