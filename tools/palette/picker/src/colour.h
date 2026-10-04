#pragma once
// tools/palette/picker — the picker's colour arithmetic. A colour IS ITS BYTE TRIPLE: a hex here is a Display-P3
// byte triple written to the window as is (the product's render.h, "A HEX HERE IS A DISPLAY-P3 BYTE TRIPLE"); no
// conversion anywhere. HSV, HSL and LCh are VIEWS over the bytes (the models below; ColourState, picker.h). THREE
// RULES MAKE A ROLE'S COLOUR FROM
// THE ELEMENTS (scene.h's role table), each ported step for step from tools/palette/colour.py so a byte here equals
// the mock tool's: the waveform outline's linear-light mix (s2l / l2s / lin_mix: the same double expressions in the
// same order, rounded half-to-even -- std::nearbyint in the default mode is Python's round()), the chrome rule's
// Windows 95 proportion (scale_byte), and cairo's own antialiasing blend (over_n_8, pixman's arithmetic), which
// re-blends every antialiased pixel of the scene over whatever colours it reads.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

// #RRGGBB, uppercase
inline std::string hex_of(Rgb c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

// "#rrggbb" (either case) -> true and the triple; anything else -> false
inline bool parse_hex(const std::string& s, Rgb& out) {
    if (s.size() != 7 || s[0] != '#') return false;
    int v[3];
    for (int i = 0; i < 3; ++i) {
        int x = 0;
        for (int k = 1 + 2 * i; k < 3 + 2 * i; ++k) {
            const char ch = s[k];
            const int d = (ch >= '0' && ch <= '9') ? ch - '0'
                        : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10
                        : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
            if (d < 0) return false;
            x = x * 16 + d;
        }
        v[i] = x;
    }
    out = Rgb{uint8_t(v[0]), uint8_t(v[1]), uint8_t(v[2])};
    return true;
}

// the frame's pixel word: cairo's ARGB32, native-endian 0xAARRGGBB, opaque
inline uint32_t word_of(Rgb c) {
    return 0xFF000000u | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.b);
}

