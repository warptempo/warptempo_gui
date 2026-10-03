#!/usr/bin/env python3
# tools/theme_catalog/crops.py — docs/themes/catalog.json -> one cropped render of the app per entry
# (docs/themes/crops/<key>.png) and docs/themes/CATALOG.md, how the architect chooses (architect 2026-10-03).
#
# Each entry becomes a theme over tools/palette/themes/ad2.json's geometry and options (scratch:
# tmp/theme_catalog/themes/<key>.json), rendered by tools/palette/render.py on the default scene
# (tmp/theme_catalog/full/<key>.png, never committed), then CROPPED, NEVER SCALED (scaling blurs the pixel picture
# he judges). THE THEME (theme_for): the entry's catalog roles where the renderer has the role (ground, label, the four
# relief bytes; menu_disabled from disabled_text); every role the entry lacks keeps the app's value. THE APP-SPECIFIC
# ROLES BY THE ROLE MAPPING (architect 2026-10-03, late): the ruler label the theme's label, the ruler ticks its
# bevel_shadow, the flag OUTLINE its bevel_dkshadow; the checked face the ground under the Hilight dither (ad2's), the
# trim arrow glyph highlight_text_ink(ground) over the app's light ink #FCFCFC. THE WELL KEEPS THE APP'S TWO-LINE EDGE
# (architect 2026-10-03, late): the PLAIN SUNKEN field edge render.h draws round the waveform, top and bottom only, no
# sides, full width -- bevel_shadow then bevel_dkshadow inward on top, bevel_light inward then bevel_hilight outward at
# the bottom (WELL). THE FLAGS are the Sonic Foundry flag (render.py flags.style "bevelled"; its stem leaves the box's
# first face column through a gap in the bottom lines), the entry's flag_rule shading them, the scene's flags left to
# right unselected, SELECTED, INVALID, DISABLED and unselected (FLAG_STATES). THE PROGRAM'S OWN COLOURS (APP_KEEP):
# the chrome is the theme's; the waveform pane, the flags and the playhead are the program's own elements, their
# colours the program's and only the flags' shading the theme's (architect 2026-10-03, late): the waveform's ink and
# canvas, the flag's face and its RECORDED label (black), the invalid flag as Windows' error-icon pair (#FF0000, the
# label white), the playhead's head and stem (the app's #8B8B8B / #FCFCFC).
#
# THE CROP: four regions of the 2304 x 1440 render stacked top to bottom, 1152 px wide, a 4-row FULLY TRANSPARENT gap
# between them (alpha 0 there, 255 everywhere else, so no join reads as chrome): the top strip's left half (menu, the
# icon row with its disabled icons, the trim lane and its begin arrow, the ruler and playhead, the first two flags,
# the well's top line and a few canvas rows), its right half over the same rows (the checked View button, the end
# arrow, the other flags), and the bottom row's left (the status panel, the clock, the state line) beside its button
# block. Written as an RGBA PNG (an indexed one with a tRNS chunk when the crop has at most 256 colours), each with
# the renderer's Display-P3 iCCP chunk (the bytes are what the glass shows).
#
#   python3 tools/theme_catalog/crops.py [key ...]      (no keys: every entry)
import json, os, struct, subprocess, sys, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PALETTE = os.path.join(REPO, 'tools', 'palette')
sys.path.insert(0, PALETTE)
import numpy as np                      # noqa: E402
import pngrw                            # noqa: E402
from colour import relative_luminance   # noqa: E402

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
CROPS = os.path.join(REPO, 'docs', 'themes', 'crops')
MD = os.path.join(REPO, 'docs', 'themes', 'CATALOG.md')
SCRATCH = os.path.join(REPO, 'tmp', 'theme_catalog')
ICCP = open(os.path.join(PALETTE, 'display_p3.iccp'), 'rb').read()

