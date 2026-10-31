#pragma once

#include "app_state.h"
#include "audio.h"
#include "materials.h"   // GuiKeySeat, program_key_seats' element
#include "playback.h"
#include "render.h"
#include "platform.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment

#include <cairo/cairo.h>
#include <array>
#include <string>
#include <string_view>
#include <vector>


class GuiCanvasSink;        // canvas_sink.h
struct GuiCanvasDrawList;   // canvas_sink.h
class GuiWaveformWorker;
struct GuiTargetRender;

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

// THE PROGRAM'S KEYS, ONE LIST FOR THE MATERIALS' BAKE (2026-10-21; the
// key sizes the KEY PATCHES are baked for, materials.h's KEYS): every key
// size the geometry lays out — the band's and row 8's IN BOTH STATES OF THE
// `h` VIEW (its hide and stand-ins move the band's left runs and row 8's
// block), the controls row's key (the render player's nine and every
// word button), the scrub's thumb and, on a platform with the on-screen
// keyboard, every key of its two pages — each a seat of its size, its lamp
// and the depths it can stand at, one seat a size, each row's own layout
// function the painter reads too (paint_handler.cpp: icon_row_seats and
// icon_row_walk, row8_block and row8_block_walk, modal_controls_seats,
// onscreen_keyboard::for_each_key). A function of the client size, the
// scale, the key's height and the keyboard's key box, which are in the
// bake's key; the sizes alone are read, never a position.
std::vector<GuiKeySeat> program_key_seats(const AppState& app);
// THE LOGO LANE'S LEGEND RECT for `legend` at this geometry (render.h's
// logo lane block; paint_handler.cpp's logo_legend_layout, the painter's own
// layout): the lane's rows from the legend's first ink column through the
// program ring's outer right column and the shadow's offset past it (the
// clock's shadow, 2026-10-28). The tick's damage for a changed legend
// (main.cpp's refresh_logo_legend) is this rect united with the one the
// painter last published (AppState::logo_legend_rect).
GuiRect logo_legend_extent(const AppState& app, const GuiLogoLegend& legend);
// (THE CAPTION'S THREE BUTTONS' RECTS, caption_button_rects, went with the
// caption, 2026-10-24.)

// -- Constants used by paint code ----------------------------------------
//
// Declared here so paint_handler.cpp can reach them. No pointer-side grab
// tolerance survives (the marker stems' died with their pointer surface,
// 2026-08-12, the trim endcaps' with the arrow buttons, 2026-10-03);
// gui_font(face), the faces at the live scale, lives in render.h, so
// render.cpp can reach it without pulling paint_handler.h into the
// lower-layer include graph.

// (THE MODAL ROW'S PAD — icon_row_pad_x, the bottom row's 8-W lead-in and
// lead-out, the icon row's pad from 2026-08-14 — left with its last reader,
// row 8's seats, 2026-10-22: the strips' ends and the modal controls row's
// are the program ring's outer columns, render.h's program_ring_columns,
// both strips flush with them since 2026-10-23.)

// NO DIALOG EDITOR CARRIES A LABEL (2026-10-14): the Settings editor's
// "Setting:" ("pointless that far away. just remove it") and the BPM editor's
// "BPM:" ("drop") are both removed; the box shows its line alone. (Each
// dialog editor's label stood at the bottom row's left pad beside its inset
// field 2026-08-12 to 2026-10-14, the bottom-strip prefixes from row 7
// before; the `h` view's commit-title editor's "Commit:" beside them
// 2026-08-07 to 2026-10-10.)
// The OPEN PROJECT prompt's label (Open Project, 2026-08-27) — ONE WAY TO SHOW
// (The Open project prompt's `Open: <projects_path>/` label and the `h`
// view's `Load: ` label stood here until 2026-08-28, when both prompts lost
// their text fields and became the field-less picker over the folder overlay
// — architect 2026-08-28; the picker's row is Cancel alone since 2026-08-29 and
// wears no label.)

// THE `h` HISTORY LANE'S ONE LABEL SPELLING — THE LINE'S OWN TEXT AS THE
// LIVE LANE SHOWS IT, NO SIGN (architect 2026-10-26 ~15:30: "we could also
// remove the brackets plus minus, because that becomes redundant at that
// point. And those are always hard to see anyways at the font size. A
// strike through is easier to see"): the removed half tells itself by its
// STRUCK label (resolve_flag_face's table), the added by its plain one. The
// disable byte `#` leads where the line's own '#' is set, the line's own
// byte. Defined in waveform_cache.cpp beside its one caller. THE FOUR LINE
// KINDS:
//   * a WARP line's payload is its tempo token: `1.05` live, `#1.05`
//     disabled;
//   * a PHASE RESET has no payload — its frame is its whole identity — so it
//     reads the live lane's display token (kPhaseResetLaneToken, render.h):
//     `reset` live, `#reset` disabled. A phase-reset same-frame CHANGED pair
//     IS a disable toggle, so `#reset` (struck) beside `reset` — or the
//     reverse — is the toggle in the file's own spelling.
// (Row 8's walk line spells its own `Scale: [-]<then> [+]<now>`,
// history_walk_line in paint_handler.cpp: a struck run has no road there.)
//
// A warp token is rest-of-line, the whole payload. ASCII by construction:
// every token comes from a sidecar grammar that is ASCII-only.
std::string history_diff_label(bool disabled, std::string_view token);

// -- The waveform plate's cache ------------------------------------------
//
// Lives for the life of the project set (run_project, main.cpp); re-rendered
// when any input to the plate's render (render_waveform_columns) has
// changed. THE PLATE IS COLUMN BOUNDS (2026-10-13; GuiPlateColumns, render.h):
// the two bands' per-column quarter-row tips, no pixel kept on the CPU — the
// frame's draw list carries them (GuiCanvasSink::plate) and the GPU
// rasterizes the antialiased plate from them (kListFragmentShader,
// 2026-10-30); the redraw path paints markers / flags /
// playhead / timestamp around them.
struct WaveformCache {
    int              width   = 0;     // the live plate's width  (== area.w when valid)
    int              height  = 0;     // the live plate's height (== area.h when valid)

