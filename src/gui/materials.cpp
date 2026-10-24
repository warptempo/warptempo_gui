#include "materials.h"

#include "app_state.h"          // the lane accessors the bake's geometry reads
#include "cool_edit_paint.h"    // the key painter the lit patches are baked from
#include "device_config.h"      // device_config_path — the image road's folder beside it
#include "display_transform.h"
#include "paint_handler.h"      // program_case_rects
#include "palette_file.h"       // program_palette_words' type
#include "svg_icon.h"           // the image road's decode: raster_info, raster_regions

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <random>
#include <span>
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

// THE BAKE'S KEY (materials.h's BAKE RULE): the client size, the scale, the
// conversion and the LED glow (the strip pad a constant since 2026-10-23) — every lane, every seat and so every tile's rect and every
// lit patch is a function of them, so a frame compares these and never lays
// anything out.
struct Key {
    int  w = 0, h = 0, scale = 0, glow = 0;
    bool converted = false;
    bool operator==(const Key&) const = default;
};

// THE LED GLOW (materials.h's set_led_glow), device px; absent = 4.
int g_led_glow = 4;

Key key_of(const AppState& app) {
    return Key{app.width, app.height, gui_scale_percent(), g_led_glow,
               display_transform::active()};
}

// THE WINDOW'S GEOMETRY A BAKE IS FOR — every tile's rect, laid out at the
// bake alone.
struct Geometry {
    int w = 0, h = 0;
    // The band's top row: the ground tile's first.
    int band_y = 0;
    // THE MARKER LANE'S ROWS, window-wide — its tile.
    int lane_y = 0, lane_h = 0;
    // THE KEY SIZES the lit patches are baked for (program_case_rects'
    // distinct w x h).
    std::vector<std::pair<int, int>> key_sizes;
};

