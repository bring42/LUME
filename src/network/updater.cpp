/**
 * updater.cpp - Pull-based OTA from GitHub Releases.
 *
 * Flow: the device fetches manifest.json from the LATEST GitHub release
 * (via the stable .../releases/latest/download/manifest.json redirect URL),
 * reads the entry for THIS board (LUME_BOARD_ID), compares the published semver
 * to the running FIRMWARE_VERSION, and — on request — downloads and flashes the
 * app image (and, if present, the LittleFS image) over HTTPS.
 *
 * ── Security posture (v1) ────────────────────────────────────────────────────
 *   • Transport:  HTTPS. GitHub redirects release-asset downloads to
 *                 objects.githubusercontent.com, so we FOLLOW redirects. TLS
 *                 uses setInsecure() (no cert pinning) — the same pragmatic
 *                 choice already shipping in src/api/prompt.cpp.
 *   • Integrity:  each image is streamed through a SHA-256 hasher and compared
 *                 against the manifest's checksum BEFORE the boot partition is
 *                 switched. A hash mismatch (or truncated download) aborts the
 *                 Update handle and leaves the running app untouched.
 *   • Anti-brick: Update.h writes to the *inactive* OTA slot; the bootloader is
 *                 only pointed at it on a verified, complete image. A failed
 *                 download/verify therefore cannot brick the device — it keeps
 *                 running the current app.
 *
 *   NOT yet implemented (documented future hardening, in priority order):
 *     1. Code signing / secure boot — verify a signature over the image, not
 *        just a checksum the same server could forge. Requires enabling ESP32
 *        Secure Boot v2 + signed app images in the build.
 *     2. TLS certificate pinning — pin GitHub's / Fastly's root instead of
 *        setInsecure(), so a MITM can't serve a substitute (still-valid-hash)
 *        image over a forged TLS session.
 *     3. Bootloader rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) so a
 *        verified-but-broken build auto-reverts on the next boot.
 */

#include "updater.h"
#include "../constants.h"
#include "../logging.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>

#include "mbedtls/sha256.h"
#include "mbedtls/version.h"
// mbedtls renamed the streaming SHA API between 2.x (_ret suffix) and 3.x.
#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
  #define LUME_SHA_STARTS(c)     mbedtls_sha256_starts((c), 0)
  #define LUME_SHA_UPDATE(c,b,l) mbedtls_sha256_update((c), (b), (l))
  #define LUME_SHA_FINISH(c,o)   mbedtls_sha256_finish((c), (o))
#else
  #define LUME_SHA_STARTS(c)     mbedtls_sha256_starts_ret((c), 0)
  #define LUME_SHA_UPDATE(c,b,l) mbedtls_sha256_update_ret((c), (b), (l))
  #define LUME_SHA_FINISH(c,o)   mbedtls_sha256_finish_ret((c), (o))
#endif

