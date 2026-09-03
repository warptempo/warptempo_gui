#include "input_handler.h"

#include "directory_walk.h"     // the one non-throwing listing walk
#include "phaseresetmarkers.h"
#include "render_cache.h"          // fingerprint_sidecar_path
#include "render_output_naming.h"  // the deliverable's path composition
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
    // NON-THROWING (directory_walk.h): a batch root edited under the dispatch —
    // the trash road, an external sync, a folder unmounted — answers what it
    // saw, which is the same "highest index seen" this scan is, rather than
    // terminating the process out of a range-for's increment.
    for_each_directory_entry(renders_dir, ec, [&scan](
            const std::filesystem::directory_entry& de) {
        std::error_code entry_ec;
        if (!de.is_directory(entry_ec) || entry_ec) return;
        const std::string name = de.path().filename().string();
        int v = 0;
        size_t i = 0;
        while (i < name.size() && name[i] >= '0' && name[i] <= '9') {
            v = v * 10 + (name[i] - '0');
            ++i;
        }
        if (i == 0 || i >= name.size() || name[i] != '_') return;
        if (v > scan.max_index) {
            scan.max_index             = v;
            scan.max_index_folder_name = name;
        }
    });
    return scan;
}

// THE FAILED RENDER'S OWN REMOVAL (architect 2026-09-02) — one body, two
// subjects: the deliverable pair in `render/` and a batch cell's file set
// under its `tmp/` folder. Every path is COMPOSED from its own owner
// (compose_render_output_path, render_staging_path, fingerprint_sidecar_path,
// and the entry sidecar triple's extensions, spelled as the load road spells
// them) and never found by walking the folder: a failure removes what THIS
// render was to write and nothing that happens to sit beside it.
//
// A MISSING FILE IS THE ORDINARY CASE, not an error — most failures return
// before anything is written, and do_render unwinds a good deal of the rest
// itself — so a `remove` that finds nothing is silent. A removal that FAILS
// is one stderr line and nothing else: it is a defensive arm (the folder is
// the product's own, under the project the user just rendered from), and the
// card the caller raises has already said the render failed.
static void remove_failed_render_files(
        const std::vector<std::filesystem::path>& paths) {
    for (const std::filesystem::path& p : paths) {
        std::error_code ec;
        std::filesystem::remove(p, ec);
        if (ec) {
            std::fprintf(stderr,
                "warptempo_gui: Could not remove '%s': %s\n",
                p.string().c_str(), ec.message().c_str());
        }
    }
}

// A failed CELL's whole file set: the wav, its staging sibling, the
// fingerprint attestation, and the three load-in-place sidecars do_render
// writes beside a batch wav (render_pipeline.cpp's
// publish_load_in_place_critical_batch_sidecars is where they are authored;
// the load road composes the same triple, input_key_dispatch.cpp). Composed
// from the cell's folder and basename, which the dispatch captures before the
// request is moved onto the worker.
static void remove_failed_batch_cell(const std::string& batch_folder,
                                     const std::string& batch_basename) {
    if (batch_folder.empty() || batch_basename.empty()) return;
    const std::filesystem::path folder(batch_folder);
    const std::filesystem::path wav =
        compose_render_output_path(folder, batch_basename);
    remove_failed_render_files({
        wav,
        std::filesystem::path(render_staging_path(wav.string())),
        std::filesystem::path(fingerprint_sidecar_path(wav.string())),
        folder / (batch_basename + ".warpmarkers"),
        folder / (batch_basename + ".phaseresetmarkers"),
        folder / (batch_basename + ".settings"),
    });
}

