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

// Waveform / marker / flag cache production. The off-screen surfaces
// on_redraw blits — the waveform plate (worker-rendered, incrementally
// panned, or synchronously rebuilt), the marker-stem cache, and the
// flag-rect cache — are produced here, away from the on-screen paint
// path in paint_handler.cpp. These are GuiPaintHandler members defined
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
    int channel_count,
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
    // the cache fingerprint, stem cache, and blit are unaffected — the inset is
    // a property of sample drawing only.
    const int inset_h = area_h - 2 * waveform_inset_px();
    if (inset_h <= 0) { cairo_destroy(ccr); return; }
    const GuiRect cache_area{0, waveform_inset_px(), area_w, inset_h};
    // channel_count is render_channels() = min(source channels, 2), and the
    // source load refuses channels != 2, so it is always 2 — only the stereo
    // split runs (the mono arm is unreachable and gone).
    if (channel_count >= 2) {
        // No channel gap: the 1972 Krips material
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
    }
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
// inset + mono/stereo split, restricted to the strip columns and clipped so a
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
    int channel_count,
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
    if (channel_count >= 2) {  // always 2 (stereo); see the sibling fn above
        const int ch_h = inset_h / 2;
        const GuiRect ch0{strip_x, waveform_inset_px(), strip_w, ch_h};
        const GuiRect ch1{strip_x, waveform_inset_px() + ch_h, strip_w, ch_h};
        render_waveform(ccr, ch0, audio, 0,
                        strip_vp_start, strip_vp_end,
                        kWaveform, warp_frame_map_or_null);
        render_waveform(ccr, ch1, audio, 1,
                        strip_vp_start, strip_vp_end,
                        kWaveform, warp_frame_map_or_null);
    }
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
    const int64_t vp_end   = vp_start +
        static_cast<int64_t>(std::nearbyint(spp * area.w));
    const int     sr       = audio.sample_rate();

    const bool is_target = (app.active_audio_view == 'T');
    std::vector<WarpFrameMapSegment> target_warp_frame_map;
    uint64_t target_warp_frame_map_hash = 0;
    if (is_target) {
        if (app.drag.active) {
            target_warp_frame_map = app.drag.frozen_warp_frame_map;
        } else {
            const TargetWarpFrameMapCache& c =
                target_view_warp_frame_map_cached(app, sr,
                    static_cast<long>(audio.total_frames()));
            target_warp_frame_map      = c.warp_frame_map;       // job needs an owned snapshot
            target_warp_frame_map_hash = c.hash;
        }
    }
    const GuiAudio* audio_source = &audio;

    in.vp_start      = vp_start;
    in.vp_end        = vp_end;
    in.area_w        = area.w;
    in.area_h        = area.h;
    in.is_target     = is_target;
    in.warp_frame_map_hash  = target_warp_frame_map_hash;
    in.channel_count = audio_source->render_channels();
    in.warp_frame_map       = std::move(target_warp_frame_map);
    in.audio         = audio_source;
    in.valid         = true;
    return in;
}

void GuiPaintHandler::maybe_enqueue_waveform_render() {
    WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) return;

    // Drag-freeze gate: during a target-view drag the warp_frame_map-derived
    // inputs are excluded from the dirty-detect comparison, so non-drag
    // viewport changes (which would still update pending_fp_* if they
    // happened) trigger a render but pure drag-motion does not.
    const bool drag_freeze = in.is_target && app.drag.active;

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
        if (!drag_freeze) {
            if (fp_h  != in.warp_frame_map_hash)     return true;
        }
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
    // stem cache can read it at completion-swap time. The job consumes
    // the original by move; the copy stays on the cache.
    wf_cache.pending_fp_warp_frame_map = in.warp_frame_map;
    job.warp_frame_map        = std::move(in.warp_frame_map);
    job.surface        = wf_cache.pending_surface;
    job.channel_count  = in.channel_count;
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
        // displayable copy for the post-completion stem rebuild.
        wf_cache.pending_fp_warp_frame_map = wf_cache.supersede_warp_frame_map;
        job.warp_frame_map        = std::move(wf_cache.supersede_warp_frame_map);
        job.surface        = wf_cache.pending_surface;
        // The superseding job reads the one process-immortal source audio;
        // channel_count reads from it.
        const GuiAudio* sup_audio = &audio;
        job.channel_count  = sup_audio->render_channels();
        job.audio          = sup_audio;

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
    // so the next maybe_rebuild_stem_cache reads the same coordinate
    // system the just-blitted waveform pixels were rendered against.
    std::swap(wf_cache.fp_warp_frame_map,     wf_cache.pending_fp_warp_frame_map);
    wf_cache.dirty           = false;

    // Invalidate the waveform area so the next paint blits the new
    // pixels. Matches the rect Viewport::invalidate_waveform_area uses.
    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
}

