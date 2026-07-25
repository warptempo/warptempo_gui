#include "paint_handler.h"

#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "waveform_worker.h"
#include "warp_frame_map.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

// Waveform / flag cache production. The off-screen surfaces
// on_redraw blits — the waveform plate (worker-rendered, incrementally
// panned, or synchronously rebuilt) and the
// flag-rect cache — are produced here, away from the on-screen paint
// path in paint_handler.cpp. (Trim is a LIVE paint pass now —
// GuiPaintHandler::paint_trim, below the playheads — so no trim pixel is
// cached; the former trim-stem cache is retired.) These are GuiPaintHandler
// members defined
// in a second translation unit; the class declaration and the on-screen
// paint passes live in paint_handler.{h,cpp}. Nothing here reaches the
// render path, so this is a labwc / build concern only.

// -- render_waveform_to_cache_surface ------------------------------------
//
// Extracted from on_redraw's inline cairo_create/cairo_destroy
// block (the body that lived between fingerprint-check and blit). Runs on
// the waveform worker thread when the main path goes through GuiWaveformWorker;
// the function itself is thread-agnostic — it touches only the dest surface
// the caller passed in, the audio handle's peak pyramid (read-only after
// load), and the warp_frame_map snapshot the caller built.

void render_waveform_to_cache_surface(
    cairo_surface_t* dest,
    int area_w,
    int area_h,
    const GuiAudio& audio,
    int64_t vp_start,
    int64_t vp_end,
    const std::vector<WarpFrameMapSegment>* warp_frame_map_or_null) {
    if (!dest || area_w <= 0 || area_h <= 0) return;

    cairo_t* ccr = cairo_create(dest);
    // Clear to transparent — the pixmap's background fill shows through
    // wherever the waveform strokes don't paint.
    cairo_save(ccr);
    cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(ccr);
    cairo_restore(ccr);
    // Samples draw into an inset sub-rect of the full-height cache surface:
    // waveform_inset_px() clear at top and bottom (the top band holds the cursor
    // triangle; the bottom mirrors it so the waveform is centered in its area).
    // The surface itself is still area_w x area_h and is blitted at area.y, so
    // the cache fingerprint and blit are unaffected — the inset is
    // a property of sample drawing only.
    const int inset_h = area_h - 2 * waveform_inset_px();
    if (inset_h <= 0) { cairo_destroy(ccr); return; }
    const GuiRect cache_area{0, waveform_inset_px(), area_w, inset_h};
    // Stereo is structural — channels != 2 refuses at load (see file_loader) —
    // so both channels always render. No channel gap: the 1972 Krips material
    // is effectively never unity, so the two channels' inner excursions do
    // not visually collide at the shared midline; a plain halve of the
    // inset region is clean. The two channels share the single inset band
    // (inset first, then split), so waveform_inset_px() stays clear above
    // the top channel and below the bottom channel, with the channels
    // meeting at the inset region's vertical center.
    const int ch_h = cache_area.h / 2;
    const GuiRect ch0{0, cache_area.y, cache_area.w, ch_h};
    const GuiRect ch1{0, cache_area.y + ch_h, cache_area.w, ch_h};
    render_waveform(ccr, ch0, audio, 0,
                    vp_start, vp_end,
                    kWaveform,
                    warp_frame_map_or_null);
    render_waveform(ccr, ch1, audio, 1,
                    vp_start, vp_end,
                    kWaveform,
                    warp_frame_map_or_null);
    cairo_destroy(ccr);
}

// -- render_waveform_strip_to_cache_surface ------------------------------
//
// Incremental-pan strip render. Redraws only the [strip_x, strip_x+strip_w)
// column of the plate (full height, including the inset bands) and leaves
// every other column untouched — the caller has already memmove'd the
// reusable pixels into place, and this fills the newly exposed edge.
//
// vp_start_full / vp_end_full describe the WHOLE plate's displayed viewport
// (not the strip's). The strip's own sample range is derived from them so the
// strip columns land at the exact frames a full-plate render at this viewport
// would produce; the shifted pixels and the freshly rendered strip then meet
// seamlessly at the strip boundary. Mirrors render_waveform_to_cache_surface's
// inset + stereo split, restricted to the strip columns and clipped so a
// 1px stroke cannot bleed past the strip edge into the reused pixels.
//
// Runs inline on the GUI thread (the strip is at most a window wide; see
// pan_waveform_incremental's over-a-window fallback), so unlike the worker
// render it touches the LIVE wf_cache.surface directly. Safe because the pan
// path only takes this branch when the worker is idle.
static void render_waveform_strip_to_cache_surface(
    cairo_surface_t* dest,
    int area_w,
    int area_h,
    int strip_x,
    int strip_w,
    const GuiAudio& audio,
    int64_t vp_start_full,
    int64_t vp_end_full,
    const std::vector<WarpFrameMapSegment>* warp_frame_map_or_null) {
    if (!dest || area_w <= 0 || area_h <= 0) return;
    if (strip_w <= 0 || strip_x < 0 || strip_x + strip_w > area_w) return;
    if (vp_end_full <= vp_start_full) return;

    const double disp_spp =
        static_cast<double>(vp_end_full - vp_start_full) / area_w;
    const int64_t strip_vp_start = vp_start_full +
        static_cast<int64_t>(std::nearbyint(disp_spp * strip_x));
    const int64_t strip_vp_end   = vp_start_full +
        static_cast<int64_t>(std::nearbyint(disp_spp * (strip_x + strip_w)));

    cairo_t* ccr = cairo_create(dest);

    // Clear only the strip column (full height, incl. the inset bands) so the
    // shifted-in pixels in the rest of the plate are left intact.
    cairo_save(ccr);
    cairo_rectangle(ccr, strip_x, 0, strip_w, area_h);
    cairo_clip(ccr);
    cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(ccr);
    cairo_restore(ccr);

    // Re-clip for the strokes so render_waveform's 1px line width cannot bleed
    // out of the strip into the reused columns.
    cairo_save(ccr);
    cairo_rectangle(ccr, strip_x, 0, strip_w, area_h);
    cairo_clip(ccr);

    const int inset_h = area_h - 2 * waveform_inset_px();
    if (inset_h <= 0) { cairo_restore(ccr); cairo_destroy(ccr); return; }
    // Stereo is structural — channels != 2 refuses at load (see file_loader) —
    // so both channels always render.
    const int ch_h = inset_h / 2;
    const GuiRect ch0{strip_x, waveform_inset_px(), strip_w, ch_h};
    const GuiRect ch1{strip_x, waveform_inset_px() + ch_h, strip_w, ch_h};
    render_waveform(ccr, ch0, audio, 0,
                    strip_vp_start, strip_vp_end,
                    kWaveform, warp_frame_map_or_null);
    render_waveform(ccr, ch1, audio, 1,
                    strip_vp_start, strip_vp_end,
                    kWaveform, warp_frame_map_or_null);
    cairo_restore(ccr);
    cairo_destroy(ccr);
}

