#pragma once

#include "app_state.h"
#include "gui_font.h"
#include "gui_input.h"
#include "onscreen_keyboard.h"   // the band's constants: the card's height budget
#include "palette_file.h"
#include "render.h"

#include <algorithm>
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
// palette_file.h: A PALETTE) and, under every chrome, of the chrome's twelve
// keys (architect 2026-10-08, kGuiChromeLines, palette_file.h: A SCHEME;
// chrome_derive.h, clearlooks_derive.h, cde_derive.h): "a very slimmed down version … a full-fledged part
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
// THE SCOPE (architect 2026-10-08 ~18:15: "saving a waveform theme requires
// saving the entire chrome theme as well … one more drop-down that shows
// what you're picking, whether it's the waveform or the chrome"): ONE
// DROP-DOWN OF TWO ROWS, "Chrome" and "Waveform" (his words), choosing which
// of the two preset kinds the picker works on (palette_file.h's head: a
// SCHEME, the chrome's twelve keys, and a PALETTE, the program's fifteen).
// The scope sets THE ELEMENT CHOOSER'S ROWS — the twelve under Chrome, the
// fifteen under Waveform, never a mixed list — and THE PRESET BUTTON: its
// label (the scope's active preset) and its menu's rows (its kind's files
// and built-ins), the four acts acting on that kind alone. OLD | NEW, Copy /
// Paste, the hex field and the wheel are the scope's live element's, with
// no scope of their own. THE SCOPE AT EVERY OPEN IS WAVEFORM — the common
// case: he authors the waveform's colors — and each scope keeps its own
// live element across a switch and a close (AppState::ColorPicker's
// `element` and `parked_element`). Esc and Close are unchanged.
//
// THE PRESETS (architect 2026-10-07, the arc's third segment: "now that we
// have the keyboard in the app, we can allow the user to give names to the
// presets", renamable "for organization", Delete behind a confirming
// prompt, the built-ins unchangeable, the chosen preset back at the next
// launch; two kinds since 2026-10-08 ~18:15). Every pick writes THE LIVE
// WORDS — the live palette (render.h's program_palette_words) or the live
// scheme (render.h's live_chrome_pick) — and installs them; the presets are
// NAMED PALETTES AND SCHEMES (palette_file.h: the compiled-in built-ins and
// his files) and EACH SCOPE'S ACTIVE PRESET is the device config's
// `palette` or `scheme` resolved (effective_palette_name,
// effective_scheme_name) — NOT A FIELD HERE: the live config is its one
// truth, read at every paint, so the picker's first open after a launch
// lands on the config's presets (or the chrome's own) with no seeding to
// keep in step. THE PRESET MENU (the bottom row's preset button, below)
// loads a preset of the scope's kind, saves the live words of that kind over
// the active file, saves them under a new name, renames the active file or
// deletes it. A CLOSE KEEPS BOTH KINDS' LIVE COLORS, ASKING NOTHING: a
// palette or a scheme picked and not saved paints until the process ends,
// and THE NEXT LAUNCH RETURNS TO THE TWO NAMED PRESETS — the menu's Save,
// lit while the scope's live words differ from its active file's, is the
// cue (architect 2026-10-07). The picker is the two folders' one writer and
// the `palette` and `scheme` keys' one writer (GuiColorPicker's preset acts,
// below).
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
//     TRIANGLE inside it, in a square whose side is the top block's height
//     or less (centered on it: THE WIDTH below, 2026-10-08 ~18:15),
//     proportioned off his capture (tmp/squeeze/Screenshot_2026-10-07_
//     07-48-13.png, measured 2026-10-07 at 100 %: the ring's outer diameter
//     174 px, its width 15 — gtk_hsv_set_metrics (174, 15), GTK's own
//     literals — and the triangle's corners ON THE RING'S INNER CIRCLE, no
//     inset): ring width = side x 15 / 174, the triangle's circumradius the
//     inner radius. Hue 0 at three o'clock, increasing counterclockwise
//     (GtkHSV's angle; the capture's red at the right, yellow above it); the
//     triangle's corners the pure hue, white at +120 degrees and black at
//     +240, turning with the hue.
//   THE RIGHT COLUMN — (a) THE CHOOSER ROW, two of the chrome's combo
//     boxes side by side (2026-10-08 ~18:15; THE WIDTH below has the
//     arithmetic): THE SCOPE COMBO at the left, showing "Chrome" or
//     "Waveform" (scope_display_name), then THE ELEMENT CHOOSER showing the
//     live element's name (element_display_name), each with its arrow
//     button; a press on either drops ITS list — the scope's two rows, or
//     THE SCOPE'S ELEMENTS (scope_element_at: under Chrome the twelve in
//     kGuiChromeLines' order, "Chrome" first; under Waveform the fifteen in
//     kGuiPaletteRoles' order) — the menu-row popup's painters and press
//     road, and a tap on a row selects. THE TWO SHARE ONE LIST: one is down
//     at a time (AppState::ColorPicker's `chooser_scope` says whose), so the
//     chooser row is ONE OF THE THREE LIST POPUPS whichever combo dropped
//     it, placed by the window and SCROLLING when its rows outgrow the room
//     (render.h's popup scroll block, the rule's one owner; combo_list), its
//     shown row scrolled into view at the open (re-derived 2026-10-08 with
//     the scope, device px): at the tablet's 300 % the fifteen rows under
//     win2000 are 15 x 51 + 2 x 3 + 6 + 6 = 783 below a chooser whose foot
//     stands at about 474 of the 1440 rows, and under clearlooks
//     15 x 57 + 3 = 858 below about 480, so the list hangs whole on the
//     tablet as on the laptop (15 x 23 + 6 = 351 at 138 %); the scope's two
//     rows hang whole anywhere.
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
//     it back, at its floor; then FOUR BUTTONS — (j) THE PRESET BUTTON,
//     the row's remainder, the chrome's
//     combo (the chooser's own drawing: win2000's sunken field with its
//     drop-down button, clearlooks' gummy button with the engine's wedge)
//     labeled with THE SCOPE'S ACTIVE PRESET'S NAME (preset_display_name: a
//     built-in's in Title Case, a file's verbatim), cut with Windows' "..."
//     when it does not fit, the caption title's rule; a press drops THE
//     PRESET MENU (below); then the push buttons Copy, Paste and Close.
//   THE PRESET MENU (architect 2026-10-07; per scope 2026-10-08) — the
//     chooser's list road (the menu-row popup's painters, the press arms a
//     row, the lift acts and closes): first the four ACTS "Save", "Save
//     As", "Rename", "Delete" (Title Case, no ellipsis — kdenlive's
//     convention: nothing here opens a dialog), each with its truthful
//     enabled bit (preset_act_enabled), then, each group behind a
//     separator, HIS FILES OF THE SCOPE'S KIND (byte order) and ITS
//     BUILT-INS (preset_menu_rows owns the order) — the acts first so they
//     lead at the head (architect 2026-10-08 ~11:10). Its width is the widest row's between
//     the popup's pads (and the scroll bar's, when it scrolls), held inside
//     the window across. IT IS ONE OF THE THREE LIST POPUPS (render.h's
//     popup scroll block, the rule's one owner, architect 2026-10-08): hung
//     from the button's foot or standing on its head by the window's room,
//     and SCROLLING when its rows outgrow it — the acts and the separators
//     ordinary rows that scroll with the names, none pinned, the scroll at
//     the head at each open; the shown rows and the bar published as painted
//     (layout). Every preset Save As writes is reachable, so the act
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
// the margin on both sides (384 − 2 x 4). THE CHOOSER ROW SETS THE RIGHT
// COLUMN (2026-10-08 ~18:15, the scope beside the element chooser — the
// planner's seat, where nothing else moves): each of its two combos is as
// wide as its widest row needs, FACE OR LIST, whichever is wider (the face:
// the field's edges and pad, the shaped name, the 4-W text gap, the
// drop-down button — under clearlooks the gummy pads and the wedge; the
// list, flush with the combo by the list's own ruling: the popup's 22-W
// left pad, the name, the same 4-W gap and the frame's lines), the element
// chooser measured over BOTH scopes' names so the split never moves with
// the scope, the two 6 W apart (kControlGapPx, the bottom row's gap); the
// right column is the wider of that row and what the 113-W top block and
// the 8-W column gap leave, and THE WHEEL TAKES WHAT THE COLUMN LEAVES, at
// most the top block's height, centered on it — so no name is cut and the
// card keeps its height (THE HEIGHT BUDGET above). The names' widths at
// the base's cap (2026-10-08, the faces' advances summed and rounded up;
// the layout measures the shaped runs live, so a kerned pair may take a W
// off): "Waveform" 50 W in Tahoma, 57 in DejaVu Sans, 51 in Go; "Selected
// Phase Reset Flag" 127, 145 and 136 —
//   win2000: the scope max(2 + 5 + 50 + 4 + 16 + 2 = 79, 22 + 50 + 4 + 2 =
//     78) = 79, the chooser max(2 + 5 + 127 + 4 + 16 + 2 = 156, 22 + 127 +
//     4 + 2 = 155) = 156; the row 79 + 6 + 156 = 241 over the 239 the
//     wheel left, so the wheel is 360 − 8 − 241 = 111 W;
//   clearlooks: the scope max(6 + 57 + 4 + 7 + 6 = 80, 22 + 57 + 4 + 1 =
//     84) = 84, the chooser max(6 + 145 + 4 + 7 + 6 = 168, 22 + 145 + 4 + 1
//     = 172) = 172; the row 84 + 6 + 172 = 262, the wheel 362 − 8 − 262 =
//     92 W;
//   cde: the scope max(1 + 5 + 51 + 4 + 16 + 1 = 78, 22 + 51 + 4 + 1 = 78)
//     = 78, the chooser max(1 + 5 + 136 + 4 + 16 + 1 = 163, 22 + 136 + 4 +
//     1 = 163) = 163; the row 78 + 6 + 163 = 247, the wheel 362 − 8 − 247 =
//     107 W.
// The slider's track takes what the label and the value cell leave, and
// the preset button what the hex field, OLD | NEW at its floor and the
// three push buttons leave. THE BOTTOM ROW'S ARITHMETIC (architect
// 2026-10-07): the inner width is 360 W under win2000 (376 less 2 x (edge 2
// + pad 6)) and 362 under clearlooks (376 less 2 x (1 + 6)); the hex field
// is its widest spelling `#DDDDDD` plus its two pads — 53 + 10 = 63 W in
// Tahoma, 60 + 10 = 70 in DejaVu Sans (shaped; the dialog field's 5-W pad
// under both, 2026-10-08); the row's five gaps are 6 W
// each; OLD | NEW its floor, two 20-W swatches inside the frame's two lines
// = 42; Copy, Paste and Close kPushButtonWidthPx = 50 each; THE PRESET
// BUTTON THE REMAINDER, 360 − 63 − 30 − 42 − 150 = 75 W under win2000 and
// 362 − 70 − 30 − 42 − 150 = 70 under clearlooks.
// WHY THE PRESET BUTTON IS IN THIS ROW AND NOT THE CHOOSER'S (architect
// 2026-10-07: the chooser's row first, beside the element combo at the width
// its longest role name needs, the preset combo the remainder — unless that
// remainder fell under 70 W under either chrome): the element combo for
// "Selected Phase Reset Flag" alone left the preset combo 77 W under
// win2000 but 67 under clearlooks, so the ruled fallback stood — the bottom
// row, the three push buttons at Windows' 50 — and the scope took the
// chooser row's other seat (THE WIDTH above).
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
// preset button's preset names; GtkComboBox's arrow spacing).
inline constexpr int kComboTextGapPx  = 4;
// THE CHROME'S COMBO AND ITS LIST — GEOMETRY SHARED BY TWO READERS
// (2026-10-07 evening): the picker's chooser and preset button, and
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
// (Close, the widest, 30 + 12 in DejaVu Sans) — so the preset button
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
    const int edge = spec.vocabulary == GuiChromeVocabulary::Win2000 ? 2 : 1;
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

