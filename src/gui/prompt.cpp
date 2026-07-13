#include "prompt.h"

#include <utility>

void GuiPrompt::proceed(DialogTrigger t) {
    switch (t) {
    // A render entry's sidecars are frozen at dispatch, so a close under an
    // open render view has nothing to persist — renders/ survives regardless.
    // No render-view teardown on the CLOSE_WINDOW arm: the process is exiting.
    case DialogTrigger::CLOSE_WINDOW:
        gui.request_exit();
        break;
    case DialogTrigger::PASTE_CONFIRM:
    case DialogTrigger::ERROR_NOTICE:
    case DialogTrigger::DEFECT_RESOLUTION:
        // All three are dispatched outside proceed: the first two directly
        // by activate_response, the defect series by
        // GuiInputHandler::handle_defect_response.
        break;
    }
}

void GuiPrompt::open_unsaved(DialogTrigger t) {
    // A modal surface is opening: stop playback. Space is swallowed while
    // the prompt is up, so playback cannot restart until it closes.
    playback_lifecycle.stop_playback_if_playing();
    app.prompt.active          = true;
    app.prompt.text            = "Save unsaved changes?";
    // Sentinel chars for non-letter keys: 0x7F = Delete, 0x1B = Escape.
    // The GuiKey → char mapping in input_handler.cpp's prompt dispatch
    // produces these for GuiKeys::Delete / GuiKeys::Escape; the prompt
    // machinery remains a vector<char> match.
    //
    // One form for every open, including a close requested out of a
    // suspended defect series. Every state the series can be showing is a
    // walkable defect, and the walkable set is by definition the loads-intact
    // set: the honest invariant is that the GUI never writes a LOAD-invalid
    // state, and a mid-series save satisfies it. The marker serializer writes
    // the time-sorted store (equal times reload legal), trim bounds persist
    // whatever the store holds (inverted bounds reload and re-walk), and
    // settings persist the committed state — so [S]ave writes a loadable file
    // that re-walks its own series on the next load. [S]ave / [Delete]
    // (discard-and-proceed) / [Esc] (cancel; resumes the series when one is
    // suspended for this close).
    app.prompt.response_keys   = {'s', '\x7f', '\x1b'};
    app.prompt.response_labels = {"[S]ave", "[Delete]", "[Esc]"};
    app.prompt.trigger         = t;
    viewport.clear_hover_popup();
    viewport.invalidate_all();
}

// Dismiss-only error notice. The text is the caller's error string —
// the parser's own message, verbatim. Single acknowledge response:
// Esc (the '\x1b' sentinel; see open_unsaved's key-mapping note).
// Modal like every other prompt while active: the pointer handlers
// swallow mouse events and on_key routes only the response key.
void GuiPrompt::open_error_notice(std::string text) {
    // A modal surface is opening: stop playback (same rule as every other
    // prompt open).
    playback_lifecycle.stop_playback_if_playing();
    app.prompt.active          = true;
    app.prompt.text            = std::move(text);
    app.prompt.response_keys   = {'\x1b'};
    app.prompt.response_labels = {"[Esc]"};
    app.prompt.trigger         = DialogTrigger::ERROR_NOTICE;
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
            // Cancel of a close/revert prompt raised over a suspended defect
            // series resumes the series: re-queue the once-per-tick funnel
            // with the origin the series was opened with (derived from
            // commit_context — Commit-origin series carry it true, so the
            // coincident-group narrowing is preserved on reopen; a
            // Load/target-entry series carries it false). run_commit_validation
            // re-opens on the next tick, re-validating from scratch, so the
            // same defect reappears — one tick of ordinary-looking UI in
            // between is acceptable, matching how the funnel already defers.
            // [U]ndo availability is origin-independent (open_defect_series
            // offers it purely on the undo_stack-non-empty rule).
            if (app.defect_series.suspended_for_close) {
                app.defect_series.pending_validation =
                    app.defect_series.commit_context
                        ? PendingValidation::Commit
                        : PendingValidation::Load;
                app.defect_series.suspended_for_close = false;
            }
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

// Route a close gesture through the prompt when history is dirty or a
// defect series is suspended for this close; otherwise proceed
// immediately. Centralizes the decision so Ctrl+Q and the WM-close
// callback share identical behavior. A suspended series is
// mid-resolution of a walkable defect — its store can be clean (a
// load-origin series has app.dirty == false), yet the close must still
// be confirmed, so it gates on suspended_for_close. The prompt is the
// ordinary save/discard/cancel form: walkable defects are load-legal, so
// the save writes a loadable file that re-walks its series on the next
// load, and Esc still resumes the suspended series.
void GuiPrompt::request_close_or_revert(DialogTrigger t) {
    if (app.prompt.active) return; // already gated; ignore re-entry
    if (app.dirty || app.defect_series.suspended_for_close) open_unsaved(t);
    else                                                    proceed(t);
}
