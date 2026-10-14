# The GNOME 2.28 icons

gnome-icon-theme 2.28.0, taken whole and self-contained (architect 2026-10-12): every drawing is from the 2.28.0
tarball's `scalable/` tree, nothing from any other release. Its look is the 2.20 to 2.28 GNOME, chosen over 2.30
(the Mist set, removed 2026-10-12 with Tango, GNOME 2.28 the one set). One SVG per `icons::Icon` enumerator (`src/gui/icons.h`), each named by its enumerator and COPIED BYTE FOR
BYTE from the tarball: never edit one. THE ONE EXCEPTION is TabDetachB (architect 2026-10-12), the product's own derived file, below. The set is hard-coded (`icons::kIconSet`; the `icons` device key and the Settings Icons
row were removed 2026-10-12). The program parses the files at launch, in place, through resvg (`src/gui/svg_icon.h`): on the laptop
from this folder, on the tablet from the APK's `icons/gnome-2.28/` assets (`android/app/build_apk.sh`); a file that is
not well-formed SVG fails the launch, naming the file. Files of other names in this folder (this README, the licence
texts) are not read.

Two seats are deliberate (architect 2026-10-12): DocumentSave wears `devices/media-floppy`, the old diskette; Lock
wears `actions/system-lock-screen`, the monitor with the padlock (architect 2026-10-12: emblem-readonly is nearly monochrome and does not read). DialogError wears `process-stop`, since 2.28's `edit-delete` is the bin. Every other
seat is the freedesktop name the removed Mist set used. THE READ-ONLY TOGGLE wears Lock; its lit face
says read-only.

REPEATS, known by position (the files byte-identical) are listed in the table.

## Provenance

- gnome-icon-theme 2.28.0, `gnome-icon-theme-2.28.0.tar.bz2`
  (https://download.gnome.org/sources/gnome-icon-theme/2.28/), sha256
  `31ecbd459ae059672793e46c773742897cb95e22ca5900ccdfb2a22aa30f3884` (GNOME's own `.sha256sum`).

## Licence

The tarball's `COPYING`, copied here verbatim as `COPYING-gnome-icon-theme-2.28.0`, is the text of the GNU GPL v2, and
it is the tarball's only licence statement: there is no "or later" and no per-file notice. Its `AUTHORS` is here as
`AUTHORS-gnome-icon-theme-2.28.0`.

How the files sit beside the GPL-3 program: each SVG is a separate data file under its own licence, read at run time
and never linked or compiled into the program's code; the repository distributes source only (THIRD_PARTY.md's own
statement).

## The one derived file

`TabDetachB.svg` is the product's own edit of `preferences-system-windows.svg` (2026-10-12): the element order only, the
group `g2865` (the lower right window) moved after `g2820` (the upper left window) so the back window paints in front;
every coordinate is unchanged. Switch Tab wears `TabDetach.svg` on tab A and this file on tab B: the same two windows,
the one in front the tab shown. GPL-2 section 2(a)'s changed-file notice is the comment inside the SVG, naming the
change and the date; the original `TabDetach.svg` stays byte-identical to the tarball's. Its hash is in the table.

## The import check

All 59 tarball files (TabDetachB, derived, added 2026-10-12) were rendered through rsvg-convert at 60 px onto one contact sheet and looked at; FormatJustifyLeft and FormatJustifyRight (2026-10-13, replacing ChronometerStart — 60 tarball files now) were rendered the same way beside the view group's indents (the product's own
resvg road was not run for this import). EditCopy left the set 2026-10-14 with the Copy Resolved Value button (59
tarball files now).

## The icons

