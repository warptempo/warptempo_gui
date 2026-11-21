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
// AN OPEN IS GTK'S IN-PLACE CELL EDITOR (a field since architect
// 2026-10-09; Arc's look 2026-11-18 / 19, send Z's design, built in send
// AA): the marker-lane editors paint a square box in the entry's fill with
// a one-W border of the accent over the edited segment, the selected
// substring in the chrome's selected pair — the picture is
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
    // THE OPENS' STOP AND THE CHANGED WRITES' STOP. Every editor's open stops
    // play through the modal-open stop (architect 2026-11-20, send EE: "play
    // should stop on editor open. i don't use that workflow and never have";
    // enter_text_edit and enter_iter_bound_edit, the decision table at
    // stop_playback_for_modal_open's declaration); a write that changes a
    // marker is a marker mutation and stops a live play (architect
    // 2026-10-14, the keyboard stop rule at stop_playback_if_playing's
    // declaration: commit_top_flag_edit and the label paste,
    // paste_payload_onto_selection). The bound commit and the BPM form's
    // field commit write session-only state outside the class.
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

    // THE LABEL PASTE — THE EDIT KEY'S HOLD, Shift+Return (architect
    // 2026-11-10: "a long press on EDIT replaces the whole label on every
    // selected marker with the system clipboard's text, without opening the
    // editor, through the editor's own validation"). THE LABEL IS THE FLAG'S
    // WHOLE LINE, the payload the editor above opens on — tempo, scale,
    // labels — and never the position or the disabled bit, which stay each
    // marker's own. For every member of the live selection, in the store's
    // order, `clipboard_text` replaces that line as select-all, paste and OK
    // in the marker's own editor would: through the one incoming filter and
    // the kind's byte cap (text_editor::replace_selection on a scratch
    // session) and through the commit's one judgment and write (the body
    // commit_top_flag_edit runs), each marker judged against the store as
    // the paste's earlier markers left it. ALL OR NOTHING — the first
    // refusal cards (kPasteTooLongCard, or the commit's "Edit rejected: …")
    // and nothing is written; an empty `clipboard_text` is a silent nothing;
    // a paste that moves no serialized field writes nothing and pushes
    // nothing. Otherwise ONE UNDO ENTRY, the changed commit's stop and tail;
    // the selection, the focus and the playhead stand. No editor opens.
    // THE CALLER OWNS THE GATES (the Shift+Return arm, input_handler.cpp:
    // both locks at the lock's gate, label_paste_actionable past it) and the
    // clipboard read.
    void paste_payload_onto_selection(const std::string& clipboard_text);

    // THE EDITOR'S EXTRA KEYS (architect 2026-11-20 / 21, send EE: "I'd like
    // to consider adding an up and down key so that we can also do tempo
    // step. And I'd like to make the pass button a toggle inherit, a true
    // toggle inherit. So both of those will be contingent upon a well-formed
    // string being in place already ... we could just card if it's not a
    // well-formed string. This will make it less jarring, the fact that the
    // flag is sitting there with the old value and we're editing the new
    // value"; Tab, his "a" then Tab gives "a.0"; the keys' seat and their
    // grey at onscreen_keyboard.h's THE EDITOR'S EXTRA KEYS). THREE ACTS ON
    // THE MARKER LANE EDITOR'S TEXT, one function each, the same shape:
    // each reads the editor's WHOLE TEXT, judges it by THE COMMIT'S OWN PARSE
    // (parse_payload_line for the flag, the bound commit's reader for a
    // bound), and on success REWRITES THE WHOLE TEXT through the one
    // incoming filter (text_editor::replace_selection), THE CARET AT ITS END
    // and no selection — a selection standing in the text is replaced with
    // the rest; NOTHING IS COMMITTED (the keyboard's Enter commits the
    // rewritten text through the editor's own road, its Esc throws it away),
    // the store, its undo and the playhead untouched. THE KEYS READ THE TEXT
    // AS IF IT WERE THE MARKER'S COMMITTED FLAG (architect 2026-11-20 / 21,
    // send EE: "a true toggle inherit that works just like the one in the
    // main window does"): each gives what committing the text and pressing
    // the main page's key would give, the tempo a `pass` text sounds at read
    // off the store with the row under edit replaced by the parsed text
    // (resolved_as_if_committed, flag_editor.cpp, where its construction,
    // its cost and why the row alone suffices stand); the refusals that are
    // facts of the committed marker — its coincidence, a fact of frames the
    // text cannot change — read the committed store. A TEXT THE PARSE
    // REFUSES CARDS ("The flag is not well formed: <the parse's reason>" /
    // "The bound is not well formed: <the bound's grammar>", no stderr line,
    // as the keys' other refusal cards) and changes nothing. The damage is
    // the marker lane's (the
    // editor's box). THE GATES ARE THE CALLER'S: the keyboard paints and the
    // keyboard-modal gate admits each key only where it means something
    // (onscreen_keyboard::key_live; handle_top_flag_editor_key the router).
    //
    // UP / DOWN, `delta` +1 / −1: THE MAIN PAGE'S STEP ON ONE MARKER, on the
    // text. On the FLAG editor the tempo step's singleton arm
    // (GuiWarpMarkersOps::adjust_tempo_cents) with its rules and refusals on
    // the parsed flag: a LABEL REFERENCE refuses on its card ("A label
    // reference has no tempo of its own"); in target view an enabled
    // COINCIDENT member refuses on its card ("That marker coincides with
    // another"; the store's row, its place being committed); a PASS text
    // freezes to owning at the tempo a pass at this place sounds at
    // (warp_tempo_step_start over the store as if the text were committed,
    // the projection-aware walk — the previous owner's effective tempo and
    // scale, whatever the committed row holds) and steps from there; an
    // OWNER text steps in place from its own number, a deviation chain's last
    // term taking the step (warp_tempo_step_move); the label definition and
    // the scale ride along; AN OWNER ON THE BRACKET'S END IS THE SILENT WALL
    // (the main page's, whose key greys there instead: this key cannot, a
    // malformed text having to card). On the BOUND editor the bound step on
    // the text's bound (the main page's Up / Down on an addressed bound cell:
    // adjust_iter_bound_cents / adjust_iter_bound_hops), the text's value the
    // start — an empty text the blank's 0 — clamped into the tie's window and
    // at the committed partner (iter_bound_step_landing_from and its phase
    // twin), the wall silent.
    void step_editor_text(int delta);
    // THE PASS KEY: A TRUE TOGGLE INHERIT ON THE FLAG EDITOR'S TEXT — the
    // three arms of GuiWarpMarkersOps::toggle_inherits on the parsed flag: an
    // OWNER becomes `pass` (its label definition kept, its scale and chain
    // gone), a `pass` becomes an OWNER AT THE TEMPO A PASS HERE SOUNDS AT
    // (marker_effective on the store as if the text were committed — the
    // row under edit the parsed `pass`, the projection-aware walk the verb
    // uses: the previous owner's effective tempo and scale, a coincident
    // member's 1.00, 1.00 with no owner before it (the frame-0 seed's, or
    // the walk's no-owner answer for a pass on frame 0); never
    // the committed row's own number; the definition kept), a LABEL
    // REFERENCE becomes `pass`. No refusal past the parse: the main page's
    // verb refuses only on the column and an empty selection, neither of
    // which an open flag editor can stand in. TWICE IN A ROW, EXACTLY: from
    // a `pass` text, `pass` again (its definition kept, the canonical
    // spelling); from an OWNER text, `pass` and then an owner at the tempo a
    // pass here inherits — its own number, scale and chain gone, the
    // definition kept (`1.23*1.5` and `1.23+0.01` come back as that
    // inherited value, not as themselves); from a REFERENCE, `pass` and then
    // that same inherited owner, the reference's name gone — the main page's
    // verb pressed twice, exactly.
    void toggle_inherit_editor_text();
    // TAB: COMPLETE A LABEL REFERENCE. Where the whole text is the start of a
    // label reference — a proper prefix of at least one DEFINED label, the
    // definitions the store's as it stands, the marker under edit's own
    // excluded (its definition goes when its text becomes a reference),
    // a disabled definition counted (a reference binds one where no enabled
    // one stands) — the text is extended by the longest run every such label
    // shares (one candidate: the whole label). True iff the text advanced.
    // NOTHING TO COMPLETE IS SILENT — an empty text, no candidate, a text
    // already at the candidates' common run, a text that is no reference's
    // start (garbage, an owner, a definition): the one autocomplete model's
    // rule (route_modal_editor_key, architect 2026-11-20, send EE). It needs
    // no well-formed string — a reference's start is never one — so it
    // cards nothing.
    bool complete_label_reference();

    // THE ITERATION BOUND EDITOR'S ONE ENTRY (the seventh text_editor Kind,
    // architect 2026-09-05: "each should be like a mini flag with its own
    // double-click" — its double tap, or EDIT on the addressed cell).
    // `column` is 'P' for the phase-reset store and anything
    // else for the warp store, and it is the ACTIVE markers view at every call
    // site, the bound editor being BOTH COLUMNS' since 2026-09-09. `side` is Lower or Upper, the cell the editor
    // opens over, and the open takes the cell's own eligibility (that column's
    // sweep predicate under a lit mode: no cell, no editor), refusing on the
    // bound step's kind sentence where the marker carries no live bracket. The
    // mechanics are the payload editor's open's, over two stores: the focus repaired,
    // the marker single-selected and landed, the addressed cell written to
    // `side` behind that select, the seed the cell's own token (`+0.00` or
    // `+0` on a blank bracket, the column deciding) fully selected.
    // A modal (keyboard, pointer and touch since 2026-10-25) whose open stops
    // play past its guards, as every editor's open does (architect
    // 2026-11-20, send EE; stop_playback_for_modal_open). Read-only refuses
    // at the callers, as the flag editor's open does.
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
    // editor's buffer on a BpmField session (AppState::settings_editor,
    // `target` the field, GuiBpmField) judged and written onto the BPM
    // owner's field. THE BUFFER IS THE FIELD'S VALUE ALONE, the key the
    // row's own (kBpmFieldKeys; architect 2026-11-19, send AA, the line
    // editor's value under the field's label); an EMPTY value CLEARS the
    // field (zero, its unset sentinel); any other value is judged by THE
    // FIELD'S OWN READER (read_bpm_beats, read_bpm_tempo — warpmarkers.h). A
    // refusal leaves the editor standing, its value selected whole, one
    // sentence on stderr and the card — two classes: the beats grammar, the
    // tempo grammar (the low / high order is the RENDER key's grey, not a
    // field's refusal). On success the field is written — session-only, no undo, no
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
    // THE PAYLOAD WRITE'S TWO SHARED TAILS (2026-11-10, the editor's commit
    // and the label paste one road each): the refusal's one sentence on
    // stderr and the card, and the changed write's tail — the dirty mark,
    // the lanes' repaint, the target-view re-warp and the re-land on
    // `reland_idx`, the render trigger.
    void card_payload_refusal(const std::string& err);
    void payload_commit_tail(int reland_idx);
    // THE EXTRA KEYS' TWO SHARED PIECES: the not-well-formed card (its one
    // sentence on the card, no stderr line) and the whole-text rewrite (the text
    // through the one incoming filter, the caret at its end, the lane's
    // damage).
    void card_unparsed_editor_text(const std::string& sentence);
    void rewrite_editor_text(const std::string& text);
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
