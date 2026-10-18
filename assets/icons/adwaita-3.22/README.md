# The Adwaita 3.22 icons

adwaita-icon-theme 3.22.0, alone (architect 2026-10-14): every drawing is the 3.22.0 tarball's own `src/fullcolor/`
artwork at its 48 slot, nothing redrawn and nothing from any other release; gnome-icon-theme 2.28, the set before it,
retired whole the same day. THE ONE EXCEPTION is the set's OWN FILE (architect 2026-10-16): the speedometer since
2026-10-17 evening, Warp's glyph and the program icon (`Speedometer.svg`, `AppIcon.svg`), NOT Adwaita's (below, "The own file"). One SVG per `icons::Icon` enumerator (`src/gui/icons.h`), each named by its enumerator.
The set is hard-coded (`icons::kIconSet`, `adwaita-3.22`; the `icons` device key and the Settings Icons row were
removed 2026-10-12). The program parses the files at launch, in place, through resvg (`src/gui/svg_icon.h`): on the
laptop from this folder, on the tablet from the APK's `icons/adwaita-3.22/` assets (`android/app/build_apk.sh`); a
file that is not well-formed SVG fails the launch, naming the file. Files of other names in this folder (this README,
the licence texts) are not read.

## How the files are made

The tarball ships its scalable artwork as SHEETS (`src/fullcolor/<sheet>.svg`, each carrying an icon's drawings at
every size, often several icons), so every file here is EXTRACTED by `tools/icon_set_adwaita_322/` (`build_set.py`
over `mapping.tsv`, its README the usage) and is that tool's byte-identical output (the own file, below, COPIED by
the same tool from its tracked source with its notice inserted, never extracted): the sheet PRUNED IN PLACE —
hidden layers, the editor's metadata and every element that puts no ink in the 48 slot are removed, every remaining
element keeping its ancestors exactly; the slot NESTED as its own `<svg>` viewport inside a 48-unit root (an app
icon with no 48 drawing nests its 256 slot, scaled by that viewport as the theme renders its 48 PNG); a mask or
filter in user space with no region of its own GIVEN, EXPLICITLY, THE REGION THE FULL SHEET'S VIEWPORT GAVE IT
(−10 % / 120 % of the sheet), because the 3.22 `edit-undo` / `edit-redo` masks otherwise clip the arrow to nothing in
every current renderer; EVERY DROP SHADOW REMOVED by id (below, `shadows.tsv`), and the few other elements the
architect rules out (`removed.tsv`, no row today), every `<use>` of a removed element with it; the defs pruned to those referenced. Nothing is redrawn. Each seat's faithful drawing (shadows kept) is
checked against the sheet cropped to its slot and against the theme's own shipped 48 PNG
(`Adwaita/48x48/<context>/<name>.png`).

## Provenance

- adwaita-icon-theme 3.22.0, `adwaita-icon-theme-3.22.0.tar.xz`
  (https://download.gnome.org/sources/adwaita-icon-theme/3.22/), sha256
  `c18bf6e26087d9819a962c77288b291efab25d0419b73d909dd771716a45dcb7`. Each file's source sheet and its sha256 are in
  the table below.
- The own file, `Speedometer.svg` and its copy `AppIcon.svg`: the project's own, its source
  `tools/icon_set_adwaita_322/own/Speedometer.svg` (below, "The own file").

## Licence

The tarball's `COPYING`, copied here verbatim as `COPYING-adwaita-icon-theme-3.22.0`: "This work is licenced under
the terms of either the GNU LGPL v3 or Creative Commons Attribution-Share Alike 3.0 United States License … When
attributing the artwork, using 'GNOME Project' is enough." The two licence texts it names are here verbatim as
`COPYING_LGPL-adwaita-icon-theme-3.22.0` and `COPYING_CCBYSA3-adwaita-icon-theme-3.22.0`, its `AUTHORS` as
`AUTHORS-adwaita-icon-theme-3.22.0`.

How the files sit beside the GPL-3 program: each SVG is a separate data file under its own licence, read at run time
and never linked or compiled into the program's code; the repository distributes source only (THIRD_PARTY.md's own
statement). Under LGPL-3 or CC BY-SA 3.0 there is no GPL-2-only concern. The own file's drawing is licensed under the
GNU LGPL v3, the set's own licence (architect 2026-10-15: "the icon licensing could go either way … the LGPL3"); it
carries no lettering and no font's outlines.

