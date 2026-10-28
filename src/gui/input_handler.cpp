#include "input_handler.h"

#include "engine/engine_geometry.h"  // kN
#include "folder_overlay.h"          // the overlay's wheel context and scroll
#include "gui_display_context.h"
#include "paint_handler.h"
#include "icons.h"                  // drop_rasters on a scale change
#include "materials.h"              // the materials' rebake at True Colors and a scale commit
#include "onscreen_keyboard.h"      // the keyboard band's wheel swallow
#include "render.h"
#include "device_config.h"
#include "settings_io.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "warpmarkers.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <optional>
#include <utility>
#include <vector>

// Keyboard input handler event entry points (on_key, the pointer handlers,
// on_wheel), dispatching into the operation structs through the reference
// members (warpops, phase_resets, flag_editor, renders_dir, active_views,
// playback_lifecycle, save_ops, prompt, selection, undo, viewport).
// compute_base_tempo_scale + BaseTempoScale (and bpm_cell_warp_markers)
// live in input_handler.h so this TU and flag_editor.cpp can reach them.

// THE DRAG GATES' ONE SENTENCE (architect 2026-08-30): the keyboard is
// swallowed while a gesture holds the pointer, and the FOUR sites that
// swallow it refuse for the SAME reason and therefore say the same words —
// the editor text-selection drag's gate, above the editor handlers; the
// pointer gestures' drag-modal gate below them; and THE RENDER PLAYER'S AND
// THE PICKER'S OWN ARMS above both (re-greped 2026-09-30 off this constant's
// raisers, the AV Sync Stats panel's arm having gone with the panel), which
// outrank that gate and so ask its
// question themselves rather than letting the band's row press or the scrub's
// marker drag swallow a key in silence. One literal, so a retune moves all
// four. (THE CAR'S TWO ROADS raise it too, re-greped 2026-09-17 — the undo /
// redo road run_undo_redo_without_key and the play's gate
// car_play_refused_by_key_gates, twice each, asking the first two gates' own
// verdicts for a chord that never passes through on_key; neither is a sixth
// gate.)
//
// THE FIRST TWO SPEAK ONLY FOR A BOUND CHORD and the last two speak for every
// chord (the unbound-keys ruling, chord_is_bound in gui_input.h): the two
// below sit on the MAIN DISPATCH, where an unbound press is answered by
// silence, while the two mode arms sit inside a mode whose
// router is the whole vocabulary — bare `r` flips Repeat one in there — and
// that
// vocabulary is not what the inventory knows, so those two ask nothing and
// answer every swallowed key.
constexpr const char* kKeysDuringDrag = "Keys are ignored during a drag";

// THE REASON CHANNEL'S ONE READER IN THIS TU (architect 2026-08-30): the
// authoring ops compose their own refusal sentences and RETURN them
// (GuiOpRefusal, warpmarkers_ops.h — the split's rationale is stated there),
// and every dispatch arm that calls one raises whatever comes back. An empty
// answer is the act having run, or having refused on something an outer gate
// already carded; either way there is nothing to say.
static void card_op_refusal(GuiNotifications& notifications,
                            GuiOpRefusal reason) {
    if (reason)
        notifications.notify(AppState::NotificationClass::Normal,
                             std::move(*reason));
}

namespace {

// THE HELD-REPEAT CARVE-OUT'S SCOPE (architect 2026-09-01, the duplicates
// ruling): deliberate presses stack their duplicate cards and a held input's
// SYNTHESIZED REPEATS coalesce into one, so the notify chokepoint has to know
// which kind of fire it is answering — and the raise sites are everywhere on
// this dispatch (the gates here, the ops' returned GuiOpRefusal sentences, the
// editors' refusals), so threading a parameter down every chain would be a
// dozen signatures for one fact that arrives once, at the top, with the event.
// This guard writes that fact into AppState::Notifications for the length of
// the dispatch and takes it away again at EVERY return of on_key's body,
// including the early ones the gates take — which is what keeps a card raised
// by a worker, a pointer gesture or a paint from ever reading a stale true.
//
// IT SAVES AND RESTORES rather than clearing: on_key does not re-enter itself
// today (the two synthetic callers are the roster's lift and its repeat burst,
// both from the pointer road), and restoring the previous value is what would
// keep a future nested dispatch honest instead of silently ending the outer
// one's scope.
struct HeldRepeatDispatchScope {
    AppState& a;
    bool      was;
    HeldRepeatDispatchScope(AppState& app_, bool repeat)
        : a(app_), was(app_.notifications.held_repeat_dispatch) {
        a.notifications.held_repeat_dispatch = repeat;
    }
    ~HeldRepeatDispatchScope() {
        a.notifications.held_repeat_dispatch = was;
    }
};

// A body run at every return of the scope it is declared in (on_key's
// modal-tooltip end below, whose body's many early returns are the reason).
template <typename F>
struct AtScopeExit {
    F f;
    ~AtScopeExit() { f(); }
};

} // namespace

// (THE MENU TITLES' ACCESS KEYS — Alt+F, Alt+E and Alt+S, the File, Edit
// and Settings titles' press from 2026-10-10 — went with the menu row,
// 2026-10-21; menu_title_access_key with them.)

// THE HANDS-OFF END (the rule and its entries at the declaration): one enum
// compare when no act stands, which is nearly every press.
void GuiInputHandler::end_audition_for_press() {
    if (!audition_sequence_standing(app)) return;
    playback_lifecycle.stop_playback_if_playing();
}

