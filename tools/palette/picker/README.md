# tools/palette/picker — Warptempo Picker, the colour picker over a true picture of the app

A design tool for picking the app's colours (render.h's palette block: the chrome, the waveform's canvas and ink,
the flags, the playhead, the chrome's text, the selected pair) by hand on the tablet's glass, one ELEMENT at a time, choosing which, over a picture of the app
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

THE ELEMENTS (architect 2026-10-04, in his order of importance, the chooser's order): CHROME (the ground, one knob;
the active element at a fresh start), CANVAS, INK, over the scene `waveform`; then UNSELECTED FLAG (the flag's face
and its stem, starting #666699) and SELECTED FLAG (the selected face and its stem, starting #9999CC: the current
design for now, CE01), over the scene `flags` — the waveform scene with the marker stems and the flags on, the fourth
flag from the left (x 1816) SELECTED with its payload box the addressed cell, so its stem is bright, the state he
meets a selected flag in most (with a bound cell addressed the stem keeps the resting face). Both flag labels are
FIXED white (his ruling: consistency; the product's own keys `flag_label` and `flag_label_selected`, not the theme's
label, so the Label element below never moves them), fixed roles re-blended over the live faces; the invalid faces and the flag outline
are fixed roles too. The flags round added the elements and the scene in the theme and no app code but one fit: the
element button sets a name too long for it (Unselected Flag) smaller, so it ends clear of the chooser's head. Then
PLAYHEAD HEAD and PLAYHEAD STEM (architect 2026-10-04: the program's two playhead keys, render.h's THE PLAYHEAD block,
starting at the product's defaults #8B8B8B and #FCFCFC), over the scene `playhead` — the flags scene's stems and flags
with NO flag selected (in the app a marker click lands the playhead on it, and a playhead standing on a marker
suppresses its own stem, so a selected flag away from the playhead is not how he meets it) and the playhead ON at its
column, x 293: the head the product's aliased shape, opaque, with its one-Windows-px outline in the label (the Label element's role),
the stem uniform from the head down the marker lane and the well to the
canvas's foot. The playhead round added no app code: the theme's elements and scene, a new export (`waveform` and
`flags` byte for byte as before). Then LABEL (architect 2026-10-04: the chrome's TEXT colour is a pickable element, not
a black / white switch and not automatic contrast; the product imports each theme's recorded text colour and has no
contrast rule, and adopting #000000 from a theme strip makes a light variant), starting #FFFFFF (the fixed label until
then), over the scene `label` — the waveform scene with ROW 8'S STATE LINE ON ("Updating...", the line a freshly
loaded project shows; the app paints its process line, the render and queue status, "Loading..." and the history
walk, in the label, `paint_bottom_row_buttons_and_clock`), the stems, flags and playhead off: the most chrome text in
one picture (the menu words, the icons' text ink, the ruler's timestamps, the clock, the state line), and no flag
label, which would stay white beside it. Every role the theme's label roots follows it live — `label`, `legend`,
`clock`, `ruler_label`, `icon_label`, `trim_arrow`, `playhead_head_border` (the export's inventory,
tools/palette/README.md) — the antialiased edges re-blended over the chrome; the field text follows it too (the field
ground is the Hilight, a ground-family colour, so its text is the label's case); the selected text is the Selected
Text's (below); every disabled word and glyph is the emboss, the chrome's. The label round added no app code: the
theme's element and scene, a new export (`waveform`, `flags` and `playhead` byte for byte as before). Then SELECTION
and SELECTED TEXT (architect 2026-10-04: the theme's SELECTED PAIR, `selected_fill` and `selected_text`, Windows'
Hilight / HilightText — render.h's mapping: every text field's selection band and selected substring, a dropdown's lit
row, the open menu anchor — starting #666666 and #FFFFFF, the colours the app's `warptempo` theme records; the planner named the band
"Selection", not "Selected Fill", so it does not read as the Selected Flag in the chooser), over the scene
`open_flag` — the flags scene's stems and flags with NO flag selected and the FOURTH flag's EDITOR OPEN, its whole text
selected, as he meets it (opening a flag's editor selects its whole text): the face the selected face (the Selected
Flag's), the black frame (the flag editor's, the product's one named literal, fixed), the band the Selection under the
glyphs in the Selected Text, their antialiased edges re-blended over the band, the stem the selected face. The open-flag
round added the theme's elements and scene, a new export (`waveform`, `flags`, `playhead` and `label` byte for byte as
before), and one fit in the app: THE CHOOSER now runs down over the slider rows (below), its ten entries ending at
panel y 868. The round to come, again a new theme and export: the invalid flags.

WHERE THE FLAGS SHOW: a tap on the right half opens the panel on the left (x 16..1136), leaving the flags at x 1343
(unselected), 1816 (SELECTED) and 2289 (unselected, clipped at the window's edge) in view, the selected one and the
last still with the theme strip open (x 1148..1508); a tap on the left half opens the panel on the right, leaving the
unselected flags at x 398 and 871 (only the first with the strip open). WHERE THE PLAYHEAD SHOWS: at x 293 (its head
x 277..311 on the ruler lane's foot), left of the first flag, so the panel on the right (a tap on the LEFT half,
x 1168..2288, its strip x 796..1156) leaves it in view; the panel on the left covers it. WHERE THE LABEL SHOWS: everywhere; the panel on
the right (a tap on the left half) leaves the menu, the left half of the icon row, the ruler's first timestamps, the
clock and the state line in view. WHERE THE OPEN FLAG SHOWS: the fourth flag, x 1816, the flags scene's selected one
(the flag he selects, then opens), in view with the panel on the LEFT (a tap on the right half) and its theme strip
open (x 1148..1508); the panel on the right covers it. No flag stays in view on both halves (the two panels leave only
x 1136..1168 between them uncovered), so the open flag is one the strip leaves in view too, on the side the flags
round's selected flag is seen from (the first flag, x 398, is the only other a strip leaves in view).

- A TAP on the picture opens the PANEL on the half of the screen opposite the tap, so the place tapped stays in view.
- The panel (GTK's colour selector and GIMP's colour dialog, their common ground, no CMYK): the hue RING with
  the saturation / value TRIANGLE inside it (its corners the pure hue, white and black, turning with the hue; drag
  either); the active element's NAME as a button and the PRESETS button beside it (below), its HEX in large type, under it BACK | "N of M" | FORWARD (the pick history, below in
  Use), the OLD | NEW swatches (the colour when the panel opened | now; a
  tap on OLD reverts); under the wheel THE MODEL SWITCH (below); six SLIDERS, the model's three (HSV's H 0–360, S
  0–100, V 0–100 at a fresh start) and R, G, B 0–255, each a long track painted with its
  live gradient (the colour along it, the other channels as they stand), a handle, its value at the right, and a
  one-unit − / + at its ends (one degree, one percent, one L or C unit, one byte; acting at the lift). THE READOUT
  shows the model's three to ONE DECIMAL, rounded to nearest — a hue in degrees, HSV's and HSL's S, V, L in percent,
  LCh's L and C as they are (247.3, 47.1, 100.0: where the view actually is, as GIMP's fields read; architect
  2026-10-04) — R, G, B whole bytes; the six fields are one width, sized for "360.0". The model's − / + still step
  whole units, from the rounded whole number (speed over fineness). Every control produces an exact
  byte triple and the scene repaints from it live. A model is a view over the bytes plus a retained hue, as GTK keeps
  HSV's: through grey the hue stays (HSV's, HSL's, LCh's through C 0), through black HSV's saturation too, HSL's
  through black and white.
