#include "svg_icon.h"

#include "display_transform.h"

#include <resvg.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace svg_icon {
namespace {

// resvg writes RGBA8888 premultiplied, byte by byte; cairo's ARGB32 is one
// native-endian 32-bit word per pixel, premultiplied too. Same values,
// another order: each of the n pixels at `data` is reread as bytes and
// rewritten as the word, in place.
void rgba_bytes_to_argb_words(unsigned char* data, int n) {
    for (int i = 0; i < n; ++i) {
        unsigned char* p = data + 4 * i;
        const uint32_t word = (static_cast<uint32_t>(p[3]) << 24) |
                              (static_cast<uint32_t>(p[0]) << 16) |
                              (static_cast<uint32_t>(p[1]) << 8) |
                              static_cast<uint32_t>(p[2]);
        std::memcpy(p, &word, sizeof word);
    }
}

// Standard base64 (RFC 4648, padded), for a raster's data URL.
std::string base64(std::string_view in) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) |
                           (static_cast<uint8_t>(in[i + 1]) << 8) |
                           static_cast<uint8_t>(in[i + 2]);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (i < in.size()) {
        const bool two = i + 1 < in.size();
        const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) |
                           (two ? static_cast<uint8_t>(in[i + 1]) << 8 : 0);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += two ? kAlphabet[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

// A 4-byte big-endian integer at `at` (the PNG's own byte order).
uint32_t be32(std::string_view b, size_t at) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(b[at])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b[at + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b[at + 2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(b[at + 3]));
}

// A 2-byte big-endian integer at `at` (a JPEG segment's own byte order).
uint32_t be16(std::string_view b, size_t at) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(b[at])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(b[at + 1]));
}

// THE ONE-<image> WRAPPER, THE DECODER'S DETOUR WITH A REASON: the tablet's
// cairo is built without its PNG reader on purpose (deps/50_cairo.sh,
// -Dpng=disabled), resvg is built with its raster-images feature on both
// devices from the one pin (85_resvg.sh, CMakeLists.txt) and so decodes PNG
// and JPEG, and no new library was wanted for it (stb_image and lodepng not
// taken). The file's bytes as a base64 data URL of its own mime in an
// <image> of its own width x height at the top-left of a frame_w x frame_h
// root, nearest: optimizeSpeed is nearest, and rendered at the identity
// every sample lands on a pixel centre, so the render IS the decoded image,
// premultiplied RGBA.
std::string image_wrapper(std::string_view bytes, const char* mime,
                          uint32_t w, uint32_t h, uint32_t frame_w,
                          uint32_t frame_h) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" "
           "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"" +
           std::to_string(frame_w) + "\" height=\"" + std::to_string(frame_h) +
           "\"><image width=\"" + std::to_string(w) + "\" height=\"" +
           std::to_string(h) +
           "\" image-rendering=\"optimizeSpeed\" xlink:href=\"data:" + mime +
           ";base64," + base64(bytes) + "\"/></svg>";
}

} // namespace

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
    return Document{
        std::shared_ptr<resvg_render_tree>(tree, resvg_tree_destroy)};
}

std::expected<RasterInfo, std::string> raster_info(std::string_view bytes) {
    static constexpr char kPng[] = "\x89PNG\r\n\x1a\n";
    if (bytes.size() >= 24 && bytes.substr(0, 8) == std::string_view(kPng, 8) &&
        bytes.substr(12, 4) == "IHDR")
        return RasterInfo{be32(bytes, 16), be32(bytes, 20), false};
    // A JPEG: FF D8, then marker segments up to the scan. The size is the
    // first START-OF-FRAME's (C0..CF but C4 DHT, C8 JPG and CC DAC): its
    // segment holds the precision byte, the height and the width.
    const auto byte = [&](size_t at) {
        return static_cast<uint8_t>(bytes[at]);
    };
    if (bytes.size() >= 4 && byte(0) == 0xFF && byte(1) == 0xD8) {
        size_t at = 2;
        while (at + 4 <= bytes.size()) {
            if (byte(at) != 0xFF) break;
            const uint8_t marker = byte(at + 1);
            if (marker == 0xFF) { ++at; continue; }   // a fill byte
            if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
                at += 2;                               // no length
                continue;
            }
            if (marker == 0xDA || marker == 0xD9) break;   // the scan, the end
            const size_t len = be16(bytes, at + 2);
            const bool sof = marker >= 0xC0 && marker <= 0xCF &&
                             marker != 0xC4 && marker != 0xC8 &&
                             marker != 0xCC;
            if (sof && len >= 7 && at + 9 <= bytes.size())
                return RasterInfo{be16(bytes, at + 7), be16(bytes, at + 5),
                                  true};
            at += 2 + len;
        }
    }
    return std::unexpected(std::string("not a PNG or JPEG"));
}

