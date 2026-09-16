#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "warpmarkers_ops.h"   // GuiOpRefusal, the shared refusal-reason
                               // channel (its contract is stated there)
#include "selection.h"
#include "undo.h"
#include "viewport.h"

class GuiAudio;

// MAGNIFICATION LEVEL AUTHORING CLUSTER (architect 2026-09-15), the THIRD
// COLUMN'S ops — the phase-reset cluster's shape over this column's store, with
// the warp payload's VALUE vocabulary on the level digit and the phase column's
// POSITIONAL vocabulary on the frame.
//
// WHAT MAKES THIS CLUSTER DIFFERENT FROM ITS TWO SIBLINGS, stated once here so
// no body below restates it:
//
//   * IT TAKES NO GuiTargetRender AT ALL. A magnification level is DISPLAY-ONLY
//     — no sample, no engine input, no render fingerprint field reads it
//     (magnificationlevelmarkers_parse.h) — so no act in this file may reach
//     GuiTargetRender::trigger, whose write is unconditional (`is_dirty_` and
//     the dirty generation) and would dispatch a preview for a picture change.
//     The absence of the member is the enforcement: there is nothing here to
//     call. Its two siblings take one and trigger on every write.
//
//   * THE PICTURE IS WHAT IT OWES INSTEAD. Every write here moves the WAVEFORM
//     GAIN PROFILE (build_waveform_gain_profile, magnificationlevelmarkers.h),
//     which is memoized on this store's generation and keys the plate
//     fingerprint and the overview bar cache by FIELD. So each body captures
//     Viewport::waveform_gain_hash() BEFORE its store write and hands it to
//     kick_waveform_sync_if_gain_changed AFTER, and the new gain lands in the
//     frame its edit does rather than a tick late on the async backstop. A
//     write the picture cannot see — a drop copying the level already in force,
//     a nudge inside one section, any write at a zoom coarser than working —
//     moves no hash and renders nothing, which is that owner's own rule.
//
//   * THE COLUMN AUTHORS IN TARGET VIEW, the only view it exists in
//     (active_column_authoring_allowed's 'M' arm, app_state.h). Its positions
//     are authored SOURCE frames like every other column's, so the nudge's
//     painted-column step runs against a mapped domain exactly as the
//     phase-reset twin's does in its own target home.
//
// Damage and viewport mutation are reached through viewport;
// stop_playback_if_playing through playback_lifecycle.
struct GuiMagnificationLevelMarkersOps {
    AppState&             app;
    const GuiAudio&       audio;
    Viewport&             viewport;
    Selection&            selection;
    Undo&                 undo;
    GuiPlaybackLifecycle& playback_lifecycle;

    GuiMagnificationLevelMarkersOps(AppState&             app_,
                                    const GuiAudio&       audio_,
                                    Viewport&             viewport_,
                                    Selection&            selection_,
                                    Undo&                 undo_,
                                    GuiPlaybackLifecycle& playback_lifecycle_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          playback_lifecycle(playback_lifecycle_) {}

    // Drop a magnification level marker at `time_frame`, the phase-reset
    // drop's body in this column's terms: the position through
    // snap_authored_frame, the shared [0, total-1] marker EOF wall, a
    // single-select of the new row, one undo entry with the drop's identity
    // hint, and the playhead seated on what was created.
    //
    // ITS LEVEL IS THE LEVEL ALREADY IN FORCE AT THAT FRAME
    // (magnification_level_in_force, magnificationlevelmarkers.h — the gain
    // profile's own step value read at one point), so A DROP CHANGES THE
    // PICTURE NOWHERE: the new marker restates the level its section already
    // carried, and the picture moves only when that level is stepped or
    // edited. Coincident drops are legal, as they are on both other columns.
    void drop_magnification_level_at_position(double time_frame);
    // The drop at the playhead — the column's one create body, reached by bare
    // `s` in T+M and by Ctrl+Shift+S's crossing. NO LEAD-IN OF ANY KIND: the
    // frame IS the anchor (a level is a picture boundary, not a synthesis
    // event), so unlike the phase column's drop this takes no audio-view fork
    // and subtracts nothing — the playhead's own instant, inverse-mapped to a
    // source frame.
    void drop_magnification_level_at_playhead();
    void delete_selected_magnification_levels();
    void toggle_magnification_level_disabled();
    // `synthesized_repeat` is the dispatching key event's platform repeat bit,
    // read only by the undo-coalesce verdict (undo.h). Returns the refusal's
    // own sentence for the dispatcher to card, or std::nullopt for "nothing to
    // say" (GuiOpRefusal, warpmarkers_ops.h). `step_columns` is the press's
    // signed PAINTED-COLUMN count (±1 bare, ±3 shifted, ±10 with ctrl — the
    // step ladder, one owner at arrow_step_magnitude in gui_input.h), the two
    // twins' own parameter.
    GuiOpRefusal nudge_selected_magnification_levels(int step_columns,
                                                     bool synthesized_repeat);
    // THE VALUE STEP ON THIS COLUMN — bare Up/Down (and the plain wheel over an
    // M flag) stepping the LEVEL DIGIT by `delta` through the same arrow
    // ladder, clamped into [0, kMarkerMagnificationMax] silently.
    //
    // SINGLETON AND GROUP, AND THE GROUP ARM IS THE TEMPO STEP'S
    // ALL-OR-NOTHING SCAN (architect 2026-09-16, retiring the per-member clamp
    // this body shipped with on 2026-09-15): a selection holding a marker at
    // the bracket's end refuses the WHOLE press toward that end, before any
    // level changes, and says so on a card — one press is one act on the
    // selection, and stepping some members while others stand would pool them
    // against each other, which is exactly what GROUP RIGIDITY refuses. A
    // singleton keeps the silent clamp, the tempo singleton's own. A DISABLED
    // MARKER STEPS, as it does under the tempo step and the value drag.
    //
    // ONE UNDO ENTRY PER BURST under GestureKind::MagnificationLevelStep, the
    // hybrid's rules; BOTH WALLS ARE ASKED AHEAD OF THE COALESCE STAMP through
    // the pair's own owners (magnification_level_step_group_actionable and the
    // directional face that reads it, app_state.h), and the press SPENDS THE
    // SELECTION on every accepted branch (selection_consumed) — a refusing exit
    // spends nothing.
    // Both locks refuse — the level is serialized content and the step pushes.
    // No re-warp, no re-land, no render and no playback stop (the value step's
    // class, the keyboard stop rule at stop_playback_if_playing).
    GuiOpRefusal adjust_magnification_level_step(int64_t delta,
                                                 bool synthesized_repeat);
};
