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
import colour as CL           # the chrome rule and cairo's antialiasing blend (the export)
import cairo
import numpy as np
# the toolkits' own rules (tools/theme_catalog/toolkit_rules.py), read by flags.style "bevelled" (flag_bevel)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'theme_catalog'))
import toolkit_rules

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
    'label': TWO_PASS('kRedesignLabel'), 'light_text': '@label', 'legend': '@label', 'menu_disabled': 'auto', 'clock': '@label',
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
    # the scroll arrow's glyph (draw_trim_arrow_button): "auto" the luminance rule's ink over the button's face (the
    # app until 2026-10-03), a colour by hand otherwise -- the theme catalog's crops state '@label', the app's rule since
    # that day (render.cpp paint_trim_arrow_button: a chrome glyph on a chrome face is the theme's label)
    'trim_arrow': 'auto',
    # the well and the waveform
    'well_border': TWO_PASS('kWaveformBorder'), 'canvas': TWO_PASS('kWaveformCanvas'), 'ink': TWO_PASS('kWaveformInk'), 'outline': 'auto',
    # playhead and flags
    'playhead_head': TWO_PASS('kPlayheadHead'), 'playhead_stem': TWO_PASS('kPlayheadStem'),
    # read only by playhead_head_outline "outline": the head's one-LW outline, the theme's label (architect 2026-10-03,
    # late: the chrome's text colour, black on light chrome, white on dark)
    'playhead_head_border': '@label',
    'flag_fill': TWO_PASS('kMarkerFlagFill'), 'flag_edge': TWO_PASS('kMarkerFlagEdge'), 'flag_border': TWO_PASS('kMarkerFlagBorder'),
    'flag_label': TWO_PASS('kMarkerFlagLabel'), 'flag_stem': '@flag_fill',
    'flag_fill_sel': TWO_PASS('kMarkerFlagFillSel'), 'flag_edge_sel': TWO_PASS('kMarkerFlagEdgeSel'), 'flag_stem_sel': '@flag_fill_sel',
    'flag_hilight': 'auto', 'flag_hilight_sel': 'auto',     # read only by flags.relief "raised"
    # an invalid flag, read only by flags.style "bevelled": WINDOWS' ERROR-ICON PAIR (architect 2026-10-03, late: the
    # Stop icon's white X on VGA bright red), the face #FF0000 and its RECORDED label #FFFFFF (Windows recorded a text
    # colour beside every face; the luminance rule is WCAG's, not Windows')
    'flag_fill_red': '#FF0000', 'flag_label_red': '#FFFFFF',
    # read only by flags.style "flat" (draw_flags_flat) with flags.selection "face", the app's (architect 2026-10-03,
    # the colour loop: THE SELECTED MARKER IS A BRIGHTER FACE, nothing white): a SELECTED flag's face `flag_fill_sel`
    # (above, the app style's selected fill, its default that style's measured one; the device config's
    # flag_face_selected), an invalid one's `flag_fill_red_sel` (invalid_face_selected), under the ONE selected label
    # `flag_label_sel` (flag_label_selected), these two defaulting to the app's defaults; and the EDITING flag -- the in-place editor's box, the selected flag opened for edit --
    # its outline black (`flag_border_edit`, Windows' WindowFrame), its face the edited flag's selected face, its text
    # `flag_label_sel`, the selected substring's glyphs `selected_text` (HilightText) over `selected_fill` (Hilight;
    # flat_edit_colours, which falls back to Windows' #000080 when the theme does not state it)
    'flag_fill_red_sel': '#FF6666', 'flag_label_sel': '#000000',
    'flag_border_edit': '#000000', 'field_ground': '#FFFFFF', 'selected_text': '#FFFFFF',
    # THE FIELD'S TEXT (2026-10-03, step 11), read only by the optional surface `dialog` (draw_dialog): Windows'
    # WindowText. `field_ground` above is the dialog field's ground, and only that since 2026-10-03 (the editing flag
    # is the selected flag opened for edit, the app's). The `card` has no role of its own: it is the ground under the
    # label (architect 2026-10-03, step 15: the app's INFO FACE, render.h's palette block; the info pair retired)
    'field_text': '#000000',
    # icon inks (icons.cpp), two_pass of each
    'icon_label': '@label', **{f'icon_{k}': list(C.two_pass(v)) for k, v in INKC.items() if k != 'text'},
    # the Windows relief set (only read when something is raised or sunken): NO DEFAULT AND NO RULE (architect
    # 2026-10-03, "no derived, imported only") -- a theme that draws relief states its four recorded bytes
    # (tools/theme_catalog/); Theme.get refuses an unstated one where it is read, Theme refuses "auto" at load
    'bevel_hilight': None, 'bevel_light': None, 'bevel_shadow': None, 'bevel_dkshadow': None,
    # THE EMBOSS'S LIGHT COPY (architect 2026-10-03, late: Windows' normal DSS_DISABLED on every variation): the colour
    # every engraved path (the menu word, the glyph, a disabled flag's label) paints one logical px right and down
    # beneath the Shadow word -- the theme's own Hilight, or on a dark variation the base's RECORDED Hilight,
    # not darkened with the face (the Shadow word darkens like every other line)
    'emboss_hilight': '@bevel_hilight',
    # the accent (the architect, 2026-10-02: the waveform's ink is the accent); read only by menu.highlight "fill"
    'accent': '@ink',
}
NUM_DEFAULTS = {'disabled_mix': SCENE['disabled_mix'], 'playhead_head_alpha': 0.8, 'canvas_delta': 0, 'ruler_pad_top': 0,
                'ruler_label_pt': 12, 'playhead_head_rows': 12}
# row 8's state line (draw_bottom), the text a freshly loaded project shows in the scene's view (Target+Warp): the
# load's eager preview render stamps "Updating..." (GuiTargetRender::stamp_updating, src/gui/target_render.cpp) and it
# stands until the preview lands; at rest app.queue_progress_text is empty and the line shows nothing (a theme's "")
STATE_TEXT_DEFAULT = 'Updating...'
OPT_DEFAULTS = {'relief': 'flat', 'separators': 'line', 'ruler_tick_relief': 'none', 'well': 'bordered', 'bottom_border': 'app', 'clock_panel': 'flat',
                'icons': 'app',
                'bars': {'menu': 'flat', 'icon_row': 'flat', 'bottom_row': 'flat'},
                'buttons': {'raised': False, 'rows': ['icon', 'bottom'], 'down': ['ViewTW'], 'down_shift': True,
                            'toggled': None, 'down_dither': False, 'gap': None, 'sep_gap': None, 'group_space': None,
                            'case': None, 'disabled': 'mix'},
                'trim': {'bar': 'app', 'ground': 'app', 'handles': 'app', 'grip': 'app', 'cap_w': None, 'lane_h': None,
                         'style': 'app', 'acid_inset': 1, 'held': None},
                'lane_order': ['trim', 'ruler', 'marker'],
                'ruler_layout': None,
                'menu': {'highlight': None, 'disabled': 'colour'},
                'fonts': {'ui_px': None, 'small_px': None},
                'state_text': STATE_TEXT_DEFAULT,
                'playhead_lane_stem': 'stem', 'playhead_head_outline': 'none',
                'dialog': None, 'card': None,
                'elements': {'ink': True, 'outline': True, 'stems': True, 'flags': True, 'playhead': True,
                             'state_line': True},
                'picker': None}
LANES3 = ('trim', 'ruler', 'marker')     # the three lane blocks lane_order restacks (top to bottom)
TRIM_STYLES = ('app', 'acid', 'scrollbar')
MENU_HL_STYLES = ('fill', 'sunken', 'raised')
# menu.disabled: the disabled menu word -- "colour" in `menu_disabled`, "engraved" Windows' embossed DrawState form,
# its light copy `emboss_hilight` (architect 2026-10-03, late: every variation's disabled text; draw_menu), "shadowed"
# that emboss mirrored for a dark face: the word in `menu_disabled` over its echo in `bevel_shadow` (architect
# 2026-10-03, late; not used since set AP, kept for the record)
MENU_DISABLED_STYLES = ('colour', 'engraved', 'shadowed')
# the options whose values are checked here (separators, bottom_border, ruler_tick_relief, clock_panel; the older options are not)
OPT_VALUES = {'separators': ('line', 'etched', 'raised', 'none'), 'bottom_border': ('app', 'line', 'etched', 'raised', 'none'),
              'ruler_tick_relief': ('none', 'light_right'), 'clock_panel': ('flat', 'sunken', 'status'),
              'playhead_lane_stem': ('stem', 'head'), 'playhead_head_outline': ('none', 'outline')}
FLAG_RELIEF = ('none', 'raised')
FLAG_STYLES = ('app', 'bevelled', 'flat')
FLAG_STATES = ('selected', 'invalid', 'disabled', 'editing')
# flags.selection, read only by flags.style "flat": how a SELECTED flag shows -- "face" THE APP'S (architect 2026-10-03,
# the colour loop: the selected face `flag_fill_sel` / `flag_fill_red_sel` under `flag_label_sel`, its stem the
# selected face, the outline unchanged), "underline" the label underlined (architect 2026-10-03, late), "fill" the face
# in the selected pair (step 11, a mock option; draw_flags_flat). The white outline ("outline", "outline+fill" and
# their flags.outline_px) retired with the app's white ring, 2026-10-03.
FLAG_SELECTIONS = ('face', 'underline', 'fill')
# the flags section's keys beside its colour aliases; flags.editing_selected (read only by flags.style "flat" with an
# editing flag): how many of the editing flag's leading characters are selected, null for all (the editor as it opens)
FLAG_OPTIONS = ('relief', 'style', 'rule', 'states', 'selection', 'editing_selected')
# trim.held (trim.style "scrollbar" only): one arrow button HELD, as a single-bound drag holds it -- "flat" the app's
# face (render.cpp paint_trim_arrow_button: Windows' DFCS_PUSHED | DFCS_FLAT, one Shadow line round the face), "sunken"
# the push button's pressed face (the plain sunken edge); the glyph one line right and down in both (step 11). Its
# optional "ring" (step 12, a mock option, read only by the "flat" face): the role the one line round the flat face
# takes, `bevel_shadow` (the app's) when absent -- set AW's ways to bring the held cap's ring out at the dark level
TRIM_HELD_CAPS = ('begin', 'end')
TRIM_HELD_FACES = ('flat', 'sunken')
TRIM_HELD_RINGS = ('bevel_shadow', 'bevel_dkshadow', 'bevel_hilight', 'emboss_hilight', 'label')
CASE_KEYS = ('h', 'w', 'glyph', 'pad_x', 'pad_y')
# buttons.disabled: a disabled glyph -- "mix" the app's, "engraved" Windows' DrawState DSS_DISABLED (its light copy
# `emboss_hilight`), "shadowed" that emboss mirrored for a dark face, as menu.disabled "shadowed" mirrors the word
# (architect 2026-10-03, late; not used since set AP, the normal emboss ruled for every variation; draw_app_glyph)
DISABLED_STYLES = ('mix', 'engraved', 'shadowed')
COLOUR_SECTIONS = (('waveform', {'ink': 'ink', 'canvas': 'canvas', 'outline': 'outline'}),
                   ('flags', {'fill': 'flag_fill', 'edge': 'flag_edge', 'border': 'flag_border', 'label': 'flag_label', 'stem': 'flag_stem',
                              'fill_sel': 'flag_fill_sel', 'edge_sel': 'flag_edge_sel', 'stem_sel': 'flag_stem_sel',
                              'hilight': 'flag_hilight', 'hilight_sel': 'flag_hilight_sel', 'fill_red': 'flag_fill_red',
                              'label_red': 'flag_label_red', 'fill_red_sel': 'flag_fill_red_sel', 'label_sel': 'flag_label_sel',
                              'border_edit': 'flag_border_edit'}))

BEVELS = ('bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow')     # Windows' COLOR_3D* order

# ------------------------------------------------------------------ THE TABLET GEOMETRY (step 13, architect 2026-10-03)
# A theme's `geometry`: "scene" (the default: the measured scene at its 200 % geometry, shifted and re-packed by the
# options above -- every earlier mock set) or "tablet" (tools/palette/tablet.py: the app at gui_scale 275, every length
# derived from the app's own constants, in scene 1002's state). The tablet geometry PAINTS THE APP'S DESIGN: the options
# in TABLET_FIXED stand at the app's values (a theme may state one only as the app has it), every other option is
# refused (the app owns every length), and a theme varies only what TABLET_FREE names: its colours and the states a mock
# shows. A relief line, a dither cell and an emboss offset are then relief_line_px (3 device px), a stem and a tick
# waveform_line_px (3), the faces 35.75 / 33 / 27.5 px (README.md, the tablet geometry).
GEOMETRIES = ('scene', 'tablet')
TABLET = False                  # set by use_tablet (Theme), with SCENE / BASE_SCENE / LW / FLAG_STEM_W rebound
APP_WELL = {'top': ['@bevel_shadow', '@bevel_dkshadow'], 'bottom': ['@bevel_light', '@bevel_hilight']}   # render_canvas
TABLET_FIXED = {
    'relief': 'thick', 'separators': 'none', 'well': APP_WELL, 'clock_panel': 'status', 'bottom_border': 'none',
    'ruler_tick_relief': 'light_right', 'icons': 'app', 'playhead_head_outline': 'outline', 'playhead_lane_stem': 'stem',
    'lane_order': ['trim', 'ruler', 'marker'], 'bars': {'menu': 'flat', 'icon_row': 'flat', 'bottom_row': 'flat'},
    'ruler_layout': None, 'fonts': {'ui_px': None, 'small_px': None},
    'playhead_head_alpha': 1.0, 'canvas_delta': 0, 'ruler_pad_top': 0, 'ruler_label_pt': 12,
    'buttons': {'raised': True, 'rows': ['icon', 'bottom'], 'down_shift': True, 'toggled': 'sunken', 'down_dither': True,
                'disabled': 'engraved', 'gap': None, 'sep_gap': None, 'group_space': None, 'case': None},
    'trim': {'style': 'scrollbar', 'bar': 'app', 'ground': 'app', 'handles': 'app', 'grip': 'app', 'cap_w': None,
             'lane_h': None, 'acid_inset': 1},
    'menu': {'highlight': None, 'disabled': 'engraved'},
}
# what a tablet theme states freely: its colours (the chrome rule's ground among them), the scene's states (the toggled buttons, the held trim cap, the flags'
# states), the optional surfaces (the state line, the dialog, the card), the element switches and the picker's export
TABLET_FREE = ('name', 'description', 'geometry', 'colours', 'chrome', 'waveform', 'flags', 'state_text', 'dialog',
               'card', 'elements', 'picker')
TABLET_FREE_SUB = {'buttons': ('down',), 'trim': ('held',)}

