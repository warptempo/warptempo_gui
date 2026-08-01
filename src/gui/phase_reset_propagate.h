#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "undo.h"
#include "viewport.h"

#include <set>

struct GuiTargetRender;
struct GuiActiveViews;
struct GuiInputHandler;

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

    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this propagate by reference, so the
    // dependency is a pointer set after construction, mirroring the settings
    // editor's `input` back-wire). Reaches handle_active_audio_view_toggle so a
    // completed paste can land in target view through the SAME chokepoint the
    // `t` key uses.
    GuiInputHandler*      input = nullptr;

    PhaseResetPropagate(AppState& app_, Viewport& viewport_, Undo& undo_,
                        GuiTargetRender& target_render_,
                        GuiActiveViews& active_views_,
                        GuiPlaybackLifecycle& playback_lifecycle_)
        : app(app_), viewport(viewport_), undo(undo_),
          target_render(target_render_), active_views(active_views_),
          playback_lifecycle(playback_lifecycle_) {}

    // Ctrl+P copy. Caller has already verified W-mode + a CONTIGUOUS run of
    // warp markers selected (the paste walks labeled blocks in strict lockstep,
    // so a gap would misalign the two label sequences — the copy gate mirrors
    // the `m` sweep's contiguity requirement). Section-based (architect
    // 2026-07-23): replaces the clipboard with the named blocks each SELECTED,
    // effective-enabled, labeled marker owns — its time to the next store
    // marker, or to the song end for the store-final marker. Non-mutating
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
