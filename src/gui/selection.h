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
    // state. Frame, not index: a reorder remap preserves frames, so a remap is
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

    // The selected-marker STEM (paint_selected_stem, architect 2026-07-25) is
    // the SINGLETON selection's always-on focus visual (BOTH columns, BOTH
    // audio views — unlike the phase overlay's P+target gate). Its SUBJECT is
    // the one selected marker's ACTIVE-COLUMN SOURCE frame, or none when the
    // selection is empty or multi (a group's cue is the members' ink triangles
    // plus the cursor landed on the focus — never a stem). Frame, not index: a reorder remap preserves frames
    // (subject-stable), exactly like phase_overlay_subject. Captured BEFORE a
    // selection mutation.
    std::optional<int64_t> stem_subject() const;

    // Damage the stem's OLD and NEW subject columns when the subject changed across
    // a mutation (`old_subject` captured before via stem_subject()); a no-op when
    // unchanged (the common case: a Tab within a non-empty set, a range shrink to
    // still-2+). FULL waveform-area damage since 2026-07-30 (the narrow
    // per-column invalidator it used rode the wrong coordinate epoch and is
    // deleted; the reason is at the definition). This is the ONE owner of the stem's
    // appear/move/disappear under always-on (mirroring the phase-overlay owner):
    // the FRAME/IMAGE-moving gestures (nudges, drags, re-warps) already full-damage
    // the waveform. Wired at the SAME selection mutators the overlay owner is;
    // where a route ALSO full-damages (undo restore, load/adopt, W/P, S/T) the
    // narrow stem damage is harmlessly redundant.
    void damage_stem_on_subject_change(std::optional<int64_t> old_subject);
};
