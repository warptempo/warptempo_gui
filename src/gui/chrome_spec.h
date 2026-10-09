#ifndef WARPTEMPO_GUI_CHROME_SPEC_H
#define WARPTEMPO_GUI_CHROME_SPEC_H

#include <cassert>
#include <string_view>

// THE CHROME SPEC (architect 2026-10-06): the choices of the period desktop
// the chrome follows, for the elements a later vocabulary may draw
// differently — read by the chokepoint that paints each element, never a
// literal at the painter. The colors stay the chrome's compiled theme's
// (theme_file.h's kGuiChromeThemes, by this spec's key) and the palette's
// (palette_file.h); the spec chooses shapes and seats, and names the
// scheme a config with no `scheme` line takes. It names NO FACE: the face
// is the device config's `font` key alone, under every chrome (architect
// 2026-10-09 ~21:20, "the font is its own drop-down"; gui_font.h's
// gui_live_face_set).
//
// THE CHROME ENDS AT THE MENU ROW (architect 2026-10-09 ~06:55, "chrome
// means chrome, the program is Cool Edit"): the chrome is the caption, the
// menu row, the window frame, the pull-downs, the cards, the prompts and the
// dialogs with their fields and buttons, the scrub and the well's frame (the
// canvas until the program's later parts take it); THE TOOLBAR BAND, ROW 8
// AND THE CANVAS COLUMN'S LANES — the view bar, the ruler, the cues — ARE
// THE PROGRAM'S, Cool Edit's under every chrome (program_spec.h) — so no
// field here shapes a toolbar case, a toolbar band, row 8's air, the trim
// lane, the ruler or a dead button's glyph.
//
// THE NAME IS `windows-2000` (architect 2026-10-07): the `chrome` key's
// value (its compiled theme is the catalog entry `windows-2000-standard`,
// the scheme's word beside it, the built-in schemes' keys being the
// catalog's). C++
// IDENTIFIERS AND FILE NAMES KEEP `Win2000` / `win2000` AS THE ABBREVIATION
// (kChromeSpecWin2000, GuiChromeVocabulary::Win2000,
// docs/engineering/win2000_deviations.md), and prose may say win2000 for the
// vocabulary; only a KEY is spelled whole.
//
// ONE VOCABULARY TODAY, THE TABLE BUILT FOR SEVERAL, ONE LIVE, CHOSEN AT
// LAUNCH (the `chrome` device key, architect 2026-10-07; THE WINDOWS LINE
// ALONE, architect 2026-10-09 ~21:20 — "I'm definitely adding XP",
// "probably stop at XP, maybe 7": WINDOWS XP is the next vocabulary, a
// second instance on this road):
//   WIN2000 (the key `windows-2000`) — Windows 2000's "Windows Standard"
//     chrome as ReactOS 0.4.16 draws it (architect 2026-10-06: "pivot to Win2K as the official
//     supported version and focus on that"; its classic chrome is that
//     scheme in every colour and metric; Windows XP's Classic and Server
//     2003's are the same), read off the architect's ReactOS captures
//     (tmp/reactos*.png, "get it as accurate to ReactOS as we can"). WHERE
//     REACTOS DEPARTS FROM WINDOWS 2000 THE PRODUCT FOLLOWS REACTOS, on
//     purpose and under the Windows 2000 name
//     (docs/engineering/win2000_deviations.md lists the departures).
// THE SETTLED RULE FOR EVERY LATER VOCABULARY (architect 2026-10-07; XP's
// law): PERIOD AUTHENTIC EXCEPT FOR THE PROPORTIONAL FIT TO WINDOWS 2000'S
// LAYOUT — its chrome from the toolkit's own sources and the period's
// captures (a provenance-clean capture of the real OS beats ReactOS wherever
// they differ, 2026-10-09), every height its source derives from the text
// cell RE-DERIVED AT THE BASE'S 13-ROW CELL by the source's own arithmetic,
// the base's 24-W icon seat, line weights as the sources give them, what
// cannot be made to fit scratched, not forced.
// A TALLER CHROME SIMPLY TAKES ITS ROWS FROM THE CANVAS (architect
// 2026-10-09 ~23:30: "because we're dropping the other chromes, I think we
// can drop the rule about the waveform height has to always be the same …
// perhaps more preferable would just be to let the waveform get a little
// smaller"): the well is each chrome's own leftover (main.cpp's lane table,
// centered_leftover_h), so a vocabulary whose caption or menu row stands
// taller than Windows 2000's shows that many rows less canvas; the
// program's lanes — the band, the canvas column's, row 8 — never flex to
// absorb a chrome's height, and nothing is clipped. (Every vocabulary's
// height differences packed into the ruler lane or its separators, so all
// wells stood equal, 2026-10-07 to 2026-10-09.)
// A LATER VOCABULARY'S ELEMENT MAY BE SMALLER THAN THE BASE'S, NEVER LARGER
// WITHOUT HIS RULING (architect 2026-10-08 ~17:45, beside the proportional
// fit: "ok to make larger elements smaller …; making them larger than
// Windows' metric when the historical frame wants the space is done only
// after consideration"): where a source draws an element smaller than
// Windows' metric the vocabulary takes the source's own (scroll_bar_px, the
// popup scroll bar, is the field it was first ruled on), and where a source
// draws it larger the base's metric stands until he rules otherwise.
// WINDOWS-2000 IS THE DEFAULT AND THE METRIC BASE (architect 2026-10-07
// ~22:45: "we could revert the default theme to Windows 2000; I just need
// to work out a good color palette", the color picker's purpose). The table
// (kGuiChromeSpecs) keeps windows-2000 FIRST: it is the metric base every
// later vocabulary is fitted to, and the table's order is the compiled
// themes' and the color picker's chromes' own schemes' (palette_file.cpp's
// assert).
// Windows 95's chrome, the second vocabulary of 2026-10-06's morning, is
// dropped (it stands in git history); its colors remain in the theme
// catalog's record (`windows-95-standard`, tools/theme_catalog). The table is
// kGuiChromeSpecs below, the live instance
// live_chrome_spec(), set once by set_live_chrome_spec before the first
// paint. WHERE A LATER VOCABULARY DRAWS DIFFERENTLY THE PAINTER FORKS ON THE
// SPEC'S `vocabulary`, the win2000 arm the code that stands today. What no
// vocabulary is expected to vary is a plain constant at its
// owner, not a field here: the caption icon's 16-px size (kCaptionIconPx),
// the card frame's four sides (paint_popup_chrome), the keyboard's
// Backspace word (onscreen_keyboard.h's cap_word) — each until a painter
// branch of a later round says otherwise.

