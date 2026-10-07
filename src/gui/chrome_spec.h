// THE ONE INCLUDE OUTSIDE THE GUARD: a ChromeSpec names a face set, so the
// sets (gui_font.h) come first; gui_font.h in turn includes this header after
// its sets and before its live block, which reads kLiveChromeSpec. With the
// guard below the include, either header may be included first.
#include "gui_font.h"

#ifndef WARPTEMPO_GUI_CHROME_SPEC_H
#define WARPTEMPO_GUI_CHROME_SPEC_H

// THE CHROME VOCABULARIES (architect 2026-10-06): a ChromeSpec is one chrome
// vocabulary's choices — how the period desktop it follows drew the elements
// the vocabularies disagree on — read by the chokepoint that paints each
// element, never a literal at the painter. The colours stay the theme's
// (theme_file.h); a vocabulary chooses shapes, seats and faces.
//   - WIN2000, LIVE: Windows 2000's "Windows Standard" chrome as ReactOS
//     0.4.16 draws it (its classic chrome is that scheme in every colour and
//     metric; Windows XP's Classic and Server 2003's are the same), read off
//     the architect's ReactOS captures (tmp/reactos*.png, "get it as accurate
//     to ReactOS as we can"). WHERE REACTOS DEPARTS FROM WINDOWS 2000 THE
//     PRODUCT FOLLOWS REACTOS, on purpose and under the Windows 2000 name: the
//     caption's gradient spans the whole caption, under the icon and the
//     buttons (Windows 2000 holds the icon cell at the start colour and the
//     buttons' stretch at the end); Minimise 7 x 2 and Restore's windows 7
//     wide, from ReactOS's own Marlett (Windows 2000's are 6); Wine Tahoma
//     for Microsoft's Tahoma (docs/engineering/windows95_deviations.md lists
//     them per vocabulary).
//   - WIN95: Windows 95's chrome, the look of 2026-10-06's morning, kept
//     byte-identical as a second vocabulary — all but its icons, the Tango
//     set since that evening (kChromeSpecWin95).
// A later step reads the vocabulary off a `chrome=` device key.

// THE CAPTION'S GRADIENT (paint_caption_gradient, render.cpp, where each
// road's rule and reason stand).
enum class GuiCaptionGradient {
    // 15-bit high colour under Windows' 4 x 4 ordered dither, one device px a
    // cell (Windows 2000 at 16-bit, the win95 vocabulary's reference).
    Dithered15Bit,
    // 24-bit, each channel rounded on its own linear ramp, no dither and no
    // quantisation (ReactOS's caption, measured on the capture).
    Smooth24Bit,
};

// THE CAPTION BUTTONS' GLYPHS (paint_handler.cpp's caption glyph block, which
// holds both sets' cells).
enum class GuiCaptionGlyphs {
    // Windows' Marlett at 10 ppem: Minimise 6 x 2, Restore's windows 6 wide,
    // Close two 45-degree bars 2 Windows px thick.
    Win95,
    // ReactOS's Marlett at 12 ppem: Minimise 7 x 2, Restore's windows 7 wide;
    // Close the smooth outline of Marlett's 8 x 7 staircase (the architect's
    // pick of the mocks, 2026-10-06).
    Win2000,
};

// WHERE THE MENU LANE'S ONE ROW OF GROUND STANDS against its 19-row content
// (render.h's menu-row block): the lane is 20 Windows px in both.
enum class GuiMenuFaceRow {
    // Under the content, a foot (Windows 95's measured 20-px band).
    Foot,
    // Above it, directly under the caption (ReactOS: caption 18, face 1,
    // menu 19).
    Head,
};

// THE OPEN MENU TITLE (paint_menu_row).
enum class GuiMenuOpenTitle {
    // Windows 95's: the title filled with the selected fill under the
    // selected text.
    SelectedFill,
    // Windows 2000's non-flat menu: the face with a one-line sunken box
    // (BDR_SUNKENOUTER) round the item, the label in the label role, pushed
    // in (menu_open_text_shift_px).
    SunkenBox,
};

// THE CARD FRAME, the tooltip's and every notification card's
// (paint_popup_chrome's Info face; render.h's palette block, THE CARD FACE).
enum class GuiCardFrame {
    // One line on the bottom and the right, the top and left the face
    // (Windows 95's one tooltip capture).
    BottomRight,
    // One line on all four sides (WS_BORDER: ReactOS's tooltip, Windows
    // 2000's).
    AllSides,
};

