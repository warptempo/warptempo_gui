# BRIEF: tmp/palette/mocktool — a from-scratch mock-up renderer of the warptempo_gui screen (2026-10-02)

You are an implementation subagent for warptempo_gui (read CLAUDE.md first). This brief builds a TOOL under
`tmp/palette/mocktool/` (gitignored tmp/). You edit NOTHING outside tmp/palette/mocktool/ (read anything you like);
no product source, no docs, no git except read-only (status/log/show/diff), NO git network commands, NEVER
`scripts/warptempo_sync` in any form, NO adb / no touching the tablet (the planner does the glass). Python 3 with
numpy and pycairo 1.29 and ImageMagick `magick` are available; PIL and freetype-py are NOT installed. Scratch goes
under tmp/ only. Never put a shell variable in an `rm -rf` path.

## Why
The architect judges palette and chrome designs as PNG mock-ups viewed full screen in Samsung Gallery on his
Galaxy Tab S10 FE (2304x1440, gui_scale 200 %: one logical px = 2 device px). Until now every mock was a
RECOLOURING of a screencap. He now wants a renderer that DRAWS the whole screen from scratch, so a theme can change
colours, Windows-95-style relief (bevelled buttons, raised bars, sunken wells), icon sets and the waveform ink
freely and still produce a realistic full-screen picture. His words: "a rough recreation of Y1b exact — doesn't
have to have every last thing, but as thorough as possible, so I can get a full realistic impression on the
tablet". The design direction it will serve: late-90s / early-2000s classic chrome — straight lines, 90-degree
corners, Win95 fake-3D relief, NO gradients, NO rounded corners, dark neutral greys; the waveform and ink must
work with the chrome as a package.

