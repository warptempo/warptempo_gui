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
// Every string on the bottom line — the timestamp, the dirty dot, the prompts,
// the queue/render/transient status, the resolved readout and the three editors'
// own text — is the redesign's sans at the redesign's size, shaped and painted
// through the ONE chokepoint like every other redesigned row.
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

// THE LINE'S FIXED SECTIONS (the architect's kdenlive model, 2026-08-01):
// nothing on the row moves when the timestamp grows a digit or the dirty dot
// appears and disappears. Every boundary is computed from SHAPED MAXIMA, never
// from the text currently on screen.
//
//   pad | A: the timestamp | pad | B: the dirty dot | pad | C: everything else
//
// A is the widest timestamp the product can ever display. The bound is the
// CONTAINER's, not the format's: a RIFF data chunk tops out near 4 GiB, which at
// the heaviest supported source (44.1kHz stereo 24-bit) is ~16232 s = 270:32, so
// the minutes field can reach three digits and no more (the derivation and the
// matching format cap are at format_timestamp, time_format.h). kWidestTimestamp
// is a three-digit specimen; with tabular digits its shaped width is EVERY
// three-digit timestamp's width, and a two-digit one is narrower by one digit
// and simply leaves that much slack at the section's right.
//
// B is one dot's own shaped width, present or not.
//
// THE PADDING IS ONE CONSTANT used three times — the row's own measured left
// pad, reused as the inter-section gap by eye-consistency (the architect's
// allowance; the alternative, a shaped space at ~4px, read cramped against a
// 13px lead-in). C runs from the last boundary to the window's right edge.
static constexpr const char* kWidestTimestamp = "000:00.000";

struct BottomRowSections {
    double a_x = 0.0;   // the timestamp's pen
    double b_x = 0.0;   // the dirty dot's pen
    double c_x = 0.0;   // the modal / status span's pen
};

static BottomRowSections bottom_row_sections(cairo_scaled_font_t* font,
                                             const GuiRect& lane) {
    const double pad = static_cast<double>(bottom_row_pad_x());
    const double a_w =
        text_shape::shape_text_run(font, kWidestTimestamp).width_px;
    const double b_w = text_shape::shape_text_run(font, "*").width_px;
    BottomRowSections s;
    // Every boundary lands on an integer pen so the hinted glyphs stay crisp,
    // the same rounding convention the redesigned rows' label origins take.
    s.a_x = std::nearbyint(static_cast<double>(lane.x) + pad);
    s.b_x = std::nearbyint(s.a_x + a_w + pad);
    s.c_x = std::nearbyint(s.b_x + b_w + pad);
    return s;
}

// The three bottom-strip editors (settings / render-commit / BPM) share this one
// body, differing only in prefix and which State they read. It shapes PREFIX AND
// PENDING AS ONE RUN — so the pair kerns exactly as it paints — and addresses
// the pending half through that run's own byte boundaries, which it publishes
// for the pointer path (AppState::BottomEditorText).
//
// NO VIEW OFFSET, deliberately: unlike the flag editor's unrolled box these
// editors run off the right edge of the window rather than scrolling. That was
// the monospace path's intent too and it is kept — the settings and commit
// strings that reach the edge are pathological, and a scrolling field here would
// need a right boundary the row does not have.
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

    // THE GLYPH INK BAND — the caret's, the selection highlight's and the red
    // flash's shared vertical extent, the face's own ascent-to-descent about the
    // baseline. The retired monospace box derived the same band by inverting a
    // chip formula; here the extents ARE the band, with no box to invert.
    const double sel_x0 = origin_x + bx[p0];
    const double sel_x1 = origin_x + run.width_px;

    // 1. THE RED FLASH, and it is the box's whole remaining visual. At rest the
    //    editors paint no box at all: the old one filled and ringed itself in
    //    the row ground, i.e. invisibly. A parse failure fills the editable
    //    run's band in kAccent under a 1px kAccentOutline ring — a parse-fail
    //    chip's exact colors, kept TUNABLE deliberately (no crop shows this
    //    state; kdenlive has no comparable surface) while everything else on the
    //    row is hard-coded.
    if (ed.red) {
        const int rx0 = static_cast<int>(std::nearbyint(sel_x0));
        const int rx1 = static_cast<int>(std::nearbyint(sel_x1));
        const int rw  = (rx1 > rx0) ? (rx1 - rx0) : 1;
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kAccentOutline.r, kAccentOutline.g,
                             kAccentOutline.b);
        cairo_rectangle(cr, rx0, band_y, rw, band_h);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, kAccent.r, kAccent.g, kAccent.b);
        cairo_rectangle(cr, rx0 + 1, band_y + 1, rw - 2, band_h - 2);
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
    //    could kern its first glyph differently and shift the ink.
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
    // live paint_trim pass, which runs BEFORE the playheads — the z-order
    // ruling.) The boxes CARRY THEIR TEXT since row 5 — the marker-text lane
    // that used to show it beneath them is gone — so the only thing painted
    // after this blit in that band is the open editor's overlay
    // (render_flag_editor_box); the editing target's flag blits here as an
    // ordinary box and the overlay covers it. Like the other
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

// One authored 100%-scale length -> device pixels, the ONE conversion every
// dimension below takes (std::nearbyint like every other integer-domain
// conversion in this tree).
int scaled_px(double authored) {
    return static_cast<int>(std::nearbyint(authored * gui_scale_factor()));
}

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
// architect states one. The hover pill therefore spans the full 30px row — the
// 1px vertical inset that stood here was a misread of the crop (those rows were
// the title-bar seam, not design).
constexpr double kMenuLabelPadPx   = 10.0;   // per side, sets the button width
constexpr double kMenuPillRadiusPx = 5.0;    // the crop's AA fits r ~ 4.6

