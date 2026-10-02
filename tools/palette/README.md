# tools/palette — the tablet screen rendered from a theme

Draws the whole 2304x1440 tablet screen (Galaxy Tab S10 FE, gui_scale 200 %: one logical px = 2 device px)
from a measured SCENE plus a JSON THEME, so a theme can change every colour, add Windows-95 relief and reshape the
ruler, trim and marker lanes, and still produce a full realistic picture. It is how the chrome's design was judged
on the glass before the app painted it: `themes/frozen.json` is the design the app paints since 2026-10-02, the
end of the mock-up lineage J4 → K4 → L2/M1 → N2 → O2 → P6/T4/U4 → R3 → S4 → V3 judged on the tablet. A
standalone utility with no link path from any product target and no CMake. Every path resolves from the scripts'
own location, so the commands run from the repository root (as written here) or from `tools/palette/` alike.

```
python3 tools/palette/render.py  tools/palette/themes/<theme>.json <out.png> [--scene <tag>] [--label]
python3 tools/palette/compare.py <render.png> <reference.png>      [--scene <tag>] [--diff out.png]
python3 tools/palette/extract.py <dir>/tablet_base_<tag>.png --first-label M:SS.mmm --flags T1,T2,... \
        --clock '<clock text>' --legend '<legend text>' [--rev <commit>] [--step 125] [--out-dir DIR]
```

- `--scene <tag>` picks `scene_<tag>.json` (+ its `waveform_<tag>.json` and `glyphs/<tag>/`). The default is
  `1002`; `compare.py` takes the same tag for its lane rows, so compare a render against a picture of the same scene.
- `--label` stamps the output's file name (no extension) in Roboto 20 px, `stamp` (140,140,140), its box's top-left
  at x 300, y 1382 on the bottom row's ground (the scene's `label_box`, which `compare.py` excludes).
- The frozen design on the default scene: `python3 tools/palette/render.py tools/palette/themes/frozen.json
  tools/palette/out/frozen_1002.png` (`out/` is the conventional, git-ignored home of renders; the renderer makes
  the output's directory when it is missing).
- Rendering is deterministic (same theme + scene -> same bytes) and takes about half a second.

## Dependencies

- Python 3, numpy, pycairo >= 1.29 (cairo with its FreeType and fontconfig backends).
- The repository's `fonts/` (Roboto, Roboto Mono): the tool's private `fonts.conf` lists only that directory.
- HarfBuzz through ctypes: `libharfbuzz.so.0`, loaded when `common.py` is imported. When it is absent the tool exits
  at import with one line naming the library; there is no fallback shaper, because the text must be shaped exactly as
  the app shapes it. `verify_fonts()` also loads `libfontconfig.so.1` (the library cairo itself resolves faces with).
- No ImageMagick and no PIL: `pngrw.py` reads and writes PNG with zlib and numpy alone. Its reader takes 8-bit,
  non-interlaced grey, grey + alpha, RGB and RGBA PNGs (every PNG the tool reads is a tablet screencap or its own
  output), undoes the five row filters, drops alpha and applies no colour profile. It replaced an ImageMagick decode
  (`magick ... -depth 8 -alpha off rgb:-`) on the evidence that both return identical bytes on the three scene
  captures, the Gallery screenshots, the tool's own renders and synthetic PNGs of every filter type in RGB and RGBA.

## Scenes

A scene is ONE state of the app measured off its own screencap, taken while the app painted its constants
unaltered (the palette knob of that day at 0), so every byte is the app's own. The scenes ship as derived data
(`scene_<tag>.json`, `waveform_<tag>.json`, `glyphs/<tag>/`), so the tool renders without any capture; the
captures themselves do not ship: they are the tablet screencaps `tmp/tablet_base_<tag>.png` in the laptop's
checkout (git-ignored), and `extract.py` re-derives a scene from its capture byte for byte (below). All three show the
app's chrome as it stood before the frozen design: the scenes carry geometry, text, glyph coverage and the
waveform, and a theme supplies the look.

| tag | capture | state | use |
|---|---|---|---|
| `1002` (default) | 2026-10-02 07:01, the APK built at a6f53163 | the lane stack of a6f53163 (ruler 56 rows); all three trim caps on screen; no flag selected; the playhead at x 293 clear of every flag | the default; every theme is judged on it |
| `1002a` | 2026-10-02 06:57, same APK, same zoom and scroll | the trim bar runs past both screen edges (no handles, the grip at x 275); the first flag SELECTED with the playhead standing on it (its stem suppressed); Edit enabled | proves the selected-flag, suppressed-stem and capless-trim roads |
| `s4` | 2026-10-02 05:39, an APK before 94403846 and a6f53163 | the OLD ruler lane (58 rows, labels seated 2 rows lower) | the older lane stack only; never a base for a new picture |

`scene_s4.json` was written by the earlier, s4-only extractor and is kept as it was (its glyphs moved to
`glyphs/s4/`, its waveform note re-worded as below); the renderer reads it unchanged. Re-measuring the s4 capture
with this extractor at `--rev a6f53163` (into a scratch `--out-dir`) fails exactly the ruler's cross-checks, because that capture
predates the rule: the lane is 58 rows against the rule's 56, the label ink 182..203 against the rule's seat
180..201, and so the ruler-label text check. Everything else comes out identical to `scene_s4.json` (glyph masks,
waveform runs, buttons, menu, clock, flags, playhead, ticks), the trim bar recorded in today's painted-extent form
(593..1578 with its 2-px relief, where the old scene has the visible stretch 611..1560).

### Extracting a scene

`extract.py` takes the base and the texts the pixels cannot name; there are no scene defaults. The tag is the base
name's suffix (`tablet_base_<tag>.png`), and it writes `scene_<tag>.json`, `waveform_<tag>.json` and `glyphs/<tag>/`
(PGM masks + `index.json`) into `tools/palette/`, or under `--out-dir`. The scene records the capture's path
relative to the repository root (`source`), so a byte-identical re-extraction reads the capture from the same path.
The command lines of the three scenes (the captures at the paths named above):

```
python3 tools/palette/extract.py tmp/tablet_base_1002.png  --rev a6f53163 --first-label 0:45.500 \
        --flags '1.27+0.00:b.33,b.33,b.33,b.33,b.33' --clock 'B | 00:45.621' --legend '100% ↑ | 7:01 AM'
python3 tools/palette/extract.py tmp/tablet_base_1002a.png --rev a6f53163 --first-label 0:45.500 \
        --flags '1.27+0.00:b.33,b.33,b.33,b.33,b.33' --clock 'B | 00:45.680' --legend '100% ↑ | 6:57 AM'
# s4 (as measured by the earlier extractor; do not re-run into tools/palette/):
#       tmp/tablet_base_s4.png --first-label 0:45.625 --flags '1.27+0.00:b.33,b.33,b.33,b.33,b.33' \
#       --clock 'B | 00:45.750' --legend '100% ↑ | 5:39 AM'
```

- `--rev`: the git revision whose source the measurements are checked against (read with `git show`), the commit
  the capture's APK was built from; without it, the working tree. THE MEASUREMENT MODEL IS THE APP BEFORE THE FROZEN
  DESIGN: the extractor parses constants the frozen design retired (kRedesignContentGround, kPlayheadHeadAlpha,
  kRulerHeadGroundPx and their kin) and expects the flat chrome they paint, so it extracts the captures of the APK
  built at a6f53163 with `--rev a6f53163`, and a capture of today's app is beyond it.
- `--first-label`: the text of the leftmost label on screen (it stands on the leftmost measured major tick);
  `--step`: the ms between labels (125 at this zoom, 8 ticks per step).
- `--flags`: every visible flag's label, left to right, comma-separated, a flag clipped by the right edge included.
- `--clock`, `--legend`: the bottom row's clock and the menu row's battery/clock legend, verbatim.

What it measures, and the cross-check each measurement must pass (a failure prints `UNEXPLAINED:` and the run exits
1; a known, explained difference prints `NOTE:`; both are recorded under the scene's `mismatches`):

- Lanes: the trim lane's top (column 3's first non-ground row under the menu row), the well's black rows, the bottom
  row's border-top; the marker lane is its table height (20 x S) over the well and the ruler the rows between it
  and the trim lane. Checked against main.cpp's lane table and the ruler's own rule, `ruler_lane_h_px` (render.h):
  pad + ceil(ascent) + kRulerHeadGroundPx x S (floored at one row) + kPlayheadHeadHeightPx x S = 0 + 30 + 2 + 24 =
  56, the pad being `ruler_label_baseline_px`'s max(0, 4 x S - (ceil(ascent) - cap)) (paint_handler.cpp), each
  constant parsed from the source. The waveform area is checked against main.cpp's two tablet stacks.
- The ruler: the label seat is the app's rule (baseline = lane top + pad + ceil(ascent) = 202), checked against the
  capture's label ink (exactly rows baseline - cap .. baseline - 1, then empty ground down to the head); the major
  ticks (2 px, rising kRulerMajorRisePx x S) and the minors fitted to the app's own arithmetic (majors
  nearbyint((t - vp) / mpp), eight minors distributed across each segment); every measured tick must lie on the
  fit. A major under the playhead head is not seen and is recorded as hidden.
