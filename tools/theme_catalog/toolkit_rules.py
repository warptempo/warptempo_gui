#!/usr/bin/env python3
# tools/theme_catalog/toolkit_rules.py — THE TOOLKITS' OWN RULES, run once at import (architect 2026-10-03: a theme
# is drawn as its own desktop drew it; where the source records only base colours and its toolkit computed the
# rest at run time, the extractor runs THAT toolkit's rule and the catalog stores the bytes, the rule named in the
# entry's provenance — part of the import, not a derivation of ours). Four toolkits, each ported from the pinned
# source named at its function (sources.py: motif_rules, tde_rules, tqt_rules, wine_rules, gtk2_rules), integer for
# integer where the source is integer, double for double where it is double:
#   motif_colors   — Motif's CalculateColorsRGB (CDE's foreground, select colour and shadows);
#   kde3_palette   — KDE 3's createApplicationPalette over Qt 3's integer HSV (TDE's KDE 3 schemes);
#   windows_dialog — Windows' Appearance dialog over shlwapi's 240-scale integer HLS;
#   gtk2_shade     — GTK 2's shade (the Clearlooks engine's ge_shade_color, GTK's gtk_style_shade, metacity's
#                    copy of it) in doubles, with gtk2_mix and metacity_blend, and the two roads a double becomes a
#                    screen byte (cairo_byte: the engine through cairo 1.8 and pixman; gdk16 / gdk_byte: a GdkColor).
# THE FLAGS' BEVEL (architect 2026-10-03, late): the waveform pane and the flags are the program's own elements, their
# base colours the program's and their SHADING the theme's, so a flag's one-line bevel is its theme family's own rule
# applied to the flag's face, as that desktop would have shaded a 3D face of that colour (flag_bevel; the catalog
# names each entry's rule in its `flag_rule`). These rules also run at render time on the app's face colours.
# The asserts at the bottom are the research's checked values (tmp/win_derivation/FINDINGS.md), run at import.

import math


def _cdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


# ------------------------------------------------------------------ Motif (lib/Xm/Color.c, ColorP.h, Xm.h.in)
XM_MAX_SHORT = 65535                       # ColorP.h XmMAX_SHORT
XM_PERCENTILE = XM_MAX_SHORT // 100        # ColorP.h XmCOLOR_PERCENTILE (655)
XM_DARK_THRESHOLD = 20 * XM_PERCENTILE     # Xm.h.in XmDEFAULT_DARK_THRESHOLD 20 (XmScreen darkThreshold unset)
XM_LITE_THRESHOLD = 93 * XM_PERCENTILE     # Xm.h.in XmDEFAULT_LIGHT_THRESHOLD 93
XM_FG_THRESHOLD = 70 * XM_PERCENTILE       # Xm.h.in XmDEFAULT_FOREGROUND_THRESHOLD 70


def motif_brightness(c):
    """Color.c Brightness: (intensity x 75 + light x 0 + luminosity x 25) / 100 over 16-bit channels, the
    luminosity 0.30 R + 0.59 G + 0.11 B in double (the constants are double literals) truncated to int."""
    r, g, b = c
    intensity = (r + g + b) // 3
    luminosity = int(0.30 * float(r) + 0.59 * float(g) + 0.11 * float(b))
    light = (min(c) + max(c)) // 2
    return (intensity * 75 + light * 0 + luminosity * 25) // 100


