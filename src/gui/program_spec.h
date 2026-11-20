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
// length, the `key_width` tuning key's W wide (36 its default, Arc's icon
// button, send Z) by the `key_height` tuning key's W tall (28 its default;
// render.h's case block, icon_case_w_px / icon_case_h_px), the face the
// whole key, its neighbors one W of ground away (key_gap_px, architect
// 2026-11-04, the unit); its glyph is centered on its face by its own
// ink-box road, its size following the key's height (paint_key_glyph).
// THE SIZE ON THE GLASS is gui_scale's (the device's choice, never this
// header's).
//
// THE SIMPLE LAYOUT (architect 2026-10-22, "the bevels are def part of the
// problem … redesign with dead simple layout", the mocks on the tablet), FLAT
// SINCE 2026-11-01 (materials.h; the well 2026-11-01 night, "THE MAIN PAGE
// IS SETTLED"): no ring, no stroke on the ground. THE PROGRAM'S LANES, top to
// bottom, ON ARC'S NUMBERS SINCE 2026-11-19 (send Z; DESIGN §1, the one
// table render.h's blocks derive): THE BAND at the window's top — THE
// TITLE BAR (28 W, render.h's title bar block), Arc's toolbar pads 7 W
// above the keys and 8 W below (render.h's toolbar pads), its keys in plain
// runs, the runs the `run_gap` tuning key's W apart (6 its default); then
// THE WELL, flat regions RUNNING THE PANEL'S WHOLE WIDTH, EDGE TO EDGE, its
// lines one W of `well_border` (render.h's THE WELL'S EDGES) — the line
// under the band, THE TRIM BAR (Arc's scrollbar: the 12-W trough, the pill
// on it), its line, THE RULER FLIPPED (16 W), its line,
// THE MARKER LANE of Cool Edit's cues (16 W, the ruler's height; the flag
// editor's box on its whole rows), its line, THE CANVAS — the waveform
// area, its grid and its center lines, its two channels' wells with the
// stereo divider between them — and the well's foot; then THE BOTTOM ICON
// ROW, 8 W, the case, 7 W; then THE STATUS BAR, its line and 22 W, on the
// app's foot — the window's, or the on-screen keyboard's top line while it
// stands (send BB, main.cpp's app_foot_y). The keys' outermost faces stand 6 W inside the panel
// (render.h's kEdgeInsetW and THE EDGE RULE at well_columns). (The band's
// head and foot lines and its panes, the tab strip under it, the column's
// air, the column's foot, the dock bar, row 8's bars and the border under
// row 8 stood 2026-10-09 to 2026-10-22; the program ring round the column
// 2026-10-22 to 2026-11-02; the well's frame and its margins 2026-11-17 to
// 2026-11-19; git history.)
//
// THE CUE AND THE PLAYHEAD'S HEAD ARE DRAWN IN SKIN LENGTHS (render.h's cue
// block and the label's lengths beside it): THE MARKER'S CHIP IS A
// SMOOTH-EDGED TRIANGLE, NO OUTLINE, AT ITS MARK'S EXACT PLACE, its apex on
// the stem's center (architect 2026-11-14 night, send J: "I think the
// chips should also go anti-aliased", "Agree on both."; cool_edit_paint.h's
// THE CUE'S CHIP), the bare triangle of the nominal rows (architect
// 2026-11-19, send AA: "the smaller version without the outline";
// render.h's cue_chip_h_px: 8 device px tall and 16 wide on the tablet at
// 200 %, 4 and 8 at 100 %), the ruler's head the same chip. ITS
// ROW COUNT IS NO FIELD HERE: it is a skin length, 4 W through scaled_px,
// rounded at the element (architect 2026-10-26 ~13:40, "why not just
// simple scale? tablet is only real surface"; render.h's cue_triangle_rows
// and kCueTriangleRowsW, the one owner — 8 px at 200 %, 4 at 100 %). THE
// LABEL'S LENGTHS ARE SKIN LENGTHS TOO, scaled with the text they place
// (the text round, architect 2026-11-03): the fill's pad, the segment gap
// and the flag editor's border (render.h's cue_fill_pad_px,
// cue_segment_gap_px, render_flag_editor_box), and THE LABEL'S LEAD from
// the column, THE CHIP'S REACH RIGHT AND A GAP OF 5 W FROM ITS CORNER
// (render.h's cue_label_lead_px and kCueLabelGapW, architect 2026-10-26
// ~12:40: the label follows the rows; 2026-11-19, send AA: "the text should
// move left to make sure that the margin remains the same" — 8 + 10 = 18
// device px on the tablet). Every line down the canvas is
// solid, one W (no dotted line since 2026-11-04). THE WELL'S
// ROWS are render.h's (the well's lanes block): skin lengths, hairlines and
// the faces' own metrics, each its own rounded part.
struct ProgramSpec {
    // (THE CASE HAS NO W TERM HERE: the key is render.h's case block, its
    // width the `key_width` tuning key's, 2026-11-06, and its height the
    // `key_height` tuning key's, 2026-11-03, its gap kKeyGapW, 2026-11-04.)
    // (THE STRIPS' RUN GAP AND PADS are render.h's: the `run_gap` tuning
    // key, the toolbar pads, the panel's inset kEdgeInsetW — THE EDGE RULE
    // at well_columns, the strips' seats paint_handler.cpp's icon_row_seats
    // and bottom_row_seats. THE TIME FIELDS are plain text since send AA
    // (2026-11-19), the overlays' slots paint_handler.cpp's (kEntryPadsW,
    // kEntryPenW), the bottom icon row's left run's pen render.h's
    // kBottomLeftPenW.)

