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
//     popup_eligible_marker directly through this reference (the bottom strip's
//     readout calls the free function). Both omitted to avoid dead weight.
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
// render.h. redesign_font_size_px() — the product's ONE text size since row 7 —
// lives in render.h so render.cpp can reach it without pulling paint_handler.h
// into the lower-layer include graph.

// THE BOTTOM ROW'S PAD — the pen x of the first glyph on the line,
// MEASURED off row_7_text.png: fitting the crop's own string offscreen at the
// row's 16px size puts the pen at x = 13 (12 and 14 both fit worse; the fit is
// at the crop's left edge, which is the window's; the architect confirmed the
// crop's x0 is the window edge at the relayout). Authored at 100% and scaled
// on gui_scale_factor() like every other redesigned dimension, rounded with
// std::nearbyint so it stays an integer.
//
// (It replaces timestamp_pad_x, the authored 8 on the font axis. The row rides
// gui_scale now — see bottom_row_h_px.) ONE CONSTANT, THREE USES in
// bottom_row_sections since the 2026-08-01 relayout: the LEFT lead-in before the
// modal span, the INTER-SECTION gap between that span and the timestamp's
// reserved cell, and the RIGHT margin after that cell. The reuse is an
// eye-consistency choice, stated there.
inline int bottom_row_pad_x() {
    return static_cast<int>(std::nearbyint(13.0 * gui_scale_factor()));
}