    // THE PLATE'S TWO SLOTS: the live `columns` and the worker's
    // `pending_columns` — the worker writes the pending slot, the completion
    // swaps the two, the synchronous rebuild writes the live one after the
    // drain — width / height and pending_width / pending_height standing for
    // them.
    GuiPlateColumns columns;
    GuiPlateColumns pending_columns;

    // Fingerprint of the LIVE plate (what the next frame will draw). Set
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
    // vp_start, vp_end, area_w, area_h, divider_px, the plate's two
    // INKS, the GAIN field, target, and the warp_frame_map hash. Every one is
    // an input the plate's PIXELS depend on (the inks through its
    // publication), and each is keyed BY FIELD rather than through whatever else happens
    // to move with it. THE AREA'S TWO DIMENSIONS AND THE STEREO DIVIDER'S
    // HEIGHT ARE THE PLATE'S WHOLE LAYOUT (the inset gone 2026-10-09, the
    // divider 2026-10-16): the channels' bands are the area less the divider,
    // halved (waveform_channel_band, render.h). (The inset was a field of its
    // own until 2026-10-09, and the measured MONOSPACE font size before row
    // 7, its proxy while it was font-derived.)
    int64_t   fp_vp_start    = 0;
    int64_t   fp_vp_end      = 0;
    int       fp_area_w      = 0;
    int       fp_area_h      = 0;
    // THE STEREO DIVIDER'S HEIGHT the live pixels were laid out with
    // (waveform_divider_px(), render.h): channel 1's band starts under it, so
    // a scale that moves it re-renders the plate BY FIELD, keyed directly. A
    // fingerprint field exists iff it is a render input (the type rule's
    // shape): the lit outline's erosion distance, the GPU shader's constant,
    // is neither.
    int       fp_divider_px  = -1;
    // THE TWO INKS the live plate is published with (the `waveform_ink` and
    // `waveform_outline` roles, waveform_plate_inks, render.h): the sink
    // writes their words (GuiCanvasSink::plate), so an install that moves
    // either (the settings page's `waveform` commit, install_program_palette,
    // palette_file.h) republishes the plate BY FIELD, keyed directly like the
    // divider's height (architect 2026-10-03; palette roles since 2026-10-07).
    WaveformPlateInks fp_inks{};
    // THE GAIN FIELD the live pixels were rendered under — the derived
    // curve's version where the picture is magnified, 0 where the gate answers
    // flat (waveform_gain_fingerprint, warp_frame_map_view.h, which owns that
    // rule). A FINGERPRINT FIELD in its own right, keyed directly like the
    // divider's height: the gain is an input to the tip mapping alone, so nothing else
    // about the plate would move if it changed by itself (the magnification
    // lamp moves no map), and without it a plate rendered at one gain could go
    // on being drawn after the gain changed. The field alone is enough to
    // re-render — no basis freeze and no map term rides with it. PIXELS ONLY —
    // this cache holds a picture, and the gain reaches no sample anywhere.
    uint64_t  fp_gain_hash = 0;
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

    // Pending-slot size and fingerprint. The worker renders into
    // pending_columns; the completion handler swaps it into columns and
    // copies pending_fp_* into fp_*. While a render is in flight,
    // pending_fp_* describes what the worker is producing — dirty-detect
    // compares against pending_fp_* (not fp_*) so we don't enqueue a
    // second render for the same target the worker is already working on.
    int              pending_width   = 0;
    int              pending_height  = 0;

    int64_t   pending_fp_vp_start    = 0;
    int64_t   pending_fp_vp_end      = 0;
    int       pending_fp_area_w      = 0;
    int       pending_fp_area_h      = 0;
    int       pending_fp_divider_px  = -1;
    WaveformPlateInks pending_fp_inks{};
    uint64_t  pending_fp_gain_hash = 0;
    bool      pending_fp_target      = false;
    uint64_t  pending_fp_warp_frame_map_hash = 0;

    // The warp_frame_map the in-flight job is consuming. Set at
    // dispatch alongside the other pending_fp_*; swapped into fp_warp_frame_map
    // at completion.
    std::vector<WarpFrameMapSegment> pending_fp_warp_frame_map;

    // Supersede slot: when dirty-detect sees a new viewport mid-render,
    // it stashes the desired fingerprint here instead of dispatching.
    // The completion handler consumes it — if set, the just-completed
    // pending bounds are discarded (they will be overwritten by the next
    // render) and a fresh job built from supersede_* is dispatched.
    // Cleared at consumption.
    bool      supersede             = false;
    int64_t   supersede_vp_start    = 0;
    int64_t   supersede_vp_end      = 0;
    double    supersede_painter_spp = 0.0;  // the lattice q, like the job's
    int       supersede_area_w      = 0;
    int       supersede_area_h      = 0;
    int       supersede_divider_px  = 0;   // GUI-captured divider height
    WaveformPlateInks supersede_inks{};    // GUI-captured plate inks
    uint64_t  supersede_gain_hash   = 0;   // GUI-captured gain field
    bool      supersede_target      = false;
    uint64_t  supersede_warp_frame_map_hash = 0;
    std::vector<WarpFrameMapSegment> supersede_warp_frame_map;
    // The superseding job always reads the project's one source audio
    // (WaveformJob.audio), which is fixed for this cache's whole lifetime, so
    // the slot carries no audio pointer or keepalive — the deferred redispatch
    // just names &audio.

    void forget_plate() {
        columns.rendered         = false;
        pending_columns.rendered = false;
        width  = 0;
        height = 0;
        pending_width  = 0;
        pending_height = 0;
        fp_rendered = false;
        // Poison the pending fingerprint so the next maybe_enqueue tick sees a
        // guaranteed mismatch and re-dispatches — area_w = -1 is impossible for
        // any valid render (compute_waveform_render_inputs rejects area.w <= 0).
        pending_fp_area_w = -1;
        // (The gain field needs no poison of its own: area_w = -1 already
        // guarantees the mismatch, and this cache carries the one poison
        // rather than one per field.)
        supersede = false;
        supersede_warp_frame_map.clear();
        fp_warp_frame_map.clear();
        pending_fp_warp_frame_map.clear();
    }

