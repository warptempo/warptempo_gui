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

// Render-view input handlers, lifted verbatim from input_handler.cpp's mega
// event handlers (on_key / on_button_press / on_motion). Each is a cohesive,
// behavior-preserving extraction of one render-view-only block; the call sites
// stay in input_handler.cpp and invoke these methods unchanged. The methods are
// declared on GuiInputHandler in input_handler.h, so this is a pure definition
// move. Two former file-local helpers they share with the staying code were
// promoted to headers so both TUs reach them: is_play_pause_key (gui_input.h)
// and the sweep_select_interval template (app_state.h).

// Render-view read-only allowlist as a predicate. While render-view is
// active only navigation / playback / exit / commit chords are honored;
// every authoring key is silently dropped. Returns true when the key is NOT
// permitted, so the on_key caller drops it with an early return. The
// permitted-chord classifiers are byte-for-byte the same set the inline gate
// used. See the call site in on_key for the per-chord allowlist rationale.
bool GuiInputHandler::render_view_key_blocked(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
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
    const bool is_playback = is_play_pause_key(key);
    const bool is_scrub =
        ((key == GuiKeys::Left || key == GuiKeys::Right) &&
         !ctrl && !shift && !alt);
    const bool is_jump =
        ((key == GuiKeys::Home || key == GuiKeys::End) &&
         !ctrl && !shift && !alt);
    const bool is_esc = (key == GuiKeys::Escape);
    const bool is_sub_view_toggle =
        (key == GuiKeys::P && !ctrl && !shift && !alt);
    const bool is_tab_cycle =
        (!ctrl && !alt &&
         (key == GuiKeys::Tab || key == GuiKeys::IsoLeftTab));
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    const bool is_ctrl_w =
        (ctrl && !shift && !alt && key == GuiKeys::W);
    const bool is_zoom =
        ((key == GuiKeys::Up || key == GuiKeys::Down) &&
         !ctrl && !shift && !alt);
    const bool is_zoom_symbol =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
         !ctrl && !shift && !alt);
    // Ctrl+Shift+=/- steps the GUI font size. Pure display state, the sibling
    // of the bare =/- zoom aliases this gate already admits; it mutates no
    // state through the swapped-out view, so render-view honors it.
    const bool is_font_size_step =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
         ctrl && shift && !alt);
    const bool is_zero =
        (key == GuiKeys::Digit0) && !ctrl && !shift && !alt;
    const bool is_follow =
        (key == GuiKeys::F && !ctrl && !shift && !alt);
    const bool is_center =
        (key == GuiKeys::C && !ctrl && !shift && !alt);
    return !(is_r || is_nav || is_render_view_nav_jump ||
             is_commit || is_playback ||
             is_scrub || is_jump || is_esc ||
             is_sub_view_toggle || is_tab_cycle ||
             is_ctrl_q || is_ctrl_w ||
             is_zoom || is_zoom_symbol || is_font_size_step ||
             is_zero || is_follow ||
             is_center);
}

