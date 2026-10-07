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

struct GuiNotifications;
struct GuiPlaybackLifecycle;
struct Viewport;

// THE COLOR PICKER (architect 2026-10-07, the picker arc's second segment) —
// the in-app picker of the program's fourteen colors (kGuiPaletteRoles,
// palette_file.h): "a very slimmed down version … a full-fledged part of the
// project … branded with the chrome … not an afterthought". This header is
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
// THE PICKS ARE THE SESSION'S (2026-10-07, until the presets land — Save /
// Save As / the `palette` key, the arc's next segment): every pick writes
// the live words (render.h's program_palette_words) and installs them, a
// Close keeps the live colors, and the next launch returns to the named
// palette. Nothing here touches the device config or the palettes folder.
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
//     showing the live element's name (role_display_name) with the arrow
//     button; a press drops its list — fourteen rows in kGuiPaletteRoles'
//     order, the menu-row popup's painters and press road — and a tap on a
//     row selects. (b)–(g) SIX SLIDER ROWS, Hue 0–360, Saturation 0–100,
//     Value 0–100, Red / Green / Blue 0–255: the label at the left (one
//     width for all six, measured from the widest), the slider in the
//     chrome's own scrub painters (win2000 the channel and Windows' pointed
//     thumb, clearlooks GtkScale's trough with its lower part filled and
//     its thumb), the value's digits at the right in a fixed cell of
//     tabular digits.
//   THE BOTTOM ROW, under both — (h) THE HEX FIELD, a sunken field in the
//     time field's shape and the field pair showing `#RRGGBB`; a tap gives
//     it the focus (a text editor of Kind::PaletteHex); (i) OLD | NEW, two
//     equal swatches in a one-line sunken frame, OLD the element's color
//     when it became the live element, NEW the current, a tap on OLD writing
//     it back; then the push buttons Copy, Paste and Close.
//   WHY THE BOTTOM ROW SPANS THE CARD and is not the right column's tail
//   (the planner's choice under the brief's leave): the card must stand
//   clear of the on-screen keyboard's band (THE HEIGHT BUDGET below), and a
//   right column of chooser + six sliders + field + swatches + buttons
//   cannot; the capture itself puts the swatch pair under the wheel.
//
// THE HEIGHT BUDGET (the architect's constraint, 2026-10-07): the band rises
// on glass whenever a text editor stands (the hex field is one), directly
// above row 8, 131 W tall (onscreen_keyboard.h: 2 x 3 of pad + 4 x 29 of key
// + 3 x 3 of gap; asserted below), and the card keeps kCardMarginPx of air
// above it. At the tablet (2304 x 1440 at 300 %, the well 885 device rows
// under clearlooks = 295 W; 954 = 318 W under win2000) the card may be at
// most 295 − 4 − 131 − 4 = 156 W tall, and IT IS EXACTLY 156 UNDER BOTH
// CHROMES: edge 2 + pad 6 + chooser 21 + gap 2 + six rows of 15 + gap 4 +
// button 23 + pad 6 + edge 2 = 156 (win2000); edge 1 + pad 6 + 21 + 2 + 90
// + 4 + button 25 + pad 6 + edge 1 = 156 (clearlooks) — every term a whole
// Windows px, so at 300 % the sum is exact. The laptop (1080 rows at
// 138 %, the well far taller in W) shows the same card with more air.
//
// THE WIDTH: kCardWidthPx = 376, the tablet's half (768 W) less the margin
// on both sides; the right column takes what the wheel and the column gap
// leave, the slider's track what the label and the value cell leave, and
// the swatch pair what the hex field and the three buttons leave.
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
inline constexpr int kComboButtonWPx  = 16;
inline constexpr int kComboArrowWPx   = 7;
inline constexpr int kComboArrowHPx   = 4;
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
// THE PUSH BUTTONS' WIDTH: Windows' 75 (the dialogs' kModalBtnMinWidthPx),
// the three words all narrower than it.
inline constexpr int kPushButtonWidthPx   = 75;
// THE SWATCH PAIR'S FLOOR: two swatches of at least this inside their frame.
inline constexpr int kSwatchMinWPx        = 16;
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
// head), and the budget it leaves: 295 − 4 − 131 − 4.
inline constexpr int kTabletWellClearlooksWPx = 295;
static_assert(card_height_wpx(kChromeSpecClearlooks) ==
              kTabletWellClearlooksWPx - 2 * kCardMarginPx - kKeyboardBandWPx);
