#include "prompt.h"

#include "input_handler.h"   // the load confirmation's two answers

#include <utility>

void GuiPrompt::proceed(DialogTrigger t) {
    switch (t) {
    case DialogTrigger::CLOSE_WINDOW:
    case DialogTrigger::REVERT_CONFIRM:
        // THE TWO COMPLETIONS (GuiCloseTarget): an exit asks the platform to
        // end the process's run loop for good; a reopen asks it to RETURN
        // from run() with the window standing, so gui_main's loop can tear
        // this project's object set down and build the next around
        // app.reopen_project (the loop contract, main.cpp). A REVERT completes
        // as a reopen — its seated name is the project already open. Both are
        // platform requests and nothing else happens here — the teardown
        // order is the loop's own.
        if (close_target_ == GuiCloseTarget::Exit) gui.request_exit();
        else                                       gui.request_run_stop();
        break;
    case DialogTrigger::PASTE_CONFIRM:
    case DialogTrigger::LOAD_IN_PLACE_CONFIRM:
    case DialogTrigger::PULL_CONFIRM:
    case DialogTrigger::RENDER_PLAYER_DELETE_CONFIRM:
    case DialogTrigger::SCORE_RESET_CONFIRM:
    case DialogTrigger::TRIM_PRESET_DELETE_CONFIRM:
    case DialogTrigger::TRIM_PRESET_SAVE_CONFIRM:
        // All seven are dispatched directly by activate_response, outside
        // proceed.
        break;
    }
}

void GuiPrompt::open_unsaved(DialogTrigger t) {
    // A modal surface is opening: the shared modal stop
    // (stop_playback_for_modal_open — its declaration owns the decision table).
    // Space is swallowed while the prompt is up, so playback cannot restart
    // until it closes.
    playback_lifecycle.stop_playback_for_modal_open();
    // Save always writes a loadable file: the marker serializer writes
    // the time-sorted store (equal times reload legal), trim bounds persist
    // whatever the store holds (inverted bounds reload intact), and
    // settings persist the committed state — the honest invariant is that
    // the GUI never writes a LOAD-invalid state. Discard
    // (discard-and-proceed) / Cancel / Save; the buttons wear their plain
    // words, the prompt's one road (PromptState's declaration owns the rule).
    // Through `present`, the state's one raise route: it clears the PAINTED
    // bit, so nothing this prompt asks can be answered until the painter has
    // put it on the screen (the rule is at PromptState).
    app.prompt.present("Save unsaved changes?",
                       kPromptDiscardCancelSave,
                       t);
    viewport.invalidate_all();
}

// THE REVERT CONFIRMATION (architect 2026-09-13): a dirty session's Revert
// (Open Project's long press) asks one question, and it is a discard-only one because a revert
// never saves — Discard / Cancel / Save would offer an answer the act does not
// have. Cancel / OK, the affirmative last as on every prompt; OK's `o` is its
// dispatch char alone, its press the answer (no key reaches a prompt,
// architect 2026-10-30). The stop below is every prompt's own
// opening act, not something this one adds: playback stops and an A/B audition ends, exactly as
// raising any other prompt does.
void GuiPrompt::open_revert_confirm() {
    playback_lifecycle.stop_playback_for_modal_open();
    app.prompt.present("Discard unsaved changes and reload?",
                       kPromptCancelOk,
                       DialogTrigger::REVERT_CONFIRM);
    viewport.invalidate_all();
}

// (THE PRODUCT'S PROMPTS ARE THE QUESTIONS ALONE (2026-08-30): no prompt
// merely shows a sentence. A refusal that answers an act is an EVENT (the
// messaging split, notifications.h) — the iteration sweep's cell-cap refusal
// is a NORMAL CARD, and the target-view entry gate refuses SILENTLY, the way
// the load road does, one stderr line and nothing on screen, because its
// refusals are unreachable from program-written input. The questions: the
// unsaved-work question with its save-failed rung,
// the phase reset paste's confirmation, the load confirmation, the revert
// confirmation, the pull's confirmation, the render player's delete, the
// score viewer's reset and the trim presets' save and delete — DialogTrigger,
// app_state.h, the one list.)

