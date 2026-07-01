#ifndef LUME_SEGMENT_VIEW_H
#define LUME_SEGMENT_VIEW_H

#include <FastLED.h>
#include "region.h"

namespace lume {

// --- Scratchpad sizing (per segment) ---
// SCRATCHPAD_SIZE  : max bytes an effect may stash in its segment scratchpad.
//   Must hold the largest effect state struct; the static_assert in
//   getScratchpad enforces this per effect at compile time. Currently the
//   ceiling is fire's FireState (heat[600]). At MAX_SEGMENTS=8 the total cost
//   is 8 * SCRATCHPAD_SIZE bytes of RAM.
// SCRATCHPAD_ALIGN : alignment guaranteed for the raw buffer. Effects
//   reinterpret_cast the buffer to their own state struct, so this must be at
//   least as strict as the alignment of any field those structs contain
//   (uint32_t/uint64_t). Defined here, the lowest-level header, so getScratchpad
//   can enforce it at compile time.
constexpr size_t SCRATCHPAD_SIZE  = 640;
constexpr size_t SCRATCHPAD_ALIGN = 8;

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
    CRGB* base;           // Pointer to controller's LED array base
    Region region;        // Which pixels this segment covers (P1.3)
    bool reversed;        // Run effect in reverse direction?
    uint8_t* scratchpad;  // Pointer to segment's scratchpad for stateful effects

    // Default constructor (empty view)
    SegmentView() : base(nullptr), region(), reversed(false), scratchpad(nullptr) {}

    // Construct view from LED array base
    SegmentView(CRGB* ledArray, uint16_t startIdx, uint16_t len, bool rev = false, uint8_t* scratch = nullptr)
        : base(ledArray)
        , region(startIdx, len)
        , reversed(rev)
        , scratchpad(scratch) {}

    // Indexed access - handles reversal transparently
    CRGB& operator[](uint16_t i) {
        uint16_t idx = reversed ? (region.length - 1 - i) : i;
        return base[region.start + idx];
    }

    const CRGB& operator[](uint16_t i) const {
        uint16_t idx = reversed ? (region.length - 1 - i) : i;
        return base[region.start + idx];
    }
    
    // --- FastLED primitive wrappers ---
    // These operate on the raw segment, ignoring reversal
    // (most FastLED operations don't care about direction)
    
    void fill(CRGB color) {
        fill_solid(raw(), region.length, color);
    }

    void fill(CRGB color, uint16_t offset, uint16_t count) {
        if (offset < region.length) {
            uint16_t actualCount = min(count, (uint16_t)(region.length - offset));
            fill_solid(raw() + offset, actualCount, color);
        }
    }

    void clear() {
        fill_solid(raw(), region.length, CRGB::Black);
    }

    void fade(uint8_t amount) {
        fadeToBlackBy(raw(), region.length, amount);
    }

    // Fill with gradient (respects reversal)
    void gradient(CRGB startColor, CRGB endColor) {
        if (reversed) {
            fill_gradient_RGB(raw(), region.length, endColor, startColor);
        } else {
            fill_gradient_RGB(raw(), region.length, startColor, endColor);
        }
    }

    // Fill with rainbow
    void rainbow(uint8_t startHue, uint8_t deltaHue = 5) {
        if (reversed) {
            fill_rainbow(raw(), region.length, startHue + (deltaHue * region.length), -deltaHue);
        } else {
            fill_rainbow(raw(), region.length, startHue, deltaHue);
        }
    }

    // --- Direct access for advanced operations ---

    // Get raw pointer to first LED in segment (for direct FastLED calls)
    CRGB* raw() { return base + region.start; }
    const CRGB* raw() const { return base + region.start; }

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
};

} // namespace lume

#endif // LUME_SEGMENT_VIEW_H
