#include "icons.h"

#include "platform.h"   // GuiPlatform::bundled_icon_files
#include "svg_icon.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
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
// default black for a `currentColor` file, which the fits measure (the fit
// is alpha alone) — and a BOUND file's bytes (one naming `currentColor`,
// icons.h's head), kept for its parses per color; loaded once by load_set
// before the window exists and never changed after.
//
// THE BOUND PARSES: per (drawing, color) the tree resvg built under that
// color, made the first time a draw asks — at most the tones a glyph's
// caller passes per drawing (the text tones, a lit key's print and its
// dimmed tone, per theme installed) — and held for the session.
//
// THE CACHES: per (drawing, ink px) a FIT, and per (drawing, ink px, color,
// x phase, y phase) its cropped raster (GlyphKey, draw_glyph). Main thread
// only: every draw is the painter's.
struct SurfaceDeleter {
    void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); }
};
using Surface = std::unique_ptr<cairo_surface_t, SurfaceDeleter>;

// A GLYPH'S FIT (icons.h's THE INK BOX): the zoom and the ink box's rect on
// rasterise_zoom's surface at that zoom.
struct Fit {
    double zoom = 0.0;
    int    x = 0, y = 0, w = 0, h = 0;
};

// ONE CROPPED RASTER'S KEY: the drawing, its ink px, its sRGB color and THE
// PLACE'S FRACTION ON EACH AXIS IN 64THS of a device px (architect
// 2026-11-15, the no-grid arc; icons.h's draw_glyph) — (0, 0) the raster of
// a whole place.
using GlyphKey = std::tuple<int, int, uint32_t, int, int>;

// THE RASTERS' BOUND: at this many the set is dropped whole and refills as
// it is asked (the shaping memo's rule, text_shape.h). WHY 1024: a seat at
// rest asks one phase — the roster's and the rows' glyphs in their few
// tones, a few hundred rasters at most — but a seat that MOVES by fractions
// (a list scrolled by the finger) can ask any of 64 phases an axis, a few
// hundred more over one gesture, and a bound those overran would drop the
// resting keys' rasters mid-gesture and re-render them (a resvg render
// each). A raster is its ink box's pixels, about 1.5 to 3 KiB at the
// product's sizes, so the bound is two or three MiB at its worst.
constexpr std::size_t kGlyphRasterBound = 1024;

struct LoadedSet {
    std::array<svg_icon::Document, kIconCount>  docs;
    std::array<std::string, kIconCount>         bound;   // a bound file's bytes
    std::map<std::pair<int, uint32_t>, svg_icon::Document> bound_docs;
    std::map<std::pair<int, int>, Fit>          fits;
    std::map<GlyphKey, Surface>                 glyphs;
};

std::optional<LoadedSet>& loaded_set() {
    static std::optional<LoadedSet> set;
    return set;
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

// THE FIT (icons.h's THE INK BOX), the mock's own walk: the probe at zoom 8,
// the zoom that puts the ink's larger side at `ink_px`, then 1 % steps down
// while the antialiased rim overruns. Measured on the check parse (alpha
// alone; no color moves it).
const Fit& fit_for(Icon icon, int ink_px) {
    LoadedSet& set = *loaded_set();
    const int index = static_cast<int>(icon);
    auto [it, fresh] = set.fits.try_emplace({index, ink_px});
    if (!fresh) return it->second;
    const svg_icon::Document& doc = set.docs[static_cast<size_t>(index)];
    constexpr double kProbeZoom = 8.0;
    Surface probe(svg_icon::rasterise_zoom(doc, kProbeZoom));
    const std::optional<AlphaBox> p = alpha_bounds(probe.get());
    if (!p) return it->second;   // no ink: the zero fit, nothing drawn
    double zoom = ink_px / (std::max(p->w, p->h) / kProbeZoom);
    for (;;) {
        Surface s(svg_icon::rasterise_zoom(doc, zoom));
        const std::optional<AlphaBox> b = alpha_bounds(s.get());
        if (!b) return it->second;
        if (std::max(b->w, b->h) <= ink_px) {
            it->second = Fit{zoom, b->x, b->y, b->w, b->h};
            return it->second;
        }
        zoom *= 0.99;
    }
}

} // namespace

std::optional<std::string> load_set() {
    const std::string label = "icons: ";
    auto bundle = GuiPlatform::bundled_icon_files();
    if (!bundle) return label + bundle.error();
    LoadedSet loaded;
    for (int i = 0; i < kIconCount; ++i) {
        const std::string file = std::string(kIconNames[i]) + ".svg";
        const auto found = bundle->find(file);
        if (found == bundle->end()) return label + file + ": missing";
        // Every file is parsed here, the error arm's one producer; a BOUND
        // file (one naming `currentColor`, icons.h's head) also keeps its
        // bytes for its parses per color (document, above).
        auto doc = svg_icon::parse(found->second);
        if (!doc) return label + file + ": " + doc.error();
        loaded.docs[static_cast<size_t>(i)] = std::move(*doc);
        if (found->second.find("currentColor") != std::string::npos)
            loaded.bound[static_cast<size_t>(i)] = found->second;
    }
    loaded_set() = std::move(loaded);
    return std::nullopt;
}

void drop_rasters() {
    if (!loaded_set()) return;
    loaded_set()->fits.clear();
    loaded_set()->glyphs.clear();
}

InkSize glyph_ink_size(Icon icon, int ink_px) {
    if (ink_px <= 0) return InkSize{};
    const Fit& fit = fit_for(icon, ink_px);
    return InkSize{fit.w, fit.h};
}

void draw_glyph(cairo_t* cr, Icon icon, double x, double y, int ink_px,
                uint32_t color) {
    if (ink_px <= 0) return;
    const Fit& fit = fit_for(icon, ink_px);
    if (fit.w <= 0 || fit.h <= 0) return;
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
    int wx = 0, wy = 0, px = 0, py = 0;
    split(x, wx, px);
    split(y, wy, py);
    const GlyphKey key{index, ink_px, rgb, px, py};
    auto it = set.glyphs.find(key);
    if (it == set.glyphs.end()) {
        // THE BOUND (kGlyphRasterBound): the set dropped whole before the
        // raster that would pass it.
        if (set.glyphs.size() >= kGlyphRasterBound) set.glyphs.clear();
        // THE CROP: the render at the fit's zoom under `color`, AT THE
        // PLACE'S FRACTION (the drawing moved right and down by it, in
        // device px — svg_icon::rasterise_zoom's phase), the fit's ink box
        // copied out whole — one column more where the x fraction pushes
        // the ink right of it, one row more where the y fraction pushes it
        // down: the hull of the box at this phase — into a surface of
        // exactly that size, then converted for the window. At phase (0, 0)
        // the render and the crop are the phase-less ones.
        Surface full(svg_icon::rasterise_zoom(document(index, rgb), fit.zoom,
                                              px / 64.0, py / 64.0));
        Surface glyph(cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, fit.w + (px > 0 ? 1 : 0),
            fit.h + (py > 0 ? 1 : 0)));
        cairo_t* c = cairo_create(glyph.get());
        cairo_set_operator(c, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface(c, full.get(), -fit.x, -fit.y);
        cairo_paint(c);
        cairo_destroy(c);
        svg_icon::convert_to_display(glyph.get());
        it = set.glyphs.emplace(key, std::move(glyph)).first;
    }
    cairo_save(cr);
    cairo_set_source_surface(cr, it->second.get(), wx, wy);
    cairo_paint(cr);
    cairo_restore(cr);
}

} // namespace icons
