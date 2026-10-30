#pragma once
#include "render.h"   // GuiRect, GuiColor, GuiCanvasDotRun, GuiPlateColumns

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// THE CANVAS SINK (2026-10-13, the GPU canvas's second milestone; the design
// record is docs/engineering/gpu_canvas_2026_10.md). Every pixel the program
// puts inside the canvas's interior — waveform_area, the canvas's paint area
// onscreen_keyboard::waveform_paint_area within it — goes through ONE sink,
// and every canvas painter is written once against it: render_canvas (the
// ground and the grid), the plate (paint_waveform_plate),
// render_center_lines, render_waveform_divider (the stereo divider's three
// rows — chrome on the canvas, 2026-10-16, render.h's waveform_divider — over
// the plate, so a plate from before a resize never crosses them), the
// phase-reset lead-in ring, the nudged pairs' runs,
// the marker stems, the cursor's dots, the scanner and the zoom anchor's
// stem, in that order (build_canvas_list, paint_handler.cpp, after
// on_redraw's paint-order block). Each but the plate is a solid converted
// color on whole device pixels (ON WAVEFORM → UNSCALED, waveform_line_px;
// the divider's rows scaled chrome, but solid integral rects all the same),
// and the plate is the antialiased waveform (architect 2026-10-30,
// render_waveform_columns' head), so the sink's vocabulary is three
// records: a RECT, a DOTTED RUN (GuiCanvasDotRun,
// render.h: a column or a row of one-px cells on a period's phase, counted
// from an origin) and THE PLATE (the per-column quarter-row tips,
// GuiPlateColumns, with its two inks). EVERY RECORD BUT THE PLATE PAINTS
// ITS OWN COLOR straight over whatever lies under it, and THE PLATE'S
// FRINGE BLENDS SOURCE-OVER with the ground and the grid beneath it — the
// GL blend's work (kListFragmentShader, gl_canvas.cpp); no record reads the
// canvas on the CPU. The dots over the plate keep their own color
// (architect 2026-10-26, on the glass, closed_questions.md): opaque records
// drawn over it.
//
// ONE SINK, ONE ROAD (architect 2026-10-30: the GL canvas the one road): the
// sink appends each primitive to the frame's GuiCanvasDrawList in paint
// order, every word argb32_opaque_word(display_color(color)) — the word a
// cairo source stores for that opaque color (display_color's channels are
// n/255, and cairo stores the byte n; cue_fill_pixel, cool_edit_paint.cpp,
// spells the identity), so a canvas record and a chrome pixel of the same
// color carry the same bytes — the plate's two words its published inks'.
// The platform draws the list under a scissor to the list's clip
// (GuiPlatform::set_canvas_frame; gl_canvas.cpp's GuiGlCanvas::present_frame);
// the CPU never paints these pixels at all. A CONCRETE CLASS, no virtual
// interface: the painters name GuiCanvasSink& as their road, and the one
// implementation is the list's.
//
// THE TRANSPARENCY INVARIANT (design record §3.4): the backbuffer's canvas
// paint area is CLEARED (CAIRO_OPERATOR_CLEAR, on_redraw; skipped where it is
// known clear already, 2026-10-14) — the chrome ground and the column's face
// skip it — so every CPU pixel inside the canvas paint area is EITHER ALPHA 0
// (the GPU's: the texture shader discards it and the canvas drawn from the
// list shows) OR ALPHA 255 (an overlay's: every overlay over the well is an
// opaque integral rectangle whose antialiasing blends against its own ground
// alone, design record §2). A partial alpha there is a design violation — a
// translucent or non-rectangular overlay would have to composite on the CPU.
// THE HOLE IS ONE RECT, ITS TENANT THE CANVAS OR THE PAGE (architect
// 2026-10-27, "1 a", the GPU page; design record §3.13): the list's clip is
// the canvas paint area, or — while the score viewer's page stands
// (score_viewer_page_stands), the modal family's box having emptied the
// canvas paint area — THE BOX'S INTERIOR, which the GPU draws whole (the
// field ground and the page, GuiCanvasPage below) and which
// paint_modal_box CLEARS at every rect that meets it, after every pass that
// paints under the box (the lanes paint their exposure under it and the box
// covers them, on_redraw's paint-order block step 12). The invariant is the
// same inside it: alpha 0, or an opaque overlay's 255 (the tooltip and the
// cards, the floater bit's two). The two tenants never stand together, so
// the scissor, the rect every upload leaves out and the floater bit's
// whole-walk rule are one rule for both.
//
// WHAT EACH RECORD WRITES, the rules the GPU's draw keeps: a rect writes its
// word on its cells; a dotted run writes the word where (c − origin) mod
// period = phase, the cells the run names (the sink moves `origin` back by
// whole periods to at or before the run's first cell, so the GPU's modulo
// never meets a negative operand); the plate writes, per cell, the
// premultiplied source of its two coverages — the ink's where the grown
// shape covers, the outline's where the grown inner's border does —
// rasterized from render_waveform_columns' tips (render.h, the plate's one
// producer; the algorithm kListFragmentShader's) and blended source-over
// under the GL blend, exactly the word in the core and nothing where no bar
// covers and the canvas shows; later
// records overwrite earlier ones in list order, the paint order; the
// scissor is the canvas paint area, the plate's area clip containing it; and
// the texture over it places every overlay pixel exactly (Milestone 1). THE
// LIST IS REBUILT WHOLE from the frame's state at every frame that meets the
// canvas paint area (on_redraw's canvas block), so no canvas pass is ever
// cut to a damage rect.

