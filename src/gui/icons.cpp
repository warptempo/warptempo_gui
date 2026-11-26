#include "icons.h"

#include "platform.h"   // GuiPlatform::bundled_icon_files
#include "svg_icon.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace icons {
namespace {

// -- The loaded set and its rasters -------------------------------------------
//
// THE SET: per drawing, in enum order, the load's check parse — resvg's
// default black for a `currentColor` file — its MEASURES off that parse
// (the canvas's larger side and the ink, svg_icon::canvas_side / ink_bounds:
// geometry alone, no color moves them), and a BOUND file's bytes (one naming
// `currentColor`, icons.h's head), kept for its parses per color; loaded
// once by load_set before the window exists and never changed after.
//
// THE BOUND PARSES: per (drawing, color) the tree resvg built under that
// color, made the first time a draw asks — at most the tones a glyph's
// caller passes per drawing (the text tones, a lit key's print and its
// dimmed tone) — and held for the session.
//
// THE CACHE: per (drawing, zoom, color, x phase, y phase) its cropped raster
// (GlyphKey, draw_glyph). Main thread only: every draw is the painter's.
struct SurfaceDeleter {
    void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); }
};
using Surface = std::unique_ptr<cairo_surface_t, SurfaceDeleter>;

// ONE CROPPED RASTER'S KEY: the drawing, THE ZOOM its box gives (the exact
// double, compared exactly: icons.h's THE CACHE), its sRGB color and THE
// PLACE'S FRACTION ON EACH AXIS IN 64THS of a device px (architect
// 2026-11-15, the no-grid arc; icons.h's draw_glyph) — (0, 0) the raster of
// a whole place.
using GlyphKey = std::tuple<int, double, uint32_t, int, int>;

// ONE CROPPED RASTER: the hull of the render's ink and its top-left on the
// render (the offset the draw adds to the place's whole part); a null
// surface for a render with no ink at that zoom.
struct GlyphRaster {
    Surface surface;
    int     x = 0, y = 0;
};

// THE RASTERS' BOUND: at this many the set is dropped whole and refills as
// it is asked (the shaping memo's rule, text_shape.h). WHY 1024: a seat at
// rest asks one zoom and one phase per drawing — the roster's and the rows'
// glyphs in their few tones, a few hundred rasters at most — but a seat that
// MOVES by fractions (a list scrolled by the finger) can ask any of 64
// phases an axis, a few hundred more over one gesture, and a bound those
// overran would drop the resting keys' rasters mid-gesture and re-render
// them (a resvg render each). A raster is the hull of its ink, about 1.5 to
// 4 KiB at the product's sizes, so the bound is two to four MiB at its worst.
constexpr std::size_t kGlyphRasterBound = 1024;

struct LoadedSet {
    std::array<svg_icon::Document, kIconCount>  docs;
    std::array<std::string, kIconCount>         bound;   // a bound file's bytes
    // THE MEASURES, the drawing's own units (icons.h's THE BOX).
    std::array<double, kIconCount>                             canvas{};
    std::array<std::optional<svg_icon::InkBounds>, kIconCount> ink;
    std::map<std::pair<int, uint32_t>, svg_icon::Document> bound_docs;
    std::map<GlyphKey, GlyphRaster>             glyphs;
};

// THE SET, empty until load_set fills it.
std::optional<LoadedSet>& loaded_set() {
    static std::optional<LoadedSet> set;
    return set;
}

// THE SET'S LOAD (load_set's body): every enumerator's file found in `files`
// and parsed; a file of any other name in the folder (the README, the
// licence text) is never looked up.
std::expected<LoadedSet, std::string> load_files(
        const std::map<std::string, std::string>& files) {
    LoadedSet loaded;
    for (int i = 0; i < kIconCount; ++i) {
        const std::string file = std::string(kIconNames[i]) + ".svg";
        const auto found = files.find(file);
        if (found == files.end())
            return std::unexpected(file + ": missing");
        // Every file is parsed here, the error arm's one producer; a BOUND
        // file (one naming `currentColor`, icons.h's head) also keeps its
        // bytes for its parses per color (document, above).
        auto doc = svg_icon::parse(found->second);
        if (!doc) return std::unexpected(file + ": " + doc.error());
        loaded.canvas[static_cast<size_t>(i)] = svg_icon::canvas_side(*doc);
        loaded.ink[static_cast<size_t>(i)]    = svg_icon::ink_bounds(*doc);
        loaded.docs[static_cast<size_t>(i)]   = std::move(*doc);
        if (found->second.find("currentColor") != std::string::npos)
            loaded.bound[static_cast<size_t>(i)] = found->second;
    }
    return loaded;
}

