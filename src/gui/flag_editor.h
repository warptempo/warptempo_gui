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

// Flag-editor cluster. Covers the marker lane's TWO editors — the flag's
// canonical-line editor (the warp column's alone) and the
// iteration bound editor (the warp and phase-reset columns') — the
// BPM form's field commit and its RENDER gate (architect 2026-10-29), and the
// BPM-mode enter/exit transitions. Damage is reached through viewport.
//
// AN OPEN IS WINDOWS' EDIT FIELD (architect 2026-10-09, every chrome): the
// marker-lane editors paint a box in the chrome's field pair with a
// one-quantum outline in the field text over the edited segment, the
// selected substring in the chrome's selected pair — the picture is
// render_flag_editor_box's (render.h's EDITING paragraph owns the look and
// the ruling's reason).
struct GuiFlagEditor {
    AppState&             app;
    GuiAudio&             audio;
    Viewport&             viewport;
    Selection&            selection;
    Undo&                 undo;
    GuiTargetRender&   target_render;
    // THE REFUSAL'S REASON (architect 2026-08-30, the strictness ruling;
    // 2026-10-03, the red frame retired): every commit refusal in this cluster
    // composes ONE sentence and feeds it to the stderr line AND to a normal
    // card, because the refused field itself only selects its whole text
    // (text_editor::refuse) and says nothing of why — the card is the
    // refusal's one statement. The reference sits here rather than
    // in the dispatcher because the reason is composed where the fact lives —
    // the settings editor's own arrangement, and the mirror image of the
    // freeze-adjacent ops clusters, which return their sentences instead
    // (GuiOpRefusal, warpmarkers_ops.h).
    GuiNotifications&     notifications;
    // THE PAYLOAD COMMIT'S STOP (architect 2026-10-14): a commit that changes
    // the marker is a marker mutation and stops a live play (the keyboard stop
    // rule at stop_playback_if_playing's declaration). Its one reader is
    // commit_top_flag_edit; the opens keep their recorded exemption, and the
    // bound commit and the BPM form's field commit write session-only state
    // outside the class.
    GuiPlaybackLifecycle& playback_lifecycle;

