#include "input_handler.h"

#include "marker_store_validate.h"
#include "phaseresetmarkers.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "time_format.h"
#include "trimmer.h"
#include "value_format.h"
#include "warp_frame_map_view.h"
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
// from input_handler.cpp (snapshot_current_authoring_state,
// snapshot_current_queued_render, finalize_render_run,
// start_render_batch, dispatch_next_batch_entry, on_batch_entry_complete,
// render_bpm_sweep). Pure definition move; no body changes.

// Render dispatch pre-flight. Two passes on the GUI thread, both
// marker-count-sized and cheap.
//
// First the raw-store walk: enumerate_marker_store_defects over both marker
// columns plus the trim checks (frame-level crossed-or-equal, then — live
// stores only — the map-format-with-trim conflict and, when the marker walk
// is clean and the live map builds, validate_trim_frames, mirroring
// open_defect_series' trim column). On any modeled defect a LIVE-store site
// (`live_store` true, validating app.warpmarkers / app.phaseresetmarkers /
// app.trim at dispatch time) opens the defect-resolution series — the
// primary surface; the user resolves and re-triggers the dispatch — while a
// QUEUED-SNAPSHOT site raises the error-notice popup with the first
// defect's message verbatim. Either way the dispatch is refused and the
// site enqueues nothing.
//
// When the raw walk is clean, the resolve-then-build chain the render worker
// runs — resolve_warp_markers_for_render (which validates the first-marker
// render grammar first) then build_warp_frame_map — runs against the given
// marker list and scale, plus, when a trim bound is set, the map-format
// refusal (trim is wav-only, ruled; the raw walk above already routes the
// live-store case into the series, so this popup serves queued snapshots —
// a snapshot's format and trim are frozen, nothing live to resolve — and
// stays the backstop beneath everything) and validate_trim_frames against
// the built map. This chain is the loud backstop for anything the
// enumerator does not model (effectively the engine-metadata and
// non-positive-tempo-product class), surfacing through the error-notice
// popup with the owner's error string, unmodified. The async pipeline's
// existing stderr failure paths remain the backstop for what the pre-flight
// never sees (per-cell tempo/scale mutations in the sweep batches); the
// popup never blocks the render thread and no strings flow through the
// async callback.
bool GuiInputHandler::warp_render_preflight(
        const std::vector<GuiWarpMarker>& markers,
        const std::vector<GuiPhaseResetMarker>& phase_resets,
        bool live_store, double scale,
        const std::string& output_format,
        bool has_trim_begin, double trim_begin_frame,
        bool has_trim_end, double trim_end_frame) {
    const long sr    = static_cast<long>(audio.sample_rate());
    const long total = static_cast<long>(audio.total_frames());

    // Raw-store walk: find the first modeled defect (chronological within
    // the marker walk; trim after). Mirrors open_defect_series' predicate
    // so a live-store refusal here always has a series to open.
    std::string first_defect;
    if (sr > 0 && total > 0) {
        std::vector<MarkerDefect> defects = enumerate_marker_store_defects(
            slice_to_warp_markers(markers),
            slice_to_phase_reset_markers(phase_resets), sr);
        if (!defects.empty()) {
            first_defect = std::move(defects.front().message);
        }
        if (first_defect.empty() && (has_trim_begin || has_trim_end)) {
            // Exact frame-double compare on the authored bounds — no
            // rounding anywhere, matching validate_trim_frames' own
            // e_src <= b_src check.
            if (has_trim_begin && has_trim_end &&
                trim_end_frame <= trim_begin_frame) {
                first_defect = "trim bounds crossed or equal";
            } else if (live_store && output_format != "wav") {
                // Map-format-with-trim conflict: modeled by the series'
                // trim column, so a live-store dispatch opens the walk
                // (its [U]ndo/[R]eset/[Delete] modal) instead of reaching
                // the resolve-chain's plain popup below. Snapshots skip
                // this branch and keep the popup path, same message.
                first_defect =
                    "map formats take no trim; clear the trim or render wav";
            } else if (live_store) {
                // validate_trim_frames under the series' guard (clean
                // markers, built map), live store only: the memoized
                // target-view cache is keyed on the live store, so a
                // snapshot has no cheap map here — the resolve/build chain
                // below covers the same class for snapshots through the
                // same popup a snapshot defect gets anyway.
                const TargetWarpFrameMapCache& c =
                    target_view_warp_frame_map_cached(
                        app, audio.sample_rate(), total);
                if (c.build_error.empty()) {
                    if (auto v = validate_trim_frames(
                            has_trim_begin, trim_begin_frame,
                            has_trim_end,   trim_end_frame,
                            static_cast<int64_t>(total),
                            c.warp_frame_map); !v) {
                        first_defect = std::move(v.error());
                    }
                }
            }
        }
    }
    if (!first_defect.empty()) {
        if (live_store) {
            // Live store: the defect series IS the surface. It
            // re-enumerates from scratch and walks the defects
            // chronologically, one modal per defect; a refused dispatch
            // enqueues nothing, and the user re-triggers the render after
            // the series resolves (no auto-proceed).
            open_defect_series(/*commit_context=*/false);
        } else {
            // Queued snapshot: a snapshot cannot be fixed by mutating the
            // live store, and it was gated live at enqueue time, so this
            // popup is a backstop, not a primary surface.
            prompt.open_error_notice(std::move(first_defect));
        }
        return false;
    }

    auto resolved = resolve_warp_markers_for_render(
        slice_to_warp_markers(markers), sr);
    if (!resolved) {
        prompt.open_error_notice(std::move(resolved.error()));
        return false;
    }
    auto built = build_warp_frame_map(
        *resolved, scale, audio.sample_rate(),
        static_cast<long>(audio.total_frames()));
    if (!built) {
        prompt.open_error_notice(std::move(built.error()));
        return false;
    }
    if (has_trim_begin || has_trim_end) {
        if (output_format != "wav") {
            prompt.open_error_notice(
                "map formats take no trim; clear the trim or render wav");
            return false;
        }
        if (auto v = validate_trim_frames(
                has_trim_begin, trim_begin_frame, has_trim_end, trim_end_frame,
                static_cast<int64_t>(audio.total_frames()), *built); !v) {
            prompt.open_error_notice(std::move(v.error()));
            return false;
        }
    }
    return true;
}