def use_tablet(path, t):
    """A theme stating "geometry": "tablet" -> the theme with the app's fixed options in place, after refusing any
    option the tablet geometry owns; rebinds the module's scene (tablet.SCENE), its line width (LW, relief_line_px) and
    the stem's width (FLAG_STEM_W, waveform_line_px). Scene 1002 only: the geometry is drawn in its state."""
    global TABLET, SCENE, BASE_SCENE, LW, FLAG_STEM_W
    import tablet as TB
    if SCENE_TAG != TB.SCENE_TAG:
        raise SystemExit(f'theme {path}: the tablet geometry is drawn in scene {TB.SCENE_TAG}\'s state, not --scene {SCENE_TAG}')
    t = json.loads(json.dumps(t))
    for k, v in list(t.items()):
        if k in TABLET_FREE: continue
        if k not in TABLET_FIXED:
            raise SystemExit(f'theme {path}: {k!r} is not read by the tablet geometry (the app owns every length; '
                             f'README.md, the tablet geometry)')
        fx = TABLET_FIXED[k]
        if isinstance(fx, dict) and k != 'well':
            if not isinstance(v, dict): raise SystemExit(f'theme {path}: {k} is an object, not {v!r}')
            for sk, sv in v.items():
                if sk in TABLET_FREE_SUB.get(k, ()): continue
                if sk not in fx or fx[sk] != sv:
                    raise SystemExit(f'theme {path}: the tablet geometry paints the app\'s {k}.{sk} '
                                     f'({fx.get(sk, "not an option")!r}), not {sv!r}')
        elif v != fx:
            raise SystemExit(f'theme {path}: the tablet geometry paints the app\'s {k} ({fx!r}), not {v!r}')
    fl = t.setdefault('flags', {})
    for k in ('relief', 'rule'):
        if k in fl: raise SystemExit(f'theme {path}: flags.{k} is not read by the tablet geometry (the app\'s flat flag)')
    if fl.get('style', 'flat') != 'flat' or fl.get('selection', 'face') != 'face':
        raise SystemExit(f'theme {path}: the tablet geometry paints the app\'s flag (flags.style "flat", flags.selection '
                         f'"face": the brighter selected face), not {fl.get("style")!r} / {fl.get("selection")!r}')
    fl['style'] = 'flat'
    for k, fx in TABLET_FIXED.items():
        if isinstance(fx, dict) and k != 'well': t.setdefault(k, {}).update(fx)
        else: t[k] = json.loads(json.dumps(fx))
    TABLET = True; BASE_SCENE = SCENE = TB.SCENE
    LW = TB.SCENE['flags']['border_w']; FLAG_STEM_W = TB.SCENE['flags']['stem_w']
    return t

class RoleColour(tuple):
    """A resolved role's bytes as Theme.get returns them, tagged with the role the painter asked for (`asked`) and the
    ROOT of its alias chain (`root`: the role whose own value is a colour, a rule or the chrome rule's line), so every
    paint names the role that owns its colour (PaintRecord, the export). Otherwise a plain (r, g, b) tuple."""
    def __new__(cls, c, root, asked):
        o = tuple.__new__(cls, c); o.root = root; o.asked = asked; return o

# THE CHROME RULE (architect 2026-10-04: the chrome is ONE KNOB, the ground): a theme's top-level `chrome` key,
# {"ground": colour, "rule": "windows95"}, states the ground and fills every other chrome role from it by Windows 95's
# proportions (colour.py windows95_chrome: Hilight, 3DLight and Shadow the ground x 255 / 223 / 128 over 192 per
# channel, half to even, capped; DkShadow #000000; the field ground and the emboss's light copy the Hilight). The roles
# it fills (colour.CHROME_ROLES) are then not stated in `colours`; the label stays the theme's own (fixed). The picker
# picks the ground and repaints every line from it by the same arithmetic (picker/src/colour.h scale_byte).
CHROME_RULES = ('windows95',)

class Theme:
    def __init__(self, path, data=None):
        """path: the theme file (its name the default name and every message's subject); data: the theme's object
        itself when the caller built it (export_scene's variants), path then naming the file it came from."""
        t = data if data is not None else json.load(open(path)) if path else {}
        self.geometry = t.get('geometry', 'scene')
        if self.geometry not in GEOMETRIES:
            raise SystemExit(f'theme {path}: geometry must be one of {GEOMETRIES}, not {self.geometry!r}')
        if self.geometry == 'tablet': t = use_tablet(path, t)
        self.name = t.get('name', os.path.splitext(os.path.basename(path))[0] if path else 'default')
        raw = dict(DEFAULTS); raw.update(t.get('colours', {}))
        self.chrome = t.get('chrome')
        if self.chrome is not None:
            ch = self.chrome
            if (not isinstance(ch, dict) or set(ch) != {'ground', 'rule'} or ch['rule'] not in CHROME_RULES
                    or not isinstance(ch['ground'], str)):
                raise SystemExit(f'theme {path}: chrome is null or {{"ground": "#rrggbb", "rule": one of {CHROME_RULES}}}, not {ch!r}')
            both = sorted(set(t.get('colours', {})) & set(CL.CHROME_ROLES))
            if both: raise SystemExit(f'theme {path}: the chrome rule fills {both}; a theme with `chrome` does not state them')
            for r, c in CL.windows95_chrome(C.parse_colour(ch['ground'])).items(): raw[r] = C.hexs(c)
        for sec, keys in COLOUR_SECTIONS:
            bad = set(t.get(sec, {})) - set(keys) - (set(FLAG_OPTIONS) if sec == 'flags' else set())
            if bad: raise SystemExit(f'theme {path}: unknown {sec} keys {sorted(bad)}')
            for k, role in keys.items():
                if k in t.get(sec, {}): raw[role] = t[sec][k]
        fl = t.get('flags', {})
        self.flag_relief = fl.get('relief', 'none')
        if self.flag_relief not in FLAG_RELIEF: raise SystemExit(f'theme {path}: flags.relief must be one of {FLAG_RELIEF}')
        self.flag_style = fl.get('style', 'app')
        if self.flag_style not in FLAG_STYLES: raise SystemExit(f'theme {path}: flags.style must be one of {FLAG_STYLES}')
        self.flag_rule = fl.get('rule'); self.flag_states = flag_states(path, fl)
        self.flag_selection = fl.get('selection', 'face')
        if 'selection' in fl and self.flag_style != 'flat':
            raise SystemExit(f'theme {path}: flags.selection is read only by flags.style "flat"')
        if self.flag_selection not in FLAG_SELECTIONS:
            raise SystemExit(f'theme {path}: flags.selection must be one of {FLAG_SELECTIONS}, not {self.flag_selection!r} '
                             f'(the white outline retired 2026-10-03)')
        self.flag_editing_selected = fl.get('editing_selected')
        if 'editing_selected' in fl:
            es = fl['editing_selected']
            if self.flag_style != 'flat' or not any(st[3] for st in self.flag_states):
                raise SystemExit(f'theme {path}: flags.editing_selected is read only by flags.style "flat" with an editing flag')
            n_ed = min(len(f['text']) for f, st in zip(BASE_SCENE['flags']['flags'], self.flag_states) if st[3])
            if es is not None and (not isinstance(es, int) or isinstance(es, bool) or not 0 <= es <= n_ed):
                raise SystemExit(f'theme {path}: flags.editing_selected is null (the whole text) or a whole number of the '
                                 f'editing flag\'s leading characters 0..{n_ed}, not {es!r}')
        if self.flag_style == 'bevelled':
            if 'relief' in fl: raise SystemExit(f'theme {path}: flags.relief is the "app" style\'s; "bevelled" draws its own bevel')
            toolkit_rules.flag_bevel(self.flag_rule, (0, 0, 0))    # a missing or malformed flags.rule fails here, in one line
            if fl.get('states', {}).get('editing'):
                raise SystemExit(f'theme {path}: flags.states "editing" is read only by flags.style "flat"')
        elif self.flag_style == 'flat':
            if 'relief' in fl or 'rule' in fl:
                raise SystemExit(f'theme {path}: flags.relief and flags.rule are not read by flags.style "flat" (no bevel)')
        elif 'rule' in fl or 'states' in fl:
            raise SystemExit(f'theme {path}: flags.rule and flags.states are read only by flags.style "bevelled" / "flat"')
        unknown = set(raw) - set(DEFAULTS)
        if unknown: raise SystemExit(f'theme {path}: unknown colour roles {sorted(unknown)}')
        for role in BEVELS:
            if raw[role] == 'auto': raise SystemExit(f'theme {path}: {role} "auto" is retired; state the relief byte (architect 2026-10-03)')
        self.raw = raw; self.c = {}
        # the colour roles the theme file itself states (its colours and the colour sections' aliases): flat_edit_colours
        # reads `selected_fill` only when stated, its default being the app's constant, not Windows' Hilight
        self.stated = set(t.get('colours', {})) | {role for sec, keys in COLOUR_SECTIONS for k, role in keys.items() if k in t.get(sec, {})}
        self.num = dict(NUM_DEFAULTS); self.num.update({k: t[k] for k in NUM_DEFAULTS if k in t})
        self.opt = json.loads(json.dumps(OPT_DEFAULTS))
        for k, v in t.items():
            if k in ('colours', 'chrome', 'waveform', 'flags', 'name', 'description', 'geometry') or k in NUM_DEFAULTS: continue
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
        for r in DEFAULTS:     # every role resolved at load, an unstated relief byte only where a painter reads it
            if self.raw[r] is not None: self.get(r)
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
        hd = self.opt['trim']['held']
        if hd is not None:
            if (not isinstance(hd, dict) or not {'cap', 'face'} <= set(hd) <= {'cap', 'face', 'ring'}
                    or hd['cap'] not in TRIM_HELD_CAPS or hd['face'] not in TRIM_HELD_FACES
                    or hd.get('ring', 'bevel_shadow') not in TRIM_HELD_RINGS):
                raise SystemExit(f'theme {path}: trim.held is null or {{"cap": one of {TRIM_HELD_CAPS}, "face": one of '
                                 f'{TRIM_HELD_FACES}, optionally "ring": one of {TRIM_HELD_RINGS}}}, not {hd!r}')
            if 'ring' in hd and hd['face'] != 'flat':
                raise SystemExit(f'theme {path}: trim.held.ring is read only by the "flat" face')
            if ts != 'scrollbar': raise SystemExit(f'theme {path}: trim.held is read only by trim.style "scrollbar"')
        dg = self.opt['dialog']
        if dg is not None and (not isinstance(dg, dict) or set(dg) != {'label', 'text'}
                               or not all(isinstance(v, str) and v for v in dg.values())):
            raise SystemExit(f'theme {path}: dialog is null or {{"label": "...", "text": "..."}} (the editor dialog\'s '
                             f'label and its field\'s text, both non-empty), not {dg!r}')
        cd = self.opt['card']
        if cd is not None and (not isinstance(cd, dict) or set(cd) != {'text'} or not isinstance(cd['text'], str) or not cd['text']):
            raise SystemExit(f'theme {path}: card is null or {{"text": "..."}} (one notification card\'s sentence), not {cd!r}')
        d = self.num['canvas_delta']
        if not isinstance(d, int) or isinstance(d, bool): raise SystemExit(f'theme {path}: canvas_delta is a whole number of logical rows, not {d!r}')
        lp = self.num['ruler_label_pt']
        if not isinstance(lp, (int, float)) or isinstance(lp, bool) or not lp > 0:
            raise SystemExit(f'theme {path}: ruler_label_pt is a point size > 0 (the timestamps\' face, 12 = the app\'s), not {lp!r}')
        hr = self.num['playhead_head_rows']; hmax = BASE_SCENE['playhead']['head_rows'] // S
        if not TABLET and (not isinstance(hr, int) or isinstance(hr, bool) or not 1 <= hr <= hmax):
            raise SystemExit(f'theme {path}: playhead_head_rows is a whole number of logical rows 1..{hmax} (the head kept on the '
                             f'lane\'s bottom, its widest top rows dropped; {hmax} = the app\'s), not {hr!r}')
        if self.opt['menu']['disabled'] not in MENU_DISABLED_STYLES:
            raise SystemExit(f'theme {path}: menu.disabled must be one of {MENU_DISABLED_STYLES}, not {self.opt["menu"]["disabled"]!r}')
        hl = self.opt['menu']['highlight']
        if hl is not None:
            items = [it['text'] for it in BASE_SCENE['menu']['items']]
            if not isinstance(hl, dict) or set(hl) - {'item', 'style', 'label'} or 'item' not in hl:
                raise SystemExit(f'theme {path}: menu.highlight is null or {{"item": ..., "style": ..., "label": colour}}, not {hl!r}')
            if hl['item'] not in items: raise SystemExit(f'theme {path}: menu.highlight.item must be one of {items}, not {hl["item"]!r}')
            hl.setdefault('style', 'fill')
            if hl['style'] not in MENU_HL_STYLES:
                raise SystemExit(f'theme {path}: menu.highlight.style must be one of {MENU_HL_STYLES}, not {hl["style"]!r}')
            # the label over the "fill" face: by default Theme.highlight_ink's luminance rule over the accent (the
            # app's rule until 2026-10-03, kept here as a legacy fallback) unless the theme sets it by hand
            self.menu_hl_label = (self.colour(hl['label']) if 'label' in hl
                                  else self.highlight_ink(self.get('accent')))
        el = self.opt['elements']
        if not all(isinstance(v, bool) for v in el.values()):
            raise SystemExit(f'theme {path}: elements maps each of {sorted(OPT_DEFAULTS["elements"])} to true or false, not {el!r}')
        pk = self.opt['picker']
        if pk is not None: picker_key(path, self, pk)
        st = self.opt['state_text']
        if st is not None and not isinstance(st, str):
            raise SystemExit(f'theme {path}: state_text is a string (row 8\'s state line) or null (no line), not {st!r}')
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
        """-> the role's bytes as a RoleColour (its alias chain's root named on it)."""
        if role in self.c: return self.c[role]
        if role in _stack: raise SystemExit(f'colour reference loop at {role}')
        v = self.raw[role]
        if v is None: raise SystemExit(f'theme {self.name}: {role} is not stated (a theme that draws relief states its four relief bytes)')
        root = role
        if isinstance(v, str) and v.startswith('@'): c = self.get(v[1:], _stack + (role,)); root = c.root
        elif v == 'auto': c = self.auto(role, _stack + (role,))
        else: c = C.parse_colour(v)
        self.c[role] = RoleColour(tuple(c), root, role); return self.c[role]

    def auto(self, role, st):
        if role == 'menu_disabled': return C.mix(self.get('label', st), self.get('menu_ground', st), self.num['disabled_mix'])
        if role == 'outline': return C.outline_of(self.get('ink', st), self.get('canvas', st))
        if role in ('flag_hilight', 'flag_hilight_sel'):     # a lighter flag colour: 35 % toward white in linear light
            return tuple(C.lin_mix(self.get(role.replace('hilight', 'fill'), st), (255, 255, 255), 0.35))
        if role == 'ruler_tick_light':                       # a lighter tick colour, the same rule
            return tuple(C.lin_mix(self.get('ruler_tick', st), (255, 255, 255), 0.35))
        if role == 'trim_arrow': return self.highlight_ink(self.get('trim_ground', st))
        raise SystemExit(f'no auto rule for {role}')

    def highlight_ink(self, fill):
        """A FALLBACK chrome text/glyph ink over a fill, for a role the theme does not state by hand: the RETAINED
        LEGACY RULE, the app's own highlight_text_ink until 2026-10-03 -- black when the fill's luminance exceeds the
        threshold, else `light_text` -- the app's light ink was its label (the label white), so the role defaults to
        @label; a theme whose label is dark names its light ink there (win95_standard: Windows' COLOR_HIGHLIGHTTEXT
        white over COLOR_HIGHLIGHT, its label being COLOR_BTNTEXT black). The flags' black label is their own rule
        and never this. THE APP ITSELF NO LONGER DERIVES THIS WAY: since 2026-10-03 its text over a fill is the
        theme's own recorded pair (render.h); this renderer keeps the derivation as the fallback a theme's crop can
        still ask for, or that an old mock set already used."""
        return C.highlight_text_ink(fill, self.get('light_text'))

    def quartet_report(self):
        """One clause for the render's log line: the relief set as stated, or that the theme draws none."""
        if any(self.raw[r] is None for r in BEVELS): return 'no relief set stated'
        return 'relief ' + ' / '.join(C.hexs(self.get(r))[1:] for r in BEVELS)

