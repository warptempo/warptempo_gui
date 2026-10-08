#pragma once

#include "app_state.h"
#include "gui_font.h"
#include "gui_input.h"
#include "onscreen_keyboard.h"   // the band's constants: the card's height budget
#include "palette_file.h"
#include "render.h"

#include <cairo.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct GuiNotifications;
struct GuiPlaybackLifecycle;
struct Viewport;

// THE COLOR PICKER (architect 2026-10-07, the picker arc's second segment) —
// the in-app picker of the program's fifteen colors (kGuiPaletteRoles,
// palette_file.h) and, under either chrome, of THE CHROME KNOB's twelve keys
// (architect 2026-10-08, kGuiChromeLines, palette_file.h; chrome_derive.h,
// clearlooks_derive.h): "a very slimmed down version … a full-fledged part
// of the project … branded with the chrome … not an afterthought". This header is
// the cluster's ONE OWNER of everything that is not pixels or a press body:
// the lengths, the layout, the color math, the wheel's raster and the acts
// (GuiColorPicker, below). The state is AppState::ColorPicker (app_state.h,
// where each field is named), the modal rank ModalDialogOwner::ColorPicker
// (app_state.h, the rank's one statement), the PAINTER
// GuiPaintHandler::paint_color_picker (paint_handler.cpp, beside every other
// painter, called from paint_modal_dialog's fork) and the PRESS ROUTER the
// color picker arms of GuiInputHandler (input_pointer.cpp, beside every
// other press router); the painter and the router both read layout() below,
// so paint and hit cannot describe different controls.
//
// THE PRESETS (architect 2026-10-07, the arc's third segment: "now that we
// have the keyboard in the app, we can allow the user to give names to the
// presets", renamable "for organization", Delete behind a confirming
// prompt, the built-ins unchangeable, the chosen preset back at the next
// launch). Every pick writes THE LIVE PALETTE (palette_file.h's
// live_palette_record: render.h's program_palette_words and
// live_chrome_pick) and installs it; the presets are NAMED
// PALETTES (palette_file.h: the compiled-in built-ins and his files) and
// THE ACTIVE PRESET is the device config's `palette` resolved
// (effective_palette_name) — NOT A FIELD HERE: the live config is its one
// truth, read at every paint, so the picker's first open after a launch
// lands on the config's preset (or the chrome's default) with no seeding to
// keep in step. THE PALETTE MENU (the bottom row's first button, below)
// loads a preset, saves the live palette over the active file, saves it
// under a new name, renames the active file or deletes it. A CLOSE KEEPS
// THE LIVE COLORS, ASKING NOTHING: a palette picked and not saved paints
// until the process ends, and THE NEXT LAUNCH RETURNS TO THE NAMED PRESET —
// the menu's Save, lit while the live palette differs from the active file's,
// is the cue (architect 2026-10-07). The picker is the folder's one writer
// and the `palette` key's one writer (GuiColorPicker's preset acts, below).
//
// WHAT IT IS. ONE CARD ON THE WELL, on the half of the window opposite the
// opening tap (so the well under the other half stays in view while
// picking), standing from the well's top edge with kCardMarginPx of air
// from the window's side and the well's top: the GROUND with a PLAIN RAISED
// two-line edge under win2000 (a floating palette's edge, DrawEdge
// EDGE_RAISED, mitred as every relief) and under clearlooks GTK's one
// shade[5] line on four sides (cl_list_frame, the well's own tone); 6 W of
// inner pad; NO caption and no title (slim). Inside it, left to right and
// top to bottom:
//   THE WHEEL (left) — GNOME 2's own, GtkHSV's hue RING with the SV
//     TRIANGLE inside it, in a square whose side is the top block's height,
//     proportioned off his capture (tmp/squeeze/Screenshot_2026-10-07_
//     07-48-13.png, measured 2026-10-07 at 100 %: the ring's outer diameter
//     174 px, its width 15 — gtk_hsv_set_metrics (174, 15), GTK's own
//     literals — and the triangle's corners ON THE RING'S INNER CIRCLE, no
//     inset): ring width = side x 15 / 174, the triangle's circumradius the
//     inner radius. Hue 0 at three o'clock, increasing counterclockwise
//     (GtkHSV's angle; the capture's red at the right, yellow above it); the
//     triangle's corners the pure hue, white at +120 degrees and black at
//     +240, turning with the hue.
//   THE RIGHT COLUMN — (a) THE ELEMENT CHOOSER, the chrome's combo box
//     showing the live element's name (element_display_name) with the arrow
//     button; a press drops its list — THE ELEMENTS (element_at: the chrome
//     knob's twelve at the top, "Chrome" first, then the fifteen in
//     kGuiPaletteRoles' order — 27 rows under either chrome), the menu-row
//     popup's painters and press road
//     — and a tap on a row selects. IT IS ONE OF THE THREE LIST POPUPS,
//     placed by the window and SCROLLING when its rows outgrow the room
//     (render.h's popup scroll block, the rule's one owner; combo_list), its
//     shown row scrolled into view at the open (re-derived 2026-10-08 with
//     the knob's twelve, device px): at the tablet's 300 % the 27 rows under
//     win2000 are 27 x 51 + 2 x 3 + 6 + 6 = 1395, more than the window holds
//     below the chooser, so the list scrolls there, and under clearlooks
//     (re-derived the same day, the knob offered there too) 27 x 57 + 3 =
//     1542, scrolling the same way. At the laptop's 138 % the list hangs
//     whole: 27 x 23 + 2 + 2 + 2 = 627 under win2000 (foot at 45 + 627 =
//     672 of the well's 860) and 27 x 26 + 1 = 703 under clearlooks (foot
//     at 44 + 703 = 747 of 852).
//     (b)–(g) SIX SLIDER ROWS, Hue 0–360, Saturation 0–100,
//     Value 0–100, Red / Green / Blue 0–255: the label at the left (one
//     width for all six, measured from the widest), the slider in the
//     chrome's own scrub painters (win2000 the channel and Windows' pointed
//     thumb, clearlooks GtkScale's trough with its lower part filled and
//     its thumb), the value's digits at the right in a fixed cell of
//     tabular digits.
//   THE BOTTOM ROW, under both — (h) THE HEX FIELD, a sunken field at the
//     dialog field's size (kModalFieldHeightPx and its pad, render.h — the
//     time fields' too since 2026-10-08) in the field pair showing
//     `#RRGGBB`; a tap gives
//     it the focus (a text editor of Kind::PaletteHex); (i) OLD | NEW, two
//     equal swatches in a one-line sunken frame, OLD the element's color
//     when it became the live element, NEW the current, a tap on OLD writing
//     it back, at its floor; then FOUR BUTTONS — (j) THE PALETTE MENU
//     BUTTON, the row's remainder, the chrome's
//     combo (the chooser's own drawing: win2000's sunken field with its
//     drop-down button, clearlooks' gummy button with the engine's wedge)
//     labeled with THE ACTIVE PRESET'S NAME (palette_display_name: a
//     built-in's in Title Case, a file's verbatim), cut with Windows' "..."
//     when it does not fit, the caption title's rule; a press drops THE
//     PALETTE MENU (below); then the push buttons Copy, Paste and Close.
//   THE PALETTE MENU (architect 2026-10-07) — the chooser's list road (the
//     menu-row popup's painters, the press arms a row, the lift acts and
//     closes): first the four ACTS "Save", "Save As", "Rename", "Delete"
//     (Title Case, no ellipsis — kdenlive's convention: nothing here opens
//     a dialog), each with its truthful enabled bit (palette_act_enabled),
//     then, each group behind a separator, HIS FILES (byte order) and THE
//     BUILT-IN SCHEMES (the chromes' two defaults, then the catalog's
//     others; palette_file.h's head) — the acts first so they lead at the
//     head (palette_menu_rows, architect 2026-10-08 ~11:10). Its width is the widest row's between
//     the popup's pads (and the scroll bar's, when it scrolls), held inside
//     the window across. IT IS ONE OF THE THREE LIST POPUPS (render.h's
//     popup scroll block, the rule's one owner, architect 2026-10-08): hung
//     from the button's foot or standing on its head by the window's room,
//     and SCROLLING when its rows outgrow it — the acts and the separators
//     ordinary rows that scroll with the names, none pinned, the scroll at
//     the head at each open; the shown rows and the bar published as painted
//     (layout). Every palette Save As writes is reachable, so the act
//     refuses no name for room. The lit row starts on the active preset's
//     name (the chooser's own seat, the hover seeded at the open) and
//     follows the pointer; a grayed row is never lit and a press on one is
//     a consumed nothing.
//   THE NAME ASK (Save As, Rename) — THE CARD'S ONE TEXT FIELD WIDENED: for
//     the length of the ask the hex field's box and the OLD | NEW frame
//     beside it are ONE FIELD (the same text editor, its Kind PaletteName),
//     the act's own word ("Save As" / "Rename") cap-centered at its left —
//     the planner's leave, taken: a tooltip does not reach glass — empty
//     for Save As, prefilled with the active name for Rename, the whole text
//     selected; the four buttons gray while it stands. Enter commits, Esc
//     cancels, and a press anywhere else abandons it (the hex field's rule).
//   WHY THE BOTTOM ROW SPANS THE CARD and is not the right column's tail
//   (the planner's choice under the brief's leave): the card must stand
//   clear of the on-screen keyboard's band (THE HEIGHT BUDGET below), and a
//   right column of chooser + six sliders + field + swatches + buttons
//   cannot; the capture itself puts the swatch pair under the wheel.
//
// THE HEIGHT BUDGET (the architect's constraint, 2026-10-07): the band rises
// on glass whenever a text editor stands (the card's one field is one), directly
// above row 8, 131 W tall (onscreen_keyboard.h: 2 x 3 of pad + 4 x 29 of key
// + 3 x 3 of gap; asserted below), and the card keeps kCardMarginPx of air
// above it. At the tablet (2304 x 1440 at 300 %, the well 945 device rows
// under clearlooks = 315 W, 954 = 318 W under win2000 — main.cpp's lane
// record) the card may be at most 315 − 4 − 131 − 4 = 176 W tall under
// clearlooks and 179 under win2000; IT STAYS 156 UNDER BOTH CHROMES (the
// design as it landed), the spare 20 W under clearlooks and 23 under
// win2000 left as air above the band: edge 2 + pad 6 + chooser 21 + gap 2 + six rows of 15 + gap 4 +
// button 23 + pad 6 + edge 2 = 156 (win2000); edge 1 + pad 6 + 21 + 2 + 90
// + 4 + button 25 + pad 6 + edge 1 = 156 (clearlooks) — every term a whole
// Windows px, so at 300 % the sum is exact. The laptop (1080 rows at
// 138 %, the well far taller in W) shows the same card with more air.
//
// THE WIDTH: kCardWidthPx = 376, the tablet's half (384 W of its 768) less
// the margin on both sides (384 − 2 x 4); the right column takes what the wheel and the column gap
// leave, the slider's track what the label and the value cell leave, and
// the palette menu button what the hex field, OLD | NEW at its floor and the
// three push buttons leave. THE BOTTOM ROW'S ARITHMETIC (architect
// 2026-10-07): the inner width is 360 W under win2000 (376 less 2 x (edge 2
// + pad 6)) and 362 under clearlooks (376 less 2 x (1 + 6)); the hex field
// is its widest spelling `#DDDDDD` plus its two pads — 53 + 10 = 63 W in
// Tahoma, 60 + 10 = 70 in DejaVu Sans (shaped; the dialog field's 5-W pad
// under both, 2026-10-08); the row's five gaps are 6 W
// each; OLD | NEW its floor, two 20-W swatches inside the frame's two lines
// = 42; Copy, Paste and Close kPushButtonWidthPx = 50 each; THE MENU BUTTON
// THE REMAINDER, 360 − 63 − 30 − 42 − 150 = 75 W under win2000 and
// 362 − 70 − 30 − 42 − 150 = 70 under clearlooks.
// WHY THE MENU BUTTON IS IN THIS ROW AND NOT THE CHOOSER'S (architect
// 2026-10-07: the chooser's row first, beside the element combo at the width
// its longest role name needs, the palette combo the remainder — unless that
// remainder fell under 70 W under either chrome): the right column is 239 W
// under win2000 and 241 under clearlooks (the inner width less the 113-W
// wheel and the 8-W column gap); the element combo for "Selected Phase Reset
// Flag" is 2 + 5 + 127 + 4 + 16 + 2 = 156 W under win2000 (the field's
// edges, its pad, the shaped name, the text-to-button gap, the drop-down
// button) and 6 + 145 + 4 + 7 + 6 = 168 under clearlooks (the gummy pads,
// the name, the gap, the wedge); with the 6-W gap the palette combo's
// remainder is 77 W under win2000 but 67 under clearlooks, so the ruled
// fallback stands: the bottom row, the three push buttons at Windows' 50.
//
// EVERY LENGTH IS A WINDOWS PX THROUGH scaled_px, ROUNDED AT THE ELEMENT (the
// rounding rule): the card is the sum of its rounded parts.
namespace color_picker {

inline constexpr int kCardMarginPx    = 4;    // from the window's side and the well's top
inline constexpr int kCardPadPx       = 6;    // inside the edge
inline constexpr int kCardWidthPx     = 376;  // the tablet's half less two margins
inline constexpr int kColumnGapPx     = 8;    // the wheel to the right column
inline constexpr int kChooserGapPx    = 2;    // the chooser to the first slider row
inline constexpr int kBlockGapPx      = 4;    // the top block to the bottom row
inline constexpr int kControlGapPx    = 6;    // between the bottom row's items
// THE CHOOSER IS 21 W TALL UNDER BOTH CHROMES: Windows' combo box at the
// 8-pt font (its edit's 13 cell + 2 x 2 of edge + 2 x 2 of pad), and GTK's
// combo button re-derived at the 13 cell with the focus terms zeroed, 13 +
// 2 x (xthickness 3 + inner-border 1) = 21 — the tool button's own
// derivation (chrome_spec.h), the proportional fit to Windows' layout.
inline constexpr int kChooserHeightPx = 21;
// THE WIN2000 COMBO'S DROP-DOWN BUTTON: Windows' SM_CXVSCROLL 16, and its
// arrow Marlett's 7 x 4 wedge. Under clearlooks the whole combo is one
// gummy button and the wedge is the engine's own, drawn at the same size.
// Both arms round each side AT THE ELEMENT through scaled_px's floored
// form, floored at a 3 x 2 device-px wedge so a small scale cannot zero the
// triangle (paint_picker_combo, paint_handler.cpp).
inline constexpr int kComboButtonWPx  = 16;
inline constexpr int kComboArrowWPx   = 7;
inline constexpr int kComboArrowHPx   = 4;
inline constexpr int kComboArrowMinWPx = 3;
inline constexpr int kComboArrowMinHPx = 2;
// THE COMBO'S TEXT stops this far short of its wedge (win2000: of the
// drop-down button), where a long name is cut with "..." (2026-10-07, the
// palette menu button's preset names; GtkComboBox's arrow spacing).
inline constexpr int kComboTextGapPx  = 4;
// THE CHROME'S COMBO AND ITS LIST — GEOMETRY SHARED BY TWO READERS
// (2026-10-07 evening): the picker's chooser and palette menu button, and
// the settings editor's CHOICE EDITOR (settings_editor.h's head), whose
// combo is this drawing at the dialog field's seat and whose list is this
// list. Device px, rounded at the element.
//   combo_drop_button — win2000's drop-down button inside the combo `r`
//     (kComboButtonWPx wide, inside the sunken field's two lines), and the
//     zero rect under clearlooks, whose whole combo is one gummy button.
//   combo_list — the list of `count` rows dropped from `combo`, its width,
//     flush (the definition's ruling), placed and scrolled by the popup
//     lists' rule (render.h's popup scroll block, place_popup_list): below
//     the combo's foot where every row fits (the chooser's), else standing
//     on its head (the choice editor's: the row is the window's foot, and
//     Windows' and GTK's combos both open above when no room lies below),
//     else the roomier side scrolled. The dropdown's own arithmetic
//     (dropdown_h_px, render.h): the frame, the item block's two margins, the
//     rows' room — an UPWARD box under clearlooks one line taller, carrying
//     its own top line (2026-10-08, popup_border_top_px; the definition says
//     why). `top` is the popup's scroll; the result's `bar` carries the
//     clamped top and the shown count whether or not it is present.
//   combo_list_item — row `i` of the domain as placed: the dropdown's item
//     rect (popup_item_rect, short of the bar when one stands), or the zero
//     rect for a row scrolled out of view.
struct ComboList {
    GuiRect        box{0, 0, 0, 0};
    bool           upward = false;
    int            count  = 0;
    PopupScrollBar bar;
};
GuiRect   combo_drop_button(const GuiRect& r);
ComboList combo_list(const GuiRect& combo, int count, int window_h, int top);
GuiRect   combo_list_item(const ComboList& l, int i);
// A SLIDER ROW IS 15 W: GtkScale's slider-width (clearlooks_paint.h's
// kClScaleSliderWidthPx) and the win2000 thumb's seat here — 3 W above the
// 4-line channel and 8 below it (the point's 5 and 3 of straight side),
// the player's 8 / 4 / 9 seat shortened to the row (comctl32 sizes the
// straight part to the control, never the point); six rows stand flush, the
// thumbs filling their rows.
inline constexpr int kSliderRowPx         = 15;
inline constexpr int kSliderThumbAbovePx  = 3;
inline constexpr int kSliderLabelGapPx    = 4;   // the label to the track
inline constexpr int kSliderValueGapPx    = 4;   // the track to the value cell
// COPY, PASTE AND CLOSE'S WIDTH (architect 2026-10-07; the head's
// arithmetic): WINDOWS' PUSH-BUTTON MINIMUM, 50 W, under the dialogs' 75
// (kModalBtnMinWidthPx) — the three words fit inside it with their pads
// (Close, the widest, 30 + 12 in DejaVu Sans) — so the palette menu button
// beside them takes what the row leaves, its name cut with "..." where it
// does not fit.
inline constexpr int kPushButtonWidthPx   = 50;
// THE SWATCH PAIR'S WIDTH, its floor: two swatches of this inside their
// frame (the planner's 2 x 20; the head's arithmetic).
inline constexpr int kSwatchMinWPx        = 20;
// THE NAME ASK'S WORD to its field (the head): Windows' label-to-control gap.
inline constexpr int kNameLabelGapPx      = 4;
// GTK'S RING PROPORTION (the head: 15 of 174), and the SV marker's radius —
// GtkHSV's circle, drawn here at 3 W (its 6-W diameter, GTK's at 100 %).
inline constexpr int kRingWidthNum        = 15;
inline constexpr int kRingWidthDen        = 174;
inline constexpr int kSvMarkerRadiusPx    = 3;

// THE BAND'S HEIGHT IN W PX (the head's budget), derived from the keyboard's
// own constants so the arithmetic above cannot drift from them.
inline constexpr int kKeyboardBandWPx =
    static_cast<int>(2 * onscreen_keyboard::kPadPx +
                     onscreen_keyboard::kRowCount *
                         onscreen_keyboard::kKeyHeightPx +
                     (onscreen_keyboard::kRowCount - 1) *
                         onscreen_keyboard::kKeyGapPx);
static_assert(kKeyboardBandWPx == 131);
// THE CARD'S HEIGHT IN W PX under each chrome (the head's sums).
constexpr int card_height_wpx(const ChromeSpec& spec) {
    const int edge = spec.vocabulary == GuiChromeVocabulary::Clearlooks ? 1 : 2;
    return 2 * edge + 2 * kCardPadPx + kChooserHeightPx + kChooserGapPx +
           6 * kSliderRowPx + kBlockGapPx +
           static_cast<int>(spec.push_button_box_px);
}
// THE TABLET'S WELL AT 300 % UNDER CLEARLOOKS, the tighter of the two (the
// head; main.cpp's lane record: 945 device rows), and the budget it leaves,
// 315 − 4 − 131 − 4 = 176: the card, the band and the two margins stand
// inside the well, the card at 156 with 20 W to spare.
inline constexpr int kTabletWellClearlooksWPx = 315;
static_assert(card_height_wpx(kChromeSpecClearlooks) + 2 * kCardMarginPx +
                  kKeyboardBandWPx <= kTabletWellClearlooksWPx);
static_assert(card_height_wpx(kChromeSpecClearlooks) == 156);
static_assert(card_height_wpx(kChromeSpecWin2000) == 156);

// -- THE CHANNELS -----------------------------------------------------------

// THE SIX SLIDERS, in the rows' order. H S V and R G B are two spellings of
// one color: THE RGB BYTES ARE THE TRUTH (the color is bytes), HSV is
// derived from them, and a drag on an HSV slider writes the bytes it
// converts to (GuiColorPicker::set_channel). THE BYTES ARE sRGB (architect
// 2026-10-08 ~05:15, display_transform.h's head): the hex field shows and
// takes the sRGB hex a screenshot or a browser's picker gives, and the
// tablet converts at the painter's entry, so the color on the glass is the
// one the browser shows for the same hex — "nothing on the picker is added
// or changed".
enum class Channel { Hue, Saturation, Value, Red, Green, Blue };
inline constexpr int kChannelCount = 6;
// The picker's stash spells the count as a literal (app_state.h includes
// no picker header); pinned here.
static_assert(std::tuple_size_v<decltype(AppState::ColorPicker::Stash::sliders)> ==
                  kChannelCount,
              "AppState::ColorPicker::Stash::sliders must hold kChannelCount");
constexpr Channel channel_at(int i) { return static_cast<Channel>(i); }
constexpr int channel_index(Channel c) { return static_cast<int>(c); }
// The row's label (Title Case) and the slider's top value: 360 degrees, 100
// percent, 255.
constexpr const char* channel_label(Channel c) {
    switch (c) {
        case Channel::Hue:        return "Hue";
        case Channel::Saturation: return "Saturation";
        case Channel::Value:      return "Value";
        case Channel::Red:        return "Red";
        case Channel::Green:      return "Green";
        case Channel::Blue:       return "Blue";
    }
    return "";
}
constexpr int channel_max(Channel c) {
    switch (c) {
        case Channel::Hue:        return 360;
        case Channel::Saturation:
        case Channel::Value:      return 100;
        case Channel::Red:
        case Channel::Green:
        case Channel::Blue:       return 255;
    }
    return 0;
}

// THE SLIDER'S SHOWN VALUE for a channel, from the state's memories and its
// bytes: H the hue memory in integer degrees, S the saturation memory in
// integer percent, V the bytes' maximum in integer percent, R G B the bytes
// (the memories' rule is at GuiColorPicker::set_color). Read by the painter
// (the thumb's column, the digits) and by the acts.
int channel_value(const AppState::ColorPicker& cp, Channel c);

// -- THE ELEMENTS (the chooser's list) -------------------------------------------

// THE CHOOSER'S LIST (architect 2026-10-08, the chrome knob; its twelve
// ~11:00; under clearlooks too since ~12:10): THE KNOB'S TWELVE AT THE TOP
// in kGuiChromeLines' order — "Chrome", "Chrome Text", "Title", "Title End",
// "Title Text", the three "Inactive" ones, "Selection", "Selection Text",
// "Field", "Field Text" (his 2026-10-04 order: the chrome first, then the
// canvas, the ink …) — then the fifteen program roles in kGuiPaletteRoles'
// order, THE SAME LIST UNDER EITHER CHROME (one theme syntax: under
// clearlooks "Title End" and "Inactive Title End" are listed and ignored,
// clearlooks_derive.h's head — their OLD shows the key's own word). An
// element is a chrome key or a program role; `role` is the key's index in
// kGuiChromeLines or the role's in kGuiPaletteRoles.
inline constexpr std::size_t kElementCount =
    kGuiChromeLineCount + kGuiPaletteRoleCount;
static_assert(std::tuple_size_v<decltype(AppState::ColorPicker::Stash::list_items)> ==
                  kElementCount,
              "AppState::ColorPicker::Stash::list_items must hold kElementCount");
struct Element {
    bool        chrome = false;   // a chrome knob's key, else a program role
    std::size_t role   = 0;
};
std::size_t element_count();
Element     element_at(std::size_t e);
// THE CHOOSER'S NAMES, Title Case (the planner's list, 2026-10-07; the
// knob's twelve, his, 2026-10-08): the role table's names are the file grammar's,
// these the user's.
const char* element_display_name(std::size_t e);
// THE ELEMENT'S LIVE COLOR: a program role's live word, or the live chrome
// pick's key — an inactive key never picked showing the active key it
// follows — and with no block in the live palette the live chrome's
// COMPILED word of the key's role (kGuiChromeLines' compiled_role), so OLD
// shows what the chrome wears.
uint32_t    element_color(std::size_t e);
// THE BLOCK A FIRST PICK CREATES (GuiColorPicker::set_color): the nine block
// keys from the live chrome's compiled words, the inactive three absent.
GuiChromePick compiled_chrome_pick();

// -- THE PRESETS ---------------------------------------------------------------

// A PALETTE'S SHOWN NAME (the menu button's label, the menu's rows): a
// built-in's generated display name ("Windows 2000 Standard", "Clearlooks",
// "CDE Northern Sky" — the key is the file grammar's, this the user's, as
// element_display_name is), a file's name verbatim. Whether `name` is a
// built-in's display name, case and all — the name ask refuses such a name
// as taken, so the menu never lists two rows reading alike.
std::string palette_display_name(std::string_view name);
bool        is_builtin_display_name(std::string_view name);

// THE MENU'S FOUR ACTS, in the rows' order (the head).
enum class PaletteAct { Save, SaveAs, Rename, Delete };
inline constexpr int kPaletteActCount = 4;
constexpr PaletteAct palette_act_at(int i) { return static_cast<PaletteAct>(i); }
constexpr const char* palette_act_label(PaletteAct a) {
    switch (a) {
        case PaletteAct::Save:   return "Save";
        case PaletteAct::SaveAs: return "Save As";
        case PaletteAct::Rename: return "Rename";
        case PaletteAct::Delete: return "Delete";
    }
    return "";
}
// THE ACTIVE PRESET — the live device config's `palette` resolved
// (effective_palette_name): the name as written, or the live chrome's
// default palette for no line (the head: no field holds it).
std::string_view active_palette(const AppState& app);
// THE ACTS' TRUTHFUL ENABLED BITS (every roster button truthful), asked by
// the painter once per paint and published as painted; the lift reads the
// published bit. SAVE gray while the active preset is a built-in (the
// built-ins cannot be changed) OR while the live palette equals the active
// preset's, its chrome knob included (nothing to save — the comparison IS
// the modified state, no flag keeps it); RENAME and DELETE gray on a
// built-in; SAVE AS always live (every refusal it has is the name's, judged
// at commit_name).
bool palette_act_enabled(const AppState& app, PaletteAct a);

// THE MENU'S ROWS, top to bottom (architect 2026-10-08 ~11:10): the four
// acts — a separator — HIS FILES (palette_file_names(), byte order) — a
// separator — THE BUILT-INS (the chromes' own defaults first, in the
// vocabularies' order, then every other scheme of kGuiChromeSchemes in the
// catalog's). A GROUP OPENS ON ITS SEPARATOR AND AN EMPTY GROUP IS NOT
// SHOWN: with no file, ONE separator stands between the acts and the
// built-ins — a Windows menu never shows an empty group between two
// separators. Each separator is a scroll row of its own in the layout
// (layout's palette menu). THE ORDER IS THE ACTS' REACHABILITY
// (2026-10-07): the acts lead, so hung from the button they are the rows
// nearest it, and a menu that scrolls opens at its head with the acts in
// view (the head's THE PALETTE MENU). A row is a NAME (its load) or an ACT;
// `separator_before` marks the row that opens a group.
struct PaletteMenuRow {
    bool        is_act = false;
    std::string name;                        // a name row's palette
    PaletteAct  act    = PaletteAct::Save;   // an act row's act
    bool        separator_before = false;    // the row opens a group
};
std::vector<PaletteMenuRow> palette_menu_rows();

// -- THE COLOR MATH ------------------------------------------------------------

struct Hsv {
    double h = 0.0;   // degrees, [0, 360)
    double s = 0.0;   // [0, 1]
    double v = 0.0;   // [0, 1]
};
// The standard conversion. rgb_to_hsv of a gray answers h = 0 and s = 0;
// the memories' rule (AppState::ColorPicker) decides what the sliders show.
Hsv      rgb_to_hsv(uint32_t rgb);
uint32_t hsv_to_rgb(double h_deg, double s, double v);
// GTK's INTENSITY — 0.30 r + 0.59 g + 0.11 b over [0, 1] — THE ONE LUMINANCE
// READ THE PRODUCT MAKES (architect 2026-10-07): GtkHSV draws its two
// markers black where the color under them is bright and white where it is
// dark, and the wheel's markers follow it. No other site reads a luminance.
double   gtk_intensity(uint32_t rgb);
// `#RRGGBB`, uppercase — the hex field's shown text.
std::string hex_spelling(uint32_t rgb);
// THE HEX FIELD'S COMMIT GRAMMAR: six hex digits, with or without a leading
// '#', either case, nothing else; anything else is "Not a color".
std::optional<uint32_t> parse_hex_color(std::string_view text);

// -- THE LAYOUT -----------------------------------------------------------------

// EVERY CONTROL'S RECT IN DEVICE PX, a pure function of the window, the
// scale, the chrome and the state (which half; whether the list or the menu
// is down; whether the name ask stands):
// the painter paints it and publishes it, the router reads the publication.
struct Layout {
    GuiRect card{0, 0, 0, 0};
    GuiRect inner{0, 0, 0, 0};
    int     edge_px = 0;
    // The wheel's square and its circles (device px, the center at the
    // square's exact middle, x + side / 2 — a pixel center for an odd side,
    // 339 at 300 %, a pixel boundary for an even one, 158 at 138 %).
    GuiRect wheel{0, 0, 0, 0};
    double  cx = 0.0, cy = 0.0, outer_r = 0.0, inner_r = 0.0;
    // The chooser: the whole combo and, under win2000, its drop-down button.
    GuiRect chooser{0, 0, 0, 0};
    GuiRect chooser_button{0, 0, 0, 0};
    // The list, when down: its placement (the box, the side, the bar) and
    // its rows by element index — the zero rect for a row scrolled out of
    // view (combo_list_item).
    ComboList list;
    GuiRect list_items[kElementCount]{};
    // The six slider rows: the row, the label's seat, the track (the thumb's
    // center travels its inset span, scrub_handle_box_px / 2 in at each end)
    // and the value cell.
    GuiRect slider_row[kChannelCount]{};
    GuiRect slider_label[kChannelCount]{};
    GuiRect slider_track[kChannelCount]{};
    GuiRect slider_value[kChannelCount]{};
    // The bottom row. `field` / `field_inner` are THE ONE TEXT FIELD — the
    // hex field, or, while the name ask stands, the wide name field (the
    // hex field's box through the OLD | NEW frame's right edge, less the
    // act's word and its gap, `name_label`); the swatches are zero then.
    GuiRect field{0, 0, 0, 0};
    GuiRect field_inner{0, 0, 0, 0};
    GuiRect name_label{0, 0, 0, 0};
    GuiRect swatch_frame{0, 0, 0, 0};
    GuiRect swatch_old{0, 0, 0, 0};
    GuiRect swatch_new{0, 0, 0, 0};
    // The palette menu button and, under win2000, its drop-down button.
    GuiRect menu_button{0, 0, 0, 0};
    GuiRect menu_button_arrow{0, 0, 0, 0};
    GuiRect buttons[3]{};   // Copy, Paste, Close
    // The palette menu, when down: its box, EVERY ROW (palette_menu_rows)
    // with its rect in the same order — the zero rect for a row scrolled out
    // of view — each separator's top row IN VIEW (a separator scrolled out
    // is not listed) and the bar (render.h's popup scroll block: its scroll
    // rows are the rows with a separator before each group). The painter
    // paints and publishes exactly these.
    GuiRect menu{0, 0, 0, 0};
    std::vector<PaletteMenuRow> menu_rows;
    std::vector<GuiRect> menu_items;
    std::vector<int> menu_sep_ys;
    PopupScrollBar menu_bar;
    // Whether the menu stands on the button's head (the upper placement),
    // the box then carrying its own top line under clearlooks (2026-10-08,
    // combo_list's rule; the painter reads it).
    bool    menu_upward = false;
};
Layout layout(const AppState& app, const GuiFont& font);

// THE SLIDER'S MAPPING (the scrub's shape, render_player_scrub_x_of): the
// thumb's center travels [track.x + half, track.x + track.w − 1 − half],
// half the grab box's (scrub_handle_box_px / 2); `value` in [0, max] maps
// linearly onto it, and a column maps back to the nearest value.
int slider_thumb_x(const GuiRect& track, int value, int max);
int slider_value_at(const GuiRect& track, int x, int max);

// THE WHEEL'S READS: the hue under a point (degrees) and the (s, v) under a
// point, GtkHSV's barycentric read clamped onto the triangle.
double wheel_hue_at(const Layout& l, int x, int y);
// Whether the point lies in the ring's annulus / inside the triangle.
bool   wheel_in_ring(const Layout& l, int x, int y);
bool   wheel_in_triangle(const Layout& l, double hue_deg, int x, int y);
void   wheel_sv_at(const Layout& l, double hue_deg, int x, int y, double& s,
                   double& v);

// THE WHEEL'S DRAWING — THE ONE PLACE THE PRODUCT PAINTS COLORS THAT ARE
// NOT ROLES, by necessity (architect 2026-10-07; render.h's palette block
// names this exception): the wheel IS the color space, as the icons are
// their own inks. The ring is the hue sweep and the triangle the SV field
// for the live hue, CONTINUOUS COLOR FIELDS rendered per pixel into cairo
// image surfaces at the device size — the ring once per size, the triangle
// once per (size, hue) — antialiased at their edges, the ring's two circles
// and the triangle's three sides, as coverage over the ground (the
// renderer's edge, as a glyph's). Then THE MARKERS: on the ring a radial
// line across its width at the live hue, on the triangle a small ring at
// (s, v), each black or white by gtk_intensity of the color under it —
// GTK's own rule. THE FIELDS ARE sRGB: on the tablet's P3 window each
// pixel's straight color is converted before its coverage premultiply
// (color_picker.cpp's premultiplied; display_transform.h's head), so the
// wheel shows each hue as the browser shows the same hex.
void paint_wheel(cairo_t* cr, const Layout& l, double hue_deg, double s,
                 double v, uint32_t rgb);

} // namespace color_picker