// Single-key response dispatch. The trigger captured at prompt-open
// time selects which response set is in play; the key picks the
// response. On a Save failure, the prompt mutates in place to a
// discard/cancel/retry state — same trigger, new text and response
// set — rather than dismissing.
void GuiPrompt::activate_response(char k) {
    if (!app.prompt.active) return;
    const DialogTrigger trigger = app.prompt.trigger;
    // The chars are the PromptAnswer tables' (app_state.h): a letter, or
    // '\x1b' for Cancel, the answer every prompt shares.

    if (trigger == DialogTrigger::PASTE_CONFIRM) {
        // ONE SUBJECT: the phase reset paste, raised by
        // PhaseResetPropagate::open_paste_confirmation. The prompt closes
        // first, so the act runs on the ordinary modal state.
        if (k == 'y') {
            app.prompt.active = false;
            viewport.invalidate_all();
            phase_reset_propagate.paste_apply();
            return;
        }
        if (k == '\x1b') {
            cancel_paste_confirmation();
            return;
        }
        return;
    }

    if (trigger == DialogTrigger::LOAD_IN_PLACE_CONFIRM) {
        // THE LOAD CONFIRMATION (2026-08-28; TWO SUBJECTS, 2026-08-29 —
        // the render player's highlighted batch entry and the `h` view's
        // viewed walk member): OK (`o`, its dispatch char) runs the act the parked subject
        // names, through the input handler; Escape drops both parked subjects.
        // ONE PROMPT BODY, so this arm knows nothing about which raise it is
        // answering — the fork is the handler's. The prompt closes first
        // either way, so the act runs on the ordinary modal state.
        if (k == 'o') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->confirm_load_in_place();
            return;
        }
        if (k == '\x1b') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->cancel_load_in_place();
            return;
        }
        return;
    }

    if (trigger == DialogTrigger::PULL_CONFIRM) {
        // THE PULL'S CONFIRMATION (architect 2026-10-19): OK (`o`, its
        // dispatch char) runs the parked plan through the input handler and
        // reloads the piece; Escape drops it. The prompt closes first either
        // way, so the pull runs on the ordinary modal state (and the reload's
        // reopen asks nothing more).
        if (k == 'o') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->answer_history_pull();
            return;
        }
        if (k == '\x1b') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->cancel_history_pull();
            return;
        }
        return;
    }

    if (trigger == DialogTrigger::RENDER_PLAYER_DELETE_CONFIRM) {
        // THE RENDER PLAYER'S DELETE (architect 2026-09-29; one render's
        // delete 2026-11-25): `d` Delete runs the parked subject's removal
        // through the input handler — a render's or the folders', the parked
        // subject deciding — and Escape drops both. The prompt closes first
        // either way, so the act runs with the player standing on the
        // ordinary modal state.
        if (k == 'd') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->confirm_render_player_delete();
            return;
        }
        if (k == '\x1b') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->cancel_render_player_delete();
            return;
        }
        return;
    }

    // THE TRIM PRESETS LIST'S TWO CONFIRMATIONS (architect
    // 2026-10-28): `d` Delete and `s` Save run their act on the parked row
    // through the input handler, Escape drops it; the prompt closes first
    // either way, so the act runs with the list standing on the ordinary
    // modal state.
    if (trigger == DialogTrigger::TRIM_PRESET_DELETE_CONFIRM ||
        trigger == DialogTrigger::TRIM_PRESET_SAVE_CONFIRM) {
        const bool del =
            trigger == DialogTrigger::TRIM_PRESET_DELETE_CONFIRM;
        if (k == (del ? 'd' : 's')) {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) {
                if (del) input->confirm_trim_preset_delete();
                else     input->confirm_trim_preset_save();
            }
            return;
        }
        if (k == '\x1b') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->cancel_trim_preset_confirm();
            return;
        }
        return;
    }

    // THE SCORE VIEWER'S RESET (2026-10-26): OK writes the range's two
    // edges through the viewer (GuiScoreViewer::confirm_reset), Cancel
    // leaves it; the viewer stands under its question either way.
    if (trigger == DialogTrigger::SCORE_RESET_CONFIRM) {
        if (k == 'o') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->confirm_score_reset();
            return;
        }
        if (k == '\x1b') {
            app.prompt.active = false;
            viewport.invalidate_all();
            return;
        }
        return;
    }

    if (trigger == DialogTrigger::REVERT_CONFIRM) {
        // OK completes the revert through proceed (close_target_ is Revert,
        // seated by the request that raised this); Escape keeps the authored
        // state and the undo history as they stood (the seated reopen name
        // unread) — the transport stays stopped, as under every prompt.
        if (k == 'o') {
            app.prompt.active = false;
            viewport.invalidate_all();
            proceed(trigger);
            return;
        }
        if (k == '\x1b') {
            app.prompt.active = false;
            viewport.invalidate_all();
            return;
        }
        return;
    }

    if (trigger == DialogTrigger::CLOSE_WINDOW) {
        if (k == 's' || k == 'r') {
            const bool ok = save_ops.save();
            if (!ok) {
                // A NEW QUESTION, so it goes through the same raise route and
                // clears the painted bit with it: the new set is answerable
                // only once this text and these buttons have been painted.
                // That is what keeps a stale-rect press from answering this
                // rung (the reasoning is at PromptState).
                // EVERY PROMPT IS A QUESTION (architect 2026-09-01, the
                // capitalization sweep): this rung asks what its buttons
                // answer, like every prompt ("Save unsaved changes?", "Load
                // '…' in place?").
                // IT CARRIES NO REASON AND RAISES NO CARD (2026-09-02): the
                // save owner cards the fault at the arm that met it
                // (save_ops.cpp) — WHICH file it could not write for a write
                // failure, or (2026-09-24) that a checkpoint is still
                // publishing and no file was even attempted
                // (kCheckpointPublishing) — so this rung inherits whichever
                // sentence and asks the one thing only it knows to ask.
                app.prompt.present("Retry the failed save?",
                                   kPromptDiscardCancelRetry,
                                   trigger);
                viewport.invalidate_all();
                return;
            }
            app.prompt.active = false;
            viewport.invalidate_all();
            proceed(trigger);
            return;
        }
        if (k == 'd') {
            app.prompt.active = false;
            viewport.invalidate_all();
            proceed(trigger);
            return;
        }
        if (k == '\x1b') {
            app.prompt.active = false;
            viewport.invalidate_all();
            return;
        }
        return;
    }
}

