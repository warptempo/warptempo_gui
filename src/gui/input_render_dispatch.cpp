#include "input_handler.h"

#include "render_pipeline.h"
#include "settings_io.h"
#include "time_format.h"
#include "frame_map_view.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// GuiInputHandler render-dispatch / batch-lifecycle methods, lifted verbatim
// from input_handler.cpp (snapshot_current_queued_render, finalize_render_run,
// start_render_batch, dispatch_next_batch_entry, on_batch_entry_complete,
// render_bpm_sweep). Pure definition move; no body changes.

AppState::QueuedRender GuiInputHandler::snapshot_current_queued_render() const {
    AppState::QueuedRender q;
    q.source_audio_path = app.source_audio_path;
    q.markers            = app.warpmarkers.markers();
    q.phase_resets       = app.phase_reset_markers.markers();
    q.engine_settings    = app.engine_settings;
    q.has_trim_begin     = app.trim.has_begin;
    q.trim_begin_sec     = app.trim.begin_seconds;
    q.has_trim_end       = app.trim.has_end;
    q.trim_end_sec       = app.trim.end_seconds;
    return q;
}

void GuiInputHandler::finalize_render_run() {
    app.queue_running          = false;
    app.queue_cancel_requested = false;
    // Invalidate the bottom strip before clearing queue_progress_text.
    // timestamp_invalidate_rect() covers the whole bottom strip; keep this
    // ordering consistent with the other status-clear paths.
    viewport.invalidate_timestamp_area();
    app.queue_progress_text.clear();
    // A target-view edit during this archival render may have queued a
    // pending target render. Worker is now idle — fire it.
    target_render.maybe_dispatch_pending();
}

void GuiInputHandler::start_render_batch(std::vector<RenderRequest> reqs,
                                         std::string batch_label) {
    if (reqs.empty()) return;

    // Snapshot the batch's destination folder before moving reqs onto
    // batch_. Each per-entry dispatch moves a RenderRequest out of
    // batch_.reqs, so reqs.front().batch_folder is only readable here
    // — by the time the terminal success branch needs it for auto-
    // open, it's been moved away. All three batch call sites construct
    // every RenderRequest in the batch with the same batch_folder
    // string, so reading the front entry is canonical.
    batch_.batch_folder = std::filesystem::path(reqs.front().batch_folder);

    batch_.reqs       = std::move(reqs);
    batch_.label      = std::move(batch_label);
    batch_.next_index = 0;
    batch_.rendered   = 0;
    batch_.active     = true;

    app.queue_cancel_requested = false;
    app.queue_running          = true;
    viewport.clear_hover_popup();

    dispatch_next_batch_entry();
}

void GuiInputHandler::dispatch_next_batch_entry() {
    if (!batch_.active) return;

    const int total = static_cast<int>(batch_.reqs.size());

    // Batch terminates if Esc was pressed since the last dispatch OR if
    // we ran out of entries. Either way: log a summary and clean up.
    const bool out_of_entries = (batch_.next_index >= total);
    const bool cancelled      = app.queue_cancel_requested;
    if (out_of_entries || cancelled) {
        if (cancelled) {
            std::fprintf(stderr,
                "warptempo_gui: %s: rendered %d of %d entries (cancelled)\n",
                batch_.label.c_str(), batch_.rendered, total);
        } else {
            std::fprintf(stderr,
                "warptempo_gui: %s: rendered %d of %d entries\n",
                batch_.label.c_str(), batch_.rendered, total);
        }
        // Auto-open render-view at the just-rendered batch's first
        // file. Gated on a non-cancelled terminal branch that
        // actually produced at least one .wav on disk; a batch
        // where every entry returned Failed leaves rendered == 0
        // and there is nothing to view. Per the render-view
        // gatekeeper invariant, render-view must be off here:
        // S / E / Ctrl+Alt+R / Ctrl+Alt+E / Ctrl+Alt+I are all dropped
        // while render-view is active, and the BPM sweep fires only from
        // the BPM editor's Enter (which cannot be open in render-view), so
        // no batch dispatch can reach this terminal branch from inside
        // render-view. The call runs before finalize_render_run +
        // reqs.clear so render-view sees the same surrounding
        // state ordering it would on a manual `r` toggle.
        const bool success = !cancelled && batch_.rendered > 0;
        if (success) {
            render_view.auto_open_batch_at_first_file(batch_.batch_folder);
        }
        batch_.active = false;
        batch_.reqs.clear();
        batch_.reqs.shrink_to_fit();
        finalize_render_run();
        return;
    }

    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "%s: rendering %d of %d...",
                  batch_.label.c_str(),
                  batch_.next_index + 1, total);
    app.queue_progress_text = buf;
    viewport.invalidate_timestamp_area();

    RenderRequest req = std::move(batch_.reqs[batch_.next_index]);
    async_renderer.dispatch(std::move(req),
        [this](RenderOutcome o) { on_batch_entry_complete(o); });
}

