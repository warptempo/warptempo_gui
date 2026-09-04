#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "render.h"
#include "platform.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment

#include <cairo/cairo.h>
#include <string>
#include <vector>

class GuiWaveformWorker;
struct GuiTargetRender;
class GuiExternalSyncWorker;

// Paint handler cluster. Owns the on_redraw and on_resize callback
// bodies, reaching shared state through the reference members below.
//
// Construction site: main.cpp, after AppState / GuiAudio / GuiPlayback /
// GuiPlatform / WaveformCache exist. Lifetime is the same scope as the other
// operation structs (Undo, Selection, GuiActiveViews, etc.).
//
// Reference list notes:
//   - Viewport& is deliberately omitted: paint never calls a Viewport method
//     (geometry queries go through the free functions waveform_area /
//     top_strip_area / current_samples_per_pixel, declared in app_state.h),
//     so the reference would be dead weight. (It was named here beside a
//     `std::function<bool(int)>& payload_eligible_marker` until 2026-09-02:
//     that gate is not a callback and has not been one for some time — it is
//     the free function `payload_eligible_marker(const AppState&,
//     const GuiAudio&, int)` over the classifier `payload_eligibility`, both
//     declared in app_state.h — and paint reads it through neither road, the
//     value pair's own acts and the roster's face calling the free function
//     directly.)
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

// THE ICON ROW'S LEFT PAD — the row's 8px lead-in, and since 2026-08-14 THE
// BOTTOM ROW'S PAD TOO, at both ends and for the modal that displaces its
// tenants (architect: "make sure bottom row is same height and metrics
// (padding, etc.) as main icon row"). It lives in this header rather than in
// the painter's file because that unification gave it a reader outside the row
// — one source, so a retune of the icon row carries to the other by
// construction. (The one-day STATUS BAR read it too, at both ends and for the
// air between its two cells, and took no number of its own with it when it
// folded back into row 8 on 2026-08-29.)
//
// TWO SEPARATELY-MEASURED PADS DIED INTO THIS ONE. bottom_row_pad_x, the
// modal's own accessor, went at the 2026-08-14 ruling: it was a
// separately-measured 13 — the status chain's number, which the modal
// inherited when it landed on this row — while the row's own buttons already
// walked from this 8. status_chain_pad_x, that 13 itself (measured off
// row_7_text.png: fitting the crop's own string offscreen at the row's 16px
// size put the pen at x = 13), went on 2026-08-29 with the chain it aligned:
// the row that took the chain's surviving strings — row 8, through its own
// state cell — reads this 8 like every other redesigned row, so the product
// has one lane pad and no second number.
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
// The OPEN PROJECT prompt's label (File → Open project, 2026-08-27) — ONE WAY TO SHOW
// (The Open project prompt's `Open: <projects_path>/` label and the `h`
// view's `Load: ` label stood here until 2026-08-28, when both prompts lost
// their text fields and became the field-less picker over the folder overlay
// — architect R22/R23; the picker's row is Cancel alone since 2026-08-29 and
// wears no label.)
// The `h` history view's COMMIT-TITLE editor (2026-08-07), whose buffer is the
// message the checkpoint commit will carry. One trailing space, like every
// prefix here that names a subject rather than a path.
constexpr const char* kCommitTitleEditorPrefix = "Commit: ";
// The MEASURE PROPAGATE's paste-offset editor (2026-08-20), whose buffer is the
// signed number of measures to add to every direct measure the paste writes.
// It names its SUBJECT and its UNIT rather than a bare noun, because `0` in a
// field labeled "Offset" says nothing about what is being offset: this dialog
// stands where the phase paste's confirmation prompt stands, so the label has
// to carry the act as well as the question.
constexpr const char* kMeasureOffsetEditorPrefix = "Paste measures, offset: ";

