#pragma once
// tools/palette/picker — the picker's colour arithmetic. A colour IS ITS BYTE TRIPLE: a hex here is a Display-P3
// byte triple written to the window as is (the product's render.h, "A HEX HERE IS A DISPLAY-P3 BYTE TRIPLE"); no
// conversion anywhere. HSV is a VIEW over the bytes (ColourState, picker.h). The one blend is a derived layer's
// rule, the linear-light mix, ported step for step from tools/palette/colour.py (s2l / l2s / lin_mix) so a derived
// byte here equals the mock tool's: the same double expressions in the same order, rounded half-to-even
// (std::nearbyint in the default mode is Python's round()).

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
