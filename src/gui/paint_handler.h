#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "render.h"
#include "warpmarkers.h"
#include "platform_wayland.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment

#include <cairo/cairo.h>
#include <functional>
#include <string>
#include <vector>

class GuiWaveformWorker;

// Paint handler cluster. Owns the on_redraw and on_resize callback
// bodies, reaching shared state through the reference members below.
//
// Construction site: main.cpp, after AppState / GuiAudio / GuiPlayback /
// GuiPlatform / WaveformCache exist. Lifetime is the same scope as the other
// operation structs (Undo, Selection, GuiActiveViews, etc.).
//
// Reference list notes:
//   - Viewport& and std::function<bool(int)>& popup_eligible_marker are
//     deliberately omitted: paint never calls a Viewport method (geometry
//     queries go through free functions waveform_area / top_strip_area /
//     current_samples_per_pixel declared in app_state.h) and never calls
//     popup_eligible_marker (the eligibility check is inlined as
//     `tempo_inherits || !label_ref.empty()` at each hover-popup paint
//     site). Both omitted to avoid dead weight.
//   - GuiPlatform& is used by the cache-rebuild paths (waveform_cache.cpp)
//     for gui.invalidate_region calls. The playhead triangle mask now lives
//     in render.cpp file-scope state (playhead_triangle_mask()), not on
//     GuiPlatform.
//   - GuiPlayback& is non-const because on_resize calls
//     playback.resync_predictor(), which mutates atomic predictor state.

// -- Constants used by paint code ----------------------------------------
//
// Declared here so paint_handler.cpp can reach them. Other constants
// (kMarkerHitHalfPx) is paint-handler-independent and
// lives in main.cpp's anonymous namespace; playhead_half_px() lives in
// render.h. flag_font_size_px() lives in render.h so render.cpp can reach
// it without pulling paint_handler.h into the lower-layer include graph.

// Timestamp text layout (bottom-left of the status strip). The
// window-bottom baseline anchor (kTimestampBaselineFromBottom) was replaced
// with row-relative baselines derived from bottom_lower_row_area /
// bottom_upper_row_area. Scales proportionally with the font_size
// setting: the authored value (8) times gui_font_scale(), rounded with
// std::nearbyint so it stays an integer and the sharp-edge conventions keep
// holding. At scale 1 it equals its authored value by identity
// (nearbyint(8*1) == 8).
inline int timestamp_pad_x() {
    return static_cast<int>(std::nearbyint(8.0 * gui_font_scale()));
}

