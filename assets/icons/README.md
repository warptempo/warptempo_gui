# The icons

ONE FLAT FOLDER, ONE FILE PER `icons::Icon` ENUMERATOR (`src/gui/icons.h`), EACH NAMED BY ITS ACT (architect
2026-10-19: "since the actions are essentially complete, we should use the action name"): `<Act>.png` or `<Act>.svg`,
the act's tooltip name in PascalCase — `Warp.png`, `Undo.svg`, `ReadOnly.svg`. THIS FOLDER IS THE DROP ROAD
(architect 2026-10-19: "simply drop a new icon with the right name, and if both a png and svg exist, the GUI favors the
png … the goal is I can build and push after any icon change, without needing to consult"): a square PNG with alpha at
256 px, named `<Act>.png`, dropped here replaces that act's glyph at the next launch on the laptop and at the next APK
build on the tablet, with no generator, no table, no row in this README and no code change. WHERE BOTH FILES STAND THE
PNG WINS, the Adwaita SVG staying beside it as the fallback until the architect calls the seat settled. A dropped drawing
is centered on its ink at the launch (below, "The shift"), so a drop needs no alignment either.

WHERE NO PNG STANDS, the drawing is adwaita-icon-theme 3.22.0's (architect 2026-10-14): the 3.22.0 tarball's own
`src/fullcolor/` artwork at its 48 slot, nothing redrawn and nothing from any other release; gnome-icon-theme 2.28,
the set before it, retired whole the same day. Two of those files are CHANGED from their drawings (below, "The changed
files"). EVERY `.png` HERE IS THE ARCHITECT'S OWN (below, "The architect's own files").

A DRAWING SEVERAL ACTS WEAR IS ONE FILE AND SYMLINKS (architect 2026-10-19: "repeats should use symlinks"), relative,
inside this folder, the real file the act that owns the drawing:

- `AppIcon.svg` -> `Quaver.svg` (2026-10-23, architect "the new note glyph on the caption's color": the program
  icon outside the window is the caption's quaver — the Android launcher icon rendered from it at every APK build
  over the caption's ground, `android/app/build_apk.sh`; the Linux icon `packaging/warptempo_gui.svg`, a link to
  `Quaver.svg`, `docs/INSTALL.md`; `AppIcon.png` -> `Warp.png`, the conductor's baton, 2026-10-18 to 2026-10-23);
- `BpmIterations.svg` -> `WavRow.svg` (the BPM Iterations button keeps the note — the quaver since 2026-10-23,
  `audio-x-generic` until then; no band key wears a glyph in the hi-fi skin);
- `WavRow.svg` -> `Quaver.svg` (2026-10-23: the wav rows wear the caption's quaver, architect "use the logo in the
  render player");
- `CardCritical.svg` -> `Cancel.svg` (`process-stop`);
- `UpAFolder.svg` -> `FolderRow.svg` (the folder; Up a Folder wears the folder row's drawing, architect 2026-10-10);
- `TransportPlay.svg` -> `Play.svg` (`media-playback-start`; the editor's Play / Stop toggle on row 8 is its own pair
  since 2026-10-20, so a drop for it leaves the render player's Play standing; its stop face, `TransportStop.svg`,
  owns `media-playback-stop` outright, the render player wearing no stop).

A PNG dropped for one of a link's acts replaces that act's glyph alone. The set is hard-coded (the `icons` device key
and the Settings Icons row were removed 2026-10-12). The program reads the files at launch, in place, through resvg
(`src/gui/svg_icon.h`): on the laptop from this folder, on the tablet from the APK's `icons/` assets
(`android/app/build_apk.sh`, which copies each link as the file it names); a file that is not well-formed SVG, or a
`.png` that is not a PNG, fails the launch, naming the file. Files of other names in this folder (this README, the
licence texts) are not read.

## How the files are made

The tarball ships its scalable artwork as SHEETS (`src/fullcolor/<sheet>.svg`, each carrying an icon's drawings at
every size, often several icons), so every file here is EXTRACTED by `tools/icon_set_adwaita_322/` (`build_set.py`
over `mapping.tsv`, its README the usage) and is that tool's byte-identical output (the tool writes no PNG and no
link; its root tag carries the builder's `data-body-shift`, below, "The shift"): the sheet PRUNED IN PLACE —
hidden layers, the editor's metadata and every element that puts no ink in the 48 slot are removed, every remaining
element keeping its ancestors exactly; the slot NESTED as its own `<svg>` viewport inside a 48-unit root (an app
icon with no 48 drawing nests its 256 slot, scaled by that viewport as the theme renders its 48 PNG); a mask or
filter in user space with no region of its own GIVEN, EXPLICITLY, THE REGION THE FULL SHEET'S VIEWPORT GAVE IT
(−10 % / 120 % of the sheet), because the 3.22 `edit-undo` / `edit-redo` masks otherwise clip the arrow to nothing in
every current renderer; EVERY DROP SHADOW REMOVED by id (below, `shadows.tsv`), and the few other elements the
architect rules out (`removed.tsv`: Value Drag's arrow, 2026-10-18), every `<use>` of a removed element with it; the defs pruned to those referenced. Nothing is redrawn. Each seat's faithful drawing (shadows kept) is
checked against the sheet cropped to its slot and against the theme's own shipped 48 PNG
(`Adwaita/48x48/<context>/<name>.png`).

## Provenance

- adwaita-icon-theme 3.22.0, `adwaita-icon-theme-3.22.0.tar.xz`
  (https://download.gnome.org/sources/adwaita-icon-theme/3.22/), sha256
  `c18bf6e26087d9819a962c77288b291efab25d0419b73d909dd771716a45dcb7`. Each SVG's source sheet and its sha256 are in
  the table below.
- Every `.png`: the project's own (below, "The architect's own files").

## Licence

The tarball's `COPYING`, copied here verbatim as `COPYING-adwaita-icon-theme-3.22.0`: "This work is licenced under
the terms of either the GNU LGPL v3 or Creative Commons Attribution-Share Alike 3.0 United States License … When
attributing the artwork, using 'GNOME Project' is enough." The two licence texts it names are here verbatim as
`COPYING_LGPL-adwaita-icon-theme-3.22.0` and `COPYING_CCBYSA3-adwaita-icon-theme-3.22.0`, its `AUTHORS` as
`AUTHORS-adwaita-icon-theme-3.22.0`.

How the files sit beside the GPL-3 program: each file (every SVG and every PNG) is a separate data file under its
own licence, read at run time and never linked or compiled into the program's code; the repository distributes source
only (THIRD_PARTY.md's own statement). Under LGPL-3 or CC BY-SA 3.0 there is no GPL-2-only concern. The architect's
own drawings are licensed under the GNU LGPL v3, the set's own licence (architect 2026-10-15: "the icon licensing could
go either way … the LGPL3"; 2026-10-19: keep the licensing simple); a PNG can carry no notice, so this README is their
record (below).

## The architect's own files

EVERY `.png` IN THIS FOLDER IS THE ARCHITECT'S OWN: drawn with Google's Gemini at his prompt and cut out by him in GIMP,
shipped at 256 x 256 RGBA, under the GNU LGPL v3 like the set (architect 2026-10-19: keep the licensing simple). No
file has a row of its own here: a drop needs no README edit. The program decodes a PNG once at launch through its own
resvg pin (`svg_icon::parse_png`; any shape, the frame its longer side) and scales it at every seat by a LANCZOS-3 AREA
FILTER from the full raster (`svg_icon::rasterise`, PIL's `Image.LANCZOS` construction), never resampled by resvg's
`<image>`; its user space is the set's 48-unit frame, so the body shift and the disabled copy's one-unit translate hold.

HISTORY: the first, Warp's baton (`Warp.png`), landed 2026-10-18 as the set's one raster exception — Gemini Flash's
redo at the set's outline weight, his pick B1 — shipped then as cut at 2048 x 2048 (sha256
`30541ba274b914d7c3a9e6ed01aaada0412ddc09dfc593b8f17587a794989c3c`, tracked until 2026-10-19 as
`tools/icon_set_adwaita_322/own/Baton.png`; git history and the planner's `tmp/baton/` keep it), resized to 256 by
ImageMagick's Lanczos filter 2026-10-19, when the exception became the drop road. The set's earlier own files — the
architect's digital metronome, 2026-10-16 to 2026-10-17, and the speedometer, the evening of 2026-10-17, both drawn with
Gemini — and the baton's first cutout, the morning of 2026-10-18, retired (git history).

## The project's own SVGs

Two SVGs here are no extraction (architect 2026-10-23, the hi-fi skin), the project's own and licensed under the GNU
LGPL v3 like the set, each one filled path in the bone F0F0F0 on a 34 x 40-unit viewBox, its ink on rows 7..34 and its
root tag `data-body-shift="0 0"` — BOXED GLYPHS, placed by their box and never centered on their ink
(`src/gui/icons.h`, THE BOXED GLYPHS):

| File | Drawing | Wearers |
|---|---|---|
| `Quaver.svg` | the quaver: Gemini Argon's one-path conversion of the planner's bitmap at the architect's prompt (his pick, row 4 of the caption sheet) | the caption's icon; the wav rows (`WavRow.svg`'s link) |
| `FolderRow.svg` | the planner's folder, `M 4 10 H 15 V 14 H 30 V 34 H 4 Z` — a square with a tab at the top (architect: "just a simple square with a tab at the top"; his pick, variant c of three tabs) | the folder and project rows; Up a Folder (`UpAFolder.svg`'s link) |

`FolderRow.svg` and `WavRow.svg` were `folder` and `audio-x-generic` until 2026-10-23; their rows in the tables below

The dropped `WavRow.png`, `FolderRow.png` and `UpAFolder.png` of the classical set (the brass record and portfolio) were REMOVED 2026-10-23 night: under the PNG-wins rule they outranked the quaver and the folder the architect ruled for the list rows ("use the logo in the render player, but then we need a folder icon as well"); a new drop under those names would win again.
are the builder's record (`tools/icon_set_adwaita_322/mapping.tsv` still names them).

## The changed files

Every extracted file is changed in one respect, its drop shadows removed (2026-10-15, the section below, recorded here
and in `tools/icon_set_adwaita_322/shadows.tsv`; the architect's PNGs, above, are no extraction and have none). Two drawings
carry a further change: one recorded under the licence with a dated notice in an XML comment at its head, the other
an element removed by `removed.tsv`, its record this section; every other file is the extraction alone. One file
carries outlined lettering, the clock's digits (Liberation Sans Bold). (`TabDetachB.svg`, `preferences-system-windows`
with its element order changed so the lower-right window stood in front — Switch Tab's tab-B glyph from 2026-10-14 —
LEFT THE SET 2026-10-18: the tab strip names the active tab, and Switch Tab wears `SwitchTab.svg`, the drawing as
extracted, in both tabs — "use the original, not the modified".)

- `HistoryView.svg` (2026-10-14; history's again since 2026-10-18, "go back to using the original git clock
  icon") is `document-open-recent` (the clock in `clocks.svg`, its yellow arrow turned counter-clockwise) with its 48
  clock-digit `<text>` elements replaced by the glyph outlines of the face the sheet names, Liberation Sans Bold, from
  the repository's own `fonts/LiberationSans-Bold.ttf` (`tools/icon_set_adwaita_322/text_to_paths.py`,
  `mapping.tsv`'s `text-to-outlines` treatment: the face's own advances and its GPOS pair kerning, text-anchor
  honored), because the product's resvg is built without text and draws none. The outline geometry is what a text
  renderer lays down from the same file, measured 0.26/255 mean against rsvg's own text rendering. Its bytes are the
  file history wore 2026-10-16 to 2026-10-17 exactly; its notice names it `DocumentOpenRecent`, its name when it was
  written (the file's stem the act's since 2026-10-19).
- `ValueDrag.svg` (2026-10-18) is `format-text-direction-rtl` (its own sheet, slot (296, 50)) with its ARROW GROUP
  `g7348` removed (`removed.tsv`; architect 2026-10-18, "drop the arrows altogether") beside its two drop shadows: THE
  PEN ALONE, as drawn, its nib at the lower left, not mirrored. It renders identically to the mock the architect judged
  (rsvg at 192 px, byte-identical PNGs). The vertical double arrow `object-flip-vertical`, Value Drag's from
  2026-10-11, left the set with it.

## The deliberate seats

Since 2026-10-23 no toolbar key wears its drawing: the keys are blank, each act's name printed under its key, and the
act files below stay loaded for a later drop (the drawings on screen are the caption's quaver and the list rows'
`WavRow` and `FolderRow`). The seats record which drawing each act's file holds.

The architect's (2026-10-14): Source wears `accessories-dictionary` (the primary source), Target `input-tablet` (the
authoring surface); since 2026-10-18 Warp the baton (the architect's own, above; its earlier glyphs there and in git
history), and since 2026-10-15 Phase Reset `camera-photo` (the phase
snapshot: a reset re-grounds the synthesis phase to the source's at one frame, and it decoheres from there), its 48 slot (300, 50) — the sheet's 256 "hires" slot is never preferred over a 48
drawing, which is tuned for the seats; Toggle History View `document-open-recent`, the clock turned back, history's
again since 2026-10-18 (`x-office-calendar` the night of 2026-10-15 and 2026-10-17 late evening to 2026-10-18); Toggle
Value Drag `format-text-direction-rtl`'s pen (2026-10-18, above); since 2026-10-16 Center `edit-find` (the magnifier
finds the playhead; its 48 slot (300, 50), the sheet's other being edit-find-replace's), Full Zoom Out `view-restore`
(the view shrunk back; the second 48 slot of `view-fullscreen-restore.svg`, (696.00012, 50.000008), proved against the
shipped `view-restore.png` at d 0.65 where the first slot sits at 54.7) and Toggle Waveform Magnification
`audio-volume-high` (the loud speaker for the louder waveform), zoom.svg's three magnifiers retired ("low contrast"); the
Read-Only toggle the one padlock `changes-prevent` in both states (2026-10-21; the `changes-allow` pair of 2026-10-14 retired); Save `document-save` (the
floppy retired); Flatten `edit-clear-all` (3.22's own broom); Load in Place `edit-paste`. CardCritical wears
`process-stop`, as Cancel does (a link). Every seat's drawing is named in the table below. (Toggle Cumulative's
`accessories-calculator` and Toggle Session History's `network-wired-disconnected` left the folder 2026-10-19 with
their acts.)

## Findings

- `multimedia-volume-control`, the dial, is excluded (again 2026-10-15): every slot's knob (16, 22, 32, 48 and the
  hires) is an embedded raster inside the SVG, a 512-px bitmap at the large slots; no vector knob exists. (The
  architect's own PNGs are his drawings, the drop road; an Adwaita SVG with a bitmap inside stays ruled out.)
- `format-text-direction-ltr` is PNG-only in 3.22 (no scalable source) and is not a mirror of `-rtl`, so the two are
  no Source / Target pair; Value Drag wears `-rtl`'s pen alone (above).
- The clock's digits (`document-open-recent`) are `<text>`, which the product's resvg does not draw — hence
  HistoryView's outlines, above.
- `find.svg` carries an `<image>` and a `<pattern>`, neither in edit-find's 48 slot nor in `Center.svg`. That file
  does carry one `<text>` ("TAN-GO", the edit-find-replace pencil's legend in the same layer) outside its 48 viewport:
  it puts no ink in the slot, and the product's resvg skips text.

## The import check

Every SVG was rendered at the 60-px seat on the band's case through resvg 0.48.1, the product's pin, with the
body shift applied, onto one contact sheet and looked at: every file draws. (`object-rotate-right`, a
candidate, draws nothing in resvg and is not seated.) The extraction's own check: every seat identical to its slot's
crop or within d ≤ 3.0 (mean, of 255) of the shipped 48 PNG, but `user-trash` (DeleteFolder): its mesh pattern sits at 3.03
from the shipped PNG, and the sheet's own crop at 7.7 from it (the crop clips the rim highlight's mask region, so it is
no reference there) — the one seat the tool prints for a look, accepted on the contact sheet. No SVG carries an `<image>`.
`PhaseReset.svg` (2026-10-15), rendered at 240 px through the same resvg 0.48.1: every ink pixel of rsvg's render
drawn, the mean difference 0.3 of 255 (antialiased edges alone). The files of 2026-10-15 / 16 the same, mean difference
to rsvg at 240 px (the resvg 0.48.1 command line, premultiplied): `HistoryView.svg` 0.63,
`Center.svg` 0.34, `FullZoomOut.svg` 0.14, `WaveformMagnification.svg` 0.30; and
`ValueDrag.svg` (2026-10-18) 0.10. The baton's 2048-px PNG (2026-10-18): the product's decode (resvg 0.48.1 at
identity) was byte-identical to PIL's premultiplied decode, and its Lanczos resample within 0.14 of 255 mean of PIL's
`Image.LANCZOS` at 60 px (0.27 at 28, 0.34 at 22). A dropped PNG needs no import check: it is its own picture.

## The drop shadows

EVERY DROP SHADOW IS REMOVED STRUCTURALLY, none kept (architect 2026-10-15, "we'll go with none"): a drop shadow is
the live glyph's raised fiction; Cool Edit's flat bitmaps never had one. Removed: the floor shadows under each
drawing, the blurred halos behind it, the etched halo pairs round the media glyphs, and the shadows one part casts on
another; kept: shading inside a drawing that is no shadow (preferences-system's screwdriver on the wrench). The list
is the one the architect approved icon by icon on a sheet the same day, tracked as
`tools/icon_set_adwaita_322/shadows.tsv`; the extraction removes each id with every `<use>` of a removed element
(LoadInPlace's `use6187`, FolderRow's eleven, HistoryView's `use6074-7`) and fails on a listed id that is missing or a
reference left dangling. The disabled face has no file of its own: the product builds it from the live raster's mask
(`icons::draw_disabled`).

| enumerator | 3.22 name | removed ids | what |
|---|---|---|---|
| OpenProject | `document-open` | `use28040` `use28042` `path4789` | floor shadow (2); the paper's shadow on the folder's back panel |
| Save | `document-save` | `rect2846-1-9-0` `rect2846-1-9-0-1` `path3960-3-8-4-0-5-5-9-33` | floor shadow (2); the arrow's shadow cast on the cabinet |
| Undo | `edit-undo` | `g9944-7-9` | floor shadow |
| Redo | `edit-redo` | `g4285` | floor shadow |
| Render | `media-record` | `g8159` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| SaveCommit | `go-top` | `path6460-6-4-4-0-5` `path6460-6-4-4-3-55-3` | floor shadow (2) |
| SavePull | `go-bottom` | `g6324` | floor shadow |
| Source | `accessories-dictionary` | `path2284` | floor shadow |
| Target | `input-tablet` | `path8537-6` `path23244-7` `path46799-7-4` | floor shadow; the pen's shadow cast on the tablet; the squiggle's dark under-copy |
| PhaseReset | `camera-photo` | `g9724` `g9730` | floor shadow (blurred); the lens's shadow cast on the foot |
| FullZoomOut | `view-restore` | `rect4667-1-5-6-45-3-7-0` | blurred halo behind each window (one rect, two instances) |
| Center | `edit-find` | `g5595` | floor shadow under the handle (`path4263-5` and `path5575`, two blurred radials) |
| WaveformMagnification | `audio-volume-high` | `path16236` | floor shadow (the wide radial under the speaker) |
| DropMarker | `list-add` | `rect4899-2` | floor shadow |
| DeleteMarkers | `list-remove` | `rect4899-2-1` | floor shadow |
| Disabled | `audio-volume-muted` | `path11644` `path10707` | floor shadow (blurred core and the wide radial) |
| Inherit | `insert-link` | `path4121` | floor shadow under the triangle |
| Flatten | `edit-clear-all` | `path6017` | floor shadow |
| Follow | `start-here` | — | no drop shadow |
| GridIterations | `x-office-spreadsheet` | `g12362` | floor shadow |
| PlayRenders | `applications-multimedia` | `path8036` | floor shadow under the note |
| LoadInPlace | `edit-paste` | `rect5387` `rect5796` `path5674` `rect5984` | floor shadow (2); the clip's and the paper's shadows cast on the board |
| HistoryView | `document-open-recent` | `use6074` `use6388-7-7` `g4531` `path7099` | floor shadow (two stacked instances of g6384: use6074 and use6388-7-7); the hands' and the arrow's shadows cast on the dial |
| AddToSelection | `edit-select-all` | `g12362` | floor shadow |
| Older | `media-seek-backward` | `g6598` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| Newer | `media-seek-forward` | `g5161` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| Left | `go-previous` | `path6460-6-4-4` `path6460-6-4-4-3` | floor shadow (2) |
| Right | `go-next` | `path6460-6-4-4-48` `path6460-6-4-4-3-2` | floor shadow (2) |
| Revert | `document-revert` | `g12362` | floor shadow |
| TransportStop | `media-playback-stop` | `g5666` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| PlayerPrevious | `media-skip-backward` | `g4795` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| Play | `media-playback-start` | `g5075` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| Pause | `media-playback-pause` | `g5996` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| PlayerNext | `media-skip-forward` | `g5937` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| Cancel | `process-stop` | `g6501` | floor shadow |
| Down | `go-down` | `path6460-6-4-4-0` `path6460-6-4-4-3-55` | floor shadow (2) |
| Up | `go-up` | `path6460-6-4-4-0-5-8` `path6460-6-4-4-3-55-3-3` | floor shadow (2) |
| ReadOnly | `changes-prevent` | `rect5961-79` | floor shadow |
| PreviousMarker | `go-first` | `path6460-6-4-4-1` `path6460-6-4-4-3-5` `path6460-6-4-4-1-9` `path6460-6-4-4-3-5-5` | floor shadow (4: bar and arrow) |
| NextMarker | `go-last` | `g4226` `g4222` | floor shadow (2 groups: bar and arrow) |
| SwitchTab | `preferences-system-windows` | `rect4667-1-5-6-2` | blurred halo behind each window (one rect, two uses) |
| Settings | `preferences-system` | `path6896` `path3562` | floor shadow (2); the screwdriver's shading on the wrench (rect7166, path7356) kept |
| FolderRow | `folder` | `g5806` | floor shadow |
| WavRow | `audio-x-generic` | `path8186` `path8188` `path8190` | floor shadow (3) |
| RepeatOne | `media-playlist-repeat` | `g7767` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| CardNormal | `dialog-information` | `path4908-7` | floor shadow under the base |
| Close | `window-close` | `path3819` `path2183` `path2187` | floor shadow (3) |
| Tooltips | `help-browser` | `path3662` `path3688` | floor shadow (2, one seen through the hole) |
| JumpToDefiningMarker | `go-jump` | `g4690` | floor shadow |
| OpenTextEditor | `accessories-text-editor` | `path9687` `path4331` `path2334` | blurred halo behind the pad; the pencil's shadows cast on the page |
| DeleteFolder | `user-trash` | `path12888` `path12886` | floor shadow and the contact ellipse under the base |
| ValueDrag | `format-text-direction-rtl` | `path7358` `path7390` | floor shadow (the flattened copy of the pen) and the nib's contact ellipse |

## The shift

The product draws every glyph BODY-CENTERED in its seat (architect 2026-10-12; `src/gui/icons.h`'s THE GLYPHS
BODY-CENTERED): the alpha >= 0.1 bounds of the drawing give the user-unit translate that puts its ink's center at the
frame's center. EVERY SVG THE BUILDER WRITES CARRIES ITS SHIFT ON ITS ROOT TAG, `data-body-shift="dx dy"` (one decimal
each), the builder's own measure (`tools/icon_set_adwaita_322/body_shift.py`: rsvg-convert at 240 px, the method the
2026-10-12 table used, so the numbers are the ones judged then; the table's column below), read by the program from
the tag before parsing, so the launch renders nothing to center them (architect 2026-10-19: the launch "used to be
very snappy"). resvg ignores the attribute, and a symlinked repeat carries its file's. A dropped SVG without the
attribute, and every PNG, is measured at the launch instead (`svg_icon::body_center_shift`; a PNG's measure is free),
so a drop still needs no step; the two measures agree within 0.1 unit on every builder file. The shift is the
product's: nothing else in a file is.

## The icons

The extracted SVGs, `tools/icon_set_adwaita_322/`'s `README_table.md` (the links and the PNGs are not the tool's).

| enumerator | 3.22 name | source sheet (src/fullcolor) | slot (x, y, w, h) | sheet sha256 | `data-body-shift` | note |
|---|---|---|---|---|---|---|
| AddToSelection | `edit-select-all` | `paper-sheets.svg` | 1500, 654, 48, 48 | `1bf2aad02a3644760903ccc36c3a942b6b2a4f408752e7fd7c0dff981e1f1a8b` | -0.1 1.0 |  |
| Cancel | `process-stop` | `navigation-icons.svg` | 696, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | 0.5 -0.5 |  |
| CardNormal | `dialog-information` | `dialog-information.svg` | 296, 50, 48, 48 | `43752d57c0e5a94957279071458888afbefb187bc97e0184f39757d97cffb7c0` | 0.0 1.5 |  |
| Center | `edit-find` | `find.svg` | 300, 50, 48, 48 | `88e1be55349ba868dfc3a45152cb376e50a10d178fe57b422be59d47f20e7ae3` | 0.9 0.4 | NEW: Center (architect 2026-10-16: the magnifier finds the playhead) |
| Close | `window-close` | `window-close.svg` | 296, 50, 48, 48 | `3145ee5b93477957dca1f8235c612e8eff2148227c95ffc53186c78b891a73b2` | -0.7 0.7 |  |
| DeleteFolder | `user-trash` | `trash.svg` | 696, 50, 48, 48 | `940cae7ced59edf15322f1e92cfa1636dade79dff9ecdea5f3dff6b9bfcd72d1` | 0.0 -0.5 |  |
| DeleteMarkers | `list-remove` | `list-add-remove.svg` | 696, 50, 48, 48 | `c9c134fbc80a3342f28b595039c21e179f9775b1682ccdbf0272716092680d61` | -1.0 1.0 |  |
| Disabled | `audio-volume-muted` | `audio-volume.svg` | 421, 335, 256, 256 | `0a1b5e6cc52d57eeb8e864b5d16e88bdb523da79b87e6bdaeff1599d83acbd9b` | -0.5 0.0 |  |
| Down | `go-down` | `navigation-icons.svg` | 186, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | -0.5 1.0 |  |
| DropMarker | `list-add` | `list-add-remove.svg` | 296, 50, 48, 48 | `c9c134fbc80a3342f28b595039c21e179f9775b1682ccdbf0272716092680d61` | -1.0 1.0 |  |
| Flatten | `edit-clear-all` | `edit-clear-all.svg` | 296, 50, 48, 48 | `0e014a4f4050e704872ff34a0b9680d08a42063ff2c7b6e42b3c5d66fb27855d` | 2.6 0.6 | Flatten: the broom (architect 2026-10-14) |
| FolderRow | `folder` | `folders.svg` | 296, 50, 48, 48 | `410c37478763ca92ce06ed3fb49cf998f905aedab1d978b69ff579763ae4b2da` | 0.5 1.0 |  |
| Follow | `start-here` | `start-here.svg` | 296, 50, 48, 48 | `92199123caea23275ed009708da8bad426d05b78d0c1808a44268ed7550a342f` | 0.0 0.0 |  |
| FullZoomOut | `view-restore` | `view-fullscreen-restore.svg` | 696, 50, 48, 48 | `dd9a137b1779bd454c300d351e50ba2f1458c729cb2fc49e890173989409a688` | -1.1 0.5 | NEW: Full zoom out (architect 2026-10-16: the view shrunk back) |
| GridIterations | `x-office-spreadsheet` | `paper-sheets.svg` | 1500, 354, 48, 48 | `1bf2aad02a3644760903ccc36c3a942b6b2a4f408752e7fd7c0dff981e1f1a8b` | -0.1 1.0 |  |
| HistoryView | `document-open-recent` | `clocks.svg` | 696.062, 355.996, 48, 48 | `0411e9fe1933a5e6d6c5d6b577256542fdca27da0655d0db77eaffdcb1b7f7f2` | 0.1 1.0 | History: the clock turned back again (architect 2026-10-18, "go back to using the original git clock icon"); TEXT TO OUTLINES: 48 text elements (Liberation Sans Bold) replaced by glyph outlines from fonts/LiberationSans-Bold.ttf |
| Inherit | `insert-link` | `insert-link.svg` | 296, 50, 48, 48 | `5653605748193e1a61a7b59a7214b416dbdc48f6d7a0944f0a396487e7f8b29e` | 0.0 -5.0 |  |
| JumpToDefiningMarker | `go-jump` | `navigation-icons.svg` | 26, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | -1.0 0.5 |  |
| Left | `go-previous` | `navigation-icons.svg` | 536, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | 0.0 -0.5 |  |
| LoadInPlace | `edit-paste` | `copy-paste-tasks.svg` | 296, 50, 48, 48 | `858cc2773cbd07e48065bf5416d869f0a49d9eabd772556da7099e3e51524f92` | -0.5 1.5 | Load in Place (architect 2026-10-14) |
| Newer | `media-seek-forward` | `media-control-icons.svg` | 406, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` | -0.8 0.0 |  |
| NextMarker | `go-last` | `navigation-icons.svg` | 856, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | 0.5 -0.5 |  |
| Older | `media-seek-backward` | `media-control-icons.svg` | 206, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` | -0.2 0.0 |  |
| OpenProject | `document-open` | `folders.svg` | 1496, 350, 48, 48 | `410c37478763ca92ce06ed3fb49cf998f905aedab1d978b69ff579763ae4b2da` | 0.0 1.4 |  |
| OpenTextEditor | `accessories-text-editor` | `accessories-text-editor.svg` | 296.062, 39.9963, 48, 48 | `f4ada0e92d0d721c4cdf5fabb99c6e5e2cdcbae3ace01ef716b3c0b8fc79524f` | -1.4 1.0 |  |
| Pause | `media-playback-pause` | `media-control-icons.svg` | 256, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` | 0.0 0.0 |  |
| PhaseReset | `camera-photo` | `camera-photo.svg` | 300, 50, 48, 48 | `ef5055cdc4061107476cab8265bebe6e7ddea7c257d844f3d2a54b477e9c194a` | 0.0 1.5 | NEW: Phase reset (architect 2026-10-15: the phase snapshot) |
| Play | `media-playback-start` | `media-control-icons.svg` | 306, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` | -1.0 0.0 |  |
| PlayerNext | `media-skip-forward` | `media-control-icons.svg` | 456, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` | 0.0 0.0 |  |
| PlayerPrevious | `media-skip-backward` | `media-control-icons.svg` | 156, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` | 0.0 0.0 |  |
| PlayRenders | `applications-multimedia` | `applications-multimedia.svg` | 296, 50, 48, 48 | `b4551addc300f3ca7d6c5c9f22044c7db2dc227e3987877cb0b99de634508ed2` | 0.0 -0.3 |  |
| PreviousMarker | `go-first` | `navigation-icons.svg` | 456, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | -0.5 -0.5 |  |
| ReadOnly | `changes-prevent` | `changes.svg` | 296, 50, 48, 48 | `f33d7f5e7945f2c772cca61ca2177ecb28080926a1f53a8b2cbbb63581b8d217` | 1.4 0.5 | NEW: Read-Only, one padlock in both states (architect 2026-10-21) |
| Redo | `edit-redo` | `edit-undo-redo.svg` | 76, 300, 48, 48 | `f898c87c923e161e6a4e2b19ac605f4651fdb8bbbcfcc57f962258e5fd5c97f0` | 0.0 -1.5 |  |
| Render | `media-record` | `media-control-icons.svg` | 506, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` | 0.0 0.0 |  |
| RepeatOne | `media-playlist-repeat` | `media-control-icons.svg` | 56, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` | 0.0 -1.5 |  |
| Revert | `document-revert` | `paper-sheets.svg` | 1900, 654, 48, 48 | `1bf2aad02a3644760903ccc36c3a942b6b2a4f408752e7fd7c0dff981e1f1a8b` | 1.9 1.0 |  |
| Right | `go-next` | `navigation-icons.svg` | 776, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | 0.0 -0.5 |  |
| Save | `document-save` | `cabinets.svg` | 696, 50, 48, 48 | `abefcd60460b10ac8be532acb78197132f8e90b3db97e6d3812c555914f0517f` | 0.5 0.9 | the floppy retired (architect 2026-10-14) |
| SaveCommit | `go-top` | `navigation-icons.svg` | 276, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | -0.5 0.5 |  |
| SavePull | `go-bottom` | `navigation-icons.svg` | 106, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | -0.5 0.5 |  |
| Settings | `preferences-system` | `preferences-system.svg` | 296.062, 49.9963, 48, 48 | `66eab3c0045c5ed6f35bb9512e19d876b6012d585cd157d0de93a0b049f7bce9` | -0.2 0.9 |  |
| Source | `accessories-dictionary` | `accessories-dictionary.svg` | 296, 50, 48, 48 | `86f5650828d5d4a3975dcbb834c1333304d5e4beeb7b6197c908764374f0ba6c` | 0.0 0.9 | NEW: Source (architect 2026-10-14) |
| SwitchTab | `preferences-system-windows` | `preferences-system-windows.svg` | 296, 50, 48, 48 | `72075ba85164fefc23d4296771d6c5db502fafb107a7cf327db049bfb33968ff` | 0.0 0.5 |  |
| Target | `input-tablet` | `input-tablet.svg` | 20, 33, 256, 256 | `3859ab7e3e364780314bfe57d02a46f095266def59a87ef1803108bab00f4b0f` | 0.2 1.2 | NEW: Target (architect 2026-10-14) |
| Tooltips | `help-browser` | `help-browser.svg` | 13, 37, 256, 256 | `86cde422bb8cd8bc966cddd4d14f8d5720f980c83a0feec78c7163122a414a57` | 0.6 0.5 |  |
| TransportStop | `media-playback-stop` | `media-control-icons.svg` | 356, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` | 0.0 0.0 |  |
| Undo | `edit-undo` | `edit-undo-redo.svg` | 16, 300, 48, 48 | `f898c87c923e161e6a4e2b19ac605f4651fdb8bbbcfcc57f962258e5fd5c97f0` | 0.0 -1.5 |  |
| Up | `go-up` | `navigation-icons.svg` | 366, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` | -0.5 1.0 |  |
| ValueDrag | `format-text-direction-rtl` | `format-text-direction-rtl.svg` | 296, 50, 48, 48 | `9adb14cb4bb35582ec9b3f8925ebd4f4278fd8571e420c0c7c97d4cbef584070` | -7.6 0.7 | NEW: Value Drag, the pen alone (architect 2026-10-18: "drop the arrows altogether"); REMOVED (removed.tsv): g7348 |
| WaveformMagnification | `audio-volume-high` | `audio-volume.svg` | 296, 50, 48, 48 | `0a1b5e6cc52d57eeb8e864b5d16e88bdb523da79b87e6bdaeff1599d83acbd9b` | -0.5 0.4 | NEW: Waveform magnification (architect 2026-10-16: the louder waveform) |
| WavRow | `audio-x-generic` | `audio-x-generic.svg` | 296.062, 49.9963, 48, 48 | `c2d7906e7d19b021cabff60aea188b709c2eaa28c69eb7176bca365e9d45ac5d` | 0.8 1.9 |  |