Geometry geometry_of(const AppState& app) {
    Geometry g;
    g.w = app.width;
    g.h = app.height;
    g.band_y = top_icon_row_area(app).y;
    const GuiRect lane = top_marker_row_area(app);
    g.lane_y = lane.y;
    g.lane_h = lane.h;
    for (const GuiRect& r : program_case_rects(app)) {
        const std::pair<int, int> s{r.w, r.h};
        if (r.w > 0 && r.h > 0 &&
            std::find(g.key_sizes.begin(), g.key_sizes.end(), s) == g.key_sizes.end())
            g.key_sizes.push_back(s);
    }
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

// THE SETTLE GATE (materials.h's BAKE RULE): a size change of a baked
// window bakes once no size change has arrived for this long.
constexpr auto kBakeSettle = std::chrono::milliseconds(150);

// ONE LIT KEY'S PATCH (materials.h's LIT KEYS): its size; a lit key is
// always down (cool_edit_paint.h's paint_key), so one patch a size.
struct LitPatch {
    int  w = 0, h = 0;
    Tile tile;
};

struct State {
    bool     baked = false;
    Key      key;
    std::array<uint32_t, kGuiMaterialCount> faces{kGroundMeanEstimate,
                                                  kMarkerBase};
    // THE TILES (materials.h's BAKE RULE): the ground's and the lane's.
    std::vector<Tile> ground, marker;
    // THE LIT KEYS' PATCHES, and the sizes they were baked for (kept so an
    // accent change rebuilds them without a layout).
    std::vector<LitPatch>             lit;
    std::vector<std::pair<int, int>>  lit_sizes;
    uint32_t                          accent = kLedLitRecord;
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
    // THE LANE'S PHASE (materials.h's LOOP), device px, accumulated, and
    // the displayed basis it last followed (follow_displayed_basis).
    double                 marker_phase = 0.0;
    bool                   phase_seen  = false;
    int64_t                phase_vp    = 0;
    double                 phase_spp   = 0.0;
    GuiMaterialRegion      region = GuiMaterialRegion::Flat;
};

State& state() {
    static State s;
    return s;
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

// THE MEAN of a surface's pixels, each channel's rounded quotient.
uint32_t mean_of(cairo_surface_t* s) {
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    double sum[3] = {0.0, 0.0, 0.0};
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(data + y * stride);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                sum[c] += static_cast<double>(channel(row[x], c));
    }
    const double n = static_cast<double>(w) * h;
    if (n <= 0.0) return 0;
    uint32_t out = 0;
    for (int c = 0; c < 3; ++c)
        out |= static_cast<uint32_t>(std::nearbyint(sum[c] / n)) << (16 - 8 * c);
    return out;
}

// -- THE PROCEDURAL BRUSHED GROUND (materials.h's head; make_pivot1.brushed)

// PIL's _gaussian_blur_radius: the extended box radius whose `passes`
// passes stand for a Gaussian of `sigma`.
double box_radius(double sigma, int passes) {
    const double sigma2 = sigma * sigma / passes;
    const double L = std::sqrt(12.0 * sigma2 + 1.0);
    const double l = std::floor((L - 1.0) / 2.0);
    double a = (2.0 * l + 1.0) * (l * (l + 1.0) - 3.0 * sigma2);
    a /= 6.0 * (sigma2 - (l + 1.0) * (l + 1.0));
    return l + a;
}

// PIL's extended box blur weights for a float radius: the whole window's
// 24-bit weight and each of its two fractional edges'.
struct BoxWeights {
    int      r  = 0;
    uint64_t ww = 0, fw = 0;
};
BoxWeights box_weights(double radius) {
    BoxWeights b;
    b.r  = static_cast<int>(radius);
    b.ww = static_cast<uint64_t>(static_cast<double>(1u << 24) / (radius * 2.0 + 1.0));
    b.fw = ((uint64_t{1} << 24) - static_cast<uint64_t>(b.r * 2 + 1) * b.ww) / 2;
    return b;
}

// ONE HORIZONTAL PASS over every row of the w x h byte field, in place
// (PIL's ImagingLineBoxBlur: the edges replicated, each output rounded to
// its byte).
void blur_rows(std::vector<uint8_t>& f, int w, int h, const BoxWeights& b) {
    std::vector<uint8_t> line(static_cast<std::size_t>(w));
    const auto at = [&](int i) {
        return static_cast<uint64_t>(line[static_cast<std::size_t>(std::clamp(i, 0, w - 1))]);
    };
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f.data() + static_cast<std::size_t>(y) * w;
        std::memcpy(line.data(), row, static_cast<std::size_t>(w));
        uint64_t acc = 0;
        for (int k = -b.r; k <= b.r; ++k) acc += at(k);
        for (int x = 0; x < w; ++x) {
            const uint64_t v = acc * b.ww + (at(x - b.r - 1) + at(x + b.r + 1)) * b.fw;
            row[x] = static_cast<uint8_t>(std::min<uint64_t>((v + (1u << 23)) >> 24, 255));
            acc += at(x + b.r + 1) - at(x - b.r);
        }
    }
}

// ONE VERTICAL PASS, the same blur down every column, walked row by row
// (a column's running sum per x) so the field is read in its own order.
void blur_columns(std::vector<uint8_t>& f, int w, int h, const BoxWeights& b) {
    const std::vector<uint8_t> in = f;
    const auto row_of = [&](int y) {
        return in.data() + static_cast<std::size_t>(std::clamp(y, 0, h - 1)) * w;
    };
    std::vector<uint64_t> acc(static_cast<std::size_t>(w), 0);
    for (int k = -b.r; k <= b.r; ++k) {
        const uint8_t* r = row_of(k);
        for (int x = 0; x < w; ++x) acc[static_cast<std::size_t>(x)] += r[x];
    }
    for (int y = 0; y < h; ++y) {
        const uint8_t* ea = row_of(y - b.r - 1);
        const uint8_t* eb = row_of(y + b.r + 1);
        const uint8_t* add = row_of(y + b.r + 1);
        const uint8_t* sub = row_of(y - b.r);
        uint8_t* out = f.data() + static_cast<std::size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            uint64_t& a = acc[static_cast<std::size_t>(x)];
            const uint64_t v = a * b.ww + (static_cast<uint64_t>(ea[x]) + eb[x]) * b.fw;
            out[x] = static_cast<uint8_t>(std::min<uint64_t>((v + (1u << 23)) >> 24, 255));
            a += static_cast<uint64_t>(add[x]) - sub[x];
        }
    }
}