// ONE LIST RECORD. `r` the cells it may write, device px of the
// full-screen surface. A RECT writes `word` on r; a
// DOTTED RUN (a column, DotsColumn: r one column wide; or a row, DotsRow:
// r one row tall) writes `word` on the cells whose coordinate c along the
// run satisfies (c − origin) mod period = phase, origin at or before the
// run's first cell; THE PLATE blends, per cell of r, the exact coverage of
// its columns' tips (the waveform's antialiasing, render_waveform_columns'
// head) — `period` the index of r's first column in the
// list's `columns`, `origin` the plate's row 0 (the canvas's first row) —
// `word` the ink and `word2` the outline, nothing where no bar covers.
enum class GuiCanvasKind : int { Rect = 0, DotsColumn = 1, DotsRow = 2, Plate = 3 };
struct GuiCanvasPrimitive {
    GuiCanvasKind kind = GuiCanvasKind::Rect;
    GuiRect  r{};
    int      period = 0;
    int      origin = 0;
    int      phase  = 0;
    uint32_t word   = 0;
    uint32_t word2  = 0;
};

// THE FRAME'S CANVAS (2026-10-13): the records in paint order, the plate
// bands' columns behind them, and what the platform needs to place them —
// the clip (waveform_area ∩ waveform_paint_area: the GL draw's scissor and
// the rect every upload leaves out; the modal box's interior instead while
// the score viewer's page stands, `page` below, 2026-10-27) and whether a
// floating surface stands
// (floating_surface_stands, paint_handler.cpp: its changes ride containing
// rects, so every damage rect uploads whole). The keyboard slot needs no
// bit: its tenants stand outside the clip (the keyboard and a dialog on it
// below the paint area, the folder overlay emptying it), and a clip that
// moved makes the platform's next walk whole. THE PROCESS'S ONE OBJECT
// (gui_canvas_frame below), because the platform reads it at present and
// outlives every project set (platform.h's loop contract). `generation`
// moves at every rebuild of the records, so the platform re-reads the
// records (its instance buffer) only when they changed.
// THE PLATE'S PAYLOAD HAS ITS OWN LIFETIME (2026-10-14): `columns` holds the
// bounds of the plate whose revision is `plate_revision`
// (GuiPlateColumns::revision, render.h; 0 before the first plate), kept
// across rebuilds and recopied by the sink only when a plate of another
// revision arrives — a revision's bounds never change, so the kept copy is
// the copy a rebuild would make. The list's Plate records, when it has any,
// draw that plate, and the platform re-uploads its bounds texture only when
// the revision or the records' spans moved (GuiGlCanvas::upload_list).
// THE GPU PAGE (architect 2026-10-27, "1 a": THE PICTURE FOLLOWS THE THUMB,
// THE SOUND WAITS FOR THE LIFT; design record §3.13). The score viewer's page
// is not painted into the backbuffer: the box's interior is the frame's hole
// (the list's clip) and the GPU draws it whole at every swap — THE FIELD
// GROUND (`field`, argb32_opaque_word of display_color, the sink's word for
// the field color) and over it THE PAGE, one R8 texture of THE GRAY STORE's
// bytes (GuiScoreGrayPage, score_page_worker.h), texelFetched at integer
// coordinates and written (g, g, g, 1) at gui_page_place's rect, centered.
// NO COLOR TABLE: sRGB -> P3 is the identity on every neutral word (all 256
// checked 2026-10-27, score_page_worker.h), so the gray byte is the window's
// byte under either conversion bit.
// TWO HALVES, the GUI's and the GL owner's:
//   * THE REQUEST — written by the GUI at every frame's first damage rect
//     (score_viewer_publish_gpu_page, score_viewer.cpp): whether the page
//     stands (the clip is then the interior), the viewer's session, the
//     field word, THE SHOWN PAGE (its index, its store entry's serial — 0
//     where the store lacks it — and whether its decode failed), THE LAST
//     PAINTED PAGE (ScoreViewer::painted_page, the page on screen, by its
//     index and serial; -1 and 0 where it is the shown page, is none, or the
//     store lacks it or holds it failed), and
//     THE WANTED TEXTURES: the store's pages at the interior's box, failed
//     ones aside, in THE FEED'S ORDER (the shown page, its neighbors in the
//     cache's order, then the rest of the range), each named by its serial
//     (GuiScoreGrayPage::serial, unique for the process's life) with its
//     size and its shared, immutable bytes — no copy — UNTIL THE TEXTURE IS
//     HELD (architect 2026-10-27, the memory fix): once the report lists a
//     serial the viewer's feed lets the store's bytes go and this record's
//     with them (GuiScoreViewer::settle_gray_on_gpu, which writes the
//     request's bytes in the tick), so a held page's record carries no
//     bytes and the reconciliation, which uploads only an unheld page,
//     never needs them.
//   * THE REPORT — written by GuiGlCanvas::present_frame: the serials it
//     holds as textures, ascending, and, at each present that drew the page,
//     the session it drew for and the page it put on screen (-1 the field
//     ground alone). THE TEXTURES MIRROR THE WANTED LIST, so the store's
//     scope (the range, the box, the score, the project) is theirs, and a
//     texture that goes while its released store entry stands takes the
//     entry with it at the viewer's next feed (settle_gray_on_gpu); the
//     page's draw is THE SHOWN PAGE'S TEXTURE WHERE IT IS HELD, ELSE THE
//     LAST PAINTED PAGE'S, ELSE THE FIELD GROUND — and the field alone for a
//     shown page whose decode failed (it is then the page on screen).
struct GuiCanvasPageTexture {
    uint64_t serial = 0;
    int      page   = -1;
    int      w      = 0;
    int      h      = 0;
    std::shared_ptr<const std::vector<uint8_t>> bytes;
};
struct GuiCanvasPage {
    // The request.
    bool     stands       = false;
    uint64_t session      = 0;
    uint32_t field        = 0;
    int      shown_page   = -1;
    uint64_t shown_serial = 0;
    bool     shown_failed = false;
    int      last_page    = -1;
    uint64_t last_serial  = 0;
    std::vector<GuiCanvasPageTexture> want;
    // The report.
    std::vector<uint64_t> held;
    uint64_t drawn_session = 0;
    int      drawn_page    = -1;
};

