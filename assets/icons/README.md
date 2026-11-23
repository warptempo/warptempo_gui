# The icons

The drawings the program paints, ONE SET, PAPIRUS (architect 2026-11-17, session 140, his set; 2026-11-21, send GG:
one look, Arc-Dark and Papirus, no second set and no device key): one SVG per `icons::Icon` enumerator
(`src/gui/icons.h`), each named by its enumerator, in this one flat folder. The program reads the set at launch,
through resvg (`src/gui/svg_icon.h`), from the APK's `icons/` assets (`apk/build_apk.sh` copies this folder's `.svg`
files there). A missing enumerator's file, or one that is not well-formed SVG, fails the launch, naming the file.
Files of other names here (this README, the licence text) are never looked up.

## Papirus, recolored

EVERY ENUMERATOR'S DRAWING HERE IS PAPIRUS'S BUT THIRTEEN, OURS, FROM ELEVEN SOURCES OF OUR OWN (THE OWN DRAWINGS,
below): papirus-icon-theme (https://github.com/PapirusDevelopmentTeam/papirus-icon-theme, release 20260801; the
drawings first imported at commit 33d971eb are byte-identical in it), its 16-px symbolic set (`Papirus/16x16/symbolic/`,
whose names are mostly links into the 16-px `actions/`, `apps/` and `panel/` trees) and drawings of the 16-px
`actions/`, `devices/` and `panel/` trees where his pick lives there. THE ROSTER is the parked picks of session 123
(his sheets of session 140) WITH ROWS RE-PICKED BY HIM: 2026-11-17 ("the first alternative for each one"), TAB
(`TabDetach`) `focus-windows`, two windows, and the picker's Up (`GoParentFolder`), re-picked once more 2026-11-23
(send OO, below); 2026-11-22 (send II, off the alternates sheet; eight of them re-picked again the same day, send MM, and some again
2026-11-23, sends NN and OO, each marked so), TWENTY-ONE ROWS — Previous / Next Marker (`BboxPrev` / `BboxNext`) OUR
OWN arrow to a line (send MM, THE OWN DRAWINGS below), Next's mirrored to point right; Score (`ViewMediaLyrics`)
`folder-music`, two beamed notes; Source (`ViewMediaTrack`) `media-optical`, a disc; Value Drag
(`TransformMoveVertical`) OUR OWN double arrow, one head up and one down on one stem (re-picked once more 2026-11-23,
send NN, THE OWN DRAWINGS below); Follow (`GoJump`) `go-first-rtl`, an arrow right to a dot (send MM), and Restrict
(`TimelineUseZoneOn`) OUR OWN dot and wall with nothing between, the playhead stopped at the window's edge, no arrow
(re-picked once more 2026-11-23, send OO); Jump to Defining (`GoJumpDeclaration`) `go-jump-declaration`; Grid
Iterations (`Mathmode`) `insert-math-expression`; Target (`ViewMediaVisualization`) `kdenlive-show-audiothumb`;
Magnification (`ZoomInY`) `audio-volume-high`; Warp (`Speedometer`) `slow_off`, a gauge; FIT's recall
(`ZoomPrevious`) `view-restore`; Older / Newer (`KeyframePrevious` / `KeyframeNext`) `keyframe-previous` /
`keyframe-next`; Session History (`DeepHistory`) `network-unavailable`, a faded network port and a cross (send MM);
the score's Start / End (`ZoneIn` / `ZoneOut`) OUR OWN, `view-readermode`'s open book with a plus or a minus in its
own moat (re-picked once more 2026-11-23, send OO); Save (`DocumentSave`) `media-floppy`; Cumulative (`BlackSum`)
OUR OWN sigma (re-picked once more 2026-11-23, send OO); Flatten (`Merge`) `tool-brush`, a brush (send MM); and
2026-11-23 (send OO, his picks off the mocks; with Restrict, Start / End and Cumulative above, ten rows, every one
OUR OWN, THE OWN DRAWINGS below), SIX MORE ROWS — the picker's Up (`GoParentFolder`) `folder-symbolic`'s folder with
an up arrow cut out of it; the score's Previous / Next Page (`GoPreviousViewPage` / `GoNextViewPage`)
`go-previous`'s head without its stem, Next's mirrored to point right; and SAVE's three history-view faces, Commit
(`VcsCommit`) `go-up`'s arrow standing on a line, Pull (`VcsPull`) `go-down`'s arrow landing on it, and Check
(`NetworkConnect`) the two arrows side by side, one up and one down. Papirus names few of the enumerators' acts, so
many rows are the nearest drawing by another name (the Note column).

