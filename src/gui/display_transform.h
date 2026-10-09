#pragma once

// THE DISPLAY TRANSFORM (architect 2026-10-08 ~05:15): EVERY AUTHORED COLOR IS
// AN sRGB BYTE TRIPLE — a palette role, a chrome role (Windows' own scheme
// bytes are sRGB records), the picker's hex, a catalog theme's byte, an
// icon's own inks — "if I open that image in the web browser, it would have
// the identical color on the GUI". A screenshot he lifts a hex from is
// unprofiled, which a browser and GIMP take as sRGB; so is every scheme file.
// THE TABLET'S WINDOW IS A DISPLAY-P3 LAYER (GuiPlatform::adopt_window,
// platform_android.cpp), so a byte handed to it untouched reads as a P3
// coordinate — more saturated than the browser shows the same hex ("ours
// looks slightly more neon"). THE TABLET THEREFORE CONVERTS sRGB -> DISPLAY-P3
// AT THE PAINTER'S ENTRY, and the panel receives P3 bytes that present the
// sRGB color as the browser presents it. The laptop's Wayland surface is
// untagged sRGB and converts nothing: there the transform never runs and
// every byte is today's.
//
// WHERE IT RUNS, the one list (re-grep `display_transform::` to re-derive it):
// set_palette_source and set_waveform_source (render.cpp — every role paint,
// the text's ink included); the words the painters write by hand (render.cpp:
// paint_checker_rect's two cells, paint_caption_gradient's ramp, and the
// waveform plate's two inks, converted on the GUI thread as
// waveform_plate_inks hands them to the job, so the worker writes them
// as they come); the icon rasters (icons.cpp, through
// svg_icon::convert_to_display); the color picker's ring and triangle
// (color_picker.cpp's premultiplied). THE CONVERSION IS AT THE
// SOURCE, never a frame-level pass: the window buffer is copied byte for
// byte, every flat color is exact, and an antialiased edge or a ramp's
// in-between level is the renderer's blend of converted colors (render.h's
// standing clause: such edges are the renderer's and are not colors).
//
// THE MATH, both spaces D65 and sharing the sRGB transfer, so no chromatic
// adaptation (no Bradford) enters:
//   1. DECODE each 8-bit channel v/255 through IEC 61966-2-1's piecewise
//      curve: v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055)^2.4 — one
//      256-entry table (decode_table).
//   2. ONE 3x3 on the linear triple, M = P3_from_XYZ * XYZ_from_sRGB, each
//      RGB->XYZ matrix derived below from its primaries and the D65 white
//      (0.3127, 0.3290) by the textbook construction (the primaries' XYZ as
//      columns, each column scaled so the white is their sum):
//        sRGB        R 0.64/0.33   G 0.30/0.60   B 0.15/0.06
//        Display-P3  R 0.680/0.320 G 0.265/0.690 B 0.150/0.060
//      giving (to ten places; the compiler derives the full doubles)
//        kSrgbToP3 = | 0.8224619687  0.1775380313  0.0000000000 |
//                    | 0.0331941989  0.9668058011  0.0000000000 |
//                    | 0.0170826307  0.0723974407  0.9105199286 |
//        kP3ToSrgb = | 1.2249401763 -0.2249401763  0.0000000000 |
//                    |-0.0420569547  1.0420569547  0.0000000000 |
//                    |-0.0196375546 -0.0786360456  1.0982736001 |
//      (each row sums to 1: a neutral maps to the same neutral, so a gray is
//      byte-identical on both devices).
//   3. ENCODE back through the same curve and round to the nearest byte:
//      the byte is the count of the 255 decision levels decode((k + 0.5) /
//      255) at or below the linear value (encode_byte) — exactly
//      nearbyint(255 * encode(L)) but for an exact half, which measures zero;
//      below 0 is 0 and above 1 is 255 (sRGB lies inside P3, so the forward
//      transform leaves only float dust outside [0, 1]; the inverse CLIPS a
//      P3 color outside sRGB to the sRGB gamut's edge, channel by channel).
// Worked: sRGB 3FF3A2 -> P3 7CF0A9, 3AD884 -> 6ED58C, D4D0C8 -> D3D0C9.
//
// THE INVERSE (srgb_word_from_p3) runs nowhere in the product: it is here so a
// color picked by eye under the 2026-10-02 semantics (a hex WAS the P3 byte
// the panel showed) can be converted once to the sRGB byte that presents the
// same color — kP3ToSrgb above is its matrix, the curve the same.
//
// THE SWITCH (active) IS TWO BITS, both true for the transform to run:
//   * THE WINDOW'S SPACE (set_window_display_p3) — GuiPlatform::
//     window_is_display_p3, read once by gui_main right after the window
//     exists and before any painter or worker thread runs; nothing changes it
//     after.
//   * TRUE COLORS (set_true_colors; architect 2026-10-08 ~05:35) — the
//     Settings menu's "True Colors" row, CHECKED, TRUE AT EVERY LAUNCH ("the
//     default is on, not something off by default") and no device key: with
//     the conversion on, a screenshot holds the converted P3 bytes, so a hex
//     read off it in GIMP is not the authored one ("I won't be able to
//     round-trip"); app screenshots are "only for occasional documentation
//     or sharing, not design", so he unchecks it for the capture alone. While
//     it is FALSE nothing converts: the raw sRGB bytes go to the P3 layer, a
//     capture round-trips byte for byte, and the glass reads oversaturated
//     meanwhile. On the laptop the row is present and acts (symmetry), and
//     changes nothing: there is no conversion to turn off. PROCESS-LIFETIME
//     STATE, FILE-SCOPE HERE AND NOT ON AppState, which gui_main rebuilds at
//     every project reopen while the window and its caches stand (the live
//     palette words' reason, render.h's program_palette_words). Its one
//     writer is install_true_colors (render.h), which owes the rebuild of
//     every cached converted pixel.
// BOTH ARE PLAIN FLAGS READ ON THE GUI THREAD ALONE: every reader above runs
// there (the waveform worker is handed the plate's inks already converted),
// so the flip between two frames needs no synchronisation.
//
// GUI-ONLY: warptempo_cli paints nothing and never includes this header.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace display_transform {