// -- Waveform-worker dirty-detect and completion -------------------------
//
// maybe_enqueue_waveform_render: called from on_tick. Computes the desired
// waveform fingerprint (mirrors the input computation on_redraw does), and
// either dispatches a fresh job, sets the supersede slot, or no-ops.
//
// The input-computation block lives in compute_waveform_render_inputs(): it
// is the single source of truth for the desired waveform fingerprint, and
// is also consumed by force_synchronous_waveform_rebuild(). on_redraw's
// consumer derivation must stay in sync with the helper the same way it
// tracked the prior inline block.

GuiPaintHandler::WaveformRenderInputs
GuiPaintHandler::compute_waveform_render_inputs() const {
    WaveformRenderInputs in;
    if (app.loading || audio.total_frames() <= 0) return in;

    const GuiRect area = waveform_area(app);
    if (area.w <= 0 || area.h <= 0) return in;

    const double  spp      = current_samples_per_pixel(app, audio);
    const int64_t vp_start = app.viewport_start_sample;
    const int64_t vp_end   = viewport_end_sample(vp_start, spp, area.w);
    const int     sr       = audio.sample_rate();

    const bool is_target = (app.active_audio_view == 'T');
    std::vector<WarpFrameMapSegment> target_warp_frame_map;
    uint64_t target_warp_frame_map_hash = 0;
    if (is_target) {
        const TargetWarpFrameMapCache& c =
            target_view_warp_frame_map_cached(app, sr,
                static_cast<long>(audio.total_frames()));
        target_warp_frame_map      = c.warp_frame_map;       // job needs an owned snapshot
        target_warp_frame_map_hash = c.hash;
    }
    const GuiAudio* audio_source = &audio;

    in.vp_start      = vp_start;
    in.vp_end        = vp_end;
    in.area_w        = area.w;
    in.area_h        = area.h;
    in.is_target     = is_target;
    in.warp_frame_map_hash  = target_warp_frame_map_hash;
    in.warp_frame_map       = std::move(target_warp_frame_map);
    in.audio         = audio_source;
    in.valid         = true;
    return in;
}

void GuiPaintHandler::maybe_enqueue_waveform_render() {
    // Full dispatch freeze during three gestures: the two displayed-basis
    // drags (marker drag, trim drag) and the target-view tempo drag. The
    // first two freeze the displayed paint basis for the whole gesture (the
    // DragState "no per-drag map copy" contract), so no waveform job may be
    // DISPATCHED or PUBLISHED mid-gesture: on_waveform_render_done's
    // completion-drop gate is the publication half, and this is the dispatch
    // half. The TEMPO drag is the opposite of frozen — it re-warps the plate
    // synchronously per cent step (kick_waveform_sync) — and sits in this
    // gate for exactly that reason: its per-step sync renders own the plate
    // mid-gesture, and an async job racing them could publish a stale basis
    // between steps (the sync path never routes through this enqueue and is
    // unaffected). Freezing the whole enqueue (not just the warp_frame_map
    // hash the former drag_freeze excluded) closes the
    // drop-rewind-redispatch loop: a
    // job for a viewport-follow / resize fingerprint dispatched just before the
    // grab used to be dropped, rewound, then re-dispatched every tick because
    // the vp/area fields still differed — wasted full renders all gesture long.
    // Nothing that legitimately re-renders can occur mid-drag anyway: keys and
    // wheels are gesture-gated, playback was stopped by the arming top-strip
    // press so no follow scroll fires, and a compositor resize simply catches
    // up at the first post-gesture tick. With no mid-drag dispatch the
    // completion drop fires AT MOST ONCE (the one job in flight at the grab).
    // The strip drag, alt-pan, and region drag are deliberately NOT here — they
    // dispatch their own mid-gesture jobs and must keep rendering.
    if (app.drag.active || app.tempo_drag.active || app.trim_drag.active)
        return;

    WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) return;

    auto fingerprint_differs = [&](
        int64_t fp_vp_s, int64_t fp_vp_e,
        int     fp_aw,   int     fp_ah,
        bool    fp_t,
        uint64_t fp_h) -> bool {
        if (fp_vp_s != in.vp_start)        return true;
        if (fp_vp_e != in.vp_end)          return true;
        if (fp_aw   != in.area_w)          return true;
        if (fp_ah   != in.area_h)          return true;
        if (fp_t    != in.is_target)       return true;
        if (fp_h    != in.warp_frame_map_hash) return true;
        return false;
    };

    const bool diff_vs_pending = fingerprint_differs(
        wf_cache.pending_fp_vp_start,
        wf_cache.pending_fp_vp_end,
        wf_cache.pending_fp_area_w,
        wf_cache.pending_fp_area_h,
        wf_cache.pending_fp_target,
        wf_cache.pending_fp_warp_frame_map_hash);

    if (!diff_vs_pending) return;

    // We need to enqueue (or supersede an in-flight job). Build the job's
    // input snapshot now; the supersede slot stores the same struct shape
    // so we can hand it directly to dispatch from on_waveform_render_done.

    if (waveform_worker.is_busy()) {
        wf_cache.supersede             = true;
        wf_cache.supersede_vp_start    = in.vp_start;
        wf_cache.supersede_vp_end      = in.vp_end;
        wf_cache.supersede_area_w      = in.area_w;
        wf_cache.supersede_area_h      = in.area_h;
        wf_cache.supersede_target      = in.is_target;
        wf_cache.supersede_warp_frame_map_hash = in.warp_frame_map_hash;
        wf_cache.supersede_warp_frame_map     = std::move(in.warp_frame_map);
        return;
    }

    // Idle: dispatch immediately. Reuse pending_surface if dimensions
    // match; recreate on mismatch (window resize, first allocation).
    if (!wf_cache.pending_surface ||
        wf_cache.pending_width  != in.area_w ||
        wf_cache.pending_height != in.area_h) {
        if (wf_cache.pending_surface) {
            cairo_surface_destroy(wf_cache.pending_surface);
            wf_cache.pending_surface = nullptr;
        }
        wf_cache.pending_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, in.area_w, in.area_h);
        wf_cache.pending_width  = in.area_w;
        wf_cache.pending_height = in.area_h;
    }

    WaveformJob job;
    job.vp_start       = in.vp_start;
    job.vp_end         = in.vp_end;
    job.area_w         = in.area_w;
    job.area_h         = in.area_h;
    job.target         = in.is_target;
    job.warp_frame_map_hash   = in.warp_frame_map_hash;
    // Stash a copy of the warp_frame_map on the pending slot so the
    // flag cache (and the out-of-trim dim) can read it at completion-swap
    // time. The job consumes
    // the original by move; the copy stays on the cache.
    wf_cache.pending_fp_warp_frame_map = in.warp_frame_map;
    job.warp_frame_map        = std::move(in.warp_frame_map);
    job.surface        = wf_cache.pending_surface;
    // The audio the worker reads: always the one process-immortal source audio.
    job.audio          = in.audio;

    wf_cache.pending_fp_vp_start    = in.vp_start;
    wf_cache.pending_fp_vp_end      = in.vp_end;
    wf_cache.pending_fp_area_w      = in.area_w;
    wf_cache.pending_fp_area_h      = in.area_h;
    wf_cache.pending_fp_target      = in.is_target;
    wf_cache.pending_fp_warp_frame_map_hash = in.warp_frame_map_hash;

    waveform_worker.dispatch(std::move(job),
        [this](bool ok) { on_waveform_render_done(ok); });
}

