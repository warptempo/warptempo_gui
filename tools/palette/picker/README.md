# tools/palette/picker — Warptempo Picker, the colour picker over a true picture of the app

A design tool for picking the app's colours (render.h's palette block: the chrome, the waveform's canvas and ink,
next the flags) by hand on the tablet's glass, one ELEMENT at a time, choosing which, over a picture of the app
exported by the mock tool. A separate APK, package
`com.warptempo.picker`, label "Warptempo Picker", installed beside the product and never touching
`com.warptempo.gui` or its files. NO LINK PATH TO OR FROM THE PRODUCT: nothing in `src/`, `android/app/` or
`CMakeLists.txt` names it, and it compiles no product source; it borrows the Android toolchain
(`android/toolchain/00_env.sh`), the static graphics libraries (`android/prebuilt/arm64-v8a`), the faces (`fonts/`)
and the debug keystore.

THE REASON IT EXISTS IS ITS COLOUR HANDLING, WHICH IS THE PRODUCT'S BY CONSTRUCTION. The window is set up as the
product's (`GuiPlatform::adopt_window`, `src/gui/platform_android.cpp`, and the manifest's colour mode): an
`RGBA_8888` buffer tagged `ADATASPACE_DISPLAY_P3` under `colorMode="wideColorGamut"`, pinned at 90 Hz, full screen with
both system bars hidden sticky-immersive (`PickerActivity.java`, the product's `hideSystemBars` copied), landscape
either way (`sensorLandscape`). A hex is a Display-P3 byte triple written to the buffer as is; there is no colour
conversion anywhere, so a hex shown here is the bytes the product shows for it, on the same panel.

## What it shows

The SCENE, full screen: a picture of the app in which every pixel is named — the colour ROLE it shows and, at an
antialiased edge, the roles it is blended over (the export, below) — so EVERY ELEMENT'S CURRENT COLOUR IS LIVE IN
EVERY SCENE, the text and glyph edges re-blended truthfully over whatever they sit on. One ELEMENT is ACTIVE: the one
the panel edits. Each element is picked over its own scene (several share one); the roles follow the elements by
their rules — the chrome's every line follows its ground by Windows 95's proportions, the waveform outline follows
the ink over the canvas — so a mock render and the picker agree by construction.

THE ELEMENTS this round (architect 2026-10-04, in his order of importance, the chooser's order): CHROME (the ground,
one knob; the active element at a fresh start), CANVAS, INK, all over the scene `waveform`. The rounds to come, in
this order, are each a new theme and export with no new app code: the unselected flag, the selected flag, then the
open flag with its selection (where the selected fill, fixed #666666 now, becomes pickable).

- A TAP on the picture opens the PANEL on the half of the screen opposite the tap, so the place tapped stays in view.
- The panel (GTK's colour selector and GIMP's colour dialog, their common ground, HSV only, no CMYK): the hue RING with
  the saturation / value TRIANGLE inside it (its corners the pure hue, white and black, turning with the hue; drag
  either); the active element's NAME as a button and the PRESETS button beside it (below), its HEX in large type, under it BACK | "N of M" | FORWARD (the pick history, below in
  Use), the OLD | NEW swatches (the colour when the panel opened | now; a
  tap on OLD reverts); six SLIDERS, H 0–360, S 0–100, V 0–100 and R, G, B 0–255, each a long track painted with its
  live gradient (the colour along it, the other channels as they stand), a handle, its value at the right, and a
  one-unit − / + at its ends (one degree, one percent, one byte; acting at the lift). THE READOUT shows H in degrees
  and S, V in percent to ONE DECIMAL, rounded to nearest (247.3, 47.1, 100.0: where the view actually is, as GIMP's
  HSV fields read; architect 2026-10-04), R, G, B whole bytes; the six fields are one width, sized for "360.0". The
  H / S / V − / + still step whole degrees and percents, from the rounded whole number (speed over fineness). Every control produces an exact
  byte triple and the scene repaints from it live. HSV is a view over the bytes plus a retained hue (and
  saturation), as GTK keeps them: through grey the hue stays, through black the saturation too.
- THE HSV HE DIALLED IS PART OF THE PICK (architect 2026-10-04). A saved pick keeps, beside its bytes, the exact HSV
  view it was saved under, and every road back to a stored colour — the launch, BACK / FORWARD, OLD, leaving the app
  with the panel open — restores that view instead of re-deriving it from the bytes. Re-derived, the view is the
  bytes' own HSV (S 0.3529 where he dialled 0.35): the number reads 35.3 where he dialled 35.0 and the handle moves, and the next − / +
  rounds from the re-derived values, while at low saturation or value one byte is several degrees of hue or a percent
  of saturation, so an axis he never touched would move. Re-derivation from bytes stays only where the bytes are the
  input: the R / G / B tracks and their − / +, and a pick saved before views were stored. The ring, the triangle and
  the H / S / V tracks and − / + set the view directly. The bytes stay the colour's truth (what is painted, what the
  product takes); the view always gives them.