// -- THE SCOPE AND THE ELEMENTS (the chooser row's two lists) -----------------

// THE SCOPE'S TWO ROWS (the head's THE SCOPE), in the list's order: his
// words, "Chrome" then "Waveform" — the chrome's elements lead, as they led
// the single list of 2026-10-08 ~11:00.
using Scope = AppState::ColorPicker::Scope;
inline constexpr int kScopeCount = 2;
constexpr Scope scope_at(int i) {
    return i == 0 ? Scope::Chrome : Scope::Waveform;
}
constexpr int scope_index(Scope s) { return s == Scope::Chrome ? 0 : 1; }
constexpr const char* scope_display_name(Scope s) {
    return s == Scope::Chrome ? "Chrome" : "Waveform";
}

// THE ELEMENTS (architect 2026-10-08, the chrome knob; its twelve ~11:00;
// under every chrome since ~12:10; by scope since ~18:15): one index space
// over both kinds — THE SCHEME'S TWELVE first, in kGuiChromeLines' order —
// "Chrome", "Chrome Text", "Title", "Title End", "Title Text", the three
// "Inactive" ones, "Selection", "Selection Text", "Field", "Field Text" —
// then THE PALETTE'S FIFTEEN in kGuiPaletteRoles' order; THE CHOOSER LISTS
// THE LIVE SCOPE'S ALONE (scope_element_at), the same rows under every
// chrome (one scheme syntax: under clearlooks "Title End" and "Inactive
// Title End" are listed and ignored, clearlooks_derive.h's head — their OLD
// shows the key's own word). An element is a chrome key or a program role;
// `role` is the key's index in kGuiChromeLines or the role's in
// kGuiPaletteRoles.
inline constexpr std::size_t kElementCount =
    kGuiChromeLineCount + kGuiPaletteRoleCount;
