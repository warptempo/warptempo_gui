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
// it holds no color (the palette's constants, palette_file.h, and the flat
// look's, materials.h).
//
// NO PROPORTIONAL FIT (architect 2026-10-09 ~10:25, "I'd like to see it at
// 20"): every one of Cool Edit's measurements (his Cool Edit
// metrics, one capture px = one W) is taken at Cool Edit's own W
// UNSCALED. THE BUTTON CASE IS NOT ONE OF THEM: the key's cell is a skin
// length, 69 device px at 300 % wide — Cool Edit's 23-W case, back as a
// device-px constexpr since 2026-11-03 (the retrofit, reversing the square
// key of 2026-11-02) — by the `key_height` tuning key's device px at 300 %
// tall (33 its default; render.h's case block, icon_case_w_px /
// icon_case_h_px), its face the cell inset one device px; its Breeze glyph
// is centered on its face by its own ink-box road, its size following the
// key's height (paint_key_glyph), its legend under it (paint_key_legend).
// THE SIZE ON THE GLASS is gui_scale's (the device's choice, never this
// header's).
//
// THE SIMPLE LAYOUT (architect 2026-10-22, "the bevels are def part of the
// problem … redesign with dead simple layout", the mocks at 300 %), FLAT
// SINCE 2026-11-01 (materials.h; the well 2026-11-01 night, "THE MAIN PAGE
// IS SETTLED"): no ring, no stroke on the ground. THE PROGRAM'S LANES, top to
// bottom: THE TOOLBAR BAND at the window's top — THE LOGO LANE (render.h's
// logo lane block, 72 device px at 300 %), the key's cell, THE STRIP PAD
// (39 at 300 %, render.h's strip_pad_px; the keys' legends in it), its keys
// in plain runs on the ground, the runs 45 apart at 300 % (render.h's
// program_run_gap_px; the hi-fi's lengths again since 2026-11-03, the
// retrofit); then THE WELL, flat regions on
// the ground (render.h's well paragraph and its lanes block, which derive
// every length at the element) — a row of ground, THE TRIM BAR (its two
// flat fills on view_bar_field_at300's 16 px and one hairline, a row of
// ground under them), THE RULER FLIPPED, a row
// of ground, THE MARKER LANE of Cool Edit's cues (the flag editor's box on
// its whole rows, "right now it's too small to click"; usability over
// accuracy, render.h's row-6 canvas paragraph), a row of ground, THE CANVAS
// — the waveform area, its grid and its center lines, its two channels'
// wells with the stereo divider's row of ground between them — and a row of
// ground, the well's foot; then ROW 8, THE STRIP PAD, the case, THE STRIP
// PAD, on the window's foot. THE WELL STANDS 6 W IN FROM EACH SIDE of the
// window's client area on the ground (column_margin_px), its regions sharing
// the well's two edge columns (render.h's well_columns, the outermost keys'
// faces' outer columns: THE EDGE RULE). (The band's head and foot lines and its panes,
// the tab strip under it, the column's air, the column's foot, the dock bar,
// row 8's bars and the border under row 8 stood 2026-10-09 to 2026-10-22;
// the program ring round the column 2026-10-22 to 2026-11-02; git history.)
//
// THE CUE AND THE PLAYHEAD'S HEAD ARE DRAWN IN ONE-DEVICE-PX CELLS AND SKIN
// LENGTHS, NOT IN ROUNDED W (render.h's cue_triangle_unit_px and the
// label's lengths beside it): THE TRIANGLE'S UNIT IS ONE DEVICE PX AT EVERY SCALE
// (architect 2026-10-26: "I think we should make the triangle smaller …
// Why not simply leave unscaled? That would preserve the stem/flag size
// relation from CEP" — "on waveform → unscaled" carried to the cue's
// triangle), so a triangle's whole cells — Cool Edit's aliased staircase,
// paint_ce_cue_triangle — stand beside the one-device-px stem, the apex the
// stem's column and every row centred on it with no remainder. ITS ROW
// COUNT IS NO FIELD HERE: it is a skin length, 9 DEVICE PX AT 300 % through
// skin_px, rounded at the element (architect 2026-10-26 ~13:40, "why not
// just simple scale? tablet is only real surface"; render.h's
// cue_triangle_rows and kCueTriangleRowsAt300, the one owner — 9 rows at
// 300 %, 6 at 200 %, 4 at 150 % and 138 %), no whole count of W. THE
// LABEL'S LENGTHS ARE SKIN LENGTHS TOO, scaled with the text they place
// (the text round, architect 2026-11-03): the fill's pad, the segment gap
// and the flag editor's outline (render.h's cue_fill_pad_px,
// cue_segment_gap_px, cue_editor_outline_px), and THE LABEL'S LEAD from the
// column, THE TRIANGLE'S REACH RIGHT PLUS A GAP OF 13 DEVICE PX AT 300 %
// (render.h's cue_label_lead_px and kCueLabelGapAt300, architect
// 2026-10-26 ~12:40: the label follows the rows). The dots down the canvas
// are device rows (2026-10-09 ~14:25, the dot fields below). THE WELL'S
// ROWS are render.h's (the well's lanes block): skin lengths, hairlines and
// the faces' own metrics, each its own rounded part.
struct ProgramSpec {
    // (THE CASE HAS NO W TERM HERE: the cell is render.h's case block, its
    // width the device-px constexpr kKeyCellWAt300 and its height the
    // `key_height` tuning key's, 2026-11-03.)
    // COOL EDIT'S TOOLBAR HAS NO HOVER FACE (a capture with the
    // pointer on a button differs from one without only by Wine's tooltip):
    // the roster's and the render player's hot face is never lit, the hover
    // walks storing none (input_pointer.cpp's two writes read this).
    bool case_hot_face;
    // (THE STRIPS' RUN GAP — 15 W, row 8's group gap as the bars left it —
    // AND THE STRIP PAD — 13 W, settled 2026-10-23 — are render.h's device-px
    // constexprs since 2026-11-03, kRunGapAt300 45 and kStripPadAt300 39, the
    // same lengths: the retrofit reversed that morning's "half the key".)
    // THE STRIPS' ENDS ARE THE WELL'S EDGE COLUMNS, NEVER THE
    // WINDOW'S EDGES (architect 2026-10-23, "yes" to tying both strips' ends
    // to the ring, the well's columns since the flat well — THE EDGE RULE;
    // render.h's well_columns the one owner), so the
    // strips and the well coincide at every scale whatever the grid floor's
    // leftover. BOTH STRIPS ARE FLUSH WITH THE WELL at both ends (architect
    // 2026-10-23, "shouldn't row 8 take the same rule as top strip?" — yes):
    // the band's first face and row 8's clock field start on the well's
    // left edge column, the band's right run's and row 8's block's last face
    // end one past its right edge column, their cells one inset px past
    // (2026-11-03) — the modals' controls row's seat
    // (paint_handler.cpp's icon_row_seats for the band, bottom_row_seats for
    // row 8 and the controls row), so neither strip nor the controls row
    // carries a field here.
    // (THE DARK TIME FIELD'S HEIGHT AND PAD are render.h's, authored at the
    // tablet's scale: kTimeFieldPadAt300 and the height derived from it and
    // the clock's cap, the marker lane's height too. THE ACTIVE TAB'S NAME
    // AND THE STATE LINE'S AIR past the clock field are paint_handler.cpp's,
    // kTabNameAirAt300 and kStateAirAt300, 2026-11-03.)

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
    // THE TRIM BAR'S FIELD, IN DEVICE PX AT 300 % (architect 2026-11-01
    // night, M5c / M5h, "good": the bar 145 .. 162; the two fills on it and
    // one hairline, the ground line under them, mock z2, 2026-11-03): THE
    // RULING IS THE 300 % NUMBER, read on the skin's road (render.h's
    // view_bar_field_h_px through skin_px, floor 1: 16 at 300 %, 11 at
    // 200 %; the bar's rows trim_bar_h_px). (THE RULER'S GROUND, TICKS AND
    // BASELINE and THE MARKER LANE'S W ROWS, QUANTA AND FILL, the hi-fi
    // well's fields 2026-10-09 to 2026-11-02, and THE STEREO DIVIDER'S FACE
    // left with the flat well: render.h's well lanes block derives them at
    // the element.)
    int  view_bar_field_at300;
    // (THE CUE'S HORIZONTAL LENGTHS — the selected fill's pad and the
    // segment gap, in label quanta until 2026-11-03 — are render.h's skin
    // lengths since, cue_fill_pad_px and cue_segment_gap_px.)
    // THE CANVAS DOTS, ONE DEVICE PX SQUARE, COUNTED IN
    // DEVICE ROWS from the canvas's first row (architect 2026-10-09 ~14:25,
    // "on waveform → unscaled": one device px at every gui_scale; render.h's
    // waveform_line_px and dotted_waveform_line_run, the stereo divider's
    // row crossed with no re-phasing, 2026-11-02). ONE ON, ONE OFF
    // (architect 2026-11-02, mock t1): THE PLAYHEAD'S dots every
    // `dot_period` 2 rows on `playhead_dot_phase` 1, the odd rows; A CUE'S
    // TWO PHASES (Cool Edit's cue and range dots) each every
    // `cue_dot_period` 4 rows — the range's on `range_dot_phase` 0, the
    // FIRST FROM THE TOP, the cue's on `cue_dot_phase` 2 — so a point cue's
    // stem stands on the even rows, its two phases alternating; and a
    // playhead standing on a stem fills the odd rows between them, so the
    // two close into ONE SOLID LINE, the playhead's line color on the odd
    // rows and the stem's on the even (the reason for the rule: mock t1s) —
    // and A SELECTED STEM PAINTS THAT LINE ITSELF, the `playhead_line` on
    // the playhead's own rows (`dot_period` / `playhead_dot_phase`),
    // playhead or not (architect 2026-11-02; render.cpp's resolve_flag_face
    // owns which color each row set wears). The phase-reset
    // lead-in ring's right side takes the same rows, and its top and bottom
    // runs — and the `h` view's nudged pairs' — the same two phases in
    // device columns counted from the reset's (the left stem's) own column,
    // one dot every two columns (paint_phase_reset_overlay_ring,
    // paint_marker_stem_runs). Every cue row is the playhead's period off
    // the playhead's phase, so the two never share a row on one column.
    int  dot_period;
    int  playhead_dot_phase;
    int  cue_dot_period;
    int  cue_dot_phase;
    int  range_dot_phase;
};