## Pixel pipeline facts (measured; do not re-derive)
- The app's Android window is a Display-P3-tagged layer; the panel receives the app's bytes as P3 coordinates. A
  mock PNG that carries the palm screenshot's iCCP chunk and NOTHING else is shown byte for byte by the Gallery.
  So: THE BYTES YOU WRITE ARE WHAT THE GLASS SHOWS, and they are also the future constexpr constants. No colour
  conversion anywhere in the renderer. Write PNGs ONLY with `tmp/palette/pngrw.py`'s `write_png(path, arr,
  [(b'iCCP', chunk_from_png('tmp/from_tablet/palm_0354.png', b'iCCP'))])` (it writes IHDR + iCCP + IDAT + IEND).
  Read PNGs with `pngrw.read_rgb` (exact bytes via magick).
- A colour borrowed from an sRGB source (a theme file, a scheme table, an icon bitmap) is what that source would
  look like on the glass only after ONE sRGB->P3 pass: `derive.srgb_to_p3` in `tmp/palette/derive.py` (import
  it the way `tmp/palette/mockset_j.py` does — derive runs code at import; redirect stdout and fake argv). Provide a
  theme-file switch per colour: a hex written `"#303030"` is taken as P3 bytes as-is; written `"srgb:#303030"` it
  goes through srgb_to_p3 once. Icon bitmaps from the retro sets are sRGB art: convert their pixels once.
- Reference pictures: `tmp/tablet_base_s4.png` is the app's own screencap at palette_passes 0 (its bytes ARE the
  constants; the lane geometry, text positions, icon glyphs, waveform, flags and the trim bar are all exact there).
  `tmp/palette/set_s3/mock_A_y1b_exact.png` is "Y1b exact": the same picture with every pixel through the
  platform's pass twice (`tmp/palette/platform_map.json`: 73 constants + 985 colours, keys `const`, `one_pass`,
  `two_pass`). It carries a small file-name label near the bottom-left clock (rows ~1382..1407, x from 300): exclude
  that box from any comparison. `tmp/palette/set_j/mock_J4_classic_neutral.png` is the architect's current pick
  (the chrome greys made neutral and lifted: ground (48,48,48), row ground (55,55,55), trim bar (64,64,64),
  selected fill (74,74,74), popup (38,38,38); ink, flags, canvas as Y1b).
- The Y1b colour of every role = `two_pass` of the constant in platform_map.json (names as in src/gui/render.h's
  palette block). The ink is (150,191,218), the flags (138,94,172) with edge (77,52,95) and black labels, the
  canvas (20,22,24), the ground (33,35,38), the row ground (42,44,48), labels (252,252,252), ruler labels
  (194,194,194), ticks (115,115,115), the line (84,86,89), the tab/field line (76,78,81), the trim bar (47,49,53)
  with bevels hi (60,63,67) / lo (40,42,45), the lane ground bevels (59,62,66) / (19,21,22), end caps
  (163,169,175) hi (170,176,182) lo (160,165,171), bottom border (19,21,22), playhead head (142,143,145), stem
  (252,252,252), disabled icons = kRedesignDisabledMix (0.322 of the label colour kept over the face — read
  `redesign_button_enabled` / the disabled-mix site in src/gui/paint_handler.cpp for the exact blend).
- The waveform outline colour is the 50 % linear-light blend of ink over canvas (`derive.lin_mix`); recompute it
  from the theme's ink and canvas, never store it.

## What to build (all under tmp/palette/mocktool/)
1. `extract.py` — run once against `tmp/tablet_base_s4.png`; writes `scene_s4.json` and `glyphs/`:
   - LANE GEOMETRY in device px: the menu row, the icon row (button boxes: find every 64x64 box by scanning;
     group separators = the 2-px (76,78,81) columns; the three view icons flush right, the toggled one with its
     selected-fill face (60,63,65)), the ruler lane (29 logical rows), the flag lane, the trim lane (bar extents,
     the two end handles, the centre grip, lane bevels), the well's black border rows/cols, the canvas, the
     bottom row (the clock cell, the transport groups 6|4|4|3 with separators), the legend at the top right. Cross-
     check the roster against `src/gui/paint_handler.cpp` (`kIconRowButtons`, `kIconRowViewGroup`, the bottom
     row's `kMarkerVerbGroup | kTransportWalkGroup | kTransportArrowGroup | kTransportGroup`) and
     `src/gui/main.cpp`'s lane table; record both the measured and the table values and flag any mismatch.
   - TEXT: the menu words (File, Edit disabled, Settings), the legend ("100% ↑ | 5:39 AM"), every ruler label
     and its x (0:45.625 … at 125 ms steps), every flag's x and label text (read them off the picture:
     "1.27+0.00:b.33", "b.33" …), the clock cell ("B | 00:45.750"). Also the tick positions (minor ticks every
     1/8 of a label step; measure).
   - THE WAVEFORM as per-column runs: for every canvas column, the vertical runs of ink pixels and of outline
     pixels per channel (classes: ink, outline). Columns under a stem or the playhead: take the runs from the
     neighbouring column (or record them as unknown and let the renderer repeat the neighbour). Store compactly.
   - THE APP'S GLYPHS as alpha masks: every icon-row and bottom-row glyph recovered from the capture by alpha =
     (p − face)/(label − face) per channel (face = the ground under that box, label = (252,252,252); for the
     disabled ones the dimmed glyph: recover the alpha against the disabled mix, or record them as disabled and
     recover against the mix's effective colour). Save as 8-bit PGM/PNG masks keyed by role name (from the roster
     order in paint_handler.cpp), plus the flag-lane and playhead shapes if they are not trivially drawable.
2. `render.py <theme.json> <out.png> [--label]` — pycairo draws the whole 2304x1440 picture into an RGB24 image
   surface, numpy takes the bytes, pngrw writes them with the iCCP chunk. Deterministic. `--label` stamps the
   output's file name like `tmp/palette/label.py` (Roboto 20 px, (140,140,140), at x 300 y 1382 on the bottom
   row's ground).
   - TEXT through cairo + FreeType on the repository's faces `fonts/Roboto-Regular.ttf` and
     `fonts/RobotoMono-Regular.ttf`: write a `fonts.conf` in the tool dir listing `fonts/` and set
     FONTCONFIG_FILE before importing cairo so `select_font_face("Roboto")` resolves to the file (verify it did:
     a fallback face is a failure). Font options: antialias GRAY, hint style SLIGHT, hint metrics ON (the app's
     choice, `src/gui/gui_font.h`). Sizes: 12 pt x 2 = 32 px for chrome text; the clock cell in Roboto Mono; the
     ruler labels at their own measured size — measure cap heights in the capture and match them. Baselines: the
     app centres the cap band in the row (`redesign_baseline`); match the capture's rows.
   - THEME SCHEMA (JSON; document it in README.md), every value optional with the Y1b default:
     colours for every role above (plus `icon_face`, `button_face`, `bevel_hilight`, `bevel_light`,
     `bevel_shadow`, `bevel_dkshadow`, `well_border`, `legend`, `menu_disabled`); `relief`: "flat" (today) |
     "thin" (one line a side) | "thick" (Windows DrawEdge two lines a side); `buttons`: whether icon-row and
     transport boxes get a raised face (flat today), and which ones are "down" (the toggled view icon = sunken);
     `trim`: the bar raised (its two bevels today) with end handles and centre grip, `well`: sunken or bordered;
     `clock_panel`: flat or sunken; `separators`: "line" | "etched"; `icons`: "app" | "se98" | "chicago95" |
     "kdeclassic" with the role→file tables and the dark-face recolour rule from
     `tmp/research/retro/mock95icons/README.md` and `render95icons.py` (sets under `tmp/keep/retro/sets/`; 16-px
     art is drawn at exactly 2x nearest-neighbour = 32 device px, centred in the box; roles with no file in the
     set keep the app glyph); `waveform`: ink, canvas, outline auto; `flags`: fill, edge, label colour.
     The DrawEdge grammar (which line takes which colour per edge type, raised/sunken/etched, BF_SOFT for push
     buttons) is quoted from source in `tmp/research/retro/landscape/README.md` §3.1; `tmp/research/retro/mock95/
     render95.py` is a working implementation over a screencap (its geometry is from an OLDER base; do not reuse
     its coordinates, reuse its edge logic). One logical line = 2 device px.
   - Draw order and fidelity: everything the capture shows, in place: menu row + legend; icon row (26 + 3 view
     icons, separators, disabled states, the toggled view icon); ruler (labels, major and minor ticks); flag lane
     (flags with their black labels, Y1b fills/edges, stems down through the canvas); trim lane (lane ground
     bevels, the bar, its bevels, end handles, centre grip, bottom border); the well (border, canvas, the two
     channels from the runs, outline class in the outline colour); the playhead (stem + head); bottom row (clock
     cell, four transport groups with separators, disabled states). Anti-aliasing: cairo's default is fine for
     text and glyph masks; rectangles must land on whole device pixels (no half-pixel smears).
3. THEMES shipped in `themes/`: `y1b_exact.json` (every default: the recreation of mock A), `j4_flat.json` (the
   J4 greys, flat, app icons), `j4_thin95.json` (J4 greys + thin relief on icon-row and transport buttons, raised
   trim bar, sunken well and clock, etched separators, app icons), `j4_thin95_se98.json` (the same with SE98
   icons recoloured by the dark-face rule), `j4_thick95_chicago.json` (thick relief, Chicago95 icons). Render all
   five to `tmp/palette/mocktool/out/mock_<theme>.png` with `--label`.
4. `compare.py <render.png> <reference.png>` — pixel mismatch overall and per lane (menu, icon row, ruler, flags,
   trim, well, bottom row), excluding the label box; writes a 50 % side-by-side `diff_<name>.png`. Run it for
   y1b_exact vs mock_A and j4_flat vs mock_J4 and REPORT THE NUMBERS honestly. Target: text and glyphs within a
   pixel of position, lanes and colours exact, the waveform runs exact; antialiasing differences are acceptable.
5. `README.md` in the tool dir: usage, the theme schema with every key and default, the measured geometry table,
   what is approximated, how to add an icon set, how to add a theme. A fresh session with no memory of this one
   must be able to render a new theme from the README alone.

## Report back (in your final message)
The files written; the compare numbers per lane for the two parity checks; every approximation and every
roster/geometry mismatch you found; anything in the theme schema you could not implement. No tablet work.
