#include "paint_handler.h"

#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "waveform_worker.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Waveform / flag cache production. The off-screen surfaces
// on_redraw blits — the waveform plate (worker-rendered or synchronously
// rebuilt; the incremental shift-and-strip pan was retired 2026-07-26) and the
// flag-rect cache — are produced here, away from the on-screen paint
// path in paint_handler.cpp. (Trim is a LIVE paint pass now —
// GuiPaintHandler::paint_trim — so no trim pixel is
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
    int inset_px,
    const GuiAudio& audio,
    int64_t vp_start,
    double  painter_spp,
    const std::vector<WarpFrameMapSegment>* warp_frame_map_or_null) {
    if (!dest || area_w <= 0 || area_h <= 0) return;

    // Clear to transparent — whatever ground the paint pass laid under the plate
    // (kWaveformCanvas, or a kWaveformRegionCanvas recolor) shows through
    // wherever the waveform samples don't paint. No ground color is ever baked
    // into the plate: its alpha is exactly what composites the ink over that
    // ground through its gaps (binary alpha since the aliasing deletion), which
    // is why a highlighted
    // span needs no plate of its own.
    // This is the LAST cairo drawing on the surface: render_waveform writes the
    // pixel words directly, so the context is destroyed before those CPU writes
    // begin (render_waveform still flushes defensively, per its contract).
    {
        cairo_t* ccr = cairo_create(dest);
        cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(ccr);
        cairo_destroy(ccr);
    }
    // Samples draw into an inset sub-rect of the full-height cache surface:
    // inset_px clear at top and bottom (the top band holds the cursor
    // triangle; the bottom mirrors it so the waveform is centered in its area).
    // inset_px is the GUI-thread-captured waveform_inset_px() snapshot (see the
    // WaveformJob geometry-capture note) — this function reads no font-scale
    // state itself, so a mid-render font commit cannot tear the geometry.
    // The surface itself is still area_w x area_h and is blitted at area.y, so
    // the cache fingerprint and blit are unaffected — the inset is
    // a property of sample drawing only.
    const int inset_h = area_h - 2 * inset_px;
    if (inset_h <= 0) return;
    const GuiRect cache_area{0, inset_px, area_w, inset_h};
    // Stereo is structural — channels != 2 refuses at load (see file_loader) —
    // so both channels always render. No channel gap: the 1972 Krips material
    // is effectively never unity, so the two channels' inner excursions do
    // not visually collide at the shared midline; a plain halve of the
    // inset region is clean. The two channels share the single inset band
    // (inset first, then split), so the inset_px band stays clear above
    // the top channel and below the bottom channel, with the channels
    // meeting at the inset region's vertical center.
    //
    // THE TWO BANDS ARE EXACTLY EQUAL AND THEY MEET FLUSH (architect
    // 2026-08-03, when the 1px channel-split line was retired): each channel
    // takes the halved-and-floored band height and the bottom one begins
    // immediately below the top one, so there is no row between them. At an odd
    // band height the spare row falls at the BOTTOM of the drawing band, inside
    // the symmetric inset where nothing draws — with no line on it, a spare row
    // between the channels would read as a one-pixel gap in the ink. The split
    // row comes from waveform_channel_split_row, which names where the bands
    // meet. Purely a vertical band offset: no column's frame span moves, so
    // plate column purity and both views' identity are untouched.
    const int split_row = waveform_channel_split_row(area_h, inset_px);
    if (split_row < 0) return;
    const int ch_h = split_row - cache_area.y;
    const GuiRect ch0{0, cache_area.y, cache_area.w, ch_h};
    const GuiRect ch1{0, split_row, cache_area.w, ch_h};
    // The full render IS the basis: global column 0 at the plate's own width.
    const WaveformBasis basis{vp_start, painter_spp, area_w};
    // ROW 6: the ink is the CROP's #1c816b, hard-coded (kWaveformInk). These two
    // calls were the `waveform_ink` key's only paint sites; the key stays
    // declared and stays in the grammar (the ruling is at the row-6 palette
    // block, render.h). Both channels take the one constant, as they took the
    // one global.
    render_waveform(dest, ch0, /*col0=*/0, audio, 0,
                    basis, kWaveformInk,
                    warp_frame_map_or_null);
    render_waveform(dest, ch1, /*col0=*/0, audio, 1,
                    basis, kWaveformInk,
                    warp_frame_map_or_null);
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
// tracked the prior inline block. It also snapshots the GUI thread's
// font-dependent geometry (area_w/area_h and inset_px = waveform_inset_px())
// so the worker render never reads the gui_scale state directly.

GuiPaintHandler::WaveformRenderInputs
GuiPaintHandler::compute_waveform_render_inputs() const {
    WaveformRenderInputs in;
    if (app.loading || audio.total_frames() <= 0) return in;

    const GuiRect area = waveform_area(app);
    if (area.w <= 0 || area.h <= 0) return in;

    const double  spp      = current_samples_per_pixel(app, audio);
    // The PAINTER's q — the lattice the viewport snaps onto and the renderer
    // maps columns on. Distinct from `spp` above, which only derives vp_end.
    const double  painter_q = painter_samples_per_pixel(app, audio, area);
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
    in.painter_spp   = painter_q;
    in.area_w        = area.w;
    in.area_h        = area.h;
    in.inset_px      = waveform_inset_px();
    in.is_target     = is_target;
    in.warp_frame_map_hash  = target_warp_frame_map_hash;
    in.warp_frame_map       = std::move(target_warp_frame_map);
    in.audio         = audio_source;
    in.valid         = true;
    return in;
}

void GuiPaintHandler::maybe_enqueue_waveform_render() {
    // Full dispatch freeze while the displayed basis is frozen — the DISPATCH
    // HALF of the two-gate freeze whose MEMBERSHIP has ONE owner,
    // displayed_basis_frozen (app_state.h, beside the basis owners): the
    // absolute painted-subject drags — marker and trim — PLUS, since
    // 2026-08-22, the two PENDING presses that aim them, because the freeze
    // contract's "the one job in flight at the grab" means the AIMED PRESS,
    // not the 8px crossing (the crossing converts the press's stored press_x,
    // so the epoch it was aimed in must survive until then — the derivation
    // is the predicate's). A gesture belongs iff it is an ABSOLUTE drag on a
    // PAINTED subject, reading the displayed basis per motion event, so that
    // publishing a new one mid-gesture would move that subject out from under
    // a stationary hand; the trim membership spans the endcaps, the bar AND —
    // since the region became the trim — the waveform overlay's own move and
    // bound drags, which hit the span on the plate basis through the
    // painter's region_columns and convert every motion column back on that
    // same basis. (Membership history, kept because each step was a ruling:
    // TWO active drags from 2026-08-18, THREE from 2026-08-15 while the
    // standing region's own editor `region_edit_drag` was a member in its own
    // right — its drags ARE the trim drags now; the target-view tempo drag
    // was a member for its own opposite reason until its 2026-07-29
    // deletion, see marker_drag.h.) The actives
    // freeze the displayed paint basis for the whole gesture (the
    // DragState "no per-drag map copy" contract), so no waveform job may be
    // DISPATCHED or PUBLISHED from the aimed press to the release:
    // on_waveform_render_done's
    // completion-drop gate is the publication half, and this is the dispatch
    // half. Freezing the whole enqueue (not just the warp_frame_map
    // hash the former drag_freeze excluded) closes the
    // drop-rewind-redispatch loop: a
    // job for a viewport-follow / resize fingerprint dispatched just before the
    // aimed press used to be dropped, rewound, then re-dispatched every tick
    // because
    // the vp/area fields still differed — wasted full renders all gesture long.
    // Nothing that legitimately re-renders can occur inside the freeze anyway:
    // keys and
    // wheels are gesture-gated (the drag-modal gate and wheel_context both
    // cover the pendings), the follow chase is paused for any live pointer
    // gesture (any_pointer_gesture_active, whose members include all of
    // these), and a compositor resize FORCE-ENDS the gestures and disarms the
    // pendings (finalize_active_drags) before catching
    // up at the first post-gesture tick. With no in-freeze dispatch the
    // completion drop fires AT MOST ONCE (the one job in flight at the aimed
    // press).
    // THE DELIBERATE NON-MEMBERS are the predicate's to enumerate; the short
    // form: the strip drag and the
    // grab-pan drive their own SYNCHRONOUS per-frame renders (kick_waveform_sync,
    // which drains this worker rather than queuing behind it) and must keep
    // rendering; the OVERVIEW lane's drags act on the whole-song lane rather
    // than on the plate; and THE SWEEP (region_drag) writes the trim from a
    // FIXED anchor to the live pointer, so there is no grabbed subject for a
    // basis swap to slide, only the ordinary one-epoch lag every painted
    // overlay carries.
    if (displayed_basis_frozen(app))
        return;

    WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) return;

    // fp_inset is the waveform inset the compared pixels were laid out under —
    // the plate's one geometry input that is not an area dimension, and the area
    // dims move with the strip heights only because the lanes do, so it is keyed
    // directly and the detect is sound by field rather than by that derivation.
    // (It keyed the measured MONOSPACE font size until row 7, as a proxy for
    // this same inset back when the inset was font-derived.)
    auto fingerprint_differs = [&](
        int64_t fp_vp_s, int64_t fp_vp_e,
        int     fp_aw,   int     fp_ah,
        int     fp_inset,
        bool    fp_t,
        uint64_t fp_h) -> bool {
        if (fp_vp_s != in.vp_start)        return true;
        if (fp_vp_e != in.vp_end)          return true;
        if (fp_aw   != in.area_w)          return true;
        if (fp_ah   != in.area_h)          return true;
        if (fp_inset != in.inset_px)       return true;
        if (fp_t    != in.is_target)       return true;
        if (fp_h    != in.warp_frame_map_hash) return true;
        return false;
    };

    const bool diff_vs_pending = fingerprint_differs(
        wf_cache.pending_fp_vp_start,
        wf_cache.pending_fp_vp_end,
        wf_cache.pending_fp_area_w,
        wf_cache.pending_fp_area_h,
        wf_cache.pending_fp_inset_px,
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
        wf_cache.supersede_painter_spp = in.painter_spp;
        wf_cache.supersede_area_w      = in.area_w;
        wf_cache.supersede_area_h      = in.area_h;
        wf_cache.supersede_inset_px    = in.inset_px;
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
    job.painter_spp    = in.painter_spp;
    job.area_w         = in.area_w;
    job.area_h         = in.area_h;
    job.inset_px       = in.inset_px;
    job.target         = in.is_target;
    job.warp_frame_map_hash   = in.warp_frame_map_hash;
    // Stash a copy of the warp_frame_map on the pending slot so the flag cache —
    // its one reader — can consume it at completion-swap time. The job takes
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
    wf_cache.pending_fp_inset_px = in.inset_px;
    wf_cache.pending_fp_target      = in.is_target;
    wf_cache.pending_fp_warp_frame_map_hash = in.warp_frame_map_hash;

    waveform_worker.dispatch(std::move(job),
        [this](bool ok) { on_waveform_render_done(ok); });
}

void GuiPaintHandler::on_waveform_render_done(bool ok) {
    // Gesture-discard gate, the PUBLICATION half of the dispatch freeze above,
    // over the SAME membership through the SAME one owner —
    // displayed_basis_frozen, app_state.h (do not re-list the members here).
    // The actives
    // freeze the displayed paint
    // basis for the whole gesture (the DragState "no per-drag map copy"
    // contract). maybe_enqueue_waveform_render's full dispatch freeze keeps a
    // NEW map edit from being DISPATCHED inside the freeze, but a job
    // dispatched (or
    // parked in the supersede slot) BEFORE the aimed press would still publish
    // its map HERE — the displayed basis would jump under a stationary pointer.
    // For an ACTIVE drag every motion event re-reads it (apply_drag_motion, the
    // trim drags, the
    // nudges, and the trim drags' own column conversions — the waveform
    // overlay's move drag is the plainest case: nothing moves, and the span
    // slides); for a PENDING press (2026-08-22, the freeze's press-time start)
    // the crossing that converts the stored press_x would interpret a press
    // aimed in the OLD painted epoch through the NEWLY published one, and the
    // first motion's delta would be wrong by the two epochs' difference —
    // dropping here is what makes the press-time aim survive to the
    // crossing. So drop the
    // completed job WHOLESALE: no surface swap, no fp_*
    // publish, no item-cache stage, and CLEAR (never dispatch) the supersede
    // slot. Renders are repeatable — rewind pending_fp_* to the still-displayed
    // fp_* so the pending fingerprint again describes what is on screen; once the
    // gesture ends — the release (a pending's motionless lift and its click act
    // included) or a force-end disarm — and the dispatch freeze reopens, the
    // next
    // maybe_enqueue_waveform_render compares the current store's desired
    // fingerprint against that (== the displayed plate) and re-renders IFF the
    // plate is stale. Both a committed move (the store hash advanced) and a
    // no-op drag that left an EARLIER pending map edit unpublished (the desired
    // hash still differs from the displayed one) re-detect correctly; a
    // genuinely up-to-date plate stays put. Because the dispatch freeze enqueues
    // NOTHING inside the freeze, this drop fires AT MOST ONCE — for the single
    // job in
    // flight at the aimed press; there is no drop-rewind-redispatch loop to
    // sustain.
    // The non-members are the predicate's own, for the dispatch gate's own
    // reasons. (The TEMPO drag was on this gate too, joining the drop
    // for its own reason — it re-warped synchronously per cent step, so a pre-grab
    // async job publishing here would have painted a stale plate over the
    // step-fresh one — until its 2026-07-29 deletion; see marker_drag.h.)
    if (displayed_basis_frozen(app)) {
        wf_cache.supersede = false;
        wf_cache.supersede_warp_frame_map.clear();
        wf_cache.pending_fp_vp_start            = wf_cache.fp_vp_start;
        wf_cache.pending_fp_vp_end              = wf_cache.fp_vp_end;
        wf_cache.pending_fp_area_w              = wf_cache.fp_area_w;
        wf_cache.pending_fp_area_h              = wf_cache.fp_area_h;
        wf_cache.pending_fp_inset_px            = wf_cache.fp_inset_px;
        wf_cache.pending_fp_target              = wf_cache.fp_target;
        wf_cache.pending_fp_warp_frame_map_hash = wf_cache.fp_warp_frame_map_hash;
        wf_cache.pending_fp_warp_frame_map      = wf_cache.fp_warp_frame_map;
        return;
    }

    if (!ok) {
        std::fprintf(stderr,
            "warptempo_gui: Waveform worker reported failure; will retry "
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
        job.painter_spp    = wf_cache.supersede_painter_spp;
        job.area_w         = sw;
        job.area_h         = sh;
        job.inset_px       = wf_cache.supersede_inset_px;
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
        wf_cache.pending_fp_inset_px = wf_cache.supersede_inset_px;
        wf_cache.pending_fp_target      = wf_cache.supersede_target;
        wf_cache.pending_fp_warp_frame_map_hash = wf_cache.supersede_warp_frame_map_hash;

        wf_cache.supersede = false;
        wf_cache.supersede_warp_frame_map.clear();

        waveform_worker.dispatch(std::move(job),
            [this](bool ok2) { on_waveform_render_done(ok2); });
        return;
    }

    // Swap the pending surface into the live slot. Cairo surface ownership
    // transfers cleanly via pointer swap; no flush needed because the worker's
    // render already committed the surface fully — its CLEAR context was
    // destroyed and render_waveform's direct pixel writes end in a
    // cairo_surface_mark_dirty, so the buffer and cairo agree before the swap.
    std::swap(wf_cache.surface,        wf_cache.pending_surface);
    std::swap(wf_cache.width,          wf_cache.pending_width);
    std::swap(wf_cache.height,         wf_cache.pending_height);

    wf_cache.fp_vp_start     = wf_cache.pending_fp_vp_start;
    wf_cache.fp_vp_end       = wf_cache.pending_fp_vp_end;
    wf_cache.fp_area_w       = wf_cache.pending_fp_area_w;
    wf_cache.fp_area_h       = wf_cache.pending_fp_area_h;
    wf_cache.fp_inset_px = wf_cache.pending_fp_inset_px;
    wf_cache.fp_rendered     = true;
    wf_cache.fp_target       = wf_cache.pending_fp_target;
    wf_cache.fp_warp_frame_map_hash = wf_cache.pending_fp_warp_frame_map_hash;
    // Publish the in-flight job's warp_frame_map to the displayed slot
    // so the next maybe_rebuild_flag_cache reads the same coordinate
    // system the just-blitted waveform pixels were rendered against.
    std::swap(wf_cache.fp_warp_frame_map,     wf_cache.pending_fp_warp_frame_map);

    // Rebuild the flag cache INLINE now, against the fingerprint just published
    // — the same shape the synchronous-rebuild path already
    // uses (force_synchronous_waveform_rebuild). The run
    // loop can service the wl_display fd (the frame callback that PAINTS) before
    // the timerfd tick that runs the on_tick dirty-check, so deferring the
    // flag rebuild to the tick let a frame blit the NEW plate over an OLD
    // flag cache — and EVERY plate-registered overlay, which by definition reads
    // the NEW fp_* through GuiPaintHandler::plate_viewport_basis (its
    // declaration in paint_handler.h enumerates which overlays those are; the
    // hazard here is the whole class, not any member of it), visibly left its
    // flags for one frame during a
    // follow-scroll / resize / drift-catchup publish. Doing the rebuild here makes
    // the committing frame blit new plate + new items together and promote the
    // staged basis atomically. The two-phase stage/promote ruling is UNCHANGED:
    // the rebuild STAGES the displayed hit map (app.staged_displayed_*), and
    // on_redraw still PROMOTES it at the committing frame; the on_tick rebuild
    // remains the idempotent fingerprint-guarded backstop. This also removes the
    // one-frame item lag every worker publish had. (The live trim pass needs no
    // rebuild — it reads the promoted item basis per frame.)
    maybe_rebuild_flag_cache();

    // Invalidate the waveform area so the next paint blits the new
    // pixels. Matches the rect Viewport::invalidate_waveform_area uses — which
    // CONTAINS THE OVERVIEW LANE since the relayout's commit B moved the strip
    // into the centered block, so the dedicated overview rider that stood here
    // is deleted with that owner's (the rationale lives there). This is the
    // ASYNC publish (follow scroll, resize, drift catch-up), whose viewport
    // moved undriven, and the lane's box owes the same frame — it simply gets it
    // from the one rect now.
    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
}

// -- Synchronous waveform rebuild (THE user-driven render route) ---------
//
// Every frame of every user-driven viewport change routes through here: pans
// and scrolls, the strip drag's zoom frames, the one-shot jumps (Tab /
// Shift+Tab / Ctrl+Shift+Tab, Home / End, center-on-playhead), the A/B and
// source/target switches, undo / redo, and the map edits. Leaving any of them
// to the async worker rebuilds the waveform one frame late, so the same-tick
// flag rebuild keys off the lagging wf_cache.fp_* and the overlays — the
// selection rectangle on a newly focused marker, say — blink across the worker
// window. Rendering + publishing the fp here makes the flag cache converge
// against the final viewport this tick.
//
// Writing into wf_cache.surface directly (not pending_surface + swap) is
// safe only because wait_until_idle() ran first — the worker is Idle and
// holds no reference to the live surface. Do not reorder the drain after
// the render.

// Synchronous-repaint rule (the waveform-layer coherence invariant):
//
// The waveform plate and the marker / playhead / trim / flag overlays are
// separate paint layers. The overlays are computed inline from live state and
// paint on the next frame; the plate is the expensive layer. If a one-shot
// state change updates the overlays inline but defers the plate to the async
// worker, the overlays jump to their new positions one or two frames before the
// plate catches up — a cross-layer desync that surfaced as zoom lag, the A/B-tab
// and Tab recenter jump, and the source/target toggle smear.
//
// The rule, realized two ways — render the correct frame before painting:
//   1. EVERY user-driven viewport/view change renders synchronously, through
//      this function. The jumps this governs: zoom, center-on-playhead, the
//      viewport-shift playhead moves (Home / End and navigate-to-marker), the
//      A/B tab switch, the source/target toggle, undo / redo — AND ALL PANNING
//      (touchpad scroll, the plain wheel's stepped pan, PageUp/PageDown, the
//      plain-drag grab-pan).
//      They arrive at a bounded rate: pointer detents
//      coalesce to one action per pointer frame, and key repeat is compositor-
//      throttled, so a full inline render per event is affordable. The pyramid
//      bounds per-column cost unconditionally, in both views (the bound and its
//      proof live at GuiAudio::level_for_span), so the render is O(area_width)
//      at any zoom level.
//
//      PAN JOINED THIS ROUTE (architect 2026-07-26): "i prefer smooth movement
//      (ie, no special handling for during movement and at-standstill — ableton
//      looks identical in both) over theoretical accuracy." The incremental
//      shift-and-strip pan is retired with its memmove, its edge strip, and its
//      boundary-column bridge repair; a pan is now just another full render, so
//      moving and resting plates are produced by one code path and cannot
//      disagree. The performance license is precedent plus the step-2 writer:
//      the strip-drag ZOOM already took a full synchronous rebuild per pointer
//      frame at this exact cost class, and the direct ARGB32 column writer
//      replaced the per-column cairo rect-fill, so pan now pays what zoom
//      always paid. (The older claim here — that the touchpad's high-rate
//      stream outruns a full-render-per-event model — is superseded by that
//      ruling; if the torrent ever does outrun it, the answer is coalescing at
//      the input edge, not a second rendering path.)
//   2. The async worker (maybe_enqueue_waveform_render) is the backstop for
//      changes the user is not actively driving: resize, the launch file
//      load, follow_scroll_if_needed during playback, and the on_tick safety
//      net that catches residual fingerprint drift. The marker and trim
//      drags freeze this worker's dispatch for the gesture's whole
//      duration (the full-enqueue gate at maybe_enqueue_waveform_render, with
//      on_waveform_render_done dropping the at-most-one job already in flight
//      at the grab — see those two functions' own comments for the mechanics).
//      The trim drag runs in either view and never touches the map, so
//      freezing it costs no re-warp. The marker (reposition) drag runs only
//      in its column's home view (W+source / P+target), likewise
//      map-independent there — and off that home there is no marker pointer
//      gesture at all any more: W+target's plain flag drag was the TEMPO drag,
//      which DID re-warp synchronously PER CENT STEP and so kept the worker
//      frozen for the opposite reason, and it is deleted (2026-07-29, see
//      marker_drag.h). No pointer gesture re-warps the plate now.
//
// This is still NOT "make everything synchronous." Async earns its keep for
// UNDRIVEN and playback-adjacent changes — resize, the launch load, follow
// scrolling, the tick's drift net — where no gesture is waiting on the frame.
// What is synchronous is everything the user is actively driving, pan included;
// the rule is that a user-driven change must not paint its overlays against a
// stale plate.
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
        in.area_w, in.area_h, in.inset_px,
        *in.audio,
        in.vp_start, in.painter_spp,
        in.warp_frame_map.empty() ? nullptr : &in.warp_frame_map);

    // Publish the displayed fingerprint NOW so the flag rebuild at
    // the tail of this function reads the current viewport. Keep pending_fp_*
    // in lockstep so the next maybe_enqueue_waveform_render sees no diff and
    // does not re-dispatch the same target on the worker.
    wf_cache.fp_vp_start     = in.vp_start;
    wf_cache.fp_vp_end       = in.vp_end;
    wf_cache.fp_area_w       = in.area_w;
    wf_cache.fp_area_h       = in.area_h;
    wf_cache.fp_inset_px = in.inset_px;
    wf_cache.fp_rendered     = true;
    wf_cache.fp_target       = in.is_target;
    wf_cache.fp_warp_frame_map_hash = in.warp_frame_map_hash;
    wf_cache.fp_warp_frame_map      = in.warp_frame_map;
    wf_cache.pending_fp_vp_start     = in.vp_start;
    wf_cache.pending_fp_vp_end       = in.vp_end;
    wf_cache.pending_fp_area_w       = in.area_w;
    wf_cache.pending_fp_area_h       = in.area_h;
    wf_cache.pending_fp_inset_px = in.inset_px;
    wf_cache.pending_fp_target       = in.is_target;
    wf_cache.pending_fp_warp_frame_map_hash = in.warp_frame_map_hash;
    wf_cache.pending_fp_warp_frame_map      = in.warp_frame_map;

    const GuiRect a = waveform_area(app);
    // ONE RECT, and it carries the OVERVIEW LANE: several kick_waveform_sync
    // tails — undo, the load-in-places, the tempo step — reach this route
    // without passing Viewport::invalidate_waveform_area, and each may have
    // moved the viewport, the domain or the map the lane's box reads. Since the
    // relayout's commit B put the lane inside the centered block, this rect
    // (window top through the waveform's bottom) contains it by construction and
    // the explicit rider that stood here is deleted — the record is at that
    // owner.
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
    // same-frame consistency EVERY kick_waveform_sync caller expects. That
    // caller set is not enumerated here: the ONE authoritative inventory lives
    // at Viewport::kick_waveform_sync's declaration (viewport.h), and a second
    // copy here is exactly the drift the one-site rule exists to prevent (the
    // copy this replaces had gained a member with no code site). The rebuild
    // also serves a caller whose plate does NOT move — the `p` W/P column
    // toggle, whose fingerprint change is the flag cache's alone — which is
    // why it runs unconditionally at this tail rather than under a
    // plate-changed guard. The rebuild is fingerprint-guarded, so it is a cheap
    // no-op when the cache already matches. It also stages the
    // event-sync displayed hit map (promoted when on_redraw commits the
    // rebuild — the two-phase commit, ruling at the selector); running it
    // here closes the hit-test staleness window that otherwise lasts until the
    // next tick.
    maybe_rebuild_flag_cache();
}

