#pragma once

#include "render.h"   // GuiRect, GuiColor, the chokepoints

#include <cairo/cairo.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

struct AppState;

// THE MATERIALS — THE FORTEPIANO CHROME (architect 2026-10-21, a Salzburg
// Walter fortepiano: "we follow the Salzburg instrument as much as we can").
// THE FRAME OF RECORD is
// tmp/theme_next/out/accent1/accent_C_marker_lane_inside_frame.png (the
// tablet at 300 %, his true-color capture recomposed; its generator
// tmp/theme_next/make_accent1.py, round 2, 2026-10-22 — the brass an accent),
// tmp/theme_next/out/cover_walnut1.png (make_tiles4.py's build_frame4) the
// frame of 2026-10-21 for what that one leaves unchanged; where a number
// here and the frame part, the frame is the law. Cool Edit's flat chrome
// becomes THREE MATERIALS — WALNUT, an even dark BRASS (the lock plate,
// AN ACCENT since 2026-10-22: architect, "in the original the brass is used
// only for accents — the lock, some hinges"; "put the brass just on the
// marker lane … leave the tabs in wood") and EBONY (the naturals) — over
// the program's geometry (program_spec.h; the simple layout since
// 2026-10-22: the plain wood, two rings and the cases); the materials fill
// it and move no length. This header is the one owner of the materials, the cover and bake
// rules, the regions, the tone rule on a material and the chrome's inks.
//
// THE MATERIALS ARE A DROP FOLDER: `assets/tiles/`, ONE MASTER PER MATERIAL
// NAMED BY ITS ROLE (kGuiMaterialNames: `Wood`, `Metal`, `Case`), each
// `<Name>.png` or `<Name>.jpg`, THE PNG WINNING where both stand (the icons'
// rule, icons.h) — Gemini-drawn 4K masters (assets/tiles/README.md) — and
// BESIDE EACH MASTER ITS PRE-RENDERED COVERS (architect 2026-10-21: "launch
// must not filter 17-megapixel masters"), `<Name>-<W>x<H>.png` at the two
// devices' standard windows, 2304 x 1440 and 1920 x 1080 — THE BRASS'S A
// QUARTER-HEIGHT STRIP of them, 2304 x 360 and 1920 x 270 (the STRIP RULE
// below) — rendered by
// tools/tiles/render_covers.py under the cover rule below with PIL's Lanczos
// (the mocks' own filter) and rerun after a drop. Read from the bundle as the
// icons are, BY NAME (GuiPlatform::bundled_tile_names and
// bundled_tile_bytes: the laptop's compiled-in folder, every file of it; the
// APK's `tiles/` assets, THE TABLET'S THREE COVERS AND NOT THE MASTERS,
// build_apk.sh). THE TUNING ROAD (architect 2026-10-22, "tune live, not on
// mocks"): A FOLDER `tiles/` BESIDE THE DEVICE CONFIG (device_config_path's
// folder — on the tablet under `adb shell run-as com.warptempo.gui`, on the
// laptop beside its config; the one road on both platforms) WHOSE FILES WIN
// OVER THE BUNDLE'S BY NAME: the catalog is every name of that folder, then
// every bundle name none of them shadows, read at the launch check and at
// every bake under the rules below unchanged — so a material is retuned by
// dropping the cover the window matches (the tablet's `<Name>-2304x1440.png`,
// the brass's `Metal-2304x360.png`;
// a master alone loses to a bundled cover that reaches the window). An absent
// folder is nothing (no file, no line, no card); no device key, no Settings
// row (materials.cpp's tile_files). A MATERIAL WITH NEITHER A MASTER NOR A COVER, A FILE THAT
// IS NEITHER A PNG NOR A JPEG, OR A COVER NOT THE SIZE ITS NAME SAYS, IS THE
// LAUNCH'S HARD FAIL naming the file (materials::check, which reads each
// file's first 64 KiB alone; a cover of any height its name proves — a
// strip's height against its lane is the match's, below); a window no
// cover reaches with no master to
// scale — the tablet's, were it ever larger than 2304 x 1440 — the bake's,
// the same way.
//
// THE COVER RULE (his "zoom to fit so there is no empty space on the screen,
// exactly"): each master is scaled by ONE factor s = max(W / w, H / h) over
// the CLIENT window W x H — the whole window, the caption to the border's
// last row — and CENTERED on it, the overhang cropped evenly both sides; no
// tiling, no mirror, no repeat, no seam, ever — THE BRASS'S LOOP
// (below) its one exception, a seamless strip under the pan (the tablet: 1440 / 3072, the
// master 2580 x 1440, 138 px cut each side). A MATERIAL PIXEL ON SCREEN AT
// (x, y) IS THE COVER'S PIXEL AT (x, y), whatever paints it: the three are
// covers of the one window geometry, the client alone (the restored laptop's
// sizing frame round it took the brass's pixels beyond it 2026-10-21 to
// 2026-10-22; it wears Windows' constants since, render.h's caption block).
// THE COVER MATCH (the planner's ruling
// 2026-10-21, after the laptop's maximized 1920 x 1047 — labwc's panel
// takes 33 rows — matched no cover and fell to the masters): for a client
// W x H the bake takes THE SMALLEST CATALOG COVER w x h WITH w >= W AND
// h >= H (by area; the exact size the smallest of all) AND DECODES ITS
// CENTER CROP — rows (h − H) / 2 on and columns (w − W) / 2 on, no
// filtering, the decode the bake (the 1920 x 1080 cover serves 1920 x 1047
// at a scale 1 % off the exact rule; a restored window shows the center of
// the same grain); ONLY A WINDOW BEYOND EVERY COVER IN A DIMENSION scales a
// master under the cover rule, by the icons' Lanczos-3 area filter
// (svg_icon::raster_regions) from one decode.
//
// THE STRIP RULE (architect 2026-10-22, "make the brass image a lot smaller
// … cutting the image in half or a quarter vertically, keeping the same
// length"): A MATERIAL'S COVER HEIGHT IS ITS OWN (kGuiMaterialCoverDivisor) —
// the brass, the marker lane's accent alone (17 W, 51 rows at 300 %), covers
// A QUARTER OF THE WINDOW'S HEIGHT at its full width, the others the whole
// window. A strip is the master scaled by ONE factor s = W / w to the
// window's WIDTH, its CENTER rows (the overhang cropped evenly top and
// bottom); a master too short for the strip at that factor is the master's
// hard fail, never a stretch (the masters are 16:9, so none is). THE MATCH
// for a strip material is the smallest cover at least W wide and at least
// THE LANE'S rows tall, and the bake decodes its center columns and THE
// LANE'S ROWS FROM ITS CENTER ROWS — so the brass pixel on screen at (x, y)
// is the strip's center row for the lane's row y, not the cover's (x, y);
// a window wider than every strip scales the master under this rule
// (materials.cpp's source_regions, bake_metal).
//
// THE LOOP (architect 2026-10-22: "it just looks odd scrolling the markers
// but not the background"; "can't we create a wraparound brass image by
// taking a portion of the image, off-shifting it, and then just blending it
// into itself so that it creates a seamless repeating loop … and then just
// have that loop infinitely. I'd much rather have that than a flat color";
// "yes, I think that's what we should do. We should create a looping
// tile"): THE BRASS UNDER THE CUES PANS WITH THEM. (1) THE STRIP IS A
// SEAMLESS LOOP, made offline by tools/tiles/render_covers.py with GIMP's
// tile-seamless method — out(x) = (1 − w(x))·A(x) + w(x)·A((x + W/2) mod W),
// w(x) = |2x/W − 1|, per channel — so its two ends meet with no seam and
// nothing composites in the app (a strip dropped through the tuning road
// loops only if it was rendered that way). (2) THE PHASE: the lane's brass
// stands at a horizontal phase that MOVES WITH THE CONTENT AT CONSTANT ZOOM
// AND HOLDS ACROSS A ZOOM (the planner's rule, his "yes" to it): at every
// change of THE DISPLAYED BASIS — the plate's published span, the basis the
// cues are painted on, so ground and cues move in the same frame even where
// the live viewport runs ahead of an async plate — where spp is unchanged
// the phase advances by the content's travel in device px, (old vp − new vp)
// / spp, accumulated in a double, and where spp moved (a zoom) it stands;
// pans, Follow's pages and the jumps carry the plate with the markers, a zoom
// never slides it (materials::follow_displayed_basis, at the plate's two
// publish sites, waveform_cache.cpp; every publish damages the viewport's
// lanes, the marker lane among them, so a moved phase is always repainted).
// THE PERIOD IS THE TILE'S WIDTH, the window's — the strip's own on the two
// standard windows; a narrower window's tile is the strip's center columns
// and shows one seam where it wraps. (3) THE BLIT stays the BAKE RULE's 1:1
// nearest copy at identity, EXTEND_NONE, never EXTEND_REPEAT or REFLECT: the
// lane's tile is baked TWICE ITS WIDTH (the wrap's two blits, at +p and at
// p − W, read as one copy out of the doubled surface), and the phase enters
// at the brass's one chokepoint (set_source's Metal Face), so every reader
// of the tile — the lane's ground (paint_ruler_row) and the cue labels' face
// boxes in the flag cache — moves together at nearbyint(phase) mod W. The
// ruler's ground and the foot lines keep their flats.
//
// THE BAKE RULE (architect: "absolutely has to be"; the cost table, cairo
// into a 2304 x 1440 surface: a solid fill 0.016 ms, a 1:1 blit of a
// window-sized bake at identity with EXTEND_NONE and FILTER_NEAREST 0.047 ms,
// an EXTEND_REFLECT pattern 1.73 ms, ~100x a solid): BAKE ONCE PER WINDOW
// GEOMETRY — its key the client size, the gui_scale and the strip pad (which
// move the lanes and the case seats; the pad since 2026-10-22) and True
// Colors (the bakes hold the window's bytes) — never at a palette
// install, a project open or a bevel key's OK. A frame compares the key
// alone; the layout is read at the bake. THE FIRST BAKE IS IMMEDIATE, at the
// first frame's PRE-PAINT HOOK (materials::ensure_settled, main.cpp — ahead
// of the backend's paint walk); a gui_scale commit, a strip pad commit and
// True Colors bake at once, one act one bake (materials::ensure). EVERY BAKE REINSTALLS THE
// PALETTE off the new Faces, so the next pre-paint widens that frame's
// damage to the whole client and rebuilds the flag cache from the new tiles
// (take_bake_landed; 2026-10-21). A CHANGE
// OF SIZE OF A BAKED WINDOW WAITS ON THE SETTLE GATE (2026-10-21, the
// laptop's frame drag, a configure per motion): no resize bakes (on_resize
// only stamps its arrival, materials::note_resize), and a paint that finds
// the size moved bakes nothing until kBakeSettle (150 ms) has passed with
// no further size change — the tick then arms the bake and damages the
// client (materials::settle_due), and that frame's pre-paint hook bakes.
// Meanwhile the painters draw from the LAST BAKE'S TILES where one still
// holds the fill, the material's flat mean where none does, and never a
// stale tile's pixel outside the new client. The tablet never resizes, so
// the gate costs it nothing (a transient size before the immersive insets
// settle is baked at once from the cover's crop, the settled one 150 ms
// later). EVERY GROUND FILL IS A 1:1 COPY FROM A BAKE AT THE SAME
// RECTANGLE; every line, bevel, ring and word a solid fill. ONLY THE PIXELS
// A MATERIAL IS EVER DRAWN ON ARE BAKED, as tiles of the window (the masters
// and covers never stay resident):
//   WOOD    ONE TILE, THE BAND'S TOP TO THE WINDOW'S LAST ROW, window-wide
//           (2026-10-22, the brass an accent and the flat family the wood:
//           the cards, the tooltip, the modal family and the keyboard's band
//           stand anywhere below the caption; the rows above and below the
//           brass panel, the canvas's margins and the lifted border's rows
//           earlier that day; the caption's rows 2026-10-21 to 2026-10-22;
//           the band's void and the inactive tab's body, two derived row
//           tiles, 2026-10-21 to 2026-10-22, gone with the simple layout);
//   METAL   ONE TILE, THE MARKER LANE'S ROWS, window-wide, from the strip,
//           its surface twice the window's width (the LOOP)
//           (2026-10-22, the STRIP RULE; THE WHOLE CLIENT 2026-10-21 to
//           2026-10-22, when the flat family stood on it, the brass panel
//           from the tab strip's top to the dock bar before that; the
//           sizing frame's band round the client and the inactive tab's body
//           rows with it 2026-10-21 to 2026-10-22);
//   CASE    THE CASE PATCHES (2026-10-21, a window-wide band per seat row
//           until then) — ONE PATCH PER GLYPH SEAT, at rest and pressed (one
//           line further in), for every case the band, row 8 and the render
//           player lay out in either state of the `h` view
//           (program_case_rects, paint_handler.h: the rows' own layouts), a
//           g x g square, and SINCE 2026-10-22 for every WORD BUTTON and
//           every ON-SCREEN KEYBOARD KEY the window can show
//           (word_and_key_case_rects: the controls row's word clusters of
//           every answer set, with and without the keyboard under the stack,
//           and every page's keys — the two keyboard forms on a platform
//           that paints one alone), each its own w x h; seats sharing rows
//           whose columns overlap baked as ONE TILE over their union (the
//           letter and symbol pages' rows, a word button's seat over a
//           player case's) — each the cover at its own place under the
//           case's ramp (below) (the caption buttons' three faces with them
//           2026-10-21 to 2026-10-22).
// THE LAUNCH ROAD (2026-10-21): the catalog listed once, each material's
// source chosen by name, and ONLY THAT FILE READ, in one sized read; the
// three materials READ, DECODED, DERIVED AND CONVERTED ON THREE THREADS AT
// ONCE (std::jthread; each its own file, its own tiles, nothing shared), the
// tiles taken into the state on the main thread after the join. A decode is
// resvg's one render of the regions' union through the data-URL detour (the
// tablet's cairo has no image reader); at a cover's identity the kept
// pixels are copied straight out of it (svg_icon::raster_regions); the
// window's conversion is the transform's word through its table-driven
// encode (display_transform.h's p3_word_from_srgb_fast).
// RESIDENT at 300 % on 2304 x 1440 about 21.2 MB (the wood's tile 12.8,
// rows 54..1440 window-wide — the band's void 0.9 and the tab body 0.4 gone
// with the simple layout, 2026-10-22 — the brass's lane tile 0.94, 2304 x 51
// doubled (the LOOP), the ebony 7.4: the program's cases' patches 1.4,
// about fifty seats, THE WORD BUTTONS 0.5, their seats 216 x 60 at three
// cluster places on the row's two lifts, and THE KEYS 5.5, 38 tiles of 78
// rows at rest and pressed over the three pages' slots), on the laptop's
// maximized 1920 x 1047 at 138 % about 8.9 MB (the wood about 7.9, the
// brass 0.36 doubled, the word buttons 0.07 and no keys — the laptop paints
// no keyboard) (ARGB32, computed off the geometry 2026-10-22 for the simple
// layout; 22.5 and 9.4 earlier that night, with the void and the tab body;
// before that, when the flat family took the wood and the brass became the
// lane's strip;
// 26.5 and 10.6 earlier that day, when the wood took the panel, the brass's
// whole-client tile 13.3 / 8.0; 25 and 10 that morning, when the word
// buttons and the keys took the ebony; 19 and 10 before that, when the
// caption left the wood and the ebony and the sizing frame's band the
// brass; 19.5 and the brass 8.1 the day before, 8.7 and 3.0 MB measured
// that morning; materials::resident_bytes, which the trace line prints at
// every bake, is the measure).
//
// THE REGIONS — WHICH MATERIAL A SURFACE IS (GuiMaterialRegion, a scope the
// painter names):
//   WOOD   THE PLAIN WOOD (architect 2026-10-22, the simple layout: "the
//          bevels are def part of the problem … redesign with dead simple
//          layout"; program_spec.h's head): the toolbar band under the
//          caption and row 8 on the window's foot, the cover as drawn under
//          their cases and between them; the canvas column's side margins;
//          THE TWO RINGS, the only strokes on the wood — THE PROGRAM RING
//          round the canvas column (below) and ROW 8'S CLOCK'S RING round its
//          time field (paint_ce_time_field) — each the wood Face's `dark`
//          and `hilight` flat strokes (the TONE RULE), every two-tone corner
//          mitred; THE STEREO DIVIDER'S ROWS end to end, inside the canvas
//          too (render.h's waveform_divider: no seam at the frame columns);
//          AND THE FAMILY THAT WAS FLAT, then the brass (architect
//          2026-10-22, "just make the rest wood — that's in keeping with the
//          instrument: the brass is supposed to be little touches here and
//          there"): THE CARDS AND THE TOOLTIP, THE MODAL FAMILY'S BOX
//          MARGINS, ITS RING, A PROMPT'S FACE INTERIOR AND ITS CONTROLS ROW
//          (modal_box.h: the dialog editors, the prompts, the render player,
//          the project picker, the progress modal), THE ON-SCREEN KEYBOARD'S
//          BAND — each the wood as drawn at its own place, its relief the
//          wood Face's strokes — and THE CHROME'S DERIVED ROLES
//          (chrome_derive.h's kChromeRoles: the palette's `face` is the
//          wood's, render.cpp's panel_face). A STROKE BETWEEN THE TWO
//          MATERIALS TAKES THE WOOD'S. THE PROGRAM RING (architect
//          2026-10-22, "B": "the wood bevel should run along the entire
//          length of the left/right frames — the times and other lanes are
//          below the bevel metaphorically"; "the ruler, the timestamp ruler
//          and the marker lane should not have a bevel on top of the wood's
//          bevel. The wood sits on top of the other two … implement the
//          basic plain rule for the wood bevel here"; the mock of record
//          tmp/theme_next/out/flat3/flat_L100.png): ONE PLAIN SUNKEN RING in
//          the wood's strokes round the whole canvas column from the view
//          bar's top row to the canvas's bottom frame row — the view bar's
//          top line and the left column the DARK stroke, the right column
//          and the bottom row the LIGHT, the two-tone corners the ordinary
//          mitre, the two columns ONE UNBROKEN RUN down the view bar's, the
//          ruler's and the marker lane's rows into the canvas ring's own
//          (render_trim_flags, paint_ruler_row, paint_canvas_column_frame);
//          the lanes stand inside it, the brass's two foot lines between
//          them running from column to column. (THE BAND'S LINES, PANES AND
//          VOID — the wood under kWoodVoidLuma's .72 between the panes —
//          THE TAB STRIP with its wood tabs, THE COLUMN'S AIR AND FOOT, THE
//          DOCK BAR, ROW 8'S BARS AND THE BORDER UNDER IT stood on the wood
//          2026-10-21 to 2026-10-22; "remove the darkening b/w tooltips and
//          history".)
//   METAL  AN ACCENT since 2026-10-22 (ONE PANEL from the column's air to
//          the dock bar, window-wide, and the two tabs' bodies 2026-10-21 to
//          2026-10-22; the flat family's grounds 2026-10-21 to 2026-10-22).
//          THE BRASS'S TWO PLACES AND ITS FADES, its only readers: (1) THE
//          MARKER LANE'S GROUND BETWEEN THE WOOD RING'S COLUMNS — the
//          canvas's x extent on the lane's rows, the cue labels' face boxes
//          with it (clipped to the canvas's columns; the flag editor's riding
//          cells the wood past them) — the brass tile as drawn AT THE PHASE
//          (the LOOP below), the one reader of the tile's pixels; (2) THE
//          RULER'S GROUND, its flat `mid`, AND THE TWO FOOT LINES between the
//          ring's columns — the view bar's light line under its field and the
//          ruler's under its ground — as they were ("leave the timestamp lane
//          a flat solid color as you have it"), reading its Face, the lane's
//          mean (the TONE RULE), through set_source's Metal flats
//          (paint_ruler_row, render_trim_flags under the Metal scope); and
//          (3) THE DISABLED CUE'S FADED TRIANGLE, LABEL AND SHADOW, mixed
//          toward its Face, the material they stand on (architect 2026-10-22;
//          render.cpp's fill_program_palette, cool_edit_derive.h's faded
//          look) — the three the Metal Face's only readers. (THE VIEW BAR'S
//          FRAME was the brass's third place until the wood's ring took its
//          columns and top line the same day, WOOD above.)
//   CASE   EVERY CASE (the band's, row 8's, the render player's) AND SINCE
//          2026-10-22 EVERY WORD BUTTON AND EVERY KEY OF THE ON-SCREEN
//          KEYBOARD (architect: "buttons in ebony — yes"; paint_ce_case on
//          their own w x h boxes, the ramp over the seat's height). The
//          case's 20 ramp rows are the ebony UNDER
//          THE LUMINANCE RULE off its own Face with Cool Edit's case span,
//          kCaseRampTop at the top row to kCaseRampBottom at the bottom (HLS
//          lightness added, the saturation kept), its lines flat strokes off
//          that Face (kCaseHighlight, the shadow's two ends kCaseShadowFirst /
//          kCaseShadowLast and the corner kCaseCorner), the black outline Cool
//          Edit's constant. THE PRESSED AND CHECKED FACE (architect
//          2026-10-21, "the top and left should be darker than the case";
//          Cool Edit's constant 808080 / F8F8F8 rings, measured on a light
//          gray case, read as a raised white frame on the ebony): the outer
//          ring black on the top and left and THE CASE'S HIGHLIGHT on the
//          bottom and right, the inner ring THE CASE'S LAST SHADOW TONE on
//          the top and left and nothing on the bottom and right, the ramp one
//          line down to the outer ring there (paint_ce_case). Each seat's
//          patch is the case cover at its own place (the frame's: every seat
//          differs, no hash). A dead glyph is Cool Edit's glyph emboss, ITS
//          TONES THE CASE'S (architect 2026-10-21, "r1"): the silhouette and
//          the rim Cool Edit's measured ratios taken onto this ramp's mean
//          lightness, the light copy F8F8F8 (disabled_glyph_tones, below).
//          (The caption buttons wore the same ebony at a face of their own,
//          their Marlett glyphs the ink, 2026-10-21 to 2026-10-22.)
//   FLAT   NO GROUND SINCE 2026-10-21 (the family went to the brass, and
//          to the wood 2026-10-22).
//          The scope's default still: what stays flat is named by its painter
//          — EVERY FIELD (a dialog editor's line, the lists' ground where you
//          pick, the time fields: the chrome's field pair) and every case's
//          own lines and rings (THE WORD BUTTONS AND THE KEYBOARD'S KEYS were
//          flat raised faces here until 2026-10-22, "ebony eventually, flat
//          fine for now", the ebony's CASE since) — their roles derived by
//          chrome_derive.h's rules FROM THE WOOD'S FACE since 2026-10-22, the
//          metal's 2026-10-21 to 2026-10-22 (the text's swaps following it).
// UNTOUCHED: the well and everything the palette draws (the canvas, the view
// bar's black field and span, the cues' triangles and dots, the playhead),
// every glyph, the cases' black outline; and THE WINDOWS PARTS (2026-10-22,
// render.h's caption block): the caption's flat ground, its three buttons
// and the sizing frame, constants all.
//
// THE TONE RULE (architect 2026-10-21): EACH MATERIAL'S FACE IS ITS BAKED
// COVER'S MEAN COLOR, measured at the bake (materials::face) — THE BRASS'S
// OVER ITS LANE TILE, the strip's center rows the bake reads (2026-10-22;
// over the whole cover's panel rects, the tab strip's top to the canvas's
// top, the canvas's foot to the dock bar and the two margins between,
// 2026-10-21 to 2026-10-22 — 765327 there, 775429 the lane's on the tablet's
// covers), THE WOOD'S OVER THE ROWS ABOVE AND BELOW THE CANVAS COLUMN
// WINDOW-WIDE — the caption and the band above the view bar's top row, row
// 8 below the ring's last row (the simple layout's rows, 2026-10-22: the tab
// strip's and the dock bar's rows were in it until then, so the wood's Face
// moved a little that night) — and THE EBONY'S OVER
// THE FULL-WIDTH ROWS THE PROGRAM'S CASES' PATCHES ARE CUT FROM AND THE
// CAPTION ROW (each seat row's g + lw rows, window-wide; the word buttons'
// and the keys' rows, ebony since 2026-10-22, never in it), NOT OVER THE
// TILES: those
// rows were each material's sample when its tones were judged — the ebony's
// before the patches replaced the seat rows' bands, both before the caption
// left the wood and the ebony (2026-10-22), its rows decoded for the means
// alone since — and measuring them still keeps every tone derived from a
// Face where it shipped (bake_wood, bake_case) — and EVERY TONE A PAINTER PUTS
// DOWN ON A MATERIAL IS COOL EDIT'S TONE RULE (cool_edit_derive.h's kTones:
// HLS L' = a·L + b, the hue and saturation kept; the case's own span above)
// ON THAT FACE, AS A FLAT STROKE — no grain on a line: the 1-W light and
// dark lines of the rings.
// Only the Face tone itself is the cover as drawn. THE ROAD IS ONE
// CHOKEPOINT: the
// painters keep their palette members — every panel tone a tone of the wood
// Face (the metal's 2026-10-21 to 2026-10-22), the palette's `face`
// (render.h's install) — and set_surface_source
// (render.h) knows each flat tone by its bytes (set_tone_face) and swaps the
// source for the scope's material: the Face its cover, the tile under the
// fill's own extents where they meet the client; any other tone that
// material's flat stroke. NO
// PAINT-TIME ALPHA: a mitre's antialiased diagonal is the two sources.
//
// THE BEVEL KEYS (tuning keys with Settings rows, device_config.h): the
// device keys `bevel_light` / `bevel_dark` (percent, 100 the rule exactly):
// every LIGHTER-than-Face tone's per-channel factor f becomes
// 1 + (f − 1)·p / 100 with p the light key, every darker one the same with
// the dark key, on every material's flat strokes (set_bevel) — applied live at
// the OK, recomputing the solid tones alone, never a bake. (The mocks' light
// tones were a multiplicative shortcut that clips, FFD688; the HLS tone
// differing a little from them is the mock's inaccuracy.)
//
// THE INKS (constants all): ONE INK, THE BONE,
// kBoneInk F0F0F0 (architect 2026-10-22: "the flags' F0 works well
// everywhere … I'm committed to F0 in the other places"; the light glyphs on
// the ebony stand for the bone accidentals), ON EVERY WORD AND DIGIT THE
// CHROME PAINTS AND ON THE FLAG LABELS, the words and the flags one ink — the
// caption's title (dulled to render.h's kCaptionInkInactive while the window
// is inactive), the active tab's name in row 8 (2026-10-22; the tab strip's
// labels until then), the ruler's digits and ticks, row 8's state line, THE
// WORDS ON THE EBONY — the word buttons' labels with their
// access-key underlines and the on-screen keyboard's caps (the caption
// buttons' glyphs 2026-10-21 to 2026-10-22: Windows' black since, render.h's
// caption block) — and THE MARKER LANE'S RESTING LABELS (a disabled cue's
// label fading from it toward the brass's Face, the lane's, 2026-10-22; the triangles, the yellow
// head, the dots, the selected label's chrome text unchanged). THE CLOCKS
// ALONE KEEP E6D5A8, the words' ink of 2026-10-21, as their own constant
// while it is under consideration — row 8's clock and the render player's
// two fields, kTimeFieldDigits on kTimeFieldGround (their rings the
// region's tones, geometry and pads unchanged). ONE SHADOW OFFSET, ONE
// SCALED WINDOWS PX (program_line_px — 3 device px at 300 %) EVERYWHERE A
// WORD WEARS ONE (architect 2026-10-22: the tab shadow "looks like one
// device pixel, but I would think that everything should be one Windows
// pixel … that's how the timestamp font offset looks, and the marker font
// as well"; "yes to implement all, incl 1 wpx shadow"): kWordShadow at
// (+d, +d) on the caption's title, the active tab's name and the ebony's
// words (the underline's shadow with its word's), the ruler's digits, the
// state line and the flag labels their own shadows at the same (+d, +d), so
// the name, the timestamp lane and the marker lane read one offset at every
// scale. (The
// words' offset was the `text_shadow` key's device px, 2 at 300 %,
// 2026-10-21 to 2026-10-22; the key is retired, an old line the ordinary
// unknown-key hard fail.) (The old brass C9A961 on the title and the menu
// row's words, the near-white on the tab labels, the ruler, the caption
// glyphs, and the amber F0B030 on the clock, retired 2026-10-21 for one ink
// E6D5A8 on the words, the flags near-white apart; that two-ink split
// retired 2026-10-22.) THE SELECTION PAIR is the constant kSelectionFill
// under kSelectionText, the bone (chrome_derive.h's rows). The cards', the
// tooltip's and the dialogs' text in their boxes keep the chrome's text
// roles. A disabled word's emboss is as before, its light copy the
// material's own light tone (materials::flat) — no word on the ebony is ever
// dead;
// a disabled glyph is Cool Edit's emboss (the caption's Marlett cell
// Windows' DSS_DISABLED emboss since 2026-10-22, render.h's caption block).

