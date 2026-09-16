#pragma once

#include "phase_reset_clipboard.h"   // warp_marker_propagates / warp_marker_label_name
#include "warpmarkers.h"             // section_end_frame

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// THE PROPAGATE FAMILY'S SHARED WALK — the parts of the phase reset propagate
// that the magnification level propagate takes verbatim (2026-09-15), factored
// here so the two families measure blocks and windows by ONE spelling. What is
// here is MECHANICAL and column-blind: the block type, the destination walk,
// the boundary guard and the membership window. What is NOT here is anything
// the anchor conversion is interleaved with — the phase family's capture,
// clear and bucketing loops ask the window OF THE ANCHOR
// (phase_reset_anchor_frame, phase_reset_propagate.cpp) while the
// magnification family asks it OF THE MARKER'S OWN FRAME, and each family
// keeps those loops in its own body.

// Boundary guard for near-end bucketing. The CONSTANT stays a seconds value
// (an authoring tolerance — the largest the user ever nudges a destination
// marker off its true section boundary, ~2-10 ms typically, never more than
// ~92 ms — deliberately NOT tied to the engine N/window size, so a
// render-setting change cannot shift which section a marker counts toward);
// each use converts it once to frames (guard * sample_rate) because block
// extents and marker positions are whole int64 source frames, widened into the
// double guard-window arithmetic. A marker whose tested point falls within the
// guard before a section end (or before a section start) counts toward the
// next chronological labeled section by shifting every block's membership
// window backward by this amount. ONE window across all six propagate acts:
// each family's copy, paste and state paste.
//
// THE POINT THE WINDOW IS ASKED OF IS EACH FAMILY'S OWN: the phase reset
// propagate asks it of a reset's ANCHOR (architect 2026-09-11 — a reset aimed
// at a marker has its anchor on that marker, so the drop's lead-in never needs
// the guard, and what the guard still covers is the hand nudge), the
// magnification level propagate of the marker's OWN FRAME (architect
// 2026-09-15 — a level is a picture boundary at its own frame, so the frame is
// its anchor and the same tolerance covers the same hand nudge with no
// conversion in front of it). The constant is UNCHANGED across the two.
constexpr double kPropagateBoundaryGuardSeconds = 0.100;

// THE TWO PASTES' SHARED "NOTHING HAPPENED" SENTENCE (architect 2026-08-30, the
// strictness ruling; kept 2026-08-31 when the switch under it left), one
// literal for both families since 2026-09-15. Its readers are each placement
// paste's matched==0 arm (where the destination produced no owned block) and
// each state paste's pair_count==0 arm. A CLEAN PARTIAL WALK is not this: it
// pasted what it had, and a success says nothing.
//
// THE SENTENCE OUTLIVED ITS ORIGINAL ARGUMENT AND STANDS ON ITS OWN. It was
// raised because both pastes ended on the ALWAYS-SWITCH to target view — a
// change of scene that looks exactly like a paste, so a run that paired no
// block had to deny it in words. On 2026-08-31 the architect removed the
// switch from every produced-nothing path instead (the misleading half of the
// success rule: a success cards when what shows would mislead, which is why
// that switch retires rather than the card). The card is KEPT because a paste
// that wrote nothing shows nothing at all now — the view stays exactly where
// it stood — and the words need no scene change to make sense.
inline constexpr const char* kNothingMatched =
    "Nothing matched, so nothing was pasted";

// One named block resolved from a warp-marker walk. `label` is the owning
// marker's label name (empty markers don't produce entries); `start` is the
// owning marker's own absolute source frame and `end` is its section's extent
// under the EFFECTIVE-PARTICIPATION rule stated at section_end_index
// (warpmarkers.h) — the next marker that participates in the render, else the
// song end. A disabled marker sitting in between is not a boundary and does
// not close the block. Both the copies' SOURCE blocks and the pastes'
// DESTINATION blocks are this type: each side's window follows its own extent.
struct PropagateBlock {
    std::string label;
    int64_t     start;
    int64_t     end;
};