// -- Synchronous waveform rebuild (discrete marker-cycle jump) -----------
//
// Tab / Shift+Tab / Ctrl+Shift+Tab routes through here from the input
// handler. The async worker rebuilds the waveform one frame late, so the
// same-tick stem/flag rebuilds key off the lagging wf_cache.fp_* and the
// selection rectangle on the newly focused marker blinks across the
// worker window. Forcing a sync render + fp publish here makes the
// stem/flag caches converge against the final viewport this tick.
//
// Writing into wf_cache.surface directly (not pending_surface + swap) is
// safe only because wait_until_idle() ran first — the worker is Idle and
// holds no reference to the live surface. Do not reorder the drain after
// the render.

// Synchronous-repaint rule (the waveform-layer coherence invariant):
//
// The waveform plate and the marker / playhead / dim / stem / flag overlays are
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
//      load, target-view marker drags (frozen during the drag, re-rendered on
//      the worker at release), follow_scroll_if_needed during playback, and
//      the on_tick safety net that catches residual fingerprint drift.
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
        in.channel_count,
        *in.audio,
        in.vp_start, in.vp_end,
        in.warp_frame_map.empty() ? nullptr : &in.warp_frame_map);

    // Publish the displayed fingerprint NOW so this same tick's
    // maybe_rebuild_stem_cache / maybe_rebuild_flag_cache read the
    // current viewport. Keep pending_fp_* in lockstep so the next
    // maybe_enqueue_waveform_render sees no diff and does not
    // re-dispatch the same target on the worker.
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

    wf_cache.dirty = false;

    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
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
void GuiPaintHandler::pan_waveform_incremental(int64_t new_vp_start) {
    const WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) { maybe_enqueue_waveform_render(); return; }

    // Fallbacks (section 5): anything that is not a clean translate of the
    // live plate goes to the worker / full-render path.
    //  - no plate yet (just after load)
    //  - worker mid-render: leave it to the worker; superseding keeps the
    //    latest viewport without racing a swap against our in-place shift
    //  - active drag: the warp_frame_map is frozen / mid-deformation
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
        maybe_enqueue_waveform_render();
        return;
    }

    const int64_t old_vp_start = wf_cache.fp_vp_start;
    const int64_t old_vp_end   = wf_cache.fp_vp_end;
    const int     plate_w      = wf_cache.fp_area_w;
    const double  disp_spp =
        static_cast<double>(old_vp_end - old_vp_start) / plate_w;
    if (disp_spp <= 0.0) { maybe_enqueue_waveform_render(); return; }

    const int delta_px = static_cast<int>(
        std::nearbyint(static_cast<double>(new_vp_start - old_vp_start) /
                       disp_spp));

    // Sub-pixel move: nothing to redraw, just advance the plate bookkeeping so
    // the dim/cursor/markers track the new viewport and the dirty-check no-ops.
    if (delta_px == 0) {
        wf_cache.fp_vp_start         = in.vp_start;
        wf_cache.fp_vp_end           = in.vp_end;
        wf_cache.pending_fp_vp_start = in.vp_start;
        wf_cache.pending_fp_vp_end   = in.vp_end;
        // Plate bookkeeping advanced to the new viewport; bring the overlay
        // caches with it so stems / flags / dim do not lag the plate.
        maybe_rebuild_stem_cache();
        maybe_rebuild_flag_cache();
        const GuiRect a = waveform_area(app);
        gui.invalidate_region(0, 0, app.width, a.y + a.h);
        return;
    }

    // Over-a-full-window pan: nothing to reuse. Synchronous full render
    // guarantees a correct frame (the rare fast-flick case), and keeps the
    // inline strip work strictly bounded to at most a window width.
    if (delta_px >= plate_w || delta_px <= -plate_w) {
        force_synchronous_waveform_rebuild();
        // force_synchronous_waveform_rebuild publishes the fingerprint but
        // leaves the stem / flag rebuild to a later tick; do it now so the
        // fast-flick frame is fully consistent rather than relying on an
        // on_tick that may not run before the next paint.
        maybe_rebuild_stem_cache();
        maybe_rebuild_flag_cache();
        return;
    }

    // Shift the plate in place. Content moves opposite the viewport: panning
    // toward later audio (delta_px > 0) slides pixels left and exposes the
    // right edge; panning toward earlier audio exposes the left edge.
    cairo_surface_flush(wf_cache.surface);
    unsigned char* data = cairo_image_surface_get_data(wf_cache.surface);
    const int stride     = cairo_image_surface_get_stride(wf_cache.surface);
    const int surf_h     = cairo_image_surface_get_height(wf_cache.surface);
    if (!data) { maybe_enqueue_waveform_render(); return; }

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
        in.channel_count,
        *in.audio,
        in.vp_start, in.vp_end,
        in.warp_frame_map.empty() ? nullptr : &in.warp_frame_map);

    // Advance the plate's viewport bookkeeping. fp_vp_start / disp_spp key the
    // live dim composite, markers, flags, and the cursor; pending_fp_* mirrors
    // it so the on_tick dirty-check sees the fingerprint already satisfied and
    // does not redundantly re-render the whole window. Everything else
    // (area, target, warp_frame_map) is unchanged by a pure pan and was
    // verified equal to in.* by the fallback gate above.
    wf_cache.fp_vp_start         = in.vp_start;
    wf_cache.fp_vp_end           = in.vp_end;
    wf_cache.pending_fp_vp_start = in.vp_start;
    wf_cache.pending_fp_vp_end   = in.vp_end;

    // The plate advanced synchronously in this event. Rebuild the stem and
    // flag caches now, against the just-published fingerprint, so the overlay
    // layers (stems, flags, and the dim they paint under markers) move in
    // lockstep with the plate. Without this they lag until the next on_tick
    // dirty-check, and a continuous drag shows the markers and their dim
    // trailing the waveform by a step. Both rebuilds are fingerprint-guarded
    // and cheap; this mirrors force_synchronous_waveform_rebuild, which
    // likewise publishes the fingerprint so the same-tick stem/flag rebuild
    // reads the new viewport.
    maybe_rebuild_stem_cache();
    maybe_rebuild_flag_cache();

    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
}

