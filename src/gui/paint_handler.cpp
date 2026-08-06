#include "paint_handler.h"

#include "icons.h"
#include "render.h"
#include "text_editor.h"
#include "text_shape.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"
#include "engine/engine_geometry.h"  // kRs

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// On-screen paint handler: on_redraw and its per-strip paint passes, and
// on_resize. The off-screen surfaces
// these passes blit — the waveform plate and the flag-rect
// cache — are produced in waveform_cache.cpp. Trim paints live per frame
// (paint_trim), out of any cache.

// -- The bottom row's shaped-text tier (row 7, 2026-08-01) ------------------
//
// MONOSPACE IS GONE FROM THE PRODUCT (architect 2026-08-01: "I wanted to get rid
// of monospace altogether — the last row should be the same font as the rest").
// Every string on the bottom line — the timestamp, the prompts, the
// queue/render/transient status, the resolved readout and the three editors'
// own text — is the redesign's sans at the redesign's size, shaped and painted
// through the ONE chokepoint like every other redesigned row. (The dirty mark
// used to be on this list; since 2026-08-01 it is in the WINDOW TITLE, which
// labwc paints — see GuiPlatform::apply_window_title.)
//
// WHAT THE TIMESTAMP LOST AND HOW IT IS REPLACED: a monospace face guaranteed
// the clock could not wiggle as its digits changed. Two facts replace that
// guarantee, and both are stronger than the face was. (1) Liberation Sans's
// lining digits are TABULAR — verified by shaping, every digit 0-9 advances
// exactly 9.0px at 16px, so "270:32.999" and "000:00.000" measure the same 80px
// — so the clock's own glyphs never move. (2) The line is laid out in FIXED
// SECTIONS (bottom_row_sections below), so nothing after the clock can move
// either, whatever the clock says.

