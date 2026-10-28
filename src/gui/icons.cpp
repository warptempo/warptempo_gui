#include "icons.h"

#include "platform.h"   // GuiPlatform::bundled_icon_files
#include "svg_icon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace icons {
namespace {

// -- The loaded set and its rasters -------------------------------------------
//
// THE SET: per drawing, in enum order, its one parse, loaded once by load_set
// before the window exists and never changed after.
//
// THE CACHE: per (drawing, device px) the LIVE raster and its ink box — the
// sizes in use are draw_boxed's seats, one per scale each (the logo lane's
// quaver at a 21-px ink at 300 %, the lists' 27-px ink seat, the cards' seat
// derived from the cap and the pad). Main thread only: every draw is the
// painter's.
struct SurfaceDeleter {
    void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); }
};
using Surface = std::unique_ptr<cairo_surface_t, SurfaceDeleter>;

struct Faces {
    Surface live;
    // The live raster's ink box (boxed_ink), measured at its first ask.
    std::optional<BoxedInk> ink;
};

struct LoadedSet {
    std::array<svg_icon::Document, kIconCount> docs;
    std::map<std::pair<int, int>, Faces>      rasters;
};

std::optional<LoadedSet>& loaded_set() {
    static std::optional<LoadedSet> set;
    return set;
}

// The drawing's faces at `px`, the live one rasterised on a miss and
// converted for the window (svg_icon::convert_to_display). Asked only after
// the launch's load (gui_main fails before any window otherwise).
Faces& faces_for(Icon icon, int px) {
    LoadedSet& set = *loaded_set();
    const int index = static_cast<int>(icon);
    Faces& f = set.rasters[{index, px}];
    if (!f.live) {
        f.live.reset(
            svg_icon::rasterise(set.docs[static_cast<size_t>(index)], px));
        svg_icon::convert_to_display(f.live.get());
    }
    return f;
}

// THE RASTER CELL'S LEFT EDGE AGAINST THE BOX'S (draw_boxed's declaration):
// half the cell's spare width left of the box, rounded once — the one
// offset draw_boxed copies at and boxed_ink measures from.
int boxed_cell_dx(int box_h) {
    const double spare = box_h * (1.0 - kGlyphBoxW / kGlyphBoxH);
    return static_cast<int>(std::nearbyint(-spare * 0.5));
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

// ONE COPY OF THE RASTER at an integer device px: the surface is the cell, so
// the copy is a straight OVER with no resample, and nothing is clipped (the
// drawing's own canvas cut it).
void draw_boxed(cairo_t* cr, Icon icon, int box_x, int box_y, int box_h) {
    if (box_h <= 0) return;
    cairo_save(cr);
    cairo_set_source_surface(cr, faces_for(icon, box_h).live.get(),
                             box_x + boxed_cell_dx(box_h), box_y);
    cairo_paint(cr);
    cairo_restore(cr);
}

// THE SHADOW, the same raster's alpha as a mask under a flat source: the
// mask is the cell draw_boxed copies, at the same whole-px offset plus the
// shadow's, so no resample either.
void draw_boxed_shadow(cairo_t* cr, Icon icon, int box_x, int box_y,
                       int box_h, GuiColor shadow) {
    if (box_h <= 0) return;
    const int t = cue_label_shadow_offset_px();
    cairo_save(cr);
    set_palette_source(cr, shadow);
    cairo_mask_surface(cr, faces_for(icon, box_h).live.get(),
                       box_x + boxed_cell_dx(box_h) + t, box_y + t);
    cairo_restore(cr);
}

BoxedInk boxed_ink(Icon icon, int box_h) {
    if (box_h <= 0) return BoxedInk{};
    Faces& f = faces_for(icon, box_h);
    if (!f.ink) {
        // THE ALPHA WALK over the live ARGB32 raster (premultiplied; the
        // window's conversion keeps alpha, svg_icon.h's convert_to_display):
        // the bounds of every pixel whose alpha is >= 128.
        cairo_surface_t* s = f.live.get();
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
                if ((row[x] >> 24) < 128) continue;
                l = std::min(l, x);
                r = std::max(r, x);
                t = std::min(t, y);
                b = std::max(b, y);
            }
        }
        const int dx = boxed_cell_dx(box_h);
        f.ink = r < 0 ? BoxedInk{}
                      : BoxedInk{l + dx, t, r + 1 + dx, b + 1};
    }
    return *f.ink;
}

} // namespace icons
