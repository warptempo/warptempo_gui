#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "render.h"
#include "platform_wayland.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment

#include <cairo/cairo.h>
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
//     for gui.invalidate_region calls. It carries no cached paint surface of
//     its own: the playhead triangle mask, the last thing it had ever held,
//     moved to render.cpp file-scope state and was then deleted outright with
//     the triangle (2026-08-02).
//   - GuiPlayback& is non-const because on_resize calls
//     playback.resync_predictor(), which mutates atomic predictor state.

// -- Constants used by paint code ----------------------------------------
//
// Declared here so paint_handler.cpp can reach them. The one pointer-side grab
// tolerance is paint-handler-independent and lives with the surface it
// belongs to (kTrimEndcapGrabPx in render.h; the marker stems' died with
// their pointer surface, 2026-08-12);
// playhead_half_px() lives in render.h. redesign_font_size_px() — the product's
// ONE text size since row 7 — lives in render.h so render.cpp can reach it
// without pulling paint_handler.h into the lower-layer include graph.

// THE STATUS CHAIN'S PAD, MEASURED off row_7_text.png: fitting the crop's own
// string offscreen at the row's 16px size puts the pen at x = 13 (12 and 14
// both fit worse; the fit is at the crop's left edge, which is the window's;
// the architect confirmed the crop's x0 is the window edge at the relayout).
// Authored at 100% and scaled on gui_scale_factor() like every other
// redesigned dimension, rounded with std::nearbyint so it stays an integer.
//
// ONE CONSTANT, TWO USES, and it TRAVELLED WITH THE CHAIN when the architect
// moved it into the tab row (2026-08-13): the RIGHT margin the chain aligns to
// — the tab row's right edge now, the bottom row's arrow cluster before that —
// and, on a row carrying a critical message, the gap between the critical chip
// and section C inside the chain. The measured number is kept across the move
// so the chain's own spacing did not change under it. (Its third use, the gap
// from the bottom row's clock cell to the chain's left clip bound, died with
// that bound.)
inline int status_chain_pad_x() {
    return scaled_px(13.0);
}

// THE ICON ROW'S LEFT PAD — the row's 8px lead-in, and since 2026-08-14 THE
// BOTTOM ROW'S PAD TOO, at both ends and for the modal that displaces its
// tenants (architect: "make sure bottom row is same height and metrics
// (padding, etc.) as main icon row"). It lives in this header rather than in
// the painter's file because that unification gave it a reader outside the
// row — one source, so a retune of the icon row carries to the bottom one by
// construction.
//
// (bottom_row_pad_x, the modal's own accessor, is deleted with the ruling: it
// was a separately-measured 13 — the status chain's number, which the modal
// inherited when it landed on this row — while the row's own buttons
// already walked from this 8. Two pads on one lane was the drift; the chain's
// 13 stays its own, on the TAB row, at status_chain_pad_x above.)
inline int icon_row_pad_x() {
    return scaled_px(8.0);
}

// Single source for the modal editor prefixes, read by the modal painter
// (paint_modal_dialog) alone since the editors became dialogs (2026-08-12;
// they were the bottom-strip prefixes from row 7 until then, and the modal is
// back on that row since 2026-08-13). Each is the modal's LABEL, painted at
// the row's left pad beside the inset field; the
// pointer path never measures one — the painter publishes the field's own
// click-to-caret origin (AppState::DialogEditorText), so the mapped
// geometry IS the painted one rather than a re-derivation that could drift.
constexpr const char* kSettingsEditorPrefix = "Setting: ";
constexpr const char* kBpmEditorPrefix      = "BPM: ";
// The load prompt (bare `'`) label. The typed entry identifier —
// `<batch_dir>/<basename>` relative to renders/ — renders directly after the
// trailing slash, so the prefix carries no trailing space.
constexpr const char* kLoadEditorPrefix   = "Load: ./renders/";
// THE SAME EDITOR'S OTHER SUBJECT: in the `h` history mode it takes a COMMIT
// SPELLING (load_history_commit_in_place), so the renders/ path lead-in
// would be a false statement about what is being loaded in place. One label,
// one trailing space, and the branch that selects it is the only place the
// two ever differ.
constexpr const char* kLoadEditorHistoryPrefix = "Load: ";
// The `h` history view's COMMIT-TITLE editor (2026-08-07), whose buffer is the
// message the checkpoint commit will carry. One trailing space, like the two
// prefixes above that name a subject rather than a path.
constexpr const char* kCommitTitleEditorPrefix = "Commit: ";