def motif_colors(bg):
    """Color.c CalculateColorsRGB on a 16-bit background -> ({'fg', 'sel', 'ts', 'bs'} as 16-bit triples, branch).
    Three models by brightness: DARK below 20 % (sel, bs and ts lifted toward white by 15 / 30 / 50 % of the
    headroom), LIGHT above 93 % (each lowered by 15 / 40 / 20 % of itself), else MEDIUM (the factors interpolated by
    brightness: sel 15, bs 60 -> 40 down, ts 50 -> 60 up). The foreground is black above the 70 % foreground
    threshold, else white (Fix 4602). CDE's dtsession calls it through XmGetColorCalculation for each colour set's
    background (SrvFile_io.c ParsePaletteInfo). Motif draws TWO shadow colours: a 2-px shadow paints both lines of a
    side in one colour, so the Windows quartet of a Motif edge is (ts, ts, bs, bs)."""
    br = motif_brightness(bg)
    M = XM_MAX_SHORT
    fg = (0, 0, 0) if br > XM_FG_THRESHOLD else (M, M, M)
    if br < XM_DARK_THRESHOLD:
        up = lambda f: tuple(v + _cdiv(f * (M - v), 100) for v in bg)
        return {'fg': fg, 'sel': up(15), 'bs': up(30), 'ts': up(50)}, 'dark'
    if br > XM_LITE_THRESHOLD:
        down = lambda f: tuple(v - _cdiv(v * f, 100) for v in bg)
        return {'fg': fg, 'sel': down(15), 'bs': down(40), 'ts': down(20)}, 'light'
    f_sel = 15 + _cdiv(br * (15 - 15), M)
    f_bs = 60 + _cdiv(br * (40 - 60), M)
    f_ts = 50 + _cdiv(br * (60 - 50), M)
    return {'fg': fg,
            'sel': tuple(v - _cdiv(v * f_sel, 100) for v in bg),
            'bs': tuple(v - _cdiv(v * f_bs, 100) for v in bg),
            'ts': tuple(v + _cdiv(f_ts * (M - v), 100) for v in bg)}, 'medium'


def x_to_8bit(c):
    """A 16-bit X colour as a 24-bit TrueColor visual stores it: the top byte of each channel (the X server's
    TrueColor resolve keeps the high bits)."""
    return tuple(v >> 8 for v in c)


# ------------------------------------------------------------------ Qt 3 (TQt src/kernel/tqcolor.cpp)
def qt_hsv(c):
    """tqcolor.cpp TQColor::hsv: integer HSV, hue -1 when achromatic."""
    r, g, b = c
    mx, whatmax = r, 0
    if g > mx: mx, whatmax = g, 1
    if b > mx: mx, whatmax = b, 2
    mn = min(r, g, b)
    delta = mx - mn
    v = mx
    s = (510 * delta + mx) // (2 * mx) if mx else 0
    if s == 0: return -1, 0, v
    if whatmax == 0:
        h = (120 * (g - b) + delta) // (2 * delta) if g >= b else (120 * (g - b + delta) + delta) // (2 * delta) + 300
    elif whatmax == 1:
        h = 120 + (120 * (b - r) + delta) // (2 * delta) if b > r else 60 + (120 * (b - r + delta) + delta) // (2 * delta)
    else:
        h = 240 + (120 * (r - g) + delta) // (2 * delta) if r > g else 180 + (120 * (r - g + delta) + delta) // (2 * delta)
    return h, s, v


def qt_set_hsv(h, s, v):
    """tqcolor.cpp TQColor::setHsv (its divisions are on non-negative values, so Python's floor is C's)."""
    if s == 0 or h == -1: return v, v, v
    if h >= 360: h %= 360
    f = h % 60; h //= 60
    p = (2 * v * (255 - s) + 255) // 510
    if h & 1:
        q = (2 * v * (15300 - s * f) + 15300) // 30600
        return {1: (q, v, p), 3: (p, q, v), 5: (v, p, q)}[h]
    t = (2 * v * (15300 - s * (60 - f)) + 15300) // 30600
    return {0: (v, t, p), 2: (p, v, t), 4: (t, p, v)}[h]


