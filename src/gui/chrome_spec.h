// THE ONE INCLUDE OUTSIDE THE GUARD: a ChromeSpec names a face set, so the
// sets (gui_font.h) come first; gui_font.h in turn includes this header after
// its sets and before its live block, which reads live_chrome_spec(). With
// the guard below the include, either header may be included first.
#include "gui_font.h"

#ifndef WARPTEMPO_GUI_CHROME_SPEC_H
#define WARPTEMPO_GUI_CHROME_SPEC_H

#include <cassert>
#include <string_view>

// THE CHROME SPEC (architect 2026-10-06): the choices of the period desktop
// the chrome follows, for the elements a later vocabulary may draw
// differently — read by the chokepoint that paints each element, never a
// literal at the painter. The colors stay the chrome's compiled theme's
// (theme_file.h's kGuiChromeThemes, by this spec's key) and the palette's
// (palette_file.h); the spec chooses shapes, seats and faces, and names the
// palette a config with no `palette` line takes.
//
// THE CHROME ENDS AT THE MENU ROW (architect 2026-10-09 ~06:55, "chrome
// means chrome, the program is Cool Edit"): the chrome is the caption, the
// menu row, the window frame, the pull-downs, the cards, the prompts and the
// dialogs with their fields and buttons, the trim lane, the scrub, the
// ruler's ticks and the well's frame (the canvas column's lanes until the
// program's later parts take them); THE TOOLBAR BAND AND ROW 8 ARE THE
// PROGRAM'S, Cool Edit's under every chrome (program_spec.h) — so no field
// here shapes a toolbar case, a toolbar band, row 8's air or a dead button's
// glyph.
//
// THE NAME IS `windows-2000` (architect 2026-10-07): the `chrome` key's
// value (its compiled theme is the catalog entry `windows-2000-standard`,
// the scheme's word beside it, which also names its default palette since
// 2026-10-08, the built-in schemes' keys being the catalog's). C++
// IDENTIFIERS AND FILE NAMES KEEP `Win2000` / `win2000` AS THE ABBREVIATION
// (kChromeSpecWin2000, GuiChromeVocabulary::Win2000,
// docs/engineering/win2000_deviations.md), and prose may say win2000 for the
// vocabulary; only a KEY is spelled whole.
//
// THREE VOCABULARIES, ONE LIVE, CHOSEN AT LAUNCH (the `chrome` device key,
// architect 2026-10-07, the planner's design under his free rein:
// "authenticity wherever possible; the most Clearlooks assets; Windows
// 2000's metrics the tie-break"):
//   WIN2000 (the key `windows-2000`) — Windows 2000's "Windows Standard"
//     chrome as ReactOS 0.4.16 draws it (architect 2026-10-06: "pivot to Win2K as the official
//     supported version and focus on that"; its classic chrome is that
//     scheme in every colour and metric; Windows XP's Classic and Server
//     2003's are the same), read off the architect's ReactOS captures
//     (tmp/reactos*.png, "get it as accurate to ReactOS as we can"). WHERE
//     REACTOS DEPARTS FROM WINDOWS 2000 THE PRODUCT FOLLOWS REACTOS, on
//     purpose and under the Windows 2000 name
//     (docs/engineering/win2000_deviations.md lists the departures).
//   CLEARLOOKS — GNOME 2.30's Clearlooks as Debian 6 squeeze drew it
//     (gtk-engines 2.20.2's GUMMY style under gnome-themes 2.30.2's gtkrc
//     and metacity theme), his squeeze captures the law (tmp/squeeze/,
//     INDEX.md), WORN AT WINDOWS' PROPORTIONS (architect 2026-10-07 ~02:20:
//     "Windows is the base: a compact, nicely proportioned layout"; the
//     instance's head). THE TOP STRIP IS CLEARLOOKS' OWN DRAWING since the
//     painters round's first part (2026-10-07: the metacity caption, the
//     menu bar — clearlooks_paint.h), AND THE DIALOGS, THE
//     DROPDOWN AND THE LISTS since its second part (the push button, the
//     entry, GtkMenu, the compact list; the tooltip and the cards were
//     Clearlooks' already: the same one-line card face), AND THE REST OF THE
//     CHROME since its last part (the trim lane as GTK's scroll bar, the
//     scrub as GtkScale, the well's scrolled-window line, the ruler's
//     ticks, the restored laptop's metacity frame). The waveform and the
//     flags stay the program's own, the flag editor with them — the flag
//     in its selected face under every chrome (architect 2026-10-07
//     ~09:45). The trim lane's bar is the product's own: the light slider
//     for the body and the caps in step with it (paint_cl_slider,
//     paint_cl_stepper).
//   CDE (the key `cde`; architect 2026-10-08 ~12:50, "let's dive right into
//     the CDE build-out", his rulings of ~14:10 on the three bevel sheets
//     and of ~15:00) — SOLARIS 9's CDE 1.5, dtwm and Motif as the
//     guidebookgallery captures show them (tmp/research/cde_solaris/
//     report.md, its dl/shots/ the law: one capture px = one Solaris px),
//     under THE SETTLED RULE FOR EVERY LATER VOCABULARY: period authentic
//     except for the proportional fit to Windows' layout — the base's cell
//     and seat, every Motif height re-derived at the 13-row cell by Motif's
//     own arithmetic, the differences absorbed by the ruler lane, line
//     weights as the sources give them, what cannot be made to fit
//     scratched. BEVELS ONE W (ruling 1: CDE's authentic weight — only
//     dtcalc used Motif's canonical 2), corners mitred like every two-tone
//     ring; THE MENU BAR 27 W (ruling 2); NO CLOSE BUTTON, close in the
//     window menu (ruling 3, dtwm's three buttons); GO SANS for Lucida
//     (ruling 4, kGuiFaceSetCde). THE FULL DTWM FRAME ON EVERY WINDOW, the
//     tablet's and the maximized laptop's too (architect 2026-10-08 ~17:45,
//     reversing the ~15:00 "hidden" on his glass pass of phase 1:
//     window_frame_maximized below), THE RULER BEHIND THE FLAGS
//     (ruler_behind_flags), CDE'S OWN 13-W SCROLL BAR (scroll_bar_px) and
//     MOTIF'S INSENSITIVE STIPPLE for every disabled word (render.h's
//     stipple pair; his rulings of ~17:30–18:00); the
//     focus ring NOT DRAWN (~15:00: "anachronism ok to fit the screen and
//     the Win95 metrics"). The painters are cde_paint.h's (the Motif bevels
//     on Windows' one-line families, the caption's four boxes, the scroll
//     bar, the scale, the frame, the stipple), the colors the Solaris
//     scheme (theme_file.h's kGuiThemeCde, derived from Solyaris.dp's four
//     sets by Motif's own rule, cde_derive.h); the instance's head below
//     records every length against its capture.
// A LATER VOCABULARY'S ELEMENT MAY BE SMALLER THAN THE BASE'S, NEVER LARGER
// WITHOUT HIS RULING (architect 2026-10-08 ~17:45, beside the proportional
// fit: "ok to make larger elements smaller, as the Clearlooks icon casings
// were; making them larger than Windows' metric when the historical frame
// wants the space is done only after consideration"): where a source draws
// an element smaller than Windows' metric the vocabulary takes the source's
// own (cde's 13-W scroll bar for the base's 16, scroll_bar_px), and where a
// source draws it larger the base's metric stands until he rules otherwise.
// WINDOWS-2000 IS THE DEFAULT AND THE METRIC BASE; CLEARLOOKS IS THE SECOND
// VOCABULARY AND CDE THE THIRD (architect 2026-10-07 ~22:45, reversing the ~16:00 ruling that
// had made clearlooks the default: "we could revert the default theme to
// Windows 2000; I just need to work out a good color palette", the color
// picker's purpose). The table (kGuiChromeSpecs) keeps windows-2000 FIRST:
// it is the metric base every later vocabulary is fitted to, and the
// table's order is the defaults' order (palette_file.cpp's assert).
// Windows 95's chrome, the second vocabulary of 2026-10-06's morning, is
// dropped (it stands in git history); its colors remain in the theme
// catalog's record (`windows-95-standard`, tools/theme_catalog). The table is
// kGuiChromeSpecs below, the live instance
// live_chrome_spec(), set once by set_live_chrome_spec before the first
// paint. WHERE THE VOCABULARIES DRAW DIFFERENTLY THE PAINTER FORKS ON THE
// SPEC'S `vocabulary`, the win2000 arm the code that stood before the fork. What no vocabulary is expected to vary is a plain constant at its
// owner, not a field here: the caption icon's 16-px size (kCaptionIconPx),
// the card frame's four sides (paint_popup_chrome), the keyboard's
// Backspace word (onscreen_keyboard.h's cap_word) — each until a painter
// branch of a later round says otherwise.

