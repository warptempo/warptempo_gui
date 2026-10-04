# tools/palette/picker — Warptempo Picker, the colour picker over a true picture of the app

A design tool for picking the program's own colours (render.h's palette block: the waveform's ink, the flags, ...)
by hand on the tablet's glass, over a picture of the app exported by the mock tool. A separate APK, package
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

The SCENE, full screen: the background picture and over it the LAYERS, each painted in its colour through its
binary mask, in manifest order. One layer is ACTIVE (the manifest names it): the one being picked. A layer may be
DERIVED from another (the waveform outline: the 50 % linear-light mix of the ink over the canvas) and then repaints
live with it. No menus, no layer switching: a round is a scene.

- A TAP on the picture opens the PANEL on the half of the screen opposite the tap, so the place tapped stays in view.
- The panel (GTK's colour selector and GIMP's colour dialog, their common ground, HSV only, no CMYK): the hue RING with
  the saturation / value TRIANGLE inside it (its corners the pure hue, white and black, turning with the hue; drag
  either); the layer's name, its HEX in large type, under it BACK | "N of M" | FORWARD (the pick history, below in
  Use), the OLD | NEW swatches (the colour when the panel opened | now; a
  tap on OLD reverts); six SLIDERS, H 0–360, S 0–100, V 0–100 and R, G, B 0–255, each a long track painted with its
  live gradient (the colour along it, the other channels as they stand), a handle, its value at the right, and a
  one-unit − / + at its ends (one degree, one percent, one byte; acting at the lift). Every control produces an exact
  byte triple and the active layer repaints from it live. HSV is a view over the bytes plus a retained hue (and
  saturation), as GTK keeps them: through grey the hue stays, through black the saturation too.
- A tap OUTSIDE the panel closes it, and that close is the ONE DELIBERATE SAVE: it COMMITS the pick when the colour
  is edited (the history's rules, below in Use). While closed, a small label at the bottom right shows the active
  layer's name, hex and count. Leaving the app with the panel open (home, the cover) saves nothing: the unsaved colour
  is discarded and the app comes back on the last saved state.
- The panel's chrome is the app's own greys: the ground #191919, the label #FFFFFF, fields #212121, flat 1-px #000000
  edges; no relief, no alpha. Text in Roboto and Roboto Mono, FreeType + cairo as the product does it.

## Files on the tablet

Everything lives in the app's EXTERNAL files dir, `/sdcard/Android/data/com.warptempo.picker/files/` (the
`externalDataPath`), which the app creates at its first launch:

| path | written by | what |
|---|---|---|
| `scene/manifest.json`, `scene/background.ppm`, `scene/<layer>.pgm` | the planner (`adb push`) | the scene (formats below) |
| `picks.txt` | the app, at every commit | one line appended: `<ISO-8601 local time> <layer> #RRGGBB` |
| `state.json` | the app, at every close of the panel | `{"colours": {"<layer>": "#RRGGBB", ...}, "entry": {"<active layer>": N}}`: every layer's colour at the last close and the active layer's history cursor (the N of "N of M"), rewritten whole |

At launch the active layer starts at its colour in `state.json` if there is one (the last close), else the
manifest's; its history is its `picks.txt` lines, and the cursor `state.json`'s entry when that entry lies in the
history and holds the colour, else the newest entry equal to the colour, else the end (a `state.json` from the
previous build, `{"<layer>": "#RRGGBB", ...}` with no entry, still reads; so does a kept `state.json` beside a deleted
`picks.txt`). Every commit also logs one line under the tag `warptempo_picker`: `picker: commit <layer> #RRGGBB`;
every history step `picker: step <layer> N of M #RRGGBB`. A missing or malformed scene, `state.json` or `picks.txt`
shows a plain message on the screen (which file, what is wrong) and logs it; there is no recovery path.

## The scene's formats

Written by `python3 tools/palette/render.py <theme.json> --export <dir>` (tools/palette/README.md, the export):

- `manifest.json`: `{"width": 2304, "height": 1440, "background": "background.ppm", "active": "ink", "layers":
  [{"name": "ink", "mask": "ink.pgm", "colour": "#808080"}, {"name": "outline", "mask": "outline.pgm", "derive":
  {"from": "ink", "over": "#0E0E0E", "linear_mix": 0.5}}]}`. A layer has exactly one of `colour` (`#rrggbb`) and
  `derive`: its colour is `from`'s mixed toward `over` by `linear_mix` in linear light (each byte decoded by the sRGB
  transfer function, `from` x (1 - t) + `over` x t, encoded, rounded half to even: `colour.py`'s `lin_mix`, ported in
  `src/colour.h`); a derive follows a picked layer, and the active layer is never derived. The width and height must
  be the window's (2304 x 1440).
