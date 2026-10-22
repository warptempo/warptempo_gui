#include "materials.h"

#include "app_state.h"          // the lane accessors the bake's geometry reads
#include "cool_edit_derive.h"   // the tone rule, kTabBodies' Default row
#include "cool_edit_paint.h"    // the disabled emboss's measured tones and ratios
#include "device_config.h"      // device_config_path — the override folder beside it
#include "display_transform.h"
#include "modal_box.h"          // the render player's controls row
#include "paint_handler.h"      // program_case_rects, word_and_key_case_rects
#include "palette_file.h"       // program_palette_words' type
#include "platform.h"           // GuiPlatform::bundled_tile_names / _bytes
#include "svg_icon.h"           // the materials' road: raster_info, raster_regions

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

// The rules are at the declarations (materials.h).

namespace {

struct SurfaceDeleter {
    void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); }
};
using Surface = std::unique_ptr<cairo_surface_t, SurfaceDeleter>;
struct PatternDeleter {
    void operator()(cairo_pattern_t* p) const { cairo_pattern_destroy(p); }
};
using Pattern = std::unique_ptr<cairo_pattern_t, PatternDeleter>;

// ONE BAKED TILE: its rect in client coordinates, its window bytes and the
// pattern a fill takes them through (EXTEND_NONE, FILTER_NEAREST, its matrix
// the tile's offset, set at each use).
struct Tile {
    GuiRect rect{};
    Surface surface;
    Pattern pattern;
};

Tile make_tile(const GuiRect& r, Surface s) {
    Tile t;
    t.rect = r;
    t.surface = std::move(s);
    t.pattern.reset(cairo_pattern_create_for_surface(t.surface.get()));
    cairo_pattern_set_extend(t.pattern.get(), CAIRO_EXTEND_NONE);
    cairo_pattern_set_filter(t.pattern.get(), CAIRO_FILTER_NEAREST);
    return t;
}

// THE BAKE'S KEY (materials.h's BAKE RULE): the client size, the scale and
// the conversion — every lane, every seat and so every tile's rect is a
// function of them (program_case_rects' declaration), so a frame compares
// these four and never lays anything out.
struct Key {
    int  w = 0, h = 0, scale = 0;
    bool converted = false;
    bool operator==(const Key&) const = default;
};
Key key_of(const AppState& app) {
    return Key{app.width, app.height, gui_scale_percent(),
               display_transform::active()};
}

// THE WINDOW'S GEOMETRY A BAKE IS FOR — every tile's rect, laid out at the
// bake alone.
struct Geometry {
    int w = 0, h = 0;
    // Wood: its Face's sample above the panel [0, panel_top) and below it
    // [panel_bottom, h), window-wide (materials.h's TONE RULE); its tile
    // the band's top to the window's last row, window-wide (bake_wood).
    int panel_top = 0, panel_bottom = 0;
    // Metal: THE MARKER LANE'S ROWS, window-wide (materials.h's METAL,
    // 2026-10-22) — its tile and its Face's sample.
    int lane_y = 0, lane_h = 0;
    // The void's rows (the band).
    int band_y = 0, band_h = 0;
    // The inactive tab's body rows (the wood's, 2026-10-22).
    int tab_body_y0 = 0, tab_body_y1 = 0;
    // THE CASES (program_case_rects: every case's outer rect, both states of
    // the `h` view) and the case's line and glyph seat — the rows the
    // ebony's Face is sampled from (bake_case).
    std::vector<GuiRect> cases;
    int lw = 0, g = 0;
    // THE WORD BUTTONS AND THE KEYS (word_and_key_case_rects, 2026-10-22):
    // their outer rects, each its own w x h — patches alone, never the
    // Face's sample.
    std::vector<GuiRect> word_and_key_cases;
    // THE CAPTION'S ROW: the ebony's Face sample alone since 2026-10-22
    // (bake_case).
    int caption_y = 0, caption_h = 0;
};

Geometry geometry_of(const AppState& app) {
    Geometry g;
    g.w = app.width;
    g.h = app.height;
    const GuiRect strip = top_tab_strip_area(app);
    g.panel_top = strip.y + strip.h;
    g.panel_bottom = bottom_row_area(app).y;
    const GuiRect lane = top_marker_row_area(app);
    g.lane_y = lane.y;
    g.lane_h = lane.h;
    const GuiRect band = top_icon_row_area(app);
    g.band_y = band.y;
    g.band_h = band.h;
    g.lw = program_line_px();
    g.tab_body_y0 = strip.y + scaled_px(kProgramSpec.tab_inactive_top_px) + g.lw;
    g.tab_body_y1 = strip.y + tab_strip_recess_h_px();
    g.g = icon_case_w_px() - 3 * g.lw;
    g.cases = program_case_rects(app);
    g.word_and_key_cases = word_and_key_case_rects(app);
    const GuiRect caption = top_caption_row_area(app);
    g.caption_y = caption.y;
    g.caption_h = caption.h;
    return g;
}

uint32_t channel(uint32_t word, int c) { return (word >> (16 - 8 * c)) & 0xFF; }

uint32_t word_of(GuiColor c) {
    const auto byte = [](double u) {
        return static_cast<uint32_t>(
            std::clamp(std::nearbyint(u * 255.0), 0.0, 255.0));
    };
    return (byte(c.r) << 16) | (byte(c.g) << 8) | byte(c.b);
}

// Rec. 601 luma of a word (the mocks' luminance rule).
double luma(uint32_t w) {
    return 0.299 * channel(w, 0) + 0.587 * channel(w, 1) + 0.114 * channel(w, 2);
}

// A TONE KEY: the Face (-1), or kTones' row `i`.
constexpr int kFaceKey = -1;

// THE SETTLE GATE (materials.h's BAKE RULE): a size change of a baked
// window bakes once no size change has arrived for this long.
constexpr auto kBakeSettle = std::chrono::milliseconds(150);

struct State {
    bool     baked = false;
    Key      key;
    std::array<uint32_t, kGuiMaterialCount> faces{};
    // THE TILES (materials.h's BAKE RULE): the wood's ground and the
    // brass's lane, the void, the inactive tab's body (the wood's) and THE CASE
    // PATCHES, one per seat at rest and one pressed.
    std::vector<Tile> wood, metal, void_tiles, tab_body;
    std::vector<Tile> case_rest, case_down;
    // THE CLIENT THE PAINTERS PAINT (every pre-paint hook's): while a size
    // change waits on the gate the tiles are the last bake's, and none is
    // ever blitted outside this.
    int client_w = 0, client_h = 0;
    // THE GATE: a changed key seen by a frame and not yet baked (`deferred`),
    // the last size change's arrival (note_resize), and the tick's arming
    // (settle_due) — the frame that bakes is the whole client's
    // (take_bake_landed).
    bool deferred = false;
    bool armed    = false;
    // A BAKE LANDED and its palette install has not yet been answered by a
    // frame's whole-client damage (take_bake_landed).
    bool landed   = false;
    std::chrono::steady_clock::time_point last_resize{};
    int                    bevel_light = 100;
    int                    bevel_dark  = 100;
    int                    text_shadow = 2;
    GuiMaterialRegion      region = GuiMaterialRegion::Flat;
    // THE TONE TABLE (set_tone_face): a flat word and its key.
    std::vector<std::pair<uint32_t, int>> tones;
    int                                   recess_key = kFaceKey;
    // Each region's flat strokes, by the table's order (rebuilt with it and
    // at a bevel change).
    std::vector<uint32_t> wood_flat, metal_flat;
    materials::CaseLines  case_lines{};
    materials::DisabledGlyphTones disabled{kCeDisabledShadow, kCeDisabledLight,
                                           kCeDisabledInset};
};

