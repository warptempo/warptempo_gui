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

// Flag-editor cluster. Covers the marker lane's THREE editors — the flag's
// canonical-line editor (the warp column's alone), the
// iteration bound editor (the warp and phase-reset columns') and the
// magnification level editor (the third column's, 2026-09-15) — the
// BPM dialog editor, and the
// BPM-mode enter/exit transitions. Damage is reached through viewport.
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
    // Keyboard-modal, pointer/wheel-transparent, no playback stop — the
    // top-strip family's recorded exemption. Read-only refuses at the callers,
    // as the flag editor's open does.
    void enter_iter_bound_edit(char column, int idx, MarkerCell side);
    // Commit the open bound session, ON THE LIVE COLUMN (which is the open's
    // column — the view cannot move under an open session): an EMPTY buffer
    // CLEARS THE WHOLE BRACKET (one bound alone is not
    // representable), a non-empty one must parse as that column's grammar —
    // the signed two-decimal cent bound on warp, the signed whole hop on
    // phase — and satisfy the walls, which are the partner bound plus the
    // tempo window on warp and the partner bound plus the reset's HOP WINDOW
    // on phase, else the editor stands, red, and a card says which. On success
    // the pair is written through that column's one write site
    // (iter_bound_step_write / phase_iter_bound_step_write, so two zeroes
    // clear there too) and NOTHING ELSE MOVES: no undo entry, no dirty
    // re-derive, no render — a bracket is outside the undo domain on both
    // columns, so the strip's damage is the whole tail — the editor closing on
    // every path except the refusal.
    void commit_iter_bound_edit();

    // THE MAGNIFICATION LEVEL EDITOR'S ONE ENTRY (the sixth text_editor Kind,
    // architect 2026-09-15), on the MAGNIFICATION LEVEL column alone and in
    // source view, the only view that column exists in (2026-09-16; target
    // for its first day). It is that column's
    // PAYLOAD editor — a magnification level marker's whole authored value is
    // its one digit — so it opens on the payload axis exactly as the warp
    // column's canonical-line editor does, off bare Return and off the flag's
    // double-click. The callers' gate is flag_editor_open_actionable's payload
    // arm (app_state.h); a call with another column active is a silent
    // defensive no-op.
    //
    // The mechanics are the payload editor's open's over the third store: the focus
    // repaired, the marker single-selected and landed, the seed the marker's
    // own level digit fully selected. Keyboard-modal, pointer/wheel-transparent,
    // no playback stop — the top-strip family's recorded exemption. Read-only
    // refuses at the callers, as its two siblings' opens do.
    void enter_magnification_level_edit(int idx);
    // Commit the open level session. THE LEVEL IS REQUIRED — a magnification
    // level marker with no level is not a state the grammar can spell
    // (magnificationlevelmarkers_parse.h) — so unlike the bound editor's commit
    // there is NO EMPTY-CLEARS ARM: an EMPTY buffer and an out-of-grammar one
    // are the same refusal, the editor left standing, red, with a card saying
    // which rule the token broke. One undo entry when the digit actually
    // changed, and the PICTURE's own gain kick with it (the profile this column
    // feeds moves); the editor closes on every path except the refusal.
    void commit_magnification_level_edit();
    void enter_bpm_edit(int idx);
    // Returns true iff the pending buffer parsed and committed (editor
    // closed). False on parse failure (editor stays open, red, and a card
    // names which of the three bpm refusals it was) or an invalid target. The
    // caller fires render_bpm_sweep() on true.
    bool commit_bpm_edit();
    void enter_bpm_mode();
    void exit_bpm_mode();
    // Wipe BOTH stores' session-only iter brackets — the single clear
    // every iteration-mode exit route shares, TWO routes re-greped
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
