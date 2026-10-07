#!/usr/bin/env python3
# tools/theme_catalog/catalog_md.py — docs/themes/catalog.json -> docs/themes/CATALOG.md, the page the architect
# chooses a theme from (architect 2026-10-03): per family its entries, darkest ground first, each with its key, name,
# provenance, display tier and crop.
#
# THE CROPS ARE FROZEN (2026-10-07): docs/themes/crops/<key>.png were rendered 2026-10-05 and 06 by the mock-up
# renderer, which retired on 2026-10-07 (the git history keeps it and the script that drove it); they are never
# re-rendered. This script only lists them: an entry with a committed crop shows it, an entry without one (an import
# since, `clearlooks`) says so, and a crop whose key has left the catalog is deleted.
#
#   python3 tools/theme_catalog/catalog_md.py
import json, os
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
CROPS = os.path.join(REPO, 'docs', 'themes', 'crops')
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
               'hand-recorded)',
    'windows-plus': 'Windows 98 / Plus! desktop themes (the shipped .theme files)',
    'kde3': 'KDE 3.5 colour schemes, as Trinity\'s tdebase carries them (relief by KDE 3\'s own rule at each scheme\'s contrast)',
    'cde': 'CDE palettes (colour set 5 the ground; foreground and shadows by Motif\'s own rule)',
    'gnome2': 'GNOME 2: Clearlooks as Debian 6 squeeze shipped it (its gtkrc\'s colour scheme; the relief, the tooltip '
              'border and the unfocused title by the engine\'s and metacity\'s own rules)',
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
    """catalog.json -> CATALOG.md: the head, then per family its entries darkest ground first, each with its crop when
    one is committed."""
    L = ['# The theme catalog', '',
         'Every entry below is a desktop theme of the era IMPORTED, not designed (architect 2026-10-03: "no derived, '
         'imported only"): its colours are the bytes its source records, each with its provenance in '
         '[catalog.json](catalog.json); where the source records only base colours and its own toolkit computed the '
         'relief at run time (KDE 3, CDE / Motif, GNOME 2\'s Clearlooks and metacity), that toolkit\'s rule ran once '
         'at import and is named. A theme is the CHROME\'s colors alone (2026-10-07): the program\'s own colors are '
         'its palettes, compiled into the app (src/gui/palette_file.h), and the program\'s own family of chosen '
         'entries left the catalog with them. The KEY is the name of the theme\'s file, '
         '`<key>.theme`, bundled with the app and copied into its `themes/` folder at every launch '
         '(`windows-2000-standard` is the one built-in theme and takes no file), and what to type in Settings\' Theme '
         'row to pick it. Each crop is the app as the retired mock-up renderer drew it on 2026-10-05 and 06 in the '
         'tablet\'s geometry (the tablet\'s 2304 x 1440 at gui_scale 275, cropped, never scaled: the top strip in two '
         'halves over the well\'s bottom lines and the bottom row, transparent between them), before the product\'s '
         'Windows 2000 pivot of 2026-10-06 evening, so the crops show the retired Windows 95 chrome in Nimbus Sans; '
         'that render road broke with the pivot and retired on 2026-10-07, the crops are frozen as last rendered, a '
         'theme\'s colours read true in them, and a theme imported since has no crop. The chrome is the theme\'s as '
         'recorded; the waveform pane, '
         'the flags and the playhead are the program\'s own elements in the colors of the crop\'s day (the lime '
         'waveform on black with its green outline, the warp flag purple #800080 and its selected face fuchsia '
         '#FF00FF, the invalid flag maroon #800000 and its selected face red #FF0000, white labels, the playhead\'s '
         'white stem; a theme names none of them since 2026-10-07: they are the app\'s palettes), its playhead\'s head '
         'the renderer\'s older grey head, not the app\'s WordPad ruler marker in the theme\'s chrome. The well '
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
         'catalog.json\'s `not_imported`. Built by `tools/theme_catalog/` (fetch.py, build.py, catalog_md.py; the theme '
         'files by gen_theme_files.py).', '']
    fams = []
    for e in cat['entries']:
        if e['family'] not in fams: fams.append(e['family'])
    for fam in fams:
        es = sorted((e for e in cat['entries'] if e['family'] == fam), key=lambda e: relative_luminance(unhex(e['roles']['ground'])))
        L += [f'## {FAMILY_HEAD[fam]}', '', f'{len(es)} entries, darkest ground first.', '']
        for e in es:
            imit = f" — imitates {e['imitates']}" if e.get('imitates') else ''
            crop = (f"![{e['key']}](crops/{e['key']}.png)" if os.path.exists(os.path.join(CROPS, e['key'] + '.png'))
                    else 'No crop: imported after the render road broke (above); its colours are its theme file\'s.')
            L += [f"### `{e['key']}`", '', f"**{e['name']}**{imit} · ground {e['roles']['ground']} · {prov_line(e)}", '',
                  f"Display tier: {e['display_tier']}", '', crop, '']
    open(MD, 'w').write('\n'.join(L) + '\n')


def main():
    cat = json.load(open(CATALOG))
    stale = sorted(set(os.listdir(CROPS)) - {e['key'] + '.png' for e in cat['entries']})
    for s in stale: os.remove(os.path.join(CROPS, s))
    write_md(cat)
    print(f'wrote {os.path.relpath(MD, REPO)} ({len(cat["entries"])} entries); deleted {len(stale)} stale crops'
          + (': ' + ', '.join(stale) if stale else ''))


if __name__ == '__main__':
    main()