// NUMPY'S RandomState(seed).rand(...) AS BYTES (the legacy MT19937, its
// integer seed init_genrand's, std::mt19937's own; each double off two
// 32-bit draws), cast to float32, times 255 and truncated as numpy's
// astype(uint8) does — rows [y0, y0 + h) of a field `w` wide.
std::vector<uint8_t> noise_bytes(uint32_t seed, int w, int y0, int h) {
    std::mt19937 rng(seed);
    rng.discard(static_cast<unsigned long long>(y0) * static_cast<unsigned long long>(w) * 2);
    std::vector<uint8_t> f(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (std::size_t i = 0; i < f.size(); ++i) {
        const uint32_t a = static_cast<uint32_t>(rng()) >> 5;
        const uint32_t b = static_cast<uint32_t>(rng()) >> 6;
        const double d = (a * 67108864.0 + b) / 9007199254740992.0;
        const float u = static_cast<float>(d) * 255.0f;
        f[i] = static_cast<uint8_t>(u);
    }
    return f;
}

// THE BRUSHED FACE (make_pivot1.brushed) over rows [y0, y0 + rows) of a
// field `w` x `field_h`, into a new opaque ARGB32 surface w x rows, sRGB:
// the noise, PIL's GaussianBlur((9, 0.4)) — three passes along x, then
// three down y — normalized to zero mean and unit deviation over the rows
// drawn, times `strength` on `base`, under (1 − vgrad·y²) with y −1 .. 1
// over the field's rows. The y passes reach three rows past the drawn ones,
// so the rows drawn are the field's own (a margin is drawn and dropped).
Surface brushed(uint32_t base, uint32_t seed, double strength, double vgrad,
                int w, int field_h, int y0, int rows) {
    if (w <= 0 || rows <= 0) return nullptr;
    constexpr int kPasses = 3;
    const int m0 = std::max(0, y0 - kPasses);
    const int m1 = std::min(field_h, y0 + rows + kPasses);
    const int fh = m1 - m0;
    std::vector<uint8_t> f = noise_bytes(seed, w, m0, fh);
    const BoxWeights bx = box_weights(box_radius(kBrushSigmaX, kPasses));
    const BoxWeights by = box_weights(box_radius(kBrushSigmaY, kPasses));
    for (int p = 0; p < kPasses; ++p) blur_rows(f, w, fh, bx);
    for (int p = 0; p < kPasses; ++p) blur_columns(f, w, fh, by);
    // The drawn rows' statistics (numpy's mean and population deviation of
    // the field's bytes over 255).
    const std::size_t first = static_cast<std::size_t>(y0 - m0) * w;
    const std::size_t count = static_cast<std::size_t>(rows) * w;
    double sum = 0.0, sum2 = 0.0;
    for (std::size_t i = first; i < first + count; ++i) {
        const double v = f[i] / 255.0;
        sum += v;
        sum2 += v * v;
    }
    const double mean = sum / static_cast<double>(count);
    const double sd = std::sqrt(std::max(0.0, sum2 / static_cast<double>(count) - mean * mean));
    const double k = strength / (sd + 1e-6);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, rows);
    cairo_surface_flush(s);
    unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    const double cb[3] = {static_cast<double>(channel(base, 0)),
                          static_cast<double>(channel(base, 1)),
                          static_cast<double>(channel(base, 2))};
    for (int y = 0; y < rows; ++y) {
        const int fy = y0 + y;
        const double t = field_h > 1 ? -1.0 + 2.0 * fy / (field_h - 1) : 0.0;
        const double g = 1.0 - vgrad * t * t;
        const uint8_t* src = f.data() + first + static_cast<std::size_t>(y) * w;
        auto* row = reinterpret_cast<uint32_t*>(data + static_cast<std::size_t>(y) * stride);
        for (int x = 0; x < w; ++x) {
            const double n = 1.0 + (src[x] / 255.0 - mean) * k;
            uint32_t word = 0xFF000000u;
            for (int c = 0; c < 3; ++c)
                word |= static_cast<uint32_t>(std::clamp(std::nearbyint(cb[c] * n * g), 0.0, 255.0))
                        << (16 - 8 * c);
            row[x] = word;
        }
    }
    cairo_surface_mark_dirty(s);
    return Surface(s);
}

