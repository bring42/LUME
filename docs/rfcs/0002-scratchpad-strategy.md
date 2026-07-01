# RFC 0002 — Scratchpad strategy for large / 2D effect state

**Status:** Accepted · **Related:** `TECH_DEBT.md` (P1.5, and P1.2/P1.3/P1.4 —
the other 2D pre-reqs), `FUTURE_ARCHITECTURE.md` (normalized-coordinate canvas),
`docs/ARCHITECTURE.md` (Invariant 3 — the scratchpad)

## Summary

Keep the fixed per-segment scratchpad for the common 1D case; add a **single
controller-owned shared workbuffer** that exactly one canvas-spanning segment may
**borrow** for state too large for that pad (2D fire, particle grids). Effects read
the borrowed buffer through a runtime-guarded `getScratchpadChecked<T>()`. The
feature is **off by default** (`LUME_WORKBUFFER_SIZE == 0`), so a 1D strip build
pays nothing; a matrix build sets `-DLUME_WORKBUFFER_SIZE=<bytes>`.

## Problem

Each `Segment` owns a fixed `SCRATCHPAD_SIZE` (640 B) inline pad, cleared on effect
change, accessed via the compile-time-guarded `getScratchpad<T>()`. This is exactly
right for 1D effects — the largest today is fire's `heat[600]`.

2D breaks the budget. A 32×32 grid of one byte per pixel is 1024 B; a particle
system or a 2D fire needs more. Two facts make "just make the pad bigger" the wrong
fix:

1. **The cost is paid `MAX_SEGMENTS`×.** Every segment carries its own pad, so
   bumping `SCRATCHPAD_SIZE` to hold a 2D grid multiplies by 8. A 4 KB 2D state
   would cost 32 KB of always-resident RAM for pads that 1D segments never use.
2. **Only one segment is ever the matrix.** A 2D canvas is realistically a single
   full-canvas segment; the others (if any) are small strip regions. Provisioning
   the worst case on all 8 slots is waste by construction.

Effect scratchpad state is **persistent across frames** (fire's heat diffuses frame
to frame), so a per-frame shared scratch that everyone reuses is not an option — the
big buffer must belong to its segment for as long as the effect runs.

## Options considered

- **A — Dims-gated larger pad.** Bump `SCRATCHPAD_SIZE` (or compile a 2D variant
  with a bigger pad). Simple, keeps the compile-time guard. Rejected: pays worst
  case × `MAX_SEGMENTS`; a large matrix state balloons RAM on every slot.
- **B — Shared workbuffer, borrowed (chosen).** One controller-owned buffer, lent
  to the single canvas-spanning segment. RAM = `8 × small pad + 1 × workbuffer`
  instead of `8 × workbuffer`. Matches the "one matrix" reality. Costs a runtime
  (not compile-time) size check on the borrowed path, and single-owner bookkeeping.
- **C — Heap/PSRAM per matrix segment.** Allocate 2D state on demand. Most flexible,
  but introduces heap in the render subsystem (fragmentation risk) and loses the
  static RAM budget the project values. Deferred — the workbuffer can move to a
  PSRAM-backed allocation later without changing the effect-facing contract.

## Decision

Adopt **B**, a tiered scratchpad:

- **Tier 1 — the fixed inline pad (unchanged).** Every segment keeps its
  `SCRATCHPAD_SIZE` pad and `getScratchpad<T>()` with its compile-time size +
  alignment guards. This remains the path for all 1D effects.
- **Tier 2 — the shared workbuffer (new).** The controller owns one
  `LUME_WORKBUFFER_SIZE`-byte buffer (`SCRATCHPAD_ALIGN`-aligned). A canvas-spanning
  segment borrows it via `controller.borrowWorkbuffer(slot, bytesNeeded)`, which
  points that segment's `SegmentView::scratchpad` at the buffer and records the
  size. `releaseWorkbuffer()` returns it. At most one borrower at a time.

Effects that hold large state read it with `view.getScratchpadChecked<T>()` — a
runtime guard that returns `nullptr` if `T` doesn't fit the *active* pad, so an
effect assigned to a segment that hasn't borrowed degrades gracefully instead of
scribbling past the buffer. Alignment is still checked at compile time.

`Segment::setEffect()` validates the effect's `stateSize` against the **active**
pad (inline or borrowed), and the registry validates a registered effect against
`MAX_EFFECT_STATE = max(SCRATCHPAD_SIZE, LUME_WORKBUFFER_SIZE)` — so a big-state 2D
effect is registrable when a workbuffer exists, yet still can't be assigned to a
segment that lacks the room.

### RAM

| Build | Per-segment pads | Workbuffer | Total scratchpad RAM |
|-------|------------------|-----------|----------------------|
| 1D strip (default, size 0) | 8 × 640 B | 1 B placeholder | ~5 KB (unchanged) |
| Matrix, 2048 B workbuffer | 8 × 640 B | 2048 B | ~7 KB |
| Option A equivalent (2 KB pad ×8) | 8 × 2048 B | — | ~16 KB |

## Mechanism (this RFC's implementation)

Landed as the seam; **no real 2D effect ships yet** (per P1.5: "establish the
mechanism"). Present and unit-tested:

- `SegmentView::scratchpadCapacity` + `getScratchpadChecked<T>()` (runtime guard).
- `Segment::attachScratchpad(buf, cap)` / `detachScratchpad()`, and a
  capacity-aware `setEffect()` reset.
- `LumeController::borrowWorkbuffer()` / `releaseWorkbuffer()` /
  `workbufferCapacity()`, single-owner, capacity-bounded. The borrow is dropped on
  any layout change (`removeSegment`/`clearSegments`) so nothing dangles through the
  copy-by-value slot shift (TECH_DEBT P2); the future 2D setup re-borrows after
  laying out its canvas.
- `MAX_EFFECT_STATE` ceiling relaxed accordingly in the registry.
- `LUME_WORKBUFFER_SIZE` defined in `segment_view.h` (default 0), covered by the
  `test_scratchpad` native suite built with `-DLUME_WORKBUFFER_SIZE=2048`.

## Consequences / follow-ups

- **Who calls `borrowWorkbuffer`?** Nobody in the 1D firmware — it wakes up when a
  2D canvas/config lands. Until then the API is dead-but-tested by design.
- **Multiple matrices** would need a small workbuffer *pool* rather than a single
  owner; out of scope until a second matrix is real.
- **PSRAM.** On ESP32-S3 a large workbuffer should target PSRAM
  (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`); swapping the buffer's backing store
  doesn't touch the effect-facing contract (option C stays open).
- **Copy-by-value segments (P2).** The borrow model sidesteps, but does not fix, the
  pre-existing scratchpad aliasing in `removeSegment`'s slot shift. Making `Segment`
  non-copyable / handle-based (P2) would let a borrow survive a layout change.
