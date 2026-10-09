# The Mist icons (the GNOME 2.30 set)

The GNOME 2.30 icon set he keeps as a choice under the `icons` device key (architect 2026-10-09: "Mist and Tango
both work"), one SVG per `icons::Icon` enumerator
(`src/gui/icons.h`), each file named by its enumerator: WHAT DEBIAN 6.0 SQUEEZE SHOWS WITH THE "MIST" ICON THEME
SELECTED (architect 2026-10-06 and 2026-10-07) — Mist's own files (gnome-themes 2.30.2's `icon-themes/Mist`, whose
`index.theme` says `Inherits=gnome`) over gnome-icon-theme 2.30's drawings, the name `mist` the one squeeze gives the
combination. The live set is the device config's `icons` key's, else the chrome spec's (`src/gui/chrome_spec.h`'s
`icon_set`, Tango under windows-2000): `icons=mist` chooses this folder; a set is chosen by its folder's name.

THE TWO SOURCES. 58 files are gnome-icon-theme 2.30's drawings, 56 TAKEN FROM THE 3.0.0 TARBALL and 2 FROM 2.30.3'S,
the release squeeze installs: 3.0.0 carries 2.30's sheets, redrawn at five of these seats (below), and at two of the
five, DocumentRevert and DialogInformation, the architect ruled squeeze's own drawing back. The 2.30 icons are
multi-icon Inkscape sheets, `src/*.svg`, one drawing per size slot: each file is one sheet's 48x48 slot EXTRACTED as
a standalone 48-unit drawing
— the elements of every visible layer that paint inside the slot's rectangle (each probed alone through rsvg-convert),
the definitions they reference, the root `width="48" height="48" viewBox="0 0 48 48"`, the drawing translated to the
origin, written with SVG as the default namespace — and verified against a render of the slot from the sheet itself
(rsvg-convert at 72 px: maximum channel difference 0 on 54 of the 55 distinct drawings, 5 levels of 255 on EditDelete's
user-trash; EditUndo's and EditRedo's judged on mask-stripped copies, below).
Every layer, not only the icon's own: zoom.svg draws part of zoom-original's and zoom-fit-best's pictures from the
zoom-in layer, and the slot shows them. 2 files are Mist's own scalable drawings, copied from the gnome-themes tarball
(byte-identical to the copies squeeze installs under `/usr/share/icons/Mist/scalable/`): `Folder` = Mist's
`places/folder.svg` and `DocumentOpen` (Open Project, the icon row's first button since 2026-10-07) = Mist's
`actions/document-open.svg` — the only two of the 60 seats Mist changes (it carries nine names, every one a blue
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

THE ACCEPT AND THE REVERT (architect 2026-10-07 ~17:00, on the GNOME families survey; Tango keeps its reload arrows
and its clock, a deliberate asymmetry: the Tango set may lag the GNOME set): the pair
rhymes as GNOME's own accept and revert, and the toolbar's second clock is gone, the clock left to History View's
`document-open-recent`. With them, the NORMAL card's bulb goes back to squeeze's. Mist carries none of the three
names (its scalable folder holds folders and `document-open` only), so all three are gnome's drawings by inheritance,
by the road above.
- DialogOkApply (Load in Place) wears gnome's `emblem-default`, the green orb with the white check — the walked version
  taken into the editor in place; the one scalable check mark the family draws. 3.0.0's `src/emblem-default.svg` slot,
  one of the five candidates kept, maximum difference 0 against the slot render at 72; 2.30.3's sheet gives a
  byte-identical extraction, and 3.12.0's slot renders identically to 3.0.0's (maximum difference 0). No mask; one
  `<filter>`, the orb's blur, which both renderers draw.
- DocumentRevert (Revert) wears gnome's `document-revert`, the page with the YELLOW return arrow — back to the viewed
  checkpoint. TAKEN FROM 2.30.3: 3.0.0's sheet redraws the arrow gray (its slot from 2.30.3's at 72: max 255, mean
  22.59), and 2.30.3's is the drawing squeeze shows. 2.30.3's `src/paper-sheets.svg` slot, three of the 304
  candidates kept, maximum difference 0 against the slot render at 72. The ruling asked for EditUndo's mask strip, but
  nothing the slot keeps references a mask — the survey listed `document-revert` beside the masked drawings because
  3.0.0's slot renders gray against the yellow shipped PNG, a redraw and not a mask (3.0.0's slot keeps none either):
  run with the strip, the extraction is byte-identical to the plain one, so the file is the plain extraction,
  unedited. Two `<filter>`s, two `<use>`s and a clip path, which both renderers draw.
