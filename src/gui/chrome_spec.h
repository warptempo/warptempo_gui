#ifndef WARPTEMPO_GUI_CHROME_SPEC_H
#define WARPTEMPO_GUI_CHROME_SPEC_H

// THE CHROME SPEC (architect 2026-10-06): the choices of the period desktop
// the chrome follows, read by the chokepoint that paints each element, never
// a literal at the painter. The colors are the materials' and the
// palette's (materials.h, palette_file.h), the chrome inheriting Cool Edit's
// tones (chrome_derive.h), the caption the walnut (materials.h's WOOD); the
// spec chooses shapes and seats. It names NO FACE: the face is the product's
// one (gui_font.h's gui_live_face_set), and no color (its own scheme, the
// `caption` key's default, stood 2026-10-08..10-21 and left with the
// captions).
//
// THE CHROME ENDS AT THE CAPTION (architect 2026-10-09 ~06:55, "chrome
// means chrome, the program is Cool Edit"; the menu row under it removed
// 2026-10-21): the chrome is the caption, the window frame, the pull-downs,
// the cards, the prompts and the
// dialogs with their fields and buttons, the scrub and the well's frame (the
// canvas until the program's later parts take it); THE TOOLBAR BAND, ROW 8
// AND THE CANVAS COLUMN'S LANES — the view bar, the ruler, the cues — ARE
// THE PROGRAM'S, Cool Edit's (program_spec.h) — so no field here shapes a
// toolbar case, a toolbar band, row 8's air, the trim lane, the ruler or a
// dead button's glyph.
//
// THERE IS ONE CHROME, WINDOWS 2000 (architect 2026-10-10, the Chrome row
// and the `chrome` device key removed: "remove the option of changing
// Chromes" — the Windows 2000 layout's thin caption gives the largest
// waveform, and a taller vocabulary such as Windows XP's would take canvas
// rows; no second vocabulary is planned): Windows 2000's "Windows Standard"
// chrome as ReactOS 0.4.16 draws it (architect 2026-10-06: "pivot to Win2K
// as the official supported version and focus on that"; its classic chrome
// is that scheme in every colour and metric), read off the architect's
// ReactOS captures (tmp/reactos*.png, "get it as accurate to ReactOS as we
// can"). WHERE REACTOS DEPARTS FROM WINDOWS 2000 THE PRODUCT FOLLOWS
// REACTOS, on purpose and under the Windows 2000 name
// (docs/engineering/win2000_deviations.md lists the departures; a
// provenance-clean capture of the real OS beats ReactOS wherever they
// differ, 2026-10-09). C++ identifiers and file names keep `Win2000` /
// `win2000` as the abbreviation (kChromeSpecWin2000,
// docs/engineering/win2000_deviations.md), and prose may say win2000.
// Windows 95's chrome, Clearlooks and CDE were dropped (they stand in git
// history).
// The one instance is kChromeSpecWin2000 below, read through
// live_chrome_spec(). What the chrome never varies is a plain constant at its
// owner, not a field here: the caption icon's 16-px size (kCaptionIconPx),
// the card frame's four sides (paint_popup_chrome), the keyboard's Backspace
// word (onscreen_keyboard.h's cap_word).

struct ChromeSpec {
    // (THE VOCABULARY'S FACE SET left the spec 2026-10-09: the face is the
    // product's one, gui_font.h.)
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
    // (THE MENU ROW'S FIELDS — its face row and 19-W content, the anchors'
    // 9 / 7 label pads and the 2-W lead, Explorer's menu band and the plain
    // menu bar's seat — left the spec 2026-10-21 with the row.)
    // (THE RULER LANE'S FIELDS left the spec 2026-10-09: the ruler is the
    // program's, Cool Edit's under every chrome, program_spec.h.)
    // (THE SCROLL BAR'S THICKNESS, scroll_bar_px — Windows' SM_CXVSCROLL 16,
    // the popup lists' vertical bar's width from 2026-10-08 and the trim
    // lane's height and its caps' square 2026-10-08 to 2026-10-09 — left the
    // spec 2026-10-14 with the popup lists' scroll bar, its last reader.)
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
    // every chrome, its height and its 5-W pad program_spec.h's.)
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
    // VOCABULARY, read off the live lane table at the trim lane's first row —
    // notification_stack_bound, notifications.cpp; architect 2026-10-08.)
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
};

// WIN2000 (architect 2026-10-06), off the ReactOS captures: THE CAPTION
// Windows' SM_CYCAPTION 18 with its DFC_CAPTION buttons 16 x 14 two px down
// and in, Minimise and Maximise touching, two px before Close, the small
// icon at (2, 1), the title's pen two px past it (render.h's caption
// block); (THE MENU ROW, Explorer's menu band and the plain menu bar's
// seat, 2026-10-02 to 2026-10-21, left with the row; git history); THE PUSH
// BUTTON Windows' 75 x 23 with 7-px pads; THE TOOLTIP comctl32's 2-px pad
// (tmp/reactos-tooltips.png); THE DROPDOWN Windows' popup menu, 17-px items
// one px inside the frame, hanging from its anchor's foot (dropdown_hang_y);
// THE ICONS Adwaita 3.22's drawings
// (icons.h's head; architect 2026-10-14). (Explorer's
// flat toolbar — the 31 x 30 case under one etched pair — stood below the
// menu row until the program's band took the lane, 2026-10-09; git history.)
inline constexpr ChromeSpec kChromeSpecWin2000 = {
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
    .push_button_box_px           = 23.0,
    .push_button_pad_left_px      = 7.0,
    .push_button_pad_right_px     = 7.0,
    .tooltip_pad_px               = 2,
    .scrub_handle_box_px          = 14.0,
    .popup_item_height_px         = 17,
    .popup_margin_px              = 1,
    .window_frame_lines           = 2,
    .window_frame_maximized       = false,
};

// THE LIVE SPEC — the one instance the process paints, Windows 2000's.
inline constexpr const ChromeSpec& live_chrome_spec() {
    return kChromeSpecWin2000;
}

#endif // WARPTEMPO_GUI_CHROME_SPEC_H
