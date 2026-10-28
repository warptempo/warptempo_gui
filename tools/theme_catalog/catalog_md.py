#!/usr/bin/env python3
# tools/theme_catalog/catalog_md.py — the built-in presets -> docs/themes/catalog.md, THE USER'S LIST OF THE WAVEFORM
# PRESETS (architect 2026-10-16: "caption and chrome options should not clutter install.md; they go in
# docs/themes/catalog.md and simply list the themes and their provenance, as well as small deviations … No colors or
# hex codes — just the titles; the user can type one in and try it out").
#
# THE PAGE: a short head (what a waveform preset is, how to choose one, the default); then THE WAVEFORM PRESETS in one
# section, read through gen_presets.py's own transcription (its head is the rule; all_presets, the include's rows in
# its order): the program's presets (warptempo_presets.txt), one sentence of what they are and the rules the program
# applies to every one; each preset one line — its display name and its key — and the default marked, read off the
# source that owns it: kGuiDefaultPaletteKey (src/gui/palette_file.h). No colors, no hex, no dates or rulings.
#
# BYTE-STABLE: no timestamp, the source's own order, LF; run it twice and the page is identical. THE PAGE IS ITS
# OUTPUT, NEVER HAND-EDITED.
#
#   python3 -I tools/theme_catalog/catalog_md.py
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import gen_presets                                                       # noqa: E402

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
        'A **waveform preset** is one of the program\'s built-in color presets, listed below. It colors the waveform '
        'and its canvas, the grid and center lines, the markers, the playhead and the scanner, and the keys\' lit lamps '
        'take the hue of its waveform color; the rest of the window is the program\'s black hi-fi panel, which no preset '
        'changes.', '',
        'To choose one, press the toolbar\'s Settings button and tap the `waveform` line: the presets are listed by '
        'name, the one in use chosen; a tap on one applies it at once and keeps it for the next launch, and Cancel or '
        'Esc leaves the colors as they were. A preset\'s key, in the list below, is what the device config\'s '
        '`waveform=` line holds.', '',
        f'The default: {default_name} (`{default_key}`).',
        '']


PRESETS_PROVENANCE = (
    'The color presets of Cool Edit Pro 2.1, the program the waveform\'s look comes from: its twenty presets under '
    'its own names, each as it is, listed alphabetically.')
PRESETS_RULES = [
    'The waveform\'s outline is each preset\'s waveform color darkened, the same hue at a fixed lightness (the '
    'lightness of Cool Edit Pro 2.1\'s view bar shadows).',
    'The scanner (the line that moves during playback) and the zoom\'s anchor line are white in every preset, as Cool '
    'Edit\'s scanner is on every one of its presets.',
]


def preset_table(rows, default_key):
    L = ['| Preset | Key |', '|---|---|']
    for key, name, _ in rows:
        L.append(f'| {name}{" (the default)" if key == default_key else ""} | `{key}` |')
    return L


def presets_section(default_key):
    ps = gen_presets.all_presets()
    if default_key not in [k for k, _, _ in ps]:
        raise SystemExit(f'catalog_md: the default preset {default_key} is no preset')
    L = ['## Presets', '', PRESETS_PROVENANCE, '']
    L += [f'- {d}' for d in PRESETS_RULES] + [''] + preset_table(ps, default_key)
    return L + ['']


def write_md():
    default_key = default_preset_key()
    default_name = next((name for key, name, _ in gen_presets.all_presets() if key == default_key), None)
    if default_name is None: raise SystemExit(f'catalog_md: the default preset {default_key} is no preset')
    L = head(default_name, default_key)
    L += presets_section(default_key)
    while L and L[-1] == '': L.pop()
    with open(MD, 'w', newline='\n') as f: f.write('\n'.join(L) + '\n')


def main():
    write_md()
    print(f'wrote {os.path.relpath(MD, REPO)}')


if __name__ == '__main__':
    main()
