#!/usr/bin/env python3
# tools/theme_catalog/catalog_md.py — the built-in presets -> docs/themes/catalog.md, THE USER'S LIST OF THE CHROMES AND
# CAPTIONS (architect 2026-10-16: "caption and chrome options should not clutter install.md; they go in
# docs/themes/catalog.md and simply list the themes and their provenance, as well as small deviations … No colors or
# hex codes — just the titles; the user can type one in and try it out").
#
# THE PAGE: a short head (what a chrome and a caption are, how to choose one, the two defaults); then THE CHROMES, Cool
# Edit Pro 2.1's twenty presets in Cool Edit's own order, read from the committed cool_edit_presets.txt through
# gen_cool_edit_presets.py's own transcription (its head is the rule), each one line — its display name and its key;
# then THE CAPTIONS, every catalog.json entry of gen_theme_files.py's KEPT_FAMILIES in the catalog's order (the
# built-ins' own order), grouped by family, each one line — its display name (gen_theme_files.scheme_display_name)
# and its key. Each section and family carries its provenance once and its small deviations; the defaults are marked,
# read off the sources that own them: kGuiDefaultPaletteKey (src/gui/palette_file.h) and the chrome's own_scheme
# (src/gui/chrome_spec.h, kGuiDefaultSchemeKey's source). No colors, no hex, no ground order, no display tiers, no
# faces, no dates or rulings: catalog.json stays the RECORD of every entry's bytes and provenance, the KDE 3, CDE and
# GNOME 2 families among them, none of which this page lists.
#
# BYTE-STABLE: no timestamp, the sources' own orders, LF; run it twice and the page is identical. THE PAGE IS ITS
# OUTPUT, NEVER HAND-EDITED.
#
#   python3 -I tools/theme_catalog/catalog_md.py
import json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import gen_cool_edit_presets as cool_edit                                # noqa: E402
import gen_theme_files as schemes                                        # noqa: E402

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
MD = os.path.join(REPO, 'docs', 'themes', 'catalog.md')
PALETTE_FILE_H = os.path.join(REPO, 'src', 'gui', 'palette_file.h')
CHROME_SPEC_H = os.path.join(REPO, 'src', 'gui', 'chrome_spec.h')


def read_one(path, pattern, what):
    """The one match of `pattern`'s group 1 in `path`, else the run's hard fail naming `what`."""
    ms = re.findall(pattern, open(path).read())
    if len(ms) != 1: raise SystemExit(f'catalog_md: {what}: {len(ms)} matches in {os.path.relpath(path, REPO)}')
    return ms[0]


def default_chrome_key():
    return read_one(PALETTE_FILE_H, r'kGuiDefaultPaletteKey = "([a-z0-9-]+)";', 'kGuiDefaultPaletteKey')


def default_caption_key():
    return read_one(CHROME_SPEC_H, r'\.own_scheme\s*=\s*"([a-z0-9-]+)"', 'own_scheme')


def head(chrome_name, chrome_key, caption_name, caption_key):
    return [
        '# Chromes and captions', '',
        'The program\'s colors come in two built-in kinds, each chosen on its own.', '',
        '- A **chrome** is one of Cool Edit Pro 2.1\'s own color presets. It colors the waveform and its canvas, the '
        'grid and center lines, the markers, the playhead, and the Panel Face that the toolbar, the panels round the '
        'waveform, the menus, the dialogs and the cards are all drawn from.',
        '- A **caption** is a Windows color scheme of the period, of which the title bar\'s colors alone are used: the '
        'title, its gradient end and its text, and the same three for a window without the focus.', '',
        'To choose one, open the Settings menu and pick Chrome or Caption: the Settings line opens on the key in use. '
        'Type another key from the lists below (Tab completes a typed beginning) and press OK or Enter: it applies at '
        'once and is kept for the next launch. Cancel or Esc leaves the colors as they were. A word that is no '
        'preset\'s key is refused.', '',
        f'The defaults: the chrome {chrome_name} (`{chrome_key}`) and the caption {caption_name} (`{caption_key}`).',
        '']


CHROMES_PROVENANCE = (
    'Cool Edit Pro 2.1\'s twenty color presets, in Cool Edit\'s own order, as their text stands inside its program '
    'file (coolpro.exe). Two of them carry no name there; 3D and Easy on the Eyes are their names in Cool Edit\'s own '
    'registry entries.')
CHROMES_DEVIATIONS = [
    'Classic Cool records no Panel Face, so Cool Edit follows Windows\' button face for it: here it takes Default\'s '
    'Face, the program\'s default chrome.',
    'The waveform\'s outline is not one of Cool Edit\'s colors: each chrome\'s is its waveform color darkened, the '
    'same hue at a fixed lightness.',
    'The Invalid color, which marks an invalid marker, is each chrome\'s own center-line color.',
    'The scanner (the line that moves during playback) and the zoom\'s anchor line are white in every chrome, as Cool '
    'Edit\'s scanner is on every one of its presets.',
]


