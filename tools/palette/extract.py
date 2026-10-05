#!/usr/bin/env python3
# tools/palette/extract.py — measures one of the app's own screencaps into a SCENE:
#     python3 tools/palette/extract.py <dir>/tablet_base_<tag>.png --first-label M:SS.mmm --flags T1,T2,... \
#         --clock 'B | 00:45.621' --legend '100% ↑ | 7:01 AM' [--rev <commit>] [--step 125] [--out-dir DIR]
# The scene TAG is the base name's suffix (tablet_base_<tag>.png); it writes scene_<tag>.json, waveform_<tag>.json
# and glyphs/<tag>/ (8-bit PGM coverage masks + index.json) into the tool's directory (or --out-dir). Everything the
# renderer places comes from here: the lane rows, the button boxes, the trim lane's rects, the ruler's ticks and
# labels, the flags, the playhead, the clock and the waveform's per-column runs. The texts the pixels cannot name
# (the first ruler label, the flag labels, the clock, the legend) are parameters, never defaults: a capture is one
# state of the app and nothing here assumes which (README.md lists each scene's command line). Each measurement
# is cross-checked against the source's own tables and rules (src/gui/main.cpp's lane table, render.h's ruler and
# head rules, paint_handler.cpp's rosters, layout constants and label seat), read at --rev (the commit the capture's
# APK was built from; default the working tree), and every text item is re-rendered and compared with the capture.
# A disagreement is printed as UNEXPLAINED and recorded; a known, explained difference is printed as NOTE and
# recorded under scene["mismatches"]. The run ends with the unexplained count (0 = clean).
# The measurement model is the app's flat picture before 2026-10-02 (it parses kRedesignContentGround,
# kPlayheadHeadAlpha, kRulerHeadGroundPx and their kin, which the frozen design retired): it extracts the captures
# of the APK built at a6f53163 (with --rev a6f53163), not a capture of today's app.
import os, sys, re, json, math, argparse, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C
import numpy as np
import cairo

AP = argparse.ArgumentParser(description='measure a tablet_base_<tag>.png screencap into scene_<tag>.json')
AP.add_argument('base', help='the screencap, <dir>/tablet_base_<tag>.png (the app\'s own bytes)')
AP.add_argument('--rev', default=None, help='read the app\'s source at this git revision, the commit the capture\'s '
                'APK was built from (default: the working tree)')
AP.add_argument('--first-label', required=True, help="the leftmost ruler label's text, M:SS.mmm (e.g. 0:45.500)")
AP.add_argument('--step', type=float, default=125.0, help='the ruler step in ms between labels (default 125)')
AP.add_argument('--flags', required=True, help='the visible flags\' label texts, left to right, comma-separated')
AP.add_argument('--clock', required=True, help="the bottom row's clock text, e.g. 'B | 00:45.621'")
AP.add_argument('--legend', required=True, help="the menu row's legend, e.g. '100%% ↑ | 7:01 AM'")
AP.add_argument('--out-dir', default=C.HERE, help='where the scene, waveform and glyphs/<tag>/ go (default: the tool)')
ARGS = AP.parse_args()

BASE = ARGS.base
m_ = re.fullmatch(r'tablet_base_(.+)\.png', os.path.basename(BASE))
if not m_: raise SystemExit(f'{BASE}: the base must be named tablet_base_<tag>.png (the tag names the scene)')
TAG = m_.group(1)
OUT = os.path.abspath(ARGS.out_dir)
GLYPH_REL = os.path.join('glyphs', TAG)
GLYPHS = os.path.join(OUT, GLYPH_REL)
os.makedirs(GLYPHS, exist_ok=True)
def parse_label(t):
    m = re.fullmatch(r'(\d+):(\d\d)\.(\d\d\d)', t)
    if not m: raise SystemExit(f'--first-label {t!r}: want M:SS.mmm')
    return (int(m.group(1)) * 60 + int(m.group(2))) * 1000.0 + int(m.group(3))
FIRST_LABEL_MS = parse_label(ARGS.first_label); STEP = ARGS.step
FLAG_TEXTS = [t.strip() for t in ARGS.flags.split(',')]
IMG = C.read_rgb(BASE).astype(np.int64)
assert IMG.shape == (C.H, C.W, 3), IMG.shape
MISMATCH = []; UNEXPLAINED = []
def mismatch(msg): MISMATCH.append(msg); UNEXPLAINED.append(msg); print('UNEXPLAINED:', msg)
def note(msg): MISMATCH.append(msg); print('NOTE:', msg)
def col(x, y): return tuple(int(v) for v in IMG[y, x])
def is_c(region, c): return (region == np.array(c)).all(-1)
def SRC(p):
    """One source file of the app, at --rev (read-only `git show`) or from the working tree."""
    if ARGS.rev is None: return open(os.path.join(C.REPO, p)).read()
    return subprocess.run(['git', '-C', C.REPO, 'show', f'{ARGS.rev}:{p}'], check=True, capture_output=True).stdout.decode()
print(f'scene {TAG!r} from {os.path.relpath(BASE, C.REPO)} -> {os.path.relpath(OUT, C.REPO)}')
# ------------------------------------------------------------------ the app's constants (from source)
RENDER_H = SRC('src/gui/render.h'); ICONS_CPP = SRC('src/gui/icons.cpp'); PAINT = SRC('src/gui/paint_handler.cpp')
APP_STATE = SRC('src/gui/app_state.h')
def hexconst(text, name):
    m = re.search(r'constexpr GuiColor ' + name + r'\s*=\s*hex\(0x([0-9A-Fa-f]{6})\)', text)
    if m is None:                       # kWaveformCanvas = hex(kWaveformCanvasRgb)
        m = re.search(r'constexpr \w+ ' + name + r'Rgb\s*=\s*0x([0-9A-Fa-f]{6})', text)
    if m is None: return None
    h = m.group(1); return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
K = {n: hexconst(RENDER_H, n) for n in re.findall(r'inline constexpr GuiColor (k\w+)\s*=\s*hex\(', RENDER_H)}
K = {n: v for n, v in K.items() if v is not None}
GROUND, LABEL = K['kRedesignContentGround'], K['kRedesignLabel']
DISABLED_MIX = float(re.search(r'kRedesignDisabledMix\s*=\s*([0-9.]+)', RENDER_H).group(1))

# icon inks: kIconX colours and each icon's path inks, in path order (icons.cpp)
ICON_INK = {n: hexconst(ICONS_CPP, n) for n in re.findall(r'constexpr GuiColor (kIcon\w+)\s*=', ICONS_CPP)}
INK_ROLE = {'kIconText': 'text', 'kIconRecord': 'record', 'kIconNegativeText': 'negative', 'kIconPreviewOn': 'preview_on',
            'kIconAccent': 'accent', 'kIconLiftCross': 'lift_cross', 'kIconPlainWhite': 'plain_white', 'kIconWav': 'wav'}
