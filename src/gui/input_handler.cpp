#include "input_handler.h"

#include "paint_handler.h"
#include "playback_speed_presets.h"
#include "render.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "trimmer.h"
#include "text_editor.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warpmarkers.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <chrono>
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
// members (warpops, phase_resets, flag_editor, render_view, active_views,
// playback_lifecycle, save_ops, prompt, selection, undo, viewport).
// compute_base_tempo_scale + BaseTempoScale live in input_handler.h so
// this TU can reach them; render_bpm_sweep() is the sole caller.

void GuiInputHandler::on_key(GuiKey key, GuiInputState mods) {
    if constexpr (kDebugPerf) {
        app.last_input_event_time = std::chrono::steady_clock::now();
    }
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
        // PASTE_CONFIRM only: Ctrl+Q / Ctrl+W abandon the pending paste
        // (the real cancel, not a synthesized Esc) and then run the
        // normal close/revert path. The unsaved-work dialogs
        // (CLOSE_WINDOW / REVERT_TO_BLANK) deliberately fall through to
        // the modal swallow below and keep blocking these chords.
        if (app.prompt.trigger == DialogTrigger::PASTE_CONFIRM &&
            ctrl && !shift && !alt &&
            (key == GuiKeys::Q || key == GuiKeys::W)) {
            prompt.cancel_paste_confirmation();
            prompt.request_close_or_revert(
                key == GuiKeys::Q ? DialogTrigger::CLOSE_WINDOW
                                  : DialogTrigger::REVERT_TO_BLANK);
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
        // Defect-resolution modal: route the lowercased key to the series'
        // own dispatcher (which acts only on the offered keys) and swallow
        // Esc entirely — see the DialogTrigger::DEFECT_RESOLUTION comment
        // in app_state.h for why this prompt has no Esc response.
        if (app.prompt.trigger == DialogTrigger::DEFECT_RESOLUTION) {
            // Ctrl+S saves and returns to the series unchanged. Every state
            // the series can be showing is a walkable defect, which is by
            // construction load-legal: the save writes a file that reloads
            // and re-walks its own series, so persisting mid-resolution is
            // safe. This is the plain-Ctrl+S path (save_ops.save); the modal
            // stays up and the walk continues from where it was.
            if (ctrl && !shift && !alt && key == GuiKeys::S) {
                save_ops.save();
                return;
            }
            // Ctrl+Q / Ctrl+W do not resolve the defect; they open the
            // close / revert prompt in its normal save/discard/cancel form
            // over the parked series (walkable defects are load-legal, so the
            // save option writes a loadable file that re-walks on reload).
            // Suspend the series (commit_context stays on defect_series and
            // drives both the resume origin and the same defect's
            // reappearance on cancel), dismiss this modal, and route to the
            // shared funnel. request_close_or_revert refuses re-entry while
            // a prompt is active, so the defect prompt is cleared first.
            if (ctrl && !shift && !alt &&
                (key == GuiKeys::Q || key == GuiKeys::W)) {
                app.defect_series.suspended_for_close = true;
                app.prompt.active = false;
                viewport.invalidate_all();
                prompt.request_close_or_revert(
                    key == GuiKeys::Q ? DialogTrigger::CLOSE_WINDOW
                                      : DialogTrigger::REVERT_TO_BLANK);
                return;
            }
            if (k != 0 && k != '\x1b') handle_defect_response(k);
            return;
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
    // always false in blank state (history is reset on revert).
    if (app.loading || audio.total_frames() <= 0) {
        if (ctrl && !shift && !alt && key == GuiKeys::Q) {
            prompt.request_close_or_revert(DialogTrigger::CLOSE_WINDOW);
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
    // Ctrl+Q / Ctrl+W (tear the edit down, then open the close / revert
    // prompt) exactly as they would with no drag in flight. A text drag
    // is not a navigation gesture, so it gets no bare-`s` carve-out.
    if (app.editor_text_drag.active) {
        const bool escape_hatch =
            key == GuiKeys::Escape ||
            (ctrl && !shift && !alt &&
             (key == GuiKeys::Q || key == GuiKeys::W));
        if (!escape_hatch) return;
        finalize_editor_text_drag();
        // fall through: the editor handler below runs Esc (cancel the
        // edit) or Ctrl+Q / Ctrl+W (tear the edit down, then the close /
        // revert prompt opens) exactly as with no drag in flight.
    }

    // Bottom-strip modal-editor gate. Modal surfaces are bottom-strip
    // surfaces — the two bottom-strip editors (the settings editor and the
    // bpm bracket editor) and the prompts (gated above); the top-strip
    // flag editor is deliberately non-modal. While a bottom-strip editor
    // is open, only the keys the editor itself consumes plus Esc, Ctrl+S,
    // and Ctrl+Q/W get through (modal_editor_key_blocked); everything
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

    // Ctrl+C while the tempo hover popup is showing copies the hovered
    // marker's effective tempo value (the pasteable "base" / "base*scale"
    // form the flag editor accepts) to the system clipboard, for pasting the
    // implied value of a pass or label ref into a neighbor's flag editor.
    // Placed below the prompt gate (line above returns while a modal is up)
    // and the two editor blocks (which return on their own Ctrl+C, keeping
    // the editor's copy-selection working while an editor owns input), so
    // reaching here means neither a modal nor an editor is active. Fires only
    // while the popup is visible, in which case copy_payload is non-empty.
    // Ctrl+Alt+C (render-view commit) carries Alt and is unaffected; Ctrl+C
    // was otherwise unbound globally.
    if (ctrl && !shift && !alt && key == GuiKeys::C &&
        app.hover_popup.visible) {
        gui.clipboard_set_text(app.hover_popup.copy_payload);
        return;
    }

    // Drag-modal input: a pointer drag owns the keyboard exactly as the
    // prompt and the text editors above do. While any drag gesture is in
    // flight, swallow every hotkey except the escape hatches — Esc stops
    // the gesture (cancel_active_drags), and Ctrl+Q / Ctrl+W cancel the
    // in-flight drag first (the same abandon Esc performs, since the
    // gesture is uncommitted) and then run the close / revert flow.
    // Cancelling before the prompt goes up is what keeps a dismissed
    // prompt from leaving a stale drag that commits on the next motion.
    // This single gate is why no downstream hotkey needs its own
    // drag guard: Tab, undo, `t`, and the rest never see a key mid-drag,
    // the sole exception being the playhead-scrub set-marker carve-out
    // below. The editor text-selection drag has its own modal gate above
    // the text-editor handlers; the four position drags here are mutually
    // exclusive with it.
    if (app.drag.active || app.trim_drag.active ||
        app.scroll_drag.active || app.playhead_drag.active) {
        if (key == GuiKeys::Escape) {
            cancel_active_drags();
            return;
        }
        if (ctrl && !shift && !alt && key == GuiKeys::Q) {
            cancel_active_drags();
            prompt.request_close_or_revert(DialogTrigger::CLOSE_WINDOW);
            return;
        }
        if (ctrl && !shift && !alt && key == GuiKeys::W) {
            cancel_active_drags();
            prompt.request_close_or_revert(DialogTrigger::REVERT_TO_BLANK);
            return;
        }
        // A playhead scrub is a navigation gesture, so it alone lets the
        // two set-marker actions through: bare `s` and Shift+S fall
        // through to the S handler below to drop a marker / phase reset
        // at the scrubbed playhead. Ctrl+S (save) stays swallowed, and
        // the position-editing drags (marker, trim, scroll) swallow
        // these too — dropping a marker mid-marker-drag would fight the
        // gesture. In render view the S key still falls through here but
        // the render-view authoring gate below drops it, so scrubbing a
        // render never authors. The four drag states are mutually
        // exclusive, so playhead_only is belt-and-suspenders that keeps
        // the intent explicit.
        const bool playhead_only =
            app.playhead_drag.active && !app.drag.active &&
            !app.trim_drag.active && !app.scroll_drag.active;
        if (!(playhead_only && !ctrl && !alt && key == GuiKeys::S)) {
            return;
        }
    }

    // Render-view input gate. Render view IS the read-only modality, so
    // render_view_key_blocked is read_only_key_blocked plus a small named
    // delta (full per-chord rationale at the predicate in
    // input_render_view.cpp). Render-view-specific ADMITS: r (toggle off),
    // Shift+Left/Right (prev/next render), Shift+Home/End (first/last,
    // clamped), Ctrl+Alt+C (commit). EXTRA BLOCKS on top of read-only: t
    // (S/T toggle), o (read-only flag), Ctrl+S (save; the save surface is
    // Ctrl+Alt+C), Shift+0..9 (playback speed), Ctrl+Tab / Ctrl+Shift+Tab
    // (A/B switch). Everything else — playback, the bare-key scrub / zoom /
    // follow / center / p sub-view toggle, Home/End, PageUp/PageDown paging,
    // Tab cycling, Ctrl+Q/W, the font-size step — follows the read-only gate.
    //
    // The archival dispatch chords (Ctrl+S save, Ctrl+E queue-add,
    // Ctrl+Alt+R/E/I render) are all absent — Ctrl+S is an EXTRA BLOCK, the
    // rest match no read-only allowlist predicate — and the BPM sweep fires
    // only from the BPM editor's Enter, which cannot be open here. With no
    // archival dispatch chord admitted and render-view entry gated on an idle
    // worker with nothing parked (handle_render_view_toggle), no batch can be
    // running or start while the view is up, so a batch completion — and its
    // terminal auto-open — only ever happens with render view closed.
    //
    // Shift+0..9 is an EXTRA BLOCK rather than a deferral because render-view
    // playback is pinned to 1x by toggle_playback's force_one_x (the audible
    // result must match the rendered warp, not the warp scaled by an extra
    // factor); admitting the chord would let a press change the entry's
    // playback speed and then have the next Space press silently snap it
    // back to 1x. Read-only mode has no such pin, so it admits Shift+0..9.
    if (app.render_view.enabled && render_view_key_blocked(key, mods)) {
        return;
    }

    // Per-tab read-only keyboard gate. Mirrors the render-view gate
    // above structurally: a permitted-keys allowlist that filters out
    // every authoring chord while admitting navigation, playback,
    // view-switching, the close-prompt routing, and the bare-o
    // toggle-off escape chord. Only runs when render-view is off and
    // the active tab's ViewState carries read_only = true. Render-view
    // is its own read-only modality that supersedes everything else,
    // so this gate sits second.
    //   - Bare o                 → toggle read-only off (escape chord)
    //   - Space                  → playback toggle
    //   - Left/Right (no mods)   → playhead-by-pixel scrub
    //   - Shift+Left/Right       → playhead-by-samples scrub
    //   - Shift+0..9             → select playback speed
    //   - Home/End (no mods)     → playhead to trim region bounds
    //   - PageUp/PageDown        → viewport step scroll by the Alt-wheel
    //     (no mods)                step. Pure navigation, same family as
    //                              the scrub and Home/End entries.
    //   - Up/Down (no mods)      → zoom in/out
    //   - =/- (no mods)          → zoom symbol-key alias
    //   - Ctrl+Shift+=/-         → step GUI font size (display preference,
    //                              not an authoring mutation)
    //   - 0 (no mods)            → fit ↔ snap-zoom toggle
    //   - f (no mods)            → follow mode toggle
    //   - c (no mods)            → center+snap-zoom on playhead
    //   - t (no mods)            → S/T sub-view toggle
    //   - p (no mods)            → W/P sub-view toggle
    //   - Tab/Shift+Tab/IsoLeftTab → cycle marker focus
    //   - Ctrl+Tab               → switch A/B tab (the other escape)
    //   - Ctrl+Shift+Tab         → march paired tabs in lockstep
    //   - Esc                    → top-level no-op
    //   - Ctrl+Q / Ctrl+W        → close-prompt routing
    //   - Ctrl+S                 → save. Deliberately admitted: save is a
    //                              whole-application persistence action
    //                              (save_ops.save writes the marker
    //                              sidecars and .settings), and admitting
    //                              it from a locked tab is what lets
    //                              gesture-owned state changed there —
    //                              the read-only flag itself, trim, view
    //                              state, font size, playback speed —
    //                              reach disk; the bare-o handler comment
    //                              below already records that the flag is
    //                              silently persisted on Ctrl+S. It is
    //                              not an authoring mutation of the
    //                              locked tab: the marker-editing chords
    //                              stay blocked, so the sidecar bytes
    //                              reflect only authoring done where it
    //                              was legal (possibly the other,
    //                              unlocked tab).
    // Authoring-mutation chords are BLOCKED at this gate, not admitted for a
    // deeper refusal: the marker / tempo / phase-reset drop / nudge /
    // status-toggle chords, the trim gestures (x / Shift+x), Delete, the
    // propagate copy/paste (Ctrl+P and the Ctrl+Alt+P pair), and undo/redo
    // (Ctrl+Z / Ctrl+Shift+Z) all drop here. The deeper owner refusals stay
    // as backstops for the paths the keyboard gate does not cover: the Delete
    // handler's own read-only branch (mouse-adjacent marker delete) and
    // do_undo / do_redo's target-tab peek (a history entry that targets the
    // other, writable tab — now reached by Ctrl+Tab to that tab first, ruled
    // acceptable for gate legibility; full rationale at read_only_key_blocked
    // in input_key_dispatch.cpp).
    if (!app.render_view.enabled && active_view_state(app).read_only &&
        read_only_key_blocked(key, mods)) {
        return;
    }

    // Target-view keyboard authoring is fully unblocked.
    // Every binding source view honors runs in target view too; the
    // input-to-source-frame boundary translation lives at the individual
    // handlers (drop_marker_at_playhead, handle_trim_*, nudge_*, etc.).

    // Bare `t` toggles view-domain (S ↔ T). Placed before the marker /
    // phase reset edit handlers so the toggle wins over any future
    // bare-t binding; placed after the prompt / editor / render-view
    // / queue gates so those still own the keyboard when active.
    // Render-view drops bare `t` via the gate above (target view is
    // unreachable from render-view; render-view exits via `r`).
    if (key == GuiKeys::T && !ctrl && !shift && !alt) {
        handle_active_audio_view_toggle();
        return;
    }

    // Bare `o` toggles the active tab's read-only flag. Always admitted
    // by the read-only allowlist above (the locked-out user must be
    // able to unlock) and dropped by the render-view allowlist (the
    // flag is irrelevant while render-view is its own read-only
    // modality). Pure view-state mutation: not undoable, not dirty;
    // silently persisted on Ctrl+S. The bottom-strip dim update lands
    // through invalidate_timestamp_area, which covers the A/B tab
    // letter glyph.
    if (key == GuiKeys::O && !ctrl && !shift && !alt) {
        ViewState& vs = active_view_state(app);
        vs.read_only = !vs.read_only;
        viewport.invalidate_timestamp_area();
        return;
    }

    // Esc cancels an in-flight render / queued batch.
    if (handle_escape_cancels(key)) return;

    // Ctrl+Q: quit (via unsaved-work dialog when dirty).
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        prompt.request_close_or_revert(DialogTrigger::CLOSE_WINDOW);
        return;
    }

    // Ctrl+W: revert to blank state (via unsaved-work dialog when dirty).
    if (ctrl && !shift && !alt && key == GuiKeys::W) {
        prompt.request_close_or_revert(DialogTrigger::REVERT_TO_BLANK);
        return;
    }

    // Render-trigger chords: Ctrl+E queue-add, Ctrl+Alt+R/E/I render,
    // Ctrl+Alt+C commit.
    if (handle_render_dispatch_keys(key, mods)) return;

    // Space / Return / KpEnter is modifier-independent.
    if (is_play_pause_key(key)) {
        // Target-view playback gating: refuse Space-to-play while a
        // target render is in flight (current is stale by
        // definition). Space-to-stop is still honored — if playback
        // happened to be running before an edit, the trigger() helper
        // already froze it, so playback.is_playing() is false in
        // practice. The empty-target-buffer case (no successful target
        // render yet in this session) is also refused so the
        // user can't play stale source-domain samples through a
        // target-view binding. Source view AND render view fall through
        // unchanged: render view plays its own loaded render buffer, not
        // target_buffer, so this in-flight / empty-buffer target gating must
        // not apply to it. render_view.enabled overrides active_audio_view
        // here, matching the "actually in target view" idiom in
        // playback_lifecycle (active_audio_view == 'T' && !render_view.enabled).
        if (app.active_audio_view == 'T' && !app.render_view.enabled &&
            !playback.is_playing()) {
            if (target_render.is_updating()) return;
            if (app.target_buffer_frames <= 0) return;
        }
        playback_lifecycle.toggle_playback();
        return;
    }

    // Shift+<digit> selects a playback speed. Shift+0 is 1.00, Shift+1
    // is 0.10, Shift+9 is 0.90. Applies immediately whether or not
    // playback is active — the audio callback picks up the new atomic
    // on the next buffer. Each preset is read from kPlaybackSpeedPresets
    // (the shared source of truth, indexed by digit), so the dispatch and
    // the .settings reader's acceptance check can never diverge.
    if (shift && !ctrl && !alt) {
        switch (key) {
        case GuiKeys::Digit0: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[0]); return;
        case GuiKeys::Digit1: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[1]); return;
        case GuiKeys::Digit2: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[2]); return;
        case GuiKeys::Digit3: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[3]); return;
        case GuiKeys::Digit4: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[4]); return;
        case GuiKeys::Digit5: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[5]); return;
        case GuiKeys::Digit6: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[6]); return;
        case GuiKeys::Digit7: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[7]); return;
        case GuiKeys::Digit8: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[8]); return;
        case GuiKeys::Digit9: playback_lifecycle.set_playback_speed(kPlaybackSpeedPresets[9]); return;
        default: break;
        }
    }

    // Bare 0 toggles between fit-file and the snap zoom level.
    // From fit-file → kSnapZoomLevel (the 2.4 s snap level, centered on
    // playhead via apply_zoom_change's numeric branch). From any numeric
    // level → fit-file. So `0` toggles between fit-file and the 2.4 s snap
    // level; C remains the direct snap-and-center gesture. Digits 1..9 are
    // intentionally unbound.
    if (!ctrl && !alt && !shift && key == GuiKeys::Digit0) {
        if (app.zoom_level == kFitFileLevel) {
            viewport.apply_zoom_change(kSnapZoomLevel);
        } else {
            viewport.apply_zoom_change(kFitFileLevel);
        }
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

    // Plain `r` toggles render analysis mode (see handle_render_view_toggle).
    if (handle_render_view_toggle(key, mods)) return;

    // The platform boundary case-folds letters and delivers the
    // unshifted GuiKey, so a Shift+letter press arrives as the lowercase
    // GuiKeys::* with mods.shift set — disambiguate via the `shift` bool.
    if (key == GuiKeys::S && !alt) {
        if (ctrl && !shift)              save_ops.save();
        else if (!ctrl && !shift &&
                 app.active_markers_view == 'P') phase_resets.drop_phase_reset_at_playhead();
        else if (!ctrl && shift &&
                 app.active_markers_view == 'W') warpops.drop_marker_at_playhead();
        else if (!ctrl && !shift &&
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
        warpops.toggle_inherits();
        return;
    }
    // Ctrl+D: toggle disabled (warp + phase reset). Plain `d` and Shift+D are unbound.
    if (key == GuiKeys::D && ctrl && !alt && !shift) {
        if (app.active_markers_view == 'P') phase_resets.toggle_phase_reset_disabled();
        else                        warpops.toggle_disabled();
        return;
    }
    if (key == GuiKeys::Delete && !ctrl && !alt) {
        // Delete acts on the group named by last_sel_group. With
        // a trim boundary last-selected, clear the selected bound(s) and
        // leave markers untouched; otherwise the marker-delete runs.
        if (app.last_sel_group == LastSelGroup::Trim) {
            delete_selected_trim();
            return;
        }
        if (!app.render_view.enabled && active_view_state(app).read_only) return;
        if (app.active_markers_view == 'P') {
            phase_resets.delete_selected_phase_reset();
            return;
        }
        if (shift) warpops.force_delete_selected_marker();
        else       warpops.delete_selected_marker();
        return;
    }

    // Tab family: Ctrl+Tab / Ctrl+Shift+Tab switch tabs; Tab / Shift+Tab /
    // IsoLeftTab cycle marker focus.
    if (handle_tab_switch_keys(key, mods)) return;

    // Tempo nudge. Ctrl+Up / Ctrl+Down only. Bare `=` / `-` were the
    // previous binding; they now zoom (see below) so the keyboard has
    // a symbol-key alias for the bare Up/Down zoom chord. No view guard
    // here — adjust_tempo returns at once unless the warp view is active,
    // so Ctrl+arrows are an inert (still consumed) no-op in phase-reset view.
    if (ctrl && !shift && !alt && key == GuiKeys::Up) {
        warpops.adjust_tempo(+0.01); return;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::Down) {
        warpops.adjust_tempo(-0.01); return;
    }
    if (key == GuiKeys::Equal && !shift && !ctrl && !alt) {
        viewport.zoom_in(); return;
    }
    if (key == GuiKeys::Minus && !shift && !ctrl && !alt) {
        viewport.zoom_out(); return;
    }

    // Ctrl+Shift+= / Ctrl+Shift+- step the GUI font size by one point. A
    // display-preference gesture, the sibling of the Shift+digit
    // playback-speed step: silent (no stderr per press, so held-key repeat
    // does not spam the log), deliberately no undo-history entry and no
    // target render (font_size is not engine input and not authoring state),
    // persisted on the next Ctrl+S through the existing .settings writer. The
    // clamp is constructive, GTK-picker style: std::clamp into [6, 72] so a
    // fractional legacy value loaded from .settings steps to the exact bound,
    // then further steps no-op there. When the step would not change the
    // value (already at a bound) the chord is a consumed silent no-op — no
    // invalidate, no rebuild. On a real change the same live sequence the
    // resize path uses runs in order: assign app.font_size,
    // set_gui_font_size_pt, full-window invalidate_all, then the resize-path
    // geometry-and-cache rebuild. Alt is excluded from the guard. Shift+=
    // arrives as GuiKeys::Equal with mods.shift set, the same level-0-keysym
    // convention the Shift+Semicolon settings opener and the bare Equal/Minus
    // zoom aliases rely on.
    if ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
        ctrl && shift && !alt) {
        const double delta = (key == GuiKeys::Equal) ? +1.0 : -1.0;
        const double next  = std::clamp(app.font_size + delta, 6.0, 72.0);
        if (next == app.font_size) return;
        app.font_size = next;
        set_gui_font_size_pt(next);
        viewport.invalidate_all();
        paint_handler.on_resize(app.width, app.height);
        return;
    }

    // x sets the begin trim at the playhead and autosets end half of the
    // visible span away.
    // Shift+x clears both bounds. The end bound keeps its mouse operations
    // (Ctrl+drag single, Ctrl+Shift+drag pair, select+Delete).
    // Plain Ctrl+x is cut (text_editor.cpp) and stays unbound here.
    if (!ctrl && !shift && !alt && key == GuiKeys::X) {
        handle_trim_set_begin_autoset();
        return;
    }
    if (shift && !ctrl && !alt && key == GuiKeys::X) {
        handle_trim_clear_both();
        return;
    }

    // `:` opens the settings prompt in the bottom strip. Keyboard-only
    // (no click analogue). The active-editor block at the top of on_key
    // routes subsequent keystrokes; opening here just primes the State.
    // The settings editor is a modal bottom-strip surface: stop playback
    // at its open. Space is inside the modal blocked set, so playback
    // cannot restart until the editor closes.
    if (key == GuiKeys::Semicolon && shift && !ctrl && !alt) {
        playback_lifecycle.stop_playback_if_playing();
        settings_editor.open();
        return;
    }

    // Render-view list navigation:
    // Shift+Left / Shift+Right -> previous / next render, with wraparound
    // Shift+Home / Shift+End -> first / last render, clamped, no wraparound
    // Outside render-view these chords fall through to the source-view handlers
    // below (see handle_render_view_nav).
    if (handle_render_view_nav(key, mods)) return;

    // Ctrl+Left / Ctrl+Right: nudge the last-selected group by one pixel of
    // time. Routes like Delete and Ctrl+wheel — a last-selected trim bound
    // (Trim group) nudges the bound; otherwise the marker/phase-reset nudge
    // runs. Refused in render-view on the trim route, mirroring the Ctrl+wheel
    // trim path's render-view refusal.
    if (ctrl && !shift && !alt && key == GuiKeys::Left) {
        if (app.last_sel_group == LastSelGroup::Trim) {
            if (app.render_view.enabled) return;
            if ((app.trim_begin_selected && app.trim.has_begin) ||
                (app.trim_end_selected && app.trim.has_end)) {
                nudge_selected_trim(-1);
                return;
            }
        }
        if (app.active_markers_view == 'P') phase_resets.nudge_selected_phase_resets(-1);
        else                        warpops.nudge_selected_markers(-1);
        return;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::Right) {
        if (app.last_sel_group == LastSelGroup::Trim) {
            if (app.render_view.enabled) return;
            if ((app.trim_begin_selected && app.trim.has_begin) ||
                (app.trim_end_selected && app.trim.has_end)) {
                nudge_selected_trim(+1);
                return;
            }
        }
        if (app.active_markers_view == 'P') phase_resets.nudge_selected_phase_resets(+1);
        else                        warpops.nudge_selected_markers(+1);
        return;
    }

    // PageUp / PageDown: step the viewport back / forward by exactly the
    // Alt-wheel step (samples_visible / 10). PageUp goes back, PageDown
    // forward. Source-view only — the render-view allowlist above excludes
    // them.
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
    // — never fall through into a bare binding (e.g. Ctrl+Shift+Alt+E
    // must not toggle end-time via GuiKeys::E).
    if (!ctrl && !shift && !alt) {
        handle_plain_bare_keys(key);
    }
}

