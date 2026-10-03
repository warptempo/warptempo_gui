#!/usr/bin/env python3
# tools/theme_catalog/crops.py — docs/themes/catalog.json -> one cropped render of the app per entry
# (docs/themes/crops/<key>.png) and docs/themes/CATALOG.md, how the architect chooses (architect 2026-10-03).
#
# Each entry becomes a theme over tools/palette/themes/ad2.json's geometry and options (scratch:
# tmp/theme_catalog/themes/<key>.json), rendered by tools/palette/render.py on the default scene
# (tmp/theme_catalog/full/<key>.png, never committed), then CROPPED, NEVER SCALED (scaling blurs the pixel picture
# he judges). THE THEME: the entry's catalog roles where the renderer has the role (ground, label, the four relief
# bytes; menu_disabled from disabled_text); every role the entry lacks keeps the app's value. THE APP-SPECIFIC ROLES
# are drawn by the app's CURRENT RULES over the theme's ground, not mapped (src/gui/render.h at the catalog's app
# commit): the ruler label = scaled_word(ground, 404, 100), the playhead head x 29/10, the flag border x 7/16, the
# ruler tick the Shadow, the checked face the ground under the Hilight dither, the well Windows' plain sunken edge
# (Shadow, DkShadow over the canvas; 3DLight, Hilight under it), the playhead stem the label, the trim arrow glyph
# highlight_text_ink(ground) over the app's light ink #FCFCFC; the waveform and the flags keep the app's colours
# (a theme owns the chrome). Their mapping is his decision once he has seen the crops.
#
# THE CROP: four regions of the 2304 x 1440 render stacked top to bottom, 1152 px wide, a 4-px #7F7F7F rule between
# them: the top strip's left half (menu, the icon row with its disabled icons, the trim lane and its begin arrow,
# the ruler and playhead, the flags, the well's top lines and a few canvas rows), its right half down to the trim
# lane's foot (the checked View button, the end arrow), and the bottom row's left (the status panel, the clock, the state line) beside its
# button block. Written as an indexed PNG when the crop has at most 256 colours (lossless), else RGB, each with the
# renderer's Display-P3 iCCP chunk (the bytes are what the glass shows).
#
#   python3 tools/theme_catalog/crops.py [key ...]      (no keys: every entry)
import json, os, struct, subprocess, sys, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PALETTE = os.path.join(REPO, 'tools', 'palette')
sys.path.insert(0, PALETTE)
import numpy as np                      # noqa: E402
import pngrw                            # noqa: E402
from colour import scaled_word, relative_luminance   # noqa: E402

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
CROPS = os.path.join(REPO, 'docs', 'themes', 'crops')
MD = os.path.join(REPO, 'docs', 'themes', 'CATALOG.md')
SCRATCH = os.path.join(REPO, 'tmp', 'theme_catalog')
ICCP = open(os.path.join(PALETTE, 'display_p3.iccp'), 'rb').read()

# the app's look outside the chrome (render.h at the catalog's app commit), P3 bytes as-is
APP_KEEP = {'canvas': '#141618', 'ink': '#96BFDA', 'flag_fill': '#8A5EAC', 'flag_edge': '#4D345F',
            'flag_fill_sel': '#B37BE0', 'flag_edge_sel': '#63447B', 'flag_label': '#000000', 'light_text': '#FCFCFC'}
APP_ROLES = {'ground': '#303030', 'label': '#FCFCFC', 'bevel_hilight': '#5E5E5E', 'bevel_light': '#434343',
             'bevel_shadow': '#1E1E1E', 'bevel_dkshadow': '#0A0A0A'}
ROLE_TO_RENDERER = {'ground': 'ground', 'label': 'label', 'bevel_hilight': 'bevel_hilight', 'bevel_light': 'bevel_light',
                    'bevel_shadow': 'bevel_shadow', 'bevel_dkshadow': 'bevel_dkshadow', 'disabled_text': 'menu_disabled'}
TOP = (0, 300)          # device rows: the menu down to 7 canvas rows under the well's top lines (ad2: 289..293)
TOP_RIGHT = (0, 192)    # the right half down to the trim lane's foot (ad2: the lane 148..192); its ruler and flags repeat the left's
BOTTOM = (1346, 1440)   # the bottom row
# the bottom row's right piece starts mid-gap before its last two groups (ad2 on 1002: the groups 1168..1540,
# 1562..1810, 1832..2080, 2102..2288), so the left piece 0..669 keeps the status panel (8..286) and the state line
BOTTOM_RIGHT_X = 1821
GAP, GAP_RGB, WIDTH = 4, (0x7F, 0x7F, 0x7F), 1152


def unhex(s): return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))
def hx(c): return '#%02X%02X%02X' % tuple(c)
def scaled(c, num, den): return hx(scaled_word(v, num, den) for v in unhex(c))


