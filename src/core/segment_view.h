#ifndef LUME_SEGMENT_VIEW_H
#define LUME_SEGMENT_VIEW_H

#include <FastLED.h>       // CRGB / CHSV (effect-facing colour inputs)
#include "region.h"
#include "render16.h"      // CRGB16 — the 16-bit render backing (premium substrate)

namespace lume {

// --- Scratchpad sizing (per segment) ---
// SCRATCHPAD_SIZE  : max bytes an effect may stash in its FIXED per-segment pad.
//   Must hold the largest 1D effect state struct; the static_assert in
//   getScratchpad enforces this per effect at compile time. Currently the
//   ceiling is fire's FireState (heat[600]). At MAX_SEGMENTS=8 the total cost
//   is 8 * SCRATCHPAD_SIZE bytes of RAM.
//   Large / 2D effect state (a 32x32 grid needs >=1024 B) does NOT come from
//   here — paying the worst case x8 segments would be wasteful. Instead one
//   canvas-spanning segment borrows a single shared workbuffer via
//   Segment::attachScratchpad and reads it with view.getScratchpadChecked<T>()
//   (runtime-guarded). See docs/rfcs/0002-scratchpad-strategy.md (P1.5).
// SCRATCHPAD_ALIGN : alignment guaranteed for the raw buffer. Effects
//   reinterpret_cast the buffer to their own state struct, so this must be at
//   least as strict as the alignment of any field those structs contain
//   (uint32_t/uint64_t). Defined here, the lowest-level header, so getScratchpad
//   can enforce it at compile time.
constexpr size_t SCRATCHPAD_SIZE  = 640;
constexpr size_t SCRATCHPAD_ALIGN = 8;

// LUME_WORKBUFFER_SIZE : bytes of the single shared workbuffer a canvas-spanning
//   segment may borrow for large / 2D effect state (P1.5). Default 0 = feature
//   off (a 1D build pays nothing). A matrix build overrides it, e.g.
//   -DLUME_WORKBUFFER_SIZE=2048 for a 32x32 grid. Defined in this low-level
//   header so the effect registry and the controller agree on the ceiling.
#ifndef LUME_WORKBUFFER_SIZE
#define LUME_WORKBUFFER_SIZE 0
#endif

// The largest effect state a segment can EVER hold: the fixed pad, or the shared
// workbuffer if bigger. The registry validates a registered effect against this;
// per-segment setEffect() then enforces the ACTUAL active pad at assignment time.
constexpr size_t MAX_EFFECT_STATE =
    SCRATCHPAD_SIZE > LUME_WORKBUFFER_SIZE ? SCRATCHPAD_SIZE : (size_t)LUME_WORKBUFFER_SIZE;

/**
 * SegmentView - A non-owning view into a CRGB array
 * 
 * This is the core abstraction that effects operate on. It provides:
 * - Bounds-safe access to a contiguous range of LEDs
 * - Automatic reversal handling
 * - Direct access to underlying FastLED primitives
 * 
 * Effects receive a SegmentView and don't know (or care) where
 * their segment is positioned in the overall strip.
 * 
 * Design: Uses base+start+length instead of pointer-to-first-LED to enable:
 * - Future non-contiguous mapping (matrix, serpentine)
 * - Per-segment transforms
 * - Debug bounds checking
 */
struct SegmentView {
    CRGB16* base;         // Pointer to controller's 16-bit canvas base
    Region region;        // Which pixels this segment covers (P1.3)
    bool reversed;        // Run effect in reverse direction?
    uint8_t* scratchpad;  // Pointer to the segment's active scratchpad
    uint16_t scratchpadCapacity;  // Bytes available at `scratchpad` (P1.5)

    // Default constructor (empty view)
    SegmentView()
        : base(nullptr), region(), reversed(false)
        , scratchpad(nullptr), scratchpadCapacity(0) {}

    // Construct view from the 16-bit canvas base. `scratchCap` is the size of the
    // buffer at `scratch`; it defaults to the fixed per-segment pad
    // (SCRATCHPAD_SIZE), but a 2D/large-state segment may point at a bigger
    // borrowed buffer (P1.5).
    SegmentView(CRGB16* canvas, uint16_t startIdx, uint16_t len, bool rev = false,
                uint8_t* scratch = nullptr, uint16_t scratchCap = SCRATCHPAD_SIZE)
        : base(canvas)
        , region(startIdx, len)
        , reversed(rev)
        , scratchpad(scratch)
        , scratchpadCapacity(scratch ? scratchCap : 0) {}

    // Indexed access - handles reversal transparently
    CRGB16& operator[](uint16_t i) {
        uint16_t idx = reversed ? (region.length - 1 - i) : i;
        return base[region.start + idx];
    }

    const CRGB16& operator[](uint16_t i) const {
        uint16_t idx = reversed ? (region.length - 1 - i) : i;
        return base[region.start + idx];
    }
    
    // --- Canvas primitives (P1.4) ---
    // Every primitive writes through operator[], so it is remap-safe: on a strip
    // [i] is contiguous; under a future 2D serpentine/tiled map [i] is the ONLY
    // correct pixel path. They deliberately do not reach for a flat raw() pointer
    // + FastLED whole-buffer call — that was the 1D-only escape hatch that defeated
    // the 2D abstraction, and it has been removed from the effect-facing contract.
    // operator[] already applies reversal, so the primitives address the segment
    // in logical order (index 0 = first pixel the effect sees).

