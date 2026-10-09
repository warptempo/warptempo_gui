#!/usr/bin/env python3
# tools/theme_catalog/catalog_md.py — docs/themes/catalog.json -> docs/themes/CATALOG.md, the page the architect
# chooses a theme from (architect 2026-10-03): per family its entries, darkest ground first, each with its key, name,
# provenance, display tier and crop.
#
# THE CROPS HAVE LEFT THE REPOSITORY (architect 2026-10-08, commit 4fba3952: docs/themes/crops/ deleted whole with their
# lines in CATALOG.md): they were rendered 2026-10-05 and 06 by the mock-up renderer, which retired on 2026-10-07, and
# were never re-rendered. The page keeps the head's account of them and an empty slot where an entry's crop was; the
# git history keeps the PNGs.
#
#   python3 tools/theme_catalog/catalog_md.py
import json, os
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
MD = os.path.join(REPO, 'docs', 'themes', 'CATALOG.md')


def unhex(s): return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


def relative_luminance(rgb):
    """The Rec. 709 weights over the sRGB-linearized channels of the bytes as given, each byte / 255: the families'
    sort key, darkest ground first."""
    def lin(c): return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(v / 255.0) for v in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


FAMILY_HEAD = {
    'windows': 'Windows: the Appearance schemes (ReactOS hivedef.inf, corroborated by the Windows XP classic schemes '
               'saved as .theme files; Windows 2000 Standard from Windows 2000\'s own setup hive; Windows 95 Standard '
               'and the six Windows 95 flavours from the Windows 95 CD\'s own shell2.inf)',
    'windows-plus': 'Windows 98 / Plus! desktop themes (the shipped .theme files)',
    'kde3': 'KDE 3.5 colour schemes, as Trinity\'s tdebase carries them (relief by KDE 3\'s own rule at each scheme\'s contrast)',
    'cde': 'CDE palettes (colour set 5 the ground; foreground and shadows by Motif\'s own rule)',
    'gnome2': 'GNOME 2: Clearlooks as Debian 6 squeeze shipped it (its gtkrc\'s colour scheme; the relief, the tooltip '
              'border and the unfocused title by the engine\'s and metacity\'s own rules)',
}


# A family's one-paragraph note under its count line.
FAMILY_NOTE = {
    'windows': 'The Windows 95 CD carries 27 Appearance schemes (shell2.inf); only the six whose bytes differ from '
               'Windows 2000\'s of the same name are entries here, under a `windows-95-` prefix — Maple, Wheat, Marine, '
               'Storm, Rose and Plum — the other 21 being equal to an entry (Windows Standard, Brick, Spruce, Teal, Red, '
               'White, and Blue, Pumpkin, Eggplant, Rainy Day, Desert, Lilac, Slate), a size variant of one, or a '
               'usability scheme (`not_imported.windows_95_cd`, catalog.json).',
}


def prov_line(e):
    p = e['provenance']; s = p['sources'][0]
    where = s.get('repository') or s.get('image') or s.get('project')
    ref = (s.get('commit') or '')[:8]
    line = f"{where}{'@' + ref if ref else ''} `{s['file']}`" if 'file' in s else where
    if len(p['sources']) > 1: line += f" + {len(p['sources']) - 1} more"
    if 'rule' in p: line += f"; {p['rule']['id']} rule" + (f" at contrast {p['rule']['contrast']}" if 'contrast' in p['rule'] else '')
    return line