namespace lume {

const char* updatePhaseName(UpdatePhase p) {
    switch (p) {
        case UpdatePhase::Idle:        return "idle";
        case UpdatePhase::Checking:    return "checking";
        case UpdatePhase::UpToDate:    return "up_to_date";
        case UpdatePhase::Available:   return "available";
        case UpdatePhase::Downloading: return "downloading";
        case UpdatePhase::Verifying:   return "verifying";
        case UpdatePhase::Flashing:    return "flashing";
        case UpdatePhase::Rebooting:   return "rebooting";
        case UpdatePhase::Error:       return "error";
    }
    return "idle";
}

namespace {

// ── Worker plumbing ──────────────────────────────────────────────────────────
// ApplyBoth is the normal atomic path (flash whichever images are behind, one
// reboot). ApplyApp/ApplyFs remain as low-level recovery/debug operations.
enum class Cmd : uint8_t { Check, ApplyBoth, ApplyApp, ApplyFs };

QueueHandle_t     g_cmdQueue = nullptr;
TaskHandle_t      g_task     = nullptr;
SemaphoreHandle_t g_mutex    = nullptr;
std::atomic<bool> g_busy{false};

// Live status, guarded by g_mutex.
UpdateStatus g_status;

// Resolved download targets from the last successful check. Written by the worker
// task; the web task reads only the word-atomic bool availability fields in
// requestAppUpdate/requestFsUpdate. The g_busy CAS gate is the real serialization
// (an apply can't proceed during a check), so those unsynchronized bool reads are
// benign. Do NOT add a non-atomic field read cross-task here without a lock.
struct Target {
    String  latest;
    String  appUrl, appSha;  size_t appSize = 0;
    String  fsUrl,  fsSha;   size_t fsSize  = 0;
    bool    hasFs = false;
    bool    available = false;   // appBehind || fsBehind
    bool    appBehind = false;   // running firmware older than latest
    bool    fsBehind  = false;   // installed filesystem older than latest (or unstamped)
} g_target;

// ── Status helpers (locked) ──────────────────────────────────────────────────
void lock()   { if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY); }
void unlock() { if (g_mutex) xSemaphoreGive(g_mutex); }

void copyStr(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

void setPhase(UpdatePhase phase, const char* stage = "", uint8_t percent = 0) {
    lock();
    g_status.phase = phase;
    copyStr(g_status.stage, sizeof(g_status.stage), stage);
    g_status.percent = percent;
    if (phase != UpdatePhase::Error) g_status.error[0] = '\0';
    unlock();
}

void setPercent(uint8_t percent) {
    lock();
    g_status.percent = percent;
    unlock();
}

void setError(const String& msg) {
    lock();
    g_status.phase = UpdatePhase::Error;
    copyStr(g_status.error, sizeof(g_status.error), msg.c_str());
    unlock();
    LOG_ERROR(LogTag::OTA, "Updater: %s", msg.c_str());
}

// ── Version compare ──────────────────────────────────────────────────────────
// Returns true if `candidate` is strictly newer than `running` (semver x.y.z;
// any -suffix/+build is ignored). Non-numeric input compares as 0.
bool isNewer(const String& candidate, const String& running) {
    auto parse = [](const String& v, int out[3]) {
        out[0] = out[1] = out[2] = 0;
        int idx = 0, start = 0;
        for (int i = 0; i <= (int)v.length() && idx < 3; i++) {
            char c = (i < (int)v.length()) ? v[i] : '.';
            if (c == '.' || c == '-' || c == '+') {
                out[idx++] = v.substring(start, i).toInt();
                start = i + 1;
                if (c == '-' || c == '+') break;
            }
        }
    };
    int a[3], b[3];
    parse(candidate, a);
    parse(running, b);
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

// ── HTTPS helpers ────────────────────────────────────────────────────────────
// A newly-configured insecure TLS client. Caller owns it for the request's life.
void configureClient(WiFiClientSecure& client) {
    client.setInsecure();          // v1: no cert pinning (see file header)
    client.setTimeout(UPDATER_HTTP_TIMEOUT_MS / 1000);
}

// GET a (small) resource fully into `out`. Follows GitHub's cross-host redirect.
bool httpsGetString(const String& url, String& out, String& err) {
    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(UPDATER_HTTP_TIMEOUT_MS);
    http.setUserAgent("LUME-Updater");
    if (!http.begin(client, url)) { err = "http.begin failed"; return false; }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        err = "HTTP " + String(code) + " for manifest";
        http.end();
        return false;
    }
    out = http.getString();
    http.end();
    return out.length() > 0;
}

// Lowercase hex of a 32-byte digest.
String toHex(const uint8_t* d, size_t n) {
    static const char* hx = "0123456789abcdef";
    String s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; i++) { s += hx[d[i] >> 4]; s += hx[d[i] & 0xF]; }
    return s;
}

// Download `url`, verify SHA-256 == expectedSha, and flash into `command`
// (U_FLASH or U_SPIFFS). Boots the new image only after verification passes.
bool downloadVerifyFlash(const String& url, const String& expectedSha,
                         size_t expectedSize, int command,
                         const char* stage, String& err) {
    if (expectedSize == 0) { err = String(stage) + ": manifest size is 0"; return false; }

    setPhase(UpdatePhase::Downloading, stage, 0);
    LOG_INFO(LogTag::OTA, "Updater: downloading %s image (%u bytes)", stage,
             (unsigned)expectedSize);

    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(UPDATER_HTTP_TIMEOUT_MS);
    http.setUserAgent("LUME-Updater");
    if (!http.begin(client, url)) { err = String(stage) + ": http.begin failed"; return false; }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        err = String(stage) + ": HTTP " + String(code);
        http.end();
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen > 0 && (size_t)contentLen != expectedSize) {
        err = String(stage) + ": size mismatch (server " + String(contentLen) +
              " vs manifest " + String(expectedSize) + ")";
        http.end();
        return false;
    }

