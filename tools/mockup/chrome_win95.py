#!/usr/bin/env python3
# tools/mockup/chrome_win95.py — THE WIN95 VOCABULARY: the capture's own stack, every role swapped for the target
# theme's, the geometry untouched (the app's own chrome under another theme, as the app would paint it — up to what
# the matcher cannot recover, matcher.py's head).
import numpy as np
from matcher import Matcher

NAME = 'win95'
EXTRAS = {}                     # the 36 roles carry this vocabulary whole


def recolour(img, scene, capture_theme, target_theme, rc):
    """-> (the recoloured capture, the Matcher (its masks and counts), {lane: [Case]}, stem columns or None)."""
    U = scene.U
    mt = Matcher(img, capture_theme, target_theme, U)
    H, W = mt.H, mt.W
    full = (slice(0, H), slice(0, W))
    mt.chrome(full)
    stem = None
    if scene.body == 'wave':
        R = scene.rows
        stem = mt.stem_columns(R['marker'], R['well'])
        head = np.zeros((H, W), bool)
        if stem:
            head[:, max(0, stem[0] - 6 * U):stem[1] + 6 * U] = True
        mt.chrome((slice(*R['ruler']), slice(0, W)), ink_cols=head)
        mt.marker_lane(R['marker'], stem, head)
        mt.well(R['well'], stem)
    else:
        mt.list_field(scene.rows['list'])
    mt.caption(scene.rows['caption'], rc)
    case_w = rc['kIconCaseLeadPx'] + rc['kIconGlyphPx'] + rc['kIconCaseTrailXPx']
    cases = {}
    for name, lane in scene.lanes.items():
        found = mt.find_cases(lane['rows'], case_w)
        if len(found) != len(lane['icons']):
            raise SystemExit(f'tools/mockup: {scene.path}: lane "{name}" lists {len(lane["icons"])} cases, the '
                             f'capture shows {len(found)} (at x {[c.x0 for c in found]})')
        for c in found:
            mt.glyph_cell(c)
        cases[name] = found
    for rows, cols in scene.fields:
        mt.field(rows, cols)
    for rows, cols, ground in scene.keeps:
        mt.keep_drawing(rows, cols, ground)
    return mt.rec, mt, cases, stem


def compose(img, scene, capture_theme, target_theme, extras, rc, args):
    rec, mt, cases, stem = recolour(img, scene, capture_theme, target_theme, rc)
    placed = {lane: [(c, c.x0, c.y0) for c in cs] for lane, cs in cases.items()}
    return rec, mt, placed
