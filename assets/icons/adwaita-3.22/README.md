# The Adwaita 3.22 icons

adwaita-icon-theme 3.22.0, alone (architect 2026-10-14): every drawing is the 3.22.0 tarball's own `src/fullcolor/`
artwork at its 48 slot, nothing redrawn and nothing from any other release; gnome-icon-theme 2.28, the set before it,
retired whole the same day. One SVG per `icons::Icon` enumerator (`src/gui/icons.h`), each named by its enumerator.
The set is hard-coded (`icons::kIconSet`, `adwaita-3.22`; the `icons` device key and the Settings Icons row were
removed 2026-10-12). The program parses the files at launch, in place, through resvg (`src/gui/svg_icon.h`): on the
laptop from this folder, on the tablet from the APK's `icons/adwaita-3.22/` assets (`android/app/build_apk.sh`); a
file that is not well-formed SVG fails the launch, naming the file. Files of other names in this folder (this README,
the licence texts) are not read.

## How the files are made

The tarball ships its scalable artwork as SHEETS (`src/fullcolor/<sheet>.svg`, each carrying an icon's drawings at
every size, often several icons), so every file here is EXTRACTED by `tools/icon_set_adwaita_322/` (`build_set.py`
over `mapping.tsv`, its README the usage) and is that tool's byte-identical output: the sheet PRUNED IN PLACE —
hidden layers, the editor's metadata and every element that puts no ink in the 48 slot are removed, every remaining
element keeping its ancestors exactly; the slot NESTED as its own `<svg>` viewport inside a 48-unit root (an app
icon with no 48 drawing nests its 256 slot, scaled by that viewport as the theme renders its 48 PNG); a mask or
filter in user space with no region of its own GIVEN, EXPLICITLY, THE REGION THE FULL SHEET'S VIEWPORT GAVE IT
(−10 % / 120 % of the sheet), because the 3.22 `edit-undo` / `edit-redo` masks otherwise clip the arrow to nothing in
every current renderer; the defs pruned to those referenced. Nothing is redrawn. Each seat is checked against the
sheet cropped to its slot and against the theme's own shipped 48 PNG (`Adwaita/48x48/<context>/<name>.png`).

## Provenance

- adwaita-icon-theme 3.22.0, `adwaita-icon-theme-3.22.0.tar.xz`
  (https://download.gnome.org/sources/adwaita-icon-theme/3.22/), sha256
  `c18bf6e26087d9819a962c77288b291efab25d0419b73d909dd771716a45dcb7`. Each file's source sheet and its sha256 are in
  the table below.

## Licence

The tarball's `COPYING`, copied here verbatim as `COPYING-adwaita-icon-theme-3.22.0`: "This work is licenced under
the terms of either the GNU LGPL v3 or Creative Commons Attribution-Share Alike 3.0 United States License … When
attributing the artwork, using 'GNOME Project' is enough." The two licence texts it names are here verbatim as
`COPYING_LGPL-adwaita-icon-theme-3.22.0` and `COPYING_CCBYSA3-adwaita-icon-theme-3.22.0`, its `AUTHORS` as
`AUTHORS-adwaita-icon-theme-3.22.0`.

How the files sit beside the GPL-3 program: each SVG is a separate data file under its own licence, read at run time
and never linked or compiled into the program's code; the repository distributes source only (THIRD_PARTY.md's own
statement). Under LGPL-3 or CC BY-SA 3.0 there is no GPL-2-only concern.

## The two changed files

Both are recorded as changed files under the licence, each with a dated notice in an XML comment at its head; every
other file is the extraction alone.

- `TabDetachB.svg` (2026-10-14) is `preferences-system-windows` with the element order changed alone: the lower-right
  window copy `use5281` moved after the front window `g5270`, every coordinate unchanged — the same derivation 2.28's
  TabDetachB had. Switch Tab wears `TabDetach.svg` on tab A and this file on tab B: the same two windows, the one in
  front the tab shown.
- `VcsDiff.svg` (2026-10-14) is `document-open-recent` (the clock in `clocks.svg`) with its 48 clock-digit `<text>`
  elements replaced by the glyph outlines of the face the sheet names, Liberation Sans Bold, from the repository's own
  `fonts/LiberationSans-Bold.ttf` (`tools/icon_set_adwaita_322/text_to_paths.py`: the face's own advances and its GPOS
  pair kerning, text-anchor honored), because the product's resvg is built without text and draws none. The outline
  geometry is what a text renderer lays down from the same file, measured 0.26/255 mean against rsvg's own text
  rendering.

## The deliberate seats

