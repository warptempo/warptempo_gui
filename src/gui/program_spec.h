#ifndef WARPTEMPO_GUI_PROGRAM_SPEC_H
#define WARPTEMPO_GUI_PROGRAM_SPEC_H

// THE PROGRAM SPEC (architect 2026-10-09 ~06:55, "chrome means chrome, the
// program is Cool Edit"; the go ~09:40): THE CHROME is the OS part — the
// caption (the menu row under it until 2026-10-21), the window frame, the
// pull-downs, the cards, the
// prompts, the project picker's and the Settings dialogs with their fields
// and buttons — each chrome's own (chrome_spec.h). THE PROGRAM is COOL EDIT PRO
// 2.1'S EDIT VIEW FROM THE TOOLBAR DOWN, THE SAME UNDER EVERY CHROME
// (windows-2000 today, Windows XP's to come). This header is the one owner of the
// program's lengths, in Windows px, read by the lane table (main.cpp), the
// accessors (render.h's program block) and the painters (cool_edit_paint.h);
// its colors are the palette's Face role and the tones Cool Edit derives
// from it (cool_edit_derive.h) beside the painter's own constants.
//
// NO PROPORTIONAL FIT (architect 2026-10-09 ~10:25, "I'd like to see it at
// 20"): every one of Cool Edit's measurements (tmp/research/cool_edit/
// METRICS.md, one capture px = one W) is taken at Cool Edit's own W
// UNSCALED, THE BUTTON CASE INCLUDED — its 20-W glyph seat, the case 23 (1
// highlight + 20 + 1 shadow + 1 black), the band round it 33 (2 head lines
// + 3 face + 23 + 2 face + 3 foot lines) and row 8 32 (4 + 23 + 5), the
// cases' pitch 23. THE REASON: the program is Cool Edit, not a chrome — the
// 24-W seat is ReactOS's large toolbar glyph, the base's seat for a chrome
// vocabulary (chrome_spec.h's head, where it stays the chrome's rule), and
// the program takes no chrome's toolbar; the set's drawings are Adwaita
// 3.22's SVGs or the architect's own 256-px PNGs (the drop road, 2026-10-19,
// icons.h), either rasterised or scaled to the seat's device size, so a
// glyph loses nothing at 20. THE
// SIZE ON THE GLASS is gui_scale's (300 x 24 / 20 = 360 puts the glyph at
// the 72 device px the 24-W seat drew at 300; the device's choice, never
// this header's). (The 24-W seat round a 27-W case, a 37-W band and a 36-W
// row 8 stood for the morning of 2026-10-09; git history.)
//
// THE PROGRAM'S LANES (2026-10-09): THE TOOLBAR BAND (the top strip's lane
// 2, under the program frame's top row, which stands directly under the
// caption since the menu row's removal, 2026-10-21), THE TAB STRIP under it
// (lane 3, 2026-10-18: Cool Edit's Organizer tabs, the two tabs' own lane,
// its fields below) and ROW 8 under its DOCK BAR (the bottom strip's lane 1)
// over COOL EDIT'S BORDER UNDER ROW 8 (bottom lane 0, 2026-10-14, on the
// window's foot since the program frame's bottom row went, 2026-10-21), the
// roster's; EVERY PROGRAM LANE SPANS THE WINDOW since 2026-10-21 (architect:
// "every row grows 1 W px each side" — the program frame's columns, inside
// which they stood one line in from each side since 2026-10-10, are gone); and, from the band down to the well, the CANVAS COLUMN'S THREE
// in ORDER R (architect 2026-10-09 ~09:40, the mock of record
// tmp/mocks/cool_edit/mock_CE_S3_GRID_4.png; its generator mock_ce_s3.py,
// NOTES.md's sets 2 and 3): THE COLUMN'S AIR (5 W of face under the band's
// last line, METRICS §4.1), THE VIEW BAR — the trim bar — (a line, the
// black field, a line: 8), THE RULER FLIPPED (11 rows of ground on the view
// bar's light line, its ticks standing on its bottom row, then its own
// light bottom line: 12) and THE MARKER LANE of Cool Edit's cues (17) —
// the ruler and the lane THE PRODUCT'S ROWS, NOT COOL EDIT'S 17 AND 11
// (architect 2026-10-09 ~16:50, the go ~21:00: the ruler's digits the
// base's six-row small digit with one W of air above them, the rows saved
// given to the marker lane, whose label box is the whole lane — "right now
// it's too small to click"; usability over accuracy, render.h's row-6
// canvas paragraph; the mock of record tmp/mocks/cool_edit/
// mock_CE_S4_TRI_05.png, NOTES.md's set 4); then
// THE CANVAS (architect 2026-10-09, the third part, "accurate to the
// mock-up"; METRICS §4.1, the mock's set 3): its dark top frame row under
// the marker lane (1), the canvas itself — the waveform area, its grid and
// its center lines, its two channels' wells with Cool Edit's stereo divider
// between them since 2026-10-16 (stereo_divider_face_px) — and its light
// bottom frame row and 5 W of face under
// it before the dock bar (the column's foot, 6). THE COLUMN STANDS 6 W IN
// FROM EACH SIDE of the window's client area on the panel's face
// (column_margin_px): the view bar, the ruler and the canvas share its
// columns and its two frame columns — dark on the left, light on the right
// — while the column's air and the marker lane span the window's width on
// the face, as Cool Edit's cue lane does.
//
// THE CUE AND THE PLAYHEAD'S HEAD ARE DRAWN IN QUANTA, NOT IN ROUNDED W
// (2026-10-09): a triangle's 9-7-5-3-1 rows (drawn as their envelope, one
// antialiased triangle, since ~12:40: paint_ce_cue_triangle), its shadow's
// one-quantum offset, the label's lead from the column and the selected
// fill's pads are counted in the quantum u = scaled_px(1, 1) (render.h's
// program_line_px; the dots down the canvas are device rows since 2026-10-09
// ~14:25, the dot fields below) — the playhead head's
// construction since 2026-10-05 — so the apex is the stem's column at every
// scale and the nine units centre on it with no remainder; the LANES' ROWS
// and the text's seats are W lengths, each its own rounded part (scaled_px's
// rule).
struct ProgramSpec {
    // THE CASE (METRICS §1.3): one highlight row and column on the top and
    // left, the glyph seat, one shadow row and column and one black row and
    // column on the right and bottom; cases abut, so the case is the pitch.
    // The glyph's seat is (+1, +1) at rest and (+2, +2) pressed or checked
    // (the shift is one line, cool_edit_paint.h).
    int  case_light_px;
    int  glyph_px;
    int  case_shadow_px;
    int  case_outer_px;
    // COOL EDIT'S TOOLBAR HAS NO HOVER FACE (METRICS §1.5: a capture with the
    // pointer on a button differs from one without only by Wine's tooltip):
    // the roster's and the render player's hot face is never lit, the hover
    // walks storing none (input_pointer.cpp's two writes read this).
    bool case_hot_face;
    // THE TOOLBAR BAND (METRICS §1.1, Cool Edit's 33 whole): two lines at its
    // head (the dark outline, then the panes' top light line), the face above
    // the case, the case, the face below it, three lines at its foot (the
    // panes' bottom mid line, the dark outline, the light line that is also
    // the top of what follows).
    int  band_head_lines;
    int  band_air_above_px;
    int  band_air_below_px;
    int  band_foot_lines;
    // A PANE (METRICS §1.2): its light left column, this much face, its
    // cases, this much face, its mid right column; between two panes a dark
    // column (the left pane's outer edge, cool_edit_paint.h), so the groove
    // reads mid | dark | light — 12 W from one pane's last case to the next
    // pane's first, as Cool Edit's.
    int  pane_lead_px;
    int  pane_trail_px;
    // THE DOCK BAR above row 8 (METRICS §2.2, Cool Edit's y = 915..920): a
    // light row, this much face, a mid row and a dark row; its left column
    // light, its right column mid.
    int  dock_bar_face_px;
    // THE BORDER UNDER ROW 8 (architect 2026-10-14, on his mock over a tablet
    // capture, tmp/modal_arc/mock_main_bottom_border_v2.png; the source Cool
    // Edit's own border under its row 8 on his 1:1 capture
    // tmp/Screenshot_2026-10-14_11-20-10.png): THE DOCK BAR'S OWN SIX ROWS —
    // a light row, this much face, a mid row and a dark row, its left column
    // light and its right column mid — painted by the dock bar's own painter
    // (paint_ce_dock_bar) on a bottom lane of its own under row 8, ON THE
    // WINDOW'S FOOT since 2026-10-21 (architect: the program frame's columns
    // and bottom row removed — "every row grows 1 W px each side"; the
    // frame's 1-W line stood under it, the window's lowest, from 2026-10-14,
    // the border inside the frame's two columns), its dark row the window's
    // lowest line, the window's width like every program lane; its six W
    // came out of the waveform, the canvas being the flex.
    int  bottom_border_face_px;
    // ROW 8 (METRICS §5.1 at one button row: Cool Edit's 55 rows hold two
    // rows of 23 cases; the product's one row of the same 23): the face
    // above the case and below it.
    int  row8_air_above_px;
    int  row8_air_below_px;
    // ROW 8'S BARS (architect 2026-10-21, the fortepiano chrome; the frame of
    // record tmp/theme_next/out/cover_walnut1.png, rows 1305..1440 at 300 %,
    // its generator make_tiles4.py's row8_relayout3): THE GROUPS ARE
    // SEPARATED BY BARS, NO GRIPPERS. A BAR is six W of columns — a light
    // column, `row8_bar_face_px` of face, a mid and a dark column, Cool
    // Edit's end bar taken whole across — and RUNS FROM THE DOCK BAR'S MID
    // ROW THROUGH THE BORDER'S LIGHT ROW, so the vertical bars MERGE INTO
    // THE HORIZONTAL LINES: its columns continue through the dock bar's two
    // lower lines above the row and the border's top line below it, no top
    // or bottom row of its own and no ridge where they meet, EVERY TWO-TONE
    // CORNER THEY LEAVE MITRED — the area between two bars a sunken two-line
    // ring, the last bar's mid and dark columns the right edge of the dock
    // bar, row 8 and the border (architect 2026-10-21, "the top uses
    // diagonal mitres"; paint_row8_joins, paint_handler.cpp, the rule).
    // BARS STAND against the window's left edge, before the clock's group;
    // after the clock's group; before the first right-anchored group; after
    // each group, ADJACENT GROUPS SHARING ONE BAR (no gap between groups);
    // and after Play against the window's right edge (the program frame's
    // columns, until 2026-10-21). THE PADS ARE THE
    // BAND'S ASYMMETRIC PUSH-IN (his word): `row8_bar_to_case_px` of face
    // after a bar, `row8_case_to_bar_px` before one — Cool Edit's measured
    // 5-before / 4-after, the bar's bevel making 4 W before it read as much
    // air as 5 after it ("the Cool Edit row spacing has had a purpose for
    // being asymmetrical", 2026-10-12). Between the clock's group and the
    // right block the dock bar's lines and the border's run on unbroken over
    // the walnut. (COOL EDIT'S OWN GROUPS stood 2026-10-09 to 2026-10-21: a
    // five-column GRIPPER of two etched pairs, 6 W of air, the cases, 4 W
    // and the end bar with its light top and mid bottom rows inside the
    // row's content band, two W of face between groups and before the first,
    // and from 2026-10-14 the last group with no end bar, closing in 5 W of
    // face to the frame's line as Cool Edit's Begin / End / Length group
    // does; git history.)
    int  row8_bar_face_px;
    int  row8_bar_to_case_px;
    int  row8_case_to_bar_px;
    // THE DARK TIME FIELD (METRICS §5.4: 72 x 17, its TL line, its ground,
    // its BR line): its height. Every time field takes the shape — row 8's
    // clock and the render player's position and length — and EVERY TIME
    // FIELD IS ITS RESERVED CELL PLUS THE PAD ON EACH SIDE, no least width
    // (architect 2026-10-10, on the tablet: "the row eight clock has a 85
    // Windows pixel floor — let's remove that floor … the media player's
    // fields look good"; the 85-W floor round row 8's clock, Cool Edit's mock
    // of 2026-10-09, removed 2026-10-10). THE PAD from its outer edge to the
    // reserved cell on each side,
    // `field_pad_px`, IS 5 W, the dialog field's own pad then
    // (kModalFieldPadXPx, which went with that field 2026-10-14, the dialog
    // editors' line standing in the modal family's box since)
    // — the time field in line with the text box (architect 2026-10-09
    // evening, "bring the timestamp in line with the text box used for the
    // regular text boxes"; 2026-10-10: "let's add back one pixel on the left
    // and one on the right side for that input box, which will also change
    // the media player's padding"). The constant stays the program's own,
    // this header owning the program's lengths, read at every time field
    // (paint_handler.cpp's time_field_pad_px). WHY 5 AND NOT 4: the time
    // field's own 4 W of 2026-10-09 evening ("the other ones are Windows
    // chrome whereas this one is a Cool Edit chrome") landed while the 85-W
    // floor still stood round row 8's clock, so he saw no change there;
    // removing the floor on 2026-10-10 then took the clock from the floor's
    // width straight to its cell plus 4 W — a larger step than the one W he
    // asked for — and the pad went back to 5 to undo that half of it. Its
    // history in one clause: Cool Edit's Begin / End / Length field's 8 W
    // (its ink 10 and 12 W in) on 2026-10-09, the dialog field's 5 W that
    // evening, the time field's own 4 W at the session's close, 5 W again
    // since 2026-10-10.
    double field_h_px;
    double field_pad_px;
    // THE STATE LINE'S AIR: its first ink this far past the bar closing the
    // time field's group — the pad after a bar (the assert below; the
    // field's own symmetric air of 2026-10-08 before the bar stood between
    // the field and the line).
    double state_air_px;

