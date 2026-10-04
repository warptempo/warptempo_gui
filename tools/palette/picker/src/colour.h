#pragma once
// tools/palette/picker — the picker's colour arithmetic. A colour IS ITS BYTE TRIPLE: a hex here is a Display-P3
// byte triple written to the window as is (the product's render.h, "A HEX HERE IS A DISPLAY-P3 BYTE TRIPLE"); no
// conversion anywhere. HSV is a VIEW over the bytes (ColourState, picker.h). THREE RULES MAKE A ROLE'S COLOUR FROM
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

// HSV -> bytes: h in degrees (360 = 0), s and v in 0..1; each channel rounded to the nearest byte
inline Rgb rgb_of_hsv(double h, double s, double v) {
    double hh = std::fmod(h, 360.0);
    if (hh < 0) hh += 360.0;
    hh /= 60.0;
    const int i = std::min(5, int(std::floor(hh)));
    const double f = hh - i;
    const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double r, g, b;
    switch (i) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return Rgb{byte_of_unit(r), byte_of_unit(g), byte_of_unit(b)};
}

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
