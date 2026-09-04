#pragma once

#include "app_state.h"
#include "phase_reset_propagate.h"
#include "platform.h"
#include "playback_lifecycle.h"
#include "render_player.h"
#include "save_ops.h"
#include "viewport.h"

// Prompt state machine, extracted from main.cpp's inline lambdas. Owns the
// unsaved-work dialog and the paste-confirm dialog, and ANSWERS the load
// confirmation (LOAD_IN_PLACE_CONFIRM — one prompt body over two subjects,
// raised by GuiInputHandler::render_player_load_in_place in the player and by
// GuiInputHandler::history_load_in_place in the `h` view, and answered back
// through the input-handler back-pointer below). Two entry points are exposed:
// request_close (called by Ctrl+Q, the WM-close callback and the Open
// prompt's commit — the ONE close road, which is why the render player's
// close and the modal editors' abandon both live inside it) and
// activate_response (called by the keyboard
// handler when a prompt is active). The other two former lambdas (open_unsaved,
// proceed) are private helpers; they have no callers outside this cluster.
//
// save_markers is reached through save_ops. viewport, phase_reset_propagate,
// and gui are reached directly.

// WHAT A CLOSE COMPLETES — the one prompt body, two completions (architect
// 2026-08-27, with the reopen loop). EXIT is Ctrl+Q's and the WM close's:
// the run loop stops and the process ends. REOPEN is File → Open project's: the run
// loop stops and gui_main's loop builds the next object set around the
// project the prompt chose (AppState::reopen_project, already seated by the
// commit). The unsaved-work question, its three answers, its Save-failed
// rung and its painted-before-answering rule are IDENTICAL for both — only
// the act the answer completes differs, which is why the target is an enum on
// the request and not a second prompt.
enum class GuiCloseTarget { Exit, Reopen };

struct GuiInputHandler;

struct GuiPrompt {
    AppState&             app;
    GuiPlatform&          gui;
    Viewport&             viewport;
    PhaseResetPropagate&  phase_reset_propagate;
    GuiSaveOps&           save_ops;
    GuiPlaybackLifecycle& playback_lifecycle;
    // THE RENDER PLAYER, held for one line of request_close: a close gesture
    // takes the mode down before it asks anything (the editors' half of that
    // same step goes through the back-pointer below). Constructed ahead of this
    // struct in main.cpp, like every other reference here.
    GuiRenderPlayer&      render_player;
    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this prompt by reference, so the
    // dependency is a cycle resolved with a pointer set post-construction —
    // the settings editor's own shape). THREE READERS: the load
    // confirmation, whose OK and Cancel reach the acts that live on
    // GuiInputHandler (confirm_load_in_place, cancel_load_in_place)
    // — the three load acts being private to that struct — and
    // request_close's own closing steps
    // (close_picker, close_stats_panel and close_modal_editors_no_commit),
    // the picker's close, the panel's and each editor's exit body living
    // there beside the surfaces themselves; and, since 2026-09-04, that same
    // road's refusal (close_refused_by_external_sync), whose predicate is the
    // synchronization worker the input handler holds.
    GuiInputHandler*      input = nullptr;

    GuiPrompt(AppState&             app_,
              GuiPlatform&          gui_,
              Viewport&             viewport_,
              PhaseResetPropagate&  phase_reset_propagate_,
              GuiSaveOps&           save_ops_,
              GuiPlaybackLifecycle& playback_lifecycle_,
              GuiRenderPlayer&      render_player_)
        : app(app_),
          gui(gui_),
          viewport(viewport_),
          phase_reset_propagate(phase_reset_propagate_),
          save_ops(save_ops_),
          playback_lifecycle(playback_lifecycle_),
          render_player(render_player_) {}

    // Route a close through the unsaved-work prompt when history is dirty;
    // otherwise complete `target` immediately. Centralizes the decision so
    // Ctrl+Q, the WM-close callback and the Open prompt's commit share
    // identical behaviour, each naming what its close completes — AND WHAT
    // CLOSES BEFORE THE QUIT QUESTION IS STATED HERE, ONCE, for every road:
    // a standing render player comes down at this road's head, a standing
    // picker with it (close_picker), and with them every standing modal
    // editor, abandoned uncommitted through the input handler's one body
    // (close_modal_editors_no_commit). No caller restates any step, which is
    // what keeps the compositor's close — arriving with no key, so it can run
    // no keyboard arm of its own — from raising the unsaved-work prompt over
    // a mode or an editor that is still up. Ctrl+Q, the WM-close callback
    // and the Open project picker's open act share the road; what differs
    // is the target.
    //
    // It can also refuse (architect 2026-09-04): a running Synchronize to
    // external storage stops the close dead at this road's head, above every
    // step named above, so nothing is torn down and no question is asked. The
    // mirror's worker has no cancel, and its join must never run under a live
    // window. The gate is one call through the back-pointer
    // (close_refused_by_external_sync), which owns the predicate, the card
    // and the reasoning; both targets meet it.
    void request_close(GuiCloseTarget target);
    void activate_response(char k);

