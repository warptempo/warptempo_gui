#!/usr/bin/env python3
# tools/palette/common.py: paths, the font road, HarfBuzz shaping, colour parsing, PNG I/O. Every path resolves from
# this file, so the tool runs from any working directory.
#
# IMPORT THIS BEFORE `import cairo` ANYWHERE: it points fontconfig at the tool's own fonts.conf,
# which lists ONLY the repository's fonts/ directory, so cairo's toy face "Liberation Sans" can
# resolve to nothing but fonts/LiberationSans-Regular.ttf — the app's FALLBACK face (gui_font.h,
# architect 2026-10-05), the one this tool paints every row in: the tablet's 275 % is a fallback
# scale. Its VERTICAL metrics are the period bitmap faces' (strike_metrics below), as in the app.
# verify_fonts() proves the resolution (fontconfig's own match, glyph ids against HarfBuzz on the
# file, and the face's metrics as measured 2026-10-05) and the renderer calls it on every run.
import os, sys, ctypes

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
FONTS = os.path.join(REPO, 'fonts')
SANS_FILE = os.path.join(FONTS, 'LiberationSans-Regular.ttf')
W, H = 2304, 1440
SCALE = 2                      # gui_scale 200 %: one logical px = 2 device px

_CONF = os.path.join(HERE, 'fonts.conf')
def _write_fonts_conf():
    txt = ('<?xml version="1.0"?>\n<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">\n<fontconfig>\n'
           f'  <dir>{FONTS}</dir>\n  <cachedir>{os.path.join(HERE, "fccache")}</cachedir>\n</fontconfig>\n')
    if not os.path.exists(_CONF) or open(_CONF).read() != txt:
        open(_CONF, 'w').write(txt)
_write_fonts_conf()
os.environ['FONTCONFIG_FILE'] = _CONF
if 'cairo' in sys.modules:
    raise RuntimeError('common.py must be imported before cairo (FONTCONFIG_FILE)')
import cairo                    # noqa: E402
import numpy as np              # noqa: E402

if HERE not in sys.path: sys.path.insert(0, HERE)
from pngrw import read_rgb, write_png                     # noqa: E402
from colour import srgb_to_p3, lin_mix, two_pass, relative_luminance, highlight_text_ink, LUMINANCE_THRESHOLD  # noqa: E402

