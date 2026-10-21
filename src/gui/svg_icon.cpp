#include "svg_icon.h"

#include "display_transform.h"

#include <resvg.h>

#include <algorithm>
#include <charconv>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
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

// Standard base64 (RFC 4648, padded), for a PNG's data URL.
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

// PIL's Lanczos-3 (its sinc_filter / lanczos_filter): sinc(x)·sinc(x / 3)
// on [-3, 3), 0 outside.
double lanczos3(double x) {
    if (!(x >= -3.0 && x < 3.0)) return 0.0;
    const auto sinc = [](double t) {
        if (t == 0.0) return 1.0;
        t *= std::numbers::pi;
        return std::sin(t) / t;
    };
    return sinc(x) * sinc(x / 3.0);
}

// One axis of the area filter: for each of `side` destination pixels, the
// first source index of its window and its weights, normalised to sum 1 over
// the whole window — source indices outside [0, n) are kept (their samples
// read as transparent), so an edge translated into the cell fades correctly.
struct AxisTaps {
    std::vector<int>    first;     // per destination pixel
    std::vector<int>    count;     // per destination pixel
    std::vector<double> weights;   // `count` each, destination after destination
    std::vector<size_t> offset;    // into `weights`
};

// k = destination px per source px; shift_src the drawing's translate in
// source px. Destination pixel i's centre maps to c = (i + 0.5) / k −
// shift_src; the kernel is stretched by the reduction (PIL's filterscale,
// max(1 / k, 1)), its support 3 · filterscale source px each side; the
// weight of source pixel s is lanczos3((s + 0.5 − c) / filterscale).
AxisTaps axis_taps(int side, double k, double shift_src) {
    AxisTaps t;
    const double scale = std::max(1.0 / k, 1.0);
    const double support = 3.0 * scale;
    t.first.resize(static_cast<size_t>(side));
    t.count.resize(static_cast<size_t>(side));
    t.offset.resize(static_cast<size_t>(side));
    for (int i = 0; i < side; ++i) {
        const double c = (i + 0.5) / k - shift_src;
        const int lo = static_cast<int>(std::floor(c - support - 0.5));
        const int hi = static_cast<int>(std::ceil(c + support - 0.5));
        const size_t at = t.weights.size();
        double sum = 0.0;
        for (int s = lo; s <= hi; ++s) {
            const double w = lanczos3((s + 0.5 - c) / scale);
            t.weights.push_back(w);
            sum += w;
        }
        for (size_t j = at; j < t.weights.size(); ++j) t.weights[j] /= sum;
        t.first[static_cast<size_t>(i)] = lo;
        t.count[static_cast<size_t>(i)] = hi - lo + 1;
        t.offset[static_cast<size_t>(i)] = at;
    }
    return t;
}

// A MATERIAL REGION'S AXIS (svg_icon.h's raster_regions): axis_taps for the
// destination pixels [first, first + side) of an image drawn at k, unshifted —
// pixel first + i's centre maps to c = (first + i + 0.5) / k — the source
// indices unclamped (the caller clamps them into the image: a material has no
// transparent margin to fade into, so past its border the filter reads the
// border's own pixels). Never called at k = 1 exactly: there the regions are
// copied out of the decode (raster_regions' first arm).
AxisTaps region_taps(int first, int side, double k) {
    AxisTaps t;
    t.first.resize(static_cast<size_t>(side));
    t.count.resize(static_cast<size_t>(side));
    t.offset.resize(static_cast<size_t>(side));
    const double scale = std::max(1.0 / k, 1.0);
    const double support = 3.0 * scale;
    for (int i = 0; i < side; ++i) {
        const size_t at = t.weights.size();
        t.offset[static_cast<size_t>(i)] = at;
        const double c = (first + i + 0.5) / k;
        const int lo = static_cast<int>(std::floor(c - support - 0.5));
        const int hi = static_cast<int>(std::ceil(c + support - 0.5));
        double sum = 0.0;
        for (int s = lo; s <= hi; ++s) {
            const double w = lanczos3((s + 0.5 - c) / scale);
            t.weights.push_back(w);
            sum += w;
        }
        for (size_t j = at; j < t.weights.size(); ++j) t.weights[j] /= sum;
        t.first[static_cast<size_t>(i)] = lo;
        t.count[static_cast<size_t>(i)] = hi - lo + 1;
    }
    return t;
}

