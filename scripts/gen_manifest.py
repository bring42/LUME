#!/usr/bin/env python3
"""
Generate manifest.json for a LUME release.

Usage: gen_manifest.py <version> [release_dir]

Reads the already-built per-board images in <release_dir> and writes
<release_dir>/manifest.json with a SHA-256 + size for each. The device fetches
this file from .../releases/latest/download/manifest.json and verifies each
image against it before booting.

Board ids MUST match BOARD_ID_BY_ENV in scripts/version.py and LUME_BOARD_ID in
the firmware (constants.h). File names MUST match what the release workflow
copies into <release_dir>.
"""
import hashlib
import json
import os
import subprocess
import sys

BOARD_IDS = [
    "esp32-s3-devkitc",
    "esp32-c3-devkitm",
    "seeed-xiao-esp32c3",
    "lilygo-t-display-s3",
]


def entry(path):
    data = open(path, "rb").read()
    return {
        "file": os.path.basename(path),
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
    }


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: gen_manifest.py <version> [release_dir]")
    version = sys.argv[1].lstrip("vV")
    release_dir = sys.argv[2] if len(sys.argv) > 2 else "release"

    try:
        build_hash = (
            subprocess.check_output(["git", "rev-parse", "--short", "HEAD"])
            .decode()
            .strip()
        )
    except Exception:
        build_hash = "dev"

    manifest = {
        "version": version,
        "buildHash": build_hash,
        "notes": "See the release notes for details.",
        "boards": {},
    }
    for bid in BOARD_IDS:
        manifest["boards"][bid] = {
            "app": entry(os.path.join(release_dir, f"lume-{bid}.bin")),
            "fs": entry(os.path.join(release_dir, f"littlefs-{bid}.bin")),
        }

    out = os.path.join(release_dir, "manifest.json")
    with open(out, "w") as f:
        json.dump(manifest, f, indent=2)
    print(json.dumps(manifest, indent=2))
    print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
