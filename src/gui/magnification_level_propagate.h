#pragma once

#include "notifications.h"
#include "app_state.h"
#include "magnification_auto_detect.h"   // the generate act's parked breakpoints
#include "playback_lifecycle.h"          // the generate act's modal stop
#include "selection.h"    // the paste's membership replace rides the chokepoint
#include "undo.h"
#include "viewport.h"

#include <optional>
#include <set>
#include <vector>

class GuiAudio;
struct GuiActiveViews;
struct GuiInputHandler;

// THE MAGNIFICATION LEVEL PROPAGATE: copy a group of magnification level
// markers and lay the same shape down again from the playhead. TWO ACTS,
// Ctrl+M and Ctrl+Alt+M, both of them S+M acts — the column exists in source
// view alone (active_column_authoring_allowed's 'M' arm), so its own marker
// view is the whole gate and the audio view is 'S' by construction at every
// press. A THIRD S+M ACT SHARES THE STRUCT since 2026-09-22 — GENERATE
// MAGNIFICATION LEVEL MARKERS on Ctrl+Alt+Shift+M, stated at the end of this
// block — for the landing it reuses, not for any part of the model below.
//
// WHAT IT IS, AND WHY IT IS NOT ITS SIBLING (architect 2026-09-19). The phase
// reset propagate (PhaseResetPropagate, phase_reset_propagate.h) carries a
// LABELED PHRASE'S phase resets from one occurrence of that label to another:
// it buckets by named warp blocks, captures each reset's section fraction,
// walks the destination's blocks in lockstep and re-scales every fraction onto
// the destination section's own duration. THIS FAMILY DOES NONE OF THAT. A
// magnification level pass is done once, at the beginning of a piece and by
// sight, BEFORE any other marker has been laid down, so there is no warp map
// to scale under, no label to match and no section to belong to. What a copy
// captures is THE SHAPE OF THE GROUP — each marker's whole-frame distance from
// the first — and what a paste does is lay that shape down again with its
// first marker ON THE PLAYHEAD:
//
//   * PURE FRAME DISTANCE. Markers at 0, 100 and 200 copied and pasted with
//     the playhead at 1000 land at 1000, 1100 and 1200, whatever the tempo map
//     says anywhere. No map, no section, no fraction, no block, no boundary
//     guard, no label walk — and so no "Stopped at … (label name diverged)"
//     report either, there being nothing to diverge.
//   * A GAPPED SELECTION IS FINE. The copy asks for no consecutive run: under
//     a distance model 0, 100 and 350 have perfectly well-defined offsets and
//     paste as 1000, 1100 and 1350. (The sibling's run gate exists for its
//     lockstep block walk, which does not exist here.)
//   * IT APPLIES AT THE PRESS, with no confirmation prompt: the act is purely
//     additive — it clears nothing and matches nothing — and one undo entry
//     away. (The sibling's paste keeps its confirmation: it clears the
//     destination blocks' existing resets, which is a question worth asking.)
//   * PAST THE SONG END IT PASTES WHAT FITS AND CARDS THE REST (architect
//     2026-09-19). Every placement landing inside [0, total - 1] is
//     materialized; the first one that does not stops the run and is reported
//     on a notification card in the family's own register. A paste where
//     everything fits is silent. THE FIRST PLACEMENT ALWAYS LANDS — its
//     offset is 0 by construction and the anchor is the in-domain playhead —
//     so every paste materializes at least one marker and there is no
//     produced-nothing case to answer (the derivation is at the assert in
//     paste_apply).
//   * A COINCIDENT LANDING SIMPLY PASTES. Nothing refuses and nothing
//     de-duplicates: coincident markers are legal in the store, and a run of
//     2+ ENABLED rows at one frame reads as the neutral level 0
//     (magnificationlevelmarkers.h), which the red cue then says.
//
//   * THERE IS NO STATE PASTE. The sibling's Ctrl+Alt+Shift+P aligns the
//     destination's EXISTING resets to the clipboard, block by block; with no
//     block walk here there is nothing to hook a state onto. The letter M's
//     THIRD CHORD, Ctrl+Alt+Shift+M, is therefore NOT a state paste: since
//     2026-09-22 it is GENERATE MAGNIFICATION LEVEL MARKERS, below.
//
//   * IT TAKES NO GuiTargetRender, the authoring cluster's rule
//     (GuiMagnificationLevelMarkersOps, magnificationlevelmarkers_ops.h): a
//     level is display-only, so no act here may dispatch a preview, and the
//     absence of the member is the enforcement. It takes the audio itself
//     instead, for the song end and for the stop report's timestamp. WHAT IT
//     OWES THE PICTURE it pays through the LANDING: every run that
//     materialized a marker ends in land_paste_in_source_view, whose
//     kick_waveform_sync is the UNCONDITIONAL synchronous rebuild — the
//     store's generation re-keys the gain profile
//     (waveform_gain_profile_cached, warp_frame_map_view.h), so that rebuild
//     reads the new gain with no gain-change kick of its own, exactly as the
//     three loads in place and the undo restore do (the inventory is at
//     Viewport::kick_waveform_sync).
//
// WHAT IS THE SIBLING'S, CLAUSE FOR CLAUSE: the selection spend on both acts
// (selection_consumed, past every refusal) and the paste ENDING WITH THE
// CREATED MARKERS SELECTED, the first focused and the playhead landed on it.
//
// GENERATE MAGNIFICATION LEVEL MARKERS (architect 2026-09-22), Ctrl+Alt+Shift+M
// and the Edit menu's first row, an M-column act like the two above. It LIVES
// HERE rather than in a struct of its own because it needs exactly what this
// one already holds — the audio (for the samples), no GuiTargetRender, and the
// paste's landing, which it reuses rather than clones. THE RULE, whole:
//   * THE ANALYSIS ALWAYS RUNS OVER THE FULL SONG, at the press and
//     synchronously (detect_magnification_levels, magnification_auto_detect.h,
//     the rule's owner), whatever the trim is.
//   * THE REPLACEMENT: every magnification level marker, enabled or disabled,
//     whose frame lies INSIDE THE TRIM is deleted, and exactly the detected
//     breakpoints whose frame lies inside the trim are inserted, all enabled.
//     Nothing else: no patching at the trim's edges and no empty-trim case —
//     if nothing is detected inside, the markers there are simply deleted.
//     Membership is the trim store's own inclusive whole-frame pair
//     (trim_contains_source_frame, app_state.h), so the full window — the old
//     "unset" — replaces the whole column.
//   * IT ASKS FIRST: an OK / Cancel prompt raised on OK ("Replace the N
//     magnification levels in the trim with M generated ones?"), the counts
//     being why the detection runs at the press. The detected in-trim list is
//     PARKED on this struct for the answer and dropped on both close roads.
//   * ON OK: ONE undo entry, op_mode 'M' — or NONE when the column comes out
//     row-equal to what it was (magnification_level_rows_equal), so a re-run
//     that regenerates what is there leaves no invisible entry and no dirty
//     mark. The generated markers END SELECTED through the paste's landing
//     (land_paste_in_source_view); with none generated the selection rests
//     empty and the playhead stays. The selection is spent. No card: the
//     prompt said what would happen and the flags show it.
struct MagnificationLevelPropagate {
    AppState&           app;
    Viewport&           viewport;
    Undo&               undo;
    const GuiAudio&     audio;
    // Owned end-of-paste view switch goes through switch_active_markers_view_to
    // so the column switch's selection clear stays consistent with the view
    // selectors' path. The paste's one call site (input_key_dispatch.cpp) does
    // not hold GuiActiveViews, so the dependency lives here.
    GuiActiveViews&     active_views;
    // The paste's "Stopped at …" report is a notification card; this is the one
    // push chokepoint it reaches.
    GuiNotifications&     notifications;
    // THE SELECTION CHOKEPOINT, held for one line: the source-view landing
    // REPLACES the membership with the set the paste created, and a replace
    // must run through a Selection mutator or the shift anchor outlives it
    // (Selection::replace_selection carries the whole reasoning).
    Selection&            selection;
    // THE GENERATE ACT'S MODAL STOP, held for one line: raising its
    // confirmation is a modal open, and a modal open stops playback through
    // the shared body (stop_playback_for_modal_open). The copy and the paste
    // raise nothing and never reach it.
    GuiPlaybackLifecycle& playback_lifecycle;

    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this propagate by reference, so the
    // dependency is a pointer set after construction, the sibling's own
    // shape). Reaches switch_active_audio_view_to so the landing can name its
    // audio view through the SAME chokepoint the view selectors use.
    GuiInputHandler*      input = nullptr;

