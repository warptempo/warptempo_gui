#pragma once

#include "app_state.h"
#include "audio.h"
#include "selection.h"
#include "text_editor.h"
#include "undo.h"
#include "viewport.h"
#include "warpmarkers.h"

#include <string>

struct GuiTargetRender;

// Flag-editor cluster. Covers the top-flag canonical-line editor, the
// iteration popup editor, the BPM popup editor, and the BPM-mode
// enter/exit transitions. Damage is reached through viewport.
struct GuiFlagEditor {
    AppState&             app;
    GuiAudio&             audio;
    Viewport&             viewport;
    Selection&            selection;
    Undo&                 undo;
    GuiTargetRender&   target_render;

    GuiFlagEditor(AppState&             app_,
                  GuiAudio&             audio_,
                  Viewport&             viewport_,
                  Selection&            selection_,
                  Undo&                 undo_,
                  GuiTargetRender&   target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          target_render(target_render_) {}

    std::string build_locked_prefix(const GuiWarpMarker& m);
    void exit_top_flag_edit_no_commit();
    void enter_top_flag_edit(int idx);
    void commit_top_flag_edit();

    // THE COMMENT EDITOR'S ONE ENTRY (the sixth text_editor Kind). `column` is
    // 'P' for the phase-reset store and anything else for the warp store, and
    // it is the ACTIVE markers view at every call site — comments are the
    // FOURTH ruled exception to the home-view binding, so this opens wherever
    // the flag paints, on either column and in either audio view, and the
    // callers' gates are read-only alone.
    //
    // NOT enter_text_edit: that helper is warp-payload-only by recorded
    // invariant (it bounds-checks the warp store and its comment says why), and
    // the comment editor resolves its index against whichever store the column
    // names. The seed is the marker's own comment field, which is the whole of
    // what a comment is — nothing inherits (architect 2026-08-20).
    void enter_comment_edit(char column, int idx);
    // Commit the open comment session: an EMPTY buffer REMOVES the comment, a
    // non-empty one stores it verbatim. One undo entry when the field actually
    // changed, and the editor closes either way.
    void commit_comment_edit();
    void enter_bpm_edit(int idx);
    // Returns true iff the pending buffer parsed and committed (editor
    // closed). False on parse failure (editor stays open, red) or an
    // invalid target. The caller fires render_bpm_sweep() on true.
    bool commit_bpm_edit();
    void enter_bpm_mode();
    void exit_bpm_mode();
    // Wipe every marker's session-only iter bracket — the single clear
    // every iteration-mode exit route shares (the `i` toggle's turning-off
    // branch, enter_bpm_mode's forced iter-off, and the S->T audio-view
    // toggle). Undo entry when something cleared; callers own the mode flip
    // and repaint.
    void wipe_iter_state();
    // Wipe every marker's session-only bpm state (owner flag, beats,
    // bracket bounds, endpoint). History-less; callers own the repaint.
    void wipe_bpm_state();

  private:
    // Shared core for the "enter editor on idx" flows. The
    // public wrappers handle their kind-specific eligibility gates
    // and seed-text builders, then delegate here for the rest:
    // target-switching (selection + editor reseat), open-selected
    // seeding, top-strip invalidate.
    void enter_text_edit(int idx,
                         text_editor::Kind kind,
                         std::string locked_prefix,
                         std::string initial_pending,
                         bool iter_grammar = false);
};
