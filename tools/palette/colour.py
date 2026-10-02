#!/usr/bin/env python3
# tools/palette/colour.py — the tool's one colour owner: sRGB <-> linear light, the sRGB -> Display-P3 pass, the
# linear-light blend, and the default theme's two-pass rule.
#
# srgb_to_p3 is one pass of the platform's sRGB -> Display-P3 conversion in linear light (sRGB -> XYZ D65 -> P3),
# rounded to bytes; a theme colour written "srgb:#rrggbb" goes through it once. lin_mix is the GIMP-style
# linear-light blend (the waveform outline is the 50 % blend of ink over canvas). two_pass is how Samsung Gallery
# showed an untagged sRGB constant at full screen before the app's window became a Display-P3 layer: the pass applied
# twice, measured patch for patch off the tablet's screencaps (2026-10-02); the matrix reproduces that measurement
# to the unit except on the colours in MEASURED_TWO_PASS, which hold the measured bytes.


def s2l(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
def l2s(v):
    v = max(0.0, min(1.0, v))
    s = v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055
    return int(round(s * 255))

# sRGB -> XYZ (D65) and Display P3 -> XYZ (D65)
M_srgb = [[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]]
M_p3 = [[0.4865709, 0.2656677, 0.1982173], [0.2289746, 0.6917385, 0.0792869], [0.0000000, 0.0451134, 1.0439444]]
def inv3(m):
    a, b, c = m[0]; d, e, f = m[1]; g, h, i = m[2]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    return [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det],
            [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
            [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]
def mul(m, v): return [sum(m[r][k] * v[k] for k in range(3)) for r in range(3)]
Mi_p3 = inv3(M_p3)

def srgb_to_p3(rgb):
    """One sRGB -> Display-P3 pass: bytes in, bytes out."""
    lin = [s2l(c) for c in rgb]; xyz = mul(M_srgb, lin); p = mul(Mi_p3, xyz); return tuple(l2s(v) for v in p)

def lin_mix(a, b, t):
    """The linear-light blend of a toward b by t (0 = a, 1 = b), rounded to bytes."""
    return tuple(l2s(s2l(a[i]) * (1 - t) + s2l(b[i]) * t) for i in range(3))

MEASURED_TWO_PASS = {
    (11, 6, 13): (9, 6, 13), (25, 15, 30): (23, 15, 28), (51, 74, 84): (59, 72, 82), (57, 74, 83): (63, 73, 81),
    (61, 74, 79): (66, 74, 78), (70, 40, 83): (62, 42, 78), (71, 41, 84): (63, 43, 79), (72, 42, 85): (64, 44, 80),
    (75, 43, 89): (67, 45, 83), (76, 44, 89): (67, 46, 84), (76, 44, 90): (67, 46, 84), (95, 46, 53): (83, 51, 54),
    (96, 46, 54): (83, 51, 55), (97, 47, 54): (84, 52, 55), (97, 55, 113): (86, 58, 107),
    (98, 56, 115): (87, 59, 109), (103, 59, 121): (92, 63, 114), (110, 64, 130): (98, 68, 122),
    (111, 64, 130): (98, 68, 122), (111, 64, 131): (98, 68, 123), (112, 65, 132): (99, 69, 124),
    (122, 195, 224): (150, 191, 218), (141, 54, 65): (122, 64, 67), (142, 54, 64): (122, 64, 67),
    (143, 55, 64): (123, 65, 67), (145, 83, 170): (129, 88, 160), (145, 83, 171): (129, 88, 161),
    (146, 84, 171): (130, 89, 161), (146, 84, 172): (130, 89, 162), (152, 88, 179): (135, 93, 169),
    (160, 92, 189): (143, 97, 179), (161, 93, 190): (144, 98, 180), (162, 94, 191): (145, 99, 181),
    (165, 95, 194): (147, 101, 184), (172, 68, 76): (148, 80, 80), (172, 99, 203): (153, 105, 191),
    (173, 60, 72): (149, 73, 76), (173, 68, 77): (149, 80, 81), (173, 100, 204): (154, 106, 192),
    (180, 70, 79): (156, 82, 83), (181, 104, 213): (161, 111, 201), (193, 111, 228): (172, 118, 216),
    (202, 75, 85): (174, 89, 91), (203, 65, 79): (174, 82, 85), (204, 66, 79): (175, 83, 85)
}
def two_pass(rgb):
    """The default theme's colour of an app constant: the measured bytes where they differ from the matrix, else
    srgb_to_p3 twice."""
    k = tuple(int(v) for v in rgb)
    return MEASURED_TWO_PASS.get(k) or tuple(srgb_to_p3(srgb_to_p3(k)))