// THE ACTS (the state is AppState::ColorPicker; every act below is the one
// writer of what it names). Owned inline by GuiInputHandler, built from its
// constructor's own parameters as the value drag is.
struct GuiColorPicker {
    AppState&             app;
    Viewport&             viewport;
    GuiNotifications&     notifications;
    GuiPlaybackLifecycle& playback_lifecycle;

    GuiColorPicker(AppState& app_, Viewport& viewport_,
                   GuiNotifications& notifications_,
                   GuiPlaybackLifecycle& playback_lifecycle_)
        : app(app_), viewport(viewport_), notifications(notifications_),
          playback_lifecycle(playback_lifecycle_) {}

    // THE OPENER — the Settings menu's "Pick Colors" row, its one road
    // (kSettingsPopupItems). `tap_x` is the window x of the lift that chose
    // the row, or -1 for no tap (the right half then). IT REFUSES AS
    // open_project_picker DOES, in its order: silent under a prompt (the
    // prompt veils everything and is its own answer); "Close the editor
    // first" under a keyboard-modal editor (the pointer-transparent flag
    // editor can stand under the menu row); silent under the render player
    // and the project picker (each opener refuses under the other — the
    // Settings anchor is dead under both, so this arm is the opener's own
    // shape rather than a road); silent while it already stands; silent
    // while loading. It is ADMITTED IN THE `h` VIEW (it changes colors, not
    // state — the view's own navigation is untouched and a Close leaves it
    // as it stood), though the Settings anchor is dead in the view and no
    // chord exists, so no road reaches it there today. Past every refusal
    // it stops playback for the modal open as every modal does, mints the
    // session, seats OLD and NEW off the live palette and damages the window.
    void open(int tap_x);
    // THE ONE CLOSE BODY — Close, Esc, the close road's head
    // (GuiPrompt::request_close). Abandons a standing edit (the hex field or
    // the name ask), drops the gesture, the list and the menu, keeps the
    // live colors (the head: no question), keeps `role` and the slot,
    // damages the window. Idempotent.
    void close();