// colour.py s2l / l2s: the sRGB transfer function over the byte as given
inline double s2l(int byte) {
    double c = byte / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
inline uint8_t l2s(double v) {
    v = std::max(0.0, std::min(1.0, v));
    const double s = v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
    return uint8_t(std::nearbyint(s * 255));
}
// colour.py lin_mix: a toward b by t in linear light (0 = a, 1 = b), each channel alone
inline uint8_t lin_mix_byte(int a, int b, double t) { return l2s(s2l(a) * (1 - t) + s2l(b) * t); }
inline Rgb lin_mix(Rgb a, Rgb b, double t) {
    return Rgb{lin_mix_byte(a.r, b.r, t), lin_mix_byte(a.g, b.g, t), lin_mix_byte(a.b, b.b, t)};
}

inline uint8_t byte_of_unit(double x) { return uint8_t(std::nearbyint(std::max(0.0, std::min(1.0, x)) * 255)); }

// ---------------------------------------------------------------- THE MODELS (architect 2026-10-04)
// The panel's three upper tracks speak ONE MODEL, chosen by the model switch: HSV (GTK's and GIMP's), HSL (GIMP's and
// CSS's) or LCh (CIE LCh(ab)). Each is a VIEW over the byte triple, never a colour of its own: a view's three numbers
// always give the bytes (rgb_of), and the bytes stay the truth. Stored numbers, per model (axis 0, 1, 2):
//   HSV  h degrees 0..360, s 0..1, v 0..1          shown H, S, V: degrees, percent, percent
//   HSL  h degrees 0..360, s 0..1, l 0..1          shown H, S, L: degrees, percent, percent
//   LCh  L 0..100, C 0..kChromaMax, h degrees 0..360   shown L, C, h as stored
// Each shown to one decimal (picker.h row_readout); the − / + step one shown unit.
enum class Model : uint8_t { Hsv, Hsl, Lch };
constexpr Model kAllModels[3] = {Model::Hsv, Model::Hsl, Model::Lch};
// the C track's range: 0..160, Display-P3's whole gamut (its most chromatic colour, the green primary #00FF00, is
// C 157.75; the blue primary 138.06, the red 133.55), so no P3 colour lies past the track's end
constexpr double kChromaMax = 160;
// the model's word in picks.txt, state.json and presets.json ("hsv", "hsl", "lch") and its name on the switch
inline const char* model_word(Model m) { return m == Model::Hsv ? "hsv" : m == Model::Hsl ? "hsl" : "lch"; }
inline const char* model_name(Model m) { return m == Model::Hsv ? "HSV" : m == Model::Hsl ? "HSL" : "LCh"; }
inline bool model_of_word(const std::string& w, Model& out) {
    for (Model m : kAllModels)
        if (w == model_word(m)) { out = m; return true; }
    return false;
}
// an axis's stored range (0 .. axis_max) and the shown unit per stored unit (percent: 100)
inline double axis_max(Model m, int axis) {
    if (m == Model::Lch) return axis == 0 ? 100 : axis == 1 ? kChromaMax : 360;
    return axis == 0 ? 360 : 1;
}
inline double axis_scale(Model m, int axis) { return m != Model::Lch && axis > 0 ? 100 : 1; }

// a colour as unit channels, UNROUNDED AND UNCLAMPED (an LCh view may lie outside the gamut)
struct Unit { double r = 0, g = 0, b = 0; };
inline Rgb rgb_of_unit(Unit u) { return Rgb{byte_of_unit(u.r), byte_of_unit(u.g), byte_of_unit(u.b)}; }
// IN GAMUT: every channel's own rounding to the nearest byte is a byte (0..255), so the colour reaches the bytes
// without clipping (a channel within half a byte of the cube's face rounds onto it, as every colour's channels round)
inline bool unit_in_gamut(Unit u) {
    for (double x : {u.r, u.g, u.b}) {
        const double k = std::nearbyint(x * 255);
        if (!(k >= 0 && k <= 255)) return false;
    }
    return true;
}

// HSV -> unit channels: h in degrees (360 = 0), s and v in 0..1
inline Unit unit_of_hsv(double h, double s, double v) {
    double hh = std::fmod(h, 360.0);
    if (hh < 0) hh += 360.0;
    hh /= 60.0;
    const int i = std::min(5, int(std::floor(hh)));
    const double f = hh - i;
    const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}
// HSV -> bytes: each channel rounded to the nearest byte
inline Rgb rgb_of_hsv(double h, double s, double v) { return rgb_of_unit(unit_of_hsv(h, s, v)); }

// HSL -> unit channels (GIMP's and CSS's HSL): the chroma c = (1 - |2l - 1|) s, the hue's sector placing c and the
// intermediate x, every channel lifted by m = l - c / 2
inline Unit unit_of_hsl(double h, double s, double l) {
    const double c = (1 - std::abs(2 * l - 1)) * s;
    double hh = std::fmod(h, 360.0);
    if (hh < 0) hh += 360.0;
    hh /= 60.0;
    const int i = std::min(5, int(std::floor(hh)));
    const double f = hh - i;
    const double x = c * (i % 2 ? 1 - f : f), m = l - c / 2;
    switch (i) {
        case 0: return {c + m, x + m, m};
        case 1: return {x + m, c + m, m};
        case 2: return {m, c + m, x + m};
        case 3: return {m, x + m, c + m};
        case 4: return {x + m, m, c + m};
        default: return {c + m, m, x + m};
    }
}
inline Rgb rgb_of_hsl(double h, double s, double l) { return rgb_of_unit(unit_of_hsl(h, s, l)); }

// THE HUE OF A BYTE TRIPLE (HSV's and HSL's, the same expression), degrees 0..360; mx > mn
inline double hue_of_bytes(Rgb c, int mx, int mn) {
    const double d = mx - mn;
    double hh;
    if (mx == c.r) hh = (c.g - c.b) / d;
    else if (mx == c.g) hh = 2 + (c.b - c.r) / d;
    else hh = 4 + (c.r - c.g) / d;
    hh *= 60;
    if (hh < 0) hh += 360;
    return hh;
}

// ---------------------------------------------------------------- LCh OVER DISPLAY-P3
// CIE LCh(ab) as GIMP's LCh scales show it (L 0..100, C, h degrees), COMPUTED OVER THE BYTES AS DISPLAY-P3 -- the
// window's colour space (render.h: "A HEX HERE IS A DISPLAY-P3 BYTE TRIPLE") -- with P3's own white, D65, as the Lab
// reference white and no chromatic adaptation, so the numbers describe what the glass shows (architect 2026-10-04).
// The bytes decode by P3's transfer curve (the sRGB curve, s2l's), the linear channels go to XYZ by the matrix of P3's
// primaries R (0.680, 0.320), G (0.265, 0.690), B (0.150, 0.060) over D65 (0.3127, 0.3290), and XYZ to Lab by CIE
// 15's exact constants (6/29). The white is the matrix's own image of (1, 1, 1), so the bytes' white is L 100, C 0.
// GIMP's LCh is babl's, D50-adapted, over the image's space (sRGB on the laptop): THE SAME HEX READS OTHER LCh NUMBERS
// IN GIMP.
namespace p3 {
struct Mat { double m[3][3]; };
inline Mat inverse(const Mat& a) {
    const auto& m = a.m;
    const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1], c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2],
                 c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    Mat r;
    r.m[0][0] = c00 / det;
    r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    r.m[1][0] = c01 / det;
    r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    r.m[2][0] = c02 / det;
    r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    return r;
}
// linear P3 -> XYZ: the primaries' XYZ (Y = 1) as columns, each scaled so that (1, 1, 1) is D65's XYZ
inline Mat make_to_xyz() {
    const double xy[4][2] = {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {0.3127, 0.3290}};
    Mat p;
    for (int k = 0; k < 3; ++k) {
        p.m[0][k] = xy[k][0] / xy[k][1];
        p.m[1][k] = 1;
        p.m[2][k] = (1 - xy[k][0] - xy[k][1]) / xy[k][1];
    }
    const double w[3] = {xy[3][0] / xy[3][1], 1, (1 - xy[3][0] - xy[3][1]) / xy[3][1]};
    const Mat pi = inverse(p);
    double sc[3];
    for (int k = 0; k < 3; ++k) sc[k] = pi.m[k][0] * w[0] + pi.m[k][1] * w[1] + pi.m[k][2] * w[2];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) p.m[r][k] *= sc[k];
    return p;
}
inline const Mat& to_xyz() { static const Mat m = make_to_xyz(); return m; }
inline const Mat& from_xyz() { static const Mat m = inverse(to_xyz()); return m; }
// the white: the matrix's rows summed (its image of 1, 1, 1)
inline double white(int k) { const auto& m = to_xyz().m; return m[k][0] + m[k][1] + m[k][2]; }
// the transfer curve on unit doubles (s2l / l2s without the byte or the clamp; a negative channel on the linear foot)
inline double decode(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
inline double encode(double x) { return x <= 0.0031308 ? x * 12.92 : 1.055 * std::pow(x, 1 / 2.4) - 0.055; }
constexpr double kD = 6.0 / 29;
inline double lab_f(double t) { return t > kD * kD * kD ? std::cbrt(t) : t / (3 * kD * kD) + 4.0 / 29; }
inline double lab_finv(double f) { return f > kD ? f * f * f : 3 * kD * kD * (f - 4.0 / 29); }
} // namespace p3

