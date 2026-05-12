#pragma once

#include "app_state.h"
#include "audio.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"
#include "warpmarkers.h"

#include <string>

// X.7.5b: flag-editor cluster, extracted from main.cpp's inline lambdas.
// Covers the top-flag canonical-line editor (V.A1), the V.B iteration
// popup editor, the Brief X.2 BPM popup editor, the Shift+I/Shift+M bulk
// clears, and the BPM-mode enter/exit transitions. clear_hover_popup is
// reached through viewport.
struct GuiFlagEditor {
    AppState&     app;
    GuiAudio&     audio;
    Viewport&     viewport;
    Selection&    selection;
    Undo&         undo;

    GuiFlagEditor(AppState&     app_,
                  GuiAudio&     audio_,
                  Viewport&     viewport_,
                  Selection&    selection_,
                  Undo&         undo_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_) {}

    std::string build_locked_prefix(const GuiWarpMarker& m);
    void exit_top_flag_edit_no_commit();
    void enter_top_flag_edit(int idx, double click_x = -1.0);
    void commit_top_flag_edit();
    void enter_iter_edit(int idx, double click_x = -1.0,
                                  double text_left_x = -1.0);
    void commit_iter_edit();
    void bulk_clear_iter_values();
    void enter_bpm_edit(int idx, double click_x = -1.0,
                                 double text_left_x = -1.0);
    void commit_bpm_edit();
    void bulk_clear_bpm_values();
    void enter_bpm_mode();
    void exit_bpm_mode();
};
