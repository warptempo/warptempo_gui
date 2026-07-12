#include "input_handler.h"

#include "warp_frame_map_view.h"
#include "warpmarkers.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Render-view input handlers: the render-view-only blocks of on_key /
// on_button_press / on_motion, each a GuiInputHandler method declared in
// input_handler.h and invoked from input_handler.cpp's event entry points.
// Helpers shared with the source-view paths live in headers both TUs
// reach: is_play_pause_key (gui_input.h) and the sweep_select_interval
// template (app_state.h).

// Render-view key gate, expressed as read-only mode plus exactly one named
// delta so the two gates cannot drift. Render view IS the read-only modality:
// everything it permits, it permits by deferring to read_only_key_blocked,
// except for a small set it admits on top (its own navigation and commit
// chords) and a small set it blocks on top (chords read-only honors but that
// would misbehave against the swapped-out display axis). Returns true when the
// key is NOT permitted, so the on_key caller drops it with an early return.
//
// Delta over read-only:
//   ADMITS (render-view-specific, checked first):
//     - r                      → toggle render-view off
//     - Shift+Left/Right       → previous/next render (deliberately shadows
//                                the authoring playhead-by-samples scrub,
//                                which is unreachable in render view — render
//                                view browses entries, it does not scrub by
//                                samples)
//     - Shift+Home/Shift+End   → first/last render, clamped
//     - Ctrl+Alt+C             → commit the displayed render
//   EXTRA BLOCKS (read-only honors these; render view does not):
//     - t                      → S/T audio-view toggle; render view owns the
//                                display axis, so the toggle is meaningless
//     - o                      → read-only flag toggle; irrelevant while
//                                render view is its own read-only modality
//     - Ctrl+S                 → save; render view's save surface is Ctrl+Alt+C
//     - Shift+0..9             → playback speed; target view's deeper refusal
//                                gates on active_audio_view being T, which
//                                render view does not guarantee, so admitting
//                                it would change the entry's playback speed
//     - Ctrl+Tab / Ctrl+Shift+Tab → A/B tab switch; the entry is bound to its
//                                commit tab and the tab slots hold the stashed
//                                authoring view state for exit restore
//   Everything else defers to read_only_key_blocked(key, mods).
//
// Two chords the deferral net-admits that render view honors safely (both
// route through the same display-domain-aware handlers the already-admitted
// chords use): PageUp/PageDown page the viewport against live_total_frames,
// and Alt+Tab / Alt+IsoLeftTab reach the same marker-focus cycle bare Tab
// does (read-only's tab-cycle predicate is not alt-strict).
bool GuiInputHandler::render_view_key_blocked(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // 1. Render-view-specific ADMITS.
    const bool is_r =
        (key == GuiKeys::R && !ctrl && !shift && !alt);
    const bool is_nav =
        ((key == GuiKeys::Left || key == GuiKeys::Right) &&
         shift && !ctrl && !alt);
    const bool is_render_view_nav_jump =
        ((key == GuiKeys::Home || key == GuiKeys::End) &&
         shift && !ctrl && !alt);
    const bool is_commit =
        (ctrl && alt && !shift &&
         key == GuiKeys::C);
    if (is_r || is_nav || is_render_view_nav_jump || is_commit) {
        return false;
    }

    // 2. Render-view EXTRA BLOCKS on top of read-only.
    const bool is_sub_audio_toggle =
        (key == GuiKeys::T && !ctrl && !shift && !alt);
    const bool is_read_only_toggle =
        (key == GuiKeys::O && !ctrl && !shift && !alt);
    const bool is_save =
        (ctrl && !shift && !alt && key == GuiKeys::S);
    const bool is_speed_select =
        (key >= GuiKeys::Digit0 && key <= GuiKeys::Digit9 &&
         shift && !ctrl && !alt);
    const bool is_ctrl_tab =
        (ctrl && !shift && key == GuiKeys::Tab);
    const bool is_ctrl_shift_tab =
        (ctrl && shift && key == GuiKeys::Tab);
    if (is_sub_audio_toggle || is_read_only_toggle || is_save ||
        is_speed_select || is_ctrl_tab || is_ctrl_shift_tab) {
        return true;
    }

    // 3. Everything else follows the read-only gate.
    return read_only_key_blocked(key, mods);
}

