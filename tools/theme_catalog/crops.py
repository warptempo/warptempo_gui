#!/usr/bin/env python3
# tools/theme_catalog/crops.py — docs/themes/catalog.json -> one cropped render of the app per entry
# (docs/themes/crops/<key>.png) and docs/themes/CATALOG.md, how the architect chooses (architect 2026-10-03).
#
# Each entry becomes a theme over tools/palette/themes/tablet.json, THE TABLET GEOMETRY (architect 2026-10-03, step 13:
# the app at gui_scale 275, every length derived from the app's own constants, tools/palette/tablet.py; scratch:
# tmp/theme_catalog/themes/<key>.json), rendered by tools/palette/render.py in scene 1002's state
# (tmp/theme_catalog/full/<key>.png, never committed), then CROPPED, NEVER SCALED (scaling blurs the pixel picture
# he judges). THE CROP IS THE APP'S DESIGN IN THE ENTRY'S CHROME (architect 2026-10-03; theme_for): the entry as
# recorded (roles.light_roles: the ground, the label, the relief quartet, the emboss's light copy, the selected pair,
# the field ground), the one function the bundled theme files are written by too (gen_theme_files.py). THE
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
# stem) and unselected (FLAG_STATES). THE PROGRAM'S OWN COLOURS ARE THE ENTRY'S THEME FILE'S (architect 2026-10-05;
# program_colours): each program role the entry's bundled file names, else the built-in's — the app's own resolution
# (gen_theme_files.resolved_roles over the role table read off theme_file.h), so a crop paints what the app paints
# under that theme: the waveform's canvas, ink and outline, the flag's face, selected face and label (the scene's flags
# are warp markers, so the WARP pair), the one selected label, the invalid flag the removed pair under the one label,
# the playhead's stem (its head the renderer's own, PROGRAM_KEYS); the icons' fixed inks are icons.cpp's (tablet.json).
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
import roles                            # noqa: E402
import gen_theme_files as G             # noqa: E402  (the role table, the built-in and each entry's file)
import tablet                           # noqa: E402  (tools/palette: the geometry the crop windows follow)

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
CROPS = os.path.join(REPO, 'docs', 'themes', 'crops')
MD = os.path.join(REPO, 'docs', 'themes', 'CATALOG.md')
SCRATCH = os.path.join(REPO, 'tmp', 'theme_catalog')
ICCP = open(os.path.join(PALETTE, 'display_p3.iccp'), 'rb').read()

# the program's own elements: the renderer's colour key <- the app's role (the role table, theme_file.h), P3 bytes
# as-is; one flag kind on the scene (warp), the invalid flag wearing the removed pair under the one flag label
PROGRAM_KEYS = (('canvas', 'waveform_canvas'), ('ink', 'waveform_ink'), ('outline', 'waveform_outline'),
                ('flag_fill', 'warp_flag'), ('flag_fill_sel', 'warp_flag_selected'), ('flag_label', 'flag_label'),
                ('flag_label_sel', 'flag_label_selected'), ('flag_fill_red', 'removed_flag'),
                ('flag_fill_red_sel', 'removed_flag_selected'), ('flag_label_red', 'flag_label'),
                ('playhead_stem', 'playhead_stem'))
# (the renderer's `playhead_head` is no app role since 2026-10-05 — the app's head is WordPad's ruler marker in chrome
# roles, render.h's THE PLAYHEAD block, which the renderer does not draw — so a crop keeps the renderer's own head at
# tablet.json's grey)
PRESETS, PICKER = G.inputs()


def program_colours(e):
    """Entry e -> {renderer key: '#RRGGBB'} for the program's elements: the roles the app resolves for its theme."""
    r = G.resolved_roles(e, PRESETS, PICKER)
    return {k: r[role] for k, role in PROGRAM_KEYS}
# the scene's flags (1002: five, left to right) in every state, so each crop shows each one
FLAG_STATES = {'editing': [0], 'selected': [1], 'invalid': [2], 'disabled': [3]}
# the chrome roles (roles.LIGHT_ROLES, whose names are the renderer's own; the field text has no surface on the crop's
# scene and rides along for the mocks that switch the dialog on; the card is off on the crop's scene)
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