- DialogInformation (a NORMAL card) wears 2.30.3's BLUE bulb, squeeze's own, in place of 3.0.0's clear one, which
  nearly vanishes on the ground: 2.30.3's `src/dialog-information.svg` slot, one of the five candidates kept, maximum
  difference 0 against the slot render at 72.

THE CALENDAR OF 2026-10-07 (architect ~04:45, ruled for both sets, resolving the clock the two history seats then
shared): ShallowHistory (Toggle History Walk) wears gnome's `x-office-calendar`, the
dated page — the walk through dated commits. Mist carries no such name, so it is gnome's drawing by inheritance, by the
same road (3.0.0's `src/x-office-calendar.svg` slot extracted, one of the five candidates kept, maximum difference 0
against the slot render at 72; 2.30.3's slot renders identically to 3.0.0's, maximum difference 0). Its page's "3" is
a path, with no `<text>` for resvg to skip.

THE MUTED SPEAKER OF 2026-10-07 (architect ~15:45, ruled for both sets: "Agree. Muted" — a disabled marker is a
marker the render does not hear; `dialog-error`'s red sign, worn until then, read as delete and piled color on the
row's left): ViewHidden (Toggle Disabled) wears gnome's `audio-volume-muted`, the speaker with its gray cross. Mist
carries no such name, so it is gnome's drawing by inheritance, by the same road (3.0.0's `src/audio-volume.svg`, the
muted icon's 48 slot — the sheet's fourth speaker, its small column's first band — two of the 59 candidates kept,
maximum difference 0 against the slot render at 72; 2.30.3's sheet gives a byte-identical extraction and its slot
renders identically to 3.0.0's, maximum difference 0). Import check on a sheet of its own: 0.50 at 72 px and 1.58 at
33 from rsvg-convert (from GNOME's 48-px PNG 1.02 for resvg, 0.83 for rsvg-convert); one `<filter>`, the speaker's
shadow's blur, which both renderers draw. The enumerator keeps its name, as Tango's README says.

REPEATS, known by position (the files byte-identical): gnome's `audio-x-generic` is AppIcon, AudioXWav and
MusicNote16th; gnome's `go-up` is GoUp and GoParentFolder. Every other file is worn once: 60 enumerators over 57
distinct drawings (AccessoriesTextEditor, the Open Text Editor button of 2026-10-09, the sixtieth: gedit's notepad
from 3.0.0's `src/accessories-text-editor.svg`, extracted by the same rule, 46 candidates, 2 kept, maximum difference 0
against the slot's render at 72 px).

THE KNOWN DEPARTURE FROM SQUEEZE'S BYTES (the planner's call, 2026-10-07: 3.0.0, except where the architect ruled
squeeze's own). Squeeze installs gnome-icon-theme 2.30.3; at 53 of the 58 GNOME seats 3.0.0's slot renders
identically to 2.30.3's (rsvg at 72, maximum difference 0), at five it was redrawn in 3.0.0: three wear 3.0.0's
drawing, the departure, and two wear 2.30.3's, taken from its sheets (max / mean channel difference of 3.0.0's slot
from 2.30.3's at 72, levels of 255):

| enumerator | slot | max | mean | what changed | worn |
|---|---|---|---|---|---|
| DocumentSave | document-save | 255 | 69.33 | the arrow: 2.30.3's red, 3.0.0's green, the drive redrawn | 3.0.0 |
| DialogCancel | process-stop | 248 | 5.71 | a light redraw | 3.0.0 |
| TabDetach | preferences-system-windows | 149 | 7.02 | a light redraw | 3.0.0 |
| DocumentRevert | document-revert | 255 | 22.59 | 2.30.3's yellow return arrow is a gray one | 2.30.3 |
| DialogInformation | dialog-information | 255 | 29.77 | 2.30.3's blue bulb is a clear one | 2.30.3 |

## The import check (the resvg ruling: a set is checked once, at its import)

All 58 files of the import through the product's own road (a scratch harness over `src/gui/svg_icon.cpp` and the
build's resvg 0.48.1) and through rsvg-convert 2.62.4, composited over #D4D0C8, the mean absolute channel difference
in levels of 255: the median over the 58 is **0.52 at 72 px** (p90 1.20, worst 2.91 HelpWhatsthis) and **1.75 at 33
px** (p90 3.46, worst 4.61 EditDelete); 1.03 at 48. Against GNOME's own 48-px PNGs in the 3.0.0 tarball, the median is
1.08 for resvg and 0.62 for rsvg-convert. DocumentOpen, a byte copy of a checked file, was re-checked on a sheet of
its own the same way, 0.35 at 72 px and 2.29 at 33; the three seats of 2026-10-07 ~17:00 on a sheet of their own:
DialogOkApply's `emblem-default` 0.89 at 72 px and 2.19 at 33 (from GNOME's 48-px PNG, pixel-identical in 2.30.3
and 3.0.0, 2.61 for resvg, 2.37 for rsvg-convert), DocumentRevert's `document-revert` 0.82 and 1.85 (from 2.30.3's 48-px
PNG, squeeze's own, 1.18 for resvg, 1.08 for rsvg-convert), DialogInformation's blue bulb 0.50 and 1.09 (from 2.30.3's
PNG 1.53 for resvg, 1.31 for rsvg-convert); ShallowHistory's `x-office-calendar`, the same day's calendar,
0.77 at 72 px and 2.38 at 33 (1.73 from GNOME's 48-px PNG, rsvg-convert 1.26). Every file draws in both renderers. Where they part, each was looked at
on the sheet:
- VcsDiff (`document-open-recent`) carries `<text>` clock numerals, which the product's resvg (built without text)
  skips and rsvg-convert draws faintly (72-px max 94); GNOME's own PNG shows no numerals either. Kept as it is.
- TimelineLift (`system-lock-screen`) and BlackSum (`accessories-calculator`) carry `feBlend` filters reading
  `BackgroundImage`; the two renderers agree within 1.39 and 1.09 levels at 72. Kept.
- HelpWhatsthis (`help-browser`): both renderers draw the same lifebuoy (2.91 apart) and both sit about 20 levels from
  GNOME's shipped PNG, whose ring is shaded differently; no mask is involved. Kept.

## Provenance

- gnome-icon-theme 3.0.0, `gnome-icon-theme-3.0.0.tar.bz2`
  (https://download.gnome.org/sources/gnome-icon-theme/3.0/), sha256
  `42da7c4ab1521e7beb1c26f5588c35a644ef57585754d8ceec427d9d7f94bb09` (GNOME's own `.sha256sum`).
- gnome-icon-theme 2.30.3, `gnome-icon-theme-2.30.3.tar.bz2`
  (https://download.gnome.org/sources/gnome-icon-theme/2.30/), sha256
  `417e2f9d77843a6dbdcb1f6fa70dd55992aae5621c6ad8a64e5baa3cb893dac7` (GNOME's own `.sha256sum`).
- gnome-themes 2.30.2, `gnome-themes-2.30.2.tar.bz2` (https://download.gnome.org/sources/gnome-themes/2.30/), sha256
  `b7efbeb429ad19003cc26c468597cef42f5b5570d819af51828efbaa441682d5` (GNOME's own `.sha256sum`); squeeze's package
  gnome-themes 2.30.2-1.

The table's source column names the tarball file and, for a GNOME seat, the slot (its name and its rectangle's
origin in the sheet's root coordinates; for the three seats of 2026-10-07 ~17:00 the sheet's own sha256 too); its last
column is the shipped file's sha256.

## Licence

gnome-icon-theme 3.0.0's `COPYING`, copied here verbatim as `COPYING-gnome-icon-theme-3.0.0`, reads: "GNOME icon theme
is distributed under the terms of either GNU LGPL v.3 or Creative Commons BY-SA 3.0 license." The set takes the 55
files from 3.0.0 under the LGPL v3, whose text the tarball ships as `COPYING_LGPL` (here `COPYING_LGPL-gnome-icon-theme-3.0.0`), with
the GNU GPL v3 beside it as `COPYING_GPL3-gnome-icon-theme-3.0.0` (the LGPL v3's base text, carried for completeness,
copied from the GNU text rather than the tarball, which does not ship it); the
alternative's notice is `COPYING_CCBYSA3` (here `COPYING_CCBYSA3-gnome-icon-theme-3.0.0`), which asks for the
attribution "GNOME Project". Credit: the tarball's `AUTHORS`, here `AUTHORS-gnome-icon-theme-3.0.0`.

gnome-icon-theme 2.30.3's `COPYING`, copied here verbatim as `COPYING-gnome-icon-theme-2.30.3`, is the text of the GNU
GPL v2, the tarball's only license statement (no per-file notice: the sheets' `cc:license` is empty; 3.0.0's NEWS
dates its "new license" to 2.31.0). The two files taken from 2.30.3, DocumentRevert and DialogInformation, are under
it. Its `AUTHORS` is byte-identical to 3.0.0's.

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
| ViewHidden | Toggle Disabled | 3.0.0 `src/audio-volume.svg`, slot `audio-volume-muted` 48x48 at (696.00037, 350) (Mist carries no `audio-volume-muted`) | — | — | `40309b0d0eeb47ac1927d7bc00d58c5561fcbc00ed5eb4736b4e1c7239a9455e` |
| InsertLink | Toggle Inherit | 3.0.0 `src/insert-link.svg`, slot `insert-link` 48x48 at (296, 50) | — | — | `e1a734fdd0ec395e9e926b3a0147580eec1109089c264da8cf29a968bba8f4ed` |
| Merge | Flatten (Ctrl+F: "the terms go") | 3.0.0 `src/edit-clear.svg`, slot `edit-clear` 48x48 at (296, 50) | — | — | `4d493da64903f1f0e6bdc1fb7b688007f05e5d82b3c6684d68f9e61feb9a3d1b` |
| BlackSum | Toggle Cumulative | 3.0.0 `src/accessories-calculator.svg`, slot `accessories-calculator` 48x48 at (296, 50) | — | — | `a4e2d1df853ecd36b30a2b2345d0d1d7104255b72b121dfa9bc50dba409cd38b` |
| GoJump | Toggle Follow | 3.0.0 `src/start-here.svg`, slot `start-here` 48x48 at (296, 50) | — | — | `210099402c0a9d5992227779bc6729133484de6030bd57812d564426db503bfa` |
| TimelineLift | Toggle Restrict Undo to Current View | 3.0.0 `src/displays.svg`, slot `system-lock-screen` 48x48 at (696.062, 349.996) | — | — | `0f416740e051c586db79849d0b138959a95a131a3cc781fe9aef79a815e45c1f` |
| MusicNote16th | BPM Iterations | 3.0.0 `src/audio-x-generic.svg`, slot `audio-x-generic` 48x48 at (296.062, 49.9963) | AudioXWav, AppIcon | — | `af9057ecdb54541c0ce57d1b9e453dc825f0cfd3bfb554fde2afc3e83b234ad0` |
| Mathmode | Toggle Grid Iterations | 3.0.0 `src/paper-sheets.svg`, slot `x-office-spreadsheet` 48x48 at (1500, 354) | — | — | `b4be444999e33cba0652aae7f060f1def713223d158555ca9ceac2d95d8db12d` |
| PreviewRenderOn | Play Renders | 3.0.0 `src/applications-multimedia.svg`, slot `applications-multimedia` 48x48 at (296, 50) | — | — | `f7b387398baed6508ae4bfef7ec60ce354207bdcbf355a9e57a783a98f33ef80` |
| DialogOkApply | Load in Place | 3.0.0 `src/emblem-default.svg` (sheet sha256 `22061a5dbc7fe2c7ab2293ba1c18cc53e6bef745e0285dc5747c566c99c0c48a`, 2.30.3's byte-identical), slot `emblem-default` 48x48 at (296, 50) (Mist carries no `emblem-default`) | — | — | `c4d44a785f118c8716b46ade8e306322d4bff9a47cd09a3ca13561b7a009c293` |
| VcsDiff | Toggle History View | 3.0.0 `src/clocks.svg`, slot `document-open-recent` 48x48 at (696.062, 355.996) | — | — | `129566f29230db701d06c68dfd99940c88a667fe42f0e6b9b9921f7c8b9ea21d` |
| ShallowHistory | Toggle History Walk | 3.0.0 `src/x-office-calendar.svg`, slot `x-office-calendar` 48x48 at (296, 50) (Mist carries no `x-office-calendar`) | — | — | `5be1fdbb9349b36520c022200165c9d0d755feb95075e56970006895243820a9` |
| EditSelect | Toggle Add to Selection | 3.0.0 `src/paper-sheets.svg`, slot `edit-select-all` 48x48 at (1500, 654) | — | — | `245ce65757ef40dce015a3fc99604e0a604678fae2032f74f959a5af06aeaf8d` |
| KeyframePrevious | Older (`,`) | 3.0.0 `src/media-control-icons.svg`, slot `media-seek-backward` 48x48 at (206, 150) | — | — | `2ca7796acc08333c3995f99fa6bfdb55198f2b11a8360d6ce6afa8e8ddc1e278` |
| KeyframeNext | Newer (`.`) | 3.0.0 `src/media-control-icons.svg`, slot `media-seek-forward` 48x48 at (406, 150) | — | — | `f34d18b3587fc220d4e832cf101c98ebad39dcddc14f0ec17610446fc9c942b6` |
| GoPrevious | Left (the playhead step) | 3.0.0 `src/navigation-icons.svg`, slot `go-previous` 48x48 at (536, 50) | — | — | `3155504fad949f054c6d88a4a550eae714b05763d1d9266188e9ca2ad289f336` |
| GoNext | Right (the playhead step) | 3.0.0 `src/navigation-icons.svg`, slot `go-next` 48x48 at (776, 50) | — | — | `d047a56474e708c114610291a130460e791531e46f1d0d5c9443a32b2112291c` |
| DocumentRevert | Revert (`v`, to the viewed checkpoint) | 2.30.3 `src/paper-sheets.svg` (sheet sha256 `36d3b13179212c51df4c7a07665933295b42aef0800fbf5f80e8bee7225ac570`), slot `document-revert` 48x48 at (1899.9999, 654) (Mist carries no `document-revert`) | — | 2.30.3's slot, 3.0.0 ≠ 2.30.3 (GPL v2); no mask to strip | `3b019e62092065d68a5b366fb68d3c4a784d7895d47761fa9212cbe4854d8628` |
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
| DialogInformation | a NORMAL card | 2.30.3 `src/dialog-information.svg` (sheet sha256 `c5512bea2e4eee6f61b34027874ad0eb1a46f00f4e54a3c6a895e23570420c2e`), slot `dialog-information` 48x48 at (296, 50) (Mist carries no `dialog-information`) | — | 2.30.3's slot, 3.0.0 ≠ 2.30.3 (GPL v2) | `ac5c58ad79cecb62f552154ef467db94029ad6361e7c74d4b25de632dea5a08a` |
| DialogError | a CRITICAL card | 3.0.0 `src/edit-delete.svg`, slot `edit-delete` 48x48 at (296, 49) | — | — | `796aaa833db70a55f292380e601890402871575a87fe501d7038dc57c88b246b` |
| WindowClose | Close (the render player) | 3.0.0 `src/window-close.svg`, slot `window-close` 48x48 at (296, 50) | — | — | `5ea22cd3007a61f65ceb55dee86a3286579bde37b0f29b22a6b3aa2399060dc1` |
| EditCopy | Copy Resolved Value | 3.0.0 `src/copy-paste-tasks.svg`, slot `edit-copy` 48x48 at (696, 50) | — | — | `7e7b8a4a65137cd6726f2fe6b3756c007586b23637f512451199092c9de16e97` |
| HelpWhatsthis | Toggle Tooltips | 3.0.0 `src/help-browser.svg`, slot `help-browser` 48x48 at (13, 37) | — | — | `73dab752241b092c65e22f7345ae6d29354e31cb86530e29d2421fe13b489dfc` |
| GoJumpDeclaration | Jump to Defining Marker | 3.0.0 `src/navigation-icons.svg`, slot `go-jump` 48x48 at (26, 50) | — | — | `1624ee1168fbc6ddb1dbcaa45c90e7ca2c817f12408b3ed737b67d369e4afbd1` |
| AccessoriesTextEditor | Open Text Editor (Return) | 3.0.0 `src/accessories-text-editor.svg`, slot `accessories-text-editor` 48x48 at (296.062, 39.9963) | — | — | `2641037dac14ccffe5a6b9071fa4dbc32960fa893ade679425a0c53db45b2c84` |
| EditDelete | Delete Folder | 3.0.0 `src/trash.svg`, slot `user-trash` 48x48 at (696, 50) | — | — | `52a0f172d734ee00d7b819644cbe9c878794e8628e462b81bbcd24e69f4f886a` |
| AppIcon | the caption's icon and the program icon outside the window (on #EDECEB: `tools/app_icon/gen_app_icon.sh`) | 3.0.0 `src/audio-x-generic.svg`, slot `audio-x-generic` 48x48 at (296.062, 49.9963) | MusicNote16th, AudioXWav | — | `af9057ecdb54541c0ce57d1b9e453dc825f0cfd3bfb554fde2afc3e83b234ad0` |
