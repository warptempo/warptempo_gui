# Warptempo GUI pan and pinch campaign — engineering record (2026-10-13)

The quantitative and methodological record of the October 2026 campaign on
the cost of a pan or pinch step on the tablet. The code comments hold each
change's mechanism and its identity claim at the owning symbol (current
behavior only); this document keeps what they deliberately leave out: the
profiles, the numbers, the order of the findings and the architect's rulings
on what was not built. The campaign ran 2026-10-12 to 2026-10-13 over six
commits (`4c42d782..7d275895`). Every commit's claim is the same: THE PICTURE
IS BYTE-IDENTICAL, only the cost moved. The engine's render campaign is a
separate record (`perf_campaign_2026_07.md`).

## 1. The symptom and the scope

The architect's reports of 2026-10-12, on the tablet, at a coarse zoom over a
project with many markers:

- pan and pinch laborious, the effort PROPORTIONAL TO THE MARKERS ON SCREEN;
- worse with the magnification lamp (the leveler's lit plate) on;
- a lingering lag after a trim render that a restart cleared. The lamp had
  been left on across the restart, so the lag was not an accumulation.

Tablet only: Galaxy Tab S10 FE, 2304 x 1440, gui_scale 300, the panel pinned
to 90 Hz (an 11.11 ms frame). The laptop was fine before the campaign. The
reference view: tab B of the 550 - 1 project, about 670 markers and 6.5
minutes of source on screen.

Cost structure at the start (the two things a pan or pinch step does): every
step rebuilt the waveform plate and the flag cache synchronously (every mover
takes the synchronous full rebuild, 2026-07-26) and then painted every lane
the damage covered.

## 2. The method

- **Profiler.** simpleperf on the tablet, `simpleperf record --app
  com.warptempo.gui -g`, the call graphs resolved by NDK r29's
  binary_cache_builder against the unstripped `libwarptempo_gui.so`,
  `report --children` read for the inclusive shares. The debuggable APK is the
  profiled build.
- **The injected workload.** One-finger pans on the canvas by `adb shell input
  swipe` (1700 to 900 and back at row 700, 1200 ms each), 15 swipes in a 24 s
  record: the only gesture adb can drive, a pinch needing two fingers. THE VIEW
  BAR'S END ZONES ARE THE TRIM GRABS: a swipe injected there drags a trim end,
  so a "zoom" workload through the view bar is invalid. The one such run
  (magon_zoom) dragged tab B's trim end seven times out and seven back; the
  in-memory end re-quantized to the column lattice (about 7900 frames per
  pixel), the disk untouched, and the drags dispatched the in-memory target
  render, which held about two cores for the whole window. Never swipe there.
- **The architect's own gesture as the baseline.** His pinch and pan recorded
  on the same project and view (75 s, 52.4 G cycles) before the first fix
  (pinch_before), and again on the installed builds after the second and third
  (120 s each).
- **The per-frame unit across builds.** SurfaceFlinger's `--latency` reports no
  per-layer frames on this Android (only the 11.11 ms vsync), so the frame is
  counted by `GuiPlatform::present`'s cycles on the same injected workload:
  the same code and the same damage on every build, so its cycles are the
  frame count in units. CPU per frame is the record's total cycles over the
  number of present-units.
- **The device benchmark.** The plate's write pass alone, an NDK-built
  micro-benchmark at the tablet's geometry (a 2256 x 960 ARGB32 plate, stride
  9024, two 480-row bands, a lit and a dark column profile), run on the
  tablet's fastest core (core 7); its variants are §3's table. It exists
  because three successive host-side guesses at the write pass did not rank
  the way the device compiler ranked them.
- **The identity checks.** For the plate and the flags: a throwaway harness
  driving the real function against the previous HEAD over random frames,
  clips and grounds, on surfaces pre-filled with garbage (792 plates of every
  width, height, zoom, lamp and line width for the delta rows). For the damage
  cut: the screen test, `adb screencap` before and after an injected pan,
  compared byte for byte over the lanes that must not change (the caption,
  menu row and band rows 0 to 185; row 8), with the canvas the control that
  must differ; and a state test, a drop of a marker then Undo (Undo lit, then
  greyed again, Redo lit, the dropped flag gone).

## 3. The findings and the commits

The first profile (HEAD `4c42d782`, magnification lamp on, 24 s of injected
pan, 32.5 G cycles) and its lamp-off twin (20.3 G):