void GuiPaintHandler::on_waveform_render_done(bool ok) {
    // Gesture-discard gate: a marker or trim drag freezes the displayed paint
    // basis for the whole gesture (the DragState "no per-drag map copy"
    // contract). maybe_enqueue_waveform_render's full dispatch freeze keeps a
    // NEW map edit from being DISPATCHED mid-gesture, but a job dispatched (or
    // parked in the supersede slot) BEFORE the drag began would still publish
    // its map HERE — the displayed basis would jump under a stationary pointer,
    // and every motion event re-reads it (apply_drag_motion, the trim drags, the
    // nudges). So drop the completed job WHOLESALE: no surface swap, no fp_*
    // publish, no item-cache stage, and CLEAR (never dispatch) the supersede
    // slot. Renders are repeatable — rewind pending_fp_* to the still-displayed
    // fp_* so the pending fingerprint again describes what is on screen; once the
    // gesture ends and the dispatch freeze reopens, the next
    // maybe_enqueue_waveform_render compares the current store's desired
    // fingerprint against that (== the displayed plate) and re-renders IFF the
    // plate is stale. Both a committed move (the store hash advanced) and a
    // no-op drag that left an EARLIER pending map edit unpublished (the desired
    // hash still differs from the displayed one) re-detect correctly; a
    // genuinely up-to-date plate stays put. Because the dispatch freeze enqueues
    // NOTHING mid-gesture, this drop fires AT MOST ONCE — for the single job in
    // flight at the grab; there is no drop-rewind-redispatch loop to sustain.
    // The TEMPO drag joins the drop for its own reason: it re-warps
    // synchronously per cent step, so a pre-grab async job publishing here
    // would paint a stale plate over the step-fresh one (its rewind then
    // points pending_fp_* at whatever the last sync step published — fp_* —
    // which is exactly what is on screen). Gated on the marker/tempo/trim
    // drags ALONE — the strip drag, alt-pan, and region
    // drag dispatch their own mid-gesture jobs and must keep publishing.
    if (app.drag.active || app.tempo_drag.active || app.trim_drag.active) {
        wf_cache.supersede = false;
        wf_cache.supersede_warp_frame_map.clear();
        wf_cache.pending_fp_vp_start            = wf_cache.fp_vp_start;
        wf_cache.pending_fp_vp_end              = wf_cache.fp_vp_end;
        wf_cache.pending_fp_area_w              = wf_cache.fp_area_w;
        wf_cache.pending_fp_area_h              = wf_cache.fp_area_h;
        wf_cache.pending_fp_target              = wf_cache.fp_target;
        wf_cache.pending_fp_warp_frame_map_hash = wf_cache.fp_warp_frame_map_hash;
        wf_cache.pending_fp_warp_frame_map      = wf_cache.fp_warp_frame_map;
        return;
    }

    if (!ok) {
        std::fprintf(stderr,
            "warptempo_gui: waveform worker reported failure; will retry "
            "on next tick\n");
        wf_cache.supersede = false;
        wf_cache.supersede_warp_frame_map.clear();
        // Make sure the next maybe_enqueue tick sees the live fingerprint
        // as dirty so we retry. Poison pending_fp_* with an impossible
        // area_w (-1) so the fingerprint comparison mismatches any valid
        // render (compute_waveform_render_inputs rejects area.w <= 0).
        wf_cache.pending_fp_area_w = -1;
        return;
    }

    // Supersede path: a viewport change happened mid-render. Discard the
    // just-completed pending pixels (they'll be overwritten by the next
    // render — no swap, no invalidate) and dispatch a fresh job built
    // from the supersede slot. The pending_surface dimensions may differ
    // from supersede_area_*, so reuse-or-recreate the same way the
    // idle-path does.
    if (wf_cache.supersede) {
        const int sw = wf_cache.supersede_area_w;
        const int sh = wf_cache.supersede_area_h;

        if (!wf_cache.pending_surface ||
            wf_cache.pending_width  != sw ||
            wf_cache.pending_height != sh) {
            if (wf_cache.pending_surface) {
                cairo_surface_destroy(wf_cache.pending_surface);
                wf_cache.pending_surface = nullptr;
            }
            if (sw > 0 && sh > 0) {
                wf_cache.pending_surface = cairo_image_surface_create(
                    CAIRO_FORMAT_ARGB32, sw, sh);
                wf_cache.pending_width  = sw;
                wf_cache.pending_height = sh;
            }
        }

        WaveformJob job;
        job.vp_start       = wf_cache.supersede_vp_start;
        job.vp_end         = wf_cache.supersede_vp_end;
        job.area_w         = sw;
        job.area_h         = sh;
        job.target         = wf_cache.supersede_target;
        job.warp_frame_map_hash   = wf_cache.supersede_warp_frame_map_hash;
        // Thread the supersede warp_frame_map into both the job and
        // pending_fp_warp_frame_map, the same way the idle-path dispatch does.
        // Copy first, then move into the job — the cache keeps a
        // displayable copy for the post-completion flag rebuild.
        wf_cache.pending_fp_warp_frame_map = wf_cache.supersede_warp_frame_map;
        job.warp_frame_map        = std::move(wf_cache.supersede_warp_frame_map);
        job.surface        = wf_cache.pending_surface;
        // The superseding job reads the one process-immortal source audio.
        job.audio          = &audio;

        wf_cache.pending_fp_vp_start    = wf_cache.supersede_vp_start;
        wf_cache.pending_fp_vp_end      = wf_cache.supersede_vp_end;
        wf_cache.pending_fp_area_w      = sw;
        wf_cache.pending_fp_area_h      = sh;
        wf_cache.pending_fp_target      = wf_cache.supersede_target;
        wf_cache.pending_fp_warp_frame_map_hash = wf_cache.supersede_warp_frame_map_hash;

        wf_cache.supersede = false;
        wf_cache.supersede_warp_frame_map.clear();

        waveform_worker.dispatch(std::move(job),
            [this](bool ok2) { on_waveform_render_done(ok2); });
        return;
    }

    // Swap the pending surface into the live slot. Cairo surface ownership
    // transfers cleanly via pointer swap; no flush needed because the
    // worker's cairo_destroy(ccr) committed the surface fully.
    std::swap(wf_cache.surface,        wf_cache.pending_surface);
    std::swap(wf_cache.width,          wf_cache.pending_width);
    std::swap(wf_cache.height,         wf_cache.pending_height);

    wf_cache.fp_vp_start     = wf_cache.pending_fp_vp_start;
    wf_cache.fp_vp_end       = wf_cache.pending_fp_vp_end;
    wf_cache.fp_area_w       = wf_cache.pending_fp_area_w;
    wf_cache.fp_area_h       = wf_cache.pending_fp_area_h;
    wf_cache.fp_rendered     = true;
    wf_cache.fp_target       = wf_cache.pending_fp_target;
    wf_cache.fp_warp_frame_map_hash = wf_cache.pending_fp_warp_frame_map_hash;
    // Publish the in-flight job's warp_frame_map to the displayed slot
    // so the next maybe_rebuild_flag_cache reads the same coordinate
    // system the just-blitted waveform pixels were rendered against.
    std::swap(wf_cache.fp_warp_frame_map,     wf_cache.pending_fp_warp_frame_map);

    // Rebuild the flag cache INLINE now, against the fingerprint just published
    // — the same shape the incremental-pan and synchronous-rebuild paths already
    // use (pan_waveform_incremental / force_synchronous_waveform_rebuild). The run
    // loop can service the wl_display fd (the frame callback that PAINTS) before
    // the timerfd tick that runs the on_tick dirty-check, so deferring the
    // flag rebuild to the tick let a frame blit the NEW plate over an OLD
    // flag cache — and the plate-registered overlays (selected stem,
    // phase-reset overlay), which read the NEW fp_* via
    // displayed_viewport_basis, visibly left their flags for one frame during a
    // follow-scroll / resize / pan-fallback publish. Doing the rebuild here makes
    // the committing frame blit new plate + new items together and promote the
    // staged basis atomically. The two-phase stage/promote ruling is UNCHANGED:
    // the rebuild STAGES the displayed hit map (app.staged_displayed_*), and
    // on_redraw still PROMOTES it at the committing frame; the on_tick rebuild
    // remains the idempotent fingerprint-guarded backstop. This also removes the
    // one-frame item lag every worker publish had. (The live trim pass needs no
    // rebuild — it reads the promoted item basis per frame.)
    maybe_rebuild_flag_cache();

    // Invalidate the waveform area so the next paint blits the new
    // pixels. Matches the rect Viewport::invalidate_waveform_area uses.
    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
}

