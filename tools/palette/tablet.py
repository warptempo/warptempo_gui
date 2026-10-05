#!/usr/bin/env python3
# tools/palette/tablet.py — THE TABLET GEOMETRY (architect 2026-10-03, step 13: "bring the mock tool into greater
# accuracy ... we're just targeting the tablet"; "derive that from the code"). The app at gui_scale 275 on the
# tablet's whole 2304 x 1440 surface, every chrome length, line width, font size, baseline and text position derived
# THE APP'S WAY from THE APP'S AUTHORED CONSTANTS, read at their owners in src/gui (the working tree) every time this
# module loads, never measured off a mock or a capture:
#   * a length is scaled_px (render.h): std::nearbyint(windows_px x 2.75), ROUNDED AT THE ELEMENT; a composite is the
#     sum of its rounded parts (the case = lead + glyph + trail), never one rounding of the sum. Python's round() is
#     round-half-even, std::nearbyint's default mode;
#   * a font size is 13 / 10 Windows px x 2.75, an unrounded double (35.75 / 27.5), on the repository's
#     Roboto through common.py's road (cairo + FreeType, SLIGHT, hint metrics on; HarfBuzz shaping on
#     the same file), and every seat is the app's: redesign_baseline (box_y + floor((box_h + cap) / 2)) for a box,
#     line_baseline (line_y + ceil(ascent)) for a line.
# THE STATE IS SCENE 1002's (scene_1002.json, the 2026-10-02 capture): its view in DEVICE px (the ruler's ms per px
# and start, so every marker, the playhead, the trim bounds and the waveform's columns stand where they stood), its
# texts (flags, clock, legend), its enabled set and its glyph masks (glyphs/1002/: the app's glyphs are 44 device px
# at 275 as they were at 200, scaled_px(16) = 44, so the masks are at their own size) and its waveform, replayed
# into the 275 % drawing band channel by channel (waveform_columns). Nothing of the scene's 200 % GEOMETRY is read.
#
#   python3 tools/palette/tablet.py        prints THE DERIVATION TABLE (every length: Windows px -> device px at 275,
#                                           its owner, and the 275 % figure the owner's own comment records) and
#                                           exits 1 if any derived length disagrees with the app's record
#
# render.py draws this geometry for a theme stating "geometry": "tablet" (README.md, the tablet geometry).
import os, re, math, json, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C            # before cairo (FONTCONFIG_FILE)

PERCENT = 275
SCALE = PERCENT / 100.0
W, H = C.W, C.H
SRC = os.path.join(C.REPO, 'src', 'gui')
SCENE_TAG = '1002'            # the state the geometry is drawn in (the head)


def px(windows_px, floor=None):
    """render.h scaled_px at 275 %: nearbyint(windows_px x 2.75), floored where the accessor floors."""
    v = int(round(windows_px * SCALE))
    return v if floor is None else max(floor, v)


# ------------------------------------------------------------------ the constants, read at their owners
def _read(rel):
    return open(os.path.join(SRC, rel), encoding='utf-8').read()


def _num(text, rel, name):
    """`constexpr <type> NAME = <number>;` in src/gui/<rel> -> the number (int or float as written)."""
    m = re.search(r'constexpr\s+[\w:]+\s+' + re.escape(name) + r'\s*=\s*([0-9.]+)\s*;', text)
    if not m: raise SystemExit(f'tablet.py: {name} not found as a constexpr number in src/gui/{rel}')
    v = m.group(1)
    return float(v) if '.' in v else int(v)


def _array(text, rel, name):
    m = re.search(re.escape(name) + r'\[[^\]]*\]\s*=\s*\{([^}]*)\}', text)
    if not m: raise SystemExit(f'tablet.py: the array {name} not found in src/gui/{rel}')
    return [int(v) for v in re.findall(r'-?\d+', m.group(1))]


