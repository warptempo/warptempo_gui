# tools/theme_catalog — Cool Edit's color presets, the built-in waveform palettes

WHAT IS LEFT OF THE THEME CATALOG (architect 2026-10-20 ~17:00, "we would at that point remove all other themes — both
chrome and caption"; ~20:15, "until I decide which preset to use for the waveform, maybe we should leave that available
but it would only affect the waveform"): the imported desktop-theme catalog (`docs/themes/catalog.json` and the user's
list `catalog.md`), its importers, its checks and the generator of the 42 built-in caption schemes
(`src/gui/chrome_schemes.inc`) were removed that day — the caption is Windows Storm's, compiled in
(`src/gui/palette_file.h`'s `kGuiCaptionOwn`), and git history keeps the catalog, its provenance and its tools. What
stays is the one road still read by the product: Cool Edit Pro 2.1's color presets, the waveform's built-in palettes.
A standalone utility: no link path from any product target, no CMake, Python 3's standard library alone.

```
python3 -I tools/theme_catalog/gen_cool_edit_presets.py [text]  # cool_edit_presets.txt -> src/gui/palette_presets.inc (committed)
```

## Cool Edit's presets — the built-in waveform palettes (architect 2026-10-09; the waveform's alone since 2026-10-20)

THE PROGRAM'S BUILT-IN PALETTES ARE COOL EDIT PRO 2.1'S COLOR PRESETS (architect 2026-10-09 ~13:30: "only Cool Edit Pro
palettes for waveform needed now"), and SINCE 2026-10-20 A PRESET SETS THE WAVEFORM'S COLORS ALONE: the panel's Face —
the eleventh role until then, through which a preset colored the whole window — is the product's one constant,
Default's 626C7B (`src/gui/cool_edit_derive.h`'s `kDefaultFace`), and is no longer transcribed.
`cool_edit_presets.txt` is the source, committed: the scheme text inside `coolpro.exe` as extracted from his own copy
(its head is the provenance note), twenty schemes in Cool Edit's order, Cool Edit's "(Default Scheme)" first, every one
named — the two names the exe's text lacks, "3D" and "Easy on the Eyes", from Cool Edit's own registry entries written
under Wine (2026-10-09, the file's head).
`gen_cool_edit_presets.py` (standard library alone; the source's path optional on its command line, the committed copy
by default) transcribes each into the ten waveform roles (`kGuiPaletteRoles`, `src/gui/palette_file.h`, read off the
header and checked) and writes `src/gui/palette_presets.inc`, the rows of `kGuiBuiltinPalettes` — never hand-edited.
Its head is the transcription's statement: WvBk, WvFg, GrdL, Cntr, CueM, RngM and Curs to their roles, THE LIT OUTLINE
the ink at HLS lightness 0.3157 (the view bar span's shadow rule, `src/gui/cool_edit_derive.h`, ported operation for
operation; `palette_file.cpp` proves every row against the C++ tone()), THE INVALID COLOR its Cntr too, the preset's
own center line (architect 2026-10-11), and the one role Cool Edit has no key for, the scanner, the same in every
preset; the keys `cool-edit-<name>` (the `waveform` device key's words), the display names Cool Edit's own ("Default"
for the default); a values line with no name line after it is the run's hard fail. Byte-stable: the presets in the
file's order, the roles in the table's, uppercase hex, LF, no timestamp.
