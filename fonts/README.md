# The product's faces

These two files are the product's face on both devices (architect 2026-10-06; the rules at `src/gui/gui_font.h`, the one face owner): Nimbus Sans, drawn at every scale at the em that matches each use's recorded vertical metrics (the numbers Windows 95 drew at 96 dpi, recorded in `gui_font.h` as `kGuiFaceMetrics`; the files' own line metrics are never read). The Linux build compiles both into `warptempo_gui`; the APK carries them as its assets. Neither device asks the system for a font.

| File | Face | Use |
|---|---|---|
| `NimbusSans-Regular.otf` | Nimbus Sans Regular (URW base35, OpenType CFF) | the body face (every row of the chrome, the time fields included) and the small face (the ruler's labels) |
| `NimbusSans-Bold.otf` | Nimbus Sans Bold (URW base35, OpenType CFF) | the window caption's title |

## Provenance and licences

**Nimbus Sans** (`NimbusSans-Regular.otf`, `NimbusSans-Bold.otf`): URW's Nimbus Sans, the Helvetica clone of the URW base35 set, (URW)++ 1999–2017, released by Artifex in 2017 under the GNU AGPL-3 with URW's font-embedding exception. The files are `fonts/NimbusSans-Regular.otf` and `fonts/NimbusSans-Bold.otf` of https://github.com/ArtifexSoftware/urw-base35-fonts at its `20200910` release tag, commit `c15105598aa7eb256b1ebfcecd3d078801521e73` (identical bytes at master's `3c0ba3b5687632dfc66526544a4e811fe0ec0cd9`, 2026-10-06). The repository's `LICENSE` (the AGPL-3 with the exception) and `COPYING` (the AGPL-3 text) travel beside them as `LICENSE-URW-base35.txt` and `COPYING-URW-base35.txt`. Chosen as the genre MS Sans Serif was drawn from (architect 2026-10-06). The app tracks it by 3/16 Windows px per glyph (`kGuiTrackingPx`, ruled 2026-10-06 to match MS Sans Serif's pixel fitting); the files themselves are unmodified. Its hhea ascent equals its "H" height, so it sits high in any program that seats text on a font's own line box; the product never does (the recorded metrics are the only vertical metric source, `src/gui/gui_font.h`).

A retune swaps a file; the ems are read off the files at the install (`src/gui/gui_font_bundled.cpp`), so only the comments that quote their numbers re-derive.
