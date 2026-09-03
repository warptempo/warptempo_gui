#pragma once

#include "app_state.h"
#include "audio.h"
#include "notifications.h"
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
    // THE RED FLASH'S SECOND READER (architect 2026-08-30, the strictness
    // ruling): every commit refusal in this cluster composes ONE sentence and
    // feeds it to the stderr line AND to a normal card, because a field that
    // turns red says only THAT it refused. The reference sits here rather than
    // in the dispatcher because the reason is composed where the fact lives —
    // the settings editor's own arrangement, and the mirror image of the
    // freeze-adjacent ops clusters, which return their sentences instead
    // (GuiOpRefusal, warpmarkers_ops.h).
    GuiNotifications&     notifications;

    GuiFlagEditor(AppState&             app_,
                  GuiAudio&             audio_,
                  Viewport&             viewport_,
                  Selection&            selection_,
                  Undo&                 undo_,
                  GuiTargetRender&   target_render_,
                  GuiNotifications&     notifications_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          target_render(target_render_),
          notifications(notifications_) {}

    void exit_top_flag_edit_no_commit();
    void enter_top_flag_edit(int idx);
    void commit_top_flag_edit();

    // THE MEASURE EDITOR'S ONE ENTRY (the sixth text_editor Kind). `column` is
    // 'P' for the phase-reset store and anything else for the warp store, and
    // it is the ACTIVE markers view at every call site — measures are the
    // FOURTH ruled exception to the home-view binding, so this opens wherever
    // the flag paints, on either column and in either audio view, and the
    // callers' gates are read-only alone.
    //
    // NOT enter_text_edit: that helper is warp-payload-only by recorded
    // invariant (it bounds-checks the warp store and its comment says why), and
    // the measure editor resolves its index against whichever store the column
    // names. The seed is the marker's own measure field, which is the whole of
    // what a measure is — nothing inherits down the label cascade (architect
    // 2026-08-20).
    void enter_measure_edit(char column, int idx);
    // Commit the open measure session: an EMPTY buffer REMOVES the measure, a
    // non-empty one is JUDGED against the measure grammar (marker_measure.h)
    // and either stored verbatim or REFUSED with the editor left standing,
    // red, and a card saying which rule the token broke. One undo entry when
    // the field actually changed; the editor closes on
    // every path except the refusal.
    void commit_measure_edit();
    void enter_bpm_edit(int idx);
    // Returns true iff the pending buffer parsed and committed (editor
    // closed). False on parse failure (editor stays open, red, and a card
    // names which of the three bpm refusals it was) or an invalid target. The
    // caller fires render_bpm_sweep() on true.
    bool commit_bpm_edit();
    void enter_bpm_mode();
    void exit_bpm_mode();
    // Wipe every marker's session-only iter bracket — the single clear
    // every iteration-mode exit route shares, THREE routes re-greped
    // 2026-09-02: the `i` toggle's turning-off branch, enter_bpm_mode's
    // forced iter-off, and the iteration sweep's success tail (the S->T
    // audio-view toggle left the list 2026-08-07; the load in place is NOT a
    // route since 2026-09-02 — it leaves the mode bit alone, the record at
    // apply_recipe_in_place). Undo entry when something cleared; callers own
    // the mode flip and repaint.
    void wipe_iter_state();
    // Wipe every marker's session-only bpm state (owner flag, beats,
    // bracket bounds, endpoint). History-less; callers own the repaint. TWO
    // CALLERS (re-greped 2026-09-02): exit_bpm_mode, the mode's one
    // off-chokepoint, and apply_recipe_in_place, where it is a statement
    // over a set that already carries defaults.
    void wipe_bpm_state();

  private:
    // Shared core for the "enter editor on idx" flows. The
    // public wrappers handle their kind-specific eligibility gates
    // and seed-text builders, then delegate here for the rest:
    // target-switching (selection + editor reseat), open-selected
    // seeding, top-strip invalidate.
    void enter_text_edit(int idx,
                         text_editor::Kind kind,
                         std::string initial_pending,
                         bool iter_grammar = false);
};