enum class GuiMaterial { Wood, Metal, Case };
inline constexpr int kGuiMaterialCount = 3;
// THE DROP NAMES, the masters' file stems, in the enum's order.
inline constexpr const char* kGuiMaterialNames[kGuiMaterialCount] = {
    "Wood", "Metal", "Case"};
// EACH MATERIAL'S COVER HEIGHT, the window's height over this (the head's
// STRIP RULE, architect 2026-10-22, "make the brass image a lot smaller …
// cutting the image in half or a quarter vertically, keeping the same
// length"): the brass a quarter-height strip, the others the whole window.
// tools/tiles/render_covers.py's FRACTION is the same table.
inline constexpr int kGuiMaterialCoverDivisor[kGuiMaterialCount] = {1, 4, 1};

// THE REGION A SURFACE STANDS IN (the head's map), the current one a scope's
// (GuiMaterialScope): which material a flat tone resolves to. Flat the
// default. (CaptionButton, the caption buttons' ebony, 2026-10-21 to
// 2026-10-22, is retired.)
enum class GuiMaterialRegion { Flat, Wood, Metal };

// THE FORTEPIANO CHROME'S CONSTANTS (the frame of record's; 0xRRGGBB, sRGB).
// (kWoodVoidLuma, the band's void's .72, left with the void, 2026-10-22.)
// THE CASE (the generator's ebony_cases_tiled): the ramp's HLS lightness
// offset at its top and bottom rows and the lines' offsets.
inline constexpr double kCaseRampTop       = 0.10;
inline constexpr double kCaseRampBottom    = -0.04;
inline constexpr double kCaseHighlight     = 0.14;
inline constexpr double kCaseShadowFirst   = -0.08;
inline constexpr double kCaseShadowLast    = -0.10;
inline constexpr double kCaseCorner        = -0.10;
// THE BONE INK (architect 2026-10-22: "the flags' F0 works well everywhere
// … I'm committed to F0 in the other places"; "the F0 bone color"): ONE INK,
// the light glyphs on the ebony standing for the bone accidentals, on EVERY
// WORD AND DIGIT THE CHROME PAINTS AND ON THE FLAG LABELS — the caption's
// title, the active tab's name, the ruler's digits and ticks, the state line,
// the word buttons' labels and the keyboard's caps on the ebony, the cue labels
// at rest — over the words' shadow where a word wore it. The words and the
// flags are one ink (the head's INKS), and it is the selection's text.
inline constexpr uint32_t kBoneInk    = 0xF0F0F0;
inline constexpr uint32_t kWordShadow = 0x140C06;
// THE TIME FIELDS' GROUND (black) AND DIGITS: row 8's clock and the render
// player's two fields alone, LEFT ON E6D5A8 (architect 2026-10-22, "let's
// leave the timestamp alone for now": the words' ink of 2026-10-21, kept
// here as the clocks' own constant while it is under consideration — a
// brassy tone was mocked and not picked; no key, no row; the amber F0B030
// until 2026-10-21).
inline constexpr uint32_t kTimeFieldGround = 0x000000;
inline constexpr uint32_t kTimeFieldDigits = 0xE6D5A8;
// THE SELECTION PAIR (architect 2026-10-21, "brass-like … the selection text
// white or near-white"): a constant fill and the bone text everywhere
// the chrome's selected pair paints — the flag editor's selected substring,
// the lit popup rows, the lists' selected rows, the dialog editors'
// selections (chrome_derive.h's selected_fill / selected_text rows; the
// waveform's ink under the program text's other member, 2026-10-16..10-21,
// retired).
inline constexpr uint32_t kSelectionFill = 0x9C7B3A;
inline constexpr uint32_t kSelectionText = kBoneInk;

