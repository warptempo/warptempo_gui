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
// enter/exit transitions. clear_hover_popup is reached through viewport.
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
    // seeding, hover-popup clear, top-strip invalidate.
    void enter_text_edit(int idx,
                         text_editor::Kind kind,
                         std::string locked_prefix,
                         std::string initial_pending,
                         bool iter_grammar = false);
};