struct Mat3 {
    double m[3][3];
};

constexpr Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

// The inverse by the adjugate over the determinant.
constexpr Mat3 inverse(const Mat3& a) {
    const auto& m = a.m;
    const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    Mat3 r{};
    r.m[0][0] = c00 / det;
    r.m[1][0] = c01 / det;
    r.m[2][0] = c02 / det;
    r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    return r;
}

// RGB -> XYZ from the three primaries' and the white's xy chromaticities.
constexpr Mat3 rgb_to_xyz(double rx, double ry, double gx, double gy,
                          double bx, double by, double wx, double wy) {
    const Mat3 p{{{rx / ry, gx / gy, bx / by},
                  {1.0, 1.0, 1.0},
                  {(1.0 - rx - ry) / ry, (1.0 - gx - gy) / gy,
                   (1.0 - bx - by) / by}}};
    const double w[3] = {wx / wy, 1.0, (1.0 - wx - wy) / wy};
    const Mat3 pi = inverse(p);
    double s[3]{};
    for (int i = 0; i < 3; ++i)
        s[i] = pi.m[i][0] * w[0] + pi.m[i][1] * w[1] + pi.m[i][2] * w[2];
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = p.m[i][j] * s[j];
    return r;
}

inline constexpr Mat3 kSrgbToXyz =
    rgb_to_xyz(0.64, 0.33, 0.30, 0.60, 0.15, 0.06, 0.3127, 0.3290);
inline constexpr Mat3 kP3ToXyz =
    rgb_to_xyz(0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290);
inline constexpr Mat3 kSrgbToP3 = mul(inverse(kP3ToXyz), kSrgbToXyz);
inline constexpr Mat3 kP3ToSrgb = mul(inverse(kSrgbToXyz), kP3ToXyz);