namespace materials {

// THE LAUNCH'S CHECK (the head): every material has a master or a cover in
// the catalog and every one of its files is a PNG or a JPEG, a cover the
// size its name says — of ANY height, so a strip's `Metal-<W>x<H>.png`
// passes whatever its H (the STRIP RULE: its height against the lane's rows
// is the bake's match, which skips a strip too short and fails the bake
// where none reaches and no master stands). Answers the first error,
// prefixed `tiles: ` and naming the file — gui_main's hard fail.
std::optional<std::string> check();

// THE BAKE, AT ONCE (the head's BAKE RULE): when the key `app` gives
// differs from the baked one, every material is baked again from the catalog
// and the palette reinstalled off the new Faces (install_palette, render.h —
// its generation rebuilds every cached surface the tiles were painted into)
// — the client repainted by the caller's damage, the whole surface by the
// next pre-paint (take_bake_landed). A zero-sized client bakes
// nothing. A FAILURE is the catalog changed under the running process (check
// passed at launch): its stderr line and the hard fail. Callers: the one-act
// roads, GuiInputHandler::apply_gui_scale, apply_strip_pad and
// apply_true_colors.
void ensure(const AppState& app);
// THE BAKE BEHIND THE SETTLE GATE (the head's BAKE RULE): the first bake at
// once; a changed key of a baked window only once the tick has armed it
// (settle_due), else nothing — the paint draws from the last bake's tiles.
// Caller: the pre-paint hook (main.cpp), before the backend paints any
// pixel.
void ensure_settled(const AppState& app);
// WHETHER A BAKE HAS LANDED since the last ask (either road above), the
// flag cleared: the pre-paint hook's question, answered by the whole
// client's damage and the flag cache's rebuild in that frame.
bool take_bake_landed();
// A SIZE CHANGE ARRIVED (GuiPaintHandler::on_resize): the gate's clock
// restarts and any armed bake waits again.
void note_resize();
// THE GATE'S RE-ARM (the loop's tick, main.cpp): true ONCE when a frame has
// found the size moved and kBakeSettle has passed since the last size
// change — the caller then damages the client, whose frame's pre-paint
// hook bakes.
bool settle_due();
// THE TRACE'S GATE, read once: WT_PAINT_TRACE=1 in the environment, or on
// Android, which cannot set one for an app, the system property
// `debug.warptempo.trace` == "1" (`adb shell setprop debug.warptempo.trace
// 1` before a launch); the two lines it opens are the bake's (materials.cpp)
// and the full-window chrome paint's (GuiPaintHandler::on_redraw), stderr,
// logcat on the tablet.
bool trace_enabled();
// Whether a bake has landed (construction state paints flat).
bool baked();
// THE MATERIAL'S FACE, its baked cover's mean, 0xRRGGBB sRGB.
uint32_t face(GuiMaterial m);

// THE BEVEL KEYS (the head), in percent: the solid tones recomputed.
void set_bevel(int light_percent, int dark_percent);
// THE BRASS'S PHASE FOLLOWS THE DISPLAYED BASIS (the head's LOOP): called
// at every publish of the plate's displayed fingerprint (the two sites in
// waveform_cache.cpp, the async swap and the synchronous rebuild) with its
// span and width — the phase advanced by the content's travel in device px,
// (old vp − new vp) / spp, where spp is unchanged, and held where it moved.
void follow_displayed_basis(int64_t vp_start, int64_t vp_end, int area_w);

// THE TONE TABLE: the flat tones a painter hands the surface chokepoint,
// each Cool Edit's tone of THE CHROME'S FACE (the palette's `face`, the
// wood's since 2026-10-22) — the Face itself and the panel tones a relief or a ground puts
// down (kTones' Face tones but the text, the label and the cue's shadow,
// which are ink) — rebuilt by every palette install.
void set_tone_face(uint32_t chrome_face);

// THE SURFACE CHOKEPOINT'S ROAD (render.h's set_surface_source), called with
// the fill's PATH ALREADY BUILT: `c` as a tone of the current region's
// material — the Face its baked tile under the path's extents where they
// meet the client (a path running past the window's edge, a cue label's box
// near it, takes the tile holding its visible part; the material's flat
// mean where no one tile holds that), any other tone the material's flat
// stroke — set as the source; false
// where `c` is no tone, the region is Flat or nothing is baked, the caller
// then setting it flat.
bool set_source(cairo_t* cr, GuiColor c);
// THE FLAT STROKE `c` resolves to in the current region (a tone's, or `c`
// itself): an ink that must wear a material's tone without its cover — a
// disabled word's emboss copies.
GuiColor flat(GuiColor c);
// ONE RECT OF A MATERIAL AS DRAWN, split over its tiles (the canvas
// column's face; client coordinates, the client's pixels alone): each part
// the cover, a part no tile holds the material's flat mean.
void paint_rect(cairo_t* cr, const GuiRect& r, GuiMaterial m);

// THE CASE'S LINES (the head's CASE), flat strokes off the ebony Face under
// the bevel keys.
struct CaseLines {
    GuiColor highlight, shadow_first, shadow_last, corner;
};
CaseLines case_lines();
// THE DISABLED GLYPH'S TONES (architect 2026-10-21, "r1" and his pick v2;
// the rule at cool_edit_paint.h's kCeDisabledShadow): Cool Edit's emboss
// taken onto the live case — the silhouette at the case ramp's mean HLS
// lightness times kCeDisabledShadowRatio (0.84, his pick) on the ebony
// Face's hue and saturation, the rim black (kCeDisabledInset), the light
// copy Cool Edit's F8F8F8 — rebuilt with the flat strokes at every
// bake. 0xRRGGBB. Read by every disabled glyph (icons::draw_disabled), its
// cache keyed on the tones.
struct DisabledGlyphTones {
    uint32_t shadow, light, inset;
    bool operator==(const DisabledGlyphTones&) const = default;
};
DisabledGlyphTones disabled_glyph_tones();
// THE CASE'S RAMP on the w x h glyph seat at (x, y) (paint_ce_case's: square
// on the program's cases, a word button's or a key's its own): the tile
// baked on that seat's rows holding its columns — its own patch, or the
// merged tile over it (`down` the pressed set, one line further in) — a
// copy at the same rectangle; a seat no tile holds (a size change waiting
// on the gate) takes the ramp's twenty flat stops off the ebony Face over
// its height.
void paint_case_ramp(cairo_t* cr, int x, int y, int w, int h, bool down);

// THE RESIDENT BAKES' BYTES (the head's figures).
std::size_t resident_bytes();

} // namespace materials

// THE REGION SCOPE (GuiMaterialRegion): the region in force while it
// stands, the previous one back at its end. Main thread only.
class GuiMaterialScope {
public:
    explicit GuiMaterialScope(GuiMaterialRegion region);
    ~GuiMaterialScope();
    GuiMaterialScope(const GuiMaterialScope&)            = delete;
    GuiMaterialScope& operator=(const GuiMaterialScope&) = delete;

private:
    GuiMaterialRegion previous_;
};
// (THE ORIGIN SCOPE, GuiMaterialOriginScope — the client's origin offset
// under the sizing frame's painter, whose band round the client took the
// brass's tile, 2026-10-21 to 2026-10-22 — is retired with that band.)
