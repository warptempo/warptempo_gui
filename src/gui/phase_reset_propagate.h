#pragma once

#include "app_state.h"
#include "undo.h"
#include "viewport.h"

struct GuiTargetRender;
struct GuiActiveViews;

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
    // so the W↔P selection swap + hover-popup clear + live-selection prune
    // stay consistent with the keyboard `p`-toggle path. The two call sites
    // for paste_apply / paste_state_apply live in different files
    // (prompt.cpp / input_handler.cpp), only one of which holds
    // GuiActiveViews — keeping the dependency here covers both with one
    // wiring.
    GuiActiveViews&     active_views;

    PhaseResetPropagate(AppState& app_, Viewport& viewport_, Undo& undo_,
                        GuiTargetRender& target_render_,
                        GuiActiveViews& active_views_)
        : app(app_), viewport(viewport_), undo(undo_),
          target_render(target_render_), active_views(active_views_) {}

    // Ctrl+P copy. Caller has already verified W-mode + exactly two
    // warp markers selected. Replaces the clipboard with the named
    // blocks and fractional placements derived from the half-open
    // [first, last) source range. Non-mutating beyond clipboard
    // state — no undo entry, no marker changes.
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
};