    // THE LIVE ELEMENT: the chooser's pick. OLD becomes the element's
    // current color; the memories re-seat from its bytes.
    void set_element(std::size_t element);
    // THE ONE WRITER OF NEW (AppState::ColorPicker::rgb): writes the live
    // palette's word for the live element — a program role's, or a chrome
    // key's, where THE FIRST PICK OF ANY CHROME ELEMENT CREATES THE BLOCK
    // (architect 2026-10-08 ~11:00): its nine from the live chrome's
    // compiled words (color_picker::compiled_chrome_pick), then the picked
    // key written — an inactive key becoming picked, its two siblings still
    // following — installs it through the apply shape
    // (install_live_words), and keeps the HSV
    // memories: `from_hsv` true means an HSV control wrote the bytes and
    // the memories already hold what it meant (so a quantized round trip
    // cannot move the slider under the hand); false means an RGB source
    // wrote them (a slider, the hex field, Paste, OLD, the chooser's
    // change), and the memories re-seat from the bytes — the hue only when
    // the color has one (max != min) and the saturation only when it is
    // not black (max > 0), GtkHSV's own behavior.
    void set_color(uint32_t rgb, bool from_hsv);
    // A SLIDER'S WRITE: `value` in [0, channel_max]; an HSV channel converts
    // through the memories, an RGB channel replaces its byte.
    void set_channel(color_picker::Channel c, int value);
    // THE WHEEL'S WRITES: the hue (degrees) with s and v kept; (s, v) with
    // the hue kept.
    void set_hue(double h_deg);
    void set_sv(double s, double v);
    // OLD's tap, Copy and Paste (the slot's rule is at the state).
    void revert_to_old();
    void copy_to_slot();
    void paste_from_slot();