// The sRGB curve's decode of v / 255, for every byte v.
inline const std::array<double, 256>& decode_table() {
    static const std::array<double, 256> t = [] {
        std::array<double, 256> d{};
        for (int v = 0; v < 256; ++v) {
            const double e = v / 255.0;
            d[static_cast<size_t>(v)] =
                e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4);
        }
        return d;
    }();
    return t;
}

// The 255 decision levels: level k is the linear value whose encode is
// (k + 0.5) / 255, the boundary between bytes k and k + 1.
inline const std::array<double, 255>& decision_levels() {
    static const std::array<double, 255> t = [] {
        std::array<double, 255> d{};
        for (int k = 0; k < 255; ++k) {
            const double e = (k + 0.5) / 255.0;
            d[static_cast<size_t>(k)] =
                e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4);
        }
        return d;
    }();
    return t;
}

inline uint32_t encode_byte(double linear) {
    const auto& lv = decision_levels();
    return static_cast<uint32_t>(
        std::upper_bound(lv.begin(), lv.end(), linear) - lv.begin());
}

// One 0xRRGGBB word through the matrix: decode, multiply, encode.
inline uint32_t transform_word(const Mat3& m, uint32_t rgb) {
    const auto& d = decode_table();
    const double in[3] = {d[(rgb >> 16) & 0xFF], d[(rgb >> 8) & 0xFF],
                          d[rgb & 0xFF]};
    uint32_t out = 0;
    for (int i = 0; i < 3; ++i) {
        const double l =
            m.m[i][0] * in[0] + m.m[i][1] * in[1] + m.m[i][2] * in[2];
        out |= encode_byte(l) << (16 - 8 * i);
    }
    return out;
}

// THE TRANSFORM: an sRGB 0xRRGGBB to the Display-P3 0xRRGGBB that presents
// the same color.
inline uint32_t p3_word_from_srgb(uint32_t srgb) {
    return transform_word(kSrgbToP3, srgb);
}
// THE INVERSE (the head: the one-time conversion of a P3-picked color).
inline uint32_t srgb_word_from_p3(uint32_t p3) {
    return transform_word(kP3ToSrgb, p3);
}

// THE SWITCH'S TWO BITS (the head).
inline bool g_window_display_p3 = false;
inline bool g_true_colors       = true;
inline void set_window_display_p3(bool on) { g_window_display_p3 = on; }
inline void set_true_colors(bool on) { g_true_colors = on; }
inline bool true_colors() { return g_true_colors; }
inline bool active() { return g_window_display_p3 && g_true_colors; }

// An sRGB 0xRRGGBB as the window takes it: converted on a P3 window, the
// word itself otherwise.
inline uint32_t display_rgb(uint32_t srgb) {
    return active() ? p3_word_from_srgb(srgb) : srgb;
}

// One PREMULTIPLIED ARGB32 pixel (an antialiased raster's) as the window takes
// it: un-premultiplied by std::nearbyint, converted, premultiplied again by
// std::nearbyint at the same alpha. A pixel whose straight color the
// transform leaves unchanged (a neutral; any pixel off a P3 window) keeps its
// word whole, so the un-premultiply's rounding never moves a gray.
inline uint32_t display_premultiplied(uint32_t argb) {
    if (!active()) return argb;
    const uint32_t a = argb >> 24;
    if (a == 0) return argb;
    uint32_t straight = 0;
    for (int k = 0; k < 3; ++k) {
        const double v = static_cast<double>((argb >> (16 - 8 * k)) & 0xFF);
        straight |= static_cast<uint32_t>(
                        std::min(255.0, std::nearbyint(v * 255.0 / a)))
                    << (16 - 8 * k);
    }
    const uint32_t converted = p3_word_from_srgb(straight);
    if (converted == straight) return argb;
    uint32_t out = a << 24;
    for (int k = 0; k < 3; ++k) {
        const double v = static_cast<double>((converted >> (16 - 8 * k)) & 0xFF);
        out |= static_cast<uint32_t>(std::nearbyint(v * a / 255.0))
               << (16 - 8 * k);
    }
    return out;
}

} // namespace display_transform