// THE SEAMLESS STRIP (materials.h's LOOP (1)), in place: out(x) =
// (1 − w)·A(x) + w·A((x + W/2) mod W), w = |2x/W − 1|, per channel,
// rounded.
void make_seamless(cairo_surface_t* s) {
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    if (w < 2) return;
    std::vector<uint32_t> a(static_cast<std::size_t>(w));
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(data + static_cast<std::size_t>(y) * stride);
        std::memcpy(a.data(), row, static_cast<std::size_t>(w) * 4);
        for (int x = 0; x < w; ++x) {
            const double t = std::abs(2.0 * x / w - 1.0);
            const uint32_t p = a[static_cast<std::size_t>(x)];
            const uint32_t q = a[static_cast<std::size_t>((x + w / 2) % w)];
            uint32_t word = 0xFF000000u;
            for (int c = 0; c < 3; ++c)
                word |= static_cast<uint32_t>(std::clamp(
                            std::nearbyint((1.0 - t) * channel(p, c) + t * channel(q, c)),
                            0.0, 255.0))
                        << (16 - 8 * c);
            row[x] = word;
        }
    }
    cairo_surface_mark_dirty(s);
}

// The strip beside a copy of itself, twice its width (materials.h's LOOP
// (3)).
Surface doubled(cairo_surface_t* one) {
    const int w = cairo_image_surface_get_width(one);
    const int h = cairo_image_surface_get_height(one);
    cairo_surface_t* two = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 2 * w, h);
    cairo_surface_flush(one);
    cairo_surface_flush(two);
    const unsigned char* sdata = cairo_image_surface_get_data(one);
    const int sstride = cairo_image_surface_get_stride(one);
    unsigned char* ddata = cairo_image_surface_get_data(two);
    const int dstride = cairo_image_surface_get_stride(two);
    const std::size_t row_bytes = static_cast<std::size_t>(w) * 4;
    for (int y = 0; y < h; ++y) {
        const unsigned char* srow = sdata + static_cast<std::size_t>(y) * sstride;
        unsigned char* drow = ddata + static_cast<std::size_t>(y) * dstride;
        std::memcpy(drow, srow, row_bytes);
        std::memcpy(drow + row_bytes, srow, row_bytes);
    }
    cairo_surface_mark_dirty(two);
    return Surface(two);
}

// -- THE IMAGE ROAD (materials.h's head) -----------------------------------

// THE ROAD'S FILE FOR A ROLE: `<Name>.png`, else `<Name>.jpg`, in the
// `tiles/` folder beside the device config; an empty path where neither
// stands (or no folder, or no config).
std::filesystem::path image_of(GuiMaterial m) {
    const std::filesystem::path config = device_config_path();
    if (config.empty()) return {};
    const std::filesystem::path folder = config.parent_path() / "tiles";
    const std::string stem(kGuiMaterialNames[static_cast<std::size_t>(m)]);
    for (const char* ext : {".png", ".jpg"}) {
        const std::filesystem::path p = folder / (stem + ext);
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    return {};
}

// ONE FILE'S BYTES, its first `limit` or the whole file, in one sized read.
// The error arms' producers: a file that will not open or read whole (IO).
std::expected<std::string, std::string> file_bytes(
    const std::filesystem::path& path, std::size_t limit = SIZE_MAX) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec)
        return std::unexpected("could not read '" + path.string() + "': " +
                               ec.message());
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::unexpected("could not open '" + path.string() + "'");
    std::string bytes(
        static_cast<std::size_t>(std::min<std::uintmax_t>(size, limit)), '\0');
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got != bytes.size())
        return std::unexpected("could not read '" + path.string() + "' whole");
    return bytes;
}

