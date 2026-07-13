// on_key dispatch helpers. Each is a GuiInputHandler method declared in
// input_handler.h; on_key calls them in sequence (if (handle_X(...))
// return;). Grouped here to keep input_handler.cpp focused on the event
// entry points and the pointer / wheel paths.

#include "input_handler.h"

#include "paint_handler.h"
#include "render.h"
#include "render_output_naming.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "text_editor.h"
#include "time_format.h"
#include "warpmarkers.h"

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

// Source-view read-only allowlist, and the base gate render_view_key_blocked
// defers to. True when key+mods is not on the allowlist and should be dropped.
// Authoring-mutation chords are blocked here at the gate, not admitted for a
// deeper owner refusal: undo/redo (Ctrl+Z / Ctrl+Shift+Z), the trim gestures
// (x / Shift+X), Delete, and every propagate command all drop at this gate.
// Ctrl+S (save) is likewise NOT on the allowlist: read-only means no save, so
// it drops here like the authoring chords. Gesture-owned state changed in a
// locked tab (the read-only flag, trim, view state, font size, playback speed)
// reaches disk only after unlocking (bare o) or via Ctrl+S from the writable
// tab — never by saving from the locked tab itself.
// ALL propagate commands are read-only-blocked: the copy (Ctrl+P) explicitly,
// the paste pair (Ctrl+Alt+P and Ctrl+Alt+Shift+P) structurally — their
// ctrl+alt modifier combinations match no allowlist predicate. The deeper
// owner refusals — do_undo / do_redo's per-entry target-tab check
// (undo.cpp), and the read-only drag refusals (input_pointer.cpp) — stay as
// backstops for the mouse and cross-tab paths, no longer the primary surface
// for these keyboard chords.
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
    const bool is_tab_cycle =
        (!ctrl && !alt && key == GuiKeys::Tab) ||
        (!ctrl && !alt && key == GuiKeys::IsoLeftTab);
    const bool is_ctrl_tab =
        (ctrl && !shift && !alt && key == GuiKeys::Tab);
    const bool is_ctrl_shift_tab =
        (ctrl && shift && !alt && key == GuiKeys::Tab);
    const bool is_esc = (key == GuiKeys::Escape);
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    // Ctrl+Z (undo) and Ctrl+Shift+Z (redo) are NOT on the allowlist: they
    // drop at this gate. The old design admitted them because an undo entry
    // may target the OTHER (writable) tab, deferring the real decision to
    // do_undo / do_redo's per-entry target-tab peek. Under the gate-block,
    // undoing from a read-only tab first requires switching to the writable
    // tab (Ctrl+Tab) — accepted for gate legibility, so that authoring
    // mutations stop uniformly at the gate. The target-tab peek in undo.cpp
    // survives as a backstop for entries that outlive a mid-history lock.
    // The trim gestures (x / Shift+X), Delete, and the propagate copy/paste
    // chords are likewise absent (blocked here).
    return !(is_o || is_play_pause || is_scrub ||
             is_home_end || is_page_updown ||
             is_zoom || is_zoom_symbol || is_font_size_step || is_zero ||
             is_speed_select ||
             is_follow || is_center || is_sub_t || is_sub_p ||
             is_tab_cycle || is_ctrl_tab || is_ctrl_shift_tab ||
             is_esc || is_ctrl_q);
}

// Modal bottom-strip editor predicate. Modal surfaces are bottom-strip
// surfaces: the two bottom-strip editors — the settings editor and the bpm
// editor (top_flag_editor reused with Kind::BpmBracket, painted in the
// bottom strip) — and the prompts (which own input through their own gates
// in on_key and the pointer handlers). The top-strip flag editor
// (Kind::FlagPayload, the iter grammar included) is deliberately NOT modal:
// it stays red-flash-or-exit-without-commit, and every command punches
// through it unchanged.
bool GuiInputHandler::modal_bottom_strip_editor_active() const {
    return text_editor::is_active(app.settings_editor) ||
           (text_editor::is_active(app.top_flag_editor) &&
            app.top_flag_editor.kind == text_editor::Kind::BpmBracket);
}