// The bottom row's ONE face, selected on `cr`. Returns the scaled font every
// shape and paint on the row must share — the text_shape precondition is that a
// run is shown with the same font it was shaped with.
static cairo_scaled_font_t* select_bottom_row_face(cairo_t* cr) {
    cairo_select_font_face(cr, "sans",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    return cairo_get_scaled_font(cr);
}

// Shape and paint one run at (x, baseline) in `color`. The row's plain-text
// tier — the successor of text_display::draw_line, which died with the
// monospace path it drew in.
static void show_row_text(cairo_t* cr, cairo_scaled_font_t* font,
                          double x, double baseline,
                          std::string_view text, GuiColor color) {
    if (text.empty()) return;
    const text_shape::ShapedRun run = text_shape::shape_text_run(font, text);
    cairo_set_source_rgb(cr, color.r, color.g, color.b);
    text_shape::show_shaped_run(cr, run, x, baseline);
}

// THE LINE'S FIXED SECTIONS (the architect's kdenlive model, 2026-08-01, RELAID
// OUT the same day): nothing on the row moves when the timestamp's digits
// change. Every boundary is computed from SHAPED MAXIMA, never from the text
// currently on screen.
//
//   pad | C: the modal / status span | pad | A: the timestamp | pad
//
// THE CLOCK IS ON THE RIGHT AND THE MODAL TEXT ON THE LEFT (architect, at the
// live look — superseding the clock-first order the row shipped with a few
// hours earlier). The DIRTY DOT'S SECTION IS GONE from the line entirely: the
// dot now rides the WINDOW TITLE beside the project name, where labwc paints it
// (GuiPlatform::apply_window_title).
//
// A IS SIZED ON THE WIDEST DIGIT, not on a specimen that assumes the face
// (architect 2026-08-01: "use the widest digit in Liberation Sans or the avg
// linux sans"). The ten digits are shaped at the CURRENT scaled font, the widest
// advance wins, and the section is a specimen built from THAT digit —
// "DD:DD.DDD", the MM:SS.mmm shape with its 7 digit slots — shaped through the
// same one-run path the painted clock takes. On Liberation Sans the digits are
// tabular (all ten advance identically, verified on the resolved face), so this
// degenerates to exactly the width the clock paints; the derivation is what
// keeps it right if hinting at some scale, or a face swap, ever makes one digit
// wider than another.
//
// TWO MINUTE DIGITS, and longer sources TRUNCATE (the ruling and what it costs
// are at format_timestamp, time_format.h). The section is that format's width
// and no wider.
//
// A IS RIGHT-ALIGNED AGAINST THE WINDOW: its cell's RIGHT edge sits one pad in
// from the lane's right edge, so the cell — not the text — is what is anchored,
// and the clock's own glyphs still cannot walk (the cell is the widest specimen
// and the text inside it keeps starting at the cell's left pen, exactly as it
// did when the cell sat on the left).
//
// THE ONE 13px PAD IS USED THREE TIMES, unchanged from the shipped row and
// deliberately: the left lead-in before C, the inter-section gap between C and
// A, and the right margin after A. One constant, three uses, an
// eye-consistency choice.
//
// A'S CELL IS RESERVED WHETHER OR NOT THE CLOCK PAINTS, and C ends where that
// reservation begins — so C never jumps and never collides. C is CLIPPED at
// that boundary; if the modal text does not fit, it clips (architect: a screen
// too small for the line is a user problem, not a layout one). See
// paint_bottom_strip's clip block.
static constexpr const char* kTimestampShape = "DD:DD.DDD";

struct BottomRowSections {
    double a_x    = 0.0;   // the timestamp's pen (its reserved cell's left)
    double c_x    = 0.0;   // the modal / status span's pen
    double c_x1   = 0.0;   // and its clip boundary: one pad before A's cell
};

// The shaped width the section arithmetic needs, MEMOISED ON THE FONT SIZE.
// Deriving A costs eleven tiny shaping passes (ten digits plus the specimen) and
// they answer the same thing on every frame: the face is fixed ("sans") and the
// size is the only variable, so the size is the whole key. Single-threaded paint
// state — the waveform worker never reaches this file's bottom-row tier.
//
// ONE WIDTH, down from three: the dirty mark's cell and the shaped space that
// separated it from the clock died with the mark's move to the window title.
struct BottomRowTextMetrics {
    double px      = -1.0;   // the size these were measured at
    double a_w     = 0.0;    // the widest timestamp's shaped width
};
static BottomRowTextMetrics g_bottom_metrics;

static const BottomRowTextMetrics& bottom_row_text_metrics(
        cairo_scaled_font_t* font, double size_px) {
    if (g_bottom_metrics.px == size_px) return g_bottom_metrics;
    // The widest of the ten digits at this size, by shaped advance.
    char widest = '0';
    double widest_w = -1.0;
    for (char d = '0'; d <= '9'; ++d) {
        const char one[2] = {d, '\0'};
        const double w = text_shape::shape_text_run(font, one).width_px;
        if (w > widest_w) { widest_w = w; widest = d; }
    }
    std::string specimen(kTimestampShape);
    for (char& c : specimen) if (c == 'D') c = widest;

    g_bottom_metrics.a_w     = text_shape::shape_text_run(font, specimen).width_px;
    g_bottom_metrics.px      = size_px;
    return g_bottom_metrics;
}

static BottomRowSections bottom_row_sections(cairo_scaled_font_t* font,
                                             const GuiRect& lane) {
    const double pad = static_cast<double>(bottom_row_pad_x());
    const BottomRowTextMetrics& m =
        bottom_row_text_metrics(font, redesign_font_size_px());
    BottomRowSections s;
    // Every boundary lands on an integer pen so the hinted glyphs stay crisp,
    // the same rounding convention the redesigned rows' label origins take.
    const double lane_x1 = static_cast<double>(lane.x) +
                           static_cast<double>(lane.w);
    s.a_x  = std::nearbyint(lane_x1 - pad - m.a_w);
    s.c_x  = std::nearbyint(static_cast<double>(lane.x) + pad);
    s.c_x1 = std::nearbyint(s.a_x - pad);
    return s;
}

// The three bottom-strip editors (settings / load / BPM) share this one
// body, differing only in prefix and which State they read. It shapes PREFIX AND
// PENDING AS ONE RUN — so the pair kerns exactly as it paints — and addresses
// the pending half through that run's own byte boundaries, which it publishes
// for the pointer path (AppState::BottomEditorText).
//
// ALL THREE INHERIT THE INVALID FLASH'S MARKER-FLAG BOX TOGETHER (2026-08-02,
// step 1 below) — they are the complete set of surfaces painting through here,
// and every one of them fits it: each is a prefix plus one editable run on one
// line, which is the exact shape the box wraps, and none carries a second field,
// a multi-line payload or a box of its own for it to fight. Their prefixes
// differ in width and nothing else, and the box is measured off the RUN, not off
// a prefix.
//
// NO VIEW OFFSET, deliberately: unlike the flag editor's unrolled box these
// editors do not scroll. That was the monospace path's intent too and it is
// kept — the settings and commit strings that reach the boundary are
// pathological, and a scrolling field here would need machinery this row does
// not want.
//
// WHAT THE 2026-08-01 RELAYOUT CHANGED ABOUT THAT CLAUSE, stated plainly: the
// editors used to run off the RIGHT EDGE OF THE WINDOW, because C ran to the
// window edge and nothing clipped it. C now ends one pad before the timestamp's
// reserved cell and the whole section is CLIPPED there (paint_bottom_strip), so
// an over-long editor string is cut at that boundary instead of at the window's.
// The intent is unchanged in substance — the editor still does not scroll and
// still has no view offset — and only the boundary moved: an editor is modal
// text in section C, and C's contents clip alike. The caret can therefore sit
// outside the clip on a pathological string; the accepted cost is the same one
// the run-off always carried (the text was off-window before), and the pointer
// mapping is unaffected because BottomEditorText publishes unclipped geometry.
//
// The published byte geometry is the PAINTER's, clip or no clip: a click past
// the boundary maps through the same byte_x table, which is exactly what the
// unclipped run-off did.
static void render_bottom_strip_editor(cairo_t* cr,
                                       AppState& app,
                                       cairo_scaled_font_t* font,
                                       const text_editor::State& ed,
                                       const char* prefix,
                                       double origin_x,
                                       double baseline_y,
                                       int band_y, int band_h) {
    const std::string prefix_s(prefix);
    const std::string full = prefix_s + ed.pending;
    const text_shape::ShapedRun run = text_shape::shape_text_run(font, full);
    const std::vector<double> bx = text_shape::byte_offsets_px(run, full.size());
    const size_t p0 = prefix_s.size();

    // PUBLISH the pending half's geometry: origin at its byte 0, boundaries
    // rebased to it. Same shape as FlagEditorBox's pair, so editor_byte_index_at
    // searches this exactly as it searches the flag editor's.
    AppState::BottomEditorText& out = app.bottom_editor_text;
    out.valid         = true;
    out.text_origin_x = origin_x + bx[p0];
    out.byte_x.clear();
    out.byte_x.reserve(ed.pending.size() + 1);
    for (size_t i = p0; i < bx.size(); ++i) out.byte_x.push_back(bx[i] - bx[p0]);

    // THE GLYPH INK BAND — the caret's and the selection highlight's shared
    // vertical extent, the face's own ascent-to-descent about the baseline. The
    // retired monospace box derived the same band by inverting a chip formula;
    // here the extents ARE the band, with no box to invert.
    //
    // THE INVALID FLASH LEFT THIS BAND in 2026-08-02: it is a marker-flag box
    // now and takes the flag's height and baseline offset instead. The caret and
    // the highlight KEEP the ink band deliberately — they are the editor's
    // resting furniture, present on every frame whether or not a box is, and the
    // two extents agree to within a pixel anyway (a 19-row flag interior against
    // this face's 19-row ascent+descent at gui_scale 100), so the flash contains
    // them exactly as the flag editor's box contains its own.
    const double sel_x0 = origin_x + bx[p0];
    const double sel_x1 = origin_x + run.width_px;

    // 1. THE INVALID FLASH, and it is the box's whole remaining visual.
    //
    //    IT IS A MARKER FLAG NOW (architect 2026-08-02: "the settings editor
    //    still uses the old kaccent color — make it like the flag editor when
    //    user enters invalid submission. same padding and size — basically it
    //    should look like a marker flag in terms of dimensions"). The box is
    //    built from THE MARKER FLAG'S OWN constants — called, never copied, so a
    //    flag retune moves this surface with it — in the flag's own paint order:
    //    the 1px left border (kMarkerFlagBorder at full strength — the flag's
    //    border takes the disabled blend on a disabled MARKER, and a bottom
    //    editor has no marker and no disabled state to inherit), the fill and
    //    its 1px top edge in the marker lane's OWN red pair
    //    (kMarkerFlagFillRed / kMarkerFlagEdgeRed — the exact pair the flag
    //    editor flashes, so there is ONE invalid red in the product and no
    //    drift), the flag's two pads around the run, the marker lane's height,
    //    and the flag's baseline offset, which is what seats the box on the text
    //    the way a flag sits on its label instead of merely being lane-tall
    //    somewhere on the row. The pre-redesign dark-red chip pair leaves with
    //    it — the last two tunable colours in the tree, which is what let the
    //    whole colors.conf system retire the same day.
    //
    //    THE SUBJECT IS UNCHANGED — the EDITABLE RUN, not the prefix. The prefix
    //    names the field; what failed to parse is what the user typed, and the
    //    box marks that. So the left pad reaches a pixel or two back over the
    //    prefix's trailing space (its glyphs repaint over the box in step 3
    //    regardless), exactly as a flag's left pad sits between its border and
    //    its first glyph.
    //
    //    THE NORMAL FACE IS DELIBERATELY UNCHANGED: at rest these editors paint
    //    NO box, and the flag editor's marker-coloured ground is not borrowed
    //    here. The architect's sentence is about the INVALID submission and the
    //    DIMENSIONS, and the reason it stops there is structural — the flag
    //    editor's fill is the edited MARKER'S OWN CLASS COLOUR, it is that
    //    marker's flag unrolled; a settings, load or BPM editor edits no
    //    marker and has no class, so kMarkerFlagFill here would be a purple that
    //    names nothing, on a row whose ground is its own sampled surface. Red is
    //    different in kind: it is a STATE, not an identity, and every editor in
    //    the product can enter it. The one box these editors have therefore
    //    takes the flag's anatomy; the resting row stays the row.
    //
    //    NO DEGENERATE CASE IS LEFT. An EMPTY invalid run needed a forced 1px
    //    width and a negative-inset guard while the box was a RING made by
    //    insetting a fill; this box is pad + run + pad under a top BAND, so an
    //    empty run is an ordinary pads-wide box and every rectangle here is
    //    positive by construction. VERTICAL OVERFLOW needs no guard either: the
    //    box is 20px at gui_scale 100 against a 31px content band and scales
    //    with it, and section C's clip (paint_bottom_strip) bounds this painter
    //    to that band the way it bounds the glyphs.
    if (ed.red) {
        const int pad_l    = marker_flag_pad_left_px();
        const int pad_r    = marker_flag_pad_right_px();
        const int edge_h   = marker_flag_edge_h_px();
        const int border_w = marker_flag_border_px();
        const int rx0   = static_cast<int>(std::nearbyint(sel_x0));
        const int rx1   = static_cast<int>(std::nearbyint(sel_x1));
        const int run_w = (rx1 > rx0) ? (rx1 - rx0) : 0;
        const int fx = rx0 - pad_l;
        const int fw = pad_l + run_w + pad_r;
        const int fy = static_cast<int>(std::nearbyint(baseline_y)) -
                       marker_flag_baseline_px();
        const int fh = marker_lane_h_px();
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kMarkerFlagBorder.r, kMarkerFlagBorder.g,
                             kMarkerFlagBorder.b);
        cairo_rectangle(cr, fx - border_w, fy, border_w, fh);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, kMarkerFlagFillRed.r, kMarkerFlagFillRed.g,
                             kMarkerFlagFillRed.b);
        cairo_rectangle(cr, fx, fy, fw, fh);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, kMarkerFlagEdgeRed.r, kMarkerFlagEdgeRed.g,
                             kMarkerFlagEdgeRed.b);
        cairo_rectangle(cr, fx, fy, fw, edge_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // 2. The selection highlight, from the same boundaries the glyphs came from.
    const bool has_sel = text_editor::has_selection(ed);
    const size_t s0 = p0 + static_cast<size_t>(text_editor::selection_start(ed));
    const size_t s1 = p0 + static_cast<size_t>(text_editor::selection_end(ed));
    if (has_sel) {
        const int hx0 = static_cast<int>(std::nearbyint(origin_x + bx[s0]));
        const int hx1 = static_cast<int>(std::nearbyint(origin_x + bx[s1]));
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        cairo_rectangle(cr, hx0, band_y, (hx1 > hx0) ? (hx1 - hx0) : 1, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // 3. Prefix and pending in one pass — one run, one paint.
    cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                         kRedesignLabel.b);
    text_shape::show_shaped_run(cr, run, origin_x, baseline_y);

    // 4. The selected substring repainted in the ground colour for contrast, the
    //    WHOLE run re-shown under a clip — shaping the substring on its own
    //    could kern its first glyph differently and shift the ink. It stays the
    //    ROW GROUND rather than following the flash box's fill the way the flag
    //    editor's does: the flag editor always has a box to knock its glyphs out
    //    of, this one has a box only while invalid, and dark-on-white stays the
    //    legible pair in both states.
    if (has_sel) {
        cairo_save(cr);
        cairo_rectangle(cr, origin_x + bx[s0], static_cast<double>(band_y),
                        bx[s1] - bx[s0], static_cast<double>(band_h));
        cairo_clip(cr);
        cairo_set_source_rgb(cr, kRedesignTabGround.r, kRedesignTabGround.g,
                             kRedesignTabGround.b);
        text_shape::show_shaped_run(cr, run, origin_x, baseline_y);
        cairo_restore(cr);
    }

    // 5. The caret: a blink-gated 1px filled column on the cursor's own byte
    //    boundary, AA off.
    if (text_editor::cursor_visible_now(ed)) {
        const int cursor_pos =
            std::clamp(ed.cursor_pos, 0, static_cast<int>(ed.pending.size()));
        const double cx =
            origin_x + bx[p0 + static_cast<size_t>(cursor_pos)];
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        cairo_rectangle(cr, static_cast<int>(std::nearbyint(cx)), band_y,
                        1, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }
}

// -- GuiPaintHandler::paint_flag_annotations -----------------------------

void GuiPaintHandler::paint_flag_annotations(cairo_t* cr,
                                             const GuiRect& top_strip) {
    // Flag annotations in the top strip. The marker/phase-reset flag BOXES live
    // on flag_cache.surface (rebuilt from on_tick via maybe_rebuild_flag_cache);
    // this pass is a pure blit. (Trim's bar and endcaps left this cache for the
    // live paint_trim pass.) The boxes CARRY THEIR TEXT since row 5 — the marker-text lane
    // that used to show it beneath them is gone — so the only thing painted
    // after this blit in that band is the open editor's overlay
    // (render_flag_editor_box) — which is why the editing target's box is NOT in
    // this surface at all: it is skipped at cache-build time (2026-08-02) rather
    // than painted here and covered, because the overlay is narrower than the
    // committed box whenever the edited text is shorter. Like the other
    // caches, the surface may be null on the very first paint after a load
    // (before the first rebuild fires); the blit is skipped and the background
    // shows through for that one frame.
    if (flag_cache.surface) {
        cairo_save(cr);
        cairo_rectangle(cr, top_strip.x, top_strip.y,
                        top_strip.w, top_strip.h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, flag_cache.surface,
                                 top_strip.x, top_strip.y);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

// -- The redesigned rows: paint_menu_row / paint_toolbar_row / paint_tab_row
// -- / paint_icon_row -------------------------------------------------------

namespace {

// THE KDENLIVE REDESIGN'S SHARED TEXT FACE. The SIZE (kRedesignFontSizePt /
// redesign_font_size_px) moved to render.h when row 5's marker flags began
// shaping their own labels inside render.cpp — one design size cannot have two
// definitions. THE FACE itself is stated here, where every row selects it: it
// is cairo's toy "sans" selector, which fontconfig resolves to Liberation Sans
// on the target — the family the crops were rendered in. The monospace face
// survives untouched on every un-redesigned surface, and each redesign row
// moves its own text over.

// (The authored-length -> device-pixels conversion every dimension below takes
// is scaled_px, render.h — the ONE conversion the whole scale axis shares.)

// THE GROUND ROWS 1 AND 2 PAINT ON, in one owner because three things read it:
// the ground fill itself, the disabled face's mix target, and the click face's.
// Focused it is the crops' #292c30; unfocused it darkens to #202326 with the
// labwc titlebar above (the ruling and the constant's provenance are at
// kRedesignRowGroundUnfocused, render.h). Row 3 does NOT call this — its ground
// is a fixed value that already equals the unfocused shade, so it has nothing
// to swap.
//
// THE TWO MIXES FOLLOW THE GROUND rather than the focused constant, which is
// the whole reason this is a function and not two literals at the fill sites: a
// disabled label is measured as a FRACTION of itself over the ground it sits on,
// and the click fill as a 30% accent tint of that same ground, so both keep
// their measured relationship in either state instead of drifting when only the
// ground moves.
GuiColor redesign_row_ground(const AppState& app) {
    return app.window_activated ? kRedesignRowGround
                                : kRedesignRowGroundUnfocused;
}

// ROW 1, measured off the two 46x30 crops
// (tmp/screenshots/kdenlive/redesign/row_1_button_{rest,hover}.png). The row
// height itself lives in render.h as kMenuRowHeightPx, because main.cpp's lane
// table needs it.
//
// THE CSS FLOAT MODEL is the ruled layout vocabulary (architect 2026-07-31): a
// flat button FILLS ITS WHOLE ROW and no margin or inset exists unless the
// architect states one. The hover pill therefore spans the row's full CONTENT
// height — the 1px vertical inset that stood here was a misread of the crop
// (those rows were the title-bar seam, not design). That content height is
// kMenuRowHeightPx = 34, not the crop's 30, and the lane is one pixel taller
// still: row 1 gained a 1px MARGIN-BOTTOM (kMenuRowMarginPx) which is lane, not
// button — the pill fills the 34 and stops at the margin strip.
constexpr double kMenuLabelPadPx   = 10.0;   // per side, sets the button width
constexpr double kMenuPillRadiusPx = 5.0;    // the crop's AA fits r ~ 4.6

// THE MENU ROW'S BUTTONS, in painted order — flush from the row's left edge and
// ADJACENT WITH NO GAP, the css float model's default (the architect states a
// gap where one exists; row 2's 2px invisible separator is the only one in the
// redesign so far, and row 1 was never given one).
//
// EVERY GUI-FACING STRING CARRIES PROPER CAPITALIZATION (architect 2026-08-01,
// generalizing the row-by-row carve-out that started here): labels are proper
// nouns/labels, messages and prose are sentence case. THE CASE SPLIT IS OVER
// (2026-08-02): the terminal round landed, so stderr/stdout prose follows the
// same rule and there is ONE rule for both surfaces. What STAYS lowercase is
// user-authored data (marker labels, titles, filenames), literal settings KEY
// names shown as keys, and the routing/filename tokens that are data rather
// than prose.
struct MenuButtonDef {
    RedesignButton id;
    const char*    label;
};
// SETTINGS SITS LAST (architect 2026-08-03, moving it behind Navigation). The
// float is adjacent with no gap and the walk below is a pure shaped-run walk, so
// the order lives HERE and in the roster enum (app_state.h, whose comment states
// that enum order IS painted order) and in nothing else — no width, pad or
// anchor term reads it.
constexpr MenuButtonDef kMenuButtons[] = {
    {RedesignButton::Quit,       "Quit"},
    // THE SECOND DROPDOWN (architect 2026-08-02): a COMMAND MENU of the zoom and
    // stepping commands, sharing the row's usual no gap. Like Settings its
    // action is a popup toggle rather than a chord, and the two share one popup
    // state — see AppState::Dropdown.
    {RedesignButton::Navigation, "Navigation"},
    {RedesignButton::Settings,   "Settings"},
};

// ROW 1'S RIGHT FLOAT — THE VIEW BAR (architect 2026-08-02), kdenlive's
// workspace switcher (kden1.png's blue "Logging | Editing | Audio | Effects |
// Color" bar, the one row the redesign had left out) reborn as the three
// ABSOLUTE VIEW SELECTORS: S+W, T+P, T+W, which are bare 1/2/3.
//
// THE CSS FLOAT VOCABULARY, and this is its first RIGHT float — recorded here
// because the model has only ever walked left-to-right before. The div's right
// edge is flush with the WINDOW's right edge (no margin stated, so none exists)
// and it spans the row's full CONTENT height; the layout walk inside it runs
// left to right from the div's own left edge, exactly like every other row's.
// Only the div's ORIGIN is new, and it needs all three widths before it can be
// placed, which is why the labels are shaped up front rather than in the walk.
//
// MARGINS DO NOT COLLAPSE HERE (a stated fact, like every margin in this
// redesign): each button carries 1px on all four sides, so two adjacent buttons
// sit 2px apart and the div is 1 + w + 2 + w + 2 + w + 1 wide. The vertical pair
// is what sets the row's content height — 34 = 1 + 32 + 1 exactly.
//
// A BUTTON'S OWN BOX is kdenlive's, read straight off the 82px "Logging" crop's
// scanline: [frame 1][fill 12][text][fill 12][frame 1], so the width is the
// shaped label plus 2*(border + padding) = label + 26, and the frame is drawn
// INSIDE the box (the row-3 side-border precedent — a face, never a size
// change).
constexpr double kViewBarBtnMarginPx = 1.0;    // all four sides, no collapse
constexpr double kViewBarBtnBorderPx = 1.0;    // drawn inside the box
constexpr double kViewBarBtnPadPx    = 12.0;   // per side, inside the border
constexpr double kViewBarRadiusPx    = 5.0;    // the redesign's one radius

struct ViewBarButtonDef {
    RedesignButton id;
    const char*    label;
};
constexpr ViewBarButtonDef kViewBarButtons[] = {
    {RedesignButton::ViewSW, "S+W"},
    {RedesignButton::ViewTP, "T+P"},
    {RedesignButton::ViewTW, "T+W"},
};
constexpr int kViewBarButtonCount =
    static_cast<int>(std::size(kViewBarButtons));

// ONE FACE PAINTER FOR THE WHOLE BAR, parameterized on the BAR BACKGROUND —
// the redesign_row_ground pattern, for its reason: every fill and every frame
// in the nine crops is either the background flat, the accent, or a FIXED LIFT
// OF THAT BACKGROUND, so the focused and unfocused bars are one rule applied to
// two grounds rather than two hand-spelled state tables that could drift.
// Provenance, the two fractions and their per-channel fits are at
// kRedesignViewBarBg (render.h).
struct ViewBarFace {
    GuiColor fill;
    GuiColor frame;
    bool     filled;   // false = the bar background already shows the rest face
    bool     framed;
};
ViewBarFace view_bar_face(GuiColor bg, bool focused, bool hovered,
                          bool selected, bool pressed) {
    ViewBarFace f{};
    // HOVER ONLY EVER MOVES THE OUTLINE, in BOTH focus states (architect
    // 2026-08-02, from the live test) — so the interior is the selected fact
    // alone and `hovered` never reaches this term.
    //
    // THIS SUPERSEDES A CROP. row_right_disabled_hover (the UNFOCUSED hover)
    // shows the interior lifted to #44464a where row_right_hover (the FOCUSED
    // one) keeps the flat #1e5774, and that asymmetry was reproduced faithfully
    // before the ruling. The architect ruled the simpler rule instead: hover is
    // the frame's job on both grounds. The crops still govern everything else
    // here — the SELECTED lifts are unchanged in both focus states, which is why
    // the two fractions below survive the ruling intact.
    const bool lift_fill  = selected;
    // THE PRESSED INTERIOR IS THE FOCUSED FACE'S ALONE, because there is no
    // unfocused click crop and there is no gesture that would need one: labwc is
    // click-to-focus, so the press that would show it ACTIVATES THE WINDOW
    // first and the button is already focused by the time it paints. If a press
    // ever does land unfocused, this falls through to the hover/selected face
    // rather than inventing a shade the crops never showed.
    const bool click_fill = pressed && focused;
    f.filled = click_fill || lift_fill;
    f.fill   = click_fill
                   ? mix_color(kRedesignAccent, bg, kRedesignClickMix)
                   : mix_color(kRedesignViewBarLiftBase, bg,
                               kRedesignViewBarSelectedMix);
    f.framed = hovered || pressed || selected;
    // The pointer's frame is the accent; a resting selected button's is the
    // calmer lift — row 4's rule, on this bar's own pair of colors.
    f.frame  = (hovered || pressed)
                   ? kRedesignAccent
                   : mix_color(kRedesignViewBarLiftBase, bg,
                               kRedesignViewBarFrameMix);
    return f;
}

// ROW 2 — THE TOOLBAR, measured at 100% off row_2_button_{rest,hover}.png
// (81x32), row_2_separator.png (1x34) and row_2_border_bottom.png. The lane
// metrics (44 content + 1 border) live in render.h with row 1's, for the same
// reason.
//
// THE CSS BOX MODEL, spelled in the walk below: the row's own left PADDING, a
// separator with 5px MARGIN on all four sides (5 + 34 + 5 = the 44 content
// height exactly), and buttons with a 6px vertical margin and NO horizontal
// margin — adjacency instead inserts a 2px INVISIBLE SEPARATOR, which is why
// that step belongs to the walk (between two buttons) and not to a button's own
// box. Inside a button the terms are paddings: 9 left, the 22px icon, a 4px
// icon->label gap, the shaped label, 10 right.
//
// THE ICON BOX IS 22, kdenlive's own size (architect 2026-07-31, re-derived off
// row_2_icon_difference.png), and the three internal terms moved WITH it so the
// Save button stays EXACTLY 81 wide: 9 + 22 + 4 + 36 + 10 = 81. The old
// 12/16/7 was a misread of the icon's INK for its BOX — document-save's path
// spans units 3..19 of the 22-unit viewBox, so a 22px box at pad 9 puts its ink
// at x 12..27 and (vertically centered, (32-22)/2 = 5) at rows 8..23, which is
// the rest crop's ink EXACTLY. The 16px box the old numbers assumed would have
// had to draw ink at 16 units wide inside a 16px box — no margin at all, which
// no Breeze icon has. Both readings put the same pixels in the same places for
// the LABEL; only the icon changed size, and the crop settles which is right.
constexpr double kToolbarRowPadLeftPx  = 5.0;
constexpr double kToolbarSepMarginPx   = 5.0;   // all four sides
constexpr double kToolbarSepWidthPx    = 1.0;
constexpr double kToolbarSepHeightPx   = 34.0;
constexpr double kToolbarBtnMarginYPx  = 6.0;   // top and bottom -> 32 tall
constexpr double kToolbarBtnGapPx      = 2.0;   // the invisible separator
constexpr double kToolbarBtnPadLeftPx  = 9.0;
constexpr double kToolbarIconPx        = 22.0;
constexpr double kToolbarIconGapPx     = 4.0;
constexpr double kToolbarBtnPadRightPx = 10.0;
constexpr double kToolbarHoverRadiusPx = 5.0;
constexpr double kToolbarHoverStrokePx = 1.0;

// What precedes a toolbar button in the layout walk.
enum class ToolbarLead {
    Separator,   // 5px margin, the 1px line, 5px margin
    Gap,         // the 2px invisible separator between adjacent buttons
};

// THE PAINTER'S HALF OF THE BUTTON ROSTER (the roster itself is
// RedesignButton, app_state.h): each row 2 button's label, icon and what leads
// it, in painted order. The press claim's chord table (input_pointer.cpp) is
// the other half; both key off the same ids.
struct ToolbarButtonDef {
    RedesignButton id;
    const char*    label;
    icons::Icon    icon;
    ToolbarLead    lead;
};
constexpr ToolbarButtonDef kToolbarButtons[] = {
    {RedesignButton::Save,   "Save",   icons::Icon::DocumentSave, ToolbarLead::Separator},
    {RedesignButton::Undo,   "Undo",   icons::Icon::EditUndo,     ToolbarLead::Separator},
    {RedesignButton::Redo,   "Redo",   icons::Icon::EditRedo,     ToolbarLead::Gap},
    {RedesignButton::Render, "Render", icons::Icon::MediaRecord,  ToolbarLead::Gap},
};

// ROW 3 — THE TAB ROW, measured at 100% off row_3_tab_{rest,hover,selected}.png
// (30 tall) with the padding taken from row_3_tab_pcmanfmqt.png and the border
// color from row_3_bottom_border.png. The lane metrics (30 content + 1 border)
// live in render.h with rows 1 and 2's, for the same reason.
//
// THE CSS FLOAT MODEL AT ITS PUREST: the tabs are FLUSH at the row's left edge,
// margin zero, adjacent with no gap, each filling the full 30px content height.
// A tab's box is its two 10px paddings around the shaped label OR the minimum
// width below, whichever is larger — and never a margin.
//
// THE MINIMUM WIDTH is what keeps a one-glyph label from producing a stub of a
// tab (architect 2026-07-31, ruled when the labels shortened to "A"/"B").
// Reconstructed from row_3_min_width.png, an 80x30 Breeze tab carrying a tiny
// label and a close button this product has no analogue for: behind the 1px
// left border the label field opens at x=1 and its ink sits at x 24..35,
// centered on 29.5 — which places the field's own center at 29 and therefore
// its right edge at 58, giving a 57px field plus the 1px border. 58 is the
// spec. The close button is IGNORED: it lives to the right of the field
// (ink at 60..67) and has no counterpart here.
//
// AT THE MINIMUM THE LABEL IS CENTERED IN ITS FIELD, not left-padded — the two
// paddings are a FLOOR term, not an anchor, so a label narrower than the minimum
// sits in the middle of the field rather than hugging its left edge. THE FIELD,
// NOT THE TAB (2026-08-01): the lock slot below adds its own width on the right,
// and centering in the total would push the A/B off-centre in the space the eye
// reads as the tab's label area.
constexpr double kTabLabelPadPx      = 10.0;  // per side, the width floor's term
constexpr double kTabMinWidthPx      = 58.0;  // see the reconstruction above
// THE 1px SIDE BORDERS OF THE SELECTED TAB ARE DRAWN INSIDE ITS OWN BOX, on the
// box's outermost columns, NOT outside it. That is a deliberate departure from
// "a border sits outside the content" and it is what keeps the geometry stable:
// a border outside would make the selected tab 2px wider than the same tab
// unselected, so switching tabs would visibly shove the other one sideways.
// Selection is a FACE, not a size.
constexpr double kTabTrimHeightPx    = 3.0;   // the selected tab's blue top
constexpr double kTabBorderPx        = 1.0;   // side borders / hover edge
// The selected tab's top corners round at r = 5, and that is MEASURED, not
// assumed: integrating the row_3_tab_selected.png corner's uncovered area
// against a quarter-disc gives 1.453 px^2 in row 1 for r = 5 against the crop's
// measured 1.453 (r = 4 predicts 1.204), and rows 0 and 2 agree to within a
// hundredth. It also puts the tab in the same corner family as row 1's hover
// pill and row 2's hover outline, both r = 5. The arc's uncovered pixels show
// whatever is behind them, which here is the row ground the lane fill already
// laid down.
constexpr double kTabCornerRadiusPx  = 5.0;
// THE LOCK SLOT — its OWN RESERVED SPACE, which ADDS to the tab's width
// (architect 2026-08-01, correcting the first build: the lock is not a
// conditional overlay on the label field, it is the pcmanfm CLOSE-ICON slot,
// always present on both tabs, and the tab is as wide as its label field plus
// that slot).
//
// THE TWO CROPS AGREE ON EXACTLY TWO NUMBERS, and those are what this is built
// from. In row_3_min_width.png (80 wide) the close-icon ink runs columns 59..68
// with the tab's right border at 79; in row_3_tab_pcmanfmqt.png (87 wide) it
// runs 66..75 with the border at 86. So in BOTH: the ink is 10px wide, and its
// right edge sits exactly 11px inside the border — i.e. a 22-unit Breeze glyph
// in a 16px box (its own inset gives the 10px ink) with 8px between that box
// and the tab's right edge. 16 and 8 are the spec; everything else about those
// foreign tabs (their label fields, their paddings) is theirs and not ours.
//
// THE TAB THEREFORE GROWS BY THE SLOT, and only by the slot: tab width = the
// label field (the same max(minimum, shaped + 2*pad) it always was) + this. The
// side-border ruling is untouched — selection is still a face and not a size,
// because the slot is there in EVERY state on BOTH tabs, so nothing a tab does
// can shove its neighbour sideways.
//
// CONFIRMED AGAINST row_3_tab_pcmanfm-qt_close_hover.png (2026-08-01), the same
// 80x30 tab with its close button HOVERED — which is the crop that finally shows
// the slot's own box instead of only its ink. The hover face spans x 56..71 and
// y 7..22: a 16x16 box, vertically centred in the 30px row ((30-16)/2 = 7,
// exactly what this painter computes), with its right edge 8px inside the tab's
// right edge (72..79). Both authored numbers reproduce it bit-for-bit, and the
// plain crop's 10px ink (59..68) centres in that box at 63.5 — so the box, the
// margin and the ink placement are all measured facts now rather than one
// measurement and two inferences. Nothing changed.
//
// THE REFERENCE HAS A HOVER FACE AND WE DELIBERATELY DO NOT: that 16x16 box is
// filled #7d343d — a destructive-action red — under the hovered close glyph. It
// is recorded because it is what the reference does, not because it is wanted:
// our slot's act is a lock TOGGLE, not a destructive close, and no hover face
// has been asked for. If one is ever wanted, this crop is its measurement.
constexpr double kTabLockBoxPx     = 16.0;
constexpr double kTabLockMarginPx  = 8.0;   // box's right edge to the tab's
constexpr double kTabLockSlotPx    = kTabLockBoxPx + kTabLockMarginPx;

// THE PAINTER'S HALF OF THE TAB ROSTER: each tab's roster id, its A/B letter
// and its label. The press claim (input_pointer.cpp) reads the same ids out of
// app.redesign_buttons; the letter is what the paint compares against
// app.active_tab_view, so the selected tab is read LIVE every paint and there
// is no second copy of "which tab is current" anywhere.
struct TabDef {
    RedesignButton id;
    char           letter;
    const char*    label;
};
constexpr TabDef kTabs[] = {
    {RedesignButton::TabA, 'A', "A"},
    {RedesignButton::TabB, 'B', "B"},
};

// A rounded rectangle from four quarter-circle arcs, used FILLED for row 1's
// hover pill and STROKED for row 2's hover outline.
void redesign_rounded_rect_path(cairo_t* cr, double x, double y,
                                double w, double h, double r) {
    constexpr double kPi = 3.14159265358979323846;
    if (r > w * 0.5) r = w * 0.5;
    if (r > h * 0.5) r = h * 0.5;
    if (r <= 0.0) { cairo_rectangle(cr, x, y, w, h); return; }
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -0.5 * kPi, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0,         0.5 * kPi);
    cairo_arc(cr, x + r,     y + h - r, r, 0.5 * kPi,   kPi);
    cairo_arc(cr, x + r,     y + r,     r, kPi,         1.5 * kPi);
    cairo_close_path(cr);
}

// A FACE BOX: the half-stroke-inset rounded rect every redesigned button-like
// surface draws its fill and its frame on. ONE PATH, filled and/or stroked —
// never a fill on the full box under a stroke on an inset one — so the fill's
// edge and the stroke's centreline cannot describe different rectangles. THE
// INVARIANT, stated once here and pointed at from every caller: the frame
// insets by HALF ITS OWN WIDTH on all four sides, so the band lands on the
// box's outermost pixel ring with no straight edge antialiased — the +0.5
// half-pixel alignment at a 1px stroke, the integer bound at an even one, both
// parities from the one expression — and the radius insets by the SAME half so
// the corner stays concentric with the box. `radius` is the authored (outer)
// corner radius; nullptr for either colour omits that pass.
//
// Callers: the view-bar buttons, the icon-row buttons, the popup chrome
// (paint_popup_chrome) and the dropdown's hovered item. TWO surfaces in the
// family deliberately keep their own bodies: the selected tab strokes the
// OPEN-BOTTOM redesign_rounded_top_rect_path (a shared closed box would seal
// it), and row 2's hover outline passes its radius UN-inset — its arc is not
// concentric with any fill (there is none at rest) and its painted pixels are
// the shipped ones, so it stays as it is rather than being harmonized.
void redesign_face_box(cairo_t* cr, int x, int y, int w, int h,
                       int lw, double radius,
                       const GuiColor* fill, const GuiColor* line) {
    const double half = static_cast<double>(lw) * 0.5;
    redesign_rounded_rect_path(cr, x + half, y + half,
                               static_cast<double>(w - lw),
                               static_cast<double>(h - lw),
                               radius - half);
    if (fill != nullptr) {
        cairo_set_source_rgb(cr, fill->r, fill->g, fill->b);
        if (line != nullptr) cairo_fill_preserve(cr);
        else                 cairo_fill(cr);
    }
    if (line != nullptr) {
        cairo_set_source_rgb(cr, line->r, line->g, line->b);
        cairo_set_line_width(cr, static_cast<double>(lw));
        cairo_stroke(cr);
    }
}

// THE BUTTON-FACE PUBLICATION, one writer so a row cannot forget a field: the
// painter stashes the rect it painted plus the LIVE enabled/selected bits
// (redesign_button_enabled / redesign_button_selected — the same predicates
// main.cpp's per-tick drift comparator replays), and returns the face so the
// caller reads .hovered / .selected back for its own paint. Every row painter
// publishes through here at the top of its per-button body; the comparator's
// vector is total over the roster, so the stash is written for every id — a
// button with no selected state simply stores the predicate's constant false.
// (The tab row's hand-copied stash once omitted `selected`, which made the
// comparator disagree with the painter on the active tab EVERY tick — a
// permanent idle full-top-strip repaint. This owner is why that class of
// omission cannot recur.)
AppState::RedesignButtonFace& publish_button_face(
    AppState& app, int64_t total_frames, RedesignButton id,
    const GuiRect& rect) {
    AppState::RedesignButtonFace& face =
        app.redesign_buttons[redesign_button_index(id)];
    face.rect     = rect;
    face.enabled  = redesign_button_enabled(app, total_frames, id);
    face.selected = redesign_button_selected(app, id);
    return face;
}

// ROW 4 — THE ICON ROW, measured at 100% off the five 32x32 state crops
// (row_4_button_{rest,hover,click,selected,selectedhover}.png),
// row_4_separator.png (1x34) and row_4_bottom_border.png. The lane metrics
// (48 content + 1 border) live in render.h with rows 1-3's.
//
// THE VERTICAL STORY IS PURE CENTERING (and it is what resolves the architect's
// 48-vs-6+34+6 discrepancy, recorded at kIconRowHeightPx): the 32px buttons
// land at +8 and the 34px separators at +7, each centered in the 48px content
// band by its own arithmetic rather than by a stated margin.
//
// THE HORIZONTAL WALK uses TWO different gaps, which is this row's own rule and
// not row 2's: 2px between ADJACENT buttons, and 4px on each side of a
// SEPARATOR (row 2 used 5). The row opens with 8px of padding.
constexpr double kIconRowPadLeftPx    = 8.0;
constexpr double kIconBtnPx           = 32.0;   // the button box, both axes
constexpr double kIconBtnGapPx        = 2.0;    // between adjacent buttons
constexpr double kIconSepGapPx        = 4.0;    // each side of a separator
constexpr double kIconSepWidthPx      = 1.0;
constexpr double kIconSepHeightPx     = 34.0;
constexpr double kIconGlyphPx         = 22.0;   // the icon box inside the button
constexpr double kIconOutlineStrokePx = 1.0;
// THE CORNER RADIUS IS 5 — MEASURED, and it lands in rows 1-3's family after
// all. Fitting rendered corners against BOTH the hover crop (stroke only) and
// the selected crop (fill under stroke) over radii 3.0..5.0 minimises squared
// per-channel error at a PATH radius of 4.5 in each — hover 2382 against
// r=4.0's 25347 and r=5.0's 8099, selected 227 against 2685 and 684 — and 4.5
// is what the authored 5 becomes once the half-stroke inset below is applied.
// (An earlier read of "4" came from fitting the PATH radius directly and
// forgetting that inset; the authored constant is the thing to state.)
constexpr double kIconCornerRadiusPx  = 5.0;

// What precedes an icon-row button in the layout walk.
enum class IconRowLead {
    First,       // the row's left padding already placed it
    Gap,         // 2px, between adjacent buttons
    Separator,   // 4px, the 1px line, 4px
};

// THE PAINTER'S HALF OF THE ICON-ROW ROSTER: each button's id, what leads it,
// and its content — a LETTER GLYPH (shaped sans, centered on both axes) or a
// 22px breeze ICON. Exactly one of the two is used: a non-null `glyph` selects
// the letter and `icon` is then never read. The press claim's chord table
// (input_pointer.cpp) is the other half; both key off the same ids.
struct IconRowDef {
    RedesignButton id;
    IconRowLead    lead;
    const char*    glyph;   // nullptr -> draw `icon` instead
    icons::Icon    icon;
};
constexpr IconRowDef kIconRowButtons[] = {
    {RedesignButton::IconS,      IconRowLead::First,     "S", icons::Icon::EditCopy},
    {RedesignButton::IconT,      IconRowLead::Gap,       "T", icons::Icon::EditCopy},
    {RedesignButton::IconW,      IconRowLead::Separator, "W", icons::Icon::EditCopy},
    {RedesignButton::IconP,      IconRowLead::Gap,       "P", icons::Icon::EditCopy},
    // (THE ZOOM PAIR SAT HERE, in its own separator-flanked group, from
    // 2026-08-01 until 2026-08-02: the architect ruled out duplicate commands on
    // the GUI when the Navigation dropdown gave `-` and `=` a home there, so the
    // two buttons and their icons were deleted whole, taking the row back to
    // eleven buttons in four groups; the keys are untouched. The twelfth and
    // fifth arrived 2026-08-04 at the row's other end — the history button —
    // and its group took the thirteenth and fourteenth 2026-08-05, the walk's
    // older / newer steps, leaving the row at FOURTEEN buttons in five groups.)
    {RedesignButton::IconCopy,   IconRowLead::Separator, nullptr, icons::Icon::EditCopy},
    {RedesignButton::IconPaste,  IconRowLead::Gap,       nullptr, icons::Icon::EditPaste},
    {RedesignButton::IconBpm,    IconRowLead::Gap,       nullptr, icons::Icon::MusicNote16th},
    {RedesignButton::IconIter,   IconRowLead::Gap,       nullptr, icons::Icon::BlackSum},
    // Follow's icon walked twice: the provisional "F" letter, then
    // media-seek-forward (2026-07-31), then go-jump (2026-08-01) — the architect
    // settling on the chevron-and-dot, which reads as GOING to a place rather
    // than as a transport control.
    {RedesignButton::IconFollow, IconRowLead::Gap,   nullptr, icons::Icon::GoJump},
    {RedesignButton::IconListen, IconRowLead::Separator, nullptr, icons::Icon::PreviewRenderOn},
    {RedesignButton::IconLoadInPlace,
     IconRowLead::Gap, nullptr, icons::Icon::DialogOkApply},
    // THE HISTORY MODE'S BUTTON (2026-08-04) — the row's twelfth, and a GROUP OF
    // ITS OWN: the architect asked for "a separation there and then another
    // button", and this row's vocabulary for a group boundary is exactly one
    // thing, IconRowLead::Separator (4px, the 1px line, 4px). It reads the same
    // way the four existing boundaries do rather than inventing a second kind of
    // gap that would have to be explained.
    {RedesignButton::IconHistory, IconRowLead::Separator, nullptr, icons::Icon::VcsDiff},
    // THE WALK'S TWO STEPS (2026-08-05) — older, then newer, JOINING the history
    // button's group rather than opening a sixth: they are the same mode's
    // controls, and the row's one group boundary already said where that mode
    // starts. So they take the ordinary 2px Gap, and the group reads History |
    // Older | Newer left to right, the arrows pointing the way each one walks.
    // Being LAST IN THE WALK is what makes this a pure append: the layout is a
    // single left-to-right accumulation with no right float and no total-width
    // term, so no existing button's rect, separator or damage band moves by a
    // pixel.
    {RedesignButton::IconHistoryOlder,
     IconRowLead::Gap, nullptr, icons::Icon::GoPrevious},
    {RedesignButton::IconHistoryNewer,
     IconRowLead::Gap, nullptr, icons::Icon::GoNext},
};

// THE TOOLBAR'S ICON, by state — redesign_button_label's sibling (app_state.h,
// which owns the label and the tooltip halves of the same fact and the reasoning
// for both). It lives HERE because the constant icons do: kToolbarButtons is the
// painter's roster half, and app_state.h carries no icon vocabulary at all.
// Exactly one button overrides its own: Render, which is the COMMIT ACT while
// the history mode stands and wears the commit icon to say so.
icons::Icon redesign_button_icon(const AppState& app, RedesignButton b,
                                 icons::Icon table_icon) {
    if (b == RedesignButton::Render && app.history_mode.active) {
        return icons::Icon::VcsCommit;
    }
    return table_icon;
}

// -- THE FLOATING SURFACES: the hover tooltip and the menu row's dropdowns --
//
// Measured off hover_shift.png (129x41, the two-line form we ship) and
// hover_plain.png (112x26, the one-line reference we do not). Both share every
// chrome pixel; the colors and the dim factor live in render.h.
//
// THE CORNER RADIUS IS THE REDESIGN'S ONE RADIUS, 5, on BOTH floating surfaces
// and on the dropdown's items — and it is measured, not assumed. Fitting each
// corner by summed squared per-channel error:
//   the tooltip's chrome  -> best at a PATH radius of 5.0 (1034), 4.5 next (1686);
//   a dropdown item HOVER -> best at a PATH radius of 5.0 (6856), 4.5 next (15059);
//   a dropdown item CLICK -> best as a FULL-BOX fill at 5.5 (1853), 5.0 next (9639).
// All three agree on an OUTER corner of ~5.5 (a stroked path at 5.0 has its
// outer edge half a line further out). The authored 5 is the nearest integer,
// half a pixel tight, and keeps ONE radius across the whole redesign rather than
// giving this surface a fractional constant of its own. The briefs' eyeball
// estimates (2-3 here, 2-3 for the tooltip) are far outside every fit.
constexpr double kPopupCornerRadiusPx = 5.0;

// THE TOOLTIP'S TYPE AND SPACING. Two sizes now: the NAME line stays at the
// redesign's 12pt, and the SHIFT line drops to 10pt (architect 2026-07-31 — the
// hint is subordinate text and should read as such). Both go through the one
// shaping chokepoint, which takes whatever size the context carries.
//
// THE VERTICAL LAYOUT IS DERIVED, NOT AUTHORED, and it is SYMMETRIC BY
// CONSTRUCTION: each line occupies its own face's (ascent + descent) band, the
// two bands are separated by a real gap, and the SAME padding closes the box
// above the first band and below the last. So the box height falls out as
//     pad + band1 [+ gap + band2] + pad
// and the top and bottom air are equal by the arithmetic rather than by a
// measured pair that could drift. At 100% on the target face that is 6 + 19 + 6
// = 31 for one line and 6 + 19 + 4 + 16 + 6 = 51 for two — replacing the old
// 41px two-line box, which the architect read as too tight between the lines and
// bottom-heavy. render.h carries only a BOUND on this for the damage band.
constexpr double kTooltipShiftFontSizePt = 10.0;
constexpr double kTooltipPadYPx          = 6.0;   // top AND bottom, equal
constexpr double kTooltipLineGapPx       = 4.0;   // between the two bands
constexpr double kTooltipPadXPx      = 5.0;
// (The damage BOUND on the height and the hover dwell live in render.h — the
// run loop reads both, for the due-check and for the band under the strip.)

// THE TOOLTIP'S TEXT lives with the roster, not here
// (redesign_button_tooltip, app_state.h, owns both the membership and the text;
// its shift line is static_asserted against redesign_button_shift_admits, so the
// hint cannot appear where a shift press does nothing).
//
// EVERY BUTTON BUT ROW 1'S HAS ONE (architect 2026-07-31, stated as the ROW's
// property at the table): the one-line form is the whole story for most, and the
// two shift-admitting buttons add the hint line below it — Render only while
// iteration mode is OFF, where its shift press has a twin to reach.
// (The TEXT and its membership live at redesign_button_tooltip, app_state.h —
// beside the roster, because the pointer side reads the same table.)

// THE DROPDOWN, in the architect's CSS terms. The item height is the ONE-LINE
// TOOLTIP'S INTERIOR (26 total less its two 1px borders = 24), which is what
// "pick a one-line item height from the tooltip's single-line anatomy" resolves
// to and keeps the two floating surfaces built from one set of numbers.
//
// The width derives rather than being authored: the widest shaped label, plus
// the redesign's standing 10px label padding per side (rows 1 and 3's), plus the
// 3px item inset per side, plus the two 1px borders. The architect pixel-tweaks
// at 100% by moving these terms.
// (The item height, its block margin, the separator's vertical margin and the
// border live in render.h with dropdown_h_px's other ingredients — the
// popup's OPEN EDGE must size the box before it is painted. Only the HORIZONTAL
// terms, which depend on the widest shaped label, are the painter's alone.)
// (BOTH MENUS SHARE EVERY NUMBER IN THIS BLOCK — chrome, item height, insets,
// separator, faces — and since 2026-08-03 the horizontal terms below as well.)
constexpr double kPopupItemInsetPx   = 3.0;   // the highlight box, per side
constexpr double kPopupSepInsetPx    = 7.0;   // the separator, per side

// THE MINIMUM ITEM WIDTH — the tab-min-width pattern, and the reason a menu of
// short labels still reads as a menu. THE VALUE IS AUTHORED, NOT DERIVED: 242
// is the architect's knob turned at the live look (2026-08-03, a flat +42px on
// the number below), which is what this constant has always been for. Nothing
// re-derives it and nothing should try.
//
// ITS PREDECESSOR WAS a derivation, and the distinction is the point: 200 came
// off the crop, whose items are 401px for labels of ~150px ink with roughly
// half that width going to an accelerator and submenu column — taking the text
// half alone and scaling to our label set (widest "Playback speed" at 113px)
// landed there. The measurement still stands; it is simply no longer what sets
// the floor.
//
// WHICH MENU IT BINDS ON, measured: the SETTINGS menu, whose content asks for
// 57 + 113 + 30 − 8 = 192 at 100% (384 at 200%), so the floor answers at both
// scales — and the whole +42 therefore lands on that popup's width, 208 -> 250
// at 100% and 416 -> 500 at 200%. The NAVIGATION menu does not move at all: its
// accelerator column puts content at 57 + 117 ("Previous marker") + 13 + 101
// ("Ctrl+Shift+Tab") + 30 − 8 = 310, already past both floors, so its box stays
// 318px wide.
//
// What the 2026-08-03 harmonization did to the SETTINGS box is separate and
// still true: the labels now start on the shared 57px indent rather than a 12px
// item pad, so the generous-right-side reading the original derivation leaned on
// is retired. The floor survives as what it does now — a width the shortest
// menus cannot fall below.
constexpr double kPopupItemMinWidthPx = 242.0;

// THE DROPDOWN'S HORIZONTAL TERMS, measured off the two-column crop
// (dropdown_full_hotkeys.png, 403x579, the popup box including its 1px borders)
// and authored in POPUP-BOX coordinates, which is how the crop reads: a menu
// with an accelerator column is easier to state — and to check against the crop
// — from the box's own edges than from the item box's.
//
// BOTH MENUS TAKE THEM (architect 2026-08-03): the settings menu's own 12px
// item-box label pad is retired and its labels now start on this same indent,
// so the two menus differ in one derived term (whether an accelerator column
// exists) rather than in their padding.
//
//  - THE LABEL INDENT is 57px from the popup's left edge to the label's pen
//    origin (ink starts at 57 or 58 depending on the glyph — "Switch Monitor"
//    and "Go To" at 57, "Focus Timecode" and "Monitor Config" at 58 — and the
//    same 57 holds on the rows that carry an icon at 39, so the column is real
//    and not a per-row accident). kdenlive reserves a CHECKBOX plus an ICON
//    gutter in there; we have neither and reproduce the RESULTING INDENT as
//    plain padding, by ruling.
//  - THE COLUMN GAP is the crop's GUARANTEED minimum separation: the widest
//    label ink ends at x=239 ("Switch Monitor Fullscreen") and the leftmost
//    accelerator ink starts at x=252 ("Ctrl+Shift+Space"), so 13px is what the
//    design promises when both columns are at their widest. (Per-row gaps are
//    all larger — 100px on the widest label's own row — because those two rows
//    are different rows; 13 is the number the min-width rule is built on.)
//  - THE RIGHT MARGIN is 30px from the popup's right edge to the accelerator's
//    last ink column, uniform across every hotkey row (all end at x=372). In
//    the source that margin also holds the submenu-arrow column (the arrows run
//    out to 388); we have no submenus and keep the margin, which is what makes
//    the accelerators sit off the edge rather than against it.
// The three reproduce the crop's own width to a pixel: 57 + 183 (widest label
// ink) + 13 + 121 (widest hotkey ink) + 30 = 404 against the measured 403.
// (They lost their `Nav` prefix with the harmonization — the indent and the
// margin are now every menu's, and the gap is a term of the ONE width rule
// below, present exactly when the optional accelerator column is.)
constexpr double kPopupLabelIndentPx  = 57.0;
constexpr double kPopupHotkeyGapPx    = 13.0;
constexpr double kPopupPadRightPx     = 30.0;

// The selected tab's outline: a rectangle with ROUNDED TOP corners, and — this
// is the load-bearing part — NO BOTTOM EDGE. It is an OPEN path running up the
// left side, over the two arcs and the top, and back down the right side, which
// is the whole shape of a Breeze tab: a tab is open at the bottom, into the
// content it selects.
//
// The openness matters ONLY to the STROKE (the 1px side borders), which is
// exactly where it must: a closed path would lay a line across the tab's foot
// and wall off the opening the whole design is about. cairo_fill closes any
// open subpath implicitly, so the same helper still fills correctly for the
// trim band above.
void redesign_rounded_top_rect_path(cairo_t* cr, double x, double y,
                                    double w, double h, double r) {
    constexpr double kPi = 3.14159265358979323846;
    if (r > w * 0.5) r = w * 0.5;
    if (r > h)       r = h;
    if (r <= 0.0) {
        // Degenerate (a scale so small the corners vanish): the same open
        // three-sided walk, so the stroke keeps its no-bottom-edge property.
        cairo_new_sub_path(cr);
        cairo_move_to(cr, x, y + h);
        cairo_line_to(cr, x, y);
        cairo_line_to(cr, x + w, y);
        cairo_line_to(cr, x + w, y + h);
        return;
    }
    // Clockwise from the bottom-left corner, so both arcs run in cairo's
    // increasing-angle direction and neither needs the negative variant.
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x, y + h);
    cairo_line_to(cr, x, y + r);
    cairo_arc(cr, x + r,     y + r, r, kPi,       1.5 * kPi);  // top-left
    cairo_line_to(cr, x + w - r, y);
    cairo_arc(cr, x + w - r, y + r, r, 1.5 * kPi, 2.0 * kPi);  // top-right
    cairo_line_to(cr, x + w, y + h);
}

// The baseline for a label vertically centered in `box`: SOLVED from the face's
// own extents, never a measured literal — center the (ascent + descent) band in
// the box and put the baseline at its foot, then round to the pixel grid so the
// glyphs stay crisp at every scale. Row 1 centers in the ROW (a flush button),
// row 2 in the 32-tall BUTTON box; one formula, two boxes.
double redesign_baseline(cairo_scaled_font_t* font, double box_y,
                         double box_h) {
    cairo_font_extents_t fe;
    cairo_scaled_font_extents(font, &fe);
    return std::nearbyint(box_y + (box_h + fe.ascent - fe.descent) * 0.5);
}

} // namespace

void GuiPaintHandler::paint_menu_row(cairo_t* cr) {
    // THE MENU ROW (top lane 0, at the window edge): a flat kdenlive-sampled
    // ground carrying TWO FLOATS — the LEFT one, "Quit", "Navigation" and
    // "Settings", and the RIGHT one, the view bar's S+W / T+P / T+W (both
    // 2026-08-02). No ring; the kdenlive bar is flat.
    //
    // THE LEFT FLOAT'S HOVER MODEL IS KDENLIVE'S, and it is TWO faces for ALL
    // THREE buttons — plus ONE mode-scoped third, the history view's disabled
    // face on the two anchors (below, at the pill):
    // at rest the label paints bare on the row ground;
    // hovered, a filled blue pill sits under it, FLUSH with the row's CONTENT
    // height (the css float model — a flat button fills its whole row, architect
    // 2026-07-31). A PRESS PAINTS NOTHING NEW — a click keeps the hover face and
    // only pointer-out rests it. The click and disabled faces belong to rows 2
    // and 4, so these two have no press-state machinery at all.
    //
    // THEIR ACTIONS DIFFER IN KIND: Quit is its chord (Ctrl+Q, dispatched
    // through the shared chord table like every other redesigned button), while
    // SETTINGS and NAVIGATION each TOGGLE A DROPDOWN — the roster's two
    // non-chord actions, since no keyboard chord opens or closes a popup. Both
    // menus lead only where the keyboard already goes: the bare `;` key still
    // opens the settings editor directly, and every navigation item is a key.
    //
    // THE RIGHT FLOAT IS A DIFFERENT SURFACE ON THE SAME ROW: its own background
    // div, five faces from its own crops, and three chord buttons that are bare
    // 1/2/3. Its layout, its box model and its face rule are at kViewBarButtons
    // and view_bar_face above; its colors at kRedesignViewBarBg (render.h).
    const GuiRect row = top_menu_row_area(app);
    if (row.w <= 0 || row.h <= 0) return;

    // THE LANE IS CONTENT + MARGIN-BOTTOM (render.h's menu_row_* trio). Both
    // floats fill the CONTENT band; the margin strip below it is left to the row
    // ground, which is exactly what a css margin shows. Under the left buttons
    // that is indistinguishable from the ground above; under the RIGHT float it
    // is the whole point, holding the bar's blue off row 2.
    const int content_h = row.h - menu_row_margin_h_px();
    if (content_h <= 0) return;

    cairo_save(cr);

    // The ground covers the WHOLE LANE, margin included — one fill, and the
    // margin needs no second source.
    const GuiColor ground = redesign_row_ground(app);
    cairo_set_source_rgb(cr, ground.r, ground.g, ground.b);
    cairo_rectangle(cr, row.x, row.y, row.w, row.h);
    cairo_fill(cr);

    // THE SHAPING CHOKEPOINT (text_shape.h): each label is MEASURED and PAINTED
    // from the one ShapedRun, so a button's width and its glyphs come from the
    // same positions and cannot disagree. Shaping a handful of glyphs per paint
    // is deliberate — the chokepoint's own comment defers caching to a profile,
    // and these are the cheapest runs there are.
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int pad    = scaled_px(kMenuLabelPadPx);
    const double rad = std::nearbyint(kMenuPillRadiusPx * gui_scale_factor());

    // THE WALK: flush from the row's left edge, ADJACENT WITH NO GAP. Row 2
    // inserts a 2px invisible separator between its adjacent buttons because the
    // architect stated one there; none is stated here, so none exists (the css
    // float model's default).
    int x = row.x;
    for (const MenuButtonDef& def : kMenuButtons) {
        const text_shape::ShapedRun run =
            text_shape::shape_text_run(font, def.label);
        const int btn_w =
            static_cast<int>(std::nearbyint(run.width_px)) + 2 * pad;

        // THE PAINTER PUBLISHES THE HIT RECT (the displayed-basis doctrine): the
        // width above exists only here, so the pointer code reads this stash
        // rather than re-shaping the string. Written every paint — a font, scale
        // or window change lands in it on the frame that displays it.
        // Neither menu button has a selected face, and both are live during a
        // load and on a blank state — which is the whole reason this row paints
        // outside the audio branches. (The one state that DOES dead them is the
        // history view, which cannot be entered from either.) The stash is
        // written anyway
        // (through the one publisher) so the tick comparator's vector is total
        // over the roster with no membership test.
        AppState::RedesignButtonFace& face = publish_button_face(
            app, audio.total_frames(), def.id,
            GuiRect{x, row.y, btn_w, content_h});

        // A MENU BUTTON STAYS LIT WHILE ITS DROPDOWN IS UP (architect
        // 2026-08-02, kdenlive's own behaviour): the pill is what says "this menu
        // is the one that is open", so it paints on the popup's own anchor as
        // well as on hover — Settings or Navigation, whichever emitted the open
        // popup, through the one anchor owner. It is also what keeps the button
        // from going dark the instant the menu appears — the open edge
        // deliberately UNHOVERS the whole roster (the pointer belongs to the
        // popup while it is up), so hover alone would drop the pill on exactly
        // the frame the dropdown arrives.
        //
        // A PAINT CONDITION, NOT A `selected` BIT. Two reasons, both structural:
        // redesign_button_selected is defined as the live fact a button's CHORD
        // flips, and neither menu button has a chord at all (they are the
        // roster's two non-chord actions) — a dropdown is not a resting mode. And
        // the tick comparator that watches the selected bits would be pure
        // duplication here: the popup's two writers, toggle_dropdown and the one
        // close owner close_dropdown, ALREADY invalidate the top strip on both
        // edges, so the comparator could only ever re-notice a change that was
        // damaged at its source.
        //
        // THE TWO TERMS ARE NEVER BOTH TRUE, and that is the hover predicate's
        // doing rather than this expression's: no roster button hovers while a
        // dropdown is open (redesign_button_hoverable), so the anchor term is the
        // ONLY thing lighting the open menu's button, and the hover term is what
        // lights every row-1 button the rest of the time — including the anchor
        // itself once its menu is down. They are written as a disjunction because
        // row 1 has exactly two faces and both ask for the SAME pill: were they to
        // coincide, nothing would need to win.
        // THE ROW'S THIRD FACE, AND IT EXISTS ONLY IN THE `h` HISTORY VIEW
        // (architect 2026-08-04): while that view stands the two MENU ANCHORS
        // are dead — toggle_dropdown refuses every open — so Settings and
        // Navigation wear the disabled face and Quit does not. Built from the
        // row's own vocabulary rather than a new sample: row 1's whole ink is
        // its label, so the label retains kRedesignDisabledMix of itself over
        // the row ground, exactly as row 2's icon+label pair does. The partition
        // and its derivation are at history_mode_disables_button
        // (input_pointer.cpp); this reads only the published bit.
        //
        // NO PILL ON A DEAD ANCHOR, and the term is gated here rather than
        // trusted to be impossible: both of its inputs are structurally false in
        // the view (no roster button hovers under a disabled bit, and no popup
        // can be open at all), but a button can go dead UNDER a resting hover
        // with no pointer event to refresh it — row 2's outline carries the same
        // guard for the same frame.
        const double keep = face.enabled ? 1.0 : kRedesignDisabledMix;
        const bool pill = face.enabled &&
                          (face.hovered ||
                           (app.dropdown.open() &&
                            def.id == dropdown_anchor_button(app.dropdown.menu)));
        if (pill) {
            cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                 kRedesignAccent.b);
            redesign_rounded_rect_path(cr, x, row.y,
                                       static_cast<double>(btn_w),
                                       static_cast<double>(content_h), rad);
            cairo_fill(cr);
        }

        // The label color is the SAME in the row's two live faces; the pill
        // under it is the whole hover cue. Dead, it is the one thing that dims
        // (keep above) — mixed toward the ROW GROUND, which is what is under it
        // there, since a dead button never wears the pill.
        const GuiColor label_c = mix_color(kRedesignLabel, ground, keep);
        cairo_set_source_rgb(cr, label_c.r, label_c.g, label_c.b);
        text_shape::show_shaped_run(
            cr, run, static_cast<double>(x + pad),
            redesign_baseline(font, static_cast<double>(row.y),
                              static_cast<double>(content_h)));

        x += btn_w;
    }

    // -- THE RIGHT FLOAT: the view bar ---------------------------------------
    //
    // The div is placed from its RIGHT edge, so all three widths are needed
    // before the first button can be drawn: the labels are shaped once here and
    // both measured and painted from those runs (the shaping chokepoint's rule —
    // one run is the single width truth), and the walk below re-uses them.
    {
        const int mar  = std::max(1, scaled_px(kViewBarBtnMarginPx));
        const int bord = std::max(1, scaled_px(kViewBarBtnBorderPx));
        const int bpad = scaled_px(kViewBarBtnPadPx);
        const int btn_y = row.y + mar;
        const int btn_h = content_h - 2 * mar;

        text_shape::ShapedRun runs[kViewBarButtonCount];
        int widths[kViewBarButtonCount];
        int div_w = 0;
        for (int i = 0; i < kViewBarButtonCount; ++i) {
            runs[i] = text_shape::shape_text_run(font, kViewBarButtons[i].label);
            widths[i] = 2 * (bord + bpad) +
                        static_cast<int>(std::nearbyint(runs[i].width_px));
            // Each button's own left and right margin — they do not collapse, so
            // the pair between two neighbours sums to 2px and the div's outer
            // pair is its 1px inset on each side. One term, applied per button,
            // spells the whole width.
            div_w += widths[i] + 2 * mar;
        }

        // FLUSH AT THE WINDOW'S RIGHT EDGE (no margin stated, so none exists).
        //
        // THERE IS STILL NO COLLISION RULE, and the measurement that used to
        // justify one has changed — recorded rather than acted on, since an
        // overlap layout is the architect's to specify. Re-measured with the
        // NAVIGATION button (2026-08-02, shaped-run walk at both scales): the
        // left float is 223px at 100% (shaped labels Quit 29 + Navigation 76 +
        // Settings 58, each plus its two 10px pads — 49 + 96 + 78; the ORDER
        // moved 2026-08-03 and the sum did not, which is the whole content of
        // that change) and the div 183 — 406 of the 1920px
        // deployment width, 1514px of slack, and 310 of the 640px floor as
        // before. At 200% they are 446 and 366: still 1108px of slack at 1920,
        // but 812 against a 640px floor THAT DOES NOT SCALE — so at that one
        // corner (the schema's ceiling scale on the defensive minimum window)
        // the floats now OVERLAP by 172px, where the pre-Navigation pair cleared
        // it by 19. What happens there is what the painters already do: the div
        // fills its background last and covers the tail of the left float's
        // labels. Nothing else changes, nothing is clickable that was not, and
        // the deployment geometry is nowhere near it.
        // Defensive only, and it takes the whole float: a lane so short that the
        // two margins eat the button leaves nothing to paint and nothing to
        // click, which is the same early-out the row's own content_h guard above
        // makes. Unreachable at any schema-legal gui_scale (content_h floors at
        // 5 and the margins at 1 each).
        if (div_w <= 0 || btn_h <= 0) { cairo_restore(cr); return; }

        const int div_x = row.x + row.w - div_w;
        const GuiColor bar_bg = app.window_activated
                                    ? kRedesignViewBarBg
                                    : kRedesignViewBarBgUnfocused;
        cairo_set_source_rgb(cr, bar_bg.r, bar_bg.g, bar_bg.b);
        cairo_rectangle(cr, div_x, row.y, div_w, content_h);
        cairo_fill(cr);

        const double bar_rad = std::nearbyint(kViewBarRadiusPx *
                                              gui_scale_factor());
        int vx = div_x;
        for (int i = 0; i < kViewBarButtonCount; ++i) {
            vx += mar;
            const int btn_w = widths[i];

            AppState::RedesignButtonFace& face = publish_button_face(
                app, audio.total_frames(), kViewBarButtons[i].id,
                GuiRect{vx, btn_y, btn_w, btn_h});

            const bool pressed =
                app.redesign_pressed ==
                redesign_button_index(kViewBarButtons[i].id);
            const ViewBarFace f =
                view_bar_face(bar_bg, app.window_activated, face.hovered,
                              face.selected, pressed);

            if (f.filled || f.framed) {
                // The shared face box (redesign_face_box — the half-stroke
                // inset rule lives there). The REST face paints no fill at all:
                // its color IS the div's background, already under it, which is
                // why the crop's resting button is invisible.
                redesign_face_box(cr, vx, btn_y, btn_w, btn_h, bord, bar_rad,
                                  f.filled ? &f.fill  : nullptr,
                                  f.framed ? &f.frame : nullptr);
            }

            // The label is kRedesignLabel in EVERY state, focused and unfocused
            // — the nine crops agree — and sits at the box's own padding origin,
            // which is the same place the width was measured from.
            cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                                 kRedesignLabel.b);
            text_shape::show_shaped_run(
                cr, runs[i], static_cast<double>(vx + bord + bpad),
                redesign_baseline(font, static_cast<double>(btn_y),
                                  static_cast<double>(btn_h)));

            vx += btn_w + mar;
        }
    }

    cairo_restore(cr);
}