// WHICH PERIOD DESKTOP DRAWS THE CHROME — the painters' one switch where
// the vocabularies draw an element differently (the head): the caption's band,
// title and buttons, the menu bar and its open title, the disabled words'
// emboss, the dialogs' push buttons and fields, the dropdown, the lists, the
// trim lane, the scrub, the well's frame, the ruler's ticks, the restored
// laptop's frame and the flag editor. A PAINTER THAT FORKS ON `== Clearlooks` ALONE draws
// its Windows arm under cde too, deliberately, where Windows' drawing in the
// Solaris scheme's roles IS Motif's (the etched lines, the emboss, the
// inverted list selection): the cde arm is
// added only where Motif draws otherwise (2026-10-08).
enum class GuiChromeVocabulary {
    Win2000,
    Clearlooks,
    Cde,
};

struct ChromeSpec {
    // THE VOCABULARY'S NAME, the `chrome` device key's value that chooses it
    // (is_chrome_key below).
    const char*        key;
    // THE VOCABULARY'S SHOWN NAME, Title Case (TEXT: names Title Case): the
    // one source of every place the user reads a vocabulary's name — the
    // Settings editor's Chrome combo and its list (kChromeChoiceSource); the
    // key stays the file grammar's. (The color picker names a built-in
    // scheme, and a default palette, by the built-in scheme's own display
    // name, palette_file.h's kGuiChromeSchemes.)
    const char*        display_name;
    // THE PAINTERS' SWITCH (GuiChromeVocabulary above).
    GuiChromeVocabulary vocabulary;
    // THE PALETTE A CONFIG WITH NO `palette` LINE TAKES under this chrome
    // (effective_palette_name, palette_file.h; architect 2026-10-07): the
    // vocabulary's own program colors, a compiled default palette, named by
    // THE CHROME'S OWN BUILT-IN SCHEME'S KEY (2026-10-08 ~11:10: the catalog
    // key verbatim, `windows-2000-standard`; palette_file.h's head) — and so
    // ALSO THE SCHEME A CONFIG WITH NO `scheme` LINE TAKES
    // (effective_scheme_name, 2026-10-08 ~18:15), which carries no keys
    // under this chrome: the compiled theme exactly.
    const char*        default_palette;
    // The vocabulary's text: the faces, their metrics and tracking (gui_font.h)
    // — its OWN set; under windows-2000 the live scheme's face tag may choose
    // the MS Sans Serif set in its place (gui_live_face_set, 2026-10-09).
    const GuiFaceSet*  face_set;
    // THE CAPTION, the top strip's lane 0, in Windows px (render.h's caption
    // block, where every element's rule stands): the lane's height; the
    // icon's seat (its size is the small icon's, kCaptionIconPx); the
    // title's pen x, the left edge of its room; the three buttons' box, its
    // top inside the lane, the inset of Close's right edge from the lane's,
    // the gap between Minimise and Maximise, and the gap before Close.
    int                caption_height_px;
    int                caption_icon_x_px;
    int                caption_icon_y_px;
    int                caption_title_x_px;
    int                caption_button_w_px;
    int                caption_button_h_px;
    int                caption_button_y_px;
    int                caption_button_inset_px;
    int                caption_button_gap_px;
    int                caption_close_gap_px;
    // THE TITLE'S ROOM ENDS this far short of Minimise's box (paint_caption_
    // row's cut and, under clearlooks, its centring).
    int                caption_title_trail_px;
    // THE MENU ROW, lane 1, in Windows px (render.h's menu-row block and
    // paint_menu_row): the face row above the content, the content (the
    // anchors' button height, the label's cell centred in it), the row
    // below it; each anchor's pads left and right of its label and the
    // band's lead before the first anchor.
    int                menu_row_head_px;
    int                menu_row_content_px;
    int                menu_row_foot_px;
    double             menu_label_pad_left_px;
    double             menu_label_pad_right_px;
    double             menu_band_lead_px;
    // THE RULER LANE'S TWO AUTHORED TERMS (render.h's ruler block; the
    // seat's rule at paint_handler.cpp's ruler_label_baseline_px): how far
    // under the lane's top the labels' CAP TOP lands, and the rows from
    // their baseline to the marker lane. The base's 4 and 7 (the lane 4 + 6
    // + 7 = 17), clearlooks' the same; UNREAD under cde, whose ruler stands
    // behind the flags (ruler_behind_flags below; 0 and 0 there).
    int                ruler_label_cap_top_px;
    int                ruler_baseline_to_marker_px;
    // THE RULER BEHIND THE FLAGS (architect 2026-10-08 ~17:30, his glass
    // pass of cde's phase 1: "the ticks reduced and the timestamp pushed
    // down; the flags remain as they are, but the ticks and the timestamp
    // hide behind the flags (they are not really that useful)"): when true
    // the ruler lane is `ruler_lane_px` W — THE LEFTOVER OF THE LANE TABLE,
    // what the vocabulary's other lanes and its frame leave of the base's
    // stack so the well keeps its rows (render.h's chrome_stack_authored_h
    // proves the sum) — its ticks short, hanging from its top, its timestamps drawn at
    // the marker lane's rows behind the flags and the playhead's head at its
    // top (paint_ruler_row's arm); the two terms above unread. False (the
    // base's ruler): the lane derives from the label face, ruler_lane_px
    // unread (0).
    bool               ruler_behind_flags;
    int                ruler_lane_px;
    // THE SCROLL BAR'S THICKNESS in Windows px — the trim lane's height, its
    // two arrow caps' square and the popup lists' vertical bar's width
    // (render.h's trim and popup scroll blocks), one source (2026-10-08):
    // Windows' SM_CXVSCROLL 16 under win2000, the base's 16 worn for GTK's
    // 15 under clearlooks, CDE's own 13 (the smaller-element rule at the
    // head).
    int                scroll_bar_px;
    // THE PUSH BUTTON — the dialogs' word buttons (paint_handler.cpp's
    // kModal* block): the box's height and the label's pads left and right.
    double             push_button_box_px;
    double             push_button_pad_left_px;
    double             push_button_pad_right_px;
    // THE TOOLTIP'S PAD inside its frame, each way (paint_popup_chrome's
    // tooltip arm, kTooltipLineGapPx beside it).
    int                tooltip_pad_px;
    // (THE TIME FIELD'S HEIGHT AND PAD left the spec 2026-10-08: every time
    // field is the dialog field, one shape under every chrome —
    // kModalFieldHeightPx and kModalFieldPadXPx, render.h.)
    // THE SCRUB THUMB'S GRAB BOX — the box the press router takes as the
    // thumb's grab band and the mapping insets the track by half of at each
    // end (render.h's scrub block, scrub_handle_box_px). WIN2000 14 (the
    // laptop pixel's 20 re-authored, architect 2026-10-02), the 11-px
    // trackbar thumb widened to it; CLEARLOOKS GTK's grab is the slider
    // itself, GtkScale::slider-length 23 (clearlooks_paint.h's
    // kClScaleSliderLengthPx).
    double             scrub_handle_box_px;
    // THE DROPDOWN, in Windows px (render.h's dropdown block, where the
    // frame and the separator's rule stand; dropdown_h_px sums them): an
    // item's height, and the menu's own margin between its frame and the
    // items, each way (the item block's top and bottom, the item's sides).
    int                popup_item_height_px;
    int                popup_margin_px;
    // (THE NOTIFICATION CARDS' SEAT IS NO FIELD: ONE RULE FOR EVERY
    // VOCABULARY, read off the live lane table at the trim lane's top —
    // notification_stack_bound, notifications.cpp; architect 2026-10-08.)
    // THE CORNERS' RADIUS in Windows px, scaled like any length and drawn
    // antialiased (clearlooks_paint.cpp: the gummy button, the open menu
    // title): 0 is a square corner, every box of win2000's.
    int                corner_radius_px;
    // THE WINDOW'S FRAME (render.h's sizing-frame block, window_frame_px):
    // the relief lines of its composite — Windows' two-line raised edge
    // under win2000 and clearlooks, dtwm's two-line outer shadow and
    // one-line inner one under cde — round the 2-W face; and whether it
    // stands while the window is maximized too: Windows hides its sizing
    // frame then, dtwm keeps its frame on every window (architect 2026-10-08
    // ~17:45: "the dtwm frame as in the original, on the tablet too"), so
    // under cde both platforms keep the band outside the app's geometry on
    // the maximized window and the tablet's (platform_wayland.h's frame_px_,
    // platform_android.h's).
    int                window_frame_lines;
    bool               window_frame_maximized;
    // THE ICON SET the vocabulary wears BY DEFAULT: a bundled folder under
    // assets/icons/ (icons.h's kIconSetKeys, static_asserted there), parsed
    // at launch through resvg (icons.h's head) — the set an absent `icons`
    // device key means; the key names another for every chrome (architect
    // 2026-10-10, gui_live_icon_set).
    const char*        icon_set;
};