| enumerator | freedesktop name | tarball file | repeats with | sha256 |
|---|---|---|---|---|
| AccessoriesTextEditor | `accessories-text-editor` | `scalable/apps/accessories-text-editor.svg` | — | `b84132b03b776d3e66d5dc61a8e6562b4974b3cee2f59f265a73f263b903cbc8` |
| AppIcon | `audio-x-generic` | `scalable/mimetypes/audio-x-generic.svg` | AudioXWav, MusicNote16th | `2f3df256957baa0f1ab855dc0e50511d397c32754c2110a60aa39bb432a8daf8` |
| AudioXWav | `audio-x-generic` | `scalable/mimetypes/audio-x-generic.svg` | AppIcon, MusicNote16th | `2f3df256957baa0f1ab855dc0e50511d397c32754c2110a60aa39bb432a8daf8` |
| BboxNext | `go-last` | `scalable/actions/go-last.svg` | — | `2d695d317dff9ad2543c35cb8df73d19151dd57fda4dd7856f5033ca62167ae2` |
| BboxPrev | `go-first` | `scalable/actions/go-first.svg` | — | `beff3e1fdf8cc9b0078716394681834f22e57c229e665a11dbe27c3875179d33` |
| BlackSum | `accessories-calculator` | `scalable/apps/accessories-calculator.svg` | — | `fd31f314578f455ed3fe8a42e028bbf2343d6e91d0cdf7912cf8505162f18981` |
| DialogCancel | `process-stop` | `scalable/actions/process-stop.svg` | DialogError | `d8197817011686293296a6db05aa09140e9eb4a848572193658695062008bec0` |
| DialogError | `process-stop` | `scalable/actions/process-stop.svg` | DialogCancel | `d8197817011686293296a6db05aa09140e9eb4a848572193658695062008bec0` |
| DialogInformation | `dialog-information` | `scalable/status/dialog-information.svg` | — | `1b6a744e60e9781b55f44c768f7a1d5c2c17ad85eedab15e6c44506e629be304` |
| DialogOkApply | `emblem-default` | `scalable/emblems/emblem-default.svg` | — | `4483a667eed895f8be6de89c11a4f5b307f0d9c2c77e240395a15c27c93a87a6` |
| DocumentExport | `format-indent-less` | `scalable/actions/format-indent-less.svg` | — | `7b086d2261f9118de2230ac6036c858e7fe66bbf6370a2b40dafdf60c6a1eead` |
| DocumentImport | `format-indent-more` | `scalable/actions/format-indent-more.svg` | — | `ee8195812f51a51cfccba65bce7af8a5dc46bf566a149dc695bd2e674d98532e` |
| DocumentOpen | `document-open` | `scalable/actions/document-open.svg` | — | `037f0c8f2909644b0059e1c2414b02b3ebf262298a43ffb82d79ae0c8b3b92f5` |
| DocumentRevert | `document-revert` | `scalable/actions/document-revert.svg` | — | `dd62fd39c75eb80b5a712c5e17a5bbf3c1fe6d2ffe7bdddab08cdbe841050984` |
| DocumentSave | `media-floppy` | `scalable/devices/media-floppy.svg` | — | `3ec9d7715ac3f89f1d744e91855ba0b8fb6047c3bb7c98440619fffbdfe42f9d` |
| EditDelete | `user-trash` | `scalable/places/user-trash.svg` | — | `f7d4b0408b6fd7cb773d321deaa43b671c218b8c4b44055adabc9e77a3cf0403` |
| EditRedo | `edit-redo` | `scalable/actions/edit-redo.svg` | — | `917d726b47cba84752a96c7496f7d965ccf07e544b8c57abefdf0dfb643fd368` |
| EditSelect | `edit-select-all` | `scalable/actions/edit-select-all.svg` | — | `ee70e1b5911d35a82ac7feecf1cd0add3721dd92557295356750681accde234d` |
| EditUndo | `edit-undo` | `scalable/actions/edit-undo.svg` | — | `f3a284c14061c8058a977899155f2467e79d8085ccff2dc7b39ff9cf049b3358` |
| Folder | `folder` | `scalable/places/folder.svg` | — | `e33653b29756f4f6f76d7f57e010c62fadc061b1ad9dcfa5fd57fd7fc7f54aca` |
| FormatJustifyLeft | `format-justify-left` (architect 2026-10-12: Source+Phase) | `scalable/actions/format-justify-left.svg` | — | `a380b382ea062f0b509bb445701e615cb49802e6c04204c3d4dda4187da9dc40` |
| FormatJustifyRight | `format-justify-right` (architect 2026-10-12: Target+Phase, which wore `format-justify-fill` as ChronometerStart until then) | `scalable/actions/format-justify-right.svg` | — | `15963f2e53a4685b4d04ed5b4f37cef541f733af0bf923ac7c4a5a8a565a280d` |
| GoDown | `go-down` | `scalable/actions/go-down.svg` | — | `8fabac55ce593bcc03bf40bfe7bf8e41f93a0818328bf296c4bdc97ed7c5109c` |
| GoJump | `start-here` | `scalable/places/start-here.svg` | — | `4811e6060d88fa7b921a786732cc4227c43b1cc448071b2c52af5a20741e8bbc` |
| GoJumpDeclaration | `go-jump` | `scalable/actions/go-jump.svg` | — | `4d53f8358b6c0f9f5a4155f8a6b012286a942e0e3c047133b8dbe2d154ba538d` |
| GoNext | `go-next` | `scalable/actions/go-next.svg` | — | `f80e6b62d54b8162e81b5f3836ad0c1c1a502a5008e667962a700739e6ac4e3a` |
| GoPrevious | `go-previous` | `scalable/actions/go-previous.svg` | — | `13a78414f2dcd636e78335b7b662825d0757ed14fbb36c7ae0b84b4df1921ae8` |
| GoUp | `go-up` | `scalable/actions/go-up.svg` | — | `f1601611c2c67bfde72af37c30f2f90f83197047131fdd13a2a781ab3ceed9b1` |
| HelpWhatsthis | `help-browser` | `scalable/apps/help-browser.svg` | — | `1b942b0efd5f98c2963972cb30affea2b12e696bb0566aedf01d5a541793dc07` |
| InsertLink | `insert-link` | `scalable/actions/insert-link.svg` | — | `45f4849115c9b3cc8885f25fbfcf6f90dbc371a17a90931f6999bb04177d705f` |
| KeyframeNext | `media-seek-forward` | `scalable/actions/media-seek-forward.svg` | — | `666c5a6d2259ce05e7e22fdea34f4f5f616b788c00370394b23dd42753d090b8` |
| KeyframePrevious | `media-seek-backward` | `scalable/actions/media-seek-backward.svg` | — | `1786b697e7925a0b948f9c9bbc09e94acb58b47f3fd7a33e5fe95ebf328c28e5` |
| ListAdd | `list-add` | `scalable/actions/list-add.svg` | — | `7bda3185d186632a1c94f445c2fd294dca958e1f4bf9d7ac7c8d3a4367858315` |
| ListRemove | `list-remove` | `scalable/actions/list-remove.svg` | — | `0523527a96ecd792973dcbd68df270805e6268a2f778ec5a795554d90c34765f` |
| Lock | `system-lock-screen` | `scalable/actions/system-lock-screen.svg` | — | `0fee2e7c4f54bcb3cf35b495cbca9a816f61620518267626d17fb846886dd0cf` |
| Mathmode | `x-office-spreadsheet` | `scalable/mimetypes/x-office-spreadsheet.svg` | — | `81c124da8fb1cdab0f223a4b54162923b53d04c36909ae358180696fc76303cc` |
| MediaPlaybackPause | `media-playback-pause` | `scalable/actions/media-playback-pause.svg` | — | `b1256da6927c2604836f14b862b62745c2873b316155f4643fc3166a101cf12e` |
| MediaPlaybackStart | `media-playback-start` | `scalable/actions/media-playback-start.svg` | — | `236ed91f2ec0e924d0575a7e53f3f1c939abce61dbf2b8a90361e616a4f9219c` |
| MediaPlaybackStop | `media-playback-stop` | `scalable/actions/media-playback-stop.svg` | — | `02da388e41869b4b2233ad244b3349f6f0f7ba3be69dbf4ef4e7561a03221926` |
| MediaRecord | `media-record` | `scalable/actions/media-record.svg` | — | `0f7aacbd02966cd723c3411e62eb00d79b653047a6130c8cc09b73f91709f899` |
| MediaRepeatSingle | `media-playlist-repeat` | `scalable/status/media-playlist-repeat.svg` | — | `ea5cc1bc844f8a979d19473fee8d92bb1c5c164893087a9efd9e13927c4644bd` |
| MediaSkipBackward | `media-skip-backward` | `scalable/actions/media-skip-backward.svg` | — | `5cb2653ce112d4b06fba4a4d042c2f2b58feb37d167d2e6ec0434bd303561756` |
| MediaSkipForward | `media-skip-forward` | `scalable/actions/media-skip-forward.svg` | — | `11a205f77de6b4fcc640e7520fab43ed5fe56151fda7d0e275111c0459972c73` |
| Merge | `edit-clear` | `scalable/actions/edit-clear.svg` | — | `bf7e3ff2ddbacd0e53dafb12c343298ac0911713a95260cb0861a9fdc35d6211` |
| MusicNote16th | `audio-x-generic` | `scalable/mimetypes/audio-x-generic.svg` | AppIcon, AudioXWav | `2f3df256957baa0f1ab855dc0e50511d397c32754c2110a60aa39bb432a8daf8` |
| PreviewRenderOn | `applications-multimedia` | `scalable/categories/applications-multimedia.svg` | — | `b544e51b44bae6fa5e95aec212ba6566eb776af8bc543f2420108d1ebaee45da` |
| SettingsConfigure | `preferences-system` | `scalable/categories/preferences-system.svg` | — | `fe6e0e022ef3ea6c1a3b979a930c9d3404efd21dd48fd71ad4eda453701f38aa` |
| ShallowHistory | `x-office-calendar` | `scalable/mimetypes/x-office-calendar.svg` | — | `2cee419db4b8f565828b1718ff10a7f9ebe6ae622e51b726a855618a5e9751a8` |
| TabDetach | `preferences-system-windows` | `scalable/apps/preferences-system-windows.svg` | — | `5b198b46dfcb737528049fff9881c9d53d82824ad73c87a6c7723817ba8923d5` |
| TabDetachB | `preferences-system-windows` (CHANGED FILE, below) | `scalable/apps/preferences-system-windows.svg` | — | `4a82be25026c3befe2cf7c4f739d16db008e5a1c10266e6dd69a9b5a5ee457d5` |
| TimelineLift | `view-fullscreen` (architect 2026-10-12: view-restore's tiny windows left the case mostly empty) | `scalable/actions/view-fullscreen.svg` | — | `30d89134642e45f2e08299a8cde3d3bb95199f3355c5ba62cc3f2e752dd0527c` |
| ValueDrag | `object-flip-vertical` | `scalable/actions/object-flip-vertical.svg` | — | `5fb09615159e44c02a584cfa048dcb8ddd0b663111da4ea6be5c26851a6fbd0b` |
| VcsCommit | `go-top` | `scalable/actions/go-top.svg` | — | `c9f7ee737b3ebaa57c40339b9d738b5525ad0f7d088008710a1e6a810a0e7470` |
| VcsDiff | `document-open-recent` | `scalable/actions/document-open-recent.svg` | — | `38824dc638b0c1cfa7b32ed818dfc7c630e31539d2255f38f03a9feb72175dc3` |
| VcsPull | `go-bottom` | `scalable/actions/go-bottom.svg` | — | `7050a2ad1d03beb9a539da7af0cadfb92f1666013751311681e71235cf425fe1` |
| ViewHidden | `audio-volume-muted` | `scalable/status/audio-volume-muted.svg` | — | `156eb03ce9c8b213350c728884ee1ee4fae3d1f9636c00a96f2013e7b427b550` |
| WindowClose | `window-close` | `scalable/actions/window-close.svg` | — | `3732bbc024751b25b628dff4dba35241a2e1955b528d80518884e569d646ccaa` |
| ZoomFitBest | `zoom-fit-best` | `scalable/actions/zoom-fit-best.svg` | — | `51638162efffd4c36f788cb4dd6eec1893603cb433a4bc29b9f1011bcb3937df` |
| ZoomInY | `zoom-in` | `scalable/actions/zoom-in.svg` | — | `a463e274f24ea34b0be7c2ec69cad25fdb5cfc50132aaf09bcef200a20a3f83a` |
| ZoomOriginal | `zoom-original` | `scalable/actions/zoom-original.svg` | — | `989ba03465dbf4f7bc64232d10a63c55de12d857f2d86f555e6dc7fe88b7bf35` |

The product draws ten arrow glyphs translated in user units (icons.h's kGnome228Shifts, architect 2026-10-12, the arrow body centered in its seat); these files stay byte-identical to the originals, the shift is the product's, not the file's.
