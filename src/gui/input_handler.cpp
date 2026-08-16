#include "input_handler.h"

#include "engine/engine_geometry.h"  // kN
#include "gui_display_context.h"
#include "paint_handler.h"
#include "render.h"
#include "settings_io.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "warpmarkers.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Keyboard input handler event entry points (on_key — the press router — and
// dispatch_key_command, the one ranked command dispatch behind it; the pointer
// handlers; on_wheel), dispatching into the operation structs through the
// reference members (warpops, phase_resets, flag_editor, renders_dir,
// active_views, playback_lifecycle, save_ops, prompt, selection, undo,
// viewport). compute_base_tempo_scale + BaseTempoScale live in input_handler.h
// so this TU can reach them; render_bpm_sweep() is the sole caller.

// THE PRESS ROUTER (architect 2026-08-16): outside a text editor the press
// only ARMS a key identity and the RELEASE runs the one command dispatch —
// the model's full statement is at this function's declaration
// (input_handler.h); the release half is on_key_release
// (input_key_dispatch.cpp).
void GuiInputHandler::on_key(GuiKey key, GuiInputState mods) {
    if (mods.synthesized_repeat) {
        // THE REPEAT RULE: the stream claims the release — the hold's first
        // repeat unarms the key, so the release after a hold resolves
        // nothing. That first repeat is the HOLD'S OPENER, standing in for
        // the press act the key no longer performs, and it dispatches with
        // the repeat bit CLEARED so undo coalescing sees exactly the
        // pre-model world — a "physical" opener (arrival-invalidate,
        // tap-window/subject rules, its own entry) followed by
        // identity-merging repeats (the argument is at
        // Undo::coalesce_gesture). An editor-typing repeat's key never armed
        // — the island below dispatches its press without arming — so its
        // opener test is false by construction and the island's stream keeps
        // the bit, byte-identical to before the model.
        const bool opener = unarm(key);
        if (opener) mods.synthesized_repeat = false;
        dispatch_key_command(key, mods, KeyDispatchPhase::Full);
        return;
    }
    // THE CONTEXT BLOCK — "the user acted" events, which stay at the PHYSICAL
    // press. Each is idempotent, and each re-runs at the top of
    // dispatch_key_command (a short re-run block there says why); the four
    // comments here are the authoritative statements.
    //
    // Double-click lifecycle, KEYBOARD half: any keyboard press between two
    // clicks breaks EVERY pending double-click candidate (TrimBar, Marker,
    // EmptyLane alike) at this one chokepoint — no legitimate double-click types
    // a key between its two presses, and a cross-context consume (seed a
    // candidate, run a command, click again to consume in a different context)
    // must not fire. The consume lives entirely in on_button_press (nothing on
    // the keyboard path reads the candidate), and the dispatch re-running the
    // clear — at a release or a synthesized repeat — is
    // equally fine: no candidate can survive a held or released key. The
    // pointer-side
    // per-branch clears (the on_button_press top-of-frame clear, the moved-drag
    // clears, the force-end finalizer's clear) stay: they own the pointer half of the
    // lifetime; this owns the keyboard half, and on_wheel owns the wheel half
    // (the same clear at its own entry).
    app.double_click = DoubleClickCandidate{};
    // ANY KEY PRESS HIDES THE HOVER TOOLTIP, the keyboard half of the rule the
    // pointer press and the wheel already carry: the hint says what the button
    // under the pointer would do, and once the user has acted — by any means — it
    // is stale advice left floating. It is what keeps a hint from standing over a
    // MODAL the key just opened, advertising a chord that modal's gate now
    // swallows, and it covers the reverse timing too by resetting the dwell, so a
    // dwell still counting when `;` opened the settings editor never comes due.
    // (The tooltip cannot come BACK under that modal: the dwell writer refuses to
    // run one while a prompt or a keyboard-modal editor is up — the rule is at
    // recompute_redesign_button_hover.) The HOVER PILL needs nothing from this
    // site: THE DIALOG'S VEIL already owns it (recompute_redesign_button_hover,
    // input_pointer.cpp) — under a PROMPT or an EDITOR dialog alike every
    // roster face goes dark, one blanket answer since the modal-trap
    // reach-through's retirement, so a modal opened by this key
    // cannot leave a lit pill behind it. The pointer-transparent FLAG editor
    // raises no veil and needs none: its roster presses were never blocked.
    hide_shift_tooltip();
    // ANY KEY PRESS ALSO ENDS THE MENU ROW'S MODE, the keyboard half of the same
    // blanket rule at the top of on_button_press. It needs no exception list for
    // the reason stated there and one more: no keyboard chord opens a dropdown at
    // all, so nothing here has to survive. The bare Esc and Ctrl+Q the ruling
    // names as dismissals are covered by this without being enumerated, and so is
    // every key that opens a modal. Gated inside disarm_menu_row: with a popup
    // OPEN this is inert and the popup's own keyboard gate in the dispatch
    // decides.
    disarm_menu_row();
    // A PHYSICAL KEY ARRIVAL ENDS A HELD BUTTON'S REPEAT BURST — the first of
    // the burst's two key edges (the other is the general keyup dispatch's own
    // top; the authoritative inventory is at AppState::ChromePress). It is
    // LOAD-BEARING FOR UNDO rather than hand-feel: Undo::coalesce_gesture
    // merges a synthesized repeat by KIND ALONE with no subject test, on the
    // premise that no command can run between a burst's opener and the repeats
    // behind it, and these two edges are what make that true for a held BUTTON
    // exactly as maybe_fire_repeat's layer-1 disarms make it true for a held
    // KEY. Only the ARM's schedule dies: the arm itself is the pointer's and a
    // key press does not end a finger's hold, so the lift still runs the act
    // this burst had not yet suppressed. Synthesized arrivals are excluded by
    // the early return above, and correctly — the platform kills its own key
    // hold at any pointer-button press, so no key repeat can arrive under a
    // held button at all. (The platform's OWN key repeat keeps its layer-1
    // disarms inside maybe_fire_repeat, untouched.)
    app.chrome_press.repeat_due_ms = 0;

    // Transient bottom-strip status message clears on every real
    // keypress, including the press whose release may set a new message
    // (the handler that sets it does so at the very end of its branch in
    // the dispatch, after the re-run of this clear there). Guarded so the
    // bottom-strip invalidate fires only when there was a message to
    // erase. See AppState::transient_status_message.
    if (!app.transient_status_message.empty()) {
        app.transient_status_message.clear();
        viewport.invalidate_status_chain_area();
    }

    if (app.prompt.active) {
        // The painted gate's PRESS half: a press aimed at an unseen surface
        // is DEAD, not deferred — it arms nothing, so its release resolves
        // nothing (the gate's whole rule is at PromptState, app_state.h; the
        // dispatch keeps the release-side half for the armed-before-paint
        // corner recorded at the model statement).
        if (!app.prompt.painted) return;
        // Bare Enter/Space on a focused prompt button press it DOWN at the
        // press — the ring's arming half (press_modal_ring_arm); the act
        // stays at the release through the modal arm, exactly as shipped.
        if (press_modal_ring_arm(key, mods)) return;
        // Answers, walks, Esc — all at the release, through the dispatch's
        // prompt gate under live gates, on the chord this press commits (the
        // letter answers compare the codepoint, which the arm carries).
        arm(key, mods);
        return;
    }
    if (keyboard_modal_editor_active()) {
        // THE KEYDOWN ISLAND: the pre-model press-time behavior whole —
        // including the editor_text_drag gate, the keyboard-modal editor
        // gate, and the ring's own press arm inside — the surface rule and
        // the OSK reasoning are at the model statement (on_key's
        // declaration, input_handler.h).
        dispatch_key_command(key, mods, KeyDispatchPhase::Full);
        return;
    }
    // Dropdown, blank/loading, and every command: this press commits the
    // chord and the release runs it, under live gates.
    arm(key, mods);
}

// The armed set's three writers (the model is at on_key's declaration; the
// set's contract at armed_keys_, input_handler.h).
void GuiInputHandler::arm(GuiKey key, GuiInputState mods) {
    for (ArmedKey& a : armed_keys_) {
        if (a.key == key) {
            // A re-press overwrites the stash: the newest press's chord is
            // the committed one, and it is what the release will dispatch.
            a.ctrl      = mods.ctrl;
            a.shift     = mods.shift;
            a.alt       = mods.alt;
            a.codepoint = mods.codepoint;
            return;
        }
    }
    armed_keys_.push_back(
        ArmedKey{key, mods.ctrl, mods.shift, mods.alt, mods.codepoint});
}

bool GuiInputHandler::unarm(GuiKey key, ArmedKey* stash) {
    for (auto it = armed_keys_.begin(); it != armed_keys_.end(); ++it) {
        if (it->key == key) {
            if (stash) *stash = *it;
            armed_keys_.erase(it);
            return true;
        }
    }
    return false;
}

void GuiInputHandler::clear_armed_keys() {
    armed_keys_.clear();
}

// The prompt-gate press half of the ring's Enter/Space act (contract at the
// declaration): bare-exact Return/Space with the live focus on a button arms
// the pressed face through the ring's own arm, at Press.
bool GuiInputHandler::press_modal_ring_arm(GuiKey key, GuiInputState mods) {
    if (mods.ctrl || mods.shift || mods.alt) return false;
    if (key != GuiKeys::Return && key != GuiKeys::Space) return false;
    if (modal_dialog_focus_live() < 0) return false;
    return route_modal_dialog_focus_key(key, mods, KeyDispatchPhase::Press);
}

