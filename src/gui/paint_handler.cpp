#include "paint_handler.h"

#include "icons.h"
#include "render.h"
#include "text_display.h"
#include "text_editor.h"
#include "text_shape.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"
#include "engine/engine_geometry.h"  // kRs

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// On-screen paint handler: on_redraw and its per-strip paint passes, and
// on_resize. The off-screen surfaces
// these passes blit — the waveform plate and the flag-rect
// cache — are produced in waveform_cache.cpp. Trim paints live per frame
// (paint_trim), out of any cache.

// The settings-prompt editor and the BPM editor paint the same
// bottom-strip text box through render_editor_text_box, differing only
// in the prefix and which text_editor::State they read. This is the one
// body both branches share. It takes the row geometry (anchor_x,
// baseline_y) the caller already solved (upper_baseline) rather than
// computing a row of its own, so the two call sites stay the single
// source for where the bottom-strip editor sits.
static void render_bottom_strip_editor(cairo_t* cr,
                                       const text_editor::State& ed,
                                       const char* prefix,
                                       double anchor_x,
                                       double baseline_y) {
    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, flag_font_size_px());

    EditorTextBox box;
    box.anchor_x        = anchor_x;
    box.baseline_y      = baseline_y;
    box.prefix          = prefix;
    box.text            = ed.pending;
    // hl_pad is the glyph inset (ring + pad); anchor_x here is the caller-solved
    // glyph origin, so this back-derivation keeps the invisible ring geometry
    // consistent with the chip renderers even though the box body reads as plain
    // light text on the dark strip.
    box.hl_pad          = flag_glyph_inset_px();
    // The normal-state ring and fill are both the background color, so the box
    // body is the same as a chip's but invisible — light text on dark bg; the
    // red flash colors match a parse-fail chip.
    box.fill            = ed.red ? kAccent        : kBackground;
    box.outline         = ed.red ? kAccentOutline : kBackground;
    box.text_color      = kText;
    box.has_selection   = text_editor::has_selection(ed);
    box.selection_start = text_editor::selection_start(ed);
    box.selection_end   = text_editor::selection_end(ed);
    box.cursor_visible  = text_editor::cursor_visible_now(ed);
    box.cursor_pos      = ed.cursor_pos;
    render_editor_text_box(cr, box);

    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_flag_annotations -----------------------------

void GuiPaintHandler::paint_flag_annotations(cairo_t* cr,
                                             const GuiRect& top_strip) {
    // Flag annotations in the top strip. The fixed-width marker/phase-reset
    // flag shapes live on
    // flag_cache.surface (rebuilt from on_tick via maybe_rebuild_flag_cache);
    // this pass is a pure blit. (Trim's b/e chips left this cache for the live
    // paint_trim pass, which runs BEFORE the playheads — the z-order ruling.)
    // The flag shapes are textless; a marker's flag
    // payload text (and the hover popup) surface in the marker-text lane, painted
    // live per-frame in paint_marker_text_lane after this blit — the editing
    // target's flag paints here as an ordinary selected shape. Like the other
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

// THE KDENLIVE REDESIGN'S SHARED TEXT FACE, in 100%-scale pixels. Every
// redesigned row's label is 12pt through the existing points*4/3 convention =
// 16px at 100%, scaled on gui_scale_factor() — the redesign's own axis, NOT the
// monospace font's (the ruling is at gui_scale_factor's declaration). THE SANS
// FACE IS THE REDESIGN'S DEFAULT FAMILY: cairo's toy "sans" selector, which
// fontconfig resolves to Liberation Sans on the target — the family the crops
// were rendered in. The monospace face survives untouched on every
// un-redesigned surface, and each redesign row moves its own text over.
constexpr double kRedesignFontSizePt = 12.0;   // -> 16.0 px at 100%

double redesign_font_size_px() {
    return kRedesignFontSizePt * 96.0 / 72.0 * gui_scale_factor();
}

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
// AT THE MINIMUM THE LABEL IS CENTERED IN THE WHOLE TAB, not left-padded — the
// two paddings are a FLOOR term now, not an anchor, so a label narrower than the
// minimum sits in the middle of the box rather than hugging its left edge.
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
    {RedesignButton::IconCopy,   IconRowLead::Separator, nullptr, icons::Icon::EditCopy},
    {RedesignButton::IconPaste,  IconRowLead::Gap,       nullptr, icons::Icon::EditPaste},
    {RedesignButton::IconBpm,    IconRowLead::Gap,       nullptr, icons::Icon::MusicNote16th},
    {RedesignButton::IconIter,   IconRowLead::Gap,       nullptr, icons::Icon::MediaPlaylistRepeat},
    // Follow's provisional "F" letter gave way to the arrow the architect chose
    // (2026-07-31): media-seek-forward, the double-triangle that reads as
    // "chase what is playing".
    {RedesignButton::IconFollow, IconRowLead::Gap,   nullptr, icons::Icon::MediaSeekForward},
    {RedesignButton::IconListen, IconRowLead::Separator, nullptr, icons::Icon::PreviewRenderOn},
    {RedesignButton::IconCommit, IconRowLead::Gap,       nullptr, icons::Icon::DialogOkApply},
};

// -- THE FLOATING SURFACES: the shift tooltip and the settings dropdown -----
//
// Measured off hover_shift.png (129x41, the two-line form we ship) and
// hover_plain.png (112x26, the one-line reference we do not). Both share every
// chrome pixel; the colors and the dim factor live in render.h.
//
// THE CORNER RADIUS IS THE REDESIGN'S ONE RADIUS, 5, and that is a CHOICE over a
// half-pixel: fitting the crop's corner (fill under border over the window
// ground) by summed squared per-channel error minimises at a PATH radius of 5.0
// (1034) with 4.5 next (1686) and 3.0 far behind (18008), and 4.5 is what the
// authored 5 becomes under the half-stroke inset every rounded surface here
// takes. Authoring 5.0 as a PATH radius would score marginally better but would
// make this the one surface whose constant means something different from rows
// 1-4's. (The brief's eyeball estimate of 2-3 is well outside the fit.)
constexpr double kPopupCornerRadiusPx = 5.0;

