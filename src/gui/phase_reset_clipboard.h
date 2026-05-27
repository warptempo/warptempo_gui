#pragma once

#include "engine/engine.h"  // Mode
#include "warpmarkers.h"

#include <string>
#include <vector>

// Session-only clipboard for the W-mode phase reset propagate feature
// (Ctrl+P copy / Ctrl+Alt+P paste). A copy captures a sequence of named
// warp blocks and the fractional positions of any phase resets that fall
// inside them. Paste walks a destination anchor's named-block sequence
// in lockstep with the clipboard, materializing phase resets at the
// destination's actual durations. Single-slot, in-memory only — never
// persisted to any sidecar, cleared on app exit.

// `source_time`, `source_start`, and `source_end` carry absolute source-
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
    double fractional_position = 0.0;
    double source_time         = 0.0;  // absolute capture-time seconds
    bool   disabled            = false;
    Mode   mode                = Mode::Pass;   // authored mode, verbatim
};

struct ClipboardBlock {
    std::string                     label_name;
    double                          source_start = 0.0;  // absolute seconds
    double                          source_end   = 0.0;  // absolute seconds
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

// Single accessor that returns a marker's label string regardless of
// whether it's a definition or a reference. Empty when the marker is
// unnamed; block matching at copy/paste time is exact string equality
// on this accessor's return value.
inline const std::string& warp_marker_label_name(const GuiWarpMarker& m) {
    return m.label_def.empty() ? m.label_ref : m.label_def;
}