// A PNG'S SCALE (svg_icon.h's rasterise): the decoded N x N
// premultiplied surface resampled into a new (px + margin_px)² ARGB32
// surface, the drawing filling the px cell. Two passes in double —
// horizontal into an N-row intermediate, then vertical — in a fixed
// summation order (byte-reproducible: -ffp-contract=off is on for every
// build); std::nearbyint at each channel at the end (the rounding doctrine:
// at the pixel, never at the scale), alpha clamped to [0, 255] and each color
// channel to [0, alpha], since a negative lobe can overshoot and the word must
// stay a valid premultiplied pixel.
cairo_surface_t* resample_raster(cairo_surface_t* src, int px, double shift_x,
                                 double shift_y, int margin_px) {
    const int n = cairo_image_surface_get_width(src);
    const int side = px + margin_px;
    const double k = px / static_cast<double>(n);
    const double unit_src = n / 48.0;   // one user unit in source px
    const AxisTaps tx = axis_taps(side, k, shift_x * unit_src);
    const AxisTaps ty = axis_taps(side, k, shift_y * unit_src);

    const unsigned char* sdata = cairo_image_surface_get_data(src);
    const int sstride = cairo_image_surface_get_stride(src);
    // The intermediate: N source rows x `side` destination columns, four
    // premultiplied channels (A, R, G, B) each.
    std::vector<double> mid(static_cast<size_t>(n) * side * 4, 0.0);
    for (int y = 0; y < n; ++y) {
        const auto* row =
            reinterpret_cast<const uint32_t*>(sdata + y * sstride);
        double* out = mid.data() + static_cast<size_t>(y) * side * 4;
        for (int i = 0; i < side; ++i) {
            const int lo = tx.first[static_cast<size_t>(i)];
            const int cnt = tx.count[static_cast<size_t>(i)];
            const double* w = tx.weights.data() + tx.offset[static_cast<size_t>(i)];
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            for (int j = 0; j < cnt; ++j) {
                const int s = lo + j;
                if (s < 0 || s >= n) continue;   // transparent outside
                const uint32_t v = row[s];
                acc[0] += w[j] * static_cast<double>((v >> 24) & 0xFF);
                acc[1] += w[j] * static_cast<double>((v >> 16) & 0xFF);
                acc[2] += w[j] * static_cast<double>((v >> 8) & 0xFF);
                acc[3] += w[j] * static_cast<double>(v & 0xFF);
            }
            for (int c = 0; c < 4; ++c) out[i * 4 + c] = acc[c];
        }
    }

    cairo_surface_t* s =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
    cairo_surface_flush(s);
    unsigned char* ddata = cairo_image_surface_get_data(s);
    const int dstride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < side; ++y) {
        const int lo = ty.first[static_cast<size_t>(y)];
        const int cnt = ty.count[static_cast<size_t>(y)];
        const double* w = ty.weights.data() + ty.offset[static_cast<size_t>(y)];
        auto* drow = reinterpret_cast<uint32_t*>(ddata + y * dstride);
        for (int x = 0; x < side; ++x) {
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            for (int j = 0; j < cnt; ++j) {
                const int r = lo + j;
                if (r < 0 || r >= n) continue;   // transparent outside
                const double* m =
                    mid.data() + (static_cast<size_t>(r) * side + x) * 4;
                for (int c = 0; c < 4; ++c) acc[c] += w[j] * m[c];
            }
            const double a = std::clamp(std::nearbyint(acc[0]), 0.0, 255.0);
            uint32_t word = static_cast<uint32_t>(a) << 24;
            for (int c = 1; c < 4; ++c) {
                const double v = std::clamp(std::nearbyint(acc[c]), 0.0, a);
                word |= static_cast<uint32_t>(v) << (24 - 8 * c);
            }
            drow[x] = word;
        }
    }
    cairo_surface_mark_dirty(s);
    return s;
}

// A 2-byte big-endian integer at `at` (a JPEG segment's own byte order).
uint32_t be16(std::string_view b, size_t at) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(b[at])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(b[at + 1]));
}

// THE ONE-<image> WRAPPER (parse_png's head says why the detour): the file's
// bytes as a base64 data URL of its own mime in an <image> of its own
// width x height at the top-left of a frame_w x frame_h root, nearest.
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

