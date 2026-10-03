#!/usr/bin/env python3
# tools/theme_catalog/toolkit_rules.py — THE TOOLKITS' OWN RULES, run once at import (architect 2026-10-03: a theme
# is drawn as its own desktop drew it; where the source records only base colours and its toolkit computed the
# rest at run time, the extractor runs THAT toolkit's rule and the catalog stores the bytes, the rule named in the
# entry's provenance — part of the import, not a derivation of ours). Two toolkits, each ported integer for integer
# from the pinned source named at its function (sources.py: motif_rules, tde_rules, tqt_rules):
#   motif_colors  — Motif's CalculateColorsRGB (CDE's foreground, select colour and shadows);
#   kde3_palette  — KDE 3's createApplicationPalette over Qt 3's integer HSV (TDE's KDE 3 schemes).
# The asserts at the bottom are the research's checked values (tmp/win_derivation/FINDINGS.md), run at import.


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


def motif_pair_8bit(bg8):
    """Motif's (ts, bs) for an 8-bit colour given as X would parse #rrggbb (each byte replicated: v x 257)."""
    m, branch = motif_colors(tuple(v * 257 for v in bg8))
    return x_to_8bit(m['ts']), x_to_8bit(m['bs']), branch


# THE CHECKS (the research's table, FINDINGS.md), run at import
assert kde3_quartet((0x30, 0x30, 0x30)) == ((0x3D,) * 3, (0x34,) * 3, (0x11,) * 3, (0, 0, 0))   # KDE 3 at 7 on 303030
assert motif_pair_8bit((0x30, 0x30, 0x30)) == ((0x98,) * 3, (0x6E,) * 3, 'dark')             # Motif, dark branch
assert motif_pair_8bit((0x41, 0x52, 0x5C)) == ((0xA6, 0xAE, 0xB3), (0x1E, 0x25, 0x2A), 'medium')   # Northern Sky's ground
