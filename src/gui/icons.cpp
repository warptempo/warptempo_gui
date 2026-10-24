#include "icons.h"

#include "platform.h"   // GuiPlatform::bundled_icon_files
#include "materials.h"   // the disabled glyph's three tones (disabled_glyph_tones)
#include "svg_icon.h"

#include <array>
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include <utility>

namespace icons {
namespace {

// -- The loaded set and its rasters -------------------------------------------
//
// THE SET: per glyph, in enum order, its one parse and its body shift
// (declared in the file or measured at the parse), loaded once by load_set before the window exists
// and never changed after.
//
// THE CACHE: per (glyph, device px) the LIVE raster and, the first time a
// disabled face asks, its DISABLED copy, Cool Edit's emboss of the live mask
// over the drawing's own one-unit translate (draw_disabled) — two sizes per
// scale in use (the toolbar seat, icon_glyph_px, and the small icon's scaled
// 16 of the caption, the cards and the list rows), at most 4 x kIconCount
// surfaces. Main thread only: every draw is the
// painter's.
struct SurfaceDeleter {
    void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); }
};
using Surface = std::unique_ptr<cairo_surface_t, SurfaceDeleter>;

struct Faces {
    Surface live;
    Surface disabled;
    // The tones `disabled` was embossed in: the case's, so a bake that moves
    // them rebuilds the copy at its next ask.
    materials::DisabledGlyphTones disabled_tones{};
};

struct LoadedSet {
    std::array<svg_icon::Document, kIconCount> docs;
    std::array<std::pair<float, float>, kIconCount> shifts{};   // body shifts
    std::map<std::pair<int, int>, Faces>      rasters;
};

std::optional<LoadedSet>& loaded_set() {
    static std::optional<LoadedSet> set;
    return set;
}

const svg_icon::Document& document(int index) {
    return loaded_set()->docs[static_cast<size_t>(index)];
}

// The glyph's faces at `px`, the live one rasterised on a miss and converted
// for the window (svg_icon::convert_to_display). Asked only after the
// launch's load (gui_main fails before any window otherwise).
Faces& faces_for(Icon icon, int px) {
    LoadedSet& set = *loaded_set();
    const int index = static_cast<int>(icon);
    Faces& f = set.rasters[{index, px}];
    if (!f.live) {
        const auto [sx, sy] = set.shifts[static_cast<size_t>(index)];
        f.live.reset(svg_icon::rasterise(document(index), px, sx, sy));
        svg_icon::convert_to_display(f.live.get());
    }
    return f;
}

// ONE COPY OF A RASTER at an integer device px: the surface is the cell, so
// the copy is a straight OVER with no resample, and nothing is clipped (the
// drawing's own canvas cut it).
void paint_raster(cairo_t* cr, cairo_surface_t* s, double x, double y) {
    cairo_save(cr);
    cairo_set_source_surface(cr, s, std::nearbyint(x), std::nearbyint(y));
    cairo_paint(cr);
    cairo_restore(cr);
}

} // namespace

std::optional<std::string> load_set() {
    const std::string label = "icons: ";
    auto bundle = GuiPlatform::bundled_icon_files();
    if (!bundle) return label + bundle.error();
    LoadedSet loaded;
    for (int i = 0; i < kIconCount; ++i) {
        // THE PNG WINS (icons.h's load_set, architect 2026-10-19):
        // `<name>.png` if present, else `<name>.svg`; neither is missing.
        const std::string png = std::string(kIconNames[i]) + ".png";
        const std::string svg = std::string(kIconNames[i]) + ".svg";
        const auto found_png = bundle->find(png);
        const auto found_svg = bundle->find(svg);
        const bool is_png = found_png != bundle->end();
        if (!is_png && found_svg == bundle->end())
            return label + svg + ": missing";
        const std::string& file = is_png ? png : svg;
        // Every file is parsed here, the error arm's one producer.
        auto doc = is_png ? svg_icon::parse_png(found_png->second)
                          : svg_icon::parse(found_svg->second);
        if (!doc) return label + file + ": " + doc.error();
        // THE BODY-CENTERING SHIFT (icons.h's THE GLYPHS BODY-CENTERED): the
        // file's declared one where it carries it (every builder SVG), else
        // measured here (a dropped SVG, a PNG).
        loaded.shifts[static_cast<size_t>(i)] =
            doc->declared_shift ? *doc->declared_shift
                                : svg_icon::body_center_shift(*doc);
        loaded.docs[static_cast<size_t>(i)] = std::move(*doc);
    }
    loaded_set() = std::move(loaded);
    return std::nullopt;
}

