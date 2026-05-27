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

// X.7.5b: flag-editor cluster, extracted from main.cpp's inline lambdas.
// Covers the top-flag canonical-line editor (V.A1), the V.B iteration
// popup editor, the Brief X.2 BPM popup editor, the Shift+I/Shift+M bulk
// clears, and the BPM-mode enter/exit transitions. clear_hover_popup is
// reached through viewport.
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
    void enter_top_flag_edit(int idx, double click_x = -1.0);
    void commit_top_flag_edit();
    void bulk_clear_iter_values();
    void enter_bpm_edit(int idx, double click_x = -1.0,
                                 double text_left_x = -1.0);
    // Returns true iff the pending buffer parsed and committed (editor
    // closed). False on parse failure (editor stays open, red) or an
    // invalid target. Brief E: the caller fires render_bpm_sweep() on true.
    bool commit_bpm_edit();
    void enter_bpm_mode();
    void exit_bpm_mode();

    // Phase-reset mode editor (P view). Mirrors enter_top_flag_edit /
    // commit_top_flag_edit but operates on app.phase_reset_markers and the
    // PhaseResetMode editor kind: click a phase-reset flag, type
    // peak/heap/pass, validate on commit, write marker.mode under the
    // phase-reset undo op, then re-render. Entry is gated to P view; the
    // caller (the click site) applies the read-only refusal.
    void enter_phase_reset_mode_edit(int idx);
    void commit_phase_reset_mode_edit();

  private:
    // Shared core for the three "enter editor on idx" flows. The
    // public wrappers handle their kind-specific eligibility gates
    // and seed-text builders, then delegate here for the rest:
    // same-target re-click (cursor recompute), target-switching
    // (selection + playhead + editor reseat), initial cursor
    // positioning, hover-popup clear, top-strip invalidate.
    // A negative `text_left_x` triggers the on-the-fly fallback via
    // flag_pending_text_left_x(app, audio, idx) — used by the top-
    // flag editor whose canonical-line layout is computed dynamically.
    void enter_text_edit(int idx,
                         text_editor::Kind kind,
                         std::string locked_prefix,
                         std::string initial_pending,
                         double click_x,
                         double text_left_x,
                         bool iter_grammar = false);
};
