#!/usr/bin/env python3
# tools/theme_catalog/parse_cde.py — a CDE dtstyle palette (.dp): the eight colour sets' BACKGROUNDS, one X colour
# per line in set order (set 1 first), each "#rrrrggggbbbb" (16-bit channels); dtsession reads the lines up to a '!'
# (SrvFile_io.c ParsePaletteInfo). The four monochrome palettes (Black, White, BlackWhite, WhiteBlack) are X colour
# NAMES and dtsession refuses them on a colour display (ParsePaletteInfo returns -1 for B_ONLY, W_ONLY, B_O_W, W_O_B,
# Srv.h): parse_dp reports them as None, never invented. Anything else malformed is a one-line hard fail.
import re

MONOCHROME = {'Black', 'White', 'BlackWhite', 'WhiteBlack'}     # Srv.h B_ONLY, W_ONLY, B_O_W, W_O_B


def parse_dp(path, stem):
    """-> (the eight lines verbatim, the eight 16-bit triples), or None for a monochrome palette."""
    txt = open(path, encoding='ascii').read()
    lines = []
    for l in txt.split('\n'):
        if l.startswith('!'): break
        if l.strip(): lines.append(l.strip())
    if stem in MONOCHROME:
        if not all(l in ('Black', 'White') for l in lines): raise SystemExit(f'{path}: a monochrome palette with {lines!r}')
        return None
    if len(lines) != 8: raise SystemExit(f'{path}: {len(lines)} colour sets, not 8')
    sets = []
    for l in lines:
        if not re.fullmatch(r'#[0-9a-fA-F]{12}', l): raise SystemExit(f'{path}: {l!r} is not #rrrrggggbbbb')
        sets.append(tuple(int(l[i:i + 4], 16) for i in (1, 5, 9)))
    return lines, sets