// THE `h` HISTORY MODE'S ONE BRACKET SPELLING — the sign, then the payload
// DIRECTLY AGAINST IT, no space (architect 2026-08-05, superseding the arc's
// original `[+] <payload>`). Defined in waveform_cache.cpp beside its first
// caller and declared here for its SECOND (2026-08-05): the bottom strip's
// corner line, whose `Scale: [-]<then> [+]<now>` is the same vocabulary at
// another surface, so the two cannot spell the sign differently.
//   * a WARP payload is the tempo token, so this reads `[+]1.05` live and
//     `[+]#1.05` disabled;
//   * a PHASE RESET has no payload — its frame is its whole identity — so it
//     reads the bare `[-]` / `[+]`, with the `#` disable spelling as its one
//     payload when the bit is set (`[+]#`). That is what makes the phase-reset
//     column's CHANGED pair say anything at all: a same-frame change there IS a
//     disable toggle, so `[-]#` beside `[+]` is the toggle in the file's own
//     spelling, where `[-]` beside `[+]` would carry no information;
//   * the CORNER passes disabled=false and the `scale=` token, the payload arm
//     alone.
// ASCII by construction: the signs are literals and every token comes from a
// sidecar grammar that is ASCII-only.
std::string history_diff_label(const char* sign, bool disabled,
                               const std::string& token);

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
// (The former trim-stem cache is retired: EVERY trim pixel — the lane ground,
// the window's bar, the two endcaps and the midpoint mark — paints live per
// frame in GuiPaintHandler::paint_trim. This flag cache is
// the one remaining item cache.)
//
// Synchronous main-thread rebuild; the fingerprint's full field list is the
// ONE authoritative copy at maybe_rebuild_flag_cache's declaration below — do
// not restate it here. The cache
// holds the marker/phase-reset flag BOXES ONLY (trim's bar and endcaps left it
// for the live trim pass); the paint pass is a pure blit. THE EDITING TARGET'S
// BOX IS SKIPPED IN THIS CACHE (2026-08-02): the open flag editor's UNROLLED box
// renders live as an overlay after the blit, and it does NOT reliably cover the
// committed box it replaces — a shortened payload makes the overlay the narrower
// of the two — so the cached pass omits that marker's box, label and hit rect
// outright and the editor owns the flag whole. The editing target is a
// fingerprint field (fp_editing_flag_target) precisely so open, close and
// retarget rebuild this surface.
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
    // THE HISTORY MODE'S SIX INPUTS (the `h` view — AppState::HistoryMode).
    // While it stands this surface carries the shown commit's DELTA instead of
    // any live marker, so what it must contain is decided by: whether the mode
    // stands at all (entering and leaving both swap the whole lane), WHICH
    // commit is shown (`,` / `.` step it), WHICH READING of it is shown (the two
    // compare modes, the tabs switching them), which diff flag holds the mode's
    // focus and WHICH ARE SELECTED (colour swaps, exactly as the live selection
    // hash is a field for the live lane), and WHICH SESSION those are indices
    // into.
    //
    // THE SESSION GENERATION IS ONE OF THEM BECAUSE THE OTHERS REPEAT. The
    // delta of a given commit is fixed for a session's lifetime, but a session
    // is not: every visit opens on index 0 with a cleared focus and the
    // iterative reading, so two sessions of the same piece are
    // indistinguishable in the other four fields — and a
    // close and a reopen delivered in one dispatch batch reach this fingerprint
    // as a single edge, with `active` never observed false. The counter (bumped
    // by open_history_mode_fresh, the one entry owner, and carried across a
    // close) is what makes every visit its own fingerprint, so the previous
    // session's diff flags cannot be blitted on over the new one's.
    //
    // AND THE COMPARE READING IS THE FIFTH (2026-08-05): a commit has TWO
    // deltas — forward against the next-newer item, and against the frozen live
    // now side — and every other field can be identical across a switch (same
    // session, same index, focus cleared to the same -1). Without it the lane
    // would keep blitting the reading the user just switched away from, and the
    // two would cross-blit freely as he switched back and forth. (At the NEWEST
    // index the two deltas coincide, so a switch there redraws the same lane;
    // this field does not know that and does not need to.)
    //
    // AND THE MULTI-SELECTION IS THE SIXTH (2026-08-05): the mode's own selected
    // set brightens its flags exactly as the focus brightens one, so a click
    // that changes membership changes what this surface must contain — the live
    // lane's fp_selection_hash applied to the mode's own list. Hashed rather
    // than counted, since a range replace can leave the size untouched while
    // naming different flags.
    //
    // AND THE WALK'S SIZE IS THE SEVENTH (2026-08-07, with the streaming
    // prefetch): the walk GROWS under a live session, and the arrival that
    // matters most moves no other field here — a view opened before member 0
    // arrives stands at index 0, focus -1, one generation, one reading, and an
    // EMPTY lane, and the delta it must now draw appears with nothing else
    // changing. Counted rather than hashed: membership only ever appends, so the
    // size is the whole of what can differ.
    //
    // AND THE WALK SOURCE IS THE EIGHTH, ITS POSITION THE NINTH (2026-08-07):
    // the view reads TWO walks now, so which one is
    // displayed is a content fact of its own — a switch between them can leave
    // index, focus, generation, reading and count every one of them unchanged
    // while the lane's whole content changes — and the LOCAL walk's `,` / `.`
    // moves its own position field, which no other input here mirrors. The local
    // walk's SIZE needs no field beside them: it is built from the two undo
    // stacks' sizes, captured at entry and frozen for the visit, unlike the
    // commit walk's streaming one.
    bool               fp_history_active     = false;
    std::size_t        fp_history_index      = 0;
    int                fp_history_focus      = -1;
    unsigned long long fp_history_generation = 0;
    GuiHistoryCompare  fp_history_compare    = GuiHistoryCompare::Iterative;
    uint64_t           fp_history_selection_hash = 0;
    std::size_t        fp_history_commit_count   = 0;
    GuiHistoryWalkSource fp_history_source   = GuiHistoryWalkSource::Commit;
    std::size_t        fp_history_local_index    = 0;

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

