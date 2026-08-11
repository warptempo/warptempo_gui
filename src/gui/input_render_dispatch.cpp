#include "input_handler.h"

#include "phaseresetmarkers.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "time_format.h"
#include "value_format.h"
#include "warp_frame_map_view.h"
#include "warpmarkers.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// GuiInputHandler render-dispatch / batch-lifecycle methods
// (snapshot_current_authoring_state, finalize_render_run,
// start_render_batch, dispatch_next_batch_entry, on_batch_entry_complete,
// render_bpm_sweep), grouped here to keep input_handler.cpp focused on the
// event entry points.

GuiInputHandler::RendersBatchScan
GuiInputHandler::max_renders_batch_index(
        const std::filesystem::path& renders_dir) {
    RendersBatchScan scan;
    std::error_code ec;
    if (!std::filesystem::is_directory(renders_dir, ec)) return scan;
    for (const auto& de :
         std::filesystem::directory_iterator(renders_dir, ec)) {
        if (!de.is_directory()) continue;
        const std::string name = de.path().filename().string();
        int v = 0;
        size_t i = 0;
        while (i < name.size() && name[i] >= '0' && name[i] <= '9') {
            v = v * 10 + (name[i] - '0');
            ++i;
        }
        if (i == 0 || i >= name.size() || name[i] != '_') continue;
        if (v > scan.max_index) {
            scan.max_index             = v;
            scan.max_index_folder_name = name;
        }
    }
    return scan;
}

AuthoringSnapshot GuiInputHandler::snapshot_current_authoring_state() const {
    AuthoringSnapshot s;
    s.active_tab        = app.active_tab_view;
    s.trim_begin_frame    = app.trim.begin_frame;
    s.trim_end_frame      = app.trim.end_frame;
    // Session prefs the per-entry .settings writer needs, captured live at
    // dispatch so the file carries the session's real values.
    s.active_markers_view = app.active_markers_view;
    s.playback_speed      = app.playback_speed;
    s.follow              = app.follow_mode;
    s.gui_scale           = app.gui_scale;
    s.audio_player        = app.audio_player;
    s.projects_repo       = app.projects_repo;

    // Browse position, captured on the TARGET axis (the entry's .settings is
    // an active_audio_view=T state). Zoom rides through unchanged; the
    // playhead and viewport express where the user was AT THIS DISPATCH.
    s.view_zoom_level = app.zoom_level;
    if (app.active_audio_view == 'T') {
        // Already target-axis: take the live values verbatim. A SWEEP CELL
        // REACHES THIS ARM TOO since 2026-08-07 (the iteration sweep dispatches
        // from either audio view now that iteration mode is target-legal), and
        // it diverges here exactly as it does in the source arm below — the
        // cell's own per-cell markers describe a different map than the live
        // one these values are expressed in — with the same answer: the wav-arm
        // writer clamp brings an out-of-domain browse position back in.
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
        // in-domain. If the live map cannot build (tripwire-class only),
        // fall back to the untranslated live values: the writer clamp keeps
        // them in-domain, and such a dispatch would surface the worker's own
        // resolve->build stderr refusal.
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
            const double cur_spp = samples_per_pixel_at(
                app.zoom_level, audio.sample_rate());
            const double ph_px = (cur_spp > 0.0)
                ? (static_cast<double>(app.playhead_cursor_sample -
                                       app.viewport_start_sample) / cur_spp)
                : 0.0;
            const double new_spp = samples_per_pixel_at(
                app.zoom_level, audio.sample_rate());
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
    req.source_load_size  = audio.source_load_size();
    req.source_load_mtime = audio.source_load_mtime();
}

bool GuiInputHandler::allocate_miscellaneous_cell(std::string& out_folder,
                                                  std::string& out_basename) {
    // The source path is process-immutable, so deriving renders/ here equals
    // deriving it at command time.
    std::filesystem::path src(app.source_audio_path);
    std::filesystem::path src_parent = src.parent_path();
    if (src_parent.empty()) src_parent = std::filesystem::path(".");
    const std::filesystem::path queue_root = src_parent / "renders";

    // Append into the most-recent misc folder, else start a new one at the
    // shared first-index convention (max_index + 1 == 1 when empty).
    const RendersBatchScan scan = max_renders_batch_index(queue_root);
    std::filesystem::path target_folder;
    if (!scan.max_index_folder_name.empty() &&
        scan.max_index_folder_name.ends_with("_miscellaneous")) {
        target_folder = queue_root / scan.max_index_folder_name;
    } else {
        target_folder = queue_root /
            (std::to_string(scan.max_index + 1) + "_miscellaneous");
    }

    std::error_code ec;
    std::filesystem::create_directories(target_folder, ec);
    if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: render-miscellaneous: Could not create "
            "'%s': %s\n",
            target_folder.string().c_str(), ec.message().c_str());
        return false;
    }

    // Next cell index inside the target folder: max+1 over `<digits>.wav`,
    // starting at 1. Non-numeric wav names are ignored (misc cells are
    // authored only here, always bare-integer names).
    int max_cell = 0;
    for (const auto& fe :
         std::filesystem::directory_iterator(target_folder, ec)) {
        if (!fe.is_regular_file()) continue;
        if (fe.path().extension() != ".wav") continue;
        const std::string stem = fe.path().stem().string();
        if (stem.empty()) continue;
        bool all_digits = true;
        int  v          = 0;
        for (char c : stem) {
            if (c < '0' || c > '9') { all_digits = false; break; }
            v = v * 10 + (c - '0');
        }
        if (!all_digits) continue;
        if (v > max_cell) max_cell = v;
    }

    out_folder   = target_folder.string();
    out_basename = std::to_string(max_cell + 1);
    return true;
}

