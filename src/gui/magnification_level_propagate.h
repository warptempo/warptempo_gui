#pragma once

#include "notifications.h"
#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"    // the paste's membership replace rides the chokepoint
#include "undo.h"
#include "viewport.h"

#include <set>

class GuiAudio;
struct GuiActiveViews;
struct GuiInputHandler;

// THE MAGNIFICATION LEVEL PROPAGATE (architect 2026-09-15): copy/paste
// operations for the W-mode magnification level propagate — the phase reset
// propagate's three acts (PhaseResetPropagate, phase_reset_propagate.h) over
// the third column, WITH THE FRAME AS ITS OWN ANCHOR. Both pastes operate on
// the warp-marker selection in W-mode and mutate the magnification level
// marker store as a side effect. Mode/selection-count gating is the caller's
// responsibility, except for the empty-clipboard silent no-op which lives
// inside paste_apply.
//
// WHAT MAKES THIS FAMILY DIFFERENT FROM ITS SIBLING, stated once here so no
// body below restates it:
//
//   * NO ANCHOR CONVERSION ANYWHERE. The phase family carries each reset
//     FORWARD to its anchor (its frame plus kPhaseResetLeadInSamples output
//     samples, mapped) and re-derives a reset from a scaled anchor as the T+P
//     drop does — phase_reset_anchor_frame / phase_reset_frame_for_anchor, the
//     ONE delta between the two bodies. A magnification level marker is a
//     PICTURE BOUNDARY at its own frame (its drop takes no lead-in either:
//     drop_magnification_level_at_playhead), so here membership is bucketed by
//     the marker's OWN FRAME, the captured fraction is the frame's, and the
//     paste scales that fraction straight into the destination frame. The
//     boundary guard window is the same constant asked of the same kind of
//     point: the sibling asks it of the anchor, which already IS the marked
//     point, so nothing compensates and nothing needs to.
//
//   * THE STATE IS THE LEVEL AND THE DISABLED BIT. The placement paste copies
//     both onto what it materializes, and the STATE paste aligns both on the
//     destination's existing markers — where the sibling's aligns the disabled
//     bit alone, because a phase reset carries nothing else. (The planner's
//     reading of "mirrored exactly", confirmed by the architect 2026-09-15:
//     state = the marker's whole non-positional content.)
//
//   * IT TAKES NO GuiTargetRender, the authoring cluster's rule
//     (GuiMagnificationLevelMarkersOps, magnificationlevelmarkers_ops.h): a
//     level is display-only, so no act here may dispatch a preview, and the
//     absence of the member is the enforcement. It takes the audio itself
//     instead, for the song end and the sample rate. WHAT IT OWES THE PICTURE
//     it pays through the LANDING: every run that wrote a block ends in
//     land_paste_in_source_view, whose kick_waveform_sync is the UNCONDITIONAL
//     synchronous rebuild — the store's generation re-keys the gain profile
//     (waveform_gain_profile_cached, warp_frame_map_view.h), so that rebuild
//     reads the new gain with no gain-change kick of its own, exactly as the
//     three loads in place and the undo restore do (the inventory is at
//     Viewport::kick_waveform_sync). A run that wrote nothing changed no
//     store and owes nothing.
//
//   * IT LANDS IN S+M, the column's only view (active_column_authoring_allowed's
//     'M' arm; source view since 2026-09-16, target for the column's first
//     day), where the sibling lands in T+P — the same tail with the audio
//     letter swapped, and its membership walk was in source frames all along
//     (walk_named_blocks over the warp store's authored frames, no map in
//     front of it).
//
// Everything else — the run rule, the section bucketing and its guard, the
// lockstep walk, the stop reports, the produced-nothing stillness, the
// byte-equal-still-lands ruling, the selection spend, the landing's order —
// is the sibling's, clause for clause, through the shared owners in
// propagate_blocks.h where the factoring was mechanical and by clone where the
// sibling's loop is interleaved with its anchor.
struct MagnificationLevelPropagate {
    AppState&           app;
    Viewport&           viewport;
    Undo&               undo;
    const GuiAudio&     audio;
    // Owned end-of-paste view switch goes through switch_active_markers_view_to
    // so the column switch's selection clear stays consistent with the digit
    // selectors' path. The two call sites for paste_apply / paste_state_apply
    // live in different files (prompt.cpp / input_key_dispatch.cpp), only one
    // of which holds GuiActiveViews — keeping the dependency here covers both
    // with one wiring.
    GuiActiveViews&     active_views;
    // The paste-confirm prompt is a modal surface; its open stops playback
    // through this lifecycle handle.
    GuiPlaybackLifecycle& playback_lifecycle;
    // The two pastes' "Stopped at …" reports are notification cards; this is
    // the one push chokepoint they reach.
    GuiNotifications&     notifications;
    // THE SELECTION CHOKEPOINT, held for one line: the target-view landing
    // REPLACES the membership with the set the paste created, and a replace
    // must run through a Selection mutator or the shift anchor outlives it
    // (Selection::replace_selection carries the whole reasoning).
    Selection&            selection;

    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this propagate by reference, so the
    // dependency is a pointer set after construction, the sibling's own
    // shape). Reaches switch_active_audio_view_to so a completed paste can
    // land in target view through the SAME chokepoint the digit selectors use.
    GuiInputHandler*      input = nullptr;

