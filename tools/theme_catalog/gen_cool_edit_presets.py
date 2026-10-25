#!/usr/bin/env python3
# tools/theme_catalog/gen_cool_edit_presets.py — tools/theme_catalog/cool_edit_presets.txt and warptempo_presets.txt ->
# THE BUILT-IN PALETTES' GENERATED INCLUDE, src/gui/palette_presets.inc (the rows of kGuiBuiltinPalettes,
# src/gui/palette_file.h, which owns what a palette is and does).
# THE BUILT-IN PALETTES ARE TWENTY-EIGHT, TWO SOURCES IN ONE INCLUDE, Cool Edit's rows first:
#   COOL EDIT'S COLOR PRESETS (architect 2026-10-09 ~13:30, "only Cool Edit Pro palettes for waveform needed now"):
#     EVERY scheme Cool Edit Pro 2.1 carries in coolpro.exe, twenty, in its own order. Its first is Cool Edit's
#     "(Default Scheme)", `cool-edit-default` (DEFAULT_KEY below, the transcription's check on the first row), THE
#     DEFAULT PALETTE (architect 2026-10-16, "make default the default theme"; palette_file.h's kGuiDefaultPaletteKey,
#     which this tool does not read; Midnight was the default 2026-10-12 to 2026-10-16);
#   THE PROGRAM'S OWN EIGHT (architect 2026-10-25, on the planner's freestyle mocks: "implement these as themes so I
#     can test it live ... I wouldn't be opposed to shipping multiple ones"), warptempo_presets.txt's, in its order
#     (that file's head is their design).
# Each row a KEY, a DISPLAY NAME and THE ELEVEN PROGRAM ROLES (kGuiPaletteRoles, read off palette_file.h and checked
# against ROLE_ORDER), both sources under the one transcription below.
#
# THE SOURCES (each text's own head says where it came from), in one value grammar (comma-separated key=value items):
# cool_edit_presets.txt's first line is "(Default Scheme)" and its values on the same line, and after it EVERY values
# line is followed by its name line (the two the exe's text lacks come from Cool Edit's own registry, the file's
# head); in warptempo_presets.txt every values line is followed by its name line from the first. A values line
# followed by another values line, or by the file's end, is the run's hard fail, the first such line named; a name
# line following no values line likewise. Lines opening with '#' are the file's note.
#
# THE TRANSCRIPTION, each role <- the scheme's key, nothing derived but the outline:
#   waveform_canvas <- WvBk          waveform_ink <- WvFg          grid <- GrdL          center <- Cntr
#   cue <- CueM                      range <- RngM                 playhead_stem <- Curs
#   face <- Face, and a scheme recording no Face ("Classic Cool", the one preset with UseW=1, every other 2) takes
#     THE DEFAULT PRESET'S FACE, read off Default's own row (626C7B; architect 2026-10-16, "make classic cool inherit
#     from default again. default is the chrome so it's internally consistent"): such a scheme makes Cool Edit follow
#     the Windows 3D face (under Wine's D4D0C8 its registry recorded a darkened UserFace 9F9785), and the product's
#     "Windows" is its default chrome, Default — Windows 95's C0C0C0 stood for one afternoon of the same day;
#   waveform_outline <- THE INK AT HLS LIGHTNESS 0.3157, hue and saturation kept: Cool Edit records no outline, and the
#     lit outline takes the lightness measured on Cool Edit's view bar span shadows (src/gui/cool_edit_derive.h's
#     `span_shadow` record, its (a, b) read off that file and checked to be (0, 0.3157); the product's span itself is
#     neutral grey since 2026-10-25, materials.h), its round trip ported below operation for operation, so the
#     bytes are cool_edit_derive.h's tone() exactly (palette_file.cpp's static_assert proves every row);
#   invalid_label <- Cntr TOO, THE PRESET'S OWN CENTER-LINE COLOR (architect 2026-10-11 ~01:00-01:20: the invalid
#     color is each palette's center line, the triangle, the dots and the resting label of an invalid cue and the `h`
#     view's removed half; Windows' dark red 800000 and the selected FF0000 of 2026-10-09 retired, the selected role
#     with it — a selected invalid label is the chrome's text as every selected label's);
#   THE ONE COOL EDIT HAS NO KEY FOR, the same in every preset: scanner FFFFFF, COOL EDIT'S OWN CONSTANT WHITE (architect
#     2026-10-16, observed in Cool Edit Pro 2.1 under Wine on the laptop: the scanner is white on every preset he tried,
#     so the preset text records no key for it; the zoom anchor's stem is the same design — his rulings of 2026-10-07
#     and 2026-10-09 now its confirmation).
#   UNUSED, no role, AND NONE WILL GAIN ONE (architect 2026-10-14, "drop": the question whether any of these keys
#     should get a palette role is closed; they are read past and never generated): Bndy (the boundary lines are not
#     drawn), _Sel, ChLi, ChBk, ChGr, ChDt, SpHi, SpLo, SpRv, SpGm, _LED, Edit, UseW, PBSg, prog, STWa, STSp, AnyL,
#     AnyR.
# THE KEY is the source's prefix — `cool-edit-` for Cool Edit's, `warptempo-` for the program's own — and the display
# name lowercased, every run of other characters one hyphen ("CEPro 1.2" -> `cool-edit-cepro-1-2`, "Phosphor, Gold and
# Ice" -> `warptempo-phosphor-gold-ice`), the `waveform` device key's word (the key was named `palette` until 2026-10-13 and `chrome`
# until 2026-10-21, the preset files' names admitted beside the built-ins until 2026-10-14, a built-in's key alone
# since); THE DISPLAY NAME is the source's own, "Default" for Cool Edit's "(Default Scheme)". Keys and display names
# are checked unique across both sources, every key in the key grammar (palette_file.cpp's is_preset_key_spelling) and every
# display name printable ASCII within 40 bytes (the color picker's typed-name grammar, 2026-10-07..14; the check
# stays as the names' bound).
#
# BYTE-STABLE: Cool Edit's presets then the program's, each in its file's order, the roles in the table's, uppercase
# hex, LF, no timestamp; run it twice and the include is identical. THE INCLUDE IS ITS OUTPUT, NEVER HAND-EDITED: a
# change is a text, then this, the output committed with it.
#
#   python3 -I tools/theme_catalog/gen_cool_edit_presets.py [Cool Edit's presets text [the program's presets text]]
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