// Plain `r` toggles render analysis mode. Returns false if the chord is not
// the bare R so the caller falls through; otherwise it is fully handled
// (including the no-op guards) and returns true. Source audio must be loaded;
// otherwise silent no-op (nothing to base the renders folder lookup on).
// Entry is gated on an idle render worker with nothing parked (one refusal
// line, rationale at the gate); exit stops any render work (rationale at
// the toggle-off arm). Toggle-on enumerates the renders folder and loads
// either the last-displayed
// render (if its path is still in the list) or the first entry; an empty
// enumeration aborts the toggle. Iter/BPM modes persist across render-view
// enter/leave; they are inert inside render view (the input gate drops i and
// M; iteration paint gates on !render_view.enabled), and Ctrl+Alt+C is the
// only forced reset.
bool GuiInputHandler::handle_render_view_toggle(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    if (!(key == GuiKeys::R && !ctrl && !shift && !alt)) return false;
    if (app.source_audio_path.empty()) return true;
    if (app.loading) return true;
    if (!app.render_view.enabled) {
        // Entry gate: render view is incompatible with running render work.
        // While archival work is running or parked, entry is REFUSED, not
        // cancelled — the running batch or parked command may be
        // irreplaceable queued work, and r is a browse gesture, so a slip of
        // the hand must not destroy renders. Esc is the explicit cancel (Esc,
        // then r). Honest drain window: right after an Esc, queue_running
        // stays set until the cancelled worker's callback finalizes, so an
        // immediate r can refuse again; pressing r a moment later enters.
        // The predicate is deliberately archival-only: a busy target PREVIEW
        // does not refuse entry (a preview is derived, loses nothing, and the
        // entry path already cancels it through cancel_in_flight_update).
        if (app.queue_running || app.pending_archival.armed) {
            std::fprintf(stderr,
                "warptempo_gui: render view refused: a render is running "
                "(Esc cancels it, then r)\n");
            return true;
        }
        std::vector<AppState::RenderViewEntry> list =
            render_view.enumerate_render_view_list();
        if (list.empty()) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: no renders found "
                "under %s/renders/\n",
                std::filesystem::path(app.source_audio_path)
                    .parent_path().string().c_str());
            return true;
        }
        // Migrate persisted selection from
        // the prior render-view session (still on the old
        // app.render_view.list) into the freshly enumerated
        // list, keyed by wav_path. Entries that disappeared
        // since last session simply lose their persisted state;
        // newly added entries start with default-empty
        // persistence (no match → load_render_view_at clears).
        if (!app.render_view.list.empty()) {
            std::map<std::string,
                AppState::RenderViewEntry*> prior;
            for (auto& pe : app.render_view.list) {
                prior[pe.wav_path.string()] = &pe;
            }
            for (auto& ne : list) {
                auto it = prior.find(ne.wav_path.string());
                if (it == prior.end()) continue;
                const auto& src = *it->second;
                ne.state           = src.state;
                ne.persisted_size  = src.persisted_size;
                ne.persisted_mtime = src.persisted_mtime;
            }
        }
        int target = 0;
        if (!app.render_view.last_path.empty()) {
            for (size_t i = 0; i < list.size(); ++i) {
                if (list[i].wav_path.string() ==
                    app.render_view.last_path) {
                    target = static_cast<int>(i);
                    break;
                }
            }
        }
        app.render_view.list      = std::move(list);
        // Iter/BPM modes persist across render-view enter/leave. The flags are
        // inert inside render view (input gate drops i/M; paint gates on
        // !render_view.enabled) and are restored on exit. Ctrl+Alt+C is now
        // the only forced reset.
        app.render_view.enabled    = true;
        // The entry's persisted W/P mode is applied per-entry at load
        // (load_render_view_at reads it from the entry's .settings before the
        // stat-gated selection restore); the global active_markers_view flag
        // is the live carrier of the mode between entries. Toggle-on is a
        // render-view ENTRY: the browse position inherits from the live
        // view (directly from target view, through the S→T toggle's anchor
        // math from source view).
        if (!render_view.load_render_view_at(target, /*entering=*/true)) {
            app.render_view.enabled = false;
            app.render_view.list.clear();
        }
    } else {
        // Leaving render view stops anything render-related. Nothing can be
        // running by construction — entry is gated on an idle worker with
        // nothing parked, the render-view key allowlist admits no dispatch
        // chord, and the view itself dispatches nothing — so this cancel is
        // the literal form of the leave-stops-renders rule and defense in
        // depth. cancel_archival_session also clears the pending target
        // preview, which is correct here — restore_source_view's target arm
        // re-derives through ensure_ready immediately after.
        cancel_archival_session();
        // Capture the just-viewed render's zoom/viewport/playhead and
        // stash its live selection onto the active entry, so the next
        // toggle-on can restore both (the selection gated by the wav's
        // stat tuple still matching). Not done on the Ctrl+Alt+C commit
        // path — the renders folder is wiped immediately after commit,
        // so the write would be lost. render_view.list is intentionally
        // NOT cleared here — re-entry migrates its persisted_* fields
        // into the freshly enumerated list.
        render_view.autosave_active_entry();
        render_view.restore_source_view();
        render_view.clear_snapshot_context();
        app.render_view.index             = -1;
    }
    return true;
}

