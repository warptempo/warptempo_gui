#!/usr/bin/env python3
# tools/theme_catalog/parse_windows.py — the Windows scheme formats: ReactOS's hivedef.inf "New Schemes", a Windows
# setup hive's own default colours (its HKCU "Control Panel\Colors" lines) and its Appearance\Schemes values, Windows
# 95's shell2.inf Appearance\Schemes values, and a .theme file's [Control Panel\Colors]. All come out under Windows' own key names (the .theme spelling, which is
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


# A Windows 95 shell2.inf Appearance\Schemes value (REG_BINARY, flags 1): the 16-bit SCHEMEDATA the Windows 95 Display
# Properties dialog stores, 492 bytes — a 6-byte header (version and size words), the ANSI NONCLIENTMETRICS-era LOGFONTs
# (MS Sans Serif, Arial), then, in the LAST 100 bytes, COLORREF rgb[25] (COLOR_SCROLLBAR 0 .. COLOR_INFOBK 24), each
# stored R, G, B and a palette-flag byte (02, or 00 on a few; ignored here). Windows 95 has no COLOR_HOTLIGHT,
# COLOR_GRADIENT*, COLOR_MENUHILIGHT or COLOR_MENUBAR.
W95_SCHEME_BYTES = 492
W95_SCHEME_COLOURS = 25
W95_SCHEME_RGB_OFFSET = W95_SCHEME_BYTES - 4 * W95_SCHEME_COLOURS


def parse_win95_schemes(path):
    r"""-> {scheme name: ({key: rgb}, recorded)}: the `HKCU,"Control Panel\Appearance\Schemes",%NAME%,1,<hex bytes>`
    values of a Windows 95 shell2.inf [schemes.reg] (continued with trailing backslashes), each named by its [Strings]
    entry, every blob W95_SCHEME_BYTES long, its colour array read under COLOR_NAMES' first 25 keys; `recorded` is
    the colour array's 100 bytes as the INF spells them (lowercase hex, comma-separated, the line wraps removed).
    The [shlold.reg] names (deleted old schemes, no bytes) are not values. Read as latin-1."""
    txt = open(path, encoding='latin-1').read()
    strings = dict(re.findall(r'^(\w+)\s*=\s*"([^"]*)"\s*$', txt, re.M))
    lines = txt.splitlines()
    out = {}
    i = 0
    while i < len(lines):
        m = re.match(r'^HKCU,"Control Panel\\Appearance\\Schemes",%(\w+)%,1,(.*)$', lines[i])
        i += 1
        if not m: continue
        body = m.group(2)
        while body.rstrip().endswith('\\'):
            if i >= len(lines): die(path, f'scheme {m.group(1)} runs past the end of the file')
            body = body.rstrip()[:-1] + lines[i].strip(); i += 1
        if not re.fullmatch(r'\s*[0-9a-fA-F]{2}(\s*,\s*[0-9a-fA-F]{2})*\s*', body):
            die(path, f'scheme {m.group(1)}: unreadable bytes')
        hexes = [x.strip().lower() for x in body.split(',')]
        bs = bytes(int(x, 16) for x in hexes)
        if len(bs) != W95_SCHEME_BYTES: die(path, f'scheme {m.group(1)} is {len(bs)} bytes, not {W95_SCHEME_BYTES}')
        if m.group(1) not in strings: die(path, f'scheme {m.group(1)} has no [Strings] name')
        name = strings[m.group(1)]
        if name in out: die(path, f'scheme {name!r} recorded twice')
        o = W95_SCHEME_RGB_OFFSET
        out[name] = ({COLOR_NAMES[j]: tuple(bs[o + 4 * j:o + 4 * j + 3]) for j in range(W95_SCHEME_COLOURS)},
                     ','.join(hexes[o:]))
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


# THE SCHEMES' MENU FONTS (2026-10-09, read for the catalog's font record — build.py's font_record, a record alone
# since ~21:20, when the scheme's face retired for the app's `font` device key): each source's NONCLIENTMETRICS records
# five LOGFONTs — the caption, the small caption, the MENU, the status bar and the message box — and the record is read
# off the MENU font, the face of the chrome's body text (the menus, the status line, the dialogs' words); the
# caption's face (Tahoma bold in Windows 2000's Brick, Times New Roman in its Rose) is the title's alone and not read. The face name is
# the LOGFONT's lfFaceName up to its first NUL (the bytes past it are the dialog's leftovers).
#   Windows 2000's hive (SCHEMEDATA, parse_hive_schemes): NONCLIENTMETRICSW after the 4-byte header — cbSize and five
#     ints (24 bytes), lfCaptionFont (LOGFONTW, 92: 28 bytes of fields, then WCHAR lfFaceName[32]), two ints,
#     lfSmCaptionFont, two ints, then lfMenuFont: its face at 4 + 24 + 92 + 8 + 92 + 8 + 28 = 256, UTF-16LE.
#   Windows 95's shell2.inf (parse_win95_schemes): the same order in LOGFONTA (60: 28 bytes, then CHAR[32]) after a
#     2-byte version and NONCLIENTMETRICS' cbSize and five ints — lfMenuFont's face at 2 + 24 + 60 + 8 + 60 + 8 + 28 =
#     190, ANSI (the CD's captions, menus, status and message faces sit at 54, 122, 190, 250 and 310).
#   A .theme file's [Metrics] NonclientMetrics= (NONCLIENTMETRICSA, its 340 bytes as decimal numbers): lfMenuFont's
#     face at 24 + 60 + 8 + 60 + 8 + 28 = 188, ANSI.
HIVE_MENU_FACE = 4 + 24 + 92 + 8 + 92 + 8 + 28
W95_MENU_FACE = 2 + 24 + 60 + 8 + 60 + 8 + 28
THEME_MENU_FACE = 24 + 60 + 8 + 60 + 8 + 28