void GuiInputHandler::finalize_render_run() {
    app.queue_running          = false;
    app.queue_cancel_requested = false;
    // Invalidate the status lane before clearing queue_progress_text.
    // timestamp_invalidate_rect() covers the status lane — the whole bottom
    // strip until row 8 stacked the transport row above it (2026-08-11) — and
    // the label lives nowhere else; keep this
    // ordering consistent with the other status-clear paths.
    viewport.invalidate_timestamp_area();
    app.queue_progress_text.clear();
    // Drop the deferred message and disarm the signal. THE PARKED STRING MUST
    // DIE HERE or a rung-served run — which never promoted, so this clear of the
    // slot has nothing of its to erase — would leave its message waiting for the
    // NEXT dispatch's signal to show it. Every terminal branch of an archival
    // session reaches this one function (the single render's on_done and the
    // batch's out-of-entries and cancelled terminal), so this is the whole
    // cleanup. The signal reset is belt-and-braces — each dispatch resets it
    // too — and keeps the resting state honest between runs; it is safe here
    // because a completion runs on the GUI thread after do_render returned.
    pending_status_text_.clear();
    synthesis_started_.store(false);
    status_promoted_ = false;
    // Worker is now idle — pump the deferred work. maybe_dispatch_pending
    // offers the beat to a parked archival command first (an explicit user
    // command outranks the derived preview), then to a pending target
    // render queued by a target-view edit during this run.
    target_render.maybe_dispatch_pending();
}

