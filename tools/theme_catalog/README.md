# tools/theme_catalog — the built-in waveform presets: Cool Edit's color presets and the program's own

WHAT IS LEFT OF THE THEME CATALOG (architect 2026-10-21, "ok to delete other captions"; the hi-fi panel,
`src/gui/materials.h`): the imported desktop-theme catalog (`docs/themes/catalog.json`), its importers (`fetch.py`,
`build.py`, the `parse_*.py` readers, `roles.py`, `sources.py`, `toolkit_rules.py`), its checks and the generator of
the 42 built-in caption schemes (`gen_theme_files.py` -> `src/gui/chrome_schemes.inc`) were removed that day with the
captions they fed — the app paints no caption since 2026-10-24 (labwc draws the laptop's title bar; the panel is
the brushed black hi-fi ground, `src/gui/materials.h`'s grounds), and git history keeps the catalog, its provenance and its tools. What stays is the one road the product still reads: the waveform's built-in
presets — Cool Edit Pro 2.1's color presets and, since 2026-10-25, the program's own eight — and the user's list of
them. A standalone utility: no link path from any
product target, no CMake, Python 3's standard library alone.

```
python3 -I tools/theme_catalog/gen_cool_edit_presets.py [text [text]]  # cool_edit_presets.txt + warptempo_presets.txt -> src/gui/palette_presets.inc (committed)
python3 -I tools/theme_catalog/catalog_md.py                          # -> docs/themes/catalog.md (the user's list of the presets)
```

## Cool Edit's presets — the first twenty built-in waveform presets (architect 2026-10-09; the waveform's alone since 2026-10-21)

THE PROGRAM'S BUILT-IN PALETTES OPEN WITH COOL EDIT PRO 2.1'S COLOR PRESETS (architect 2026-10-09 ~13:30: "only Cool Edit Pro
palettes for waveform needed now"), and SINCE 2026-10-21 A PRESET SETS THE WAVEFORM'S COLORS ALONE (the `waveform`
device key, `chrome` until that day): the panel's Face is the hi-fi ground's mean once the ground is baked
(`src/gui/materials.h`'s grounds and TONE RULE, `src/gui/render.cpp`'s `panel_face`), so each preset's transcribed
Face stands in the table as a record no surface reads.
`cool_edit_presets.txt` is the source, committed: the scheme text inside `coolpro.exe` as extracted from his own copy
(its head is the provenance note), twenty schemes in Cool Edit's order, Cool Edit's "(Default Scheme)" first, every one
named — the two names the exe's text lacks, "3D" and "Easy on the Eyes", from Cool Edit's own registry entries written
under Wine (2026-10-09, the file's head).
`gen_cool_edit_presets.py` (standard library alone; the two sources' paths optional on its command line, Cool Edit's
first, the committed copies by default) transcribes each into the eleven program roles (`kGuiPaletteRoles`, `src/gui/palette_file.h`, read off the
header and checked) and writes `src/gui/palette_presets.inc`, the rows of `kGuiBuiltinPalettes` — never hand-edited.
Its head is the transcription's statement: WvBk, WvFg, GrdL, Cntr, CueM, RngM, Curs and Face to their roles (a scheme
without a Face, Classic Cool alone, takes the Default preset's Face, 626C7B, read off Default's own row, architect
2026-10-16), THE LIT OUTLINE the ink at HLS lightness 0.3157 (the view bar span's shadow rule,
`src/gui/cool_edit_derive.h`, ported operation for operation; `palette_file.cpp` proves every row against the C++
tone()), THE INVALID COLOR its Cntr too, the preset's own center line (architect 2026-10-11), and the one role Cool Edit
has no key for, the scanner, the same in every preset; the keys `cool-edit-<name>` (the `waveform` device key's
words), the display names Cool Edit's own ("Default" for the default); a values line with no name line after it is the
run's hard fail. Byte-stable: the presets in the file's order, the roles in the table's, uppercase hex, LF, no
timestamp.

## The program's own presets — warptempo_presets.txt (architect 2026-10-25)

`warptempo_presets.txt` is the second source, committed: the eight presets of the architect's freestyle round of
2026-10-25, designed by the planner on the hi-fi panel (its head is their design: a mid-light ink near .6 saturation,
the cue and range two separable families, no red but the invalid's), in Cool Edit's value grammar with WvFg, WvBk,
GrdL, Cntr, CueM, RngM, Curs and Face recorded for every one, each values line followed by its display name.
`gen_cool_edit_presets.py` reads it after Cool Edit's text and writes its rows after Cool Edit's twenty in the one
include, under the same transcription (the outline derived off the ink, the invalid color the center line, the scanner
white); the keys `warptempo-<name>` by the same key rule, checked unique against Cool Edit's.

## The user's list — docs/themes/catalog.md (architect 2026-10-16)

`catalog_md.py` writes `docs/themes/catalog.md`, THE USER'S LIST OF THE WAVEFORM PRESETS: a short head (what a preset
sets, how to choose one, the default read off `kGuiDefaultPaletteKey`), then the twenty presets in Cool Edit's order by
display name and key, read through `gen_cool_edit_presets.py`'s own transcription, the section's provenance once and
its small deviations, then a second section for the program's own eight, one sentence of what they are and their
names and keys; no colors, no hex, no dates or rulings (its head). Byte-stable; the page is its output, never
hand-edited. (Its caption section — the program's own Default and the 42 Windows and Plus! schemes — left with the
captions 2026-10-21.)
