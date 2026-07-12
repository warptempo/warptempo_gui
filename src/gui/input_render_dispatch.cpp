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

// GuiInputHandler render-dispatch / batch-lifecycle methods
// (snapshot_current_authoring_state, snapshot_current_queued_render,
// finalize_render_run, start_render_batch, dispatch_next_batch_entry,
// on_batch_entry_complete, render_bpm_sweep), grouped here to keep
// input_handler.cpp focused on the event entry points.

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
        bool has_trim_begin, int64_t trim_begin_frame,
        bool has_trim_end, int64_t trim_end_frame) {
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
            // Exact integer compare on the authored bounds — the bounds are
            // whole int64 source frames, matching validate_trim_frames' own
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
    s.active_tab        = app.active_tab_view;
    s.has_trim_begin    = app.trim.has_begin;
    s.trim_begin_frame    = app.trim.begin_frame;
    s.has_trim_end      = app.trim.has_end;
    s.trim_end_frame      = app.trim.end_frame;
    // Session prefs the per-entry .settings writer needs, captured live at
    // dispatch so the file carries the session's real values.
    s.active_markers_view = app.active_markers_view;
    s.playback_speed      = app.playback_speed;
    s.follow              = app.follow_mode;
    s.font_size           = app.font_size;

    // Browse position, captured on the TARGET axis (the entry's .settings is
    // an active_audio_view=T state). Zoom rides through unchanged; the
    // playhead and viewport express where the user was AT THIS DISPATCH.
    s.view_zoom_level = app.zoom_level;
    if (app.active_audio_view == 'T') {
        // Already target-axis: take the live values verbatim.
        s.view_viewport_start_frame = app.viewport_start_sample;
        s.view_playhead_frame       = app.playhead_cursor_sample;
    } else {
        // Source view: forward-map the playhead through the live target map
        // the same way handle_active_audio_view_toggle does its S-to-T
        // anchor, so the captured position is the target image of the
        // on-screen playhead with its screen column preserved. At dispatch
        // the live stores equal the request's stores for plain dispatches,
        // so the live map IS the entry's axis; a sweep cell rewrites markers
        // per cell and diverges, which the wav-arm writer clamp brings back
        // in-domain. If the live map cannot build (walkable-defect store),
        // fall back to the untranslated live values: the writer clamp keeps
        // them in-domain, and a dispatch from a defective store is already
        // refused upstream by preflight.
        const int64_t src_total = audio.total_frames();
        const TargetWarpFrameMapCache& c = target_view_warp_frame_map_cached(
            app, audio.sample_rate(),
            static_cast<long>(src_total));
        if (c.build_error.empty()) {
            // Forward-map the playhead, banker's-rounded, exactly as the
            // toggle does.
            const int64_t ph = app.playhead_cursor_sample < 0
                                   ? 0 : app.playhead_cursor_sample;
            const int64_t tph = static_cast<int64_t>(std::nearbyint(
                map_source_to_target(static_cast<double>(ph),
                                     c.warp_frame_map)));
            s.view_playhead_frame = tph;

            // Derive the viewport so the translated playhead keeps its
            // pre-flip screen column: ph_px is the playhead's column in the
            // source domain; the target-domain viewport start places the
            // translated playhead at that same column, at the unchanged zoom.
            const GuiRect area = waveform_area(app);
            const double cur_spp = samples_per_pixel_at(
                app.zoom_level, area.w, src_total, audio.sample_rate());
            const double ph_px = (cur_spp > 0.0)
                ? (static_cast<double>(app.playhead_cursor_sample -
                                       app.viewport_start_sample) / cur_spp)
                : 0.0;
            const double new_spp = samples_per_pixel_at(
                app.zoom_level, area.w, c.tgt_total_frames,
                audio.sample_rate());
            const double new_vp_d =
                static_cast<double>(tph) - ph_px * new_spp;
            s.view_viewport_start_frame =
                static_cast<int64_t>(std::nearbyint(new_vp_d));
        } else {
            s.view_viewport_start_frame = app.viewport_start_sample;
            s.view_playhead_frame       = app.playhead_cursor_sample;
        }
    }
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
    // Worker is now idle — pump the deferred work. maybe_dispatch_pending
    // offers the beat to a parked archival command first (an explicit user
    // command outranks the derived preview), then to a pending target
    // render queued by a target-view edit during this run.
    target_render.maybe_dispatch_pending();
}