State& state() {
    static State s;
    return s;
}

// THE FACTORS OF A TONE on a Face (materials.h's TONE RULE and BEVEL KEYS):
// per channel tone / Face, a lighter-than-Face tone's (its HLS L above the
// Face's) pulled toward 1 by the light key, a darker one's by the dark key —
// then the Face scaled by them, each channel rounded and clamped.
uint32_t beveled(uint32_t tone, uint32_t face) {
    using cool_edit_derive::hls_of;
    const State& s = state();
    const bool lighter = hls_of(tone).l > hls_of(face).l;
    const double p = (lighter ? s.bevel_light : s.bevel_dark) / 100.0;
    uint32_t out = 0;
    for (int c = 0; c < 3; ++c) {
        const double f0 = static_cast<double>(channel(tone, c)) /
                          std::max<double>(channel(face, c), 1.0);
        const double f = 1.0 + (f0 - 1.0) * p;
        out |= static_cast<uint32_t>(std::clamp(
                   std::nearbyint(static_cast<double>(channel(face, c)) * f),
                   0.0, 255.0))
               << (16 - 8 * c);
    }
    return out;
}

// The case's tone at HLS lightness offset `b` (saturation kept).
uint32_t case_tone(uint32_t face, double b) {
    return cool_edit_derive::tone(face, 1.0, b);
}

// EVERY FLAT STROKE, rebuilt off the Faces and the bevel keys.
void rebuild_flats() {
    State& s = state();
    const auto row = [&](uint32_t face) {
        std::vector<uint32_t> v;
        for (const auto& [word, key] : s.tones) {
            if (key == kFaceKey) { v.push_back(face); continue; }
            v.push_back(beveled(cool_edit_derive::tone_of(
                                    cool_edit_derive::kTones[static_cast<std::size_t>(key)],
                                    face, 0),
                                face));
        }
        return v;
    };
    const uint32_t wood = s.faces[static_cast<std::size_t>(GuiMaterial::Wood)];
    const uint32_t metal = s.faces[static_cast<std::size_t>(GuiMaterial::Metal)];
    const uint32_t ebony = s.faces[static_cast<std::size_t>(GuiMaterial::Case)];
    s.wood_flat = row(wood);
    s.metal_flat = row(metal);
    s.case_lines = materials::CaseLines{
        hex(beveled(case_tone(ebony, kCaseHighlight), ebony)),
        hex(beveled(case_tone(ebony, kCaseShadowFirst), ebony)),
        hex(beveled(case_tone(ebony, kCaseShadowLast), ebony)),
        hex(beveled(case_tone(ebony, kCaseCorner), ebony))};
    // THE DISABLED GLYPH'S TONES (materials.h): the live ramp's mean HLS
    // lightness over its twenty flat stops, his ratio onto it for the
    // silhouette, the rim black.
    constexpr int kCaseStopCount =
        static_cast<int>(cool_edit_derive::kCaseRamp.size());
    double mean_l = 0.0;
    for (int i = 0; i < kCaseStopCount; ++i) {
        const double b = kCaseRampTop + (kCaseRampBottom - kCaseRampTop) * i /
                                            static_cast<double>(kCaseStopCount - 1);
        mean_l += cool_edit_derive::hls_of(case_tone(ebony, b)).l;
    }
    mean_l /= kCaseStopCount;
    const double sil_l = mean_l * kCeDisabledShadowRatio;
    s.disabled = materials::DisabledGlyphTones{
        cool_edit_derive::tone(ebony, 0.0, sil_l), kCeDisabledLight,
        kCeDisabledInset};
}

// THE FACE'S RATIO of a tone to its Face, Rec. 601 luma (the luminance rule).
double luma_ratio(uint32_t tone, uint32_t face) {
    return luma(tone) / std::max(luma(face), 1e-6);
}

// THE CASE RAMP'S STOP on the seat's device row `row` of `g`
// (cool_edit_paint.h's FACE RAMP: stop i on [nearbyint(i·g/20),
// nearbyint((i+1)·g/20))).
constexpr int kCaseStops = static_cast<int>(cool_edit_derive::kCaseRamp.size());
int stop_of(int row, int g, int stops) {
    for (int i = 0; i < stops; ++i) {
        const int y1 = static_cast<int>(
            std::nearbyint(static_cast<double>(i + 1) * g / stops));
        if (row < y1) return i;
    }
    return stops - 1;
}
// The ramp's luma ratio at stop i (materials.h's CASE).
double case_ramp_ratio(int i, uint32_t ebony) {
    const double b = kCaseRampTop + (kCaseRampBottom - kCaseRampTop) * i /
                                        static_cast<double>(kCaseStops - 1);
    return luma_ratio(case_tone(ebony, b), ebony);
}
// The inactive tab body's luma ratio at stop k (materials.h's WOOD):
// kTabBodies' Default row, each row's HLS lightness over the Default Face's,
// taken onto the material's Face `face` (the wood's since 2026-10-22, the
// brass's until then — on a Face darker than HLS L .5 the ratio is the
// lightness ratio itself, the same on either).
double tab_body_ratio(int k, uint32_t face) {
    using namespace cool_edit_derive;
    const TabBody& d = tab_body(kDefaultFace);
    const double a = hls_of(d.rows[static_cast<std::size_t>(k)]).l /
                     hls_of(kDefaultFace).l;
    return luma_ratio(tone(face, a, 0.0), face);
}

// One pixel row's channels times `f`, rounded and clamped, opaque.
void scale_row(const uint32_t* src, uint32_t* dst, int w, double f) {
    for (int x = 0; x < w; ++x) {
        uint32_t word = 0xFF000000u;
        for (int c = 0; c < 3; ++c)
            word |= static_cast<uint32_t>(std::clamp(
                        std::nearbyint(static_cast<double>(channel(src[x], c)) * f),
                        0.0, 255.0))
                    << (16 - 8 * c);
        dst[x] = word;
    }
}

// A NEW SURFACE of `src`'s rows [y0, y0 + h) (client rows; `src_y` the
// source tile's first row), each row's pixels times its luma ratio — sRGB,
// converted for the window later.
Surface scaled_rows(cairo_surface_t* src, int src_y, int y0, int h,
                    const auto& ratio_of_row) {
    const int w = cairo_image_surface_get_width(src);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(src);
    cairo_surface_flush(out);
    const unsigned char* sdata = cairo_image_surface_get_data(src);
    const int sstride = cairo_image_surface_get_stride(src);
    unsigned char* ddata = cairo_image_surface_get_data(out);
    const int dstride = cairo_image_surface_get_stride(out);
    for (int y = 0; y < h; ++y)
        scale_row(reinterpret_cast<const uint32_t*>(
                      sdata + static_cast<std::size_t>(y0 + y - src_y) * sstride),
                  reinterpret_cast<uint32_t*>(ddata + static_cast<std::size_t>(y) * dstride),
                  w, ratio_of_row(y));
    cairo_surface_mark_dirty(out);
    return Surface(out);
}

// THE SAME IN PLACE: each of `s`'s rows times its luma ratio.
void scale_in_place(cairo_surface_t* s, const auto& ratio_of_row) {
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(data + static_cast<std::size_t>(y) * stride);
        scale_row(row, row, w, ratio_of_row(y));
    }
    cairo_surface_mark_dirty(s);
}