    ~WaveformCache() { forget_plate(); }
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
// for the live trim pass); the paint pass is a pure blit. THE EDITED BOX IS
// SKIPPED IN THIS CACHE (2026-08-02): an open marker-lane field renders live as
// an overlay after the blit, and it does NOT reliably cover the committed box it
// replaces — a shortened payload makes the overlay the narrower of the two — so
// the cached pass omits that box, its label and its hit rect outright and the
// editor owns it whole. SINCE 2026-09-05 THAT IS ONE RULE FOR ALL THREE KINDS
// (SuppressedBox, render.h): the pass omits the edited box AND everything right
// of it, those boxes riding the field's edge under the editor's own painter, so
// a payload field takes its marker's whole column and a bound field takes its
// cell and whatever follows. The
// suppression is a fingerprint field in both its halves
// (fp_suppressed_box_index, fp_suppressed_box_cell) precisely so open, close,
// retarget and a move between one marker's own cells all rebuild this surface.
//
// THE SURFACE HOLDS THE ROWS ITS INK CAN REACH (2026-10-13): the window's
// width, and the client rows from THE RULER LANE'S TOP (top_ruler_row_area's
// y, `origin_y`) through the top strip's bottom (the canvas's top frame row's
// foot) — the ruler lane, the marker lane and the frame row, 36 + 51 + 1 =
// 88 device rows at 300 % where the whole strip from row 0 is 289
// (re-derived 2026-10-28, the frame row one device px). Every cue pixel is
// inside the marker lane's rows BY THE PAINTERS' OWN CLIPS, not by a bound on
// the font: in paint_cues (render.cpp) every label's face box and its text,
// shadow and ink alike, are painted under a clip of the label's span
// rectangles, each the lane's top row and cue_fill_h_px() tall (the marker
// lane's composite height), so no cached label pixel leaves those rows
// however far a glyph overshoots; the triangles stand on the lane's own rows
// under a clip of those rows (paint_ce_cue_triangle's cells,
// cool_edit_paint.h; paint_cues), so the shadow's offset down, one label
// quantum since 2026-10-26 ~13:05, is cut at the lane's last row and never
// lands on the frame row; and the flag editor and its riding cells paint
// later onto the frame, never into this surface. The ruler lane above is in
// the band as its clean upper boundary, with margin, not because anything
// there is inked by a cue; the frame row is the strip's own foot.
// THE PAINTERS STILL WORK IN CLIENT COORDINATES: the rebuild translates its
// context by -origin_y and hands each pass the band the surface holds as its
// `top_strip_area`, so every published hit rect and stem is client-coordinate
// as it was, and each pass's own clip (clip_to_waveform_columns over that
// band's rows) bounds its ink to the surface. The blit
// (paint_flag_annotations) lands the surface at (top_strip.x, origin_y),
// clipped to the top strip — AND ONLY WHILE ITS BAND IS THE LIVE BAND
// (2026-10-14): origin_y and height equal to the ruler lane's live top and
// the rows from it through the strip's bottom. A gui_scale commit moves the
// band before the next tick rebuilds the surface, and a surface blitted at
// its old origin would stamp the old scale's cues into the toolbar band,
// above every lane the rebuild's damage repaints; the reasoning is at the
// blit.
// IDENTITY RESTS ON (2026-10-13) — the bytes are the whole-strip surface's
// of before, by construction: the same painters paint the same
// client-coordinate geometry through an integral translation (cairo ignores
// a translation in the font's ctm, and the platform hands on_redraw an
// untranslated context; the direct road,
// direct_pixel_target, carries the translation and the clip list through);
// each pass's clip loses only rows above the marker lane, which its own
// span clip (paint_cues) already excluded; and the blit puts the same bytes on the same client rows — the
// rows above origin_y held only transparent pixels, which the OVER blit left
// untouched.
struct FlagCache {
    cairo_surface_t* surface = nullptr;
    int              width   = 0;
    int              height  = 0;
    // THE SURFACE'S ORIGIN (2026-10-13): the first client row it holds, the
    // ruler lane's top — row 0 of the surface is this client row. Written at
    // every rebuild with the surface it describes; read by the blit, which
    // refuses a surface whose origin or height is not the live band's. A
    // function of the scale alone (the lanes above the ruler are scaled
    // lengths and gap 1 is zero, the top lanes outweighing the bottom's), so
    // fp_gui_scale_percent keys it and the rebuild asserts as much at a match.
    int              origin_y = 0;