void GuiInputHandler::dispatch_single_archival_render(
        RenderRequest req, std::vector<uint8_t> fingerprint) {
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
                !app.render_view.enabled &&
                !target_render.is_updating()) {
                // ensure_ready() may fill from the shared cache when the
                // just-rendered fingerprint is already registered, or
                // render the current target state if the state changed or
                // a longer-than-RAM-tier entry is still registering on the
                // writer thread. That miss is benign; if finalize_render_run
                // just launched a pending target render, leave it alone.
                //
                // Gated on !render_view.enabled: with render view open the
                // flag can be 'T' while the displayed audio is a render wav
                // (R does not touch active_audio_view), and ensure_ready
                // would rebind playback to target_buffer (clean path) or
                // trigger a preview against the displayed render (dirty
                // path) beneath the open view. Under render view the
                // T-entry funnel re-runs at the next real T entry
                // (restore_source_view's own ensure_ready).
                target_render.ensure_ready();
            }
        });
    // dispatch() cleared the session fingerprint; re-arm it for this
    // single-render session so an identical re-dispatch no-ops and a
    // fingerprint-matching target-preview trigger waits the render out.
    async_renderer.set_session_fingerprint(std::move(fingerprint));
}

void GuiInputHandler::kill_running_render_and_park(
        AppState::PendingArchivalCommand cmd) {
    async_renderer.request_cancel();
    app.queue_cancel_requested = true;
    cmd.armed = true;
    app.pending_archival = std::move(cmd);
}