// THE MENU ROW'S BUTTONS, in painted order — flush from the row's left edge and
// ADJACENT WITH NO GAP, the css float model's default (the architect states a
// gap where one exists; row 2's 2px invisible separator is the only one in the
// redesign so far, and row 1 was never given one).
//
// REDESIGNED ROWS GET CORRECT CAPITALIZATION, row by row (architect
// 2026-07-31): the all-lowercase program-text rule now CARVES OUT the
// redesigned rows' labels. It still governs stderr, the prompts, and every
// un-redesigned surface until that surface's own row lands.
struct MenuButtonDef {
    RedesignButton id;
    const char*    label;
};
constexpr MenuButtonDef kMenuButtons[] = {
    {RedesignButton::Quit,     "Quit"},
    {RedesignButton::Settings, "Settings"},
};

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
    // THE ZOOM PAIR (architect 2026-08-01), between separators of its own so it
    // reads as its own group rather than as a tail of the view radios or a head
    // of the clipboard set. Momentary like the clipboard buttons — zoom is an
    // action, not a mode, so neither takes the selected face.
    {RedesignButton::IconZoomOut, IconRowLead::Separator, nullptr, icons::Icon::ZoomOut},
    {RedesignButton::IconZoomIn,  IconRowLead::Gap,       nullptr, icons::Icon::ZoomIn},
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
    {RedesignButton::IconCommit, IconRowLead::Gap,       nullptr, icons::Icon::DialogOkApply},
};

// -- THE FLOATING SURFACES: the hover tooltip and the settings dropdown -----
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
// EVERY BUTTON BUT QUIT AND SETTINGS HAS ONE (architect 2026-07-31): the
// one-line form is the whole story for most, and the two shift-admitting
// buttons add the hint line below it.
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
// border live in render.h with settings_popup_h_px's other ingredients — the
// popup's OPEN EDGE must size the box before it is painted. Only the HORIZONTAL
// terms, which depend on the widest shaped label, are the painter's alone.)
constexpr double kPopupItemInsetPx   = 3.0;   // the highlight box, per side
constexpr double kPopupSepInsetPx    = 7.0;   // the separator, per side

// THE LABEL IS LEFT-ALIGNED at a small pad, NOT centred (architect 2026-07-31,
// from dropdown_full): its items put text 36px from the highlight's left edge,
// but that 36 is a RESERVED ICON COLUMN plus the text pad, and icons are ruled
// OFF here exactly as they are on the tabs. Subtracting a ~28px Breeze icon
// column (the 22px glyph plus its gaps) leaves 8 — which the architect then
// widened to 12 at the live look, wanting the labels further from the Settings
// button's own text above them. It remains the tweak knob.
constexpr double kPopupLabelPadLeftPx = 12.0;

