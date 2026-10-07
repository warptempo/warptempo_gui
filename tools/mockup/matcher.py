#!/usr/bin/env python3
# tools/mockup/matcher.py — THE MATCHER: a capture's pixels read back into ROLES by exact colour, region by region,
# and written out in another theme's roles.
#
# The app paints every chrome pixel as one of the 39 roles (theme_file.h) and every glyph in its drawing's own inks,
# with no antialias at a whole-multiple gui_scale on the faces the capture was painted in — but for the relief's
# MITRES (since 2026-10-06, paint_relief_frame, render.cpp: each raised or sunken edge's top-right and bottom-left
# corner blocks split along the diagonal, antialiased, so the pixels on the diagonal blend the two tones; the
# corner, below, under WHAT IT CANNOT RECOVER). So a pixel's role is the
# capture theme's role whose value it equals — where two roles share a value, the REGION decides (the scene's lanes),
# and inside a region a few adjacency rules decide what the value alone cannot:
#   - DkShadow and the label ink (both black in the built-in): a black pixel with Shadow one Windows px beside it in
#     any of the four directions is a relief line (every two-line edge pairs DkShadow with Shadow: a raised edge's
#     outer bottom-right, a sunken edge's outer or inner top-left), and so is a black one step along from such a
#     pixel (the corner cells); any other black is ink. The playhead's head is
#     the exception: its outline is the label ink beside its own Shadow, so the head's columns take ink.
#   - a flag's label (white in the built-in, Hilight's value): a label-coloured pixel whose nearest other pixels to
#     its left and right on its row are flag faces is the label; elsewhere it is Hilight or the stem.
#   - a flag's frame (DkShadow): black touching a flag face.
#   - a toolbar case's glyph cell: the background is the case's face (the checkerboard of Hilight cells over the
#     ground on a lit case, its lattice anchored at the case's corner as paint_button_box anchors it); what is not
#     background is the glyph — DISABLED when it is the emboss (Hilight and Shadow, or mixes of them and the
#     ground, in the retired emboss's offset structure — every capture before 2026-10-06's Tango set: those roles
#     are rewritten, a mix as the same mix), ENABLED otherwise (the drawing's own inks, kept as captured).
# THE DITHERS' CELL IS ONE DEVICE PX (since effd544f, render.cpp): the lit case's checker is paint_checker_rect's
# 2x2 pattern brush of device px, the caption gradient's matrix indexes device columns and rows — both forms are
# checked against render.cpp at import, so a painter change hard-fails here instead of mis-reading. A capture taken
# before effd544f painted both in Windows-px cells: mock.py's --legacy-dither reads it at that cell (dither_cell = U).
# WHAT IT CANNOT RECOVER: a glyph ink equal to the background (a silver pixel of an enabled glyph on the face, a
# white one on a lit cell) becomes the new background; a value two roles share inside one region goes to the region's
# owner; a MITRED CORNER's diagonal pixels (a capture taken since 2026-10-06), a blend of the two relief tones that
# equals no role, stay as captured in the capture theme's blend (README.md, The matcher and its limits) — the exact
# pixels either side of the diagonal still read by value and the DkShadow adjacency rule, and find_cases' reading row
# lies below the corner blocks. Pixels no rule claims are left as captured and counted.
import os, re
import numpy as np

# The caption gradient's ordered dither, read off render.cpp at import (paint_caption_gradient's own matrix), and
# the two painters' device-px form checked there.
_RENDER_CPP = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'gui', 'render.cpp')
_SRC = open(_RENDER_CPP).read()
_m = re.search(r'kCaptionDitherRank\[4\]\[4\] = \{(.*?)\};', _SRC, re.S)
if not _m:
    raise SystemExit(f'tools/mockup: {_RENDER_CPP}: no kCaptionDitherRank (the source moved)')
DITHER_RANK = np.array([int(v) for v in re.findall(r'\d+', _m.group(1))]).reshape(4, 4)
if sorted(DITHER_RANK.ravel()) != list(range(16)):
    raise SystemExit(f'tools/mockup: {_RENDER_CPP}: kCaptionDitherRank is not a 4x4 rank matrix')