// THE ONE COMMAND DISPATCH — the whole ranked key body, on_key itself until
// 2026-08-16, renamed verbatim when the press/release split landed (the model
// is at on_key's declaration, input_handler.h). Entered at Release by
// on_key_release for an armed physical key, and at Full by the keydown island,
// the synthesized-repeat path, and three synthetic chord dispatches (the
// chrome lift, the chrome button hold-repeat, and the dropdown item,
// input_pointer.cpp).
void GuiInputHandler::dispatch_key_command(GuiKey key, GuiInputState mods,
                                           KeyDispatchPhase phase) {
    // THE CONTEXT BLOCK'S RE-RUN. The press router (on_key) runs these four at
    // every physical press — the authoritative comments are there — and they
    // run again here because each is idempotent and this body is also entered
    // WITHOUT a physical press directly under it: the synthetic chord
    // dispatches (the chrome lift and the dropdown item) relied on them when
    // this body WAS on_key and must stay byte-identical.
    app.double_click = DoubleClickCandidate{};
    hide_shift_tooltip();
    disarm_menu_row();
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    if (!app.transient_status_message.empty()) {
        app.transient_status_message.clear();
        viewport.invalidate_status_chain_area();
    }

    // The modal prompt (painted on the bottom row since 2026-08-13) owns input while
    // active. Only the prompt's
    // own response keys do anything; everything else is swallowed so
    // marker edits / playback / viewport keys cannot sneak in while
    // the prompt is up. Delete and Escape map to sentinel chars '\x7f' and
    // '\x1b' so they participate in the same vector<char> match as letter
    // responses. The dialog's BUTTONS answer too — the pointer's press claim
    // (input_pointer.cpp) calls the same activate_response — but the keyboard
    // half here is byte-identical to the bottom-strip era.
    // EVERY response — letters, Delete, Escape alike — matches BARE ONLY
    // (architect 2026-07-28): no ctrl, no alt, and no shift. That is what stops
    // Ctrl+S from picking the Save answer in the close prompt and Alt+Y from applying
    // a confirmed paste.
    // CASE-SENSITIVITY IS THE CODEPOINT'S JOB, NOT !shift's (architect 2026-07-30):
    // the platform case-folds letter keysyms, so the GuiKey says `y` for every way
    // of typing a Y, and the old `!shift` spelling let CAPSLOCK deliver a
    // visually-uppercase Y that still answered the Yes response — the exact outcome the
    // case-sensitivity was there to forbid. `mods.codepoint` is the true character
    // under the live keyboard state (xkb_state_key_get_utf32 at the platform
    // boundary, shift AND lock applied), so the letter arm reads THAT: a capital Y
    // never matches a lowercase response key, however it was produced. The bare-only
    // gate stays as the modifier rule it always was, and the Delete / Escape
    // responses keep matching on the GuiKey (they carry no case and no codepoint
    // worth reading). (The bracket-accelerator LABELS this match once shipped
    // beside — "[S]ave", pacman's Y/n convention — went out with the
    // bottom-strip prompt line on 2026-08-12, came back with the row hours
    // later and are RETIRED AGAIN, with their reason recorded, on 2026-08-13:
    // the responses are BUTTONS wearing plain words that name their key on a
    // TOOLTIP, PromptState's declaration owning the label rule. THE MATCH HERE
    // WAS UNCHANGED BY ALL THREE, deliberately.)
    if (app.prompt.active) {
        // THE PAINTED GATE (2026-08-13): a prompt the user has not SEEN
        // answers nothing. One dispatch batch arrives whole before the loop
        // paints, so a key queued behind the raise itself (Ctrl+Q tearing an
        // editor down and raising the unsaved-work prompt, Delete right behind
        // it) was answering a question that had never been on screen. EVERY
        // key is consumed while the bit is false — the response letters, Esc,
        // Delete, and the Ctrl+Q hatch below with them: a consumed no-answer is
        // the only safe reading of input aimed at an unseen surface, and
        // nothing is lost that a second press after the paint does not
        // recover. The bit's writer is paint_modal_dialog's prompt branch and
        // the whole rule lives at PromptState (app_state.h). Since the keyup
        // model this line is the rule's RELEASE-SIDE half — the press router
        // holds the press half (a press aimed at an unseen surface arms
        // nothing); what reaches here unpainted is the recorded rare corner,
        // a release whose press predates the prompt.
        if (!app.prompt.painted) return;
        // PASTE_CONFIRM only: Ctrl+Q abandons the pending paste (the real
        // cancel, not a synthesized Esc) and then runs the normal close
        // path. The unsaved-work dialog (CLOSE_WINDOW) deliberately falls
        // through to the modal swallow below and keeps blocking this chord.
        if (app.prompt.trigger == DialogTrigger::PASTE_CONFIRM &&
            ctrl && !shift && !alt && key == GuiKeys::Q) {
            prompt.cancel_paste_confirmation();
            prompt.request_close();
            return;
        }
        // THE FOCUS RING (2026-08-13), the prompt's half: bare Tab and bare
        // Left/Right walk the answer buttons, and bare Enter or bare Space
        // presses the focused one down and commits it at the key's release. It
        // is ranked here — under the painted gate, over the response match —
        // because it is navigation and activation over an unanswered question
        // and must obey the same "only a surface the user has seen" rule the
        // answers do.
        // A PROMPT IS RAISED WITH PASSIVE FOCUS ON ITS LAST BUTTON since later
        // that day, SUPERSEDING this gate's own "a prompt opens with no button
        // focused, so a stray Enter cannot answer": Enter DOES answer now, and
        // what makes it safe is that the last button is the ESCAPE SENTINEL
        // plus the painted gate directly above (PromptState carries the
        // supersession in full). The route is shared with the editor dialogs' —
        // one ring, one owner (route_modal_dialog_focus_key,
        // input_key_dispatch.cpp); a prompt has no field, so the editors'
        // completion-first Tab arm has no counterpart here and this call is the
        // whole of the prompt's ring. The response letters below are untouched,
        // so answering by letter, Delete or Esc is byte-identical to before the
        // ring. The phase passes through whole: Press never arrives here
        // (press_modal_ring_arm calls the ring directly), and the arm's own
        // Release refusal is stated at the arm.
        if (route_modal_dialog_focus_key(key, mods, phase)) {
            return;
        }
        char k = 0;
        if (!ctrl && !shift && !alt) {
            if (mods.codepoint >= 'a' && mods.codepoint <= 'z') {
                k = static_cast<char>(mods.codepoint);
            } else if (key == GuiKeys::Delete) {
                k = '\x7f';
            } else if (key == GuiKeys::Escape) {
                k = '\x1b';
            }
        }
        if (k != 0) {
            for (char rk : app.prompt.response_keys) {
                if (k == rk) {
                    prompt.activate_response(rk);
                    return;
                }
            }
        }
        return;
    }

    // THE OPEN DROPDOWN IS KEYBOARD-MODAL, in the editors' shape and ranked
    // directly under the prompt (a prompt still wins: it is the older modal and
    // Ctrl+Q below can open one). While the popup is up NO COMMAND RUNS — the
    // gate swallows every chord it does not name, which is the whole point: a
    // popup that let `s` drop a marker underneath it would be a trap.
    //
    // It admits exactly two keys, and both DISMISS:
    //   - bare Esc CLOSES it, and that is THE SIXTH BARE-ESC BINDING (the
    //     enumeration further down carries the full list and this rank). ONE
    //     BINDING FOR EVERY MENU: the gate reads the shared popup state, so the
    //     Navigation dropdown (2026-08-02) and the File one (2026-08-13) joined
    //     the existing binding rather
    //     than adding a seventh — a dropdown is a dropdown — and the Navigation
    //     one's deletion (2026-08-15) took none away for the same reason;
    //   - Ctrl+Q closes it and FALLS THROUGH so the ordinary close route runs
    //     below, matching every other modal's Ctrl+Q hatch.
    // A popup and an editor CANNOT be open together, so this gate can never
    // contend with route_modal_editor_key — and the claim rests on TWO
    // mechanisms, one per class. The popup opens only from row 1, and while a
    // DIALOG editor is up the press that would open it dies at the dialog's
    // veil in on_button_press, which since 2026-08-13 swallows the roster
    // whole. The pointer-transparent FLAG
    // editor swallows nothing, so instead the open ENDS it: toggle_dropdown's
    // open path discards the edit, exactly as a press outside its box does. The
    // reverse direction is this gate's own doing — `;` is swallowed here, so no
    // editor opens under a popup either.
    if (app.dropdown.open()) {
        if (dropdown_key_blocked(key, mods)) return;
    }

    // Blank / loading state: only the quit / close-gesture bindings run;
    // everything else no-ops. Dialog can't fire here because dirty is
    // always false in blank state (the only blank state is the transient
    // pre-load window before the sole startup load completes, when nothing
    // has been loaded or edited yet).
    //
    // Loading is a BLOCKING, UNINTERRUPTIBLE phase: the run loop is suspended
    // for the whole synchronous decode/pyramid/install, so a Ctrl+Q or WM
    // close pressed during loading is not observed here at all — it is read
    // when run() resumes after the load completes, and the deferred quit is
    // then honored (the app quits on completion). This gate's Ctrl+Q admission
    // is for a close queued before/around load, which still yields that
    // deferred quit; an urgent abort is pkill / the compositor's force-close.
    if (app.loading || audio.total_frames() <= 0) {
        if (ctrl && !shift && !alt && key == GuiKeys::Q) {
            prompt.request_close();
        }
        return;
    }

    // Editor text-selection drag modal gate. This sits ABOVE the
    // text-editor handlers because the flag / settings editor is active
    // during its own selection drag, and those handlers would otherwise
    // tear the edit down and dispatch a command on any command key; this
    // gate must intercept first so the drag owns the keyboard like every
    // other drag. Swallow every key that is not an escape hatch. For an
    // escape hatch, finalize the selection first (reading the still-live
    // editor geometry) and then fall through with no return, so the
    // editor and global handlers below run Esc (cancel the edit) or
    // Ctrl+Q (tear the edit down, then open the close prompt) exactly as
    // they would with no drag in flight. A text drag is not a navigation
    // gesture, so it gets no bare-`s` carve-out. BOTH hatches are
    // modifier-exact: a modified Escape has no binding anywhere, so it is
    // swallowed here like any other key rather than ending the drag.
    // THIS DRAG KEEPS ITS Esc HATCH while the POINTER gestures below lost theirs
    // (they have no cancel at all — the rule is at the drag-modal gate): the hatch
    // FINALIZES the text selection, restoring nothing, and the Esc it falls
    // through to belongs to the EDITOR's own modality (discard the buffer), which
    // is a different thing from cancelling a gesture.
    // (THE KEYBOARD OWNS NO CURSOR RE-RESOLVE, 2026-08-03. A scope guard stood
    // here, armed by the two routes that end a gesture from a key — the text-drag
    // hatch immediately below and the pointer gestures' Ctrl+Q hatch in the
    // drag-modal gate — and paid at whatever return the route took, because the
    // refresh could not run where the gesture ends: both routes fall onward, and
    // the editor is still open one line later, so the map would answer with the
    // modal's own Arrow. The whole difficulty was an ordering problem, and the
    // per-iteration cursor owner dissolves it: this handler is called from a
    // dispatched key event, and the loop's tail re-resolves after the entire call
    // has returned — past every editor close, every prompt raise and every
    // teardown any route below performs.)

    if (app.editor_text_drag.active) {
        const bool escape_hatch =
            (!ctrl && !shift && !alt && key == GuiKeys::Escape) ||
            (ctrl && !shift && !alt && key == GuiKeys::Q);
        if (!escape_hatch) return;
        finalize_editor_text_drag();
        // THE HATCH IS A GESTURE END: this drag holds the cursor down to the
        // Arrow through any_pointer_gesture_active, so pressing Esc over the
        // waveform must come back showing the Pan
        // — which it does at this iteration's tail, once the editor
        // below has closed and everything else this call does has settled.
        // (The TOP FLAG EDITOR is in it too, by a different route: it is
        // pointer-transparent, so its own modality never forced the Arrow — the
        // live text drag alone did, through the same gesture refusal.)
        // fall through: the editor handler below runs Esc (cancel the
        // edit) or Ctrl+Q (tear the edit down, then the close prompt
        // opens) exactly as with no drag in flight.
    }

    // KEYBOARD-MODAL EDITOR GATE. While ANY editor is open — the three
    // single-State dialog ones (settings, load, and the commit title since
    // 2026-08-07), the bpm bracket (a dialog too), and the top-strip flag editor
    // (architect 2026-07-28, which brought the last of them in) — only the keys
    // the editor itself consumes plus bare Esc, Ctrl+S, and Ctrl+Q get through
    // (modal_editor_key_blocked); everything else — playback, navigation, zoom,
    // mode toggles, tab switches, undo/redo, marker / trim chords, the
    // Ctrl+Alt render chords — drops HERE, silently, so no authoring or view
    // change can happen while an edit is up. Admitted keys route through the
    // editor blocks below.
    // This gate is what makes an unbound chord STRUCTURALLY inert rather than
    // merely unlisted: nothing downstream can see it, so nothing downstream can
    // tear an edit down on its way to a command that does not exist.
    // Modality is about CHORDS only: the wheel still punches through a flag
    // editor (navigation), which rides modal_dialog_editor_active rather
    // than this predicate, and opening a flag editor still does not stop
    // playback — that one rides no predicate at all, each modal surface
    // spelling its own stop at its open site.
    // Sits after the text-drag gate so an in-flight editor selection drag keeps
    // owning the keyboard exactly as before.
    if (keyboard_modal_editor_active() &&
        modal_editor_key_blocked(key, mods)) {
        return;
    }

    // The top-flag editor owns the keyboard while active. Only the three keys
    // the gate above admits past an open editor arrive here — its own editor
    // keys, Ctrl+S, and Ctrl+Q — so this block cannot see a command. Routes
    // BEFORE the render/batch Esc cancel so bare Esc closes the edit
    // first; Esc with no active edit falls through to the rest. Returning false
    // (Ctrl+Q only) leaves the edit already torn down and lets this dispatch
    // run the close routing.
    if (text_editor::is_active(app.top_flag_editor)) {
        if (handle_top_flag_editor_key(key, mods)) return;
    }

    // Settings-prompt editor (`;` opener). Same shape as the flag-editor
    // block above. The two editors are mutually exclusive in practice
    // because the flag editor's block returns early while it owns the
    // keyboard, so a stray `;` can't open settings over a live flag
    // edit. Routed before the render/batch Esc cancel so Esc
    // closes the edit first.
    if (text_editor::is_active(app.settings_editor)) {
        if (handle_settings_editor_key(key, mods)) return;
    }

    // Load prompt editor (bare `'` opener). Same modal shape as the
    // settings editor block above; the two are mutually exclusive in practice
    // (each opener no-ops while the other owns the keyboard). Routed before the
    // render/batch Esc cancel so Esc closes the edit first.
    if (text_editor::is_active(app.load_editor)) {
        if (handle_load_editor_key(key, mods)) return;
    }

    // Commit-title editor (Ctrl+S in the `h` history view). Same modal shape
    // as the two blocks above, and mutually exclusive with them by construction:
    // its opener is a chord the keyboard-modal gate drops while any editor is
    // open, and the view's own allowlist admits no other opener.
    if (text_editor::is_active(app.commit_title_editor)) {
        if (handle_commit_title_editor_key(key, mods)) return;
    }

    // Ctrl+C copies the FOCUSED marker's resolved effective tempo — the
    // pasteable "base" / "base*scale" form the flag editor accepts — onto the
    // SYSTEM clipboard, so the implied value of a pass or label ref pastes into
    // a neighbour's flag editor and equally into any other application. The
    // clipboard has ONE representation, the platform's; this composes the
    // string and hands it straight over, holding no copy of its own.
    //
    // THE SELECTION TRANSLATION of the old hover copy (row 5, 2026-08-01). The
    // payload used to be cached in hover_popup at each rect-entry and this
    // binding fired only while a hover readout showed; the hover machinery is
    // gone, so the subject is the LAST-SELECTED marker — the same subject the
    // bottom-strip readout now names, resolved through the same eligibility gate
    // and the same frozen composer, so what you see is what you copy. An EMPTY
    // SELECTION (or an ineligible focus — an owner, a phase reset, iteration
    // mode, the 'P' column) is a CONSUMED NO-OP: the payload comes back empty
    // and nothing is written. Ctrl+C is otherwise unbound globally.
    //
    // LIVE-TEST FLAGGED, like the readout it follows.
    //
    // Placed below the prompt gate (the line above returns while a modal is up)
    // and the two editor blocks (which return on their own Ctrl+C, keeping the
    // editor's copy-selection working while an editor owns input), so reaching
    // here means neither a modal nor an editor is active.
    if (ctrl && !shift && !alt && key == GuiKeys::C) {
        if (popup_eligible_marker(app, app.last_selected_marker)) {
            std::string payload;
            compute_hover_popup_text(
                slice_to_warp_markers(app.warpmarkers.markers()),
                app.last_selected_marker, audio.sample_rate(),
                audio.total_frames(), &payload);
            if (!payload.empty()) gui.clipboard_set_text(payload);
        }
        return;
    }

    // Drag-modal input: a pointer drag owns the keyboard exactly as the
    // prompt and the text editors above do. While any drag gesture is in
    // flight, EVERY hotkey is swallowed — Esc included.
    //
    // POINTER GESTURES HAVE NO CANCEL (architect 2026-07-29). The rule, stated
    // once here because this gate is the one chokepoint every gesture's keyboard
    // story passes through; every gesture struct and release body carries only its
    // own class plus a pointer back:
    //   * ESC MID-GESTURE IS A CONSUMED NO-OP — it does not stop, abandon or
    //     revert anything, and the gesture continues under the pointer exactly as
    //     if no key had been pressed;
    //   * RELEASE ENDS THE GESTURE AND WHAT STANDS STANDS — the release body
    //     commits: the proposed marker position, the live trim bounds, the region as
    //     extended (the tempo drag's already-written cents left this list when
    //     the whole tempo drag was deleted);
    //   * BUTTON-LOST ENDS IT THE SAME WAY (the !primary_button_held arms in
    //     on_motion all route to the release bodies);
    //   * UNDO IS THE MITIGATION — a drag implies "I am ready to commit", and the
    //     entry each release path pushes is what takes it back. Trim is the one
    //     gesture with no undo entry to offer, which is trim's standing
    //     history-less ruling, not a cancel gap.
    // So no gesture captures pre-gesture bystander state anywhere: the selection
    // snapshots, the grab-playhead captures and the pre-drag region copies are all
    // deleted, along with cancel_active_drags itself.
    // CTRL+Q IS NOT A CANCEL EITHER — it ENDS the gestures through their release
    // bodies (finalize_active_drags, input_pointer.cpp) and then opens the close
    // flow, so nothing is left live under the prompt to commit on a later motion
    // if the prompt is dismissed. main.cpp's resize and WM-close callbacks call
    // the same finalizer for the same reason.
    // THAT A WINDOW RESIZE OR A WM CLOSE MID-DRAG THEREFORE COMMITS the gesture is
    // ARCHITECT-ACCEPTED (2026-07-29: "not a real use case - do whatever is easiest
    // to code and has least loopholes"), and the ruling is the reason to keep this
    // shape rather than grow a third behaviour for it: routing every force-end
    // through the SAME release bodies is both the least code and the fewest
    // loopholes, since no path can then leave a gesture half-ended.
    // This single gate is why no downstream hotkey needs its own
    // drag guard: Tab, undo, `t`, and the rest never see a key mid-drag.
    // The editor text-selection drag has its own modal gate above
    // the text-editor handlers; the pointer gestures here — the marker /
    // trim / region drags, the one nav drag and its pending
    // click (scroll_drag — one state for the pending, the pan and the ctrl
    // zoom phase since 2026-08-14; the dual-axis STRIP drag was a member here
    // until its deletion, 2026-08-15),
    // the overview lane's box drag (overview_drag — the pending outside press,
    // the pan and the edge
    // drags, one state for the pending and moved phases since the lane
    // rework the same day),
    // THE STANDING REGION'S OWN EDITOR (region_edit_drag — the move and the two
    // bound drags, 2026-08-15: it must swallow chords for the same reason the
    // region former does, its span being live under the pointer, and because an
    // unmoved press there still owes the deferred click act at its release),
    // and the
    // MARKER FLAG'S PENDING CLICK, THE SWEEP'S LAST FOUR PENDING ACTS
    // (pending_click — the trim bar's two bound sets and its framing
    // double-click, the empty lane's create double-click and the `h` view's
    // three diff-flag clicks, 2026-08-15) plus the pending trim drag (a press
    // held before it resolves) —
    // are mutually exclusive with it. The marker pending is on this list for a
    // SECOND reason since 2026-08-15, scroll_drag's own: the whole flag click
    // runs at the LIFT now, reading the selection, the focus and playback state
    // there, so no chord may move any of it in between. The four above are here
    // for that same second reason and it is sharper for them: each act re-asks
    // its gates LIVE at the lift — the bound set's strictly-inside partner test,
    // the create's read-only and home-view refusals, the mode's own flag list —
    // so a chord that moved a trim bound, locked the tab, switched the view or
    // stepped the walk between press and release would have the lift decide
    // against a state the user never pressed on.
    // scroll_drag belongs on the list too: a live
    // pan must swallow authoring keys rather than letting one run over a latched
    // pan — and its PENDING phase must, because the deferred click act reads
    // playback state at the release and no command may move it in between.
    // (The scrub is a one-shot ACT, not a gesture, so it has no entry of its
    // own — but since 2026-08-13 its press arms scroll_drag like every other
    // press on the navigation surface, which is what keeps a chord from moving
    // playback between a lower-half press and the release that auditions. The
    // tempo drag and its pending were entries until
    // 2026-07-29, when the whole tempo drag was deleted — see marker_drag.h.)
    if (app.drag.active || app.trim_drag.active ||
        app.region_drag.active || app.region_edit_drag.active ||
        app.scroll_drag.active || app.overview_drag.active ||
        app.pending_marker_press.active || app.pending_click.active() ||
        app.pending_trim_drag.active) {
        // The ONE hatch left, modifier-exact (a modified Ctrl+Q has no binding
        // anywhere): end the gestures as their release would, then run the close
        // flow. Bare Esc takes the swallow below with every other key.
        if (ctrl && !shift && !alt && key == GuiKeys::Q) {
            finalize_active_drags();
            prompt.request_close();
            // THE CURSOR RE-RESOLVES AFTER THE FORCE-END, and nothing here has
            // to arrange it: the finalizer cleared the gesture state
            // pointer_cursor_kind reads and request_close may have raised the
            // prompt it reads next, and the loop's per-iteration owner answers
            // after both — after this whole call, in fact, so a later edit below
            // cannot get the ordering wrong.
            return;
        }
        return;
    }

    // THE `h` HISTORY MODE — its own keys, then its allowlist. Placed HERE,
    // directly under the drag-modal gate and directly over the read-only one,
    // and the position carries two facts rather than being a convenience.
    //
    // (1) EVERY ENTRY REFUSAL THE MODE NEEDS HAS ALREADY RUN. A prompt, an open
    // dropdown, loading-or-absent audio, the editor text drag, any of the four
    // keyboard-modal editors and any live pointer gesture all swallow the key
    // above this line, so `h` cannot open the mode in any of those states and no
    // predicate is re-tested here to say so. The full statement is at
    // handle_history_mode_key.
    //
    // (2) THE READ-ONLY BIT IS IRRELEVANT TO THE MODE, and being above that gate
    // is what makes it so: the mode gates by itself, in both tabs and both
    // views, so the mode's whole vocabulary never meets the read-only allowlist
    // and needs no entry in it. A locked tab reads history exactly as a writable
    // one does — it is a viewer either way. (The read-only allowlist happens to
    // admit the same navigation shapes anyway, but that is its own answer for
    // its own reason, not a dependency of this one.)
    if (handle_history_mode_key(key, mods)) return;
    if (app.history_mode.active &&
        history_mode_key_blocked(key, mods, app)) {
        return;
    }

    // Per-tab read-only keyboard gate: a permitted-keys allowlist that filters
    // out every AUTHORING chord — the marker stores and the engine settings are
    // what the lock protects (architect 2026-08-07) — while admitting
    // navigation, playback, view-switching, the close-prompt routing, the bare-o
    // toggle-off escape chord, and THE BAND, THE SAVE AND THE RENDER. Runs when
    // the active tab's ViewState carries read_only = true.
    //   - Bare o                 → toggle read-only off (escape chord)
    //   - Space (no mods)        → playback toggle
    //   - Left/Right (no mods)   → playhead-by-pixel step, and ONLY with an
    //                              EMPTY selection (the waveform lane): with one
    //                              the same press also carries the marker — the
    //                              marker-lane position nudge — which
    //                              is authoring and drops here
    //                              (playhead_in_marker_lane)
    //   - Home/End (no mods)     → playhead to trim region bounds
    //   - PageUp/PageDown        → viewport step scroll by the Alt-wheel
    //     (no mods)                step. Pure navigation, same family as
    //                              the playhead-step and Home/End entries.
    //   - =/- (no mods)          → zoom in/out
    //   - 0 (no mods)            → full zoom-out, else the `c` command
    //                              (run_overview_command)
    //   - f (no mods)            → follow mode toggle
    //   - c (no mods)            → focused-marker jump (when present) +
    //                              working zoom; with no focused marker,
    //                              working zoom centered on the playhead
    //   - t (no mods)            → S/T sub-view toggle
    //   - p (no mods)            → W/P sub-view toggle
    //   - 1/2/3 (no mods)        → the absolute view selectors (S+W / T+P /
    //                              T+W), which run exactly the two handlers
    //                              above
    //   - Tab/Shift+Tab/IsoLeftTab → cycle marker focus
    //   - Ctrl+Tab               → switch A/B tab (the other escape)
    //   - Ctrl+Shift+Tab         → march paired tabs in lockstep
    //   - Esc                    → the render/batch cancel (and the editor /
    //                              prompt closes); nothing else — the
    //                              selection/region ladder it used to serve here
    //                              is deleted, so a bare Esc with no render
    //                              running is a plain no-op
    //   - Ctrl+Q                 → close-prompt routing
    //   - Ctrl+S                 → the save (2026-08-07). It writes the state
    //                              the tab already holds and authors nothing —
    //                              and the close prompt's Save answer, which sits
    //                              above this gate, always did save from a
    //                              locked tab through the same owner
    //   - Ctrl+Alt+R,            → the renders (2026-08-07): the single render
    //     Ctrl+Alt+Shift+R          or the iteration sweep, and the
    //                              miscellaneous cell. A render READS the
    //                              authored state. (Save and Commit rode this
    //                              pair into the `h` view until 2026-08-08; it
    //                              is the Ctrl+S entry's now, on that entry's
    //                              own reasoning — it publishes what the tab
    //                              already holds)
    //   - x / Shift+X (no ctrl,  → the trim set-from-region and the maximizer
    //     no alt)                  (2026-08-07). Trim is BAND, not content
    // Authoring-mutation chords are BLOCKED at this gate, not admitted for a
    // deeper refusal: the marker / tempo / phase-reset drop / nudge /
    // status-toggle chords, Delete, `;` (the settings editor, whose engine-key
    // commits are authored content), `i`, `'`, the
    // propagate copy/paste (Ctrl+P and the Ctrl+Alt+P pair), and undo/redo
    // (Ctrl+Z / Ctrl+Shift+Z) all drop here. This gate is the ONLY read-only
    // guard on the keyboard path — and since 2026-08-07 the only one on the
    // POINTER path too has gone, the trim band's gate having been deleted with
    // the reclassification. The surviving deeper checks each cover a
    // surface it cannot reach — do_undo / do_redo's target-tab peek (the
    // ACTIVE tab is writable but the top history entry targets the other,
    // read-only tab; this gate tests only the active tab), and the per-gesture
    // pointer AUTHORING guards (a pointer gesture never passes through this
    // dispatch — the ONE pointer surface that does is the redesigned buttons,
    // which dispatch their chord through it precisely so this gate applies to
    // them unchanged: Undo and Redo drop here in a locked tab exactly as their
    // keys do, while Save and Render now pass exactly as theirs do). Full
    // rationale at read_only_key_blocked in input_key_dispatch.cpp.
    if (active_view_state(app).read_only &&
        read_only_key_blocked(key, mods)) {
        return;
    }

    // Keyboard authoring is HOME-VIEW gated, not view-blind: warp markers
    // author in source view, phase resets in target view, via the one
    // predicate active_column_authoring_allowed consulted at each
    // individual handler below (marker drop, status toggle, flag editor open,
    // etc.) beside the read-only check above — off home a handler still
    // dispatches here but refuses silently, navigation-class. The TWO ruled
    // exceptions: (1) the bare UP/DOWN TEMPO CENT STEP in W+target (owner-only
    // there, singleton and group) — the whole tempo surface since 2026-07-29, when
    // the family's other two flavors, the pointer tempo drag and the bare
    // Left/Right tempo-image step, were deleted (marker_drag.h), leaving bare
    // Left/Right in W+target a consumed refusal at the split below;
    // (2) the phase-reset propagate paste starts in source
    // view and lands in target through the `t` toggle chokepoint; and (3) since
    // 2026-08-07 the ITERATION-BRACKET WIPE in W+target, granted with the ruling
    // that iteration mode is target-legal (the authoritative inventory and the
    // argument are at active_column_authoring_allowed, app_state.h). (The
    // 2026-07-24 "third exception" — a both-views warp POSITION nudge — was
    // re-ruled away the same day: no warp position authoring in target view.)

    // Bare `t` toggles view-domain (S ↔ T). Placed before the marker /
    // phase reset edit handlers so the toggle wins over any future
    // bare-t binding; placed after the prompt / editor / queue gates so
    // those still own the keyboard when active.
    if (key == GuiKeys::T && !ctrl && !shift && !alt) {
        handle_active_audio_view_toggle();
        return;
    }

    // BARE 1 / 2 / 3 ARE ABSOLUTE VIEW SELECTORS (architect 2026-08-01): `1` is
    // S+W, `2` is T+P, `3` is T+W. They name a COMBINATION rather than flipping
    // an axis, so pressing the key for the combination you are already in is a
    // consumed no-op — that is the whole difference from `t` and `p`, which are
    // toggles. (S+P deliberately has NO key: phase resets author in target view,
    // so S+P is the one combination that is display-only on both axes, and the
    // architect gave the three keys to the three worth reaching directly. He
    // intends dedicated BUTTONS for these later, outside today's icon-row
    // layout; for now the keyboard is the whole surface.)
    //
    // COMPOSED, NEVER RE-SPELLED: each axis is applied by the very handler its
    // own key uses — handle_active_audio_view_toggle for S/T (bare `t`) and
    // GuiActiveViews::toggle_active_markers_view for W/P (bare `p`) — and only
    // when that axis actually differs. Every invariant those two own therefore
    // arrives by construction: the target-view entry validation and its error
    // notice, the domain translation of viewport / playhead / zoom, the flag
    // editor teardown and the iter wipe, the selection clear, the coincidence
    // auto-select, the region clear, and the synchronous plate rebuild. There is
    // no third view-switch route to keep in step with the other two.
    //
    // THE ORDER IS AUDIO FIRST, THEN MARKERS, and it is decided by the
    // COINCIDENCE AUTO-SELECT rather than by taste. Exactly one of the two
    // switches runs that scan — the W/P one (the entry chokepoint list is at
    // auto_select_marker_at_playhead; `t` is not on it) — and the scan converts
    // marker frames into the ACTIVE AUDIO DOMAIN before comparing them with the
    // playhead. So when both axes change, the scan must run after the audio
    // switch has translated the domain, or it would compare source frames
    // against a playhead about to be re-expressed in target frames and select
    // the wrong marker (or none). Audio-then-markers puts the scan last and
    // makes it read the FINISHED combination. The reverse order has no
    // compensating advantage: `t` performs no selection scan of its own that
    // would want the final column.
    //
    // A REFUSED AUDIO SWITCH STOPS THE WHOLE PRESS. Entering target view can
    // fail its validation gate (the error-notice class, unreachable from
    // program-written input), and half-applying an ABSOLUTE selector would leave
    // the user in a combination they did not ask for. The verdict is read off
    // the state the handler writes rather than through a new return value —
    // one owner, no signature change.
    if ((key == GuiKeys::Digit1 || key == GuiKeys::Digit2 ||
         key == GuiKeys::Digit3) && !ctrl && !shift && !alt) {
        const char want_audio   = (key == GuiKeys::Digit1) ? 'S' : 'T';
        const char want_markers = (key == GuiKeys::Digit2) ? 'P' : 'W';
        if (app.active_audio_view != want_audio) {
            handle_active_audio_view_toggle();
            if (app.active_audio_view != want_audio) return;   // refused
        }
        if (app.active_markers_view != want_markers) {
            active_views.toggle_active_markers_view();
        }
        return;
    }

    // Bare `o` toggles the active tab's read-only flag. Always admitted
    // by the read-only allowlist above (the locked-out user must be
    // able to unlock). Pure view-state mutation: not undoable, not dirty;
    // silently persisted on the next Ctrl+S — WHICH THE LOCKED TAB CAN NOW RUN
    // ITSELF (2026-08-07: the save authors nothing, so it is on the allowlist),
    // so a tab locked here reaches disk without an unlock and without a trip to
    // the other tab. THE TAB'S PADLOCK IS
    // THE WHOLE VISIBLE CUE since row 7 deleted the bottom strip's
    // "(read-only)" token as a restatement of it.
    if (key == GuiKeys::O && !ctrl && !shift && !alt) {
        ViewState& vs = active_view_state(app);
        vs.read_only = !vs.read_only;
        // The bottom row still repaints here: it is cheap, and the row-2/row-4
        // enabled faces below share this flag's fate frame for frame.
        viewport.invalidate_status_chain_area();
        // AND THE TOP STRIP: the read-only bit WEARS A FACE up there — the
        // icon row's read-only toggle since 2026-08-14 (the active tab's own
        // padlock slot, 2026-08-01..14, before that). The button's LAMP does
        // ride the tick comparator's `selected` bit, but its GLYPH swaps
        // closed-for-open on the same flag and no stashed bit carries that, so
        // the one damage at the one writer stays the cheaper and more honest
        // answer than a fifth stashed bit.
        // (The toolbar four's ENABLED faces — icon-row members since the
        // 2026-08-12 relayout — also move with this flag, and those
        // the comparator does catch — this damage merely arrives first.)
        viewport.invalidate_top_strip();
        return;
    }

    // BARE ESC CLEARS A RESTING REGION (architect 2026-07-30, live-test
    // refinement: "if 'esc' to clear region (but not cancel drag) cheap now? if
    // so, implement it also"). It is, and this is the whole implementation: a
    // resting scratch span is display state with no owner but the user, so
    // dropping it needs no snapshot, no membership work and no playhead move.
    // RANKED HERE, between the editors/prompts above and the render cancel below:
    // a modal surface still wins the key, and a resting region wins over the
    // render cancel because it is the more local thing on screen. With no region
    // resting the key falls straight through and cancels the render exactly as
    // before.
    // CLEAR BUT NEVER CANCEL, and that is STRUCTURAL rather than a test here: a
    // drag in flight is swallowed by the DRAG-MODAL GATE far above (which admits
    // only Ctrl+Q), so a mid-drag Esc never reaches this arm at all — the drag
    // keeps extending under the pointer and its span survives, matching the
    // no-cancel rule every pointer gesture holds. Only a span the user has
    // RELEASED can be cleared from here.
    // BARE-EXACT, like every other Escape reader (strict modifier validation).
    if (key == GuiKeys::Escape && !ctrl && !shift && !alt && app.region.active) {
        clear_region_highlight(app, viewport);
        return;
    }

    // Bare Esc cancels an in-flight render / queued batch.
    if (handle_escape_cancels(key, mods)) return;

    // THE WHOLE ESC STORY, stated here because this is where the selection/region
    // ESC LADDER used to be dispatched and the ladder is DELETED — rungs,
    // down-only doctrine and all (architect 2026-07-29). BARE ESC IS BOUND IN SIX
    // PLACES AND NOWHERE ELSE (re-derived 2026-07-31 — the drag-modal gate above
    // tests only Ctrl+Q, so Esc is UNBOUND there and falls through with every
    // other key while a gesture is in flight; it is NOT one of the six), each of
    // the six earlier in this function than this point, so reaching here means
    // the key has nothing left to do. THEY ARE LISTED IN RANK ORDER, outermost
    // modal first:
    //   (a) THE EDITOR TEXT-DRAG ESC HATCH — a bare-exact Escape ends an in-flight
    //       text-selection drag (above); a SUB-PART of the editor class below,
    //       since it can only fire while one of the five editors owns the
    //       keyboard, and the same press then falls through to that editor's own
    //       close/cancel;
    //   (b) THE EDITORS — all five, through route_modal_editor_key: Esc closes /
    //       cancels the edit (the editor blocks above, bit-for-bit unchanged);
    //       the commit-title editor (2026-08-07) joined that route and added no
    //       place of its own, which is the point of there being one route;
    //   (c) THE PROMPTS — Esc activates the rightmost response (the prompt gate at
    //       the top of this dispatch, unchanged);
    //   (c2) THE DROPDOWNS — Esc closes the open popup (the popup gate, directly
    //       under the prompt gate; architect 2026-07-31, the SIXTH binding).
    //       BOTH menus — Settings and File — are this ONE
    //       binding: they
    //       share one popup state and one gate, so the second dropdown
    //       (Navigation, 2026-08-02) and the third (File, 2026-08-13) added no
    //       seventh place and the Navigation one's deletion (2026-08-15) took
    //       none away — the count is a property of the GATE, not of the menu
    //       list. It cannot collide with (a)/(b):
    //       a popup and an editor can never be open together, by TWO mechanisms
    //       — the four dialog editors' veil swallows the press that would open
    //       a menu, and the pointer-transparent flag editor, which does not, is
    //       ENDED by the open (toggle_dropdown's open path). It ranks BELOW the
    //       prompt because Ctrl+Q from inside the popup can raise one;
    //   (d) THE REGION CLEAR — the arm just above (architect 2026-07-30);
    //   (e) THE RENDER / BATCH CANCEL — handle_escape_cancels, just above.
    // THE `h` HISTORY VIEW ADMITTED BARE ESC ON 2026-08-04 AND THE COUNT DID NOT
    // MOVE: its allowlist stopped dropping the key, which lets (d) and (e) run
    // inside the view. Those two are what the ADMISSION buys, not the only rungs
    // reachable in there (re-derived 2026-08-07): the view also opens the load
    // editor on `'` and the COMMIT-TITLE editor on Ctrl+S, so (a) and (b) run
    // in it too — and (c) with them, the checkpoint's own failure notice being a
    // prompt this view can raise — but each of those gates sits ABOVE the
    // allowlist in this dispatch, so the key never reaches the admission at all. It gained no
    // binding of its own, and Esc cannot close it: the view's toggle is
    // handle_history_mode_key's, whose whole vocabulary is enumerated at
    // history_mode_owns_key (input_key_dispatch.cpp) and carries no Esc shape in
    // any modifier combination, so no Esc reaches it.
    // What Esc still does NOT do is the old ladder: NO deselect, NO playhead land,
    // NO drop-to-span, and no collapse of anything but the span itself. A 2+
    // selection and a singleton are
    // left exactly as found. Leaving the MARKER LANE is
    // not an Esc act either: it is any DESELECTING route (Home/End, a
    // waveform click, the trim setters, an undo restore that clears — see
    // playhead_in_marker_lane). And the region clear above is now the ONE route
    // that drops a span WITHOUT moving the playhead or changing the selection —
    // the standing gap in the clear-site set (clear_region_highlight,
    // input_handler.h), closed by giving the user a key for it.
    // A bare Esc that gets past here falls to the bare-key tail, whose Escape case
    // is an explicit no-op (handle_plain_bare_keys) — the one place the key ends.
    // Modified Escape remains unbound everywhere, at every Escape reader.

    // Ctrl+Q: quit (via unsaved-work dialog when dirty).
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        prompt.request_close();
        return;
    }

    // Render-trigger chords: Ctrl+Alt+R (the single render, or the ITERATION
    // SWEEP while iteration mode is on) and Ctrl+Alt+Shift+R (the
    // miscellaneous render, a consumed no-op in iteration mode). (The
    // load-editor opener is bare `'`, handled separately below.)
    if (handle_render_dispatch_keys(key, mods)) return;

    // CTRL+H — THE HISTORY VIEW'S REVERT ACT (architect 2026-08-05): apply the
    // view's SELECTED diff flags backwards into the live store and close the
    // view. Outside the mode the chord is UNBOUND, which is what the mode test
    // says — there is no second meaning to select between, unlike Ctrl+S's.
    //
    // IT IS DISPATCHED HERE RATHER THAN CLAIMED BY handle_history_mode_key, and
    // that placement is the whole gate story: this line sits BELOW the read-only
    // gate, so a locked tab drops the chord exactly as it drops `'` — the lock
    // means hands off the piece's authored state, and this act writes markers.
    // (It parted company with the checkpoint act on 2026-08-07, when the gate
    // reclassified saving and rendering as authoring-free; that act runs from a
    // locked tab, on Ctrl+S since 2026-08-08.) The mode's allowlist above
    // admits the chord (and only while there is a subject to revert), which is
    // what lets it reach here at all; the act's own body owns everything past
    // that.
    if (app.history_mode.active && ctrl && !shift && !alt && key == GuiKeys::H) {
        run_history_revert();
        return;
    }

    // Space is the sole playback toggle, and it is modifier-strict — every
    // modified Space is unbound (is_play_pause_key owns that test). Return /
    // keypad Enter are NOT playback keys; they open the flag editor, handled
    // just below.
    if (is_play_pause_key(key, mods)) {
        // Target-view playback gating: refuse Space-to-play while a
        // target render is in flight (current is stale by
        // definition). Space-to-stop is still honored — if playback
        // happened to be running before an edit, the trigger() helper
        // already froze it, so playback.is_playing() is false in
        // practice. The empty-target-buffer case (no successful target
        // render yet in this session) is also refused so the
        // user can't play stale source-domain samples through a
        // target-view binding. Source view falls through unchanged.
        // Both refusals write NOTHING, so a press inside the sub-tick window
        // between a natural end and the tick that deactivates the scanner
        // leaves that scanner exactly as it found it — the tick's end-of-audio
        // branch has no is_updating gate and deactivates it on its own.
        if (app.active_audio_view == 'T' &&
            !playback.is_playing()) {
            if (target_render.is_updating()) return;
            if (app.target_buffer_frames <= 0) return;
        }
        // LEAD-IN AUDITION, START EDGE ONLY (architect 2026-07-28): when the
        // phase-reset lead-in overlay has a SUBJECT, Space launches the scanner
        // kN/2 output samples AHEAD of the resting playhead — the full-scale
        // point of a reset dropped at the playhead — without moving the cursor,
        // so the stop merely deactivates the scanner and the cursor it never
        // touched is exactly where it was. A non-destructive audition of the
        // OLA/Hann synthesis lead-in.
        // THE PREDICATE IS THE SELECTION-STATE ONE (Selection::phase_overlay_
        // subject), never GuiPaintHandler::phase_reset_overlay_band: the band
        // layers geometry gates (area size, samples-per-pixel, sub-pixel forward
        // width, offscreen refusal) on top, so keying Space on it would let a
        // scroll or a zoom silently change what Space does.
        // The kN/2 is EXACT output-sample arithmetic. The painted band is a ±1px
        // approximation for jitter reasons and must never enter this number.
        const int64_t launch_offset =
            (!playback.is_playing() &&
             selection.phase_overlay_subject().has_value()) ? kN / 2 : 0;
        // SPACE ALWAYS PLAYS FROM THE PLAYHEAD (architect 2026-07-30, Q2: "drop
        // the left edge launch - play issues from playhead OR scrub - user can
        // click scrub region to preview"). The region arm that stood here — a
        // left-bound launch through scrub_launch_at whenever a span rested — is
        // DELETED with the SPAN FORM: the region is trim scratch, not a launch
        // point, and the SCRUB is the gesture for previewing it — the waveform
        // lower half's plain click, its one entry (the act runs at the
        // motionless release since 2026-08-13): click inside
        // the span and it auditions from there, the span resting untouched.
        // Space now touches no region at all, in either
        // direction: it neither reads one nor clears one.
        playback_lifecycle.toggle_playback(launch_offset);
        return;
    }

    // Bare Return / KpEnter opens the flag editor on the focused marker — the
    // click-to-edit replacement (Enter is already the editor's commit key, so
    // the open/commit round-trip is symmetric). While any editor is open the
    // editor blocks above consume Enter first (commit), so this is reached only
    // with no editor active. Repair the focus first, then: a focused warp
    // marker (last_selected_marker >= 0) in W view opens its canonical-line
    // editor with the seeded content fully selected (open-selected, like every
    // open route — the first keystroke replaces it). P view (phase resets have
    // no per-flag editor) and no focused marker are no-ops.
    // Read-only already dropped Return at the allowlist gate above (the editor
    // is an authoring surface — the old click-to-edit refused read-only too).
    // Modifier-strict: only the plain, unmodified press binds.
    if ((key == GuiKeys::Return || key == GuiKeys::KpEnter) &&
        !ctrl && !shift && !alt) {
        selection.repair_last_selected();
        // The flag editor is a warp authoring surface (label/ref/tempo/iter),
        // so it opens only in warp's home view: off home
        // (active_column_authoring_allowed false) refuses silently, exactly
        // like the P-view refusal already encoded in the condition.
        if (app.last_selected_marker >= 0 && app.active_markers_view != 'P' &&
            active_column_authoring_allowed(app)) {
            flag_editor.enter_top_flag_edit(app.last_selected_marker);
        }
        return;
    }

    // Bare 0 goes to FULL ZOOM OUT, and runs the `c` command once it is already
    // there (run_overview_command — architect 2026-08-05, no longer a toggle;
    // the second arm was a bare center for one day). The trim-bar double-click
    // deliberately
    // DIVERGES from this (run_span_framing_command — it zooms to the region
    // / trim / whole-song span); C remains the DIRECT working-zoom-and-center
    // gesture — `0` reaches it only from full out, and by calling it — while
    // the Tab family changes no zoom at all (2026-08-05), so `0` is
    // the one command that reaches the whole song. DIGITS 1, 2
    // and 3 are the ABSOLUTE VIEW SELECTORS since 2026-08-01 (their block is up
    // beside bare `t`, the axis handler they compose); 4..9 are unbound.
    if (!ctrl && !alt && !shift && key == GuiKeys::Digit0) {
        run_overview_command();
        return;
    }

    // Ctrl+Z undo / Ctrl+Shift+Z redo — the WHOLE family, SHIFT the one
    // meaningful bit (which is why this arm is one of strict modifier
    // validation's deliberately-untightened families, conventions.md). Placed
    // before the GuiKeys::S save handling so modifier dispatch reads
    // left-to-right in the source. Both are silent no-ops when their respective
    // stack is empty. ALT IS UNBOUND HERE and stays that way: the target
    // compositor (labwc) grabs Ctrl+Alt+Z / Ctrl+Alt+Shift+Z, so the GUI never
    // receives them — the alt pair that briefly meant stay-put undo/redo was
    // rolled back for that collision (selection-model.md), and an alt-carrying
    // shape is again a plain no-op under strict modifier validation.
    if (ctrl && !alt && key == GuiKeys::Z) {
        if (shift) undo.do_redo();
        else       undo.do_undo();
        return;
    }

    // P / I / M letter keys (phase-reset clipboard, view toggle, iteration,
    // bpm mode).
    if (handle_mode_keys(key, mods)) return;

    // The platform boundary case-folds letters and delivers the
    // unshifted GuiKey, so a Shift+letter press arrives as the lowercase
    // GuiKeys::* with mods.shift set — disambiguate via the `shift` bool.
    if (key == GuiKeys::S) {
        // bare `s` is the ONE drop in both columns, and it is the AUGMENTED one
        // (architect 2026-07-28). AUGMENTED is the one word for it across the
        // tree — the four other sites are the empty-lane double-click's
        // declaration (input_handler.h), its EmptyLane candidate row
        // (app_state.h), and its press-side and create-side bodies
        // (input_pointer.cpp) — and it describes
        // the PAYLOAD — the dropped marker carries the previous marker's
        // effective tempo instead of a plain 1.00. It no longer describes a
        // GESTURE: the plain-versus-augmented modifier convention died with
        // Alt+S the same day, leaving bare `s` and the bare empty-lane
        // double-click as the only drops there are.
        // In W view it drops an owner carrying the
        // immediate-prior marker's effective tempo: the AUGMENTED drop splits a
        // section WITHOUT changing the map — render-neutral, which is why it
        // beat the plain 1.00 drop, whose mid-warp landing audibly changes the
        // section it falls in. Neutrality is the RULE, not a guarantee: two
        // states break it, and both STAND (architect 2026-07-28):
        //   - onto an OCCUPIED frame: find_immediate_prior looks strictly BEFORE
        //     the drop frame while drop_marker deliberately allows landing on an
        //     occupied one, so dropping where an enabled owner already sits
        //     builds a 2+-survivor exact-frame group, which the resolver
        //     replaces with one synthetic 1.00 owner — that section's authored
        //     tempo is gone.
        //     It stands because it is LOUD: the affected markers turn
        //     normalization-red at once, the standing display cue for a state
        //     the resolver rewrites, so nothing is silently lost. drop_marker
        //     already rules exact-frame degeneracy the render boundary's to
        //     collapse, not the drop's.
        //   - inside a LABEL DEFINITION: only a preceding label REF is
        //     special-cased, so a preceding label_def takes the ordinary numeric
        //     arm. Both halves keep their local tempo, but a definition's SPAN
        //     IS ITS MEANING — the parser caches its target duration from where
        //     the section ends (warp_frame_map_build.cpp) — so the split reprices
        //     every reference to that label. A property of definitions, not a
        //     defect.
        // In P view it drops the lead-in reset: a reset placed N/2 before the
        // playhead so its OLA/Hann lead-in reaches full scale AT the playhead
        // (the perceived transient), which composes with Space's lead-in
        // audition — drop then Space cancels the two N/2 offsets and auditions
        // from exactly where the cursor was.
        // Ctrl+S saves; every other modifier combination on `s` is unbound and a
        // consumed no-op here.
        //
        // THE HISTORY VIEW SELECTS THE OTHER COMMAND (architect 2026-08-08, the
        // iteration bit's own bit-selects-the-command precedent applied to this
        // chord): while the view stands Ctrl+S is SAVE AND COMMIT — the act that
        // runs this very save first and then publishes the checkpoint — so the
        // fork is here, inside the one route, and the Save button reaches it by
        // synthesizing this chord like every other redesigned button. The act's
        // own preconditions are already spent above: the view's allowlist admits
        // this chord only with a non-empty head delta and no checkpoint in
        // flight, which is the same one decision that greys the button. (It rode
        // Ctrl+Alt+R from 2026-08-04 to 2026-08-08; the act is save-first by
        // definition, so it belongs on the save chord and the Render hijack is
        // gone whole.) Inside the commit-title editor Ctrl+S is the PLAIN save
        // again, through the five-editor modal contract, which sits above this
        // arm and never reaches it.
        if (ctrl && !shift && !alt) {
            if (app.history_mode.active) {
                open_history_commit_editor();
                return;
            }
            save_ops.save();
            return;
        }
        // Both drops are home-view authoring, so off home refuses silently
        // (consumed no-op). The lead-in arm needs no separate target-view test:
        // P's home IS target, so this one gate already carries it.
        if (!active_column_authoring_allowed(app)) return;
        if (!ctrl && !shift && !alt) {
            if (app.active_markers_view == 'P')
                phase_resets.drop_phase_reset_lead_in_at_playhead();
            else
                warpops.drop_copy_previous_at_playhead();
        }
        return;
    }
    // Ctrl+N: toggle pass (inherit) status on the focused warp marker,
    // symmetric with Ctrl+D below — pass, like disabled, is a status
    // toggled on an existing marker, never dropped directly. No
    // phase-reset equivalent (a reset has no tempo source to inherit
    // from), so this no-ops in P view. Plain `n` and Shift+N are unbound.
    if (key == GuiKeys::N && ctrl && !alt && !shift) {
        if (app.active_markers_view == 'P') return;
        // Warp authoring (not a ruled target-view exception): source home only.
        if (!active_column_authoring_allowed(app)) return;
        warpops.toggle_inherits();
        return;
    }
    // Ctrl+D: toggle disabled (warp + phase reset). Plain `d` and Shift+D are unbound.
    if (key == GuiKeys::D && ctrl && !alt && !shift) {
        // Status toggle authors the active column's store: home view only
        // (the predicate maps W->source, P->target). Off home is a consumed
        // no-op.
        if (!active_column_authoring_allowed(app)) return;
        if (app.active_markers_view == 'P') phase_resets.toggle_phase_reset_disabled();
        else                        warpops.toggle_disabled();
        return;
    }
    if (key == GuiKeys::Delete && !ctrl && !alt && !shift) {
        // Delete acts on the active marker store. No read-only check here:
        // Delete drops at the read-only gate above, which is the keyboard
        // path's single guard, and no pointer path reaches the delete routines.
        // Trim is not part of the selection system, so Delete never acts on a
        // bound (Shift+X is trim's clear; bare x is set-only).
        // Deletion authors the active column's store: home view only (the
        // predicate maps W->source, P->target). Off home is a consumed no-op.
        if (!active_column_authoring_allowed(app)) return;
        if (app.active_markers_view == 'P') {
            phase_resets.delete_selected_phase_reset();
            return;
        }
        warpops.delete_selected_marker();
        return;
    }

    // Tab family: Ctrl+Tab / Ctrl+Shift+Tab switch tabs; Tab / Shift+Tab /
    // IsoLeftTab cycle marker focus (always recentering the viewport on the
    // focused marker).
    if (handle_tab_switch_keys(key, mods)) return;

    // Tempo nudge, bare Up / Down (architect 2026-07-28). No view or selection
    // guard here — adjust_tempo_cents returns at once unless the warp view is
    // active with a non-empty selection and a valid focus, so the vertical
    // arrows are an inert (still consumed) no-op everywhere else, phase-reset
    // view included. `=` / `-` are the zoom keys (see below). Modified Up / Down
    // are unbound. Read-only tabs refuse upstream: the allowlist does not admit
    // the vertical arrows in any form.
    if (!alt && !shift && !ctrl && key == GuiKeys::Up) {
        warpops.adjust_tempo_cents(+1, mods.synthesized_repeat); return;
    }
    if (!alt && !shift && !ctrl && key == GuiKeys::Down) {
        warpops.adjust_tempo_cents(-1, mods.synthesized_repeat); return;
    }
    // The two zoom-step commands. CTRL+WHEEL DISPATCHES THESE SAME BODIES
    // (handle_wheel, via Viewport::zoom_steps — the coalesced form, one whole
    // level per completed detent; architect 2026-08-12), so the key and the
    // wheel chord cannot drift apart in feel.
    if (key == GuiKeys::Equal && !shift && !ctrl && !alt) {
        viewport.zoom_in(); return;
    }
    if (key == GuiKeys::Minus && !shift && !ctrl && !alt) {
        viewport.zoom_out(); return;
    }

    // x SETS the trim; Shift+X MAXIMIZES it to the full window (architect
    // 2026-07-25 splitting the old x-branch, re-posed 2026-07-30 under
    // always-set). Bare x is set-only: a live region trims to it (overwriting
    // the resting window; the span is then CONSUMED and the selection cleared —
    // architect 2026-07-30 / 2026-07-29; a DEGENERATE inverse-mapped span refuses
    // instead of writing a pair the crossed-commit reset would throw away), and
    // and with NO region it is a consumed nothing). Shift+X writes [0, total-1]
    // (handle_trim_shift_x). CTRL+SHIFT+X SHOWS THE REGION since 2026-08-16
    // (handle_show_region): make sure a region exists — seeding one at the trim
    // window if none stands — and bring it into view. It CLEARS nothing and is
    // NOT a toggle. It is a GENUINELY NEW BINDING and not a widening of either
    // arm above — the strict-modifier rule made that combination a no-op
    // everywhere — and it exists because bare `x` had carried the seed for one
    // day and one key must not mean two unrelated things (show and commit).
    // The playhead is an OUTPUT of the two trim writes, never an input:
    // every trim WRITE parks it at the new trim start (architect 2026-08-05 —
    // the rule and its membership at the head of input_trim.cpp), and a refused
    // press moves nothing. Trim's pointer routes
    // are the PLAIN trim-bar press (single via an endcap hit, pair via a bridge
    // press strictly between the two bound columns); trim is outside the
    // selection system, so there is no Delete arm. Plain Ctrl+x is cut
    // (text_editor.cpp) and stays unbound here.
    if (!ctrl && !shift && !alt && key == GuiKeys::X) {
        handle_trim_x();
        return;
    }
    if (!ctrl && shift && !alt && key == GuiKeys::X) {
        handle_trim_shift_x();
        return;
    }
    if (ctrl && shift && !alt && key == GuiKeys::X) {
        handle_show_region();
        return;
    }

    // Bare `;` opens the settings prompt as a modal on the bottom row.
    // Keyboard-only (no click analogue). The keydown island (on_key) routes
    // subsequent keystrokes into the active-editor block at the top of this
    // dispatch; opening here just primes the State.
    // The settings editor is a modal DIALOG surface, so its open takes the
    // shared modal stop (stop_playback_for_modal_open — the decision table and
    // the flag editor's exemption live at its declaration). THE STOP IS THE
    // OPENER'S SINCE 2026-08-07, not this site's: the editor gained a read-only
    // refusal that day (a locked tab authors no engine settings), so the stop
    // moved inside GuiSettingsEditor::open past that gate, the open_load_editor
    // precedent. This key never meets that refusal anyway — `;` is off the
    // read-only allowlist and drops far above — but the two openers share one
    // owner rather than one of them keeping a private copy.
    if (key == GuiKeys::Semicolon && !shift && !ctrl && !alt) {
        settings_editor.open();
        return;
    }

    // Bare `'` opens the load prompt as a modal on the bottom row: load a chosen
    // render in place as the new authoring baseline by NAME — or, while the `h`
    // history mode stands, a COMMIT by its SHA, the editor's other subject
    // (open_load_editor's own branch; the mode admits this one key through
    // history_mode_key_blocked above). TWO PRODUCERS, ONE ROUTE: this key and
    // the icon row's load button, which synthesizes exactly this bare chord
    // through the redesign chord table, so both subjects reach both producers
    // and no second opener exists. A modal DIALOG
    // surface. open_load_editor owns the no-source / renders-side guards AND
    // the playback stop: playback halts only when the modal actually opens, so a
    // refused open leaves a listening session undisturbed (once open, Space is
    // inside the modal blocked set, so playback cannot restart until the editor
    // closes).
    if (key == GuiKeys::Apostrophe && !shift && !ctrl && !alt) {
        open_load_editor();
        return;
    }

    // BARE Left / Right, MARKER-LANE half (architect 2026-07-28). The horizontal
    // arrows step one painted column; the selection
    // decides which LANE that step happens in, and playhead_in_marker_lane is that
    // decision. With a selection the arrows move the FOCUSED MARKER and the
    // always-visible cursor RIDES ALONG — the two routes below both re-land the
    // playhead on their committed focus (finish_position_nudge), so the marker
    // and the cursor move together, visibly. (The lane model used to be argued
    // from a suppression — no cursor painted, the focused flag's ink triangle
    // standing in for it — and that argument retired 2026-07-30 with the
    // suppression; the behaviour is unchanged.) HORIZONTAL MOVEMENT IS A FOCUS ACT
    // (architect 2026-07-29): a 2+ selection COLLAPSES to its focus in the position
    // nudges' shared prologue and the focus alone steps — groups are never moved
    // (the doctrine at the head of position_nudge.h). With no selection the
    // playhead is in
    // the waveform lane and this branch does not match: the key falls through
    // to the bare-key tail, which steps the cursor alone. The lane is left by any
    // DESELECTING route (the lane model at playhead_in_marker_lane; Esc is NOT
    // one — it clears a resting span and nothing else, touching no selection),
    // and there is no fallback, so an
    // off-home marker-lane press is a consumed no-op, never a
    // waveform-lane step. (The AUDITION SCRUB is a different gesture entirely — the waveform
    // lower-half one-shot press — and no arrow key reaches it.)
    // ROUTE BEFORE THE STOP: this branch must decide the route ahead of the
    // waveform-lane body's stop / selection-clear / region-clear, because the two
    // lanes carry DIFFERENT playback regimes — the position nudges stop in
    // position_nudge_prologue even when they later refuse, while the
    // W+target refusal below stops nothing at all (a refused press leaves a
    // listening session running). Merging the regimes would give a refused press a
    // stop it does not have.
    // Trim is not part of the selection system, so the nudge never acts on a
    // bound (trim's own gestures live on the trim bar — the endcap and bridge
    // drags; roster at route_trim_bar_press's header, input_trim.cpp).
    // ROUTING — TWO ROUTES AND ONE REFUSAL (architect 2026-07-29, down from three
    // routes): each column's POSITION nudge runs in its HOME view only — warp in
    // source, phase reset in target (the home-view binding) — and every other
    // combination is a CONSUMED REFUSAL. W+TARGET is now one of those refusals: it
    // used to dispatch the TEMPO-IMAGE STEP there (the tempo drag's keyboard twin,
    // stepping the focused marker's IMAGE through a predecessor tempo solve), and
    // that whole family is DELETED — the tempo surface is the bare UP/DOWN cent
    // step alone, which is unaffected in W+target and is where tempo authoring
    // lives (the delete list is at the head of marker_drag.h). No fallback to the
    // waveform-lane step, by the standing rule that a refused marker-lane press is
    // a consumed no-op. Read-only
    // tabs refuse both routes upstream: the allowlist admits the bare
    // horizontal arrows only when playhead_in_marker_lane is false, so a locked
    // tab with a selection drops them at the gate.
    if (!alt && !shift && !ctrl &&
        (key == GuiKeys::Left || key == GuiKeys::Right) &&
        playhead_in_marker_lane()) {
        const int direction = (key == GuiKeys::Left) ? -1 : +1;
        // Both routes take the key event's platform repeat bit: it is what makes a
        // HELD arrow one undo entry (Undo::coalesce_gesture).
        const bool rpt = mods.synthesized_repeat;
        if (app.active_markers_view == 'P') {
            if (app.active_audio_view != 'T') return;   // phase home = target
            phase_resets.nudge_selected_phase_resets(direction, rpt);
        } else if (app.active_audio_view == 'T') {
            return;   // W+target: no position authoring, no tempo-image step
        } else {
            warpops.nudge_selected_markers(direction, rpt);  // warp home: position
        }
        return;
    }

    // PageUp / PageDown: step the viewport back / forward by exactly the
    // Alt-wheel step (samples_visible / 10). PageUp goes back, PageDown
    // forward. Pure active-display navigation, so the read-only allowlist
    // admits it.
    if (!ctrl && !alt && !shift &&
        (key == GuiKeys::PageDown || key == GuiKeys::PageUp)) {
        const int64_t step = std::max<int64_t>(
            1, samples_visible(app, audio) / kViewportLeadDivisor);
        viewport.scroll_viewport(key == GuiKeys::PageUp ? -step : +step);
        return;
    }

    // Bare-key dispatch. Every modifier-gated handler above this point
    // returns on match, so by the time we reach here, any modifier being
    // held means the chord had no binding and should be a silent no-op
    // — never fall through into a bare binding (e.g. Ctrl+Shift+Alt+F
    // must not toggle follow via GuiKeys::F).
    if (!ctrl && !shift && !alt) {
        handle_plain_bare_keys(key);
    }
}

