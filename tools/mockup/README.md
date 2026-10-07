# tools/mockup — mock-ups over the tablet's own screenshots

WHAT IT IS FOR (architect 2026-10-06): "the tablet is what we're designing for". He takes screenshots of the app's
screens on the tablet, and icons, chrome and themes are mocked OVER THEM — "the same tool can be used for metrics,
for chrome, for CDE, etc.". A capture's pixels are read back into the theme roles they were painted in, written out
in another theme's roles, re-laid by a CHROME VOCABULARY, optionally given another icon set's glyphs, and saved for
the tablet's Gallery. A standalone utility: no link path from any product target, no CMake; Python 3 + numpy, plus
`rsvg-convert` for `--icons`. tools/palette stays the colour authority this tool imports.

## The colour rule

THE BYTES WRITTEN ARE WHAT THE GLASS SHOWS. The app's Android window is a Display-P3 layer, and Samsung Gallery shows
a PNG byte for byte only when it carries the Display-P3 iCCP chunk and nothing else. Every output goes through
`tools/palette/common.py`'s `save_png` (tools/palette/README.md, THE PIXEL CONTRACT); this tool converts no colour,
so a theme's `#rrggbb` is a P3 byte triple exactly as the app paints it.

## Usage

```
python3 tools/mockup/mock.py --capture <png | fragment> --theme <file.theme | builtin> -o <out.png>
        [--capture-theme <file.theme | builtin>] [--chrome win95|motif] [--separator]
        [--icons <dir of <Enumerator>.svg>] [--scene <json>] [--extra key=value ...] [--legacy-dither]
tools/mockup/push.sh <png...>            # the planner's: onto /sdcard/Download in name order
tools/mockup/push.sh --delete <name...>  # the superseded ones off it
```

- `--capture`: the screenshot, or a fragment of a scene's capture name (`062542`), which resolves through the
  scene's `capture_path`. The scene is the one in `scenes/` naming that capture unless `--scene` says otherwise
  (a scene used on another capture is a stderr note, a size mismatch a hard fail).
- `--capture-theme`: the theme the capture was painted in, what the matcher keys on. `builtin` (the default) is
  `windows-2000-standard` since 2026-10-06 evening, its 39 values READ FROM `src/gui/theme_file.h` on every run, so
  the tool cannot drift from the app. A capture painted in the earlier built-in, `windows-95-standard`, names that
  file: `--capture-theme assets/themes/windows-95-standard.theme`. Every scene in `scenes/` is a capture of the
  Windows 95 chrome, its small 23 x 22 toolbar case frozen in scene.py (the product dropped that chrome for Windows
  2000's on 2026-10-06; a Windows 2000 capture's large case has no scene field yet).
- `--theme`: the target, a `.theme` file in the app's own grammar (themes.py): `role=value` lines, `#rrggbb` or one
  of the twenty names, a role not named takes the built-in's value, a caption start without its end is a flat
  caption; anything the app refuses is refused in the app's words (first error only).
- `--chrome`: `win95` is the capture's own stack recoloured, every role swapped, the geometry untouched; `motif`
  re-lays it as CDE's (chrome_motif.py's head, Solaris 9's measurements: frame 5 + title 17 + menu 27 + toolbar 28
  [+ `--separator` 2] + scroll bar 13 + ruler 15/17 + marker 18 = 125 Windows px, the ruler the flex lane).
- `--extra`: the colours a vocabulary needs beyond the 39 roles — never a 40th role in a theme file (the app
  hard-fails unknown keys). motif: `trough` (the scroll bar's trough, Motif's select colour of the ground's set),
  `frame_ts` / `frame_bs` (the frame's shadows, the active title set's); one not given is Motif's own rule on its
  role's 8-bit value (stated on stderr), which can sit one step off a .dp palette's 16-bit values.
- `--legacy-dither`: the capture predates effd544f. The app paints its two dithers — the lit case's checker
  (paint_checker_rect, Windows' 2x2 pattern brush) and the caption gradient (paint_caption_gradient's 4x4 matrix) —
  ONE DEVICE PX A CELL since effd544f, and the matcher reads (and `--icons` repaints the face) at that cell; an
  older capture painted them in Windows-px cells, so without the switch its lit cases' checker reads as glyph (the
  emboss on a lit disabled case is then missed, the case taken as enabled and kept as captured — the identity
  still holds, a recolour does not). Every scene below today is such a capture: pass the switch for them.
- `--icons`: every case the scene lists takes `<Enumerator>.svg` from the folder (the product's set:
  `assets/icons/tango/`), rasterised at the app's glyph seat whatever the drawing's own cell, seated at the case's
  fixed (3, 3) Windows px plus the lit case's one-line shift (icons.h's PLACEMENT), over the case's face in the
  target theme; a disabled case gets draw_disabled's saturate (the luminance at 192 / 255). An alternate set
  previews by pointing at another folder with the same enumerator names.

The last line of every run names the cases found per lane (disabled and lit counts) and the capture pixels no rule
claimed (left as captured; 0 on the three scenes).

## The scene format (`scenes/*.json`)

Authored once per captured screen, in WINDOWS PX at the capture's `gui_scale` (a multiple of 100 here, so every
length is whole device px; a half Windows px is allowed where the app's own centring lands one):

- `capture` (its file name), `capture_path` (repository-relative), `build` (the APK it shows), `notes` (the state).
- `gui_scale`, `size_px`.
- `body`: `wave` (the main and history views) or `list` (the render player).
- `rows`: `caption`, `menu`, `bottom`, and for `wave` `trim`, `ruler` (the label and tick rows), `marker`,
  `well`, for `list` `list`, each `[top, bottom)`.