// THE MEAN of a set of surfaces' pixels, each channel's rounded sum.
uint32_t mean_of(const std::vector<Surface>& surfaces,
                 std::size_t first_n = static_cast<std::size_t>(-1)) {
    double sum[3] = {0.0, 0.0, 0.0};
    double n = 0.0;
    for (std::size_t i = 0; i < surfaces.size() && i < first_n; ++i) {
        cairo_surface_t* s = surfaces[i].get();
        cairo_surface_flush(s);
        const int w = cairo_image_surface_get_width(s);
        const int h = cairo_image_surface_get_height(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        for (int y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(data + y * stride);
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 3; ++c)
                    sum[c] += static_cast<double>(channel(row[x], c));
        }
        n += static_cast<double>(w) * h;
    }
    uint32_t out = 0;
    if (n <= 0.0) return out;
    for (int c = 0; c < 3; ++c)
        out |= static_cast<uint32_t>(std::nearbyint(sum[c] / n)) << (16 - 8 * c);
    return out;
}

// A BAKED SURFACE IN THE WINDOW'S BYTES (display_transform.h): converted in
// place, once, at the bake — the transform's word exactly, through its
// table-driven encode (p3_word_from_srgb_fast), a run of equal pixels
// converted once.
void convert(cairo_surface_t* s) {
    if (!display_transform::active()) return;
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    uint32_t last_in = 0xFFFFFFFFu, last_out = 0;
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(data + y * stride);
        for (int x = 0; x < w; ++x) {
            const uint32_t in = row[x] & 0xFFFFFFu;
            if (in != last_in) {
                last_in = in;
                last_out = 0xFF000000u | display_transform::p3_word_from_srgb_fast(in);
            }
            row[x] = last_out;
        }
    }
    cairo_surface_mark_dirty(s);
}

// -- THE CATALOG (materials.h's head: the drop folder by name) --------------

// THE TILE FILES THE CATALOG READS (architect 2026-10-22, the tuning road;
// materials.h's head): every name of the OVERRIDE FOLDER `tiles/` beside the
// device config, then every bundle name none of them shadows. `local` holds
// the override folder's names alone, each read from `folder`; every other
// name is the bundle's (GuiPlatform::bundled_tile_bytes).
struct TileFiles {
    std::filesystem::path    folder;
    std::vector<std::string> local;
    std::vector<std::string> names;
    // The override file's path for `name`, or an empty path: the bundle's.
    std::filesystem::path local_path(const std::string& name) const {
        return std::find(local.begin(), local.end(), name) != local.end()
                   ? folder / name
                   : std::filesystem::path{};
    }
};

// THE LISTING. NOTHING WHEN THE OVERRIDE FOLDER IS ABSENT (no file, no line,
// no card); its files filtered as the bundle's are (`.png` and `.jpg`, a
// symlink as the file it names). THE ERROR ARMS' PRODUCERS: the bundle's
// (platform_wayland.h's declaration) and an override folder that stands but
// will not list (IO), with the system's words.
std::expected<TileFiles, std::string> tile_files() {
    TileFiles f;
    const std::filesystem::path config = device_config_path();
    if (!config.empty()) {
        const std::filesystem::path folder = config.parent_path() / "tiles";
        std::error_code ec;
        if (std::filesystem::exists(folder, ec)) {
            f.folder = folder;
            std::filesystem::directory_iterator it(folder, ec);
            for (; !ec && it != std::filesystem::directory_iterator();
                 it.increment(ec)) {
                const std::string name = it->path().filename().string();
                if (!name.ends_with(".png") && !name.ends_with(".jpg")) continue;
                std::error_code tec;
                if (it->is_regular_file(tec)) f.local.push_back(name);
            }
        }
        if (ec)
            return std::unexpected("could not read '" + folder.string() + "': " +
                                   ec.message());
    }
    const auto bundle = GuiPlatform::bundled_tile_names();
    if (!bundle) return std::unexpected(bundle.error());
    f.names = f.local;
    for (const std::string& name : *bundle)
        if (std::find(f.local.begin(), f.local.end(), name) == f.local.end())
            f.names.push_back(name);
    return f;
}

// ONE TILE FILE'S BYTES, its first `limit` or the whole file, in one sized
// read: the override file at `local` when there is one, the bundle's `name`
// otherwise. The error arms' producers: a file that will not open or read
// whole (IO), the bundle's own.
std::expected<std::string, std::string> tile_bytes(
    const std::string& name, const std::filesystem::path& local,
    std::size_t limit = SIZE_MAX) {
    if (local.empty()) return GuiPlatform::bundled_tile_bytes(name, limit);
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(local, ec);
    if (ec)
        return std::unexpected("could not read '" + local.string() + "': " +
                               ec.message());
    std::FILE* f = std::fopen(local.c_str(), "rb");
    if (!f) return std::unexpected("could not open '" + local.string() + "'");
    std::string bytes(
        static_cast<std::size_t>(std::min<std::uintmax_t>(size, limit)), '\0');
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got != bytes.size())
        return std::unexpected("could not read '" + local.string() + "' whole");
    return bytes;
}

// The name a tile file's error line carries: the override file's path, so
// the line says which folder it came from, else the bundle's bare name.
std::string tile_label(const std::string& name,
                       const std::filesystem::path& local) {
    return local.empty() ? name : local.string();
}

// A COVER'S NAME, `<Name>-<W>x<H>.png`: its W and H, or false.
bool parse_cover(std::string_view file, std::string_view stem, int& w, int& h) {
    if (file.size() <= stem.size() + 5 || !file.starts_with(stem) ||
        file[stem.size()] != '-' || !file.ends_with(".png"))
        return false;
    const std::string_view mid =
        file.substr(stem.size() + 1, file.size() - stem.size() - 5);
    const std::size_t x = mid.find('x');
    if (x == std::string_view::npos) return false;
    const auto num = [](std::string_view t, int& v) {
        const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
        return !t.empty() && r.ec == std::errc{} && r.ptr == t.data() + t.size() &&
               v > 0;
    };
    return num(mid.substr(0, x), w) && num(mid.substr(x + 1), h);
}

struct Catalog {
    struct Cover {
        int         w = 0, h = 0;
        std::string file;
    };
    // Each material's master (the PNG winning; empty: none) and covers.
    std::array<std::string, kGuiMaterialCount>        master;
    std::array<std::vector<Cover>, kGuiMaterialCount> covers;
};

Catalog catalog_of(const std::vector<std::string>& names) {
    Catalog c;
    for (std::size_t m = 0; m < kGuiMaterialCount; ++m) {
        const std::string stem(kGuiMaterialNames[m]);
        bool png = false;
        for (const std::string& f : names) {
            int w = 0, h = 0;
            if (f == stem + ".png") { c.master[m] = f; png = true; }
            else if (f == stem + ".jpg" && !png) c.master[m] = f;
            else if (parse_cover(f, stem, w, h))
                c.covers[m].push_back(Catalog::Cover{w, h, f});
        }
    }
    return c;
}