void GuiInputHandler::park_render_status(std::string text) {
    // THE ONE OWNER of "an archival entry is about to be dispatched": it retires
    // the outgoing entry's status and arms the incoming one's.
    //
    // THE RETRACTION IS THE HALF THAT IS NOT OBVIOUS. Within a sweep each cell
    // parks its own message, and a cell that SYNTHESIZED had its message
    // promoted into the slot; if the next cell is then served by a reuse rung it
    // promotes nothing, and without this the previous cell's "Rendering 3 of 8"
    // would sit on the strip through however many byte-copy cells follow —
    // naming an entry that finished and a count that is no longer true. Clearing
    // only what WE promoted is what keeps this from reaching across owners: a
    // preview's "Updating..." in the shared slot is not ours to erase.
    if (status_promoted_) {
        // Invalidate before clearing, the ordering every status-clear path here
        // keeps.
        viewport.invalidate_timestamp_area();
        app.queue_progress_text.clear();
        status_promoted_ = false;
    }
    // BEFORE THE DISPATCH BY CONSTRUCTION (both callers park, then dispatch):
    // this reset is what stops the PREVIOUS session's fired signal from
    // promoting the incoming message instantly, which is exactly what a reuse
    // cell following a synthesis cell would otherwise do.
    synthesis_started_.store(false);
    pending_status_text_ = std::move(text);
}

// THE RENDER BUTTON'S CANCEL FACE, maintained per tick (architect 2026-08-11;
// the contract, the readers and the one-tick-staleness argument live at
// AppState::render_cancel_face). A TRANSITION WRITER on the drift-comparator
// pattern: the live fact is the cancel act's own predicate — the exact two
// branches cancel_archival_session tests — and each flip pays one
// invalidate_top_strip, which is what repaints Render's label, icon and hint
// on the edges no other damage covers (a render finishing is an async event
// with no top-strip damage of its own).
void GuiInputHandler::tick_render_cancel_face() {
    const bool now = async_renderer.is_busy() || app.queue_running;
    if (now == app.render_cancel_face) return;
    app.render_cancel_face = now;
    viewport.invalidate_top_strip();
}

void GuiInputHandler::tick_promote_render_status() {
    // THE TICK IS THE OBSERVER because the event being watched happens on the
    // WORKER thread: do_render stores the signal as it crosses into synthesis,
    // and nothing on the GUI thread is woken by that — the completion eventfd
    // fires at the END of the render, far too late to be this message's cue.
    // Polling a flag once per tick is the whole mechanism, and the ~8 ms it can
    // cost (the timerfd's free-running interval) is invisible against a render.
    // Same idiom, and the same wiring site, as the preview label's run hold
    // (GuiTargetRender::tick_updating_hold).
    //
    // ORDERED CHEAPEST FIRST: nothing parked is the resting state.
    if (pending_status_text_.empty())  return;
    // The worker went idle without ever crossing the boundary — a rung-served
    // render whose finalize is about to clear the parked string, or has already.
    // Promoting now would paint a message for work that is over.
    if (!async_renderer.is_busy())     return;
    if (!synthesis_started_.load())    return;
    // LIVENESS, AND THE SLOT BELONGS TO THE LIVE SESSION'S OWNER. A kill lands
    // on the GUI thread while the worker is somewhere inside do_render, so a
    // session cancelled just below its last reuse/cancel check still crosses the
    // synthesis boundary and fires the signal: without this, a preview edit that
    // killed a running archival render — stamping its own "Updating..." into the
    // shared slot as it went (GuiTargetRender::trigger's busy branch) — would
    // then watch the dead archival's "Rendering..." land on top of it, and a slow
    // cancellation would keep that false label up until the pending preview was
    // pumped. A killed session can never publish Success, so its message names
    // work whose product is discarded either way.
    //
    // THE TEST IS LIVENESS, NOT OWNERSHIP OF THE SLOT: the symmetric case is
    // legitimate and must keep working — an archival command that preempts a
    // running preview parks AFTER the kill, on a session dispatched fresh, and
    // its promotion rightly replaces the stale "Updating..." the run hold left
    // standing. The park's session is unambiguous because both park sites park
    // and then dispatch in the same GUI-thread call, and no dispatch can happen
    // while the worker is busy — so the token this reads is always the parked
    // message's own. Refusing in place rather than clearing keeps ONE owner for
    // the park's death (finalize_render_run, which every terminal branch
    // reaches); the check simply keeps answering no until then.
    if (async_renderer.current_session_cancelled()) return;

    app.queue_progress_text = pending_status_text_;
    // Explicitly, rather than by moving out: one promotion per parked message is
    // the contract, and the emptiness above is the test that enforces it.
    pending_status_text_.clear();
    // Records that the text now in the shared slot is OURS, which is what lets
    // the next park retract it and keeps that retraction from touching another
    // owner's message.
    status_promoted_ = true;
    viewport.invalidate_timestamp_area();
}

