#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "render.h"
#include "warpmarkers.h"
#include "platform_wayland.h"
#include "engine/stft_container.h"   // TimeMapSegment

#include <cairo/cairo.h>
#include <string>
#include <vector>

class GuiWaveformWorker;

// X.7.8a: paint handler cluster. Owns the on_redraw and on_resize callback
// bodies, extracted verbatim from main.cpp's lambdas. Bodies use the
// reference members below in place of the original lambda captures; the
// only behavior change is the indirection.
//
// Construction site: main.cpp, after AppState / GuiAudio / GuiPlayback /
// GuiPlatform / WaveformCache exist. Lifetime is the same scope as the other
// operation structs (Undo, Selection, GuiActiveViews, etc.).
//
// Reference list deviates from the original brief:
//   - The brief listed Viewport& and std::function<bool(int)>&
//     popup_eligible_marker, but a verbatim body copy reveals that paint
//     never calls a Viewport method (geometry queries go through free
//     functions waveform_area / top_strip_area / current_samples_per_pixel
//     declared in app_state.h) and never calls popup_eligible_marker (the
//     eligibility check is inlined as `tempo_inherits || !label_ref.empty()`
//     at each hover-popup paint site). Both omitted to avoid dead weight.
//   - GuiPlatform& is added because paint calls gui.playhead_triangle_surface()
//     for the playhead's triangle indicator.
//   - GuiPlayback& is non-const because on_resize calls
//     playback.resync_predictor(), which mutates atomic predictor state.

// -- Constants used by paint code ----------------------------------------
//
// X.7.8a: hoisted from main.cpp's anonymous namespace so paint_handler.cpp
// can reach them. Other constants (kHoverDelayMs, kMarkerHitHalfPx,
// kDoubleClickMs, kDoubleClickPixels, kPlayheadHalfPx, kTimestampRegionW,
// kTimestampRegionH, kDirtyGapPx, kZoomMsPerPixel) are paint-handler-
// independent and stay in main.cpp's anonymous namespace.

constexpr int      kProgressBarHeight        = 4;
constexpr int      kChannelGapPx             = 2;
// kFlagFontSize lives in render.h so render.cpp can reach it without
// pulling paint_handler.h into the lower-layer include graph.

// Timestamp text layout (bottom-left of the status strip).
constexpr int      kTimestampPadX            = 8;
constexpr int      kTimestampBaselineFromBottom = 12;
constexpr double   kTabLetterGapPx           = 10.0;

// V.B / V.B Addendum 2: iteration popup vertical geometry. The popup sits
// kIterPopupVerticalGapPx above its flag's top edge; the edit-state text
// baseline is shifted up by kIterPopupVPadExtraPx so the pending text
// clears the popup rect's bottom inner padding.
constexpr double   kIterPopupVerticalGapPx   = 4.0;
constexpr double   kIterPopupVPadExtraPx     = 1.0;

// -- Off-screen pixel cache for the waveform subsystem -------------------
//
// Lives for the life of main(); recreated when the waveform area is
// resized; re-rendered when any input to render_waveform has changed.
// The redraw path blits this surface onto the pixmap and paints markers
// / flags / playhead / timestamp on top. No implicit Cairo state from
// the main pixmap context leaks in — render_waveform does its own
// save/restore and does not depend on the caller's transform.
struct WaveformCache {
    cairo_surface_t* surface = nullptr;
    int              width   = 0;     // surface width  (== area.w when valid)
    int              height  = 0;     // surface height (== area.h when valid)

    // Fingerprint of the LIVE surface (what the next blit will draw). Set
    // at completion-swap time, not at dispatch. fp_target discriminates
    // the source-view and target-view caches: a `t` toggle flips it
    // without disturbing the source-domain inputs, forcing a cache rebuild.
    // fp_timemap_hash captures the warp marker / trim state baked into
    // the timemap the target paint just consumed; any authoring edit in
    // source view that would shift the deformity invalidates the target
    // view's last cached paint on its next entry.
    int64_t   fp_vp_start    = 0;
    int64_t   fp_vp_end      = 0;
    int64_t   fp_trim_begin  = 0;
    int64_t   fp_trim_end    = 0;
    int       fp_area_w      = 0;
    int       fp_area_h      = 0;
    long long fp_audio_gen   = -1;     // -1 = never rendered
    bool      fp_target      = false;
    uint64_t  fp_timemap_hash = 0;

    // Stage B (layered-paint): the timemap baked into the live waveform
    // pixels. The stem cache reads this to render target-view stems
    // against the same coordinate system the displayed waveform uses, so
    // stems and waveform pixels snap together at the completion swap
    // instead of diverging during the rebuild window. Empty in source
    // view; empty before the first completion has fired.
    std::vector<TimeMapSegment> fp_timemap;