// ONE MATERIAL'S SOURCE FOR THE CLIENT w x h (materials.h's COVER RULE):
// the smallest catalog cover reaching it in both dimensions (by area, the
// exact size the smallest of all) — its center crop, no filtering — else
// the master under the cover rule, else none.
struct Source {
    std::string file;
    bool        cover = false;
    int         cw = 0, ch = 0;   // a cover's size (its name's)
    std::filesystem::path local;   // the override file's path; empty: the bundle's
};
// A STRIP MATERIAL (materials.h's kGuiMaterialCoverDivisor) is matched
// against THE ROWS ITS TILE NEEDS for `h`, not the window's.
std::optional<Source> source_of(const Catalog& c, GuiMaterial m, int w, int h) {
    const std::size_t i = static_cast<std::size_t>(m);
    const Catalog::Cover* best = nullptr;
    for (const Catalog::Cover& cv : c.covers[i]) {
        if (cv.w < w || cv.h < h) continue;
        const long long area = static_cast<long long>(cv.w) * cv.h;
        if (!best || area < static_cast<long long>(best->w) * best->h)
            best = &cv;
    }
    if (best) return Source{best->file, true, best->w, best->h, {}};
    if (!c.master[i].empty()) return Source{c.master[i], false, 0, 0, {}};
    return std::nullopt;
}

// A STRIP MATERIAL'S PLACE (materials.h's STRIP RULE): the client rows
// [y, y + h) its tile holds, taken from its source's CENTER rows; the
// window's height over `divisor` the strip's.
struct StripRows {
    int y = 0, h = 0, divisor = 1;
};

// THE RAW REGIONS of one material for the client w x h (client rects in,
// every one inside the client; the sRGB surfaces out): its source read by
// name (the one file), a cover's center crop at the identity, a master under
// the cover rule — or, for a STRIP material (`strip` non-null), the strip
// rule: a cover's center columns and the center rows of its height, a
// master scaled to the window's width alone, its center rows.
std::expected<std::vector<Surface>, std::string> source_regions(
    const Source& src, int w, int h, const std::vector<GuiRect>& rects,
    const StripRows* strip = nullptr) {
    const auto bytes = tile_bytes(src.file, src.local);
    if (!bytes) return std::unexpected("tiles: " + bytes.error());
    const std::string label = tile_label(src.file, src.local);
    const auto info = svg_icon::raster_info(*bytes);
    if (!info) return std::unexpected("tiles: " + label + ": " + info.error());
    double k = 1.0;
    int ox = 0, oy = 0;
    if (src.cover) {
        if (static_cast<int>(info->w) != src.cw ||
            static_cast<int>(info->h) != src.ch)
            return std::unexpected("tiles: " + label + ": not " +
                                   std::to_string(src.cw) + "x" +
                                   std::to_string(src.ch));
        ox = (src.cw - w) / 2;
        oy = strip ? (src.ch - strip->h) / 2 - strip->y : (src.ch - h) / 2;
    } else if (strip) {
        // THE STRIP RULE (materials.h): one factor, the window's width over
        // the master's; the master too short for the strip at that factor
        // is the master's hard fail, never a stretch.
        k = static_cast<double>(w) / info->w;
        const int sw = std::max(w, static_cast<int>(std::nearbyint(info->w * k)));
        const int sh = static_cast<int>(std::nearbyint(info->h * k));
        if (sh < h / strip->divisor || sh < strip->h)
            return std::unexpected("tiles: " + label + ": too short for a 1/" +
                                   std::to_string(strip->divisor) + " strip at " +
                                   std::to_string(w) + "x" + std::to_string(h));
        ox = (sw - w) / 2;
        oy = (sh - strip->h) / 2 - strip->y;
    } else {
        // THE COVER RULE (materials.h): one factor, the overhang cropped
        // evenly (the tool's own arithmetic, tools/tiles/render_covers.py).
        k = std::max(static_cast<double>(w) / info->w,
                     static_cast<double>(h) / info->h);
        const int sw = std::max(w, static_cast<int>(std::nearbyint(info->w * k)));
        const int sh = std::max(h, static_cast<int>(std::nearbyint(info->h * k)));
        ox = (sw - w) / 2;
        oy = (sh - h) / 2;
    }
    std::vector<svg_icon::RasterRegion> regions;
    for (const GuiRect& r : rects)
        regions.push_back(svg_icon::RasterRegion{r.x + ox, r.y + oy, r.w, r.h});
    auto out = svg_icon::raster_regions(*bytes, *info, k, regions);
    if (!out) return std::unexpected("tiles: " + label + ": " + out.error());
    std::vector<Surface> owned;
    owned.reserve(out->size());
    for (cairo_surface_t* s : *out) owned.emplace_back(s);
    return owned;
}

// ONE MATERIAL'S BAKE, built on its own thread (materials.h's launch road):
// the Face, the tiles in sRGB converted for the window, each beside its
// rect. Nothing here touches State.
struct Baked {
    std::string                             error;   // empty: success
    uint32_t                                face = 0;
    // grounds: a material's ground tiles; extra: the wood's void; body: the
    // wood's inactive tab body; rest / down: the ebony's patches.
    std::vector<std::pair<GuiRect, Surface>> grounds, extra, body, rest, down;
    double                                  ms = 0.0;   // read, decode, derive
};

// Whether `inner` lies whole inside `outer`.
bool contains_rect(const GuiRect& outer, const GuiRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

std::vector<GuiRect> nonempty(std::vector<GuiRect> v) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const GuiRect& r) { return r.w <= 0 || r.h <= 0; }),
            v.end());
    return v;
}
GuiRect band_of(const Geometry& g, int y0, int y1) {
    return GuiRect{0, y0, g.w, std::max(0, y1 - y0)};
}

// A NEW SURFACE, `r` (client rect) cut out of `band`, whose first pixel is
// client (bx, by).
Surface cut(cairo_surface_t* band, int bx, int by, const GuiRect& r) {
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.w, r.h);
    cairo_surface_flush(band);
    cairo_surface_flush(out);
    const unsigned char* sdata = cairo_image_surface_get_data(band);
    const int sstride = cairo_image_surface_get_stride(band);
    unsigned char* ddata = cairo_image_surface_get_data(out);
    const int dstride = cairo_image_surface_get_stride(out);
    for (int y = 0; y < r.h; ++y)
        std::memcpy(ddata + static_cast<std::size_t>(y) * dstride,
                    sdata + static_cast<std::size_t>(r.y - by + y) * sstride +
                        static_cast<std::size_t>(r.x - bx) * 4,
                    static_cast<std::size_t>(r.w) * 4);
    cairo_surface_mark_dirty(out);
    return Surface(out);
}