- The playhead: located by its HEAD (kPlayheadHead at kPlayheadHeadAlpha 0.8 over the ground, (120,121,124); the
  widest row spans col - 18 .. col + 19), every head row checked against kPlayheadHeadHalf. Its stem (kPlayheadStem,
  2 px) is the cross-check where it shows; where it does not, the head's column must carry a flag's stem: the app
  suppresses the playhead's WHOLE stem where a marker stands on its column (`playhead_stem_suppressed`,
  paint_handler.cpp: the marker's stem wins, the marker-lane run included), and the scene records `stem_suppressed`.
- Flags: each left border followed by an edge colour on the lane's top row; the fill names the state (kMarkerFlagFill
  or kMarkerFlagFillSel -> `selected`), the edge must match it, the width (pads + the shaped label) must meet the
  right border — or, for a flag whose box runs past the screen's right edge (`clipped`), its edge must run to the
  edge. Every flag's stem must stand at its x in its own fill colour, and the stems must be the only non-waveform
  columns in the canvas besides the playhead's.
- The trim lane (render.cpp's painter, back to front): the ground's bevel rows, the BAR (kTrimLaneBar under a
  trim_bar_edge_px relief: light top and left, dark bottom and right, the dark pair painted last), the HANDLES (cap
  surfaces flush at the bar's ends) and the CENTRE GRIP (a cap tile with a bar-coloured hollow), the lane's bottom
  border last. The scene records the bar's PAINTED extent: from the begin handle's column to the end handle's end,
  or, for a bound off screen, one column past that edge (render.cpp's bar_lo = -1 / bar_hi = lane_w + 1), derived
  from how many relief columns show there. A capture with no handles or no grip records them absent; with no ground
  visible, the ground's bevel rows are the caps' (one surface lambda paints both).
- The buttons: both rows' boxes are laid out from paint_handler.cpp's rosters and layout constants and checked
  against the separators and every ink pixel; each glyph's 8-bit coverage is recovered per ink and re-composited
  through cairo against the capture; enabled / disabled is read from the glyph (a disabled glyph never exceeds its
  disabled_mix), the menu words' from their ink (an enabled word carries kRedesignLabel at full coverage).
- The texts: the menu row (items + `--legend`), the ruler's label band and the clock are re-rendered through the
  app's road and compared byte for byte with the capture (0 differing pixels each on 1002 and 1002a).

## The pixel contract

THE BYTES WRITTEN ARE WHAT THE GLASS SHOWS. The app's Android window is a Display-P3 layer, so the panel takes
the app's bytes as P3 coordinates; a PNG carrying a Display-P3 iCCP chunk and nothing else is shown byte for byte
by Samsung Gallery. The renderer does NO colour conversion of its own: every PNG goes out through
`common.save_png` = `pngrw.write_png(path, arr, [(b'iCCP', <display_p3.iccp>)])` (chunks IHDR, iCCP, IDAT, IEND).
`display_p3.iccp` is that chunk's raw data, 299 bytes (the 'Skia' Display-P3 profile), taken from a Samsung Gallery
screenshot on the tablet, 2026-10-02. A theme hex is therefore a P3 byte triple and a constexpr constant as-is.
The only conversion is the one a theme asks for: a colour written `"srgb:#303030"` goes through
`colour.srgb_to_p3` once (a colour borrowed from an sRGB source).

## Files

| file | what |
|---|---|
| `common.py` | paths (all from the file's own location); the font road (writes `fonts.conf`, sets `FONTCONFIG_FILE` before cairo loads, `verify_fonts()`); HarfBuzz shaping through ctypes; `redesign_baseline` / `line_baseline`; colour parsing; `save_png` |
| `colour.py` | the one colour owner: `s2l` / `l2s`, `srgb_to_p3` (one sRGB -> Display-P3 pass in linear light), `lin_mix` (the linear-light blend), `two_pass` (the default look's rule, with the measured exceptions) |
| `pngrw.py` | PNG read (pure Python: zlib + the five filters) and write (IHDR, the extra chunks, IDAT, IEND) |
| `display_p3.iccp` | the Display-P3 iCCP chunk every written PNG carries (above) |
| `extract.py` | measures a capture `tablet_base_<tag>.png` into `scene_<tag>.json`, `waveform_<tag>.json`, `glyphs/<tag>/` and cross-checks every measurement against the source (at `--rev`) and the capture |
| `render.py` | the renderer (theme schema below) |
| `compare.py` | per-lane parity numbers (the scene's lanes) and a side-by-side `diff_<name>.png` |
| `scene_<tag>.json` | `tag`, `source`, the extraction `params`; lanes, the ruler rule's terms, button boxes (+ enabled / selected state), separators, the trim lane's rects, ruler ticks + labels, flags (+ selected / clipped), the playhead (+ stem_suppressed), clock, the app constants, the icon inks, the `glyphs` directory, the recorded `mismatches` (notes) |
| `waveform_<tag>.json` | per canvas column, the vertical runs `[y, len, y, len, ...]` (relative to the canvas top) of ink and of outline pixels |
| `glyphs/<tag>/` | `<row>_<Button>__<ink>.pgm`: each app glyph's 8-bit rasteriser coverage per ink (44x44 device px), `index.json` (icon, enabled, inks, files, under) |
| `themes/frozen.json` | THE theme: the design the app paints since 2026-10-02 |
| `out/` | renders and compare diffs (generated, git-ignored) |
| `fonts.conf`, `fccache/` | the tool's private fontconfig, listing ONLY the repository's `fonts/` (generated, git-ignored) |

## Text: the app's own road

Faces are the repository's `fonts/Roboto-Regular.ttf` (sans, 12pt x 2 = 32 px) and `fonts/RobotoMono-Regular.ttf`
(the clock, 11pt x 2 = 29.333 px). `common.py` writes `fonts.conf` (a `<dir>` for `fonts/` and a private cachedir)
and sets `FONTCONFIG_FILE` before `import cairo`, so `select_font_face("Roboto")` can only resolve to that file.
Font options as the app (`src/gui/gui_font.h`, `gui_font_bundled.cpp`): antialias GRAY, hint style SLIGHT, hint
metrics ON. Text is shaped like `text_shape::shape_text_run` (HarfBuzz on the same file at scale size*64: unhinted
advances, GPOS kerning; libharfbuzz through ctypes) and painted with `show_glyphs` at pen + offset.
`verify_fonts()` runs on every render and fails unless (1) fontconfig's own FcFontMatch returns the repository
file for each family, (2) the glyph ids cairo maps for a probe string equal HarfBuzz's on that file, and (3) the
metrics equal the app's table (sans 32 px ascent 30 / descent 8 / cap 22; mono 29.33 px 31 / 8 / 21). Seats are the
app's as of the scenes' commit (a6f53163): menu and clock `redesign_baseline` (box_y + floor((box_h + cap) / 2)) ->
rows 41 and 1404; ruler `ruler_label_baseline_px` (lane top + pad 0 + ceil(ascent) 30) -> 202, re-derived at the
`ruler_label_pt` size (or placed by `ruler_layout`); flags lane.y + 32 -> 260. Result: every text pixel of the 1002
and 1002a captures is reproduced byte-exact (menu, legend, ruler labels, flag labels, clock).

## Measured geometry (device px, end-exclusive)

Every row and column of the 1002 scene, re-measured; 1002a and s4 only where they differ.

| element | 1002 (default) | 1002a | s4 (old lane stack) |
|---|---|---|---|
| menu lane | rows 0..60; File at x 20 (pad 20), buttons nearbyint(shaped width) + 40 wide; Edit disabled; legend `100% ↑ \| 7:01 AM` right edge 2288; baseline 41 | Edit enabled; legend `6:57 AM` | legend `5:39 AM` |
| icon lane | rows 60..152; buttons 64x64 at y 74; glyph 44x44 at +10,+10; separators 2x68 at y 72, x 88 374 660 878 1164 \| view 2078 | same | same |
| icon buttons | x 16 \| 98 166 234 302 \| 384 452 520 588 \| 670 738 806 \| 888 956 1024 1092 \| 1174 1242 1310 1378 1446 1514 1582 \| 2088 2156 2224; ViewTW toggled (fill + ring, radius 10, 2 px line) | same | same |
| trim lane | rows 152..172; ground bevel lo 152-153, hi 154-155; bottom border 170-171 (full width) | no ground visible (bevel rows from the caps) | same as 1002 |
| trim bar | painted x 660..1937 (visible 678..1919 between the handles), relief 2 px: hi rows 152-153 and left columns, lo rows 168-169 and right columns (under the handles here) | painted x -1..2305: the bar runs past both edges, its left relief's one column at x 0 (hi), its right relief's one at x 2303 (lo, the top-right corner dark) | x 611..1560 (the visible stretch; no side relief recorded) |
| trim caps | handles 660..678 and 1919..1937 (cap bevel lo 152-153, hi 154-155, endcap below); grip 1289..1307, hollow 1293..1303 x 156..166 in bar ink | no handles; grip 275..293, hollow 279..289 | handles 593..611, 1560..1578; grip 1076..1094 |
| ruler lane | rows 172..228 (56 = 0 + 30 + 2 + 24); labels at tick + 6, baseline 202, ink rows 180..201, ground 202..203; major ticks 2 px from 220 (rise 8), minors from 228, all to 268; 125 ms per label, 8 ticks per step; fit ms_per_px 0.562811, vp 45455.538 ms; majors at 79 301 523 745 967 1189 1412 1634 1856 2078 2300 (none hidden), labels 0:45.500 .. 0:46.750 | same | rows 172..230 (58); baseline 204, ink 182..203; majors from 222 at 189 (411 hidden) 633 .. 2188, minors from 230 to 270; ms_per_px 0.562747, vp 45518.641; first label 0:45.625 |
| playhead | head rows 204..228 (12 authored rows x 2), half-widths 18 16 14 12 12 10 8 8 6 4 2 2 (+2), kPlayheadHead at alpha 0.8 over the ruler (120,121,124), top row x 275..313; stem x 293..295, marker-lane run 228..268, canvas run 268..1346 | head top row x 380..418, col 398 on the first flag's stem: the stem suppressed | head rows 206..230, stem x 410..412 |
| marker lane | rows 228..268 (40); flags x 398 (`1.27+0.00:b.33`, w 221), 871 1343 1816 (`b.33`, w 70), 2289 (`b.33`, clipped: its box would end at 2359); 2-px border each side, 2-px top edge, label at x + 4, baseline 260; stems 2 px from 268 to 1346 in the flag's fill | the first flag selected: fill (201,116,237), edge (112,64,131), its stem in the selected fill | rows 230..270; flags x 286 759 1231 1704 2177 (none clipped); baseline 262 |
| well | rows 268..1346; black border 268..272 and 1342..1346; canvas 272..1342 (1070 rows); ink runs only (no outline pixels at this zoom) | same | 270..1346, canvas 274..1342 (1068) |
| bottom lane | rows 1346..1440; border-top 1346-1347 at x 2..2302; buttons 64x64 at y 1362: 1082 1150 1218 1286 1354 1422 \| 1508 1576 1644 1712 \| 1798 1866 1934 2002 \| 2088 2156 2224; separators 2x64 at x 1496 1786 2076; clock `B \| 00:45.621` at x 16, baseline 1404 | clock `B \| 00:45.680` | clock `B \| 00:45.750` |

### Enabled and disabled

- 1002 (the default; the same set as s4): disabled are menu Edit; Undo, Redo, Copy, BPM, the history group's six
  companions (Walk, Cumulative, Revert, Older, Newer, Load in place); the marker verbs Delete, Disable, Inherit,
  Jump to defining; Down, Up; and Play/Stop. Everything else is enabled, the marker walk's Left/Right included.
- 1002a: disabled are Undo, Redo, Copy, BPM, the history group's six companions, Jump to defining, and Left, Right.
  Menu Edit, the marker verbs Delete / Disable / Inherit, Down, Up and Play/Stop are enabled.

## Theme schema

A theme is a JSON object; every key is optional and its default is THE DEFAULT LOOK: the app before 2026-10-02 as
Samsung Gallery showed it, every role the scene's app constant through `colour.two_pass` (the platform's
sRGB -> Display-P3 pass twice, the measured bytes where the measurement and the matrix differ). Colour values:
`"#rrggbb"` (P3 bytes as-is), `"srgb:#rrggbb"` (one sRGB->P3 pass), `[r, g, b]`, `"@role"` (another role's
resolved colour) or `"auto"` (the rule given). Unknown keys are refused, inside `waveform` and `flags` too; the
newer options (`separators`, `bottom_border`, `well`, `canvas_delta`, `trim.cap_w`, `trim.lane_h`, `flags.relief`,
`ruler_tick_relief`, `ruler_pad_top`, `ruler_layout`, `lane_order`, `trim.style`, `trim.acid_inset`,
`menu.highlight`, `ruler_label_pt`, `playhead_head_rows`, `icons`) refuse an unknown value.

Top level: `name`, `description` (free text), `colours` {role: colour}, `waveform` {`ink`, `canvas`, `outline`},
`flags` {`fill`, `edge`, `border`, `label`, `stem`, `fill_sel`, `edge_sel`, `stem_sel`, `hilight`, `hilight_sel`}
(aliases of the roles `ink`, `canvas`, `outline`, `flag_*`) plus `flags.relief` (`"none"` \| `"raised"`, below),
the numbers and the options below.

### Colour roles (default = `colour.two_pass` of the scene's app constant)

| role | default | where |
|---|---|---|
| `ground` | (33,35,38) kRedesignContentGround | the window ground |
| `menu_ground`, `icon_row_ground`, `trim_ground`, `ruler_ground`, `marker_ground`, `bottom_row_ground` | `@ground` | each lane's ground |
| `row_ground` | (42,44,48) kRedesignRowGround | not painted by this scene; available for references |
| `popup` | (29,31,34) kRedesignPopupGround | not painted; available |
| `icon_face` | `@icon_row_ground` | a raised icon-row button's face |
| `button_face` | `@bottom_row_ground` | a raised bottom-row button's face; also the base of the `auto` bevels |
| `down_face` | `@selected_fill` | the toggled (down) button's face |
| `label` | (252,252,252) | menu words |
| `legend` | `@label` | the battery + clock legend |
| `menu_disabled` | `auto` = mix(label, menu_ground, disabled_mix) | the disabled menu word (the app's byte-space mix) |
| `clock` | `@label` | the clock text |
| `ruler_label`, `ruler_tick` | (194,194,194), (115,115,115) | |
| `ruler_tick_light` | `auto` = colour.lin_mix(ruler_tick, [255,255,255], 0.35) (the `flag_hilight` rule; (181,181,181) from `#737373`); any colour value, `"@bevel_hilight"` included | the light line of `ruler_tick_relief` `"light_right"`; not painted otherwise |
| `line` | (84,86,89) kRedesignLine | the toggled button's ring (app style) |
| `tab_line` | (76,78,81) | |
| `separator`, `bottom_border` | `@tab_line` | separators (line style), the bottom row's border-top (line style; the colour role, not the `bottom_border` option) |
| `selected_fill` | (61,63,65) | |
| `trim_bar`, `trim_bar_bevel_hi`, `trim_bar_bevel_lo` | (47,49,53), (60,63,67), (40,42,45) | |
| `trim_ground_bevel_hi`, `trim_ground_bevel_lo` | (59,62,66), (19,21,22) | |
| `trim_cap`, `trim_cap_bevel_hi`, `trim_cap_bevel_lo` | (163,169,175), (170,176,182), (160,165,171) | handles and grip |
| `trim_bottom_border` | (19,21,22) | |
| `well_border` | (0,0,0) | |
| `canvas`, `ink` | (20,22,24), (150,191,218) | |
| `outline` | `auto` = colour.lin_mix(ink, canvas, 0.5) | recomputed from the theme's ink and canvas |
| `playhead_head`, `playhead_stem` | (142,143,145), (252,252,252) | |
| `flag_fill`, `flag_edge`, `flag_border`, `flag_label` | (138,94,172), (77,52,95), (19,21,22), (0,0,0) | |
| `flag_stem` | `@flag_fill` | |
| `flag_fill_sel`, `flag_edge_sel` | (179,123,224), (99,68,123) (two_pass of kMarkerFlagFillSel #C974ED, kMarkerFlagEdgeSel #704083) | a selected flag's box (its border and label stay `flag_border`, `flag_label`) |
| `flag_stem_sel` | `@flag_fill_sel` | a selected flag's stem |
| `flag_hilight`, `flag_hilight_sel` | `auto` = colour.lin_mix(flag_fill, [255,255,255], 0.35), likewise from `flag_fill_sel` (35 % toward white in linear light; (190,174,206) and (210,184,235) from the default fills) | the light lines of `flags.relief` `"raised"`; not painted otherwise |
| `icon_label` | `@label` | the app glyphs' text ink |
| `icon_record`, `icon_negative`, `icon_preview_on`, `icon_lift_cross`, `icon_accent`, `icon_plain_white`, `icon_wav` | two_pass of icons.cpp's kIcon* | the coloured app-glyph paths |
| `bevel_hilight`, `bevel_light`, `bevel_shadow`, `bevel_dkshadow` | `auto`: button_face x 1.75, = button_face, x 0.69, black (the Redmond97 Dark ratios #373737 -> #606060 / #262626) | the Windows relief set |
| `accent` | `@ink` (the architect, 2026-10-02: the waveform's ink is the accent) | the `menu.highlight` `"fill"` face; nothing else reads it |
| `stamp` | (140,140,140) | the `--label` text |

### Numbers

`disabled_mix` (0.322, kRedesignDisabledMix: a disabled glyph or word keeps that share of itself over what is
under it), `playhead_head_alpha` (0.8), `canvas_delta` (0: a whole number of logical rows, see THE WELL below),
`ruler_pad_top` (0: a whole number of logical rows >= 0, see the options table; not with `ruler_layout`), `ruler_label_pt` (12: the timestamps'
point size, a number > 0, see the options table), `playhead_head_rows` (12: the head's logical rows, a whole number 1..12, see the
options table).

### Options

The measurements quoted below were taken with themes that share frozen.json's colours and its RELIEF OPTIONS —
`relief` thin, `buttons` raised (both rows, ViewTW down, `down_shift`), `separators` etched, `clock_panel` sunken,
`bottom_border` none, `ruler_tick_relief` light_right, and the `trim` styles bar / handles / grip raised and ground
sunken — and differ from it in the well and the lane geometry each one names; a key not named is at its default
(12 pt labels, the app's seat rule, a 12-row head at alpha 0.8).

| key | values (default first) | effect |
|---|---|---|
| `relief` | `"flat"` \| `"thin"` \| `"thick"` | line count of every raised/sunken edge: thin = one logical line (2 device px) a side, thick = the two-line DrawEdge |
| `buttons` | `{"raised": false, "rows": ["icon","bottom"], "down": ["ViewTW"], "down_shift": true, "toggled": null, "down_dither": false}` | `raised`: the listed rows' buttons get a face + EDGE_RAISED+BF_SOFT; `down`: buttons drawn down; `toggled`: `"app"` (the rounded fill + ring the app paints; default when not raised), `"sunken"` (EDGE_SUNKEN+BF_SOFT on `down_face`; default when raised), `"flat_fill"` (square fill, no edge); `down_shift`: the glyph moves one logical px down-right on a sunken button; `down_dither`: the Windows checked face, a 1-logical-px checkerboard of bevel_hilight |
| `separators` | `"line"` \| `"etched"` \| `"raised"` \| `"none"` | the icon row's and bottom row's vertical separators: line = the app's 2-px `separator`; etched = one LW line of Shadow left of the measured x, Hilight on it (EDGE_ETCHED, the light side right); raised = Hilight then Shadow; none = nothing drawn and nothing moves, the measured gap alone separating the groups |
| `bottom_border` | `"app"` \| `"line"` \| `"etched"` \| `"raised"` \| `"none"` | the bottom row's border-top (x 2..2302 from row 1346): app = whatever `separators` is; line = the 2-px `bottom_border` colour; etched / raised = two LW lines from row 1346 (Shadow then Hilight / the reverse), the second on the bottom row's first content rows (1348-1349); none = nothing, the well's own bottom seam separating the well and the bottom row |
| `well` | `"bordered"` \| `"sunken"` \| `{"top": [...], "bottom": [...]}` | see THE WELL below |
| `clock_panel` | `"flat"` \| `"sunken"` | a status-bar panel round the clock cell (x 8 .. cell end + 8, the buttons' rows) |
| `bars` | `{"menu": "flat", "icon_row": "flat", "bottom_row": "flat"}` | `"raised"`: an EDGE_RAISED frame round that band |
| `trim` | `{"bar": "app", "ground": "app", "handles": "app", "grip": "app", "cap_w": null, "lane_h": null, "style": "app", "acid_inset": 1}` | with `style` `"app"` (below): `bar`: app (its two bevel rows) \| raised \| flat; `ground`: app (its bevels + bottom border) \| flat \| sunken (the lane as a sunken frame the bar rides in); `handles`, `grip`: app (cap bevels, the grip's hollow) \| raised \| flat; `cap_w`: the handles' and the grip's width in logical px (null = the measured 9 logical = 18 device px). A wider handle grows INWARD, its outer edge (the trimmed point) fixed: the handle left of the bar's middle keeps its left column, the other its right; the grip grows symmetrically about its centre; the bar's painted extent is unchanged. The app grip's hollow keeps its measured 4-px walls, so it stays centred (cap_w 10 on 1002: handles 660..679 and 1917..1936, grip 1288..1307, hollow 1292..1303). The caps' HEIGHT is the style's, not the lane's 20 rows: rows 152..169 (the lane's bottom border under them), except raised and flat caps in a sunken ground, 154..169 (cap_w 10 gives 20 x 16 device px); with `trim.lane_h` the caps' bottom follows the lane's (below) |
| `trim.lane_h` | `null` (the scene's measured height: 10 logical rows on every scene) \| a whole number >= it | the trim lane's height in logical rows; a smaller value is refused. With d = (lane_h - measured) x LW device rows, the lane is rows y0 .. y1 + d: its BOTTOM moves down by d (the bottom border, the app bar's lower bevel, the sunken frame's lower line), its top rows stay (the app ground's and caps' bevel rows, the app bar's upper bevel, the app grip's square hollow 156..166, so the app grip's face grows under it), and the bar and caps extend to the new bottom. The ruler lane (56 rows) and the marker lane (40) keep their heights and move down by d with everything on them (ticks, labels, the playhead head and its marker-lane run, the flags); the well's top, its seam lines and the canvas top move down by d, the stems' tops with them; the well's bottom, the canvas bottom and the bottom row stay, so the canvas loses d rows and the waveform replays through THE WAVEFORM RESCALED. The menu, the icon row, the bottom row and the `--label` stamp are untouched. Measured on 1002 with the relief options, the one-line list-form well `{"top": ["@bevel_hilight"], "bottom": ["@bevel_hilight"]}` and `"lane_h": 12, "cap_w": 10` (d = 4; ranges end-exclusive, the ink spans and the `-` pairs inclusive): trim lane 152..176 (its frame's light line 174-175); caps 20 x 20 device px, rows 154..174 (face 156..172), handles x 660..680 and 1917..1937, grip 1288..1308; ruler lane 176..232, first label baseline 206 (ink rows 184..205); marker lane 232..272, the first flag's box rows 232..272; the head rows 208..232; the well 272..1346, its seam lines 272-273 and 1344-1345, the canvas 274..1344 (1070 rows, the measured height again, so the runs replay unchanged: ink 402..1228); rows 0..151, the ruler-and-marker strip shifted by d and rows 1346..1439 byte-identical to the same theme without `lane_h` (cap_w 8). THE CAPS' INNER HEIGHT in a sunken thin lane is 2 x lane_h - 4 device rows (154 .. y1 - 2): lane_h 10 / 11 / 12 / 13 / 14 -> 16 / 18 / 20 / 22 / 24 rows, so a square cap is `cap_w` = lane_h - 2 (8 / 9 / 10 / 11 / 12) |
| `trim.style` | `"app"` \| `"acid"` | app: the lane as the app paints it, styled by `bar` / `ground` / `handles` / `grip` / `cap_w` (above). acid: Sonic Foundry ACID 3's loop bar, and `bar` / `ground` / `handles` / `grip` / `cap_w` are IGNORED (`render.draw_trim_acid`): the lane's ground is flat `trim_ground` (no frame, no bevels, no bottom border); the bar a flat `trim_bar` rect over the bar's painted columns, from the lane's top + `acid_inset` logical rows to its bottom - the same inset; at each end whose HANDLE is on screen (the scene's handles: a bound off screen has none, so 1002a's acid bar has no notch) a `trim_cap` triangle the bar's full height h, pointing inward, its vertical edge on the handle's outer column (the trimmed point), its apex h / 2 columns in at the bar's middle row; no grip, no relief anywhere. The notch is filled with cairo ANTIALIAS_NONE (a pixel stair, the Windows way) on a path running half a row past the bar's top and bottom, so bar row i (0-based) takes min(i + 1, h - i) columns: a symmetric slope-1 stair 1, 2 .. h / 2, h / 2 .. 2, 1 for an even h (the plain triangle samples to an empty top row and a lopsided stair). Measured on 1002 with the relief options, frozen.json's two-line well, `trim` cap_w 9, lane_h 11 and `ruler_pad_top` 2 (THE PAD-2 THEME), + `"lane_order": ["ruler", "marker", "trim"]`, `"style": "acid"` (lane_h 11, so h = 22 - 2 x 2 = 18; ranges end-exclusive, the spans inclusive): the trim lane 252..274; the bar rows 254..272, columns 660..1937 (`trim_bar` visible 661..1935, the notches owning the end columns on every row); the left notch columns 660-668 inclusive (rows 254 and 271 one column, rows 262-263 nine), the right 1928-1936 mirrored; every lane pixel is `trim_ground`, `trim_bar` or `trim_cap` |
| `trim.acid_inset` | `1` \| a whole number of logical rows >= 0 leaving the bar at least one row (2 x inset < the lane's rows) | the acid bar's inset from the lane's top and bottom; read only by `style` `"acid"` |
| `lane_order` | `["trim", "ruler", "marker"]` \| any order of those three names (a top-level list) | the three lane BLOCKS stacked top to bottom in this order between the icon row's bottom and the well's top, each keeping its height (after `trim.lane_h` and `ruler_pad_top` or `ruler_layout`, which size the blocks first; the rows quoted in those two rows are the default order's) and carrying its own content (`render.order_scene`): the trim lane its ground, frame, bar and caps; the ruler lane its ground (its `ruler_pad_top` rows included), labels, the playhead head (on its bottom rows) and the majors' rise; the marker lane its ground, flags, the ticks' marker-lane part and the playhead's marker-lane run. The well, the canvas and the bottom row do not move (the blocks' total height is unchanged), nor do the stems' tops (the well's top): when the marker lane is not directly above the well, no stem is drawn between them, the lane in between covering that run as the app would paint it under its own lane. THE TICKS (this tool's reading, not an app rule): while the ruler sits directly above the marker lane a major is one rect from its rise into the ruler lane down the marker lane, as today; with another lane between them the major's rise is drawn in the RULER lane's bottom rows (ruler bottom - kRulerMajorRisePx x S .. ruler bottom) and every tick's marker-lane part as today, `ruler_tick_relief`'s light line following each part. Measured on 1002 with the pad-2 theme (`trim.style` above; in the default order its lanes are trim 152..174, ruler 174..234, marker 234..274) + `["ruler", "trim", "marker"]` (ranges end-exclusive, the ink spans inclusive; the default order's rows in brackets): the ruler lane 152..212 (174..234), rows 152..155 its pad; the first label baseline 186 (208), ink rows 164..185 (186..207); the major at x 301 in two parts, the rise 204..212 and the marker-lane part 234..274 (one rect 226..274), its light line 303..304 over the same rows; the head rows 188..212 (210..234); the trim lane 212..234 (152..174); the marker lane 234..274 unmoved, the first flag's box rows 234..274, label baseline 266. Each lane block is byte-identical to the default order's same block moved (trim 212..234 = 152..174, ruler 152..212 = 174..234, marker = 234..274: 0 differing pixels each), and rows 0..151 and 274..1381 equal the default order's (the `--label` stamp differs). With `["ruler", "marker", "trim"]`: the ruler 152..212, the marker lane 212..252 (the first flag's box rows 212..252, label baseline 244; the ticks one rect again, 204..252 at x 301), the trim lane 252..274 right above the well; the playhead's stem runs 212..252 in the marker lane and resumes at the canvas (278: the list-form well stops the stems at the canvas), the flags' stems likewise; the stem columns in the trim lane are its ground or the bar; the ruler and marker blocks byte-identical to the default order's moved |
| `ruler_pad_top` | `0` \| a whole number of logical rows >= 0 (a top-level number) | rows of ruler-lane GROUND inserted at the TOP of the ruler lane (right under the trim lane in the default `lane_order`); a negative or fractional value is refused. With d = ruler_pad_top x LW device rows the ruler lane is rows y0 .. y1 + d: its top and the trim lane and everything above stay; the labels' baseline, the ticks' tops and bottoms, the playhead head, the marker lane with its flags and the playhead's marker-lane run, the stems' tops, the well's top and its seam lines and the canvas top move down by d; the canvas bottom, the well's bottom and the bottom row stay, so the canvas loses d rows and the waveform replays through THE WAVEFORM RESCALED. It is the same scene shift as `trim.lane_h`, opened at the ruler lane's top instead of the trim lane's bottom (`render.shift_scene`, `at` 'ruler' / 'trim'), and the two add: the trim shift moves the ruler lane's top, the pad then opens the lane further. Measured on 1002 with the relief options, the one-line list-form well `{"top": ["@bevel_hilight"], "bottom": ["@bevel_hilight"]}`, `trim` cap_w 9 and lane_h 11 (so the trim shift is 2) + `"ruler_pad_top": 2` (d = 4; ranges end-exclusive, the ink spans inclusive; the same theme without the pad in brackets): trim lane 152..174 (unchanged); ruler lane 174..234 (174..230), rows 174..177 plain ground; the first label (0:45.500) baseline 208 (204), ink rows 186..207 (182..203); the first major tick (x 79) rows 226..274 (222..270); the head rows 210..234 (206..230); the marker lane 234..274, the flags' boxes with it, label baseline 266; the well 274..1346, its one-line list-form seams 274-275 and 1344-1345, the canvas 276..1344 (1068 rows; 1072), ink 404..1229 (401..1228), inside the canvas. Rows 0..173, the unpadded theme's rows 174..271 shifted by d and rows 1346..1439 are byte-identical to the unpadded theme's |
| `ruler_layout` | `null` \| `{"above": A, "below": B}`, whole logical rows >= 0 (a top-level object; refused together with a `ruler_pad_top` key) | the ruler lane's vertical layout as two direct numbers instead of the app's seat rule (`render.ruler_layout_rows`). `above` = the ground rows from the ruler lane's TOP (the row after the lane above it, after `lane_order`) to the digits' CAP TOP (the labels' first ink row at `ruler_label_pt`); `below` = the ground rows from the labels' BASELINE (the row after the last cap ink row) to the MARKER LANE's top (the ruler lane's bottom, where the head sits, when `lane_order` puts another lane between them). The lane is above + cap + below, cap = nearbyint(the face's cap height) at that size, in device rows (8 pt = 21.333 px: cap 16 device = 8 logical; 12 pt = 32 px: 22 = 11). `null` keeps the app's rule (the cap-top seat plus `ruler_pad_top`, the lane the scene's). The lane's change against the scene's is opened by the same shift as `ruler_pad_top` (`render.shift_scene` at 'ruler'; negative when the lane is shorter): the ticks, the head, the marker lane with its flags, the stems' tops and the well's top move by it, the canvas bottom stays and the waveform replays through THE WAVEFORM RESCALED. THE HEAD KEEPS ITS SEAT, its 12 logical rows (`playhead_head_rows`) ending at the marker lane's top, so `below` = 12 (= `playhead_head_rows`) makes it TOUCH the digits (its top row directly under their baseline) and `below` < 12 OVERLAP them, drawn in the app's order: the labels, then the head at `playhead_head_alpha` over them. The app's rule (kRulerHeadGroundPx) keeps at least one logical row of ground between the baseline and the head, so `below` < 13 is a picture the app will not produce. A lane shorter than the head (24 device rows; `playhead_head_rows` x 2 when set) is refused. Measured on 1002 with the relief options, frozen.json's two-line well, `trim` cap_w 9, lane_h 11 and `ruler_label_pt` 8 (ranges end-exclusive, ink rows inclusive): `{"above": 9, "below": 16}` reproduces the same theme laid out by the seat rule with `"ruler_pad_top": 5` instead, its lane 174..240 (33 logical), ink 192..207, baseline 208, the head 216..240 (4 logical rows of ground over it), the marker lane 240..280, the canvas 284..1342 (1058 rows), byte-identical to it unlabelled (labelled, every differing pixel is inside the stamp's ink box); `{5, 12}`: lane 174..224 (25), ink 184..199, baseline 200, head 200..224, TOUCHING (the second label's last ink row, x 310..315, directly over the head's top row, x 275..312), marker lane 224..264, canvas 268..1342 (1074); `{3, 10}`: lane 174..216 (21), ink 180..195, baseline 196, head 192..216, OVERLAPPING the digits' four lowest rows (10 of the second label's ink pixels under the head), marker lane 216..256, canvas 260..1342 (1082) |
| `ruler_label_pt` | `12` \| any point size > 0 (a top-level number) | the ruler timestamps' size: Roboto at pt x 96 / 72 x 2 device px (12 = 32 px, the app's 12pt x gui_scale) through the same font road (GRAY, SLIGHT, hint metrics on), shaped by HarfBuzz at that size; the flags, the menu, the clock and the stamp keep theirs. The SEAT is the app's rule at the new size (`render.ruler_label_seat`, `ruler_label_baseline_px`): without `ruler_layout` (which places the labels itself, below) the cap top stays kRulerLabelCapTopPx = 4 logical rows under the ruler lane's top (plus `ruler_pad_top`): baseline = lane top + pad rows x 2 + max(0, 8 - (ceil(ascent) - nearbyint(cap))) + ceil(ascent), that size's ascent and cap (cairo's, as `verify_fonts` measures them); each label's x is unchanged (its tick + 6). THE LANE KEEPS ITS HEIGHT (see What is approximated) unless `ruler_layout` sizes it. Measured on 1002 with the relief options, frozen.json's two-line well, `trim` cap_w 9, lane_h 11 and `ruler_pad_top` 5 (THE PAD-5 THEME: ruler lane 174..240, the pad's 10 rows on top; the head 216..240), device px, ascent / descent / cap, the derived pad, the baseline, the label ink rows (inclusive), the shaped width of `0:45.500`: 12 pt 32 px, 30 / 8 / 22, pad 0, baseline 214, ink 192..213, 124.09 \| 11 pt 29.333 px, 28 / 8 / 21, pad 1, baseline 213, ink 192..212, 113.75 \| 10 pt 26.667 px, 25 / 7 / 18, pad 1, baseline 210, ink 192..209, 103.39 \| 9 pt 24 px, 23 / 6 / 17, pad 2, baseline 209, ink 192..208, 93.05. The cap top is row 192 at every size; the narrowest gap from a label's last ink column to the next major tick is 93 / 103 / 113 / 123 columns |
| `playhead_head_rows` | `12` \| a whole number of logical rows 1..12 (a top-level number) | the playhead head's height. The app's head is 12 authored rows, half-widths 9 8 7 6 6 5 4 4 3 2 1 1 at 100 % (each row S = 2 device rows here); a smaller N keeps the head SEATED on the ruler lane's bottom (its tip's rows and its bottom row unchanged) and drops the top 12 - N logical rows, the WIDEST ones: the drawn head is the last N entries of the half-width list (`render.head_rows_drawn`), at `playhead_head_alpha`, in the app's order (over the labels). The playhead's marker-lane run (the stem under the head) is unchanged; nothing moves. `ruler_layout`'s lane-shorter-than-the-head refusal reads the drawn head. Outside 1..12 (or not a whole number) is refused. 12 is byte-identical to the key's absence. Measured on 1002 with frozen.json's options at other head heights (8 pt, `ruler_layout` {6, 10}, opaque head; the ruler lane 174..222, baseline 202, so baseline -> marker lane = 10 logical rows; ranges end-exclusive, x spans inclusive): 12 (the key's absence): the head 198..222 (12 logical), top row x 275..312 (38 px, 19 logical), OVERLAPPING the digits by 2 logical rows; 10: the head 202..222, top row x 279..308 (30 px, 15 logical), TOUCHING (the second label's last ink row 201, x 310..315, directly over the head's top row); 9: the head 204..222, top row x 281..306 (26 px, 13 logical), one logical row of ground under the baseline. Every pixel that differs from the 12-row head is in the dropped rows 198..201 (10) or 198..203 (9), x 275..312 |
| `menu` | `{"highlight": null}` \| `{"highlight": {"item": "File" \| "Edit" \| "Settings", "style": "fill" \| "sunken" \| "raised", "label": colour}}` (`style` defaults to `"fill"`, `label` to `"@label"`) | a face on ONE menu anchor (`render.draw_menu`). The anchor's rectangle is the app's (paint_menu_row: THE LANE IS THE PILL): the whole menu lane (rows 0..60), nearbyint(shaped label width) + 2 x 20 wide, the anchors adjacent from x 0 — File 0..90, Edit 90..184, Settings 184..341. `"fill"`: the rectangle filled in `accent`, SQUARE corners (the Windows 95 menu-bar highlight), the label drawn over it in `label` (any colour value; it replaces the enabled or the disabled colour alike); `"sunken"` / `"raised"`: no fill, one thin line a side on the rectangle (sunken: `bevel_shadow` top and left, `bevel_hilight` bottom and right, the BR pair last; raised the reverse — Windows 98's hot-tracked menu title, sunken when open, raised on hover), whatever `relief` is, the label unchanged. The legend and the other anchors are untouched; a `bars.menu` `"raised"` frame is drawn first and the face covers it over the rectangle. Measured on the pad-5 theme (`ruler_label_pt` above) + File `"fill"`: the change is exactly rows 0..59 x columns 0..89 (5400 px: 4908 accent (150,191,218), the 492 label pixels on the accent-to-label line, 242 of them full `label`), the label ink rows 18..40, columns 22..69; + File `"sunken"`: the same box, 2-px (30,30,30) top and left, (94,94,94) bottom and right |
| `flags.relief` | `"none"` \| `"raised"` | raised: the flag keeps its 2-px border columns, and inside them the fill gets one LW line of `flag_hilight` on top and left and one of `flag_edge` on bottom and right (the dark pair last, owning the top-right and bottom-left corners); the 2-px dark top band is not drawn (the relief replaces it). A selected flag uses `flag_hilight_sel` / `flag_edge_sel`. The label's seat is unchanged |
| `ruler_tick_relief` | `"none"` \| `"light_right"` | light_right: every tick, major and minor, gets one LW line (2 device px) of `ruler_tick_light` immediately right of it, over exactly the tick's own rows (a major's from `major_top`, a minor's from `minor_top`, both to `tick_bottom`; on 1002 the major at 301..302 gets 303..304 x rows 220..267, the minor at 135..136 gets 137..138 x 228..267), drawn right after its tick, so the labels, the head, the flags and the stems cover it wherever they cover the tick. Nothing moves; the ticks keep their columns and colour |
| `icons` | `"app"` | the app's own glyphs, recovered per ink from the capture (a disabled one mixed toward what is under it at `disabled_mix`); any other value is refused (the retro icon sets were not shipped) |

The edge grammar (`render.relief_lines`, Windows' DrawEdge), lines as
(outer TL, outer BR), (inner TL, inner BR); TL drawn first, BR last (BR owns the top-right and bottom-left corners):
push button EDGE_RAISED+BF_SOFT = (Hilight, DkShadow), (3DLight, Shadow); pressed EDGE_SUNKEN+BF_SOFT = (DkShadow,
Hilight), (Shadow, 3DLight); panel EDGE_RAISED = (3DLight, DkShadow), (Hilight, Shadow); well EDGE_SUNKEN = (Shadow,
Hilight), (DkShadow, 3DLight); thin = the first pair's Hilight/Shadow (raised) or Shadow/Hilight (sunken).

### The well

The well's rows are the scene's `lanes.well` (268..1346 on 1002): its top is the lane stack's bottom (the marker lane's in the default `lane_order`; the order never moves it), moved only by
`trim.lane_h` and `ruler_pad_top` / `ruler_layout`; its bottom is the scene's well bottom moved by `canvas_delta` logical rows (2 device rows each) with
the bottom row's border-top fixed. A positive delta needs a strip of ground between the well and the bottom row: every scene has
none (gap 2 = 0), so a positive delta is refused with that message; a negative delta leaves a strip of `ground`
between the well's bottom seam and the bottom row's border (-2 on 1002: the well ends at 1342, rows 1342..1345
ground).

- `"bordered"`: `well_border` (black) on the measured seam rows, 4 device rows top and bottom (268..271, 1342..1345).
  The stems (flags and playhead) run through the border to the well's bottom.
- `"sunken"`: thin = Shadow outer line over the black inner line on top, black then Hilight below; thick =
  Shadow/DkShadow on top, 3DLight/Hilight below (EDGE_SUNKEN), on the same measured seam rows.
- THE LIST FORM `{"top": [line, ...], "bottom": [line, ...]}`: each entry one LW line (2 device px) of a colour
  (any colour value above, `"@role"` included, but not `"auto"`), BOTH LISTS IN SCREEN ORDER, top to bottom (the top
  list from the outside in, the bottom list from the inside out). the thin sunken well = `{"top": ["@bevel_shadow",
  "@well_border"], "bottom": ["@well_border", "@bevel_hilight"]}` (byte-identical to `"sunken"` at thin); a plain thin
  sunken = `{"top": ["@bevel_shadow"], "bottom": ["@bevel_hilight"]}`; thick EDGE_SUNKEN = `{"top": ["@bevel_shadow",
  "@bevel_dkshadow"], "bottom": ["@bevel_light", "@bevel_hilight"]}`; one black line = `{"top": ["#000000"],
  "bottom": ["#000000"]}`; no seam = `{"top": [], "bottom": []}`.
- The canvas is DERIVED: list form, canvas top = well top + len(top) x LW and canvas bottom = well bottom -
  len(bottom) x LW; bordered and sunken keep their measured 4 seam rows at each end. With `"sunken"` and the list
  form the stems stop at the canvas.

THE WAVEFORM RESCALED: when the derived canvas height differs from the scene's measured one (1070 rows on 1002,
1068 on s4), each column's ink runs and outline runs are replayed separately through a nearest-neighbour vertical
map: the runs expand to a boolean column of the measured height, destination row r (0-based from the canvas top)
takes source row floor(r x old_h / new_h), and the result is re-run (no antialiasing, no blending; ink is painted
first, the outline over it, as always). The channel split moves with it to canvas top + ceil(split_rel x new_h /
old_h), the first destination row whose source row is at or below the measured split (= canvas top + new_h / 2 for
an even height, the extractor's measured rule). At the same height the runs are drawn unchanged at the derived canvas top. On
1002 the ink spans canvas rows 128..954 (400..1226); one Shadow / one Hilight line gives canvas 270..1343 (1074) and
ink 399..1228; three lines a side 274..1339 (1066), ink 402..1225; no seam 268..1345 (1078), ink 397..1230.

## Adding a theme

Copy `themes/frozen.json`, change what you need, render; an empty `{}` is the default look. A colour borrowed from
an sRGB source (a theme file, a scheme table) takes the `srgb:` prefix; a colour you want on the glass exactly takes
the plain hex. Example — Redmond97 Dark's face as sRGB, thick relief:

```json
{"name": "r97", "colours": {"ground": "srgb:#373737", "bevel_hilight": "srgb:#606060", "bevel_shadow": "srgb:#262626",
 "bevel_dkshadow": "#000000"}, "relief": "thick", "buttons": {"raised": true}, "separators": "etched",
 "well": "sunken", "clock_panel": "sunken", "trim": {"bar": "raised", "ground": "sunken", "handles": "raised", "grip": "raised"}}
```

## Renderer notes for the scene's states

- Z-order as the app paints: the ruler (ticks, each with its `ruler_tick_relief` light line, labels, the head, the playhead's marker-lane run), the flags over that
  run, the well and the waveform (at the derived canvas, see THE WELL), then the flag stems, then the playhead's stem over them — except where the scene
  records `stem_suppressed`: there the playhead's stem is left out entirely (both runs) and the coincident flag's
  stem shows, exactly the app's `playhead_stem_suppressed`. The head always paints.
- A selected flag draws in `flag_fill_sel` / `flag_edge_sel` and its stem in `flag_stem_sel`; a clipped flag is drawn
  whole and clipped by the surface, as the app's is.
- The ticks: one rect per tick while the ruler sits on the marker lane; with a lane between them (`lane_order`) a
  major's rise and every tick's marker-lane part are drawn separately, each with its own light line.
- An acid trim lane (`trim.style` `"acid"`) is the flat ground, the flat bar and the two aliased notches only; no
  relief, no grip.
- The trim bar (`trim.bar` `app` style) paints in render.cpp's order: face, light top, light left, dark bottom, dark
  right, over the scene's painted extent; the handles and the grip paint over it. `scene_s4.json` has no `bar_edge`,
  so the s4 bar draws without side relief, exactly as before.

## What is approximated

- Without `ruler_layout`, a smaller `ruler_label_pt` re-seats the labels by the app's rule but keeps the lane's
  rows: in the app as of the scenes' commit (a6f53163) the ruler lane's height is DERIVED from the label face
  (`ruler_lane_h_px` = the seat + kRulerHeadGroundPx x S + the head), so the lane would shrink by the seat's change
  (12 -> 11 / 10 / 9 pt: 1 / 4 / 5 rows at 200 %) and hand those rows to the waveform, the head keeping
  kRulerHeadGroundPx's authored row of ground under the digits. Here the head, the ticks and everything below stay,
  so the ground between the digits and the head grows by those rows instead. `ruler_layout` sizes the lane directly
  instead (the head then keeps only its seat), which is how frozen.json draws the frozen design's ruler (the app now
  derives the lane from the label face and kRulerBaselineToMarkerPx, render.h).

- Another `lane_order` is the measured blocks RESTACKED, not the app laid out in that order: each block carries
  its capture's rows unchanged. The split of a major tick's rise from its marker-lane part when a lane separates
  the two is this tool's reading; the app has no rule for it.
- A taller trim lane (`trim.lane_h`), a ruler top pad (`ruler_pad_top`) and a `ruler_layout` lane are the scene
  SHIFTED, not a re-layout by the app: the ruler's contents, the marker lane and everything on them are the capture's measured rows moved down
  by d, and the canvas gives up those rows. The app would lay out the same way (its lane stack hands the leftover to
  the waveform, main.cpp's lane table; a pad would enter `ruler_lane_h_px` and `ruler_label_baseline_px` alike), but
  the waveform here is the replay below, not the app's recomputed peaks. A `ruler_layout` lane shorter than the
  scene's moves everything below it UP by the difference, and the canvas takes the rows.

- A scene is ONE state of the app (its capture): its zoom, flags, playhead, enabled set and the ViewTW view.
  Nothing is laid out for other states; the waveform is replayed from the capture's pixels, not from audio.
- A canvas of another height (the list-form well, `canvas_delta`) gets the capture's columns through the
  nearest-neighbour vertical map (THE WELL), not a re-render from audio at that height: the app would recompute each
  column's peaks for the new height, so individual rows can differ from the replay by a row where a run's end falls
  between source rows, and some rows of the source are repeated (a taller canvas) or dropped (a shorter one).
- Waveform columns under the flag stems and the playhead's stem repeat their outer neighbour column
  (`waveform_<tag>.json`'s `filled_columns`: 12 on 1002 — five flags and the playhead — and 10 on 1002a, where the
  playhead's stem is suppressed); they are covered by the stems in every theme anyway.
- App glyphs are the capture's own rasteriser coverage recovered per ink, so they stay exact only at this size;
  78 of their pixels (glyphs whose sub-paths overlap or touch) reconstruct 1 byte off, in every scene. Two-ink
  glyphs (Restrict Undo, Listen) are composited text-then-colour, where the app interleaves its paths; only the
  shared edge pixels could differ.
- The toggled button's app-style face and the playhead head are drawn with the app's own cairo calls; all
  relief, etching and panels are this tool's design reading of the Windows grammar, not app code (the app's own
  painter took them over in the frozen design). The toggled glyph shift is Windows' behaviour.
- The default look's colours are two_pass(constant) and every antialiased pixel is blended from them; the Gallery
  put every antialiased pixel of the app's own picture through the pass instead, so edges differ from such a
  picture by 1-4 bytes.

## Parity (2026-10-02, `compare.py`)

A theme holding the app's constants as they stood at a6f53163 (every role the constant itself, `flag_fill_sel` /
`flag_edge_sel` the Sel constants; not shipped, the app having moved to the frozen design) against each capture,
the geometry check:

| scene | differing pixels of 3,311,000 | max | where |
|---|---|---|---|
| 1002 | 78 (icon row 54, bottom row 24) | 1 | all inside glyph boxes; menu, trim, ruler, flags, well and every text pixel byte-exact |
| 1002a | 78 (icon row 54, bottom row 24) | 1 | the same; the selected flag and its stem, the suppressed playhead stem, the capless trim lane with its edge relief columns, the clipped flag: byte-exact |
| s4 | 78 | 1 | the same (rendered with `--scene s4`, byte-identical to the render before scenes were parameters) |

`themes/frozen.json` on 1002 renders byte for byte the last mock-up of the lineage, the picture judged on the tablet
(0 differing pixels; on 1002a likewise identical to that mock-up's theme rendered on 1002a).

## Recorded notes (each scene's `mismatches`; none unexplained)

- All three: the waveform area is the whole leftover (1078 rows, 268..1345, on 1002 and 1002a; 1076 on s4) with
  gap 2 = 0 — the device's config is max_waveform_height 0 (no maximum), main.cpp's "At 0" tablet stack; the
  templates' default 500 would give 1000 + gap 2 = 78 (76 on s4).
- All three: the brief's "26 + 3 view icons": the source has kIconRowButtons 23 + kIconRowViewGroup 3 = 26 (groups
  1|4|4|3|4|7|3).
- All three: an earlier icon roster listed IconTooltips before IconSettings; the source and the captures have
  Settings (sliders) then Tooltips (the i with the pointer). The note is recorded verbatim in each scene.
- 1002a: the trim bar's bounds are both off screen, so the bar runs one column past each edge and its 2-px side
  relief shows one column at x 0 (light) and one at x 2303 (dark); no ground column is visible, so the ground's
  bevel rows are the caps'.