// THE DECLARED SHIFT (svg_icon.h's body_center_shift head): the root tag's
// `data-body-shift="dx dy"`, read by a plain scan of the bytes from the
// first `<svg` to the next `>`. Absent: nullopt. Present: two numbers, one
// space between, each rounded to the tenth as the launch's measure rounds
// (so "2.6" is the very float body_center_shift would return); anything
// else is false (the caller's "malformed" line).
bool declared_shift(std::string_view bytes,
                    std::optional<std::pair<float, float>>& out) {
    out.reset();
    const size_t tag = bytes.find("<svg");
    if (tag == std::string_view::npos) return true;
    const std::string_view head =
        bytes.substr(tag, bytes.find('>', tag) - tag);
    static constexpr std::string_view kAttr = "data-body-shift=\"";
    const size_t at = head.find(kAttr);
    if (at == std::string_view::npos) return true;
    const size_t from = at + kAttr.size();
    const size_t to = head.find('"', from);
    if (to == std::string_view::npos) return false;
    const std::string_view value = head.substr(from, to - from);
    const size_t gap = value.find(' ');
    if (gap == std::string_view::npos) return false;
    double v[2] = {0.0, 0.0};
    const std::string_view part[2] = {value.substr(0, gap),
                                      value.substr(gap + 1)};
    for (int k = 0; k < 2; ++k) {
        const char* end = part[k].data() + part[k].size();
        const auto r = std::from_chars(part[k].data(), end, v[k]);
        if (part[k].empty() || r.ec != std::errc{} || r.ptr != end ||
            !std::isfinite(v[k]))
            return false;
    }
    const auto tenth = [](double x) {
        return static_cast<float>(std::nearbyint(x * 10.0) / 10.0) + 0.0f;
    };
    out = std::pair{tenth(v[0]), tenth(v[1])};
    return true;
}

} // namespace

std::expected<Document, std::string> parse(std::string_view bytes) {
    std::optional<std::pair<float, float>> shift;
    if (!declared_shift(bytes, shift))
        return std::unexpected(std::string("malformed data-body-shift"));
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
                                                       resvg_tree_destroy),
                    nullptr, shift};
}