// THE TOOLTIP, in the crop's own numbers. Two 15px line SLOTS whose block
// starts 5px below the top border — which puts the two baselines at 18 and 33,
// the crop's own, through the shared extents solver rather than by spelling
// them — inside a 41px box, with 5px of horizontal padding each side. The
// one-line crop's 26px total is the sibling anatomy the dropdown's item height
// borrows from (26 less its two 1px borders = 24).
constexpr double kTooltipPadXPx      = 5.0;
constexpr double kTooltipPadTopPx    = 5.0;
constexpr double kTooltipLineStepPx  = 15.0;
// (The box height and the hover dwell live in render.h — the run loop reads
// both, for the due-check and for the damage band under the strip.)

// THE TOOLTIP'S TEXT, on the two shift-admitting buttons and nowhere else
// (redesign_button_shift_admits, app_state.h, is the one owner of that
// membership — this table is keyed off the same fact, so the hint cannot appear
// where a shift press does nothing).
//
// LINE 2 IS A GESTURE HINT, and the standing preference is that UI text never
// hints gestures — the user reads HELP. THE ARCHITECT COMMISSIONED THIS ONE
// EXPLICITLY (2026-07-31, adopting kdenlive's own wording), so it is a ruled
// exception scoped to exactly these two buttons, not a softening of the rule.
struct TooltipDef {
    RedesignButton id;
    const char*    line1;   // kdenlive's pattern: name + its chord
    const char*    line2;
};
constexpr const char* kTooltipShiftLine = "Press Shift for more.";
constexpr TooltipDef kTooltips[] = {
    {RedesignButton::Render,    "Render (Ctrl+Alt+R)",             kTooltipShiftLine},
    {RedesignButton::IconPaste, "Paste phase resets (Ctrl+Alt+P)", kTooltipShiftLine},
};

// THE DROPDOWN, in the architect's CSS terms. The item height is the ONE-LINE
// TOOLTIP'S INTERIOR (26 total less its two 1px borders = 24), which is what
// "pick a one-line item height from the tooltip's single-line anatomy" resolves
// to and keeps the two floating surfaces built from one set of numbers.
//
// The width derives rather than being authored: the widest shaped label, plus
// the redesign's standing 10px label padding per side (rows 1 and 3's), plus the
// 3px item inset per side, plus the two 1px borders. The architect pixel-tweaks
// at 100% by moving these terms.
// (The item height, the separator's vertical margin and the border live in
// render.h with settings_popup_h_px's other ingredients — the popup's OPEN EDGE
// must size the box before it is painted. Only the HORIZONTAL terms, which
// depend on the widest shaped label, are the painter's alone.)
constexpr double kPopupItemInsetPx   = 3.0;   // the highlight box, per side
constexpr double kPopupLabelPadPx    = 10.0;  // inside the highlight box
constexpr double kPopupSepInsetPx    = 7.0;   // the separator, per side

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
        const int tab_w   = std::max(min_w, label_w + 2 * pad);

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
                std::nearbyint((static_cast<double>(tab_w) - run.width_px) *
                               0.5),
            redesign_baseline(font, static_cast<double>(lane.y),
                              static_cast<double>(content_h)));

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

void GuiPaintHandler::paint_popup_chrome(cairo_t* cr, const GuiRect& r) {
    // ONE CHROME FOR BOTH FLOATING SURFACES — the tooltip and the dropdown are
    // the same box (the two crops are byte-identical in every chrome pixel), so
    // they share this and cannot drift apart. ONE PATH, filled then stroked, the
    // construction the row-4 fit settled: the fill's edge and the stroke's
    // centreline describe the same rectangle by construction.
    const int    lw   = popup_border_px();
    const double half = static_cast<double>(lw) * 0.5;
    const double rad  = std::nearbyint(kPopupCornerRadiusPx * gui_scale_factor());
    redesign_rounded_rect_path(cr, r.x + half, r.y + half,
                               static_cast<double>(r.w - lw),
                               static_cast<double>(r.h - lw), rad - half);
    cairo_set_source_rgb(cr, kRedesignRowGround.r, kRedesignRowGround.g,
                         kRedesignRowGround.b);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, kRedesignLine.r, kRedesignLine.g, kRedesignLine.b);
    cairo_set_line_width(cr, static_cast<double>(lw));
    cairo_stroke(cr);
}