void GuiInputHandler::cycle_marker_focus(bool forward) {
    if (forward) selection.select_next_marker();
    else         selection.select_prev_marker();

    // The select above establishes the focused marker; the shared jump tail
    // moves the playhead onto it and recenters AT THE CURRENT ZOOM. Byte-
    // identical to the `c` gesture's marker jump — the zoom is what separates
    // the two commands: `c` snaps to the working level, a Tab walk keeps
    // whatever level the user is reading at.
    // A CYCLE STEP THAT LANDS NOTHING CHANGES NOTHING: with no marker to focus
    // the jump returns false having touched neither playhead nor viewport — a
    // Tab in an empty collection stays the consumed nothing it has always been.
    //
    // NO ZOOM ON TAB (architect 2026-08-05, reverting his own same-day ruling
    // that had every step set kWorkingZoomLevel here): the walk is navigation
    // and must not re-frame the view under the user, so the whole family — the
    // three bare chords and the Ctrl+Shift+Tab lockstep march, which calls this
    // once per tab — lands and recentres at the level it was pressed at. `c` and
    // `0`'s second arm are untouched and remain the routes to the working zoom.
    jump_playhead_to_focused_marker();
}

void clear_region_highlight(AppState& app, Viewport& viewport) {
    // The clear+damage shape the existing region-clear sites use (the navigation
    // jumps, end_region_drag_min_size_check): reset to a blank RegionState and
    // damage
    // the waveform area once, under which the recolored ground repaints away.
    // Guarded so a call on
    // the common no-region path costs nothing.
    if (!app.region.active) return;
    app.region = RegionState{};
    viewport.invalidate_waveform_area();
}

