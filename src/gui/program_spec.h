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
// 20"): every one of Cool Edit's measurements (his Cool Edit
// metrics, one capture px = one W) is taken at Cool Edit's own W
// UNSCALED, THE BUTTON CASE INCLUDED — its 20-W glyph seat, the case 23 (1
// highlight + 20 + 1 shadow + 1 black), the cases' pitch 23. THE REASON: the
// program is Cool Edit, not a chrome — the 24-W seat is ReactOS's large
// toolbar glyph, the base's seat for a chrome vocabulary (chrome_spec.h's
// head, where it stays the chrome's rule), and the program takes no
// chrome's toolbar. (No key draws a glyph since the hi-fi skin, 2026-10-23:
// the seat is the key's middle slot alone.) THE SIZE ON THE GLASS is gui_scale's (300 x 24 / 20 = 360 puts the
// glyph at the 72 device px the 24-W seat drew at 300; the device's choice,
// never this header's).
//
// THE SIMPLE LAYOUT (architect 2026-10-22, "the bevels are def part of the
// problem … redesign with dead simple layout", the mocks at 300 %): ONE SUNKEN RING round the
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
// the black field, the light line: 1 + 5 + 1 = 7 W, 21 rows at 300 % —
// the field view_bar_field_at300's 15 px), THE
// RULER FLIPPED (11 rows of ground under the view bar's light line, its
// ticks standing on its bottom row, then its own light bottom line: 12) and
// THE MARKER LANE of Cool Edit's cues (17) — the ruler and the lane THE
// PRODUCT'S ROWS, NOT COOL EDIT'S 17 AND 11 (architect 2026-10-09 ~16:50,
// the go ~21:00: the ruler's digits the base's six-row small digit with one
// W of air above them, the rows saved given to the marker lane, whose label
// box is the whole lane — "right now it's too small to click"; usability
// over accuracy, render.h's row-6 canvas paragraph) — then THE CANVAS: its dark top
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
// THE CUE AND THE PLAYHEAD'S HEAD ARE DRAWN IN UNITS, NOT IN ROUNDED W,
// TWO UNITS WITH ONE OWNER EACH (render.h's cue_triangle_unit_px and
// cue_label_unit_px): THE TRIANGLE'S UNIT IS ONE DEVICE PX AT EVERY SCALE
// (architect 2026-10-26: "I think we should make the triangle smaller …
// Why not simply leave unscaled? That would preserve the stem/flag size
// relation from CEP" — "on waveform → unscaled" carried to the cue's
// triangle), so a triangle's whole cells — Cool Edit's aliased staircase,
// paint_ce_cue_triangle — stand beside the one-device-px stem, the apex the
// stem's column and every row centred on it with no remainder. ITS ROW
// COUNT IS NO FIELD HERE: it is a skin length, 8 DEVICE PX AT 300 % through
// skin_px, rounded at the element (architect 2026-10-26 ~13:40, "why not
// just simple scale? tablet is only real surface"; render.h's
// cue_triangle_rows and kCueTriangleRowsAt300, the one owner — 8 rows at
// 300 %, 5 at 200 %, 4 at 150 % and 138 %), no whole count of W. THE
// LABEL'S UNIT IS THE SCALED QUANTUM
// u = scaled_px(1, 1) (render.h's program_line_px), the selected fill's pads,
// the segment gap and the shadows' offset counted in it — the label's and,
// since 2026-10-26 ~13:05, the triangle's, one quantum right and down —
// because the text they place is scaled; THE LABEL'S LEAD from the column
// is THE TRIANGLE'S REACH RIGHT PLUS A GAP OF 8 DEVICE PX AT 300 %, scaled
// (render.h's cue_label_lead_px and kCueLabelGapAt300, architect
// 2026-10-26 ~12:40: the label follows the rows; settled ~13:05), a skin
// length rather than a field here, being no whole count of W or quanta. The dots down the canvas
// are device rows (2026-10-09 ~14:25, the dot fields below). The LANES'
// ROWS and the text's seats are W lengths, each its own rounded part
// (scaled_px's rule).
struct ProgramSpec {
    // THE CASE: one highlight row and column on the top and
    // left, the glyph seat, one shadow row and column and one black row and
    // column on the right and bottom; cases abut, so the case is the pitch.
    // The glyph's seat is (+1, +1) at rest and (+2, +2) pressed or checked
    // (the shift is one line, cool_edit_paint.h).
    int  case_light_px;
    int  glyph_px;
    int  case_shadow_px;
    int  case_outer_px;
    // COOL EDIT'S TOOLBAR HAS NO HOVER FACE (a capture with the
    // pointer on a button differs from one without only by Wine's tooltip):
    // the roster's and the render player's hot face is never lit, the hover
    // walks storing none (input_pointer.cpp's two writes read this).
    bool case_hot_face;
    // THE STRIPS' RUNS (architect 2026-10-22, the simple layout): the band's
    // and row 8's keys stand in PLAIN RUNS on the ground, the keys abutting
    // inside a run and THIS MUCH PLAIN GROUND BETWEEN TWO RUNS — row 8's group
    // gap as the bars left it (a 6-W bar between a 5-W and a 4-W pad), the
    // band's between its three left runs (architect: "add a spacing after
    // render, and after flatten").
    int  run_gap_px;
    // THE STRIP PAD (architect 2026-10-22, the simple layout; SETTLED AT 13
    // 2026-10-23 — "I never asked for that strip pad number … I don't think
    // I needed to change that number", the legends' vertical seat "seems to
    // be perfect"; the mocks ran 8 .. 15, 13 the pick): THIS MUCH
    // GROUND ABOVE AND BELOW THE KEY on row 8 and the modal controls row
    // (row 8's own rows), and BELOW THE KEY on the band, whose upper pad is
    // the logo lane (2026-10-24, render.h's logo lane block); the keys'
    // printed legends stand in the bottom pad (paint_handler.cpp's
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
    // measured field, then the dialog field's pad, 2026-10-09 to
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