void GuiPaintHandler::paint_toolbar_row(cairo_t* cr) {
    // THE TOOLBAR ROW (top lane 1, row 2 of the redesign): the same flat
    // kdenlive-sampled ground as the menu row, a 1px border-bottom across the
    // WHOLE window width, two vertical separators, and four icon+label buttons
    // — Save, [separator], Undo, Redo, Render — each firing its chord's exact
    // route on press (the claim and the chord table are in input_pointer.cpp).
    //
    // ROW 2 HAS FOUR FACES, the most of any redesigned row (architect
    // 2026-07-31), and every one of them is decided here:
    //   REST     — the icon and label on the bare row ground.
    //   HOVER    — a 1px accent OUTLINE around the button's exact box, interior
    //              untouched (row 1's model with a different shape).
    //   CLICK    — the outline unchanged, the interior filled with the row
    //              ground tinted kRedesignClickMix toward the accent,
    //              shown for as long as the physical button is held. The action
    //              already fired at the press; this face is purely visual, and
    //              it is ROW 2'S ALONE (rows 1 and 3 keep two faces each).
    //   DISABLED — the icon paths and the label each retaining
    //              kRedesignDisabledMix of themselves over the ground, with NO
    //              hover outline and NO click face. The predicate is
    //              redesign_button_enabled (app_state.h), which mirrors each
    //              chord's own refusals; the press claim reads the SAME
    //              predicate and dispatches nothing for a disabled button, so
    //              the face and the behavior cannot disagree.
    const GuiRect lane = top_toolbar_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    const int border_h  = toolbar_border_h_px();
    const int content_h = lane.h - border_h;
    if (content_h <= 0) return;

    cairo_save(cr);

    // Ground over the CONTENT band only (the border sits outside it, css-style).
    // THE ONE GROUND READ, shared with both mixes below so the faces and the
    // surface they sit on can never disagree about which state the window is in.
    const GuiColor ground = redesign_row_ground(app);
    cairo_set_source_rgb(cr, ground.r, ground.g, ground.b);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, content_h);
    cairo_fill(cr);

    // THE BORDER-BOTTOM AND THE SEPARATORS ARE PIXEL-BOUND RECTANGLE FILLS, not
    // strokes: every edge here is axis-aligned on integer bounds, so a fill is
    // crisp by construction and needs no +0.5 alignment (the standing rule for
    // axis-aligned 1px work — the alternative it offers, a stroked line at the
    // half-pixel, is what the hover outline below takes, because that shape has
    // round corners and must be stroked).
    cairo_set_source_rgb(cr, kRedesignLine.r, kRedesignLine.g, kRedesignLine.b);
    cairo_rectangle(cr, lane.x, lane.y + content_h, lane.w, border_h);
    cairo_fill(cr);

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int sep_margin  = scaled_px(kToolbarSepMarginPx);
    const int sep_w       = scaled_px(kToolbarSepWidthPx);
    const int sep_h       = scaled_px(kToolbarSepHeightPx);
    const int btn_margin_y = scaled_px(kToolbarBtnMarginYPx);
    const int btn_gap     = scaled_px(kToolbarBtnGapPx);
    const int pad_left    = scaled_px(kToolbarBtnPadLeftPx);
    const int icon_px     = scaled_px(kToolbarIconPx);
    const int icon_gap    = scaled_px(kToolbarIconGapPx);
    const int pad_right   = scaled_px(kToolbarBtnPadRightPx);
    const int btn_y       = lane.y + btn_margin_y;
    const int btn_h       = content_h - 2 * btn_margin_y;

    // THE LAYOUT WALK, left to right in css float order: the row's left padding
    // opens it, then each button's LEAD (a full-margin separator, or the 2px
    // invisible separator between adjacent buttons) and the button itself. Every
    // future row inherits the rule by walking this way — a button never carries
    // a horizontal margin of its own.
    int x = lane.x + scaled_px(kToolbarRowPadLeftPx);
    for (const ToolbarButtonDef& def : kToolbarButtons) {
        if (def.lead == ToolbarLead::Separator) {
            x += sep_margin;
            cairo_set_source_rgb(cr, kRedesignLine.r, kRedesignLine.g,
                                 kRedesignLine.b);
            cairo_rectangle(cr, x, lane.y + sep_margin, sep_w, sep_h);
            cairo_fill(cr);
            x += sep_w + sep_margin;
        } else {
            x += btn_gap;
        }

        // The label is shaped ONCE per button and both measured and painted from
        // that run — the button's width exists nowhere else, which is exactly why
        // the painter publishes the hit rect. So a label that CHANGES WITH STATE
        // (Render's, in iteration mode — redesign_button_label owns which and
        // why) simply shapes wider and takes the width it needs: the walk below
        // is a css float walk with no authored button width anywhere in it, and
        // every button to this one's right is placed from the running pen. There
        // is nothing here to keep in step with the text.
        const text_shape::ShapedRun run = text_shape::shape_text_run(
            font, redesign_button_label(app, def.id, def.label));
        const int label_w = static_cast<int>(std::nearbyint(run.width_px));
        const int btn_w = pad_left + icon_px + icon_gap + label_w + pad_right;

        // THE ENABLED VECTOR IS STASHED AS IT IS PAINTED, through the one
        // publisher: main.cpp's per-tick comparator reads it back to notice
        // that the live answer has drifted (an undo push, a read-only toggle, a
        // load completing — none of which damages the strip on its own) and
        // pays one invalidate_top_strip to bring the faces up to date.
        AppState::RedesignButtonFace& face = publish_button_face(
            app, audio.total_frames(), def.id,
            GuiRect{x, btn_y, btn_w, btn_h});
        const bool enabled = face.enabled;

        // The click face rides the PHYSICAL hold, so it survives the pointer
        // wandering off the button mid-press; a disabled button never gets one
        // because the press claim never records it.
        const bool pressed =
            enabled &&
            app.redesign_pressed == redesign_button_index(def.id);
        // Hover cannot rest on a disabled button (the recompute refuses to set
        // it), but a button can go disabled UNDER a resting hover with no
        // pointer event, so the face is gated here too rather than trusting the
        // pointer state to be fresh.
        const bool outlined = enabled && (face.hovered || pressed);

        if (pressed && btn_w > 0 && btn_h > 0) {
            // The click INTERIOR, painted before the outline so the 1px accent
            // ring is the outermost thing on the box exactly as in the hover
            // face. Square fill on integer bounds under a rounded outline: the
            // crop shows the fill running to the outline's inner edge with the
            // corners covered by the ring's own AA.
            const GuiColor click =
                mix_color(kRedesignAccent, ground, kRedesignClickMix);
            cairo_set_source_rgb(cr, click.r, click.g, click.b);
            cairo_rectangle(cr, x, btn_y, btn_w, btn_h);
            cairo_fill(cr);
        }

        if (outlined && btn_h > 0) {
            // The crop's straight edges are pure accent with AA only at the
            // corners, so the outline is INSET BY HALF ITS OWN WIDTH (the
            // half-stroke inset rule — full statement at redesign_face_box).
            // DELIBERATELY NOT the shared face box: this stroke passes its
            // radius UN-inset where the owner insets it by the same half, and
            // these painted pixels are the shipped ones — the recorded keeper
            // at the owner's comment.
            const int    lw   = std::max(1, scaled_px(kToolbarHoverStrokePx));
            const double half = static_cast<double>(lw) * 0.5;
            cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                 kRedesignAccent.b);
            cairo_set_line_width(cr, static_cast<double>(lw));
            redesign_rounded_rect_path(
                cr, x + half, btn_y + half,
                static_cast<double>(btn_w - lw),
                static_cast<double>(btn_h - lw),
                std::nearbyint(kToolbarHoverRadiusPx * gui_scale_factor()));
            cairo_stroke(cr);
        }

        // ONE MIX FACTOR FOR BOTH INKS: the icon paths and the label each retain
        // the same fraction of themselves over the row ground, so a disabled
        // button dims as one object. Enabled, the factor is 1 and both paint the
        // rest face's own colors — exactly on the pixel, the ULP caveat at
        // mix_color's declaration (render.h) applying here as everywhere.
        const double keep = enabled ? 1.0 : kRedesignDisabledMix;

        // The icon fills its own square, vertically centered in the button box,
        // in ITS OWN color (the icon table owns that — media-record is red where
        // the other three are the label white).
        icons::draw(cr, redesign_button_icon(app, def.id, def.icon),
                    static_cast<double>(x + pad_left),
                    static_cast<double>(btn_y + (btn_h - icon_px) / 2),
                    static_cast<double>(icon_px), keep, ground);

        const GuiColor label_c =
            mix_color(kRedesignLabel, ground, keep);
        cairo_set_source_rgb(cr, label_c.r, label_c.g, label_c.b);
        text_shape::show_shaped_run(
            cr, run, static_cast<double>(x + pad_left + icon_px + icon_gap),
            redesign_baseline(font, static_cast<double>(btn_y),
                              static_cast<double>(btn_h)));

        x += btn_w;
    }

    cairo_restore(cr);
}

