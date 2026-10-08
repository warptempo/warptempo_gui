#include "svg_icon.h"

#include "display_transform.h"

#include <resvg.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace svg_icon {

std::expected<Document, std::string> parse(std::string_view bytes) {
    // The options are resvg's defaults (96 dpi, no resources directory: a
    // bundled drawing references no file; no font database, the build has no
    // text), made per parse and freed with it: nothing global outlives a
    // load.
    resvg_options* options = resvg_options_create();
    resvg_render_tree* tree = nullptr;
    const int32_t code = resvg_parse_tree_from_data(
        bytes.data(), bytes.size(), options, &tree);
    resvg_options_destroy(options);
    // ONLY RESVG_OK IS COMPARED. resvg 0.48.1's hand-written resvg.h is out
    // of step with the library's own enum (lib.rs has SVGZ_UNSUPPORTED at 2,
    // the header lacks it), so every header name from 2 up is one below the
    // code the library returns (the header's RESVG_ERROR_PARSING_FAILED = 6
    // is really INVALID_SIZE; a real parse failure returns 7, which the
    // header does not name). The line prints the library's number.
    if (code != RESVG_OK)
        return std::unexpected("not well-formed SVG (resvg error " +
                               std::to_string(code) + ")");
    return Document{std::shared_ptr<resvg_render_tree>(tree,
                                                       resvg_tree_destroy)};
}

cairo_surface_t* rasterise(const Document& doc, int px) {
    // A new image surface is cleared to transparent, which resvg's render
    // composites onto. Its stride is exactly 4 * px: cairo aligns ARGB32
    // rows to 4 bytes, and resvg's pixmap is packed rows of 4 bytes.
    cairo_surface_t* s =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
    assert(cairo_image_surface_get_stride(s) == 4 * px);
    cairo_surface_flush(s);
    unsigned char* data = cairo_image_surface_get_data(s);

    const resvg_size size = resvg_get_image_size(doc.tree.get());
    const double k = std::min(px / static_cast<double>(size.width),
                              px / static_cast<double>(size.height));
    const resvg_transform fit{
        static_cast<float>(k), 0.0f, 0.0f, static_cast<float>(k),
        static_cast<float>((px - size.width * k) / 2.0),
        static_cast<float>((px - size.height * k) / 2.0)};
    resvg_render(doc.tree.get(), fit, static_cast<uint32_t>(px),
                 static_cast<uint32_t>(px), reinterpret_cast<char*>(data));

    // resvg writes RGBA8888 premultiplied, byte by byte; cairo's ARGB32 is
    // one native-endian 32-bit word per pixel, premultiplied too. Same
    // values, another order: each pixel is reread as bytes and rewritten as
    // the word.
    const int n = px * px;
    for (int i = 0; i < n; ++i) {
        unsigned char* p = data + 4 * i;
        const uint32_t word = (static_cast<uint32_t>(p[3]) << 24) |
                              (static_cast<uint32_t>(p[0]) << 16) |
                              (static_cast<uint32_t>(p[1]) << 8) |
                              static_cast<uint32_t>(p[2]);
        std::memcpy(p, &word, sizeof word);
    }
    cairo_surface_mark_dirty(s);
    return s;
}

cairo_surface_t* saturated_copy(cairo_surface_t* live) {
    cairo_surface_flush(live);
    const int w = cairo_image_surface_get_width(live);
    const int h = cairo_image_surface_get_height(live);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(out);
    const unsigned char* src = cairo_image_surface_get_data(live);
    unsigned char* dst = cairo_image_surface_get_data(out);
    const int src_stride = cairo_image_surface_get_stride(live);
    const int dst_stride = cairo_image_surface_get_stride(out);
    constexpr double kFrame = 192.0 / 255.0;
    for (int y = 0; y < h; ++y) {
        const auto* srow = reinterpret_cast<const uint32_t*>(src + y * src_stride);
        auto* drow = reinterpret_cast<uint32_t*>(dst + y * dst_stride);
        for (int x = 0; x < w; ++x) {
            const uint32_t p = srow[x];
            const double a = static_cast<double>((p >> 24) & 0xFF);
            const double r = static_cast<double>((p >> 16) & 0xFF);
            const double g = static_cast<double>((p >> 8) & 0xFF);
            const double b = static_cast<double>(p & 0xFF);
            const double lum = 0.30 * r + 0.59 * g + 0.11 * b;
            const auto l = static_cast<uint32_t>(std::nearbyint(lum * kFrame));
            const auto a2 = static_cast<uint32_t>(std::nearbyint(a * kFrame));
            drow[x] = (a2 << 24) | (l << 16) | (l << 8) | l;
        }
    }
    cairo_surface_mark_dirty(out);
    return out;
}

