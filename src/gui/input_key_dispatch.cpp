// on_key dispatch helpers, lifted verbatim from input_handler.cpp's on_key.
// Each is a GuiInputHandler method declared in input_handler.h; on_key calls
// them in sequence (if (handle_X(...)) return;). Grouped here to keep
// input_handler.cpp focused on the event entry points and the pointer /
// wheel paths.

#include "input_handler.h"

#include "paint_handler.h"
#include "render.h"
#include "render_pipeline.h"
#include "settings_io.h"
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
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Source-view read-only allowlist (sibling of render_view_key_blocked).
// True when key+mods is not on the allowlist and should be dropped.
bool GuiInputHandler::read_only_key_blocked(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool is_o =
        (key == GuiKeys::O && !ctrl && !shift && !alt);
    const bool is_play_pause = is_play_pause_key(key);
    const bool is_scrub =
        ((key == GuiKeys::Left || key == GuiKeys::Right) &&
         !ctrl && !shift && !alt);
    const bool is_scrub_samples =
        ((key == GuiKeys::Left || key == GuiKeys::Right) &&
         !ctrl && shift && !alt);
    const bool is_home_end =
        ((key == GuiKeys::Home || key == GuiKeys::End) &&
         !ctrl && !shift && !alt);
    const bool is_page_updown =
        ((key == GuiKeys::PageUp || key == GuiKeys::PageDown) &&
         !ctrl && !shift && !alt);
    const bool is_zoom =
        ((key == GuiKeys::Up || key == GuiKeys::Down) &&
         !ctrl && !shift && !alt);
    const bool is_zoom_symbol =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
         !ctrl && !shift && !alt);
    // Ctrl+Shift+=/- steps the GUI font size. Pure display state, the sibling
    // of the bare =/- zoom aliases this gate already admits, so it is honored
    // in a read-only tab (blocking authoring mutations is not its concern).
    const bool is_font_size_step =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
         ctrl && shift && !alt);
    const bool is_zero =
        (key == GuiKeys::Digit0 && !ctrl && !shift && !alt);
    // Shift+0..9 selects a playback speed. Playback speed is a playback
    // preference, not an authoring mutation, so the read-only gate admits
    // it like Space and the scrub chords; in a read-only tab whose active
    // view is target, set_playback_speed's own target-view refusal
    // (playback_lifecycle.cpp) still governs, so admission here is safe
    // in every view domain.
    const bool is_speed_select =
        (key >= GuiKeys::Digit0 && key <= GuiKeys::Digit9 &&
         shift && !ctrl && !alt);
    const bool is_follow =
        (key == GuiKeys::F && !ctrl && !shift && !alt);
    const bool is_center =
        (key == GuiKeys::C && !ctrl && !shift && !alt);
    const bool is_sub_t =
        (key == GuiKeys::T && !ctrl && !shift && !alt);
    const bool is_sub_p =
        (key == GuiKeys::P && !ctrl && !shift && !alt);
    // Bare `h` jumps the playhead to the focused phase reset's anticipation
    // offset point (input_handler.cpp). Pure navigation — a playhead move plus
    // at most a viewport recenter, exactly the Tab/scrub family this gate
    // admits — so a read-only tab still honors it (it authors nothing).
    const bool is_offset_hop =
        (key == GuiKeys::H && !ctrl && !shift && !alt);
    const bool is_tab_cycle =
        (!ctrl && key == GuiKeys::Tab) ||
        (!ctrl && key == GuiKeys::IsoLeftTab);
    const bool is_ctrl_tab =
        (ctrl && !shift && key == GuiKeys::Tab);
    const bool is_ctrl_shift_tab =
        (ctrl && shift && key == GuiKeys::Tab);
    const bool is_esc = (key == GuiKeys::Escape);
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    const bool is_ctrl_w =
        (ctrl && !shift && !alt && key == GuiKeys::W);
    const bool is_save =
        (ctrl && !shift && !alt && key == GuiKeys::S);
    const bool is_copy_phase_resets =
        (ctrl && !shift && !alt && key == GuiKeys::P);
    // Ctrl+Z (undo) and Ctrl+Shift+Z (redo) are admitted through this
    // active-tab gate unconditionally. The honored-ness rule is not the
    // active tab's read-only state but the target entry's tab, which may
    // be the other tab; only do_undo / do_redo have that entry in hand,
    // so the real read-only decision lives there, not here.
    const bool is_undo_redo = (ctrl && !alt && key == GuiKeys::Z);
    const bool is_trim_set =
        (key == GuiKeys::X && !ctrl && !shift && !alt);
    const bool is_trim_clear =
        (key == GuiKeys::X && !ctrl && shift && !alt);
    const bool is_delete =
        (key == GuiKeys::Delete && !ctrl && !alt);
    return !(is_o || is_play_pause || is_scrub || is_scrub_samples ||
             is_home_end || is_page_updown ||
             is_zoom || is_zoom_symbol || is_font_size_step || is_zero ||
             is_speed_select ||
             is_follow || is_center || is_sub_t || is_sub_p ||
             is_offset_hop ||
             is_tab_cycle || is_ctrl_tab || is_ctrl_shift_tab ||
             is_esc || is_ctrl_q || is_ctrl_w || is_save ||
             is_copy_phase_resets || is_undo_redo ||
             is_trim_set || is_trim_clear || is_delete);
}

// Esc-cancel handlers for in-flight operations. See the declaration in
// input_handler.h for routing order.
bool GuiInputHandler::handle_escape_cancels(GuiKey key) {
    // Esc during a render-in-flight requests cancellation. Two effects:
    //   1. async_renderer.request_cancel() sets the worker's cancel flag,
    //      which do_render passes through to the engine.
    //   2. app.queue_cancel_requested = true so that on_batch_entry_complete
    //      finalizes the batch instead of dispatching the next entry.
    // Both are needed: (1) interrupts the current render mid-stream;
    // (2) stops the batch state machine from advancing after the
    // cancelled render's on_done fires.
    if (key == GuiKeys::Escape && async_renderer.is_busy()) {
        async_renderer.request_cancel();
        app.queue_cancel_requested = true;
        return true;
    }
    if (key == GuiKeys::Escape && app.queue_running) {
        // Render-state housekeeping flag survives a frame past the
        // worker's actual completion (worker_state_ transitions
        // Running -> CompletionPending while is_busy() still returns
        // true; once on_completion_event fires it goes Idle). The
        // is_busy() branch above covers that window. This branch is
        // the rare case where queue_running is set but the worker has
        // already cleared — defensive, mirrors the prior behavior.
        app.queue_cancel_requested = true;
        return true;
    }
    return false;
}

