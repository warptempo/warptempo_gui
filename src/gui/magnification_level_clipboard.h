#pragma once

#include <cstdint>
#include <utility>
#include <vector>

// Session-only clipboard for the MAGNIFICATION LEVEL propagate (Ctrl+M copy /
// Ctrl+Alt+M paste): a FLAT ORDERED LIST OF WHOLE-FRAME OFFSETS with the state
// each one carries, and nothing else. In memory only — never persisted to any
// sidecar, cleared on app exit. ITS OWN SLOT beside the phase reset
// clipboard's (AppState): a copy of one column never overwrites the other's.
//
// IT SHARES NO TYPE WITH THE PHASE RESET CLIPBOARD, and the asymmetry is the
// ruling rather than an accident (architect 2026-09-19; the co-equal-axes rule
// asks that a divergence between the two columns be recorded at the site, and
// this is the record). The phase reset clipboard holds a sequence of NAMED
// WARP BLOCKS and the section fractions of the resets inside them, because
// that paste re-scales those fractions onto a destination section's own
// duration — it carries a phrase's phase resets from one occurrence of a label
// to another. THIS COLUMN CARRIES NO BLOCK, NO LABEL, NO FRACTION AND NO WARP
// MARKER AT ALL: a magnification level pass is laid down once, at the start of
// a piece and by sight, before any other marker exists, so what a copy means
// here is THE SHAPE OF A GROUP OF BOUNDARIES — the distances between them —
// and what a paste means is that shape laid down again from the playhead.
struct MagnificationLevelClipboardPlacement {
    // Whole source frames from the FIRST captured marker: the first
    // placement's offset is 0 by construction and every later one is >= 0,
    // the capture walking the selection in ascending frame order. AN
    // AUTHORED DISTANCE — the difference of two authored frames — so it never
    // leaves the integer domain and the paste's landing needs no rounding of
    // its own.
    int64_t offset_frames = 0;
    // THE STATE CARRIED WITH THE POSITION: the level the marker sets from its
    // frame on, and its disabled bit — the whole of a magnification level
    // marker's non-positional content, copied onto what the paste
    // materializes.
    uint8_t level         = 0;
    bool    disabled      = false;
};

// One ordered list, ascending by offset, replaced whole by each copy.
class MagnificationLevelClipboard {
public:
    void set(std::vector<MagnificationLevelClipboardPlacement> placements) {
        placements_ = std::move(placements);
    }
    bool empty() const { return placements_.empty(); }
    const std::vector<MagnificationLevelClipboardPlacement>& placements() const {
        return placements_;
    }

private:
    std::vector<MagnificationLevelClipboardPlacement> placements_;
};