ICON_PATH_INKS = {}
for m in re.finditer(r'constexpr IconPath k(\w+)Paths\[\]\s*=\s*\{(.*?)\n\};', ICONS_CPP, re.S):
    inks = re.findall(r'\{\s*(kIcon\w+)\s*,', m.group(2)); u = []
    for i in inks:
        if i not in u: u.append(i)
    ICON_PATH_INKS[m.group(1)] = u

# ------------------------------------------------------------------ the rosters (paint_handler.cpp)
def roster(name, kind):
    body = re.search(r'constexpr ' + kind + r' ' + name + r'\[\]\s*=\s*\{(.*?)\n\};', PAINT, re.S).group(1)
    body = re.sub(r'//[^\n]*', '', body)
    return re.findall(r'\{RedesignButton::(\w+),\s*icons::Icon::(\w+)\}', body)
ICON_ROW = roster('kIconRowButtons', 'IconRowDef'); VIEW_GROUP = roster('kIconRowViewGroup', 'IconRowDef')
BOTTOM = [roster(n, 'TransportRowDef') for n in ('kMarkerVerbGroup', 'kTransportWalkGroup', 'kTransportArrowGroup', 'kTransportGroup')]
og = re.search(r'bool redesign_button_opens_icon_group\(RedesignButton b\)\s*\{(.*?)\n\}', APP_STATE, re.S).group(1)
OPENS = set(re.findall(r'case RedesignButton::(\w+):', re.sub(r'//[^\n]*', '', og)))
# the state swaps visible in this capture (redesign_button_icon): a writable tab wears Unlock,
# a stopped transport MediaPlaybackStart (its table icon), Save/Render their table icons.
SWAPPED = {'IconReadOnly': 'Unlock'}

# the ruler lane's derived height and the head (render.h, paint_handler.cpp)
def intconst(text, name): return int(re.search(r'constexpr int\s+' + name + r'\s*=\s*(\d+)\s*;', text).group(1))
RULER_HEAD_GROUND = intconst(RENDER_H, 'kRulerHeadGroundPx')
MARKER_LANE = intconst(RENDER_H, 'kMarkerLaneHeightPx')
HEAD_H = intconst(RENDER_H, 'kPlayheadHeadHeightPx')
HALF = [int(v) for v in re.search(r'kPlayheadHeadHalf\[kPlayheadHeadHeightPx\]\s*=\s*\{([^}]*)\}', RENDER_H).group(1).split(',')]
HEAD_ALPHA = float(re.search(r'kPlayheadHeadAlpha\s*=\s*([0-9.]+)', RENDER_H).group(1))
CAP_TOP = float(re.search(r'kRulerLabelCapTopPx\s*=\s*([0-9.]+)', PAINT).group(1))
MAJOR_RISE = float(re.search(r'kRulerMajorRisePx\s*=\s*([0-9.]+)', PAINT).group(1))
assert len(HALF) == HEAD_H

# ------------------------------------------------------------------ lanes (device px, end-exclusive)
S = C.SCALE
def scaled_px(v, floor=None):
    r = int(np.rint(v * S)); return r if floor is None or r >= floor else floor
# ruler_label_baseline_px: pad = max(0, scaled 4 - (ceil(ascent) - nearbyint(cap))), baseline = pad + ceil(ascent)
ASC = C.font_extents(C.SANS, C.SANS_PX)[0]; CAP = C.cap_height(C.SANS, C.SANS_PX)
RULER_PAD = max(0, scaled_px(CAP_TOP) - (math.ceil(ASC) - int(np.rint(CAP))))
RULER_BASELINE_OFF = RULER_PAD + math.ceil(ASC)
# ruler_lane_h_px: the baseline + kRulerHeadGroundPx (floored at one device row) + the head's rows
RULER_RULE = RULER_BASELINE_OFF + scaled_px(RULER_HEAD_GROUND, 1) + scaled_px(HEAD_H)
TABLE = dict(menu=30 * S, icon=46 * S, trim=10 * S, ruler=RULER_RULE, marker=scaled_px(MARKER_LANE), bottom=46 * S + 2,
             wave_default=500 * S)
print(f'ruler rule: pad {RULER_PAD} + ceil(ascent) {math.ceil(ASC)} + ground {scaled_px(RULER_HEAD_GROUND, 1)} + head '
      f'{scaled_px(HEAD_H)} = {RULER_RULE} rows; label baseline at lane top + {RULER_BASELINE_OFF}')
def runs_v(x, y0=0, y1=C.H):
    out = []; s = y0
    for y in range(y0 + 1, y1 + 1):
        if y == y1 or col(x, y) != col(x, s): out.append((s, y, col(x, s))); s = y
    return out
def runs_h(y, x0=0, x1=C.W):
    out = []; s = x0
    for x in range(x0 + 1, x1 + 1):
        if x == x1 or col(x, y) != col(s, y): out.append((s, x, col(s, y))); s = x
    return out
rv = runs_v(3)
# the trim lane's first row: the first row under the menu row whose column 3 leaves the ground (column 3 is left of
# every icon-row button; the lane's top row is the ground's bevel lo, or the bar's bevel hi where the bar covers it)
trim_y = next(a for a, b, c in rv if a >= 60 and c != GROUND)
bottom_border_y = next(a for a, b, c in rv if a > 1000 and c == K['kRedesignTabLine'])
well_top = next(a for a, b, c in rv if c == (0, 0, 0))
well_bot = next(b for a, b, c in rv if c == (0, 0, 0) and a > 1000)
lanes = dict(menu=[0, 60], icon=[60, trim_y], trim=[trim_y, trim_y + TABLE['trim']])
# the ruler and the marker lane share no measurable boundary (both are ground): the marker lane is its table height
# over the well, the ruler the rows between the trim lane and it; the ruler's own rule is the cross-check
lanes['marker'] = [well_top - TABLE['marker'], well_top]
lanes['ruler'] = [lanes['trim'][1], lanes['marker'][0]]
lanes['well'] = [well_top, well_bot]
lanes['bottom'] = [bottom_border_y, C.H]
for n in ('menu', 'icon', 'ruler', 'bottom'):
    if lanes[n][1] - lanes[n][0] != TABLE[n]:
        mismatch(f'lane {n}: measured {lanes[n]} = {lanes[n][1]-lanes[n][0]} rows, the app\'s rule says {TABLE[n]}')
tb = [y for y in range(lanes['trim'][0], lanes['trim'][1]) if col(3, y) == K['kTrimLaneBottomBorder']]
if col(3, lanes['trim'][1]) != GROUND or col(3, lanes['trim'][1] - 1) != K['kTrimLaneBottomBorder']:
    mismatch(f"trim lane {lanes['trim']}: its bottom border does not end the lane (column 3 rows {tb})")