- THE MODEL SWITCH (architect 2026-10-04): a dropdown button under the wheel, at the left over the three tracks it
  switches, built as the chooser (a tap opens the list HSV / HSL / LCh with the shown model marked; a tap on an entry
  picks it, a press lifted on another picks nothing, a tap outside closes it). The three tracks, their − / + and their
  readouts then speak that model; R, G, B stay. THE RING AND THE TRIANGLE STAY HSV (GTK's and GIMP's own selector) and
  work in every model: in HSL the pen's HSV becomes the HSL view exactly (the same hue; the triangle never moves the
  hue he dialled), in LCh the pen's HSV gives the bytes and LCh reads them. The model is the panel's, every element's
  alike, and persists in `state.json` (one logcat line, `picker: model <HSV | HSL | LCh>`). Choosing a model changes
  no colour and no stored view.
  - HSL is GIMP's and CSS's HSL over the bytes: H 0–360, S 0–100, L 0–100.
  - LCh is CIE LCh(ab) as GIMP's LCh scales show it — L 0–100, C 0–160, h 0–360 — COMPUTED OVER THE BYTES AS
    DISPLAY-P3, the window's colour space (render.h: "A HEX HERE IS A DISPLAY-P3 BYTE TRIPLE"), with P3's own white
    D65 as Lab's white and no chromatic adaptation, so the numbers describe what the glass shows: the bytes decode by
    P3's transfer curve (the sRGB curve), go to XYZ by the matrix of P3's primaries (0.680, 0.320), (0.265, 0.690),
    (0.150, 0.060) over D65 (0.3127, 0.3290), and to Lab by CIE's exact constants (`src/colour.h`). White #FFFFFF is
    L 100 C 0; the primaries read #FF0000 L 54.97 C 133.55 h 45.21, #00FF00 L 86.59 C 157.75 h 136.95 (P3's most
    chromatic colour, so the C track's 0–160 holds the whole gamut), #0000FF L 33.83 C 138.06 h 306.29. THE SAME HEX
    READS OTHER LCh NUMBERS IN GIMP ON THE LAPTOP: GIMP's LCh is babl's, D50-adapted, over the image's space (sRGB
    there).
  - OUT OF GAMUT (LCh alone can leave it; a colour is inside when every channel's own rounding to a byte is a byte,
    so it reaches the bytes with no clipping): a track paints its out-of-gamut stretch in a flat neutral, the panel's
    ground #191919 (a gap in the track; no alpha, no hatching), and a drag, a tap or a − / + whose value lies outside
    STOPS AT THE LAST IN-GAMUT VALUE ON THE WAY THERE along that axis: a walk from the current value toward the target
    in steps of 0.01 unit to the first value outside, then bisection to 1e-9 unit (a stretch outside narrower than
    0.01 unit can be stepped over; a stop within 1e-6 unit of where the view stands moves nothing, so − / + against the
    edge is still). A value inside the gamut is reached directly, across an out-of-gamut stretch if the pen goes there.
    No colour is ever clipped: an LCh view always gives its bytes by rounding alone.
