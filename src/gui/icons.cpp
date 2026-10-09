#include "icons.h"

#include "platform.h"   // GuiPlatform::bundled_icon_files
#include "display_transform.h"
#include "svg_icon.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <utility>

namespace icons {
namespace {

// -- The loaded set and its rasters -------------------------------------------
//
// THE SET: per glyph, in enum order, its one UNBOUND parse, or — for a BOUND
// file, one that paints in `currentColor` (icons.h's head) — its bytes,
// kept for a parse per color; loaded once by load_svg_set before the window
// exists and never changed after.
//
// THE BOUND PARSES: per (glyph, color) the tree resvg built under that
// color, made the first time a draw asks and held until drop_bound_faces.
//
// THE CACHE: per (glyph, device px, color) the LIVE raster and, the first
// time a disabled face asks, its DISABLED copy by the spec's rule
// (draw_disabled) — an unbound glyph's color is kUnbound, so it has two
// sizes per scale in use (the toolbar seat, icon_glyph_px, and the small
// icon's scaled 16 of the caption, the cards and the list rows), at most
// 4 x 59 surfaces under Tango or Mist; a bound one a few more, one per
// surface role it stands on. Main thread only: every draw is the painter's.
struct SurfaceDeleter {
    void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); }
};
using Surface = std::unique_ptr<cairo_surface_t, SurfaceDeleter>;

struct Faces {
    Surface live;
    Surface disabled;
};

// The color word of an unbound glyph's cache key: no 0xRRGGBB word reaches
// it.
constexpr uint32_t kUnbound = 0xFFFFFFFFu;

struct LoadedSet {
    std::array<svg_icon::Document, kIconCount> docs;    // unbound: the parse
    std::array<std::string, kIconCount>        bound;   // bound: the bytes
    std::map<std::pair<int, uint32_t>, svg_icon::Document> bound_docs;
    std::map<std::tuple<int, int, uint32_t>, Faces> rasters;
};

std::optional<LoadedSet>& loaded_set() {
    static std::optional<LoadedSet> set;
    return set;
}

bool is_bound(int index) {
    return !loaded_set()->bound[static_cast<size_t>(index)].empty();
}

// The cache key's color: the surface's text role as authored (sRGB, the
// raster converted for the window after, as every glyph's), or kUnbound.
uint32_t key_color(int index, GuiSurface surface) {
    if (!is_bound(index)) return kUnbound;
    return argb32_opaque_word(surface_text(surface)) & 0xFFFFFFu;
}

// The tree a raster of `index` under `color` renders: the unbound parse, or
// the bound file parsed under that color on a miss. The load parsed every
// bound file once already (its validation), so this parse cannot fail.
const svg_icon::Document& document(int index, uint32_t color) {
    LoadedSet& set = *loaded_set();
    if (color == kUnbound) return set.docs[static_cast<size_t>(index)];
    auto [it, fresh] = set.bound_docs.try_emplace({index, color});
    if (fresh) {
        auto doc = svg_icon::parse(set.bound[static_cast<size_t>(index)],
                                   color);
        assert(doc.has_value());
        it->second = std::move(*doc);
    }
    return it->second;
}

// The glyph's faces at `px` on `surface`, the live one rasterised on a miss
// and converted for the window (svg_icon::convert_to_display). Asked only
// after the launch's load (gui_main fails before any window otherwise).
Faces& faces_for(Icon icon, int px, GuiSurface surface) {
    LoadedSet& set = *loaded_set();
    const int index = static_cast<int>(icon);
    const uint32_t color = key_color(index, surface);
    Faces& f = set.rasters[{index, px, color}];
    if (!f.live) {
        f.live.reset(svg_icon::rasterise(document(index, color), px));
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
        // Every file is parsed here, the error arm's one producer; a BOUND
        // file (one naming `currentColor`, icons.h's head) keeps its bytes
        // for its parses per color (document, above), this parse only its
        // check.
        auto doc = svg_icon::parse(found->second, std::nullopt);
        if (!doc) return label + file + ": " + doc.error();
        if (found->second.find("currentColor") != std::string::npos)
            loaded.bound[static_cast<size_t>(i)] = found->second;
        else
            loaded.docs[static_cast<size_t>(i)] = std::move(*doc);
    }
    loaded_set() = std::move(loaded);
    return std::nullopt;
}

void drop_rasters() {
    if (loaded_set()) loaded_set()->rasters.clear();
}

void drop_bound_faces() {
    if (!loaded_set()) return;
    LoadedSet& set = *loaded_set();
    set.bound_docs.clear();
    std::erase_if(set.rasters, [](const auto& entry) {
        return std::get<2>(entry.first) != kUnbound;
    });
}

void draw(cairo_t* cr, Icon icon, double x, double y, double size_px,
          GuiSurface surface) {
    const int px = static_cast<int>(std::nearbyint(size_px));
    if (px <= 0) return;
    paint_raster(cr, faces_for(icon, px, surface).live.get(), x, y);
}

void draw_disabled(cairo_t* cr, Icon icon, double x, double y,
                   double size_px, GuiSurface surface) {
    const int px = static_cast<int>(std::nearbyint(size_px));
    if (px <= 0) return;
    Faces& f = faces_for(icon, px, surface);
    if (!f.disabled) {
        // THE DISABLED FACE IS DERIVED FROM THE sRGB RASTER (svg_icon.h's
        // convert_to_display): on a P3 window the live face is already
        // converted, so the derivation takes a fresh render of the same tree,
        // and its result is converted after — ReactOS's grays pass whole,
        // GTK's tinted pixels convert like any ink.
        Surface fresh;
        cairo_surface_t* source = f.live.get();
        if (display_transform::active()) {
            const int index = static_cast<int>(icon);
            fresh.reset(svg_icon::rasterise(
                document(index, key_color(index, surface)), px));
            source = fresh.get();
        }
        switch (live_chrome_spec().disabled_glyph) {
        case GuiDisabledGlyph::ReactOSSaturate:
            f.disabled.reset(svg_icon::saturated_copy(source));
            break;
        case GuiDisabledGlyph::GtkSaturatePixelate:
            f.disabled.reset(svg_icon::saturated_pixelated_copy(source));
            break;
        case GuiDisabledGlyph::MotifStipple:
            f.disabled.reset(svg_icon::stippled_copy(source));
            break;
        }
        svg_icon::convert_to_display(f.disabled.get());
    }
    paint_raster(cr, f.disabled.get(), x, y);
}

void draw_cased(cairo_t* cr, Icon icon, int case_x, int case_y,
                double size_px, int button_shift_px) {
    const double x = case_x + icon_case_lead_px() + button_shift_px;
    const double y = case_y + icon_case_lead_px() + button_shift_px;
    draw(cr, icon, x, y, size_px, GuiSurface::Face);
}

void draw_cased_disabled(cairo_t* cr, Icon icon, int case_x, int case_y,
                         double size_px, int button_shift_px) {
    const double x = case_x + icon_case_lead_px() + button_shift_px;
    const double y = case_y + icon_case_lead_px() + button_shift_px;
    draw_disabled(cr, icon, x, y, size_px, GuiSurface::Face);
}

} // namespace icons