void GuiPaintHandler::paint_shift_tooltip(cairo_t* cr) {
    // THE TWO-LINE SHIFT TOOLTIP, on whichever of the two shift-admitting
    // buttons is hovered — at most one, because at most one button is hovered.
    // The tick owns WHEN it appears (the dwell); this owns only what it looks
    // like, and publishes the rect it painted so the hide edge can damage it.
    app.redesign_tooltip.rect = GuiRect{0, 0, 0, 0};
    if (!app.redesign_tooltip.visible) return;

    const TooltipDef* def = nullptr;
    for (const TooltipDef& t : kTooltips) {
        const AppState::RedesignButtonFace& f =
            app.redesign_buttons[redesign_button_index(t.id)];
        // A DISABLED BUTTON ADVERTISES NOTHING: a greyed Render's shift chord is
        // as refused as its plain one, so it gets no hint. The hover recompute
        // already refuses to hover a disabled button, so this is the belt to
        // that braces — cheap, and it keeps the rule stated where it is visible.
        if (f.hovered &&
            redesign_button_enabled(app, audio.total_frames(), t.id)) {
            def = &t;
            break;
        }
    }
    if (def == nullptr) return;

    const GuiRect& btn =
        app.redesign_buttons[redesign_button_index(def->id)].rect;
    if (btn.w <= 0 || btn.h <= 0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const text_shape::ShapedRun r1 = text_shape::shape_text_run(font, def->line1);
    const text_shape::ShapedRun r2 = text_shape::shape_text_run(font, def->line2);

    const int pad_x = scaled_px(kTooltipPadXPx);
    const int w = static_cast<int>(std::nearbyint(std::max(r1.width_px,
                                                           r2.width_px))) +
                  2 * pad_x;
    const int h = tooltip_h_px();

    // BELOW THE BUTTON, LEFT-ALIGNED WITH IT, then CLAMPED FULLY ON-WINDOW so a
    // button near the right edge cannot push it off. The clamp is a pure
    // position fix — the box never shrinks, because a truncated hint would be
    // worse than one that shifted.
    int x = btn.x;
    int y = btn.y + btn.h;
    if (x + w > app.width) x = app.width - w;
    if (x < 0) x = 0;
    if (y + h > app.height) y = app.height - h;
    if (y < 0) y = 0;
    app.redesign_tooltip.rect = GuiRect{x, y, w, h};

    paint_popup_chrome(cr, app.redesign_tooltip.rect);

    // Two 15px slots from the top padding, each baseline solved by the shared
    // extents centrer — the crop's 18 and 33 fall out rather than being spelled.
    const int step  = scaled_px(kTooltipLineStepPx);
    const int top   = y + scaled_px(kTooltipPadTopPx);
    cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                         kRedesignLabel.b);
    text_shape::show_shaped_run(
        cr, r1, static_cast<double>(x + pad_x),
        redesign_baseline(font, static_cast<double>(top),
                          static_cast<double>(step)));
    // The hint line is DIMMED by the one measured factor (kRedesignDimMix),
    // uniformly — the crop emphasises no word inside it.
    const GuiColor dim =
        mix_color(kRedesignLabel, kRedesignRowGround, kRedesignDimMix);
    cairo_set_source_rgb(cr, dim.r, dim.g, dim.b);
    text_shape::show_shaped_run(
        cr, r2, static_cast<double>(x + pad_x),
        redesign_baseline(font, static_cast<double>(top + step),
                          static_cast<double>(step)));

    cairo_restore(cr);
}

void GuiPaintHandler::paint_settings_popup(cairo_t* cr) {
    // THE SETTINGS DROPDOWN, hanging flush under the menu row's Settings button
    // at ZERO margin — its top edge IS the menu row's bottom edge, under the
    // button's left edge. Publishes its own rect and every item rect, so the
    // press claim hit-tests exactly what was painted and never re-shapes a
    // label.
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
    const int inset     = scaled_px(kPopupItemInsetPx);
    const int label_pad = scaled_px(kPopupLabelPadPx);
    const int sep_inset = scaled_px(kPopupSepInsetPx);
    const int sep_mar   = popup_sep_margin_y_px();
    const int sep_block = 2 * sep_mar + border;   // margin, line, margin

    // WIDTH FROM THE WIDEST SHAPED LABEL — the runs are shaped once here and
    // reused for the paint below, so the box and the glyphs come from the same
    // measurements (the displayed-basis doctrine, again).
    text_shape::ShapedRun runs[kSettingsPopupItemCount];
    double widest = 0.0;
    for (int i = 0; i < kSettingsPopupItemCount; ++i) {
        runs[i] = text_shape::shape_text_run(font, kSettingsPopupItems[i].label);
        widest = std::max(widest, runs[i].width_px);
    }
    const int w = static_cast<int>(std::nearbyint(widest)) +
                  2 * label_pad + 2 * inset + 2 * border;
    // THE HEIGHT COMES FROM THE SHARED SUM, not a second walk here: the open
    // edge damages settings_popup_h_px() before this ever runs, so the two must
    // be one expression.
    const int h = settings_popup_h_px();

    int x = btn.x;
    int y = btn.y + btn.h;               // flush: zero margin under the row
    if (x + w > app.width) x = app.width - w;
    if (x < 0) x = 0;
    app.settings_popup.rect = GuiRect{x, y, w, h};

    paint_popup_chrome(cr, app.settings_popup.rect);

    int iy = y + border;
    for (int i = 0; i < kSettingsPopupItemCount; ++i) {
        if (kSettingsPopupItems[i].separator_before) {
            // 1px line, inset horizontally, with its own vertical margin against
            // the item on each side. Pixel-bound fill, crisp by construction.
            cairo_set_source_rgb(cr, kRedesignLine.r, kRedesignLine.g,
                                 kRedesignLine.b);
            cairo_rectangle(cr, x + sep_inset, iy + sep_mar,
                            w - 2 * sep_inset, border);
            cairo_fill(cr);
            iy += sep_block;
        }
        // ITEMS TOUCH — zero vertical gap between adjacent ones — and each
        // one's HIGHLIGHT box insets horizontally from the border. The published
        // rect is the highlight box, so the clickable area is exactly the area
        // that lights.
        const GuiRect item{x + inset, iy, w - 2 * inset, item_h};
        app.settings_popup.item_rects[static_cast<size_t>(i)] = item;

        if (app.settings_popup.hovered_item == i) {
            // The Breeze menu model, and the menu-row pill's sibling: a flat
            // accent fill under an unchanged white label.
            cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                 kRedesignAccent.b);
            cairo_rectangle(cr, item.x, item.y, item.w, item.h);
            cairo_fill(cr);
        }

        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(
            cr, runs[i],
            static_cast<double>(item.x) +
                std::nearbyint((static_cast<double>(item.w) -
                                runs[i].width_px) * 0.5),
            redesign_baseline(font, static_cast<double>(item.y),
                              static_cast<double>(item.h)));
        iy += item_h;
    }

    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_marker_text_lane -----------------------------

