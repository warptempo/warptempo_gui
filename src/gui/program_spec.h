#ifndef WARPTEMPO_GUI_PROGRAM_SPEC_H
#define WARPTEMPO_GUI_PROGRAM_SPEC_H

// THE PROGRAM SPEC (architect 2026-10-09 ~06:55, "chrome means chrome, the
// program is Cool Edit"; the go ~09:40): THE CHROME is the OS part — the
// caption, the menu row, the window frame, the pull-downs, the cards, the
// prompts, the picker's and the Settings dialogs with their fields and
// buttons — each chrome's own (chrome_spec.h). THE PROGRAM is COOL EDIT PRO
// 2.1'S EDIT VIEW FROM THE TOOLBAR DOWN, THE SAME UNDER EVERY CHROME
// (windows-2000, clearlooks, cde). This header is the one owner of the
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
// the program takes no chrome's toolbar; the icon sets are scalable and
// Gemini's redraws resolution-free, so a glyph loses nothing at 20. THE
// SIZE ON THE GLASS is gui_scale's (300 x 24 / 20 = 360 puts the glyph at
// the 72 device px the 24-W seat drew at 300; the device's choice, never
// this header's). (The 24-W seat round a 27-W case, a 37-W band and a 36-W
// row 8 stood for the morning of 2026-10-09; git history.)
//
// THE PROGRAM'S LANES (2026-10-09): THE TOOLBAR BAND (the top strip's lane
// 2) and ROW 8 under its DOCK BAR (the bottom strip's one lane), the
// roster's; and, from the band down to the well, the CANVAS COLUMN'S THREE
// in ORDER R (architect 2026-10-09 ~09:40, the mock of record
// tmp/mocks/cool_edit/mock_CE_S3_GRID_4.png; its generator mock_ce_s3.py,
// NOTES.md's sets 2 and 3): THE COLUMN'S AIR (5 W of face under the band's
// last line, METRICS §4.1), THE VIEW BAR — the trim bar — (a line, the
// black field, a line: 8), THE RULER FLIPPED (17 rows of ground on the view
// bar's light line, its ticks standing on its bottom row, then its own
// light bottom line: 18) and THE MARKER LANE of Cool Edit's cues (11), the
// well's top frame row under its last. The canvas itself — its grid and
// its centre lines — is a later part; the well and its frame stand as they
// were.
//
// THE CUE AND THE PLAYHEAD'S HEAD ARE DRAWN IN QUANTA, NOT IN ROUNDED W
// (2026-10-09): a triangle's 9-7-5-3-1 rows, its shadow column, the label's
// lead from the column, the selected fill's pads and the dots down the
// canvas are counted in the stem's own width u = scaled_px(1, 1) (render.h's
// waveform_line_px) — the playhead head's construction since 2026-10-05 —
// so the apex is the stem's column at every scale and the nine units centre
// on it with no remainder; the LANES' ROWS and the text's seats are W
// lengths, each its own rounded part (scaled_px's rule).
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
    // column, so the groove reads mid | dark | light — 12 W from one pane's
    // last case to the next pane's first, as Cool Edit's.
    int  pane_lead_px;
    int  pane_trail_px;
    // THE DOCK BAR above row 8 (METRICS §2.2, Cool Edit's y = 915..920): a
    // light row, this much face, a mid row and a dark row; its left column
    // light, its right column mid.
    int  dock_bar_face_px;
    // ROW 8 (METRICS §5.1 at one button row: Cool Edit's 55 rows hold two
    // rows of 23 cases; the product's one row of the same 23): the face
    // above the case and below it.
    int  row8_air_above_px;
    int  row8_air_below_px;
    // ROW 8'S GROUPS (METRICS §5.2): the face before the first group and
    // between two groups; the GRIPPER's five columns (light, mid, face,
    // light, mid — two etched double lines) running the case's rows and
    // `gripper_reach_px` more above and below (each mid column one row lower
    // than its light one, the etch), the face from
    // the gripper to the first case and from the last case to the END BAR,
    // whose six columns are a light column, this much face, a mid and a dark
    // column (its top row light and its bottom row mid).
    int  row8_group_gap_px;
    int  gripper_cols;
    int  gripper_reach_px;
    int  gripper_to_case_px;
    int  case_to_end_bar_px;
    int  end_bar_face_px;
    // THE DARK TIME FIELD (METRICS §5.4: 72 x 17, its TL line, its ground,
    // its BR line; the mocked field 85 wide round the clock's cell with 8 of
    // pad): its height, ROW 8'S CLOCK'S least width (the mock's 85; the
    // render player's two fields are their cell and pads alone), and the pad
    // from its outer edge to the reserved cell on each side. Every time field
    // takes the shape: row 8's clock and the render player's position and
    // length.
    double field_h_px;
    double field_min_w_px;
    double field_pad_px;
    // THE STATE LINE'S AIR: its first ink this far past the time-field
    // group's end bar (the field's own symmetric air of 2026-10-08, the end
    // bar now standing between the field and the line).
    double state_air_px;

    // -- THE CANVAS COLUMN (METRICS §3, §4; NOTES.md sets 2 and 3) ----------
    // THE COLUMN'S AIR: the face between the band's last line and the view
    // bar's top line (METRICS §4.1, Cool Edit's y = 79..83), a lane of its
    // own above the trim lane (main.cpp's lane table).
    int  column_air_px;
    // THE VIEW BAR'S FIELD between its two lines (METRICS §3: Cool Edit's
    // 10; architect 2026-10-09, mock set 1's axis, 6): the trim lane is a
    // line, this field and a line.
    int  view_bar_field_px;
    // THE RULER'S GROUND, its rows 0 .. 16 under the view bar's light line
    // (METRICS §4.4, 17 rows), then its own light bottom line; THE TICKS
    // stand on the ground's bottom row and rise this far into it (minor and
    // major); THE DIGITS' BASELINE is the top of this ground row (12), the
    // cap of 7 above it and the black shadow ending on it.
    int  ruler_ground_px;
    int  ruler_minor_tick_px;
    int  ruler_major_tick_px;
    int  ruler_baseline_px;
    // THE MARKER LANE (METRICS §4.2, 11 rows): this many W rows above the
    // cue's triangle (row 0 the air, the label's cap on rows 1 .. 7), then
    // the triangle's cue_triangle_rows quanta, its apex on the lane's last
    // row; the label's baseline the top of this lane row (8), the selected
    // fill this many rows from the lane's top (rows 0 .. 8: one row of air
    // above the cap and one below the baseline).
    int  cue_above_triangle_px;
    int  cue_triangle_rows;
    int  cue_baseline_px;
    int  cue_fill_px;
    // THE CUE'S HORIZONTAL LENGTHS, IN QUANTA from the marker's column
    // (the head): the label's first column past the column (Cool Edit's
    // stem 416 → label 422), the selected fill's first column (one quantum
    // before the text) and its pad past the text, the gap between two of a
    // marker's label segments' boxes (the bound cells, the history's two
    // halves — the product's own, a segment's text standing the label's
    // lead past the previous one's end), and the overlap rule's lead: a
    // label is clipped at the next triangle's left edge when the next column
    // stands more than this far right.
    int  cue_label_lead;
    int  cue_fill_lead;
    int  cue_fill_pad;
    int  cue_segment_gap;
    int  cue_overlap_lead;
    // THE CANVAS DOTS (METRICS §4.2, §4.3), one quantum square, counted in
    // quanta from the canvas's first row: THE PLAYHEAD'S every `dot_period`
    // rows on its phase; A CUE'S TWO COLORS each every `cue_dot_period` rows
    // (architect 2026-10-09 ~11:50, Cool Edit's two cue colors) — THE RED
    // (`cue`) on `cue_dot_phase` 7, THE BLUE (`range`) on `range_dot_phase`
    // 3, so a point cue's two alternate one dot every `dot_period` rows, BLUE
    // FIRST FROM THE TOP, and a one-color stem has half the dots (render.cpp's
    // resolve_flag_face owns which a flag wears). THE PHASES ARE THE
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
    .row8_air_above_px  = 4,
    .row8_air_below_px  = 5,
    .row8_group_gap_px  = 2,
    .gripper_cols       = 5,
    .gripper_reach_px   = 2,
    .gripper_to_case_px = 6,
    .case_to_end_bar_px = 4,
    .end_bar_face_px    = 3,
    .field_h_px         = 17.0,
    .field_min_w_px     = 85.0,
    .field_pad_px       = 8.0,
    .state_air_px       = 5.0,
    .column_air_px         = 5,
    .view_bar_field_px     = 6,
    .ruler_ground_px       = 17,
    .ruler_minor_tick_px   = 2,
    .ruler_major_tick_px   = 4,
    .ruler_baseline_px     = 12,
    .cue_above_triangle_px = 6,
    .cue_triangle_rows     = 5,
    .cue_baseline_px       = 8,
    .cue_fill_px           = 9,
    .cue_label_lead        = 6,
    .cue_fill_lead         = 5,
    .cue_fill_pad          = 1,
    .cue_segment_gap       = 4,
    .cue_overlap_lead      = 6,
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
constexpr int program_row8_authored_h(const ProgramSpec& p) {
    return p.row8_air_above_px + program_case_authored_px(p) +
           p.row8_air_below_px;
}
// The canvas column's three lanes above the well (the head): the view bar a
// line, its field and a line; the ruler its ground and its bottom line; the
// marker lane its rows above the triangle and the triangle's quanta (one W
// each at 100 %).
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
           program_ruler_authored_h(p) + program_marker_lane_authored_h(p);
}
static_assert(program_case_authored_px(kProgramSpec) == 23);
static_assert(program_band_authored_h(kProgramSpec) == 33);
static_assert(program_dock_bar_authored_h(kProgramSpec) == 6);
static_assert(program_row8_authored_h(kProgramSpec) == 32);
static_assert(program_view_bar_authored_h(kProgramSpec) == 8);
static_assert(program_ruler_authored_h(kProgramSpec) == 18);
static_assert(program_marker_lane_authored_h(kProgramSpec) == 11);
static_assert(program_column_authored_h(kProgramSpec) == 42);
// THE CUE'S ROWS (METRICS §4.2): the label's cap (7, rows 1 .. 7) and the
// triangle (rows 6 .. 10) share rows 6 and 7 but never columns — the label
// stands past the triangle's shadow; the fill's rows end one below the
// baseline; the playhead's head (rows 12 .. 16 of the ruler) is the cue's
// triangle on the ruler's own rows.
static_assert(kProgramSpec.cue_baseline_px - 7 == 1);
static_assert(kProgramSpec.cue_fill_px == kProgramSpec.cue_baseline_px + 1);
static_assert(kProgramSpec.cue_label_lead >
              (2 * kProgramSpec.cue_triangle_rows - 1) / 2 + 1);
static_assert(kProgramSpec.ruler_ground_px - kProgramSpec.cue_triangle_rows ==
              kProgramSpec.ruler_baseline_px);
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