// Plain `r` toggles render analysis mode. Returns false if the chord is not
// the bare R so the caller falls through; otherwise it is fully handled
// (including the no-op guards) and returns true. Source audio must be loaded;
// otherwise silent no-op (nothing to base the renders folder lookup on).
// Toggle-on enumerates the renders folder and loads either the last-displayed
// render (if its path is still in the list) or the first entry; an empty
// enumeration aborts the toggle. Iteration mode is forcibly disabled on entry;
// the prior value is not restored on toggle-off — the user re-enables it
// explicitly if desired.
bool GuiInputHandler::handle_render_view_toggle(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    if (!(key == GuiKeys::R && !ctrl && !shift && !alt)) return false;
    if (app.source_audio_path.empty()) return true;
    if (app.loading) return true;
    if (!app.render_view.enabled) {
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
        app.render_view.src_sr    = audio.sample_rate();
        app.render_view.src_total = audio.total_frames();
        app.render_view.list      = std::move(list);
        // Iter/BPM modes persist across render-view enter/leave. The flags are
        // inert inside render view (input gate drops i/M; paint gates on
        // !render_view.enabled) and are restored on exit. Ctrl+Alt+C is now
        // the only forced reset.
        app.render_view.enabled    = true;
        // Render-view shares the global active_markers_view
        // flag, so the user's chosen mode carries across the
        // view-domain transition without per-entry restore.
        if (!render_view.load_render_view_at(target)) {
            app.render_view.enabled = false;
            app.render_view.list.clear();
        }
    } else {
        // Capture the just-viewed render's zoom/viewport/playhead
        // before restoring source-audio state. Not done on the
        // Ctrl+Alt+C commit path — the renders folder is wiped
        // immediately after commit, so the write would be lost.
        if (app.render_view.index >= 0 &&
            app.render_view.index <
                static_cast<int>(app.render_view.list.size())) {
            render_view.write_rendersettings_for(
                app.render_view.list[app.render_view.index]);
        }
        // Stash the live selection onto
        // the active entry so the next toggle-on can restore
        // it (gated by the wav's stat tuple still matching).
        // render_view.list is intentionally NOT cleared here
        // — re-entry migrates its persisted_* fields into the
        // freshly enumerated list.
        render_view.stash_render_view_selection_to_active_entry();
        render_view.restore_source_audio();
        app.render_view.warp_markers.clear();
        app.render_view.phase_resets.clear();
        app.render_view.index             = -1;
        app.render_view.src_F_begin       = 0;
        app.render_view.src_F_end         = 0;
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
        if (app.render_view.index >= 0 &&
            app.render_view.index <
                static_cast<int>(app.render_view.list.size())) {
            render_view.write_rendersettings_for(
                app.render_view.list[app.render_view.index]);
        }
        // Stash the outgoing entry's
        // selection so re-navigating back later (in the same
        // session) restores it. load_render_view_at then loads
        // the destination's own persisted state if its stat tuple
        // still matches; otherwise leaves selection empty.
        render_view.stash_render_view_selection_to_active_entry();

        if (!render_view.refresh_render_view_list()) {
            // Renders folder is empty (e.g. user deleted it externally).
            // Exit render-view gracefully.
            render_view.exit_render_view_and_clear();
            return true;
        }

        const int n = static_cast<int>(app.render_view.list.size());
        int next = app.render_view.index;
        if (key == GuiKeys::Left)  next = (next - 1 + n) % n;
        else                       next = (next + 1) % n;
        render_view.load_render_view_at(next);
        return true;
    }

    // Shift+Home / Shift+End: jump render-view to first / last entry, clamped
    // (no wraparound — Shift+Home at index 0 stays at 0, Shift+End at the last
    // entry stays). Gated on app.render_view.enabled; outside render-view the
    // chord is a silent no-op (the bare-key switch at the bottom of this
    // function is modifier-strict). Same pre-nav refresh of the renders/
    // folder.
    if (app.render_view.enabled && shift && !ctrl && !alt &&
        (key == GuiKeys::Home || key == GuiKeys::End)) {
        if (app.render_view.index >= 0 &&
            app.render_view.index <
                static_cast<int>(app.render_view.list.size())) {
            render_view.write_rendersettings_for(
                app.render_view.list[app.render_view.index]);
        }
        render_view.stash_render_view_selection_to_active_entry();

        if (!render_view.refresh_render_view_list()) {
            render_view.exit_render_view_and_clear();
            return true;
        }

        const int n = static_cast<int>(app.render_view.list.size());
        const int target = (key == GuiKeys::Home) ? 0 : (n - 1);
        if (target == app.render_view.index) return true;
        render_view.load_render_view_at(target);
        return true;
    }

    return false;
}

