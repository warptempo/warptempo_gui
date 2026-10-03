#!/usr/bin/env python3
# tools/theme_catalog/levels.py — THE THREE LEVELS, the one module of the level arithmetic (architect 2026-10-03,
# settled on the mock set AQ): every catalog entry at LIGHT, DIM and DARK, so the app does no colour arithmetic and
# carries imported themes only. Read by gen_theme_table.py (the app's generated table, src/gui/theme_table.h) and by
# crops.py (the catalog's crops, the LIGHT level).
#
#   LIGHT  the entry as recorded: its ground, label and relief quartet.
#   DIM    the ground keeps its HLS hue and saturation at relative luminance 0.080 (ground_at);
#   DARK   the same at 0.035. On both, ALL FOUR RELIEF LINES (Hilight, 3DLight, Shadow, DkShadow) keep their hue and
#          saturation with their HLS lightness x L(new face) / L(base face) — black stays black, and a 3DLight equal
#          to the base face becomes the new face exactly — and the label is #FFFFFF.
#   THE EMBOSS LIGHT COPY (the disabled word's and glyph's light layer, Windows' DSS_DISABLED): LIGHT the recorded
#          Hilight; DIM / DARK Windows' own dialog-rule Hilight of the new face (toolkit_rules.windows_dialog: the
#          HLS lightness halfway to white, hue and saturation kept) — the recorded Hilight there would read as an
#          enabled word.
#   THE THREE PAIRS — selected, info, field — and where each comes from (every level):
#     SELECTED the entry's selected_fill / selected_text; a CDE entry, which records none (Motif selects by inverse
#              video), takes its title_active with colour set 1's own Motif foreground as the text;
#     INFO     the entry's info_ground / info_text; where its source records none (KDE 3.5's kcsrc, CDE) the app's own
#              #FFFFE1 / #000000 (Windows' COLOR_INFOBK / COLOR_INFOTEXT);
#     FIELD    the entry's field_ground / field_text, recorded on every entry.
#   LIGHT carries the three as recorded. ON DIM AND DARK THE THREE GROUNDS DARKEN IN PROPORTION (architect 2026-10-03,
#   mock sets AS and AT: AS02 picked for dark, "darkened in proportion" for dim as well, and the selection
#   "proportionally darken"): each ground — the field's, the info's (the card and the tooltip) and the selected fill,
#   the CDE pair's title_active included — moves to relative luminance = its recorded luminance x TARGET[level] /
#   L(the entry's recorded ground), the ground's own ratio, with its HUE AND HSV SATURATION KEPT (pair_ground_at: the
#   HLS rule the ground itself keeps turned #FFFFE1, HLS saturation 1.0, into a saturated olive; its HSV saturation is
#   0.118). The ratio's numerator is the level's ruled luminance TARGET, not the luminance the searched ground lands
#   on: that is AS02's own arithmetic, and the landed one (#353535 for #C0C0C0 at dark) would put the card one blue
#   step off AS02's #494940. An entry whose recorded ground is darker than the level's target (the warptempo entry at
#   both levels, cde-northern-sky at dim) has a ratio above 1, so its three grounds LIGHTEN with its face — the rule
#   as ruled, which the search caps at the brightest colour of the hue and saturation. THE TEXT ON EACH OF THE THREE
#   IS THE LEVEL'S LABEL #FFFFFF on both levels, mock AS02's rule for dark, taken for dim by symmetry (the planner's
#   call: the dim mocks showed black text on a light field, which no longer stands once the field darkens with the
#   face — the architect may overrule it on the glass).
# The entry's disabled_text is NOT carried: every disabled word and glyph in the app is the emboss (render.h's
# palette block), so no site reads it.
#
# THE EXHAUSTIVE SEARCHES DEFINE THE RESULTS (the ground's HLS lightness at every 1 / 100000 step, a pair ground's
# HSV value likewise, the first nearest luminance kept): ground_at_exhaustive and pair_ground_at_exhaustive are the
# definitions, and ground_at / pair_ground_at the same searches vectorized with numpy, op for op (colorsys's
# hls_to_rgb / hsv_to_rgb arithmetic, Python's round-half-even, the luminance by a per-byte table of
# colour.relative_luminance's own channel function), checked against the definitions on every case the table reads
# by `python3 tools/theme_catalog/levels.py --verify`.
import colorsys, functools, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, 'tools', 'palette'))
import numpy as np                                                     # noqa: E402
import toolkit_rules                                                   # noqa: E402
from colour import relative_luminance, srgb_channel_to_linear         # noqa: E402

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
LEVELS = ('light', 'dim', 'dark')
TARGET = {'dim': 0.080, 'dark': 0.035}
QUARTET = ('bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow')
APP_INFO = ('#FFFFE1', '#000000')     # the app's info pair where a source records none
DIM_LABEL = '#FFFFFF'
# the roles one level carries, in the generated table's field order
ROLES = ('ground', 'label', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow', 'emboss_hilight',
         'selected_fill', 'selected_text', 'info_ground', 'info_text', 'field_ground', 'field_text')


