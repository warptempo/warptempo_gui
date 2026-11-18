# The icons

The drawings the program paints, in TWO SETS, one SVG per `icons::Icon` enumerator (`src/gui/icons.h`) in each, each
named by its enumerator, each set in one flat folder: KDE's Breeze in this folder, papirus-icon-theme's Papirus in
`papirus/`. THE `icons` DEVICE KEY CHOOSES which set every painter draws (architect 2026-11-14, session 123: "the
icons get their own device key", not tied to the theme — the theme files stay palette-only and either set goes with
any theme; 2026-11-17, session 140: "No breeze glyphs at all", HIS SET PAPIRUS, the key's first-run value;
`src/gui/device_config.h`), live from the Settings page's value list. The program reads both sets at launch, through
resvg (`src/gui/svg_icon.h`), from the APK's `icons/` and `icons/papirus/` assets, the one bundle
(`apk/build_apk.sh` copies the two folders' drawings there, each link as the file it names). A missing file in either
set, or one that is not well-formed SVG, fails the launch, naming the file. Files of other names here (this README,
the licence texts, the launcher's `AppIcon.svg`) are not read. Both sets wear one recolor rule (THE RECOLOR RULE,
below) and one ink-box road; the seats and which key wears which enumerator are one table whatever the set (below),
the battery's length alone the set's (Papirus's below).

## Breeze, recolored

EVERY DRAWING IN THIS FOLDER IS KDE'S BREEZE (architect 2026-11-01, his rulings on M4, M5 and M7: "Breeze is
committed", one visual size; "we should use breeze glyphs only. ok to modify eg to make them monochrome" — the one set
until 2026-11-17, the `breeze` set since). The drawings are breeze-icons 6.31.0's (KDE Frameworks, https://invent.kde.org/frameworks/breeze-icons),
as Arch Linux installs them under `/usr/share/icons/breeze-dark/`, the 22-px folders. The roster is AS RULED
(architect 2026-11-02, R12 / R14 / R15). Every key carries a glyph centered on its face and its printed legend under it
(architect 2026-11-03, the retrofit: the legends back, the tooltips naming the act); which key wears which drawing is the act vocabularies' owners' (`redesign_button_icon` in `src/gui/app_state.h`,
`render_player_button_icon` and `modal_word_icon` in `src/gui/paint_handler.cpp`, `score_viewer_button_icon` in
`src/gui/score_viewer.h`), and since 2026-11-08 (architect, sessions 95 / 96) the on-screen keyboard's glyph keys
(`onscreen_keyboard::cap_icon` in `src/gui/onscreen_keyboard.h`: Tab FormatIndentMore and Enter KeyEnter, its two
drawings of its own; Bksp EditClear, Copy / Paste EditCopy / EditPaste, LEFT / RIGHT GoPrevious / GoNext and the pass
key InsertLink shared with the program keys), at the program keys' one ink size. OFF THE KEYS (`src/gui/icons.h`'s
head): the logo lane's note is MusicNote16th (the BPM
key's drawing too) and its battery legend's glyph Battery000Charging while on power, Battery000 otherwise; the list
rows wear Folder on a folder or project row and ViewMediaTrack, the double notes, on a wav row (SOURCE's drawing, a
deliberate share: "keep just one"); a normal card wears DialogInformation and a critical one DialogError, the two
cut-out transcriptions (below).

THE LAUNCHER ICON is `AppIcon.svg`, a relative symlink to `MusicNote16th.svg` — the one symlink here, Breeze's note
whatever `icons` set the tablet runs — which
`apk/build_apk.sh` renders at every build in the `default` theme's text tone over its flat ground — SB Gunmetal_mod,
B3B4BD over 1F2128, read from `themes/system/sb_gunmetal_mod.theme`
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
text tones and the print on a lit face (`src/gui/materials.h`, `key_ink`): B5B5B5 at rest, 5D5D5D on a disabled key, 111111
on a lit key's face (the accent, or a radio key's purple) and 090909 on a lit disabled one's dimmed face (the accent face, 2026-11-11, session 106), 070707 on a list's highlighted row,
2A2A2A on a notification card (Live's orange, since 2026-11-04 night).
What the colors told apart is lost, as the ruling accepts: Render's red disc, the two deletes' red and the battery's red
and green are the glyph's one color (the two cards, whose marks only a second color told apart, are cut out instead,
below).