// THE SUMS of `surface` (client rect `at`) over the parts of `rects` inside
// it, each channel's, added into `sum` and the pixel count into `n`.
void accumulate(cairo_surface_t* surface, const GuiRect& at,
                const std::vector<GuiRect>& rects, double (&sum)[3], double& n) {
    cairo_surface_flush(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (const GuiRect& r : rects) {
        const int x0 = std::max(r.x, at.x), y0 = std::max(r.y, at.y);
        const int x1 = std::min(r.x + r.w, at.x + at.w);
        const int y1 = std::min(r.y + r.h, at.y + at.h);
        for (int y = y0; y < y1; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(
                data + static_cast<std::size_t>(y - at.y) * stride);
            for (int x = x0; x < x1; ++x)
                for (int c = 0; c < 3; ++c)
                    sum[c] += static_cast<double>(channel(row[x - at.x], c));
        }
        if (x1 > x0 && y1 > y0) n += static_cast<double>(x1 - x0) * (y1 - y0);
    }
}
// THE MEAN of the sums, each channel's rounded quotient (0 where no pixel).
uint32_t mean_word(const double (&sum)[3], double n) {
    uint32_t out = 0;
    if (n <= 0.0) return out;
    for (int c = 0; c < 3; ++c)
        out |= static_cast<uint32_t>(std::nearbyint(sum[c] / n)) << (16 - 8 * c);
    return out;
}
// THE MEAN of `surface` (client rect `at`) over the parts of `rects` inside
// it.
uint32_t mean_over(cairo_surface_t* surface, const GuiRect& at,
                   const std::vector<GuiRect>& rects) {
    double sum[3] = {0.0, 0.0, 0.0};
    double n = 0.0;
    accumulate(surface, at, rects, sum, n);
    return mean_word(sum, n);
}

// THE WOOD (materials.h's WOOD, 2026-10-22: every ground but the marker
// lane's accent): ONE TILE, THE BAND'S TOP TO THE WINDOW'S LAST ROW,
// window-wide (the cards, the tooltip, the modal family and the keyboard's
// band stand anywhere below the caption); THE VOID, the band's rows times
// kWoodVoidLuma; THE INACTIVE TAB'S BODY, its rows window-wide, each W
// row's stop its luma ratio (tab_body_ratio). The caption's rows are
// DECODED as a region of their own for THE FACE's sample alone — the rows
// above the panel and below it, window-wide, exactly the pixels it was
// measured over before the wood took the panel (materials.h's TONE RULE),
// so no wood tone moved. The canvas interior is the GPU's, its rows of the
// tile never blitted.
void bake_wood(const Source& src, const Geometry& g, Baked& out) {
    // The caption's rows (the sample's alone) and the tile's, each decoded
    // only where it has rows; the tile's index last.
    const int band_y = std::clamp(g.band_y, 0, g.h);
    const std::vector<GuiRect> rects = nonempty(
        {band_of(g, 0, band_y), band_of(g, band_y, g.h)});
    if (rects.empty() || rects.back().y != band_y) return;
    auto raw = source_regions(src, g.w, g.h, rects);
    if (!raw) { out.error = raw.error(); return; }
    if (raw->size() != rects.size()) return;
    {
        const std::vector<GuiRect> sample = nonempty(
            {band_of(g, 0, g.panel_top), band_of(g, g.panel_bottom, g.h)});
        double sum[3] = {0.0, 0.0, 0.0};
        double n = 0.0;
        for (std::size_t i = 0; i < rects.size(); ++i)
            if ((*raw)[i]) accumulate((*raw)[i].get(), rects[i], sample, sum, n);
        out.face = mean_word(sum, n);
    }
    const std::size_t ti = rects.size() - 1;
    const GuiRect& at = rects[ti];
    if (!(*raw)[ti]) return;
    cairo_surface_t* tile = (*raw)[ti].get();
    if (g.band_h > 0 && g.band_y >= at.y && g.band_y + g.band_h <= at.y + at.h)
        out.extra.emplace_back(
            band_of(g, g.band_y, g.band_y + g.band_h),
            scaled_rows(tile, at.y, g.band_y, g.band_h,
                        [](int) { return kWoodVoidLuma; }));
    const int body_h = g.tab_body_y1 - g.tab_body_y0;
    if (body_h > 0 && g.tab_body_y0 >= at.y && g.tab_body_y1 <= at.y + at.h) {
        constexpr int kStops = 15;
        const uint32_t wood = out.face;
        out.body.emplace_back(
            band_of(g, g.tab_body_y0, g.tab_body_y1),
            scaled_rows(tile, at.y, g.tab_body_y0, body_h, [&](int y) {
                return tab_body_ratio(stop_of(y, body_h, kStops), wood);
            }));
    }
    out.grounds.emplace_back(at, std::move((*raw)[ti]));
}

// THE METAL, THE ACCENT (materials.h's METAL and STRIP RULE, 2026-10-22):
// ONE TILE, THE MARKER LANE'S ROWS window-wide, the strip's center rows; ITS
// FACE THE MEAN OF THOSE PIXELS, the strip's rows the bake reads (the whole
// client's panel rects 2026-10-21 to 2026-10-22) — read by the ruler's flat
// ground, the view bar's frame and the disabled cue's fades alone. (The whole-client tile, and the
// inactive tab's body under the luma ratio, 2026-10-21 to 2026-10-22.)
void bake_metal(const Source& src, const Geometry& g, Baked& out) {
    const GuiRect lane = band_of(g, std::clamp(g.lane_y, 0, g.h),
                                 std::clamp(g.lane_y + g.lane_h, 0, g.h));
    if (lane.h <= 0 || lane.w <= 0) return;
    const StripRows strip{lane.y, lane.h, kGuiMaterialCoverDivisor[static_cast<std::size_t>(GuiMaterial::Metal)]};
    auto raw = source_regions(src, g.w, g.h, {lane}, &strip);
    if (!raw) { out.error = raw.error(); return; }
    if (raw->empty() || !(*raw)[0]) return;
    out.face = mean_over((*raw)[0].get(), lane, {lane});
    out.grounds.emplace_back(lane, std::move((*raw)[0]));
}

// THE SEATS MERGED INTO TILES (materials.h's CASE PATCHES): seats sharing
// their rows (y and h) whose columns OVERLAP are one tile over the union of
// their columns — the on-screen keyboard's letter page and symbol pages lay
// different keys over the same rows, a word button's seat and a render
// player case's can share rows and columns — so no pixel is baked twice; a
// seat overlapping none (every band case, row 8's, the keys of a row all
// pages share) stays its own patch. The ramp is a function of the seat's
// row alone over its height, so a merged tile holds, at each seat it covers,
// exactly the bytes that seat's own patch would.
std::vector<GuiRect> merged_seats(std::vector<GuiRect> seats) {
    std::sort(seats.begin(), seats.end(), [](const GuiRect& a, const GuiRect& b) {
        if (a.y != b.y) return a.y < b.y;
        if (a.h != b.h) return a.h < b.h;
        return a.x < b.x;
    });
    std::vector<GuiRect> out;
    for (const GuiRect& r : seats) {
        if (!out.empty()) {
            GuiRect& last = out.back();
            if (last.y == r.y && last.h == r.h && r.x < last.x + last.w) {
                last.w = std::max(last.x + last.w, r.x + r.w) - last.x;
                continue;
            }
        }
        out.push_back(r);
    }
    return out;
}

// THE EBONY: THE CASE PATCHES — per case its glyph seat, the case less its
// lines on each axis (paint_ce_case: (w − 3·lw) x (h − 3·lw), square on the
// program's cases), at rest, (lw, lw) in, and pressed, (2·lw, 2·lw) in,
// merged where they overlap (merged_seats), each the cover at its own place
// under the ramp over its own height. The Face is THE ROWS THE PROGRAM'S
// CASES STAND ON — each seat row's g + lw rows, window-wide, decoded
// together with the caption row — the ebony's sample since the landing, so
// the patches move no tone a case derives from it (the caption row the
// sample's alone since its buttons left the ebony, 2026-10-22, materials.h's
// TONE RULE; the word buttons' and the keys' rows, ebony since 2026-10-22,
// never join it). A patch inside a decoded row is CUT from it; any other —
// a word button's, a key's — is decoded at its own rect; a patch past the
// window's edge is not baked.
void bake_case(const Source& src, const Geometry& g, Baked& out) {
    std::vector<int> tops;
    for (const GuiRect& c : g.cases)
        if (std::find(tops.begin(), tops.end(), c.y) == tops.end())
            tops.push_back(c.y);
    std::vector<GuiRect> rows{band_of(g, g.caption_y, g.caption_y + g.caption_h)};
    for (const int t : tops)
        rows.push_back(band_of(g, t + g.lw, t + 2 * g.lw + g.g));
    for (GuiRect& r : rows) {   // clipped to the client
        const int y0 = std::max(r.y, 0);
        const int y1 = std::min(r.y + r.h, g.h);
        r = GuiRect{0, y0, g.w, std::max(0, y1 - y0)};
    }
    // The rows' indices as decoded (an empty row seats nothing).
    std::vector<int> at(rows.size(), -1);
    std::vector<GuiRect> regions;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].w > 0 && rows[i].h > 0 && g.g > 0) {
            at[i] = static_cast<int>(regions.size());
            regions.push_back(rows[i]);
        }
    const std::size_t sample_n = regions.size();
    // THE SEATS, at rest and pressed, merged.
    std::vector<GuiRect> rest_seats, down_seats;
    for (const std::vector<GuiRect>* set : {&g.cases, &g.word_and_key_cases})
        for (const GuiRect& c : *set) {
            const int gw = c.w - 3 * g.lw;
            const int gh = c.h - 3 * g.lw;
            if (gw <= 0 || gh <= 0) continue;
            rest_seats.push_back(GuiRect{c.x + g.lw, c.y + g.lw, gw, gh});
            down_seats.push_back(GuiRect{c.x + 2 * g.lw, c.y + 2 * g.lw, gw, gh});
        }
    // EACH PATCH'S SOURCE: the decoded row holding it, else its own region
    // (inside the client), else none.
    struct Patch {
        GuiRect rect{};
        bool    down = false;
        int     row = -1;      // the decoded row's index in `rows`
        int     region = -1;   // its own region's index
    };
    std::vector<Patch> patches;
    const GuiRect client{0, 0, g.w, g.h};
    for (const bool down : {false, true})
        for (const GuiRect& r : merged_seats(down ? down_seats : rest_seats)) {
            Patch p{r, down};
            for (std::size_t i = 0; i < rows.size() && p.row < 0; ++i)
                if (at[i] >= 0 && contains_rect(rows[i], r))
                    p.row = static_cast<int>(i);
            if (p.row < 0) {
                if (!contains_rect(client, r)) continue;   // past the window's edge
                p.region = static_cast<int>(regions.size());
                regions.push_back(r);
            }
            patches.push_back(p);
        }
    auto raw = source_regions(src, g.w, g.h, regions);
    if (!raw) { out.error = raw.error(); return; }
    const uint32_t eb = out.face = mean_of(*raw, sample_n);
    std::array<double, kCaseStops> ramp{};
    for (int i = 0; i < kCaseStops; ++i)
        ramp[static_cast<std::size_t>(i)] = case_ramp_ratio(i, eb);
    for (Patch& p : patches) {
        Surface s = p.row >= 0
                        ? cut((*raw)[static_cast<std::size_t>(at[static_cast<std::size_t>(p.row)])].get(),
                              0, rows[static_cast<std::size_t>(p.row)].y, p.rect)
                        : std::move((*raw)[static_cast<std::size_t>(p.region)]);
        const int h = p.rect.h;
        scale_in_place(s.get(), [&](int y) {
            return ramp[static_cast<std::size_t>(stop_of(y, h, kCaseStops))];
        });
        (p.down ? out.down : out.rest).emplace_back(p.rect, std::move(s));
    }
}