void GuiPaintHandler::paint_tab_row(cairo_t* cr) {
    // THE TAB ROW (top lane 2, row 3 of the redesign): the Breeze tab bar for
    // the A/B navigational tabs — "Tab A" and "Tab B", flush at the left edge,
    // over a #202326 ground, with a 1px border-bottom across the whole window
    // width that BREAKS under the selected tab.
    //
    // THE SELECTED TAB IS app.active_tab_view, read live every paint. Its face
    // is a 3px accent trim with rounded top corners over an interior that is the
    // row ground itself — so it reads as an opening rather than as a filled
    // shape — flanked by 1px side borders. The inactive tab is a flat fill, rest
    // or hover; there is no selected-hover face and no click face anywhere in
    // this row (a tab press is a chord, never a refusal), and this row has NO
    // disabled face at all.
    //
    // THE ROW IS THE COMPARE SELECTOR WHILE THE `h` HISTORY VIEW STANDS
    // (architect 2026-08-05), which is a REPURPOSING of the surface, not a state
    // of the tabs: the labels read "Iterative" and "Cumulative", the selected
    // face marks the live reading (redesign_button_selected's own history arm),
    // a press on the other switches (the tab row's band claim, input_pointer.cpp)
    // and THE LOCK SLOTS ARE GONE WHOLE — no padlock drawn, no rect published,
    // no width reserved, because a lock is TAB state and these are not tabs. It
    // is why the row's own former disabled face (the mode's, 2026-08-04) is
    // retired with this arc: the tabs are live in the view now, and every other
    // state of this row is the ordinary one. ONE NAME for that state, read once
    // here and consulted by the three places it changes: the width, the label
    // and the lock.
    //
    // THE PADLOCK PUBLICATION IS ZEROED FIRST, every run, so a tab that stops
    // being read-only (or stops being active) cannot strand a clickable rect
    // where nothing is drawn — the same write-it-every-run rule the floating
    // surfaces and the flag editor's box follow. In the compare view it stays
    // zero for the whole run, no lock being drawn at all.
    const bool compare_selector = app.history_mode.active;
    app.tab_lock_rect = GuiRect{0, 0, 0, 0};
    const GuiRect lane = top_tab_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    const int border_h  = tab_row_border_h_px();
    const int content_h = lane.h - border_h;
    if (content_h <= 0) return;

    cairo_save(cr);

    // Ground over the WHOLE lane, border row included. It is the content band's
    // ground, it is the selected tab's interior (the architect's ruling that the
    // two are one color), it is what the corner arcs antialias against — and
    // covering the border row too is what the BREAK below is made of: where the
    // border does not paint, this ground shows, so the selected tab's opening
    // carries the tab's own color instead of whatever render_background happened
    // to leave there.
    cairo_set_source_rgb(cr, kRedesignTabGround.r, kRedesignTabGround.g,
                         kRedesignTabGround.b);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, lane.h);
    cairo_fill(cr);

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int pad      = scaled_px(kTabLabelPadPx);
    const int min_w    = scaled_px(kTabMinWidthPx);
    const int lock_box = scaled_px(kTabLockBoxPx);
    const int lock_mar = scaled_px(kTabLockMarginPx);
    const int slot_w   = scaled_px(kTabLockSlotPx);
    const int trim_h   = std::max(1, scaled_px(kTabTrimHeightPx));
    const int line_w   = std::max(1, scaled_px(kTabBorderPx));
    const double radius = std::nearbyint(kTabCornerRadiusPx * gui_scale_factor());

    // The selected tab's span, recorded during the walk so the border below can
    // break under it. Zero width means "no tab is selected here", which cannot
    // happen with active_tab_view always 'A' or 'B' but costs one int to state.
    int sel_x = 0, sel_w = 0;

    // THE WALK: tabs flush from the lane's left edge, adjacent, margin zero. A
    // tab's width is the LARGER of its two paddings around the shaped label and
    // the minimum — and nothing else, so it is identical selected or not (the
    // side borders draw inside the box). With the A/B labels the minimum is what
    // binds, which makes both tabs exactly the same width and the row regular by
    // construction; with the compare view's two words the shaped run binds and
    // the tabs differ in width, which is what a label-sized tab bar does and the
    // reason nothing in this walk assumes they match.
    int x = lane.x;
    for (const TabDef& def : kTabs) {
        // THE LABEL IS THE OVERRIDE OWNER'S (redesign_button_label, app_state.h,
        // which also answers for the Render button's two mode labels), so the
        // table's constant and the history view's compare word are one lookup
        // and cannot drift into two spellings.
        const text_shape::ShapedRun run = text_shape::shape_text_run(
            font, redesign_button_label(app, def.id, def.label));
        const int label_w = static_cast<int>(std::nearbyint(run.width_px));
        // THE FIELD, then THE SLOT. The field is what it always was — the shaped
        // label auto-sizes it, so the compare labels widen the tabs and nothing
        // else moves. The slot is reserved on every tab in every state SO LONG AS
        // THE TABS ARE TABS, which is what keeps both identical in width and
        // makes locking one shove nothing; in the compare view there is no lock,
        // so there is no slot either and the tab is its field.
        const int field_w = std::max(min_w, label_w + 2 * pad);
        const int tab_w   = field_w + (compare_selector ? 0 : slot_w);

        // THE STASH IS WHAT THE DRIFT COMPARATOR READS (main.cpp's per-tick
        // enabled/selected sweep), so publishing `selected` is load-bearing,
        // not bookkeeping: leave it at its default and the live active-tab
        // compare disagrees with the stash on the selected tab EVERY pass,
        // which invalidates the whole top strip at tick cadence forever. The
        // one publisher writes it from redesign_button_selected — the roster
        // predicate's own active_tab_view compare, so the painted face below
        // reads THE SAME fact the comparator replays, with no second spelling.
        AppState::RedesignButtonFace& face = publish_button_face(
            app, audio.total_frames(), def.id,
            GuiRect{x, lane.y, tab_w, content_h});
        const bool selected = face.selected;

        // THE ROW HAS NO DISABLED FACE AGAIN (2026-08-05). It grew one on
        // 2026-08-04 for the `h` history view, which greyed both tabs because
        // their chord was consumed; the architect then made the view REPURPOSE
        // the pair as the compare selector, so the tabs are live in the one
        // state that ever dimmed them — derived, since Ctrl+Tab is that
        // selector's own chord and the mode claims it (history_mode_owns_key)
        // — and redesign_button_enabled answers true for them everywhere. The dim machinery went with
        // its producer rather than sitting here unreachable; the product's one
        // disabled blend is unchanged and still the rule on rows 2 and 4.
        const bool hovered = face.hovered;
        // The face this tab wears — the fill for an inactive tab, and the ground
        // every ink below sits on.
        const GuiColor tab_face =
            selected ? kRedesignTabGround
                     : (hovered ? kRedesignTabHover : kRedesignTabRest);

        if (selected) {
            sel_x = x;
            sel_w = tab_w;

            // ONE SHAPE, TWO CLIPPED USES, and the clips are complementary — so
            // no pixel is written twice and the two halves cannot describe
            // different tabs. FILLED under a clip to the top trim band it is the
            // 3px blue top, whose only antialiasing is the two corner arcs;
            // STROKED under a clip to everything below that band it is the 1px
            // side borders, picking those same arcs up where the blue leaves off
            // and running vertical to the lane's last content row.
            cairo_save(cr);
            cairo_rectangle(cr, x, lane.y, tab_w, trim_h);
            cairo_clip(cr);
            redesign_rounded_top_rect_path(cr, x, lane.y,
                                           static_cast<double>(tab_w),
                                           static_cast<double>(content_h),
                                           radius);
            cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                 kRedesignAccent.b);
            cairo_fill(cr);
            cairo_restore(cr);

            cairo_save(cr);
            cairo_rectangle(cr, x, lane.y + trim_h, tab_w, content_h - trim_h);
            cairo_clip(cr);
            {
                // THE STROKE GEOMETRY, in one expression per axis:
                //  - inset by HALF the stroke width on the left, right and top
                //    (the half-stroke inset rule — its full statement lives at
                //    redesign_face_box, whose CLOSED box this deliberately does
                //    not call: the tab's path is open at the bottom);
                //  - the radius inset by the SAME half, which keeps the arc
                //    CONCENTRIC with the filled trim's arc above (both centered
                //    on x+radius, lane.y+radius) so the border picks the blue up
                //    exactly where it ends;
                //  - and the height run to the content band's LAST ROW rather
                //    than inset, because the path has no bottom edge to align:
                //    a butt-capped vertical ending at lane.y+content_h covers
                //    every row down to the border, which is what the crop shows.
                const double half = static_cast<double>(line_w) * 0.5;
                cairo_set_line_width(cr, static_cast<double>(line_w));
                cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                                     kRedesignTabLine.b);
                redesign_rounded_top_rect_path(
                    cr, x + half, lane.y + half,
                    static_cast<double>(tab_w - line_w),
                    static_cast<double>(content_h) - half,
                    radius - half);
                cairo_stroke(cr);
            }
            cairo_restore(cr);
        } else {
            // The inactive tab: a flat fill, square corners, no borders. Hovered
            // it takes the lighter blue-grey PLUS a 1px edge across its own
            // bottom row — the hover face recolors that row, which is the crop's
            // whole difference from rest.
            cairo_set_source_rgb(cr, tab_face.r, tab_face.g, tab_face.b);
            cairo_rectangle(cr, x, lane.y, tab_w, content_h);
            cairo_fill(cr);
            if (hovered && content_h > line_w) {
                cairo_set_source_rgb(cr, kRedesignTabHoverEdge.r,
                                     kRedesignTabHoverEdge.g,
                                     kRedesignTabHoverEdge.b);
                cairo_rectangle(cr, x, lane.y + content_h - line_w,
                                tab_w, line_w);
                cairo_fill(cr);
            }
        }

        // The label is the SAME white in every state, CENTERED on both axes:
        // horizontally in the tab's FIELD (the padding is the width FLOOR's
        // term, not an anchor — at the minimum width a left-padded label would
        // hug the border instead of sitting in the middle; the field is the
        // whole tab when there is no lock slot), vertically by the shared
        // extents-solved baseline. Rounded to the pixel grid like every other
        // integer-domain conversion, so the glyphs stay crisp; the halving makes
        // a 1px bias unavoidable at odd leftovers and nearbyint's banker's
        // rounding is the project's one answer for that.
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(
            cr, run,
            static_cast<double>(x) +
                std::nearbyint((static_cast<double>(field_w) - run.width_px) *
                               0.5),
            redesign_baseline(font, static_cast<double>(lane.y),
                              static_cast<double>(content_h)));

        // THE LOCK, drawn last so it sits over whatever face the tab wears, and
        // drawn on both tabs in both lock states, in its own reserved slot (the
        // geometry and its two measured numbers are at kTabLockBoxPx) — WHENEVER
        // THE TABS ARE TABS, which is every state but the compare view below.
        //
        // TWO STATES, ONE CONTROL. LOCKED is the closed padlock at the icon
        // table's own kIconText white: full colour, because a read-only tab is
        // a state worth seeing from across the window. UNLOCKED is the OPEN
        // padlock DIMMED — the same treatment row 2's disabled icons take, a
        // per-path mix of the glyph's own colour toward the ground it sits on by
        // kRedesignDisabledMix (0.322), through the icons module's own keep_own/
        // mixed_with pair. That is the redesign's ONE dim family reused rather
        // than a new grey invented here, and mixing toward THE TAB'S CURRENT
        // FACE (selected ground, rest, or hover) is what the disabled rule
        // already says: a fraction of itself over the row's current ground.
        //
        // The dim is what makes the pair read as one control: the open lock is
        // present, legible and quiet, and locking it brightens rather than
        // conjures. It is also why the slot can be permanent without shouting.
        //
        // BOTH TABS SHOW IT; only the ACTIVE one's rect is published (the
        // contract is at AppState::tab_lock_rect) — the click is bare `o`, which
        // is defined on the active tab alone.
        //
        // THE SLOT IS GONE WHOLE IN THE `h` COMPARE VIEW (architect 2026-08-05,
        // superseding the 2026-08-04 dead-slot face): a padlock reports a TAB's
        // read-only bit, and in there the pair is not tabs but the compare
        // selector, so there is no such bit to report and nothing to dim. Not
        // drawn, not reserved in the width above, and no rect published — the
        // press path's lock branch is therefore unreachable in the mode without
        // testing for it, and bare `o` (still blocked by the allowlist) has no
        // pointer affordance left to lie about.
        if (!compare_selector) {
            const ViewState& vs = (def.letter == 'B') ? app.tab_b : app.tab_a;
            const int lx = x + tab_w - lock_mar - lock_box;
            const int ly = lane.y + (content_h - lock_box) / 2;
            if (vs.read_only) {
                icons::draw(cr, icons::Icon::Lock,
                            static_cast<double>(lx), static_cast<double>(ly),
                            static_cast<double>(lock_box), 1.0, tab_face);
            } else {
                icons::draw(cr, icons::Icon::Unlock,
                            static_cast<double>(lx), static_cast<double>(ly),
                            static_cast<double>(lock_box),
                            kRedesignDisabledMix, tab_face);
            }
            if (selected) app.tab_lock_rect = GuiRect{lx, ly, lock_box, lock_box};
        }

        x += tab_w;
    }

    // THE BORDER-BOTTOM, full window width at the lane's last row — EXCEPT under
    // the selected tab, where it BREAKS because that tab opens into the content
    // below. Two pixel-bound rectangle fills (left of the tab, right of it),
    // either of which is empty when the selected tab sits at an edge; crisp on
    // integer bounds like every other axis-aligned 1px fill in these rows.
    cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                         kRedesignTabLine.b);
    const int border_y = lane.y + content_h;
    if (sel_w <= 0) {
        cairo_rectangle(cr, lane.x, border_y, lane.w, border_h);
        cairo_fill(cr);
    } else {
        if (sel_x > lane.x) {
            cairo_rectangle(cr, lane.x, border_y, sel_x - lane.x, border_h);
            cairo_fill(cr);
        }
        const int right_x = sel_x + sel_w;
        if (right_x < lane.x + lane.w) {
            cairo_rectangle(cr, right_x, border_y,
                            lane.x + lane.w - right_x, border_h);
            cairo_fill(cr);
        }
    }

    cairo_restore(cr);
}

void GuiPaintHandler::paint_icon_row(cairo_t* cr) {
    // THE ICON ROW (top lane 3, row 4 of the redesign): the same #202326 ground
    // the tab row above opens into, a 1px border-bottom across the WHOLE window
    // width, four vertical separators, and fourteen 32x32 buttons in five
    // groups — the S/T and W/P view radios, the phase-reset copy/paste pair with
    // the bpm / iteration / follow modes, the listen / load-in-place pair, and
    // the history group: the mode's own button (2026-08-04) plus the walk's
    // older / newer arrows (2026-08-05).
    // (The zoom out/in pair lived here for one day, 2026-08-01 to 2026-08-02;
    // the Navigation dropdown is those two commands' pointer home now.)
    //
    // NO FOCUS SWAP HERE: this ground already IS the unfocused shade rows 1 and
    // 2 darken to, so there is nothing for it to change to (redesign_row_ground
    // is deliberately not called).
    //
    // FIVE FACES, AND NO DISABLED ONE — the architect supplied exactly these:
    //   REST          — the bare glyph on the row ground, no chrome.
    //   HOVER         — a 1px accent rounded OUTLINE. THE RULED READING is that
    //                   hover IS the outline, applied over WHICHEVER fill the
    //                   button has: the selectedhover crop is the accent outline
    //                   over the selected fill, unchanged otherwise.
    //   CLICK         — the interior filled with the row ground tinted 30%
    //                   toward the accent, the SAME kRedesignClickMix machinery
    //                   row 2 uses, under the accent outline.
    //   SELECTED      — kRedesignSelectedFill under a 1px kRedesignLine outline,
    //                   persistent, reading the live fact its chord flips
    //                   (redesign_button_selected).
    //   SELECTED+HOVER— the selected fill under the ACCENT outline.
    // SELECTED + CLICK was not supplied, and THE CLICK FILL WINS while held: a
    // press is transient and its feedback should be the same wherever it lands,
    // so the pressed tint replaces the selected fill for exactly the hold and
    // the selected fill returns at the release.
    // THE ABSENT DISABLED FACE IS A SCOPE DIFFERENCE FROM ROW 2, deliberately:
    // presses here always dispatch and the CHORDS' OWN refusals answer (the
    // read-only gate blocks the authoring ones, loading blocks everything),
    // inherited through on_key rather than mirrored — the standing
    // chord-dispatch ruling doing exactly the work it exists for. THE TWO RULED
    // EXCEPTIONS ARE BOTH THE `h` HISTORY VIEW'S, and both are a MODE rather
    // than a refusal, which is what the per-press refusals above cannot express:
    // while the view stands it greys the buttons it consumes (architect
    // 2026-08-04, at the face code below), and INVERSELY the walk's two arrow
    // buttons rest DISABLED and light only in there (architect 2026-08-05) —
    // their keys, bare `,` and bare `.`, are bound nowhere else in the product.
    // Both live at redesign_button_enabled, so this row's painter needs no arm
    // of its own for either.
    const GuiRect lane = top_icon_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    const int border_h  = icon_row_border_h_px();
    const int content_h = lane.h - border_h;
    if (content_h <= 0) return;

    cairo_save(cr);

    cairo_set_source_rgb(cr, kRedesignTabGround.r, kRedesignTabGround.g,
                         kRedesignTabGround.b);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, content_h);
    cairo_fill(cr);

    // The border-bottom runs the ENTIRE window width with no break — the tab
    // row's break is the tab row's own fact, about a tab opening into this
    // surface; nothing opens into what is below here.
    cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                         kRedesignTabLine.b);
    cairo_rectangle(cr, lane.x, lane.y + content_h, lane.w, border_h);
    cairo_fill(cr);

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int btn      = scaled_px(kIconBtnPx);
    const int btn_gap  = scaled_px(kIconBtnGapPx);
    const int sep_gap  = scaled_px(kIconSepGapPx);
    const int sep_w    = scaled_px(kIconSepWidthPx);
    const int sep_h    = scaled_px(kIconSepHeightPx);
    const int glyph_px = scaled_px(kIconGlyphPx);
    const int lw       = std::max(1, scaled_px(kIconOutlineStrokePx));
    const double radius = std::nearbyint(kIconCornerRadiusPx *
                                         gui_scale_factor());

    // EVERYTHING CENTERS IN THE CONTENT BAND — see the constants block: this is
    // the whole vertical layout, and it is what absorbs the 46/48 discrepancy.
    const int btn_y = lane.y + (content_h - btn)   / 2;
    const int sep_y = lane.y + (content_h - sep_h) / 2;

    int x = lane.x + scaled_px(kIconRowPadLeftPx);
    for (const IconRowDef& def : kIconRowButtons) {
        if (def.lead == IconRowLead::Separator) {
            x += sep_gap;
            cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                                 kRedesignTabLine.b);
            cairo_rectangle(cr, x, sep_y, sep_w, sep_h);
            cairo_fill(cr);
            x += sep_w + sep_gap;
        } else if (def.lead == IconRowLead::Gap) {
            x += btn_gap;
        }

        AppState::RedesignButtonFace& face = publish_button_face(
            app, audio.total_frames(), def.id, GuiRect{x, btn_y, btn, btn});

        // THE SIXTH FACE, AND THE ROW'S ONLY DEAD ONE: the `h` history view
        // (architect 2026-08-04). It is the ruled EXCEPTION to the never-grey
        // rule above, scoped to that mode alone — while the view stands, copy,
        // paste, bpm, iteration, follow and listen are consumed acts and say so,
        // while the S/T + W/P radios, the load-in-place opener, the history
        // button itself and the walk's two arrows stay live. Which is which is
        // DERIVED from the mode's own gates
        // (history_mode_disables_button, input_pointer.cpp, where the whole
        // partition is inventoried); nothing here decides membership.
        //
        // THE SAME FACE, WORN AT REST, IS THE WALK'S TWO ARROWS OUTSIDE THE
        // VIEW (architect 2026-08-05): the mode-scoped exception inverted, for
        // the two buttons whose keys exist only inside it. Same blend, same
        // pointer-dead press and same absent tooltip, decided at the same one
        // predicate — so the row still has exactly one dead face, in two
        // states of one mode rather than in two mechanisms.
        //
        // THE FACE IS THE ROW'S OWN INKS AT kRedesignDisabledMix — the product's
        // one disabled blend, row 2's rule applied to this row's glyph, letter
        // and box: everything retains that fraction of itself over what sits
        // under it, so a dead button dims as ONE object. A dead SELECTED toggle
        // (iteration or follow left on) keeps its fill and outline muted rather
        // than dropped: the mode cannot change that state, so hiding it would be
        // a lie, and dimming it says "true, but not yours right now". Colour
        // only — gui_scale moves geometry, so the face is identical at 100% and
        // 200%.
        const double keep = face.enabled ? 1.0 : kRedesignDisabledMix;
        // Both pointer faces are gated on the live bit rather than trusted: the
        // recompute refuses to hover a disabled button and the claim never
        // records a press on one, but a button can go dead UNDER either with no
        // pointer event to refresh it (row 2's outline carries the same guard).
        const bool hovered = face.hovered && face.enabled;
        const bool pressed =
            face.enabled &&
            app.redesign_pressed == redesign_button_index(def.id);

        // THE FILL AND THE OUTLINE ARE DECIDED SEPARATELY, which is exactly the
        // architect's reading of the five crops: the outline says "the pointer
        // is here" and the fill says "this is the state", so every combination
        // of the two falls out instead of being enumerated.
        const bool has_fill = pressed || face.selected;
        const bool has_line = hovered || pressed || face.selected;
        // What the glyph ends up sitting on, which is the ground its own dim
        // mixes toward: the painted fill where there is one, else the row.
        GuiColor under = kRedesignTabGround;
        if (has_fill || has_line) {
            // The shared face box (redesign_face_box — one path, filled and
            // stroked, the half-stroke inset rule stated there). THIS ROW'S FIT
            // SETTLED THE CONSTRUCTION: fitting both candidates against the
            // selected crop, the shared inset path scores 227 where the
            // full-box fill scores 270 at its own best radius and 2129 at this
            // one, and it is what the source widget does (a single rounded rect
            // drawn with both a brush and a pen).
            const GuiColor fill = mix_color(
                pressed ? mix_color(kRedesignAccent, kRedesignTabGround,
                                    kRedesignClickMix)
                        : kRedesignSelectedFill,
                kRedesignTabGround, keep);
            // Accent when the pointer is on it or it is held; otherwise the
            // calm grey that frames a resting toggled-on button.
            const GuiColor line = mix_color(
                (hovered || pressed) ? kRedesignAccent : kRedesignLine,
                kRedesignTabGround, keep);
            redesign_face_box(cr, x, btn_y, btn, btn, lw, radius,
                              has_fill ? &fill : nullptr,
                              has_line ? &line : nullptr);
            if (has_fill) under = fill;
        }

        if (def.glyph != nullptr) {
            // A LETTER BUTTON: the shaped glyph centered on BOTH axes — the
            // width from the run itself (never a font metric guess) and the
            // baseline from the shared extents solver.
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, def.glyph);
            const GuiColor letter_c = mix_color(kRedesignLabel, under, keep);
            cairo_set_source_rgb(cr, letter_c.r, letter_c.g, letter_c.b);
            text_shape::show_shaped_run(
                cr, run,
                static_cast<double>(x) +
                    std::nearbyint((static_cast<double>(btn) - run.width_px) *
                                   0.5),
                redesign_baseline(font, static_cast<double>(btn_y),
                                  static_cast<double>(btn)));
        } else {
            // An ICON BUTTON: the 22px box centered in the 32px button (+5 at
            // 100%), each path in its own color from the icon table. The dimming
            // term is the mode-scoped dead face's and is inert (keep == 1, the
            // table's colors bit-identical) in every other state.
            icons::draw(cr, def.icon,
                        static_cast<double>(x + (btn - glyph_px) / 2),
                        static_cast<double>(btn_y + (btn - glyph_px) / 2),
                        static_cast<double>(glyph_px), keep, under);
        }

        x += btn;
    }

    cairo_restore(cr);
}


// -- The floating surfaces ---------------------------------------------------

void GuiPaintHandler::paint_popup_chrome(cairo_t* cr, const GuiRect& r,
                                         GuiColor ground, GuiColor border) {
    // ONE BOX SHAPE FOR BOTH FLOATING SURFACES, TWO COLOR PAIRS. The tooltip and
    // the dropdown are built the same way and dressed differently — kdenlive
    // gives its menus a darker ground and a softer border than its tooltips, and
    // each crop pinned its own pair — so the colors are the caller's and the
    // geometry is the shared face box (redesign_face_box — one path, filled
    // then stroked, the half-stroke inset rule stated there).
    const int    lw  = popup_border_px();
    const double rad = std::nearbyint(kPopupCornerRadiusPx * gui_scale_factor());
    redesign_face_box(cr, r.x, r.y, r.w, r.h, lw, rad, &ground, &border);
}