AuthoringSnapshot GuiInputHandler::snapshot_current_authoring_state() const {
    AuthoringSnapshot s;
    s.valid             = true;
    s.active_tab        = app.active_tab_view;
    s.active_audio_view = app.active_audio_view;
    s.has_trim_begin    = app.trim.has_begin;
    s.trim_begin_frame    = app.trim.begin_frame;
    s.has_trim_end      = app.trim.has_end;
    s.trim_end_frame      = app.trim.end_frame;
    s.zoom_level        = app.zoom_level;
    s.viewport_start    = app.viewport_start_sample;
    s.playhead          = app.playhead_cursor_sample;
    return s;
}

void GuiInputHandler::attach_shared_render_resources(RenderRequest& req) {
    req.render_cache        = &target_render.render_cache;
    req.source_samples      = audio.samples_shared();
    req.source_total_frames = audio.total_frames();
    req.source_load_size  = audio.source_load_size();
    req.source_load_mtime = audio.source_load_mtime();
}

AppState::QueuedRender GuiInputHandler::snapshot_current_queued_render() const {
    AppState::QueuedRender q;
    q.source_audio_path = app.source_audio_path;
    q.warp_markers            = app.warpmarkers.markers();
    q.phase_resets       = app.phaseresetmarkers.markers();
    q.engine_settings    = app.engine_settings;
    q.has_trim_begin     = app.trim.has_begin;
    q.trim_begin_frame     = app.trim.begin_frame;
    q.has_trim_end       = app.trim.has_end;
    q.trim_end_frame       = app.trim.end_frame;
    q.authoring          = snapshot_current_authoring_state();
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
// e.g. "36 beats @ 220 bpm from 00:32.008 to 00:46.562". Beats is an
// integer; bpm is a double printed in plain shortest round-trip form
// ("220" stays "220", "220.5" stays "220.5"); the two timestamps are the
// span's owner and endpoint marker times in display seconds
// (frame / sample_rate, converted by the caller), formatted via the shared
// mm:ss.mmm formatter. Stored verbatim in the cell's .rendersettings bpm=
// field and promoted into .settings on commit.
static std::string format_bpm_descriptor(int beats, double bpm,
                                         double start_seconds,
                                         double end_seconds) {
    return std::to_string(beats) + " beats @ " +
           format_value_double(bpm, 0) + " bpm from " +
           format_timestamp(start_seconds) + " to " +
           format_timestamp(end_seconds);
}

// Sweep every BPM in the BPM owner's [bpm_lo, bpm_hi] range, computing
// (base_tempo, scale) per cell and rendering one .wav per cell into
// `<source_parent>/renders/<N>_render_bpm_iterations/`. The per-cell engine
// values land in the `.rendersettings` sidecar's engine block (written by
// do_render); Ctrl+Alt+C reads only the scale field back when committing a BPM
// cell. The substantive difference from the iter render handler is per-cell
// mutation of cell_settings.scale, in addition to per-cell marker mutation.
// Returns true iff a batch was dispatched; every guard bail returns false.
// Body is the former Ctrl+Alt+M block verbatim, minus the keystroke gate.
bool GuiInputHandler::render_bpm_sweep() {
    if (app.active_markers_view != 'W') return false;
    if (!app.bpm_mode_enabled) return false;
    if (app.source_audio_path.empty()) return false;
    if (audio.sample_rate() <= 0) return false;
    if (audio.total_frames() <= 0) return false;

    const std::vector<GuiWarpMarker> base_warp_markers =
        app.warpmarkers.markers();

    // Pre-flight before any cell work. This base site validates the LIVE
    // store at dispatch time — base_warp_markers is a just-taken copy of
    // app.warpmarkers, and the settings/trim arguments read the live state
    // directly — so a modeled defect (a trimmed map-format render
    // included) opens the defect-resolution series; a non-modeled failure
    // refuses with the popup. Per-cell scale/tempo mutations stay on the
    // async stderr backstop.
    if (!warp_render_preflight(base_warp_markers,
                               app.phaseresetmarkers.markers(),
                               /*live_store=*/true,
                               app.engine_settings.scale,
                               app.engine_settings.output_format,
                               app.trim.has_begin, app.trim.begin_frame,
                               app.trim.has_end, app.trim.end_frame)) {
        return false;
    }

    int owner_idx = -1;
    for (int i = 0; i < static_cast<int>(base_warp_markers.size()); ++i) {
        if (base_warp_markers[i].bpm_owner) {
            owner_idx = i;
            break;
        }
    }
    if (owner_idx < 0) return false;
    const GuiWarpMarker& owner = base_warp_markers[owner_idx];
    if (owner.bpm_beats <= 0)   return false;
    if (!(owner.bpm_lo > 0.0))  return false;
    if (!(owner.bpm_hi > 0.0))  return false;

    // Span endpoint is explicit (set on the `m` two-marker span gate).
    const int endpoint_idx = owner.bpm_endpoint;
    if (endpoint_idx <= owner_idx ||
        endpoint_idx >= static_cast<int>(base_warp_markers.size())) {
        return false;   // missing or malformed span: no sweep
    }
    // The span duration is a musical (seconds-domain) quantity — the BPM
    // math needs beats per minute — so this is a genuine display/physics
    // conversion, not a persistence one: frames / sample_rate.
    const double duration_seconds =
        (base_warp_markers[endpoint_idx].time_frame - owner.time_frame) /
        static_cast<double>(audio.sample_rate());
    if (!(duration_seconds > 0.0)) return false;

    // One cell per whole-bpm step from lo up to hi inclusive; fractional
    // lo keeps its fraction across the walk (72.5, 73.5, ...).
    std::vector<double> bpm_values;
    for (double b = owner.bpm_lo; b <= owner.bpm_hi; b += 1.0) {
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
        app.phaseresetmarkers.markers();

    std::vector<RenderRequest> reqs;
    reqs.reserve(bpm_values.size());
    int seq = 1;
    for (double bpm : bpm_values) {
        const auto computed = compute_base_tempo_scale(
            duration_seconds, owner.bpm_beats, bpm);
        if (!computed) {
            std::fprintf(stderr,
                "warptempo_gui: render-bpm: rejected cell "
                "bpm=%s (duration=%.6f, beats=%d)\n",
                format_value_double(bpm, 0).c_str(),
                duration_seconds, owner.bpm_beats);
            continue;
        }

        std::vector<GuiWarpMarker> cell_warp_markers = base_warp_markers;
        // Owner: concrete computed base tempo, scale carried in settings.
        // Any positive base tempo serializes exactly (padded shortest
        // round-trip form), so no range gate protects the save/reload
        // round trip; compute_base_tempo_scale's positivity guards are the
        // only cell filter.
        cell_warp_markers[owner_idx].tempo_inherits = false;
        cell_warp_markers[owner_idx].tempo_base     = computed->base_tempo;
        cell_warp_markers[owner_idx].tempo_scale.reset();
        // Span-internal markers pass: their own tempo is subsumed by the
        // owner's span tempo. Disabled span-internal markers stay disabled
        // but also pass (the disabled flag is independent of tempo_inherits).
        for (int i = owner_idx + 1; i < endpoint_idx; ++i) {
            cell_warp_markers[i].tempo_inherits = true;
            cell_warp_markers[i].tempo_base     = 1.0;   // inert default
            cell_warp_markers[i].tempo_scale.reset();    // inert: no typed scale
            // label_def on a span-internal marker is preserved (refs are
            // excluded from spans by the `m` two-marker span gate, but a def
            // may exist); only the tempo fields are rewritten. Do not touch
            // label_def, disabled, or any non-tempo field.
        }
        // endpoint marker: untouched — its section lies outside the span.

        EngineSettings cell_settings = app.engine_settings;
        cell_settings.scale = computed->scale;
        // Provenance descriptor for this cell's .rendersettings; promoted
        // verbatim into .settings on Ctrl+Alt+C commit.
        cell_settings.bpm =
            format_bpm_descriptor(
                owner.bpm_beats, bpm,
                owner.time_frame /
                    static_cast<double>(audio.sample_rate()),
                base_warp_markers[endpoint_idx].time_frame /
                    static_cast<double>(audio.sample_rate()));

        char num_buf[16];
        std::snprintf(num_buf, sizeof(num_buf),
                      "%0*d", pad_width, seq);
        // Filename embeds the exact cell values in padded shortest
        // round-trip form (bpm plain, tempo min 2 decimals, scale min 4),
        // so the name never rounds away stored precision.
        std::string basename = num_buf;
        basename += '_';
        basename += format_value_double(bpm, 0);
        basename += ',';
        basename += format_value_double(computed->base_tempo, 2);
        basename += ',';
        basename += format_value_double(computed->scale, 4);

        RenderRequest req = build_render_request(
            app.source_audio_path, std::move(cell_warp_markers), base_phase_resets,
            std::move(cell_settings),
            app.trim.has_begin, app.trim.begin_frame,
            app.trim.has_end,   app.trim.end_frame,
        batch_folder.string(), std::move(basename));
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);
        reqs.push_back(std::move(req));
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