def _face_a(bs, off):
    return bs[off:off + 32].split(b'\0')[0].decode('latin-1')


def _hex_values(path, pattern, flags_text):
    """The Appearance\\Schemes values of an INF (continued lines joined) -> {[Strings] name: bytes}."""
    txt = open(path, encoding='latin-1').read()
    strings = dict(re.findall(r'^(\w+)\s*=\s*"([^"]*)"\s*$', txt, re.M))
    lines = txt.splitlines()
    out = {}
    i = 0
    while i < len(lines):
        m = re.match(pattern, lines[i])
        i += 1
        if not m: continue
        body = m.group(2)
        while body.rstrip().endswith('\\'):
            if i >= len(lines): die(path, f'scheme {m.group(1)} runs past the end of the file')
            body = body.rstrip()[:-1] + lines[i].strip(); i += 1
        if m.group(1) not in strings: die(path, f'scheme {m.group(1)} has no [Strings] name')
        out[strings[m.group(1)]] = bytes(int(x.strip(), 16) for x in body.split(','))
    if not out: die(path, f'no {flags_text} Appearance\\Schemes values')
    return out


def parse_hive_scheme_fonts(path):
    r"""-> {scheme name: menu font face}: a Windows setup hive's Appearance\Schemes values (parse_hive_schemes' blobs),
    each value's lfMenuFont face (HIVE_MENU_FACE)."""
    out = {}
    for name, bs in _hex_values(path, r'^HKCU,"Control Panel\\Appearance\\Schemes","%(\w+)%",0x00030001,(.*)$',
                                'hive').items():
        if len(bs) != SCHEME_BYTES: die(path, f'scheme {name!r} is {len(bs)} bytes, not {SCHEME_BYTES}')
        face = bs[HIVE_MENU_FACE:HIVE_MENU_FACE + 64].decode('utf-16le').split('\0')[0]
        if not face: die(path, f'scheme {name!r} names no menu font')
        out[name] = face
    return out


def parse_win95_scheme_fonts(path):
    r"""-> {scheme name: menu font face}: a Windows 95 shell2.inf's Appearance\Schemes values (parse_win95_schemes'
    blobs), each value's lfMenuFont face (W95_MENU_FACE)."""
    out = {}
    for name, bs in _hex_values(path, r'^HKCU,"Control Panel\\Appearance\\Schemes",%(\w+)%,1,(.*)$',
                                'shell2.inf').items():
        if len(bs) != W95_SCHEME_BYTES: die(path, f'scheme {name!r} is {len(bs)} bytes, not {W95_SCHEME_BYTES}')
        face = _face_a(bs, W95_MENU_FACE)
        if not face: die(path, f'scheme {name!r} names no menu font')
        out[name] = face
    return out


def parse_theme_menu_font(path):
    """-> the menu font face of a .theme file's [Metrics] NonclientMetrics= (THEME_MENU_FACE), or None when the file
    carries no NonclientMetrics line (the XP saved schemes of xp_classic_zkedem record colours alone)."""
    txt = open(path, encoding='latin-1').read()
    m = re.search(r'^NonclientMetrics=([\d ]+?)\s*$', txt, re.M)
    if not m: return None
    bs = bytes(int(x) for x in m.group(1).split())
    if len(bs) < THEME_MENU_FACE + 32: die(path, f'NonclientMetrics is {len(bs)} bytes')
    face = _face_a(bs, THEME_MENU_FACE)
    if not face: die(path, 'NonclientMetrics names no menu font')
    return face


def read_png_rgb(path):
    """-> (width, height, rows): an 8-bit RGB, non-interlaced PNG's pixels as rows of (r, g, b) tuples, through zlib and
    the five PNG filters (the PNG specification's section 9), the standard library alone; any other PNG kind is a
    one-line hard fail (the inputs are pinned captures)."""
    import struct, zlib
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n': die(path, 'not a PNG')
    pos, idat, ihdr = 8, b'', None
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR': ihdr = struct.unpack('>IIBBBBB', body)
        elif kind == b'IDAT': idat += body
        pos += 12 + n
    if ihdr is None: die(path, 'no IHDR')
    w, h, depth, ctype, _, _, interlace = ihdr
    if (depth, ctype, interlace) != (8, 2, 0): die(path, f'not an 8-bit RGB non-interlaced PNG {ihdr}')
    raw, stride, bpp = zlib.decompress(idat), w * 3, 3
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif f != 0: die(path, f'row {y}: filter {f}')
        rows.append([tuple(line[i:i + 3]) for i in range(0, stride, 3)])
        prev = line
    return w, h, rows
