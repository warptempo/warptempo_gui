# The materials

THE FORTEPIANO CHROME'S MATERIALS (architect 2026-10-21, after a Salzburg Walter fortepiano): ONE FLAT FOLDER, ONE
MASTER PER MATERIAL, NAMED BY ITS ROLE (`src/gui/materials.h`, `kGuiMaterialNames`, whose head owns every rule here):

- `Wood` — the walnut: the caption and the menu row, the toolbar band (its void between the panes the walnut under the
  luminance rule), the tab strip's ground, the dock bar, row 8, the border under it and the window's frame;
- `Metal` — the dark, even brass of the lock plate: the two tabs and the panel from the column's air to the dock bar
  (the view bar's, the ruler's and the marker lane's grounds, the margins and frames round the waveform, the foot); the
  menus, cards, dialogs and the keyboard are flat in its average color's tones;
- `Case` — the ebony of the naturals: every button case and the caption's three buttons.

Each is `<Name>.png` or `<Name>.jpg`; WHERE BOTH STAND THE PNG WINS (the icons' rule). Beside each master stand its two
PRE-RENDERED COVERS, `<Name>-2304x1440.png` (the tablet's window) and `<Name>-1920x1080.png` (the laptop's maximized
window). THIS FOLDER IS A DROP ROAD: drop a master under a material's name, then RENDER ITS COVERS before the build —

    python3 -I tools/tiles/render_covers.py

— and commit the master and the six covers together. Files of other names (this README) are not read. A material is
opaque (its alpha is not read); a 4K master is the intent.

THE COVER RULE: each master is scaled by one factor, max(W / w, H / h) for the window W x H, so it fills the window
exactly, centred, the overhang cropped evenly both sides — no tiling, no mirror, no repeat, ever (the tablet: the
5504 x 3072 master drawn at 2580 x 1440, 138 px cut each side). The covers are that rule run once with PIL's Lanczos,
the filter the mocks were judged in. THE MATCH (2026-10-21): for a window W x H the program takes THE SMALLEST COVER
AT LEAST W WIDE AND H TALL (by area; the exact size the smallest of all) and bakes ITS CENTRE CROP, the rows and
columns its overhang leaves evenly either side, with no filtering — the laptop's maximized 1920 x 1047 (labwc's panel
takes 33 rows) and a restored laptop window both from the 1920 x 1080 cover, the tablet from its 2304 x 1440. Only a
window larger than every cover in one dimension scales the master itself (the icons' Lanczos-3 area filter,
`svg_icon::raster_regions`); with no master there (the tablet) that is the bake's hard stop.

THE ROAD: the program reads the folder in place, by name — on the laptop from this folder (its path compiled in,
`CMakeLists.txt`'s `WARPTEMPO_BUNDLED_TILES_DIR`); on the tablet from the APK's `tiles/` assets, which carry the three
`-2304x1440` covers and not the masters (`android/app/build_apk.sh`). It checks at launch, off each file's first bytes,
that every material has a master or a cover, that every file is a PNG or a JPEG and that every cover is the size its
name says; then, per window geometry, it reads the one file each material's match chose and bakes the pixels each
material is drawn on, the three materials at once (`materials::ensure_settled`). A launch that misses a material, or
finds a file that is neither a PNG nor a JPEG, stops at startup with one line naming the file; on the tablet a missing
cover stops it the same way.

## Provenance and licence

Every master in this folder is drawn with Google's Gemini at the architect's prompt for this project, 2026-10-21, and
licensed under the GNU LGPL v3, the icon set's own licence (`assets/icons/README.md`, "Licence": each file a separate
data file read at run time, never linked or compiled into the program's code); the covers are renders of them under
the same licence. A JPEG or PNG carries no notice, so this README is their record. The settled three: `Wood` the first
set's darker walnut, `Metal` the darker step of the first even-sheet brass pair, `Case` the ebony run. The generation
records (the prompts, the sheets, every candidate) are the planner's `tmp/theme_next/gemini_out/`, not shipped.
