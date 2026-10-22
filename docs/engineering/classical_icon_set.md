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
Read-Only / Save / Folder, Flatten, Settings, Folder) and were replaced by drawings that relate to no act: the star
(the model for the brass arrows' metal), the disc, the gamepad, the hard drive, the monitor and the globe. Five
act drawings stay (Source's book, Target's tablet, Phase Reset's camera, the magnification speaker and Play Renders'
film strip), with the calculator of a retired act.
The act drawings are read from `assets/icons/`; the others are standalone SVGs in `tools/classical_set/style_src/`,
each extracted from the adwaita-icon-theme 3.22.0 tarball at its 48 slot by `extract_style_tile.py` (the product's
own extractor, `tools/icon_set_adwaita_322/extract.py`, its drop shadows removed by id). A replacement tile is a
new row there, a new entry in `dark_outlines.py`'s `TILES`, and a look at the rebuilt sheet: twelve firm outlines,
no interior line gone black, no shadow. THE STAR IS OLD BRASS (architect 2026-10-21, the set's materials below): Tango's
glossy gold star is re-tinted in the treatment, never in its extracted file — `dark_outlines.py`'s `RECOLOR` maps its
twelve yellows by one rule (`brass`: the hue toward brass, the saturation reduced, the lightness lowered), its white
glints and black untouched, before the outline pass, so its outline inks from the brass.

## (b) The prompt

`tools/classical_set/prompt.md` is paste-ready whole: attach `style_sheet.png`, ask for a 4K JPEG, paste the file.
It asks for the WHOLE 56-cell sheet every time (8 columns x 7 rows, a numbered label under each icon, one flat #ECECEC
ground). THE MATERIALS ARE MOZART'S FORTEPIANO'S (architect 2026-10-21, since draft 14): OLD BRASS for every arrow, flag and
case fitting (no gold anywhere), POLISHED STEEL for the render player's glyphs (no silver), BONE for white (no ivory),
and walnut. The drawings are judged on the NEAR-BLACK EBONY button they will sit on (the pick pages, `contact.png` and
the act strips on ebony #291D1B), but since draft 19 (architect 2026-10-21 ~21:10, "let's try without the near-black
lines — everything already rendered fine") the prompt's head says only that the icons are shown at 48 x 48: its two
ebony sentences of draft 14 (the near-black button, no object black as a whole) were removed as the suspect for the
checker pattern on two of about ten sheets. Since draft 22 (architect 2026-10-21 ~22:45, "remove all the framing you
did today") the head and the materials are the pre-session wording plus the agreed materials (brass, steel, bone,
walnut, the marbled Viennese portfolio), every "no X" framing clause the session added removed on his word — they
"didn't change the output"; the checkerboard sheets are kept, their two greys 11 levels apart, inside the cut's 20, so
the slicer cuts them cleanly (proven on 20261021223831). Since draft 22 the pocket watches have no chain ("that makes
the icons smaller"), Toggle Tooltips is one bold question mark alone and Toggle Inherit one bold ditto mark alone (the
copyist's "the same again"), both in brass with the set's gloss since draft 23 (2026-10-21 ~23:05, "make the tooltips
and the idem brass"; light bone-colored for draft 22 alone). Since draft 24 (2026-10-22 ~00:10, "make the render stamp
not inside paper but free floating also") Render is one red wax seal alone, large and free-standing, no paper or score
under it. Since draft 25 (2026-10-22 ~00:20, "value drag should be double headed updown arrow, and add to selection
should be simple plus") Toggle Add to Selection is one bold brass plus sign alone (the manicule with its cuff retired)
and Toggle Value Drag one bold brass double-headed vertical arrow, a head up and a head down (the two-staff score sheet
retired). Since draft 19 Phase Reset's clapperboard is metal — its clapper sticks and
rim, the stripes metal and bone, never black and white ("the clapper should be changed to bronze to stand
out"), brass since draft 20 ("keep the clapper as brass") — while Wav Row's black record stays by his word ("the wavrow shows only on mid-level bg, so dark record is
fine"). Since draft 16 (architect 2026-10-21 ~19:50, the tan folders "a bit too bright and tan … kind of modern")
the brass is a dull, dark antique brass, an old door knob's, darker than a trumpet's; Folder and Save are THE VIENNESE
PORTFOLIO, a stiff pasteboard music portfolio in bold burgundy-and-blue marbled paper with a mid-brown calf spine and
corners and two cloth ties, as a 1780 copyist kept his parts (the tan calf case of draft 14 retired); Read-Only is a bare
brass padlock with no case, and Switch Tab (draft 17, 2026-10-21 ~20:25) a closed notebook in the portfolio's marbled boards with two solid index tabs, brass and burgundy. THE PROMPT'S METAL WORD WAS BRONZE for drafts 18–19 (architect 2026-10-21 ~20:45, "go bronze everywhere"): measured on the draft-15 and -16 sheets, Gemini's "brass" floor was a yellow gold (the arrow bodies L .50 S .42 against the lock plate's #5B4024), so the prompt names a darker metal — old bronze, a church bell's, no green patina, no copper orange — and Gemini's lightening lands on old brass ("if gemini lightens it, we just go slightly too dark"). IT IS BRASS AGAIN since draft 20 (architect 2026-10-21 ~21:25, on the first bronze sheet beside photos of brass and of Mozart's fortepiano: "brass is much closer to gold than to bronze in my view … I think gemini knows and is portraying it correctly"): the materials line is draft 15's plain old brass — warm, hand-polished, deep, not gilt, not gold, not mirror-bright — draft 16's door-knob darkening and draft 18's bronze both gone. A slice-time re-tint by the style star's rule was mocked (tmp/classical_brass/brass_tint_mock.png) and not taken: olive arrows, mottled parchment. The sheet's own ground stays #ECECEC: the cut needs a
flat light ground to take away. Why 56 and no more (architect 2026-10-20): Gemini fills the grid it is given and never draws a partial last
row — every sheet asked to carry one cell more came back packed into a full 8 x 7 with a cell dropped or merged — and
packing more cells into the sheet costs each icon its resolution. So THE LEFT / RIGHT PAIRS ARE DRAWN ONCE (architect
2026-10-21 evening, draft 15: "we mirror left/right but never up/down since the latter can have diff shading — gemini
generally doesn't give left/right diff shading"): the slicer writes Right.png as Left.png, Redo.png as Undo.png and
PlayerNext.png as PlayerPrevious.png, each flipped left to right; Up and Down are both drawn, the main page's plain brass
arrows and the render player's own steel PLAYER UP / PLAYER DOWN (`PlayerUp`, `PlayerDown`, cells 48–49, new acts so the
player no longer wears the main page's). UP ONE LEVEL IS OFF THE SHEET: the act keeps its folder drawing (the
`UpAFolder.svg -> FolderRow.svg` link stands), since a steel up arrow beside the player's new steel Up would be the
identical-arrows pair that link was made to remove. Older and Newer are BOTH DRAWN again (cells 35–36), Background Sound
(`BackgroundSound`, the umbrella) at cell 37; Older and Newer are worn by the `h` view alone. Previous / Next Marker stay
drawn (Next Marker is not round like the watch, so it is not derived). The new acts' enumerators (`BackgroundSound`,
`PlayerUp`, `PlayerDown`) come with the chrome's own feature: until they exist their sliced PNGs wait in `tmp/`, never
as an orphan file in `assets/icons/`. Under draft 14 (the `--draft14` list below) Redo was drawn for its lighting and
Newer was not drawn but DERIVED FROM OLDER — the watch cannot be mirrored (its digits would read backwards), so the
slicer turns the arrow alone, the prompt asking Older's arrow CLEAR of the watch and lit straight from above. Words learned the hard way: never say "mirror image" (Gemini copies the drawing
instead of mirroring it — say "the SAME object turned to face right"); a fade-out must be forbidden explicitly (NO
fades into the background, nothing below a stand fading) or Gemini fades the foot of a stand or an arrow's tail.

## (c) The slicer

`slice_sheets.py` cuts each sheet with GIMP's own fuzzy select, headless (`gimp_fuzzy.py`): threshold 20 from the
corner and from a ground point in every cell, then a second pass adding a seed in every enclosed pocket of ground
(inside a ring, between an object and its arrow) — the architect's Shift-clicks — and THEN A SECOND POCKET PASS on the
full-resolution cut (architect 2026-10-20, for the light ground left between the legs of a starling, an earlier draft's Left): the first pass wants
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
row's columns off the sheet itself (Gemini's grid drifts), clears ONLY the label glyph boxes — and a glyph is NEUTRAL INK: a component with 2 % or more of its pixels colored (JPEG chroma >= 40) is an icon part, never cleared, whatever its size (Add to Selection's gold plus stood high beside the hand before draft 25, inside the label row above's window, and was cleared with the text under the size rule alone) — crops each cell's ink
at 44/48 fill and writes `<folder>/sliced/<stamp>/<Act>.png` — 256 x 256 RGBA, named by the act's enumerator
(`src/gui/icons.h`'s `kIconNames`), drop-ready — beside the input JPEG, plus `sheet_cut.png` (the whole sheet cut,
labels cleared, full resolution: the base for a manual fix in GIMP), `sheet_cut_raw.png` (GIMP's cut alone) and
`contact.png` (the cells on ebony #291D1B, the case the icons stand on since 2026-10-21, each made act — mirrored or turned — a tile of its own in an eighth row). Every file it
writes is an act. After the cells, `PlayerNext.png` is `PlayerPrevious.png` flipped left to right (the same 256-px
RGBA), and so are `Right.png` from `Left.png` and `Redo.png` from `Undo.png` (draft 15); under `--draft14`, `Newer.png` is `Older.png` with its arrow turned (`turned_twin`: of Older's alpha components the largest is
the watch and the rest the arrow; the whole image is flipped and the original watch put back in its mirrored box, so
the watch reads as drawn and the arrow points right on the right), both written beside their source. When Older's
arrow touches its watch they are one component and cannot be parted: the slicer prints `TOUCHING Older: Newer not
derived` and writes no Newer — the cue to redraw (that list's mechanism; draft 15 draws Newer). THERE ARE FIVE
56-CELL LISTS, all 8 x 7, so nothing in a sheet tells them apart and a flag picks the list: the default is the current
prompt's (draft 15: Older and Newer drawn, Background Sound at 37, the plain brass Left / Up / Down at 38–40, Player
Up / Down at 48–49; Redo, Right and PlayerNext mirrored); `--draft14` reads the sheets of 2026-10-21 ~16:50–17:00
(`tmp/classical_set/gemini-new9/`: Redo at cell 34, Background Sound at cell 37, Player Next and Newer not drawn,
PlayerNext mirrored, Newer turned); `--newer-drawn` reads the sheets of 2026-10-20 ~10:05 to 2026-10-21 (Newer drawn
at cell 37, PlayerNext mirrored); `--redo-mirrored` reads the sheets of 2026-10-20 ~09:50 (the one commit efb39ea5: Redo absent, Player Next
drawn at cell 47 — only `tmp/classical_set/gemini-new4/`'s sheets were drawn from it), writing Redo as Undo mirrored
and nothing for PlayerNext; `--legacy56` reads the sheets of 2026-10-19 (a silver Stop at cell 46 that no enumerator
wears, Redo and Player Next drawn), writes no file for cell 46 and nothing mirrored (55 files). A sheet fed the wrong
list slices wrong after cell 33 (between `--draft14` and `--newer-drawn`, cell 37 alone is misnamed). A sheet from the 2026-10-20 morning prompt that asked for one cell more (packed into
seven rows with a cell lost) slices WRONG and must not be fed in. A `CLIPPED?` line names a cell whose ink continues just past its band's edge: so far
it has always been a label piece pooled into the icon above it (outside the crop, left in `sheet_cut.png`) or a
drawn border's dash, never a cut icon — but look at the contact sheet. `EMPTY` names a cell with no ink.
`SLICE_REUSE=1` re-slices from an existing `sheet_cut_raw.png` without GIMP's first two passes (a rule change, ~5 s a sheet).

## (d) The family pages for the pick

The architect picks FROM FAMILIES, not from whole sheets: a family's members must share shading and lighting.
`family_sheets.py` lays every sliced sheet's block of each family side by side on 1920 x 1080 PNG pages, its tiles on ebony like contact.png, judged on
the laptop only (never pushed to the tablet). The families: Arrows (Left, Right, Up, Down — the plain brass arrows of
draft 15, Right the mirrored Left — and, since 2026-10-22, Value Drag, the double-headed brass arrow of draft 25), Flags (Drop Marker, Previous / Next
Marker, Jump to Defining Marker: the flag is the marker, so the acts that carry it are judged together, since
2026-10-20), Undo and Redo (Redo the mirrored Undo), Watches (Revert, History View, Older, Newer — both drawn again since draft 15),
Transport keys (Play, Pause, Player Previous / Next, Repeat One, Player Up / Down, Load in Place, Close), Folders,
read-only and save (Folder, Read-Only, Save — Up One Level left with the sheet), Hands
(Cancel, Follow, and the conductor's Transport Play / Transport Stop — the cuffs must match; Add to Selection, the
bare plus of draft 25, among the isolated acts since 2026-10-22), Paper
and books (without Drop Marker, with Save Commit / Save Pull's podiums; without Value Drag, which joined the Arrows,
and without Toggle Inherit, Toggle Tooltips and Render since 2026-10-22 — the bare ditto mark,
question mark and wax seal of drafts 22 and 24 are no paper objects, so they are judged among the isolated acts),
Magnifying glasses (Full Zoom Out, Center), Card medallions; the other acts are isolated, one row per act with every version. An icon with an object besides the arrow goes with its
object; a group is all or nothing. A `hide.json` beside the sheets (kept with them, never in the repository) names
the members to hide per sheet — an input drawing copied verbatim, an isometric drawing — and a sheet whose group has
ANY hidden member shows no block for that group; hidden is omitted, never shown. ALL FLAGS FLY RIGHT (architect
2026-10-20: the pennant's cloth to the RIGHT of its pole, on every cell that carries it): `flag_side.py` finds each Flags
tile's cloth and pole and adds every left-flying tile to the hide.json, so that sheet's Flags block is omitted (its
head says how, and why not by the tallest dark run; a tile it cannot decide stays visible and is named). OUTDATED
DRAWINGS ARE HIDDEN BY DRAFT (architect 2026-10-20: "everything that isn't currently in prompt.md"): `family_sheets.py`'s
`REDRAWN` names, per act, the prompt draft its current drawing was first asked in (a different object, never a
refinement of the same one), and a sliced folder given as `<prefix>=<folder>@<draft>` hides every act redrawn after
that draft, so an old batch keeps its unchanged acts and loses its superseded ones; a redrawn act's row in `REDRAWN`
moves to the new draft when its line changes. `act_strip.py`
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
    python3 -I tools/classical_set/slice_sheets.py tmp/<sheets>/*.jpg                    # <sheets>/sliced/<stamp>/<Act>.png (--draft14 | --newer-drawn | --redo-mirrored | --legacy56: the older lists)
    python3 -I tools/classical_set/flag_side.py --hide tmp/<sheets>/hide.json a=tmp/<sheets>/sliced   # left-flying flags hidden
    python3 -I tools/classical_set/family_sheets.py tmp/<pages> tmp/<sheets>/hide.json a=tmp/<sheets>/sliced@<draft>
    python3 -I tools/classical_set/act_strip.py tmp/<strip>.png <Act> a=tmp/<sheets>/sliced