## The own file

`Speedometer.svg` (2026-10-17 evening), and `AppIcon.svg` byte for byte, is NOT from adwaita-icon-theme: Warp's
glyph and the program icon (`tools/app_icon/gen_app_icon.sh`), the tempo instrument (architect 2026-10-17 evening, on
laptop sheets: "c1. ok to implement and install"). PROVENANCE: drawn with Google's Gemini at the architect's prompt,
2026-10-17, against a style sheet of Lapo Calamandrei's glossy Adwaita 3.22 drawings; the second of four drawings,
in the 48-unit frame (viewBox 0 0 48 48, no nested viewport). THE CLEANUP, the same evening: its three `<text>`
numerals (0, 120, 240) deleted with their emptied group and its font attributes (the product's resvg draws no text);
its ground-shadow ellipse deleted with the radial gradient that filled it (the set carries no drop shadow, below);
the needle's dark offset copy and the hub's dark disc, the shadows they cast on the face, deleted from the tracked
source the same evening by the same rule (architect: "follow the rule"; kept as mocked for the first landing); every
other element as drawn, Gemini's other comments kept. THE TINT, variant C1 of a one-axis sheet: the five chrome
gradients — the outer bezel, its border, the inner bevel ring, the hub rim and the hub cap — re-tinted from Tango
aluminium (hue 60–90, saturation 3–9 %, a faint warmth) to hue 210 at saturation 6 %, every stop's HLS lightness kept
(a stop at lightness .02 or less, or .98 or more, stays grey). THE DRAWING: centre (24, 23); a chrome bezel r 20.5
with a .8 stroke and a white specular ring inside it, an inner bevel ring and a dark recessed frame round the dark
radial face r 16.2, a glass gloss over its upper part; a white scale arc and nine white majors with ten minors on a
270° arc from −135° to +135°, its last 45° a red gradient arc with red ticks and a soft red glow; a tapered red needle
at +52° from twelve o'clock with a bright ridge line; a chrome hub cap.
No `<text>`, no filter, no CSS class, no font's outlines. LICENCE: above. Its tracked source is
`tools/icon_set_adwaita_322/own/Speedometer.svg`; the tool copies it with a dated notice inserted as one XML comment
after the root's start tag, every other byte verbatim (`mapping.tsv`'s `own` treatment). HISTORY: the architect's
digital metronome, Warp's glyph and the program icon from 2026-10-16 to 2026-10-17 evening (git history).

## The changed files

Every extracted file is changed in one respect, its drop shadows removed (2026-10-15, the section below, recorded here
and in `tools/icon_set_adwaita_322/shadows.tsv`; the own file, above, is no extraction and has none). Two drawings
carry a further change, each recorded under the licence with a dated notice in an XML comment at its head; every other
file is the extraction alone. One file carries outlined lettering, the clock's digits here (Liberation Sans Bold); the
own file carries none.

- `TabDetachB.svg` (2026-10-14) is `preferences-system-windows` with the element order changed alone: the lower-right
  window copy `use5281` moved after the front window `g5270`, every coordinate unchanged — the same derivation 2.28's
  TabDetachB had. Switch Tab wears `TabDetach.svg` on tab A and this file on tab B: the same two windows, the one in
  front the tab shown.
