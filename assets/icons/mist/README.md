# The Mist icons (the Clearlooks vocabulary's icon set)

The Clearlooks vocabulary's icons, one SVG per `icons::Icon` enumerator (`src/gui/icons.h`), each file named by its
enumerator: WHAT DEBIAN 6.0 SQUEEZE SHOWS WITH THE "MIST" ICON THEME SELECTED (architect 2026-10-06 and 2026-10-07) —
Mist's own files (gnome-themes 2.30.2's `icon-themes/Mist`, whose `index.theme` says `Inherits=gnome`) over
gnome-icon-theme 2.30's drawings, the name `mist` the one squeeze gives the combination. The set is in the bundle and
nothing reads it yet: the live set is the chrome spec's (`src/gui/chrome_spec.h`'s `icon_set`, today `tango`); a set
is chosen by its folder's name, so this folder is simply there for the vocabulary that names it.

THE TWO SOURCES. 57 files are gnome-icon-theme 2.30's drawings TAKEN FROM THE 3.0.0 TARBALL (the 2.30 icons are
multi-icon Inkscape sheets, `src/*.svg`, one drawing per size slot; 3.0.0 carries 2.30's sheets, redrawn at four of
these seats, below): each file is one sheet's 48x48 slot EXTRACTED as a standalone 48-unit drawing
— the elements of every visible layer that paint inside the slot's rectangle (each probed alone through rsvg-convert),
the definitions they reference, the root `width="48" height="48" viewBox="0 0 48 48"`, the drawing translated to the
origin, written with SVG as the default namespace — and verified against a render of the slot from the sheet itself
(rsvg-convert at 72 px: maximum channel difference 0 on 52 of the 53 distinct drawings, 5 levels of 255 on EditDelete's
user-trash; EditUndo's and EditRedo's judged on mask-stripped copies, below).
Every layer, not only the icon's own: zoom.svg draws part of zoom-original's and zoom-fit-best's pictures from the
zoom-in layer, and the slot shows them. 2 files are Mist's own scalable drawings, copied from the gnome-themes tarball
(byte-identical to the copies squeeze installs under `/usr/share/icons/Mist/scalable/`): `Folder` = Mist's
`places/folder.svg` and `DocumentOpen` (Open Project, the icon row's first button since 2026-10-07) = Mist's
`actions/document-open.svg` — the only two of the 59 seats Mist changes (it carries nine names, every one a blue
folder; the wav row, Up a Folder and every other toolbar and row 8 seat inherit from gnome). Mist's drawings fill the
48 canvas edge to edge where GNOME's keep 3–4 units of margin, so its folder reads larger at the same seat.

THE EDITS, each in the table's "edited" column, nothing else changed:
- Mist's files: their `xmlns:s` namespace URI carries a stray space (`http://inkscape.sourceforge.net/DTD/s
  odipodi-0.dtd`), which librsvg refuses as an invalid URI; the space is removed (`…/DTD/sodipodi-0.dtd`), one byte.
  resvg parses the original too and draws it identically (the 72-px rasters byte-equal); the repair is for the import
  check's reference renderer. The table gives the original's sha256 beside the shipped file's.
- EditUndo and EditRedo (`edit-undo-redo.svg`): the arrow sits under a `<mask>` that both rsvg-convert 2.62 and resvg
  paint as NOTHING — the glyph vanishes, leaving only its shadow; every mask reference is stripped (`mask:url(#…)` →
  `mask:none`, `mask="url(#…)"` removed), so the arrow draws whole, without the fade GNOME's PNG shows at its tail.
  Their extraction's probe was judged on a mask-stripped copy for the same reason (otherwise edit-undo's arrow, painting
  nothing alone, is not kept).