- THE VIEW HE DIALLED IS PART OF THE PICK (architect 2026-10-04). A saved pick keeps, beside its bytes, the exact
  view it was saved under — the model shown when the colour last changed and its three numbers — and every road back
  to a stored colour — the launch, BACK / FORWARD, OLD, leaving the app with the panel open, a preset's load —
  restores that view instead of re-deriving it from the bytes when the panel shows that model; a panel showing another
  model re-derives its view from the bytes (for a grey, an HSV view keeps a stored HSL view's hue and the other way
  round), and switching to the stored model shows the stored view exactly. Re-derived, the view is the
  bytes' own (S 0.3529 where he dialled 0.35): the number reads 35.3 where he dialled 35.0 and the handle moves, and the next − / +
  rounds from the re-derived values, while at low saturation or value one byte is several degrees of hue or a percent
  of saturation, so an axis he never touched would move. Re-derivation from bytes stays only where the bytes are the
  input: the R / G / B tracks and their − / +, a theme's swatch, and a pick saved before views were stored. The ring,
  the triangle and the model's tracks and − / + set the view directly. The bytes stay the colour's truth (what is
  painted, what the product takes); the view always gives them.
- THE CHOOSER: a tap on the element's name (at the lift, as every panel control) opens a vertical list of the
  elements in manifest order over the panel's right column, the active one marked by a white square. ITS FIT
  (2026-10-04: room for the elements to come, the invalid flags among them): its rows keep the pop-up's and the model list's 76 px and run down
  from under the button (panel y 108) over the slider rows as far as the panel's pad, PAINTED OVER EVERYTHING THEY COVER
  — the fields, tracks and handles go under its field, and the panel's words (the hex, the count, Old and New, a
  readout) are clipped to outside it — so ten entries end at y 868, fourteen at 1172 and fifteen, the most, at 1248
  (`panel::kChooserMax`; an export with more is refused at the launch with a message). A tap on an
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
  export's `themes.json`, below), each by its CATALOG KEY (the name he types in the app's Settings, the family prefix
  grouping them by provenance; architect 2026-10-04) with a swatch of its ground. A scroll frame paints the list and
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
  height (the panel itself unchanged): the theme's key with its display title small under it, a close control (an X, top right), and EVERY COLOUR THE
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
| `picks.txt` | the app, at every commit | one line appended: `<ISO-8601 local time> <key> #RRGGBB <model> <a> <b> <c>` (the element's key; the view the pick was saved under: its model's word, `hsv`, `hsl` or `lch`, and its three numbers) |
| `state.json` | the app, at every close of the panel, every switch of element or model, a commit at the presets pop-up's opening, a preset load, and the theme strip's opening and close | `{"active": "<key>", "model": "hsv", "colours": {"<key>": "#RRGGBB", ...}, "hsv": {"<key>": [h, s, v], ...}, "hsl": {"<key>": [h, s, l], ...}, "lch": {"<key>": [L, C, h], ...}, "entry": {"<key>": N, ...}, "theme": "<theme key>"}`: the active element, the model the panel shows, every element's colour, its view in the map of the view's model (a map written only when some view is in it) and its history cursor (the N of "N of M"; absent with an empty history), and the open theme strip's theme (absent when none), rewritten whole. It is always THE LAST SAVED STATE: while the panel is open the active element is written as the panel's opening (OLD and its cursor), so an unsaved edit never reaches it |
| `presets.json` | the app, at every save of a preset | `{"presets": [{"number": N, "saved": "<ISO-8601 local time>", "colours": {"<key>": "#RRGGBB", ...}, "hsv": {"<key>": [h, s, v], ...}, "hsl": {...}, "lch": {...}}, ...]}`: the presets oldest first, the numbers ascending (the name is "Preset N"), every colour with its view in its model's map (each map only when some view is in it, so an all-HSV preset is written as before the model switch), the maps together over the colours' keys, rewritten whole |
| `tools/palette/picker/presets/presets.json` (the repository) | the planner, each session (`adb pull`) | the tablet's `presets.json` copied verbatim into git, so his presets outlive the device and every planner (the cloud's too) can read them; the source of the product's preset themes (`tools/theme_catalog/build.py` preset_entries, architect 2026-10-04) and of their device-config lines (`tools/theme_catalog/preset_keys.py`); the tablet's `picks.txt` and `state.json` copied beside it, and the laptop check loads all three over each new export (Build, the laptop check) |

