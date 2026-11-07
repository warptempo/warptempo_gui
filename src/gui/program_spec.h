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
// UNSCALED. THE BUTTON CASE IS NOT ONE OF THEM: the key's face is a skin
// length, the `key_width` tuning key's W wide (architect 2026-11-06, a
// tuning key until he rules the number; 35 its default, 70 device px on
// the tablet at 200 %) — Cool Edit's 23-W case (the retrofit of 2026-11-03
// reversing the square key of 2026-11-02) — by the `key_height` tuning
// key's W tall (17 its default; render.h's case block, icon_case_w_px /
// icon_case_h_px), the face the whole key, its neighbors one W of ground
// away (key_gap_px, architect 2026-11-04, the unit); its Breeze glyph
// is centered on its face by its own ink-box road, its size following the
// key's height (paint_key_glyph), its legend under it (paint_key_legend).
// THE SIZE ON THE GLASS is gui_scale's (the device's choice, never this
// header's).
//
// THE SIMPLE LAYOUT (architect 2026-10-22, "the bevels are def part of the
// problem … redesign with dead simple layout", the mocks on the tablet), FLAT
// SINCE 2026-11-01 (materials.h; the well 2026-11-01 night, "THE MAIN PAGE
// IS SETTLED"): no ring, no stroke on the ground. THE PROGRAM'S LANES, top to
// bottom: THE TOOLBAR BAND at the window's top — THE LOGO LANE (render.h's
// logo lane block, 36 W), the key, THE STRIP PAD (19 W, render.h's
// strip_pad_px; the keys' legends in it), its keys in plain runs on the
// ground, the runs the `run_gap` tuning key's W apart (22 its default since
// 2026-11-07; render.h's program_run_gap_px; the hi-fi's
// lengths again since 2026-11-03, the retrofit, rounded to the unit on
// 2026-11-04 night); then THE WELL,
// flat regions on
// the ground (render.h's well paragraph and its lanes block, which derive
// every length at the element) — a row of ground, THE TRIM BAR (its two
// flat fills on view_bar_field_w's 14 px and one hairline, a row of
// ground under them), THE RULER FLIPPED, a row
// of ground, THE MARKER LANE of Cool Edit's cues (the flag editor's box on
// its whole rows, "right now it's too small to click"; usability over
// accuracy, render.h's row-6 canvas paragraph), a row of ground, THE CANVAS
// — the waveform area, its grid and its center lines, its two channels'
// wells with the stereo divider's row of ground between them — and a row of
// ground, the well's foot; then ROW 8, THE STRIP PAD, the case, THE STRIP
// PAD, on the window's foot. THE WELL STANDS 6 W IN FROM EACH SIDE of the
// window's client area on the ground (column_margin_px) and half the grid
// floor's leftover, THE WELL THE WAVEFORM AREA (architect 2026-11-03), its
// regions sharing the area's columns (render.h's well_columns, the outermost
// keys' faces' outer columns: THE EDGE RULE). (The band's head and foot lines and its panes,
// the tab strip under it, the column's air, the column's foot, the dock bar,
// row 8's bars and the border under row 8 stood 2026-10-09 to 2026-10-22;
// the program ring round the column 2026-10-22 to 2026-11-02; git history.)
//
// THE CUE AND THE PLAYHEAD'S HEAD ARE DRAWN IN ONE-DEVICE-PX CELLS AND SKIN
// LENGTHS, NOT IN ROUNDED W (render.h's cue_triangle_unit_px and the
// label's lengths beside it): THE TRIANGLE'S UNIT IS ONE DEVICE PX AT EVERY SCALE
// (architect 2026-10-26: "I think we should make the triangle smaller …
// Why not simply leave unscaled? That would preserve the stem/flag size
// relation from CEP"; its own number since the stems became one W,
// 2026-11-04, "untouched: the triangle's cell and the ruler's head"), so a
// triangle's whole cells — Cool Edit's aliased staircase,
// paint_ce_cue_triangle — stand beside the one-W stem, CENTERED ON THE
// FRAME'S COLUMN, the apex the frame's column and every row centred on it
// with no remainder (the stem straddling the frame, fill_waveform_line's
// rule since 2026-11-06, so on the tablet the apex points at the stem's
// right pixel: architect 2026-11-06, session 67, "it should point at the
// right column"); THE MARKER'S CHIP is that staircase in a one-W outline, a
// second, larger staircase of the same cells (architect 2026-11-04, mock
// g1; render.h's cue_chip_rows: 29 x 15 round 19 x 10 on the tablet at
// 200 %, 13 x 7 round 9 x 5 at 100 %), the ruler's head the same chip. ITS
// ROW COUNT IS NO FIELD HERE: it is a skin length, 5 W through scaled_px,
// rounded at the element (architect 2026-10-26 ~13:40, "why not just
// simple scale? tablet is only real surface"; render.h's cue_triangle_rows
// and kCueTriangleRowsW, the one owner — 10 rows at 200 %, 5 at 100 %: the
// fill is a whole 5 W; its staircase's CELLS are one device px each,
// cue_triangle_unit_px, the distinct thing that does not scale). THE
// LABEL'S LENGTHS ARE SKIN LENGTHS TOO, scaled with the text they place
// (the text round, architect 2026-11-03): the fill's pad, the segment gap
// and the flag editor's outline (render.h's cue_fill_pad_px,
// cue_segment_gap_px, cue_editor_outline_px), and THE LABEL'S LEAD from the
// column, THE BARE TRIANGLE'S REACH RIGHT, THE OUTLINE'S W AND A GAP OF 6 W
// (render.h's cue_label_lead_px and kCueLabelGapW, architect 2026-10-26
// ~12:40: the label follows the rows; the W 2026-11-04: 10 + 2 + 12 = 24
// device px on the tablet). Every line down the canvas is
// solid, one W (no dotted line since 2026-11-04). THE WELL'S
// ROWS are render.h's (the well's lanes block): skin lengths, hairlines and
// the faces' own metrics, each its own rounded part.
struct ProgramSpec {
    // (THE CASE HAS NO W TERM HERE: the key is render.h's case block, its
    // width the `key_width` tuning key's, 2026-11-06, and its height the
    // `key_height` tuning key's, 2026-11-03, its gap kKeyGapW, 2026-11-04.)
    // COOL EDIT'S TOOLBAR HAS NO HOVER FACE (a capture with the
    // pointer on a button differs from one without only by Wine's tooltip):
    // the roster's and the render player's hot face is never lit, the hover
    // walks storing none (input_pointer.cpp's two writes read this).
    bool case_hot_face;
    // (THE STRIPS' RUN GAP — row 8's group gap as the bars left it — AND
    // THE STRIP PAD — settled 2026-10-23 — are render.h's since 2026-11-03,
    // kStripPadW 19 since 2026-11-04 night (the retrofit reversed that
    // morning's "half the key") and the run gap the `run_gap` tuning key's
    // since 2026-11-07, 22 its default, the constexpr kRunGapW 22 before it.)
    // THE STRIPS' ENDS ARE THE WELL'S FIRST AND LAST COLUMNS, NEVER THE
    // WINDOW'S EDGES (architect 2026-10-23, "yes" to tying both strips' ends
    // to the ring, the well's columns since the flat well — THE EDGE RULE;
    // render.h's well_columns the one owner), so the
    // strips and the well coincide at every scale whatever the grid floor's
    // leftover. BOTH STRIPS ARE FLUSH WITH THE WELL at both ends (architect
    // 2026-10-23, "shouldn't row 8 take the same rule as top strip?" — yes):
    // the band's first face and row 8's clock field start on the well's
    // first column, the band's right run's and row 8's block's last face
    // end on its last column (the face the key, 2026-11-04) — the modals'
    // controls row's seat
    // (paint_handler.cpp's icon_row_seats for the band, bottom_row_seats for
    // row 8 and the controls row), so neither strip nor the controls row
    // carries a field here.
    // (THE DARK TIME FIELD'S HEIGHT AND PAD are render.h's: kTimeFieldPadW
    // and the height derived from it and the clock's cap, the marker lane's
    // height too. THE ACTIVE TAB'S NAME AND THE STATE LINE'S AIR past the
    // clock field are paint_handler.cpp's, kTabNameAirW and kStateAirW,
    // 2026-11-03, 7 W since 2026-11-04 night.)

