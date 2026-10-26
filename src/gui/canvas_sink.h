#pragma once
#include "render.h"   // GuiRect, GuiColor, GuiCanvasDotRun, GuiPlateColumns

#include <cairo/cairo.h>
#include <cstdint>
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
// stem, in that order (on_redraw's paint-order block, paint_handler.cpp).
// Each is a solid converted color on whole device pixels (ON WAVEFORM →
// UNSCALED, waveform_line_px; the divider's rows scaled chrome, but solid
// integral rects all the same), so the sink's vocabulary is three records:
// a RECT, a DOTTED RUN (GuiCanvasDotRun, render.h: a column or a row of
// one-px cells on a period's phase, counted from an origin) and THE PLATE
// (the per-column bar bounds, GuiPlateColumns, with its two inks).
//
// TWO IMPLEMENTATIONS, one per road:
//   * THE CAIRO SINK (GuiCairoCanvasSink) — the cpu canvas on both
//     platforms, the reference: each primitive is the cairo calls the
//     painter made before the sink existed — begin / end the painter's
//     cairo_save / cairo_restore with ANTIALIAS_NONE exactly where it set it
//     (render_canvas, the center lines, the divider, the ring, the runs) and the default
//     where it set none (the cursor, the scanner, the anchor, the stems); a
//     rects call ONE fill of its rectangles under set_waveform_source (the
//     body set_palette_source shares, render.cpp); a dots call ONE fill of
//     its runs' 1 x 1 cells; the stems' DIRECT ROAD (direct_pixel_target,
//     render.h: the converted word stored straight into the target inside
//     its clip) and their A8 MASK ROAD where it is refused, both moved here
//     unchanged in bytes; the plate the cache surface blitted OVER at the
//     area's origin under the area's clip.
//   * THE LIST SINK (GuiListCanvasSink) — the GL canvas on both platforms
//     (the build's WARPTEMPO_CANVAS=gl, kGuiCanvasDrawList): each primitive
//     appended to the frame's GuiCanvasDrawList in paint order, every word
//     argb32_opaque_word(display_color(color)) — the word the cairo sink's
//     source stores for that opaque color (display_color's channels are
//     n/255, and cairo stores the byte n; cue_fill_pixel, cool_edit_paint.cpp,
//     spells the identity) — the plate's two words its published inks' as the
//     cairo road bakes them. The platform draws the list under a scissor to
//     the list's clip (GuiPlatform::set_canvas_frame; gl_canvas.cpp's
//     GuiGlCanvas::present_frame), the CPU never paints these pixels at all.
//
// THE TRANSPARENCY INVARIANT (design record §3.4): under the GL canvas the
// backbuffer's canvas paint area is CLEARED (CAIRO_OPERATOR_CLEAR, on_redraw;
// skipped where it is known clear already, 2026-10-14) where the cpu road
// fills and paints it — the chrome ground and the
// column's face skip it — so every CPU pixel inside the canvas paint area is
// EITHER ALPHA 0 (the GPU's: the texture shader discards it and the canvas
// drawn from the list shows) OR ALPHA 255 (an overlay's: every overlay over
// the well is an opaque integral rectangle whose antialiasing blends against
// its own ground alone, design record §2). A partial alpha there is a design
// violation — a translucent or non-rectangular overlay would have to
// composite on the CPU — and the screencap harness is its detector.
//
// WHERE IDENTITY RESTS, the three claims:
//   * THE CPU CANVAS AGAINST THE TREE BEFORE THE SINK: the cairo
//     sink's calls are the painters' own, argument for argument; where a
//     painter's one path became a primitive's (the ring's top and bottom runs
//     share one fill as before, the runs' cells now ordered run by run),
//     every rectangle is integral under an integral translation with an
//     opaque source and OVER, so a fill writes the source on exactly the
//     union of its cells whatever their order in the path.
//   * THE GL CANVAS AGAINST THE CPU CANVAS: a rect writes the same word on
//     the same cells; a dotted run writes the word where (c − origin) mod
//     period = phase, the same cells the cairo sink enumerates (the list
//     moves `origin` back by whole periods to at or before the run's first
//     cell, so the GPU's modulo never meets a negative operand); the plate's
//     select is render_waveform's priority on the same bounds
//     (render_waveform_columns, render.h, the one producer of both roads),
//     the zero word where the cairo road leaves the plate transparent and
//     the canvas shows; later records overwrite earlier ones in list order,
//     the cairo road's paint order; the scissor is the cpu road's clip
//     (the canvas paint area, the plate's area clip containing it); and the
//     texture over it places every overlay pixel exactly (Milestone 1).
//   * WHAT IS NOT IDENTICAL BY CONSTRUCTION: the list is rebuilt WHOLE from
//     the frame's state where the cairo road repaints only the damaged part
//     of a persistent backbuffer, so a canvas change some route forgot to
//     damage shows at once on the GL canvas and stays stale on the cpu one —
//     a damage defect on either build, never a road's difference.