// Render-view list navigation. Handles both Shift+Left/Right (wraparound) and
// Shift+Home / Shift+End (clamp to first/last, no wraparound). Each chord
// pre-refreshes the renders/ folder, stashes the outgoing entry, exits
// render-view gracefully if the folder is empty, then loads the target entry.
// Returns true from each handled path; false when neither chord matches (or
// render-view is off) so the caller falls through to the source-view playhead
// handlers.
bool GuiInputHandler::handle_render_view_nav(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    // Shift+Left / Shift+Right navigates the render-view
    // list with wraparound. Active only when render_view.enabled is
    // true; in source-view these chords fall through to the normal
    // playhead-by-pixel handler in the switch below. Wraparound:
    // Shift+Right past the end loops to index 0,
    // Shift+Left before index 0 loops to the last entry.
    //
    // Pre-nav refresh: re-enumerate the renders/ folder and merge
    // per-entry persisted state into the refreshed list before
    // computing the target index. New batch folders or wavs that
    // appeared since render-view was entered become visible; deleted
    // entries vanish. Empty-after-refresh exits render-view.
    if (app.render_view.enabled && shift && !ctrl && !alt &&
        (key == GuiKeys::Left || key == GuiKeys::Right)) {
        // Capture outgoing state and stash selection before refresh —
        // refresh_render_view_list may reorder/drop entries.
        // Re-navigating back later (in the same session) restores the
        // stashed selection: load_render_view_at loads the destination's
        // own persisted state if its stat tuple still matches; otherwise
        // it leaves the selection empty.
        render_view.autosave_active_entry();

        if (!render_view.refresh_render_view_list()) {
            // Renders folder is empty (e.g. user deleted it externally).
            // Exit render-view gracefully. Leaving render view stops any
            // render work first — see the toggle-off arm of
            // handle_render_view_toggle.
            cancel_archival_session();
            render_view.exit_render_view_and_clear();
            return true;
        }

        const int n = static_cast<int>(app.render_view.list.size());
        int next = app.render_view.index;
        if (key == GuiKeys::Left)  next = (next - 1 + n) % n;
        else                       next = (next + 1) % n;
        // Entry-to-entry NAVIGATION: the browsed position carries over
        // unchanged (both axes are target axes; runtime clamps own an
        // out-of-domain carry at first use).
        render_view.load_render_view_at(next, /*entering=*/false);
        return true;
    }

    // Shift+Home / Shift+End: jump render-view to first / last entry, clamped
    // (no wraparound — Shift+Home at index 0 stays at 0, Shift+End at the last
    // entry stays). Gated on app.render_view.enabled; outside render-view the
    // chord is a silent no-op (the bare-key switch at the bottom of this
    // function is modifier-strict). Same pre-nav refresh of the renders/
    // folder. The endpoint no-op must compare the displayed identity
    // (last_path, stamped only by a successful entry load), not just the
    // integer index: the pre-nav refresh may have clamped index onto a
    // different entry when the displayed wav vanished from disk, and the
    // fall-through load then reloads that slot so display and index agree
    // again.
    if (app.render_view.enabled && shift && !ctrl && !alt &&
        (key == GuiKeys::Home || key == GuiKeys::End)) {
        render_view.autosave_active_entry();

        if (!render_view.refresh_render_view_list()) {
            // Empty-folder exit stops render work first — see the toggle-off
            // arm of handle_render_view_toggle.
            cancel_archival_session();
            render_view.exit_render_view_and_clear();
            return true;
        }

        const int n = static_cast<int>(app.render_view.list.size());
        const int target = (key == GuiKeys::Home) ? 0 : (n - 1);
        if (target == app.render_view.index &&
            app.render_view.list[target].wav_path.string() ==
                app.render_view.last_path) return true;
        // Entry-to-entry NAVIGATION, same carry-over as Shift+Left/Right.
        render_view.load_render_view_at(target, /*entering=*/false);
        return true;
    }

    return false;
}

