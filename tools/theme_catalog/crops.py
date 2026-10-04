#!/usr/bin/env python3
# tools/theme_catalog/crops.py — docs/themes/catalog.json -> one cropped render of the app per entry
# (docs/themes/crops/<key>.png) and docs/themes/CATALOG.md, how the architect chooses (architect 2026-10-03).
#
# Each entry becomes a theme over tools/palette/themes/tablet.json, THE TABLET GEOMETRY (architect 2026-10-03, step 13:
# the app at gui_scale 275, every length derived from the app's own constants, tools/palette/tablet.py; scratch:
# tmp/theme_catalog/themes/<key>.json), rendered by tools/palette/render.py in scene 1002's state
# (tmp/theme_catalog/full/<key>.png, never committed), then CROPPED, NEVER SCALED (scaling blurs the pixel picture
# he judges). THE CROP IS THE APP'S FINAL DESIGN AT THE LIGHT LEVEL (architect 2026-10-03; theme_for): the entry's
# LIGHT row as the app's generated table carries it (levels.level_roles: the ground, the label, the relief quartet,
# the emboss's light copy, the selected pair, the field ground), so the crop and the app read one module. THE
# APP-SPECIFIC ROLES BY THE ROLE MAPPING (architect 2026-10-03; tablet.json states them): the ruler label the theme's
# label, the ruler ticks its Shadow, the flag OUTLINE its DkShadow, the trim lane's arrow glyph its label (render.cpp
# paint_trim_arrow_button); the checked face the ground under the Hilight dither. EVERY OPTION IS THE APP'S (the tablet
# geometry fixes them, render.py TABLET_FIXED): the well's two-line PLAIN SUNKEN edge (Shadow then DkShadow inward on
# top, 3DLight inward then Hilight outward at the bottom), the flat flag with its one-px DkShadow outline and the stem
# leaving the box's first face column across the bottom outline, the SELECTED flag's BRIGHTER FACE (architect
# 2026-10-03, the colour loop: the face and the stem the selected key under the selected label, the outline still
# DkShadow; the white outline retired), the engraved disabled word and glyph (Windows' DSS_DISABLED: the emboss's light
# copy one px right and down, then the word or glyph in Shadow), the playhead's head outlined in the theme's label. The
# scene's flags show left to right EDITING (the in-place editor, the selected flag opened for edit: a black frame on the
# selected face, its whole text in the selected pair), SELECTED, INVALID, DISABLED (the ground, the label embossed, no
# stem) and unselected (FLAG_STATES). THE PROGRAM'S OWN COLOURS (APP_KEEP, the device config's defaults): the
# waveform's ink, canvas and outline, the flag's face, selected face and RECORDED label, the one selected label, the
# invalid face, its selected face and its label, the playhead's head and stem; the icons' fixed inks are icons.cpp's
# (tablet.json).
#
# THE CROP: four regions of the 2304 x 1440 render stacked top to bottom, 1152 px wide, a 4-row FULLY TRANSPARENT gap
# between them (alpha 0 there, 255 everywhere else, so no join reads as chrome): the top strip's left half (menu, the
# icon row with its disabled icons, the trim lane and its begin arrow, the ruler and playhead, the first two flags,
# the well's top lines and a few canvas rows), its right half over the same rows (the checked View button, the end
# arrow, the other flags), and the well's bottom lines over the bottom row, its left (the status panel, the clock, the
# state line) beside its last two button groups. THE WINDOWS FOLLOW THE TABLET GEOMETRY (tablet.SCENE; regions()). Written as an RGBA PNG (an indexed one with a tRNS chunk when the crop has at most 256 colours), each with
# the renderer's Display-P3 iCCP chunk (the bytes are what the glass shows).
#
#   python3 tools/theme_catalog/crops.py [key ...]      (no keys: every entry)
#   python3 tools/theme_catalog/crops.py --md           (renders nothing: CATALOG.md rewritten from catalog.json and the
#                                                        crops of keys no longer in it deleted, the others untouched)
import json, os, struct, subprocess, sys, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PALETTE = os.path.join(REPO, 'tools', 'palette')
sys.path.insert(0, PALETTE)
import numpy as np                      # noqa: E402
import pngrw                            # noqa: E402
from colour import relative_luminance   # noqa: E402
sys.path.insert(0, HERE)
import levels                           # noqa: E402
import tablet                           # noqa: E402  (tools/palette: the geometry the crop windows follow)

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
CROPS = os.path.join(REPO, 'docs', 'themes', 'crops')
MD = os.path.join(REPO, 'docs', 'themes', 'CATALOG.md')
SCRATCH = os.path.join(REPO, 'tmp', 'theme_catalog')
ICCP = open(os.path.join(PALETTE, 'display_p3.iccp'), 'rb').read()

