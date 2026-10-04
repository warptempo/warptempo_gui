#!/usr/bin/env python3
# tools/palette/colour.py — the tool's one colour owner: sRGB <-> linear light, the sRGB -> Display-P3 pass, the
# linear-light blend, the default theme's two-pass rule, the chrome rule (windows95_chrome) and cairo's antialiasing
# blend (over_coverage).
#
# srgb_to_p3 is one pass of the platform's sRGB -> Display-P3 conversion in linear light (sRGB -> XYZ D65 -> P3),
# rounded to bytes; a theme colour written "srgb:#rrggbb" goes through it once. lin_mix is the GIMP-style
# linear-light blend (the waveform outline is the 50 % blend of ink over canvas). two_pass is how Samsung Gallery
# showed an untagged sRGB constant at full screen before the app's window became a Display-P3 layer: the pass applied
# twice, measured patch for patch off the tablet's screencaps (2026-10-02); the matrix reproduces that measurement
# to the unit except on the colours in MEASURED_TWO_PASS, which hold the measured bytes.
#
# highlight_text_ink and relative_luminance are a RETAINED LEGACY OPTION, for re-rendering mock sets made before
# 2026-10-03: they port the app's text-over-a-fill rule as it stood in src/gui/render.h's palette block through that
# date (relative_luminance, kHighlightTextLuminanceThreshold, highlight_text_ink), integer arithmetic for integer
# arithmetic, the asserts under them that rule's own static_asserts, run at import. THE APP RETIRED THE RULE
# 2026-10-03: text over a fill is now the theme's own RECORDED PAIR (render.h, Windows' ButtonFace/ButtonText
# convention), no luminance derivation — this module keeps the old arithmetic only so scenes built under it still
# render unchanged; a new scene reads the theme's recorded pair instead (render.py). THE RELIEF IS NEVER DERIVED HERE
# (architect 2026-10-03, "no derived, imported only"): a theme states its four relief bytes, recorded from its source
# (tools/theme_catalog/, docs/themes/catalog.json).


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

# ------------------------------------------------------------------ THE CHROME RULE (architect 2026-10-04: the chrome is ONE KNOB,
# the ground; tools/palette/picker/ picks it on the glass)
# WINDOWS 95'S PROPORTIONS: Windows 95 Standard's relief quartet over its ground 192 (#C0C0C0; theme_table.h's
# `windows-95-standard` light row: Hilight #FFFFFF, 3DLight #DFDFDF, Shadow #808080, DkShadow #000000) carried to
# any ground -- each line is the ground x (its Windows 95 byte) / 192, PER CHANNEL, so a tinted ground keeps its hue
# in every line; DkShadow is #000000 whatever the ground; Windows 95's field (COLOR_WINDOW, white) is its Hilight and
# so is the emboss's light copy (levels.py's LIGHT rule: the recorded Hilight). The theme `warptempo`'s light row is
# exactly this at the ground #191919 (25 x 255 / 192 = 33.20 -> #21, x 223 / 192 = 29.04 -> #1D, x 128 / 192 = 16.67
# -> #11; theme_table.h's row 0x191919 / 0x212121 / 0x1D1D1D / 0x111111 / 0x000000, field and emboss 0x212121).
# THE ROUNDING IS levels.py's (its rgb(): `int(round(v * 255))`, Python's round, HALF TO EVEN), here on the exact
# rational ground x num / 192, capped at 255: scale_byte below, integer arithmetic, and the picker's
# src/colour.h scale_byte the same step for step. A tie occurs (32 x 255 / 192 = 42.5 -> 42; 96 x 223 / 192 = 111.5
# -> 112), so the half-to-even mode is part of the rule.
CHROME_DEN = 192
CHROME_LINES = (('bevel_hilight', 255), ('bevel_light', 223), ('bevel_shadow', 128))   # (role, Windows 95's byte)
CHROME_SAME = (('field_ground', 'bevel_hilight'), ('emboss_hilight', 'bevel_hilight'))
CHROME_FIXED = (('bevel_dkshadow', (0, 0, 0)),)
CHROME_ROLES = ('ground',) + tuple(r for r, _ in CHROME_LINES) + tuple(r for r, _ in CHROME_FIXED) + tuple(r for r, _ in CHROME_SAME)

def scale_byte(c, num, den=CHROME_DEN):
    """One channel of the chrome rule: c x num / den rounded half to even (levels.py's round), capped at 255."""
    q, r = divmod(int(c) * num, den)
    if 2 * r > den or (2 * r == den and q % 2): q += 1
    return min(255, q)

def windows95_chrome(ground):
    """-> {role: (r, g, b)} for every role in CHROME_ROLES at the ground (8-bit), the rule above."""
    g = tuple(int(v) for v in ground)
    out = {'ground': g}
    for role, num in CHROME_LINES: out[role] = tuple(scale_byte(v, num) for v in g)
    for role, c in CHROME_FIXED: out[role] = c
    for role, same in CHROME_SAME: out[role] = out[same]
    return out

# THE CHECKS (run at import): the rounding is levels.py's on every byte and numerator, and the neutral #191919 gives
# the theme `warptempo`'s row exactly
assert all(scale_byte(c, n) == min(255, int(round(c * n / CHROME_DEN))) for c in range(256) for n in (255, 223, 128))
assert windows95_chrome((0x19, 0x19, 0x19)) == {
    'ground': (0x19,) * 3, 'bevel_hilight': (0x21,) * 3, 'bevel_light': (0x1D,) * 3, 'bevel_shadow': (0x11,) * 3,
    'bevel_dkshadow': (0, 0, 0), 'field_ground': (0x21,) * 3, 'emboss_hilight': (0x21,) * 3}