// Esc during a pointer drag stops the gesture. Marker and trim drags
// are stopped before their deferred commit, so their pending change is
// discarded and the live state returns to pre-drag; scroll and playhead
// drags apply their motion continuously, so stopping just ends them
// where they are. One verb across all four: Esc means stop.
void GuiInputHandler::cancel_active_drags() {
    if (app.drag.active) {
        // The live marker store was untouched during motion (proposed
        // positions lived in moveable_times, read by paint through
        // DragOverlay), so no marker revert is needed. The motion handler
        // did track the playhead onto the grabbed marker's proposed
        // position, so restore the pre-drag playhead captured at
        // begin_drag before resetting, then repaint the committed positions.
        app.playhead_cursor_sample = app.drag.pre_drag_playhead_sample;
        if (playback.is_playing()) playback.resync_predictor();
        app.drag = DragState{};
        viewport.invalidate_waveform_area();
        viewport.invalidate_top_strip();
        viewport.invalidate_timestamp_area();
    }
    if (app.trim_drag.active) {
        // Trim motion mutates app.trim live but keeps the pre-drag
        // bounds in orig_begin/orig_end, so restore them before clearing
        // the gesture.
        if (app.trim_drag.moved) {
            app.trim.begin_frame = app.trim_drag.orig_begin_frame;
            app.trim.end_frame   = app.trim_drag.orig_end_frame;
        }
        app.trim_drag = TrimDragState{};
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
    }
    // Scroll and playhead drags have already applied their motion, so
    // stopping is just ending the gesture at its current position.
    if (app.scroll_drag.active)   app.scroll_drag = ScrollDragState{};
    if (app.playhead_drag.active) app.playhead_drag = PlayheadDragState{};
}