// -- Synchronous waveform rebuild (discrete marker-cycle jump) -----------
//
// Tab / Shift+Tab / Ctrl+Shift+Tab routes through here from the input
// handler. The async worker rebuilds the waveform one frame late, so the
// same-tick flag rebuild keys off the lagging wf_cache.fp_* and the
// selection rectangle on the newly focused marker blinks across the
// worker window. Forcing a sync render + fp publish here makes the
// flag cache converge against the final viewport this tick.
//
// Writing into wf_cache.surface directly (not pending_surface + swap) is
// safe only because wait_until_idle() ran first — the worker is Idle and
// holds no reference to the live surface. Do not reorder the drain after
// the render.

// Synchronous-repaint rule (the waveform-layer coherence invariant):
//
// The waveform plate and the marker / playhead / dim / trim / flag overlays are
// separate paint layers. The overlays are computed inline from live state and
// paint on the next frame; the plate is the expensive layer. If a one-shot
// state change updates the overlays inline but defers the plate to the async
// worker, the overlays jump to their new positions one or two frames before the
// plate catches up — a cross-layer desync that surfaced as zoom lag, the A/B-tab
// and Tab recenter jump, and the source/target toggle smear.
//
// The rule, realized three ways — render the correct frame before painting:
//   1. One-shot discrete viewport/view jumps render synchronously, through this
//      function. The jumps this governs: zoom, center-on-playhead, the
//      viewport-shift playhead moves (Home / End and navigate-to-marker), the
//      A/B tab switch, the source/target toggle, and undo / redo. They arrive
//      at a bounded rate: pointer detents
//      coalesce to one action per pointer frame, and key repeat is compositor-
//      throttled, so a full inline render per event is affordable. The pyramid
//      bounds per-column cost, so the render is O(area_width) at any zoom level.
//   2. The one sustained pointer gesture (pan / scroll) uses the incremental
//      shift-and-strip path (pan_waveform_incremental) — also synchronous in
//      frame, just a partial render. The built-in touchpad emits a high-rate
//      continuous stream a full-render-per-event model cannot keep up with, so
//      pan must NOT be converted to a full sync render. The over-a-window
//      fast-flick fallback in pan_waveform_incremental already drops to this
//      full sync rebuild.
//   3. The async worker (maybe_enqueue_waveform_render) is the backstop for
//      changes the user is not actively driving: resize, the launch file
//      load, follow_scroll_if_needed during playback, and the on_tick safety
//      net that catches residual fingerprint drift. The marker, trim, and
//      tempo drags all freeze this worker's dispatch for the gesture's whole
//      duration (the full-enqueue gate at maybe_enqueue_waveform_render, with
//      on_waveform_render_done dropping the at-most-one job already in flight
//      at the grab — see those two functions' own comments for the mechanics).
//      The trim drag runs in either view and never touches the map, so
//      freezing it costs no re-warp. The marker (reposition) drag runs only
//      in its column's home view (W+source / P+target), likewise
//      map-independent there. Off marker's home, in W+target, a plain flag
//      drag is instead the TEMPO drag, which DOES re-warp — synchronously,
//      PER CENT STEP, through kick_waveform_sync (case 1 above), not just
//      once at release — so the worker stays frozen through every step for
//      that reason, not because a drag is idle.
//
// This is NOT "make everything synchronous." Async earns its keep for the
// touchpad torrent and for undriven / playback-adjacent changes; the rule is
// only that a one-shot jump must not paint its overlays against a stale plate.
void GuiPaintHandler::force_synchronous_waveform_rebuild() {
    const WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) return;

    // Drain any in-flight worker job so we own the cache surfaces.
    // Cancels a Running job and synchronously consumes a
    // CompletionPending one. After this the worker is Idle and will not touch
    // wf_cache surfaces underneath us.
    waveform_worker.wait_until_idle();

    // Clear any stale supersede request: wait_until_idle may have
    // cancelled a job whose supersede slot was set; we are about to
    // publish the current viewport ourselves, so the slot must not
    // re-dispatch an old one on a later tick.
    wf_cache.supersede = false;
    wf_cache.supersede_warp_frame_map.clear();

    // Render into the LIVE surface directly. Reuse-or-recreate on
    // dimension mismatch, mirroring the dispatch path.
    if (!wf_cache.surface ||
        wf_cache.width  != in.area_w ||
        wf_cache.height != in.area_h) {
        if (wf_cache.surface) {
            cairo_surface_destroy(wf_cache.surface);
            wf_cache.surface = nullptr;
        }
        wf_cache.surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, in.area_w, in.area_h);
        wf_cache.width  = in.area_w;
        wf_cache.height = in.area_h;
    }

    // in.audio is the source audio.
    render_waveform_to_cache_surface(
        wf_cache.surface,
        in.area_w, in.area_h,
        *in.audio,
        in.vp_start, in.vp_end,
        in.warp_frame_map.empty() ? nullptr : &in.warp_frame_map);

    // Publish the displayed fingerprint NOW so the flag rebuild at
    // the tail of this function reads the current viewport. Keep pending_fp_*
    // in lockstep so the next maybe_enqueue_waveform_render sees no diff and
    // does not re-dispatch the same target on the worker.
    wf_cache.fp_vp_start     = in.vp_start;
    wf_cache.fp_vp_end       = in.vp_end;
    wf_cache.fp_area_w       = in.area_w;
    wf_cache.fp_area_h       = in.area_h;
    wf_cache.fp_rendered     = true;
    wf_cache.fp_target       = in.is_target;
    wf_cache.fp_warp_frame_map_hash = in.warp_frame_map_hash;
    wf_cache.fp_warp_frame_map      = in.warp_frame_map;
    wf_cache.pending_fp_vp_start     = in.vp_start;
    wf_cache.pending_fp_vp_end       = in.vp_end;
    wf_cache.pending_fp_area_w       = in.area_w;
    wf_cache.pending_fp_area_h       = in.area_h;
    wf_cache.pending_fp_target       = in.is_target;
    wf_cache.pending_fp_warp_frame_map_hash = in.warp_frame_map_hash;
    wf_cache.pending_fp_warp_frame_map      = in.warp_frame_map;

    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);

    // Rebuild the flag cache inline, against the fingerprint just
    // published. Without this the plate leads its overlays: the run loop
    // services the wl_display fd (the input event AND the frame callback that
    // paints) before the timerfd tick that would run the on_tick dirty-check,
    // so the paint blits the fresh plate over a flag cache
    // that is one or more ticks stale — and during a zoom key-repeat or wheel
    // torrent the plate leads the overlays by the whole gesture. Doing the
    // rebuild here makes plate, fingerprint, flag cache, and the
    // staged displayed hit map all commit before the next paint — the
    // same-frame consistency every kick_waveform_sync caller expects (zoom,
    // Home/End, center-on-playhead, tab/view swaps, drops/deletes/commits,
    // undo/redo). The rebuild is fingerprint-guarded, so it is a cheap
    // no-op when the cache already matches. It also stages the
    // event-sync displayed hit map (promoted when on_redraw commits the
    // rebuild — the two-phase commit, ruling at the selector); running it
    // here closes the hit-test staleness window that otherwise lasts until the
    // next tick.
    maybe_rebuild_flag_cache();
}