    // -- THE CANVAS COLUMN (METRICS §3, §4; NOTES.md sets 2 and 3) ----------
    // THE COLUMN'S AIR: the face between the line above it — the tab
    // strip's page line since 2026-10-18, the band's last line before — and
    // the view bar's top line (METRICS §4.1, Cool Edit's y = 79..83), a lane
    // of its own above the trim lane (main.cpp's lane table).
    int  column_air_px;
    // THE TAB STRIP (architect 2026-10-18, the go "proceed"; the law
    // tmp/Screenshot_2026-10-18_16-07-45.png, Cool Edit Pro 2.1's Organizer
    // tab strip on his Wine 1:1 capture, METRICS §2.3, and the mocks of
    // record tmp/tabs_mock/tabs_v11_A.png / _B.png with their generator
    // make11.py): A TOP LANE OF ITS OWN BETWEEN THE BAND AND THE COLUMN'S
    // AIR — `tab_recess_px` of the dock's RECESS tone under the band's last
    // light line, then THE PAGE'S TOP LIGHT LINE, one line in the hilight
    // tone, the page the column's face below it. Cool Edit's Organizer stands
    // its Files / Effects / Favorites tabs in 32 rows of recess over its page
    // line; the product's 20 W hold the active tab's 18 and two rows of air
    // over it (his mocks, the recess "taken whole" at the tabs' own heights).
    // THE TABS IN IT, in W from the lane's top and left (the page edge is the
    // lane's first column, the window's since 2026-10-21): AN INACTIVE
    // TAB 16 W tall from row `tab_inactive_top_px`, its footprint
    // `tab_lead_px` in from the page edge (Cool Edit's Files at x 7 against
    // the page's x 6), the footprints `tab_gap_px` apart; THE ACTIVE TAB
    // grows `tab_growth_px` each side (its right growth the drop shadow's
    // column) and `tab_inactive_top_px − tab_active_top_px` up over its own
    // footprint, so the gap beside it closes to one W and with the first tab
    // active its light column stands flush on the page edge, the window's
    // edge; the tab line under the unlit tabs runs `tab_line_reach_px`
    // past each end of their footprints. EVERY ONE OF THESE IS ITS OWN
    // ROUNDED PART THROUGH scaled_px, like every chrome length (architect
    // 2026-10-18 ~19:55: "if the gap is supposed to be two Windows pixels,
    // then just make it two Windows pixels … we just follow the scaling. Hit
    // areas are defined by what's painted" — the tabs' press targets are the
    // painted boxes, ce_tab_strip_layout). Inside a tab's edge
    // columns THE LABEL'S CELL — its shaped advance, width_px, never its
    // ink — stands `tab_pad_px` from each, so the cell is centered in the
    // footprint, and the run is drawn from the cell's left edge (architect
    // 2026-10-18 ~20:20, "agree on both"): Cool Edit's Organizer is a
    // Windows tab control padding its text cell 6 px each side; the 6 / 5
    // its capture reads on the INK is MS Sans Serif's bitmap glyph cells
    // carrying their inter-letter space as one blank column on the right — a
    // width of the bitmap face, never copied (HEIGHTS ARE MATCHED, NEVER
    // WIDTHS). The lit tab's frame grows round the FIXED label — the same
    // origin and cell lit or unlit — so its left pad reads one W more than
    // its right, the raised frame's two-column right edge (mid inside the
    // shadow), Windows' own construction and Cool Edit's 7 / 5. The painter
    // is cool_edit_paint.h's paint_ce_tab_strip, the tones
    // cool_edit_derive.h's (the tab table, kTabBodies).
    int  tab_recess_px;
    int  tab_inactive_top_px;
    int  tab_active_top_px;
    int  tab_lead_px;
    int  tab_gap_px;
    int  tab_growth_px;
    int  tab_line_reach_px;
    int  tab_pad_px;
    // THE COLUMN'S SIDE MARGIN (METRICS §4.1, Cool Edit's x = 202..207 of
    // face before the canvas frame's dark column at 208; the mock's FX0 = 6
    // and FX1 = WW − 7): this much face from each side of the window's client
    // area to the column's frame column, the frame column one line, then the
    // column's interior — the view bar's field, the ruler's ground and the
    // canvas. THE COLUMN'S FOOT: this much face under the canvas's light
    // bottom frame row before the dock bar's light row — THE PRODUCT'S OWN
    // 5 W, mirroring the column's air above the view bar (the assert below).
    // Cool Edit has no such run: the 8 rows of face it shows above its dock
    // bar (METRICS §4.1's rows 907..914) are its LOWER CUE LANE, which the
    // product does not have, and the waveform keeps those rows (architect
    // 2026-10-14, "agree").
    int  column_margin_px;
    int  column_foot_px;
    // THE CANVAS'S HORIZONTAL GRID (architect 2026-10-09, the design loop's
    // mock set 3, "grid 4 per half channel"; METRICS §4.4's GrdL): each
    // channel's half height divided into this many, a line at every inner
    // division above and below the zero row (render_canvas, render.h's row-6
    // canvas paragraph).
    int  grid_divisions;
    // THE STEREO DIVIDER'S FACE (architect 2026-10-16, on his Wine captures
    // of Cool Edit Pro 2.1 in stereo, tmp/Screenshot_2026-10-16_15-50-05.png
    // rows 521-523): Cool Edit draws a stereo file as TWO SUNKEN WELLS, one
    // per channel, each its own ring, and between the upper well's light
    // bottom line and the lower well's dark top line THIS MUCH OF THE
    // PANEL'S FACE — the three rows running through the canvas's side frame
    // columns to the column's margins, no separation from the panel. The
    // whole divider is program_stereo_divider_authored_h below; render.h's
    // waveform_divider owns its rows.
    int  stereo_divider_face_px;
    // THE VIEW BAR'S FIELD between its two lines (METRICS §3: Cool Edit's
    // 10; architect 2026-10-09, mock set 1's axis, 6): the trim lane is a
    // line, this field and a line.
    int  view_bar_field_px;
    // THE RULER'S GROUND, its rows 0 .. 10 under the view bar's light line
    // (architect 2026-10-09 ~16:50 / ~21:00: Cool Edit's 17 rows, METRICS
    // §4.4, shrunk to 1 + 6 + 4 — "the timestamps should basically be
    // touching the trim bar — one Windows pixel of empty space above"), then
    // its own light bottom line: ROW 0 AIR; THE DIGITS the base's six-row
    // small digit (GuiFace::Small, gui_font.h — every set's small face, a
    // cell all above the baseline) on rows 1 .. 6, their BASELINE the top of
    // this ground row (7), the black (+1, +1) shadow's last row 7; THE TICKS
    // stand on the ground's bottom row and rise this far into it (minor and
    // major: rows 9 .. 10 and 7 .. 10), a major's top row the shadow's last,
    // the tick painted over it (the mock of record's order, paint_ruler_row).
    int  ruler_ground_px;
    int  ruler_minor_tick_px;
    int  ruler_major_tick_px;
    int  ruler_baseline_px;
    // THE MARKER LANE (architect 2026-10-09 ~16:50 / ~21:00, 17 rows where
    // Cool Edit's has 11, METRICS §4.2 — the six the ruler gave): this many W
    // rows above the cue's triangle, then the triangle's cue_triangle_rows
    // quanta on rows 12 .. 16 ("the triangle height is okay right now"), its
    // apex on the lane's last row; THE LABEL'S BOX — the selected fill, the
    // resting face box, the press target and the flag editor's field — this
    // many rows from the lane's top, THE WHOLE LANE (rows 0 .. 16: "the text
    // box should take up the whole marker lane … right now it's too small to
    // click"; render.h's cue_fill_h_px reads the lane's own composite, so the
    // box is the lane at every scale); THE LABEL'S LINE BOX CENTRED IN THAT
    // BOX (architect 2026-10-09 evening, on the glass at 300 %: the editor's
    // band stood three rows of field above and six below, and he asked for
    // it even; the cap band's centring of ~21:45 before it): no authored
    // baseline row — render.h's
    // cue_baseline_px derives it from the lane's height and the program
    // face's recorded line box (ascent + descent) by the box rule, the box's
    // top plus the ascent, the flag editor's selection band the same box; at
    // 100 % the box on rows 2 .. 13, the cap 7 on rows 5 .. 11 and the
    // baseline the top of row 12, the descenders over the triangle's rows
    // beside it (render.h's asserts, where the face is in scope).
    int  cue_above_triangle_px;
    int  cue_triangle_rows;
    int  cue_fill_px;
    // THE CUE'S HORIZONTAL LENGTHS, IN QUANTA from the marker's column
    // (the head): the label's first column past the column (Cool Edit's
    // stem 416 → label 422), the selected fill's first column (one quantum
    // before the text) and its pad past the text, the gap between two of a
    // marker's label segments' boxes (the bound cells, the history's two
    // halves — the product's own, a segment's text standing the label's
    // lead past the previous one's end). (A label is never clipped: a later
    // cue's face box occludes an earlier label, render.h's overlap rule,
    // 2026-10-10.)
    int  cue_label_lead;
    int  cue_fill_lead;
    int  cue_fill_pad;
    int  cue_segment_gap;
    // THE CANVAS DOTS (METRICS §4.2, §4.3), ONE DEVICE PX SQUARE, COUNTED IN
    // DEVICE ROWS from the canvas's first row (architect 2026-10-09 ~14:25,
    // "on waveform → unscaled": Cool Edit's 1-px dot every 4 rows at 1:1, at
    // every gui_scale; render.h's waveform_line_px and
    // dotted_waveform_line_runs): THE PLAYHEAD'S every `dot_period`
    // rows on its phase; A CUE'S TWO COLORS each every `cue_dot_period` rows
    // (architect 2026-10-09 ~11:50, Cool Edit's two cue colors) — THE RED
    // (`cue`) on `cue_dot_phase` 7, THE BLUE (`range`) on `range_dot_phase`
    // 3, so a point cue's two alternate one dot every `dot_period` rows, BLUE
    // FIRST FROM THE TOP, and a one-color stem has half the dots (render.cpp's
    // resolve_flag_face owns which color each phase wears: the invalid color
    // takes the red's phase, the history's added half its cue red on the
    // blue's, 2026-10-11); the phase-reset lead-in
    // ring's right side takes the same rows and its top and bottom runs the
    // same phases in device columns counted from the reset's own column
    // (paint_phase_reset_overlay_ring, 2026-10-09 ~17:40). THE PHASES ARE THE
    // CAPTURE'S, RE-COUNTED FROM THE CANVAS'S TOP (the captures are the law,
    // 2026-10-09): METRICS gives the red at capture y ≡ 3 and the blue at
    // y ≡ 7 (mod 8), absolute rows, and Cool Edit's canvas starts at y = 108
    // ≡ 4 (mod 8), so from its own first row the red stands on rows ≡ 7 and
    // the blue on rows ≡ 3 (its first dot, y = 111, canvas row 3, is blue);
    // the playhead's y ≡ 1 (mod 4) is canvas row ≡ 1 either way, 108 being
    // ≡ 0 (mod 4). Every cue row is the playhead's period off the playhead's
    // phase, so the two never share a row on one column.
    int  dot_period;
    int  playhead_dot_phase;
    int  cue_dot_period;
    int  cue_dot_phase;
    int  range_dot_phase;
};