void GuiInputHandler::cycle_marker_focus_with_recenter(bool forward) {
    if (forward) selection.select_next_marker();
    else         selection.select_prev_marker();

    int64_t src_sample = 0;
    if (app.last_sel_group == LastSelGroup::Trim) {
        // The cycle landed on a trim bound. Recenter on its source frame; the
        // center below is shared with the marker path, so Tab centers onto a
        // trim bound exactly as it does onto a marker.
        if (app.last_selected_trim == 'B') {
            if (!app.trim.has_begin) return;
            src_sample = app.trim.begin_frame;
        } else if (app.last_selected_trim == 'E') {
            if (!app.trim.has_end) return;
            src_sample = app.trim.end_frame;
        } else {
            return;
        }
    } else {
        const int idx = app.last_selected_marker;
        if (idx < 0) return;
        if (app.active_markers_view == 'P') {
            // Render-view recenters on the render_view display stores
            // (authored-domain positions); authoring recenters on the
            // live store.
            const auto& tv = app.render_view.enabled
                ? app.render_view.phase_resets
                : app.phaseresetmarkers.markers();
            if (idx >= static_cast<int>(tv.size())) return;
            src_sample = tv[idx].time_frame;
        } else {
            const auto& mv = app.render_view.enabled
                ? app.render_view.warp_markers
                : app.warpmarkers.markers();
            if (idx >= static_cast<int>(mv.size())) return;
            src_sample = mv[idx].time_frame;
        }
    }
    // Mapped views: forward-translate the marker's source-frame through
    // the display context (target view's live map; render view's snapshot
    // map) so the playhead lands on the marker's
    // displayed position; the viewport recenter below also uses this
    // displayed value via center_viewport_on_playhead.
    int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
    // Playhead domain clamp: Tab mirrors move_playhead_to exactly — both
    // read live_total_frames, which reports the active display context's
    // domain total (active_display_context, gui_display_context.h, is the
    // shared domain source) — so Tab and bare Left/Right — which route
    // through move_playhead_to's clamp — cannot disagree about the same
    // endpoint. Tab onto trim end — legal at total — rests at total - 1.
    {
        const int64_t live_total = live_total_frames(app, audio);
        if (sample < 0) sample = 0;
        if (live_total > 0 && sample >= live_total) sample = live_total - 1;
    }

    playback_lifecycle.stop_playback_if_playing();

    // Capture the old playhead pixel-x before mutating, for the
    // no-scroll invalidation branch below.
    const double old_px = playhead_pixel_x(app, audio);
    const int64_t old_vp = app.viewport_start_sample;

    // Set the cursor directly — no move_playhead_to, which would scroll
    // the viewport a second time before centering. Mirror move_playhead_to's
    // scanner-sync invariant: when the scanner is inactive its sample
    // tracks the cursor (Tab always runs with playback stopped, so the
    // scanner is inactive here, but keep the guard for symmetry).
    app.playhead_cursor_sample = sample;
    if (!app.playhead_scanner_active) {
        app.playhead_scanner_sample = sample;
    }

    // Center the viewport on the focused marker at the current zoom — Tab
    // leaves the zoom level alone. center_viewport_on_playhead is the SOLE
    // viewport write in this path: it reads the cursor we just set and scrolls
    // once to center it, emitting one coherent set of waveform + top-strip
    // damage against the final viewport.
    viewport.center_viewport_on_playhead();

    // center_viewport_on_playhead only invalidates when the viewport
    // actually moved. At end-of-file (viewport already clamped) it does
    // not move, so the cursor's column change still needs its own
    // invalidation — mirror move_playhead_to's no-scroll branch. When the
    // viewport did move, the playhead columns are already inside the
    // waveform-area damage center emitted, so only invalidate columns in
    // the unmoved case to avoid a redundant rect.
    if (app.viewport_start_sample == old_vp) {
        const double new_px = playhead_pixel_x(app, audio);
        viewport.invalidate_playhead_columns(old_px, new_px);
    }
    viewport.invalidate_timestamp_area();

    // Discrete jump: render the waveform synchronously and publish the
    // displayed fingerprint now, so this tick's stem/flag caches rebuild
    // once against the final viewport instead of blinking across the
    // async worker's rebuild window. The worker stays the path for
    // continuous gestures; we just don't route this one-shot jump
    // through it.
    paint_handler.force_synchronous_waveform_rebuild();
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
    // Strict modifier matching: each wheel chord is an exact match.
    if (ctrl && !shift && !alt) {
        // Ctrl+wheel: when the begin trim bound is last-selected, move the end
        // bound. Otherwise nudge the focused warp marker's tempo. Refused in
        // render-view and read-only (both paths — the trim-end move was
        // read-only-mobile while trim was one unified setting across both
        // tabs; with per-tab trim that rationale is gone, so the bound
        // refuses exactly like the marker tempo nudge, a silent no-op).
        if (app.render_view.enabled) return;
        if (app.last_sel_group == LastSelGroup::Trim &&
            app.last_selected_trim == 'B' &&
            app.trim.has_begin && app.trim.has_end) {
            if (active_view_state(app).read_only) return;
            const int sr = audio.sample_rate();
            if (audio.total_frames() <= 0 || sr <= 0) return;
            const double spp = current_samples_per_pixel(app, audio);
            if (spp <= 0.0) return;
            // Pixel-column-anchored end-move, marker-identical to the
            // nudges (the derivation and the exact-painted-move rationale
            // live at nudge_selected_markers): read the end bound's
            // currently painted column, step it by the wheel's
            // whole-column step, and commit that column's time — source
            // view: viewport start plus column times samples-per-pixel;
            // target view: the column's target-domain time inverse-mapped
            // through the cached map — through snap_authored_frame
            // (inside authored_frame_at_column), so the stored bound is a
            // whole source frame. The step keeps its samples_visible /
            // kTrimEndWheelDivisor magnitude, expressed as whole pixel
            // columns per detent so each detent's painted move is exact
            // and no sub-column residue accumulates across detents. The
            // end bound clamps to its own absolute walls — floor 0,
            // ceiling the end wall at frame EOF exactly (end-at-EOF is a
            // valid render); plain integer compares, the load
            // guard's own comparison, applied AFTER the column snap so
            // the walls win over the pixel grid. There is no
            // partner wall: the end bound crosses the begin bound freely
            // and the begin bound is untouched here. The zero floor is the
            // walls' lower end, kept for representability — a negative
            // position is unrepresentable in the authored frame form the
            // .settings file persists.
            const int64_t step = std::max<int64_t>(
                1, samples_visible(app, audio) / kTrimEndWheelDivisor);
            const int64_t step_cols = std::max<int64_t>(
                1, static_cast<int64_t>(std::nearbyint(
                       static_cast<double>(step) / spp)));
            const int64_t dcols =
                (button == GuiMouseButton::WheelUp ? -step_cols : +step_cols) *
                count;
            const std::vector<WarpFrameMapSegment> no_map;
            const auto& map = (app.active_audio_view == 'T')
                ? target_view_warp_frame_map_cached(
                      app, sr,
                      static_cast<long>(audio.total_frames())).warp_frame_map
                : no_map;
            const int c = painted_column_of_source_frame(
                app, audio, static_cast<double>(app.trim.end_frame), map);
            int64_t v = authored_frame_at_column(
                app, audio, c + static_cast<int>(dcols), map);
            if (v < 0) v = 0;
            const int64_t end_wall = audio.total_frames();
            if (v > end_wall) v = end_wall;
            app.trim.end_frame = v;
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            target_render.trigger();
            // Trim commit site (see handle_trim_set_autoset in
            // input_trim.cpp): each wheel frame's end-bound move is its own
            // commit, so a move across the begin bound opens the defect
            // series on the next tick.
            app.defect_series.pending_validation = PendingValidation::Commit;
            return;
        }
        if (active_view_state(app).read_only) return;
        // Tempo nudge applies to warp markers only — phase-reset mode has no
        // tempo. Mirror the Ctrl+Up / Ctrl+Down keyboard guard so Ctrl+wheel
        // is a no-op here instead of nudging the warp marker that happens to
        // sit at the phase-reset selection's index, which silently corrupted
        // an unrelated warp marker and fired a spurious target render.
        if (app.active_markers_view == 'P') return;
        const double delta =
            (button == GuiMouseButton::WheelUp ? -0.01 : +0.01) *
            static_cast<double>(count);
        warpops.adjust_tempo(delta);
        return;
    }
    if (alt && !ctrl && !shift) {
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

// Coalesced wheel entry point. The platform delivers one of these per
// pointer frame carrying the net detent count (>= 1), instead of pumping a
// WheelUp/WheelDown through on_button_press once per detent. The gating here
// mirrors on_button_press's wheel-relevant guards (prompt / editor modals,
// loading, active drags, area hit-test) so a wheel event is swallowed in
// exactly the same situations as before; the wheel branch was identical in
// the render-view and source-view arms of on_button_press, so a single
// shared handler covers both.
void GuiInputHandler::on_wheel(GuiMouseButton dir, int count, int x, int y,
                               GuiInputState mods) {
    if constexpr (kDebugPerf) {
        app.last_input_event_time = std::chrono::steady_clock::now();
    }
    if (app.prompt.active) return;
    // Only the bottom-strip modal surfaces swallow the wheel (the settings
    // editor and the BpmBracket reuse of top_flag_editor, the same predicate
    // the keyboard gate uses). The top-strip flag editor is deliberately
    // NOT modal — commands punch through it on the keyboard, so wheel zoom,
    // Alt+wheel pan, and Ctrl+wheel authoring punch through it too.
    if (modal_bottom_strip_editor_active()) return;
    if (app.loading || audio.total_frames() <= 0) return;
    // A wheel event during ANY active drag is ignored, matching
    // on_button_press and the keyboard's drag-modal gate. The playhead
    // scrub is included: the keyboard gate swallows every authoring chord
    // mid-scrub, so the wheel's authoring routes (Ctrl+wheel tempo, the
    // trim-end move) and viewport changes must not slip through either.
    if (app.drag.active) return;
    if (app.trim_drag.active) return;
    if (app.scroll_drag.active) return;
    if (app.playhead_drag.active) return;

    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const bool inside_waveform =
        x >= area.x && x < area.x + area.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top =
        x >= top.x && x < top.x + top.w &&
        y >= top.y && y < top.y + top.h;

    handle_wheel(dir, count, mods.ctrl, mods.shift, mods.alt, inside_waveform, inside_top);
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
        case text_editor::KeyAction::PasteRequested:
            text_editor::replace_selection(s, gui.clipboard_get_text());
            return true;
        default:
            return false;
    }
}

std::expected<std::vector<WarpFrameMapSegment>, std::string>
validate_target_view_entry(const std::vector<GuiWarpMarker>& markers,
                           double scale, int sample_rate, long total_frames,
                           bool has_trim_begin, int64_t trim_begin_frame,
                           bool has_trim_end,   int64_t trim_end_frame) {
    // Contract and caller list in input_handler.h: this is the shared
    // entry-half validity predicate for target view — the keyboard S → T
    // toggle below and GuiFileLoader::load_file's active_audio_view=T
    // restore gate on exactly this walk.
    auto resolved = resolve_warp_markers_for_render(
        slice_to_warp_markers(markers), sample_rate);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    auto r = build_warp_frame_map(*resolved, scale, sample_rate, total_frames);
    if (!r) return std::unexpected(std::move(r.error()));
    if (has_trim_begin || has_trim_end) {
        if (auto v = validate_trim_frames(
                has_trim_begin, trim_begin_frame,
                has_trim_end,   trim_end_frame,
                static_cast<int64_t>(total_frames), *r);
            !v) {
            return std::unexpected(std::move(v.error()));
        }
    }
    return std::move(*r);
}

void GuiInputHandler::handle_active_audio_view_toggle() {
    // Audio must be loaded — `t` is a silent no-op in blank state.
    // (The blank/loading guard near the top of on_key already covers
    // this, but the helper is defensive in case future callers reach
    // it from elsewhere.)
    if (audio.total_frames() <= 0) return;
    if (app.active_audio_view == 'S' &&
        !target_render.target_view_available()) {
        return;
    }

    // Build the current warp_frame_map from the live warp marker store +
    // settings trim. Same resolve-then-build pipeline the render
    // pipeline runs, so the visible deformity in target view matches
    // what the engine would emit.
    // Trim is a render-time cut, not a view-time concept: build_warp_frame_map
    // builds the WHOLE-song map, and the toggle translates source-frame
    // viewport / playhead / total_frames across the whole song against it —
    // see the matching comment in paint_handler.cpp's per-paint recompute.
    //
    // Validity gate, entry half: entering target view (S → T) with a
    // render-invalid marker state (first-marker grammar, dangling label
    // ref, tie) or a set trim failing validate_trim_frames stays in source
    // view — target view is blocked while invalid, and target playback
    // must never audition an unrenderable window. The predicate is
    // validate_target_view_entry (definition above): resolve, then build,
    // then — trim bound set — validate_trim_frames against the full
    // trim-off map just built (the same construction the target-view cache
    // holds); it returns that map, so entry validation and the translation
    // map below are one build. GuiFileLoader::load_file gates its
    // active_audio_view=T restore on the SAME predicate — a load restore
    // and a keystroke entry block identically. The defect-resolution
    // series is the surface: it opens on the modeled defect, the user
    // resolves, and pressing `t` again enters (no auto-proceed). The
    // error-notice popup remains only for failures the series does not
    // model — after the enumerator that is effectively the engine-metadata
    // and non-positive-tempo-product class, unreachable from
    // program-written input — kept as the loud backstop.
    //
    // Leaving target view (T → S) never gates: the trim column is masked
    // off from the predicate call and a resolve/build failure is ignored —
    // the kick-back path (enforce_target_view_validity) rides this same
    // T → S branch while the map is invalid, so the exit falls back to the
    // empty map — identity translation for the playhead/viewport — and
    // always succeeds (a valid map with an invalid trim keeps the built
    // map for the exit translation, exactly as before).
    const bool entering_target = (app.active_audio_view == 'S');
    std::vector<WarpFrameMapSegment> warp_frame_map;
    auto entry = validate_target_view_entry(
        app.warpmarkers.markers(), app.engine_settings.scale,
        audio.sample_rate(), static_cast<long>(audio.total_frames()),
        entering_target && app.trim.has_begin, app.trim.begin_frame,
        entering_target && app.trim.has_end,   app.trim.end_frame);
    if (entry) {
        warp_frame_map = std::move(*entry);
    } else if (entering_target) {
        if (!open_defect_series(/*commit_context=*/false)) {
            prompt.open_error_notice(std::move(entry.error()));
        }
        return;
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

    int64_t new_playhead = app.playhead_cursor_sample;

    bool going_to_target = false;
    if (app.active_audio_view == 'S') {
        // S → T: forward-translate the playhead. The deformed-domain
        // total is derived from the warp_frame_map cache by live_total_frames,
        // so the post-flip viewport math needs no cached total here.
        const double tph = map_source_to_target(
            static_cast<size_t>(app.playhead_cursor_sample < 0
                                ? 0 : app.playhead_cursor_sample),
            warp_frame_map);
        new_playhead = static_cast<int64_t>(std::nearbyint(tph));

        app.active_audio_view = 'T';
        going_to_target = true;
    } else {
        // T → S: inverse-translate the playhead.
        const double sph = map_target_to_source(
            static_cast<size_t>(app.playhead_cursor_sample < 0
                                ? 0 : app.playhead_cursor_sample),
            warp_frame_map);
        new_playhead = static_cast<int64_t>(std::nearbyint(sph));

        app.active_audio_view              = 'S';
    }

    // The S/T toggle translates the active tab's live playhead across the
    // domain flip; the inactive tab's stored playhead must translate too, or
    // a later Ctrl+Tab loads a stale-domain position that gets read in the
    // new domain. Same warp_frame_map, same direction as the active
    // translation.
    // To keep the inactive tab's playhead at the same on-screen column after
    // the toggle (matching the active-tab invariant above), the viewport is
    // shifted by the same delta as the playhead rather than translated
    // independently — translating both endpoints separately through the
    // nonlinear warp_frame_map was what caused the slide. At a fixed numeric zoom
    // the samples-per-pixel is domain-invariant, so equal sample deltas map
    // to equal pixel columns; at fit-file zoom the viewport is re-clamped to
    // zero on the next tab activation anyway, so the shifted value is
    // harmless. The active tab's own slot is left stale on purpose — it
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
    const int64_t dest_total = live_total_frames(app, audio);
    const auto clamp_dest = [dest_total](int64_t s) -> int64_t {
        if (s < 0) return 0;
        if (dest_total > 0 && s >= dest_total) return dest_total - 1;
        return s;
    };
    new_playhead = clamp_dest(new_playhead);

    {
        ViewState& other = (app.active_tab_view == 'B') ? app.tab_a : app.tab_b;

        const auto xlate = [&](int64_t s) -> int64_t {
            const size_t q = static_cast<size_t>(s < 0 ? 0 : s);
            const double r = going_to_target
                ? map_source_to_target(q, warp_frame_map)
                : map_target_to_source(q, warp_frame_map);
            return static_cast<int64_t>(std::nearbyint(r));
        };

        const int64_t other_old_ph = other.playhead_cursor_sample;
        const int64_t other_new_ph = clamp_dest(xlate(other_old_ph));
        other.playhead_cursor_sample = other_new_ph;
        other.viewport_start_sample += (other_new_ph - other_old_ph);
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

// Validity gate, kick-back half (the entry half lives at the top of
// handle_active_audio_view_toggle). Runs once per event-loop tick from
// main.cpp's on_tick, before the tick's waveform dirty-detect, so the
// kick lands on the first tick after an invalidating edit — the same
// beat as the edit's own repaint, never lazily later. The memoized cache
// makes the steady-state check a generation compare; the rebuild that
// actually detects the failure is the one the invalidating edit forces.
// The kick lands T → S first, THEN the defect-resolution series opens on
// the modeled defect (the popup remains only for the non-modeled class);
// run_commit_validation runs earlier on the same tick, so a commit's own
// modal wins the beat and this gate defers behind it like any prompt.
void GuiInputHandler::enforce_target_view_validity() {
    if (app.loading || audio.total_frames() <= 0) return;
    if (app.render_view.enabled) return;   // render-view lists come from
                                           // files, not the live store
    if (app.active_audio_view != 'T') return;
    // Another prompt owning the bottom strip means no edit can be in
    // flight (prompts are modal); defer the kick a tick rather than
    // clobber it. The error state persists in the cache, so the kick
    // fires on the first tick after dismissal.
    if (app.prompt.active) return;
    const TargetWarpFrameMapCache& c = target_view_warp_frame_map_cached(
        app, audio.sample_rate(), static_cast<long>(audio.total_frames()));
    // Two kick conditions, checked in order: an invalid marker state (the
    // cached build error) and — the trim column of the same gate — a set
    // trim that fails validate_trim_frames against the cached map (the
    // cache's map IS the full map, built trim-off from the live store), the
    // live sample rate, and total frames. Either way target playback never
    // auditions an unrenderable window. The error string is the owner's
    // verbatim (parser's or trimmer's), consumed only by the popup backstop
    // when the defect series does not model the failure.
    std::string err;
    if (!c.build_error.empty()) {
        // Copy before toggling: the T → S toggle path re-resolves and can
        // touch the cache the reference points into.
        err = c.build_error;
    } else if (app.trim.has_begin || app.trim.has_end) {
        if (auto v = validate_trim_frames(
                app.trim.has_begin, app.trim.begin_frame,
                app.trim.has_end,   app.trim.end_frame,
                static_cast<int64_t>(audio.total_frames()),
                c.warp_frame_map);
            !v) {
            err = std::move(v.error());
        }
    }
    if (err.empty()) return;
    handle_active_audio_view_toggle();   // T → S: unconditional, identity
                                         // fallback for the playhead math
    if (!open_defect_series(/*commit_context=*/false)) {
        // Backstop for failures the series does not model — effectively
        // the engine-metadata / non-positive-tempo-product class,
        // unreachable from program-written input.
        prompt.open_error_notice(err);
    }
}