// Render-trigger chords. See the declaration for the chord list.
bool GuiInputHandler::handle_render_dispatch_keys(GuiKey key,
                                                  GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    // Ctrl+E: snapshot current authoring state into the in-memory
    // render queue. No disk writes; on-disk authoring files are untouched.
    // Each entry now snapshots its full render state (markers, phase
    // resets, engine settings, trim).
    if (ctrl && !alt && !shift &&
        key == GuiKeys::E) {
        if (app.source_audio_path.empty()) return true;
        app.queued_renders.push_back(snapshot_current_queued_render());
        std::fprintf(stderr,
            "warptempo_gui: queued render (%zu in queue)\n",
            app.queued_renders.size());
        return true;
    }

    // Ctrl+Alt+R: single render into the source directory using `title`
    // from settings. Does not consult the in-memory queue; empty
    // batch_folder/batch_basename selects the source-directory naming
    // convention inside do_render. A successful sibling wav publish emits
    // the .peaks pyramid and .fingerprint sidecar, but not batch-only
    // render-view sidecars (.warpmarkers / .phaseresetmarkers /
    // .rendersettings / .renderwarpmarkers / .renderphaseresetmarkers).
    // Title-not-set is a hard error surfaced from do_render.
    if (ctrl && alt && !shift &&
        key == GuiKeys::R) {
        if (app.source_audio_path.empty()) return true;
        // Reject overlapping submissions: the dispatcher is single-job.
        // The GUI's existing serialization (queue_running gate) prevents
        // this in practice, but a defensive early-return keeps the
        // contract local.
        if (async_renderer.is_busy()) return true;

        // Pre-flight the live store on the GUI thread: a modeled defect
        // (a trimmed map-format render included) opens the
        // defect-resolution series; a non-modeled failure raises the
        // popup. Either way the dispatch is refused.
        if (!warp_render_preflight(app.warpmarkers.markers(),
                                   app.phaseresetmarkers.markers(),
                                   /*live_store=*/true,
                                   app.engine_settings.scale,
                                   app.engine_settings.output_format,
                                   app.trim.has_begin, app.trim.begin_frame,
                                   app.trim.has_end, app.trim.end_frame)) {
            return true;
        }

        RenderRequest req = build_render_request(
            app.source_audio_path, app.warpmarkers.markers(),
            app.phaseresetmarkers.markers(), app.engine_settings,
            app.trim.has_begin, app.trim.begin_frame,
            app.trim.has_end,   app.trim.end_frame);
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);
        // Empty batch_folder/basename selects the source-dir naming
        // convention inside do_render. The dispatch hands the request to
        // the worker thread; on_done fires on the GUI thread when the
        // render finishes (success, failure, or cancel).
        app.queue_cancel_requested = false;
        app.queue_running          = true;
        app.queue_progress_text    = "rendering...";
        viewport.clear_hover_popup();
        viewport.invalidate_timestamp_area();
        async_renderer.dispatch(std::move(req),
            [this](RenderOutcome o) {
                const bool success = (o == RenderOutcome::Success);
                if (o == RenderOutcome::Cancelled) {
                    std::fprintf(stderr, "warptempo_gui: render cancelled\n");
                }
                finalize_render_run();
                if (success && app.active_audio_view == 'T' &&
                    !target_render.is_updating()) {
                    // ensure_ready() may fill from the shared cache when the
                    // just-rendered fingerprint is already registered, or
                    // render the current target state if the state changed or
                    // a longer-than-RAM-tier entry is still registering on the
                    // writer thread. That miss is benign; if finalize_render_run
                    // just launched a pending target render, leave it alone.
                    target_render.ensure_ready();
                }
            });
        return true;
    }

    // Ctrl+Alt+E: render the in-memory queue as one batch. Each wav
    // queued entry produces a sibling .wav plus commit-critical
    // .warpmarkers, .phaseresetmarkers, and .rendersettings sidecars inside
    // a fresh batch folder
    // `<source_parent>/renders/<index>_render_all_in_queue/`.
    // The index is one greater than the highest pre-existing batch index
    // in that renders folder (regardless of command tag). Filenames
    // inside the batch are the entry position zero-padded to fit the
    // queue size: 01..10 for 10 entries, 1..7 for 7, 001..100 for 100.
    //
    // Empty queue auto-enqueues the current authoring state (same shape
    // as Ctrl+E) and dispatches a queue-of-one batch. Use Ctrl+Alt+R for
    // single-shot rendering into the source directory; Ctrl+Alt+E always
    // produces a batch folder under renders/.
    //
    // Esc between entries drops the remainder. The current render
    // cannot be interrupted (no mid-engine cancellation); its sidecars
    // are written if it succeeds, then the loop exits and the rest of
    // the queue is discarded. The batch folder is left as-is on disk —
    // partial batches just contain fewer files than the queue had.
    // The in-memory queue is cleared after execution whether all
    // entries ran or Esc cut it short.
    if (ctrl && alt && !shift &&
        key == GuiKeys::E) {
        if (app.source_audio_path.empty()) return true;
        if (app.queued_renders.empty()) {
            // Pre-flight the live store BEFORE the auto-enqueue so a
            // refused dispatch leaves nothing queued (a modeled defect
            // opens the defect-resolution series; the rest gets the popup).
            if (!warp_render_preflight(app.warpmarkers.markers(),
                                       app.phaseresetmarkers.markers(),
                                       /*live_store=*/true,
                                       app.engine_settings.scale,
                                       app.engine_settings.output_format,
                                       app.trim.has_begin,
                                       app.trim.begin_frame,
                                       app.trim.has_end,
                                       app.trim.end_frame)) {
                return true;
            }
            app.queued_renders.push_back(snapshot_current_queued_render());
            std::fprintf(stderr,
                "warptempo_gui: queue empty; enqueueing current state "
                "and rendering\n");
        }

        // Pre-flight every queued entry against its own snapshot (markers,
        // phase resets, scale, output format, and trim — Ctrl+E snapshots
        // may differ from the live store and from each other). A snapshot
        // cannot be fixed by mutating the live store, so failures surface
        // through the popup backstop, not the defect series. First failure
        // refuses the whole batch; the queue is left intact so the user
        // can fix and re-dispatch. Runs before the queue is moved out.
        for (const auto& q : app.queued_renders) {
            if (!warp_render_preflight(q.warp_markers,
                                       q.phase_resets,
                                       /*live_store=*/false,
                                       q.engine_settings.scale,
                                       q.engine_settings.output_format,
                                       q.has_trim_begin, q.trim_begin_frame,
                                       q.has_trim_end, q.trim_end_frame)) {
                return true;
            }
        }

        std::vector<AppState::QueuedRender> entries =
            std::move(app.queued_renders);
        app.queued_renders.clear();

        std::filesystem::path src(app.source_audio_path);
        std::filesystem::path src_parent = src.parent_path();
        if (src_parent.empty()) src_parent = std::filesystem::path(".");
        const std::filesystem::path queue_root = src_parent / "renders";

        // Resolve the next batch index: max+1 over directory entries
        // matching `<digits>_<anything>`. Empty / missing renders/
        // folder seeds index 1.
        int next_index = 1;
        std::error_code ec;
        if (std::filesystem::is_directory(queue_root, ec)) {
            int max_idx = 0;
            for (const auto& de :
                 std::filesystem::directory_iterator(queue_root, ec)) {
                if (!de.is_directory()) continue;
                const std::string name = de.path().filename().string();
                int v = 0;
                size_t i = 0;
                while (i < name.size() &&
                       name[i] >= '0' && name[i] <= '9') {
                    v = v * 10 + (name[i] - '0');
                    ++i;
                }
                if (i == 0 || i >= name.size() || name[i] != '_') continue;
                if (v > max_idx) max_idx = v;
            }
            next_index = max_idx + 1;
        }

        const std::string command_tag = "render_all_in_queue";
        const std::filesystem::path batch_folder =
            queue_root /
            (std::to_string(next_index) + "_" + command_tag);
        std::filesystem::create_directories(batch_folder, ec);
        if (ec) {
            std::fprintf(stderr,
                "warptempo_gui: render-all: could not create '%s': %s\n",
                batch_folder.string().c_str(), ec.message().c_str());
            return true;
        }

        // Width-to-fit zero-padding for filename indices. pad_width is
        // computed from the queue size and clamped to a sane upper
        // bound so the snprintf below has a known-bounded output.
        const int total = static_cast<int>(entries.size());
        int pad_width = 1;
        for (int n = total; n >= 10; n /= 10) ++pad_width;
        if (pad_width > 9) pad_width = 9;

        std::vector<RenderRequest> reqs;
        reqs.reserve(total);
        for (int i = 0; i < total; ++i) {
            const auto& q = entries[i];
            char num_buf[16];
            std::snprintf(num_buf, sizeof(num_buf),
                          "%0*d", pad_width, i + 1);
            std::fprintf(stderr,
                "warptempo_gui: rendering entry %d of %d: %s/%s.wav\n",
                i + 1, total,
                batch_folder.filename().string().c_str(), num_buf);

            RenderRequest req = build_render_request(
                q.source_audio_path, q.warp_markers, q.phase_resets, q.engine_settings,
                q.has_trim_begin, q.trim_begin_frame, q.has_trim_end, q.trim_end_frame,
                batch_folder.string(), num_buf);
            req.authoring = q.authoring;
            attach_shared_render_resources(req);
            reqs.push_back(std::move(req));
        }

        if (async_renderer.is_busy()) return true;
        start_render_batch(std::move(reqs), "render queue");
        return true;
    }

    // Ctrl+Alt+I renders the Cartesian product of the per-marker iter ranges
    // authored in iteration mode. Output lands in
    // `<source_parent>/renders/<N>_render_iterations/`, with one .wav per cell
    // named `<seq>_<delta_csv>.wav`. The CSV holds the swept markers' deltas
    // in timeline order, formatted `%+0.2f`; markers with no iter range
    // authored are excluded from the CSV and contribute one fixed value (their
    // authored tempo_base) to the product. Per-cell progress and Esc
    // cancellation are handled by run_render_batch. Silent no-op outside
    // iteration mode.
    if (ctrl && alt && !shift &&
        key == GuiKeys::I) {
        if (app.source_audio_path.empty()) return true;
        if (!app.iteration_mode_enabled) return true;

        // Pre-flight the live store: a modeled defect (a trimmed
        // map-format render included) opens the defect-resolution series
        // and refuses the whole sweep; a non-modeled failure refuses with
        // the popup. Per-cell tempo_base mutations remain on the async
        // stderr backstop.
        if (!warp_render_preflight(app.warpmarkers.markers(),
                                   app.phaseresetmarkers.markers(),
                                   /*live_store=*/true,
                                   app.engine_settings.scale,
                                   app.engine_settings.output_format,
                                   app.trim.has_begin, app.trim.begin_frame,
                                   app.trim.has_end, app.trim.end_frame)) {
            return true;
        }

        // Snapshot markers in timeline order (the GuiWarpMarkers store is
        // sorted by time_frame, with ties legal). For each owning marker
        // build its per-cell delta list: a single 0.0 when no iter
        // range is authored, otherwise integer-cents enumeration from
        // iter_start to iter_end inclusive. Integer-cents avoids the
        // float-accumulation drift a naive `for (d=start; d<=end;
        // d+=0.01)` would suffer across many steps.
        const std::vector<GuiWarpMarker> base_warp_markers =
            app.warpmarkers.markers();
        std::vector<int>                 eligible_indices;
        std::vector<std::vector<double>> per_marker_deltas;
        std::vector<bool>                is_swept;
        for (int i = 0; i < static_cast<int>(base_warp_markers.size()); ++i) {
            const GuiWarpMarker& m = base_warp_markers[i];
            if (!iter_popup_eligible_marker(m)) continue;
            eligible_indices.push_back(i);
            const bool swept =
                !std::isnan(m.iter_start) && !std::isnan(m.iter_end);
            is_swept.push_back(swept);
            std::vector<double> deltas;
            if (swept) {
                const int start_cents = static_cast<int>(
                    std::lround(m.iter_start * 100.0));
                const int end_cents = static_cast<int>(
                    std::lround(m.iter_end * 100.0));
                // The bracket commit enforces start <= end, but a stray
                // hand-edit of memory could violate it. Treat that as
                // no sweep rather than producing zero cells.
                if (start_cents > end_cents) {
                    deltas.push_back(0.0);
                    is_swept.back() = false;
                } else {
                    for (int c = start_cents; c <= end_cents; ++c) {
                        deltas.push_back(static_cast<double>(c) / 100.0);
                    }
                }
            } else {
                deltas.push_back(0.0);
            }
            per_marker_deltas.push_back(std::move(deltas));
        }

        bool any_swept = false;
        for (bool s : is_swept) {
            if (s) { any_swept = true; break; }
        }
        if (!any_swept) {
            std::fprintf(stderr,
                "warptempo_gui: render-iterations: no iter ranges "
                "authored; nothing to render\n");
            return true;
        }

        size_t total_cells = 1;
        for (const auto& d : per_marker_deltas) total_cells *= d.size();
        if (total_cells == 0) return true;

        std::filesystem::path src(app.source_audio_path);
        std::filesystem::path src_parent = src.parent_path();
        if (src_parent.empty()) src_parent = std::filesystem::path(".");
        const std::filesystem::path queue_root = src_parent / "renders";

        // Resolve the next batch index: max+1 over `<digits>_<anything>`
        // entries. Mirrors render_all_in_queue's scanner.
        int next_index = 1;
        std::error_code ec;
        if (std::filesystem::is_directory(queue_root, ec)) {
            int max_idx = 0;
            for (const auto& de :
                 std::filesystem::directory_iterator(queue_root, ec)) {
                if (!de.is_directory()) continue;
                const std::string name = de.path().filename().string();
                int v = 0;
                size_t i = 0;
                while (i < name.size() &&
                       name[i] >= '0' && name[i] <= '9') {
                    v = v * 10 + (name[i] - '0');
                    ++i;
                }
                if (i == 0 || i >= name.size() || name[i] != '_') continue;
                if (v > max_idx) max_idx = v;
            }
            next_index = max_idx + 1;
        }

        const std::string command_tag = "render_iterations";
        const std::filesystem::path batch_folder =
            queue_root /
            (std::to_string(next_index) + "_" + command_tag);
        std::filesystem::create_directories(batch_folder, ec);
        if (ec) {
            std::fprintf(stderr,
                "warptempo_gui: render-iterations: could not create "
                "'%s': %s\n",
                batch_folder.string().c_str(), ec.message().c_str());
            return true;
        }

        const int total = static_cast<int>(total_cells);
        int pad_width = 1;
        for (int n = total; n >= 10; n /= 10) ++pad_width;
        if (pad_width > 9) pad_width = 9;

        // Snapshot phase resets once — every cell shares the same
        // phase reset configuration, only marker tempo_base values
        // differ across cells.
        const std::vector<GuiPhaseResetMarker> base_phase_resets =
            app.phaseresetmarkers.markers();

        // Cartesian product enumeration. `indices[k]` holds the
        // current cell coordinate along the k-th eligible marker
        // (timeline order). Rightmost dimension increments fastest:
        // consecutive cells differ in the last marker's delta first.
        const size_t num_dims = per_marker_deltas.size();
        std::vector<size_t> indices(num_dims, 0);

        std::vector<RenderRequest> reqs;
        reqs.reserve(total);
        for (int cell = 0; cell < total; ++cell) {
            std::string delta_csv;
            for (size_t k = 0; k < num_dims; ++k) {
                if (!is_swept[k]) continue;
                const double d = per_marker_deltas[k][indices[k]];
                char dbuf[16];
                std::snprintf(dbuf, sizeof(dbuf), "%+0.2f", d);
                if (!delta_csv.empty()) delta_csv += ',';
                delta_csv += dbuf;
            }

            char num_buf[16];
            std::snprintf(num_buf, sizeof(num_buf),
                          "%0*d", pad_width, cell + 1);
            std::string basename = num_buf;
            basename += '_';
            basename += delta_csv;

            std::vector<GuiWarpMarker> cell_warp_markers = base_warp_markers;
            for (size_t k = 0; k < num_dims; ++k) {
                const int mi = eligible_indices[k];
                // Per-cell tempo is a computed value, not an authored one,
                // so it takes no bracket gate: with deltas up to
                // +-kIterDeltaMax a cell tempo can go non-positive, and
                // build_warp_frame_map's existing refusal on the async
                // render path (stderr) is the backstop.
                cell_warp_markers[mi].tempo_base =
                    base_warp_markers[mi].tempo_base +
                    per_marker_deltas[k][indices[k]];
                // The engine doesn't consume iter values; clear them
                // so the request is quiet.
                cell_warp_markers[mi].iter_start =
                    std::numeric_limits<double>::quiet_NaN();
                cell_warp_markers[mi].iter_end =
                    std::numeric_limits<double>::quiet_NaN();
            }

            RenderRequest req = build_render_request(
                app.source_audio_path, std::move(cell_warp_markers), base_phase_resets,
                app.engine_settings,
                app.trim.has_begin, app.trim.begin_frame,
                app.trim.has_end,   app.trim.end_frame,
                batch_folder.string(), std::move(basename));
            req.authoring = snapshot_current_authoring_state();
            attach_shared_render_resources(req);
            reqs.push_back(std::move(req));

            // Increment rightmost dimension; carry left on overflow.
            // The last cell leaves indices in an overflowed state but
            // the loop exits before that's read.
            for (int k = static_cast<int>(num_dims) - 1; k >= 0; --k) {
                ++indices[k];
                if (indices[k] < per_marker_deltas[k].size()) break;
                indices[k] = 0;
            }
        }

        if (async_renderer.is_busy()) return true;
        start_render_batch(std::move(reqs), "render iterations");
        app.iteration_mode_enabled = false;   // iteration sweep turns off after fire
        viewport.invalidate_top_strip();
        return true;
    }

    // The BPM sweep render formerly bound to Ctrl+Alt+M now lives
    // in render_bpm_sweep(), fired by Enter in the bottom-strip BPM editor
    // after a successful commit. The keystroke is retired here.

    // Ctrl+Alt+C commits the displayed render's full authoring snapshot:
    // source-domain warp markers and phase resets, the complete engine
    // settings block, and any authoring view state carried by the
    // .rendersettings sidecar. Marker/reset promotion remains one
    // cross-file undo entry; settings and trim stay outside undo by
    // standing convention. After the commit succeeds: render-view exits,
    // the parked source audio is restored, and <source_parent>/renders/
    // is recursively wiped. The committed render survives through the
    // render cache, not as a folder artifact. Silent no-op outside
    // render-view.
    if (ctrl && alt && !shift &&
        key == GuiKeys::C) {
        if (!app.render_view.enabled) return true;
        if (app.render_view.index < 0) return true;

        // app.render_view.warp_markers / .phase_resets are render-domain display
        // state. Ctrl+Alt+C promotes the render's source-domain authoring
        // sidecars, so every required sidecar is validated and collected
        // before the first mutation.
        const auto& cur_e =
            app.render_view.list[app.render_view.index];
        const std::filesystem::path sidecar =
            cur_e.batch_folder / (cur_e.basename + ".rendersettings");
        const RendersettingsAuthoring authoring =
            read_rendersettings_authoring(sidecar);
        const std::expected<EngineSettings, std::string> commit_engine_settings =
            read_rendersettings_engine_block(sidecar);
        if (!commit_engine_settings) {
            std::fprintf(stderr,
                "warptempo_gui: commit aborted: rendersettings engine "
                "block read failed for '%s': %s\n",
                sidecar.string().c_str(),
                commit_engine_settings.error().c_str());
            return true;
        }
        const bool has_authoring_block =
            authoring.has_active_tab ||
            authoring.has_active_audio_view ||
            authoring.has_trim_begin ||
            authoring.has_trim_end ||
            authoring.has_zoom_level ||
            authoring.has_viewport_start ||
            authoring.has_playhead;
        std::vector<GuiWarpMarker>    src_warp;
        std::vector<GuiPhaseResetMarker> src_phase_resets;
        {
            const std::filesystem::path wm =
                cur_e.batch_folder / (cur_e.basename + ".warpmarkers");
            GuiWarpMarkers m;
            auto r = m.load(wm.string());
            if (!r) {
                std::fprintf(stderr,
                    "warptempo_gui: commit aborted: load failed for '%s': "
                    "%s\n",
                    wm.string().c_str(), r.error().c_str());
                return true;
            }
            src_warp = m.markers();
        }
        {
            const std::filesystem::path tm = cur_e.batch_folder /
                (cur_e.basename + ".phaseresetmarkers");
            GuiPhaseResetMarkers t;
            auto r = t.load(tm.string());
            if (!r) {
                std::fprintf(stderr,
                    "warptempo_gui: commit aborted: load failed for '%s': "
                    "%s\n",
                    tm.string().c_str(), r.error().c_str());
                return true;
            }
            src_phase_resets = t.markers();
        }

        std::vector<GuiWarpMarker>    warp_pre  = app.warpmarkers.markers();
        std::vector<GuiPhaseResetMarker> phase_reset_pre = app.phaseresetmarkers.markers();
        const int                 hint_last = app.last_selected_marker;

        app.warpmarkers.markers_mut()    = std::move(src_warp);
        app.phaseresetmarkers.markers_mut() = std::move(src_phase_resets);
        app.selected_markers.clear();
        app.last_selected_marker = -1;
        // Commit is a wholesale authoring reset. Every per-tab per-mode slot
        // referencing the replaced marker stores is stale.
        {
            auto clear_marker_slots = [](ViewState& t) {
                t.warp_selected.clear();
                t.warp_last_selected        = -1;
                t.phase_reset_selected.clear();
                t.phase_reset_last_selected = -1;
            };
            clear_marker_slots(app.tab_a);
            clear_marker_slots(app.tab_b);
        }

        const char commit_tab =
            authoring.has_active_tab ? authoring.active_tab : app.active_tab_view;
        // Attribute the entry to the mode the commit was performed in so undo/redo restore the user's context and interpret post-restore hints against that marker store.
        const char commit_marker_mode = app.active_markers_view;
        undo.push_undo_both(std::move(warp_pre), std::move(phase_reset_pre),
                       commit_marker_mode, hint_last, commit_tab);
        undo.recompute_dirty();

        // Full engine-settings commit. The strict engine block was validated
        // before marker mutation, so commit can adopt the typed recipe without
        // degrading to the previous live settings.
        app.engine_settings = *commit_engine_settings;

        const std::filesystem::path src(app.source_audio_path);
        std::filesystem::path src_parent = src.parent_path();
        if (src_parent.empty()) src_parent = std::filesystem::path(".");
        const std::filesystem::path renders_root =
            src_parent / "renders";

        // Committing a render is a wholesale authoring reset. Clear every
        // marker's session-only iteration values and turn off both sweep
        // modes' visibility. BPM values were already consumed when the sweep
        // that produced this render fired.
        // No separate undo entry: the commit pushes its own cross-file undo
        // and wipes renders/; iter values are session-only, never serialized.
        {
            auto& mv = app.warpmarkers.markers_mut();
            for (auto& m : mv) {
                m.iter_start = std::numeric_limits<double>::quiet_NaN();
                m.iter_end   = std::numeric_limits<double>::quiet_NaN();
            }
        }
        app.iteration_mode_enabled = false;
        app.bpm_mode_enabled       = false;

        render_view.restore_source_audio();

        if (authoring.has_active_tab &&
            authoring.active_tab != app.active_tab_view) {
            active_views.switch_active_tab_view_to(authoring.active_tab);
        }

        if (has_authoring_block) {
            app.trim.has_begin = authoring.has_trim_begin;
            app.trim.begin_frame = authoring.has_trim_begin
                ? authoring.trim_begin_frame
                : 0.0;
            app.trim.has_end = authoring.has_trim_end;
            app.trim.end_frame = authoring.has_trim_end
                ? authoring.trim_end_frame
                : 0.0;
            if (!app.trim.has_begin) app.trim_begin_selected = false;
            if (!app.trim.has_end)   app.trim_end_selected   = false;
            if (!app.trim.has_begin && !app.trim.has_end) {
                app.last_selected_trim = 0;
            }
        }

        if (authoring.has_active_audio_view &&
            authoring.active_audio_view == 'S' &&
            app.active_audio_view == 'T') {
            app.active_audio_view = 'S';
            target_render.rebind_to_source();
        } else if (authoring.has_active_audio_view &&
                   authoring.active_audio_view == 'T') {
            app.active_audio_view = 'T';
        }

        if (authoring.has_zoom_level &&
            authoring.zoom_level >= kFitFileLevel &&
            authoring.zoom_level <= kMaxNumericLevel) {
            app.zoom_level = authoring.zoom_level;
        }
        if (authoring.has_viewport_start) {
            app.viewport_start_sample = authoring.viewport_start;
        }
        if (authoring.has_playhead) {
            app.playhead_cursor_sample = authoring.playhead;
            if (!app.playhead_scanner_active) {
                app.playhead_scanner_sample = authoring.playhead;
            }
        }
        clamp_viewport_start(app, audio);
        viewport.clear_hover_popup();
        viewport.kick_waveform_sync();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();

        target_render.trigger();

        app.render_view.list.clear();
        app.render_view.warp_markers.clear();
        app.render_view.phase_resets.clear();
        app.render_view.index             = -1;
        app.render_view.src_F_begin       = 0;
        app.render_view.src_F_end         = 0;
        app.render_view.last_path.clear();

        std::error_code ec;
        if (std::filesystem::is_directory(renders_root, ec)) {
            std::filesystem::remove_all(renders_root, ec);
            if (ec) {
                std::fprintf(stderr,
                    "warptempo_gui: render-view: wipe failed for '%s': "
                    "%s\n",
                    renders_root.string().c_str(),
                    ec.message().c_str());
            }
        }

        std::fprintf(stderr,
            "warptempo_gui: render-view: committed render and wiped "
            "renders/\n");
        gui.invalidate_region(0, 0, app.width, app.height);
        return true;
    }
    return false;
}

