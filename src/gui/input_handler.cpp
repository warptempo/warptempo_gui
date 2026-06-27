#include "input_handler.h"

#include "paint_handler.h"
#include "render.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "text_editor.h"
#include "time_format.h"
#include "frame_map_view.h"
#include "warpmarkers.h"
#include "frame_map.h"

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

// Keyboard input handler. Method bodies are byte-identical to the
// lambdas they replaced in main.cpp (set_on_key at the original main.cpp:1588;
// run_render_batch at the original main.cpp:1539). The only changes are:
//
//   - Capture-by-reference of `app`, `audio`, `gui`, `playback`, `viewport`,
//     `selection`, `undo`, `warpops`, `phase resets`, `flag_editor`,
//     `render_view`, `active_views`, `playback_lifecycle`, `save_ops`, `prompt`
//     is now reference-member access on `this`. The std::function forwarder
//     pattern (clear_hover_popup, stop_playback_if_playing, save_markers,
//     request_close_or_revert, prompt_activate_response, toggle_playback,
//     set_playback_speed) was retired in favor of direct struct method
//     calls (viewport.clear_hover_popup,
//     playback_lifecycle.stop_playback_if_playing /
//     toggle_playback / set_playback_speed, save_ops.save,
//     prompt.request_close_or_revert / activate_response).
//   - Forwarder lambdas in main.cpp (do_undo, do_redo, recompute_dirty,
//     push_undo_both, select_*, clear_selection, set_single_selection,
//     toggle_selection_membership, move_playhead_*, zoom_*, scroll_viewport,
//     center_viewport_on_playhead, invalidate_*, trim_*_sample, drop_*,
//     delete_*, force_delete_*, toggle_inherits/disabled/begin_time/end_time,
//     adjust_tempo, clear_trim, nudge_*, jump_*, drop_phase_reset_at_playhead,
//     toggle_phase_reset_*, detect_phase_resets, clear_all_phase_resets,
//     enter_*_edit, commit_*_edit, exit_top_flag_edit_no_commit,
//     bulk_clear_*_values, enter_bpm_mode, exit_bpm_mode,
//     toggle_active_markers_view, load_render_view_at, restore_source_audio,
//     stash_render_view_selection_to_active_entry,
//     enumerate_render_view_list, write_rendersettings_for) were rewritten
//     to direct method calls on the appropriate operation struct ref.
//   - Free function calls (do_render, iter_popup_eligible_marker,
//     effective_disabled, compute_base_tempo_scale, text_editor::*,
//     std::filesystem::*, std::printf, etc.) keep their original spelling.
//     compute_base_tempo_scale + BaseTempoScale moved out of main.cpp's
//     anonymous namespace into input_handler.h so this TU can reach them;
//     render_bpm_sweep() is the sole caller.

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

    // Render-view input gate. While render-view is active
    // only keys driving navigation / playback / exit / commit are
    // honored; every authoring key is silently dropped so a stray
    // press can't mutate state through a swapped-out view.
    // Allowlist:
    //   - r (no mods)            → toggle render-view off
    //   - Shift+Left/Right       → previous/next render
    //   - Ctrl+Alt+C             → commit displayed render's markers
    //   - Space                  → playback toggle
    //   - Left/Right (no mods)   → playhead-by-pixel scrub
    //   - Home/End (no mods)     → playhead to absolute file bounds
    //                              (render-view has no trim — see
    //                              viewport.cpp trim_range)
    //   - Esc                    → top-level no-op
    //   - p (no mods)            → toggle warp/phase reset sub-view
    //   - Tab / Shift+Tab /      → cycle marker focus (no A/B tabs in
    //     IsoLeftTab               render-view, so Ctrl+Tab / Ctrl+Shift+Tab
    //                              stay no-ops; cycles the render-domain
    //                              collection per the active p-state)
    //   - Ctrl+Q / Ctrl+W        → close-prompt routing
    //   - Up/Down (no mods)      → zoom in/out
    //   - =/- (no mods)          → zoom in/out symbol-key alias
    //   - 0 (no mods)            → fit ↔ max-zoom-in toggle
    //   - f (no mods)          → follow mode toggle
    //   - c (no mods)            → center+max-zoom-in on playhead
    //
    // Note on the absent disk-save-shape keys: Ctrl+S (save),
    // Ctrl+E (queue-add), Ctrl+Alt+R (single render to source dir),
    // Ctrl+Alt+E (render queue), and Ctrl+Alt+I (render iterations) are
    // intentionally NOT on the allowlist. The BPM sweep render is likewise
    // unreachable here — it fires only from the BPM editor's Enter, which
    // cannot be open in render-view. Rendering and queue-add are disk-write
    // operations against the source folder (the same shape as
    // Ctrl+S), and render-view is read-only with respect to
    // authoring state and source-dir writes — admitting any of
    // them would mutate state through a swapped-out view. The
    // batch-render auto-open path in dispatch_next_batch_entry
    // relies on this invariant: because none of the render-
    // shaped keys can fire while render-view is on, the auto-
    // open call cannot be reached with render-view already
    // active, and so the new render_view.auto_open_batch_at_
    // first_file method handles only the "render-view is off"
    // case.
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
    //   - Home/End (no mods)     → playhead to trim region bounds
    //   - Up/Down (no mods)      → zoom in/out
    //   - =/- (no mods)          → zoom symbol-key alias
    //   - 0 (no mods)            → fit ↔ max-zoom-in toggle
    //   - f (no mods)            → follow mode toggle
    //   - c (no mods)            → center+max-zoom on playhead
    //   - t (no mods)            → S/T sub-view toggle
    //   - p (no mods)            → W/P sub-view toggle
    //   - Tab/Shift+Tab/IsoLeftTab → cycle marker focus
    //   - Ctrl+Tab               → switch A/B tab (the other escape)
    //   - Ctrl+Shift+Tab         → march paired tabs in lockstep
    //   - Esc                    → top-level no-op
    //   - Ctrl+Q / Ctrl+W        → close-prompt routing
    //   - Ctrl+P                 → copy phase reset placements
    //                              (read-only: writes only to the
    //                              session-only phase_reset_clipboard)
    // Every authoring chord (drop, drag, delete, label/tempo edit,
    // trim set/unset, disabled-flag toggle, paste, iteration / BPM
    // mode, render dispatch, queue add, undo/redo) is silently
    // dropped at this gate.
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
    // on the next buffer.
    if (shift && !ctrl && !alt) {
        switch (key) {
        case GuiKeys::Digit0: playback_lifecycle.set_playback_speed(1.0f); return;
        case GuiKeys::Digit1: playback_lifecycle.set_playback_speed(0.1f); return;
        case GuiKeys::Digit2: playback_lifecycle.set_playback_speed(0.2f); return;
        case GuiKeys::Digit3: playback_lifecycle.set_playback_speed(0.3f); return;
        case GuiKeys::Digit4: playback_lifecycle.set_playback_speed(0.4f); return;
        case GuiKeys::Digit5: playback_lifecycle.set_playback_speed(0.5f); return;
        case GuiKeys::Digit6: playback_lifecycle.set_playback_speed(0.6f); return;
        case GuiKeys::Digit7: playback_lifecycle.set_playback_speed(0.7f); return;
        case GuiKeys::Digit8: playback_lifecycle.set_playback_speed(0.8f); return;
        case GuiKeys::Digit9: playback_lifecycle.set_playback_speed(0.9f); return;
        default: break;
        }
    }

    // Bare 0 toggles between fit-file and max-zoom-in (kMinNumericLevel).
    // From fit-file → kSnapZoomLevel (the 2.4 s snap level, centered on
    // playhead via apply_zoom_change's numeric branch). From any numeric
    // level → fit-file. So `0` toggles between fit-file and the 2.4 s snap
    // level; C remains the always-direct max-in gesture. Digits 1..9 are
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
        else if (!ctrl && app.active_markers_view == 'P') phase_resets.drop_phase_reset_at_playhead();
        else if (!ctrl && shift)         warpops.drop_marker_at_playhead();
        else if (!ctrl && !shift)        warpops.drop_copy_previous_at_playhead();
        return;
    }
    // n / Shift+N: warp-only authoring (pass drop / toggle inherit). No
    // phase-reset equivalent, so this no-ops in P view (matching the
    // !ctrl && !alt gate the S handler above uses for its W-view branches).
    if (key == GuiKeys::N && !ctrl && !alt) {
        if (app.active_markers_view == 'P') return;
        if (shift) warpops.toggle_inherits();
        else       warpops.drop_inherit_marker_at_playhead();
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

    // x sets the begin trim at the playhead and autosets end 5 s away.
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
    if (key == GuiKeys::Semicolon && shift && !ctrl && !alt) {
        settings_editor.open();
        return;
    }

    // `j` jumps the selected set to the playhead, anchored on
    // last_selected_marker. All-or-nothing clamp check.
    if (key == GuiKeys::J && !shift && !ctrl && !alt) {
        if (app.active_markers_view == 'P') phase_resets.jump_phase_reset_selection_to_playhead();
        else                        warpops.jump_selection_to_playhead();
        return;
    }

    // Render-view list navigation: Shift+Left/Right (wraparound) and
    // Shift+Home/End (clamp). Outside render-view these chords fall through to
    // the source-view handlers below (see handle_render_view_nav).
    if (handle_render_view_nav(key, mods)) return;

    // Ctrl+Left / Ctrl+Right: nudge selected markers by one pixel.
    if (ctrl && !shift && !alt && key == GuiKeys::Left) {
        if (app.active_markers_view == 'P') phase_resets.nudge_selected_phase_resets(-1);
        else                        warpops.nudge_selected_markers(-1);
        return;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::Right) {
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

    const int sr = audio.sample_rate();
    int64_t src_sample = 0;
    if (app.last_sel_group == LastSelGroup::Trim) {
        // The cycle landed on a trim bound. Recenter on its source frame; the
        // center below is shared with the marker path, so Tab centers onto a
        // trim bound exactly as it does onto a marker.
        if (app.last_selected_trim == 'B') {
            if (!app.trim.has_begin) return;
            src_sample = static_cast<int64_t>(std::nearbyint(
                app.trim.begin_seconds * static_cast<double>(sr)));
        } else if (app.last_selected_trim == 'E') {
            if (!app.trim.has_end) return;
            src_sample = static_cast<int64_t>(std::nearbyint(
                app.trim.end_seconds * static_cast<double>(sr)));
        } else {
            return;
        }
    } else {
        const int idx = app.last_selected_marker;
        if (idx < 0) return;
        if (app.active_markers_view == 'P') {
            // Render-view recenters on the displayed render-domain phase
            // resets; authoring recenters on the live store.
            const auto& tv = app.render_view.enabled
                ? app.render_view.phase_resets
                : app.phase_reset_markers.markers();
            if (idx >= static_cast<int>(tv.size())) return;
            src_sample = static_cast<int64_t>(std::nearbyint(
                tv[idx].time_seconds * static_cast<double>(sr)));
        } else {
            const auto& mv = app.render_view.enabled
                ? app.render_view.markers
                : app.warpmarkers.markers();
            if (idx >= static_cast<int>(mv.size())) return;
            src_sample = static_cast<int64_t>(std::nearbyint(
                mv[idx].time_seconds * static_cast<double>(sr)));
        }
    }
    // Target view: forward-translate marker's source-frame to active-
    // domain (target-frame) so the playhead lands on the marker's
    // displayed position; the viewport recenter below also uses this
    // target-frame value via center_viewport_on_playhead.
    const int64_t sample = source_frame_to_active_domain(app, audio, src_sample);

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
        // render-view and read-only (same guards as Ctrl+Up/Down).
        if (app.render_view.enabled) return;
        if (active_view_state(app).read_only) return;
        if (app.last_sel_group == LastSelGroup::Trim &&
            app.last_selected_trim == 'B' &&
            app.trim.has_begin && app.trim.has_end) {
            const int sr = audio.sample_rate();
            if (audio.total_frames() <= 0 || sr <= 0) return;
            const double sr_d = static_cast<double>(sr);
            const int64_t step = std::max<int64_t>(
                1, samples_visible(app, audio) / kTrimEndWheelDivisor);
            const int64_t dlt =
                (button == GuiMouseButton::WheelUp ? -step : +step) * count;
            int64_t end_active = source_frame_to_active_domain(app, audio,
                static_cast<int64_t>(std::nearbyint(app.trim.end_seconds * sr_d)));
            end_active += dlt;
            const int64_t begin_active = source_frame_to_active_domain(app, audio,
                static_cast<int64_t>(std::nearbyint(app.trim.begin_seconds * sr_d)));
            const double spp = current_samples_per_pixel(app, audio);
            const int64_t eps = static_cast<int64_t>(
                std::nearbyint(static_cast<double>(kMarkerHitHalfPx) * spp));
            if (end_active < begin_active + eps) end_active = begin_active + eps;
            // Viewport clamp: keep the end bound on-screen (last fully-visible
            // pixel) so the wheel can't push it offscreen, where its precise
            // location would be hidden. end_active and the bounds are both
            // active-domain, so clamp directly.
            const auto vb = viewport_marker_bounds(app, audio);
            if (end_active > vb.second) end_active = vb.second;
            // EOF-eps: the end stops eps short of the live EOF (was a flush
            // clamp to lt), matching the marker and trim-drag convention.
            // Applied after the viewport clamp so trim validity wins on-screen.
            const int64_t lt = live_total_frames(app, audio);
            if (end_active > lt - eps) end_active = lt - eps;
            // Trim is excluded from undo/redo history. Re-enable this capture
            // and the push below to restore trim undo.
            // SettingsSnapshot pre = capture_current_settings(app);
            app.trim.end_seconds = snap_to_timestamp_grid(static_cast<double>(
                active_domain_to_source_frame(app, audio, end_active)) / sr_d);
            // undo.push_settings_undo(std::move(pre));
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            target_render.trigger();
            return;
        }
        // Tempo nudge applies to warp markers only — phase-reset mode has no
        // tempo. Mirror the Ctrl+Up / Ctrl+Down keyboard guard so Ctrl+wheel
        // is a no-op here instead of nudging the warp marker that happens to
        // sit at the phase-reset selection's index, which silently corrupted
        // an unrelated warp marker and fired a spurious target render.
        if (app.active_markers_view == 'P') return;
        // Dismiss an active flag edit so the tempo change is visible.
        if (text_editor::is_active(app.top_flag_editor)) {
            handle_top_flag_editor_key(GuiKeys::Escape, GuiInputState{});
        }
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
    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        return;
    }
    if (app.loading || audio.total_frames() <= 0) return;
    // A wheel event during an active drag is ignored, matching on_button_press.
    if (app.drag.active) return;
    if (app.trim_drag.active) return;
    if (app.scroll_drag.active) return;

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

void GuiInputHandler::handle_active_audio_view_toggle() {
    // Audio must be loaded — `t` is a silent no-op in blank state.
    // (The blank/loading guard near the top of on_key already covers
    // this, but the helper is defensive in case future callers reach
    // it from elsewhere.)
    if (audio.total_frames() <= 0) return;

    // Build the current frame_map from the live warp marker store +
    // settings trim. Same resolve-then-build pipeline the render
    // pipeline runs, so the visible deformity in target view matches
    // what the engine would emit. An empty / failed build degenerates
    // to identity (the helpers return src_frame unchanged), which is
    // the right fallback when there are no qualifying markers.
    MapBuildInput tmin;
    tmin.markers      = resolve_markers_for_render(slice_to_warp_markers(app.warpmarkers.markers()));
    tmin.scale        = app.engine_settings.scale;
    tmin.sample_rate  = audio.sample_rate();
    tmin.total_frames = static_cast<long>(audio.total_frames());
    // Trim is a render-time cut, not a view-time concept. The toggle
    // translates source-frame viewport / playhead / total_frames across
    // the WHOLE song, so the frame_map must too — see the matching
    // comment in paint_handler.cpp's per-paint recompute. Passing the
    // the project trim here would shrink the segment list to the
    // exposition's source-frame range and identity-extrapolate the
    // post-exposition tail from the wrong tgt_frame anchor.
    tmin.has_trim_begin = false;
    tmin.trim_begin_sec = 0.0;
    tmin.has_trim_end   = false;
    tmin.trim_end_sec   = 0.0;
    std::vector<FrameMapSegment> tmap;
    auto r = build_maps(tmin);
    if (r) {
        const MapBuildResult& tmres = *r;
        tmap.reserve(tmres.frame_map.size());
        for (const auto& s : tmres.frame_map) {
            tmap.push_back(FrameMapSegment{s.src_frame, s.tgt_frame});
        }
    }

    // Playback is disabled in target view (Space is in the gate's
    // blocked set). Stop on every toggle so playback never finds
    // itself chasing a playhead in the other domain. Mirrors the
    // viewport-mutator pattern of "stop_playback_if_playing before
    // mutating playhead state".
    playback_lifecycle.stop_playback_if_playing();

    // Anchor the toggle on the playhead's pre-flip screen-pixel column.
    // Compute ph_px now, translate the playhead through the frame_map,
    // then derive the new viewport_start so the translated playhead
    // occupies the same column. zoom_level is preserved across the
    // flip (the visible time span will differ by the frame_map's net
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
        // total is derived from the frame_map cache by live_total_frames,
        // so the post-flip viewport math needs no cached total here.
        const double tph = map_source_to_target(
            static_cast<size_t>(app.playhead_cursor_sample < 0
                                ? 0 : app.playhead_cursor_sample), tmap);
        new_playhead = static_cast<int64_t>(std::nearbyint(tph));

        app.active_audio_view = 'T';
        going_to_target = true;
    } else {
        // T → S: inverse-translate the playhead.
        const double sph = map_target_to_source(
            static_cast<size_t>(app.playhead_cursor_sample < 0
                                ? 0 : app.playhead_cursor_sample), tmap);
        new_playhead = static_cast<int64_t>(std::nearbyint(sph));

        app.active_audio_view              = 'S';
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
    // the frame_map hash all flip, so the displayed plate must change. Render it
    // synchronously and publish the displayed fingerprint now, so the
    // bottom-strip S/T indicator and the playhead column do not repaint a frame
    // ahead of the deformed waveform. The plate is built from source audio plus
    // the live frame_map, independent of the target render buffer, so this is
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