cairo_surface_t* stippled_copy(cairo_surface_t* live) {
    cairo_surface_flush(live);
    const int w = cairo_image_surface_get_width(live);
    const int h = cairo_image_surface_get_height(live);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(out);
    const unsigned char* src = cairo_image_surface_get_data(live);
    unsigned char* dst = cairo_image_surface_get_data(out);
    const int src_stride = cairo_image_surface_get_stride(live);
    const int dst_stride = cairo_image_surface_get_stride(out);
    for (int y = 0; y < h; ++y) {
        const auto* srow = reinterpret_cast<const uint32_t*>(src + y * src_stride);
        auto* drow = reinterpret_cast<uint32_t*>(dst + y * dst_stride);
        for (int x = 0; x < w; ++x)
            drow[x] = (x + y) % 2 == 0 ? srow[x] : 0u;
    }
    cairo_surface_mark_dirty(out);
    return out;
}

cairo_surface_t* saturated_pixelated_copy(cairo_surface_t* live) {
    cairo_surface_flush(live);
    const int w = cairo_image_surface_get_width(live);
    const int h = cairo_image_surface_get_height(live);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(out);
    const unsigned char* src = cairo_image_surface_get_data(live);
    unsigned char* dst = cairo_image_surface_get_data(out);
    const int src_stride = cairo_image_surface_get_stride(live);
    const int dst_stride = cairo_image_surface_get_stride(out);
    constexpr double kSaturation = 0.8;
    constexpr double kDarkFactor = 0.7;
    for (int y = 0; y < h; ++y) {
        const auto* srow = reinterpret_cast<const uint32_t*>(src + y * src_stride);
        auto* drow = reinterpret_cast<uint32_t*>(dst + y * dst_stride);
        for (int x = 0; x < w; ++x) {
            const uint32_t p = srow[x];
            const uint32_t a = (p >> 24) & 0xFF;
            if (a == 0) { drow[x] = 0; continue; }
            // the pixbuf's unpremultiplied bytes
            double c[3];
            for (int k = 0; k < 3; ++k) {
                const double v = static_cast<double>((p >> (16 - 8 * k)) & 0xFF);
                c[k] = std::min(255.0, std::nearbyint(v * 255.0 / a));
            }
            const double i = c[0] * 0.30 + c[1] * 0.59 + c[2] * 0.11;
            uint32_t word = a << 24;
            for (int k = 0; k < 3; ++k) {
                const double v =
                    (x + y) % 2 == 0
                        ? i / 2 + 127
                        : std::clamp(((1.0 - kSaturation) * i +
                                      kSaturation * c[k]) * kDarkFactor,
                                     0.0, 255.0);
                const double level = std::floor(v);   // the uchar's truncation
                const auto pm = static_cast<uint32_t>(
                    std::nearbyint(level * a / 255.0));
                word |= pm << (16 - 8 * k);
            }
            drow[x] = word;
        }
    }
    cairo_surface_mark_dirty(out);
    return out;
}

void convert_to_display(cairo_surface_t* s) {
    if (!display_transform::active()) return;
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(data + y * stride);
        for (int x = 0; x < w; ++x)
            row[x] = display_transform::display_premultiplied(row[x]);
    }
    cairo_surface_mark_dirty(s);
}

} // namespace svg_icon
