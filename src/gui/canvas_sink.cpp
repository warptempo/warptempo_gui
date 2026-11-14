#include "canvas_sink.h"

#include <cassert>
#include <cstddef>

// The canvas sink (the rule, the invariant and what each record writes are
// at canvas_sink.h's head).

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

// THE TINT'S RECORD (architect 2026-11-12, the sweep highlight): the word
// the rects' own for the color, the alpha the whole percent the list
// shader divides by 100 (kListFragmentShader's tint arm), at most 100 by
// its one producer's grammar (parse_sweep_highlight, device_config.h).
void GuiCanvasSink::tint(const GuiRect& r, GuiColor toward, int percent) {
    if (r.w <= 0 || r.h <= 0 || percent <= 0) return;
    GuiCanvasPrimitive p;
    p.kind  = GuiCanvasKind::Tint;
    p.r     = r;
    p.word  = argb32_opaque_word(display_color(toward));
    p.word2 = static_cast<uint32_t>(percent);
    list_.prims.push_back(p);
}

// THE PLATE'S RECORDS: one per STORED band (GuiPlateColumns::Band: the
// bands whose bounds are whole; the plate is transparent elsewhere), each
// THE BAND'S DEVICE-PX RECT — the plate's own width (GuiPlateColumns::width)
// from the area's left edge, the band's rows off the area's origin — at the
// plate's own size, so a plate published at a stale size is drawn at that
// size, the scissor (inside the area) cutting what overhangs.
// THE RECORD'S BARS ARE NOT ITS DEVICE COLUMNS (architect 2026-11-14, send
// G; render.h's THE PLATE'S BARS): it carries the count of the band's row
// of bars and the plate's draw transform — the centre of the row's first
// bar and the bars' pitch, in device px from the rect's left edge, the
// plate's doubles handed on as the floats the shader takes.
// THE BOUNDS ARE COPIED ONCE PER REVISION (2026-10-14; GuiCanvasDrawList's
// plate payload): a plate whose revision the list already holds keeps the
// held copy — the same bands' bounds, since the live slot changes only at a
// publication and every publication takes a new revision — and each record's
// offset into it is the running sum of the stored bands' bar counts either
// way, the offsets the copy made.
void GuiCanvasSink::plate(const GuiRect& area, const GuiPlateColumns& columns,
                          WaveformPlateInks inks) {
    if (!columns.rendered) return;
    assert(columns.revision != 0);
    const bool copy = columns.revision != list_.plate_revision;
    if (copy) list_.columns.clear();
    const uint32_t ink     = argb32_opaque_word(hex(inks.ink_rgb));
    const uint32_t core    = argb32_opaque_word(hex(inks.core_rgb));
    std::size_t offset = 0;
    for (const GuiPlateColumns::Band& b : columns.band) {
        if (!b.stored) continue;
        const WaveformBandColumns& c = b.columns;
        const int n = c.col_hi - c.col_lo;
        const int h = c.y_hi - c.y_lo;
        if (n <= 0 || h <= 0 || columns.width <= 0) continue;
        assert(static_cast<int>(c.cols.size()) == n);
        GuiCanvasPrimitive p;
        p.kind   = GuiCanvasKind::Plate;
        p.r      = GuiRect{area.x, area.y + c.y_lo, columns.width, h};
        p.period = static_cast<int>(offset);
        p.origin = area.y;
        p.word   = ink;
        p.word2  = core;
        // The row's first bar is the plate's stored bar col_lo (0 for a
        // stored band, which is whole), so its centre stands col_lo bars
        // past the plate's first.
        p.bars       = n;
        p.bars_x0    = static_cast<float>(
            columns.bar_x0 + static_cast<double>(c.col_lo) * columns.bar_pitch);
        p.bars_pitch = static_cast<float>(columns.bar_pitch);
        if (copy)
            list_.columns.insert(list_.columns.end(), c.cols.begin(),
                                 c.cols.end());
        offset += c.cols.size();
        list_.prims.push_back(p);
    }
    assert(offset == list_.columns.size());
    list_.plate_revision = columns.revision;
}