void GuiPaintHandler::paint_shift_tooltip(cairo_t* cr) {
    // THE HOVER TOOLTIP, on whichever roster button is hovered — at most one,
    // because at most one button is hovered. The tick owns WHEN it appears (the
    // dwell); this owns only what it looks like, and publishes the rect it
    // painted so the hide edge can damage it.
    app.redesign_tooltip.rect = GuiRect{0, 0, 0, 0};
    if (!app.redesign_tooltip.visible) return;

    int hovered = -1;
    for (int i = 0; i < kRedesignButtonCount; ++i) {
        const RedesignButton id = static_cast<RedesignButton>(i);
        if (redesign_button_tooltip(app, id).line1 == nullptr) continue;
        // A DISABLED BUTTON ADVERTISES NOTHING: a greyed Render's chords are as
        // refused as each other, so it gets no hint. The hover recompute already
        // refuses to hover a disabled button; this is the belt to that braces,
        // and it keeps the rule stated where it is visible.
        if (app.redesign_buttons[i].hovered &&
            redesign_button_enabled(app, audio.total_frames(), id)) {
            hovered = i;
            break;
        }
    }
    if (hovered < 0) return;

    const RedesignTooltipText text =
        redesign_button_tooltip(app, static_cast<RedesignButton>(hovered));
    const GuiRect& btn = app.redesign_buttons[hovered].rect;
    if (btn.w <= 0 || btn.h <= 0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);

    // TWO SIZES ON ONE CONTEXT, IN TWO PHASES — MEASURE, THEN PAINT — and the
    // phase split is not tidiness, it is the chokepoint's stated precondition:
    // show_shaped_run must run with THE SAME scaled font set on `cr` that shaped
    // the run (text_shape.h). Shaping both lines up front and then painting both
    // would leave the SECOND size on the context while the FIRST line's glyphs
    // were emitted — its 12pt-shaped positions rendered at 10pt, which is a
    // wrong-size line with mis-spaced glyphs, exactly the mismatch signature the
    // contract warns about. So each line's size is (re-)set immediately before
    // its own paint, below.
    //
    // The SIZE, not the font POINTER, is what is carried between the phases:
    // cairo_get_scaled_font returns a borrowed reference that a later
    // cairo_set_font_size releases, so a pointer held across a size change is not
    // ours to use. Each phase re-fetches; only plain doubles cross.
    const double size1 = redesign_font_size_px();
    const double size2 =
        kTooltipShiftFontSizePt * 96.0 / 72.0 * gui_scale_factor();
    const bool two_line = (text.line2 != nullptr);

    cairo_set_font_size(cr, size1);
    cairo_scaled_font_t* f1 = cairo_get_scaled_font(cr);
    const text_shape::ShapedRun r1 = text_shape::shape_text_run(f1, text.line1);
    cairo_font_extents_t fe1;
    cairo_scaled_font_extents(f1, &fe1);
    const double band1 = fe1.ascent + fe1.descent;

    double band2 = 0.0, w2 = 0.0;
    text_shape::ShapedRun r2;
    if (two_line) {
        cairo_set_font_size(cr, size2);
        cairo_scaled_font_t* f2 = cairo_get_scaled_font(cr);
        r2 = text_shape::shape_text_run(f2, text.line2);
        cairo_font_extents_t fe2;
        cairo_scaled_font_extents(f2, &fe2);
        band2 = fe2.ascent + fe2.descent;
        w2 = r2.width_px;
    }

    const int pad_x = scaled_px(kTooltipPadXPx);
    const int pad_y = scaled_px(kTooltipPadYPx);
    const int gap   = two_line ? scaled_px(kTooltipLineGapPx) : 0;
    const int w = static_cast<int>(std::nearbyint(std::max(r1.width_px, w2))) +
                  2 * pad_x;
    // THE SYMMETRIC BOX: the same pad above the first band and below the last,
    // with a real gap between them. Nothing is authored but the two paddings and
    // the gap; the bands are the face's own.
    const int h = static_cast<int>(std::nearbyint(band1 + band2)) + gap +
                  2 * pad_y;

    // BELOW THE BUTTON, LEFT-ALIGNED WITH IT, then CLAMPED FULLY ON-WINDOW so a
    // button near an edge cannot push it off. The clamp is a pure position fix —
    // the box never shrinks, because a truncated hint would be worse than one
    // that shifted.
    int x = btn.x;
    int y = btn.y + btn.h;
    if (x + w > app.width)  x = app.width - w;
    if (x < 0) x = 0;
    if (y + h > app.height) y = app.height - h;
    if (y < 0) y = 0;
    app.redesign_tooltip.rect = GuiRect{x, y, w, h};

    paint_popup_chrome(cr, app.redesign_tooltip.rect, kRedesignRowGround,
                       kRedesignLine);

    // THE PAINT PHASE. Each line RE-SETS its own size first, so the context
    // carries the very font that shaped the run it is about to emit — the
    // chokepoint's precondition, honored per line. Line 1 is therefore
    // byte-identical in face, size and paint to the one-line form's line, which
    // is the whole point: the two forms differ by an added line, not by anything
    // about the first one.
    //
    // Each line sits on ITS OWN band, so each baseline is that band's foot — the
    // shared centrer applied to a slot that is exactly the band, which makes the
    // symmetry above true of the ink and not merely of the arithmetic.
    cairo_set_font_size(cr, size1);
    cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                         kRedesignLabel.b);
    text_shape::show_shaped_run(
        cr, r1, static_cast<double>(x + pad_x),
        redesign_baseline(cairo_get_scaled_font(cr),
                          static_cast<double>(y + pad_y), band1));
    if (two_line) {
        cairo_set_font_size(cr, size2);
        // The hint line is DIMMED by the one measured factor, uniformly.
        const GuiColor dim =
            mix_color(kRedesignLabel, kRedesignRowGround, kRedesignDimMix);
        cairo_set_source_rgb(cr, dim.r, dim.g, dim.b);
        text_shape::show_shaped_run(
            cr, r2, static_cast<double>(x + pad_x),
            redesign_baseline(cairo_get_scaled_font(cr),
                              static_cast<double>(y + pad_y) + band1 + gap,
                              band2));
    }

    cairo_restore(cr);
}

void GuiPaintHandler::paint_dropdown(cairo_t* cr) {
    // THE MENU ROW'S DROPDOWN — ONE painter for BOTH menus, hanging flush under
    // the button that emits it at ZERO margin: its top edge is the BUTTON's
    // bottom edge (not the lane's: row 1's 1px margin-bottom puts those one pixel
    // apart, and the architect ruled the button; the full argument is at the
    // anchor arithmetic below), under that button's left edge. Publishes its own
    // rect and every item rect, so the press claim hit-tests exactly what was
    // painted and never re-shapes a label.
    //
    // ITS CHROME IS ITS OWN (kRedesignPopupGround under kRedesignTabLine), not
    // the tooltip's: kdenlive dresses menus darker than hints, and dropdown_full
    // is the authority for this surface.
    //
    // NO ICONS, NO CHECKBOXES, NO SUBMENU ARROWS, by ruling — the crops reserve
    // all three columns and this product has none of them, exactly as the tabs
    // dropped theirs. What the columns' SPACE becomes is the labels' left INDENT
    // and the accelerator's right margin.
    //
    // THE TWO MENUS DIFFER IN EXACTLY ONE PLACE since 2026-08-03: the
    // accelerator COLUMN, which navigation has and settings does not. The width
    // FOLLOWS from it — one expression with an optional term — rather than being
    // a second difference of its own, and everything else (chrome, item height,
    // insets, separator, faces, baseline, and now the label indent and right
    // margin) is one set of numbers by construction.
    app.dropdown.rect = GuiRect{0, 0, 0, 0};
    app.dropdown.item_rects = {};
    if (!app.dropdown.open()) return;

    const DropdownMenu menu = app.dropdown.menu;
    const int count = dropdown_item_count(menu);
    const GuiRect& btn =
        app.redesign_buttons[redesign_button_index(
            dropdown_anchor_button(menu))].rect;
    if (btn.w <= 0 || btn.h <= 0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int border    = popup_border_px();
    const int item_h    = popup_item_h_px();
    const int block_mar = popup_item_margin_y_px();
    const int inset     = scaled_px(kPopupItemInsetPx);
    const int sep_inset = scaled_px(kPopupSepInsetPx);
    const int sep_mar   = popup_sep_margin_y_px();
    const int sep_block = 2 * sep_mar + border;   // margin, line, margin
    const int lw        = border;
    const double radius = std::nearbyint(kPopupCornerRadiusPx *
                                         gui_scale_factor());

    // WIDTH FROM THE WIDEST SHAPED RUN(S) behind the authored minimum — every
    // run is shaped once here and reused for the paint below, so the box and the
    // glyphs come from the same measurements (the displayed-basis doctrine).
    text_shape::ShapedRun runs[kDropdownMaxItemCount];
    text_shape::ShapedRun hot_runs[kDropdownMaxItemCount];
    double widest = 0.0, widest_hot = 0.0;
    bool has_hotkeys = false;
    for (int i = 0; i < count; ++i) {
        const DropdownRow row = dropdown_row(menu, i);
        runs[i] = text_shape::shape_text_run(font, row.label);
        widest = std::max(widest, runs[i].width_px);
        if (row.hotkey != nullptr) {
            has_hotkeys = true;
            hot_runs[i] = text_shape::shape_text_run(font, row.hotkey);
            widest_hot = std::max(widest_hot, hot_runs[i].width_px);
        }
    }
    // ONE WIDTH RULE WITH AN OPTIONAL COLUMN, authored on the POPUP box: the
    // label indent, the widest label, then — only where an accelerator column
    // exists — the guaranteed column gap and the widest accelerator, then the
    // right margin, less the chrome the item box adds back below.
    //
    // THE OPTIONAL TERM IS DRIVEN OFF THE ITEM TABLE, not off the menu
    // enumerator: an accelerator column is a property of the rows (a menu whose
    // rows carry no hotkey has none), and expressing it that way is what leaves
    // the two menus differing by one DERIVED term instead of by two hand-written
    // rules. A menu that grew hotkeys would widen here with no edit.
    //
    // The authored minimum applies to both — it is the item box's floor, the
    // reason a menu of short labels still reads as a menu — and the navigation
    // menu simply never reaches it.
    const int pad_l    = scaled_px(kPopupLabelIndentPx);
    const int gap      = scaled_px(kPopupHotkeyGapPx);
    const int pad_r    = scaled_px(kPopupPadRightPx);
    const int chrome_w = 2 * inset + 2 * border;
    const int content_w =
        pad_l + static_cast<int>(std::nearbyint(widest)) +
        (has_hotkeys ? gap + static_cast<int>(std::nearbyint(widest_hot)) : 0) +
        pad_r - chrome_w;
    const int item_w = std::max(scaled_px(kPopupItemMinWidthPx), content_w);
    const int w = item_w + chrome_w;
    // THE HEIGHT COMES FROM THE SHARED SUM, not a second walk here: the open
    // edge damages dropdown_h_px() before this ever runs, so the two must be one
    // expression.
    const int h = dropdown_h_px(menu);

    // FLUSH WITH THE BUTTON IT EMITS FROM, on BOTH axes (architect 2026-08-02).
    // When row 1 gained its 1px margin-bottom the button's bottom edge and the
    // lane's stopped being the same row of pixels, and the anchor briefly moved
    // to the LANE on the reading that "the menu row's bottom edge" meant the
    // whole lane. He ruled the button: the dropdown hangs off the thing that
    // opened it. So the box's top edge lands on row 1's MARGIN STRIP and covers
    // it for as long as the menu is up — that is the ruled look, not a leak.
    int x = btn.x;
    int y = btn.y + btn.h;               // flush: zero margin under the button
    if (x + w > app.width) x = app.width - w;
    if (x < 0) x = 0;
    app.dropdown.rect = GuiRect{x, y, w, h};

    paint_popup_chrome(cr, app.dropdown.rect, kRedesignPopupGround,
                       kRedesignTabLine);

    // The item block opens BELOW the border by its own margin, and closes with
    // the same margin above the bottom border — the crop's 3px, mirrored.
    int iy = y + border + block_mar;
    for (int i = 0; i < count; ++i) {
        const DropdownRow row = dropdown_row(menu, i);
        if (row.separator_before) {
            // 1px line, inset horizontally, with its own vertical margin against
            // the item on each side. Pixel-bound fill, crisp by construction.
            cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                                 kRedesignTabLine.b);
            cairo_rectangle(cr, x + sep_inset, iy + sep_mar,
                            w - 2 * sep_inset, border);
            cairo_fill(cr);
            iy += sep_block;
        }
        // ITEMS TOUCH — zero vertical gap between adjacent ones — and each
        // one's box insets horizontally from the border. The published rect is
        // that box, so the clickable area is exactly the area that lights.
        const GuiRect item{x + inset, iy, item_w, item_h};
        app.dropdown.item_rects[static_cast<size_t>(i)] = item;

        const bool pressed = (app.dropdown.pressed_item == i);
        const bool hovered = (app.dropdown.hovered_item == i);
        if (pressed || hovered) {
            // TWO FACES FROM THE ITEM CROPS, and they are built differently
            // because one has an outline and the other does not:
            //   PRESSED — the FULL accent fill over the WHOLE item box. No
            //     stroke, so no inset: the fill's own edge is the visible edge.
            //     It is visible at all only because items act on RELEASE, the
            //     one redesign surface that does.
            //     EXACTLY ONE ITEM IS EVER LIT, and that is the input side's
            //     doing rather than a rule here: the ARM FOLLOWS THE POINTER
            //     while a press is live, so the pressed and hovered indices are
            //     the same item then and this test simply picks the face —
            //     pressed while the button is down, hovered when it is not.
            //     Nothing distinguishes an item the pointer has left.
            //   HOVERED — the same 30% accent tint the click faces use, over
            //     THIS popup's ground, under a 1px outline of the accent
            //     lightened 15% toward white. Fill and stroke share ONE INSET
            //     path so they describe one rectangle.
            // The half-stroke inset on the hovered form makes the two faces the
            // SAME SIZE on screen (both outer edges land at `radius` from the
            // box corner), which is what the crops show — each saturates in the
            // same column.
            if (pressed) {
                cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                     kRedesignAccent.b);
                redesign_rounded_rect_path(cr, item.x, item.y,
                                           static_cast<double>(item.w),
                                           static_cast<double>(item.h), radius);
                cairo_fill(cr);
            } else {
                const GuiColor fill = mix_color(kRedesignAccent,
                                                kRedesignPopupGround,
                                                kRedesignClickMix);
                const GuiColor line = mix_color(GuiColor{1.0, 1.0, 1.0},
                                                kRedesignAccent,
                                                kRedesignHoverLightenMix);
                // The shared face box (redesign_face_box — the half-stroke
                // inset rule lives there).
                redesign_face_box(cr, item.x, item.y, item.w, item.h, lw,
                                  radius, &fill, &line);
            }
        }

        // LEFT-ALIGNED AT THE ONE INDENT, measured from the POPUP box's own left
        // edge in both menus, and vertically centred by the shared solver. On
        // the settings menu the right side carries the leftover, which the
        // minimum width above is what guarantees; on the navigation menu the
        // accelerator carries it.
        const double base = redesign_baseline(font,
                                              static_cast<double>(item.y),
                                              static_cast<double>(item.h));
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(cr, runs[i],
                                    static_cast<double>(x + pad_l), base);

        // THE ACCELERATOR COLUMN is RIGHT-ALIGNED to the popup's own right
        // margin, not to the item box's: the margin is a fact about the box the
        // crop measured, and aligning to it keeps every hotkey's last ink column
        // on one line whatever the item inset is. Its ink is the sampled dim
        // (kRedesignPopupHotkey), in every face — the item's fill is the whole
        // hover/press cue, as it is for the label.
        if (row.hotkey != nullptr) {
            const double hot_x =
                static_cast<double>(x + w - pad_r) -
                std::nearbyint(hot_runs[i].width_px);
            cairo_set_source_rgb(cr, kRedesignPopupHotkey.r,
                                 kRedesignPopupHotkey.g,
                                 kRedesignPopupHotkey.b);
            text_shape::show_shaped_run(cr, hot_runs[i], hot_x, base);
        }
        iy += item_h;
    }

    cairo_restore(cr);
}

// -- THE RULER LANE (top lane 5, row 5 of the redesign) ---------------------
//
// A LOOK/MODEL SPLIT, and it is deliberate: the ruler takes KDENLIVE'S LOOK and
// REAPER'S GEOMETRY MODEL (architect 2026-08-01).
//   LOOK, from row_5_full.png: 1px #737373 ticks, a #c2c2c2 label at the
//     redesign's ordinary 12pt, and the two tick lengths differing at their TOP
//     — majors rise 4px above the marker lane, minors start at it, and BOTH run
//     down to the marker lane's bottom (the waveform top). That shared bottom is
//     what makes "the majors peek above the flags" the whole mechanism; the
//     brief's "minors end where the marker band begins" was superseded by the
//     measurement.
//   MODEL, from Reaper: WHERE the ticks go. A round ladder of labeled steps, the
//     smallest rung whose label pitch clears the minimum, and eight binary
//     minors inside each step.
// The composite's own tick spacing is neither — it is a hand-assembled kdenlive
// frame, and it carries elements (a zone edge or a guide) this product has no
// analogue for. It was measured for the LOOK only.
//
// PAINT-ONLY. Nothing here snaps, authors, or hit-tests: the ladder decides
// pixels and nothing else.
namespace {

// THE ROUND LADDER of labeled steps, in milliseconds. Every rung is a value a
// musician reads without arithmetic; the gaps (no 3s, no 15s, no 45s) are the
// point, not an omission.
constexpr int64_t kRulerLadderMs[] = {
    125, 250, 500, 1000, 2000, 5000, 10000, 30000,
    60000, 120000, 300000, 600000, 1800000, 3600000,
};
// Eight binary minors inside each labeled step: the step halves three times, so
// a minor is always a musically-round fraction of its label.
constexpr int  kRulerMinorsPerStep = 8;
// The pitch rule, stated on the MINOR because that is the crowding that matters:
// a rung is admissible while its minors stay at least this far apart, which puts
// its labels at least 8x that apart.
constexpr double kRulerMinMinorPitchPx = 12.0;
// The label band's top padding; with the 12pt band this lands the baseline on
// the composite's own label rows.
constexpr double kRulerLabelPadTopPx   = 4.0;
// How far a MAJOR tick rises above the marker lane. Minors rise none.
constexpr double kRulerMajorRisePx     = 4.0;

// The smallest ladder rung whose minors clear the minimum pitch. Falls back to
// the coarsest rung when even that crowds (an absurd zoom-out), which is the
// honest answer: keep the topmost rung rather than draw a solid band of ticks.
int64_t ruler_step_ms(double ms_per_px) {
    if (ms_per_px <= 0.0) return kRulerLadderMs[0];
    for (int64_t step : kRulerLadderMs) {
        const double minor_px =
            (static_cast<double>(step) / kRulerMinorsPerStep) / ms_per_px;
        if (minor_px >= kRulerMinMinorPitchPx * gui_scale_factor()) return step;
    }
    return kRulerLadderMs[std::size(kRulerLadderMs) - 1];
}

// `M:SS.mmm`, REAPER VERBATIM: the minutes field is ALWAYS present, zero
// included — "0:47.250" below a minute, not "47.250" (architect 2026-08-01,
// retiring the leading-unit dropping, which was the planner's invention and not
// what Reaper does; a field that appears and disappears makes the ruler's own
// format a moving target to read).
//
// THE MILLISECONDS RULE STANDS: the fraction shows only while the labelled step
// is sub-second, so a bar-level ruler reads "1:30" and a transient-level one
// reads "0:02.250" — each showing exactly what varies, which is the half of the
// old rule that was actually Reaper's.
std::string ruler_label_text(int64_t ms, int64_t step_ms) {
    if (ms < 0) ms = 0;
    const int64_t m   = ms / 60000;
    const int64_t s   = (ms % 60000) / 1000;
    const int64_t mil = ms % 1000;
    char buf[32];
    if ((step_ms % 1000) != 0)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld.%03lld",
                      (long long)m, (long long)s, (long long)mil);
    else
        std::snprintf(buf, sizeof(buf), "%lld:%02lld",
                      (long long)m, (long long)s);
    return std::string(buf);
}

} // namespace