    // THE CARD'S ONE TEXT FIELD (AppState::ColorPicker::field_editor), in
    // one of two Kinds: PaletteHex, THE HEX FIELD, or PaletteName, THE NAME
    // ASK (the head). field_focus: with no edit standing, enter the hex
    // editor over `#RRGGBB` with the whole text selected, so the first
    // keystroke replaces it (Windows' tab into a field); with either edit
    // standing, seat the caret at `tap_x` on the published run
    // (text_editor::byte_index_from_shaped_x). field_commit: Enter's, forked
    // on the Kind — the hex field's parse_hex_color applies, else the
    // refusal the product's way (text_editor::refuse selects the whole text,
    // the card says "Not a color") with the editor standing; the name ask's
    // body is commit_name below. field_cancel: Esc's — the editor leaves, the
    // hex field showing NEW again, the name ask's field the hex field and
    // the swatches again. A press anywhere else on the card while the editor
    // stands is the cancel (the flag editor's own rule for a press outside
    // its box).
    void field_focus(int tap_x);
    // field_select_word: THE DOUBLE TAP on a standing edit (2026-10-08) —
    // the run of the tapped character's class under `tap_x`, every editor's
    // own select_word_at (text_editor.h): `#RRGGBB`'s six digits, the `#`
    // being punctuation, or the name ask's word. The pointer's road to it
    // is color_picker_press's field arm.
    void field_select_word(int tap_x);
    void field_commit();
    void field_cancel();
    bool field_active() const {
        return text_editor::is_active(app.color_picker.field_editor);
    }
    bool name_ask_active() const {
        return field_active() &&
               app.color_picker.field_editor.kind ==
                   text_editor::Kind::PaletteName;
    }