    int64_t   fp_vp_start            = 0;
    int64_t   fp_vp_end              = 0;
    int       fp_area_w              = 0;
    int       fp_area_h              = 0;
    bool      fp_target              = false;
    uint64_t  fp_warp_frame_map_hash        = 0;
    // THE GUI SCALE THESE PIXELS WERE LAID OUT AT, as an integer PERCENT
    // (gui_scale_percent(), render.h). Every dimension of a flag rides the
    // scale — the box, the pole, the label's font size, the cells'
    // padding — so it is an input to this surface exactly as the viewport and
    // the marker generations are, and it is keyed BY FIELD like the plate's own
    // line width and gain field rather than through whatever else happens to move
    // with it. (fp_area_h does move at every 1 % step on a 1080-px window,
    // because the waveform's 500-px cap and the strip's lanes are all scaled —
    // but that is arithmetic on one window size, not construction; a window
    // geometry where the strip height sat on a floor would leave the whole
    // fingerprint unchanged across a scale commit and blit the old flags.)
    int       fp_gui_scale_percent   = -1;
    // THE PALETTE THESE PIXELS WERE PAINTED IN (palette_generation(),
    // render.h — architect 2026-10-03; the palette's roles since
    // 2026-10-07): every flag pixel is a palette color (the cue triangle and
    // its shadow, the faded triangle and shadow of a disabled cue, the label
    // inks), so an install (the Settings color rows' commit,
    // install_program_palette) re-renders the surface BY FIELD, the scale's
    // shape. (No axis or side colors a cue, so no color needs a field beyond
    // this generation: fp_active_markers_view keys the lane's CONTENT — which
    // column's markers, and which column's half of the history delta — not a
    // color.)
    uint64_t  fp_palette_generation  = 0;

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
    // IT HOLDS THE COLUMN'S VERDICT since 2026-09-10 (iteration_column_lit
    // asked of the painted column, waveform_cache.cpp), not the bare mode bit:
    // the mode is lit for ONE column — the one the user is standing in, the
    // switch being locked while the lamp is on — and the other paints no
    // cells, so this field says what this surface shows.
    bool      fp_iteration_mode           = false;
    // THE ADDRESSED CELL (MarkerCell, stored as its integer value): the
    // focused marker's addressed cell is the one that wears the selected
    // pair, and a marker press moves the axis without necessarily moving
    // the selection (a re-press of the focused flag's other cell), so the
    // axis is a content fact of this surface in its own right. The marker
    // the bright cell sits on is the focus, already keyed by
    // fp_selection_hash.
    int       fp_addressed_cell           = 0;
    // THE STANDING MARKER-LANE EDITOR'S SUPPRESSION — WHICH MARKER and WHICH
    // OF ITS BOXES the cached pass does not paint (SuppressedBox, render.h,
    // where the one model is stated). Both halves are content facts of this
    // surface: opening, closing or retargeting an editor changes which boxes
    // the pass must contain, and so does moving between a marker's own cells,
    // since a lower-bound field and an upper-bound field on the SAME marker
    // suppress different boxes. Without them the cache would keep the
    // suppressed frame after the editor closed — and keep the drawn box while
    // it opened.
    //
    // ONE FACT IN TWO FIELDS, replacing the two independent target indices
    // this carried before the bound editors joined on 2026-09-05
    // and a bare index could not have said WHICH cell. The value is the one
    // derivation's (suppressed_flag_box), read here and by the painter alike.
    int       fp_suppressed_box_index     = -1;
    int       fp_suppressed_box_cell      = 0;
    // THE HISTORY MODE'S SIX INPUTS (the `h` view — AppState::HistoryMode).
    // While it stands this surface carries the shown commit's DELTA instead of
    // any live marker, so what it must contain is decided by: whether the mode
    // stands at all (entering and leaving both swap the whole lane), WHICH
    // commit is shown (`,` / `.` step it), WHICH READING of it is shown (the two
    // compare modes, the Cumulative toggle switching them), which diff flag holds the mode's
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
        width    = 0;
        height   = 0;
        origin_y = 0;
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
    // THE TARGET RENDER DISPATCHER, read for ONE fact (2026-08-30): the
    // preview's readiness, which the play/stop button's enabled face asks
    // through the one publisher (publish_button_face) since the
    // truthful-buttons ruling — the painter stashes what it paints and the
    // predicate needs the object. Const here; nothing in the painter drives it.
    const GuiTargetRender& target_render;
    WaveformCache&     wf_cache;
    FlagCache&         flag_cache;
    GuiWaveformWorker& waveform_worker;
    GuiPlatform&       gui;