def theme_for(e):
    """Entry e as a render.py theme over tablet.json: the app on the tablet, the entry's chrome (roles.light_roles)
    over the role mapping and its theme's program colours (program_colours), every option the app's."""
    t = json.load(open(os.path.join(PALETTE, 'themes', 'tablet.json')))
    t['name'] = e['key']
    t['description'] = (f'{e["name"]} ({e["family"]}) from docs/themes/catalog.json over tablet.json, '
                        f'the app on the tablet (tools/theme_catalog/crops.py)')
    col = t['colours']; r = roles.light_roles(e)
    for role in roles.LIGHT_ROLES: col[role] = r[role]
    col.update({'ruler_label': '@label', 'ruler_tick': '@bevel_shadow', 'flag_border': '@bevel_dkshadow',
                'ruler_tick_light': '@bevel_hilight', 'down_face': '@ground', 'trim_arrow': '@label'})
    col.update(program_colours(e))
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
    'warptempo': 'Warptempo: the program\'s own (`warptempo`, the architect\'s pick of the colour loop, and his '
                 'presets saved on the colour picker: chosen, not imported)',
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
         'family that imports nothing is the program\'s own, Warptempo: `warptempo`, CHOSEN, NOT IMPORTED (the architect\'s pick of the colour '
         'loop, 2026-10-03; its provenance is his ruling), and each `warptempo-preset-<n>`, his Preset <n> saved on '
         'the colour picker, chosen, not imported either (2026-10-04: the preset\'s chrome ground through the '
         'picker\'s chrome rule, the roles the picker shows fixed). The KEY is the name of the theme\'s file, '
         '`<key>.theme`, bundled with the app and copied into its `themes/` folder at every launch '
         '(`windows-95-standard` is the one built-in theme and takes no file), and what to type in Settings\' Theme '
         'row to pick it. Each crop is the app rendered in the theme (tools/palette in its tablet '
         'geometry: the tablet\'s 2304 x 1440 at gui_scale 275, every length derived from the app\'s own constants; '
         'cropped, never scaled; tools/theme_catalog/crops.py): the top strip in two halves over the well\'s bottom '
         'lines and the bottom row, transparent between them. The chrome is the theme\'s as recorded; the waveform pane, '
         'the flags and the playhead are the program\'s own elements in the theme\'s program colours as the app '
         'resolves them: each program role the theme\'s file names, else the built-in\'s (`windows-95-standard`: the '
         'lime waveform on black with its green outline, the warp flag purple #800080 and its selected face fuchsia '
         '#FF00FF, the phase-reset flag teal #008080 and its selected face aqua #00FFFF, the history\'s added flag '
         'green #008000 and its selected face lime #00FF00, the invalid flag maroon #800000 and its selected face red '
         '#FF0000, white labels on every face, the '
         'playhead\'s white stem); only the colour-picker presets name their own (the crops draw the playhead\'s head '
         'as the renderer\'s older grey head, not the app\'s WordPad ruler marker in the theme\'s chrome). The well '
         'keeps the app\'s two-line sunken edge (the theme\'s Shadow and DkShadow above, its 3DLight and Hilight below); '
         'the flags are the flat Acid flag (the scene\'s flags are warp markers, so the warp pair), the face with a '
         'one-px outline in the theme\'s DkShadow, the stem leaving the face across the bottom outline, shown left to '
         'right editing (the in-place editor: the selected face under a black frame, its text in the selected pair), '
         'selected (the selected face under the selected label, the stem with it), invalid (the removed pair), disabled '
         '(the ground, the label embossed) and unselected; the playhead\'s head carries a one-px outline in the '
         'theme\'s label; disabled words and glyphs are Windows\' emboss; the ruler label and the trim arrow are the '
         'theme\'s label, the ruler ticks its Shadow. The DISPLAY TIER is the smallest period colour set holding '
         'every colour the entry\'s roles use: vga (the 16 VGA colours), windows-20 (those and Windows\' four static '
         'extras #C0DCC0, #A6CAF0, #FFFBF0, #A0A0A4, always solid on a 256-colour display), else high-colour. Not imported: '
         'catalog.json\'s `not_imported`. Built by `tools/theme_catalog/` (fetch.py, build.py, crops.py; the theme '
         'files by gen_theme_files.py).', '']
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
