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
  either); the layer's name, its HEX in large type, the OLD | NEW swatches (the colour when the panel opened | now; a
  tap on OLD reverts); six SLIDERS, H 0–360, S 0–100, V 0–100 and R, G, B 0–255, each a long track painted with its
  live gradient (the colour along it, the other channels as they stand), a handle, its value at the right, and a
  one-unit − / + at its ends (one degree, one percent, one byte; acting at the lift). Every control produces an exact
  byte triple and the active layer repaints from it live. HSV is a view over the bytes plus a retained hue (and
  saturation), as GTK keeps them: through grey the hue stays, through black the saturation too.
- A tap OUTSIDE the panel closes it and COMMITS the pick. While closed, a small label at the bottom right shows the
  active layer's name and hex. Leaving the app with the panel open (home, the cover) commits too.
- The panel's chrome is the app's own greys: the ground #191919, the label #FFFFFF, fields #212121, flat 1-px #000000
  edges; no relief, no alpha. Text in Roboto and Roboto Mono, FreeType + cairo as the product does it.

## Files on the tablet

Everything lives in the app's EXTERNAL files dir, `/sdcard/Android/data/com.warptempo.picker/files/` (the
`externalDataPath`), which the app creates at its first launch:

| path | written by | what |
|---|---|---|
| `scene/manifest.json`, `scene/background.ppm`, `scene/<layer>.pgm` | the planner (`adb push`) | the scene (formats below) |
| `picks.txt` | the app, at every commit | one line appended: `<ISO-8601 local time> <layer> #RRGGBB` |
| `state.json` | the app, at every commit | `{"<layer>": "#RRGGBB", ...}`: every layer's colour at the last commit, rewritten whole |

At launch the active layer starts at its colour in `state.json` if there is one (the last committed pick), else the
manifest's. Every commit also logs one line under the tag `warptempo_picker`: `picker: commit <layer> #RRGGBB`. A
missing or malformed scene shows a plain message on the screen (which file, what is wrong) and logs it; there is no
recovery path.

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
`picks.txt` and `state.json`. Its frames are written as PNGs in `build/check/` for the eye.

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
adb logcat -s warptempo_picker:I                                    # the window's set-up lines, the scene, each commit
adb shell cat $D/picks.txt                                          # the picks; state.json beside it
```

The `chmod` is the product's own experience (`scripts/warptempo_sync`: a directory `adb push` creates belongs to
`shell` with mode 770, which the app cannot list; chmod takes on this storage). If adb cannot read what the app wrote
there, read it as the app: `adb exec-out run-as com.warptempo.picker cat $D/picks.txt` (the build is debuggable).
If adb cannot write the external dir at all on this Android, push to `/data/local/tmp/picker_scene/` and copy as the
app: `adb shell run-as com.warptempo.picker sh -c "'mkdir -p $D/scene && cp /data/local/tmp/picker_scene/* $D/scene/'"`
(the host shell expands `$D`).

A new round is a new theme and a new export pushed over the old scene (no new app code); `state.json`'s colour for the
new active layer, if any, wins over the manifest's at launch — delete `state.json` (and `picks.txt`, once read) to
start a round from the manifest.

## Source

| file | what |
|---|---|
| `src/colour.h` | the byte triple, hex, the frame's pixel word, `lin_mix` (colour.py's, step for step), HSV -> bytes |
| `src/json.{h,cpp}` | the tiny JSON reader (manifest.json, state.json) |
| `src/scene.{h,cpp}` | the scene: load and validate, derive, paint the picture; state.json |
| `src/fonts.{h,cpp}` | Roboto and Roboto Mono from memory, as `src/gui/gui_font_bundled.cpp` builds them (SLIGHT hinting) |
| `src/picker.{h,cpp}` | the picker: ColourState (the bytes plus the retained hue), the panel's geometry, painting, touch, commit |
| `src/main_android.cpp` | the glue's lifecycle, the window set-up, one-pointer touch, the R<->B blit |
| `src/host_check.cpp`, `check_refs.py` | the laptop check (above) |
| `java/com/warptempo/picker/PickerActivity.java` | the full-screen sliver |
| `AndroidManifest.xml` | the package, the colour mode, the orientation lock |
| `build_picker.sh` | the APK, or `--check` |