// P / I / M letter-key handlers. See the declaration for the chord list.
bool GuiInputHandler::handle_mode_keys(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Ctrl+P: copy phase reset placements from a two-warp-marker
    // selection into the session clipboard. W-mode only; phase reset
    // mode is a silent no-op. Off-count selection in W-mode emits a
    // one-line stderr nudge.
    if (key == GuiKeys::P && ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.selected_markers.size() != 2) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset copy: select exactly two warp "
                "markers\n");
            return true;
        }
        phase_reset_propagate.copy_from_selection();
        return true;
    }

    // Ctrl+Alt+P: paste clipboard phase resets onto the destination
    // anchored at the single selected warp marker. W-mode only; phase
    // reset mode is a silent no-op. Empty clipboard is a silent no-op.
    // Opens a confirmation prompt before any mutation.
    if (key == GuiKeys::P && ctrl && !shift && alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.phase_reset_clipboard.empty()) return true;
        if (app.selected_markers.size() != 1) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset paste: select exactly one warp "
                "marker\n");
            return true;
        }
        phase_reset_propagate.open_paste_confirmation();
        return true;
    }

    // Ctrl+Alt+Shift+P: propagate the enabled/disabled *state* of
    // clipboard placements onto the matching destination region's
    // phase resets, in order. Positions are not modified. W-mode only;
    // phase reset mode is a silent no-op. Empty clipboard is a silent
    // no-op. Unlike Ctrl+Alt+P, no confirmation prompt — applies
    // directly. Divergence/mismatch is reported via the bottom-strip
    // transient status message rather than a modal dialog.
    if (key == GuiKeys::P && ctrl && shift && alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.phase_reset_clipboard.empty()) return true;
        if (app.selected_markers.size() != 1) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset state-paste: select exactly one "
                "warp marker\n");
            return true;
        }
        phase_reset_propagate.paste_state_apply();
        return true;
    }

    // `p` (no modifiers) toggles phase reset view globally.
    // Render-view shares the global active_markers_view flag, so a
    // single handler serves both views. Render-view inherits the
    // engine precondition check from toggle_active_markers_view.
    if (key == GuiKeys::P && !ctrl && !shift && !alt) {
        active_views.toggle_active_markers_view();
        return true;
    }

    // `i` (no modifiers) toggles iteration mode in warp. Silent
    // no-op in phase reset view (phase reset flags carry no tempo to
    // iterate). The editor-active branch above already swallows any
    // keystroke while a popup edit is in flight, so this code only
    // runs with no active editor. Toggling repaints the top strip
    // so iteration popups appear or vanish in one frame.
    if (key == GuiKeys::I && !ctrl && !shift && !alt) {
        if (app.active_markers_view == 'W') {
            // Mutual exclusion. Toggling iter ON forces
            // BPM mode off; toggling iter OFF leaves BPM untouched.
            const bool turning_on = !app.iteration_mode_enabled;
            if (turning_on && app.bpm_mode_enabled) {
                app.bpm_mode_enabled = false;
            }
            app.iteration_mode_enabled = !app.iteration_mode_enabled;
            viewport.clear_hover_popup();
            viewport.invalidate_top_strip();
        }
        return true;
    }
    // Shift+I: bulk-clear every marker's iter values AND exit
    // iteration mode in one keystroke ("stop authoring this mode").
    // Only fires while iteration mode is on; otherwise silent no-op.
    if (key == GuiKeys::I && !ctrl && shift && !alt) {
        if (app.active_markers_view == 'W' && app.iteration_mode_enabled) {
            flag_editor.bulk_clear_iter_values();
            app.iteration_mode_enabled = false;
            viewport.invalidate_top_strip();
        }
        return true;
    }

    // `m` (no modifiers): open the BPM editor on the earlier of two
    // selected markers that define an explicit span, or — if BPM mode is
    // already on — toggle it (and the editor) off. Warp view only; silent
    // no-op in phase reset view. Mutual exclusion with iter mode is handled
    // inside enter_bpm_mode. The gate requires exactly two selected markers
    // with no label_ref anywhere in the span; any other selection is a
    // silent no-op.
    if (key == GuiKeys::M && !ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.bpm_mode_enabled) {
            // Re-press: editor and mode go down together.
            if (text_editor::is_active(app.top_flag_editor) &&
                app.top_flag_editor.kind ==
                    text_editor::Kind::BpmBracket) {
                flag_editor.exit_top_flag_edit_no_commit();
            }
            flag_editor.exit_bpm_mode();
            viewport.invalidate_timestamp_area();
            return true;
        }
        // Two-marker span gate. Exactly two markers must be selected; the
        // earlier owns, the later closes the span. Neither endpoint nor any
        // span-internal marker may be a label_ref — commit rewrites every
        // in-span tempo and a ref cannot take a manual tempo.
        if (app.selected_markers.size() != 2) return true;
        const auto& mv = app.warpmarkers.markers();
        auto it = app.selected_markers.begin();
        const int owner    = *it++;       // std::set: ascending, so owner is
        const int endpoint = *it;         // the earlier index, endpoint later
        if (owner < 0 || endpoint >= static_cast<int>(mv.size())) return true;
        // No label_ref anywhere in [owner, endpoint] inclusive (endpoint
        // included in the eligibility scan even though its section is not in
        // the rendered region — a ref endpoint still cannot bound the span
        // cleanly). Disabled markers ARE allowed and remain in-span.
        for (int i = owner; i <= endpoint; ++i) {
            if (!mv[i].label_ref.empty()) return true;   // silent no-op
        }
        // Owner must still satisfy the BPM-eligibility predicate (e.g. not
        // itself a label_ref — already covered — and any other standing
        // condition bpm_popup_eligible_marker encodes).
        if (!bpm_popup_eligible_marker(mv[owner])) return true;
        // enter_bpm_mode tags the owner and flips the mode flag. It no
        // longer auto-selects a next-marker cue; the span endpoint is
        // explicit, so record it on the owner and keep both selected
        // markers highlighted as the span cue.
        flag_editor.enter_bpm_mode();
        if (!app.bpm_mode_enabled) return true;   // gate inside bailed
        {
            auto& mvw = app.warpmarkers.markers_mut();
            mvw[owner].bpm_endpoint = endpoint;
        }
        const std::set<int> span_selection = app.selected_markers;
        flag_editor.enter_bpm_edit(owner);
        bool restored = false;
        for (int s : span_selection) {
            if (app.selected_markers.insert(s).second) restored = true;
        }
        if (restored) viewport.invalidate_top_strip();
        return true;
    }

    return false;
}