void GuiInputHandler::on_key(GuiKey key, GuiInputState mods) {
    // The dispatch's own repeat scope, first statement so it covers the whole
    // body (the ruling and the reasoning are at the guard above).
    const HeldRepeatDispatchScope repeat_scope(app, mods.synthesized_repeat);
    // Double-click lifecycle, KEYBOARD half: any keyboard command between two
    // clicks breaks EVERY pending double-click candidate (TrimBar, Marker,
    // EmptyLane alike) at this one chokepoint — no legitimate double-click types
    // a key between its two presses, and a cross-context consume (seed a
    // candidate, run a command, click again to consume in a different context)
    // must not fire. The consume lives entirely in on_button_press (nothing on
    // the keyboard path reads the candidate), and key-repeat re-entering here is
    // equally fine — no candidate can survive a held key. The pointer-side
    // per-branch clears (the on_button_press top-of-frame clear, the moved-drag
    // clears, the force-end finalizer's clear) stay: they own the pointer half of the
    // lifetime; this owns the keyboard half, and on_wheel owns the wheel half
    // (the same clear at its own entry).
    app.double_click = DoubleClickCandidate{};
    // THE A/B AUDITION IS HANDS-OFF (architect 2026-10-28; the rule at
    // end_audition_for_press's declaration): a bound chord ends a standing act
    // here, ahead of every gate below, bare Esc among them. THE TWO
    // EXCEPTIONS are bare Space and Shift+Space, the only bound Space chords
    // (chord_is_bound): their own dispatch forks on the standing act and
    // takes its stop side — toggle_playback reading `phase != Idle` as
    // transport-live, GuiAbAudition::start routing into the stop body — so
    // ending it here would turn Space into a launch and Shift+Space into a
    // restart.
    if (key != GuiKeys::Space &&
        chord_is_bound(key, mods, app.history_mode.active))
        end_audition_for_press();
    // KEYS DO NOT HIDE THE HOVER TOOLTIP (architect 2026-10-06, Windows 95's
    // tooltip control, which processes mouse messages alone — the model is at
    // AppState::RedesignTooltip): a hint standing while the user types stays.
    // A KEY THAT RAISES A MODAL TAKES A ROSTER HINT DOWN WITH IT, at every
    // return of this body: Windows shows a tooltip only while its window is
    // active, and a modal takes the activation. The test is the roster walk's
    // own no-wait rule (tooltip_dwell_suppressed — a prompt, any editor, the
    // progress modal, the render player, the picker: its five terms, the
    // count re-derived 2026-10-25), asked after the key has run,
    // so a hint never stands over the modal the key just opened, advertising
    // a chord that modal's gate now swallows, for the frames before the
    // tick's walk would find no owner (the walk then also stops a wait still
    // counting, so it never comes due under the modal). A DIALOG owner needs
    // no term: a surface raised over its row replaces the stash, and the
    // painter refuses a box whose stamp differs (paint_shift_tooltip).
    const AtScopeExit modal_tooltip_end{[this] {
        const AppState::RedesignTooltip& t = app.redesign_tooltip;
        if (t.visible &&
            t.owner.surface == AppState::RedesignTooltip::Surface::Roster &&
            tooltip_dwell_suppressed())
            hide_shift_tooltip();
    }};
    // (THE HELD ARROW BUTTONS' REPEAT BURST is NOT disarmed here, and its
    // key-press disarm deliberately does not live in this body: since the
    // burst's own fires dispatch THROUGH on_key — the tick's opener goes out
    // as a physical event by the opener rule — a disarm at this top would kill
    // the burst at its own first fire. The physical-key-arrival edge is the
    // platform delivery in main.cpp's set_on_key hook, upstream of every
    // synthetic caller; the derivation is at that site and the burst's edge
    // inventory at AppState::ChromePress. The platform's OWN key repeat keeps
    // its layer-1 disarms inside maybe_fire_repeat, untouched.)
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // The modal prompt (painted on the bottom row from 2026-08-13, in the
    // modal family's box and controls row since 2026-10-14) owns input
    // while active. Only the prompt's own ACCESS KEYS, Esc and its focus ring
    // do anything; everything else is swallowed so marker edits / playback /
    // viewport keys cannot sneak in while the prompt is up. The dialog's
    // BUTTONS answer too — the pointer's press claim (input_pointer.cpp)
    // reaches the same activate_response through the shared dispatch, which
    // the access keys ride as well.
    // THE ACCESS KEYS ARE WINDOWS STRICT (architect 2026-10-10 ~16:05; the
    // rule is PromptState's, app_state.h): a prompt's focus is always on a
    // button, so a button's underlined letter answers BARE or with ALT, CASE-
    // INSENSITIVELY on the case-folded key (Caps Lock answers), Shift and
    // Ctrl spelling nothing — the menus' own match. That SUPERSEDES the
    // bare-only match of 2026-07-28 (which kept Alt+Y off the paste
    // confirmation) and the codepoint-exact lowercase match of 2026-07-30
    // (which kept a Caps Lock Y from answering Yes): Windows answers both.
    // Ctrl+S still picks no Save. ESC IS THE LAST ANSWER, Cancel, matched
    // bare on the GuiKey; the Delete key answers nothing any more (it was
    // Discard's until Windows strict, which gave Discard its D).
    if (app.prompt.active) {
        // THE PAINTED GATE (2026-08-13): a prompt the user has not SEEN
        // answers nothing. One dispatch batch arrives whole before the loop
        // paints, so a key queued behind the raise itself (Ctrl+Q tearing an
        // editor down on the close road and raising the unsaved-work prompt,
        // Delete right behind
        // it) was answering a question that had never been on screen. EVERY
        // key is consumed while the bit is false — the access letters and Esc
        // with them: a consumed no-answer is
        // the only safe reading of input aimed at an unseen surface, and
        // nothing is lost that a second press after the paint does not
        // recover. The bit's writer is paint_modal_dialog's prompt branch and
        // the whole rule lives at PromptState (app_state.h).
        if (!app.prompt.painted) return;
        // NO HATCH (architect 2026-10-25, "block all gestures except the one
        // escape, or anything that closes the modal"): Ctrl+Q and Ctrl+S
        // are consumed below with every chord the prompt does not bind, the
        // paste confirmation's Ctrl+Q (which abandoned the paste and ran the
        // close road until then) included. A WINDOW MANAGER'S CLOSE over a
        // standing prompt does what it always did: request_close refuses
        // re-entry, so it is ignored while the question stands.
        // THE FOCUS RING (2026-08-13), the prompt's half: bare Tab and bare
        // Left/Right walk the answer buttons, and bare Enter or bare Space
        // presses the focused one down and commits it at the key's release. It
        // is ranked here — under the painted gate, over the response match —
        // because it is navigation and activation over an unanswered question
        // and must obey the same "only a surface the user has seen" rule the
        // answers do.
        // A PROMPT IS RAISED WITH PASSIVE FOCUS ON A BUTTON since later that
        // day, SUPERSEDING this gate's own "a prompt opens with no button
        // focused, so a stray Enter cannot answer": Enter DOES answer now, and
        // what makes it safe is that the focused button is the ESCAPE SENTINEL
        // — on every prompt but the FOUR CONFIRMATIONS (the load's two
        // raisers, Revert's and the phase reset paste's), which ask for
        // their OK — or Yes — deliberately, each
        // being the second step of an explicit
        // act (PromptState's PromptInitialFocus) — plus the
        // painted gate directly above (PromptState carries the supersession in
        // full). The route is shared with the editor dialogs' —
        // one ring, one owner (route_modal_dialog_focus_key,
        // input_key_dispatch.cpp); a prompt has no field, so the editors'
        // completion-first Tab arm has no counterpart here and this call is the
        // whole of the prompt's ring. The access letters below are the ring's
        // equals (any button answers its letter wherever the ring stands),
        // so answering by letter or Esc is the same with or without the ring.
        if (route_modal_dialog_focus_key(key, mods)) {
            return;
        }
        const int access =
            modal_dialog_access_button(key, mods, /*field_focused=*/false);
        if (access >= 0) {
            dispatch_modal_dialog_button(access);
            return;
        }
        if (!ctrl && !shift && !alt && key == GuiKeys::Escape) {
            prompt.activate_response('\x1b');
        }
        return;
    }

    // THE PROGRESS MODAL IS KEYBOARD-MODAL (architect 2026-10-14, AppState::
    // BatchProgress), ranked directly under the prompt (a WM close's
    // unsaved-work question can stand over a running batch) and above every
    // other gate — no list owner or editor stands beside it. ITS
    // VOCABULARY IS ITS CANCEL: bare Esc cancels at the press (the batch's
    // cancel road, cancel_batch_progress — the render cancel's own act, (d)
    // in the bare-Esc enumeration below), and the focus ring's Tab / Left /
    // Right / Enter / Space walk and press its one button, Enter or Space
    // cancelling at the key's release (route_modal_dialog_focus_key). EVERY
    // OTHER KEY IS CONSUMED, silently — a bound chord under any modal says
    // nothing (2026-10-25) — and authoring stays locked for the batch's
    // whole run. NO HATCH (architect 2026-10-25): Ctrl+Q fell through to the
    // close road until then; the window manager's close is the quit road,
    // and over a running batch it asks the unsaved-work question (or
    // proceeds) exactly as before, request_close taking nothing down that
    // the progress modal owns.
    if (app.batch_progress.active) {
        if (route_modal_dialog_focus_key(key, mods)) return;
        if (!ctrl && !shift && !alt && key == GuiKeys::Escape)
            cancel_batch_progress();
        return;
    }

    // (THE OPEN DROPDOWN WAS KEYBOARD-MODAL HERE, directly under the prompt —
    // bare Esc closed it, Ctrl+Q closed it and fell through, a row's access
    // key ran the row and every other chord was swallowed — until the
    // Settings popup, its last menu, retired 2026-10-21.)

    // THE RENDER PLAYER IS KEYBOARD-MODAL (2026-08-28), ranked under the
    // prompt (its load confirmation is a prompt and outranks it). Above
    // everything else:
    // while the mode stands ITS ROUTER IS THE WHOLE VOCABULARY (the contract
    // at route_render_player_key), and only Ctrl+S and Ctrl+Q fall through to
    // the ordinary dispatch — Ctrl+S to the save, which touches no transport,
    // and Ctrl+Q with the player already closed, so the quit road runs on the
    // ordinary state; Ctrl+O and Ctrl+Alt+O the catch-all consumes in
    // silence. No editor and no
    // gesture of any OTHER surface can stand under it (its opener refuses
    // under every editor; the veil arms none), so the gates below never
    // contend with it — its own two arms are this block's business, below.
    //
    // NOTHING POPS MID-GESTURE, and because this block outranks the drag-modal
    // gate below it asks that gate's question itself
    // (any_pointer_gesture_active — the player's two arms are members of it,
    // and the reason is stated there). While the overlay's row press or the
    // scrub's marker drag is armed the keyboard belongs to the gesture exactly
    // as it does under a marker or trim drag: every key is swallowed — Esc
    // included, so the mode cannot close under a held press, and Enter and
    // Space cannot open a row or start a sound under one either. THE ONE HATCH
    // is the drag-modal gate's own, modifier-exact Ctrl+Q, which falls through
    // to the ordinary close road; that road closes the player first
    // (GuiPrompt::request_close), and the close drops both arms with the
    // overlay and the scrub state — the force-end every other gesture takes.
    if (app.render_player.active) {
        if (any_pointer_gesture_active(app)) {
            // THE DRAG GATE'S QUESTION ASKED EARLIER SAYS THE DRAG GATE'S
            // SENTENCE (architect 2026-08-30): this arm outranks that gate,
            // so the press never reaches it — and one fact refused in two
            // places must not be two sentences.
            if (!(ctrl && !shift && !alt && key == GuiKeys::Q)) {
                notifications.notify(AppState::NotificationClass::Normal,
                                     kKeysDuringDrag);
                return;
            }
        } else if (route_render_player_key(key, mods)) {
            return;
        }
    }

    // THE SCORE VIEWER IS KEYBOARD-MODAL THE SAME WAY (architect 2026-10-26,
    // score_viewer.h): its router is the whole vocabulary while it stands,
    // Ctrl+S and Ctrl+Q its two fall-throughs, and its page scrub's arm takes
    // the keyboard for the gesture as the player's scrub does, the drag
    // gate's one hatch with it.
    if (app.score_viewer.active) {
        if (any_pointer_gesture_active(app)) {
            if (!(ctrl && !shift && !alt && key == GuiKeys::Q)) {
                notifications.notify(AppState::NotificationClass::Normal,
                                     kKeysDuringDrag);
                return;
            }
        } else if (route_score_viewer_key(key, mods)) {
            return;
        }
    }

    // THE PICKER IS KEYBOARD-MODAL THE SAME WAY (architect 2026-08-28): the
    // field-less picker over the folder overlay (the project picker, the
    // settings page and the preset picker it swaps in), at the player's rank (the two never stand together) and with
    // the player's shape — ITS ROUTER IS THE WHOLE VOCABULARY
    // (route_picker_key), Ctrl+Q the one fall-through (with the picker
    // already closed by the close road's exit), Ctrl+S consumed by the router
    // as the save, under the project picker Ctrl+O as the close and
    // Ctrl+Alt+O as Revert (2026-10-25, whose question stands over the
    // picker — the prompt gate above then owns every key, so its Esc is the
    // question's Cancel and a second Esc the picker's close), under the
    // settings page and the preset picker it swaps in bare `;` as the close
    // (2026-10-28), and
    // Ctrl+Shift+S (the publish chord, 2026-10-10)
    // as a consumed nothing — the pull's question must not open over a
    // picker. The gesture clause is the player's
    // own: the overlay's row press arm is a member of
    // any_pointer_gesture_active under every owner, and while it stands
    // every key is swallowed but the drag-modal gate's Ctrl+Q hatch.
    if (app.picker.active) {
        if (any_pointer_gesture_active(app)) {
            // The player's arm above, verbatim — the same gate's question
            // asked earlier, the same sentence.
            if (!(ctrl && !shift && !alt && key == GuiKeys::Q)) {
                notifications.notify(AppState::NotificationClass::Normal,
                                     kKeysDuringDrag);
                return;
            }
        } else if (route_picker_key(key, mods)) {
            return;
        }
    }

    // THE NO-PIECE GATE: a set before its load has installed the audio
    // (no_audio_to_dispatch_on, app_state.h). THE OPEN'S PRESENTATION HOLD
    // SPANS THIS WHOLE STATE (platform.h's head: armed before the set's run()
    // and released only after its load), and its key hook drops every
    // platform press but plain Ctrl+Q before this body runs (run_project's
    // hook block, main.cpp); the chrome's synthesized presses (a key's lift,
    // its hold-repeat) need a press that same hold dropped, and the car's
    // roads are dropped at their own hook. So PLAIN Ctrl+Q IS THE ONE KEY
    // THAT REACHES THIS GATE — read in the set's run() ahead of its first
    // tick, which runs the load (both backends deliver a pass's input before
    // its tick) — and it is admitted: dirty is false with nothing loaded, so
    // the close exits at once.
    //
    // THE GATE'S REST IS A SILENT RETURN AND RAISES NO CARD: no other press
    // has a producer here, so there is nothing to answer (the type rule).
    //
    // Loading itself is a BLOCKING, UNINTERRUPTIBLE phase: the run loop is
    // suspended for the whole synchronous decode/pyramid/install, so a Ctrl+Q
    // or WM close pressed during loading is not observed here at all — it is
    // read when run() resumes after the load completes, with the audio
    // installed, and the deferred quit is then honored by Ctrl+Q's own arm
    // below (the app quits on completion); an urgent abort is pkill / the
    // compositor's force-close.
    if (no_audio_to_dispatch_on(app, audio.total_frames())) {
        if (ctrl && !shift && !alt && key == GuiKeys::Q)
            prompt.request_close(GuiCloseTarget::Exit);
        return;
    }

    // Editor text-selection drag modal gate. This sits ABOVE the
    // text-editor handlers because the flag / settings editor is active
    // during its own selection drag, and those handlers would otherwise
    // tear the edit down and dispatch a command on any command key; this
    // gate must intercept first so the drag owns the keyboard like every
    // other drag. Swallow every key but the one escape hatch, BARE Esc (its
    // only hatch since 2026-10-25; Ctrl+Q was its second until then, and is
    // consumed as every modal consumes it). For the hatch, finalize the
    // selection first (reading the still-live editor geometry) and then fall
    // through with no return, so the editor handler below runs Esc (cancel
    // the edit) exactly as it would with no drag in flight. A text drag is
    // not a navigation gesture, so it gets no bare-`s` carve-out. The hatch
    // is modifier-exact: NO MODIFIED ESCAPE BINDS ANYWHERE in this product (the
    // one that did, Ctrl+Esc, retired on 2026-09-01 when bare Esc took the
    // notification stack whole), so a modified Escape has nothing to be an
    // escape hatch FOR and is swallowed here like any other key rather than
    // ending the drag.
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
            (!ctrl && !shift && !alt && key == GuiKeys::Escape);
        if (!escape_hatch) {
            // THE SWALLOW IS SILENT (2026-10-25): a text drag exists only
            // under an editor, which is a modal, and a bound chord under any
            // modal says nothing — the card this gate raised (the pointer
            // drags' "Keys are ignored during a drag", 2026-08-30) retired
            // with the editor gate's own. NO HATCH BUT Esc: Ctrl+Q, the
            // drag's second hatch until then, is consumed as every modal
            // consumes it.
            return;
        }
        finalize_editor_text_drag();
        // THE HATCH IS A GESTURE END: this drag holds the cursor to the TEXT
        // I-beam through pointer_cursor_kind's live-drag arm (architect
        // 2026-09-03), so pressing Esc over the
        // waveform must come back showing the Pan
        // — which it does at this iteration's tail, once the editor
        // below has closed and everything else this call does has settled.
        // (The TOP FLAG EDITOR is in it too, by the SAME route: that arm sits
        // above the dialog editors' blanket and reads the drag's own record,
        // so it is one owner for both editor families.)
        // fall through: the editor handler below runs Esc (cancel the
        // edit) exactly as with no drag in flight.
    }

    // KEYBOARD-MODAL EDITOR GATE. While ANY editor is open — the
    // single-State dialog one (settings; the commit title stood beside it
    // 2026-08-07 to 2026-10-10), the bpm bracket (a
    // dialog too), and the top-strip flag editor
    // (architect 2026-07-28, which brought the last of them in) — only the keys
    // the editor itself consumes (typing, its motion keys, Esc and Return)
    // and a dialog's ring get through (modal_editor_key_blocked); everything
    // else — playback, navigation, zoom, mode toggles, tab switches,
    // undo/redo, marker / trim chords, the Ctrl+Alt render chords, and since
    // 2026-10-25 Ctrl+S and Ctrl+Q (NO HATCH) — drops HERE, SILENTLY, so no
    // authoring or view change can happen while an edit is up. Admitted keys
    // route through the editor blocks below.
    // This gate is what makes an unbound chord STRUCTURALLY inert rather than
    // merely unlisted: nothing downstream can see it, so nothing downstream can
    // tear an edit down on its way to a command that does not exist.
    // THE POINTER IS VEILED BY THE SAME PREDICATE since 2026-10-25 (an act
    // that takes the keyboard is a modal — keyboard_modal_editor_active's
    // declaration names every reader); opening a flag editor still does not
    // stop playback — that one rides no predicate at all, each modal surface
    // spelling its own stop at its open site.
    // Sits after the text-drag gate so an in-flight editor selection drag keeps
    // owning the keyboard exactly as before.
    if (keyboard_modal_editor_active() &&
        modal_editor_key_blocked(key, mods)) {
        // A BOUND CHORD UNDER ANY MODAL IS SILENT (architect 2026-10-25:
        // "chords are used with the laptop, and I'm not using the laptop for
        // authoring … silence"): the editors answer a chord they do not own as
        // the prompt and the progress modal do — consumed, nothing said. (The
        // card "Close the editor first: <chord> is ignored while it is open"
        // stood here, for a bound chord alone, 2026-08-30 to 2026-10-25.)
        return;
    }

    // The top-flag editor owns the keyboard while active. Only its own editor
    // keys (and a dialog's ring, for the BPM bracket) pass the gate above, so
    // this block cannot see a command, and it consumes every key it is handed
    // (route_modal_editor_key answers true throughout since 2026-10-25, when
    // its Ctrl+Q fall-through to the close road went with the hatch). Routes
    // BEFORE the render/batch Esc cancel so bare Esc closes the edit
    // first.
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

    // (THE EDITORS' OWN ctrl-exact Ctrl+C / Ctrl+X / Ctrl+V are handled in the
    // editor blocks above. Past them Ctrl+C binds nothing since 2026-10-14:
    // the resolved value's copy is Ctrl+Shift+J, beside Ctrl+J's jump in the
    // ordinary dispatch below — is_copy_value_key, gui_input.h.)

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
    //     commits: the proposed marker position, the live trim bounds, the trim as
    //     swept (the tempo drag's already-written cents left this list when
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
    // VALUE (2026-09-10 — the flag's vertical drag, armed by bare `[`'s lamp
    // since 2026-10-11 (value_drag_posture),
    // which writes the live store per motion and so must swallow every chord
    // exactly as its horizontal sibling does) /
    // trim / region drags, the one nav drag and its pending
    // click (scroll_drag — one state for the pending, the pan and the ctrl
    // zoom phase since 2026-08-14; the dual-axis STRIP drag was a member here
    // until its deletion, 2026-08-15),
    // (THE STANDING REGION'S OWN EDITOR was an entry of its own from
    // 2026-08-15 until 2026-08-18: `region_edit_drag`, the move and the two
    // bound drags. Its state is DELETED — those drags ARE the trim drags named
    // above now that the region is the trim — so the gate is unchanged and one
    // name fewer.)
    // And the
    // MARKER FLAG'S PENDING PRESS, THE ONE DEFERRED PENDING CLICK ACT
    // (pending_click — the trim bar's two bound sets, the one click still
    // lift-deferred since 2026-08-17) plus the pending trim drag (a press
    // held before it resolves) —
    // are mutually exclusive with it. The marker pending's click already ran
    // at its press (2026-08-17), but it stays on this list: the drag it may
    // become reads the store and the selection the press left, and the seed
    // its release writes must pair with the press the user made — so no chord
    // may move any of it in between. For the bound set the reason is sharper:
    // its act re-asks its gates LIVE at the lift — the strictly-inside partner
    // test above all — so a chord that moved a trim bound between press and
    // release would have the lift decide against a state the user never
    // pressed on. (The GEOMETRY of its aim is frozen with every other aimed
    // press's since 2026-09-24 — displayed_basis_frozen, app_state.h; the
    // gates stay the lift's.)
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
    if (keyboard_owned_by_pointer_gesture()) {
        // The ONE hatch left, modifier-exact (a modified Ctrl+Q has no binding
        // anywhere): end the gestures as their release would, then run the close
        // flow. Bare Esc takes the swallow below with every other key.
        if (ctrl && !shift && !alt && key == GuiKeys::Q) {
            finalize_active_drags();
            prompt.request_close(GuiCloseTarget::Exit);
            // THE CURSOR RE-RESOLVES AFTER THE FORCE-END, and nothing here has
            // to arrange it: the finalizer cleared the gesture state
            // pointer_cursor_kind reads and request_close may have raised the
            // prompt it reads next, and the loop's per-iteration owner answers
            // after both — after this whole call, in fact, so a later edit below
            // cannot get the ordering wrong.
            return;
        }
        // THE SWALLOW SAYS WHY (architect 2026-08-30), in the editor text
        // drag's own sentence: a gesture owns the keyboard, and that is the
        // one fact worth a sentence — the chord is not what was wrong, so it
        // is not what the card names. Bare Esc lands here with every other
        // BOUND key and is carded like them: it is NOT the bare-Esc no-op the
        // top-level default is, it is a press this gate deliberately refuses
        // (POINTER GESTURES HAVE NO CANCEL, the rule stated above), and Esc is
        // a bound chord, so the test below admits it.
        // THE TEST IS THE MAIN DISPATCH'S OWN (chord_is_bound, gui_input.h):
        // an unbound press does nothing with no gesture in flight and says
        // nothing with one. IN THE STANDING MODE (2026-09-01, U4), the bit
        // handed over below: a mode-only chord outside its mode — bare `,`
        // during a grab-pan with the `h` view down — is unbound in this state
        // like any other, and the drag is not what stopped it.
        if (chord_is_bound(key, mods, app.history_mode.active))
            notifications.notify(AppState::NotificationClass::Normal,
                                 kKeysDuringDrag);
        return;
    }

    // THE `h` HISTORY MODE — its own keys, then its allowlist. Placed HERE,
    // directly under the drag-modal gate and directly over the read-only one,
    // and the position carries two facts rather than being a convenience.
    //
    // (1) EVERY ENTRY REFUSAL THE MODE NEEDS HAS ALREADY RUN. A prompt,
    // loading-or-absent audio, the editor text drag, any of the three
    // keyboard-modal editors and any live pointer gesture all swallow the press
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
    // its own reason, not a dependency of this one.) THE ITERATION HALF OF THE
    // LOCK IS THE ONE THING THAT DOES REACH THE MODE (2026-09-10), and this
    // position is why it is spelled at the mode's own ENTRY arm rather than in
    // either allowlist: the view's acts push history, so opening it under a lit
    // lamp refuses with the lock's card, while the CLOSE edge stays ungated.
    if (handle_history_mode_key(key, mods)) return;
    if (app.history_mode.active &&
        history_mode_key_blocked(key, mods, app)) {
        // THE SWALLOWED CHORD SAYS SO (architect 2026-08-30), naming the
        // chord AND the mode: the view is a read-only lens over another
        // state, most of its refusals have a lit-looking key behind them, and
        // the roster's grey already says the same thing to the pointer
        // (history_mode_disables_button reads this very predicate for the
        // face). THE CARD IS THE CALL SITE'S, never the predicate's — that
        // face walk asks the same function once per button per frame, and a
        // card raised inside it would paint a stack every frame.
        //
        // THE GENERIC SENTENCE IS FOR A CHORD THAT MEANS NOTHING IN THE MODE,
        // AND FOR NOTHING ELSE (architect 2026-09-01: "look for instances
        // where it lies by saying it's not allowed when, in fact, it's only
        // contextually not allowed"). CTRL+S WAS THE INSTANCE HE HIT — the
        // view's own Save-and-Commit chord, admitted by the allowlist only
        // while the act could run, so a committed-and-current session was told
        // "Ctrl+S is not available in the history view" about the one chord
        // the view exists to run. The membership is the CHORD'S ALONE for it
        // now: Ctrl+S is admitted as vocabulary and the ACT answers (the
        // publishing card for a worker mid-act, silence for an empty head
        // delta — the one-dimensional refusal whose greyed Save-and-Commit
        // face is the cue), with the face reading the act's own predicate
        // rather than this gate's admission.
        //
        // TWO ADMISSIONS ARE STILL CONDITIONAL ON STATE, and for those the
        // chord is NOT what was wrong (architect 2026-08-30): bare `'` and
        // bare `v` are the view's own vocabulary, admitted while the walk
        // carries a member and while a revert has a subject on a writable tab.
        // Answering either with the generic sentence would say the key means
        // nothing in here, which is false — so each names the state that
        // refused it, exactly as the greyed Load in place and Revert buttons
        // say the same thing to the pointer, and NEITHER LIES with the
        // sentence it does say. They keep the terms because the one decision
        // that refuses the chord is also the one that greys the button, which
        // is what the derived partition buys; the record of the alternative is
        // Ctrl+S's, one paragraph up. (The membership is the allowlist's,
        // re-greped: those two are the whole of it — the walk's `,` / `.` are
        // the mode's own arm's, claimed above this gate and never reaching
        // it.)
        const bool bare = !ctrl && !shift && !alt;
        if (key == GuiKeys::Apostrophe && bare) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 "There is nothing to load");
            return;
        }
        if (key == GuiKeys::V && bare) {
            // The admission composes the SUBJECT with the LOCK
            // (history_revert_actionable), so the card forks on the same two
            // terms rather than blaming the subject for a locked tab.
            notifications.notify(
                AppState::NotificationClass::Normal,
                history_mode_revert_subject_standing(app.history_mode)
                    ? kTabReadOnlyCard
                    : "Select a change to revert");
            return;
        }
        // AND THE GENERIC SENTENCE IS FOR A BOUND CHORD ALONE (architect
        // 2026-08-30, the unbound-keys ruling; chord_is_bound, gui_input.h).
        // "is not available in the history view" says the key means something
        // outside it, which is exactly what the two arms above were carved out
        // for; for a chord bound nowhere it would say the same false thing, so
        // that press takes the silence every unbound press takes. The view's
        // OWN vocabulary never reaches this line at all — bare `g`, `u` and the
        // `,` / `.` pair are claimed by handle_history_mode_key, Ctrl+S is
        // admitted outright, and bare `'` and bare `v` take the fork above — so
        // admitting them to the inventory costs this gate nothing. THE MODE BIT
        // IS TRUE HERE BY CONSTRUCTION (2026-09-01, U4 — this arm runs only
        // while the view stands), so the inventory this gate asks is the WIDER
        // one, the seven shapes included; that is the same answer it gave when
        // the predicate was state-blind, and the sentence above is why it costs
        // nothing.
        if (chord_is_bound(key, mods, app.history_mode.active))
            notifications.notify(AppState::NotificationClass::Normal,
                                 spell_chord(key, mods) +
                                     " is not available in the history view");
        return;
    }

    // THE AUTHORING LOCK'S KEYBOARD GATE, and it is ONE GATE FOR TWO LOCKS
    // since 2026-09-10: a permitted-keys allowlist that filters out every
    // AUTHORING chord — the marker stores and the engine settings are what the
    // read-only lock protects (architect 2026-08-07), and the whole UNDO
    // DOMAIN is what the ITERATION lock protects (architect 2026-09-10) —
    // while admitting navigation, playback, view-switching, the close-prompt
    // routing, the bare-o toggle-off escape chord, and THE BAND, THE SAVE AND
    // THE RENDER. Runs when the active tab's ViewState carries read_only =
    // true OR grid iterations stands (authoring_locked, app_state.h).
    //
    // THE LIST BELOW IS THE READ-ONLY ONE. The iteration lock's is that list
    // MINUS THE COLUMN SWITCH — the four absolute view selectors, bare
    // 1 / 2 / 3 / 4, which run the S/T and column chokepoint BODIES and so
    // carry the column with them (the individual bare `t`/`p` keys were deleted
    // whole with their view lamps 2026-09-15, so the selectors are the axis's
    // only road); the mode is lit for the column you are in —
    // MINUS THE Ctrl+Shift+Tab PAIRED MARCH, whose own tab switch clears the
    // selection, so its second step could never walk the bound cells honestly
    // (architect 2026-09-10; Ctrl+Tab stays admitted in every state, the two
    // tabs sharing both stores) — MINUS BARE `o` ON A WRITABLE TAB, the
    // read-only toggle's lock direction (architect 2026-09-10; narrowed
    // 2026-10-13: the padlock cannot lock the tab the lamp stands lit on, and
    // its unlock is admitted) —
    // AND PLUS FOUR ADMISSIONS: bare `i` (the off edge must always be
    // reachable — the BPM opener, bare `m` then and Ctrl+B now, was a fifth
    // until 2026-09-10, when the architect ruled its own swap-the-mode exit
    // out and it fell to the base list, which refuses it),
    // Up/Down and Return WITH A BOUND AXIS ADDRESSED (the bound cells are the
    // mode's own authoring surface), Ctrl+Z and Ctrl+Shift+Z (admitted not to
    // act but so their own arm can card the sentence that names undo), and the
    // CLIPBOARD COPY bare P, bare-exact (it reads a selection into a
    // session clipboard and pushes nothing, so the undo domain this lock
    // protects has no interest in it; the two PASTES, Ctrl+P and
    // Ctrl+Shift+P, stay refused). That
    // list has ONE owner, iteration_lock_key_blocked (input_key_dispatch.cpp),
    // written as this one's complement plus its deltas rather than as a second
    // copy. BOTH LISTS ARE ASKED, the two locks composing on a locked tab
    // reached by a switch under the lamp (the fork below states it). BARE `h`
    // IS REFUSED UNDER
    // THAT LOCK TOO and is not on either list:
    // the history vocabulary is claimed a dispatch above this gate, so the
    // view's entry arm carries the refusal.
    //   - Bare o                 → toggle read-only off (escape chord).
    //                              THE ITERATION LIST SUBTRACTS ITS LOCK
    //                              DIRECTION (2026-09-10; 2026-10-13): the
    //                              padlock cannot lock the tab the lamp
    //                              stands lit on
    //   - Space (no mods)        → playback toggle
    //   - Left/Right (bare)      → playhead step, and ONLY with an
    //                              EMPTY selection (the waveform lane): with one
    //                              the same press also carries the marker — the
    //                              marker-lane position nudge — which
    //                              is authoring and drops here
    //                              (playhead_in_marker_lane)
    //   - Home/End (bare or      → playhead to trim region bounds (bare)
    //     Shift)                   or to the piece's ends (Shift, since
    //                              2026-10-24; Ctrl until then)
    //   - PageUp/PageDown        → viewport step scroll by the plain wheel's
    //     (no mods)                stepped-pan step. Pure navigation, same
    //                              family as the playhead-step and Home/End
    //                              entries.
    //   - 0 (no mods)            → full zoom-out stamping the view, then the
    //                              restore of that view in the audio view it
    //                              was stamped in; a silent no-op with
    //                              nothing to return to (run_overview_command)
    //   - Shift+0                → frame the trim (2026-10-24), the trim
    //                              bar's double-click (run_span_framing_command):
    //                              the camera alone
    //   - c (no mods)            → focused-marker jump (when present) +
    //                              working zoom; with no focused marker,
    //                              working zoom centered on the playhead;
    //                              arms the hold posture
    //   - f (no mods)            → toggle the follow lamp (toggle_follow)
    //   - 1/2/3/4 (no mods)      → the absolute selectors, one axis each
    //                              (Source / Target / Warp / Phase Reset), the
    //                              ONE road onto the two view axes: 1 / 2 the
    //                              S/T chokepoint switch_active_audio_view_to,
    //                              3 / 4 the column entry
    //                              GuiActiveViews::select_active_markers_view
    //   - Tab/Shift+Tab/IsoLeftTab → cycle marker focus, the landing's camera
    //                              the landing owner's (Viewport::land_subject)
    //   - Ctrl+Tab               → switch A/B tab (the other escape)
    //   - Ctrl+Shift+Tab         → march paired tabs in lockstep, a round
    //                              trip ending on the starting tab
    //   - Esc                    → the render/batch cancel (and the editor /
    //                              prompt closes), then the whole notification
    //                              stack if none of those is standing — the
    //                              selection/region ladder it used to serve here
    //                              is deleted, so a bare Esc with no render
    //                              running and no card standing is a plain
    //                              no-op (the seven-place contract at its
    //                              enumeration's home, input_handler.cpp's
    //                              on_key)
    //   - Ctrl+Q                 → close-prompt routing
    //   - Ctrl+O                 → the Open project picker (2026-08-28). It
    //                              authors nothing: it lists the projects, and
    //                              choosing one reopens the program around
    //                              another project rather than writing into
    //                              this one. Bare `o` above is the other act
    //                              on the letter, not this one's other half
    //   - Ctrl+S                 → the save (2026-08-07). It writes the state
    //                              the tab already holds and authors nothing —
    //                              and the close prompt's Save answer, which sits
    //                              above this gate, always did save from a
    //                              locked tab through the same owner
    //   - Ctrl+Alt+R,            → the renders (2026-08-07): the single render
    //     Ctrl+Alt+Shift+R          or the iteration sweep, and the
    //                              miscellaneous cell. A render READS the
    //                              authored state. (Save and commit rode this
    //                              pair into the `h` view until 2026-08-08; it
    //                              is the Ctrl+S entry's now, on that entry's
    //                              own reasoning — it publishes what the tab
    //                              already holds)
    //   - Ctrl+Shift+0 (no alt)  → the trim maximizer, Reset Trim (2026-08-07,
    //                              on Shift+[ until 2026-09-22, Shift+0 until
    //                              2026-10-24). Trim is BAND, not content
    //   - l (no mods)            → the render player (2026-08-28): it plays
    //                              a rendered wav and authors nothing; its
    //                              one authoring act, Load in place, refuses
    //                              inside the player on a locked tab
    //   - ' (no mods), OUTSIDE   → the same render player (2026-09-01), the
    //     the `h` view              same act on a second key: the state selects
    //                              which act the chord is, so the gate asks the
    //                              mode bit the dispatch below asks. INSIDE the
    //                              view the chord is the LOAD and stays blocked
    //   - ; (no mods)            → the settings page (2026-10-21, the page
    //                              2026-10-28): the lock governs the keys, not
    //                              the surface — a row opens the settings
    //                              editor (or the preset picker), whose
    //                              engine-key commits refuse on a locked tab,
    //                              the device keys and the band commit
    // Authoring-mutation chords are BLOCKED at this gate, not admitted for a
    // deeper refusal: the marker / tempo / phase-reset drop / nudge /
    // status-toggle chords, Delete, `i`, `'` in the `h` view, the
    // phase-reset propagate's copy and pastes (bare P, Ctrl+P, Ctrl+Shift+P),
    // and undo/redo (Ctrl+Z / Ctrl+Shift+Z) all drop here. (`;`, THE SETTINGS
    // PAGE, IS ADMITTED since 2026-10-21 — the lock governs the keys, not
    // the surface, the 2026-09-04 rule: a row opens the settings editor (or
    // the preset picker), and the editor's engine-key commits refuse on a
    // locked tab.) This gate is the ONLY read-only
    // guard on the keyboard path — and since 2026-08-07 the only one on the
    // POINTER path too has gone, the trim band's gate having been deleted with
    // the reclassification. The surviving deeper checks each cover a
    // surface it cannot reach — do_undo / do_redo's target-tab peek (the
    // ACTIVE tab is writable but the top history entry targets the other,
    // read-only tab; this gate tests only the active tab), and the per-gesture
    // pointer AUTHORING guards (a pointer gesture never passes through on_key —
    // the ONE pointer surface that does is the redesigned buttons, which
    // dispatch their chord through on_key precisely so this gate applies to
    // them unchanged: Undo and Redo drop here in a locked tab exactly as their
    // keys do, while Save and Render now pass exactly as theirs do). Full
    // rationale at read_only_key_blocked in input_key_dispatch.cpp.
    // THE LOCK IS ONE LOCK WITH TWO REASONS (architect 2026-09-10, the
    // iteration lock; the composed predicate is authoring_locked, app_state.h,
    // which the faces read — here each half spells its own bit, so the pair is
    // not asked twice). The READ-ONLY half is the per-tab bit and its
    // allowlist is read_only_key_blocked below; the ITERATION half is grid
    // iterations standing, whose allowlist is that one plus the bound cells
    // (iteration_lock_key_blocked, input_key_dispatch.cpp, where both deltas
    // are stated).
    //
    // THE TWO REASONS COMPOSE AND READ-ONLY RANKS FIRST (architect
    // 2026-10-13: the lamp lights on a writable ACTIVE tab whatever the other
    // tab's bit, so a tab switch under it can stand on a locked tab; the
    // account is at authoring_locked, app_state.h). The verdict drops a chord
    // either list drops — the wider list carries the read-only refusals, the
    // iteration list delta (a), which read-only admits — and the card names
    // the read-only lock whenever its list is the one that said no: on a
    // locked tab that is the reason the user can fix there. THE OFF EDGE IS
    // NEVER EATEN: bare `i` under a lit lamp is the wider list's own
    // state-dependent admission (is_iteration_off_edge, read_only_key_blocked)
    // and the iteration list admits it too, so the lamp goes out from either
    // tab. The iteration side's other admissions (the bound axis, the undo
    // pair, the clipboard copy, the tie, bare `[`) stay refused on a locked
    // tab, the wider list eating them as it does with the lamp dark.
    if (authoring_lock_refuses_chord(key, mods)) {
        // THE LOCK SAYS SO, AND THE CARD NAMES THE CHORD (architect
        // 2026-08-30): "<chord> is not available on a read-only tab", said
        // for a chord this product BINDS and for no other. The sentence is
        // kept over a bare "This tab is read-only" because it names what was
        // pressed, which is what the user is looking for after a press that
        // did nothing. THE ENGLISH IS read_only_chord_card's (notifications.h)
        // and not this site's: the English has one owner beside its knowing
        // siblings (kTabReadOnlyCard, notifications.h).
        //
        // THE SECOND CLASS IT WAS BUILT FOR HAS GONE SILENT — TWICE OVER, in
        // two rulings a day apart — and with it the cost the wording was chosen
        // to cover. read_only_key_blocked is the COMPLEMENT OF AN ALLOWLIST, so
        // it answers true both for a bound authoring chord the lock is holding
        // back AND for a chord that binds nothing at all (bare `5`, an unbound
        // modifier decoration); when every unbound press elsewhere was answered
        // with "<chord> is not bound", saying nothing at all here would have
        // been the one place that class went unanswered, and a bare "This tab
        // is read-only" would have stated a WRONG CAUSE for it. THE
        // UNBOUND-KEYS RULING (architect 2026-08-30, the same day: bound keys
        // either show an effect or a card, so an unbound key is identified by
        // its silence) dissolved both horns for the chords bound NOWHERE, and
        // THE MODE TERM (architect 2026-09-01, U4) dissolved them for the
        // chords bound nowhere IN THIS STATE: the `h` view's own seven shapes —
        // bare `g`, bare `u`, bare `v`, bare `,`, bare `.` and the two shifted
        // walk jumps — are the view's vocabulary and no part of a locked tab's,
        // so outside the view they now take the same silence a writable tab
        // gives them. His instance was bare `v`, the revert act, which this
        // gate answered with "V is not available on a read-only tab" while a
        // writable tab in the same state ignored it without a word. The two
        // OWNER LINES that make them the view's alone are
        // handle_history_mode_key's mode return (input_key_dispatch.cpp) and
        // the revert arm's own mode test (input_handler.cpp, below this gate);
        // the inventory that reads them is chord_is_bound (gui_input.h), and
        // this gate's MEMBERSHIP — what it blocks — learns nothing and stays
        // the chord's alone.
        //
        // SO WHAT REMAINS IS A SENTENCE ABOUT A CHORD THAT REALLY WOULD HAVE
        // ACTED, OR CARDED ITS OWN REASON, ON A WRITABLE TAB IN THIS SAME
        // STATE. Where the act would ALSO have refused for its own reason —
        // bare `s` in T+W, `i` in the P column, a verb with no selection — the
        // lock still speaks, because the lock is the OUTERMOST STATE (a mode
        // entered on purpose, exactly as the `h` view is) and both refusals are
        // true; the ruling's target is the chord that MEANS NOTHING in the
        // state, not the chord that means something and is refused twice. Three
        // standing precedents say the same thing: the faces compose
        // `!read_only && <the act's own predicate>` with the lock first, the
        // `h` view cards "S is not available in the history view" in T+W under
        // this very reasoning, and ONE CARD PER PRESS raises at the outermost
        // site that has the reason.
        //
        // The rejected alternative stays rejected on its own reasoning: a
        // membership list of the authoring chords to fork the sentence on would
        // be a second spelling of the bindings, and its drift would lie.
        // (chord_is_bound is a spelling of the BINDINGS' EXISTENCE, not of the
        // lock's allowlist — it decides whether a card is owed, never which
        // cause is named, so its drift costs a missed card and never a wrong
        // one.)
        //
        // IT IS THE KEY'S CARD ALONE — a GREYED button never reaches here,
        // its press being consumed at arm_redesign_press's disabled line
        // (nothing arms, so no chord dispatches at the lift), which is the
        // truthful-buttons ruling's own division: the grey IS the message on
        // the roster, and the card is the message on the keyboard. A LIT
        // button whose chord the lock drops does reach it, and is answered
        // exactly as its key is. The bare literal kTabReadOnlyCard stays the
        // word of the three sites that KNOW THEIR ACT and so need no chord in
        // the sentence — re-greped 2026-09-04, when the settings editor's lock
        // moved off the opener: the editor's engine-key commit, the render
        // player's Load in place, and the `h` view's bare `v` fork above.
        //
        // AND THE CARD FORKS ON THE REASON AT THIS ONE SITE (architect
        // 2026-09-10), on the REASON THAT ACTUALLY STOOD — the read-only
        // list first where both stand (2026-10-13, the fork's own note
        // above). The
        // read-only sentence names the chord for the reason
        // above — this gate drops unbound chords with bound ones, so it has to
        // say which it ate. THE ITERATION SENTENCE NAMES THE WAY OUT INSTEAD
        // (kIterationLockCard, app_state.h — "Turn off grid iterations
        // first"): the mode is one the user turned on deliberately and can see
        // (the lamp is lit, the bound cells are painted), so one clause
        // answers it, and naming the chord would add nothing the press did not
        // already say. chord_is_bound gates BOTH halves, for its own reason:
        // an unbound press is identified by its silence in every state.
        // (Ctrl+Z and Ctrl+Shift+Z are NOT answered here under the iteration
        // lock — the allowlist admits them so their own arm can say
        // kIterationLockUndoCard / kIterationLockRedoCard, which name the act
        // a user pressing undo is asking about.)
        // THE VERDICT AND THE CARD ARE authoring_lock_refuses_chord's (defined
        // below on_key), the one body this gate and the car's undo / redo road
        // (run_undo_redo_without_key) both ask.
        return;
    }

    // Keyboard authoring is HOME-VIEW gated in the WARP column, not
    // view-blind: warp markers author in source view, while the phase-reset
    // column authors in both audio views (2026-08-30; again since S+P's
    // return, architect 2026-10-12, after three weeks in T+P alone) — via the one
    // predicate active_column_authoring_allowed consulted at the individual
    // handlers below beside the read-only check above.
    // WHAT IT GATES IS THE POSITIONAL FAMILY (architect 2026-08-24): the
    // binding's own rationale is that a PLACEMENT edit in target view mutates
    // the map the view is displayed in, which says nothing about a status, an
    // existence or a value edit — so the predicate's consumers on this
    // dispatch are the marker drop and the Ctrl+B bpm open (which rewrites
    // tempo through a derivation over a SPAN); the phase-reset Ctrl+D and
    // Delete arms carried it until the P side opened.
    // THE RULED EXCEPTIONS ARE ENUMERATED AT ONE SITE,
    // active_column_authoring_allowed (app_state.h); the members that
    // dispatch from here are the bare UP/DOWN TEMPO CENT STEP in W+target
    // (singleton and group, the singleton freezing a pass as it does in
    // source view since 2026-09-13 — the whole tempo surface since
    // 2026-07-29, when the pointer tempo drag and the bare Left/Right
    // tempo-image step were deleted, marker_drag.h, leaving bare Left/Right in
    // W+target a consumed refusal at the split below), the phase-reset
    // propagate paste (a W-column act that lands in the P column of the audio
    // view it was pressed in, land_paste_in_phase_reset_view, and switches no
    // audio view), the iteration-bracket wipe, and — since
    // 2026-08-24 — the WARP STATUS/VALUE FAMILY: Ctrl+D,
    // Ctrl+N, Delete and the flag editor's Return open, each admitted in
    // W+target with the cent step's re-land contract in its own tail.

    // (Bare `t` and `p`, the S <-> T and W <-> P toggles, are gone: the two
    // axes are reachable through the four selectors below alone — the keys
    // and the icon row's two view pairs. handle_active_audio_view_toggle is
    // the body `t` called; it stays.)

    // BARE 1 / 2 / 3 / 4 ARE THE SELECTORS, ONE AXIS EACH (architect
    // 2026-10-14, the icon row's two radio pairs): `1` is Source and `2`
    // Target (the audio view, the markers view untouched), `3` is Warp and
    // `4` Phase Reset (the markers view, the audio view untouched); the view
    // is the product of the two, both columns pairing with both audio views.
    // Each names a VALUE, and THE KEY FOR THE VALUE ALREADY STANDING SELECTS
    // ITS AXIS'S OTHER VALUE (architect 2026-10-23 night, "pushing a button
    // in, I would expect always to cause it to pop out, because that's the
    // basic mechanism of the button; building a suction on the other one is
    // the right way to go": bare 3 under Warp selects Phase Reset; the
    // consumed no-op of 2026-10-14 reversed), so every press moves its axis.
    // The icon row's pairs synthesize the four keys in the same order, the
    // lit member's press popping through this same arm — the buttons and the
    // keys one rule by construction.
    //
    // COMPOSED, NEVER RE-SPELLED: each axis is applied by its own chokepoint —
    // switch_active_audio_view_to for S/T and
    // GuiActiveViews::select_active_markers_view for the column — the press
    // handing each the value it resolves (the named one, or its axis's other
    // when the named one stands), so the S/T chokepoint's same-view no-op and
    // the column's differs-test are guards a press never meets since
    // 2026-10-23, kept as each body's own spelling. Every invariant those two
    // own therefore arrives by construction: the target-view entry validation and
    // its error notice, the domain translation of viewport / playhead / zoom,
    // the flag editor teardown and the iter wipe, the selection clear, the
    // coincidence auto-select (the column entry's; the audio switch runs none,
    // auto_select_marker_at_playhead), and the synchronous plate rebuild.
    // There is no third view-switch route to keep in step with the other two.
    // A refused target entry (the tripwire class, unreachable from
    // program-written input, silent on screen) leaves the view as it was.
    if ((key == GuiKeys::Digit1 || key == GuiKeys::Digit2 ||
         key == GuiKeys::Digit3 || key == GuiKeys::Digit4) &&
        !ctrl && !shift && !alt) {
        if (key == GuiKeys::Digit1 || key == GuiKeys::Digit2) {
            const char named = (key == GuiKeys::Digit1) ? 'S' : 'T';
            const char other = (named == 'S') ? 'T' : 'S';
            switch_active_audio_view_to(
                app.active_audio_view == named ? other : named);
        } else {
            const char named = (key == GuiKeys::Digit3) ? 'W' : 'P';
            const char other = (named == 'W') ? 'P' : 'W';
            const char want_markers =
                app.active_markers_view == named ? other : named;
            if (app.active_markers_view != want_markers)
                active_views.select_active_markers_view(want_markers);
        }
        // INSIDE THE `h` VIEW THE SWITCH FLUSHES ITS OWED REBUILD (2026-10-13):
        // the rebuild's history arm replaces app.history_mode.flags with the
        // entering column's list, and a later key of the same drain (Tab's
        // cycle, Center, Revert) reads that list, so it must be the entering
        // one before this press returns — one rebuild per press, as while
        // every kick rendered inline. Outside the mode nothing reads the
        // rebuild's output at event time, so the pre-paint hook pays it.
        if (app.history_mode.active) paint_handler.flush_owed_waveform_rebuild();
        return;
    }

    // Bare `o` toggles the active tab's read-only flag. Always admitted
    // by the read-only allowlist above (the locked-out user must be
    // able to unlock) and REFUSED BY THE ITERATION ONE since 2026-09-10
    // (architect): the padlock cannot lock a tab under a lit lamp, because
    // that state left the mode's own cells refusing with the TAB's sentence
    // and bare `i` — off the read-only list — unable to put the lamp out. The
    // refusal is the gate's, on kIterationLockCard, with the icon row's Read-Only
    // button greyed beside it (iteration_lock_greys, app_state.h); nothing
    // here forks, the chord never arriving under a lit lamp.
    // Pure view-state mutation: not undoable, not dirty;
    // silently persisted on the next Ctrl+S — WHICH THE LOCKED TAB CAN NOW RUN
    // ITSELF (2026-08-07: the save authors nothing, so it is on the allowlist),
    // so a tab locked here reaches disk without an unlock and without a trip to
    // the other tab. THE TAB'S PADLOCK IS
    // THE WHOLE VISIBLE CUE since row 7 deleted the bottom strip's
    // "(read-only)" token as a restatement of it.
    // The write and the damage that goes with it live in set_tab_read_only
    // (its contract is at the declaration, input_handler.h), which this road
    // shares with the settings editor's `tab_a_read_only=` /
    // `tab_b_read_only=` commit — the bit's second and only other writer, and
    // the reason the setter takes a tab rather than assuming the active one.
    if (key == GuiKeys::O && !ctrl && !shift && !alt) {
        set_tab_read_only(app.active_tab_view,
                          !active_view_state(app).read_only);
        return;
    }

    // THE TRIM REGION OVERLAY'S ESC HIDE STOOD HERE AND IS RETIRED (2026-08-21);
    // the overlay itself retired whole on 2026-10-03 (architect, "the trim bar
    // is enough"), so there is nothing for any key to hide. A cancel takes ONE
    // press.

    // Bare Esc cancels an in-flight render / queued batch.
    if (handle_escape_cancels(key, mods)) return;

    // THE WHOLE ESC STORY, stated here because this is where the selection/region
    // ESC LADDER used to be dispatched and the ladder is DELETED — rungs,
    // down-only doctrine and all (architect 2026-07-29). BARE ESC IS BOUND IN
    // SEVEN PLACES AND NOWHERE ELSE (re-derived 2026-10-21, when the dropdowns'
    // (c2) left with the Settings popup; EIGHT from 2026-09-30, when the AV SYNC
    // STATS PANEL's (c5) left with the panel — the drag-modal gate above tests
    // only Ctrl+Q, so Esc is UNBOUND there and falls through with every other
    // key while a gesture is in flight; it is NOT one of the seven), the first
    // six earlier in this function than this point, so reaching here means
    // the press has nothing left to do BUT the stack. THE LETTERING BELOW IS
    // AN ENUMERATION, NOT THE DISPATCH'S ORDER (corrected 2026-09-02): on_key
    // tests the PROMPT gate first of all, ahead of the loading gate and so
    // ahead of the editor hatch and the editors — which is right, a prompt
    // outranking an editor everywhere — while the letters below run
    // editor-first. Read them as a list of the seven places (c2 the retired
    // dropdowns' letter, kept so the others keep theirs), and read on_key
    // itself for which one answers when two could:
    //   (a) THE EDITOR TEXT-DRAG ESC HATCH — a bare-exact Escape ends an in-flight
    //       text-selection drag (above); a SUB-PART of the editor class below,
    //       since it can only fire while one of the editors owns the
    //       keyboard, and the same press then falls through to that editor's own
    //       close/cancel;
    //   (b) THE EDITORS — every kind, through route_modal_editor_key: Esc
    //       closes / cancels the edit (the editor blocks above, bit-for-bit
    //       unchanged);
    //       the commit-title editor (2026-08-07 to 2026-10-10) joined that
    //       route and added no place of its own, which is the point of there
    //       being one route;
    //   (c) THE PROMPTS — Esc activates the rightmost response (the prompt gate at
    //       the top of on_key, unchanged);
    //   (c2) THE DROPDOWNS — Esc closed the open popup (architect 2026-07-31)
    //       until the Settings popup, the last menu, retired 2026-10-21;
    //   (c3) THE RENDER PLAYER (2026-08-28, the sixth) — Esc closes the mode
    //       (route_render_player_key, under the prompt gate and over every
    //       gate below it). It ranks below the prompt because its load
    //       confirmation IS a prompt, and it cannot collide with (a)/(b): the
    //       player never opens under an editor and admits no editor opener;
    //   (c4) THE PICKER (2026-08-28, the seventh — the Open project picker,
    //       one router) — Esc closes it (route_picker_key,
    //       at the player's rank and with its argument: the reopen's
    //       unsaved-tab question IS a prompt, and no picker opens under an
    //       editor or admits an editor opener). The two typed prompts it
    //       replaced were members of (b), so the count grew by one place, and
    //       it did not move again when the `h` view's own picker retired
    //       2026-08-29 — that key raises a PROMPT there now, rung (c);
    //   ((c5), THE AV SYNC STATS PANEL's own router, was the ninth place
    //       from 2026-09-03 and went with the panel on 2026-09-30.)
    //   ((c6), THE COLOR PICKER's own router, was the ninth place again
    //       from 2026-10-07 and went with the picker on 2026-10-14.)
    //   (d) THE RENDER / BATCH CANCEL — handle_escape_cancels, just above,
    //       for a single render; a BATCH's Esc is answered higher, at the
    //       progress modal's own block under the prompt gate (2026-10-14),
    //       and it is this same act — cancel_batch_progress runs
    //       cancel_archival_session, the one body both reach — so the place
    //       moved up and the count did not move;
    //   (e) THE NOTIFICATION STACK, WHOLE (architect 2026-09-01, superseding
    //       the 2026-08-31 arm that took the oldest card alone) — the LAST
    //       rung, at the bare-key tail (handle_plain_bare_keys' Escape arm,
    //       input_key_dispatch.cpp, where the reasoning lives): a press none
    //       of the six above claims clears every card, whatever its class,
    //       and with no card standing the arm is the ruled silence it has
    //       always been. It is the keyboard twin of the shifted or held lift
    //       on a card (the card's X until 2026-10-01) and it is RANKED
    //       rather than privileged — the card's own pointer claim sits above
    //       every veil, this key under all of them.
    // A SIXTH PLACE STOOD BETWEEN (c2) AND (d) AND IS RETIRED: THE REGION HIDE
    // (joined 2026-07-30, retired 2026-08-21; the overlay itself retired
    // 2026-10-03 — the record is at the site it stood at, just above the
    // cancel).
    // THE `h` HISTORY VIEW ADMITTED BARE ESC ON 2026-08-04 AND THE COUNT DID NOT
    // MOVE: its allowlist stopped dropping the key, which lets (d) — and,
    // since 2026-08-31, (e), the mode's own cards being ordinary cards — run
    // inside the view. That rung is what the ADMISSION buys, not the only rung
    // reachable in there (re-derived 2026-08-28): the view also opens the
    // load confirmation on `'` and the pull's question on Ctrl+Shift+S, and
    // any editor its admitted chords can open, so (a),
    // (b) and (c) run in it too — the checkpoint's own failure notice being
    // another prompt this view can raise — but each of those gates sits ABOVE
    // the allowlist in on_key, so the press never reaches the admission at
    // all. It gained no binding of its own, and Esc cannot close it: the
    // view's toggle is handle_history_mode_key's, whose whole vocabulary is
    // enumerated at
    // history_mode_owns_key (input_key_dispatch.cpp) and carries no Esc shape in
    // any modifier combination, so no Esc reaches it.
    // What Esc still does NOT do is the old ladder: NO deselect, NO playhead land,
    // NO drop-to-span, and no collapse of anything but the span itself. A 2+
    // selection and a singleton are
    // left exactly as found. Leaving the MARKER LANE is
    // not an Esc act either: it is any DESELECTING route (Home/End, a
    // waveform click, the trim setters, an undo restore that clears — see
    // playhead_in_marker_lane).
    // A bare Esc that gets past here falls to the bare-key tail, whose Escape case
    // is place (e) — the stack's clear, and an explicit no-op with no card
    // standing (handle_plain_bare_keys) — the one place the press ends.
    // THE SEVEN PLACES ARE BARE ESC'S AND EVERY MODIFIED ESCAPE IS UNBOUND, at
    // every Escape reader. CTRL+ESC IS THE RETIREMENT RECORD: it was bound at
    // the very head of this function on the morning of 2026-09-01 to clear the
    // whole notification stack from above every gate, a DIFFERENT CHORD that
    // added no place to this list, and it retired that evening when place (e)
    // took the whole stack itself — one act wants one chord, and two roads onto
    // it is a second road (the ruling is at that arm and at notifications.h).

    // Ctrl+Q: quit (via unsaved-work dialog when dirty).
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        prompt.request_close(GuiCloseTarget::Exit);
        return;
    }

    // CTRL+O — THE OPEN PROJECT PROMPT (architect 2026-08-28). This is the
    // one spelling the convention allows it: bare `o` is the
    // read-only toggle, and the two are neighbours on a letter rather than two
    // halves of one act. It is a
    // HOTKEY and so acts at the PRESS; ctrl-exact through the shared predicate
    // the read-only allowlist reads, so Ctrl+Shift+O stays the strict rule's
    // consumed no-op. IT SITS BESIDE QUIT because it is the same family — the
    // two acts on the session as a whole — and the placement is otherwise free:
    // every state that must refuse the picker refuses it ABOVE this line (a
    // standing prompt, the player's and the picker's own routers,
    // loading-or-absent audio, the editor text drag, any
    // keyboard-modal editor) or inside the opener's own body, which is the
    // road the band's Open Project button takes and carries the gates for
    // both. Nothing
    // is restated here. THE `h` VIEW IS NOT ONE OF THOSE STATES since
    // 2026-08-29 (architect, "admit both"): its allowlist ADMITS this chord
    // beside Ctrl+Q, so the press reaches this line in the view and the picker
    // opens over it — the same act on the same road, one act on the session as
    // a whole beside the other. Not repeat-eligible: that gate is an allowlist of the
    // stepping keys and this letter is not on it, so a held Ctrl+O opens the
    // picker once.
    if (is_open_project_key(key, mods)) {
        open_project_picker();
        return;
    }

    // CTRL+ALT+O — REVERT (architect 2026-09-13): reopen the CURRENT
    // project from disk, discarding unsaved changes and the undo history. It
    // sits beside Open and Quit for the same reason they sit together — the
    // acts on the session as a whole — and the placement is otherwise free on
    // the same terms as Open's: every state that must refuse it refuses it
    // ABOVE this line (a standing prompt, the render player's router, which
    // consumes it in silence as it consumes Ctrl+O, the
    // loading gate, the editor text drag, any keyboard-modal editor) or inside
    // the act's own body. THE PICKER'S ROUTER ANSWERS IT ABOVE TOO, with this
    // same body (2026-10-25, route_picker_key), so under the picker the act
    // runs without reaching this line. The `h` view and both locks ADMIT it (their
    // allowlists name the shared predicate). Ctrl+alt-exact through that
    // predicate (is_revert_project_key, gui_input.h); not repeat-eligible.
    if (is_revert_project_key(key, mods)) {
        revert_project();
        return;
    }

    // Render-trigger chords: Ctrl+Alt+R (the single render, or the ITERATION
    // SWEEP while iteration mode is on) and Ctrl+Alt+Shift+R (the
    // miscellaneous render, a consumed no-op in iteration mode). (The
    // load-editor opener is bare `'`, handled separately below.)
    if (handle_render_dispatch_keys(key, mods)) return;

    // BARE `v` — THE HISTORY VIEW'S REVERT ACT (architect 2026-08-05 on
    // Ctrl+H, MOVED HERE 2026-09-01): apply the view's SELECTED diff flags
    // backwards into the live store and close the view. Outside the mode the
    // key is UNBOUND, which is what the mode test says — there is no second
    // meaning to select between, unlike Ctrl+S's.
    //
    // IT WAS CTRL+H FROM 2026-08-05 TO 2026-09-01. That chord predated the
    // mode's own buttons and read oddly beside the bare `h` that opens the
    // view — a modified twin of the toggle that is not the toggle. The
    // architect moved it to a bare letter on his consistency principle the day
    // the RENDER PLAYER'S STOP RETIRED (the row is the main window's transport
    // triple now), which left bare `v` carrying no meaning anywhere in the
    // product: no cross-context clash can exist, so the mode's act can have
    // the letter outright. CTRL+H IS UNBOUND AGAIN and takes the silence every
    // unbound chord takes (chord_is_bound, gui_input.h — the deduction rule:
    // bound keys act or card, so silence identifies unbound).
    //
    // IT IS DISPATCHED HERE RATHER THAN CLAIMED BY handle_history_mode_key, and
    // that placement is the whole gate story: this line sits BELOW the read-only
    // gate, so a locked tab drops the press exactly as it drops `'` — the lock
    // means hands off the piece's authored state, and this act writes markers.
    // (It parted company with the checkpoint act on 2026-08-07, when the gate
    // reclassified saving and rendering as authoring-free; that act runs from a
    // locked tab, on Ctrl+S since 2026-08-08.) The mode's allowlist above
    // admits the key (and only while there is a subject to revert), which is
    // what lets it reach here at all; the act's own body owns everything past
    // that. THE BARE SPELLING CHANGES NOTHING ABOUT THAT PLACEMENT:
    // history_mode_owns_key's bare list is `h`/`u`/`g`/Home/End/`c` and names
    // no `v`, so the key falls past the mode's own arm to this line exactly as
    // the ctrl spelling fell past it, and the allowlist below still decides.
    //
    // BARE-EXACT, and one-shot exactly as the chord was: repeat_eligible's
    // global arms name no bare letter (the step gestures, the cycle and the
    // five repeating Ctrl chords are the whole of it), so a held `v` reverts
    // once, as a held Ctrl+H did.
    if (app.history_mode.active && !ctrl && !shift && !alt &&
        key == GuiKeys::V) {
        run_history_revert();
        return;
    }

    // THE VALUE PAIR (architect 2026-08-29) — CTRL+SHIFT+J copies the FOCUSED
    // marker's resolved value to the system clipboard (since 2026-10-14),
    // CTRL+J jumps to the marker that value came from on the OTHER A/B tab
    // (since 2026-09-29). They took the place of the retired resolved readout,
    // and they sit here, in the ordinary dispatch below the read-only gate and
    // below the mode's own claim, because both are read-only-LEGAL (the
    // allowlist admits them, so the gate above passes them through) and both
    // are refused in the `h` view by that view's allowlist, which never names
    // them. Exact through the shared predicates (gui_input.h), so these
    // arms and the allowlist cannot drift; every refusal is the act's own
    // card. The bottom row's Jump to Defining Marker dispatches Ctrl+J through
    // here at its lift, and Ctrl+Shift+J at its shift-click or long press.
    if (is_copy_value_key(key, mods)) {
        copy_focused_marker_value();
        return;
    }
    if (is_jump_to_value_source_key(key, mods)) {
        jump_to_value_source();
        return;
    }
    // Space is the sole playback toggle, and it is modifier-strict — every
    // modified Space but ONE is unbound (is_play_pause_key owns that test; the
    // one is Shift+Space, the A/B audition, the arm just below this one).
    // Return / keypad Enter are NOT playback keys; they open the flag editor,
    // handled further below.
    if (is_play_pause_key(key, mods)) {
        // A GUI TRANSPORT PRESS, counted AHEAD OF THE REFUSAL below so a
        // refused play supersedes a pending car play too (the rule at
        // GuiCarTransport::PendingCarPlay, car_transport.h; the count's
        // writers at AppState::gui_transport_press_count).
        ++app.gui_transport_press_count;
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
        // THE PREDICATE IS GuiTargetRender::preview_ready (2026-08-26), the
        // one owner both this edge and the A/B audition's gate read.
        // A LAUNCH GATE ALONE (2026-10-28, Sol's finding of that day): a
        // STANDING A/B AUDITION is transport-live (the hands-off rule at
        // end_audition_for_press's declaration, which exempts this key so
        // that its own dispatch stops the act) and passes this gate to
        // toggle_playback's stop fork, in every phase — a rest, a live play,
        // or OtherFirst waiting on the other tab's unrendered preview, where
        // nothing plays and the preview is not ready. The readiness question
        // is a launch's alone; without the term the press (or the PLAY key's
        // plain lift, which synthesizes it) would return here silently and
        // the act would go on when the preview landed.
        if (!audition_sequence_standing(app) &&
            app.active_audio_view == 'T' &&
            !playback.is_playing() &&
            !target_render.preview_ready()) {
            // The refusal is silent (architect 2026-09-04): row 8's state
            // cell already carries the preview render's own `Updating...`
            // while it runs, and the Play button greys on this very
            // predicate, so the screen says why. That is the rule stated in
            // notifications.h's silent list as the second member of the
            // one-dimensional class — a bound key's refusal is silent when
            // row 8's process line already names the reason. It carded from
            // 2026-08-30 to that day, in the sentence the waveform scrub's
            // twin refusal shared (kTargetPreviewNotReadyCard, deleted with
            // both raises). Space stays bound: chord_is_bound and spell_chord
            // are untouched, so this silence is this rule's and not an
            // unbinding's.
            return;
        }
        // LEAD-IN AUDITION, START EDGE ONLY (architect 2026-07-28): when the
        // phase-reset lead-in overlay has a SUBJECT, Space launches the scanner
        // kPhaseResetLeadInSamples (kN/2) output samples AHEAD of the resting
        // playhead — past the seed grain of a reset dropped at the playhead
        // (to the schedule's rounding, the accepted residue recorded with the
        // derivation: three terms scaling with the LOCAL SEGMENT SLOPE, not a
        // fixed sample count), on the instant the drop's lead-in was derived to
        // protect (the derivation at drop_phase_reset_lead_in_at_playhead,
        // phaseresetmarkers_ops.cpp) — without moving the cursor, so the stop
        // merely deactivates the scanner and the cursor it never touched is
        // exactly where it was. A non-destructive audition of the reset's
        // settled output.
        // THE PREDICATE IS THE SELECTION-STATE ONE (Selection::phase_overlay_
        // subject), never GuiPaintHandler::phase_reset_overlay_band: the band
        // layers geometry gates (area size, samples-per-pixel, sub-pixel forward
        // width, offscreen refusal) on top, so keying Space on it would let a
        // scroll or a zoom silently change what Space does.
        // The kN/2 is EXACT output-sample arithmetic. The painted band is a ±1px
        // approximation for jitter reasons and must never enter this number.
        // THE DERIVATION IS THE ONE OFFSET OWNER since 2026-08-30
        // (phase_reset_lead_in_launch_offset, selection.cpp — lifted so the
        // PLAY button's face can read the REAL launch position through
        // space_launch_would_play instead of the resting cursor alone).
        const int64_t launch_offset =
            phase_reset_lead_in_launch_offset(app, playback);
        // SPACE ALWAYS PLAYS FROM THE PLAYHEAD (architect 2026-07-30, Q2: "drop
        // the left edge launch - play issues from playhead OR scrub - user can
        // click scrub region to preview"). The region arm that stood here — a
        // left-bound launch through scrub_launch_at whenever a span rested — is
        // DELETED with the SPAN FORM: the region IS THE TRIM (the model is at
        // RegionDragState, app_state.h), not a launch
        // point, and the SCRUB is the gesture for previewing it — the waveform
        // lower half's plain click, its one entry (the act runs at the
        // motionless release since 2026-08-13): click inside
        // the trim window and it auditions from there.
        // Space reads no trim bound to launch from.
        playback_lifecycle.toggle_playback(launch_offset);
        return;
    }

    // SHIFT+SPACE IS THE A/B AUDITION (architect 2026-08-26): switch to the
    // other tab, play kAuditionMs from its playhead twice, switch back, play
    // the same span from this tab's playhead twice. A hotkey, so it acts at
    // the PRESS; shift-exact (is_ab_audition_key — no ctrl, no alt, the strict
    // rule's other decorations of Space staying consumed no-ops). THE CHORD IS
    // A TOGGLE (architect 2026-08-31): a press that finds the act standing
    // STOPS it through the one stop body — the same body bare Space's fork
    // reaches — instead of refusing, which is also what a held key meets
    // (Space is one-shot in repeat_eligible, so no repeat ever arrives).
    // Every refusal left is the act's own AND CARDED THERE since 2026-08-30
    // (GuiAbAudition::start, whose header holds the inventory): a dead or
    // absent device, and either tab with nothing to play from — a preview not
    // ready in target view among them. Nothing is spelled here; this site only
    // routes. The `h` view consumes the chord at history_mode_key_blocked
    // (playback is removed from the view whole and Space in neither form is
    // on its allowlist); loading-or-absent audio consumed it at the head of
    // this function; a read-only tab ADMITS it (the allowlist's
    // is_ab_audition term — it authors nothing). The play button's shift-click
    // and long press arrive here through the same chord
    // (redesign_button_shift_admits).
    if (is_ab_audition_key(key, mods)) {
        // A GUI TRANSPORT PRESS, counted ahead of start()'s refusals (the
        // rule at GuiCarTransport::PendingCarPlay, car_transport.h).
        ++app.gui_transport_press_count;
        ab_audition.start();
        return;
    }

    // Bare Return / KpEnter opens the ADDRESSED CELL'S editor on the focused
    // marker (architect 2026-09-05; the flag editor alone until then) — the
    // click-to-edit replacement (Enter is already the editor's commit key, so
    // the open/commit round-trip is symmetric). While any editor is open the
    // editor blocks above consume Enter first (commit), so this is reached only
    // with no editor active. Repair the focus first, then fork on
    // AppState::addressed_cell: the payload by default — every focus reached
    // by a walk, a jump, a clear or a restore is addressed there, with no
    // exception left since 2026-09-10 (a bracket-only entry's restore brought
    // back the bound cell it changed until then; the bracket left the undo
    // domain and the entry with it) — opens the
    // canonical-line editor, a bound cell its bound editor, each with its
    // seeded content fully selected
    // (open-selected, like every open route — the first keystroke replaces
    // it). Read-only already dropped Return at the allowlist gate above (every
    // one of these is an authoring surface — the old click-to-edit refused
    // read-only too). Modifier-strict: only the plain, unmodified press opens
    // an editor; Return's ctrl forms are the phase-reset propagate pair
    // (architect 2026-10-21, is_phase_reset_copy_key / is_phase_reset_paste_
    // key, gui_input.h), falling past this arm to handle_mode_keys below.
    if ((key == GuiKeys::Return || key == GuiKeys::KpEnter) &&
        !ctrl && !shift && !alt) {
        selection.repair_last_selected();
        // THE WARP COLUMN IS THE PAYLOAD EDITOR'S VIEW GATE SINCE 2026-08-24
        // (architect): the flag editor edits the marker's PAYLOAD — tempo,
        // label_def / label_ref, per-marker scale, the disabled bit — and
        // never its position, so it is a member of the fifth ruled exception
        // to the home-view binding and opens in W+target as well as W+source
        // (the inventory is at active_column_authoring_allowed, app_state.h;
        // the commit's own tail carries the target-view re-warp and playhead
        // re-land). P view still refuses it — phase resets have no per-flag
        // editor — while the BOUND editor is both columns' since 2026-09-09: a bound
        // cell is addressed on either column's eligible flags now. THE REFUSALS ARE ONE
        // PREDICATE since 2026-08-30 (flag_editor_open_actionable,
        // app_state.h, forking on the axis since 2026-09-05). AND
        // THEY SHARE ONE SENTENCE (architect 2026-08-30): the P column and
        // an unfocused selection are the same answer from the user's side —
        // the editor wants a warp marker and does not have one — so the
        // card names the subject it needs rather than forking on which half
        // of the predicate said no.
        //
        // THE TIE'S OWN SENTENCE, RANKED AHEAD OF THE SHARED ONE (architect
        // 2026-09-19): a bound cell addressed on a TIE FOLLOWER refuses
        // through the predicate below — but the predicate's sentence names the
        // subject the editor WANTS, which here it already has. So the tie
        // answers first, in its own words, and the shared sentence keeps
        // meaning what it always meant.
        if (addressed_bound_cell_is_tie_follower(app)) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kBoundCellTiedCard);
            return;
        }
        if (!flag_editor_open_actionable(app)) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 "Select a warp marker to edit its line");
            return;
        }
        const int focus = app.last_selected_marker;
        switch (app.addressed_cell) {
        case MarkerCell::Payload:
            // THE PAYLOAD AXIS OPENS THE WARP column's line editor
            // (flag_editor_open_actionable refuses P, which authors no payload
            // line at all).
            flag_editor.enter_top_flag_edit(focus);
            return;
        case MarkerCell::Lower:
        case MarkerCell::Upper:
            // A bound cell addressed on a marker whose kind carries no
            // bracket cards the bound step's own kind sentence — a fact about
            // the marker's kind, behind a live face, the Up/Down pair's
            // shape. It is the pair's BELT: a bound cell is addressed only
            // where its cell paints, and while the lamp stands the lock
            // refuses every act that could take the carrier away, Ctrl+D
            // included — so the predicate has no answer to give today. The
            // predicate forks on the live column, so this arm opens the
            // addressed cell's editor on either.
            if (const char* refusal = iter_bound_step_kind_refusal(app)) {
                notifications.notify(AppState::NotificationClass::Normal,
                                     refusal);
                return;
            }
            flag_editor.enter_iter_bound_edit(app.active_markers_view, focus,
                                              app.addressed_cell);
            return;
        }
        return;
    }

    // THE ZOOM STEP ON `=` AND `-` (architect 2026-10-27, Zoom In and Zoom
    // Out returned): `=` steps the horizontal ZOOM in and `-` out, ONE LEVEL
    // bare and TWO shifted (is_zoom_step_key / zoom_step_levels, gui_input.h),
    // ABOUT THE VIEWPORT'S CENTRE (Viewport::zoom_step, whose head owns what
    // the step does to the hold, follow and Zoom to Fit's recall). Ctrl, alt
    // and the keypad's KP_Add / KP_Subtract bind nothing, one spelling per
    // act. Each is a SILENT consumed no-op at its wall — `=` at the floor,
    // `-` at the fit, the benign one-dimensional refusal — and its key greys
    // there (zoom_step_actionable, the act's own leading return). A pure
    // viewport move: it stops nothing and writes no selection. One-shot.
    if (is_zoom_step_key(key, mods)) {
        viewport.zoom_step(zoom_step_levels(key, mods));
        return;
    }

    // Bare 0 is A TOGGLE (run_overview_command — architect 2026-09-23): full
    // zoom out stamping the view it leaves, then the restore of that zoom
    // level, playhead and viewport start, no centring, in the audio view the
    // stamp was taken in; with nothing to return to (a ceiling it did not
    // produce, or a stamp from the other audio view) it is a silent no-op
    // and its button greys. Neither press arms or keeps a
    // camera posture: both clear HOLD and CHASE through the one chokepoint
    // clear (clamp_viewport_start). The trim-bar double-click
    // deliberately
    // DIVERGES from this (run_span_framing_command — it zooms to the
    // trim / whole-song span); C remains the DIRECT working-zoom-and-center
    // gesture, while
    // the Tab family changes no zoom (2026-08-05), so `0` is
    // the one command that reaches the whole song. DIGITS 1 TO 4
    // are the ABSOLUTE VIEW SELECTORS, one axis each (architect 2026-10-14;
    // their block is up beside the axis handlers they compose); 5..9 are
    // unbound. CTRL+SHIFT+0 is RESET TRIM (architect 2026-10-24; Shift+0 from
    // 2026-09-22; its arm is further down this body, where the trim bracket
    // family stood).
    if (!ctrl && !alt && !shift && key == GuiKeys::Digit0) {
        run_overview_command();
        return;
    }
    // SHIFT+0 FRAMES THE TRIM (architect 2026-10-24, Zoom to Fit's shifted
    // press and long press): the trim bar's double-click EXACTLY — the one
    // command both run (run_span_framing_command: a proper trim sub-window
    // with the edge margin each side, else the whole song), so the key, the
    // button and the gesture are one act. A camera act: no trim bound, the
    // playhead and the selection untouched, admitted on a locked tab, under
    // the iteration lock and in the `h` view as the double-click is
    // (is_trim_frame_key, gui_input.h, the three readers).
    if (is_trim_frame_key(key, mods)) {
        run_span_framing_command();
        return;
    }

    // Ctrl+Z undo / Ctrl+Shift+Z redo — the WHOLE family, SHIFT the one
    // meaningful bit (which is why this arm is one of strict modifier
    // validation's deliberately-untightened families,
    // GuiInputHandler::on_key). Placed
    // before the GuiKeys::S save handling so modifier dispatch reads
    // left-to-right in the source. AN EMPTY STACK SAYS SO SINCE 2026-08-30.
    // ALT IS UNBOUND HERE and stays that way: the target
    // compositor (labwc) grabs Ctrl+Alt+Z / Ctrl+Alt+Shift+Z, so the GUI never
    // receives them — the alt pair that briefly meant stay-put undo/redo was
    // rolled back for that collision (closed_questions.md), and an alt-carrying
    // shape is again a plain no-op under strict modifier validation.
    if (ctrl && !alt && key == GuiKeys::Z) {
        run_undo_redo_command(/*redo=*/shift, mods.synthesized_repeat);
        return;
    }

    // The phase-reset clipboard family on the letter P (bare P the copy,
    // Ctrl+P the paste, Ctrl+Shift+P the state paste; architect 2026-10-24),
    // and the I / K / L letter keys (`i` iteration mode,
    // `k` add-to-selection, and `l`, the render player's opener).
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
        //     special-cased — stepped over to the marker behind it, as the pass
        //     walk steps over it (architect 2026-09-13) — so a preceding
        //     label_def takes the ordinary numeric arm. Both halves keep their local tempo, but a definition's SPAN
        //     IS ITS MEANING — the parser caches its target duration from where
        //     the section ends (warp_frame_map_build.cpp) — so the split reprices
        //     every reference to that label. A property of definitions, not a
        //     defect.
        // In P view it drops the lead-in reset: a reset placed N/2 before the
        // playhead so the playhead's instant lies past the seed grain and its
        // crossover — full level, the new phase settled, placed on the map
        // (the derivation at drop_phase_reset_lead_in_at_playhead,
        // phaseresetmarkers_ops.cpp) — which composes with Space's lead-in
        // audition — drop then Space cancels the two N/2 offsets and auditions
        // from exactly where the cursor was, to the drop's own whole-frame snap
        // (exact under a speed-up, a few output samples under a slow-down; the
        // one-term arithmetic is at phase_reset_lead_in_launch_offset,
        // selection.cpp).
        // THE LETTER CARRIES FOUR CHORDS: bare `s` is the drop above, Ctrl+S
        // saves, Shift+S (2026-08-28) drops a phase reset from the warp
        // column's either view — the same lead-in drop with the trip to T+P
        // in front of it (in the P column it refuses whole since 2026-08-30
        // as already crossed; the act's head) — and CTRL+SHIFT+S PUBLISHES
        // (2026-10-10, below). Every
        // other modifier combination on `s` is unbound and FALLS OUT OF THIS
        // BLOCK (2026-08-30) to the strict-modifier tail, which says so on a
        // card: this block claims the four spellings it binds and nothing
        // else, so an unbound one is answered by the tail that answers every
        // other unbound chord rather than dying here unremarked.
        //
        // CTRL+SHIFT+S IS THE PUBLISH CHORD, IN BOTH VIEWS (architect
        // 2026-10-10 ~18:35: "ctrl shift s should commit to github using the
        // default title, no modal. this is to help commit quickly from main
        // page. in history view the hotkey for save then becomes ctrl s and
        // save and commit becomes ctrl shift s. however the icons in history
        // remain the same. also ctrl shift s becomes pull if appropriate";
        // the commit-title editor retired the same night): the pull, the
        // check or save-and-commit under the default title by the GitHub
        // status and the device's role (run_publish_chord owns the fork). The
        // `h` view's Save button reaches it by spelling this chord at its lift
        // (finish_chrome_press_release), its face unchanged. CTRL+S IS THE
        // PLAIN SAVE IN BOTH VIEWS since that ruling (it was the checkpoint
        // act in the view from 2026-08-08, and the plain save had no key
        // there). Both chords are admitted by the read-only gate — a save and
        // a checkpoint of the state the tab already holds author nothing —
        // and by the `h` view's allowlist; inside a standing PICKER or player
        // the router answers Ctrl+S as the plain save and
        // Ctrl+Shift+S as a consumed nothing, and under an editor the
        // keyboard-modal gate swallows Ctrl+Shift+S on its card.
        if (is_publish_key(key, mods)) {
            run_publish_chord();
            return;
        }
        if (ctrl && !shift && !alt) {
            // A CLEAN SESSION'S CTRL+S IS SILENT (architect 2026-10-05): the
            // greyed Save is the answer (GuiSaveOps::save_from_key).
            save_ops.save_from_key();
            return;
        }
        // SHIFT+S DROPS A PHASE RESET FROM THE WARP COLUMN'S EITHER VIEW,
        // STAYING IN THAT AUDIO VIEW (architect 2026-10-12: S+W lands in S+P,
        // T+W in T+P; born 2026-08-28 as a trip to T+P, and since 2026-08-30
        // the P column refuses it whole as already crossed — the act's
        // leading refusal). It sits ABOVE the home-view gate below because it
        // does not need the gate's permission — it crosses to the P column,
        // which authors in both audio views, and drops there, which is why it
        // is not a sixth ruled exception to the binding (the note is at
        // active_column_authoring_allowed, app_state.h). Shift-exact through
        // the shared predicate, so this arm and the read-only allowlist cannot
        // drift; the act's own body carries every other refusal.
        if (is_phase_reset_drop_key(key, mods)) {
            drop_phase_reset_crossing();
            return;
        }
        // THE BARE DROP, and the home-view gate lives INSIDE its arm since
        // 2026-08-30 rather than above it: the gate is the ACT'S refusal, so
        // it must not swallow a spelling that never meant to drop — an
        // unbound Alt+S now falls past this block to the
        // strict-modifier tail and is answered as the unbound chord it is.
        // One press, one card, the right one.
        if (!ctrl && !shift && !alt) {
            // T+W SAYS SO (architect 2026-08-30, the strictness ruling; the
            // P column dropped its half of this gate the same day — bare `s`
            // in the phase column drops in both audio views, S+P and T+P
            // alike, the lead-in taken in each (architect 2026-10-12) — so the
            // only refusal left here is the warp column's and the card needs
            // no fork). THE DROP BUTTON NEVER GREYS ON THIS REFUSAL
            // (the twin rule, recorded at its arm in
            // redesign_button_enabled): in T+W its shift twin above — the
            // long press, glass's only road — still crosses and drops, so
            // the button stays lit and its PLAIN lift reaches this card,
            // synthesizing this very chord.
            if (!active_column_authoring_allowed(app)) {
                notifications.notify(
                    AppState::NotificationClass::Normal,
                    "Markers are placed in source view");
                return;
            }
            // Past the gate the column is W (in source view) or P, whose own
            // in-column drop this is: the CROSSING chord Shift+S refuses
            // in-column and leaves the drop to this key.
            // The drops' reason channel: the off-edge refusal
            // (kMarkerDropOffEdgeCard — End at the whole-song zoom, then `s`).
            card_op_refusal(notifications,
                            app.active_markers_view == 'P'
                                ? phase_resets.drop_phase_reset_lead_in_at_playhead()
                                : warpops.drop_copy_previous_at_playhead());
            return;
        }
    }
    // Ctrl+N: toggle pass (inherit) status on the focused warp marker,
    // symmetric with Ctrl+D below — pass, like disabled, is a status
    // toggled on an existing marker, never dropped directly. No
    // phase-reset equivalent (a reset has no tempo source to inherit
    // from), so this refuses in P view — THE PREDICATE IS THE OP'S OWN
    // (inherit_toggle_actionable, app_state.h, which the Toggle inherit
    // button's face reads too), asked HERE AGAIN since 2026-08-30 so the
    // refusal can be NAMED on a card: the fact lives in one owner, and the
    // sentence lives where a press can be answered. Plain `n` and Shift+N are
    // unbound.
    if (key == GuiKeys::N && ctrl && !alt && !shift) {
        // NO HOME-VIEW GATE SINCE 2026-08-24: a pass/owner conversion is a
        // VALUE edit rather than a placement, so it joins the fifth ruled
        // exception (the inventory is at active_column_authoring_allowed,
        // app_state.h) and dispatches in W+target too. The op's own tail
        // carries the target-view re-warp and the playhead re-land.
        //
        // THE REFUSAL SAYS WHICH ONE IT IS (architect 2026-08-30), forked on
        // the predicate's own first term: the P column has no tempo source to
        // inherit at all — a fact about the column — while a missing
        // selection or focus is a subject the user can supply. ONE CARD PER
        // PRESS: the op's identical leading return is the belt below it and
        // stays silent, and the button greys on this same predicate, so no
        // lift reaches this line.
        if (!inherit_toggle_actionable(app)) {
            const char* card = "Select a marker to convert";
            switch (app.active_markers_view) {
                case 'P': card = "Phase resets carry no tempo to inherit"; break;
                case 'W': break;
            }
            notifications.notify(AppState::NotificationClass::Normal, card);
            return;
        }
        // THE SELECTION IS SPENT (architect 2026-09-12): a press past the
        // refusal above is a marker verb acting on what Add to selection built,
        // so the building pass ends here — at the act's entry, ahead of the
        // op's own collapse and of every changed-path test inside it. The class
        // and its whole inventory are at selection_consumed (app_state.h).
        selection_consumed(app);
        warpops.toggle_inherits();
        return;
    }
    // CTRL+SHIFT+N: TIE the selected markers into ONE AXIS of the grid
    // iteration sweep, or untie them again (architect 2026-09-19) — every
    // cell of the grid then applies the same delta to each tied member, so
    // three markers with the first two tied make a 3 x 2 grid rather than a
    // 3 x 3 x 2 one. Ctrl-and-shift exact, ONE-SHOT: a tie repeats onto
    // itself, so the chord is in no repeat_eligible list.
    //
    // IT IS THE INHERIT TOGGLE'S OWN LETTER AND ITS OWN BUTTON: the tie has
    // no button of its own — it rides IconMarkerInherit's shift-click and
    // chrome_shift_hold_ms() long press (redesign_button_shift_admits,
    // app_state.h), which is where the two acts' relation is argued.
    //
    // BOTH COLUMNS, and the LIT one: the body takes the active column, which
    // the verdict's lamp test has already narrowed to W or P. THE ITERATION
    // LOCK ADMITS THIS CHORD (iteration_lock_key_blocked's delta (b),
    // input_key_dispatch.cpp) — a tie is bracket state, and the bracket's own
    // acts are what the lock leaves open — while READ-ONLY refuses it with no
    // entry of its own, the base allowlist carrying no `N` at all.
    if (key == GuiKeys::N && ctrl && shift && !alt) {
        // ONE OWNER FOR THE REFUSAL, THE CARD AND THE DIRECTION
        // (iter_tie_toggle_verdict, app_state.h), asked inside the body so
        // the face and the act cannot read it differently.
        this->run_iter_tie_toggle();
        return;
    }
    // CTRL+F AND CTRL+SHIFT+F: flatten EVERY warp marker's TEMPO DEVIATIONS
    // (architect 2026-09-19) — the plain chord CLEARS the terms, the shifted
    // twin COLLAPSES them to their one sum. Ctrl-exact and ctrl+shift-exact,
    // and ONE-SHOT: a flatten repeats onto itself, so neither form is in
    // repeat_eligible. Bare `f` binds nothing since the follow lamp's
    // deletion (2026-09-23).
    //
    // UNIVERSAL, AND NO SELECTION IS READ (architect 2026-09-19, after using
    // the feature: "Universal always. Otherwise I just have to shift-click
    // everything. That's how the old script worked, and I'm comfortable with
    // it."). Both kinds act on every owning warp marker in the piece: nothing
    // needs selecting first, nothing is consulted, and a selection cannot
    // protect a marker from either — which is also why this arm spends no
    // selection (the class and the exclusion are at selection_consumed,
    // app_state.h).
    //
    // BOTH LOCKS RANK ABOVE THIS ARM and neither is spelled here: `F` sits on
    // no read_only_key_blocked and no iteration_lock_key_blocked allowlist
    // entry — both gates are allowlists — so authoring_lock_refuses_chord at
    // the head of on_key eats the chord and cards its own sentence, which is
    // the outermost refusal a locked press can get. The Ctrl+D and Ctrl+N
    // arms above take the same order.
    //
    // ONE CARD PER PRESS, AND THE REFUSAL SAYS WHICH ONE IT IS, forked on the
    // predicate's own first term exactly as the Ctrl+N arm above forks: in
    // the W column the sentence is a statement about the PIECE and the kind
    // picks which of the two it is, while the P column gets a fact about the
    // column — a phase reset carries no tempo at all, so the W sentences would be false there whenever warp
    // markers do carry terms. The op's identical leading return is the belt
    // below it and stays silent; the Flatten button greys on the CLEAR kind,
    // so a plain lift never reaches this line while a shifted one still can
    // (the twin rule).
    if (key == GuiKeys::F && ctrl && !alt) {
        const TempoFlattenKind kind =
            shift ? TempoFlattenKind::Collapse : TempoFlattenKind::Clear;
        if (!tempo_flatten_actionable(app, kind)) {
            const char* card =
                shift ? "No marker carries more than one tempo deviation"
                      : "No marker carries tempo deviations";
            switch (app.active_markers_view) {
                case 'P': card = "Phase resets carry no tempo to flatten"; break;
                case 'W': break;
            }
            notifications.notify(AppState::NotificationClass::Normal, card);
            return;
        }
        warpops.flatten_tempo_deviations(kind);
        return;
    }
    // Ctrl+D: toggle disabled (warp + phase reset). Plain `d` and Shift+D are unbound.
    if (key == GuiKeys::D && ctrl && !alt && !shift) {
        // THE TWO COLUMNS ANSWER DIFFERENTLY SINCE 2026-08-24. The WARP arm
        // takes NO home-view gate: a disable toggle is a STATUS edit, not a
        // placement, so it is a member of the fifth ruled exception to the
        // home-view binding (the inventory is at
        // active_column_authoring_allowed, app_state.h) and its op's tail
        // carries the target-view re-warp and playhead re-land. The
        // PHASE-RESET arm carried the home-view gate until the P column
        // opened to both audio views (architect 2026-08-30, and again with
        // S+P's return, 2026-10-12), so BOTH ARMS'
        // ONE LEADING REFUSAL is the empty selection
        // (marker_selection_verb_actionable, app_state.h), which the Disable
        // button's face reads too, and the one card names the missing
        // subject — "Phase resets are edited in target view" retired with
        // the gate. ONE CARD PER PRESS — both ops' leading
        // marker_selection_standing returns are the belts below and stay
        // silent — and the Disable button greys on this same predicate, so
        // its lift never reaches here.
        if (!marker_selection_verb_actionable(app)) {
            notifications.notify(
                AppState::NotificationClass::Normal,
                "Select a marker to enable or disable");
            return;
        }
        // THE SELECTION IS SPENT, ON BOTH COLUMNS (architect 2026-09-12): the
        // line stands past the carded refusal and AHEAD of the column fork, so
        // a disable is a disable whichever store it writes — the co-equal axes
        // rule — and neither op spells the lamp (selection_consumed,
        // app_state.h).
        selection_consumed(app);
        // ONE ARM PER COLUMN.
        if (app.active_markers_view == 'P') {
            phase_resets.toggle_phase_reset_disabled();
            return;
        }
        warpops.toggle_disabled();
        return;
    }
    // SHIFT+HOME / SHIFT+END: the WHOLE-PIECE jump (architect 2026-09-26; on
    // SHIFT since 2026-10-24, the Go to Start / Go to End buttons' return —
    // their long press is the glass's Shift — and Ctrl+Home / Ctrl+End, its
    // spelling until then, unbound). Frame 0 and the ACTIVE DOMAIN's last
    // frame, whatever the trim window is, where the bare pair lands on the
    // trim bounds — the way to reach audio the window has cut off without
    // opening the window first. Same body, same unconditional acts
    // (run_playhead_end_jump, input_key_dispatch.cpp), through the movement
    // owner as always.
    //
    // THEY DISPATCH HERE, WITH THE OTHER MODIFIED CHORDS, because the bare
    // pair's arms live in a switch this function reaches only with no modifier
    // held — a chord has no road into handle_plain_bare_keys by construction.
    // Shift is the only modifier they take: Ctrl, Ctrl+Shift and Alt forms
    // bind nothing and are consumed no-ops under strict modifier validation.
    //
    // A TEXT EDITOR NEVER REACHES THIS ARM: Home / End under any modifier are
    // the editors' own caret motion and selection (text_editor::classify_key's
    // MotionEditKey arm admits ctrl and shift on Home / End), and the editor
    // blocks sit at the top of on_key, far above this dispatch — so an open
    // editor consumes the chord before the jump can see it. The render player
    // never reaches it either: its router owns the whole keyboard while it
    // stands, and its own Shift+Home / Shift+End are the item folder's ends
    // (route_render_player_key).
    //
    // The `h` history view claims them too, one arm above this whole dispatch:
    // in there a jump ALREADY means the piece's ends, so the chord means the
    // same thing in every state (history_mode_owns_key).
    if ((key == GuiKeys::Home || key == GuiKeys::End) &&
        shift && !ctrl && !alt) {
        run_playhead_end_jump(key == GuiKeys::End, /*whole_piece=*/true);
        return;
    }
    if (key == GuiKeys::Delete && !ctrl && !alt && !shift) {
        // Delete acts on the active marker store. No read-only check here:
        // Delete drops at the read-only gate above, which is the keyboard
        // path's single guard, and no pointer path reaches the delete routines.
        // Trim is not part of the selection system, so Delete never acts on a
        // bound (Ctrl+Shift+0 is trim's clear; the sweep, the bar's drags and its
        // bound-set clicks are its setters).
        // THE TWO COLUMNS ANSWER DIFFERENTLY SINCE 2026-08-24, the Ctrl+D arm's
        // twin: deleting a marker is an EXISTENCE edit rather than a
        // placement, so the WARP arm is a member of the fifth ruled exception
        // to the home-view binding (the inventory is at
        // active_column_authoring_allowed, app_state.h) and takes no home-view
        // gate, its op's tail carrying the target-view re-warp and the
        // playhead re-land. The PHASE-RESET arm carried the home-view gate
        // until the P column opened to both audio views (architect
        // 2026-08-30, and again with S+P's return, 2026-10-12).
        // BOTH ARMS' ONE LEADING REFUSAL is the empty selection
        // (marker_selection_verb_actionable, app_state.h — the Ctrl+D arm's
        // twin), which the Delete button's face reads too; the card names
        // the missing subject — "Phase resets are deleted in target view"
        // retired with the gate. ONE CARD PER PRESS, the ops' own leading
        // returns staying silent below it, and the greyed Delete button
        // never reaching it.
        if (!marker_selection_verb_actionable(app)) {
            notifications.notify(
                AppState::NotificationClass::Normal,
                "Select a marker to delete");
            return;
        }
        // THE SELECTION IS SPENT, ON BOTH COLUMNS (architect 2026-09-12), the
        // Ctrl+D arm's twin above: past the carded refusal, ahead of the column
        // fork, and spelled at neither op (selection_consumed, app_state.h).
        selection_consumed(app);
        // ONE ARM PER COLUMN, the Ctrl+D arm's twin.
        if (app.active_markers_view == 'P') {
            phase_resets.delete_selected_phase_reset();
            return;
        }
        warpops.delete_selected_marker();
        return;
    }

    // Tab family: Ctrl+Tab switches tabs; Ctrl+Shift+Tab marches both tabs
    // (a round trip, each step centred by bare `c`'s act, the working zoom
    // written); Tab / Shift+Tab / IsoLeftTab cycle marker focus, landing
    // through the landing owner's walk (Viewport::land_subject,
    // LandingKind::Walk), which reads the zoom and never writes it.
    if (handle_tab_switch_keys(key, mods)) return;

    // Tempo nudge, Up / Down (architect 2026-07-28). No view or selection
    // guard here — adjust_tempo_cents returns at once unless the warp view is
    // active with a non-empty selection and a valid focus, and SINCE 2026-08-30
    // it hands back the sentence for that refusal — and for the singleton's
    // kind refusal (a label ref in either view, a coincident-collapse member
    // in target view) and the group's wall — for this arm to card: the fact is
    // the act's, the card is the dispatch's. THE SINGLETON'S BRACKET END IS
    // SILENT since 2026-08-31 and the Up / Down buttons GREY on it instead
    // (tempo_cent_step_direction_actionable, app_state.h), the kind refusal
    // (since 2026-09-13) and the group's wall greying AND carding beside it.
    //
    // THE MODIFIER IS THE MAGNITUDE (architect 2026-08-31): bare
    // steps ONE cent, Ctrl THREE and Shift TEN (the rungs swapped 2026-09-21,
    // shift the long stride), through the ladder's one owner
    // arrow_step_magnitude (gui_input.h) — the vertical pair's alone since
    // 2026-09-21, the horizontal ladder being retired (the tempo is numeric,
    // placement graphical). THE ACT IS THE
    // SAME BODY at every magnitude: adjust_tempo_cents has taken a signed cent
    // count since it was written, so the three chords differ in nothing but
    // the number they hand it, and its own arms answer at the scaled size —
    // the singleton CLAMPS into the tempo bracket (a Shift press three cents
    // from the max lands ON the max, and one already resting there is the
    // silent walled no-op with the greyed button beside it), while the group
    // REFUSES WHOLE unless every selected member can take the FULL step
    // (group rigidity, at the scan). CTRL+SHIFT SPELLS NOTHING here as
    // everywhere — the pair falls through to the strict-modifier tail — and
    // ALT still binds no press at all. Read-only tabs refuse upstream in all
    // three forms: the allowlist does not admit the vertical arrows on any
    // modifier, and the card names the chord that was pressed.
    //
    // THE ADDRESSED CELL PICKS THE BODY (architect 2026-09-04, the iteration
    // bound cells): with a bound cell addressed (AppState::addressed_cell,
    // written by a marker press on that cell, back on the payload the moment
    // the mode goes off) the same chord at the same magnitude runs the SECOND
    // step body — adjust_iter_bound_cents on the warp column and
    // adjust_iter_bound_hops on the phase-reset one — whose refusal this arm
    // cards exactly as it cards the tempo step's. THIS ARM IS THE VALUE STEP — the chord's name — and the cell
    // is its subject. SAME LADDER: the bound step takes no repeat bit and no
    // coalescing at all (2026-09-10 — it records nothing, so a held run has no
    // burst to open or merge into and simply steps), while the tempo step
    // keeps both, under its own GestureKind. The
    // Up/Down buttons' face forks on the same axis.
    //
    // AND UNDER A LIT LAMP ONLY THE BOUND ARM IS REACHED: the composed gate
    // above admits Up/Down exactly on a bound axis (iteration_lock_key_blocked),
    // so the payload step belongs to a dark
    // lamp, and the buttons grey on the same fork.
    if (!alt && !(ctrl && shift) &&
        (key == GuiKeys::Up || key == GuiKeys::Down)) {
        const int64_t delta = (key == GuiKeys::Up ? +1 : -1) *
                              arrow_step_magnitude(mods);
        switch (app.addressed_cell) {
        case MarkerCell::Payload:
            card_op_refusal(notifications,
                            warpops.adjust_tempo_cents(
                                delta, mods.synthesized_repeat));
            return;
        case MarkerCell::Lower:
        case MarkerCell::Upper:
            // AND THE COLUMN PICKS WHICH BOUND BODY (2026-09-09): the
            // phase-reset column carries an iteration bracket of its own, in
            // HOPS of the analysis lattice, so the same chord at the same
            // magnitude runs that column's step there. It is the one fork this
            // arm makes for the second column — every predicate the faces and
            // the tooltips read forks inside its own body — and the ladder is
            // the same on both, each domain reading the signed magnitude as
            // its own unit. NEITHER TAKES THE REPEAT BIT (2026-09-10): the
            // bound step records nothing, so a held run has no burst to open
            // or merge into and simply steps.
            if (app.active_markers_view == 'P') {
                card_op_refusal(notifications,
                                phase_resets.adjust_iter_bound_hops(
                                    app.addressed_cell,
                                    static_cast<int>(delta)));
                return;
            }
            card_op_refusal(notifications,
                            warpops.adjust_iter_bound_cents(
                                app.addressed_cell, delta));
            return;
        }
        return;
    }
    // CTRL+SHIFT+0 RESETS THE TRIM to the whole song (architect 2026-10-24;
    // Shift+0 from 2026-09-22, which frames the trim since — its arm beside
    // bare `0`'s above) — the maximizer, the RECOVERY route trim's absence
    // from undo requires. It sat on Shift+[ beside the trim region toggle on
    // bare `[` until the architect deleted that toggle on 2026-09-22 (the
    // tablet's pen reaches the trim bar) and gave the bracket to the Waveform
    // Magnification lamp; `0` already means "the whole song" to the camera,
    // so its ctrl-shifted form says it to the trim. The one predicate is
    // is_trim_maximize_key (gui_input.h). It arrives as the `0` key plus the
    // modifier bits, never as a `)` keysym: key_from_keycode
    // (platform_wayland.cpp) reads LEVEL 0 of the keymap, so the shifted
    // level is never consulted. ITS POINTER ROUTE IS ZOOM TO FIT'S CTRL+SHIFT
    // PRESS (redesign_button_ctrl_shift_admits: on the glass the S Pen's
    // barrel held through the long press); a bound dragged onto its partner
    // on the trim bar is the other road to the whole song
    // (auto_clear_crossed_trim, input_trim.cpp).
    //
    // The playhead is an OUTPUT of every trim write, never an input: each parks
    // it at the new trim start (architect 2026-08-05 — the rule and its
    // membership at the head of input_trim.cpp), and a refused press moves
    // nothing. Trim's pointer routes are the trim bar's PLAIN press (single via
    // an endcap hit, pair via a bridge press) and the SWEEP that writes it in
    // one stroke; trim is outside the selection system, so there is no Delete
    // arm.
    if (is_trim_maximize_key(key, mods)) {
        handle_trim_maximize();
        return;
    }

    // BARE `;` OPENS THE SETTINGS PAGE (architect 2026-10-28), the Settings
    // button's plain press dispatching the same chord: the editor's keys as
    // `key=value` rows in the folder overlay, a row opening the settings
    // editor prefilled on its line (open_settings_page, the contract at the
    // declaration). The overlay's open takes the shared modal stop at its
    // own body, past its guards. THE KEY REACHES THE OPENER ON A LOCKED TAB
    // AND UNDER GRID ITERATIONS (read_only_key_blocked's is_settings_page,
    // which the iteration gate inherits): the lock governs the keys, not the
    // surface (the 2026-09-04 rule), and the page is the only road to the
    // device keys, so the editor's commit arms' own lock refusals are the
    // defense (settings_editor.cpp). The `h` view's allowlist refuses it.
    // Shift+; binds nothing (chord_is_bound), silent.
    if (key == GuiKeys::Semicolon && !shift && !ctrl && !alt) {
        open_settings_page();
        return;
    }

    // BARE `'` IS LOAD IN PLACE, AND THE MODE BIT PICKS WHICH ROAD (architect
    // design 2026-08-28). OUTSIDE the `h` view it opens the RENDER PLAYER,
    // which is where a render entry is chosen — a highlighted batch cell and
    // the modal row's Load in place button, `'` again inside the player. IN
    // the view it RAISES THE LOAD CONFIRMATION ON THE VIEWED MEMBER — a
    // commit by its short SHA, or a local member by its number
    // (history_load_in_place; the mode admits this one key through
    // history_mode_key_blocked above) — the view's own road, with no list
    // since 2026-08-29 ("overengineered; I don't really need it": `,` and `.`
    // already walk the members and the lane shows the one they stand on). The
    // fork is HERE rather than inside either road, so each owns one surface
    // whole. TWO PRODUCERS, ONE ROUTE: this key and the icon row's load
    // button, which synthesizes exactly this bare chord through the redesign
    // chord table — and SINCE 2026-09-01 THAT BUTTON REACHES ONLY THE VIEW'S
    // ROAD, its face being dead outside the `h` view (it moved to the history
    // group, whose resting grey it took; the record is at its roster entry).
    // So the PLAYER has exactly one icon-row opener, Play renders, and this
    // key is the second road onto it for the keyboard alone — which is the
    // no-second-road doctrine satisfied, that doctrine being about pointer
    // homes. THE LOCK ANSWERS PER STATE for the same reason (2026-09-01,
    // read_only_key_blocked): outside the view this press is bare `l`'s own
    // admitted act and passes on a locked tab, inside it the load is blocked.
    // Each road is a modal surface and owns its own
    // no-source guard AND the playback stop: playback halts only when the
    // modal actually opens, so a refused raise leaves a listening session
    // undisturbed.
    if (key == GuiKeys::Apostrophe && !shift && !ctrl && !alt) {
        if (app.history_mode.active) history_load_in_place();
        else                         toggle_render_player();
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
    // the waveform lane and this branch does not match: the press falls through
    // to the bare-key tail, which steps the cursor alone. The lane is left by any
    // DESELECTING route (the lane model at playhead_in_marker_lane; Esc is NOT
    // one — it touches no selection),
    // and there is no fallback, so an
    // off-home marker-lane press is a consumed no-op, never a
    // waveform-lane step. (The AUDITION SCRUB is a different gesture entirely — the waveform
    // lower-half one-shot press — and no arrow key reaches it.)
    // ROUTE BEFORE THE STOP: this branch must decide the route ahead of the
    // waveform-lane body's stop / selection-clear, because the two
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
    // tabs refuse both routes upstream: the allowlist admits the
    // horizontal arrows only when playhead_in_marker_lane is false, so a locked
    // tab with a selection drops them at the gate, the lock's lane term
    // reading its one owner.
    //
    // BARE ONLY, AND ONE STEP IN THE ACTIVE COLUMN'S UNIT (architect
    // 2026-09-21, the horizontal ladder retired on every column): Shift and
    // Ctrl spell nothing on Left / Right and fall to the strict-modifier tail
    // in silence (chord_is_bound, gui_input.h). THE CAMERA IS THE HOLD
    // POSTURE'S (architect 2026-09-23): nudge_camera (app_state.h) answers
    // HoldColumn while an explicit centring's posture stands and FollowEdge
    // otherwise, carried to the shared tail and changing nothing else the act
    // does — it was Ctrl's choice for the day before. The unit is
    // horizontal_arrow_step's — one painted column on W and M, ONE HOP on the
    // phase-reset column — and it travels as a HorizontalArrowStep, its unit
    // beside its count, so the hop step reaches the same twin, prologue and
    // landing owner as the column step and takes everything else the nudge
    // does. The WAVEFORM lane's twin is handle_plain_bare_keys' Left / Right
    // case (input_key_dispatch.cpp), which asks the same owner.
    //
    // THE NUDGE KEEPS THE HOLD POSTURE ACROSS ITS WHOLE ACT, the posture's
    // first exemption (AppState::camera_hold): the camera is read BEFORE the
    // act and the bit written back after it, because the act lands the
    // playhead through a movement owner and may move the camera, each of
    // which would otherwise put the posture out.
    if (!ctrl && !alt && !shift &&
        (key == GuiKeys::Left || key == GuiKeys::Right) &&
        playhead_in_marker_lane()) {
        const HorizontalArrowStep step = horizontal_arrow_step(
            key == GuiKeys::Left ? -1 : +1, app.active_markers_view);
        const NudgeCamera camera = nudge_camera(app);
        const bool hold_before = app.camera_hold;
        // Both routes take the press's platform repeat bit: it is what makes a
        // HELD arrow one undo entry (Undo::coalesce_gesture).
        const bool rpt = mods.synthesized_repeat;
        // T+W IS THE CONSUMED REFUSAL, through the HOME-VIEW RULE'S OWN OWNER
        // (active_column_authoring_allowed, app_state.h) rather than a second
        // spelling of it: this site hand-wrote the rule until 2026-08-29, and
        // a one-owner rule with a hand copy beside it is a rule waiting to
        // drift. ONE refusal is left since the P column opened to both audio
        // views (architect 2026-08-30, and again with S+P's return,
        // 2026-10-12) — a WARP marker in target view, which
        // lost the tempo-image step with the whole tempo drag family — so the
        // card needs no column fork; "Phase resets are moved in target view"
        // retired with the P gate.
        // IT ASKS THE OWNER AND NOT THE COMPOSED PREDICATE since 2026-08-31,
        // and the difference is the CARD: horizontal_arrow_step_actionable
        // grew the marker's own WALL term that day for the Left / Right
        // buttons' face, and this sentence is about the VIEW — raising it for
        // a marker resting on frame 0 would be a lie. The wall's refusal is
        // the twins' own and it is SILENT (a benign one-dimensional refusal
        // already at its state), so a walled press still runs the act and the
        // act still writes nothing. The buttons grey on both terms, so a lift
        // reaches neither line.
        if (!active_column_authoring_allowed(app)) {
            notifications.notify(
                AppState::NotificationClass::Normal,
                "Markers are moved in source view");
            return;
        }
        // The twins' reason channel, raised here where a press is known to
        // have happened (GuiOpRefusal, warpmarkers_ops.h). Since 8cf604ce
        // both twins carry ONE payload through it, kMarkerNudgeOffEdgeCard
        // (notifications.h), for the painted-edge refusal
        // (source_frame_off_right_edge — a landing past the last painted
        // column at this zoom) and std::nullopt otherwise — the rest of the
        // prologue's refusals are belts or an outer gate's card, and the
        // wall went silent on 2026-08-31 — and the arm stays because the
        // channel is the cluster's contract, not this gesture's private
        // arrangement.
        // Past the gate the column is W (in source view) or P — one twin per
        // column, each the same body over its own store.
        const auto refusal =
            app.active_markers_view == 'P'
                ? phase_resets.nudge_selected_phase_resets(step, camera, rpt)
                : warpops.nudge_selected_markers(step, camera, rpt);
        app.camera_hold = hold_before;
        card_op_refusal(notifications, refusal);
        return;
    }

    // (CTRL+LEFT / CTRL+RIGHT IN THE WAVEFORM LANE stood here from 2026-09-22,
    // the step with the held-column camera; the architect deleted the chord
    // 2026-09-23 when the camera became the hold posture's, so the ctrl form
    // binds nothing and falls to the strict-modifier tail in silence.)

    // PageUp / PageDown: step the viewport back / forward by exactly the
    // plain wheel's stepped-pan step (samples_visible / kViewportPanStepDivisor).
    // PageUp goes back, PageDown
    // forward. Pure active-display navigation, so the read-only allowlist
    // admits it.
    if (!ctrl && !alt && !shift &&
        (key == GuiKeys::PageDown || key == GuiKeys::PageUp)) {
        const int64_t step = std::max<int64_t>(
            1, samples_visible(app, audio) / kViewportPanStepDivisor);
        viewport.scroll_viewport(key == GuiKeys::PageUp ? -step : +step);
        return;
    }

    // Bare-key dispatch. Every modifier-gated handler above this point
    // returns on match, so by the time we reach here, any modifier being
    // held means the chord had no binding and should be a silent no-op
    // — never fall through into a bare binding (e.g. Ctrl+Shift+Alt+F
    // must not run bare `c` via GuiKeys::C).
    if (!ctrl && !shift && !alt) {
        handle_plain_bare_keys(key);
        return;
    }
    // THE STRICT-MODIFIER TAIL IS SILENT, AND THAT SILENCE IS THE ANSWER
    // (architect 2026-08-30): "bound keys either show an effect or a card, so
    // an unbound key is identified by its silence." Every modifier-gated
    // handler above returned on match, so a modified press that arrives here
    // wears a combination nothing binds, and the product says nothing at all.
    //
    // THIS IS THE RULE'S HOME on the key path, stated once here and pointed at
    // from the gates: because every OTHER refusal now answers — an act's own
    // reason, a gate's state, a wall — a press that produces no card and no
    // visible change can only be a press with no binding, which is a stronger
    // answer than a sentence saying so and costs no card for a mistyped
    // modifier. THE GATES ABOVE ASK chord_is_bound (gui_input.h) FOR EXACTLY
    // THIS REASON: a chord that would be silent here must not be given a
    // sentence by a gate it happened to meet on the way down, or the same
    // press would say two different things depending on the state it landed
    // in. Strict modifier validation itself is untouched — the no-op it makes
    // of an unbound combination is the same no-op it always was
    // (GuiInputHandler::on_key states the rule).
    //
    // THE BARE HALF is handle_plain_bare_keys' default arm, silent in the same
    // words for the same reason. Both carded "<chord> is not bound" for the
    // one day of 2026-08-30, with the render player's and the picker's
    // catch-alls, the folder overlay's modified row press and the pointer's
    // modified press on the waveform and the top strip; all six retired
    // together that evening.
}