void GuiInputHandler::maybe_reestablish_target_buffer() {
    if (app.active_audio_view == 'T' &&
        !target_render.is_updating() && !async_renderer.is_busy() &&
        (target_render.is_dirty() || app.target_buffer_frames <= 0)) {
        // Re-establish the target buffer only when it is actually
        // stale or empty/cold. An archival render never touches
        // target_buffer, so a clean, bound buffer needs no
        // re-establishment: calling ensure_ready() there would trip
        // its defensive stop and cut an in-progress target audition
        // for a fully redundant rebind of the same buffer at the same
        // anchor. When re-establishment IS wanted, ensure_ready() may
        // fill from the shared cache when the just-rendered
        // fingerprint is already registered, or render the current
        // target state if the state changed or the freshly rendered
        // entry is still registering on the writer thread. That miss
        // is benign; if finalize_render_run just launched a pending
        // target render, is_updating() is true and we leave it alone.
        // The busy gate closes the parked-command race: finalize's pump
        // runs first and offers the beat to a parked archival command,
        // which sets neither pending_ nor in_flight_, so is_updating()
        // cannot see it. Without the busy check ensure_ready's trigger
        // would kill that explicit command in favor of this derived
        // preview, inverting the priority the pump just enforced. The
        // buffer re-establishment happens through the launched session's
        // own completion path (an archival completion re-runs this helper;
        // a preview completion rebinds itself).
        target_render.ensure_ready();
    }
}

void GuiInputHandler::dispatch_single_archival_render(RenderRequest req) {
    app.queue_cancel_requested = false;
    app.queue_running          = true;
    // THE MESSAGE IS PARKED, NOT SHOWN (architect 2026-08-08). do_render's three
    // reuse rungs are ahead of all engine work by design, so a dispatch says
    // nothing about whether anything will be rendered: an up-to-date artifact, a
    // project-artifact byte copy or a render-cache publish all return before the
    // engine is touched. "Rendering..." means synthesis is happening — not that
    // a command was issued — so it waits until the worker reports crossing that
    // boundary, and a rung-served render never shows it at all.
    park_render_status("Rendering...");
    req.synthesis_started = &synthesis_started_;
    async_renderer.dispatch(std::move(req),
        [this](RenderOutcome o) {
            const bool success = (o == RenderOutcome::Success);
            if (o == RenderOutcome::Cancelled) {
                std::fprintf(stderr, "warptempo_gui: Render cancelled\n");
            }
            finalize_render_run();
            // On success, re-establish a cold/stale target buffer (see
            // maybe_reestablish_target_buffer for the full rationale).
            if (success) maybe_reestablish_target_buffer();
        });
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
        if (cmd.miscellaneous) {
            // Late-bind the output cell now that the worker is idle — the
            // killed render can no longer be mid-publication, so this scan is
            // exact (see allocate_miscellaneous_cell). A creation failure
            // drops the command (stderr already printed) and reports false so
            // the caller's own pending target preview still gets the idle beat.
            std::string folder, basename;
            if (!allocate_miscellaneous_cell(folder, basename)) return false;
            cmd.reqs.front().batch_folder   = std::move(folder);
            cmd.reqs.front().batch_basename = std::move(basename);
        }
        dispatch_single_archival_render(std::move(cmd.reqs.front()));
    } else {
        start_render_batch(std::move(cmd.reqs), std::move(cmd.batch_label));
    }
    return true;
}