// -- Flag-cache fingerprint hashes ---------------------------------------

namespace {

// FNV-1a over the live drag-overlay state, folded into the FlagCache
// fingerprint (its only consumer). Hashing the drag state directly removes the
// requirement that every mutation site of app.drag remember to bump a generation
// counter. The loops run over ONE slot (a drag moves one marker — groups are never
// moved), so the whole hash is a handful of nanoseconds.
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
// generation-bump across every mutation site of selected_markers.
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
// reads for the marker-driven half. (THE GEOMETRY HALF IS GONE — it keyed the
// measured monospace grid every flag dimension used to derive from; row 5 moved
// those onto the gui_scale axis, leaving the field a recorded vestige, and row 7
// deleted the measurement itself.) The cache holds
// EVERY flag shape (marker + phase reset) EXCEPT THE EDITING TARGET'S, whose box
// and hit rect are skipped here because the live editor overlay painted after
// this blit cannot be assumed to cover them (2026-08-02) — which is why the
// edited marker is a fingerprint field of its own below. Trim's pixels left this cache and
// the retired trim-stem cache for the live paint_trim pass, so no trim field
// remains in the fingerprint (a trim edit repaints through its own mutation
// damage, no cache rebuild).

// ONE DIFF FLAG'S LABEL, both columns, both sides — and, since 2026-08-05, the
// bottom strip's `Scale:` segment too. The contract and the shapes it produces
// are at the declaration (paint_handler.h); what the body says is the whole
// spelling: THE SIGN, THEN THE PAYLOAD DIRECTLY AGAINST IT.
//
// NO SPACE AFTER THE BRACKET (architect 2026-08-05, superseding the arc's
// original `[+] <payload>`), which is why the empty-payload case needs no arm of
// its own any more: a phase reset with its bit clear appends nothing and rests
// at the bare sign.
std::string history_diff_label(const char* sign, bool disabled,
                               const std::string& token) {
    std::string out(sign);
    if (disabled) out += '#';
    out += token;
    return out;
}

void GuiPaintHandler::rebuild_history_diff_flags() {
    std::vector<HistoryDiffFlag>& out = app.history_mode.flags;
    out.clear();

    // THE DISPLAYED DELTA IS THE SESSION'S (SOURCE, READING) PAIR, through the
    // one accessor that forks on it (AppState::HistoryMode::displayed_delta):
    // the COMMIT walk or the LOCAL one, read iteratively forward against the
    // next-newer item or cumulatively against the frozen live now side. The
    // lane's shapes, colours and text are identical in all four — green is the
    // newer side everywhere — so nothing below this line knows which walk or
    // which reading it is drawing, nor that the two readings coincide at the
    // newest index of either.
    const GuiHistoryCommitDelta* d =
        app.history_mode.displayed_delta(app.history_compare());
    if (!d) return;

    // THE ACTIVE MARKERS VIEW PICKS THE COLUMN, exactly as it picks which store
    // the live lane walks: warp entries where warp flags paint, phase-reset
    // entries where phase resets paint. The other column's delta is not shown —
    // one lane, one column, and the mode is a view onto that lane.
    // THE THEN SIDE'S VALUE RIDES ALONG WITH ITS LABEL (2026-08-05) on every
    // flag that HAS a then side — the removed ones and the changed pairs — for
    // the REVERT act, which restores exactly that value (the fields' contract is
    // at HistoryDiffFlag, render.h). The label and the value come off the SAME
    // delta entry here, so the flag cannot show one thing and restore another.
    //
    // THE DISABLED AXIS IS FILLED PER HALF, TEXT AND FACE APART (architect
    // 2026-08-22, the cascade deepening the axis the same day it landed): the
    // LABEL's '#' composes from each side's VERBATIM LOCAL bit and the revert's
    // `then_disabled` carries that same byte, while the PAINT bits —
    // `then_effective_disabled` on every flag with a REMOVED half,
    // `now_effective_disabled` on every flag with an ADDED one — take the
    // delta's per-side EFFECTIVE verdicts (the cascade resolved within each
    // side's own commit; on the phase column local IS effective, no cascade
    // existing there). Text, revert byte and dim all come off the SAME delta
    // entry, so they can never describe different lines — they simply answer
    // the line's two different questions, its bytes and its live-lane face. A
    // half a flag does not
    // have leaves its bits at the struct's false, which no painter or act reads:
    // each is meaningful exactly when its own half's bool is set.
    if (app.active_markers_view == 'P') {
        for (const GuiHistoryPhaseResetChange& c : d->phase_reset_changed) {
            HistoryDiffFlag f;
            f.time_frame   = c.frame;
            f.removed      = true;
            f.added        = true;
            f.removed_text = history_diff_label("[-]", c.then_disabled, {});
            f.added_text   = history_diff_label("[+]", c.now_disabled, {});
            f.then_disabled           = c.then_disabled;
            f.then_effective_disabled = c.then_disabled;
            f.now_effective_disabled  = c.now_disabled;
            f.then_measure  = c.then_measure;
            out.push_back(std::move(f));
        }
        for (const GuiHistoryPhaseResetEntry& e : d->phase_reset_removed) {
            HistoryDiffFlag f;
            f.time_frame   = e.frame;
            f.removed      = true;
            f.removed_text = history_diff_label("[-]", e.disabled, {});
            f.then_disabled           = e.disabled;
            f.then_effective_disabled = e.disabled;
            f.then_measure  = e.measure;
            out.push_back(std::move(f));
        }
        for (const GuiHistoryPhaseResetEntry& e : d->phase_reset_added) {
            HistoryDiffFlag f;
            f.time_frame = e.frame;
            f.added      = true;
            f.added_text = history_diff_label("[+]", e.disabled, {});
            f.now_effective_disabled = e.disabled;
            out.push_back(std::move(f));
        }
    } else {
        for (const GuiHistoryWarpChange& c : d->warp_changed) {
            HistoryDiffFlag f;
            f.time_frame   = c.frame;
            f.removed      = true;
            f.added        = true;
            f.removed_text =
                history_diff_label("[-]", c.then_disabled, c.then_tempo_token);
            f.added_text =
                history_diff_label("[+]", c.now_disabled, c.now_tempo_token);
            f.then_token    = c.then_tempo_token;
            f.then_disabled           = c.then_disabled;
            f.then_effective_disabled = c.then_effective_disabled;
            f.now_effective_disabled  = c.now_effective_disabled;
            out.push_back(std::move(f));
        }
        for (const GuiHistoryWarpEntry& e : d->warp_removed) {
            HistoryDiffFlag f;
            f.time_frame   = e.frame;
            f.removed      = true;
            f.removed_text =
                history_diff_label("[-]", e.disabled, e.tempo_token);
            f.then_token    = e.tempo_token;
            f.then_disabled           = e.disabled;
            f.then_effective_disabled = e.effective_disabled;
            out.push_back(std::move(f));
        }
        for (const GuiHistoryWarpEntry& e : d->warp_added) {
            HistoryDiffFlag f;
            f.time_frame = e.frame;
            f.added      = true;
            f.added_text = history_diff_label("[+]", e.disabled, e.tempo_token);
            f.now_effective_disabled = e.effective_disabled;
            out.push_back(std::move(f));
        }
    }

    // PAINTED IN FRAME ORDER, which is the live lane's own reading order — its
    // stores are time-ordered, so its flags run left to right and a later box
    // covers an earlier one's tail. The three groups above arrive interleaved by
    // frame otherwise, which would scatter the occlusion. The sort is STABLE, so
    // coincident entries keep the group order they were built in — changed, then
    // removed, then added — and the ADDED flag, the state the session actually
    // holds, is the one on top.
    std::stable_sort(out.begin(), out.end(),
                     [](const HistoryDiffFlag& a, const HistoryDiffFlag& b) {
                         return a.time_frame < b.time_frame;
                     });
}

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
    // The flags carry TEXT since row 5, and iteration mode changes what that
    // text says (flag_text_iter splices the bracket). See fp_iteration_mode.
    const bool      iter_on    = app.iteration_mode_enabled;
    // THE EDITED MARKER (or -1): its flag box is suppressed below, so it is a
    // fingerprint input like any other content fact. Resolved with the SAME two
    // gates render_flag_editor_box opens on — an active editor of kind
    // FlagPayload — so the pass that skips and the pass that draws can never
    // disagree about which marker is being edited.
    const int editing_flag_target =
        (text_editor::is_active(app.top_flag_editor) &&
         app.top_flag_editor.kind == text_editor::Kind::FlagPayload)
            ? app.top_flag_editor.target : -1;
    // THE MARKER WHOSE MEASURE EDITOR IS OPEN (or -1), resolved with the same
    // two gates the MeasureText paint arm opens on. A SECOND field rather than
    // a kind flag beside the one above, and that is what makes the fingerprint
    // distinguish KIND AND TARGET at once: a payload session on i reads
    // (i, -1), a measure session on i reads (-1, i), and the two suppressions
    // this pass applies are exactly these two numbers. The COLUMN is already a
    // fingerprint field (fp_active_markers_view), which is what makes one index
    // enough for a suppression both columns take.
    const int editing_measure_target =
        (text_editor::is_active(app.top_flag_editor) &&
         app.top_flag_editor.kind == text_editor::Kind::MeasureText)
            ? app.top_flag_editor.target : -1;