// WIN2000 (architect 2026-10-06), off the ReactOS captures: THE CAPTION
// Windows' SM_CYCAPTION 18 with its DFC_CAPTION buttons 16 x 14 two px down
// and in, Minimise and Maximise touching, two px before Close, the small
// icon at (2, 1), the title's pen two px past it (render.h's caption
// block); THE MENU ROW Explorer's menu band, the 19-row button under one
// face row, the label 9 px in and 7 after, the rebar's 2-px lead
// (paint_handler.cpp's kMenuLabel* record); THE PUSH
// BUTTON Windows' 75 x 23 with 7-px pads; THE TOOLTIP comctl32's 2-px pad
// (tmp/reactos-tooltips.png); THE DROPDOWN Windows' popup menu, 17-px items
// one px inside the frame; THE ICONS Tango 0.8.90's scalable drawings,
// ReactOS's own model of a Windows 2000 desktop dressed in Tango (architect
// 2026-10-06: assets/icons/tango/, its README the mapping). (Explorer's
// flat toolbar — the 31 x 30 case under one etched pair — stood below the
// menu row until the program's band took the lane, 2026-10-09; git history.)
inline constexpr ChromeSpec kChromeSpecWin2000 = {
    .key                          = "windows-2000",
    .display_name                 = "Windows 2000",
    .vocabulary                   = GuiChromeVocabulary::Win2000,
    .default_palette              = "windows-2000-standard",
    .face_set                     = &kGuiFaceSetWin2000,
    .caption_height_px            = 18,
    .caption_icon_x_px            = 2,
    .caption_icon_y_px            = 1,
    .caption_title_x_px           = 20,
    .caption_button_w_px          = 16,
    .caption_button_h_px          = 14,
    .caption_button_y_px          = 2,
    .caption_button_inset_px      = 2,
    .caption_button_gap_px        = 0,
    .caption_close_gap_px         = 2,
    .caption_title_trail_px       = 2,
    .menu_row_head_px             = 1,
    .menu_row_content_px          = 19,
    .menu_row_foot_px             = 0,
    .menu_label_pad_left_px       = 9.0,
    .menu_label_pad_right_px      = 7.0,
    .menu_band_lead_px            = 2.0,
    .ruler_label_cap_top_px       = 4,
    .ruler_baseline_to_marker_px  = 7,
    .ruler_behind_flags           = false,
    .ruler_lane_px                = 0,
    .scroll_bar_px                = 16,
    .push_button_box_px           = 23.0,
    .push_button_pad_left_px      = 7.0,
    .push_button_pad_right_px     = 7.0,
    .tooltip_pad_px               = 2,
    .scrub_handle_box_px          = 14.0,
    .popup_item_height_px         = 17,
    .popup_margin_px              = 1,
    .corner_radius_px             = 0,
    .window_frame_lines           = 2,
    .window_frame_maximized       = false,
    .icon_set                     = "tango",
};