inline constexpr ProgramSpec kProgramSpec = {
    .case_light_px      = 1,
    .glyph_px           = 20,
    .case_shadow_px     = 1,
    .case_outer_px      = 1,
    .case_hot_face      = false,
    .band_head_lines    = 2,
    .band_air_above_px  = 3,
    .band_air_below_px  = 2,
    .band_foot_lines    = 3,
    .pane_lead_px       = 5,
    .pane_trail_px      = 4,
    .dock_bar_face_px   = 3,
    .bottom_border_face_px = 3,
    .row8_air_above_px  = 4,
    .row8_air_below_px  = 5,
    .row8_bar_face_px    = 3,
    .row8_bar_to_case_px = 5,
    .row8_case_to_bar_px = 4,
    .field_h_px         = 17.0,
    .field_pad_px       = 5.0,
    .state_air_px       = 5.0,
    .column_air_px         = 5,
    .tab_recess_px         = 20,
    .tab_inactive_top_px   = 4,
    .tab_active_top_px     = 2,
    .tab_lead_px           = 1,
    .tab_gap_px            = 2,
    .tab_growth_px         = 1,
    .tab_line_reach_px     = 1,
    .tab_pad_px            = 6,
    .column_margin_px      = 6,
    .column_foot_px        = 5,
    .grid_divisions        = 4,
    .stereo_divider_face_px = 1,
    .view_bar_field_px     = 6,
    .ruler_ground_px       = 11,
    .ruler_minor_tick_px   = 2,
    .ruler_major_tick_px   = 4,
    .ruler_baseline_px     = 7,
    .cue_above_triangle_px = 12,
    .cue_triangle_rows     = 5,
    .cue_fill_px           = 17,
    .cue_label_lead        = 6,
    .cue_fill_lead         = 5,
    .cue_fill_pad          = 1,
    .cue_segment_gap       = 4,
    .dot_period            = 4,
    .playhead_dot_phase    = 1,
    .cue_dot_period        = 8,
    .cue_dot_phase         = 7,
    .range_dot_phase       = 3,
};