// THE LONGER SCOPE'S ROW COUNT, the chooser list's rows at most.
inline constexpr std::size_t kListRowMax =
    std::max(kGuiChromeLineCount, kGuiPaletteRoleCount);
static_assert(std::tuple_size_v<decltype(AppState::ColorPicker::Stash::list_items)> ==
                  kListRowMax,
              "AppState::ColorPicker::Stash::list_items must hold kListRowMax");
static_assert(kScopeCount <= static_cast<int>(kListRowMax));
struct Element {
    bool        chrome = false;   // a scheme's key, else a program role
    std::size_t role   = 0;
};
std::size_t element_count();
Element     element_at(std::size_t e);
// The scope an element belongs to, the scope's row count, and its row `i`
// as an element index (row ↔ element, the chooser's list).
Scope       element_scope(std::size_t e);
std::size_t scope_element_count(Scope s);
std::size_t scope_element_at(Scope s, std::size_t i);
std::size_t element_row(std::size_t e);
// THE CHOOSER'S NAMES, Title Case (the planner's list, 2026-10-07; the
// knob's twelve, his, 2026-10-08): the role table's names are the file grammar's,
// these the user's.
const char* element_display_name(std::size_t e);
// THE ELEMENT'S LIVE COLOR: a program role's live word, or the live scheme's
// key — an inactive key never picked showing the active key it follows —
// and with no keys in the live scheme the live chrome's COMPILED word of the
// key's role (kGuiChromeLines' compiled_role), so OLD shows what the chrome
// wears.
uint32_t    element_color(std::size_t e);
// THE BLOCK A FIRST PICK CREATES (GuiColorPicker::set_color): the nine
// block keys from the live chrome's compiled words, the inactive three
// absent.
GuiChromePick compiled_chrome_pick();
// THE KEYS OF THE SCHEME ON SCREEN, what Save As writes (palette_file.h's
// write_scheme_file: written as the screen shows it, 2026-10-08): the live
// pick, or — the live chrome's own scheme standing, which carries no keys
// live — that built-in's twelve, its inactive three among them.
GuiChromePick live_scheme_keys();