// Single source for the two bottom-strip editor prefixes. The paint
// sites (render_bottom_strip_editor calls) and the mouse drag-to-select
// geometry helper (active_editor_text in input_handler.cpp) both derive
// the editable text's char-0 origin from these, so the origin math can
// never drift from the painted prefix.
constexpr const char* kSettingsEditorPrefix = "setting: ";
constexpr const char* kBpmEditorPrefix      = "bpm: ";
// The render-commit prompt (bare `'`) label. The typed entry identifier —
// `<batch_dir>/<basename>` relative to renders/ — renders directly after the
// trailing slash, so the prefix carries no trailing space.
constexpr const char* kCommitEditorPrefix   = "commit: ./renders/";

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
    // fp_warp_frame_map_hash captures the warp marker / trim state baked into
    // the warp_frame_map the target paint just consumed; any authoring edit in
    // source view that would shift the deformity invalidates the target
    // view's last cached paint on its next entry.
    int64_t   fp_vp_start    = 0;
    int64_t   fp_vp_end      = 0;
    int       fp_area_w      = 0;
    int       fp_area_h      = 0;
    // false until the first worker completion (or synchronous rebuild) has
    // published live pixels. The stem/flag caches gate on it — they hold no
    // sensible displayed-viewport values before the first waveform paint.
    bool      fp_rendered    = false;
    bool      fp_target      = false;
    uint64_t  fp_warp_frame_map_hash = 0;

    // Layered-paint: the warp_frame_map baked into the live waveform
    // pixels. The stem cache reads this to render target-view stems
    // against the same coordinate system the displayed waveform uses, so
    // stems and waveform pixels snap together at the completion swap
    // instead of diverging during the rebuild window. Empty in source
    // view; empty before the first completion has fired.
    std::vector<WarpFrameMapSegment> fp_warp_frame_map;

    // Pending-slot surface and fingerprint. The worker renders
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
    int       pending_fp_area_w      = 0;
    int       pending_fp_area_h      = 0;
    bool      pending_fp_target      = false;
    uint64_t  pending_fp_warp_frame_map_hash = 0;

    // The warp_frame_map the in-flight job is consuming. Set at
    // dispatch alongside the other pending_fp_*; swapped into fp_warp_frame_map
    // at completion.
    std::vector<WarpFrameMapSegment> pending_fp_warp_frame_map;

    // Supersede slot: when dirty-detect sees a new viewport mid-render,
    // it stashes the desired fingerprint here instead of dispatching.
    // The completion handler consumes it — if set, the just-completed
    // pending surface is discarded (its pixels will be overwritten by
    // the next render) and a fresh job built from supersede_* is
    // dispatched. Cleared at consumption.
    bool      supersede             = false;
    int64_t   supersede_vp_start    = 0;
    int64_t   supersede_vp_end      = 0;
    int       supersede_area_w      = 0;
    int       supersede_area_h      = 0;
    bool      supersede_target      = false;
    uint64_t  supersede_warp_frame_map_hash = 0;
    std::vector<WarpFrameMapSegment> supersede_warp_frame_map;
    // The superseding job always reads the one process-immortal source audio
    // (WaveformJob.audio), so the slot carries no audio pointer or keepalive —
    // the deferred redispatch just names &audio.

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
        fp_rendered = false;
        // Poison the pending fingerprint so the next maybe_enqueue tick sees a
        // guaranteed mismatch and re-dispatches — area_w = -1 is impossible for
        // any valid render (compute_waveform_render_inputs rejects area.w <= 0).
        pending_fp_area_w = -1;
        supersede = false;
        supersede_warp_frame_map.clear();
        fp_warp_frame_map.clear();
        pending_fp_warp_frame_map.clear();
    }

    ~WaveformCache() { destroy_surface(); }
};

// -- Off-screen pixel cache for the marker stems ------------------------
//
// Mirrors WaveformCache's "live" side but with no pending/supersede plumbing
// — stem rebuilds are synchronous on the main thread (sub-millisecond at
// the marker counts the editor admits). The fingerprint is split into two
// halves:
//   1. Displayed-viewport inputs (vp_start/vp_end/trim/target/warp_frame_map_hash/
//      area dimensions): read from wf_cache.fp_*, NOT from
//      current app state. This is how the stem layer snaps together with
//      the waveform layer at the worker's completion swap — both sides
//      key off the same set of displayed-viewport values, which the
//      waveform's swap callback publishes atomically.
//   2. Marker-driven inputs (warpmarker/phase_reset generations, drag
//      overlay hash/active, marker view): read live
//      from app state. These have no waveform coupling, so the stem
//      layer reacts to them immediately on the next tick. The drag
//      overlay is hashed (not generation-counted) so future mutations
//      of app.drag don't need to remember to bump a callsite counter.
//
// Surface matches the waveform area: stems now span the WAVEFORM AREA ONLY
// (top at screen y = area.y, down to area.y + area.h — no strip overhang; the
// flag+triangle structure above lives in the FlagCache). The cache surface is
// waveform-height and the blit positions it at screen y = area.y.
struct StemCache {
    cairo_surface_t* surface = nullptr;
    int              width   = 0;
    int              height  = 0;

    int64_t   fp_vp_start         = 0;
    int64_t   fp_vp_end           = 0;
    int64_t   fp_trim_begin       = 0;
    int64_t   fp_trim_end         = 0;
    int       fp_area_w           = 0;
    int       fp_area_h           = 0;       // surface height (incl. stem overhang)
    bool      fp_target           = false;
    uint64_t  fp_warp_frame_map_hash     = 0;