bool GuiInputHandler::jump_playhead_to_focused_marker() {
    // The walk is markers-only (trim is not a cycle stop). The playhead lands on
    // the focused marker unconditionally, and the viewport always recenters on
    // it (below) — follow mode does not gate the cycle.
    // FOCUS RESOLUTION, kept for the `false` RETURN ALONE: a missing or
    // out-of-range focus aborts the WHOLE jump, stop included, and the land
    // owner's silent no-op cannot express that, so the two refusals stay spelled
    // here. The FRAME is not resolved here at all — that is the owner's.
    {
        const int idx = app.last_selected_marker;
        if (idx < 0) return false;
        if (idx >= active_marker_count(app)) return false;
    }

    playback_lifecycle.stop_playback_if_playing();

    // THE LAND GOES THROUGH ITS ONE OWNER (2026-07-30): the two-step placement
    // basis, the direct cursor write with NO viewport move, and the damage that
    // follows it are land_playhead_on_marker's (input_pointer.cpp, where the
    // marker-lane-owns-the-playhead rule and the caller inventory live). This
    // site hand-copied that recipe; now it calls it. NOT move_playhead_to, which
    // would scroll the viewport a second time before the centering below.
    // The owner OWNS the damage: full waveform area + the clock cell on a land that
    // MOVES, and an early return on a land onto the sample the playhead already
    // holds — nothing moved there, so nothing needs erasing. What stays HERE is
    // exactly what the owner does not provide: the stop above, the region clear,
    // and the recenter below.
    land_playhead_on_marker(app, audio, viewport, app.last_selected_marker);

    // Navigation jump: dissolve a resting region highlight — its span is stale
    // now the playhead has left it. Covers the whole Tab family and `c` through
    // this one shared tail.
    clear_region_highlight(app, viewport);

    // Center the viewport on the focused marker at the current zoom. THE ZOOM
    // IS THE CALLER'S, and the two callers answer differently: `c` snaps to the
    // working zoom right after this returns, the Tab family sets nothing at all
    // (architect 2026-08-05, "no zoom on Tab") — so this tail frames the stop at
    // whatever level it was called at, and only `c`'s apply_zoom_change
    // re-centers after it. This recenter is unconditional: follow
    // mode does not gate the cycle (architect 2026-07-19, reversing the
    // earlier follow-only rule). center_viewport_on_playhead is the SOLE
    // viewport write in this path: it reads the cursor we just set and
    // scrolls once to center it, emitting one coherent set of waveform +
    // top-strip damage against the final viewport.
    // center_viewport_on_playhead routes through kick_waveform_sync (whose
    // installed callback IS force_synchronous_waveform_rebuild) inside its
    // own moved guard, so a recenter that scrolls already gets its one
    // synchronous rebuild here — no second call in this function's tail. The
    // unmoved path (EOF-clamped no-op) needs none.
    viewport.center_viewport_on_playhead();
    return true;
}