inline constexpr ProgramSpec kProgramSpec = {
    .case_hot_face      = false,
    .column_margin_px      = 6,
    .grid_divisions        = 4,
    .view_bar_field_at300  = 16,
    .dot_period            = 2,
    .playhead_dot_phase    = 1,
    .cue_dot_period        = 4,
    .cue_dot_phase         = 2,
    .range_dot_phase       = 0,
};

// (THE AUTHORED TOTALS — the band's and row 8's strip pads in W, the lane
// table's record — left 2026-11-03: every lane of the band and of row 8 is
// a skin length in device px at 300 %, render.h's case block.)
// (THE CANVAS COLUMN'S W SUMS — the view bar's 7, the ruler's 12, the marker
// lane's 17, the stereo divider's 3 — left with the flat well, 2026-11-02:
// the well's lanes are device px derived at the element, render.h's well
// lanes block.)
// (THE LABEL CLEARS THE TRIANGLE BY CONSTRUCTION: render.h's
// cue_label_lead_px is the triangle's reach right plus the gap, never
// negative, at every scale.)
// The cue's two phases alternate one dot every playhead period, and neither
// shares a row with the playhead's dots (one on, one off: the playhead — and
// a selected stem's white — the odd rows, the stem's two phases the even,
// 2026-11-02).
static_assert(kProgramSpec.cue_dot_period == 2 * kProgramSpec.dot_period);
static_assert((kProgramSpec.range_dot_phase - kProgramSpec.cue_dot_phase +
               kProgramSpec.cue_dot_period) % kProgramSpec.cue_dot_period ==
              kProgramSpec.dot_period);
static_assert(kProgramSpec.cue_dot_phase % kProgramSpec.dot_period !=
              kProgramSpec.playhead_dot_phase);
// (The label's text stands one fill pad inside its fill — the fill's lead
// and pad are the label's lead: render.h's cue_fill_lead_px.)

#endif // WARPTEMPO_GUI_PROGRAM_SPEC_H