# THE PICKER KEY (read only by --export; THE EXPORT below): {"active": key, "elements": [{"key", "name", "role",
# "scene"}, ...], "scenes": {name: overrides, ...}}. An ELEMENT is one colour the picker picks: `key` its word in
# picks.txt and state.json (lower case, [a-z][a-z0-9_]*), `name` its Title Case name in the chooser, `role` the colour
# role it sets -- one the theme states as a colour, or `ground` under the chrome rule (the chrome's one knob) -- and
# `scene` the scene it is picked over. A SCENE is this theme with its overrides merged in (picker_scene_theme: an
# object merges key by key, anything else replaces), one picture of the app: several elements may share one. A scene
# overrides the states a mock shows (elements, flags' states, buttons.down, trim.held, dialog, card, state_text), never
# a colour: the role table is one for every scene.
PICKER_KEY_RE = r'[a-z][a-z0-9_]*'
PICKER_SCENE_FIXED = ('name', 'description', 'geometry', 'colours', 'chrome', 'waveform', 'picker')

def picker_key(path, th, pk):
    """Theme's check of the `picker` key (the head above); a hard fail names what is wrong."""
    import re
    if not isinstance(pk, dict) or 'layers' in pk:
        raise SystemExit(f'theme {path}: picker is {{"active": key, "elements": [...], "scenes": {{...}}}} (the layers form '
                         f'retired 2026-10-04 with the multi-element picker), not {pk!r}')
    if set(pk) != {'active', 'elements', 'scenes'}:
        raise SystemExit(f'theme {path}: picker has exactly the keys active, elements and scenes, not {sorted(pk)}')
    sc = pk['scenes']
    if not isinstance(sc, dict) or not sc or not all(isinstance(k, str) and re.fullmatch(PICKER_KEY_RE, k) and isinstance(v, dict)
                                                    for k, v in sc.items()):
        raise SystemExit(f'theme {path}: picker.scenes maps scene names ([a-z][a-z0-9_]*) to override objects, not {sc!r}')
    for name, ov in sc.items():
        bad = sorted(set(ov) & set(PICKER_SCENE_FIXED))
        if bad: raise SystemExit(f'theme {path}: picker scene {name!r} overrides {bad}; a scene changes states, never a colour')
        fl = ov.get('flags', {})
        if not isinstance(fl, dict) or set(fl) - set(FLAG_OPTIONS):
            raise SystemExit(f'theme {path}: picker scene {name!r}: flags overrides only {FLAG_OPTIONS}, never a colour')
    els = pk['elements']
    if not isinstance(els, list) or not 1 <= len(els) <= 32:
        raise SystemExit(f'theme {path}: picker.elements is a list of 1..32 elements, not {els!r}')
    seen = {'key': set(), 'name': set(), 'role': set()}
    for e in els:
        if (not isinstance(e, dict) or set(e) != {'key', 'name', 'role', 'scene'} or not all(isinstance(v, str) and v for v in e.values())
                or not re.fullmatch(PICKER_KEY_RE, e['key'])):
            raise SystemExit(f'theme {path}: a picker element is {{"key": [a-z][a-z0-9_]*, "name": "Title Case", "role": role, '
                             f'"scene": name}}, not {e!r}')
        for k in seen:
            if e[k] in seen[k]: raise SystemExit(f'theme {path}: two picker elements share the {k} {e[k]!r}')
            seen[k].add(e[k])
        if e['scene'] not in sc: raise SystemExit(f'theme {path}: picker element {e["key"]!r}\'s scene {e["scene"]!r} is not in picker.scenes')
        r = e['role']
        if r not in DEFAULTS: raise SystemExit(f'theme {path}: picker element {e["key"]!r}\'s role {r!r} is not a colour role')
        if r == 'ground':
            if th.chrome is None:
                raise SystemExit(f'theme {path}: the element {e["key"]!r} picks the ground, which needs the chrome rule (the '
                                 f'`chrome` key): its lines follow it')
        elif r in CL.CHROME_ROLES and th.chrome is not None:
            raise SystemExit(f'theme {path}: picker element {e["key"]!r}\'s role {r!r} is a line of the chrome rule, not a colour')
        else:
            v = th.raw[r]
            if not isinstance(v, (str, list)) or (isinstance(v, str) and (v.startswith('@') or v == 'auto')):
                raise SystemExit(f'theme {path}: picker element {e["key"]!r}\'s role {r!r} is {v!r}; an element\'s role is stated '
                                 f'as a colour (an alias or a rule would follow another role)')
    if pk['active'] not in seen['key']:
        raise SystemExit(f'theme {path}: picker.active {pk["active"]!r} is not an element\'s key')

def flag_states(path, fl):
    """flags.states -> one (selected, invalid, disabled, editing) per scene flag, left to right: {"selected": [i, ...],
    "invalid": [...], "disabled": [...], "editing": [...]}, each a list of the scene's flag indices (0 = the leftmost);
    a flag the lists do not name is unselected, valid and enabled, a scene flag measured selected stays selected.
    Selected and invalid combine (a red sunken flag; flat: the bright selected red); disabled is never invalid (the
    app's ladder: disabled wins) and combines with selected for flags.style "flat" alone (the app's provisional
    selected-disabled arm, 2026-10-03; a bevelled disabled button is never pushed); editing combines with invalid alone
    (the in-place editor over an invalid marker takes its selected red; flags.style "flat" only). Read by flags.style
    "bevelled" and "flat" (draw_flags), so one mock shows every state."""
    n = len(BASE_SCENE['flags']['flags']); st = fl.get('states', {})
    if (not isinstance(st, dict) or set(st) - set(FLAG_STATES)
            or not all(isinstance(v, list) and len(set(v)) == len(v)
                       and all(isinstance(i, int) and not isinstance(i, bool) and 0 <= i < n for i in v) for v in st.values())):
        raise SystemExit(f'theme {path}: flags.states is {{"selected": [i, ...], "invalid": [...], "disabled": [...]}}, the scene\'s '
                         f'flag indices 0..{n - 1} left to right, each at most once per list, not {st!r}')
    dis = set(st.get('disabled', [])); ed = set(st.get('editing', []))
    if dis & set(st.get('invalid', [])):
        raise SystemExit(f'theme {path}: flags.states: a disabled flag is not invalid ({sorted(dis)}; disabled wins)')
    if dis & set(st.get('selected', [])) and fl.get('style') != 'flat':
        raise SystemExit(f'theme {path}: flags.states: a selected disabled flag is read only by flags.style "flat"')
    if ed & (set(st.get('selected', [])) | dis):
        raise SystemExit(f'theme {path}: flags.states: an editing flag is in no other list but "invalid" ({sorted(ed)})')
    if ed & set(st.get('invalid', [])) and fl.get('style') != 'flat':
        raise SystemExit(f'theme {path}: flags.states: an invalid editing flag is read only by flags.style "flat"')
    return [(i not in ed and (i in st.get('selected', []) or BASE_SCENE['flags']['flags'][i].get('selected', False)),
             i in st.get('invalid', []), i in dis, i in ed) for i in range(n)]

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
    if TABLET: return SCENE['ui_px']        # the tablet geometry: 13 Windows px x 2.75 = 35.75
    up = th.opt['fonts']['ui_px']
    return C.SANS_PX if up is None else float(up * S)

def flag_seat(th):
    """fonts.ui_px -> None (null: the scene's flags as measured, the app of the scenes' commit) or (box_h, lane_h,
    edge+ascent): the flag box's height, the marker lane's height and the label's baseline offset under the box's top,
    device rows. THE APP'S RULE (render.h's marker-lane block at kMarkerLaneAirPx, paint_handler.cpp's
    marker_lane_rows): box = edge_h + ceil(ascent) + ceil(descent) at the normal face, every term a whole device row
    and nothing rounded further; lane = the air + box, THE AIR ONE PX OF GROUND ABOVE THE BOX AND NONE BELOW IT, so the
    box's bottom row is the lane's last row and the flag stands on the well (seat_flags; its stem runs on through the
    well's top lines, draw_stems). The air is the scene's edge_h, the box's top band: the app sizes both as one
    Windows px (kMarkerLaneAirPx and kMarkerFlagEdgePx, scaled_px(1, 1)), here one logical px. The lane's difference
    from the measured one is opened (or closed, negative) at its bottom (shift_scene 'marker'): 228 + 2 + 43 = 273 on
    1002 at ui_px 17 (34 px: ascent 32, descent 9)."""
    if th.opt['fonts']['ui_px'] is None: return None
    F = BASE_SCENE['flags']; px = ui_font_px(th)
    asc, desc = C.font_extents(C.SANS, px)[:2]
    off = F['edge_h'] + math.ceil(asc); box = off + math.ceil(desc)
    return box, F['edge_h'] + box, off

def flags_on_well(th):
    """True when the flags stand on the well (flag_seat's rule, fonts.ui_px set): the stems then run from the well's
    top, through its top lines, as the app's waveform_stem_band does (draw_stems). The tablet geometry's flags always
    do (the app's marker lane, render.h)."""
    return TABLET or flag_seat(th) is not None

def marker_shift(th):
    """flag_seat -> d, the device rows the marker lane grows by (negative: shrinks by; 0 with fonts.ui_px null)."""
    fs = flag_seat(th)
    return 0 if fs is None else fs[1] - (BASE_SCENE['lanes']['marker'][1] - BASE_SCENE['lanes']['marker'][0])

def seat_flags(sc, th):
    """The scene with its flag boxes' rows and label baseline set by flag_seat (after the 'marker' shift, before
    order_scene restacks): the box's bottom the marker lane's last row, its top the air under the lane's top, the
    baseline edge_h + ceil(ascent) under the box's top. fonts.ui_px null returns the scene itself."""
    fs = flag_seat(th)
    if fs is None: return sc
    box, lane, off = fs
    sc = json.loads(json.dumps(sc)); F = sc['flags']; m0, m1 = sc['lanes']['marker']
    F['y1'] = m1; F['y0'] = m1 - box; F['baseline'] = F['y0'] + off
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
    if TABLET: return 0, BASE_SCENE['playhead']['head_rows']      # the app's whole head (playhead_head_h_px)
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