// Render-view mouse-press handler. Fully terminating: the on_button_press
// caller invokes this and returns. Left-click on a marker line (waveform), a
// flag rect (top strip), or a snapshot trim bound's stem or chip toggles /
// sets selection and jumps the playhead; left-click elsewhere positions the
// playhead (with playback stop) and clears the selection unless Shift is
// held. In P sub-view the top-strip click hits the snapshot's phase-reset
// chips through hit_test_flag, the same as a W flag click. Alt-exact presses
// inside the waveform arm the same viewport scroll-drag source view runs.
// Drag-create and top-strip playhead movement are silent no-ops so the
// read-only marker-state invariant is preserved. Recomputes the cheap
// geometry it needs; derives the modifier bools from `mods`.
void GuiInputHandler::handle_render_view_press(GuiMouseButton button, int x,
                                               int y, bool inside_top,
                                               bool inside_waveform,
                                               GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const GuiRect area = waveform_area(app);
    // Wheel events arrive via on_wheel (coalesced per pointer frame), not
    // here; a stray wheel button is caught by the Left-only gate below.
    if (button != GuiMouseButton::Left) return;
    // Top-strip clicks stop playback first: they can open the iter/
    // bpm/flag editors and continuing audio during text editing is
    // the wrong default. Waveform clicks keep playback alive — the
    // per-press reseek to the click sample happens at the playhead-
    // drag press sites below, gated on was_playing && sample !=
    // playhead_at_entry.
    const bool was_playing_rv = playback.is_playing();
    const int64_t playhead_at_entry_rv = app.playhead_cursor_sample;
    if (inside_top) playback_lifecycle.stop_playback_if_playing();
    // Alt+drag (exact) pans the viewport — the same stepped scroll-drag
    // source view arms (on_button_press's Alt-exact arm), regardless of
    // whether the press landed on a marker or trim stem, so a pan never
    // grabs anything. No-op in the top strip; the scroll happens on motion.
    // The scroll-drag only moves the viewport, so it is fine in render
    // view's read-only modality, and it deliberately does NOT override
    // follow mode: a pan during playback moves the view along with the
    // audio rather than signaling a stop.
    if (alt && !ctrl && !shift) {
        if (inside_waveform) {
            app.scroll_drag.active        = true;
            app.scroll_drag.last_x        = x;
            app.scroll_drag.accum_samples = 0.0;
        }
        return;
    }
    int hit = -1;
    if (inside_waveform)  hit = hit_test_marker_line(app, audio, x);
    else if (inside_top)  hit = hit_test_flag(app, audio, x, y);
    else                  return;
    // Snapshot trim bounds are mouse-pickable like the snapshot markers,
    // with source view's press priority: markers take priority on a shared
    // column, so the trim tests run only on a marker/flag miss — the stem
    // column in the waveform area, the chip rect first (falling back to the
    // stem column) in the top strip. Alt is reserved for panning and never
    // routes to trim, matching source view's trim_gesture filter. A hit
    // selects the bound and jumps the playhead to its displayed frame; no
    // drag ever starts — the bound is immutable in render view, exactly
    // like the snapshot markers.
    if (hit < 0 && !alt) {
        TrimHit th = TrimHit::None;
        if (inside_waveform) {
            th = hit_test_trim_boundary(app, audio, x);
        } else {
            // Top strip: the b/e chip rect is tested first, the stem column
            // fills in on a miss — the same order the source-view press uses.
            const TrimHit chip = hit_test_trim_chip(app, audio, x, y);
            th = (chip != TrimHit::None)
                     ? chip
                     : hit_test_trim_boundary(app, audio, x);
        }
        if (th != TrimHit::None) {
            // Select the bound via the helper source-view clicks use: a
            // plain click single-selects (this bound on, the other off,
            // marker selection dropped, group Trim — the same
            // exactly-one-thing-selected rule the marker-click arm below
            // and the Tab-stop walk follow); Shift adds to the selection,
            // exactly as source view's trim click passes shift through.
            select_trim_boundary(th, /*additive=*/shift);
            gui.invalidate_region(0, 0, app.width, app.height);
            // Jump the playhead to the bound's displayed frame: the
            // snapshot bound is an authored source frame, so
            // forward-translate through the display context (snapshot
            // map), the same route the marker click below takes.
            const int64_t src = (th == TrimHit::Begin)
                ? app.render_view.snapshot_trim_begin_frame
                : app.render_view.snapshot_trim_end_frame;
            viewport.move_playhead_to(
                source_frame_to_active_domain(app, audio, src));
            return;
        }
    }
    // Live selection lives in the global pair regardless of view domain.
    // active_markers_view tells us which marker list the indices map to.
    const bool sub_t = (app.active_markers_view == 'P');
    std::set<int>& sel = app.selected_markers;
    int& last_sel      = app.last_selected_marker;
    const int n = sub_t
        ? static_cast<int>(app.render_view.phase_resets.size())
        : static_cast<int>(app.render_view.warp_markers.size());
    if (hit >= 0 && hit < n) {
        if (shift) {
            auto it = sel.find(hit);
            if (it == sel.end()) {
                sel.insert(hit);
                last_sel = hit;
            } else {
                sel.erase(it);
                if (last_sel == hit) {
                    last_sel = sel.empty()
                        ? -1
                        : *sel.rbegin();
                }
            }
        } else {
            sel.clear();
            sel.insert(hit);
            last_sel = hit;
            // Fresh single-select drops any Tab-focused snapshot trim bound
            // (the bounds are cycle stops now), mirroring source view's
            // set_single_selection: exactly one thing stays selected.
            app.trim_begin_selected = false;
            app.trim_end_selected   = false;
            app.last_selected_trim  = 0;
        }
        gui.invalidate_region(0, 0, app.width, app.height);
        // The stores hold AUTHORED frames; the playhead lives on the
        // render's displayed axis (the full target axis), so
        // forward-translate through the display context (snapshot map).
        int64_t sample;
        if (sub_t) {
            sample = source_frame_to_active_domain(
                app, audio,
                app.render_view.phase_resets[hit].time_frame);
        } else {
            sample = source_frame_to_active_domain(
                app, audio,
                app.render_view.warp_markers[hit].time_frame);
        }
        viewport.move_playhead_to(sample);
        // Any waveform-area press starts a
        // playhead-drag gesture. Top-strip flag-click does not.
        if (inside_waveform) {
            if (was_playing_rv && sample != playhead_at_entry_rv) {
                playback_lifecycle.reseek_keeping_alive(sample);
            }
            if (was_playing_rv) app.follow_overridden_for_session = true;
            app.playhead_drag.active = true;
            app.playhead_drag.press_marker_idx = hit;
            app.playhead_drag.last_swept_sample = sample;
        }
        return;
    }
    // Empty-space click in the waveform area: clear the active
    // sub-view's selection (unless Shift) and move the playhead.
    // Also start a playhead-drag gesture so
    // the motion handler's snap logic kicks in.
    if (inside_waveform) {
        const bool had_trim_sel =
            app.trim_begin_selected || app.trim_end_selected;
        if (!shift &&
            (!sel.empty() || last_sel != -1 || had_trim_sel)) {
            sel.clear();
            last_sel = -1;
            // An empty-space click also drops a Tab-focused snapshot trim
            // bound, mirroring source view's clear_selection on an empty click.
            app.trim_begin_selected = false;
            app.trim_end_selected   = false;
            app.last_selected_trim  = 0;
            gui.invalidate_region(0, 0, app.width, app.height);
        }
        const double spp = current_samples_per_pixel(app, audio);
        int rel = x - area.x;
        if (rel < 0) rel = 0;
        if (rel >= area.w) rel = area.w - 1;
        const int64_t sample =
            app.viewport_start_sample +
            static_cast<int64_t>(std::nearbyint(rel * spp));
        viewport.move_playhead_to(sample);
        if (was_playing_rv && sample != playhead_at_entry_rv) {
            playback_lifecycle.reseek_keeping_alive(sample);
        }
        if (was_playing_rv) app.follow_overridden_for_session = true;
        app.playhead_drag.active = true;
        app.playhead_drag.press_marker_idx = -1;
        app.playhead_drag.last_swept_sample = sample;
    }
}