THE VIEW'S NUMBERS are, for HSV, h in degrees 0..360 and s, v in 0..1; for HSL h in degrees, s, l in 0..1; for LCh L
0..100, C 0..160, h in degrees — written as the shortest decimal that reads back as the
same double (`227`, `0.35`, `0.34671532846715331` after a drag, `2e+02` for 200), so a stored view is restored
exactly; a view must give its hex (`src/colour.h` `rgb_of_view`; an LCh view inside the gamut), and one that does not,
or lies outside those ranges, fails the load, as does an element with views in two maps or a `"model"` that is none of
the three. Earlier files still read, the HSV form being today's HSV view unchanged: a `state.json` without `"model"`
shows HSV; a `picks.txt` line without a view, `<ISO-8601 local time> <key> #RRGGBB`, is a pick whose view is re-derived
from its bytes as before (the lines are never rewritten; a new commit appends the new form after them), and a
`state.json` without a view map starts on its colour re-derived; one with `"hsv"` and `"entry"` for one layer alone and
no `"active"` (the single-layer builds', such as the ink round's `{"colours": {"ink": "#A6B9DE"}, "entry": {"ink":
58}}`) starts every other element at the manifest's colour, on the manifest's active element. A key that is no element
of the export (an earlier round's layer) is read and never used; so is a `"theme"` the export does not list, and a
preset's key that is no element. An element a file does not name (the flags in every file written before the flags
round) starts at the manifest's colour with an empty history, and a preset that does not name it (one saved with three
elements) leaves it as it is at a load. `presets.json` is read as strictly as the
others: a number not above the one before, a colour without its view, or a view not giving its hex fails the load.