    // -- THE WELL (THE CANVAS COLUMN) -----------------------------------------
    // THE WELL'S SIDE MARGIN (Cool Edit's x = 202..207 of
    // face before the canvas frame's dark column at 208; the mock's FX0 = 6
    // and FX1 = WW − 7): this much ground from each side of the window's
    // client area, then half the grid floor's leftover (main.cpp's
    // waveform_area), then the well, which is the waveform area (architect
    // 2026-11-03) — the trim bar's, the ruler's, the marker lane's and the
    // canvas's columns. 9 W SINCE THE REBASE TO 200 % (architect 2026-11-05:
    // Cool Edit's 6 times 1.5, so the tablet's 18 device px and its well,
    // columns 24 .. 2279, stand).
    int  column_margin_px;
    // THE CANVAS'S HORIZONTAL GRID (architect 2026-10-09, the design loop's
    // third mock set, "grid 4 per half channel"; Cool Edit's GrdL): each
    // channel's half height divided into this many, a line at every inner
    // division above and below the zero row (render_canvas, render.h's row-6
    // canvas paragraph).
    int  grid_divisions;
    // THE TRIM BAR'S FIELD, IN W (architect 2026-11-01 night, M5c / M5h,
    // "good"; the two fills on it and one hairline, the ground line under
    // them, mock z2, 2026-11-03; 7 W since 2026-11-04 night, so the fill is
    // 8 W and the bar rows 146 .. 163 on the tablet): THE RULING IS THE
    // TABLET'S 14 DEVICE PX, read on the one road (render.h's
    // view_bar_field_h_px through scaled_px, floor 1: 14 at 200 %, 7 at
    // 100 %; the bar's rows trim_bar_h_px). (THE RULER'S GROUND, TICKS AND
    // BASELINE and THE MARKER LANE'S W ROWS, QUANTA AND FILL, the hi-fi
    // well's fields 2026-10-09 to 2026-11-02, and THE STEREO DIVIDER'S FACE
    // left with the flat well: render.h's well lanes block derives them at
    // the element.)
    int  view_bar_field_w;
    // (THE CUE'S HORIZONTAL LENGTHS — the selected fill's pad and the
    // segment gap, in label quanta until 2026-11-03 — are render.h's skin
    // lengths since, cue_fill_pad_px and cue_segment_gap_px.)
    // (THE CANVAS DOTS — the playhead's period and phase and a cue's two
    // phases, Cool Edit's cue and range dots — left 2026-11-04 with the
    // dotted stems and cursor: every line on the canvas is solid, render.h's
    // marker-lane and playhead paragraphs.)
};

inline constexpr ProgramSpec kProgramSpec = {
    .case_hot_face      = false,
    .column_margin_px      = 9,
    .grid_divisions        = 4,
    .view_bar_field_w  = 7,
};

// (THE AUTHORED TOTALS — the band's and row 8's strip pads, the lane
// table's record — left 2026-11-03: every lane of the band and of row 8 is
// a skin length in W, render.h's case block.)
// (THE CANVAS COLUMN'S W SUMS — the view bar's 7, the ruler's 12, the marker
// lane's 17, the stereo divider's 3 — left with the flat well, 2026-11-02:
// the well's lanes are device px derived at the element, render.h's well
// lanes block.)
// (THE LABEL CLEARS THE CHIP BY CONSTRUCTION: render.h's
// cue_label_lead_px is the bare triangle's reach right, the outline's W and
// the gap, never negative, at every scale.)
// (The label's text stands one fill pad inside its fill — the fill's lead
// and pad are the label's lead: render.h's cue_fill_lead_px.)

#endif // WARPTEMPO_GUI_PROGRAM_SPEC_H
