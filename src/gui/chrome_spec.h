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
// literal at the painter. The colours stay the theme's (theme_file.h); the
// spec chooses shapes, seats and faces, and names the theme a config with
// no `theme` line wears.
//
// TWO VOCABULARIES, ONE LIVE, CHOSEN AT LAUNCH (the `chrome` device key,
// architect 2026-10-07, the planner's design under his free rein:
// "authenticity wherever possible; the most Clearlooks assets; Windows
// 2000's metrics the tie-break"):
//   WIN2000 — Windows 2000's "Windows Standard" chrome as ReactOS 0.4.16
//     draws it (architect 2026-10-06: "pivot to Win2K as the official
//     supported version and focus on that"; its classic chrome is that
//     scheme in every colour and metric; Windows XP's Classic and Server
//     2003's are the same), read off the architect's ReactOS captures
//     (tmp/reactos*.png, "get it as accurate to ReactOS as we can"). WHERE
//     REACTOS DEPARTS FROM WINDOWS 2000 THE PRODUCT FOLLOWS REACTOS, on
//     purpose and under the Windows 2000 name
//     (docs/engineering/win2000_deviations.md lists the departures). The
//     default when the config names no chrome.
//   CLEARLOOKS — GNOME 2.30's Clearlooks as Debian 6 squeeze drew it
//     (gtk-engines 2.20.2's GUMMY style under gnome-themes 2.30.2's gtkrc
//     and metacity theme), his squeeze captures the law (tmp/squeeze/,
//     INDEX.md), WORN AT WINDOWS' PROPORTIONS (architect 2026-10-07 ~02:20:
//     "Windows is the base: a compact, nicely proportioned layout"; the
//     instance's head). THE TOP STRIP IS CLEARLOOKS' OWN DRAWING since the
//     painters round's first part (2026-10-07: the metacity caption, the
//     menu bar, the toolbar band and its gummy buttons, the separators,
//     GTK's insensitive icon — clearlooks_paint.h); the rest of the chrome
//     (the dialogs, the trim lane, the scrub, the cards, the restored
//     laptop's frame) still draws Windows' faces in Clearlooks' lengths
//     until the round's later parts teach each one its branch.
// Windows 95's chrome, the second vocabulary of 2026-10-06's morning, is
// dropped (it stands in git history); its colours remain an ordinary theme
// file. The table is kGuiChromeSpecs below, the live instance
// live_chrome_spec(), set once by set_live_chrome_spec before the first
// paint. WHERE THE TWO DRAW DIFFERENTLY THE PAINTER FORKS ON THE SPEC'S
// `vocabulary` (or on the toolbar style or the disabled glyph, where that
// is the natural switch), the win2000 arm the code that stood before the
// fork. What no vocabulary is expected to vary is a plain constant at its
// owner, not a field here: the caption icon's 16-px size (kCaptionIconPx),
// the card frame's four sides (paint_popup_chrome), the keyboard's
// Backspace word (onscreen_keyboard.h's cap_word) — each until a painter
// branch of a later round says otherwise.

// WHICH PERIOD DESKTOP DRAWS THE CHROME — the painters' one switch where
// the two draw an element differently (the head): the caption's band,
// title and buttons, the menu bar and its open title, the toolbar band,
// the disabled words' emboss.
enum class GuiChromeVocabulary {
    Win2000,
    Clearlooks,
};

