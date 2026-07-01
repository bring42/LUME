#ifndef LUME_BODY_GUARD_H
#define LUME_BODY_GUARD_H

#include <cstdint>

namespace lume {

// Single-owner guard for chunked async request-body assembly (TECH_DEBT P0.3).
//
// AsyncTCP runs every handler's body callback on one task and interleaves the
// chunks of concurrent requests. The per-handler `static String` accumulators
// are only safe if a single body is assembled at a time — otherwise two POSTs
// corrupt each other's buffer. This is the pure state machine behind the
// beginBody()/endBody() helpers; it is header-only and host-testable.
//
// A claim carries a timestamp so a client that disconnects mid-body (AsyncTCP
// never delivers the final chunk, so end() is never called) self-heals after a
// timeout instead of wedging the slot forever. The slot is only ever held during
// body accumulation (milliseconds for the small JSON bodies here), so the
// timeout can be short.
class BodyGuard {
public:
    static constexpr uint32_t kTimeoutMs = 5000;

    // Claim the slot for `token` at time `nowMs`. Returns true if granted: the
    // slot is free, already owned by this token (re-entry across chunks), or the
    // prior owner's claim has gone stale.
    bool begin(const void* token, uint32_t nowMs) {
        if (owner_ == nullptr || owner_ == token ||
            (nowMs - claimedMs_) >= kTimeoutMs) {
            owner_ = token;
            claimedMs_ = nowMs;
            return true;
        }
        return false;
    }

    // Release the slot if `token` owns it (no-op otherwise).
    void end(const void* token) {
        if (owner_ == token) owner_ = nullptr;
    }

private:
    const void* owner_ = nullptr;
    uint32_t    claimedMs_ = 0;
};

} // namespace lume

#endif // LUME_BODY_GUARD_H
