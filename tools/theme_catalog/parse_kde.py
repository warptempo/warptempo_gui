#!/usr/bin/env python3
# tools/theme_catalog/parse_kde.py — a KDE 3 / TDE colour scheme (.kcsrc): one [Color Scheme] group of KConfig
# "key=value" lines. Colours are "r,g,b" or "#rrggbb"; an empty value is KConfig's "unset" (the reader takes its
# default) and is not recorded; contrast is the scheme's relief contrast (KDE's default 7 when absent). Localized
# names (Name[xx]=) and the non-colour flags (shadeSortColumn) are not colours and are not recorded. A malformed file
# is a one-line hard fail naming it.
import re

NON_COLOUR = {'Name', 'Comment', 'contrast', 'shadeSortColumn'}


def parse_kcsrc(path):
    """-> (name, {key: rgb}, contrast or None)."""
    txt = open(path, encoding='utf-8').read()
    if not re.search(r'^\[Color Scheme\]\s*$', txt, re.M): raise SystemExit(f'{path}: no [Color Scheme] group')
    name = None; contrast = None; cols = {}
    for line in txt.splitlines():
        line = line.strip()
        if not line or line.startswith('#') or line.startswith('['): continue
        m = re.fullmatch(r'([A-Za-z.]+)(\[[^\]]*\])?=(.*)', line)
        if not m: raise SystemExit(f'{path}: unreadable line {line!r}')
        k, loc, v = m.group(1), m.group(2), m.group(3).strip()
        if loc: continue
        if k == 'Name': name = v; continue
        if k == 'contrast':
            if not re.fullmatch(r'\d+', v): raise SystemExit(f'{path}: contrast {v!r} is not a number')
            contrast = int(v); continue
        if k in NON_COLOUR: continue
        if v == '': continue
        if re.fullmatch(r'\d+,\d+,\d+', v): rgb = tuple(int(x) for x in v.split(','))
        elif re.fullmatch(r'#[0-9a-fA-F]{6}', v): rgb = tuple(int(v[i:i + 2], 16) for i in (1, 3, 5))
        else: raise SystemExit(f'{path}: {k}={v!r} is not a colour')
        if max(rgb) > 255: raise SystemExit(f'{path}: {k}={v!r} out of range')
        cols[k] = rgb
    if not name: raise SystemExit(f'{path}: no Name=')
    return name, cols, contrast