void GuiInputHandler::run_center_command() {
    // THE BARE `c` COMMAND, WHOLE — the working zoom centered on the playhead,
    // with a focused stop re-landed under it first — and THE ONE PLACE THE MODE
    // FORK LIVES. THREE CALLERS: the live `c` key arm (handle_plain_bare_keys),
    // the history mode's own `c` arm (handle_history_mode_key, which must claim
    // the key to keep it off the mode's allowlist) and, since 2026-08-05,
    // run_overview_command's SECOND ARM — `0` pressed with the zoom already at
    // full out runs exactly what `c` runs, so `0` twice is overview then working
    // zoom on the focus.
    //
    // THE FORK IS HERE RATHER THAN AT THE CALLERS because that makes it ONE
    // decision for all three: the two key arms are each reachable in one mode
    // only (the mode claims `c` above the live dispatch, so the live arm below
    // never runs in the mode), and `0` is reachable in both — putting the
    // question at `0` alone would leave the mode's own `c` answering it a second
    // time in another spelling. One owner, one answer.
    if (app.history_mode.active) {
        // THE MODE'S RE-EXPRESSION: the live recipe read against the mode's own
        // data. With a focus standing the playhead re-lands on that diff flag
        // first (idempotent when it is already there, which after a click or a
        // Tab step it is). The live arm's repair_last_selected /
        // jump_playhead_to_focused_marker pair is deliberately NOT run: it walks
        // the live stores, which the lane is not showing. The region clear is
        // unconditional and up front, exactly as the live arm does it — the
        // no-focus path never reaches a land's own tail.
        clear_region_highlight(app, viewport);
        const int focus = app.history_mode.focus;
        if (focus >= 0 &&
            focus < static_cast<int>(app.history_mode.flags.size())) {
            // The live arm's stop lives inside its jump, so it stops only when
            // something is focused; this keeps that shape. NO REACHABLE
            // PRODUCER (recorded 2026-08-06): the entry owner stops any session
            // running before `h` and nothing in the view can start one, so this
            // is a formality kept for the regime's shape — the same note the
            // mode's Tab cycle, its Home/End and the revert act carry at their
            // own stops (input_key_dispatch.cpp).
            playback_lifecycle.stop_playback_if_playing();
            land_playhead_on_source_frame(
                app, audio, viewport,
                app.history_mode.flags[
                    static_cast<std::size_t>(focus)].time_frame);
        }
        viewport.apply_zoom_change(kWorkingZoomLevel);
        viewport.center_viewport_on_playhead();
        return;
    }

    // THE LIVE RECIPE. Jump to the working zoom (kWorkingZoomLevel, the ideal
    // warp-authoring zoom). When a marker is focused, first jump the playhead
    // exactly onto it — the same jump the Tab family runs, after the same
    // last-selected repair — then set the working zoom and center on it; with no
    // focused marker, keep the plain working-zoom-and-center-on-playhead
    // behavior.
    // Clear the region here, unconditionally and up front: the no-focus arm
    // never reaches jump_playhead_to_focused_marker's clear tail (that function
    // early-returns with nothing focused), and a region drag clears the marker
    // selection, so region-drag-then-`c` is exactly the no-focus path. HELP lists
    // `c` in the clear set unconditionally. The focused arm then double-clears
    // via the jump tail — a no-op, since the helper's !active guard returns
    // immediately on the already-cleared region.
    // A GROUP CARRIES (architect 2026-07-30, with the SPAN FORM retired): the
    // collapse-to-focus that stood here is deleted — it existed only to keep a
    // group from resting SPANLESS, a state that no longer exists now the region
    // is trim scratch rather than a group's playhead form. The jump below is a
    // jump TO THE FOCUS and accepts a group's focus as-is; the always-visible
    // cursor lands there, the other members keeping their brightened flags.
    clear_region_highlight(app, viewport);
    selection.repair_last_selected();
    jump_playhead_to_focused_marker();
    viewport.apply_zoom_change(kWorkingZoomLevel);
    viewport.center_viewport_on_playhead();
}

