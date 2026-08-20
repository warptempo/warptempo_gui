#pragma once

#include <string>
#include <utility>
#include <vector>

// SESSION-ONLY CLIPBOARD FOR THE MEASURE PROPAGATE (architect 2026-08-20):
// Ctrl+/ copies, Ctrl+Alt+/ pastes under a signed measure offset. Single-slot,
// in-memory only — never persisted to any sidecar, cleared on app exit — the
// phase reset clipboard's shape and lifetime exactly (phase_reset_clipboard.h).
//
// A copy captures, IN STORE ORDER, one entry per SELECTED warp marker that
// takes part in a propagate walk (`warp_marker_propagates`,
// phase_reset_clipboard.h — the ONE membership predicate both propagates
// share). The paste walks the destination's own members from the anchor in
// lockstep with these entries, matching BY LABEL and stopping whole at the
// first divergence, which is the phase propagate's rule reused rather than
// re-decided.
//
// IT IS WARP-COLUMN ONLY, AND THAT IS A RULING RATHER THAN AN OMISSION
// (architect 2026-08-20) — recorded here because the naming-symmetry rule wants
// every one-column surface to say so at its own site. PHASE RESET MARKERS DO
// CARRY MEASURES (the field is homed on both serialized bases), and they are
// edited, saved and loaded exactly as warp measures are; what they do not have
// is a PROPAGATE. The asymmetry follows the labels: propagate matches
// destinations by label name, and only the warp column has labels — a phase
// reset line is a frame plus a disable bit, with nothing to align a lockstep
// walk on. A phase-column measure propagate would need a different matching
// rule, not a mirrored one, so there is no symmetric surface being skipped.

// One captured marker: the label it matched on, and the measure it carried.
//
// `has_measure` is NOT `measure_text.empty()` folded away, because the paste
// treats the two states differently and the distinction has to survive the
// clipboard: a captured marker WITHOUT a measure pastes as a CLEAR of the
// destination's measure (the copy captured "none", so the paste writes none),
// while a captured measure pastes as a value. Collapsing them would make a
// clear indistinguishable from a skip.
struct MeasureClipboardEntry {
    std::string label_name;
    bool        has_measure = false;
    std::string measure_text;
};

class MeasureClipboard {
public:
    void set(std::vector<MeasureClipboardEntry> entries) {
        entries_ = std::move(entries);
    }
    void clear()                                          { entries_.clear(); }
    bool empty() const                             { return entries_.empty(); }
    const std::vector<MeasureClipboardEntry>& entries() const {
        return entries_;
    }

private:
    std::vector<MeasureClipboardEntry> entries_;
};