static_assert(card_height_wpx(kChromeSpecWin2000) == 156);

// -- THE CHANNELS -----------------------------------------------------------

// THE SIX SLIDERS, in the rows' order. H S V and R G B are two spellings of
// one color: THE RGB BYTES ARE THE TRUTH (the color is bytes), HSV is
// derived from them, and a drag on an HSV slider writes the bytes it
// converts to (GuiColorPicker::set_channel).
enum class Channel { Hue, Saturation, Value, Red, Green, Blue };
inline constexpr int kChannelCount = 6;
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

// THE CHOOSER'S NAMES, one per role of kGuiPaletteRoles in its order, Title
// Case (the planner's list, 2026-10-07): the role table's names are the
// file grammar's, these the user's.
const char* role_display_name(std::size_t role);

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
// scale, the chrome and the state (which half; whether the list is down):
// the painter paints it and publishes it, the router reads the publication.
struct Layout {
    GuiRect card{0, 0, 0, 0};
    GuiRect inner{0, 0, 0, 0};
    int     edge_px = 0;
    // The wheel's square and its circles (device px, the center at pixel
    // centers of the square's middle).
    GuiRect wheel{0, 0, 0, 0};
    double  cx = 0.0, cy = 0.0, outer_r = 0.0, inner_r = 0.0;
    // The chooser: the whole combo and, under win2000, its drop-down button.
    GuiRect chooser{0, 0, 0, 0};
    GuiRect chooser_button{0, 0, 0, 0};
    // The list, when down: its box and its fourteen rows.
    GuiRect list{0, 0, 0, 0};
    GuiRect list_items[kGuiPaletteRoleCount]{};
    // The six slider rows: the row, the label's seat, the track (the thumb's
    // center travels its inset span, scrub_handle_box_px / 2 in at each end)
    // and the value cell.
    GuiRect slider_row[kChannelCount]{};
    GuiRect slider_label[kChannelCount]{};
    GuiRect slider_track[kChannelCount]{};
    GuiRect slider_value[kChannelCount]{};
    // The bottom row.
    GuiRect hex_field{0, 0, 0, 0};
    GuiRect hex_inner{0, 0, 0, 0};
    GuiRect swatch_frame{0, 0, 0, 0};
    GuiRect swatch_old{0, 0, 0, 0};
    GuiRect swatch_new{0, 0, 0, 0};
    GuiRect buttons[3]{};   // Copy, Paste, Close
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
// GTK's own rule.
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
    // session, seats OLD and NEW off the live words and damages the window.
    void open(int tap_x);
    // THE ONE CLOSE BODY — Close, Esc, the close road's head
    // (GuiPrompt::request_close). Abandons a standing hex edit, drops the
    // gesture and the list, keeps the live colors, keeps `role` and the
    // slot, damages the window. Idempotent.
    void close();

    // THE LIVE ELEMENT: the chooser's pick. OLD becomes the element's
    // current color; the memories re-seat from its bytes.
    void set_role(std::size_t role);
    // THE ONE WRITER OF NEW (AppState::ColorPicker::rgb): writes the live
    // words' word for the live element, installs them
    // (install_program_palette), runs the apply shape — the synchronous
    // plate rebuild and the whole window's damage — and keeps the HSV
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

    // THE HEX FIELD. focus: enter the editor over `#RRGGBB` with the whole
    // text selected, so the first keystroke replaces it (Windows' tab into
    // a field), or, already focused, seat the caret at `tap_x` on the
    // published run (text_editor::byte_index_from_shaped_x). commit:
    // Enter's — parse_hex_color applies, else the refusal the product's way
    // (text_editor::refuse selects the whole text, the card says "Not a
    // color") with the editor standing. cancel: Esc's — the editor leaves
    // and the field shows NEW again. A press anywhere else on the card
    // while the editor stands is the cancel (the flag editor's own rule for
    // a press outside its box).
    void hex_focus(int tap_x);
    void hex_commit();
    void hex_cancel();
    bool hex_active() const {
        return text_editor::is_active(app.color_picker.hex_editor);
    }

    // THE DAMAGE OF THE CARD'S OWN CHANGES — the card's rect as painted, the
    // list's with it (a color change damages the window through set_color).
    void damage_card();
};