wave_h = well_bot - well_top
gap2 = bottom_border_y - well_bot
leftover = C.H - lanes['well'][0] - (lanes['bottom'][1] - lanes['bottom'][0])
if wave_h == leftover and gap2 == 0:
    note(f'waveform area: {wave_h} device rows ({well_top}..{well_bot-1}) with gap 2 = 0 -- the device\'s config is '
         f'max_waveform_height 0 (no maximum), main.cpp\'s "At 0" tablet stack: the waveform takes the whole leftover; '
         f'the templates\' default 500 would give {TABLE["wave_default"]} + gap 2 = {leftover - TABLE["wave_default"]}')
elif wave_h != TABLE['wave_default']:
    mismatch(f'waveform area: measured {wave_h} rows with gap 2 = {gap2}; neither the leftover {leftover} '
             f'(max_waveform_height 0) nor the default {TABLE["wave_default"]}')
# the well's border thickness: black rows at top and bottom
bt = sum(1 for y in range(well_top, well_top + 10) if col(3, y) == (0, 0, 0))
bb = sum(1 for y in range(well_bot - 10, well_bot) if col(3, y) == (0, 0, 0))
canvas = [well_top + bt, well_bot - bb]
assert col(3, canvas[0]) == K['kWaveformCanvas'] and col(3, canvas[1] - 1) == K['kWaveformCanvas']
# the bottom row's border-top: rows and x inset
bb_rows = [y for y in range(bottom_border_y, bottom_border_y + 6) if col(1000, y) == K['kRedesignTabLine']]
bb_x = [x for x in range(0, C.W) if col(x, bottom_border_y) == K['kRedesignTabLine']]
bottom_border = dict(y=bottom_border_y, h=len(bb_rows), x0=bb_x[0], x1=bb_x[-1] + 1)
content_bottom = [bottom_border_y + len(bb_rows), C.H]
print('lanes', lanes, 'canvas', canvas, 'bottom border', bottom_border)

# ------------------------------------------------------------------ the icon row and the bottom row: boxes
BTN, GAP, PAD, GLYPH = 32 * S, 2 * S, 8 * S, 22 * S
SEP_GAP, SEP_W, SEP_H = 4 * S, 1 * S, 34 * S
TSEP_GAP, TSEP_H = 5 * S, 32 * S
icon_lane_h = lanes['icon'][1] - lanes['icon'][0]
btn_y = lanes['icon'][0] + (icon_lane_h - BTN) // 2
sep_y = lanes['icon'][0] + (icon_lane_h - SEP_H) // 2
buttons = []; seps = []
x = PAD
for i, (b, ic) in enumerate(ICON_ROW):
    if i:
        if b in OPENS: x += SEP_GAP; seps.append(dict(row='icon', x=x, y=sep_y, w=SEP_W, h=SEP_H)); x += SEP_W + SEP_GAP
        else: x += GAP
    buttons.append(dict(row='icon', button=b, icon=SWAPPED.get(b, ic), table_icon=ic, x=x, y=btn_y, w=BTN, h=BTN)); x += BTN
view_x0 = C.W - PAD - (len(VIEW_GROUP) * BTN + (len(VIEW_GROUP) - 1) * GAP)
seps.append(dict(row='icon', x=view_x0 - SEP_GAP - SEP_W, y=sep_y, w=SEP_W, h=SEP_H))
for i, (b, ic) in enumerate(VIEW_GROUP):
    buttons.append(dict(row='icon', button=b, icon=ic, table_icon=ic, x=view_x0 + i * (BTN + GAP), y=btn_y, w=BTN, h=BTN))
cb_h = content_bottom[1] - content_bottom[0]
bbtn_y = content_bottom[0] + (cb_h - BTN) // 2
bsep_y = content_bottom[0] + (cb_h - TSEP_H) // 2
gw = lambda n: n * BTN + (n - 1) * GAP
block_w = sum(gw(len(g)) for g in BOTTOM) + 3 * (TSEP_GAP + SEP_W + TSEP_GAP)
ax = C.W - PAD - block_w
for gi, g in enumerate(BOTTOM):
    if gi:
        pen = ax + TSEP_GAP - GAP; seps.append(dict(row='bottom', x=pen, y=bsep_y, w=SEP_W, h=TSEP_H)); ax = pen + SEP_W + TSEP_GAP
    for b, ic in g:
        buttons.append(dict(row='bottom', button=b, icon=ic, table_icon=ic, x=ax, y=bbtn_y, w=BTN, h=BTN)); ax += BTN + GAP
# cross-check: every separator column measured, every ink pixel of both rows inside a computed glyph box
for name, (y0, y1) in (('icon', lanes['icon']), ('bottom', content_bottom)):
    band = IMG[y0:y1]; sepm = is_c(band, K['kRedesignTabLine'])
    meas = [int(c) for c in np.where(sepm.sum(0) >= 40)[0]]
    want = sorted(c for s in seps if s['row'] == name for c in range(s['x'], s['x'] + s['w']))
    if meas != want: mismatch(f'{name} row separators: measured cols {meas}, layout {want}')
    ys = [int(y) + y0 for y in np.where(sepm.sum(1) > 0)[0]]
    s0 = [s for s in seps if s['row'] == name][0]
    if (min(ys), max(ys) + 1) != (s0['y'], s0['y'] + s0['h']): mismatch(f'{name} separator rows {min(ys)}..{max(ys)} vs layout {s0["y"]}..')
    ink = np.abs(band - np.array(GROUND)).sum(-1) > 0
    ink &= ~sepm
    cover = np.zeros_like(ink)
    for b in buttons:
        if b['row'] == name: cover[b['y'] - y0:b['y'] - y0 + BTN, b['x']:b['x'] + BTN] = True
    stray = ink & ~cover
    if name == 'bottom':                       # the clock's text lives left of the buttons
        stray[:, :buttons[[i for i, b in enumerate(buttons) if b['row'] == 'bottom'][0]]['x']] = False
    n = int(stray.sum())
    if n: mismatch(f'{name} row: {n} ink pixels outside the computed button boxes')
groups_icon = [1]
for b, _ in ICON_ROW[1:]:
    if b in OPENS: groups_icon.append(1)
    else: groups_icon[-1] += 1
print('icon row groups', groups_icon, '+ view', len(VIEW_GROUP), '| bottom', [len(g) for g in BOTTOM])
if len(ICON_ROW) + len(VIEW_GROUP) != 26: mismatch(f'icon row: {len(ICON_ROW)} + {len(VIEW_GROUP)} buttons')
else: note('brief says "26 + 3 view icons"; the source has kIconRowButtons 23 + kIconRowViewGroup 3 = 26 in all '
               f'(groups {groups_icon} + 3); this extraction follows the source and the capture')
