#include "canvas_sink.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>

// The canvas sink's two roads (the rule, the invariant and where identity
// rests are at canvas_sink.h's head).

namespace {

// The non-negative remainder, the dotted runs' one modulo.
int floor_mod(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

// THE CELLS OF ONE DOTTED RUN, in order along it: `f(c)` for every c in
// [start, start + len) with (c − origin) mod period = phase (0 <= phase <
// period, every phase program_spec.h names).
template <typename F>
void for_each_dot(int start, int len, int period, int origin, int phase,
                  F&& f) {
    if (len <= 0 || period <= 0) return;
    assert(phase >= 0 && phase < period);
    const int first =
        start + floor_mod(phase - floor_mod(start - origin, period), period);
    for (int c = first; c < start + len; c += period) f(c);
}

// THE DOT COLUMN'S MASK (the stems' mask road, GuiCairoCanvasSink::stem_dots):
// a one-px-wide A8 strip `h` rows tall, 255 on rows k ≡ `phase` (mod
// `period`) counting from its top (k from `phase` up), 0 elsewhere. Cached by
// (h, period, phase) for the process; a size change replaces the cache
// whole. Cost cut only (architect 2026-10-12): it paints the rectangles'
// pixels. Since 2026-10-13 it is THE MASK ROAD, taken only where the direct
// road (direct_pixel_target, render.h) is refused.
cairo_surface_t* dot_column_mask(int h, int period, int phase) {
    struct Entry {
        int h, period, phase;
        cairo_surface_t* surface;
    };
    static std::vector<Entry> cache;
    for (const Entry& e : cache)
        if (e.h == h && e.period == period && e.phase == phase)
            return e.surface;
    if (cache.size() >= 4) {
        for (const Entry& e : cache) cairo_surface_destroy(e.surface);
        cache.clear();
    }
    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_A8, 1, std::max(h, 1));
    unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    std::memset(data, 0, static_cast<std::size_t>(stride) * std::max(h, 1));
    for (int k = phase; k < h; k += period) data[k * stride] = 255;
    cairo_surface_mark_dirty(surface);
    cache.push_back(Entry{h, period, phase, surface});
    return surface;
}

} // namespace

GuiCanvasDrawList& gui_canvas_frame() {
    static GuiCanvasDrawList frame;
    return frame;
}

// -- THE CAIRO SINK ----------------------------------------------------------

void GuiCairoCanvasSink::begin(bool aliased) {
    cairo_save(cr_);
    if (aliased) cairo_set_antialias(cr_, CAIRO_ANTIALIAS_NONE);
}

void GuiCairoCanvasSink::end() {
    cairo_restore(cr_);
}

void GuiCairoCanvasSink::rects(std::span<const GuiRect> rs, GuiColor color) {
    set_waveform_source(cr_, color);
    for (const GuiRect& r : rs)
        cairo_rectangle(cr_, r.x, r.y, r.w, r.h);
    cairo_fill(cr_);
}

// One 1 x 1 rectangle a cell (waveform_line_px's one device px), every run's
// cells in one path, one fill — the shape of the retired helper
// fill_dotted_waveform_line (deleted 2026-10-13 with this sink), which the
// cursor and the ring's right side drew through, and the ring's and the
// runs' one path of cells.
void GuiCairoCanvasSink::dots(std::span<const GuiCanvasDotRun> runs,
                              GuiColor color) {
    set_waveform_source(cr_, color);
    for (const GuiCanvasDotRun& run : runs) {
        if (run.vertical) {
            for_each_dot(run.y, run.len, run.period, run.origin, run.phase,
                         [&](int y) {
                             cairo_rectangle(cr_, static_cast<double>(run.x),
                                             static_cast<double>(y), 1.0, 1.0);
                         });
        } else {
            for_each_dot(run.x, run.len, run.period, run.origin, run.phase,
                         [&](int x) {
                             cairo_rectangle(cr_, static_cast<double>(x),
                                             static_cast<double>(run.y), 1.0,
                                             1.0);
                         });
        }
    }
    cairo_fill(cr_);
}

