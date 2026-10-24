#ifndef WARPTEMPO_GUI_PROGRAM_SPEC_H
#define WARPTEMPO_GUI_PROGRAM_SPEC_H

// THE PROGRAM SPEC (architect 2026-10-09 ~06:55, "chrome means chrome, the
// program is Cool Edit"; the go ~09:40): THE CHROME is the OS part — the
// caption and the window frame (labwc's since 2026-10-24; the menu row
// under the caption until 2026-10-21), the
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
// highlight + 20 + 1 shadow + 1 black), the cases' pitch 23. THE REASON: the
// program is Cool Edit, not a chrome — the 24-W seat is ReactOS's large
// toolbar glyph, the base's seat for a chrome vocabulary (chrome_spec.h's
// head, where it stays the chrome's rule), and the program takes no
// chrome's toolbar; the set's drawings are Adwaita 3.22's SVGs or the
// architect's own 256-px PNGs (the drop road, 2026-10-19, icons.h), either
// rasterised or scaled to the seat's device size, so a glyph loses nothing
// at 20. THE SIZE ON THE GLASS is gui_scale's (300 x 24 / 20 = 360 puts the
// glyph at the 72 device px the 24-W seat drew at 300; the device's choice,
// never this header's).
//
// THE SIMPLE LAYOUT (architect 2026-10-22, "the bevels are def part of the
// problem … redesign with dead simple layout"; the frame of record
// tmp/theme_next/out/layout4/layout4_pad13.png, the modals
// tmp/theme_next/out/screens/, both at 300 %): ONE SUNKEN RING round the
// canvas column and one round row 8's clock are the program's only strokes
// on the ground (the hi-fi skin's brushed panel since 2026-10-23, every
// stroke one device px, render.h's line rule). THE PROGRAM'S LANES, top to
// bottom: THE TOOLBAR BAND at the window's top on both devices (under the
// laptop's caption until 2026-10-24) — THE LOGO LANE (its upper pad since
// 2026-10-24, render.h's logo lane block), the key, THE STRIP PAD (13 W,
// settled 2026-10-23: strip_pad_px's field below; the keys' legends in the
// bottom pad), its keys in plain runs
// on the ground, the runs `run_gap_px` apart; then THE CANVAS COLUMN inside
// THE PROGRAM RING — THE VIEW BAR (the trim bar: the ring's dark top row,
// the black field, the light line: 8), THE
// RULER FLIPPED (11 rows of ground under the view bar's light line, its
// ticks standing on its bottom row, then its own light bottom line: 12) and
// THE MARKER LANE of Cool Edit's cues (17) — the ruler and the lane THE
// PRODUCT'S ROWS, NOT COOL EDIT'S 17 AND 11 (architect 2026-10-09 ~16:50,
// the go ~21:00: the ruler's digits the base's six-row small digit with one
// W of air above them, the rows saved given to the marker lane, whose label
// box is the whole lane — "right now it's too small to click"; usability
// over accuracy, render.h's row-6 canvas paragraph; the mock of record
// tmp/mocks/cool_edit/mock_CE_S4_TRI_05.png) — then THE CANVAS: its dark top
// frame row under the marker lane (1), the canvas itself — the waveform
// area, its grid and its center lines, its two channels' wells with Cool
// Edit's stereo divider between them (stereo_divider_face_px) — and its
// light bottom frame row (1), the ring's last row; then ROW 8, THE STRIP
// PAD, the case, THE STRIP PAD, on the window's foot. THE COLUMN STANDS 6 W
// IN FROM EACH SIDE of the window's client area on the ground
// (column_margin_px): the view bar, the ruler, the marker lane and the
// canvas share the ring's two columns — dark on the left, light on the
// right. (The band's head and foot lines and its panes, the tab strip under
// it, the column's air, the column's foot, the dock bar, row 8's bars and
// the border under row 8 stood 2026-10-09 to 2026-10-22; git history.)
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
    // THE STRIPS' RUNS (architect 2026-10-22, the simple layout): the band's
    // and row 8's keys stand in PLAIN RUNS on the ground, the keys abutting
    // inside a run and THIS MUCH PLAIN GROUND BETWEEN TWO RUNS — row 8's group
    // gap as the bars left it (a 6-W bar between a 5-W and a 4-W pad), the
    // band's between its three left runs (layout4: "add a spacing after
    // render, and after flatten").
    int  run_gap_px;
    // THE STRIP PAD (architect 2026-10-22, the simple layout; SETTLED AT 13
    // 2026-10-23 — "I never asked for that strip pad number … I don't think
    // I needed to change that number", the legends' vertical seat "seems to
    // be perfect"; the mocks ran 8 .. 15, layout4_pad13 the pick): THIS MUCH
    // GROUND ABOVE AND BELOW THE KEY on both strips, the band's and row 8's
    // (and the modal controls row's, row 8's own rows), the keys' printed
    // legends standing in the bottom pad (paint_handler.cpp's
    // key_legend_seat). A settled design is a constexpr: the `strip_pad`
    // device key and its Settings row, 2026-10-22 to 2026-10-23, are
    // removed, an old `strip_pad=` line the ordinary unknown-key hard fail.
    // render.h's strip_pad_px the one road to device px.
    int  strip_pad_px;
    // THE STRIPS' ENDS ARE THE PROGRAM RING'S OUTER COLUMNS, NEVER THE
    // WINDOW'S EDGES (architect 2026-10-23, "yes" to tying both strips' ends
    // to the ring; render.h's program_ring_columns the one owner), so the
    // strips and the ring coincide at every scale whatever the grid floor's
    // leftover. BOTH STRIPS ARE FLUSH WITH THE RING at both ends (architect
    // 2026-10-23, "shouldn't row 8 take the same rule as top strip?" — yes):
    // the band's first case and row 8's clock field start on the ring's
    // outer left column, the band's right run and row 8's block end one past
    // its outer right column — the modals' controls row's seat
    // (paint_handler.cpp's icon_row_seats for the band, bottom_row_seats for
    // row 8 and the controls row), so neither strip nor the controls row
    // carries a field here.
    // (THE DARK TIME FIELD'S 17-W HEIGHT AND 5-W PAD — Cool Edit's
    // METRICS §5.4 field, then the dialog field's pad, 2026-10-09 to
    // 2026-10-23 — left this spec with the hi-fi skin: the field's 40 and
    // 12 px at 300 % are paint_handler.cpp's kTimeFieldHeightAt300 /
    // kTimeFieldPadAt300, authored at the tablet's scale. Every time field is
    // still its reserved cell plus the pad each side, no least width —
    // architect 2026-10-10, the 85-W floor round row 8's clock removed.)
    // THE ACTIVE TAB'S NAME (architect 2026-10-22: the tab strip goes, "Tab
    // A" / "Tab B" stands on the plain wood right of the clock): its first
    // ink this far past the clock ring's light column. THE STATE LINE'S AIR:
    // its first ink this far past the name's advance (the pad that stood
    // after the clock group's closing bar, 2026-10-21).
    double tab_name_air_px;
    double state_air_px;

    // -- THE CANVAS COLUMN (METRICS §3, §4; NOTES.md sets 2 and 3) ----------
    // THE COLUMN'S SIDE MARGIN (METRICS §4.1, Cool Edit's x = 202..207 of
    // face before the canvas frame's dark column at 208; the mock's FX0 = 6
    // and FX1 = WW − 7): this much ground from each side of the window's
    // client area to the program ring's column, the column's slot one line
    // (its line one device px on the slot's outer edge, 2026-10-23), then the
    // column's interior — the view bar's field, the ruler's ground, the
    // marker lane's brighter strip and the canvas.
    int  column_margin_px;
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
    // its own light bottom line: THE DIGITS the ruler face (GuiFace::Small,
    // gui_font.h's type table: 16 px at 300 % since 2026-10-24, "R16", its
    // ink from just inside row 3 down to the baseline at 300 %; the base's
    // six-row digit on rows 1 .. 6 under row 0's air 2026-10-09 to
    // 2026-10-23), their BASELINE the top of this ground row (7), the black
    // (+1, +1) shadow reaching row 7; THE TICKS
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
    .run_gap_px         = 15,
    .strip_pad_px       = 13,
    .tab_name_air_px    = 5.0,
    .state_air_px       = 5.0,
    .column_margin_px      = 6,
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
// ROW 8 (2026-10-22, the simple layout): the strip pad, the case, the
// strip pad (strip_pad_px's field, 13 since 2026-10-23).
constexpr int program_strip_authored_h(const ProgramSpec& p) {
    return p.strip_pad_px + program_case_authored_px(p) + p.strip_pad_px;
}
// THE BAND'S AUTHORED PART (architect 2026-10-24): the case and the strip
// pad under it. Its upper pad is THE LOGO LANE, authored in device px at
// 300 % like the skin's other lengths (render.h's logo lane block, logo_lane_h_px), so
// it is not a Windows-px term here (the band was row 8's strip 2026-10-22
// to 2026-10-24).
constexpr int program_band_authored_h(const ProgramSpec& p) {
    return program_case_authored_px(p) + p.strip_pad_px;
}
// The canvas column's lanes above the canvas (the head): the view bar a
// line, its field and a line; the ruler its ground and its bottom line; the
// marker lane its rows above the triangle and the triangle's quanta (one W
// each at 100 %); the canvas's top frame row one line. Below the canvas, its
// bottom frame row, one line, the program ring's last row.
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
    return program_view_bar_authored_h(p) + program_ruler_authored_h(p) +
           program_marker_lane_authored_h(p) + 1;
}
constexpr int program_canvas_bottom_authored_h(const ProgramSpec&) {
    return 1;
}
// The stereo divider inside the canvas: the upper well's light bottom line,
// the face, the lower well's dark top line, one W each (the field's head).
constexpr int program_stereo_divider_authored_h(const ProgramSpec& p) {
    return 1 + p.stereo_divider_face_px + 1;
}
static_assert(program_case_authored_px(kProgramSpec) == 23);
// THE FRAME OF RECORD'S STRIPS (layout4_pad13): 13 + 23 + 13; the band's
// authored part under its logo lane 23 + 13.
static_assert(program_strip_authored_h(kProgramSpec) == 49);
static_assert(program_band_authored_h(kProgramSpec) == 36);
// ROW 8'S GROUP GAP IS THE ONE THE BARS LEFT (2026-10-22): a 6-W bar between
// the 5-W pad after a bar and the 4-W pad before one (run_gap_px's field).
static_assert(kProgramSpec.run_gap_px == 6 + 5 + 4);
static_assert(program_view_bar_authored_h(kProgramSpec) == 8);
static_assert(program_ruler_authored_h(kProgramSpec) == 12);
static_assert(program_marker_lane_authored_h(kProgramSpec) == 17);
static_assert(program_column_authored_h(kProgramSpec) == 38);
static_assert(program_stereo_divider_authored_h(kProgramSpec) == 3);
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