The architect's (2026-10-14): Source wears `accessories-dictionary` (the primary source), Target `input-tablet` (the
authoring surface), Warp `mail-mark-junk` (the warped sheet), Phase Reset `weather-fog` (the waves in phase); the
Read-Only toggle the padlock pair, `changes-allow` unlocked and `changes-prevent` locked; Save `document-save` (the
floppy retired); Flatten `edit-clear-all` (3.22's own broom); Load in Place `edit-paste`. DialogError wears
`process-stop`, as DialogCancel does. Every other seat keeps its freedesktop name.

REPEATS, the files byte-identical: AppIcon, AudioXWav and MusicNote16th (`audio-x-generic`); DialogCancel and
DialogError (`process-stop`).

## Findings

- `multimedia-volume-control`, the dial, is excluded: its knob is a 512-px bitmap inside the SVG, not a drawing.
- `format-text-direction-ltr` is PNG-only in 3.22 (no scalable source) and is not a mirror of `-rtl`, so the two are
  no Source / Target pair.
- The clock's digits (`document-open-recent`) are `<text>`, which the product's resvg does not draw — hence VcsDiff's
  outlines, above.

## The import check

Every file was rendered at the 60-px seat on the band's case through resvg 0.48.1, the product's pin, with the
generator's body shift applied, onto one contact sheet and looked at: every file draws. (`object-rotate-right`, a
candidate, draws nothing in resvg and is not seated.) The extraction's own check: every seat identical to its slot's
crop or within d ≤ 3.0 (mean, of 255) of the shipped 48 PNG, but `user-trash` (EditDelete): its mesh pattern sits at 3.03
from the shipped PNG, and the sheet's own crop at 7.7 from it (the crop clips the rim highlight's mask region, so it is
no reference there) — the one seat the tool prints for a look, accepted on the contact sheet. No file carries an `<image>`.

## The shift

The product draws every glyph BODY-CENTERED in its seat (architect 2026-10-12): a user-unit translate per file,
`src/gui/icon_shifts.inc`, generated by `tools/icon_center/gen_icon_shifts.py` (the body is the drawing with its
shadows stripped structurally; the nested slot is walked like any group). The files stay as the tool writes them; the
shift is the product's, not the file's.

## The icons