// THE ROSTER'S TWO TOOLBARS' FACES — the icon row's and row 8's cases and
// the render player's row (paint_toolbar_box, paint_handler.cpp, where the
// faces stand), and the separator in their group gaps
// (paint_toolbar_separator). Both styles have a HOT face, the one hover
// face the product draws (toolbar_style_has_hot_face).
enum class GuiToolbarStyle {
    // Explorer's FLAT toolbar (comctl32's TBSTYLE_FLAT as ReactOS 0.4.16's
    // toolbar.c draws it): no edge at rest, one raised line on the HOT case,
    // one sunken line pressed and checked, and an ETCHED SEPARATOR in every
    // group gap (TOOLBAR_DrawFlatSeparator).
    Flat,
    // GTK 2's tool button, GtkToolbar::button-relief NONE, as Clearlooks'
    // GUMMY style draws it (clearlooks_paint.h's tool button): nothing at
    // rest, the gummy button in bg[PRELIGHT] on the HOT case, in bg[ACTIVE]
    // with the inner shadow pressed and checked (the glyph one W px right
    // and down, GtkButton::child-displacement), and the gummy separator in
    // every group gap (clearlooks_gummy_draw_separator).
    GtkReliefNone,
};
constexpr bool toolbar_style_has_hot_face(GuiToolbarStyle s) {
    return s == GuiToolbarStyle::Flat || s == GuiToolbarStyle::GtkReliefNone;
}

// THE DISABLED GLYPH'S RULE (icons::draw_disabled, svg_icon.h): a dead
// button's drawing as its desktop greyed it.
enum class GuiDisabledGlyph {
    // ReactOS's comctl32 imagelist: ILS_SATURATE | ILS_ALPHA at 192.
    ReactOSSaturate,
    // GTK 2's insensitive icon: gdk_pixbuf_saturate_and_pixelate (0.8, TRUE).
    GtkSaturatePixelate,
};

// WHERE THE NOTIFICATION CARDS' STACK STARTS against the icon row's band
// (notification_stack_bound, notifications.cpp). One seat today, both
// instances', asserted there over every instance like the toolbar style.
enum class GuiCardSeat {
    // On the band's foot, the card wholly below the toolbar (the planner's
    // interim, 2026-10-06: the band is the 30-px case and holds no card).
    UnderBand,
};

struct ChromeSpec {
    // THE VOCABULARY'S NAME, the `chrome` device key's value that chooses it
    // (is_chrome_key below).
    const char*        key;
    // THE PAINTERS' SWITCH (GuiChromeVocabulary above).
    GuiChromeVocabulary vocabulary;
    // THE THEME A CONFIG WITH NO `theme` LINE WEARS under this chrome
    // (effective_theme_key, theme_file.h): the vocabulary's own colours.
    const char*        default_theme;
    // The vocabulary's text: the faces, their metrics and tracking (gui_font.h).
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
    // THE TOOLBAR CASE of the icon row and row 8, in Windows px (render.h's
    // icon-row block, where the case's rule and its rounding stand): the
    // case left of and above the glyph, the glyph's square seat, the case
    // right of it and below it.
    int                toolbar_case_lead_px;
    int                toolbar_glyph_px;
    int                toolbar_case_trail_x_px;
    int                toolbar_case_trail_y_px;
    GuiToolbarStyle    toolbar_style;
    // THE GROUP GAP of both toolbars and the separator standing in it, in
    // Windows px (render.h's icon-row block, paint_toolbar_separator): the
    // gap's width, the separator's first column from the gap's left edge,
    // and its inset from the case's top and bottom.
    int                toolbar_group_gap_px;
    int                toolbar_separator_x_px;
    int                toolbar_separator_inset_y_px;
    // THE ICON ROW'S STACK (render.h's icon-row block): whether ONE ETCHED
    // PAIR heads the lane under the menu row, the ground above and below the
    // case inside the toolbar band, and the ground between the band and the
    // trim lane.
    bool               icon_row_etched_pair;
    int                icon_row_air_px;
    int                icon_row_foot_px;
    // ROW 8'S OWN AIR above and below its case (render.h's bottom-row block).
    int                bottom_row_air_px;
    // THE PUSH BUTTON — the dialogs' word buttons (paint_handler.cpp's
    // kModal* block): the box's height and the label's pads left and right.
    double             push_button_box_px;
    double             push_button_pad_left_px;
    double             push_button_pad_right_px;
    // THE TOOLTIP'S PAD inside its frame, each way (paint_popup_chrome's
    // tooltip arm, kTooltipLineGapPx beside it).
    int                tooltip_pad_px;
    GuiCardSeat        card_seat;
    // THE CORNERS' RADIUS in Windows px, scaled like any length and drawn
    // antialiased (clearlooks_paint.cpp: the gummy button, the open menu
    // title): 0 is a square corner, every box of win2000's.
    int                corner_radius_px;
    // A DEAD BUTTON'S GLYPH (GuiDisabledGlyph above).
    GuiDisabledGlyph   disabled_glyph;
    // THE ICON SET the vocabulary wears: a bundled folder under
    // assets/icons/, parsed at launch through resvg (icons.h's head).
    const char*        icon_set;
};