void GuiInputHandler::start_render_batch(std::vector<RenderRequest> reqs,
                                         std::string batch_label) {
    if (reqs.empty()) return;

    batch_.reqs       = std::move(reqs);
    batch_.label      = std::move(batch_label);
    batch_.next_index = 0;
    batch_.rendered   = 0;
    batch_.active     = true;

    app.queue_cancel_requested = false;
    app.queue_running          = true;

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
                "warptempo_gui: %s: Rendered %d of %d entries (cancelled)\n",
                batch_.label.c_str(), batch_.rendered, total);
        } else {
            std::fprintf(stderr,
                "warptempo_gui: %s: Rendered %d of %d entries\n",
                batch_.label.c_str(), batch_.rendered, total);
        }
        // A finished batch just leaves its artifacts on disk; nothing
        // auto-opens. The user presses `l` to listen or `'` to load an
        // entry in place by name.
        batch_.active = false;
        batch_.reqs.clear();
        batch_.reqs.shrink_to_fit();
        finalize_render_run();
        // The parked-batch route suppresses the first render's success tail
        // via the busy gate, so the batch's own terminal must re-establish a
        // cold buffer, exactly like a single archival success. A cancelled
        // batch stays symmetric with the single Cancelled outcome and never
        // re-establishes (Esc means the user took control; any edit re-previews).
        if (!cancelled) maybe_reestablish_target_buffer();
        return;
    }

    // THE BATCH LABEL RIDES IN A TRAILING PARENTHETICAL so the GUI sentence
    // leads with a capital of its own ("Rendering 1 of 5 (BPM)...") whatever
    // the label's case is. The label is one string shared with the stderr
    // summary above, and the two labels take the case rule differently:
    // "BPM" is CAPITALIZED ALWAYS (architect 2026-08-02 — the acronym caps in
    // both surfaces, which deliberately released the 2026-08-01 GUI/stderr
    // byte-identity hold for that one token), while "render iterations" stays
    // lowercase because it is a shared ROUTING/CATEGORY LABEL rather than
    // sentence-initial prose in either surface: in the GUI it sits inside this
    // parenthetical, and in the summary it fills the tag slot ahead of the
    // message proper, whose own first word takes the capital
    // ("warptempo_gui: render iterations: Rendered 3 of 8 entries"). Its
    // position after the "warptempo_gui: " prefix is NOT the reason — the
    // 2026-08-02 terminal pass looks past the program-name prefix when it
    // locates that first prose word. A future label that DOES lead a sentence
    // capitalizes at its definition, not here.
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "Rendering %d of %d (%s)...",
                  batch_.next_index + 1, total,
                  batch_.label.c_str());
    // PARKED, NOT SHOWN — same rule as the single render (rationale at
    // dispatch_single_archival_render), and this is where it earns most: a
    // sweep's cells are individually rung-served or synthesized, so the row
    // counts up only across the cells that actually rendered something and a run
    // of reuse cells passes in silence. The park is also what RETRACTS the
    // previous cell's message when that cell did show one — see the owner.
    park_render_status(buf);

    RenderRequest req = std::move(batch_.reqs[batch_.next_index]);
    req.synthesis_started = &synthesis_started_;
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

