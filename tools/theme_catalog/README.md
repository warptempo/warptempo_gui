# tools/theme_catalog — the built-in waveform presets and the user's list of them

What is left of the theme catalog (architect 2026-10-21; the imported desktop-theme catalog, its importers and the
caption schemes left that day with the captions, git history keeping them) is the one road the product still reads:
the waveform's built-in presets and the user's list of them. A standalone utility: no link path from any product
target, no CMake, Python 3's standard library alone.

```
python3 -I tools/theme_catalog/gen_presets.py [text]   # warptempo_presets.txt -> src/gui/palette_presets.inc (committed)
python3 -I tools/theme_catalog/catalog_md.py           # -> docs/themes/catalog.md (the user's list of the presets)
```

## The presets — warptempo_presets.txt (architect 2026-10-26)

`warptempo_presets.txt` is THE ONE SOURCE, committed: the program's forty presets, Cool Edit Pro 2.1's twenty color
presets in its own order, each as it is and then with the Terminal ink (the DEC VT220's green and its grid under the
theme's every other color) — "the point is to get closer to Cool Edit again. Also show two versions of each CEP theme"
(its head is their provenance, the pairing and the invalid color's rule), each values line followed by its display
name, in Cool Edit Pro 2.1's value grammar.

`gen_presets.py` transcribes each into the eleven program roles (`kGuiPaletteRoles`, `src/gui/palette_file.h`, read
off the header and checked) and writes `src/gui/palette_presets.inc`, the rows of `kGuiBuiltinPalettes` — never
hand-edited. Its head is the transcription's statement: WvBk, WvFg, GrdL, Cntr, CueM, RngM, Curs and Face to their
roles, and THE INVALID COLOR the preset's own Inv, the one key of the program's own (architect 2026-10-25 night; on
the forty the row's own center line, 2026-10-26); THE LIT OUTLINE the ink at HLS
lightness 0.3157, the lightness measured on Cool Edit Pro 2.1's view bar span shadows across five of its presets
(2026-10-09; `src/gui/cool_edit_derive.h`'s `span_shadow`, ported operation for operation, `palette_file.cpp` proving
every row against the C++ tone()); the scanner white in every preset. The keys `warptempo-<name>` (the `waveform`
device key's words). Byte-stable: the presets in the file's order, the roles in the table's, uppercase hex, LF, no
timestamp.

## The user's list — docs/themes/catalog.md (architect 2026-10-16)

`catalog_md.py` writes `docs/themes/catalog.md`: a short head (what a preset sets, how to choose one, the default read
off `kGuiDefaultPaletteKey`), then one section, the presets by display name and key in the source's order, read
through `gen_presets.py`'s own transcription, with one sentence of what they are and the rules applied to every one;
no colors, no hex, no dates or rulings. Byte-stable; the page is its output, never hand-edited.