// THE MINIMUM ITEM WIDTH, so the right side carries clearly more empty space
// than the left — the tab-min-width pattern, and the reason a menu of short
// labels still reads as a menu. DERIVED rather than copied: the crop's items are
// 401px for labels of ~150px ink, but roughly half that width is the accelerator
// and submenu column this product has no analogue for. Taking the text half
// alone and scaling to our label set (widest "Playback speed" at 113px) leaves
// 200 as the authored floor: 8px of left pad and 113 of label leaves 79px
// trailing, about ten times the left pad, which is the stated asymmetry. This is
// the knob the architect tweaks at 100%.
constexpr double kPopupItemMinWidthPx = 200.0;

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
    // ground carrying TWO buttons — "Quit", whose action is Ctrl+Q's exact
    // route, and "Settings", whose action is the dropdown toggle. No ring; the
    // kdenlive bar is flat.
    //
    // THE HOVER MODEL IS KDENLIVE'S, and it is TWO faces, not three, for BOTH
    // buttons: at rest the label paints bare on the row ground; hovered, a
    // filled blue pill sits under it, FLUSH with the row (the css float model —
    // a flat button fills its whole row, architect 2026-07-31). A PRESS PAINTS
    // NOTHING NEW — a click keeps the hover face and only pointer-out rests it.
    // The click and disabled faces belong to rows 2 and 4 and do not reach here,
    // so this row has no press-state machinery at all.
    //
    // THE TWO ACTIONS DIFFER IN KIND: Quit is its chord (Ctrl+Q, dispatched
    // through the shared chord table like every other redesigned button), while
    // SETTINGS TOGGLES THE DROPDOWN — the roster's one non-chord action, since
    // no keyboard chord opens or closes a popup. The bare `;` key still opens
    // the settings editor directly and is untouched by the dropdown.
    const GuiRect row = top_menu_row_area(app);
    if (row.w <= 0 || row.h <= 0) return;

    cairo_save(cr);

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
        AppState::RedesignButtonFace& face =
            app.redesign_buttons[redesign_button_index(def.id)];
        face.rect = GuiRect{x, row.y, btn_w, row.h};
        // Neither menu button has a disabled face — both are live during a load
        // and on a blank state, which is the whole reason this row paints
        // outside the audio branches. The stash is written anyway so the tick
        // comparator's vector is total over the roster with no membership test.
        face.enabled = redesign_button_enabled(app, audio.total_frames(),
                                               def.id);

        if (face.hovered) {
            cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                 kRedesignAccent.b);
            redesign_rounded_rect_path(cr, x, row.y,
                                       static_cast<double>(btn_w),
                                       static_cast<double>(row.h), rad);
            cairo_fill(cr);
        }

        // The label color is the SAME in both faces; the pill under it is the
        // whole hover cue.
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(
            cr, run, static_cast<double>(x + pad),
            redesign_baseline(font, static_cast<double>(row.y),
                              static_cast<double>(row.h)));

        x += btn_w;
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
        // the painter publishes the hit rect.
        const text_shape::ShapedRun run =
            text_shape::shape_text_run(font, def.label);
        const int label_w = static_cast<int>(std::nearbyint(run.width_px));
        const int btn_w = pad_left + icon_px + icon_gap + label_w + pad_right;

        AppState::RedesignButtonFace& face =
            app.redesign_buttons[redesign_button_index(def.id)];
        face.rect = GuiRect{x, btn_y, btn_w, btn_h};

        // THE ENABLED VECTOR IS STASHED AS IT IS PAINTED, and this is the only
        // writer: main.cpp's per-tick comparator reads it back to notice that
        // the live answer has drifted (an undo push, a read-only toggle, a load
        // completing — none of which damages the strip on its own) and pays one
        // invalidate_top_strip to bring the faces up to date.
        const bool enabled =
            redesign_button_enabled(app, audio.total_frames(), def.id);
        face.enabled = enabled;

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
            // corners, so the outline is INSET BY HALF ITS OWN WIDTH: the
            // centerline runs at x + lw/2, which is the +0.5 half-pixel
            // alignment at a 1px stroke and the integer bound at an even one
            // (200%), and either way the painted band lands exactly on the
            // button box's outermost pixel ring with no straight edge
            // antialiased. One expression, both parities.
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
        // button dims as one object. Enabled, the factor is 1 and both are
        // bit-identical to the rest face.
        const double keep = enabled ? 1.0 : kRedesignDisabledMix;

        // The icon fills its own square, vertically centered in the button box,
        // in ITS OWN color (the icon table owns that — media-record is red where
        // the other three are the label white).
        icons::draw(cr, def.icon, static_cast<double>(x + pad_left),
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
    // or hover; there is no selected-hover face and no click or disabled face
    // anywhere in this row (a tab press is a chord, never a refusal).
    //
    // THE PADLOCK PUBLICATION IS ZEROED FIRST, every run, so a tab that stops
    // being read-only (or stops being active) cannot strand a clickable rect
    // where nothing is drawn — the same write-it-every-run rule the floating
    // surfaces and the flag editor's box follow.
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
    // side borders draw inside the box). With one-glyph labels the minimum is
    // what binds, which makes both tabs exactly the same width and the row
    // regular by construction.
    int x = lane.x;
    for (const TabDef& def : kTabs) {
        const text_shape::ShapedRun run =
            text_shape::shape_text_run(font, def.label);
        const int label_w = static_cast<int>(std::nearbyint(run.width_px));
        // THE FIELD, then THE SLOT. The field is what it always was; the slot is
        // reserved on every tab in every state, so both tabs stay identical in
        // width by construction and locking one shoves nothing.
        const int field_w = std::max(min_w, label_w + 2 * pad);
        const int tab_w   = field_w + slot_w;

        AppState::RedesignButtonFace& face =
            app.redesign_buttons[redesign_button_index(def.id)];
        face.rect    = GuiRect{x, lane.y, tab_w, content_h};
        face.enabled = redesign_button_enabled(app, audio.total_frames(),
                                               def.id);

        const bool selected = (app.active_tab_view == def.letter);
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
                //  - inset by HALF the stroke width on the left, right and top,
                //    so the band lands on the box's outermost pixel ring with no
                //    straight edge antialiased — the +0.5 half-pixel alignment
                //    at 1px, the integer bound at 2px, both parities from the
                //    same term (row 2's hover outline sets the precedent);
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
            const GuiColor fill =
                face.hovered ? kRedesignTabHover : kRedesignTabRest;
            cairo_set_source_rgb(cr, fill.r, fill.g, fill.b);
            cairo_rectangle(cr, x, lane.y, tab_w, content_h);
            cairo_fill(cr);
            if (face.hovered && content_h > line_w) {
                cairo_set_source_rgb(cr, kRedesignTabHoverEdge.r,
                                     kRedesignTabHoverEdge.g,
                                     kRedesignTabHoverEdge.b);
                cairo_rectangle(cr, x, lane.y + content_h - line_w,
                                tab_w, line_w);
                cairo_fill(cr);
            }
        }

        // The label is the SAME white in every state, CENTERED on both axes:
        // horizontally in the tab box (the padding is the width FLOOR's term,
        // not an anchor — at the minimum width a left-padded label would hug the
        // border instead of sitting in the middle), vertically by the shared
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
        // drawn ALWAYS — on both tabs, in both states, in its own reserved slot
        // (the geometry and its two measured numbers are at kTabLockBoxPx).
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
        {
            const ViewState& vs = (def.letter == 'B') ? app.tab_b : app.tab_a;
            const int lx = x + tab_w - lock_mar - lock_box;
            const int ly = lane.y + (content_h - lock_box) / 2;
            if (vs.read_only) {
                icons::draw(cr, icons::Icon::Lock,
                            static_cast<double>(lx), static_cast<double>(ly),
                            static_cast<double>(lock_box));
            } else {
                // The face this tab is actually wearing, which is the ground the
                // dim mixes toward.
                const GuiColor ground =
                    selected ? kRedesignTabGround
                             : (face.hovered ? kRedesignTabHover
                                             : kRedesignTabRest);
                icons::draw(cr, icons::Icon::Unlock,
                            static_cast<double>(lx), static_cast<double>(ly),
                            static_cast<double>(lock_box),
                            kRedesignDisabledMix, ground);
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
    // width, three vertical separators, and eleven 32x32 buttons in four groups
    // — the S/T and W/P view radios, the phase-reset copy/paste pair with the
    // bpm / iteration / follow modes, and the listen / commit render pair.
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
    // chord-dispatch ruling doing exactly the work it exists for.
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

        AppState::RedesignButtonFace& face =
            app.redesign_buttons[redesign_button_index(def.id)];
        face.rect     = GuiRect{x, btn_y, btn, btn};
        face.enabled  = redesign_button_enabled(app, audio.total_frames(),
                                                def.id);
        face.selected = redesign_button_selected(app, def.id);

        const bool pressed =
            app.redesign_pressed == redesign_button_index(def.id);

        // THE FILL AND THE OUTLINE ARE DECIDED SEPARATELY, which is exactly the
        // architect's reading of the five crops: the outline says "the pointer
        // is here" and the fill says "this is the state", so every combination
        // of the two falls out instead of being enumerated.
        const bool has_fill = pressed || face.selected;
        const bool has_line = face.hovered || pressed || face.selected;
        if (has_fill || has_line) {
            // ONE PATH, FILLED AND STROKED — not a fill on the full box under a
            // stroke on an inset one. The crops settle it: fitting both
            // constructions against the selected crop, the shared inset path
            // scores 227 where the full-box fill scores 270 at its own best
            // radius and 2129 at this one, and it is what the source widget does
            // (a single rounded rect drawn with both a brush and a pen). Sharing
            // the path also means the fill's edge and the stroke's centreline
            // cannot describe different rectangles.
            //
            // The inset is HALF THE STROKE on all four sides, so the band lands
            // on the box's outermost pixel ring with no straight edge
            // antialiased — rows 2 and 3's alignment term, both parities from
            // the one expression — and the radius insets by the same half so the
            // corner stays concentric with the box.
            const double half = static_cast<double>(lw) * 0.5;
            redesign_rounded_rect_path(cr, x + half, btn_y + half,
                                       static_cast<double>(btn - lw),
                                       static_cast<double>(btn - lw),
                                       radius - half);
            if (has_fill) {
                const GuiColor fill =
                    pressed ? mix_color(kRedesignAccent, kRedesignTabGround,
                                        kRedesignClickMix)
                            : kRedesignSelectedFill;
                cairo_set_source_rgb(cr, fill.r, fill.g, fill.b);
                if (has_line) cairo_fill_preserve(cr);
                else          cairo_fill(cr);
            }
            if (has_line) {
                // Accent when the pointer is on it or it is held; otherwise the
                // calm grey that frames a resting toggled-on button.
                const GuiColor line = (face.hovered || pressed)
                                          ? kRedesignAccent : kRedesignLine;
                cairo_set_source_rgb(cr, line.r, line.g, line.b);
                cairo_set_line_width(cr, static_cast<double>(lw));
                cairo_stroke(cr);
            }
        }

        if (def.glyph != nullptr) {
            // A LETTER BUTTON: the shaped glyph centered on BOTH axes — the
            // width from the run itself (never a font metric guess) and the
            // baseline from the shared extents solver.
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, def.glyph);
            cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                                 kRedesignLabel.b);
            text_shape::show_shaped_run(
                cr, run,
                static_cast<double>(x) +
                    std::nearbyint((static_cast<double>(btn) - run.width_px) *
                                   0.5),
                redesign_baseline(font, static_cast<double>(btn_y),
                                  static_cast<double>(btn)));
        } else {
            // An ICON BUTTON: the 22px box centered in the 32px button (+5 at
            // 100%), each path in its own color from the icon table. No dimming
            // term — this row has no disabled face.
            icons::draw(cr, def.icon,
                        static_cast<double>(x + (btn - glyph_px) / 2),
                        static_cast<double>(btn_y + (btn - glyph_px) / 2),
                        static_cast<double>(glyph_px));
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
    // each crop pinned its own pair — so the colors are the caller's and only
    // the geometry is shared. ONE PATH, filled then stroked, the construction
    // the row-4 fit settled: the fill's edge and the stroke's centreline
    // describe the same rectangle by construction.
    const int    lw   = popup_border_px();
    const double half = static_cast<double>(lw) * 0.5;
    const double rad  = std::nearbyint(kPopupCornerRadiusPx * gui_scale_factor());
    redesign_rounded_rect_path(cr, r.x + half, r.y + half,
                               static_cast<double>(r.w - lw),
                               static_cast<double>(r.h - lw), rad - half);
    cairo_set_source_rgb(cr, ground.r, ground.g, ground.b);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, border.r, border.g, border.b);
    cairo_set_line_width(cr, static_cast<double>(lw));
    cairo_stroke(cr);
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
        if (redesign_button_tooltip(id).line1 == nullptr) continue;
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
        redesign_button_tooltip(static_cast<RedesignButton>(hovered));
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

void GuiPaintHandler::paint_settings_popup(cairo_t* cr) {
    // THE SETTINGS DROPDOWN, hanging flush under the menu row's Settings button
    // at ZERO margin — its top edge IS the menu row's bottom edge, under the
    // button's left edge. Publishes its own rect and every item rect, so the
    // press claim hit-tests exactly what was painted and never re-shapes a
    // label.
    //
    // ITS CHROME IS ITS OWN (kRedesignPopupGround under kRedesignTabLine), not
    // the tooltip's: kdenlive dresses menus darker than hints, and dropdown_full
    // is the authority for this surface.
    //
    // NO ICONS, by ruling — the crop reserves an icon column and this product
    // does not, exactly as the tabs dropped theirs. The space that column would
    // have taken is what the labels' left pad is measured against.
    app.settings_popup.rect = GuiRect{0, 0, 0, 0};
    app.settings_popup.item_rects = {};
    if (!app.settings_popup.open) return;

    const GuiRect& btn =
        app.redesign_buttons[redesign_button_index(RedesignButton::Settings)].rect;
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
    const int label_pad = scaled_px(kPopupLabelPadLeftPx);
    const int sep_inset = scaled_px(kPopupSepInsetPx);
    const int sep_mar   = popup_sep_margin_y_px();
    const int sep_block = 2 * sep_mar + border;   // margin, line, margin
    const int lw        = border;
    const double radius = std::nearbyint(kPopupCornerRadiusPx *
                                         gui_scale_factor());

    // WIDTH FROM THE WIDEST SHAPED LABEL behind the authored minimum — the runs
    // are shaped once here and reused for the paint below, so the box and the
    // glyphs come from the same measurements (the displayed-basis doctrine).
    text_shape::ShapedRun runs[kSettingsPopupItemCount];
    double widest = 0.0;
    for (int i = 0; i < kSettingsPopupItemCount; ++i) {
        runs[i] = text_shape::shape_text_run(font, kSettingsPopupItems[i].label);
        widest = std::max(widest, runs[i].width_px);
    }
    const int item_w =
        std::max(scaled_px(kPopupItemMinWidthPx),
                 label_pad + static_cast<int>(std::nearbyint(widest)) +
                     label_pad);
    const int w = item_w + 2 * inset + 2 * border;
    // THE HEIGHT COMES FROM THE SHARED SUM, not a second walk here: the open
    // edge damages settings_popup_h_px() before this ever runs, so the two must
    // be one expression.
    const int h = settings_popup_h_px();

    int x = btn.x;
    int y = btn.y + btn.h;               // flush: zero margin under the row
    if (x + w > app.width) x = app.width - w;
    if (x < 0) x = 0;
    app.settings_popup.rect = GuiRect{x, y, w, h};

    paint_popup_chrome(cr, app.settings_popup.rect, kRedesignPopupGround,
                       kRedesignTabLine);

    // The item block opens BELOW the border by its own margin, and closes with
    // the same margin above the bottom border — the crop's 3px, mirrored.
    int iy = y + border + block_mar;
    for (int i = 0; i < kSettingsPopupItemCount; ++i) {
        if (kSettingsPopupItems[i].separator_before) {
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
        app.settings_popup.item_rects[static_cast<size_t>(i)] = item;

        const bool pressed = (app.settings_popup.pressed_item == i);
        const bool hovered = (app.settings_popup.hovered_item == i);
        if (pressed || hovered) {
            // TWO FACES FROM THE ITEM CROPS, and they are built differently
            // because one has an outline and the other does not:
            //   PRESSED — the FULL accent fill over the WHOLE item box. No
            //     stroke, so no inset: the fill's own edge is the visible edge.
            //     It is visible at all only because items act on RELEASE, the
            //     one redesign surface that does.
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
                const double half = static_cast<double>(lw) * 0.5;
                const GuiColor fill = mix_color(kRedesignAccent,
                                                kRedesignPopupGround,
                                                kRedesignClickMix);
                const GuiColor line = mix_color(GuiColor{1.0, 1.0, 1.0},
                                                kRedesignAccent,
                                                kRedesignHoverLightenMix);
                redesign_rounded_rect_path(cr, item.x + half, item.y + half,
                                           static_cast<double>(item.w - lw),
                                           static_cast<double>(item.h - lw),
                                           radius - half);
                cairo_set_source_rgb(cr, fill.r, fill.g, fill.b);
                cairo_fill_preserve(cr);
                cairo_set_source_rgb(cr, line.r, line.g, line.b);
                cairo_set_line_width(cr, static_cast<double>(lw));
                cairo_stroke(cr);
            }
        }

        // LEFT-ALIGNED at the pad, vertically centred by the shared solver. The
        // right side carries the leftover, which the minimum width above is what
        // guarantees.
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(
            cr, runs[i], static_cast<double>(item.x + label_pad),
            redesign_baseline(font, static_cast<double>(item.y),
                              static_cast<double>(item.h)));
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
    const int    wave_w    = waveform_area(app).w;
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
    const int major_top   = marker.y - static_cast<int>(std::nearbyint(
                                kRulerMajorRisePx * gui_scale_factor()));

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
    const int head_half_max = static_cast<int>(std::nearbyint(
        static_cast<double>(kPlayheadHeadHalf[0]) * gui_scale_factor()));
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
    // TIP-DOWN AT THE MARKER LANE'S TOP, centered on the playhead column — the
    // same column the stem below runs on, so head and stem are one object.
    //
    // THE HEAD MOVED OUT OF THE RULER LANE (architect 2026-08-01, at the row-5
    // live test). It sat on the ruler's bottom rows; it now occupies the MARKER
    // lane's TOP rows, and the ruler lane is labels + tick-tops only. The point
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
    // AND THIS PAINTER OWNS THE STEM'S MARKER-LANE SEGMENT TOO. render_playhead's
    // line begins at the waveform's top, so without this the lane between the
    // head's tip and the waveform would be blank. The segment lands here rather
    // than in paint_playheads for a reason that is not aesthetic: render_playhead
    // is SHARED WITH THE SCANNER, and the scanner is waveform-only by ruling (it
    // draws no head and belongs to no strip lane), so reaching that function up
    // into a strip lane would give the scanner a lane presence it must not have.
    // Here the segment sits inside the CURSOR-ONLY head block, under the same
    // column gate, and the pass order does the rest: ticks, then the head and
    // both its stem segments, then the marker stems (a disjoint band — the
    // waveform), then the flags on top.
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
            // THE POINT IS STEM PARITY: the playhead's stem now begins exactly
            // where every marker stem begins — the waveform top — so the two
            // read as the same object at the same length, which the top
            // position broke by giving the playhead an extra 8 rows of stem
            // inside the lane. The marker-lane segment below therefore shrinks
            // to ZERO by construction (head_bottom IS the waveform top), and
            // its own guard is what expresses that rather than a deletion: the
            // segment survives as the term that would reappear if the head ever
            // moved off the boundary again.
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

            // THE STEM'S MARKER-LANE SEGMENT: from the head's TIP ROW down to
            // the waveform's top edge, where render_playhead's own segment picks
            // it up. AT THE HEAD'S PRESENT POSITION THIS IS ZERO ROWS — the tip
            // stands on the waveform boundary — and the guard below is what
            // says so. It re-derives from head_bottom rather than from the lane
            // seam precisely so it follows the head: it was 8 rows at 100% when
            // the head sat at the lane's top, and it is 0 now, from the same
            // expression. It paints AFTER the
            // ticks, so a tick crossing the cursor's column in this lane passes
            // UNDER the stem, which is the stated order (ticks below the
            // head+stem); and before the flag blit, so a marker sharing the
            // column hides it — the hidden-by-marker model, which now reaches
            // the head as well as this segment.
            // AND IT SUPPRESSES WITH THE WAVEFORM SEGMENT (2026-08-01): the two
            // are one line, so where a marker's stem stands on the playhead's
            // frame neither half paints (the ruling is at
            // playhead_stem_suppressed; the head above is deliberately NOT
            // gated). At the head's present position the segment is already zero
            // rows, so this reads as belt — which is the point of keeping the
            // term: if the head ever leaves the waveform boundary again, the
            // remnant it grows back is suppressed with its other half rather
            // than reappearing as a stub.
            const int stem_h =
                playhead_stem_suppressed() ? 0 : (tick_bottom - head_bottom);
            if (stem_h > 0) {
                cairo_set_source_rgb(cr, kPlayheadStem.r, kPlayheadStem.g,
                                     kPlayheadStem.b);
                cairo_rectangle(cr, lane.x + col, head_bottom, 1, stem_h);
                cairo_fill(cr);
            }
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
    // worker completion), the blit is skipped and the kCanvas
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
    // kCanvas, or a kRegionCanvas recolor — the pass before
    // this one left. That is what makes ink over a highlighted span identical
    // to ink over plain canvas wherever coverage is full.
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
    c.lo_col = static_cast<int>(std::nearbyint(
        (static_cast<double>(lo) - basis.vp_start) / basis.spp));
    c.hi_col = static_cast<int>(std::nearbyint(
        (static_cast<double>(hi) - basis.vp_start) / basis.spp));
    return c;
}

// -- GuiPaintHandler::paint_region_ground --------------------------------

// THE REGION HIGHLIGHT IS A GROUND RECOLOR (the Ableton model, architect
// 2026-07-26): the span's CANVAS becomes the opaque kRegionCanvas over the full
// content height. Called from on_redraw after render_canvas and BEFORE
// paint_waveform_plate, so the ARGB32 plate composites over the recolored
// ground and its antialiased fringes blend against it — the ink over a
// highlighted span is bit-identical to ink over plain canvas wherever coverage
// is full, and only the ground carries the highlight. The retired form was a
// translucent wash painted OVER the plate, which lifted the ink itself —
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

    // Columns: left_col uses the same std::nearbyint-to-int placement the stem
    // renderer uses, so the overlay's left edge stays on the marker's column.
    // right_col is a fixed whole-pixel offset ahead of it, so the far edge
    // tracks the marker in lockstep instead of wobbling by independent
    // per-endpoint rounding. width_px is the overlay span banker's-rounded to
    // whole pixels — an approximate but rigid forward extent, which beats an
    // exact but jittering one (the span is an authoring aid, not an engine
    // point).
    const int left_col =
        static_cast<int>(std::nearbyint((ms - vp_start) / spp));
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
    // redesign's colour ruling, superseding the tunable kOverlayOutline, whose
    // ONE paint site this was; that key is now declared and inert like kCanvas,
    // kWaveform, kLine and kStripAnchorStem. It reads the same constant the
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

// The LIVE trim pass (architect 2026-07-25 — trim z-order below the playhead):
// every trim pixel — both b/e chips, the bridge bar, the strip-crossing stem
// segments, and the waveform stem segments — paints here per frame, in the old
// trim-stem-cache slot (after the phase-reset overlay's ring, before
// paint_marker_stems and hence before every playhead element), so the playhead
// stem sits OVER a trim stem sharing its column while marker flags
// stay above the playheads (the z-order flip untouched). "Markers over trim" is
// now STRUCTURAL pass order — trim < selected stem < playheads < flag blit —
// not an intra-cache paint convention; the two-segment stem join (strip segment
// (retired with render_trim_stems, 2026-08-01) lived in
// this ONE pass instead of joining bit-exactly across two caches.
//
// BASIS: the FREE item-geometry owners — item_viewport_basis(app, audio)
// and displayed_or_live_target_map(app, audio) — feeding the shared geometry
// owners displayed_trim_ms / trim_bound_column / trim_bridge_gap /
// trim_chip_rect inside the two renderers, so paint stays column-coherent with
// hit_test_trim_chip / route_trim_chip_press, which read exactly that basis
// (paint == hit by shared owners). Deliberately NOT the member
// GuiPaintHandler::plate_viewport_basis(): that is the PLATE-fingerprint
// basis for plate-registered overlays, and the two differ inside the accepted
// resize item-only-promotion window — trim must ride the ITEM basis the chips'
// hit rects resolve on. The renderers' column math therefore divides the
// basis span by basis.area_w (the width the committed items were mapped
// against), which is why the waveform rect handed to them carries that width.
//
// COLD STATES (nothing promoted yet — first paint after load/adopt, the view
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
    // No trim gate: the window is ALWAYS set (2026-07-30), so the chips, the
    // stems and the bridge bar simply always paint — at the full window the
    // chips rest on the song edges and the bar spans between them.
    if (area.w <= 0 || area.h <= 0) return;
    if (top_strip.w <= 0 || top_strip.h <= 0) return;

    // The ITEM basis (free owner; the member plate_viewport_basis is the other
    // epoch — see the header comment above).
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.area_w <= 0 || basis.spp <= 0.0) return;

    // The item pixels' map: empty (identity) in source view, the committed
    // displayed map (live fallback cold) in target view — exactly
    // hit_test_trim_chip's selection.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* map_arg =
        dmap.empty() ? nullptr : &dmap;

    // Per-bound displayed-domain positions through the shared mapping owner
    // (displayed_trim_ms returns an integral-valued double; the int64 round
    // trip through TrimRange is exact, so trim_bound_column sees the same
    // value the hit sites pass). Both bounds are always meaningful.
    TrimRange trim{
        static_cast<int64_t>(displayed_trim_ms(app.trim.begin_frame, map_arg)),
        static_cast<int64_t>(displayed_trim_ms(app.trim.end_frame, map_arg))};

    // Waveform rect for the renderers: real screen origin/height, width =
    // basis.area_w (the committed item width — the column-mapping denominator
    // and the [0, wave_w) painter clip, keeping paint == hit through the
    // accepted resize window; equal to waveform_area(app).w at rest).
    const GuiRect wave_rect{area.x, area.y, basis.area_w, area.h};

    // ONE HALF ONLY since 2026-08-01: the waveform stem pass is deleted, so
    // this is the strip's bar + endcaps (with the side-aware offscreen
    // sentinels and the effective-width clip inside render_trim_flags).
    //
    // The chip lane's y-band is THREADED IN as top_trim_row_area(app) rather
    // than re-derived inside the painter: this is the same accessor
    // hit_test_trim_chip's y-gate and route_trim_chip_press' bridge y-gate
    // read, so the painted band and the clickable band have ONE owner and
    // cannot drift if the lanes above the chip row ever change.
    // NO WAVEFORM STEMS (architect 2026-08-01): the bar and its two endcaps are
    // the trim window's WHOLE display. render_trim_stems drew a 1px kTrimStem
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
    const int col = static_cast<int>(std::nearbyint(
        (app.strip_drag.anchor_sample - vp_start) / spp));
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
// WHAT IT FIXES: the two stems are derived through DIFFERENT spp arithmetic —
// marker stems publish from the flag-cache rebuild over waveform_area(app).w,
// the playhead from playhead_pixel_x against plate_viewport_basis — so at some
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
    // ruler lane's aliased head, and paint_ruler_row owns that head (it needs
    // the tick columns for the pre-blended crossing) along with the stem's
    // marker-lane segment. So both calls below pass draw_triangle=false and this
    // pass is the WAVEFORM segment of the stem, plus the scanner's line. The
    // lane rect is still threaded through — render_playhead requires it
    // unconditionally so a triangle-drawing call can never omit it. WHICH lane
    // it names is now arbitrary — the head moved to the MARKER lane in
    // 2026-08-01 and this pass draws nothing in either.
    const GuiRect tri_lane = top_ruler_row_area(app);

    // Playheads paint UNDER the marker flags (the Z-ORDER FLIP, architect
    // 2026-07-23 — see the paint-order block in on_redraw): the cursor's line
    // passes beneath a marker flag sharing its column, so a cursor resting on a
    // marker sits hidden behind that marker's flag. The scanner line is
    // waveform-only and has no strip presence at all, so its stacking is
    // unaffected; the cursor still draws over the marker STEMS below it in the
    // waveform. Gated on the waveform OR the top strip being exposed: the head
    // and the marker-lane stem segment live in the strip and are repainted by
    // paint_ruler_row on the same frame, so a strip-only damage must reach both
    // passes.
    //
    // Paint order: scanner first (gated on playhead_scanner_active), then the
    // cursor. The cursor draws over the scanner on overlap.
    if (app.playhead_scanner_active) {
        const double scan_px = scanner_pixel_x(app, wf_cache.fp_vp_start,
                                               disp_spp);
        render_playhead(cr, area, tri_lane, scan_px, kPlayheadScanner,
                        /*draw_triangle=*/false);
    }

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
    // previewed by the lower-half scrub press, consumed by `x`), so it dissolves
    // nothing and suppresses nothing, and the split half-triangle renderer is
    // deleted outright. The non-empty-selection suppression is
    // gone too: a cursor resting ON the focused marker is simply hidden behind
    // that marker's flag by the z-order flip, which is what the old else-arm was
    // spelling out by not painting — and when the arrows move the focused marker
    // the cursor rides along VISIBLY, which is the lane model's honest reading.
    // The region ground still paints under the plate (paint_region_ground); the
    // cursor line crosses it exactly as it crosses waveform ink.
    // THE TRIANGLE IS OFF EVERYWHERE (row 5): the cursor's tip-down triangle
    // retired with the triangle lane, and its successor — the ruler lane's
    // aliased head — is painted by the ruler pass, which owns the tick columns
    // the head's pre-blended crossing needs. So this call is the stem's WAVEFORM
    // segment; the ruler pass draws the head and the marker-lane segment above
    // it, and the three make one unbroken line.
    // THE STEM IS kPlayheadStem NOW (#fcfcfc), superseding the old cursor line's
    // color at this surface: the head above it is the playhead's identity, and
    // the stem is that head's line continued down through the waveform.
    //
    // Z-INTENT, stated now and to be verified when the marker painter lands:
    // ruler ticks, then the playhead head + stem, then the marker flags and
    // their stems on top. That order is the HIDDEN-BY-MARKER model translated —
    // a flag sharing the cursor's column hides it, exactly as flags painted over
    // the old triangle — and it is also why the stem is drawn to run OVER the
    // waveform's own borders when row 6 adds them: the stem is a boundary line
    // like the marker stems beside it, not a thing the borders clip.
    if (!playhead_stem_suppressed()) {
        render_playhead(cr, area, tri_lane, px_x, kPlayheadStem,
                        /*draw_triangle=*/false);
    }
}