// THE HEAD GATES' SHARED HALVES — each a verdict or a sentence on_key's own
// gate asks, hoisted so the car's undo / redo road (run_undo_redo_without_key,
// below) asks the same question rather than a second copy of it. The gates'
// rulings and their rankings stay stated at their sites in on_key.
bool GuiInputHandler::keyboard_owned_by_pointer_gesture() const {
    return app.drag.active || app.value_drag.active || app.trim_drag.active ||
           app.region_drag.active ||
           app.scroll_drag.active ||
           app.pending_marker_press.active || app.pending_click.active() ||
           app.pending_trim_drag.active;
}

// THE AUTHORING LOCK'S KEYBOARD VERDICT AND ITS CARD (the ruling, the
// composition that ranks the read-only sentence first, and the card's two
// sentences are stated at the gate in on_key): true when the lock drops the
// chord, the card raised for a bound chord alone.
bool GuiInputHandler::authoring_lock_refuses_chord(GuiKey key,
                                                   GuiInputState mods) {
    if (!authoring_lock_drops_chord(app, key, mods)) return false;
    const bool read_only_says_no =
        active_view_state(app).read_only && read_only_key_blocked(app, key, mods);
    if (chord_is_bound(key, mods, app.history_mode.active))
        notifications.notify(AppState::NotificationClass::Normal,
                             read_only_says_no
                                 ? read_only_chord_card(spell_chord(key, mods))
                                 : std::string(kIterationLockCard));
    return true;
}