# the program's own elements, P3 bytes as-is (architect 2026-10-03, late): the waveform's ink and canvas, the flag's
# face and its recorded label (render.h at the catalog's app commit: kFlagRgb, kMarkerFlagLabel), the invalid flag as
# Windows' error-icon pair (the Stop icon's white X on VGA bright red), the playhead's head and stem (kPlayheadHead,
# kPlayheadStem: program colours like the flags, not theme roles), and the app's light ink (the trim arrow's rule)
APP_KEEP = {'canvas': '#141618', 'ink': '#96BFDA', 'flag_fill': '#8A5EAC', 'flag_label': '#000000',
            'flag_fill_red': '#FF0000', 'flag_label_red': '#FFFFFF', 'playhead_head': '#8B8B8B', 'playhead_stem': '#FCFCFC',
            'light_text': '#FCFCFC'}
# the well's lines in screen order (render.py's list form): render.h's PLAIN SUNKEN edge, top and bottom only
WELL = {'top': ['@bevel_shadow', '@bevel_dkshadow'], 'bottom': ['@bevel_light', '@bevel_hilight']}
# the scene's flags (1002: five, left to right) in the four states, so each crop and mock shows every one
FLAG_STATES = {'selected': [1], 'invalid': [2], 'disabled': [3]}
APP_ROLES = {'ground': '#303030', 'label': '#FCFCFC', 'bevel_hilight': '#5E5E5E', 'bevel_light': '#434343',
             'bevel_shadow': '#1E1E1E', 'bevel_dkshadow': '#0A0A0A'}
ROLE_TO_RENDERER = {'ground': 'ground', 'label': 'label', 'bevel_hilight': 'bevel_hilight', 'bevel_light': 'bevel_light',
                    'bevel_shadow': 'bevel_shadow', 'bevel_dkshadow': 'bevel_dkshadow', 'disabled_text': 'menu_disabled'}
TOP = (0, 300)          # device rows: the menu down to 7 canvas rows under the well's two top lines (ad2: 289..293)
TOP_RIGHT = TOP         # the right half over the same rows: its flags are the scene's third to fifth (x 1343, 1816, 2289)
BOTTOM = (1346, 1440)   # the bottom row
# the bottom row's right piece starts mid-gap before its last two groups (ad2 on 1002: the groups 1168..1540,
# 1562..1810, 1832..2080, 2102..2288), so the left piece 0..669 keeps the status panel (8..286) and the state line
BOTTOM_RIGHT_X = 1821
GAP, WIDTH = 4, 1152


def unhex(s): return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))
def hx(c): return '#%02X%02X%02X' % tuple(c)


def theme_for(e):
    t = json.load(open(os.path.join(PALETTE, 'themes', 'ad2.json')))
    t['name'] = e['key']
    t['description'] = f'{e["name"]} ({e["family"]}) from docs/themes/catalog.json over ad2.json\'s geometry (tools/theme_catalog/crops.py)'
    col = {k: v for k, v in t['colours'].items() if k not in ('ruler_label', 'flag_border', 'playhead_head', 'down_face')}
    for role, v in APP_ROLES.items(): col[role] = v
    for role, rr in ROLE_TO_RENDERER.items():
        if role in e['roles']: col[rr] = e['roles'][role]
    col.update({'ruler_label': '@label', 'ruler_tick': '@bevel_shadow', 'flag_border': '@bevel_dkshadow',
                'ruler_tick_light': '@bevel_hilight', 'down_face': '@ground'})
    col.update(APP_KEEP)
    t['colours'] = col
    t['well'] = {k: list(v) for k, v in WELL.items()}
    t['flags'] = {'style': 'bevelled', 'rule': e['flag_rule'], 'states': FLAG_STATES}
    return t


def crop(full):
    """The render -> the stacked RGBA crop: every region pixel opaque, the gap rows between regions alpha 0."""
    a = np.concatenate([full, np.full(full.shape[:2] + (1,), 255, np.uint8)], axis=2)
    gap = np.zeros((GAP, WIDTH, 4), np.uint8)
    top_l = a[TOP[0]:TOP[1], 0:WIDTH]
    top_r = a[TOP_RIGHT[0]:TOP_RIGHT[1], a.shape[1] - WIDTH:]
    right = a.shape[1] - BOTTOM_RIGHT_X
    bot = np.concatenate([a[BOTTOM[0]:BOTTOM[1], 0:WIDTH - right], a[BOTTOM[0]:BOTTOM[1], BOTTOM_RIGHT_X:]], axis=1)
    return np.ascontiguousarray(np.concatenate([top_l, gap, top_r, gap, bot], axis=0))