// Tab-key family. See the declaration for the chord list.
bool GuiInputHandler::handle_tab_switch_keys(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;

    // Ctrl+Tab toggles A/B navigational tabs. Stops playback, saves
    // current viewport/zoom/playhead to the leaving tab, restores the
    // target tab. Does not mark the document dirty.
    if (ctrl && !shift && key == GuiKeys::Tab) {
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        target_render.trigger();
        return true;
    }

    // Ctrl+Shift+Tab: advance both tabs' marker focus and end on the
    // opposite tab. Composes bare Tab and Ctrl+Tab so the user can
    // march paired tabs forward in lockstep with one chord.
    if (ctrl && shift && key == GuiKeys::Tab) {
        cycle_marker_focus_with_recenter(true);
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        cycle_marker_focus_with_recenter(true);
        target_render.trigger();
        return true;
    }

    // Bare Tab / Shift+Tab / IsoLeftTab: cycle focus and recenter on the
    // focused marker at the current zoom. The Ctrl+Tab branch above runs first and
    // returns, so Ctrl+Tab is consumed before reaching here; the explicit
    // !ctrl guards below ensure Ctrl+Shift+Tab does not slip into the
    // cycle path either.
    if (!ctrl && key == GuiKeys::Tab && !shift) {
        cycle_marker_focus_with_recenter(true);  return true;
    }
    if (!ctrl && key == GuiKeys::Tab && shift)  {
        cycle_marker_focus_with_recenter(false); return true;
    }
    if (!ctrl && key == GuiKeys::IsoLeftTab)    {
        cycle_marker_focus_with_recenter(false); return true;
    }

    return false;
}