- `fields`: the time fields (`rows`, `cols`), read in the clock pair.
- `keeps`: drawings in their own inks that are no role (a list row's file glyph), with the role of their `ground`.
- `lanes`: per toolbar lane its `rows` and its `groups`, each group the cases' enumerators left to right, read from
  the source's rosters (paint_handler.cpp: kIconRowButtons, kIconRowHistoryStandIns, kIconRowHistoryOpener,
  kIconRowViewGroup, row 8's four tables, the player branch of paint_modal_dialog) with the stateful faces the
  capture shows. A lane whose case count differs from the capture's is a hard fail with both counts.

The caption's icon seat and button boxes and the case's glyph seat are not in a scene: they are read off
`src/gui/render.h` on every run.

Scenes today, all of the 2026-10-06 captures of APK 7c6c0d8b (the Chicago95 glyphs and the period bitmap faces;
before effd544f, so Windows-px dithers: `--legacy-dither`):
`main.json` (062542: icon row 20 cases, row 8 17), `history.json` (062603: 19, 17), `player.json` (062555: 20, 7).

## The matcher and its limits

matcher.py's head is the rule. Every chrome pixel is a role by exact colour; where two roles share a value the
region decides (the caption, the fields, the marker lane, the well, the list), and inside a region a few adjacency
rules decide the rest (DkShadow beside Shadow, a flag's label between faces, a flag's frame touching a face, the
well's top lines, the emboss's offset structure for a disabled glyph). WHAT IT CANNOT RECOVER from a capture:
- an ENABLED glyph's ink equal to its background — a silver pixel on the face, a white one on a lit cell — turns
  into the target's background (use `--icons` to repaint glyphs whole);
- an antialiased enabled glyph edge keeps the capture's ground in its blend (only the emboss's mixes are remapped);
- THE CAPTION'S CLOSE X is the set's WindowClose drawing, antialiased since effd544f: its solid interior is the
  label role and recolours, its edge blends are no role's value and stay as captured (counted as unclaimed);
  `--icons` repaints case glyphs only and does not reach the caption;
- THE RELIEF'S MITRES (since 2026-10-06, paint_relief_frame): every raised or sunken edge's top-right and bottom-left
  corner blocks are split along the diagonal, antialiased; the pixels on the diagonal blend the two relief tones,
  are no role's value and stay as captured (counted as unclaimed) — one pixel a corner block at 138 %, a
  diagonal of relief_line_px pixels a block at the tablet's scale; a capture from before the ruling has square
  corners and no such pixels;
- a value two roles share inside one region goes to that region's owner (e.g. a black glyph pixel touching a relief
  line reads as DkShadow);
- anything the capture does not show — the inactive caption, a pressed face, a role whose element is off screen
  (the main capture, for one, shows no lit outline) — has no pixels to recolour.

## Adding a chrome vocabulary

A new file `chrome_<name>.py` beside mock.py (picked up by name; `--chrome <name>`), with `NAME`, `EXTRAS`
({key: what it is}, the colours it needs beyond the roles) and `compose(img, scene, capture_theme, target_theme,
extras, rc, args) -> (out, matcher, placed)`, where `placed` maps each lane to `[(case, x, y)]`, the case's box
corner in the output, so `--icons` seats glyphs wherever the vocabulary moved the cases. chrome_motif.py is the
model: it calls chrome_win95.recolour for the role swap and only moves strips and draws its own elements.

## Validation (2026-10-06)

- Identity: each of the three captures through `--theme builtin` reproduces itself, 0 differing px, with and
  without `--legacy-dither` (2026-10-06, after effd544f); without it 062555's lit disabled case reads as enabled.
- Round trip: 062542 to `assets/themes/warptempo.theme` and back (`--capture-theme` that file, `--theme builtin`),
  both legs `--legacy-dither`: 0 differing px.
- CDE: `--capture 062542 --theme examples/cde-solaris9-default-calc.theme --chrome motif --separator --legacy-dither
  --extra trough=#9397A5` against the theme author's tmp/theme_author/mock_CD3_solaris9-default-calc-canvas.png differs
  where the prototype was wrong or drew from other inputs, nowhere else: the toolbar's and row 8's air (the
  prototype left them the caption colour), the right frame (the prototype's strips overwrote it), `frame_bs` one
  step off (Motif's rule on #B24D7A gives #57253C, the .dp's 16-bit value #57253B), the glyph cells (the checker
  and the emboss in the target's Hilight, the enabled glyphs' own greys kept, the one touching case it missed),
  the ruler ticks in the marker lane (Hilight, not the flag label), the well's top lines (the flanks DkShadow, the
  stems in their faces). That comparison ran on CD1's stack (menu 25, scroll bar 16, ruler 14/16); the stack now
  carries Solaris 9's measured lengths (tmp/cde/report_B1.md §1), so the menu bar, the scroll bar, the ruler and the
  Maximize glyph (9 x 9, sunken on the maximised window) differ from the prototype by design.

## Examples (`examples/`)

The theme author's CDE files of 2026-10-06 (tmp/theme_author/report_CD1.md):
- `cde-solaris9-default.theme`: Solaris 9's Default as Sun ran it — dtsession's MEDIUM_COLOR mapping of Crimson.dp's
  sets 1, 2 and 4 (Sun's own set 3, #63639C, for the warp flag), every shadow by Motif's rule; the catalog's
  `cde-crimson` takes the HIGH_COLOR reading instead and differs.
- `cde-solaris9-golden.theme`: Golden.dp (on Solaris 9's own palette list) under the same MEDIUM_COLOR mapping.
- `cde-solaris9-default-calc.theme`: Default with the waveform on dtcalc's display colours (set 3 under cream).