// -- Off-screen pixel cache for the OVERVIEW STRIP's bars ----------------
//
// The whole-song min/max bars the overview lane blits (paint_overview_strip)
// — the piece rendered once across the lane's width through the plate
// renderer's own column writer (render_waveform, source domain, no map),
// transparent outside the ink exactly like the plate, so the lane's ground
// shows through.
//
// THE INVALIDATION KEY IS (width, height) AND NOTHING ELSE — the LANE's own
// dimensions (the cache surface is lane-sized and blits at the lane's origin;
// the bars are drawn into the content band inside it, borders excluded), which
// move only on a window resize or a gui_scale
// commit (both funnel through the lane accessor this cache is measured
// against). THE KEY'S SHAPE IS UNCHANGED BY THE RELAYOUT'S COMMIT B, which
// fixed the lane's HEIGHT on every host (render.h's kOverviewHeightPx): the
// height still varies with gui_scale, so it stays a key field rather than
// becoming a constant this cache could drop — and the WIDTH was always the one
// that moves on a resize. The AUDIO IS DELIBERATELY NOT A KEY FIELD: the source is loaded
// ONCE at launch and is process-immortal (file_loader — there is no
// in-session source load; `'` load-in-place replaces sidecars and marker
// stores, never the sample buffer), so the bars' input cannot change under a
// live process and a per-frame tick repaint never re-reads the pyramid.
// Rebuilds are synchronous at the paint site (O(lane width) with the
// pyramid's unconditional <=5-pairs-per-column bound — the whole-song span is
// exactly what the coarse rungs exist for).
//
// OWNED BY GuiPaintHandler AS A VALUE, unlike WaveformCache and FlagCache
// (main.cpp-constructed references): those two are touched from outside the
// painter — the worker completion path and main.cpp's tick — while this one
// has no consumer but paint_overview_strip, so the narrower home is the
// honest one.
struct OverviewBarCache {
    cairo_surface_t* surface  = nullptr;
    int              width    = 0;
    int              height   = 0;
    bool             rendered = false;