// THE UNDO / REDO COMMAND — Ctrl+Z's and Ctrl+Shift+Z's arm (on_key), whose
// caller has already passed every head gate; `synthesized_repeat` is the
// key's own repeat bit. The Undo / Redo roster buttons reach it through their
// chord on on_key, so the gates above hold for them unchanged.
//
// RETURNS WHETHER A RESTORE RAN (the contract and the car's reason at the
// declaration): false at each of the two refusal arms below, otherwise the
// ops' own answer. on_key's arm drops it on the floor.
bool GuiInputHandler::run_undo_redo_command(bool redo,
                                            bool synthesized_repeat) {
    // THE ITERATION LOCK REFUSES FIRST (architect 2026-09-10: "They just
    // don't go in the undo stack at all; they're considered transient by
    // design" — so while the lamp is lit the history is frozen whole, both
    // stacks and both tabs, exactly as the `h` view freezes the local
    // walk's). It is ranked ahead of the two terms below because the lock
    // is the OUTERMOST STATE, a mode entered on purpose, and because those
    // two would otherwise answer a full stack with the other tab's
    // sentence — a wrong cause. The pair NAMES THE ACT rather than saying
    // "turn off grid iterations first" like every other site under this
    // lock: a user who just pressed Ctrl+Z is asking about undo. Both
    // buttons grey on the same fact (history_step_actionable's third term),
    // so no lift reaches this line; the sentence is THIS KEY'S alone since
    // 2026-09-12, when the refusal-reason tooltip lines went — the greyed
    // pair names its own act and nothing more.
    // The chord IS admitted by iteration_lock_key_blocked precisely so
    // this arm can speak.
    if (app.iteration_mode_enabled) {
        notifications.notify(AppState::NotificationClass::Normal,
                             redo ? kIterationLockRedoCard
                                  : kIterationLockUndoCard);
        return false;
    }
    // THE REFUSAL SAYS WHICH ONE IT IS (architect 2026-08-30). The
    // authoritative test is history_step_actionable (app_state.h), the
    // one predicate do_undo / do_redo run and the Undo / Redo buttons
    // grey on; asked here it can also NAME what it refused, forked on
    // its own two terms — an empty stack (nothing recorded in that
    // direction) and a top entry whose TARGET tab is locked, which is
    // the cross-tab case the active tab's own lock cannot explain (the
    // keyboard gate ahead of this body already answers a locked ACTIVE tab). ONE CARD
    // PER PRESS: the ops' own guard is the belt behind this and stays
    // silent, and both buttons grey on the same predicate, so neither a
    // lift nor a held button's fire reaches this line.
    const std::vector<UndoEntry>& stack =
        redo ? app.history.redo_stack : app.history.undo_stack;
    if (!history_step_actionable(app, stack)) {
        // A HELD Ctrl+Z THAT RUNS OUT OF HISTORY STOPS SILENTLY (architect
        // 2026-09-13): a SYNTHESIZED REPEAT meeting the empty stack is a
        // benign one-dimensional refusal already at its state — the hold
        // walked the history to its end, and the restores it just ran are
        // the screen's answer. A DELIBERATE press keeps the card, the
        // counter-class rule (notifications.h) standing for it. The other
        // tab's lock below is not a wall the hold ran into but a reason,
        // so it cards on a repeat too, once per burst through
        // HeldRepeatDispatchScope. The held BUTTON never reaches this
        // line: its face greys on the same predicate and its burst rests
        // there (tick_chrome_press_repeat).
        if (stack.empty() && synthesized_repeat) return false;
        notifications.notify(
            AppState::NotificationClass::Normal,
            stack.empty()
                ? (redo ? "There is nothing to redo"
                        : "There is nothing to undo")
                : "That step belongs to the other tab, which is "
                  "read-only");
        return false;
    }
    return redo ? undo.do_redo() : undo.do_undo();
}