// WHICH PERIOD DESKTOP DRAWS THE CHROME — the painters' one switch where
// a later vocabulary draws an element differently (the head); one value
// today, Windows XP's to join it. A later vocabulary's arm is added only
// where its desktop draws otherwise than Windows 2000 in its own scheme's
// roles (2026-10-08).
enum class GuiChromeVocabulary {
    Win2000,
};

struct ChromeSpec {
    // THE VOCABULARY'S NAME, the `chrome` device key's value that chooses it
    // (is_chrome_key below).
    const char*        key;
    // THE VOCABULARY'S SHOWN NAME, Title Case (TEXT: names Title Case): the
    // one source of every place the user reads a vocabulary's name — the
    // Settings editor's Chrome combo and its list (kChromeChoiceSource); the
    // key stays the file grammar's. (The color picker names a built-in
    // scheme by its own display name, palette_file.h's kGuiChromeSchemes.)
    const char*        display_name;
    // THE PAINTERS' SWITCH (GuiChromeVocabulary above).
    GuiChromeVocabulary vocabulary;
    // THE CHROME'S OWN BUILT-IN SCHEME'S KEY (2026-10-08 ~11:10: the catalog
    // key verbatim, `windows-2000-standard`; palette_file.h's head): THE
    // SCHEME A CONFIG WITH NO `scheme` LINE TAKES (effective_scheme_name,
    // 2026-10-08 ~18:15), which carries no keys under this chrome — the
    // compiled theme exactly. It names no palette: the default palette is
    // Cool Edit's one "Default" under every chrome (2026-10-09,
    // palette_file.h's head).
    const char*        own_scheme;
    // (THE VOCABULARY'S FACE SET left the spec 2026-10-09: the `font` device
    // key names the face under every chrome, gui_font.h.)
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
    // row's cut).
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
    // (THE RULER LANE'S FIELDS left the spec 2026-10-09: the ruler is the
    // program's, Cool Edit's under every chrome, program_spec.h.)
    // THE SCROLL BAR'S THICKNESS in Windows px — the popup lists' vertical
    // bar's width (render.h's popup scroll block): Windows' SM_CXVSCROLL 16
    // under win2000; a later vocabulary's own where its source's is smaller
    // (the smaller-element rule at the head). (It was the trim lane's
    // height and its caps' square too, 2026-10-08 to 2026-10-09; the trim
    // lane is the program's view bar since.)
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
    // field is the program's since 2026-10-09, Cool Edit's dark field under
    // every chrome, its height and its own 4-W pad program_spec.h's.)
    // THE SCRUB THUMB'S GRAB BOX — the box the press router takes as the
    // thumb's grab band and the mapping insets the track by half of at each
    // end (render.h's scrub block, scrub_handle_box_px). WIN2000 14 (the
    // laptop pixel's 20 re-authored, architect 2026-10-02), the 11-px
    // trackbar thumb widened to it.
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
    // antialiased: 0 is a square corner, every box of win2000's. NO PAINTER
    // READS IT TODAY (2026-10-09); the field is KEPT for Windows XP, the
    // next vocabulary, whose caption and buttons may want rounded corners —
    // its first reader is that vocabulary's painter arm.
    int                corner_radius_px;
    // THE WINDOW'S FRAME (render.h's sizing-frame block, window_frame_px):
    // the relief lines of its composite — Windows' two-line raised edge
    // under win2000 — round the 2-W face; and whether it stands while the
    // window is maximized too: Windows hides its sizing frame then, so the
    // field is false; a vocabulary whose window manager keeps its frame on
    // every window would set it, and both platforms then keep the band
    // outside the app's geometry on the maximized window and the tablet's
    // (platform_wayland.h's frame_px_, platform_android.h's).
    int                window_frame_lines;
    bool               window_frame_maximized;
    // THE ICON SET the vocabulary wears BY DEFAULT: a bundled folder under
    // assets/icons/ (icons.h's kIconSetKeys, static_asserted there), parsed
    // at launch through resvg (icons.h's head) — the set an absent `icons`
    // device key means; the key names another for every chrome (architect
    // 2026-10-09, gui_live_icon_set).
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
    .own_scheme                   = "windows-2000-standard",
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

// THE TABLE — every vocabulary, the `chrome` key's whole vocabulary in its
// order (is_chrome_key).
// WINDOWS-2000 FIRST, THE METRIC BASE AND THE DEFAULT (the head;
// kDefaultChromeKey below), then a later vocabulary in its arrival's order
// (Windows XP next, 2026-10-09). The Settings menu's Chrome row lists the
// keys in this order (its choice editor, kChromeChoiceSource, app_state.h).
inline constexpr const ChromeSpec* kGuiChromeSpecs[] = {
    &kChromeSpecWin2000,
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
    "must be windows-2000";
// THE DEFAULT, a config with no `chrome` line (DeviceConfig::chrome's
// initializer spells it), and what the first run stamps: `windows-2000`
// (architect 2026-10-07 ~22:45, the head). The win2000 vocabulary's key is
// `windows-2000` (architect 2026-10-07: the term spelled whole); the
// earlier `win2000` is an unknown word, the launch's hard fail, as is every
// word no vocabulary owns (a device config naming one is fixed by hand,
// docs/INSTALL.md).
inline constexpr const char* kDefaultChromeKey = "windows-2000";
static_assert(is_chrome_key(kDefaultChromeKey));

// THE LIVE SPEC — the one instance the process paints (architect
// 2026-10-07): chosen ONCE, at launch, from the device config's `chrome`
// (set_live_chrome_spec, gui_main, right after the config's read and before
// the icon set's load, the window and the first paint, exactly as the scale
// is installed), and never moved — a Settings commit of `chrome` writes the
// file and takes effect at the next launch, because the icon set and the
// lane memos are built on it. Construction state until
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