// THE ROSTER'S TWO TOOLBARS' FACES — the icon row's and row 8's cases
// (paint_toolbar_box, paint_handler.cpp, where both styles' faces stand).
enum class GuiToolbarStyle {
    // Windows 95's toolbar: every case SOFT RAISED at rest (EDGE_RAISED |
    // BF_SOFT), SOFT SUNKEN pressed or checked, no hot face, and bare ground
    // in a group gap (comctl32's classic style leaves the gap blank).
    Raised,
    // Explorer's FLAT toolbar (comctl32's TBSTYLE_FLAT as ReactOS 0.4.16's
    // toolbar.c draws it): no edge at rest, one raised line on the HOT case,
    // one sunken line pressed and checked, and an ETCHED SEPARATOR in every
    // group gap (TOOLBAR_DrawFlatSeparator).
    Flat,
};

// WHERE THE NOTIFICATION CARDS' STACK STARTS against the icon row's band
// (notification_stack_bound, notifications.cpp).
enum class GuiCardSeat {
    // On the top etched pair's foot: a one-line card fills the toolbar band
    // between the two etched pairs exactly (win95's band is the card's 28
    // Windows px).
    InBand,
    // On the band's foot, the card wholly below the toolbar (the planner's
    // interim, 2026-10-06: the win2000 band is the 30-px case and holds no
    // card).
    UnderBand,
};

struct ChromeSpec {
    // The vocabulary's text: the faces, their metrics and tracking.
    const GuiFaceSet*  face_set;
    GuiCaptionGradient caption_gradient;
    GuiCaptionGlyphs   caption_glyphs;
    GuiMenuFaceRow     menu_face_row;
    GuiMenuOpenTitle   menu_open_title;
    // The open title's label shift, right and down, in Windows px.
    int                menu_open_text_shift_px;
    GuiCardFrame       card_frame;
    // The tooltip's air between its frame and its text, in Windows px: under
    // AllSides the frame's line stands outside it, under BottomRight the
    // frame lies inside the vertical pad (the unlined top and left read as
    // the pad's own face).
    int                tooltip_pad_x_px;
    int                tooltip_pad_y_px;
    // The on-screen keyboard's Backspace cap (onscreen_keyboard.h cap_word,
    // where both faces' measurements stand).
    const char*        backspace_cap;
    // THE TOOLBAR CASE of the icon row and row 8, in Windows px (render.h's
    // icon-row block, where the case's rule and its rounding stand): the
    // case left of and above the glyph, the glyph's square seat, the case
    // right of it and below it.
    int                toolbar_case_lead_px;
    int                toolbar_glyph_px;
    int                toolbar_case_trail_x_px;
    int                toolbar_case_trail_y_px;
    GuiToolbarStyle    toolbar_style;
    // THE ICON ROW'S STACK under the top etched pair (render.h's icon-row
    // block): the ground above and below the case inside the toolbar band,
    // whether a second etched pair closes the band, and the ground between
    // the lane's last line and the trim lane.
    int                icon_row_air_px;
    bool               icon_row_second_etched_pair;
    int                icon_row_foot_px;
    // ROW 8'S OWN AIR above and below its case (render.h's bottom-row block):
    // a field of its own since the icon row's band lost its air.
    int                bottom_row_air_px;
    GuiCardSeat        card_seat;
    // THE ICON SET the vocabulary wears: a bundled folder under
    // assets/icons/, read at launch through the subset reader (icons.h's
    // head). Tango in both since the product's own 16-unit set retired
    // (architect 2026-10-06); a field still, so a vocabulary names its set.
    const char*        icon_set;
};