bool GuiInputHandler::dispatch_pending_archival_command() {
    if (!app.pending_archival.armed) return false;
    if (async_renderer.is_busy())    return false;
    AppState::PendingArchivalCommand cmd = std::move(app.pending_archival);
    app.pending_archival = {};
    // Every park site fills reqs; an empty slot dispatches nothing and
    // must report false so the caller's own pending work still runs.
    if (cmd.reqs.empty()) return false;
    if (cmd.single) {
        dispatch_single_archival_render(std::move(cmd.reqs.front()),
                                        std::move(cmd.fingerprint));
    } else {
        start_render_batch(std::move(cmd.reqs), std::move(cmd.batch_label));
    }
    return true;
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
        // Show render-view at the just-rendered batch's first file. Gated
        // on a non-cancelled terminal branch that actually produced at
        // least one .wav on disk; a batch where every entry returned Failed
        // leaves rendered == 0 and there is nothing to view. Render view
        // opens only against an idle worker with nothing parked, so the
        // auto-open — an automatic `r` — obeys the same entry gate the r key
        // does. The completed batch is fully finalized — state cleared, then
        // finalize_render_run — BEFORE the auto-open runs, and finalize's
        // maybe_dispatch_pending can hand the worker to a parked archival
        // command or a pending target render on that same beat; when the
        // pump started new work the auto-open is skipped outright with the
        // gate's one refusal line — the batch is on disk and in the render
        // list for a later `r`, exactly like the modal-surface skip below.
        const bool success = !cancelled && batch_.rendered > 0;
        // Latch the folder the auto-open consumes before the state clear.
        // auto_open_batch_at_first_file reads only its batch_folder argument
        // (all other inputs are app.* fields), and the clear below drops
        // batch_.reqs and active while finalize_render_run pumps the worker.
        const std::filesystem::path completed_batch_folder =
            batch_.batch_folder;
        batch_.active = false;
        batch_.reqs.clear();
        batch_.reqs.shrink_to_fit();
        finalize_render_run();
        // Modal-surface guard (architect-ruled SKIP): a completion callback
        // must never mutate the view stack underneath a modal surface — a
        // bottom-strip editor or any prompt. Auto-opening render view here
        // would flip the displayed domain and rebind playback under an open
        // bpm/settings session, or re-enter the view under a defect series.
        // When a modal is up the
        // auto-open is skipped outright — the batch is on disk and in the
        // render list; `r` shows it once the modal closes. No deferral slot:
        // skip keeps the modal invariant absolute with no new lifecycle.
        if (success && !modal_bottom_strip_editor_active() &&
            !app.prompt.active) {
            // Entry gate, the same refusal the r key prints: render view
            // never opens over running or parked render work.
            if (app.queue_running || app.pending_archival.armed) {
                std::fprintf(stderr,
                    "warptempo_gui: render view refused: a render is running "
                    "(Esc cancels it, then r)\n");
            } else {
                render_view.auto_open_batch_at_first_file(
                    completed_batch_folder);
            }
        }
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
// mm:ss.mmm formatter. Stored verbatim in the cell's per-entry .settings
// bpm= field and promoted into the source .settings on commit.
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
// values land in the per-entry `.settings` sidecar's engine block (written
// by do_render); Ctrl+Alt+C adopts them when committing a BPM cell. The
// substantive difference from the iter render handler is per-cell
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
    // async stderr backstop. The bpm editor session is modal — nothing can
    // mutate the stores, settings, or trim between mode entry and this
    // dispatch, and the entry state rested through the commit-validation
    // funnel — so a store defect here is a backstop, not a reachable path.
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

    // One cell per whole-bpm step from lo up to hi inclusive, generated by
    // integer index (b = lo + i, a fresh product each iteration) so the walk
    // never accumulates float error. A fractional lo keeps its fraction
    // across the walk (72.5, 73.5, ...). parse_bpm_bracket (warpmarkers.h) —
    // the sole route into bpm_lo/bpm_hi, used by the flag editor commit path
    // — bounds both ends to the bpm bracket [kBpmMin, kBpmMax] with lo <= hi,
    // so the count is bounded by floor(kBpmMax - kBpmMin) + 1 = 391 and >= 1.
    const double cell_count = std::floor(owner.bpm_hi - owner.bpm_lo) + 1.0;

    std::vector<double> bpm_values;
    bpm_values.reserve(static_cast<size_t>(cell_count));
    for (double i = 0.0; i < cell_count; i += 1.0) {
        const double b = owner.bpm_lo + i;
        if (b <= owner.bpm_hi) {
            bpm_values.push_back(b);
        } else {
            break;
        }
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
        // compute_base_tempo_scale's positivity and bracket guards are the
        // cell filter; the BPM editor commit already checked the bracket
        // at both bracket ends (the derivation is monotone in bpm), so in
        // practice every cell derives in-bracket and serializes exactly
        // (padded shortest round-trip form).
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
        // Provenance descriptor for this cell's per-entry .settings;
        // promoted verbatim into the source .settings on Ctrl+Alt+C commit.
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

    if (async_renderer.is_busy()) {
        // A render dispatch kills the running render; a sweep never
        // matches a session fingerprint, so there is no wait case. Park
        // the fully built batch for the worker-idle pump.
        AppState::PendingArchivalCommand cmd;
        cmd.reqs        = std::move(reqs);
        cmd.batch_label = "bpm";
        kill_running_render_and_park(std::move(cmd));
    } else {
        start_render_batch(std::move(reqs), "bpm");
    }
    // Batch fully built and committed to run (dispatched, or parked behind
    // the killed render's drain): every request carries its own moved
    // marker snapshot and its cell_settings.bpm descriptor string, so nothing
    // downstream reads the live bpm marker state. Wipe it and close bpm mode
    // together (exit_bpm_mode is the chokepoint — it wipes the state and
    // repaints both strips). Wiping while the mode stayed on would leave an
    // ownerless bpm mode painting a blank bracket with no owner to re-edit;
    // the sweep is the end of the workflow, and re-sweeping re-selects a span
    // anyway. Every guard bail above returns false and leaves the bpm state
    // untouched here — the Enter dispatch (handle_top_flag_editor_key) then
    // exits the mode itself, since the editor already closed on commit and
    // bpm mode is exactly its editor session.
    flag_editor.exit_bpm_mode();
    return true;
}