    // Stage A: pending-slot surface and fingerprint. The worker renders
    // into pending_surface; the completion handler swaps it into surface
    // and copies pending_fp_* into fp_*. While a render is in flight,
    // pending_fp_* describes what the worker is producing — dirty-detect
    // compares against pending_fp_* (not fp_*) so we don't enqueue a
    // second render for the same target the worker is already working on.
    cairo_surface_t* pending_surface = nullptr;
    int              pending_width   = 0;
    int              pending_height  = 0;

    int64_t   pending_fp_vp_start    = 0;
    int64_t   pending_fp_vp_end      = 0;
    int64_t   pending_fp_trim_begin  = 0;
    int64_t   pending_fp_trim_end    = 0;
    int       pending_fp_area_w      = 0;
    int       pending_fp_area_h      = 0;
    long long pending_fp_audio_gen   = -1;
    bool      pending_fp_target      = false;
    uint64_t  pending_fp_timemap_hash = 0;

    // Stage B: the timemap the in-flight job is consuming. Set at
    // dispatch alongside the other pending_fp_*; swapped into fp_timemap
    // at completion.
    std::vector<TimeMapSegment> pending_fp_timemap;

    // Supersede slot: when dirty-detect sees a new viewport mid-render,
    // it stashes the desired fingerprint here instead of dispatching.
    // The completion handler consumes it — if set, the just-completed
    // pending surface is discarded (its pixels will be overwritten by
    // the next render) and a fresh job built from supersede_* is
    // dispatched. Cleared at consumption.
    bool      supersede             = false;
    int64_t   supersede_vp_start    = 0;
    int64_t   supersede_vp_end      = 0;
    int64_t   supersede_trim_begin  = 0;
    int64_t   supersede_trim_end    = 0;
    int       supersede_area_w      = 0;
    int       supersede_area_h      = 0;
    long long supersede_audio_gen   = -1;
    bool      supersede_target      = false;
    uint64_t  supersede_timemap_hash = 0;
    std::vector<TimeMapSegment> supersede_timemap;

    // Stage A: `dirty` no longer drives the dispatch decision (the
    // pending_fp_* comparison does). It remains as a startup/clear flag:
    // set at construction and at destroy_surface to indicate "the live
    // surface has no valid pixels yet, show nothing until the first
    // worker completion publishes pixels." Not consulted by
    // maybe_enqueue_waveform_render.
    bool dirty = true;

    void destroy_surface() {
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        if (pending_surface) {
            cairo_surface_destroy(pending_surface);
            pending_surface = nullptr;
        }
        width  = 0;
        height = 0;
        pending_width  = 0;
        pending_height = 0;
        dirty  = true;
        fp_audio_gen         = -1;
        pending_fp_audio_gen = -1;
        supersede = false;
        supersede_timemap.clear();
        fp_timemap.clear();
        pending_fp_timemap.clear();
    }

    ~WaveformCache() { destroy_surface(); }
};

// -- Off-screen pixel cache for the marker stems (Stage B) ---------------
//
// Mirrors WaveformCache's "live" side but with no pending/supersede plumbing
// — stem rebuilds are synchronous on the main thread (sub-millisecond at
// the marker counts the editor admits). The fingerprint is split into two
// halves:
//   1. Displayed-viewport inputs (vp_start/vp_end/trim/target/timemap_hash/
//      area dimensions/audio_gen): read from wf_cache.fp_*, NOT from
//      current app state. This is how the stem layer snaps together with
//      the waveform layer at the worker's completion swap — both sides
//      key off the same set of displayed-viewport values, which the
//      waveform's swap callback publishes atomically.
//   2. Marker-driven inputs (warpmarker/phase_reset generations, drag
//      overlay generation/active, marker view, render-view flag): read
//      live from app state. These have no waveform coupling, so the stem
//      layer reacts to them immediately on the next tick.
//
// Surface height differs from WaveformCache's: stem geometry spans from
// `area.y - kMarkerConnectorRows` (connector top) down to `area.y + area.h`
// (waveform bottom). The cache surface is sized to fit that full vertical
// extent; the blit positions it at screen y = area.y - kMarkerConnectorRows.
struct StemCache {
    cairo_surface_t* surface = nullptr;
    int              width   = 0;
    int              height  = 0;

    long long fp_audio_gen        = -1;     // -1 = never rendered
    int64_t   fp_vp_start         = 0;
    int64_t   fp_vp_end           = 0;
    int64_t   fp_trim_begin       = 0;
    int64_t   fp_trim_end         = 0;
    int       fp_area_w           = 0;
    int       fp_area_h           = 0;       // surface height (incl. connector rows)
    bool      fp_target           = false;
    uint64_t  fp_timemap_hash     = 0;

    long long fp_warpmarker_generation       = -1;
    long long fp_phase_reset_generation      = -1;
    long long fp_drag_overlay_generation     = -1;
    bool      fp_drag_active                 = false;
    char      fp_active_markers_view         = '\0';
    bool      fp_render_view_enabled         = false;

