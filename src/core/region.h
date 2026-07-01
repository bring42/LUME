#ifndef LUME_REGION_H
#define LUME_REGION_H

#include <stdint.h>

namespace lume {

/**
 * Region - the set of canvas pixels a segment covers (TECH_DEBT P1.3)
 *
 * Today a Region is a 1D half-open interval [start, start+length) — a "Range".
 * Tomorrow it becomes a Rect for 2D matrices; that is the whole point of naming
 * it. `Segment` used to fuse three concerns into a bare `start + length` pair
 * (physical layout, logical canvas, and this region); pulling the region out
 * into one shared value type means coverage, persistence, and the view all speak
 * it, so growing to 2D changes those sites in one place instead of touching every
 * `start + length` arithmetic scattered across the tree.
 *
 * Region is PURE GEOMETRY. Pixel storage (the CRGB base pointer) and render
 * orientation (`reversed`) deliberately live in SegmentView, not here: a Region
 * describes *which* pixels are covered, not how they're stored or which way an
 * effect scans them. That keeps it directly promotable to a Rect without dragging
 * per-axis flip state in before 2D needs it.
 */
struct Region {
    uint16_t start;   // index of the first pixel
    uint16_t length;  // number of pixels covered

    Region() : start(0), length(0) {}
    Region(uint16_t startIdx, uint16_t len) : start(startIdx), length(len) {}

    // Pixel count.
    uint16_t size() const { return length; }

    // One past the last pixel (exclusive upper bound), e.g. for `i < end()` loops.
    uint16_t end() const { return start + length; }

    // Inclusive last pixel index. Guards the empty case so we never underflow to
    // 65535 (an empty region reports its own start rather than start-1).
    uint16_t stop() const { return length ? (start + length - 1) : start; }

    bool empty() const { return length == 0; }

    // Membership test — the coverage primitive (clearUncoveredLeds): is pixel `i`
    // inside this region? Half-open, so [start, start+length).
    bool contains(uint16_t i) const { return i >= start && i < start + length; }
};

} // namespace lume

#endif // LUME_REGION_H