ONE ROAD, THE INK BOX. A glyph is scaled by its INK BOX, not its viewBox: the drawing's antialiased ink (alpha > 0) is
fitted so that its larger side is the seat's own length — 18 / 34 of the key's height at 300 % on every key (18 on the
34 key, since 2026-11-04 night, his pick d1), 18 for the logo lane's note (since 2026-11-04 night, his pick g1), 22 for Breeze's battery glyph (since 2026-11-04 night; Papirus's the legend digits' 15 rows, below), 26 on a list row, .525 of a card's first line inside the
border (the unit of 2, 2026-11-04 night) — scaled with the
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
| `EditPaste` | `actions/22/edit-paste.svg` | `aa76c92f924b6e8efb75b6f10ac255e474b2f8df087564c400743a4114c15ad6` |
| `ZoomIn` | `actions/22/zoom-in.svg` | `c1cb53527ad5cbabdeae9a25d289aa5bf5559737935634bfc81a6ced9c0be2a6` |
| `ZoomOut` | `actions/22/zoom-out.svg` | `cfb5a75b1defeda835329d31401f4a0ff126bf831dc7810eaadccf374808a296` |
| `ZoomFitBest` | `actions/22/zoom-fit-best.svg` | `7c43931b78725877af03061e72a75cf21f397adeda43984d7fd85cbd3693b706` |
| `ZoomPrevious` | `actions/22/zoom-previous.svg` | `5eb25db579fd8a4460136aa3338d27cae3f49e83498965eb13ca60c11eb2a32e` |
| `ZoomInY` | `actions/22/zoom-in-y.svg (-> y-zoom-in.svg)` | `aa074bac0f1e26277b0c16888522d55dfa1c744c9ec8261fd1eb5787305f44a3` |
| `GoJump` | `actions/22/go-jump.svg` | `6c254b1cd227c165fe0417ec73c23e0a772b5b624ae5505af9c8520fdddb2cdc` |
| `TimelineUseZoneOn` | `actions/22/timeline-use-zone-on.svg` | `07d476e0b176d65d608cf22db4e16c23f19284bcb1b576c1d56dbefd2faa95c5` |
| `EditCut` | `actions/22/edit-cut.svg` | `79ae76358c0a2fd9160cc2dd7bbb35dada6ea016f01765ca77f0b21190ea4007` |
| `Bookmarks` | `actions/22/bookmarks.svg` | `62595b53a0e1783b40795e4963a449191049e831d69c9012ec21c139df86db91` |
| `MusicNote16th` | `actions/22/music-note-16th.svg` | `1411cf454d39ef5781564fde937acc5b512b8f31f5372b079996680f39bc8c17` |
| `Mathmode` | `actions/22/mathmode.svg (-> insert-math-expression.svg)` | `9aba84e814f66b3e8dc5b70733572d49022cfff882d100936fb12e602906c91d` |
| `Merge` | `actions/22/merge.svg` | `277a1f792bdf141940bc09f8c39d8b8f0ed7e9bbcf93a8f00f73eac8e2eef58a` |
| `AudioHeadphones` | `devices/22/audio-headphones.svg` | `b58d460baa9fc5ea4bdbec0d08e6ba7995de4c87a3282bd98d1af9b82c0e369b` |
| `Lock` | `actions/22/lock.svg (-> document-encrypted.svg)` | `593c91b5f2effa7374b659df78ace17f8fdc13b6fea5827bf387ed16ed73c25c` |
| `SettingsConfigure` | `actions/22/settings-configure.svg (-> configure.svg)` | `9121870839c7a14189f6c9125959829e6d32c6f302233b31fff995989bd80355` |
| `HelpWhatsthis` | `actions/22/help-whatsthis.svg` | `a1fbf15f03cda98d9c8ab1ceffe641d71662cd6b303db0bf878651321969762f` |
| `ViewMediaLyrics` | `actions/22/view-media-lyrics.svg` | `1798db8ff7d67aa780fba0ed906df1497496ad94f05fb1fe6cca0a414884a97d` |
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
| `EditClear` | `actions/22/edit-clear.svg` | `f663b81cc2e9343f9c8b1e9cc973078e3b855a480fcec2f72894d0347a93c27f` |
| `ViewRefresh` | `actions/22/view-refresh.svg` | `e7fa6cf32858c91bf392f5b483ae2611b05817b32b826254166b56bea247e914` |
| `Folder` | `places/22/folder.svg` | `2bda082b65bc5265eadce09310c8e180f99d535803cf2380e5c40addc7a61344` |
| `Battery000` | `status/22/battery-000.svg` | `5d6e18e0d0de0b25a31840ab145836600ee49f9ea303cf9cfffab7480f4bbecb` |
| `Battery000Charging` | `status/22/battery-000-charging.svg` | `cf32f3a179b10791c7489706c1ede044b4bd4ece16eb7b86e2be87024e3e1e16` |
| `FormatIndentMore` | `actions/22/format-indent-more.svg` | `5d13577a091c917fec9ddd2a6347f71fc8d60ab279d8c5d7e59cbd8a98aa1025` |
| `KeyEnter` | `actions/22/key-enter.svg` | `b2f24627fc85743b6985633da5ba2f85122d1e66a007c5f3ec8693c86a2ceccb` |

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

