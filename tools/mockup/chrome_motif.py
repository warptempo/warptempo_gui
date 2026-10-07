#!/usr/bin/env python3
# tools/mockup/chrome_motif.py — THE MOTIF VOCABULARY: CDE's stack (dtwm's frame and title bar, Motif's menu bar,
# dtmail's toolbar, an XmScrollBar for the trim lane, an XmScale slider for the playhead's head) re-laid over the
# capture recoloured by chrome_win95 (every role swapped first, then moved). Each length and its source are the
# CDE research report of 2026-10-06 (tmp/cde/report_B1.md §1, correcting tmp/theme_author/report_CD1.md's menu bar
# and scroll bar), in Windows px:
#   frame 5 + title 17 + menu 27 + toolbar 28 [+ separator 2] + scroll bar 13 + ruler 15/17 + marker 18 = 125
# — the frame dtwm's resizeBorderWidth 5; the title the font's 13-px line + WM_TITLE_BAR_PADDING 4; the menu bar
# Solaris's measured 29 screen px less the additive 2 of the 15 -> 13 cell transposition (Motif sizes are the font's
# height plus fixed margins); the toolbar dtmail's 3 air + 22 case + 3 air; the scroll bar Solaris's measured
# 1 + 11 + 1 — THE RULER IS THE FLEX LANE: 15 with the separator, 17 without, so the marker lane and the waveform's
# well stand where the capture has them (rows 125.. untouched) — and the bottom keeps 1 air + 22 case + 1 air + the
# frame's 5.
# The content stands 5 Windows px in from the left frame; what a real 10-px-narrower layout would reflow is cut by
# the right frame. A LIST BODY (the render player) is the capture's sunken list, its top and bottom edges kept and
# the interior rows and columns its new height and width lose cut from its bottom and right.
#
# THREE COLOURS THE 39 ROLES LACK (--extra): `trough` (the scroll bar's trough: Motif's select colour of the
# ground's set), `frame_ts` / `frame_bs` (the window frame's and title bar's top and bottom shadows: the active
# title set's). Any one not given is computed by Motif's own rule (lib/Xm/Color.c CalculateColorsRGB, as
# tools/theme_catalog/toolkit_rules.py runs it) on the role it belongs to — `ground` for the trough,
# `caption_active` for the frame — taken as a 16-bit X colour (each byte times 257); a palette's .dp file carries
# 16-bit values of its own, so its colours can sit one step off what this rule gives from the 8-bit role: pass them
# when they are known.
import os, sys
import numpy as np
from chrome_win95 import recolour, dither_cell

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'theme_catalog'))
import toolkit_rules                                       # noqa: E402  Motif's CalculateColorsRGB

NAME = 'motif'
EXTRAS = {'trough': "the scroll bar's trough", 'frame_ts': "the frame's top shadow",
          'frame_bs': "the frame's bottom shadow"}


def motif_rule(c8):
    m, _ = toolkit_rules.motif_colors(tuple(int(v) * 257 for v in c8))
    return {k: np.array(toolkit_rules.x_to_8bit(v), np.uint8) for k, v in m.items()}