    long long fp_warp_generation       = -1;
    long long fp_phase_reset_generation      = -1;
    uint64_t  fp_drag_overlay_hash           = 0;
    bool      fp_drag_active                 = false;
    char      fp_active_markers_view         = '\0';
    uint64_t  fp_selection_hash              = 0;

    // Trim boundary stems share this cache. The begin/end frame
    // positions ride fp_trim_begin / fp_trim_end above; these capture the
    // project has-set bits so the cache rebuilds when a bound
    // appears/disappears.
    bool      fp_trim_has_begin              = false;
    bool      fp_trim_has_end                = false;

    void destroy_surface() {
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        width  = 0;
        height = 0;
    }

    ~StemCache() { destroy_surface(); }
};

// -- Off-screen pixel cache for the top-strip flag rects ----------------
//
// Mirrors StemCache's shape: synchronous main-thread rebuild fingerprinted
// against wf_cache.fp_* (displayed-viewport inputs) plus marker-store
// generations, drag-overlay hash, selection hash, and marker-view. The cache
// holds ALL flag pixels (the fixed-width marker/phase-reset shapes and the b/e
// trim chips); the paint pass is a pure blit. The flag editor's text renders
// live in the marker-text lane, not in this cache, so the editing target's flag
// paints here as an ordinary selected shape — no skip-guard, no per-frame live
// flag render in the cache.
//
// The cache surface matches `top_strip_area(app)`: width = window width,
// height = top_strip_height, origin (0,0). The blit at on_redraw time
// positions the surface at screen (top_strip.x, top_strip.y) (= (0, 0)).
struct FlagCache {
    cairo_surface_t* surface = nullptr;
    int              width   = 0;
    int              height  = 0;

    int64_t   fp_vp_start            = 0;
    int64_t   fp_vp_end              = 0;
    int       fp_area_w              = 0;
    int       fp_area_h              = 0;
    bool      fp_target              = false;
    uint64_t  fp_warp_frame_map_hash        = 0;

    long long fp_warp_generation    = -1;
    long long fp_phase_reset_generation   = -1;
    uint64_t  fp_drag_overlay_hash        = 0;
    uint64_t  fp_selection_hash           = 0;
    char      fp_active_markers_view      = '\0';

    // The begin/end trim flag chips ride this cache (they live in
    // top_upper_row_area, inside top_strip_area). Their pixels depend on the
    // displayed-domain bound positions, whether each bound is set, and each
    // bound's selected bit — none of which bump any marker generation, so
    // they are part of the cache identity. Mirrors the StemCache fp_trim_*
    // fields so chip and stem rebuild on the same triggers.
    int64_t   fp_trim_begin               = 0;
    int64_t   fp_trim_end                 = 0;
    bool      fp_trim_has_begin           = false;
    bool      fp_trim_has_end             = false;

    void destroy_surface() {
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        width  = 0;
        height = 0;
    }

    ~FlagCache() { destroy_surface(); }
};

// -- GuiPaintHandler -----------------------------------------------------
//
// Extracted from main.cpp's set_on_redraw / set_on_resize lambdas.
// Reference members map to the long-lived state the paint code reads.
// The struct is constructed once, then the original lambda registrations
// become one-line calls into these methods.
struct GuiPaintHandler {
    AppState&          app;
    const GuiAudio&    audio;
    GuiPlayback&       playback;
    WaveformCache&     wf_cache;
    StemCache&         stem_cache;
    FlagCache&         flag_cache;
    GuiWaveformWorker& waveform_worker;
    GuiPlatform&       gui;