// ONE MATERIAL'S WHOLE THREAD: the bake, then every tile in the window's
// bytes; its own time.
void bake_one(GuiMaterial m, const Source& src, const Geometry& g, Baked& out) {
    const auto t0 = std::chrono::steady_clock::now();
    switch (m) {
        case GuiMaterial::Wood:  bake_wood(src, g, out);  break;
        case GuiMaterial::Metal: bake_metal(src, g, out); break;
        case GuiMaterial::Case:  bake_case(src, g, out);  break;
    }
    if (out.error.empty())
        for (auto* set : {&out.grounds, &out.extra, &out.body, &out.rest, &out.down})
            for (auto& [rect, s] : *set) convert(s.get());
    out.ms = std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - t0)
                 .count();
}

// Whether `inner` lies whole inside `outer`.
bool contains(const GuiRect& outer, double x0, double y0, double x1, double y1) {
    return x0 >= outer.x && y0 >= outer.y && x1 <= outer.x + outer.w &&
           y1 <= outer.y + outer.h;
}

// THE PIXELS THE PAINTERS PAINT, in client coordinates: the client (State::
// client_w / client_h).
GuiRect client_rect() {
    const State& s = state();
    return GuiRect{0, 0, s.client_w, s.client_h};
}

// `t` as the source, its origin at its rect's corner.
void set_tile_source(cairo_t* cr, const Tile& t) {
    cairo_matrix_t at;
    cairo_matrix_init_translate(&at, -static_cast<double>(t.rect.x),
                                -static_cast<double>(t.rect.y));
    cairo_pattern_set_matrix(t.pattern.get(), &at);
    cairo_set_source(cr, t.pattern.get());
}

// THE TILE OF `tiles` holding the current path's extents WHERE THEY MEET
// THE CLIENT, or null where they miss it. A path may run past the window's
// edge in ordinary use (a cue label's face box near the right edge, a cue
// panned partly offscreen; 2026-10-21): the paint context is clipped to the
// client (both backends' paint walk), so only the part inside it is ever
// filled, and that part is what a tile must hold. A stale tile's pixel
// therefore never lands outside the client (the gate's guard): a tile of a
// larger, older window holds the intersection and the clip cuts the rest; a
// smaller, older one that misses part of it answers null, the flat mean.
const Tile* tile_under_path(cairo_t* cr, const std::vector<Tile>& tiles) {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    cairo_fill_extents(cr, &x0, &y0, &x1, &y1);
    const GuiRect c = client_rect();
    x0 = std::max(std::floor(x0), static_cast<double>(c.x));
    y0 = std::max(std::floor(y0), static_cast<double>(c.y));
    x1 = std::min(std::ceil(x1), static_cast<double>(c.x + c.w));
    y1 = std::min(std::ceil(y1), static_cast<double>(c.y + c.h));
    if (x1 <= x0 || y1 <= y0) return nullptr;
    for (const Tile& t : tiles)
        if (contains(t.rect, x0, y0, x1, y1)) return &t;
    return nullptr;
}

// THE GROUND TILES of a material: the ebony has none (its patches are
// seats, paint_case_ramp's, and its caption-button faces left 2026-10-22),
// so a rect of it is its flat mean.
const std::vector<Tile>& tiles_of(GuiMaterial m) {
    static const std::vector<Tile> kNone;
    State& s = state();
    switch (m) {
        case GuiMaterial::Wood:  return s.wood;
        case GuiMaterial::Metal: return s.metal;
        case GuiMaterial::Case:  return kNone;
    }
    return kNone;
}

std::size_t bytes_of(const std::vector<Tile>& tiles) {
    std::size_t n = 0;
    for (const Tile& t : tiles)
        if (t.surface)
            n += static_cast<std::size_t>(cairo_image_surface_get_stride(t.surface.get())) *
                 static_cast<std::size_t>(cairo_image_surface_get_height(t.surface.get()));
    return n;
}

