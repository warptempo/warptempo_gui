# The icons

The drawings the program paints, one SVG per `icons::Icon` enumerator (`src/gui/icons.h`), each named by its
enumerator, in this one flat folder. The program reads them at launch, through resvg (`src/gui/svg_icon.h`), from the
APK's `icons/` assets, the one bundle (`apk/build_apk.sh` copies this folder's drawings there, each link as the file it
names). A missing file, or one that is not well-formed SVG, fails the launch, naming the file. Files of other names
here (this README, the licence text, the launcher's `AppIcon.svg`) are not read.

## Breeze, recolored

EVERY DRAWING HERE IS KDE'S BREEZE (architect 2026-11-01, his rulings on M4, M5 and M7: "Breeze is committed", one
visual size; "we should use breeze glyphs only. ok to modify eg to make them
monochrome"). The drawings are breeze-icons 6.31.0's (KDE Frameworks, https://invent.kde.org/frameworks/breeze-icons),
as Arch Linux installs them under `/usr/share/icons/breeze-dark/`, the 22-px folders. The roster is AS RULED
(architect 2026-11-02, R12 / R14 / R15). Every key carries a glyph centered on its face and its printed legend under it
(architect 2026-11-03, the retrofit: the legends back, the tooltips naming the act); which key wears which drawing is the act vocabularies' owners' (`redesign_button_icon` in `src/gui/app_state.h`,
`render_player_button_icon` and `modal_word_icon` in `src/gui/paint_handler.cpp`, `score_viewer_button_icon` in
`src/gui/score_viewer.h`). OFF THE KEYS (`src/gui/icons.h`'s head): the logo lane's note is MusicNote16th (the BPM
key's drawing too) and its battery legend's glyph Battery000Charging while on power, Battery000 otherwise; the list
rows wear Folder on a folder or project row and ViewMediaTrack, the double notes, on a wav row (SOURCE's drawing, a
deliberate share: "keep just one"); a normal card wears DialogInformation and a critical one DialogError, the two
cut-out transcriptions (below).

THE LAUNCHER ICON is `AppIcon.svg`, a relative symlink to `MusicNote16th.svg` — the one symlink here — which
`apk/build_apk.sh` renders at every build in the logo's text tone B5B5B5 over the launcher's flat ground
(`apk/res/mipmap-anydpi-v26/ic_launcher.xml`); the app itself does not read it.

THE IMPORTER, `tools/icons/import_breeze.py` (standalone, stdlib only, no link path from any product target), holds the
one table, enumerator to source file, and writes each `<Enumerator>.svg` here; a re-pick is a row of its table and a
rerun (`python3 -I tools/icons/import_breeze.py`), which also prints the provenance table below. EVERY FILE IS A
DISTINCT DRAWING, checked by realpath and path data rather than by name (many Breeze names are symlinks:
`dialog-close` is `window-close`, so one enumerator, WindowClose), save the one deliberate share (architect
2026-11-02, R15): GoPreviousViewPage and GoNextViewPage, the score viewer's page keys, are `go-previous`'s and
`go-next`'s drawings, the chevrons LEFT and RIGHT wear, two files each written from its own source name.

THE RECOLOR RULE. Every literal color in a `fill`, `stroke`, `stop-color`, `color`, `flood-color` or `lighting-color`
attribute or `style` declaration — the Breeze color scheme's text (#fcfcfc in the dark theme's files), its accent
(#3daee9), its negative and positive texts (#da4453, #27ae60) and the other reds — is `currentColor`; `none` and every
opacity stay. The Breeze color-scheme `<style>` block (and with it an emptied `<defs>`) and every `class` attribute are
dropped; the path data and each drawing's own viewBox are kept verbatim. The importer fails on a file that would keep a
literal color, a class or a style block, or a shape with no fill on it or an ancestor (resvg's default black). So
`currentColor` resolves against the color THE PROGRAM SUPPLIES: each file is parsed per color with resvg's user
stylesheet `svg { color: #RRGGBB; }` (`svg_icon::parse`; the road at `src/gui/icons.h`'s head), the color one of the
text tones (`src/gui/materials.h`): B5B5B5 at rest, 757575 on a disabled key, 111111 on a lit key or a list's lit row.
What the colors told apart is lost, as the ruling accepts: Render's red disc, the two deletes' red and the battery's red
and green are the glyph's one color (the two cards, whose marks only a second color told apart, are cut out instead,
below).

ONE ROAD, THE INK BOX. A glyph is scaled by its INK BOX, not its viewBox: the drawing's antialiased ink (alpha > 0) is
fitted so that its larger side is the seat's own length — 16 / 33 of the key's height at 300 % on every key (16 on the
33 key), 21 for the logo lane's note, 19 for the battery glyph, 27 on a list row, .525 of a card's first line inside the
border — scaled with the
window as every chrome length (`icons::glyph_ink_size` / `icons::draw_glyph`; each seat's placement at its painter).

### Provenance

The source column is the file under `/usr/share/icons/breeze-dark/` (a source that is a symlink in the theme names the
file it resolves to); the sha256 is of the source's bytes in 6.31.0. The files here are their recolored
transcriptions, with no path data changed.

| Enumerator | Source (breeze-dark 6.31.0) | sha256 of the source |
|---|---|---|
| `DocumentOpen` | `actions/22/document-open.svg` | `d19d88c6c88ea53d1f65cb022867116013fb9533b9c0c5534e3ab6424be33b26` |
| `DocumentSave` | `actions/22/document-save.svg` | `4ddc1bf339cf3fd7e873203ed2f02720af78a9d0c58f2053143f8a818853033e` |
| `DocumentSaveAs` | `actions/22/document-save-as.svg` | `65b9d3341d744d242fc4a47f0e04c5231311b30ec76de37352fa687e9ad95d64` |
| `VcsPull` | `actions/22/vcs-pull.svg` | `1b0134c2e1463bc7616ac8e887b2416c585b7a036b835418e46db98644020158` |
| `NetworkConnect` | `actions/22/network-connect.svg` | `5fc958b3a60991f9be3762b5defa18c5bb0e376360e439a38985ae46291946a5` |
| `VcsCommit` | `actions/22/vcs-commit.svg` | `8297de250e521368ffb12e772dd934f4ef4da51133d8bf8315ebb9d82528c41f` |
| `EditUndo` | `actions/22/edit-undo.svg` | `486bb2f9a684a44717e6f10541ef718b05714a06b35b78310b0866a321ab8138` |
| `EditRedo` | `actions/22/edit-redo.svg` | `efad314c8c890eee0b1af6bad52c0261939c986a91b49c250a2b2597a874e102` |
| `MediaRecord` | `actions/22/media-record.svg` | `a53acc5fa7381ccb9fcc02d218d203ab8a5ab6fddd542569ffca87c885bdbeab` |
| `DialogCancel` | `actions/22/dialog-cancel.svg` | `e292845ffca2e88efbc2494bc9c9d6ddc479b907e2a792c6be880229a1dc0586` |
| `EditCopy` | `actions/22/edit-copy.svg` | `34f11f3f9452b7d2389599d52540abfb70c2aa074e1119c2777488fc4e7412c1` |
| `ZoomIn` | `actions/22/zoom-in.svg` | `c1cb53527ad5cbabdeae9a25d289aa5bf5559737935634bfc81a6ced9c0be2a6` |
| `ZoomOut` | `actions/22/zoom-out.svg` | `cfb5a75b1defeda835329d31401f4a0ff126bf831dc7810eaadccf374808a296` |
| `ZoomFitBest` | `actions/22/zoom-fit-best.svg` | `7c43931b78725877af03061e72a75cf21f397adeda43984d7fd85cbd3693b706` |
| `ZoomPrevious` | `actions/22/zoom-previous.svg` | `5eb25db579fd8a4460136aa3338d27cae3f49e83498965eb13ca60c11eb2a32e` |
| `ZoomInY` | `actions/22/zoom-in-y.svg (-> y-zoom-in.svg)` | `aa074bac0f1e26277b0c16888522d55dfa1c744c9ec8261fd1eb5787305f44a3` |
| `GoJump` | `actions/22/go-jump.svg` | `6c254b1cd227c165fe0417ec73c23e0a772b5b624ae5505af9c8520fdddb2cdc` |
| `TimelineUseZoneOn` | `actions/22/timeline-use-zone-on.svg` | `07d476e0b176d65d608cf22db4e16c23f19284bcb1b576c1d56dbefd2faa95c5` |
| `EditCut` | `actions/22/edit-cut.svg` | `79ae76358c0a2fd9160cc2dd7bbb35dada6ea016f01765ca77f0b21190ea4007` |
| `MusicNote16th` | `actions/22/music-note-16th.svg` | `1411cf454d39ef5781564fde937acc5b512b8f31f5372b079996680f39bc8c17` |
| `Mathmode` | `actions/22/mathmode.svg (-> insert-math-expression.svg)` | `9aba84e814f66b3e8dc5b70733572d49022cfff882d100936fb12e602906c91d` |
| `Merge` | `actions/22/merge.svg` | `277a1f792bdf141940bc09f8c39d8b8f0ed7e9bbcf93a8f00f73eac8e2eef58a` |
| `AudioHeadphones` | `devices/22/audio-headphones.svg` | `b58d460baa9fc5ea4bdbec0d08e6ba7995de4c87a3282bd98d1af9b82c0e369b` |
| `Lock` | `actions/22/lock.svg (-> document-encrypted.svg)` | `593c91b5f2effa7374b659df78ace17f8fdc13b6fea5827bf387ed16ed73c25c` |
| `SettingsConfigure` | `actions/22/settings-configure.svg (-> configure.svg)` | `9121870839c7a14189f6c9125959829e6d32c6f302233b31fff995989bd80355` |
| `HelpWhatsthis` | `actions/22/help-whatsthis.svg` | `a1fbf15f03cda98d9c8ab1ceffe641d71662cd6b303db0bf878651321969762f` |
| `ViewMediaLyrics` | `actions/22/view-media-lyrics.svg` | `1798db8ff7d67aa780fba0ed906df1497496ad94f05fb1fe6cca0a414884a97d` |
| `Raindrop` | `actions/22/raindrop.svg` | `d707a1b993c33457af562d52e634f00d8d09ca290adb8df9bc32c86fc7a87f89` |
| `ShallowHistory` | `actions/22/shallow-history.svg (-> appointment-new.svg)` | `8fa5b7d7eaba2c0e8a0518c6a4ccb85ad95ee98f653bbfa5f6ccfcb0ff0cc492` |
| `DeepHistory` | `actions/22/deep-history.svg` | `57233ea276c64d77c740b0bc166efffe96813a28e93f655eaa970f3f1f879242` |
| `BlackSum` | `actions/22/black_sum.svg` | `217bcb4e246e2893edb1fbfbdea0bf60ee1d4d981dea3a54edd74682b8c0e214` |
| `DocumentRevert` | `actions/22/document-revert.svg` | `1a0532e6416056e6d74f4d636d3774fa3e549b68fb22d117ff692b7fde6afbd2` |
| `KeyframePrevious` | `actions/22/keyframe-previous.svg` | `6170abdd8b79802cdc55973a178424b319b4620cf5b807927fb79aaec9125859` |
| `KeyframeNext` | `actions/22/keyframe-next.svg` | `2245dd83a76022af60da4068ec6fd36d4b32b422294acdd13e6c758f466ad838` |
| `DialogOkApply` | `actions/22/dialog-ok-apply.svg` | `6a9a4cbc6565ff52eb2c5251e7bf2b8b3172cddc1fb1e82e3d0b089e57d6464b` |
| `ViewMediaTrack` | `actions/22/view-media-track.svg (-> filename-filetype-amarok.svg)` | `e1af5a8c15945569532b69b6bf8155b0c2663b0a506ec2e3d4edbcea900a2279` |
| `ViewMediaVisualization` | `actions/22/view-media-visualization.svg (-> waveform.svg)` | `28f2cdb02b205aa5289a30d1ca66bea121b7b971aa74c3dbf511f01178a18a2f` |
| `Speedometer` | `actions/22/speedometer.svg (-> filename-bpm-amarok.svg)` | `6343a8464f24f03c38ca515bbe28faac7b429b2838784b287fd41360ae1e1dd9` |
| `ChronometerStart` | `actions/22/chronometer-start.svg (-> player-time.svg)` | `6cda34c0291234402bfd694f81f8841aa05bde86ce13ac68ea8048fa23f9cb81` |
| `MediaPlaybackStart` | `actions/22/media-playback-start.svg` | `e1925fda1fd2fc42e1ec6599238c2793e81f9bf978955a02f2d01222d49aecde` |
| `MediaPlaybackStop` | `actions/22/media-playback-stop.svg` | `31c039c86dc46ee21971be63b48e0ba3bce32ce81d2661843267381d13987fd5` |
| `MediaPlaybackPause` | `actions/22/media-playback-pause.svg` | `a6a4153334a78f9b12e7cf3daca4ddd6dce1f032c218bc1ad7527858d4f440f0` |
| `MediaSkipBackward` | `actions/22/media-skip-backward.svg` | `a3bdb0b0b540184842cf9d8796f601ffabf5a2a67773d4ddc9a23cf9f3e7247b` |
| `MediaSkipForward` | `actions/22/media-skip-forward.svg` | `a49246a16d87d33ccbc9b8c730992b5624b3393d56a1fe2ddfb51576878e81fe` |
| `MediaSeekBackward` | `actions/22/media-seek-backward.svg` | `7b3368c67a7e9152916b5cc2009174e360ed4b7254d52a536afc926c19aa3309` |
| `MediaSeekForward` | `actions/22/media-seek-forward.svg` | `c0681588d203759a91d23f69475f4d2f90ce8a039c651289cc54280864042993` |
| `MediaRepeatSingle` | `actions/22/media-repeat-single.svg` | `67ba230de73507c3c57f6f1e2875b5c7a5634db8c503321b1ce6343f74af6f65` |
| `ListAdd` | `actions/22/list-add.svg` | `08536709e7fb93104de3c5129936525837fff6c8a17c88090df09c83541d3636` |
| `ListRemove` | `actions/22/list-remove.svg (-> paint-none.svg)` | `76c364a82eede6569fe79710dc3efdf8be16cf4d23f37e2a31163f8e6b1cd61f` |
| `EditDelete` | `actions/22/edit-delete.svg` | `20bb20c5e29be7f21aaea9fcd25afadaa2c568bc3e256597f1cf213f4cbaf3d1` |
| `ViewHidden` | `actions/22/view-hidden.svg` | `4a957d696371babc812eef174144723af856482411896dd633238a1c6232e4fa` |
| `InsertLink` | `actions/22/insert-link.svg` | `69af6b43d00fd2709f72ce28da318ac5bef2dca486b22abf6e5a46459139bef1` |
| `Link` | `actions/22/link.svg` | `b17c4795cd7a6688e9263c860e618df6bc2bad418417fe6939559d0010809538` |
| `GoJumpDeclaration` | `actions/22/go-jump-declaration.svg` | `a0641979532197c9e9a9a5585d468b0980c79206751587431ab903af69fb57fe` |
| `DocumentEdit` | `actions/22/document-edit.svg` | `96854f50a3eb27a3b2dfc5479de7dbb9ffe79ebcee744253ebeb099776970c87` |
| `EditSelect` | `actions/22/edit-select.svg` | `23b3466eecfa6c6be02f1f2f3fb65dae61f010319196f526c9a621678cb1afb4` |
| `TransformMoveVertical` | `actions/22/transform-move-vertical.svg` | `8e7bbd8bd9bf44630e338f5c2b1a0aad9b4a59a274bfebac324a4b9b42da2751` |
| `GoDown` | `actions/22/go-down.svg` | `a596707adfa8df69f1a96f538037747aee20a14bc3d925ae381bff63073c2851` |
| `GoUp` | `actions/22/go-up.svg` | `2cdc6b8772dcd9ff02cd2951c506671063fccaecc0cc41848f995e977a3f9297` |
| `BboxPrev` | `actions/22/bboxprev.svg` | `b24f272b2ce66c6abc7c3fc79c211481d16dd2c8e65f464f3aeca9afb311207d` |
| `BboxNext` | `actions/22/bboxnext.svg` | `0f84cb0b1d659ece97c67e0a2b23cbd7d706f6cef5b9de04ba104a7a1811aad4` |
| `TabDetach` | `actions/22/tab-detach.svg` | `89ec6a1f2820bf3287e540e25fa052dfa8dbe149429462fab9990e285f20b30c` |
| `ZoomOriginal` | `actions/22/zoom-original.svg` | `0af7f88cc96e0d6de138008202a7410b4f096cfd9a1f72afb8a111a363a93305` |
| `GoPrevious` | `actions/22/go-previous.svg` | `7c798a16bc8e2b338f11c6274c1db316b7a02f15f4558d15b12d2959d75abff1` |
| `GoNext` | `actions/22/go-next.svg` | `9405643f10913c6efe7e09517efca10f993353304ddb05bfc2eb7f8694d905d9` |
| `GoParentFolder` | `actions/22/go-parent-folder.svg` | `f80a2899e640197dd56d0b944a4005425ad73379ac889dd772f9a224a406b540` |
| `GoPreviousViewPage` | `actions/22/go-previous-view-page.svg (-> go-previous.svg)` | `7c798a16bc8e2b338f11c6274c1db316b7a02f15f4558d15b12d2959d75abff1` |
| `GoNextViewPage` | `actions/22/go-next-view-page.svg (-> go-next.svg)` | `9405643f10913c6efe7e09517efca10f993353304ddb05bfc2eb7f8694d905d9` |
| `EditReset` | `actions/22/edit-reset.svg` | `6ece0a7720a0f2998d28f0fcbeea59c9d197a796b506d7fc0f711f935e566d78` |
| `ZoneIn` | `actions/22/zone-in.svg` | `f01ba4487f5446696ce9f464cc1b15ee65c114bd4b814bf8f67d3e12911a325d` |
| `ZoneOut` | `actions/22/zone-out.svg` | `d8c9c2015cc0a1ff90e10d94d6a5ab4672cb04e3fb8005cbee490891e2843288` |
| `WindowClose` | `actions/22/window-close.svg` | `f3ddbc5dcf93bd63715dc5d23081e7f94285d7cdfec4c45dc5bca5abb2e8f8bf` |
| `TaskComplete` | `status/22/task-complete.svg` | `f5ac2ac923bb59f764fa1b3ba0365906b7694b2aa6d1b9497d4cfdc4406ffa93` |
| `EditClear` | `actions/22/edit-clear.svg` | `f663b81cc2e9343f9c8b1e9cc973078e3b855a480fcec2f72894d0347a93c27f` |
| `ViewRefresh` | `actions/22/view-refresh.svg` | `e7fa6cf32858c91bf392f5b483ae2611b05817b32b826254166b56bea247e914` |
| `Folder` | `places/22/folder.svg` | `2bda082b65bc5265eadce09310c8e180f99d535803cf2380e5c40addc7a61344` |
| `Battery000` | `status/22/battery-000.svg` | `5d6e18e0d0de0b25a31840ab145836600ee49f9ea303cf9cfffab7480f4bbecb` |
| `Battery000Charging` | `status/22/battery-000-charging.svg` | `cf32f3a179b10791c7489706c1ede044b4bd4ece16eb7b86e2be87024e3e1e16` |

THE TWO CARDS ARE HAND TRANSCRIPTIONS, not the importer's (architect 2026-11-02, R14 (3)): Breeze's
`status/22/dialog-error.svg` and `status/22/dialog-information.svg` fill a rounded square in one color and draw the
mark over it in another, so the recolor rule would fill the square whole. `DialogError.svg` and
`DialogInformation.svg` are their 10-10 CUT-OUT transcriptions (git `7cfb9989^:assets/icons/breeze/`): the square's
outline and the mark's path data verbatim in ONE path, `fill="currentColor"` with `fill-rule="evenodd"`, so the mark
is a hole through which the card's face shows, in Breeze's own 22-unit viewBox. The importer never writes them (its
`HAND` list); it checks them by the recolor rule's own check at every run.

| Enumerator | Source (breeze-dark 6.31.0), transcribed by hand | sha256 of the source |
|---|---|---|
| `DialogInformation` | `status/22/dialog-information.svg` (cut out) | `baaeb230d5ee57b19e8e3ea188582d77537b5ae1e6957d8a0351c0a7588f7648` |
| `DialogError` | `status/22/dialog-error.svg` (cut out) | `8b103104de6b2fc3c773e3f6109d61adc3c549d0d89a46f3bbe2155de69f704f` |

The symlink: `AppIcon.svg` -> `MusicNote16th.svg` (the launcher's drawing, above).

## Licence

Breeze's icons are licensed under the GNU Lesser General Public License, version 3 or (at your option) any later
version — LGPL-3.0-or-later; copyright the breeze-icons authors (KDE). The files here are modified copies (the recolor
rule above). The licence text is `LICENSE-LGPL-3.txt`, which supplements the GNU GPL v3 in the repository's
`LICENSE`. The drawings are data,
read at run time and never linked or compiled into the program's code; the repository distributes source only
(`THIRD_PARTY.md`).