// THE AUTHORED TOTALS, the lane table's record (render.h's
// chrome_stack_authored_h sums them under every chrome).
constexpr int program_case_authored_px(const ProgramSpec& p) {
    return p.case_light_px + p.glyph_px + p.case_shadow_px + p.case_outer_px;
}
constexpr int program_band_authored_h(const ProgramSpec& p) {
    return p.band_head_lines + p.band_air_above_px +
           program_case_authored_px(p) + p.band_air_below_px +
           p.band_foot_lines;
}
constexpr int program_dock_bar_authored_h(const ProgramSpec& p) {
    return 1 + p.dock_bar_face_px + 1 + 1;
}
constexpr int program_bottom_border_authored_h(const ProgramSpec& p) {
    return 1 + p.bottom_border_face_px + 1 + 1;
}
constexpr int program_row8_authored_h(const ProgramSpec& p) {
    return p.row8_air_above_px + program_case_authored_px(p) +
           p.row8_air_below_px;
}
// The canvas column's lanes above the canvas (the head): the view bar a
// line, its field and a line; the ruler its ground and its bottom line; the
// marker lane its rows above the triangle and the triangle's quanta (one W
// each at 100 %); the canvas's top frame row one line. Below the canvas, the
// column's foot: the bottom frame row and its face.
constexpr int program_view_bar_authored_h(const ProgramSpec& p) {
    return 1 + p.view_bar_field_px + 1;
}
constexpr int program_ruler_authored_h(const ProgramSpec& p) {
    return p.ruler_ground_px + 1;
}
constexpr int program_marker_lane_authored_h(const ProgramSpec& p) {
    return p.cue_above_triangle_px + p.cue_triangle_rows;
}
constexpr int program_column_authored_h(const ProgramSpec& p) {
    return p.column_air_px + program_view_bar_authored_h(p) +
           program_ruler_authored_h(p) + program_marker_lane_authored_h(p) +
           1;
}
// THE TAB STRIP (2026-10-18): the recess and the page's top light line.
constexpr int program_tab_strip_authored_h(const ProgramSpec& p) {
    return p.tab_recess_px + 1;
}
constexpr int program_column_foot_authored_h(const ProgramSpec& p) {
    return 1 + p.column_foot_px;
}
// The stereo divider inside the canvas: the upper well's light bottom line,
// the face, the lower well's dark top line, one W each (the field's head).
constexpr int program_stereo_divider_authored_h(const ProgramSpec& p) {
    return 1 + p.stereo_divider_face_px + 1;
}
static_assert(program_case_authored_px(kProgramSpec) == 23);
static_assert(program_band_authored_h(kProgramSpec) == 33);
static_assert(program_dock_bar_authored_h(kProgramSpec) == 6);
// THE BORDER UNDER ROW 8 IS THE DOCK BAR'S STACK EXACTLY (its field's
// ruling), so the dock bar's painter draws it.
static_assert(kProgramSpec.bottom_border_face_px ==
              kProgramSpec.dock_bar_face_px);