// Human-readable provenance descriptor for a BPM cell,
// e.g. "36 beats @ 220 bpm from 00:32.008 to 00:46.562". Beats is an
// integer; bpm is a double printed in plain shortest round-trip form
// ("220" stays "220", "220.5" stays "220.5"); the two timestamps are the
// span's owner time and its END (the boundary marker's time, or the song
// end when the span reaches the store-final marker's section) in display
// seconds (frame / sample_rate, converted by the caller), formatted via the shared
// mm:ss.mmm formatter. Stored verbatim in the cell's per-entry .settings
// bpm= field and promoted into the source .settings on the cell's
// load-in-place.
// DATA, NOT DISPLAY: no paint site reads this string — it is a sidecar value
// that also feeds the render fingerprint (render_cache.cpp's EngineField::Bpm
// arm), so its " bpm " stays lowercase where the display label capitalizes.
// Capitalizing it would move sidecar bytes and mint fresh cache keys.
static std::string format_bpm_descriptor(int beats, double bpm,
                                         double start_seconds,
                                         double end_seconds) {
    return std::to_string(beats) + " beats @ " +
           format_value_double(bpm, 0) + " bpm from " +
           format_timestamp(start_seconds) + " to " +
           format_timestamp(end_seconds);
}

// Sweep every BPM in the BPM owner's [bpm_lo, bpm_hi] range, computing
// (base_tempo, scale) per cell and rendering one `.wav` per cell into
// `<source_parent>/renders/<N>_bpm/`. The per-cell engine
// values land in the per-entry `.settings` sidecar's engine block (written
// by do_render); the `'` load-in-place (load_render_entry_in_place) applies
// them when loading a BPM cell in place. The
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

    // Dispatch validates nothing: the render worker's own resolve->build
    // chain (resolve_warp_markers_for_render, which normalizes ambiguous
    // marker arrangements to tempo 1.00, then build_warp_frame_map) is the
    // tripwire surface, and its per-cell scale/tempo mutations stay on the
    // async stderr backstop. Trim never refuses (crossed/equal bounds cannot
    // rest; an ambiguous trim renders untrimmed).
    // This scans for the session bpm-mode owner FLAG (bpm_owner): the index of the
    // marker the sweep rewrites, in the base store's own coordinates. Purely local
    // to this function.
    int bpm_owner_idx = -1;
    for (int i = 0; i < static_cast<int>(base_warp_markers.size()); ++i) {
        if (base_warp_markers[i].bpm_owner) {
            bpm_owner_idx = i;
            break;
        }
    }
    if (bpm_owner_idx < 0) return false;
    const GuiWarpMarker& owner = base_warp_markers[bpm_owner_idx];
    if (owner.bpm_beats <= 0)   return false;
    if (!(owner.bpm_lo > 0.0))  return false;
    if (!(owner.bpm_hi > 0.0))  return false;

    // Span endpoint is explicit (set on the `m` section gate). It is one past
    // the last selected marker: endpoint_idx == store size is the SONG-END
    // sentinel (the last selected marker is store-final, its section runs to
    // total_frames), endpoint_idx < size means the marker there is the closing
    // boundary. A value <= owner or past the size is missing/malformed.
    const int store_size = static_cast<int>(base_warp_markers.size());
    const int endpoint_idx = owner.bpm_endpoint;
    if (endpoint_idx <= bpm_owner_idx || endpoint_idx > store_size) {
        return false;   // missing or malformed span: no sweep
    }
    // Span end frame: the boundary marker's time when one exists, else the
    // song end. Named once here and reused for the duration and the
    // descriptor's endpoint seconds.
    const int64_t span_end_frame =
        (endpoint_idx < store_size)
            ? base_warp_markers[endpoint_idx].time_frame
            : audio.total_frames();
    // The span duration is a musical (seconds-domain) quantity — the BPM
    // math needs beats per minute — so this is a genuine display/physics
    // conversion, not a persistence one: frames / sample_rate.
    const double duration_seconds =
        (span_end_frame - owner.time_frame) /
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

    std::error_code ec;
    const int next_index = max_renders_batch_index(queue_root).max_index + 1;

    // FILENAME TOKEN, therefore DATA and lowercase: it names the on-disk
    // batch folder `<N>_bpm/`, which the `l` listen and `'` load-in-place
    // routes reach by name. The display label below is the separate, capitalized
    // "BPM" — the case ruling reaches every PROSE surface, the terminal
    // included since the 2026-08-02 pass, so the split here is prose versus
    // DATA rather than display versus stderr.
    const std::string command_tag = "bpm";
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
                "warptempo_gui: render-bpm: Rejected cell "
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
        cell_warp_markers[bpm_owner_idx].tempo_inherits = false;
        cell_warp_markers[bpm_owner_idx].tempo_cents    = computed->base_tempo_cents;
        cell_warp_markers[bpm_owner_idx].tempo_scale.reset();
        // Span-internal markers pass: their own tempo is subsumed by the
        // owner's span tempo. Disabled span-internal markers stay disabled
        // but also pass (the disabled flag is independent of tempo_inherits).
        for (int i = bpm_owner_idx + 1; i < endpoint_idx; ++i) {
            cell_warp_markers[i].tempo_inherits = true;
            cell_warp_markers[i].tempo_cents    = 100;   // inert default
            cell_warp_markers[i].tempo_scale.reset();    // inert: no typed scale
            // label_def on a span-internal marker is preserved (refs are
            // excluded from spans by the `m` section gate's ref scan, but a
            // def may exist); only the tempo fields are rewritten. Do not
            // touch label_def, disabled, or any non-tempo field.
        }
        // Boundary marker (when one exists): untouched — it owns the
        // FOLLOWING section, which lies outside the span. At song end
        // (endpoint_idx == store size) there is no boundary marker and the
        // loop above already ran to the store end, so every following marker
        // passes.

        EngineSettings cell_settings = app.engine_settings;
        cell_settings.scale = computed->scale;
        // Provenance descriptor for this cell's per-entry .settings; promoted
        // verbatim into the source .settings on the `'` load-in-place
        // (load_render_entry_in_place).
        cell_settings.bpm =
            format_bpm_descriptor(
                owner.bpm_beats, bpm,
                owner.time_frame /
                    static_cast<double>(audio.sample_rate()),
                span_end_frame /
                    static_cast<double>(audio.sample_rate()));

        char num_buf[16];
        std::snprintf(num_buf, sizeof(num_buf),
                      "%0*d", pad_width, seq);
        // Filename embeds the exact cell values (bpm plain shortest, tempo
        // straight from cents via format_tempo_cents, scale min-4 padded
        // shortest), so the name never rounds away stored precision.
        std::string basename = num_buf;
        basename += '_';
        basename += format_value_double(bpm, 0);
        basename += ',';
        basename += format_tempo_cents(computed->base_tempo_cents);
        basename += ',';
        basename += format_value_double(computed->scale, 4);

        RenderRequest req = build_render_request(
            app.source_audio_path, std::move(cell_warp_markers), base_phase_resets,
            std::move(cell_settings),
            app.trim.begin_frame, app.trim.end_frame,
        batch_folder.string(), std::move(basename));
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);
        reqs.push_back(std::move(req));
        ++seq;
    }

    if (reqs.empty()) {
        std::fprintf(stderr,
            "warptempo_gui: render-bpm: No valid cells; "
            "nothing to render\n");
        return false;
    }

    std::filesystem::create_directories(batch_folder, ec);
    if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: render-bpm: Could not create "
            "'%s': %s\n",
            batch_folder.string().c_str(), ec.message().c_str());
        return false;
    }

    // The batch's DISPLAY label — the progress parenthetical and the stderr
    // summary, both fed from this one string. "BPM" is capitalized always
    // (architect 2026-08-02): it is an acronym, not a sentence position, so
    // both surfaces carry the caps. It is NOT the folder token above.
    if (async_renderer.is_busy()) {
        // A render dispatch kills the running render. Park the fully built
        // batch for the worker-idle pump.
        AppState::PendingArchivalCommand cmd;
        cmd.reqs        = std::move(reqs);
        cmd.batch_label = "BPM";
        kill_running_render_and_park(std::move(cmd));
    } else {
        start_render_batch(std::move(reqs), "BPM");
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