# paint_checker_rect: a 2x2 tile, lit where (x + y) is even, repeated from the case's corner one device px a cell;
# paint_caption_gradient: the rank at [row & 3][column & 3] of device px, the ramp the column over the last.
for _what, _pat in (('the checker tile', r'cairo_image_surface_create\(CAIRO_FORMAT_ARGB32, 2, 2\)'),
                    ("the checker's lit cells", r'\(x \+ y\) % 2 == 0 \? lit_word'),
                    ("the gradient's device columns", r'kCaptionDitherRank\[p\]\[x & 3\]'),
                    ("the gradient's device rows", r'\(y & 3\) \* r\.w'),
                    ("the gradient's ramp", r'static_cast<double>\(x\) / \(r\.w - 1\)')):
    if not re.search(_pat, _SRC):
        raise SystemExit(f'tools/mockup: {_RENDER_CPP}: {_what} is not the device-px form matcher.py mirrors '
                         f'(the source moved)')

FACE_ROLES = ('warp_flag', 'phase_reset_flag', 'added_flag', 'removed_flag',
              'warp_flag_selected', 'phase_reset_flag_selected', 'added_flag_selected', 'removed_flag_selected')
# each selected face -> the label it carries (the selected label per kind, theme_file.h, 2026-10-07); every resting
# face carries the one `flag_label`
SELECTED_LABEL = {'warp_flag_selected': 'warp_label_selected',
                  'phase_reset_flag_selected': 'phase_reset_label_selected',
                  'added_flag_selected': 'added_label_selected',
                  'removed_flag_selected': 'removed_label_selected'}


def shift(m, dy, dx):
    """m moved by (dy, dx), the vacated edge False."""
    o = np.zeros_like(m)
    H, W = m.shape
    o[max(dy, 0):H + min(dy, 0), max(dx, 0):W + min(dx, 0)] = m[max(-dy, 0):H + min(-dy, 0), max(-dx, 0):W + min(-dx, 0)]
    return o


def dilate(m, r):
    o = m.copy()
    for dy in (-r, 0, r):
        for dx in (-r, 0, r):
            if dy or dx:
                o |= shift(m, dy, dx)
    return o