// THE STEMS' TWO ROADS (paint_marker_stems' head carries the measurement):
// the direct road where direct_pixel_target admits the context — each dot's
// word stored inside each clip rect, the target marked dirty at stems_end —
// else the cached A8 mask laid at the column with the source color.
void GuiCairoCanvasSink::stems_begin() {
    direct_ok_ = direct_pixel_target(cr_, direct_);
}

void GuiCairoCanvasSink::stem_dots(int x, int y, int len, int period,
                                   int phase, GuiColor color) {
    if (direct_ok_) {
        const uint32_t word = argb32_opaque_word(display_color(color));
        const int dx      = x + direct_.dx;
        const int y_first = y + phase + direct_.dy;
        const int y_end   = y + len + direct_.dy;   // exclusive
        for (const GuiRect& r : direct_.clip) {
            if (dx < r.x || dx >= r.x + r.w) continue;
            const int lo = std::max(y_first, r.y);
            const int hi = std::min(y_end, r.y + r.h);   // exclusive
            if (lo >= hi) continue;
            int yy = y_first;
            if (yy < lo) yy += (lo - yy + period - 1) / period * period;
            for (; yy < hi; yy += period)
                reinterpret_cast<uint32_t*>(
                    direct_.data +
                    static_cast<std::size_t>(yy) * direct_.stride)[dx] = word;
        }
        return;
    }
    cairo_surface_t* mask = dot_column_mask(len, period, phase);
    set_waveform_source(cr_, color);
    cairo_mask_surface(cr_, mask, static_cast<double>(x),
                       static_cast<double>(y));
}

void GuiCairoCanvasSink::stems_end() {
    if (direct_ok_) cairo_surface_mark_dirty(direct_.surface);
    direct_ok_ = false;
}

void GuiCairoCanvasSink::plate(const GuiRect& area, cairo_surface_t* surface,
                               const GuiPlateColumns& /*columns*/,
                               WaveformPlateInks /*inks*/) {
    if (!surface) return;
    cairo_save(cr_);
    cairo_rectangle(cr_, area.x, area.y, area.w, area.h);
    cairo_clip(cr_);
    cairo_set_source_surface(cr_, surface, area.x, area.y);
    cairo_paint(cr_);
    cairo_restore(cr_);
}

// -- THE LIST SINK -----------------------------------------------------------

GuiListCanvasSink::GuiListCanvasSink(GuiCanvasDrawList& list) : list_(list) {
    list_.prims.clear();
    list_.columns.clear();
    ++list_.generation;
}

void GuiListCanvasSink::rects(std::span<const GuiRect> rs, GuiColor color) {
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
void GuiListCanvasSink::dots(std::span<const GuiCanvasDotRun> runs,
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

void GuiListCanvasSink::stem_dots(int x, int y, int len, int period,
                                  int phase, GuiColor color) {
    const GuiCanvasDotRun run{true, x, y, len, period, y, phase};
    dots(std::span<const GuiCanvasDotRun>(&run, 1), color);
}

// THE PLATE'S RECORDS: one per STORED band (the cairo road's surface holds
// exactly those bands' words and is transparent elsewhere), placed where the
// blit puts the surface's pixels — the band's surface columns and rows off
// the area's origin — at the plate's own size, so a plate published at a
// stale size is drawn exactly as the blit draws it, the scissor (inside the
// blit's area clip) cutting what overhangs.
void GuiListCanvasSink::plate(const GuiRect& area, cairo_surface_t* /*surface*/,
                              const GuiPlateColumns& columns,
                              WaveformPlateInks inks) {
    if (!columns.rendered) return;
    const uint32_t ink     = argb32_opaque_word(hex(inks.ink_rgb));
    const uint32_t outline = argb32_opaque_word(hex(inks.outline_rgb));
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
        p.period = static_cast<int>(list_.columns.size());
        p.origin = area.y;
        p.word   = ink;
        p.word2  = outline;
        list_.columns.insert(list_.columns.end(), c.cols.begin(),
                             c.cols.end());
        list_.prims.push_back(p);
    }
}
