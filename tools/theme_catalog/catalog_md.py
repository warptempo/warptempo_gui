#!/usr/bin/env python3
# tools/theme_catalog/catalog_md.py — the built-in presets -> docs/themes/catalog.md, THE USER'S LIST OF THE WAVEFORM
# PRESETS (architect 2026-10-16: "caption and chrome options should not clutter install.md; they go in
# docs/themes/catalog.md and simply list the themes and their provenance, as well as small deviations … No colors or
# hex codes — just the titles; the user can type one in and try it out").
#
# THE PAGE: a short head (what a waveform preset is, how to choose one, the default); then THE WAVEFORM PRESETS in two
# sections, both read through gen_cool_edit_presets.py's own transcription (its head is the rule; all_presets, the
# include's rows in its order): Cool Edit Pro 2.1's twenty in Cool Edit's own order (cool_edit_presets.txt), with the
# section's provenance once and its small deviations, then the program's own eight (warptempo_presets.txt, architect
# 2026-10-25), one sentence of what they are; each preset one line — its display name and its key — and the default
# marked, read off the source that owns it: kGuiDefaultPaletteKey (src/gui/palette_file.h). No colors, no hex, no
# dates or rulings. (THE CAPTIONS — the
# program's own Default and the 42 Windows and Plus! schemes read from the imported theme catalog — were the page's
# second section until they left the program 2026-10-21, the catalog and its importers with them; the presets were
# called chromes until the same day.)
#
# BYTE-STABLE: no timestamp, the source's own order, LF; run it twice and the page is identical. THE PAGE IS ITS
# OUTPUT, NEVER HAND-EDITED.
#
#   python3 -I tools/theme_catalog/catalog_md.py
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import gen_cool_edit_presets as cool_edit                                # noqa: E402

MD = os.path.join(REPO, 'docs', 'themes', 'catalog.md')
PALETTE_FILE_H = os.path.join(REPO, 'src', 'gui', 'palette_file.h')


def read_one(path, pattern, what):
    """The one match of `pattern`'s group 1 in `path`, else the run's hard fail naming `what`."""
    ms = re.findall(pattern, open(path).read())
    if len(ms) != 1: raise SystemExit(f'catalog_md: {what}: {len(ms)} matches in {os.path.relpath(path, REPO)}')
    return ms[0]


def default_preset_key():
    return read_one(PALETTE_FILE_H, r'kGuiDefaultPaletteKey = "([a-z0-9-]+)";', 'kGuiDefaultPaletteKey')


def head(default_name, default_key):
    return [
        '# Waveform presets', '',
        'A **waveform preset** is one of the program\'s twenty-eight built-in color presets: Cool Edit Pro 2.1\'s own '
        'twenty and the program\'s own eight. It colors the waveform and its canvas, the grid and center lines, the '
        'markers, the playhead and the scanner, and the keys\' lit lamps take the hue of its waveform color; the rest of '
        'the window is the program\'s black hi-fi panel, which no preset changes.', '',
        'To choose one, press the toolbar\'s Settings button and type `waveform=` and a key from the lists below (Tab '
        'completes a typed beginning, and a Tab right after `waveform=` adds the key in use), then press OK or Enter: it '
        'applies at once and is kept for the next launch. Cancel or Esc leaves the colors as they were. A word that is '
        'no preset\'s key is refused.', '',
        f'The default: {default_name} (`{default_key}`).',
        '']


PRESETS_PROVENANCE = (
    'Cool Edit Pro 2.1\'s twenty color presets, in Cool Edit\'s own order, as their text stands inside its program '
    'file (coolpro.exe). Two of them carry no name there; 3D and Easy on the Eyes are their names in Cool Edit\'s own '
    'registry entries.')
PRESETS_DEVIATIONS = [
    'Each preset\'s Panel Face, Cool Edit\'s color for its panels, is not used: the program\'s panel is its black '
    'hi-fi ground, and no preset\'s Panel Face is read.',
    'The waveform\'s outline is not one of Cool Edit\'s colors: each preset\'s is its waveform color darkened, the '
    'same hue at a fixed lightness.',
    'The Invalid color, which marks an invalid marker, is each preset\'s own center-line color.',
    'The scanner (the line that moves during playback) and the zoom\'s anchor line are white in every preset, as Cool '
    'Edit\'s scanner is on every one of its presets.',
]


WARPTEMPO_PROVENANCE = (
    'Eight presets of the program\'s own, made for its black panel: a softer waveform color than Cool Edit\'s and two '
    'marker colors easy to tell apart, the outline, the Invalid color (a red, their center line\'s) and the scanner '
    'following the same rules as Cool Edit\'s above.')


def preset_table(rows, default_key):
    L = ['| Preset | Key |', '|---|---|']
    for key, name, _ in rows:
        L.append(f'| {name}{" (the default)" if key == default_key else ""} | `{key}` |')
    return L


def presets_sections(default_key):
    ps = cool_edit.all_presets()
    if default_key not in [k for k, _, _ in ps]:
        raise SystemExit(f'catalog_md: the default preset {default_key} is no preset')
    cool = [p for p in ps if p[0].startswith(cool_edit.KEY_PREFIX)]
    ours = [p for p in ps if p[0].startswith(cool_edit.WARPTEMPO_KEY_PREFIX)]
    if len(cool) + len(ours) != len(ps): raise SystemExit('catalog_md: a preset key of neither source\'s prefix')
    L = ['## Presets', '', PRESETS_PROVENANCE, '']
    L += [f'- {d}' for d in PRESETS_DEVIATIONS] + [''] + preset_table(cool, default_key) + ['']
    L += ['## The program\'s own presets', '', WARPTEMPO_PROVENANCE, ''] + preset_table(ours, default_key)
    return L + ['']


def write_md():
    default_key = default_preset_key()
    default_name = next((name for key, name, _ in cool_edit.all_presets() if key == default_key), None)
    if default_name is None: raise SystemExit(f'catalog_md: the default preset {default_key} is no preset')
    L = head(default_name, default_key)
    L += presets_sections(default_key)
    while L and L[-1] == '': L.pop()
    with open(MD, 'w', newline='\n') as f: f.write('\n'.join(L) + '\n')


def main():
    write_md()
    print(f'wrote {os.path.relpath(MD, REPO)}')


if __name__ == '__main__':
    main()
