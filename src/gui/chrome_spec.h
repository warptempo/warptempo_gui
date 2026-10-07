// THE ONE INCLUDE OUTSIDE THE GUARD: a ChromeSpec names a face set, so the
// set (gui_font.h) comes first; gui_font.h in turn includes this header after
// its set and before its live block, which reads kLiveChromeSpec. With the
// guard below the include, either header may be included first.
#include "gui_font.h"

#ifndef WARPTEMPO_GUI_CHROME_SPEC_H
#define WARPTEMPO_GUI_CHROME_SPEC_H

// THE CHROME SPEC (architect 2026-10-06): the choices of the period desktop
// the chrome follows, for the elements a later vocabulary may draw
// differently — read by the chokepoint that paints each element, never a
// literal at the painter. The colours stay the theme's (theme_file.h); the
// spec chooses shapes, seats and faces.
//
// ONE VOCABULARY, ONE INSTANCE (architect 2026-10-06: "pivot to Win2K as the
// official supported version and focus on that"): Windows 2000's "Windows
// Standard" chrome as ReactOS 0.4.16 draws it (its classic chrome is that
// scheme in every colour and metric; Windows XP's Classic and Server 2003's
// are the same), read off the architect's ReactOS captures
// (tmp/reactos*.png, "get it as accurate to ReactOS as we can"). WHERE
// REACTOS DEPARTS FROM WINDOWS 2000 THE PRODUCT FOLLOWS REACTOS, on purpose
// and under the Windows 2000 name (docs/engineering/win2000_deviations.md
// lists the departures). Windows 95's chrome, the second vocabulary of
// 2026-10-06's morning, is dropped (it stands in git history); its colours
// remain an ordinary theme file. THE TABLE STAYS A TABLE so a later
// vocabulary (CDE's, paused) joins it as a second instance read off a device
// key; until then there is no `chrome=` key and kLiveChromeSpec is the one
// instance. What no later vocabulary is expected to vary is a plain constant
// at its owner, not a field here: the caption's smooth gradient
// (paint_caption_gradient), the caption buttons' glyphs (paint_handler.cpp's
// caption glyph block), the menu lane's face row above its content
// (render.h's kMenuRowHeadPx), the open menu title's sunken box
// (paint_menu_row), the card frame's four sides and the tooltip's pad
// (paint_popup_chrome, kTooltipPadPx), the keyboard's Backspace word
// (onscreen_keyboard.h's cap_word).

// THE ROSTER'S TWO TOOLBARS' FACES — the icon row's and row 8's cases
// (paint_toolbar_box, paint_handler.cpp, where the faces stand). One style
// today; each painter that draws only this style says so in a static_assert,
// so a second value names every site it must teach.
enum class GuiToolbarStyle {
    // Explorer's FLAT toolbar (comctl32's TBSTYLE_FLAT as ReactOS 0.4.16's
    // toolbar.c draws it): no edge at rest, one raised line on the HOT case,
    // one sunken line pressed and checked, and an ETCHED SEPARATOR in every
    // group gap (TOOLBAR_DrawFlatSeparator).
    Flat,
};

// WHERE THE NOTIFICATION CARDS' STACK STARTS against the icon row's band
// (notification_stack_bound, notifications.cpp). One seat today, asserted
// there like the toolbar style.
enum class GuiCardSeat {
    // On the band's foot, the card wholly below the toolbar (the planner's
    // interim, 2026-10-06: the band is the 30-px case and holds no card).
    UnderBand,
};

struct ChromeSpec {
    // The vocabulary's text: the faces, their metrics and tracking (gui_font.h).
    const GuiFaceSet*  face_set;
    // THE TOOLBAR CASE of the icon row and row 8, in Windows px (render.h's
    // icon-row block, where the case's rule and its rounding stand): the
    // case left of and above the glyph, the glyph's square seat, the case
    // right of it and below it.
    int                toolbar_case_lead_px;
    int                toolbar_glyph_px;
    int                toolbar_case_trail_x_px;
    int                toolbar_case_trail_y_px;
    GuiToolbarStyle    toolbar_style;
    // THE ICON ROW'S STACK under its etched pair (render.h's icon-row block):
    // the ground above and below the case inside the toolbar band, and the
    // ground between the lane's last line and the trim lane.
    int                icon_row_air_px;
    int                icon_row_foot_px;
    // ROW 8'S OWN AIR above and below its case (render.h's bottom-row block).
    int                bottom_row_air_px;
    GuiCardSeat        card_seat;
    // THE ICON SET the vocabulary wears: a bundled folder under
    // assets/icons/, parsed at launch through resvg (icons.h's head).
    const char*        icon_set;
};

// WIN2000 (architect 2026-10-06), off the ReactOS captures: THE TOOLBARS
// (architect 2026-10-06, on mock_TF1 and the captures) Windows' LARGE case,
// 31 x 30 — lead 3, the 24-px seat, trail 4 right and 3 below — in
// Explorer's flat style, the band the case itself (Explorer's button rect is
// 30 rows, rows 182-211 of tmp/reactos-hover.png) under ONE etched pair with
// 4 px of ground before the trim lane, row 8 keeping its own 3 / 3 air
// (render.h's icon-row block); THE ICONS Tango 0.8.90's scalable drawings,
// ReactOS's own model of a Windows 2000 desktop dressed in Tango (architect
// 2026-10-06: assets/icons/tango/, its README the mapping), the disabled
// glyph ReactOS's saturate.
inline constexpr ChromeSpec kChromeSpecWin2000 = {
    .face_set                = &kGuiFaceSetWin2000,
    .toolbar_case_lead_px    = 3,
    .toolbar_glyph_px        = 24,
    .toolbar_case_trail_x_px = 4,
    .toolbar_case_trail_y_px = 3,
    .toolbar_style           = GuiToolbarStyle::Flat,
    .icon_row_air_px         = 0,
    .icon_row_foot_px        = 4,
    .bottom_row_air_px       = 3,
    .card_seat               = GuiCardSeat::UnderBand,
    .icon_set                = "tango",
};

// THE LIVE SPEC: the one instance (architect 2026-10-06).
inline constexpr const ChromeSpec& kLiveChromeSpec = kChromeSpecWin2000;

#endif // WARPTEMPO_GUI_CHROME_SPEC_H