// THE BAKE ITSELF (materials.h's BAKE RULE and LAUNCH ROAD) for geometry
// `g`: the tile files' names, each material's source, the three materials on
// three threads, then the tiles into State on this one.
std::optional<std::string> bake(const Geometry& g) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto files = tile_files();
    if (!files) return "tiles: " + files.error();
    const Catalog catalog = catalog_of(files->names);
    std::array<Source, kGuiMaterialCount> sources;
    for (std::size_t m = 0; m < kGuiMaterialCount; ++m) {
        // A strip material matched against its tile's rows (source_of).
        const bool strip = kGuiMaterialCoverDivisor[m] > 1;
        const auto src = source_of(catalog, static_cast<GuiMaterial>(m), g.w,
                                   strip ? g.lane_h : g.h);
        if (!src)
            return "tiles: " + std::string(kGuiMaterialNames[m]) + ": no cover reaches " +
                   std::to_string(g.w) + "x" + std::to_string(g.h) + " and no master";
        sources[m] = *src;
        sources[m].local = files->local_path(src->file);
    }
    // The transform's tables built here, before the threads read them.
    if (display_transform::active()) (void)display_transform::p3_word_from_srgb_fast(0);
    std::array<Baked, kGuiMaterialCount> baked;
    {
        std::array<std::jthread, kGuiMaterialCount> threads;
        for (std::size_t m = 0; m < kGuiMaterialCount; ++m)
            threads[m] = std::jthread([&, m] {
                bake_one(static_cast<GuiMaterial>(m), sources[m], g, baked[m]);
            });
    }   // joined
    for (const Baked& b : baked)
        if (!b.error.empty()) return b.error;

    State& s = state();
    s.wood.clear();
    s.metal.clear();
    s.void_tiles.clear();
    s.tab_body.clear();
    s.case_rest.clear();
    s.case_down.clear();
    const auto take = [](std::vector<std::pair<GuiRect, Surface>>& from,
                         std::vector<Tile>& to) {
        for (auto& [rect, surface] : from) to.push_back(make_tile(rect, std::move(surface)));
    };
    Baked& wood = baked[static_cast<std::size_t>(GuiMaterial::Wood)];
    Baked& metal = baked[static_cast<std::size_t>(GuiMaterial::Metal)];
    Baked& ebony = baked[static_cast<std::size_t>(GuiMaterial::Case)];
    take(wood.grounds, s.wood);
    take(wood.extra, s.void_tiles);
    take(wood.body, s.tab_body);
    take(metal.grounds, s.metal);
    take(ebony.rest, s.case_rest);
    take(ebony.down, s.case_down);
    for (std::size_t m = 0; m < kGuiMaterialCount; ++m) s.faces[m] = baked[m].face;
    s.baked = true;
    rebuild_flats();
    // THE TRACE (materials::trace_enabled; paint_handler.cpp's on_redraw the
    // other line): each material's file and thread time (its read, decode,
    // derived tiles and conversion), the bake's wall time and the resident
    // bytes.
    if (materials::trace_enabled())
        std::fprintf(stderr,
                     "warptempo_gui: materials baked %dx%d: decode wood %.1f "
                     "metal %.1f case %.1f ms (%s, %s, %s), wall %.1f ms, "
                     "resident %zu bytes\n",
                     g.w, g.h, wood.ms, metal.ms, ebony.ms,
                     sources[0].file.c_str(), sources[1].file.c_str(),
                     sources[2].file.c_str(),
                     std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - t0)
                         .count(),
                     materials::resident_bytes());
    return std::nullopt;
}

// A BAKE NOW for the app's geometry, `k` its key: the hard fail on a
// failure (the catalog changed under the running process), and the palette
// installed again off the new Faces — always, since the tiles every cached
// surface was painted from have moved even where the Faces have not, and
// the install's generation is what rebuilds those surfaces (render.h's
// palette_generation; the flag cache's label boxes among them) — and the
// landing flagged for the pre-paint hook's whole-client damage
// (take_bake_landed).
void bake_now(const AppState& app, const Key& k) {
    State& s = state();
    if (const std::optional<std::string> err = bake(geometry_of(app))) {
        std::fprintf(stderr, "warptempo_gui: %s\n", err->c_str());
        std::exit(1);
    }
    s.key = k;
    s.deferred = false;
    s.armed = false;
    s.landed = true;
    install_palette(program_palette_words());
}

} // namespace