    // THE HISTORY MODE'S EIGHT INPUTS (contract at the FlagCache fields). The
    // GENERATION is the one that is not about the shown commit but about WHICH
    // SESSION is showing it: two visits open in the same shape and a close plus
    // a reopen can reach this check as one edge, so without it the new session's
    // lane would keep blitting the old session's flags. THE COMPARE READING is
    // the one that is about the shown commit but not about WHICH commit: one
    // index has two deltas, and a switch moves no other field here. THE
    // SELECTION HASH is the mode's own membership, through the LIVE lane's own
    // hash owner rather than a second one — the focus rides into it as that
    // function's second term, which is redundant beside the field above and
    // costs nothing, and keeping one hash for both selection-shaped inputs is
    // what that redundancy buys.
    const bool               history_active = app.history_mode.active;
    const std::size_t        history_index  = app.history_mode.index;
    const int                history_focus  = app.history_mode.focus;
    const unsigned long long history_generation = app.history_mode.generation;
    // The READING is the session's own bit since 2026-08-08 (it moved off
    // HistoryMode onto AppState so a mode edge cannot reset it); the fingerprint
    // term is unchanged in value space — the same two-valued reading, read
    // through the one mapping owner.
    const GuiHistoryCompare  history_compare    = app.history_compare();
    const uint64_t           history_sel_hash   = hash_selection(
                                 app.history_mode.selection,
                                 app.history_mode.focus);
    // THE WALK'S SIZE (2026-08-07): the prefetch streams members in while the
    // view stands, and the arrival of member 0 into an empty walk changes the
    // lane's whole content while moving no other field above it.
    const std::size_t        history_count      =
        app.history_mode.session.commit_count();
    // THE WALK SOURCE AND THE LOCAL POSITION (2026-08-07). The
    // source is what makes a tab switch across the two walks repaint at all —
    // index, compare, focus and generation can every one of them be unchanged
    // across it — and the local index is the other walk's own `,` / `.`, which
    // moves no field above either. THE LOCAL WALK'S SIZE NEEDS NO FIELD: it is
    // built from the two undo stacks' sizes, captured at the mode's entry and
    // frozen for the visit (GuiHistoryLocalWalk's premise), so it cannot move
    // while this cache lives.
    const GuiHistoryWalkSource history_source = app.history_mode.source;
    const std::size_t        history_local_index = app.history_mode.local_index;

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
        flag_cache.fp_iteration_mode          == iter_on &&
        flag_cache.fp_editing_flag_target     == editing_flag_target &&
        flag_cache.fp_editing_measure_target  == editing_measure_target &&
        flag_cache.fp_history_active          == history_active &&
        flag_cache.fp_history_index           == history_index &&
        flag_cache.fp_history_focus           == history_focus &&
        flag_cache.fp_history_generation      == history_generation &&
        flag_cache.fp_history_compare         == history_compare &&
        flag_cache.fp_history_selection_hash  == history_sel_hash &&
        flag_cache.fp_history_commit_count    == history_count &&
        flag_cache.fp_history_source          == history_source &&
        flag_cache.fp_history_local_index     == history_local_index;

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
    // The flag/triangle LANE rects the shapes occupy, straight from the lane
    // accessors — the same bands the empty-lane press gate and the hit rects
    // read. The top strip is anchored at screen y=0 and this surface mirrors it
    // 1:1, so the screen-coordinate lane rects are already surface-local.
    const FlagLaneRects flag_lanes{top_marker_row_area(app)};
    // The width the flag column mapping divides the displayed span by — the same
    // denominator the live trim pass and the hit tests use (this pass stages it
    // for them at the tail), so flags stay column-aligned with the trim/stem
    // verticals below them. The surface stays full-strip width; a
    // non-multiple-of-16 window leaves the gutter columns unpainted.
    //
    // IT IS THE PLATE'S OWN WIDTH, NOT THE LIVE ONE (2026-08-01, closing a
    // resize-window basis split). The numerator here is the DISPLAYED span
    // (fp_vp_end - fp_vp_start); dividing it by the LIVE effective width mixed
    // two epochs, so during an async plate publish after a resize the flags,
    // their stems, their hit rects and the trim geometry were mapped at a
    // samples-per-pixel the blitted ink did not have — while the playhead, the
    // region columns and the ruler, which all read plate_viewport_basis
    // ((fp_vp_end - fp_vp_start)/fp_area_w), had it right. The plate is blitted
    // 1:1 at its own scale, never stretched, so an overlay that wants to sit on
    // its ink must use the width that ink was rendered at. Both bases are now
    // the one expression, and the fingerprint still catches every width change
    // transitively: fp_vp_end = vp_start + nearbyint(spp·w) and the effective
    // width moves in steps of 16 at spp >= 27.5 frames/px, so no width change
    // can leave the displayed span untouched.
    //
    // The live-width fallback mirrors plate_viewport_basis's own cold arm: the
    // fp_rendered gate above makes it unreachable in practice (a plate that has
    // published rendered at a positive width), and it costs one compare.
    const int wave_w = wf_cache.fp_area_w > 0 ? wf_cache.fp_area_w
                                              : waveform_area(app).w;
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

