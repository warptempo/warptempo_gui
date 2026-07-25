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
    // Command-adjacency counter: one bump per discrete user command, here and
    // at the other two dispatch entry points (on_button_press, on_wheel). The
    // undo-coalesce guard merges an eligible press only when it is the
    // immediately-next command, so any intervening keypress — even a swallowed
    // or unhandled one — breaks a same-gesture burst. The platform delivers
    // here only on key PRESS (releases return early) and drops bare modifiers
    // and F-keys before delivery, so this never fires for a held modifier;
    // key-repeat re-enters through the same path as consecutive commands, which
    // correctly coalesce.
    ++app.command_seq;
    // Stem-pin preserve: re-stamp the lateral-gesture pin at function exit if
    // this command painted nothing (a silent refusal like P-view Alt+Up/Down,
    // an unbound key, a modal-swallowed key). See StemPinPreserveGuard.
    StemPinPreserveGuard stem_pin_guard(app, gui);
    // Double-click lifecycle, KEYBOARD half: any keyboard command between two
    // clicks breaks EVERY pending double-click candidate (ZoomRow, Marker,
    // EmptyLane alike) at this one chokepoint — no legitimate double-click types
    // a key between its two presses, and a cross-context consume (seed a
    // candidate, run a command, click again to consume in a different context)
    // must not fire. The consume lives entirely in on_button_press (nothing on
    // the keyboard path reads the candidate), and key-repeat re-entering here is
    // equally fine — no candidate can survive a held key. The pointer-side
    // per-branch clears (the on_button_press top-of-frame clear, the moved-drag
    // clears, the Esc drag-cancel clears) stay: they own the pointer half of the
    // lifetime; this owns the keyboard half, and on_wheel owns the wheel half
    // (same clear beside its own command_seq bump).
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
    // the prompt is up. Letter keys are case-insensitive; Delete and
    // Escape map to sentinel chars '\x7f' and '\x1b' so they participate
    // in the same vector<char> match as letter responses.
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
        if (key >= GuiKeys::A && key <= GuiKeys::Z) {
            k = static_cast<char>('a' + (key - GuiKeys::A));
        } else if (key == GuiKeys::Delete) {
            k = '\x7f';
        } else if (key == GuiKeys::Escape) {
            k = '\x1b';
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
    // gesture, so it gets no bare-`s` carve-out.
    if (app.editor_text_drag.active) {
        const bool escape_hatch =
            key == GuiKeys::Escape ||
            (ctrl && !shift && !alt && key == GuiKeys::Q);
        if (!escape_hatch) return;
        finalize_editor_text_drag();
        // fall through: the editor handler below runs Esc (cancel the
        // edit) or Ctrl+Q (tear the edit down, then the close prompt
        // opens) exactly as with no drag in flight.
    }

    // Bottom-strip modal-editor gate. Modal surfaces are bottom-strip
    // surfaces — the two bottom-strip editors (the settings editor and the
    // bpm bracket editor) and the prompts (gated above); the top-strip
    // flag editor is deliberately non-modal. While a bottom-strip editor
    // is open, only the keys the editor itself consumes plus Esc, Ctrl+S,
    // and Ctrl+Q get through (modal_editor_key_blocked); everything
    // else — playback, navigation, zoom, mode toggles, tab switches,
    // undo/redo, marker / trim chords — drops here, so no authoring or
    // view change can happen while the editor is up. Admitted keys route
    // through the editor blocks below. Sits after the text-drag gate so an
    // in-flight editor selection drag keeps owning the keyboard exactly as
    // before.
    if (modal_bottom_strip_editor_active() &&
        modal_editor_key_blocked(key, mods)) {
        return;
    }

    // The top-flag editor owns the keyboard while active. Routes here
    // BEFORE queue/drag/playhead Esc handlers so Esc cancels the edit
    // first; Esc with no active edit falls through to the rest.
    if (text_editor::is_active(app.top_flag_editor)) {
        if (handle_top_flag_editor_key(key, mods)) return;
    }

    // Settings-prompt editor (`;` opener). Same shape as the flag-editor
    // block above. The two editors are mutually exclusive in practice
    // because the flag editor's block returns early while it owns the
    // keyboard, so a stray `;` can't open settings over a live flag
    // edit. Routed before queue/drag/playhead Esc handlers so Esc
    // cancels the edit first.
    if (text_editor::is_active(app.settings_editor)) {
        if (handle_settings_editor_key(key, mods)) return;
    }

    // Render-commit prompt editor (bare `'` opener). Same modal shape as the
    // settings editor block above; the two are mutually exclusive in practice
    // (each opener no-ops while the other owns the keyboard). Routed before the
    // queue/drag/playhead Esc handlers so Esc cancels the edit first.
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
    // flight, swallow every hotkey except the escape hatches — Esc stops
    // the gesture (cancel_active_drags), and Ctrl+Q cancels the in-flight
    // drag first (the same abandon Esc performs, since the gesture is
    // uncommitted) and then runs the close flow.
    // Cancelling before the prompt goes up is what keeps a dismissed
    // prompt from leaving a stale drag that commits on the next motion.
    // This single gate is why no downstream hotkey needs its own
    // drag guard: Tab, undo, `t`, and the rest never see a key mid-drag.
    // The editor text-selection drag has its own modal gate above
    // the text-editor handlers; the pointer gestures here — the marker /
    // tempo / trim / strip / region drags, the Alt+drag grab-pan (scroll_drag),
    // and the
    // pending marker / tempo / trim drags (a press held before its drag begins) —
    // are mutually exclusive with it. scroll_drag belongs on the list too: a live
    // pan must swallow authoring keys, and Esc must route to cancel_active_drags
    // (which ends it) rather than falling past to run a hotkey over a latched pan.
    // (The scrub is a one-shot press action, not a gesture — it arms nothing,
    // so it has no entry here.)
    if (app.drag.active || app.tempo_drag.active || app.trim_drag.active ||
        app.strip_drag.active || app.region_drag.active ||
        app.scroll_drag.active ||
        app.pending_marker_drag.active || app.pending_tempo_drag.active ||
        app.pending_trim_drag.active) {
        if (key == GuiKeys::Escape) {
            cancel_active_drags();
            return;
        }
        if (ctrl && !shift && !alt && key == GuiKeys::Q) {
            cancel_active_drags();
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
    //   - Space                  → playback toggle
    //   - Left/Right (no mods)   → playhead-by-pixel scrub
    //   - Home/End (no mods)     → playhead to trim region bounds
    //   - PageUp/PageDown        → viewport step scroll by the Alt-wheel
    //     (no mods)                step. Pure navigation, same family as
    //                              the scrub and Home/End entries.
    //   - =/- (no mods)          → zoom in/out
    //   - 0 (no mods)            → fit ↔ snap-zoom toggle
    //   - f (no mods)            → follow mode toggle
    //   - c (no mods)            → center+snap-zoom on playhead
    //   - t (no mods)            → S/T sub-view toggle
    //   - p (no mods)            → W/P sub-view toggle
    //   - Tab/Shift+Tab/IsoLeftTab → cycle marker focus
    //   - Ctrl+Tab               → switch A/B tab (the other escape)
    //   - Ctrl+Shift+Tab         → march paired tabs in lockstep
    //   - Esc                    → top-level no-op
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
    // wheel and pointer guards (wheel and mouse events never pass through
    // on_key). Full rationale at read_only_key_blocked in
    // input_key_dispatch.cpp.
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
    // exceptions: (1) the TEMPO family in W+target — one motion
    // (stretch/squish), three flavors: the Alt+Up/Down step (owner-only there),
    // the tempo drag, and the Alt+Left/Right tempo-image step (the drag's
    // keyboard twin, dispatched below where the warp column's nudge route
    // splits by view); (2) the phase-reset propagate paste starts in source
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
    // the other, unlocked tab — or after a bare-o unlock). The bottom-strip dim
    // update lands through invalidate_timestamp_area, which covers the A/B tab
    // letter glyph.
    if (key == GuiKeys::O && !ctrl && !shift && !alt) {
        ViewState& vs = active_view_state(app);
        vs.read_only = !vs.read_only;
        viewport.invalidate_timestamp_area();
        return;
    }

    // Esc cancels an in-flight render / queued batch.
    if (handle_escape_cancels(key)) return;

    // Esc ladder (architect 2026-07-23, DOWN-ONLY as of round 4): the
    // selection/region collapse rung, placed AFTER the higher-priority consumers —
    // a live pointer drag (cancelled at the drag-modal gate above), an open editor
    // (closed in the editor blocks above), and an in-flight render/batch (cancelled
    // just above) each win — and in place of the old plain region clear. It walks
    // ONE rung per Esc: an active region + 2+ selected clears the SELECTION ONLY
    // (region rests, demoted to Free); an active region + 0/1 selected collapses to
    // the playhead (clear region + selection, playhead to its lo bound) — a region
    // never shrinks into a subregion; else a programmatic multi-select drops to its
    // extent region + deselects; else a singleton deselects + lands the playhead on
    // the marker. All navigation-class, so this runs in read-only too (the
    // allowlist admits Esc). See handle_escape_selection_region for the rungs.
    if (key == GuiKeys::Escape && handle_escape_selection_region()) return;

    // Ctrl+Q: quit (via unsaved-work dialog when dirty).
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        prompt.request_close();
        return;
    }

    // Render-trigger chords: Ctrl+Alt+R single render, Ctrl+Alt+Shift+R
    // miscellaneous render, Ctrl+Alt+I iteration sweep. (The render-commit
    // opener is bare `'`, handled separately below.)
    if (handle_render_dispatch_keys(key, mods)) return;

    // Alt+Space in actual target view, phase-reset mode: non-destructive
    // audition of the OLA/Hann synthesis lead-in. Launches the playback scanner
    // N/2 output samples AHEAD of the resting playhead (full-scale point of a
    // reset dropped at the playhead) without moving the cursor, so stop just
    // deactivates the scanner — the cursor it never touched is exactly where
    // it was. Placed BEFORE the modifier-independent
    // is_play_pause_key block, which would otherwise swallow Alt+Space and run
    // a plain toggle. Restricted to Space (not Return/KpEnter). Source view and
    // warp mode fall through to the normal toggle below.
    if (key == GuiKeys::Space && alt && !ctrl && !shift &&
        app.active_markers_view == 'P' &&
        app.active_audio_view == 'T') {
        // Mirror the plain-Space target gate: refuse Space-to-play while a
        // target render is in flight or before any successful render populated
        // the buffer. Space-to-stop (playing) is always honored.
        if (!playback.is_playing()) {
            if (target_render.is_updating()) return;
            if (app.target_buffer_frames <= 0) return;
        }
        playback_lifecycle.toggle_playback(kN / 2);
        return;
    }

    // Space is the sole playback toggle, modifier-independent (Return / keypad
    // Enter are NOT playback keys — they open the flag editor, handled just
    // below).
    if (is_play_pause_key(key)) {
        // Target-view playback gating: refuse Space-to-play while a
        // target render is in flight (current is stale by
        // definition). Space-to-stop is still honored — if playback
        // happened to be running before an edit, the trigger() helper
        // already froze it, so playback.is_playing() is false in
        // practice. The empty-target-buffer case (no successful target
        // render yet in this session) is also refused so the
        // user can't play stale source-domain samples through a
        // target-view binding. Source view falls through unchanged.
        if (app.active_audio_view == 'T' &&
            !playback.is_playing()) {
            if (target_render.is_updating()) return;
            if (app.target_buffer_frames <= 0) return;
        }
        // With an active region, Space auditions from its LEFT bound — the
        // smaller of the two active-domain endpoints, regardless of drag
        // direction — because the point of the highlight is to hear its start.
        // Only on the START edge (a Space that STOPS is untouched; a Space with
        // no active region is untouched). move_playhead_to owns the
        // active-domain clamp and the region frames are already active-domain,
        // so the just-moved cursor is what toggle_playback reads as its start.
        // The region sets ONLY the start: the trim loop verdict is still
        // captured from app.trim inside toggle_playback, so region + trim loops
        // the trim window as before and region + no trim plays through to the
        // end with no loop.
        // DELIBERATE (architect 2026-07-21): a region left bound outside a
        // set trim's window trips toggle_playback's existing "playhead
        // outside trim" no-op — a contradiction resolved by playing nothing,
        // not by widening the window.
        if (!playback.is_playing() && app.region.active) {
            viewport.move_playhead_to(
                std::min(app.region.a_frame, app.region.b_frame));
        }
        playback_lifecycle.toggle_playback();
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
    // (run_zoom_toggle_command). The zoom-strip double-click deliberately
    // DIVERGES from this (run_zoom_double_click_command — it zooms to the region
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
        // bare `s` = plain drop, Alt+S = augmented drop; the same bare/Alt
        // plain/augmented split in both views. In P view the augmented drop is
        // the target-view lead-in reset — a reset dropped N/2 before the
        // playhead so its lead-in output reaches full scale at the playhead
        // (the perceived transient). It exists only in target view, where the
        // output-domain overlay/lead-in it aligns to lives, so source-view
        // Alt+S in P mode stays a no-op (falls through). In W view bare `s`
        // drops a plain neutral 1.00 owner and Alt+S drops an augmented owner
        // that copies the immediate-prior marker's effective tempo. Ctrl+S
        // saves; a Shift-modified `s` is unbound (a consumed no-op here).
        if (ctrl && !shift && !alt) { save_ops.save(); return; }
        // Every remaining `s` arm is a marker drop (warp bare/Alt copy, phase
        // bare/Alt lead-in) — home-view authoring. Off home refuses silently
        // (consumed no-op), covering the warp Alt+S copy-drop that is otherwise
        // reachable in target view. The Alt+S lead-in arm already requires
        // P && T = phase's home, so the predicate is a no-op there.
        if (!active_column_authoring_allowed(app)) return;
        if (!ctrl && !shift && !alt &&
                 app.active_markers_view == 'P') phase_resets.drop_phase_reset_at_playhead();
        else if (alt && !ctrl && !shift &&
                 app.active_markers_view == 'P' &&
                 app.active_audio_view == 'T')   phase_resets.drop_phase_reset_lead_in_at_playhead();
        else if (!ctrl && !shift && !alt &&
                 app.active_markers_view == 'W') warpops.drop_marker_at_playhead();
        else if (alt && !ctrl && !shift &&
                 app.active_markers_view == 'W') warpops.drop_copy_previous_at_playhead();
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
        // bound (bare x is trim's clear).
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

    // Tempo nudge. Alt+Up / Alt+Down only. No view guard here —
    // adjust_tempo_cents returns at once unless the warp view is active, so
    // Alt+arrows are an inert (still consumed) no-op in phase-reset view. Bare
    // Up / Down are unbound (an ordinary no-op at the bare-key tail); `=` / `-`
    // are the zoom keys (see below).
    if (alt && !shift && !ctrl && key == GuiKeys::Up) {
        warpops.adjust_tempo_cents(+1); return;
    }
    if (alt && !shift && !ctrl && key == GuiKeys::Down) {
        warpops.adjust_tempo_cents(-1); return;
    }
    if (key == GuiKeys::Equal && !shift && !ctrl && !alt) {
        viewport.zoom_in(); return;
    }
    if (key == GuiKeys::Minus && !shift && !ctrl && !alt) {
        viewport.zoom_out(); return;
    }

    // x SETS the trim; Shift+X UNSETS it (architect 2026-07-25, splitting the
    // old x-branch). Bare x is set-only: a live region trims to it (overwriting
    // any existing bounds; the highlight is KEPT, re-coupled to the new window
    // through sync_highlight_to_trim_window — architect 2026-07-23), and with no
    // region it is a silent no-op. Shift+X clears both bounds
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
    // The settings editor is a modal bottom-strip surface: stop playback
    // at its open. Space is inside the modal blocked set, so playback
    // cannot restart until the editor closes.
    if (key == GuiKeys::Semicolon && !shift && !ctrl && !alt) {
        playback_lifecycle.stop_playback_if_playing();
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

    // Alt+Left / Alt+Right: nudge the selected markers / phase resets by one
    // pixel of time. Trim is not part of the selection system, so the nudge
    // never acts on a bound (trim's pointer route is the plain chip-row
    // press-drag on its chip / the inter-chip bridge).
    //
    // ROUTING (architect 2026-07-24 second pass, re-ruling the same-day "third
    // exception" away): each column's POSITION nudge runs in its HOME view only
    // — warp in source, phase reset in target (the home-view binding). In
    // W+TARGET Alt+Left/Right is NOT a position gesture: it dispatches the
    // TEMPO-IMAGE STEP (MarkerDragOps::step_tempo_image), the tempo drag's
    // keyboard twin — steps the FOCUSED marker's IMAGE by one painted column
    // per press where the cent grid allows, else the minimum directional cent
    // (travel can span several columns), via the (deduped participant)
    // predecessor tempo solve, the drag's eligibility legs included. Read-only
    // tabs refuse all three routes
    // upstream (read_only_key_blocked — Alt-exact arrows are not allowlisted).
    if (alt && !shift && !ctrl && key == GuiKeys::Left) {
        if (app.active_markers_view == 'P') {
            if (app.active_audio_view != 'T') return;   // phase home = target
            phase_resets.nudge_selected_phase_resets(-1);
        } else if (app.active_audio_view == 'T') {
            marker_drag.step_tempo_image(-1);   // W+target: tempo-image step
        } else {
            warpops.nudge_selected_markers(-1); // warp home (source): position
        }
        return;
    }
    if (alt && !shift && !ctrl && key == GuiKeys::Right) {
        if (app.active_markers_view == 'P') {
            if (app.active_audio_view != 'T') return;
            phase_resets.nudge_selected_phase_resets(+1);
        } else if (app.active_audio_view == 'T') {
            marker_drag.step_tempo_image(+1);
        } else {
            warpops.nudge_selected_markers(+1);
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
    // The clear+damage shape the existing region-clear sites use (Esc,
    // end_region_drag_min_size_check): reset to a blank RegionState and damage
    // the waveform area once. The wash and the split playhead repaint away and
    // the cursor playhead returns under that same damage. Guarded so a call on
    // the common no-region path costs nothing.
    if (!app.region.active) return;
    app.region = RegionState{};
    viewport.invalidate_waveform_area();
}

bool GuiInputHandler::jump_playhead_to_focused_marker() {
    // The walk is markers-only (trim is not a cycle stop). The playhead moves
    // to the focused marker's source frame unconditionally, and the viewport
    // always recenters on it (below) — follow mode does not gate the cycle.
    int64_t src_sample = 0;
    {
        const int idx = app.last_selected_marker;
        if (idx < 0) return false;
        if (app.active_markers_view == 'P') {
            const auto& tv = app.phaseresetmarkers.markers();
            if (idx >= static_cast<int>(tv.size())) return false;
            src_sample = tv[idx].time_frame;
        } else {
            const auto& mv = app.warpmarkers.markers();
            if (idx >= static_cast<int>(mv.size())) return false;
            src_sample = mv[idx].time_frame;
        }
    }
    // Target view: forward-translate the marker's source-frame through
    // the display context (the live map) so the playhead lands on the
    // marker's displayed position; the viewport recenter below also uses this
    // displayed value via center_viewport_on_playhead.
    int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
    // Playhead domain clamp through clamp_playhead_to_live_domain (the
    // domain ruling), exactly like a bare Left/Right sync.
    sample = clamp_playhead_to_live_domain(sample, app, audio);

    playback_lifecycle.stop_playback_if_playing();

    // Capture the old playhead pixel-x before mutating, for the
    // no-scroll invalidation branch below.
    const double old_px = playhead_pixel_x(app, audio);
    const int64_t old_vp = app.viewport_start_sample;

    // Set the cursor directly — no move_playhead_to, which would scroll
    // the viewport a second time before centering.
    app.playhead_cursor_sample = sample;

    // Navigation jump: dissolve a resting region highlight (its span is stale
    // now the playhead has left it, and the region would suppress the cursor
    // playhead we just moved). Covers the whole Tab family and `c` through this
    // one shared tail.
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

    // The viewport did not move when it was already clamped at EOF
    // (center_viewport_on_playhead a no-op). In that no-move case the
    // cursor's column change still needs its own invalidation — mirror
    // move_playhead_to's no-scroll branch. When the viewport did move, the
    // playhead columns are already inside the waveform-area damage center
    // emitted, so only invalidate columns in the unmoved case to avoid a
    // redundant rect.
    if (app.viewport_start_sample == old_vp) {
        const double new_px = playhead_pixel_x(app, audio);
        viewport.invalidate_playhead_columns(old_px, new_px);
    }
    viewport.invalidate_timestamp_area();
    return true;
}

void GuiInputHandler::run_zoom_toggle_command() {
    // The bare `0` key toggle: at the working zoom → full zoom-out (the per-file
    // effective ceiling, whole song visible); anywhere else → the working zoom,
    // centered on the playhead via apply_zoom_change. The zoom-strip DOUBLE-CLICK
    // deliberately DIVERGES from this (see run_zoom_double_click_command): the
    // toggle here returns any non-working level to the working zoom, whereas the
    // double-click zooms to the region / trim / whole-song span.
    //
    // Bare `0` recenters the view on the playhead (apply_zoom_change), so the
    // region highlight is stale context — clear it (architect-listed
    // explicitly). The zoom-strip double-click does NOT come through here, so
    // its span-framing keeps any live region.
    clear_region_highlight(app, viewport);
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
    const double visible_t = samples_per_pixel_at(target_level, W, total, sr) *
                             static_cast<double>(W);
    const int64_t target_start =
        static_cast<int64_t>(std::nearbyint(mid - visible_t / 2.0));

    // Set level + start through the two clamp chokepoints and repaint like the
    // other zoom commands; the idempotent current-vs-target no-op lives there.
    // NOT apply_zoom_change (which would recenter on the playhead).
    viewport.apply_zoom_to_start(target_level, target_start);
}

void GuiInputHandler::run_zoom_double_click_command() {
    // The zoom-strip double-click ZOOMS TO A SPAN, split from
    // run_zoom_toggle_command so the bare `0` key keeps its working-zoom toggle
    // and `c` keeps its marker-jump working zoom. It only ever FRAMES a span,
    // never the fine working zoom. Span priority: a live region wins (over a
    // trim); else a set trim completed to its extremes; else the whole song
    // (full zoom-out). The framing is idempotent — a second double-click with
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
    } else if (app.trim.has_begin || app.trim.has_end) {
        // A set trim completed to its extremes exactly as playback does (missing
        // begin -> 0, missing end -> total; compute_trim_samples owns that in
        // source frames). Express both bounds in the ACTIVE domain: source view
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
    // Only the bottom-strip modal surfaces swallow the wheel (the settings
    // editor and the BpmBracket reuse of top_flag_editor, the same predicate
    // the keyboard gate uses). The top-strip flag editor is deliberately
    // NOT modal — commands punch through it on the keyboard, so wheel zoom and
    // Alt+wheel pan punch through it too.
    //
    // The wheel routes by area only — the waveform and the top strip. Under Alt
    // both pan and under no modifier the waveform zooms, so there is no row-wise
    // split inside the top strip anymore (the chip-row trim-end move is retired,
    // and with it the region that existed solely to keep the platform's
    // sub-detent remainder attribution from bridging two diverging Alt routes).
    // Fewer regions is strictly safer for the accumulator.
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
    if (modal_bottom_strip_editor_active()) return -1;
    if (app.loading || audio.total_frames() <= 0) return -1;
    if (any_pointer_gesture_active(app)) return -1;

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
    // Command-adjacency bump (see on_key). The platform delivers one on_wheel
    // per pointer frame carrying the net detent count, and each wheel route
    // applies that net count in a single call (zoom / pan both
    // take the summed count), so one wheel frame is one command and one bump —
    // a burst of same-frame detents is a single command, distinct wheel frames
    // are consecutive commands that coalesce.
    ++app.command_seq;
    // Double-click lifecycle, WHEEL half: a wheel command between two clicks
    // moves content under the pointer (a zoom rescales, a pan slides), so the
    // second click must not consume as a double-click of the first. Clear every
    // pending candidate here, beside the command_seq bump, exactly as on_key's
    // keyboard half does — the platform delivers one on_wheel per pointer frame
    // (net detent count >= 1) after accumulating sub-detent remainder itself, so
    // every call here is a completed detent frame that bumps, matching on_key's
    // unconditional placement.
    app.double_click = DoubleClickCandidate{};
    // Stem-pin preserve: re-stamp at exit if this wheel frame painted nothing
    // (a wall-saturated zoom-OUT — WheelUp already at the effective ceiling,
    // zoom_steps returns before any invalidate; an out-of-context wheel over the
    // bottom strip — handle_wheel early-returns; a modifier-mismatched chord
    // swallowed there). See StemPinPreserveGuard.
    StemPinPreserveGuard stem_pin_guard(app, gui);
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
    // fallback for the one tick until the item caches (stems/flags) rebuild,
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
    // the waveform without the wash.
    app.region = RegionState{};

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
