#include "canvas_sink.h"

#include <cassert>
#include <cstddef>

// The canvas sink (the rule, the invariant and what each record writes are
// at canvas_sink.h's head).

namespace {

// The non-negative remainder, the dotted runs' one modulo.
int floor_mod(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

} // namespace

GuiCanvasDrawList& gui_canvas_frame() {
    static GuiCanvasDrawList frame;
    return frame;
}

// -- THE SINK ----------------------------------------------------------------

// THE COLUMNS ARE NOT CLEARED HERE (2026-10-14): they belong to the plate's
// revision, not to the rebuild (plate, below).
GuiCanvasSink::GuiCanvasSink(GuiCanvasDrawList& list) : list_(list) {
    list_.prims.clear();
    ++list_.generation;
}

void GuiCanvasSink::rects(std::span<const GuiRect> rs, GuiColor color) {
    const uint32_t word = argb32_opaque_word(display_color(color));
    for (const GuiRect& r : rs) {
        if (r.w <= 0 || r.h <= 0) continue;
        GuiCanvasPrimitive p;
        p.kind = GuiCanvasKind::Rect;
        p.r    = r;
        p.word = word;
        list_.prims.push_back(p);
    }
}

// THE ORIGIN MOVED BACK BY WHOLE PERIODS to at or before the run's first
// cell: the same cells (the modulo is periodic in the origin), and every
// offset the GPU computes is non-negative — GLSL ES 3.00 leaves % on a
// negative operand undefined.
void GuiCanvasSink::dots(std::span<const GuiCanvasDotRun> runs,
                         GuiColor color) {
    const uint32_t word = argb32_opaque_word(display_color(color));
    for (const GuiCanvasDotRun& run : runs) {
        if (run.len <= 0 || run.period <= 0) continue;
        assert(run.phase >= 0 && run.phase < run.period);
        const int start = run.vertical ? run.y : run.x;
        GuiCanvasPrimitive p;
        p.kind   = run.vertical ? GuiCanvasKind::DotsColumn
                                : GuiCanvasKind::DotsRow;
        p.r      = run.vertical ? GuiRect{run.x, run.y, 1, run.len}
                                : GuiRect{run.x, run.y, run.len, 1};
        p.period = run.period;
        p.origin = start - floor_mod(start - run.origin, run.period);
        p.phase  = run.phase;
        p.word   = word;
        list_.prims.push_back(p);
    }
}

void GuiCanvasSink::stem_dots(int x, int y, int len, int period, int phase,
                              GuiColor color) {
    const GuiCanvasDotRun run{true, x, y, len, period, y, phase};
    dots(std::span<const GuiCanvasDotRun>(&run, 1), color);
}

// THE PLATE'S RECORDS: one per STORED band (GuiPlateColumns::Band: the
// bands whose bounds are whole; the plate is transparent elsewhere), each
// placed at the band's own columns and rows off the area's origin — at the
// plate's own size, so a plate published at a stale size is drawn at that
// size, the scissor (inside the area) cutting what overhangs.
// THE BOUNDS ARE COPIED ONCE PER REVISION (2026-10-14; GuiCanvasDrawList's
// plate payload): a plate whose revision the list already holds keeps the
// held copy — the same bands' bounds, since the live slot changes only at a
// publication and every publication takes a new revision — and each record's
// offset into it is the running sum of the stored bands' widths either way,
// the offsets the copy made.
void GuiCanvasSink::plate(const GuiRect& area, const GuiPlateColumns& columns,
                          WaveformPlateInks inks) {
    if (!columns.rendered) return;
    assert(columns.revision != 0);
    const bool copy = columns.revision != list_.plate_revision;
    if (copy) list_.columns.clear();
    const uint32_t ink     = argb32_opaque_word(hex(inks.ink_rgb));
    const uint32_t outline = argb32_opaque_word(hex(inks.outline_rgb));
    std::size_t offset = 0;
    for (const GuiPlateColumns::Band& b : columns.band) {
        if (!b.stored) continue;
        const WaveformBandColumns& c = b.columns;
        const int w = c.col_hi - c.col_lo;
        const int h = c.y_hi - c.y_lo;
        if (w <= 0 || h <= 0) continue;
        assert(static_cast<int>(c.cols.size()) == w);
        GuiCanvasPrimitive p;
        p.kind   = GuiCanvasKind::Plate;
        p.r      = GuiRect{area.x + c.col_lo, area.y + c.y_lo, w, h};
        p.period = static_cast<int>(offset);
        p.origin = area.y;
        p.word   = ink;
        p.word2  = outline;
        if (copy)
            list_.columns.insert(list_.columns.end(), c.cols.begin(),
                                 c.cols.end());
        offset += c.cols.size();
        list_.prims.push_back(p);
    }
    assert(offset == list_.columns.size());
    list_.plate_revision = columns.revision;
}