def edge(cr, x0, y0, x1, y1, lines, sides='ltrb', lw=None):
    """DrawEdge: lines = [(outer TL, outer BR), (inner TL, inner BR)], each lw device px (LW, the renderer's line, when
    None), TL first and BR last (BR owns the top-right and bottom-left corners), each pair inset one line."""
    if lw is None: lw = LW
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
    (redesign_baseline: the cap band centred; 41 at 32 px, the measured baseline). A disabled word is `menu_disabled`
    (menu.disabled "colour"), or with menu.disabled "engraved" Windows 95's greyed menu item, DrawState DSS_DISABLED
    as for the glyphs (draw_app_glyph): the word in `emboss_hilight` (default bevel_hilight) one logical px right and
    down, then in bevel_shadow at its place over it; or with menu.disabled "shadowed" that emboss mirrored for a dark face (architect 2026-10-03,
    late: the disabled word of the dark variations LIGHTER than the face): the echo in bevel_shadow one logical px
    right and down first, then the word in `menu_disabled` at its place over it. A disabled anchor under the "fill"
    highlight keeps the highlight's label, neither engraved nor shadowed."""
    m = SCENE['menu']; x = 0; px = ui_font_px(th); y0, y1 = SCENE['lanes']['menu']
    base = m['baseline'] if th.opt['fonts']['ui_px'] is None else C.redesign_baseline(C.SANS, px, y0, y1 - y0)
    hl = th.opt['menu']['highlight']
    for it in m['items']:
        w = C.shape(C.SANS, px, it['text'])[1]; bw = int(np.rint(w)) + 2 * m['pad']
        col = th.get('label') if it['enabled'] else th.get('menu_disabled')
        engrave = not it['enabled'] and th.opt['menu']['disabled'] == 'engraved'
        shadow = not it['enabled'] and th.opt['menu']['disabled'] == 'shadowed'
        if hl is not None and hl['item'] == it['text']:
            if hl['style'] == 'fill':
                fill(cr, x, y0, x + bw, y1, th.get('accent')); col = th.menu_hl_label; engrave = shadow = False
            else:
                s_, h_ = th.get('bevel_shadow'), th.get('bevel_hilight')
                edge(cr, x, y0, x + bw, y1, [(s_, h_)] if hl['style'] == 'sunken' else [(h_, s_)])
        if engrave:
            show(cr, C.SANS, px, it['text'], x + m['pad'] + LW, base + LW, th.get('emboss_hilight'))
            col = th.get('bevel_shadow')
        if shadow:
            show(cr, C.SANS, px, it['text'], x + m['pad'] + LW, base + LW, th.get('bevel_shadow'))
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
    `emboss_hilight` (default bevel_hilight) one logical px right and down (beneath), then in bevel_shadow at (x, y).
    Disabled with "shadowed" (architect 2026-10-03, late: the disabled icons of a dark variation, the word's inversion
    carried to the glyphs; not used since set AP, kept for the record): that emboss mirrored for a dark face, as
    menu.disabled "shadowed" draws the word -- the same union mask, first in bevel_shadow one logical px right and down
    (the echo), then in `menu_disabled` (the disabled word's colour, lighter than the face) at (x, y)."""
    mode = th.opt['buttons']['disabled']
    if not enabled and mode in ('engraved', 'shadowed'):
        m = np.maximum.reduce([pgm(g['files'][ink]) for ink in g['inks']])
        echo, top = (('emboss_hilight', 'bevel_shadow') if mode == 'engraved' else ('bevel_shadow', 'menu_disabled'))
        C.src(cr, th.get(echo)); paint_mask(cr, m, x + LW, y + LW, px)
        C.src(cr, th.get(top)); paint_mask(cr, m, x, y, px)
        return
    keep = 1.0 if enabled else th.num['disabled_mix']
    for ink in g['inks']:     # enabled: the ink itself (mix at keep 1 is the same bytes; the role stays named on it)
        c = ink_colour(th, ink) if enabled else C.mix(ink_colour(th, ink), under, keep)
        C.src(cr, c); paint_mask(cr, pgm(g['files'][ink]), x, y, px)

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
        if TABLET: gx, gy, gp = x + SCENE['glyph_off'], y + SCENE['glyph_off'], SCENE['glyph_px']   # the case's (3, 3)
        elif cs is None: gx, gy, gp = x + (w - SCENE['glyph_px']) // 2, y + (h - SCENE['glyph_px']) // 2, SCENE['glyph_px']
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

TRIM_ARROW_ROWS = (1, 3, 5, 7)     # render.h kTrimArrowGlyphRows: the scroll arrow's four columns from the tip

def draw_trim_arrow_button(cr, th, x, y0, w, h, points_left, held=None, ring='bevel_shadow'):
    """ONE ARROW BUTTON, render.cpp's paint_trim_arrow_button: trim_ground under relief_lines 'panel' (the plain raised
    edge), then Windows' scroll arrow as integer rectangles -- four columns 1, 3, 5 and 7 units tall from the tip, each
    centred on the glyph's middle row, a unit LW device px (one logical px: the app's scaled_px(1, 1), the unit the
    relief lines take here), the 4 x 7-unit glyph centred in the button with an odd difference floored toward the
    top-left -- its tip LEFT on the begin button and RIGHT on the end button, in `trim_arrow` (by default the luminance
    rule's ink over the button's face, Theme.highlight_ink, the app's rule until 2026-10-03; the catalog's crops state
    the theme's label, the app's rule since). HELD (trim.held's face, step 11): "flat" the app's pressed face, the
    ground under ONE line round it (Windows' DFCS_PUSHED | DFCS_FLAT) in `ring`, the role trim.held.ring names
    (`bevel_shadow`, the app's, when absent; step 12's mock option); "sunken" the push button's
    pressed face, the plain sunken edge (relief_lines 'sunken': Shadow / Hilight outer, DkShadow / 3DLight inner at
    relief "thick"); in both the glyph one LW right and down."""
    g = th.get('trim_ground'); fill(cr, x, y0, x + w, y0 + h, g)
    if held == 'flat': edge(cr, x, y0, x + w, y0 + h, [(th.get(ring), th.get(ring))])
    elif held == 'sunken': edge(cr, x, y0, x + w, y0 + h, relief_lines(th, 'sunken'))
    else: edge(cr, x, y0, x + w, y0 + h, relief_lines(th, 'panel'))
    push = LW if held else 0
    u = LW; n = len(TRIM_ARROW_ROWS); gw, gh = n * u, TRIM_ARROW_ROWS[-1] * u
    gx, gy = x + (w - gw) // 2 + push, y0 + (h - gh) // 2 + push; ink = th.get('trim_arrow')
    for i, rows in enumerate(TRIM_ARROW_ROWS):        # i = 0 is the tip
        slot = i if points_left else n - 1 - i; top = (TRIM_ARROW_ROWS[-1] - rows) // 2
        fill(cr, gx + slot * u, gy + top * u, gx + (slot + 1) * u, gy + (top + rows) * u, ink)

def draw_trim_scrollbar(cr, th):
    """trim.style "scrollbar": the lane as Windows 95's scroll bar, miniaturized, render.cpp's render_trim_flags back to
    front. The whole lane is draw_grounds' trim_ground (Windows' scroll bar face is the button face; trim_ground
    defaults to @ground). THE TRACK: a checkerboard of bevel_hilight over that ground in 1-logical-px cells (LW x LW
    device px), its phase anchored at the lane's top-left (x 0, the lane's y0), the cell (i, j) = ((x // LW),
    (y - y0) // LW) lit when i + j is even, so every lane height dithers identically; flush, no frame.
    THE THUMB is the kept region, the begin bound's column to the end bound's: a scene handle stands at each IN-VIEW
    bound (the begin's left edge on its column, the end's right edge one past its own; a bound with no handle is off
    screen, on its own side). THE ARROW BUTTONS (render.h kTrimArrowButtonPx, trim_endcap_rect): at each in-view bound a
    SQUARE button the lane's height on a side (16 x 16 Windows px on the 16-px lane), the begin's left edge on the
    begin column pointing left, the end's right edge on the end column pointing right (draw_trim_arrow_button); NARROW
    (both in view and the kept span under two buttons) the begin keeps its column and the end button stands edge to
    edge right of it. THE BODY, painted first, runs between the two buttons' inner edges (empty in the narrow case),
    trim_ground under relief_lines 'panel' (plain raised: thick = 3DLight / DkShadow outer, Hilight / Shadow inner;
    thin = Hilight / Shadow) the lane's full height, no grip; an OFF-SCREEN side runs past that window edge by its
    edge's whole thickness, so its side lines fall outside the surface (on 1002a, both bounds off screen, the body
    spans -run .. W + run and shows no side line). bar / ground / handles / grip / cap_w are not read. A scene with no
    bar on screen is all track."""
    T = SCENE['trim']; y0, y1 = T['y0'], T['y1']; g = th.get('trim_ground')
    C.src(cr, th.get('bevel_hilight'))
    for j, yy in enumerate(range(y0, y1, LW)):
        for xx in range((j % 2) * LW, C.W, 2 * LW): cr.rectangle(xx, yy, LW, min(LW, y1 - yy))
    cr.fill()
    if T['bar'] is None: return
    btn = y1 - y0; run = {'flat': 1, 'thin': 1, 'thick': 2}[th.opt['relief']] * LW
    mid = (T['bar'][0] + T['bar'][1]) / 2
    begin = [h[0] for h in T['handles'] if (h[0] + h[1]) / 2 < mid]       # the begin column
    end = [h[1] for h in T['handles'] if (h[0] + h[1]) / 2 >= mid]        # one past the end column
    narrow = bool(begin and end) and end[0] - begin[0] < 2 * btn
    lo = begin[0] + btn if begin else -run
    hi = end[0] - btn if end else C.W + run
    if hi > lo: fill(cr, lo, y0, hi, y1, g); edge(cr, lo, y0, hi, y1, relief_lines(th, 'panel'))
    hd = th.opt['trim']['held']
    face = lambda cap: hd['face'] if hd is not None and hd['cap'] == cap else None
    ring = hd.get('ring', 'bevel_shadow') if hd is not None else 'bevel_shadow'
    if begin: draw_trim_arrow_button(cr, th, begin[0], y0, btn, y1 - y0, True, face('begin'), ring)
    if end: draw_trim_arrow_button(cr, th, begin[0] + btn if narrow else end[0] - btn, y0, btn, y1 - y0, False, face('end'),
                                   ring)

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
    if TABLET: return SCENE['small_px'], SCENE['ruler']['baseline']    # 27.5 px, the app's seat (tablet.py)
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
    # playhead_head_rows draws only the head's last rows (head_rows_drawn), its bottom row where it was;
    # elements.playhead false: no head, no outline, no lane stem (and no stem in the well, draw_stems)
    if not th.opt['elements']['playhead']: return
    h0, hn = head_rows_drawn(th)
    cr.save(); cr.rectangle(0, P['head_top'] + h0, C.W, hn); cr.clip()
    C.src(cr, th.get('playhead_head'), th.num['playhead_head_alpha'])
    for r in range(h0, P['head_rows']):
        hw = P['head_half'][r]; cr.rectangle(P['col'] - hw, P['head_top'] + r, 2 * hw + P['w'], 1)
    cr.fill(); cr.restore()
    if th.opt['playhead_head_outline'] == 'outline': draw_head_outline(cr, th, h0)
    # playhead_lane_stem "head" (architect 2026-10-03, late, a mock option): this run over the chrome lanes takes the
    # head's colour, opaque; in the well (its lines and the canvas, draw_stems) the stem keeps playhead_stem
    if not P.get('stem_suppressed'):   # playhead_stem_suppressed: a coincident marker's stem wins the whole column
        fill(cr, P['col'], SCENE['lanes']['marker'][0], P['col'] + P['w'], SCENE['lanes']['marker'][1],
             th.get('playhead_head' if th.opt['playhead_lane_stem'] == 'head' else 'playhead_stem'))

def head_outline_runs(h0):
    """playhead_head_outline "outline": the drawn head's ONE-LW OUTLINE -> [(row, x0, x1)], device px end-exclusive:
    the head's own pixels (the drawn rows h0.. of head_half, each row col - hw .. col + hw + w) that have a pixel
    outside the head within LW of them straight up, down, left or right -- the inner boundary, so the head keeps its
    size and the outline takes its outermost LW (as the flat flag's outline takes the box's border columns). Taken in
    the four directions only, a one-step stair of the head's sides is a one-LW staircase, each step's corner
    touching the next diagonally (the Windows 95 arrow cursor's black edge); the top row and the tip's bottom row are
    outline across their width (nothing of the head above or below them; the stem starts below the head and is
    not part of it)."""
    P = SCENE['playhead']; rows = P['head_rows'] - h0
    x_lo = P['col'] - max(P['head_half']) - LW; wd = 2 * (max(P['head_half']) + LW) + P['w']
    m = np.zeros((rows + 2 * LW, wd + 2 * LW), bool)
    for r in range(h0, P['head_rows']):
        hw = P['head_half'][r]; a = P['col'] - hw - x_lo + LW
        m[r - h0 + LW, a:a + 2 * hw + P['w']] = True
    inner = m.copy()
    for k in range(1, LW + 1):
        inner[k:, :] &= m[:-k, :]; inner[:-k, :] &= m[k:, :]; inner[:, k:] &= m[:, :-k]; inner[:, :-k] &= m[:, k:]
    out = []
    for y, row in enumerate(m & ~inner):
        e = np.flatnonzero(np.diff(np.concatenate(([0], row.astype(np.int8), [0]))))
        out += [(P['head_top'] + h0 + y - LW, x_lo - LW + a, x_lo - LW + b) for a, b in zip(e[::2], e[1::2])]
    return out

def draw_head_outline(cr, th, h0):
    """playhead_head_outline "outline" (architect 2026-10-03, late): the head's one-LW outline (head_outline_runs) in
    `playhead_head_border` (the theme's `label`), opaque, over the head's fill (`playhead_head`, unchanged)."""
    for y, a, b in head_outline_runs(h0): fill(cr, a, y, b, y + 1, th.get('playhead_head_border'))

def draw_flags(cr, th):
    """Each flag: border, fill, the dark top band (or flags.relief), border, label. fonts.ui_px: the label at
    ui_font_px; the box's rows are seat_flags' (the box standing on the well, flag_seat), and its width is the
    measured one while the text fits it with the measured side pads (w = pad_l + nearbyint(shaped width) + pad_r, the
    pads kMarkerFlagPadLeftPx / RightPx x S = 4 + 4, pad_r read off the scene as w - pad_l - nearbyint(width at 32 px)),
    else the text + those pads."""
    if th.flag_style == 'bevelled': return draw_flags_bevelled(cr, th)
    if th.flag_style == 'flat': return draw_flags_flat(cr, th)
    F = SCENE['flags']; px = ui_font_px(th)
    for f in F['flags']:
        bx, bw = f['x'], flag_fill_w(th, f); sel = '_sel' if f.get('selected') else ''
        fill(cr, bx - F['border_w'], F['y0'], bx, F['y1'], th.get('flag_border'))
        fill(cr, bx, F['y0'], bx + bw, F['y1'], th.get('flag_fill' + sel))
        if th.flag_relief == 'raised':   # inside the border: one LW line of hilight top + left, edge bottom + right (last)
            edge(cr, bx, F['y0'], bx + bw, F['y1'], [(th.get('flag_hilight' + sel), th.get('flag_edge' + sel))])
        else:                            # the app's dark top band
            fill(cr, bx, F['y0'], bx + bw, F['y0'] + F['edge_h'], th.get('flag_edge' + sel))
        fill(cr, bx + bw, F['y0'], bx + bw + F['border_w'], F['y1'], th.get('flag_border'))
        show(cr, C.SANS, px, f['text'], bx + F['pad_l'], F['baseline'], th.get('flag_label'))

def flag_fill_w(th, f):
    """A flag's fill width between its two border columns, device px: the measured one, or with fonts.ui_px the text
    at the face + the measured side pads when it no longer fits (draw_flags' docstring)."""
    F = SCENE['flags']; bw = f['w']
    if th.opt['fonts']['ui_px'] is None: return bw
    pr = bw - F['pad_l'] - int(np.rint(C.shape(C.SANS, C.SANS_PX, f['text'])[1]))
    return max(bw, F['pad_l'] + int(np.rint(C.shape(C.SANS, ui_font_px(th), f['text'])[1])) + pr)

def flag_face(th, i):
    """flags.style "bevelled": scene flag i's face colour and state -> (face, selected, disabled): the flag's face
    (`flag_fill`), an invalid flag's Windows' error red (`flag_fill_red`), a disabled flag's the theme's `ground`.
    The stem takes the face (draw_stems)."""
    sel, red, dis, _ = th.flag_states[i]
    return th.get('ground' if dis else 'flag_fill_red' if red else 'flag_fill'), sel, dis

def flag_label_ink(th, i):
    """flags.style "bevelled": scene flag i's label ink, a RECORDED colour beside its face (architect 2026-10-03,
    late: Windows 95 recorded a text colour beside every face -- ButtonFace / ButtonText, Hilight / HilightText --
    and the luminance rule is WCAG 2.0's contrast math, not Windows', so no program element reads it): `flag_label`
    on a normal face (the app's black, kMarkerFlagLabel), `flag_label_red` on an invalid one (#FFFFFF, the Stop icon's
    white X). Selection keeps the face and so its label; a disabled flag's label is engraved (draw_flags_bevelled)."""
    return th.get('flag_label_red' if th.flag_states[i][1] else 'flag_label')

def flag_box(th, f):
    """flags.style "bevelled": a flag's box (x0, y0, x1, y1), device px end-exclusive: the app style's height and
    width (its two border columns and the fill between), placed so THE STEM'S COLUMN (the scene's flag x) IS THE BOX'S
    FIRST FACE COLUMN, inside the one-line outline and the one-line bevel (architect 2026-10-03, late: the stem leaves
    the flag from its face, not its corner, as the app's flags sat before the bevel), so the box's left edge lies 2 LW
    left of the stem."""
    F = SCENE['flags']; x0 = f['x'] - 2 * LW
    return x0, F['y0'], x0 + F['border_w'] + flag_fill_w(th, f) + F['border_w'], F['y1']

# flags.style "bevelled": the stem's width, one Windows px (the renderer's line, LW), and so the width of its gap in
# the box's bottom lines (draw_flags_bevelled, draw_stems)
FLAG_STEM_W = LW

def draw_flags_bevelled(cr, th):
    """flags.style "bevelled": THE SONIC FOUNDRY FLAG (architect 2026-10-03, late; ACID's and Vegas' bevelled box) --
    the waveform pane and the flags are the program's own elements, drawn as a custom control of a Windows-95-era
    program: the base colours the program's, the SHADING the theme's. In the box (flag_box, the height and width the
    app style's), from the outside in: a one-line `flag_border` OUTLINE on all four sides (it keeps overlapping flags
    apart; the top line takes the old dark band's row), a ONE-LINE BEVEL (Windows' BDR_RAISEDINNER: light top and left,
    dark bottom and right, the dark pair last; Motif's top and bottom shadow), the face. The bevel is the theme family's
    own rule on the face (flags.rule, a catalog entry's flag_rule; toolkit_rules.flag_bevel), as that desktop shaded a
    3D face of that colour. The label at the app style's seat in the box (its x border_w + pad_l past the box's left,
    its baseline the scene's), in the colour recorded beside its face (flag_label_ink: `flag_label`, or
    `flag_label_red` on an invalid face; architect 2026-10-03, late). THE STATES (flags.states, flag_states): selected = the same bevel SUNKEN
    (dark top and left, light bottom and right) with the label one logical px right and down, a pushed button's face;
    invalid = the face `flag_fill_red` (Windows' error red), its bevel by the same rule; disabled = a disabled button: the face `ground`,
    the theme's own bevel_hilight / bevel_shadow, the label ENGRAVED (Windows' disabled text: the label in
    `emboss_hilight` one logical px right and down, then in bevel_shadow at its place) and no stem (draw_stems). THE STEM'S
    GAP (architect 2026-10-03, late): in every state with a stem (all but disabled) the bottom outline line and the
    bottom bevel line are broken at the stem's columns (the box's first face column, flag_box; FLAG_STEM_W wide) and
    the face runs through, so the face and the stem below it are one unbroken same-colour region (a non-antialiased
    selection of the face takes the stem with it); the sunken label's nudge moves neither the stem nor the gap. Every
    line is one logical px (LW, 2 device px), the renderer's unit for one Windows px."""
    F = SCENE['flags']; px = ui_font_px(th)
    for i, f in enumerate(F['flags']):
        x0, y0, x1, y1 = flag_box(th, f); face, sel, dis = flag_face(th, i)
        light, dark = ((th.get('bevel_hilight'), th.get('bevel_shadow')) if dis
                       else toolkit_rules.flag_bevel(th.flag_rule, face))
        fill(cr, x0, y0, x1, y1, th.get('flag_border'))                  # the outline (the face covers its inside)
        fill(cr, x0 + LW, y0 + LW, x1 - LW, y1 - LW, face)
        edge(cr, x0 + LW, y0 + LW, x1 - LW, y1 - LW, [(dark, light) if sel else (light, dark)])
        if not dis: fill(cr, f['x'], y1 - 2 * LW, f['x'] + FLAG_STEM_W, y1, face)   # the stem's gap
        lx, ly = x0 + F['border_w'] + F['pad_l'] + (LW if sel else 0), F['baseline'] + (LW if sel else 0)
        if dis:
            show(cr, C.SANS, px, f['text'], lx + LW, ly + LW, th.get('emboss_hilight'))
            show(cr, C.SANS, px, f['text'], lx, ly, th.get('bevel_shadow'))
        else:
            show(cr, C.SANS, px, f['text'], lx, ly, flag_label_ink(th, i))

def flat_flag_box(th, f):
    """flags.style "flat": a flag's box (x0, y0, x1, y1), device px end-exclusive: THE APP STYLE'S BOX (its two
    border columns and the fill between, the scene's rows), so the stem's column (the scene's flag x) is the box's
    FIRST FACE COLUMN, the first column of fill colour, as the app's flags sit today; the one-LW outline takes the
    border columns (border_w = LW on every scene)."""
    F = SCENE['flags']; assert F['border_w'] == LW, F['border_w']
    x0 = f['x'] - LW
    return x0, F['y0'], x0 + 2 * LW + flag_fill_w(th, f), F['y1']

def flat_selection_fill(th):
    """flags.style "flat": the selected pair's fill (Windows' Hilight) -- `selected_fill` as the theme states it, else
    Windows' #000080. `selected_fill` is read only when the theme states it (Theme.stated): its renderer default is the
    app's constant, kept for down_face, not Windows' Hilight."""
    return th.get('selected_fill') if 'selected_fill' in th.stated else C.parse_colour('#000080')

def flat_selected_face(th, i):
    """flags.style "flat": scene flag i's SELECTED FACE, the app's (architect 2026-10-03, the colour loop):
    `flag_fill_red_sel` over an invalid flag ("bright red means selected and error"), else `flag_fill_sel` -- a
    selected disabled flag's too (the provisional arm)."""
    sel, red, dis, ed = th.flag_states[i]
    return th.get('flag_fill_red_sel' if red and not dis else 'flag_fill_sel')

def flat_edit_colours(th, i):
    """flags.style "flat": the EDITING flag i's (face, text, selection fill, selected text), the app's in-place editor
    (render_flag_editor_box, architect 2026-10-03, set BX): THE SELECTED FLAG OPENED FOR EDIT -- its face the flag's
    selected face (flat_selected_face: `flag_fill_sel`, or `flag_fill_red_sel` over an invalid flag; the field pair
    plays no part), its text `flag_label_sel`, the selected substring in the selected pair: `selected_fill`
    (flat_selection_fill) under `selected_text` (Windows' HilightText, #FFFFFF where the theme states none)."""
    return flat_selected_face(th, i), th.get('flag_label_sel'), flat_selection_fill(th), th.get('selected_text')

def flat_stem_colour(th, i):
    """flags.style "flat": scene flag i's stem colour, or None (no stem): THE SELECTED FACE on a selected flag under
    flags.selection "face" (the app's: the selected flag's face and stem take the selected colour) and on the EDITING
    flag (the edited marker is the selected one, the app's editor stem), else the face's (`flag_fill`, `flag_fill_red`
    when invalid; the "underline" and "fill" mock forms keep the marker's own); a disabled flag none, selected or
    not."""
    sel, red, dis, ed = th.flag_states[i]
    if dis: return None
    if ed or (sel and th.flag_selection == 'face'): return flat_selected_face(th, i)
    return th.get('flag_fill_red' if red else 'flag_fill')

def face_underline(family, size_px):
    """The face's OWN UNDERLINE at size_px -> (top, thickness), device px as unrounded doubles, the top measured DOWN
    from the baseline: the font file's `post` table underlinePosition and underlineThickness over its `head` table's
    unitsPerEm (Roboto Regular: -150 and 100 of 2048, so 2.490 and 1.660 at 34 px). underlinePosition is the distance
    of the underline's TOP from the baseline, negative below it (the OpenType definition; FreeType's
    FT_Face.underline_position moves it to the stroke's centre, this does not)."""
    import struct
    d = open(C.SANS_FILE if family == C.SANS else C.MONO_FILE, 'rb').read()
    n = struct.unpack('>H', d[4:6])[0]
    tab = {d[12 + 16 * i:16 + 16 * i]: struct.unpack('>I', d[20 + 16 * i:24 + 16 * i])[0] for i in range(n)}
    pos, thick = struct.unpack('>hh', d[tab[b'post'] + 8:tab[b'post'] + 12])
    upem = struct.unpack('>H', d[tab[b'head'] + 18:tab[b'head'] + 20])[0]
    return -pos * size_px / upem, thick * size_px / upem

def flat_underline_rect(th, f):
    """flags.style "flat" with flags.selection "underline": scene flag f's underline (x0, y0, x1, y1), device px
    end-exclusive: Roboto's own underline at the label's size (face_underline), each term rounded at its element
    (std::nearbyint, the app's rule): its top row the baseline + nearbyint(top), its rows nearbyint(thickness) (at
    least one), its columns nearbyint of the shaped run's origin and end (the advance, as the editing band's)."""
    F = SCENE['flags']; px = ui_font_px(th)
    x0 = flat_flag_box(th, f)[0]
    lx = x0 + F['border_w'] + F['pad_l']
    top, thick = face_underline(C.SANS, px)
    y0 = F['baseline'] + int(np.rint(top))
    return (int(np.rint(lx)), y0, int(np.rint(lx + C.shape(C.SANS, px, f['text'])[1])), y0 + max(1, int(np.rint(thick))))

def draw_flags_flat(cr, th):
    """flags.style "flat": THE ACID FLAG (architect 2026-10-03, late, the bevelled flag retired as the candidate: "a
    3D surface with a cut through it, and when you press it the cut goes the opposite direction"). The box is the
    app style's (flat_flag_box): a ONE-LW OUTLINE on all four sides, `flag_border` (the catalog themes alias it to
    `@bevel_dkshadow`, the theme's dark colour) in every state but editing, the face inside it filled flat, no bevel,
    the label at the app style's seat (x border_w + pad_l past the box's left, the scene's baseline). THE STEM leaves
    from the box's FIRST FACE COLUMN and runs down CROSSING THE BOTTOM OUTLINE, drawn over it here (one LW wide,
    FLAG_STEM_W), then on through the well's top lines into the canvas (draw_stems), so the face, the outline's
    crossing and the stem are one same-colour column (flat_stem_colour). THE STATES (flags.states): unselected = the
    face `flag_fill`, the label `flag_label`; INVALID = the face `flag_fill_red`, the label `flag_label_red`;
    SELECTED under flags.selection "face" (the app's, architect 2026-10-03, the colour loop: A BRIGHTER FACE, NOTHING
    WHITE) = the face and the stem the selected face (`flag_fill_sel`, or `flag_fill_red_sel` when invalid), the label
    `flag_label_sel`, the outline unchanged; DISABLED = the face `ground`, the label ENGRAVED (in `emboss_hilight` one
    LW right and down, then in `bevel_shadow` at its place), no stem; SELECTED DISABLED (the app's provisional arm,
    Windows 95's highlighted disabled menu item) = the face `flag_fill_sel`, the label FLAT in `bevel_shadow` (no
    emboss), no stem; EDITING = the in-place editor, the selected flag opened for edit (flat_edit_colours): the face
    the flag's selected face (`flag_fill_red_sel` over an invalid flag), the outline `flag_border_edit` (black, the
    window frame), the text `flag_label_sel` with its selected substring -- the whole text as the editor opens, or the
    first flags.editing_selected characters -- in the selected pair: the `selected_fill` band over the face's rows
    inside the outline, its columns the nearbyint of the run's origin and of the selected run's end (the app's band,
    render_flag_editor_box; at least one column), the glyphs in `selected_text` clipped to it and in `flag_label_sel`
    clipped to its complement, so no pixel takes both inks; the caret is not drawn (its blink's dark half); the box
    keeps its width (architect: "the extending part is not necessary"), the stem the selected face. A refused Enter
    recolours nothing in the app (the red frame retired, 2026-10-03), so there is no refusal state to draw.
    THE SELECTION'S OTHER FORMS (mock options): "underline" (architect 2026-10-03, late; Windows 95 underlined every
    menu and button accelerator letter) changes SELECTED alone: the label's text UNDERLINED and nothing else -- no
    nudge, no width or height change -- at Roboto's own underline position and thickness at the label's size
    (flat_underline_rect), in the label's colour, straight through any descender (no skip-ink, as Windows drew it),
    across the shaped run's advance; "fill" (step 11) the face in the selected pair -- `selected_fill` behind
    (flat_selection_fill, Windows' #000080 where the theme states none), the label in `selected_text` -- the stem the
    marker's own (flat_stem_colour). Neither applies to a disabled flag, which shows only the "face" form's arm."""
    F = SCENE['flags']; px = ui_font_px(th)
    for i, f in enumerate(F['flags']):
        x0, y0, x1, y1 = flat_flag_box(th, f); sel, red, dis, ed = th.flag_states[i]
        sface = sel and th.flag_selection == 'face'
        sf = sel and not dis and th.flag_selection == 'fill'
        ul = sel and not dis and th.flag_selection == 'underline'
        if ed: face, ed_text, sel_fill, sel_text = flat_edit_colours(th, i)
        elif sface: face = flat_selected_face(th, i)
        elif dis: face = th.get('ground')
        elif sf: face = flat_selection_fill(th)
        else: face = th.get('flag_fill_red' if red else 'flag_fill')
        border = th.get('flag_border_edit' if ed else 'flag_border')
        fill(cr, x0, y0, x1, y1, border)                                   # the outline (the face covers its inside)
        fill(cr, x0 + LW, y0 + LW, x1 - LW, y1 - LW, face)
        sc = flat_stem_colour(th, i)
        if sc is not None: fill(cr, f['x'], y1 - LW, f['x'] + FLAG_STEM_W, y1, sc)   # the stem over the bottom outline
        lx, ly = x0 + F['border_w'] + F['pad_l'], F['baseline']
        if dis and sface:
            show(cr, C.SANS, px, f['text'], lx, ly, th.get('bevel_shadow'))
        elif dis:
            show(cr, C.SANS, px, f['text'], lx + LW, ly + LW, th.get('emboss_hilight'))
            show(cr, C.SANS, px, f['text'], lx, ly, th.get('bevel_shadow'))
        elif ed and th.flag_editing_selected == 0:                         # no selection: the text alone
            show(cr, C.SANS, px, f['text'], lx, ly, ed_text)
        elif ed:
            n = th.flag_editing_selected
            run = f['text'] if n is None else f['text'][:n]
            ix0 = int(np.rint(lx)); ix1 = int(np.rint(lx + C.shape(C.SANS, px, run)[1]))
            bx0, bx1 = max(ix0, x0 + LW), min(max(ix1, ix0 + 1), x1 - LW)
            iy0, iy1 = y0 + LW, y1 - LW
            fill(cr, bx0, iy0, bx1, iy1, sel_fill)
            cr.save()                                                      # the text off the band
            if bx0 > x0 + LW: cr.rectangle(x0 + LW, iy0, bx0 - (x0 + LW), iy1 - iy0)
            if x1 - LW > bx1: cr.rectangle(bx1, iy0, (x1 - LW) - bx1, iy1 - iy0)
            cr.clip(); show(cr, C.SANS, px, f['text'], lx, ly, ed_text); cr.restore()
            cr.save(); cr.rectangle(bx0, iy0, bx1 - bx0, iy1 - iy0); cr.clip()   # the text on the band
            show(cr, C.SANS, px, f['text'], lx, ly, sel_text); cr.restore()
        else:
            ink = th.get('flag_label_sel' if sface else 'selected_text' if sf else 'flag_label_red' if red else 'flag_label')
            show(cr, C.SANS, px, f['text'], lx, ly, ink)
            if ul: fill(cr, *flat_underline_rect(th, f), ink)

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
    if TABLET:      # window rows: the capture's band mapped channel by channel (tablet.waveform_columns)
        import tablet as TB
        return 0, TB.waveform_columns(WAVE), SCENE['waveform_map']['new_band'][1]
    _, _, c0, c1 = well_geometry(th); old_h, new_h = WAVE['h'], c1 - c0
    cols = {cls: [rescale_runs(r, old_h, new_h) for r in WAVE[cls]] for cls in ('ink', 'outline')}
    rel = WAVE['channel_split'] - WAVE['y0']
    return c0, cols, c0 + -(-rel * new_h // old_h)

def draw_waveform(arr, th, record=None):
    """The capture's runs into the picture's bytes, ink then outline; with a PaintRecord each class is one opaque paint."""
    y0, cols, _ = waveform_runs(th)
    for cls in ('ink', 'outline'):
        if not th.opt['elements'][cls]: continue        # elements.ink / elements.outline false: that class unpainted
        c = th.get(cls); m = np.zeros(arr.shape[:2], bool)
        for x, runs in enumerate(cols[cls]):
            for i in range(0, len(runs), 2): m[y0 + runs[i]:y0 + runs[i] + runs[i + 1], x] = True
        arr[m] = np.array(c, np.uint8)
        idx = np.flatnonzero(m)
        if record is not None and idx.size: record.paint(c, idx, np.full(idx.size, 255, np.uint8))

def draw_stems(cr, th):
    # THE STEMS' ROWS: the app of the scenes' commit runs every stem through the well's black border rows to the
    # well's bottom ("bordered"); with the other wells ("sunken", the list form) the stems span the derived canvas,
    # except when the FLAGS STAND ON THE WELL (flags_on_well, fonts.ui_px set): then they run from the well's top
    # through its top lines to the canvas's foot, the app's waveform_stem_band -- a stem leaves its box's bottom row
    # (the marker lane's last) straight into the well's first line. Z-order (paint_marker_stems, then
    # paint_playheads): the playhead's stem over every marker stem, except where a marker stands on its column --
    # there the app suppresses the playhead's whole stem (playhead_stem_suppressed) and the marker's stem shows.
    F = SCENE['flags']; P = SCENE['playhead']
    wt, wb, c0, c1 = well_geometry(th)
    if th.opt['well'] == 'bordered': y0, y1 = F['stem_y0'], F['stem_y1'] + wb - SCENE['lanes']['well'][1]
    else: y0, y1 = (wt if flags_on_well(th) else c0), c1
    el = th.opt['elements']      # elements.stems false: no marker stem; elements.playhead false: no playhead stem
    for i, f in enumerate(F['flags'] if el['stems'] else ()):
        if th.flag_style == 'bevelled':     # the stem takes the flag's face in every state; a disabled flag has none
            face, _, dis = flag_face(th, i)
            if not dis: fill(cr, f['x'], y0, f['x'] + FLAG_STEM_W, y1, face)
            continue
        if th.flag_style == 'flat':         # flat_stem_colour: the face's, a selected or editing flag's selected face; a disabled flag has none
            sc = flat_stem_colour(th, i)
            if sc is not None: fill(cr, f['x'], y0, f['x'] + FLAG_STEM_W, y1, sc)
            continue
        fill(cr, f['x'], y0, f['x'] + F['stem_w'], y1, th.get('flag_stem_sel' if f.get('selected') else 'flag_stem'))
    if el['playhead'] and not P.get('stem_suppressed'): fill(cr, P['col'], y0, P['col'] + P['w'], y1, th.get('playhead_stem'))

def clock_cell(th):
    """-> (cell width, the dirty mark's advance), device px at the clock's seated size: the app's reserved cell
    (paint_bottom_row_buttons_and_clock) -- the tab prefix ('B | '), the DD:DD.DDD specimen at the widest digit (nine
    equal monospace advances) and the dirty mark's one cell, reserved whether or not it is painted."""
    ck = SCENE['clock']
    dw = max(C.shape(C.MONO, ck['size_px'], d)[1] for d in '0123456789')
    mark = C.shape(C.MONO, ck['size_px'], '*')[1]
    return C.shape(C.MONO, ck['size_px'], 'B | ')[1] + 9 * dw + mark, mark

def clock_panel_rect(th):
    """-> (x0, y0, x1, y1) of the painted clock panel (clock_panel "sunken" / "status"), or None ("flat"): the clock
    cell (clock_cell, its width ceiled) with 4 logical px either side -- the app's kStatusPanelPadPx, 3 Windows px, at
    the set-AD scale (x 1.375: 4.125 -> 4; it does not follow another scale) -- over the bottom buttons' rows
    (buttons.case's height), the cell starting at the scene's clock x. draw_bottom paints it; stamp clears it."""
    if th.opt['clock_panel'] not in ('sunken', 'status'): return None
    ck = SCENE['clock']; cell = clock_cell(th)[0]
    bb0 = [b for b in button_geometry(th)[0] if b['row'] == 'bottom'][0]
    pp = SCENE['panel_pad'] if TABLET else 4 * S        # the tablet geometry: scaled_px(kStatusPanelPadPx 3) = 8
    return ck['x'] - pp, bb0['y'], ck['x'] + int(math.ceil(cell)) + pp, bb0['y'] + bb0['h']

def group_space_px(th):
    """The bottom row's space between two groups in device px: buttons.group_space x S, or 8 logical px (Windows'
    eight) when it is null."""
    if TABLET: return SCENE['group_space']      # scaled_px(kIconGroupSpacePx 8) = 22
    gs = th.opt['buttons']['group_space']
    return (8 if gs is None else gs) * S

def state_line(th):
    """-> (x, clip right, baseline, px, text) of row 8's STATE LINE, or None (no line). The app's line
    (paint_bottom_row_buttons_and_clock, architect 2026-10-03): words on the row's ground, no panel, the sans at the
    normal face (ui_font_px) in `label`, left-aligned one group space (group_space_px) past the status panel's right
    line, clipped one group space short of the bottom row's button block, on the row's solved baseline over its
    content rows (redesign_baseline, the clock's band). It stands beside the "status" panel alone (clock_panel
    "status", today's row 8: ONE status panel and a line); with any other clock_panel, with state_text null or "",
    with elements.state_line false, or with no span left, there is no line."""
    text = th.opt['state_text']; pr = clock_panel_rect(th)
    if th.opt['clock_panel'] != 'status' or not text or not th.opt['elements']['state_line']: return None
    gs = group_space_px(th); x = pr[2] + gs
    right = min(b['x'] for b in button_geometry(th)[0] if b['row'] == 'bottom') - gs
    if right <= x: return None
    bc = SCENE['bottom_content']; px = ui_font_px(th)
    return x, right, C.redesign_baseline(C.SANS, px, bc[0], bc[1] - bc[0]), px, text

def draw_bottom_border(cr, th):
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

def draw_bottom(cr, th):
    draw_bottom_border(cr, th)
    ck = SCENE['clock']; pr = clock_panel_rect(th); cx = ck['x']
    if pr is not None:
        x0, y0, x1, y1 = pr
        # "status": Windows' status-bar panel, ONE LW line whatever `relief` is (Shadow top and left, Hilight bottom
        # and right, the BR pair last); "sunken": relief_lines 'sunken' at the theme's relief
        lines = [(th.get('bevel_shadow'), th.get('bevel_hilight'))] if th.opt['clock_panel'] == 'status' else relief_lines(th, 'sunken')
        edge(cr, x0, y0, x1, y1, lines)
        # "status": THE RUN IS CENTRED IN THE CELL (the app, architect 2026-10-03): it starts half the dirty mark's
        # advance past the cell's origin, a fractional x, so the prefix and the digits stand centred in the reserved
        # cell whether or not the mark is painted
        if th.opt['clock_panel'] == 'status': cx = ck['x'] + clock_cell(th)[1] / 2
    show(cr, C.MONO, ck['size_px'], ck['text'], cx, ck['baseline'], th.get('clock'))
    sl = state_line(th)
    if sl is not None:      # the state line, clipped (never ellipsised) one group space short of the button block
        x, right, base, px, text = sl; bc = SCENE['bottom_content']
        cr.save(); cr.rectangle(x, bc[0], right - x, bc[1] - bc[0]); cr.clip()
        show(cr, C.SANS, px, text, x, base, th.get('label')); cr.restore()

# ------------------------------------------------------------------ the two optional surfaces (step 11, 2026-10-03)
# The modal dialog and the notification card have no measured scene: their LENGTHS are the app's own at the tablet's
# 275 % (APP_SCALE), each Windows-px constant converted at its element with nearbyint (render.h scaled_px; Python's
# round-half-even is std::nearbyint's default mode), read off the painters named below. THEIR LINES AND THEIR TEXT
# ARE THIS RENDERER'S, as every other element of a mock: a relief line is LW (the scene's logical px) and the words
# are the normal face at ui_font_px, so the two surfaces stand beside the scene's own buttons and flags in one
# grammar. Both are painted only when a theme switches them on (the `dialog` and `card` keys, default null).
APP_SCALE = 2.75

def app_px(windows_px, floor=None):
    """render.h scaled_px at the tablet's 275 %: nearbyint(windows_px x 2.75), floored where the app floors."""
    v = int(round(windows_px * APP_SCALE))
    return v if floor is None else max(floor, v)

def dialog_layout(th):
    """`dialog` -> the editor dialog's geometry on the bottom row's content band (paint_modal_dialog's editor branch,
    paint_handler.cpp: the label at the row's pad, the field, one pad, OK and Cancel), device px, or None when off:
    {'label': (x, baseline), 'field': (x0, y0, x1, y1) outer, 'inner': (x0, y0, x1, y1), 'baseline', 'text_x',
     'caret': (x, y0, x1, y1), 'buttons': [(x0, y0, x1, y1, word, label_w)], 'right'}.
    The app's constants: the row pad icon_row_pad_x 8, the button gap kModalButtonGapPx 6, the push button
    kModalBtnBoxPx 23 tall and kModalBtnMinWidthPx 75 wide at least (pads 7 + 7 round the ceiled run), the reserved
    focus ring kModalFocusFramePx 1, the field kModalFieldHeightPx 23 tall and kModalFieldWidthPx 378 wide (the room
    left by the label and the buttons caps it), its ink kModalFieldPadXPx 5 inside its two-line sunken edge, the
    caret one Windows px wide over the face's line band (nearbyint(baseline - ascent), nearbyint(ascent + descent)),
    standing after the text (the field focused, no selection). The field and the buttons are centred in the band
    ((band - box) // 2); the field's text sits on the field's own band, the label on the buttons' (redesign_baseline)."""
    dg = th.opt['dialog']
    if dg is None: return None
    px = ui_font_px(th); y0, y1 = SCENE['bottom_content']; ch = y1 - y0
    pad = app_px(8); bgap = app_px(6); btn_h = app_px(23); ring = app_px(1, 1)
    words = []
    for word in ('OK', 'Cancel'):
        lw = int(math.ceil(C.shape(C.SANS, px, word)[1]))
        words.append((word, lw, max(app_px(75), app_px(7) + lw + app_px(7))))
    buttons_w = sum(w for _, _, w in words) + bgap * (len(words) - 1)
    cx0, cx1 = pad, C.W - pad
    buttons_x_max = max(cx0, cx1 - ring - buttons_w)
    btn_y = y0 + (ch - btn_h) // 2
    label_w = int(math.ceil(C.shape(C.SANS, px, dg['label'])[1]))
    fx = cx0 + label_w + pad
    field_w = max(min(app_px(378), (buttons_x_max - ring - pad) - fx), app_px(29, 1))
    bx = min(fx + field_w + pad + ring, buttons_x_max)
    fh = app_px(23); fy = y0 + (ch - fh) // 2; fb = 2 * LW
    inner = (fx + fb, fy + fb, fx + field_w - fb, fy + fh - fb)
    base = C.redesign_baseline(C.SANS, px, fy, fh)
    tx = inner[0] + app_px(5)
    asc, desc = C.font_extents(C.SANS, px)[:2]
    cx = int(round(tx + C.shape(C.SANS, px, dg['text'])[1]))
    caret = (cx, int(round(base - asc)), cx + app_px(1, 1), int(round(base - asc)) + int(round(asc + desc)))
    buttons = []
    for word, lw, w in words:
        buttons.append((bx, btn_y, bx + w, btn_y + btn_h, word, lw)); bx += w + bgap
    return {'label': (cx0, C.redesign_baseline(C.SANS, px, btn_y, btn_h)), 'field': (fx, fy, fx + field_w, fy + fh),
            'inner': inner, 'baseline': base, 'text_x': tx, 'caret': caret, 'buttons': buttons, 'right': buttons[-1][2]}

def draw_dialog(cr, th):
    """`dialog`: the EDITOR DIALOG on the bottom row (dialog_layout), which the row's tenants -- the clock panel, the
    state line and the button block -- yield to whole (paint_modal_dialog; render() skips them): the label in
    `label`; the field's face `field_ground` under the PLAIN SUNKEN two-line edge (Shadow / Hilight outer, DkShadow /
    3DLight inner), its text in `field_text`, clipped to the band between the ink pads, and the caret in `field_text`
    (the field focused, the caret only, no selection); OK and Cancel as push buttons at rest (paint_button_box's PUSH
    family: `button_face` under the PLAIN RAISED edge, 3DLight / DkShadow outer, Hilight / Shadow inner), each label
    centred ((w - ceiled run) // 2) in `label`, no focus frame (an editor opens with the focus in its field)."""
    L = dialog_layout(th)
    if L is None: return
    px = ui_font_px(th); dg = th.opt['dialog']
    h, l, s_, d = (th.get(r) for r in BEVELS)
    show(cr, C.SANS, px, dg['label'], *L['label'], th.get('label'))
    x0, y0, x1, y1 = L['field']; ix0, iy0, ix1, iy1 = L['inner']
    fill(cr, ix0, iy0, ix1, iy1, th.get('field_ground'))
    edge(cr, x0, y0, x1, y1, [(s_, h), (d, l)])
    pad = app_px(5)
    cr.save(); cr.rectangle(ix0 + pad, iy0, ix1 - ix0 - 2 * pad, iy1 - iy0); cr.clip()
    show(cr, C.SANS, px, dg['text'], L['text_x'], L['baseline'], th.get('field_text'))
    fill(cr, *L['caret'], th.get('field_text'))
    cr.restore()
    for bx0, by0, bx1, by1, word, lw in L['buttons']:
        fill(cr, bx0, by0, bx1, by1, th.get('button_face'))
        edge(cr, bx0, by0, bx1, by1, [(l, d), (h, s_)])
        show(cr, C.SANS, px, word, bx0 + (bx1 - bx0 - lw) // 2, C.redesign_baseline(C.SANS, px, by0, by1 - by0), th.get('label'))

def draw_info_glyph(cr, x, y, size):
    """Breeze's dialog-information (icons.cpp kDialogInformationPaths, the 22-unit view box) at (x, y), size device px
    square: the rounded plate (3, 3)..(19, 19), corner radius 2, in kIconAccent #96BFDA, and the white 'i' -- the dot
    (10, 6)..(12, 8) and the stem (10, 10)..(12, 16) -- cairo's default antialiasing, as the app fills its paths."""
    k = size / 22.0
    cr.save(); cr.translate(x, y); cr.scale(k, k)
    r = 2.0; cr.new_sub_path()
    cr.arc(17, 5, r, -math.pi / 2, 0); cr.arc(17, 17, r, 0, math.pi / 2)
    cr.arc(5, 17, r, math.pi / 2, math.pi); cr.arc(5, 5, r, math.pi, 1.5 * math.pi); cr.close_path()
    C.src(cr, C.parse_colour('#96BFDA')); cr.fill()
    cr.rectangle(10, 6, 2, 2); cr.rectangle(10, 10, 2, 6)
    C.src(cr, (255, 255, 255)); cr.fill()
    cr.restore()

def card_rect(th):
    """`card` -> the one card's (x0, y0, x1, y1), its glyph's (x, y, size) and its text's (x, baseline), or None: the
    app's paint_notifications at 275 %, one line. The card is the icon row's content height (icon_row_content_h_px:
    the 22-px toolbar case -- 3 + 16 + 3, each rounded -- and 5 px of air a side), its ONE PAD the case's vertical
    margin ((card - case) // 2), the glyph 16 px square at the case's inset ((case - glyph) // 2) inside a case-square
    box one pad in from the left edge, the text one pad past that box, and the text's right air pad + inset; its width
    that chrome + the ceiled run, clamped to [kNotificationMinWidthPx 198, kNotificationMaxWidthPx 465]; flush right at
    kPanelPadPx 1 from the window's right edge and 1 under the menu row (notification_stack_bound). The baseline is
    redesign_baseline over the card's one-line band."""
    cd = th.opt['card']
    if cd is None: return None
    px = ui_font_px(th)
    case = app_px(3) + app_px(16) + app_px(3); card_h = 2 * app_px(5) + case
    pad = (card_h - case) // 2; glyph = app_px(16); inset = (case - glyph) // 2
    chrome = 2 * pad + case + pad + inset
    w = chrome + int(math.ceil(C.shape(C.SANS, px, cd['text'])[1]))
    w = min(max(w, app_px(198)), max(app_px(198), app_px(465)))
    panel = app_px(1, 1); x1 = C.W - panel; x0 = x1 - w; y0 = SCENE['lanes']['menu'][1] + panel
    return ((x0, y0, x1, y0 + card_h), (x0 + pad + inset, y0 + pad + inset, glyph),
            (x0 + pad + case + pad, C.redesign_baseline(C.SANS, px, y0, card_h)))

def draw_card(cr, th):
    """`card`: ONE NOTIFICATION CARD, the app's Info face (paint_popup_chrome): the `ground` inside one line a side,
    `bevel_light` (3DLight) top and left, `bevel_dkshadow` bottom and right (the dark pair last), square, no shadow;
    the normal class's glyph (draw_info_glyph) and the sentence in the `label` (card_rect's geometry; architect
    2026-10-03, step 15: "the card should just become ground", on both levels). Painted over
    everything under it, as the stack is the app's top layer."""
    cr_ = card_rect(th)
    if cr_ is None: return
    (x0, y0, x1, y1), (gx, gy, gs), (tx, base) = cr_
    fill(cr, x0, y0, x1, y1, th.get('ground'))
    edge(cr, x0, y0, x1, y1, [(th.get('bevel_light'), th.get('bevel_dkshadow'))])
    draw_info_glyph(cr, gx, gy, gs)
    show(cr, C.SANS, ui_font_px(th), th.opt['card']['text'], tx, base, th.get('label'))

def stamp(cr, th, text):
    # THE ROW-8 STAMP (architect 2026-10-04), with elements.state_line false: the file name at the RULER TIMESTAMPS'
    # DRAWN size (ruler_label_seat's px: 27.5 device px on the tablet geometry, not ruler_label_px's 32) in the
    # theme's `label`, its drawn glyphs' ink box (cairo's text extents) centred vertically on row 8's content rows,
    # 16 device px past the clock panel's right edge (the scene's label_box x0 when no panel is painted; the
    # dialog's right edge + 16 when the dialog stands in the row's tenants' place)
    if not th.opt['elements']['state_line']:
        px = ruler_label_seat(th)[0]; y0, y1 = SCENE['bottom_content']; pr = clock_panel_rect(th)
        dl = dialog_layout(th)
        x = (max(SCENE['label_box']['x0'], dl['right'] + 16) if dl is not None
             else SCENE['label_box']['x0'] if pr is None else pr[2] + 16)
        cr.save(); C.set_font(cr, C.SANS, px); e = cr.text_extents(text); cr.restore()
        base = int(round(y0 + (y1 - y0 - e.height) / 2 - e.y_bearing))
        show(cr, C.SANS, px, text, x, base, th.get('label'))
        return
    # with the state line on, the stamp as before: Roboto 20 px, `stamp` (140,140,140), its box's top-left at the
    # scene's label_box (x 300, y 1382 on every scene; buttons.case's 'bottom' shift moves it with the bottom row's
    # content top); x clears what row 8 paints left of it: max(label_box x0, the clock panel's right edge + 16, the
    # state line's painted end + 16 device px)
    asc = C.font_extents(C.SANS, 20)[0]; lb = SCENE['label_box']; pr = clock_panel_rect(th); sl = state_line(th)
    x = lb['x0'] if pr is None else max(lb['x0'], pr[2] + 16)
    if sl is not None: x = max(x, int(math.ceil(min(sl[0] + C.shape(C.SANS, sl[3], sl[4])[1], sl[1]))) + 16)
    dl = dialog_layout(th)
    if dl is not None: x = max(lb['x0'], dl['right'] + 16)     # the dialog stands in the row's tenants' place
    show(cr, C.SANS, 20, text, x, lb['y0'] + math.ceil(asc), th.get('stamp'))

def render(theme_path, out_path, label=False):
    th, rgb = render_rgb(theme_path, os.path.splitext(os.path.basename(out_path))[0] if label else None)
    C.save_png(out_path, rgb)
    return th

def render_rgb(theme_path, label_text=None, data=None, record=None):
    """-> (Theme, the picture as an H x W x 3 uint8 array): the theme at theme_path (or its object `data`, the path then
    naming it), stamped with label_text when given. With a PaintRecord `record` every paint is also recorded in it
    (TeeContext; the export)."""
    C.verify_fonts()
    th = Theme(theme_path, data)
    di, db = case_delta(th)
    global SCENE; SCENE = shift_scene(shift_scene(shift_scene(BASE_SCENE, di, 'icon'), lane_shift(th), 'trim'), pad_shift(th), 'ruler')
    SCENE = order_scene(seat_flags(shift_scene(SCENE, marker_shift(th), 'marker'), th), th.opt['lane_order'])
    SCENE = seat_clock(shift_scene(SCENE, db, 'bottom'), th)
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, C.W, C.H); cr = cairo.Context(surf)
    if record is not None: cr = TeeContext(cr, record)
    cr.set_antialias(cairo.ANTIALIAS_DEFAULT)
    draw_grounds(cr, th)
    draw_bars(cr, th)
    draw_menu(cr, th)
    draw_separators(cr, th)
    draw_buttons(cr, th, ('icon',))
    draw_trim(cr, th)
    draw_ruler(cr, th)
    if th.opt['elements']['flags']: draw_flags(cr, th)
    draw_well(cr, th)
    surf.flush()
    # the waveform's runs go straight into the surface's bytes (RGB24 = BGRx in memory)
    st = surf.get_stride(); mem = np.frombuffer(surf.get_data(), np.uint8).reshape(C.H, st)[:, :C.W * 4].reshape(C.H, C.W, 4)
    rgb = C.surface_to_rgb(surf); draw_waveform(rgb, th, record)
    mem[..., 0] = rgb[..., 2]; mem[..., 1] = rgb[..., 1]; mem[..., 2] = rgb[..., 0]; surf.mark_dirty()
    draw_stems(cr, th)
    if th.opt['dialog'] is None:
        draw_bottom(cr, th)
        draw_buttons(cr, th, ('bottom',))
    else:       # the row's tenants yield to the modal whole; its border-top is the lane's chrome and stays
        draw_bottom_border(cr, th)
        draw_dialog(cr, th)
    draw_card(cr, th)
    if label_text is not None: stamp(cr, th, label_text)
    if record is not None: record.finish()
    return th, C.surface_to_rgb(surf)

# ------------------------------------------------------------------ THE PICKER'S EXPORT (tools/palette/picker/)
# `render.py THEME --export DIR` writes what the colour picker app paints (picker/README.md): every scene of the theme's
# `picker` key as a picture whose every pixel is NAMED -- which colour role it shows, and over which roles it is
# blended -- so the picker repaints any element live, antialiased edges included, with no rasterizing on the device.
#
# THE PAINT RECORD (PaintRecord, TeeContext): the scene is rendered once with a recording context, which runs every
# cairo call twice -- on the picture, and on an A8 PROBE with an opaque source under the same path, clip, transform
# and font -- so each paint's 8-bit COVERAGE is cairo's own (the glyphs' and the icon masks' antialiasing as cairo
# rasterized them for the picture; nothing is re-implemented), and its colour names its role (common.src tells the
# context the RoleColour, its alias root the identity; any other colour is a literal '#RRGGBB'). Per pixel the record
# keeps the BASE (the last paint covering it fully) and the COVERAGE STACK (every partial paint since, in order). The
# palette composites nothing else: a source with alpha, an operator, a pattern source or a pixel no opaque paint
# covers is a hard fail.
#
# THE ROLES: every identity a scene paints becomes one row of the manifest's role table, with its RULE over the
# elements (picker_roles): an element's own role; a line of the chrome rule (`scale` of the chrome element, the
# Windows 95 proportion, or DkShadow's fixed #000000); the waveform outline's `derive` (the 50 % linear-light mix of
# the ink over the canvas, following both live); anything else a fixed `colour`.
#
# THE FILES: manifest.json, and per scene <scene>.base.pgm (binary P5: each byte the role-table index of the pixel's
# base) and <scene>.cover.bin (the stacks: COVER_MAGIC, a little-endian uint32 count, then per stacked pixel in
# ascending order its uint32 index y x W + x, a uint8 depth n >= 1 and n (uint8 role, uint8 coverage 1..254) pairs,
# oldest first); and themes.json, THE PRODUCT'S THEMES (product_themes, below), which the picker lists in its presets
# pop-up and opens as a strip of swatches.
#
# THE CHECKS, before anything is trusted: the record recomposed (colour.over_coverage, cairo's arithmetic) equals the
# render byte for byte; the written files read back equal it again; and for every colour set of picker_check_sets
# (each element moved, the chrome to a tinted ground with the rule's half-to-even ties and to a bright one whose lines
# cap, all moved at once) a fresh render of each scene at those colours equals the files recomposed at them -- the
# proof that no pixel is attributed to the wrong role.
COVER_MAGIC = b'WTCOVER1'
PAINT_DEPTH = 8                 # the deepest coverage stack accepted (the emboss is two, an icon's touching inks a few)

def write_pnm(path, arr):
    """Binary PNM, 8-bit: P6 for an H x W x 3 array, P5 for an H x W one."""
    arr = np.ascontiguousarray(arr, np.uint8)
    magic = b'P6' if arr.ndim == 3 else b'P5'
    with open(path, 'wb') as f: f.write(b'%s\n%d %d\n255\n' % (magic, arr.shape[1], arr.shape[0])); f.write(arr.tobytes())

def read_pnm(path):
    """The inverse of write_pnm (its own header shape only)."""
    b = open(path, 'rb').read(); magic, w, h, mx, rest = b.split(maxsplit=4)
    assert mx == b'255' and magic in (b'P6', b'P5')
    return np.frombuffer(rest, np.uint8).reshape((int(h), int(w), 3) if magic == b'P6' else (int(h), int(w)))

def colour_identity(c):
    """-> (identity, asked): a RoleColour's alias root and the role the painter asked for; any other colour its bytes
    as cairo takes them (cairo-color.c: each channel / 255 x 65535 + 0.5, truncated to 16 bits, its high byte to
    pixman) as '#RRGGBB', twice."""
    if isinstance(c, RoleColour): return c.root, c.asked
    h = C.hexs(tuple(int(v / 255.0 * 65535.0 + 0.5) >> 8 for v in c)); return h, h

class PaintRecord:
    """The paint record of one render (the head above): base, depth and the coverage stacks per pixel, flat y x W + x."""
    def __init__(self):
        n = C.W * C.H
        self.ids, self.index = [], {}
        self.base = np.full(n, -1, np.int32); self.depth = np.zeros(n, np.int32)
        self.lid = np.zeros((PAINT_DEPTH, n), np.int32); self.lcov = np.zeros((PAINT_DEPTH, n), np.uint8)
        self.asked = {}         # (asked role, identity) -> [px covered fully, px covered partly], the inventory

    def paint(self, c, idx, cov):
        """One paint of colour c: the pixels idx (flat, ascending, distinct) at coverages cov (1..255)."""
        ident, asked = colour_identity(c)
        if ident not in self.index: self.index[ident] = len(self.ids); self.ids.append(ident)
        i = self.index[ident]
        full = cov == 255; fi, pi, pc = idx[full], idx[~full], cov[~full]
        self.base[fi] = i; self.depth[fi] = 0
        if pi.size:
            d = self.depth[pi]
            if (d >= PAINT_DEPTH).any(): raise SystemExit(f'export: a pixel stacks more than {PAINT_DEPTH} partial paints')
            self.lid[d, pi] = i; self.lcov[d, pi] = pc; self.depth[pi] = d + 1
        a = self.asked.setdefault((asked, ident), [0, 0]); a[0] += int(fi.size); a[1] += int(pi.size)

    def finish(self):
        bad = np.flatnonzero(self.base < 0)
        if bad.size:
            raise SystemExit(f'export: {bad.size} pixels are covered by no opaque paint (the first at x {bad[0] % C.W}, '
                             f'y {bad[0] // C.W}); the picture is not the paints alone')

    def stacks(self):
        """-> (idx, n, roles P x K, coverages P x K) of the stacked pixels, ascending."""
        idx = np.flatnonzero(self.depth > 0); n = self.depth[idx]; k = int(n.max()) if idx.size else 0
        return idx, n, self.lid[:k, idx].T.copy(), self.lcov[:k, idx].T.copy()

class TeeContext:
    """THE RECORDING CONTEXT (the head above): the picture's cairo.Context and an A8 probe driven by the same calls.
    Path, clip, transform, antialias and font calls go to both; a paint runs on both and the probe's nonzero bytes are
    that paint's coverage (then cleared); the source is set on the picture alone (the probe's stays opaque) and named by
    the colour common.src notes first. Any call it does not know is refused, so no state can reach one side only."""
    _BOTH = frozenset(('save', 'restore', 'rectangle', 'move_to', 'line_to', 'arc', 'arc_negative', 'curve_to',
                       'close_path', 'new_sub_path', 'new_path', 'clip', 'translate', 'scale', 'set_antialias',
                       'select_font_face', 'set_font_size', 'set_font_options', 'set_line_width'))
    _PAINT = frozenset(('fill', 'fill_preserve', 'stroke', 'stroke_preserve', 'mask', 'mask_surface', 'show_glyphs'))
    _READ = frozenset(('get_font_face', 'get_scaled_font', 'text_extents'))

    def __init__(self, cr, record):
        self._cr, self._rec = cr, record
        self._ps = cairo.ImageSurface(cairo.FORMAT_A8, C.W, C.H)
        if self._ps.get_stride() != C.W: raise SystemExit('export: the A8 probe\'s stride is not the width')
        self._pc = cairo.Context(self._ps); self._pc.set_source_rgba(0, 0, 0, 1)
        self._buf = np.frombuffer(self._ps.get_data(), np.uint8)
        self._noted = None; self._source = None

    def note_source(self, c, a):
        if a is not None and a != 1.0:
            raise SystemExit(f'export: a paint with alpha {a} (the palette composites nothing but antialiasing)')
        self._noted = c

    def _take_source(self, r, g, b):
        c, self._noted = self._noted, None
        self._source = c if c is not None else tuple(v * 255.0 for v in (r, g, b))

    def set_source_rgb(self, r, g, b):
        self._take_source(r, g, b); self._cr.set_source_rgb(r, g, b)

    def set_source_rgba(self, r, g, b, a):
        if a != 1.0: raise SystemExit(f'export: a source with alpha {a}')
        self._take_source(r, g, b); self._cr.set_source_rgba(r, g, b, a)

    def __getattr__(self, name):
        if name.startswith('_'): raise AttributeError(name)
        if name in self._READ: return getattr(self._cr, name)
        if name in self._BOTH:
            f, g = getattr(self._cr, name), getattr(self._pc, name)
            def both(*a, **k): g(*a, **k); return f(*a, **k)
            return both
        if name in self._PAINT:
            f, g = getattr(self._cr, name), getattr(self._pc, name)
            def paint(*a, **k):
                if self._source is None: raise SystemExit(f'export: a {name} with no source set')
                r = f(*a, **k); g(*a, **k); self._take(); return r
            return paint
        raise SystemExit(f'export: the recording context does not know cairo\'s {name}; mirror it or refuse it')

    def _take(self):
        self._ps.flush()
        nz = np.flatnonzero(self._buf)
        if nz.size:
            self._rec.paint(self._source, nz, self._buf[nz].copy())
            self._buf[nz] = 0; self._ps.mark_dirty()

def blend_stacks(img, idx, n, lr, lc, role_rgb):
    """img (pixels x 3, int64) with each stacked pixel's paints blended over its base, oldest first (colour.over_coverage,
    cairo's arithmetic); role_rgb the colour of every role / identity index."""
    for k in range(lr.shape[1] if lr.ndim == 2 else 0):
        sel = n > k; ii = idx[sel]
        img[ii] = CL.over_coverage(role_rgb[lr[sel, k]], lc[sel, k].astype(np.int64)[:, None], img[ii])
    return img

def recompose(base, stacks, role_rgb):
    """-> the H x W x 3 picture of a base index map (flat) and its stacks at the roles' colours."""
    role_rgb = np.asarray(role_rgb, np.int64)
    img = role_rgb[base].copy()
    blend_stacks(img, *stacks, role_rgb)
    return img.reshape(C.H, C.W, 3).astype(np.uint8)

def picker_scene_theme(base, ov):
    """One picker scene's theme: `base` with the scene's overrides merged in (an object merges key by key, anything
    else replaces)."""
    def merge(a, b):
        out = json.loads(json.dumps(a))
        for k, v in b.items():
            out[k] = merge(out[k], v) if isinstance(v, dict) and isinstance(out.get(k), dict) else json.loads(json.dumps(v))
        return out
    return merge(base, ov)

def picker_apply(data, elements, colours):
    """The theme `data` with element colours {key: '#RRGGBB'} stated: an element's role in `colours` (any section
    alias of it dropped), the ground through the chrome key."""
    t = json.loads(json.dumps(data)); role = {e['key']: e['role'] for e in elements}
    for key, hexv in colours.items():
        r = role[key]
        if r == 'ground': t['chrome']['ground'] = hexv; continue
        for sec, keys in COLOUR_SECTIONS:
            for k, rr in keys.items():
                if rr == r: t.get(sec, {}).pop(k, None)
        t.setdefault('colours', {})[r] = hexv
    return t

def picker_roles(th, elements, idents):
    """-> the manifest's role table, one row per identity in `idents` order: {"name", and one of "element": key,
    "scale": {"of": key, "num", "den"}, "derive": {"from": key, "over": key or "#RRGGBB", "linear_mix"}, "colour"}
    (the head above). A rule follows elements only; a role whose rule would follow no element is its fixed colour."""
    by_role = {e['role']: e['key'] for e in elements}
    lines = dict(CL.CHROME_LINES); same = dict(CL.CHROME_SAME); fixed = dict(CL.CHROME_FIXED)
    rows = []
    for ident in idents:
        row = {'name': ident}
        if ident.startswith('#'): row['colour'] = ident
        elif ident in by_role: row['element'] = by_role[ident]
        elif th.chrome is not None and 'ground' in by_role and ident in CL.CHROME_ROLES:
            if ident in fixed: row['colour'] = C.hexs(fixed[ident])
            else: row['scale'] = {'of': by_role['ground'], 'num': lines[same.get(ident, ident)], 'den': CL.CHROME_DEN}
        elif ident == 'outline' and th.raw['outline'] == 'auto' and 'ink' in by_role:
            row['derive'] = {'from': by_role['ink'], 'over': by_role.get('canvas', C.hexs(th.get('canvas'))), 'linear_mix': 0.5}
        elif th.raw.get(ident) == 'auto':
            raise SystemExit(f'export: the role {ident!r} is painted by its "auto" rule, which the picker does not carry; '
                             f'state it, or port its rule (picker_roles, the picker\'s src/scene.cpp)')
        else: row['colour'] = C.hexs(th.get(ident))
        rows.append(row)
    return rows

def picker_role_colours(roles, el_colours):
    """-> (roles x 3) the role table's colours at the element colours {key: (r, g, b)}: the picker's src/scene.cpp
    role_colour, the same arithmetic (colour.scale_byte, colour.lin_mix)."""
    out = []
    for r in roles:
        if 'element' in r: c = el_colours[r['element']]
        elif 'colour' in r: c = C.parse_colour(r['colour'])
        elif 'scale' in r:
            sc = r['scale']; c = tuple(CL.scale_byte(v, sc['num'], sc['den']) for v in el_colours[sc['of']])
        else:
            d = r['derive']; over = C.parse_colour(d['over']) if d['over'].startswith('#') else el_colours[d['over']]
            c = CL.lin_mix(tuple(el_colours[d['from']]), tuple(over), d['linear_mix'])
        out.append(tuple(c))
    return np.array(out, np.int64)

def write_cover(path, idx, n, lr, lc):
    """<scene>.cover.bin (the head above)."""
    out = bytearray(COVER_MAGIC); out += int(idx.size).to_bytes(4, 'little')
    for p in range(idx.size):
        out += int(idx[p]).to_bytes(4, 'little'); out.append(int(n[p]))
        for k in range(int(n[p])): out.append(int(lr[p, k])); out.append(int(lc[p, k]))
    with open(path, 'wb') as f: f.write(out)

def read_cover(path):
    """The inverse of write_cover -> (idx, n, roles P x K, coverages P x K)."""
    b = open(path, 'rb').read()
    if b[:8] != COVER_MAGIC: raise SystemExit(f'{path}: not a cover file')
    cnt = int.from_bytes(b[8:12], 'little'); at = 12; recs = []
    for _ in range(cnt):
        i = int.from_bytes(b[at:at + 4], 'little'); k = b[at + 4]; at += 5
        recs.append((i, k, b[at:at + 2 * k:2], b[at + 1:at + 2 * k:2])); at += 2 * k
    if at != len(b): raise SystemExit(f'{path}: trailing bytes')
    K = max((r[1] for r in recs), default=0)
    idx = np.array([r[0] for r in recs], np.int64); n = np.array([r[1] for r in recs], np.int64)
    lr = np.zeros((cnt, K), np.int64); lc = np.zeros((cnt, K), np.int64)
    for p, r in enumerate(recs): lr[p, :r[1]] = list(r[2]); lc[p, :r[1]] = list(r[3])
    return idx, n, lr, lc

# THE CHECK'S COLOUR SETS (picker_check_sets): a chrome element at a TINT whose channels 32 and 96 hit the rule's
# half-to-even ties (32 x 255 / 192 = 42.5, 96 x 223 / 192 = 111.5) and at a BRIGHT ground whose x 255 / 192 caps at
# 255; every other element at a probe colour of its own; then all of them moved at once
CHECK_GROUNDS = ('#206048', '#E6D2B4')
CHECK_PROBES = ('#CC9966', '#203040', '#3366CC', '#7A2E5C', '#55AA22')

def picker_check_sets(elements):
    """-> [{key: '#RRGGBB'}, ...]: the export's and the laptop check's colour sets (above)."""
    sets, every, k = [], {}, 0
    for e in elements:
        if e['role'] == 'ground':
            sets += [{e['key']: g} for g in CHECK_GROUNDS]; every[e['key']] = CHECK_GROUNDS[1]
        else:
            p = CHECK_PROBES[k % len(CHECK_PROBES)]; k += 1; sets.append({e['key']: p}); every[e['key']] = p
    return sets + [every]

def picker_manifest_rgb(man):
    """{element key: (r, g, b)} of a manifest's own element colours."""
    return {e['key']: C.parse_colour(e['colour']) for e in man['elements']}

def export_scene(theme_path, out_dir):
    base = json.load(open(theme_path))
    th0 = Theme(theme_path, base)
    pk = th0.opt['picker']
    if pk is None: raise SystemExit(f'theme {theme_path}: --export needs the theme\'s picker key (the elements and their scenes)')
    elements = pk['elements']
    # 1. every scene rendered once with the record, the record recomposed against its own render
    recs = {}
    for name, ov in pk['scenes'].items():
        rec = PaintRecord(); th, rgb = render_rgb(theme_path, data=picker_scene_theme(base, ov), record=rec)
        own = np.array([th.get(i) if not i.startswith('#') else C.parse_colour(i) for i in rec.ids], np.int64)
        if not (recompose(rec.base, rec.stacks(), own) == rgb).all():
            bad = int((recompose(rec.base, rec.stacks(), own) != rgb).any(axis=2).sum())
            raise SystemExit(f'export: scene {name!r}: the paint record recomposed differs from the render at {bad} px')
        recs[name] = (rec, rgb)
    # 2. the role table: every identity painted, scenes in order
    idents = []
    for rec, _ in recs.values(): idents += [i for i in rec.ids if i not in idents]
    roles = picker_roles(th0, elements, idents); ri = {r['name']: k for k, r in enumerate(roles)}
    if len(roles) > 255: raise SystemExit(f'export: {len(roles)} roles; a base byte names at most 255')
    for e in elements:
        if e['role'] not in recs[e['scene']][0].ids:
            raise SystemExit(f'export: the element {e["key"]!r} ({e["role"]}) paints no pixel of its scene {e["scene"]!r}')
    # 3. the files
    os.makedirs(out_dir, exist_ok=True)
    man = {'width': C.W, 'height': C.H, 'active': pk['active'],
           'elements': [{'key': e['key'], 'name': e['name'], 'colour': C.hexs(th0.get(e['role'])), 'scene': e['scene']}
                        for e in elements],
           'roles': roles, 'scenes': []}
    for name, (rec, _) in recs.items():
        remap = np.array([ri[i] for i in rec.ids], np.int64)
        idx, n, lr, lc = rec.stacks()
        write_pnm(os.path.join(out_dir, name + '.base.pgm'), remap[rec.base].astype(np.uint8).reshape(C.H, C.W))
        write_cover(os.path.join(out_dir, name + '.cover.bin'), idx, n, remap[lr] if lr.size else lr, lc)
        man['scenes'].append({'name': name, 'base': name + '.base.pgm', 'cover': name + '.cover.bin'})
    with open(os.path.join(out_dir, 'manifest.json'), 'w') as f: json.dump(man, f, indent=1); f.write('\n')
    themes = product_themes()
    with open(os.path.join(out_dir, 'themes.json'), 'w') as f:
        json.dump({'source': 'docs/themes/catalog.json, checked against src/gui/theme_table.h; the light level',
                   'themes': themes}, f, indent=1); f.write('\n')
    # 4. read back: the files at the manifest's colours are each scene's render; at every check set, a fresh render
    back = picker_read(out_dir)
    sets = picker_check_sets(elements)
    for name, (rec, rgb) in recs.items():
        if not (picker_picture(back, name, picker_manifest_rgb(back[0])) == rgb).all():
            raise SystemExit(f'export: scene {name!r}: the written files recomposed are not the render')
        for cs in sets:
            want = render_rgb(theme_path, data=picker_apply(picker_scene_theme(base, pk['scenes'][name]), elements, cs))[1]
            el = {**picker_manifest_rgb(back[0]), **{k: C.parse_colour(v) for k, v in cs.items()}}
            got = picker_picture(back, name, el)
            if not (got == want).all():
                raise SystemExit(f'export: scene {name!r} at {cs}: the files recomposed differ from the render at '
                                 f'{int((got != want).any(axis=2).sum())} px (a pixel attributed to the wrong role)')
    # the report: per scene the painted px and the stacks; the inventory of every role a painter asked for
    for name, (rec, _) in recs.items():
        idx, n, _, _ = rec.stacks()
        print(f'scene {name}: {len(rec.ids)} roles painted, {idx.size} antialiased px ({int(n.sum())} stacked paints, '
              f'deepest {int(n.max()) if n.size else 0}); {os.path.getsize(os.path.join(out_dir, name + ".cover.bin"))} '
              f'cover bytes')
    print('roles:')
    rule = {r['name']: ('element ' + r['element'] if 'element' in r else
                        'scale %s x %d / %d' % (r['scale']['of'], r['scale']['num'], r['scale']['den']) if 'scale' in r else
                        'derive %s over %s' % (r['derive']['from'], r['derive']['over']) if 'derive' in r else
                        'fixed ' + r['colour']) for r in roles}
    asked = {}
    for name, (rec, _) in recs.items():
        for (a, i), (full, part) in rec.asked.items():
            v = asked.setdefault((a, i), {}); v[name] = (full, part)
    for (a, i) in sorted(asked, key=lambda k: (rule[k[1]], k[1], k[0])):
        px = '; '.join(f'{sc} {f} solid / {p} aa' for sc, (f, p) in asked[(a, i)].items())
        print(f'  {a:<18} -> {i:<16} {rule[i]:<30} {px}')
    print(f'exported {out_dir}: {len(elements)} elements ({", ".join(e["key"] for e in elements)}), {len(roles)} roles, '
          f'{len(recs)} scenes; the record and the written files recompose each render byte for byte, and at '
          f'{len(sets)} colour sets each fresh render too; themes.json: {len(themes)} product themes, '
          f'{min(len(t["colours"]) for t in themes)}..{max(len(t["colours"]) for t in themes)} colours each')

# THE PRODUCT'S THEMES (themes.json; architect 2026-10-04: the product's themes as starting points, a theme OPENED in
# the picker as a strip of every colour it records, any one adopted as an element's colour). Taken at export time from
# exactly what generates the product's table, never a hand-kept copy: the entries of docs/themes/catalog.json
# (tools/theme_catalog/levels.py entries(), which gen_theme_table.py writes src/gui/theme_table.h from), in its order,
# each at its LIGHT level (the theme as its makers recorded it; the dark level is the app's arithmetic, not a record).
# The generated table is read too and must list the same keys, names and light grounds in the same order, so a stale
# table is a hard fail ("regenerate it"), not a silent difference. A theme's COLOURS are every distinct byte triple
# the catalog records for it: its catalog roles (ground first), every raw value its source records under the source's
# own key names, and every value its toolkit's rule computed at import (provenance.rule.computed: KDE 3's relief, CDE's
# Motif shades of each colour set); each colour once, in that order of first appearance, with every name that records
# it.
THEME_TABLE = os.path.join(C.REPO, 'src', 'gui', 'theme_table.h')
THEME_CATALOG = os.path.join(C.REPO, 'docs', 'themes', 'catalog.json')

def product_themes():
    """-> [{"key", "name", "ground", "colours": [{"hex", "names": [...]}, ...]}, ...] (the head above)."""
    import re
    entries = json.load(open(THEME_CATALOG))['entries']
    text = open(THEME_TABLE).read()
    count = re.search(r'kGuiThemeCount = (\d+);', text)
    rows = re.findall(r'^    \{"([^"]*)", "([^"]*)", "[^"]*",\n     \{0x([0-9A-F]{6}),[^\n]*// light$', text, re.M)
    if not count or int(count.group(1)) != len(rows):
        raise SystemExit(f'export: {THEME_TABLE}: {len(rows)} entries read, not kGuiThemeCount; the table\'s shape changed')
    want = [(e['key'], e['name'], e['roles']['ground'].upper()[1:]) for e in entries]
    if [(k, n, g) for k, n, g in rows] != want:
        raise SystemExit(f'export: {THEME_TABLE} does not list the catalog\'s entries (keys, names, light grounds) in its '
                         f'order; regenerate it (tools/theme_catalog/gen_theme_table.py)')
    out = []
    for e in entries:
        names = {}
        for src in (e['roles'], e['raw'], (e['provenance'].get('rule') or {}).get('computed', {})):
            for k, v in src.items():
                if not re.fullmatch(r'#[0-9A-Fa-f]{6}', v): raise SystemExit(f'export: {e["key"]}: {k} is {v!r}, not #rrggbb')
                names.setdefault(v.upper(), []).append(k)
        out.append({'key': e['key'], 'name': e['name'], 'ground': e['roles']['ground'].upper(),
                    'colours': [{'hex': h, 'names': n} for h, n in names.items()]})
    return out

def picker_read(out_dir):
    """-> (manifest, {scene: (base flat, stacks)}): an export read back."""
    man = json.load(open(os.path.join(out_dir, 'manifest.json')))
    return man, {s['name']: (read_pnm(os.path.join(out_dir, s['base'])).reshape(-1).astype(np.int64),
                             read_cover(os.path.join(out_dir, s['cover']))) for s in man['scenes']}

def picker_picture(back, scene, el_colours):
    """An export's scene recomposed at the element colours {key: (r, g, b)}: the picker's picture, in Python."""
    man, sc = back
    base, stacks = sc[scene]
    return recompose(base, stacks, picker_role_colours(man['roles'], el_colours))

if __name__ == '__main__':
    argv = sys.argv[1:]; args = []
    for i, a in enumerate(argv):
        if a.startswith('--') or (i and argv[i - 1] in ('--scene', '--export')): continue
        args.append(a)
    if '--export' in argv:
        if len(args) != 1 or argv.index('--export') + 1 >= len(argv): raise SystemExit('usage: render.py <theme.json> --export <dir> [--scene <tag>]')
        export_scene(args[0], argv[argv.index('--export') + 1]); raise SystemExit(0)
    if len(args) != 2: raise SystemExit('usage: render.py <theme.json> <out.png> [--scene <tag>] [--label]\n'
                                        '       render.py <theme.json> --export <dir> [--scene <tag>]')
    th = render(args[0], args[1], '--label' in sys.argv)
    print('wrote', args[1], f'(scene {SCENE_TAG}; {th.quartet_report()})')