    void destroy_surface() {
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        width    = 0;
        height   = 0;
        rendered = false;
    }

    ~OverviewBarCache() { destroy_surface(); }
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
    // (20 fields, RE-DERIVED 2026-08-07 off the compare in
    // maybe_rebuild_flag_cache — other sites state only a pointer here):
    //   - GEOMETRY, four fields off the displayed plate (wf_cache.fp_*):
    //     fp_vp_start, fp_vp_end, fp_target, fp_warp_frame_map_hash;
    //   - GEOMETRY, two fields off the LIVE top strip rect (top_strip_area, not
    //     the plate — this surface mirrors the strip, not the waveform):
    //     fp_area_w, fp_area_h;
    //   - MARKER-DRIVEN, five read live from app state: fp_warp_generation,
    //     fp_phase_reset_generation, fp_drag_overlay_hash, fp_selection_hash,
    //     fp_active_markers_view;
    //   - CONTENT, two more: fp_iteration_mode (it changes what the flags SAY)
    //     and fp_editing_flag_target (the open editor's marker, whose box this
    //     pass SKIPS);
    //   - THE HISTORY MODE, NINE (four 2026-08-04, the fifth and sixth
    //     2026-08-05, the seventh, eighth and ninth 2026-08-07):
    //     fp_history_active, fp_history_index, fp_history_focus,
    //     fp_history_generation, fp_history_compare,
    //     fp_history_selection_hash, fp_history_commit_count,
    //     fp_history_source and fp_history_local_index — the `h` view replaces
    //     the lane's whole content, so these decide it as completely as the five
    //     marker-driven fields decide the live one; the generation is what
    //     distinguishes two SESSIONS that agree on the others (a close and a
    //     reopen this pass never sees between), the compare bit what
    //     distinguishes ONE MEMBER'S TWO DELTAS (iterative forward against the
    //     next-newer item, cumulative against the frozen now side), the SOURCE
    //     what distinguishes THE TWO WALKS (the committed history and the
    //     session's own STATE TIMELINE — the model is GuiHistoryLocalWalk's,
    //     history_diff.h — whose positions are two different fields),
    //     and the selection hash what distinguishes two membership states of one
    //     shown delta (the mode's multi-selection, whose members wear the focus's
    //     own brightened face).
    // The measured-font field left the list with row 7's monospace deletion; the
    // flag editor's TEXT was never one of these — it renders live as an overlay
    // after this cache's blit, and only the identity of the suppressed box is a
    // fingerprint fact. Rebuilds are synchronous (sub-millisecond at observed
    // flag counts). This rebuild is the SOLE item-basis STAGE site (the retired
    // trim-stem cache's rebuild was the only other one) — see the staging
    // comment at its tail in waveform_cache.cpp.
    void maybe_rebuild_flag_cache();