void GuiPaintHandler::paint_marker_text_lane(cairo_t* cr) {
    // The marker-text lane (top lane 5, between the trim chips and the flags).
    // THE OCCLUSION MODEL: the lane shows EVERY onscreen marker's text ambiently
    // when the whole visible set fits unoccluded at the 9-glyph budget, else it
    // falls back to the ONE-run arbitration (hover, else last-selected). Every
    // ambient run is CAPPED at the budget (truncation is permanent). Two OVERLAYS
    // paint on top of the ambient runs (each suppresses its own marker's ambient
    // run, then draws its replacement last — the same pattern): the TEXT-HOVER
    // EXPANSION (a run whose full text exceeds the budget, drawn in full while its
    // TEXT is hovered) and, above it, the FlagPayload editor box. All paint live
    // here (per-keystroke editor, per-motion hover, per-frame ambient set), after
    // the flag-cache blit — no cache, the live-overlay role the bottom strip's
    // editor/hover paints had before the lane existed. Each run centers its
    // monospace text over its marker's painted column and clamps it fully onscreen
    // (lane_text_left_x_at_frame) on a kBackground fill behind the run with no
    // border. The editor box flashes its fill kAccent on an invalid commit.
    const GuiRect lane      = top_marker_text_row_area(app);
    const double  baseline  = lane.y + monospace_text_row_baseline_offset();
    const double  advance   = monospace_advance();
    if (advance <= 0.0) return;

    const bool editor_active =
        text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::FlagPayload;
    const int editor_target = editor_active ? app.top_flag_editor.target : -1;

    // A run's marker is DISABLED — the glyph half of the opaque disabled cue
    // whose shape half is the flag's kMarkerDisabled pair. Runs are indexed in
    // the ACTIVE column's store (see LaneTextRun), so the verdict follows that
    // column: the warp side respects the label_ref cascade through
    // effective_disabled, the phase-reset side reads its bool (it has no
    // cascade) — the same split the selection walk's disabled-skip uses.
    const bool phase_reset_column = app.active_markers_view == 'P';
    auto run_disabled = [&](int idx) -> bool {
        if (idx < 0) return false;
        if (phase_reset_column) {
            const auto& pv = app.phaseresetmarkers.markers();
            return idx < static_cast<int>(pv.size()) && pv[idx].disabled;
        }
        const auto& mv = app.warpmarkers.markers();
        return idx < static_cast<int>(mv.size()) && effective_disabled(mv, idx);
    };

    // Per-run painter: kBackground fill exactly behind the run (AA off for a
    // crisp edge), then the display text — no border, no caret. Glyphs paint
    // kText, or the opaque kTextDisabled when the run's marker is disabled (a
    // color class, never an alpha fade). WIDTH uses the
    // run's glyph count (never txt.size(): a truncated run is 11 bytes / 9
    // glyphs), while cairo receives the whole UTF-8 display string (the toy API
    // draws U+2026 at the uniform mono advance). source_frame centers the run on
    // the marker's painted column (lane_text_left_x_at_frame), column-agnostic so
    // this needs no knowledge of which store the marker came from; a bad advance
    // or clamp yields left<0 and skips. source_frame is a DOUBLE so a mid-drag run
    // centers on the dragged member's free proposed position.
    auto paint_run = [&](double source_frame, const std::string& txt,
                         size_t glyphs, bool disabled) {
        if (glyphs == 0) return;
        const double left = lane_text_left_x_at_frame(
            app, audio, source_frame, glyphs);
        if (left < 0.0) return;
        const double run_w = static_cast<double>(glyphs) * advance;
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kBackground.r, kBackground.g, kBackground.b);
        cairo_rectangle(cr, left, static_cast<double>(lane.y),
                        run_w, static_cast<double>(lane.h));
        cairo_fill(cr);
        cairo_restore(cr);
        text_display::draw_line(cr, left, baseline, txt,
                                disabled ? kTextDisabled : kText,
                                flag_font_size_px());
    };

    // Every ambient run (all-visible: the whole set; fallback: the 0/1 run),
    // resolved by the shared owner current_marker_lane_runs — the ONE arbitration
    // the unified marker hit resolver (marker_hit_at) also reads, so the painted
    // runs and the clickable runs cannot drift. Two overlay suppressions: while
    // the editor is open SKIP its own marker's ambient run (the editor box below
    // replaces it), and while a text-hover expansion is active SKIP the expanded
    // marker's capped run (the full-text run below replaces it). Both suppressed
    // markers' CAPPED runs still participated in the verdict.
    const LaneRunSet set = current_marker_lane_runs(app, audio);
    const int expanded_target = set.has_expanded ? set.expanded.marker_index : -1;
    for (const LaneTextRun& run : set.runs) {
        if (editor_active && run.marker_index == editor_target) continue;
        if (run.marker_index == expanded_target) continue;
        paint_run(run.source_frame, run.text, run.glyphs,
                  run_disabled(run.marker_index));
    }

    // The text-hover EXPANDED run paints LAST among the ambient runs (on top,
    // occluding the neighbors it overlaps — the one text occlusion), before the
    // editor box. Full text, centered on the marker's column exactly like its
    // capped run was. (Hover is cleared while any editor is open, so an expansion
    // and the editor box never coexist.)
    if (set.has_expanded)
        paint_run(set.expanded.source_frame, set.expanded.text,
                  set.expanded.glyphs,
                  run_disabled(set.expanded.marker_index));

    // The FlagPayload editor box LAST, overlaying any ambient run it overlaps.
    if (editor_active) {
        const text_editor::State& ed = app.top_flag_editor;
        // The caret-origin owner supplies the box's left x, centered on the
        // marker and clamped onscreen, so paint and the click->byte caret math
        // share one origin.
        const double left = flag_pending_text_left_x(app, audio, ed.target);
        if (left < 0.0) return;   // invalid editor target
        cairo_save(cr);
        cairo_select_font_face(cr, "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, flag_font_size_px());
        EditorTextBox box;
        box.anchor_x        = left;
        box.baseline_y      = baseline;
        // The MM:SS.mmm| locked prefix is display-only and not shown in the
        // lane — only the editable payload paints, centered on the marker.
        box.prefix          = "";
        box.text            = ed.pending;
        box.hl_pad          = flag_glyph_inset_px();
        // The open editor paints like an ordinary MARKER chip: kMarker fill +
        // kMarkerOutline ring — the one live marker pair, the same
        // rectangle/outline a selected chip paints too (selection only fills
        // a flag's triangle interior, which this box — a plain rectangle —
        // has none of). So the box reads as "this marker, open for editing"
        // instead of an invisible bg-on-bg box. On an invalid commit both
        // flash to kAccent — a color CHANGE on an already-visible box, not a
        // box appearing from nowhere. Text stays kText and readable on
        // kMarker (the same pairing a chip's context uses).
        box.fill            = ed.red ? kAccent        : kMarker;
        box.outline         = ed.red ? kAccentOutline : kMarkerOutline;
        box.text_color      = kText;
        box.has_selection   = text_editor::has_selection(ed);
        box.selection_start = text_editor::selection_start(ed);
        box.selection_end   = text_editor::selection_end(ed);
        box.cursor_visible  = text_editor::cursor_visible_now(ed);
        box.cursor_pos      = ed.cursor_pos;
        render_editor_text_box(cr, box);
        cairo_restore(cr);
    }
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
    // bottom rows are render_canvas's kLine border and no band-filling pass may
    // cover them. (The plate's own inset band leaves those rows transparent
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
// clipped to the CONTENT band so it cannot cover the area's kLine border rows.
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
    cairo_set_source_rgb(cr, kRegionCanvas.r, kRegionCanvas.g,
                         kRegionCanvas.b);
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
// 2026-07-27): the band's 1px opaque kOverlayOutline border and nothing else,
// painted AFTER the plate. It is a BOUNDARY LINE, like the playheads and the
// stems, so an opaque line crossing waveform ink is correct and intended, and
// with no fill inside it the band now READS as the two edges of a span rather
// than as a tinted region. The CONTENT band bounds it, so the top and bottom
// runs sit inside the kLine border
// rather than on them. A vertical side is drawn only where the band's own edge
// is the true edge — both x0 and x1 come back already clipped to the area, so a
// band running past a viewport edge draws its border there too; that is the
// same flush-to-the-edge reading the trim bridge's clipped fill has, and the
// band is an aid rather than a hit target, so no sentinel machinery is needed.
void GuiPaintHandler::paint_phase_reset_overlay_ring(
    cairo_t* cr, const GuiRect& area) {
    const PhaseResetOverlayBand band = phase_reset_overlay_band(area);
    if (!band.valid) return;

    const GuiRect content = waveform_content_rect(area);
    const double w = band.x1 - band.x0;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, kOverlayOutline.r, kOverlayOutline.g,
                         kOverlayOutline.b);
    const double y0 = static_cast<double>(content.y);
    const double h  = static_cast<double>(content.h);
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
// paint_selected_stem and hence before every playhead element), so the playhead
// triangle sits OVER a trim stem crossing the triangle lane while marker flags
// stay above the playheads (the z-order flip untouched). "Markers over trim" is
// now STRUCTURAL pass order — trim < selected stem < playheads < flag blit —
// not an intra-cache paint convention; the two-segment stem join (strip segment
// from render_trim_flags, waveform segment from render_trim_stems) lives in
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

    // Waveform stem segments first (verbatim geometry: hard-aliased 1-px
    // verticals, solid kTrimStem straight over the ink),
    // then the top-strip half (chips + bridge bar + strip stem segments, with
    // the side-aware offscreen sentinels and the effective-width clip inside
    // render_trim_flags). The two halves are geometrically disjoint and meet
    // at the waveform top edge, so their relative order is cosmetic.
    //
    // The chip lane's y-band is THREADED IN as top_upper_row_area(app) rather
    // than re-derived inside the painter: this is the same accessor
    // hit_test_trim_chip's y-gate and route_trim_chip_press' bridge y-gate
    // read, so the painted band and the clickable band have ONE owner and
    // cannot drift if the lanes above the chip row ever change.
    render_trim_stems(cr, wave_rect,
                      basis.vp_start_frame, basis.vp_end_frame, trim);
    render_trim_flags(cr, top_strip, top_upper_row_area(app), wave_rect,
                      basis.vp_start_frame, basis.vp_end_frame, trim);
}

