#!/usr/bin/env python3
# tools/theme_catalog/crops.py — docs/themes/catalog.json -> one cropped render of the app per entry
# (docs/themes/crops/<key>.png) and docs/themes/CATALOG.md, how the architect chooses (architect 2026-10-03).
#
# Each entry becomes a theme over tools/palette/themes/ad2.json's geometry and options (scratch:
# tmp/theme_catalog/themes/<key>.json), rendered by tools/palette/render.py on the default scene
# (tmp/theme_catalog/full/<key>.png, never committed), then CROPPED, NEVER SCALED (scaling blurs the pixel picture
# he judges). THE THEME (theme_for): the entry's catalog roles where the renderer has the role (ground, label, the four
# relief bytes; menu_disabled from disabled_text); every role the entry lacks keeps the app's value. THE APP-SPECIFIC
# ROLES BY THE ROLE MAPPING (architect 2026-10-03, late): the ruler label the theme's label, the ruler ticks and the
# playhead head its bevel_shadow, the flag OUTLINE its bevel_dkshadow; the playhead stem the label, the checked face
# the ground under the Hilight dither (ad2's), the trim arrow glyph highlight_text_ink(ground) over the app's light
# ink #FCFCFC. THE WELL IS SOUND RECORDER'S WAVEFORM BOX (architect 2026-10-03, late): one bevel_shadow line on top and
# one bevel_hilight line at the bottom, full width, no sides (Windows' one-line static edge). THE FLAGS are the
# Sonic Foundry flag (render.py flags.style "bevelled"), the entry's flag_rule shading them, the scene's flags left to
# right unselected, SELECTED, INVALID, DISABLED and unselected (FLAG_STATES). The waveform's ink and canvas and the
# flag's face and red keep the app's colours (APP_KEEP): the chrome is the theme's, the pane and the flags the
# program's own elements, shaded by the theme.
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

# the program's own elements (render.h at the catalog's app commit), P3 bytes as-is: the waveform's ink and canvas,
# the flag's face and the stock red (kFlagRgb, kRedRgb), and the app's light ink (the trim arrow's rule)
APP_KEEP = {'canvas': '#141618', 'ink': '#96BFDA', 'flag_fill': '#8A5EAC', 'flag_fill_red': '#BB575A',
            'light_text': '#FCFCFC'}
# the scene's flags (1002: five, left to right) in the four states, so each crop and mock shows every one
FLAG_STATES = {'selected': [1], 'invalid': [2], 'disabled': [3]}
APP_ROLES = {'ground': '#303030', 'label': '#FCFCFC', 'bevel_hilight': '#5E5E5E', 'bevel_light': '#434343',
             'bevel_shadow': '#1E1E1E', 'bevel_dkshadow': '#0A0A0A'}
ROLE_TO_RENDERER = {'ground': 'ground', 'label': 'label', 'bevel_hilight': 'bevel_hilight', 'bevel_light': 'bevel_light',
                    'bevel_shadow': 'bevel_shadow', 'bevel_dkshadow': 'bevel_dkshadow', 'disabled_text': 'menu_disabled'}
TOP = (0, 300)          # device rows: the menu down to 7 canvas rows under the well's top lines (ad2: 289..293)
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
    col.update({'ruler_label': '@label', 'ruler_tick': '@bevel_shadow', 'playhead_head': '@bevel_shadow',
                'flag_border': '@bevel_dkshadow', 'ruler_tick_light': '@bevel_hilight', 'down_face': '@ground',
                'playhead_stem': '@label'})
    col.update(APP_KEEP)
    t['colours'] = col
    t['well'] = {'top': ['@bevel_shadow'], 'bottom': ['@bevel_hilight']}
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
         'own elements, their colours the app\'s and their shading the theme\'s (architect 2026-10-03, late): the well is '
         'Sound Recorder\'s waveform box (the theme\'s Shadow above, its Hilight below), the flags the bevelled box of '
         'Sonic Foundry\'s editors shaded by the theme family\'s own rule (the entry\'s flag rule), shown left to right '
         'unselected, selected, invalid and disabled; the ruler label is the theme\'s label, the ruler ticks and the '
         'playhead head its Shadow, the flag outline its DkShadow. Not imported: '
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
                  f"![{e['key']}](crops/{e['key']}.png)", '']
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
