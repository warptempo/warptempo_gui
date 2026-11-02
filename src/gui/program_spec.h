#ifndef WARPTEMPO_GUI_PROGRAM_SPEC_H
#define WARPTEMPO_GUI_PROGRAM_SPEC_H

// THE PROGRAM SPEC (architect 2026-10-09 ~06:55, "chrome means chrome, the
// program is Cool Edit"; the go ~09:40): THE CHROME is the OS part — the
// caption and the window frame (none since 2026-10-24, the tablet's window
// the whole panel; the menu row under the caption until 2026-10-21), the
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
// highlight + 20 + 1 shadow + 1 black) wide, the cases' pitch 23 (its
// height the `key_height` tuning key since 2026-10-29). THE REASON: the
// program is Cool Edit, not a chrome — the 24-W seat is ReactOS's large
// toolbar glyph, the base's seat for a chrome vocabulary (chrome_spec.h's
// head, where it stays the chrome's rule), and the program takes no
// chrome's toolbar. (The seat is a WIDTH term of the case alone; the key's
// Breeze glyph is centered on its face by its own ink-box road, paint_key_glyph,
// 2026-11-02, whatever this seat.) THE SIZE ON THE GLASS is gui_scale's (300 x 24 / 20 = 360 puts the
// glyph at the 72 device px the 24-W seat drew at 300; the device's choice,
// never this header's).
//
// THE SIMPLE LAYOUT (architect 2026-10-22, "the bevels are def part of the
// problem … redesign with dead simple layout", the mocks at 300 %), FLAT
// SINCE 2026-11-01 (materials.h; the well 2026-11-01 night, "THE MAIN PAGE
// IS SETTLED"): no ring, no stroke on the ground. THE PROGRAM'S LANES, top to
// bottom: THE TOOLBAR BAND at the window's top — THE LOGO LANE (its upper pad since
// 2026-10-24, render.h's logo lane block), the key, THE STRIP PAD (13 W,
// settled 2026-10-23: strip_pad_px's field below; the keys' legends in the
// bottom pad), its keys in plain runs
// on the ground, the runs `run_gap_px` apart; then THE WELL, flat regions on
// the ground (render.h's well paragraph and its lanes block, which derive
// every length at the element) — a row of ground, THE TRIM BAR (its frame
// and its interior, view_bar_field_at300's 16 px), THE RULER FLIPPED, a row
// of ground, THE MARKER LANE of Cool Edit's cues (the flag editor's box on
// its whole rows, "right now it's too small to click"; usability over
// accuracy, render.h's row-6 canvas paragraph), a row of ground, THE CANVAS
// — the waveform area, its grid and its center lines, its two channels'
// wells with the stereo divider's row of ground between them — and a row of
// ground, the well's foot; then ROW 8, THE STRIP PAD, the case, THE STRIP
// PAD, on the window's foot. THE WELL STANDS 6 W IN FROM EACH SIDE of the
// window's client area on the ground (column_margin_px), its regions sharing
// the well's two edge columns (render.h's well_columns, the keys' outer
// columns: THE EDGE RULE). (The band's head and foot lines and its panes,
// the tab strip under it, the column's air, the column's foot, the dock bar,
// row 8's bars and the border under row 8 stood 2026-10-09 to 2026-10-22;
// the program ring round the column 2026-10-22 to 2026-11-02; git history.)
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
// u = scaled_px(1, 1) (render.h's cue_label_unit_px), the selected fill's
// pads, the segment gap and the flag editor's outline counted in it,
// because the text they place is scaled; THE LABEL'S LEAD from the column
// is THE TRIANGLE'S REACH RIGHT PLUS A GAP OF 11 DEVICE PX AT 300 %, scaled
// (render.h's cue_label_lead_px and kCueLabelGapAt300, architect
// 2026-10-26 ~12:40: the label follows the rows; settled ~13:05; the
// triangle's retired shadow's 3 px folded into the gap 2026-11-02), a skin
// length rather than a field here, being no whole count of W or quanta. The dots down the canvas
// are device rows (2026-10-09 ~14:25, the dot fields below). THE WELL'S
// ROWS are render.h's (the well's lanes block): skin lengths, hairlines and
// the faces' own metrics, each its own rounded part.
struct ProgramSpec {
    // THE CASE: one highlight row and column on the top and
    // left, the glyph seat, one shadow row and column and one black row and
    // column on the right and bottom; cases abut, so the case is the pitch.
    // The glyph's seat is (+1, +1) at rest and (+2, +2) pressed or checked
    // (the shift is one line, cool_edit_paint.h). THE FOUR SUM THE CASE'S
    // WIDTH ALONE since 2026-10-29 (architect): its height is the
    // `key_height` tuning key's device px at 300 % (render.h's
    // icon_case_h_px).
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
    // THE STRIPS' ENDS ARE THE WELL'S EDGE COLUMNS, NEVER THE
    // WINDOW'S EDGES (architect 2026-10-23, "yes" to tying both strips' ends
    // to the ring, the well's columns since the flat well — THE EDGE RULE;
    // render.h's well_columns the one owner), so the
    // strips and the well coincide at every scale whatever the grid floor's
    // leftover. BOTH STRIPS ARE FLUSH WITH THE WELL at both ends (architect
    // 2026-10-23, "shouldn't row 8 take the same rule as top strip?" — yes):
    // the band's first case and row 8's clock field start on the well's
    // left edge column, the band's right run and row 8's block end one past
    // its right edge column — the modals' controls row's seat
    // (paint_handler.cpp's icon_row_seats for the band, bottom_row_seats for
    // row 8 and the controls row), so neither strip nor the controls row
    // carries a field here.
    // (THE DARK TIME FIELD'S 17-W HEIGHT AND 5-W PAD — Cool Edit's
    // measured field, then the dialog field's pad, 2026-10-09 to
    // 2026-10-23 — left this spec with the hi-fi skin: the field's 39 and
    // 12 px at 300 % are paint_handler.cpp's kTimeFieldHeightAt300 /
    // kTimeFieldPadAt300, authored at the tablet's scale. Every time field is
    // still its reserved cell plus the pad each side, no least width —
    // architect 2026-10-10, the 85-W floor round row 8's clock removed.)
    // THE ACTIVE TAB'S NAME (architect 2026-10-22: the tab strip goes, "Tab
    // A" / "Tab B" stands on the bare ground right of the clock): its first
    // ink this far past the clock field's right edge. THE STATE LINE'S AIR:
    // its first ink this far past the name's advance (the pad that stood
    // after the clock group's closing bar, 2026-10-21).
    double tab_name_air_px;
    double state_air_px;

