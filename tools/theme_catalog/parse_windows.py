#!/usr/bin/env python3
# tools/theme_catalog/parse_windows.py — the Windows scheme formats: ReactOS's hivedef.inf "New Schemes", a Windows
# setup hive's own default colours (its HKCU "Control Panel\Colors" lines) and its Appearance\Schemes values, and a
# .theme file's [Control Panel\Colors]. All come out under Windows' own key names (the .theme spelling, which is
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


# A Windows NT setup hive's Appearance\Schemes value (REG_BINARY, flags 0x00030001): the SCHEMEDATA the Display
# Properties dialog stores — SHORT version, WORD pad, NONCLIENTMETRICSW (500 bytes), LOGFONTW lfIconTitle (92 bytes),
# then COLORREF rgb[29] (COLOR_SCROLLBAR .. COLOR_GRADIENTINACTIVECAPTION, 0x00BBGGRR): 4 + 500 + 92 + 116 = 712 bytes.
SCHEME_RGB_OFFSET = 4 + 500 + 92
SCHEME_COLOURS = 29
SCHEME_BYTES = SCHEME_RGB_OFFSET + 4 * SCHEME_COLOURS


def parse_hive_schemes(path):
    """-> {scheme name: {key: rgb}}: a Windows setup hive's (HIVEDEF.INF) `HKCU,"Control Panel\\Appearance\\Schemes",
    "%NAME%",0x00030001,<hex bytes>` values (continued with trailing backslashes), each named by its [Strings] entry,
    every blob SCHEME_BYTES long, its colour array read under COLOR_NAMES' first 29 keys. Read as latin-1."""
    txt = open(path, encoding='latin-1').read()
    strings = dict(re.findall(r'^(\w+)="([^"]*)"\s*$', txt, re.M))
    lines = txt.splitlines()
    out = {}
    i = 0
    while i < len(lines):
        m = re.match(r'^HKCU,"Control Panel\\Appearance\\Schemes","%(\w+)%",0x00030001,(.*)$', lines[i])
        i += 1
        if not m: continue
        body = m.group(2)
        while body.rstrip().endswith('\\'):
            if i >= len(lines): die(path, f'scheme {m.group(1)} runs past the end of the file')
            body = body.rstrip()[:-1] + lines[i]; i += 1
        if not re.fullmatch(r'\s*[0-9a-fA-F]{2}(\s*,\s*[0-9a-fA-F]{2})*\s*', body):
            die(path, f'scheme {m.group(1)}: unreadable bytes')
        bs = bytes(int(x, 16) for x in body.split(','))
        if len(bs) != SCHEME_BYTES: die(path, f'scheme {m.group(1)} is {len(bs)} bytes, not {SCHEME_BYTES}')
        if m.group(1) not in strings: die(path, f'scheme {m.group(1)} has no [Strings] name')
        name = strings[m.group(1)]
        if name in out: die(path, f'scheme {name!r} recorded twice')
        o = SCHEME_RGB_OFFSET
        out[name] = {COLOR_NAMES[j]: tuple(bs[o + 4 * j:o + 4 * j + 3]) for j in range(SCHEME_COLOURS)}
    if not out: die(path, 'no Appearance\\Schemes values')
    return out


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