// -- Incremental pan (shift-and-strip) -----------------------------------
//
// Pan fast-path: instead of re-rendering the whole window on the worker for a
// pure horizontal pan, shift the already-rendered plate by the pixel delta and
// render only the thin newly-exposed edge strip inline. O(strip) per frame
// instead of O(window), so the pipeline keeps pace with fast touchpad scroll.
//
// Routed here from Viewport::scroll_viewport (a pure pan — spp and view are
// unchanged) via the request_waveform_pan_ callback. new_vp_start is the
// post-clamp app.viewport_start_sample in the displayed domain (source frames
// in source view, target frames in target view).
//
// Target view uses this path too: a pan is a translation in the DISPLAYED
// (target) domain, the plate is uniformly indexed in that domain
// (render_waveform maps column i -> vp_start + spp*i, then target->source via
// the warp_frame_map), and the warp_frame_map is invariant across a pan (marker/scale edits
// rebuild it and stay on the worker path, caught by the fp_warp_frame_map_hash gate
// below). So a uniform pixel shift is exactly as correct in target view as in
// source view.
//
// This is an optimization layered over the worker backstop: every exit that is
// not a clean shift falls back to the worker (maybe_enqueue) or a synchronous
// full render, and the on_tick dirty-check re-renders if the fingerprint ever
// drifts.
void GuiPaintHandler::pan_waveform_incremental(int64_t new_vp_start,
                                               bool synchronous) {
    // The mid-gesture guarantee (synchronous mode, the Alt+drag grab-pan): no
    // frame this event paints shows the plate from a basis older than the
    // viewport. Every path that would otherwise leave the frame on a stale-basis
    // plate — a busy worker, or a non-shift precondition failure — is resolved
    // in-frame (drain-and-proceed, or a full synchronous rebuild) rather than
    // deferred to the async worker. The async caller class (wheel pan,
    // PageUp/PageDown) keeps the worker/enqueue fallbacks: those gestures are not
    // the live-basis-overlay ride the grab-pan is, and the on_tick backstop
    // covers their drift.
    auto fall_back = [&]() {
        if (synchronous) force_synchronous_waveform_rebuild();
        else             maybe_enqueue_waveform_render();
    };

    const WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) { fall_back(); return; }

    // Synchronous mode drains a busy worker rather than deferring to it. Deferring
    // (the async model) would leave THIS frame on the live plate, whose basis
    // predates the viewport we are about to paint — the mid-gesture desync this
    // path exists to prevent. wait_until_idle absorbs/discards the in-flight
    // completion coherently (the same drain force_synchronous_waveform_rebuild
    // relies on): after it the worker holds no reference to the cache surfaces and
    // wf_cache.fp_* is mutually consistent with wf_cache.surface, whether the job
    // published (swap + fp update) or was cancelled (no swap, fp unchanged). We
    // then continue to the precondition gate against that drained state — the
    // clean-shift case takes the fast path, the rest fall through to the full
    // rebuild below.
    if (synchronous && waveform_worker.is_busy()) {
        waveform_worker.wait_until_idle();
    }

    // Fallbacks: anything that is not a clean translate of the live plate goes to
    // the fall_back path (async: worker/enqueue; synchronous: full rebuild).
    //  - no plate yet (just after load)
    //  - worker mid-render: async leaves it to the worker (superseding keeps the
    //    latest viewport without racing a swap against our in-place shift);
    //    synchronous already drained above, so this only trips on a wait timeout
    //  - active drag: the plate is held still by the frozen-coordinate regime
    //    while the overlay repositions markers, so this is not a clean pan
    //  - dimension mismatch (resize since the plate was rendered)
    //  - view / warp_frame_map mismatch: not a pure pan (e.g. 't' toggle, marker edit)
    if (!wf_cache.surface ||
        waveform_worker.is_busy() ||
        app.drag.active ||
        wf_cache.fp_area_w != in.area_w ||
        wf_cache.fp_area_h != in.area_h ||
        wf_cache.width     != in.area_w ||
        wf_cache.height    != in.area_h ||
        wf_cache.fp_target       != in.is_target ||
        wf_cache.fp_warp_frame_map_hash != in.warp_frame_map_hash) {
        fall_back();
        return;
    }

    const int64_t old_vp_start = wf_cache.fp_vp_start;
    const int64_t old_vp_end   = wf_cache.fp_vp_end;
    const int     plate_w      = wf_cache.fp_area_w;
    const double  disp_spp =
        static_cast<double>(old_vp_end - old_vp_start) / plate_w;
    if (disp_spp <= 0.0) { fall_back(); return; }

    const int delta_px = static_cast<int>(
        std::nearbyint(static_cast<double>(new_vp_start - old_vp_start) /
                       disp_spp));

    // Sub-pixel move: nothing to redraw, just advance the plate bookkeeping so
    // the dim/cursor/markers track the new viewport and the dirty-check no-ops.
    if (delta_px == 0) {
        wf_cache.fp_vp_start         = in.vp_start;
        wf_cache.fp_vp_end           = in.vp_end;
        // Repair the COMPLETE pending fingerprint, not just the viewport — see
        // the memmove publish below for the full render/cancel-loop rationale.
        // A synchronous drain above may have poisoned pending_fp_area_w = -1;
        // the plate's basis (area, target, warp_frame_map) is unchanged by a
        // pure pan and equals in.* by the fallback gate, so these are the
        // correct published values on every route.
        wf_cache.pending_fp_vp_start    = in.vp_start;
        wf_cache.pending_fp_vp_end      = in.vp_end;
        wf_cache.pending_fp_area_w      = in.area_w;
        wf_cache.pending_fp_area_h      = in.area_h;
        wf_cache.pending_fp_target      = in.is_target;
        wf_cache.pending_fp_warp_frame_map_hash = in.warp_frame_map_hash;
        wf_cache.pending_fp_warp_frame_map      = in.warp_frame_map;
        // Plate bookkeeping advanced to the new viewport; bring the flag
        // cache with it so flags / dim do not lag the plate.
        maybe_rebuild_flag_cache();
        const GuiRect a = waveform_area(app);
        gui.invalidate_region(0, 0, app.width, a.y + a.h);
        return;
    }

    // Over-a-full-window pan: nothing to reuse. Synchronous full render
    // guarantees a correct frame (the rare fast-flick case), and keeps the
    // inline strip work strictly bounded to at most a window width.
    if (delta_px >= plate_w || delta_px <= -plate_w) {
        // force_synchronous_waveform_rebuild rebuilds the flag
        // cache inline at its tail, so the fast-flick frame is already fully
        // consistent — plate and overlays commit together, no reliance on an
        // on_tick that may not run before the next paint.
        force_synchronous_waveform_rebuild();
        return;
    }

    // Shift the plate in place. Content moves opposite the viewport: panning
    // toward later audio (delta_px > 0) slides pixels left and exposes the
    // right edge; panning toward earlier audio exposes the left edge.
    cairo_surface_flush(wf_cache.surface);
    unsigned char* data = cairo_image_surface_get_data(wf_cache.surface);
    const int stride     = cairo_image_surface_get_stride(wf_cache.surface);
    const int surf_h     = cairo_image_surface_get_height(wf_cache.surface);
    if (!data) { fall_back(); return; }

    int strip_x = 0;
    int strip_w = 0;
    if (delta_px > 0) {
        const int shift = delta_px;
        const size_t move_bytes =
            static_cast<size_t>(plate_w - shift) * 4;
        for (int row = 0; row < surf_h; ++row) {
            unsigned char* p = data + static_cast<size_t>(row) * stride;
            std::memmove(p, p + static_cast<size_t>(shift) * 4, move_bytes);
        }
        strip_x = plate_w - shift;
        strip_w = shift;
    } else {
        const int shift = -delta_px;
        const size_t move_bytes =
            static_cast<size_t>(plate_w - shift) * 4;
        for (int row = 0; row < surf_h; ++row) {
            unsigned char* p = data + static_cast<size_t>(row) * stride;
            std::memmove(p + static_cast<size_t>(shift) * 4, p, move_bytes);
        }
        strip_x = 0;
        strip_w = shift;
    }
    cairo_surface_mark_dirty(wf_cache.surface);

    // Render the newly exposed edge strip at the new viewport. in.vp_end is
    // the full plate's displayed end (== new_vp_start + the preserved span,
    // since spp and area_w are unchanged), so the strip columns map to the
    // identical frames a full render would produce.
    render_waveform_strip_to_cache_surface(
        wf_cache.surface,
        in.area_w, in.area_h,
        strip_x, strip_w,
        *in.audio,
        in.vp_start, in.vp_end,
        in.warp_frame_map.empty() ? nullptr : &in.warp_frame_map);

    // Advance the plate's viewport bookkeeping. fp_vp_start / disp_spp key the
    // live dim composite, markers, flags, and the cursor; pending_fp_* mirrors
    // it so the on_tick dirty-check sees the fingerprint already satisfied and
    // does not redundantly re-render the whole window.
    //
    // Repair the COMPLETE pending fingerprint, not just the viewport. In
    // synchronous mode this event may have DRAINED a running worker job above
    // (wait_until_idle cancels it), which lands on_waveform_render_done with
    // ok==false and poisons pending_fp_area_w = -1 as the tick-retry marker. A
    // viewport-only update would leave that poison in place: the next on_tick's
    // fingerprint_differs test would trip and enqueue a redundant full render,
    // which the next synchronous frame drains and cancels again, poisoning
    // anew — a render/cancel loop that runs until release. Rewriting every
    // pending_fp_* field (the same set force_synchronous_waveform_rebuild
    // republishes) closes it: drain -> cancel-poison -> shift-repair -> the tick
    // sees a satisfied fingerprint. The poison is only ever produced by the
    // synchronous drain (wait_until_idle is the waveform worker's sole cancel
    // route, called only from the two synchronous paths); the async caller
    // falls back to the worker whenever a job is in flight, so it can never
    // reach this clean-shift publish carrying a poison. The rewrite runs on
    // both routes anyway because everything but the viewport (area, target,
    // warp_frame_map) is unchanged by a pure pan and equals in.* by the
    // fallback gate above — so these are the correct published values whether
    // or not a poison was present, and the publish leaves a self-consistent
    // fingerprint by construction.
    wf_cache.fp_vp_start         = in.vp_start;
    wf_cache.fp_vp_end           = in.vp_end;
    wf_cache.pending_fp_vp_start = in.vp_start;
    wf_cache.pending_fp_vp_end   = in.vp_end;
    wf_cache.pending_fp_area_w   = in.area_w;
    wf_cache.pending_fp_area_h   = in.area_h;
    wf_cache.pending_fp_target   = in.is_target;
    wf_cache.pending_fp_warp_frame_map_hash = in.warp_frame_map_hash;
    wf_cache.pending_fp_warp_frame_map      = in.warp_frame_map;

    // The plate advanced synchronously in this event. Rebuild the
    // flag cache now, against the just-published fingerprint, so the overlay
    // layers (flags, and the dim) move in
    // lockstep with the plate. Without this they lag until the next on_tick
    // dirty-check, and a continuous drag shows the markers and their dim
    // trailing the waveform by a step. The rebuild is fingerprint-guarded
    // and cheap; this is the same inline rebuild the synchronous path runs.
    maybe_rebuild_flag_cache();

    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
}