def unhex(s): return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))
def hx(c): return '#%02X%02X%02X' % tuple(c)
def hls(c): return colorsys.rgb_to_hls(*(v / 255 for v in c))
def rgb(h, l, s): return tuple(int(round(v * 255)) for v in colorsys.hls_to_rgb(h, min(1.0, max(0.0, l)), s))


# The exhaustive searches' luminance, memoized: a pure function of the 8-bit colour, and a search's candidates repeat
# (at most 256 distinct bytes along an axis), so --verify runs in minutes, not hours; the values are the function's.
_rl = functools.lru_cache(maxsize=None)(relative_luminance)


def ground_at_exhaustive(g, target):
    """THE DEFINITION: `g` (8-bit) at relative luminance `target`, its HLS hue and saturation kept — the lightness
    i / 100000 for i in 1 .. 99999 whose colour's luminance is nearest the target, the first such kept."""
    h, l, s = hls(g)
    best = None
    for i in range(1, 100000):
        c = rgb(h, i / 100000, s)
        d = abs(_rl(c) - target)
        if best is None or d < best[0]: best = (d, c)
    return best[1]


_LIN = np.array([srgb_channel_to_linear(v / 255.0) for v in range(256)])


def _channel(m1, m2, hue):
    """colorsys._v over a vector of (m1, m2) at one scalar hue, op for op."""
    hue = hue % 1.0
    if hue < 1.0 / 6.0: return m1 + (m2 - m1) * hue * 6.0
    if hue < 0.5: return m2
    if hue < 2.0 / 3.0: return m1 + (m2 - m1) * (2.0 / 3.0 - hue) * 6.0
    return m1


def ground_at(g, target):
    """ground_at_exhaustive vectorized (the module head says how it is the same search)."""
    h, l0, s = hls(g)
    ls = np.arange(1, 100000) / 100000
    ls = np.minimum(1.0, np.maximum(0.0, ls))
    if s == 0.0:
        chans = (ls, ls, ls)
    else:
        m2 = np.where(ls <= 0.5, ls * (1.0 + s), ls + s - (ls * s))
        m1 = 2.0 * ls - m2
        chans = (_channel(m1, m2, h + 1.0 / 3.0), _channel(m1, m2, h), _channel(m1, m2, h - 1.0 / 3.0))
    b = [np.rint(c * 255).astype(np.int64) for c in chans]
    lum = 0.2126 * _LIN[b[0]] + 0.7152 * _LIN[b[1]] + 0.0722 * _LIN[b[2]]
    i = int(np.argmin(np.abs(lum - target)))
    return int(b[0][i]), int(b[1][i]), int(b[2][i])


def pair_ground_at_exhaustive(c, target):
    """THE DEFINITION: `c` (8-bit) at relative luminance `target`, its HSV hue and saturation kept — the value
    i / 100000 for i in 1 .. 100000 whose colour's luminance is nearest the target, the first such kept. A neutral
    colour (saturation 0) gives the grey ground_at would."""
    h, s, _ = colorsys.rgb_to_hsv(*(v / 255 for v in c))
    best = None
    for i in range(1, 100001):
        col = tuple(int(round(x * 255)) for x in colorsys.hsv_to_rgb(h, s, i / 100000))
        d = abs(_rl(col) - target)
        if best is None or d < best[0]: best = (d, col)
    return best[1]


def pair_ground_at(c, target):
    """pair_ground_at_exhaustive vectorized (the module head says how it is the same search): colorsys.hsv_to_rgb
    over a vector of values at one scalar hue and saturation."""
    h, s, _ = colorsys.rgb_to_hsv(*(v / 255 for v in c))
    v = np.arange(1, 100001) / 100000
    if s == 0.0:
        chans = (v, v, v)
    else:
        i = int(h * 6.0)
        f = (h * 6.0) - i
        p = v * (1.0 - s)
        q = v * (1.0 - s * f)
        t = v * (1.0 - s * (1.0 - f))
        chans = ((v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q))[i % 6]
    b = [np.rint(ch * 255).astype(np.int64) for ch in chans]
    lum = 0.2126 * _LIN[b[0]] + 0.7152 * _LIN[b[1]] + 0.0722 * _LIN[b[2]]
    k = int(np.argmin(np.abs(lum - target)))
    return int(b[0][k]), int(b[1][k]), int(b[2][k])