// THE CAR'S UNDO / REDO (architect 2026-09-17: the head unit's Previous and
// Next with the render player closed are Ctrl+Z and Ctrl+Shift+Z WHOLE). A
// deliberate press of the chord that never passes through on_key, so it asks
// the HEAD GATES on_key asks ahead of the arm, in on_key's order and through
// the same verdicts and sentences: an editor text-selection
// drag or a pointer gesture owning the keyboard (kKeysDuringDrag), an open
// editor swallowing the chord — SILENTLY since 2026-10-25, a bound chord
// under any modal saying nothing (the editor gate in on_key), and the editor
// a belt here, the caller dropping every editor first — and the
// authoring lock (a read-only tab cards the chord; the iteration lock admits
// it so the command's own arm names undo). The routers ranked above those
// gates — the prompt, the two folder-overlay contents, every editor —
// and the `h` view's gate are the CALLER'S to drop (GuiCarTransport::admits),
// and chord_is_bound answers true for both chords, which the helpers ask
// anyway. Then the command itself, as a deliberate press (no repeat).
// ON_KEY'S NO-PIECE GATE IS NOT ASKED: the car's hook drops every command
// under the open's presentation hold (run_project's hook block, main.cpp),
// which stands until the load has installed the audio, so no car press
// reaches here without a piece.
//
// IT ANSWERS THE CAR'S OWN QUESTION (architect 2026-09-18): true iff a
// restore actually ran — false at each head gate below, otherwise the
// command's answer, which is false at each of its two refusal arms and at
// the ops' belt. THE SKIP PLAYS ONLY WHAT IT ACTUALLY STEPPED TO, so a
// refused press ends here with today's card and nothing else, a running loop
// left running (nothing new was stepped to, so there is nothing new to hear;
// the tail is GuiCarTransport::car_previous / car_next).
bool GuiInputHandler::run_undo_redo_without_key(bool redo) {
    const GuiKey key = GuiKeys::Z;
    GuiInputState mods{};
    mods.ctrl  = true;
    mods.shift = redo;
    const auto card_bound = [&](std::string sentence) {
        if (chord_is_bound(key, mods, app.history_mode.active))
            notifications.notify(AppState::NotificationClass::Normal,
                                 std::move(sentence));
    };
    // AN EDITOR SWALLOWS THE CHORD IN SILENCE (2026-10-25; the one predicate
    // for a keyboard-taking modal), its text drag with it — a text drag
    // exists only under an editor.
    if (keyboard_modal_editor_active()) return false;
    if (keyboard_owned_by_pointer_gesture()) {
        card_bound(kKeysDuringDrag);
        return false;
    }
    if (authoring_lock_refuses_chord(key, mods)) return false;
    return run_undo_redo_command(redo, /*synthesized_repeat=*/false);
}