// Bare-key (no-modifier) dispatch. See the declaration for the binding list;
// the caller gates on no modifiers held.
void GuiInputHandler::handle_plain_bare_keys(GuiKey key) {
    switch (key) {
    case GuiKeys::Escape: /* top-level Escape is a no-op */ break;
    case GuiKeys::Left:
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        viewport.move_playhead_pixels(-1);
        break;
    case GuiKeys::Right:
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        viewport.move_playhead_pixels(+1);
        break;
    case GuiKeys::Up:     viewport.zoom_in();                        break;
    case GuiKeys::Down:   viewport.zoom_out();                       break;
    case GuiKeys::F: {
        const bool was_off = !app.follow_mode;
        app.follow_mode = !app.follow_mode;
        if (was_off && app.follow_mode &&
            playback.is_playing()) {
            // Explicit enable overrides a prior manual-pan suppression so
            // follow resumes paging, not just the one initial jump.
            app.follow_overridden_for_session = false;
            playback.resync_predictor();
            // Land the scanner at the page-turn position if it had drifted
            // offscreen; no-op when it is already in view.
            viewport.follow_scroll_if_needed();
        }
        break;
    }
    case GuiKeys::C:      viewport.apply_zoom_change(kSnapZoomLevel);
                    viewport.center_viewport_on_playhead();    break;
    case GuiKeys::Home:   playback_lifecycle.stop_playback_if_playing();
                    viewport.move_playhead_to(viewport.trim_begin_sample());
                    if (app.trim.has_begin)
                        selection.select_trim_bound('B');
                    break;
    case GuiKeys::End:    playback_lifecycle.stop_playback_if_playing();
                    viewport.move_playhead_to(viewport.trim_end_sample() - 1);
                    if (app.trim.has_end)
                        selection.select_trim_bound('E');
                    break;
    default: break;
    }
}