    // -- THE WELL (THE CANVAS COLUMN) -----------------------------------------
    // THE WELL'S SIDE MARGIN (Cool Edit's x = 202..207 of
    // face before the canvas frame's dark column at 208; the mock's FX0 = 6
    // and FX1 = WW − 7): this much ground from each side of the window's
    // client area to the well's edge column, the column one device px,
    // the hairline itself (render.h's well_edge_column_w_px, 2026-10-28),
    // then the well's interior — the trim bar's, the ruler's, the marker
    // lane's and the canvas's columns.
    int  column_margin_px;
    // THE CANVAS'S HORIZONTAL GRID (architect 2026-10-09, the design loop's
    // third mock set, "grid 4 per half channel"; Cool Edit's GrdL): each
    // channel's half height divided into this many, a line at every inner
    // division above and below the zero row (render_canvas, render.h's row-6
    // canvas paragraph).
    int  grid_divisions;
    // THE TRIM BAR'S INTERIOR between its two frame lines, IN DEVICE PX AT
    // 300 % (architect 2026-11-01 night, M5c / M5h, "good": the bar 145 ..
    // 162 framed in one-px 757575; the hi-fi field's 15 between its slots,
    // his T15 of 2026-10-24, before it): THE RULING IS THE 300 % NUMBER, read
    // on the skin's road (render.h's view_bar_field_h_px through skin_px,
    // floor 1: 16 at 300 %, 11 at 200 %). (THE RULER'S GROUND, TICKS AND
    // BASELINE and THE MARKER LANE'S W ROWS, QUANTA AND FILL, the hi-fi
    // well's fields 2026-10-09 to 2026-11-02, and THE STEREO DIVIDER'S FACE
    // left with the flat well: render.h's well lanes block derives them at
    // the element.)
    int  view_bar_field_at300;
    // THE CUE'S HORIZONTAL LENGTHS, IN LABEL QUANTA (the head): the
    // selected fill's pad either side of the text — the fill's first column
    // one pad before the text, so THE FILL'S LEAD IS THE LABEL'S LEAD LESS
    // ONE PAD (render.h's cue_fill_lead_px; the label's lead itself THE
    // TRIANGLE'S REACH RIGHT PLUS A GAP OF 11 DEVICE PX AT 300 %, architect
    // 2026-10-26 ~12:40, settled ~13:05, the gap 8 px past the shadow until
    // the shadow's retirement, 2026-11-02 — 19 device px at 300 %, 12 at
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
    // dotted_waveform_line_run, the stereo divider's row crossed with no
    // re-phasing, 2026-11-02): THE PLAYHEAD'S every `dot_period`
    // rows on its phase; A CUE'S TWO PHASES each every `cue_dot_period` rows
    // (architect 2026-10-09 ~11:50, Cool Edit's cue and range dots) — the
    // cue's on `cue_dot_phase` 7, the range's on `range_dot_phase`
    // 3, so a point cue's two alternate one dot every `dot_period` rows, the
    // range's FIRST FROM THE TOP, every live cue's, warp or phase reset, both
    // in the one `cue` since 2026-11-01 (render.cpp's resolve_flag_face owns
    // which color each phase wears); the
    // phase-reset lead-in
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
    .view_bar_field_at300  = 16,
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
// THE CASE'S WIDTH, A WIDTH (render.h's icon_case_w_px): the W sum. THE
// CASE'S HEIGHT IS NO W TERM since 2026-10-29 (architect): it is the
// `key_height` tuning key's device px at 300 % (render.h's icon_case_h_px),
// on the logo lane's road, so the strip and band sums below hold their pads
// alone and the key's rows stand outside them as the logo lane's do.
constexpr int program_case_authored_px(const ProgramSpec& p) {
    return p.case_light_px + p.glyph_px + p.case_shadow_px + p.case_outer_px;
}
// ROW 8'S AUTHORED PART (2026-10-22, the simple layout; the case's height
// out of it since 2026-10-29): the strip pad above the key and the strip
// pad under it (strip_pad_px's field, 13 since 2026-10-23).
constexpr int program_strip_authored_h(const ProgramSpec& p) {
    return p.strip_pad_px + p.strip_pad_px;
}
// THE BAND'S AUTHORED PART (architect 2026-10-24; the case's height out of
// it since 2026-10-29): the strip pad under the key. Its upper pad is THE
// LOGO LANE, authored in device px at 300 % like the skin's other lengths
// (render.h's logo lane block, logo_lane_h_px), and its key the
// `key_height` key's device px at 300 %, so neither is a Windows-px term
// here (the band was row 8's strip 2026-10-22 to 2026-10-24).
constexpr int program_band_authored_h(const ProgramSpec& p) {
    return p.strip_pad_px;
}
// (THE CANVAS COLUMN'S W SUMS — the view bar's 7, the ruler's 12, the marker
// lane's 17, the stereo divider's 3 — left with the flat well, 2026-11-02:
// the well's lanes are device px derived at the element, render.h's well
// lanes block.)
// THE CASE'S WIDTH 23 W.
static_assert(program_case_authored_px(kProgramSpec) == 23);
// THE STRIPS' W PARTS: row 8's two pads 13 + 13; the band's under its
// logo lane and its key the pad's 13.
static_assert(program_strip_authored_h(kProgramSpec) == 26);
static_assert(program_band_authored_h(kProgramSpec) == 13);
// ROW 8'S GROUP GAP IS THE ONE THE BARS LEFT (2026-10-22): a 6-W bar between
// the 5-W pad after a bar and the 4-W pad before one (run_gap_px's field).
static_assert(kProgramSpec.run_gap_px == 6 + 5 + 4);
// (THE LABEL CLEARS THE TRIANGLE BY CONSTRUCTION: render.h's
// cue_label_lead_px is the triangle's reach right plus the gap, never
// negative, at every scale.)
// The cue's two phases alternate one dot every playhead period, and neither
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