def _chunk(name, data):
    c = struct.pack('>I', len(data)) + name + data
    return c + struct.pack('>I', zlib.crc32(name + data) & 0xffffffff)


def write_png(path, arr):
    """An RGBA crop -> an indexed PNG (colour type 3, a tRNS chunk carrying the gap's alpha 0) when it has <= 256
    colours, else 8-bit RGBA (colour type 6); both lossless, both with the Display-P3 iCCP chunk, every row unfiltered
    (on these flat pictures PNG's adaptive filtering compresses worse: 59.9 KB against 49.4 KB on kde3-dark-blue,
    measured on the RGB crops)."""
    h, w, _ = arr.shape
    flat = arr.reshape(-1, 4).astype(np.uint32)
    key = (flat[:, 3] << 24) | (flat[:, 0] << 16) | (flat[:, 1] << 8) | flat[:, 2]
    cols, idx = np.unique(key, return_inverse=True)
    head = b'\x89PNG\r\n\x1a\n'
    if len(cols) > 256:
        rows = np.concatenate([np.zeros((h, 1), np.uint8), arr.reshape(h, w * 4)], axis=1)
        out = (head + _chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + _chunk(b'iCCP', ICCP)
               + _chunk(b'IDAT', zlib.compress(rows.tobytes(), 9)) + _chunk(b'IEND', b''))
        open(path, 'wb').write(out)
        return 'rgba', len(cols)
    depth = 8 if len(cols) > 16 else 4 if len(cols) > 4 else 2 if len(cols) > 2 else 1
    plte = np.stack([(cols >> 16) & 255, (cols >> 8) & 255, cols & 255], axis=1).astype(np.uint8).tobytes()
    trns = ((cols >> 24) & 255).astype(np.uint8).tobytes()
    idx = idx.reshape(h, w).astype(np.uint8)
    if depth < 8:
        per = 8 // depth; pad = (-w) % per
        idx = np.pad(idx, ((0, 0), (0, pad)))
        packed = np.zeros((h, idx.shape[1] // per), np.uint8)
        for i in range(per): packed |= (idx[:, i::per] << (8 - depth * (i + 1))).astype(np.uint8)
        idx = packed
    rows = np.concatenate([np.zeros((h, 1), np.uint8), idx], axis=1)
    out = (head + _chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, 3, 0, 0, 0))
           + _chunk(b'iCCP', ICCP) + _chunk(b'PLTE', plte) + _chunk(b'tRNS', trns)
           + _chunk(b'IDAT', zlib.compress(rows.tobytes(), 9)) + _chunk(b'IEND', b''))
    open(path, 'wb').write(out)
    return f'indexed {depth}-bit', len(cols)


FAMILY_HEAD = {
    'windows': 'Windows: the Appearance schemes (ReactOS hivedef.inf, corroborated by the Windows XP classic schemes '
               'saved as .theme files; Windows 95 Standard hand-recorded)',
    'windows-plus': 'Windows 98 / Plus! desktop themes (the shipped .theme files)',
    'kde3': 'KDE 3 / Trinity colour schemes (relief by KDE 3\'s own rule at each scheme\'s contrast)',
    'cde': 'CDE palettes (colour set 5 the ground; foreground and shadows by Motif\'s own rule)',
    'warptempo': 'Warptempo: the app\'s own look as it stands on 2026-10-03',
}


def prov_line(e):
    p = e['provenance']; s = p['sources'][0]
    where = s.get('repository') or s.get('image') or s.get('project')
    ref = (s.get('commit') or '')[:8]
    line = f"{where}{'@' + ref if ref else ''} `{s['file']}`" if 'file' in s else where
    if len(p['sources']) > 1: line += f" + {len(p['sources']) - 1} more"
    if 'rule' in p: line += f"; {p['rule']['id']} rule" + (f" at contrast {p['rule']['contrast']}" if 'contrast' in p['rule'] else '')
    return line