## Papirus, recolored

EVERY DRAWING IN `papirus/` IS PAPIRUS'S (architect 2026-11-17, session 140, his set): papirus-icon-theme
(https://github.com/PapirusDevelopmentTeam/papirus-icon-theme, commit 33d971eb), its 16-px symbolic set
(`Papirus/16x16/symbolic/`, whose names are mostly links into the 16-px `actions/`, `apps/` and `panel/` trees) plus
three drawings of the 16-px `actions/` tree that the symbolic set does not carry. THE ROSTER is the parked picks of
session 123 (his sheets of session 140) WITH FIVE ROWS RE-PICKED BY HIM 2026-11-17 ("the first alternative for each
one"): TAB (`TabDetach`) `focus-windows`, two windows; DRAG (`TransformMoveVertical`) `object-flip-vertical`; the
picker's Up (`GoParentFolder`) `actions/go-parent-folder.svg`; the score's zone in and zone out (`ZoneIn`,
`ZoneOut`) `actions/zone-in.svg` and `actions/zone-out.svg` — the last three from the `actions/` tree. Papirus names
few of the enumerators' acts, so many rows are the nearest drawing by another name (the Note column).

THE IMPORTER, `tools/icons/import_papirus.py` (standalone, stdlib only, no link path from any product target), holds the
one table, enumerator to source file under `Papirus/16x16/`, and writes each `papirus/<Enumerator>.svg` by THE RECOLOR
RULE above (Papirus's color scheme is Breeze's own convention, a `<style>` of `.ColorScheme-*` classes over
`currentColor`); the checkout is its one argument and is read as data, nothing in it run
(`python3 -I tools/icons/import_papirus.py <papirus-icon-theme checkout>`), and it prints the provenance table below.
EVERY ROW IS IMPORTED, THE TWO CARDS INCLUDED: Papirus's `dialog-information` and `dialog-error` already cut their mark
(an i, an !) out of a filled disc, the mark's subpaths wound against the disc's, so one color shows it — no hand
transcription (Breeze's two are, above). EVERY FILE IS A DISTINCT DRAWING, by path data (the importer flags a share;
there is none — the score viewer's page keys wear chevrons of their own, not GoPrevious's / GoNext's arrows). The
drawings keep their own opacities: the batteries' bodies stand at 35 % (the discharging one beside a solid
exclamation mark, the charging one with a bolt cut through it).

THE BATTERY IS SMALLER UNDER PAPIRUS (architect 2026-11-17, session 140: "otherwise it looks like it's falling out of
the line"): its height is the legend digits' ink band, its top on the digits' top row — 15 rows on the tablet against
Breeze's 22-wide, 14-row outline (`src/gui/render.h`'s LOGO LANE block, the glyph cell). Every other seat's length is
the set's alike.

### Provenance

The source column is the file under the checkout's `Papirus/16x16/` (a source that is a symlink names the file it
resolves to); the sha256 is of the source's bytes at 33d971eb. The files in `papirus/` are their recolored
transcriptions, with no path data changed.

| Enumerator | Source (Papirus/16x16/, papirus-icon-theme 33d971eb) | sha256 of the source | Note |
|---|---|---|---|
| `DocumentOpen` | `symbolic/actions/document-open-symbolic.svg (-> actions/document-open.svg)` | `f1917ee23fee2eb4cb5af289f6210cb945d97eb739c5a6c71f5ed94b38680867` |  |
| `DocumentSave` | `symbolic/actions/document-save-symbolic.svg (-> actions/document-save.svg)` | `311a835d4802af8f177cf2dfb60a5d61d7c460dc7776cc41cbe7c7b5360918e3` |  |
| `DocumentSaveAs` | `symbolic/actions/document-save-as-symbolic.svg (-> actions/document-save-as.svg)` | `a34c9bff6657763e2da384006ba70db0654b7cd441018c390b55757d90db8581` |  |
| `VcsPull` | `symbolic/actions/arrow-pointing-at-line-down-symbolic.svg` | `cac4c212c16b35932194517e326ae706a458b89dbdbab477325ff8e7db27f238` | no vcs-pull; an arrow down onto a line |
| `NetworkConnect` | `symbolic/status/network-transmit-receive-symbolic.svg (-> panel/network-transmit-receive.svg)` | `5b97c62c89249267a0c190fa81503ef84b46a7148f7c422f6cd0d539d6e10d81` | no network-connect; the two-way network arrows |
| `VcsCommit` | `symbolic/actions/arrow-pointing-away-from-line-up-symbolic.svg` | `18b60022d01e028e9243c5c34479b286b958ad8164e762d097b24715bc90b03b` | no vcs-commit; an arrow up from a line |
| `EditUndo` | `symbolic/actions/edit-undo-symbolic.svg (-> actions/edit-undo.svg)` | `a9a6d908951617f2e557df2ebdcd4f9c0115577e1c8d2a29f2c78d710dfe34cb` |  |
| `EditRedo` | `symbolic/actions/edit-redo-symbolic.svg (-> actions/edit-redo.svg)` | `e086ac6ba7eb5475557feb75ba735f57119f5243f9d41249184e6dc6fca5a6f2` |  |
| `MediaRecord` | `symbolic/actions/media-record-symbolic.svg (-> actions/media-record.svg)` | `9fd1b5e4b13ac6ff139400e63f80a83025d579b88fb4806ead5fda5b4b002721` |  |
| `DialogCancel` | `symbolic/actions/cross-small-circle-outline-symbolic.svg` | `bb3dd939c9970c88234c368f556481d29a51876b7a054f6e67cc71fce0861973` | no dialog-cancel; a circled cross |
| `EditCopy` | `symbolic/actions/edit-copy-symbolic.svg (-> actions/edit-copy.svg)` | `b89f6f8def3560b3a77583e896059a51832316f8059627735d508e7c6ece7251` |  |
| `EditPaste` | `symbolic/actions/edit-paste-symbolic.svg (-> actions/edit-paste.svg)` | `c123a1419570169b316a4e4980834f6f7c6dc9da2bed01c83e023274941ee024` |  |
| `ZoomIn` | `symbolic/actions/zoom-in-symbolic.svg (-> actions/zoom-in.svg)` | `0909faf491ca9ddfd327d220affa2cd9c7cfa345ff77df4418a2145f79ec27fa` |  |
| `ZoomOut` | `symbolic/actions/zoom-out-symbolic.svg (-> actions/zoom-out.svg)` | `bcf189637391d0aff15fb67f7e7b8a41888184e32ea60d49a260c5c8ef5cef10` |  |
| `ZoomFitBest` | `symbolic/actions/zoom-fit-best-symbolic.svg (-> actions/zoom-fit-best.svg)` | `8c01fbdcfa24a3e94b19a909b7db021b11634d5bb7eb9a1ef6cbe1e700189b94` |  |
| `ZoomPrevious` | `symbolic/actions/xapp-go-history-previous-symbolic.svg` | `7613b67db4d7b9ad89d5b292edb58c8a09067edf3614f3c36913f99123d536af` | no zoom-previous; the history-back arrow |
| `ZoomInY` | `symbolic/actions/image-resize-symbolic.svg` | `56fb516e3a018184cace9304f5a0d532f9638e02eb659bca3cb9ea838a3b44a5` | no zoom-in-y; the resize handles |
| `GoJump` | `symbolic/actions/go-jump-symbolic.svg (-> actions/go-jump.svg)` | `d51795fabf4dfeed790da98dee6e20e6a7da4b2bc9b59f4cf72add056033bb69` |  |
| `TimelineUseZoneOn` | `symbolic/actions/selection-mode-symbolic.svg` | `ad66c8a59d0811735020588e7374c98a2a9b4382d42c6a2b6fcb9d0fb467d605` | no timeline-use-zone-on; selection-mode |
| `EditCut` | `symbolic/actions/edit-cut-symbolic.svg (-> actions/edit-cut.svg)` | `a77a41d3c41147cfee808ef99dc97bc005e50c5bd7297cf60f27814a49681e38` |  |
| `Bookmarks` | `symbolic/actions/bookmarks-symbolic.svg (-> actions/bookmarks.svg)` | `49c3baac8e3c875ec7f7f3975062f89c81c0d97ca8100b3e5266cb81dac489cd` |  |
| `MusicNote16th` | `symbolic/actions/music-note-single-symbolic.svg` | `5d3b0f21a7313e430fdeb4995b071caa1cb3d9d234848df511359df983448be5` | no 16th note; the single note |
| `Mathmode` | `symbolic/apps/accessories-calculator-symbolic.svg` | `7b760dc81f0478d0b05c3b39df2ad334a2ca3bda4752940a32911d9431ee6861` | no mathmode; the calculator |
| `Merge` | `symbolic/actions/object-merge-symbolic.svg` | `867247d19b92ca5e0bdf72882941c60a3b74098d9c696a41441e64c4e9222434` | no merge; object-merge |
| `AudioHeadphones` | `symbolic/devices/audio-headphones-symbolic.svg` | `b9bd23e546a1f62bf44a9260004a8c7e3c7f1c7d8e6744f9ca88c4205a81d478` |  |
| `Lock` | `symbolic/actions/lock-symbolic.svg (-> actions/system-lock-screen.svg)` | `f43c953a60fa80e663b8fb4047b6967f0a93db0e349fe8178af057954c76e34e` |  |
| `SettingsConfigure` | `symbolic/actions/settings-symbolic.svg (-> actions/configure.svg)` | `df30d791a1311a1f531afa4fe73960778fb02db47ed2a192853df521088bf36f` | no settings-configure; settings |
| `HelpWhatsthis` | `symbolic/actions/help-about-symbolic.svg (-> actions/help-about.svg)` | `e07bc239c74f4c1372be356610350e51d18d42e268540c1a1d3c7c672a93d98e` | no help-whatsthis; help-about |
| `ViewMediaLyrics` | `symbolic/actions/lyrics-on-symbolic.svg` | `9301c2839a022843697e6000dab9c46091057a8400e2503d93e8c04ce972a60e` | no view-media-lyrics; lyrics-on |
| `ShallowHistory` | `symbolic/actions/recent-symbolic.svg (-> actions/document-open-recent.svg)` | `9b16854c8865286e4aacac9086b0e1a595c9e2029d3e8e2c346fcb54be7af3b6` | no shallow-history; recent (a clock) |
| `DeepHistory` | `symbolic/actions/clock-alt-symbolic.svg (-> actions/clock.svg)` | `9aa490fdacb7f6eac0ed931a6deb82821f5d8ba42f87f963084b833f54763a88` | no deep-history; clock-alt |
| `BlackSum` | `symbolic/actions/feather-plus-circle-symbolic.svg` | `bf3bb6c5293e1b291d52f5730587f7010da3aa163e48a3252e121e17d8b25ae6` | no black_sum; a circled plus |
| `DocumentRevert` | `symbolic/actions/document-revert-symbolic.svg (-> actions/document-revert.svg)` | `3ebae2dd73ccf42a5f0d12d7e5548e025f3dd2dced023a73f98fc60f67cd0118` |  |
| `KeyframePrevious` | `symbolic/actions/skip-backward-large-symbolic.svg` | `0040e413493b89c3d7d1ef656bd9fecdec5796a8a31d65d730146708a27cd5b9` | no keyframe-previous; the large skip back |
| `KeyframeNext` | `symbolic/actions/skip-forward-large-symbolic.svg` | `8c4adf3d6f12c037fe885933ad7244f47bc925d63464fe901a3cad7231819b91` | no keyframe-next; the large skip forward |
| `DialogOkApply` | `symbolic/actions/feather-check-symbolic.svg (-> symbolic/actions/dino-tick-symbolic.svg)` | `7353cf8a23d1e471aa100fbedbabe074b3ae7e12c630c0feba9942281e15c4a5` | no dialog-ok-apply; a check |
| `ViewMediaTrack` | `symbolic/actions/music-playlist-symbolic.svg (-> actions/view-media-playlist.svg)` | `9f97f7ac269a6c45a79bec6a65308c71cd3854dc7e6aaacf23cf0783cb486659` | no view-media-track; the playlist notes |
| `ViewMediaVisualization` | `symbolic/apps/media-eq-symbolic.svg (-> symbolic/apps/multimedia-equalizer-symbolic.svg)` | `8e22c6f4163a195922c738dd084f9f8e918a6f2c58947dab3a71bcdd525849f3` | no view-media-visualization; the equalizer |
| `Speedometer` | `symbolic/actions/speedometer-symbolic.svg (-> actions/kt-speed-limits.svg)` | `72845361b79fb9a312dec2904ee92450ce219a1afe9a7218fd58e6439caa25b1` |  |
| `ChronometerStart` | `symbolic/status/stopwatch-symbolic.svg` | `0923c5102e664c9b3aa03acd8a86d25a63a218bb819cd8a6f0b90be8119b9566` | no chronometer-start; the stopwatch |
| `MediaPlaybackStart` | `symbolic/actions/media-playback-start-symbolic.svg (-> actions/media-playback-start.svg)` | `320e13c81811a48657cdab3469aee4893b8365daceff7eabb2bab1674bd3642a` |  |
| `MediaPlaybackStop` | `symbolic/actions/media-playback-stop-symbolic.svg (-> actions/media-playback-stop.svg)` | `fa72d5e22fc3a84b4a48787dd08b66a0330c3d62aaf2b8fd53b2209449b3a682` |  |
| `MediaPlaybackPause` | `symbolic/actions/media-playback-pause-symbolic.svg (-> actions/media-playback-pause.svg)` | `cbfbad4115b4d76f2d612950923a1ec8d87b48b18c9a6706ff8b6b92b26767f2` |  |
| `MediaSkipBackward` | `symbolic/actions/media-skip-backward-symbolic.svg (-> actions/media-skip-backward.svg)` | `f4f91e6db2ea9a677ff55c37a28bfac150dbd94255dc34d3d12c8ec2f4e742b3` |  |
| `MediaSkipForward` | `symbolic/actions/media-skip-forward-symbolic.svg (-> actions/media-skip-forward.svg)` | `00e2795412db9854cb7bf22dc6090a7ce6415f0127e9468c8c9f2b8817bad810` |  |
| `MediaSeekBackward` | `symbolic/actions/media-seek-backward-symbolic.svg (-> actions/media-seek-backward.svg)` | `4e76dcc4918484537a1a265df6f9334aba96987bd09dc038293c9a5fc4bb6feb` |  |
| `MediaSeekForward` | `symbolic/actions/media-seek-forward-symbolic.svg (-> actions/media-seek-forward.svg)` | `09c6a9b30beea328c46cbf88137083f6c150cf1f79d0b93e3866da033cad9cc0` |  |
| `MediaRepeatSingle` | `symbolic/actions/playlist-repeat-song-symbolic.svg (-> actions/media-repeat-track-amarok.svg)` | `4c93a72a6dcb3119f43ec492aaa8a9a25c6ed3f8dfad4159bf194169a8c582dc` | no media-repeat-single; playlist-repeat-song |
| `ListAdd` | `symbolic/actions/list-add-symbolic.svg (-> actions/list-add.svg)` | `0c2c62d576e0b273f03da2e454fd8bda49b66c8d720e250ae146c1209aa83053` |  |
| `ListRemove` | `symbolic/actions/list-remove-symbolic.svg (-> actions/list-remove.svg)` | `28c01e0ac1e894ca5774f9063572b240dfb33442a1892cea21252215f8c783bf` |  |
| `EditDelete` | `symbolic/actions/edit-delete-symbolic.svg (-> actions/entry-delete.svg)` | `2174e0aabbf5159b0fa96db59f1ae3b5505580c0ae67fe1b78c6a9f3e5778c46` |  |
| `ViewHidden` | `symbolic/actions/view-conceal-symbolic.svg (-> actions/image-red-eye.svg)` | `c44447be031fa13f7129b867e634963d9ea1a301dfd94ef524db787f1775a983` | no view-hidden; view-conceal |
| `InsertLink` | `symbolic/actions/insert-link-symbolic.svg (-> actions/insert-link.svg)` | `ba3c3dede042c7016d49bf035f74756b9765bbf496d914b49d3bd2a0e97607f3` |  |
| `Link` | `symbolic/actions/link-symbolic.svg (-> actions/link.svg)` | `7eb8711a67339f322193592509bf621b3aaefee61ab232e0be17f0ee1267f10a` |  |
| `GoJumpDeclaration` | `symbolic/actions/send-to-symbolic.svg (-> actions/send-to.svg)` | `a49c4b8526b66d859c0511a4e2681ecc31571cb5592eef6958da9b17dee81b59` | no go-jump-declaration; the send-to arrow |
| `DocumentEdit` | `symbolic/actions/document-edit-symbolic.svg (-> actions/edit.svg)` | `673e07a25559e0014fd3409139f92436771c2014e09a3514edc4a42d3f90ba7b` |  |
| `EditSelect` | `symbolic/actions/edit-select-symbolic.svg (-> actions/edit-select.svg)` | `02def4b01e5b9111bf52eaba120513f64894f320434845a75ecfc0fa4f330f67` |  |
| `TransformMoveVertical` | `symbolic/actions/object-flip-vertical-symbolic.svg (-> actions/object-flip-vertical.svg)` | `f978a83db9a8837d0e58e2d286d7fe2ea1231c4e2273251422a5ce5f6fda91c3` | no transform-move-vertical; object-flip-vertical (re-picked 2026-11-17) |
| `GoDown` | `symbolic/actions/go-down-symbolic.svg (-> actions/go-down.svg)` | `7b77fae327f734c80faea8980ef6ad68e36fac56df2ac09b01c8448f35fecec1` |  |
| `GoUp` | `symbolic/actions/go-up-symbolic.svg (-> actions/go-up.svg)` | `86b31dec3b65c47c5f0c8965da8463c35365cf0f1046c6a391a9a8083ff2d938` |  |
| `BboxPrev` | `symbolic/actions/pan-start-symbolic.svg (-> actions/pan-start.svg)` | `785b39faed6157315951bf46ac4d166a1758aa70ee8e4c4ec859e0bab2d1bd42` | no bboxprev; the left pan triangle |
| `BboxNext` | `symbolic/actions/pan-end-symbolic.svg (-> actions/pan-end.svg)` | `17f659e3bd99222cfc4088b980e3049bfc76397e4550d91e32759cee80647243` | no bboxnext; the right pan triangle |
| `TabDetach` | `symbolic/actions/focus-windows-symbolic.svg (-> actions/focus-windows.svg)` | `174c461a8b625026aa2537fcbc6bebefd17fc31e58a09e388f55b9dee91f3065` | no tab-detach; focus-windows, two windows (re-picked 2026-11-17) |
| `ZoomOriginal` | `symbolic/actions/zoom-original-symbolic.svg (-> actions/zoom-original.svg)` | `4ccc559527aecd140fd0e8b5de9881ce6bfff978b4cd28be366f0b5d509a489f` |  |
| `GoPrevious` | `symbolic/actions/go-previous-symbolic.svg (-> actions/go-previous.svg)` | `5bc42db4e2cbf4bdc12a91f3430cf01447ba558645cffd499b318861df771d92` |  |
| `GoNext` | `symbolic/actions/go-next-symbolic.svg (-> actions/go-next.svg)` | `9a99f8bd24357cf56535e3c50d97874056c5928f12bca038025316309597ba07` |  |
| `GoParentFolder` | `actions/go-parent-folder.svg` | `21769fc225e7827e63393f8185e827a70d96a8d08d47cd712bf16deb4f6f89f8` | the 16-px actions tree, no symbolic twin (re-picked 2026-11-17) |
| `GoPreviousViewPage` | `symbolic/actions/lucide-chevron-left-symbolic.svg` | `8d430458fc57a73c41df6abbe8537e39af0234c74edd01b21d25ecf811438395` | no go-previous-view-page; a chevron of its own |
| `GoNextViewPage` | `symbolic/actions/lucide-chevron-right-symbolic.svg` | `bc4e87c2eb4f0cfe9f59fa527174e22a2420591d442e85e9b8ffb1623e6a60ee` | no go-next-view-page; a chevron of its own |
| `EditReset` | `symbolic/actions/object-rotate-left-symbolic.svg (-> actions/object-rotate-left.svg)` | `d99b6f9e536672447d619a3d789b7b459a0bb2f0accd9e4c5c0a4570775a005b` | no edit-reset; the counter-clockwise arrow |
| `ZoneIn` | `actions/zone-in.svg` | `6a4c8d78f084b60e2c2cc5cfa985984b761a3a8a9ff72d1de38aba05e351eda1` | the 16-px actions tree, no symbolic twin (re-picked 2026-11-17) |
| `ZoneOut` | `actions/zone-out.svg` | `21177b7570a1b886833cf67d436f8ab13866a776ddb399abbfc272cb4a358b38` | the 16-px actions tree, no symbolic twin (re-picked 2026-11-17) |
| `WindowClose` | `symbolic/actions/window-close-symbolic.svg (-> actions/window-close.svg)` | `290fa132ddfa8ce774fa7000485a624a9a01e30e3e6707bba3c4a73e98189ee3` |  |
| `EditClear` | `symbolic/actions/edit-clear-symbolic.svg (-> actions/edit-clear.svg)` | `c51332adb04993f9984a1e2c49aa6fd1ba8657f3e298c340a90b97a9b5256e69` |  |
| `ViewRefresh` | `symbolic/actions/view-refresh-symbolic.svg (-> actions/view-refresh.svg)` | `3b61cbe0cba68155db98b395c16f3c520ebd601a18ba4e4f7e31507041cad329` |  |
| `Folder` | `symbolic/places/folder-symbolic.svg` | `7820cddb76eb60b759d9c82dd35036898363588e7b3de8f0f8643d9e7de4dff3` |  |
| `DialogInformation` | `symbolic/status/dialog-information-symbolic.svg (-> panel/state-information.svg)` | `2b8d444c6d5b27562d3b9042bd6c0e0e404eb941fa18dd58d7a18a885f35c9a1` |  |
| `DialogError` | `symbolic/status/dialog-error-symbolic.svg (-> panel/state-error.svg)` | `32754a7cb6bcaeaf760fde0cb2c3d3ab4c1d9d820aab7358b04035fd33e34536` |  |
| `Battery000` | `symbolic/status/battery-000-symbolic.svg (-> panel/battery-000.svg)` | `856e8d9e62ba1ead7470621e5d70eda39d69650fd34625442e31ed73bc129062` |  |
| `Battery000Charging` | `symbolic/status/battery-000-charging-symbolic.svg (-> panel/battery-000-charging.svg)` | `06310e609bbe0e293598f2dc9987d2a22565004e0202e9a16328fdf722f10f39` |  |
| `FormatIndentMore` | `symbolic/actions/format-indent-more-symbolic.svg (-> actions/format-indent-more.svg)` | `950a2222431581fe70478e7bd640c5c15cfd163d9f32867a5b221a0f6938e49c` |  |
| `KeyEnter` | `symbolic/actions/keyboard-enter-symbolic.svg` | `3f3d6745742a454428ece370bc49f4de81f311fcf28e4d07793c1f8bee6d595a` | no key-enter; keyboard-enter |

## Licence

Breeze's icons are licensed under the GNU Lesser General Public License, version 3 or (at your option) any later
version — LGPL-3.0-or-later; copyright the breeze-icons authors (KDE). The files here are modified copies (the recolor
rule above). The licence text is `LICENSE-LGPL-3.txt`, which supplements the GNU GPL v3 in the repository's
`LICENSE`. The drawings are data,
read at run time and never linked or compiled into the program's code; the repository distributes source only
(`THIRD_PARTY.md`).

Papirus's icons are licensed under the GNU General Public License, version 3 — GPL-3.0; copyright the papirus-icon-theme
authors (its `AUTHORS`). The files in `papirus/` are modified copies (the recolor rule above). The licence text is
`papirus/LICENSE-GPL-3.txt` (the checkout's `LICENSE`, verbatim), the same licence as the repository's own `LICENSE`.
They too are data, read at run time and never linked or compiled into the program's code.