// THE IMAGE'S REGION `r` (client rows) FOR THE CLIENT w x h: under the
// COVER RULE (the ground), or, `strip`, the STRIP RULE (the lane: the image
// scaled to the window's width, its center rows the lane's) — one decode.
std::expected<Surface, std::string> image_region(
    const std::filesystem::path& path, int w, int h, const GuiRect& r,
    bool strip) {
    const auto bytes = file_bytes(path);
    if (!bytes) return std::unexpected("tiles: " + bytes.error());
    const std::string label = path.string();
    const auto info = svg_icon::raster_info(*bytes);
    if (!info) return std::unexpected("tiles: " + label + ": " + info.error());
    double k = 1.0;
    int ox = 0, oy = 0;
    if (strip) {
        k = static_cast<double>(w) / info->w;
        const int sw = std::max(w, static_cast<int>(std::nearbyint(info->w * k)));
        const int sh = static_cast<int>(std::nearbyint(info->h * k));
        if (sh < r.h)
            return std::unexpected("tiles: " + label + ": too short for the " +
                                   std::to_string(r.h) + "-row lane at " +
                                   std::to_string(w) + " wide");
        ox = (sw - w) / 2;
        oy = (sh - r.h) / 2 - r.y;
    } else {
        k = std::max(static_cast<double>(w) / info->w,
                     static_cast<double>(h) / info->h);
        const int sw = std::max(w, static_cast<int>(std::nearbyint(info->w * k)));
        const int sh = std::max(h, static_cast<int>(std::nearbyint(info->h * k)));
        ox = (sw - w) / 2;
        oy = (sh - h) / 2;
    }
    const svg_icon::RasterRegion region{r.x + ox, r.y + oy, r.w, r.h};
    auto out = svg_icon::raster_regions(*bytes, *info, k, std::span(&region, 1));
    if (!out) return std::unexpected("tiles: " + label + ": " + out.error());
    if (out->empty() || !(*out)[0])
        return std::unexpected("tiles: " + label + ": no pixels");
    return Surface((*out)[0]);
}

// ONE GROUND'S BAKE, built on its own thread: the tile in sRGB, its mean,
// then the window's bytes. Nothing here touches State.
struct Baked {
    std::string error;   // empty: success
    GuiRect     rect{};
    Surface     surface;
    uint32_t    face = 0;
    double      ms = 0.0;
    bool        image = false;   // the image road's, not the procedural
};

void bake_ground(const Geometry& g, Baked& out) {
    const int y0 = std::clamp(g.band_y, 0, g.h);
    out.rect = GuiRect{0, y0, g.w, g.h - y0};
    if (out.rect.w <= 0 || out.rect.h <= 0) return;
    if (const std::filesystem::path p = image_of(GuiMaterial::Ground); !p.empty()) {
        auto s = image_region(p, g.w, g.h, out.rect, false);
        if (!s) { out.error = s.error(); return; }
        out.surface = std::move(*s);
        out.image = true;
    } else {
        out.surface = brushed(kGroundBase, kGroundSeed, kGroundStrength,
                              kGroundVGrad, g.w, g.h, y0, out.rect.h);
    }
    out.face = mean_of(out.surface.get());
}

void bake_marker(const Geometry& g, Baked& out) {
    const int y0 = std::clamp(g.lane_y, 0, g.h);
    const int y1 = std::clamp(g.lane_y + g.lane_h, 0, g.h);
    out.rect = GuiRect{0, y0, g.w, y1 - y0};
    if (out.rect.w <= 0 || out.rect.h <= 0) return;
    Surface one;
    if (const std::filesystem::path p = image_of(GuiMaterial::Marker); !p.empty()) {
        auto s = image_region(p, g.w, g.h, out.rect, true);
        if (!s) { out.error = s.error(); return; }
        one = std::move(*s);
        out.image = true;
    } else {
        one = brushed(kMarkerBase, kMarkerSeed, kGroundStrength, 0.0, g.w,
                      g.h, y0, out.rect.h);
    }
    make_seamless(one.get());
    out.face = mean_of(one.get());
    out.surface = doubled(one.get());
}

void bake_one(GuiMaterial m, const Geometry& g, Baked& out) {
    const auto t0 = std::chrono::steady_clock::now();
    if (m == GuiMaterial::Ground) bake_ground(g, out);
    else                          bake_marker(g, out);
    if (out.error.empty() && out.surface) convert(out.surface.get());
    out.ms = std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - t0)
                 .count();
}

// Whether the rect [x0, x1) x [y0, y1) lies whole inside `outer`.
bool contains(const GuiRect& outer, double x0, double y0, double x1, double y1) {
    return x0 >= outer.x && y0 >= outer.y && x1 <= outer.x + outer.w &&
           y1 <= outer.y + outer.h;
}