// Render-view mouse-press handler. Fully terminating: the on_button_press
// caller invokes this and returns. Left-click on a marker line (waveform) or a
// flag rect (top strip) toggles selection and jumps the playhead; left-click
// elsewhere positions the playhead (with playback stop) and clears the
// selection unless Shift is held. Drag-create and top-strip playhead movement
// are silent no-ops so the read-only marker-state invariant is preserved.
// Recomputes the cheap geometry it needs; derives `shift` from `mods`.
void GuiInputHandler::handle_render_view_press(GuiMouseButton button, int x,
                                               int y, bool inside_top,
                                               bool inside_waveform,
                                               GuiInputState mods) {
    const bool shift = mods.shift;
    const GuiRect area = waveform_area(app);
    // Wheel events arrive via on_wheel (coalesced per pointer frame), not
    // here; a stray wheel button is caught by the Left-only gate below.
    if (button != GuiMouseButton::Left) return;
    // In phase reset sub-view, top-strip clicks
    // are silent no-ops (phase resets have no flag rects). Bail
    // before hit-testing so we don't attempt selection bookkeeping
    // on a non-existent flag pack.
    if (app.active_markers_view == 'P' && inside_top) return;
    // Top-strip clicks stop playback first: they can open the iter/
    // bpm/flag editors and continuing audio during text editing is
    // the wrong default. Waveform clicks keep playback alive — the
    // per-press reseek to the click sample happens at the playhead-
    // drag press sites below, gated on was_playing && sample !=
    // playhead_at_entry.
    const bool was_playing_rv = playback.is_playing();
    const int64_t playhead_at_entry_rv = app.playhead_cursor_sample;
    if (inside_top) playback_lifecycle.stop_playback_if_playing();
    int hit = -1;
    if (inside_waveform)  hit = hit_test_marker_line(app, audio, x);
    else if (inside_top)  hit = hit_test_flag(app, audio, x, y);
    else                  return;
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
        }
        gui.invalidate_region(0, 0, app.width, app.height);
        int64_t sample;
        if (sub_t) {
            sample = static_cast<int64_t>(std::nearbyint(
                app.render_view.phase_resets[hit].time_frame));
        } else {
            sample = static_cast<int64_t>(std::nearbyint(
                app.render_view.warp_markers[hit].time_frame));
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
        if (!shift &&
            (!sel.empty() || last_sel != -1)) {
            sel.clear();
            last_sel = -1;
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
// interval. Otherwise run hover-popup detection against render_view.warp_markers
// (suppressed in phase reset sub-view because hit_test_flag short-circuits to
// -1). Fully terminating; the on_motion caller returns after it.
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
            if (app.active_markers_view == 'P') {
                new_playhead = static_cast<int64_t>(std::nearbyint(
                    app.render_view.phase_resets[hit].time_frame));
            } else {
                new_playhead = static_cast<int64_t>(std::nearbyint(
                    app.render_view.warp_markers[hit].time_frame));
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
            if (hit >= 0) {
                const bool already_single =
                    app.selected_markers.size() == 1 &&
                    *app.selected_markers.begin() == hit;
                if (!already_single) {
                    app.selected_markers.clear();
                    app.selected_markers.insert(hit);
                    app.last_selected_marker = hit;
                    gui.invalidate_region(0, 0, app.width, app.height);
                }
            } else if (!app.selected_markers.empty() ||
                       app.last_selected_marker != -1) {
                app.selected_markers.clear();
                app.last_selected_marker = -1;
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
            // markers at fast pointer speeds). Render-view positions
            // are already in the display domain — identity interval,
            // no map translation.
            const int64_t prev = app.playhead_drag.last_swept_sample;
            if (prev >= 0 && new_playhead != prev) {
                int64_t a = prev, b = new_playhead;
                const bool forward = (b >= a);
                if (!forward) std::swap(a, b);
                // Sweep endpoints are frame doubles on the render's own
                // time axis, the render-view stores' domain.
                const double lo_t = static_cast<double>(a);
                const double hi_t = static_cast<double>(b);
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