void GuiInputHandler::run_overview_command() {
    // The bare `0` key: FULL ZOOM OUT FIRST, THE `c` COMMAND WHEN ALREADY THERE
    // (architect 2026-08-05, REPLACING the working-zoom toggle this used to be —
    // `0` no longer returns anywhere, so the old two-way shape and its name are
    // gone; there was never a stashed return level to delete, the toggle's other
    // end having been the fixed kWorkingZoomLevel). Below the per-file effective
    // ceiling → jump to it, the whole song in the window, playhead untouched.
    // ALREADY at it → RUN THE `c` COMMAND (architect 2026-08-05, superseding the
    // bare center that arm shipped with the same day): so `0` pressed twice is
    // overview, then the working zoom on the focus — the round trip restored,
    // with `c`'s own focus semantics rather than a second, weaker centering. The
    // fork between the live `c` and the history mode's own lives inside
    // run_center_command, its one owner.
    // The trim-bar DOUBLE-CLICK still DIVERGES from this (see
    // run_span_framing_command): it zooms to the region / trim / whole-song
    // SPAN, where this one command only ever reaches the whole song.
    //
    // THE FIRST ARM IS A PURE VIEWPORT MOVE (architect 2026-07-30, reversing the
    // 2026-07-29 clear+collapse as OVERSCOPED): it touches neither the selection
    // nor the region nor the playhead — only the zoom level and, through
    // apply_zoom_change, the viewport start. A selection span's endpoints are
    // ACTIVE-DOMAIN frames and a zoom changes no domain, so a group and its extent
    // survive the overview exactly as they survive `=` / `-` and the wheel.
    // The family is the group-verb doctrine (position_nudge.h): `0` sits with the
    // zoom framing on the span-READ side, not with the collapse+land verbs. It does
    // not stop a live audition either — the pure-viewport-move class of the keyboard
    // stop rule (stop_playback_if_playing's declaration, playback_lifecycle.h).
    // THE REGION CLEAR DIED WITH THE COLLAPSE, NOT SEPARATELY — do not reintroduce
    // it on this arm: a clear that left a 2+ selection standing would rest it
    // SPANLESS, the hybrid third form the architect rejected (that state draws no
    // playhead cue at all), so the two are one decision.
    // THE SECOND ARM IS `c`, SO IT CARRIES `c`'s REGIME, not this one's: the
    // region clear, the focus repair, the land onto the focused marker and the
    // stop that rides inside that land are all the center command's, stated at
    // its owner. Nothing about them is decided here — this arm only chooses
    // between the two commands.
    // Both arms read the RESTING cursor (apply_zoom_change and
    // center_viewport_on_playhead both take the scanner while playing and the
    // cursor otherwise). With a selection the cursor already rests on the focus —
    // every focus-changing route lands it.
    //
    // `>=` rather than `==`, matching Viewport::zoom_out's own ceiling test: the
    // clamp chokepoint keeps the live level at or under the ceiling, and a level
    // resting exactly on it is what "already full out" means either way.
    const double full_out = effective_max_zoom_level(
        waveform_area(app).w, live_total_frames(app, audio),
        audio.sample_rate());
    if (app.zoom_level >= full_out) {
        run_center_command();
    } else {
        viewport.apply_zoom_change(full_out);
    }
}

void frame_span_into_view(AppState& app, const GuiAudio& audio,
                          Viewport& viewport, int64_t lo, int64_t hi,
                          bool margin) {
    // The shared span framer (declared in input_handler.h). Computes the margined
    // fit level exactly as the double-click always has, then ALWAYS derives the
    // start by CENTERING the margined span in the window through the UNROUNDED
    // visible width (spp_t * W) — grid quantization is owned downstream by
    // clamp_viewport_start, so NO painter-quantized pre-rounding belongs here (see
    // the centering block below). A span too small for kMinZoom to fill (the
    // floor-saturated case) rests centered instead of left-aligned, and the
    // unclamped case degenerates to the span's left edge (unrounded spp_t * W ==
    // the margined span by the fit-level solve). Ends at apply_zoom_to_start.
    if (audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const int     W    = area.w;
    const int     sr   = audio.sample_rate();
    if (W <= 0 || sr <= 0) return;
    const int64_t total = live_total_frames(app, audio);
    if (total <= 0) return;

    if (hi < lo) std::swap(lo, hi);   // defensive; monotone callers keep order
    double flo = static_cast<double>(lo);
    double fhi = static_cast<double>(hi);
    if (margin) {
        const double m = 0.025 * static_cast<double>(hi - lo);
        flo -= m;
        fhi += m;
    }

    // Fit level: effective_max_zoom_level's formula with the span in place of
    // total, clamped into [kMinZoom, per-file effective ceiling]. A zoom-OUT
    // ceiling and a zoom-IN floor, so framing a tiny span may go deep (down to
    // kMinZoom) while a span wider than the song saturates at whole-song-visible.
    double span = fhi - flo;
    if (span < 1.0) span = 1.0;  // guard log2 of <= 0 (degenerate lo == hi)
    const double raw_level = 1.0 + std::log2(
        span * 1000.0 /
        (0.625 * static_cast<double>(sr) * static_cast<double>(W)));
    const double ceiling = effective_max_zoom_level(W, total, sr);
    const double target_level = std::clamp(raw_level, kMinZoom, ceiling);

    // CENTER: place the margined span's midpoint at the window center, using the
    // UNROUNDED visible width (spp_t * W) — grid quantization is owned downstream
    // by clamp_viewport_start (the chokepoint), so no painter-quantized
    // pre-rounding belongs here. Only the FINAL start is nearbyint'd. This keeps
    // the promised single behavior change (the floor-saturated centering): in the
    // ordinary UNCLAMPED fit spp_t * W equals the margined span in exact reals, so
    // mid - span/2 == flo and the start degenerates to nearbyint(flo) identically
    // (pre-rounding the width could shift it a frame, which clamp_viewport_start
    // then amplifies to a whole grid step). The residual is ULP-level exp2/log2
    // round-trip noise, material only within an ULP of a .5 rounding boundary. The
    // ceiling-saturated whole-song case (mid = total/2, spp_t * W ~= total ->
    // start ~= 0) is then wall-clamped to 0 by clamp_viewport_start as before.
    const double mid       = 0.5 * (flo + fhi);
    const double visible_t = samples_per_pixel_at(target_level, sr) *
                             static_cast<double>(W);
    const int64_t target_start =
        static_cast<int64_t>(std::nearbyint(mid - visible_t / 2.0));

    // Set level + start through the two clamp chokepoints and repaint like the
    // other zoom commands; the idempotent current-vs-target no-op lives there.
    // NOT apply_zoom_change (which would recenter on the playhead).
    viewport.apply_zoom_to_start(target_level, target_start);
}

// PREFER A SCROLL, ZOOM ONLY WHEN THE SPAN CANNOT FIT (architect 2026-07-25
// post-labwc, decided on PAINTED COLUMNS). This body was the GROUP undo/redo
// restore's inline tail from that day until 2026-08-16, when the
// SHOW-REGION button asked for the identical behaviour in the architect's
// own words — "like undo in terms of zoom/viewport: if the region can fit at
// current zoom and is not fully in view, it is brought into view just like undo
// marker group, without affecting zoom; if it cannot fit, zoom is made to fit"
// — and it was HOISTED rather than described twice. The restore keeps its own
// class plus a pointer here; this is the one authoritative statement.
//
// THE FIT CONTRACT IS PAINTED COLUMNS, not a sample span — an endpoint paints
// at its OWN column and the painter does NOT edge-clamp it, so the capacity is
// the pixel range [0, W), NOT q*W samples (which overcounts by up to a column)
// and NOT the grid-snapped start (clamp_viewport_start moves it ~half a pixel).
// Both tests decide on the endpoints' columns under the painter's own basis
// (painter_samples_per_pixel + the shared displayed_column_at rounding — the
// endpoints already live in the active display domain, so no warp map is
// walked). THREE ARMS:
//   - fully visible (both endpoint columns in [0, W) under the CURRENT start)
//     -> no viewport write at all;
//   - otherwise TENTATIVELY center at the current zoom (viewport_start =
//     midpoint - visible/2, then clamp_viewport_start) and re-test the columns
//     under the clamped start: both in [0, W) -> the SCROLL stands (no zoom
//     change, no margin);
//   - else -> frame_span_into_view with margin (the cannot-fit fallback; the
//     framer only ever zooms OUT to fit — fit level + 2.5%-per-side, centered,
//     clamped [kMinZoom, effective ceiling], NO playhead recenter). It
//     OVERWRITES the tentative viewport wholesale (level + start via
//     apply_zoom_to_start), so the tentative write needs no revert.
//
// THE FRAMER'S no-op GUARD AND ITS ONE EXCEPTION (accepted, architect
// 2026-07-25, ratified after talk-through). apply_zoom_to_start's
// current-vs-target no-op normally cannot leave the failing tentative state
// standing, because a fit that failed at the current level forces the framer to
// a DIFFERENT (more zoomed-out) level to seat the MARGIN-widened span — the
// level differs, so the guard does not short-circuit. THE EXCEPTION is the
// CONJUNCTION the two code paths already embody: (a) an endpoint's painted
// column still fails the [0, W) test after the ceiling / start-0 clamp — which
// happens for ANY hi landing in the final half-pixel interval at the ceiling q,
// NOT only total-1 (e.g. W=1920, total=4,410,000, q=2296.875: hi = total-1000
// rounds to column W without ending at EOF) — AND (b) the margined fit request
// clamps back to that SAME ceiling, so apply_zoom_to_start no-ops and the
// ceiling rest at start 0 stands. Both are required: a NARROW EOF-ending span
// fails (a) but not (b) — e.g. [4,000,000, total-1] ends at EOF yet its
// 5%-widened span frames to a DEEPER level, exercising no no-op — while the
// (a)-failing wide case no-ops because its margined span is already at least
// song-wide. When the conjunction holds the endpoint rests AT or PAST the
// effective waveform's right edge: half-culled, or (at a non-multiple-of-16
// window width) sitting in the 0-15px inert right gutter, where a flag at its
// painted width can show WHOLE just outside the effective span — flag centers
// use the effective W (floored to a multiple of 16) while the flag surface
// spans the full strip. At the ruled deployment widths (1920 / 2560 / 3840, all
// multiples of 16) the gutter is empty and it half-culls. Either way NO route
// places the endpoint INSIDE the effective span at whole-song-visible — the
// standing flags-may-hang-half-offscreen geometry (cull only when FULLY out),
// the SAME cull the level-preserving navigation routes show there (Tab, which
// keeps the level; the marker-click land, which writes no viewport; and the
// trim-bar double-click framer itself, no-op under this conjunction) — not a
// framing defect, and identical under every option reachable within the
// whole-song-ceiling and centered-flag rulings. The futile framer call is left
// as-is (a harmless no-op there); a ceiling special-case would be a branch for
// ZERO behavioral difference. THIS WHOLE BODY DIVERGES from the trim-bar
// DOUBLE-CLICK's unconditional zoom-to-span; the framer itself is untouched.
//
// ACCEPTED COST on the framer arm: apply_zoom_to_start runs one sync render and
// each caller's own unconditional kick runs a second over identical final state
// — a bounded duplicate on a discrete keystroke (the keyboard zoom's per-press
// cost).
//
// IT WRITES ONLY THE VIEWPORT and damages nothing: the caller owns its own
// damage and its own sync kick, which is what keeps the restore behaviourally
// unchanged by the hoist (its tail already invalidated and kicked
// unconditionally) and what lets the SHOW-REGION command pay the same tail
// once. That command is MOMENTARY AND STATELESS and deliberately not a toggle
// — a lamp reading the region's EXISTENCE would strand a scrolled-away span
// behind a lit button whose only press cleared it — so it clears nothing,
// ever; the architect's own reasoning is at handle_show_region
// (input_trim.cpp).
// A degenerate geometry (q <= 0 or W <= 0) leaves the viewport put, matching
// the inline version's own guard.
void bring_span_into_view(AppState& app, const GuiAudio& audio,
                          Viewport& viewport, int64_t lo, int64_t hi) {
    const GuiRect area = waveform_area(app);
    const int     W    = area.w;
    const double  q    = painter_samples_per_pixel(app, audio, area);
    // Endpoint column under a given viewport start, on the flag painters' basis
    // (the shared displayed_column_at rounding, warp_frame_map_view.h).
    auto both_columns_visible = [&](int64_t vp_start) {
        const int lo_col = displayed_column_at(
            static_cast<double>(lo), static_cast<double>(vp_start), q);
        const int hi_col = displayed_column_at(
            static_cast<double>(hi), static_cast<double>(vp_start), q);
        return lo_col >= 0 && lo_col < W && hi_col >= 0 && hi_col < W;
    };
    if (q > 0.0 && W > 0 && !both_columns_visible(app.viewport_start_sample)) {
        // Tentatively center at the current zoom and clamp.
        const int64_t visible = samples_visible(app, audio);
        app.viewport_start_sample = (lo + hi) / 2 - visible / 2;
        clamp_viewport_start(app, audio);
        if (!both_columns_visible(app.viewport_start_sample)) {
            // Cannot fit at this level even centered -> zoom out to fit
            // (overwrites the tentative viewport wholesale).
            frame_span_into_view(app, audio, viewport, lo, hi,
                                 /*margin=*/true);
        }
    }
}

void GuiInputHandler::run_span_framing_command() {
    // The trim-bar double-click ZOOMS TO A SPAN, its own route beside the bare
    // `0` overview (run_overview_command) and `c`'s marker-jump working zoom.
    // It only ever FRAMES a span,
    // never the fine working zoom. Span priority: a live region wins (over a
    // trim); else a proper trim SUB-WINDOW; else the whole song
    // (full zoom-out — which is also where the FULL trim window lands, it being
    // the whole song). The framing is idempotent — a second double-click with
    // the viewport unchanged is a no-op (apply_zoom_to_start's current-vs-target
    // compare), while any pan/zoom between clicks re-frames.
    if (audio.total_frames() <= 0) return;

    const GuiRect area = waveform_area(app);
    const int     W    = area.w;
    const int     sr   = audio.sample_rate();
    if (W <= 0 || sr <= 0) return;
    const int64_t total = live_total_frames(app, audio);
    if (total <= 0) return;

    // The target span in ACTIVE-domain frames [lo, hi], and whether it takes the
    // framing margin (region / trim) or none (the whole song already fills the
    // window at the effective ceiling).
    int64_t lo = 0, hi = total;
    bool    margin = false;
    if (app.region.active) {
        // Region endpoints are already active-domain frames; the region wins
        // over any set trim.
        lo = std::min(app.region.a_frame, app.region.b_frame);
        hi = std::max(app.region.a_frame, app.region.b_frame);
        margin = true;
    } else if (!trim_is_full_window(app.trim, audio.total_frames())) {
        // A proper SUB-WINDOW frames itself. A FULL window is the old unset
        // state — nothing to frame beyond the whole song — so it falls through
        // to the whole-song arm exactly as an unset trim always did (the
        // recognition is the shared owner trim_window_is_full,
        // settings_file.h). The bounds come from compute_trim_samples, which
        // owns the range in source frames. Express both bounds in the ACTIVE
        // domain: source view
        // uses the source frames directly; target view maps each through
        // displayed_or_live_target_map — the same basis the flags, endcaps and
        // region paint at — which is identity on the empty source-view map, so
        // one call covers both views.
        const std::pair<long long, long long> trim_src =
            compute_trim_samples(app, audio.total_frames());
        const std::vector<WarpFrameMapSegment>& dmap =
            displayed_or_live_target_map(app, audio);
        lo = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(static_cast<double>(trim_src.first), dmap)));
        hi = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(static_cast<double>(trim_src.second), dmap)));
        if (hi < lo) std::swap(lo, hi);  // defensive; the monotone map keeps order
        margin = true;
    }

    // Frame the span through the shared framer (2.5%-per-side for region / trim,
    // none for the whole song — already whole-song at the effective ceiling,
    // start 0). Centering + the wall clamp make the whole-song arm degenerate to
    // the effective ceiling at start 0. The idempotent no-op lives in
    // apply_zoom_to_start inside the framer.
    frame_span_into_view(app, audio, viewport, lo, hi, margin);
}