    GuiPaintHandler(AppState&          app_,
                    const GuiAudio&    audio_,
                    GuiPlayback&       playback_,
                    const GuiTargetRender& target_render_,
                    WaveformCache&     wf_cache_,
                    FlagCache&         flag_cache_,
                    GuiWaveformWorker& waveform_worker_,
                    GuiPlatform&       gui_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          target_render(target_render_),
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

    // THE OPEN'S FIRST PICTURE IS COMPLETE (the presentation hold, platform.h's
    // head; architect 2026-10-27, "withhold the painting until it's ready to
    // paint the whole thing"): the live plate is PUBLISHED and is THE LIVE
    // VIEW'S — a worker completion or a synchronous rebuild has published
    // (fp_rendered), no rebuild is owed, and every fingerprint field of the
    // published plate equals what compute_waveform_render_inputs answers for
    // the live state now (the fingerprint's members, the roster at the fp_ fields above, which
    // maybe_enqueue_waveform_render compares), so no plate is missing, stale or still on the worker. Its one
    // caller is the loop's tick, asked only while the hold stands and only
    // once the project's load has returned (which applied the restored
    // camera, tab and views — the load's own tail); the caller pays an owed
    // rebuild first (flush_owed_waveform_rebuild), since a hold paints no
    // frame whose pre-paint hook would pay it. WHY THE PLATE ALONE IS THE SET,
    // each other part of the first picture checked against it (2026-10-27):
    //   * THE MARKER LANE: the flag cache rebuilds INLINE at both plate
    //     publishes against the fingerprint just published, and the tick runs
    //     its fingerprint-guarded rebuild ahead of this ask, so the lane is the
    //     plate's by construction; the displayed hit map it stages is promoted
    //     by the releasing frame's on_redraw.
    //   * A TARGET-VIEW OPEN: the plate's map is the warp map this predicate's
    //     own inputs build synchronously (the memoized target map), so the
    //     plate it waits for is the target picture; the target RENDER the
    //     load dispatches feeds playback and row 8's "Updating..." state, no
    //     pixel of the plate.
    //   * THE GAIN: the magnification lamp is dark at every open (AppState::
    //     show_waveform_magnification), so the field is 0 and the gain
    //     derivation's thread delays nothing. THE PEAKS PYRAMID is built
    //     inside the load (GuiAudio::load), before this can be asked.
    //   * THE SCORE PAGE: the viewer is closed at every open (its opener is
    //     the one writer of its standing bit); its pages decode when it opens.
    //   * THE MATERIALS: the process's first bake runs in the hold's one
    //     frame's pre-paint hook (platform.h's head), ahead of the load
    //     (materials.h's BAKE RULE); a bake's palette reinstall moves no
    //     plate ink (the inks are the preset's words, not the ground's).
    //   * NO WAVEFORM ROWS (2026-10-28, Sol's round): where waveform_area has
    //     no rows or no columns (compute_waveform_render_inputs's empty-area
    //     arm; a schema-legal lane stack past the window, waveform_area's
    //     owner comment), no route ever renders a plate, so none is owed:
    //     the predicate answers TRUE and the clipped picture is complete.
    // What may still change after the first picture is STATE, never a part of
    // the picture arriving: the GitHub check's and the history prefetch's
    // verdicts, the score soundtrack's readiness and the target render's
    // completion each move a face or row 8's line when they land.
    bool live_plate_published() const;

    // Dirty-detect for the flag-rect cache. Called from on_tick
    // AFTER maybe_enqueue_waveform_render so both layers (waveform,
    // flags) key off the same wf_cache.fp_* and snap together at the
    // waveform's completion swap. THE ONE AUTHORITATIVE FINGERPRINT FIELD LIST
    // (26 fields, RE-DERIVED 2026-10-26 off the compare in
    // maybe_rebuild_flag_cache — other sites state only a pointer here):
    //   - GEOMETRY, four fields off the displayed plate (wf_cache.fp_*):
    //     fp_vp_start, fp_vp_end, fp_target, fp_warp_frame_map_hash;
    //   - GEOMETRY, two fields off the LIVE surface band (not the plate —
    //     this surface holds strip rows, not the waveform): fp_area_w the
    //     window's width (top_strip_area's), fp_area_h the band's height, the
    //     ruler lane's top through the top strip's bottom (2026-10-13,
    //     FlagCache's head; its origin keyed through fp_gui_scale_percent);
    //   - THE SCALE, one field read live off render.h: fp_gui_scale_percent —
    //     every flag dimension rides gui_scale, so the axis is keyed BY FIELD
    //     rather than left to ride whichever strip dimension happens to move
    //     with it (2026-08-29) — the cue triangle's rows and the Program
    //     face's size among them, constants of the scale since 2026-10-26
    //     ~13:05 (render.h's cue_triangle_rows, gui_font.h's table), so the
    //     cues' tuning field of that day left with its keys;
    //   - THE PALETTE, read live: fp_palette_generation (render.h's
    //     palette_generation — every flag pixel a palette color, 2026-10-03);
    //   - MARKER-DRIVEN, five read live from app state: fp_warp_generation,
    //     fp_phase_reset_generation, fp_drag_overlay_hash,
    //     fp_selection_hash, fp_active_markers_view;
    //   - CONTENT, four more: fp_iteration_mode (it changes what the flags
    //     SHOW — the bound cells), fp_addressed_cell (which cell of the focus
    //     is the bright one), and the standing editor's suppression in its two
    //     halves, fp_suppressed_box_index and fp_suppressed_box_cell (which
    //     marker, and which of its boxes this pass skips along with everything
    //     right of it);
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
    // FOLLOW'S PAGE TURN joined this route 2026-09-02 (the vanishing
    // playhead line — the reasoning is at Viewport::follow_scroll_if_needed).
    // ONE PER FRAME since 2026-10-13: a kick OWES it (owe_waveform_rebuild)
    // and the pre-paint hook runs it (flush_owed_waveform_rebuild); a run by
    // any road discharges the debt. The rule is at Viewport::kick_waveform_sync.
    void force_synchronous_waveform_rebuild();

    // THE OWED REBUILD (architect 2026-10-13; the rule and its flush sites at
    // Viewport::kick_waveform_sync's declaration, viewport.h).
    // owe_waveform_rebuild: kick_waveform_sync's road (the callback wired in
    // main.cpp) — marks the synchronous rebuild owed and declares the rect the
    // rebuild declares (the window's top through the waveform's bottom,
    // damage_window_top_rows), so the frame that runs it is scheduled whether
    // or not the kicking route damaged anything of its own.
    // flush_owed_waveform_rebuild: runs the owed rebuild now, a no-op when
    // none is owed — the pre-paint hook's tail (main.cpp), and the event-time
    // readers of what the rebuild publishes, each naming its read at its site.
    void owe_waveform_rebuild();
    void flush_owed_waveform_rebuild();

    // THE PRE-PAINT CHECK OF WHAT THE NARROWED ROUTES OWE (2026-10-13): the
    // rows above the viewport's lanes and row 8's clock cell, each repainted
    // only where the live fingerprint has moved past what the target shows.
    // Called at the end of the pre-paint hook (main.cpp); the rule is at the
    // definition (paint_handler.cpp), the contract at GuiUpperRowsFingerprint
    // (app_state.h).
    void settle_owed_lane_damage();

    // THE GL CANVAS'S FRAME BOUNDARY (2026-10-13, canvas_sink.h): clears the
    // two per-frame bits on_redraw's canvas block keeps — the list's clip and
    // floater bit written, the list built — so the next frame's first damage
    // rect writes them again and its first rect meeting the canvas's paint
    // area builds the list. Called from the pre-paint hook (main.cpp), the
    // one seat that opens every painted frame.
    void begin_canvas_frame();

    // THE PLATE PAINT BASIS: vp_start and samples-per-pixel LOCKED
    // to the published plate (wf_cache.fp_*) while the worker rebuilds against a
    // viewport change, so every live overlay stays
    // registered with the cached pixels instead of the not-yet-painted live
    // viewport. This is the ONE authoritative enumeration of the
    // PLATE-REGISTERED overlays (re-derived by grep over this accessor's
    // callers, 2026-08-02; the region ground and ink left 2026-10-03): the
    // phase-reset ring (through
    // phase_reset_overlay_band), the playhead head (the ruler pass), the
    // cursor playhead, the scanner — plus its two per-frame narrow damage sites in
    // main.cpp — and the strip-drag anchor; and ONE INPUT READER (architect
    // 2026-09-24, strictly as painted), the waveform-lane playhead step's
    // hold, which takes the cursor's prior column as the cursor pass painted
    // it (GuiInputHandler::run_waveform_lane_playhead_step, re-derived by
    // grep 2026-09-24). Other sites state only their own
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
    // which basis the selected stem's damage rode. The two owners stay
    // distinct (the mechanism/lifecycle split is stated at
    // item_viewport_basis); only the names changed.
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
    // THE DEBT (2026-10-13): set by owe_waveform_rebuild, cleared at the head
    // of force_synchronous_waveform_rebuild by whichever road runs it. While it
    // stands the worker's dirty-detect stands down (maybe_enqueue_waveform_render's
    // head): the owed rebuild publishes the live fingerprint before the next
    // paint, so a job dispatched in between would only be drained unfinished.
    bool waveform_rebuild_owed = false;

    // THE GL CANVAS'S PER-FRAME BITS (begin_canvas_frame above, on_redraw's
    // canvas block): the frame's list clip and floater bit written, and the
    // frame's list built.
    bool canvas_frame_seen = false;
    bool canvas_list_built = false;
    // THE CANVAS PAINT AREA'S KNOWN-CLEAR RECORD (2026-10-14; the rule at
    // on_redraw's canvas block, where the CLEAR it skips stands). Between
    // painted frames: `canvas_clear_known` — every backbuffer pixel of
    // `canvas_clear_area` (the last painted frame's paint area, client px) is
    // 0. Through a frame, the summary the next frame's first rect folds into
    // it: `canvas_clear_open` (a painted frame's summary is pending), the
    // frame's decision `canvas_clear_skip`, whether a rect of it covered the
    // area whole (`canvas_clear_covered`), and whether a floating surface
    // stood over it (`canvas_clear_floater`, the frame's floater bit).
    // on_resize forgets it.
    bool    canvas_clear_known   = false;
    GuiRect canvas_clear_area{0, 0, 0, 0};
    bool    canvas_clear_open    = false;
    bool    canvas_clear_skip    = false;
    bool    canvas_clear_covered = false;
    bool    canvas_clear_floater = false;
    // The frame's draw list from the canvas passes, through the sink, in
    // on_redraw's paint order (the definition, paint_handler.cpp).
    void build_canvas_list(GuiCanvasDrawList& frame, bool plate_branch);

    // The two as-painted fingerprints and the paint's half of their record
    // (settle_owed_lane_damage's block, paint_handler.cpp).
    GuiUpperRowsFingerprint upper_rows_fingerprint();
    GuiClockFingerprint     clock_fingerprint();
    void note_upper_rows_painted(const GuiRect& exposed);

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
        // The stereo divider's height (waveform_divider_px(), render.h),
        // captured on the GUI thread beside area_w/area_h so the worker
        // render reads no scale state (the GUI thread mutates that without
        // draining jobs): channel 1's band starts under it, so it is both a
        // render input and a fingerprint field; the area's dimensions and
        // divider_px are the plate's whole layout.
        int      divider_px    = 0;
        // The plate's two INKS (waveform_plate_inks), captured the same way
        // and for the same reason: the plate bakes their words, so they are
        // both render inputs and a fingerprint field.
        WaveformPlateInks inks{};
        // The waveform PICTURE's gain field (waveform_gain_fingerprint): nonzero
        // means apply the audio's derived curve. It is both the render input
        // and the fingerprint field, exactly like divider_px above: it feeds the
        // tip mapping and nothing else, so nothing else would move if it
        // changed alone.
        uint64_t gain_hash = 0;
        bool     is_target     = false;
        uint64_t warp_frame_map_hash  = 0;
        // The translation map: the target-view map in target view, empty in
        // source view.
        std::vector<WarpFrameMapSegment> warp_frame_map;
        // The audio the plate reads from: always the project's one source
        // audio, fixed for this painter's whole lifetime. Set by
        // compute_waveform_render_inputs; routed into WaveformJob.audio and
        // into the synchronous render path.
        const GuiAudio* audio = nullptr;
        bool     valid         = false;        // false if degenerate / loading
    };