# the program's own elements, P3 bytes as-is: the device config's defaults (device_config.h, architect 2026-10-03, the
# colour loop) -- the waveform's grey ink on its black canvas and the lit inner-bar outline, the flag's slate violet,
# its selected #CCCCFF and its recorded white label, the one selected label black, the invalid dark red, its selected
# bright red and its white label, the playhead's head and stem
APP_KEEP = {'canvas': '#000000', 'ink': '#808080', 'outline': '#5C5C5C', 'flag_fill': '#666699', 'flag_fill_sel': '#CCCCFF',
            'flag_label': '#FFFFFF', 'flag_label_sel': '#000000', 'flag_fill_red': '#993333',
            'flag_fill_red_sel': '#FF6666', 'flag_label_red': '#FFFFFF', 'playhead_head': '#8B8B8B',
            'playhead_stem': '#FCFCFC'}
# the scene's flags (1002: five, left to right) in every state, so each crop shows each one
FLAG_STATES = {'editing': [0], 'selected': [1], 'invalid': [2], 'disabled': [3]}
# the level roles (levels.ROLES' names are the renderer's own; the field text has no surface on the crop's scene and
# rides along for the mocks that switch the dialog on; the card, likewise off on the crop's scene, is the ground under
# the label and reads no role of its own)
LEVEL_TO_RENDERER = ('ground', 'label', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow',
                     'emboss_hilight', 'selected_fill', 'selected_text', 'field_ground', 'field_text')
GAP, WIDTH = 4, 1152


def regions():
    """The crop's windows on the tablet geometry (tablet.SCENE), device rows and columns end-exclusive -> (TOP,
    BOTTOM, BOTTOM_RIGHT_X): the top strip from the menu down to 7 canvas rows under the well's two top lines (0..296:
    the canvas from 289), both halves over those rows (the right half's flags the scene's third to fifth, x 1343, 1816,
    2289); the well's two bottom lines and the bottom row (1343..1440); the bottom row's right piece from the middle of
    the group space before its last two groups (the arrows from 1819: x 1808), so the left piece 0..656 keeps the status
    panel (14..308) and the state line (from 330)."""
    S = tablet.SCENE; gs = S['group_space']
    top = (0, S['canvas'][0] + 7)
    bottom = (S['canvas'][1], S['lanes']['bottom'][1])
    right_x = S['bottom_groups'][-2] - gs // 2
    return top, bottom, right_x


TOP, BOTTOM, BOTTOM_RIGHT_X = regions()
TOP_RIGHT = TOP


def unhex(s): return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))
def hx(c): return '#%02X%02X%02X' % tuple(c)