// -- GuiPaintHandler::paint_selected_stem --------------------------------

// Selected-marker stem (architect 2026-07-25): the stem is the SINGLETON selection's focus
// visual — it marks where the playhead sits/would land on the one selected
// marker, and it ALWAYS paints for that marker, with NO exception at all (the
// active-region suppression it carried died 2026-07-30 with the SPAN FORM — the
// region is trim scratch, not a playhead, and outranks nothing).
// The visibility predicate is "exactly ONE marker selected"
// (+ the bounds checks below): the hover,
// lateral-gesture PIN, and tempo-drag arms are GONE as gates (the whole
// conditional-stem apparatus — stem_pin_*, the hover arm, the click-site stem
// damages — was harvested when the stem became unconditional). "Always" replaces
// every prior expiry semantic: the stem is hover-INDEPENDENT (a keyboard-only
// selection shows it too) and playback-INDEPENDENT (it persists through scrubs
// and auditions), because it is the selection's focus cue, not a working
// affordance. A marker GRAB is always a SINGLETON selection now (the arming press
// single-selects, and groups are never moved — architect 2026-07-29, the doctrine
// at the head of position_nudge.h), so a live position drag paints its stem
// with no gesture arm needed, and there is no group-grab case to reject beyond the
// size check below.
// The ONE non-selection
// input is the DragOverlay proposal override below: under a live POSITION drag —
// the only marker pointer gesture left, the W+target tempo drag having been deleted
// with the tempo-image family (marker_drag.h) — the
// stem tracks the flag 1:1 at the mid-gesture proposed frame.
// Painted in kSelectedStem — its OWN palette key (architect 2026-07-27), tuned
// independently of every flag fill and ring: a line run the full height of the
// waveform reads far louder than the same value does as a 1px border around a
// flag, so what is right for the ring is not right here — through
// render_playhead's line-only form (draw_triangle=false): one solid line
// straight over whatever it crosses, the waveform ink included (the former
// ink-notch two-tone is retired). It lives OUT of the stem cache as a per-frame
// one-column overlay
// over the plate; a disabled marker's stem is not recolored here (the flag's
// opaque disabled pair conveys it). The
// displayed paint basis (fp_vp_start + disp_spp + the displayed map) matches
// paint_playheads / the cached flags, so the stem lands on the flag's own column;
// the drag override reads the frozen displayed map its proposal was computed
// against. A focused GROUP (2+ selected) paints no stem — the members' kWaveform
// ink triangles plus the always-visible cursor landed on the focus are the
// group's cue (architect 2026-07-30, with the span form retired). The size check
// below is the whole rule — the stem is a SINGLETON visual, never a group's.
void GuiPaintHandler::paint_selected_stem(cairo_t* cr, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    // A single selected marker, else no stem. The stem is a SINGLETON visual and
    // nothing suppresses it: the region is trim scratch (a ground recolor), not a
    // playhead form, so a resting span leaves the stem exactly where it is.
    if (app.selected_markers.size() != 1) return;
    const int idx = *app.selected_markers.begin();
    if (idx < 0) return;

    // The one non-selection input: a live POSITION drag grabbing the active column
    // overrides the store frame with the mid-gesture proposed position so the stem
    // tracks the flag 1:1 (the only marker pointer gesture there is — see the
    // header).
    const bool drag_arm =
        app.drag.active && app.drag.drag_mode == app.active_markers_view;

    // The marker's effective time: the live store frame, or — under a drag that
    // grabs it — the proposed mid-gesture position (a source-frame double) from
    // the DragOverlay, so the stem tracks the flag 1:1 during the drag.
    double eff_time = 0.0;
    if (app.active_markers_view == 'P') {
        const auto& pv = app.phaseresetmarkers.markers();
        if (idx >= static_cast<int>(pv.size())) return;
        eff_time = static_cast<double>(pv[idx].time_frame);
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (idx >= static_cast<int>(mv.size())) return;
        eff_time = static_cast<double>(mv[idx].time_frame);
    }
    if (drag_arm) {
        DragOverlay ov;
        ov.indices = &app.drag.dragging_markers;
        ov.times   = &app.drag.moveable_times;
        eff_time = ov.effective_time(idx, eff_time);
    }

    const PlateViewportBasis basis = plate_viewport_basis();
    const double disp_spp = basis.spp;
    if (disp_spp <= 0.0) return;
    const double vp_start = basis.vp_start;

    // Forward-map the (possibly fractional) source frame to the displayed axis
    // (identity in source view), the same shape render.cpp's frame_to_paint_sample
    // uses (nearbyint, then the map).
    double ms = std::nearbyint(eff_time);
    if (app.active_audio_view == 'T') {
        const std::vector<WarpFrameMapSegment>& dmap =
            displayed_or_live_target_map(app, audio);
        if (!dmap.empty()) {
            const size_t q = ms < 0.0 ? static_cast<size_t>(0)
                                      : static_cast<size_t>(ms);
            ms = std::nearbyint(map_source_to_target(q, dmap));
        }
    }
    const double px_x = (ms - vp_start) / disp_spp;

    // render_playhead draws only the 1px line here (draw_triangle=false), which
    // is exactly the stem; it column-culls px_x itself, so a stem off the visible
    // strip paints nothing. The triangle lane rect is still handed over — the
    // parameter is unconditional so no call site can drift from the lane owner.
    render_playhead(cr, area, top_triangle_row_area(app), px_x, kSelectedStem,
                    /*draw_triangle=*/false);
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
    // The lane the cursor's triangle is stamped in, from
    // the lane accessor rather than derived from the waveform top edge, so it
    // rides the same band as the flag triangles beside it.
    const GuiRect tri_lane = top_triangle_row_area(app);

    // Playheads paint UNDER the marker flags (the Z-ORDER FLIP, architect
    // 2026-07-23 — see the paint-order block in on_redraw): the cursor line +
    // triangle passes beneath a marker flag sharing their column, so a cursor
    // resting on a marker sits hidden behind that marker's flag. The scanner line
    // stays waveform-only (no flag lane), so its stacking is unaffected; the
    // cursor still draws over the marker STEMS below it in the waveform. The
    // triangle indicator lives in the top strip, so render whenever either the
    // waveform or top strip is exposed; otherwise a flag-strip-only repaint would
    // erase the triangle.
    //
    // Paint order: scanner first (line only, gated on playhead_scanner_active),
    // then cursor (line + triangle). The cursor draws over the scanner on
    // overlap.
    if (app.playhead_scanner_active) {
        const double scan_px = scanner_pixel_x(app, wf_cache.fp_vp_start,
                                               disp_spp);
        render_playhead(cr, area, tri_lane, scan_px, kPlayheadScanner,
                        /*draw_triangle=*/false);
    }

    // THE CURSOR PLAYHEAD ALWAYS PAINTS (architect 2026-07-30): ONE playhead
    // form, drawn at the resting cursor column whatever the selection and
    // whatever the region are doing. The kPlayheadCursor 1px line + tip-down
    // triangle, painted solid straight over the plate ink; ONE color for the
    // line and the triangle alike.
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
    render_playhead(cr, area, tri_lane, px_x, kPlayheadCursor,
                    /*draw_triangle=*/true);
}