// -- Marker stem cache dirty-detect and rebuild --------------------------
//
// Called from on_tick AFTER maybe_enqueue_waveform_render. Reads displayed-
// viewport inputs from wf_cache.fp_* (the LIVE waveform fingerprint — the
// post-swap viewport, not necessarily the current app state); reads
// marker-driven inputs from app state directly. Diverging fingerprint
// triggers a synchronous offscreen rebuild + region invalidation.

namespace {

// FNV-1a over the live drag-overlay state, folded into the StemCache
// fingerprint. Hashing the drag state directly removes the requirement
// that every mutation site of app.drag remember to bump a generation
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

void GuiPaintHandler::maybe_rebuild_stem_cache() {
    if (app.loading || audio.total_frames() <= 0) return;

    // No live waveform yet → no stems. The first stem rebuild happens
    // after the first waveform-completion swap (which sets
    // wf_cache.fp_rendered); until then the displayed-viewport fields hold
    // defaults that wouldn't agree with anything sensible on the marker
    // side anyway.
    if (!wf_cache.fp_rendered) return;

    const GuiRect area = waveform_area(app);
    if (area.w <= 0 || area.h <= 0) return;

    // Surface includes the stem overhang above the waveform — see the
    // geometry note in StemCache's class comment. The overhang
    // is the TALLER trim value (stem_cache_overhang_px = kStemAboveWaveformPx
    // + row_h + gap) so the upper-row trim stem is not clipped at its top;
    // marker stems land transparently lower in the same surface.
    const int overhang = stem_cache_overhang_px();
    const int surface_w = area.w;
    const int surface_h = area.h + overhang;

    // Displayed-viewport inputs: read from wf_cache.fp_*, not app state.
    const int64_t  vp_start     = wf_cache.fp_vp_start;
    const int64_t  vp_end       = wf_cache.fp_vp_end;
    const bool     is_target    = wf_cache.fp_target;
    const uint64_t warp_frame_map_hash = wf_cache.fp_warp_frame_map_hash;

    // Marker-driven inputs: read live from app state.
    const long long warp_gen   = app.warpmarkers.generation();
    const long long phase_gen  = app.phaseresetmarkers.generation();
    const uint64_t  drag_hash  = hash_drag_overlay(app.drag);
    const bool     drag_active = app.drag.active;
    const char     mv          = app.active_markers_view;
    const uint64_t sel_hash    = hash_selection(app.selected_markers,
                                                app.last_selected_marker);

    // Trim boundary stems. Positions ride trim_begin / trim_end
    // (displayed domain), has-set + selected bits from the active A/B tab.
    // Computed by the shared helper so the flag cache's b/e chips read the
    // exact same values (chip + stem are one unit).
    const DisplayedTrim dtrim   = compute_displayed_trim();
    const bool trim_has_begin   = dtrim.has_begin;
    const bool trim_has_end     = dtrim.has_end;
    const bool trim_begin_sel   = dtrim.begin_selected;
    const bool trim_end_sel     = dtrim.end_selected;
    const int64_t trim_begin    = dtrim.begin;
    const int64_t trim_end      = dtrim.end;

    const bool matches =
        stem_cache.surface &&
        stem_cache.fp_vp_start                == vp_start &&
        stem_cache.fp_vp_end                  == vp_end &&
        stem_cache.fp_trim_begin              == trim_begin &&
        stem_cache.fp_trim_end                == trim_end &&
        stem_cache.fp_area_w                  == surface_w &&
        stem_cache.fp_area_h                  == surface_h &&
        stem_cache.fp_target                  == is_target &&
        stem_cache.fp_warp_frame_map_hash            == warp_frame_map_hash &&
        stem_cache.fp_warp_generation   == warp_gen &&
        stem_cache.fp_phase_reset_generation  == phase_gen &&
        stem_cache.fp_drag_overlay_hash       == drag_hash &&
        stem_cache.fp_drag_active             == drag_active &&
        stem_cache.fp_active_markers_view     == mv &&
        stem_cache.fp_selection_hash          == sel_hash &&
        stem_cache.fp_trim_has_begin          == trim_has_begin &&
        stem_cache.fp_trim_has_end            == trim_has_end &&
        stem_cache.fp_trim_begin_selected     == trim_begin_sel &&
        stem_cache.fp_trim_end_selected       == trim_end_sel;

    if (matches) return;

    // Reuse-or-recreate the surface on dimension change.
    if (!stem_cache.surface ||
        stem_cache.width  != surface_w ||
        stem_cache.height != surface_h) {
        if (stem_cache.surface) {
            cairo_surface_destroy(stem_cache.surface);
            stem_cache.surface = nullptr;
        }
        stem_cache.surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, surface_w, surface_h);
        stem_cache.width  = surface_w;
        stem_cache.height = surface_h;
    }

