#pragma once

#include "app_state.h"
#include "viewport.h"

#include <set>

class GuiAudio;

// Selection-cluster operations, extracted from main.cpp's inline
// lambdas. The struct holds references to the long-lived state the methods
// read and write; bodies are byte-identical to the originals modulo `this->`
// access on the captured references.
struct Selection {
    AppState&       app;
    const GuiAudio& audio;
    Viewport&       viewport;

    Selection(AppState&       app_,
              const GuiAudio& audio_,
              Viewport&       viewport_)
        : app(app_),
          audio(audio_),
          viewport(viewport_) {}

    void repair_last_selected();
    void set_single_selection(int idx);
    void clear_selection();
    void collapse_to_focused();
    bool toggle_selection_membership(int idx);
    void select_range_from_anchor(int idx);
    void select_contained_in_span(int64_t lo, int64_t hi);
    void sanitize_selection_after_restore(int n);
    void cycle_selection(bool forward);
    void select_next_marker();
    void select_prev_marker();
    void prune_live_selection();

   private:
    // R6 focus model (architect 2026-07-23): the cursor playhead's FORM depends
    // on selection emptiness — empty = breeze-green line + triangle (waveform
    // focus), non-empty = grey STEMLESS triangle (marker-lane focus). A change
    // that CROSSES the emptiness boundary flips whether the 1px waveform line
    // paints and the triangle's color, so the playhead COLUMN must repaint even
    // when the playhead itself does not move. The mutators already emit
    // top-strip/timestamp damage (which covers the triangle lane) but not the
    // waveform-area line, so this adds the playhead-column damage on a flip.
    // `was_empty` is captured BEFORE the mutation; a no-op when emptiness is
    // unchanged (the common case: Tab within a non-empty set, a range shrink).
    void damage_playhead_if_focus_flipped(bool was_empty);

    // R7 (round 3, architect 2026-07-23): the phase-reset lead-in overlay
    // (paint_phase_reset_overlay) is SUPPRESSED while the selection has 2+
    // members, so a selection-size change that CROSSES the 2 threshold flips its
    // visibility. The overlay lives in the WAVEFORM but these mutators damage only
    // the top strip / timestamp (+ the R6 playhead column), so a crossing needs
    // waveform damage to paint/erase the overlay's forward span. It only shows in
    // P + target view, so this gates there and damages the whole plate once on the
    // crossing (bounded, rare). `old_size` is captured BEFORE the mutation; a
    // no-op when the 2 threshold is not crossed.
    void damage_overlay_on_size2_crossing(size_t old_size);
};