def compose(img, scene, CT, TT_rgb, extras, rc, args):
    rec, mt, cases, stem = recolour(img, scene, CT, TT_rgb, rc, dither_cell(scene, args))
    T = mt.TT
    U = scene.U
    H, W = mt.H, mt.W
    SEP = args.separator
    derived = {'trough': ('ground', 'sel'), 'frame_ts': ('caption_active', 'ts'), 'frame_bs': ('caption_active', 'bs')}
    X = {}
    for k, (role, part) in derived.items():
        if k in extras:
            X[k] = np.array(extras[k], np.uint8)
        else:
            X[k] = motif_rule(T[role])[part]
            print(f'tools/mockup: motif {k} not given: Motif\'s rule on {role} gives #%02X%02X%02X' % tuple(X[k]),
                  file=sys.stderr)
    TROUGH, FRAME_TS, FRAME_BS = X['trough'], X['frame_ts'], X['frame_bs']

    out = np.zeros((H, W, 3), np.uint8)
    placed = {lane: [] for lane in cases}

    def fill(y0, y1, x0, x1, c):
        out[max(0, y0):y1, max(0, x0):x1] = c

    def bevel(y0, y1, x0, x1, t, tl, br):
        """Motif's shadow of thickness t round [x0,x1) x [y0,y1): top-left colour, bottom-right colour (br owns the
        top-right and bottom-left corners)."""
        fill(y0, y0 + t, x0, x1, tl); fill(y0, y1, x0, x0 + t, tl)
        fill(y1 - t, y1, x0, x1, br); fill(y0, y1, x1 - t, x1, br)

    def raised(y0, y1, x0, x1, t=2 * U): bevel(y0, y1, x0, x1, t, T['hilight'], T['shadow'])
    def sunken(y0, y1, x0, x1, t=2 * U): bevel(y0, y1, x0, x1, t, T['shadow'], T['hilight'])

    # ---- 1. THE FRAME: 5 W = external bevel 2 (ts / bs) + face 2 + join bevel 1 (bs inside top/left, ts inside
    # bottom/right), dtwm's resizeBorderWidth 5; corner pieces title bar + border = 17 + 5 = 22 W long, each ending
    # in a bs/ts junction line.
    F = 5 * U
    ACT = T['caption_active']
    fill(0, H, 0, W, ACT)
    bevel(0, H, 0, W, 2 * U, FRAME_TS, FRAME_BS)
    bevel(F - U, H - F + U, F - U, W - F + U, U, FRAME_BS, FRAME_TS)
    CL = 22 * U
    for x in (CL - U, W - CL):
        fill(U, F, x, x + U, FRAME_BS); fill(2 * U, F, x + U, x + 2 * U, FRAME_TS)
        fill(H - F, H - U, x, x + U, FRAME_BS); fill(H - F, H - 2 * U, x + U, x + 2 * U, FRAME_TS)
    for y in (CL - U, H - CL):
        fill(y, y + U, U, F, FRAME_BS); fill(y + U, y + 2 * U, 2 * U, F, FRAME_TS)
        fill(y, y + U, W - F, W - U, FRAME_BS); fill(y + U, y + 2 * U, W - F, W - 2 * U, FRAME_TS)
    X0, X1 = F, W - F
    DX = F                                         # the content stands inside the frame's left border

    def put(arr, y, x=DX):
        """Seat a recoloured strip at (x, y), cut at the right frame."""
        h, w = arr.shape[:2]; w2 = min(w, X1 - x)
        out[y:y + h, x:x + w2] = arr[:, :w2]

    # ---- 2. THE TITLE BAR: 17 W = the font's 13-px line + WM_TITLE_BAR_PADDING 4; four abutting boxes with 1-W
    # shadows: the window-menu button, the title, Minimize, Maximize (mwm has no Close). The gadgets' glyphs, each
    # centred in its box with 1-W shadows (dtwm draws them in code): the window-menu button's bar (TB - 8) x 4 = 9 x 4
    # raised, Minimize a 4 x 4 raised square, Maximize a (TB - 8) = 9 x 9 square drawn SUNKEN — the window is
    # maximised, and dtwm's painter swaps the Maximize glyph's bevel to sunken in that state (WmCDecor.c 951-961).
    TB = 17 * U; Y_T = F
    fill(Y_T, H - F, X0, X1, T['ground'])          # the client's ground under every lane
    def tbox(x0, x1): fill(Y_T, Y_T + TB, x0, x1, ACT); bevel(Y_T, Y_T + TB, x0, x1, U, FRAME_TS, FRAME_BS)
    tbox(X0, X0 + TB); tbox(X0 + TB, X1 - 2 * TB); tbox(X1 - 2 * TB, X1 - TB); tbox(X1 - TB, X1)
    def glyph(x0, y0, w, h, sunk=False):
        fill(y0, y0 + h, x0, x0 + w, ACT)
        bevel(y0, y0 + h, x0, x0 + w, U, *((FRAME_BS, FRAME_TS) if sunk else (FRAME_TS, FRAME_BS)))
    glyph(X0 + 4 * U, Y_T + (TB - 4 * U) // 2, TB - 8 * U, 4 * U)
    glyph(X1 - 2 * TB + (TB - 4 * U) // 2, Y_T + (TB - 4 * U) // 2, 4 * U, 4 * U)
    glyph(X1 - TB + 4 * U, Y_T + 4 * U, TB - 8 * U, TB - 8 * U, sunk=True)
    # the title text: the capture's own caption glyphs, centred in the title box as dtwm centres it
    cy0, cy1 = scene.rows['caption']
    tx_from = (rc['kCaptionIconXPx'] + rc['kCaptionIconPx']) * U
    tx_to = W - (rc['kCaptionButtonInsetPx'] + 3 * rc['kCaptionButtonWPx'] + rc['kCaptionCloseGapPx']) * U
    tm = mt.m('caption_active_text')[cy0:cy1, tx_from:tx_to]
    ys, xs = np.nonzero(tm)
    if len(xs):
        tx0, tx1 = xs.min(), xs.max() + 1
        box_x0, box_x1 = X0 + TB, X1 - 2 * TB
        dst_x = box_x0 + (box_x1 - box_x0 - (tx1 - tx0)) // 2
        dst_y = Y_T + U + (TB - 2 * U - (cy1 - cy0)) // 2
        region = out[dst_y:dst_y + (cy1 - cy0), dst_x:dst_x + (tx1 - tx0)]
        region[tm[:, tx0:tx1]] = T['caption_active_text']

    # ---- 3. THE MENU BAR: 27 W, a 1-W raised band across the client.
    MB = 27 * U; Y_M = Y_T + TB
    fill(Y_M, Y_M + MB, X0, X1, T['ground']); bevel(Y_M, Y_M + MB, X0, X1, U, T['hilight'], T['shadow'])
    m0, m1 = scene.rows['menu']
    put(rec[m0:m1], Y_M + U + (MB - 2 * U - (m1 - m0)) // 2)

    # ---- 4. THE TOOLBAR: 28 W = 3 air + 22 case + 3 air (dtmail's row of pixmap push buttons); with --separator,
    # XmSeparatorGadget SHADOW_ETCHED_IN under it (2 W: a Shadow line, a Hilight line).
    Y_I = Y_M + MB; IR = 28 * U + (2 * U if SEP else 0)
    c0, c1 = scene.lanes['icon_row']['rows']
    put(rec[c0:c1], Y_I + 3 * U)
    placed['icon_row'] = [(c, c.x0 + DX, Y_I + 3 * U) for c in cases['icon_row'] if c.x0 + DX < X1]
    if SEP:
        fill(Y_I + IR - 2 * U, Y_I + IR - U, X0, X1, T['shadow']); fill(Y_I + IR - U, Y_I + IR, X0, X1, T['hilight'])
    Y_S = Y_I + IR

    if scene.body == 'wave':
        R = scene.rows
        # ---- 5. THE SCROLL BAR (the trim lane, 13 W): a 1-W sunken border (Solaris draws shadowThickness 1 on
        # the scroll bar) round an 11-W trough in the select colour, the slider a 1-W raised box filling the trough's
        # height, the two handles Motif's shaded arrows (9 W, vertically centred: they fill the slider's 9-W
        # interior) at the slider's ends, 1 W in from its bevel.
        tr0 = R['trim'][0]
        row = img[tr0 + 7 * U]
        l3 = np.flatnonzero(np.all(row == mt.CT['light_3d'], axis=1))
        bk = np.flatnonzero(np.all(row == mt.CT['dk_shadow'], axis=1))
        THUMB = (int(l3[0]), int(bk[-1]) + 1)
        SB = 13 * U
        fill(Y_S, Y_S + SB, X0, X1, TROUGH); sunken(Y_S, Y_S + SB, X0, X1, U)
        sx0, sx1 = THUMB[0] + DX, min(THUMB[1] + DX, X1 - U)
        fill(Y_S + U, Y_S + SB - U, sx0, sx1, T['ground']); raised(Y_S + U, Y_S + SB - U, sx0, sx1, U)

        def arrow(xa, ya, size, direction):
            """XmArrowButton's picture: a triangle of the ground, a light edge on its upper side and a dark edge
            on its lower side and back; direction -1 left, +1 right."""
            for i in range(size):
                d = abs(i - size // 2); span = size - 2 * d
                if span <= 0: continue
                x_from = xa + (size - span if direction < 0 else 0); x_to = x_from + span
                out[ya + i, x_from:x_to] = T['ground']
                edge = T['hilight'] if i <= size // 2 else T['shadow']
                if direction < 0: out[ya + i, x_from:x_from + U] = edge
                else: out[ya + i, x_to - U:x_to] = edge
            back = T['shadow'] if direction < 0 else T['hilight']
            xb = xa + size - U if direction < 0 else xa
            out[ya:ya + size, xb:xb + U] = back
        AR = 9 * U; ay = Y_S + (SB - AR) // 2
        arrow(sx0 + 2 * U, ay, AR, -1); arrow(sx1 - 2 * U - AR, ay, AR, +1)

        # ---- 6. THE RULER: 15 W with the separator, 17 W without — the labels' cap band seated at the lane's
        # top, the ticks as captured; the head replaced by XmScale's slider with its ETCHED_LINE mark, 15 x 7 W, its
        # etched centre line at the time.
        Y_R = Y_S + SB; RL = (15 if SEP else 17) * U
        r0, r1 = R['ruler']
        ruler = rec[r0:r1].copy()
        if stem:
            head_x0, head_x1 = stem[0] - 6 * U, stem[1] + 6 * U
            ruler[(r1 - r0) - 8 * U:, head_x0:head_x1] = T['ground']
        put(ruler, Y_R + RL - (r1 - r0))
        if stem:
            cx = (stem[0] + stem[1]) // 2 + DX; SW, SH_ = 15 * U, 7 * U; sy = Y_R + RL - SH_
            fill(sy, sy + SH_, cx - SW // 2, cx + SW // 2, T['ground']); raised(sy, sy + SH_, cx - SW // 2, cx + SW // 2)
            fill(sy + 2 * U, sy + SH_ - 2 * U, cx - U, cx, T['shadow'])
            fill(sy + 2 * U, sy + SH_ - 2 * U, cx, cx + U, T['hilight'])

        # ---- 7. THE MARKER LANE and THE WELL, in place (the capture's own rows).
        Y_K = Y_R + RL
        k0, k1 = R['marker']
        if Y_K != k0:
            raise SystemExit(f'tools/mockup: motif: the stack ends at row {Y_K}, the marker lane begins at {k0}')
        mk = rec[k0:k1].copy()
        if stem:                                       # the old head's tip row
            tip = mt.m('label')[k0:k0 + U, head_x0:head_x1] | mt.m('dk_shadow')[k0:k0 + U, head_x0:head_x1]
            mk[:U, head_x0:head_x1][tip] = T['ground']
        put(mk, k0)
        w0, w1 = R['well']
        put(rec[w0:w1], w0)
        Y_B = w1
    else:
        # ---- 5'. A LIST BODY: the capture's sunken list from the stack's end to the bottom row, its top and its
        # bottom edges kept, the interior rows the new height loses cut above the bottom edge and the columns the
        # frames take cut left of the right edge.
        l0, l1 = scene.rows['list']
        Y_B = l1
        h_new = Y_B - Y_S
        w_new = X1 - DX
        edge = 2 * U
        lst = np.concatenate([rec[l0:l0 + h_new - edge], rec[l1 - edge:l1]], axis=0)
        lst = np.concatenate([lst[:, :w_new - edge], lst[:, W - edge:W]], axis=1)
        out[Y_S:Y_B, DX:X1] = lst

    # ---- 8. THE BOTTOM ROW: 1 air + 22 case + 1 air above the frame's 5.
    b0, b1 = scene.lanes['bottom_row']['rows']
    put(rec[b0:b1], Y_B + U)
    placed['bottom_row'] = [(c, c.x0 + DX, Y_B + U) for c in cases['bottom_row'] if c.x0 + DX < X1]
    if Y_B + U + (b1 - b0) + U + F != H:
        raise SystemExit('tools/mockup: motif: the bottom row does not close on the frame')
    return out, mt, placed