    cairo_t* ccr = cairo_create(stem_cache.surface);
    cairo_save(ccr);
    cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(ccr);
    cairo_restore(ccr);

    // Local rect translates the screen-coord stem geometry into the cache
    // surface's coordinate system. Setting local.y = overhang puts the
    // waveform top at that offset, so the TALLEST stem (the upper-row trim
    // stem, top = local.y - kFlagBottomLiftPx - (row_h + gap)) lands at
    // surface y = 0, marker stems (top = local.y - kFlagBottomLiftPx) land
    // transparently lower, and y1 = overhang + area.h = surface_h. The blit
    // at on_redraw time positions the surface at screen y = area.y - overhang
    // so everything lands correctly.
    const GuiRect local_area{
        0,
        overhang,
        surface_w,
        area.h
    };
    const TrimRange trim_struct{trim_begin, trim_end};
    const int sr = audio.sample_rate();

    // Target-view stems consume the displayed warp_frame_map (the one baked
    // into the live waveform pixels), not a freshly-built one — keeps
    // stem positions consistent with the displayed waveform during the
    // worker's rebuild window.
    const std::vector<WarpFrameMapSegment>* tmap_arg =
        (is_target && !wf_cache.fp_warp_frame_map.empty())
            ? &wf_cache.fp_warp_frame_map : nullptr;

