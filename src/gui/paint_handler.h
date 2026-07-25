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
    // published live pixels. The flag cache gates on it — it holds no
    // sensible displayed-viewport values before the first waveform paint.
    bool      fp_rendered    = false;
    bool      fp_target      = false;
    uint64_t  fp_warp_frame_map_hash = 0;

    // Layered-paint: the warp_frame_map baked into the live waveform
    // pixels. The flag-cache rebuild reads this to render target-view
    // flags against the same coordinate system the displayed waveform
    // uses (and to stage the displayed hit map), so flags and waveform
    // pixels snap together at the completion swap instead of diverging
    // during the rebuild window; compute_displayed_trim reads it for the
    // out-of-trim dim's plate-composite frames. Empty in source view;
    // empty before the first completion has fired.
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
    int       supersede_inset_px    = 0;   // GUI-captured font-dependent inset
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

// -- Off-screen pixel cache for the top-strip flag rects ----------------
//
// (The former trim-stem cache is retired: EVERY trim pixel — chips, bridge
// wash, strip stem segments, waveform stem segments — paints live per frame in
// GuiPaintHandler::paint_trim, below the playheads. This flag cache is the one
// remaining item cache.)
//
// Synchronous main-thread rebuild fingerprinted
// against wf_cache.fp_* (displayed-viewport inputs) plus marker-store
// generations, drag-overlay hash, selection hash, and marker-view. The cache
// holds the fixed-width marker/phase-reset flag shapes ONLY (trim's b/e chips
// left it for the live trim pass); the paint pass is a pure blit. The flag
// editor's text renders
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
    FlagCache&         flag_cache;
    GuiWaveformWorker& waveform_worker;
    GuiPlatform&       gui;

    GuiPaintHandler(AppState&          app_,
                    const GuiAudio&    audio_,
                    GuiPlayback&       playback_,
                    WaveformCache&     wf_cache_,
                    FlagCache&         flag_cache_,
                    GuiWaveformWorker& waveform_worker_,
                    GuiPlatform&       gui_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          wf_cache(wf_cache_),
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

    // Dirty-detect for the flag-rect cache. Called from on_tick
    // AFTER maybe_enqueue_waveform_render so both layers (waveform,
    // flags) key off the same wf_cache.fp_* and snap together at the
    // waveform's completion swap. Reads displayed-viewport inputs from
    // wf_cache.fp_*; reads marker-driven inputs (warpmarker / phase_reset
    // generations, drag-overlay hash, selection hash, marker-view,
    // editor targets) live from app state. Rebuilds are
    // synchronous (sub-millisecond at observed flag counts). This rebuild
    // is the SOLE item-basis STAGE site (the retired trim-stem cache's
    // rebuild was the only other one) — see the staging comment at its
    // tail in waveform_cache.cpp.
    void maybe_rebuild_flag_cache();

    // Force a synchronous waveform rebuild + fp_vp_* update for a single
    // discrete viewport jump (the marker-focus cycle). Renders into the
    // live surface on the calling (main) thread and publishes the
    // displayed fingerprint immediately, so a same-tick flag rebuild
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
        // Font-dependent waveform inset (waveform_inset_px()), captured on the
        // GUI thread beside area_w/area_h so the worker render reads no
        // gui_font_scale()/g_font_size_pt state (the GUI thread mutates that
        // without draining jobs). All font-derived geometry is snapshotted.
        int      inset_px      = 0;
        bool     is_target     = false;
        uint64_t warp_frame_map_hash  = 0;
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

    // Displayed-domain trim boundary state for the out-of-trim DIM
    // (compute_out_of_trim_rects, its sole consumer now — the live trim pass
    // paint_trim maps its own frames through the item-basis owners
    // displayed_trim_ms / displayed_or_live_target_map instead, matching the
    // trim hit tests; this helper stays on the PLATE's fp map because the dim
    // is a plate composite). Positions are the AUTHORED
    // per-bound frames — unordered (bounds may be inverted mid-gesture —
    // crossed cannot rest — and this paints per frame; past-EOF is load-fatal,
    // so each bound is within [0, EOF])
    // — translated into the displayed domain (target-view warp_frame_map from
    // wf_cache.fp_warp_frame_map, or source-frame).
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
    // A SPLIT basis: the trim FRAMES are the LIVE bounds mapped through the
    // plate's fp map (compute_displayed_trim()), positioned on the PLATE
    // fingerprint's viewport/spp
    // (displayed_viewport_basis, the plate-registered owner region wash and
    // playheads share). The dim is a plate composite, so it registers with the
    // plate: across a drag the viewport is fixed (plate == live) and the edge
    // stays locked to the trim stem, while during an async publish window the
    // plate basis keeps the edge on the just-blitted plate instead of the
    // not-yet-blitted live span. (The live trim pass paint_trim rides the ITEM
    // basis instead — equal to this plate basis at every plate-writer commit,
    // so dim edge and trim stem coincide there; inside the accepted resize
    // item-only-promotion window the two bases diverge and the dim edge can
    // sit a hair off the stem until the plate republishes — see the basis
    // comment at paint_trim.) Both rects span the full waveform height; ATOP
    // confines the recolor to sample pixels.
    struct OutOfTrimRects {
        bool    has_left  = false;
        GuiRect left{};
        bool    has_right = false;
        GuiRect right{};
    };
    OutOfTrimRects compute_out_of_trim_rects(const GuiRect& area) const;

    // The displayed-viewport paint basis: vp_start and samples-per-pixel LOCKED
    // to the blitted plate (wf_cache.fp_*) while the worker rebuilds against a
    // viewport change, so every live overlay (region wash, phase-reset overlay,
    // selected stem, strip-drag anchor, playheads) stays
    // registered with the cached pixels instead of the not-yet-painted live
    // viewport. spp falls back to the LIVE current_samples_per_pixel when no
    // plate has published a span yet (fp_area_w <= 0, cold before the first
    // completion). The ONE owner of that recipe; each caller keeps its own
    // spp <= 0 guard where it has one today.
    struct DisplayedViewportBasis {
        double vp_start = 0.0;
        double spp      = 0.0;
    };
    DisplayedViewportBasis displayed_viewport_basis() const;

    // The region-select span's on-screen column pair under a given displayed
    // basis. Endpoints are active-domain frames stored in drag order; normalize
    // to [lo, hi] then map to columns via the plain viewport transform (the
    // endpoints already live in the displayed domain, so no warp map is walked).
    // Shared by paint_region_wash and the split-playhead branch so the wash edges
    // and the split half-triangles land on exactly the same columns.
    struct RegionColumns {
        int lo_col = 0;
        int hi_col = 0;
    };
    RegionColumns region_columns(const DisplayedViewportBasis& basis) const;

    // on_redraw paint passes. Each renders one strip/layer; on_redraw keeps
    // the rects_intersect gates and calls these in place.
    void paint_flag_annotations(cairo_t* cr, const GuiRect& top_strip);
    void paint_marker_text_lane(cairo_t* cr);
    void paint_waveform_plate(cairo_t* cr, const GuiRect& area);
    void paint_region_wash(cairo_t* cr, const GuiRect& area);
    void paint_phase_reset_overlay(cairo_t* cr, const GuiRect& area);
    // The LIVE trim pass (architect 2026-07-25 — trim z-order below the
    // playhead): paints EVERY trim pixel per frame — the b/e chips, the bridge
    // wash, the strip-crossing stem segments, and the waveform stem segments —
    // in ONE pass, in the old trim-stem-cache slot: after
    // paint_phase_reset_overlay, before paint_selected_stem and hence before
    // every playhead element, while the flag blit still follows the playheads.
    // "Markers over trim" and "playhead over trim" are therefore STRUCTURAL
    // pass order (trim < selected stem < playheads < flags), not an intra-cache
    // paint convention. Invoked whenever the exposed rect intersects the top
    // strip OR the waveform area — render_background erases every exposed
    // top-strip pixel, so a strip-only damage (hover text, a flag change) must
    // repaint the live chips/bridge/strip stems too; the outer Cairo damage
    // clip bounds the actual work. See the definition for the basis contract.
    void paint_trim(cairo_t* cr, const GuiRect& area, const GuiRect& top_strip);
    // Selected-marker stem (blue-focus pivot, architect 2026-07-25): a per-frame
    // live overlay marking the SINGLE selected marker's column — where the playhead
    // sits/would land on it — ALWAYS painted for a singleton selection. It is the
    // singleton's focus visual: hover-, pin-, and gesture-INDEPENDENT (the whole
    // conditional-stem apparatus was harvested with this pivot), so a keyboard-only
    // selection shows it and it persists through scrubs/auditions. The ONE
    // non-selection input is a live position DRAG, which overrides the store frame
    // with the proposed position so the stem tracks the flag. Painted BLUE
    // (kSelected — it marks the selected marker like its flag) through
    // render_playhead's line-only form over the plate; a live overlay, so
    // selection changes never rebuild a cache. A focused GROUP (2+ selected) shows
    // no stem — its blue focus cue is the extent-region wash (kSelectedLight), the
    // stem's "spread" form.
    void paint_selected_stem(cairo_t* cr, const GuiRect& area);
    void paint_playheads(cairo_t* cr, const GuiRect& area);
    void paint_strip_drag_anchor(cairo_t* cr, const GuiRect& area);
    void paint_bottom_strip(cairo_t* cr, int sr);
};