    WaveformRenderInputs compute_waveform_render_inputs() const;

    // (The out-of-trim DIM and its two private helpers — compute_displayed_trim
    // and compute_out_of_trim_rects — are retired wholesale with the opaque
    // recolor model, architect 2026-07-26: TRIM recolors no plate pixel, the
    // trim bar spanning the window being the whole inside-the-window signal.
    // Neither helper had any other consumer, so both went with the pass.)

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
        bool   valid  = false;
        double x0     = 0.0;   // left screen x, clipped to the area
        double x1     = 0.0;   // right screen x, exclusive, clipped
        // The reset's own column in screen x, UNCLIPPED (left of x0 when the
        // stem stands off the area's left edge): the origin the ring's top
        // and bottom dot runs are phased from, so the pattern stays put on
        // the song as the view pans.
        double stem_x = 0.0;
    };
    PhaseResetOverlayBand phase_reset_overlay_band(const GuiRect& area) const;

    // on_redraw paint passes. Each renders one strip/layer; on_redraw keeps
    // the rects_intersect gates and calls these in place.
    void paint_flag_annotations(cairo_t* cr, const GuiRect& top_strip);
    // THE RULER LANE (top lane 5): Cool Edit's flipped ruler — its ground and
    // bottom line, the timestamp ladder's ticks and digits and the playhead's
    // head (the `playhead_stem` role) — and the MARKER LANE'S panel face under the cues the flag
    // blit lays over it. Reads the DISPLAYED plate basis, so it re-derives on
    // every pan/zoom along with the strip content it is painted beside.
    // PUBLISHES the head's hit as painted (AppState::playhead_head_hit, the
    // head drag's target, 2026-10-09).
    void paint_ruler_row(cairo_t* cr);
    // THE TWO REDESIGNED BUTTON ROWS (the MENU ROW, top lane 1, went
    // 2026-10-21) — the ICON ROW (top lane 1 since 2026-10-22, row 4: the
    // thirty-one view/mode/action buttons (re-counted 2026-10-29 off
    // kIconRowButtons, the four stand-ins and the right run), no border of its
    // own since 2026-10-01, twenty-seven standing outside the `h` view and
    // eighteen inside it — kIconRowHistoryStandIns since 2026-10-05, the hide,
    // history_mode_hides_button, since 2026-10-07), and the
    // UNIFIED BOTTOM ROW's button cluster (bottom_row_area, over the border
    // under row 8 on the window's foot: the clock cell and the STATE
    // CELL left, then THE TWO SKIPS (Go to Start / Go to End, back since
    // 2026-10-24), the marker verbs, the step and walk (Down, Up, Previous
    // Marker, Next Marker) and the nudge and play (Switch Tab, Center, Left,
    // Right and Play / Stop, the last case; architect 2026-10-18) as one
    // block of four runs, the run gap between two (row8_block), its last
    // case ending one past the PROGRAM RING'S OUTER RIGHT COLUMN — flush
    // with the ring, never the window's edge (bottom_row_seats,
    // program_ring_columns) — declared below; in the `h` view Older / Newer stand in
    // the Left / Right seats, 2026-10-11).
    // Both PUBLISH their buttons' hit rects into app.redesign_buttons —
    // the painter is the only place a shaped label's width exists, so the
    // pointer code reads the stash instead of re-shaping (the displayed-basis
    // doctrine) — and every one stashes the ENABLED and SELECTED bits it
    // painted beside them, through the one publisher (publish_button_face),
    // for main.cpp's staleness comparator.
    //
    // Both are called from on_redraw OUTSIDE the loading / total>0
    // branches, each gated on its OWN exposure — they are the passes with no
    // dependence on the loaded audio, so a button is visible exactly whenever it
    // is clickable (their press claims sit above the pointer path's loading
    // guard). The exposure gate matters: each shapes labels through HarfBuzz,
    // which a narrow per-frame playhead damage must not pay for. Nothing painted
    // after them touches their lanes, the flag cache being transparent there.
    void paint_icon_row(cairo_t* cr);
    // THE LOGO LANE (architect 2026-10-24; render.h's logo lane block): the
    // band's upper pad — the quaver, the brand and the battery + clock
    // legend with its glyph — painted by paint_icon_row over the band's
    // ground; publishes the legend's rect (AppState::logo_legend_rect).
    void paint_logo_lane(cairo_t* cr, const GuiRect& band);
    // (THE TAB STRIP's painter, paint_tab_strip, 2026-10-18, is retired with
    // the strip, 2026-10-22.)
    // (THE PROGRAM'S FRAME's painter, paint_program_frame — its top row
    // alone, one Shadow line under the caption, from 2026-10-21 — is retired
    // with the row, 2026-10-22; THE BOTTOM OVERLAYS' painter,
    // paint_bottom_overlays — the taskbar line and the dialog's ground,
    // 2026-10-10 to 2026-10-14 — with the taskbars.)
    // (THE CAPTION'S PAINTER, paint_caption_row — the window's own title
    // bar at the top lane 0, 2026-10-05 to 2026-10-24 — is removed with the
    // lane: the tablet's window has none.)
    // THE UNIFIED BOTTOM ROW'S BUTTON-AND-CLOCK HALF (rows 8 and 9 merged,
    // 2026-08-12; the layout architect 2026-09-29; Cool Edit's row 8 since
    // 2026-10-09): the clock in its dark time field (2026-10-05) in a
    // ring at the left, the active tab's name and THE STATE LINE on the ground
    // past it (architect 2026-10-03: the `h` walk line or the render's
    // progress line at the normal face, clipped short of the right block),
    // then the right block packed against the program ring's right edge
    // (bottom_row_seats) — the two skips, Go to Start and Go to End
    // (2026-10-24), the
    // MARKER-VERB GROUP (its membership is kMarkerVerbGroup's, in the .cpp),
    // the step and walk and the nudge and play with Play / Stop its last
    // case (2026-10-18) — each group a plain run, the run gap between two
    // (the simple layout, 2026-10-22; the bars of 2026-10-21, Cool Edit's
    // grippers and end bars 2026-10-09 to 2026-10-21, eight Windows px of
    // bare ground between groups 2026-09-29 to 2026-10-09) — painted onto
    // the lane
    // paint_bottom_strip has
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