    // -- THE CANVAS COLUMN ----------------------------------------------------
    // THE COLUMN'S SIDE MARGIN (Cool Edit's x = 202..207 of
    // face before the canvas frame's dark column at 208; the mock's FX0 = 6
    // and FX1 = WW − 7): this much ground from each side of the window's
    // client area to the program ring's column, the column's slot one line
    // (its line one device px on the slot's outer edge, 2026-10-23), then the
    // column's interior — the view bar's field, the ruler's ground, the
    // marker lane's brighter strip and the canvas.
    int  column_margin_px;
    // THE CANVAS'S HORIZONTAL GRID (architect 2026-10-09, the design loop's
    // third mock set, "grid 4 per half channel"; Cool Edit's GrdL): each
    // channel's half height divided into this many, a line at every inner
    // division above and below the zero row (render_canvas, render.h's row-6
    // canvas paragraph).
    int  grid_divisions;
    // THE STEREO DIVIDER'S FACE (architect 2026-10-16, on his Wine captures
    // of Cool Edit Pro 2.1 in stereo, rows 521-523 of the capture): Cool Edit draws a stereo file as TWO SUNKEN WELLS, one
    // per channel, each its own ring, and between the upper well's light
    // bottom line and the lower well's dark top line THIS MUCH OF THE
    // PANEL'S FACE — the three rows running through the canvas's side frame
    // columns to the column's margins, no separation from the panel. The
    // whole divider is program_stereo_divider_authored_h below; render.h's
    // waveform_divider owns its rows.
    int  stereo_divider_face_px;
    // THE VIEW BAR'S FIELD between its two lines, IN DEVICE PX AT 300 %
    // (architect 2026-10-24, his pick T15 off a one-step series 18 .. 12 on
    // the live lane — "I think 15 works well … looking at the Cool Edit
    // original there is a similar relationship where the timestamp numbers
    // fit just about inside the view lane"; his T15 mock):
    // the trim lane is a line, this field and a line. THE
    // RULING IS THE 300 % NUMBER, read on the hi-fi skin's road
    // (render.h's view_bar_field_h_px through skin_px, floor 1: 15 at 300 %,
    // 10 at 200 %, 8 at 150 %); the spec owns it because the authored sums
    // below read it, as 15 / 3 = 5 W exactly (the static_assert there). The
    // field was 18 px at 300 % 2026-10-09 to 2026-10-24 (the first mock set's axis,
    // under Cool Edit's own 30).
    int  view_bar_field_at300;
    // THE RULER'S GROUND, its rows 0 .. 10 under the view bar's light line
    // (architect 2026-10-09 ~16:50 / ~21:00: Cool Edit's 17 rows,
    // shrunk to 1 + 6 + 4 — "the timestamps should basically be
    // touching the trim bar — one Windows pixel of empty space above"), then
    // its own light bottom line: THE DIGITS the ruler face (GuiFace::Small,
    // gui_font.h's type table: 16 px at 300 % since 2026-10-24, "R16", its
    // ink from just inside row 3 down to the baseline at 300 %; the base's
    // six-row digit on rows 1 .. 6 under row 0's air 2026-10-09 to
    // 2026-10-23), their BASELINE the top of this ground row (7), no shadow
    // under them (architect 2026-10-25); THE TICKS
    // stand on the ground's bottom row and rise this far into it (minor and
    // major: rows 9 .. 10 and 7 .. 10), a major's top row the baseline's,
    // the tick painted over a digit's overshoot there (paint_ruler_row's
    // order).
    int  ruler_ground_px;
    int  ruler_minor_tick_px;
    int  ruler_major_tick_px;
    int  ruler_baseline_px;
    // THE MARKER LANE (architect 2026-10-09 ~16:50 / ~21:00, 17 rows where
    // Cool Edit's has 11 — the six the ruler gave): COMPOSED OF
    // `cue_lane_ground_px` W rows and `cue_lane_quanta` label quanta, each
    // part rounded on its own (render.h's marker_lane_h_px: 51 device rows
    // at 300 %, 22 at 138 %, 63 at 360 %), THE LANE KEEPING ITS ROWS AT EVERY
    // SCALE when the triangle left the quantum (2026-10-26); THE TRIANGLE
    // its rows in DEVICE ROWS at the lane's bottom (render.h's
    // cue_triangle_rows: 8 at 300 %, 5 at 200 %, 4 at 138 %), its apex
    // on the lane's last row, the rows above it the remainder (the unit at
    // the head); THE LABEL'S BOX — the selected fill, the
    // resting face box, the press target and the flag editor's field — this
    // many rows from the lane's top, THE WHOLE LANE (rows 0 .. 16: "the text
    // box should take up the whole marker lane … right now it's too small to
    // click"; render.h's cue_fill_h_px reads the lane's own composite, so the
    // box is the lane at every scale); THE LABEL'S CAP BAND CENTRED IN THAT
    // BOX (architect 2026-10-26 ~11:30, "check the text vertical alignment —
    // consider that we only use a narrow range of chars"): no authored
    // baseline row — render.h's cue_baseline_px derives it from the lane's
    // height and the program face's cap band, and the flag editor's
    // selection band, the face's line box, stands from that baseline; at
    // 300 % the baseline row 33 of the 51, the box on rows 13 .. 37, the
    // descenders over the triangle's rows beside it.
    int  cue_lane_ground_px;
    int  cue_lane_quanta;
    int  cue_fill_px;
    // THE CUE'S HORIZONTAL LENGTHS, IN LABEL QUANTA (the head): the
    // selected fill's pad either side of the text — the fill's first column
    // one pad before the text, so THE FILL'S LEAD IS THE LABEL'S LEAD LESS
    // ONE PAD (render.h's cue_fill_lead_px; the label's lead itself THE
    // TRIANGLE'S REACH RIGHT PLUS A GAP OF 8 DEVICE PX AT 300 %, architect
    // 2026-10-26 ~12:40, settled ~13:05 — 19 device px at 300 %, 12 at
    // 200 %, render.h's cue_label_lead_px) — and the gap
    // between two of a marker's label
    // segments' boxes (the bound cells, the history's two halves — the
    // product's own, a segment's text standing the gap and two pads past
    // the previous one's end). (A label is never clipped: a later cue's face
    // box occludes an earlier label, render.h's overlap rule, 2026-10-10.)
    int  cue_fill_pad;
    int  cue_segment_gap;
    // THE CANVAS DOTS, ONE DEVICE PX SQUARE, COUNTED IN
    // DEVICE ROWS from the canvas's first row (architect 2026-10-09 ~14:25,
    // "on waveform → unscaled": Cool Edit's 1-px dot every 4 rows at 1:1, at
    // every gui_scale; render.h's waveform_line_px and
    // dotted_waveform_line_runs): THE PLAYHEAD'S every `dot_period`
    // rows on its phase; A CUE'S TWO COLORS each every `cue_dot_period` rows
    // (architect 2026-10-09 ~11:50, Cool Edit's two cue colors) — THE RED
    // (`cue`) on `cue_dot_phase` 7, THE BLUE (`range`) on `range_dot_phase`
    // 3, so a point cue's two alternate one dot every `dot_period` rows, BLUE
    // FIRST FROM THE TOP, every live cue's (render.cpp's resolve_flag_face
    // owns which color each phase wears); the phase-reset lead-in
    // ring's right side takes the same rows and its top and bottom runs the
    // same phases in device columns counted from the reset's own column
    // (paint_phase_reset_overlay_ring, 2026-10-09 ~17:40). THE PHASES ARE THE
    // CAPTURE'S, RE-COUNTED FROM THE CANVAS'S TOP (the captures are the law,
    // 2026-10-09): the capture gives the red at y ≡ 3 and the blue at
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
    .view_bar_field_at300  = 15,
    .ruler_ground_px       = 11,
    .ruler_minor_tick_px   = 2,
    .ruler_major_tick_px   = 4,
    .ruler_baseline_px     = 7,
    .cue_lane_ground_px    = 12,
    .cue_lane_quanta       = 5,
    .cue_fill_px           = 17,
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
// line, its field (authored at 300 %, its W the device px over 3) and a line; the ruler its ground and its bottom line; the
// marker lane its W rows and its label quanta (one W each at 100 %); the
// canvas's top frame row one line. Below the canvas, its
// bottom frame row, one line, the program ring's last row.
constexpr int program_view_bar_authored_h(const ProgramSpec& p) {
    return 1 + p.view_bar_field_at300 / 3 + 1;
}
constexpr int program_ruler_authored_h(const ProgramSpec& p) {
    return p.ruler_ground_px + 1;
}
constexpr int program_marker_lane_authored_h(const ProgramSpec& p) {
    return p.cue_lane_ground_px + p.cue_lane_quanta;
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
// THE STRIPS: 13 + 23 + 13; the band's
// authored part under its logo lane 23 + 13.
static_assert(program_strip_authored_h(kProgramSpec) == 49);
static_assert(program_band_authored_h(kProgramSpec) == 36);
// ROW 8'S GROUP GAP IS THE ONE THE BARS LEFT (2026-10-22): a 6-W bar between
// the 5-W pad after a bar and the 4-W pad before one (run_gap_px's field).
static_assert(kProgramSpec.run_gap_px == 6 + 5 + 4);
// THE VIEW BAR'S FIELD IS A WHOLE W (2026-10-24): its 300 % number divides
// by 3, so the W sum is exact and skin_px rounds it as scaled_px rounds 5 W.
static_assert(kProgramSpec.view_bar_field_at300 % 3 == 0);
static_assert(program_view_bar_authored_h(kProgramSpec) == 7);
static_assert(program_ruler_authored_h(kProgramSpec) == 12);
static_assert(program_marker_lane_authored_h(kProgramSpec) == 17);
static_assert(program_column_authored_h(kProgramSpec) == 37);
static_assert(program_stereo_divider_authored_h(kProgramSpec) == 3);
// THE RULER'S ROWS (2026-10-09 ~21:00): the six the ruler gave are the six
// the marker lane took, so the column and every stack keep their totals.
static_assert(program_ruler_authored_h(kProgramSpec) +
                  program_marker_lane_authored_h(kProgramSpec) ==
              18 + 11);
// THE MAJOR TICK'S TOP ROW IS THE DIGITS' BASELINE ROW (7), the tick painted
// over a digit's overshoot below the baseline; the PLAYHEAD'S HEAD, the
// cue's triangle in its device rows (the head), stands on the ground's last
// rows (render.h's cue_triangle_rows, 8 device rows at 300 % of the
// ground's 33; 8/3 W against the ground's 11 W, so the head fits the ground
// at every scale) over the ticks' feet and covers what stands under it.
// The digits' own rows — one W of air, then the small
// face's six — are render.h's assert, where the face is in scope.
static_assert(kProgramSpec.ruler_ground_px - kProgramSpec.ruler_major_tick_px ==
              kProgramSpec.ruler_baseline_px);
// THE CUE'S ROWS (the product's lane): the label's box the whole lane, the
// label's cap band centred in it (render.h's cue_baseline_px), the
// triangle's device rows at its bottom (8/3 W at every scale against the
// lane's 17 W, render.h's cue_triangle_rows).
static_assert(kProgramSpec.cue_fill_px ==
              program_marker_lane_authored_h(kProgramSpec));
// (THE LABEL CLEARS THE TRIANGLE BY CONSTRUCTION: render.h's
// cue_label_lead_px is the triangle's reach right — its rows and its
// shadow's offset — plus the gap, never negative, at every scale.)
// The cue's two colors alternate one dot every playhead period, and neither
// shares a row with the playhead's dots.
static_assert(kProgramSpec.cue_dot_period == 2 * kProgramSpec.dot_period);
static_assert((kProgramSpec.range_dot_phase - kProgramSpec.cue_dot_phase +
               kProgramSpec.cue_dot_period) % kProgramSpec.cue_dot_period ==
              kProgramSpec.dot_period);
static_assert(kProgramSpec.cue_dot_phase % kProgramSpec.dot_period !=
              kProgramSpec.playhead_dot_phase);
// (The label's text stands one pad quantum inside its fill — the fill's
// lead and pad are the label's lead: render.h's cue_fill_lead_px.)

#endif // WARPTEMPO_GUI_PROGRAM_SPEC_H
