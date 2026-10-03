#!/usr/bin/env python3
# tools/palette/render.py — draws the whole 2304x1440 warptempo_gui tablet screen from scratch.
#     python3 tools/palette/render.py <theme.json> <out.png> [--scene <tag>] [--label]
# The scene (geometry, text, ticks, flags, glyph masks, waveform runs) is scene_<tag>.json + its waveform_<tag>.json
# + glyphs/<tag>/ (default tag 1002), all measured off the app's own screencap by extract.py; the theme (README.md)
# sets every colour and the relief. pycairo draws into an RGB24 surface, numpy takes the bytes, and pngrw writes them
# with the Display-P3 iCCP chunk (display_p3.iccp) and nothing else: THE BYTES WRITTEN ARE WHAT THE GLASS SHOWS.
# Deterministic: the same theme and scene give the same bytes.
import os, sys, json, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C            # before cairo: FONTCONFIG_FILE
import cairo
import numpy as np

def scene_tag(argv):
    """--scene <tag> (or --scene=<tag>); the default scene is 1002."""
    for i, a in enumerate(argv):
        if a == '--scene': return argv[i + 1]
        if a.startswith('--scene='): return a.split('=', 1)[1]
    return '1002'
SCENE_TAG = scene_tag(sys.argv)
SCENE = json.load(open(os.path.join(C.HERE, f'scene_{SCENE_TAG}.json')))
BASE_SCENE = SCENE              # as measured; render() rebinds SCENE to it shifted by trim.lane_h and ruler_pad_top / ruler_layout (shift_scene),
                                # its three lane blocks then restacked in lane_order (order_scene)
WAVE = json.load(open(os.path.join(C.HERE, SCENE['waveform'])))
GLYPH_DIR = os.path.join(C.HERE, SCENE.get('glyphs', os.path.join('glyphs', SCENE_TAG)))   # scene_s4.json predates the key
GIDX = json.load(open(os.path.join(GLYPH_DIR, 'index.json')))['glyphs']
S = SCENE['scale']; LW = S                      # one logical line = 2 device px
KC = {k: tuple(v) for k, v in SCENE['constants'].items()}
INKC = {k: tuple(v) for k, v in SCENE['icon_inks'].items()}

# ------------------------------------------------------------------ the theme: roles, defaults, resolution
# A role's default is the scene's app constant (the app before 2026-10-02) as the Gallery showed it, two_pass.
TWO_PASS = lambda name: list(C.two_pass(KC[name]))
DEFAULTS = {
    # grounds
    'ground': TWO_PASS('kRedesignContentGround'), 'row_ground': TWO_PASS('kRedesignRowGround'), 'popup': TWO_PASS('kRedesignPopupGround'),
    'menu_ground': '@ground', 'icon_row_ground': '@ground', 'trim_ground': '@ground', 'ruler_ground': '@ground',
    'marker_ground': '@ground', 'bottom_row_ground': '@ground',
    'icon_face': '@icon_row_ground', 'button_face': '@bottom_row_ground', 'down_face': '@selected_fill',
    # text
    'label': TWO_PASS('kRedesignLabel'), 'legend': '@label', 'menu_disabled': 'auto', 'clock': '@label',
    'ruler_label': TWO_PASS('kRulerLabel'), 'ruler_tick': TWO_PASS('kRulerTick'), 'stamp': [140, 140, 140],
    'ruler_tick_light': 'auto',     # read only by ruler_tick_relief "light_right"
    # lines and fills
    'line': TWO_PASS('kRedesignLine'), 'tab_line': TWO_PASS('kRedesignTabLine'), 'selected_fill': TWO_PASS('kRedesignSelectedFill'),
    'separator': '@tab_line', 'bottom_border': '@tab_line',
    # the trim lane
    'trim_bar': TWO_PASS('kTrimLaneBar'), 'trim_bar_bevel_hi': TWO_PASS('kTrimBarBevelHi'), 'trim_bar_bevel_lo': TWO_PASS('kTrimBarBevelLo'),
    'trim_ground_bevel_hi': TWO_PASS('kTrimGroundBevelHi'), 'trim_ground_bevel_lo': TWO_PASS('kTrimGroundBevelLo'),
    'trim_cap': TWO_PASS('kTrimLaneEndcap'), 'trim_cap_bevel_hi': TWO_PASS('kTrimCapBevelHi'), 'trim_cap_bevel_lo': TWO_PASS('kTrimCapBevelLo'),
    'trim_bottom_border': TWO_PASS('kTrimLaneBottomBorder'),
    # the well and the waveform
    'well_border': TWO_PASS('kWaveformBorder'), 'canvas': TWO_PASS('kWaveformCanvas'), 'ink': TWO_PASS('kWaveformInk'), 'outline': 'auto',
    # playhead and flags
    'playhead_head': TWO_PASS('kPlayheadHead'), 'playhead_stem': TWO_PASS('kPlayheadStem'),
    'flag_fill': TWO_PASS('kMarkerFlagFill'), 'flag_edge': TWO_PASS('kMarkerFlagEdge'), 'flag_border': TWO_PASS('kMarkerFlagBorder'),
    'flag_label': TWO_PASS('kMarkerFlagLabel'), 'flag_stem': '@flag_fill',
    'flag_fill_sel': TWO_PASS('kMarkerFlagFillSel'), 'flag_edge_sel': TWO_PASS('kMarkerFlagEdgeSel'), 'flag_stem_sel': '@flag_fill_sel',
    'flag_hilight': 'auto', 'flag_hilight_sel': 'auto',     # read only by flags.relief "raised"
    # icon inks (icons.cpp), two_pass of each
    'icon_label': '@label', **{f'icon_{k}': list(C.two_pass(v)) for k, v in INKC.items() if k != 'text'},
    # the Windows relief set (only read when something is raised or sunken); 'auto' derives from button_face
    'bevel_hilight': 'auto', 'bevel_light': 'auto', 'bevel_shadow': 'auto', 'bevel_dkshadow': 'auto',
    # the accent (the architect, 2026-10-02: the waveform's ink is the accent); read only by menu.highlight "fill"
    'accent': '@ink',
}
NUM_DEFAULTS = {'disabled_mix': SCENE['disabled_mix'], 'playhead_head_alpha': 0.8, 'canvas_delta': 0, 'ruler_pad_top': 0,
                'ruler_label_pt': 12, 'playhead_head_rows': 12}
OPT_DEFAULTS = {'relief': 'flat', 'separators': 'line', 'ruler_tick_relief': 'none', 'well': 'bordered', 'bottom_border': 'app', 'clock_panel': 'flat',
                'icons': 'app',
                'bars': {'menu': 'flat', 'icon_row': 'flat', 'bottom_row': 'flat'},
                'buttons': {'raised': False, 'rows': ['icon', 'bottom'], 'down': ['ViewTW'], 'down_shift': True,
                            'toggled': None, 'down_dither': False, 'gap': None, 'sep_gap': None, 'group_space': None,
                            'case': None, 'disabled': 'mix'},
                'trim': {'bar': 'app', 'ground': 'app', 'handles': 'app', 'grip': 'app', 'cap_w': None, 'lane_h': None,
                         'style': 'app', 'acid_inset': 1},
                'lane_order': ['trim', 'ruler', 'marker'],
                'ruler_layout': None,
                'menu': {'highlight': None},
                'fonts': {'ui_px': None, 'small_px': None}}
LANES3 = ('trim', 'ruler', 'marker')     # the three lane blocks lane_order restacks (top to bottom)
TRIM_STYLES = ('app', 'acid', 'scrollbar')
MENU_HL_STYLES = ('fill', 'sunken', 'raised')
# the options whose values are checked here (separators, bottom_border, ruler_tick_relief, clock_panel; the older options are not)
OPT_VALUES = {'separators': ('line', 'etched', 'raised', 'none'), 'bottom_border': ('app', 'line', 'etched', 'raised', 'none'),
              'ruler_tick_relief': ('none', 'light_right'), 'clock_panel': ('flat', 'sunken', 'status')}
FLAG_RELIEF = ('none', 'raised')
CASE_KEYS = ('h', 'w', 'glyph', 'pad_x', 'pad_y')
DISABLED_STYLES = ('mix', 'engraved')
COLOUR_SECTIONS = (('waveform', {'ink': 'ink', 'canvas': 'canvas', 'outline': 'outline'}),
                   ('flags', {'fill': 'flag_fill', 'edge': 'flag_edge', 'border': 'flag_border', 'label': 'flag_label', 'stem': 'flag_stem',
                              'fill_sel': 'flag_fill_sel', 'edge_sel': 'flag_edge_sel', 'stem_sel': 'flag_stem_sel',
                              'hilight': 'flag_hilight', 'hilight_sel': 'flag_hilight_sel'}))