def theme_for(e, level='light'):
    """Entry e at `level` (levels.LEVELS) as a render.py theme over tablet.json: the app on the tablet, its level row
    (levels.level_roles) over the role mapping and the program's colours, every option the app's."""
    t = json.load(open(os.path.join(PALETTE, 'themes', 'tablet.json')))
    t['name'] = e['key']
    t['description'] = (f'{e["name"]} ({e["family"]}) at its {level} level from docs/themes/catalog.json over tablet.json, '
                        f'the app on the tablet (tools/theme_catalog/crops.py)')
    col = t['colours']; r = levels.level_roles(e, level)
    for role in LEVEL_TO_RENDERER: col[role] = r[role]
    col.update({'ruler_label': '@label', 'ruler_tick': '@bevel_shadow', 'flag_border': '@bevel_dkshadow',
                'ruler_tick_light': '@bevel_hilight', 'down_face': '@ground', 'trim_arrow': '@label'})
    col.update(APP_KEEP)
    t['flags'] = {'style': 'flat', 'states': FLAG_STATES}
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
    'kde3': 'KDE 3.5 colour schemes, as Trinity\'s tdebase carries them (relief by KDE 3\'s own rule at each scheme\'s contrast)',
    'cde': 'CDE palettes (colour set 5 the ground; foreground and shadows by Motif\'s own rule)',
    'warptempo': 'Warptempo: the program\'s own (the app\'s look of the morning of 2026-10-03 recorded off render.h, '
                 '`warptempo`, the architect\'s pick of the colour loop and the app\'s default, and his presets saved on '
                 'the colour picker: chosen, not imported)',
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
         'relief at run time (KDE 3, CDE / Motif), that toolkit\'s rule ran once at import and is named. The one '
         'family that imports nothing is the program\'s own, Warptempo: the app\'s look of 2026-10-03 recorded off '
         'render.h, `warptempo`, the app\'s default, CHOSEN, NOT IMPORTED (the architect\'s pick of the colour '
         'loop, 2026-10-03; its provenance is his ruling), and each `warptempo-preset-<n>`, his Preset <n> saved on '
         'the colour picker, chosen, not imported either (2026-10-04: the preset\'s chrome ground through the '
         'picker\'s chrome rule, the roles the picker shows fixed). The KEY is '
         'what to type in Settings to pick it. Each crop is the app rendered in the theme (tools/palette in its tablet '
         'geometry: the tablet\'s 2304 x 1440 at gui_scale 275, every length derived from the app\'s own constants; '
         'cropped, never scaled; tools/theme_catalog/crops.py): the top strip in two halves over the well\'s bottom '
         'lines and the bottom row, transparent between them, at the theme\'s LIGHT level (the app\'s `theme_level`; dark is the '
         'generated table\'s, tools/theme_catalog/levels.py). The chrome is the theme\'s; the waveform pane, the flags '
         'and the playhead are the program\'s own elements in the app\'s default colours (architect 2026-10-03): the '
         'well keeps the app\'s two-line sunken edge (the theme\'s Shadow and DkShadow above, its 3DLight and Hilight '
         'below) round the grey waveform on black; the flags are the flat Acid flag, the face the app\'s slate violet '
         '#666699 with its recorded white label and a one-px outline in the theme\'s DkShadow, the stem leaving the face '
         'across the bottom outline, shown left to right editing (the in-place editor: the selected face under a black '
         'frame, its text in the selected pair), selected (the brighter face #CCCCFF under a black label, the stem '
         'with it), invalid (#993333, white label), disabled (the ground, the label embossed) and unselected; the '
         'playhead\'s #8B8B8B head carries a one-px outline in the theme\'s label over its #FCFCFC '
         'stem; disabled words and glyphs are Windows\' emboss; the ruler label and the trim arrow are the theme\'s '
         'label, the ruler ticks its Shadow. The DISPLAY TIER is the smallest period colour set holding '
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
    if sys.argv[1:] == ['--md']:
        stale = sorted(set(os.listdir(CROPS)) - {e['key'] + '.png' for e in cat['entries']})
        for s in stale: os.remove(os.path.join(CROPS, s))
        write_md(cat, {})
        print(f'wrote {os.path.relpath(MD, REPO)} ({len(cat["entries"])} entries); deleted {len(stale)} stale crops: '
              + ', '.join(stale))
        return
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