def theme_for(e):
    t = json.load(open(os.path.join(PALETTE, 'themes', 'ad2.json')))
    t['name'] = e['key']
    t['description'] = f'{e["name"]} ({e["family"]}) from docs/themes/catalog.json over ad2.json\'s geometry (tools/theme_catalog/crops.py)'
    col = {k: v for k, v in t['colours'].items() if k not in ('ruler_label', 'flag_border', 'playhead_head', 'down_face')}
    for role, v in APP_ROLES.items(): col[role] = v
    for role, rr in ROLE_TO_RENDERER.items():
        if role in e['roles']: col[rr] = e['roles'][role]
    g = col['ground']
    col.update({'ruler_label': scaled(g, 404, 100), 'playhead_head': scaled(g, 29, 10), 'flag_border': scaled(g, 7, 16),
                'ruler_tick': '@bevel_shadow', 'ruler_tick_light': '@bevel_hilight', 'down_face': '@ground',
                'well_border': '@bevel_dkshadow', 'playhead_stem': '@label'})
    col.update(APP_KEEP)
    t['colours'] = col
    t['well'] = {'top': ['@bevel_shadow', '@bevel_dkshadow'], 'bottom': ['@bevel_light', '@bevel_hilight']}
    return t


def crop(full):
    a = full
    gap = np.empty((GAP, WIDTH, 3), np.uint8); gap[:] = GAP_RGB
    top_l = a[TOP[0]:TOP[1], 0:WIDTH]
    top_r = a[TOP_RIGHT[0]:TOP_RIGHT[1], a.shape[1] - WIDTH:]
    right = a.shape[1] - BOTTOM_RIGHT_X
    bot = np.concatenate([a[BOTTOM[0]:BOTTOM[1], 0:WIDTH - right], a[BOTTOM[0]:BOTTOM[1], BOTTOM_RIGHT_X:]], axis=1)
    return np.ascontiguousarray(np.concatenate([top_l, gap, top_r, gap, bot], axis=0))


def _chunk(name, data):
    c = struct.pack('>I', len(data)) + name + data
    return c + struct.pack('>I', zlib.crc32(name + data) & 0xffffffff)


def write_png(path, arr):
    """Indexed (colour type 3) when the picture has <= 256 colours, else RGB (pngrw.write_png); both lossless, both
    with the Display-P3 iCCP chunk, every row unfiltered (on these flat pictures PNG's adaptive filtering compresses
    worse: 59.9 KB against 49.4 KB on kde3-dark-blue)."""
    h, w, _ = arr.shape
    flat = arr.reshape(-1, 3)
    key = (flat[:, 0].astype(np.uint32) << 16) | (flat[:, 1].astype(np.uint32) << 8) | flat[:, 2]
    cols, idx = np.unique(key, return_inverse=True)
    if len(cols) > 256:
        pngrw.write_png(path, arr, [(b'iCCP', ICCP)]); return 'rgb', len(cols)
    depth = 8 if len(cols) > 16 else 4 if len(cols) > 4 else 2 if len(cols) > 2 else 1
    plte = np.stack([(cols >> 16) & 255, (cols >> 8) & 255, cols & 255], axis=1).astype(np.uint8).tobytes()
    idx = idx.reshape(h, w).astype(np.uint8)
    if depth < 8:
        per = 8 // depth; pad = (-w) % per
        idx = np.pad(idx, ((0, 0), (0, pad)))
        packed = np.zeros((h, idx.shape[1] // per), np.uint8)
        for i in range(per): packed |= (idx[:, i::per] << (8 - depth * (i + 1))).astype(np.uint8)
        idx = packed
    rows = np.concatenate([np.zeros((h, 1), np.uint8), idx], axis=1)
    out = (b'\x89PNG\r\n\x1a\n' + _chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, 3, 0, 0, 0))
           + _chunk(b'iCCP', ICCP) + _chunk(b'PLTE', plte) + _chunk(b'IDAT', zlib.compress(rows.tobytes(), 9))
           + _chunk(b'IEND', b''))
    open(path, 'wb').write(out)
    return f'indexed {depth}-bit', len(cols)


FAMILY_HEAD = {
    'windows': 'Windows: the Appearance schemes (ReactOS hivedef.inf, corroborated by the Windows XP classic schemes '
               'saved as .theme files; Windows 95 Standard hand-recorded)',
    'windows-plus': 'Windows 98 / Plus! desktop themes (the shipped .theme files)',
    'reactos': 'ReactOS: schemes no second source corroborates as Windows\'',
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
         'row. The ruler label, the playhead head, the flag border and the checked face are drawn by the app\'s current '
         'rules over the theme\'s ground, and the waveform and the flags keep the app\'s colours; how those follow a '
         'theme is still to be decided. Built by `tools/theme_catalog/` (fetch.py, build.py, crops.py).', '']
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