// THE CAR'S PLAY MEETS SPACE'S OWN GATES (architect 2026-09-17: "look at how
// undo/redo and play work in the GUI under an open flag editor or a drag, and
// follow that rule" — EACH CAR BUTTON IS ITS KEY). Bare Space's head gates in
// on_key's order, through the same verdicts and the same sentences the skips'
// road above asks, answered for the caller: TRUE means the press died here,
// having already said whatever the key says. The routers ranked above these
// gates and the `h` view's are the caller's to drop
// (GuiCarTransport::admits).
//
// ON_KEY'S NO-PIECE GATE IS NOT ASKED, as at the skips' road above: no car
// press reaches here without a piece.
//
// IT IS THE GATES AND NOT THE KEY WHOLE, the one place the car's three
// buttons differ in shape: Previous and Next run Ctrl+Z's own command body,
// while the play's ACT is the car's alone — a loop of the trim from its begin,
// GuiPlaybackLifecycle::car_toggle_playback — so the cluster keeps the act
// (and Space's target-view readiness gate, which ranks below these) and takes
// only the gates from here.
//
// THE EDITOR ARM RAISES NO CARD: a bound chord under any modal is silent
// (2026-10-25), and Space is a PRINTABLE key the field takes as a typed
// space. It reads the one predicate for a keyboard-taking modal, its text
// drag with it, and is a belt — the caller drops every editor first
// (GuiCarTransport::admits).
//
// THE AUTHORING LOCK IS NOT ASKED, and that too is the key's answer: both its
// reasons ADMIT Space — read_only_key_blocked carries is_play_pause on its
// allowlist and iteration_lock_key_blocked falls through to that list — so a
// call would be a branch with no true arm.
bool GuiInputHandler::car_play_refused_by_key_gates() {
    const GuiKey key = GuiKeys::Space;
    GuiInputState mods{};
    const auto card_bound = [&](std::string sentence) {
        if (chord_is_bound(key, mods, app.history_mode.active))
            notifications.notify(AppState::NotificationClass::Normal,
                                 std::move(sentence));
    };
    if (keyboard_modal_editor_active()) return true;
    if (keyboard_owned_by_pointer_gesture()) {
        card_bound(kKeysDuringDrag);
        return true;
    }
    return false;
}

// THE ADDRESSED-CELL WRITE AND ITS DAMAGE, spelled once for the walk: the
// bright cell moves, and the flag cache keys the axis (fp_addressed_cell,
// waveform_cache.cpp), so the marker lane must be damaged wherever the axis
// moves — including where the MEMBERSHIP stood still, which on a same-marker
// step is every time (that arm's collapse re-seats the same singleton). This
// is the marker press's own write (run_marker_click_act, input_pointer.cpp),
// guard included: a write that changes nothing damages nothing.
void GuiInputHandler::write_addressed_cell(MarkerCell cell) {
    if (app.addressed_cell == cell) return;
    app.addressed_cell = cell;
    viewport.invalidate_top_strip();
}

void GuiInputHandler::cycle_marker_focus(bool forward,
                                         MarkerLandingFrame frame) {
    // THE WALK REFUSES WHOLE AT A WALL (architect 2026-08-30, the strictness
    // ruling), and the test is THE STEP OWNER'S — marker_walk_step
    // (app_state.h), whose `marker` field is -1 where the step would land
    // nothing. A step with nothing ahead writes NOTHING: no select, no
    // playhead land, no recentre. THE Ctrl+Shift+Tab MARCH
    // takes this answer at each of its two cycles (its tab switches and its
    // two `c`s are their own acts and still run); it reads nothing back, the
    // step being this body's own business since the march stopped walking
    // cells.
    //
    // THE REFUSAL IS SILENT (architect 2026-08-31): a benign
    // one-dimensional refusal already at its state says nothing — one glance
    // at the marker lane shows the focus is at its end. The Walk button stays
    // lit there (its ctrl form, the tab switch, always acts — the twin rule
    // at redesign_button_enabled), so its plain or shifted lift at the wall
    // reaches this same silence.
    const MarkerWalkStep step = marker_walk_step(app, audio, forward);
    if (step.marker < 0) return;

    // A STEP BETWEEN THE CELLS OF ONE MARKER IS A COLLAPSE AND AN AXIS WRITE,
    // AND NOTHING ELSE (architect 2026-09-10, the bound cells joining the
    // walk; the collapse the same day, the cells being "a separate system"
    // that "is by design targeting each marker individually"): the focus has
    // not moved, so there is no OTHER marker to select, the playhead is
    // already sitting on this one — it is what made it the seat — and moving
    // it would be a movement in the music for no reason. `frame` GOVERNS
    // MARKER-TO-MARKER STEPS ALONE for the same reason: there is no new marker
    // to frame, and a recentre here would move the camera under a user reading
    // the cell he just stepped onto.
    //
    // THE COLLAPSE IS WHAT KEEPS A BOUND AXIS SINGLETON, which every road onto
    // one now does (the plain cell press and the bound editor's open both
    // single-select already): a walk that stepped onto a cell while a 2+
    // selection stood would address a bound the arrows could only step for one
    // marker of it. set_single_selection on the seat is the cheapest spelling
    // of that — it is a no-op on the store's membership when the seat is
    // already the singleton, and it damages the top strip either way, which
    // this arm owes for the moved bright cell in any case. It runs BEFORE the
    // axis write for the family's own reason: the seat resets the axis to the
    // payload (Selection::seat_focus), so the cell is written behind it, as
    // the marker press and the two cell editors' opens write theirs.

    if (step.same_marker) {
        selection.set_single_selection(step.marker);
        write_addressed_cell(step.cell);
        return;
    }

    // The marker step. Selection::cycle_selection asks marker_walk_landing
    // again on the way through, which is the same landing this step carries:
    // the same-marker arm has already returned above, so the only way here is
    // the one where the step IS the landing owner's answer.
    if (forward) selection.select_next_marker();
    else         selection.select_prev_marker();

    // The select above establishes the focused marker; the shared jump tail
    // moves the playhead onto it and treats the camera as `frame` says.
    //
    // THIS BODY DOES NOT DECIDE THE FRAMING and asks no preference of its own
    // (architect 2026-09-04). The walk moves focus and lands the playhead; the
    // camera is its caller's statement, forwarded untouched: the three Tab
    // arms state MarkerLandingFrame::Land, the landing owner's walk
    // (Viewport::land_subject, LandingKind::Walk, architect 2026-09-24),
    // which reads the zoom and never writes it; both steps of the
    // Ctrl+Shift+Tab paired march state Center and run bare `c`'s act behind
    // it (2026-09-26).
    // Putting a policy read in here is what once made the march inherit
    // one, which is the shape the required parameter exists to prevent:
    // framing cannot be acquired by saying nothing. `c` remains the direct
    // route to the working zoom (`0` reached it too, at a full zoom out it
    // did not produce, until 2026-09-23; that press is a silent no-op now).
    // A CYCLE STEP THAT LANDS NOTHING CHANGES NOTHING: with no marker to focus
    // the jump returns false having touched neither playhead nor viewport — a
    // Tab in an empty collection stays the consumed nothing it has always been.
    // The jump's own false return — a missing or out-of-range focus — cannot
    // happen behind the gate above: the select just focused the landing that
    // gate proved. Nothing here reads it; the return exists for the other
    // caller (run_center_command).
    // A step is a movement like any other through the land owner.
    // (A FRAMING STEP FROM A FINER ZOOM RETURNED TO WORKING here from
    // 2026-09-15 to 2026-09-22, on a Center the zoom-derived walk stated; it
    // went with that derivation.)
    // THE HOLD POSTURE is the landing owner's to arm, on the walk's centring
    // of the single landing (AppState::camera_hold); nothing here arms it.
    jump_playhead_to_focused_marker(frame);

    // AND THE CELL THE STEP CAME TO REST ON, written AFTER the seat because
    // every Selection mutator resets the axis to the payload as it seats the
    // focus (Selection::seat_focus) — the marker press's and the two cell
    // editors' opens do the same for the same reason. Payload needs no write:
    // the seat has already put it there.
    if (step.cell != MarkerCell::Payload) write_addressed_cell(step.cell);
}

bool GuiInputHandler::jump_playhead_to_focused_marker(MarkerLandingFrame frame) {
    // The walk is markers-only (trim is not a cycle stop). The playhead lands on
    // the focused marker unconditionally, and the camera below does whatever
    // `frame` says — this body resolves nothing, asking no lamp, no camera
    // posture and no mode (architect 2026-09-04). Follow never gated the
    // cycle either.
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
    // holds — nothing moved there, so nothing needs erasing. IT ALSO ENDS THE
    // A/B AUDITION, being one of the two playhead movement owners (the rule at
    // Viewport::move_playhead_to, viewport.cpp). What stays
    // HERE is exactly what the owner does not provide: the stop above and the
    // recenter below.
    land_playhead_on_marker(app, audio, viewport, app.last_selected_marker);

    // Center the viewport on the focused marker at the current zoom. THE ZOOM
    // IS THE CALLER'S, and the two callers answer differently: `c` snaps to the
    // working zoom right after this returns, the Tab walk sets nothing
    // (architect 2026-08-05, "no zoom on Tab", at every level since
    // 2026-09-22), and the paired march's steps run `c`'s act behind this
    // (2026-09-26) — so this tail frames the stop at whatever level it was
    // called at, and only `c`'s zoom write re-centres after it. Follow does
    // not gate it either (architect 2026-07-19, reversing the earlier
    // follow-only rule).
    // center_viewport_on_playhead is the SOLE viewport write in this arm: it
    // reads the cursor we just set and scrolls once to center it, emitting one
    // coherent set of waveform + top-strip damage against the final viewport.
    // center_viewport_on_playhead routes through kick_waveform_sync (whose
    // installed callback OWES the frame's synchronous rebuild,
    // GuiPaintHandler::owe_waveform_rebuild) inside its own moved guard, so a
    // recenter that scrolls already gets its one rebuild before the frame —
    // no second call in this function's tail. The unmoved path (EOF-clamped
    // no-op) needs none.
    //
    // `frame` IS THE CALLER'S TOO (architect 2026-09-04), and it selects among the switch's arms and nothing else:
    // the land above already happened, so a Land that moves no camera still
    // moves the focus and the playhead — the camera simply stays where the
    // user left it while the landing is on screen.
    // THE Land ARM IS THE LANDING OWNER'S WALK (Viewport::land_subject,
    // LandingKind::Walk, architect 2026-09-24) over the cursor just seated
    // (lo == hi; playback is stopped above): at the working zoom, as painted,
    // it centres the landing, on screen or not, arming the hold posture; at
    // every other zoom, finer or coarser, an on-screen landing moves nothing
    // and an off-screen one is paged in the edge margin from the left edge. A single marker always
    // fits, so the owner's cannot-fit verdict is dropped.
    //
    // WHO PASSES WHAT, re-grepped 2026-09-26: `c` (run_center_command) states
    // Center; cycle_marker_focus forwards Land from the three live Tab arms
    // and Center from the two steps of the Ctrl+Shift+Tab paired march, each
    // followed by `c`'s own act. Ctrl+J reaches the camera through
    // run_center_command by name and the A/B audition through the centre
    // key's act (run_center_key_command, 2026-09-29), and so both take its
    // Center with it.
    switch (frame) {
        case MarkerLandingFrame::Center:
            viewport.center_viewport_on_playhead();
            break;
        case MarkerLandingFrame::Land:
            (void)viewport.land_subject(app.playhead_cursor_sample,
                                        app.playhead_cursor_sample,
                                        LandingKind::Walk);
            break;
    }
    return true;
}

void GuiInputHandler::run_center_command() {
    // THE BARE `c` COMMAND, WHOLE — playback stopped, then the working zoom
    // centered on the playhead, with a focused stop re-landed under it first —
    // and THE ONE PLACE THE MODE
    // FORK LIVES. TWO CALLERS, re-grepped 2026-09-29: the centre keys' act
    // run_center_key_command (since 2026-09-23 the one road of the live `c`
    // key arm in handle_plain_bare_keys, the history mode's own `c` arm in
    // handle_history_mode_key — which must claim the key to keep it off the
    // mode's allowlist — and the writer of the hold posture a centring key
    // arms; since 2026-09-26 also run by both steps of the Ctrl+Shift+Tab
    // paired march, live and `h`; and since 2026-09-29 by the A/B audition's
    // GuiAbAudition::apply_working_zoom, which opens each half of the act with
    // this command on the tab that half plays, inside the tab switch's own
    // frame, and arms the hold as the key does — the rule and the ordering it
    // owes the audition's sequence are at ab_audition.h; it called this
    // command directly from 2026-08-28), and, since 2026-08-29, the jump to
    // the value's source
    // (CTRL+J, jump_to_value_source, input_key_dispatch.cpp), WHICH CALLS THIS
    // TWICE — it is still ONE caller: once on the
    // CURRENT tab before it leaves (the audition's own shape, so the origin
    // tab is framed on the reference it was read from) and once as its last
    // act, after the single-select, so the focused arm below centers on
    // exactly the marker the jump named.
    //
    // (THE Ctrl+Shift+Tab PAIRED MARCH, live and `h`, reaches this through
    // run_center_key_command behind each of its two steps, the round trip of
    // 2026-09-26.)
    //
    // (`0` WAS A CALLER too until 2026-09-23: at a full zoom out it had not
    // produced it ran this command. That arm was deleted the same day — such
    // a `0` is a silent no-op, run_overview_command.)
    //
    // THE LEVEL IS ALWAYS kWorkingZoomLevel. From 2026-08-18 to 2026-09-23 it
    // was a parameter that `0`'s second press filled with the level it had
    // stamped; that press restores the stamped view itself now (the toggle,
    // run_overview_command), so no caller substitutes and the parameter went.
    //
    // THE FORK IS HERE RATHER THAN AT THE CALLERS because that makes it ONE
    // decision for all of them: the audition and Ctrl+J are refused inside
    // the mode, while the centre keys' act is reached from both, the live `c`
    // arm and the mode's own `c` claim. One owner, one answer.
    if (app.history_mode.active) {
        // THE MODE'S RE-EXPRESSION: the live recipe read against the mode's own
        // data. With a focus standing the playhead re-lands on that diff flag
        // first (idempotent when it is already there, which after a click or a
        // Tab step it is). The live arm's repair_last_selected /
        // jump_playhead_to_focused_marker pair is deliberately NOT run: it walks
        // the live stores, which the lane is not showing. `c` with nothing
        // focused is a zoom and a recenter, camera and nothing else.
        // THE RANGE TEST IS THE FORK'S ONE OWNER since 2026-09-01
        // (center_command_lands_on_focus, app_state.h — the Center button's
        // hint reads it too, "Center on focus" / "Center on playhead").
        if (center_command_lands_on_focus(app)) {
            const int focus = app.history_mode.focus;
            // The stop stands on the focused arm alone here, while the live
            // recipe stops on both of its arms. NO REACHABLE PRODUCER
            // (recorded 2026-08-06): the entry owner stops any session running
            // before `h` and nothing in the view can start one, so this call
            // is a formality kept for the regime's shape — the same note the
            // mode's Tab cycle, its Home/End and the revert act carry at their
            // own stops (input_key_dispatch.cpp) — and the no-focus arm, which
            // spells none, centres the resting playhead all the same.
            playback_lifecycle.stop_playback_if_playing();
            // On the focused CUE: a nudged pair's focused half alone
            // (AppState::HistoryMode::focus_half, 2026-10-26).
            land_playhead_on_domain_frame(
                app, audio, viewport,
                history_diff_flag_landing_domain_frame(
                    app, audio,
                    app.history_mode.flags[static_cast<std::size_t>(focus)],
                    app.history_mode.focus_half));
        }
        viewport.apply_zoom_change(kWorkingZoomLevel);
        viewport.center_viewport_on_playhead();
        return;
    }

    // THE LIVE RECIPE. Jump to kWorkingZoomLevel, the ideal warp-authoring
    // zoom. When a marker is focused, first
    // jump the playhead
    // exactly onto it — the same jump the Tab family runs, after the same
    // last-selected repair — then set that level and center on it; with no
    // focused marker, keep the plain zoom-and-center-on-playhead
    // behavior.
    // The focused arm's jump lands the playhead through the land owner, a
    // movement; the NO-FOCUS arm only zooms and recenters, a camera move.
    // THE COMMAND ALWAYS STOPS PLAYBACK BEFORE IT CENTRES (architect
    // 2026-09-29), on both arms, so the centring is always on the resting
    // playhead and the hold the centre key arms behind it
    // (run_center_key_command) stands on the playhead it centred — never on
    // a moving scanner that leaves a resting cursor uncentred, perhaps off
    // screen. Follow is the road that keeps the scanner in view. ONE STOP PER
    // ROAD: the focused arm's stop is the jump's own (the Tab walk's, which
    // it shares), and the jump returns false exactly when it touched nothing,
    // stop included, so the no-focus arm spells its stop on that return,
    // ahead of the zoom and the centre. Every caller inherits it here — bare
    // `c`, the Center button, both steps of the paired march, Ctrl+J's two
    // calls and the A/B audition's three, each of which runs with the act's
    // sequence Idle and none of the act's plays sounding — the first ahead of
    // the switch that would stop a play the press found anyway, the other two
    // behind a switch — so the stop ends no play of the act's own
    // (ab_audition.h).
    // A GROUP CARRIES (architect 2026-07-30, with the SPAN FORM retired): the
    // collapse-to-focus that stood here is deleted — it existed only to keep a
    // group from resting SPANLESS, a state that no longer exists now the region
    // IS THE TRIM rather than a group's playhead form. The jump below is a
    // jump TO THE FOCUS and accepts a group's focus as-is; the always-visible
    // cursor lands there, the other members keeping their brightened flags.
    // (The jump's own no-focus return is center_command_lands_on_focus's live
    // arm — the focus atom — plus an out-of-range belt the faces accept
    // reading unrepaired; the Center button's hint reads that predicate.)
    // THE CAMERA IS STATED, NOT INHERITED (architect 2026-09-04): `c` frames,
    // always and by its own name, so it hands the jump
    // MarkerLandingFrame::Center rather than taking a default. Its own
    // center_viewport_on_playhead below the zoom is the post-zoom re-centre
    // and is a separate act from this one.
    selection.repair_last_selected();
    if (!jump_playhead_to_focused_marker(MarkerLandingFrame::Center))
        playback_lifecycle.stop_playback_if_playing();
    viewport.apply_zoom_change(kWorkingZoomLevel);
    viewport.center_viewport_on_playhead();
}

// THE CENTRE KEY'S ACT (contract at the declaration). FOLLOW'S SUSPENSION IS
// LEFT TO THE CHOKEPOINT: the command stops any play before it centres, so
// the centring's camera write suspends nothing that is read — the suspension
// means something only while a project play runs, and the next launch clears
// it (AppState::follow_suspended).
void GuiInputHandler::run_center_key_command() {
    run_center_command();
    app.camera_hold = true;
}

// IS THERE A VIEW FOR BARE `0` TO RETURN TO — the contract is at the
// declaration (app_state.h).
bool overview_recall_restorable(const AppState& app) {
    const ViewState& v = active_view_state(app);
    return v.whole_song_visible &&
           v.overview_recall.audio_view == app.active_audio_view;
}

// WHAT BARE `0` WOULD DO — the contract and the three readers are at the
// declaration (app_state.h); the reasoning for each arm is at
// run_overview_command below, which is the fork's act and decides nothing of
// its own past this. The ceiling compare is `>=`.
OverviewCommandTarget overview_command_target(const AppState& app,
                                              const GuiAudio& audio) {
    OverviewCommandTarget t;
    // NO PIECE, NO ACT: while there is nothing to dispatch on (a set before
    // its load has installed the audio — the open's hold's one frame paints
    // this face, platform.h's head) the fork answers the no-op through the
    // one owner the roster's grey reads
    // (no_audio_to_dispatch_on), so the tooltip over the grey face names the
    // act plainly and nothing below reads the working column before a load
    // publishes it.
    if (no_audio_to_dispatch_on(app, audio.total_frames())) {
        t.arm = OverviewCommandTarget::Arm::NoOp;
        return t;
    }
    // THE WHOLE-SONG STATE ANSWERS FIRST (since 2026-09-02): a `0` that
    // landed on the ceiling is still out after a resize or an S/T flip has
    // moved that ceiling, and its stamp is the answer — in the stamp's own
    // audio view.
    if (overview_recall_restorable(app)) {
        t.arm   = OverviewCommandTarget::Arm::RestoreView;
        t.level = clamp_zoom_level(
            app, audio, active_view_state(app).overview_recall.zoom_level);
        return t;
    }
    // The state standing over the other view's stamp is out with nothing to
    // return to; a zoom-out there would re-stamp over the stamp the other
    // view still owns.
    if (active_view_state(app).whole_song_visible) {
        t.arm = OverviewCommandTarget::Arm::NoOp;
        return t;
    }
    const double full_out = effective_max_zoom_level(
        waveform_area(app).w, live_total_frames(app, audio),
        audio.working_column());
    if (app.zoom_level >= full_out) {
        t.arm = OverviewCommandTarget::Arm::NoOp;
        return t;
    }
    t.arm   = OverviewCommandTarget::Arm::ZoomOut;
    t.level = full_out;
    return t;
}