| enumerator | 3.22 name | source sheet (src/fullcolor) | slot (x, y, w, h) | sheet sha256 | note |
|---|---|---|---|---|---|
| AccessoriesDictionary | `accessories-dictionary` | `accessories-dictionary.svg` | 296, 50, 48, 48 | `86f5650828d5d4a3975dcbb834c1333304d5e4beeb7b6197c908764374f0ba6c` | NEW: Source (architect 2026-10-14) |
| AccessoriesTextEditor | `accessories-text-editor` | `accessories-text-editor.svg` | 296.062, 39.9963, 48, 48 | `f4ada0e92d0d721c4cdf5fabb99c6e5e2cdcbae3ace01ef716b3c0b8fc79524f` |  |
| AppIcon | `audio-x-generic` | `audio-x-generic.svg` | 296.062, 49.9963, 48, 48 | `c2d7906e7d19b021cabff60aea188b709c2eaa28c69eb7176bca365e9d45ac5d` |  |
| AudioXWav | `audio-x-generic` | `audio-x-generic.svg` | 296.062, 49.9963, 48, 48 | `c2d7906e7d19b021cabff60aea188b709c2eaa28c69eb7176bca365e9d45ac5d` |  |
| BboxNext | `go-last` | `navigation-icons.svg` | 856, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| BboxPrev | `go-first` | `navigation-icons.svg` | 456, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| BlackSum | `accessories-calculator` | `accessories-calculator.svg` | 296, 50, 48, 48 | `aeafa0e25eb404c73da589337ddb6dfb1aeec8ab2663a7038702c9379de912fb` |  |
| WeatherFog | `weather-fog` | `weather-icons.svg` | 296, 350, 48, 48 | `aa8311297490b3270f4fbdbe12b05dd5c005349be2109413611e2db97d9919fa` | NEW: Phase reset (architect 2026-10-14: the waves in phase) |
| ChangesAllow | `changes-allow` | `changes.svg` | 696, 50, 48, 48 | `f33d7f5e7945f2c772cca61ca2177ecb28080926a1f53a8b2cbbb63581b8d217` | NEW: Read-Only while unlocked (architect 2026-10-14) |
| ChangesPrevent | `changes-prevent` | `changes.svg` | 296, 50, 48, 48 | `f33d7f5e7945f2c772cca61ca2177ecb28080926a1f53a8b2cbbb63581b8d217` | NEW: Read-Only while locked (architect 2026-10-14) |
| DialogCancel | `process-stop` | `navigation-icons.svg` | 696, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| DialogError | `process-stop` | `navigation-icons.svg` | 696, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| DialogInformation | `dialog-information` | `dialog-information.svg` | 296, 50, 48, 48 | `43752d57c0e5a94957279071458888afbefb187bc97e0184f39757d97cffb7c0` |  |
| DialogOkApply | `edit-paste` | `copy-paste-tasks.svg` | 296, 50, 48, 48 | `858cc2773cbd07e48065bf5416d869f0a49d9eabd772556da7099e3e51524f92` | Load in Place (architect 2026-10-14) |
| DocumentOpen | `document-open` | `folders.svg` | 1496, 350, 48, 48 | `410c37478763ca92ce06ed3fb49cf998f905aedab1d978b69ff579763ae4b2da` |  |
| DocumentRevert | `document-revert` | `paper-sheets.svg` | 1900, 654, 48, 48 | `1bf2aad02a3644760903ccc36c3a942b6b2a4f408752e7fd7c0dff981e1f1a8b` |  |
| DocumentSave | `document-save` | `cabinets.svg` | 696, 50, 48, 48 | `abefcd60460b10ac8be532acb78197132f8e90b3db97e6d3812c555914f0517f` | the floppy retired (architect 2026-10-14) |
| EditDelete | `user-trash` | `trash.svg` | 696, 50, 48, 48 | `940cae7ced59edf15322f1e92cfa1636dade79dff9ecdea5f3dff6b9bfcd72d1` |  |
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
| MailMarkJunk | `mail-mark-junk` | `mail-mark-junk.svg` | 296, 50, 48, 48 | `31a04d08638ca657945e2d1f956ce7b4280bb3628e73d8342a8eff4aec8e91b4` | NEW: Warp (architect 2026-10-14: the warped sheet) |
| MusicNote16th | `audio-x-generic` | `audio-x-generic.svg` | 296.062, 49.9963, 48, 48 | `c2d7906e7d19b021cabff60aea188b709c2eaa28c69eb7176bca365e9d45ac5d` |  |
| PreviewRenderOn | `applications-multimedia` | `applications-multimedia.svg` | 296, 50, 48, 48 | `b4551addc300f3ca7d6c5c9f22044c7db2dc227e3987877cb0b99de634508ed2` |  |
| SettingsConfigure | `preferences-system` | `preferences-system.svg` | 296.062, 49.9963, 48, 48 | `66eab3c0045c5ed6f35bb9512e19d876b6012d585cd157d0de93a0b049f7bce9` |  |
| ShallowHistory | `x-office-calendar` | `x-office-calendar.svg` | 296, 50, 48, 48 | `9559baa48d2eae10a3307d477c5d7436d4a911ed28cf815344ac85808c165bc8` |  |
| TabDetach | `preferences-system-windows` | `preferences-system-windows.svg` | 296, 50, 48, 48 | `72075ba85164fefc23d4296771d6c5db502fafb107a7cf327db049bfb33968ff` |  |
| TabDetachB | `preferences-system-windows` | `preferences-system-windows.svg` | 296, 50, 48, 48 | `72075ba85164fefc23d4296771d6c5db502fafb107a7cf327db049bfb33968ff` | DERIVED, below |
| TimelineLift | `view-fullscreen` | `view-fullscreen-restore.svg` | 296, 50, 48, 48 | `dd9a137b1779bd454c300d351e50ba2f1458c729cb2fc49e890173989409a688` |  |
| ValueDrag | `object-flip-vertical` | `object-flip-rotate.svg` | 76.0001, 300, 48, 48 | `cd9be5ee2271c9a797d49aa9749f70c4ad48f53768f651c166560baba865afa1` |  |
| VcsCommit | `go-top` | `navigation-icons.svg` | 276, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| VcsDiff | `document-open-recent` | `clocks.svg` | 696.062, 355.996, 48, 48 | `0411e9fe1933a5e6d6c5d6b577256542fdca27da0655d0db77eaffdcb1b7f7f2` | TEXT TO OUTLINES: 48 text elements (Liberation Sans Bold) replaced by glyph outlines from fonts/LiberationSans-Bold.ttf |
| VcsPull | `go-bottom` | `navigation-icons.svg` | 106, 50, 48, 48 | `c4750deb87d0b477ccb60aeda9276cae6fdec2fa467c995d3ec6e4def7c127dd` |  |
| ViewHidden | `audio-volume-muted` | `audio-volume.svg` | 421, 335, 256, 256 | `0a1b5e6cc52d57eeb8e864b5d16e88bdb523da79b87e6bdaeff1599d83acbd9b` |  |
| WindowClose | `window-close` | `window-close.svg` | 296, 50, 48, 48 | `3145ee5b93477957dca1f8235c612e8eff2148227c95ffc53186c78b891a73b2` |  |
| ZoomFitBest | `zoom-fit-best` | `zoom.svg` | 696, 350, 48, 48 | `10dc824bc7ed335b0ee21be0670857b7b559f2adedd9ffe9f7cf0a1805c254a5` |  |
| ZoomInY | `zoom-in` | `zoom.svg` | 296, 50, 48, 48 | `10dc824bc7ed335b0ee21be0670857b7b559f2adedd9ffe9f7cf0a1805c254a5` |  |
| ZoomOriginal | `zoom-original` | `zoom.svg` | 296, 350, 48, 48 | `10dc824bc7ed335b0ee21be0670857b7b559f2adedd9ffe9f7cf0a1805c254a5` |  |