    // -- THE PRESETS' ACTS (the head; the menu's rows reach them at the
    //    lift, color_picker_release) -------------------------------------
    //
    // LOAD — a name row's tap: the named palette installed live, its chrome
    // block or its absence with it (palette_record → the apply shape) — A
    // BUILT-IN CHROME-ONLY, its twelve keys installed and the live fifteen
    // standing (palette_file.h's head), the live chrome's own scheme
    // bringing the compiled chrome back — THE ACTIVE PRESET made `name` (the
    // `palette` key written, write_palette_key), OLD and NEW reseated off
    // the live element's new color. A load of the active preset itself is
    // the same act: the file's words back, the unsaved picks dropped.
    void load_palette(std::string_view name);
    // SAVE — write_palette_file(active, the live palette, its chrome block
    // when it carries one); a failure cards.
    // The act is gray where it would be a no-op or a refusal
    // (palette_act_enabled), so the press that reaches it always writes.
    void save_palette();
    // SAVE AS and RENAME — open THE NAME ASK (the head): the field editor
    // in Kind PaletteName, empty (Save As) or over the active name
    // (Rename), the whole text selected; the menu is already down.
    void begin_name_ask(AppState::ColorPicker::NameAsk ask);
    // THE NAME ASK'S ENTER. The pending name is judged here, THE ONE JUDGE
    // (palette_file.h's writers assert what it settles): outside the name
    // grammar (is_palette_name_spelling) "Not a name"; a
    // built-in's key, a built-in's display name or a loaded file's — another
    // than the one being renamed — "Name taken" (Save does the overwrite;
    // Save As never does); each a refusal the product's way, the editor
    // standing. Then SAVE AS writes the live palette under the name and makes
    // it the active preset (the key written, as a load writes it); RENAME
    // renames the active file and rewrites the key — the same name a no-op
    // that closes the ask. An I/O failure closes the ask and cards (class 5:
    // the live colors and the active preset stand); success closes it.
    void commit_name();
    // DELETE — raise the confirming PROMPT over the standing picker,
    // "Delete '<name>'?" with Delete / Cancel, CANCEL FOCUSED and `d`
    // Delete's letter: the render player's Delete question exactly
    // (render_player_delete — a deletion nothing takes back), the name
    // parked at AppState::ColorPicker::pending_delete. The prompt outranks
    // the picker (ModalDialogOwner), and its two answers are the two below,
    // reached through GuiInputHandler.
    void raise_delete();
    // The prompt's Delete: remove_palette_file(the parked name); the live
    // palette falls to THE LIVE CHROME'S DEFAULT — its default fifteen and
    // its compiled chrome, the next launch's picture — installed live, OLD and
    // NEW reseated, the `palette` key cleared and persisted — the menu
    // button then shows the default's name. A failure cards and changes
    // nothing else. The Cancel drops the parked name and changes nothing.
    void confirm_delete();
    void cancel_delete();