std::expected<Document, std::string> parse_png(std::string_view bytes) {
    // THE DECODER IS RESVG, A DETOUR WITH A REASON: the tablet's cairo is
    // built without its PNG reader on purpose (android/deps/50_cairo.sh,
    // -Dpng=disabled), resvg is built with its raster-images feature on both
    // devices from the one pin (85_resvg.sh, CMakeLists.txt) and so decodes
    // PNG, and no new library was wanted for it (stb_image and lodepng not
    // taken). The PNG's bytes are wrapped once in a minimal SVG — one <image>
    // of the file's own W x H as a base64 data URL, at the top-left of an
    // N x N frame, N the longer side — and rendered ONCE at N x N with the
    // identity transform: optimizeSpeed is nearest, and at identity every
    // sample lands on a pixel centre, so the render IS the decoded image,
    // premultiplied RGBA, transparent past a non-square image's short side.
    // resvg's own <image> resampling never scales a PNG (at a 34x reduction it
    // aliased like nearest neighbour): the scale is resample_raster's area
    // filter.
    static constexpr char kSignature[] = "\x89PNG\r\n\x1a\n";
    if (bytes.size() < 24 ||
        bytes.substr(0, 8) != std::string_view(kSignature, 8) ||
        bytes.substr(12, 4) != "IHDR")
        return std::unexpected(std::string("not a PNG"));
    const uint32_t w = be32(bytes, 16);
    const uint32_t h = be32(bytes, 20);
    const uint32_t frame = std::max(w, h);
    auto wrapper =
        parse(image_wrapper(bytes, "image/png", w, h, frame, frame));
    if (!wrapper) return std::unexpected(wrapper.error());

    const int n = static_cast<int>(frame);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
    assert(cairo_image_surface_get_stride(s) == 4 * n);
    cairo_surface_flush(s);
    unsigned char* data = cairo_image_surface_get_data(s);
    const resvg_transform identity{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    resvg_render(wrapper->tree.get(), identity, frame, frame,
                 reinterpret_cast<char*>(data));
    rgba_bytes_to_argb_words(data, n * n);
    cairo_surface_mark_dirty(s);
    return Document{nullptr,
                    std::shared_ptr<cairo_surface_t>(s, cairo_surface_destroy),
                    std::nullopt};
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
    std::string_view bytes, const RasterInfo& info, double k,
    std::span<const RasterRegion> regions) {
    assert(k > 0.0 && info.w > 0 && info.h > 0);
    const int iw = static_cast<int>(info.w);
    const int ih = static_cast<int>(info.h);
    const char* mime = info.jpeg ? "image/jpeg" : "image/png";
    std::vector<cairo_surface_t*> out;
    if (regions.empty()) return out;

    // AT k = 1 THE REGIONS ARE THE DECODED PIXELS THEMSELVES (a bundled
    // cover's bake, materials.h): the regions' union span decoded once and
    // each region's rows COPIED out of the pixmap — resvg's premultiplied
    // RGBA bytes swizzled to cairo's ARGB32 word, the alpha forced opaque (a
    // material's alpha is not read) — no taps, no doubles (2026-10-21). Every
    // region lies inside the image at k = 1 (the caller's crop).
    if (k == 1.0) {
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

    // EACH REGION'S TAPS and their clamped source span; the decode covers the
    // spans' union, rendered ONCE.
    struct Axis {
        AxisTaps x, y;
    };
    std::vector<Axis> axes;
    axes.reserve(regions.size());
    int sx0 = iw, sx1 = 0, sy0 = ih, sy1 = 0;
    const auto widen = [](const AxisTaps& t, int n, int& lo, int& hi) {
        for (size_t i = 0; i < t.first.size(); ++i) {
            lo = std::min(lo, std::clamp(t.first[i], 0, n - 1));
            hi = std::max(hi, std::clamp(t.first[i] + t.count[i] - 1, 0, n - 1) + 1);
        }
    };
    for (const RasterRegion& r : regions) {
        assert(r.w > 0 && r.h > 0);
        axes.push_back(Axis{region_taps(r.x0, r.w, k), region_taps(r.y0, r.h, k)});
        widen(axes.back().x, iw, sx0, sx1);
        widen(axes.back().y, ih, sy0, sy1);
    }
    const int sw = sx1 - sx0;
    const int sh = sy1 - sy0;

    // THE SOURCE SPAN, decoded: the wrapper at the image's own size, rendered
    // at the identity translated by the span's corner, so every sample lands
    // on a pixel centre (parse_png's reasoning). The decode is resvg's and
    // its buffer is freed before the resample's rows are kept.
    auto wrapper =
        parse(image_wrapper(bytes, mime, info.w, info.h, info.w, info.h));
    if (!wrapper) return std::unexpected(wrapper.error());
    std::vector<unsigned char> src(static_cast<size_t>(sw) * sh * 4, 0);
    const resvg_transform at{1.0f, 0.0f, 0.0f, 1.0f, -static_cast<float>(sx0),
                             -static_cast<float>(sy0)};
    resvg_render(wrapper->tree.get(), at, static_cast<uint32_t>(sw),
                 static_cast<uint32_t>(sh), reinterpret_cast<char*>(src.data()));
    wrapper->tree.reset();

    for (std::size_t ri = 0; ri < regions.size(); ++ri) {
        const RasterRegion& r = regions[ri];
        const AxisTaps& tx = axes[ri].x;
        const AxisTaps& ty = axes[ri].y;
        // EACH HORIZONTAL TAP'S BYTE OFFSET in a span row, its index clamped
        // into the image once (the weights' own order).
        std::vector<size_t> tap_at(tx.weights.size());
        for (int i = 0; i < r.w; ++i)
            for (int j = 0; j < tx.count[static_cast<size_t>(i)]; ++j)
                tap_at[tx.offset[static_cast<size_t>(i)] + static_cast<size_t>(j)] =
                    static_cast<size_t>(
                        std::clamp(tx.first[static_cast<size_t>(i)] + j, 0, iw - 1) -
                        sx0) * 4;
        // THE HORIZONTAL PASS ON DEMAND: a source row filtered into r.w x 3
        // doubles the first time a destination row's taps reach it, kept in a
        // window that slides down with the destination rows (their taps never
        // move up), so the intermediate is a handful of rows, not the span.
        std::vector<std::vector<double>> window;
        int window_first = 0;
        const auto filtered = [&](int src_row) -> const double* {
            if (window.empty()) window_first = src_row;
            while (window_first + static_cast<int>(window.size()) <= src_row) {
                const int y = window_first + static_cast<int>(window.size());
                std::vector<double> row(static_cast<size_t>(r.w) * 3, 0.0);
                const unsigned char* srow =
                    src.data() + static_cast<size_t>(y - sy0) * sw * 4;
                for (int i = 0; i < r.w; ++i) {
                    const int cnt = tx.count[static_cast<size_t>(i)];
                    const size_t off = tx.offset[static_cast<size_t>(i)];
                    const double* wt = tx.weights.data() + off;
                    const size_t* at_px = tap_at.data() + off;
                    double acc[3] = {0.0, 0.0, 0.0};
                    for (int j = 0; j < cnt; ++j)
                        for (int c = 0; c < 3; ++c)
                            acc[c] += wt[j] * static_cast<double>(
                                                  srow[at_px[j] + static_cast<size_t>(c)]);
                    for (int c = 0; c < 3; ++c)
                        row[static_cast<size_t>(i) * 3 + static_cast<size_t>(c)] = acc[c];
                }
                window.push_back(std::move(row));
            }
            return window[static_cast<size_t>(src_row - window_first)].data();
        };
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.w, r.h);
        cairo_surface_flush(s);
        unsigned char* ddata = cairo_image_surface_get_data(s);
        const int dstride = cairo_image_surface_get_stride(s);
        std::vector<const double*> tap_rows;
        for (int y = 0; y < r.h; ++y) {
            const int lo = ty.first[static_cast<size_t>(y)];
            const int cnt = ty.count[static_cast<size_t>(y)];
            const double* wt = ty.weights.data() + ty.offset[static_cast<size_t>(y)];
            // Rows above this destination row's first clamped tap leave the
            // window.
            const int first_needed = std::clamp(lo, 0, ih - 1);
            while (!window.empty() && window_first < first_needed) {
                window.erase(window.begin());
                ++window_first;
            }
            tap_rows.resize(static_cast<size_t>(cnt));
            for (int j = 0; j < cnt; ++j)
                tap_rows[static_cast<size_t>(j)] =
                    filtered(std::clamp(lo + j, 0, ih - 1));
            auto* drow = reinterpret_cast<uint32_t*>(ddata + y * dstride);
            for (int x = 0; x < r.w; ++x) {
                double acc[3] = {0.0, 0.0, 0.0};
                for (int j = 0; j < cnt; ++j) {
                    const double* m =
                        tap_rows[static_cast<size_t>(j)] + static_cast<size_t>(x) * 3;
                    for (int c = 0; c < 3; ++c) acc[c] += wt[j] * m[c];
                }
                uint32_t word = 0xFF000000u;
                for (int c = 0; c < 3; ++c)
                    word |= static_cast<uint32_t>(std::clamp(
                                std::nearbyint(acc[c]), 0.0, 255.0))
                            << (16 - 8 * c);
                drow[x] = word;
            }
        }
        cairo_surface_mark_dirty(s);
        out.push_back(s);
    }
    return out;
}

double unit_px(const Document& doc, int px) {
    if (doc.raster) return px / 48.0;   // the 48-unit frame (svg_icon.h)
    const resvg_size size = resvg_get_image_size(doc.tree.get());
    return std::min(px / static_cast<double>(size.width),
                    px / static_cast<double>(size.height));
}

std::pair<float, float> body_center_shift(const Document& doc) {
    // The measured surface and its user unit in px: a PNG's own decode (N
    // px, N / 48 a unit), else the SVG's unshifted render at 240 px.
    constexpr int kMeasurePx = 240;
    cairo_surface_t* s = doc.raster ? doc.raster.get()
                                    : rasterise(doc, kMeasurePx);
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    // THE BODY: alpha >= 0.1, i.e. >= 26 of 255 (0.1 · 255 = 25.5).
    int x0 = w, x1 = -1, y0 = h, y1 = -1;
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(data + y * stride);
        for (int x = 0; x < w; ++x) {
            if (((row[x] >> 24) & 0xFF) < 26) continue;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
    }
    const double unit = doc.raster ? w / 48.0 : unit_px(doc, kMeasurePx);
    if (!doc.raster) cairo_surface_destroy(s);
    if (x1 < 0) return {0.0f, 0.0f};
    // The bbox's center (its pixel edges x0 and x1 + 1) to the frame's, in
    // user units, rounded to the tenth.
    const auto tenth = [](double v) {
        return static_cast<float>(std::nearbyint(v * 10.0) / 10.0);
    };
    const double dx = (w / 2.0 - (x0 + x1 + 1) / 2.0) / unit;
    const double dy = (h / 2.0 - (y0 + y1 + 1) / 2.0) / unit;
    return {tenth(dx) + 0.0f, tenth(dy) + 0.0f};
}

cairo_surface_t* rasterise(const Document& doc, int px, double shift_x,
                           double shift_y, int margin_px) {
    assert(margin_px >= 0);
    if (doc.raster)
        return resample_raster(doc.raster.get(), px, shift_x, shift_y,
                               margin_px);
    // A new image surface is cleared to transparent, which resvg's render
    // composites onto. Its stride is exactly 4 * side: cairo aligns ARGB32
    // rows to 4 bytes, and resvg's pixmap is packed rows of 4 bytes.
    const int side = px + margin_px;
    cairo_surface_t* s =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
    assert(cairo_image_surface_get_stride(s) == 4 * side);
    cairo_surface_flush(s);
    unsigned char* data = cairo_image_surface_get_data(s);

    const resvg_size size = resvg_get_image_size(doc.tree.get());
    const double k = unit_px(doc, px);
    const resvg_transform fit{
        static_cast<float>(k), 0.0f, 0.0f, static_cast<float>(k),
        static_cast<float>((px - size.width * k) / 2.0 + k * shift_x),
        static_cast<float>((px - size.height * k) / 2.0 + k * shift_y)};
    resvg_render(doc.tree.get(), fit, static_cast<uint32_t>(side),
                 static_cast<uint32_t>(side), reinterpret_cast<char*>(data));

    rgba_bytes_to_argb_words(data, side * side);
    cairo_surface_mark_dirty(s);
    return s;
}

cairo_surface_t* embossed_copy(cairo_surface_t* live, cairo_surface_t* light,
                               uint32_t light_rgb, uint32_t shadow_rgb,
                               uint32_t inset_rgb) {
    cairo_surface_flush(live);
    cairo_surface_flush(light);
    const int w = cairo_image_surface_get_width(live);
    const int h = cairo_image_surface_get_height(live);
    const int ow = cairo_image_surface_get_width(light);
    const int oh = cairo_image_surface_get_height(light);
    assert(ow >= w && oh >= h);
    cairo_surface_t* out =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
    cairo_surface_flush(out);
    unsigned char* dst = cairo_image_surface_get_data(out);
    const int dst_stride = cairo_image_surface_get_stride(out);
    // A mask's coverage at (x, y) of surface `s`, 0 outside it.
    const auto coverage = [](cairo_surface_t* s, int x, int y) -> double {
        if (x < 0 || y < 0 || x >= cairo_image_surface_get_width(s) ||
            y >= cairo_image_surface_get_height(s))
            return 0.0;
        const auto* row = reinterpret_cast<const uint32_t*>(
            cairo_image_surface_get_data(s) +
            y * cairo_image_surface_get_stride(s));
        return static_cast<double>((row[x] >> 24) & 0xFF) / 255.0;
    };
    const auto channel = [](uint32_t rgb, int shift) {
        return static_cast<double>((rgb >> shift) & 0xFF);
    };
    for (int y = 0; y < oh; ++y) {
        auto* drow = reinterpret_cast<uint32_t*>(dst + y * dst_stride);
        for (int x = 0; x < ow; ++x) {
            // Three flat tones from the two coverages S (live) and L
            // (light), premultiplied: the light copy OUTSIDE the silhouette,
            // L·(1 − S); the silhouette's body where the copy overlaps it,
            // min(S, L); THE INSET RIM, the silhouette's coverage the copy
            // does not reach, max(0, S − L) — a one-unit band inside S along
            // its top-left edges. The grey and the rim sum to S, so the
            // footprint is the plain emboss's, S + L·(1 − S).
            const double s = coverage(live, x, y);
            const double l = coverage(light, x, y);
            const double al = l * (1.0 - s);
            const double ag = std::min(s, l);
            const double ad = std::max(0.0, s - l);
            uint32_t word = static_cast<uint32_t>(
                                std::nearbyint((s + al) * 255.0))
                            << 24;
            for (int shift = 16; shift >= 0; shift -= 8) {
                const double v = channel(shadow_rgb, shift) * ag +
                                 channel(inset_rgb, shift) * ad +
                                 channel(light_rgb, shift) * al;
                word |= static_cast<uint32_t>(std::nearbyint(v)) << shift;
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
