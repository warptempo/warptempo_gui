#include "materials.h"

#include "app_state.h"          // the lane accessors the bake's geometry reads
#include "cool_edit_paint.h"    // the key painter the key patches are baked from
#include "display_transform.h"
#include "paint_handler.h"      // program_key_seats
#include "palette_file.h"       // program_palette_words' type

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/system_properties.h>

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
// key's height (the `key_height` tuning key since 2026-10-29, render.h's
// seat: it moves every lane and every key patch as the scale does), the
// on-screen keyboard's key box (the `osk_key_w` and `osk_key_h` tuning keys
// since 2026-10-29, the same seat's neighbor: it moves the keyboard's
// patches and the controls row's seat on its band) and the
// conversion (the strip pad a constant since 2026-10-23, the LED glow
// since 2026-10-26) — every lane, every seat and so every tile's rect and
// every key patch is a function of them, so a frame compares these and
// never lays anything out.
struct Key {
    int  w = 0, h = 0, scale = 0, key_h = 0, osk_w = 0, osk_h = 0;
    bool converted = false;
    bool operator==(const Key&) const = default;
};

// THE LED GLOW (materials.h's KEYS): the halo's reach past the lit LED's
// rect, 4 DEVICE PX at every scale (architect 2026-10-23, "agree to all",
// tuned on the glass as a device key; settled 2026-10-26 at
// its default, "it stays on"). Unscaled: the key's own unit was device px.
constexpr int kLedGlowPx = 4;

Key key_of(const AppState& app) {
    return Key{app.width, app.height, gui_scale_percent(),
               key_height_at300(), osk_key_w_at300(), osk_key_h_at300(),
               display_transform::active()};
}

// THE WINDOW'S GEOMETRY A BAKE IS FOR — every tile's rect, laid out at the
// bake alone.
struct Geometry {
    int w = 0, h = 0;
    // The band's top row: the ground tile's first.
    int band_y = 0;
    // THE KEYS the patches are baked for (program_key_seats).
    std::vector<GuiKeySeat> key_seats;
};