    if (!Update.begin(expectedSize, command)) {
        err = String(stage) + ": Update.begin: " + Update.errorString();
        http.end();
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    LUME_SHA_STARTS(&sha);

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[UPDATER_CHUNK_SIZE];
    size_t written = 0;
    uint8_t lastPct = 255;

    while (written < expectedSize) {
        if (!http.connected() && stream->available() == 0) break;
        size_t avail = stream->available();
        if (avail == 0) { delay(1); continue; }
        size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
        if (want > expectedSize - written) want = expectedSize - written;
        int got = stream->readBytes(buf, want);
        if (got <= 0) { delay(1); continue; }

        LUME_SHA_UPDATE(&sha, buf, got);
        if (Update.write(buf, got) != (size_t)got) {
            err = String(stage) + ": Update.write: " + Update.errorString();
            Update.abort();
            mbedtls_sha256_free(&sha);
            http.end();
            return false;
        }
        written += got;

        uint8_t pct = (uint8_t)((uint64_t)written * 100 / expectedSize);
        if (pct != lastPct) { lastPct = pct; setPercent(pct); }
    }
    http.end();

    if (written != expectedSize) {
        err = String(stage) + ": truncated (" + String(written) + "/" +
              String(expectedSize) + ")";
        Update.abort();
        mbedtls_sha256_free(&sha);
        return false;
    }

    // Verify BEFORE committing the boot switch.
    setPhase(UpdatePhase::Verifying, stage, 100);
    uint8_t digest[32];
    LUME_SHA_FINISH(&sha, digest);
    mbedtls_sha256_free(&sha);
    String got = toHex(digest, sizeof(digest));
    String want = expectedSha;
    want.toLowerCase();
    if (got != want) {
        err = String(stage) + ": SHA-256 mismatch (got " + got.substring(0, 12) +
              "… want " + want.substring(0, 12) + "…)";
        Update.abort();
        return false;
    }

    setPhase(UpdatePhase::Flashing, stage, 100);
    if (!Update.end(true)) {   // true = set boot partition (app) / finalize (fs)
        err = String(stage) + ": Update.end: " + Update.errorString();
        return false;
    }
    LOG_INFO(LogTag::OTA, "Updater: %s image verified + flashed", stage);
    return true;
}

// Read the filesystem's own version stamp, written into the LittleFS image at
// build time (scripts/version.py -> data/fsver). Returns "" if the file is
// absent — which is the case for any image built before the stamp existed, so
// the caller treats "" as "older than any release" and self-heals it.
String readInstalledFsVersion() {
    File f = LittleFS.open("/fsver", "r");
    if (!f) return String();
    String v = f.readStringUntil('\n');
    f.close();
    v.trim();
    return v;
}

// ── Check ────────────────────────────────────────────────────────────────────
bool doCheck(String& err) {
    setPhase(UpdatePhase::Checking);
    g_target = Target{};

    lock();
    copyStr(g_status.current, sizeof(g_status.current), FIRMWARE_VERSION);
    g_status.latest[0] = '\0';
    g_status.notes[0] = '\0';
    g_status.updateAvailable = false;
    g_status.appAvailable = false;
    g_status.fsAvailable = false;
    unlock();

    const String manifestUrl =
        String("https://github.com/") + LUME_GH_OWNER + "/" + LUME_GH_REPO +
        "/releases/latest/download/manifest.json";

    String body;
    if (!httpsGetString(manifestUrl, body, err)) return false;

    // Filter: only the fields we need for THIS board (keeps the doc small on C3).
    JsonDocument filter;
    filter["version"] = true;
    filter["notes"]   = true;
    filter["boards"][LUME_BOARD_ID]["app"]["file"]   = true;
    filter["boards"][LUME_BOARD_ID]["app"]["sha256"] = true;
    filter["boards"][LUME_BOARD_ID]["app"]["size"]   = true;
    filter["boards"][LUME_BOARD_ID]["fs"]["file"]    = true;
    filter["boards"][LUME_BOARD_ID]["fs"]["sha256"]  = true;
    filter["boards"][LUME_BOARD_ID]["fs"]["size"]    = true;

    JsonDocument doc;
    DeserializationError je =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (je) { err = String("manifest parse: ") + je.c_str(); return false; }

    const char* latest = doc["version"] | "";
    if (!latest[0]) { err = "manifest missing version"; return false; }

    JsonObject board = doc["boards"][LUME_BOARD_ID];
    if (board.isNull() || board["app"].isNull()) {
        err = String("manifest has no assets for board '") + LUME_BOARD_ID + "'";
        return false;
    }

    const String assetBase =
        String("https://github.com/") + LUME_GH_OWNER + "/" + LUME_GH_REPO +
        "/releases/latest/download/";

    g_target.latest  = latest;
    g_target.appUrl  = assetBase + (const char*)(board["app"]["file"] | "");
    g_target.appSha  = (const char*)(board["app"]["sha256"] | "");
    g_target.appSize = board["app"]["size"] | 0;

    if (!board["fs"].isNull() && board["fs"]["file"].is<const char*>()) {
        g_target.fsUrl  = assetBase + (const char*)(board["fs"]["file"] | "");
        g_target.fsSha  = (const char*)(board["fs"]["sha256"] | "");
        g_target.fsSize = board["fs"]["size"] | 0;
        g_target.hasFs  = g_target.fsSize > 0 && g_target.fsSha.length() > 0;
    }

    if (g_target.appSize == 0 || g_target.appSha.length() != 64) {
        err = "manifest app entry incomplete (size/sha256)";
        return false;
    }

    // Compare BOTH installed images to the latest release independently. The app
    // version is baked in (FIRMWARE_VERSION); the fs version is read from the
    // running LittleFS image's /fsver stamp. An unstamped fs (legacy image) reads
    // as "" and is treated as behind, so a device that only ever got a firmware
    // update — the classic footgun — still self-heals its stale UI on next check.
    const String fsInstalled = readInstalledFsVersion();

    bool appBehind = isNewer(g_target.latest, String(FIRMWARE_VERSION));
    bool fsBehind;
    if (!g_target.hasFs)                fsBehind = false;              // no fs image published
    else if (fsInstalled.length() == 0) fsBehind = true;              // unstamped legacy fs -> stale
    else                                fsBehind = isNewer(g_target.latest, fsInstalled);

    bool avail = appBehind || fsBehind;
    g_target.appBehind = appBehind;
    g_target.fsBehind  = fsBehind;
    g_target.available = avail;

    const char* notes = doc["notes"] | "";

    lock();
    copyStr(g_status.latest, sizeof(g_status.latest), g_target.latest.c_str());
    copyStr(g_status.fsVersion, sizeof(g_status.fsVersion), fsInstalled.c_str());
    copyStr(g_status.notes, sizeof(g_status.notes), notes);
    g_status.updateAvailable = avail;
    g_status.appAvailable = appBehind;
    g_status.fsAvailable = fsBehind;
    g_status.phase = avail ? UpdatePhase::Available : UpdatePhase::UpToDate;
    unlock();

    LOG_INFO(LogTag::OTA, "Updater: app=%s fs=%s latest=%s -> %s (app=%s fs=%s)",
             FIRMWARE_VERSION, fsInstalled.length() ? fsInstalled.c_str() : "(unstamped)",
             g_target.latest.c_str(), avail ? "UPDATE AVAILABLE" : "up to date",
             appBehind ? "yes" : "no", fsBehind ? "yes" : "no");
    return true;
}

// ── Apply: firmware (app slot) ONLY ──────────────────────────────────────────
// Written to the INACTIVE OTA slot, so the web server keeps serving normally and
// a failure can't brick (boot stays on the old app). Reboots on success.
void doApplyApp() {
    if (!g_target.available) {
        setError("No firmware update available (run check first)");
        return;
    }
    LOG_INFO(LogTag::OTA, "Updater: applying firmware %s", g_target.latest.c_str());

    String err;
    if (!downloadVerifyFlash(g_target.appUrl, g_target.appSha, g_target.appSize,
                             U_FLASH, "app", err)) {
        setError(err);
        return;
    }

    setPhase(UpdatePhase::Rebooting, "app", 100);
    LOG_INFO(LogTag::OTA, "Updater: firmware update complete, rebooting");
    delay(1500);          // let the polling UI observe the "rebooting" status
    ESP.restart();
}

// ── Apply: filesystem (LittleFS) ONLY ────────────────────────────────────────
// Independent of the firmware update. This overwrites the LittleFS partition the
// UI is served from — updaterInProgress() is true throughout, so the server
// returns 503 for UI reads instead of reading a partition mid-erase. An
// interrupted FS flash can garble the UI, but it's a single partition (no A/B),
// recoverable by re-running this step. Reboots on success so the new UI mounts.
void doApplyFs() {
    if (!g_target.available || !g_target.hasFs) {
        setError("No filesystem update available (run check first)");
        return;
    }
    LOG_INFO(LogTag::OTA, "Updater: applying filesystem %s", g_target.latest.c_str());

    String err;
    if (!downloadVerifyFlash(g_target.fsUrl, g_target.fsSha, g_target.fsSize,
                             U_SPIFFS, "fs", err)) {
        setError(err);
        return;
    }

    setPhase(UpdatePhase::Rebooting, "fs", 100);
    LOG_INFO(LogTag::OTA, "Updater: filesystem update complete, rebooting");
    delay(1500);
    ESP.restart();
}

// ── Apply: atomic (filesystem + firmware) ────────────────────────────────────
// The normal update path. Flashes only the images that are actually behind —
// filesystem first (single partition, in-place), then firmware (inactive A/B
// slot) — and reboots ONCE at the end so the two images move together. On any
// failure it stops and reports; a later check re-detects whatever is still
// behind (the fs stamp makes a half-finished update self-healing), so the user
// just retries. Order matters: if the app flash fails after the fs succeeded,
// we boot the old app on the new UI (tolerated); we never boot a new app on an
// old UI.
void doApplyBoth() {
    if (!g_target.available) {
        setError("No update available (run check first)");
        return;
    }
    LOG_INFO(LogTag::OTA, "Updater: applying update %s (fs=%s app=%s)",
             g_target.latest.c_str(),
             (g_target.fsBehind && g_target.hasFs) ? "yes" : "no",
             g_target.appBehind ? "yes" : "no");

    String err;
    bool fsFlashed = false;
    if (g_target.fsBehind && g_target.hasFs) {
        if (!downloadVerifyFlash(g_target.fsUrl, g_target.fsSha, g_target.fsSize,
                                 U_SPIFFS, "fs", err)) {
            // fs partition may be partially written; stay up (no reboot) so the
            // user can retry, matching doApplyFs's documented behavior.
            setError(err);
            return;
        }
        fsFlashed = true;
    }
    if (g_target.appBehind) {
        if (!downloadVerifyFlash(g_target.appUrl, g_target.appSha, g_target.appSize,
                                 U_FLASH, "app", err)) {
            setError(err);
            // If the fs was already flashed, the new UI is committed and live
            // under a now-stale mount — we MUST reboot so it mounts cleanly. We
            // come back on the OLD app (its boot slot was never switched) serving
            // the NEW UI, which is the tolerated direction; the next check sees
            // appBehind and re-offers the firmware (self-heal). Without the fs
            // flash there's nothing destructive to recover from — just report.
            if (fsFlashed) {
                LOG_INFO(LogTag::OTA,
                         "Updater: app flash failed after fs; rebooting to mount new fs (firmware re-offered next check)");
                setPhase(UpdatePhase::Rebooting, "", 100);
                delay(1500);
                ESP.restart();
            }
            return;
        }
    }

    setPhase(UpdatePhase::Rebooting, "", 100);
    LOG_INFO(LogTag::OTA, "Updater: update complete, rebooting");
    delay(1500);          // let the polling UI observe the "rebooting" status
    ESP.restart();
}

void workerTask(void*) {
    Cmd cmd;
    for (;;) {
        if (xQueueReceive(g_cmdQueue, &cmd, portMAX_DELAY) != pdTRUE) continue;

        String err;
        switch (cmd) {
            case Cmd::Check:     if (!doCheck(err)) setError(err); break;
            case Cmd::ApplyBoth: doApplyBoth(); break;  // sets its own error / reboots
            case Cmd::ApplyApp:  doApplyApp();  break;  // sets its own error / reboots
            case Cmd::ApplyFs:   doApplyFs();   break;  // sets its own error / reboots
        }
        g_busy.store(false);
    }
}

} // namespace