    // (NO COMMIT CONFIRMATION HERE ANY MORE. The `h` history view's
    // Save-and-Commit act was guarded by a fourth prompt — HISTORY_COMMIT, one
    // question with `y` and Esc — until 2026-08-07, when the architect replaced
    // it with the COMMIT-TITLE EDITOR: the act asks for the message instead of
    // asking for permission, and a bare Enter over the prefilled default is the
    // old `y`. The prompt kind, this opener and the back-pointer to the input
    // handler that its `y` reached the act through were all deleted; the editor
    // lives with the mode's other machinery, at
    // GuiInputHandler::open_history_commit_editor. THE BACK-POINTER IS BACK
    // SINCE 2026-08-28 for a different question — the render player's load
    // confirmation, whose raise lives on the input handler beside the act it
    // confirms; the member above records its one reader.)

    // (THE DISMISS-ONLY ERROR NOTICE — ERROR_NOTICE, open_error_notice —
    // RETIRED WHOLE 2026-08-30, architect. It was the pre-split surface for a
    // sentence the user had to be shown: the environmental and tripwire-class
    // refusals, painted in the modal like every other prompt, dismissed by its
    // lone "OK" on Esc. The messaging split (messaging.md) left it with
    // nothing to carry. A refusal that answers an act is an EVENT, so the
    // ITERATION SWEEP'S CELL-CAP refusal is a NORMAL CARD now, with the same
    // sentence; and the TARGET-VIEW ENTRY GATE refuses SILENTLY the way its
    // twin the load restore always did — one stderr line, nothing on screen —
    // because its refusals are unreachable from program-written input (the
    // ruling and its reason are at validate_target_view_entry,
    // input_handler.h). The CHECKPOINT ACT'S FAILURE REPORT was a third caller
    // from 2026-08-07: it arrived on a worker's clock, past every gate, and
    // had to make room for itself; on 2026-08-09 the architect replaced it
    // with the bottom row's permanent paint-only critical slot, and since
    // 2026-08-29 it is a CRITICAL NOTIFICATION CARD.
    //
    // WHAT SURVIVES THIS STRUCT ARE THE QUESTIONS ALONE — the unsaved-work
    // question with its save-failed rung, the paste confirmation and the load
    // confirmation — so EVERY PROMPT IN THE PRODUCT IS NOW A QUESTION, which
    // is the messaging split's own rule read back into the type.
    //
    // THE PRODUCT STILL HAS ONE ASYNCHRONOUS MODAL OPENER, and it never was
    // this one: the compositor's WM close raises the unsaved-work prompt
    // (GuiPrompt::request_close from main.cpp's set_on_close) on the
    // compositor's clock, with no key and no gesture behind it. It is safe
    // because it CLEARS THE WAY IN ITS OWN BODY first — force-ending every
    // live gesture through their release bodies, hiding the floating hint and
    // closing the popup — so the honest rule is that nothing asynchronous
    // raises a modal without doing that, rather than that nothing
    // asynchronous raises one at all.)

    // Real abandon for an active PASTE_CONFIRM prompt: dismiss the
    // prompt and clear the pending paste anchor. Called from
    // activate_response on Esc, and from the Ctrl+Q interception in
    // input_handler so both cancels go through one path (no synthesized
    // Esc keystroke). Safe to call only when a
    // PASTE_CONFIRM prompt is up.
    void cancel_paste_confirmation();

    // Real abandon for a standing LOAD_IN_PLACE_CONFIRM, for a subject that
    // has just been destroyed under it. Its ONE caller is the history
    // prefetch's FAILED-SCAN arrival (GuiInputHandler::on_history_prefetch_ready),
    // which closes the `h` view off a poll and with it clears the walk member
    // the question names — leaving an actionable "Load '<member>' in place?"
    // over the ordinary editor whose OK would find no subject and load
    // nothing, silently. This drops the question and both parked subjects
    // through the answer's own Cancel body, so a subject cannot outlive its
    // question on the asynchronous edge either.
    //
    // IT IS THIS ONE PROMPT KIND AND NOT A GENERAL DISMISS: the trigger test
    // is what makes it safe to call from an edge that knows nothing about
    // which question is standing — an unsaved-work or paste question is left
    // exactly where it was.
    void cancel_load_confirmation();

private:
    void open_unsaved(DialogTrigger t);
    void proceed(DialogTrigger t);

    // The completion the standing CLOSE_WINDOW prompt will run — seated at
    // request_close and read at proceed. A prompt cannot be raised over a
    // prompt (request_close refuses re-entry), so the standing question and
    // this target always belong to the same request.
    GuiCloseTarget close_target_ = GuiCloseTarget::Exit;
};