// The tree a glyph of `index` renders under `color`: a bound file parsed
// under that color on a miss, else the drawing's one parse (its own inks).
// The load parsed every file once already (its check), so this parse cannot
// fail.
const svg_icon::Document& document(int index, uint32_t color) {
    LoadedSet& set = *loaded_set();
    const std::string& bytes = set.bound[static_cast<size_t>(index)];
    if (bytes.empty()) return set.docs[static_cast<size_t>(index)];
    auto [it, fresh] = set.bound_docs.try_emplace({index, color});
    if (fresh) {
        auto doc = svg_icon::parse(bytes, color & 0xFFFFFFu);
        assert(doc.has_value());
        it->second = std::move(*doc);
    }
    return it->second;
}

// The bounds of every pixel of `s` (ARGB32, premultiplied) whose alpha is
// non-zero: {x, y, w, h}, or nullopt for none.
struct AlphaBox {
    int x = 0, y = 0, w = 0, h = 0;
};
std::optional<AlphaBox> alpha_bounds(cairo_surface_t* s) {
    cairo_surface_flush(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    const int stride = cairo_image_surface_get_stride(s);
    int l = w, t = h, r = -1, b = -1;
    for (int y = 0; y < h; ++y) {
        const uint32_t* row =
            reinterpret_cast<const uint32_t*>(data + y * stride);
        for (int x = 0; x < w; ++x) {
            if ((row[x] >> 24) == 0) continue;
            l = std::min(l, x);
            r = std::max(r, x);
            t = std::min(t, y);
            b = std::max(b, y);
        }
    }
    if (r < 0) return std::nullopt;
    return AlphaBox{l, t, r + 1 - l, b + 1 - t};
}

} // namespace

std::optional<std::string> load_set() {
    const std::string label = "icons: ";
    auto bundle = GuiPlatform::bundled_icon_files();
    if (!bundle) return label + bundle.error();
    auto set = load_files(*bundle);
    if (!set) return label + set.error();
    loaded_set() = std::move(*set);
    return std::nullopt;
}

void drop_rasters() {
    std::optional<LoadedSet>& set = loaded_set();
    if (!set) return;
    set->glyphs.clear();
}

namespace {
// THE BOX'S ZOOM for `icon`: the box over the canvas's larger side (icons.h's
// THE BOX); 0 for a box of no size or a drawing with no ink.
double box_zoom(Icon icon, double box_px) {
    const LoadedSet& set = *loaded_set();
    const auto index = static_cast<size_t>(icon);
    if (!(box_px > 0.0) || !set.ink[index] || !(set.canvas[index] > 0.0))
        return 0.0;
    return box_px / set.canvas[index];
}
} // namespace

std::optional<GuiRectF> glyph_ink_in_box(Icon icon, double box_px) {
    const double zoom = box_zoom(icon, box_px);
    if (!(zoom > 0.0)) return std::nullopt;
    const svg_icon::InkBounds& ink = *loaded_set()->ink[static_cast<size_t>(icon)];
    return GuiRectF{ink.x * zoom, ink.y * zoom, ink.w * zoom, ink.h * zoom};
}

double glyph_box_for_ink_height(Icon icon, double ink_h_px) {
    const LoadedSet& set = *loaded_set();
    const auto index = static_cast<size_t>(icon);
    if (!set.ink[index] || !(set.ink[index]->h > 0.0)) return 0.0;
    return ink_h_px / set.ink[index]->h * set.canvas[index];
}

