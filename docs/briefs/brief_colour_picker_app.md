# BRIEF: the colour picker app (tools/palette/picker/) and the mock tool's element switches + export (2026-10-04)

Model: Opus, effort high. You are the implementation subagent for warptempo_gui: read CLAUDE.md first (the roles, the
shell hygiene), then `tools/palette/README.md` whole, then this brief.

## Your fence
- You write ONLY under `tools/palette/` (the new `tools/palette/picker/` included). Everything else is read-only: no
  `src/`, no `android/app/`, no `CMakeLists.txt`, no `docs/`, no CLAUDE.md, no root `.gitignore` (a
  `tools/palette/picker/.gitignore` of your own is fine).
- Git: read-only (status / diff / log / show). Never stage or commit. Never `scripts/warptempo_sync` in any form.
- NO adb, no touching the tablet: the planner installs and verifies. You MAY build the picker APK with your own
  build script to self-check, and run `tools/palette/render.py` as much as you like.
- Scratch goes under `tmp/` (gitignored). Never put a shell variable in an `rm -rf` path.
- The product is untouched and not rebuilt. If something in the product's Android pieces seems to need a change,
  stop and say so in your report instead.

## Why
The architect picks the program's own colours (render.h's palette block: the waveform ink, the flags, ...) by eye on
the tablet's glass. Option-and-talk mock rounds settled the greys (the chrome is the `warptempo` light row; the
canvas is #0E0E0E) but failed for THE INK, which will be the defining colour of the app and needs the full gamut.
He wants to pick it by hand, live, on the glass, with a real colour picker over a true picture of the app — then
the unselected flag, then the selected flag, each a later round with no new app code. Third-party paint apps have
unknown colour handling; this app exists because ITS COLOUR HANDLING IS THE PRODUCT'S BY CONSTRUCTION.

## Part A — the picker app: `tools/palette/picker/`
A standalone design tool: its own `README.md` (what it is, that it has no link path to or from the product, how to
build, install, feed and read it), its own build script, its own source. C++ over the NDK, a few hundred lines to a
thousand. A SEPARATE APK, package `com.warptempo.picker`, label "Warptempo Picker", installed beside the product;
it never touches `com.warptempo.gui` or its files. `debuggable="true"`.