- THE CHOOSER: a tap on the element's name (at the lift, as every panel control) opens a vertical list of the
  elements in manifest order over the panel's right column, the active one marked by a white square. A tap on an
  entry picks it and closes the chooser (a press on one entry lifted on another picks nothing and leaves it open); a
  tap anywhere outside the chooser closes it and changes nothing. CHOOSING ANOTHER ELEMENT IS THE CLOSE FOR THE ONE
  BEING LEFT (the history's rules, below in Use); choosing the active element again is a no-op.
- THE PRESETS (architect 2026-10-04: he tunes whole looks on the glass and keeps them to come back to). A PRESETS
  button beside the element's name opens the PRESETS POP-UP, built like the chooser (the same chrome and tap rules: a
  tap on an entry acts and closes it, a press lifted on another entry acts on nothing, a tap outside it closes it and
  changes nothing), over the panel below the two buttons and the panel's whole width (the theme names are long).
  OPENING IT IS THE CLOSE FOR THE PANEL'S EDIT (the chooser's rule): an edited colour commits first, so a preset always
  snapshots saved colours. Its first line, fixed, is "Save as Preset N" (N the highest saved + 1, never reused while
  the file lives); under it a LIST THAT SCROLLS by a pen or finger drag (a drag past 16 px scrolls and acts on nothing;
  a tap acts at the lift; the scroll is kept while the app lives): the saved presets, oldest first, each with a small
  swatch of every element's colour in manifest order; then the plain heading "Themes" and THE PRODUCT'S THEMES (the
  export's `themes.json`, below), each by its name with a swatch of its ground. A scroll frame paints the list and
  nothing else (no re-blend).
  - SAVE appends the whole look — every element's colour and its exact view — to `presets.json` as "Preset N" (one
    logcat line, `picker: preset save Preset N: <key> #RRGGBB ...`); duplicates are allowed.
  - LOAD sets every element to the preset's colour and view: each element whose colour or view changes gets ONE
    committed pick in its own history (a `picks.txt` line, as a commit), an unchanged element none, so BACK steps to
    the history's previous newest entry — where he was when his cursor stood there, always so for the active element,
    whose edit the opening committed (an element he had stepped back mid-history and left finds that entry further
    back). `state.json` follows; the panel stays open on the active element, OLD now the loaded colour; one logcat
    line, `picker: preset load Preset N: committed <key> #RRGGBB ...; unchanged <key> ...`.
  - No rename, delete or overwrite (they wait for a keyboard).
