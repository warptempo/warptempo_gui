# The materials

THE FORTEPIANO CHROME'S MATERIALS (architect 2026-10-21, after a Salzburg Walter fortepiano): ONE FLAT FOLDER, ONE
MASTER PER MATERIAL, NAMED BY ITS ROLE (`src/gui/materials.h`, `kGuiMaterialNames`, whose head owns every rule here):

- `Wood` — the walnut: the toolbar band (its void between the panes the walnut under the luminance rule), the tab
  strip's ground, the dock bar, row 8 and the border under it (the caption, walnut on 2026-10-21, is flat ebony since
  2026-10-22, a constant in `src/gui/render.h`'s caption block);
- `Metal` — the dark, even brass of the lock plate: the two tabs and the panel from the column's air to the dock bar
  (the view bar's, the ruler's and the marker lane's grounds, the margins and frames round the waveform, the foot); the
  menus, cards, dialogs and the keyboard are flat in its average color's tones;
- `Case` — the ebony of the naturals: every button case (the caption's three buttons, ebony on 2026-10-21, are Windows
  2000's own since 2026-10-22).

Each is `<Name>.png` or `<Name>.jpg`; WHERE BOTH STAND THE PNG WINS (the icons' rule). Beside each master stand its two
PRE-RENDERED COVERS, `<Name>-2304x1440.png` (the tablet's window) and `<Name>-1920x1080.png` (the laptop's maximized
window). THIS FOLDER IS A DROP ROAD: drop a master under a material's name, then RENDER ITS COVERS before the build —

    python3 -I tools/tiles/render_covers.py

— and commit the master and the six covers together. Files of other names (this README) are not read. A material is
opaque (its alpha is not read); a 4K master is the intent.

THE COVER RULE: each master is scaled by one factor, max(W / w, H / h) for the window W x H, so it fills the window
exactly, centered, the overhang cropped evenly both sides — no tiling, no mirror, no repeat, ever (the tablet: the
5504 x 3072 master drawn at 2580 x 1440, 138 px cut each side). The covers are that rule run once with PIL's Lanczos,
the filter the mocks were judged in. THE MATCH (2026-10-21): for a window W x H the program takes THE SMALLEST COVER
AT LEAST W WIDE AND H TALL (by area; the exact size the smallest of all) and bakes ITS CENTER CROP, the rows and
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
cover stops it the same way. THE TUNING ROAD (2026-10-22): a folder `tiles/` beside the device config (on the tablet
under `adb shell run-as com.warptempo.gui`) is read first, its files winning over this folder's by name under the same
rules — retune a material by dropping the cover its window matches there, and nothing happens while the folder is
absent.

## Provenance and licence

Every master in this folder is drawn at the architect's prompt for this project — `Case` with Google's Gemini,
2026-10-21; `Metal` with Gemini from a brass tile, 2026-10-22 (his pick off six live candidates, "the one most realistic to
what a music instrument would look like"; the darker even sheet of 2026-10-21 until then); `Wood` with OpenAI's ChatGPT from the Walter photos and upscaled to 5504 x 3072 by Gemini,
2026-10-22 (architect: "the most realistic … I'm OK with red if it's realistic"; the first Gemini walnut, 2026-10-21,
read as a stylized, hot red) — and licensed under the GNU LGPL v3, the icon set's own licence (`assets/icons/README.md`, "Licence": each file a separate
data file read at run time, never linked or compiled into the program's code); the covers are renders of them under
the same licence. A JPEG or PNG carries no notice, so this README is their record. The settled three: `Wood` the ChatGPT walnut of 2026-10-22, `Metal` the brass of 2026-10-22, `Case` the ebony run. The generation
records (the prompts, the sheets, every candidate) are the planner's `tmp/theme_next/gemini_out/`, not shipped.