At launch EVERY ELEMENT starts at its colour in `state.json` if there is one (the last close, with its view when
the file has one), else the manifest's; its history is its `picks.txt` lines, and its cursor `state.json`'s entry when
that entry lies in the history and holds the start, else the newest entry holding it, else the end. The active
element is `state.json`'s when it names one, else the manifest's. An entry HOLDS the
start when their bytes are equal and, if both carry a view, their views are too (the first build's `state.json`,
`{"<layer>": "#RRGGBB", ...}` with no entry, still reads; so does a kept `state.json` beside a deleted `picks.txt`).
Every commit also logs one line under the tag `warptempo_picker`: `picker: commit <key> #RRGGBB`; every history step
`picker: step <key> N of M #RRGGBB`; every switch `picker: element <key> #RRGGBB N of M`; every model chosen
`picker: model <HSV | HSL | LCh>`. A missing or malformed scene, `state.json` or `picks.txt`
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
  ...]}`: every entry of the product's theme table, LIGHT level, in the table's order (100 on 2026-10-04, the
  architect's presets `warptempo-preset-<n>` last), each with every distinct colour the catalog records for it, the
  ground first (`render.py` product_themes is the authoritative
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
THE CHECK THEME (`build/check/multi/`: the round's theme plus test elements filling the chooser to fourteen entries —
the icons' inks over the waveform scene, the invalid face over a scene of its own with two flags invalid), and renders
their references with the mock tool's own code. It
checks: every scene of both at the manifest's colours and at every colour set of `render.picker_check_sets` (each
element moved, each other element to a probe colour of its own, the chrome to the tint #206048 whose 32 and 96 hit the rule's half-to-even ties and to the bright
#E6D2B4 whose lines cap, all moved at once) equals the mock tool's render BYTE FOR BYTE, painted whole and reached by
the live repaint; a synthetic export with roles derived over the canvas element and over a literal equals
`colour.py`'s composition at the canvas and the ink moved; the C++ `lin_mix` equals `colour.py`'s on all 65536 byte
pairs; THE BLEND equals cairo's solid source through an A8 mask on every (source, frame, coverage) byte triple, each
channel (16,777,216 cases); `scale_byte` is half to even and capped; HSV keeps the hue through grey and black and
bytes -> HSV -> bytes is the identity over the whole cube; THE MODELS (bytes -> HSL -> bytes and bytes -> LCh -> bytes
the identity over the whole cube, every LCh inside the gamut, the most chromatic triple #00FF00 at C 157.75 inside the
C track; white L 100 C 0, black L 0; HSL's and LCh's retention; HSL against Python's own `colorsys` and LCh against
`check_refs.py`'s independent numpy over Display-P3 (D65) on 20264 byte triples, to 1e-9, the P3 primaries and
secondaries printed; THE MODEL SWITCH's sessions: the list's tap rules, HSL dialled by its tracks and − / + and its
view committed (`hsl` in picks.txt), restored exactly by a relaunch, BACK and FORWARD, the switch to HSV and back
leaving it untouched, the triangle keeping its hue, OLD; LCh dialled, A DRAG ON C TO THE TRACK'S END STOPPING AT THE
GAMUT (inside, C + 1e-8 outside, the bytes unclipped), C + at the edge moving nothing, the out-of-gamut stretch
painted in the neutral, a drag on h across the gamut's edges inside at every frame, its hue through C 0, its view
committed and restored by a relaunch, BACK and FORWARD; an HSL pick met by BACK in LCh and shown exactly in HSL; the
triangle in LCh; a preset over three models restored exactly; a grey saved in HSV keeping its hue in HSL; the new
forms' refusals); the readouts show one decimal rounded to nearest
(247.27 -> 247.3, 0.4714 -> 47.1, 0.99999 -> 100.0) and the widest, measured in the mono face, fits its field; the scripted sessions on the active element (the panel,
the pick history, the HSV he dialled: as before); THE CHOOSER (it opens; ITS FIT: fourteen entries inside the panel's
pad and the screen, its rectangle the same picture at two colours while the hex, the swatch and the readouts, tracks
and handles of the slider rows under it changed; a tap outside it closes it alone, writing
nothing; a press lifted on another entry picks nothing; the active entry again is a no-op; the edited chrome
committed by the switch to the Ink; the switch Ink -> Unselected Flag showing the flags scene with both faces, the
chrome's tint and the ink's colour live in it, the unselected face repainting alone; the switches on to the Selected
Flag, the Selection's scene and the chooser's last entry, over the slider rows, each committing the element left); THE PLAYHEAD (architect 2026-10-04: the panel
on the right, the playhead left of it; the switch to the Playhead Head showing the playhead scene, the head, its
outline the Label's #FFFFFF and the stem at the manifest's colours, in the picture and the frame; the head repainting
alone, its outline and the stem staying; its edit committed by the switch to the Playhead Stem; the stem repainting
alone; its edit committed by the close; both kept by a relaunch with their views, every other element untouched);
THE SWITCH'S PICTURE (the round's own export launched at the colours of its all-moved reference: the waveform scene,
the switch Ink -> Unselected Flag and the switch back, and the switch Ink -> Playhead Head each equal the mock tool's
render byte for byte, the switch on to the Label the label scene's and on to the Selection the open_flag scene's); THE
LABEL (architect 2026-10-04: its role the element, the flag labels fixed #FFFFFF; the panel on the right, the switch to the Label showing
the label scene, a word's solid pixel repainting and the antialiased edges re-blending over the chrome at one byte's
move; committed by the close and kept by a relaunch; the playhead head's outline then the new label, the flag labels
still white); THE OPEN FLAG (the open-flag round, architect 2026-10-04; both roles their elements, the editor's frame fixed black; the panel
on the left, the open flag right of it and of the strip's column; the switch to the Selection showing the open_flag
scene, the band repainting and the selected text's antialiased edges re-blending over it, the text, the face and the
frame staying; committed by the switch to the Selected Text, whose glyphs and edges then repaint, the band staying;
committed by the close; both kept by a relaunch with their views, every other element untouched); every element's history, cursor and view independent across switches and relaunches, and the element he
left returned to; today's `picks.txt` (58 ink lines, 15 of the first build's form and 43 with a view, the last
#A6B9DE) and `state.json` (`{"colours": {"ink": "#A6B9DE"}, "entry": {"ink": 58}}`) loading unchanged; THE PRESETS
(the export's themes, their count the data's, the program's own family last with its presets after `warptempo`; two
saves named Preset 1 and Preset 2, the edited panel colour committed by the pop-up's
opening; a load giving each changed element one pick and an unchanged one none, OLD the loaded colour, BACK returning;
a relaunch keeping the presets and their views; a drag that scrolls and acts on nothing; a tap outside that changes
nothing; a malformed `presets.json` refused; a three-element preset, saved before the flags round, loading and
committing the chrome, canvas and ink while both flags, both playhead elements, the Label and the selected pair stay
as they were, no pick for any); THE THEME STRIP (Windows 95 Standard opened, its #C0C0C0 adopted into
the chrome as an edit giving its own quartet #FFFFFF / #DFDFDF / #808080 / #000000 exactly, OLD reverting it, the
switch to the canvas committing it with the strip still open, another swatch adopted into the canvas, the strip
surviving a relaunch; Brick's ground giving the rule's lines and not its own; CDE Alpine's strip scrolled by a drag
that adopts nothing; the close control, `state.json` never holding an unsaved adoption); THE TABLET'S FILES OF
2026-10-04 (`check_data/tablet_2026-10-04/`: 110 `picks.txt` lines over three elements and its `state.json`) loading
unchanged, every view exact, HSV shown, the flags, the playhead, the Label and the selected pair at the manifest's colours with empty histories, and
a preset saved and loaded over them appending nothing; the repository's `presets/presets.json` reading unchanged;
those of the presets build's install (`check_data/tablet_2026-10-04_presets/`, 136 lines, the ink active, chrome 12
of 12, canvas 34 of 34, ink 90 of 90) loading unchanged the same way; THE REPOSITORY'S COPY OF THE TABLET'S FILES
(`presets/`'s `picks.txt`, `state.json` and `presets.json`, whatever the latest copy holds; architect 2026-10-04, the
playhead round's install) loading over the round's own export: every element `state.json` names at its colour, its
model, active element and open theme, every element the files never name (the playhead's two, the Label, the selected pair) at the manifest's
colour with an empty history, and each of his presets loaded over them setting the elements it names and leaving
every other as it was, with no pick; and THE PER-FRAME COST
of a pen drag on the ink, the chrome and the unselected flag, and on the chrome's h track in LCh (printed). Its frames are written as PNGs in
`build/check/work/` for the eye (`frame_chooser_open.png`, `frame_chrome_tint{,_open}.png`,
`frame_{unselected,selected}_flag_open.png` (the flags scene, the panel on the left),
`frame_playhead_{head,stem}_open.png` (the playhead scene, the panel on the right), `frame_label_open.png` (the label
scene, the panel on the right), `frame_selection_open.png` and `frame_selected_text_open.png` (the open_flag scene, the
panel on the left), `frame_selection_switch_open.png`, `frame_switch_flags_moved.png`, `frame_switch_open_flag_moved.png`,
the history's `frame_history_*.png`, `frame_presets_open.png`, `frame_presets_themes.png` (the pop-up scrolled into
the themes), `frame_strip_{open,adopted,scrolled}.png`, `frame_model_list_open.png`, `frame_model_hsl_open.png` and
`frame_model_lch_open.png` (the C track's out-of-gamut stretch and h's).

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
own history, cursor and view across switches and relaunches.

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
| `src/colour.h` | the byte triple, hex, the frame's pixel word, `lin_mix` (colour.py's, step for step), `scale_byte` (the chrome rule), `over_n_8` (pixman's blend, line for line), THE MODELS: HSV, HSL and LCh over Display-P3 (D65) to and from the bytes, the gamut test |
| `src/json.{h,cpp}` | the tiny JSON reader (manifest.json, state.json) |
| `src/scene.{h,cpp}` | the export: load and validate (`themes.json` too), the roles' rules, the scene's runs and stacks, the whole paint and the live repaint; the Pick (bytes and view); the readers of state.json, picks.txt and presets.json |
| `src/fonts.{h,cpp}` | Roboto and Roboto Mono from memory, as `src/gui/gui_font_bundled.cpp` builds them (SLIGHT hinting) |
| `src/picker.{h,cpp}` | the picker: ColourState (the bytes plus every model's view with its retained hue, a stored view restored, the gamut stop), the model switch, the launch state (`picker_load`), every element's state and pick history, the chooser, the presets pop-up, the theme strip, the panel's geometry, painting, touch, the close's save |
| `src/main_android.cpp` | the glue's lifecycle, the window set-up, one-pointer touch, the R<->B blit |
| `src/host_check.cpp`, `check_refs.py`, `check_data/` | the laptop check (above; `check_refs.py` also writes the models' independent reference); `check_data/tablet_2026-10-04/` the tablet's `picks.txt` and `state.json` of that morning, `check_data/tablet_2026-10-04_presets/` those at the presets build's install, copied verbatim; `presets/` (the latest copy) read too |
| `java/com/warptempo/picker/PickerActivity.java` | the full-screen sliver |
| `AndroidManifest.xml` | the package, the colour mode, the orientation lock |
| `build_picker.sh` | the APK, or `--check` |