AuthoringSnapshot GuiInputHandler::snapshot_current_authoring_state() const {
    AuthoringSnapshot s;
    s.active_tab        = app.active_tab_view;
    s.trim_begin_frame    = app.trim.begin_frame;
    s.trim_end_frame      = app.trim.end_frame;
    // Session prefs the per-entry .settings writer needs, captured live at
    // dispatch so the file carries the session's real values.
    s.active_markers_view = app.active_markers_view;
    s.follow              = app.follow_mode;
    s.centered            = app.centered_mode;
    s.waveform_magnification_level = app.waveform_magnification_level;

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
        // the same way switch_active_audio_view_to does its S-to-T
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
    // The source path is process-immutable, so deriving the batch root here
    // equals deriving it at command time.
    const std::filesystem::path queue_root =
        project_batch_root(app.source_audio_path);

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
        // THE CARD IS THIS BODY'S, not its callers' (architect 2026-08-30):
        // TWO roads allocate a cell — the Ctrl+Alt+Shift+R arm and the
        // worker-idle pump's late binding — and both refuse for exactly this
        // reason and add none of their own, so one card here is one card per
        // press on either. Both clauses are the three dispatchers' one
        // composer's (render_folder_creation_failure, renders_dir.h): the
        // stderr line keeps the whole path under this road's tag, the card
        // names the folder.
        const GuiFailure f = render_folder_creation_failure(
            "render-miscellaneous", target_folder, ec);
        std::fprintf(stderr, "warptempo_gui: %s\n", f.diagnostic.c_str());
        notifications.notify(AppState::NotificationClass::Normal, f.display);
        return false;
    }

    // Next cell index inside the target folder: max+1 over `<digits>.wav`,
    // starting at 1. Non-numeric wav names are ignored (misc cells are
    // authored only here, always bare-integer names).
    int max_cell = 0;
    for_each_directory_entry(target_folder, ec, [&max_cell](
            const std::filesystem::directory_entry& fe) {
        std::error_code entry_ec;
        if (!fe.is_regular_file(entry_ec) || entry_ec) return;
        if (fe.path().extension() != ".wav") return;
        const std::string stem = fe.path().stem().string();
        if (stem.empty()) return;
        bool all_digits = true;
        int  v          = 0;
        for (char c : stem) {
            if (c < '0' || c > '9') { all_digits = false; break; }
            v = v * 10 + (c - '0');
        }
        if (!all_digits) return;
        if (v > max_cell) max_cell = v;
    });

    out_folder   = target_folder.string();
    out_basename = std::to_string(max_cell + 1);
    return true;
}