    // THE TOOLTIP, painted TOPMOST — after every row pass, so it overlaps the
    // rows it hangs over. It PUBLISHES the rect it painted
    // (AppState::redesign_tooltip.rect) only so the hide edge can damage it,
    // and writes a zero rect when not shown, which is the correct empty answer.
    // IT SERVES TWO SURFACES since 2026-08-13 — the roster and the MODAL
    // DIALOG's buttons, one tooltip state whose owner names which (the
    // encoding is at AppState::RedesignTooltip) — which is why it paints
    // AFTER paint_modal_dialog: it reads the stash that call publishes, and a
    // hint over the modal is the one floating surface a modal does coexist
    // with. (The Settings dropdown, the second floater of this kind, retired
    // 2026-10-21 with paint_dropdown.)
    void paint_shift_tooltip(cairo_t* cr);
    // THE CARD FACE every floating surface draws, the period's tooltip
    // (architect 2026-10-04): `card_ground` with a thin `card_frame` line on
    // all four sides (the tooltip and the notification cards; the sides
    // 2026-10-06; Cool Edit's colors 2026-10-10;
    // render.h's palette block). (Its MENU face, the dropdown's plain raised
    // panel, went with the Settings popup, 2026-10-21.)
    void paint_popup_chrome(cairo_t* cr, const GuiRect& r);
    // THE NOTIFICATION CARDS (2026-08-29): the visible stack, top-right under
    // row 1, painted after the flag editor's box — above every lane and the
    // keyboard slot, below the pointer-transient tooltip and above the modal (the order is on_redraw's step 13). It
    // PUBLISHES each card's rect (AppState::Notifications::painted)
    // for the press claim and the hover walk, and runs on
    // every frame for the floating surfaces' own reason: a skipped run would
    // strand a stale publication. Model and ruling at notifications.h.
    void paint_notifications(cairo_t* cr);
    // (The open flag editor has no pass member here. It is render_flag_editor_box
    // in render.cpp — the last of the marker-text lane's paint pass, unrolled
    // into the flag itself in row 5's checkpoint C, where it shares the flag
    // painter's class ladder, pads, baseline and shaping. on_redraw calls the
    // free function directly, in the floating-surfaces slot and for their
    // reason: it publishes geometry the pointer path reads.)
    // THE CANVAS PASSES PAINT THROUGH THE CANVAS SINK (2026-10-13,
    // canvas_sink.h): the plate, the ring, the stems and their runs, the
    // cursor, the scanner and the anchor below — into the frame's draw list
    // (build_canvas_list).
    void paint_waveform_plate(GuiCanvasSink& sink, const GuiRect& area);
    // The overlay band's DOTTED ring — the phase-reset overlay's whole
    // visual, its right side and its top and bottom runs one-device-px dots
    // in the reset's own stem pattern (its left side is the stem itself;
    // architect 2026-10-09) — painted AFTER the plate and before the stems,
    // over the ink like every dot on the canvas.
    void paint_phase_reset_overlay_ring(GuiCanvasSink& sink,
                                        const GuiRect& area);
    // The LIVE trim pass: paints EVERY trim pixel per frame — the view bar
    // whole (its lines, its black field and the span; no playhead, architect
    // 2026-10-09 ~14:30) — in ONE pass, entirely inside its lane. Its slot is in THE AUTHORITATIVE PAINT-ORDER BLOCK IN
    // on_redraw; this states only this pass's own place in it. Invoked
    // whenever the exposed rect intersects the top strip OR the waveform area
    // — render_background erases every exposed top-strip pixel, so a
    // strip-only damage (hover text, a flag change) must repaint the live view
    // bar too; the outer Cairo damage clip bounds the actual work. See the
    // definition for the basis contract.
    void paint_trim(cairo_t* cr, const GuiRect& area, const GuiRect& top_strip);
    // MARKER STEMS (row 5, 2026-08-01) — the per-frame waveform overlay that
    // replaced the singleton selected-marker stem outright. EVERY ENABLED marker
    // of the active column stems, always, as COOL EDIT'S DOTTED COLUMN since
    // 2026-10-09, in its TWO CUE COLORS since ~11:50: one-device-px dots on
    // the canvas's device rows (~14:25, "on waveform → unscaled"), the blue
    // `range` on rows ≡ 3 and the red `cue`
    // on rows ≡ 7 (mod 8), each where the cue wears it, warp or phase reset,
    // the canvas alone (render.h's marker-lane paragraph); a DISABLED marker
    // stems never.
    // Which colors stand is the cue's class whether the marker is selected
    // or not (the cue never changes when selected); it is resolved in the
    // painter (resolve_flag_face) and published in the stash. The stems paint
    // under the playhead's dots (architect 2026-09-23), which stand on rows
    // ≡ 1 (mod 4), so a playhead on a marker's column interleaves with it.
    //
    // It paints from the marker painter's stash (AppState::marker_stems) rather
    // than walking the store: the stem stands on its cue's apex column, and
    // that column is the one the painter already resolved on the displayed
    // basis, so stem and cue cannot land on different pixels during an async
    // publish window. A live overlay, not a cache — the stash is the cached
    // part.
    //
    // NO PAINT-TIME COLOR OVERRIDE (architect 2026-10-03): the stem paints
    // the stash's class. The open flag editor's refusal recolors nothing.
    // It paints the `h` view's nudged pairs' ring runs first
    // (paint_marker_stem_runs, AppState::marker_stem_runs), the stems their
    // sides.
    void paint_marker_stems(GuiCanvasSink& sink, const GuiRect& area);
    // The nudged pairs' ring runs (MarkerStemRun, render.h; the body
    // owns the form), paint_marker_stems' first act.
    void paint_marker_stem_runs(GuiCanvasSink& sink, const GuiRect& area);
    // THE RESTING CURSOR's canvas run (its head is paint_ruler_row's; nothing
    // over the view bar, architect 2026-10-09 ~14:30): Cool Edit's dotted
    // column in the palette's `playhead_stem` (its Curs yellow by default),
    // one-device-px dots on the canvas's device rows ≡ 1 (mod 4), painted
    // after the marker stems, whose dots stand between its own.
    void paint_playheads(GuiCanvasSink& sink, const GuiRect& area);
    // THE MOVING PLAYBACK LINE, its own pass since 2026-08-01 and invoked AFTER
    // paint_marker_stems and paint_playheads: the scanner draws OVER the stems
    // (and over the cursor where they meet) instead of being erased by every
    // marker it sweeps past. Waveform-only, gated on playhead_scanner_active —
    // see the definition for the ruling.
    void paint_scanner(GuiCanvasSink& sink, const GuiRect& area);
    void paint_strip_drag_anchor(GuiCanvasSink& sink, const GuiRect& area);
    void paint_bottom_strip(cairo_t* cr);
    // THE MODAL (2026-08-13): every modal's controls on the modal family's
    // controls row, and a prompt's question or a dialog editor's line in the
    // family's box (modal_box.h, 2026-10-14) — row 8's own painter yields the
    // lane to it. Painted from on_redraw right after the keyboard slot,
    // unconditionally: it publishes AppState::modal_dialog and
    // AppState::dialog_editor_text, the geometry the pointer path's veil,
    // button claims and click-to-caret read. The full design record is at the
    // definition.
    void paint_modal_dialog(cairo_t* cr);

