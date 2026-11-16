#include "text_raster.h"

// The road, its promises, its cost and its cache are at the head of
// text_raster.h.

#include <cairo/cairo-ft.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace text_raster {

namespace {

// THE LOAD cairo's font backend makes for the product's options
// (cairo-ft-font.c: the face's own load flags, FT_LOAD_NO_BITMAP, with hint
// style SLIGHT's target, FT_LOAD_TARGET_LIGHT, under gray antialiasing) —
// the one place this unit names them; gui_font_bundled.cpp's options and
// its face are their source.
constexpr FT_Int32 kLoadFlags = FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_LIGHT;

// One 26.6 unit, a 64th of a px: the resolution a place is taken to.
constexpr int k64 = 64;

// THE FACE'S LOCK, TAKEN ONLY WHEN A GLYPH MUST BE LOADED: cairo's lock
// sets this scaled font's size on the FT face the uses share (text_shape's
// ScaledFontFace, the same lock, says why it must be cairo's), so a call
// served whole from the tables never touches the face.
class LazyFace {
public:
    explicit LazyFace(cairo_scaled_font_t* font) : font_(font) {}
    ~LazyFace() {
        if (face_ != nullptr) cairo_ft_scaled_font_unlock_face(font_);
    }
    LazyFace(const LazyFace&) = delete;
    LazyFace& operator=(const LazyFace&) = delete;
    FT_Face get() {
        if (face_ == nullptr) face_ = cairo_ft_scaled_font_lock_face(font_);
        return face_;
    }

private:
    cairo_scaled_font_t* font_;
    FT_Face              face_ = nullptr;
};

// WHICH FONT a raster or a box belongs to: the cairo font face (the file —
// the product's two live for the process) and the em in 26.6, the size
// cairo sets on the FT face for this scaled font. Never the scaled font's
// own address: the face owner rebuilds it when the em moves, and a freed
// one's address can be its successor's.
struct FontKey {
    const void* face = nullptr;
    uint32_t    em26 = 0;
    bool operator==(const FontKey&) const = default;
};
FontKey font_key(cairo_scaled_font_t* font) {
    cairo_matrix_t m;
    cairo_scaled_font_get_font_matrix(font, &m);
    return FontKey{cairo_scaled_font_get_font_face(font),
                   static_cast<uint32_t>(std::lround(m.xx * k64))};
}

uint64_t mix(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

// -- THE RASTER TABLE --------------------------------------------------------
//
// One glyph's coverage at one phase: its bitmap's rows in the arena
// (`offset`, `w` bytes a row, no padding) and where its first pixel stands
// from the whole part of the place (`left` right of it, `top` above the
// baseline's whole row — FreeType's bitmap_left / bitmap_top). A glyph with
// no ink (a space) is an entry of no size.
struct Raster {
    FontKey  font;
    uint32_t glyph  = 0;
    uint8_t  px     = 0;      // the place's fractions, in 64ths
    uint8_t  py     = 0;
    bool     used   = false;
    int16_t  left   = 0;
    int16_t  top    = 0;
    uint16_t w      = 0;
    uint16_t h      = 0;
    uint32_t offset = 0;
};

// THE BOUND (the head's THE COST AND THE CACHE): kMaxRasters entries in a
// table of twice as many slots (open addressing, linear probing, never more
// than half full) and kArenaBound bytes of coverage — about a Bold 24 px
// glyph's 300 bytes times the entries — the whole dropped at once when a
// run would pass either. A run is taken kRunGlyphs at a time, so one run's
// entries always fit a cleared table.
constexpr std::size_t kMaxRasters = 8192;
constexpr std::size_t kSlots      = 2 * kMaxRasters;
constexpr std::size_t kArenaBound = std::size_t{4} << 20;
constexpr int         kRunGlyphs  = 1024;

struct RasterTable {
    std::vector<Raster>  slots;
    std::vector<uint8_t> arena;
    std::size_t          count = 0;

    void reserve_once() {
        if (!slots.empty()) return;
        slots.resize(kSlots);
        arena.reserve(kArenaBound);
    }
    void clear() {
        std::fill(slots.begin(), slots.end(), Raster{});
        arena.clear();
        count = 0;
    }
};
RasterTable& raster_table() {
    static RasterTable table;
    return table;
}

std::size_t raster_slot(const FontKey& font, uint32_t glyph, unsigned px,
                        unsigned py) {
    const uint64_t h =
        mix(reinterpret_cast<uintptr_t>(font.face) ^
            (static_cast<uint64_t>(font.em26) << 40) ^
            (static_cast<uint64_t>(glyph) << 12) ^ (px << 6) ^ py);
    return static_cast<std::size_t>(h) & (kSlots - 1);
}

// The glyph's raster at the phase (px, py), from the table or rendered into
// it (the head's THE ROAD). The answer is an index: the arena may grow past
// its bound inside one run (dropped at the next run's start), so no pointer
// into it is held across a load.
std::size_t raster_of(RasterTable& t, LazyFace& face, const FontKey& font,
                      uint32_t glyph, unsigned px, unsigned py) {
    std::size_t i = raster_slot(font, glyph, px, py);
    for (;; i = (i + 1) & (kSlots - 1)) {
        Raster& r = t.slots[i];
        if (!r.used) break;
        if (r.glyph == glyph && r.px == px && r.py == py && r.font == font)
            return i;
    }
    Raster& r = t.slots[i];
    r.font  = font;
    r.glyph = glyph;
    r.px    = static_cast<uint8_t>(px);
    r.py    = static_cast<uint8_t>(py);
    r.used  = true;
    ++t.count;
    const FT_Face ft = face.get();
    // A glyph the face cannot load, or one with no outline, has no ink: an
    // entry of no size (cairo skips it the same way).
    if (ft == nullptr || FT_Load_Glyph(ft, glyph, kLoadFlags) != 0 ||
        ft->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        return i;
    // The fitted outline slid by the place's exact fraction: right by px,
    // DOWN by py (FreeType's y is up).
    FT_Outline_Translate(&ft->glyph->outline, static_cast<FT_Pos>(px),
                         -static_cast<FT_Pos>(py));
    if (FT_Render_Glyph(ft->glyph, FT_RENDER_MODE_NORMAL) != 0) return i;
    const FT_Bitmap& b = ft->glyph->bitmap;
    if (b.width == 0 || b.rows == 0 || b.buffer == nullptr ||
        b.pixel_mode != FT_PIXEL_MODE_GRAY)
        return i;
    r.left   = static_cast<int16_t>(ft->glyph->bitmap_left);
    r.top    = static_cast<int16_t>(ft->glyph->bitmap_top);
    r.w      = static_cast<uint16_t>(b.width);
    r.h      = static_cast<uint16_t>(b.rows);
    r.offset = static_cast<uint32_t>(t.arena.size());
    t.arena.resize(t.arena.size() + static_cast<std::size_t>(r.w) * r.h);
    uint8_t* out = t.arena.data() + r.offset;
    for (unsigned y = 0; y < b.rows; ++y)
        std::memcpy(out + static_cast<std::size_t>(y) * r.w,
                    b.buffer + static_cast<std::ptrdiff_t>(y) * b.pitch,
                    r.w);
    return i;
}

// -- THE BOX TABLE -----------------------------------------------------------
//
// One glyph's hinted outline's control box at the origin, in 26.6 (y up, as
// FreeType answers it); `inked` false for a glyph with no outline. Phase
// free: the outline is translated after the fit, so the box at a place is
// this box moved by the place. Bounded and cleared whole like the rasters.
struct Box {
    FontKey  font;
    uint32_t glyph = 0;
    bool     used  = false;
    bool     inked = false;
    FT_BBox  box{0, 0, 0, 0};
};
constexpr std::size_t kMaxBoxes = 2048;
constexpr std::size_t kBoxSlots = 2 * kMaxBoxes;
struct BoxTable {
    std::vector<Box> slots;
    std::size_t      count = 0;
};
BoxTable& box_table() {
    static BoxTable table;
    return table;
}

const Box& box_of(BoxTable& t, LazyFace& face, const FontKey& font,
                  uint32_t glyph) {
    if (t.slots.empty()) t.slots.resize(kBoxSlots);
    if (t.count >= kMaxBoxes) {
        std::fill(t.slots.begin(), t.slots.end(), Box{});
        t.count = 0;
    }
    std::size_t i = static_cast<std::size_t>(
                        mix(reinterpret_cast<uintptr_t>(font.face) ^
                            (static_cast<uint64_t>(font.em26) << 40) ^
                            glyph)) &
                    (kBoxSlots - 1);
    for (;; i = (i + 1) & (kBoxSlots - 1)) {
        Box& b = t.slots[i];
        if (!b.used) break;
        if (b.glyph == glyph && b.font == font) return b;
    }
    Box& b  = t.slots[i];
    b.font  = font;
    b.glyph = glyph;
    b.used  = true;
    ++t.count;
    const FT_Face ft = face.get();
    if (ft != nullptr && FT_Load_Glyph(ft, glyph, kLoadFlags) == 0 &&
        ft->glyph->format == FT_GLYPH_FORMAT_OUTLINE &&
        ft->glyph->outline.n_points > 0) {
        FT_Outline_Get_CBox(&ft->glyph->outline, &b.box);
        b.inked = b.box.xMax > b.box.xMin && b.box.yMax > b.box.yMin;
    }
    return b;
}

// A place's whole px and its fraction in 64ths, the nearest 64th (a
// fraction that rounds to a whole px is the next px at phase 0).
struct Place {
    int      whole = 0;
    unsigned frac  = 0;
};
Place place_of(double v) {
    const double f = std::floor(v);
    long frac = std::lround((v - f) * k64);
    int whole = static_cast<int>(f);
    if (frac >= k64) {
        frac = 0;
        ++whole;
    }
    return Place{whole, static_cast<unsigned>(frac)};
}

// One glyph of a run, resolved: its raster and its bitmap's first pixel in
// device px.
struct Stamp {
    std::size_t raster = 0;
    int         x      = 0;
    int         y      = 0;
};

// One run of at most kRunGlyphs: every glyph's raster summed, saturating,
// into the one mask and the mask composited at its whole place.
void show_run(cairo_t* cr, cairo_scaled_font_t* font, const FontKey& key,
              double tx, double ty, const cairo_glyph_t* glyphs, int count) {
    RasterTable& t = raster_table();
    t.reserve_once();
    // THE BOUND, tested where no index is held: a run that could pass the
    // entries, or an arena already past its bytes, starts on a cleared
    // table.
    if (t.count + static_cast<std::size_t>(count) > kMaxRasters ||
        t.arena.size() > kArenaBound)
        t.clear();
    // The run's two reused buffers (the head: nothing allocated per glyph,
    // nor per run once they have grown).
    static std::vector<Stamp>   stamps;
    static std::vector<uint8_t> mask;
    stamps.clear();
    LazyFace face(font);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
    for (int i = 0; i < count; ++i) {
        const Place px = place_of(glyphs[i].x + tx);
        const Place py = place_of(glyphs[i].y + ty);
        const std::size_t ri =
            raster_of(t, face, key, static_cast<uint32_t>(glyphs[i].index),
                      px.frac, py.frac);
        const Raster& r = t.slots[ri];
        if (r.w == 0 || r.h == 0) continue;
        const Stamp s{ri, px.whole + r.left, py.whole - r.top};
        if (!any) {
            x0 = s.x;
            y0 = s.y;
            x1 = s.x + r.w;
            y1 = s.y + r.h;
            any = true;
        } else {
            x0 = std::min(x0, s.x);
            y0 = std::min(y0, s.y);
            x1 = std::max(x1, s.x + r.w);
            y1 = std::max(y1, s.y + r.h);
        }
        stamps.push_back(s);
    }
    if (!any) return;
    const int w = x1 - x0;
    const int h = y1 - y0;
    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, w);
    if (stride <= 0) return;
    const std::size_t bytes =
        static_cast<std::size_t>(stride) * static_cast<std::size_t>(h);
    if (mask.size() < bytes) mask.resize(bytes);
    std::memset(mask.data(), 0, bytes);
    for (const Stamp& s : stamps) {
        const Raster& r = t.slots[s.raster];
        const uint8_t* src = t.arena.data() + r.offset;
        uint8_t* dst = mask.data() +
                       static_cast<std::size_t>(s.y - y0) * stride +
                       (s.x - x0);
        for (int y = 0; y < r.h; ++y, src += r.w, dst += stride)
            for (int x = 0; x < r.w; ++x) {
                const unsigned v = static_cast<unsigned>(dst[x]) + src[x];
                dst[x] = static_cast<uint8_t>(v > 255u ? 255u : v);
            }
    }
    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        mask.data(), CAIRO_FORMAT_A8, w, h, stride);
    // The mask's corner in user space: whole device px less the context's
    // translation.
    cairo_mask_surface(cr, surface, static_cast<double>(x0) - tx,
                       static_cast<double>(y0) - ty);
    // The surface's pixels are the reused buffer's: flushed and finished
    // here, before the next run writes it.
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
}

} // namespace