// THE PAGE'S PLACE IN THE BOX (the score viewer's, score_viewer.h's head):
// a w x h page centered on both axes at whole device px, the integer
// halves of the slack — the GPU page's draw reads this one arithmetic.
inline GuiRect gui_page_place(const GuiRect& box, int w, int h) {
    return GuiRect{box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h};
}

struct GuiCanvasDrawList {
    GuiRect  clip{};
    bool     floater = false;
    uint64_t generation = 0;
    uint64_t plate_revision = 0;
    std::vector<GuiCanvasPrimitive>  prims;
    std::vector<WaveformColumnRows>  columns;
    // THE GPU PAGE (above): while `page.stands` the clip is the box's
    // interior and the page draws there in the records' stead.
    GuiCanvasPage page;
};
GuiCanvasDrawList& gui_canvas_frame();

// THE SINK (the head's ONE SINK, ONE ROAD). Constructed over the frame's list,
// it rebuilds the list's records from empty (the clip and the floater bit
// are the caller's), the generation moved at construction; the plate's
// columns are kept while their revision holds (GuiCanvasDrawList's plate
// payload). rects and dots append in `color` (an authored color,
// display_color applied here); stem_dots a marker stem's dotted column (rows
// [y, y + len) at column x, the phase counted from y); plate the cache's
// published plate at `area` from its `columns` (not rendered: nothing),
// `inks` the published inks.
class GuiCanvasSink {
public:
    explicit GuiCanvasSink(GuiCanvasDrawList& list);
    void rects(std::span<const GuiRect> rs, GuiColor color);
    void dots(std::span<const GuiCanvasDotRun> runs, GuiColor color);
    void stem_dots(int x, int y, int len, int period, int phase,
                   GuiColor color);
    void plate(const GuiRect& area, const GuiPlateColumns& columns,
               WaveformPlateInks inks);

private:
    GuiCanvasDrawList& list_;
};