// -- THE PRESETS ---------------------------------------------------------------

// A PRESET'S SHOWN NAME of the scope's kind (the preset button's label, the
// menu's rows): a built-in's generated display name ("Windows 2000
// Standard", "Clearlooks", "CDE Northern Sky" — the key is the file
// grammar's, this the user's, as element_display_name is; a default palette
// shown by its chrome's own scheme's), a file's name verbatim. Whether
// `name` is a built-in's display name of the scope's kind, case and all —
// the name ask refuses such a name as taken, so the menu never lists two
// rows reading alike.
std::string preset_display_name(Scope s, std::string_view name);
bool        is_builtin_display_name(Scope s, std::string_view name);
// Whether `name` is a built-in of the scope's kind (a default palette, or
// any built-in scheme), and whether it names a preset of that kind at all.
bool        is_builtin_preset(Scope s, std::string_view name);
bool        is_preset_name(Scope s, std::string_view name);

// THE MENU'S FOUR ACTS, in the rows' order (the head).
enum class PresetAct { Save, SaveAs, Rename, Delete };
inline constexpr int kPresetActCount = 4;
constexpr PresetAct preset_act_at(int i) { return static_cast<PresetAct>(i); }
constexpr const char* preset_act_label(PresetAct a) {
    switch (a) {
        case PresetAct::Save:   return "Save";
        case PresetAct::SaveAs: return "Save As";
        case PresetAct::Rename: return "Rename";
        case PresetAct::Delete: return "Delete";
    }
    return "";
}
// THE SCOPE'S ACTIVE PRESET — the live device config's `scheme` or
// `palette` resolved (effective_scheme_name, effective_palette_name): the
// name as written, or the live chrome's own for no line (the head: no field
// holds it).
std::string_view active_preset(const AppState& app, Scope s);
// THE ACTS' TRUTHFUL ENABLED BITS for the live scope (every roster button
// truthful), asked by the painter once per paint and published as painted;
// the lift reads the published bit. SAVE gray while the active preset is a
// built-in (the built-ins cannot be changed) OR while the scope's live words
// equal the active preset's (nothing to save — the comparison IS the
// modified state, no flag keeps it); RENAME and DELETE gray on a built-in;
// SAVE AS always live (every refusal it has is the name's, judged at
// commit_name).
bool preset_act_enabled(const AppState& app, PresetAct a);