| share of CPU | lamp on | lamp off |
|---|---|---|
| rebuild per step (synchronous) | 51 % | 14.8 % |
| &emsp; the plate | 32.5 % | 5.8 % |
| &emsp; the flag cache | 18.7 % | 9 % |
| paint | 36.6 % | 70.8 % |
| &emsp; `paint_marker_stems` | 16.1 % | 30.7 % |

Inside the lit plate, `render_waveform`'s own cost was 18 %, of which 12.8 % of
ALL CPU sat on one instruction: the column-major write's store, one word per
row at the surface's 9 KB row stride, a cache miss a row, the lit plate's tall
bars several times the dark plate's rows (the magnified plate cost about eight
times the dark one per step); the expander's linear range-max scan 9.2 %. In
the flag cache, `paint_cues` was 13.8 % (triangles 7.4, text 4.2, boxes about
2). The stems cost two `cairo_mask_surface` calls per stem with a one-pixel-wide
A8 mask, pixman's per-scanline setup paid for one pixel on each of 960 rows.
The architect's own baseline (pinch_before) agreed in shape: rebuild 49.7 %
(plate 35.0, flag cache 14.7), paint 36.5 % (stems 9.3, plate blit 5.1).

### 3.1 `4c42d782` — label text culled, stems one mask each

The flag cache clips each label's text to the spans no later opaque box covers
and skips a fully masked label (the boxes and the hit rects unchanged); the
stems lay each stem as one mask of a cached one-pixel A8 dot column instead of
a rectangle per dot. A scratch memcmp matched the old painting in every case
tried. A triangle blit was tried and left out: its antialiased edges differ by
a level from the direct fill.

### 3.2 `ee089e19` — the row-major plate pass, the range-max table, direct stems

- `render_waveform` gathers every column's bar rows first and writes the
  surface row by row, the word at each pixel decided by the same priority the
  replace-writes expressed (inner over outer, else untouched): only the order
  of the stores changed.