static_assert(program_bottom_border_authored_h(kProgramSpec) == 6);
static_assert(program_row8_authored_h(kProgramSpec) == 32);
// ROW 8'S BAR (2026-10-21): a light column, the face, a mid and a dark
// column, 6 W across — the frame of record's 18 px at 300 %; the state
// line's first ink stands the pad after a bar past the clock group's.
constexpr int program_row8_bar_authored_w(const ProgramSpec& p) {
    return 1 + p.row8_bar_face_px + 1 + 1;
}
static_assert(program_row8_bar_authored_w(kProgramSpec) == 6);
static_assert(kProgramSpec.state_air_px == kProgramSpec.row8_bar_to_case_px);
static_assert(program_view_bar_authored_h(kProgramSpec) == 8);
static_assert(program_ruler_authored_h(kProgramSpec) == 12);
static_assert(program_marker_lane_authored_h(kProgramSpec) == 17);
static_assert(program_column_authored_h(kProgramSpec) == 43);
static_assert(program_column_foot_authored_h(kProgramSpec) == 6);
static_assert(program_tab_strip_authored_h(kProgramSpec) == 21);
// THE TAB STRIP'S ROWS (2026-10-18): an inactive tab is 16 W tall from its
// top row to the recess's last — its light top row and a 15-row body — and
// the active tab 18, its face running on through the page line; a tab's
// width is its label's cell (its advance) plus 14 W (the two edge columns
// and the two pads, 2026-10-18 ~20:20).
static_assert(kProgramSpec.tab_recess_px - kProgramSpec.tab_inactive_top_px ==
              16);
