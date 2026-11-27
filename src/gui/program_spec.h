#ifndef WARPTEMPO_GUI_PROGRAM_SPEC_H
#define WARPTEMPO_GUI_PROGRAM_SPEC_H

// THE PROGRAM SPEC (architect 2026-10-09 ~06:55, "chrome means chrome, the
// program is Cool Edit"): the program's lanes keep COOL EDIT PRO 2.1'S
// EDIT VIEW's grammar from the toolbar down — the lane order, the grid and
// center lines, the triangles, the playhead, the stereo divider — worn in
// the one look, Arc-Dark's (materials.h; the window paints no chrome of its
// own, the tablet's window the whole panel). This header holds the
// program's one remaining field, the canvas grid's divisions (read by
// render.cpp's canvas grid), and records below where the program's lengths
// went: render.h's blocks, each a whole U through scaled_px, read by the
// lane table (main.cpp) and the painters (cool_edit_paint.h). It holds no
// color (the palette's constants, palette_file.h, and the flat look's,
// materials.h).
//
// NO PROPORTIONAL FIT (architect 2026-10-09 ~10:25, "I'd like to see it at
// 20"): every one of Cool Edit's measurements (his Cool Edit
// metrics, one capture px = one U) is taken at Cool Edit's own size
// UNSCALED. THE BUTTON CASE IS NOT ONE OF THEM: the key's face is a skin
// length, 36 U wide by 28 U tall (render.h's kKeyWidthU and kKeyHeightU,
// Arc's icon button; its case block, icon_case_w_px /
// icon_case_h_px), the box the whole key, its border included, a linked
// group's neighbors sharing one seam column (materials.h's
// key_link_pitch_px); its drawing a fixed 16 U box centered on it, the
// drawing at the box's exact zoom and place (paint_key_glyph).
// THE SIZE ON THE GLASS is gui_scale's (the device's choice, never this
// header's).
//
// THE LAYOUT IS FLAT (materials.h; architect 2026-11-01 night, "THE MAIN
// PAGE IS SETTLED"): no ring, no stroke on the ground. THE PROGRAM'S LANES,
// top to bottom, ON ARC'S NUMBERS (2026-11-19; render.h's blocks): THE
// TITLE BAR (28 U, render.h's title bar block) and THE TOP ICON ROW, Arc's
// toolbar pad 7 U above the keys and 7 U below (render.h's ARC'S TOOLBAR
// PADS), its keys in linked groups, the groups kRunGapU apart (6 U,
// render.h); then
// THE WELL, flat regions RUNNING THE PANEL'S WHOLE WIDTH, EDGE TO EDGE, its
// lines one U of `well_border` (render.h's THE WELL'S EDGES) — the line
// under the top icon row, THE TRIM BAR (Arc's scrollbar: the 12-U trough,
// the pill on it), its line, THE RULER FLIPPED (16 U), its line,
// THE MARKER LANE of Cool Edit's cues (16 U, the ruler's height; the flag
// editor's box on its whole rows), its line, THE CANVAS — the waveform
// area, its grid and its center lines, its two channels' wells with the
// stereo divider between them — and the well's foot; then THE BOTTOM ICON
// ROW, 7 U, the case, 7 U; then THE STATUS BAR, its line and 22 U, on the
// window's foot, the on-screen keyboard standing over the last ones while
// it stands, the app never relaid for it (architect 2026-11-20,
// onscreen_keyboard.h's placement block). The keys'
// outermost faces stand 6 U inside the panel
// (render.h's kEdgeInsetU and THE EDGE RULE at well_columns).
//
// THE CUE AND THE PLAYHEAD'S HEAD ARE DRAWN IN SKIN LENGTHS (render.h's cue
// block and the label's lengths beside it): THE MARKER'S CHIP IS A
// SMOOTH-EDGED TRIANGLE, NO OUTLINE, AT ITS MARK'S EXACT PLACE, its apex on
// the stem's center (architect 2026-11-14 night: "I think the
// chips should also go anti-aliased", "Agree on both."; cool_edit_paint.h's
// THE CUE'S CHIP), the bare triangle of the nominal rows (architect
// 2026-11-19: "the smaller version without the outline";
// render.h's cue_chip_h_px: 8 device px tall and 16 wide on the tablet at
// 200 %, 4 and 8 at 100 %), the ruler's head the same chip. ITS
// ROW COUNT IS NO FIELD HERE: it is a skin length, 4 U through scaled_px,
// rounded at the element (architect 2026-10-26 ~13:40, "why not just
// simple scale? tablet is only real surface"; render.h's cue_triangle_rows
// and kCueTriangleRowsU, the one owner — 8 px at 200 %, 4 at 100 %). THE
// LABEL'S LENGTHS ARE SKIN LENGTHS TOO, scaled with the text they place
// (the text round, architect 2026-11-03): the fill's pad, the segment gap
// and the flag editor's border (render.h's cue_fill_pad_px,
// cue_segment_gap_px, render_flag_editor_box), and THE LABEL'S LEAD from
// the column, THE CHIP'S REACH RIGHT AND A GAP OF 5 U FROM ITS CORNER
// (render.h's cue_label_lead_px and kCueLabelGapU, architect 2026-10-26
// ~12:40: the label follows the rows; 2026-11-19: "the text should
// move left to make sure that the margin remains the same" — 8 + 10 = 18
// device px on the tablet). Every line down the canvas is
// solid, one U (2026-11-04). THE WELL'S
// ROWS are render.h's (the well's lanes block): skin lengths, hairlines and
// the faces' own metrics, each its own rounded part.
struct ProgramSpec {
    // (THE CASE HAS NO U TERM HERE: the key is render.h's case block,
    // kKeyWidthU x kKeyHeightU.)
    // (THE STRIPS' RUN GAP AND PADS are render.h's: kRunGapU, the toolbar
    // pads, the panel's inset kEdgeInsetU — THE EDGE RULE
    // at well_columns, the strips' seats paint_handler.cpp's icon_row_seats
    // and bottom_row_seats. THE TIME FIELDS are plain text (architect
    // 2026-11-19), the overlays' label's slot paint_handler.cpp's time_label_w_px
    // (the specimen's advance rounded up to a whole U), the bottom icon
    // row's left run's pen render.h's kBottomLeftPenU.)