    GuiPaintHandler(AppState&          app_,
                    const GuiAudio&    audio_,
                    GuiPlayback&       playback_,
                    WaveformCache&     wf_cache_,
                    StemCache&         stem_cache_,
                    FlagCache&         flag_cache_,
                    GuiWaveformWorker& waveform_worker_,
                    GuiPlatform&       gui_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          wf_cache(wf_cache_),
          stem_cache(stem_cache_),
          flag_cache(flag_cache_),
          waveform_worker(waveform_worker_),
          gui(gui_) {}

    // Installed hook (kick_waveform_* pattern), fired inside on_redraw the
    // instant the displayed-map promotion advances displayed_map_gen — BEFORE
    // any painting — and only when a promotion actually happened. main.cpp wires
    // it to Viewport::recompute_hover_at_cursor so the just-promoted map's flag
    // positions re-resolve the hover identity and rewrite lane_text/readout_text/
    // copy_payload in-place, and every overlay this frame paints (and any Ctrl+C
    // landing before the next tick) sees the new identity rather than the old
    // map's. Held as a std::function to avoid a Viewport& member on the paint
    // handler. Empty until wired.
    std::function<void()> on_displayed_map_promoted;

    void on_redraw(cairo_t* cr, int x, int y, int w, int h);
    void on_resize(int w, int h);

    // Dirty-detect. Compares the current desired waveform
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

    // Invoked from the worker's DoneCallback (which fires on the
    // main thread, via the eventfd handler the platform layer routes
    // through GuiWaveformWorker::on_completion_event). Either dispatches
    // a supersede job, or swaps the pending surface into the live slot
    // and invalidates the waveform area.
    void on_waveform_render_done(bool ok);

    // Dirty-detect for the stem cache. Called from on_tick AFTER
    // maybe_enqueue_waveform_render. Reads displayed-viewport inputs from
    // wf_cache.fp_*; reads marker-driven inputs from app state. If the
    // fingerprint matches, no-ops. Otherwise rebuilds the offscreen
    // surface synchronously (sub-millisecond at observed marker counts)
    // and invalidates the stem strip so the next paint blits the new
    // pixels.
    void maybe_rebuild_stem_cache();

    // Dirty-detect for the flag-rect cache. Called from on_tick
    // AFTER maybe_rebuild_stem_cache so all three layers (waveform, stems,
    // flags) key off the same wf_cache.fp_* and snap together at the
    // waveform's completion swap. Reads displayed-viewport inputs from
    // wf_cache.fp_*; reads marker-driven inputs (warpmarker / phase_reset
    // generations, drag-overlay hash, selection hash, marker-view,
    // editor targets) live from app state. Rebuilds are
    // synchronous (sub-millisecond at observed flag counts).
    void maybe_rebuild_flag_cache();

    // Force a synchronous waveform rebuild + fp_vp_* update for a single
    // discrete viewport jump (the marker-focus cycle). Renders into the
    // live surface on the calling (main) thread and publishes the
    // displayed fingerprint immediately, so a same-tick stem/flag rebuild
    // reads the current viewport instead of the lagging async one. NOT
    // for continuous gestures — those stay on the worker.
    void force_synchronous_waveform_rebuild();

    // Incremental pan fast-path. For a pure horizontal pan, shift the live plate
    // by the pixel delta and render only the newly exposed edge strip inline,
    // instead of kicking a full worker re-render. new_vp_start is the post-clamp
    // viewport start in the displayed domain. Wired from main.cpp into Viewport
    // via the request_waveform_pan_ callback.
    //
    // `synchronous` picks the caller class. When false (the wheel pan and
    // PageUp/PageDown steps in scroll_viewport), every case that is not a clean
    // translate of the current plate — no plate, worker busy, drag, resize,
    // view/warp_frame_map change — defers to the async worker, and the on_tick
    // backstop catches residual drift. When true (the Alt+drag grab-pan, through
    // scroll_viewport), the mid-gesture frame must never paint over a
    // stale-basis plate: a busy worker is DRAINED (wait_until_idle) rather than
    // deferred to, then the shift proceeds against the drained state, and every
    // remaining non-shift case falls back to force_synchronous_waveform_rebuild
    // instead of enqueue-and-return. The over-a-window fast-flick fallback is a
    // full synchronous rebuild in both modes.
    void pan_waveform_incremental(int64_t new_vp_start, bool synchronous = false);

private:
    // Waveform fingerprint inputs derived from current app state. This is
    // the single source of truth for the desired waveform fingerprint —
    // both maybe_enqueue_waveform_render (async path) and
    // force_synchronous_waveform_rebuild (sync path) consume it. The
    // on_redraw consumer-side derivation must stay in sync with this
    // helper the same way it tracked the prior inline block.
    struct WaveformRenderInputs {
        int64_t  vp_start      = 0;
        int64_t  vp_end        = 0;
        int      area_w        = 0;
        int      area_h        = 0;
        bool     is_target     = false;
        uint64_t warp_frame_map_hash  = 0;
        int      channel_count = 0;
        // The translation map: the target-view map in target view, empty in
        // source view.
        std::vector<WarpFrameMapSegment> warp_frame_map;
        // The audio the plate reads from: always the one process-immortal
        // source audio. Set by compute_waveform_render_inputs; routed into
        // WaveformJob.audio and into the synchronous / pan render paths.
        const GuiAudio* audio = nullptr;
        bool     valid         = false;        // false if degenerate / loading
    };

    WaveformRenderInputs compute_waveform_render_inputs() const;

    // Displayed-domain trim boundary state, shared by the stem cache (which
    // paints the begin/end stems) and the flag cache (which paints the b/e
    // chips that cap them). Computing it in one place keeps chip and stem in
    // lockstep — same positions, same has bits (trim has no selected state) —
    // so they always read as one continuous unit. Positions are the AUTHORED
    // per-bound frames — unordered (bounds may be inverted mid-gesture —
    // crossed cannot rest — and this paints per frame; past-EOF is load-fatal,
    // so each bound is within [0, EOF])
    // — translated into the displayed domain (target-view warp_frame_map from
    // wf_cache.fp_warp_frame_map, or source-frame), matching the marker
    // stems' coordinate system.
    struct DisplayedTrim {
        int64_t begin          = 0;
        int64_t end            = 0;
        bool    has_begin      = false;
        bool    has_end        = false;
    };
    DisplayedTrim compute_displayed_trim() const;

    // Out-of-trim dim rects in SCREEN coordinates for the current frame, or
    // an empty result when nothing should dim (no trim, or an
    // INVERTED trim — begin strictly later than end in the displayed domain
    // shades nothing; a mid-gesture-only state, since crossed bounds
    // cannot rest past the commit auto-clear).
    // Painted by on_redraw as a CAIRO_OPERATOR_ATOP overlay right after the
    // waveform plate blit, so the dim recolors only the out-of-trim sample
    // pixels (the plate itself is trim-agnostic — see render_waveform).
    // Resolved from the LIVE viewport (app.viewport_start_sample + live spp)
    // and the displayed-domain trim frames compute_displayed_trim() returns
    // (the SAME source the trim stems use), so the dim edge stays locked to
    // the trim stem across a drag with no cache rebuild. Both rects span the
    // full waveform height; ATOP confines the recolor to sample pixels.
    struct OutOfTrimRects {
        bool    has_left  = false;
        GuiRect left{};
        bool    has_right = false;
        GuiRect right{};
    };
    OutOfTrimRects compute_out_of_trim_rects(const GuiRect& area) const;

    // on_redraw paint passes. Each renders one strip/layer; on_redraw keeps
    // the rects_intersect gates and calls these in place.
    void paint_flag_annotations(cairo_t* cr, const GuiRect& top_strip, int sr);
    void paint_marker_text_lane(cairo_t* cr);
    void paint_waveform_plate(cairo_t* cr, const GuiRect& area);
    void paint_region_wash(cairo_t* cr, const GuiRect& area);
    void paint_phase_reset_overlay(cairo_t* cr, const GuiRect& area);
    void paint_marker_stems(cairo_t* cr, const GuiRect& marker_paint_rect);
    void paint_playheads(cairo_t* cr, const GuiRect& area);
    void paint_strip_drag_anchor(cairo_t* cr, const GuiRect& area);
    void paint_bottom_strip(cairo_t* cr, int sr);
};