// Shared wheel handler. TWO ARMS, BOTH EXACT-MATCHED.
//
// THE PLAIN WHEEL IS THE STEPPED PAN (architect 2026-08-12, the eighth glass
// ruling: "alt+wheel is step pan... that comes from Reaper" — with alt leaving
// the pointer entirely, the pan moved onto the PLAIN wheel): the alt+wheel's
// exact old body — the samples_visible / kViewportLeadDivisor stride through
// the scroll_viewport funnel, which is what carries the follow suppression —
// over the waveform, the overview lane and the top strip alike (every context
// id, one route; the two bools below say only "a wheel-live surface").
//
// CTRL+WHEEL IS THE ZOOM STEP (architect-ruled 2026-08-12, later the same day's
// field session): one whole zoom level per completed detent, up = in, down =
// out, dispatching the `=` / `-` commands' own bodies through Viewport::
// zoom_steps — the coalesced form whose final state equals calling
// zoom_in()/zoom_out() once per detent, including the floor's
// recenter-on-playhead and the effective ceiling's saturation. NOT A REVERT of
// that morning's wheel-zoom deletion: what was deleted is the PLAIN wheel zoom
// ("superseded by the ctrl-drag zoom... we already have the =/- hotkeys"), and
// the plain wheel remains the pan — this is a NEW binding on a chord that bound
// nothing, adding the zoom back as the ctrl-drag's own detent-sized sibling
// (same modifier, same axis, same surfaces).
//
// IT IS THE KEYBOARD ZOOM'S SEMANTICS, NOT THE DRAG'S, deliberately: dispatching
// the `=`/`-` bodies means it CENTERS on the playhead (the scanner while one
// runs, the resting cursor otherwise — apply_zoom_change's split) rather than
// pivoting on the column under the pointer, and it suppresses follow no more
// than the keys do, being a zoom that centers ON the scanner. The
// pointer-anchored pivot is what the ctrl-DRAG is for. Pure viewport move like
// the keys otherwise: no playhead write, no selection change, no region clear,
// no playback stop, read-only-legal.
//
// Every OTHER combination stays a swallowed no-op (strict modifier validation):
// alt+wheel, shift+wheel and every mixed pair alike.
void GuiInputHandler::handle_wheel(GuiMouseButton button, int count,
                                   bool ctrl, bool shift, bool alt,
                                   bool inside_waveform, bool inside_top) {
    if (!inside_waveform && !inside_top) return;
    // `count` is the net detent count coalesced for this pointer frame
    // (always >= 1 from the platform). Each arm scales its per-step quantity
    // by that count and applies it in ONE viewport call, so the damage /
    // hover / worker-kick path fires once per frame regardless of burst size.
    // count == 1 reproduces the single-detent behavior.
    if (count < 1) count = 1;
    if (!ctrl && !shift && !alt) {
        const int64_t step = std::max<int64_t>(
            1, samples_visible(app, audio) / kViewportLeadDivisor);
        viewport.scroll_viewport((button == GuiMouseButton::WheelUp ? -step : +step) * count);
        return;
    }
    if (ctrl && !shift && !alt) {
        // Positive steps zoom in. The platform's sub-detent accumulator keys
        // its remainder on the modifier chord as well as the hit region, and
        // clears it outright on any modifier change, so remainder grown while
        // panning can never complete a detent as a zoom (platform_wayland.cpp's
        // context key).
        viewport.zoom_steps(button == GuiMouseButton::WheelUp ? +count : -count);
    }
}

int GuiInputHandler::wheel_context(int x, int y) const {
    // The SINGLE wheel routing predicate, shared by two surfaces that must
    // never drift: the platform's per-frame accumulator probe (which decides
    // whether and under what context key sub-detent scroll remainder may
    // accumulate) and on_wheel's own completed-detent gate. Every swallow
    // gate below is a gate on_wheel would otherwise spell inline; keeping
    // them here means the platform never grows remainder in a context the
    // eventual emission could not fire in.
    //
    // Only the DIALOG modal surfaces swallow the wheel (the settings, load
    // and commit-title editors and the BpmBracket reuse of top_flag_editor
    // — membership re-greped 2026-08-12 at the dialog arc; the inherited
    // list had omitted the commit-title editor) —
    // modal_dialog_editor_active, deliberately NOT the keyboard gate's
    // keyboard_modal_editor_active. The top-strip flag editor IS keyboard-modal
    // (architect 2026-07-28) and the wheel still punches through it anyway,
    // because the wheel is NAVIGATION, not a chord, and that ruling is about
    // chords: panning while an edit is open changes no state the edit
    // owns and discards nothing, so there is nothing for modality to protect.
    //
    // The wheel routes by area — the waveform, the top strip, and the
    // OVERVIEW STRIP (2026-08-12: the lane is a navigation surface, so its
    // wheel is the stepped pan like the areas above it) — plus the ONE
    // row-wise carve-out below, the redesigned rows' inert band. ALL THREE
    // take the same one route since 2026-08-12 (the eighth glass ruling: the
    // plain wheel is the STEPPED PAN everywhere and the alt forms are deleted;
    // the same day's ctrl+wheel zoom step rides the identical route, the
    // modifier forking inside handle_wheel and never here), so the context ids
    // differ only for the platform's
    // sub-detent remainder attribution, harmlessly. Fewer regions is
    // strictly safer for the accumulator, and the inert band is the safest kind:
    // it emits nothing at all. THE TWO BLANK BANDS SPLIT (the relayout's commit
    // B, whose two flexible gaps center the waveform): GAP 2, between the
    // waveform and the bottom row, needs no band of its own — it lies below
    // every area this probe tests, so a wheel there falls to the no-context 0
    // exactly as the old blank foot's did — while GAP 1, between the menu row
    // and the centered block, lies INSIDE top_strip_area and therefore JOINS THE
    // INERT BAND LIST below, blank window ground being no more a panning surface
    // at the top of the window than at its foot.
    //
    // A wheel event during ANY active pointer gesture is ignored, matching
    // on_button_press and the keyboard's drag-modal gate. The region drag
    // is included: the keyboard gate swallows every authoring chord
    // mid-drag, so viewport changes must not slip through either.
    // The editor-text drag is included: viewport changes must not fire under a
    // held text-selection drag either (a wheel can emit axis events while the
    // primary button stays held), so this gate is any_pointer_gesture_active,
    // every gesture — the marker flag's pending click included, so a wheel
    // cannot pan or zoom the viewport out from under a flag press before it
    // resolves.
    if (app.prompt.active) return -1;
    // AN OPEN DROPDOWN SWALLOWS THE WHEEL. It cannot close from here — this
    // predicate is const and the platform probes it speculatively — so on_wheel
    // owns the close and this owns only the swallow, which is also what keeps
    // the sub-detent accumulator from growing remainder under a popup.
    if (app.dropdown.open()) return -1;
    if (modal_dialog_editor_active()) return -1;
    if (app.loading || audio.total_frames() <= 0) return -1;
    if (any_pointer_gesture_active(app)) return -1;

    // THE REDESIGNED ROWS ARE WHEEL-INERT (architect 2026-07-31) — ONE decision
    // for the whole family, recorded here where the routing lives, and every
    // future row inherits it by joining this band list. A wheel over any
    // redesigned row does NOTHING: those rows are chrome with no scrollable
    // content and no relation to the waveform under them, so passing the event
    // through to the top-strip zoom/pan (which is what happened before row 1's
    // ruling) made a scroll over row 1's buttons silently zoom the song. They sit
    // ABOVE the area tests below because they are sub-bands of the top strip and
    // must win over it. Returning -1 (rather than 0) also stops the platform's
    // sub-detent accumulator from growing remainder over them.
    {
        const GuiRect bands[] = {
            // (The toolbar row's band left with its lane, 2026-08-12 — the
            // relayout dissolved row 2 into the icon row, whose band below
            // covers its four buttons now.)
            top_menu_row_area(app),
            // GAP 1's blank band, the ONE non-lane member (commit B): it sits
            // inside the top-strip area below, which pans, so without this
            // entry a wheel over blank window ground would scroll the song —
            // exactly the fault row 1's ruling named. Not a redesigned row, but
            // the same answer for the same reason, and the reason it needs a
            // rect at all is at top_flex_gap_area (app_state.h).
            top_flex_gap_area(app),
            top_tab_row_area(app),   top_icon_row_area(app),
            // The bottom row joined the family's inert band list 2026-08-11
            // (as the transport row), exactly as the rule above promises a
            // future row would, and the 2026-08-12 unification widened its
            // band to the whole merged lane — the clock cell and the modal
            // that displaces it are as wheel-inert as the buttons. It is a bottom-strip
            // lane, so the top-strip area test below would never have routed it
            // anyway; membership here is what stops the sub-detent accumulator
            // growing remainder over it, like its four siblings.
            bottom_row_area(app),
        };
        for (const GuiRect& b : bands) {
            if (rect_contains(b, x, y)) return -1;
        }
    }

    // THE OVERVIEW STRIP (context 3): a navigation surface, so the wheel is
    // the stepped pan there. TESTED BEFORE THE AREAS since the relayout's
    // commit B: the lane is a TOP-STRIP lane now (it was disjoint from both
    // areas while it sat in the bottom strip, and this clause followed them),
    // so the top-strip test below would otherwise answer 2 over it first. Both
    // ids take the same one route, so the ordering costs nothing but the id's
    // honesty — and the id is what the platform attributes sub-detent remainder
    // to. A positive context here also admits the touch nav's per-frame frames
    // over the lane, which is the same navigation-class answer — and is exactly
    // why apply_touch_nav_update carries its OWN thin-lane refusal: this
    // predicate is the WHEEL's routing owner and the wheel stays live here, so
    // the nav gesture's refusal could not be folded into it.
    if (rect_contains(top_overview_row_area(app), x, y)) return 3;
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const bool inside_waveform = rect_contains(area, x, y);
    const bool inside_top      = rect_contains(top, x, y);
    if (inside_waveform) return 1;
    if (inside_top) return 2;
    return 0;
}

// Coalesced wheel entry point. The platform delivers one of these per
// pointer frame carrying the net detent count (>= 1), instead of pumping a
// WheelUp/WheelDown through on_button_press once per detent. The gating —
// prompt / editor modals, loading, active gestures, area hit-test — lives in
// wheel_context above, the same predicate the platform's sub-detent
// accumulator probes, so a wheel event is swallowed in exactly the same
// situations the platform refuses to accumulate remainder in.
void GuiInputHandler::on_wheel(GuiMouseButton dir, int count, int x, int y,
                               GuiInputState mods) {
    // Double-click lifecycle, WHEEL half: a wheel command between two clicks
    // moves content under the pointer (a zoom rescales, a pan slides), so the
    // second click must not consume as a double-click of the first. Clear every
    // pending candidate here, exactly as on_key's keyboard half does — the
    // platform delivers one on_wheel per pointer frame (net detent count >= 1)
    // after accumulating sub-detent remainder itself, so every call here is a
    // completed detent frame, matching on_key's unconditional placement.
    app.double_click = DoubleClickCandidate{};
    // A WHEEL DISMISSES BOTH FLOATING SURFACES: the popup closes (and the event
    // is consumed — wheel_context refuses it while open, so nothing scrolls
    // under it), and the tooltip hides. Both run before the context test so the
    // dismissal happens on the very frame the wheel arrives.
    close_dropdown();
    hide_shift_tooltip();
    const int ctx = wheel_context(x, y);
    if (ctx < 0) return;
    // ctx: 1 waveform, 2 the top strip, 3 the overview strip. All three take
    // the same two-arm vocabulary — plain = the stepped pan, ctrl = the zoom
    // step (the eighth glass ruling and the same day's ctrl+wheel binding). The
    // overview rides the waveform's slot: handle_wheel only asks "am I on a
    // wheel-live navigation surface", and the lane is one — so the modifier
    // alone picks pan or zoom there too. THE CONTEXT ANSWER IS
    // MODIFIER-INDEPENDENT by construction (wheel_context takes only x/y and
    // reads no modifier state), so ctrl cannot change WHERE the wheel is live,
    // only what it does there.
    handle_wheel(dir, count, mods.ctrl, mods.shift, mods.alt,
                 ctx == 1 || ctx == 3, ctx == 2);
}

bool GuiInputHandler::apply_editor_clipboard(
        text_editor::KeyAction action, text_editor::State& s) {
    switch (action) {
        case text_editor::KeyAction::CopyRequested:
            gui.clipboard_set_text(text_editor::selected_text(s));
            return true;
        case text_editor::KeyAction::CutRequested:
            gui.clipboard_set_text(text_editor::selected_text(s));
            text_editor::replace_selection(s, std::string());
            return true;
        case text_editor::KeyAction::PasteRequested: {
            // The bytes come from the SYSTEM clipboard, so a URL copied in a
            // browser pastes straight into a settings field. GuiPlatform
            // short-circuits to our own last-published payload while we still
            // hold the selection, so a copy-then-paste inside this process
            // never touches the pipe.
            //
            // AN EMPTY ANSWER IS A CONSUMED NO-OP: no offer, no text mime on
            // the offer, or a failed read all mean there is nothing to paste,
            // and pasting nothing must not delete the selection instead.
            //
            // NO FILTER HERE. replace_selection is the product's one incoming
            // text filter (printable ASCII plus well-formed UTF-8, controls and
            // malformed bytes dropped a byte at a time), and an external
            // clipboard is precisely the boundary it was written for.
            std::string text = gui.clipboard_get_text();
            if (text.empty()) return true;
            text_editor::replace_selection(s, text);
            return true;
        }
        default:
            return false;
    }
}

std::expected<std::vector<WarpFrameMapSegment>, std::string>
validate_target_view_entry(const std::vector<GuiWarpMarker>& markers,
                           double scale, int sample_rate, long total_frames) {
    // Contract and caller list in input_handler.h: this is the shared
    // entry-half validity predicate for target view — the keyboard S → T
    // toggle below and GuiFileLoader::load_file's active_audio_view=T
    // restore gate on exactly this walk. The resolver normalizes ambiguous
    // marker arrangements to tempo 1.00 (one stderr line per timestamp),
    // and trim plays no part (crossed/equal cannot rest; an ambiguous trim
    // at render time falls back to untrimmed), so entry effectively gates
    // only on the tripwire-class build failures.
    auto resolved = resolve_warp_markers_for_render(
        slice_to_warp_markers(markers), sample_rate, total_frames);
    auto r = build_warp_frame_map(resolved, scale, sample_rate, total_frames);
    if (!r) return std::unexpected(std::move(r.error()));
    return std::move(*r);
}

