#include "prompt.h"

#include <utility>

void GuiPrompt::proceed(DialogTrigger t) {
    switch (t) {
    case DialogTrigger::CLOSE_WINDOW:
        gui.request_exit();
        break;
    case DialogTrigger::PASTE_CONFIRM:
    case DialogTrigger::ERROR_NOTICE:
        // Both are dispatched directly by activate_response, outside proceed.
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
                       t);
    viewport.invalidate_all();
}

// Dismiss-only error notice. The text is the caller's error string —
// the parser's own message, verbatim. Single acknowledge response:
// Esc (the '\x1b' sentinel; see open_unsaved's key-mapping note). Its
// one button wears "OK" rather than "Cancel": nothing is being cancelled and
// the act already refused. Its tooltip names Escape like every other Esc
// button's does (modal_dialog_button_hint, app_state.h).
// Modal like every other prompt while active: the pointer veil consumes
// everything outside the dialog and the key dispatch's prompt gate routes only
// the response key, at that key's release.
void GuiPrompt::open_error_notice(std::string text) {
    // A modal surface is opening: the shared modal stop, same rule as every
    // other prompt open.
    playback_lifecycle.stop_playback_for_modal_open();
    // The one raise route (PromptState::present), so the painted gate holds
    // here too.
    app.prompt.present(std::move(text), {'\x1b'}, {"OK"},
                       DialogTrigger::ERROR_NOTICE);
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
                                   trigger);
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
// otherwise proceed immediately. Centralizes the decision so Ctrl+Q and
// the WM-close callback share identical behavior.
void GuiPrompt::request_close() {
    if (app.prompt.active) return; // already gated; ignore re-entry
    if (app.dirty)
        open_unsaved(DialogTrigger::CLOSE_WINDOW);
    else
        proceed(DialogTrigger::CLOSE_WINDOW);
}