// unit channels -> L, C, h (h in degrees 0..360; at C 0 the hue is atan2's of the round-off and means nothing: the
// caller keeps its own, ColourState's retention). A NEUTRAL (r == g == b) is exactly C 0, L from its luminance alone
inline void lch_of_unit(Unit u, double out[3]) {
    if (u.r == u.g && u.g == u.b) {
        out[0] = 116 * p3::lab_f(p3::decode(u.r)) - 16;
        out[1] = 0;
        out[2] = 0;
        return;
    }
    const double lin[3] = {p3::decode(u.r), p3::decode(u.g), p3::decode(u.b)};
    const auto& m = p3::to_xyz().m;
    double f[3];
    for (int k = 0; k < 3; ++k) f[k] = p3::lab_f((m[k][0] * lin[0] + m[k][1] * lin[1] + m[k][2] * lin[2]) / p3::white(k));
    const double a = 500 * (f[0] - f[1]), b = 200 * (f[1] - f[2]);
    out[0] = 116 * f[1] - 16;
    out[1] = std::hypot(a, b);
    double h = std::atan2(b, a) * (180 / 3.14159265358979323846);
    if (h < 0) h += 360;
    out[2] = h >= 360 ? 0 : h;
}
// L, C, h -> unit channels, unclamped (unit_in_gamut says whether the bytes reach it); C 0 is the neutral of L, every
// channel the same
inline Unit unit_of_lch(double L, double C, double h) {
    const double fy = (L + 16) / 116;
    if (C == 0) {
        const double e = p3::encode(p3::lab_finv(fy));
        return {e, e, e};
    }
    const double hr = h * (3.14159265358979323846 / 180);
    const double fx = fy + C * std::cos(hr) / 500, fz = fy - C * std::sin(hr) / 200;
    const double xyz[3] = {p3::white(0) * p3::lab_finv(fx), p3::lab_finv(fy), p3::white(2) * p3::lab_finv(fz)};
    const auto& m = p3::from_xyz().m;
    double lin[3];
    for (int k = 0; k < 3; ++k) lin[k] = m[k][0] * xyz[0] + m[k][1] * xyz[1] + m[k][2] * xyz[2];
    return {p3::encode(lin[0]), p3::encode(lin[1]), p3::encode(lin[2])};
}
inline bool lch_in_gamut(double L, double C, double h) { return unit_in_gamut(unit_of_lch(L, C, h)); }

