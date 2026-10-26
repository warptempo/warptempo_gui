#include "canvas_sink.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <utility>

// The canvas sink's two roads (the rule, the invariant and where identity
// rests are at canvas_sink.h's head).

namespace {

// The non-negative remainder, the dotted runs' one modulo.
int floor_mod(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

// THE KNOCKOUT'S COLOR (canvas_sink.h's head): the `waveform_canvas` role,
// the color render_canvas's ground is laid in, so a hole is the ground's
// bytes. Its word on both roads is argb32_opaque_word(display_color(...)),
// the word set_waveform_source stores for it.
GuiColor knockout_color() {
    return palette().waveform_canvas;
}
uint32_t knockout_word() {
    return argb32_opaque_word(display_color(knockout_color()));
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

// THE KNOCKOUT'S CELL ON THE CAIRO ROAD (canvas_sink.h's head): the plate
// surface's word under window cell (x, y), where the blit put it — (x −
// area.x, y − area.y), inside the area's clip and the surface's bounds — is
// non-zero. The plate stores opaque words or the zero word and nothing else
// (render.cpp's write pass, THE IDENTITY; the caller clears what no call
// stored), so this is the cell set plate_column_covers_row (render.h)
// answers on the bounds the plate was written from.
bool GuiCairoCanvasSink::plate_covers(int x, int y) const {
    if (plate_data_ == nullptr) return false;
    if (x < plate_area_.x || x >= plate_area_.x + plate_area_.w ||
        y < plate_area_.y || y >= plate_area_.y + plate_area_.h)
        return false;
    const int px = x - plate_area_.x;
    const int py = y - plate_area_.y;
    if (px >= plate_w_ || py >= plate_h_) return false;
    const uint32_t* row = reinterpret_cast<const uint32_t*>(
        plate_data_ + static_cast<std::size_t>(py) * plate_stride_);
    return row[px] != 0u;
}

// One 1 x 1 rectangle a cell (waveform_line_px's one device px), the
// run's cells in TWO PATHS, TWO FILLS (the knockout, canvas_sink.h's head):
// the cells off the plate in `color`, then the cells over it in the canvas
// color — disjoint cells under an opaque source, so the order writes
// nothing different; with no plate handed the second path is empty and not
// filled. The cursor's dots, the ring's runs and side and the nudged pairs'
// runs paint through here.
void GuiCairoCanvasSink::dots(std::span<const GuiCanvasDotRun> runs,
                              GuiColor color) {
    const auto lay = [&](bool covered) {
        for (const GuiCanvasDotRun& run : runs) {
            if (run.vertical) {
                for_each_dot(run.y, run.len, run.period, run.origin,
                             run.phase, [&](int y) {
                                 if (plate_covers(run.x, y) != covered) return;
                                 cairo_rectangle(cr_,
                                                 static_cast<double>(run.x),
                                                 static_cast<double>(y), 1.0,
                                                 1.0);
                             });
            } else {
                for_each_dot(run.x, run.len, run.period, run.origin,
                             run.phase, [&](int x) {
                                 if (plate_covers(x, run.y) != covered) return;
                                 cairo_rectangle(cr_, static_cast<double>(x),
                                                 static_cast<double>(run.y),
                                                 1.0, 1.0);
                             });
            }
        }
    };
    set_waveform_source(cr_, color);
    lay(false);
    cairo_fill(cr_);
    if (plate_data_ == nullptr) return;
    set_waveform_source(cr_, knockout_color());
    lay(true);
    cairo_fill(cr_);
}

// THE STEMS' TWO ROADS (paint_marker_stems' head carries the measurement):
// the direct road where direct_pixel_target admits the context — each dot's
// word stored inside each clip rect, the canvas word where the plate covers
// the dot's cell (the knockout, canvas_sink.h's head), the target marked
// dirty at stems_end — else the cached A8 mask laid at the column with the
// source color, then the covered cells as 1 x 1 rectangles in the canvas
// color, one fill: the same pixels.
void GuiCairoCanvasSink::stems_begin() {
    direct_ok_ = direct_pixel_target(cr_, direct_);
}

void GuiCairoCanvasSink::stem_dots(int x, int y, int len, int period,
                                   int phase, GuiColor color) {
    if (direct_ok_) {
        const uint32_t word  = argb32_opaque_word(display_color(color));
        const uint32_t knock = knockout_word();
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
                    static_cast<std::size_t>(yy) * direct_.stride)[dx] =
                    plate_covers(x, yy - direct_.dy) ? knock : word;
        }
        return;
    }
    cairo_surface_t* mask = dot_column_mask(len, period, phase);
    set_waveform_source(cr_, color);
    cairo_mask_surface(cr_, mask, static_cast<double>(x),
                       static_cast<double>(y));
    if (plate_data_ == nullptr) return;
    bool any = false;
    for (int k = phase; k < len; k += period) {
        if (!plate_covers(x, y + k)) continue;
        cairo_rectangle(cr_, static_cast<double>(x),
                        static_cast<double>(y + k), 1.0, 1.0);
        any = true;
    }
    if (!any) return;
    set_waveform_source(cr_, knockout_color());
    cairo_fill(cr_);
}

void GuiCairoCanvasSink::stems_end() {
    if (direct_ok_) cairo_surface_mark_dirty(direct_.surface);
    direct_ok_ = false;
}

// THE BLIT, and the plate kept for the frame's knockout (canvas_sink.h's
// head): the surface's words read in place, flushed first, its size and the
// area it was blitted at.
void GuiCairoCanvasSink::plate(const GuiRect& area, cairo_surface_t* surface,
                               const GuiPlateColumns& /*columns*/,
                               WaveformPlateInks /*inks*/) {
    plate_data_ = nullptr;
    if (!surface) return;
    // Both plate surfaces are ARGB32 (render_waveform's guard,
    // waveform_cache.cpp's creation).
    assert(cairo_image_surface_get_format(surface) == CAIRO_FORMAT_ARGB32);
    cairo_surface_flush(surface);
    plate_data_   = cairo_image_surface_get_data(surface);
    plate_stride_ = cairo_image_surface_get_stride(surface);
    plate_w_      = cairo_image_surface_get_width(surface);
    plate_h_      = cairo_image_surface_get_height(surface);
    plate_area_   = area;
    cairo_save(cr_);
    cairo_rectangle(cr_, area.x, area.y, area.w, area.h);
    cairo_clip(cr_);
    cairo_set_source_surface(cr_, surface, area.x, area.y);
    cairo_paint(cr_);
    cairo_restore(cr_);
}

// -- THE LIST SINK -----------------------------------------------------------

// THE COLUMNS ARE NOT CLEARED HERE (2026-10-14): they belong to the plate's
// revision, not to the rebuild (plate, below).
GuiListCanvasSink::GuiListCanvasSink(GuiCanvasDrawList& list) : list_(list) {
    list_.prims.clear();
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
    const uint32_t word  = argb32_opaque_word(display_color(color));
    const uint32_t knock = knockout_word();
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
        knockout(run, knock);
    }
}