### Build
Reuse, do not fork: `android/toolchain/00_env.sh` (the NDK, build tools, JDK paths and `wt_say` / `wt_die`),
`android/prebuilt/arm64-v8a` (static cairo, pixman, FreeType, harfbuzz — note there is NO libpng, so cairo cannot
read PNGs on the device; see the file formats below), the Roboto faces in `fonts/`, and the steps of
`android/app/build_apk.sh` (the same debug keystore, never minted: the script's own keystore rule applies verbatim;
aapt2, javac -> d8 if you keep a Java sliver, zipalign -P 16, apksigner, verify). A stripped variant of that
script under `tools/palette/picker/`, its build output in a gitignored directory beside it
(e.g. `tools/palette/picker/build/`; the root `.gitignore`'s `build*/` may already cover it — check). A small
CMake project or direct clang++ calls through the NDK — your call. `android_native_app_glue` (as the product uses it)
is fine.

### Colour truth (the reason it exists — do this exactly)
The window must be set up EXACTLY as the product's: read `GuiPlatform::adopt_window` in
`src/gui/platform_android.cpp` (and that file's head on the P3 measurement) and the product's
`android/app/AndroidManifest.xml` (`android:colorMode="wideColorGamut"`, the comment on it). Same buffer format
(`WINDOW_FORMAT_RGBA_8888` via `ANativeWindow_setBuffersGeometry`), same `ANativeWindow_setBuffersDataSpace(...,
ADATASPACE_DISPLAY_P3)`, same manifest colour mode, full screen with BOTH system bars hidden sticky-immersive as the
product's `android/app/java/com/warptempo/gui/MainActivity.java` does (copy its bar-hiding into your own one-class
sliver under your own package if that is the road; the landscape lock as the product's: `sensorLandscape`). Then a
hex shown in the picker is the same hex shown by the product, on the same panel. NO COLOUR CONVERSION ANYWHERE: a
hex is a Display-P3 byte triple written to the buffer as is (render.h's "A HEX HERE IS A DISPLAY-P3 BYTE TRIPLE").
Paint with cairo into an image surface and copy it into the locked `ANativeWindow_Buffer` (mind cairo's ARGB32
byte order versus RGBA_8888 and the buffer's stride), or write the buffer directly — your call; either way the
bytes land unchanged. Log the dataspace call's result like the product does.

### What it shows
Full screen (the tablet: 2304x1440 landscape), the SCENE: a background picture of the app exported by the mock tool
(Part B), and over it the LAYERS named in the scene's manifest, each painted in its current colour through its
mask. One layer is ACTIVE (the manifest names it): the one being picked. Today's scene is the ink round: chrome
`warptempo` + canvas #0E0E0E + the waveform's ink (and its outline, if the mock tool draws one — see Part B), with
the marker stems, flags, playhead and row 8's state line switched off.

- Masks are BINARY (0 or 255; anything else is a hard fail with a logcat line — the palette composites nothing).
  Painting: for each layer in manifest order, where its mask is 255 the pixel is the layer's colour.
- A layer may be DERIVED from another instead of picked: the waveform outline's rule is the 50 % LINEAR-LIGHT blend
  of the ink over the canvas (the sRGB transfer function applied to the P3 bytes, as `colour.py` / the old
  `make_cr.py` blend does: decode each byte to linear, average, encode, round). The manifest states the rule
  (e.g. `"derive": {"from": "ink", "over": "#0E0E0E", "linear_mix": 0.5}`) and the app repaints it live with the ink.
- On launch the active layer's colour is the last committed pick if one exists (the state file below), else the
  manifest's.

### The picker panel (the architect's ask: GTK's / GIMP's colour dialogs, without CMYK)
He knows the GTK colour selector (the one XFCE's terminal opens) and GIMP's colour dialog well; the panel follows
their common ground, on the S Pen (single touch is enough; a finger works the same):

1. A COLOUR WHEEL: a hue ring with a saturation/value TRIANGLE inside it (GTK's selector, GIMP's "Wheel"). Drag on
   the ring turns the hue; drag in the triangle picks saturation and value; both repaint the active layer live.
2. SLIDERS in GIMP's "Scales" manner — each a long horizontal track painted with its own LIVE GRADIENT (the colour
   that results along it, given the other channels), a handle, the numeric value beside it, and a one-unit
   decrement / increment at its ends (for fine steps by tap):
   - H 0–360, S 0–100, V 0–100 — HSV only, as both apps show it (architect 2026-10-04: "I meant HSV"; no HSL,
     no switch);
   - R, G, B 0–255 (one unit = one byte).
   No CMYK, no LCh.
3. The HEX in large type (`#RRGGBB`, uppercase), and an OLD | NEW swatch pair (the colour when the panel opened |
   now); a tap on OLD reverts to it.

The colour's truth is the RGB byte triple: every control produces an exact byte triple and repaints from it. Keep
the hue (and saturation) while the colour passes through grey or black, as GTK does, so a drag never snaps the
hue to 0 — HSV is a view over the bytes plus that retained hue. Dragging any control repaints the active layer
on the picture live (as fast as the panel allows; damage only what changed if full-frame copies are slow — you
cannot measure on the device, so make it reasonable: a 2304x1440 copy per move event is acceptable to start).

OPENING AND CLOSING: a tap anywhere on the picture opens the panel; it opens on the HALF of the screen OPPOSITE the
tap (so the place he tapped to look at stays in view). A tap outside the panel closes it and COMMITS the pick
(below); while closed, the hex of the active layer stays readable in a small corner label (bottom right, clear of
row 8's clock panel is fine). No other UI: no menus, no layer switching (the manifest names the active layer).

Panel chrome: draw it plainly in the app's own chrome colours so it does not bias the eye — the ground #191919, the
label white #FFFFFF, fields #212121, flat 1-px edges in #000000 (no relief needed, no gradients other than the
slider tracks and the wheel, no rounded corners, no alpha). Text in Roboto (sans) and Roboto Mono for the numbers
and the hex, FreeType + cairo as the product does it (`src/gui/gui_font.h` is the reference; the faces may be
loaded from APK assets as the product's `build_apk.sh` step 1 does). Sizes: the panel is for an S Pen at arm's
length on a 2304x1440 11-inch panel — numbers about the ruler labels' drawn size or larger, the hex clearly
larger; slider tracks long (precision: 256 bytes over the track). Lay it out sensibly; the architect will judge it
on the glass.

### Read-back (so he never reads hex aloud)
- Every COMMIT (panel close) appends a line `<ISO-8601 local time> <layer> #RRGGBB` to `picks.txt`, rewrites
  `state.json` (each layer's current colour), and logs one line to logcat under the tag `warptempo_picker`
  (`picker: commit <layer> #RRGGBB`).
- Where: the app's EXTERNAL files dir (`ANativeActivity::externalDataPath`, i.e.
  `/sdcard/Android/data/com.warptempo.picker/files/`), so the planner reads with `adb pull` / `adb shell cat`;
  the SCENE is read from `<that dir>/scene/` too (the planner pushes it there). If the scene is missing or
  malformed, the app shows a plain message on the screen (which file, what is wrong) and logs it — no recovery.
  State in the README the exact adb commands to push a scene, launch, and read the picks; and the fallback via
  `adb shell run-as com.warptempo.picker` if the external dir is not writable by adb on this Android (the planner
  will find out).

### File formats (no PNG decoder on the device)
- `background.ppm`: binary P6, 8-bit, the full screen (2304x1440).
- `<layer>.pgm`: binary P5, 8-bit, same size, the binary mask (absent for a derived layer only if the rule says the
  derived layer has its own mask — it does: the outline has its own pixels, so every layer has a mask).
- `manifest.json`: `{"width":2304,"height":1440,"background":"background.ppm","active":"ink","layers":[{"name":"ink",
  "mask":"ink.pgm","colour":"#808080"},{"name":"outline","mask":"outline.pgm","derive":{...}}]}` — or an
  equivalent of your design; document it in the README. A tiny hand JSON reader in C++ is fine (the producer is
  ours, no adversarial input).

## Part B — the mock tool: element switches and the export (`tools/palette/render.py`)
1. ELEMENT SWITCHES as theme options (a theme's `options` block, documented in README's Options): each on/off —
   the waveform ink, its outline (if render.py draws one; if it does not, say so and treat the outline layer as
   absent — then the manifest has the ink alone), the marker stems, the flags, the playhead (head, its outline, its
   lane stem), row 8's state line. Defaults: everything on (today's pictures unchanged — verify by rendering an
   existing theme before and after and `cmp`-ing the PNGs). These replace the scratch monkeypatch wrappers
   `tmp/colour_arc2/cq/render_canvas_only.py` and `tmp/colour_arc2/cr/render_ink_only.py`: read both — they show
   exactly which functions each switch suppresses.
2. THE ROW-8 STAMP as the architect ruled it (2026-10-04) — `render_ink_only.py`'s `stamp()` is the reference: the
   label at the RULER TIMESTAMPS' DRAWN size (`ruler_label_seat(th)[0]`, 27.5 device px on the tablet geometry —
   NOT `ruler_label_px`, which answers 32 there; that mistake cost a round), in the theme's LABEL colour, its drawn
   glyphs' ink box (cairo's text extents) centred vertically on row 8's content rows, 16 device px past the status
   panel's right edge. It replaces the current `--label` stamp when the state line is off; with the state line on,
   keep whatever `--label` does today unless the stamp then collides — state what you chose.
3. THE EXPORT: `render.py THEME --export DIR` (or a sibling script in tools/palette — your call) writes the
   picker's scene: `background.ppm` (the picture with the picked layers' elements as rendered — or switched off;
   the masks overwrite them anyway), one mask per layer, and `manifest.json` naming the active layer and each
   layer's colour from the theme. Derive each mask truthfully from the renderer itself (e.g. render once with the
   layer in one sentinel colour and once in another and take the pixels that differ — or a direct mask pass;
   your call), and verify it: the background with every layer painted through its mask in the theme's colours must
   equal the normal render byte for byte (`cmp`); report it.
4. A theme for the ink round: `tools/palette/themes/picker_ink.json` — `warptempo.json` with the canvas #0E0E0E,
   the ink #808080 (the tablet's current key), the outline by its rule, and the switches: stems, flags, playhead,
   state line OFF. Export it into `tools/palette/out/picker_ink/` (gitignored) so the planner can push it.
5. README: the switches, the stamp, the export and its formats, and a line pointing at `picker/`.

## Report
What you built, file by file; the build script's exact invocation and the APK path; the `cmp` results of Part B
(defaults unchanged; the export recomposes byte-identically); every place you made a judgment call; anything you
could not verify without the device (the planner will check: the dataspace log line, a screencap, a commit's
picks.txt line). Do not claim device behaviour you have not seen.