SOURCE = os.path.join(HERE, 'cool_edit_presets.txt')
WARPTEMPO_SOURCE = os.path.join(HERE, 'warptempo_presets.txt')
PALETTE_FILE_H = os.path.join(REPO, 'src', 'gui', 'palette_file.h')
COOL_EDIT_DERIVE_H = os.path.join(REPO, 'src', 'gui', 'cool_edit_derive.h')
PRESETS_INC = os.path.join(REPO, 'src', 'gui', 'palette_presets.inc')
GENERATED = '// GENERATED by tools/theme_catalog/gen_cool_edit_presets.py; never hand-edit.\n'

# THE ROLE TABLE'S ORDER (kGuiPaletteRoles), checked against the header at every run
ROLE_ORDER = ('waveform_canvas', 'waveform_ink', 'waveform_outline', 'grid', 'center', 'cue', 'range', 'invalid_label',
              'playhead_stem', 'scanner', 'face')
# role <- the scheme's key (the head)
FROM_KEY = {'waveform_canvas': 'WvBk', 'waveform_ink': 'WvFg', 'grid': 'GrdL', 'center': 'Cntr', 'cue': 'CueM',
            'range': 'RngM', 'playhead_stem': 'Curs', 'face': 'Face'}
# the role Cool Edit has no key for (the head)
FIXED = {'scanner': 0xFFFFFF}
DEFAULT_LINE = '(Default Scheme),'
DEFAULT_NAME = 'Default'
DEFAULT_KEY = 'cool-edit-default'
KEY_PREFIX = 'cool-edit-'
WARPTEMPO_KEY_PREFIX = 'warptempo-'
OUTLINE = (0.0, 0.3157)   # the span shadow's (a, b), checked against cool_edit_derive.h
TABLE_ROW = r'\{"([a-z_]+)",\s*&GuiPalette::\w+\}'


def role_table():
    """kGuiPaletteRoles' names read off src/gui/palette_file.h, in order."""
    text = open(PALETTE_FILE_H).read()
    body = text[text.index('kGuiPaletteRoles[] = {'):]
    body = body[:body.index('};')]
    return tuple(re.findall(TABLE_ROW, body))


def span_shadow_fit():
    """cool_edit_derive.h's `span_shadow` tone's (a, b)."""
    m = re.search(r'\{"span_shadow",\s*0x[0-9A-Fa-f]{6},\s*([-0-9.]+),\s*([-0-9.]+),', open(COOL_EDIT_DERIVE_H).read())
    if not m: raise SystemExit(f'gen_cool_edit_presets: no span_shadow tone in {COOL_EDIT_DERIVE_H}')
    return float(m.group(1)), float(m.group(2))


