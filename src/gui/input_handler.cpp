#include "input_handler.h"

#include "engine/engine_geometry.h"  // kN
#include "gui_display_context.h"
#include "paint_handler.h"
#include "render.h"
#include "settings_io.h"
#include "text_editor.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warpmarkers.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Keyboard input handler event entry points (on_key, the pointer handlers,
// on_wheel), dispatching into the operation structs through the reference
// members (warpops, phase_resets, flag_editor, renders_dir, active_views,
// playback_lifecycle, save_ops, prompt, selection, undo, viewport).
// compute_base_tempo_scale + BaseTempoScale live in input_handler.h so
// this TU can reach them; render_bpm_sweep() is the sole caller.

void GuiInputHandler::on_key(GuiKey key, GuiInputState mods) {
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
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Transient bottom-strip status message clears on every real
    // keypress, including the press that may set a new message later
    // in this same on_key call (the handler that sets it does so at
    // the very end of its branch, after this clear). Guarded so the
    // bottom-strip invalidate fires only when there was a message to
    // erase. See AppState::transient_status_message.
    if (!app.transient_status_message.empty()) {
        app.transient_status_message.clear();
        viewport.invalidate_timestamp_area();
    }

    // Bottom-strip prompt owns input while active. Only the prompt's
    // own response keys do anything; everything else is swallowed so
    // marker edits / playback / viewport keys cannot sneak in while
    // the prompt is up. Delete and Escape map to sentinel chars '\x7f' and
    // '\x1b' so they participate in the same vector<char> match as letter
    // responses.
    // EVERY response — letters, Delete, Escape alike — matches BARE ONLY
    // (architect 2026-07-28): no ctrl, no alt, and no shift. That is what stops
    // Ctrl+S from picking `[s]ave` in the close prompt, Alt+Y from applying a
    // confirmed paste, and Ctrl+O from acknowledging the render-environment
    // prompt.
    // CASE-SENSITIVITY IS THE CODEPOINT'S JOB, NOT !shift's (architect 2026-07-30):
    // the platform case-folds letter keysyms, so the GuiKey says `y` for every way
    // of typing a Y, and the old `!shift` spelling let CAPSLOCK deliver a
    // visually-uppercase Y that still answered `[y]es` — the exact outcome the
    // case-sensitivity was there to forbid. `mods.codepoint` is the true character
    // under the live keyboard state (xkb_state_key_get_utf32 at the platform
    // boundary, shift AND lock applied), so the letter arm reads THAT: a capital Y
    // never matches a lowercase response key, however it was produced. The bare-only
    // gate stays as the modifier rule it always was, and the Delete / Escape
    // responses keep matching on the GuiKey (they carry no case and no codepoint
    // worth reading).
    if (app.prompt.active) {
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
    //     enumeration further down carries the full list and this rank);
    //   - Ctrl+Q closes it and FALLS THROUGH so the ordinary close route runs
    //     below, matching every other modal's Ctrl+Q hatch.
    // A popup and an editor CANNOT be open together (the popup opens only from a
    // press, and a press dies at the editor gates; `;` is swallowed here), so
    // this gate can never contend with route_modal_editor_key.
    if (app.settings_popup.open) {
        if (settings_popup_key_blocked(key, mods)) return;
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
    if (app.editor_text_drag.active) {
        const bool escape_hatch =
            (!ctrl && !shift && !alt && key == GuiKeys::Escape) ||
            (ctrl && !shift && !alt && key == GuiKeys::Q);
        if (!escape_hatch) return;
        finalize_editor_text_drag();
        // fall through: the editor handler below runs Esc (cancel the
        // edit) or Ctrl+Q (tear the edit down, then the close prompt
        // opens) exactly as with no drag in flight.
    }

    // KEYBOARD-MODAL EDITOR GATE. While ANY editor is open — the two
    // bottom-strip ones, the bpm bracket, and the top-strip flag editor
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
    // editor (navigation), which rides modal_bottom_strip_editor_active rather
    // than this predicate, and opening a flag editor still does not stop
    // playback — that one rides no predicate at all, each bottom-strip surface
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
    // (Ctrl+Q only) leaves the edit already torn down and lets on_key run the
    // close routing.
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

    // Render-commit prompt editor (bare `'` opener). Same modal shape as the
    // settings editor block above; the two are mutually exclusive in practice
    // (each opener no-ops while the other owns the keyboard). Routed before the
    // render/batch Esc cancel so Esc closes the edit first.
    if (text_editor::is_active(app.commit_editor)) {
        if (handle_commit_editor_key(key, mods)) return;
    }

    // Ctrl+C while the tempo hover popup is showing copies the hovered
    // marker's effective tempo value (the pasteable "base" / "base*scale"
    // form the flag editor accepts) to the internal text clipboard
    // (AppState::text_clipboard), the same session-only clipboard the flag
    // and settings editors use, for pasting the implied value of a pass or
    // label ref into a neighbor's flag editor.
    // Placed below the prompt gate (line above returns while a modal is up)
    // and the two editor blocks (which return on their own Ctrl+C, keeping
    // the editor's copy-selection working while an editor owns input), so
    // reaching here means neither a modal nor an editor is active. Fires only
    // while a pass/ref resolved readout is showing, in which case copy_payload
    // is non-empty (owners and phase resets carry no payload). Ctrl+C was
    // otherwise unbound globally.
    if (ctrl && !shift && !alt && key == GuiKeys::C &&
        !app.hover_popup.copy_payload.empty()) {
        app.text_clipboard = app.hover_popup.copy_payload;
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
    // trim / strip / region drags, the Alt+drag grab-pan (scroll_drag),
    // and the
    // pending marker / trim drags (a press held before its drag begins) —
    // are mutually exclusive with it. scroll_drag belongs on the list too: a live
    // pan must swallow authoring keys rather than letting one run over a latched
    // pan. (The scrub is a one-shot press action, not a gesture — it arms nothing,
    // so it has no entry here. The tempo drag and its pending were entries until
    // 2026-07-29, when the whole tempo drag was deleted — see marker_drag.h.)
    if (app.drag.active || app.trim_drag.active ||
        app.strip_drag.active || app.region_drag.active ||
        app.scroll_drag.active ||
        app.pending_marker_drag.active ||
        app.pending_trim_drag.active) {
        // The ONE hatch left, modifier-exact (a modified Ctrl+Q has no binding
        // anywhere): end the gestures as their release would, then run the close
        // flow. Bare Esc takes the swallow below with every other key.
        if (ctrl && !shift && !alt && key == GuiKeys::Q) {
            finalize_active_drags();
            prompt.request_close();
            return;
        }
        return;
    }

    // Per-tab read-only keyboard gate: a permitted-keys allowlist that filters
    // out every authoring chord while admitting navigation, playback,
    // view-switching, the close-prompt routing, and the bare-o
    // toggle-off escape chord. Runs when the active tab's ViewState carries
    // read_only = true.
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
    //   - 0 (no mods)            → working zoom ↔ full zoom-out toggle
    //                              (run_zoom_toggle_command)
    //   - f (no mods)            → follow mode toggle
    //   - c (no mods)            → focused-marker jump (when present) +
    //                              working zoom; with no focused marker,
    //                              working zoom centered on the playhead
    //   - t (no mods)            → S/T sub-view toggle
    //   - p (no mods)            → W/P sub-view toggle
    //   - Tab/Shift+Tab/IsoLeftTab → cycle marker focus
    //   - Ctrl+Tab               → switch A/B tab (the other escape)
    //   - Ctrl+Shift+Tab         → march paired tabs in lockstep
    //   - Esc                    → the render/batch cancel (and the editor /
    //                              prompt closes); nothing else — the
    //                              selection/region ladder it used to serve here
    //                              is deleted, so a bare Esc with no render
    //                              running is a plain no-op
    //   - Ctrl+Q                 → close-prompt routing
    // Ctrl+S is NOT admitted: read-only means no save, so it drops at this
    // gate like the authoring chords. Gesture-owned state changed in a locked
    // tab (the read-only flag itself, trim, view state, font size, playback
    // speed) reaches disk only after unlocking (bare o) or via Ctrl+S from the
    // writable tab.
    // Authoring-mutation chords are BLOCKED at this gate, not admitted for a
    // deeper refusal: the marker / tempo / phase-reset drop / nudge /
    // status-toggle chords, the trim gesture (x), Delete, the
    // propagate copy/paste (Ctrl+P and the Ctrl+Alt+P pair), and undo/redo
    // (Ctrl+Z / Ctrl+Shift+Z) all drop here. This gate is the ONLY read-only
    // guard on the keyboard path; the surviving deeper checks each cover a
    // surface it cannot reach — do_undo / do_redo's target-tab peek (the
    // ACTIVE tab is writable but the top history entry targets the other,
    // read-only tab; this gate tests only the active tab), and the per-gesture
    // wheel and pointer guards (a wheel event never passes through on_key, and
    // neither does a pointer gesture — the ONE pointer surface that does is the
    // toolbar row's four buttons, which dispatch their chord through on_key
    // precisely so this gate applies to them unchanged: Save, Undo, Redo and
    // Render all drop here in a locked tab exactly as their keys do). Full
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
    // view and lands in target through the `t` toggle chokepoint. (The
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

    // Bare `o` toggles the active tab's read-only flag. Always admitted
    // by the read-only allowlist above (the locked-out user must be
    // able to unlock). Pure view-state mutation: not undoable, not dirty;
    // silently persisted on the next Ctrl+S from a writable surface (Ctrl+S
    // drops at the read-only gate, so a tab just locked here reaches disk from
    // the other, unlocked tab — or after a bare-o unlock). The bottom strip's
    // "(read-only)" token is the whole visible cue, and its update lands through
    // invalidate_timestamp_area, which covers that status line.
    if (key == GuiKeys::O && !ctrl && !shift && !alt) {
        ViewState& vs = active_view_state(app);
        vs.read_only = !vs.read_only;
        viewport.invalidate_timestamp_area();
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
    // resting the press falls straight through and cancels the render exactly as
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
    // the press has nothing left to do. THEY ARE LISTED IN RANK ORDER, outermost
    // modal first:
    //   (a) THE EDITOR TEXT-DRAG ESC HATCH — a bare-exact Escape ends an in-flight
    //       text-selection drag (above); a SUB-PART of the editor class below,
    //       since it can only fire while one of the four editors owns the
    //       keyboard, and the same press then falls through to that editor's own
    //       close/cancel;
    //   (b) THE EDITORS — all four, through route_modal_editor_key: Esc closes /
    //       cancels the edit (the editor blocks above, bit-for-bit unchanged);
    //   (c) THE PROMPTS — Esc activates the rightmost response (the prompt gate at
    //       the top of on_key, unchanged);
    //   (c2) THE SETTINGS DROPDOWN — Esc closes the popup (the popup gate,
    //       directly under the prompt gate; architect 2026-07-31, the SIXTH
    //       binding). It cannot collide with (a)/(b): a popup and an editor can
    //       never be open together, the popup opening only from a press and a
    //       press dying at the editor gates. It ranks BELOW the prompt because
    //       Ctrl+Q from inside the popup can raise one;
    //   (d) THE REGION CLEAR — the arm just above (architect 2026-07-30);
    //   (e) THE RENDER / BATCH CANCEL — handle_escape_cancels, just above.
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
    // is an explicit no-op (handle_plain_bare_keys) — the one place the press ends.
    // Modified Escape remains unbound everywhere, at every Escape reader.

    // Ctrl+Q: quit (via unsaved-work dialog when dirty).
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        prompt.request_close();
        return;
    }

    // Render-trigger chords: Ctrl+Alt+R single render, Ctrl+Alt+Shift+R
    // miscellaneous render, Ctrl+Alt+I iteration sweep. (The render-commit
    // opener is bare `'`, handled separately below.)
    if (handle_render_dispatch_keys(key, mods)) return;

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
        // point, and the lower-half SCRUB press is the gesture for previewing it
        // (click anywhere inside the span and it auditions from there, the span
        // resting untouched). Space now touches no region at all, in either
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

    // Bare 0 toggles between the working zoom and full zoom-out
    // (run_zoom_toggle_command). The trim-bar double-click deliberately
    // DIVERGES from this (run_span_framing_command — it zooms to the region
    // / trim / whole-song span, never the working zoom); the key keeps the plain
    // toggle. C remains the direct working-zoom-and-center gesture. Digits 1..9
    // are intentionally unbound.
    if (!ctrl && !alt && !shift && key == GuiKeys::Digit0) {
        run_zoom_toggle_command();
        return;
    }

    // Ctrl+Z undo / Ctrl+Shift+Z redo. Placed before the GuiKeys::S save
    // handling so modifier dispatch reads left-to-right in the source.
    // Both are silent no-ops when their respective stack is empty.
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
        if (ctrl && !shift && !alt) { save_ops.save(); return; }
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
    // with no region it is a silent no-op. Shift+X writes [0, total-1]
    // (handle_trim_shift_x). The playhead plays no part. Trim's pointer routes
    // are the PLAIN chip-row press (single via a chip-rect hit, pair via a bridge
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

    // Bare `;` opens the settings prompt in the bottom strip. Keyboard-only
    // (no click analogue). The active-editor block at the top of on_key
    // routes subsequent keystrokes; opening here just primes the State.
    // The settings editor is a modal bottom-strip surface, so its open takes the
    // shared modal stop (stop_playback_for_modal_open — the decision table and
    // the flag editor's exemption live at its declaration). This open has no
    // guards to clear: `;` always opens the editor, so the stop and the open are
    // adjacent unconditionally.
    if (key == GuiKeys::Semicolon && !shift && !ctrl && !alt) {
        playback_lifecycle.stop_playback_for_modal_open();
        settings_editor.open();
        return;
    }

    // Bare `'` opens the render-commit prompt in the bottom strip: commit a
    // chosen render as the new authoring baseline by NAME. Keyboard-only. A
    // modal bottom-strip surface. open_commit_editor owns the no-source /
    // empty-renders guards AND the playback stop: playback halts only when the
    // modal actually opens, so a refused open leaves a listening session
    // undisturbed (once open, Space is inside the modal blocked set, so
    // playback cannot restart until the editor closes).
    if (key == GuiKeys::Apostrophe && !shift && !ctrl && !alt) {
        open_commit_editor();
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
    // bound (trim's pointer route is the plain chip-row press-drag on its chip /
    // the inter-chip bridge).
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
        // Both routes take the press's platform repeat bit: it is what makes a
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
    // moves the playhead onto it and recenters. Byte-identical to the `c`
    // gesture's marker jump.
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
        const int n = (app.active_markers_view == 'P')
            ? static_cast<int>(app.phaseresetmarkers.markers().size())
            : static_cast<int>(app.warpmarkers.markers().size());
        if (idx >= n) return false;
    }

    playback_lifecycle.stop_playback_if_playing();

    // THE LAND GOES THROUGH ITS ONE OWNER (2026-07-30): the two-step placement
    // basis, the direct cursor write with NO viewport move, and the damage that
    // follows it are land_playhead_on_marker's (input_pointer.cpp, where the
    // marker-lane-owns-the-playhead rule and the caller inventory live). This
    // site hand-copied that recipe; now it calls it. NOT move_playhead_to, which
    // would scroll the viewport a second time before the centering below.
    // The owner OWNS the damage: full waveform area + timestamp on a land that
    // MOVES, and an early return on a land onto the sample the playhead already
    // holds — nothing moved there, so nothing needs erasing. What stays HERE is
    // exactly what the owner does not provide: the stop above, the region clear,
    // and the recenter below.
    land_playhead_on_marker(app, audio, viewport, app.last_selected_marker);

    // Navigation jump: dissolve a resting region highlight — its span is stale
    // now the playhead has left it. Covers the whole Tab family and `c` through
    // this one shared tail.
    clear_region_highlight(app, viewport);

    // Center the viewport on the focused marker at the current zoom — Tab
    // leaves the zoom level alone. This recenter is unconditional: follow
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

void GuiInputHandler::run_zoom_toggle_command() {
    // The bare `0` key toggle: at the working zoom → full zoom-out (the per-file
    // effective ceiling, whole song visible); anywhere else → the working zoom,
    // centered on the playhead via apply_zoom_change. The trim-bar DOUBLE-CLICK
    // deliberately DIVERGES from this (see run_span_framing_command): the
    // toggle here returns any non-working level to the working zoom, whereas the
    // double-click zooms to the region / trim / whole-song span.
    //
    // BARE `0` IS A PURE VIEWPORT MOVE (architect 2026-07-30, reversing the
    // 2026-07-29 clear+collapse as OVERSCOPED): it touches neither the selection
    // nor the region nor the playhead — only the zoom level and, through
    // apply_zoom_change, the viewport start. A selection span's endpoints are
    // ACTIVE-DOMAIN frames and a zoom changes no domain, so a group and its extent
    // survive the overview toggle exactly as they survive `=` / `-` and the wheel.
    // The family is the group-verb doctrine (position_nudge.h): `0` sits with the
    // zoom framing on the span-READ side, not with the collapse+land verbs. It does
    // not stop a live audition either — the pure-viewport-move class of the keyboard
    // stop rule (stop_playback_if_playing's declaration, playback_lifecycle.h).
    // THE REGION CLEAR DIED WITH THE COLLAPSE, NOT SEPARATELY — do not reintroduce
    // it on its own: a clear that left a 2+ selection standing would rest it
    // SPANLESS, the hybrid third form the architect rejected (that state draws no
    // playhead cue at all), so the two are one decision.
    // The centering below reads the RESTING cursor (apply_zoom_change takes the
    // scanner while playing and the cursor otherwise). With a selection the cursor
    // already rests on the focus — every focus-changing route lands it — so nothing
    // here needed the land that used to precede it.
    if (app.zoom_level == kWorkingZoomLevel) {
        viewport.apply_zoom_change(effective_max_zoom_level(
            waveform_area(app).w, live_total_frames(app, audio),
            audio.sample_rate()));
    } else {
        viewport.apply_zoom_change(kWorkingZoomLevel);
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

void GuiInputHandler::run_span_framing_command() {
    // The trim-bar double-click ZOOMS TO A SPAN, split from
    // run_zoom_toggle_command so the bare `0` key keeps its working-zoom toggle
    // and `c` keeps its marker-jump working zoom. It only ever FRAMES a span,
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
        // displayed_or_live_target_map — the same basis the flags, chips and
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

// Shared wheel handler. Verbatim from the lambda at the original
// main.cpp:1444 — only difference is the captured viewport / playhead
// helpers now resolve through this struct's reference members.
void GuiInputHandler::handle_wheel(GuiMouseButton button, int count,
                                   bool ctrl, bool shift, bool alt,
                                   bool inside_waveform, bool inside_top) {
    if (!inside_waveform && !inside_top) return;
    // `count` is the net detent count coalesced for this pointer frame
    // (always >= 1 from the platform). Each chord scales its single per-step
    // action by that count and applies it in ONE viewport call, so the
    // damage / hover / worker-kick path fires once per frame regardless of
    // burst size. count == 1 reproduces the single-detent behavior.
    if (count < 1) count = 1;
    // Strict modifier matching: each wheel chord is an exact match. Ctrl+Alt is
    // no longer a wheel chord — it matches nothing here and the event is
    // swallowed.
    if (alt && !ctrl && !shift) {
        // Alt+wheel pans everywhere over the waveform or top strip — the whole
        // strip is one pan surface now (the chip-row trim-end move is retired).
        const int64_t step = std::max<int64_t>(
            1, samples_visible(app, audio) / kViewportLeadDivisor);
        viewport.scroll_viewport((button == GuiMouseButton::WheelUp ? -step : +step) * count);
        return;
    }
    if (!ctrl && !shift && !alt) {
        // WheelUp zooms out, WheelDown zooms in; the net level change applies
        // in a single apply_zoom_change inside zoom_steps.
        viewport.zoom_steps(button == GuiMouseButton::WheelUp ? -count : +count);
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
    // Only the BOTTOM-STRIP modal surfaces swallow the wheel (the settings and
    // render-commit editors and the BpmBracket reuse of top_flag_editor) —
    // modal_bottom_strip_editor_active, deliberately NOT the keyboard gate's
    // keyboard_modal_editor_active. The top-strip flag editor IS keyboard-modal
    // (architect 2026-07-28) and the wheel still punches through it anyway,
    // because the wheel is NAVIGATION, not a chord, and that ruling is about
    // chords: zooming or panning while an edit is open changes no state the edit
    // owns and discards nothing, so there is nothing for modality to protect.
    //
    // The wheel routes by area — the waveform and the top strip — plus the ONE
    // row-wise carve-out below, the redesigned rows' inert band. Under Alt both
    // areas pan and under no modifier the waveform zooms, so the top strip needs
    // no further split (the chip-row trim-end move is retired, and with it the
    // region that existed solely to keep the platform's sub-detent remainder
    // attribution from bridging two diverging Alt routes). Fewer regions is
    // strictly safer for the accumulator, and the inert band is the safest kind:
    // it emits nothing at all.
    //
    // A wheel event during ANY active pointer gesture is ignored, matching
    // on_button_press and the keyboard's drag-modal gate. The region drag
    // is included: the keyboard gate swallows every authoring chord
    // mid-drag, so viewport changes must not slip through either.
    // The editor-text drag is included: viewport changes must not fire under a
    // held text-selection drag either (a wheel can emit axis events while the
    // primary button stays held), so this gate is any_pointer_gesture_active,
    // every gesture — the pending marker drag included, so a wheel cannot pan
    // or zoom the viewport out from under a flag press before its drag begins.
    if (app.prompt.active) return -1;
    // AN OPEN DROPDOWN SWALLOWS THE WHEEL. It cannot close from here — this
    // predicate is const and the platform probes it speculatively — so on_wheel
    // owns the close and this owns only the swallow, which is also what keeps
    // the sub-detent accumulator from growing remainder under a popup.
    if (app.settings_popup.open) return -1;
    if (modal_bottom_strip_editor_active()) return -1;
    if (app.loading || audio.total_frames() <= 0) return -1;
    if (any_pointer_gesture_active(app)) return -1;

    // THE REDESIGNED ROWS ARE WHEEL-INERT (architect 2026-07-31) — ONE decision
    // for the whole family, recorded here where the routing lives, and every
    // future row inherits it by joining this band list. A wheel over any
    // redesigned row does NOTHING: those rows are chrome with no scrollable
    // content and no relation to the waveform under them, so passing the event
    // through to the top-strip zoom/pan (which is what happened before row 1's
    // ruling) made a scroll over the Quit button silently zoom the song. They sit
    // ABOVE the area tests below because they are sub-bands of the top strip and
    // must win over it. Returning -1 (rather than 0) also stops the platform's
    // sub-detent accumulator from growing remainder over them.
    {
        const GuiRect bands[] = {
            top_menu_row_area(app), top_toolbar_row_area(app),
            top_tab_row_area(app),  top_icon_row_area(app),
        };
        for (const GuiRect& b : bands) {
            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h)
                return -1;
        }
    }

    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const bool inside_waveform =
        x >= area.x && x < area.x + area.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top =
        x >= top.x && x < top.x + top.w &&
        y >= top.y && y < top.y + top.h;
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
    close_settings_popup();
    hide_shift_tooltip();
    const int ctx = wheel_context(x, y);
    if (ctx < 0) return;
    // ctx: 1 waveform, 2 the top strip. The waveform zooms (plain) or pans
    // (Alt); the top strip pans (Alt).
    handle_wheel(dir, count, mods.ctrl, mods.shift, mods.alt,
                 ctx == 1, ctx == 2);
}

bool GuiInputHandler::apply_editor_clipboard(
        text_editor::KeyAction action, text_editor::State& s) {
    switch (action) {
        case text_editor::KeyAction::CopyRequested:
            app.text_clipboard = text_editor::selected_text(s);
            return true;
        case text_editor::KeyAction::CutRequested:
            app.text_clipboard = text_editor::selected_text(s);
            text_editor::replace_selection(s, std::string());
            return true;
        case text_editor::KeyAction::PasteRequested:
            text_editor::replace_selection(s, app.text_clipboard);
            return true;
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
    // (The blank/loading guard near the top of on_key already covers
    // this, but the helper is defensive in case future callers reach
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

    // The warp flag editor is a source-view-only authoring surface (the
    // home-view binding rule, 2026-07-22). The S -> T toggle would strand a
    // live one on a now-refusing target surface, so close it WITHOUT
    // committing (the exact Esc teardown) before the flip proceeds. Guarded
    // internally on an active editor, so this is a no-op when none is open.
    // Only `t` and the settings-editor `active_audio_view=` commit route here
    // into target view — Ctrl+Tab never changes active_audio_view — so this
    // is the one place the toggle-into-target edge is handled.
    if (entering_target) flag_editor.exit_top_flag_edit_no_commit();

    // Entering target view exits iteration mode through the shared wipe
    // chokepoint: brackets are the step-away batch tool, target view the
    // live-by-hand tool. wipe_iter_state pushes ONE undo entry when any
    // bracket existed (plain undo back in S restores the bracket set — the
    // ungated-undo rule at the chokepoint), and no-ops otherwise. This makes
    // mode-off-in-target an INVARIANT: the mode only turns on in warp+source
    // (the `i` toggle's active_column_authoring_allowed gate), Ctrl+Tab never
    // changes the audio view, and every S -> T entry runs this exit. The
    // settings-editor active_audio_view=T commit routes through this same edge
    // and inherits the wipe (a GUI-kind commit is history-less, but this
    // wipe's undo entry is the warp-store side effect wipe_iter_state always
    // pushes — exactly like the `i` toggle, no special-casing). No extra
    // invalidation here — the toggle's own tail repaints everything.
    if (entering_target && app.iteration_mode_enabled) {
        flag_editor.wipe_iter_state();
        app.iteration_mode_enabled = false;
    }

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

    // THE GROUP CARRIES ACROSS THE FLIP (architect 2026-07-30, resolving the
    // deferral this block used to record): `t` translates the same markers into
    // another domain rather than changing which column is addressed, so a 2+
    // selection survives it BY IDENTITY — the collapse-to-focus that stood here
    // is deleted. It existed to keep a group from resting SPANLESS, and with the
    // SPAN FORM retired there is no such state to avoid: the group's cue is its
    // members' ink triangles plus the always-visible cursor, and the
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
    // invalidate so the bottom-strip S/T indicator, the waveform
    // surface, and the playhead column all repaint in one frame.
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
    viewport.clear_hover_popup();
    // One-shot discrete jump with a domain change: is_target, the viewport, and
    // the warp_frame_map hash all flip, so the displayed plate must change. Render it
    // synchronously and publish the displayed fingerprint now, so the
    // bottom-strip S/T indicator and the playhead column do not repaint a frame
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

void GuiInputHandler::apply_font_size(double pt) {
    // The shared live sequence a source load's apply_settings_engine_and_prefs
    // tail runs (assign the field, push to the renderer, full invalidate, then
    // the resize-path geometry-and-cache rebuild). Both callers gate the no-op
    // case, so no early-return here.
    app.font_size = pt;
    set_gui_font_size_pt(pt);
    viewport.invalidate_all();
    paint_handler.on_resize(app.width, app.height);
}

void GuiInputHandler::apply_gui_scale(int percent) {
    // apply_font_size's shape on the OTHER scale axis, and for the same reason:
    // the value feeds main.cpp's per-lane height table (the menu row's
    // menu_row_h_px), so a change moves every strip boundary and the waveform
    // area with them. Same four steps in the same order — assign, push, full
    // invalidate, resize-path geometry-and-cache rebuild — so the new layout is
    // on screen at the commit rather than at the next restart. The sole caller
    // (the settings editor's `gui_scale=` commit) gates the no-op case.
    app.gui_scale = percent;
    set_gui_scale_percent(percent);
    viewport.invalidate_all();
    paint_handler.on_resize(app.width, app.height);
}