// WIN95 (architect 2026-10-06): the morning's look, byte-identical — the
// toolbars Windows 95's small case (23 x 22, the 16-px bitmap at (3, 3)) in
// soft raised relief, WordPad's 3 / 3 air inside two etched pairs and a 3-px
// foot (render.h's icon-row block holds the record). ITS ICONS ARE NOT THE
// MORNING'S (architect 2026-10-06 evening, the product's own 16-unit set
// retired: "I don't anticipate using those because they are 16-pixel
// icons"): the Tango set at the small case's 16-px seat, its disabled glyph
// ReactOS's saturate — Windows 95's emboss read the retired drawings' solid
// inks, which Tango's gradients do not have
// (docs/engineering/windows95_deviations.md).
inline constexpr ChromeSpec kChromeSpecWin95 = {
    .face_set                = &kGuiFaceSetWin95,
    .caption_gradient        = GuiCaptionGradient::Dithered15Bit,
    .caption_glyphs          = GuiCaptionGlyphs::Win95,
    .menu_face_row           = GuiMenuFaceRow::Foot,
    .menu_open_title         = GuiMenuOpenTitle::SelectedFill,
    .menu_open_text_shift_px = 0,
    .card_frame              = GuiCardFrame::BottomRight,
    .tooltip_pad_x_px        = 4,
    .tooltip_pad_y_px        = 2,
    .backspace_cap           = "Backsp",
    .toolbar_case_lead_px    = 3,
    .toolbar_glyph_px        = 16,
    .toolbar_case_trail_x_px = 4,
    .toolbar_case_trail_y_px = 3,
    .toolbar_style           = GuiToolbarStyle::Raised,
    .icon_row_air_px         = 3,
    .icon_row_second_etched_pair = true,
    .icon_row_foot_px        = 3,
    .bottom_row_air_px       = 3,
    .card_seat               = GuiCardSeat::InBand,
    .icon_set                = "tango",
};

// WIN2000 (architect 2026-10-06), off the ReactOS captures: the open title's
// text one Windows px right and down (ReactOS's menu.c offsets the item's text
// rect by (1, 1) while it is open; measured on tmp/reactos-menu2.png, Edit
// open against tmp/reactos-hover.png, Edit closed, each from its window's
// left edge: the E's first column 44 against 43, the cap's top row 28 against
// 27); the tooltip 19 rows, its frame round a 2-px pad each side (Views on
// tmp/reactos-tooltips.png: frame, 2 rows of face, the 13-row cell, 2 rows,
// frame; 2 columns of face before the V); THE TOOLBARS (architect 2026-10-06,
// on mock_TF1 and the captures) Windows' LARGE case, 31 x 30 — lead 3, the
// 24-px seat, trail 4 right and 3 below — in Explorer's flat style, the band
// the case itself (Explorer's button rect is 30 rows, rows 182-211 of
// tmp/reactos-hover.png) under ONE etched pair with 4 px of ground before the
// trim lane, row 8 keeping its own 3 / 3 air (render.h's icon-row block);
// THE ICONS Tango 0.8.90's scalable drawings, ReactOS's own model of a
// Windows 2000 desktop dressed in Tango (architect 2026-10-06:
// assets/icons/tango/, its README the mapping), the disabled glyph
// ReactOS's saturate.
inline constexpr ChromeSpec kChromeSpecWin2000 = {
    .face_set                = &kGuiFaceSetWin2000,
    .caption_gradient        = GuiCaptionGradient::Smooth24Bit,
    .caption_glyphs          = GuiCaptionGlyphs::Win2000,
    .menu_face_row           = GuiMenuFaceRow::Head,
    .menu_open_title         = GuiMenuOpenTitle::SunkenBox,
    .menu_open_text_shift_px = 1,
    .card_frame              = GuiCardFrame::AllSides,
    .tooltip_pad_x_px        = 2,
    .tooltip_pad_y_px        = 2,
    .backspace_cap           = "Backspace",
    .toolbar_case_lead_px    = 3,
    .toolbar_glyph_px        = 24,
    .toolbar_case_trail_x_px = 4,
    .toolbar_case_trail_y_px = 3,
    .toolbar_style           = GuiToolbarStyle::Flat,
    .icon_row_air_px         = 0,
    .icon_row_second_etched_pair = false,
    .icon_row_foot_px        = 4,
    .bottom_row_air_px       = 3,
    .card_seat               = GuiCardSeat::UnderBand,
    .icon_set                = "tango",
};

// THE LIVE VOCABULARY: win2000 (architect 2026-10-06). One constexpr for now;
// the ChromeSpec step reads it off the `chrome=` device key.
inline constexpr const ChromeSpec& kLiveChromeSpec = kChromeSpecWin2000;

#endif // WARPTEMPO_GUI_CHROME_SPEC_H