// WIN2000 (architect 2026-10-06), off the ReactOS captures: THE CAPTION
// Windows' SM_CYCAPTION 18 with its DFC_CAPTION buttons 16 x 14 two px down
// and in, Minimise and Maximise touching, two px before Close, the small
// icon at (2, 1), the title's pen two px past it (render.h's caption
// block); THE MENU ROW Explorer's menu band, the 19-row button under one
// face row, the label 9 px in and 7 after, the rebar's 2-px lead
// (paint_handler.cpp's kMenuLabel* record); THE TOOLBARS (architect
// 2026-10-06, on mock_TF1 and the captures) Windows' LARGE case, 31 x 30 —
// lead 3, the 24-px seat, trail 4 right and 3 below — in Explorer's flat
// style, the band the case itself (Explorer's button rect is 30 rows, rows
// 182-211 of tmp/reactos-hover.png) under ONE etched pair with 4 px of
// ground before the trim lane, row 8 keeping its own 3 / 3 air, the groups
// 8 px apart round comctl32's etched separator at (8 / 2 − 1) from the
// band's top + 2 to its bottom − 2 (render.h's icon-row block); THE PUSH
// BUTTON Windows' 75 x 23 with 7-px pads; THE TOOLTIP comctl32's 2-px pad
// (tmp/reactos-tooltips.png); THE ICONS Tango 0.8.90's scalable drawings,
// ReactOS's own model of a Windows 2000 desktop dressed in Tango (architect
// 2026-10-06: assets/icons/tango/, its README the mapping), the disabled
// glyph ReactOS's saturate.
inline constexpr ChromeSpec kChromeSpecWin2000 = {
    .key                          = "win2000",
    .vocabulary                   = GuiChromeVocabulary::Win2000,
    .default_theme                = "windows-2000-standard",
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
    .toolbar_case_lead_px         = 3,
    .toolbar_glyph_px             = 24,
    .toolbar_case_trail_x_px      = 4,
    .toolbar_case_trail_y_px      = 3,
    .toolbar_style                = GuiToolbarStyle::Flat,
    .toolbar_group_gap_px         = 8,
    .toolbar_separator_x_px       = 8 / 2 - 1,
    .toolbar_separator_inset_y_px = 2,
    .icon_row_etched_pair         = true,
    .icon_row_air_px              = 0,
    .icon_row_foot_px             = 4,
    .bottom_row_air_px            = 3,
    .push_button_box_px           = 23.0,
    .push_button_pad_left_px      = 7.0,
    .push_button_pad_right_px     = 7.0,
    .tooltip_pad_px               = 2,
    .card_seat                    = GuiCardSeat::UnderBand,
    .corner_radius_px             = 0,
    .disabled_glyph               = GuiDisabledGlyph::ReactOSSaturate,
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
//   (right_titlebar_edge 1 + its border 1); the glyphs at icon_size =
//   Bmin 7 `max` (16 − Bpad 6 x 2) = 7, centred (hpadding 4); the menu
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
//   THE TOOLBARS GtkToolbar (gtktoolbar.c): the tool button 32 x 32, the
//   24-px icon + 2 x the button style's xthickness 3 + inner-border 1 + 1
//   (gtkbutton.c gtk_button_size_request), GtkWidget::focus-line-width and
//   focus-padding ZEROED (architect 2026-10-07 ~02:20: the case at the
//   base's proportion — squeeze's own 36 added 2 x (1 + 1) of focus
//   ring, which the product never draws), the icon at (4, 4); 2 px of air
//   each way (ythickness 1 + GtkToolbar::internal-padding 1), the band 36;
//   no etched pair (the menu bar's last row is the line) and no foot (GTK
//   stacks flush); row 8 the same 2-px air. THE GROUP GAP 12
//   (GtkSeparatorToolItem's DEFAULT_SPACE_SIZE) round the separator's pair
//   at 5-6 ((12 − xthickness 1) / 2) from 20 % to 80 % of the 32-row item
//   (_gtk_toolbar_paint_space_line: 32 x 2 / 10 = 6 to 32 x 8 / 10 = 25,
//   both rows drawn — the 36-row item's 7..28 on his gedit capture), 6 rows
//   in at each end.
//   THE PUSH BUTTON the cell 13 + 2 x (xthickness 3 + focus-line-width 1 +
//   focus-padding 1 + inner-border 1) = 25 (his file chooser's Open, 29 at
//   the 17 cell), the label 6 px in each way; THE TOOLTIP the tooltips
//   style's 4-px pad.
//   THE DRAWING: the gummy toolbar style (GtkReliefNone), radius 3 on the
//   gummy boxes (the gtkrc's `radius = 3.0`), GTK's insensitive icon, the
//   icons "mist" (gnome-icon-theme 2.30's drawings under Mist's own
//   folders, assets/icons/mist/).
inline constexpr ChromeSpec kChromeSpecClearlooks = {
    .key                          = "clearlooks",
    .vocabulary                   = GuiChromeVocabulary::Clearlooks,
    .default_theme                = "clearlooks",
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
    .toolbar_case_lead_px         = 4,
    .toolbar_glyph_px             = 24,
    .toolbar_case_trail_x_px      = 4,
    .toolbar_case_trail_y_px      = 4,
    .toolbar_style                = GuiToolbarStyle::GtkReliefNone,
    .toolbar_group_gap_px         = 12,
    .toolbar_separator_x_px       = 5,
    .toolbar_separator_inset_y_px = 6,
    .icon_row_etched_pair         = false,
    .icon_row_air_px              = 2,
    .icon_row_foot_px             = 0,
    .bottom_row_air_px            = 2,
    .push_button_box_px           = 25.0,
    .push_button_pad_left_px      = 6.0,
    .push_button_pad_right_px     = 6.0,
    .tooltip_pad_px               = 4,
    .card_seat                    = GuiCardSeat::UnderBand,
    .corner_radius_px             = 3,
    .disabled_glyph               = GuiDisabledGlyph::GtkSaturatePixelate,
    .icon_set                     = "mist",
};

// THE TABLE — every vocabulary, the `chrome` key's whole vocabulary in its
// order (is_chrome_key), the face install's probe walking it
// (gui_font_install_bundled: every set's faces must carry their band glyphs).
inline constexpr const ChromeSpec* kGuiChromeSpecs[] = {
    &kChromeSpecWin2000,
    &kChromeSpecClearlooks,
};

// A PREDICATE OVER EVERY INSTANCE — the static_asserts of the painters that
// draw one value of an enumerated field (the toolbar style, the card seat)
// ask it, so a second value in any instance names every site to teach.
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
    "must be win2000 or clearlooks";
// THE DEFAULT, a config with no `chrome` line (DeviceConfig::chrome's
// initializer spells it).
inline constexpr const char* kDefaultChromeKey = "win2000";
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
inline const ChromeSpec* g_live = &kChromeSpecWin2000;
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