void GuiInputHandler::run_overview_command() {
    // The bare `0` key, Zoom to Fit (its name since 2026-10-24): A TOGGLE
    // BETWEEN FULL ZOOM OUT AND THE VIEW IT LEFT
    // (architect 2026-09-23: "Zero actually should be a toggle between full
    // zoom out and restore to previous zoom level, playhead position, and
    // viewport position"). It superseded the 2026-08-05 / 2026-08-18 reading,
    // whose second press ran the `c` command at the stamped level and so
    // re-centred instead of restoring the window.
    //
    // THE FORK is overview_command_target's, one owner shared with the Zoom
    // to Fit button's face and tooltip; its three arms:
    //
    // FIRST PRESS (ZoomOut): STAMP the zoom level, the viewport start and the
    // resting playhead into the active tab's OverviewRecall, with the audio
    // view they are expressed in, then jump to the per-file ceiling and raise
    // the tab's whole-song state. A PURE VIEWPORT MOVE (architect 2026-07-30):
    // it writes neither the selection nor the playhead, stops nothing (the
    // pure-viewport-move class of the keyboard stop rule,
    // stop_playback_if_playing's declaration), and a selection's span survives
    // it as it survives every zoom gesture.
    //
    // SECOND PRESS (RestoreView, overview_recall_restorable: the whole-song
    // state standing in the audio view the stamp was taken in): RESTORE THE
    // THREE VERBATIM, NO CENTRING. The playhead first, through the movement
    // owner Viewport::move_playhead_to — a route to a spot nothing marks, so
    // when it moves the cursor it pays exactly what Home / End pay (the stop,
    // the selection clear or, in the `h` view, the mode's focus clear); a
    // stamp equal to the live cursor writes nothing there. Then the
    // whole-song state is cleared and the level and viewport start go down
    // together through Viewport::apply_zoom_to_start, whose
    // clamp_viewport_start owns the lattice snap and the walls (the discrete
    // move's damage, predictor resync and synchronous rebuild are that
    // applier's). While zoomed out the whole song is visible, so the movement
    // owner's keep-visible edge-align never scrolls ahead of the applier. If
    // the playhead moved while zoomed out, the stamped one still comes back —
    // the ruling's "restore ... playhead position". A stamped level above a
    // ceiling that has since fallen clamps onto it; the state is cleared
    // regardless, so the press still ends the trip.
    //
    // NOTHING TO RETURN TO (NoOp — architect 2026-09-23: "if there's nothing
    // to return to, or the place you would return to has been put away
    // because we've changed views, zero should just lose its meaning"): a
    // ceiling this key did not produce, or a stamp taken in the other audio
    // view. A benign refusal already at its state — silent, no card
    // (notifications.h) — and the button greys unless one of its two modified
    // presses would act, Shift+0's frame or Ctrl+Shift+0's reset
    // (span_framing_would_act, reset_trim_would_act; 2026-10-25). The
    // whole-song state is left standing, so a flip back
    // brings the stamp into reach again. (Until that ruling a cross-view
    // stamp was re-expressed through the live map and the unstamped ceiling
    // ran the `c` command; both arms are deleted.)
    //
    // THE HOLD POSTURE AND FOLLOW (AppState::camera_hold /
    // follow_suspended): `0` arms no hold and keeps none. Both acting presses
    // put the hold out and suspend a following play's paging through the one
    // chokepoint compare in clamp_viewport_start (the camera they settle
    // differs from the last one settled), and the restore's movement owner
    // puts HOLD out as well; no exemption is spelled here. The no-op writes
    // nothing and so changes neither.
    //
    // THE TWO RECORDS are this function's alone: it is the stamp's one writer
    // and the whole-song state's one setter (both declarations, app_state.h).
    // The trim-bar DOUBLE-CLICK and Shift+0 DIVERGE from this
    // (run_span_framing_command): they frame the trim / whole-song SPAN.
    const OverviewCommandTarget target = overview_command_target(app, audio);
    switch (target.arm) {
    case OverviewCommandTarget::Arm::ZoomOut: {
        // Stamp BEFORE the move, since the move is what makes the view
        // historical. The live fields are the clamped truth, and the level is
        // strictly below the ceiling on this arm.
        OverviewRecall& r = active_view_state(app).overview_recall;
        r.zoom_level             = app.zoom_level;
        r.viewport_start_sample  = app.viewport_start_sample;
        r.playhead_cursor_sample = app.playhead_cursor_sample;
        r.audio_view             = app.active_audio_view;
        viewport.apply_zoom_change(target.level);
        // The whole-song state is raised AFTER the applier, which clears it
        // as every level-moving zoom write does (the inventory is at
        // ViewState::whole_song_visible). While it stands the clamp chokepoint
        // re-derives the level from the live ceiling, so a resize or an S/T
        // flip carries the picture instead of stranding a number.
        active_view_state(app).whole_song_visible = true;
        return;
    }
    case OverviewCommandTarget::Arm::RestoreView: {
        const OverviewRecall r = active_view_state(app).overview_recall;
        const int64_t playhead =
            clamp_playhead_to_live_domain(r.playhead_cursor_sample, app, audio);
        if (playhead != app.playhead_cursor_sample) {
            playback_lifecycle.stop_playback_if_playing();
            if (app.history_mode.active) {
                if (clear_history_mode_focus(app.history_mode)) {
                    viewport.invalidate_all();
                }
            } else if (!app.selected_markers.empty() ||
                       app.last_selected_marker != -1) {
                selection.clear_selection();
                viewport.invalidate_waveform_area();
            }
            viewport.move_playhead_to(playhead);
        }
        active_view_state(app).whole_song_visible = false;
        viewport.apply_zoom_to_start(target.level, r.viewport_start_sample);
        return;
    }
    case OverviewCommandTarget::Arm::NoOp:
        return;
    }
}

namespace {
// WHERE THE SPAN FRAMER WOULD LAND — the level and the unsnapped start
// frame_span_into_view hands apply_zoom_to_start, as a pure question
// (2026-10-25: lifted out of the framer so span_framing_would_act, the Zoom to
// Fit key's frame term, asks the arithmetic the act runs, never a copy of it).
// TWO READERS: frame_span_into_view and span_framing_would_act below. No
// landing (nullopt) where there is nothing to frame on: no piece loaded, a
// zero-width waveform, an empty live domain — the framer's three head returns.
struct SpanFramingLanding {
    double  level = 0.0;
    int64_t start = 0;
};

std::optional<SpanFramingLanding> span_framing_landing(const AppState& app,
                                                       const GuiAudio& audio,
                                                       int64_t lo, int64_t hi,
                                                       bool margin) {
    // The shared span framer's arithmetic (frame_span_into_view, declared in
    // input_handler.h, applies it). Computes the margined
    // fit level, then ALWAYS derives the start by CENTERING the margined span in
    // the PAINTED window at that level (W·q_t, q_t the sixteenth-frame grid step
    // — the span the waveform will show); the start itself is left unsnapped,
    // clamp_viewport_start owning the viewport grid (see the centering block
    // below). A span too small for the floor (effective_min_zoom_level) to fill (the floor-saturated case)
    // rests centered instead of left-aligned, and the unclamped case degenerates
    // to the span's left edge (W·q_t covers the margined span by the fit-level
    // solve, past it by under a sixteenth of a frame per column). Ends at
    // apply_zoom_to_start.
    if (audio.total_frames() <= 0) return std::nullopt;
    const GuiRect area = waveform_area(app);
    const int     W    = area.w;
    if (W <= 0) return std::nullopt;
    const int64_t column = audio.working_column();
    const int64_t total = live_total_frames(app, audio);
    if (total <= 0) return std::nullopt;

    if (hi < lo) std::swap(lo, hi);   // defensive; monotone callers keep order
    double flo = static_cast<double>(lo);
    double fhi = static_cast<double>(hi);
    // THE MARGIN IS THE EDGE MARGIN, A FRACTION OF THE WINDOW PER SIDE
    // (kViewportEdgeMarginFraction, app_state.h, since 2026-09-22 — it padded
    // 2.5 % OF THE SPAN per side until then): the framed span occupies
    // 1 − 2m of the window it is fitted into, so each side pads m / (1 − 2m)
    // of the span. The fit and the centring below then read the padded span
    // unchanged.
    if (margin) {
        const double f = kViewportEdgeMarginFraction;
        const double m = f / (1.0 - 2.0 * f) * static_cast<double>(hi - lo);
        flo -= m;
        fhi += m;
    }

    // Fit level: effective_max_zoom_level's own solve (fit_zoom_level, main.cpp
    // — the level whose painted span W·q covers the span) with the
    // span in place of total, clamped into [effective floor, per-file
    // effective ceiling]. A zoom-OUT ceiling and a zoom-IN floor, so framing a
    // tiny span may go deep (down to effective_min_zoom_level) while a span wider than the song
    // saturates at whole-song-visible — and the whole song's span solves to
    // the ceiling itself.
    double span = fhi - flo;
    if (span < 1.0) span = 1.0;  // guard log2 of <= 0 (degenerate lo == hi)
    const double raw_level = fit_zoom_level(span, W, column);
    const double ceiling = effective_max_zoom_level(W, total, column);
    const double target_level =
        std::clamp(raw_level, effective_min_zoom_level(column), ceiling);

    // CENTER: place the margined span's midpoint at the centre of the PAINTED
    // window at the target level, visible_t = W·q_t (samples_visible's span at
    // that level, the one geometry land_subject's fit test and centring read) —
    // at the fit level and at every whole level (column × 2^(L − 2), a whole
    // number of frames or a half at level 1) q_t is the level's spp itself,
    // and a level clamped to the floor or the ceiling centres on the window it
    // will actually paint. Only the FINAL start is nearbyint'd;
    // the viewport grid is clamp_viewport_start's (the chokepoint). In the
    // ordinary UNCLAMPED fit W·q_t is the margined span rounded UP to the
    // sixteenth-frame grid (under W/16 frames over, fit_zoom_level), so
    // mid - visible_t/2 sits under W/32 frames left of flo — a column or two at
    // the deepest zoom and nothing at a coarse one, inside the edge margin
    // either way — and the start is nearbyint(flo) less that residue. The
    // ceiling-saturated whole-song case (mid = total/2, W·q_t >= total ->
    // start <= 0) is clamped to 0 by clamp_viewport_start's visible >= total
    // branch.
    const double mid       = 0.5 * (flo + fhi);
    const double visible_t =
        painter_quantized_spp(samples_per_pixel_at(target_level, column)) *
        static_cast<double>(W);
    const int64_t target_start =
        static_cast<int64_t>(std::nearbyint(mid - visible_t / 2.0));
    return SpanFramingLanding{target_level, target_start};
}

// THE SPAN SHIFT+0 AND THE TRIM BAR'S DOUBLE-CLICK FRAME — the target of
// run_span_framing_command as a pure question (2026-10-25, lifted beside the
// landing above for the same reader). TWO READERS: run_span_framing_command
// and span_framing_would_act below. No span (nullopt) where the command has
// nothing to dispatch on: no piece, a zero-width waveform, no sample rate, an
// empty live domain — its head returns.
struct TrimFramingSpan {
    int64_t lo     = 0;
    int64_t hi     = 0;
    bool    margin = false;
};

std::optional<TrimFramingSpan> trim_framing_span(const AppState& app,
                                                 const GuiAudio& audio) {
    if (audio.total_frames() <= 0) return std::nullopt;

    const GuiRect area = waveform_area(app);
    const int     W    = area.w;
    const int     sr   = audio.sample_rate();
    if (W <= 0 || sr <= 0) return std::nullopt;
    const int64_t total = live_total_frames(app, audio);
    if (total <= 0) return std::nullopt;

    // The target span in ACTIVE-domain frames [lo, hi], and whether it takes the
    // framing margin (the trim window) or none (the whole song already fills
    // the window at the effective ceiling).
    TrimFramingSpan span{0, total, false};
    // (THE REGION ARM IS GONE, 2026-08-18, and with it the span priority this
    // body used to carry: a live region won over the trim, which was a real
    // choice while the two were separate states. The region IS the trim now,
    // so one arm answers.)
    if (!trim_is_full_window(app.trim, audio.total_frames())) {
        // A proper SUB-WINDOW frames itself. A FULL window is the old unset
        // state — nothing to frame beyond the whole song — so it falls through
        // to the whole-song arm exactly as an unset trim always did (the
        // recognition is the shared owner trim_window_is_full,
        // settings_file.h). The bounds come from compute_trim_samples, which
        // owns the range in source frames. Express both bounds in the ACTIVE
        // domain: source view
        // uses the source frames directly; target view maps each through
        // displayed_or_live_target_map — the same basis the flags and endcaps
        // paint at — which is identity on the empty source-view map, so
        // one call covers both views.
        const std::pair<long long, long long> trim_src =
            compute_trim_samples(app, audio.total_frames());
        const std::vector<WarpFrameMapSegment>& dmap =
            displayed_or_live_target_map(app, audio);
        span.lo = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(static_cast<double>(trim_src.first), dmap)));
        span.hi = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(static_cast<double>(trim_src.second), dmap)));
        if (span.hi < span.lo)
            std::swap(span.lo, span.hi);  // defensive; the monotone map keeps order
        span.margin = true;
    }
    return span;
}
}  // namespace

void frame_span_into_view(AppState& app, const GuiAudio& audio,
                          Viewport& viewport, int64_t lo, int64_t hi,
                          bool margin) {
    // The shared span framer (declared in input_handler.h): the landing above,
    // then level + start through the two clamp chokepoints and the repaint like
    // the other zoom commands; the idempotent current-vs-target no-op lives
    // there (zoom_to_start_would_move). NOT apply_zoom_change (which would
    // recenter on the playhead).
    if (const std::optional<SpanFramingLanding> landing =
            span_framing_landing(app, audio, lo, hi, margin)) {
        viewport.apply_zoom_to_start(landing->level, landing->start);
    }
}

// WOULD SHIFT+0 (FRAME THE TRIM) ACT — the contract and the reader are at the
// declaration (app_state.h). The command's own three steps asked ahead: its
// span, the framer's landing, the applier's idempotence test.
bool span_framing_would_act(const AppState& app, const GuiAudio& audio) {
    const std::optional<TrimFramingSpan> span = trim_framing_span(app, audio);
    if (!span) return false;
    const std::optional<SpanFramingLanding> landing =
        span_framing_landing(app, audio, span->lo, span->hi, span->margin);
    return landing &&
           zoom_to_start_would_move(app, audio, landing->level, landing->start);
}

void GuiInputHandler::run_span_framing_command() {
    // The trim-bar double-click and Shift+0 (since 2026-10-24, Zoom to Fit's
    // shifted press) ZOOM TO A SPAN, their own route beside bare `0`'s
    // whole-piece toggle (run_overview_command) and `c`'s marker-jump working
    // zoom. It only ever FRAMES a span,
    // never the fine working zoom. TWO ARMS since 2026-08-18: a proper trim
    // SUB-WINDOW, else the whole song (full zoom-out — which is also where the
    // FULL trim window lands, it being the whole song). The region arm above
    // them died with the separate region state. The framing is idempotent — a second double-click with
    // the viewport unchanged is a no-op (apply_zoom_to_start's current-vs-target
    // compare, zoom_to_start_would_move — which is also why the Zoom to Fit
    // key greys over a framed full trim, span_framing_would_act), while any
    // pan/zoom between clicks re-frames.
    // The span is trim_framing_span's (above, the one copy the Zoom to Fit
    // key's frame term also reads), framed through the shared framer (the edge
    // margin per side for the trim window, none for the whole song — already
    // whole-song at the effective ceiling, start 0). Centering + the wall
    // clamp make the whole-song arm degenerate to the effective ceiling at
    // start 0.
    if (const std::optional<TrimFramingSpan> span =
            trim_framing_span(app, audio)) {
        frame_span_into_view(app, audio, viewport, span->lo, span->hi,
                             span->margin);
    }
}

// Shared wheel handler. ONE ARM, EXACT-MATCHED (architect approval
// 2026-09-14): THE PLAIN WHEEL IS THE STEPPED PAN, and every modified wheel is
// a swallowed no-op.
//
// THE STEPPED PAN: the samples_visible / kViewportPanStepDivisor stride through
// the scroll_viewport funnel, whose clamp puts out the hold posture and
// suspends a following play's paging,
// over the waveform and the top strip alike (every context
// id, one route; the two bools below say only "a wheel-live surface"). up =
// earlier, down = later. HISTORY OF THE SPELLING: plain from 2026-08-12 (the
// eighth glass ruling moved the pan onto the bare form), on ALT from
// 2026-08-27 while the plain wheel stepped the waveform magnification, and
// plain again since 2026-09-14, when that setting retired — the picture's gain
// varies over source time now — with the ALT
// FORM DELETED. CTRL+WHEEL, the zoom step since 2026-08-12, WAS DELETED THE
// SAME DAY, and the Viewport's coalesced zoom-steps body with it (its one
// caller): the zoom gestures are the ctrl-DRAG's zoom phase and the pinch,
// its keys `=` / `-` (the zoom step, back 2026-10-27, one level bare and
// two shifted), `0` and `c`.
//
// Every OTHER combination is a swallowed no-op (strict modifier validation):
// ctrl, alt, shift and every mixed chord alike. Pure viewport move otherwise:
// no playhead write, no selection change, no playback stop,
// read-only-legal.
void GuiInputHandler::handle_wheel(GuiMouseButton button, int count,
                                   bool ctrl, bool shift, bool alt,
                                   bool inside_waveform, bool inside_top) {
    if (!inside_waveform && !inside_top) return;
    // `count` is the net detent count coalesced for this pointer frame
    // (always >= 1 from the platform). The arm scales its per-step quantity
    // by that count and applies it in ONE call, so the damage /
    // hover / worker-kick path fires once per frame regardless of burst size.
    // count == 1 reproduces the single-detent behavior.
    if (count < 1) count = 1;
    if (ctrl || shift || alt) return;
    const int64_t step = std::max<int64_t>(
        1, samples_visible(app, audio) / kViewportPanStepDivisor);
    viewport.scroll_viewport((button == GuiMouseButton::WheelUp ? -step : +step) * count);
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
    // EVERY EDITOR SWALLOWS THE WHEEL (architect 2026-10-25: an act that
    // takes the keyboard is a modal, and "even the waveform stops the pan and
    // drag") — keyboard_modal_editor_active, the one predicate for a
    // keyboard-taking modal, whose membership this line restates none of.
    // (The lane editors — the flag editor and the iteration bound editor —
    // let the wheel pan through them from 2026-07-28, the wheel being
    // navigation and not a chord, and a wheel over a flag cell closed one
    // uncommitted and stepped the cell; reversed with their
    // pointer-transparency, so the wheel is the dialog editors' -1 under
    // every editor.)
    //
    // The wheel routes by area — the waveform and the top strip — plus the ONE
    // row-wise carve-out below, the redesigned rows' inert band. BOTH
    // take the same one route since 2026-08-12, and the wheel's one arm rides
    // it (the plain stepped pan, every modifier refused inside handle_wheel and
    // never here), so the context ids
    // differ only for the platform's
    // sub-detent remainder attribution, harmlessly. Fewer regions is
    // strictly safer for the accumulator, and the inert band is the safest kind:
    // it emits nothing at all. THE TWO BLANK BANDS SPLIT (the relayout's commit
    // B, whose two flexible gaps center the waveform): GAP 2, between the
    // waveform and the bottom row, needs no band of its own — it lies below
    // every area this probe tests, so a wheel there falls to the no-context 0
    // exactly as the old blank foot's did — while GAP 1, BETWEEN THE ICON ROW
    // AND THE TRIM LANE (main.cpp's vertical rule is the owner), lies INSIDE
    // top_strip_area and
    // therefore JOINS THE INERT BAND LIST below, that band being no more a
    // panning surface under the toolbar than the blank ground at the
    // window's foot.
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
    // A NOTIFICATION CARD SWALLOWS THE WHEEL (2026-08-29), first and above
    // every veil below exactly as its press claim ranks above them: a card is
    // opaque to the pointer and it is not a scroll surface, so a detent over
    // one must neither reach the waveform under it nor scroll the band it may
    // overlap. -1 rather than 0 for the inert bands' own reason — the
    // platform grows no sub-detent remainder in a context that can fire
    // nothing.
    if (notification_card_at(app, x, y) != 0) return -1;
    if (app.prompt.active) return -1;
    // THE PROGRESS MODAL SWALLOWS THE WHEEL (2026-10-14): its box covers the
    // canvas column and nothing under it scrolls; the touch navigation
    // refuses under it too (apply_touch_nav_update's state list).
    if (app.batch_progress.active) return -1;
    // (AN OPEN DROPDOWN SWALLOWED THE WHEEL here until the Settings popup
    // retired, 2026-10-21.)
    // THE LIVE-GESTURE REFUSAL IS RANKED ABOVE THE OVERLAY (2026-08-28), not
    // below it as it was when the player's context landed: the overlay's row
    // press and the scrub's marker drag are members of the predicate now, and
    // the overlay's own context is the one wheel arm a live gesture could
    // otherwise have moved — a detent under a held row press would scroll the
    // list out from under the arm the lift re-hits. Its rank against the
    // editor and loading tests below is free (all three refuse).
    if (any_pointer_gesture_active(app)) return -1;
    // THE FOLDER OVERLAY'S WHEEL (2026-08-28): live over the BAND alone —
    // context 4, the list's one-row-per-detent scroll — and swallowed
    // everywhere else, the veil's own answer for the wheel under the player
    // and the pickers alike (neither is an editor, so the swallow below never
    // sees them — this line is their whole wheel answer). The band is the
    // keyboard slot's rect; the standing predicate is the owner tag.
    if (folder_overlay_stands(app)) {
        return rect_contains(folder_overlay::surface_rect(app), x, y) ? 4 : -1;
    }
    // THE SCORE VIEWER SWALLOWS THE WHEEL everywhere (2026-10-26): nothing
    // under its box scrolls, and the page takes no hidden gesture.
    if (score_viewer_stands(app)) return -1;
    // THE ON-SCREEN KEYBOARD SWALLOWS THE WHEEL (2026-10-10, ON SCREEN IS AS
    // PAINTED): its band (onscreen_keyboard.h's placement block; with the
    // overlay's line and a dialog standing on it until 2026-10-14) is
    // opaque, not a scroll surface, and what lies under it is covered, so a
    // detent over its painted rect reaches neither the waveform nor row 8
    // behind it. -1 for the inert bands' reason. (A running touch gesture
    // that crosses the keys navigates on: its surface was decided at its
    // start, apply_touch_nav_update.) Under any editor the line below
    // swallows everything, the modal family's box over the canvas with it.
    if (rect_contains(onscreen_keyboard::standing_rect(app, gui), x, y))
        return -1;
    if (keyboard_modal_editor_active()) return -1;
    if (app.loading || audio.total_frames() <= 0) return -1;

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
            // (The menu row's band left with its lane, 2026-10-21.)
            // (The program frame's top row's band left with its lane,
            // 2026-10-22.)
            // (The caption's band left with its lane, 2026-10-24.)
            top_icon_row_area(app),
            // (The tab strip's band, 2026-10-18 to 2026-10-22, left with the
            // strip.)
            // GAP 1's band, the ONE non-lane member (between the band and
            // the view bar, window ground): it sits inside the
            // top-strip area below, which
            // pans, so without this entry a wheel over that ground would
            // scroll the song — exactly the fault row 1's ruling named. Not
            // a redesigned row, but the same answer for the same reason, and
            // the reason it needs a rect at all is at top_flex_gap_area
            // (app_state.h).
            top_flex_gap_area(app),
            // The bottom row joined the family's inert band list 2026-08-11
            // (as the transport row), exactly as the rule above promises a
            // future row would, and the 2026-08-12 unification widened its
            // band to the whole merged lane — the clock cell and the modal
            // that displaces it are as wheel-inert as the buttons. It is a bottom-strip
            // lane, so the top-strip area test below would never have routed it
            // anyway; membership here is what stops the sub-detent accumulator
            // growing remainder over it, like its three siblings.
            bottom_row_area(app),
        };
        for (const GuiRect& b : bands) {
            if (rect_contains(b, x, y)) return -1;
        }
    }

    // THE WAVEFORM WHERE IT IS PAINTED (2026-10-10): waveform_paint_area, the
    // press road's own navigation surface (point_on_nav_surface,
    // input_pointer.cpp) — the overlay's rows over the canvas answered -1
    // above, and this reads the same one geometry rather than the whole
    // canvas interior — LESS THE STEREO DIVIDER'S ROWS (architect
    // 2026-10-16, point_on_waveform_divider, render.h): chrome, so a wheel
    // there falls to the no-context 0 as on the frame's columns beside the
    // waveform (a running touch gesture navigates on across them, its
    // surface decided at its start, apply_touch_nav_update).
    // THE TOP STRIP across the program lanes' columns, read off the trim
    // lane's rect (the window's width since 2026-10-21; the program frame's
    // top row, inert ground inside the strip and answering the no-context 0,
    // left with its lane 2026-10-22).
    const GuiRect area  = onscreen_keyboard::waveform_paint_area(app, gui);
    const GuiRect top   = top_strip_area(app);
    const GuiRect lanes = top_trim_row_area(app);
    const bool inside_waveform =
        rect_contains(area, x, y) &&
        !point_on_waveform_divider(waveform_area(app), y);
    const bool inside_top      = rect_contains(top, x, y) &&
                                 x >= lanes.x && x < lanes.x + lanes.w;
    if (inside_waveform) return 1;
    // A FLAG CELL (context 5, architect 2026-09-14): the plain wheel over a
    // flag of the LIVE marker lane selects that flag and steps that cell
    // (run_flag_cell_wheel), so it gets its OWN id — the platform's sub-detent
    // remainder is keyed by this answer, and a remainder grown over the lane's
    // ground must not complete a detent over a flag or the other way round.
    // THE `h` VIEW IS EXCLUDED — its flags are the mode's diff flags and a
    // wheel there stays the plain stepped pan. No editor stands here (the
    // swallow above, 2026-10-25). THE TOUCH
    // ROAD NEVER READS THIS ANSWER: apply_touch_nav_update asks state alone,
    // so a nav frame over a flag navigates as it does anywhere else.
    if (inside_top && !app.history_mode.active &&
        hit_test_flag(app, audio, x, y) >= 0)
        return 5;
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
    // THE A/B AUDITION IS HANDS-OFF (architect 2026-10-28; the rule at
    // end_audition_for_press's declaration): a wheel frame ends a standing
    // act ahead of the context test, wherever it lands — the zoom and the
    // stepped pan it drives no longer leave the act standing.
    end_audition_for_press();
    // A WHEEL DISMISSES THE TOOLTIP, before the context test so the
    // dismissal happens on the very frame the wheel arrives. (It closed the
    // Settings dropdown too, until the popup retired, 2026-10-21.)
    hide_shift_tooltip();
    const int ctx = wheel_context(x, y);
    if (ctx < 0) return;
    // ctx 4 — THE FOLDER OVERLAY (2026-08-28): one row per detent on the PLAIN
    // wheel, the wheel's direction the list's (down = later rows) — the band
    // scrolls and nothing else moves, whichever content fills it. The whole
    // wheel vocabulary under either owner is this arm; every other position is
    // swallowed at the context. The mechanics are the widget's and the damage
    // this file's, the band being the only thing that moved.
    //
    // EVERY MODIFIED WHEEL IS A SWALLOWED NO-OP HERE (2026-08-29), strict
    // modifier validation's own rule, which this arm read as "every modifier
    // ignored" until that date and so let Ctrl / Alt / Shift + wheel scroll
    // the list. The band binds one wheel act, so the other three chords bind
    // nothing and must not fall through to a bare one — and none of them may
    // reach the surfaces BELOW the veil either, which is why the refusal is a
    // return rather than a fall-through.
    if (ctx == 4) {
        if (mods.ctrl || mods.shift || mods.alt) return;
        const int rows = dir == GuiMouseButton::WheelDown ? count : -count;
        if (folder_overlay::scroll_rows(app, rows)) {
            viewport.invalidate_rect(folder_overlay::surface_rect(app));
        }
        return;
    }
    // (ctx 6, A SCROLLING POPUP LIST's wheel — the settings choice editor's
    // list, kPopupWheelRows a notch — stood 2026-10-08 to 2026-10-14 and went
    // with the list.)
    // ctx 5 — A FLAG CELL (architect 2026-09-14): the PLAIN wheel selects the
    // flag under the pointer and steps that cell (run_flag_cell_wheel); every
    // MODIFIED wheel is a swallowed no-op here as on the other surfaces.
    if (ctx == 5) {
        if (mods.ctrl || mods.shift || mods.alt) return;
        run_flag_cell_wheel(dir, count, x, y);
        return;
    }
    // ctx: 1 waveform, 2 the top strip. Both take
    // the same one-arm vocabulary — plain = the stepped pan, every modified
    // wheel a swallowed no-op (architect approval 2026-09-14). THE CONTEXT
    // ANSWER
    // IS MODIFIER-INDEPENDENT by construction (wheel_context takes only x/y and
    // reads no modifier state), so a modifier cannot change WHERE the wheel is
    // live, only whether it acts there.
    handle_wheel(dir, count, mods.ctrl, mods.shift, mods.alt,
                 ctx == 1, ctx == 2);
}