# -- THE HLS ROUND TRIP, cool_edit_derive.h's tone() operation for operation (Python's floats are its doubles) --------

def frac(x):
    t = x - float(int(x))
    return t + 1.0 if t < 0.0 else t


def rgb_to_hls(r, g, b):
    maxc = (r if r > b else b) if r > g else (g if g > b else b)
    minc = (r if r < b else b) if r < g else (g if g < b else b)
    sumc = maxc + minc
    rangec = maxc - minc
    l = sumc / 2.0
    if minc == maxc: return 0.0, l, 0.0
    s = rangec / sumc if l <= 0.5 else rangec / (2.0 - maxc - minc)
    rc = (maxc - r) / rangec
    gc = (maxc - g) / rangec
    bc = (maxc - b) / rangec
    if r == maxc:   h = bc - gc
    elif g == maxc: h = 2.0 + rc - bc
    else:           h = 4.0 + gc - rc
    return frac(h / 6.0), l, s


def hls_value(m1, m2, hue):
    hue = frac(hue)
    if hue < 1.0 / 6.0: return m1 + (m2 - m1) * hue * 6.0
    if hue < 0.5:       return m2
    if hue < 2.0 / 3.0: return m1 + (m2 - m1) * (2.0 / 3.0 - hue) * 6.0
    return m1


def byte_of(v):
    n = int(v * 255.0 + 0.5)
    return 0 if n < 0 else (255 if n > 255 else n)


def tone(face, a, b):
    h, fl, s = rgb_to_hls(float((face >> 16) & 0xFF) / 255.0, float((face >> 8) & 0xFF) / 255.0,
                          float(face & 0xFF) / 255.0)
    l = a * fl + b
    l = 0.0 if l < 0.0 else (1.0 if l > 1.0 else l)
    if s == 0.0:
        v = byte_of(l)
        return (v << 16) | (v << 8) | v
    m2 = l * (1.0 + s) if l <= 0.5 else l + s - l * s
    m1 = 2.0 * l - m2
    return (byte_of(hls_value(m1, m2, h + 1.0 / 3.0)) << 16) | (byte_of(hls_value(m1, m2, h)) << 8) | \
        byte_of(hls_value(m1, m2, h - 1.0 / 3.0))


# -- THE SOURCE ---------------------------------------------------------------------------------------------------

def parse_values(line):
    """One values line -> {key: value}."""
    out = {}
    for item in line.split(','):
        k, sep, v = item.partition('=')
        if not sep: raise SystemExit(f'gen_cool_edit_presets: not a key=value item: {item!r}')
        if k in out: raise SystemExit(f'gen_cool_edit_presets: duplicate key {k} in {line!r}')
        out[k] = v
    return out


def presets(path, opens_with_default=True):
    """A source -> [(display name, {key: value})] in the file's order (the head's pairing): Cool Edit's text opens
    with its "(Default Scheme)" line (`opens_with_default`), the program's pairs every values line from the first."""
    lines = [ln for ln in open(path, newline='\n').read().split('\n') if ln and not ln.startswith('#')]
    out = []
    if opens_with_default:
        if not lines or not lines[0].startswith(DEFAULT_LINE):
            raise SystemExit(f'gen_cool_edit_presets: {path} does not open with {DEFAULT_LINE!r}')
        out.append([DEFAULT_NAME, parse_values(lines[0][len(DEFAULT_LINE):])])
        lines = lines[1:]
    elif not lines:
        raise SystemExit(f'gen_cool_edit_presets: {path} holds no preset')
    pending = None   # the values line still waiting for its name line
    for ln in lines:
        if '=' in ln:
            if pending is not None:
                raise SystemExit(f'gen_cool_edit_presets: the values line {pending!r} is followed by no name line')
            out.append([None, parse_values(ln)])
            pending = ln
        else:
            if pending is None: raise SystemExit(f'gen_cool_edit_presets: the name line {ln!r} follows no values line')
            out[-1][0] = ln
            pending = None
    if pending is not None:
        raise SystemExit(f'gen_cool_edit_presets: the values line {pending!r} is followed by no name line')
    return [tuple(p) for p in out]