void GuiInputHandler::finalize_render_run() {
    app.queue_running          = false;
    app.queue_cancel_requested = false;
    // Invalidate the state cell before clearing queue_progress_text.
    // invalidate_status_cell_area covers THE BOTTOM ROW'S LANE WHOLE — the
    // label's home since 2026-08-29's fold, the one-day status bar's that
    // morning and the tab row's from 2026-08-13 — and the label
    // lives nowhere else; keep this
    // ordering consistent with the other status-clear paths.
    viewport.invalidate_status_cell_area();
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
        viewport.invalidate_status_cell_area();
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
// the contract, the narrowing rationale and the honesty argument live at
// AppState::render_cancel_face). A TRANSITION WRITER on the drift-comparator
// pattern: the live fact is queue_running — the EXPLICIT acts' own bit, whose
// writers are exactly the two archival dispatchers, so the automatic preview
// updates never raise this face — and each flip pays one
// invalidate_top_strip, which is what repaints Render's label, icon and hint
// on the edges no other damage covers (a render finishing is an async event
// with no top-strip damage of its own).
void GuiInputHandler::tick_render_cancel_face() {
    const bool now = app.queue_running;
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
    viewport.invalidate_status_cell_area();
}

// THE FIELD REPORT OF 2026-08-13 AND WHAT IT ACTUALLY WAS, recorded here
// because this is the site the answer lives at. The architect reported that
// "Rendering..." and "Updating..." had stopped appearing since the 2026-08-12
// relayout, and the chain was moved into the tab row that day. THE MOVE FIXED
// NO DEFECT, and must not be credited with one: the whole path was re-traced
// against the shipped tree — the writers, this promotion, the damage owner's
// rect (a 774px live span at 1920x1080), the painter's own exposure gate, the
// degenerate-span guard and the frame-class gating — and every link was sound.
// Three facts explain the report, none of them a bug:
//   * THE RUNG SILENCE IS THE 2026-08-08 RULING ITSELF. do_render's three
//     reuse rungs all return ABOVE the synthesis_started store, so an archival
//     render served by an up-to-date artifact, a project-artifact byte copy or
//     a render-cache publish shows NOTHING — which is the point of parking the
//     message. Re-rendering an unchanged recipe is exactly that case.
//   * THE PREVIEW HAS THE SAME SHAPE: stamp_updating fires only on the
//     synthesis miss and on trigger()'s busy branch, so a preview resolved on
//     either synchronous rung (undo/redo A->B->A, an S->T entry with a current
//     fingerprint, an out-of-window phase-reset edit) is silent too.
//   * A SINGLE EDIT'S LABEL CAN LIVE BETWEEN TWO FRAMES. The stamp and the
//     completion clear are both GUI-thread and nothing forces a paint between
//     them; the run HOLD covers repeat runs only, by design (the timeline at
//     GuiTargetRender::stamp_updating states the ~40ms single-tap lifetime).
// What the relayout DID change is where a shown label lands: the chain went
// from the bottom-LEFT lead-in to the gutter between the clock and the arrows.
// The 2026-08-13 move to the tab row's top right addressed that and nothing
// else. THE LABEL'S HOME TODAY IS ROW 8'S STATE CELL, right of the clock
// (2026-08-29's evening fold, after that morning's one-day status bar;
// messaging.md) — the fourth surface it has had, and the three causes above
// are untouched by every one of the moves.

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
    // WHICH FOLDER THIS REQUEST PUBLISHES INTO, read before the move: an empty
    // batch_folder selects the deliverable naming convention inside do_render
    // (`render/<title>.wav`), a set one a batch cell under `tmp/`. It is the
    // whole of the prune's membership below — Ctrl+Alt+R's plain single render
    // is the deliverable arm, and Ctrl+Alt+Shift+R's miscellaneous cell, which
    // arrives here with its folder late-bound, is not.
    const bool publishes_deliverable = req.batch_folder.empty();
    // THE PAIR THIS REQUEST WOULD PUBLISH, composed from the REQUEST's own
    // settings and read before the move for the same reason the flag above is:
    // the failure arm below removes what this render was to write, and a title
    // edited in the settings editor while the render ran must not redirect that
    // removal onto a pair this render never touched. (The Success prune reads
    // the LIVE title instead, and deliberately: its definition is "the current
    // title's deliverable and nothing else" — see the prune's own note.)
    const std::filesystem::path deliverable_wav =
        publishes_deliverable
            ? compose_render_output_path(
                  render_output_directory(req.source_audio_path),
                  render_output_stem(req.engine_settings))
            : std::filesystem::path();
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
        [this, publishes_deliverable, deliverable_wav](
                RenderOutcome o, const GuiFailure& failure) {
            const bool success = (o == RenderOutcome::Success);
            if (o == RenderOutcome::Cancelled) {
                std::fprintf(stderr, "warptempo_gui: Render cancelled\n");
            }
            finalize_render_run();
            // THE DELIVERABLE'S PUBLISH PRUNES `render/` (architect
            // 2026-08-29): the folder holds the current title's deliverable
            // and nothing else, so a retitle's first render takes the previous
            // title's pair with it (the player showed one in its listing too,
            // until it moved inside `tmp/` on 2026-09-01). It runs
            // HERE, on the GUI thread after do_render returned, so the wav and
            // its .fingerprint are both on disk — do_render writes the
            // fingerprint last, as the attestation that the set is complete —
            // before anything else goes. The stem it keeps is the LIVE title,
            // not the request's: the definition is "the current title's" and
            // this prune is the ONLY place it lives (prune_render_folder,
            // renders_dir.h — THIS IS ITS ONE TRIGGER since the player stopped
            // listing `render/` on 2026-09-01). The Synchronize mirror does
            // not restate it: since 2026-09-02 it LISTS `render/` and ships
            // whatever is there, so the stick follows this prune rather than
            // agreeing with it by a rule of its own (external_sync.h rule 1).
            // A title edited in the
            // settings editor while this render ran leaves what it published
            // for the next deliverable's prune. Only Success prunes — a killed or failed
            // render published nothing — and only the deliverable arm: a batch
            // cell lands in `tmp/`, a different folder, never pruned.
            //
            // THE THREE OUTCOMES ANSWER THE DELIVERABLE FOLDER DIFFERENTLY
            // (architect 2026-09-02), and the split is the whole ruling:
            //   SUCCESS   prunes — the folder holds the current title's pair
            //             and nothing else.
            //   FAILED    DELETES THE PAIR THIS RENDER WAS TO PUBLISH — "the
            //             missing audio is the clue". The standing file is
            //             stale BY CONSTRUCTION: a render only reaches engine
            //             work past the up-to-date rung, which means the
            //             fingerprint on disk did not match this recipe, so
            //             what survives a failure is a deliverable that no
            //             longer matches the authored state and would be
            //             mirrored to the stick as if it did. Silence plus a
            //             plausible wav is the shape that misleads; an absent
            //             wav cannot.
            //   CANCELLED leaves it alone — a cancel is the user's own
            //             deliberate act, he knows the deliverable is the old
            //             one, and taking his audio away for a keypress he
            //             meant would be the product deciding for him.
            // (Deletion AT DISPATCH was weighed and not chosen — "no need to
            // overthink": a render that succeeds replaces the pair anyway, and
            // a cancel would then have destroyed it for nothing.)
            if (success && publishes_deliverable) {
                prune_render_folder(app.source_audio_path, app.engine_settings);
            }
            if (o == RenderOutcome::Failed) {
                // THE REMOVAL IS THE DELIVERABLE ARM'S ALONE. A FAILED
                // MISCELLANEOUS CELL — this dispatcher's other subject —
                // removes nothing: its output lands in `tmp/`, which is
                // transient by ruling and taken wholesale at the next
                // load-in-place's trash, and no mirror ships a cell as a
                // deliverable. (A sweep's cells are the one `tmp/` case that
                // IS removed, at on_batch_entry_complete, because a sweep
                // leaves a folder of them to walk.)
                if (publishes_deliverable) {
                    // The pair, composed at dispatch: the wav this request
                    // named and its `.fingerprint` sibling through the sidecar
                    // path's one owner (render_cache.h), never a second
                    // spelling of the extension. The staging sibling rides
                    // with them — do_render unlinks its own on nearly every
                    // failure path, and one left by an earlier crash is this
                    // render's own file by name.
                    remove_failed_render_files({
                        deliverable_wav,
                        std::filesystem::path(
                            render_staging_path(deliverable_wav.string())),
                        std::filesystem::path(fingerprint_sidecar_path(
                            deliverable_wav.string())),
                    });
                }
                // THE FAILURE'S CARD (architect 2026-09-02): a background act
                // the user was not watching finishing badly is an EVENT
                // (messaging.md), and until now a failed archival render
                // cleared the state cell to the same blank a rung-served
                // success shows. The reason is do_render's own composition,
                // ITS DISPLAY CLAUSE (GuiFailure, failure.h — the worker
                // printed the diagnostic with the full paths on its `Render
                // error:` line; this thread raises the clause whose paths are
                // named the project's way, so the card no longer ends
                // mid-path on his titles), appended under the appended-reason
                // rule — lowercased initial through the one composer, the
                // system's words left as they arrive. A Failed return with no
                // composed reason (there is no such site today) says the bare
                // sentence rather than a dangling colon. BOTH ARMS CARD: the
                // deliverable's failure and the miscellaneous cell's alike,
                // each an explicit command of his that produced nothing.
                notifications.notify(
                    AppState::NotificationClass::Normal,
                    failure.display.empty()
                        ? std::string("Render failed")
                        : "Render failed: " +
                              lowercase_initial(failure.display));
            }
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
    // Every cell of a batch carries the same folder (each sweep composes one
    // and stamps it on all of its requests), so the first request names it for
    // the whole run — kept because the requests are moved out one by one.
    batch_.folder     = batch_.reqs.front().batch_folder;
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
            // A SHORT SWEEP SAYS SO (architect 2026-09-02). The stderr line
            // stays whole; the card is the sweep's own answer on screen, since
            // a run that produced fewer cells than it counted up to looks
            // exactly like one that produced them all — the progress line is
            // gone by then either way. A COMPLETE sweep says nothing (a
            // render's completion is not notified), and a CANCELLED one says
            // nothing either: the cancel is his deliberate act, and the state
            // cell going blank at the press is its answer.
            if (batch_.rendered < total) {
                notifications.notify(
                    AppState::NotificationClass::Normal,
                    "Rendered " + std::to_string(batch_.rendered) +
                        " of " + std::to_string(total));
            }
            // AND A SWEEP THAT LEFT NOTHING LEAVES NO FOLDER. Each failed cell
            // has already removed its own files (on_batch_entry_complete), so
            // an empty folder here is a batch whose every cell failed — the
            // `3_bpm/` the dispatcher created before the first cell ran. It is
            // removed HERE rather than at the failing cell precisely because
            // do_render does NOT create a batch folder: taking it away
            // mid-sweep would fail every cell that followed. A cancelled sweep
            // keeps its folder, the standing ruling that a killed session may
            // leave partial batch folders (tmp/ is transient).
            if (!batch_.folder.empty()) {
                const std::filesystem::path folder(batch_.folder);
                std::error_code ec;
                if (std::filesystem::is_directory(folder, ec) &&
                    std::filesystem::is_empty(folder, ec) && !ec) {
                    // Through the same removal body the cells use, so a
                    // refusal reports on the one stderr line they share.
                    remove_failed_render_files({folder});
                }
            }
        }
        // A finished batch just leaves its artifacts on disk; nothing
        // auto-opens. The user presses `l` to listen or `'` to load an
        // entry in place by name.
        batch_.active = false;
        batch_.folder.clear();
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

    // THE BATCH LABEL NAMES WHAT IS BEING COUNTED (architect 2026-08-29):
    // "Rendering 3 of 8 grid iterations..." / "Rendering 3 of 8 BPM
    // iterations...",
    // the label reading as the plural noun the two numerals count rather than
    // as a category in a trailing parenthetical ("Rendering 3 of 8 (BPM)...",
    // which said neither what the 8 were nor what the render was doing to
    // them). BOTH LABELS ARE THE MENU ROWS' OWN WORDS since the 2026-08-31
    // rebrand ("Grid iterations" / "BPM iterations", kSeriesPopupItems): the
    // counted noun IS the command's name, so the progress line and the row
    // the user pressed say the same thing. The label is one string shared
    // with the stderr summary above,
    // where it fills the TAG SLOT ahead of the message proper
    // ("warptempo_gui: grid iterations: Rendered 3 of 8 entries") — the same
    // words read as a tag there, which is what lets the two surfaces keep one
    // string.
    // THE CASE RULE, unchanged by the rewording and no exception to anything:
    // "BPM" is an acronym and so capitalizes everywhere (architect
    // 2026-08-02), while "iterations" falls mid-sentence in both surfaces and
    // so is lowercase by the sentence-case rule itself (the product's text
    // rules are stated once at paint_handler.cpp's menu-row block).
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "Rendering %d of %d %s...",
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
    // The cell's own names, read BEFORE the request is moved onto the worker
    // (a moved-from request names nothing): they are what a failed cell's
    // removal composes its paths from.
    const std::string cell_folder   = req.batch_folder;
    const std::string cell_basename = req.batch_basename;
    async_renderer.dispatch(std::move(req),
        [this, cell_folder, cell_basename](RenderOutcome o,
                                           const GuiFailure&) {
            on_batch_entry_complete(o, cell_folder, cell_basename);
        });
}