- `WaveformRangeMaxIndex`: a blocked sparse table (blocks of 16, memory about
  the array's own size) over the expander multipliers answers the range maximum
  in constant time. It stores multipliers, never gained peaks, so the ruled-out
  gained pyramid (`perf_campaign_2026_07.md`'s record of retired candidates)
  stays ruled out.
- The stems stored as pixel words straight into the context's image target
  inside its clip rectangles (`stem_direct_target` names the conditions), the
  cached-mask road kept for any context that refuses them.

Measured on his pinch after the commit (pinch_after2, 120 s, 186 G cycles):
the expander 10x cheaper (7.5 % to 1.6 %), the stems 13x cheaper (9.3 % to
0.5 %), the rebuild per step at about 62 to 88 % of before; but
`render_waveform`'s self cost per step fell only to about 0.73x, the row-major
loop visiting every canvas pixel through a branch chain (self 34.1 % of all
CPU, the plate 41.4 %, the flag cache 26.8 % of which `paint_cues` 20.4,
triangles 10.9, shaping 3.5, glyphs 2.2; paint 21.4 %).

### 3.3 `f60b4203` — the vectorized select pass (slower), the triangle stamp, the shaping memo

- The plate's write pass rewritten as a branch-free mask select over six
  int32 SoA arrays (inner interior ink, inner border outline, outer ink, else
  the zero word), which clang vectorizes; its identity rests on the caller's
  whole-surface clear, named at the clear as a dependency.
  RECORDED AS SLOWER ON THE DEVICE COMPILER: on his pinch after the commit
  (pinch_after3, 120 s, 153 G cycles) `render_waveform`'s self cost was 40.2 %
  of all CPU (the plate 47.8 %), about 1.2x slower per step than the branchy
  loop once normalized by the expander's cost; the benchmark of §3.4 confirmed
  it. Pixman's src_n fills stood at 13.4 % self, the plate's whole-surface
  clear among them.
- `paint_ce_cue_triangle` computes the triangle's antialiased coverage once per
  (quantum, rows) with cairo itself and composites each marker through it with
  cairo's own `lerp8x4` arithmetic on the target's words; a cut triangle or any
  other context fills through cairo as before. Triangles 10.9 % to 4.2 % (the
  stamp 2.3 % self). `direct_pixel_target` (render.h) is the one check the
  stems and the triangles share.
- `cue_label_run` (since 2026-10-13 `text_shape::memoized_run`, shared with the ruler's timestamps) memoizes a label's shaped run by font and text: shaping
  3.5 % to 0.3 %. A per-label sprite cache tried alongside was dropped, its
  identity leaning on cairo's internal clip handling for a small gain.

The same injected pan (f60b_pan, 15 swipes / 24 s, lamp on) cost 18.0 G cycles
against 32.5 G: frames processed +23 %, CPU per frame 45 % of `4c42d782`'s.
Paint was now 48 % (plate blit 8.6, band 6.2 + 7.7, row 8 3.4, `render_canvas`
3.5, `render_background` 3.3, flag blit 3.3, `present` 11 with 6.9 self, the
display transform 2.4 self, the caption 1.5) and the rebuild 32 % (plate 22,
flag cache 10). A second finding stood in that profile: every pan step repainted
the caption, the menu row, the toolbar band and row 8 (about 12 % of CPU on
lanes that do not change on a pan); §3.5.

### 3.4 `195fd1b1` — the plate as delta rows, the whole band, no clear

The device benchmark's ranking of the plate's write pass per lit plate (the
dark plate ranks the same way):

| variant | ms per lit plate |
|---|---|
| column-major runs (`4c42d782`'s, the record before the campaign) | 3.33 |
| row-major, branchy (`ee089e19`) | 3.32 |
| vectorized select (`f60b4203`) | 4.53 (the slowest) |
| NEON tiles with the bounds in registers | 1.84 to 2.40 |
| delta rows, whole band, no clear | 0.47 |
| the clear alone | 0.39 |

`render_waveform` now decides one scratch row in full at the band's first row,
buckets every column's bar boundaries by row in one counting pass, re-decides
only the columns whose bars start or end at each later row, and copies the
scratch row onto the surface row by row. The call writes its band whole (zero
where no bar covers) and returns whether it stored its whole area;
`render_waveform_to_cache_surface` drops the whole-surface clear and clears
only what no call stored: the spare last row of an odd plate and the band of a
call that returned early. The per-column computations are untouched. The
write costs about what the clear alone costs, so the clear is the part removed.

Measured (delta_pan, same injected pan, 50.0 G cycles): the app processed about
FIVE TIMES the frames of `f60b4203` in the same 24 s (present cycles 10.3 G
against 1.97 G, about 90 fps during the injected pan, the panel's rate), CPU per
frame 4.85 present-units. The rebuild was 9.5 % (plate 5.8, flag cache 3.6);
THE PAINT WAS 81 %: `present` 20.6 (13.3 self: the dirty-rect copy and the
P3 transform), plate blit 13.8, `paint_ce_case` 13.4 + `paint_icon_row` 10.6
(the band repainted every step), `render_background` 6.6, `render_canvas` 6.2,
`paint_bottom_strip` 6.0 (row 8 repainted every step), flag blit 5.2.

### 3.5 `94d92f3b` — a viewport step repaints only the lanes it can change

`Viewport::invalidate_waveform_area` had damaged the window from row 0 to the
canvas foot, the flag-cache stage a second rect from row 0, and every zoom
applier the clock, though nothing in the caption, the menu row, the band or row
8 reads the viewport on a pan.

`damage_window_top_rows` (viewport.cpp) now damages the viewport lanes, from
the band's foot to the caller's bottom, and marks the rows above as owed a
check, ONE DEBT PER PAINT BUFFER (two on Wayland, one on Android).
`GuiPaintHandler::settle_owed_lane_damage` settles it at the end of the
pre-paint hook on both backends, after the scanner advance and any Follow
page: it compares a fingerprint of everything the caption, menu row and band
paint (the window and palette globals, the caption's title, activity,
maximized state and button faces, the menu titles' enabled and open bits, the
band's standing members in walk order with their panes, enabled, selected,
pressed and glyph) against the one the buffer last painted, and declares the
old rect whole when they differ, when a record is invalid, or when a floating
surface stands; otherwise the rows already show what a repaint would paint. The
clock cell takes the same treatment under `owe_clock_check` with its own
fingerprint (the time text, the tab letter, the yield). The fingerprints are
the painters' own predicates, published by the paint that covers the rows; a
partial paint under different inputs marks the record invalid. The band's walk
is shared with the painter (`icon_row_walk`). Cases, boxes and fields outside
the clip's extents are skipped before they draw (`clip_meets`). The band is
gated by its painted inputs, never dropped from the pan: its greying can change
with the view (Full Zoom Out at the whole song reads the zoom level), and the
fingerprint carries that bit. The erase under the lanes stays whole; its gap
inventory is not crisp.

Measured (dmg_pan, same injected pan, 43.6 G cycles): per frame 3.9
present-units, the band, row 8, caption and menu row no longer painting on a
pan step, `settle_owed_lane_damage` itself 0.14 %. Screen tests: the upper lanes
and row 8 byte-identical across two injected pans, the canvas differing; a
dropped marker then Undo showed Undo lit and then greyed again, Redo lit, the
dropped flag gone.

### 3.6 `7d275895` — residue

Comment-only residue of the review of `5c08a180..f60b4203` (row 8's inventories,
the default palette's name, the hold-column helper's callers, six comments on
the viewport damage rect) and the launcher band's edge on an exact half
rounding to even in `tools/app_icon/gen_app_icon.sh` (the HDPI launcher
background moved one row; every other density byte-identical). No effect on
the pan or pinch.

### 3.7 The ladder

CPU per frame, in present-units on the same injected pan workload (lamp on):

| commit | per frame | note |
|---|---|---|
| `4c42d782` | 20.2 | the morning's baseline, 32.5 G cycles |
| `f60b4203` | 9.1 | 18.0 G cycles; `ee089e19` sat between, measured by his pinch (§3.2), not by this unit |
| `195fd1b1` | 4.85 | 50.0 G cycles at about 5x the frames |
| `94d92f3b` | 3.9 | 43.6 G cycles |

The app holds about 90 fps through injected pans from `195fd1b1` on.

## 4. What remains per frame

Profile of `94d92f3b` (dmg_pan), shares of all CPU: `present` 25.7 % (11.9
self: the dirty-rect copy), the plate blit 19.2, `render_canvas` 8.3,
`render_background` 7.4, the rebuild 13 (plate 7.3, flag cache 5.6), the flag
blit 2.2. What is left is the CPU canvas itself: the plate and the flags
rebuilt into cache surfaces, blitted onto the canvas, the canvas copied to the
window buffer. Each of §5's structural items attacks one piece of it.

## 5. The structural items and the architect's rulings of 2026-10-13

THE STANDING PRE-APPROVAL (architect 2026-10-13): any change that is
pixel-identical and also truthfulness-identical or better proceeds without a
further ruling. The items below change the structure, and each has his word:

- **P1, the opaque plate** (the ground and the grid baked into the plate, a
  SOURCE blit, no canvas fill). INDEFINITELY DEFERRED OR REJECTED, 2026-10-13:
  it pays only on a CPU canvas. The tablet moves to a GPU canvas and the laptop
  follows for parity (the laptop was fine before the campaign and is never an
  authoring device), so no device keeps a CPU canvas to optimize.
- **P2, painting straight into the `ANativeWindow` buffer.** DROPPED,
  2026-10-13: superseded by the GPU canvas's EGL surface.
- **P3, the flag strip** (an opaque flag cache, label sprites, no label text
  past a coarseness threshold). REJECTED, 2026-10-13, RE-OPENABLE if the idea
  seems attractive later (his words). Recorded here and not in
  `closed_questions.md`, because it is not closed.
- **P4, one rebuild per frame, coalesced at the pre-paint hook.** APPROVED,
  2026-10-13, and folded into the GPU arc as its first step. It touches the
  letter of the 2026-07-26 "every mover takes the synchronous full rebuild"
  ruling, not its intent: every frame still takes a synchronous rebuild from
  the latest event; the only frames skipped are those no display ever showed.
- **P5, the GPU canvas.** WANTED, 2026-10-13: the tablet first, the laptop the
  following week. Its design is the GPU arc's own record: see
  `docs/engineering/gpu_canvas_2026_10.md`.

## 6. The instruments

`tmp/` is gitignored and holds the scripts; the method above is what makes
them re-derivable.

- **The profile runner**: starts `simpleperf record --app com.warptempo.gui -g`
  on the tablet for a fixed duration into a data file, waits three seconds, then
  injects alternating one-finger pan swipes across the canvas until five
  seconds before the record ends, then pulls the data file. The swipe count and
  the cycle totals are the comparison between builds; the record is fresh per
  build, the project, view and lamp state held the same.
- **The damage test**: three screencaps (before, after one injected pan, after
  the reverse pan), then a byte comparison of the crops that must not change
  (the caption, menu row and band, row 8) with the canvas the control that must
  differ. The same test run after any change to a fingerprint's inputs shows
  whether a lane went stale.
- **The device benchmark**: the plate's write pass alone at the tablet's
  geometry, one function per variant over the same synthetic columns, built with
  the NDK's clang and run on core 7 by `adb shell`; the variants are §3.4's
  table.
- **The identity harness** (host-side, per change): the real function from the
  built tree against the previous HEAD's over random inputs, on pre-filled
  garbage surfaces, byte-compared.