assert windows95_chrome((0xC0, 0xC0, 0xC0))['bevel_light'] == (0xDF,) * 3      # Windows 95 Standard itself


# ------------------------------------------------------------------ CAIRO'S ANTIALIASING BLEND (the picker re-blends with it)
# Text and icon glyphs are the only antialiased paints (render.py's PaintRecord): an opaque solid source through an
# 8-bit coverage m onto the opaque frame. cairo hands that to pixman, whose arithmetic is pixman-0.46.4's
# pixman-fast-path.c fast_composite_over_n_8_8888 (the loop body: m 0xff and an opaque source -> the source; m 0 ->
# the frame untouched; else d = in (src, m), *dst = over (d, *dst)) over pixman-combine32.h's macros: in() is
# UN8x4_MUL_UN8, over() UN8x4_MUL_UN8_ADD_UN8x4 (dest x (255 - alpha(d)) + d), each lane's product
# MUL: t = x x a + 0x80, (t + (t >> 8)) >> 8 (UN8_rb_MUL_UN8), the sum saturating at 255 (UN8_rb_ADD_UN8_rb). The four
# lanes never carry into each other, so this is the same arithmetic one channel at a time; the picker's
# src/colour.h over_n_8 is the word form, the macros line for line. render.py --export proves this against cairo's
# own picture (every antialiased pixel recomposed byte for byte), and build_picker.sh --check the C++ form against
# cairo on every (source, frame, coverage) byte triple.
def _mul_un8(x, a):
    t = x * a + 0x80
    return (t + (t >> 8)) >> 8

def over_coverage(src, m, dst):
    """One channel (ints or numpy int arrays): the frame byte dst after an opaque src byte through coverage m."""
    d = _mul_un8(src, m)                               # in (src, m)
    alpha = _mul_un8(0xFF, m)                          # alpha (d): the opaque source's 0xff through the same multiply
    out = _mul_un8(dst, 0xFF - alpha) + d              # over (d, dst): dest x (255 - alpha) + d, saturating
    return out.clip(None, 255) if hasattr(out, 'dtype') else min(255, out)

# the fast path's two branches are this arithmetic's own ends: m 0xff gives the source (MUL(x, 0xff) = x, MUL(x, 0) =
# 0) and m 0 the frame
assert all(_mul_un8(x, 0xFF) == x and _mul_un8(x, 0) == 0 for x in range(256))


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


# ------------------------------------------------------------------ the renderer's legacy luminance option (render.h's rule
# until 2026-10-03; the app now takes text over a fill from the theme's recorded pair instead, never by derivation)
def srgb_channel_to_linear(c):
    """render.h srgb_channel_to_linear as it stood until 2026-10-03, step for step (the 2.4 power as x^2 x (x^2)^(1/5),
    the fifth root by 64 Newton steps from above), so the legacy luminance matches that retired rule's double bit for
    bit."""
    if c <= 0.04045: return c / 12.92
    x2 = ((c + 0.055) / 1.055) * ((c + 0.055) / 1.055)
    y = 1.0
    for _ in range(64): y = (4.0 * y + x2 / (y * y * y * y)) / 5.0
    return x2 * y

def relative_luminance(rgb):
    """render.h relative_luminance as it stood until 2026-10-03: the Rec. 709 weights over the sRGB-linearized
    channels of the BYTES as given (a theme byte is a P3 byte and the app's constant alike; no conversion), each
    channel byte / 255."""
    r, g, b = (v / 255.0 for v in rgb)
    return 0.2126 * srgb_channel_to_linear(r) + 0.7152 * srgb_channel_to_linear(g) + 0.0722 * srgb_channel_to_linear(b)

# render.h kHighlightTextLuminanceThreshold as it stood until 2026-10-03: the equal-contrast point, where black and
# the label white stand at the same contrast ratio over a fill (L = sqrt(1.05 x 0.05) - 0.05).
LUMINANCE_THRESHOLD = 0.17912878474779

def highlight_text_ink(fill, light):
    """render.h highlight_text_ink as it stood until 2026-10-03, kept here as the renderer's legacy option for old mock
    sets: the chrome's text over a fill was black (kRedesignHighlightLabel) when the fill's luminance exceeded the
    threshold, else the LIGHT ink `light` (the app's kRedesignLabel, the label white). Its domain was the chrome's
    text and glyphs; the flags' black label was always its own rule. THE APP NO LONGER CALLS THIS: since 2026-10-03
    its text over a fill is the theme's own recorded pair (render.h)."""
    return (0, 0, 0) if relative_luminance(fill) > LUMINANCE_THRESHOLD else tuple(light)

# THE CHECKS (that retired rule's own static_asserts, kept to verify this legacy arithmetic still matches it; run at
# import):
assert highlight_text_ink((0x00, 0x00, 0x80), (252, 252, 252)) == (252, 252, 252)    # Windows' highlight #000080: the label
assert highlight_text_ink((0xC0, 0xC0, 0xC0), (252, 252, 252)) == (0, 0, 0)          # Windows 95's face #C0C0C0: black
assert highlight_text_ink((0xFF, 0xFF, 0xE1), (252, 252, 252)) == (0, 0, 0)          # INFO #FFFFE1: black