// CLEARLOOKS (architect 2026-10-07; report CL1 §3, his squeeze captures the
// law for every drawing, tmp/squeeze/ — GTK px are Windows px, the 96-dpi
// pixel). THE VOCABULARY WEARS WINDOWS' PROPORTIONS UNDER CLEARLOOKS' OWN
// DRAWING (architect 2026-10-07 ~02:20: "Windows is the base: a compact,
// nicely proportioned layout; the fonts and the icons become
// disproportionate in Clearlooks"): THE BASE'S CELL — the 13-row cell, cap 8
// (kGuiFaceSetGnome2, DejaVu Sans at GNOME's "Sans 8") — and THE BASE'S
// SEAT — the 24-W large toolbar glyph — and every height GTK derives from
// the cell RE-DERIVED AT 13 by GTK's and metacity's own arithmetic (his
// squeeze captures, at Sans 10's 17-row cell, check that arithmetic: 24 /
// 25 / 40 / 29 there, 20 / 21 / 36 / 25 here):
//   THE CAPTION metacity's `normal_maximized` frame (metacity-theme-1.xml
//   lines 13-31; theme.c meta_frame_layout_get_borders): the title cell 13
//   + title_border.top 4 + .bottom 3 = 20; the buttons the lane less
//   button_border.top 2 and .bottom 2 = 16 tall, aspect_ratio 1.0 = 16
//   wide, at y 2, each with button_border.left / .right 1 (2 px between
//   neighbours, an 18-px pitch) and Close's right edge 2 in
//   (right_titlebar_edge 1 + its border 1); Close's and Minimise's glyphs
//   at the capture's proportion of the box, 16 x 8 / 20 held even = 6
//   (hpadding 5), not Bmin 7 `max` (16 − Bpad 6 x 2) = 7 — Maximise and
//   Restore keep the 7 (hpadding 4), whose ops lose their window below it
//   (architect 2026-10-07, Minimise at 6 ~05:30; caption_icon_size,
//   clearlooks_paint.cpp); the menu
//   button at left_titlebar_edge 1 + border 1 = (2, 2), its 16-px mini
//   icon centred in its 16 x 16 — FILLING IT, NO MARGIN (the box the icon's
//   own size); the title's room from 21 (the menu button's 2 + 16 + its
//   border 1 + title_border.left 2) to 3 short of Minimise (its border 1 +
//   title_border.right 2), the title centred in it (paint_caption_row).
//   THE MENU ROW GtkMenuBar (gtkmenubar.c size_request): ythickness 1 + the
//   item (gtkmenuitem.c: the cell 13 + 2 x the menu_item style's ythickness
//   3 = 19) + ythickness 1 = 21, the label 5 px in and 5 after (xthickness 2
//   + GtkMenuItem::horizontal-padding 3), the first item at the bar's
//   xthickness 1.
//   (GtkToolbar's band — the 32-W tool button at the cell, 36 with its
//   air — no longer stands: the program's band took the lane, 2026-10-09;
//   its two lengths stay recorded where the generated toolbar tones are
//   computed, clearlooks_derive.h's kClToolCasePx and kClToolBandPx.)
//   THE PUSH BUTTON the cell 13 + 2 x (xthickness 3 + focus-line-width 1 +
//   focus-padding 1 + inner-border 1) = 25 (his file chooser's Open, 29 at
//   the 17 cell), the label 6 px in each way; THE TOOLTIP the tooltips
//   style's 4-px pad; THE DROPDOWN GtkMenu under the "menu" style
//   (x/ythickness 0: no margin, the items against the frame) with the
//   menu_item style's items, the cell 13 + 2 x ythickness 3 = 19 (23 at
//   the 17 cell on his capture 23-22-06).
//   THE ENTRY (every dialog field — render.h's
//   kModalFieldHeightPx, not a spec field, the three chromes agreeing) GTK's at
//   the cell, 13 + 2 x (3 + 2) = 23 (his 27 at the 17 cell, the Customize
//   dialog's entry), its text xthickness 3 + inner-border 2 = 5 in; THE
//   SCRUB'S GRAB GtkScale's slider, slider-length 23 (the default style's).
//   THE DRAWING: radius 3 on the gummy boxes (the gtkrc's `radius = 3.0`),
//   the icons "mist" (gnome-icon-theme 2.30's drawings under Mist's own
//   folders, assets/icons/mist/).
inline constexpr ChromeSpec kChromeSpecClearlooks = {
    .key                          = "clearlooks",
    .display_name                 = "Clearlooks",
    .vocabulary                   = GuiChromeVocabulary::Clearlooks,
    .default_palette              = "clearlooks",
    .face_set                     = &kGuiFaceSetGnome2,
    .caption_height_px            = 20,
    .caption_icon_x_px            = 2,
    .caption_icon_y_px            = 2,
    .caption_title_x_px           = 21,
    .caption_button_w_px          = 16,
    .caption_button_h_px          = 16,
    .caption_button_y_px          = 2,
    .caption_button_inset_px      = 2,
    .caption_button_gap_px        = 2,
    .caption_close_gap_px         = 2,
    .caption_title_trail_px       = 3,
    .menu_row_head_px             = 1,
    .menu_row_content_px          = 19,
    .menu_row_foot_px             = 1,
    .menu_label_pad_left_px       = 5.0,
    .menu_label_pad_right_px      = 5.0,
    .menu_band_lead_px            = 1.0,
    .ruler_label_cap_top_px       = 4,
    .ruler_baseline_to_marker_px  = 7,
    .ruler_behind_flags           = false,
    .ruler_lane_px                = 0,
    .scroll_bar_px                = 16,
    .push_button_box_px           = 25.0,
    .push_button_pad_left_px      = 6.0,
    .push_button_pad_right_px     = 6.0,
    .tooltip_pad_px               = 4,
    .scrub_handle_box_px          = 23.0,
    .popup_item_height_px         = 19,
    .popup_margin_px              = 0,
    .corner_radius_px             = 3,
    .window_frame_lines           = 2,
    .window_frame_maximized       = false,
    .icon_set                     = "mist",
};