    // Drag overlay: pass through only when a drag is live. During a
    // drag the fingerprint mismatches every tick on the drag-overlay
    // hash alone (moveable_times[k] changes on every motion event), so
    // this rebuild reads the live moveable_times each pass.
    DragOverlay drag_overlay_storage;
    const DragOverlay* drag_overlay = nullptr;
    if (drag_active) {
        drag_overlay_storage.indices = &app.drag.dragging_markers;
        drag_overlay_storage.times   = &app.drag.moveable_times;
        drag_overlay = &drag_overlay_storage;
    }

    // Trim boundary stems, painted in both 'W' and 'P' views. Positions are
    // the displayed-domain trim frames (already translated); the has-set /
    // selected bits decide which stems draw and in what color. Painted BEFORE
    // the regular marker stems so that where a trim bound and a regular marker
    // share a column the regular stem (painted last on this shared surface)
    // sits in front; the taller trim stem reads as "underneath," reachable by
    // its hotkey.
    render_trim_stems(
        ccr, local_area, vp_start, vp_end,
        trim_struct,
        trim_has_begin, trim_begin_sel,
        trim_has_end, trim_end_sel,
        wf_cache.surface);

    if (mv == 'P') {
        const auto& list = app.phaseresetmarkers.markers();
        render_phaseresetmarkers(
            ccr, local_area, list,
            vp_start, vp_end, sr,
            app.selected_markers, tmap_arg, drag_overlay,
            wf_cache.surface);
    } else {
        const auto& list = app.warpmarkers.markers();
        render_markers(
            ccr, local_area, list,
            vp_start, vp_end, sr,
            app.selected_markers, tmap_arg, drag_overlay,
            wf_cache.surface);
    }

    cairo_destroy(ccr);

    stem_cache.fp_vp_start                  = vp_start;
    stem_cache.fp_vp_end                    = vp_end;
    stem_cache.fp_trim_begin                = trim_begin;
    stem_cache.fp_trim_end                  = trim_end;
    stem_cache.fp_area_w                    = surface_w;
    stem_cache.fp_area_h                    = surface_h;
    stem_cache.fp_target                    = is_target;
    stem_cache.fp_warp_frame_map_hash              = warp_frame_map_hash;
    stem_cache.fp_warp_generation     = warp_gen;
    stem_cache.fp_phase_reset_generation    = phase_gen;
    stem_cache.fp_drag_overlay_hash         = drag_hash;
    stem_cache.fp_drag_active               = drag_active;
    stem_cache.fp_active_markers_view       = mv;
    stem_cache.fp_selection_hash            = sel_hash;
    stem_cache.fp_trim_has_begin            = trim_has_begin;
    stem_cache.fp_trim_has_end              = trim_has_end;
    stem_cache.fp_trim_begin_selected       = trim_begin_sel;
    stem_cache.fp_trim_end_selected         = trim_end_sel;
    stem_cache.dirty                        = false;

    // Invalidate the stem region. Viewport-driven invalidations
    // already cover this strip, but pure marker-store edits (warp_gen /
    // phase_gen bumps) don't pass through the viewport's invalidator —
    // damage the strip explicitly so the next paint blits the new
    // pixels. Idempotent against the waveform's own damage.
    gui.invalidate_region(
        0,
        area.y - overhang,
        app.width,
        surface_h);
}

// -- Flag-rect cache dirty-detect and rebuild ----------------------------
//
// Mirrors maybe_rebuild_stem_cache: same wf_cache.fp_* coupling for the
// displayed-viewport half of the fingerprint; same live-app-state reads
// for the marker-driven half (with selection + editor target additions).
// The cache holds every flag rect EXCEPT the FlagPayload-editor target
// (skipped via the render_flags skip-guard so the live editor render in
// on_redraw owns those pixels — keeps the cache fingerprint independent
// of pending-text width and cursor blink).