namespace materials {

bool trace_enabled() {
    static const bool on = [] {
        if (const char* t = std::getenv("WT_PAINT_TRACE"); t && t[0] == '1')
            return true;
#if defined(__ANDROID__)
        char value[PROP_VALUE_MAX] = {};
        if (__system_property_get("debug.warptempo.trace", value) > 0 &&
            std::strcmp(value, "1") == 0)
            return true;
#endif
        return false;
    }();
    return on;
}

std::optional<std::string> check() {
    const auto files = tile_files();
    if (!files) return "tiles: " + files.error();
    // EACH FILE'S HEADER, read off its first bytes alone (a JPEG's frame
    // header can follow an ICC or EXIF segment, hence the 64 KiB): a PNG or
    // a JPEG, and a cover the size its name says, any height (a strip's
    // against its lane is the match's, source_of).
    constexpr std::size_t kHeaderBytes = 64 * 1024;
    for (const std::string& file : files->names) {
        const std::filesystem::path local = files->local_path(file);
        const auto head = tile_bytes(file, local, kHeaderBytes);
        if (!head) return "tiles: " + head.error();
        const auto info = svg_icon::raster_info(*head);
        if (!info) return "tiles: " + tile_label(file, local) + ": " + info.error();
        for (const char* name : kGuiMaterialNames) {
            int w = 0, h = 0;
            if (parse_cover(file, name, w, h) &&
                (static_cast<int>(info->w) != w || static_cast<int>(info->h) != h))
                return "tiles: " + tile_label(file, local) + ": not " +
                       std::to_string(w) + "x" + std::to_string(h);
        }
    }
    const Catalog catalog = catalog_of(files->names);
    for (std::size_t m = 0; m < kGuiMaterialCount; ++m) {
        const std::string stem(kGuiMaterialNames[m]);
        if (catalog.master[m].empty() && catalog.covers[m].empty())
            return "tiles: " + stem + ": missing (no " + stem + ".png, " + stem +
                   ".jpg or " + stem + "-<W>x<H>.png)";
    }
    return std::nullopt;
}

void ensure(const AppState& app) {
    if (app.width <= 0 || app.height <= 0) return;
    State& s = state();
    s.client_w = app.width;
    s.client_h = app.height;
    const Key k = key_of(app);
    if (s.baked && k == s.key) {
        s.deferred = false;
        s.armed = false;
        return;
    }
    bake_now(app, k);
}

void ensure_settled(const AppState& app) {
    if (app.width <= 0 || app.height <= 0) return;
    State& s = state();
    s.client_w = app.width;
    s.client_h = app.height;
    const Key k = key_of(app);
    if (s.baked && k == s.key) {
        s.deferred = false;
        s.armed = false;
        return;
    }
    // THE FIRST BAKE IS IMMEDIATE; a change of a baked one waits for the
    // tick's arming (settle_due), whose frame is the whole client's.
    if (!s.baked || s.armed) {
        bake_now(app, k);
        return;
    }
    s.deferred = true;
}

void note_resize() {
    State& s = state();
    s.last_resize = std::chrono::steady_clock::now();
    s.armed = false;
}

bool settle_due() {
    State& s = state();
    if (!s.deferred || s.armed) return false;
    if (std::chrono::steady_clock::now() - s.last_resize < kBakeSettle) return false;
    s.armed = true;
    return true;
}

bool take_bake_landed() {
    State& s = state();
    return std::exchange(s.landed, false);
}

bool baked() { return state().baked; }

uint32_t face(GuiMaterial m) {
    return state().faces[static_cast<std::size_t>(m)];
}

void set_bevel(int light_percent, int dark_percent) {
    State& s = state();
    s.bevel_light = light_percent;
    s.bevel_dark  = dark_percent;
    if (s.baked) rebuild_flats();
}

void set_text_shadow(int px) { state().text_shadow = px; }
int text_shadow_px() { return state().text_shadow; }

void set_tone_face(uint32_t chrome_face) {
    // THE SURFACE TONES (the declaration): the Face, then kTones' Face tones
    // a relief or a ground puts down — the text, the label and the cue's
    // shadow are ink and stay flat. A word two tones share (a dark Face can
    // round two near tones to one byte triple) answers the first.
    static constexpr std::string_view kSurfaceTones[] = {
        "recess", "mid", "gripper_dark", "dark", "tab_dark", "field_dark",
        "page_dark", "hilight", "gripper_light", "field_light", "tab_light",
        "tab_line"};
    State& s = state();
    s.tones.clear();
    s.tones.emplace_back(chrome_face & 0xFFFFFFu, kFaceKey);
    s.recess_key = kFaceKey;
    for (std::size_t i = 0; i < std::size(cool_edit_derive::kTones); ++i) {
        const cool_edit_derive::Tone& t = cool_edit_derive::kTones[i];
        if (t.source != cool_edit_derive::ToneSource::Face) continue;
        if (std::find(std::begin(kSurfaceTones), std::end(kSurfaceTones),
                      std::string_view(t.name)) == std::end(kSurfaceTones))
            continue;
        const uint32_t w = cool_edit_derive::tone_of(t, chrome_face, 0);
        const bool taken =
            std::any_of(s.tones.begin(), s.tones.end(),
                        [&](const auto& e) { return e.first == w; });
        if (!taken) s.tones.emplace_back(w, static_cast<int>(i));
        if (std::string_view(t.name) == "recess")
            s.recess_key = static_cast<int>(i);
    }
    if (s.baked) rebuild_flats();
}

namespace {
// The tone table's row of `c`, or -1.
int tone_row(GuiColor c) {
    const State& s = state();
    const uint32_t w = word_of(c);
    for (std::size_t i = 0; i < s.tones.size(); ++i)
        if (s.tones[i].first == w) return static_cast<int>(i);
    return -1;
}
const std::vector<uint32_t>* flats_of_region() {
    State& s = state();
    switch (s.region) {
        case GuiMaterialRegion::Wood:          return &s.wood_flat;
        case GuiMaterialRegion::Metal:         return &s.metal_flat;
        case GuiMaterialRegion::Flat:          return nullptr;
    }
    return nullptr;
}
} // namespace

bool set_source(cairo_t* cr, GuiColor c) {
    State& s = state();
    if (!s.baked || s.region == GuiMaterialRegion::Flat) return false;
    const int row = tone_row(c);
    if (row < 0) return false;
    const int key = s.tones[static_cast<std::size_t>(row)].second;
    const std::vector<uint32_t>& flats = *flats_of_region();
    // THE FACE, and the band's void: the tile under the path, else the flat.
    const std::vector<Tile>* tiles = nullptr;
    if (key == kFaceKey) {
        tiles = s.region == GuiMaterialRegion::Wood ? &s.wood : &s.metal;
    } else if (key == s.recess_key && s.region == GuiMaterialRegion::Wood) {
        tiles = &s.void_tiles;
    }
    if (tiles) {
        if (const Tile* t = tile_under_path(cr, *tiles)) {
            set_tile_source(cr, *t);
            return true;
        }
    }
    set_palette_source(cr, hex(flats[static_cast<std::size_t>(row)]));
    return true;
}

GuiColor flat(GuiColor c) {
    const State& s = state();
    if (!s.baked || s.region == GuiMaterialRegion::Flat) return c;
    const int row = tone_row(c);
    if (row < 0) return c;
    return hex((*flats_of_region())[static_cast<std::size_t>(row)]);
}

void paint_rect(cairo_t* cr, const GuiRect& r, GuiMaterial m) {
    if (r.w <= 0 || r.h <= 0) return;
    State& s = state();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // The flat mean under the whole rect (the panel's face before a bake),
    // then each tile's part over it, inside the client (the gate's guard).
    set_palette_source(cr, s.baked ? hex(s.faces[static_cast<std::size_t>(m)])
                                   : palette().face);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
    if (s.baked) {
        const int cx1 = std::min(r.x + r.w, s.client_w);
        const int cy1 = std::min(r.y + r.h, s.client_h);
        for (const Tile& t : tiles_of(m)) {
            const int x0 = std::max({r.x, t.rect.x, 0});
            const int y0 = std::max({r.y, t.rect.y, 0});
            const int x1 = std::min(cx1, t.rect.x + t.rect.w);
            const int y1 = std::min(cy1, t.rect.y + t.rect.h);
            if (x1 <= x0 || y1 <= y0) continue;
            set_tile_source(cr, t);
            cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);
}

CaseLines case_lines() { return state().case_lines; }
DisabledGlyphTones disabled_glyph_tones() { return state().disabled; }

void paint_case_ramp(cairo_t* cr, int x, int y, int w, int h, bool down) {
    if (w <= 0 || h <= 0) return;
    State& s = state();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (contains(client_rect(), x, y, x + w, y + h)) {
        // THE TILE ON THE SEAT'S ROWS holding its columns (a patch the seat's
        // own, or a merged tile over it — bake_case's merged_seats).
        for (const Tile& t : down ? s.case_down : s.case_rest) {
            if (t.rect.y != y || t.rect.h != h || x < t.rect.x ||
                x + w > t.rect.x + t.rect.w)
                continue;
            set_tile_source(cr, t);
            cairo_rectangle(cr, x, y, w, h);
            cairo_fill(cr);
            cairo_restore(cr);
            return;
        }
    }
    // NO PATCH FOR THIS SEAT (the declaration): the ramp's flat stops.
    const uint32_t eb = s.faces[static_cast<std::size_t>(GuiMaterial::Case)];
    for (int i = 0; i < kCaseStops; ++i) {
        const int y0 = static_cast<int>(std::nearbyint(static_cast<double>(i) * h / kCaseStops));
        const int y1 = static_cast<int>(std::nearbyint(static_cast<double>(i + 1) * h / kCaseStops));
        if (y1 <= y0) continue;
        const double b = kCaseRampTop + (kCaseRampBottom - kCaseRampTop) * i /
                                            static_cast<double>(kCaseStops - 1);
        set_palette_source(cr, hex(case_tone(eb, b)));
        cairo_rectangle(cr, x, y + y0, w, y1 - y0);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

void paint_tab_body(cairo_t* cr, const GuiRect& body) {
    if (body.w <= 0 || body.h <= 0) return;
    State& s = state();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_rectangle(cr, body.x, body.y, body.w, body.h);
    // The wood's flat mean where no baked row holds it (the panel's face
    // before a bake).
    const Tile* t = s.baked ? tile_under_path(cr, s.tab_body) : nullptr;
    if (t)            set_tile_source(cr, *t);
    else if (s.baked) set_palette_source(cr, hex(s.faces[static_cast<std::size_t>(GuiMaterial::Wood)]));
    else              set_palette_source(cr, palette().face);
    cairo_fill(cr);
    cairo_restore(cr);
}

std::size_t resident_bytes() {
    const State& s = state();
    std::size_t n = 0;
    for (const auto* set : {&s.wood, &s.metal, &s.void_tiles, &s.tab_body,
                            &s.case_rest, &s.case_down})
        n += bytes_of(*set);
    return n;
}

} // namespace materials

GuiMaterialScope::GuiMaterialScope(GuiMaterialRegion region)
    : previous_(state().region) {
    state().region = region;
}

GuiMaterialScope::~GuiMaterialScope() { state().region = previous_; }
