# The product's faces

These five files are the product's faces on both devices (architect 2026-10-05; the rules at `src/gui/gui_font.h`, the one face owner). Three are the PERIOD BITMAP FACES, one strike of 1-bit pixels each, authored in Windows px: they are drawn pixel for pixel (each strike pixel a k x k block) wherever gui_scale is a whole multiple of 100, and they are the ONLY source of vertical metrics at every scale. Two are the FALLBACK, Nimbus Sans (architect 2026-10-06), drawn at every other scale at the em that matches each strike vertically. The Linux build compiles all five into `warptempo_gui`; the APK carries them as its assets. Neither device asks the system for a font.

| File | Face | Use |
|---|---|---|
| `crox1h.otb` | Cronyx "Helvetica" Regular, the 5x13 strike (ppem 11, ascent 11, descent 2, caps and digits 9 rows) | the body face: every row of the chrome, the time fields included |
| `crox1hb.otb` | Cronyx "Helvetica" Bold, the same strike | the window caption's title |
| `small_fonts_digits.otb` | the reconstructed Small Fonts digits (0-9 . : , a 7-row cell all above the baseline; digits advance 5, '.' and ':' 2) | the ruler's labels |
| `NimbusSans-Regular.otf` | Nimbus Sans Regular (URW base35, OpenType CFF) | the fallback for the body and the small face |
| `NimbusSans-Bold.otf` | Nimbus Sans Bold (URW base35, OpenType CFF) | the fallback for the caption's title |

## Provenance and licences

**Cronyx Helvetica** (`crox1h.otb`, `crox1hb.otb`): the architect's upload of X.Org's Cronyx Cyrillic set (font-cronyx-cyrillic) in OTB form, whose Latin is a pixel trace of MS Sans Serif 8 pt — MS Sans Serif's 13-px cell at 96 dpi; 168 glyphs, printable ASCII and the full KOI8 Cyrillic. Bundled on his ruling (2026-10-05). Their name table reads "Copyright (C) 1990, 1991 EWT Consulting, Portions Copyright (C) 1994 Cronyx Ltd., Portions Copyright (C) 1996-1997 by Andrey A. Chernov, Moscow, Russia." Their licence is the X-style notice of font-cronyx-cyrillic, which travels beside them as `COPYING-Cronyx.txt`, copied from the xfonts-cyrillic package's copyright file (`/usr/share/doc/xfonts-cyrillic/copyright`, the font-cronyx-cyrillic section).

**The Small Fonts digits** (`small_fonts_digits.otb`): reconstructed pixel-exact from Windows' Small Fonts as Sonic Foundry ACID Pro 3.0 drew its level meter and ruler and Vegas Audio its ruler, read off the architect's own screenshots (2026-10-05). Only the characters the ruler's labels use are recorded. GENERATED — regenerate, never hand-edit: the glyph table and its provenance are `tools/small_fonts/glyphs.py`, the generator `tools/small_fonts/gen_small_fonts.py`.

**Nimbus Sans** (`NimbusSans-Regular.otf`, `NimbusSans-Bold.otf`): URW's Nimbus Sans, the Helvetica clone of the URW base35 set, (URW)++ 1999–2017, released by Artifex in 2017 under the GNU AGPL-3 with URW's font-embedding exception. The files are `fonts/NimbusSans-Regular.otf` and `fonts/NimbusSans-Bold.otf` of https://github.com/ArtifexSoftware/urw-base35-fonts at its `20200910` release tag, commit `c15105598aa7eb256b1ebfcecd3d078801521e73` (identical bytes at master's `3c0ba3b5687632dfc66526544a4e811fe0ec0cd9`, 2026-10-06). The repository's `LICENSE` (the AGPL-3 with the exception) and `COPYING` (the AGPL-3 text) travel beside them as `LICENSE-URW-base35.txt` and `COPYING-URW-base35.txt`. Chosen as the genre MS Sans Serif was drawn from (architect 2026-10-06). Its hhea ascent equals its "H" height, so it sits high in any program that seats text on a font's own line box; the product never does (the strikes are the only vertical metric source, `src/gui/gui_font.h`).

A retune swaps a file; the strikes' metrics and the fallback's ems are read off the files at the install (`src/gui/gui_font_bundled.cpp`), so only the comments that quote their numbers re-derive.