def word(values, key, name):
    v = values.get(key)
    if v is None or not re.fullmatch(r'[0-9A-Fa-f]{6}', v):
        raise SystemExit(f'gen_cool_edit_presets: {name} records no six-digit {key}: {v!r}')
    return int(v, 16)


def preset_words(name, values, default_face):
    """One preset -> its eleven words in ROLE_ORDER (the head's transcription); `default_face` the Default preset's
    own Face, taken by a scheme recording none."""
    w = {}
    for role, key in FROM_KEY.items():
        if role == 'face' and key not in values: w[role] = default_face
        else: w[role] = word(values, key, name)
    w['waveform_outline'] = tone(w['waveform_ink'], *OUTLINE)
    w['invalid_label'] = w['center']
    w.update(FIXED)
    return [w[r] for r in ROLE_ORDER]


def preset_key(name, prefix=KEY_PREFIX):
    return prefix + re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-')


def all_presets(cool_edit_source=SOURCE, warptempo_source=WARPTEMPO_SOURCE):
    """Both sources -> [(key, display name, {key: value})], Cool Edit's twenty then the program's own (the head)."""
    return ([(preset_key(n), n, v) for n, v in presets(cool_edit_source)] +
            [(preset_key(n, WARPTEMPO_KEY_PREFIX), n, v) for n, v in presets(warptempo_source, False)])


def main():
    if len(sys.argv) > 3:
        raise SystemExit('usage: python3 -I tools/theme_catalog/gen_cool_edit_presets.py '
                         '[Cool Edit\'s presets text [the program\'s presets text]]')
    sources = (sys.argv[1:] + [SOURCE, WARPTEMPO_SOURCE][len(sys.argv) - 1:])
    if role_table() != ROLE_ORDER:
        raise SystemExit(f'gen_cool_edit_presets: {PALETTE_FILE_H}\'s kGuiPaletteRoles is {role_table()}, '
                         'not ROLE_ORDER')
    if span_shadow_fit() != OUTLINE:
        raise SystemExit(f'gen_cool_edit_presets: {COOL_EDIT_DERIVE_H}\'s span_shadow is {span_shadow_fit()}, '
                         f'not {OUTLINE}')
    ps = all_presets(*sources)
    keys = [k for k, _, _ in ps]
    names = [n for _, n, _ in ps]
    if keys[0] != DEFAULT_KEY: raise SystemExit(f'gen_cool_edit_presets: the default\'s key is {keys[0]}')
    for what, xs in (('key', keys), ('display name', names)):
        dup = sorted({x for x in xs if xs.count(x) > 1})
        if dup: raise SystemExit(f'gen_cool_edit_presets: duplicate preset {what}s: {dup}')
    for k in keys:
        if not re.fullmatch(r'[a-z0-9]+(-[a-z0-9]+)*', k) or len(k) > 40:
            raise SystemExit(f'gen_cool_edit_presets: the key {k!r} is outside the key grammar')
    for n in names:
        if not (0 < len(n) <= 40 and all(0x20 <= ord(c) <= 0x7E for c in n) and n == n.strip() and '/' not in n
                and '"' not in n and '\\' not in n):
            raise SystemExit(f'gen_cool_edit_presets: the display name {n!r} is outside the palette name grammar')
    # the face a scheme recording none takes (the head): Default's own, the first row's recorded Face
    default_face = word(ps[0][2], FROM_KEY['face'], ps[0][1])
    rows = ''.join(f'    {{"{k}", "{n}", {{{", ".join(f"0x{w:06X}" for w in preset_words(n, v, default_face))}}}}},\n'
                   for k, n, v in ps)
    with open(PRESETS_INC, 'w', newline='\n') as f:
        f.write(GENERATED +
                '// THE BUILT-IN PALETTES (src/gui/palette_file.h\'s kGuiBuiltinPalettes, included in its initializer): '
                'Cool Edit\n'
                '// Pro 2.1\'s twenty color presets in its own order (tools/theme_catalog/cool_edit_presets.txt), then '
                'the program\'s\n'
                '// own eight, keyed warptempo-, in their text\'s order (tools/theme_catalog/warptempo_presets.txt); '
                'each its key,\n'
                '// its display name and the eleven in kGuiPaletteRoles\' order under the generator\'s rules; '
                'regenerate, never hand-edit.\n' + rows)
    print(f'wrote {os.path.relpath(PRESETS_INC, REPO)}: {len(ps)} built-in palettes')


if __name__ == '__main__':
    main()