// -- Flag-cache fingerprint hashes ---------------------------------------

namespace {

// FNV-1a over the live drag-overlay state, folded into the FlagCache
// fingerprint (its only consumer). Hashing the drag state directly removes the
// requirement that every mutation site of app.drag remember to bump a generation
// counter. Cost is dominated by the loop over moveable_times,
// which at observed selection sizes (0–5) is a handful of nanoseconds.
uint64_t hash_drag_overlay(const DragState& d) {
    uint64_t h = 0xcbf29ce484222325ULL;
    h ^= static_cast<uint64_t>(d.active ? 1 : 0);
    h *= 0x100000001b3ULL;
    h ^= static_cast<uint64_t>(d.dragging_markers.size());
    h *= 0x100000001b3ULL;
    for (int idx : d.dragging_markers) {
        h ^= static_cast<uint64_t>(idx);
        h *= 0x100000001b3ULL;
    }
    // moveable_times is parallel to dragging_markers; equal-length by
    // invariant. memcpy each double's bit pattern into a uint64 so the
    // floating-point representation is captured exactly (no equality /
    // NaN considerations).
    for (double t : d.moveable_times) {
        uint64_t bits;
        std::memcpy(&bits, &t, sizeof(bits));
        h ^= bits;
        h *= 0x100000001b3ULL;
    }
    return h;
}

// FNV-1a over the live selection set + last-selected anchor. Folded
// into the FlagCache fingerprint to avoid distributing a
// generation-bump across the fifteen mutation sites of selected_markers.
uint64_t hash_selection(const std::set<int>& s,
                        int last_selected) {
    uint64_t h = 0xcbf29ce484222325ULL;
    h ^= static_cast<uint64_t>(s.size());
    h *= 0x100000001b3ULL;
    for (int idx : s) {
        h ^= static_cast<uint64_t>(idx);
        h *= 0x100000001b3ULL;
    }
    h ^= static_cast<uint64_t>(last_selected);
    h *= 0x100000001b3ULL;
    return h;
}

} // namespace