    MagnificationLevelPropagate(AppState& app_, Viewport& viewport_, Undo& undo_,
                                const GuiAudio& audio_,
                                GuiActiveViews& active_views_,
                                GuiNotifications& notifications_,
                                Selection& selection_,
                                GuiPlaybackLifecycle& playback_lifecycle_)
        : app(app_), viewport(viewport_), undo(undo_), audio(audio_),
          active_views(active_views_),
          notifications(notifications_), selection(selection_),
          playback_lifecycle(playback_lifecycle_) {}

    // Ctrl+M copy. Caller has already verified the M column and a non-empty
    // selection — the act's two gates, both carded there. Replaces THIS
    // COLUMN'S clipboard with one placement per SELECTED magnification level
    // marker, in ascending frame order: its whole-frame offset from the first
    // selected marker, its level and its disabled bit. Non-mutating beyond
    // clipboard state — no undo entry, no marker changes.
    void copy_from_selection();

    // Ctrl+Alt+M paste. Caller has already verified the M column and a
    // non-empty clipboard. Materializes one marker per placement at the
    // playhead's source frame plus that placement's offset, stopping at the
    // first landing past the song end and carding it, and producing ONE undo
    // entry (op_mode 'M') for the run.
    void paste_apply();

    // The paste's tail, and since 2026-09-22 the generate act's: land in
    // SOURCE view with the M column active and the newly created markers
    // selected (the column's only view), the first focused and the playhead
    // landed on it. `created` is the exact post-write index set of the markers
    // the act materialized, and it is never empty — the paste's own assert
    // carries that derivation, and generate_apply calls it only when it
    // generated something. The two
    // view switches are NO-OPS ON
    // EVERY ROAD, the act being an S+M act at the press; they stand because
    // this tail is where the paste names the view it ends in, and naming it
    // through the two chokepoints is what keeps that claim true if a road ever
    // reaches here from elsewhere.
    void land_paste_in_source_view(const std::set<int>& created);

    // Ctrl+Alt+Shift+M, the press. Caller has already verified the M column
    // (carded there). Runs the detector over the full song, parks the
    // breakpoints inside the trim, stops playback and raises the OK / Cancel
    // confirmation (DialogTrigger::GENERATE_MAGNIFICATION_CONFIRM) naming how
    // many markers would be deleted and how many inserted.
    void open_generate_confirmation();

    // The confirmation's OK (GuiPrompt::activate_response, the prompt already
    // down): applies the replacement from the parked breakpoints and clears
    // them. A no-op when nothing is parked.
    void generate_apply();

    // The confirmation's Cancel: drops the parked breakpoints. Nothing else
    // was written at the press, so there is nothing else to undo.
    void cancel_generate_confirmation() { pending_generated_.reset(); }

private:
    // THE PARKED ANSWER: the detected breakpoints inside the trim, set only
    // while the generate confirmation stands (the modal prompt holds the store
    // still under it) and cleared on both of its close roads, so it can never
    // reach a later answer. EMPTY is a real answer — nothing detected inside —
    // which is why "nothing parked" is the optional's own empty state.
    std::optional<std::vector<GuiDetectedMagnificationLevel>> pending_generated_;
};