void GuiPaintHandler::paint_ruler_row(cairo_t* cr) {
    const GuiRect lane   = top_ruler_row_area(app);
    const GuiRect marker = top_marker_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    cairo_save(cr);
    cairo_set_source_rgb(cr, kRedesignTabGround.r, kRedesignTabGround.g,
                         kRedesignTabGround.b);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, lane.h);
    cairo_fill(cr);

    // THE DISPLAYED BASIS, not the live viewport: the ruler must agree with the
    // pixels actually on screen, so it reads the same plate epoch the playheads
    // and the flag cache do. That is also what hooks it to the per-pan/zoom
    // repaint — every user-driven viewport change runs the synchronous plate
    // rebuild and repaints the strip, and this pass rides along with it.
    const PlateViewportBasis basis = plate_viewport_basis();
    const int sr = audio.sample_rate();
    if (basis.spp <= 0.0 || sr <= 0) { cairo_restore(cr); return; }

    const double ms_per_px = basis.spp * 1000.0 / static_cast<double>(sr);
    const double vp_ms     = basis.vp_start * 1000.0 / static_cast<double>(sr);
    // THE WALK WIDTH IS THE PLATE'S OWN, NOT THE LIVE ONE — the same
    // published-width rule the flag cache spells at its wave_w read
    // (waveform_cache.cpp). The spp above comes from the published fingerprint,
    // so the width bounding the tick walk, the label span and the head's
    // tick-crossing recording must be the width that spp was published AGAINST:
    // during the async resize window (on_resize stores new dimensions while the
    // OLD plate stays blitted until the worker publishes) a grown live width
    // would walk ticks across the newly exposed right-hand area at the old
    // plate's scale — ticks disagreeing with the ink they sit over. The
    // live-width fallback mirrors plate_viewport_basis's own cold arm (no plate
    // published yet, spp already live).
    const int wave_w = wf_cache.fp_area_w > 0 ? wf_cache.fp_area_w
                                              : waveform_area(app).w;
    if (ms_per_px <= 0.0 || wave_w <= 0) { cairo_restore(cr); return; }

    const int64_t step  = ruler_step_ms(ms_per_px);
    const double  end_ms = vp_ms + ms_per_px * wave_w;
    // (There is no `minor` time step any more. It had two consumers — the float
    // placement of each minor tick and the head's float re-derivation of which
    // columns carried one — and the rigid comb replaced both with integer
    // distribution across a segment. The MINORS-PER-STEP count is still the
    // ladder's own kRulerMinorsPerStep; only its expression as a duration is
    // gone.)

    const int tick_bottom = marker.y + marker.h;         // the waveform top
    const int minor_top   = marker.y;                    // no rise
    const int major_top   = marker.y - scaled_px(kRulerMajorRisePx);

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);
    cairo_font_extents_t fe;
    cairo_scaled_font_extents(font, &fe);
    const double baseline =
        redesign_baseline(font,
                          static_cast<double>(lane.y) +
                              std::nearbyint(kRulerLabelPadTopPx *
                                             gui_scale_factor()),
                          fe.ascent + fe.descent);

    // SUB-SECOND EMPHASIS: while the step is finer than a second, the WHOLE
    // SECONDS are the landmarks, so they take the brighter label white while
    // every other label stays the ruler's dim grey. One color swap, no second
    // type size — the simplest reproduction of Reaper's emphasis that survives
    // at every scale.
    const bool emphasize = step < 1000;

    // THE PLAYHEAD HEAD'S TICK-CROSSING WINDOW, declared before the walk because
    // the walk fills it: one byte per column across the head's widest row —
    // 0 = no tick, 1 = minor, 2 = major. The head block below repaints exactly
    // these columns in the pre-blended value, so the crossing is the ticks the
    // walk actually painted rather than a second derivation of where ticks ought
    // to be (which is what it was, and what the rigid comb would have made
    // wrong).
    //
    // FIXED CAPACITY, no allocation: kPlayheadHeadHalf[0] is 9 authored px and
    // the gui_scale schema caps at 200%, so the window is at most 2*18+1 = 37
    // columns. 48 is headroom; the recording clamps to it, so a raised ceiling
    // would lose the pre-blend at the outermost head columns rather than write
    // out of bounds.
    constexpr int kHeadTickWindowCap = 48;
    std::array<uint8_t, kHeadTickWindowCap> head_ticks{};
    const int head_half_max = scaled_px(kPlayheadHeadHalf[0]);
    const double head_px_pre = playhead_pixel_x(app, basis.vp_start, basis.spp);
    const int head_cursor_col = static_cast<int>(std::nearbyint(head_px_pre));
    int head_window = 2 * head_half_max + 1;
    if (head_window > kHeadTickWindowCap) head_window = kHeadTickWindowCap;
    const int head_col0 = head_cursor_col - head_half_max;

    // THE COMB IS RIGID UNDER PAN (architect 2026-08-01, from the alt+drag
    // shimmer at working zoom: the minor ticks visibly stepped at different
    // moments, a breathing comb; the majors read fine).
    //
    // THE MECHANISM, verified in the code this replaces: every tick rounded its
    // OWN float time->pixel position independently, `nearbyint((t - vp_ms) /
    // ms_per_px)`. Tick spacing in pixels is minor/ms_per_px, which is not an
    // integer in general, so each tick carries its own fractional phase; a pan
    // shifts every phase by the same amount but each tick crosses ITS rounding
    // boundary at a different viewport offset, so neighbours take their 1px step
    // on different frames. The comb breathes even though nothing about the time
    // grid moved.
    //
    // THE FIX: each MAJOR keeps its own rounded position, and its EIGHT MINORS
    // are placed at integer offsets DISTRIBUTED ACROSS THE SEGMENT'S INTEGER
    // WIDTH — offset(i) = nearbyint(i * seg_w / 8) from the major's rounded x,
    // where seg_w is the distance between two rounded majors. A distribution of
    // an integer width is a pure function of that width, so while the majors
    // translate by whole columns the entire comb translates with them, rigidly:
    // no minor rounds against the screen at all. Under ZOOM the widths change
    // and the distribution re-derives, which is a real spacing change rather
    // than jitter.
    //
    // THE TRADE, the architect's own ("the ticks are purely informative — I'd
    // rather have smoothness than total precision"): a distributed minor can sit
    // up to ONE PIXEL off its exact time position — half from its major's own
    // anchor rounding and half from the distribution's — measured at 1.000px
    // worst case over a swept zoom/rung/phase range, on segments the ladder
    // keeps at least 96px wide. It NEVER ACCUMULATES: every segment re-anchors
    // on its own major, so the error is bounded inside one segment rather than
    // walking across the ruler. One pixel on a countable informative line, in
    // exchange for a comb that stops breathing under every pan.
    const int64_t first_step = static_cast<int64_t>(std::floor(vp_ms / step));
    // A step index's own rounded column: the ONE place a tick position meets the
    // screen grid. Majors anchor here; minors are distributed between them.
    const auto major_col = [&](int64_t k) {
        const double t = static_cast<double>(k) * static_cast<double>(step);
        return static_cast<int>(std::nearbyint((t - vp_ms) / ms_per_px));
    };
    for (int64_t k = first_step; ; ++k) {
        const double step_ms = static_cast<double>(k) * static_cast<double>(step);
        if (step_ms > end_ms) break;
        const int mx    = major_col(k);
        const int seg_w = major_col(k + 1) - mx;
        for (int i = 0; i < kRulerMinorsPerStep; ++i) {
            // THE INTEGER COMB, and the only culling test there is: a tick is at
            // its distributed column or it is offscreen. The old float-time
            // pre-filter went with the float positions it filtered.
            const int col = (i == 0)
                ? mx
                : mx + static_cast<int>(std::nearbyint(
                      static_cast<double>(i) * static_cast<double>(seg_w) /
                      static_cast<double>(kRulerMinorsPerStep)));
            if (col < 0 || col >= wave_w) continue;
            const bool major = (i == 0);
            // Record the crossing for the playhead head below, which repaints
            // these exact columns in the pre-blended value. Recording what the
            // walk PAINTS is what keeps head and ticks one source of truth; the
            // head used to re-derive them from the float minor grid, which now
            // no longer describes where the ticks are.
            if (head_window > 0) {
                const int w_idx = col - head_col0;
                if (w_idx >= 0 && w_idx < head_window)
                    head_ticks[static_cast<size_t>(w_idx)] = major ? 2 : 1;
            }
            cairo_set_source_rgb(cr, kRulerTick.r, kRulerTick.g, kRulerTick.b);
            cairo_rectangle(cr, lane.x + col, major ? major_top : minor_top,
                            1, tick_bottom - (major ? major_top : minor_top));
            cairo_fill(cr);
            if (!major) continue;
            // The label sits just right of its own major tick, so the number and
            // the line it names cannot drift apart.
            // The label's TIME is still the exact step time — only tick
            // PLACEMENT is distributed, and a major is at its own exact time
            // anyway. Its x rides `col`, which for a major IS the rounded major,
            // so number and line cannot drift apart.
            const int64_t label_ms = static_cast<int64_t>(std::llround(step_ms));
            if (label_ms < 0) continue;
            const std::string txt =
                ruler_label_text(label_ms, step);
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, txt.c_str());
            const GuiColor c = (emphasize && (label_ms % 1000) == 0)
                                   ? kRedesignLabel : kRulerLabel;
            cairo_set_source_rgb(cr, c.r, c.g, c.b);
            text_shape::show_shaped_run(cr, run,
                                        static_cast<double>(lane.x + col + 3),
                                        baseline);
        }
    }

    // -- THE PLAYHEAD HEAD, the successor to the retired cursor triangle -----
    //
    // IT LIVES IN THIS PAINTER because it must know where the TICKS are: where a
    // tick's column crosses the head, those head pixels take a PRE-BLENDED
    // constant rather than the head's own grey, and the opaque-palette doctrine
    // has no compositing to do that with. Ticks and head therefore share one
    // owner and the crossing is exact instead of approximated.
    //
    // ALIASED BY CONSTRUCTION: the shape is a transcribed per-row HALF-WIDTH
    // table (kPlayheadHeadHalf), painted as integer rectangles — one per row —
    // so it has hard edges at every scale, which a path fill would not. At
    // gui_scale > 100% each source row becomes `s` device rows and each
    // half-width scales with it, which enlarges the pixel steps rather than
    // smoothing them: the shape stays the drawing it was transcribed from.
    //
    // TIP-DOWN ON THE MARKER LANE'S BOTTOM ROWS, its tip ON the waveform
    // boundary, centered on the playhead column — the same column the stem below
    // runs on, so head and stem are one object.
    //
    // THE HEAD MOVED OUT OF THE RULER LANE (architect 2026-08-01, at the row-5
    // live test). It sat on the ruler's bottom rows; it now occupies the MARKER
    // lane's BOTTOM rows (the relocation's first shape put it at that lane's TOP
    // and the architect amended it the same day, for the stem parity the band
    // block below states), and the ruler lane is labels + tick-tops only. The point
    // of the move is OCCLUSION: the flag blit follows this pass, so a marker
    // sharing the cursor's column now covers part of the head — the accepted
    // look, and the hidden-by-marker model reaching the head itself rather than
    // only the stem below it.
    //
    // IT STAYS IN THIS PAINTER even though it no longer paints in this painter's
    // own lane, and for the reason it was here to begin with: the tick-crossing
    // pre-blend needs the tick columns, and the ticks are walked here. Moving
    // the head to a marker-lane painter would split one object across two
    // owners and re-derive the tick grid a second time.
    //
    // THE HEAD'S TIP STANDS ON THE WAVEFORM BOUNDARY, so the cursor's stem
    // begins exactly where the head ends: render_playhead's waveform segment
    // (paint_playheads) starts at the waveform's top row, and head_bottom below
    // IS that row, so head and stem meet with no gap and no lane pixel between
    // them — one unbroken line across the seam, with no other playhead pixel in
    // this lane. The pass order does the rest: ticks, then the head, then the
    // flag blit over both (the hidden-by-marker model reaches the head too).
    // The stem's WAVEFORM segment and the marker stems are already down by the
    // time this pass runs, in a band this one never touches (the sequence is
    // the paint-order block in on_redraw).
    {
        const double cursor_px = playhead_pixel_x(app, basis.vp_start, basis.spp);
        const int col = static_cast<int>(std::nearbyint(cursor_px));
        if (col >= 0 && col < wave_w) {
            const double s   = gui_scale_factor();
            const int    rows = static_cast<int>(std::nearbyint(
                                    kPlayheadHeadHeightPx * s));
            // THE BAND IS THE MARKER LANE'S BOTTOM `rows`, tip ON the waveform
            // boundary (architect 2026-08-01, amending the first relocation,
            // which put it at the lane's top). At 100% that is rows 45..56 of a
            // 37..56 lane, with the waveform starting at 57.
            //
            // THE POINT IS STEM PARITY: the playhead's stem begins exactly
            // where every marker stem begins — the waveform top — so the two
            // read as the same object at the same length. head_bottom IS the
            // waveform top, which is what hands the line off to
            // render_playhead's waveform segment with no lane pixels in
            // between.
            const int    head_bottom = marker.y + marker.h;
            const int    head_top    = head_bottom - rows;
            for (int r = 0; r < rows; ++r) {
                // Each device row reads its SOURCE row's half-width, so the
                // transcribed silhouette survives scaling as steps, not slopes.
                const int src = std::min(kPlayheadHeadHeightPx - 1,
                                         static_cast<int>(r / s));
                const int half = static_cast<int>(std::nearbyint(
                                     kPlayheadHeadHalf[src] * s));
                const int y0 = head_top + r;
                const int x0 = lane.x + col - half;
                const int w  = 2 * half + 1;
                cairo_set_source_rgb(cr, kPlayheadHead.r, kPlayheadHead.g,
                                     kPlayheadHead.b);
                cairo_rectangle(cr, x0, y0, w, 1);
                cairo_fill(cr);
            }
            // THE TICK CROSSINGS, painted back over the head in the pre-blended
            // value. Re-walking the ladder would be a second source of truth, so
            // the crossing is decided the cheap way instead: a column carries a
            // tick exactly when it is a whole number of minors from the origin,
            // which is the same test the walk above used.
            //
            // ONLY THE ACTUAL INTERSECTION IS RECOLORED, and the intersection is
            // computed from the SAME tops and bottom the tick walk drew with
            // rather than assumed. THE MOVE INTO THE MARKER LANE CHANGED THE
            // ANSWER COMPLETELY, which is exactly why this is computed:
            //   * BEFORE, on the ruler's bottom rows, a MINOR never touched the
            //     head at any scale (its top was marker.y, which WAS the head's
            //     bottom — abutting bands) and a MAJOR touched only its own
            //     rise, 4 rows of 12 at 100%.
            //   * NOW both classes cut the FULL SILHOUETTE. The head sits on
            //     the marker lane's BOTTOM rows and every tick runs the lane's
            //     whole height to the waveform top, which is exactly the head's
            //     tip row. Minors start at the lane top, majors `rise` above it;
            //     either way the tick covers the head end to end. So both
            //     intersections are [head_top, head_bottom) entire — 12 rows of
            //     12 at 100%, 18 of 18 at 150%, 24 of 24 at 200%.
            // The major/minor split is KEPT even though the two now resolve to
            // the same band: it is the tick walk's own `major = (i == 0)` fact,
            // and asserting "they coincide" in code instead of deriving it is
            // how the previous geometry's answer would have survived this move
            // as a silent lie.
            cairo_set_source_rgb(cr, kPlayheadHeadTick.r, kPlayheadHeadTick.g,
                                 kPlayheadHeadTick.b);
            // THE CROSSING READS THE COMB THE WALK PAINTED. It used to re-derive
            // the tick columns from the float minor grid — "is this column within
            // half a pixel of a whole multiple of `minor`" — which was a second
            // derivation that happened to agree while every tick rounded its own
            // float position. IT WOULD NOT AGREE NOW: the minors are distributed
            // across their segment's integer width (the rigid-comb note at the
            // walk), so their columns are no longer a function of time alone.
            // The walk records each painted tick into head_ticks, and this reads
            // it back — one source of truth by construction rather than by two
            // expressions being kept in step.
            //
            // The window is centred on the same cursor column this block paints
            // the head at (head_col0 was derived from it before the walk), so
            // the two cannot drift.
            for (int dx = -head_half_max; dx <= head_half_max; ++dx) {
                const int tc = col + dx;
                if (tc < 0 || tc >= wave_w) continue;
                const int w_idx = tc - head_col0;
                if (w_idx < 0 || w_idx >= head_window) continue;
                const uint8_t kind = head_ticks[static_cast<size_t>(w_idx)];
                if (kind == 0) continue;
                // WHICH tick it is decides where it starts, straight off the
                // walk's own `major = (i == 0)` verdict (2 = major, 1 = minor).
                const bool major_here = (kind == 2);
                const int tick_top = major_here ? major_top : minor_top;
                const int y_lo = std::max(tick_top, head_top);
                const int y_hi = std::min(tick_bottom, head_bottom);
                for (int y = y_lo; y < y_hi; ++y) {
                    const int src = std::min(kPlayheadHeadHeightPx - 1,
                                             static_cast<int>((y - head_top) / s));
                    const int half = static_cast<int>(std::nearbyint(
                                         kPlayheadHeadHalf[src] * s));
                    if (dx < -half || dx > half) continue;
                    cairo_rectangle(cr, lane.x + tc, y, 1, 1);
                }
            }
            cairo_fill(cr);
        }
    }

    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_waveform_plate -------------------------------