void GuiInputHandler::on_batch_entry_complete(RenderOutcome outcome) {
    if (!batch_.active) return;

    if (outcome == RenderOutcome::Success)   ++batch_.rendered;
    if (outcome == RenderOutcome::Cancelled) app.queue_cancel_requested = true;

    ++batch_.next_index;
    dispatch_next_batch_entry();
    // dispatch_next_batch_entry either dispatched the next batch
    // entry (worker is busy → pending target render stays queued) or
    // finalized via finalize_render_run (which already pumps the
    // pending target render). Either way we don't need to call
    // maybe_dispatch_pending here.
}

// Human-readable provenance descriptor for a committed BPM cell,
// e.g. "36 beats @ 220 bpm from 00:32.008 to 00:46.562". Beats and bpm are
// integers; the two timestamps are the span's owner and endpoint marker
// times, formatted via the shared mm:ss.mmm formatter. Stored verbatim in
// the cell's .rendersettings bpm= field and promoted into .settings on
// commit.
static std::string format_bpm_descriptor(int beats, int bpm,
                                         double start_seconds,
                                         double end_seconds) {
    return std::to_string(beats) + " beats @ " +
           std::to_string(bpm) + " bpm from " +
           format_timestamp(start_seconds) + " to " +
           format_timestamp(end_seconds);
}

