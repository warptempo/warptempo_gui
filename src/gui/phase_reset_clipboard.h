#pragma once

#include "warpmarkers.h"

#include <cstdint>
#include <string>
#include <vector>

// Session-only clipboard for the W-mode phase reset propagate feature
// (Ctrl+P copy / Ctrl+Alt+P paste). A copy captures a sequence of named
// warp blocks and the fractional positions of any phase resets that fall
// inside them. Paste walks a destination anchor's named-block sequence
// in lockstep with the clipboard, materializing phase resets at the
// destination's actual durations. Single-slot, in-memory only — never
// persisted to any sidecar, cleared on app exit.

// `source_frame`, `source_start_frame`, and `source_end_frame` carry absolute source-
// domain geometry so paste_state_apply can apply a boundary-aware count
// using the same N-sample guard on both clipboard and destination sides.
// paste_apply materializes every placement at its fractional_position
// regardless of boundary proximity; with the shared lead-in tolerance
// applied at capture, fractional_position may be slightly negative for a
// lead-in placement and paste_apply 0-clamps the materialized time.
struct ClipboardPlacement {
    // Approximate range (-guard/duration, 1.0). A lead-in reset captured
    // up to the guard before the block start yields a small negative
    // fraction; paste_apply clamps the materialized time to 0.
    double  fractional_position = 0.0;
    int64_t source_frame         = 0;   // absolute capture-time source frames
    bool    disabled            = false;
};

struct ClipboardBlock {
    std::string                     label_name;
    int64_t                         source_start_frame = 0;  // absolute source frames
    int64_t                         source_end_frame   = 0;  // absolute source frames
    std::vector<ClipboardPlacement> placements;
};

class PhaseResetClipboard {
public:
    void set(std::vector<ClipboardBlock> blocks) {
        blocks_ = std::move(blocks);
    }
    void clear()                                      { blocks_.clear(); }
    bool empty() const                                { return blocks_.empty(); }
    const std::vector<ClipboardBlock>& blocks() const { return blocks_; }

private:
    std::vector<ClipboardBlock> blocks_;
};

// ---------------------------------------------------------------------------
// THE PROPAGATE FAMILY'S TWO SHARED WARP-MARKER ACCESSORS. They live in this
// header because it is the one both propagates already include: the PHASE
// RESET propagate (Ctrl+P / Ctrl+Alt+P / Ctrl+Alt+Shift+P, which owns the
// clipboard above) and, since 2026-08-20, the MEASURE propagate (Ctrl+/ and
// Ctrl+Alt+/, whose own clipboard is measure_clipboard.h). Neither re-spells
// either accessor.

// Single accessor that returns a marker's label string regardless of
// whether it's a definition or a reference. Empty when the marker is
// unnamed; block matching at copy/paste time is exact string equality
// on this accessor's return value, on BOTH propagates.
inline const std::string& warp_marker_label_name(const GuiWarpMarker& m) {
    return m.label_def.empty() ? m.label_ref : m.label_def;
}

// THE PROPAGATE WALK'S MEMBERSHIP, one predicate for all four walks that ask
// it (the phase copy's selected-run loop and its destination walk_named_blocks,
// and the measure propagate's two): a marker takes part iff it CARRIES A LABEL
// NAME and is EFFECTIVELY ENABLED.
//
// Both terms are load-bearing and neither is an efficiency filter. The LABEL is
// what the paste's lockstep matches on, so an unlabeled marker has nothing to
// align and is skipped on both sides. EFFECTIVE-DISABLED (the label_ref cascade,
// not the raw bit) is skipped because a disabled marker is dropped before the
// warp map is built — it neither owns a section nor bounds one — so admitting it
// on one side and not the other would open a lockstep gap.
//
// IT IS EXTRACTED RATHER THAN RESTATED because "the two sides must filter
// identically" is the paste's whole correctness premise, and a premise held by
// discipline across four loops is the kind that drifts. `effective_disabled`
// re-scans the store for a disabled def on every label-ref query, so a whole
// walk through here is worst-case O(n^2) — the deliberate choice over a cached
// keep-mask, propagate being a discrete command over tens-to-hundreds of
// markers (the reasoning is stated in full at section_end_index,
// warpmarkers.h).
inline bool warp_marker_propagates(const std::vector<GuiWarpMarker>& mv,
                                   int                              i) {
    if (i < 0 || i >= static_cast<int>(mv.size())) return false;
    if (warp_marker_label_name(mv[static_cast<size_t>(i)]).empty()) return false;
    return !effective_disabled(mv, i);
}