class Theme:
    def __init__(self, path):
        t = json.load(open(path)) if path else {}
        self.name = t.get('name', os.path.splitext(os.path.basename(path))[0] if path else 'default')
        raw = dict(DEFAULTS); raw.update(t.get('colours', {}))
        for sec, keys in COLOUR_SECTIONS:
            bad = set(t.get(sec, {})) - set(keys) - ({'relief'} if sec == 'flags' else set())
            if bad: raise SystemExit(f'theme {path}: unknown {sec} keys {sorted(bad)}')
            for k, role in keys.items():
                if k in t.get(sec, {}): raw[role] = t[sec][k]
        self.flag_relief = t.get('flags', {}).get('relief', 'none')
        if self.flag_relief not in FLAG_RELIEF: raise SystemExit(f'theme {path}: flags.relief must be one of {FLAG_RELIEF}')
        unknown = set(raw) - set(DEFAULTS)
        if unknown: raise SystemExit(f'theme {path}: unknown colour roles {sorted(unknown)}')
        self.raw = raw; self.c = {}
        self.num = dict(NUM_DEFAULTS); self.num.update({k: t[k] for k in NUM_DEFAULTS if k in t})
        self.opt = json.loads(json.dumps(OPT_DEFAULTS))
        for k, v in t.items():
            if k in ('colours', 'waveform', 'flags', 'name', 'description') or k in NUM_DEFAULTS: continue
            if k not in OPT_DEFAULTS: raise SystemExit(f'theme {path}: unknown key {k!r}')
            if isinstance(OPT_DEFAULTS[k], dict):
                bad = set(v) - set(OPT_DEFAULTS[k])
                if bad: raise SystemExit(f'theme {path}: unknown {k} keys {sorted(bad)}')
                self.opt[k].update(v)
            else: self.opt[k] = v
        if self.opt['icons'] != 'app':
            raise SystemExit(f'theme {path}: icons must be "app" (the retro icon sets were not shipped), not {self.opt["icons"]!r}')
        b = self.opt['buttons']
        if b['toggled'] is None: b['toggled'] = 'sunken' if b['raised'] else 'app'
        bg = b['gap']
        if bg is not None and (not isinstance(bg, int) or isinstance(bg, bool) or bg < 0):
            raise SystemExit(f'theme {path}: buttons.gap is null (the scene\'s measured positions) or a whole number of logical px >= 0 '
                             f'between adjacent buttons of one group, not {bg!r}')
        sg = b['sep_gap']
        if sg is not None and (not isinstance(sg, int) or isinstance(sg, bool) or sg < 0):
            raise SystemExit(f'theme {path}: buttons.sep_gap is null (the scene\'s measured separator gaps) or a whole number of logical px '
                             f'>= 0 added to every separator gap on both sides, not {sg!r}')
        gs = b['group_space']
        if gs is not None and (not isinstance(gs, int) or isinstance(gs, bool) or gs < 0):
            raise SystemExit(f'theme {path}: buttons.group_space is null (the scene\'s measured separator gaps) or a whole number of logical '
                             f'px >= 0 of empty ground between adjacent groups, not {gs!r}')
        if gs is not None and sg is not None:
            raise SystemExit(f'theme {path}: buttons.group_space and buttons.sep_gap are exclusive (group_space is the whole space between groups)')
        if gs is not None and self.opt['separators'] != 'none':
            raise SystemExit(f'theme {path}: buttons.group_space needs separators "none" (no separator stands between the groups), '
                             f'not {self.opt["separators"]!r}')
        cs = b['case']
        if cs is not None:
            if (not isinstance(cs, dict) or set(cs) != set(CASE_KEYS)
                    or not all(isinstance(cs[k], int) and not isinstance(cs[k], bool) for k in CASE_KEYS)
                    or min(cs['h'], cs['w'], cs['glyph']) < 1 or min(cs['pad_x'], cs['pad_y']) < 0
                    or cs['glyph'] + cs['pad_x'] > cs['w'] or cs['glyph'] + cs['pad_y'] > cs['h']):
                raise SystemExit(f'theme {path}: buttons.case is null or {{"h": H, "w": W, "glyph": G, "pad_x": PX, "pad_y": PY}}, '
                                 f'whole logical px (H, W, G >= 1, PX, PY >= 0, G + PX <= W, G + PY <= H), not {cs!r}')
        if b['disabled'] not in DISABLED_STYLES:
            raise SystemExit(f'theme {path}: buttons.disabled must be one of {DISABLED_STYLES}, not {b["disabled"]!r}')
        self.button_geometry = None     # button_geometry's memo: (buttons, separators), computed at the first painter's call
        for k, vals in OPT_VALUES.items():
            if self.opt[k] not in vals: raise SystemExit(f'theme {path}: {k} must be one of {vals}, not {self.opt[k]!r}')
        for r in DEFAULTS: self.get(r)
        # the well: "bordered" | "sunken" | {"top": [line, ...], "bottom": [line, ...]} (one LW line each, top to bottom)
        w = self.opt['well']
        if isinstance(w, dict):
            if set(w) != {'top', 'bottom'} or not all(isinstance(w[k], list) for k in w):
                raise SystemExit(f'theme {path}: the list-form well is {{"top": [colour, ...], "bottom": [colour, ...]}}, not {w!r}')
            self.well_lines = {k: [self.colour(v) for v in w[k]] for k in ('top', 'bottom')}
        elif w in ('bordered', 'sunken'): self.well_lines = None
        else: raise SystemExit(f'theme {path}: well must be "bordered", "sunken" or the list form, not {w!r}')
        cw = self.opt['trim']['cap_w']
        if cw is not None and (not isinstance(cw, int) or isinstance(cw, bool) or cw < 1):
            raise SystemExit(f'theme {path}: trim.cap_w is a whole number of logical px >= 1, not {cw!r}')
        lh = self.opt['trim']['lane_h']; mh = (BASE_SCENE['trim']['y1'] - BASE_SCENE['trim']['y0']) // S
        ts = self.opt['trim']['style']
        if ts not in TRIM_STYLES: raise SystemExit(f'theme {path}: trim.style must be one of {TRIM_STYLES}, not {ts!r}')
        if ts == 'scrollbar':   # the lane may also shrink: the thumb needs its relief lines top and bottom, nothing else
            lines = {'flat': 1, 'thin': 1, 'thick': 2}[self.opt['relief']]
            if lh is not None and (not isinstance(lh, int) or isinstance(lh, bool) or lh < 2 * lines):
                raise SystemExit(f'theme {path}: trim.lane_h with style "scrollbar" is a whole number of logical rows >= {2 * lines} '
                                 f'(the thumb\'s {lines}-line relief top and bottom), not {lh!r}')
        elif lh is not None and (not isinstance(lh, int) or isinstance(lh, bool) or lh < mh):
            raise SystemExit(f'theme {path}: trim.lane_h is a whole number of logical rows >= the scene\'s measured {mh} '
                             f'(the lane only grows: the ruler, marker lane and well top move down, the canvas gives up the rows), not {lh!r}')
        lo = self.opt['lane_order']
        if not isinstance(lo, list) or sorted(lo) != sorted(LANES3):
            raise SystemExit(f'theme {path}: lane_order is a list of the three names {list(LANES3)} in top-to-bottom order, not {lo!r}')
        ai = self.opt['trim']['acid_inset']
        lane_rows = lh if lh is not None else mh
        if not isinstance(ai, int) or isinstance(ai, bool) or ai < 0 or 2 * ai >= lane_rows:
            raise SystemExit(f'theme {path}: trim.acid_inset is a whole number of logical rows >= 0 leaving the bar at least one '
                             f'row of the lane\'s {lane_rows}, not {ai!r}')
        d = self.num['canvas_delta']
        if not isinstance(d, int) or isinstance(d, bool): raise SystemExit(f'theme {path}: canvas_delta is a whole number of logical rows, not {d!r}')
        lp = self.num['ruler_label_pt']
        if not isinstance(lp, (int, float)) or isinstance(lp, bool) or not lp > 0:
            raise SystemExit(f'theme {path}: ruler_label_pt is a point size > 0 (the timestamps\' face, 12 = the app\'s), not {lp!r}')
        hr = self.num['playhead_head_rows']; hmax = BASE_SCENE['playhead']['head_rows'] // S
        if not isinstance(hr, int) or isinstance(hr, bool) or not 1 <= hr <= hmax:
            raise SystemExit(f'theme {path}: playhead_head_rows is a whole number of logical rows 1..{hmax} (the head kept on the '
                             f'lane\'s bottom, its widest top rows dropped; {hmax} = the app\'s), not {hr!r}')
        hl = self.opt['menu']['highlight']
        if hl is not None:
            items = [it['text'] for it in BASE_SCENE['menu']['items']]
            if not isinstance(hl, dict) or set(hl) - {'item', 'style', 'label'} or 'item' not in hl:
                raise SystemExit(f'theme {path}: menu.highlight is null or {{"item": ..., "style": ..., "label": colour}}, not {hl!r}')
            if hl['item'] not in items: raise SystemExit(f'theme {path}: menu.highlight.item must be one of {items}, not {hl["item"]!r}')
            hl.setdefault('style', 'fill'); hl.setdefault('label', '@label')
            if hl['style'] not in MENU_HL_STYLES:
                raise SystemExit(f'theme {path}: menu.highlight.style must be one of {MENU_HL_STYLES}, not {hl["style"]!r}')
            self.menu_hl_label = self.colour(hl['label'])
        rp = self.num['ruler_pad_top']
        if not isinstance(rp, int) or isinstance(rp, bool) or rp < 0:
            raise SystemExit(f'theme {path}: ruler_pad_top is a whole number of logical rows >= 0 (ground added at the ruler lane\'s top: '
                             f'the labels, ticks, head, marker lane and well top move down, the canvas gives up the rows), not {rp!r}')
        fo = self.opt['fonts']
        for k in ('ui_px', 'small_px'):
            v = fo[k]
            if v is not None and (not isinstance(v, int) or isinstance(v, bool) or v < 1):
                raise SystemExit(f'theme {path}: fonts.{k} is null (the app\'s face) or a whole number of logical px > 0, not {v!r}')
        if fo['small_px'] is not None and 'ruler_label_pt' in t:
            raise SystemExit(f'theme {path}: fonts.small_px and ruler_label_pt both size the ruler labels; keep one')
        rl = self.opt['ruler_layout']
        if rl is not None:
            if 'ruler_pad_top' in t:
                raise SystemExit(f'theme {path}: ruler_layout and ruler_pad_top are one or the other (ruler_layout sets the ground '
                                 f'above the digits directly; drop ruler_pad_top)')
            if (not isinstance(rl, dict) or set(rl) != {'above', 'below'}
                    or not all(isinstance(rl[k], int) and not isinstance(rl[k], bool) and rl[k] >= 0 for k in rl)):
                raise SystemExit(f'theme {path}: ruler_layout is null or {{"above": A, "below": B}}, whole logical rows >= 0 '
                                 f'(ground from the lane\'s top to the digits\' cap top, and from their baseline to the marker lane), not {rl!r}')

    def colour(self, v):
        """Any colour value the schema accepts, outside the role table: '@role', '#rrggbb', 'srgb:#rrggbb', [r, g, b]."""
        if isinstance(v, str) and v.startswith('@'):
            if v[1:] not in self.raw: raise SystemExit(f'unknown colour role {v!r}')
            return self.get(v[1:])
        if v == 'auto': raise SystemExit('"auto" names a role\'s own rule; a well line takes a colour or "@role"')
        return C.parse_colour(v)

    def get(self, role, _stack=()):
        if role in self.c: return self.c[role]
        if role in _stack: raise SystemExit(f'colour reference loop at {role}')
        v = self.raw[role]
        if isinstance(v, str) and v.startswith('@'): c = self.get(v[1:], _stack + (role,))
        elif v == 'auto': c = self.auto(role, _stack + (role,))
        else: c = C.parse_colour(v)
        self.c[role] = tuple(c); return self.c[role]

    def auto(self, role, st):
        if role == 'menu_disabled': return C.mix(self.get('label', st), self.get('menu_ground', st), self.num['disabled_mix'])
        if role == 'outline': return C.outline_of(self.get('ink', st), self.get('canvas', st))
        if role in ('flag_hilight', 'flag_hilight_sel'):     # a lighter flag colour: 35 % toward white in linear light
            return tuple(C.lin_mix(self.get(role.replace('hilight', 'fill'), st), (255, 255, 255), 0.35))
        if role == 'ruler_tick_light':                       # a lighter tick colour, the same rule
            return tuple(C.lin_mix(self.get('ruler_tick', st), (255, 255, 255), 0.35))
        f = self.get('button_face', st)
        if role == 'bevel_hilight': return tuple(min(255, round(v * 1.75)) for v in f)      # Redmond97 Dark: #373737 -> #606060
        if role == 'bevel_light': return f                                                # the Windows 2000 rule: 3DLight = face
        if role == 'bevel_shadow': return tuple(round(v * 0.69) for v in f)               # #373737 -> #262626
        if role == 'bevel_dkshadow': return (0, 0, 0)
        raise SystemExit(f'no auto rule for {role}')