    // THE DAMAGE OF THE CARD'S OWN CHANGES — the card's rect as painted, the
    // list's and the menu's with it (a color change damages the window
    // through set_color).
    void damage_card();

    // THE `palette` KEY'S ONE WRITER (the presets' acts): `name` becomes THE
    // ACTIVE PRESET — the key's value the name, or EMPTY (NO LINE) when it
    // is the live chrome's own default palette (device_config.h: absent =
    // the chrome's own); a same-value write never
    // reaches the writer (the config's no-op rule); otherwise the live
    // struct takes it and write_device_config persists it, a failure the
    // diagnostic on stderr and the display on a card, the live value
    // standing for the session (class 5).
    void write_palette_key(std::string_view name);
    // THE PRESETS' APPLY: `record` installed as the live palette through
    // the apply shape set_color runs (install_live_words), and OLD and NEW
    // reseated off the live element's new color with the memories.
    void apply_palette_record(const GuiPaletteRecord& record);
    // THE APPLY SHAPE'S ONE ROAD (2026-10-07; the knob 2026-10-08): the
    // record's fifteen installed (install_program_palette, palette_file.h)
    // and its chrome knob (install_chrome_pick, render.h), then the apply
    // shape run once, as install_program_palette's declaration states it.
    // Its two callers are set_color and apply_palette_record.
    void install_live_words(const GuiPaletteRecord& record);
};
