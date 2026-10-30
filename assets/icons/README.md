# The icons

The drawings the program paints, one SVG per `icons::Icon` enumerator (`src/gui/icons.h`), each named by its
enumerator. The keys print their names and draw no glyph, so these four drawings are the only icons on screen. The
program reads the files at launch, in place, through resvg (`src/gui/svg_icon.h`): on the laptop from this folder, on
the tablet from the APK's `icons/` assets (`apk/build_apk.sh`, which copies each link as the file it names). A
missing file, or one that is not well-formed SVG, fails the launch, naming the file. Files of other names here (this
README, the licence text) are not read.

## The drawings

Each is the project's own, drawn in the bone F0F0F0 on a 34 x 40-unit viewBox with its ink on rows 7..34 — a BOXED
GLYPH, placed by its box (`src/gui/icons.h`, THE BOXED GLYPHS).

| File | Drawing | Where it shows |
|---|---|---|
| `Quaver.svg` | the quaver: Gemini Argon's one-path conversion of a bitmap the planner drew, at the architect's prompt | the logo lane's note; the wav rows (`WavRow.svg`); the program icon (`AppIcon.svg`) |
| `FolderRow.svg` | the folder, a square with a tab at the top, drawn by the planner | the folder and project rows |
| `CardNormal.svg` | the i, a small rectangle over a larger one, drawn by the planner | a normal card's glyph |
| `CardCritical.svg` | the X, two crossed strokes, drawn by the planner | a critical card's glyph |

## The symlinks

A drawing with two uses is one file and a relative symlink in this folder:

- `WavRow.svg` -> `Quaver.svg` (the wav rows wear the quaver);
- `AppIcon.svg` -> `Quaver.svg` (the program icon outside the window: the Android launcher icon is rendered from it at
  every APK build, `apk/build_apk.sh`; the Linux icon is `packaging/warptempo_gui.svg`, a link to
  `Quaver.svg`, `docs/INSTALL.md`).

## Licence

The four drawings are licensed under the GNU Lesser General Public License v3 (`LICENSE-LGPL-3.txt`, which supplements
the GNU GPL v3 in the repository's `LICENSE`). They are data, read at run time and never linked or compiled into the
program's code; the repository distributes source only (`THIRD_PARTY.md`).