def write_md(cat, sizes):
    L = ['# The theme catalog', '',
         'Every entry below is a desktop theme of the era IMPORTED, not designed (architect 2026-10-03: "no derived, '
         'imported only"): its colours are the bytes its source records, each with its provenance in '
         '[catalog.json](catalog.json); where the source records only base colours and its own toolkit computed the '
         'relief at run time (KDE 3, CDE / Motif), that toolkit\'s rule ran once at import and is named. The KEY is '
         'what to type in Settings to pick it. Each crop is the app rendered in the theme (tools/palette on ad2.json\'s '
         'geometry, cropped, never scaled; tools/theme_catalog/crops.py): the top strip in two halves over the bottom '
         'row, transparent between them. The chrome is the theme\'s; the waveform pane and the flags are the program\'s '
         'own elements, their colours the app\'s and their shading the theme\'s (architect 2026-10-03, late): the well keeps '
         'the app\'s two-line sunken edge (the theme\'s Shadow and DkShadow above, its 3DLight and Hilight below), the '
         'flags the bevelled box of Sonic Foundry\'s editors shaded by the theme family\'s own rule (the entry\'s flag '
         'rule), the stem leaving the box\'s face through a gap in its bottom lines, shown left to right '
         'unselected, selected, invalid and disabled, each label the colour recorded beside its face (black on the '
         'app\'s purple; the invalid flag is Windows\' error-icon pair, white on #FF0000); the playhead is the '
         'program\'s too (the app\'s #8B8B8B head, #FCFCFC stem); the ruler label is the theme\'s label, the ruler '
         'ticks its Shadow, the flag outline its DkShadow. The DISPLAY TIER is the smallest period colour set holding '
         'every colour the entry\'s roles use: vga (the 16 VGA colours), windows-20 (those and Windows\' four static '
         'extras #C0DCC0, #A6CAF0, #FFFBF0, #A0A0A4, always solid on a 256-colour display), else high-colour. Not imported: '
         'catalog.json\'s `not_imported`. Built by `tools/theme_catalog/` (fetch.py, build.py, crops.py).', '']
    fams = []
    for e in cat['entries']:
        if e['family'] not in fams: fams.append(e['family'])
    for fam in fams:
        es = sorted((e for e in cat['entries'] if e['family'] == fam), key=lambda e: relative_luminance(unhex(e['roles']['ground'])))
        L += [f'## {FAMILY_HEAD[fam]}', '', f'{len(es)} entries, darkest ground first.', '']
        for e in es:
            imit = f" — imitates {e['imitates']}" if e.get('imitates') else ''
            L += [f"### `{e['key']}`", '', f"**{e['name']}**{imit} · ground {e['roles']['ground']} · {prov_line(e)}", '',
                  f"Display tier: {e['display_tier']}", '', f"![{e['key']}](crops/{e['key']}.png)", '']
    open(MD, 'w').write('\n'.join(L) + '\n')


def main():
    cat = json.load(open(CATALOG))
    want = set(sys.argv[1:])
    os.makedirs(CROPS, exist_ok=True)
    for d in ('themes', 'full'): os.makedirs(os.path.join(SCRATCH, d), exist_ok=True)
    sizes = {}
    for e in cat['entries']:
        if want and e['key'] not in want: continue
        tp = os.path.join(SCRATCH, 'themes', e['key'] + '.json'); fp = os.path.join(SCRATCH, 'full', e['key'] + '.png')
        json.dump(theme_for(e), open(tp, 'w'), indent=1)
        r = subprocess.run([sys.executable, os.path.join(PALETTE, 'render.py'), tp, fp], capture_output=True, text=True)
        if r.returncode: raise SystemExit(f'crops: {e["key"]}: render failed: {r.stderr.strip() or r.stdout.strip()}')
        out = os.path.join(CROPS, e['key'] + '.png')
        kind, n = write_png(out, crop(pngrw.read_rgb(fp)))
        sizes[e['key']] = os.path.getsize(out)
        print(f'{e["key"]:40s} {sizes[e["key"]]:8d} bytes  {kind}, {n} colours')
    if not want:
        stale = set(os.listdir(CROPS)) - {e['key'] + '.png' for e in cat['entries']}
        for s in stale: os.remove(os.path.join(CROPS, s))
        write_md(cat, sizes)
    print(f'total {sum(sizes.values())} bytes over {len(sizes)} crops')


if __name__ == '__main__':
    main()