# a note every scene carries verbatim: an earlier icon roster had Tooltips before Settings; the source and the
# capture have Settings (sliders) then Tooltips (the pointer on the i)
note('render95icons.py ICON_GROUPS lists IconTooltips before IconSettings; kIconRowButtons (and the capture: '
         'sliders, then the i-with-pointer) has IconSettings then IconTooltips -- its S-mocks gave each the other\'s set icon')

# the toggled button: its face box is visible (selected fill + kRedesignLine ring)
for b in buttons:
    inner = IMG[b['y'] + 4:b['y'] + BTN - 4, b['x'] + 4:b['x'] + BTN - 4]
    edge = IMG[b['y'] + 2, b['x'] + 20:b['x'] + 44]
    b['selected'] = bool(is_c(edge, K['kRedesignSelectedFill']).all())
    b['under'] = list(K['kRedesignSelectedFill'] if b['selected'] else GROUND)
sel = [b['button'] for b in buttons if b['selected']]
print('selected (toggled) buttons:', sel)

# ------------------------------------------------------------------ glyphs: per-ink 8-bit coverage
def ink_list(b):
    return ICON_PATH_INKS[b['icon']]
def mul8(x, a):
    """pixman / cairo UN8 multiply with rounding: (t = x*a + 0x80; (t + (t >> 8)) >> 8)."""
    t = x * a + 128; return (t + (t >> 8)) >> 8
def over8(s8, m, d):
    """cairo's opaque-solid OVER through a coverage m (lerp8x4 / pixman over_n_8_8888): s*m + d*(255-m)."""
    return mul8(s8, m) + mul8(d, 255 - m)
def source8(c):
    """the 8-bit colour cairo hands pixman for a double source: round(c/255 * 65535) >> 8."""
    return (np.floor(np.asarray(c, float) / 255.0 * 65535.0 + 0.5).astype(np.int64)) >> 8

def recover(b):
    """-> enabled, keep, {ink_name: coverage uint8 GLYPHxGLYPH}. The coverage is the rasteriser's own 8-bit m:
    for every pixel the m (or the (m1, m2) pair for a two-ink glyph) whose composite under cairo's arithmetic
    lands nearest the captured bytes -- exact wherever the paths do not touch."""
    gx, gy = b['x'] + (BTN - GLYPH) // 2, b['y'] + (BTN - GLYPH) // 2
    b['glyph_x'], b['glyph_y'], b['glyph_px'] = gx, gy, GLYPH
    p = IMG[gy:gy + GLYPH, gx:gx + GLYPH].reshape(-1, 3); g = np.array(b['under'], np.int64)
    inks = ink_list(b)
    full = [np.array(ICON_INK[i], float) for i in inks]
    maxdis = max(np.abs(DISABLED_MIX * (c - g)).max() for c in full)
    enabled = np.abs(p - g).max() > maxdis + 2
    keep = 1.0 if enabled else DISABLED_MIX
    s8 = [source8(C.mix(c, g, keep)) for c in full]
    M = np.arange(256)
    if len(inks) == 1:
        lut = over8(s8[0][None, :], M[:, None], g[None, :])              # 256 x 3
        err = np.abs(p[:, None, :] - lut[None, :, :]).sum(-1)           # N x 256
        cov = [err.argmin(1)]
    else:
        assert len(inks) == 2
        E = np.stack([s8[0] - g, s8[1] - g], 1).astype(float)
        A = np.linalg.lstsq(E, (p - g).T.astype(float), rcond=None)[0].T * 255
        m1c = np.clip(np.round(A[:, 0]), 0, 255).astype(np.int64); m2c = np.clip(np.round(A[:, 1]), 0, 255).astype(np.int64)
        best = np.full(len(p), 1 << 30); b1 = m1c.copy(); b2 = m2c.copy()
        for d1 in range(-6, 7):
            for d2 in range(-6, 7):
                m1 = np.clip(m1c + d1, 0, 255); m2 = np.clip(m2c + d2, 0, 255)
                q = over8(s8[1][None, :], m2[:, None], over8(s8[0][None, :], m1[:, None], g[None, :]))
                e = np.abs(q - p).sum(-1) * 1000 + (m1 * m2 > 0)        # prefer a pixel owned by one ink
                upd = e < best; best[upd] = e[upd]; b1[upd] = m1[upd]; b2[upd] = m2[upd]
        cov = [b1, b2]
    return enabled, keep, {INK_ROLE[i]: c.astype(np.uint8).reshape(GLYPH, GLYPH) for i, c in zip(inks, cov)}

def write_pgm(path, m):
    h, w = m.shape; open(path, 'wb').write(b'P5\n%d %d\n255\n' % (w, h) + m.tobytes())

def composite_glyph(dst_rgb, b, masks, inks, keep):
    """Re-render through cairo exactly as the renderer will (mask_surface per ink, in path order)."""
    s = cairo.ImageSurface(cairo.FORMAT_RGB24, GLYPH, GLYPH); cr = cairo.Context(s)
    C.src(cr, b['under']); cr.paint()
    for name in inks:
        m = masks[name]; ms = cairo.ImageSurface(cairo.FORMAT_A8, GLYPH, GLYPH); st = ms.get_stride()
        buf = np.frombuffer(ms.get_data(), np.uint8).reshape(GLYPH, st); buf[:, :GLYPH] = m; ms.mark_dirty()
        c = C.mix(INK_COLOUR_CONST[name], b['under'], keep)
        C.src(cr, c); cr.mask_surface(ms, 0, 0)
    return C.surface_to_rgb(s)
INK_COLOUR_CONST = {INK_ROLE[k]: v for k, v in ICON_INK.items()}

gindex = {}
bad_total = 0
for b in buttons:
    enabled, keep, masks = recover(b)
    b['enabled'] = bool(enabled)
    key = f"{b['row']}_{b['button']}"
    files = {}
    for ink, m in masks.items():
        fn = f'{key}__{ink}.pgm'; write_pgm(os.path.join(GLYPHS, fn), m); files[ink] = fn
    inks = [INK_ROLE[i] for i in ink_list(b)]
    rec = composite_glyph(None, b, masks, inks, keep).astype(np.int64)
    ref = IMG[b['glyph_y']:b['glyph_y'] + GLYPH, b['glyph_x']:b['glyph_x'] + GLYPH]
    dmax = int(np.abs(rec - ref).max()); nbad = int((np.abs(rec - ref).max(-1) > 0).sum())
    bad_total += nbad
    gindex[key] = dict(row=b['row'], button=b['button'], icon=b['icon'], enabled=b['enabled'], inks=inks, files=files,
                       under=b['under'], size=GLYPH, recon_mismatch_px=nbad, recon_max_err=dmax)
    print(f"glyph {key:34s} {b['icon']:20s} {'enabled ' if enabled else 'DISABLED'} inks {inks} recon mismatch {nbad} px (max {dmax})")