namespace {
// THE CROPPED RASTER of `icon` in the `box_px` box at the place (x, y) in
// `color` (draw_glyph's road, the declaration's THE CACHE), and the whole
// px it is copied at — the place's whole part plus the crop's offset; null
// for a drawing with no ink, a box of no size, or a render with no ink.
cairo_surface_t* placed_raster(Icon icon, double x, double y, double box_px,
                               uint32_t color, int& wx, int& wy) {
    const double zoom = box_zoom(icon, box_px);
    if (!(zoom > 0.0)) return nullptr;
    LoadedSet& set = *loaded_set();
    const int index = static_cast<int>(icon);
    const uint32_t rgb = color & 0xFFFFFFu;
    // THE PLACE'S WHOLE PART AND ITS FRACTION IN 64THS, each axis (the
    // nearest 64th; a fraction that rounds to a whole px is the next px at
    // phase 0) — the text's own resolution (text_raster.cpp's place_of).
    const auto split = [](double v, int& whole, int& phase) {
        const double f = std::floor(v);
        whole = static_cast<int>(f);
        phase = static_cast<int>(std::lround((v - f) * 64.0));
        if (phase >= 64) {
            phase = 0;
            ++whole;
        }
    };
    int px = 0, py = 0;
    split(x, wx, px);
    split(y, wy, py);
    const GlyphKey key{index, zoom, rgb, px, py};
    auto it = set.glyphs.find(key);
    if (it == set.glyphs.end()) {
        // THE BOUND (kGlyphRasterBound): the set dropped whole before the
        // raster that would pass it.
        if (set.glyphs.size() >= kGlyphRasterBound) set.glyphs.clear();
        // THE CROP: the render at the box's zoom under `color`, AT THE
        // PLACE'S FRACTION (the drawing moved right and down by it, in
        // device px — svg_icon::rasterise_zoom's phase), the hull of its
        // ink (alpha_bounds) copied out into a surface of exactly that size,
        // then converted for the window. Every pixel left out is wholly
        // transparent. At phase (0, 0) the render is the phase-less one.
        Surface full(svg_icon::rasterise_zoom(document(index, rgb), zoom,
                                              px / 64.0, py / 64.0));
        GlyphRaster raster;
        if (const std::optional<AlphaBox> b = alpha_bounds(full.get())) {
            raster.surface.reset(
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32, b->w, b->h));
            cairo_t* c = cairo_create(raster.surface.get());
            cairo_set_operator(c, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_surface(c, full.get(), -b->x, -b->y);
            cairo_paint(c);
            cairo_destroy(c);
            svg_icon::convert_to_display(raster.surface.get());
            raster.x = b->x;
            raster.y = b->y;
        }
        it = set.glyphs.emplace(key, std::move(raster)).first;
    }
    wx += it->second.x;
    wy += it->second.y;
    return it->second.surface.get();
}
} // namespace

void draw_glyph(cairo_t* cr, Icon icon, double x, double y, double box_px,
                uint32_t color) {
    int wx = 0, wy = 0;
    cairo_surface_t* raster = placed_raster(icon, x, y, box_px, color, wx, wy);
    if (raster == nullptr) return;
    cairo_save(cr);
    cairo_set_source_surface(cr, raster, wx, wy);
    cairo_paint(cr);
    cairo_restore(cr);
}

void draw_glyph_tinted(cairo_t* cr, Icon icon, double x, double y,
                       double box_px, uint32_t raster_color,
                       uint32_t window_word) {
    // A drawing with its own inks ignores the tone (icons.h's head): its
    // one raster.
    if (loaded_set()->bound[static_cast<size_t>(icon)].empty()) {
        draw_glyph(cr, icon, x, y, box_px, raster_color);
        return;
    }
    int wx = 0, wy = 0;
    cairo_surface_t* raster =
        placed_raster(icon, x, y, box_px, raster_color, wx, wy);
    if (raster == nullptr) return;
    cairo_save(cr);
    cairo_set_source_rgb(cr,
                         static_cast<double>((window_word >> 16) & 0xFFu) / 255.0,
                         static_cast<double>((window_word >> 8) & 0xFFu) / 255.0,
                         static_cast<double>(window_word & 0xFFu) / 255.0);
    cairo_mask_surface(cr, raster, wx, wy);
    cairo_restore(cr);
}

} // namespace icons