// Render-view motion handler with playhead-drag snap support: when a drag is
// in flight, snap the playhead to the visible sub-view's markers (3px epsilon),
// matching source-view's gesture, with Shift sweep-select across the dragged
// interval. Otherwise clear any hover popup: render view is read-only display
// with no hover popups (see the tail comment). Fully terminating; the on_motion
// caller returns after it.
void GuiInputHandler::handle_render_view_motion(int mouse_x, int mouse_y,
                                                GuiInputState mods) {
    if (app.playhead_drag.active) {
        viewport.clear_hover_popup();
        if (!mods.primary_button_held) {
            app.playhead_drag = PlayheadDragState{};
            return;
        }
        const int sr = audio.sample_rate();
        if (sr <= 0) return;
        const GuiRect area = waveform_area(app);
        const double spp = current_samples_per_pixel(app, audio);
        if (spp <= 0.0) return;
        const int hit = hit_test_marker_line(app, audio, mouse_x);
        int64_t new_playhead;
        if (hit >= 0) {
            // Authored-domain store position -> displayed axis, the same
            // translation the press handler applies.
            if (app.active_markers_view == 'P') {
                new_playhead = source_frame_to_active_domain(
                    app, audio,
                    app.render_view.phase_resets[hit].time_frame);
            } else {
                new_playhead = source_frame_to_active_domain(
                    app, audio,
                    app.render_view.warp_markers[hit].time_frame);
            }
        } else {
            int rel = mouse_x - area.x;
            if (rel < 0) rel = 0;
            if (rel >= area.w) rel = area.w - 1;
            new_playhead = app.viewport_start_sample +
                static_cast<int64_t>(std::nearbyint(rel * spp));
        }
        if (new_playhead != app.playhead_cursor_sample) {
            viewport.move_playhead_to(new_playhead);
        }
        // Live selection in render view. Same model as source view, written
        // through the global selection pair with a full-window invalidate to
        // match this path's existing selection writes.
        if (!mods.shift) {
            // A fresh single-select or an empty-drag clear also drops any
            // Tab-focused snapshot trim bound (the bounds are cycle stops
            // now), mirroring the press handler and source view.
            const bool had_trim_sel =
                app.trim_begin_selected || app.trim_end_selected;
            if (hit >= 0) {
                const bool already_single =
                    app.selected_markers.size() == 1 &&
                    *app.selected_markers.begin() == hit;
                if (!already_single || had_trim_sel) {
                    app.selected_markers.clear();
                    app.selected_markers.insert(hit);
                    app.last_selected_marker = hit;
                    app.trim_begin_selected = false;
                    app.trim_end_selected   = false;
                    app.last_selected_trim  = 0;
                    gui.invalidate_region(0, 0, app.width, app.height);
                }
            } else if (!app.selected_markers.empty() ||
                       app.last_selected_marker != -1 || had_trim_sel) {
                app.selected_markers.clear();
                app.last_selected_marker = -1;
                app.trim_begin_selected = false;
                app.trim_end_selected   = false;
                app.last_selected_trim  = 0;
                gui.invalidate_region(0, 0, app.width, app.height);
            }
        } else {
            // Endpoint add: unchanged hit-based pickup (3px epsilon).
            if (hit >= 0 &&
                hit != app.playhead_drag.press_marker_idx &&
                !app.selected_markers.count(hit)) {
                app.selected_markers.insert(hit);
                app.last_selected_marker = hit;
                gui.invalidate_region(0, 0, app.width, app.height);
            }
            // Interval sweep: add every marker the playhead PASSED
            // since the last motion event (point-sampling skipped
            // markers at fast pointer speeds). The stores hold AUTHORED
            // positions, so inverse-translate the displayed interval
            // endpoints through the display context (inverse-map through
            // the snapshot map) — the same shape source view's target
            // arm uses — so the interval compare runs in the stores' own
            // domain.
            const int64_t prev = app.playhead_drag.last_swept_sample;
            if (prev >= 0 && new_playhead != prev) {
                int64_t a = prev, b = new_playhead;
                const bool forward = (b >= a);
                if (!forward) std::swap(a, b);
                int64_t lo = active_domain_to_source_frame(app, audio, a);
                int64_t hi = active_domain_to_source_frame(app, audio, b);
                if (lo > hi) std::swap(lo, hi);
                const double lo_t = static_cast<double>(lo);
                const double hi_t = static_cast<double>(hi);
                const bool swept = (app.active_markers_view == 'P')
                    ? sweep_select_interval(
                          app, app.render_view.phase_resets,
                          lo_t, hi_t, forward,
                          app.playhead_drag.press_marker_idx)
                    : sweep_select_interval(
                          app, app.render_view.warp_markers,
                          lo_t, hi_t, forward,
                          app.playhead_drag.press_marker_idx);
                if (swept)
                    gui.invalidate_region(0, 0, app.width, app.height);
            }
        }
        // Keep the sweep anchor fresh on every motion event of the
        // drag, Shift or not (mirrors the source/target branch).
        app.playhead_drag.last_swept_sample = new_playhead;
        return;
    }
    // Render-view is read-only display, so no hover popup runs here. The
    // display markers carry bare label_refs with no label_def to resolve and
    // inherited tempo that doesn't exist on the render timeline, so
    // compute_hover_popup_text has nothing meaningful to report — the popup is
    // disabled outright. Clear any popup still showing from source view; on a
    // normal render-view hover this is a cheap no-op (entry already cleared it).
    (void)mouse_y;
    viewport.clear_hover_popup();
}