// CDE (architect 2026-10-08; tmp/research/cde_solaris/report.md §3 and the
// coder's own measurements on its dl/shots/, named per line — "N" the
// notepad capture applications_office_notepad_cde15solaris9.png, "O" the
// Open dialog interface_dialogs_openfile_cde15solaris9.png, "F" the file
// manager system_managers_filemanager_cde15solaris9.png, "C" the Calendar
// capture tmp/cde15solaris9-1-1.png (2026-10-09, 1:1); capture px are
// Solaris px at 75 dpi, so every HEIGHT is re-derived at the base's 13-row
// cell by Motif's arithmetic and every MARGIN is kept as the px constant
// Motif's resources gave it — the settled rule).
//   THE CAPTION dtwm's title bar, 19 px on N (rows 5-23: 1 ts + 17 + 1 bs,
//   the 17 the 15-row cell + 2) = 17 W at the base cell (1 + 13 + 2 + 1),
//   FOUR RAISED ONE-W BOXES side by side, the lane's whole height, each its
//   own ts / bs ring in colour set 1's tones (N rows 5 / 23 and the columns
//   at x 5, 23, 24, 557, 558, 576, 577, 595: the window-menu button at the
//   left, the title box, Minimize, Maximize at the right, all 19 x 19 =
//   17 x 17 here) — caption_button_w / h 17 at y 0, inset 0, no gaps, no
//   Close box (ruling 3: the Close slot IS the window-menu button at the
//   lane's left, caption_button_rects' cde arm; its menu carries the
//   verbs); NO ICON (dtwm draws none; the seat fields unread); the title
//   CENTERED in the title box, in the medium face (kGuiFaceSetCde), from
//   its ts column (caption_title_x_px 18 = the menu button's 17 + the box's
//   line) to one line before Minimize's box (caption_title_trail_px 1).
//   THE GLYPHS dtwm's raised bars, read off N: the menu button's bar 11 x
//   4 at (4, 7) of its 19 (x 9-19, y 12-15), Minimize's square 4 x 4 at
//   (7, 7) (x 565-568), Maximize's square 11 x 11 at (4, 4) (x 581-591,
//   y 9-19), each a raised one-px ring round the face — fitted to the
//   17-box as 10 x 4, 4 x 4 and 10 x 10, centred by integer division
//   (cde_paint.h's caption block); pressed, a box sinks (dtwm's armed
//   button), its glyph unmoved.
//   THE MENU ROW Motif's menu bar, 29 px on N (row 24 ts, 25-51 face, 52
//   bs; the cap's top on face row 9 = margins 6 + (ascent 13 − cap 10)):
//   head 1 (the ts row) + content 25 (the base cell 13 + 6 above and below:
//   the RowColumn's marginHeight 3, the cascade's one-px shadow and its
//   marginHeight 2) + foot 1 (the bs row) = 27 (ruling 2). THE BAR IS A
//   RAISED FORM THE LANE'S WHOLE WIDTH, ONE RING (paint_menu_row's cde arm;
//   re-read on C 2026-10-09): the ts along its top (C row 24) and down its
//   left edge (C x 5), the bs down its right edge (C x 654) and along its
//   bottom — Motif's 2 px there (C rows 52-53), the product's 1 W (architect
//   2026-10-09 ~03:15: "1") — mitred; the walk's lead below counts the left
//   ts column. THE PADS (re-read
//   2026-10-08 ~18:00, his ruling 5: "make it a little more accurate")
//   off N's text cells (the mnemonic underlines, row 46, are the cells'
//   extents): File's cell starts at x 16, 11 px past the bar's ts column
//   (x 5), and between File's cell end (x 41) and Edit's cell start (x 55)
//   lie 14 px — so the lead L (the bar's shadow and the RowColumn's
//   marginWidth) + the cascade's pad p = 11 and 2 p + the spacing s = 14.
//   The split is Motif's own arithmetic, the one that also gives the 6
//   rows above: the RowColumn's marginWidth 3 (L = 1 + 3 = 4,
//   menu_band_lead_px) and a menu bar's spacing 0 (the
//   armed boxes touch), the cascade's pad 7 = its one-px shadow + its
//   marginWidth 6 (menu_label_pad_left_px / _right_px) — every margin kept
//   as the px constant Motif's resources gave it (the settled rule) — and
//   THE NEDIT CAPTURE OF 2026-10-08 CONFIRMS IT (tmp/Screenshot_2026-10-08_
//   10-14-22.png, a real Motif application under NsCDE's Solaris palette,
//   its "File" title armed, re-measured): the bar's ts column x 510, the
//   armed box from x 514 (L 4), "File"'s ink 521-545 inside the box's
//   514-552 (7 a side), "Edit"'s ink from 560 (the next box flush at 553,
//   spacing 0). THE OPEN TITLE IS THAT ARMED BOX ETCHED IN: Motif's
//   XmNenableEtchedInMenu, which CDE's sys.resources sets True, draws the
//   armed cascade SUNKEN — a one-W ring, the bottom shadow on its top row
//   and left column, the top shadow on its bottom row and right column —
//   round the SELECT COLOR (XmNselectColor, cde_select; the capture's
//   3E7C8E is its body 4992A7 at 85 %), the label unpushed in the label
//   color (paint_cde_armed); THE BOX SPANS THE BAR'S BODY ROWS EXACTLY,
//   the content rows between the head's top shadow and the foot's bottom
//   shadow (the capture's 274-296 under the bar's 273 / 297, no body row
//   above or below it — the capture's relation kept over the RowColumn
//   margin the height's arithmetic counts, the bar's 27 unchanged), and
//   the anchor's whole width. Its pull-down hangs with its
//   top shadow ON the bar's bottom shadow, its left edge flush with the
//   box's (dropdown_hang_y), the armed item the same etched-in face.
//   (dtfile's flat toolbar under Motif's raised form stood below the
//   menu bar until the program's band took the lane, 2026-10-09; git
//   history.)
//   THE FRAME dtwm's 5 on all four sides (N rows 0-4 / 396-400: 2 light +
//   2 face + 1 dark on the top and left, mirrored on the bottom and right;
//   window_frame_lines 3 round the 2-W face) on every window
//   (window_frame_maximized), outside the app's geometry, with dtwm's
//   CORNER PIECES (cde_paint.h's frame block). THE SCROLL BAR 13
//   (scroll_bar_px; N rows 383-395 and report §3: the 1-px trough ring, the
//   11-px slider and arrows inside it) — the trim lane and the popup bar.
//   THE RULER LANE 4, BEHIND THE FLAGS (ruler_behind_flags; architect
//   2026-10-08 ~17:30): the lane table's leftover — the frame's 5 + 5 and
//   the stack 17 + 27 + the program's band 33 + 13 + 4 + 18 above the well
//   with the program's dock bar and row 8, 38, below it sum to the base's
//   122 + 38 (render.h's static_assert), so the well keeps the base's 960
//   device rows at 300 % (main.cpp's lane table); its two label terms
//   unread. (3 for the morning of 2026-10-09, while Motif's toolbar form
//   took a row of it.)
//   THE PUSH BUTTON Motif's XmPushButton on O: 25 tall (Cancel rows
//   340-364 = 1 + 23 + 1 round the 15-row cell + 8) = 23 at the base cell,
//   the dialog field's own height; its pad 6 (File Encoding's box x 136-237
//   against its ink 143-231: 7 to the ink less the F's 1-2 px bearing);
//   the default button's extra ring (O's OK, two px outside) NOT DRAWN.
//   THE TOOLTIP a Motif panel at the card's 2-W pad (CDE has no tooltip:
//   the cards and the tip take a raised panel in the body, black text).
//   THE SCRUB'S GRAB the base's 14, the slider drawn 11 wide (the scale,
//   cde_paint.h). THE DROPDOWN Motif's pulldown: items 19 (the cell 13 +
//   2 x (marginHeight 2 + the 1-W shadow), no capture shows one open —
//   derived), no margin inside the one-W raised frame (the RowColumn's 0).
//   NO ROUNDED CORNER; every disabled word MOTIF'S 50 % STIPPLE at one
//   device px a cell (architect 2026-10-08 ~17:45: "a combination of
//   period-authentic (no color blend) and modern (no pixelation)";
//   render.h's stipple pair — a dead button's glyph is the program's,
//   cool_edit_paint.h); THE ICONS
//   Tango's ("for internal testing", the architect: the GNOME set may
//   follow).
inline constexpr ChromeSpec kChromeSpecCde = {
    .key                          = "cde",
    .display_name                 = "CDE",
    .vocabulary                   = GuiChromeVocabulary::Cde,
    .default_palette              = "solaris",
    .face_set                     = &kGuiFaceSetCde,
    .caption_height_px            = 17,
    .caption_icon_x_px            = 0,
    .caption_icon_y_px            = 0,
    .caption_title_x_px           = 18,
    .caption_button_w_px          = 17,
    .caption_button_h_px          = 17,
    .caption_button_y_px          = 0,
    .caption_button_inset_px      = 0,
    .caption_button_gap_px        = 0,
    .caption_close_gap_px         = 0,
    .caption_title_trail_px       = 1,
    .menu_row_head_px             = 1,
    .menu_row_content_px          = 25,
    .menu_row_foot_px             = 1,
    .menu_label_pad_left_px       = 7.0,
    .menu_label_pad_right_px      = 7.0,
    .menu_band_lead_px            = 4.0,
    .ruler_label_cap_top_px       = 0,
    .ruler_baseline_to_marker_px  = 0,
    .ruler_behind_flags           = true,
    .ruler_lane_px                = 4,
    .scroll_bar_px                = 13,
    .push_button_box_px           = 23.0,
    .push_button_pad_left_px      = 6.0,
    .push_button_pad_right_px     = 6.0,
    .tooltip_pad_px               = 2,
    .scrub_handle_box_px          = 14.0,
    .popup_item_height_px         = 19,
    .popup_margin_px              = 0,
    .corner_radius_px             = 0,
    .window_frame_lines           = 3,
    .window_frame_maximized       = true,
    .icon_set                     = "tango",
};