    // Mirrors WaveformCache::dirty — "no pixels yet, skip blit." Set at
    // construction and at destroy_surface; cleared by the first rebuild.
    bool dirty = true;

    void destroy_surface() {
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        width  = 0;
        height = 0;
        dirty  = true;
        fp_audio_gen = -1;
    }

    ~StemCache() { destroy_surface(); }
};

// -- Iteration popup geometry --------------------------------------------
//
// On-screen geometry for one iteration popup. `flag_rect` is the
// underlying flag's rect (used to anchor); `hit_rect` is the clickable
// region; `text` is the current popup text (for paint and seed-on-edit).
struct IterPopupHit {
    int          marker_index;
    GuiRect      flag_rect;
    GuiRect      hit_rect;
    std::string  text;
};

// Compute iteration popup hit-rects for visible owning markers in
// `top_strip_area`. Uses `compute_flag_hit_rects` for the underlying flag
// positions (so popups inherit the flag-strip greedy elision). Each
// popup sits kIterPopupVerticalGapPx above its flag's top edge. The
// hit_rect height matches the flag's height; width is the monospace
// extent of the popup's current text plus a small horizontal pad so
// edits with longer pending strings stay clickable.
std::vector<IterPopupHit> compute_iter_popup_hits(
    cairo_t* cr,
    GuiRect top_strip_area,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// -- BPM popup geometry --------------------------------------------------
//
// Brief X.2 BPM popup geometry. Mirrors IterPopupHit.
struct BpmPopupHit {
    int          marker_index;
    GuiRect      flag_rect;
    GuiRect      hit_rect;
    std::string  text;
};

// Brief X.2: compute BPM popup hit-rects for visible owning markers that
// carry a stored BPM value. Mirrors compute_iter_popup_hits in shape:
// uniform hit_rect.w sized to "99@[999,999]" so click targets are stable
// as values change; pack collision uses the painted-text width so static
// states pixel-match neighbors.
std::vector<BpmPopupHit> compute_bpm_popup_hits(
    cairo_t* cr,
    GuiRect top_strip_area,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// -- GuiPaintHandler -----------------------------------------------------
//
// X.7.8a: extracted from main.cpp's set_on_redraw / set_on_resize lambdas.
// Reference members map to the long-lived state the paint code reads.
// The struct is constructed once, then the original lambda registrations
// become one-line calls into these methods.
struct GuiPaintHandler {
    AppState&          app;
    const GuiAudio&    audio;
    GuiPlayback&       playback;
    WaveformCache&     wf_cache;
    StemCache&         stem_cache;
    GuiWaveformWorker& waveform_worker;
    GuiPlatform&       gui;

    GuiPaintHandler(AppState&          app_,
                    const GuiAudio&    audio_,
                    GuiPlayback&       playback_,
                    WaveformCache&     wf_cache_,
                    StemCache&         stem_cache_,
                    GuiWaveformWorker& waveform_worker_,
                    GuiPlatform&       gui_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          wf_cache(wf_cache_),
          stem_cache(stem_cache_),
          waveform_worker(waveform_worker_),
          gui(gui_) {}

    void on_redraw(cairo_t* cr, int x, int y, int w, int h);
    void on_resize(int w, int h);

    // Stage A: dirty-detect. Compares the current desired waveform
    // fingerprint against pending_fp_* (the fingerprint the worker is
    // producing, or the last published live fingerprint when idle).
    // - Equal: return; the worker is already producing the right pixels
    //   (or has just produced them).
    // - Different and worker idle: dispatch a fresh render job, updating
    //   pending_fp_* to the desired fingerprint and allocating/reusing
    //   the pending surface.
    // - Different and worker busy: set the supersede slot so the
    //   completion handler dispatches a fresh job for the latest
    //   fingerprint at completion time.
    // Called from on_tick.
    void maybe_enqueue_waveform_render();

    // Stage A: invoked from the worker's DoneCallback (which fires on the
    // main thread, via the eventfd handler the platform layer routes
    // through GuiWaveformWorker::on_completion_event). Either dispatches
    // a supersede job, or swaps the pending surface into the live slot
    // and invalidates the waveform area.
    void on_waveform_render_done(bool ok);

    // Stage B: dirty-detect for the stem cache. Called from on_tick AFTER
    // maybe_enqueue_waveform_render. Reads displayed-viewport inputs from
    // wf_cache.fp_*; reads marker-driven inputs from app state. If the
    // fingerprint matches, no-ops. Otherwise rebuilds the offscreen
    // surface synchronously (sub-millisecond at observed marker counts)
    // and invalidates the stem strip so the next paint blits the new
    // pixels.
    void maybe_rebuild_stem_cache();
};
