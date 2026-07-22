#!/usr/bin/env python3
"""
Inject firmware version metadata into the build as -D macros.

Runs as a PlatformIO *pre* extra_script. It defines, per build:
  FIRMWARE_VERSION   semver string, from the git tag (or FW_VERSION env), else
                     the fallback in constants.h stays in effect.
  FIRMWARE_BUILD_HASH short git hash (or "dev" when git is unavailable).
  LUME_BOARD_ID       release-asset id for THIS env, so the running firmware
                      knows which manifest.json entry / .bin to pull.

Version resolution order:
  1. $FW_VERSION            (explicit override, e.g. CI passes the tag)
  2. `git describe --tags`  (e.g. "v1.2.0" or "v1.2.0-3-gabc1234")
  3. (nothing)             -> constants.h fallback "1.0.0" is used

The macro values are added ONLY when we resolved them, so a plain local
`pio run` with no tags still compiles (using the constants.h fallbacks).
"""
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)
import subprocess
import os
import re

# Map PlatformIO env name -> release asset id. Keep in sync with the keys the
# release workflow writes into manifest.json and its asset filenames.
BOARD_ID_BY_ENV = {
    "esp32-s3-devkitc-1": "esp32-s3-devkitc",
    "esp32-c3-devkitm-1": "esp32-c3-devkitm",
    "seeed_xiao_esp32c3": "seeed-xiao-esp32c3",
    "lilygo-t-display-s3": "lilygo-t-display-s3",
}


def _git(args):
    try:
        out = subprocess.check_output(
            ["git"] + args, stderr=subprocess.DEVNULL, cwd=env.subst("$PROJECT_DIR")
        )
        return out.decode().strip()
    except Exception:
        return ""


def _clean_semver(raw):
    """Strip a leading 'v' and any -N-gHASH suffix -> bare semver, or ''."""
    if not raw:
        return ""
    raw = raw.lstrip("vV")
    m = re.match(r"^(\d+\.\d+\.\d+)", raw)
    return m.group(1) if m else ""


pioenv = env.get("PIOENV", "")
board_id = BOARD_ID_BY_ENV.get(pioenv, "")

version = _clean_semver(os.environ.get("FW_VERSION", "")) or _clean_semver(
    _git(["describe", "--tags", "--always"])
)
build_hash = _git(["rev-parse", "--short", "HEAD"]) or "dev"

defines = [("FIRMWARE_BUILD_HASH", env.StringifyMacro(build_hash))]
if version:
    defines.append(("FIRMWARE_VERSION", env.StringifyMacro(version)))
if board_id:
    defines.append(("LUME_BOARD_ID", env.StringifyMacro(board_id)))

env.Append(CPPDEFINES=defines)

print(
    "🔖 version.py: env=%s version=%s hash=%s board_id=%s"
    % (pioenv, version or "(fallback)", build_hash, board_id or "(fallback)")
)