// THE TABLE — every vocabulary, the `chrome` key's whole vocabulary in its
// order (is_chrome_key), the face install's probe walking it
// (gui_font_install_bundled: every set's faces must carry their band glyphs).
// WINDOWS-2000 FIRST, THE METRIC BASE AND THE DEFAULT (the head;
// kDefaultChromeKey below), then the vocabularies in their arrival's order.
// The Settings menu's Chrome row lists the keys in
// this order (its choice editor, kChromeChoiceSource, app_state.h).
inline constexpr const ChromeSpec* kGuiChromeSpecs[] = {
    &kChromeSpecWin2000,
    &kChromeSpecClearlooks,
    &kChromeSpecCde,
};

// A PREDICATE OVER EVERY INSTANCE — a static_assert that must hold for every
// vocabulary asks it (re-grepped 2026-10-08: the caption glyph cell's fit,
// paint_handler.cpp), so an instance that breaks it names the site.
template <typename Pred>
constexpr bool chrome_specs_all(Pred pred) {
    for (const ChromeSpec* s : kGuiChromeSpecs)
        if (!pred(*s)) return false;
    return true;
}

// THE `chrome` KEY'S GRAMMAR — the ONE owner, asked by the device config's
// reader (any other word the launch's first-error hard fail: a hand edit is
// the only producer) and by the settings editor's Chrome row (its refused
// commit): a vocabulary's key, byte for byte.
constexpr const ChromeSpec* chrome_spec_for_key(std::string_view v) {
    for (const ChromeSpec* s : kGuiChromeSpecs)
        if (v == s->key) return s;
    return nullptr;
}
constexpr bool is_chrome_key(std::string_view v) {
    return chrome_spec_for_key(v) != nullptr;
}
inline constexpr const char* kChromeGrammarReason =
    "must be windows-2000, clearlooks or cde";