// Top-flag editor key routing. See the declaration for the consumed/command
// contract.
bool GuiInputHandler::handle_top_flag_editor_key(GuiKey key,
                                                 GuiInputState mods) {
    const auto action = text_editor::handle_key(
        app.top_flag_editor, key, mods);
    if (action == text_editor::KeyAction::CommitRequested) {
        // Iteration editing is a widened-grammar FlagPayload
        // commit (commit_top_flag_edit), not a separate bracket editor.
        if (app.top_flag_editor.kind ==
                text_editor::Kind::BpmBracket) {
            // Enter commits + renders + closes in one action.
            // A successful commit stores the values on the owner and
            // closes the editor; only then does the BPM sweep fire. A
            // parse failure leaves the editor open (red) and renders
            // nothing.
            if (flag_editor.commit_bpm_edit()) {
                // Ordering is load-bearing: render_bpm_sweep early-bails
                // on !bpm_mode_enabled, so the sweep MUST fire before
                // exit_bpm_mode clears the flag. Then close out the mode
                // and repaint the bottom strip without the editor.
                render_bpm_sweep();
                flag_editor.exit_bpm_mode();
                // The fired sweep consumes its values. The
                // render already snapshotted the owner into its
                // RenderRequest, so clearing here doesn't disturb it.
                // Next M on this marker seeds [].
                auto& mv = app.warpmarkers.markers_mut();
                for (auto& m : mv) {
                    if (m.bpm_owner) {
                        m.bpm_owner = false;
                        m.bpm_beats = 0;
                        m.bpm_lo    = 0;
                        m.bpm_hi    = 0;
                    }
                }
                viewport.invalidate_timestamp_area();
            }
        } else {
            flag_editor.commit_top_flag_edit();
        }
        return true;
    }
    if (action == text_editor::KeyAction::CancelRequested) {
        if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
            flag_editor.exit_top_flag_edit_no_commit();
            flag_editor.exit_bpm_mode();
            viewport.invalidate_timestamp_area();
        } else {
            flag_editor.exit_top_flag_edit_no_commit();
        }
        return true;
    }
    if (apply_editor_clipboard(action, app.top_flag_editor)) {
        // Same repaint as the Consumed branch — text may have changed
        // (cut / paste); copy repaints harmlessly.
        if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket)
            viewport.invalidate_timestamp_area();
        else
            viewport.invalidate_top_strip();
        return true;
    }
    if (action == text_editor::KeyAction::Consumed) {
        // The BpmBracket editor draws in the bottom strip (like the
        // settings editor); FlagPayload / IterationBracket draw in the
        // top strip. Invalidate whichever strip the editor lives in.
        if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket)
            viewport.invalidate_timestamp_area();
        else
            viewport.invalidate_top_strip();
        return true;
    }
    // NotConsumed: the editor does not own this key, so it is a command.
    // Cancel the edit (Esc-discard: no commit, no validation), using the
    // same teardown Esc uses, then fall through (no return) so the key
    // reaches the global command dispatch below and runs. This is how every
    // command (Ctrl+Q/W/S, Ctrl+Z, Ctrl+Tab, Ctrl+P, Ctrl+E, ...) works
    // mid-edit: exit first, then the command. No command list — the editor
    // owns only its editing keymap and everything else punches through.
    if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        flag_editor.exit_top_flag_edit_no_commit();
        flag_editor.exit_bpm_mode();
        viewport.invalidate_timestamp_area();
    } else {
        flag_editor.exit_top_flag_edit_no_commit();
    }
    return false;  // not the editor's key — let on_key run the command
}

