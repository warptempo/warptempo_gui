#!/usr/bin/env python3
# tools/theme_catalog/parse_windows.py — the Windows scheme formats: ReactOS's hivedef.inf "New Schemes", a Windows
# setup hive's own default colours (its HKCU "Control Panel\Colors" lines) and a .theme file's [Control Panel\Colors]. Both come out under Windows' own key names (the .theme spelling, which is
# also the name the registry's Control Panel\Colors uses), each value an (r, g, b) byte triple. A malformed file is a
# one-line hard fail naming it (NO BACKSTOPS: the inputs are pinned third-party files).
import re

# The COLOR_* indices (winuser.h: COLOR_SCROLLBAR 0 .. COLOR_MENUBAR 30) under the .theme key names. hivedef.inf's
# "Color #n" is COLOR_* index n.
COLOR_NAMES = ['Scrollbar', 'Background', 'ActiveTitle', 'InactiveTitle', 'Menu', 'Window', 'WindowFrame', 'MenuText',
               'WindowText', 'TitleText', 'ActiveBorder', 'InactiveBorder', 'AppWorkspace', 'Hilight', 'HilightText',
               'ButtonFace', 'ButtonShadow', 'GrayText', 'ButtonText', 'InactiveTitleText', 'ButtonHilight',
               'ButtonDkShadow', 'ButtonLight', 'InfoText', 'InfoWindow', 'ButtonAlternateFace', 'HotTrackingColor',
               'GradientActiveTitle', 'GradientInactiveTitle', 'MenuHilight', 'MenuBar']


def die(path, msg):
    raise SystemExit(f'{path}: {msg}')


def parse_hivedef(path):
    """-> {scheme name: {key: rgb}}: every "New Schemes\\<n>\\Sizes\\0" colour (0x00BBGGRR) of scheme n, named by the
    FIRST [Strings] block's DESKTOP_SCHEME_<n> (the English names; the later blocks are translations)."""
    txt = open(path, encoding='utf-8', errors='strict').read()
    m = re.search(r'^\[Strings\]\s*$(.*?)^\[', txt, re.M | re.S)
    if not m: die(path, 'no [Strings] block')
    names = dict(re.findall(r'^DESKTOP_SCHEME_(\d+)="([^"]+)"', m.group(1), re.M))
    out = {}
    for n, i, v in re.findall(r'New Schemes\\(\d+)\\Sizes\\0","Color #(\d+)",0x00010001,0x([0-9a-fA-F]{8})', txt):
        if n not in names: die(path, f'scheme {n} has colours but no DESKTOP_SCHEME_{n} name')
        v = int(v, 16)
        out.setdefault(names[n], {})[COLOR_NAMES[int(i)]] = (v & 255, (v >> 8) & 255, (v >> 16) & 255)
    if not out: die(path, 'no New Schemes colours')
    for name, cols in out.items():
        if len(cols) != len(COLOR_NAMES): die(path, f'scheme {name!r} records {len(cols)} of {len(COLOR_NAMES)} colours')
    return out


def parse_hive_colors(path):
    """-> {key: rgb}: a Windows setup hive's (HIVEDEF.INF) default colours, its `HKCU,"Control Panel\\Colors","Key",
    <type>,"R G B"` lines, every key as written (Windows 2000 spells one "AppWorkSpace"). Read as latin-1."""
    txt = open(path, encoding='latin-1').read()
    cols = {}
    for k, r, g, b in re.findall(r'^HKCU,"Control Panel\\Colors","(\w+)",0x[0-9a-fA-F]{8},"(\d+) (\d+) (\d+)"\s*$',
                                 txt, re.M):
        if k in cols: die(path, f'colour {k!r} recorded twice')
        rgb = (int(r), int(g), int(b))
        if max(rgb) > 255: die(path, f'colour {k!r} out of range')
        cols[k] = rgb
    if not cols: die(path, 'no Control Panel\\Colors lines')
    return cols


def parse_theme(path):
    """-> (display name or None, {key: rgb}) from a .theme file: [Theme] DisplayName= and [Control Panel\\Colors]
    "Key=R G B" lines, every key as written. Read as latin-1 (the files carry a stray (c) byte)."""
    txt = open(path, encoding='latin-1').read()
    sec = re.search(r'^\[Control Panel\\Colors\]\s*$(.*?)(?=^\[|\Z)', txt, re.M | re.S)
    if not sec: die(path, 'no [Control Panel\\Colors] section')
    cols = {}
    for line in sec.group(1).splitlines():
        line = line.strip()
        if not line or line.startswith(';'): continue
        m = re.fullmatch(r'(\w+)\s*=\s*(\d+)\s+(\d+)\s+(\d+)', line)
        if not m: die(path, f'unreadable colour line {line!r}')
        rgb = tuple(int(x) for x in m.groups()[1:])
        if max(rgb) > 255: die(path, f'colour out of range {line!r}')
        cols[m.group(1)] = rgb
    dn = re.search(r'^DisplayName=(.*)$', txt, re.M)
    return (dn.group(1).strip() if dn else None), cols
