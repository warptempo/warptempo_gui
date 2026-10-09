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
// THE PROPORTIONAL FIT: Cool Edit's measurements (tmp/research/cool_edit/
// METRICS.md, one capture px = one W) are taken at Cool Edit's own W
// UNSCALED, with ONE exception — the button case. Cool Edit's 23-W case
// stands round a 20-px bitmap; the product's glyph seat is the base's 24 W
// (the large toolbar glyph every icon set is drawn for), so the case is 27
// (1 highlight + 24 + 1 shadow + 1 black) and the band round it 37 where
// Cool Edit's is 33 (its 3 + 23 + 2 face rows become 3 + 27 + 2). Every
// other length — the lines, the panes' face margins, the grooves, the
// grippers, the end bars, the dock bar, the dark time field — is Cool
// Edit's own number.
//
// THIS BRIEF BUILT THE TWO LANES THE ROSTER STANDS IN — THE TOOLBAR BAND
// (the top strip's lane 2) and ROW 8 under its DOCK BAR (the bottom strip's
// one lane); the canvas column (the view bar, the ruler, the cue lane, the
// canvas) follows in later parts and its lanes stay as they were.
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
    // THE TOOLBAR BAND (METRICS §1.1, the case 27 for 23): two lines at its
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
    // rows of 23 cases; the product's one row of 27): the face above the
    // case and below it.
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
};

inline constexpr ProgramSpec kProgramSpec = {
    .case_light_px      = 1,
    .glyph_px           = 24,
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
static_assert(program_case_authored_px(kProgramSpec) == 27);
static_assert(program_band_authored_h(kProgramSpec) == 37);
static_assert(program_dock_bar_authored_h(kProgramSpec) == 6);
static_assert(program_row8_authored_h(kProgramSpec) == 36);

#endif // WARPTEMPO_GUI_PROGRAM_SPEC_H