void initUpdater() {
    if (g_task) return;
    if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
    if (!g_cmdQueue) g_cmdQueue = xQueueCreate(1, sizeof(Cmd));
    if (!g_mutex || !g_cmdQueue) {
        LOG_ERROR(LogTag::OTA, "Updater: failed to create mutex/queue");
        return;
    }
    copyStr(g_status.current, sizeof(g_status.current), FIRMWARE_VERSION);
    BaseType_t ok = xTaskCreatePinnedToCore(
        workerTask, "fw_updater", UPDATER_TASK_STACK_SIZE, nullptr,
        UPDATER_TASK_PRIORITY, &g_task, UPDATER_TASK_CORE);
    if (ok != pdPASS) { g_task = nullptr; LOG_ERROR(LogTag::OTA, "Updater: task create failed"); }
}

bool requestUpdateCheck() {
    if (!g_cmdQueue) return false;
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return false;
    Cmd c = Cmd::Check;
    if (xQueueSend(g_cmdQueue, &c, 0) != pdTRUE) { g_busy.store(false); return false; }
    return true;
}

bool requestUpdate() {
    if (!g_cmdQueue) return false;
    if (!g_target.available) return false;
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return false;
    Cmd c = Cmd::ApplyBoth;
    if (xQueueSend(g_cmdQueue, &c, 0) != pdTRUE) { g_busy.store(false); return false; }
    return true;
}

bool requestAppUpdate() {
    if (!g_cmdQueue) return false;
    if (!g_target.available) return false;
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return false;
    Cmd c = Cmd::ApplyApp;
    if (xQueueSend(g_cmdQueue, &c, 0) != pdTRUE) { g_busy.store(false); return false; }
    return true;
}

bool requestFsUpdate() {
    if (!g_cmdQueue) return false;
    if (!g_target.available || !g_target.hasFs) return false;
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return false;
    Cmd c = Cmd::ApplyFs;
    if (xQueueSend(g_cmdQueue, &c, 0) != pdTRUE) { g_busy.store(false); return false; }
    return true;
}

UpdateStatus updaterStatus() {
    UpdateStatus snap;
    lock();
    snap = g_status;
    unlock();
    return snap;
}

bool updaterInProgress() {
    UpdateStatus s = updaterStatus();
    return s.phase == UpdatePhase::Downloading ||
           s.phase == UpdatePhase::Verifying   ||
           s.phase == UpdatePhase::Flashing     ||
           s.phase == UpdatePhase::Rebooting;
}

} // namespace lume
