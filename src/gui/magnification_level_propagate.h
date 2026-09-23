#pragma once

#include "notifications.h"
#include "app_state.h"
#include "selection.h"    // the paste's membership replace rides the chokepoint
#include "undo.h"
#include "viewport.h"

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
// press.
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
//     block walk here there is nothing to hook a state onto, so the letter
//     M's third chord, Ctrl+Alt+Shift+M, is unbound.
//
//   * IT TAKES NO GuiTargetRender, the authoring cluster's rule
//     (GuiMagnificationLevelMarkersOps, magnificationlevelmarkers_ops.h): a
//     level is display-only, so no act here may dispatch a preview, and the
//     absence of the member is the enforcement. It takes the audio itself
//     instead, for the song end and for the stop report's timestamp. THE
//     STORE MOVES NO PIXEL of the waveform (the picture's gain is the curve
//     derived from the source, GuiAudio::gain_curve), so what a paste owes
//     the screen is the flag lane: every run that materialized a marker ends
//     in land_paste_in_source_view, whose kick_waveform_sync is the
//     UNCONDITIONAL synchronous rebuild and its flag-cache tail (the
//     inventory is at Viewport::kick_waveform_sync).
//
// WHAT IS THE SIBLING'S, CLAUSE FOR CLAUSE: the selection spend on both acts
// (selection_consumed, past every refusal) and the paste ENDING WITH THE
// CREATED MARKERS SELECTED, the first focused and the playhead landed on it.
//
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
                                Selection& selection_)
        : app(app_), viewport(viewport_), undo(undo_), audio(audio_),
          active_views(active_views_),
          notifications(notifications_), selection(selection_) {}

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

    // The paste's tail: land in
    // SOURCE view with the M column active and the newly created markers
    // selected (the column's only view), the first focused and the playhead
    // landed on it. `created` is the exact post-write index set of the markers
    // the act materialized, and it is never empty — the paste's own assert
    // carries that derivation. The two
    // view switches are NO-OPS ON
    // EVERY ROAD, the act being an S+M act at the press; they stand because
    // this tail is where the paste names the view it ends in, and naming it
    // through the two chokepoints is what keeps that claim true if a road ever
    // reaches here from elsewhere.
    void land_paste_in_source_view(const std::set<int>& created);
};