# display_p3.iccp: the raw iCCP chunk data (299 bytes, the 'Skia' Display-P3 profile) of a Samsung Gallery screenshot
ICCP = open(os.path.join(HERE, 'display_p3.iccp'), 'rb').read()
def save_png(path, arr):
    """THE ONLY WRITER: IHDR + the Display-P3 iCCP chunk + IDAT + IEND (the output's directory made when missing)."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    write_png(path, arr, [(b'iCCP', ICCP)])

# ------------------------------------------------------------------ fonts: cairo side
SANS = 'Liberation Sans'
def font_options():
    o = cairo.FontOptions()
    o.set_antialias(cairo.ANTIALIAS_GRAY)          # the app: cairo's default GRAY, no subpixel order
    o.set_hint_style(cairo.HINT_STYLE_SLIGHT)      # gui_font_bundled.cpp: SLIGHT on both devices
    o.set_hint_metrics(cairo.HINT_METRICS_ON)      # image surfaces' default, every extent a whole px
    return o

def set_font(cr, family, size_px):
    cr.select_font_face(family, cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
    cr.set_font_size(size_px)
    cr.set_font_options(font_options())
    return cr.get_scaled_font()

# ------------------------------------------------------------------ fonts: HarfBuzz via ctypes
# The app shapes with hb-ft (unhinted advances, GPOS kerning) and paints with cairo_show_glyphs;
# here hb-ot on the same file at scale size*64 does the same shaping (advances agree to 1/64 px).
try: _hb = ctypes.CDLL('libharfbuzz.so.0')
except OSError as e:      # no fallback shaper: the text must be shaped exactly as the app shapes it
    raise SystemExit(f'tools/palette needs HarfBuzz (libharfbuzz.so.0) for text shaping: {e}')
class _HbInfo(ctypes.Structure):
    _fields_ = [('codepoint', ctypes.c_uint32), ('mask', ctypes.c_uint32), ('cluster', ctypes.c_uint32),
                ('var1', ctypes.c_uint32), ('var2', ctypes.c_uint32)]
class _HbPos(ctypes.Structure):
    _fields_ = [('x_advance', ctypes.c_int32), ('y_advance', ctypes.c_int32), ('x_offset', ctypes.c_int32),
                ('y_offset', ctypes.c_int32), ('var', ctypes.c_uint32)]
_hb.hb_blob_create_from_file.restype = ctypes.c_void_p; _hb.hb_blob_create_from_file.argtypes = [ctypes.c_char_p]
_hb.hb_face_create.restype = ctypes.c_void_p; _hb.hb_face_create.argtypes = [ctypes.c_void_p, ctypes.c_uint]
_hb.hb_font_create.restype = ctypes.c_void_p; _hb.hb_font_create.argtypes = [ctypes.c_void_p]
_hb.hb_font_set_scale.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
_hb.hb_buffer_create.restype = ctypes.c_void_p
_hb.hb_buffer_destroy.argtypes = [ctypes.c_void_p]
_hb.hb_buffer_add_utf8.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_uint, ctypes.c_int]
_hb.hb_buffer_set_direction.argtypes = [ctypes.c_void_p, ctypes.c_int]
_hb.hb_buffer_guess_segment_properties.argtypes = [ctypes.c_void_p]
_hb.hb_shape.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
_hb.hb_buffer_get_glyph_infos.restype = ctypes.POINTER(_HbInfo)
_hb.hb_buffer_get_glyph_infos.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]
_hb.hb_buffer_get_glyph_positions.restype = ctypes.POINTER(_HbPos)
_hb.hb_buffer_get_glyph_positions.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]
_HB_FACES = {}
def _hb_face(path):
    if path not in _HB_FACES:
        _HB_FACES[path] = _hb.hb_face_create(_hb.hb_blob_create_from_file(path.encode()), 0)
    return _HB_FACES[path]

def shape(family, size_px, text):
    """-> (glyphs [(index, x_offset, y_offset, x_advance)], width_px) exactly like text_shape::shape_text_run."""
    font = _hb.hb_font_create(_hb_face(SANS_FILE))
    sc = int(round(size_px * 64)); _hb.hb_font_set_scale(font, sc, sc)
    buf = _hb.hb_buffer_create(); b = text.encode('utf-8')
    _hb.hb_buffer_add_utf8(buf, b, len(b), 0, len(b)); _hb.hb_buffer_set_direction(buf, 4)   # HB_DIRECTION_LTR
    _hb.hb_buffer_guess_segment_properties(buf); _hb.hb_shape(font, buf, None, 0)
    n = ctypes.c_uint(0); inf = _hb.hb_buffer_get_glyph_infos(buf, ctypes.byref(n)); pos = _hb.hb_buffer_get_glyph_positions(buf, ctypes.byref(n))
    out = [(inf[i].codepoint, pos[i].x_offset / 64.0, pos[i].y_offset / 64.0, pos[i].x_advance / 64.0) for i in range(n.value)]
    _hb.hb_buffer_destroy(buf)
    return out, sum(g[3] for g in out)

def show_text(cr, family, size_px, text, x, y):
    """text_shape::show_shaped_run: pen from x, glyphs at pen + offset, baseline y. Returns the shaped width."""
    set_font(cr, family, size_px)
    gl, w = shape(family, size_px, text); pen = x; out = []
    for idx, xo, yo, adv in gl:
        out.append(cairo.Glyph(idx, pen + xo, y - yo)); pen += adv
    cr.show_glyphs(out)
    return w

def cap_height(family, size_px):
    s = cairo.ImageSurface(cairo.FORMAT_RGB24, 4, 4); cr = cairo.Context(s)
    return set_font(cr, family, size_px).text_extents('H').height

def font_extents(family, size_px):
    s = cairo.ImageSurface(cairo.FORMAT_RGB24, 4, 4); cr = cairo.Context(s)
    return set_font(cr, family, size_px).extents()

def label_ink_rows(size_px):
    """-> (rows above the baseline, rows from the baseline's row down) holding any coverage when the printable ASCII
    set (0x21..0x7E, one shaped run) is painted at a whole-row baseline: the app's flag-label ink (marker_lane_rows,
    paint_handler.cpp, architect 2026-10-05), painted rather than read off a metric."""
    import math
    txt = ''.join(chr(c) for c in range(0x21, 0x7F)); m = int(math.ceil(2.0 * size_px))
    w = int(math.ceil(shape(SANS, size_px, txt)[1])) + 2 * m
    s = cairo.ImageSurface(cairo.FORMAT_ARGB32, w, 2 * m); cr = cairo.Context(s)
    cr.set_source_rgb(0, 0, 0); show_text(cr, SANS, size_px, txt, m, m); s.flush()
    a = np.ndarray((2 * m, s.get_stride() // 4), np.uint32, s.get_data())[:, :w]
    rows = np.where(a.max(axis=1) > 0)[0]
    return m - int(rows[0]), int(rows[-1]) - m + 1

def redesign_baseline(family, size_px, box_y, box_h):        # paint_handler.cpp
    import math; return box_y + math.floor((box_h + cap_height(family, size_px)) * 0.5)
def line_baseline(family, size_px, line_y):
    import math; return line_y + math.ceil(font_extents(family, size_px)[0])

SANS_PX = 12.0 * 96.0 / 72.0 * SCALE        # the probe size, 32 px

# ------------------------------------------------------------------ the period bitmap faces (gui_font.h)
# The app's vertical metrics at every scale are the strikes' (architect 2026-10-05): read here off the same files,
# as gui_font_bundled.cpp reads them — the strike's ascent / descent, its cap band (the "0"'s rows above the
# baseline) and a specimen's ink rows (gui_strike_ink_rows).
BODY_STRIKE = os.path.join(FONTS, 'crox1h.otb')
SMALL_STRIKE = os.path.join(FONTS, 'small_fonts_digits.otb')
_STRIKES = {}
def strike_metrics(path):
    """-> {'ascent', 'descent', 'cap', 'glyphs': {char: (top, height)}} in Windows px."""
    if path not in _STRIKES:
        from fontTools.ttLib import TTFont
        f = TTFont(path); st = f['EBLC'].strikes[0].bitmapSizeTable.hori; data = f['EBDT'].strikeData[0]
        glyphs = {chr(cp): (data[g].metrics.BearingY, data[g].metrics.height) for cp, g in f.getBestCmap().items()}
        _STRIKES[path] = {'ascent': st.ascender, 'descent': -st.descender, 'cap': glyphs['0'][0], 'glyphs': glyphs}
    return _STRIKES[path]

def strike_ink_rows(path, specimen):
    """-> (rows above the baseline, rows from the baseline's row down) the specimen lights (gui_strike_ink_rows)."""
    g = strike_metrics(path)['glyphs']
    return (max(g[c][0] for c in specimen if c in g and g[c][1]),
            max(g[c][1] - g[c][0] for c in specimen if c in g and g[c][1]))

def fallback_em(strike_path, band_char):
    """The fallback's em in Windows px (gui_fallback_em_px): the strike's cap over Liberation's unscaled ink height
    of `band_char` per em ("H" for the body, "0" for the digits)."""
    from fontTools.ttLib import TTFont
    f = TTFont(SANS_FILE); g = f['glyf'][f.getBestCmap()[ord(band_char)]]
    return strike_metrics(strike_path)['cap'] / ((g.yMax - g.yMin) / f['head'].unitsPerEm)

def verify_fonts(verbose=False):
    """Fail loudly unless the family resolves to the repository's file. Three proofs:
    (1) fontconfig's own FcFontMatch (the call cairo makes) names the file; (2) the glyph ids cairo maps
    for a probe string equal HarfBuzz's on the file; (3) the metrics equal the app's measured table
    (Liberation Sans at 32 px, SLIGHT, hint metrics on: ascent 29 / descent 7 / cap 22, measured 2026-10-05)."""
    fc = ctypes.CDLL('libfontconfig.so.1')
    fc.FcInitLoadConfigAndFonts.restype = ctypes.c_void_p
    fc.FcNameParse.restype = ctypes.c_void_p; fc.FcNameParse.argtypes = [ctypes.c_char_p]
    fc.FcConfigSubstitute.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
    fc.FcDefaultSubstitute.argtypes = [ctypes.c_void_p]
    fc.FcFontMatch.restype = ctypes.c_void_p; fc.FcFontMatch.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
    fc.FcPatternGetString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    cfg = fc.FcInitLoadConfigAndFonts()
    report = []
    for fam, want, px, metr in ((SANS, SANS_FILE, SANS_PX, (29, 7, 22)),):
        pat = fc.FcNameParse(fam.encode()); fc.FcConfigSubstitute(cfg, pat, 0); fc.FcDefaultSubstitute(pat)
        res = ctypes.c_int(0); m = fc.FcFontMatch(cfg, pat, ctypes.byref(res))
        f = ctypes.c_char_p(); fc.FcPatternGetString(m, b'file', 0, ctypes.byref(f))
        got = os.path.realpath(f.value.decode()) if f.value else None
        assert got == os.path.realpath(want), f'fontconfig resolved {fam!r} to {got}, not {want}'
        s = cairo.ImageSurface(cairo.FORMAT_RGB24, 4, 4); cr = cairo.Context(s); sf = set_font(cr, fam, px)
        assert cr.get_font_face().get_family() == fam
        probe = 'File Edit 0:45.625 b.33 100% | 5:39 AM'
        cg = [g.index for g in sf.text_to_glyphs(0, 0, probe, False)]
        hg = [g[0] for g in shape(fam, px, probe)[0]]
        assert cg == hg, f'{fam}: cairo glyph ids differ from HarfBuzz on {want} (fallback face?)'
        asc, desc = sf.extents()[0], sf.extents()[1]; cap = sf.text_extents('H').height
        assert (round(asc), round(desc), round(cap)) == metr, (fam, asc, desc, cap, metr)
        report.append(f'{fam!r} -> {got}  (ids match HarfBuzz; ascent {asc:g} descent {desc:g} cap {cap:g} at {px:.3f}px)')
    if verbose:
        for r in report: print('font:', r)
    return report

# ------------------------------------------------------------------ colours (colour.py owns the arithmetic)
def parse_colour(v):
    """'#rrggbb' = P3 bytes as-is (what the glass shows); 'srgb:#rrggbb' = one sRGB->P3 pass; [r,g,b] = bytes."""
    if isinstance(v, (list, tuple)): return tuple(int(x) for x in v)
    v = v.strip(); conv = False
    if v.lower().startswith('srgb:'): conv, v = True, v[5:].strip()
    h = v.lstrip('#'); c = tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    return tuple(srgb_to_p3(c)) if conv else c
def hexs(c): return '#%02X%02X%02X' % tuple(c)
def mix(own, toward, keep):
    """render.h mix_color: per-channel in byte space (GuiColor doubles), keep_own of `own` over `toward`."""
    return tuple(t + (o - t) * keep for o, t in zip(own, toward))
def outline_of(ink, canvas):
    """The waveform outline: the 50 % linear-light blend of ink over canvas (colour.lin_mix)."""
    return tuple(lin_mix(tuple(ink), tuple(canvas), 0.5))

def src(cr, c, a=None):
    """cairo source from 0..255 bytes (fractional allowed, as GuiColor doubles are). A recording context
    (render.py's TeeContext) is told the colour first, so the paint names the role it came from."""
    note = getattr(cr, 'note_source', None)
    if note is not None: note(c, a)
    if a is None: cr.set_source_rgb(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0)
    else: cr.set_source_rgba(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, a)

def surface_to_rgb(surf):
    surf.flush(); h, w, st = surf.get_height(), surf.get_width(), surf.get_stride()
    buf = np.frombuffer(surf.get_data(), np.uint8).reshape(h, st)[:, :w * 4].reshape(h, w, 4)
    return np.ascontiguousarray(buf[..., [2, 1, 0]])         # RGB24 is BGRx in memory (little endian)