// THE PIXELS THE PAINTERS PAINT, in client coordinates.
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

// THE LANE'S TILE AT THE PHASE (materials.h's LOOP): client x shows the
// strip's column (x − rect.x − p) mod W, p the phase's whole px in [0, W) and
// W the tile's rect width, read out of the doubled surface at column x −
// rect.x − p + W — one 1:1 nearest copy, EXTEND_NONE.
void set_marker_tile_source(cairo_t* cr, const Tile& t, double phase) {
    const int64_t w = t.rect.w;
    int64_t p = w > 0 ? static_cast<int64_t>(std::llrint(phase)) % w : 0;
    if (p < 0) p += w;
    cairo_matrix_t at;
    cairo_matrix_init_translate(
        &at, -static_cast<double>(t.rect.x) - static_cast<double>(p) +
                 static_cast<double>(w),
        -static_cast<double>(t.rect.y));
    cairo_pattern_set_matrix(t.pattern.get(), &at);
    cairo_set_source(cr, t.pattern.get());
}

// THE TILE OF `tiles` holding the current path's extents WHERE THEY MEET
// THE CLIENT, or null where they miss it (the paint context is clipped to
// the client, so only the part inside it is ever filled, and that part is
// what a tile must hold; a stale tile's pixel never lands outside the
// client).
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

const std::vector<Tile>& tiles_of(GuiMaterial m) {
    State& s = state();
    return m == GuiMaterial::Ground ? s.ground : s.marker;
}

std::size_t bytes_of_surface(cairo_surface_t* s) {
    return s ? static_cast<std::size_t>(cairo_image_surface_get_stride(s)) *
                   static_cast<std::size_t>(cairo_image_surface_get_height(s))
             : 0;
}

// THE LIT KEYS' PATCHES (materials.h's LIT KEYS and set_led_glow): for
// every size the key painter's own lit key at the origin — DOWN, a lit key
// being a key down (architect 2026-10-23; cool_edit_paint.h's paint_key):
// the wall's one-device-px band inside the outline's top row and left
// column, the LED one device px right and down — then THE GLOW composited
// over the face inside the wall and the outline, round THE MOVED LED's rect
// (key_led_rect with `down`) — the accent at alpha .35 at the rect's edge
// falling off as (1 − d / glow)² to 0 at `glow` px out, d the pixel's
// distance from the rect — the LED's own pixels left as painted. In the
// window's bytes: the painter converts its own colors (set_palette_source),
// the glow's accent converted here.
void rebuild_lit_patches() {
    State& s = state();
    s.lit.clear();
    const GuiColor accent = display_color(hex(s.accent));
    const double ar = accent.r * 255.0, ag = accent.g * 255.0, ab = accent.b * 255.0;
    constexpr double kGlowPeak = 0.35;
    // The face's first row and column: past the outline and the wall.
    const int face0 = 2 * hairline_px();
    for (const auto& [w, h] : s.lit_sizes) {
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t* cr = cairo_create(surf);
        const GuiRect r{0, 0, w, h};
        paint_key_plain(cr, r, /*down=*/true, GuiKeyLed::Lit);
        cairo_destroy(cr);
        const GuiRect led = key_led_rect(r, /*down=*/true);
        if (g_led_glow > 0 && led.w > 0 && led.h > 0) {
            cairo_surface_flush(surf);
            unsigned char* data = cairo_image_surface_get_data(surf);
            const int stride = cairo_image_surface_get_stride(surf);
            const double glow = static_cast<double>(g_led_glow);
            for (int y = face0; y < h - 1; ++y) {
                auto* row = reinterpret_cast<uint32_t*>(data + static_cast<std::size_t>(y) * stride);
                for (int x = face0; x < w - 1; ++x) {
                    // The pixel's centre's distance from the rect.
                    const double px = x + 0.5, py = y + 0.5;
                    const double dx = std::max({static_cast<double>(led.x) - px, 0.0,
                                                px - static_cast<double>(led.x + led.w)});
                    const double dy = std::max({static_cast<double>(led.y) - py, 0.0,
                                                py - static_cast<double>(led.y + led.h)});
                    const double d = std::sqrt(dx * dx + dy * dy);
                    if (d <= 0.0 || d >= glow) continue;
                    const double t = 1.0 - d / glow;
                    const double a = kGlowPeak * t * t;
                    const uint32_t p = row[x];
                    const auto mix = [&](int c, double target) {
                        return static_cast<uint32_t>(std::clamp(
                            std::nearbyint((1.0 - a) * channel(p, c) + a * target),
                            0.0, 255.0));
                    };
                    row[x] = 0xFF000000u | (mix(0, ar) << 16) | (mix(1, ag) << 8) |
                             mix(2, ab);
                }
            }
            cairo_surface_mark_dirty(surf);
        }
        LitPatch lp;
        lp.w = w;
        lp.h = h;
        lp.tile = make_tile(GuiRect{0, 0, w, h}, Surface(surf));
        s.lit.push_back(std::move(lp));
    }
}