json.dump(dict(note='8-bit coverage per ink (P5 PGM, GLYPHxGLYPH device px); composite each ink in order with cairo mask_surface '
                    'in mix(ink, under, keep), keep = 1 enabled / kRedesignDisabledMix disabled', glyphs=gindex),
          open(os.path.join(GLYPHS, 'index.json'), 'w'), indent=1)
print('glyph reconstruction: total mismatching px', bad_total)

# ------------------------------------------------------------------ menu row text (exact layout of paint_menu_row)
# each item's enabled state is measured: an enabled word carries kRedesignLabel at full coverage somewhere, a disabled
# one never exceeds its mix toward the ground
menu = dict(pad=10 * S, items=[], legend=ARGS.legend, legend_right=C.W - PAD,
            baseline=C.redesign_baseline(C.SANS, C.SANS_PX, 0, 60))
mx_ = 0
for word in ('File', 'Edit', 'Settings'):
    w = int(np.rint(C.shape(C.SANS, C.SANS_PX, word)[1])); box = IMG[0:60, mx_:mx_ + w + 2 * menu['pad']]
    en = bool(is_c(box, LABEL).any())
    if not en and np.abs(box - np.array(GROUND)).max() == 0: mismatch(f'menu item {word}: no ink at x {mx_}..')
    menu['items'].append(dict(text=word, enabled=en)); mx_ += w + 2 * menu['pad']
print('menu', [(i['text'], 'enabled' if i['enabled'] else 'DISABLED') for i in menu['items']], 'legend', repr(menu['legend']))

# ------------------------------------------------------------------ the trim lane (render.cpp's trim painter)
# Back to front, as the app paints it: the ground (its two bevel rows), THE BAR (kTrimLaneBar under a relief
# trim_bar_edge_px thick: light top and left, dark bottom and right, the dark pair painted last), the two HANDLES
# (cap surfaces at the bar's ends, absent for a bound off screen, whose bar then runs one column past the edge), the
# CENTRE GRIP (a cap tile with a bar-coloured hollow, painted only where it fits), the lane's bottom border last.
ty0, ty1 = lanes['trim']
trim = dict(y0=ty0, y1=ty1)
probe_y = ty0 + (ty1 - ty0) // 2                       # a body row under every bevel, through the grip's hollow
r0 = runs_h(ty0)
caps = [[a, b_] for a, b_, c in r0 if c == K['kTrimCapBevelLo']]
def has_hollow(c): return bool(is_c(IMG[probe_y, c[0]:c[1]], K['kTrimLaneBar']).any())
grips = [c for c in caps if has_hollow(c)]; handles = [c for c in caps if not has_hollow(c)]
if len(grips) > 1 or len(handles) > 2: mismatch(f'trim lane: {len(grips)} grips, {len(handles)} handles')
grip = grips[0] if grips else None
trim['caps'] = caps; trim['handles'] = handles; trim['grip'] = grip
def rows_of(x, c, y0=ty0, y1=ty1): return [y for y in range(y0, y1) if col(x, y) == c]
# the bar: its visible columns on the body row, then its painted extent
BAR_CS = (K['kTrimLaneBar'], K['kTrimBarBevelHi'], K['kTrimBarBevelLo'])
in_cap = np.zeros(C.W, bool)
for a, b_ in caps: in_cap[a:b_] = True
barvis = [x for x in range(C.W) if col(x, probe_y) in BAR_CS and not in_cap[x]]
hi_top = [(a, b_) for a, b_, c in r0 if c == K['kTrimBarBevelHi']]
if barvis:
    bx_ = max(hi_top, key=lambda r: r[1] - r[0]); bx_ = (bx_[0] + bx_[1]) // 2      # a bar column away from caps
    trim['bar_bevel_hi_rows'] = rows_of(bx_, K['kTrimBarBevelHi'])
    trim['bar_bevel_lo_rows'] = rows_of(bx_, K['kTrimBarBevelLo'])
    edge = len(trim['bar_bevel_hi_rows'])
    if len(trim['bar_bevel_lo_rows']) != edge or edge != scaled_px(1, 1):
        mismatch(f'trim bar relief: hi rows {trim["bar_bevel_hi_rows"]}, lo rows {trim["bar_bevel_lo_rows"]}, '
                 f'trim_bar_edge_px {scaled_px(1, 1)}')
    trim['bar_edge'] = edge
    v0, v1 = barvis[0], barvis[-1] + 1
    # left end: a begin handle flush against the bar starts the bar on its own column (bar_lo = bc.col); else the bar
    # runs off the left edge, its left relief showing only the columns that land on screen
    lh = [h for h in handles if h[1] == v0]
    if lh: x0 = lh[0][0]
    elif v0 == 0:
        n = 0
        while n < edge and col(n, probe_y) == K['kTrimBarBevelHi']: n += 1
        x0 = n - edge
        if n == 0: mismatch('trim bar: runs off the left edge with no relief column showing')
    else: x0 = v0; mismatch(f'trim bar: ends at x {v0} with no begin handle')
    rh = [h for h in handles if h[0] == v1]
    if rh: x1 = rh[0][1]
    elif v1 == C.W:
        n = 0
        while n < edge and col(C.W - 1 - n, probe_y) == K['kTrimBarBevelLo']: n += 1
        x1 = C.W + edge - n
        if n == 0: mismatch('trim bar: runs off the right edge with no relief column showing')
    else: x1 = v1; mismatch(f'trim bar: ends at x {v1} with no end handle')
    trim['bar'] = [x0, x1]
    if not lh and x0 < 0: note(f'trim bar: the begin bound is off screen, so the bar runs one column past the left edge '
                               f'(bar_lo = -1, render.cpp) and its {edge}-px left relief shows {edge + x0} column(s) at x 0')
    if not rh and x1 > C.W: note(f'trim bar: the end bound is off screen, so the bar runs one column past the right edge '
                                 f'(bar_hi = lane_w + 1) and its {edge}-px right relief shows {C.W + edge - x1} column(s) '
                                 f'at x {C.W - 1}')
else:
    trim['bar'] = None
# the caps' bevel rows (any cap); the ground's bevel rows (a ground column, else the caps': render.cpp's one surface
# lambda paints both with the same lo/hi heights)
gcols = [x for x in range(C.W) if col(x, probe_y) == GROUND]
cap_x = (caps[0][0] + 1) if caps else None
if cap_x is not None:
    trim['cap_bevel_lo_rows'] = rows_of(cap_x, K['kTrimCapBevelLo'])
    trim['cap_bevel_hi_rows'] = rows_of(cap_x, K['kTrimCapBevelHi'])
if gcols:
    trim['ground_bevel_lo_rows'] = rows_of(gcols[0], K['kTrimGroundBevelLo'], ty0, ty0 + 8)
    trim['ground_bevel_hi_rows'] = rows_of(gcols[0], K['kTrimGroundBevelHi'])
    if cap_x is not None and (trim['ground_bevel_lo_rows'], trim['ground_bevel_hi_rows']) != \
            (trim['cap_bevel_lo_rows'], trim['cap_bevel_hi_rows']):
        mismatch('trim lane: the ground\'s bevel rows differ from the caps\'')