def lane_shift(th):
    """trim.lane_h -> d, the device rows the trim lane grows by (0 at the default: the scene's measured height)."""
    lh = th.opt['trim']['lane_h']
    return 0 if lh is None else (lh - (BASE_SCENE['trim']['y1'] - BASE_SCENE['trim']['y0']) // S) * LW

def ruler_label_px(th):
    """ruler_label_pt -> the timestamps' face in device px: pt x 96 / 72 x S (12 pt = 32 px, the app's); fonts.small_px,
    when set, replaces it: small_px x S (the theme refuses both)."""
    sp = th.opt['fonts']['small_px']
    if sp is not None: return float(sp * S)
    return th.num['ruler_label_pt'] * 96.0 / 72.0 * S

def ui_font_px(th):
    """fonts.ui_px -> the normal face in device px: ui_px x S, or the app's C.SANS_PX (12 pt x 2 = 32) when null. It
    drives the menu items and legend (draw_menu), the flag labels (flag_seat, draw_flags) and, by the same ratio, the
    clock (seat_clock); the --label stamp keeps its 20 px."""
    up = th.opt['fonts']['ui_px']
    return C.SANS_PX if up is None else float(up * S)

def flag_seat(th):
    """fonts.ui_px -> None (null: the scene's flags as measured) or (box_h, lane_h, edge+ascent): the flag box's height,
    the marker lane's height and the label's baseline offset under the box's top, device rows. THE MEASURED PADS: the
    app seats a flag label as a text line under the box's dark top band (paint_handler.cpp: baseline = box top + edge_h +
    ceil(ascent), 228 + 2 + 30 = 260 on 1002) and the box ends at the descent line (2 + 30 + 8 = 40, the lane's 20 logical
    rows, no ground above or below). At the new face: need = edge_h + ceil(ascent) + ceil(descent). need = the measured
    box (the app's face): the box and the lane as measured. Otherwise the box is need rounded up to whole logical rows
    and the lane follows the text BOTH WAYS, one logical row of ground above and below the box: a larger face grows it,
    lane = max(measured, box + 2 x LW); a smaller one shrinks it, lane = min(measured, box + 2 x LW) (a box within
    two logical rows of the measured one keeps the measured lane, the box centred in it). The lane's difference from
    the measured one is opened (or closed, negative) at its bottom (shift_scene 'marker') and the box centred in it."""
    if th.opt['fonts']['ui_px'] is None: return None
    F = BASE_SCENE['flags']; px = ui_font_px(th)
    asc, desc = C.font_extents(C.SANS, px)[:2]
    off = F['edge_h'] + math.ceil(asc); need = off + math.ceil(desc)
    box = F['y1'] - F['y0']; lane = BASE_SCENE['lanes']['marker'][1] - BASE_SCENE['lanes']['marker'][0]
    if need == box: return box, lane, off
    grown = need > box; box = -(-need // LW) * LW
    return box, (max if grown else min)(lane, box + 2 * LW), off

def marker_shift(th):
    """flag_seat -> d, the device rows the marker lane grows by (negative: shrinks by; 0 at the app's face)."""
    fs = flag_seat(th)
    return 0 if fs is None else fs[1] - (BASE_SCENE['lanes']['marker'][1] - BASE_SCENE['lanes']['marker'][0])

def seat_flags(sc, th):
    """The scene with its flag boxes' rows and label baseline set by flag_seat (after the 'marker' shift, before
    order_scene restacks): the box centred in the marker lane, the baseline edge_h + ceil(ascent) under its top.
    fonts.ui_px null returns the scene itself."""
    fs = flag_seat(th)
    if fs is None: return sc
    box, lane, off = fs
    sc = json.loads(json.dumps(sc)); F = sc['flags']; m0, m1 = sc['lanes']['marker']
    F['y0'] = m0 + (m1 - m0 - box) // 2; F['y1'] = F['y0'] + box; F['baseline'] = F['y0'] + off
    return sc

def seat_clock(sc, th):
    """fonts.ui_px -> the clock's Roboto Mono at the scene's measured size_px x ui_px x S / C.SANS_PX (the normal face's
    ratio), re-seated by the app's rule over the bottom row's content rows (redesign_baseline, as shift_scene 'bottom'
    re-seats it: the baseline moves by the rule's change). Null returns the scene itself."""
    if th.opt['fonts']['ui_px'] is None: return sc
    sc = json.loads(json.dumps(sc)); ck = sc['clock']; bc = sc['bottom_content']
    px = ck['size_px'] * ui_font_px(th) / C.SANS_PX
    ck['baseline'] += (C.redesign_baseline(C.MONO, px, bc[0], bc[1] - bc[0])
                       - C.redesign_baseline(C.MONO, ck['size_px'], bc[0], bc[1] - bc[0]))
    ck['size_px'] = px
    return sc

def head_rows_drawn(th):
    """playhead_head_rows -> (first, rows): the drawn head is the LAST rows device rows of the scene's head_half list
    (its tip's rows unchanged, the head still seated on the lane's bottom), the widest top (12 - N) x S rows dropped."""
    rows = th.num['playhead_head_rows'] * S
    return BASE_SCENE['playhead']['head_rows'] - rows, rows

def ruler_layout_rows(th):
    """ruler_layout -> (above, cap, below) in device rows, or None (the default: the app's seat rule). above = the
    ground from the ruler lane's top to the digits' cap top, cap = nearbyint(the face's cap height) at ruler_label_pt,
    below = the ground from the labels' baseline (the row after the last cap row) to the marker lane's top; the lane is
    their sum. The head keeps its seat on the lane's bottom rows, so a lane shorter than the head is refused."""
    rl = th.opt['ruler_layout']
    if rl is None: return None
    cap = int(np.rint(C.cap_height(C.SANS, ruler_label_px(th))))
    rows = (rl['above'] * LW, cap, rl['below'] * LW)
    hd = head_rows_drawn(th)[1]
    if sum(rows) < hd:
        raise SystemExit(f'ruler_layout {rl}: the lane would be {sum(rows)} device rows ({rows[0]} + cap {cap} + {rows[2]}), '
                         f'shorter than the playhead head\'s {hd}, which keeps its seat on '
                         f'the lane\'s bottom rows')
    return rows

def ruler_shortfall(th):
    """fonts.small_px on the app's seat rule (no ruler_layout) -> the device rows the ruler lane changes by to seat the
    labels with the measured pads, following the text BOTH WAYS. It grows by what it lacks: the seat (ruler_seat_rows:
    the cap top kRulerLabelCapTopPx under the lane's top, then ceil(ascent)) + the scene's head ground
    (kRulerHeadGroundPx x S, ruler_rule 'ground') + the drawn head, less the scene's lane (56 = 30 + 2 + 24 on 1002).
    When that holds them it shrinks (negative) by the seat rows a face smaller than the app's saves, ruler_seat_rows(px)
    - ruler_seat_rows(32) (the head's measured rows and its ground kept under the labels; 0 at the app's face and
    above it). 0 with ruler_layout (which sizes the lane from the face itself, both ways) and without small_px
    (ruler_label_pt keeps the lane: see What is approximated)."""
    if th.opt['fonts']['small_px'] is None or th.opt['ruler_layout'] is not None: return 0
    lane = BASE_SCENE['lanes']['ruler'][1] - BASE_SCENE['lanes']['ruler'][0]
    seat = ruler_seat_rows(ruler_label_px(th))
    need = seat + BASE_SCENE['ruler_rule']['ground'] + head_rows_drawn(th)[1]
    return need - lane if need > lane else min(0, seat - ruler_seat_rows(C.SANS_PX))

def pad_shift(th):
    """ruler_pad_top or ruler_layout -> d, the device rows the ruler lane grows by, opened at its top (0 at the
    default). ruler_layout's d is its lane (above + cap + below) less the scene's measured lane, and may be negative:
    the lane shrinks and the canvas takes the rows (shift_scene); the labels' seat is then ruler_label_seat's.
    ruler_shortfall's rows are added to the pad's (opened at the top too, or closed there when negative;
    ruler_label_seat takes them back off the labels' seat, so they land under the digits, above the head)."""
    rows = ruler_layout_rows(th)
    if rows is None: return th.num['ruler_pad_top'] * LW + ruler_shortfall(th)
    return sum(rows) - (BASE_SCENE['lanes']['ruler'][1] - BASE_SCENE['lanes']['ruler'][0])

def case_delta(th):
    """buttons.case -> (icon d, bottom d), the device rows each button row grows by: H x S less the row's measured
    button height (64 on every scene), so the row keeps its measured air above and below its buttons (14 device rows
    each on every scene: the icon row's buttons 74..138 in 60..152, the bottom row's 1362..1426 in its content rows
    1348..1440) and its lane is H x S + that air. (0, 0) at the default."""
    cs = th.opt['buttons']['case']
    if cs is None: return 0, 0
    out = []
    for row in ('icon', 'bottom'):
        hs = {b['h'] for b in BASE_SCENE['buttons'] if b['row'] == row}
        if len(hs) != 1: raise SystemExit(f'scene {SCENE_TAG}: the {row} row\'s buttons have heights {sorted(hs)}; buttons.case needs one')
        out.append(cs['h'] * S - hs.pop())
    return tuple(out)

TRIM_ROW_KEYS = ('bar_bevel_hi_rows', 'bar_bevel_lo_rows', 'cap_bevel_lo_rows', 'cap_bevel_hi_rows', 'ground_bevel_lo_rows',
                 'ground_bevel_hi_rows', 'bottom_border_rows')

def move_trim(L, T, d):
    """The trim lane moved whole by d device rows: its rows and every recorded row inside it (order_scene, shift_scene 'icon')."""
    L['trim'] = [v + d for v in L['trim']]; T['y0'] += d; T['y1'] += d
    for k in TRIM_ROW_KEYS:
        if k in T: T[k] = [r + d for r in T[k]]
    if T.get('grip_hollow'): T['grip_hollow']['y0'] += d; T['grip_hollow']['y1'] += d

def shift_scene(sc, d, at):
    """The scene with d device rows opened at `at`, everything below the opening moved down by d. The menu, the icon
    row, the bottom row and the well's bottom stay; the ruler lane's contents (ticks, labels, head), the marker lane,
    its flags, the playhead's marker-lane run and the stems' tops move down by d; the well's top and the measured
    canvas top follow, so the canvas loses d rows (the waveform replays through rescale_runs).
      at 'icon' (buttons.case, case_delta's icon d): the icon row d rows taller, opened at its bottom (its top and its
        buttons' y stay, so the air above the buttons is kept and the air below follows the taller box); the trim
        lane moves down whole, every recorded row inside it with it (move_trim), and the ruler lane whole. d may be
        negative (the row shorter, everything below it moved up, the canvas taller).
      at 'trim' (trim.lane_h): the trim lane d rows taller. Its bottom (its bottom border, the bar's lower bevel) moves
        down by d, its top rows (the ground's and the caps' bevels, the bar's upper bevel, the app grip's square
        hollow) stay; the ruler lane keeps its height and moves down whole.
      at 'ruler' (ruler_pad_top, ruler_layout, fonts.small_px): d rows of ruler ground inserted at the ruler lane's
        top. The trim lane and the ruler lane's top stay; the ruler lane is d rows taller. ruler_layout's d and
        ruler_shortfall's may be negative (the lane shorter, everything below it moved up, the canvas taller); the label
        baseline is re-seated by ruler_label_seat.
      at 'bottom' (buttons.case, case_delta's bottom d): the OTHER direction -- the bottom row d rows taller, opened
        at its top: the row's top (its border-top, the content rows' top) moves UP by d, and with it the well's bottom,
        the measured canvas bottom, the stems' bottoms, the row's buttons and separators (their air above kept), and
        the --label stamp's box; the clock's baseline is re-seated by the app's rule over the new content rows
        (redesign_baseline, paint_handler.cpp's clock: box = the content rows' top .. their bottom, the cap band
        centred), which keeps it centred on the buttons as well, their air being equal above and below. The menu and
        everything from the icon row to the well's top stay, so the canvas loses d rows (negative d: gains).
      at 'marker' (fonts.ui_px, marker_shift): the marker lane d rows taller, opened at its BOTTOM (applied in the
        default lane order, the marker lane directly over the well, before order_scene): the lane's bottom, the ticks'
        bottom (every tick runs down the lane), the stems' tops, the well's top and the canvas top move down by d; the
        flags' rows are seat_flags'. d may be negative (a smaller face: the lane shorter, the well's top and the canvas
        top moved up, the canvas taller).
    d = 0 returns the scene itself."""
    if d == 0: return sc
    if at not in ('icon', 'trim', 'ruler', 'bottom', 'marker'): raise ValueError(at)
    sc = json.loads(json.dumps(sc)); L = sc['lanes']; R = sc['ruler']
    if at == 'marker':
        L['marker'][1] += d; R['tick_bottom'] += d; L['well'][0] += d; sc['canvas'][0] += d
        sc['flags']['stem_y0'] += d; sc['playhead']['stem_y0'] += d
        return sc
    if at == 'bottom':
        u = -d; bc = sc['bottom_content']; ck = sc['clock']
        old = C.redesign_baseline(C.MONO, ck['size_px'], bc[0], bc[1] - bc[0])
        new = C.redesign_baseline(C.MONO, ck['size_px'], bc[0] + u, bc[1] - bc[0] - u)
        ck['baseline'] += new - old
        bc[0] += u; L['bottom'][0] += u; sc['bottom_border']['y'] += u
        L['well'][1] += u; sc['canvas'][1] += u
        sc['flags']['stem_y1'] += u; sc['playhead']['stem_y1'] += u
        for b in sc['buttons']:
            if b['row'] == 'bottom': b['y'] += u; b['glyph_y'] += u
        for s_ in sc['separators']:
            if s_['row'] == 'bottom': s_['y'] += u
        sc['label_box']['y0'] += u; sc['label_box']['y1'] += u
        return sc
    if at == 'icon':
        L['icon'][1] += d
        move_trim(L, sc['trim'], d)
        L['ruler'][0] += d; R['y0'] += d
    if at == 'trim':
        T = sc['trim']
        L['trim'][1] += d
        T['y1'] += d
        T['bar_bevel_lo_rows'] = [r + d for r in T['bar_bevel_lo_rows']]
        T['bottom_border_rows'] = [r + d for r in T['bottom_border_rows']]
        L['ruler'][0] += d; R['y0'] += d
    L['ruler'][1] += d; R['y1'] += d
    L['marker'] = [L['marker'][0] + d, L['marker'][1] + d]
    L['well'][0] += d; sc['canvas'][0] += d
    for k in ('major_top', 'minor_top', 'tick_bottom', 'baseline'): R[k] += d
    P = sc['playhead']; P['head_top'] += d; P['stem_y0'] += d
    F = sc['flags']
    for k in ('y0', 'y1', 'baseline', 'stem_y0'): F[k] += d
    return sc

def order_scene(sc, order):
    """The scene with its three lane blocks (trim, ruler, marker: contiguous, in that order, between the icon row and
    the well) restacked top to bottom in `order`, each keeping its height (after trim.lane_h and ruler_pad_top / ruler_layout) and
    carrying its own content: the trim lane its ground, frame, bar and caps (every recorded row); the ruler lane its
    ground, labels, the playhead head and the majors' rise; the marker lane its ground, its flags, the ticks' marker-lane
    part and the playhead's marker-lane run. The well, the canvas, the stems' tops (the well's top) and the bottom row
    do not move, so between a moved marker lane and the well no stem is drawn: the lane in between covers that run.
    The ticks: a major is one rect from major_top to tick_bottom while the ruler sits directly above the marker lane
    (both blocks then move by the same amount); otherwise it splits into the rise (major_top .. minor_top, carried by
    the ruler lane: its bottom rows) and the marker-lane part (minor_top .. tick_bottom, carried by the marker lane),
    recorded as R['rise'] = [top, bottom]. The default order returns the scene itself."""
    if list(order) == list(LANES3): return sc
    sc = json.loads(json.dumps(sc)); L = sc['lanes']; R = sc['ruler']; T = sc['trim']; F = sc['flags']; P = sc['playhead']
    for a, b in zip(LANES3, LANES3[1:]):
        if L[a][1] != L[b][0]: raise SystemExit(f'scene {SCENE_TAG}: the {a} and {b} lanes are not contiguous ({L[a]}, {L[b]})')
    y = L['trim'][0]; dd = {}
    for n in order: dd[n] = y - L[n][0]; y += L[n][1] - L[n][0]
    if dd['ruler'] != dd['marker']: R['rise'] = [R['major_top'] + dd['ruler'], R['minor_top'] + dd['ruler']]
    move_trim(L, T, dd['trim'])          # the trim lane: its rows and every recorded row inside it
    d = dd['ruler']                      # the ruler lane: labels, the majors' rise, the head
    L['ruler'] = [v + d for v in L['ruler']]; R['y0'] += d; R['y1'] += d; R['baseline'] += d; R['major_top'] += d
    P['head_top'] += d
    d = dd['marker']                     # the marker lane: flags, the ticks' marker-lane part (the playhead's run reads lanes.marker)
    L['marker'] = [v + d for v in L['marker']]; R['minor_top'] += d; R['tick_bottom'] += d
    for k in ('y0', 'y1', 'baseline'): F[k] += d
    return sc

# ------------------------------------------------------------------ drawing primitives (whole device px only)
def fill(cr, x0, y0, x1, y1, c):
    if x1 <= x0 or y1 <= y0: return
    C.src(cr, c); cr.rectangle(x0, y0, x1 - x0, y1 - y0); cr.fill()

def edge(cr, x0, y0, x1, y1, lines, sides='ltrb', lw=LW):
    """DrawEdge: lines = [(outer TL, outer BR), (inner TL, inner BR)], each lw device px, TL first and BR last
    (BR owns the top-right and bottom-left corners), each pair inset one line."""
    for i, (tl, br) in enumerate(lines):
        a, b, c, d = x0 + i * lw, y0 + i * lw, x1 - i * lw, y1 - i * lw
        if tl is not None:
            if 't' in sides: fill(cr, a, b, c, b + lw, tl)
            if 'l' in sides: fill(cr, a, b, a + lw, d, tl)
        if br is not None:
            if 'b' in sides: fill(cr, a, d - lw, c, d, br)
            if 'r' in sides: fill(cr, c - lw, b if 't' in sides else y0, c, d, br)

def relief_lines(th, kind):
    """The Windows DrawEdge grammar (README.md, the edge grammar), thin = one line a side."""
    h, l, s, d = (th.get('bevel_hilight'), th.get('bevel_light'), th.get('bevel_shadow'), th.get('bevel_dkshadow'))
    thick = th.opt['relief'] == 'thick'
    if kind == 'button':    # EDGE_RAISED + BF_SOFT (DrawFrameControl push button)
        return [(h, d), (l, s)] if thick else [(h, s)]
    if kind == 'pressed':   # EDGE_SUNKEN + BF_SOFT
        return [(d, h), (s, l)] if thick else [(s, h)]
    if kind == 'panel':     # EDGE_RAISED
        return [(l, d), (h, s)] if thick else [(h, s)]
    if kind == 'sunken':    # EDGE_SUNKEN (wells, status panels)
        return [(s, h), (d, l)] if thick else [(s, h)]
    raise ValueError(kind)

def show(cr, family, px, text, x, y, c):
    C.src(cr, c); return C.show_text(cr, family, px, text, x, y)

# ------------------------------------------------------------------ the lanes
def draw_grounds(cr, th):
    L = SCENE['lanes']
    fill(cr, 0, 0, C.W, C.H, th.get('ground'))
    fill(cr, 0, L['menu'][0], C.W, L['menu'][1], th.get('menu_ground'))
    fill(cr, 0, L['icon'][0], C.W, L['icon'][1], th.get('icon_row_ground'))
    fill(cr, 0, L['trim'][0], C.W, L['trim'][1], th.get('trim_ground'))
    fill(cr, 0, L['ruler'][0], C.W, L['ruler'][1], th.get('ruler_ground'))
    fill(cr, 0, L['marker'][0], C.W, L['marker'][1], th.get('marker_ground'))
    fill(cr, 0, L['bottom'][0], C.W, L['bottom'][1], th.get('bottom_row_ground'))

def draw_menu(cr, th):
    """The anchors walk flush from x 0, adjacent, each nearbyint(shaped width) + 2 x pad wide and the whole menu lane
    tall (paint_menu_row: THE LANE IS THE PILL). menu.highlight puts a face on one of them: "fill" = that rectangle,
    square-cornered, in `accent`, the label over it in highlight.label; "sunken" / "raised" = one thin relief line a
    side on it (Shadow then Hilight / the reverse: Windows 98's hot-tracked menu title, sunken when open, raised on
    hover), the label unchanged. The legend and the other anchors are untouched. fonts.ui_px sets the face (ui_font_px;
    the anchors' widths follow the shaped text) and re-seats the baseline by the app's rule over the whole menu lane
    (redesign_baseline: the cap band centred; 41 at 32 px, the measured baseline)."""
    m = SCENE['menu']; x = 0; px = ui_font_px(th); y0, y1 = SCENE['lanes']['menu']
    base = m['baseline'] if th.opt['fonts']['ui_px'] is None else C.redesign_baseline(C.SANS, px, y0, y1 - y0)
    hl = th.opt['menu']['highlight']
    for it in m['items']:
        w = C.shape(C.SANS, px, it['text'])[1]; bw = int(np.rint(w)) + 2 * m['pad']
        col = th.get('label') if it['enabled'] else th.get('menu_disabled')
        if hl is not None and hl['item'] == it['text']:
            if hl['style'] == 'fill':
                fill(cr, x, y0, x + bw, y1, th.get('accent')); col = th.menu_hl_label
            else:
                s_, h_ = th.get('bevel_shadow'), th.get('bevel_hilight')
                edge(cr, x, y0, x + bw, y1, [(s_, h_)] if hl['style'] == 'sunken' else [(h_, s_)])
        show(cr, C.SANS, px, it['text'], x + m['pad'], base, col)
        x += bw
    w = C.shape(C.SANS, px, m['legend'])[1]
    show(cr, C.SANS, px, m['legend'], m['legend_right'] - int(np.rint(w)), base, th.get('legend'))

def draw_bars(cr, th):
    L = SCENE['lanes']; bars = th.opt['bars']
    for key, (y0, y1) in (('menu', L['menu']), ('icon_row', L['icon']), ('bottom_row', (SCENE['bottom_content'][0], C.H))):
        if bars.get(key, 'flat') == 'raised': edge(cr, 0, y0, C.W, y1, relief_lines(th, 'panel'))

def button_geometry(th):
    """-> (buttons, separators), the boxes draw_buttons and draw_separators paint, computed once per theme (memoized on
    it, after render() has rebound SCENE; the lane shifts and the restack never touch these boxes, except buttons.case's
    'bottom' shift, which moves the bottom row's y with its top). buttons.gap, buttons.sep_gap, buttons.group_space and
    buttons.case all null = the scene's own lists, its measured x. Any one set re-packs both rows as the app walks them (paint_handler.cpp: the icon row's left groups walk right from the left
    pad, its view group and all of the bottom row sit flush at the right margin), each button moved in x only (y, w, h
    and every other field as measured):
      GROUPS: a row's buttons in x order, a separator of the row lying between two of them closing a group.
      CHAINS: a separator whose two measured gaps (last button's right edge -> separator x, separator x + w -> next
        button x) are equal -- the standard gap, separator, gap -- joins its groups into one chain; unequal gaps (the
        icon row's view separator: 432 device px from Load in Place, 8 from Source+Warp on 1002) break the chain there,
        and that separator moves with the chain on its nearer side, keeping that gap.
      ANCHOR: a chain whose last button's right edge is the row's right margin (C.W - the icon row's first button x,
        the left pad mirrored: 2288 on 1002) is RIGHT-ANCHORED and packs leftward from that edge; any other chain is
        LEFT-ANCHORED and packs rightward from its first button's measured x.
      PACK: within a group the buttons stand at a pitch of w + gap x S device px; between groups the measured gaps
        are kept, each widened by sep_gap (gap, separator, gap) and the separator's x moves with the pack.
    gap 2 reproduces the scenes' measured pitch (64 + 4 = 68), so it re-derives every measured x.
    buttons.case {"h": H, "w": W, ...} (Windows 95's toolbar button, the case 23 wide x 22 tall, read at another size)
    makes every box W x S wide and H x S tall (y as measured, or as the 'bottom' shift moved it: the restack is
    shift_scene's) and every separator H x S - the measured button height taller (its overhang kept). It re-packs like
    gap: the groups, chains and anchors are found on the MEASURED boxes, the within-group step is gap x S or, gap null,
    each pair's measured gap, and the packed boxes take the case's width; the overlap refusal below catches a case
    wide enough to push the icon row's left chain into its view group.
    buttons.sep_gap null keeps the measured separator gaps (8 device px either side on the icon row, 10 on the bottom
    row on 1002). An integer is EXTRA logical px added to every gap the pack lays down beside a separator, both sides of
    every separator in both rows (sep_gap x S device px): an inner separator's two gaps, and a break separator's one
    kept gap (the view separator's 8 to Source+Warp: it moves left by the extra), so each row keeps its own base. It
    re-packs too: with buttons.gap null the within-group step is each pair's own measured gap (read off the scene before
    the pack, not assumed), so sep_gap 0 re-derives every measured x. Chains are still found on the MEASURED gaps.
    buttons.group_space (only with separators "none", never with sep_gap) is ABSOLUTE: an integer is the whole empty
    space between two adjacent groups of one chain in logical px, the same on both rows (group_space x S device px):
    an inner separator's span (gap, separator, gap) becomes exactly that, the separator given w 0 and parked at the
    span's middle (nothing paints it). A break separator keeps its measured kept gap (the view group stays flush at the
    margin). It re-packs like sep_gap (gap null = each pair's measured within-group step).
    Two packed boxes of one row overlapping (a large gap pushing a left chain into a right one) is refused."""
    if th.button_geometry is not None: return th.button_geometry
    gap = th.opt['buttons']['gap']; sep = th.opt['buttons']['sep_gap']; gsp = th.opt['buttons']['group_space']
    cs = th.opt['buttons']['case']
    if gap is None and sep is None and gsp is None and cs is None:
        th.button_geometry = (SCENE['buttons'], SCENE['separators']); return th.button_geometry
    extra = (sep or 0) * S
    what = ', '.join(f'buttons.{k} {v}' for k, v in (('gap', gap), ('sep_gap', sep), ('group_space', gsp), ('case', cs))
                     if v is not None)
    btns = [dict(b) for b in SCENE['buttons']]; seps = [dict(s) for s in SCENE['separators']]
    margin = C.W - min(b['x'] for b in btns if b['row'] == 'icon')
    for row in sorted({b['row'] for b in btns}):
        bs = sorted((b for b in btns if b['row'] == row), key=lambda b: b['x'])
        ss = [s for s in seps if s['row'] == row]
        groups, between = [[bs[0]]], []         # between[i]: the separator closing groups[i]
        for a, b in zip(bs, bs[1:]):
            mid = [s for s in ss if a['x'] + a['w'] <= s['x'] and s['x'] + s['w'] <= b['x']]
            if len(mid) > 1: raise SystemExit(f'scene {SCENE_TAG}: {len(mid)} separators between {row} buttons {a["button"]} and {b["button"]}')
            if mid: groups.append([b]); between.append(mid[0])
            else: groups[-1].append(b)
        if len(between) != len(ss):
            raise SystemExit(f'scene {SCENE_TAG}: a {row}-row separator stands outside its buttons; {what} cannot re-pack it')
        # chains: [groups, inner separators with their (left, right) gaps, a leading / trailing break separator]
        chains = [{'groups': [groups[0]], 'inner': [], 'lead': None, 'trail': None}]
        for i, s in enumerate(between):
            gl = s['x'] - (groups[i][-1]['x'] + groups[i][-1]['w']); gr = groups[i + 1][0]['x'] - (s['x'] + s['w'])
            if gl == gr:            # the measured gaps decide the chains; the pack lays them down widened by sep_gap,
                if gsp is None:     # or as group_space's whole span with the separator w 0 at its middle
                    chains[-1]['inner'].append((s, gl + extra, gr + extra))
                else:
                    span = gsp * S; s['w'] = 0
                    chains[-1]['inner'].append((s, span // 2, span - span // 2))
                chains[-1]['groups'].append(groups[i + 1])
            else:
                nxt = {'groups': [groups[i + 1]], 'inner': [], 'lead': None, 'trail': None}
                if gl < gr: chains[-1]['trail'] = (s, gl + extra)
                else: nxt['lead'] = (s, gr + extra)
                chains.append(nxt)
        # the within-group step before each button (bi >= 1): gap x S, or with gap null its own measured gap to the
        # button before it, read here before the pack moves anything
        steps = {id(g[bi]): gap * S if gap is not None else g[bi]['x'] - (g[bi - 1]['x'] + g[bi - 1]['w'])
                 for g in groups for bi in range(1, len(g))}
        for ch in chains:                       # the anchor, on the measured boxes
            ch['right'] = ch['groups'][-1][-1]['x'] + ch['groups'][-1][-1]['w'] == margin
        if cs is not None:                      # the case's boxes, the separators' overhang kept
            hs = {b['h'] for b in bs}
            if len(hs) != 1: raise SystemExit(f'scene {SCENE_TAG}: the {row} row\'s buttons have heights {sorted(hs)}; {what} needs one')
            dh = cs['h'] * S - hs.pop()
            for b in bs: b['w'], b['h'] = cs['w'] * S, cs['h'] * S
            for s in ss: s['h'] += dh
        for ch in chains:
            G, inner = ch['groups'], ch['inner']
            if ch['right']:                         # right-anchored: walk left from the margin
                x = margin
                for gi in range(len(G) - 1, -1, -1):
                    for bi in range(len(G[gi]) - 1, -1, -1):
                        b = G[gi][bi]; x -= b['w']; b['x'] = x
                        if bi: x -= steps[id(b)]
                    if gi:
                        s, gl, gr = inner[gi - 1]; x -= gr + s['w']; s['x'] = x; x -= gl
                if ch['lead']: s, gr = ch['lead']; s['x'] = x - gr - s['w']     # (nothing follows the margin: no trail)
            else:                                   # left-anchored: walk right from the first button's measured x
                x = G[0][0]['x']
                for gi, grp in enumerate(G):
                    if gi:
                        s, gl, gr = inner[gi - 1]; x += gl; s['x'] = x; x += s['w'] + gr
                    for bi, b in enumerate(grp):
                        if bi: x += steps[id(b)]
                        b['x'] = x; x += b['w']
                if ch['trail']: s, gl = ch['trail']; s['x'] = x + gl
                if ch['lead']: s, gr = ch['lead']; s['x'] = G[0][0]['x'] - gr - s['w']
        boxes = sorted([(b['x'], b['x'] + b['w'], b['button']) for b in bs] + [(s['x'], s['x'] + s['w'], 'separator') for s in ss])
        for (a0, a1, an), (b0, b1, bn) in zip(boxes, boxes[1:]):
            if b0 < a1: raise SystemExit(f'{what}: the {row} row\'s {an} ({a0}..{a1}) and {bn} ({b0}..{b1}) would overlap')
    th.button_geometry = (btns, seps); return th.button_geometry

def draw_separators(cr, th):
    # line: the app's 2-px separator; etched = Shadow then Hilight (the light side right), raised = Hilight then Shadow,
    # both one LW line either side of the separator's x (measured, or re-packed by buttons.gap / sep_gap: button_geometry);
    # none = nothing drawn, the gap alone separates
    style = th.opt['separators']
    if style == 'none': return
    for s in button_geometry(th)[1]:
        if style in ('etched', 'raised'):
            a, b = ('bevel_shadow', 'bevel_hilight') if style == 'etched' else ('bevel_hilight', 'bevel_shadow')
            fill(cr, s['x'] - LW, s['y'], s['x'], s['y'] + s['h'], th.get(a))
            fill(cr, s['x'], s['y'], s['x'] + LW, s['y'] + s['h'], th.get(b))
        else:
            fill(cr, s['x'], s['y'], s['x'] + s['w'], s['y'] + s['h'], th.get('separator'))

def a8_surface(m):
    h, w = m.shape; ms = cairo.ImageSurface(cairo.FORMAT_A8, w, h); st = ms.get_stride()
    buf = np.frombuffer(ms.get_data(), np.uint8).reshape(h, st); buf[:, :w] = m; ms.mark_dirty(); return ms

_PGM = {}
def pgm(fn):
    if fn not in _PGM:
        d = open(os.path.join(GLYPH_DIR, fn), 'rb').read(); parts = d.split(b'\n', 3)
        w, h = map(int, parts[1].split()); _PGM[fn] = np.frombuffer(parts[3], np.uint8).reshape(h, w)
    return _PGM[fn]

def ink_colour(th, ink):
    return th.get('icon_label') if ink == 'text' else th.get('icon_' + ink)

def paint_mask(cr, m, x, y, px):
    """One coverage mask m (the capture's glyph_px square) through the current source with its top-left at (x, y),
    px device px square: at its own size cairo.mask_surface as measured; at another size a cairo.SurfacePattern over
    the A8 surface, scaled by its matrix (pattern = (user - origin) x glyph_px / px), FILTER_BILINEAR, EXTEND_NONE."""
    ms = a8_surface(m)
    if px == m.shape[0] == m.shape[1]: cr.mask_surface(ms, x, y); return
    kx, ky = m.shape[1] / px, m.shape[0] / px
    p = cairo.SurfacePattern(ms); p.set_filter(cairo.FILTER_BILINEAR)
    p.set_matrix(cairo.Matrix(kx, 0, 0, ky, -x * kx, -y * ky)); cr.mask(p)

def draw_app_glyph(cr, th, g, x, y, under, enabled, px):
    """The app's glyph at (x, y), px device px square (paint_mask). Enabled, or disabled with buttons.disabled "mix":
    each ink in order in mix(ink, under, keep), keep = 1 / disabled_mix. Disabled with "engraved" (Windows' DrawState
    DSS_DISABLED): the union of the ink masks (the per-pixel max of the per-ink coverages), painted twice -- first in
    bevel_hilight one logical px right and down (beneath), then in bevel_shadow at (x, y)."""
    if not enabled and th.opt['buttons']['disabled'] == 'engraved':
        m = np.maximum.reduce([pgm(g['files'][ink]) for ink in g['inks']])
        C.src(cr, th.get('bevel_hilight')); paint_mask(cr, m, x + LW, y + LW, px)
        C.src(cr, th.get('bevel_shadow')); paint_mask(cr, m, x, y, px)
        return
    keep = 1.0 if enabled else th.num['disabled_mix']
    for ink in g['inks']:
        C.src(cr, C.mix(ink_colour(th, ink), under, keep)); paint_mask(cr, pgm(g['files'][ink]), x, y, px)

def draw_buttons(cr, th, rows):
    """Faces and the app's glyphs through cairo, each box at button_geometry's x (measured, or re-packed by buttons.gap /
    sep_gap / group_space / case). The glyph: centred in the box at the capture's glyph_px, or with buttons.case
    glyph x S device px square at the box's top-left + (pad_x, pad_y) x S (Windows' offset; the right and bottom air
    is what the case leaves), the capture's mask scaled to it (paint_mask)."""
    bo = th.opt['buttons']; raised = bo['raised']; cs = bo['case']
    for b in button_geometry(th)[0]:
        if b['row'] not in rows: continue
        key = f"{b['row']}_{b['button']}"; g = GIDX[key]; enabled = g['enabled']
        x, y, w, h = b['x'], b['y'], b['w'], b['h']
        row_ground = th.get('icon_row_ground') if b['row'] == 'icon' else th.get('bottom_row_ground')
        face = th.get('icon_face') if b['row'] == 'icon' else th.get('button_face')
        down = b['button'] in bo['down']
        under = row_ground; shift = 0
        styled = raised and b['row'] in bo['rows']
        if down and bo['toggled'] == 'app':
            # redesign_face_box: one rounded path (radius 10, half-stroke inset), selected fill + kRedesignLine ring
            keep = 1.0 if enabled else th.num['disabled_mix']
            fc = C.mix(th.get('down_face'), row_ground, keep); lc = C.mix(th.get('line'), row_ground, keep)
            half = LW / 2.0; r = round(5 * S) - half
            xx, yy, ww, hh = x + half, y + half, w - LW, h - LW
            cr.new_sub_path(); cr.arc(xx + ww - r, yy + r, r, -math.pi / 2, 0); cr.arc(xx + ww - r, yy + hh - r, r, 0, math.pi / 2)
            cr.arc(xx + r, yy + hh - r, r, math.pi / 2, math.pi); cr.arc(xx + r, yy + r, r, math.pi, 1.5 * math.pi); cr.close_path()
            C.src(cr, fc); cr.fill_preserve(); C.src(cr, lc); cr.set_line_width(LW); cr.stroke()
            under = fc
        elif down and bo['toggled'] in ('sunken', 'flat_fill'):
            fc = th.get('down_face'); fill(cr, x, y, x + w, y + h, fc)
            if bo['down_dither']:      # the Windows checked-button face: a 1-logical-px checkerboard of hilight over the face
                hl = th.get('bevel_hilight')
                for yy in range(y, y + h, LW):
                    for xx in range(x + (((yy - y) // LW) % 2) * LW, x + w, 2 * LW): fill(cr, xx, yy, xx + LW, yy + LW, hl)
            if bo['toggled'] == 'sunken': edge(cr, x, y, x + w, y + h, relief_lines(th, 'pressed'))
            under = fc; shift = LW if (bo['down_shift'] and bo['toggled'] == 'sunken') else 0
        elif styled:
            fill(cr, x, y, x + w, y + h, face); edge(cr, x, y, x + w, y + h, relief_lines(th, 'button')); under = face
        # the glyph: the app's own, recovered per ink from the capture (a disabled glyph mixed toward what is under it)
        if cs is None: gx, gy, gp = x + (w - SCENE['glyph_px']) // 2, y + (h - SCENE['glyph_px']) // 2, SCENE['glyph_px']
        else: gx, gy, gp = x + cs['pad_x'] * S, y + cs['pad_y'] * S, cs['glyph'] * S
        draw_app_glyph(cr, th, g, gx + shift, gy + shift, under, enabled, gp)

def draw_trim(cr, th):
    T = SCENE['trim']; y0, y1 = T['y0'], T['y1']; o = th.opt['trim']
    if o['style'] == 'acid': return draw_trim_acid(cr, th)
    if o['style'] == 'scrollbar': return draw_trim_scrollbar(cr, th)
    bb0 = T['bottom_border_rows'][0]
    # the lane ground
    if o['ground'] == 'app':
        fill(cr, 0, T['ground_bevel_lo_rows'][0], C.W, T['ground_bevel_lo_rows'][-1] + 1, th.get('trim_ground_bevel_lo'))
        fill(cr, 0, T['ground_bevel_hi_rows'][0], C.W, T['ground_bevel_hi_rows'][-1] + 1, th.get('trim_ground_bevel_hi'))
        fill(cr, 0, bb0, C.W, y1, th.get('trim_bottom_border'))
    elif o['ground'] == 'sunken':
        edge(cr, -LW * 4, y0, C.W + LW * 4, y1, relief_lines(th, 'sunken'))
    else:
        fill(cr, 0, bb0, C.W, y1, th.get('trim_bottom_border'))
    by0 = y0; by1 = bb0 if o['ground'] != 'sunken' else y1 - (2 if th.opt['relief'] == 'thick' else 1) * LW
    if o['ground'] == 'sunken': by0 = y0 + (2 if th.opt['relief'] == 'thick' else 1) * LW
    # the bar (absent when no part of the window is on screen)
    if T['bar'] is not None:
        bx0, bx1 = T['bar']
        if o['bar'] == 'app':
            # render.cpp's order: the face, the light top, the light left, the dark bottom, the dark right (the dark pair
            # last, owning the top-right and bottom-left corners). A scene with `bar_edge` records the bar's painted
            # extent (one column past a screen edge where its bound is off screen); scene_s4.json predates it and records
            # the visible stretch between the handles, whose side edges the handles cover.
            e = T.get('bar_edge')
            fill(cr, bx0, by0, bx1, by1, th.get('trim_bar'))
            fill(cr, bx0, T['bar_bevel_hi_rows'][0], bx1, T['bar_bevel_hi_rows'][-1] + 1, th.get('trim_bar_bevel_hi'))
            if e: fill(cr, bx0, by0, bx0 + e, by1, th.get('trim_bar_bevel_hi'))
            fill(cr, bx0, T['bar_bevel_lo_rows'][0], bx1, T['bar_bevel_lo_rows'][-1] + 1, th.get('trim_bar_bevel_lo'))
            if e: fill(cr, bx1 - e, by0, bx1, by1, th.get('trim_bar_bevel_lo'))
        else:
            fill(cr, bx0, by0, bx1, by1, th.get('trim_bar'))
            if o['bar'] == 'raised': edge(cr, bx0, by0, bx1, by1, relief_lines(th, 'panel'))

    # handles and the centre grip
    for kind, rects in cap_rects(th):
        for hx0, hx1 in rects:
            if o[kind] == 'app':
                fill(cr, hx0, y0, hx1, bb0, th.get('trim_cap'))
                fill(cr, hx0, T['cap_bevel_lo_rows'][0], hx1, T['cap_bevel_lo_rows'][-1] + 1, th.get('trim_cap_bevel_lo'))
                fill(cr, hx0, T['cap_bevel_hi_rows'][0], hx1, T['cap_bevel_hi_rows'][-1] + 1, th.get('trim_cap_bevel_hi'))
                if kind == 'grip':   # the hollow keeps its measured wall thickness either side, so it stays centred
                    gh = T['grip_hollow']; gx0, gx1 = T['grip']
                    fill(cr, hx0 + (gh['x0'] - gx0), gh['y0'], hx1 - (gx1 - gh['x1']), gh['y1'], th.get('trim_bar'))
            else:
                fill(cr, hx0, by0, hx1, by1, th.get('trim_cap'))
                if o[kind] == 'raised': edge(cr, hx0, by0, hx1, by1, relief_lines(th, 'button'))

def draw_trim_acid(cr, th):
    """trim.style "acid" (Sonic Foundry ACID 3's loop bar): the lane's ground is draw_grounds' flat trim_ground (no frame,
    no bevels, no bottom border); the bar a flat trim_bar rect over its painted columns, rows y0 + acid_inset .. y1 -
    acid_inset (logical rows); at each end whose handle is on screen a trim_cap triangle the bar's full height h,
    pointing inward: its vertical edge on the handle's outer column (the trimmed point), its apex h / 2 columns inward
    at the bar's middle row, filled aliased (ANTIALIAS_NONE: a pixel stair). The path runs half a row past the bar's
    top and bottom (vertical edge ty - 0.5 .. by + 0.5, apex (h + 1) / 2 in at ty + h / 2): ANTIALIAS_NONE samples pixel
    centres, so bar row i (0-based) takes min(i + 1, h - i) columns -- 1, 2, .. h / 2, h / 2, .. 2, 1 for an even h, a
    symmetric slope-1 stair inside the bar's rows (the plain triangle (ty, by, apex at h / 2) samples to 0, 1, .. h / 2,
    .. 1, its top row empty). No grip, no relief; bar / ground / handles / grip / cap_w are not read."""
    T = SCENE['trim']; ins = th.opt['trim']['acid_inset'] * LW
    if T['bar'] is None: return
    ty, by = T['y0'] + ins, T['y1'] - ins; h = by - ty
    bx0, bx1 = T['bar']; fill(cr, bx0, ty, bx1, by, th.get('trim_bar'))
    mid = (bx0 + bx1) / 2
    cr.save(); cr.set_antialias(cairo.ANTIALIAS_NONE); C.src(cr, th.get('trim_cap'))
    for hx0, hx1 in T['handles']:
        ex, apex = (hx0, hx0 + (h + 1) / 2) if (hx0 + hx1) / 2 < mid else (hx1, hx1 - (h + 1) / 2)
        cr.move_to(ex, ty - 0.5); cr.line_to(ex, by + 0.5); cr.line_to(apex, ty + h / 2); cr.close_path(); cr.fill()
    cr.restore()

def draw_trim_scrollbar(cr, th):
    """trim.style "scrollbar": the lane as Windows 95's scroll bar, miniaturized. The whole lane is draw_grounds'
    trim_ground (Windows' scroll bar face is the button face; trim_ground defaults to @ground). THE TRACK: a
    checkerboard of bevel_hilight over that ground in 1-logical-px cells (LW x LW device px), its phase anchored at the
    lane's top-left (x 0, the lane's y0), the cell (i, j) = ((x // LW), (y - y0) // LW) lit when i + j is even, so
    every lane height dithers identically; flush, no frame. THE THUMB, painted over the track: the kept region, from
    the left cap's outer edge to the right cap's outer edge -- the union of the scene's painted bar extent and its
    handles (660..1937 on 1002; on 1002a, both bounds off screen and no handles, the bar's -1..2305, so the thumb runs
    past both edges and only one column of its outer relief line shows at each) -- the lane's full height, filled with
    trim_ground and edged relief_lines 'panel' (EDGE_RAISED: thick = 3DLight / DkShadow outer, Hilight / Shadow inner;
    thin = Hilight / Shadow). No grip, no cap squares, no lane bevels, no bottom border: bar / ground / handles / grip
    are not read, and cap_w is not read either (the caps are the thumb's ends; their hit band is the app's business).
    A scene with no bar on screen is all track."""
    T = SCENE['trim']; y0, y1 = T['y0'], T['y1']; g = th.get('trim_ground')
    C.src(cr, th.get('bevel_hilight'))
    for j, yy in enumerate(range(y0, y1, LW)):
        for xx in range((j % 2) * LW, C.W, 2 * LW): cr.rectangle(xx, yy, LW, min(LW, y1 - yy))
    cr.fill()
    if T['bar'] is None: return
    xs = [T['bar']] + [tuple(h) for h in T['handles']]
    x0, x1 = min(a for a, _ in xs), max(b for _, b in xs)
    fill(cr, x0, y0, x1, y1, g); edge(cr, x0, y0, x1, y1, relief_lines(th, 'panel'))

def cap_rects(th):
    """[('handles', [(x0, x1), ...]), ('grip', [(x0, x1)] or [])] at trim.cap_w (None = the measured widths). A handle
    grows INWARD, its outer edge (the trimmed point) fixed: the begin handle (left of the bar's middle) keeps x0, the
    end handle keeps x1. The grip grows symmetrically about its centre (an odd surplus puts the extra column right)."""
    T = SCENE['trim']; cw = th.opt['trim']['cap_w']
    handles = [tuple(h) for h in T['handles']]; grip = [tuple(T['grip'])] if T['grip'] else []
    if cw is not None:
        w = cw * S; mid = (T['bar'][0] + T['bar'][1]) / 2 if T['bar'] is not None else C.W / 2
        handles = [(x0, x0 + w) if (x0 + x1) / 2 < mid else (x1 - w, x1) for x0, x1 in handles]
        grip = [(x0 - (w - (x1 - x0)) // 2, x0 - (w - (x1 - x0)) // 2 + w) for x0, x1 in grip]
    return (('handles', handles), ('grip', grip))

def ruler_seat_rows(px):
    """ruler_label_baseline_px (paint_handler.cpp) at a label face of px device px: the derived pad max(0,
    kRulerLabelCapTopPx x S - (ceil(ascent) - nearbyint(cap))), then ceil(ascent) -- rows under the lane's top."""
    asc = math.ceil(C.font_extents(C.SANS, px)[0]); cap = int(np.rint(C.cap_height(C.SANS, px)))
    return max(0, 4 * S - (asc - cap)) + asc

def ruler_label_seat(th):
    """-> (label px, baseline row) at ruler_label_pt: px = pt x 96 / 72 x S (12 pt = 32 px, the app's). With
    ruler_layout the baseline is the lane's top (after every shift and restack) + above + cap. Otherwise it is the
    scene's (lane top + ruler_pad_top + the seat at 32 px, after every shift and restack) re-seated by the app's
    rule at the new size: the cap top stays kRulerLabelCapTopPx rows under the lane's top, ruler_shortfall's rows
    (opened or closed at the lane's top) taken back off. Without fonts.small_px the lane keeps its height."""
    px = ruler_label_px(th); b = SCENE['ruler']['baseline']
    rows = ruler_layout_rows(th)
    if rows is not None: return px, SCENE['lanes']['ruler'][0] + rows[0] + rows[1]
    if px == C.SANS_PX and th.opt['fonts']['small_px'] is None: return px, b
    return px, b - ruler_seat_rows(C.SANS_PX) + ruler_seat_rows(px) - ruler_shortfall(th)

def draw_ruler(cr, th):
    R = SCENE['ruler']; P = SCENE['playhead']
    light = th.opt['ruler_tick_relief'] == 'light_right'
    # a tick's parts: one rect (major_top or minor_top .. tick_bottom) while the ruler sits directly above the marker
    # lane; with a lane between them (order_scene's R['rise']) a major's rise in the ruler lane's bottom rows, then
    # every tick's marker-lane part -- each part with its own light line
    rise = R.get('rise')
    for t in R['ticks']:
        if rise is None: parts = [(R['major_top'] if t['major'] else R['minor_top'], R['tick_bottom'])]
        else: parts = ([tuple(rise)] if t['major'] else []) + [(R['minor_top'], R['tick_bottom'])]
        for top, bot in parts:
            fill(cr, t['x'], top, t['x'] + R['tick_w'], bot, th.get('ruler_tick'))
            if light:   # light_right: one LW line right of the tick over its own rows, before the labels, head, flags, stems
                fill(cr, t['x'] + R['tick_w'], top, t['x'] + R['tick_w'] + LW, bot, th.get('ruler_tick_light'))
    px, base = ruler_label_seat(th)
    for lb in R['labels']:
        show(cr, C.SANS, px, lb['text'], lb['x'], base, th.get('ruler_label'))
    # the head: aliased rows at kPlayheadHeadAlpha, then the stem down the marker lane (flags cover it);
    # playhead_head_rows draws only the head's last rows (head_rows_drawn), its bottom row where it was
    h0, hn = head_rows_drawn(th)
    cr.save(); cr.rectangle(0, P['head_top'] + h0, C.W, hn); cr.clip()
    C.src(cr, th.get('playhead_head'), th.num['playhead_head_alpha'])
    for r in range(h0, P['head_rows']):
        hw = P['head_half'][r]; cr.rectangle(P['col'] - hw, P['head_top'] + r, 2 * hw + P['w'], 1)
    cr.fill(); cr.restore()
    if not P.get('stem_suppressed'):   # playhead_stem_suppressed: a coincident marker's stem wins the whole column
        fill(cr, P['col'], SCENE['lanes']['marker'][0], P['col'] + P['w'], SCENE['lanes']['marker'][1], th.get('playhead_stem'))

def draw_flags(cr, th):
    """Each flag: border, fill, the dark top band (or flags.relief), border, label. fonts.ui_px: the label at
    ui_font_px; the box's rows are seat_flags' (the measured rows while the text fits them), and its width is the
    measured one while the text fits it with the measured side pads (w = pad_l + nearbyint(shaped width) + pad_r, the
    pads kMarkerFlagPadLeftPx / RightPx x S = 4 + 4, pad_r read off the scene as w - pad_l - nearbyint(width at 32 px)),
    else the text + those pads."""
    F = SCENE['flags']; px = ui_font_px(th); grow = th.opt['fonts']['ui_px'] is not None
    for f in F['flags']:
        bx, bw = f['x'], f['w']; sel = '_sel' if f.get('selected') else ''
        if grow:
            pr = bw - F['pad_l'] - int(np.rint(C.shape(C.SANS, C.SANS_PX, f['text'])[1]))
            bw = max(bw, F['pad_l'] + int(np.rint(C.shape(C.SANS, px, f['text'])[1])) + pr)
        fill(cr, bx - F['border_w'], F['y0'], bx, F['y1'], th.get('flag_border'))
        fill(cr, bx, F['y0'], bx + bw, F['y1'], th.get('flag_fill' + sel))
        if th.flag_relief == 'raised':   # inside the border: one LW line of hilight top + left, edge bottom + right (last)
            edge(cr, bx, F['y0'], bx + bw, F['y1'], [(th.get('flag_hilight' + sel), th.get('flag_edge' + sel))])
        else:                            # the app's dark top band
            fill(cr, bx, F['y0'], bx + bw, F['y0'] + F['edge_h'], th.get('flag_edge' + sel))
        fill(cr, bx + bw, F['y0'], bx + bw + F['border_w'], F['y1'], th.get('flag_border'))
        show(cr, C.SANS, px, f['text'], bx + F['pad_l'], F['baseline'], th.get('flag_label'))

def well_geometry(th):
    """-> (well top, well bottom, canvas top, canvas bottom), device rows, end-exclusive. The well's top is the lane
    stack's bottom (moved only by buttons.case, trim.lane_h, ruler_pad_top / ruler_layout and the fonts lanes, both ways, shift_scene; lane_order never moves it); its bottom is the scene's well bottom moved by canvas_delta logical rows, the bottom row's
    border-top fixed (a positive delta needs a ground strip under the well: the scenes have none, gap 2 = 0). The
    list-form well's canvas is what its lines leave; "bordered" / "sunken" keep their measured seam rows."""
    L = SCENE['lanes']; wt, wb = L['well']; cv = SCENE['canvas']; d = th.num['canvas_delta'] * LW
    gap = SCENE['bottom_border']['y'] - wb
    if d > gap:
        raise SystemExit(f'canvas_delta {th.num["canvas_delta"]}: the well bottom would move {d} device rows down, but scene '
                         f'{SCENE_TAG} has {gap} rows of ground between the well (bottom {wb}) and the bottom row\'s border '
                         f'(row {SCENE["bottom_border"]["y"]}); only zero or a negative delta is possible here')
    wb += d
    if th.well_lines is not None:
        c0, c1 = wt + len(th.well_lines['top']) * LW, wb - len(th.well_lines['bottom']) * LW
    else:
        c0, c1 = cv[0], wb - (L['well'][1] - cv[1])
    if c1 - c0 < 1: raise SystemExit(f'the well\'s seams leave no canvas (rows {c0}..{c1})')
    return wt, wb, c0, c1

def draw_well(cr, th):
    wt, wb, c0, c1 = well_geometry(th); cv = (c0, c1)
    fill(cr, 0, cv[0], C.W, cv[1], th.get('canvas'))
    if th.well_lines is not None:   # the list form: each line LW rows, both lists in screen order (top to bottom)
        for i, c in enumerate(th.well_lines['top']): fill(cr, 0, wt + i * LW, C.W, wt + (i + 1) * LW, c)
        for i, c in enumerate(th.well_lines['bottom']): fill(cr, 0, cv[1] + i * LW, C.W, cv[1] + (i + 1) * LW, c)
    elif th.opt['well'] == 'sunken':
        if th.opt['relief'] == 'thick':
            fill(cr, 0, wt, C.W, wt + LW, th.get('bevel_shadow')); fill(cr, 0, wt + LW, C.W, cv[0], th.get('bevel_dkshadow'))
            fill(cr, 0, cv[1], C.W, wb - LW, th.get('bevel_light')); fill(cr, 0, wb - LW, C.W, wb, th.get('bevel_hilight'))
        else:   # one line a side outside, the black border kept as the inner line
            fill(cr, 0, wt, C.W, wt + LW, th.get('bevel_shadow')); fill(cr, 0, wt + LW, C.W, cv[0], th.get('well_border'))
            fill(cr, 0, cv[1], C.W, wb - LW, th.get('well_border')); fill(cr, 0, wb - LW, C.W, wb, th.get('bevel_hilight'))
    else:
        fill(cr, 0, wt, C.W, cv[0], th.get('well_border')); fill(cr, 0, cv[1], C.W, wb, th.get('well_border'))

def rescale_runs(runs, old_h, new_h):
    """One column's runs [y, len, ...] replayed at new_h rows through the nearest-neighbour vertical map: expand to a
    boolean column of old_h rows, sample dst row r at src row floor(r * old_h / new_h), re-run. No blending."""
    if new_h == old_h: return runs
    col = np.zeros(old_h, bool)
    for i in range(0, len(runs), 2): col[runs[i]:runs[i] + runs[i + 1]] = True
    m = col[(np.arange(new_h) * old_h) // new_h]
    e = np.flatnonzero(np.diff(np.concatenate(([0], m.astype(np.int8), [0]))))
    out = []
    for a, b in zip(e[0::2], e[1::2]): out += [int(a), int(b - a)]
    return out

def waveform_runs(th):
    """-> (canvas top, {'ink': cols, 'outline': cols}, channel split row): the capture's runs, rescaled when the derived
    canvas height differs from the measured one. The split row is the first destination row whose source row is at or
    below the measured split: canvas top + ceil(split_rel * new_h / old_h)."""
    _, _, c0, c1 = well_geometry(th); old_h, new_h = WAVE['h'], c1 - c0
    cols = {cls: [rescale_runs(r, old_h, new_h) for r in WAVE[cls]] for cls in ('ink', 'outline')}
    rel = WAVE['channel_split'] - WAVE['y0']
    return c0, cols, c0 + -(-rel * new_h // old_h)

def draw_waveform(arr, th):
    y0, cols, _ = waveform_runs(th); ink = np.array(th.get('ink'), np.uint8); out = np.array(th.get('outline'), np.uint8)
    for cls, c in (('ink', ink), ('outline', out)):
        for x, runs in enumerate(cols[cls]):
            for i in range(0, len(runs), 2): arr[y0 + runs[i]:y0 + runs[i] + runs[i + 1], x] = c

def draw_stems(cr, th):
    # the app runs every stem through the well's black border rows; a sunken well's bevels are a frame, so there
    # the stems stop at the canvas. Z-order (paint_marker_stems, then paint_playheads): the playhead's stem over
    # every marker stem, except where a marker stands on its column -- there the app suppresses the playhead's
    # whole stem (playhead_stem_suppressed) and the marker's stem shows.
    F = SCENE['flags']; P = SCENE['playhead']
    _, wb, c0, c1 = well_geometry(th)
    # through the border to the well's bottom ("bordered"), else the derived canvas ("sunken", the list form)
    y0, y1 = (F['stem_y0'], F['stem_y1'] + wb - SCENE['lanes']['well'][1]) if th.opt['well'] == 'bordered' else (c0, c1)
    for f in F['flags']:
        fill(cr, f['x'], y0, f['x'] + F['stem_w'], y1, th.get('flag_stem_sel' if f.get('selected') else 'flag_stem'))
    if not P.get('stem_suppressed'): fill(cr, P['col'], y0, P['col'] + P['w'], y1, th.get('playhead_stem'))

def clock_panel_rect(th):
    """-> (x0, y0, x1, y1) of the painted clock panel (clock_panel "sunken" / "status"), or None ("flat"): the clock
    cell (prefix + the widest-digit DD:DD.DDD specimen + the dirty mark, at the clock's seated size) with 4 logical px
    either side, over the bottom buttons' rows (buttons.case's height). draw_bottom paints it; stamp clears it."""
    if th.opt['clock_panel'] not in ('sunken', 'status'): return None
    ck = SCENE['clock']
    dw = max(C.shape(C.MONO, ck['size_px'], d)[1] for d in '0123456789')
    cell = C.shape(C.MONO, ck['size_px'], 'B | ')[1] + 9 * dw + C.shape(C.MONO, ck['size_px'], '*')[1]
    bb0 = [b for b in button_geometry(th)[0] if b['row'] == 'bottom'][0]
    return ck['x'] - 4 * S, bb0['y'], ck['x'] + int(math.ceil(cell)) + 4 * S, bb0['y'] + bb0['h']

def draw_bottom(cr, th):
    bb = SCENE['bottom_border']
    # the bottom row's border-top: "app" takes the `separators` style, whatever it is (line, etched, raised, none);
    # etched / raised are two LW lines from the border's top row, the second on the bottom row's first content rows;
    # "none" draws nothing, the well's own bottom seam separating the two
    style = th.opt['bottom_border']
    if style == 'app': style = th.opt['separators']
    if style in ('etched', 'raised'):
        a, b = ('bevel_shadow', 'bevel_hilight') if style == 'etched' else ('bevel_hilight', 'bevel_shadow')
        fill(cr, bb['x0'], bb['y'], bb['x1'], bb['y'] + LW, th.get(a))
        fill(cr, bb['x0'], bb['y'] + LW, bb['x1'], bb['y'] + 2 * LW, th.get(b))
    elif style == 'line':
        fill(cr, bb['x0'], bb['y'], bb['x1'], bb['y'] + bb['h'], th.get('bottom_border'))
    ck = SCENE['clock']; pr = clock_panel_rect(th)
    if pr is not None:
        x0, y0, x1, y1 = pr
        # "status": Windows' status-bar panel, ONE LW line whatever `relief` is (Shadow top and left, Hilight bottom
        # and right, the BR pair last); "sunken": relief_lines 'sunken' at the theme's relief
        lines = [(th.get('bevel_shadow'), th.get('bevel_hilight'))] if th.opt['clock_panel'] == 'status' else relief_lines(th, 'sunken')
        edge(cr, x0, y0, x1, y1, lines)
    show(cr, C.MONO, ck['size_px'], ck['text'], ck['x'], ck['baseline'], th.get('clock'))

def stamp(cr, th, text):
    # the file-name stamp: Roboto 20 px, `stamp` (140,140,140), its box's top-left at the scene's label_box (x 300,
    # y 1382 on every scene; buttons.case's 'bottom' shift moves it with the bottom row's content top); x clears a
    # painted clock panel: max(label_box x0, the panel's right edge + 16 device px)
    asc = C.font_extents(C.SANS, 20)[0]; lb = SCENE['label_box']; pr = clock_panel_rect(th)
    x = lb['x0'] if pr is None else max(lb['x0'], pr[2] + 16)
    show(cr, C.SANS, 20, text, x, lb['y0'] + math.ceil(asc), th.get('stamp'))

def render(theme_path, out_path, label=False):
    C.verify_fonts()
    th = Theme(theme_path)
    di, db = case_delta(th)
    global SCENE; SCENE = shift_scene(shift_scene(shift_scene(BASE_SCENE, di, 'icon'), lane_shift(th), 'trim'), pad_shift(th), 'ruler')
    SCENE = order_scene(seat_flags(shift_scene(SCENE, marker_shift(th), 'marker'), th), th.opt['lane_order'])
    SCENE = seat_clock(shift_scene(SCENE, db, 'bottom'), th)
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, C.W, C.H); cr = cairo.Context(surf)
    cr.set_antialias(cairo.ANTIALIAS_DEFAULT)
    draw_grounds(cr, th)
    draw_bars(cr, th)
    draw_menu(cr, th)
    draw_separators(cr, th)
    draw_buttons(cr, th, ('icon',))
    draw_trim(cr, th)
    draw_ruler(cr, th)
    draw_flags(cr, th)
    draw_well(cr, th)
    surf.flush()
    # the waveform's runs go straight into the surface's bytes (RGB24 = BGRx in memory)
    st = surf.get_stride(); mem = np.frombuffer(surf.get_data(), np.uint8).reshape(C.H, st)[:, :C.W * 4].reshape(C.H, C.W, 4)
    rgb = C.surface_to_rgb(surf); draw_waveform(rgb, th)
    mem[..., 0] = rgb[..., 2]; mem[..., 1] = rgb[..., 1]; mem[..., 2] = rgb[..., 0]; surf.mark_dirty()
    draw_stems(cr, th)
    draw_bottom(cr, th)
    draw_buttons(cr, th, ('bottom',))
    if label: stamp(cr, th, os.path.splitext(os.path.basename(out_path))[0])
    C.save_png(out_path, C.surface_to_rgb(surf))
    return th

if __name__ == '__main__':
    argv = sys.argv[1:]; args = []
    for i, a in enumerate(argv):
        if a.startswith('--') or (i and argv[i - 1] == '--scene'): continue
        args.append(a)
    if len(args) != 2: raise SystemExit('usage: render.py <theme.json> <out.png> [--scene <tag>] [--label]')
    render(args[0], args[1], '--label' in sys.argv)
    print('wrote', args[1], f'(scene {SCENE_TAG})')