// THE ROAD, A BUILD CHOICE (CMakeLists.txt's WARPTEMPO_CANVAS, the compile
// definition WARPTEMPO_CANVAS_GL, 2026-10-14 for both platforms): true on
// either platform's gl build, false on either's cpu build, whose sink is
// always the cairo one. A constexpr rather than a preprocessor fork, so the
// cpu builds compile the list road too.
#if defined(WARPTEMPO_CANVAS_GL) && WARPTEMPO_CANVAS_GL
inline constexpr bool kGuiCanvasDrawList = true;
#else
inline constexpr bool kGuiCanvasDrawList = false;
#endif

// ONE LIST RECORD. `r` the cells it may write, device px of the GUI's
// client area (the platform adds its frame). A RECT writes `word` on r; a
// DOTTED RUN (a column, DotsColumn: r one column wide; or a row, DotsRow:
// r one row tall) writes `word` on the cells whose coordinate c along the
// run satisfies (c − origin) mod period = phase, origin at or before the
// run's first cell; THE PLATE writes, per cell of r, the priority select of
// its column's bounds — `period` the index of r's first column in the
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
// the rect every upload leaves out) and whether a floating surface stands
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
// across rebuilds and recopied by the list sink only when a plate of another
// revision arrives — a revision's bounds never change, so the kept copy is
// the copy a rebuild would make. The list's Plate records, when it has any,
// draw that plate, and the platform re-uploads its bounds texture only when
// the revision or the records' spans moved (GuiGlCanvas::upload_list).
struct GuiCanvasDrawList {
    GuiRect  clip{};
    bool     floater = false;
    uint64_t generation = 0;
    uint64_t plate_revision = 0;
    std::vector<GuiCanvasPrimitive>  prims;
    std::vector<WaveformColumnRows>  columns;
};
GuiCanvasDrawList& gui_canvas_frame();

// THE SINK. begin / end bracket one painter's scope (`aliased` the
// painter's ANTIALIAS_NONE); rects and dots each one fill on the cairo road
// in `color` (an authored color, display_color applied by the road);
// stems_begin / stem_dots / stems_end the marker stems' bracket and their
// dotted columns (rows [y, y + len) at column x, the phase counted from y);
// plate the cache's published plate at `area`, the cairo road blitting
// `surface` (null: nothing) and the list road reading `columns` (not
// rendered: nothing), `inks` the published inks.
class GuiCanvasSink {
public:
    virtual ~GuiCanvasSink() = default;
    virtual void begin(bool aliased) = 0;
    virtual void end() = 0;
    virtual void rects(std::span<const GuiRect> rs, GuiColor color) = 0;
    virtual void dots(std::span<const GuiCanvasDotRun> runs,
                      GuiColor color) = 0;
    virtual void stems_begin() = 0;
    virtual void stem_dots(int x, int y, int len, int period, int phase,
                           GuiColor color) = 0;
    virtual void stems_end() = 0;
    virtual void plate(const GuiRect& area, cairo_surface_t* surface,
                       const GuiPlateColumns& columns,
                       WaveformPlateInks inks) = 0;
};

class GuiCairoCanvasSink final : public GuiCanvasSink {
public:
    explicit GuiCairoCanvasSink(cairo_t* cr) : cr_(cr) {}
    void begin(bool aliased) override;
    void end() override;
    void rects(std::span<const GuiRect> rs, GuiColor color) override;
    void dots(std::span<const GuiCanvasDotRun> runs, GuiColor color) override;
    void stems_begin() override;
    void stem_dots(int x, int y, int len, int period, int phase,
                   GuiColor color) override;
    void stems_end() override;
    void plate(const GuiRect& area, cairo_surface_t* surface,
               const GuiPlateColumns& columns,
               WaveformPlateInks inks) override;

private:
    cairo_t*          cr_;
    DirectPixelTarget direct_;
    bool              direct_ok_ = false;
};

// Rebuilds `list`'s records from empty (the clip and the floater bit are the
// caller's), the generation moved at construction; the plate's columns are
// kept while their revision holds (GuiCanvasDrawList's plate payload).
class GuiListCanvasSink final : public GuiCanvasSink {
public:
    explicit GuiListCanvasSink(GuiCanvasDrawList& list);
    void begin(bool) override {}
    void end() override {}
    void rects(std::span<const GuiRect> rs, GuiColor color) override;
    void dots(std::span<const GuiCanvasDotRun> runs, GuiColor color) override;
    void stems_begin() override {}
    void stem_dots(int x, int y, int len, int period, int phase,
                   GuiColor color) override;
    void stems_end() override {}
    void plate(const GuiRect& area, cairo_surface_t* surface,
               const GuiPlateColumns& columns,
               WaveformPlateInks inks) override;

private:
    GuiCanvasDrawList& list_;
};