- `DocumentOpenRecent.svg` (2026-10-14; history's again since 2026-10-16) is `document-open-recent` (the clock in
  `clocks.svg`, its yellow arrow turned counter-clockwise) with its 48 clock-digit `<text>` elements replaced by the
  glyph outlines of the face the sheet names, Liberation Sans Bold, from the repository's own
  `fonts/LiberationSans-Bold.ttf` (`tools/icon_set_adwaita_322/text_to_paths.py`, `mapping.tsv`'s `text-to-outlines`
  treatment: the face's own advances and its GPOS pair kerning, text-anchor honored), because the product's resvg is
  built without text and draws none. The outline geometry is what a text renderer lays down from the same file,
  measured 0.26/255 mean against rsvg's own text rendering. It is the drawing history wore 2026-10-14 to 2026-10-15
  as `VcsDiff.svg`, rendered identically (rsvg at 240 and 60 px, byte-identical PNGs); its bytes differ from that
  file's in the notice alone, which names the new enumerator and the drop shadows' removal.

## The deliberate seats

The architect's (2026-10-14): Source wears `accessories-dictionary` (the primary source), Target `input-tablet` (the
authoring surface); since 2026-10-16 Warp the own file, the speedometer since 2026-10-17 evening (its history above;
appointment-new's clock of 2026-10-15 retired: "it fits in theory, but it just didn't fit in practice"), and since 2026-10-15 Phase Reset `camera-photo` (the phase
snapshot: a reset re-grounds the synthesis phase to the source's at one frame, and it decoheres from there), its 48 slot (300, 50) — the sheet's 256 "hires" slot is never preferred over a 48
drawing, which is tuned for the seats; Toggle History View `document-open-recent`, the clock turned back (architect
2026-10-16, "give history the clock with backarrow back"; `x-office-calendar`, history's for the night of 2026-10-15,
retired) and Toggle Session History `network-wired-disconnected` (Session
shuts off git and stays local: the cable unplugged, 2026-10-15); since 2026-10-16 Center `edit-find` (the magnifier
finds the playhead; its 48 slot (300, 50), the sheet's other being edit-find-replace's), Full Zoom Out `view-restore`
(the view shrunk back; the second 48 slot of `view-fullscreen-restore.svg`, (696.00012, 50.000008), proved against the
shipped `view-restore.png` at d 0.65 where the first slot sits at 54.7) and Toggle Waveform Magnification
`audio-volume-high` (the loud speaker for the louder waveform), zoom.svg's three magnifiers retired ("low contrast"); the
Read-Only toggle the padlock pair, `changes-allow` unlocked and `changes-prevent` locked; Save `document-save` (the
floppy retired); Flatten `edit-clear-all` (3.22's own broom); Load in Place `edit-paste`. DialogError wears
`process-stop`, as DialogCancel does. Every other seat keeps its freedesktop name.

REPEATS, the files byte-identical: AppIcon and Speedometer (the own file); AudioXWav and MusicNote16th
(`audio-x-generic`; the BPM Iterations button keeps the note, the program icon until 2026-10-15); DialogCancel and
DialogError (`process-stop`).

## Findings

- `multimedia-volume-control`, the dial, is excluded (again 2026-10-15): every slot's knob (16, 22, 32, 48 and the
  hires) is an embedded raster inside the SVG, a 512-px bitmap at the large slots; no vector knob exists.
- `format-text-direction-ltr` is PNG-only in 3.22 (no scalable source) and is not a mirror of `-rtl`, so the two are
  no Source / Target pair.
- `network-wired-disconnected` is VECTOR at its 48 slot (2026-10-16): its sheet carries no `<image>`, no `<pattern>` and
  no `data:` URI, the extraction is the slot's crop exactly (rsvg, 240 px, max difference 0) and within d 1.0 of the
  theme's shipped 48 PNG — the PNG is the theme's render of this drawing.
- The clock's digits (`document-open-recent`) are `<text>`, which the product's resvg does not draw — hence
  DocumentOpenRecent's outlines, above.
- `find.svg` carries an `<image>` and a `<pattern>`, neither in edit-find's 48 slot nor in `EditFind.svg`. That file
  does carry one `<text>` ("TAN-GO", the edit-find-replace pencil's legend in the same layer) outside its 48 viewport:
  it puts no ink in the slot, and the product's resvg skips text.

## The import check

Every file was rendered at the 60-px seat on the band's case through resvg 0.48.1, the product's pin, with the
generator's body shift applied, onto one contact sheet and looked at: every file draws. (`object-rotate-right`, a
candidate, draws nothing in resvg and is not seated.) The extraction's own check: every seat identical to its slot's
crop or within d ≤ 3.0 (mean, of 255) of the shipped 48 PNG, but `user-trash` (EditDelete): its mesh pattern sits at 3.03
from the shipped PNG, and the sheet's own crop at 7.7 from it (the crop clips the rim highlight's mask region, so it is
no reference there) — the one seat the tool prints for a look, accepted on the contact sheet. No file carries an `<image>`.
`CameraPhoto.svg` (2026-10-15), rendered at 240 px through the same resvg 0.48.1: every ink pixel of rsvg's render
drawn, the mean difference 0.3 of 255 (antialiased edges alone). The files of 2026-10-15 / 16 the same, mean difference
to rsvg at 240 px: `Speedometer.svg` 0.37 (measured 2026-10-17 evening with the resvg 0.48.1 command line, premultiplied, and again after the two cast shadows' removal; 272 of rsvg's 34,710 ink pixels past 20 levels, at most 42, every one on an antialiased edge — the bezel's outer rim and the scale's ticks and arcs — nothing drawn lost; against Inkscape at 480 px max 51, 99.9th percentile 25, mean 0.29, the maxima on the red glow's gradient edge, the maximum unchanged after the removal),
`NetworkWiredDisconnected.svg` 0.37, `EditFind.svg` 0.34, `ViewRestore.svg` 0.14, `AudioVolumeHigh.svg` 0.30.

## The drop shadows

EVERY DROP SHADOW IS REMOVED STRUCTURALLY, none kept (architect 2026-10-15, "we'll go with none"): a drop shadow is
the live glyph's raised fiction; Cool Edit's flat bitmaps never had one. Removed: the floor shadows under each
drawing, the blurred halos behind it, the etched halo pairs round the media glyphs, and the shadows one part casts on
another; kept: shading inside a drawing that is no shadow (preferences-system's screwdriver on the wrench). The list
is the one the architect approved icon by icon on a sheet the same day, tracked as
`tools/icon_set_adwaita_322/shadows.tsv`; the extraction removes each id with every `<use>` of a removed element
(DialogOkApply's `use6187`, Folder's eleven, DocumentOpenRecent's `use6074-7`) and fails on a listed id that is missing or a
reference left dangling. The disabled face has no file of its own: the product builds it from the live raster's mask
(`icons::draw_disabled`).

| enumerator | 3.22 name | removed ids | what |
|---|---|---|---|
| DocumentOpen | `document-open` | `use28040` `use28042` `path4789` | floor shadow (2); the paper's shadow on the folder's back panel |
| DocumentSave | `document-save` | `rect2846-1-9-0` `rect2846-1-9-0-1` `path3960-3-8-4-0-5-5-9-33` | floor shadow (2); the arrow's shadow cast on the cabinet |
| EditUndo | `edit-undo` | `g9944-7-9` | floor shadow |
| EditRedo | `edit-redo` | `g4285` | floor shadow |
| MediaRecord | `media-record` | `g8159` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| VcsCommit | `go-top` | `path6460-6-4-4-0-5` `path6460-6-4-4-3-55-3` | floor shadow (2) |
| VcsPull | `go-bottom` | `g6324` | floor shadow |
| AccessoriesDictionary | `accessories-dictionary` | `path2284` | floor shadow |
| InputTablet | `input-tablet` | `path8537-6` `path23244-7` `path46799-7-4` | floor shadow; the pen's shadow cast on the tablet; the squiggle's dark under-copy |
| Speedometer | the own file | — | no drop shadow (the ground-shadow ellipse removed before the file entered the tree, 2026-10-17 evening; the needle's and the hub's dark copies cast on the face removed from the tracked source the same evening, "follow the rule") |
| CameraPhoto | `camera-photo` | `g9724` `g9730` | floor shadow (blurred); the lens's shadow cast on the foot |
| ViewRestore | `view-restore` | `rect4667-1-5-6-45-3-7-0` | blurred halo behind each window (one rect, two instances) |
| EditFind | `edit-find` | `g5595` | floor shadow under the handle (`path4263-5` and `path5575`, two blurred radials) |
| AudioVolumeHigh | `audio-volume-high` | `path16236` | floor shadow (the wide radial under the speaker) |
| ListAdd | `list-add` | `rect4899-2` | floor shadow |
| ListRemove | `list-remove` | `rect4899-2-1` | floor shadow |
| ViewHidden | `audio-volume-muted` | `path11644` `path10707` | floor shadow (blurred core and the wide radial) |
| InsertLink | `insert-link` | `path4121` | floor shadow under the triangle |
| Merge | `edit-clear-all` | `path6017` | floor shadow |
| BlackSum | `accessories-calculator` | `rect12531` | blurred halo behind the calculator |
| GoJump | `start-here` | — | no drop shadow |
| MusicNote16th | `audio-x-generic` | `path8186` `path8188` `path8190` | floor shadow (3) |
| Mathmode | `x-office-spreadsheet` | `g12362` | floor shadow |
| PreviewRenderOn | `applications-multimedia` | `path8036` | floor shadow under the note |
| DialogOkApply | `edit-paste` | `rect5387` `rect5796` `path5674` `rect5984` | floor shadow (2); the clip's and the paper's shadows cast on the board |
| DocumentOpenRecent | `document-open-recent` | `use6074` `use6388-7-7` `g4531` `path7099` | floor shadow (two stacked instances of g6384: use6074 and use6388-7-7); the hands' and the arrow's shadows cast on the dial |
| NetworkWiredDisconnected | `network-wired-disconnected` | `path7840` `rect7696` `path7678` | floor shadow under the plug; the blurred halo behind the wall plate; the cable's blurred shadow |
| EditSelect | `edit-select-all` | `g12362` | floor shadow |
| KeyframePrevious | `media-seek-backward` | `g6598` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| KeyframeNext | `media-seek-forward` | `g5161` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| GoPrevious | `go-previous` | `path6460-6-4-4` `path6460-6-4-4-3` | floor shadow (2) |
| GoNext | `go-next` | `path6460-6-4-4-48` `path6460-6-4-4-3-2` | floor shadow (2) |
| DocumentRevert | `document-revert` | `g12362` | floor shadow |
| MediaSkipBackward | `media-skip-backward` | `g4795` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| MediaPlaybackStart | `media-playback-start` | `g5075` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| MediaPlaybackStop | `media-playback-stop` | `g5666` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| MediaPlaybackPause | `media-playback-pause` | `g5996` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| MediaSkipForward | `media-skip-forward` | `g5937` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| DialogCancel | `process-stop` | `g6501` | floor shadow |
| GoDown | `go-down` | `path6460-6-4-4-0` `path6460-6-4-4-3-55` | floor shadow (2) |
| GoUp | `go-up` | `path6460-6-4-4-0-5-8` `path6460-6-4-4-3-55-3-3` | floor shadow (2) |
| ChangesAllow | `changes-allow` | `rect5961` | floor shadow |
| ChangesPrevent | `changes-prevent` | `rect5961-79` | floor shadow |
| BboxPrev | `go-first` | `path6460-6-4-4-1` `path6460-6-4-4-3-5` `path6460-6-4-4-1-9` `path6460-6-4-4-3-5-5` | floor shadow (4: bar and arrow) |
| BboxNext | `go-last` | `g4226` `g4222` | floor shadow (2 groups: bar and arrow) |
| TabDetach | `preferences-system-windows` | `rect4667-1-5-6-2` | blurred halo behind each window (one rect, two uses) |
| TabDetachB | `preferences-system-windows` | `rect4667-1-5-6-2` | blurred halo behind each window (one rect, two uses) |
| SettingsConfigure | `preferences-system` | `path6896` `path3562` | floor shadow (2); the screwdriver's shading on the wrench (rect7166, path7356) kept |
| Folder | `folder` | `g5806` | floor shadow |
| AudioXWav | `audio-x-generic` | `path8186` `path8188` `path8190` | floor shadow (3) |
| MediaRepeatSingle | `media-playlist-repeat` | `g7767` | the etched halo pair round the glyph (a dark ring and a light ring offset below, one blurred group at 50 %) |
| DialogInformation | `dialog-information` | `path4908-7` | floor shadow under the base |
| DialogError | `process-stop` | `g6501` | floor shadow |
| WindowClose | `window-close` | `path3819` `path2183` `path2187` | floor shadow (3) |
| HelpWhatsthis | `help-browser` | `path3662` `path3688` | floor shadow (2, one seen through the hole) |
| GoJumpDeclaration | `go-jump` | `g4690` | floor shadow |
| AccessoriesTextEditor | `accessories-text-editor` | `path9687` `path4331` `path2334` | blurred halo behind the pad; the pencil's shadows cast on the page |
| EditDelete | `user-trash` | `path12888` `path12886` | floor shadow and the contact ellipse under the base |
| ValueDrag | `object-flip-vertical` | `g10278` | floor shadow |
| AppIcon | the own file | — | Speedometer's: no drop shadow (the ground shadow and the two cast shadows out) |

## The shift

The product draws every glyph BODY-CENTERED in its seat (architect 2026-10-12): a user-unit translate per file,
`src/gui/icon_shifts.inc`, generated by `tools/icon_center/gen_icon_shifts.py` (the body is the shipped file itself,
shadow-less by construction). The files stay as the tool writes them; the
shift is the product's, not the file's.

## The icons

| enumerator | 3.22 name | source sheet (src/fullcolor, or own/ beside this tool) | slot (x, y, w, h) | sheet sha256 | note |
|---|---|---|---|---|---|
| AccessoriesDictionary | `accessories-dictionary` | `accessories-dictionary.svg` | 296, 50, 48, 48 | `86f5650828d5d4a3975dcbb834c1333304d5e4beeb7b6197c908764374f0ba6c` | NEW: Source (architect 2026-10-14) |
| AccessoriesTextEditor | `accessories-text-editor` | `accessories-text-editor.svg` | 296.062, 39.9963, 48, 48 | `f4ada0e92d0d721c4cdf5fabb99c6e5e2cdcbae3ace01ef716b3c0b8fc79524f` |  |
| AppIcon | — | `own/Speedometer.svg` | — | `28790b819d460fc4527ee3e4e1893db2318a09c889080a967a4a9d6a8e009609` | OWN FILE, Speedometer's: the program icon (architect 2026-10-17 evening) |
| AudioVolumeHigh | `audio-volume-high` | `audio-volume.svg` | 296, 50, 48, 48 | `0a1b5e6cc52d57eeb8e864b5d16e88bdb523da79b87e6bdaeff1599d83acbd9b` | NEW: Waveform magnification (architect 2026-10-16: the louder waveform) |
| AudioXWav | `audio-x-generic` | `audio-x-generic.svg` | 296.062, 49.9963, 48, 48 | `c2d7906e7d19b021cabff60aea188b709c2eaa28c69eb7176bca365e9d45ac5d` |  |
| BboxNext | `go-last` | `navigation-icons.svg` | 856, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| BboxPrev | `go-first` | `navigation-icons.svg` | 456, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| BlackSum | `accessories-calculator` | `accessories-calculator.svg` | 296, 50, 48, 48 | `aeafa0e25eb404c73da589337ddb6dfb1aeec8ab2663a7038702c9379de912fb` |  |
| CameraPhoto | `camera-photo` | `camera-photo.svg` | 300, 50, 48, 48 | `ef5055cdc4061107476cab8265bebe6e7ddea7c257d844f3d2a54b477e9c194a` | NEW: Phase reset (architect 2026-10-15: the phase snapshot) |
| ChangesAllow | `changes-allow` | `changes.svg` | 696, 50, 48, 48 | `f33d7f5e7945f2c772cca61ca2177ecb28080926a1f53a8b2cbbb63581b8d217` | NEW: Read-Only while unlocked (architect 2026-10-14) |
| ChangesPrevent | `changes-prevent` | `changes.svg` | 296, 50, 48, 48 | `f33d7f5e7945f2c772cca61ca2177ecb28080926a1f53a8b2cbbb63581b8d217` | NEW: Read-Only while locked (architect 2026-10-14) |
| DialogCancel | `process-stop` | `navigation-icons.svg` | 696, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| DialogError | `process-stop` | `navigation-icons.svg` | 696, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| DialogInformation | `dialog-information` | `dialog-information.svg` | 296, 50, 48, 48 | `43752d57c0e5a94957279071458888afbefb187bc97e0184f39757d97cffb7c0` |  |
| DialogOkApply | `edit-paste` | `copy-paste-tasks.svg` | 296, 50, 48, 48 | `858cc2773cbd07e48065bf5416d869f0a49d9eabd772556da7099e3e51524f92` | Load in Place (architect 2026-10-14) |
| DocumentOpen | `document-open` | `folders.svg` | 1496, 350, 48, 48 | `410c37478763ca92ce06ed3fb49cf998f905aedab1d978b69ff579763ae4b2da` |  |
| DocumentOpenRecent | `document-open-recent` | `clocks.svg` | 696.062, 355.996, 48, 48 | `0411e9fe1933a5e6d6c5d6b577256542fdca27da0655d0db77eaffdcb1b7f7f2` | History: the clock turned back (architect 2026-10-16); TEXT TO OUTLINES: 48 text elements (Liberation Sans Bold) replaced by glyph outlines from fonts/LiberationSans-Bold.ttf |
| DocumentRevert | `document-revert` | `paper-sheets.svg` | 1900, 654, 48, 48 | `1bf2aad02a3644760903ccc36c3a942b6b2a4f408752e7fd7c0dff981e1f1a8b` |  |
| DocumentSave | `document-save` | `cabinets.svg` | 696, 50, 48, 48 | `abefcd60460b10ac8be532acb78197132f8e90b3db97e6d3812c555914f0517f` | the floppy retired (architect 2026-10-14) |
| EditDelete | `user-trash` | `trash.svg` | 696, 50, 48, 48 | `940cae7ced59edf15322f1e92cfa1636dade79dff9ecdea5f3dff6b9bfcd72d1` |  |
| EditFind | `edit-find` | `find.svg` | 300, 50, 48, 48 | `88e1be55349ba868dfc3a45152cb376e50a10d178fe57b422be59d47f20e7ae3` | NEW: Center (architect 2026-10-16: the magnifier finds the playhead) |
| EditRedo | `edit-redo` | `edit-undo-redo.svg` | 76, 300, 48, 48 | `f898c87c923e161e6a4e2b19ac605f4651fdb8bbbcfcc57f962258e5fd5c97f0` |  |
| EditSelect | `edit-select-all` | `paper-sheets.svg` | 1500, 654, 48, 48 | `1bf2aad02a3644760903ccc36c3a942b6b2a4f408752e7fd7c0dff981e1f1a8b` |  |
| EditUndo | `edit-undo` | `edit-undo-redo.svg` | 16, 300, 48, 48 | `f898c87c923e161e6a4e2b19ac605f4651fdb8bbbcfcc57f962258e5fd5c97f0` |  |
| Folder | `folder` | `folders.svg` | 296, 50, 48, 48 | `410c37478763ca92ce06ed3fb49cf998f905aedab1d978b69ff579763ae4b2da` |  |
| GoDown | `go-down` | `navigation-icons.svg` | 186, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| GoJump | `start-here` | `start-here.svg` | 296, 50, 48, 48 | `92199123caea23275ed009708da8bad426d05b78d0c1808a44268ed7550a342f` |  |
| GoJumpDeclaration | `go-jump` | `navigation-icons.svg` | 26, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| GoNext | `go-next` | `navigation-icons.svg` | 776, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| GoPrevious | `go-previous` | `navigation-icons.svg` | 536, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| GoUp | `go-up` | `navigation-icons.svg` | 366, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| HelpWhatsthis | `help-browser` | `help-browser.svg` | 13, 37, 256, 256 | `86cde422bb8cd8bc966cddd4d14f8d5720f980c83a0feec78c7163122a414a57` |  |
| InputTablet | `input-tablet` | `input-tablet.svg` | 20, 33, 256, 256 | `3859ab7e3e364780314bfe57d02a46f095266def59a87ef1803108bab00f4b0f` | NEW: Target (architect 2026-10-14) |
| InsertLink | `insert-link` | `insert-link.svg` | 296, 50, 48, 48 | `5653605748193e1a61a7b59a7214b416dbdc48f6d7a0944f0a396487e7f8b29e` |  |
| KeyframeNext | `media-seek-forward` | `media-control-icons.svg` | 406, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` |  |
| KeyframePrevious | `media-seek-backward` | `media-control-icons.svg` | 206, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` |  |
| ListAdd | `list-add` | `list-add-remove.svg` | 296, 50, 48, 48 | `c9c134fbc80a3342f28b595039c21e179f9775b1682ccdbf0272716092680d61` |  |
| ListRemove | `list-remove` | `list-add-remove.svg` | 696, 50, 48, 48 | `c9c134fbc80a3342f28b595039c21e179f9775b1682ccdbf0272716092680d61` |  |
| Mathmode | `x-office-spreadsheet` | `paper-sheets.svg` | 1500, 354, 48, 48 | `1bf2aad02a3644760903ccc36c3a942b6b2a4f408752e7fd7c0dff981e1f1a8b` |  |
| MediaPlaybackPause | `media-playback-pause` | `media-control-icons.svg` | 256, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` |  |
| MediaPlaybackStart | `media-playback-start` | `media-control-icons.svg` | 306, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` |  |
| MediaPlaybackStop | `media-playback-stop` | `media-control-icons.svg` | 356, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` |  |
| MediaRecord | `media-record` | `media-control-icons.svg` | 506, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` |  |
| MediaRepeatSingle | `media-playlist-repeat` | `media-control-icons.svg` | 56, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` |  |
| MediaSkipBackward | `media-skip-backward` | `media-control-icons.svg` | 156, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` |  |
| MediaSkipForward | `media-skip-forward` | `media-control-icons.svg` | 456, 150, 48, 48 | `253ef37680b01b4be919bade07de9d6190f7258e14b53d2ba15d49d62f5d9fcc` |  |
| Merge | `edit-clear-all` | `edit-clear-all.svg` | 296, 50, 48, 48 | `0e014a4f4050e704872ff34a0b9680d08a42063ff2c7b6e42b3c5d66fb27855d` | Flatten: the broom (architect 2026-10-14) |
| MusicNote16th | `audio-x-generic` | `audio-x-generic.svg` | 296.062, 49.9963, 48, 48 | `c2d7906e7d19b021cabff60aea188b709c2eaa28c69eb7176bca365e9d45ac5d` |  |
| NetworkWiredDisconnected | `network-wired-disconnected` | `network-wired-disconnected.svg` | 296, 50, 48, 48 | `4c9d212cedd9877315cc1e0b05685a582bea53d517ff798bf734e6be9639047e` | NEW: Session (architect 2026-10-15: shut off git and stay local) |
| PreviewRenderOn | `applications-multimedia` | `applications-multimedia.svg` | 296, 50, 48, 48 | `b4551addc300f3ca7d6c5c9f22044c7db2dc227e3987877cb0b99de634508ed2` |  |
| SettingsConfigure | `preferences-system` | `preferences-system.svg` | 296.062, 49.9963, 48, 48 | `66eab3c0045c5ed6f35bb9512e19d876b6012d585cd157d0de93a0b049f7bce9` |  |
| Speedometer | — | `own/Speedometer.svg` | — | `28790b819d460fc4527ee3e4e1893db2318a09c889080a967a4a9d6a8e009609` | OWN FILE, not Adwaita's: Warp (architect 2026-10-17 evening), above |
| TabDetach | `preferences-system-windows` | `preferences-system-windows.svg` | 296, 50, 48, 48 | `72075ba85164fefc23d4296771d6c5db502fafb107a7cf327db049bfb33968ff` |  |
| TabDetachB | `preferences-system-windows` | `preferences-system-windows.svg` | 296, 50, 48, 48 | `72075ba85164fefc23d4296771d6c5db502fafb107a7cf327db049bfb33968ff` | DERIVED, below |
| ValueDrag | `object-flip-vertical` | `object-flip-rotate.svg` | 76.0001, 300, 48, 48 | `cd9be5ee2271c9a797d49aa9749f70c4ad48f53768f651c166560baba865afa1` |  |
| VcsCommit | `go-top` | `navigation-icons.svg` | 276, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| VcsPull | `go-bottom` | `navigation-icons.svg` | 106, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| ViewHidden | `audio-volume-muted` | `audio-volume.svg` | 421, 335, 256, 256 | `0a1b5e6cc52d57eeb8e864b5d16e88bdb523da79b87e6bdaeff1599d83acbd9b` |  |
| ViewRestore | `view-restore` | `view-fullscreen-restore.svg` | 696, 50, 48, 48 | `dd9a137b1779bd454c300d351e50ba2f1458c729cb2fc49e890173989409a688` | NEW: Full zoom out (architect 2026-10-16: the view shrunk back) |
| WindowClose | `window-close` | `window-close.svg` | 296, 50, 48, 48 | `3145ee5b93477957dca1f8235c612e8eff2148227c95ffc53186c78b891a73b2` |  |
