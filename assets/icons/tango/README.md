# The Tango icons (the product's icon set)

The product's icons, one SVG per `icons::Icon` enumerator (`src/gui/icons.h`), each file named by its enumerator:
TANGO 0.8.90'S SCALABLE DRAWINGS, the model ReactOS follows on its Windows 2000 desktop (architect 2026-10-06:
Tango's own files only, nothing drawn, no GNOME file), the chrome spec's set (`src/gui/chrome_spec.h`'s `icon_set`);
the set the product drew itself before is git history.

Every file is a 48-unit Inkscape drawing COPIED BYTE FOR BYTE from Tango's `scalable/` tree: never edit one (an edit
breaks the provenance below). The program parses the files AT LAUNCH, in place, through resvg (`src/gui/svg_icon.h`)
— on the laptop from this folder, on the tablet from the APK's `icons/tango/` assets (`android/app/build_apk.sh`) —
and a file that is not well-formed SVG fails the launch, naming the file. A construct resvg does not draw is skipped
silently, so the set was checked at its import on a sheet against rsvg-convert (resvg sits 0.5 levels of 255 from it
at 72 px, the median over the 58 files; DocumentOpen joined on 2026-10-07 as a byte copy of a file already checked,
and the same day's two seat changes, below, put drawings the set already wore on DialogOkApply and DocumentRevert,
re-checked on a sheet all the same: 0.70 and 0.88 at 72 px, 1.56 and 2.43 at 33; ShallowHistory's calendar, below, a
file the set did not carry before, 0.69 at 72 px and 1.97 at 33). Files of other names in this folder
(this README, the licence texts) are not read.

THE TWO SEATS OF 2026-10-07 (architect: "the arrows pointing at each other become Load in Place, and the clock
becomes Revert"): DialogOkApply (Load in Place) wears `actions/view-refresh`, the reload's arrows — the walked version
loaded into the editor in place; DocumentRevert (Revert) wears `actions/appointment-new`, the clock with its small
star — back to the viewed checkpoint in time. Open Project's DocumentOpen keeps `actions/document-open`, which Load
in Place wore until then.

THE CALENDAR OF 2026-10-07 (architect ~04:45, resolving the clock the two history seats then shared: Revert keeps
`appointment-new`): ShallowHistory (Toggle History Walk) wears `mimetypes/x-office-calendar`, the dated page — the walk
through dated commits. Its drawing is a path, with no `<text>` for resvg to skip.

THE MUTED SPEAKER OF 2026-10-07 (architect ~15:45, ruled for both sets: "Agree. Muted"): ViewHidden (Toggle Disabled)
wears `status/audio-volume-muted`, the speaker with its red cross — a disabled marker is a marker the render does not
hear; `status/dialog-error`, worn until then, read as delete. The file came from the 0.8.90 tarball (its sha256 above
verified at the download) and is byte-identical in Debian's 0.9.0 orig. Its import check, on a sheet of its own the
same way: 0.87 at 72 px and 2.10 at 33 from rsvg-convert. The enumerator keeps its name (the research records under
`docs/research/` spell it, so a rename would not be mechanical).

REPEATS, known by position (the files byte-identical): `mimetypes/audio-x-generic` is AppIcon (the caption and the
program icon), AudioXWav (a wav row) and MusicNote16th (BPM Iterations); `actions/view-refresh` is DialogOkApply
(Load in Place) and MediaRepeatSingle (Toggle Repeat One); `actions/process-stop` is DialogCancel and WindowClose;
`actions/go-up` is GoUp and GoParentFolder. Every other file is worn once: 59 enumerators over 54 distinct drawings.

## Provenance

The bytes are the Tango Desktop Project's icon theme 0.8.90 release (`tango-icon-theme-0.8.90.tar.gz`, sha256
`6e98d8032d57d818acc907ec47e6a718851ff251ae7c29aafb868743eb65c88e`), each file also byte-identical in Debian's
`tango-icon-theme_0.9.0.orig.tar.xz` (http://deb.debian.org/debian/pool/main/t/tango-icon-theme/, sha256
`82c5c17fe905961c099fa4100192335e7d376478e8b806ed8465f4d861ed4b92`), the reproducible source for every byte here.
The table's last column is each file's sha256.

## Licence

Tango 0.8.90's `COPYING`, copied here verbatim as `COPYING-0.8.90`, is one line: "The icons in this repository are
herefore released into the Public Domain." Tango 0.9.0 relicensed the same bytes under Creative Commons Zero 1.0
Universal, whose text is copied here verbatim as `CC0-1.0`; its `COPYING` reads:

```
Everything in this package is released under the Creative Commons Zero
1.0 Universal (see CC0-1.0), except for the following files:

- 32x32/mimetypes/application-pdf.png
- 32x32/mimetypes/application-pdf.svg
- scalable/mimetypes/application-pdf.svg

which are published under the GNU Lesser General Public License 2.1 (see
LGPL-2.1), because they reuse the PDF logo from Nuvola icons in
gnome-themes-extras-0.9.0. Nuvola icons were released under the
LGPL-2.1.
```

None of the three excepted files is taken. Either dedication alone suffices; CC0 is the fallback where a
public-domain dedication is not recognised. Credit: Tango 0.8.90's `AUTHORS`, copied here verbatim. The APK carries
the SVG files only; this repository carries the texts.

## The icons

| enumerator | act | Tango file | repeats with | sha256 |
|---|---|---|---|---|
| DocumentOpen | Open Project (Revert its shift twin) | scalable/actions/document-open.svg | — | `16d5d4ac93a43413602fb93624b3a664c04d0fe3fd0066dd60c7121f14815549` |
| DocumentSave | Save | scalable/actions/document-save.svg | — | `1f6449445b659c35880150969eee3c728e463ccc2d2151abbad77294021cc24f` |
| EditUndo | Undo | scalable/actions/edit-undo.svg | — | `9c81f5f14aa7a1457faf14ecd1bfc255ed16cef15a661b5f30d23f0d385450c9` |
| EditRedo | Redo | scalable/actions/edit-redo.svg | — | `68299b630e813ac2218465c9df40d54b47100f7c66ea445e374f3216225a5a55` |
| MediaRecord | Render (and the sweep) | scalable/actions/media-record.svg | — | `9c5dde38e0ee240890da78d38d298a1bf57ae87d1c472ac27a8be82aab4f267a` |
| VcsCommit | Save and Commit (Save's face in `h`) | scalable/actions/go-top.svg | — | `269303b8de1722e4f2e3964779a1423d55a5f9b61ad8ece421b723ffc1e7aa98` |
| VcsPull | Pull (Save's face, GitHub ahead) | scalable/actions/go-bottom.svg | — | `85f182e535869d45d0b29bba4047989987d414bfc1216f79e608a42fb78d1157` |
| DocumentExport | Source+Warp (view 1) | scalable/actions/format-indent-less.svg | — | `6322f892249e93f9af54ff17d56060e8fa1d64a418cdabf35bc7b0e625e06404` |
| DocumentImport | Target+Warp (view 2) | scalable/actions/format-indent-more.svg | — | `582758f55f613489b0144f8206db74a853d7808b6470d2291efb75fa6676a646` |
| ChronometerStart | Target+Phase (view 3) | scalable/actions/format-justify-fill.svg | — | `3544d40afd279da388a41fae3956370f6c938332ed0f2710c07b533e20ae4b3a` |
| ZoomFitBest | Full Zoom Out | scalable/actions/view-fullscreen.svg | — | `b8780e8d56497b87e2966c9e85136ed097e26f33c4b2097bc34cfc6508ff0108` |
| ZoomOriginal | Center on Focus | scalable/actions/edit-find.svg | — | `5b76234b96e715b2f91dc87de17eefa3d1c0f310e0c91c864320f992f34f06e5` |
| ZoomInY | Toggle Waveform Magnification | scalable/actions/system-search.svg | — | `efc8aa5a1610b4ded65618aca577558d0439a7f0dead14d39210b16e8a348ac9` |
| ListAdd | Drop Marker | scalable/actions/list-add.svg | — | `6d6515bcc6d5f651d1075d7d28d27c2502003577f858d1b7581e3df7878512a7` |
| ListRemove | Delete Markers | scalable/actions/list-remove.svg | — | `bb93d1ec24513e3c8f41a176678ee5385f56e7bb0e5925ce8bcbdfe1679868a5` |
| ViewHidden | Toggle Disabled | scalable/status/audio-volume-muted.svg | — | `d14cad8e9b7c671eb94a989fac42135d01502fddf6b2bad79d1afc6222ba95d6` |
| InsertLink | Toggle Inherit | scalable/emblems/emblem-symbolic-link.svg | — | `55bc9af77c6ceab32208ccd16b05b6d469e105a24dc2767c23089221d1498cbc` |
| Merge | Flatten (Ctrl+F: "the terms go") | scalable/actions/edit-clear.svg | — | `e91169a5b89351a361035e5fdb2f1da6335e86bc7e8663a87692559d3c4db816` |
| BlackSum | Toggle Cumulative | scalable/apps/accessories-calculator.svg | — | `83d3adb2358ac8d279a37694b006eaa59a53bf6cc957b8a2cc122cb94690a132` |
| GoJump | Toggle Follow | scalable/places/start-here.svg | — | `9cac3ab3f89812c5554d54d8b0a8a84d9de80856e33021344bc468c5200ccfce` |
| TimelineLift | Toggle Restrict Undo to Current View | scalable/actions/system-lock-screen.svg | — | `7e89140af3a3c4ec2bd697871fed9c2837e2a8ec87f3fa495577ae02058a9066` |
| MusicNote16th | BPM Iterations | scalable/mimetypes/audio-x-generic.svg | AppIcon, AudioXWav | `d7619127acfe25edf59eca10be48776bf33d3a2dfa3a4dba9923032cc5dbc05f` |
| Mathmode | Toggle Grid Iterations | scalable/mimetypes/x-office-spreadsheet.svg | — | `71408450477d1b3e76b08fbb1f22b06c6b4f289bb2b8e04ecde3b33b91bdeb2b` |
| PreviewRenderOn | Play Renders | scalable/categories/applications-multimedia.svg | — | `97f2b4c4b59293d0a6a0dd02e6e8973e3c5b5ca6c5e11be4355f4d89c3abc3c1` |
| DialogOkApply | Load in Place | scalable/actions/view-refresh.svg | MediaRepeatSingle | `a43596670cfced66e14bd9ee984288a9d69490f0f08953ad13f2a6bf5e4d70fb` |
| VcsDiff | Toggle History View | scalable/status/image-loading.svg | — | `877fcc4feef211429cb1ebde25949a24a55db8808e317bde3cb22a54e4011001` |
| ShallowHistory | Toggle History Walk | scalable/mimetypes/x-office-calendar.svg | — | `60933d1d2721a300db02a813a31b7426c44e8eb855f9d210ab27b261c1a1f35b` |
| EditSelect | Toggle Add to Selection | scalable/actions/edit-select-all.svg | — | `5699524dbb79821a707d238b4f18a32deca10a66e7c899fb0efb93ee4a449e2a` |
| KeyframePrevious | Older (`,`) | scalable/actions/media-seek-backward.svg | — | `cbd686518c8ce0a9bb3b46535dacb18a0764ea68fbc477056df075c5721d7c1e` |
| KeyframeNext | Newer (`.`) | scalable/actions/media-seek-forward.svg | — | `1c93a0c5ecc19c7bec6df68aa8b6da2022042cb1be470b299a96e6b966c582a4` |
| GoPrevious | Left (the playhead step) | scalable/actions/go-previous.svg | — | `75269150414f9bdfb0eee741ad36209c500d27c0b2d8462b134e0cf7b6d8f7d8` |
| GoNext | Right (the playhead step) | scalable/actions/go-next.svg | — | `e7e79d37fb27d42437e0449affe7b8effbc3391e4450f53f0f88602049f7b30c` |
| DocumentRevert | Revert (`v`, to the viewed checkpoint) | scalable/actions/appointment-new.svg | — | `a450debfdd4e8e0563c0d51edf208079a227f62735ebb27cc1baa89f11bf8d66` |
| MediaSkipBackward | Go to Start | scalable/actions/media-skip-backward.svg | — | `4c399158deade1c6210108b50b58cce29d01be84bf3400f9d80960159517bb75` |
| MediaPlaybackStart | Play | scalable/actions/media-playback-start.svg | — | `b09e773d2a7982eb3c9c2e59115f7b9f7cae4b78abfb59baf8c752cc5626c028` |
| MediaPlaybackStop | Stop | scalable/actions/media-playback-stop.svg | — | `c361b71d78b9f6ff225b863bdbad15ea9b1e1d8744092cea8ba4429c8f4c14de` |
| MediaPlaybackPause | Pause (the render player) | scalable/actions/media-playback-pause.svg | — | `65b004042c5167370c3b1beb99eeeae46716bcdc79f8144bc5192bd2c9c6116c` |
| MediaSkipForward | Go to End | scalable/actions/media-skip-forward.svg | — | `5ef8c15b2a3ae34d7decb4874137dff89b00aa2912c3e040966144b039cf53dc` |
| DialogCancel | Cancel (Render's mid-render face) | scalable/actions/process-stop.svg | WindowClose | `594fca9066549a473625d4a8e603861d079742c13ab6bd69ab6c7a577d77e091` |
| GoDown | Down (the value ladder) | scalable/actions/go-down.svg | — | `4f18bfddfeb4888870ab3cc309ed46418fbb555945508ddbfbb664a0d513f07d` |
| GoUp | Up (the value ladder) | scalable/actions/go-up.svg | GoParentFolder | `7d51d4af61813049ee8cf2d3091099404048a3477e5366e646b19627179cce95` |
| Lock | Toggle Read-Only, locked | scalable/emblems/emblem-readonly.svg | — | `aabc9fc74cd88ae80669f85f66b66ba980c13769d87b70a36e6b79d07c340382` |
| Unlock | Toggle Read-Only, unlocked | scalable/apps/accessories-text-editor.svg | — | `ab79ede7f8b2c1de4b229d3e17c300f270c712c7ddd8a9e9fb1f7b472a26fc89` |
| BboxPrev | Previous Marker (Shift+Tab) | scalable/actions/go-first.svg | — | `465eba822a18240e059c1ad6d68c120ca9dfe134692b160897597bff9b2712f5` |
| BboxNext | Next Marker (Tab) | scalable/actions/go-last.svg | — | `5a24d27249a5b557c4be774e322a642a299c0908412d52b6de847e2f2c8a9d82` |
| TabDetach | Switch Tab | scalable/apps/preferences-system-windows.svg | — | `e7e3bb63358211734430c132fc00064540ca4dd74b6b2e7ea1bc5cbdfdea2c14` |
| SettingsConfigure | Settings | scalable/categories/preferences-system.svg | — | `12c8cf88a3c5c23667243eeab6aeb499c699039433b23ddf766cca8bf11a1990` |
| Folder | a folder row | scalable/places/folder.svg | — | `65e24650244b04139c6bb8a76a5e53382d9a09d4cd3eaa6752eff6ef5fafeff1` |
| AudioXWav | a wav row | scalable/mimetypes/audio-x-generic.svg | AppIcon, MusicNote16th | `d7619127acfe25edf59eca10be48776bf33d3a2dfa3a4dba9923032cc5dbc05f` |
| MediaRepeatSingle | Toggle Repeat One | scalable/actions/view-refresh.svg | DialogOkApply | `a43596670cfced66e14bd9ee984288a9d69490f0f08953ad13f2a6bf5e4d70fb` |
| GoParentFolder | Up a Folder | scalable/actions/go-up.svg | GoUp | `7d51d4af61813049ee8cf2d3091099404048a3477e5366e646b19627179cce95` |
| DialogInformation | a NORMAL card | scalable/status/dialog-information.svg | — | `0695314b6f6153c2d6b10da9bf47a2554c3f7b9bf8c0fedafd716c33a3603708` |
| DialogError | a CRITICAL card | scalable/emblems/emblem-unreadable.svg | — | `7b54655d45b437976aa546f9f200700ae2d6edb3fd471dba5533171a8718512d` |
| WindowClose | Close (the render player) | scalable/actions/process-stop.svg | DialogCancel | `594fca9066549a473625d4a8e603861d079742c13ab6bd69ab6c7a577d77e091` |
| EditCopy | Copy Resolved Value | scalable/actions/edit-copy.svg | — | `ed87900f9236c019d0285632ae9634e4c9ed6c51e28d870361966f33a47bf644` |
| HelpWhatsthis | Toggle Tooltips | scalable/apps/help-browser.svg | — | `f1c955b657a686bc7acc433a597db25c0b733f2678b67b8f26d2946cda543d00` |
| GoJumpDeclaration | Jump to Defining Marker | scalable/actions/go-jump.svg | — | `da4e4493791e6e53325c72f67e61c4ef21d148edda740256beb48c7e72f062f6` |
| EditDelete | Delete Folder | scalable/actions/edit-delete.svg | — | `406e8c8eba860fec81d84fd9fb4b956bfd2e8f1a1432ed3f46c9cd61b3a8b728` |
| AppIcon | the caption's icon and the program icon (the chrome grey ground, #D4D0C8) | scalable/mimetypes/audio-x-generic.svg | AudioXWav, MusicNote16th | `d7619127acfe25edf59eca10be48776bf33d3a2dfa3a4dba9923032cc5dbc05f` |