Geometry geometry_of(const AppState& app) {
    Geometry g;
    g.w = app.width;
    g.h = app.height;
    g.band_y = top_icon_row_area(app).y;
    g.key_seats = program_key_seats(app);
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

// THE KEY PATCHES' MAP KEY (materials.h's KEYS): the size, the depth and
// the lamp, packed — the size's two 14-bit fields hold any window's key.
uint64_t patch_key(int w, int h, int depth, bool lamp) {
    return (static_cast<uint64_t>(w & 0x3FFF) << 18) |
           (static_cast<uint64_t>(h & 0x3FFF) << 4) |
           (static_cast<uint64_t>(depth & 0x3) << 1) | (lamp ? 1u : 0u);
}

struct State {
    bool     baked = false;
    Key      key;
    // THE GROUND'S FACE, its tile's mean (materials.h's TONE RULE).
    uint32_t face = kGroundMeanEstimate;
    // THE GROUND'S TILE (materials.h's BAKE RULE), one once baked.
    std::vector<Tile> ground;
    // THE KEY PATCHES (patch_key), and the key sizes they were baked for
    // (kept so an accent change rebuilds the lit ones without a layout).
    std::unordered_map<uint64_t, Tile> patches;
    std::vector<GuiKeySeat>            key_seats;
    uint32_t                           accent = kLedLitRecord;
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
    GuiMaterialRegion region = GuiMaterialRegion::Flat;
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

// -- THE PROCEDURAL BRUSHED GROUND (materials.h's head)

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

// THE BRUSHED FACE over rows [y0, y0 + rows) of a
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

// THE GROUND'S BAKE, built on its own thread: the tile in sRGB, its mean,
// then the window's bytes. Nothing here touches State.
struct Baked {
    GuiRect     rect{};
    Surface     surface;
    uint32_t    face = 0;
    double      ms = 0.0;
};

void bake_ground(const Geometry& g, Baked& out) {
    const auto t0 = std::chrono::steady_clock::now();
    const int y0 = std::clamp(g.band_y, 0, g.h);
    out.rect = GuiRect{0, y0, g.w, g.h - y0};
    if (out.rect.w > 0 && out.rect.h > 0) {
        out.surface = brushed(kGroundBase, kGroundSeed, kGroundStrength,
                              kGroundVGrad, g.w, g.h, y0, out.rect.h);
        out.face = mean_of(out.surface.get());
        if (out.surface) convert(out.surface.get());
    }
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

std::size_t bytes_of_surface(cairo_surface_t* s) {
    return s ? static_cast<std::size_t>(cairo_image_surface_get_stride(s)) *
                   static_cast<std::size_t>(cairo_image_surface_get_height(s))
             : 0;
}

// -- THE KEY PATCHES (materials.h's KEYS) ----------------------------------

// THE GLOW (kLedGlowPx above) over the patch `surf` of the key
// `r` at `depth`: composited over the face inside the wall and the outline,
// round THAT DEPTH'S LED rect (key_led_rect) — the accent at alpha .35 at
// the rect's edge falling off as (1 − d / glow)² to 0 at `glow` px out, d
// the pixel's distance from the rect — the LED's own pixels left as
// painted. The accent converted to the window's bytes here.
void composite_glow(cairo_surface_t* surf, const GuiRect& r, int depth) {
    const GuiRect led = key_led_rect(r, depth);
    if (led.w <= 0 || led.h <= 0) return;
    const GuiColor accent = display_color(hex(state().accent));
    const double ar = accent.r * 255.0, ag = accent.g * 255.0, ab = accent.b * 255.0;
    constexpr double kGlowPeak = 0.35;
    // The face's first row and column: past the outline and the wall.
    const int face0 = (1 + depth) * hairline_px();
    cairo_surface_flush(surf);
    unsigned char* data = cairo_image_surface_get_data(surf);
    const int stride = cairo_image_surface_get_stride(surf);
    constexpr double glow = kLedGlowPx;
    for (int y = face0; y < r.h - 1; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(data + static_cast<std::size_t>(y) * stride);
        for (int x = face0; x < r.w - 1; ++x) {
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

// THE KEY PATCHES (materials.h's KEYS): for every seat and each depth it
// stands at, the key painter's own key at the origin — a lamp key's LED dead
// at rest and lit down, a lampless key's none — then, lit, THE GLOW. In the
// window's bytes: the painter converts its own colors (set_palette_source),
// the glow converted here. `lit_only` (an accent change): the lamp keys'
// depths 1 and 2 alone, the other patches standing.
void rebuild_key_patches(bool lit_only) {
    State& s = state();
    if (!lit_only) s.patches.clear();
    for (const GuiKeySeat& seat : s.key_seats) {
        if (seat.w <= 0 || seat.h <= 0) continue;
        if (lit_only && !seat.lamp) continue;
        const GuiRect r{0, 0, seat.w, seat.h};
        for (int depth = 0; depth <= 2; ++depth) {
            if (!(seat.depths & (1u << depth))) continue;
            const bool lit = seat.lamp && depth > 0;
            if (lit_only && !lit) continue;
            const GuiKeyLed led = !seat.lamp ? GuiKeyLed::None
                                  : lit      ? GuiKeyLed::Lit
                                             : GuiKeyLed::Unlit;
            cairo_surface_t* surf =
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32, seat.w, seat.h);
            cairo_t* cr = cairo_create(surf);
            paint_key_plain(cr, r, depth, led);
            cairo_destroy(cr);
            if (lit) composite_glow(surf, r, depth);
            s.patches.insert_or_assign(
                patch_key(seat.w, seat.h, depth, seat.lamp),
                make_tile(r, Surface(surf)));
        }
    }
}

// THE BAKE ITSELF (materials.h's BAKE RULE) for geometry `g`: the ground
// on its own thread while this one bakes the key patches (which read no
// ground: the thread touches no State and the patches nothing the thread
// writes, so the bytes are the same however the two interleave), then the
// tile into State.
void bake(const Geometry& g) {
    const auto t0 = std::chrono::steady_clock::now();
    // The transform's tables built here, before the thread reads them.
    if (display_transform::active()) (void)display_transform::p3_word_from_srgb_fast(0);
    Baked baked;
    State& s = state();
    double keys_ms = 0.0;
    {
        const std::jthread ground([&] { bake_ground(g, baked); });
        const auto k0 = std::chrono::steady_clock::now();
        s.key_seats = g.key_seats;
        rebuild_key_patches(false);
        keys_ms = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - k0)
                      .count();
    }   // joined

    s.ground.clear();
    if (baked.surface) {
        s.ground.push_back(make_tile(baked.rect, std::move(baked.surface)));
        s.face = baked.face;
    }
    s.baked = true;
    // THE TRACE (materials::trace_enabled; paint_handler.cpp's on_redraw the
    // other line): the ground's thread time, the key patches' count and
    // time, the bake's wall time and the resident bytes.
    if (materials::trace_enabled())
        std::fprintf(stderr,
                     "warptempo_gui: materials baked %dx%d: ground %.1f ms, "
                     "keys %zu patches %.1f ms, wall %.1f ms, "
                     "resident %zu bytes\n",
                     g.w, g.h, baked.ms, s.patches.size(), keys_ms,
                     std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - t0)
                         .count(),
                     materials::resident_bytes());
}

// A BAKE NOW for the app's geometry, `k` its key, and the palette installed
// again off the new mean — always, since a cached surface painted from the
// old tile is stale even where the mean has not moved, and the install's
// generation is what rebuilds those surfaces (render.h's
// palette_generation; the flag cache's label boxes, in the face, among
// them) — and the landing flagged for the pre-paint hook's whole-client
// damage (take_bake_landed).
void bake_now(const AppState& app, const Key& k) {
    State& s = state();
    bake(geometry_of(app));
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
        char value[PROP_VALUE_MAX] = {};
        return __system_property_get("debug.warptempo.trace", value) > 0 &&
               std::strcmp(value, "1") == 0;
    }();
    return on;
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

uint32_t face() { return state().face; }

uint32_t ring_light() { return ring_light_of(face()); }
uint32_t ring_dark() { return ring_dark_of(face()); }

void set_accent(uint32_t accent) {
    State& s = state();
    if (s.accent == accent) return;
    s.accent = accent;
    if (s.baked) rebuild_key_patches(true);
}
uint32_t accent_color() { return state().accent; }

bool set_source(cairo_t* cr, GuiColor c) {
    State& s = state();
    if (!s.baked || s.region == GuiMaterialRegion::Flat) return false;
    // THE FACE ALONE IS THE GROUND (the head's TONE RULE): the palette's
    // `face`, the ground's mean.
    if (word_of(c) != (word_of(palette().face))) return false;
    if (const Tile* t = tile_under_path(cr, s.ground)) {
        set_tile_source(cr, *t);
        return true;
    }
    set_palette_source(cr, hex(s.face));
    return true;
}

void paint_rect(cairo_t* cr, const GuiRect& r) {
    if (r.w <= 0 || r.h <= 0) return;
    State& s = state();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // The flat mean under the whole rect, then each tile's part over it,
    // inside the client (the gate's guard).
    set_palette_source(cr, hex(s.face));
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
    if (s.baked) {
        const int cx1 = std::min(r.x + r.w, s.client_w);
        const int cy1 = std::min(r.y + r.h, s.client_h);
        for (const Tile& t : s.ground) {
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

bool paint_key_patch(cairo_t* cr, const GuiRect& r, int depth, bool lamp) {
    const State& s = state();
    if (!s.baked || r.w <= 0 || r.h <= 0) return false;
    const auto it = s.patches.find(patch_key(r.w, r.h, depth, lamp));
    if (it == s.patches.end()) return false;
    const Tile& t = it->second;
    cairo_save(cr);
    cairo_matrix_t at;
    cairo_matrix_init_translate(&at, -static_cast<double>(r.x),
                                -static_cast<double>(r.y));
    cairo_pattern_set_matrix(t.pattern.get(), &at);
    cairo_set_source(cr, t.pattern.get());
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
    cairo_restore(cr);
    return true;
}

std::size_t resident_bytes() {
    const State& s = state();
    std::size_t n = 0;
    for (const Tile& t : s.ground) n += bytes_of_surface(t.surface.get());
    for (const auto& [k, t] : s.patches) n += bytes_of_surface(t.surface.get());
    return n;
}

} // namespace materials

GuiMaterialScope::GuiMaterialScope(GuiMaterialRegion region)
    : previous_(state().region) {
    state().region = region;
}

GuiMaterialScope::~GuiMaterialScope() { state().region = previous_; }