// The load confirmation's programmatic abandon (contract at the declaration).
// It reaches the SAME body Cancel reaches — the prompt down, both parked
// subjects dropped through cancel_load_in_place — so the asynchronous edge
// and the button's own Cancel cannot leave different state behind.
void GuiPrompt::cancel_load_confirmation() {
    if (!app.prompt.active) return;
    if (app.prompt.trigger != DialogTrigger::LOAD_IN_PLACE_CONFIRM) return;
    app.prompt.active = false;
    viewport.invalidate_all();
    if (input != nullptr) input->cancel_load_in_place();
}

void GuiPrompt::cancel_paste_confirmation() {
    app.prompt.active = false;
    app.pending_paste_anchor = -1;
    viewport.invalidate_all();
}

// Route a close gesture through the prompt when history is dirty;
// otherwise proceed immediately. Centralizes the decision so the platform's
// close (Back), the Open project picker's open act and Revert
// share identical behaviour; what differs between them is the target, seated
// here for proceed — and, for Revert alone, the question a dirty session is
// asked — and (2026-10-25) whether a standing picker comes down first
// (the exit alone; the reopen and the revert are the picker's own questions).
void GuiPrompt::request_close(GuiCloseTarget target, bool question_asked) {
    if (app.prompt.active) return; // already gated; ignore re-entry
    // THE RENDER PLAYER COMES DOWN FIRST, on every road into this one: the
    // mode's transport is stopped, the view's buffer rebound and the overlay
    // cleared before the question is asked, so a Cancel leaves the ordinary
    // window rather than a player the user cannot see the prompt through — one
    // road, one closer, the platform's close (Back, main.cpp's set_on_close)
    // among the roads. A no-op when the mode is down, which is every other
    // close.
    render_player.close();
    // AND SO DOES A STANDING PICKER ON THE EXIT ROAD (2026-08-28), the same
    // shape one mode over — one close body, close_picker, which takes the
    // overlay down with it. ON THE REOPEN AND REVERT ROADS IT STAYS
    // (architect 2026-10-25, one rule for the overlays: an overlay stays
    // standing under its own question, Cancel returning to it, OK ending
    // it): with a picker standing, those two targets are reached only from
    // the picker itself — its pick (open_project_commit) and its Ctrl+Alt+O
    // (route_picker_key), the OPEN key's long press among its spellings —
    // so the question is the picker's own, painted over the list, and the
    // picker ends with its session when the reopen completes (AppState dies
    // with run_project's frame, main.cpp). An exit is no question of the
    // picker's — the platform's close — and takes it down before asking, as
    // it takes the player.
    if (input != nullptr && target == GuiCloseTarget::Exit)
        input->close_picker();
    // AND THE SCORE VIEWER COMES DOWN FIRST (2026-10-26), the player's step:
    // only the exit road reaches here with it standing (its router consumes
    // Revert's chord and the picker's opener), and a quit question asked
    // over it would leave a Cancel returning to a viewer the question was
    // not about. A no-op when it is down.
    if (input != nullptr) input->close_score_viewer();
    // AND EVERY STANDING MODAL EDITOR COMES DOWN WITH IT, uncommitted, for the
    // identical reason and on the identical road, so a close over a standing
    // editor never asks the unsaved-work question through it, the editor back
    // on screen at Cancel — and so no prompt ever stands with the on-screen
    // keyboard up (the prompt's one road is its buttons). The step is
    // the input handler's (each editor's own exit body, and at most one can
    // stand), stated once at close_modal_editors_no_commit and idempotent.
    if (input != nullptr) input->close_modal_editors_no_commit();
    close_target_ = target;
    if (!app.dirty || question_asked)
        proceed(DialogTrigger::CLOSE_WINDOW);
    else if (target == GuiCloseTarget::Revert)
        open_revert_confirm();
    else
        open_unsaved(DialogTrigger::CLOSE_WINDOW);
}
