#include "prompt.h"

void GuiPrompt::proceed(DialogTrigger t) {
    switch (t) {
    case DialogTrigger::CLOSE_WINDOW:
        gui.request_exit();
        break;
    case DialogTrigger::REVERT_TO_BLANK:
        file_loader.revert_to_blank();
        break;
    case DialogTrigger::PASTE_CONFIRM:
        // Paste prompt is dispatched directly by activate_response;
        // proceed is not the path it lands on.
        break;
    }
}

void GuiPrompt::open_unsaved(DialogTrigger t) {
    app.prompt.active          = true;
    app.prompt.text            = "Save unsaved changes?";
    // Sentinel chars for non-letter keys: 0x7F = Delete, 0x1B = Escape.
    // The GuiKey → char mapping in input_handler.cpp's prompt dispatch
    // produces these for GuiKeys::Delete / GuiKeys::Escape; the prompt
    // machinery remains a vector<char> match.
    app.prompt.response_keys   = {'s', '\x7f', '\x1b'};
    app.prompt.response_labels = {"[S]ave", "[Delete]", "[Esc]"};
    app.prompt.trigger         = t;
    viewport.clear_hover_popup();
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

    if (trigger == DialogTrigger::CLOSE_WINDOW ||
        trigger == DialogTrigger::REVERT_TO_BLANK) {
        if (k == 's' || k == 'r') {
            const bool ok = save_ops.save();
            if (!ok) {
                app.prompt.text            = "Save failed.";
                app.prompt.response_keys   = {'r', '\x7f', '\x1b'};
                app.prompt.response_labels =
                    {"[R]etry", "[Delete]", "[Esc]"};
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

// Route a close / revert gesture through the prompt when history is
// dirty; otherwise proceed immediately. Centralizes the decision so
// Ctrl+Q, Ctrl+W, and the WM-close callback share identical behavior.
void GuiPrompt::request_close_or_revert(DialogTrigger t) {
    if (app.prompt.active) return; // already gated; ignore re-entry
    if (app.dirty) open_unsaved(t);
    else           proceed(t);
}