// THE BAKE ITSELF (materials.h's BAKE RULE) for geometry `g`: the two
// grounds on two threads, then the tiles into State on this one, and the
// lit patches.
std::optional<std::string> bake(const Geometry& g) {
    const auto t0 = std::chrono::steady_clock::now();
    // The transform's tables built here, before the threads read them.
    if (display_transform::active()) (void)display_transform::p3_word_from_srgb_fast(0);
    std::array<Baked, kGuiMaterialCount> baked;
    {
        std::array<std::jthread, kGuiMaterialCount> threads;
        for (std::size_t m = 0; m < kGuiMaterialCount; ++m)
            threads[m] = std::jthread([&, m] {
                bake_one(static_cast<GuiMaterial>(m), g, baked[m]);
            });
    }   // joined
    for (const Baked& b : baked)
        if (!b.error.empty()) return b.error;

    State& s = state();
    s.ground.clear();
    s.marker.clear();
    for (std::size_t m = 0; m < kGuiMaterialCount; ++m) {
        Baked& b = baked[m];
        if (!b.surface) continue;
        (m == 0 ? s.ground : s.marker).push_back(make_tile(b.rect, std::move(b.surface)));
        s.faces[m] = b.face;
    }
    s.baked = true;
    s.lit_sizes = g.key_sizes;
    rebuild_lit_patches();
    // THE TRACE (materials::trace_enabled; paint_handler.cpp's on_redraw the
    // other line): each ground's thread time and source, the bake's wall
    // time and the resident bytes.
    if (materials::trace_enabled())
        std::fprintf(stderr,
                     "warptempo_gui: materials baked %dx%d: ground %.1f ms (%s), "
                     "marker %.1f ms (%s), wall %.1f ms, resident %zu bytes\n",
                     g.w, g.h, baked[0].ms, baked[0].image ? "image" : "brushed",
                     baked[1].ms, baked[1].image ? "image" : "brushed",
                     std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - t0)
                         .count(),
                     materials::resident_bytes());
    return std::nullopt;
}

// A BAKE NOW for the app's geometry, `k` its key: the hard fail on a
// failure (an image of the road's that will not decode), and the palette
// installed again off the new mean — always, since the tiles every cached
// surface was painted from have moved even where the mean has not, and the
// install's generation is what rebuilds those surfaces (render.h's
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
    // EACH ROLE'S FILE'S HEADER, read off its first bytes alone (a JPEG's
    // frame header can follow an ICC or EXIF segment, hence the 64 KiB): a
    // PNG or a JPEG. Both extensions of a role are checked where both stand.
    const std::filesystem::path config = device_config_path();
    if (config.empty()) return std::nullopt;
    const std::filesystem::path folder = config.parent_path() / "tiles";
    constexpr std::size_t kHeaderBytes = 64 * 1024;
    for (const char* stem : kGuiMaterialNames)
        for (const char* ext : {".png", ".jpg"}) {
            const std::filesystem::path p = folder / (std::string(stem) + ext);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(p, ec)) continue;
            const auto head = file_bytes(p, kHeaderBytes);
            if (!head) return "tiles: " + head.error();
            const auto info = svg_icon::raster_info(*head);
            if (!info) return "tiles: " + p.string() + ": " + info.error();
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

uint32_t ring_light() { return ring_light_of(face(GuiMaterial::Ground)); }
uint32_t ring_dark() { return ring_dark_of(face(GuiMaterial::Ground)); }

