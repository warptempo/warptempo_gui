# The classical icon set: from a Gemini sheet to the drop road

The classical set is the architect's own: Google's Gemini draws the whole set of acts on one sheet in the style of an
input sheet, the sheet is cut and sliced into one 256-px PNG per act, he picks the drawings family by family, and the
picks are dropped into `assets/icons/` (the drop road, `src/gui/icons.h`'s head). The tools live in
`tools/classical_set/`; every script runs with `python3 -I` from the repository root and says what it does in its
head. THE SHEETS ARE NOT KEPT: once the prompt is right they are reproducible, so nothing Gemini draws enters the
repository — a family that needs redoing is redrawn on a whole new sheet and re-sliced. A NEW ACT IS ALWAYS DRAWN ON
THE WHOLE SHEET, never alone: one item drawn alone comes out at a different outline weight from its siblings.

## (a) The input style sheet

`tools/classical_set/style_sheet.png` (committed; its record `style_sheet.txt`) is the image Gemini is given: twelve
Adwaita 3.22 drawings on a 4 x 3 grid, flat light gray, no labels, no shadows, each object's OUTER OUTLINE inked
near-black and thickened x1.5 while its interior lines keep their tonal color (Red Hat's Bluecurve the guide: firm
ink round the silhouette, so the output reads at 48 px on the toolbar's gray-blue case). The treatment is
`dark_outlines.py`: a tonal stroke with real length outside everything painted before it is an outer outline; one
lying inside is an interior line; the per-tile calls the rule cannot make are recorded in its tables and in the
sheet's record.

Why these twelve: Gemini copies the input's drawings into its output set, so NO TILE MAY RESEMBLE AN ACT it is not
meant to model. The clock, the padlocks, the broom, the wrench and GNOME's tan folder each bled into an act (Revert,
Read-Only / Save / Folder, Flatten, Settings, Folder) and were replaced by drawings that relate to no act: the gold
star (the model for the gold arrows' gold), the disc, the gamepad, the hard drive, the monitor and the globe. Five
act drawings stay (Source's book, Target's tablet, Phase Reset's camera, the magnification speaker and Play Renders'
film strip), with the calculator of a retired act.
The act drawings are read from `assets/icons/`; the others are standalone SVGs in `tools/classical_set/style_src/`,
each extracted from the adwaita-icon-theme 3.22.0 tarball at its 48 slot by `extract_style_tile.py` (the product's
own extractor, `tools/icon_set_adwaita_322/extract.py`, its drop shadows removed by id). A replacement tile is a
new row there, a new entry in `dark_outlines.py`'s `TILES`, and a look at the rebuilt sheet: twelve firm outlines,
no interior line gone black, no shadow.

## (b) The prompt

`tools/classical_set/prompt.md` is paste-ready whole: attach `style_sheet.png`, ask for a 4K JPEG, paste the file.
It asks for the WHOLE 56-cell sheet every time (8 columns x 7 rows, a numbered label under each icon, one flat #ECECEC
ground). Why 56 and no more (architect 2026-10-20): Gemini fills the grid it is given and never draws a partial last
row — every sheet asked to carry one cell more came back packed into a full 8 x 7 with a cell dropped or merged — and
packing more cells into the sheet costs each icon its resolution. So PLAYER NEXT IS NOT DRAWN: the silver transport
glyphs are lit straight from above (vertical gradients), so the slicer writes PlayerNext.png as PlayerPrevious.png flipped
left to right and the light survives, the pair living only in the render player, driven from the car. REDO IS DRAWN: the
gold arrows are lit from the top left, so a mirrored Redo would flip the light and the pair stands side by side on the
toolbar. Words learned the hard way: never say "mirror image" (Gemini copies the drawing
instead of mirroring it — say "the SAME object turned to face right"); a fade-out must be forbidden explicitly (NO
fades into the background, nothing below a stand fading) or Gemini fades the foot of a stand or an arrow's tail.

## (c) The slicer

`slice_sheets.py` cuts each sheet with GIMP's own fuzzy select, headless (`gimp_fuzzy.py`): threshold 20 from the
corner and from a ground point in every cell, then a second pass adding a seed in every enclosed pocket of ground
(inside a ring, between an object and its arrow) — the architect's Shift-clicks — and THEN A SECOND POCKET PASS on the
full-resolution cut (architect 2026-10-20, for the light ground left between the starling's legs): the first pass wants
a pocket flat (mean within 6 of the ground, p95 within 12, >= 100 px), and a small pocket is mostly rim, where the JPEG
rings along the outline ink (the 100905 starling's: 145 px, mean 8.3, p95 16). A tighter color match cannot find it —
the lace cuff's holes, the silver keys' highlights and the clapperboard's white stripes sit as close to the ground — so
the second pass is structural: an enclosed opaque ground-colored region (>= 20 px, neutral, mean within 10 and its
interior's too, an area and not a stroke) whose 4-px rim is outline ink, whose ring 10–24 px out is NOT mostly ink (the
object's own fill lies past the outline; a clapper stripe, a chalk line or a bird's eye sits inside black paint), and
which lies 20–64 px from the cut (a whole object part between it and the outside — a white baton has one outline alone —
but a hole through the object, never a mark mid-page). Each such pocket is seeded through the same GIMP fuzzy select
(threshold 20, antialiased) on the raw cut; the seeds are recorded in `sheet_cut_raw.json` beside it, so a re-slice
under `SLICE_REUSE=1` skips the pass where the raw cut already carries it (the raw cut's sidecar is the marker; a raw cut
without one gets the pass, ~3 s to find the seeds plus one GIMP run). The bounds and their measurements are at
`pockets2`'s head. It reads the label rows and each
row's columns off the sheet itself (Gemini's grid drifts), clears ONLY the label glyph boxes — and a glyph is NEUTRAL INK: a component with 2 % or more of its pixels colored (JPEG chroma >= 40) is an icon part, never cleared, whatever its size (Add to Selection's gold plus stands high beside the hand, inside the label row above's window, and was cleared with the text under the size rule alone) — crops each cell's ink
at 44/48 fill and writes `<folder>/sliced/<stamp>/<Act>.png` — 256 x 256 RGBA, named by the act's enumerator
(`src/gui/icons.h`'s `kIconNames`), drop-ready — beside the input JPEG, plus `sheet_cut.png` (the whole sheet cut,
labels cleared, full resolution: the base for a manual fix in GIMP), `sheet_cut_raw.png` (GIMP's cut alone) and
`contact.png` (the cells on the Face gray, each mirrored act a tile of its own in an eighth row). Every file it
writes is an act. After the cells, `PlayerNext.png` is `PlayerPrevious.png` flipped left to right (the same 256-px
RGBA), written beside it. THERE ARE THREE 56-CELL LISTS, all 8 x 7, so nothing in a sheet tells them apart and a flag
picks the list: the default is the current prompt's (Redo at cell 34, Player Next not drawn, PlayerNext mirrored);
`--redo-mirrored` reads the sheets of 2026-10-20 ~09:50 (the one commit efb39ea5: Redo absent, Player Next
drawn at cell 47 — only `tmp/classical_set/gemini-new4/`'s sheets were drawn from it), writing Redo as Undo mirrored
and nothing for PlayerNext; `--legacy56` reads the sheets of 2026-10-19 (a silver Stop at cell 46 that no enumerator
wears, Redo and Player Next drawn), writes no file for cell 46 and nothing mirrored (55 files). A sheet fed the wrong
list slices wrong after cell 33. A sheet from the 2026-10-20 morning prompt that asked for one cell more (packed into
seven rows with a cell lost) slices WRONG and must not be fed in. A `CLIPPED?` line names a cell whose ink continues just past its band's edge: so far
it has always been a label piece pooled into the icon above it (outside the crop, left in `sheet_cut.png`) or a
drawn border's dash, never a cut icon — but look at the contact sheet. `EMPTY` names a cell with no ink.
`SLICE_REUSE=1` re-slices from an existing `sheet_cut_raw.png` without GIMP's first two passes (a rule change, ~5 s a sheet).

## (d) The family pages for the pick

The architect picks FROM FAMILIES, not from whole sheets: a family's members must share shading and lighting.
`family_sheets.py` lays every sliced sheet's block of each family side by side on 1920 x 1080 PNG pages, judged on
the laptop only (never pushed to the tablet). The families: Starlings (Left, Right), Flags (Drop Marker, Previous / Next
Marker, Jump to Defining Marker: the flag is the marker, so the acts that carry it are judged together, since
2026-10-20), Undo and Redo, Watches (Revert, History View, Older, Newer), Transport keys
(Play, Pause, Player Previous / Next, Repeat One, Load in Place, Close), Folders, read-only and save, Hands
(Cancel, Add to Selection, Follow, and the conductor's Transport Play / Transport Stop — the cuffs must match), Paper
and books (without Drop Marker, with Save Commit / Save Pull's podiums, and, since 2026-10-20, Up, Down and Value Drag, drawn as a sheet of
score paper whose notes crowd or open across the page), Magnifying glasses (Full Zoom Out, Center), Card medallions; the
other acts are isolated, one row per act with every version. An icon with an object besides the arrow goes with its
object; a group is all or nothing. A `hide.json` beside the sheets (kept with them, never in the repository) names
the members to hide per sheet — an input drawing copied verbatim, an isometric drawing — and a sheet whose group has
ANY hidden member shows no block for that group; hidden is omitted, never shown. ALL FLAGS FLY RIGHT (architect
2026-10-20: the pennant's cloth to the RIGHT of its pole, on every cell that carries it): `flag_side.py` finds each Flags
tile's cloth and pole and adds every left-flying tile to the hide.json, so that sheet's Flags block is omitted (its
head says how, and why not by the tallest dark run; a tile it cannot decide stays visible and is named). `act_strip.py`
is the close-up: one act across every sheet.

## (e) The drop

Copy the picked `<Act>.png` into `assets/icons/` and build; `<Act>.png` wins over `<Act>.svg`, which stays as the
fallback until the seat is called settled. A drawing several acts wear is ONE file and symlinks
(`ln -s Warp.png AppIcon.png`). Nothing else: no table, no README row, no code change (`src/gui/icons.h`'s head).
Nothing in `tools/classical_set/` writes there.

## (f) Dependencies and the runs

GIMP 3.2 with its python-fu batch interpreter (`gimp-console`), resvg (the product's pin), rsvg-convert, Python 3 with
Pillow, numpy and lxml; the extracted adwaita-icon-theme 3.22.0 tarball (`tools/icon_set_adwaita_322/README.md`)
for a new style tile.

    python3 -I tools/classical_set/extract_style_tile.py <tarball-root> [Tile ...]      # a style tile into style_src/
    python3 -I tools/classical_set/dark_outlines.py                                      # style_sheet.png (+ review copies in tmp/classical_style_work/)
    python3 -I tools/classical_set/slice_sheets.py tmp/<sheets>/*.jpg                    # <sheets>/sliced/<stamp>/<Act>.png (--redo-mirrored | --legacy56: the two older lists)
    python3 -I tools/classical_set/flag_side.py --hide tmp/<sheets>/hide.json a=tmp/<sheets>/sliced   # left-flying flags hidden
    python3 -I tools/classical_set/family_sheets.py tmp/<pages> tmp/<sheets>/hide.json a=tmp/<sheets>/sliced
    python3 -I tools/classical_set/act_strip.py tmp/<strip>.png <Act> a=tmp/<sheets>/sliced
