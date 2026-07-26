#pragma once

#include "app_state.h"
#include "viewport.h"

#include <optional>
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
    // Focus a marker WITHOUT changing membership: set it as last_selected and
    // dissolve the shift-range anchor (the selection-mutator anchor rule),
    // damaging the top strip + timestamp for the lane-text run / readout. The
    // group marker drag uses this at the threshold crossing (begin_drag) so the
    // grabbed marker becomes the focus while the whole selection stays selected.
    // Membership is untouched, so no focus-emptiness flip — but the FOCUS moving
    // can change the overlay subject, so it owns that repaint (a no-op for
    // today's 2+-selected callers, where the overlay is suppressed either way).
    void focus_without_collapse(int idx);
    void clear_selection();
    void collapse_to_focused();
    bool toggle_selection_membership(int idx);
    void select_range_from_anchor(int idx);
    void sanitize_selection_after_restore(int n);
    void cycle_selection(bool forward);
    void select_next_marker();
    void select_prev_marker();
    void prune_live_selection();

   private:
    // Focus model (architect 2026-07-23, blue-focus pivot 2026-07-25): the cursor
    // playhead's presence depends on selection emptiness — empty = Breeze blue line
    // + triangle at the cursor (waveform focus); non-empty conceptually moves the
    // cursor COINCIDENT with the selected marker, so its line coincides with the
    // marker's blue stem and its triangle sits hidden BEHIND the flag (the z-order
    // flip) — the cursor form is not painted here because the stem IS it (a
    // singleton) / the extent-region wash is its spread (a group).
    // A change that CROSSES the emptiness boundary makes the blue cursor
    // line+triangle appear or disappear, so the playhead COLUMN must repaint even
    // when the playhead itself does not move. The mutators already emit
    // top-strip/timestamp damage (which covers the triangle lane) but not the
    // waveform-area line, so this adds the playhead-column damage on a flip.
    // `was_empty` is captured BEFORE the mutation; a no-op when emptiness is
    // unchanged (the common case: Tab within a non-empty set, a range shrink).
    void damage_playhead_if_focus_flipped(bool was_empty);

    // The phase-reset lead-in overlay (paint_phase_reset_overlay, architect
    // 2026-07-23) annotates the ONE focused enabled reset. Its SUBJECT — the
    // frame it paints at, or none — is the selection-state portion of that
    // paint's visibility rule (P + target view, selection under the 2-member
    // suppression, no active region, a valid enabled focused reset). The overlay
    // lives in the WAVEFORM but these mutators damage only the top strip /
    // timestamp (+ the playhead column), so a change of subject needs
    // waveform damage to paint/erase the overlay's forward span. Frame, not
    // index: a reorder remap preserves frames, so a remap is subject-stable.
    std::optional<int64_t> phase_overlay_subject() const;

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

    // The selected-marker STEM (paint_selected_stem, the blue-focus pivot 2026-07-25)
    // is the SINGLETON selection's always-on focus visual (BOTH columns, BOTH audio
    // views — unlike the phase overlay's P+target gate). Its SUBJECT is the one
    // selected marker's ACTIVE-COLUMN SOURCE frame, or none when the selection is
    // empty or multi (a group's cue is the extent-region wash, not a stem). Frame,
    // not index: a reorder remap preserves frames (subject-stable), exactly like
    // phase_overlay_subject. Captured BEFORE a selection mutation.
    std::optional<int64_t> stem_subject() const;

    // Damage the stem's OLD and NEW subject columns when the subject changed across
    // a mutation (`old_subject` captured before via stem_subject()); a no-op when
    // unchanged (the common case: a Tab within a non-empty set, a range shrink to
    // still-2+). Narrow displayed-basis column damage (Viewport::invalidate_stem_column,
    // the low-level displayed-item invalidator) — the stem is a 1px column, so
    // full-area would be wasteful. This is the ONE owner of the stem's
    // appear/move/disappear under always-on (mirroring the phase-overlay owner):
    // the FRAME/IMAGE-moving gestures (nudges, drags, re-warps) already full-damage
    // the waveform. Wired at the SAME selection mutators the overlay owner is;
    // where a route ALSO full-damages (undo restore, load/adopt, W/P, S/T) the
    // narrow stem damage is harmlessly redundant.
    void damage_stem_on_subject_change(std::optional<int64_t> old_subject);
};