    // (No trim pass here: trim is painted LIVE, every pixel of it, inside its
    // own lane (GuiPaintHandler::paint_trim over top_trim_row_area), and this
    // cache holds the marker and phase-reset FLAG shapes. Two owners over two
    // disjoint bands — nothing to arbitrate between them.)

    // Red-flag sets: the marker indices whose render normalizes to the 1.00
    // fallback, painted the hard-coded kMarkerFlagFillRed/kMarkerFlagEdgeRed
    // pair with the kMarkerStemRed stem whatever their selection state
    // (resolve_flag_face — a disabled red marker blends that same pair toward
    // the lane ground and stays recognisably red).
    // Read from the memoized caches (keyed on the respective store
    // generation), so the silent classification runs only on a marker change,
    // not on this per-tick rebuild; the committed store means a red flag
    // freezes through a marker drag and re-evaluates at commit. The active
    // view supplies only its own column's set.
    //
    // THIS IS THE SOLE PRODUCER of the marker painter's stash (app.flag_hit_rects
    // / app.marker_stems, contract at their declaration): the boxes' widths are
    // derived from shaped labels, so the pass that draws them is the only one
    // that can report them. The active view supplies its own column's stash and
    // the other column's is not retained — hit tests and the stem pass are both
    // active-column-only.
    if (history_active) {
        // THE HISTORY MODE OWNS THE LANE WHOLE (AppState::HistoryMode): no live
        // marker paints. (The lane is not the whole of that suppression — the
        // phase-reset lead-in RING is a live-marker surface in the WAVEFORM, and
        // it is gated at its own visibility owner, phase_reset_overlay_band in
        // paint_handler.cpp, since 2026-08-05.) This arm becomes the producer of the same two
        // stashes the two marker arms below produce — with `marker_index`
        // carrying an index into app.history_mode.flags rather than into a
        // store, which is what lets hit_test_flag serve the mode's focus click
        // unchanged.
        rebuild_history_diff_flags();
        render_history_diff_flags(
            ccr, local_top_strip, flag_lanes, wave_w,
            app.history_mode.flags,
            vp_start, vp_end,
            history_focus,
            app.history_mode.selection,
            &app.flag_hit_rects,
            &app.marker_stems,
            // THE SAME MAP ARGUMENT the live columns take — a diff flag's frame
            // is an authored SOURCE frame exactly as a marker's is, in both
            // stores, so target view translates it through the same segments and
            // a removed marker lands on the column a live one at that frame
            // would.
            tmap_arg);
    } else if (mv == 'P') {
        const std::set<int>& pr_red =
            phase_reset_red_flag_set_cached(app).red;
        render_phase_reset_flags(
            ccr, local_top_strip, flag_lanes, wave_w,
            app.phaseresetmarkers.markers(),
            vp_start, vp_end, sr,
            app.selected_markers,
            pr_red,
            &app.flag_hit_rects,
            &app.marker_stems,
            tmap_arg,
            drag_overlay,
            editing_measure_target);
    } else {
        const std::set<int>& warp_red = warp_red_flag_set_cached(
            app, sr, static_cast<long>(audio.total_frames())).red;
        // (The two editing targets reach this call as its last two arguments —
        // the edited marker's box, and the measured marker's measure box, are
        // the open editor's to paint, not this pass's.)
        render_flags(ccr, local_top_strip, flag_lanes, wave_w,
                     app.warpmarkers.markers(),
                     vp_start, vp_end, sr,
                     app.selected_markers,
                     warp_red,
                     iter_on,
                     &app.flag_hit_rects,
                     &app.marker_stems,
                     tmap_arg,
                     drag_overlay,
                     editing_flag_target,
                     editing_measure_target);
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
    flag_cache.fp_iteration_mode          = iter_on;
    flag_cache.fp_editing_flag_target     = editing_flag_target;
    flag_cache.fp_editing_measure_target  = editing_measure_target;
    flag_cache.fp_history_active          = history_active;
    flag_cache.fp_history_index           = history_index;
    flag_cache.fp_history_focus           = history_focus;
    flag_cache.fp_history_generation      = history_generation;
    flag_cache.fp_history_compare         = history_compare;
    flag_cache.fp_history_selection_hash  = history_sel_hash;
    flag_cache.fp_history_commit_count    = history_count;
    flag_cache.fp_history_source          = history_source;
    flag_cache.fp_history_local_index     = history_local_index;

    // Event-synchronized hit geometry, STAGE phase: these OFFSCREEN flags just
    // rebuilt, so stage the
    // map they baked (target view) or a clear (source view); on_redraw promotes
    // it at the frame that blits this cache. THIS IS THE SOLE ITEM-BASIS STAGE
    // SITE (grep app.staged_displayed_valid = true): the retired trim-stem
    // cache's rebuild used to stage the same value in the same tick, and
    // deleting that duplicate is safe exactly because THIS rebuild remains the
    // stage owner on every viewport/map/dimension change — every one of those
    // changes moves a field of this cache's fingerprint (fp_vp span, map hash,
    // target bit, top-strip dims, measured font px; wave_w is the PLATE's width
    // now and cannot move without moving the fp_vp span with it — the
    // derivation is at wave_w — and the window width moves surface_w besides),
    // so the rebuild fires and re-stages. A trim-only
    // change no longer stages anything, which is correct: trim never entered
    // the staged basis values, and the live trim pass reads the promoted basis
    // per frame. Ruling at the selector.
    if (is_target)
        app.staged_displayed_target_warp_frame_map = wf_cache.fp_warp_frame_map;
    else
        app.staged_displayed_target_warp_frame_map.clear();
    // Stage the displayed VIEWPORT alongside the map — the same fp_vp span the
    // flags just mapped through, over the same width they were column-mapped
    // against. THAT WIDTH IS NOW THE PLATE'S (wave_w == fp_area_w since
    // 2026-08-01 — the derivation is at wave_w above), so the item basis, the
    // plate basis and the flag pass are one {span, width} pair instead of two
    // that agreed only while no resize was in flight. The contract this line
    // has always stated — stage what the flags were mapped against — is
    // unchanged and still what keeps trim paint, trim hits and flag hits on the
    // flags' own geometry.
    app.staged_displayed_vp_start = wf_cache.fp_vp_start;
    app.staged_displayed_vp_end   = wf_cache.fp_vp_end;
    app.staged_displayed_area_w   = wave_w;
    app.staged_displayed_valid = true;

    // THE REBUILD'S OWN DAMAGE REACHES THE WAVEFORM (architect 2026-08-01,
    // closing the ONE-PRESS STEM LAG). It was the top strip alone, which was
    // exactly right while this cache was a strip surface and nothing else; ROW 5
    // MADE IT A PRODUCER OF WAVEFORM PIXELS — app.marker_stems, painted live by
    // GuiPaintHandler::paint_marker_stems inside the waveform area — and the
    // damage never followed.
    //
    // THE LAG, in the order the loop actually runs it: a nudge press mutates the
    // store and queues its own full strip+waveform damage; THE FRAME PAINTS
    // BEFORE THE NEXT TICK (the run loop services the compositor's frame
    // callback, and this rebuild lives in on_tick — the same
    // paint-outruns-the-fingerprint-check ordering the `p` view switch was
    // fixed for), so that repaint consumed the OLD cache and the OLD stash and
    // was internally consistent: stale flag, stale stem. Then the tick ran, this
    // function rebuilt both, and damaged the STRIP only — so the next frame
    // blitted the NEW flag surface over an untouched waveform, leaving the stem
    // ink at the old column. Flag moved, stem did not, and the stem caught up
    // only when the NEXT press's own waveform damage arrived: every press showed
    // the previous press's stem, which reads as "stepping back resyncs them".
    //
    // WHY THE DAMAGE AND NOT A SYNCHRONOUS REBUILD AT EVERY MUTATION: this shape
    // is what DELAY-AND-SYNC asks for. The stash is BOTH the stem paint source
    // and the hit geometry (flag_hit_rects + marker_stems, one producer — this
    // function), and it is staged into the item basis three lines above, so
    // stash, basis and pixels now all advance at the SAME frame — one frame
    // behind the press, consistently, with the display never showing a column
    // the hit test disagrees with. Making every marker mutation rebuild
    // synchronously would also land them together, but it moves a HarfBuzz
    // shaping pass over every visible label onto each press at key-repeat
    // cadence, and it would need a mutation-site inventory that can rot; this is
    // one rect at the one producer.
    //
    // The rect is the top strip PLUS the waveform — Viewport::invalidate_-
    // waveform_area's waveform rect, re-spelled because this struct holds no
    // Viewport (the same widening that free helper documents). It CONTAINS the
    // old strip-only rect by construction (waveform_area.y IS the top strip's
    // height and its own height floors at 0), so it replaces rather than joins
    // it; and the platform's containment coalescing drops it wholesale on the
    // paths that already damaged the waveform this frame — every marker
    // mutation, every pan/zoom, every drag motion event — so those pay nothing.
    // (THE OVERVIEW LANE IS INSIDE THIS RECT since the relayout's commit B moved
    // the strip into the centered block, so this rebuild now repaints it as a
    // matter of geometry — a cached blit plus two outlines, cheap. It owes the
    // lane nothing on the merits: nothing on the strip keys on the marker stores
    // this rebuild tracks — its bars are the piece, its box the viewport, its
    // tick the playhead — which is why no rider was ever added here, and why
    // none is needed now that the containment does it.)
    const GuiRect wave = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, wave.y + wave.h);
}
