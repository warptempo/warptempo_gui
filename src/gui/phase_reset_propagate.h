#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"    // the paste's membership replace rides the chokepoint
#include "undo.h"
#include "viewport.h"

#include <set>
#include <string>

struct GuiTargetRender;
struct GuiActiveViews;
struct GuiInputHandler;

// THE PROPAGATE FAMILY'S STOP-MESSAGE TIMESTAMP, shared by BOTH propagates
// since 2026-08-20 (it was file-local to phase_reset_propagate.cpp until the
// measure propagate became a second caller). Formats a stop message's timestamp
// in whichever audio domain the user is currently in. The input is a
// SOURCE-frame value (warp markers, clipboard blocks and dest blocks all live
// in whole source frames, widened into this double parameter); the timestamp is
// the display rendering, format_timestamp(frame / sr). In source view:
// identity, labeled " source time". In target view: forward-translate to the
// active domain via source_frame_to_active_domain, labeled " target time". A
// degenerate (empty / failed-build) target-view map translates as identity, so
// the timestamp reads through unchanged but stays labeled " target time",
// consistent with the identity fallback the rest of the target-view paint uses
// on an empty map.
//
// ONE SPELLING IS THE POINT: both propagates report a lockstep divergence in
// the same register ("Stopped at <timestamp> (label name diverged)"), and a
// second implementation would let the two drift in the one place the user
// compares them.
std::string format_domain_timestamp(double source_frame, const AppState& app,
                                    const GuiAudio& audio);

// Copy/paste operations for the W-mode phase reset propagate feature.
// Both methods operate on warp-marker selection in W-mode and mutate
// the phase reset list as a side effect (paste only). Mode/selection-
// count gating is the caller's responsibility, except for the
// empty-clipboard silent no-op which lives inside paste_apply.

struct PhaseResetPropagate {
    AppState&           app;
    Viewport&           viewport;
    Undo&               undo;
    GuiTargetRender& target_render;
    // Owned end-of-paste view switch goes through switch_active_markers_view_to
    // so the column switch's selection clear stays consistent
    // with the keyboard `p`-toggle path. The two call sites
    // for paste_apply / paste_state_apply live in different files
    // (prompt.cpp / input_handler.cpp), only one of which holds
    // GuiActiveViews — keeping the dependency here covers both with one
    // wiring.
    GuiActiveViews&     active_views;
    // The paste-confirm prompt is a modal surface; its open stops playback
    // through this lifecycle handle.
    GuiPlaybackLifecycle& playback_lifecycle;
    // THE SELECTION CHOKEPOINT, held for one line (2026-08-29): the target-view
    // landing REPLACES the membership with the set the paste created, and a
    // replace must run through a Selection mutator or the sticky ctrl and the
    // shift anchor outlive it (Selection::replace_selection carries the whole
    // reasoning; the contract is at AppState::add_to_selection). Every other
    // selection effect on this path is already the column switch's own clear.
    Selection&            selection;

    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this propagate by reference, so the
    // dependency is a pointer set after construction, mirroring the settings
    // editor's `input` back-wire). Reaches switch_active_audio_view_to so a
    // completed paste can land in target view through the SAME chokepoint the
    // `t` key uses.
    GuiInputHandler*      input = nullptr;

    PhaseResetPropagate(AppState& app_, Viewport& viewport_, Undo& undo_,
                        GuiTargetRender& target_render_,
                        GuiActiveViews& active_views_,
                        GuiPlaybackLifecycle& playback_lifecycle_,
                        Selection& selection_)
        : app(app_), viewport(viewport_), undo(undo_),
          target_render(target_render_), active_views(active_views_),
          playback_lifecycle(playback_lifecycle_), selection(selection_) {}

    // Ctrl+P copy. Caller has already verified W-mode + a CONTIGUOUS run of
    // warp markers selected (the paste walks labeled blocks in strict lockstep,
    // so a gap would misalign the two label sequences — the copy gate mirrors
    // the `m` sweep's contiguity requirement). Section-based (architect
    // 2026-07-23): replaces the clipboard with the named blocks each SELECTED,
    // effective-enabled, labeled marker owns — its time to the end of the
    // section it renders, which is the next EFFECTIVELY-ENABLED marker's time
    // or the song end when none follows (a disabled marker is dropped before
    // the warp map is built, so it bounds nothing; the extent rule is stated in
    // full at section_end_index, warpmarkers.h). Non-mutating
    // beyond clipboard state — no undo entry, no marker changes.
    void copy_from_selection();

    // Build the named-block list for the paste confirmation prompt
    // and stash the anchor on AppState. Caller has verified W-mode +
    // exactly one warp marker selected and a non-empty clipboard.
    void open_paste_confirmation();

    // Materialize the paste against the destination anchor stashed in
    // AppState::pending_paste_anchor. Walks the destination block list
    // in lockstep with the clipboard, stops on the first name divergence,
    // and produces a single undo entry covering all materialized blocks.
    void paste_apply();

    // Ctrl+Alt+Shift+P: propagate only the disabled/enabled *state* of
    // clipboard placements onto matching destination phase resets,
    // leaving positions untouched. Caller has verified W-mode + exactly
    // one warp marker selected + non-empty clipboard. Walks the
    // destination block list in lockstep with the clipboard, stops at
    // the first label divergence or per-block phase-reset count
    // mismatch, and produces at most one undo entry covering all
    // aligned blocks. A divergence/mismatch is reported via
    // AppState::transient_status_message; a clean or empty run is
    // silent.
    void paste_state_apply();

    // Shared end-of-paste tail for all three paste actions: land the completed
    // paste in TARGET view (phase reset's home) with the newly created resets
    // selected. `created` is the exact post-insert index set of the resets this
    // paste materialized (empty for the no-materialize / state-only tails).
    void land_paste_in_target_view(const std::set<int>& created);
};