def selected_pair(e):
    r = e['roles']
    if 'selected_fill' in r: return r['selected_fill'], r['selected_text']
    return r['title_active'], e['provenance']['rule']['computed']['motif:set1.fg']


def info_pair(e):
    r = e['roles']
    if 'info_ground' in r: return r['info_ground'], r['info_text']
    return APP_INFO


PAIRS = (('selected_fill', 'selected_text'), ('info_ground', 'info_text'), ('field_ground', 'field_text'))


def pair_targets(e, level):
    """The three pair grounds' recorded colours and their DIM / DARK luminance targets (the module head's rule):
    [(role, recorded 8-bit colour, target)]."""
    r = e['roles']
    ratio = TARGET[level] / relative_luminance(unhex(r['ground']))
    rec = dict(zip(('selected_fill', 'info_ground', 'field_ground'),
                   (selected_pair(e)[0], info_pair(e)[0], r['field_ground'])))
    return [(role, unhex(rec[role]), relative_luminance(unhex(rec[role])) * ratio) for role in rec]


def level_roles(e, level, ground_search=ground_at, pair_search=pair_ground_at):
    """One catalog entry at one level -> {role: '#RRGGBB'} over ROLES (the module head is the rule)."""
    r = e['roles']
    base_face = unhex(r['ground'])
    out = {}
    if level == 'light':
        out['ground'] = r['ground']; out['label'] = r['label']
        for role in QUARTET: out[role] = r[role]
        out['emboss_hilight'] = r['bevel_hilight']
    else:
        g = ground_search(base_face, TARGET[level])
        k = hls(g)[1] / hls(base_face)[1]
        out['ground'] = hx(g); out['label'] = DIM_LABEL
        for role in QUARTET:
            c = unhex(r[role])
            if role == 'bevel_light' and c == base_face: out[role] = hx(g); continue
            h, l, s = hls(c)
            out[role] = hx(rgb(h, l * k, s))
        out['emboss_hilight'] = hx(toolkit_rules.windows_dialog(g)[0])
    out['selected_fill'], out['selected_text'] = selected_pair(e)
    out['info_ground'], out['info_text'] = info_pair(e)
    out['field_ground'], out['field_text'] = r['field_ground'], r['field_text']
    if level != 'light':
        for role, c, target in pair_targets(e, level): out[role] = hx(pair_search(c, target))
        for _, text in PAIRS: out[text] = DIM_LABEL
    assert tuple(out) == ROLES, tuple(out)
    return out


def entries():
    return json.load(open(CATALOG))['entries']


def verify():
    """ground_at and pair_ground_at against their definitions on every case the table reads (every entry's ground,
    and its three pair grounds, at DIM and DARK; a repeated colour and target searched once); one line per mismatch,
    exit 1 on any."""
    bad = n = 0
    for e in entries():
        g = unhex(e['roles']['ground'])
        for lvl in ('dim', 'dark'):
            a, b = ground_at(g, TARGET[lvl]), ground_at_exhaustive(g, TARGET[lvl]); n += 1
            if a != b: bad += 1; print(f'{e["key"]} {lvl}: ground_at {hx(a)} != exhaustive {hx(b)}')
    print(f'ground_at: {n - bad} of {n} cases identical to the exhaustive search')
    cases = {}
    for e in entries():
        for lvl in ('dim', 'dark'):
            for role, c, target in pair_targets(e, lvl): cases.setdefault((c, target), f'{e["key"]} {lvl} {role}')
    pbad = 0
    for (c, target), where in cases.items():
        a, b = pair_ground_at(c, target), pair_ground_at_exhaustive(c, target)
        if a != b: pbad += 1; print(f'{where}: pair_ground_at {hx(a)} != exhaustive {hx(b)}')
    print(f'pair_ground_at: {len(cases) - pbad} of {len(cases)} distinct cases identical to the exhaustive search')
    return bad + pbad


if __name__ == '__main__':
    if sys.argv[1:] == ['--verify']: raise SystemExit(1 if verify() else 0)
    raise SystemExit('usage: python3 tools/theme_catalog/levels.py --verify')