// Bottom-strip modal-editor key gate, the sibling of
// read_only_key_blocked's allowlist shape. True when key+mods is not on
// the allowlist and should be dropped. While a bottom-strip editor is open
// the user can reach the editor itself, Esc (exit), Ctrl+S (save; the
// editor stays open), and Ctrl+Q (close routing) —
// nothing else: Space-as-playback, zoom, mode toggles, tab switches,
// undo/redo, and the marker / trim chords all drop here. "The editor
// itself" mirrors text_editor::handle_key's consumption exactly — Escape
// and Enter unconditionally, Ctrl+A/C/X/V (select-all + clipboard;
// handle_key tests only Ctrl, so the Shift/Alt variants mirror through),
// the cursor and editing keys Left / Right / Home / End / BackSpace /
// Delete under any modifiers, and printable insertion (no Ctrl/Alt, ASCII
// codepoint; Space lands in the buffer as a typed character, not as
// playback) — plus the settings editor's own bare-Tab value autocomplete
// (handle_settings_editor_key intercepts it before handle_key; the bpm
// editor has no Tab route, so bare Tab drops while it is open). Admitted
// keys flow into the existing editor routing unchanged, so the only
// NotConsumed keys that can reach the editors' command tails are the three
// allowlisted chords.
bool GuiInputHandler::modal_editor_key_blocked(GuiKey key,
                                               GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool is_esc    = (key == GuiKeys::Escape);
    const bool is_commit =
        (key == GuiKeys::Return || key == GuiKeys::KpEnter);
    const bool is_editor_ctrl_chord =
        (ctrl && (key == GuiKeys::A || key == GuiKeys::C ||
                  key == GuiKeys::X || key == GuiKeys::V));
    const bool is_editor_motion_or_edit =
        (key == GuiKeys::Left || key == GuiKeys::Right ||
         key == GuiKeys::Home || key == GuiKeys::End ||
         key == GuiKeys::BackSpace || key == GuiKeys::Delete);
    const bool is_printable =
        (!ctrl && !alt &&
         mods.codepoint >= 0x20 && mods.codepoint <= 0x7e);
    const bool is_settings_autocomplete =
        (text_editor::is_active(app.settings_editor) &&
         key == GuiKeys::Tab && !ctrl && !shift && !alt);
    const bool is_save =
        (ctrl && !shift && !alt && key == GuiKeys::S);
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    return !(is_esc || is_commit || is_editor_ctrl_chord ||
             is_editor_motion_or_edit || is_printable ||
             is_settings_autocomplete ||
             is_save || is_ctrl_q);
}

// The Esc-cancel semantics as a callable body, shared by the Esc key
// handler below and the render-view Esc-cancel paths
// (input_render_view.cpp). Requesting cancellation has two effects:
//   1. async_renderer.request_cancel() sets the worker's cancel flag,
//      which do_render passes through to the engine.
//   2. app.queue_cancel_requested = true so that on_batch_entry_complete
//      finalizes the batch instead of dispatching the next entry.
// Both are needed: (1) interrupts the current render mid-stream;
// (2) stops the batch state machine from advancing after the
// cancelled render's on_done fires.
// Cancel also disarms both parked slots that the worker-idle pump would
// otherwise resurrect: the parked archival command (app.pending_archival)
// and the parked target preview (GuiTargetRender::pending_, cleared via
// cancel_in_flight_update).
bool GuiInputHandler::cancel_archival_session() {
    if (async_renderer.is_busy()) {
        async_renderer.request_cancel();
        app.queue_cancel_requested = true;
        // Cancel means stop rendering: a parked archival command (a
        // dispatch that killed this render and is waiting out its drain)
        // AND a parked target preview are both disarmed, or the
        // worker-idle pump (finalize_render_run to maybe_dispatch_pending)
        // would resurrect a render the moment the cancel lands. The
        // preview slot is cleared through cancel_in_flight_update, which
        // also covers the updating... progress text and the case where
        // the busy render is the preview's own.
        app.pending_archival = {};
        target_render.cancel_in_flight_update();
        return true;
    }
    if (app.queue_running) {
        // Render-state housekeeping flag survives a frame past the
        // worker's actual completion (worker_state_ transitions
        // Running -> CompletionPending while is_busy() still returns
        // true; once on_completion_event fires it goes Idle). The
        // is_busy() branch above covers that window. This branch is
        // the rare case where queue_running is set but the worker has
        // already cleared — defensive, mirrors the prior behavior.
        // Both parked slots are disarmed here too, same pump-resurrection
        // reason as the is_busy() branch.
        app.queue_cancel_requested = true;
        app.pending_archival = {};
        target_render.cancel_in_flight_update();
        return true;
    }
    return false;
}