- THE THEME STRIP (architect 2026-10-04: the product's themes as inspiration). A tap on a theme in the pop-up OPENS it:
  the pop-up closes and the panel grows a STRIP, a 360-px column 12 px beside it on the scene's side and the panel's
  height (the panel itself unchanged): the theme's name, a close control (an X, top right), and EVERY COLOUR THE
  CATALOG RECORDS FOR THE THEME, each a swatch beside its hex and every name that records it (wrapped small text), a
  list that scrolls as the pop-up's does. A TAP ON A SWATCH ADOPTS IT AS THE ACTIVE ELEMENT'S COLOUR, AS AN EDIT,
  exactly as a control's drag would: the bytes set, the view re-derived from them (a theme stores no view), the scene
  live, NEW showing it, OLD still reverting, the close the one save. Adopted as the chrome it sets the ground, and every
  line follows by Windows 95's rule, never the theme's own relief. The swatch showing the active element's colour is
  marked. THE STRIP STAYS OPEN ACROSS ELEMENT SWITCHES (he adopts for another element by choosing it with the chooser)
  and across relaunches (`state.json`'s `"theme"`), until its close control or another theme opened; it shows while
  the panel is open, and a tap on it is never a tap outside the panel. Opening and closing it log a line each.
- A tap OUTSIDE the panel closes it, and that close is the ONE DELIBERATE SAVE: it COMMITS the pick when the colour
  is edited (the history's rules, below in Use). While closed, a small label at the bottom right shows the active
  element's name, hex and count. Leaving the app with the panel open (home, the cover) saves nothing: the unsaved colour
  is discarded and the app comes back on the last saved state.
- The panel's chrome is the app's own greys: the ground #191919, the label #FFFFFF, fields #212121, flat 1-px #000000
  edges; no relief, no alpha. Text in Roboto and Roboto Mono, FreeType + cairo as the product does it.

## Files on the tablet

Everything lives in the app's EXTERNAL files dir, `/sdcard/Android/data/com.warptempo.picker/files/` (the
`externalDataPath`), which the app creates at its first launch:

| path | written by | what |
|---|---|---|
| `scene/manifest.json`, `scene/<scene>.base.pgm`, `scene/<scene>.cover.bin` | the planner (`adb push`) | the export (formats below) |
| `picks.txt` | the app, at every commit | one line appended: `<ISO-8601 local time> <key> #RRGGBB hsv <h> <s> <v>` (the element's key; the view the pick was saved under) |
| `state.json` | the app, at every close of the panel, every switch of element, a commit at the presets pop-up's opening, a preset load, and the theme strip's opening and close | `{"active": "<key>", "colours": {"<key>": "#RRGGBB", ...}, "hsv": {"<key>": [h, s, v], ...}, "entry": {"<key>": N, ...}, "theme": "<theme key>"}`: the active element, every element's colour, its view and its history cursor (the N of "N of M"; absent with an empty history), and the open theme strip's theme (absent when none), rewritten whole. It is always THE LAST SAVED STATE: while the panel is open the active element is written as the panel's opening (OLD and its cursor), so an unsaved edit never reaches it |
| `presets.json` | the app, at every save of a preset | `{"presets": [{"number": N, "saved": "<ISO-8601 local time>", "colours": {"<key>": "#RRGGBB", ...}, "hsv": {"<key>": [h, s, v], ...}}, ...]}`: the presets oldest first, the numbers ascending (the name is "Preset N"), every colour with its view over the same keys, rewritten whole |

THE VIEW'S NUMBERS are h in degrees 0..360 and s, v in 0..1, written as the shortest decimal that reads back as the
same double (`227`, `0.35`, `0.34671532846715331` after a drag), so a stored view is restored exactly; a view must
give its hex (`rgb_of_hsv`), and one that does not, or lies outside those ranges, fails the load. Earlier files still
read: a `picks.txt` line without a view, `<ISO-8601 local time> <key> #RRGGBB`, is a pick whose view is re-derived
from its bytes as before (the lines are never rewritten; a new commit appends the new form after them), and a
`state.json` without `"hsv"` starts on its colour re-derived; one with `"hsv"` and `"entry"` for one layer alone and
no `"active"` (the single-layer builds', such as the ink round's `{"colours": {"ink": "#A6B9DE"}, "entry": {"ink":
58}}`) starts every other element at the manifest's colour, on the manifest's active element. A key that is no element
of the export (an earlier round's layer) is read and never used; so is a `"theme"` the export does not list, and a
preset's key that is no element (that element keeps its colour at a load). `presets.json` is read as strictly as the
others: a number not above the one before, a colour without its view, or a view not giving its hex fails the load.

At launch EVERY ELEMENT starts at its colour in `state.json` if there is one (the last close, with its view when
the file has one), else the manifest's; its history is its `picks.txt` lines, and its cursor `state.json`'s entry when
that entry lies in the history and holds the start, else the newest entry holding it, else the end. The active
element is `state.json`'s when it names one, else the manifest's. An entry HOLDS the
start when their bytes are equal and, if both carry a view, their views are too (the first build's `state.json`,
`{"<layer>": "#RRGGBB", ...}` with no entry, still reads; so does a kept `state.json` beside a deleted `picks.txt`).
Every commit also logs one line under the tag `warptempo_picker`: `picker: commit <key> #RRGGBB`; every history step
`picker: step <key> N of M #RRGGBB`; every switch `picker: element <key> #RRGGBB N of M`. A missing or malformed scene, `state.json` or `picks.txt`
shows a plain message on the screen (which file, what is wrong) and logs it; there is no recovery path.

## The export's formats

Written by `python3 tools/palette/render.py <theme.json> --export <dir>` (tools/palette/README.md, the export, and
render.py's export section), every scene of the theme in one export; `src/scene.h`'s head is the authoritative
statement:

- `manifest.json`: `{"width": 2304, "height": 1440, "active": "chrome", "elements": [{"key": "chrome", "name":
  "Chrome", "colour": "#191919", "scene": "waveform"}, ...], "roles": [...], "scenes": [{"name": "waveform", "base":
  "waveform.base.pgm", "cover": "waveform.cover.bin"}]}`. An ELEMENT: its key (`[a-z][a-z0-9_]*`, its word in
  picks.txt and state.json), its Title Case name, its colour, its scene; 1..32. A ROLE: its name and ONE RULE —
  `{"element": key}` the element's colour; `{"scale": {"of": key, "num": n, "den": d}}` THE CHROME RULE's line, each
  channel x n / d rounded half to even (levels.py's rounding) and capped at 255 (`src/colour.h` scale_byte;
  tools/palette/README.md, the chrome rule: Hilight 255, 3DLight 223, Shadow 128 over 192, the field and the emboss
  the Hilight); `{"derive": {"from": key, "over": key or "#rrggbb", "linear_mix": t}}` the linear-light mix of one
  element toward another (the outline over the LIVE canvas) or a literal (colour.py's `lin_mix`, ported in
  `src/colour.h`, rounded half to even); `{"colour": "#rrggbb"}` fixed. A rule follows elements only; 1..255 roles.
  The width and height must be the window's (2304 x 1440).
- `<scene>.base.pgm`: binary P5, 8-bit, width x height: each byte the role index of the pixel's BASE.
- `<scene>.cover.bin`: the antialiased pixels' COVERAGE STACKS: `WTCOVER1`, a little-endian uint32 count, then per
  pixel in ascending order its uint32 index y x width + x, a uint8 depth n >= 1 and n (uint8 role, uint8 coverage
  1..254) pairs, oldest first. Such a pixel is its base's colour with each paint blended over it by CAIRO'S OWN
  ARITHMETIC (`src/colour.h` over_n_8: pixman 0.46.4's fast_composite_over_n_8_8888 and pixman-combine32.h's
  UN8x4_MUL_UN8 / UN8x4_MUL_UN8_ADD_UN8x4, line for line), so the picture is render.py's byte for byte at any colours.
- `themes.json`: THE PRODUCT'S THEMES, `{"source": "...", "themes": [{"key": "windows-95-standard", "name": "Windows
  95 Standard", "ground": "#C0C0C0", "colours": [{"hex": "#C0C0C0", "names": ["ground", "Scrollbar", ...]}, ...]},
  ...]}`: every entry of the product's theme table, LIGHT level, in the table's order (98 today), each with every
  distinct colour the catalog records for it, the ground first (`render.py` product_themes is the authoritative
  statement: the source is `docs/themes/catalog.json`, exactly what `tools/theme_catalog/gen_theme_table.py` generates
  `src/gui/theme_table.h` from, and the table is read too and must agree, so a stale table fails the export). A
  theme's colours are its catalog roles, every raw value its source records under the source's own key names, and
  every value its toolkit's rule computed at import (KDE 3's relief, CDE's Motif shades of each colour set): 6 to 34
  colours a theme.
- An earlier export (`layers`, `background.ppm`) is refused with a message: export the theme again; so is an export
  without `themes.json` (written before the presets).

NOTHING IS RASTERIZED ON THE DEVICE. At load each scene's base map becomes runs of one role per row (the stacked
pixels left out) and its stacks a list, each carrying the bits of the elements its colour reads. A colour change
refills the runs of every role whose colour moved and re-blends the stacks that read the moved element (a lookup over
~10^4..10^5 pixels); the picture is then copied into the frame as before.

## Build

```
bash tools/palette/picker/build_picker.sh            # the APK: tools/palette/picker/build/warptempo_picker.apk
bash tools/palette/picker/build_picker.sh --check    # the laptop check, no device
```

The APK build is a stripped `android/app/build_apk.sh`: the NDK's clang++ over `src/` and the NDK's
`android_native_app_glue` (no CMake), static cairo / pixman / FreeType / HarfBuzz, `-ffp-contract=off`, javac -> d8 for
the one-class sliver, aapt2 link (no `res/`: no icon), the `.so` stored, zipalign -P 16, apksigner with the product's
debug keystore (never minted: a missing keystore stops the script), then the verification (signature, alignment, 16 KB
LOAD segments, DT_NEEDED exactly libc / libdl / libm / libandroid / libnativewindow / liblog). Its output is
`build/`, git-ignored by the root's `build*/`.

The LAPTOP CHECK builds the portable core (`json`, `scene`, `fonts`, `picker`) with the host's g++, cairo and
FreeType into `build/check/host_check`; `check_refs.py` exports `themes/picker.json` afresh (`build/check/scene/`) and
THE CHECK THEME (`build/check/multi/`: the round's theme plus a Flag Test element, the flag face, over a second scene
with the flags and the first flag's editor open), and renders their references with the mock tool's own code. It
checks: every scene of both at the manifest's colours and at every colour set of `render.picker_check_sets` (each
element moved, the chrome to the tint #206048 whose 32 and 96 hit the rule's half-to-even ties and to the bright
#E6D2B4 whose lines cap, all moved at once) equals the mock tool's render BYTE FOR BYTE, painted whole and reached by
the live repaint; a synthetic export with roles derived over the canvas element and over a literal equals
`colour.py`'s composition at the canvas and the ink moved; the C++ `lin_mix` equals `colour.py`'s on all 65536 byte
pairs; THE BLEND equals cairo's solid source through an A8 mask on every (source, frame, coverage) byte triple, each
channel (16,777,216 cases); `scale_byte` is half to even and capped; HSV keeps the hue through grey and black and
bytes -> HSV -> bytes is the identity over the whole cube; the H / S / V readouts show one decimal rounded to nearest
(247.27 -> 247.3, 0.4714 -> 47.1, 0.99999 -> 100.0) and the widest, measured in the mono face, fits its field; the scripted sessions on the active element (the panel,
the pick history, the HSV he dialled: as before); THE CHOOSER (it opens; a tap outside it closes it alone, writing
nothing; a press lifted on another entry picks nothing; the active entry again is a no-op; the edited chrome
committed by the switch to the Ink; the switch to the Flag Test's scene with the chrome's tint and the ink's colour
live in it); every element's history, cursor and view independent across switches and relaunches, and the element he
left returned to; today's `picks.txt` (58 ink lines, 15 of the first build's form and 43 with a view, the last
#A6B9DE) and `state.json` (`{"colours": {"ink": "#A6B9DE"}, "entry": {"ink": 58}}`) loading unchanged; THE PRESETS
(the export's 98 themes; two saves named Preset 1 and Preset 2, the edited panel colour committed by the pop-up's
opening; a load giving each changed element one pick and an unchanged one none, OLD the loaded colour, BACK returning;
a relaunch keeping the presets and their views; a drag that scrolls and acts on nothing; a tap outside that changes
nothing; a malformed `presets.json` refused); THE THEME STRIP (Windows 95 Standard opened, its #C0C0C0 adopted into
the chrome as an edit giving its own quartet #FFFFFF / #DFDFDF / #808080 / #000000 exactly, OLD reverting it, the
switch to the canvas committing it with the strip still open, another swatch adopted into the canvas, the strip
surviving a relaunch; Brick's ground giving the rule's lines and not its own; CDE Alpine's strip scrolled by a drag
that adopts nothing; the close control, `state.json` never holding an unsaved adoption); THE TABLET'S FILES OF
2026-10-04 (`check_data/tablet_2026-10-04/`: 110 `picks.txt` lines over three elements and its `state.json`) loading
unchanged, every view exact, and a preset saved and loaded over them appending nothing; and THE
PER-FRAME COST of a pen drag on the ink and on the chrome (printed). Its frames are written as PNGs in
`build/check/work/` for the eye (`frame_chooser_open.png`, `frame_chrome_tint{,_open}.png`, `frame_flag_open.png`,
the history's `frame_history_*.png`, `frame_presets_open.png`, `frame_presets_themes.png` (the pop-up scrolled into
the themes), `frame_strip_{open,adopted,scrolled}.png`).

## Use (the planner, over adb)

```
adb install -r tools/palette/picker/build/warptempo_picker.apk
adb shell am start -n com.warptempo.picker/.PickerActivity          # once: the app makes its files dir (and says "no scene")
D=/sdcard/Android/data/com.warptempo.picker/files
adb shell mkdir -p $D/scene
adb shell rm -f $D/scene/background.ppm $D/scene/ink.pgm      # the ink round's export, refused now
adb push tools/palette/out/picker/. $D/scene/
adb shell chmod -R 777 $D/scene                                     # adb's new folders and files are shell's (770 / 660)
adb shell am force-stop com.warptempo.picker
adb shell am start -n com.warptempo.picker/.PickerActivity
adb logcat -s warptempo_picker:I                                    # the window's set-up lines, the scene, each commit and step
adb shell cat $D/picks.txt                                          # the picks; state.json and presets.json beside it
```

The `chmod` is the product's own experience (`scripts/warptempo_sync`: a directory `adb push` creates belongs to
`shell` with mode 770, which the app cannot list; chmod takes on this storage). If adb cannot read what the app wrote
there, read it as the app: `adb exec-out run-as com.warptempo.picker cat $D/picks.txt` (the build is debuggable).
If adb cannot write the external dir at all on this Android, push to `/data/local/tmp/picker_scene/` and copy as the
app: `adb shell run-as com.warptempo.picker sh -c "'mkdir -p $D/scene && cp /data/local/tmp/picker_scene/* $D/scene/'"`
(the host shell expands `$D`).

THE PICK HISTORY (architect 2026-10-04: backtrack and compare without typing a hex), ONE PER ELEMENT: its committed
picks, oldest first, exactly its `picks.txt` lines, read at launch (it survives restarts) and grown by each commit. A
cursor marks the entry being shown; the panel's count "N of M" is the active element's cursor's entry of its
history's length ("0 of 0" with none), and the closed corner label repeats it after the hex. Every element keeps its
own history, cursor and HSV view across switches and relaunches.

- BACK and FORWARD (the arrows under the hex) move the cursor one entry and restore that entry, its bytes and its
  view, at the pen's lift: the scene repaints live and every control follows; OLD stays the colour the panel opened
  with (or the element was chosen with).
- A disabled button (its glyph dimmed) does nothing: BACK at the first entry, FORWARD at the last, both with an empty
  history.
- CLOSING THE PANEL IS THE ONE DELIBERATE SAVE ("just throw it away. One deliberate save action"). The colour is
  EDITED when it differs from the cursor's entry — its bytes, or its view when the entry carries one (at low value an
  S − / + can move the view and not the bytes; the number he reads is what he saves) — or the history is empty. A close on an edited colour commits it
  (one `picks.txt` line, the history's new end, the logcat line) and the cursor goes to the end; a close on a colour
  reached by stepping, unchanged, appends nothing, and `state.json` records the colour and the cursor.
- CHOOSING ANOTHER ELEMENT IS THE CLOSE FOR THE ONE BEING LEFT (the planner's ruling, confirmed by the architect
  2026-10-04): an edited colour is committed exactly as a close commits it (one `picks.txt` line, the logcat line,
  `state.json`), then the panel stands open on the chosen element, OLD its current colour. He switches to see the new
  colour against its neighbours, and a switch that silently threw away the colour he is looking at would undo what he
  came to compare. By the same rule an element with an empty history commits its colour as he leaves it, as its
  first close would. Choosing the active element again is a no-op.
- An UNSAVED edit is thrown away by a step (BACK / FORWARD go from the cursor's entry) and by leaving the app with the
  panel open (the colour and the cursor return to the panel's opening, or the element's choice: the last saved
  state). No SAVED pick is ever thrown away: unlike an editor's undo there is no redo branch, a commit always appends
  at the end.

A new round is a new theme and a new export pushed over the old scene (no new app code); `state.json`'s colours win
over the manifest's at launch — delete `state.json` (and `picks.txt`, once read) to start a round from the manifest.
An element's history is every `picks.txt` line under its key, across rounds, until `picks.txt` is deleted.

## Source

| file | what |
|---|---|
| `src/colour.h` | the byte triple, hex, the frame's pixel word, `lin_mix` (colour.py's, step for step), `scale_byte` (the chrome rule), `over_n_8` (pixman's blend, line for line), HSV -> bytes |
| `src/json.{h,cpp}` | the tiny JSON reader (manifest.json, state.json) |
| `src/scene.{h,cpp}` | the export: load and validate (`themes.json` too), the roles' rules, the scene's runs and stacks, the whole paint and the live repaint; the Pick (bytes and view); the readers of state.json, picks.txt and presets.json |
| `src/fonts.{h,cpp}` | Roboto and Roboto Mono from memory, as `src/gui/gui_font_bundled.cpp` builds them (SLIGHT hinting) |
| `src/picker.{h,cpp}` | the picker: ColourState (the bytes plus the retained hue, a stored view restored), the launch state (`picker_load`), every element's state and pick history, the chooser, the presets pop-up, the theme strip, the panel's geometry, painting, touch, the close's save |
| `src/main_android.cpp` | the glue's lifecycle, the window set-up, one-pointer touch, the R<->B blit |
| `src/host_check.cpp`, `check_refs.py`, `check_data/` | the laptop check (above); `check_data/tablet_2026-10-04/` the tablet's `picks.txt` and `state.json` of that morning, copied verbatim |
| `java/com/warptempo/picker/PickerActivity.java` | the full-screen sliver |
| `AndroidManifest.xml` | the package, the colour mode, the orientation lock |
| `build_picker.sh` | the APK, or `--check` |