// THE MENU'S ROWS for the scope, top to bottom (architect 2026-10-08
// ~11:10; per scope ~18:15; the defaults' separator ~19:30): the four acts
// — a separator — HIS FILES OF THE KIND (byte order) — a separator — THE
// BUILT-INS: under Waveform THE CHROME'S DEFAULT PALETTE ALONE (the live
// chrome's; the other chromes' defaults are their own chromes' — the
// `palette` key still names them, palette_file.h); under Chrome THE THREE
// CHROMES' OWN SCHEMES in the vocabularies' order (windows-2000-standard,
// clearlooks, solaris) — A SEPARATOR ("just put a separator after the
// default color themes", architect 2026-10-08 ~19:30) — then THE CATALOG'S
// OTHER 95 in the catalog's order (its 97 less the two chromes' own it
// holds, windows-2000-standard and clearlooks, listed above). A GROUP OPENS ON ITS SEPARATOR AND AN EMPTY
// GROUP IS NOT SHOWN: with no file, ONE separator stands between the acts
// and the built-ins — a Windows menu never shows an empty group between two
// separators; the defaults' group is never empty. Each separator is a scroll
// row of its own in the layout (layout's preset menu). THE ORDER IS THE
// ACTS' REACHABILITY (2026-10-07): the acts lead, so hung from the button
// they are the rows nearest it, and a menu that scrolls opens at its head
// with the acts in view (the head's THE PRESET MENU). A row is a NAME (its
// load) or an ACT; `separator_before` marks the row that opens a group.
struct PresetMenuRow {
    bool        is_act = false;
    std::string name;                       // a name row's preset
    PresetAct   act    = PresetAct::Save;   // an act row's act
    bool        separator_before = false;   // the row opens a group
};
std::vector<PresetMenuRow> preset_menu_rows(Scope s);

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
    // The chooser row: the scope combo and the element chooser, each the
    // whole combo and, under win2000 and cde, its drop-down button.
    GuiRect scope{0, 0, 0, 0};
    GuiRect scope_button{0, 0, 0, 0};
    GuiRect chooser{0, 0, 0, 0};
    GuiRect chooser_button{0, 0, 0, 0};
    // The chooser row's list, when down (the scope's or the elements',
    // AppState::ColorPicker's `chooser_scope`): its placement (the box, the
    // side, the bar) and its rows by row — the zero rect for a row scrolled
    // out of view (combo_list_item).
    ComboList list;
    GuiRect list_items[kListRowMax]{};
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
    // The preset button and, under win2000 and cde, its drop-down button.
    GuiRect menu_button{0, 0, 0, 0};
    GuiRect menu_button_arrow{0, 0, 0, 0};
    GuiRect buttons[3]{};   // Copy, Paste, Close
    // The preset menu, when down: its box, EVERY ROW (preset_menu_rows)
    // with its rect in the same order — the zero rect for a row scrolled out
    // of view — each separator's top row IN VIEW (a separator scrolled out
    // is not listed) and the bar (render.h's popup scroll block: its scroll
    // rows are the rows with a separator before each group). The painter
    // paints and publishes exactly these.
    GuiRect menu{0, 0, 0, 0};
    std::vector<PresetMenuRow> menu_rows;
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
    // session, SETS THE SCOPE TO WAVEFORM (the head's THE SCOPE: the
    // waveform's colors the common case; a live element of the chrome's
    // parked for the Chrome scope), seats OLD and NEW off the live words and
    // damages the window.
    void open(int tap_x);
    // THE ONE CLOSE BODY — Close, Esc, the close road's head
    // (GuiPrompt::request_close). Abandons a standing edit (the hex field or
    // the name ask), drops the gesture, the list and the menu, keeps the
    // live colors (the head: no question), keeps `role` and the slot,
    // damages the window. Idempotent.
    void close();

    // THE SCOPE: the scope combo's pick. The two scopes' live elements
    // trade places (the head: each scope keeps its own), and the new
    // element seats OLD and NEW as set_element does; the same scope is a
    // no-op. The preset button follows from the scope at the next paint.
    void set_scope(color_picker::Scope scope);
    // THE LIVE ELEMENT: the chooser's pick, one of the scope's. OLD becomes
    // the element's current color; the memories re-seat from its bytes.
    void set_element(std::size_t element);
    // THE ONE WRITER OF NEW (AppState::ColorPicker::rgb): writes the live
    // word for the live element — a program role's in the live palette, or
    // a key's in the live scheme, where THE FIRST PICK OF ANY CHROME ELEMENT
    // OVER THE CHROME'S OWN SCHEME CREATES THE BLOCK (architect 2026-10-08
    // ~11:00): its nine from the live chrome's compiled words
    // (color_picker::compiled_chrome_pick), then the picked key written — an
    // inactive key becoming picked, its two siblings still following —
    // installs it through the apply shape (install_live_words), and keeps the HSV
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
    //    lift, color_picker_release). EACH ACTS ON THE LIVE SCOPE'S KIND
    //    ALONE — a scheme's keys or a palette's fifteen, the other kind's
    //    live words and key untouched (2026-10-08 ~18:15) -----------------
    //
    // LOAD — a name row's tap: the named preset installed live (Chrome:
    // scheme_record's keys, or none for the live chrome's own scheme, the
    // compiled chrome back; Waveform: palette_record's fifteen) through the
    // apply shape, THE SCOPE'S ACTIVE PRESET made `name` (its key written,
    // write_preset_key), OLD and NEW reseated off the live element's new
    // color. A load of the active preset itself is the same act: the file's
    // words back, the unsaved picks dropped.
    void load_preset(std::string_view name);
    // SAVE — the scope's live words over the active file (write_scheme_file
    // with the live scheme, which a file's load always seats, or
    // write_palette_file with the live fifteen); a failure cards. The act is
    // gray where it would be a no-op or a refusal (preset_act_enabled), so
    // the press that reaches it always writes.
    void save_preset();
    // SAVE AS and RENAME — open THE NAME ASK (the head): the field editor
    // in Kind PaletteName, empty (Save As) or over the active name
    // (Rename), the whole text selected; the menu is already down.
    void begin_name_ask(AppState::ColorPicker::NameAsk ask);
    // THE NAME ASK'S ENTER. The pending name is judged here, THE ONE JUDGE
    // (palette_file.h's writers assert what it settles), against THE
    // SCOPE'S KIND'S NAME SPACE (palette_file.h's head: two folders): outside
    // the name grammar (is_palette_name_spelling) "Not a name"; a built-in's
    // key or display name of the kind, or a loaded file's of the kind —
    // another than the one being renamed — "Name taken" (Save does the
    // overwrite; Save As never does); each a refusal the product's way, the
    // editor standing. Then SAVE AS writes the scope's live words under the
    // name — for a scheme the keys on screen (color_picker::live_scheme_keys:
    // while the live chrome's own stands, which carries no keys live, its
    // built-in's twelve, installed live with the write so the active file
    // and the live scheme are one record) — and makes it the scope's active
    // preset (the key written, as a load
    // writes it); RENAME renames the active file and rewrites the key — the
    // same name a no-op that closes the ask. An I/O failure closes the ask
    // and cards (class 5: the live colors and the active preset stand);
    // success closes it.
    void commit_name();
    // DELETE — raise the confirming PROMPT over the standing picker,
    // "Delete '<name>'?" with Delete / Cancel, CANCEL FOCUSED and `d`
    // Delete's letter: the render player's Delete question exactly
    // (render_player_delete — a deletion nothing takes back), the name
    // parked at AppState::ColorPicker::pending_delete (the scope cannot move
    // under the prompt, which outranks the picker). The prompt outranks
    // the picker (ModalDialogOwner), and its two answers are the two below,
    // reached through GuiInputHandler.
    void raise_delete();
    // The prompt's Delete: the parked file of the scope's kind removed; the
    // scope's live words fall to THE LIVE CHROME'S OWN — its compiled
    // chrome (Chrome) or its default fifteen (Waveform), the next launch's
    // picture — installed live, OLD and NEW reseated, the scope's key
    // cleared and persisted — the preset button then shows the chrome's own
    // name. A failure cards and changes nothing else. The Cancel drops the
    // parked name and changes nothing.
    void confirm_delete();
    void cancel_delete();

    // THE DAMAGE OF THE CARD'S OWN CHANGES — the card's rect as painted, the
    // list's and the menu's with it (a color change damages the window
    // through set_color).
    void damage_card();

    // THE `scheme` AND `palette` KEYS' ONE WRITER (the presets' acts): `name`
    // becomes THE LIVE SCOPE'S ACTIVE PRESET — its key's value the name, or
    // EMPTY (NO LINE) when it is the live chrome's own (device_config.h:
    // absent = the chrome's own); a same-value write never reaches the
    // writer (the config's no-op rule); otherwise the live struct takes it
    // and write_device_config persists it, a failure the diagnostic on
    // stderr and the display on a card, the live value standing for the
    // session (class 5).
    void write_preset_key(std::string_view name);
    // THE PRESETS' APPLY: the two kinds' live words installed through the
    // apply shape set_color runs (install_live_words) — the acts pass the
    // other kind's live words through unchanged — and OLD and NEW reseated
    // off the live element's new color with the memories.
    void apply_live_words(const GuiPaletteWords& words,
                          const std::optional<GuiChromePick>& scheme);
    // THE APPLY SHAPE'S ONE ROAD (2026-10-07; the scheme 2026-10-08): the
    // fifteen installed (install_program_palette, palette_file.h) and the
    // scheme's keys (install_chrome_pick, render.h), then the apply shape
    // run once, as install_program_palette's declaration states it. Its two
    // callers are set_color and apply_live_words.
    void install_live_words(const GuiPaletteWords& words,
                            const std::optional<GuiChromePick>& scheme);
};