def write_md(cat):
    """catalog.json -> CATALOG.md: the head, then per family its entries darkest ground first (the windows family's
    note on the Windows 95 CD after its heading)."""
    L = ['# The theme catalog', '',
         'Every entry below is a desktop theme of the era IMPORTED, not designed (architect 2026-10-03: "no derived, '
         'imported only"): its colours are the bytes its source records, each with its provenance in '
         '[catalog.json](catalog.json); where the source records only base colours and its own toolkit computed the '
         'relief at run time (KDE 3, CDE / Motif, GNOME 2\'s Clearlooks and metacity), that toolkit\'s rule ran once '
         'at import and is named. A theme is the CHROME\'s colors alone (2026-10-07): the program\'s own colors are '
         'its palettes, compiled into the app (src/gui/palette_file.h), and the program\'s own family of chosen '
         'entries left the catalog with them. THE CATALOG IS A RECORD, NOT A SHIPPED SET (2026-10-08): no theme '
         'ships as a file and none is chosen in the app; each chrome\'s colors are compiled in, and the catalog is '
         'their source — `windows-2000-standard` the windows-2000 chrome\'s, `clearlooks` the clearlooks chrome\'s '
         '(tools/theme_catalog/gen_theme_files.py, src/gui/theme_file.h) — and the record a look made official as a '
         'new chrome variant is drawn from. Each entry once had a crop, the app as the retired mock-up renderer drew it on 2026-10-05 and 06 in the '
         'tablet\'s geometry (the tablet\'s 2304 x 1440 at gui_scale 275, cropped, never scaled: the top strip in two '
         'halves over the well\'s bottom lines and the bottom row, transparent between them), before the product\'s '
         'Windows 2000 pivot of 2026-10-06 evening, so the crops showed the retired Windows 95 chrome in Nimbus Sans; '
         'that render road broke with the pivot and retired on 2026-10-07, the crops were frozen as last rendered (a '
         'theme\'s colours read true in them, and a theme imported since had none), and they left the repository on '
         '2026-10-08 (the git history keeps them). The chrome was the theme\'s as '
         'recorded; the waveform pane, '
         'the flags and the playhead were the program\'s own elements in the colors of the crop\'s day (the lime '
         'waveform on black with its green outline, the warp flag purple #800080 and its selected face fuchsia '
         '#FF00FF, the invalid flag maroon #800000 and its selected face red #FF0000, white labels, the playhead\'s '
         'white stem; a theme names none of them since 2026-10-07: they are the app\'s palettes), its playhead\'s head '
         'was the renderer\'s older grey head, not the app\'s WordPad ruler marker in the theme\'s chrome. The well '
         'kept the app\'s two-line sunken edge (the theme\'s Shadow and DkShadow above, its 3DLight and Hilight below); '
         'the flags were the flat Acid flag (the scene\'s flags were warp markers, so the warp pair), the face with a '
         'one-px outline in the theme\'s DkShadow, the stem leaving the face across the bottom outline, shown left to '
         'right editing (the in-place editor: the selected face under a black frame, its text in the selected pair), '
         'selected (the selected face under the selected label, the stem with it), invalid (the removed pair), disabled '
         '(the ground, the label embossed) and unselected; the playhead\'s head carried a one-px outline in the '
         'theme\'s label; disabled words and glyphs were Windows\' emboss; the ruler label and the trim arrow were the '
         'theme\'s label, the ruler ticks its Shadow. The DISPLAY TIER is the smallest period colour set holding '
         'every colour the entry\'s roles use: vga (the 16 VGA colours), windows-20 (those and Windows\' four static '
         'extras #C0DCC0, #A6CAF0, #FFFBF0, #A0A0A4, always solid on a 256-colour display), else high-colour. Not imported: '
         'catalog.json\'s `not_imported`. Built by `tools/theme_catalog/` (fetch.py, build.py, catalog_md.py; the theme '
         'files by gen_theme_files.py).', '']
    fams = []
    for e in cat['entries']:
        if e['family'] not in fams: fams.append(e['family'])
    for fam in fams:
        es = sorted((e for e in cat['entries'] if e['family'] == fam), key=lambda e: relative_luminance(unhex(e['roles']['ground'])))
        L += [f'## {FAMILY_HEAD[fam]}', '', f'{len(es)} entries, darkest ground first.', '']
        if fam in FAMILY_NOTE: L += [FAMILY_NOTE[fam], '']
        for e in es:
            imit = f" — imitates {e['imitates']}" if e.get('imitates') else ''
            L += [f"### `{e['key']}`", '', f"**{e['name']}**{imit} · ground {e['roles']['ground']} · {prov_line(e)}", '',
                  f"Display tier: {e['display_tier']}", '', '']
    open(MD, 'w').write('\n'.join(L) + '\n')


def main():
    cat = json.load(open(CATALOG))
    write_md(cat)
    print(f'wrote {os.path.relpath(MD, REPO)} ({len(cat["entries"])} entries)')


if __name__ == '__main__':
    main()
