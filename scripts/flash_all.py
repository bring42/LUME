#
# flash_all.py — one-command full flash.
#
# Registers a `flashall` custom target that uploads the firmware and then the
# LittleFS web UI in a single step, so you don't have to remember to run both
# `upload` and `uploadfs` after a first flash / a UI change:
#
#     pio run -e <env> -t flashall
#
# (uploadfs auto-gzips the web assets via gzip_web_files.py, as usual.)
#
Import("env")  # noqa: F821  (injected by PlatformIO)

pioenv = env["PIOENV"]

env.AddCustomTarget(
    name="flashall",
    dependencies=None,
    actions=[
        "pio run -e {} -t upload".format(pioenv),
        "pio run -e {} -t uploadfs".format(pioenv),
    ],
    title="Flash All",
    description="Upload firmware, then the LittleFS web UI, in one command",
)