    // Resolve the history mode's CURRENT commit into app.history_mode.flags, the
    // painted-order list the lane's diff pass consumes. Called from
    // maybe_rebuild_flag_cache's history arm and nowhere else, so the list is
    // rebuilt exactly when the surface that shows it is — one producer for the
    // flags, their hit rects and their stems alike. A commit whose delta cannot
    // be resolved (an out-of-range index, an unavailable session) yields an
    // empty list, which paints an empty lane rather than a stale one.
    void rebuild_history_diff_flags();

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
    // viewport change, so every live overlay stays
    // registered with the cached pixels instead of the not-yet-painted live
    // viewport. This is the ONE authoritative enumeration of the
    // PLATE-REGISTERED overlays (re-derived by grep over this accessor's
    // callers, 2026-08-02): the region ground, the phase-reset ring (through
    // phase_reset_overlay_band), the playhead head (painted in the ruler pass,
    // but living in the MARKER lane since the row-5 live test), the cursor
    // playhead, the scanner — plus its two per-frame narrow damage sites in
    // main.cpp — and the strip-drag anchor. Other sites state only their own
    // class plus a pointer here. The MARKER STEMS are deliberately not among
    // them: they paint from the flag painter's own stash, on the basis those
    // boxes were laid out against, so a stem cannot leave its flag.
    // spp falls back to the LIVE current_samples_per_pixel when no
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

    // The trim region overlay's on-screen column pair under a given displayed
    // basis. THE SPAN IS DERIVED FROM THE TRIM every call (trim_overlay_span,
    // app_state.h — the region IS the trim since 2026-08-18): that owner crosses
    // both resting trim bounds into the ACTIVE display domain and returns them
    // ALREADY ORDERED, so this maps them to columns with the plain viewport
    // transform and walks no warp map. NOTHING IS STORED and there is no
    // endpoint pair to normalize — do not reintroduce either; the overlay cannot
    // drift from the 9 px bar because both read the one trim.
    // THREE consumers, and the last is why this is PUBLIC: paint_region_ground
    // and paint_region_ink draw the overlay's two halves from it — the ground
    // and the ink cannot disagree about where it is — and
    // GuiInputHandler::region_manipulation_hit (input_pointer.cpp) HIT-TESTS the
    // shown overlay's move zone and its two grab bands from the same call on the
    // same PLATE basis — so a grabbed bound is exactly a painted one, by
    // construction rather than by two derivations agreeing. It stays a named
    // helper because the column pair is a rule, not an inline expression.
    struct RegionColumns {
        int lo_col = 0;
        int hi_col = 0;
    };
    RegionColumns region_columns(const PlateViewportBasis& basis) const;

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

    // The overview strip's bar cache and its dirty-detect (the key contract at
    // OverviewBarCache above): rebuild the whole-song bars iff the lane's
    // dimensions moved; called from paint_overview_strip only.
    OverviewBarCache overview_bar_cache;
    void maybe_rebuild_overview_bar_cache(const GuiRect& lane);

    // (The out-of-trim DIM and its two private helpers — compute_displayed_trim
    // and compute_out_of_trim_rects — are retired wholesale with the opaque
    // recolor model, architect 2026-07-26: TRIM recolors no blitted pixel, the
    // trim bar spanning the window being the whole inside-the-window signal.
    // Neither helper had any other consumer, so both went with the pass. The
    // dim's second-masked-pass MECHANISM came back for the region's ink half in
    // 2026-08-18 — paint_region_ink — over the region's span alone.)

