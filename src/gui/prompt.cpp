#include "prompt.h"

#include "input_handler.h"   // the load confirmation's two answers

#include <utility>

void GuiPrompt::proceed(DialogTrigger t) {
    switch (t) {
    case DialogTrigger::CLOSE_WINDOW:
        // THE TWO COMPLETIONS (GuiCloseTarget): an exit asks the platform to
        // end the process's run loop for good; a reopen asks it to RETURN
        // from run() with the window standing, so gui_main's loop can tear
        // this project's object set down and build the next around
        // app.reopen_project (the loop contract, main.cpp). Both are
        // platform requests and nothing else happens here — the teardown
        // order is the loop's own.
        if (close_target_ == GuiCloseTarget::Reopen) gui.request_run_stop();
        else                                         gui.request_exit();
        break;
    case DialogTrigger::PASTE_CONFIRM:
    case DialogTrigger::ERROR_NOTICE:
    case DialogTrigger::LOAD_IN_PLACE_CONFIRM:
        // All three are dispatched directly by activate_response, outside
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
    // Sentinel chars for non-letter keys: 0x7F = Delete, 0x1B = Escape.
    // The GuiKey → char mapping in input_handler.cpp's prompt dispatch
    // produces these for GuiKeys::Delete / GuiKeys::Escape; the prompt
    // machinery remains a vector<char> match.
    //
    // Save always writes a loadable file: the marker serializer writes
    // the time-sorted store (equal times reload legal), trim bounds persist
    // whatever the store holds (inverted bounds reload intact), and
    // settings persist the committed state — the honest invariant is that
    // the GUI never writes a LOAD-invalid state. `s` = Save / Delete =
    // discard-and-proceed / Esc = cancel; the labels are the BUTTONS' PLAIN
    // WORDS (PromptState's declaration owns the rule and the retired bracket
    // spelling's record), so the Delete sentinel wears "Discard" and Escape
    // wears "Cancel" — each button naming its key on its TOOLTIP instead.
    // Through `present`, the state's one raise route: it clears the PAINTED
    // bit, so nothing this prompt asks can be answered until the painter has
    // put it on the screen (the rule is at PromptState).
    app.prompt.present("Save unsaved changes?",
                       {'s', '\x7f', '\x1b'},
                       {"Save", "Discard", "Cancel"},
                       t,
                       PromptInitialFocus::LastButton);
    viewport.invalidate_all();
}

// Dismiss-only error notice. The text is the caller's error string —
// the parser's own message, verbatim. Single acknowledge response:
// Esc (the '\x1b' sentinel; see open_unsaved's key-mapping note). Its
// one button wears "OK" rather than "Cancel": nothing is being cancelled and
// the act already refused. Its tooltip names Escape like every other Esc
// button's does (modal_dialog_button_hint, app_state.h).
// Modal like every other prompt while active: the pointer veil consumes
// everything outside the dialog and on_key routes only the response key.
void GuiPrompt::open_error_notice(std::string text) {
    // A modal surface is opening: the shared modal stop, same rule as every
    // other prompt open.
    playback_lifecycle.stop_playback_for_modal_open();
    // The one raise route (PromptState::present), so the painted gate holds
    // here too.
    app.prompt.present(std::move(text), {'\x1b'}, {"OK"},
                       DialogTrigger::ERROR_NOTICE,
                       PromptInitialFocus::LastButton);
    viewport.invalidate_all();
}

// Single-key response dispatch. The trigger captured at prompt-open
// time selects which response set is in play; the key picks the
// response. On a Save failure, the prompt mutates in place to a
// retry/discard/cancel state — same trigger, new text and response
// set — rather than dismissing.
void GuiPrompt::activate_response(char k) {
    if (!app.prompt.active) return;
    const DialogTrigger trigger = app.prompt.trigger;
    // Sentinels: '\x7f' = Delete (discard), '\x1b' = Escape (cancel).
    // See open_unsaved above.

    if (trigger == DialogTrigger::ERROR_NOTICE) {
        // Acknowledge-and-dismiss; nothing proceeds and nothing mutates.
        if (k == '\x1b') {
            app.prompt.active = false;
            viewport.invalidate_all();
        }
        return;
    }

    if (trigger == DialogTrigger::PASTE_CONFIRM) {
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
        // THE RENDER PLAYER'S LOAD CONFIRMATION (2026-08-28): `o` is OK and
        // runs the shared act through the input handler (which closes the
        // player on success); Escape drops the parked entry. The prompt
        // closes first either way, so the act runs on the ordinary modal
        // state — the player's own row comes back under it.
        if (k == 'o') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->confirm_render_player_load();
            return;
        }
        if (k == '\x1b') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input != nullptr) input->cancel_render_player_load();
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
                // That is what closes the stale-rect press this rung used to
                // leave answerable (the reasoning is at PromptState).
                app.prompt.present("Save failed.",
                                   {'r', '\x7f', '\x1b'},
                                   {"Retry", "Discard", "Cancel"},
                                   trigger,
                                   PromptInitialFocus::LastButton);
                viewport.invalidate_all();
                return;
            }
            app.prompt.active = false;
            viewport.invalidate_all();
            proceed(trigger);
            return;
        }
        if (k == '\x7f') {
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

void GuiPrompt::cancel_paste_confirmation() {
    app.prompt.active = false;
    app.pending_paste_anchor = -1;
    viewport.invalidate_all();
}

// Route a close gesture through the prompt when history is dirty;
// otherwise proceed immediately. Centralizes the decision so Ctrl+Q, the
// WM-close callback and the Open prompt's commit share identical behaviour;
// what differs between them is the target, seated here for proceed.
void GuiPrompt::request_close(GuiCloseTarget target) {
    if (app.prompt.active) return; // already gated; ignore re-entry
    // THE RENDER PLAYER COMES DOWN FIRST, on every road into this one: the
    // mode's transport is stopped, the view's buffer rebound and the overlay
    // cleared before the question is asked, so a Cancel leaves the ordinary
    // window rather than a player the user cannot see the prompt through. It
    // lives here rather than in the keyboard's own Ctrl+Q arm because the
    // compositor's close (main.cpp's set_on_close) arrives without a key and
    // must take the identical step — one road, one closer. A no-op when the
    // mode is down, which is every other close.
    render_player.close();
    // AND EVERY STANDING MODAL EDITOR COMES DOWN WITH IT, uncommitted, for the
    // identical reason and on the identical road: the keyboard's Ctrl+Q used
    // to do this itself, editor by editor, and the COMPOSITOR'S CLOSE — which
    // carries no key — did not, so a WM close over the Open prompt asked the
    // unsaved-work question through a still-standing editor and its picker
    // band, both back on screen at Cancel. The step is the input handler's
    // (each editor's own exit body, and at most one can stand), stated once at
    // close_modal_editors_no_commit and idempotent, so the roads that already
    // closed their editor — File → Open project's commit — pay nothing.
    if (input != nullptr) input->close_modal_editors_no_commit();
    close_target_ = target;
    if (app.dirty)
        open_unsaved(DialogTrigger::CLOSE_WINDOW);
    else
        proceed(DialogTrigger::CLOSE_WINDOW);
}