else:
    trim['ground_bevel_lo_rows'] = trim['cap_bevel_lo_rows']; trim['ground_bevel_hi_rows'] = trim['cap_bevel_hi_rows']
    note('trim lane: no ground column is visible (the bar spans the screen); the ground\'s bevel rows are taken from the '
         'caps\', which render.cpp paints through the same surface lambda')
if cap_x is None:
    trim['cap_bevel_lo_rows'] = trim['ground_bevel_lo_rows']; trim['cap_bevel_hi_rows'] = trim['ground_bevel_hi_rows']
trim['bottom_border_rows'] = rows_of(3, K['kTrimLaneBottomBorder'], ty0 + 8)
if grip:   # the centre grip: a hollow box in endcap ink — its side walls and the bar showing inside
    gxm = (grip[0] + grip[1]) // 2
    inside = [y for y in range(ty0, ty1) if col(gxm, y) == K['kTrimLaneBar']]
    walls = [x for x in range(grip[0], grip[1]) if col(x, inside[0]) == K['kTrimLaneEndcap']]
    trim['grip_hollow'] = dict(x0=max(x for x in walls if x < gxm) + 1, x1=min(x for x in walls if x > gxm),
                               y0=inside[0], y1=inside[-1] + 1)
print('trim', trim)

# ------------------------------------------------------------------ ruler: ticks and labels
ruler_y0, marker_y0, marker_y1 = lanes['ruler'][0], lanes['marker'][0], lanes['marker'][1]
tick = K['kRulerTick']
head_rows = scaled_px(HEAD_H); head_top = marker_y0 - head_rows
# the label seat: the app's rule (ruler_label_baseline_px), checked against the capture's label ink rows — the digits
# are cap-band ink with no descenders, so the ink is exactly the rows baseline - cap .. baseline - 1, and the ground
# rows from the baseline to the head are empty
ruler_baseline = ruler_y0 + RULER_BASELINE_OFF
ink_rows = [y for y in range(ruler_y0, head_top) if (np.abs(IMG[y] - np.array(GROUND)).sum(-1) > 0).any()]
want_rows = list(range(ruler_baseline - int(np.rint(CAP)), ruler_baseline))
if ink_rows != want_rows:
    mismatch(f'ruler labels: ink rows {ink_rows[0]}..{ink_rows[-1]}, the app\'s seat (baseline {ruler_baseline}, cap '
             f'{int(np.rint(CAP))}) says {want_rows[0]}..{want_rows[-1]} with ground below to the head at {head_top}')
else:
    print(f'ruler labels: ink rows {ink_rows[0]}..{ink_rows[-1]}, baseline {ruler_baseline} (lane top + {RULER_BASELINE_OFF}), '
          f'ground rows {ruler_baseline}..{head_top - 1} empty, head from {head_top}: as the app\'s rule')
maj_rows = [y for y in range(ruler_y0, marker_y0) if is_c(IMG[y], tick).sum() > 4]
major_top = maj_rows[0]
majc = [int(x) for x in np.where(is_c(IMG[major_top + 2], tick))[0]]
majors = sorted(set(x for x in majc if x - 1 not in majc))                 # left column of each 2-px tick
minc = [int(x) for x in np.where(is_c(IMG[marker_y0 + 4], tick))[0]]
minors_seen = sorted(set(x for x in minc if x - 1 not in minc))
# fit the app's own arithmetic: col = nearbyint((t - vp) / mpp), minors mx + nearbyint(i * seg / 8); the first label
# (--first-label) stands on the leftmost measured major
best = None
for mpp_num in np.linspace(STEP / 223.0, STEP / 221.0, 4001):
    off = majors[0] * mpp_num
    vp = FIRST_LABEL_MS - off
    pred = [int(np.rint((FIRST_LABEL_MS + k * STEP - vp) / mpp_num)) for k in range(int(C.W * mpp_num / STEP) + 2)]
    errs = sum(min(abs(p - m) for p in pred) for m in majors)
    if best is None or errs < best[0]: best = (errs, mpp_num, vp)
_, MPP, VP = best
ticks = []
k = int(np.floor(VP / STEP))
while True:
    t = k * STEP
    if t > VP + MPP * C.W: break
    mx = int(np.rint((t - VP) / MPP)); seg = int(np.rint(((k + 1) * STEP - VP) / MPP)) - mx
    for i in range(8):
        c = mx if i == 0 else mx + int(np.rint(i * seg / 8))
        if 0 <= c < C.W: ticks.append(dict(x=c, major=(i == 0), ms=t))
    k += 1
pm = sorted(t['x'] for t in ticks if t['major']); pn = sorted(t['x'] for t in ticks)
hidden = [x for x in pm if x not in majors]          # a major under the playhead head is not seen
if best[0] or not all(x in pm for x in majors): mismatch(f'ruler fit: majors {majors} vs fit {pm}')
if not all(x in pn for x in minors_seen): mismatch(f'ruler fit: minors off the fit {sorted(set(minors_seen) - set(pn))}')
print(f'ruler fit: ms_per_px {MPP:.6f} vp_ms {VP:.3f}; majors measured {len(majors)} at {majors} (+{len(hidden)} hidden '
      f'{hidden}), minors seen {len(minors_seen)} all on the fit; total ticks {len(ticks)}')
labels = []
for t in ticks:
    if not t['major']: continue
    ms = int(round(t['ms'])); s_, mss = divmod(ms, 1000); m_, s_ = divmod(s_, 60)
    labels.append(dict(text=f'{m_}:{s_:02d}.{mss:03d}', x=t['x'] + S + 2 * S))     # col + line px + scaled 2
ruler = dict(y0=ruler_y0, y1=marker_y0, major_top=major_top, minor_top=marker_y0, tick_bottom=marker_y1, tick_w=S,
             baseline=ruler_baseline, ms_per_px=MPP, vp_ms=VP, step_ms=STEP, ticks=ticks, labels=labels)
if major_top != marker_y0 - scaled_px(MAJOR_RISE): mismatch(f'major tick rise: top {major_top}, kRulerMajorRisePx says {marker_y0 - scaled_px(MAJOR_RISE)}')