def caption_gradient(width, height, cell, start, end):
    """paint_caption_gradient's picture, (height, width, 3): start to end per channel across `cell`-px cells (1, the
    device px, since effd544f; U for a legacy capture), each channel quantised to five bits under the 4x4 ordered
    dither and bit-replicated; equal ends are one flat fill of the exact colour."""
    if tuple(start) == tuple(end):
        out = np.empty((height, width, 3), np.uint8); out[:] = start
        return out
    cols = 0
    while cols * cell < width:
        cols += 1
    k = np.minimum(np.arange(width) // cell, cols - 1)
    j = np.arange(height) // cell
    t = k / (cols - 1) if cols > 1 else np.zeros(width)
    rank = DITHER_RANK[(j & 3)[:, None], (k & 3)[None, :]]
    out = np.empty((height, width, 3), np.uint8)
    s0, e0 = np.asarray(start, np.float64), np.asarray(end, np.float64)
    for c in range(3):
        v = s0[c] + (e0[c] - s0[c]) * t
        level = v[None, :] * 31.0 / 255.0 + (rank + 0.5) / 16.0
        q = np.clip(np.floor(level), 0, 31).astype(np.int32)
        out[..., c] = (q << 3) | (q >> 2)
    return out


class Case:
    """A toolbar case found on a capture: its box in device px and the state read off it."""

    def __init__(self, x0, y0, x1, y1, checked, disabled):
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1
        self.checked, self.disabled = checked, disabled

    def __repr__(self):
        return f'Case({self.x0}..{self.x1}{" checked" if self.checked else ""}{" disabled" if self.disabled else ""})'


class Matcher:
    def __init__(self, img, capture_theme, target_theme, U, dither_cell=1):
        self.img = img
        self.H, self.W = img.shape[:2]
        self.CT = {k: np.array(v, np.uint8) for k, v in capture_theme.items()}
        self.TT = {k: np.array(v, np.uint8) for k, v in target_theme.items()}
        self.U = U
        self.dither_cell = dither_cell      # the checker's and the gradient's cell in device px (the head)
        self.rec = img.copy()
        self.claimed = np.zeros((self.H, self.W), bool)
        self._masks = {}

    # ---------------------------------------------------------------- masks
    def is_colour(self, c):
        key = tuple(int(v) for v in c)
        if key not in self._masks:
            self._masks[key] = np.all(self.img == np.array(key, np.uint8), axis=2)
        return self._masks[key]

    def m(self, role):
        return self.is_colour(self.CT[role])

    def beside(self, m):
        U = self.U
        return shift(m, 0, U) | shift(m, 0, -U) | shift(m, U, 0) | shift(m, -U, 0)

    def relief_dk(self):
        """The DkShadow-valued pixels that are relief lines: beside Shadow, or one step along from one that is (a
        raised edge's top-right and bottom-left corner cells, whose neighbours are Hilight and the DkShadow line)."""
        dkv = self.m('dk_shadow')
        dk = dkv & self.beside(self.m('shadow'))
        return dk | (dkv & self.beside(dk))

    # ---------------------------------------------------------------- writes
    def put(self, reg, mask, colour):
        """Write `colour` where `mask` holds inside region `reg` (a (row slice, col slice) pair)."""
        sel = np.zeros((self.H, self.W), bool)
        sel[reg] = mask[reg]
        self.rec[sel] = colour
        self.claimed |= sel

    def keep(self, reg, mask):
        sel = np.zeros((self.H, self.W), bool)
        sel[reg] = mask[reg]
        self.rec[sel] = self.img[sel]
        self.claimed |= sel

    # ---------------------------------------------------------------- region kinds
    def chrome(self, reg, ink_cols=None):
        """The chrome's roles: the ground, the relief quartet and the label ink (DkShadow by adjacency)."""
        for role in ('ground', 'light_3d', 'hilight', 'shadow'):
            self.put(reg, self.m(role), self.TT[role])
        dkv, lab = self.m('dk_shadow'), self.m('label')
        if np.array_equal(self.CT['dk_shadow'], self.CT['label']):
            dk = self.relief_dk()
            if ink_cols is not None:
                dk &= ~ink_cols
            self.put(reg, lab & ~dk, self.TT['label'])
            self.put(reg, dk, self.TT['dk_shadow'])
        else:
            self.put(reg, lab, self.TT['label'])
            self.put(reg, dkv, self.TT['dk_shadow'])

    def caption(self, rows, rc):
        """The caption lane: the active caption's start colour becomes the target's gradient (or flat fill), its
        text the target's caption text, the three button boxes chrome, the program icon its own inks."""
        U, (y0, y1) = self.U, rows
        reg = (slice(y0, y1), slice(0, self.W))
        grad = np.zeros((self.H, self.W, 3), np.uint8)
        grad[y0:y1] = caption_gradient(self.W, y1 - y0, self.dither_cell, self.TT['caption_active'],
                                       self.TT['caption_active_gradient'])
        cap = self.m('caption_active')
        sel = np.zeros((self.H, self.W), bool); sel[reg] = cap[reg]
        self.rec[sel] = grad[sel]; self.claimed |= sel
        self.put(reg, self.m('caption_active_text'), self.TT['caption_active_text'])
        # the program icon (paint_caption_row: kCaptionIcon{X,Y}Px, kCaptionIconPx): its drawing's own inks
        ix, iy, isz = rc['kCaptionIconXPx'] * U, y0 + rc['kCaptionIconYPx'] * U, rc['kCaptionIconPx'] * U
        ireg = (slice(iy, iy + isz), slice(ix, ix + isz))
        self.keep(ireg, ~cap)
        # the three buttons (caption_button_rects): Close at the right inset, the gap, Maximize, Minimize
        bw, bh, ins, gap = (rc[k] * U for k in ('kCaptionButtonWPx', 'kCaptionButtonHPx', 'kCaptionButtonInsetPx',
                                                 'kCaptionCloseGapPx'))
        cx = self.W - ins - bw; mx = cx - gap - bw
        for x in (mx - bw, mx, cx):
            self.chrome((slice(y0 + ins, y0 + ins + bh), slice(x, x + bw)))

    def find_cases(self, rows, case_w):
        """Every toolbar case across lane rows [y0, y1): `case_w` Windows px wide (the 23-px case), its left edge a
        two-line opening pair and its right edge the closing pair of the same relief family, read cell by cell
        (one Windows px a cell) on the row 2.5 Windows px below the lane's top — inside the two edge lines, above
        the glyph's seat. The four families (render.cpp's paint_relief_*): soft raised opens Hilight, 3DLight and
        closes Shadow, DkShadow; soft sunken DkShadow, Shadow / 3DLight, Hilight; plain raised 3DLight, Hilight /
        Shadow, DkShadow; plain sunken Shadow, DkShadow / 3DLight, Hilight. Touching cases are told apart by the
        fixed width, not by a run of colour."""
        U, (y0, y1) = self.U, rows
        row = self.img[y0 + (5 * U) // 2]
        c = {k: tuple(int(v) for v in self.CT[k]) for k in ('hilight', 'light_3d', 'shadow', 'dk_shadow')}
        H_, L3, SH, DK = c['hilight'], c['light_3d'], c['shadow'], c['dk_shadow']
        family = {(H_, L3): (SH, DK), (DK, SH): (L3, H_), (L3, H_): (SH, DK), (SH, DK): (L3, H_)}
        cells = [tuple(int(v) for v in row[k * U]) for k in range(self.W // U)]
        cases, k, n = [], 0, len(cells)
        while k + case_w <= n:
            close = family.get((cells[k], cells[k + 1]))
            if close and (cells[k + case_w - 2], cells[k + case_w - 1]) == close:
                x0 = k * U
                checked = cells[k] in (DK, SH)
                cases.append(Case(x0, y0, x0 + case_w * U, y1, checked, False))
                k += case_w
            else:
                k += 1
        return cases

    def case_background(self, case, theme):
        """The case's face inside its edges in `theme` ('CT' or 'TT'): (rows, cols, 3) and the region."""
        U, T = self.U, getattr(self, theme)
        ix0, iy0, ix1, iy1 = case.x0 + 2 * U, case.y0 + 2 * U, case.x1 - 2 * U, case.y1 - 2 * U
        bg = np.empty((iy1 - iy0, ix1 - ix0, 3), np.uint8); bg[:] = T['ground']
        if case.checked:                                  # paint_checker_rect anchored at the case's corner
            yy = (np.arange(iy0, iy1) - case.y0) // self.dither_cell
            xx = (np.arange(ix0, ix1) - case.x0) // self.dither_cell
            lit = ((yy[:, None] + xx[None, :]) % 2) == 0
            bg[lit] = T['hilight']
        return bg, (slice(iy0, iy1), slice(ix0, ix1))

    def _emboss_mix(self, cur):
        """Each pixel as a mix of two of the emboss's three colours (the ground, Hilight, Shadow — a lit cell's
        Hilight is the first of them too): (ok, A index, B index, t) with pixel = t * A + (1 - t) * B, within two
        steps a channel — a scalable glyph's antialiased emboss edge is such a mix."""
        P = cur.astype(np.float64)
        cols = [self.CT['ground'], self.CT['hilight'], self.CT['shadow']]
        best = np.full(P.shape[:2], np.inf); bi = np.zeros(P.shape[:2], int); bj = np.zeros(P.shape[:2], int)
        bt = np.zeros(P.shape[:2])
        for i, j in ((0, 1), (0, 2), (1, 2)):
            A, B = cols[i].astype(np.float64), cols[j].astype(np.float64)
            d = A - B
            t = np.clip(((P - B) @ d) / max(float(d @ d), 1e-9), 0, 1)
            res = np.abs(P - (B + t[..., None] * d)).max(axis=2)
            better = res < best
            best[better], bi[better], bj[better], bt[better] = res[better], i, j, t[better]
        return best <= 2.0, bi, bj, bt

    def glyph_cell(self, case):
        """Recolour a case's interior: the face the target's, the glyph's emboss the target's, an enabled glyph's
        own inks kept. Sets case.disabled: the glyph is the EMBOSS when every pixel of it is the ground, Hilight,
        Shadow or a mix of two of them AND it has the emboss's structure (the retired draw_engraved, every capture
        before the Tango set: the mask in Shadow at the glyph's place over its copy in Hilight one line right and
        down, so a Hilight pixel has Shadow one line up and left, and a Shadow pixel has Shadow or Hilight one line
        down and right — nine in ten of each, for the antialiased edges). An enabled glyph made of the same two greys alone fails the structure."""
        U = self.U
        bg_ct, reg = self.case_background(case, 'CT')
        bg_tt, _ = self.case_background(case, 'TT')
        cur = self.img[reg]
        is_bg = np.all(cur == bg_ct, axis=2)
        glyph = ~is_bg
        hl_any = np.all(cur == self.CT['hilight'], axis=2)     # a lit cell's Hilight included
        hl = hl_any & glyph
        sh = np.all(cur == self.CT['shadow'], axis=2)
        ok, bi, bj, bt = self._emboss_mix(cur)
        disabled = False
        if glyph.any() and ok[glyph].all() and sh.any():
            up_left_sh = np.zeros_like(sh); up_left_sh[U:, U:] = sh[:-U, :-U]
            down_right = np.zeros_like(sh); down_right[:-U, :-U] = (sh | hl_any)[U:, U:]
            hl_fit = up_left_sh[hl].mean() if hl.any() else 1.0
            sh_fit = down_right[sh].mean()
            disabled = hl_fit >= 0.9 and sh_fit >= 0.9
        case.disabled = bool(disabled)
        out = cur.copy()
        out[is_bg] = bg_tt[is_bg]
        if case.disabled:
            tcols = np.stack([self.TT['ground'], self.TT['hilight'], self.TT['shadow']]).astype(np.float64)
            g = glyph
            mix = bt[g][:, None] * tcols[bi[g]] + (1 - bt[g][:, None]) * tcols[bj[g]]
            out[g] = np.clip(np.floor(mix + 0.5), 0, 255).astype(np.uint8)
        self.rec[reg] = out
        self.claimed[reg] = True

    def field(self, rows, cols):
        """A time field: the clock pair inside the status panel's one-line sunken frame (Shadow, Hilight)."""
        reg = (slice(*rows), slice(*cols))
        for role in ('clock_ground', 'shadow', 'hilight', 'clock_text'):
            self.put(reg, self.m(role), self.TT[role])

    def stem_columns(self, marker_rows, well_rows):
        """The playhead's stem: the columns of the stem's colour from the marker lane through the well."""
        U = self.U
        col = self.m('playhead_stem')[marker_rows[0] + 2 * U:well_rows[1] - 2 * U].all(axis=0)
        xs = np.flatnonzero(col)
        if len(xs) == 0:
            return None
        return int(xs.min()), int(xs.max()) + 1

    def faces(self):
        """{role: mask} of the flag faces, the later role winning where two share a value."""
        return {r: self.m(r) for r in FACE_ROLES}

    def marker_lane(self, rows, stem, head_cols):
        """The marker lane: chrome, then the flags (faces, labels, frames), then the playhead's stem."""
        U, (y0, y1) = self.U, rows
        reg = (slice(y0, y1), slice(0, self.W))
        self.chrome(reg, ink_cols=head_cols)
        faces = self.faces()
        any_face = np.zeros((self.H, self.W), bool)
        for r, fm in faces.items():
            self.put(reg, fm, self.TT[r])
            any_face |= fm
        # the labels: a label-coloured run bounded on its row by faces on both sides, each selected face's run in its
        # own kind's selected label
        labc = self.m('flag_label')
        for lr in SELECTED_LABEL.values():
            labc = labc | self.m(lr)
        lab = np.zeros((self.H, self.W), bool)
        on_sel = {r: np.zeros((self.H, self.W), bool) for r in SELECTED_LABEL}
        idx = np.arange(self.W)
        for y in range(y0, y1):
            nl = ~labc[y]
            left = np.maximum.accumulate(np.where(nl, idx, -1))
            right = np.minimum.accumulate(np.where(nl, idx, self.W)[::-1])[::-1]
            ok = labc[y] & (left >= 0) & (right < self.W)
            li, ri = np.clip(left, 0, self.W - 1), np.clip(right, 0, self.W - 1)
            ok &= any_face[y, li] & any_face[y, ri]
            lab[y] = ok
            for r in SELECTED_LABEL:
                on_sel[r][y] = ok & faces[r][y, li]
        any_sel = np.zeros((self.H, self.W), bool)
        for r, lr in SELECTED_LABEL.items():
            any_sel |= on_sel[r]
            self.put(reg, lab & on_sel[r], self.TT[lr])
        self.put(reg, lab & ~any_sel, self.TT['flag_label'])
        # the frames: DkShadow touching a face
        self.put(reg, self.m('dk_shadow') & dilate(any_face, U), self.TT['dk_shadow'])
        if stem:
            sc = np.zeros((self.H, self.W), bool); sc[:, stem[0]:stem[1]] = True
            self.put(reg, self.m('playhead_stem') & sc & ~lab, self.TT['playhead_stem'])

    def well(self, rows, stem):
        """The waveform's well: its two plain-sunken edge lines top and bottom (full width, no verticals), the
        canvas, the ink and the lit outline, the stems (the flags' faces, the playhead's), the scanner."""
        U, (y0, y1) = self.U, rows
        reg = (slice(y0, y1), slice(0, self.W))
        for r in FACE_ROLES:
            self.put(reg, self.m(r), self.TT[r])
        for r in ('scanner', 'waveform_outline', 'waveform_ink', 'waveform_canvas', 'playhead_stem'):
            self.put(reg, self.m(r), self.TT[r])
        top1, top2 = (slice(y0, y0 + U), slice(0, self.W)), (slice(y0 + U, y0 + 2 * U), slice(0, self.W))
        bot1, bot2 = (slice(y1 - 2 * U, y1 - U), slice(0, self.W)), (slice(y1 - U, y1), slice(0, self.W))
        # the canvas never reaches the top two lines (taken from the area, render.h): a DkShadow-valued pixel there
        # is the line itself or a flag's outline carried down beside its stem (fill_stem_flanks)
        self.put(top1, self.m('shadow'), self.TT['shadow'])
        for t in (top1, top2):
            self.put(t, self.m('dk_shadow'), self.TT['dk_shadow'])
        self.put(bot1, self.m('light_3d'), self.TT['light_3d'])
        self.put(bot2, self.m('hilight'), self.TT['hilight'])

    def list_field(self, rows):
        """A list view (the render player's): a plain-sunken field, its rows in the field pair, the highlighted
        row's band in the selected pair."""
        U, (y0, y1) = self.U, rows
        self.chrome((slice(y0, y1), slice(0, self.W)))
        ireg = (slice(y0 + 2 * U, y1 - 2 * U), slice(2 * U, self.W - 2 * U))
        fill = self.m('selected_fill')
        band = np.zeros((self.H, self.W), bool)
        width = self.W - 4 * U
        rows_sel = fill[:, 2 * U:self.W - 2 * U].sum(axis=1) > width // 2
        band[rows_sel, :] = True
        self.put(ireg, self.m('field_ground') & ~band, self.TT['field_ground'])
        self.put(ireg, self.m('field_text') & ~band, self.TT['field_text'])
        self.put(ireg, fill & band, self.TT['selected_fill'])
        self.put(ireg, self.m('selected_text') & band & ~fill, self.TT['selected_text'])

    def keep_drawing(self, rows, cols, ground):
        """A drawing in its own inks (a list row's icon): kept, except its ground role's pixels."""
        reg = (slice(*rows), slice(*cols))
        g = self.m(ground)
        self.keep(reg, ~g)
        self.put(reg, g, self.TT[ground])

    def unclaimed(self):
        return int((~self.claimed).sum())