def qt_light(c, factor):
    """tqcolor.cpp TQColor::light: V x factor / 100, an overflow past 255 taken out of the saturation."""
    if factor <= 0: return tuple(c)
    if factor < 100: return qt_dark(c, 10000 // factor)
    h, s, v = qt_hsv(c)
    v = factor * v // 100
    if v > 255:
        s = max(0, s - (v - 255)); v = 255
    return qt_set_hsv(h, s, v)


def qt_dark(c, factor):
    """tqcolor.cpp TQColor::dark: V x 100 / factor."""
    if factor <= 0: return tuple(c)
    if factor < 100: return qt_light(c, 10000 // factor)
    h, s, v = qt_hsv(c)
    return qt_set_hsv(h, s, v * 100 // factor)


QT_BLACK = (0, 0, 0)
QT_DARK_GRAY = (128, 128, 128)   # tqcolor.cpp stdcol: TQt::darkGray


# ------------------------------------------------------------------ KDE 3 (TDE tdelibs tdecore/tdeapplication.cpp)
KDE3_DEFAULT_CONTRAST = 7        # tdeglobalsettings.cpp TDEGlobalSettings::contrast: readNumEntry("contrast", 7)


def kde3_palette(background, foreground, contrast=KDE3_DEFAULT_CONTRAST):
    """tdeapplication.cpp createApplicationPalette at the scheme's contrast -> the active group's relief and the
    disabled foreground, 8-bit triples:
      highlightVal = 100 + (2c + 4) x 16 / 10, lowlightVal = 100 + (2c + 4) x 10;
      light = background.light(highlightVal), midlight = background.light(110), dark = background.dark(lowlightVal),
      mid = background.dark(120), shadow = black (tqpalette.cpp: the seven-colour TQColorGroup constructor);
      the disabled foreground: foreground.dark(lowlightVal) when its V > 128, foreground.light(highlightVal) when it
      is another non-black colour, darkGray when it is black.
    Qt's Windows bevel (qDrawWinButton, raised) draws light / shadow outside and midlight / dark inside, so the
    Windows quartet is (light, midlight, dark, shadow): Hilight, 3DLight, Shadow, DkShadow."""
    hv = 100 + (2 * contrast + 4) * 16 // 10
    lv = 100 + (2 * contrast + 4) * 10
    v = qt_hsv(foreground)[2]
    if v > 128: dis = qt_dark(foreground, lv)
    elif tuple(foreground) != QT_BLACK: dis = qt_light(foreground, hv)
    else: dis = QT_DARK_GRAY
    return {'light': qt_light(background, hv), 'midlight': qt_light(background, 110),
            'dark': qt_dark(background, lv), 'mid': qt_dark(background, 120), 'shadow': QT_BLACK,
            'disabled_foreground': tuple(dis)}


def kde3_quartet(background, contrast=KDE3_DEFAULT_CONTRAST):
    p = kde3_palette(background, (0, 0, 0), contrast)
    return p['light'], p['midlight'], p['dark'], p['shadow']


# ------------------------------------------------------------------ Windows (Wine dlls/shlwapi/ordinal.c)
def win_rgb_to_hls(c):
    """ordinal.c ColorRGBToHLS: 8-bit RGB -> (hue, lightness, saturation) on Windows' 240 scale, integer for integer
    (its divisions are on non-negative values, so Python's floor is C's); an achromatic colour's hue is 160, as
    native returns."""
    r, g, b = c
    mx, mn = max(r, g, b), min(r, g, b)
    L = ((mx + mn) * 240 + 255) // 510
    if mx == mn: return 160, L, 0
    d = mx - mn
    S = ((mx + mn) // 2 + d * 240) // (mx + mn) if L <= 120 else ((510 - mx - mn) // 2 + d * 240) // (510 - mx - mn)
    rn = (d // 2 + mx * 40 - r * 40) // d; gn = (d // 2 + mx * 40 - g * 40) // d; bn = (d // 2 + mx * 40 - b * 40) // d
    H = bn - gn if r == mx else (80 + rn - bn if g == mx else 160 + gn - rn)
    if H < 0: H += 240
    elif H > 240: H -= 240
    return H, L, S


def _win_convert_hue(h, m1, m2):
    """ordinal.c ConvertHue."""
    h = h - 240 if h > 240 else (h + 240 if h < 0 else h)
    if h > 160: return m1
    elif h > 120: h = 160 - h
    elif h > 40: return m2
    return (h * (m2 - m1) + 20) // 40 + m1


def win_hls_to_rgb(H, L, S):
    """ordinal.c ColorHLSToRGB (with its GET_RGB scaling, (v x 255 + 120) / 240)."""
    if S:
        m2 = S + L - (S * L + 120) // 240 if L > 120 else ((S + 240) * L + 120) // 240
        m1 = L * 2 - m2
        f = lambda h: (_win_convert_hue(h, m1, m2) * 255 + 120) // 240
        return f(H + 80), f(H), f(H - 80)
    v = L * 255 // 240
    return v, v, v


def windows_dialog(face):
    """Windows' Appearance dialog on a picked 3D face -> the quartet (Hilight, 3DLight, Shadow, DkShadow), 8-bit
    triples: in HLS on the 240 scale, Hilight = the lightness halfway to white with the half rounded up, Shadow = two
    thirds of the lightness floored, hue and saturation kept; 3DLight = the face; DkShadow black. THE RULE is the
    dialog's behaviour as measured (the research of 2026-10-03): byte-exact on the stock schemes Brick, Green Olive,
    Lilac, Maple, Pumpkin, Rainy Day, Rose and Sand and on the XP / 7 dialog's measured D4D0C8 -> EAE8E3 (VirtualDub's
    note); THE ARITHMETIC is shlwapi's ColorRGBToHLS / ColorHLSToRGB as Wine implements them (sources.py wine_rules:
    dlls/shlwapi/ordinal.c at wine-10.0), the conversions the dialog runs through."""
    H, L, S = win_rgb_to_hls(face)
    return win_hls_to_rgb(H, L + (240 - L + 1) // 2, S), tuple(face), win_hls_to_rgb(H, (2 * L) // 3, S), (0, 0, 0)


# ------------------------------------------------------------------ GTK 2 (gtk-engines 2.20.2, GTK 2, metacity 2.30)
def gtk2_hsb(c):
    """engines/support/cairo-support.c ge_hsb_from_color, doubles in [0, 1] -> (hue in degrees, saturation,
    lightness): HLS with lightness (max + min) / 2; a grey (max - min < 0.0001) has hue and saturation 0."""
    r, g, b = c
    if r > g: mx, mn = max(r, b), min(g, b)
    else:     mx, mn = max(g, b), min(r, b)
    l = (mx + mn) / 2
    if abs(mx - mn) < 0.0001: return 0.0, 0.0, l
    s = (mx - mn) / (mx + mn) if l <= 0.5 else (mx - mn) / (2 - mx - mn)
    d = mx - mn
    if r == mx: h = (g - b) / d
    elif g == mx: h = 2 + (b - r) / d
    else: h = 4 + (r - g) / d
    h *= 60
    if h < 0.0: h += 360
    return h, s, l


def _modula(n, d):
    """cairo-support.c MODULA: ((gint) n % d) + (n - (gint) n), C's truncating cast and remainder."""
    i = int(n)
    return (abs(i) % d) * (1 if i >= 0 else -1) + (n - i)


def gtk2_rgb(h, s, l):
    """cairo-support.c ge_color_from_hsb -> doubles (r, g, b); a saturation of 0 is the grey l."""
    m2 = l * (1 + s) if l <= 0.5 else l + s - l * s
    m1 = 2 * l - m2
    out = [l, l, l]
    for i, m3 in enumerate((h + 120, h, h - 120) if s != 0 else ()):
        if m3 > 360: m3 = _modula(m3, 360)
        elif m3 < 0: m3 = 360 - _modula(abs(m3), 360)
        if m3 < 60: out[i] = m1 + (m2 - m1) * m3 / 60
        elif m3 < 180: out[i] = m2
        elif m3 < 240: out[i] = m1 + (m2 - m1) * (240 - m3) / 60
        else: out[i] = m1
    return tuple(out)


def gtk2_unit(c8):
    """An 8-bit colour as GTK holds it: gtkrc's "#rrggbb" is a GdkColor of 16-bit channels (each byte replicated,
    v x 257), which the engine reads as v / 65535.0 (ge_gdk_color_to_cairo) -- exactly the byte over 255."""
    return tuple(v / 255 for v in c8)


def gtk2_shade(c, k):
    """cairo-support.c ge_shade_color (= GTK 2's gtk_style_shade, gtkstyle.c, and metacity's copy of it, theme.c):
    a colour of doubles -> the lightness and the saturation both multiplied by k, each clamped to [0, 1], hue kept;
    k = 1.0 returns the colour unchanged."""
    if k == 1.0: return tuple(c)
    h, s, l = gtk2_hsb(c)
    return gtk2_rgb(h, max(0.0, min(s * k, 1.0)), max(0.0, min(l * k, 1.0)))


def gtk2_mix(c1, c2, f):
    """cairo-support.c ge_mix_color: c1 x (1 - f) + c2 x f, channel by channel (gtkrc's mix (f, a, b) is a x f +
    b x (1 - f), so it is gtk2_mix(b, a, f))."""
    return tuple(a * (1 - f) + b * f for a, b in zip(c1, c2))


def cairo_byte(c):
    """A colour of doubles -> the 8-bit pixel the Clearlooks engine paints it as: cairo 1.8 (squeeze's libcairo2
    1.8.10) turns a source channel into 16 bits as d x 65536 truncated (cairo-color.c _cairo_color_double_to_short,
    capped at 65535) and pixman 0.16 keeps the top byte of a solid colour, so floor(256 d) capped at 255. The
    capture is the check: shade(#F5F5B5, 0.6) is #BABA45 on his screen (rounding would say #BABA46), the engine's
    one-line pair #FBFBFA / #E0DEDD (rounding: #FAFAFA / #DFDEDC)."""
    return tuple(min(255, int(v * 65536) >> 8) for v in c)


def gdk16(c):
    """A colour of doubles -> the GdkColor GTK's own gtk_style_shade and metacity store: each channel x 65535.0
    truncated to 16 bits (gtkstyle.c / theme.c: b->red = red * 65535.0)."""
    return tuple(int(v * 65535.0) for v in c)


def gdk_color(c8):
    """gtkrc's "#rrggbb" as GTK parses it: a GdkColor, each byte replicated into 16 bits (v x 257)."""
    return tuple(v * 257 for v in c8)


def gdk_byte(c16):
    """A GdkColor -> its 8-bit pixel on a 24-bit TrueColor visual: the top byte of each channel."""
    return tuple(v >> 8 for v in c16)


def metacity_blend(bg16, fg16, alpha):
    """metacity 2.30 theme.c color_composite, the "blend/<background>/<foreground>/<alpha>" colour of a theme (the
    FIRST colour is the background, theme.c meta_color_spec_new_from_string): the alpha as a 16-bit integer
    (alpha x 0xffff truncated), each channel bg + (((fg - bg) x alpha + 0x8000) >> 16) on GdkColors."""
    a = int(alpha * 0xffff)
    return tuple(b + (((f - b) * a + 0x8000) >> 16) for b, f in zip(bg16, fg16))



# ------------------------------------------------------------------ the two renderers' arithmetic (the Clearlooks tones)
# The engine's colours reach the screen through cairo 1.8.10 over pixman 0.16.4 (squeeze's libcairo2 and
# libpixman-1-0); metacity's through GDK on a 24-bit visual and its own gradient code. These are THEIR integer roads,
# ported so a ramp's painted rows and a translucent stroke's composite are recorded as the screen showed them
# (build.py's Clearlooks tones). Checked against his squeeze captures at the bottom of this file: the toolbar band's
# 38 ramp rows, the menu bar's 24 and the open menu title's 22, byte for byte.
def _cairo_short(d):
    """cairo 1.8 _cairo_color_double_to_short: d x (65536 - 1e-5), truncated."""
    return int(d * (65536.0 - 1e-5))


def _fixed_16_16(d):
    """cairo 1.8 _cairo_fixed_16_16_from_double: the nearest 16.16 fixed value."""
    return int(math.floor(d * 65536.0 + 0.5))


def pixman_vertical_ramp_row(stops, y0, y1, row):
    """A vertical cairo linear gradient from user y0 to y1 (whole px; cairo_pattern_create_linear (x, y0, x, y1)) with
    `stops` [(offset, (r, g, b) doubles)], as pixman 0.16 samples it at pixel row `row`'s centre -> the 8-bit pixel
    (pixman-linear-gradient.c: t = ((b x v) >> 16) + off with b = 2^32 / dy, the walker's 8-bit interpolation with
    stepper ((1 << 24) + w / 2) / w and the PAD extend; the stops' channels cairo's 16-bit shorts' top bytes,
    unpremultiplied). Opaque stops only (alpha 1)."""
    dy = (y1 - y0) * 65536
    b = (1 << 32) // dy
    off = (-b * (y0 * 65536)) >> 16
    t = ((b * (row * 65536 + 32768)) >> 16) + off
    st = [(_fixed_16_16(o), tuple(_cairo_short(v) >> 8 for v in c)) for o, c in stops]
    n = 0
    while n < len(st) and not t < st[n][0]: n += 1
    lx, lc = (-2 ** 31, st[0][1]) if n == 0 else st[n - 1]
    rx, rc = (2 ** 31 - 1, st[-1][1]) if n == len(st) else st[n]
    stepper = 0 if (lx == rx or lc == rc) else ((1 << 24) + (rx - lx) // 2) // (rx - lx)
    dist = ((t - lx) * stepper) >> 16
    return tuple((l * (256 - dist) + r * dist) >> 8 for l, r in zip(lc, rc))


def pixman_vertical_t(y0, y1, row):
    """The 16.16 gradient position pixman 0.16 gives pixel row `row` of a vertical gradient from y0 to y1 (whole px)."""
    dy = (y1 - y0) * 65536
    b = (1 << 32) // dy
    return ((b * (row * 65536 + 32768)) >> 16) + ((-b * (y0 * 65536)) >> 16)


def pixman_step_row(y0, y1):
    """THE ROW WHERE A RAMP'S STEP AT 0.5 FALLS (the gummy ramps' two stops at 0.5): the first row of [y0, y1) whose
    position is at or past one half -- y0 + (n + 1) / 2 in integers for every height the chrome draws (the truncated
    b puts an odd ramp's middle row, exactly at one half, above the step)."""
    return next(r for r in range(y0, y1) if pixman_vertical_t(y0, y1, r) >= 32768)


def pixman_alpha_ramp_alpha(a0, y0, y1, row):
    """The alpha byte pixman 0.16 gives row `row` of a gradient from alpha a0 at y0 to alpha 0 at y1 (the gummy
    button's pressed shadow, clearlooks_draw_gummy.c: 0.58 -> 0 over three px), the colour the same at both stops."""
    dy = (y1 - y0) * 65536
    b = (1 << 32) // dy
    off = (-b * (y0 * 65536)) >> 16
    t = ((b * (row * 65536 + 32768)) >> 16) + off
    left = _cairo_short(a0) >> 8
    stepper = ((1 << 24) + 65536 // 2) // 65536
    dist = min(256, max(0, (max(0, t) * stepper) >> 16))
    return (left * (256 - dist)) >> 8


def _mul_un8(a, b):
    t = a * b + 0x80
    return ((t >> 8) + t) >> 8


def pixman_over(src8, alpha8, dst8):
    """pixman's OVER of an UNPREMULTIPLIED source (8-bit colour, 8-bit alpha) onto an opaque 8-bit pixel: the source
    premultiplied by the walker's rounding (x a / 255, rounded), then d x (255 - a) / 255 + s, saturating."""
    return tuple(min(255, _mul_un8(d, 255 - alpha8) + _mul_un8(s, alpha8)) for s, d in zip(src8, dst8))


def cairo_solid_over(c, alpha, dst8):
    """A solid cairo source (r, g, b) doubles at `alpha` stroked or filled at full coverage onto an opaque 8-bit pixel
    (cairo 1.8: the solid's shorts premultiplied, their top bytes, then pixman's OVER) -> the 8-bit pixel."""
    a8 = _cairo_short(alpha) >> 8
    s8 = tuple(_cairo_short(v * alpha) >> 8 for v in c)
    return tuple(min(255, _mul_un8(d, 255 - a8) + s) for s, d in zip(s8, dst8))


def metacity_vertical_gradient_rows(c0_16, c1_16, height):
    """metacity 2.30 gradient.c meta_gradient_create_vertical (a two-colour <gradient type="vertical">): the GdkColors'
    top bytes, stepped in 16.16 fixed point, C's truncating division -> the `height` rows' 8-bit pixels (the end colour
    itself is never reached)."""
    r0 = [v >> 8 for v in c0_16]
    rf = [v >> 8 for v in c1_16]
    acc = [v << 16 for v in r0]
    d = [int(((f - s) << 16) / height) for s, f in zip(r0, rf)]
    out = []
    for _ in range(height):
        out.append(tuple((a >> 16) & 255 for a in acc))
        acc = [a + x for a, x in zip(acc, d)]
    return out

# ------------------------------------------------------------------ the flags' bevel
FLAG_RULES = ('windows-dialog', 'kde3', 'motif', 'flat')


def flag_bevel(rule, face):
    """A flag's ONE-LINE BEVEL (architect 2026-10-03, late): (light, dark) for a 3D face of colour `face` (8-bit) by
    its theme family's own rule, `rule` being a catalog entry's flag_rule — {"id": "windows-dialog"} (the families
    windows, windows-plus and warptempo: windows_dialog's Hilight and Shadow, Windows' BDR_RAISEDINNER pair),
    {"id": "kde3", "contrast": c} (kde3_palette's light and dark at the scheme's contrast), {"id": "motif"} (Motif's
    top and bottom shadow, motif_pair_8bit), {"id": "flat"} (the family gnome2: no bevel, the face on both sides --
    Clearlooks draws no one-line bevel round a raised face: clearlooks_gummy_draw_button, clearlooks_draw_gummy.c, fills
    the face with a four-stop ramp inside a 1-px border that is a MIX of the theme's shade[6] and the face
    (clearlooks_set_mixed_color, 0.2) with a translucent highlight along the top and left only, so no light / dark
    pair of the face exists to record). A malformed rule is a one-line fail."""
    rid = rule.get('id') if isinstance(rule, dict) else None
    if rid == 'windows-dialog' and set(rule) == {'id'}:
        q = windows_dialog(face); return q[0], q[2]
    if rid == 'kde3' and set(rule) == {'id', 'contrast'} and isinstance(rule['contrast'], int):
        p = kde3_palette(face, QT_BLACK, rule['contrast']); return p['light'], p['dark']
    if rid == 'motif' and set(rule) == {'id'}:
        ts, bs, _ = motif_pair_8bit(face); return ts, bs
    if rid == 'flat' and set(rule) == {'id'}:
        return tuple(face), tuple(face)
    raise SystemExit(f'flag rule {rule!r}: one of {{"id": "windows-dialog"}}, {{"id": "kde3", "contrast": c}}, '
                     f'{{"id": "motif"}}, {{"id": "flat"}}')


def motif_pair_8bit(bg8):
    """Motif's (ts, bs) for an 8-bit colour given as X would parse #rrggbb (each byte replicated: v x 257)."""
    m, branch = motif_colors(tuple(v * 257 for v in bg8))
    return x_to_8bit(m['ts']), x_to_8bit(m['bs']), branch


# THE CHECKS (the research's table, FINDINGS.md), run at import
assert kde3_quartet((0x30, 0x30, 0x30)) == ((0x3D,) * 3, (0x34,) * 3, (0x11,) * 3, (0, 0, 0))   # KDE 3 at 7 on 303030
assert motif_pair_8bit((0x30, 0x30, 0x30)) == ((0x98,) * 3, (0x6E,) * 3, 'dark')             # Motif, dark branch
assert motif_pair_8bit((0x41, 0x52, 0x5C)) == ((0xA6, 0xAE, 0xB3), (0x1E, 0x25, 0x2A), 'medium')   # Northern Sky's ground
assert windows_dialog((0xD4, 0xD0, 0xC8))[0] == (0xEA, 0xE8, 0xE3)          # the XP / 7 dialog's measured Hilight
assert windows_dialog((0x83, 0x99, 0xB1)) == ((0xC1, 0xCC, 0xD9), (0x83, 0x99, 0xB1), (0x4F, 0x65, 0x7D), (0, 0, 0))   # Rainy Day
# GTK 2 / Clearlooks (gtk-engines 2.20.2; tmp research CL1's port, checked against his squeeze captures): the engine's
# shade table at realize, shade[3] of #EDECEB = #C4C2BF and spot[1] of #86ABD9 = #92B4DF, both under either byte road
_cl = lambda h, k: cairo_byte(gtk2_shade(gtk2_unit(tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))), k))
assert _cl('EDECEB', 0.82) == (0xC4, 0xC2, 0xBF) and _cl('86ABD9', 1.05) == (0x92, 0xB4, 0xDF)
assert _cl('F5F5B5', 0.6) == (0xBA, 0xBA, 0x45)                                              # the tooltip's border, as captured
assert (_cl('EDECEB', 1.06), _cl('EDECEB', 0.94)) == ((0xFB, 0xFB, 0xFA), (0xE0, 0xDE, 0xDD))   # the inset pair, as captured
assert gdk_byte(metacity_blend(gdk_color((0, 0, 0)), gdk_color((0xED, 0xEC, 0xEB)), 0.45)) == (0x6B, 0x6A, 0x6A)   # metacity's unfocused title
del _cl
