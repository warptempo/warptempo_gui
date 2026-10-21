# The tiles

THE WOOD CHROME'S MATERIALS (architect 2026-10-20: "get the code right, then decide which tile"): ONE FLAT FOLDER, ONE
FILE PER MATERIAL, NAMED BY ITS ROLE (`src/gui/materials.h`, `kGuiMaterialNames`):

- `Wood` — the caption, the menu row and the toolbar band (its panes, grooves and edge columns);
- `WoodDark` — the band's void, its stretch where no pane stands;
- `MetalDark` — every other chrome surface: the tabs, the column's margins, the dock bar, row 8, the border under it,
  the program frame's line, the pull-downs, cards and tooltip, the modal box and its controls row, the render player,
  the on-screen keyboard, the dialog fields, the laptop's sizing frame;
- `MetalVeryDark` — every recess below the band: the tab strip's ground, the column's air, the ruler's and the marker
  lane's ground, the column's foot;
- `Case` — every button case's interior and the caption's three buttons.

Each is `<Name>.png` or `<Name>.jpg`; WHERE BOTH STAND THE PNG WINS (the icons' rule). THIS FOLDER IS A DROP ROAD: a
file dropped here under a material's name replaces that material at the next launch on the laptop and at the next APK
build on the tablet, with no table, no row in this README and no code change. A tile is opaque (its alpha is not read)
and any size; a 4K master is the intent. A missing material, or a file that is neither a PNG nor a JPEG, fails the
launch, naming the file. Files of other names (this README) are not read.

THE ROAD: the program reads the files at launch, in place — on the laptop from this folder (its path compiled in,
`CMakeLists.txt`'s `WARPTEMPO_BUNDLED_TILES_DIR`), on the tablet from the APK's `tiles/` assets
(`android/app/build_apk.sh`) — decodes each through its own resvg pin (`svg_icon::raster_info` and `raster_region`,
the icons' data-URL detour, PNG and JPEG alike) and draws it at `gui_scale / 300` with the icons' Lanczos-3 area filter
(the tablet's 300 % takes the master 1:1). Only the part kept is rendered: the woods' strip (the master's whole scaled
width by the upper region's height, from its vertical centre), the metals' and the case's centre crops (mirror-tiled
on screen). Each material's FACE is its kept tile's mean color, and every tone a painter puts down on it is Cool
Edit's tone rule on that Face, painted on every material as the tone itself, a flat opaque stroke with no grain
(architect 2026-10-20 ~22:50: the lines of the wood, the metals and the cases alike; `materials.h`'s head). Only a
Face tone is the tile as drawn, and the case's ramp its tile scaled per row.

## Provenance and licence

Every file in this folder is drawn with Google's Gemini at the architect's prompt for this project (cut and shaved by
the planner), and licensed under the GNU LGPL v3, the icon set's own licence (`assets/icons/README.md`, "Licence":
each file a separate data file read at run time, never linked or compiled into the program's code). A JPEG or PNG
carries no notice, so this README is their record. The first set, placed 2026-10-20 as the placeholder the code was
written against: `Wood` and `WoodDark` one Gemini chat's light and dark planks, the three others one chat's metal set
(the planner's `tmp/wood_mock/` records the sheets); his pick of a set replaces them by drop.