void drop_rasters() {
    if (loaded_set()) loaded_set()->rasters.clear();
}

void draw(cairo_t* cr, Icon icon, double x, double y, double size_px) {
    const int px = static_cast<int>(std::nearbyint(size_px));
    if (px <= 0) return;
    paint_raster(cr, faces_for(icon, px).live.get(), x, y);
}

void draw_boxed(cairo_t* cr, Icon icon, int box_x, int box_y, int box_h) {
    if (box_h <= 0) return;
    const double spare = box_h * (1.0 - kGlyphBoxW / kGlyphBoxH);
    draw(cr, icon, static_cast<double>(box_x) - spare * 0.5,
         static_cast<double>(box_y), static_cast<double>(box_h));
}

void draw_disabled(cairo_t* cr, Icon icon, double x, double y,
                   double size_px) {
    const int px = static_cast<int>(std::nearbyint(size_px));
    if (px <= 0) return;
    Faces& f = faces_for(icon, px);
    const materials::DisabledGlyphTones tones = materials::disabled_glyph_tones();
    if (!f.disabled || !(f.disabled_tones == tones)) {
        // THE SILHOUETTE MASK IS THE LIVE RASTER'S ALPHA, which the window's
        // conversion keeps (svg_icon.h's convert_to_display). THE LIGHT MASK
        // IS THE DRAWING ITSELF TRANSLATED BY ONE ICON UNIT right and down
        // (architect 2026-10-15, "the glyph's own line thickness"), composed
        // into the render transform beside the glyph's body shift and
        // rasterised at that offset — exact antialiasing, never a shifted
        // raster, never rounded to a device px (1.25 px at 300 %, 0.58 at
        // 138) — onto a surface widened by the offset's ceiling so nothing it
        // carries past the cell is lost; only its alpha is read, so it is
        // never converted. The three sRGB tones — the case's, since
        // 2026-10-21 (materials::disabled_glyph_tones) — are converted after;
        // the cache is per device px and per tones, and drop_rasters empties
        // it at a scale change.
        LoadedSet& set = *loaded_set();
        const int index = static_cast<int>(icon);
        const auto [sx, sy] = set.shifts[static_cast<size_t>(index)];
        const svg_icon::Document& doc = document(index);
        const int margin =
            static_cast<int>(std::ceil(svg_icon::unit_px(doc, px)));
        Surface light(
            svg_icon::rasterise(doc, px, sx + 1.0, sy + 1.0, margin));
        f.disabled.reset(svg_icon::embossed_copy(
            f.live.get(), light.get(), tones.light, tones.shadow, tones.inset));
        f.disabled_tones = tones;
        svg_icon::convert_to_display(f.disabled.get());
    }
    paint_raster(cr, f.disabled.get(), x, y);
}

void draw_cased(cairo_t* cr, Icon icon, int case_x, int case_y,
                double size_px, int button_shift_px) {
    const double x = case_x + icon_case_lead_px() + button_shift_px;
    const double y = case_y + icon_case_lead_px() + button_shift_px;
    draw(cr, icon, x, y, size_px);
}

void draw_cased_disabled(cairo_t* cr, Icon icon, int case_x, int case_y,
                         double size_px, int button_shift_px) {
    const double x = case_x + icon_case_lead_px() + button_shift_px;
    const double y = case_y + icon_case_lead_px() + button_shift_px;
    draw_disabled(cr, icon, x, y, size_px);
}

} // namespace icons