// THE KNOCKOUT ON THE LIST ROAD (canvas_sink.h's head): the run's cells over
// the plate's bars written again in the canvas word by records appended
// after the run's own — a later record overwrites an earlier one in list
// order, so the shader needs nothing new. The bars are this frame's plate
// as `plate()` placed it: a band's columns are its rect's, its rows the
// rect's rows, and its bounds are in the plate's rows, `plate_origin_` the
// plate's row 0 — the cells the Plate record writes a non-zero word on,
// plate_column_covers_row (render.h).
//   A VERTICAL RUN at column x inside a band's columns: the covered rows of
// that column — [o0, o1] ∪ [i0, i1], merged where they touch, exactly
// plate_column_covers_row's set — moved to window rows and cut to the
// band's rows and the run's, each interval ONE MORE DotsColumn of the
// run's period and phase, its origin moved back to at or before the cut
// interval's first cell as dots() moves it.
//   A HORIZONTAL RUN (the ring's top and bottom, the nudged pairs' runs):
// per covered cell one 1 x 1 Rect.
void GuiListCanvasSink::knockout(const GuiCanvasDotRun& run,
                                 uint32_t canvas_word) {
    for (const PlateBand& b : plate_bands_) {
        const int bx1 = b.r.x + b.r.w;   // exclusive
        const int by1 = b.r.y + b.r.h;   // exclusive
        if (run.vertical) {
            if (run.x < b.r.x || run.x >= bx1) continue;
            const WaveformColumnRows& c =
                list_.columns[b.offset +
                              static_cast<std::size_t>(run.x - b.r.x)];
            // The two bars' rows, the empty ones dropped, in order of start.
            int iv[2][2];
            int n = 0;
            if (c.o0 <= c.o1) { iv[n][0] = c.o0; iv[n][1] = c.o1; ++n; }
            if (c.i0 <= c.i1) { iv[n][0] = c.i0; iv[n][1] = c.i1; ++n; }
            if (n == 2 && iv[1][0] < iv[0][0]) {
                std::swap(iv[0][0], iv[1][0]);
                std::swap(iv[0][1], iv[1][1]);
            }
            if (n == 2 && iv[1][0] <= iv[0][1] + 1) {
                iv[0][1] = std::max(iv[0][1], iv[1][1]);
                n = 1;
            }
            for (int k = 0; k < n; ++k) {
                const int lo =
                    std::max({iv[k][0] + plate_origin_, b.r.y, run.y});
                const int hi = std::min({iv[k][1] + plate_origin_, by1 - 1,
                                         run.y + run.len - 1});   // inclusive
                if (lo > hi) continue;
                GuiCanvasPrimitive p;
                p.kind   = GuiCanvasKind::DotsColumn;
                p.r      = GuiRect{run.x, lo, 1, hi + 1 - lo};
                p.period = run.period;
                p.origin = lo - floor_mod(lo - run.origin, run.period);
                p.phase  = run.phase;
                p.word   = canvas_word;
                list_.prims.push_back(p);
            }
        } else {
            if (run.y < b.r.y || run.y >= by1) continue;
            const int y = run.y - plate_origin_;
            const auto knock_cell = [&](int x) {
                if (x < b.r.x || x >= bx1) return;
                const WaveformColumnRows& c =
                    list_.columns[b.offset +
                                  static_cast<std::size_t>(x - b.r.x)];
                if (!plate_column_covers_row(c, y)) return;
                GuiCanvasPrimitive p;
                p.kind = GuiCanvasKind::Rect;
                p.r    = GuiRect{x, run.y, 1, 1};
                p.word = canvas_word;
                list_.prims.push_back(p);
            };
            for_each_dot(run.x, run.len, run.period, run.origin, run.phase,
                         knock_cell);
        }
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
// THE BOUNDS ARE COPIED ONCE PER REVISION (2026-10-14; GuiCanvasDrawList's
// plate payload): a plate whose revision the list already holds keeps the
// held copy — the same bands' bounds, since the live slot changes only at a
// publication and every publication takes a new revision — and each record's
// offset into it is the running sum of the stored bands' widths either way,
// the offsets the copy made.
// THE BANDS KEPT FOR THE FRAME'S KNOCKOUT (canvas_sink.h's head): each
// record's rect and column offset, the plate's row 0 the area's y; none
// where no plate is rendered.
void GuiListCanvasSink::plate(const GuiRect& area, cairo_surface_t* /*surface*/,
                              const GuiPlateColumns& columns,
                              WaveformPlateInks inks) {
    plate_bands_.clear();
    if (!columns.rendered) return;
    plate_origin_ = area.y;
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
        plate_bands_.push_back(PlateBand{p.r, offset});
        offset += c.cols.size();
        list_.prims.push_back(p);
    }
    assert(offset == list_.columns.size());
    list_.plate_revision = columns.revision;
}