    // -- THE WELL (THE CANVAS COLUMN) -----------------------------------------
    // (THE WELL'S SIDE MARGIN, 12 W round the frame of 2026-11-17, retired
    // 2026-11-19, send Z: the well runs the panel's whole width.)
    // THE CANVAS'S HORIZONTAL GRID (architect 2026-10-09, the design loop's
    // third mock set, "grid 4 per half channel"; Cool Edit's GrdL): each
    // channel's half height divided into this many, a line at every inner
    // division above and below the zero (render_canvas, render.h's row-6
    // canvas paragraph).
    int  grid_divisions;
    // (THE TRIM BAR'S HEIGHT is render.h's since 2026-11-17, the 12-W
    // trough kTrimTroughW in its well lanes block, which derives every
    // length of the well at the element; the 7-W field of 2026-11-01 night
    // retired with it.)
    // (THE CUE'S HORIZONTAL LENGTHS — the selected fill's pad and the
    // segment gap, in label quanta until 2026-11-03 — are render.h's skin
    // lengths since, cue_fill_pad_px and cue_segment_gap_px.)
    // (THE CANVAS DOTS — the playhead's period and phase and a cue's two
    // phases, Cool Edit's cue and range dots — left 2026-11-04 with the
    // dotted stems and cursor: every line on the canvas is solid, render.h's
    // marker-lane and playhead paragraphs.)
};

inline constexpr ProgramSpec kProgramSpec = {
    .grid_divisions        = 4,
};

// (THE AUTHORED TOTALS — the band's and row 8's strip pads, the lane
// table's record — left 2026-11-03: every lane of the band and of row 8 is
// a skin length in W, render.h's case block.)
// (THE CANVAS COLUMN'S W SUMS — the view bar's 7, the ruler's 12, the marker
// lane's 17, the stereo divider's 3 — left with the flat well, 2026-11-02:
// the well's lanes are device px derived at the element, render.h's well
// lanes block.)
// (THE LABEL CLEARS THE CHIP BY CONSTRUCTION: render.h's
// cue_label_lead_px is the chip's reach right and the gap, never negative,
// at every scale.)
// (The label's text stands one fill pad inside its fill — the fill's lead
// and pad are the label's lead: render.h's cue_fill_lead_px.)

#endif // WARPTEMPO_GUI_PROGRAM_SPEC_H