// Single source for the three bottom-strip editor prefixes, read by the paint
// sites (render_bottom_strip_editor) alone since row 7. The pointer path no
// longer measures a prefix at all: the painter shapes prefix and pending as ONE
// run and publishes where the pending half begins, so the click-to-caret origin
// IS the painted one rather than a re-derivation that could drift from it.
constexpr const char* kSettingsEditorPrefix = "Setting: ";
constexpr const char* kBpmEditorPrefix      = "BPM: ";
// The render-commit prompt (bare `'`) label. The typed entry identifier —
// `<batch_dir>/<basename>` relative to renders/ — renders directly after the
// trailing slash, so the prefix carries no trailing space.
constexpr const char* kCommitEditorPrefix   = "Commit: ./renders/";

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
    // The measured font pixel size the live pixels were rendered under. The
    // plate's own font dependence is the inset band and the area height; keying
    // the measure itself makes both sound by field (see the fingerprint note in
    // waveform_cache.cpp).
    // The waveform INSET the live pixels were rendered with (waveform_inset_px()
    // — the plate's one geometry input that is not an area dimension). Keyed
    // directly, so an inset change dirties the plate BY FIELD rather than
    // through whichever area dimension happens to move with it. (It keyed the
    // measured MONOSPACE font size until row 7, as a proxy for this: the inset
    // was font-derived then. The proxy died with the grid; the thing itself is
    // what the job takes.)
    int       fp_inset_px = -1;
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
    // during the rebuild window — its one consumer. Empty in source view;
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
    int       pending_fp_inset_px = -1;
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
    double    supersede_painter_spp = 0.0;  // the lattice q, like the job's
    int       supersede_area_w      = 0;
    int       supersede_area_h      = 0;
    int       supersede_inset_px    = 0;   // GUI-captured waveform inset
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
// bar, strip stem segments, waveform stem segments — paints live per frame in
// GuiPaintHandler::paint_trim, below the playheads. This flag cache is the one
// remaining item cache.)
//
// Synchronous main-thread rebuild; the fingerprint's full field list is the
// ONE authoritative copy at maybe_rebuild_flag_cache's declaration below — do
// not restate it here. The cache
// holds the marker/phase-reset flag BOXES ONLY (trim's bar and endcaps left it
// for the live trim pass); the paint pass is a pure blit. The open flag editor's
// UNROLLED box renders live as an overlay AFTER this blit and always covers the
// flag it replaces, so the editing target's flag caches here as an ordinary box
// — no skip-guard, no per-frame live flag render in the cache.
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
    // (THE MEASURED-FONT FIELD IS GONE — row 7. It said "the metrics these flag
    // pixels were laid out with", true while flag shapes were monospace-derived;
    // row 5 moved every flag dimension onto the gui_scale axis and left it a
    // recorded vestige, and row 7 deleted the measurement it keyed.)
    // ITERATION MODE joined the fingerprint with row 5 (2026-08-01): the flags
    // CARRY TEXT now, composed through flag_text_iter, which splices the
    // `+[lo, hi]` bracket exactly when this bit is on. Before row 5 the shapes
    // were textless and the bracket surfaced only in the marker-text lane, a
    // live per-frame pass that needed no fingerprint; `i` damages the top strip
    // but the rebuild is fingerprint-guarded, so without this field the damage
    // would repaint the same cached bytes.
    bool      fp_iteration_mode           = false;
    // THE MARKER WHOSE FLAG EDITOR IS OPEN, or -1. In the fingerprint because
    // that marker's box is SKIPPED in the cached pass (the open editor paints it
    // unrolled instead), so opening, closing or retargeting the editor changes
    // what this surface must contain. Without it the cache would keep the
    // suppressed frame after the editor closed — and keep the drawn box while it
    // opened. Contract at render_flags' editing_marker_index (render.h).
    int       fp_editing_flag_target      = -1;

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
    // waveform's completion swap. THE ONE AUTHORITATIVE FINGERPRINT FIELD LIST
    // (12 fields, re-derived 2026-08-01 — other sites state only a pointer
    // here): fp_vp_start, fp_vp_end, fp_area_w, fp_area_h, fp_target,
    // fp_warp_frame_map_hash (all six displayed-viewport inputs, read from
    // wf_cache.fp_*), plus fp_warp_generation, fp_phase_reset_generation,
    // fp_drag_overlay_hash, fp_selection_hash, fp_active_markers_view (the
    // marker-driven inputs, read live from app state) and fp_iteration_mode
    // (which changes what the flags SAY). The measured-font field left the list
    // with row 7's monospace deletion; the flag editor's text was never one of
    // these — it renders live as an overlay after this cache's blit. Rebuilds are
    // synchronous (sub-millisecond at observed flag counts). This rebuild
    // is the SOLE item-basis STAGE site (the retired trim-stem cache's
    // rebuild was the only other one) — see the staging comment at its
    // tail in waveform_cache.cpp.
    void maybe_rebuild_flag_cache();

    // Force a synchronous waveform rebuild + fp_vp_* update for a user-driven
    // viewport jump. Renders into the live surface on the calling (main)
    // thread and publishes the displayed fingerprint immediately, so a
    // same-tick flag rebuild reads the current viewport instead of the lagging
    // async one. This is the route for EVERY user-driven viewport change,
    // PANNING INCLUDED since the incremental shift-and-strip path was retired
    // (architect 2026-07-26 — moving and resting plates come off one code path;
    // see the routing rules at the definition). Undriven changes — resize, the
    // launch load, follow scrolling — stay on the worker.
    void force_synchronous_waveform_rebuild();

    // THE PLATE PAINT BASIS: vp_start and samples-per-pixel LOCKED
    // to the blitted plate (wf_cache.fp_*) while the worker rebuilds against a
    // viewport change, so every live overlay (the region ground, the overlay
    // ring, the selected stem, the strip-drag anchor, the playheads) stays
    // registered with the cached pixels instead of the not-yet-painted live
    // viewport. This is the ONE authoritative enumeration of the
    // PLATE-REGISTERED overlays; other sites state only their own class plus a
    // pointer here. spp falls back to the LIVE current_samples_per_pixel when no
    // plate has published a span yet (fp_area_w <= 0, cold before the first
    // completion). The ONE owner of that recipe; each caller keeps its own
    // spp <= 0 guard where it has one today.
    //
    // NAMED FOR ITS EPOCH (architect 2026-07-30). This accessor and the free
    // item_viewport_basis(app, audio) (app_state.h) were BOTH spelled
    // `displayed_viewport_basis` and both returned a `DisplayedViewportBasis`,
    // so C++ name lookup silently resolved the unqualified spelling to THIS one
    // inside the class scope and the free owner needed a ::-qualification
    // workaround to be reachable at all — two coordinate epochs indistinguishable
    // by grep, which is how three authoritative comments came to disagree about
    // which basis the selected stem's damage rode. The two epochs stay distinct
    // (the resize item-only-promotion window is real; the do-not-collapse ruling
    // is at item_viewport_basis); only the names changed.
    //
    // PUBLIC because the playheads' narrow DAMAGE sites need it: damage follows
    // the basis of the pixels it erases, and the sites that can see a
    // GuiPaintHandler resolve their columns here rather than on the live
    // viewport (the rule and the per-site shape table live at playhead_pixel_x,
    // app_state.h). Read-only geometry, no state touched.
    struct PlateViewportBasis {
        double vp_start = 0.0;
        double spp      = 0.0;
    };
    PlateViewportBasis plate_viewport_basis() const;

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
        // The PAINTER's samples-per-pixel (painter_samples_per_pixel, the one
        // owner) — the same q clamp_viewport_start snaps the viewport onto. The
        // renderer needs the exact double, not a re-derivation from vp_end -
        // vp_start, because the authoring lattice is defined in terms of it.
        // vp_end above stays: it is a FINGERPRINT input and unrelated to this.
        double   painter_spp   = 0.0;
        int      area_w        = 0;
        int      area_h        = 0;
        // The waveform inset (waveform_inset_px()), captured on the GUI thread
        // beside area_w/area_h so the worker render reads no scale state (the
        // GUI thread mutates that without draining jobs). All scale-derived
        // geometry is snapshotted. It is BOTH a render input and a fingerprint
        // field — the plate's only non-area geometry, so nothing else would
        // move if it changed alone.
        int      inset_px      = 0;
        bool     is_target     = false;
        uint64_t warp_frame_map_hash  = 0;
        // The translation map: the target-view map in target view, empty in
        // source view.
        std::vector<WarpFrameMapSegment> warp_frame_map;
        // The audio the plate reads from: always the one process-immortal
        // source audio. Set by compute_waveform_render_inputs; routed into
        // WaveformJob.audio and into the synchronous render path.
        const GuiAudio* audio = nullptr;
        bool     valid         = false;        // false if degenerate / loading
    };

    WaveformRenderInputs compute_waveform_render_inputs() const;

    // (The out-of-trim DIM and its two private helpers — compute_displayed_trim
    // and compute_out_of_trim_rects — are retired wholesale with the opaque
    // recolor model, architect 2026-07-26: the plate is never recolored after
    // the blit, and the trim bridge bar is the whole inside-the-window signal.
    // Neither helper had any other consumer, so both went with the pass.)

    // The region-select span's on-screen column pair under a given displayed
    // basis. Endpoints are active-domain frames stored in drag order; normalize
    // to [lo, hi] then map to columns via the plain viewport transform (the
    // endpoints already live in the displayed domain, so no warp map is walked).
    // Its sole consumer since 2026-07-30 is paint_region_ground (the
    // split-playhead branch that shared it died with the SPAN FORM); it stays a
    // named helper because the column pair is a rule, not an inline expression.
    struct RegionColumns {
        int lo_col = 0;
        int hi_col = 0;
    };
    RegionColumns region_columns(const PlateViewportBasis& basis) const;

    // The phase-reset overlay band's clipped screen-x span for this frame, or
    // valid == false when no band shows (wrong view, no eligible focused reset,
    // a suppressing multi-selection, a sub-pixel width, or a span clipped
    // wholly offscreen). Kept SEPARATE from its one consumer
    // (paint_phase_reset_overlay_ring): it owns every visibility gate as well as
    // the span, and Selection::phase_overlay_subject mirrors its selection-state
    // gates (never the geometry ones) for the damage owners and for Space's
    // lead-in audition — one rule, that mirror's readers enumerated at its own
    // declaration in selection.h.
    struct PhaseResetOverlayBand {
        bool   valid = false;
        double x0    = 0.0;   // left screen x, clipped to the area
        double x1    = 0.0;   // right screen x, exclusive, clipped
    };
    PhaseResetOverlayBand phase_reset_overlay_band(const GuiRect& area) const;

    // on_redraw paint passes. Each renders one strip/layer; on_redraw keeps
    // the rects_intersect gates and calls these in place.
    void paint_flag_annotations(cairo_t* cr, const GuiRect& top_strip);
    // THE RULER LANE (top lane 5): the timestamp ladder and its ticks. Reads the
    // DISPLAYED plate basis, so it re-derives on every pan/zoom along with the
    // strip content it is painted beside.
    void paint_ruler_row(cairo_t* cr);
    // THE FOUR REDESIGNED ROWS — the MENU ROW (top lane 0, row 1: the flat
    // sampled ground plus the "Quit" and "Settings" buttons), the TOOLBAR ROW
    // (top lane 1,
    // row 2: the same ground, its border-bottom, its separators and the four
    // Save/Undo/Redo/Render buttons) and the TAB ROW (top lane 2, row 3: the
    // "A"/"B" Breeze tabs, their frame and its broken border-bottom) and the
    // ICON ROW (top lane 3, row 4: the thirteen view/mode/action buttons, their
    // separators and its border-bottom).
    // All four PUBLISH their buttons' hit rects into app.redesign_buttons —
    // the painter is the only place a shaped label's width exists, so the
    // pointer code reads the stash instead of re-shaping (the displayed-basis
    // doctrine) — and all three stash the ENABLED VECTOR they painted beside it
    // for main.cpp's staleness comparator, plus (row 4) the SELECTED bits.
    //
    // All four are called from on_redraw OUTSIDE the loading / total>0
    // branches, each gated on its OWN exposure — they are the passes with no
    // dependence on the loaded audio, so a button is visible exactly whenever it
    // is clickable (their press claims sit above the pointer path's loading
    // guard). The exposure gate matters: each shapes labels through HarfBuzz,
    // which a narrow per-frame playhead damage must not pay for. Nothing painted
    // after them touches the four lanes, the flag cache being transparent
    // there.
    void paint_menu_row(cairo_t* cr);
    void paint_toolbar_row(cairo_t* cr);
    void paint_tab_row(cairo_t* cr);
    void paint_icon_row(cairo_t* cr);

    // THE TWO FLOATING SURFACES, painted TOPMOST — after every row pass, so they
    // overlap the rows they hang over. They cannot coexist: the dropdown opens
    // on a PRESS and any press hides the tooltip, and while the dropdown is open
    // no roster button hovers (redesign_button_hoverable), so no tooltip can
    // arm. Both PUBLISH the rect they painted (AppState::redesign_tooltip.rect,
    // AppState::settings_popup.rect + item_rects) — the dropdown's for its hit
    // tests, the tooltip's only so the hide edge can damage it — and both write
    // a zero rect when not shown, which is the correct empty answer.
    void paint_shift_tooltip(cairo_t* cr);
    void paint_settings_popup(cairo_t* cr);
    // The shared box shape both draw, dressed by the caller: the tooltip takes
    // #292c30 under #535659, the dropdown its own darker #1c1f22 under #4c4e51.
    void paint_popup_chrome(cairo_t* cr, const GuiRect& r,
                            GuiColor ground, GuiColor border);
    // (The open flag editor has no pass member here. It is render_flag_editor_box
    // in render.cpp — the last of the marker-text lane's paint pass, unrolled
    // into the flag itself in row 5's checkpoint C, where it shares the flag
    // painter's class ladder, pads, baseline and shaping. on_redraw calls the
    // free function directly, in the floating-surfaces slot and for their
    // reason: it publishes geometry the pointer path reads.)
    void paint_waveform_plate(cairo_t* cr, const GuiRect& area);
    // THE GROUND RECOLOR, painted after render_canvas and BEFORE the plate blit
    // (the Ableton model — the highlight changes the ground, the ink is
    // untouched). The region highlight is the only one: the phase-reset overlay
    // recolors no ground (architect 2026-07-27).
    void paint_region_ground(cairo_t* cr, const GuiRect& area);
    // THE 1px CHANNEL SPLIT LINE (2026-08-01) — the L/R boundary, which is also
    // the lower-half scrub boundary, drawn in cairo OVER the plate blit and
    // under every boundary line above it (ground furniture, not a cursor). Both
    // views, always; 1px at every gui_scale by ruling. Its row is the shared
    // owner waveform_channel_split_row, read on the plate's published geometry.
    void paint_channel_split(cairo_t* cr, const GuiRect& area);
    // The overlay band's 1px ring — the phase-reset overlay's whole visual —
    // painted AFTER the plate, a boundary line like the playheads, so it
    // crosses the ink deliberately.
    void paint_phase_reset_overlay_ring(cairo_t* cr, const GuiRect& area);
    // The LIVE trim pass (architect 2026-07-25 — trim z-order below the
    // playhead): paints EVERY trim pixel per frame — the b/e chips, the bridge
    // bar, the strip-crossing stem segments, and the waveform stem segments —
    // in ONE pass, in the old trim-stem-cache slot: after
    // paint_phase_reset_overlay_ring, before paint_marker_stems and hence before
    // every playhead element, while the flag blit still follows the playheads.
    // "Markers over trim" and "playhead over trim" are therefore STRUCTURAL
    // pass order (trim < marker stems < playheads < flags), not an intra-cache
    // paint convention. Invoked whenever the exposed rect intersects the top
    // strip OR the waveform area — render_background erases every exposed
    // top-strip pixel, so a strip-only damage (hover text, a flag change) must
    // repaint the live chips/bridge/strip stems too; the outer Cairo damage
    // clip bounds the actual work. See the definition for the basis contract.
    void paint_trim(cairo_t* cr, const GuiRect& area, const GuiRect& top_strip);
    // MARKER STEMS (row 5, 2026-08-01) — the per-frame waveform overlay that
    // replaced the singleton selected-marker stem outright. EVERY ENABLED marker
    // of the active column stems, always, from its flag's bottom (= the marker
    // lane's bottom = the waveform top) down through the waveform to the
    // window's content bottom, in its class's UNSELECTED color; a DISABLED
    // marker stems never. Selection changes nothing here — a selected default
    // marker keeps the calm #9b59b6 stem, the architect's explicit rule, and the
    // selection cue is entirely the flag's bright colour pair.
    //
    // It paints from the marker painter's stash (AppState::marker_stems) rather
    // than walking the store: the stem stands on its flag box's LEFT EDGE, and
    // that column is the one the painter already resolved on the displayed
    // basis, so stem and flag cannot land on different pixels during an async
    // publish window. A live overlay, not a cache — the stash is the cached
    // part.
    //
    // ONE PAINT-TIME COLOUR OVERRIDE, and one only (2026-08-01): the open flag
    // editor's invalid-commit RED FLASH reaches its marker's stem, so a flashing
    // flag and its stem agree. It is applied here rather than published into the
    // stash because that is how the flash face itself works — an override over
    // the resolved class, per frame, out of any cache (the definition carries
    // the reasoning and the damage story).
    //
    // The old singleton stem's whole apparatus goes with it: the size()==1 gate,
    // the DragOverlay re-derivation (the stash already carries the mid-drag
    // column), the grey focus-column paint, and selection.cpp's stem
    // capture/damage pairs. The `selected_stem` config key it painted from
    // outlived this site by a day and died with the whole tunable palette
    // (2026-08-02).
    void paint_marker_stems(cairo_t* cr, const GuiRect& area);
    // THE COINCIDENT-STEM SUPPRESSION (architect 2026-08-01) — 035e669's model
    // reinstated under row 5's always-on-stem regime. True when a MARKER'S OWN
    // STEM is standing where the cursor playhead's stem would stand, in which
    // case the playhead's stem does not paint at all (neither its waveform
    // segment nor its marker-lane remnant) and the marker's stem IS the display.
    // The HEAD still paints — see the definition for the whole ruling, the two
    // ways a stem qualifies, and why this is a state compare and never a pixel
    // one.
    bool playhead_stem_suppressed() const;
    // THE RESTING CURSOR's waveform stem (the head and the marker-lane segment
    // belong to paint_ruler_row). Paints UNDER the marker stems and the flags —
    // the z-order flip — which is the hidden-by-marker model for a cursor
    // sitting ON a marker.
    void paint_playheads(cairo_t* cr, const GuiRect& area);
    // THE MOVING PLAYBACK LINE, its own pass since 2026-08-01 and invoked AFTER
    // paint_marker_stems: the scanner draws OVER the stems (and over the cursor
    // where they meet) instead of being erased by every marker it sweeps past.
    // Waveform-only, gated on playhead_scanner_active — see the definition for
    // the ruling and for why the cursor did NOT move with it.
    void paint_scanner(cairo_t* cr, const GuiRect& area);
    void paint_strip_drag_anchor(cairo_t* cr, const GuiRect& area);
    void paint_bottom_strip(cairo_t* cr, int sr);
};