void set_accent(uint32_t accent) {
    State& s = state();
    if (s.accent == accent) return;
    s.accent = accent;
    if (s.baked) rebuild_lit_patches();
}
uint32_t accent_color() { return state().accent; }

void set_led_glow(int px) { g_led_glow = std::max(0, px); }

void follow_displayed_basis(int64_t vp_start, int64_t vp_end, int area_w) {
    State& s = state();
    if (area_w <= 0 || vp_end <= vp_start) return;
    const double spp =
        static_cast<double>(vp_end - vp_start) / static_cast<double>(area_w);
    // AT CONSTANT spp THE CONTENT'S TRAVEL; ACROSS A CHANGE OF spp NOTHING.
    if (s.phase_seen && spp == s.phase_spp)
        s.marker_phase += static_cast<double>(s.phase_vp - vp_start) / spp;
    s.phase_seen = true;
    s.phase_vp   = vp_start;
    s.phase_spp  = spp;
    // Kept inside one period of the live tile, so the double never grows.
    if (!s.marker.empty() && s.marker.front().rect.w > 0) {
        const double w = static_cast<double>(s.marker.front().rect.w);
        s.marker_phase = std::fmod(s.marker_phase, w);
        if (s.marker_phase < 0.0) s.marker_phase += w;
    }
}

bool set_source(cairo_t* cr, GuiColor c) {
    State& s = state();
    if (!s.baked || s.region == GuiMaterialRegion::Flat) return false;
    const GuiMaterial m = s.region == GuiMaterialRegion::Ground
                              ? GuiMaterial::Ground
                              : GuiMaterial::Marker;
    // THE FACE ALONE IS A GROUND (the head's TONE RULE): the palette's
    // `face`, the ground's mean.
    if (word_of(c) != (word_of(palette().face))) return false;
    if (const Tile* t = tile_under_path(cr, tiles_of(m))) {
        // THE LANE AT ITS PHASE, every reader of its tile together — the
        // lane's ground and the cue labels' face boxes (the LOOP).
        if (m == GuiMaterial::Marker) set_marker_tile_source(cr, *t, s.marker_phase);
        else                          set_tile_source(cr, *t);
        return true;
    }
    set_palette_source(cr, hex(s.faces[static_cast<std::size_t>(m)]));
    return true;
}

void paint_rect(cairo_t* cr, const GuiRect& r, GuiMaterial m) {
    if (r.w <= 0 || r.h <= 0) return;
    State& s = state();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // The flat mean under the whole rect, then each tile's part over it,
    // inside the client (the gate's guard).
    set_palette_source(cr, hex(s.faces[static_cast<std::size_t>(m)]));
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
            if (m == GuiMaterial::Marker) set_marker_tile_source(cr, t, s.marker_phase);
            else                          set_tile_source(cr, t);
            cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);
}

bool paint_lit_key(cairo_t* cr, const GuiRect& r) {
    const State& s = state();
    if (!s.baked) return false;
    for (const LitPatch& p : s.lit) {
        if (p.w != r.w || p.h != r.h) continue;
        cairo_save(cr);
        cairo_matrix_t at;
        cairo_matrix_init_translate(&at, -static_cast<double>(r.x),
                                    -static_cast<double>(r.y));
        cairo_pattern_set_matrix(p.tile.pattern.get(), &at);
        cairo_set_source(cr, p.tile.pattern.get());
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_fill(cr);
        cairo_restore(cr);
        return true;
    }
    return false;
}

DisabledGlyphTones disabled_glyph_tones() {
    return DisabledGlyphTones{kCeDisabledShadow, kCeDisabledLight, kCeDisabledInset};
}

std::size_t resident_bytes() {
    const State& s = state();
    std::size_t n = 0;
    for (const auto* set : {&s.ground, &s.marker})
        for (const Tile& t : *set) n += bytes_of_surface(t.surface.get());
    for (const LitPatch& p : s.lit) n += bytes_of_surface(p.tile.surface.get());
    return n;
}

} // namespace materials

GuiMaterialScope::GuiMaterialScope(GuiMaterialRegion region)
    : previous_(state().region) {
    state().region = region;
}

GuiMaterialScope::~GuiMaterialScope() { state().region = previous_; }