# ------------------------------------------------------------------ the playhead
# Located by its HEAD on the ruler's bottom rows: kPlayheadHead at kPlayheadHeadAlpha over the ground, the widest row
# (the head's top) spanning col - half[0] .. col + half[0] + S - 1. Its stem (kPlayheadStem, S wide) is the
# cross-check where it shows; where a marker stands on the playhead's column the app suppresses the WHOLE stem
# (playhead_stem_suppressed: the marker's stem wins its column, the marker-lane run included), so the scene records
# stem_suppressed and the renderer leaves the stem out.
hv = tuple(int(round(HEAD_ALPHA * h + (1 - HEAD_ALPHA) * g)) for h, g in zip(K['kPlayheadHead'], GROUND))
head_half = [scaled_px(HALF[min(int(r / S), HEAD_H - 1)], 1) for r in range(head_rows)]
top_cols = [int(x) for x in np.where(is_c(IMG[head_top], hv))[0]]
ph_col = top_cols[0] + head_half[0]
if top_cols[-1] != ph_col + head_half[0] + S - 1: mismatch(f'playhead head top row {head_top}: {top_cols[0]}..{top_cols[-1]}')
for r in range(head_rows):
    y = head_top + r; hw = head_half[r]
    xs = [x for x in range(ph_col - 30, ph_col + 32) if col(x, y) != GROUND and col(x, y) != tick]
    if (min(xs), max(xs)) != (ph_col - hw, ph_col + hw + S - 1): mismatch(f'playhead head row {y}: {min(xs)}..{max(xs)}')
stem_cols = [int(x) for x in np.where(is_c(IMG[canvas[0] + 2], K['kPlayheadStem']))[0]]
playhead = dict(col=ph_col, w=S, head_top=head_top, head_rows=head_rows, head_half=head_half, head_alpha=HEAD_ALPHA,
                stem_y0=well_top, stem_y1=well_bot, stem_suppressed=False)
# (the stem's marker-lane run is painted by the ruler row, under the flags)

# ------------------------------------------------------------------ flags
FILLS = {K['kMarkerFlagFill']: False, K['kMarkerFlagFillSel']: True}
EDGES = {False: K['kMarkerFlagEdge'], True: K['kMarkerFlagEdgeSel']}
fl_border = K['kMarkerFlagBorder']
lb = [int(x) for x in np.where(is_c(IMG[marker_y0 + 10], fl_border))[0]]
borders = []
for x in lb:
    if borders and x == borders[-1][1]: borders[-1][1] = x + 1
    else: borders.append([x, x + 1])
flags = []
# each flag: left border (S px) at bx-S, fill from bx (a left border's column after it carries an edge colour on the
# lane's top row), its right border closing at bx+bw — or past the screen's right edge for a clipped flag
for x0, x1 in borders:
    bx = x1
    if bx >= C.W or col(bx, marker_y0) not in EDGES.values(): continue
    fc = col(bx, marker_y0 + 10)
    if fc not in FILLS: mismatch(f'flag at {bx}: fill {fc} is neither kMarkerFlagFill nor kMarkerFlagFillSel'); continue
    sel = FILLS[fc]
    if col(bx, marker_y0) != EDGES[sel]: mismatch(f'flag at {bx}: edge {col(bx, marker_y0)} does not match its fill')
    flags.append(dict(x=bx, selected=sel))
if len(flags) != len(FLAG_TEXTS): mismatch(f'flags: {len(flags)} measured, {len(FLAG_TEXTS)} texts given (--flags)')
for f, t in zip(flags, FLAG_TEXTS):
    w = C.shape(C.SANS, C.SANS_PX, t)[1]; f['text'] = t
    f['w'] = 2 * S + 2 * S + int(np.rint(w))      # kMarkerFlagPadLeftPx + kMarkerFlagPadRightPx + the shaped width
    f['clipped'] = f['x'] + f['w'] + S > C.W
    # verify the right border lands where the fill stops (a clipped flag's fill must run to the screen's edge)
    fill_c = K['kMarkerFlagFillSel'] if f['selected'] else K['kMarkerFlagFill']
    if not f['clipped']:
        if col(f['x'] + f['w'], marker_y0 + 10) != fl_border or col(f['x'] + f['w'] - 1, marker_y0 + 10) != fill_c:
            mismatch(f'flag at {f["x"]} text {t!r}: width {f["w"]} does not meet its right border')
    elif not all(col(x, marker_y0 + 1) == EDGES[f['selected']] for x in range(f['x'], C.W)):
        mismatch(f'clipped flag at {f["x"]}: its edge does not run to the screen edge')
mid = (canvas[0] + canvas[1]) // 2
stems = [int(x) for x in np.where(is_c(IMG[mid], K['kMarkerFlagFill']) | is_c(IMG[mid], K['kMarkerFlagFillSel']))[0]]
stem_x = sorted(set(x for x in stems if x - 1 not in stems))
if stem_x != [f['x'] for f in flags]: mismatch(f'flag stems at {stem_x}, flags at {[f["x"] for f in flags]}')
for f in flags:
    want = K['kMarkerFlagFillSel'] if f['selected'] else K['kMarkerFlagFill']
    if col(f['x'], mid) != want: mismatch(f'flag at {f["x"]}: its stem is {col(f["x"], mid)}, not its fill {want}')
edge_rows = [y for y in range(marker_y0, marker_y1) if col(flags[0]['x'] + 3, y) == EDGES[flags[0]['selected']]]
flagspec = dict(y0=marker_y0, y1=marker_y1, border_w=S, edge_h=len(edge_rows), pad_l=2 * S,
                baseline=marker_y0 + 16 * S, stem_w=S, stem_y0=well_top, stem_y1=well_bot, flags=flags)
print('flags', [(f['x'], f['w'], f['text'], 'SELECTED' if f['selected'] else '', 'clipped' if f['clipped'] else '')
                for f in flags])

# the playhead's stem: visible (then it must stand on the head's column), or suppressed by a coincident marker stem
if stem_cols:
    if stem_cols != list(range(ph_col, ph_col + S)): mismatch(f'playhead stem cols {stem_cols}, head says {ph_col}')
    print(f'playhead: head at col {ph_col} (rows {head_top}..{marker_y0 - 1}), stem visible at {stem_cols}')
elif ph_col in [f['x'] for f in flags]:
    playhead['stem_suppressed'] = True
    print(f'playhead: head at col {ph_col}; its stem is suppressed by the coincident flag stem at {ph_col} '
          f'(playhead_stem_suppressed)')
else: mismatch(f'playhead at {ph_col}: no stem visible and no marker stem on its column')

# ------------------------------------------------------------------ the clock (paint_bottom_row_buttons_and_clock)
# THE CLOCK'S TEXT IS A PARAMETER AND ITS SEAT IS NOT MEASURED HERE: the captures this reads (a6f53163) painted it in
# Roboto Mono 11 pt, the face that retired from the repository 2026-10-05 with the app's time fields, so its pixels
# cannot be re-rendered and are not checked; render.py draws the clock at today's rule (the time field, Roboto at the
# normal face), whatever the scene records. The baseline and size below are the capture's era's, kept as the scene's
# record: cap-centred in the content rows at that face's measured 21-row cap (gui_font_bundled.cpp's reference table).
cb_h = content_bottom[1] - content_bottom[0]
clock = dict(text=ARGS.clock, x=PAD, baseline=content_bottom[0] + (cb_h + 21) // 2,
             size_px=11.0 * 96.0 / 72.0 * C.SCALE)