    // Convert an 8-bit effect colour (params, CHSV) to the 16-bit canvas domain.
    static CRGB16 c16(const CRGB& c) { return CRGB16::fromRGB8(c.r, c.g, c.b); }

    void fill(CRGB16 color) {
        for (uint16_t i = 0; i < region.length; i++) (*this)[i] = color;
    }
    void fill(const CRGB& color) { fill(c16(color)); }

    void fill(CRGB16 color, uint16_t offset, uint16_t count) {
        if (offset >= region.length) return;
        uint16_t actualCount = min(count, (uint16_t)(region.length - offset));
        for (uint16_t i = 0; i < actualCount; i++) (*this)[offset + i] = color;
    }
    void fill(const CRGB& color, uint16_t offset, uint16_t count) {
        fill(c16(color), offset, count);
    }

    void clear() {
        for (uint16_t i = 0; i < region.length; i++) (*this)[i] = CRGB16();
    }

    // Fade toward black: amount 0 = unchanged, 255 = black.
    void fade(uint8_t amount) {
        uint8_t keep = 255 - amount;
        for (uint16_t i = 0; i < region.length; i++) (*this)[i] = scale((*this)[i], keep);
    }

    // Logical gradient: index 0 is startColor, index size()-1 is endColor. Because
    // operator[] applies any reversal, we no longer swap the endpoints by hand.
    void gradient(CRGB16 startColor, CRGB16 endColor) {
        if (region.length == 0) return;
        if (region.length == 1) { (*this)[0] = startColor; return; }
        for (uint16_t i = 0; i < region.length; i++) {
            uint16_t frac = (uint16_t)((uint32_t)i * 65535 / (region.length - 1));
            (*this)[i] = blend16(startColor, endColor, frac);
        }
    }
    void gradient(const CRGB& startColor, const CRGB& endColor) {
        gradient(c16(startColor), c16(endColor));
    }

    // Logical rainbow: index 0 is startHue, advancing by deltaHue. operator[]
    // applies any reversal, matching the old wrapper's forward/reverse result.
    void rainbow(uint8_t startHue, uint8_t deltaHue = 5) {
        uint8_t hue = startHue;
        for (uint16_t i = 0; i < region.length; i++) {
            (*this)[i] = c16(CRGB(CHSV(hue, 255, 255)));
            hue += deltaHue;
        }
    }

    // --- Geometry accessors ---

    // Get the pixel region this view covers (P1.3)
    const Region& getRegion() const { return region; }

    // Get segment start index (for position tracking)
    uint16_t getStart() const { return region.start; }

    // Get length
    uint16_t size() const { return region.length; }

    // Check if valid
    bool valid() const { return base != nullptr && !region.empty(); }

    // --- Scratchpad access for stateful effects ---
    
    // Get typed scratchpad pointer.
    // Guards (caught at compile time, for every effect that calls this):
    //  - state must fit in the buffer
    //  - state must not need stronger alignment than the buffer guarantees,
    //    otherwise the reinterpret_cast below yields unaligned word access and
    //    faults on Xtensa/RISC-V at runtime.
    template<typename T>
    T* getScratchpad() {
        static_assert(sizeof(T) <= SCRATCHPAD_SIZE,
                      "Effect state exceeds scratchpad size (SCRATCHPAD_SIZE)");
        static_assert(alignof(T) <= SCRATCHPAD_ALIGN,
                      "Effect state needs stronger alignment than the scratchpad guarantees");
        return reinterpret_cast<T*>(scratchpad);
    }

    template<typename T>
    const T* getScratchpad() const {
        static_assert(sizeof(T) <= SCRATCHPAD_SIZE,
                      "Effect state exceeds scratchpad size (SCRATCHPAD_SIZE)");
        static_assert(alignof(T) <= SCRATCHPAD_ALIGN,
                      "Effect state needs stronger alignment than the scratchpad guarantees");
        return reinterpret_cast<const T*>(scratchpad);
    }

    // Runtime-checked scratchpad access for LARGE / dimension-dependent state
    // (P1.5). Unlike getScratchpad<T>() — whose compile-time guard is fixed to
    // the small per-segment pad (SCRATCHPAD_SIZE) — this validates T against the
    // *actual* capacity of the attached buffer, which may be a bigger borrowed
    // workbuffer. Returns nullptr if the state doesn't fit (or no pad is
    // attached), so a 2D effect degrades gracefully instead of scribbling past
    // the buffer. Alignment is still enforced at compile time.
    template<typename T>
    T* getScratchpadChecked() {
        static_assert(alignof(T) <= SCRATCHPAD_ALIGN,
                      "Effect state needs stronger alignment than the scratchpad guarantees");
        if (!scratchpad || sizeof(T) > scratchpadCapacity) return nullptr;
        return reinterpret_cast<T*>(scratchpad);
    }

    template<typename T>
    const T* getScratchpadChecked() const {
        static_assert(alignof(T) <= SCRATCHPAD_ALIGN,
                      "Effect state needs stronger alignment than the scratchpad guarantees");
        if (!scratchpad || sizeof(T) > scratchpadCapacity) return nullptr;
        return reinterpret_cast<const T*>(scratchpad);
    }
};

} // namespace lume

#endif // LUME_SEGMENT_VIEW_H