// -- GuiPaintHandler::paint_bottom_strip ---------------------------------

void GuiPaintHandler::paint_bottom_strip(cairo_t* cr, int sr) {
    // Bottom strip: TWO text rows. The status line lives on the lower (outer)
    // row and paints UNCONDITIONALLY — it is no longer the trailing else of a
    // chain, so it stays visible while an editor is open on the upper
    // (inner) row, letting the user keep their timestamp / S-T / W-P /
    // A-B bearings while typing. The upper row carries the transient /
    // modal chain in precedence order: prompt > queue > settings editor
    // > BPM editor > pass/ref hover readout. The prompt is a one-key-answer modal
    // and owns the upper row; status stays visible under it (harmless
    // context). (The hover readout is the resolved-tempo string for a pass /
    // label_ref marker; a marker's OWN value shows in the marker-text lane —
    // paint_marker_text_lane.) Each row's baseline is derived from its row rect, not
    // from the window bottom. (The former pan-strip row retired — pan lives on
    // the Alt+drag waveform grab and the strip drag's horizontal axis.)
    const GuiRect lower_row = bottom_lower_row_area(app);
    const GuiRect upper_row = bottom_upper_row_area(app);

    const double lower_baseline =
        lower_row.y + monospace_text_row_baseline_offset();
    const double upper_baseline =
        upper_row.y + monospace_text_row_baseline_offset();

    // --- Lower row: status line (always on). One assembled field
    //     drawn in a single pass; elements are space-separated and
    //     paint uniformly in kText: timestamp, S/T, W/P, A/B, then the
    //     literal "(read-only)" token when the active A/B tab carries the
    //     read-only flag.
    //
    //     The dirty * and transient message appendices follow the
    //     tokens — they are status, not view letters.
    //
    //     sr is the loaded file's sample rate and the playhead samples are
    //     source-frames. Split-playhead: track the scanner during playback
    //     (what the user hears), the cursor otherwise (the scanner is
    //     meaningful only while active, so the ternary takes the cursor at rest).
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
        if (seconds > 5999.999) seconds = 5999.999;

        std::string assembled = format_timestamp(seconds);
        assembled += ' ';
        assembled += (app.active_audio_view == 'T'
                        ? 'T' : 'S');
        assembled += ' ';
        assembled += app.active_markers_view;
        assembled += ' ';
        assembled += app.active_tab_view;
        if (active_view_state(app).read_only) {
            assembled += ' ';
            assembled += "(read-only)";
        }
        if (app.dirty) {
            assembled += ' ';
            assembled += '*';
        }
        if (!app.transient_status_message.empty()) {
            assembled += ' ';
            assembled += app.transient_status_message;
        }

        text_display::draw_line(
            cr, static_cast<double>(timestamp_pad_x()), lower_baseline,
            assembled, kText, flag_font_size_px());
    }

    // --- Upper row: transient / modal chain. ---
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
        text_display::draw_line(
            cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
            assembled, kText, flag_font_size_px());
    } else if (!app.queue_progress_text.empty()) {
        text_display::draw_line(
            cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
            app.queue_progress_text, kText, flag_font_size_px());
    } else if (text_editor::is_active(app.settings_editor)) {
        // Settings prompt overlay: "setting: <pending>"
        // through the shared bottom-strip editor helper. Fill is
        // kBackground normally, kAccent on parse failure (handled
        // inside the helper).
        render_bottom_strip_editor(cr, app.settings_editor,
                                   kSettingsEditorPrefix,
                                   static_cast<double>(timestamp_pad_x()),
                                   upper_baseline);
    } else if (text_editor::is_active(app.commit_editor)) {
        // Render-commit prompt overlay: "commit: ./renders/<pending>"
        // through the same shared bottom-strip editor helper as the settings
        // branch above. Fill is kBackground normally, kAccent on an
        // unresolved / bad commit (handled inside the helper).
        render_bottom_strip_editor(cr, app.commit_editor,
                                   kCommitEditorPrefix,
                                   static_cast<double>(timestamp_pad_x()),
                                   upper_baseline);
    } else if (text_editor::is_active(app.top_flag_editor) &&
               app.top_flag_editor.kind ==
                   text_editor::Kind::BpmBracket) {
        // BPM editor overlay, through the same bottom-strip
        // editor helper as the settings branch above. top_flag_editor
        // with kind==BpmBracket only ever paints here, never over the
        // flag in the top strip.
        render_bottom_strip_editor(cr, app.top_flag_editor,
                                   kBpmEditorPrefix,
                                   static_cast<double>(timestamp_pad_x()),
                                   upper_baseline);
    } else {
        // The pass/ref resolved readout renders below every modal/progress tier,
        // driven by BOTH hover and selection. Simple rule: the HOVERED marker's
        // readout wins when present (compute_hover_popup_text, cached in
        // hover_popup at recompute); else the LAST-SELECTED marker's, computed
        // live here when it is an eligible pass/label_ref (popup_eligible_marker,
        // itself 'W'-view + non-iteration only). One readout, hover wins. Owners
        // and phase resets have nothing to resolve, so their strip stays clean
        // while their own value shows in the marker-text lane. The live
        // computation is the notice-free string (copy_payload is a hover-only
        // concern, so no out-param here).
        std::string readout = app.hover_popup.readout_text;
        if (readout.empty() &&
            popup_eligible_marker(app, app.last_selected_marker)) {
            readout = compute_hover_popup_text(
                slice_to_warp_markers(app.warpmarkers.markers()),
                app.last_selected_marker, sr, audio.total_frames());
        }
        if (!readout.empty()) {
            text_display::draw_line(
                cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
                readout, kText, flag_font_size_px());
        }
    }
}