// Settings-prompt editor key routing. Same consumed/command contract as
// handle_top_flag_editor_key.
bool GuiInputHandler::handle_settings_editor_key(GuiKey key,
                                                 GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    // Bare Tab autocompletes the value side of `key=` with the key's
    // current stored value (canonical engine keys only), for recall and
    // editing. Only an unmodified Tab is intercepted; Shift / Ctrl /
    // Alt + Tab fall through to handle_key unchanged.
    if (key == GuiKeys::Tab && !ctrl && !shift && !alt) {
        settings_editor.autocomplete_value();
        return true;
    }
    const auto action = text_editor::handle_key(
        app.settings_editor, key, mods);
    if (action == text_editor::KeyAction::CommitRequested) {
        settings_editor.commit();
        return true;
    }
    if (action == text_editor::KeyAction::CancelRequested) {
        settings_editor.exit_no_commit();
        return true;
    }
    if (apply_editor_clipboard(action, app.settings_editor)) {
        viewport.invalidate_timestamp_area();
        return true;
    }
    if (action == text_editor::KeyAction::Consumed) {
        viewport.invalidate_timestamp_area();
        return true;
    }
    // NotConsumed: a command. Cancel the settings edit (Esc-discard) and
    // fall through so the global dispatch runs the command.
    settings_editor.exit_no_commit();
    return false;  // not the editor's key — let on_key run the command
}
