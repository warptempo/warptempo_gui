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
//     byte-identical as a second vocabulary.
// Later steps add fields (the toolbars, the icon set) and read the vocabulary
// off a `chrome=` device key.

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
};

// WIN95 (architect 2026-10-06): today's look, byte-identical.
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
};

// WIN2000 (architect 2026-10-06), off the ReactOS captures: the open title's
// text one Windows px right and down (ReactOS's menu.c offsets the item's text
// rect by (1, 1) while it is open; measured on tmp/reactos-menu2.png, Edit
// open against tmp/reactos-hover.png, Edit closed, each from its window's
// left edge: the E's first column 44 against 43, the cap's top row 28 against
// 27); the tooltip 19 rows, its frame round a 2-px pad each side (Views on
// tmp/reactos-tooltips.png: frame, 2 rows of face, the 13-row cell, 2 rows,
// frame; 2 columns of face before the V).
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
};

// THE LIVE VOCABULARY: win2000 (architect 2026-10-06). One constexpr for now;
// the ChromeSpec step reads it off the `chrome=` device key.
inline constexpr const ChromeSpec& kLiveChromeSpec = kChromeSpecWin2000;

#endif // WARPTEMPO_GUI_CHROME_SPEC_H