// Esc-cancel handlers for in-flight operations. See the declaration in
// input_handler.h for routing order.
bool GuiInputHandler::handle_escape_cancels(GuiKey key) {
    if (key != GuiKeys::Escape) return false;
    return cancel_archival_session();
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
        // Deliberately unclamped: this restores a previously-resting value,
        // already inside the playhead's [0, total - 1] domain
        // (move_playhead_to holds the ruling).
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
    // Ctrl+Alt+R: single render into the source directory using `title`
    // from settings. Empty batch_folder/batch_basename selects the
    // source-directory naming convention inside do_render. A successful
    // sibling wav publish emits
    // the .fingerprint sidecar, but not batch-only sidecars
    // (.warpmarkers / .phaseresetmarkers / .settings).
    // Title-not-set is a hard error surfaced from do_render.
    if (ctrl && alt && !shift &&
        key == GuiKeys::R) {
        if (app.source_audio_path.empty()) return true;

        // Pre-flight the live store on the GUI thread: a modeled defect
        // (a trimmed map-format render included) opens the
        // defect-resolution series; a non-modeled failure raises the
        // popup. Either way the dispatch is refused.
        if (!warp_render_preflight(app.warpmarkers.markers(),
                                   app.phaseresetmarkers.markers(),
                                   app.engine_settings.scale,
                                   app.engine_settings.output_format,
                                   app.trim.has_begin, app.trim.begin_frame,
                                   app.trim.has_end, app.trim.end_frame)) {
            return true;
        }

        // A render dispatch kills the running render — unless the running
        // render's fingerprint equals this command's, in which case the
        // command is a no-op: the worker is already producing exactly this
        // deliverable. Only a single archival render carries a session
        // fingerprint (a batch never matches, its empty fingerprint matching
        // nothing).
        std::vector<uint8_t> fingerprint =
            compute_live_render_fingerprint(app, audio);
        if (async_renderer.is_busy() && !fingerprint.empty() &&
            fingerprint == async_renderer.session_fingerprint()) {
            return true;
        }

        // Empty batch_folder/basename selects the source-dir naming
        // convention inside do_render.
        RenderRequest req = build_render_request(
            app.source_audio_path, app.warpmarkers.markers(),
            app.phaseresetmarkers.markers(), app.engine_settings,
            app.trim.has_begin, app.trim.begin_frame,
            app.trim.has_end,   app.trim.end_frame);
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);

        if (async_renderer.is_busy()) {
            // Kill the running render and park this command; the
            // worker-idle pump dispatches it once the cancellation drains.
            AppState::PendingArchivalCommand cmd;
            cmd.single      = true;
            cmd.fingerprint = std::move(fingerprint);
            cmd.reqs.push_back(std::move(req));
            kill_running_render_and_park(std::move(cmd));
            return true;
        }

        // The dispatch hands the request to the worker thread; on_done
        // fires on the GUI thread when the render finishes (success,
        // failure, or cancel).
        dispatch_single_archival_render(std::move(req),
                                        std::move(fingerprint));
        return true;
    }

    // Ctrl+Alt+I renders the Cartesian product of the per-marker iter ranges
    // authored in iteration mode. Output lands in
    // `<source_parent>/renders/<N>_render_iterations/`, one cell per product
    // point with basename `<seq>_<delta_csv>`; the artifact(s) per cell depend
    // on output_format — a `.wav` for wav output, or the map/midi sidecars for
    // a map output_format. The CSV holds the swept markers' deltas
    // in timeline order, formatted `%+0.2f`; markers with no iter range
    // authored are excluded from the CSV and contribute one fixed value (their
    // authored tempo_cents) to the product. Per-cell progress and Esc
    // cancellation are handled by the batch runner (start_render_batch and the
    // ActiveBatch lifecycle). Silent no-op outside iteration mode.
    if (ctrl && alt && !shift &&
        key == GuiKeys::I) {
        if (app.source_audio_path.empty()) return true;
        if (!app.iteration_mode_enabled) return true;

        // Pre-flight the live store: a modeled defect (a trimmed
        // map-format render included) opens the defect-resolution series
        // and refuses the whole sweep; a non-modeled failure refuses with
        // the popup. Per-cell tempo_cents mutations remain on the async
        // stderr backstop.
        if (!warp_render_preflight(app.warpmarkers.markers(),
                                   app.phaseresetmarkers.markers(),
                                   app.engine_settings.scale,
                                   app.engine_settings.output_format,
                                   app.trim.has_begin, app.trim.begin_frame,
                                   app.trim.has_end, app.trim.end_frame)) {
            return true;
        }

        // Snapshot markers in timeline order (the GuiWarpMarkers store is
        // sorted by time_frame, with ties legal). For each owning marker
        // build its per-cell delta list in integer cents: a single 0 when
        // no iter range is authored, otherwise the cents enumeration from
        // iter_start_cents to iter_end_cents inclusive. Deltas and tempos
        // share the one integer-cents domain, so the per-cell base + delta
        // below is plain integer addition — no conversion anywhere.
        const std::vector<GuiWarpMarker> base_warp_markers =
            app.warpmarkers.markers();
        std::vector<int>                  eligible_indices;
        std::vector<std::vector<int64_t>> per_marker_delta_cents;
        std::vector<bool>                 is_swept;
        for (int i = 0; i < static_cast<int>(base_warp_markers.size()); ++i) {
            const GuiWarpMarker& m = base_warp_markers[i];
            if (!iter_popup_eligible_marker(m)) continue;
            eligible_indices.push_back(i);
            const bool swept =
                m.iter_start_cents.has_value() && m.iter_end_cents.has_value();
            is_swept.push_back(swept);
            std::vector<int64_t> delta_cents;
            if (swept) {
                const int64_t start_cents = *m.iter_start_cents;
                const int64_t end_cents   = *m.iter_end_cents;
                // The editor commit enforces start <= end and the bracket is
                // session-only (wiped on mode exit), so an inverted bracket
                // here is an internal breach — refuse the dispatch loudly and
                // enqueue nothing, repairing no iter state (the state is
                // evidence; the bracket lifecycle owns wiping). Pre-mutation:
                // nothing above has touched app state or the queue.
                if (start_cents > end_cents) {
                    std::fprintf(stderr,
                        "warptempo_gui: render-iterations refused: marker %d "
                        "iter bracket start exceeds end\n", i);
                    return true;
                }
                for (int64_t c = start_cents; c <= end_cents; ++c) {
                    delta_cents.push_back(c);
                }
            } else {
                delta_cents.push_back(0);
            }
            per_marker_delta_cents.push_back(std::move(delta_cents));
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

        // Cap the Cartesian product before it can overflow or exhaust
        // memory: each cell is a full archival render, so a real sweep is
        // tens to hundreds of cells. The per-axis brackets don't bound the
        // product — a handful of markers each with a wide bracket multiply
        // into billions of cells, which narrows to a negative int at the
        // reserve/enumeration site (std::length_error) or exhausts memory
        // materializing RenderRequests. Accumulate with a CHECKED product
        // that refuses the instant the running total exceeds the cap, so no
        // overflow can occur (the cap sits far below any integer boundary).
        // kMaxIterSweepCells is planner-chosen anti-pathology insurance,
        // flagged for architect retune.
        constexpr size_t kMaxIterSweepCells = 10000;
        size_t total_cells = 1;
        bool over_cap = false;
        for (const auto& d : per_marker_delta_cents) {
            total_cells *= d.size();
            if (total_cells > kMaxIterSweepCells) { over_cap = true; break; }
        }
        if (total_cells == 0) return true;
        if (over_cap) {
            // Refuse before any allocation, batch-folder creation, request
            // materialization, or render kill/park. `total_cells` here is an
            // accurate lower bound on the true product (the running product
            // already exceeded the cap before every axis was folded in), so
            // report "more than <cap>" rather than computing the full
            // product. Iteration mode and the brackets survive for
            // correction — the wipe-and-exit tail below does not run.
            prompt.open_error_notice(
                "Iteration sweep refused: more than " +
                std::to_string(kMaxIterSweepCells) +
                " cells (cap " + std::to_string(kMaxIterSweepCells) +
                "). Narrow the marker brackets and retry.");
            return true;
        }

        std::filesystem::path src(app.source_audio_path);
        std::filesystem::path src_parent = src.parent_path();
        if (src_parent.empty()) src_parent = std::filesystem::path(".");
        const std::filesystem::path queue_root = src_parent / "renders";

        // Resolve the next batch index: max+1 over `<digits>_<anything>`
        // entries.
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

        // The cap check above bounds total_cells at kMaxIterSweepCells
        // (<= 10000), so this narrowing to int is exact — no truncation and
        // no negative wrap can reach the reserve/enumeration below.
        const int total = static_cast<int>(total_cells);
        int pad_width = 1;
        for (int n = total; n >= 10; n /= 10) ++pad_width;
        if (pad_width > 9) pad_width = 9;

        // Snapshot phase resets once — every cell shares the same
        // phase reset configuration, only marker tempo_cents values
        // differ across cells.
        const std::vector<GuiPhaseResetMarker> base_phase_resets =
            app.phaseresetmarkers.markers();

        // Cartesian product enumeration. `indices[k]` holds the
        // current cell coordinate along the k-th eligible marker
        // (timeline order). Rightmost dimension increments fastest:
        // consecutive cells differ in the last marker's delta first.
        const size_t num_dims = per_marker_delta_cents.size();
        std::vector<size_t> indices(num_dims, 0);

        std::vector<RenderRequest> reqs;
        reqs.reserve(total);
        for (int cell = 0; cell < total; ++cell) {
            std::string delta_csv;
            for (size_t k = 0; k < num_dims; ++k) {
                if (!is_swept[k]) continue;
                // Signed two-decimal text straight from cents — no double
                // round-trip (format_signed_delta_cents, warpmarkers.h).
                if (!delta_csv.empty()) delta_csv += ',';
                delta_csv += format_signed_delta_cents(
                    per_marker_delta_cents[k][indices[k]]);
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
                // +-kIterDeltaMaxCents a cell tempo can go non-positive,
                // and build_warp_frame_map's existing refusal on the async
                // render path (stderr) is the backstop. Base and delta live
                // in the one integer-cents domain, so the sum is plain
                // integer addition and the cell sidecar's N.NN spelling
                // re-parses to exactly this value — Ctrl+Alt+C promotion
                // stays closed under the grammar by type.
                cell_warp_markers[mi].tempo_cents =
                    base_warp_markers[mi].tempo_cents +
                    per_marker_delta_cents[k][indices[k]];
                // The engine doesn't consume iter values; clear them
                // so the request is quiet.
                cell_warp_markers[mi].iter_start_cents.reset();
                cell_warp_markers[mi].iter_end_cents.reset();
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
                if (indices[k] < per_marker_delta_cents[k].size()) break;
                indices[k] = 0;
            }
        }

        if (async_renderer.is_busy()) {
            // A render dispatch kills the running render; a sweep never
            // matches a session fingerprint, so there is no wait case.
            // Park the fully built batch for the worker-idle pump.
            AppState::PendingArchivalCommand cmd;
            cmd.reqs        = std::move(reqs);
            cmd.batch_label = "render iterations";
            kill_running_render_and_park(std::move(cmd));
        } else {
            start_render_batch(std::move(reqs), "render iterations");
        }
        // The sweep is committed to run either way (dispatched, or parked
        // behind the killed render's drain): iteration mode turns off after
        // fire, and exiting the mode IS the bracket clear (wipe_iter_state,
        // the chokepoint every other iter-mode exit runs). Safe here: every
        // request above carries its own per-cell marker copies, so nothing
        // dispatched reads the live iter fields.
        flag_editor.wipe_iter_state();
        app.iteration_mode_enabled = false;
        viewport.invalidate_top_strip();
        return true;
    }

    // The BPM sweep render fires from render_bpm_sweep(), triggered by Enter
    // in the bottom-strip BPM editor after a successful commit; there is no
    // key-dispatch handler for it here.

    // Ctrl+Alt+C commits the displayed render by INHERITING its .settings
    // wholesale. The mechanism is the file, which is FROZEN at dispatch and
    // never rewritten by render view: the handler strict-reads the entry's
    // dispatch-time bytes and applies the WHOLE of it as the new session —
    // both tab bands (viewport/zoom/playhead/read_only/trim), active_tab_view,
    // active_audio_view, playback_speed, follow, font_size, and the engine
    // block — alongside the source-domain marker pair the commit adopts. Render
    // entries are target-view states, so commit lands in 'T' on the entry's
    // dispatch tab at exactly the QUEUE-MOMENT zoom / viewport / playhead (the
    // browsed position is deliberately not preserved — render view is an audio
    // player with no per-entry memory). With the file immutable and the A/B tab
    // chords blocked in render view, the fingerprint-regression class is closed
    // by construction: nothing can drift the entry's sidecars between dispatch
    // and commit.
    // One carve-out from the wholesale inheritance: active_markers_view is NOT
    // applied — W/P is global by ruling, so the LIVE mode survives the commit
    // (recorded at the prefs block below).
    // Undo scope is unchanged: marker/reset promotion plus the pre-commit
    // engine settings are one cross-file undo entry; the inherited prefs and
    // view state stay outside undo by the same standing convention that keeps
    // view state and trim out of history (recorded at the undo push below).
    // After the commit succeeds: render-view exits (playback rebinds to the
    // always-source audio object; the entry buffer frees; the tail's preview
    // trigger then dispatches the adopted target buffer), and
    // <source_parent>/renders/ is recursively wiped. The committed render
    // survives through the render cache, not as a folder artifact. Silent
    // no-op outside render-view.
    if (ctrl && alt && !shift &&
        key == GuiKeys::C) {
        if (!app.render_view.enabled) return true;
        if (app.render_view.index < 0) return true;

        // app.render_view.warp_markers / .phase_resets are display state
        // (the snapshot vectors adopted wholesale — disabled rows included,
        // styled as in target view — at authored-domain positions). Ctrl+Alt+C
        // reads the entry's frozen source-domain sidecars and adopts them
        // wholesale: the `.settings` through the one strict whole-file schema
        // (read_settings_file, the same read load_render_view_at runs) and
        // both marker sidecars through their strict loaders
        // (GuiWarpMarkers::load / GuiPhaseResetMarkers::load). A syntactically
        // malformed sidecar aborts at its own read — a broken file genuinely
        // cannot be promoted, first error only — and that is the ONLY abort:
        // the entry sidecars are TRUSTED (written once at dispatch by
        // do_render, never hand-edited between display and commit), so there is
        // no adversarial content re-attestation of the assembled candidate. The
        // render worker's source-clobber backstop and the engine tripwires
        // remain the last-ditch guards on the promoted recipe. Walkable,
        // GUI-committable defects (coincident markers, dangling refs,
        // equal/inverted trim, first-marker grammar) adopt and walk the commit
        // series like any other commit.
        const auto& cur_e =
            app.render_view.list[app.render_view.index];
        const std::filesystem::path sidecar =
            render_view.settings_path(cur_e);
        // The entry's .settings is frozen at dispatch and never rewritten by
        // render view, so the strict read below sees the DISPATCH-TIME bytes —
        // which is the point: the commit inherits the position/tab the project
        // had when the render was QUEUED, not the browsed one.
        const auto settings = read_settings_file(sidecar.string());
        if (!settings) {
            std::fprintf(stderr,
                "warptempo_gui: commit aborted: settings read "
                "failed for '%s': %s\n",
                sidecar.string().c_str(),
                settings.error().c_str());
            return true;
        }
        std::vector<GuiWarpMarker>    src_warp;
        std::vector<GuiPhaseResetMarker> src_phase_resets;
        const std::filesystem::path wm =
            cur_e.batch_folder / (cur_e.basename + ".warpmarkers");
        const std::filesystem::path tm =
            cur_e.batch_folder / (cur_e.basename + ".phaseresetmarkers");
        {
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

        // The complete candidate is collected. The commit tab is the tab
        // named by the snapshot's active_tab_view (the dispatch tab the entry
        // was rendered from), and that tab's view-state band carries the
        // recipe trim that shaped this render — the same projection
        // load_render_view_at displays. The tab switch at the application site
        // happens before the trim restore, so the live fields it writes belong
        // to the commit tab.
        const char commit_tab = settings->active_tab_view;

        // No landing-view latch: the entry's frozen .settings — the
        // dispatch-time viewport/zoom/playhead/tab — IS the session this commit
        // inherits wholesale below (both tab bands, active_tab_view,
        // active_audio_view, the prefs, and the engine block; active_markers_view
        // is the one carve-out — W/P is global, so the live mode survives).
        // restore_source_view still runs the exit arm, but the post-restore
        // block overwrites its stash restore with the parsed file, so the exit
        // stash is transient here.
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

        // Attribute the entry to the mode the commit was performed in so undo/redo restore the user's context and interpret post-restore hints against that marker store.
        const char commit_marker_mode = app.active_markers_view;
        undo.push_undo_both(std::move(warp_pre), std::move(phase_reset_pre),
                       commit_marker_mode, hint_last, commit_tab);
        undo.recompute_dirty();
        // Undo scope, unchanged by the full inheritance: this entry captures
        // the marker pair and the pre-commit engine settings only
        // (capture_current_settings, snapshotted just now inside
        // push_undo_both). The inherited session prefs (playback_speed,
        // follow, font_size) and the whole view state (both tab bands,
        // active_tab_view, active_audio_view, the live position) ride OUTSIDE
        // undo — the same standing convention that
        // keeps view state and trim out of history. Ctrl+Z restores the
        // pre-commit stores and engine block exactly as before; it does not
        // roll back the adopted prefs or landing view. Grow this payload only
        // deliberately.

        // Full engine-settings commit. The whole snapshot was validated
        // before marker mutation, so commit can adopt the typed recipe
        // without degrading to the previous live settings.
        app.engine_settings = settings->engine;

        const std::filesystem::path src(app.source_audio_path);
        std::filesystem::path src_parent = src.parent_path();
        if (src_parent.empty()) src_parent = std::filesystem::path(".");
        const std::filesystem::path renders_root =
            src_parent / "renders";

        // Committing a render is a wholesale authoring reset. Clear every
        // marker's session-only iteration and bpm state and turn off both
        // sweep modes' visibility. A bpm sweep already wiped its state on
        // dispatch, but the reset wipes unconditionally so a render committed
        // from any path leaves no session-only state behind.
        // No separate undo entry: the commit pushes its own cross-file undo
        // and wipes renders/; iter and bpm values are session-only, never
        // serialized (wipe_bpm_state is history-less by design). A full-window
        // repaint at the tail covers both strips the wipes touch.
        {
            auto& mv = app.warpmarkers.markers_mut();
            for (auto& m : mv) {
                m.iter_start_cents.reset();
                m.iter_end_cents.reset();
            }
        }
        flag_editor.wipe_bpm_state();
        app.iteration_mode_enabled = false;
        app.bpm_mode_enabled       = false;

        // Deliberately no cancel_archival_session on this exit: the tail's
        // target_render.trigger() already stops or adopts a running rebuild
        // through its kill/match-wait — a rebuild whose session fingerprint
        // matches the just-committed state is ADOPTED as the preview,
        // deliberate reuse, not a leak.
        render_view.restore_source_view();

        // Full inheritance (the ruled shape): the entry's frozen .settings —
        // the dispatch-time view — is the whole session now, the
        // same SettingsFileTab-to-live application a source load performs.
        // Both tab bands come from the file (view_state_from_settings_tab, the
        // shared band conversion: viewport/zoom/playhead, read_only, and the
        // trim pair; a parsed band carries no selection, matching the
        // wholesale marker-selection reset above). This replaces the exit
        // stash restore_source_view just applied — the stash was transient.
        app.tab_a = view_state_from_settings_tab(settings->tab_a);
        app.tab_b = view_state_from_settings_tab(settings->tab_b);

        // active_tab_view from the file (== commit_tab). Set it directly —
        // NOT switch_active_tab_view_to, which would
        // swap the live fields WITH the tab slots and re-push the authoring
        // position; both bands are already the file's, so the live fields are
        // pulled straight from the active band with no double-apply.
        app.active_tab_view = commit_tab;
        {
            const ViewState& band =
                (commit_tab == 'B') ? app.tab_b : app.tab_a;
            // Live viewport/zoom/playhead from the active band land at the
            // dispatch-time (queue-moment) position the entry was written with.
            // The render display axis IS the full target axis of the snapshot
            // map, so this lands at that position with no translation; zoom
            // levels share one vocabulary across views.
            app.viewport_start_sample  = band.viewport_start_sample;
            app.zoom_level             = band.zoom_level;
            // Deliberately unclamped, the restore-site convention: the
            // dispatch-time playhead may rest outside the adopted map's target
            // extent; move_playhead_to owns the value at first use.
            app.playhead_cursor_sample = band.playhead_cursor_sample;
            // Live trim is the active band's trim, which is exactly the
            // recipe trim that shaped this render: the frozen file's per-tab
            // trim is intact, and the commit tab's band trim is that recipe
            // trim (both read from the same parsed file), so it is the trim
            // now live.
            // A committed render lands with no trim bound focused: a parsed
            // band carries the default false/0 trim-selection fields, matching
            // the marker-selection reset.
            app.trim                = band.trim;
            app.trim_begin_selected = band.trim_begin_selected;
            app.trim_end_selected   = band.trim_end_selected;
            app.last_selected_trim  = band.last_selected_trim;
            app.last_sel_group      = band.last_sel_group;
        }

        // Session prefs from the file, the same application path a source
        // load uses (has_X ? X : default; a program-written entry carries
        // every key). These ride OUTSIDE undo — see the undo-scope note above.
        //
        // Recorded asymmetry: active_markers_view is deliberately NOT applied
        // from the file. W/P is global by ruling — one mode binary affecting
        // every view alike — so the LIVE mode survives the commit (a `p` flip
        // made while auditioning stays put), unlike playback_speed, follow,
        // font_size, active_audio_view, and both tab bands, which all adopt
        // from the file here.
        app.follow_mode    = settings->has_follow ? settings->follow : true;
        app.playback_speed = settings->has_playback_speed
            ? settings->playback_speed : 1.0f;
        // Push the inherited speed to the engine so playback picks it up, the
        // same call load_file makes after the parse.
        playback.set_speed(app.playback_speed);
        // font_size through the gesture's live-apply route: the renderer
        // file-scope state plus the resize-path geometry rebuild
        // (set_gui_font_size_pt + on_resize), exactly as load_file applies a
        // parsed font_size. The full-window repaint at this handler's tail
        // supplies the damage.
        app.font_size = settings->has_font_size ? settings->font_size : 11.0;
        set_gui_font_size_pt(app.font_size);
        paint_handler.on_resize(app.width, app.height);

        // active_audio_view from the file — 'T' by the dispatch writer's
        // construction, which is also the ruled landing. The flag flip is the
        // whole target-view entry mechanism: restore_source_view above rebound
        // playback to the source samples (and, when the pre-commit session was
        // already in 'T', its ensure_ready funnel ran against the adopted
        // stores), and the tail's target_render.trigger() marks the buffer
        // stale and dispatches the adopted preview, which rebinds playback on
        // completion — superseding any buffer the funnel bound.
        app.active_audio_view = settings->has_active_audio_view
            ? settings->active_audio_view : 'S';

        if (!app.playhead_scanner_active) {
            app.playhead_scanner_sample = app.playhead_cursor_sample;
        }
        clamp_viewport_start(app, audio);
        viewport.clear_hover_popup();
        viewport.kick_waveform_sync();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();

        target_render.trigger();

        app.render_view.list.clear();
        render_view.clear_snapshot_context();
        app.render_view.index             = -1;
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
            // Forced bpm-off routes through the exit_bpm_mode chokepoint so
            // it wipes the session-only bpm state like any other mode exit.
            // Backstop only: bpm mode is exactly its modal editor session
            // now, and the modal key gate drops `i` while that editor is
            // open, so this branch has no reachable path.
            const bool turning_on = !app.iteration_mode_enabled;
            if (turning_on && app.bpm_mode_enabled) {
                flag_editor.exit_bpm_mode();
            }
            if (!turning_on) {
                // Turning iteration mode OFF wipes every marker's
                // session-only iter bracket — exiting the mode is the
                // clear (wipe_iter_state, shared with enter_bpm_mode's
                // forced iter-off so the two exit routes cannot drift).
                // Runs before the flag flips.
                flag_editor.wipe_iter_state();
            }
            app.iteration_mode_enabled = !app.iteration_mode_enabled;
            viewport.clear_hover_popup();
            viewport.invalidate_top_strip();
        }
        return true;
    }

    // `m` (no modifiers): open the BPM editor on the earlier of two
    // selected markers that define an explicit span. Warp view only; silent
    // no-op in phase reset view. Mutual exclusion with iter mode is handled
    // inside enter_bpm_mode. The gate requires exactly two selected markers
    // with no label_ref anywhere in the span; any other selection is a
    // silent no-op. There is no toggle-off branch: the bpm editor is a
    // modal bottom-strip surface, so while it is open `m` never reaches
    // this dispatch — it is just a typed character the bracket grammar
    // rejects — and bpm mode never rests without its editor (the mode's
    // only exits are the editor's own: Esc, and Enter's dispatch tail).
    if (key == GuiKeys::M && !ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') return true;
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
        // The bpm editor is a modal bottom-strip surface: stop playback at
        // its open. Space is inside the modal blocked set, so playback
        // cannot restart until the editor closes.
        playback_lifecycle.stop_playback_if_playing();
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
    const bool alt   = mods.alt;

    // Ctrl+Tab toggles A/B navigational tabs. Stops playback, saves
    // current viewport/zoom/playhead to the leaving tab, restores the
    // target tab. Does not mark the document dirty. Alt-strict: an Alt
    // held alongside makes the chord an unbound no-op, never this binding.
    if (ctrl && !shift && !alt && key == GuiKeys::Tab) {
        // Render view blocks Ctrl+Tab outright at the key gate
        // (render_view_key_blocked's EXTRA BLOCKS), so this arm runs only in
        // authoring views: the A/B tab is authoring view state and render view
        // may not mutate the live tab. The trigger dispatches a target-view
        // preview, always safe here for the same reason.
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        target_render.trigger();
        return true;
    }

    // Ctrl+Shift+Tab: advance both tabs' marker focus and end on the
    // opposite tab. Composes bare Tab and Ctrl+Tab so the user can
    // march paired tabs forward in lockstep with one chord. Render view blocks
    // this chord at the key gate (like Ctrl+Tab), so this arm is authoring-only;
    // cycle_marker_focus_with_recenter survives unchanged for those views.
    if (ctrl && shift && !alt && key == GuiKeys::Tab) {
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
    // cycle path either. Alt-strict everywhere: an Alt held makes the chord
    // an unbound no-op rather than falling into the cycle.
    if (!ctrl && !alt && key == GuiKeys::Tab && !shift) {
        cycle_marker_focus_with_recenter(true);  return true;
    }
    if (!ctrl && !alt && key == GuiKeys::Tab && shift)  {
        cycle_marker_focus_with_recenter(false); return true;
    }
    if (!ctrl && !alt && key == GuiKeys::IsoLeftTab)    {
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
                // render_bpm_sweep owns the mode teardown on its success
                // path: after the batch is built and accepted (dispatched,
                // or parked behind a killed render's drain — a busy worker
                // no longer bails) it wipes the session-only bpm state and
                // exits bpm mode, so an accepted sweep leaves no marker
                // carrying bpm state and the next M on this marker seeds
                // []. A guard-bail (return false) is an environmental
                // backstop — batch-folder creation failure, no valid
                // cells; the stale-endpoint / store-defect classes are
                // unreachable because the modal bpm session freezes the
                // store between mode entry and this dispatch. The commit
                // already closed the editor, and bpm mode is exactly its
                // editor session, so a bail exits the mode here —
                // mode-without-editor stays unreachable.
                if (!render_bpm_sweep()) {
                    flag_editor.exit_bpm_mode();
                }
            } else if (!text_editor::is_active(app.top_flag_editor)) {
                // commit_bpm_edit closed the editor without committing
                // (the invalid-target backstop): take the mode down with
                // it. A red-flash refusal leaves the editor open and
                // deliberately does not land here.
                flag_editor.exit_bpm_mode();
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
    // NotConsumed: the editor does not own this key. The two kinds split
    // here. The bpm editor is a modal bottom-strip surface: the on_key
    // gate (modal_editor_key_blocked) admits only its own keys plus Esc,
    // Ctrl+S, and Ctrl+Q, so a NotConsumed key is one of those three
    // chords. Ctrl+S saves with the editor (and the bpm session) left
    // open — save is not an exit; Esc and Enter are the mode's only
    // exits. Ctrl+Q tears the editor and the mode down together
    // (mode-without-editor stays unreachable) and falls through so the
    // global dispatch runs the close routing. Anything else is
    // swallowed as a backstop.
    if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        if (mods.ctrl && !mods.shift && !mods.alt && key == GuiKeys::S) {
            save_ops.save();
            return true;
        }
        if (mods.ctrl && !mods.shift && !mods.alt && key == GuiKeys::Q) {
            flag_editor.exit_top_flag_edit_no_commit();
            flag_editor.exit_bpm_mode();
            return false;  // let on_key run the close routing
        }
        return true;  // modal: swallow
    }
    // Top-strip flag editor (deliberately non-modal): the key is a command.
    // Cancel the edit (Esc-discard: no commit, no validation), using the
    // same teardown Esc uses, then fall through (no return) so the key
    // reaches the global command dispatch below and runs. This is how every
    // command (Ctrl+Q/S, Ctrl+Z, Ctrl+Tab, Ctrl+P, ...) works
    // mid-edit: exit first, then the command. No command list — the editor
    // owns only its editing keymap and everything else punches through.
    flag_editor.exit_top_flag_edit_no_commit();
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
    // NotConsumed: the settings editor is a modal bottom-strip surface —
    // the on_key gate (modal_editor_key_blocked) admits only its own keys
    // plus Esc, Ctrl+S, and Ctrl+Q, so a NotConsumed key is one of those
    // three chords. Ctrl+S saves with the editor left open (save is not an
    // exit); Ctrl+Q discards the edit (Esc-discard) and falls through so
    // the global dispatch runs the close routing. Anything else
    // is swallowed as a backstop.
    if (ctrl && !shift && !alt && key == GuiKeys::S) {
        save_ops.save();
        return true;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        settings_editor.exit_no_commit();
        return false;  // let on_key run the close routing
    }
    return true;  // modal: swallow
}