std::expected<std::vector<cairo_surface_t*>, std::string> raster_regions(
    std::string_view bytes, const RasterInfo& info,
    std::span<const RasterRegion> regions) {
    assert(info.w > 0 && info.h > 0);
    const int iw = static_cast<int>(info.w);
    const int ih = static_cast<int>(info.h);
    const char* mime = info.jpeg ? "image/jpeg" : "image/png";
    std::vector<cairo_surface_t*> out;
    if (regions.empty()) return out;

    // THE REGIONS ARE THE DECODED PIXELS THEMSELVES: the regions' union span
    // decoded once (image_wrapper at the identity translated by the span's
    // corner, every sample on a pixel center) and each region's rows COPIED
    // out of the pixmap — resvg's premultiplied RGBA bytes swizzled to
    // cairo's ARGB32 word, the alpha forced opaque (a raster's alpha is not
    // read). Every region lies inside the image (the caller's crop).
    int ux0 = iw, uy0 = ih, ux1 = 0, uy1 = 0;
    for (const RasterRegion& r : regions) {
        assert(r.w > 0 && r.h > 0 && r.x0 >= 0 && r.y0 >= 0 &&
               r.x0 + r.w <= iw && r.y0 + r.h <= ih);
        ux0 = std::min(ux0, r.x0);
        uy0 = std::min(uy0, r.y0);
        ux1 = std::max(ux1, r.x0 + r.w);
        uy1 = std::max(uy1, r.y0 + r.h);
    }
    const int uw = ux1 - ux0;
    const int uh = uy1 - uy0;
    auto wrapper =
        parse(image_wrapper(bytes, mime, info.w, info.h, info.w, info.h));
    if (!wrapper) return std::unexpected(wrapper.error());
    std::vector<unsigned char> src(static_cast<size_t>(uw) * uh * 4, 0);
    const resvg_transform at{1.0f, 0.0f, 0.0f, 1.0f,
                             -static_cast<float>(ux0),
                             -static_cast<float>(uy0)};
    resvg_render(wrapper->tree.get(), at, static_cast<uint32_t>(uw),
                 static_cast<uint32_t>(uh),
                 reinterpret_cast<char*>(src.data()));
    wrapper->tree.reset();
    for (const RasterRegion& r : regions) {
        cairo_surface_t* s =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.w, r.h);
        cairo_surface_flush(s);
        unsigned char* ddata = cairo_image_surface_get_data(s);
        const int dstride = cairo_image_surface_get_stride(s);
        for (int y = 0; y < r.h; ++y) {
            const unsigned char* p =
                src.data() +
                (static_cast<size_t>(r.y0 - uy0 + y) * uw + (r.x0 - ux0)) * 4;
            auto* drow = reinterpret_cast<uint32_t*>(ddata + y * dstride);
            for (int x = 0; x < r.w; ++x, p += 4)
                drow[x] = 0xFF000000u | (static_cast<uint32_t>(p[0]) << 16) |
                          (static_cast<uint32_t>(p[1]) << 8) |
                          static_cast<uint32_t>(p[2]);
        }
        cairo_surface_mark_dirty(s);
        out.push_back(s);
    }
    return out;
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

    rgba_bytes_to_argb_words(data, px * px);
    cairo_surface_mark_dirty(s);
    return s;
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
