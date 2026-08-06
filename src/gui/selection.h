#pragma once

#include "app_state.h"
#include "viewport.h"

#include <optional>

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
    void sanitize_selection_after_restore(int n);
    void cycle_selection(bool forward);
    void select_next_marker();
    void select_prev_marker();

    // The phase-reset lead-in overlay (phase_reset_overlay_band, architect
    // 2026-07-23) annotates the ONE focused enabled reset. Its SUBJECT — the
    // frame it paints at, or none — is the SELECTION-STATE portion of that
    // paint's visibility rule (P + target view, selection under the 2-member
    // suppression, a valid enabled focused reset). The band's
    // geometry gates (area size, samples-per-pixel, sub-pixel forward width,
    // offscreen refusal) are deliberately NOT here: they are not selection
    // state, and neither is the band's `h` HISTORY VIEW suppression (2026-08-05
    // — the view paints no live marker surface, a display fact), which is why
    // Space's lead-in still launches in there. Frame, not index: a reorder remap
    // preserves frames, so a remap is
    // subject-stable. ONE DELIBERATE DIVERGENCE from the band during a live
    // phase-reset drag: the band reads the drag-proposed time
    // (paint_handler.cpp), while this mirror reads the STORE time — the
    // drag's damage is a full-area invalidate, so the mirror's readers never
    // need the proposed value mid-drag.
    // THREE READERS. Two are the damage owner just below (called at every
    // selection mutator): the overlay lives in the WAVEFORM but the selection
    // mutators damage only the top strip / timestamp, so a change of subject
    // needs waveform damage to paint/erase the overlay's forward span. The third is SPACE
    // (input_handler.cpp, architect 2026-07-28): a start-edge Space with a
    // subject launches the lead-in audition at cursor + kN/2. That reader is why
    // this is public, and why the STATE-ONLY spelling is load-bearing — keying
    // Space on the painted band instead would let a scroll or a zoom silently
    // change what Space does.
    std::optional<int64_t> phase_overlay_subject() const;

   private:
    // Damage the waveform when the overlay subject changed across a mutation.
    // `old_subject` is captured BEFORE the mutation via phase_overlay_subject();
    // a no-op when the subject is unchanged (the common case). This is the
    // explicit owner of the overlay's appear/disappear and focus-move repaints:
    // before C-A (architect 2026-07-24) the stem cache's fp_selection_hash
    // rebuilt on every selection change and its tail damaged the whole strip, so
    // the overlay rode that accident; the C-A cut removed it, leaving this owner.
    // It SUBSUMES the old size-2-only crossing helper (a 1<->2 crossing is a
    // subject change) and additionally covers the 0<->1 focus swaps and the
    // focus moving between resets at different frames.
    void damage_overlay_on_subject_change(std::optional<int64_t> old_subject);

    // THE STEM'S SUBJECT PAIR IS DELETED (row 5, 2026-08-01). stem_subject() /
    // damage_stem_on_subject_change() were the phase-overlay owner's twin for the
    // selected-marker stem, because that stem was a SELECTION visual that could
    // appear, move or vanish with no other repaint. Stems are
    // selection-independent now — every enabled marker stems, always, in its
    // class's unselected colour — so there is no selection-driven stem
    // transition left to own. The overlay owner above stands.
};