    MagnificationLevelPropagate(AppState& app_, Viewport& viewport_, Undo& undo_,
                                const GuiAudio& audio_,
                                GuiActiveViews& active_views_,
                                GuiPlaybackLifecycle& playback_lifecycle_,
                                GuiNotifications& notifications_,
                                Selection& selection_)
        : app(app_), viewport(viewport_), undo(undo_), audio(audio_),
          active_views(active_views_),
          playback_lifecycle(playback_lifecycle_),
          notifications(notifications_), selection(selection_) {}

    // Ctrl+M copy. Caller has already verified W-mode + a CONTIGUOUS run of
    // warp markers selected (the paste walks labeled blocks in strict lockstep,
    // so a gap would misalign the two label sequences — the copy gate mirrors
    // the sibling's and the BPM sweep's contiguity requirement). Section-based:
    // replaces THIS COLUMN'S clipboard with the named blocks each SELECTED,
    // effective-enabled, labeled marker owns — its time to the end of the
    // section it renders (section_end_index, warpmarkers.h) — and, inside
    // each, every magnification level marker whose OWN FRAME falls in the
    // block's guard window, with its level and its disabled bit. Non-mutating
    // beyond clipboard state — no undo entry, no marker changes.
    void copy_from_selection();

    // Stash the anchor on AppState, tag the pending paste as this column's
    // (AppState::pending_paste_column) and raise the confirmation. Caller has
    // verified W-mode + exactly one warp marker selected and a non-empty
    // clipboard.
    void open_paste_confirmation();

    // Materialize the paste against the destination anchor stashed in
    // AppState::pending_paste_anchor. Walks the destination block list in
    // lockstep with the clipboard, stops on the first name divergence, and
    // produces a single undo entry (op_mode 'M') covering all materialized
    // blocks.
    void paste_apply();

    // Ctrl+Alt+Shift+M: propagate only the *state* of clipboard placements —
    // the LEVEL and the disabled bit — onto matching destination magnification
    // level markers, leaving positions untouched. Caller has verified W-mode +
    // exactly one warp marker selected + non-empty clipboard. Walks the
    // destination block list in lockstep with the clipboard, stops at the
    // first label divergence or per-block marker count mismatch, and produces
    // at most one undo entry covering all aligned blocks. A divergence or
    // mismatch is reported as a notification card; a clean or empty run is
    // silent.
    void paste_state_apply();

    // Shared end-of-paste tail: land the completed paste in TARGET view with
    // the M column active and the newly created markers selected. `created` is
    // the exact post-insert index set of the markers this paste materialized
    // (empty for the state-only tail, which rewrites fields on markers that
    // already exist). ONLY A PASTE THAT WROTE A BLOCK REACHES IT, and a paste
    // that PAIRED blocks and left the store byte-equal STILL LANDS — the
    // sibling's two rulings, stated in full at
    // PhaseResetPropagate::land_paste_in_target_view, into source view.
    void land_paste_in_source_view(const std::set<int>& created);
};