THE TWO SEATS OF 2026-10-07 (architect, ruled for Tango — "the arrows pointing at each other become Load in Place,
and the clock becomes Revert" — and taken here by the symmetry of the sets): DialogOkApply (Load in Place) wears
gnome's `view-refresh`, the reload's arrow — the walked version loaded into the editor in place; DocumentRevert
(Revert) wears gnome's `appointment-new`, the clock with its star — back to the viewed checkpoint in time. Mist
carries neither name (its scalable folder holds folders and `document-open` only), so both are gnome's drawings by
inheritance; Load in Place wore Mist's `document-open` and Revert gnome's `document-revert` until then. Both came by
the road above (3.0.0's slot extracted, maximum difference 0 against the slot render at 72; 2.30.3's slot renders
identically to 3.0.0's for both, maximum difference 0); `appointment-new`'s extraction is byte-identical to
ShallowHistory's file.

REPEATS, known by position (the files byte-identical): gnome's `audio-x-generic` is AppIcon, AudioXWav and
MusicNote16th; gnome's `go-up` is GoUp and GoParentFolder; gnome's `appointment-new` is ShallowHistory (Toggle History
Walk) and DocumentRevert (Revert). Every other file is worn once: 59 enumerators over 55 distinct drawings.

THE KNOWN DEPARTURE FROM SQUEEZE'S BYTES (the planner's call, 2026-10-07: 3.0.0 throughout). Squeeze installs
gnome-icon-theme 2.30.3, whose sheets are not taken; at 53 of the 57 GNOME seats 3.0.0's slot renders identically to
2.30.3's (rsvg at 72, maximum difference 0), at four it was redrawn in 3.0.0 and the set wears 3.0.0's drawing (max /
mean channel difference of 3.0.0's slot from 2.30.3's at 72, levels of 255):

| enumerator | slot | max | mean | what changed |
|---|---|---|---|---|
| DocumentSave | document-save | 255 | 69.33 | the arrow: 2.30.3's red, 3.0.0's green, the drive redrawn |
| DialogCancel | process-stop | 248 | 5.71 | a light redraw |
| TabDetach | preferences-system-windows | 149 | 7.02 | a light redraw |
| DialogInformation | dialog-information | 255 | 29.77 | 2.30.3's blue bulb is a clear one |

## The import check (the resvg ruling: a set is checked once, at its import)

All 58 files of the import through the product's own road (a scratch harness over `src/gui/svg_icon.cpp` and the
build's resvg 0.48.1) and through rsvg-convert 2.62.4, composited over #D4D0C8, the mean absolute channel difference
in levels of 255: the median over the 58 is **0.52 at 72 px** (p90 1.20, worst 2.91 HelpWhatsthis) and **1.75 at 33
px** (p90 3.46, worst 4.61 EditDelete); 1.03 at 48. Against GNOME's own 48-px PNGs in the 3.0.0 tarball, the median is
1.08 for resvg and 0.62 for rsvg-convert. The two seats changed on 2026-10-07 and DocumentOpen, a byte copy of a
checked file, were re-checked on a sheet of their own the same way: DialogOkApply's `view-refresh` 0.46 at 72 px and
1.33 at 33 (1.07 from GNOME's 48-px PNG), DocumentRevert, a byte copy of ShallowHistory, 2.47 and 3.29 (its clock
numerals, below), DocumentOpen 0.35 and 2.29. Every file draws in both renderers. Where they part, each was looked at
on the sheet:
- VcsDiff (`document-open-recent`), ShallowHistory and DocumentRevert (`appointment-new`) carry `<text>` clock
  numerals, which the product's resvg (built without text) skips and rsvg-convert draws faintly (72-px max 94 / 109);
  GNOME's own PNGs show no numerals either. Kept as they are.
- TimelineLift (`system-lock-screen`) and BlackSum (`accessories-calculator`) carry `feBlend` filters reading
  `BackgroundImage`; the two renderers agree within 1.39 and 1.09 levels at 72. Kept.
- HelpWhatsthis (`help-browser`): both renderers draw the same lifebuoy (2.91 apart) and both sit about 20 levels from
  GNOME's shipped PNG, whose ring is shaded differently; no mask is involved. Kept.

## Provenance

- gnome-icon-theme 3.0.0, `gnome-icon-theme-3.0.0.tar.bz2`
  (https://download.gnome.org/sources/gnome-icon-theme/3.0/), sha256
  `42da7c4ab1521e7beb1c26f5588c35a644ef57585754d8ceec427d9d7f94bb09` (GNOME's own `.sha256sum`).
- gnome-themes 2.30.2, `gnome-themes-2.30.2.tar.bz2` (https://download.gnome.org/sources/gnome-themes/2.30/), sha256
  `b7efbeb429ad19003cc26c468597cef42f5b5570d819af51828efbaa441682d5` (GNOME's own `.sha256sum`); squeeze's package
  gnome-themes 2.30.2-1.

The table's source column names the tarball file and, for a GNOME seat, the slot (its name and its rectangle's
origin in the sheet's root coordinates); its last column is the shipped file's sha256.

## Licence

gnome-icon-theme 3.0.0's `COPYING`, copied here verbatim as `COPYING-gnome-icon-theme-3.0.0`, reads: "GNOME icon theme
is distributed under the terms of either GNU LGPL v.3 or Creative Commons BY-SA 3.0 license." The set takes the 57
under the LGPL v3, whose text the tarball ships as `COPYING_LGPL` (here `COPYING_LGPL-gnome-icon-theme-3.0.0`), with
the GNU GPL v3 beside it as `COPYING_GPL3-gnome-icon-theme-3.0.0` (the LGPL v3's base text, carried for completeness,
copied from the GNU text rather than the tarball, which does not ship it); the
alternative's notice is `COPYING_CCBYSA3` (here `COPYING_CCBYSA3-gnome-icon-theme-3.0.0`), which asks for the
attribution "GNOME Project". Credit: the tarball's `AUTHORS`, here `AUTHORS-gnome-icon-theme-3.0.0`.

gnome-themes 2.30.2 is under the GNU LGPL 2.1, its `COPYING` copied here verbatim as `COPYING-gnome-themes-2.30.2`;
its `AUTHORS` (here `AUTHORS-gnome-themes-2.30.2`) credits "Susan Kare — Mist (aka 'Flat-Blue') icons". The APK
carries the SVG files only; this repository carries the texts. Files of other names in this folder (this README, the
licence texts) are not read.

## The icons

| enumerator | act | source | repeats with | edited at import | sha256 |
|---|---|---|---|---|---|
| DocumentOpen | Open Project (Revert its shift twin) | Mist `icon-themes/Mist/scalable/actions/document-open.svg` | — | namespace repair (original `d38c3d84a0af9e73eaa1422e6caa891dc007291996e8fbd421829bd366a811f5`) | `c64e047996ec536473723c38049d7a098c9350be449ccd2a8bbe6de98e91d3a9` |
| DocumentSave | Save | 3.0.0 `src/cabinets.svg`, slot `document-save` 48x48 at (696, 50) | — | 3.0.0 ≠ 2.30.3 | `f676db4d816fdc09804ca0e9c2c231133f03a470ee154b446cba7990a831e127` |
| EditUndo | Undo | 3.0.0 `src/edit-undo-redo.svg`, slot `edit-undo` 48x48 at (16, 300) | — | masks stripped | `09403be09074eb05905bc06835aadb46aba648a9a46c1dd64ad0edf0a5e1953b` |
| EditRedo | Redo | 3.0.0 `src/edit-undo-redo.svg`, slot `edit-redo` 48x48 at (76, 300) | — | masks stripped | `508b572c05cf1a1784cff2c4af843848ecc0129b7dad19f6dd81de3481402de9` |
| MediaRecord | Render (and the sweep) | 3.0.0 `src/media-control-icons.svg`, slot `media-record` 48x48 at (506, 150) | — | — | `74c5b53227cdaeafc0146b0fee64a30e637ddc41c3bf5c54bb0c6dd716042690` |
| VcsCommit | Save and Commit (Save's face in `h`) | 3.0.0 `src/navigation-icons.svg`, slot `go-top` 48x48 at (276, 50) | — | — | `688ae6ef13012732275787fbe349939db74a16d9d28336013f126097c9936d61` |
| VcsPull | Pull (Save's face, GitHub ahead) | 3.0.0 `src/navigation-icons.svg`, slot `go-bottom` 48x48 at (106, 50) | — | — | `97e3b2f54a0bf9b2929db9b68e2ec1f3b5e00273afbd7071ee5f0d34d8675a7c` |
| DocumentExport | Source+Warp (view 1) | 3.0.0 `src/format-indent-justify-text.svg`, slot `format-indent-less` 48x48 at (6, 110) | — | — | `c87164554c80e2e5b80ea66266d0c3d6b395093381ab64482229523969243bcf` |
| DocumentImport | Target+Warp (view 2) | 3.0.0 `src/format-indent-justify-text.svg`, slot `format-indent-more` 48x48 at (66, 110) | — | — | `b313756860f6776958dbc5d8e2d27d125e6aa56819f832a03ebc029649bdd595` |
| ChronometerStart | Target+Phase (view 3) | 3.0.0 `src/format-indent-justify-text.svg`, slot `format-justify-fill` 48x48 at (306, 110) | — | — | `1900b8a4f3e358e99c662bda9c2ea391d82c4616b4972ae604bbd62e080603d4` |
| ZoomFitBest | Full Zoom Out | 3.0.0 `src/zoom.svg`, slot `zoom-fit-best` 48x48 at (696, 350) | — | — | `13f120abed1142e8eb130906b885cb536a39ba1861bbcb707469b82fbae9fb59` |
| ZoomOriginal | Center on Focus | 3.0.0 `src/zoom.svg`, slot `zoom-original` 48x48 at (296, 350) | — | — | `947b48966887a06270e9fabd8d3b2dc5412b253b45bc9b999c61687fcf3bf373` |
| ZoomInY | Toggle Waveform Magnification | 3.0.0 `src/zoom.svg`, slot `zoom-in` 48x48 at (296, 50) | — | — | `bdb0f701b7515292fe85d79898bab3365a6b7b51a9b7050c0c30288bd82f46fc` |
| ListAdd | Drop Marker | 3.0.0 `src/list-add-remove.svg`, slot `list-add` 48x48 at (296, 50) | — | — | `8b85ba35c298e67653a53cfc7a23b4220e47103d1c351da06cbc2b78b14120b4` |
| ListRemove | Delete Markers | 3.0.0 `src/list-add-remove.svg`, slot `list-remove` 48x48 at (696, 50) | — | — | `52ee707fe8abfed23255f48737ac90d2ee2d9a106c99b18c19d28b940b39d524` |
| ViewHidden | Toggle Disabled | 3.0.0 `src/dialog-error.svg`, slot `dialog-error` 48x48 at (296, 50) | — | — | `3a325ee86732df0305e2da55932af791c36a28ca14f27d0e10cee721dffbc697` |
| InsertLink | Toggle Inherit | 3.0.0 `src/insert-link.svg`, slot `insert-link` 48x48 at (296, 50) | — | — | `e1a734fdd0ec395e9e926b3a0147580eec1109089c264da8cf29a968bba8f4ed` |
| Merge | Flatten (Ctrl+F: "the terms go") | 3.0.0 `src/edit-clear.svg`, slot `edit-clear` 48x48 at (296, 50) | — | — | `4d493da64903f1f0e6bdc1fb7b688007f05e5d82b3c6684d68f9e61feb9a3d1b` |
| BlackSum | Toggle Cumulative | 3.0.0 `src/accessories-calculator.svg`, slot `accessories-calculator` 48x48 at (296, 50) | — | — | `a4e2d1df853ecd36b30a2b2345d0d1d7104255b72b121dfa9bc50dba409cd38b` |
| GoJump | Toggle Follow | 3.0.0 `src/start-here.svg`, slot `start-here` 48x48 at (296, 50) | — | — | `210099402c0a9d5992227779bc6729133484de6030bd57812d564426db503bfa` |
| TimelineLift | Toggle Restrict Undo to Current View | 3.0.0 `src/displays.svg`, slot `system-lock-screen` 48x48 at (696.062, 349.996) | — | — | `0f416740e051c586db79849d0b138959a95a131a3cc781fe9aef79a815e45c1f` |
| MusicNote16th | BPM Iterations | 3.0.0 `src/audio-x-generic.svg`, slot `audio-x-generic` 48x48 at (296.062, 49.9963) | AudioXWav, AppIcon | — | `af9057ecdb54541c0ce57d1b9e453dc825f0cfd3bfb554fde2afc3e83b234ad0` |
| Mathmode | Toggle Grid Iterations | 3.0.0 `src/paper-sheets.svg`, slot `x-office-spreadsheet` 48x48 at (1500, 354) | — | — | `b4be444999e33cba0652aae7f060f1def713223d158555ca9ceac2d95d8db12d` |
| PreviewRenderOn | Play Renders | 3.0.0 `src/applications-multimedia.svg`, slot `applications-multimedia` 48x48 at (296, 50) | — | — | `f7b387398baed6508ae4bfef7ec60ce354207bdcbf355a9e57a783a98f33ef80` |
| DialogOkApply | Load in Place | 3.0.0 `src/navigation-icons.svg`, slot `view-refresh` 48x48 at (616, 50) (Mist carries no `view-refresh`) | — | — | `2371a1d6832ffc8180fc2df0f243455b3722dc1e253a62fc8111a22f98e507d6` |
| VcsDiff | Toggle History View | 3.0.0 `src/clocks.svg`, slot `document-open-recent` 48x48 at (696.062, 355.996) | — | — | `129566f29230db701d06c68dfd99940c88a667fe42f0e6b9b9921f7c8b9ea21d` |
| ShallowHistory | Toggle History Walk | 3.0.0 `src/clocks.svg`, slot `appointment-new` 48x48 at (296.062, 55.9963) | DocumentRevert | — | `806c7ed7e292db3edf5f1a3caec8397706b41e4640db09d6ab92c4543fae395d` |
| EditSelect | Toggle Add to Selection | 3.0.0 `src/paper-sheets.svg`, slot `edit-select-all` 48x48 at (1500, 654) | — | — | `245ce65757ef40dce015a3fc99604e0a604678fae2032f74f959a5af06aeaf8d` |
| KeyframePrevious | Older (`,`) | 3.0.0 `src/media-control-icons.svg`, slot `media-seek-backward` 48x48 at (206, 150) | — | — | `2ca7796acc08333c3995f99fa6bfdb55198f2b11a8360d6ce6afa8e8ddc1e278` |
| KeyframeNext | Newer (`.`) | 3.0.0 `src/media-control-icons.svg`, slot `media-seek-forward` 48x48 at (406, 150) | — | — | `f34d18b3587fc220d4e832cf101c98ebad39dcddc14f0ec17610446fc9c942b6` |
| GoPrevious | Left (the playhead step) | 3.0.0 `src/navigation-icons.svg`, slot `go-previous` 48x48 at (536, 50) | — | — | `3155504fad949f054c6d88a4a550eae714b05763d1d9266188e9ca2ad289f336` |
| GoNext | Right (the playhead step) | 3.0.0 `src/navigation-icons.svg`, slot `go-next` 48x48 at (776, 50) | — | — | `d047a56474e708c114610291a130460e791531e46f1d0d5c9443a32b2112291c` |
| DocumentRevert | Revert (`v`, to the viewed checkpoint) | 3.0.0 `src/clocks.svg`, slot `appointment-new` 48x48 at (296.062, 55.9963) (Mist carries no `appointment-new`) | ShallowHistory | — | `806c7ed7e292db3edf5f1a3caec8397706b41e4640db09d6ab92c4543fae395d` |
| MediaSkipBackward | Go to Start | 3.0.0 `src/media-control-icons.svg`, slot `media-skip-backward` 48x48 at (156, 150) | — | — | `36dd7c445acd6380eeed3f1b6b75d620d3e990ba3fe7fe5f2efbdf084a8942c5` |
| MediaPlaybackStart | Play | 3.0.0 `src/media-control-icons.svg`, slot `media-playback-start` 48x48 at (306, 150) | — | — | `b622e494ee220ca90f417b4332b36121672dade8cc668986fd12b777ff018099` |
| MediaPlaybackStop | Stop | 3.0.0 `src/media-control-icons.svg`, slot `media-playback-stop` 48x48 at (356, 150) | — | — | `43f78b6cfeb4e7f1ccc9a31e286c5e08fcad6c3b3d0efedfa54283ffc0763765` |
| MediaPlaybackPause | Pause (the render player) | 3.0.0 `src/media-control-icons.svg`, slot `media-playback-pause` 48x48 at (256, 150) | — | — | `5d590581153e47cafee13e2e3ed156af3fcc8923a47c5fbd75cf21a17dc4d0da` |
| MediaSkipForward | Go to End | 3.0.0 `src/media-control-icons.svg`, slot `media-skip-forward` 48x48 at (456, 150) | — | — | `59255549061dab46601d8e9ae678df87175d09d3cd63047680735d7a8a9225dc` |
| DialogCancel | Cancel (Render's mid-render face) | 3.0.0 `src/navigation-icons.svg`, slot `process-stop` 48x48 at (696, 50) | — | 3.0.0 ≠ 2.30.3 | `9a8383ed16d05c5091c41caa64ad5d28106e85a7ef6a5d94c5ba15d51803bba0` |
| GoDown | Down (the value ladder) | 3.0.0 `src/navigation-icons.svg`, slot `go-down` 48x48 at (186, 50) | — | — | `9740ecff336ea73d1fc3651c1f2dc7992a5179c75fcd038d5d40b58f28a8f821` |
| GoUp | Up (the value ladder) | 3.0.0 `src/navigation-icons.svg`, slot `go-up` 48x48 at (366, 50) | GoParentFolder | — | `398b36552f6037180f7be36160d22a4180db0c5d4c5ce6593c65c5fd1e7e396f` |
| Lock | Toggle Read-Only, locked | 3.0.0 `src/changes.svg`, slot `changes-prevent` 48x48 at (296, 50) | — | — | `ae4d15d03f13b3351bac46d00ae0c1a0cfc4b1db8b29379807d4cb0800019ec2` |
| Unlock | Toggle Read-Only, unlocked | 3.0.0 `src/changes.svg`, slot `changes-allow` 48x48 at (696, 50) | — | — | `138f451327658b2c600c5a0336aa6a24b90f12b652959c3c96aa5606a8d06e7e` |
| BboxPrev | Previous Marker (Shift+Tab) | 3.0.0 `src/navigation-icons.svg`, slot `go-first` 48x48 at (456, 50) | — | — | `01e0a2c310027a4c8fce2ade0e9971fd3c55d891df64210e498ee6ec9b3b8ff3` |
| BboxNext | Next Marker (Tab) | 3.0.0 `src/navigation-icons.svg`, slot `go-last` 48x48 at (856, 50) | — | — | `01007440faf4da38305b5f25d8ea98dec712a9442aea40f91a25786c80889628` |
| TabDetach | Switch Tab | 3.0.0 `src/preferences-system-windows.svg`, slot `preferences-system-windows` 48x48 at (296, 50) | — | 3.0.0 ≠ 2.30.3 | `206cfdfd36c084b4d586a85ec036fd4d5a30b549ea6d9cb0c894efbcbdfc0fe0` |
| SettingsConfigure | Settings | 3.0.0 `src/preferences-system.svg`, slot `preferences-system` 48x48 at (296.062, 49.9963) | — | — | `f39fe9462f889924bc4564c5233b56141f32d56737470c2cb12f53c0fd567fbc` |
| Folder | a folder row | Mist `icon-themes/Mist/scalable/places/folder.svg` | — | namespace repair (original `4c7a2378b702b4a4567525236905b35f456ce4a13cafa973271947e060e01467`) | `5e15025b6bd9f08a17f18665371b6927bf4dab0b4a5575d3673cf7dbc4d10e52` |
| AudioXWav | a wav row | 3.0.0 `src/audio-x-generic.svg`, slot `audio-x-generic` 48x48 at (296.062, 49.9963) | MusicNote16th, AppIcon | — | `af9057ecdb54541c0ce57d1b9e453dc825f0cfd3bfb554fde2afc3e83b234ad0` |
| MediaRepeatSingle | Toggle Repeat One | 3.0.0 `src/media-control-icons.svg`, slot `media-playlist-repeat` 48x48 at (56, 150) | — | — | `7bbc59c69d5f443288e59a23400ee6042727e5a9839e5c4f7be4855d9ead2c9e` |
| GoParentFolder | Up a Folder | 3.0.0 `src/navigation-icons.svg`, slot `go-up` 48x48 at (366, 50) | GoUp | — | `398b36552f6037180f7be36160d22a4180db0c5d4c5ce6593c65c5fd1e7e396f` |
| DialogInformation | a NORMAL card | 3.0.0 `src/dialog-information.svg`, slot `dialog-information` 48x48 at (296, 50) | — | 3.0.0 ≠ 2.30.3 | `45c36a6cd363964821c59b0efa5c48de243a0d6142338f53a4590a08890c3650` |
| DialogError | a CRITICAL card | 3.0.0 `src/edit-delete.svg`, slot `edit-delete` 48x48 at (296, 49) | — | — | `796aaa833db70a55f292380e601890402871575a87fe501d7038dc57c88b246b` |
| WindowClose | Close (the render player) | 3.0.0 `src/window-close.svg`, slot `window-close` 48x48 at (296, 50) | — | — | `5ea22cd3007a61f65ceb55dee86a3286579bde37b0f29b22a6b3aa2399060dc1` |
| EditCopy | Copy Resolved Value | 3.0.0 `src/copy-paste-tasks.svg`, slot `edit-copy` 48x48 at (696, 50) | — | — | `7e7b8a4a65137cd6726f2fe6b3756c007586b23637f512451199092c9de16e97` |
| HelpWhatsthis | Toggle Tooltips | 3.0.0 `src/help-browser.svg`, slot `help-browser` 48x48 at (13, 37) | — | — | `73dab752241b092c65e22f7345ae6d29354e31cb86530e29d2421fe13b489dfc` |
| GoJumpDeclaration | Jump to Defining Marker | 3.0.0 `src/navigation-icons.svg`, slot `go-jump` 48x48 at (26, 50) | — | — | `1624ee1168fbc6ddb1dbcaa45c90e7ca2c817f12408b3ed737b67d369e4afbd1` |
| EditDelete | Delete Folder | 3.0.0 `src/trash.svg`, slot `user-trash` 48x48 at (696, 50) | — | — | `52a0f172d734ee00d7b819644cbe9c878794e8628e462b81bbcd24e69f4f886a` |
| AppIcon | the caption's icon and the program icon (the chrome grey ground, #D4D0C8) | 3.0.0 `src/audio-x-generic.svg`, slot `audio-x-generic` 48x48 at (296.062, 49.9963) | MusicNote16th, AudioXWav | — | `af9057ecdb54541c0ce57d1b9e453dc825f0cfd3bfb554fde2afc3e83b234ad0` |
