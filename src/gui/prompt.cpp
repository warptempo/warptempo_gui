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
    case DialogTrigger::LOAD_IN_PLACE_CONFIRM:
        // Both are dispatched directly by activate_response, outside
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

// (THE DISMISS-ONLY ERROR NOTICE RETIRED WHOLE 2026-08-30, with its
// ERROR_NOTICE trigger, its Esc-only response set and its lone "OK" button.
// It was the pre-split surface for a sentence the user had to be shown, and
// the messaging split (messaging.md) left it with nothing to carry: a refusal
// that answers an act is an EVENT, so its ONE remaining loud caller — the
// iteration sweep's cell-cap refusal — is a NORMAL CARD now, and its other,
// the target-view entry gate, refuses SILENTLY the way the load road always
// did, one stderr line and nothing on screen, because its refusals are
// unreachable from program-written input. THE PRODUCT'S PROMPTS ARE THE
// QUESTIONS ALONE now: the unsaved-work question with its save-failed rung,
// the paste confirmation and the load confirmation.)

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

    if (trigger == DialogTrigger::LOAD_IN_PLACE_CONFIRM) {
        // THE LOAD CONFIRMATION (2026-08-28; TWO SUBJECTS since 2026-08-29 —
        // the render player's highlighted batch entry and the `h` view's
        // viewed walk member): `o` is OK and runs the act the parked subject
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

    if (trigger == DialogTrigger::CLOSE_WINDOW) {
        if (k == 's' || k == 'r') {
            const bool ok = save_ops.save();
            if (!ok) {
                // A NEW QUESTION, so it goes through the same raise route and
                // clears the painted bit with it: the new set is answerable
                // only once this text and these buttons have been painted.
                // That is what closes the stale-rect press this rung used to
                // leave answerable (the reasoning is at PromptState).
                // EVERY PROMPT IS A QUESTION (architect 2026-09-01, the
                // capitalization sweep): this rung read "Save failed." — the
                // one statement-with-a-period among prompts that ask
                // ("Save unsaved changes?", "Load '…' in place?") — and now
                // asks what its buttons answer. The response set is untouched.
                // IT CARRIES NO REASON AND RAISES NO CARD (2026-09-02): the
                // save owner cards WHICH file it could not write at the arm
                // that met the fault (save_ops.cpp), so this rung inherits
                // that sentence and asks the one thing only it knows to ask.
                app.prompt.present("Retry the failed save?",
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

// The load confirmation's programmatic abandon (contract at the declaration).
// It reaches the SAME body Esc reaches — the prompt down, both parked subjects
// dropped through cancel_load_in_place — so the asynchronous edge and the
// keyboard's own Cancel cannot leave different state behind.
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
// otherwise proceed immediately. Centralizes the decision so Ctrl+Q, the
// WM-close callback and the Open project picker's open act share identical
// behaviour; what differs between them is the target, seated here for
// proceed.
void GuiPrompt::request_close(GuiCloseTarget target) {
    if (app.prompt.active) return; // already gated; ignore re-entry
    // Not while a synchronization is running (architect 2026-09-04), and the
    // question is asked here, above every closing step below, because a
    // refused close must leave every surface standing: the render player still
    // up, the picker still up, the editor still holding its uncommitted text.
    // Three roads run a prelude of their own before they reach this line, and
    // each prelude is that road's own act rather than a closing step of the
    // quit's — the drag hatch finalizes the active drags (a drag's end is "any
    // end commits", the pointer-gesture rule), the paste-confirm hatch cancels
    // its confirmation, and the compositor's close callback closes a standing
    // dropdown — so a refusal under any of the three leaves the window as that
    // prelude left it and takes down no surface of the quit's own. The gate
    // belongs on this road for the same reason those steps do — it is the one
    // road Ctrl+Q, the compositor's close, the tablet's BACK and the picker's
    // reopen all pass through, so no producer
    // of a quit can miss it and none restates it. The predicate, the card and
    // the reasoning are the input handler's one body
    // (close_refused_by_external_sync); this line only asks.
    if (input != nullptr && input->close_refused_by_external_sync()) return;
    // THE RENDER PLAYER COMES DOWN FIRST, on every road into this one: the
    // mode's transport is stopped, the view's buffer rebound and the overlay
    // cleared before the question is asked, so a Cancel leaves the ordinary
    // window rather than a player the user cannot see the prompt through. It
    // lives here rather than in the keyboard's own Ctrl+Q arm because the
    // compositor's close (main.cpp's set_on_close) arrives without a key and
    // must take the identical step — one road, one closer. A no-op when the
    // mode is down, which is every other close.
    render_player.close();
    // AND SO DOES A STANDING PICKER (2026-08-28), the same shape one mode
    // over — one close body, close_picker, which takes the overlay's band
    // down with it; idempotent, so the road that already closed it — the
    // Open project picker's own reopen — pays nothing.
    if (input != nullptr) input->close_picker();
    // AND A STANDING AV SYNC STATS PANEL (2026-09-03), the same shape one mode
    // further — one close body, close_stats_panel, which disarms the display
    // measurement and takes the band down with it, so no road out of the
    // window can leave the instrument running. Idempotent like the two above.
    if (input != nullptr) input->close_stats_panel();
    // AND EVERY STANDING MODAL EDITOR COMES DOWN WITH IT, uncommitted, for the
    // identical reason and on the identical road: the keyboard's Ctrl+Q used
    // to do this itself, editor by editor, and the COMPOSITOR'S CLOSE — which
    // carries no key — did not, so a WM close over a standing editor asked the
    // unsaved-work question through it, back on screen at Cancel. The step is
    // the input handler's (each editor's own exit body, and at most one can
    // stand), stated once at close_modal_editors_no_commit and idempotent.
    if (input != nullptr) input->close_modal_editors_no_commit();
    close_target_ = target;
    if (app.dirty)
        open_unsaved(DialogTrigger::CLOSE_WINDOW);
    else
        proceed(DialogTrigger::CLOSE_WINDOW);
}