    GuiFlagEditor(AppState&             app_,
                  GuiAudio&             audio_,
                  Viewport&             viewport_,
                  Selection&            selection_,
                  Undo&                 undo_,
                  GuiTargetRender&   target_render_,
                  GuiNotifications&     notifications_,
                  GuiPlaybackLifecycle& playback_lifecycle_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          target_render(target_render_),
          notifications(notifications_),
          playback_lifecycle(playback_lifecycle_) {}

    void exit_top_flag_edit_no_commit();
    // THE FLAG EDITOR'S OPEN: the marker's plain canonical payload — tempo,
    // scale, labels, the disabled bit — fully selected. It carries no
    // bracket: a bound is authored in its own cell's editor below.
    void enter_top_flag_edit(int idx);
    void commit_top_flag_edit();

    // THE ITERATION BOUND EDITOR'S ONE ENTRY (the seventh text_editor Kind,
    // architect 2026-09-05: "each should be like a mini flag with its own
    // double-click"). `column` is 'P' for the phase-reset store and anything
    // else for the warp store, and it is the ACTIVE markers view at every call
    // site, the bound editor being BOTH COLUMNS' since 2026-09-09. `side` is Lower or Upper, the cell the editor
    // opens over, and the open takes the cell's own eligibility (that column's
    // sweep predicate under a lit mode: no cell, no editor), refusing on the
    // bound step's kind sentence where the marker carries no live bracket. The
    // mechanics are the payload editor's open's, over two stores: the focus repaired,
    // the marker single-selected and landed, the addressed cell written to
    // `side` behind that select, the seed the cell's own token (`+0.00` or
    // `+0` on a blank bracket, the column deciding) fully selected.
    // A modal (keyboard, pointer, wheel and touch since 2026-10-25), no
    // playback stop at the open — the lane editors' recorded exemption. Read-only refuses at the callers,
    // as the flag editor's open does.
    void enter_iter_bound_edit(char column, int idx, MarkerCell side);
    // Commit the open bound session, ON THE LIVE COLUMN (which is the open's
    // column — the view cannot move under an open session): an EMPTY buffer
    // CLEARS THE WHOLE BRACKET (one bound alone is not
    // representable), a non-empty one must parse as that column's grammar —
    // the signed two-decimal cent bound on warp, the signed whole hop on
    // phase — and satisfy the walls, which are the partner bound plus the
    // tempo window on warp and the partner bound plus the reset's HOP WINDOW
    // on phase, else the editor stands, its text selected, and a card says which (an
    // editor-owned refusal: a bracket is session state no parser reads, its
    // backstops the sweep's inverted-bracket breach arm). On success
    // the pair is written through that column's one write site
    // (iter_bound_step_write / phase_iter_bound_step_write, so two zeroes
    // clear there too) and NOTHING ELSE MOVES: no undo entry, no dirty
    // re-derive, no render — a bracket is outside the undo domain on both
    // columns, so the strip's damage is the whole tail — the editor closing on
    // every path except the refusal.
    void commit_iter_bound_edit();

    // THE BPM FORM'S FIELD COMMIT (architect 2026-10-29): the dialog line
    // editor's pending line on a BpmField session (AppState::settings_editor,
    // `target` the field, GuiBpmField) judged and written onto the BPM
    // owner's field. The line must begin with THE ROW'S OWN KEY and its `=`
    // (kBpmFieldKeys); an EMPTY value CLEARS the field (zero, its unset
    // sentinel); any other value is judged by THE FIELD'S OWN READER
    // (read_bpm_beats, read_bpm_tempo — warpmarkers.h). A refusal leaves the
    // editor standing, its line selected whole, one sentence on stderr and
    // the card — three classes: the key, the beats grammar, the tempo
    // grammar (the low / high order is the RENDER key's grey, not a field's
    // refusal). On success the field is written — session-only, no undo, no
    // dirty, as every bpm value — and the editor closes; its route's close
    // hook returns to the form (handle_settings_editor_key).
    void commit_bpm_field();
    // THE BPM FORM'S RENDER GATE (the derived-value bracket gate, at the
    // RENDER press since 2026-10-29): with the three fields set and in order
    // (the key's face, bpm_form_button_enabled), true iff the sweep's two
    // ends derive in bracket — the base tempo and scale
    // (compute_base_tempo_scale) and every rescaled marker outside the span
    // (bpm_cell_warp_markers). False cards which wall refused and changes
    // nothing: the form stands. A refusal is not a no-op, so the key does not
    // grey for it.
    bool bpm_sweep_bracket_admits();
    void enter_bpm_mode();
    void exit_bpm_mode();
    // Wipe BOTH stores' session-only iter brackets AND THEIR TIES
    // (2026-09-19, a tie being the bracket's own kind of state) — the single
    // clear every iteration-mode exit route shares, TWO routes re-greped
    // 2026-09-10: the `i` toggle's turning-off branch and the iteration
    // sweep's success tail (the S->T
    // audio-view toggle left the list 2026-08-07; enter_bpm_mode's forced
    // iter-off left it 2026-09-10, when bare `m` stopped being an exit and
    // became a refusal; the load in place is NOT a
    // route since 2026-09-02 — it leaves the mode bit alone, the record at
    // apply_recipe_in_place). It also puts an addressed BOUND cell back on
    // the payload, the cells going with the mode. HISTORY-LESS since
    // 2026-09-10: the clear pushes nothing at all, the bracket having left the
    // undo domain whole, so a wipe is final; callers own the mode flip and
    // repaint.
    void wipe_iter_state();
    // Wipe every marker's session-only bpm state (owner flag, beats,
    // bracket bounds, endpoint). History-less; callers own the repaint. TWO
    // CALLERS (re-greped 2026-09-02): exit_bpm_mode, the mode's one
    // off-chokepoint, and apply_recipe_in_place, where it is a statement
    // over a set that already carries defaults.
    void wipe_bpm_state();

  private:
    // The phase-reset arm of commit_iter_bound_edit above, split out so the
    // two columns' walls and grammars each read straight through. Called with
    // the session's target, side and pending; every belt, refusal and tail is
    // the warp arm's in this column's terms.
    void commit_phase_iter_bound_edit(int idx, MarkerCell side,
                                      const std::string& next);
    // Shared core for the "enter editor on idx" flows. The
    // public wrappers handle their kind-specific eligibility gates
    // and seed-text builders, then delegate here for the rest:
    // target-switching (selection + editor reseat), open-selected
    // seeding, top-strip invalidate.
    void enter_text_edit(int idx,
                         text_editor::Kind kind,
                         std::string initial_pending);
};