// -- Flag-rect cache dirty-detect and rebuild ----------------------------
//
// Called from on_tick AFTER maybe_enqueue_waveform_render: wf_cache.fp_*
// coupling for the displayed-viewport half of the fingerprint, live-app-state
// reads for the marker-driven half. The cache holds
// EVERY flag shape (marker + phase reset) — the flag editor's text renders live
// in the marker-text lane, not in this cache, so the editing target's flag is an
// ordinary cached shape (no skip-guard). Trim's chips/stems left this cache and
// the retired trim-stem cache for the live paint_trim pass, so no trim field
// remains in the fingerprint (a trim edit repaints through its own mutation
// damage, no cache rebuild).

void GuiPaintHandler::maybe_rebuild_flag_cache() {
    if (app.loading || audio.total_frames() <= 0) return;

    // No live waveform yet → no flags. Until wf_cache.fp_rendered comes up
    // after the
    // first worker swap, the displayed-viewport fields hold defaults that
    // wouldn't agree with anything sensible.
    if (!wf_cache.fp_rendered) return;

    const GuiRect top_strip = top_strip_area(app);
    if (top_strip.w <= 0 || top_strip.h <= 0) return;

    const int surface_w = top_strip.w;
    const int surface_h = top_strip.h;

    // Displayed-viewport inputs from wf_cache.fp_*. Warp/phase flags are
    // positioned at marker times only.
    const int64_t  vp_start     = wf_cache.fp_vp_start;
    const int64_t  vp_end       = wf_cache.fp_vp_end;
    const bool     is_target    = wf_cache.fp_target;
    const uint64_t warp_frame_map_hash = wf_cache.fp_warp_frame_map_hash;

    // Marker-driven inputs from app state.
    const long long warp_gen   = app.warpmarkers.generation();
    const long long phase_gen  = app.phaseresetmarkers.generation();
    const uint64_t  drag_hash  = hash_drag_overlay(app.drag);
    const uint64_t  sel_hash   = hash_selection(
                                     app.selected_markers,
                                     app.last_selected_marker);
    const char      mv         = app.active_markers_view;

    const bool matches =
        flag_cache.surface &&
        flag_cache.fp_vp_start                == vp_start &&
        flag_cache.fp_vp_end                  == vp_end &&
        flag_cache.fp_area_w                  == surface_w &&
        flag_cache.fp_area_h                  == surface_h &&
        flag_cache.fp_target                  == is_target &&
        flag_cache.fp_warp_frame_map_hash            == warp_frame_map_hash &&
        flag_cache.fp_warp_generation   == warp_gen &&
        flag_cache.fp_phase_reset_generation  == phase_gen &&
        flag_cache.fp_drag_overlay_hash       == drag_hash &&
        flag_cache.fp_selection_hash          == sel_hash &&
        flag_cache.fp_active_markers_view     == mv;

    if (matches) return;

    if (!flag_cache.surface ||
        flag_cache.width  != surface_w ||
        flag_cache.height != surface_h) {
        if (flag_cache.surface) {
            cairo_surface_destroy(flag_cache.surface);
            flag_cache.surface = nullptr;
        }
        flag_cache.surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, surface_w, surface_h);
        flag_cache.width  = surface_w;
        flag_cache.height = surface_h;
    }

    cairo_t* ccr = cairo_create(flag_cache.surface);
    cairo_save(ccr);
    cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(ccr);
    cairo_restore(ccr);

    // top_strip_area is anchored at (0, 0), so the local rect equals the
    // surface rect. The blit at on_redraw time positions the surface back
    // at screen (0, 0).
    const GuiRect local_top_strip{0, 0, surface_w, surface_h};
    // Effective waveform width: the flag column mapping divides the displayed
    // span by this width — the same denominator the live trim pass and the hit
    // tests use — so flags stay column-aligned with the trim/stem verticals
    // below them. The
    // surface stays full-strip width; a non-multiple-of-16 window leaves the
    // gutter columns unpainted.
    const int wave_w = waveform_area(app).w;
    const int sr = audio.sample_rate();

    const std::vector<WarpFrameMapSegment>* tmap_arg =
        (is_target && !wf_cache.fp_warp_frame_map.empty())
            ? &wf_cache.fp_warp_frame_map : nullptr;

    DragOverlay drag_overlay_storage;
    const DragOverlay* drag_overlay = nullptr;
    if (app.drag.active) {
        drag_overlay_storage.indices = &app.drag.dragging_markers;
        drag_overlay_storage.times   = &app.drag.moveable_times;
        drag_overlay = &drag_overlay_storage;
    }

    // (No trim pass here: "markers over trim" is structural pass order now —
    // the live trim pass paints before the playheads, and this cache blits
    // after them — so the cache carries marker/phase-reset flag shapes only.)

    // Red-flag sets: the marker indices whose render normalizes to the 1.00
    // fallback, painted kAccent unless selected. Read from the memoized caches
    // (keyed on the respective store generation), so the silent classification
    // runs only on a marker change, not on this per-tick rebuild; the committed
    // store means a red flag freezes through a marker drag and re-evaluates at
    // commit. The active view supplies only its own column's set.
    if (mv == 'P') {
        const std::set<int>& pr_red =
            phase_reset_red_flag_set_cached(app).red;
        render_phase_reset_flags(
            ccr, local_top_strip, wave_w,
            app.phaseresetmarkers.markers(),
            vp_start, vp_end, sr,
            app.selected_markers,
            pr_red,
            tmap_arg,
            drag_overlay);
    } else {
        const std::set<int>& warp_red = warp_red_flag_set_cached(
            app, sr, static_cast<long>(audio.total_frames())).red;
        render_flags(ccr, local_top_strip, wave_w,
                     app.warpmarkers.markers(),
                     vp_start, vp_end, sr,
                     app.selected_markers,
                     warp_red,
                     tmap_arg,
                     drag_overlay);
    }

    cairo_destroy(ccr);

    flag_cache.fp_vp_start                = vp_start;
    flag_cache.fp_vp_end                  = vp_end;
    flag_cache.fp_area_w                  = surface_w;
    flag_cache.fp_area_h                  = surface_h;
    flag_cache.fp_target                  = is_target;
    flag_cache.fp_warp_frame_map_hash            = warp_frame_map_hash;
    flag_cache.fp_warp_generation   = warp_gen;
    flag_cache.fp_phase_reset_generation  = phase_gen;
    flag_cache.fp_drag_overlay_hash       = drag_hash;
    flag_cache.fp_selection_hash          = sel_hash;
    flag_cache.fp_active_markers_view     = mv;

    // Event-synchronized hit geometry, STAGE phase: these OFFSCREEN flags just
    // rebuilt, so stage the
    // map they baked (target view) or a clear (source view); on_redraw promotes
    // it at the frame that blits this cache. THIS IS THE SOLE ITEM-BASIS STAGE
    // SITE (grep app.staged_displayed_valid = true): the retired trim-stem
    // cache's rebuild used to stage the same value in the same tick, and
    // deleting that duplicate is safe exactly because THIS rebuild remains the
    // stage owner on every viewport/map/dimension change — every one of those
    // changes moves a field of this cache's fingerprint (fp_vp span, map hash,
    // target bit, top-strip dims; wave_w changes only with the window width,
    // which changes surface_w), so the rebuild fires and re-stages. A trim-only
    // change no longer stages anything, which is correct: trim never entered
    // the staged basis values, and the live trim pass reads the promoted basis
    // per frame. Ruling at the selector.
    if (is_target)
        app.staged_displayed_target_warp_frame_map = wf_cache.fp_warp_frame_map;
    else
        app.staged_displayed_target_warp_frame_map.clear();
    // Stage the displayed VIEWPORT alongside the map — same fp_vp span the
    // flags just mapped through, divided by the LIVE effective waveform width
    // wave_w these flags were column-mapped against (NOT fp_area_w, the
    // plate width).
    app.staged_displayed_vp_start = wf_cache.fp_vp_start;
    app.staged_displayed_vp_end   = wf_cache.fp_vp_end;
    app.staged_displayed_area_w   = wave_w;
    app.staged_displayed_valid = true;

    gui.invalidate_region(top_strip.x, top_strip.y,
                          top_strip.w, top_strip.h);
}