void GuiInputHandler::handle_active_audio_view_toggle() {
    // Audio must be loaded — `t` is a silent no-op in blank state.
    // (The blank/loading guard near the top of dispatch_key_command already
    // covers this, but the helper is defensive in case future callers reach
    // it from elsewhere.)
    if (audio.total_frames() <= 0) return;

    // Build the current warp_frame_map from the live warp marker store.
    // Same resolve-then-build pipeline the render pipeline runs, so the
    // visible deformity in target view matches what the engine would emit.
    // Trim is a render-time cut, not a view-time concept: build_warp_frame_map
    // builds the WHOLE-song map, and the toggle translates source-frame
    // viewport / playhead / total_frames across the whole song against it —
    // see the matching comment in paint_handler.cpp's per-paint recompute.
    //
    // Validity gate, entry half: entering target view (S → T) gates only on
    // the tripwire-class build failures — marker arrangements always enter,
    // because the parser resolver normalizes ambiguous arrangements to
    // tempo 1.00 at resolve time (one stderr line per timestamp), and trim
    // plays no part (crossed/equal cannot rest, and an ambiguous trim at
    // render time falls back to the full, untrimmed deliverable, so there
    // is no unrenderable window to protect playback from); nothing gates or
    // modals in the GUI. The predicate is validate_target_view_entry
    // (definition above): resolve, then build; it returns the built map, so
    // entry validation and the translation map below are one build.
    // GuiFileLoader::load_file gates its active_audio_view=T restore on the
    // SAME predicate — a load restore and a keystroke entry block
    // identically. A refusal — the engine-metadata /
    // non-positive-tempo-product class, unreachable from program-written
    // input — surfaces through the error-notice popup with the owner's
    // error string.
    //
    // Leaving target view (T → S) never gates: a resolve/build failure is
    // ignored — the exit falls back to the empty map — identity translation
    // for the playhead/viewport — and always succeeds.
    const bool entering_target = (app.active_audio_view == 'S');
    std::vector<WarpFrameMapSegment> warp_frame_map;
    auto entry = validate_target_view_entry(
        app.warpmarkers.markers(),
        app.engine_settings.scale,
        audio.sample_rate(), static_cast<long>(audio.total_frames()));
    if (entry) {
        warp_frame_map = std::move(*entry);
    } else if (entering_target) {
        prompt.open_error_notice(std::move(entry.error()));
        return;
    }

    // THE HISTORY MODE'S OWN FOCUS CLEARS ON THIS SWITCH, exactly as it clears
    // on a `,` / `.` step and for the identical reason — it is an ordinal into
    // the PAINTED diff-flag list, and the switch is about to rebuild that list
    // (the contract, and the full clearer list, are at AppState::HistoryMode::
    // focus). This axis's clear lives HERE, at the S/T chokepoint, so the bare
    // `t` key, the icon row's S/T radios and the 1/2/3 selectors that compose
    // this handler all inherit it with no second route; the W/P axis clears at
    // its own toggle. Placed BELOW the refusal above (a switch that never
    // happened must not clear anything) and far above the kick at the tail,
    // which is what rebuilds the flag cache — `focus` is one of that cache's
    // fingerprint fields, so it has to be settled before the rebuild reads it.
    // Unconditional: with the mode down the pair already rests empty, the
    // whole-struct reset at close_history_mode having put it there. It goes
    // through the ONE clearer, which takes the mode's multi-selection with the
    // focus for the identical reason (clear_history_mode_focus, app_state.h).
    clear_history_mode_focus(app.history_mode);

    // The warp flag editor is a source-view-only authoring surface (the
    // home-view binding rule, 2026-07-22). The S -> T toggle would strand a
    // live one on a now-refusing target surface, so close it WITHOUT
    // committing (the exact Esc teardown) before the flip proceeds. Guarded
    // internally on an active editor, so this is a no-op when none is open.
    // Only `t` and the settings-editor `active_audio_view=` commit route here
    // into target view — Ctrl+Tab never changes active_audio_view — so this
    // is the one place the toggle-into-target edge is handled.
    if (entering_target) flag_editor.exit_top_flag_edit_no_commit();

    // (NOTHING HAPPENS TO ITERATION MODE ON THIS EDGE — the record of a
    // DELETED wipe, kept because the invariant it created was leaned on in
    // four other files. Until 2026-08-07 an S -> T entry EXITED iteration mode
    // through wipe_iter_state, clearing every marker's bracket and pushing one
    // undo entry, on the architect's 2026-07-23 ruling that "brackets are the
    // step-away batch tool, target view the live-by-hand tool". THAT RULING IS
    // SUPERSEDED: ITERATION MODE IS TARGET-LEGAL (architect 2026-08-07). Its
    // premise — that target view is where tempo is authored live, by hand —
    // stopped holding when contortion ruling 8 (2026-07-29) made the bare
    // Up/Down cent step the whole tempo surface and admitted it in W+target,
    // and nothing technical ever bound a bracket to source view: a bracket is
    // DELTA-RELATIVE (the sweep enumerates base + delta per cell), so it
    // composes with any base-tempo change from either view. So the mode bit
    // and the brackets now PERSIST ACROSS S <-> T IN BOTH DIRECTIONS, and
    // mode-off-in-target is no longer an invariant anywhere. What did NOT
    // change: bracket AUTHORING is still source-only — the flag editor's iter
    // grammar keeps its home-view gate through both open routes, and the
    // editor teardown one line above still runs. The settings-editor
    // active_audio_view=T commit routes through this same edge and inherits
    // the persistence exactly as it inherited the wipe.)

    // Target-view playback is rebound to the rendered target buffer once it is
    // ready, and Space is gated while that buffer is unavailable or updating.
    // Stop on every toggle so playback never chases a playhead in the other
    // domain. Mirrors the viewport-mutator pattern of
    // "stop_playback_if_playing before mutating playhead state".
    playback_lifecycle.stop_playback_if_playing();

    // Anchor the toggle on the playhead's pre-flip screen-pixel column.
    // Compute ph_px now, translate the playhead through the warp_frame_map,
    // then derive the new viewport_start so the translated playhead
    // occupies the same column. zoom_level is preserved across the
    // flip (the visible time span will differ by the warp_frame_map's net
    // stretch — that is the deformity made visible). clamp_viewport_start
    // below pins viewport_start to file bounds; when the playhead sits
    // near the start / end of the file, the clamp moves the playhead's
    // column toward the edge instead of forcing it inside the file.
    const double cur_spp = current_samples_per_pixel(app, audio);
    const double ph_px =
        (cur_spp > 0.0)
        ? (static_cast<double>(app.playhead_cursor_sample -
                               app.viewport_start_sample) / cur_spp)
        : 0.0;

    // The translated playhead stays a DOUBLE until clamp_dest below: the
    // int64 conversion happens only inside the clamp, whose bound is the
    // post-flip destination total.
    double new_playhead_d = static_cast<double>(app.playhead_cursor_sample);

    bool going_to_target = false;
    if (app.active_audio_view == 'S') {
        // S → T: forward-translate the playhead. The deformed-domain
        // total is derived from the warp_frame_map cache by live_total_frames,
        // so the post-flip viewport math needs no cached total here.
        new_playhead_d = map_source_to_target(
            static_cast<size_t>(app.playhead_cursor_sample < 0
                                ? 0 : app.playhead_cursor_sample),
            warp_frame_map);

        app.active_audio_view = 'T';
        going_to_target = true;
    } else {
        // T → S: inverse-translate the playhead.
        new_playhead_d = map_target_to_source(
            static_cast<size_t>(app.playhead_cursor_sample < 0
                                ? 0 : app.playhead_cursor_sample),
            warp_frame_map);

        app.active_audio_view              = 'S';
    }

    // Event-synchronized hit geometry: the live view just flipped, so the
    // displayed hit map's previous contents describe the OTHER view's last item
    // pixels. Clear it — AND the staged value, or a stale staged map would
    // promote the wrong geometry at the next paint — to the cold live-map
    // fallback for the one tick until the flag item cache rebuilds,
    // stage the new view's map, and a frame promotes it — the recorded
    // cold-state seam, not a new one. This is the sole gesture toggle
    // chokepoint (the `t` key and the settings-editor `active_audio_view=`
    // commit both route here). Ruling at the selector.
    app.displayed_target_warp_frame_map.clear();
    app.staged_displayed_target_warp_frame_map.clear();
    // The displayed-viewport mirror (sibling of the map) resets to cold too, so
    // the lane geometry falls back to the live viewport until the new view's
    // item caches stage a fresh basis and a frame promotes it.
    app.displayed_vp_start = 0;
    app.displayed_vp_end   = 0;
    app.displayed_area_w   = 0;
    app.staged_displayed_vp_start = 0;
    app.staged_displayed_vp_end   = 0;
    app.staged_displayed_area_w   = 0;
    app.staged_displayed_valid = false;

    // The region-select span is in the ACTIVE display domain, which just
    // flipped (source <-> target frames). Its endpoints are meaningless in the
    // new domain, so clear it; the full-window invalidate at the tail repaints
    // the waveform on its plain canvas ground.
    app.region = RegionState{};
    // AND THE SEATED PINCH'S ANCHOR GOES WITH IT, for the identical reason
    // (codex round 20): TouchNavZoomState::anchor_sample is an ACTIVE-DOMAIN
    // song frame, and nothing about two fingers resting on the glass stops a
    // keyboard `t` or a mouse click on the S/T radio from reaching here — so a
    // pinch held across this flip would go on zooming about a SOURCE frame read
    // as a TARGET one, seating the view on an unrelated song point. THIS IS THE
    // CORRECTNESS member of the rule: it is the one view write that changes what
    // the stored number MEANS. It sits here, beside the two assignments above,
    // because since codex round 21 the clear rides the WRITES of the active view
    // state rather than the commands that reach them — the derivation, the whole
    // membership and the do-not-add-touch-to-any_pointer_gesture_active note are
    // at the free function's declaration (input_handler.h). The pre-arc
    // STATELESS pinch could not have this defect — it kept nothing between
    // frames — which is why the seat is what brought it.
    clear_touch_zoom_seat(app, viewport);

    // THE GROUP CARRIES ACROSS THE FLIP (architect 2026-07-30, resolving the
    // deferral this block used to record): `t` translates the same markers into
    // another domain rather than changing which column is addressed, so a 2+
    // selection survives it BY IDENTITY — the collapse-to-focus that stood here
    // is deleted. It existed to keep a group from resting SPANLESS, and with the
    // SPAN FORM retired there is no such state to avoid: the group's cue is its
    // members' brightened flags plus the always-visible cursor, and the
    // selection-gated land below re-expresses the focus EXACTLY, which is what
    // seats that cursor where the readout says it is. The region clear above is
    // STRUCTURAL and stays (its endpoints are ACTIVE-DOMAIN frames and the domain
    // just flipped); nothing re-derives one, the region being trim scratch.
    // ALL THREE CALLERS get this: the bare `t` key, the settings editor's
    // `active_audio_view=` GUI-key twin, and the propagate paste's tail (moot
    // there — its column swap clears the selection immediately after).

    // The S/T toggle translates the active tab's live playhead across the
    // domain flip; the inactive tab's stored playhead must translate too, or
    // a later Ctrl+Tab loads a stale-domain position that gets read in the
    // new domain. Same warp_frame_map, same direction as the active
    // translation.
    // To keep the inactive tab's playhead at the same on-screen column after
    // the toggle (matching the active-tab invariant above), the viewport is
    // shifted by the same delta as the playhead rather than translated
    // independently — translating both endpoints separately through the
    // nonlinear warp_frame_map was what caused the slide. At a fixed zoom
    // level the samples-per-pixel is domain-invariant, so equal sample deltas
    // map to equal pixel columns; at the effective zoom-out ceiling the
    // viewport is re-clamped to zero on the next tab activation anyway, so the
    // shifted value is harmless. The active tab's own slot is left stale on purpose — it
    // resyncs at the next stash boundary.
    // Playhead domain clamp on the domain flip, both tabs, applied to each
    // translated value BEFORE its viewport delta / anchor math is computed
    // from it: a nearbyint of a translated in-domain playhead can land
    // exactly on the destination total (e.g. a playhead parked on the last
    // frame). The destination total is the post-flip live_total_frames —
    // active_audio_view flipped above, so this is exactly the total the
    // subsequent viewport math (current_samples_per_pixel,
    // clamp_viewport_start) reads; move_playhead_to holds the domain
    // ruling ([0, total - 1]). Both tabs live in the one global domain, so
    // one total clamps both.
    //
    // The clamp runs in the DOUBLE domain, before the nearbyint/int64
    // conversion: the cast's domain must be guaranteed at the cast itself,
    // not by callers' store hygiene — a translated extreme value (the map's
    // identity tail extrapolates any query, and float→int64 conversion of a
    // result at or past 2^63 is undefined) is defused here regardless of
    // how it got in. For every finite input whose old post-cast conversion
    // was defined this is byte-identical: the bound double(dest_total - 1)
    // is an exact integer double for any product-reachable total, so
    // nearbyint(clamp(v)) == clamp(nearbyint(v)) — in-domain values are
    // unchanged. The `!(v >= 0.0)` spelling also rests a NaN at 0.
    const int64_t dest_total = live_total_frames(app, audio);
    const auto clamp_dest = [dest_total](double v) -> int64_t {
        const double hi =
            static_cast<double>(dest_total > 0 ? dest_total - 1 : 0);
        if (!(v >= 0.0)) v = 0.0;
        if (v > hi) v = hi;
        return static_cast<int64_t>(std::nearbyint(v));
    };
    const int64_t new_playhead = clamp_dest(new_playhead_d);

    {
        ViewState& other = (app.active_tab_view == 'B') ? app.tab_a : app.tab_b;

        // Returns the raw translated DOUBLE; the int64 conversion happens
        // only inside clamp_dest (the double-domain guard above).
        const auto xlate = [&](int64_t s) -> double {
            const size_t q = static_cast<size_t>(s < 0 ? 0 : s);
            return going_to_target
                ? map_source_to_target(q, warp_frame_map)
                : map_target_to_source(q, warp_frame_map);
        };

        const int64_t other_old_ph = other.playhead_cursor_sample;
        const int64_t other_new_ph = clamp_dest(xlate(other_old_ph));
        other.playhead_cursor_sample = other_new_ph;
        // Shift the stored inactive viewport by the same playhead delta, but
        // keep the result a load-shaped band: non-negative and overflow-safe.
        // The stored viewport is any canonical int64 (settings editor / loaded
        // file), so the raw signed add can overflow, and a domain-contracting
        // flip (a legal playhead far right of its viewport under a faster map)
        // can drive an unchecked += below zero — either would let a normal
        // gesture plus Ctrl+S serialize an int64 the load grammar refuses.
        // The subtraction cannot overflow (other_new_ph is clamp_dest output in
        // [0, dest_total - 1] and other_old_ph is a non-negative int64, so the
        // difference lies in (INT64_MIN, INT64_MAX]); the add saturates toward
        // the delta's sign, then max(0, .) floors it. No band-aware upper/domain
        // clamp is applied — stored bands are load-shaped only (a slowing map
        // legitimately persists positions past the source total), and the
        // Ctrl+Tab restore's runtime clamp owns the domain fit exactly as it
        // does for values read from disk. Non-negative + overflow-safe is the
        // writable-implies-loadable bar.
        const int64_t delta = other_new_ph - other_old_ph;
        int64_t moved;
        if (__builtin_add_overflow(other.viewport_start_sample, delta,
                                   &moved)) {
            moved = delta >= 0 ? INT64_MAX : 0;
        }
        other.viewport_start_sample = std::max<int64_t>(0, moved);
    }

    // Domain is flipped — current_samples_per_pixel below reads the
    // post-flip live_total_frames against the preserved zoom_level.
    app.playhead_cursor_sample = new_playhead;
    const double new_spp = current_samples_per_pixel(app, audio);
    const double new_vp_d =
        static_cast<double>(new_playhead) - ph_px * new_spp;
    app.viewport_start_sample =
        static_cast<int64_t>(std::nearbyint(new_vp_d));

    // Clamp viewport into the new domain's bounds, then full-window
    // invalidate so the icon row's lit S/T pair, the waveform
    // surface, and the playhead column all repaint in one frame. (The S/T
    // indicator was the bottom strip's until the row-7 collapse deleted the
    // letters — row 4's buttons carry that state now.)
    clamp_viewport_start(app, audio);
    // A SURVIVING SELECTION RE-EXPRESSES THROUGH ITS FOCUS, NOT THROUGH THE
    // CURSOR. The generic translation above is a double round trip — the cursor
    // is already an integer frame in the OLD domain, and mapping it plus
    // rounding again need not return the focused marker's image: at a legal
    // 1/4 slope a marker at source 1001 paints at target 250, whose inverse is
    // source 1000, so the collapse would leave the focus at 1001 with the
    // cursor written to 1000 and every later Space / arrow reading the
    // stale point. THE MARKER LANE OWNS THE PLAYHEAD
    // (land_playhead_on_marker's doctrine, input_pointer.cpp): pre-switch the
    // cursor rests ON the focus by that same premise, so landing on the focus in
    // the new domain IS the correct re-expression of it — the map-change re-land
    // form the singleton tempo step's label-coupling fix established. It lands on
    // the FOCUS whatever the selection's size (architect 2026-07-30, with the
    // collapse dropped): a carried GROUP re-expresses through its focus exactly
    // as a singleton does, which is the whole reason the collapse was droppable.
    // An EMPTY selection has no focus to re-express and keeps the generic
    // translation untouched — that gate is what makes this "t keeps the cursor
    // where it is" for the on-marker case and nothing at all otherwise.
    // PLACED HERE by domain validity: active_audio_view flipped far above and the
    // collapse has run, so source_frame_to_active_domain reads the NEW view's map
    // (the live target-view map cache, which rebuilds on demand and does not wait
    // for the kick below). A pure cursor write with no viewport move, so the
    // viewport anchoring computed just above stands: the landed frame and the
    // generic translation can differ by SEVERAL frames — the double round trip
    // has no one-frame bound, and a more compressed legal product widens it (at
    // the 1/4 slope, source 1002 maps to target 250.5, rounds to 250, and
    // inverses to source 1000) — but the gap stays well under one PAINTED COLUMN,
    // which is what the anchoring cares about: a column is at least ~27 frames at
    // the deepest zoom the product allows. The frame count is not the premise;
    // the column is.
    if (!app.selected_markers.empty() && app.last_selected_marker >= 0)
        land_playhead_on_marker(app, audio, viewport, app.last_selected_marker);
    // One-shot discrete jump with a domain change: is_target, the viewport, and
    // the warp_frame_map hash all flip, so the displayed plate must change. Render it
    // synchronously and publish the displayed fingerprint now, so the
    // icon row's lit S/T pair and the playhead column do not repaint a frame
    // ahead of the deformed waveform. The plate is built from source audio plus
    // the live warp_frame_map, independent of the target render buffer, so this is
    // unaffected by the ensure_ready / rebind_to_source below.
    viewport.kick_waveform_sync();
    gui.invalidate_region(0, 0, app.width, app.height);

    if (going_to_target) {
        // S → T: ensure playback is bound to a current target buffer.
        // ensure_ready short-circuits to a clean rebind if no edits
        // have invalidated the cached buffer since the last successful
        // render; otherwise it falls through to trigger()'s cancel-
        // clear-dispatch sequence so a fresh render runs against the
        // current engine input.
        target_render.ensure_ready();
    } else {
        // T → S: cancel any in-flight target render and rebind
        // playback to source.wav. No replacement dispatch — source
        // view's playback reads source.wav across archival renders.
        target_render.rebind_to_source();
    }
}

void GuiInputHandler::apply_gui_scale(int percent) {
    // The shared live sequence a source load's apply_settings_engine_and_prefs
    // tail runs, and the only one left since row 7 deleted apply_font_size with
    // its key (architect approval 2026-08-01): the value feeds main.cpp's
    // per-lane height table and every other painted dimension, so a change moves
    // every strip boundary and the waveform area with them. Four steps —
    // assign, push, full
    // invalidate, resize-path geometry-and-cache rebuild — so the new layout is
    // on screen at the commit rather than at the next restart. The sole caller
    // (the settings editor's `gui_scale=` commit) gates the no-op case.
    app.gui_scale = percent;
    set_gui_scale_percent(percent);
    viewport.invalidate_all();
    paint_handler.on_resize(app.width, app.height);
}