def _roster(text, rel, name):
    """A roster table `constexpr <Def> NAME[] = { {RedesignButton::X, icons::Icon::Y}, ... };` -> [X, ...]."""
    m = re.search(re.escape(name) + r'\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not m: raise SystemExit(f'tablet.py: the roster {name} not found in src/gui/{rel}')
    body = '\n'.join(l for l in m.group(1).split('\n') if not l.strip().startswith('//'))
    return re.findall(r'\{\s*RedesignButton::(\w+)\s*,', body)


def read_constants():
    """Every authored number the geometry reads, from the source as it stands: -> (K, the owners {name: file})."""
    rh, ph, ph_h, ah = _read('render.h'), _read('paint_handler.cpp'), _read('paint_handler.h'), _read('app_state.h')
    K, own = {}, {}
    for name in ('kMenuRowHeightPx', 'kIconGlyphPx', 'kIconCaseLeadPx', 'kIconCaseTrailXPx', 'kIconCaseTrailYPx',
                 'kIconRowAirPx', 'kIconGroupSpacePx', 'kTrimLaneHeightPx', 'kTrimArrowButtonPx',
                 'kRulerBaselineToMarkerPx', 'kMarkerLaneAirPx', 'kMarkerFlagPadLeftPx', 'kMarkerFlagPadRightPx',
                 'kMarkerFlagEdgePx', 'kMarkerFlagBorderPx', 'kReliefLinePx', 'kBottomRowBorderPx',
                 'kRedesignFontSizePx', 'kRulerLabelFontSizePx', 'kPlayheadHeadHeightPx',
                 'kPlayheadUnitPx', 'kTrimArrowGlyphCols'):
        K[name] = _num(rh, 'render.h', name); own[name] = 'render.h'
    for name in ('kMenuLabelPadPx', 'kStatusPanelPadPx', 'kTimeFieldHeightPx', 'kRulerLabelCapTopPx', 'kRulerMajorRisePx',
                 'kRulerMinorsPerStep', 'kRulerMinMinorPitchPx', 'kModalButtonGapPx', 'kModalFieldHeightPx',
                 'kModalFieldPadXPx', 'kModalFieldWidthPx', 'kModalBtnBoxPx', 'kModalBtnMinWidthPx',
                 'kModalBtnPadLeftPx', 'kModalBtnPadRightPx', 'kModalFocusFramePx'):
        K[name] = _num(ph, 'paint_handler.cpp', name); own[name] = 'paint_handler.cpp'
    nh, fo = _read('notifications.h'), _read('folder_overlay.h')
    for name in ('kNotificationMinWidthPx', 'kNotificationMaxWidthPx'):
        K[name] = _num(nh, 'notifications.h', name); own[name] = 'notifications.h'
    K['kPanelPadPx'] = _num(fo, 'folder_overlay.h', 'kPanelPadPx'); own['kPanelPadPx'] = 'folder_overlay.h'
    K['kPlayheadHeadHalf'] = _array(rh, 'render.h', 'kPlayheadHeadHalf'); own['kPlayheadHeadHalf'] = 'render.h'
    K['kTrimArrowGlyphRows'] = _array(rh, 'render.h', 'kTrimArrowGlyphRows'); own['kTrimArrowGlyphRows'] = 'render.h'
    K['kRulerLadderMs'] = _array(ph, 'paint_handler.cpp', 'kRulerLadderMs'); own['kRulerLadderMs'] = 'paint_handler.cpp'
    # the two inline lengths: the row pad (icon_row_pad_x, paint_handler.h) and the ruler label's air past its tick's
    # etched pair (paint_ruler_row: col + waveform_line_px() + scaled_px(2))
    m = re.search(r'inline int icon_row_pad_x\(\)\s*\{\s*return scaled_px\(([0-9.]+)\)', ph_h)
    if not m: raise SystemExit('tablet.py: icon_row_pad_x\'s scaled_px(N) not found in src/gui/paint_handler.h')
    K['icon_row_pad_x'] = float(m.group(1)); own['icon_row_pad_x'] = 'paint_handler.h'
    m = re.search(r'col \+\s*waveform_line_px\(\) \+\s*scaled_px\((\d+)\)', ph)
    if not m: raise SystemExit('tablet.py: the ruler label\'s scaled_px(N) air not found in paint_ruler_row')
    K['ruler_label_air'] = int(m.group(1)); own['ruler_label_air'] = 'paint_handler.cpp (paint_ruler_row)'
    # THE ROSTERS, in walk order, and the icon row's group leaders
    for name in ('kIconRowButtons', 'kIconRowViewGroup', 'kMarkerVerbGroup', 'kTransportWalkGroup',
                 'kTransportArrowGroup', 'kTransportGroup'):
        K[name] = _roster(ph, 'paint_handler.cpp', name); own[name] = 'paint_handler.cpp'
    m = re.search(r'redesign_button_opens_icon_group\(RedesignButton b\)\s*\{(.*?)return true;', ah, re.S)
    if not m: raise SystemExit('tablet.py: redesign_button_opens_icon_group not found in src/gui/app_state.h')
    K['leaders'] = re.findall(r'case RedesignButton::(\w+):', m.group(1)); own['leaders'] = 'app_state.h'
    return K, own


K, OWNER = read_constants()

# the faces at 275 % (render.h: a font size is not a grid point)
UI_PX = K['kRedesignFontSizePx'] * SCALE         # 35.75, the normal face
SMALL_PX = K['kRulerLabelFontSizePx'] * SCALE    # 27.5, the ruler's labels
# the app's measured faces at 275 % (paint_handler.cpp's redesign_baseline table, render.h's ruler block)
FACE_TABLE = {'sans': (UI_PX, 34, 9, 25), 'small': (SMALL_PX, 26, 7, 20)}


def verify_faces():
    """The two faces' ascent / descent / cap through this tool's road equal the app's measured table at 275 %."""
    for key, (size, asc, desc, cap) in FACE_TABLE.items():
        fam = C.SANS
        e = C.font_extents(fam, size); got = (math.ceil(e[0]), math.ceil(e[1]), int(round(C.cap_height(fam, size))))
        if got != (asc, desc, cap):
            raise SystemExit(f'tablet.py: the {key} face at {size} px measures ascent/descent/cap {got}, the app\'s table '
                             f'{(asc, desc, cap)}: the font road has drifted from the app\'s')


def shaped_w(family, size, text): return C.shape(family, size, text)[1]


# ------------------------------------------------------------------ the waveform's drawing band (render.h)
def waveform_band(area_y, area_h, inset):
    """-> (top, split, bottom), window rows: the area less the symmetric inset at each end, the two channels the
    halved-and-floored band (waveform_channel_split_row), the spare row at the bottom."""
    inset_h = area_h - 2 * inset
    split = inset + inset_h // 2
    return area_y + inset, area_y + split, area_y + split + inset_h // 2


# ------------------------------------------------------------------ the geometry
def build():
    """-> (scene, table): the scene dict render.py draws (the keys of scene_<tag>.json it reads, plus the tablet's own:
    glyph_off, panel_pad, group_space, ui_px, small_px, time_field_h, waveform_map) and the derivation table's rows."""
    verify_faces()
    base = json.load(open(os.path.join(C.HERE, f'scene_{SCENE_TAG}.json')))
    T = []          # (region, element, windows px, device px, owner, the owner's own 275 % record or None)
    def row(region, element, wpx, dev, owner, record=None):
        T.append((region, element, wpx, dev, owner, record)); return dev

    lw = row('lines', 'relief line (every edge line, dither cell, emboss offset, outline)', K['kReliefLinePx'],
             px(K['kReliefLinePx'], 1), 'render.h relief_line_px', 3)
    t = row('lines', 'waveform line (ticks, stems, the playhead\'s column)', 1, px(1, 1), 'render.h waveform_line_px', 3)

    # THE LANE STACK (main.cpp's lane table; every lane its own composite)
    menu_h = row('menu row', 'lane = content', K['kMenuRowHeightPx'], px(K['kMenuRowHeightPx'], 5),
                 'render.h menu_row_h_px', 52)
    lead, glyph = px(K['kIconCaseLeadPx']), px(K['kIconGlyphPx'])
    case_w = row('icon row', 'case width = lead + glyph + trail', '3 + 16 + 4',
                 lead + glyph + px(K['kIconCaseTrailXPx']), 'render.h icon_case_w_px', 63)
    case_h = row('icon row', 'case height = lead + glyph + trail', '3 + 16 + 3',
                 lead + glyph + px(K['kIconCaseTrailYPx']), 'render.h icon_case_h_px', 60)
    row('icon row', 'case lead (glyph offset x and y)', K['kIconCaseLeadPx'], lead, 'render.h icon_case_lead_px', 8)
    row('icon row', 'glyph', K['kIconGlyphPx'], glyph, 'render.h icon_glyph_px', 44)
    air = row('icon row', 'air above and below the case', K['kIconRowAirPx'], px(K['kIconRowAirPx']),
              'render.h kIconRowAirPx', 14)
    icon_h = row('icon row', 'lane = air + case + air', '5 + 22 + 5', 2 * air + case_h, 'render.h icon_row_h_px', 88)
    gap = row('icon row', 'group space', K['kIconGroupSpacePx'], px(K['kIconGroupSpacePx']), 'render.h icon_group_space_px', 22)
    pad = row('icon row', 'row pad (both ends; the menu legend\'s right margin)', K['icon_row_pad_x'],
              px(K['icon_row_pad_x']), 'paint_handler.h icon_row_pad_x', 22)
    trim_h = row('trim lane', 'lane', K['kTrimLaneHeightPx'], px(K['kTrimLaneHeightPx'], 3), 'render.h trim_lane_h_px', 44)
    btn = row('trim lane', 'arrow button (square)', K['kTrimArrowButtonPx'], px(K['kTrimArrowButtonPx'], 3),
              'render.h trim_arrow_button_w_px', 44)
    u = px(1, 1); rows_ = K['kTrimArrowGlyphRows']
    gw, gh = K['kTrimArrowGlyphCols'] * u, rows_[-1] * u
    row('trim lane', 'arrow glyph (4u x 7u, u = scaled_px(1))', '4 x 7', f'{gw} x {gh}', 'render.h kTrimArrowGlyphRows', '12 x 21')
    row('trim lane', 'arrow glyph offset in the button ((b - glyph) // 2)', '-', f'+{(btn - gw) // 2}, +{(btn - gh) // 2}',
        'render.cpp paint_trim_arrow_button', '+16, +11')
    # the ruler: the label LINE seated so its cap top lands kRulerLabelCapTopPx under the lane's top
    s_asc, s_cap = FACE_TABLE['small'][1], FACE_TABLE['small'][3]
    rpad = row('ruler', 'label pad = max(0, scaled_px(4) - (ceil(ascent) - cap))', K['kRulerLabelCapTopPx'],
               max(0, px(K['kRulerLabelCapTopPx']) - (s_asc - s_cap)), 'paint_handler.cpp ruler_label_baseline_px', 5)
    rbase = row('ruler', 'label baseline under the lane top = pad + ceil(ascent)', '-', rpad + s_asc,
                'paint_handler.cpp ruler_label_baseline_px', 31)
    to_marker = row('ruler', 'baseline to the marker lane', K['kRulerBaselineToMarkerPx'], px(K['kRulerBaselineToMarkerPx']),
                    'render.h kRulerBaselineToMarkerPx', 19)
    ruler_h = row('ruler', 'lane = baseline + baseline-to-marker', '-', rbase + to_marker, 'paint_handler.cpp ruler_lane_h_px', 50)
    rise = row('ruler', 'major tick rise above the marker lane', K['kRulerMajorRisePx'], px(K['kRulerMajorRisePx']),
               'paint_handler.cpp kRulerMajorRisePx', 8)
    lab_air = row('ruler', 'label x past its tick = t + scaled_px(2)', '1 + 2', t + px(K['ruler_label_air']),
                  'paint_handler.cpp paint_ruler_row', 9)
    head_rows = row('ruler', 'playhead head rows', K['kPlayheadHeadHeightPx'], px(K['kPlayheadHeadHeightPx']),
                    'render.h playhead_head_h_px', 22)
    halves = []
    for r in range(head_rows):          # playhead_head_half_px: the source row by the truncating inverse
        src = min(max(int(r / SCALE), 0), K['kPlayheadHeadHeightPx'] - 1)
        halves.append(px(K['kPlayheadHeadHalf'][src], 1))
    row('ruler', 'head widest row = 2 x half(0) + t', f'2 x {K["kPlayheadHeadHalf"][0]} + 1', 2 * halves[0] + t,
        'render.h playhead_head_half_px', 35)
    # the marker lane: the flag box (edge + ceil(ascent) + ceil(descent)) and its air above
    u_asc, u_desc = FACE_TABLE['sans'][1], FACE_TABLE['sans'][2]
    edge_h = row('marker lane', 'flag edge band (top and bottom outline rows)', K['kMarkerFlagEdgePx'],
                 px(K['kMarkerFlagEdgePx'], 1), 'render.h marker_flag_edge_h_px', 3)
    box_h = row('marker lane', 'flag box = edge + ceil(ascent) + ceil(descent)', '-', edge_h + u_asc + u_desc,
                'paint_handler.cpp marker_lane_rows', 46)
    m_air = row('marker lane', 'air above the box', K['kMarkerLaneAirPx'], px(K['kMarkerLaneAirPx'], 1),
                'render.h marker_lane_air_px', 3)
    marker_h = row('marker lane', 'lane = air + box', '-', m_air + box_h, 'paint_handler.cpp marker_lane_h_px', 49)
    fbase = row('marker lane', 'label baseline under the box top = edge + ceil(ascent)', '-', edge_h + u_asc,
                'paint_handler.cpp marker_flag_baseline_px', 37)
    border_w = row('marker lane', 'flag border (left column, the run\'s closing column)', K['kMarkerFlagBorderPx'],
                   px(K['kMarkerFlagBorderPx'], 1), 'render.h marker_flag_border_px', 3)
    pad_l = row('marker lane', 'flag pad left', K['kMarkerFlagPadLeftPx'], px(K['kMarkerFlagPadLeftPx'], 1),
                'render.h marker_flag_pad_left_px', 6)
    pad_r = row('marker lane', 'flag pad right', K['kMarkerFlagPadRightPx'], px(K['kMarkerFlagPadRightPx'], 1),
                'render.h marker_flag_pad_right_px', 6)
    bb = row('bottom row', 'top row of ground (where the border stood)', K['kBottomRowBorderPx'],
             px(K['kBottomRowBorderPx'], 1), 'render.h bottom_row_border_h_px', 3)
    bottom_h = row('bottom row', 'lane = top row + the icon row\'s content', '1 + 32', bb + icon_h, 'render.h bottom_row_h_px', 91)
    top_h = menu_h + icon_h + trim_h + ruler_h + marker_h
    row('stack', 'the top lanes whole (menu + icon + trim + ruler + marker)', '-', top_h, 'main.cpp strip_total_h', 283)
    wave_h = row('stack', 'waveform area = leftover (max_waveform_height 0: no maximum; both gaps 0)', '-',
                 H - top_h - bottom_h, 'main.cpp waveform_clamped_h', 1066)
    border = row('well', 'border a side = 2 relief lines (plain sunken)', '2 x 1', 2 * lw, 'render.h waveform_border_px', 6)
    inset = row('well', 'waveform inset (drawing band) a side', K['kPlayheadUnitPx'], px(K['kPlayheadUnitPx'], 2),
                'render.h waveform_inset_px', 16)
    # the bottom row and the clock
    panel_pad = row('bottom row', 'status panel pad round the clock cell', K['kStatusPanelPadPx'],
                    px(K['kStatusPanelPadPx']), 'paint_handler.cpp kStatusPanelPadPx', 8)
    m_pad = row('menu row', 'anchor label pad (per side)', K['kMenuLabelPadPx'], px(K['kMenuLabelPadPx']),
                'paint_handler.cpp kMenuLabelPadPx', 19)
    # the two optional surfaces (render.py dialog_layout and card_rect place them with these lengths)
    for name, el, own_ in (('kModalFieldHeightPx', 'dialog field height (its two-line sunken edge inside)', 'paint_modal_dialog'),
                           ('kModalFieldWidthPx', 'dialog field width (the room left caps it)', 'paint_modal_dialog'),
                           ('kModalFieldPadXPx', 'dialog field ink pad inside its edge', 'paint_modal_dialog'),
                           ('kModalBtnBoxPx', 'dialog push button height', 'paint_modal_dialog'),
                           ('kModalBtnMinWidthPx', 'dialog push button minimum width', 'paint_modal_dialog'),
                           ('kModalBtnPadLeftPx', 'dialog push button label pad (each side)', 'paint_modal_dialog'),
                           ('kModalButtonGapPx', 'dialog button gap', 'paint_modal_dialog'),
                           ('kModalFocusFramePx', 'dialog focus ring reserve', 'paint_modal_dialog'),
                           ('kNotificationMinWidthPx', 'card minimum width', 'paint_notifications'),
                           ('kNotificationMaxWidthPx', 'card maximum width', 'paint_notifications'),
                           ('kPanelPadPx', 'card inset from the window edge and the menu row', 'notification_stack_bound')):
        row('surfaces', el, K[name], px(K[name], 1 if name == 'kModalFocusFramePx' or name == 'kPanelPadPx' else None),
            f'{OWNER[name]} {name} ({own_})')
    row('fonts', 'normal face (menu, legend, flags, the clock, state line, dialog, card)', K['kRedesignFontSizePx'], UI_PX,
        'render.h redesign_font_size_px', 35.75)
    row('fonts', 'small face (ruler labels)', K['kRulerLabelFontSizePx'], SMALL_PX, 'render.h ruler_label_font_size_px', 27.5)

    # ---- the rows, top to bottom
    y = 0; L = {}
    for name, h in (('menu', menu_h), ('icon', icon_h), ('trim', trim_h), ('ruler', ruler_h), ('marker', marker_h),
                    ('well', wave_h), ('bottom', bottom_h)):
        L[name] = [y, y + h]; y += h
    assert y == H, y
    area_y, area_h = L['well'][0], wave_h
    canvas = [area_y + border, area_y + area_h - border]

    # ---- the menu row: anchors flush from x 0, nearbyint(shaped) + 2 pads; the legend flush right at the row pad
    menu_base = C.redesign_baseline(C.SANS, UI_PX, 0, menu_h)
    row('menu row', 'baseline = floor((52 + cap 25) / 2)', '-', menu_base, 'paint_handler.cpp redesign_baseline', 38)
    menu = {'pad': m_pad, 'items': base['menu']['items'], 'legend': base['menu']['legend'], 'legend_right': W - pad,
            'baseline': menu_base}

    # ---- the buttons (the icon row's walk and the bottom row's right block), the scene's enabled set and toggles
    by_name = {(b['row'], b['button']): b for b in base['buttons']}
    def button(rowname, name, x, y_):
        b = by_name.get((rowname, name))
        if b is None: raise SystemExit(f'tablet.py: the app\'s {rowname} row has {name}, scene {SCENE_TAG} has no glyph for it')
        return dict(b, x=x, y=y_, w=case_w, h=case_h, glyph_x=x + lead, glyph_y=y_ + lead, glyph_px=glyph)
    buttons = []
    btn_y = L['icon'][0] + air
    view_n = len(K['kIconRowViewGroup'])
    view_x0 = W - pad - view_n * case_w
    x = pad; first = True
    for name in K['kIconRowButtons']:
        if not first and name in K['leaders']: x += gap
        first = False
        buttons.append(button('icon', name, x, btn_y)); x += case_w
    left_end = x
    for i, name in enumerate(K['kIconRowViewGroup']): buttons.append(button('icon', name, view_x0 + i * case_w, btn_y))
    if left_end > view_x0 - gap: raise SystemExit('tablet.py: the icon row\'s left walk runs under its view group')
    row('icon row', 'left walk end (pad + 23 cases + 5 gaps)', '8 + 23 x 23 + 5 x 8', left_end,
        'paint_handler.cpp paint_icon_row', 1581)
    row('icon row', 'view group from the right (gap + 3 cases + pad)', '8 + 3 x 23 + 8', W - view_x0 + gap,
        'paint_handler.cpp paint_icon_row', 233)
    content = [L['bottom'][0] + bb, L['bottom'][1]]
    bbtn_y = content[0] + air
    groups = [K['kMarkerVerbGroup'], K['kTransportWalkGroup'], K['kTransportArrowGroup'], K['kTransportGroup']]
    block_w = sum(len(g) for g in groups) * case_w + (len(groups) - 1) * gap
    ax = W - pad - block_w
    row('bottom row', 'right block = 17 cases + 3 gaps', '17 x 23 + 3 x 8', block_w, 'paint_handler.cpp paint_bottom_row_buttons_and_clock', 1137)
    row('bottom row', 'right block x = W - pad - block', '-', ax, 'paint_handler.cpp paint_bottom_row_buttons_and_clock', 1145)
    block_x = ax
    for gi, g in enumerate(groups):
        if gi: ax += gap
        for name in g: buttons.append(button('bottom', name, ax, bbtn_y)); ax += case_w
    group_starts = []; ax = block_x
    for gi, g in enumerate(groups):
        if gi: ax += gap
        group_starts.append(ax); ax += len(g) * case_w

    # ---- the trim lane: the scene's bound columns (its view), the arrow buttons trim_endcap_rect places
    tr = base['trim']; mid = (tr['bar'][0] + tr['bar'][1]) / 2
    begin = [h[0] for h in tr['handles'] if (h[0] + h[1]) / 2 < mid]
    end = [h[1] for h in tr['handles'] if (h[0] + h[1]) / 2 >= mid]
    handles = ([[begin[0], begin[0] + btn]] if begin else []) + ([[end[0] - btn, end[0]]] if end else [])
    trim = {'y0': L['trim'][0], 'y1': L['trim'][1], 'bar': tr['bar'], 'handles': handles, 'caps': handles, 'grip': None}

    # ---- the ruler: the scene's view (ms per device px, its start), the app's comb and labels
    R0 = base['ruler']; ms_per_px, vp_ms = R0['ms_per_px'], R0['vp_ms']
    step = next((s for s in K['kRulerLadderMs']
                 if (s / K['kRulerMinorsPerStep']) / ms_per_px >= K['kRulerMinMinorPitchPx'] * SCALE),
                K['kRulerLadderMs'][-1])
    end_ms = vp_ms + ms_per_px * W
    major_col = lambda k: int(round((k * step - vp_ms) / ms_per_px))
    ticks, labels = [], []
    k = math.floor(vp_ms / step)
    while k * step <= end_ms:
        mx = major_col(k); seg = major_col(k + 1) - mx
        for i in range(K['kRulerMinorsPerStep']):
            col = mx if i == 0 else mx + int(round(i * seg / K['kRulerMinorsPerStep']))
            if col < 0 or col >= W: continue
            ticks.append({'x': col, 'major': i == 0, 'ms': k * step})
            if i == 0 and k * step >= 0:
                ms = k * step
                txt = f'{ms // 60000}:{(ms % 60000) // 1000:02d}' + (f'.{ms % 1000:03d}' if step % 1000 else '')
                labels.append({'text': txt, 'x': col + lab_air})
        k += 1
    marker_y = L['marker'][0]
    ruler = {'y0': L['ruler'][0], 'y1': L['ruler'][1], 'major_top': marker_y - rise, 'minor_top': marker_y,
             'tick_bottom': L['marker'][1], 'tick_w': t, 'baseline': L['ruler'][0] + rbase, 'ms_per_px': ms_per_px,
             'vp_ms': vp_ms, 'step_ms': step, 'ticks': ticks, 'labels': labels}
    P0 = base['playhead']
    playhead = {'col': P0['col'], 'w': t, 'head_top': marker_y - head_rows, 'head_rows': head_rows, 'head_half': halves,
                'head_alpha': 1.0, 'stem_y0': area_y, 'stem_y1': area_y + area_h - border,
                'stem_suppressed': P0.get('stem_suppressed', False)}

    # ---- the flags: the box band (the lane less its air), each box pad + nearbyint(shaped) + pad between borders
    F0 = base['flags']; box_y = L['marker'][0] + m_air
    flags = {'y0': box_y, 'y1': box_y + box_h, 'border_w': border_w, 'edge_h': edge_h, 'pad_l': pad_l,
             'baseline': box_y + fbase, 'stem_w': t, 'stem_y0': area_y, 'stem_y1': area_y + area_h - border,
             'flags': [dict(f, w=pad_l + int(round(shaped_w(C.SANS, UI_PX, f['text']))) + pad_r) for f in F0['flags']]}
    for f in flags['flags']: f['clipped'] = f['x'] + f['w'] + border_w > W

    # ---- the clock: THE TIME FIELD (paint_handler.cpp, architect 2026-10-05) -- kTimeFieldHeightPx centred in the
    # content rows, the reserved cell at the row pad (the widest tab letter, ' | ', the widest-digit specimen), the
    # cap band centred in the field
    ck = base['clock']
    field_h = row('bottom row', 'time field height', K['kTimeFieldHeightPx'], px(K['kTimeFieldHeightPx']),
                  'paint_handler.cpp kTimeFieldHeightPx', 47)
    field_y = content[0] + (content[1] - content[0] - field_h) // 2
    row('bottom row', 'time field top in the 88-row content = (88 - 47) / 2', '-', field_y - content[0],
        'paint_handler.cpp time_field_rect', 20)
    clock_base = C.redesign_baseline(C.SANS, UI_PX, field_y, field_h)
    row('bottom row', 'clock baseline in the 47-row field = floor((47 + cap 25) / 2)', '-', clock_base - field_y,
        'paint_handler.cpp redesign_baseline', 36)
    wd = max('0123456789', key=lambda d: shaped_w(C.SANS, UI_PX, d))
    cell = max(shaped_w(C.SANS, UI_PX, l) for l in 'AB') + shaped_w(C.SANS, UI_PX, ' | ') + \
        shaped_w(C.SANS, UI_PX, 'DD:DD.DDD'.replace('D', wd))
    panel_r = pad - panel_pad + math.ceil(cell) + 2 * panel_pad
    row('bottom row', 'time field right edge (cell at the pad, ceiled, + 2 pads)', '-', panel_r,
        'paint_handler.cpp paint_bottom_row_buttons_and_clock', 239)
    row('bottom row', 'state line x = field right + group space', '-', panel_r + gap,
        'paint_handler.cpp paint_bottom_row_buttons_and_clock', 261)
    row('bottom row', 'state line clip right = block x - group space', '-', block_x - gap,
        'paint_handler.cpp paint_bottom_row_buttons_and_clock', 1123)
    clock = {'text': ck['text'], 'x': pad, 'baseline': clock_base, 'size_px': UI_PX}

    # ---- the waveform: the 200 % capture's drawing band -> the 275 % band, channel by channel (waveform_columns)
    old_area = base['lanes']['well']; old_inset = 16      # a6f53163: scaled_px(8 laptop px) at 200 %
    old_band = waveform_band(old_area[0], old_area[1] - old_area[0], old_inset)
    from_wave = json.load(open(os.path.join(C.HERE, base['waveform'])))
    if old_band[1] != from_wave['channel_split']:
        raise SystemExit(f'tablet.py: scene {SCENE_TAG}\'s measured channel split {from_wave["channel_split"]} is not the '
                         f'a6f53163 band\'s {old_band[1]}')
    new_band = waveform_band(area_y, area_h, inset)
    row('well', 'drawing band top / channel split / bottom (window rows)', '-', '/'.join(map(str, new_band)),
        'render.h waveform_channel_split_row', None)

    scene = {'tag': 'tablet', 'source': f'derived (tools/palette/tablet.py) in scene {SCENE_TAG}\'s state', 'size': [W, H],
             'scale': base['scale'], 'lanes': L, 'canvas': canvas, 'well_border_rows': [border, border],
             'bottom_border': {'y': L['bottom'][0], 'h': 0, 'x0': 0, 'x1': W}, 'bottom_content': content,
             'menu': menu, 'buttons': buttons, 'separators': [], 'glyph_px': glyph, 'glyphs': base['glyphs'],
             'trim': trim, 'ruler': ruler, 'playhead': playhead, 'flags': flags, 'clock': clock,
             'waveform': base['waveform'], 'constants': base['constants'], 'icon_inks': base['icon_inks'],
             'disabled_mix': base['disabled_mix'], 'label_box': base['label_box'],
             # the tablet's own keys (render.py reads them under "geometry": "tablet")
             'glyph_off': lead, 'panel_pad': panel_pad, 'group_space': gap, 'ui_px': UI_PX, 'small_px': SMALL_PX,
             'time_field_h': field_h,
             'waveform_map': {'old_band': list(old_band), 'new_band': list(new_band)},
             'bottom_groups': group_starts, 'block_x': block_x}
    return scene, T


SCENE, TABLE = build()


def waveform_columns(wave):
    """The capture's runs (`wave`, waveform_<tag>.json: relative to its canvas top) -> {'ink': cols, 'outline': cols},
    each column's runs in WINDOW rows at 275 %: per channel, the 200 % band's rows map onto the 275 % band's through
    the nearest-neighbour vertical map (destination row r of a channel takes source row floor(r x old_h / new_h) of
    the same channel), so each channel keeps its centre and its peaks scale with its height; no antialiasing, no
    blending. The app would recompute each column's peaks at the new height (render_waveform), so a run's end can
    differ by a row (README, the tablet geometry's residuals)."""
    ob, nb = SCENE['waveform_map']['old_band'], SCENE['waveform_map']['new_band']
    y0 = wave['y0']; out = {}
    import numpy as np
    for cls in ('ink', 'outline'):
        cols = []
        for runs in wave[cls]:
            src = np.zeros(H, bool)
            for i in range(0, len(runs), 2): src[y0 + runs[i]:y0 + runs[i] + runs[i + 1]] = True
            dst = np.zeros(H, bool)
            for (o0, o1), (n0, n1) in (((ob[0], ob[1]), (nb[0], nb[1])), ((ob[1], ob[2]), (nb[1], nb[2]))):
                r = np.arange(n1 - n0)
                dst[n0:n1] = src[o0 + (r * (o1 - o0)) // (n1 - n0)]
            e = np.flatnonzero(np.diff(np.concatenate(([0], dst.astype(np.int8), [0]))))
            cols.append([int(v) for a, b in zip(e[0::2], e[1::2]) for v in (a, b - a)])
        out[cls] = cols
    return out


def print_table():
    bad = 0
    print(f'THE TABLET GEOMETRY at gui_scale {PERCENT} (2304 x 1440), derived from src/gui as it stands')
    print(f'{"region":12s} {"element":72s} {"Windows px":>14s} {"device px":>12s} {"app record":>11s}  owner')
    for region, element, wpx, dev, owner, rec in TABLE:
        ok = rec is None or str(rec) == str(dev)
        bad += not ok
        print(f'{region:12s} {element:72s} {str(wpx):>14s} {str(dev):>12s} {"" if rec is None else str(rec):>11s}'
              f'{"  " if ok else " !"} {owner}')
    S = SCENE; L = S['lanes']
    print('\nrows (end-exclusive): ' + ', '.join(f'{k} {v[0]}..{v[1]}' for k, v in L.items()) +
          f'; canvas {S["canvas"][0]}..{S["canvas"][1]}; bottom content {S["bottom_content"][0]}..{S["bottom_content"][1]}')
    print(f'ruler baseline {S["ruler"]["baseline"]}, majors from {S["ruler"]["major_top"]}, minors from {S["ruler"]["minor_top"]} '
          f'to {S["ruler"]["tick_bottom"]}, step {S["ruler"]["step_ms"]} ms, {len(S["ruler"]["ticks"])} ticks; head rows '
          f'{S["playhead"]["head_top"]}..{S["playhead"]["head_top"] + S["playhead"]["head_rows"]}, halves {S["playhead"]["head_half"]}')
    print(f'flags: box rows {S["flags"]["y0"]}..{S["flags"]["y1"]}, baseline {S["flags"]["baseline"]}; ' +
          ', '.join(f'x {f["x"]} fill {f["w"]}' for f in S['flags']['flags']))
    print(f'trim: {S["trim"]["y0"]}..{S["trim"]["y1"]}, buttons {S["trim"]["handles"]}; menu baseline {S["menu"]["baseline"]}; '
          f'clock baseline {S["clock"]["baseline"]}; bottom groups at {S["bottom_groups"]}')
    print(f'icon row boxes x: ' + ' '.join(str(b['x']) for b in S['buttons'] if b['row'] == 'icon') + f' (y {S["buttons"][0]["y"]})')
    print(f'bottom row boxes x: ' + ' '.join(str(b['x']) for b in S['buttons'] if b['row'] == 'bottom') +
          f' (y {[b for b in S["buttons"] if b["row"] == "bottom"][0]["y"]})')
    print('\nconstants read: ' + ', '.join(f'{k} {v} ({OWNER[k]})' for k, v in K.items()
                                           if not isinstance(v, list) or k.startswith('k') and 'Group' not in k
                                           and 'Buttons' not in k))
    print(f'\n{len(TABLE)} rows, {bad} disagreeing with the app\'s record')
    return bad


if __name__ == '__main__':
    sys.exit(1 if print_table() else 0)
