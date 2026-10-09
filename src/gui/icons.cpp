#include "icons.h"

#include "platform.h"   // GuiPlatform::bundled_icon_files
#include "display_transform.h"
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
// THE SET: per glyph, in enum order, its one parse, loaded once by
// load_svg_set before the window exists and never changed after.
//
// THE CACHE: per (glyph, device px) the LIVE raster and, the first time a
// disabled face asks, its DISABLED copy, ReactOS's saturate (draw_disabled)
// — two sizes per scale in use (the toolbar seat, icon_glyph_px, and the
// small icon's scaled 16 of the caption, the cards and the list rows), at
// most 4 x kIconCount surfaces. Main thread only: every draw is the
// painter's.
struct SurfaceDeleter {
    void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); }
};
using Surface = std::unique_ptr<cairo_surface_t, SurfaceDeleter>;

struct Faces {
    Surface live;
    Surface disabled;
};

struct LoadedSet {
    std::array<svg_icon::Document, kIconCount> docs;
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
        f.live.reset(svg_icon::rasterise(document(index), px));
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

std::optional<std::string> load_svg_set(std::string_view set) {
    const std::string label = "icon set '" + std::string(set) + "': ";
    auto bundle = GuiPlatform::bundled_icon_files(set);
    if (!bundle) return label + bundle.error();
    LoadedSet loaded;
    for (int i = 0; i < kIconCount; ++i) {
        const std::string file = std::string(kIconNames[i]) + ".svg";
        const auto found = bundle->find(file);
        if (found == bundle->end()) return label + file + ": missing";
        // Every file is parsed here, the error arm's one producer.
        auto doc = svg_icon::parse(found->second);
        if (!doc) return label + file + ": " + doc.error();
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

void draw_disabled(cairo_t* cr, Icon icon, double x, double y,
                   double size_px) {
    const int px = static_cast<int>(std::nearbyint(size_px));
    if (px <= 0) return;
    Faces& f = faces_for(icon, px);
    if (!f.disabled) {
        // THE DISABLED FACE IS DERIVED FROM THE sRGB RASTER (svg_icon.h's
        // convert_to_display): on a P3 window the live face is already
        // converted, so the derivation takes a fresh render of the same tree,
        // and its result is converted after (ReactOS's grays pass whole).
        Surface fresh;
        cairo_surface_t* source = f.live.get();
        if (display_transform::active()) {
            fresh.reset(svg_icon::rasterise(
                document(static_cast<int>(icon)), px));
            source = fresh.get();
        }
        f.disabled.reset(svg_icon::saturated_copy(source));
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
