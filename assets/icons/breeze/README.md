# The Breeze icons (the neutral set)

One SVG per `icons::Icon` enumerator (`src/gui/icons.h`), each file named by its enumerator: KDE'S BREEZE SYMBOLIC
DRAWINGS, MONOCHROME, the neutral set the architect tunes colors under (architect 2026-10-10: "a neutral set that will
help me with tuning the colors. There should just be one breeze icon [set], but it should take whatever the font color
is that sits on the face, the chrome. It's okay if they lose some information in the process"). It is no chrome's own
set: the device config's `icons=breeze` (the Settings menu's Icons row) chooses it under any chrome, at the next
launch. The drawings are the ones the product wore from August 2026 until 2026-10-06 (git `b5bdea6e^`, where they
stood under their Breeze names), chosen per enumerator then; DocumentOpen, the one enumerator that joined after
(Open Project, 2026-10-07), takes Breeze's `actions/22/document-open`.

THE RECOLOR RULE (architect 2026-10-10). Every fill and stroke that was a color — the Breeze color scheme's text
(#fcfcfc in the dark theme's files), its accent (#3daee9), its negative text (#da4453) and the other reds, white —
is `currentColor`; `fill:none` / `stroke:none` and every opacity stay. The files carry NO literal color and no
Breeze color-scheme `<style>` block or `class` attribute, so `currentColor` resolves against the color THE LOADER
SUPPLIES: the text role of the surface the glyph stands on (the label on the toolbars, the caption's text on the
caption, the card's text on the cards, the list's text pair on the list rows — `GuiSurface`, `src/gui/render.h`;
the road at `src/gui/icons.h`'s head), following a live chrome pick. What the colors told apart is lost, as the ruling
accepts: the Render disc, Delete's and Remove's red, the half-disc on Play Renders and the red X on Toggle Restrict
Undo are the glyph's one color now. ONE EXCEPTION, the two cards' glyphs, DialogInformation and DialogError: Breeze
draws them as a white mark on a filled rounded square, which one color would turn into a plain square, so the mark
is CUT OUT of the square (one path, even-odd fill) — the card's ground shows through where the white stood.

THE 24 SEAT. Breeze's 22-unit drawings are wrapped in a 24-unit viewBox, translated (1, 1) — Breeze's own 24-px
convention (its `actions/24/` files are the 22-px drawings so wrapped) — so the cell fills the toolbar case's 24-W
seat at 1 unit = 1 W, and a 1-unit stroke is 3 device px at 300 %, crisp. Plain SVG, the default namespace, no
Inkscape attributes, the path data verbatim (AudioXWav's two Inkscape layer transforms kept: they cancel).

The program parses the files AT LAUNCH, in place, through resvg (`src/gui/svg_icon.h`) — on the laptop from this
folder, on the tablet from the APK's `icons/breeze/` assets (`android/app/build_apk.sh`) — and a file that is not
well-formed SVG fails the launch, naming the file. The set was checked at its import against rsvg-convert, both
rendering `currentColor` black: the alpha channel's mean difference per file is 0.13 levels of 255 at 72 px (the
median over the 59 files; 0.76 the largest, DialogError) and 1.49 at 33 (3.60 the largest, DocumentSave). Files of
other names in this folder (this README, the licence text) are not read.

REPEATS, known by position: `mimetypes/22/audio-x-wav` is AudioXWav (a wav row) and AppIcon (the caption's icon) —
Breeze's 64-unit `audio-x-generic`, worn by AppIcon until 2026-10-06, is a full-color drawing, not a symbolic one,
and is not taken. Every other file is worn once: 59 enumerators over 58 distinct drawings.

## Provenance

The drawings are breeze-icons 6.30.0's, KDE Frameworks (https://invent.kde.org/frameworks/breeze-icons), as Arch
Linux installs them under `/usr/share/icons/`: the dark theme's `breeze-dark/<context>/22/` files for all but
DocumentOpen, taken from the light theme's `breeze/actions/22/document-open.svg` (the two themes draw the same paths
and differ only in the color scheme's text color, which the recolor rule replaces). The table's last column is the
sha256 of each SOURCE file in 6.30.0; the files here are their recolored, re-seated transcriptions, with no path
data changed (the August 2026 copies of GoJump, GoNext and GoPrevious differed from 6.30.0's only in trailing
whitespace). Media Record's red disc is the same file in both themes.

## Licence

Breeze's icons are licensed under the GNU Lesser General Public License, version 3 or (at your option) any later
version — LGPL-3.0-or-later (Arch's package metadata for breeze-icons 6.30.0 lists `LGPL-3.0-or-later` and
`LGPL-2.1-only`). The licence text is copied here verbatim from https://www.gnu.org/licenses/lgpl-3.0.txt as
`COPYING-LGPL-3.0.txt`; the LGPL incorporates the GNU GPL version 3, whose text is this repository's `LICENSE`.
Copyright the breeze-icons authors (KDE). The APK carries the SVG files only; this
repository carries the text.

## The icons

| enumerator | act | Breeze file (breeze-icons 6.30.0) | repeats with | source sha256 |
|---|---|---|---|---|
| DocumentOpen | Open Project (Revert its shift twin) | breeze/actions/22/document-open.svg | — | `ef1445399ffbcaa8eb43c40705351f157f4a2b1169ace3d294f264a85f295649` |
| DocumentSave | Save | breeze-dark/actions/22/document-save.svg | — | `4ddc1bf339cf3fd7e873203ed2f02720af78a9d0c58f2053143f8a818853033e` |
| EditUndo | Undo | breeze-dark/actions/22/edit-undo.svg | — | `486bb2f9a684a44717e6f10541ef718b05714a06b35b78310b0866a321ab8138` |
| EditRedo | Redo | breeze-dark/actions/22/edit-redo.svg | — | `efad314c8c890eee0b1af6bad52c0261939c986a91b49c250a2b2597a874e102` |
| MediaRecord | Render (and the sweep) | breeze-dark/actions/22/media-record.svg | — | `a53acc5fa7381ccb9fcc02d218d203ab8a5ab6fddd542569ffca87c885bdbeab` |
| VcsCommit | Save and Commit (Save's face in `h`) | breeze-dark/actions/22/vcs-commit.svg | — | `8297de250e521368ffb12e772dd934f4ef4da51133d8bf8315ebb9d82528c41f` |
| VcsPull | Pull (Save's face, GitHub ahead) | breeze-dark/actions/22/vcs-pull.svg | — | `1b0134c2e1463bc7616ac8e887b2416c585b7a036b835418e46db98644020158` |
| DocumentExport | Source+Warp (view 1) | breeze-dark/actions/22/document-export.svg | — | `837681d590f8d303f8b670ef3957dc7b56effe190996facbae5eb709d0d0188b` |
| DocumentImport | Target+Warp (view 2) | breeze-dark/actions/22/document-import.svg | — | `02080575f136f4beac19f4be12def161ed38db610eea1662a0e5a42772ca27d5` |
| ChronometerStart | Target+Phase (view 3) | breeze-dark/actions/22/chronometer-start.svg | — | `6cda34c0291234402bfd694f81f8841aa05bde86ce13ac68ea8048fa23f9cb81` |
| ZoomFitBest | Full Zoom Out | breeze-dark/actions/22/zoom-fit-best.svg | — | `7c43931b78725877af03061e72a75cf21f397adeda43984d7fd85cbd3693b706` |
| ZoomOriginal | Center on Focus | breeze-dark/actions/22/zoom-original.svg | — | `0af7f88cc96e0d6de138008202a7410b4f096cfd9a1f72afb8a111a363a93305` |
| ZoomInY | Toggle Waveform Magnification | breeze-dark/actions/22/zoom-in-y.svg | — | `aa074bac0f1e26277b0c16888522d55dfa1c744c9ec8261fd1eb5787305f44a3` |
| ListAdd | Drop Marker | breeze-dark/actions/22/list-add.svg | — | `08536709e7fb93104de3c5129936525837fff6c8a17c88090df09c83541d3636` |
| ListRemove | Delete Markers | breeze-dark/actions/22/list-remove.svg | — | `76c364a82eede6569fe79710dc3efdf8be16cf4d23f37e2a31163f8e6b1cd61f` |
| ViewHidden | Toggle Disabled | breeze-dark/actions/22/view-hidden.svg | — | `4a957d696371babc812eef174144723af856482411896dd633238a1c6232e4fa` |
| InsertLink | Toggle Inherit | breeze-dark/actions/22/insert-link.svg | — | `69af6b43d00fd2709f72ce28da318ac5bef2dca486b22abf6e5a46459139bef1` |
| Merge | Flatten (Ctrl+F: "the terms go") | breeze-dark/actions/22/merge.svg | — | `277a1f792bdf141940bc09f8c39d8b8f0ed7e9bbcf93a8f00f73eac8e2eef58a` |
| BlackSum | Toggle Cumulative | breeze-dark/actions/22/black_sum.svg | — | `217bcb4e246e2893edb1fbfbdea0bf60ee1d4d981dea3a54edd74682b8c0e214` |
| GoJump | Toggle Follow | breeze-dark/actions/22/go-jump.svg | — | `6c254b1cd227c165fe0417ec73c23e0a772b5b624ae5505af9c8520fdddb2cdc` |
| TimelineLift | Toggle Restrict Undo to Current View | breeze-dark/actions/22/timeline-lift.svg | — | `73b8d05a180fd553dc95401df4afc264c2dc40e8daa2e404159581fe72849dda` |
| MusicNote16th | BPM Iterations | breeze-dark/actions/22/music-note-16th.svg | — | `1411cf454d39ef5781564fde937acc5b512b8f31f5372b079996680f39bc8c17` |
| Mathmode | Toggle Grid Iterations | breeze-dark/actions/22/mathmode.svg | — | `9aba84e814f66b3e8dc5b70733572d49022cfff882d100936fb12e602906c91d` |
| PreviewRenderOn | Play Renders | breeze-dark/actions/22/preview-render-on.svg | — | `590342541ac99754ec6a59822e70785e7ea9b9b88bb01b0d0c50ff58f8d5daeb` |
| DialogOkApply | Load in Place | breeze-dark/actions/22/dialog-ok-apply.svg | — | `6a9a4cbc6565ff52eb2c5251e7bf2b8b3172cddc1fb1e82e3d0b089e57d6464b` |
| VcsDiff | Toggle History View | breeze-dark/actions/22/vcs-diff.svg | — | `6a9b047a25303375be3a1684122157ad40646121d642692cf315feab53f02881` |
| ShallowHistory | Toggle History Walk | breeze-dark/actions/22/shallow-history.svg | — | `8fa5b7d7eaba2c0e8a0518c6a4ccb85ad95ee98f653bbfa5f6ccfcb0ff0cc492` |
| EditSelect | Toggle Add to Selection | breeze-dark/actions/22/edit-select.svg | — | `23b3466eecfa6c6be02f1f2f3fb65dae61f010319196f526c9a621678cb1afb4` |
| KeyframePrevious | Older (`,`) | breeze-dark/actions/22/keyframe-previous.svg | — | `6170abdd8b79802cdc55973a178424b319b4620cf5b807927fb79aaec9125859` |
| KeyframeNext | Newer (`.`) | breeze-dark/actions/22/keyframe-next.svg | — | `2245dd83a76022af60da4068ec6fd36d4b32b422294acdd13e6c758f466ad838` |
| GoPrevious | Left (the playhead step) | breeze-dark/actions/22/go-previous.svg | — | `7c798a16bc8e2b338f11c6274c1db316b7a02f15f4558d15b12d2959d75abff1` |
| GoNext | Right (the playhead step) | breeze-dark/actions/22/go-next.svg | — | `9405643f10913c6efe7e09517efca10f993353304ddb05bfc2eb7f8694d905d9` |
| DocumentRevert | Revert (`v`, to the viewed checkpoint) | breeze-dark/actions/22/document-revert.svg | — | `1a0532e6416056e6d74f4d636d3774fa3e549b68fb22d117ff692b7fde6afbd2` |
| MediaSkipBackward | Go to Start | breeze-dark/actions/22/media-skip-backward.svg | — | `a3bdb0b0b540184842cf9d8796f601ffabf5a2a67773d4ddc9a23cf9f3e7247b` |
| MediaPlaybackStart | Play | breeze-dark/actions/22/media-playback-start.svg | — | `e1925fda1fd2fc42e1ec6599238c2793e81f9bf978955a02f2d01222d49aecde` |
| MediaPlaybackStop | Stop | breeze-dark/actions/22/media-playback-stop.svg | — | `31c039c86dc46ee21971be63b48e0ba3bce32ce81d2661843267381d13987fd5` |
| MediaPlaybackPause | Pause (the render player) | breeze-dark/actions/22/media-playback-pause.svg | — | `a6a4153334a78f9b12e7cf3daca4ddd6dce1f032c218bc1ad7527858d4f440f0` |
| MediaSkipForward | Go to End | breeze-dark/actions/22/media-skip-forward.svg | — | `a49246a16d87d33ccbc9b8c730992b5624b3393d56a1fe2ddfb51576878e81fe` |
| DialogCancel | Cancel (Render's mid-render face) | breeze-dark/actions/22/dialog-cancel.svg | — | `e292845ffca2e88efbc2494bc9c9d6ddc479b907e2a792c6be880229a1dc0586` |
| GoDown | Down (the value ladder) | breeze-dark/actions/22/go-down.svg | — | `a596707adfa8df69f1a96f538037747aee20a14bc3d925ae381bff63073c2851` |
| GoUp | Up (the value ladder) | breeze-dark/actions/22/go-up.svg | — | `2cdc6b8772dcd9ff02cd2951c506671063fccaecc0cc41848f995e977a3f9297` |
| Lock | Toggle Read-Only, locked | breeze-dark/actions/22/lock.svg | — | `593c91b5f2effa7374b659df78ace17f8fdc13b6fea5827bf387ed16ed73c25c` |
| Unlock | Toggle Read-Only, unlocked | breeze-dark/actions/22/unlock.svg | — | `e01bcb5c0f8093acc99d0a46d8f532ff0299efa945350ee0334bddbdaee921e8` |
| BboxPrev | Previous Marker (Shift+Tab) | breeze-dark/actions/22/bboxprev.svg | — | `b24f272b2ce66c6abc7c3fc79c211481d16dd2c8e65f464f3aeca9afb311207d` |
| BboxNext | Next Marker (Tab) | breeze-dark/actions/22/bboxnext.svg | — | `0f84cb0b1d659ece97c67e0a2b23cbd7d706f6cef5b9de04ba104a7a1811aad4` |
| TabDetach | Switch Tab | breeze-dark/actions/22/tab-detach.svg | — | `89ec6a1f2820bf3287e540e25fa052dfa8dbe149429462fab9990e285f20b30c` |
| SettingsConfigure | Settings | breeze-dark/actions/22/settings-configure.svg | — | `9121870839c7a14189f6c9125959829e6d32c6f302233b31fff995989bd80355` |
| Folder | a folder row | breeze-dark/places/22/folder.svg | — | `2bda082b65bc5265eadce09310c8e180f99d535803cf2380e5c40addc7a61344` |
| AudioXWav | a wav row | breeze-dark/mimetypes/22/audio-x-wav.svg | AppIcon | `97d1c798bbaa99abbd943dc09cd7f4122963296f8be86784fe540655683bf9ab` |
| MediaRepeatSingle | Toggle Repeat One | breeze-dark/actions/22/media-repeat-single.svg | — | `67ba230de73507c3c57f6f1e2875b5c7a5634db8c503321b1ce6343f74af6f65` |
| GoParentFolder | Up a Folder | breeze-dark/actions/22/go-parent-folder.svg | — | `f80a2899e640197dd56d0b944a4005425ad73379ac889dd772f9a224a406b540` |
| DialogInformation | a NORMAL card | breeze-dark/status/22/dialog-information.svg | — | `baaeb230d5ee57b19e8e3ea188582d77537b5ae1e6957d8a0351c0a7588f7648` |
| DialogError | a CRITICAL card | breeze-dark/status/22/dialog-error.svg | — | `8b103104de6b2fc3c773e3f6109d61adc3c549d0d89a46f3bbe2155de69f704f` |
| WindowClose | Close (the render player) | breeze-dark/actions/22/window-close.svg | — | `f3ddbc5dcf93bd63715dc5d23081e7f94285d7cdfec4c45dc5bca5abb2e8f8bf` |
| EditCopy | Copy Resolved Value | breeze-dark/actions/22/edit-copy.svg | — | `34f11f3f9452b7d2389599d52540abfb70c2aa074e1119c2777488fc4e7412c1` |
| HelpWhatsthis | Toggle Tooltips | breeze-dark/actions/22/help-whatsthis.svg | — | `a1fbf15f03cda98d9c8ab1ceffe641d71662cd6b303db0bf878651321969762f` |
| GoJumpDeclaration | Jump to Defining Marker | breeze-dark/actions/22/go-jump-declaration.svg | — | `a0641979532197c9e9a9a5585d468b0980c79206751587431ab903af69fb57fe` |
| EditDelete | Delete Folder | breeze-dark/actions/22/edit-delete.svg | — | `20bb20c5e29be7f21aaea9fcd25afadaa2c568bc3e256597f1cf213f4cbaf3d1` |
| AppIcon | the caption's icon | breeze-dark/mimetypes/22/audio-x-wav.svg | AudioXWav | `97d1c798bbaa99abbd943dc09cd7f4122963296f8be86784fe540655683bf9ab` |
