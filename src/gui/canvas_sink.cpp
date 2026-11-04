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
// THE GROW IS STAMPED AT EVERY REBUILD (GuiCanvasDrawList::plate_grow):
// the key's live seat, in subcells, whether or not a plate follows — the GL
// canvas compares it with its program's at every list draw, so a list
// without a plate must carry the same number as one with.
GuiCanvasSink::GuiCanvasSink(GuiCanvasDrawList& list) : list_(list) {
    list_.prims.clear();
    ++list_.generation;
    list_.plate_grow = waveform_grow_quarters();
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

// THE RECORD IS THE RUN'S WHOLE t-THICK EXTENT (render.h's GuiCanvasDotRun,
// 2026-11-04): its thickness is the cell the GPU divides by, so it is never
// cut here — the list's scissor cuts a line's columns past the area's edge.
// THE ORIGIN MOVED BACK BY WHOLE PERIODS OF CELLS (period · t coordinates)
// to at or before the run's first coordinate: the same cells (the cell
// index's modulo is periodic in the origin by period · t), and every offset
// the GPU computes is non-negative, so its integer division is the floor —
// GLSL ES 3.00 leaves % on a negative operand undefined.
void GuiCanvasSink::dots(std::span<const GuiCanvasDotRun> runs,
                         GuiColor color) {
    const uint32_t word = argb32_opaque_word(display_color(color));
    for (const GuiCanvasDotRun& run : runs) {
        if (run.len <= 0 || run.period <= 0) continue;
        assert(run.phase >= 0 && run.phase < run.period);
        assert(run.t > 0);
        const int start = run.vertical ? run.y : run.x;
        GuiCanvasPrimitive p;
        p.kind   = run.vertical ? GuiCanvasKind::DotsColumn
                                : GuiCanvasKind::DotsRow;
        p.r      = run.vertical ? GuiRect{run.x, run.y, run.t, run.len}
                                : GuiRect{run.x, run.y, run.len, run.t};
        p.period = run.period;
        p.origin = start - floor_mod(start - run.origin, run.period * run.t);
        p.phase  = run.phase;
        p.word   = word;
        list_.prims.push_back(p);
    }
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