// THE DEFAULT, a config with no `chrome` line (DeviceConfig::chrome's
// initializer spells it), and what the first run stamps: `windows-2000`
// (architect 2026-10-07 ~22:45, the head; clearlooks was the default from
// ~16:00 that day until then). The win2000 vocabulary's key is
// `windows-2000` (architect 2026-10-07: the term spelled whole); the
// earlier `win2000` is an unknown word, the launch's hard fail.
inline constexpr const char* kDefaultChromeKey = "windows-2000";
static_assert(is_chrome_key(kDefaultChromeKey));

// THE LIVE SPEC — the one instance the process paints (architect
// 2026-10-07): chosen ONCE, at launch, from the device config's `chrome`
// (set_live_chrome_spec, gui_main, right after the config's read and before
// the icon set's load, the window and the first paint, exactly as the scale
// is installed), and never moved — a Settings commit of `chrome` writes the
// file and takes effect at the next launch, because the faces' caches, the
// icon set and the lane memos are built on it. Construction state until
// then is the default's (warptempo_cli, which paints nothing, never sets
// it).
namespace chrome_spec_detail {
inline const ChromeSpec* g_live = chrome_spec_for_key(kDefaultChromeKey);
} // namespace chrome_spec_detail
inline const ChromeSpec& live_chrome_spec() {
    return *chrome_spec_detail::g_live;
}
// Precondition: is_chrome_key(key), the config's reader having judged it.
// ONE CALLER, gui_main, once per launch.
inline void set_live_chrome_spec(std::string_view key) {
    const ChromeSpec* s = chrome_spec_for_key(key);
    assert(s != nullptr);
    chrome_spec_detail::g_live = s;
}

#endif // WARPTEMPO_GUI_CHROME_SPEC_H