# ------------------------------------------------------------------ the texts, re-rendered against the capture
# Every text the parameters name, drawn through the app's road on its ground and compared byte for byte with the
# capture's band: the menu row (items + legend) and the ruler's label band (rows above the head); the clock is not
# (its face retired, above).
def text_band(y0, y1, x0, x1, draws):
    s = cairo.ImageSurface(cairo.FORMAT_RGB24, C.W, C.H); cr = cairo.Context(s)
    C.src(cr, GROUND); cr.paint()
    for family, px, text, x, y, c in draws:
        C.src(cr, c); C.show_text(cr, family, px, text, x, y)
    a = C.surface_to_rgb(s).astype(np.int64)[y0:y1, x0:x1]; b = IMG[y0:y1, x0:x1]
    return int((np.abs(a - b).max(-1) > 0).sum())
mdraws = []; mx_ = 0
for it in menu['items']:
    w = C.shape(C.SANS, C.SANS_PX, it['text'])[1]
    mdraws.append((C.SANS, C.SANS_PX, it['text'], mx_ + menu['pad'], menu['baseline'],
                   LABEL if it['enabled'] else C.mix(LABEL, GROUND, DISABLED_MIX)))
    mx_ += int(np.rint(w)) + 2 * menu['pad']
lw_ = C.shape(C.SANS, C.SANS_PX, menu['legend'])[1]
mdraws.append((C.SANS, C.SANS_PX, menu['legend'], menu['legend_right'] - int(np.rint(lw_)), menu['baseline'], LABEL))
n_menu = text_band(0, 60, 0, C.W, mdraws)
n_rl = text_band(ruler_y0, head_top, 0, C.W, [(C.SANS, C.SANS_PX, l['text'], l['x'], ruler_baseline, K['kRulerLabel'])
                                              for l in labels] + [])
# the ruler band also holds the major ticks' rise only from major_top, below head_top, so the band is text alone
print(f'text check (differing px): menu row + legend {n_menu}, ruler labels {n_rl}')
for n, what in ((n_menu, 'menu row / legend (--legend)'), (n_rl, 'ruler labels (--first-label, --step)')):
    if n: mismatch(f'text check: {what} differs from the capture in {n} px')

# ------------------------------------------------------------------ the waveform: per-column runs
ink_c = K['kWaveformInk']; outl_c = K['kWaveformForegroundOutline']; cv = K['kWaveformCanvas']
area = IMG[canvas[0]:canvas[1]]
known = is_c(area, ink_c) | is_c(area, outl_c) | is_c(area, cv)
unknown_cols = [int(x) for x in np.where(~known.all(0))[0]]
stem_set = set(c for f in flags for c in range(f['x'], min(C.W, f['x'] + S)))
if not playhead['stem_suppressed']: stem_set |= set(range(ph_col, ph_col + S))
if sorted(stem_set) != unknown_cols: mismatch(f'waveform: non-waveform columns {unknown_cols}, stems at {sorted(stem_set)}')
def col_runs(mask_col):
    ys = np.where(mask_col)[0]
    if not len(ys): return []
    br = np.where(np.diff(ys) > 1)[0]; starts = np.r_[ys[0], ys[br + 1]]; ends = np.r_[ys[br], ys[-1]] + 1
    out = []
    for a_, b_ in zip(starts, ends): out += [int(a_), int(b_ - a_)]
    return out
ink_runs, outl_runs, filled = [], [], {}
for x in range(C.W):
    src_x = x
    if x in unknown_cols:   # a stem column: repeat the nearest known neighbour on the same side
        left = x - 1; right = x + 1
        while left in unknown_cols: left -= 1
        while right in unknown_cols: right += 1
        src_x = left if ((x - left) <= (right - x) and left >= 0) or right >= C.W else right
        filled[x] = src_x
    ink_runs.append(col_runs(is_c(area[:, src_x], ink_c)))
    outl_runs.append(col_runs(is_c(area[:, src_x], outl_c)))
n_ink = sum(len(r) // 2 for r in ink_runs); n_out = sum(len(r) // 2 for r in outl_runs)
print(f'waveform: {n_ink} ink runs, {n_out} outline runs, {len(filled)} stem columns filled from neighbours {filled}')
WAVE_NAME = f'waveform_{TAG}.json'
json.dump(dict(y0=canvas[0], h=canvas[1] - canvas[0], channel_split=canvas[0] + (canvas[1] - canvas[0]) // 2,
               filled_columns={str(k): v for k, v in filled.items()}, ink=ink_runs, outline=outl_runs),
          open(os.path.join(OUT, WAVE_NAME), 'w'), separators=(',', ':'))

# ------------------------------------------------------------------ write the scene
enabled = sorted(b['button'] for b in buttons if b['enabled']); disabled = [b['button'] for b in buttons if not b['enabled']]
# `source` is the capture's BASE NAME alone: where the captures live is the README's (Scenes), not a checkout path.
scene = dict(
    tag=TAG, source=os.path.basename(BASE), size=[C.W, C.H], scale=S,
    params=dict(first_label=ARGS.first_label, step_ms=STEP, flags=FLAG_TEXTS, clock=ARGS.clock, legend=ARGS.legend),
    lanes=lanes, table_rows=TABLE, ruler_rule=dict(pad=RULER_PAD, ascent=math.ceil(ASC), ground=scaled_px(RULER_HEAD_GROUND, 1),
                                                   head=scaled_px(HEAD_H), lane=RULER_RULE, baseline_off=RULER_BASELINE_OFF),
    canvas=canvas, well_border_rows=[bt, bb], bottom_border=bottom_border, bottom_content=content_bottom,
    menu=menu, buttons=buttons, separators=seps, glyph_px=GLYPH, glyphs=GLYPH_REL, trim=trim, ruler=ruler,
    playhead=playhead, flags=flagspec, clock=clock, waveform=WAVE_NAME, constants={k: list(v) for k, v in K.items()},
    icon_inks={INK_ROLE[k]: list(v) for k, v in ICON_INK.items()}, disabled_mix=DISABLED_MIX,
    label_box=dict(x0=300, y0=1382, x1=560, y1=1408, note="mock A's file-name label; compare.py excludes it"),
    mismatches=MISMATCH)
json.dump(scene, open(os.path.join(OUT, f'scene_{TAG}.json'), 'w'), indent=1)
print('disabled:', [m['text'] for m in menu['items'] if not m['enabled']], '+', disabled)
print(f'wrote scene_{TAG}.json, {WAVE_NAME} and {len(gindex)} glyphs in {GLYPH_REL}/; {len(MISMATCH)} notes recorded, '
      f'{len(UNEXPLAINED)} UNEXPLAINED mismatches')
sys.exit(1 if UNEXPLAINED else 0)