// a model's view -> unit channels, and -> bytes
inline Unit unit_of_view(Model m, const double x[3]) {
    return m == Model::Hsv ? unit_of_hsv(x[0], x[1], x[2]) : m == Model::Hsl ? unit_of_hsl(x[0], x[1], x[2])
                                                                             : unit_of_lch(x[0], x[1], x[2]);
}
inline Rgb rgb_of_view(Model m, const double x[3]) { return rgb_of_unit(unit_of_view(m, x)); }

// THE CHROME RULE'S CHANNEL (colour.py scale_byte; architect 2026-10-04, Windows 95's proportions): c x num / den
// rounded to nearest, HALF TO EVEN (levels.py's rounding, Python's round), capped at 255 -- integer arithmetic, so
// exact. A tie occurs (32 x 255 / 192 = 42.5 -> 42, 96 x 223 / 192 = 111.5 -> 112).
inline uint8_t scale_byte(int c, int num, int den) {
    const int p = c * num;
    int q = p / den;
    const int r = p % den;
    if (2 * r > den || (2 * r == den && (q & 1))) ++q;
    return uint8_t(std::min(255, q));
}
inline Rgb scale_rgb(Rgb c, int num, int den) {
    return Rgb{scale_byte(c.r, num, den), scale_byte(c.g, num, den), scale_byte(c.b, num, den)};
}

// CAIRO'S ANTIALIASING BLEND: an opaque solid source through an 8-bit coverage m onto the opaque frame, the path cairo
// takes for every glyph and icon mask of the scene (render.py's paint record). Ported line for line from pixman 0.46.4
// (cairo's engine): pixman-fast-path.c fast_composite_over_n_8_8888's loop body --
//     if (m == 0xff) { if (srca == 0xff) *dst = src; ... } else if (m) { d = in (src, m); *dst = over (d, *dst); }
// -- with in() = UN8x4_MUL_UN8 and over() = UN8x4_MUL_UN8_ADD_UN8x4 (dest, ~d >> 24, d) of pixman-combine32.h,
// whose lanes are UN8_rb_MUL_UN8 (t = x x a + 0x800080 per red/blue pair, (t + ((t >> 8) & 0xff00ff)) >> 8) and the
// saturating UN8_rb_ADD_UN8_rb (t |= 0x1000100 - ((t >> 8) & 0xff00ff)). The words are the frame's 0xAARRGGBB; the
// source is opaque (every scene colour is). build_picker.sh --check compares it with cairo on every (source, frame,
// coverage) byte triple, and the scene's recomposition with the mock tool's render byte for byte.
namespace pixman {
constexpr uint32_t G_SHIFT = 8, RB_MASK = 0xff00ff, RB_ONE_HALF = 0x800080, RB_MASK_PLUS_ONE = 0x1000100;
inline uint32_t un8_rb_mul_un8(uint32_t x, uint32_t a) {          // UN8_rb_MUL_UN8
    uint32_t t = (x & RB_MASK) * a;
    t += RB_ONE_HALF;
    x = (t + ((t >> G_SHIFT) & RB_MASK)) >> G_SHIFT;
    return x & RB_MASK;
}
inline uint32_t un8_rb_add_un8_rb(uint32_t x, uint32_t y) {       // UN8_rb_ADD_UN8_rb
    uint32_t t = x + y;
    t |= RB_MASK_PLUS_ONE - ((t >> G_SHIFT) & RB_MASK);
    return t & RB_MASK;
}
inline uint32_t un8x4_mul_un8(uint32_t x, uint32_t a) {           // UN8x4_MUL_UN8
    const uint32_t r1 = un8_rb_mul_un8(x, a);
    const uint32_t r2 = un8_rb_mul_un8(x >> G_SHIFT, a);
    return r1 | (r2 << G_SHIFT);
}
inline uint32_t un8x4_mul_un8_add_un8x4(uint32_t x, uint32_t a, uint32_t y) {   // UN8x4_MUL_UN8_ADD_UN8x4
    uint32_t r1 = un8_rb_mul_un8(x, a);
    r1 = un8_rb_add_un8_rb(r1, y & RB_MASK);
    uint32_t r2 = un8_rb_mul_un8(x >> G_SHIFT, a);
    r2 = un8_rb_add_un8_rb(r2, (y >> G_SHIFT) & RB_MASK);
    return r1 | (r2 << G_SHIFT);
}
} // namespace pixman

// the frame word after an opaque source word `src` through coverage m over the frame word `dst`
inline uint32_t over_n_8(uint32_t src, uint8_t m, uint32_t dst) {
    if (m == 0xff) return src;                                     // the opaque source's branch
    if (m == 0) return dst;
    const uint32_t d = pixman::un8x4_mul_un8(src, m);              // in (src, m)
    return pixman::un8x4_mul_un8_add_un8x4(dst, ~d >> 24, d);      // over (d, dst)
}