// -- GuiPaintHandler::on_redraw ------------------------------------------

void GuiPaintHandler::on_redraw(cairo_t* cr, int x, int y, int w, int h) {
    init_monospace_grid_metrics(cr);

    // Event-synchronized hit geometry, PROMOTE phase (ruling at the selector):
    // done at the TOP of the frame, BEFORE any painting, so the flag cache this
    // frame blits (blit-only below) AND the overlays this
    // frame paints around it (the live trim pass, the marker-text lane, hover,
    // selection, the
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
    // displayed_map_gen so a silent geometry change is visible to hover identity.
    //
    // Refresh the hover identity against the JUST-promoted map, still before any
    // painting: the hook (recompute_hover_at_cursor, wired in main.cpp) hit-tests
    // the new flag positions and re-stamps lane_text/readout_text/copy_payload,
    // so the run/readout this frame paints — and any Ctrl+C landing after this
    // frame but before the next tick — reads the new identity, not the old map's
    // (the run could otherwise follow a marker to its new column though it is no
    // longer under the cursor). No input dispatches mid-paint (single-threaded),
    // so the recompute is safe here. The on_tick displayed_gen check remains the
    // BACKSTOP for a promotion-free store mutation; this hook owns the
    // promoting-frame case that the tick (running before the paint) cannot.
    if (app.staged_displayed_valid) {
        app.displayed_target_warp_frame_map =
            std::move(app.staged_displayed_target_warp_frame_map);
        app.staged_displayed_target_warp_frame_map.clear();
        // Promote the displayed VIEWPORT mirror in the SAME block (one promote,
        // one gen bump): the marker-text lane geometry advances to the fp_*
        // viewport the just-blitted flag cache was built against, in
        // lockstep with the map above.
        app.displayed_vp_start = app.staged_displayed_vp_start;
        app.displayed_vp_end   = app.staged_displayed_vp_end;
        app.displayed_area_w   = app.staged_displayed_area_w;
        app.staged_displayed_valid = false;
        ++app.displayed_map_gen;
        if (on_displayed_map_promoted) on_displayed_map_promoted();
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

    // THE FOUR REDESIGNED ROWS PAINT ON EVERY FRAME CLASS, deliberately OUTSIDE
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
    // lanes (the flag cache is transparent over them, every other pass owns a
    // lane below them), so painting them first overdraws nothing.
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
    }

    if (app.loading) {
        // Blank plate during load; the only feedback is the bottom-strip
        // upper-row status ("loading..."), the same slot renders use. Painted
        // here because the total>0 bottom-strip block below does not run while
        // loading (and total_frames is 0 on a cold launch).
        const GuiRect upper_row = bottom_upper_row_area(app);
        const double  upper_baseline =
            upper_row.y + monospace_text_row_baseline_offset();
        text_display::draw_line(
            cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
            app.queue_progress_text, kText, flag_font_size_px());
    } else if (audio.total_frames() > 0) {
        const GuiRect area       = waveform_area(app);
        const GuiRect top_strip  = top_strip_area(app);
        const GuiRect exposed{x, y, w, h};
        const int     sr         = audio.sample_rate();

        // The live viewport / target-warp_frame_map computations live in the
        // cache rebuild paths (waveform via the worker, flags via
        // maybe_rebuild_flag_cache), not in on_redraw, which reads
        // wf_cache.fp_* for displayed-viewport inputs and treats the plate and
        // flag strips as blit-then-overlay paths. Trim is a live pass
        // (paint_trim) on the free item-basis owners.
        //
        // Final paint order (bottom to top of the stack): canvas ground + its
        // kLine border (painted above, unconditionally) -> region ground ->
        // waveform plate -> overlay ring -> LIVE
        // TRIM (chips + bridge bar + strip and waveform stem segments, one pass)
        // -> selected stem -> playheads (scanner line + cursor) -> flag blit ->
        // marker-text lane / zoom ring -> strip-drag anchor -> bottom strip.
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
        //     paints before every playhead element, so the playhead triangle
        //     sits over a trim stem crossing the triangle lane:
        //     trim < selected stem < playheads < marker flags.

        if (rects_intersect(exposed, area)) {
            // THE GROUND RECOLOR, under the plate. render_canvas already laid
            // the kCanvas ground for the whole area above; this repaints the
            // region's span of it opaquely, so the plate's ink and its
            // antialiased fringes composite against the recolored ground.
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

        if (rects_intersect(exposed, area)) {
            // Selected-marker stem over the plate + trim, under the playhead and
            // the flags: the single selected marker's focus column, live per-frame
            // (not cached), ALWAYS painted for a singleton selection (no
            // hover/pin/gesture condition).
            paint_selected_stem(cr, area);
        }

        // Playheads BEFORE the flag blit (Z-ORDER FLIP, architect 2026-07-23):
        // the scanner line stays waveform-only (triangle-free, no lane conflict —
        // its stacking vs the lanes is unaffected by this move), while the cursor
        // line+triangle now paints UNDER the
        // marker flags that follow. flag_cache.surface is ARGB32, CLEAR-cleared
        // each rebuild and transparent outside the painted shapes, so the flag
        // blit composites source-over and never erases the playheads it does not
        // cover. Gated on area OR top_strip: the cursor line lives in the waveform
        // area, its triangle in the triangle lane (top strip).
        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            paint_playheads(cr, area);
        }

        if (rects_intersect(exposed, top_strip)) {
            paint_flag_annotations(cr, top_strip);
            // Marker-text lane (top row 3): the hover popup and the flag
            // editor's live text, painted over the just-blitted flag cache —
            // the same layering role the bottom strip's hover/editor paints had.
            paint_marker_text_lane(cr);
        }

        // Strip-drag anchor stem: over the plate/stems in the waveform area
        // only. It now paints AFTER the playheads (the flip moved them up), so
        // where the pivot column coincides with the cursor/scanner column during
        // a strip drag the anchor stem sits OVER the playhead LINE (both are
        // waveform verticals; the playhead's triangle lane is untouched, the
        // anchor carries no triangle). The anchor shows only mid-strip-drag, so
        // this overlap is transient and the pivot affordance reading on top is
        // acceptable. paint_marker_text_lane likewise ends up after the playheads
        // but on the non-overlapping text lane.
        if (rects_intersect(exposed, area)) {
            paint_strip_drag_anchor(cr, area);
        }

        const GuiRect bottom_strip = timestamp_invalidate_rect(app);
        if (rects_intersect(exposed, bottom_strip)) {
            paint_bottom_strip(cr, sr);
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