static_assert(kProgramSpec.tab_recess_px - kProgramSpec.tab_active_top_px ==
              18);
// The lit first tab stands on the page edge: its growth is the lead; the
// tab line's run reaches past an unlit footprint by the growth, so it meets
// a lit neighbour's light column across the gap.
static_assert(kProgramSpec.tab_growth_px == kProgramSpec.tab_lead_px &&
              kProgramSpec.tab_line_reach_px == kProgramSpec.tab_growth_px &&
              kProgramSpec.tab_gap_px == 2 * kProgramSpec.tab_growth_px);
static_assert(1 + 2 * kProgramSpec.tab_pad_px + 1 == 14);
static_assert(program_stereo_divider_authored_h(kProgramSpec) == 3);
// The column's foot mirrors its air — the product's own 5 W under the
// canvas's bottom frame row against Cool Edit's 5 rows of face above the
// view bar (METRICS §4.1); Cool Edit's lower cue lane, its 8 rows above the
// dock bar, the product does not have (column_foot_px, architect 2026-10-14).
static_assert(kProgramSpec.column_foot_px == kProgramSpec.column_air_px);
// THE RULER'S ROWS (2026-10-09 ~21:00): the six the ruler gave are the six
// the marker lane took, so the column and every stack keep their totals.
static_assert(program_ruler_authored_h(kProgramSpec) +
                  program_marker_lane_authored_h(kProgramSpec) ==
              18 + 11);