void show_glyphs(cairo_t* cr, cairo_scaled_font_t* font,
                 const cairo_glyph_t* glyphs, int count) {
    if (count <= 0) return;
    const FontKey key = font_key(font);
    // The place is taken in device px (the head's THE CONTEXT).
    cairo_matrix_t m;
    cairo_get_matrix(cr, &m);
    for (int at = 0; at < count; at += kRunGlyphs)
        show_run(cr, font, key, m.x0, m.y0, glyphs + at,
                 std::min(kRunGlyphs, count - at));
}

InkBox ink_box(cairo_scaled_font_t* font, const cairo_glyph_t* glyphs,
               int count) {
    InkBox out;
    if (count <= 0) return out;
    const FontKey key = font_key(font);
    BoxTable& t = box_table();
    LazyFace face(font);
    for (int i = 0; i < count; ++i) {
        const Box& b =
            box_of(t, face, key, static_cast<uint32_t>(glyphs[i].index));
        if (!b.inked) continue;
        const double gx0 = glyphs[i].x + static_cast<double>(b.box.xMin) / k64;
        const double gx1 = glyphs[i].x + static_cast<double>(b.box.xMax) / k64;
        // FreeType's y is up, the place's down.
        const double gy0 = glyphs[i].y - static_cast<double>(b.box.yMax) / k64;
        const double gy1 = glyphs[i].y - static_cast<double>(b.box.yMin) / k64;
        if (out.empty) {
            out = InkBox{gx0, gy0, gx1, gy1, false};
        } else {
            out.x0 = std::min(out.x0, gx0);
            out.y0 = std::min(out.y0, gy0);
            out.x1 = std::max(out.x1, gx1);
            out.y1 = std::max(out.y1, gy1);
        }
    }
    return out;
}

} // namespace text_raster