void GuiPaintHandler::paint_waveform_plate(cairo_t* cr, const GuiRect& area) {
    // wf_cache.surface is produced by one of two paths, both of which
    // leave this paint path blit-only:
    //   1. Worker full render — maybe_enqueue_waveform_render
    //      dispatches a full-window render on GuiWaveformWorker,
    //      which swaps into wf_cache.surface on completion. Fires
    //      for UNDRIVEN changes — resize, the launch load, follow-scroll
    //      during playback — and as the on_tick backstop for any residual
    //      fingerprint drift (a warp_frame_map hash included). Map EDITS
    //      themselves are user-driven and take path 2.
    //   2. Synchronous full render — force_synchronous_waveform_rebuild
    //      renders the full window inline on the GUI thread for every
    //      USER-DRIVEN viewport change: zoom, center-on-playhead, the
    //      one-shot jumps, and panning/scrolling (which had its own
    //      incremental shift-and-strip path until 2026-07-26 — retired so
    //      a moving plate and a resting one come off one route).
    // The paint path is blit-only — it draws whatever pixels the
    // live surface currently holds. For worker-path renders that may
    // be a one- or two-frame-old viewport during the worker-rebuild
    // window; the synchronous path updates the plate in the same
    // frame, so it has no such lag. The flag layer closes
    // any mismatch by layering flags onto a surface keyed
    // off the same displayed-viewport.
    //
    // If wf_cache.surface is null (initial load, before the first
    // worker completion), the blit is skipped and the canvas
    // ground fill shows through. The user-visible difference is one
    // extra paint frame of empty canvas between load and first
    // waveform display, masked by the existing load-time progress
    // bar.
    //
    // BLIT-ONLY, AND NOTHING RECOLORS IT AFTER: the out-of-trim dim (a second
    // ink color masked through the plate's own alpha) is retired
    // with the opaque recolor model (architect 2026-07-26). The trim bridge bar
    // is the whole inside-the-window signal now, and the plate's pixels are
    // exactly what the renderer wrote, composited once over whichever ground —
    // kWaveformCanvas, or a kWaveformRegionCanvas recolor — the pass before
    // this one left. The aliased renderer's alpha is binary (the antialiased
    // plate is deleted), so ink over a highlighted span is identical to ink
    // over plain canvas everywhere.
    //
    // The clip is the CONTENT band, not the full area: the area's top and
    // bottom rows are render_canvas's 2px black border (row 6) and no
    // band-filling pass may cover them. (The plate's own inset band leaves those rows transparent
    // anyway, so this is the structural statement of the rule rather than a
    // pixel change.)
    if (wf_cache.surface) {
        const GuiRect content = waveform_content_rect(area);
        cairo_save(cr);
        cairo_rectangle(cr, content.x, content.y, content.w, content.h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, wf_cache.surface,
                                 area.x, area.y);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

// -- GuiPaintHandler::plate_viewport_basis / region_columns ----------

// See the declaration comment in paint_handler.h: the fp-recipe basis locked to
// the blitted plate while the worker rebuilds, with the live spp fallback when
// no plate has published a span yet.
GuiPaintHandler::PlateViewportBasis
GuiPaintHandler::plate_viewport_basis() const {
    PlateViewportBasis b;
    b.spp = wf_cache.fp_area_w > 0
        ? static_cast<double>(wf_cache.fp_vp_end - wf_cache.fp_vp_start) /
          static_cast<double>(wf_cache.fp_area_w)
        : current_samples_per_pixel(app, audio);
    b.vp_start = static_cast<double>(wf_cache.fp_vp_start);
    return b;
}

GuiPaintHandler::RegionColumns
GuiPaintHandler::region_columns(const PlateViewportBasis& basis) const {
    const int64_t lo = std::min(app.region.a_frame, app.region.b_frame);
    const int64_t hi = std::max(app.region.a_frame, app.region.b_frame);
    RegionColumns c;
    // The one column rounding (displayed_column_at, warp_frame_map_view.h), on
    // the PLATE basis the caller passed — the endpoints already live in the
    // active display domain, so no warp map is walked.
    c.lo_col = displayed_column_at(static_cast<double>(lo),
                                   basis.vp_start, basis.spp);
    c.hi_col = displayed_column_at(static_cast<double>(hi),
                                   basis.vp_start, basis.spp);
    return c;
}

// -- GuiPaintHandler::paint_region_ground --------------------------------

// THE REGION HIGHLIGHT IS A GROUND RECOLOR (the Ableton model, architect
// 2026-07-26): the span's CANVAS becomes the opaque kWaveformRegionCanvas over
// the full
// content height. Called from on_redraw after render_canvas and BEFORE
// paint_waveform_plate, so the ARGB32 plate composites over the recolored
// ground — and since the aliased renderer's alpha is BINARY (the antialiased
// plate is deleted; docs/engineering/waveform_antialiasing_retired.md), the
// ink over a highlighted span is bit-identical to ink over plain canvas
// everywhere, and only the ground carries the highlight. The retired form was
// a translucent wash painted OVER the plate, which lifted the ink itself —
// exactly what the recolor model rejects.
// Session-only, nothing persisted; not part of the plate/flag caches — a direct
// per-frame pass, so no cache is involved. AA off, integer edges. The fill is
// clipped to the CONTENT band so it cannot cover the area's border rows.
void GuiPaintHandler::paint_region_ground(cairo_t* cr, const GuiRect& area) {
    if (!app.region.active) return;
    if (area.w <= 0 || area.h <= 0) return;

    // Displayed-viewport recipe: the same fp_* fingerprint paint_playheads and
    // the overlay band use, so the ground stays locked to the blitted plate
    // while the worker rebuilds against a viewport change.
    const PlateViewportBasis basis = plate_viewport_basis();
    if (basis.spp <= 0.0) return;

    // Endpoints normalized to [lo, hi] and mapped to columns via the shared
    // region_columns owner (the plain viewport transform — the endpoints already
    // live in the displayed domain, so no warp map is walked, unlike the phase
    // reset overlay whose source-frame marker crosses to target first).
    const RegionColumns cols = region_columns(basis);

    double x0 = static_cast<double>(area.x + cols.lo_col);
    double x1 = static_cast<double>(area.x + cols.hi_col);
    // Clamp to the visible strip; a span wholly offscreen paints nothing.
    x0 = std::max(x0, static_cast<double>(area.x));
    x1 = std::min(x1, static_cast<double>(area.x + area.w));
    if (x1 <= x0) return;

    const GuiRect content = waveform_content_rect(area);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, kWaveformRegionCanvas.r, kWaveformRegionCanvas.g,
                         kWaveformRegionCanvas.b);
    cairo_rectangle(cr, x0, static_cast<double>(content.y),
                    x1 - x0, static_cast<double>(content.h));
    cairo_fill(cr);
    cairo_restore(cr);
}

// -- GuiPaintHandler::phase_reset_overlay_band / its ring pass ------------

// Paint-only overlay width: two synthesis hops of target/output time, the
// scale of the reset's local effect — the stretch of output immediately
// following the reset over which the re-seeded phase takes hold before
// normal propagation resumes. A pure authoring aid with no engine meaning,
// consumed nowhere else in the product.
constexpr double kPhaseResetOverlayHops = 2.0;
const int64_t kPhaseResetOverlaySamples = static_cast<int64_t>(
    std::nearbyint(kPhaseResetOverlayHops * static_cast<double>(kRs)));

// Resolves the band shown ahead of the focused phase reset marker: a
// fixed-width forward span in target time starting at the marker's stem
// column, showing the stretch of output immediately following the reset over
// which the re-seeded phase takes hold. Paint-only: no persisted state,
// nothing on disk, no settings key, no undo interaction.
//
// THE GEOMETRY AND VISIBILITY OWNER, kept SEPARATE from its one consumer
// (paint_phase_reset_overlay_ring) rather than folded into it. It carries every
// visibility gate — view, focus, the multi-select suppression, the eligible-marker
// resolve, the sub-pixel and offscreen refusals — plus the clipped span, and
// Selection::phase_overlay_subject MIRRORS its selection-state gates — MINUS the
// geometry ones, which are not selection state — to decide when a subject change
// needs waveform damage and, since 2026-07-28, whether Space auditions the
// lead-in. One rule, so it stays one function; that mirror's own reader
// inventory lives at its declaration in selection.h. (It served a second pass,
// an opaque ground recolor under the plate, until the ring became the overlay's
// whole visual — architect 2026-07-27.)
//
// Painted in TARGET view, never source view, and this is a
// phase-reset-only surface with no warp sibling (naming-symmetry asymmetry,
// recorded here per CLAUDE.md). The span is a fixed target/output-domain
// width, so it is constant in target time. Source view would show a
// map-dependent, varying width — misrepresenting a constant span — so the
// overlay is not drawn there. The reset's local take-hold stretch is a
// phase-reset-only concept, so there is nothing on the warp axis to mirror.
GuiPaintHandler::PhaseResetOverlayBand
GuiPaintHandler::phase_reset_overlay_band(const GuiRect& area) const {
    PhaseResetOverlayBand out;
    // Visibility: always-on for the focused enabled marker while the global
    // W/P mode is on P, in target view; never source view. Everything
    // downstream is domain-agnostic.
    if (app.active_markers_view != 'P') return out;
    if (area.w <= 0 || area.h <= 0) return out;
    // The multi-select suppression (architect 2026-07-23): the overlay depicts ONE
    // focused reset's lead-in, a single-focus authoring aid, so a MULTI-select
    // (2+ members) suppresses it — the state is about a span of markers rather
    // than a single focus, and the overlay would clutter. (A singleton or empty
    // selection shows it as before; the multi-select builders all damage the
    // waveform, so the overlay's appear/disappear rides their damage.)
    //
    // NO REGION GATE HERE, and none is needed — THE DERIVATION, recorded once at
    // this site with Selection::phase_overlay_subject's mirror pointing here:
    // every region former DESELECTS at press (the plain upper-half waveform drag
    // and the shift waveform press are the only two — the inventory is at
    // RegionState, app_state.h), so a region rests ONLY beside an EMPTY
    // selection, and an empty selection carries no focused reset for this band
    // to annotate. A region and a subject cannot coexist, so no region test
    // could ever decide this band's visibility.
    if (app.selected_markers.size() >= 2) return out;

    // Paint sample: the exact expression render.cpp's file-local
    // frame_to_paint_sample uses, so marker and overlay can never disagree.
    double ms;
    {
        if (app.active_audio_view != 'T') return out;

        const auto& markers = app.phaseresetmarkers.markers();
        const int idx = app.last_selected_marker;
        if (idx < 0 || idx >= static_cast<int>(markers.size())) return out;
        const auto& marker = markers[idx];
        // Skip a disabled focused reset — a disabled phase reset paints no
        // overlay, reading its `disabled` bool directly (phase resets carry no
        // label cascade).
        if (marker.disabled) return out;

        // Map selection: the DISPLAYED paint basis (displayed_or_live_target_map
        // — the SAME map the flags, stems, drag overlay and riding playhead read,
        // falling back to the live cache when cold), so the overlay stays locked
        // to the reset it annotates even inside a worker publish window where the
        // displayed map lags the live cache. No map means identity (matching the
        // stem renderer's fallback). We are already known to be in target view.
        const std::vector<WarpFrameMapSegment>* tmap = nullptr;
        const std::vector<WarpFrameMapSegment>& m =
            displayed_or_live_target_map(app, audio);
        if (!m.empty()) tmap = &m;

        // Effective time: during a phase-reset-mode drag, read the focused
        // marker's proposed time through the DragOverlay (same construction
        // as hit_test_flag). A warp-mode drag's indices refer to the warp
        // list, so guard on drag_mode 'P'; otherwise use the live store's
        // time.
        double eff_time = marker.time_frame;
        if (app.drag.active && app.drag.drag_mode == 'P') {
            DragOverlay ov;
            ov.indices = &app.drag.dragging_markers;
            ov.times   = &app.drag.moveable_times;
            eff_time = ov.effective_time(idx, marker.time_frame);
        }

        if (tmap && !tmap->empty()) {
            const size_t src_frame =
                static_cast<size_t>(std::nearbyint(eff_time));
            ms = std::nearbyint(map_source_to_target(src_frame, *tmap));
        } else {
            ms = std::nearbyint(eff_time);
        }
    }

    // Displayed-viewport recipe: same as paint_playheads, so the overlay
    // stays locked to the blitted plate while the worker
    // rebuilds against a viewport change.
    const PlateViewportBasis basis = plate_viewport_basis();
    const double spp = basis.spp;
    if (spp <= 0.0) return out;
    const double vp_start = basis.vp_start;

    // Columns: left_col takes the stem renderer's own placement — the shared
    // displayed_column_at rounding (warp_frame_map_view.h) — so the overlay's
    // left edge stays on the marker's column.
    // right_col is a fixed whole-pixel offset ahead of it, so the far edge
    // tracks the marker in lockstep instead of wobbling by independent
    // per-endpoint rounding. width_px is the overlay span banker's-rounded to
    // whole pixels — an approximate but rigid forward extent, which beats an
    // exact but jittering one (the span is an authoring aid, not an engine
    // point).
    const int left_col = displayed_column_at(ms, vp_start, spp);
    const int width_px = static_cast<int>(std::nearbyint(
        static_cast<double>(kPhaseResetOverlaySamples) / spp));

    // Too-zoomed-out: if the fixed forward extent rounds below one pixel,
    // paint nothing at all — no sliver, no clamped minimum.
    if (width_px < 1) return out;

    const int right_col = left_col + width_px;

    // The band spans columns [left_col, right_col): the stem's own column
    // (left_col) sits inside it, and the stems paint after both of the band's
    // passes, so the stem stays crisp on top of the left seam.
    double x0 = static_cast<double>(area.x + left_col);
    double x1 = static_cast<double>(area.x + right_col);

    // Horizontal clip to [area.x, area.x + area.w); the band shows whenever the
    // intersection is non-empty even if the stem column is off-screen left
    // (the tail can be visible while the stem is not).
    x0 = std::max(x0, static_cast<double>(area.x));
    x1 = std::min(x1, static_cast<double>(area.x + area.w));
    if (x1 <= x0) return out;

    out.valid = true;
    out.x0    = x0;
    out.x1    = x1;
    return out;
}

// THE OVERLAY RING — the phase-reset overlay's WHOLE visual (architect
// 2026-07-27): the band's 1px opaque border in the phase-reset stem's own
// purple (2026-08-01) and nothing else,
// painted AFTER the plate. It is a BOUNDARY LINE, like the playheads and the
// stems, so an opaque line crossing waveform ink is correct and intended, and
// with no fill inside it the band now READS as the two edges of a span rather
// than as a tinted region.
//
// THE HORIZONTALS RIDE THE BORDER'S OUTERMOST ROWS (architect 2026-08-01,
// GIMP-verified at the row-6 live look). They sat on the CONTENT band, one row
// inside each 2px border — which put them where the marker and playhead stems
// cross, so the ring read as broken by every stem. The top run moved UP 2px onto
// the top border's FIRST row and the bottom run DOWN 2px onto the bottom
// border's LAST row, and the verticals extend to meet them. This pass is
// therefore the ONE band-filling pass that deliberately does NOT clip to
// waveform_content_rect (the inventory at that helper says so): the ring is not
// content, it is a frame drawn ON the frame.
//
// A vertical side is drawn only where the band's own edge
// is the true edge — both x0 and x1 come back already clipped to the area, so a
// band running past a viewport edge draws its border there too; that is the
// same flush-to-the-edge reading the trim bridge's clipped fill has, and the
// band is an aid rather than a hit target, so no sentinel machinery is needed.
void GuiPaintHandler::paint_phase_reset_overlay_ring(
    cairo_t* cr, const GuiRect& area) {
    const PhaseResetOverlayBand band = phase_reset_overlay_band(area);
    if (!band.valid) return;

    const double w = band.x1 - band.x0;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // THE RING IS THE STEM'S PURPLE (architect 2026-08-01): kMarkerFlagFill
    // #9b59b6, the phase-reset class's own UNSELECTED fill — "they're one unit",
    // the ring and the stem of the reset it annotates. Hard-coded per the
    // redesign's colour ruling, superseding the tunable grey #7f8c8d this drew
    // in, whose ONE paint site this was — which is what left its config key
    // unread and, a day later, deleted with the whole tunable palette. It reads
    // the same constant the
    // stems resolve to rather than a copy of its value, so the two cannot drift.
    cairo_set_source_rgb(cr, kMarkerFlagFill.r, kMarkerFlagFill.g,
                         kMarkerFlagFill.b);
    // THE FULL AREA, not the content band: the top run lands on row area.y (the
    // top border's first row) and the bottom on row area.y + area.h - 1 (the
    // bottom border's last), with the verticals spanning every row between them.
    const double y0 = static_cast<double>(area.y);
    const double h  = static_cast<double>(area.h);
    cairo_rectangle(cr, band.x0, y0, w, 1.0);            // top
    cairo_rectangle(cr, band.x0, y0 + h - 1.0, w, 1.0);  // bottom
    cairo_rectangle(cr, band.x0, y0, 1.0, h);            // left
    cairo_rectangle(cr, band.x1 - 1.0, y0, 1.0, h);      // right
    cairo_fill(cr);
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_trim -----------------------------------------

// The LIVE trim pass: every trim pixel — the lane ground, the window bar, the
// two endcaps and the midpoint mark — paints here per frame, entirely inside
// the trim lane, which no later pass paints on; its slot is step 5 of the
// paint-order block in on_redraw (the one authoritative sequence).
//
// BASIS: the FREE item-geometry owners — item_viewport_basis(app, audio)
// and displayed_or_live_target_map(app, audio) — feeding the shared geometry
// owners displayed_trim_ms / trim_bound_column / trim_bridge_gap /
// trim_endcap_rect inside the two renderers, so paint stays column-coherent with
// hit_test_trim_endcap / route_trim_bar_press, which read exactly that basis
// (paint == hit by shared owners). The ONE state where paint and hit describe
// different bounds is the `h` HISTORY VIEW, where this pass substitutes the
// viewed commit's diff span for the authored pair (the block below owns that
// ruling): coherence is moot there because the view consumes every press that
// reaches either hit test, so no gesture can act on the substituted columns.
// Deliberately NOT the member
// GuiPaintHandler::plate_viewport_basis(): that is the PLATE-fingerprint
// basis for plate-registered overlays, and the two differ inside the accepted
// resize item-only-promotion window — trim must ride the ITEM basis the
// endcaps' hit rects resolve on. The renderers' column math therefore divides the
// basis span by basis.area_w (the width the committed items were mapped
// against), which is why the waveform rect handed to them carries that width.
//
// COLD STATES (nothing promoted yet — first paint after a load or
// load-in-place, the view
// toggle): the free accessors fall back to the LIVE viewport/map, so trim
// paints on the pre-first-publish frame too. Small intentional behavior
// change: the retired cached path SKIPPED its null cache surfaces there, so
// trim was absent for that one frame — the live pass paints it (an
// improvement, not byte-identical cold behavior).
//
// COORDINATES: both renderers take SCREEN-space rects (the top strip anchors
// at (0,0), so its screen and former cache-local coords coincide; the waveform
// rect carries its real screen x/y).
void GuiPaintHandler::paint_trim(cairo_t* cr, const GuiRect& area,
                                 const GuiRect& top_strip) {
    // No trim gate: the window is ALWAYS set (2026-07-30), so the bar and its
    // endcaps simply always paint — at the full window the caps rest on the
    // song edges and the bar spans the whole lane between them.
    if (area.w <= 0 || area.h <= 0) return;
    if (top_strip.w <= 0 || top_strip.h <= 0) return;

    // The ITEM basis (free owner; the member plate_viewport_basis is the other
    // epoch — see the header comment above).
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.area_w <= 0 || basis.spp <= 0.0) return;

    // The item pixels' map: empty (identity) in source view, the committed
    // displayed map (live fallback cold) in target view — exactly
    // hit_test_trim_endcap's selection.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* map_arg =
        dmap.empty() ? nullptr : &dmap;

    // THE SOURCE-FRAME PAIR THE BAR DISPLAYS. Ordinarily the authored trim
    // window; while the `h` HISTORY VIEW stands, the VIEWED COMMIT'S DIFF SPAN
    // instead (architect 2026-08-05 — the view is a viewer, and the bar is the
    // one lane wide enough to say at a glance where in the piece a checkpoint's
    // changes lie). An empty delta shows the full window, the span's own
    // "nothing to frame" answer, which is also what the authored trim rests at
    // by default.
    //
    // DISPLAY-ONLY, AND ONLY HERE: app.trim is not read, not written and not
    // shadowed — this is one substitution at the one paint site, above the
    // displayed_trim_ms mapping below, so the substituted frames ride the target
    // view's display map exactly as the authored pair does. Nothing consumes the
    // painted pair for BEHAVIOR while the view stands: the trim bar's three
    // press routes are consumed by the pointer allowlist
    // (handle_history_mode_press), which is also why pointer_cursor_kind empties
    // the band's cues in the mode, and the endcap / bridge hit tests are
    // reachable only from those presses. Its span-framing DOUBLE-CLICK is not
    // consumed but REPLACED there, framing the diff span — and it reads the
    // delta directly, never this painted pair, so it adds no consumer either.
    //
    // THE ONE RECORDED INCOHERENCE: playback still uses the REAL trim range, so
    // an audition inside the view starts and stops at the authored window while
    // the bar above it shows the diff span. Deliberate — the mode changes no
    // authored state and no playable range, and the two owners of that range
    // (playback and navigation) are not display sites.
    int64_t bar_begin_frame = app.trim.begin_frame;
    int64_t bar_end_frame   = app.trim.end_frame;
    if (app.history_mode.active) {
        const GuiHistoryCommitDelta* d = app.history_mode.session.delta_at(
            app.history_mode.index, app.history_mode.compare);
        int64_t lo = 0, hi = 0;
        if (d && d->frame_span(lo, hi)) {
            bar_begin_frame = lo;
            bar_end_frame   = hi;
        } else {
            const TrimState full = full_trim_window(audio.total_frames());
            bar_begin_frame = full.begin_frame;
            bar_end_frame   = full.end_frame;
        }
    }

    // Per-bound displayed-domain positions through the shared mapping owner
    // (displayed_trim_ms returns an integral-valued double; the int64 round
    // trip through TrimRange is exact, so trim_bound_column sees the same
    // value the hit sites pass). Both bounds are always meaningful.
    TrimRange trim{
        static_cast<int64_t>(displayed_trim_ms(bar_begin_frame, map_arg)),
        static_cast<int64_t>(displayed_trim_ms(bar_end_frame, map_arg))};

    // Waveform rect for the renderers: real screen origin/height, width =
    // basis.area_w (the committed item width — the column-mapping denominator
    // and the [0, wave_w) painter clip, keeping paint == hit through the
    // accepted resize window; equal to waveform_area(app).w at rest).
    const GuiRect wave_rect{area.x, area.y, basis.area_w, area.h};

    // ONE HALF ONLY since 2026-08-01: the waveform stem pass is deleted, so
    // this is the strip's bar + endcaps (with the side-aware offscreen
    // sentinels and the effective-width clip inside render_trim_flags).
    //
    // The trim bar lane's y-band is THREADED IN as top_trim_row_area(app)
    // rather than re-derived inside the painter: this is the same accessor
    // hit_test_trim_endcap's y-gate and route_trim_bar_press' bridge y-gate
    // read, so the painted band and the clickable band have ONE owner and
    // cannot drift if the lanes above the trim bar ever change.
    // NO WAVEFORM STEMS (architect 2026-08-01): the bar and its two endcaps are
    // the trim window's WHOLE display. render_trim_stems drew a 1px grey
    // vertical down the waveform at each bound; the redesigned lane says the
    // window where the window is, and a pair of full-height lines competing with
    // the marker stems said it a second time in the same pixels.
    render_trim_flags(cr, top_strip, top_trim_row_area(app), wave_rect,
                      basis.vp_start_frame, basis.vp_end_frame, trim);
}

// -- GuiPaintHandler::paint_marker_stems ---------------------------------

// EVERY ENABLED MARKER STEMS, ALWAYS (row 5, architect): the per-frame waveform
// overlay that replaced the singleton selected-marker stem. The full contract —
// what stems, in what colour, and why selection changes none of it — is at the
// declaration.
//
// It reads the marker painter's stash (app.marker_stems) instead of walking a
// store: the stem stands on its flag box's LEFT EDGE, and that column was
// already resolved by the pass that painted the box, on the displayed basis
// those pixels were laid out against. So the DragOverlay substitution, the
// source->target map walk, the per-marker cull and the colour ladder all happen
// exactly once, in the painter, and a stem can never land a pixel away from its
// own flag. Disabled markers are simply absent from the stash.
//
// The stem spans the waveform area top to bottom — the flag's bottom edge IS
// the waveform top (the marker lane rests flush on it), so the two meet with no
// seam and no strip-crossing segment to draw. It runs OVER the waveform's own
// borders when row 6 adds them, the same z-intent the playhead stem records: a
// stem is a boundary line, not something the borders clip.
void GuiPaintHandler::paint_marker_stems(cairo_t* cr, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    if (app.marker_stems.empty()) return;

    // THE INVALID-COMMIT RED FLASH REACHES THE STEM (architect 2026-08-01): a
    // flashing flag and its stem read as one object, exactly as a coincident
    // marker's red pair already does (#da4453 either way — the flash borrows the
    // red CLASS, it does not invent a colour).
    //
    // IT IS A PAINT-TIME OVERRIDE, mirroring how the flash face itself is stored
    // and painted: render_flag_editor_box resolves the marker's ordinary face
    // through the one class ladder and then overrides the pair when `ed.red` is
    // set, per frame, out of any cache. This is that override on the stem's own
    // live pass — the stash keeps publishing the marker's real class, so nothing
    // has to be un-published when the flash clears and the flag cache needs no
    // fingerprint for a transient. A DISABLED marker has no stash entry and
    // therefore no flashing stem, which is the same absence its flag's missing
    // stem always was.
    //
    // The 'W' test is the guard the index needs, not decoration: the flag editor
    // is a warp-column surface by its open gates, and stash indices belong to
    // whichever column is active, so without it a P-view stash row could match a
    // warp target's index and redden an unrelated phase reset.
    const text_editor::State& ed = app.top_flag_editor;
    const int flash_idx =
        (text_editor::is_active(ed) &&
         ed.kind == text_editor::Kind::FlagPayload && ed.red &&
         app.active_markers_view == 'W')
            ? ed.target
            : -1;

    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);
    const double y0 = static_cast<double>(area.y);
    const double y1 = static_cast<double>(area.y + area.h);
    for (const MarkerStem& stem : app.marker_stems) {
        // Column-gate exactly like render_playhead's line does, so a stem whose
        // flag hangs into view from the left (the boxes run rightward) never
        // leaks its column into the chrome beside the waveform.
        const double col = stem.x - static_cast<double>(area.x);
        if (col < 0.0 || col >= static_cast<double>(area.w)) continue;
        const double x_px = static_cast<double>(area.x) + col + 0.5;
        const GuiColor c = (stem.marker_index == flash_idx) ? kMarkerStemRed
                                                            : stem.color;
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        cairo_move_to(cr, x_px, y0);
        cairo_line_to(cr, x_px, y1);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_strip_drag_anchor ----------------------------

// Paints the strip-drag anchor stem (the Ableton pivot affordance) at the
// drag's current anchor column, full waveform height. Live only mid-gesture:
// gated on the drag being active AND past the moved threshold, so a bare press
// shows nothing and it vanishes the moment the drag ends (release / button loss /
// the force-end finalizer clear strip_drag before the next paint; Esc no longer
// ends a gesture at all). The anchor column is recomputed
// each frame from the persisted anchor_sample against the DISPLAYED viewport
// (wf_cache.fp_*), the same basis paint_region_ground and paint_playheads use,
// so the stem stays locked to the blitted plate while the worker rebuilds. The
// anchor lives in the active display domain (viewport_start + col*spp), so no
// warp map is walked. render_strip_anchor_stem clamps the column to the visible
// edges — an edge-pinned anchor draws the clamp itself.
void GuiPaintHandler::paint_strip_drag_anchor(cairo_t* cr, const GuiRect& area) {
    if (!app.strip_drag.active || !app.strip_drag.moved) return;
    if (area.w <= 0 || area.h <= 0) return;

    const PlateViewportBasis basis = plate_viewport_basis();
    const double spp = basis.spp;
    if (spp <= 0.0) return;
    const double vp_start = basis.vp_start;
    // The one column rounding (displayed_column_at, warp_frame_map_view.h), on
    // the PLATE basis hoisted above.
    const int col =
        displayed_column_at(app.strip_drag.anchor_sample, vp_start, spp);
    render_strip_anchor_stem(cr, area, col);
}

// -- GuiPaintHandler::playhead_stem_suppressed ---------------------------

// THE PLAYHEAD'S STEM SUPPRESSES WHERE A MARKER'S STEM ALREADY STANDS
// (architect 2026-08-01). This REINSTATES 035e669's model — "the cursor playhead
// is conceptually COINCIDENT with the selection and fully hidden behind the
// marker — line on the stem, triangle behind the flag; suppression as
// implementation, not absence" — which the 2026-07-30 always-paints ruling
// deleted. It is the coincident case ALONE that the always-paints clause loses:
// the playhead still paints everywhere else, unconditionally, and the HEAD
// paints even here (the architect expects it partly visible behind a coincident
// flag; a ±1 column on a 19px head is invisible, which is exactly what a 1px
// stem beside another 1px stem is not).
//
// WHY IT IS PRINCIPLED AGAIN, and why it was not on 2026-07-30: in the OLD
// visual model only a selected SINGLETON stemmed, so suppressing the playhead
// over an unstemmed marker would have left the column blank — absence, not
// hiding. Row 5 gives EVERY ENABLED marker an always-on stem, so a coincident
// marker's own stem is a real, always-present line for the playhead to hide
// behind, and the deleted model becomes true again.
//
// WHAT IT FIXES: the two stems are derived through DIFFERENT column arithmetic
// — marker stems publish from the flag-cache rebuild (the flag layout's own
// column resolution; its spp rides the plate's published width since the
// 2026-08-01 resize-window fix, see waveform_cache.cpp's wave_w read), the
// playhead from playhead_pixel_x against plate_viewport_basis — so at some
// zoom rests a marker and a playhead standing on the SAME frame round to columns
// one pixel apart, and nudging or dragging the marker made the pair flicker
// between one line and two. Suppression removes the second line rather than
// trying to make two roundings agree.
//
// A STATE COMPARE, NEVER A PIXEL ONE: the qualifying test is the LAND's own
// exact-int64 formula — clamp_playhead_to_live_domain(source_frame_to_active_-
// domain(time_frame)) == playhead_cursor_sample — reused verbatim from
// auto_select_marker_at_playhead (input_pointer.cpp), which owns the coincidence
// family's question "is the playhead standing on a marker". Comparing columns
// instead would ask the two roundings to agree, which is the defect.
//
// THE WALK IS OVER THE PAINTED STEMS (app.marker_stems), not over a store, and
// that is what makes "a stem is standing there" the literal predicate: the stash
// holds one entry per ENABLED, VISIBLE marker (a disabled marker publishes none,
// a culled one publishes none), so a marker with no stem can never suppress the
// playhead's — the blank-column failure mode is structurally unreachable rather
// than argued. It is also bounded by the visible marker count.
//
// TWO WAYS A STEM QUALIFIES:
//   * THE DRAG RIDE. While a marker drag tows the playhead (apply_drag_motion
//     writes the cursor to the proposal's own active-domain position every
//     motion event, and commit_drag lands it on the committed frame), the
//     dragged marker's stem and the playhead ARE one object by construction —
//     but mid-motion the proposal is a fractional double and the store still
//     holds the pre-drag frame, so the exact compare below cannot see it. The
//     drag's own fact is what the arm reads instead, and it is the same fact the
//     overlay paints the flag and the stash publishes the stem with: the
//     marker's index appearing in the DragOverlay.
//   * EXACT COINCIDENCE AT REST, the compare above — which is what the keyboard
//     nudge leaves behind (the nudges re-land the playhead through
//     land_playhead_on_marker, whose write IS this formula, so a nudged marker
//     rests exactly coincident) and what every marker click, Tab jump and
//     coincidence auto-select leave behind too.
//
// SCOPE NOTE, deliberately WIDER than "the focused marker": any marker with a
// painted stem suppresses, focused or not. The artifact is the same ±1 wherever
// the playhead stands on a marker, the display is that marker's stem either way,
// and reading the FOCUS here would make a waveform pixel depend on the
// SELECTION — the exact dependency row 5 deleted Selection::stem_subject /
// damage_stem_on_subject_change for (selection.cpp), whose mutators damage the
// top strip and not the waveform. Keyed on the playhead and the stash instead,
// every input this reads is already damaged by its own writer.
bool GuiPaintHandler::playhead_stem_suppressed() const {
    if (app.marker_stems.empty()) return false;

    // The dragged marker, or -1. The view compare is a statement, not a repair:
    // the drag-modal gate swallows `p`, so a live drag's mode is always the
    // active column — the stash indices this compares against are that column's.
    const int dragged =
        (app.drag.active && app.drag.drag_mode == app.active_markers_view &&
         !app.drag.dragging_markers.empty())
            ? app.drag.dragging_markers[0]
            : -1;

    const auto coincident = [&](int64_t source_frame) {
        return clamp_playhead_to_live_domain(
                   source_frame_to_active_domain(app, audio, source_frame),
                   app, audio) == app.playhead_cursor_sample;
    };

    const auto& wv = app.warpmarkers.markers();
    const auto& pv = app.phaseresetmarkers.markers();
    const bool phase = (app.active_markers_view == 'P');
    for (const MarkerStem& stem : app.marker_stems) {
        const int i = stem.marker_index;
        if (i == dragged) return true;
        if (i < 0) continue;
        // Index-guarded against the store the stash was published from having
        // shrunk since (an undo under a stale stash): a missing row simply does
        // not suppress.
        if (phase) {
            if (i < static_cast<int>(pv.size()) && coincident(pv[i].time_frame))
                return true;
        } else {
            if (i < static_cast<int>(wv.size()) && coincident(wv[i].time_frame))
                return true;
        }
    }
    return false;
}

// -- GuiPaintHandler::paint_playheads ------------------------------------

void GuiPaintHandler::paint_playheads(cairo_t* cr, const GuiRect& area) {
    // Use the displayed viewport AND its samples-per-pixel
    // (wf_cache.fp_vp_start, derived spp) so the cursor stays in
    // lockstep with the cached waveform / stem / flag layers during
    // the 1-2 paint frames while the worker rebuilds against a
    // viewport change. See declaration comment in app_state.h.
    const PlateViewportBasis basis = plate_viewport_basis();
    const double disp_spp = basis.spp;
    const double px_x = playhead_pixel_x(app, wf_cache.fp_vp_start, disp_spp);
    // ROW 5 RETIRED THE TRIANGLE and this pass draws NOTHING in a strip lane
    // any more: the tip-down triangle died with its lane, its successor is the
    // aliased head on the MARKER lane's bottom rows, and paint_ruler_row owns it
    // despite the lane (it needs the tick columns for the pre-blended
    // crossing). So this pass is the WAVEFORM segment of the cursor's
    // stem, nothing else — and since 2026-08-02 render_playhead draws a line and
    // only a line: the dead triangle branch is deleted, and with it the lane
    // rect this call used to thread through to it.

    // The cursor paints UNDER the marker flags (the Z-ORDER FLIP, architect
    // 2026-07-23 — see the paint-order block in on_redraw): its line passes
    // beneath a marker flag sharing its column, so a cursor resting on a marker
    // sits hidden behind that marker's flag, and it passes under the marker
    // STEMS below it in the waveform too. Gated on the waveform OR the top strip
    // being exposed: the cursor's HEAD lives in the strip (paint_ruler_row) and
    // this stem in the waveform, and the two halves of one line repaint
    // together whatever the damage shape — the outer Cairo clip bounds the
    // actual work.
    //
    // THE SCANNER LEFT THIS PASS (architect 2026-08-01) — it is paint_scanner
    // now, invoked after the marker stems, so the moving line crosses a marker's
    // stem instead of blinking out behind it. This pass is the CURSOR alone, and
    // the two are no longer ordered against each other here: the scanner is over
    // everything in the waveform area while it runs.

    // THE CURSOR PLAYHEAD ALWAYS PAINTS (architect 2026-07-30): ONE playhead
    // form, drawn at the resting cursor column whatever the selection and
    // whatever the region are doing — a 1px line painted solid straight over the
    // plate ink. WITH ONE EXCEPTION SINCE 2026-08-01, and exactly one: where a
    // MARKER'S stem already stands on the playhead's frame, the playhead's STEM
    // does not paint and that marker's stem is the display (035e669's
    // hidden-behind-the-marker model, reinstated — the whole ruling is at
    // playhead_stem_suppressed). The clause above still holds everywhere else,
    // and the HEAD paints in the suppressed case too (paint_ruler_row).
    //
    // The three-way chain that used to live here is gone with the SPAN FORM: the
    // region is no longer a playhead at all (it is TRIM SCRATCH — a ground recolor
    // formed by the plain upper-half waveform drag and the shift waveform press,
    // previewed by the scrub press (either entry), consumed by `x`), so it dissolves
    // nothing and suppresses nothing, and the split half-triangle renderer is
    // deleted outright. The non-empty-selection suppression is
    // gone too: a cursor resting ON the focused marker is simply hidden behind
    // that marker's flag by the z-order flip, which is what the old else-arm was
    // spelling out by not painting — and when the arrows move the focused marker
    // the cursor rides along VISIBLY, which is the lane model's honest reading.
    // The region ground still paints under the plate (paint_region_ground); the
    // cursor line crosses it exactly as it crosses waveform ink.
    // THE TRIANGLE IS OFF EVERYWHERE (row 5): the cursor's tip-down triangle
    // retired with the triangle lane, and its successor — the aliased head on
    // the MARKER lane's bottom rows — is painted by the ruler pass anyway, that pass
    // owning the tick columns the head's pre-blended crossing needs. So this call is the stem's WAVEFORM
    // segment; the ruler pass draws the head, its tip on the waveform boundary
    // where this segment begins, and the two make one unbroken line.
    // THE STEM IS kPlayheadStem NOW (#fcfcfc), superseding the old cursor line's
    // color at this surface: the head above it is the playhead's identity, and
    // the stem is that head's line continued down through the waveform.
    //
    // Z-INTENT: this segment goes down UNDER the marker stems painted after it
    // and under the flag boxes blitted after those, and the head above it (the
    // ruler pass) likewise goes under the flags. That is the HIDDEN-BY-MARKER
    // model translated — a flag sharing the cursor's column hides it, exactly as
    // flags painted over the old triangle — and it is also why the stem is drawn
    // to run OVER the waveform's own borders: the stem is a boundary line like
    // the marker stems beside it, not a thing the borders clip.
    if (!playhead_stem_suppressed()) {
        render_playhead(cr, area, px_x, kPlayheadStem);
    }
}

// -- GuiPaintHandler::paint_scanner --------------------------------------

// THE SCANNER PASSES OVER THE MARKER STEMS (architect 2026-08-01, at the row-6
// live playback look). It was drawn inside paint_playheads, which runs BEFORE
// paint_marker_stems, so every always-on marker stem overpainted the moving
// line's column: at our marker density the scanner blinked out repeatedly as it
// crossed the song. Its own pass, invoked after the stems, is the fix — and it
// is the SCANNER ALONE that moved. The resting cursor keeps painting under the
// stems and under the flags (the hidden-by-marker model, which is about a
// cursor COINCIDENT with a marker); a scanner sweeping past one is not that
// case, and a line that vanishes where the user is looking is not a z-order
// statement, it is a dropout.
//
// SO THE SCANNER IS NOW TOPMOST IN THE WAVEFORM AREA while it runs — over the
// stems, over the cursor where they overlap, over the plate and the region
// ground. Everything it covers is a per-frame repaint anyway.
//
// It stays WAVEFORM-ONLY: no head, no lane presence, nothing in the top strip
// (the ruling is at paint_ruler_row's head block — render_playhead is shared
// with the cursor and, since 2026-08-02, cannot reach a strip lane at all: it
// draws the line inside `area` and nothing else).
// Same displayed-plate basis the cursor uses, so both ride the blitted pixels
// through a worker rebuild; the value fields it reads are meaningful only while
// active, which is exactly what the gate asks.
void GuiPaintHandler::paint_scanner(cairo_t* cr, const GuiRect& area) {
    if (!app.playhead_scanner_active) return;

    const PlateViewportBasis basis = plate_viewport_basis();
    const double scan_px =
        scanner_pixel_x(app, wf_cache.fp_vp_start, basis.spp);
    render_playhead(cr, area, scan_px, kPlayheadScanner);
}

// -- GuiPaintHandler::paint_bottom_strip ---------------------------------

void GuiPaintHandler::paint_bottom_strip(cairo_t* cr, int sr) {
    // ROW 7 — THE BOTTOM STRIP IS ONE LINE (architect 2026-08-01). The status
    // row and the modal/editor row collapsed into a single lane of TWO FIXED
    // SECTIONS: the active modal / editor / prompt / status text on the LEFT and
    // the TIMESTAMP on the RIGHT. The boundaries come from shaped maxima and
    // never from the current text, so nothing on the line moves when the clock
    // grows a digit (bottom_row_sections, at the top of this file, with the
    // layout).
    //
    // WHAT DIED WITH THE COLLAPSE, and why it is not missing: the S/T · W/P ·
    // A/B view readout and the "(read-only)" token. Rows 3 and 4 display all
    // three view states as lit buttons and tabs, and the tab locks show
    // read-only, so the letters were restating what the redesigned rows say in
    // their own vocabulary. WHAT LEFT LATER THE SAME DAY: the dirty mark's own
    // section, which moved to the WINDOW TITLE beside the project name
    // (GuiPlatform::apply_window_title) — the title is the mark's only home now.
    //
    // PRECEDENCE IN THE MODAL SPAN, highest first: prompt > queue /
    // loading status > settings editor > load editor > BPM editor > transient
    // status message > the resolved-value readout. MODAL TEXT WINS OVER THE
    // READOUT (the planner's call at the row-7 brief, FLAGGED for the architect):
    // the readout is a passive description of the selection, the others are
    // things the user is doing or waiting on, and only one span exists. The
    // transient message sits directly above the readout for the same reason and
    // cannot collide with an editor in practice — it is cleared by the next key
    // press, and opening any editor is one.
    //
    // The row paints on EVERY frame class (loading, blank, loaded) like the four
    // redesigned rows above it: the line is audio-independent, and the loading
    // status is one of the things it carries.
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = bottom_row_content_area(app);
    const int     border  = bottom_row_border_h_px();

    // THE ROW'S OWN GROUND AND ITS TWO BORDERS, hard-coded from row_7_text.png
    // per the redesign's color ruling (the constants and the two-greys note are
    // at kRedesignBottomLine, render.h). The ground erases whatever chrome
    // render_background laid down, so the strip no longer depends on the tunable
    // kBackground happening to hold the same value.
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kRedesignTabGround.r, kRedesignTabGround.g,
                             kRedesignTabGround.b);
        cairo_rectangle(cr, content.x, content.y, content.w, content.h);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                             kRedesignTabLine.b);
        cairo_rectangle(cr, lane.x, lane.y, lane.w, border);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, kRedesignBottomLine.r, kRedesignBottomLine.g,
                             kRedesignBottomLine.b);
        cairo_rectangle(cr, lane.x, lane.y + lane.h - border, lane.w, border);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // ONE FACE, ONE BASELINE, ONE SET OF SECTION BOUNDARIES for the whole line.
    // The baseline is solved the way every redesigned row solves one: center the
    // face's own (ascent + descent) band in the content band and round to the
    // pixel grid. With the row on the redesign's sans this formula reproduces
    // the crop's measured baseline (row 22 of 33) exactly.
    cairo_save(cr);
    cairo_scaled_font_t* font = select_bottom_row_face(cr);
    const double baseline = redesign_baseline(font,
                                              static_cast<double>(content.y),
                                              static_cast<double>(content.h));
    const BottomRowSections sec = bottom_row_sections(font, lane);

    // The glyph ink band the editors' caret, selection and red flash share.
    cairo_font_extents_t fe;
    cairo_scaled_font_extents(font, &fe);
    const int band_y = static_cast<int>(std::nearbyint(baseline - fe.ascent));
    const int band_h = static_cast<int>(
        std::nearbyint(fe.ascent + fe.descent));

    // The published editor geometry is rewritten from scratch every run, so an
    // editor that is not painted (closed, or outranked in the chain below)
    // leaves nothing behind for the pointer path to grab.
    app.bottom_editor_text = AppState::BottomEditorText{};

    // --- Section A: the timestamp, RIGHT-ALIGNED — its reserved cell's right
    //     edge one pad in from the window's right edge (architect 2026-08-01).
    //     sr is the loaded file's sample rate and the
    //     playhead samples are source-frames. Split-playhead: track the scanner
    //     during playback (what the user hears), the cursor otherwise (the
    //     scanner is meaningful only while active, so the ternary takes the
    //     cursor at rest). The old paint-site clamp at 5999.999 is GONE: one
    //     owner caps the clock, and it is format_timestamp (at 59:59.999 — a
    //     longer source truncates, the architect's ruling, recorded there).
    //     Unclipped: the cell is inside the lane by construction.
    {
        const int64_t ts_sample = app.playhead_scanner_active
            ? app.playhead_scanner_sample
            : app.playhead_cursor_sample;
        double seconds = 0.0;
        if (sr > 0) {
            seconds = static_cast<double>(ts_sample) /
                      static_cast<double>(sr);
        }
        if (seconds < 0.0) seconds = 0.0;
        show_row_text(cr, font, sec.a_x, baseline,
                      format_timestamp(seconds), kRedesignLabel);
    }

    // --- Section C: the modal / editor / status chain, in the span that runs
    //     from the left lead-in to one pad before the timestamp's reserved
    //     cell. ---
    //
    // THE SPAN IS CLIPPED AT THE RESERVATION, and that clip is the whole
    // overrun mechanism (architect 2026-08-01: a screen too small to hold the
    // line is a user problem — "too small screen is adversarial" — so there is
    // no ellipsis, no shrink and no scroll here). The reservation holds even
    // on the frames where the clock is momentarily absent, so C's right edge
    // never moves and its text never jumps.
    //
    // The clip covers the row's whole content band vertically, so the editors'
    // caret, selection highlight and red flash clip on the same boundary as
    // their glyphs — one rectangle for every branch below, taken once. A
    // degenerate lane (a window narrower than pad + pad + A + pad, which the
    // 640px minimum makes unreachable at every gui_scale) yields an empty span
    // and simply paints no C at all rather than a normalized backwards rect.
    const double c_w = sec.c_x1 - sec.c_x;
    if (c_w <= 0.0) {
        cairo_restore(cr);
        return;
    }
    cairo_save(cr);
    cairo_rectangle(cr, sec.c_x, static_cast<double>(content.y),
                    c_w, static_cast<double>(content.h));
    cairo_clip(cr);

    if (app.prompt.active) {
        // Plain tier: the prompt text and its response labels assembled
        // into one string joined by single ' ' characters and drawn in a
        // single pass. Single space between tokens: two spaces never appear
        // in GUI output, and modals use exactly one.
        std::string assembled = app.prompt.text;
        for (const auto& label : app.prompt.response_labels) {
            assembled += ' ';
            assembled += label;
        }
        show_row_text(cr, font, sec.c_x, baseline, assembled, kRedesignLabel);
    } else if (app.history_mode.active &&
               text_editor::is_active(app.load_editor)) {
        // THE LOAD EDITOR OVER THE MODE'S OWN LINE (2026-08-04). The mode
        // ADMITS `'` — in the mode that editor loads a COMMIT in place
        // rather than a render entry — so for the first time the mode's line
        // has a contender for the cell, and the editor takes it: it is the
        // surface the user is typing into, and a caret with nowhere to paint
        // is not a modal editor.
        // The mode's line comes straight back when the edit ends either way.
        //
        // THE BRANCH IS MODE-SCOPED, above the mode's line and therefore above
        // the queue status too, which is the ranking the mode already has:
        // while it stands, this span is the mode's, and an editor the mode
        // itself opened inherits that standing. OUTSIDE the mode the chain is
        // untouched — the ordinary load editor keeps its old rank below the
        // queue status, where the opener's own running-render guard means the
        // two never contend anyway.
        //
        // ITS PREFIX IS THE SUBJECT'S: `Load: ` with no ./renders/ lead-in,
        // since what the buffer holds is a commit spelling.
        render_bottom_strip_editor(cr, app, font, app.load_editor,
                                   kLoadEditorHistoryPrefix,
                                   sec.c_x, baseline, band_y, band_h);
    } else if (app.history_mode.active) {
        // THE `h` HISTORY MODE'S ONE LINE, in the modal span — the same cell the
        // three bottom-strip editors paint in. It ranks directly under the
        // PROMPT (Ctrl+Q's quit dialog can still be raised over it) and the
        // in-mode load editor above, and over the queue status, the remaining
        // editors and the readout, because while the mode stands this line is
        // what the strip is for. Those remaining editors cannot arrive here at
        // all: the mode's keyboard allowlist admits no opener but `'`.
        //
        // THE SHAPE: the commit's position in the walk and its short SHA, then
        // the scale — `Scale: [-]<then token> [+]<now token>`, in the lane's own
        // sign vocabulary and through the lane's own spelling owner
        // (history_diff_label), so the corner and the flags cannot come to
        // bracket differently.
        //
        // THE SEGMENT APPEARS ONLY WHEN THE SCALE CHANGED (architect
        // 2026-08-05, superseding the arc's unchanged-token report): a value
        // that both sides agree on is not a difference, and this view shows
        // differences. So there is no unchanged arm at all, and the both-sides-
        // empty case is covered by the same test rather than by one of its own.
        // A side carrying no `scale=` line has an EMPTY token and shows as its
        // bracket with nothing after it — unreachable while `scale` is a
        // required settings key and every walk member is strict-load clean, and
        // kept as the least-surprising shape rather than a refusal.
        //
        // THE POSITION/SHA LEAD-IN IS THE PLANNER'S CHOICE, flagged for the
        // architect's live pass — it is one `if` and one `+=`, deliberately
        // removable without touching the scale half.
        //
        // Plain label ink (kRedesignLabel) through show_row_text, matching the
        // prompt branch above and the transient-message and readout branches
        // below: this is a passive report, not an editor, so it takes no caret,
        // no prefix face and no flash.
        std::string line;
        const std::size_t count = app.history_mode.session.commit_count();
        if (count > 0) {
            line += std::to_string(app.history_mode.index + 1);
            line += '/';
            line += std::to_string(count);
            const std::string& sha =
                app.history_mode.session.sha_at(app.history_mode.index);
            if (!sha.empty()) {
                line += ' ';
                line += sha.substr(0, 7);
            }
        }
        const GuiHistoryCommitDelta* d = app.history_mode.session.delta_at(
            app.history_mode.index, app.history_mode.compare);
        // No unavailable-delta arm: walk membership is the strict whole-set
        // load (history_diff.h's gate, 2026-08-04), so every commit this line
        // can name has a real delta — the old `Ambiguous` token died with the
        // display machinery it named.
        if (d && d->scale_changed) {
            if (!line.empty()) line += ' ';
            line += "Scale: ";
            line += history_diff_label("[-]", /*disabled=*/false,
                                       d->then_scale_token);
            line += ' ';
            line += history_diff_label("[+]", /*disabled=*/false,
                                       d->now_scale_token);
        }
        show_row_text(cr, font, sec.c_x, baseline, line, kRedesignLabel);
    } else if (!app.queue_progress_text.empty()) {
        // The render/batch/queue status AND the startup "loading..." line —
        // one slot, and the reason this painter runs on the loading frame class
        // too (it is the only feedback there).
        show_row_text(cr, font, sec.c_x, baseline, app.queue_progress_text,
                      kRedesignLabel);
    } else if (text_editor::is_active(app.settings_editor)) {
        // Settings prompt overlay: "setting: <pending>", through the shared
        // shaped-editor body (which publishes the caret geometry and owns the
        // parse-failure red flash).
        render_bottom_strip_editor(cr, app, font, app.settings_editor,
                                   kSettingsEditorPrefix,
                                   sec.c_x, baseline, band_y, band_h);
    } else if (text_editor::is_active(app.load_editor)) {
        // Load prompt overlay: "Load: ./renders/<pending>", through
        // the same shared body; its red flash is an unresolved / bad commit.
        render_bottom_strip_editor(cr, app, font, app.load_editor,
                                   kLoadEditorPrefix,
                                   sec.c_x, baseline, band_y, band_h);
    } else if (text_editor::is_active(app.top_flag_editor) &&
               app.top_flag_editor.kind ==
                   text_editor::Kind::BpmBracket) {
        // BPM editor overlay, through the same bottom-strip
        // editor helper as the settings branch above. top_flag_editor
        // with kind==BpmBracket only ever paints here, never over the
        // flag in the top strip.
        render_bottom_strip_editor(cr, app, font, app.top_flag_editor,
                                   kBpmEditorPrefix,
                                   sec.c_x, baseline, band_y, band_h);
    } else if (!app.transient_status_message.empty()) {
        // The transient one-line outcome report (phase-reset paste divergence,
        // "No renders to load in place", ...). It used to ride the two-row
        // status line
        // as an appendix; with one line and one span it takes its
        // place in the chain, directly above the readout. Cleared by the next
        // key press, which is also what opens every editor above it, so the two
        // cannot compete in practice.
        show_row_text(cr, font, sec.c_x, baseline,
                      app.transient_status_message, kRedesignLabel);
    } else {
        // THE RESOLVED READOUT IS SELECTION-ONLY (row 5, 2026-08-01). It used
        // to be "hover wins, else the last-selected marker"; the hover arm died
        // with the whole hover-popup machinery, so what is left is the arm that
        // was already here — the LAST-SELECTED marker's resolved tempo, computed
        // live when it is an eligible pass/label_ref (popup_eligible_marker,
        // itself 'W'-view + non-iteration only). Owners and phase resets have
        // nothing to resolve, so their strip stays clean while their own value
        // shows on their flag.
        //
        // compute_hover_popup_text — in the FROZEN parser, and untouched — keeps
        // this one live caller; only the hover half of its name is now history.
        // The out-param for the pasteable payload stays unused here: this site
        // wants the notice-free display string, and Ctrl+C asks for the payload
        // itself at its own site.
        //
        // LIVE-TEST FLAGGED: whether the readout should follow the selection
        // alone is the architect's call at the row-5 look. Row 7 adds a second
        // flagged fact — it is the LOWEST tier of the one span, so any modal,
        // editor or status message hides it while it is up.
        std::string readout;
        if (popup_eligible_marker(app, app.last_selected_marker)) {
            readout = compute_hover_popup_text(
                slice_to_warp_markers(app.warpmarkers.markers()),
                app.last_selected_marker, sr, audio.total_frames());
        }
        if (!readout.empty()) {
            show_row_text(cr, font, sec.c_x, baseline, readout, kRedesignLabel);
        }
    }
    cairo_restore(cr);   // section C's clip
    cairo_restore(cr);   // the row's font/color state
}