// THE MAJOR TICK'S TOP ROW IS THE DIGITS' SHADOW'S LAST (the black shadow,
// one row under the digits' rows 1 .. 6, ends on row 7), the tick painted
// over it; the
// PLAYHEAD'S HEAD, the cue's triangle on the ground's last five rows (6 ..
// 10), reaches the digits' last row (6) and covers what stands under it
// (the mock of record). The digits' own rows — one W of air, then the small
// face's six — are render.h's assert, where the face is in scope.
static_assert(kProgramSpec.ruler_ground_px - kProgramSpec.ruler_major_tick_px ==
              kProgramSpec.ruler_baseline_px);
static_assert(kProgramSpec.ruler_ground_px - kProgramSpec.cue_triangle_rows ==
              kProgramSpec.ruler_baseline_px - 1);
// THE CUE'S ROWS (the product's lane): the triangle on rows 12 .. 16, the
// label's box the whole lane, the label's line box centred in it (render.h's
// asserts read the program face's recorded line box, cap and descent against
// the lane: the cap on rows 5 .. 11, the descent ending above the last row).
static_assert(kProgramSpec.cue_fill_px ==
              program_marker_lane_authored_h(kProgramSpec));
static_assert(kProgramSpec.cue_label_lead >
              (2 * kProgramSpec.cue_triangle_rows - 1) / 2 + 1);
// The cue's two colors alternate one dot every playhead period, and neither
// shares a row with the playhead's dots.
static_assert(kProgramSpec.cue_dot_period == 2 * kProgramSpec.dot_period);
static_assert((kProgramSpec.range_dot_phase - kProgramSpec.cue_dot_phase +
               kProgramSpec.cue_dot_period) % kProgramSpec.cue_dot_period ==
              kProgramSpec.dot_period);
static_assert(kProgramSpec.cue_dot_phase % kProgramSpec.dot_period !=
              kProgramSpec.playhead_dot_phase);
// The label's text stands one pad quantum inside its fill: the fill's lead
// and pad are the label's lead (render.cpp's cue_segment_boxes).
static_assert(kProgramSpec.cue_label_lead ==
              kProgramSpec.cue_fill_lead + kProgramSpec.cue_fill_pad);

#endif // WARPTEMPO_GUI_PROGRAM_SPEC_H