// -- GuiPaintHandler::paint_bottom_strip ---------------------------------

void GuiPaintHandler::paint_bottom_strip(cairo_t* cr, int sr) {
    // ROW 7 — THE BOTTOM STRIP IS ONE LINE (architect 2026-08-01). The status
    // row and the modal/editor row collapsed into a single lane of THREE FIXED
    // SECTIONS: the TIMESTAMP, the DIRTY DOT, and — when one applies — the
    // active modal / editor / prompt / status text in the span after them. The
    // boundaries come from shaped maxima and never from the current text, so
    // nothing on the line moves when the clock grows a digit or the dot
    // appears (bottom_row_sections, at the top of this file, with the layout).
    //
    // WHAT DIED WITH THE COLLAPSE, and why it is not missing: the S/T · W/P ·
    // A/B view readout and the "(read-only)" token. Rows 3 and 4 display all
    // three view states as lit buttons and tabs, and the tab locks show
    // read-only, so the letters were restating what the redesigned rows say in
    // their own vocabulary.
    //
    // PRECEDENCE IN THE AFTER-TIMESTAMP SPAN, highest first: prompt > queue /
    // loading status > settings editor > commit editor > BPM editor > transient
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

    // --- Section A: the timestamp. sr is the loaded file's sample rate and the
    //     playhead samples are source-frames. Split-playhead: track the scanner
    //     during playback (what the user hears), the cursor otherwise (the
    //     scanner is meaningful only while active, so the ternary takes the
    //     cursor at rest). The old paint-site clamp at 5999.999 is GONE with the
    //     fixed section: format_timestamp owns the cap, and a second clamp here
    //     would silently cut the display an octave below the format's own bound.
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

    // --- Section B: the dirty dot, in its own reserved cell. It KEEPS ITS GLYPH
    //     (a bare '*') — the architect ruled the form unchanged and the crop
    //     says nothing about it — and its cell exists whether or not it shows,
    //     so appearing and disappearing moves nothing.
    if (app.dirty) {
        show_row_text(cr, font, sec.b_x, baseline, "*", kRedesignLabel);
    }

    // --- Section C: the modal / editor / status chain, in the span that runs
    //     from the last fixed boundary to the window's right edge. ---
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
    } else if (text_editor::is_active(app.commit_editor)) {
        // Render-commit prompt overlay: "commit: ./renders/<pending>", through
        // the same shared body; its red flash is an unresolved / bad commit.
        render_bottom_strip_editor(cr, app, font, app.commit_editor,
                                   kCommitEditorPrefix,
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
        // "no renders to commit", ...). It used to ride the status line as an
        // appendix after the dirty dot; with one line and one span it takes its
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
    cairo_restore(cr);
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
    // the last COMMITTED frame's geometry — that guarantee is unchanged. Bump
    // displayed_map_gen so the promotion has a record.
    //
    // THE HOVER REFRESH HOOK THAT HUNG OFF THIS EDGE IS GONE (row 5): it
    // re-resolved the marker hover cache against the just-promoted map, and
    // there is no hover cache any more. Nothing subscribes to the promotion; the
    // overlays below simply read the promoted basis.
    if (app.staged_displayed_valid) {
        app.displayed_target_warp_frame_map =
            std::move(app.staged_displayed_target_warp_frame_map);
        app.staged_displayed_target_warp_frame_map.clear();
        // Promote the displayed VIEWPORT mirror in the SAME block (one promote,
        // one gen bump): the flag editor's box geometry advances to the fp_*
        // viewport the just-blitted flag cache was built against, in
        // lockstep with the map above.
        app.displayed_vp_start = app.staged_displayed_vp_start;
        app.displayed_vp_end   = app.staged_displayed_vp_end;
        app.displayed_area_w   = app.staged_displayed_area_w;
        app.staged_displayed_valid = false;
        ++app.displayed_map_gen;
    }

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);

    render_background(cr, x, y, w, h);
    // THE GROUND SPLIT: the chrome erase above covers the whole exposed rect;
    // the waveform area then takes the lighter kCanvas ground. Unconditional and
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
        // Final paint order (bottom to top of the stack): canvas ground + its
        // 2px black border (painted above, unconditionally) -> region ground ->
        // waveform plate -> overlay ring -> LIVE
        // TRIM (bar + endcaps + waveform stem segments, one pass)
        // -> playheads (scanner line + cursor stem) -> MARKER STEMS -> ruler ->
        // flag blit -> flag editor overlay -> strip-drag anchor. (The bottom row
        // left the tail of this sequence in row 7 — it paints with the other
        // redesigned rows above, on every frame class, and overlaps none of
        // these passes.)
        // Three structural rulings live in this sequence:
        //   THE RECOLOR MODEL (architect 2026-07-26) — a highlight changes the
        //     GROUND, so the ONE ground recolor (the region's) paints BEFORE the
        //     plate and the ink composites over it. The phase-reset overlay
        //     contributes no ground at all (architect 2026-07-27): its 1px RING
        //     is its whole visual, and a boundary line paints AFTER the plate,
        //     crossing the ink like the stems do.
        //   THE Z-ORDER FLIP (architect 2026-07-23) — the cursor playhead (its
        //     line+triangle) passes UNDER
        //     marker flags, so a cursor resting on a marker sits hidden behind
        //     that marker's flag (identical 15-wide triangle geometry at the
        //     same column); the selected-marker focus triangles are GONE
        //     (architect 2026-07-25 — a singleton's focus is its STEM, a
        //     group's is its members' ink triangles plus the landed cursor).
        //   TRIM BELOW THE PLAYHEAD (architect 2026-07-25) — every trim pixel
        //     paints before every playhead element, so the playhead sits over a
        //     trim stem sharing its column: trim < playheads < marker stems <
        //     marker flags. (Row 5 moved the marker stems ABOVE the playheads,
        //     where the singleton selected stem used to sit below them — the
        //     hidden-by-marker z-intent; the trim half of the rule is
        //     untouched.)

        if (rects_intersect(exposed, area)) {
            // THE GROUND RECOLOR, under the plate. render_canvas already laid
            // the kCanvas ground for the whole area above; this repaints the
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
        // pixels (waveform stems AND the strip's chips/bridge/stem segments).
        // Gated on EITHER half being exposed: render_background erased every
        // exposed top-strip pixel above, so a strip-only damage (hover text, a
        // flag change) must repaint the strip-resident trim pixels, and a
        // waveform-only damage the stem segments; the outer Cairo damage clip
        // bounds the actual work either way.
        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            paint_trim(cr, area, top_strip);
        }

        // Playheads BEFORE the flag blit (Z-ORDER FLIP, architect 2026-07-23):
        // the scanner line stays waveform-only (triangle-free, no lane conflict —
        // its stacking vs the lanes is unaffected by this move), while the cursor
        // line+triangle now paints UNDER the
        // marker flags that follow. flag_cache.surface is ARGB32, CLEAR-cleared
        // each rebuild and transparent outside the painted shapes, so the flag
        // blit composites source-over and never erases the playheads it does not
        // cover. Gated on area OR top_strip: the cursor line lives in the waveform
        // area, its head and marker-lane segment in the top strip.
        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            paint_playheads(cr, area);
        }

        // MARKER STEMS AFTER THE PLAYHEADS (row 5's z-intent, now verifiable
        // because both exist): ruler ticks, then the playhead head + stem, then
        // the marker flags and their stems ON TOP. That is the hidden-by-marker
        // model translated — a marker sharing the cursor's column hides it,
        // exactly as flags painted over the old triangle — and it is why the
        // stems paint here rather than in the pre-playhead slot the singleton
        // selected stem occupied. The flag BOXES follow in the strip blit below;
        // the stems are their waveform half and must not be split across the
        // playhead by paint order.
        if (rects_intersect(exposed, area)) {
            paint_marker_stems(cr, area);
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
    paint_settings_popup(cr);
    paint_shift_tooltip(cr);

    cairo_restore(cr);

    // Force any pending Cairo ops out to the X server. The subsequent flush
    // in GuiPlatform::dispatch_event is then a cheap no-op.
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