    // THE ON-SCREEN KEYBOARD (2026-08-27), the glass's key surface — its
    // band across the program lanes on the window's foot, its block of keys
    // centered in it (onscreen_keyboard.h's placement block), over row 8 and
    // the waveform area's lower part. It paints AFTER every waveform pass and
    // BEFORE the modal and the floating surfaces, which is where it sits in
    // the picture: the flag editor is above it in the marker lane, a dialog
    // editor's stack above it in the modal family's box, and the waveform
    // under it is simply not painted where this paints.
    //
    // GATED WHOLE ON onscreen_keyboard::stands (onscreen_keyboard.h — the
    // layout table, the geometry and the two lamps all live there): this body
    // returns at its head while the keyboard does not stand.
    //
    // AND THEN ON ITS OWN EXPOSURE, exactly as the four redesigned rows are and
    // for their reason: this pass shapes up to thirteen cap runs per row, which
    // the outer Cairo clip would NOT elide, and a
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
    // of AppState::keyboard_slot_painted_standing (the slot comparator's
    // as-painted bit, onscreen_keyboard::reconcile_slot_standing): it refreshes the bit only on a rect that
    // FULLY COVERS the band — the roster publisher's own rule, for the two
    // edges the keyboard's body used to carry (a show whose route damaged
    // only the marker lane or the bottom row, a hide painted first by an
    // unrelated narrow damage) — then paints whichever tenant stands.
    // NOT exposure-gated, for the bit's sake: it runs on EVERY frame class
    // (one platform query and two bit reads with the slot down).
    void paint_keyboard_slot(cairo_t* cr, const GuiRect& exposed);

    // THE FOLDER OVERLAY (2026-08-28, the render player): the keyboard-slot
    // list panel, Windows' list view on a sunken field (2026-10-06) — folder
    // and wav rows with their glyphs, the pickers' glyph-less rows, the
    // settings page's group rows in the dead key's print (2026-10-28), the
    // highlight band and the pressed face, all under the band's
    // clip and the live scroll offset. Gated whole on folder_overlay::stands
    // and then on its own exposure per row, as the keyboard is. It publishes
    // no geometry: the press router walks the same row table through the same
    // walker (folder_overlay::for_each_row / row_at).
    void paint_folder_overlay(cairo_t* cr, const GuiRect& exposed);
};