// Sweep every BPM in the BPM owner's
// [bpm_lo, bpm_hi] range, computing (base_tempo, scale) per cell and
// rendering one .wav per cell into
// `<source_parent>/renders/<N>_render_bpm_iterations/`. The per-cell
// engine values land in the `.rendersettings` sidecar's engine block
// (written by do_render); Ctrl+Alt+C reads only the scale field back when
// committing a BPM cell. The substantive difference from the iter render
// handler is per-cell mutation of cell_settings.scale, in addition to
// per-cell marker mutation. Returns true iff a batch was dispatched; every
// guard bail returns false. Body is the former Ctrl+Alt+M block verbatim,
// minus the keystroke gate.
bool GuiInputHandler::render_bpm_sweep() {
    if (app.active_markers_view != 'W') return false;
    if (!app.bpm_mode_enabled) return false;
    if (app.source_audio_path.empty()) return false;
    if (audio.sample_rate() <= 0) return false;
    if (audio.total_frames() <= 0) return false;

    const std::vector<GuiWarpMarker> base_markers =
        app.warpmarkers.markers();
    int owner_idx = -1;
    for (int i = 0; i < static_cast<int>(base_markers.size()); ++i) {
        if (base_markers[i].bpm_owner) {
            owner_idx = i;
            break;
        }
    }
    if (owner_idx < 0) return false;
    const GuiWarpMarker& owner = base_markers[owner_idx];
    if (owner.bpm_beats <= 0) return false;
    if (owner.bpm_lo    <= 0) return false;
    if (owner.bpm_hi    <= 0) return false;

    // Span endpoint is explicit (set on the `m` two-marker span gate).
    const int endpoint_idx = owner.bpm_endpoint;
    if (endpoint_idx <= owner_idx ||
        endpoint_idx >= static_cast<int>(base_markers.size())) {
        return false;   // missing or malformed span: no sweep
    }
    const double duration_seconds =
        base_markers[endpoint_idx].time_seconds - owner.time_seconds;
    if (!(duration_seconds > 0.0)) return false;

    std::vector<int> bpm_values;
    for (int b = owner.bpm_lo; b <= owner.bpm_hi; ++b) {
        bpm_values.push_back(b);
    }
    if (bpm_values.empty()) return false;

    std::filesystem::path src(app.source_audio_path);
    std::filesystem::path src_parent = src.parent_path();
    if (src_parent.empty()) src_parent = std::filesystem::path(".");
    const std::filesystem::path queue_root = src_parent / "renders";

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

    const std::string command_tag = "render_bpm_iterations";
    const std::filesystem::path batch_folder =
        queue_root /
        (std::to_string(next_index) + "_" + command_tag);

    int pad_width = 1;
    for (int n = static_cast<int>(bpm_values.size());
         n >= 10; n /= 10) ++pad_width;
    if (pad_width > 9) pad_width = 9;

    const std::vector<GuiPhaseResetMarker> base_phase_resets =
        app.phase_reset_markers.markers();

    std::vector<RenderRequest> reqs;
    reqs.reserve(bpm_values.size());
    int seq = 1;
    for (int bpm : bpm_values) {
        const auto computed = compute_base_tempo_scale(
            duration_seconds, owner.bpm_beats, bpm);
        if (!computed) {
            std::fprintf(stderr,
                "warptempo_gui: render-bpm: rejected cell "
                "bpm=%d (duration=%.6f, beats=%d)\n",
                bpm, duration_seconds, owner.bpm_beats);
            continue;
        }

        std::vector<GuiWarpMarker> cell_markers = base_markers;
        // Owner: concrete computed base tempo, scale carried in settings.
        cell_markers[owner_idx].tempo_inherits = false;
        cell_markers[owner_idx].tempo_base     = computed->base_tempo;
        cell_markers[owner_idx].tempo_scale.clear();
        // Span-internal markers pass: their own tempo is subsumed by the
        // owner's span tempo. Disabled span-internal markers stay disabled
        // but also pass (the disabled flag is independent of tempo_inherits).
        for (int i = owner_idx + 1; i < endpoint_idx; ++i) {
            cell_markers[i].tempo_inherits = true;
            cell_markers[i].tempo_base     = 1.0;       // inert default
            cell_markers[i].tempo_scale    = "1.0000";  // model's inert scale
            // label_def on a span-internal marker is preserved (refs are
            // excluded from spans by the `m` two-marker span gate, but a
            // def may exist);
            // only the tempo fields are rewritten. Do not touch label_def,
            // disabled, or any non-tempo field.
        }
        // endpoint marker: untouched — its section lies outside the span.

        EngineSettings cell_settings = app.engine_settings;
        cell_settings.scale = computed->scale;
        // Provenance descriptor for this cell's .rendersettings; promoted
        // verbatim into .settings on Ctrl+Alt+C commit.
        cell_settings.bpm =
            format_bpm_descriptor(owner.bpm_beats, bpm,
                                  owner.time_seconds,
                                  base_markers[endpoint_idx].time_seconds);

        char num_buf[16];
        std::snprintf(num_buf, sizeof(num_buf),
                      "%0*d", pad_width, seq);
        char rest_buf[64];
        std::snprintf(rest_buf, sizeof(rest_buf),
                      "_%d,%.2f,%.6f",
                      bpm, computed->base_tempo, computed->scale);
        std::string basename = num_buf;
        basename += rest_buf;

        reqs.push_back(build_render_request(
            app.source_audio_path, std::move(cell_markers), base_phase_resets,
            std::move(cell_settings),
            app.trim.has_begin, app.trim.begin_seconds,
            app.trim.has_end,   app.trim.end_seconds,
            audio.sample_rate(),
            batch_folder.string(), std::move(basename)));
        ++seq;
    }

    if (reqs.empty()) {
        std::fprintf(stderr,
            "warptempo_gui: render-bpm: no valid cells; "
            "nothing to render\n");
        return false;
    }

    std::filesystem::create_directories(batch_folder, ec);
    if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: render-bpm: could not create "
            "'%s': %s\n",
            batch_folder.string().c_str(), ec.message().c_str());
        return false;
    }

    if (async_renderer.is_busy()) return false;
    start_render_batch(std::move(reqs), "bpm");
    return true;
}
