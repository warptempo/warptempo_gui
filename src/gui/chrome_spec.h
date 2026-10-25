#ifndef WARPTEMPO_GUI_CHROME_SPEC_H
#define WARPTEMPO_GUI_CHROME_SPEC_H

// THE CHROME SPEC (architect 2026-10-06): the choices of the period desktop
// the chrome follows, read by the chokepoint that paints each element, never
// a literal at the painter. The colors are the materials' and the
// palette's (materials.h, palette_file.h), the chrome inheriting Cool Edit's
// tones (chrome_derive.h); the app paints no caption since 2026-10-24
// (render.h's caption note); the spec chooses
// shapes and seats. It names NO FACE: the face is the product's
// one (gui_font.h's gui_live_face_set), and no color (its own scheme, the
// `caption` key's default, stood 2026-10-08..10-21 and left with the
// captions).
//
// THE CHROME ENDS AT THE CAPTION (architect 2026-10-09 ~06:55, "chrome
// means chrome, the program is Cool Edit"; the menu row under it removed
// 2026-10-21, with its three pull-downs): the chrome is the caption, the
// window frame, the cards, the prompts and the
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
// owner, not a field here: the card frame's four sides (paint_popup_chrome), the keyboard's Backspace
// word (onscreen_keyboard.h's cap_word).

struct ChromeSpec {
    // (THE VOCABULARY'S FACE SET left the spec 2026-10-09: the face is the
    // product's one, gui_font.h.)
    // (THE CAPTION'S FIELDS — Windows 2000's SM_CYCAPTION 18, its
    // DFC_CAPTION buttons and the small icon's seat — left the spec
    // 2026-10-24 with the app's caption: labwc draws the laptop's title bar,
    // the tablet has none; the hi-fi caption of 2026-10-23 read none of them.)
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
    // (THE SCRUB THUMB'S GRAB BOX, scrub_handle_box_px — WIN2000 14, the
    // 11-px trackbar thumb widened to it, the inset the mapping took half of
    // at each end — left the spec 2026-10-25: the thumb's center runs to the
    // channel's ends and the thumb itself is the grab band, render.h's scrub
    // block.)
    // (THE DROPDOWN's item height and margin, Windows' popup menu, went with
    // the Settings popup, 2026-10-21.)
    // (THE NOTIFICATION CARDS' SEAT IS NO FIELD: ONE RULE FOR EVERY
    // VOCABULARY, read off the live lane table at the trim lane's first row —
    // notification_stack_bound, notifications.cpp; architect 2026-10-08.)
    // (THE WINDOW'S FRAME — its relief lines and whether it stood on a
    // maximized window — left the spec 2026-10-24 with the app's sizing
    // frame: labwc draws the laptop's borders.)
};

// WIN2000 (architect 2026-10-06), off the ReactOS captures: (THE CAPTION,
// Windows' SM_CYCAPTION 18 with its DFC_CAPTION buttons, until 2026-10-24;
// THE MENU ROW, Explorer's menu band and the plain menu bar's
// seat, 2026-10-02 to 2026-10-21, left with the row; git history); THE PUSH
// BUTTON Windows' 75 x 23 with 7-px pads; THE TOOLTIP comctl32's 2-px pad
// (tmp/reactos-tooltips.png) (THE DROPDOWN, Windows' popup menu, until
// 2026-10-21);
// THE ICONS Adwaita 3.22's drawings
// (icons.h's head; architect 2026-10-14). (Explorer's
// flat toolbar — the 31 x 30 case under one etched pair — stood below the
// menu row until the program's band took the lane, 2026-10-09; git history.)
inline constexpr ChromeSpec kChromeSpecWin2000 = {
    .push_button_box_px           = 23.0,
    .push_button_pad_left_px      = 7.0,
    .push_button_pad_right_px     = 7.0,
    .tooltip_pad_px               = 2,
};

// THE LIVE SPEC — the one instance the process paints, Windows 2000's.
inline constexpr const ChromeSpec& live_chrome_spec() {
    return kChromeSpecWin2000;
}

#endif // WARPTEMPO_GUI_CHROME_SPEC_H