// -- GuiPaintHandler::on_redraw ------------------------------------------

void GuiPaintHandler::on_redraw(cairo_t* cr, int x, int y, int w, int h) {
    // (The monospace grid measure that opened every frame is gone with the face
    // — row 7. Nothing in the product measures text outside a shaping pass now,
    // and every such pass owns its own font selection.)

    // Event-synchronized hit geometry, PROMOTE phase (ruling at the selector):
    // done at the TOP of the frame, BEFORE any painting, so the flag cache this
    // frame blits (blit-only below) AND the overlays this
    // frame paints around it (the live trim pass, the flag editor overlay, the
    // marker stems, the
    // playhead) all land on the SAME map — the one the committed items were
    // built against. Promoting at the frame's END instead let the overlays paint
    // against the OLD map on the very frame that first blit the rebuilt cache,
    // then advanced the map silently with no further damage, so those overlay
    // pixels could stay misplaced (and a stationary hover could keep naming a
    // marker whose flag had moved away). Promote the staged value the last item
    // rebuild left, once — staged_displayed_valid clears on the first damage rect
    // of the frame, so the remaining rects are no-ops; idle frames with no staged
    // value do nothing. A rebuild always invalidates its item region, so the
    // committing frame's damage always includes the items. No input dispatches
    // mid-loop (single-threaded) and the whole frame still commits atomically
    // after the loop in GuiPlatform::paint_one_frame, so a press only ever reads
    // the last COMMITTED frame's geometry — that guarantee is unchanged.
    //
    // NOTHING SUBSCRIBES TO THIS EDGE any more, and it no longer publishes one:
    // the hover refresh hook that hung off it re-resolved the marker hover cache
    // against the just-promoted map, and row 5 deleted the hover cache; the
    // displayed_map_gen counter that outlived it as a bare record went too
    // (2026-08-02, write-only). The overlays below simply read the promoted
    // basis. A future subscriber hangs its key here, on this block.
    if (app.staged_displayed_valid) {
        app.displayed_target_warp_frame_map =
            std::move(app.staged_displayed_target_warp_frame_map);
        app.staged_displayed_target_warp_frame_map.clear();
        // Promote the displayed VIEWPORT mirror in the SAME block (ONE promote):
        // the flag editor's box geometry advances to the fp_* viewport the
        // just-blitted flag cache was built against, in lockstep with the map
        // above.
        app.displayed_vp_start = app.staged_displayed_vp_start;
        app.displayed_vp_end   = app.staged_displayed_vp_end;
        app.displayed_area_w   = app.staged_displayed_area_w;
        app.staged_displayed_valid = false;
    }

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);

    render_background(cr, x, y, w, h);
    // THE GROUND SPLIT: the chrome erase above covers the whole exposed rect;
    // the waveform area then takes its own kWaveformCanvas ground. Unconditional and
    // ahead of every content branch, so a cold frame (loading, no audio, or a
    // null plate before the first worker publish) shows canvas where the
    // waveform will be rather than a chrome-colored hole. The outer clip already
    // bounds this to the exposed rect, so the full-rect fill costs nothing off
    // the damage. The rect is the EFFECTIVE-width waveform_area, so the <=15px
    // inert right gutter at a non-multiple-of-16 window stays chrome — it is
    // outside every grid-aligned surface and no waveform pixel ever paints there
    // (no gutter exists at 1920/2560/3840).
    {
        const GuiRect canvas = waveform_area(app);
        render_canvas(cr, canvas.x, canvas.y, canvas.w, canvas.h);
    }

    // THE FOUR REDESIGNED TOP ROWS AND THE BOTTOM ROW PAINT ON EVERY FRAME
    // CLASS, deliberately OUTSIDE
    // the loading / total>0 branches below: they are the surfaces with no
    // dependence on the loaded audio, and their buttons are claimed ABOVE the
    // pointer path's loading guard for exactly that reason. A button that is
    // clickable must be visible — painting it only in the total>0 branch would
    // leave the press claim live over a lane showing bare chrome during a load,
    // and dead on a cold launch where the row has never painted (the hit rects
    // are the painter's stash). Their own opaque grounds erase the chrome
    // render_background laid down.
    //
    // Each is gated on its OWN exposure rather than run unconditionally like the
    // canvas ground above: these passes shape labels through HarfBuzz, which the
    // outer Cairo clip would not elide, so a narrow per-frame playhead damage
    // must not pay for them. Nothing painted after this point touches the four
    // top lanes (the flag cache is transparent over them, every other pass owns a
    // lane below them), so painting them first overdraws nothing.
    //
    // THE BOTTOM ROW JOINS THEM (row 7): it is audio-independent in the same
    // sense — the timestamp reads 00:00.000 with no source, and the loading line
    // is one of the things the row carries, which is why the separate loading-
    // only draw that used to sit below is gone. Nothing painted later overlaps
    // it except the floating surfaces, which paint over everything by design.
    {
        const GuiRect exposed{x, y, w, h};
        if (rects_intersect(exposed, top_menu_row_area(app))) {
            paint_menu_row(cr);
        }
        if (rects_intersect(exposed, top_toolbar_row_area(app))) {
            paint_toolbar_row(cr);
        }
        if (rects_intersect(exposed, top_tab_row_area(app))) {
            paint_tab_row(cr);
        }
        if (rects_intersect(exposed, top_icon_row_area(app))) {
            paint_icon_row(cr);
        }
        if (rects_intersect(exposed, bottom_row_area(app))) {
            paint_bottom_strip(cr, audio.sample_rate());
        }
    }

    if (audio.total_frames() > 0 && !app.loading) {
        const GuiRect area       = waveform_area(app);
        const GuiRect top_strip  = top_strip_area(app);
        const GuiRect exposed{x, y, w, h};

        // The live viewport / target-warp_frame_map computations live in the
        // cache rebuild paths (waveform via the worker, flags via
        // maybe_rebuild_flag_cache), not in on_redraw, which reads
        // wf_cache.fp_* for displayed-viewport inputs and treats the plate and
        // flag strips as blit-then-overlay paths. Trim is a live pass
        // (paint_trim) on the free item-basis owners.
        //
        // THE AUTHORITATIVE PAINT ORDER, bottom of the stack to top. This is
        // the ONE full enumeration in the tree — every other site states its
        // own pass's class plus a pointer here — and it is derived from the
        // call sequence below plus the two unconditional passes above this
        // branch:
        //   1. render_background — the chrome erase over the whole exposed
        //      rect (above, unconditional).
        //   2. render_canvas — the waveform area's ground AND its 2px black
        //      top/bottom borders (above, unconditional).
        //   3. the four redesigned top rows and the bottom row, each on its own
        //      exposure (above, outside this branch; they own lanes nothing
        //      below them paints on).
        //   4. region ground -> waveform plate -> phase-reset overlay ring.
        //   5. LIVE TRIM, one pass, entirely inside the trim lane: the lane
        //      ground, the window's bar, the two endcaps and the midpoint mark.
        //   6. the CURSOR's WAVEFORM stem segment (paint_playheads — the head
        //      is the ruler pass's, step 9).
        //   7. the MARKER STEMS (waveform).
        //   8. the SCANNER (waveform).
        //   9. the RULER lane — ticks and labels — AND, in the same pass, the
        //      cursor's HEAD on the marker lane: the head's pre-blended tick
        //      crossing needs the tick columns, so one owner walks both (the
        //      reasoning is at that block in paint_ruler_row).
        //  10. the FLAG BLIT.
        //  11. the strip-drag anchor stem (waveform, mid-gesture only).
        //  12. the flag editor's box, then the dropdown and the tooltip — the
        //      floating surfaces, after every pass above and outside this
        //      branch.
        // (The bottom row left the tail of this sequence in row 7 — it paints
        // with the other redesigned rows at step 3, on every frame class, and
        // overlaps none of these passes.)
        // Two structural rulings live in this sequence:
        //   THE RECOLOR MODEL (architect 2026-07-26) — a highlight changes the
        //     GROUND, so the ONE ground recolor (the region's) paints BEFORE the
        //     plate and the ink composites over it. The phase-reset overlay
        //     contributes no ground at all (architect 2026-07-27): its 1px RING
        //     is its whole visual, and a boundary line paints AFTER the plate,
        //     crossing the ink like the stems do.
        //   THE Z-ORDER FLIP (architect 2026-07-23) — the cursor playhead's
        //     STEM passes UNDER
        //     marker flags, so a cursor resting on a marker sits hidden behind
        //     that marker's flag standing in the same column; a SELECTION adds
        //     no playhead-like mark of its own, its whole cue being its
        //     members' BRIGHTENED FLAGS (the class ladder's brighter pair) with
        //     the landed cursor on the focus. (Row 5 moved the marker stems
        //     ABOVE the cursor's stem — the hidden-by-marker z-intent —
        //     and 2026-08-01 lifted the SCANNER alone above the stems, so the
        //     moving line does not blink out at every marker it crosses.)

        if (rects_intersect(exposed, area)) {
            // THE GROUND RECOLOR, under the plate. render_canvas already laid
            // the kWaveformCanvas ground for the whole area above; this repaints the
            // region's span of it opaquely, so the plate's transparent gaps show
            // the recolored ground rather than the plain one.
            paint_region_ground(cr, area);
            paint_waveform_plate(cr, area);
            // The overlay band's boundary ring — the phase-reset overlay's whole
            // visual — over the plate and under trim
            // and the stems, so the focused reset's own stem stays crisp on top
            // of the left seam.
            paint_phase_reset_overlay_ring(cr, area);
        }

        // LIVE TRIM PASS — the old trim-stem-cache slot, now covering ALL trim
        // pixels (the trim bar lane's ground, the window's bar, the endcaps and
        // the midpoint mark).
        // Gated on EITHER half being exposed: render_background erased every
        // exposed top-strip pixel above, so a strip-only damage (hover text, a
        // flag change) must repaint the strip-resident trim pixels; the outer
        // Cairo damage clip bounds the actual work either way.
        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            paint_trim(cr, area, top_strip);
        }

        // The CURSOR BEFORE the flag blit (Z-ORDER FLIP, architect 2026-07-23):
        // its line paints UNDER the
        // marker flags that follow. (The scanner used to ride along in this pass
        // and now paints after the stems, below — waveform-only either way, so
        // its stacking against the lanes never entered the question.)
        // flag_cache.surface is ARGB32, CLEAR-cleared
        // each rebuild and transparent outside the painted shapes, so the flag
        // blit composites source-over and never erases the playheads it does not
        // cover. Gated on area OR top_strip: the cursor line lives in the waveform
        // area, its head in the top strip.
        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            paint_playheads(cr, area);
        }

        // MARKER STEMS AFTER THE CURSOR (row 5's z-intent): the cursor's
        // waveform stem passes UNDER them, and the flag boxes go over
        // everything in the strip blit below. That is the hidden-by-marker
        // model translated — a marker sharing the cursor's column hides it,
        // exactly as flags painted over the old triangle — and it is why the
        // stems paint here rather than in the pre-playhead slot the singleton
        // selected stem occupied. The stems are the flags' waveform half and
        // must not be split across the cursor by paint order. (The full
        // sequence is the paint-order block above.)
        if (rects_intersect(exposed, area)) {
            paint_marker_stems(cr, area);
        }

        // THE SCANNER LAST OF THE WAVEFORM VERTICALS (architect 2026-08-01):
        // the moving line paints AFTER the stems, so it crosses them instead of
        // being erased column by column as it sweeps past every marker. Only the
        // scanner moved — the cursor stayed in paint_playheads above, under the
        // stems and under the flags. Waveform-only, so no top_strip arm.
        if (rects_intersect(exposed, area)) {
            paint_scanner(cr, area);
        }

        if (rects_intersect(exposed, top_strip)) {
            // The ruler paints BEFORE the flags: its ticks descend past the
            // marker lane's top and must sit UNDER whatever that lane draws.
            paint_ruler_row(cr);
            paint_flag_annotations(cr, top_strip);
        }

        // Strip-drag anchor stem: over the plate/stems in the waveform area
        // only. It now paints AFTER the playheads (the flip moved them up), so
        // where the pivot column coincides with the cursor/scanner column during
        // a strip drag the anchor stem sits OVER the playhead LINE (both are
        // waveform verticals; the playhead's strip-lane pixels are untouched,
        // the anchor has none). The anchor shows only mid-strip-drag, so
        // this overlap is transient and the pivot affordance reading on top is
        // acceptable. The flag editor's box likewise ends up after the
        // playheads, but on the non-overlapping marker lane.
        if (rects_intersect(exposed, area)) {
            paint_strip_drag_anchor(cr, area);
        }

    }

    // THE FLOATING SURFACES PAINT TOPMOST — after EVERY pass above, including
    // the waveform, because both hang below the top strip and overlap whatever
    // is under them. They are NOT exposure-gated the way the rows are: each
    // writes the rect it painted (or a zero rect) on every run, and a run that
    // skipped would strand a stale rect for the hit tests and the damage to
    // read. Hidden, each costs one boolean.
    //
    // THEY CANNOT COEXIST, so their order between themselves is moot: the
    // dropdown opens on a PRESS and a press hides the tooltip, and while the
    // dropdown is open no roster button hovers, so no tooltip can arm under it.
    //
    // THE OPEN FLAG EDITOR'S BOX PAINTS HERE, ahead of those two and after every
    // pass above — including the flag blit it must cover — and UNCONDITIONALLY,
    // for the floating surfaces' own reason: it publishes the geometry the
    // pointer path grabs, and a run that skipped would strand a stale box. Off
    // the damage the outer Cairo clip makes it free, and with no editor open it
    // is two boolean tests. It sits OUTSIDE the loading / total>0 branch above
    // for the same reason — that branch is where the publication would go
    // missing.
    render_flag_editor_box(cr, app, audio);
    paint_dropdown(cr);
    paint_shift_tooltip(cr);

    cairo_restore(cr);

    // The target here IS the memory the compositor will read: each buffer's
    // surface is a cairo IMAGE surface created over the mmap'd wl_shm pool
    // (GuiPlatform::recreate_shm_pool), and the pool pages are shared with the
    // compositor. Flushing is cairo's handshake before anything outside cairo
    // reads that memory — it lands whatever the backend is still holding in
    // internal state, so the backing bytes are complete. This is the last point
    // where that can happen: nothing between here and the publish flushes the
    // surface again (destroying the context ends the drawing, not the surface,
    // which outlives every frame with the pool), and the very next thing
    // GuiPlatform::paint_one_frame does after the paint loop is
    // wl_surface_attach + wl_surface_commit.
    cairo_surface_flush(cairo_get_target(cr));
}

// -- GuiPaintHandler::on_resize ------------------------------------------

void GuiPaintHandler::on_resize(int w, int h) {
    app.width  = w;
    app.height = h;
    if (app.loading || audio.total_frames() <= 0) return;

    // A zoom level valid at the old width may exceed the per-file effective
    // ceiling at the new width. The level ceiling and the viewport clamp both
    // live in clamp_viewport_start now; the resize keeps only its TRIGGER role
    // and delegates. When the level actually moved the reflow changed spp under
    // the playback predictor, so re-anchor it.
    const double old_zoom = app.zoom_level;
    clamp_viewport_start(app, audio);
    if (app.zoom_level != old_zoom && playback.is_playing())
        playback.resync_predictor();
}