    // (The region-select span's column pair, RegionColumns / region_columns,
    // moved up into the PUBLIC block beside plate_viewport_basis on 2026-08-15,
    // when the region gained its own edit drag and the hit test needed the
    // painter's own answer rather than a second derivation of it.)

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
    // THE FOUR REDESIGNED BUTTON ROWS — the MENU ROW (top lane 0, row 1: the
    // flat
    // sampled ground plus the "File" and "Settings" menu
    // buttons and the view bar), the TAB ROW
    // (top lane 1, row 3: the
    // "A"/"B" Breeze tabs, their frame and its broken border-bottom, and —
    // since 2026-08-13 — the right-aligned STATUS CHAIN painted under them),
    // the
    // ICON ROW (top lane 2, row 4: the twenty-eight view/mode/action buttons —
    // the deleted toolbar row's four lead them since the 2026-08-12 relayout
    // and the history group's seven close them since 2026-08-18 — their
    // separators and its border-bottom, all of them painted on every frame
    // since 2026-08-14), and the UNIFIED BOTTOM ROW's button
    // cluster (bottom lane 0, the strip's one lane since the relayout's commit
    // B: the transport three and the clock left, then the marker verbs, the
    // marker-walk three and the arrow four flush
    // right behind their separators, declared
    // below).
    // All four PUBLISH their buttons' hit rects into app.redesign_buttons —
    // the painter is the only place a shaped label's width exists, so the
    // pointer code reads the stash instead of re-shaping (the displayed-basis
    // doctrine) — and every one stashes the ENABLED and SELECTED bits it
    // painted beside them, through the one publisher (publish_button_face),
    // for main.cpp's staleness comparator.
    //
    // All four are called from on_redraw OUTSIDE the loading / total>0
    // branches, each gated on its OWN exposure — they are the passes with no
    // dependence on the loaded audio, so a button is visible exactly whenever it
    // is clickable (their press claims sit above the pointer path's loading
    // guard). The exposure gate matters: each shapes labels through HarfBuzz,
    // which a narrow per-frame playhead damage must not pay for. Nothing painted
    // after them touches the three lanes, the flag cache being transparent
    // there. (paint_toolbar_row died with row 2 at the 2026-08-12 relayout —
    // its four buttons are the icon row's first group.)
    void paint_menu_row(cairo_t* cr);
    void paint_tab_row(cairo_t* cr);
    // THE STATUS CHAIN — the critical chip then section C's four-tier ladder,
    // right-aligned at the TAB ROW's right margin (architect 2026-08-13). Called
    // from paint_tab_row ALONE and BEFORE its tab walk, which is the whole
    // collision rule: the tabs paint over the chain and win, and text pushed
    // under a tab is accepted. Takes the row's already-selected face and its
    // CONTENT BAND — the lane less its two border rows — so the chain and the
    // tab labels cannot resolve two baselines and neither border is reachable
    // from inside here. The full layout record, the tier ladder and the chip's
    // derived box are at the definition.
    void paint_status_chain(cairo_t* cr, const GuiRect& band,
                            cairo_scaled_font_t* font);
    void paint_icon_row(cairo_t* cr);
    // THE UNIFIED BOTTOM ROW'S BUTTON-AND-CLOCK HALF (rows 8 and 9 merged,
    // 2026-08-12; the arrows flush right since the same day's relayout): the
    // transport three at the left pad, then the right margin's block — the
    // four marker verbs with ADD TO SELECTION behind them + separator +
    // marker-walk three + separator + arrow four (2026-08-15 for the walk
    // group, 2026-08-18 for the verbs) — at
    // the icon row's boxes, and the
    // monospace clock at its own left-anchored pen behind the transport's
    // separator (centred in the lane until 2026-08-18), painted onto the lane paint_bottom_strip has
    // already grounded — that painter is the lane's one chrome owner and the
    // only caller of this body, which keeps the family's fifth button-row
    // painter separate only because the button cluster's tables and the
    // clock's metrics live beside it.
    void paint_bottom_row_buttons_and_clock(cairo_t* cr);