- `background.ppm`: binary P6, 8-bit, width x height.
- `<layer>.pgm`: binary P5, 8-bit, the same size, every byte 0 or 255 (anything else is a hard fail: the palette
  composites nothing). Where a mask is 255 the pixel is the layer's colour.

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
FreeType into `build/check/host_check`, has `check_refs.py` export `themes/picker_ink.json` afresh and render its
references with the mock tool's own code, and checks: the scene's picture is the background byte for byte; the C++
`lin_mix` equals `colour.py`'s on all 65536 byte pairs; the picture with the ink at #CC9966 equals the mock tool's
render of the theme with that ink, byte for byte; a synthetic scene with a derived layer (today's has none) equals
`colour.py`'s composition; HSV keeps the hue through grey and black and bytes -> HSV -> bytes is the identity; a
scripted session (open on the opposite half, a track, an increment, the ring, OLD, close) behaves and commits to
`picks.txt` and `state.json`; a scripted HISTORY session covers the empty history, commits, BACK and FORWARD, the
disabled ends, a no-op close after a step, an edit-then-step discard, leaving the app with an unsaved edit, and
reloads restoring the cursor (this build's `state.json`, the previous build's, a deleted `picks.txt`). Its frames are
written as PNGs in `build/check/` for the eye (`frame_history_{empty,mid,first,closed}.png` the history's).

## Use (the planner, over adb)

```
adb install -r tools/palette/picker/build/warptempo_picker.apk
adb shell am start -n com.warptempo.picker/.PickerActivity          # once: the app makes its files dir (and says "no scene")
D=/sdcard/Android/data/com.warptempo.picker/files
adb shell mkdir -p $D/scene
adb push tools/palette/out/picker_ink/. $D/scene/
adb shell chmod -R 777 $D/scene                                     # adb's new folders and files are shell's (770 / 660)
adb shell am force-stop com.warptempo.picker
adb shell am start -n com.warptempo.picker/.PickerActivity
adb logcat -s warptempo_picker:I                                    # the window's set-up lines, the scene, each commit and step
adb shell cat $D/picks.txt                                          # the picks; state.json beside it
```

The `chmod` is the product's own experience (`scripts/warptempo_sync`: a directory `adb push` creates belongs to
`shell` with mode 770, which the app cannot list; chmod takes on this storage). If adb cannot read what the app wrote
there, read it as the app: `adb exec-out run-as com.warptempo.picker cat $D/picks.txt` (the build is debuggable).
If adb cannot write the external dir at all on this Android, push to `/data/local/tmp/picker_scene/` and copy as the
app: `adb shell run-as com.warptempo.picker sh -c "'mkdir -p $D/scene && cp /data/local/tmp/picker_scene/* $D/scene/'"`
(the host shell expands `$D`).

THE PICK HISTORY (architect 2026-10-04: backtrack and compare without typing a hex). The history is the active
layer's committed picks, oldest first: exactly its `picks.txt` lines, read at launch (it survives restarts) and grown
by each commit. A cursor marks the entry being shown; the panel's count "N of M" is the cursor's entry of the
history's length ("0 of 0" with none), and the closed corner label repeats it after the hex.

- BACK and FORWARD (the arrows under the hex) move the cursor one entry and set the colour to that entry, at the pen's
  lift: the layer repaints live and every control follows; OLD stays the colour the panel opened with.
- A disabled button (its glyph dimmed) does nothing: BACK at the first entry, FORWARD at the last, both with an empty
  history.
- CLOSING THE PANEL IS THE ONE DELIBERATE SAVE ("just throw it away. One deliberate save action"). The colour is
  EDITED when it differs from the cursor's entry (or the history is empty). A close on an edited colour commits it
  (one `picks.txt` line, the history's new end, the logcat line) and the cursor goes to the end; a close on a colour
  reached by stepping, unchanged, appends nothing, and `state.json` records the colour and the cursor.
- An UNSAVED edit is thrown away by a step (BACK / FORWARD go from the cursor's entry) and by leaving the app with the
  panel open (the colour and the cursor return to the panel's opening, the last saved state). No SAVED pick is ever
  thrown away: unlike an editor's undo there is no redo branch, a commit always appends at the end.

A new round is a new theme and a new export pushed over the old scene (no new app code); `state.json`'s colour for the
new active layer, if any, wins over the manifest's at launch — delete `state.json` (and `picks.txt`, once read) to
start a round from the manifest. A layer's history is every `picks.txt` line under its name, across rounds, until
`picks.txt` is deleted.

## Source

| file | what |
|---|---|
| `src/colour.h` | the byte triple, hex, the frame's pixel word, `lin_mix` (colour.py's, step for step), HSV -> bytes |
| `src/json.{h,cpp}` | the tiny JSON reader (manifest.json, state.json) |
| `src/scene.{h,cpp}` | the scene: load and validate, derive, paint the picture; the readers of state.json and picks.txt |
| `src/fonts.{h,cpp}` | Roboto and Roboto Mono from memory, as `src/gui/gui_font_bundled.cpp` builds them (SLIGHT hinting) |
| `src/picker.{h,cpp}` | the picker: ColourState (the bytes plus the retained hue), the launch state (`picker_load`), the pick history, the panel's geometry, painting, touch, the close's save |
| `src/main_android.cpp` | the glue's lifecycle, the window set-up, one-pointer touch, the R<->B blit |
| `src/host_check.cpp`, `check_refs.py` | the laptop check (above) |
| `java/com/warptempo/picker/PickerActivity.java` | the full-screen sliver |
| `AndroidManifest.xml` | the package, the colour mode, the orientation lock |
| `build_picker.sh` | the APK, or `--check` |
