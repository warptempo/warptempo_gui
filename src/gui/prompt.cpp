#include "prompt.h"

#include "env_fingerprint.h"
#include "input_handler.h"

#include <utility>

void GuiPrompt::proceed(DialogTrigger t) {
    switch (t) {
    case DialogTrigger::CLOSE_WINDOW:
        gui.request_exit();
        break;
    case DialogTrigger::PASTE_CONFIRM:
    case DialogTrigger::ERROR_NOTICE:
    case DialogTrigger::ENV_HASH_MISMATCH:
    case DialogTrigger::HISTORY_COMMIT:
        // All four are dispatched directly by activate_response, outside
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
    app.prompt.active          = true;
    app.prompt.text            = "Save unsaved changes?";
    // Sentinel chars for non-letter keys: 0x7F = Delete, 0x1B = Escape.
    // The GuiKey → char mapping in input_handler.cpp's prompt dispatch
    // produces these for GuiKeys::Delete / GuiKeys::Escape; the prompt
    // machinery remains a vector<char> match.
    //
    // [S]ave always writes a loadable file: the marker serializer writes
    // the time-sorted store (equal times reload legal), trim bounds persist
    // whatever the store holds (inverted bounds reload intact), and
    // settings persist the committed state — the honest invariant is that
    // the GUI never writes a LOAD-invalid state. [S]ave / [Delete]
    // (discard-and-proceed) / [Esc] (cancel). The accelerator letter
    // capitalizes in the label while the match stays lowercase-exact —
    // PromptState's declaration owns that rule.
    app.prompt.response_keys   = {'s', '\x7f', '\x1b'};
    app.prompt.response_labels = {"[S]ave", "[Delete]", "[Esc]"};
    app.prompt.trigger         = t;
    viewport.invalidate_all();
}

// Dismiss-only error notice. The text is the caller's error string —
// the parser's own message, verbatim. Single acknowledge response:
// Esc (the '\x1b' sentinel; see open_unsaved's key-mapping note).
// Modal like every other prompt while active: the pointer handlers
// swallow mouse events and on_key routes only the response key.
void GuiPrompt::open_error_notice(std::string text) {
    // A modal surface is opening: the shared modal stop, same rule as every
    // other prompt open.
    playback_lifecycle.stop_playback_for_modal_open();
    app.prompt.active          = true;
    app.prompt.text            = std::move(text);
    app.prompt.response_keys   = {'\x1b'};
    app.prompt.response_labels = {"[Esc]"};
    app.prompt.trigger         = DialogTrigger::ERROR_NOTICE;
    viewport.invalidate_all();
}

// Load-time render-environment mismatch, advisory only. ONE response key:
// 'o' acknowledges by
// restamping the four live hashes (history-less, no-dirty GUI-kind state).
// There is deliberately NO dismiss-without-ack path — Esc is not a response
// key, so the prompt's key filter swallows it like every other non-response
// key, and acknowledging is the only way past the prompt.
void GuiPrompt::open_env_hash_mismatch(const std::string& changed_list) {
    // A modal surface is opening: the shared modal stop, same rule as every
    // other prompt open (at the load-time call site playback is not running, but
    // the chokepoint keeps the invariant unconditional).
    playback_lifecycle.stop_playback_for_modal_open();
    app.prompt.active          = true;
    app.prompt.text            = "Render libraries changed since last save (" +
                                 changed_list +
                                 "). New renders may not match old ones.";
    app.prompt.response_keys   = {'o'};
    app.prompt.response_labels = {"[O]k"};
    app.prompt.trigger         = DialogTrigger::ENV_HASH_MISMATCH;
    viewport.invalidate_all();
}

// The history mode's commit confirmation. Same shape as the paste confirm — one
// question, `y` or Esc — because it is the same kind of thing: an act that runs
// past the point of easy undo and should be asked about once. What is new is
// where the effect lands: outside this session, in the projects repository.
// The text names both halves of what will happen, the message and the
// destination, and nothing about either is editable here.
void GuiPrompt::open_history_commit_confirm(const std::string& commit_title,
                                            const std::string& repo) {
    // A modal surface is opening: the shared modal stop, same rule as every
    // other prompt open (stop_playback_for_modal_open's declaration owns the
    // decision table).
    playback_lifecycle.stop_playback_for_modal_open();
    app.prompt.active          = true;
    // THE QUESTION NAMES BOTH HALVES since 2026-08-04: the act saves the piece
    // beside its source through the ordinary Ctrl+S owner and only then writes,
    // commits and pushes the checkpoint, so a question that said "Commit" alone
    // would understate what `y` does. SENTENCE CASE HERE, deliberately: this is
    // prose, and the title-cased "Save and Commit" is the BUTTON's label, which
    // is the architect's named exception to the sentence-case convention rather
    // than a spelling to copy into a sentence.
    app.prompt.text =
        "Save and commit \"" + commit_title + "\" to " + repo + "?";
    app.prompt.response_keys   = {'y', '\x1b'};
    app.prompt.response_labels = {"[Y]es", "[Esc]"};
    app.prompt.trigger         = DialogTrigger::HISTORY_COMMIT;
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

    if (trigger == DialogTrigger::ENV_HASH_MISMATCH) {
        // 'o' is the SOLE response key (no dismiss-without-ack path exists;
        // Esc never reaches here — it is not in response_keys, so the key
        // filter swallows it). Acknowledge: stamp all four LIVE hashes to the
        // current environment's. This is history-less, no-dirty GUI-kind state
        // (like trim / view prefs): the restamp marks NOTHING dirty and simply
        // persists on the next ordinary Ctrl+S. A save-less session drops it,
        // so the next load re-fires this modal by design (self-healing).
        if (k == 'o') {
            const RenderEnvHashes& cur = compute_render_env_hashes();
            app.libm_hash          = cur.libm;
            app.libmvec_hash       = cur.libmvec;
            app.fftw3_hash         = cur.fftw3;
            app.fftw3_threads_hash = cur.fftw3_threads;
            app.prompt.active = false;
            viewport.invalidate_all();
            return;
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

    if (trigger == DialogTrigger::HISTORY_COMMIT) {
        // THE PROMPT COMES DOWN BEFORE THE ACT RUNS — the paste confirm's own
        // order, and it matters more here: the act shells out to git and can
        // take a moment, and it closes the history view at its tail, so running
        // it under a prompt still marked active would leave the strip painting a
        // question that has already been answered.
        if (k == 'y') {
            app.prompt.active = false;
            viewport.invalidate_all();
            if (input) input->run_history_commit();
            return;
        }
        // ABANDON: nothing has been written at this point — the act does not
        // begin until `y` — so there is no state to unwind and Esc is a plain
        // dismissal.
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