    // THE TWO FLOATING SURFACES, painted TOPMOST — after every row pass, so they
    // overlap the rows they hang over. They cannot coexist, and the claim rests
    // on the OPEN EDGE rather than on which gestures can reach it: toggle_
    // dropdown's open path hides the tooltip outright (a press opens a menu, and
    // so does an armed row-1 hover), and while the popup stands NO roster button
    // answers the pointer at all (redesign_button_hover_zone — the term the hint
    // and the hover face still share), so nothing can stamp a fresh dwell
    // under it. Both PUBLISH the rect
    // they painted (AppState::redesign_tooltip.rect,
    // AppState::dropdown.rect + item_rects) — the dropdown's for its hit
    // tests, the tooltip's only so the hide edge can damage it — and both write
    // a zero rect when not shown, which is the correct empty answer.
    // THE TOOLTIP SERVES TWO SURFACES since 2026-08-13 — the roster and the
    // MODAL DIALOG's buttons, one dwell state whose owner names which (the
    // encoding is at AppState::RedesignTooltip) — which is why it paints
    // AFTER paint_modal_dialog rather than beside the dropdown: it reads the
    // stash that call publishes, and a hint over the modal is the one floating
    // surface a modal does coexist with.
    void paint_shift_tooltip(cairo_t* cr);
    void paint_dropdown(cairo_t* cr);
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
    // THE REGION HIGHLIGHT, ONE HIGHLIGHT IN TWO OPAQUE HALVES STRADDLING THE
    // PLATE BLIT (the Ableton model, extended to the ink 2026-08-18). The GROUND
    // half paints after render_canvas and BEFORE the blit; the INK half masks
    // its own colour through the blitted plate's binary alpha immediately AFTER
    // it, over the identical span. Neither half is a wash, and the two share the
    // basis and column owners so they cannot disagree. The region is the only
    // recolor there is: the phase-reset overlay recolors nothing (architect
    // 2026-07-27).
    void paint_region_ground(cairo_t* cr, const GuiRect& area);
    void paint_region_ink(cairo_t* cr, const GuiRect& area);
    // The overlay band's 1px ring — the phase-reset overlay's whole visual —
    // painted AFTER the plate, a boundary line like the playheads, so it
    // crosses the ink deliberately.
    void paint_phase_reset_overlay_ring(cairo_t* cr, const GuiRect& area);
    // The LIVE trim pass: paints EVERY trim pixel per frame — the trim bar
    // lane whole (its ground, the window's bar, the two endcaps and the
    // midpoint mark) — in ONE pass, entirely inside the trim lane. Its slot is
    // step 5 of THE AUTHORITATIVE PAINT-ORDER BLOCK IN on_redraw; this states
    // only this pass's own place in it. Invoked whenever the exposed rect
    // intersects the top strip OR the waveform area — render_background erases
    // every exposed top-strip pixel, so a strip-only damage (hover text, a
    // flag change) must repaint the live trim bar/endcaps too; the outer Cairo
    // damage clip bounds the actual work. See the definition for the basis
    // contract.
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
    // case the playhead's stem (its waveform segment, the cursor's only stem
    // pixels) does not paint and the marker's stem IS the display.
    // The HEAD still paints — see the definition for the whole ruling, the two
    // ways a stem qualifies, and why this is a state compare and never a pixel
    // one.
    bool playhead_stem_suppressed() const;
    // THE RESTING CURSOR's waveform stem (the head belongs to
    // paint_ruler_row). Paints UNDER the marker stems and the flags —
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
    void paint_bottom_strip(cairo_t* cr);
    // THE OVERVIEW STRIP (top lane 3 since the relayout's commit B, 2026-08-12
    // — the Ableton model): the lane's kWaveformCanvas ground under its ONE
    // kWaveformBorder row at the bottom edge, the cached whole-song bars
    // (overview_bar_cache below), the VIEWPORT BOX
    // and the PLAYHEAD TICK. Called from on_redraw beside the button rows'
    // passes on the lane's own exposure; the ground paints on every frame class
    // (a lane inside the centered block must not read as a hole while loading)
    // and the content gates on loaded audio inside. Full design record at the
    // definition.
    void paint_overview_strip(cairo_t* cr);
    // THE MODAL (2026-08-13): the BOTTOM ROW, hosting the prompts and the
    // four modal editors (settings / load / commit-title / BPM) while one
    // stands — the row's own painter yields the lane to it. Painted LAST from
    // on_redraw's tail, unconditionally: it publishes AppState::modal_dialog
    // and AppState::dialog_editor_text, the geometry the pointer path's veil,
    // button claims and click-to-caret read. The full design record is at the
    // definition; the field's sampled chrome is at render.h's kModal* block.
    void paint_modal_dialog(cairo_t* cr);
};
