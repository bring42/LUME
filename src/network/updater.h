#ifndef LUME_UPDATER_H
#define LUME_UPDATER_H

// Pull-based OTA: the device checks GitHub Releases for a newer LUME build and
// flashes it over HTTPS. This is distinct from ota.cpp's ArduinoOTA (push-based,
// dev-time). Security posture (v1): HTTPS transport + SHA-256 verification of
// each downloaded image against the release manifest. NO code signing yet — see
// updater.cpp header comment for the documented future hardening path.

#include <Arduino.h>

namespace lume {

enum class UpdatePhase : uint8_t {
    Idle,         // nothing happening
    Checking,     // querying GitHub for the latest release
    UpToDate,     // check finished: running the latest (or newer) version
    Available,    // check finished: a newer version is available (details set)
    Downloading,  // apply in progress: streaming an image
    Verifying,    // apply in progress: checking SHA-256
    Flashing,     // apply in progress: committing to flash
    Rebooting,    // apply succeeded: reboot imminent
    Error         // last operation failed (error message set)
};

const char* updatePhaseName(UpdatePhase p);

// Snapshot of updater state. Returned by value so the web task never races the
// worker's mutation of the live struct.
struct UpdateStatus {
    UpdatePhase phase = UpdatePhase::Idle;
    char    current[24]   = {0};   // running firmware (app) version
    char    fsVersion[24] = {0};   // installed filesystem version (from /fsver; "" if unstamped)
    char    latest[24]    = {0};   // latest published version (after a check)
    // A single check reports whether EITHER image is behind the latest release.
    // updateAvailable is true if the app OR the filesystem needs updating, so a
    // device whose two images have drifted (e.g. an interrupted update, or a
    // legacy filesystem with no version stamp) still self-heals on the next check.
    bool    updateAvailable = false; // app OR fs behind
    bool    appAvailable    = false; // running firmware is behind the latest
    bool    fsAvailable     = false; // installed filesystem is behind the latest
    char    notes[192]    = {0};   // release notes (truncated)
    uint8_t percent       = 0;     // 0..100 within the active phase
    char    stage[8]      = {0};   // which target is in progress: "app"|"fs"|""
    char    error[128]    = {0};   // populated when phase == Error
};

// Create the background worker task + command queue (idempotent). Call once in
// setup(), after storage/config are up.
void initUpdater();

// Enqueue a "check GitHub for updates" job. Returns false if the worker is busy.
// One check populates both appAvailable and fsAvailable from the manifest.
bool requestUpdateCheck();

// Apply the update ATOMICALLY: flash whichever images are behind (filesystem
// first, then firmware) and reboot ONCE. This is the normal update path — the
// two images are versioned together as one release, so they're always updated
// together and can never drift into a half-updated state. Returns false if the
// worker is busy or nothing is available.
bool requestUpdate();

// Apply the firmware (app-slot) image ONLY: download → verify → flash the
// inactive OTA slot → reboot. Low-level recovery/debug path; the UI uses the
// atomic requestUpdate() instead. A/B protected; a failure never bricks.
bool requestAppUpdate();

// Apply the filesystem (LittleFS) image ONLY: download → verify → flash the FS
// partition → reboot. Low-level recovery/debug path; the UI uses the atomic
// requestUpdate() instead. Returns false if busy or no FS update is available.
bool requestFsUpdate();

// Thread-safe snapshot of the current updater status.
UpdateStatus updaterStatus();

// True while an apply is actively downloading/verifying/flashing. The web server
// uses this to stop serving the LittleFS-backed UI while the FS image is being
// overwritten (returns 503 instead of reading a partition mid-erase).
bool updaterInProgress();

} // namespace lume

#endif // LUME_UPDATER_H
