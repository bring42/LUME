#ifndef LUME_SEGMENT_VIEW_H
#define LUME_SEGMENT_VIEW_H

#include <FastLED.h>

namespace lume {

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
    uint16_t start;       // First LED index in segment
    uint16_t length;      // Number of LEDs in this segment
    bool reversed;        // Run effect in reverse direction?
    
    // Default constructor (empty view)
    SegmentView() : base(nullptr), start(0), length(0), reversed(false) {}
    
    // Construct view from LED array base
    SegmentView(CRGB* ledArray, uint16_t startIdx, uint16_t len, bool rev = false)
        : base(ledArray)
        , start(startIdx)
        , length(len)
        , reversed(rev) {}
    
    // Indexed access - handles reversal transparently
    CRGB& operator[](uint16_t i) {
        uint16_t idx = reversed ? (length - 1 - i) : i;
        return base[start + idx];
    }
    
    const CRGB& operator[](uint16_t i) const {
        uint16_t idx = reversed ? (length - 1 - i) : i;
        return base[start + idx];
    }
    
    // --- FastLED primitive wrappers ---
    // These operate on the raw segment, ignoring reversal
    // (most FastLED operations don't care about direction)
    
    void fill(CRGB color) {
        fill_solid(raw(), length, color);
    }
    
    void fill(CRGB color, uint16_t offset, uint16_t count) {
        if (offset < length) {
            uint16_t actualCount = min(count, (uint16_t)(length - offset));
            fill_solid(raw() + offset, actualCount, color);
        }
    }
    
    void clear() {
        fill_solid(raw(), length, CRGB::Black);
    }
    
    void fade(uint8_t amount) {
        fadeToBlackBy(raw(), length, amount);
    }
    
    void blur(uint8_t amount) {
        blur1d(raw(), length, amount);
    }
    
    void blend(CRGB* source, fract8 amount) {
        nblend(raw(), source, length, amount);
    }
    
    // Fill with gradient (respects reversal)
    void gradient(CRGB startColor, CRGB endColor) {
        if (reversed) {
            fill_gradient_RGB(raw(), length, endColor, startColor);
        } else {
            fill_gradient_RGB(raw(), length, startColor, endColor);
        }
    }
    
    // Fill with rainbow
    void rainbow(uint8_t startHue, uint8_t deltaHue = 5) {
        if (reversed) {
            fill_rainbow(raw(), length, startHue + (deltaHue * length), -deltaHue);
        } else {
            fill_rainbow(raw(), length, startHue, deltaHue);
        }
    }
    
    // Fill from palette
    void fillFromPalette(const CRGBPalette16& palette, uint8_t startIndex, 
                         uint8_t incIndex = 1, TBlendType blendType = LINEARBLEND) {
        uint8_t index = startIndex;
        for (uint16_t i = 0; i < length; i++) {
            (*this)[i] = ColorFromPalette(palette, index, 255, blendType);
            index += incIndex;
        }
    }
    
    // --- Direct access for advanced operations ---
    
    // Get raw pointer to first LED in segment (for direct FastLED calls)
    CRGB* raw() { return base + start; }
    const CRGB* raw() const { return base + start; }
    
    // Get segment start index (for position tracking)
    uint16_t getStart() const { return start; }
    
    // Get length
    uint16_t size() const { return length; }
    
    // Check if valid
    bool valid() const { return base != nullptr && length > 0; }
    
    // Map a normalized position (0.0-1.0) to LED index
    uint16_t map(float normalized) const {
        return (uint16_t)(normalized * (length - 1));
    }
    
    // Map an 8-bit position (0-255) to LED index
    uint16_t map8(uint8_t pos) const {
        return scale16by8(length - 1, pos);
    }
};

} // namespace lume

#endif // LUME_SEGMENT_VIEW_H