bool GuiInputHandler::apply_editor_clipboard(
        text_editor::KeyAction action, text_editor::State& s) {
    switch (action) {
        // THE COPY IGNORES THE VERDICT AND THE CUT OBEYS IT, and the two
        // are one rule rather than an inconsistency (2026-09-03).
        //
        // A FAILED COPY CHANGES NOTHING ON SCREEN, so it says nothing: the
        // editor is its own world (notifications.h), the text it did not take is
        // still sitting there selected, and the paste that follows shows the
        // truth the moment it is asked for. The CARDED copy is the one whose
        // result nothing paints at all (the main window's Ctrl+Shift+J, the
        // resolved value's copy).
        //
        // A FAILED CUT IS DIFFERENT IN KIND, because a cut is a copy AND a
        // deletion: erasing on a refused claim would destroy the bytes with
        // nowhere to paste them from — the clipboard did not take them and,
        // uniquely among the editors' acts, there is no undo inside an editor
        // to get them back. SO THE ERASE HANGS ON A TRUE VERDICT: on false the
        // selection stays exactly as it was and the press CARDS, because a
        // refusal that leaves the screen unchanged must say why the key did
        // nothing (the sentence's composer is card_clipboard_refusal,
        // input_key_dispatch.cpp; a card stacks over an editor as over
        // anything else — the flag editor is keyboard-modal and stops
        // nothing).
        //
        // WHY THE VERDICT IS THE RIGHT TEST ON BOTH BACKENDS. False means no
        // claim went out, and a refused claim does not leave the bytes
        // readable: Wayland writes clipboard_send_text_ before it claims, but
        // clipboard_get_text hands that payload back only while
        // clipboard_we_own_ stands, and a refusal never raises that bit (the
        // failed-source arm clears it), so the paste falls through to whatever
        // another application is offering. Android reads the system store
        // outright once its road is open; clipboard_text_ is the ROAD-ABSENT
        // fallback alone, and the road-absent arm answers false as well — the
        // one case where a refusal DOES leave the text pastable in-app.
        // THE ONE OTHER SUB-CASE where a false verdict still leaves the bytes
        // reachable is a Wayland refusal UNDER A CLAIM WE ALREADY HOLD (an
        // earlier copy's source still answers `send` from the updated store).
        // Both are refused all the same: the verdict is the one thing this
        // side can ask, and over a DELETION the conservative answer — keep the
        // text, say so — is the right one to be wrong with.
        case text_editor::KeyAction::CopyRequested:
            (void)gui.clipboard_set_text(text_editor::selected_text(s));
            return true;
        case text_editor::KeyAction::CutRequested:
            if (!gui.clipboard_set_text(text_editor::selected_text(s))) {
                card_clipboard_refusal(notifications, "cut");
                return true;
            }
            // An empty insert can only shrink the buffer, so this call has no
            // refusal to report (the cap's rule is at replace_selection).
            (void)text_editor::replace_selection(s, std::string());
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
            //
            // AND A PASTE TOO LONG FOR THE FIELD SAYS SO (architect
            // 2026-08-30): the filter refuses the operation whole, leaving the
            // field as it stood, and the card says why. The
            // sentence is here because this is the paste's one site — the
            // keyboard's own over-capacity refusal reports through
            // KeyAction::OverCapacity and cards at route_modal_editor_key.
            std::string text = gui.clipboard_get_text();
            if (text.empty()) return true;
            if (!text_editor::replace_selection(s, text)) {
                notifications.notify(AppState::NotificationClass::Normal,
                                     "The pasted text is too long for this "
                                     "field");
            }
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

// THE FLIP, one line over the set-to form below — ONE CALLER since bare `t`
// was deleted on 2026-09-15, the settings editor's `active_audio_view=`
// commit. Every invariant the switch owns lives in
// the one body; the contract for both spellings is at the declaration
// (input_handler.h).
void GuiInputHandler::handle_active_audio_view_toggle() {
    switch_active_audio_view_to(app.active_audio_view == 'S' ? 'T' : 'S');
}

void GuiInputHandler::switch_active_audio_view_to(char target_view) {
    // Audio must be loaded — the switch is a silent no-op in blank state.
    // (The blank/loading guard near the top of on_key already covers
    // this, but the helper is defensive in case future callers reach
    // it from elsewhere.)
    if (audio.total_frames() <= 0) return;

    // THE SAME-VIEW NO-OP, the set-to form's own first line: naming the view you
    // are already in changes nothing — no stop, no translation, no editor
    // teardown, no kick. It is what lets a caller that names a COMBINATION (the
    // four view selectors, the undo restore) hand this axis its tag unconditionally
    // instead of spelling the guard by hand. The flip above can never reach it.
    if (target_view != 'S' && target_view != 'T') return;
    if (target_view == app.active_audio_view) return;

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
    // identically, AND SINCE 2026-08-30 THEY ALSO REFUSE ALIKE: a refusal
    // leaves the view in source and says one stderr line with the builder's
    // own words, and nothing appears on screen. The predicate's declaration
    // (input_handler.h) owns the ruling and its reason — the refusals are
    // unreachable from program-written input, so there is no on-screen
    // surface for them by ruling.
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
        // THE SILENT REFUSAL, the load road's own (file_loader.cpp): stay in
        // source view and name the cause on stderr, one line, the same
        // sentence at both entry surfaces.
        std::fprintf(stderr,
                     "warptempo_gui: Target view entry refused: %s\n",
                     entry.error().c_str());
        return;
    }

    // THE COLUMN IS NOT THIS AXIS'S BUSINESS: both columns pair with both
    // audio views (architect 2026-10-12, S+P's return — from 2026-09-21 until
    // then this body landed T+P on W before it left for source), so the
    // switch carries the phase-reset column across S <-> T exactly as it
    // carries the warp column, selection and all (the group-carry block
    // below).

    // THE HISTORY MODE'S OWN FOCUS CLEARS ON THIS SWITCH, exactly as it clears
    // on a `,` / `.` step and for the identical reason — it is an ordinal into
    // the PAINTED diff-flag list, and the switch is about to rebuild that list
    // (the contract, and the full clearer list, are at AppState::HistoryMode::
    // focus). This axis's clear lives HERE, at the S/T chokepoint, so the bare
    // four view selectors and every other caller of this chokepoint inherit it
    // with no second route; the column axis clears at its own switch. Placed
    // BELOW the refusal above (a switch that never
    // happened must not clear anything) and far above the kick at the tail,
    // which is what rebuilds the flag cache — `focus` is one of that cache's
    // fingerprint fields, so it has to be settled before the rebuild reads it.
    // Unconditional: with the mode down the pair already rests empty, the
    // whole-struct reset at close_history_mode having put it there. It goes
    // through the ONE clearer, which takes the mode's multi-selection with the
    // focus for the identical reason (clear_history_mode_focus, app_state.h).
    clear_history_mode_focus(app.history_mode);

    // A LIVE TOP-STRIP EDITOR CLOSES ON EVERY S<->T FLIP, WITHOUT COMMITTING
    // (the exact Esc teardown), AND THE CLOSE IS SYMMETRIC SINCE 2026-08-24.
    // It was the S -> T half's alone while the flag editor was a source-only
    // surface, and the reason was that the flip would strand a live one on a
    // now-refusing surface. That reason is gone — the payload editor is legal
    // in both audio views now (the fifth ruled exception to the home-view
    // binding, active_column_authoring_allowed, app_state.h) — and the
    // surviving one is symmetric by nature: the flip RE-EXPRESSES the marker
    // lane in the other domain, and this editor is the flag UNROLLED from its
    // marker's painted column with the playhead landed there, so a survivor
    // would have to be re-derived in the new domain. Closing it is simpler
    // than re-landing it, and it is what the flip already does to everything
    // else around the caret. Guarded internally on an active editor, so this
    // is a no-op when none is open. Only `t` and the settings-editor
    // `active_audio_view=` commit route here — Ctrl+Tab never changes
    // active_audio_view — so this is the one place either edge is handled.
    // THE CLOSE IS KIND-AGNOSTIC: every top-strip editor kind tears down on
    // T->S (architect 2026-08-25: "we want symmetry as much as possible").
    flag_editor.exit_top_flag_edit_no_commit();

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
    // mode-off-in-target is no longer an invariant anywhere. BRACKET AUTHORING
    // FOLLOWED ON 2026-08-24: it was source-only only because the flag editor
    // was, and the editor is legal in both audio views now (the home-view
    // binding's fifth ruled exception, active_column_authoring_allowed,
    // app_state.h), so the iter grammar is typed wherever the editor opens.
    // The editor teardown one line above still runs, symmetric now and for a
    // different reason. SINCE 2026-09-13 THE MODE NEVER CROSSES THIS EDGE AT
    // ALL: it lives in target view alone, bare `i` crossing S -> T through
    // this very chokepoint before it lights, and every caller that could name
    // 'S' under a lit lamp refusing ahead of it (bare 1 and its three fellow
    // selectors at the iteration lock's gate, the settings editor's typed
    // `active_audio_view=S`, and the undo/redo restore, which the lock
    // refuses whole) — the invariant is at AppState::iteration_mode_enabled,
    // and this body takes no term for it.)

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

    // (THE S/T SWITCH'S OVERLAY HIDE IS DELETED, architect 2026-08-19, with the
    // A/B tab switch's twin: the trim is the same per-tab pair in both audio
    // views, so a domain flip has nothing to put away.)
    // THE SEATED PINCH'S ANCHOR IS CLEARED HERE, a flip making it a stale
    // song frame: TouchNavZoomState::anchor_sample is an ACTIVE-DOMAIN
    // song frame, and nothing about two fingers resting on the glass stops a
    // keyboard `t` or a mouse click on the audio-view lamp from reaching here — so a
    // pinch held across this flip would go on zooming about a SOURCE frame read
    // as a TARGET one, seating the view on an unrelated song point. THIS IS THE
    // CORRECTNESS member of the rule: it is the one view write that changes what
    // the stored number MEANS. It sits here, beside the two assignments above,
    // because the clear rides the WRITES of the active view
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
    // seats that cursor where the readout says it is. The trim bar paints
    // from the trim's source-frame bounds every frame, crossing them into
    // whichever domain is live, so the flip owes it nothing.
    // EVERY CALLER OF THIS CHOKEPOINT GETS IT, re-greped 2026-10-14: the
    // absolute Source / Target selectors on bare 1 / 2 (and the icon row's
    // Source / Target pair, which synthesizes them),
    // the settings editor's `active_audio_view=` GUI-key twin, bare `i`'s own
    // crossing into target, and the undo/redo restore's audio-view tag.
    // (Shift+S called it until 2026-10-12, when the crossing chord learned to
    // stay in its audio view, and the phase-reset propagate paste's landing
    // tail until 2026-10-14, when the paste learned the same.) A carried
    // PHASE-RESET selection rides it exactly as a
    // warp one does, both columns pairing with both views since that day.

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
    // THE FLIP PUTS THE HOLD OUT HERE AND DERIVES NOTHING (architect
    // 2026-09-29): an S/T flip is always a changed camera at the chokepoint,
    // and unlike the tab switch it does not re-derive the bit on arrival —
    // the anchored start's re-snap can move a centred rest by one column, so
    // the flip cannot meet the exact test (hold_derived_on_arrival,
    // viewport.cpp). Bare 1/2/3/4 across S and T therefore drop the hold; the
    // W/P column switch moves no camera and keeps it.
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
    // THROUGH THE RESEAT, NOT THE LAND (2026-08-19): a TRANSLATION IS NOT A
    // MOVEMENT — the flip maps the same musical instant into the other domain,
    // the closest it can come to not moving the playhead at all, so it must not
    // end an A/B audition as a movement would. The land owner ends it;
    // reseat_playhead_on_marker is the identical lookup and write without it
    // (the rule and the whole exemption set are at Viewport::move_playhead_to,
    // viewport.cpp).
    if (!app.selected_markers.empty() && app.last_selected_marker >= 0)
        reseat_playhead_on_marker(app, audio, viewport, app.last_selected_marker);
    // One-shot discrete jump with a domain change: is_target, the viewport, and
    // the warp_frame_map hash all flip, so the displayed plate must change. Render it
    // synchronously and publish the displayed fingerprint now, so the
    // icon row's lit S/T pair and the playhead column do not repaint a frame
    // ahead of the deformed waveform. The plate is built from source audio plus
    // the live warp_frame_map, independent of the target render buffer, so this is
    // unaffected by the ensure_ready / rebind_to_source below.
    viewport.kick_waveform_sync();
    gui.invalidate_region(0, 0, app.width, app.height);

    // THIS FORK IS THE RENDER PLAYER'S CLOSE RE-EXPRESS TOO (2026-08-28):
    // GuiRenderPlayer::close takes the same two-arm fork verbatim to hand
    // the engine back to the VIEW's buffer after the player's item, so a
    // change here is a change there.
    if (going_to_target) {
        // S → T: ensure playback is bound to a current target buffer.
        // ensure_ready short-circuits to a clean rebind if no edits
        // have invalidated the cached buffer since the last successful
        // render; waits when a dispatch already stands for the current
        // dirty generation (its completion rebinds — the generation guard
        // at GuiTargetRender, 2026-09-02); otherwise it falls through to
        // trigger()'s cancel-clear-dispatch sequence so a fresh render
        // runs against the current engine input.
        target_render.ensure_ready();
    } else {
        // T → S: cancel any in-flight target render and rebind
        // playback to source.wav. No replacement dispatch — source
        // view's playback reads source.wav across archival renders.
        target_render.rebind_to_source();
    }
}

// SHIFT+S — DROP A PHASE RESET FROM THE WARP COLUMN, STAYING IN THE AUDIO
// VIEW (architect 2026-10-12; born 2026-08-28 as the trip to T+P from any
// view). The contract is at the declaration (input_handler.h); what lives
// here is the order and why each step is where it is.
//
// THE COLUMN SWITCH FIRST AND THE DROP LAST: switch_active_markers_view_to('P')
// is the W/P WRITER rather than the column entry, the propagate paste's reason
// exactly — the writer's own selection CLEAR must land before the drop, and the
// entry's coincidence auto-select would only be overwritten by the drop's
// single-select one line later. Which is also the whole answer to "does the
// new reset end up selected": the drop single-selects what it creates and
// re-seats the playhead onto it, and nothing after it touches either. THE
// AUDIO VIEW IS NOT TOUCHED (architect 2026-10-12, "source stays source,
// target stays target"): S+W lands in S+P and T+W in T+P, and the one drop
// body (drop_phase_reset_lead_in_at_playhead, phaseresetmarkers_ops.cpp)
// takes the kN/2 OUTPUT-sample lead-in in either view — through the live map
// in source view, one calculation at the drop (drop_at_playhead_source_frame)
// — so the reset lands where Shift+S's old trip to target would have put it,
// with no domain flip under the waveform the reader is holding steady.
//
//   * ONE UNDO ENTRY, and it is the DROP'S OWN push — this body adds none.
//     The entry's three view tags are therefore the standing audio view / P /
//     the standing tab, the view the act LANDED in, which is where Ctrl+Z
//     lands the reader (the restore's contract, the landing-view rule with
//     it, is at UndoEntry, app_state.h). The column he pressed the key IN is
//     not recoverable from a phase-reset entry — op_mode is that entry's KIND
//     and its landing_column its column tag — and the whole act is one press
//     either way.
//   * THE KICK IS LAST, after the drop: the column swap moves a flag-cache
//     FINGERPRINT field, and this kick lands the plate, the column and the
//     new selection's flag in the same frame (the paste's reason,
//     land_paste_in_phase_reset_view — the paste stays in its audio view
//     too since 2026-10-14, this act's symmetry).
void GuiInputHandler::drop_phase_reset_crossing() {
    // The blank/loading guard near the top of on_key already covers this; the
    // helper is defensive for the same reason the S/T chokepoint's own copy is
    // — nothing below it should run against an empty session.
    if (app.loading || audio.total_frames() <= 0) return;
    // ALREADY CROSSED, NOTHING TO DO (architect 2026-08-30): the chord IS
    // the crossing from the warp column, so with the P column standing — S+P
    // or T+P — the act refuses WHOLE, before any switch, and says so. Bare `s`
    // is untouched (in the P column it is the drop). The Drop button's shift
    // lift and the glass long press synthesize this same chord and meet this
    // same refusal; its face NEVER greys past the lock (one form is always
    // live, the twin rule's arm at redesign_button_enabled), and its
    // tooltip's shift line drops exactly where this refuses (the stateful
    // overload, 2026-09-01).
    if (!phase_reset_drop_crossing_actionable(app)) {
        notifications.notify(AppState::NotificationClass::Normal,
                             "Already in phase reset view");
        return;
    }
    active_views.switch_active_markers_view_to('P');
    // The drop's own refusals — no sample rate, a frame past the EOF wall,
    // silent; a lead-in frame past the painted edge at this zoom, carded
    // (kMarkerDropOffEdgeCard, asked in the P column after the switch, the
    // geometry the reset would paint in) — leave the view where the switch
    // put it: the act asked for the P column and got there, which is honest
    // even when the reset could not be placed.
    card_op_refusal(notifications,
                    phase_resets.drop_phase_reset_lead_in_at_playhead());
    viewport.kick_waveform_sync();
}

void GuiInputHandler::apply_gui_scale(int percent) {
    // THE LIVE ROAD for the product's one scale axis: the value feeds main.cpp's
    // per-lane height table and every other painted dimension, so a change moves
    // every strip boundary and the waveform area with them. Five steps —
    // assign, PERSIST, push, full
    // invalidate, resize-path geometry-and-cache rebuild — so the new layout is
    // on screen at the commit rather than at the next restart. The sole caller
    // (the settings editor's `gui_scale=` commit) gates the no-op case.
    //
    // THE PERSIST IS PART OF THE COMMIT since 2026-08-27: the scale is a
    // per-DEVICE preference now (device_config.h) and Ctrl+S does not carry it,
    // so the only moment it can be written is the moment it changes. The write
    // is advisory — the live value stands either way — and SAID since
    // 2026-09-02: the writer composes the failure's two clauses at its one
    // failure point (GuiFailure, failure.h), this site prints the diagnostic
    // and cards the display. It goes through THE LIVE CONFIG
    // (AppState::device_config, the loop's one struct): the file holds every
    // key of kDeviceConfigKeys (device_config.cpp) and is rewritten whole, and
    // that struct is the one place all of them are live at once, across every
    // reopen (the ownership rule is at write_device_config, device_config.h).
    app.gui_scale = percent;
    app.device_config->gui_scale = percent;
    if (auto failure = write_device_config(*app.device_config)) {
        std::fprintf(stderr, "warptempo_gui: %s\n",
                     failure->diagnostic.c_str());
        notifications.notify(AppState::NotificationClass::Normal,
                             failure->display);
    }
    set_gui_scale_percent(percent);
    // THE ICON RASTERS ARE PER DEVICE PX (icons.h): the cache drops and
    // refills lazily at the new scale's sizes.
    icons::drop_rasters();
    // THE INPUT CORE'S TOUCH SLOP IS A SCALED LENGTH TOO, and the core sits
    // below the GUI model and cannot resolve it (the contract and the
    // two-call-site inventory are at GuiInputCore::set_touch_slop_px). This is
    // the LIVE road — the settings editor's `gui_scale=` commit; gui_main's
    // startup read of the device config is the other. The push must follow
    // set_gui_scale_percent:
    // drag_moved_threshold_px() reads the value that call just installed.
    gui.set_touch_slop_px(drag_moved_threshold_px());
    // The stillness radius rides the same road (set_touch_stillness_px).
    gui.set_touch_stillness_px(scaled_px(kDefaultTouchStillnessPx, 1));
    viewport.invalidate_all();
    paint_handler.on_resize(app.width, app.height);
    // THE MATERIALS AT THE NEW SCALE, AT ONCE (materials.h's BAKE RULE: one
    // act, one bake — the settle gate is a frame drag's alone).
    materials::ensure(app);
}

void GuiInputHandler::apply_true_colors(bool on) {
    // The apply shape is at install_true_colors' declaration (render.h).
    const WaveformPlateInks before = waveform_plate_inks();
    install_true_colors(on);
    icons::drop_rasters();
    // THE MATERIALS HOLD THE WINDOW'S BYTES (materials.h's BAKE RULE): baked
    // again now, before the flag cache's boxes are refreshed from them.
    materials::ensure(app);
    if (waveform_plate_inks() != before) viewport.kick_waveform_sync();
    else                                 viewport.refresh_flag_cache();
    viewport.invalidate_all();
}

void GuiInputHandler::set_tab_read_only(char tab_view, bool value) {
    // The contract — sole writer, band resolved by tab, damage only when the
    // named band is the active one, history-less — is at the declaration
    // (input_handler.h). The two callers are the bare-`o` arm above, which the
    // icon row's Read-Only button reaches by synthesizing that press, and the
    // settings editor's per-tab read_only commit.
    ViewState& band = (tab_view == 'B') ? app.tab_b : app.tab_a;
    if (band.read_only == value) return;
    band.read_only = value;
    if (tab_view == app.active_tab_view) viewport.invalidate_top_strip();
}

void GuiInputHandler::set_show_waveform_magnification(bool desired) {
    // The contract — sole writer, per-project, history-less, the gain kick —
    // is at the declaration (input_handler.h). The ONE caller is the
    // bare-backtick arm, which the icon row's button reaches by synthesizing
    // that press.
    const uint64_t prior_gain_hash = viewport.waveform_gain_hash();
    app.show_waveform_magnification = desired;
    viewport.kick_waveform_sync_if_gain_changed(prior_gain_hash);
}

void GuiInputHandler::set_show_tooltips(bool desired) {
    // The contract — sole writer, per-project, history-less, the dark edge
    // ending the model whole — is at the declaration (input_handler.h), and
    // so are its callers: the main window's bare-backslash arm (which the
    // icon row's button reaches by synthesizing that press) and the folder
    // overlay's two routers' own backslash arms.
    app.show_tooltips = desired;
    if (desired) return;
    end_tooltip_hover(TooltipHoverEnd::Hard);
}