// THE MEMBERSHIP WINDOW OF ONE BLOCK, [lo, hi), the guard already converted to
// frames: the window shifts back by the guard at both ends, EXCEPT that a
// SONG-END block (its extent ends at `song_end_frame`) keeps the shifted LOWER
// bound (a marker a hand nudge left just short of the final owner still
// belongs to it) and uses the UNSHIFTED upper bound. The end guard exists to
// reassign the tail to the NEXT section's owner; at song end there is no next
// owner, so the guard would orphan the tail instead — the final block owns its
// section through the last frame. Detected by extent-end == song_end_frame,
// exact and unique: an interior block ends at the next EFFECTIVELY-ENABLED
// marker's time (section_end_frame's rule), and that is still some marker's
// authored time, which walls at total-1 < total = song_end_frame. hi is
// clamped to >= lo so a pathologically tiny block produces an EMPTY window
// (count 0), never an inverted one.
//
// Each side follows its OWN extent: a clipboard block captured at song end may
// pair with a non-final destination and vice versa, so a caller asks this of
// the source block and of the destination block separately.
inline std::pair<double, double> propagate_membership_window(
    int64_t start, int64_t end, double guard_frames, int64_t song_end_frame) {
    const double lo = static_cast<double>(start) - guard_frames;
    const double hi = (end == song_end_frame)
                          ? static_cast<double>(end)
                          : std::max(lo, static_cast<double>(end) - guard_frames);
    return {lo, hi};
}

// Walk the warp marker list across [from_idx, to_idx_exclusive), returning
// the named blocks in order. A block's extent is section_end_frame
// (warpmarkers.h): its owning marker's time to the next EFFECTIVELY-ENABLED
// marker's time, or to the SONG END (song_end_frame, source frames) when no
// enabled marker follows — so the store-final enabled marker owns the section
// running to the song end, and so does a marker trailed only by disabled ones
// (section rule, architect 2026-07-23). Markers without a label name, and
// EFFECTIVELY-DISABLED labeled markers, contribute no block: each copy filters
// effective-disabled selected markers out of its clipboard, so this destination
// walk must filter them identically or a disabled labeled marker opens a
// lockstep gap. Ownership and EXTENT are filtered the same way — a disabled
// marker neither owns a block nor ends one, so no span is left ownerless
// between two enabled markers.
//
// THE PASTE-DESTINATION WALK, for both families. The copies are NOT routed
// through it: a copy filters on the SELECTED set as well as on membership,
// which this walk does not express — each copy keeps its own selected-run loop
// and takes section_end_frame, the SAME extent, so the destination blocks are
// measured exactly as the clipboard's were, which is what the lockstep match
// depends on.
inline std::vector<PropagateBlock> walk_named_blocks(
    const std::vector<GuiWarpMarker>& mv,
    int from_idx, int to_idx_exclusive, int64_t song_end_frame) {
    std::vector<PropagateBlock> out;
    const int n = static_cast<int>(mv.size());
    if (from_idx < 0)        from_idx = 0;
    if (to_idx_exclusive > n) to_idx_exclusive = n;
    for (int i = from_idx; i < to_idx_exclusive; ++i) {
        // The propagate family's ONE membership predicate
        // (warp_marker_propagates, phase_reset_clipboard.h): labeled AND
        // effectively enabled. An effective-disabled labeled marker is not a
        // block owner, and not a boundary either — section_end_frame walks
        // past it (warpmarkers.h states the rule).
        if (!warp_marker_propagates(mv, i)) continue;
        const std::string& name = warp_marker_label_name(mv[i]);
        const int64_t start = mv[i].time_frame;
        const int64_t end   = section_end_frame(mv, i, song_end_frame);
        out.push_back(PropagateBlock{name, start, end});
    }
    return out;
}