def chromes_section(default_key):
    ps = cool_edit.presets(cool_edit.SOURCE)
    keys = [cool_edit.preset_key(name) for name, _ in ps]
    if default_key not in keys: raise SystemExit(f'catalog_md: the default chrome {default_key} is no preset')
    L = ['## Chromes', '', CHROMES_PROVENANCE, '']
    L += [f'- {d}' for d in CHROMES_DEVIATIONS] + ['', '| Chrome | Key |', '|---|---|']
    for (name, _), key in zip(ps, keys):
        L.append(f'| {name}{" (the default)" if key == default_key else ""} | `{key}` |')
    return L + ['']


# Each kept family's heading, provenance and small deviations (the head): the family's word as the display names
# carry it, the source once, the notes.
CAPTION_FAMILIES = {
    'windows': (
        'Windows',
        'The Appearance schemes of Windows 95, 98, 2000 and Me. Most are Windows 2000\'s and Windows XP\'s classic '
        'schemes as ReactOS\'s setup data records them, checked against the same schemes saved from Windows XP\'s '
        'Display Properties as .theme files, which are also the source of Desert, Spruce and Windows 98 Standard; '
        'Windows 2000 Standard is Windows 2000\'s own setup data; Windows 95 Standard and the six Windows 95 schemes '
        'are the Windows 95 CD\'s own (shell2.inf).',
        [
            'The Windows 95 CD carries 27 Appearance schemes. Only the six whose colors differ from Windows 2000\'s of '
            'the same name are listed, as Windows 95 Maple, Wheat, Marine, Storm, Rose and Plum; the other 21 are '
            'equal to a scheme listed here (Windows Standard, Brick, Spruce, Teal, Red, White, and Blue, Pumpkin, '
            'Eggplant, Rainy Day, Desert, Lilac, Slate), a size variant of one, or a high-contrast scheme.',
            'Windows Me Standard is Windows 2000 Standard\'s colors: Windows Me changed only the font.',
            'ReactOS\'s own "ReactOS Standard" and "ReactOS Classic" are Windows 2000 Standard and Windows 98 Standard '
            'under other names, listed under those.',
            'Windows 98\'s own default scheme records no title-bar gradient; Windows 98 Standard\'s gradient is the '
            'one Windows 2000 and Windows XP record for the same scheme.',
            'A scheme that records no gradient (every Windows 95 one) draws its title bar in one flat color.',
        ]),
    'windows-plus': (
        'Windows 98 and Plus! desktop themes',
        'The color schemes of the desktop themes Windows 98 and Microsoft Plus! shipped, from their own .theme files.',
        [
            'The names leave out the themes\' color-depth tag, "(256 color)" or "(high color)".',
            'None of the themes records a title-bar gradient: their title bars are one flat color.',
        ]),
}


def captions_section(cat, default_key):
    es = [e for e in cat['entries'] if e['family'] in schemes.KEPT_FAMILIES]
    if default_key not in {e['key'] for e in es}: raise SystemExit(f'catalog_md: the default caption {default_key} is no scheme')
    L = ['## Captions', '',
         f'The {len(es)} captions are the Windows color schemes of the period, grouped by where they come from.', '']
    for fam in schemes.KEPT_FAMILIES:
        title, prov, notes = CAPTION_FAMILIES[fam]
        L += [f'### {title}', '', prov, ''] + [f'- {n}' for n in notes] + ['', '| Caption | Key |', '|---|---|']
        for e in (e for e in es if e['family'] == fam):
            mark = ' (the default)' if e['key'] == default_key else ''
            L.append(f'| {schemes.scheme_display_name(e)}{mark} | `{e["key"]}` |')
        L.append('')
    return L


def write_md(cat):
    chrome_key = default_chrome_key()
    caption_key = default_caption_key()
    chrome_name = next(name for name, _ in cool_edit.presets(cool_edit.SOURCE)
                       if cool_edit.preset_key(name) == chrome_key)
    caption_name = next(schemes.scheme_display_name(e) for e in cat['entries'] if e['key'] == caption_key)
    L = head(chrome_name, chrome_key, caption_name, caption_key)
    L += chromes_section(chrome_key)
    L += captions_section(cat, caption_key)
    while L and L[-1] == '': L.pop()
    with open(MD, 'w', newline='\n') as f: f.write('\n'.join(L) + '\n')


def main():
    cat = json.load(open(CATALOG))
    write_md(cat)
    print(f'wrote {os.path.relpath(MD, REPO)}')


if __name__ == '__main__':
    main()