void GuiPaintHandler::maybe_rebuild_flag_cache() {
    if (app.loading || audio.total_frames() <= 0) return;

    // No live waveform yet → no flags. Same pre-first-completion guard as
    // the stem cache uses; until wf_cache.fp_rendered comes up after the
    // first worker swap, the displayed-viewport fields hold defaults that
    // wouldn't agree with anything sensible.
    if (!wf_cache.fp_rendered) return;

    const GuiRect top_strip = top_strip_area(app);
    if (top_strip.w <= 0 || top_strip.h <= 0) return;

    const int surface_w = top_strip.w;
    const int surface_h = top_strip.h;

    // Displayed-viewport inputs from wf_cache.fp_*. Warp/phase flags are
    // positioned at marker times only. The b/e trim chips also ride this
    // strip, so the displayed-domain trim positions + has/selected bits (from
    // the shared helper, identical to the stem cache's) are now part of the
    // flag cache's identity.
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
    // Iteration mode only affects warp-view flags.
    const bool      iter_on    = app.iteration_mode_enabled &&
                                 mv == 'W';

    // FlagPayload editor target drives the skip-guard (cache leaves a
    // hole for the live editor render to fill). The IterationBracket /
    // BpmBracket kinds do not feed the cache fingerprint.
    // FlagPayload (W view) drives the skip-guard: the cache leaves a hole for
    // the live editor render to fill. P view has no per-flag editor.
    int flag_target = -1;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::FlagPayload) {
        flag_target = app.top_flag_editor.target;
    }

    // Displayed-domain trim state for the b/e chips (shared helper,
    // same values the stem cache paints its stems at).
    const DisplayedTrim dtrim = compute_displayed_trim();

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
        flag_cache.fp_active_markers_view     == mv &&
        flag_cache.fp_flag_editor_target      == flag_target &&
        flag_cache.fp_iteration_mode_enabled  == iter_on &&
        flag_cache.fp_trim_begin              == dtrim.begin &&
        flag_cache.fp_trim_end                == dtrim.end &&
        flag_cache.fp_trim_has_begin          == dtrim.has_begin &&
        flag_cache.fp_trim_has_end            == dtrim.has_end &&
        flag_cache.fp_trim_begin_selected     == dtrim.begin_selected &&
        flag_cache.fp_trim_end_selected       == dtrim.end_selected;

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
    // Effective waveform width: the flag column mapping shares the stem
    // cache's samples-per-pixel (both divide the same displayed span by this
    // width), so chips stay column-aligned with the stems below them. The
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

    // Cache overlay: marker_index activates the skip-guard for the
    // FlagPayload editor target. Other fields stay defaulted — pending
    // text, cursor state, selection range live in the live editor
    // render only.
    FlagEditorOverlay cache_overlay;
    cache_overlay.marker_index        = flag_target;

    if (mv == 'P') {
        render_phase_reset_flags(
            ccr, local_top_strip, wave_w,
            app.phaseresetmarkers.markers(),
            vp_start, vp_end, sr,
            flag_font_size_px(),
            app.selected_markers,
            tmap_arg,
            drag_overlay);
    } else {
        render_flags(ccr, local_top_strip, wave_w,
                     app.warpmarkers.markers(),
                     vp_start, vp_end, sr,
                     flag_font_size_px(),
                     app.selected_markers,
                     cache_overlay,
                     tmap_arg,
                     drag_overlay,
                     iter_on);
    }

    // The b/e trim chips cap their stems in the upper top row. Painted
    // in both 'W' and 'P' views (like the stems) in the AUTHORING views.
    // The real waveform_area sets the upper-row chip bottom; the top strip's
    // screen origin equals the cache surface origin (0,0), so local_top_strip
    // and the real waveform rect need no translation.
    render_trim_flags(
        ccr, local_top_strip, waveform_area(app),
        vp_start, vp_end, flag_font_size_px(),
        TrimRange{dtrim.begin, dtrim.end},
        dtrim.has_begin, dtrim.begin_selected,
        dtrim.has_end, dtrim.end_selected);

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
    flag_cache.fp_flag_editor_target      = flag_target;
    flag_cache.fp_iteration_mode_enabled  = iter_on;
    flag_cache.fp_trim_begin              = dtrim.begin;
    flag_cache.fp_trim_end                = dtrim.end;
    flag_cache.fp_trim_has_begin          = dtrim.has_begin;
    flag_cache.fp_trim_has_end            = dtrim.has_end;
    flag_cache.fp_trim_begin_selected     = dtrim.begin_selected;
    flag_cache.fp_trim_end_selected       = dtrim.end_selected;
    flag_cache.dirty                      = false;

    gui.invalidate_region(top_strip.x, top_strip.y,
                          top_strip.w, top_strip.h);
}