WHICH SEAT WEARS WHICH DRAWING is the act vocabularies' owners' (`redesign_button_icon` in `src/gui/app_state.h`,
`render_player_button_icon` in `src/gui/paint_handler.cpp`, `score_viewer_button_icon` in `src/gui/score_viewer.h`),
and the on-screen keyboard's glyph keys (`onscreen_keyboard::cap_icon` in `src/gui/onscreen_keyboard.h`: Tab
FormatIndentMore and Enter KeyEnter, its two drawings of its own; Bksp EditClear, Copy / Paste EditCopy / EditPaste,
LEFT / RIGHT GoPrevious / GoNext, UP / DOWN GoUp / GoDown and the pass key InsertLink shared with the program keys).
OFF THE KEYS (`src/gui/icons.h`'s head): the title bar's note is MusicNote16th and its battery Battery000Charging
while on power, Battery000 otherwise; the list rows wear Folder on a folder or project row, AudioHeadphones, the
headphones, on a wav row, and on every other list's rows the opener key's drawing; a normal card wears
DialogInformation and a standing one DialogError. THE NOTE IS SHARED THREE WAYS (architect 2026-11-22, sends II and
KK): the title bar, the BPM key and the BPM form's rows, and the launcher icon is rendered from it (below); THE
HEADPHONES TWO (architect 2026-11-22, send MM): the PLAYER key and the render player's wav rows; SOURCE's disc,
ViewMediaTrack, is the key's alone.

THE IMPORTER, `tools/icons/import_papirus.py` (standalone, stdlib only, no link path from any product target), holds the
one table, enumerator to source file under `Papirus/16x16/` (or, for an own drawing, under `tools/icons/own/`), and
writes each `<Enumerator>.svg` here by THE RECOLOR RULE (below); the checkout is its one argument and is read as data, nothing in it run
(`python3 -I tools/icons/import_papirus.py <papirus-icon-theme checkout>`), and it prints the provenance table below.
It writes only its rows' files and removes nothing, so a rerun leaves the licence text and this README as they are.
THE TWO CARDS ARE IMPORTED AS PAPIRUS DRAWS THEM: Papirus's `dialog-information` and `dialog-error` already cut their
mark (an i, an !) out of a filled disc, the mark's subpaths wound against the disc's, so one color shows it.
EVERY FILE IS A DISTINCT DRAWING, by its written shapes and transforms rather than by name (the importer flags a
share; there is none — the score viewer's page keys wear our own head without a stem, not GoPrevious's / GoNext's
arrows). ONE SOURCE MAY MAKE TWO DRAWINGS BY THE TRANSFORM, twice: `BboxPrev` and `BboxNext` are both the marker
pair's own drawing, and `GoPreviousViewPage` and `GoNextViewPage` both the score's own page head, Previous's as
drawn and Next's mirrored (the importer notes the shared source).

THE OWN DRAWINGS, the eleven sources that are no Papirus file, in `tools/icons/own/`, all at Papirus's weight in a 16
x 16 box (the numbers below in its units), ten made from Papirus drawings whose path data they keep verbatim wherever
a part is Papirus's, and one, the sigma, drawn outright. THE OWN DRAWINGS' STRAIGHT ARROWS WEAR PAPIRUS'S HEAD AND
STEM (`go-previous` / `go-up` / `go-down` / `go-first`) rather than a solid triangle head, the shape he keeps for the
keys' straight arrows (architect 2026-11-23, send OO); Papirus's own drawings keep the heads Papirus drew.
- THE MARKER PAIR'S (architect 2026-11-22, send MM, his pick off the mock, "gap 0"), `BboxPrev.svg`: `actions/go-first.svg`
  with its dot turned into a line 2 wide (the arrow stem's thickness), rows 2 to 14 with square ends, the arrow's tip
  touching it, the whole ink re-centered in the 16 canvas (shifted 1.5 left, ink x 1.5 to 14.5); the arrow's path data
  is go-first's verbatim. Previous Marker wears it as drawn, Next Marker mirrored (THE TRANSFORM, below).
- VALUE DRAG'S (architect 2026-11-23, send NN, his pick off the mock, "B3"), `TransformMoveVertical.svg`: go-first's
  arrow turned upright, one head up and one head down superimposed on one stem, the stroke kept at 2, the arms
  shortened — each head 10 wide and 6.5 deep (go-first's head corner 6 in from the tip -> 5), tip to tip 16 (ink y 0
  to 16, x 3 to 13), the heads 3 apart — drawn as the two arrows' union, one outline.
- CUMULATIVE'S (architect 2026-11-23, send OO, "S1"), `BlackSum.svg`: a sigma, OURS OUTRIGHT, no Papirus source (the
  16-px set has no sigma under any name tried); its shape follows Material Design Icons' `sigma`, two bars and a
  45-degree fold, no serifs, REDRAWN at Papirus's weight with none of its path data: 10 wide and 12 tall (ink x 3 to
  13, y 2 to 14), the bars 2 thick, the fold 3 across (go-previous's arm).
- THE SCORE'S PAGE KEYS' (send OO, "P1"), `GoPreviousViewPage.svg`: `actions/go-previous.svg`'s head with the stem
  removed — its vertices 2,8 8,2 9.5,3.5 and 9.5,12.5 8,14 kept, the stem's two inner corners replaced by the arms'
  own meeting point 5,8 — the ink re-centered (shifted 2.25 right, ink x 4.25 to 11.75, y 2 to 14). Previous Page
  wears it as drawn, Next Page mirrored (THE TRANSFORM, below).
- THE SCORE'S START AND END (send OO, "K5"), `ZoneIn.svg` and `ZoneOut.svg`: `actions/view-readermode.svg`'s open book,
  its path verbatim, with a mark in the bottom-right corner standing in its own moat 1 wide cut out of the book — Start
  a PLUS 6 x 6 at a stroke of 2 (ink x 10 to 16, y 10 to 16; the size of Papirus's own plus in `actions/folder-new.svg`),
  End a MINUS, the plus's bar (ink x 10 to 16, y 12 to 14). The moat is a `<clipPath>` on the book (the box less the mark
  grown by 1, even-odd), so the drawing keeps one color and no literal one.
- RESTRICT'S (send OO, "C2"), `TimelineUseZoneOn.svg`: the playhead stopped at the window's edge, a dot and a wall
  with nothing between and no arrow — go-first's dot (r 2) at cx 6, cy 8, the marker pair's wall, 2 wide, rows 2 to
  14, at x 10 to 12, the clear gap 2, the ink x 4 to 12, centered. Follow keeps Papirus's `go-first-rtl`, an arrow
  right to a dot.
- THE PICKER'S UP (send OO, "U1"), `GoParentFolder.svg`: `symbolic/places/folder-symbolic.svg`'s folder, its path
  verbatim, with a head-and-stem up arrow cut out of its body by the even-odd rule, as Papirus cuts its own folder
  emblems (`folder-new`'s plus): the arrow 8 x 8 (x 4 to 12, y 4.5 to 12.5), the stroke 2, the head in Value Drag's
  grammar (45-degree arms, square ends).
- COMMIT AND PULL (send OO, "V1"), `VcsCommit.svg` and `VcsPull.svg`: `actions/go-up.svg`'s / `actions/go-down.svg`'s
  arrow, its path verbatim, touching a line 2 thick and 12 wide (x 2 to 14) under it with nothing between (the marker
  pair's "gap 0") — Commit's stem stands on the line, Pull's tip lands on it — the whole shifted up 1, the ink rows 1
  to 15.
- CHECK (send OO, "N2"), `NetworkConnect.svg`: Commit's arrow and Pull's side by side, one up and one down, no line,
  each 8 wide and 14 tall at a stroke of 2 (ink x 0 to 16, y 1 to 15).

Every shape of every own source already paints `currentColor` (the moats' clip paths included, which paint nothing),
and the importer reads each through the same recolor rule and check as a Papirus file; each one's XML comment, which
names its derivation, rides into its written files.

THE TRANSFORM (architect 2026-11-22, send II). A row of the importer's table may name one, `mirror` (left-right, the
one kind); the importer then wraps every child of the drawing's root `<svg>` in one `<g transform="...">` about the
center of the drawing's box (its viewBox, else its width and height) — `matrix(-1 0 0 1 16 0)` for a 16-px drawing —
and resvg applies it at the parse. The path data inside stays verbatim. Two rows wear it: `BboxNext`, the marker
pair's own drawing mirrored (send MM), and `GoNextViewPage`, the score's own page head mirrored (send OO).

THE RECOLOR RULE. Every literal color in a `fill`, `stroke`, `stop-color`, `color`, `flood-color` or `lighting-color`
attribute or `style` declaration is `currentColor`; `none`, `currentColor`, `inherit` and `url(...)` stay, and every
opacity stays. The color-scheme `<style>` block (Papirus's `.ColorScheme-*` classes over `currentColor`, and with it an
emptied `<defs>`) and every `class` attribute are dropped; the path data and each drawing's own viewBox are kept
verbatim (a transformed row's group the one addition). The importer fails on a file that would keep a literal color, a class or a style block, or a shape with no
fill on it or an ancestor (resvg's default black). So `currentColor` resolves against the color THE PROGRAM SUPPLIES:
each file is parsed per color with resvg's user stylesheet `svg { color: #RRGGBB; }` (`svg_icon::parse`; the road at
`src/gui/icons.h`'s head), the color one of the text tones or the print on a lit face (`src/gui/materials.h`,
`key_ink`). A drawing's own opacities stay: the batteries' bodies stand at 35 % (the discharging one beside a solid
exclamation mark, the charging one with a bolt cut through it), and so does Session History's network port beside its
solid cross (`DeepHistory`).

ONE ROAD, THE INK BOX (`src/gui/icons.h`'s THE INK BOX, the lengths' one owner). A glyph is scaled by its ink, never
by its viewBox: the drawing's antialiased ink (alpha > 0) is fitted at each seat's own length, scaled with the window as
every chrome length (`icons::glyph_ink_size`, `icons::ink_in_box`, `icons::draw_glyph`; each seat's placement at its
painter). The keys', the list rows' and the cards' drawing stands as its whole 16-px canvas in a 16 W box (Papirus's
drawings at their native size), the title bar's note in a 12 W box, and the battery's ink box at the title bar's cap
band (architect 2026-11-17, session 140: "otherwise it looks like it's falling out of the line"), its top on the cap
band's top (`src/gui/render.h`'s title bar block, THE BATTERY).

### Provenance

The source column is the file under the checkout's `Papirus/16x16/` (a source that is a symlink names the file it
resolves to), or, for an own drawing, its path in this repository; the sha256 is of the source's bytes, in
papirus-icon-theme 20260801 (the run's checkout its installed package's icon root; every row kept from 33d971eb has
the same bytes there) or in this repository. The files here are their recolored transcriptions, with no path data
changed, two of them wrapped in their transform.

| Enumerator | Source (Papirus/16x16/, papirus-icon-theme 20260801; or ours, its repository path) | sha256 of the source | Note |
|---|---|---|---|
| `DocumentOpen` | `symbolic/actions/document-open-symbolic.svg (-> actions/document-open.svg)` | `f1917ee23fee2eb4cb5af289f6210cb945d97eb739c5a6c71f5ed94b38680867` |  |
| `DocumentSave` | `devices/media-floppy.svg` | `d3fad61e83d95be524bb5273b30079ac40da971fd042028fa9830a3ce3927c41` | media-floppy, a floppy disk, his pick over document-save (re-picked 2026-11-22) |
| `VcsPull` | `tools/icons/own/VcsPull.svg` | `9d24b394659ae02c6f97efbef85aab9f0b157b91dd14496bbe001afe577f16f1` | no vcs-pull; ours, go-down's arrow, its tip landing on a line 2 thick (re-picked 2026-11-23, send OO) |
| `NetworkConnect` | `tools/icons/own/NetworkConnect.svg` | `734454cabbe50fc6f9d63b057da8e833230860a906de1c2945bfa8c147fde04a` | no network-connect; ours, two arrows side by side, one up and one down, in go-up's and go-down's grammar (re-picked 2026-11-23, send OO) |
| `VcsCommit` | `tools/icons/own/VcsCommit.svg` | `1a679df4864ef4c9455e7244a1227c125118346dc5de8271f7c06e96a39d456b` | no vcs-commit; ours, go-up's arrow, its stem standing on a line 2 thick (re-picked 2026-11-23, send OO) |
| `EditUndo` | `symbolic/actions/edit-undo-symbolic.svg (-> actions/edit-undo.svg)` | `a9a6d908951617f2e557df2ebdcd4f9c0115577e1c8d2a29f2c78d710dfe34cb` |  |
| `EditRedo` | `symbolic/actions/edit-redo-symbolic.svg (-> actions/edit-redo.svg)` | `e086ac6ba7eb5475557feb75ba735f57119f5243f9d41249184e6dc6fca5a6f2` |  |
| `MediaRecord` | `symbolic/actions/media-record-symbolic.svg (-> actions/media-record.svg)` | `9fd1b5e4b13ac6ff139400e63f80a83025d579b88fb4806ead5fda5b4b002721` |  |
| `DialogCancel` | `symbolic/actions/cross-small-circle-outline-symbolic.svg` | `bb3dd939c9970c88234c368f556481d29a51876b7a054f6e67cc71fce0861973` | no dialog-cancel; a circled cross |
| `EditCopy` | `symbolic/actions/edit-copy-symbolic.svg (-> actions/edit-copy.svg)` | `b89f6f8def3560b3a77583e896059a51832316f8059627735d508e7c6ece7251` |  |
| `EditPaste` | `symbolic/actions/edit-paste-symbolic.svg (-> actions/edit-paste.svg)` | `c123a1419570169b316a4e4980834f6f7c6dc9da2bed01c83e023274941ee024` |  |
| `ZoomIn` | `symbolic/actions/zoom-in-symbolic.svg (-> actions/zoom-in.svg)` | `0909faf491ca9ddfd327d220affa2cd9c7cfa345ff77df4418a2145f79ec27fa` |  |
| `ZoomOut` | `symbolic/actions/zoom-out-symbolic.svg (-> actions/zoom-out.svg)` | `bcf189637391d0aff15fb67f7e7b8a41888184e32ea60d49a260c5c8ef5cef10` |  |
| `ZoomFitBest` | `symbolic/actions/zoom-fit-best-symbolic.svg (-> actions/zoom-fit-best.svg)` | `8c01fbdcfa24a3e94b19a909b7db021b11634d5bb7eb9a1ef6cbe1e700189b94` |  |
| `ZoomPrevious` | `actions/view-restore.svg` | `e7dccd470d9e616c2e577fe4a0b6b8f0117495ba9e378883fe887c7aa8cd0867` | no zoom-previous; view-restore, four corners turned in (re-picked 2026-11-22) |
| `ZoomInY` | `actions/audio-volume-high.svg` | `f3b25582adcb2b020cb83bb6576d1013d3ee64785af312bee6d7cf88c054b2c9` | no zoom-in-y; audio-volume-high, a speaker and its waves (re-picked 2026-11-22) |
| `GoJump` | `symbolic/actions/go-first-rtl-symbolic.svg (-> actions/go-last.svg)` | `824fa2be70b7d33e089e7ee7817e8ddb3bdbddc7375cef7692a86030c7514b33` | go-first-rtl, an arrow right to a dot, his pick over go-jump (re-picked 2026-11-22, send MM) |
| `TimelineUseZoneOn` | `tools/icons/own/TimelineUseZoneOn.svg` | `ce0fd3977a4ae6c07cbb6a141e5e4a0389ec434cbf7b6e4ba6a60b2ab80fb0b2` | no timeline-use-zone-on; ours, go-first's dot and the marker pair's wall with nothing between, no arrow (re-picked 2026-11-23, send OO) |
| `EditCut` | `symbolic/actions/edit-cut-symbolic.svg (-> actions/edit-cut.svg)` | `a77a41d3c41147cfee808ef99dc97bc005e50c5bd7297cf60f27814a49681e38` |  |
| `Bookmarks` | `symbolic/actions/bookmarks-symbolic.svg (-> actions/bookmarks.svg)` | `49c3baac8e3c875ec7f7f3975062f89c81c0d97ca8100b3e5266cb81dac489cd` |  |
| `MusicNote16th` | `symbolic/actions/music-note-single-symbolic.svg` | `5d3b0f21a7313e430fdeb4995b071caa1cb3d9d234848df511359df983448be5` | no 16th note; the single note |
| `Mathmode` | `actions/insert-math-expression.svg` | `654333cac794f9cbc23eef5b229aec7a7ba4947a878e8e7e5c0717794af7f9e5` | no mathmode; insert-math-expression, an f of x (re-picked 2026-11-22) |
| `Merge` | `symbolic/actions/tool-brush-symbolic.svg` | `768616b0aa2ed03c18440b8ed71cad021b10c8292a320b111f4e45632e910d18` | tool-brush, a brush, his pick over merge and edit-paste-style (re-picked 2026-11-22, send MM) |
| `AudioHeadphones` | `symbolic/devices/audio-headphones-symbolic.svg` | `b9bd23e546a1f62bf44a9260004a8c7e3c7f1c7d8e6744f9ca88c4205a81d478` |  |
| `Lock` | `symbolic/actions/lock-symbolic.svg (-> actions/system-lock-screen.svg)` | `f43c953a60fa80e663b8fb4047b6967f0a93db0e349fe8178af057954c76e34e` |  |
| `SettingsConfigure` | `symbolic/actions/settings-symbolic.svg (-> actions/configure.svg)` | `df30d791a1311a1f531afa4fe73960778fb02db47ed2a192853df521088bf36f` | no settings-configure; settings |
| `HelpWhatsthis` | `symbolic/actions/help-about-symbolic.svg (-> actions/help-about.svg)` | `e07bc239c74f4c1372be356610350e51d18d42e268540c1a1d3c7c672a93d98e` | no help-whatsthis; help-about |
| `ViewMediaLyrics` | `symbolic/places/folder-music-symbolic.svg` | `01ffd6e4ff6b24a1980a2965a62719df2d1c8faa1d314406baf73f15aefa387d` | no view-media-lyrics; folder-music, two beamed notes (re-picked 2026-11-22) |
| `ShallowHistory` | `symbolic/actions/recent-symbolic.svg (-> actions/document-open-recent.svg)` | `9b16854c8865286e4aacac9086b0e1a595c9e2029d3e8e2c346fcb54be7af3b6` | no shallow-history; recent (a clock) |
| `DeepHistory` | `panel/network-unavailable.svg (-> panel/network-wired-offline.svg)` | `ffd38f78062e37210ae4d1c38b985b7fdee67f71bfca5fbd99c2b319a37dee65` | no deep-history; network-unavailable, a faded network port and a cross (re-picked 2026-11-22, send MM) |
| `BlackSum` | `tools/icons/own/BlackSum.svg` | `8ac5edbed9f201f2f6533375eb78e2475e36532079ec01efb50a3913335042da` | no black_sum; ours, a sigma, no Papirus source (re-picked 2026-11-23, send OO) |
| `DocumentRevert` | `symbolic/actions/document-revert-symbolic.svg (-> actions/document-revert.svg)` | `3ebae2dd73ccf42a5f0d12d7e5548e025f3dd2dced023a73f98fc60f67cd0118` |  |
| `KeyframePrevious` | `actions/keyframe-previous.svg` | `dcb56a7748d890872d1e1566d192691b5c65300a9e13d629dbdd3830c33789c2` | a stopwatch and a triangle back (re-picked 2026-11-22) |
| `KeyframeNext` | `actions/keyframe-next.svg` | `649db167a62c4c238f4febefb2f2cc376ada0bae88e9bb158187067f0c1f501d` | a stopwatch and a triangle forward (re-picked 2026-11-22) |
| `DialogOkApply` | `symbolic/actions/feather-check-symbolic.svg (-> symbolic/actions/dino-tick-symbolic.svg)` | `7353cf8a23d1e471aa100fbedbabe074b3ae7e12c630c0feba9942281e15c4a5` | no dialog-ok-apply; a check |
| `ViewMediaTrack` | `devices/media-optical.svg` | `6f4c817244f11723618f299ef1b4c8c3fbd975373f713bc81aa88700f4f651ad` | no view-media-track; media-optical, a disc (re-picked 2026-11-22) |
| `ViewMediaVisualization` | `actions/kdenlive-show-audiothumb.svg` | `fddf283c83384e1e93ffdfca989766e5757cc95eef5396f5c25d81a3b3788b52` | no view-media-visualization; kdenlive-show-audiothumb, an arrow through chevrons (re-picked 2026-11-22) |
| `Speedometer` | `actions/slow_off.svg` | `88518fc56829794b0def593c57f932e0a434f9eb0be5ca0a64f2788c6579c296` | slow_off, a gauge, his pick over speedometer (re-picked 2026-11-22) |
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
| `GoJumpDeclaration` | `actions/go-jump-declaration.svg` | `4609b9c197a31baa689783c251237a3df8fff24a11d3930e899596850280fef7` | a flag and a turning arrow (re-picked 2026-11-22) |
| `DocumentEdit` | `symbolic/actions/document-edit-symbolic.svg (-> actions/edit.svg)` | `673e07a25559e0014fd3409139f92436771c2014e09a3514edc4a42d3f90ba7b` |  |
| `EditSelect` | `symbolic/actions/edit-select-symbolic.svg (-> actions/edit-select.svg)` | `02def4b01e5b9111bf52eaba120513f64894f320434845a75ecfc0fa4f330f67` |  |
| `TransformMoveVertical` | `tools/icons/own/TransformMoveVertical.svg` | `1c5b587f66dc0b07785f1550963f96826cafa2979171c2ceaa419cad7c120a7a` | no transform-move-vertical; ours, go-first's arrow upright, one head up and one down on one stem (re-picked 2026-11-23, send NN) |
| `GoDown` | `symbolic/actions/go-down-symbolic.svg (-> actions/go-down.svg)` | `7b77fae327f734c80faea8980ef6ad68e36fac56df2ac09b01c8448f35fecec1` |  |
| `GoUp` | `symbolic/actions/go-up-symbolic.svg (-> actions/go-up.svg)` | `86b31dec3b65c47c5f0c8965da8463c35365cf0f1046c6a391a9a8083ff2d938` |  |
| `BboxPrev` | `tools/icons/own/BboxPrev.svg` | `07195df200437a8a9a6380560b434b493a1c694983fd30cab23ec691df419b42` | no bboxprev; ours, go-first's arrow to a line 2 wide, as drawn, pointing left (re-picked 2026-11-22, send MM) |
| `BboxNext` | `tools/icons/own/BboxPrev.svg` | `07195df200437a8a9a6380560b434b493a1c694983fd30cab23ec691df419b42` | no bboxnext; ours, go-first's arrow to a line 2 wide, mirrored left-right to point right (re-picked 2026-11-22, send MM); one source with BboxPrev, another drawing by the transform |
| `TabDetach` | `symbolic/actions/focus-windows-symbolic.svg (-> actions/focus-windows.svg)` | `174c461a8b625026aa2537fcbc6bebefd17fc31e58a09e388f55b9dee91f3065` | no tab-detach; focus-windows, two windows (re-picked 2026-11-17) |
| `ZoomOriginal` | `symbolic/actions/zoom-original-symbolic.svg (-> actions/zoom-original.svg)` | `4ccc559527aecd140fd0e8b5de9881ce6bfff978b4cd28be366f0b5d509a489f` |  |
| `GoPrevious` | `symbolic/actions/go-previous-symbolic.svg (-> actions/go-previous.svg)` | `5bc42db4e2cbf4bdc12a91f3430cf01447ba558645cffd499b318861df771d92` |  |
| `GoNext` | `symbolic/actions/go-next-symbolic.svg (-> actions/go-next.svg)` | `9a99f8bd24357cf56535e3c50d97874056c5928f12bca038025316309597ba07` |  |
| `GoParentFolder` | `tools/icons/own/GoParentFolder.svg` | `4ce683ce9e12073ec478a87fa7c0c36d65d983a3ced004fc3ac4011f0528f02f` | ours, folder-symbolic's folder with an up arrow cut out of its body, his pick over go-parent-folder (re-picked 2026-11-23, send OO) |
| `GoPreviousViewPage` | `tools/icons/own/GoPreviousViewPage.svg` | `2ace6a993255466adb18f55cea123a41c7e84321dcd25e5e4090b503718e9aa5` | no go-previous-view-page; ours, go-previous's head without its stem, as drawn, pointing left (re-picked 2026-11-23, send OO) |
| `GoNextViewPage` | `tools/icons/own/GoPreviousViewPage.svg` | `2ace6a993255466adb18f55cea123a41c7e84321dcd25e5e4090b503718e9aa5` | no go-next-view-page; ours, go-previous's head without its stem, mirrored left-right to point right (re-picked 2026-11-23, send OO); one source with GoPreviousViewPage, another drawing by the transform |
| `EditReset` | `symbolic/actions/object-rotate-left-symbolic.svg (-> actions/object-rotate-left.svg)` | `d99b6f9e536672447d619a3d789b7b459a0bb2f0accd9e4c5c0a4570775a005b` | no edit-reset; the counter-clockwise arrow |
| `ZoneIn` | `tools/icons/own/ZoneIn.svg` | `de0b3ba35d9c2105856de8755e179baa888934c42b2ac315d5342a57e66b4db0` | ours, view-readermode's open book with a plus in its own moat, his pick over zone-in (re-picked 2026-11-23, send OO) |
| `ZoneOut` | `tools/icons/own/ZoneOut.svg` | `42c9aeae9564c29081fae2fbce03c9fe8d5eee86b5ab9987fe5bbba67710790f` | ours, view-readermode's open book with a minus in its own moat, his pick over zone-out (re-picked 2026-11-23, send OO) |
| `EditClear` | `symbolic/actions/edit-clear-symbolic.svg (-> actions/edit-clear.svg)` | `c51332adb04993f9984a1e2c49aa6fd1ba8657f3e298c340a90b97a9b5256e69` |  |
| `Folder` | `symbolic/places/folder-symbolic.svg` | `7820cddb76eb60b759d9c82dd35036898363588e7b3de8f0f8643d9e7de4dff3` |  |
| `DialogInformation` | `symbolic/status/dialog-information-symbolic.svg (-> panel/state-information.svg)` | `2b8d444c6d5b27562d3b9042bd6c0e0e404eb941fa18dd58d7a18a885f35c9a1` |  |
| `DialogError` | `symbolic/status/dialog-error-symbolic.svg (-> panel/state-error.svg)` | `32754a7cb6bcaeaf760fde0cb2c3d3ab4c1d9d820aab7358b04035fd33e34536` |  |
| `Battery000` | `symbolic/status/battery-000-symbolic.svg (-> panel/battery-000.svg)` | `856e8d9e62ba1ead7470621e5d70eda39d69650fd34625442e31ed73bc129062` |  |
| `Battery000Charging` | `symbolic/status/battery-000-charging-symbolic.svg (-> panel/battery-000-charging.svg)` | `06310e609bbe0e293598f2dc9987d2a22565004e0202e9a16328fdf722f10f39` |  |
| `FormatIndentMore` | `symbolic/actions/format-indent-more-symbolic.svg (-> actions/format-indent-more.svg)` | `950a2222431581fe70478e7bd640c5c15cfd163d9f32867a5b221a0f6938e49c` |  |
| `KeyEnter` | `symbolic/actions/keyboard-enter-symbolic.svg` | `3f3d6745742a454428ece370bc49f4de81f311fcf28e4d07793c1f8bee6d595a` | no key-enter; keyboard-enter |

## The launcher icon, Papirus's note

THE LAUNCHER ICON is `MusicNote16th.svg`, the title bar's note, the BPM key's and the BPM form's rows' drawing (architect
2026-11-21, send HH: one drawing, one file), Papirus's and credited in the table above. `apk/build_apk.sh` renders it
at every build, its art 46 dp of the 108 dp layer (architect 2026-11-22, send II), in the font color over its flat
ground — Arc-Dark's, the one look, D3DAE3 over 2F343F, read from the compiled row of the
look's tones, `kArcDarkTones` in `src/gui/gui_theme.h` (its `.text` and `.ground` lines)
(`apk/res/mipmap-anydpi-v26/ic_launcher.xml`).

## Licence

Papirus's icons are licensed under the GNU General Public License, version 3 — GPL-3.0; copyright the papirus-icon-theme
authors (its `AUTHORS`). The enumerators' files here are modified copies (the recolor rule above); thirteen are of
our own drawings (`tools/icons/own/`, THE OWN DRAWINGS above): ten of those sources are themselves modified copies of
Papirus drawings under the same licence, and Cumulative's sigma (`tools/icons/own/BlackSum.svg`) is ours outright,
under the repository's own licence. The licence text is
`LICENSE-GPL-3.txt` (the checkout's `LICENSE`, verbatim), the same licence as the repository's own `LICENSE`.

The drawings are data, read at run time (the launcher's at build time) and never linked or compiled into the
program's code; the repository distributes source only (`THIRD_PARTY.md`).