void GuiInputHandler::on_batch_entry_complete(RenderOutcome outcome,
                                              const std::string& cell_folder,
                                              const std::string& cell_basename) {
    if (!batch_.active) return;

    if (outcome == RenderOutcome::Success)   ++batch_.rendered;
    if (outcome == RenderOutcome::Cancelled) app.queue_cancel_requested = true;
    if (outcome == RenderOutcome::Failed) {
        // A FAILED CELL'S PARTIAL OUTPUT GOES WITH IT (architect 2026-09-02),
        // the deliverable's rule one folder over: what a failure leaves under
        // `tmp/` is a cell that does not render the recipe its name and its
        // sidecars claim — the `'` load in place would apply that recipe, and
        // the player would offer the wav. do_render unwinds much of this
        // itself (it removes what IT created before returning Failed); the
        // removal here is the whole set by composition, so a cell that
        // inherited an older run's files under the same name goes too. The
        // CELL'S OWN card is not raised: a sweep's answer is the tail's
        // "Rendered N of M", one sentence for a run of up to 391 cells.
        remove_failed_batch_cell(cell_folder, cell_basename);
    }

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
// DATA, NOT DISPLAY, and so verbatim by the text rules' own data clause (the
// one statement is at paint_handler.cpp's menu-row block): no paint site reads
// this string — it is a sidecar value that also feeds the render fingerprint
// (render_cache.cpp's EngineField::Bpm arm), so its " bpm " stays lowercase
// where the display label capitalizes the acronym, and capitalizing it would
// move sidecar bytes and mint fresh cache keys.
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
// `<source parent>/tmp/<N>_bpm/`. The per-cell engine
// values land in the per-entry `.settings` sidecar's engine block (written
// by do_render); the `'` load-in-place (load_render_entry_in_place) applies
// them when loading a BPM cell in place. The
// substantive difference from the iter render handler is per-cell
// mutation of cell_settings.scale, in addition to per-cell marker mutation —
// which since 2026-08-26 reaches the WHOLE map, not the span alone: every
// effectively enabled owning marker outside the span is rescaled by the
// owner's own change (bpm_cell_warp_markers, input_handler.h, which since
// 2026-08-29 leaves every disabled marker untouched in the span and out).
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

    // Span endpoint is explicit (set on the `m` section gate). It is the index
    // of the first EFFECTIVELY-ENABLED marker after the last selected one
    // (section_end_index, warpmarkers.h): endpoint_idx == store size is the
    // SONG-END sentinel (no enabled marker follows, so the last section runs
    // to total_frames), endpoint_idx < size means the marker there is the
    // closing boundary. A value <= owner or past the size is
    // missing/malformed.
    const int store_size = static_cast<int>(base_warp_markers.size());
    const int endpoint_idx = owner.bpm_endpoint;
    if (endpoint_idx <= bpm_owner_idx || endpoint_idx > store_size) {
        return false;   // missing or malformed span: no sweep
    }
    // Span end frame: the boundary marker's time when one exists — the next
    // effectively-enabled marker's — else the song end. Named once here and
    // reused for the duration and the descriptor's endpoint seconds.
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

    const std::filesystem::path queue_root =
        project_batch_root(app.source_audio_path);

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

        // The cell's marker vector, whole, from its one owner
        // (bpm_cell_warp_markers, input_handler.h): the owner stamped with
        // the derived base tempo, the span's enabled markers passed, and
        // EVERY ENABLED OWNING MARKER OUTSIDE THE SPAN rescaled by the
        // owner's own change so the map keeps its shape — a disabled marker
        // is untouched wherever it sits. The editor commit already ran the
        // same rewrite at
        // both bracket ends (monotone in bpm), so in practice every cell
        // rescales in-bracket; the refusal here is the per-cell backstop,
        // reported like a refused derivation.
        auto cell_warp_markers = bpm_cell_warp_markers(
            base_warp_markers, bpm_owner_idx, endpoint_idx,
            computed->base_tempo_cents);
        if (!cell_warp_markers) {
            std::fprintf(stderr,
                "warptempo_gui: render-bpm: Rejected cell "
                "bpm=%s: a rescaled marker tempo leaves the tempo "
                "bracket [%s, %s]\n",
                format_value_double(bpm, 0).c_str(),
                format_tempo_cents(kTempoMinCents).c_str(),
                format_tempo_cents(kTempoMaxCents).c_str());
            continue;
        }

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
            app.source_audio_path, std::move(*cell_warp_markers), base_phase_resets,
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
        // The iteration sweep's own arm, in the same shared composition: the
        // two sweeps fail the same way and must not answer differently.
        const GuiFailure f =
            render_folder_creation_failure("render-bpm", batch_folder, ec);
        std::fprintf(stderr, "warptempo_gui: %s\n", f.diagnostic.c_str());
        notifications.notify(AppState::NotificationClass::Normal, f.display);
        return false;
    }

    // The batch's DISPLAY label — the progress line's counted noun and the
    // stderr summary's tag, both fed from this one string. "BPM iterations"
    // since the 2026-08-31 rebrand, which put ONE noun through the menu row
    // ("BPM iterations"), this line and the sweep's twin: the counted things
    // ARE the menu row's iterations. It read "BPM values" from 2026-08-29
    // (architect: the line says what the numbers count, and "BPM" alone named
    // a quantity rather than the cells) and plain "BPM" before that. "BPM" is
    // capitalized always (architect 2026-08-02): it is an acronym, not a
    // sentence position, so both surfaces carry the caps, and "iterations"
    // beside it stays lowercase for the reason the grid batch's does. It is
    // NOT the folder token above.
    if (async_renderer.is_busy()) {
        // A render dispatch kills the running render. Park the fully built
        // batch for the worker-idle pump.
        AppState::PendingArchivalCommand cmd;
        cmd.reqs        = std::move(reqs);
        cmd.batch_label = "BPM iterations";
        kill_running_render_and_park(std::move(cmd));
    } else {
        start_render_batch(std::move(reqs), "BPM iterations");
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