    // -- THE WELL (THE CANVAS COLUMN) -----------------------------------------
    // (The well runs the panel's whole width, 2026-11-19.)
    // THE CANVAS'S HORIZONTAL GRID (architect 2026-10-09, "grid 4 per half
    // channel"; Cool Edit's GrdL): each
    // channel's half height divided into this many, a line at every inner
    // division above and below the zero (render_canvas, render.h's THE
    // WAVEFORM block's canvas paragraph).
    int  grid_divisions;
    // (THE TRIM BAR'S HEIGHT is render.h's (2026-11-17), the 12-U
    // trough kTrimTroughU in THE WELL'S LANES block, which derives every
    // length of the well at the element.)
    // (THE CUE'S HORIZONTAL LENGTHS — the fill's pad and the
    // segment gap — are render.h's skin
    // lengths, cue_fill_pad_px and cue_segment_gap_px.)
};

inline constexpr ProgramSpec kProgramSpec = {
    .grid_divisions        = 4,
};

// (Every lane of the top icon row and of the bottom icon row is a skin
// length in U, render.h's case block; the well's lanes are device px
// derived at the element, render.h's THE WELL'S LANES block.)
// (THE LABEL CLEARS THE CHIP BY CONSTRUCTION: render.h's
// cue_label_lead_px is the chip's reach right and the gap, never negative,
// at every scale.)
// (The label's text stands one fill pad inside its fill — the fill's lead
// and pad are the label's lead: render.h's cue_fill_lead_px.)

#endif // WARPTEMPO_GUI_PROGRAM_SPEC_H