// THE `h` HISTORY MODE'S ONE BRACKET SPELLING — the sign, then the payload
// DIRECTLY AGAINST IT, no space (architect 2026-08-05, superseding the arc's
// original `[+] <payload>`). Defined in waveform_cache.cpp beside its first
// caller and declared here for its SECOND (2026-08-05): ROW 8'S STATE CELL's
// walk line, whose `Scale: [-]<then> [+]<now>` is the same vocabulary at
// another surface, so the two cannot spell the sign differently.
//   * a WARP payload is the tempo token, so this reads `[+]1.05` live and
//     `[+]#1.05` disabled;
//   * a PHASE RESET has no payload — its frame is its whole identity — so it
//     reads the bare `[-]` / `[+]`, with the `#` disable spelling as its one
//     payload when the bit is set (`[+]#`). That is what makes the phase-reset
//     column's CHANGED pair say anything at all: a same-frame change there IS a
//     disable toggle OR a measure edit, so `[-]#` beside `[+]` is the toggle in
//     the file's own spelling, where `[-]` beside `[+]` would carry no
//     information;
//   * the CORNER passes disabled=false and the `scale=` token, the payload arm
//     alone.
//
// `measure` IS THE PHASE COLUMN'S SUFFIX AND ITS ALONE (architect 2026-08-22,
// ruling that the phase halves paint their measure bytes like the warp halves
// always have). Non-empty, it appends the sidecar's own ` //<measure>` — the
// separator bytes included, because THE LANE'S HONESTY RULE IS VERBATIM BYTES
// and the warp halves show that same raw suffix INSIDE their rest-of-line
// tokens; a measureless line appends nothing, so no empty separator can ever
// paint. It is the one spelling of the suffix on this side of the parser
// boundary, mirroring the two sidecar writers' (warpmarkers.cpp,
// phaseresetmarkers.cpp) and the split in marker_measure.h. WHO PASSES IT: the
// three phase-column fills in rebuild_history_diff_flags and nothing else — the
// four WARP fills pass none because a warp token is rest-of-line and already
// carries its own suffix (passing one there would paint it twice), and the
// corner's `Scale:` segment has no measure at all. Defaulted for those six.
// ASCII by construction: the signs are literals and every token and measure
// comes from a sidecar grammar that is ASCII-only.
std::string history_diff_label(const char* sign, bool disabled,
                               const std::string& token,
                               const std::string& measure = {});

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
    //
    // THE FINGERPRINT'S MEMBERS, in full and in one place (the dirty-detect
    // compare in waveform_cache.cpp walks exactly these, and the dispatch,
    // completion-swap and synchronous-publish sites copy exactly these):
    // vp_start, vp_end, area_w, area_h, inset_px, MAGNIFICATION, target, and
    // the warp_frame_map hash. Every one is an input the plate's PIXELS depend
    // on, and each is keyed BY FIELD rather than through whatever else happens
    // to move with it.
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
    // THE VISUAL MAGNIFICATION LEVEL the live pixels were rendered at
    // (app.waveform_magnification_level — the ladder settings_file.h brackets).
    // A FINGERPRINT FIELD in its own right, keyed directly like the inset: it
    // is an input to the tip mapping alone, so nothing else about the plate
    // would move if it changed by itself, and without it a plate rendered at
    // one gain could go on being blitted after the setting moved. THE LEVEL AND
    // NOT ITS GAIN, so the compare is integer. PIXELS ONLY — this cache holds a
    // picture, and the level reaches no sample anywhere.
    int       fp_magnification_level = 0;
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
    int       pending_fp_magnification_level = 0;
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
    int       supersede_magnification_level = 0; // GUI-captured picture level
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
        // (The magnification level needs no poison of its own: area_w = -1 already
        // guarantees the mismatch, and this cache carries the one poison
        // rather than one per field.)
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
// retarget rebuild this surface. THE MEASURE EDITOR HAS THE SAME SHAPE ONE BOX
// SMALLER: its target's MEASURE box alone is skipped here (the flag keeps
// painting) and it has its own fingerprint field, fp_editing_measure_target.
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
    // THE GUI SCALE THESE PIXELS WERE LAID OUT AT, as an integer PERCENT
    // (gui_scale_percent(), render.h). Every dimension of a flag rides the
    // scale — the box, the pole, the label's font size, the measure box's
    // padding — so it is an input to this surface exactly as the viewport and
    // the marker generations are, and it is keyed BY FIELD like the plate's own
    // inset and magnification rather than through whatever else happens to move
    // with it. (fp_area_h does move at every 1 % step on a 1080-px window,
    // because the waveform's 500-px cap and the strip's lanes are all scaled —
    // but that is arithmetic on one window size, not construction; a window
    // geometry where the strip height sat on a floor would leave the whole
    // fingerprint unchanged across a scale commit and blit the old flags.)
    int       fp_gui_scale_percent   = -1;

    long long fp_warp_generation    = -1;
    long long fp_phase_reset_generation   = -1;
    uint64_t  fp_drag_overlay_hash        = 0;
    uint64_t  fp_selection_hash           = 0;
    char      fp_active_markers_view      = '\0';
    // (THE MEASURED-FONT FIELD IS GONE — row 7. It said "the metrics these flag
    // pixels were laid out with", true while flag shapes were monospace-derived;
    // row 5 moved every flag dimension onto the gui_scale axis and left it a
    // recorded vestige, and row 7 deleted the measurement it keyed. What row 5
    // moved those dimensions ONTO is keyed above since 2026-08-29:
    // fp_gui_scale_percent is the axis itself, so the dependence the deleted
    // field used to stand for is a field again, and by construction this time.)
    // ITERATION MODE joined the fingerprint with row 5 (2026-08-01): the flags
    // CARRY TEXT now, and the mode changes what an eligible flag paints — the
    // two bound cells beside it, exactly when this bit is on (the spliced
    // bracket in the text until 2026-09-04). Before row 5 the shapes were
    // textless and the bracket surfaced only in the marker-text lane, a live
    // per-frame pass that needed no fingerprint; `i` damages the top strip but
    // the rebuild is fingerprint-guarded, so without this field the damage
    // would repaint the same cached bytes.
    bool      fp_iteration_mode           = false;
    // THE ADDRESSED CELL (IterStepCell, stored as its integer value): the
    // focused marker's addressed cell wears the cue underline while the mode
    // is on, and a marker press moves the axis without necessarily moving
    // the selection (a re-press of the focused flag's other cell), so the
    // axis is a content fact of this surface in its own right. The marker
    // the cue sits on is the focus, already keyed by fp_selection_hash.
    int       fp_iter_step_cell           = 0;
    // THE MARKER WHOSE FLAG EDITOR IS OPEN, or -1. In the fingerprint because
    // that marker's box is SKIPPED in the cached pass (the open editor paints it
    // unrolled instead), so opening, closing or retargeting the editor changes
    // what this surface must contain. Without it the cache would keep the
    // suppressed frame after the editor closed — and keep the drawn box while it
    // opened. Contract at render_flags' editing_marker_index (render.h).
    int       fp_editing_flag_target      = -1;
    // THE MARKER WHOSE MEASURE EDITOR IS OPEN, or -1 — the sibling of the field
    // above, for the sibling suppression: that marker's MEASURE BOX alone is
    // skipped in the cached pass (its flag keeps painting), the live field
    // taking its place after the blit. A SECOND field rather than a kind flag
    // beside the first, which is what lets the two together distinguish KIND
    // AND TARGET: a payload session reads (i, -1) and a measure session
    // (-1, i). Contract at render_flags' editing_measure_index (render.h).
    int       fp_editing_measure_target   = -1;
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
// THE INVALIDATION KEY IS (width, height, MAGNIFICATION) — the LANE's own
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
// in-session source load; `'` load-in-place replaces the marker stores and
// the engine block, never the sample buffer), so the bars' input cannot change under a
// live process and a per-frame tick repaint never re-reads the pyramid.
// Rebuilds are synchronous at the paint site (O(lane width) with the
// pyramid's unconditional <=5-pairs-per-column bound — the whole-song span is
// exactly what the coarse rungs exist for).
//
// THE MAGNIFICATION JOINED THE KEY 2026-08-26, with the setting itself: ONE
// GAIN ON EVERY WAVEFORM PICTURE, no exception — this 24px band is where a
// quiet passage disappears first, and clipping in a whole-song map costs
// nothing. It is an input to these bars' tip mapping exactly as it is to the
// plate's, so it is keyed BY FIELD beside the two dimensions rather than left
// to ride one of them. PIXELS ONLY: the factor scales this picture and reaches
// no sample anywhere.
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
    // The magnification LEVEL the cached bars were drawn at (the key's third
    // field) — the level and not its gain, so the compare is integer.
    int              magnification_level = 0;
    bool             rendered = false;

    void destroy_surface() {
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        width    = 0;
        height   = 0;
        magnification_level = 0;
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
    // THE TARGET RENDER DISPATCHER, read for ONE fact (2026-08-30): the
    // preview's readiness, which the play/stop button's enabled face asks
    // through the one publisher (publish_button_face) since the
    // truthful-buttons ruling — the painter stashes what it paints and the
    // predicate needs the object. Const here; nothing in the painter drives it.
    const GuiTargetRender& target_render;
    // The synchronization worker, read for one fact: whether a mirror is
    // running right now. Row 8's process line falls back to the mirror's own
    // string while it is, and that fallback is derived here at the reader
    // rather than written into the shared slot at the dispatch
    // (process_line_text, paint_handler.cpp, which is the one site that asks).
    // Const for the same reason target_render above is — nothing in the
    // painter drives it — and is_busy() is a lone atomic load with no lock the
    // worker ever holds across it, so asking it once per frame is free and
    // cannot block on the copying.
    const GuiExternalSyncWorker& external_sync_worker;
    WaveformCache&     wf_cache;
    FlagCache&         flag_cache;
    GuiWaveformWorker& waveform_worker;
    GuiPlatform&       gui;

    GuiPaintHandler(AppState&          app_,
                    const GuiAudio&    audio_,
                    GuiPlayback&       playback_,
                    const GuiTargetRender& target_render_,
                    const GuiExternalSyncWorker& external_sync_worker_,
                    WaveformCache&     wf_cache_,
                    FlagCache&         flag_cache_,
                    GuiWaveformWorker& waveform_worker_,
                    GuiPlatform&       gui_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          target_render(target_render_),
          external_sync_worker(external_sync_worker_),
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
    // (24 fields, RE-DERIVED 2026-08-29 off the compare in
    // maybe_rebuild_flag_cache — other sites state only a pointer here):
    //   - GEOMETRY, four fields off the displayed plate (wf_cache.fp_*):
    //     fp_vp_start, fp_vp_end, fp_target, fp_warp_frame_map_hash;
    //   - GEOMETRY, two fields off the LIVE top strip rect (top_strip_area, not
    //     the plate — this surface mirrors the strip, not the waveform):
    //     fp_area_w, fp_area_h;
    //   - THE SCALE, one field read live off render.h: fp_gui_scale_percent —
    //     every flag dimension rides gui_scale, so the axis is keyed BY FIELD
    //     rather than left to ride whichever strip dimension happens to move
    //     with it (2026-08-29);
    //   - MARKER-DRIVEN, five read live from app state: fp_warp_generation,
    //     fp_phase_reset_generation, fp_drag_overlay_hash, fp_selection_hash,
    //     fp_active_markers_view;
    //   - CONTENT, four more: fp_iteration_mode (it changes what the flags
    //     SHOW — the bound cells), fp_iter_step_cell (which cell of the focus
    //     wears the cue), fp_editing_flag_target (the payload editor's marker,
    //     whose box this pass SKIPS whole) and fp_editing_measure_target (the
    //     measure editor's marker, whose MEASURE BOX alone this pass skips);
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
    // launch load, the preview completion's repaint — stay on the worker;
    // FOLLOW SCROLLING joined this route 2026-09-02 (the vanishing playhead
    // line — the reasoning is at Viewport::follow_scroll_if_needed).
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
        // The waveform PICTURE's magnification LEVEL
        // (app.waveform_magnification_level). BOTH a render input and a
        // fingerprint field, exactly like inset_px above: it feeds the tip
        // mapping and nothing else, so nothing else would move if it changed
        // alone.
        int      magnification_level = 0;
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
    // sampled ground plus the four menu
    // buttons and the view bar), the TAB ROW
    // (top lane 1, row 3: the
    // "A"/"B" Breeze tabs, their frame and its broken border-bottom — the row
    // paints tabs and nothing else since 2026-08-29, when the STATUS CHAIN it
    // had carried under them from 2026-08-13 was deleted for the one-day
    // status bar whose state text is row 8's own cell now),
    // the
    // ICON ROW (top lane 2, row 4: the twenty-eight view/mode/action buttons —
    // the deleted toolbar row's four lead them since the 2026-08-12 relayout,
    // the ITERATION PAIR came back from the menu row on 2026-09-04
    // and the history group's seven close them since 2026-08-18 — their
    // separators and its border-bottom, all of them painted on every frame
    // since 2026-08-14), and the UNIFIED BOTTOM ROW's button
    // cluster (bottom lane 0, the strip's ONE lane, ON THE WINDOW'S FOOT since
    // the relayout's commit B apart from the one day the STATUS BAR stood
    // under it, 2026-08-29:
    // the transport three, the clock and the STATE CELL left, then the marker
    // verbs, the
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
    void paint_icon_row(cairo_t* cr);
    // THE UNIFIED BOTTOM ROW'S BUTTON-AND-CLOCK HALF (rows 8 and 9 merged,
    // 2026-08-12; the arrows flush right since the same day's relayout): the
    // transport three at the left pad, then the right margin's block — the
    // four marker verbs with the COPY VALUE button (2026-08-29), the EDIT FLAG
    // button, the MEASURE and ADD TO SELECTION behind them + separator +
    // marker-walk three + separator + arrow four (2026-08-15 for the walk
    // group, 2026-08-18 for the verbs) — at
    // the icon row's boxes, the
    // monospace clock at its own left-anchored pen behind the transport's
    // separator (centred in the lane until 2026-08-18) and THE STATE CELL
    // beside that clock (2026-08-29, the status bar's fold into this row: the
    // `h` walk line or the render's progress line, at the clock's own
    // separator-to-digits distance and clipped where the right block begins),
    // painted onto the lane paint_bottom_strip has
    // already grounded — that painter is the lane's one chrome owner and the
    // only caller of this body, which keeps the family's fifth button-row
    // painter separate only because the button cluster's tables and the
    // clock's metrics live beside it.
    void paint_bottom_row_buttons_and_clock(cairo_t* cr);
    // (paint_status_bar IS DELETED — architect 2026-08-29, the evening of the
    // day the bar landed. It painted the window's LAST ROW, three bands under
    // two cells of state; the lane went whole and the STATE TEXT is the body
    // above's own cell, right of the clock. The record is at that deletion's
    // note in the .cpp.)

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
    // THE NOTIFICATION CARDS (2026-08-29): the visible stack, top-right under
    // row 1, painted after the flag editor's box and before the dropdown —
    // above every lane and the keyboard slot, below the two pointer-transient
    // floaters and the modal row (the order is on_redraw's step 13). It
    // PUBLISHES each card's rect and X box (AppState::Notifications::painted)
    // for the press claim, the cursor map and the hover walk, and runs on
    // every frame for the floating surfaces' own reason: a skipped run would
    // strand a stale publication. Model and ruling at notifications.h.
    void paint_notifications(cairo_t* cr);
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
    // five modal editors (settings / load / commit-title / measure paste-offset /
    // BPM) while one
    // stands — the row's own painter yields the lane to it. Painted LAST from
    // on_redraw's tail, unconditionally: it publishes AppState::modal_dialog
    // and AppState::dialog_editor_text, the geometry the pointer path's veil,
    // button claims and click-to-caret read. The full design record is at the
    // definition; the field's sampled chrome is at render.h's kModal* block.
    void paint_modal_dialog(cairo_t* cr);

    // THE ON-SCREEN KEYBOARD (2026-08-27), the glass's key surface — full
    // window width, directly above the bottom row, over the waveform area's
    // lower part. It paints AFTER every waveform pass and BEFORE the three
    // floating surfaces, which is where it sits in the picture: the flag editor
    // is above it in the marker lane, a dialog editor below it in the bottom
    // row, and the waveform under it is simply not painted where this paints.
    //
    // GATED WHOLE ON onscreen_keyboard::stands (onscreen_keyboard.h — the
    // layout table, the geometry and the two lamps all live there): this body
    // returns at its head on the laptop, permanently, because that predicate's
    // platform term is false there.
    //
    // AND THEN ON ITS OWN EXPOSURE, exactly as the four redesigned rows are and
    // for their reason: this pass shapes up to nine cap runs and draws up to
    // nine icons per row, which the outer Cairo clip would NOT elide, and a
    // narrow per-frame damage must not pay for them — the caret blink damages
    // the editor's box every half second, and the flag editor is the one
    // keyboard-modal surface that does not stop playback, so the scanner's own
    // clock-cell damage can run at tick cadence underneath it.
    //
    // It publishes no geometry: the press router derives its rects from the
    // same walker this does, so there is no stash for a skipped run to strand.
    // (The slot's as-painted bit is the dispatch's below, since 2026-08-28.)
    void paint_onscreen_keyboard(cairo_t* cr, const GuiRect& exposed);

    // THE KEYBOARD SLOT (2026-08-28): the ONE band above the bottom row that
    // the on-screen keyboard and the FOLDER OVERLAY (folder_overlay.h) share
    // — the overlay replaces the keyboard there, so at most one tenant
    // stands. This dispatch is the slot's one paint entry and THE ONE WRITER
    // of AppState::keyboard_slot_painted_standing (the tick comparator's
    // as-painted bit, main.cpp): it refreshes the bit only on a rect that
    // FULLY COVERS the band — the roster publisher's own rule, for the two
    // edges the keyboard's body used to carry (a show whose route damaged
    // only the marker lane or the bottom row, a hide painted first by an
    // unrelated narrow damage) — then paints whichever tenant stands.
    // NOT exposure-gated, for the bit's sake: it runs on EVERY frame class
    // (one platform query and two bit reads with the slot down).
    void paint_keyboard_slot(cairo_t* cr, const GuiRect& exposed);

    // THE FOLDER OVERLAY (2026-08-28, the render player): the keyboard-slot
    // list panel — folder / wav / up rows with their Breeze glyphs, the
    // highlight band, the hover and pressed faces, the ring's focus outline,
    // and the transport glyph on the playing item's row, all under the band's
    // clip and the live scroll offset. Gated whole on folder_overlay::stands
    // and then on its own exposure per row, as the keyboard is. It publishes
    // no geometry: the press router walks the same row table through the same
    // walker (folder_overlay::for_each_row / row_at).
    void paint_folder_overlay(cairo_t* cr, const GuiRect& exposed);
};
