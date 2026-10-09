#include "color_picker.h"

#include "clearlooks_paint.h"   // cl_scale_thumb_h_px (the thumb's grab height)
#include "device_config.h"     // the `palette` and `scheme` keys' writer (write_device_config)
#include "icons.h"             // drop_bound_faces (a changed twelve)
#include "notifications.h"
#include "playback_lifecycle.h"
#include "text_shape.h"
#include "theme_file.h"         // the compiled chrome's words (a key's OLD, the block's seed)
#include "viewport.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <utility>

namespace color_picker {

// -- THE ELEMENTS ------------------------------------------------------------------

namespace {
// One per role of kGuiPaletteRoles, in its order (asserted below against
// the table's names, so a role added to the table names its row here).
struct RoleName {
    const char* role;
    const char* name;
};
constexpr RoleName kRoleNames[] = {
    {"waveform_canvas",           "Canvas"},
    {"waveform_ink",              "Ink"},
    {"waveform_outline",          "Waveform Outline"},
    {"cue",                       "Cue"},
    {"range",                     "Range"},
    {"invalid_label",             "Invalid Label"},
    {"invalid_label_selected",    "Invalid Label Selected"},
    {"playhead_stem",             "Playhead"},
    {"scanner",                   "Scanner"},
    {"face",                      "Panel Face"},
};
static_assert(std::size(kRoleNames) == kGuiPaletteRoleCount);
constexpr bool role_names_follow_the_table() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        if (std::string_view(kRoleNames[i].role) != kGuiPaletteRoles[i].name)
            return false;
    return true;
}
static_assert(role_names_follow_the_table());
// The scheme's twelve, in kGuiChromeLines' order (asserted the same
// way; the names his, architect 2026-10-08 ~11:00).
constexpr RoleName kChromeNames[] = {
    {"chrome_ground",               "Chrome"},
    {"chrome_text",                 "Chrome Text"},
    {"chrome_title_start",          "Title"},
    {"chrome_title_end",            "Title End"},
    {"chrome_title_text",           "Title Text"},
    {"chrome_inactive_title_start", "Inactive Title"},
    {"chrome_inactive_title_end",   "Inactive Title End"},
    {"chrome_inactive_title_text",  "Inactive Title Text"},
    {"chrome_selection",            "Selection"},
    {"chrome_selection_text",       "Selection Text"},
    {"chrome_field",                "Field"},
    {"chrome_field_text",           "Field Text"},
};
static_assert(std::size(kChromeNames) == kGuiChromeLineCount);
constexpr bool chrome_names_follow_the_lines() {
    for (std::size_t i = 0; i < kGuiChromeLineCount; ++i)
        if (std::string_view(kChromeNames[i].role) != kGuiChromeLines[i].key)
            return false;
    return true;
}
static_assert(chrome_names_follow_the_lines());

// The compiled theme's word for a chrome role (a key's OLD while no block
// stands, and the seed of the block its first pick creates).
uint32_t compiled_chrome_word(std::string_view role) {
    const std::size_t i = theme_role_index(role);
    assert(i < kGuiThemeRoleCount);
    return chrome_theme_words(live_chrome_spec())[i];
}
} // namespace

std::size_t element_count() { return kElementCount; }

Element element_at(std::size_t e) {
    assert(e < element_count());
    if (e < kGuiChromeLineCount) return Element{true, e};
    return Element{false, e - kGuiChromeLineCount};
}

Scope element_scope(std::size_t e) {
    return element_at(e).chrome ? Scope::Chrome : Scope::Waveform;
}

std::size_t scope_element_count(Scope s) {
    return s == Scope::Chrome ? kGuiChromeLineCount : kGuiPaletteRoleCount;
}

std::size_t scope_element_at(Scope s, std::size_t i) {
    assert(i < scope_element_count(s));
    return s == Scope::Chrome ? i : kGuiChromeLineCount + i;
}

std::size_t element_row(std::size_t e) {
    const Element el = element_at(e);
    return el.role;
}

const char* element_display_name(std::size_t e) {
    const Element el = element_at(e);
    return el.chrome ? kChromeNames[el.role].name : kRoleNames[el.role].name;
}

uint32_t element_color(std::size_t e) {
    const Element el = element_at(e);
    if (!el.chrome) return program_palette_words()[el.role];
    const std::optional<GuiChromePick>& pick = live_chrome_pick();
    return pick ? chrome_line_word(*pick, el.role)
                : compiled_chrome_word(kGuiChromeLines[el.role].compiled_role);
}

GuiChromePick live_scheme_keys() {
    if (const std::optional<GuiChromePick>& live = live_chrome_pick())
        return *live;
    const GuiChromeScheme* own =
        builtin_scheme(live_chrome_spec().default_palette);
    assert(own != nullptr);   // defaults_are_their_chromes_schemes
    return own->chrome;
}

GuiChromePick compiled_chrome_pick() {
    GuiChromePick p;
    for (std::size_t k = 0; k < kGuiChromeLineCount; ++k)
        if (is_chrome_block_line(k))
            set_chrome_line_word(p, k,
                                 compiled_chrome_word(kGuiChromeLines[k].compiled_role));
    return p;
}

// -- THE PRESETS ---------------------------------------------------------------

namespace {
// The name ask's cap is the name grammar's (text_editor.h).
static_assert(text_editor::kMaxPendingCharsPaletteName ==
              static_cast<int>(kPaletteNameMaxBytes));
} // namespace

// A BUILT-IN IS SHOWN BY ITS GENERATED DISPLAY NAME (kGuiChromeSchemes,
// palette_file.h — a default palette by its chrome's own scheme's, the
// same word), a file by its name.
std::string preset_display_name(Scope s, std::string_view name) {
    if (is_builtin_preset(s, name))
        return std::string(builtin_scheme(name)->display_name);
    return std::string(name);
}

bool is_builtin_display_name(Scope s, std::string_view name) {
    if (s == Scope::Chrome) {
        for (const GuiChromeScheme& b : kGuiChromeSchemes)
            if (name == b.display_name) return true;
        return false;
    }
    for (const GuiDefaultPalette& d : kGuiDefaultPalettes)
        if (name == builtin_scheme(d.name)->display_name) return true;
    return false;
}

bool is_builtin_preset(Scope s, std::string_view name) {
    return s == Scope::Chrome ? is_builtin_scheme_name(name)
                              : is_builtin_palette_name(name);
}

bool is_preset_name(Scope s, std::string_view name) {
    return s == Scope::Chrome ? is_scheme_name(name) : is_palette_name(name);
}

std::string_view active_preset(const AppState& app, Scope s) {
    assert(app.device_config != nullptr);
    return s == Scope::Chrome
               ? effective_scheme_name(app.device_config->scheme)
               : effective_palette_name(app.device_config->palette);
}

bool preset_act_enabled(const AppState& app, PresetAct a) {
    const Scope scope = app.color_picker.scope;
    const std::string_view active = active_preset(app, scope);
    const bool builtin = is_builtin_preset(scope, active);
    switch (a) {
        case PresetAct::Save:
            if (builtin) return false;
            return scope == Scope::Chrome
                       ? live_chrome_pick() != scheme_record(active)
                       : program_palette_words() != palette_record(active);
        case PresetAct::SaveAs:
            return true;
        case PresetAct::Rename:
        case PresetAct::Delete:
            return !builtin;
    }
    return false;
}

std::vector<PresetMenuRow> preset_menu_rows(Scope s) {
    // The acts first (the declaration: the order is their reachability).
    std::vector<PresetMenuRow> rows;
    for (int i = 0; i < kPresetActCount; ++i) {
        PresetMenuRow r;
        r.is_act = true;
        r.act    = preset_act_at(i);
        rows.push_back(std::move(r));
    }
    // A group opens on a separator; an empty group is never shown, so with
    // no file the built-ins follow the acts across ONE separator.
    const auto add_name = [&rows](std::string name, bool opens_group) {
        PresetMenuRow r;
        r.name             = std::move(name);
        r.separator_before = opens_group;
        rows.push_back(std::move(r));
    };
    bool first = true;
    for (std::string& n : s == Scope::Chrome ? scheme_file_names()
                                             : palette_file_names()) {
        add_name(std::move(n), first);
        first = false;
    }
    // The built-ins. Under Waveform the live chrome's default palette alone.
    if (s == Scope::Waveform) {
        add_name(std::string(live_chrome_spec().default_palette), true);
        return rows;
    }
    // Under Chrome the chromes' own schemes first, in the vocabularies'
    // order, then — behind their own separator (architect 2026-10-08
    // ~19:30) — every other scheme in the catalog's.
    first = true;
    for (const GuiDefaultPalette& d : kGuiDefaultPalettes) {
        add_name(d.name, first);
        first = false;
    }
    first = true;
    for (const GuiChromeScheme& b : kGuiChromeSchemes) {
        const std::string_view key = b.key;
        if (is_builtin_palette_name(key)) continue;   // a chrome's own, above
        add_name(std::string(key), first);
        first = false;
    }
    return rows;
}

// -- THE COLOR MATH ------------------------------------------------------------

namespace {
constexpr double kPi = std::numbers::pi;

struct Rgb { double r, g, b; };   // each [0, 1]

Rgb bytes_to_unit(uint32_t rgb) {
    return Rgb{((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0,
               (rgb & 0xFF) / 255.0};
}
uint32_t unit_to_bytes(const Rgb& c) {
    const auto byte = [](double u) {
        return static_cast<uint32_t>(
            std::clamp(std::nearbyint(u * 255.0), 0.0, 255.0));
    };
    return (byte(c.r) << 16) | (byte(c.g) << 8) | byte(c.b);
}
// The standard HSV -> RGB on unit values, h in degrees (any real; 360 is 0).
Rgb hsv_unit(double h_deg, double s, double v) {
    s = std::clamp(s, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    double h = std::fmod(h_deg, 360.0);
    if (h < 0.0) h += 360.0;
    const double hh = h / 60.0;
    const int    i  = static_cast<int>(std::floor(hh)) % 6;
    const double f  = hh - std::floor(hh);
    const double p  = v * (1.0 - s);
    const double q  = v * (1.0 - s * f);
    const double t  = v * (1.0 - s * (1.0 - f));
    switch (i) {
        case 0:  return Rgb{v, t, p};
        case 1:  return Rgb{q, v, p};
        case 2:  return Rgb{p, v, t};
        case 3:  return Rgb{p, q, v};
        case 4:  return Rgb{t, p, v};
        default: return Rgb{v, p, q};
    }
}
double intensity_unit(const Rgb& c) {
    return 0.30 * c.r + 0.59 * c.g + 0.11 * c.b;
}
} // namespace

Hsv rgb_to_hsv(uint32_t rgb) {
    const Rgb c = bytes_to_unit(rgb);
    const double mx = std::max({c.r, c.g, c.b});
    const double mn = std::min({c.r, c.g, c.b});
    Hsv out;
    out.v = mx;
    const double d = mx - mn;
    out.s = mx > 0.0 ? d / mx : 0.0;
    if (d <= 0.0) {
        out.h = 0.0;
        return out;
    }
    double h = 0.0;
    if (mx == c.r)      h = std::fmod((c.g - c.b) / d, 6.0);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
    else                h = (c.r - c.g) / d + 4.0;
    h *= 60.0;
    if (h < 0.0) h += 360.0;
    out.h = h;
    return out;
}

uint32_t hsv_to_rgb(double h_deg, double s, double v) {
    return unit_to_bytes(hsv_unit(h_deg, s, v));
}

double gtk_intensity(uint32_t rgb) {
    return intensity_unit(bytes_to_unit(rgb));
}

std::string hex_spelling(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%06X", rgb & 0xFFFFFFu);
    return std::string(buf);
}

std::optional<uint32_t> parse_hex_color(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6) return std::nullopt;
    uint32_t v = 0;
    for (const char c : text) {
        uint32_t d = 0;
        if (c >= '0' && c <= '9')      d = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
        else return std::nullopt;
        v = (v << 4) | d;
    }
    return v;
}

// -- THE LAYOUT -----------------------------------------------------------------

namespace {
// THE TOP BLOCK'S HEIGHT IN W PX (the head's sums): the wheel's side is
// the smaller of it and kWheelBlockWPx (THE GRID).
constexpr int kTopBlockWPx =
    kChooserHeightPx + kControlGapPx + kChannelCount * kSliderRowPx;
static_assert(kTopBlockWPx == 129);

// The widest shaped width of a set of specimens, ceiled to a whole column
// (the modal's own round-up rule: a measured width that places something
// after it rounds UP).
int ceil_px(double w) { return static_cast<int>(std::ceil(w)); }

// The widest of the ten digits in `font`, for the tabular cells (the time
// field's own specimen rule, time_field_metrics).
char widest_digit(const GuiFont& font) {
    char   widest   = '0';
    double widest_w = -1.0;
    for (char d = '0'; d <= '9'; ++d) {
        const char one[2] = {d, '\0'};
        const double w = text_shape::shape_text_run(font, one).width_px;
        if (w > widest_w) { widest_w = w; widest = d; }
    }
    return widest;
}
char widest_hex_digit(const GuiFont& font) {
    char   widest   = widest_digit(font);
    double widest_w = text_shape::shape_text_run(
                          font, std::string(1, widest)).width_px;
    for (char d = 'A'; d <= 'F'; ++d) {
        const char one[2] = {d, '\0'};
        const double w = text_shape::shape_text_run(font, one).width_px;
        if (w > widest_w) { widest_w = w; widest = d; }
    }
    return widest;
}
} // namespace

GuiRect combo_drop_button(const GuiRect& r) {
    if (live_chrome_spec().vocabulary == GuiChromeVocabulary::Clearlooks)
        return GuiRect{0, 0, 0, 0};
    // the sunken field's two lines (one under cde, Motif's text field —
    // paint_picker_combo's cde arm, 2026-10-08)
    const int fb =
        (live_chrome_spec().vocabulary == GuiChromeVocabulary::Cde ? 1 : 2) *
        relief_line_px();
    const int bw = scaled_px(kComboButtonWPx);
    return GuiRect{r.x + r.w - fb - bw, r.y + fb, bw, r.h - 2 * fb};
}

// ONE DROP-DOWN PADDING UNDER EVERY CHROME — THIS COMMENT IS THE RULE'S
// ONE OWNER (architect 2026-10-09 ~00:30, on three tablet captures, of the
// menus' 22-W pads on the picker's preset menu: "a classic padding style,
// but generally used in roomier interfaces; the picker is relegated to
// small space" — ruled: STANDARDIZE
// ALL THREE TO WINDOWS' DROP-DOWN PADDING, APPROXIMATELY, UNDER EVERY
// CHROME, the padding one number "especially on the left"). Windows' own
// combo lists — ReactOS WordPad's font combo — inset their text a few px
// with no check-mark column; the combo's text stands inside the sunken
// field's two lines and the dialog field's 5-W pad (kModalFieldPadXPx).
// That is THE DROP-DOWN INSET, kComboTextInsetPx = 2 + 5 = 7 W, ONE NUMBER
// UNDER EVERY CHROME — under clearlooks too, though its gummy button's own
// pad is 6, and under cde, though its field is one line (the planner's
// reading of "approximately Windows' padding"). It is:
//   THE FACE'S TEXT INSET, from the combo's outer left edge to the name
//     (paint_picker_combo; the layout's combo_w, whose face is symmetric
//     about the name under win2000 and cde);
//   EVERY DROP-DOWN LIST'S ROW INSET, ON BOTH SIDES OF ITS WIDEST NAME — a
//     list's minimum width is inset + name + inset, the name in every row
//     standing directly under the name on the face — for THE SCOPE LIST, THE
//     ELEMENT LIST, THE PRESET MENU (its acts, his files and the built-ins
//     alike; the layout's and the painter's pad) and THE SETTINGS EDITOR'S
//     CHOICE LIST, which reads the same painters (paint_combo_list).
// THE SEPARATORS are the one per-chrome difference among them
// (paint_popup_separator's three kinds). The element chooser's width stays
// the column's remainder (the head's THE WIDTH). The menus' 22-W pads
// (kPopupPadXPx, render.h) are the menu row's pull-downs' alone.
int combo_text_inset_px() {
    return scaled_px(kComboTextInsetPx);
}

// THE LIST IS EXACTLY THE COMBO'S OUTER BOX WIDE, FLUSH AT BOTH EDGES,
// under every chrome (architect 2026-10-08, on his clearlooks capture: "the
// drop-down looks longer than the button you press"): Windows' ComboLBox
// takes the combo's own width (CB_SETDROPPEDWIDTH's default), its frame
// flush with the combo's sunken edge, and GTK 2.20's GtkComboBox sizes its
// popup menu to the widget's ALLOCATION width (gtk_combo_box_menu_popup),
// which is the gummy button's whole box. Measured on that capture
// (2026-10-08, 300 %): list and combo both x 199–1332. What reads wider
// under clearlooks is the button's own 1-W INSET RING — the outermost line
// of its box, inside its xthickness, in a tone near the ground (224 on the
// left, 251 on the right over a 237 ground) — so the combo's dark border
// shows 1 W inside the box on each side while the menu's grey frame stands
// on the box's edge. Flush with the ring is GTK's; the ring is not
// subtracted here.
//
// AN UPWARD BOX CARRIES ITS OWN TOP LINE (2026-10-08, his capture _033145,
// the preset menu standing on its button with its top line missing — and
// the choice editor's capture _010753 the same, the gap hidden there as
// black over the well): under clearlooks the hanging box's top line stands
// one row above it, on its opener's foot, which the opener's own repaint
// carries; standing on the combo's head there is no such row, the line fell
// outside every rect the box damages and was clipped away. So the upward box
// is one line taller and its rows start below that line
// (popup_border_top_px(upward), render.h), under win2000 nothing changing.
//
// THE SIDE AND THE SCROLL ARE THE POPUP LISTS' (render.h's popup scroll
// block, 2026-10-08): place_popup_list picks the side and the shown count,
// and the bar stands inside the box at its right — the box keeping the
// combo's width (the flush ruling above), the rows ending at the bar.
ComboList combo_list(const GuiRect& combo, int count, int window_h, int top) {
    ComboList l;
    const PopupListPlacement p =
        place_popup_list(combo, window_h, count * popup_item_h_px());
    l.box    = GuiRect{combo.x, p.y, combo.w, p.h};
    l.upward = p.upward;
    l.count  = count;
    l.bar    = popup_scroll_bar(
        l.box, p.upward,
        std::vector<int>(static_cast<std::size_t>(count), popup_item_h_px()),
        p.room_h, top);
    return l;
}

GuiRect combo_list_item(const ComboList& l, int i) {
    if (i < l.bar.top || i >= l.bar.top + l.bar.visible || i >= l.count)
        return GuiRect{0, 0, 0, 0};
    return popup_item_rect(l.box,
                           l.box.y + popup_border_top_px(l.upward) +
                               popup_item_margin_y_px() +
                               (i - l.bar.top) * popup_item_h_px(),
                           l.bar.present);
}

Layout layout(const AppState& app, const GuiFont& font) {
    Layout l;
    const ChromeSpec& spec = live_chrome_spec();
    // The card's edge: Windows' two lines, GTK's one, Motif's one (the
    // panel's one-W bevel, 2026-10-08).
    const int lw      = relief_line_px();
    const int edge    = card_edge_wpx(spec) * lw;
    const int pad     = scaled_px(kCardPadPx);
    const int margin  = scaled_px(kCardMarginPx);
    const int gap     = scaled_px(kControlGapPx);   // THE ONE GAP (THE GRID)
    const int chooser_h = scaled_px(kChooserHeightPx);
    const int row_h   = scaled_px(kSliderRowPx, 1);
    const int btn_h   = scaled_px(spec.push_button_box_px);
    // THE BOTTOM BAND (card_bottom_band_wpx): the push button's height, or
    // under cde the taller band the one card height leaves, the row
    // centered in it.
    const int band_h  = scaled_px(card_bottom_band_wpx(spec));
    const int block_h = chooser_h + gap + kChannelCount * row_h;
    const int inner_h = block_h + gap + band_h;
    const int card_w  = scaled_px(kCardWidthPx);
    const int card_h  = 2 * edge + 2 * pad + inner_h;

    // THE SEAT (the head): the foot kCardMarginPx above the on-screen
    // keyboard's band — its rect whether or not it stands, the one rule on
    // both devices — and the card rising from there.
    const GuiRect band = onscreen_keyboard::surface_rect(app);
    l.edge_px = edge;
    l.card = GuiRect{app.color_picker.on_right ? app.width - margin - card_w
                                               : margin,
                     band.y - margin - card_h, card_w, card_h};
    l.inner = GuiRect{l.card.x + edge + pad, l.card.y + edge + pad,
                      card_w - 2 * (edge + pad), inner_h};

    // THE ONE COLUMN SPLIT (the head's THE GRID): the wheel's block
    // kWheelBlockWPx under every chrome, the right column kColumnGapPx past
    // it to the inner right edge; the scope combo as wide as its widest row
    // needs (face or flush list), the element chooser the column's rest.
    const int text_gap = scaled_px(kComboTextGapPx);
    const auto combo_w = [&](double text_w) {
        const int t = ceil_px(text_w);
        const int inset = combo_text_inset_px();
        int face = 0;
        if (spec.vocabulary == GuiChromeVocabulary::Clearlooks) {
            // The inset, the name, then GtkComboBox's own right side: the
            // text gap, the wedge and the gummy button's right pad.
            face = inset + t + text_gap +
                   scaled_px(kComboArrowWPx, kComboArrowMinWPx) +
                   scaled_px(spec.push_button_pad_right_px);
        } else {
            // THE FACE IS SYMMETRIC (architect 2026-10-08 ~22:25, on his cde
            // glass: "the Chrome versus Waveform picker is short on the
            // right-hand side"): the drop-down inset from the outer edge to
            // the name, and the same air inside the field's edge after it
            // before the drop-down button's edge (the inset less the field's
            // lines, one under cde — combo_drop_button), then the button and
            // the field's edge: 2 x inset + name + button.
            face = 2 * inset + t + scaled_px(kComboButtonWPx);
        }
        // THE LIST'S MINIMUM (combo_text_inset_px's rule): the inset on
        // both sides of the name.
        const int list = 2 * inset + t;
        return std::max(face, list);
    };
    double scope_text = 0.0;
    for (int i = 0; i < kScopeCount; ++i)
        scope_text = std::max(scope_text,
                              text_shape::shape_text_run(
                                  font, scope_display_name(scope_at(i))).width_px);
    const int scope_w   = combo_w(scope_text);
    const int pair_gap  = gap;
    const int wheel_w   = scaled_px(kWheelBlockWPx);
    const int col_x     = l.inner.x + wheel_w + scaled_px(kColumnGapPx);
    const int col_w     = l.inner.x + l.inner.w - col_x;
    const int side      = std::max(1, std::min(wheel_w, block_h));

    // THE WHEEL, the left column, centered in its block on both axes. The
    // ring's width is GTK's proportion of the side as laid (the head: side x
    // 15 / 174), rounded at the element.
    l.wheel   = GuiRect{l.inner.x + (wheel_w - side) / 2,
                        l.inner.y + (block_h - side) / 2, side, side};
    l.cx      = l.wheel.x + side / 2.0;
    l.cy      = l.wheel.y + side / 2.0;
    l.outer_r = side / 2.0;
    l.inner_r = l.outer_r -
                std::max(1.0, std::nearbyint(static_cast<double>(side) *
                                             kRingWidthNum / kRingWidthDen));

    // THE RIGHT COLUMN: the chooser row (the scope, then the element
    // chooser, the remainder), then the six slider rows.
    l.scope = GuiRect{col_x, l.inner.y, scope_w, chooser_h};
    l.scope_button = combo_drop_button(l.scope);
    l.chooser = GuiRect{col_x + scope_w + pair_gap, l.inner.y,
                        col_w - scope_w - pair_gap, chooser_h};
    l.chooser_button = combo_drop_button(l.chooser);
    double label_w = 0.0;
    for (int i = 0; i < kChannelCount; ++i)
        label_w = std::max(label_w,
                           text_shape::shape_text_run(
                               font, channel_label(channel_at(i))).width_px);
    const int lab_w = ceil_px(label_w);
    const std::string digits(3, widest_digit(font));
    const int val_w = ceil_px(text_shape::shape_text_run(font, digits).width_px);
    const int gap_l = scaled_px(kSliderLabelGapPx);
    const int gap_v = scaled_px(kSliderValueGapPx);
    const int pad_v = scaled_px(kSliderValuePadPx);   // the cell to the column's edge
    const int rows_y = l.inner.y + chooser_h + gap;
    for (int i = 0; i < kChannelCount; ++i) {
        const int ry = rows_y + i * row_h;
        l.slider_row[i]   = GuiRect{col_x, ry, col_w, row_h};
        l.slider_label[i] = GuiRect{col_x, ry, lab_w, row_h};
        const int tx = col_x + lab_w + gap_l;
        const int tw =
            std::max(1, col_w - lab_w - gap_l - gap_v - val_w - pad_v);
        l.slider_track[i] = GuiRect{tx, ry, tw, row_h};
        l.slider_value[i] = GuiRect{tx + tw + gap_v, ry, val_w, row_h};
    }

    // THE BOTTOM ROW (the head's arithmetic): the hex field, OLD | NEW at
    // its floor, the preset button the remainder, then Copy, Paste
    // and Close at kPushButtonWidthPx, right-flushed — the row's boxes
    // centered in the bottom band (flush with it where the band is the
    // button's height).
    const int by = l.inner.y + block_h + gap + (band_h - btn_h) / 2;
    const int field_h = scaled_px(kModalFieldHeightPx);   // the dialog field's (render.h)
    const int field_pad = scaled_px(kModalFieldPadXPx);
    const std::string hex_specimen = "#" + std::string(6, widest_hex_digit(font));
    const int cell_w = ceil_px(text_shape::shape_text_run(font, hex_specimen).width_px);
    const int field_w = cell_w + 2 * field_pad;
    const GuiRect hex_field{l.inner.x, by + (btn_h - field_h) / 2, field_w,
                            field_h};
    const int swatch_floor = 2 * (scaled_px(kSwatchMinWPx) + lw);
    const int bw = scaled_px(kPushButtonWidthPx);
    const int close_x = l.inner.x + l.inner.w - bw;
    l.buttons[2] = GuiRect{close_x, by, bw, btn_h};
    l.buttons[1] = GuiRect{close_x - gap - bw, by, bw, btn_h};
    l.buttons[0] = GuiRect{close_x - 2 * (gap + bw), by, bw, btn_h};
    const int menu_x = hex_field.x + hex_field.w + gap + swatch_floor + gap;
    l.menu_button = GuiRect{menu_x, by,
                            std::max(1, l.buttons[0].x - gap - menu_x), btn_h};
    // The combo's drop-down button, seated in the preset button's own
    // sunken field.
    l.menu_button_arrow = combo_drop_button(l.menu_button);
    const int sf_x = hex_field.x + hex_field.w + gap;
    const int sf_w = l.menu_button.x - gap - sf_x;
    if (app.color_picker.field_editor.kind == text_editor::Kind::PaletteName &&
        text_editor::is_active(app.color_picker.field_editor)) {
        // THE NAME ASK: the act's word at the left, cap-centered on the
        // button band, then ONE FIELD to OLD | NEW's right edge.
        const char* word =
            app.color_picker.name_ask == AppState::ColorPicker::NameAsk::Rename
                ? preset_act_label(PresetAct::Rename)
                : preset_act_label(PresetAct::SaveAs);
        const int word_w = ceil_px(text_shape::shape_text_run(font, word).width_px);
        l.name_label = GuiRect{l.inner.x, by, word_w, btn_h};
        const int fx = l.inner.x + word_w + scaled_px(kNameLabelGapPx);
        l.field = GuiRect{fx, hex_field.y, sf_x + sf_w - fx, field_h};
    } else {
        l.field = hex_field;
        l.swatch_frame = GuiRect{sf_x, by, sf_w, btn_h};
        const int sw_in_w = sf_w - 2 * lw;
        l.swatch_old = GuiRect{sf_x + lw, by + lw, sw_in_w / 2, btn_h - 2 * lw};
        l.swatch_new = GuiRect{l.swatch_old.x + l.swatch_old.w, by + lw,
                               sw_in_w - sw_in_w / 2, btn_h - 2 * lw};
    }
    l.field_inner = GuiRect{l.field.x + lw, l.field.y + lw, l.field.w - 2 * lw,
                            l.field.h - 2 * lw};

    // THE LIST, when down: the combo's list (combo_list, the popup lists'
    // placement and scroll) from the combo that dropped it, its width — the
    // scope's two rows, or the scope's elements.
    if (app.color_picker.chooser_open) {
        const bool scope_list = app.color_picker.chooser_scope;
        const int count =
            scope_list ? kScopeCount
                       : static_cast<int>(
                             scope_element_count(app.color_picker.scope));
        l.list = combo_list(scope_list ? l.scope : l.chooser, count,
                            app.height, app.color_picker.chooser_scroll.top);
        for (int i = 0; i < count; ++i)
            l.list_items[i] = combo_list_item(l.list, i);
    }

    // THE PRESET MENU, when down: its rows (preset_menu_rows' order, the acts
    // first) with A SEPARATOR BLOCK BEFORE EACH ROW THAT OPENS A GROUP (his
    // files', the built-ins', the catalog's schemes') — each a scroll row of
    // its own, so the menu has a scroll row more than it has rows per separator
    // — placed and scrolled by the popup lists' rule (render.h's popup scroll
    // block): hung from the button's foot or standing on its head, the shown
    // scroll rows — every row from the top whose heights, a separator at its
    // own, fit the box's room — laid from the item block's top. Its width is
    // the widest row's label between THE DROP-DOWN INSET on both sides
    // (combo_text_inset_px's rule, 2026-10-09: the preset menu is a drop-down,
    // not a menu-row pull-down), plus the bar when it scrolls, or the button's,
    // whichever is wider, at the button's left edge held inside the window
    // across — measured over every row, so a scroll never changes it. Every row
    // is in menu_rows; a row scrolled out of view carries the zero rect, so the
    // painter publishes no row the window does not show.
    if (app.color_picker.menu_open) {
        const int pad_x       = combo_text_inset_px();
        const int menu_item_h = popup_item_h_px();
        std::vector<PresetMenuRow> rows =
            preset_menu_rows(app.color_picker.scope);
        const int n    = static_cast<int>(rows.size());
        const int seps = static_cast<int>(std::ranges::count_if(
            rows, [](const PresetMenuRow& r) { return r.separator_before; }));
        // The scroll rows in order: each a row's index, or -1 for the
        // separator that opens the next row's group.
        std::vector<int> scroll_rows;
        scroll_rows.reserve(static_cast<std::size_t>(n + seps));
        for (int i = 0; i < n; ++i) {
            if (rows[static_cast<std::size_t>(i)].separator_before)
                scroll_rows.push_back(-1);
            scroll_rows.push_back(i);
        }
        const int total = static_cast<int>(scroll_rows.size());
        std::vector<int> row_h;
        row_h.reserve(scroll_rows.size());
        for (const int row : scroll_rows)
            row_h.push_back(row < 0 ? popup_sep_block_px() : menu_item_h);
        const int content_h = n * menu_item_h + seps * popup_sep_block_px();
        const PopupListPlacement p =
            place_popup_list(l.menu_button, app.height, content_h);
        double widest = 0.0;
        for (const PresetMenuRow& r : rows) {
            const std::string label =
                r.is_act ? std::string(preset_act_label(r.act))
                         : preset_display_name(app.color_picker.scope, r.name);
            widest = std::max(widest,
                              text_shape::shape_text_run(font, label).width_px);
        }
        const int bar_w = p.scrolls ? popup_scroll_bar_w_px() : 0;
        const int w = std::min(app.width,
                               std::max(l.menu_button.w,
                                        2 * pad_x + ceil_px(widest) + bar_w));
        int mx = l.menu_button.x;
        if (mx + w > app.width) mx = app.width - w;
        if (mx < 0) mx = 0;
        l.menu = GuiRect{mx, p.y, w, p.h};
        l.menu_upward = p.upward;
        l.menu_bar = popup_scroll_bar(l.menu, p.upward, std::move(row_h),
                                      p.room_h,
                                      app.color_picker.menu_scroll.top);
        l.menu_items.assign(rows.size(), GuiRect{0, 0, 0, 0});
        l.menu_sep_ys.clear();
        int iy = p.y + popup_border_top_px(p.upward) + popup_item_margin_y_px();
        const int first = l.menu_bar.top;
        for (int sr = first; sr < first + l.menu_bar.visible && sr < total;
             ++sr) {
            const int row = scroll_rows[static_cast<std::size_t>(sr)];
            if (row < 0) {
                l.menu_sep_ys.push_back(iy);   // a group's head: its separator
                iy += popup_sep_block_px();
                continue;
            }
            l.menu_items[static_cast<std::size_t>(row)] =
                popup_item_rect(l.menu, iy, l.menu_bar.present);
            iy += menu_item_h;
        }
        l.menu_rows = std::move(rows);
    }
    return l;
}

int slider_thumb_x(const GuiRect& track, int value, int max) {
    const int half = scrub_handle_box_px() / 2;
    const int x0 = track.x + half;
    const int x1 = track.x + track.w - 1 - half;
    if (x1 <= x0 || max <= 0) return x0;
    const double t = std::clamp(static_cast<double>(value) / max, 0.0, 1.0);
    return x0 + static_cast<int>(std::nearbyint(t * (x1 - x0)));
}

int slider_value_at(const GuiRect& track, int x, int max) {
    const int half = scrub_handle_box_px() / 2;
    const int x0 = track.x + half;
    const int x1 = track.x + track.w - 1 - half;
    if (x1 <= x0 || max <= 0) return 0;
    const double t =
        std::clamp(static_cast<double>(x - x0) / (x1 - x0), 0.0, 1.0);
    return static_cast<int>(std::nearbyint(t * max));
}

// -- THE WHEEL'S READS -----------------------------------------------------------

namespace {
struct Pt { double x, y; };
// The triangle's three corners for `hue_deg` on the inner circle: the hue,
// white at +120, black at +240 (GtkHSV's compute_triangle; y grows down,
// so the sine is subtracted).
void triangle_corners(const Layout& l, double hue_deg, Pt& h, Pt& w, Pt& b) {
    const auto at = [&](double deg) {
        const double a = deg * kPi / 180.0;
        return Pt{l.cx + std::cos(a) * l.inner_r, l.cy - std::sin(a) * l.inner_r};
    };
    h = at(hue_deg);
    w = at(hue_deg + 120.0);
    b = at(hue_deg + 240.0);
}
// Barycentric weights of `p` against (h, w, b); the signed area twice.
void barycentric(const Pt& p, const Pt& h, const Pt& w, const Pt& b,
                 double& wh, double& ww, double& wb) {
    const double det = (w.y - b.y) * (h.x - b.x) + (b.x - w.x) * (h.y - b.y);
    if (std::fabs(det) < 1e-9) { wh = 1.0; ww = 0.0; wb = 0.0; return; }
    wh = ((w.y - b.y) * (p.x - b.x) + (b.x - w.x) * (p.y - b.y)) / det;
    ww = ((b.y - h.y) * (p.x - b.x) + (h.x - b.x) * (p.y - b.y)) / det;
    wb = 1.0 - wh - ww;
}
Pt closest_on_segment(const Pt& p, const Pt& a, const Pt& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 0.0) return a;
    const double t =
        std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0);
    return Pt{a.x + t * dx, a.y + t * dy};
}
double dist2(const Pt& a, const Pt& b) {
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}
} // namespace

double wheel_hue_at(const Layout& l, int x, int y) {
    const double dx = (x + 0.5) - l.cx;
    const double dy = (y + 0.5) - l.cy;
    double deg = std::atan2(-dy, dx) * 180.0 / kPi;
    if (deg < 0.0) deg += 360.0;
    if (deg >= 360.0) deg -= 360.0;
    return deg;
}

bool wheel_in_ring(const Layout& l, int x, int y) {
    const double dx = (x + 0.5) - l.cx;
    const double dy = (y + 0.5) - l.cy;
    const double r  = std::hypot(dx, dy);
    return r >= l.inner_r && r <= l.outer_r;
}

bool wheel_in_triangle(const Layout& l, double hue_deg, int x, int y) {
    Pt h, w, b;
    triangle_corners(l, hue_deg, h, w, b);
    double wh, ww, wb;
    barycentric(Pt{x + 0.5, y + 0.5}, h, w, b, wh, ww, wb);
    return wh >= 0.0 && ww >= 0.0 && wb >= 0.0;
}

void wheel_sv_at(const Layout& l, double hue_deg, int x, int y, double& s,
                 double& v) {
    Pt h, w, b;
    triangle_corners(l, hue_deg, h, w, b);
    Pt p{x + 0.5, y + 0.5};
    double wh, ww, wb;
    barycentric(p, h, w, b, wh, ww, wb);
    if (wh < 0.0 || ww < 0.0 || wb < 0.0) {
        // Outside: the closest point on the triangle (GtkHSV clamps a drag
        // onto it), then its weights.
        const Pt c1 = closest_on_segment(p, h, w);
        const Pt c2 = closest_on_segment(p, w, b);
        const Pt c3 = closest_on_segment(p, b, h);
        Pt best = c1;
        if (dist2(p, c2) < dist2(p, best)) best = c2;
        if (dist2(p, c3) < dist2(p, best)) best = c3;
        barycentric(best, h, w, b, wh, ww, wb);
        wh = std::clamp(wh, 0.0, 1.0);
        ww = std::clamp(ww, 0.0, 1.0);
    }
    v = std::clamp(wh + ww, 0.0, 1.0);
    s = v > 1e-9 ? std::clamp(wh / (wh + ww), 0.0, 1.0) : 0.0;
}

// -- THE WHEEL'S DRAWING ------------------------------------------------------------

namespace {
// THE TWO CACHED RASTERS (the head's rule: the ring once per size, the
// triangle once per size and hue), ARGB32 premultiplied, the wheel's square.
// Each also keys THE CONVERSION STATE its pixels were premultiplied under
// (display_transform::active(), `converted`), so a flip of True Colors
// (install_true_colors, render.h) rebuilds both.
struct RingCache {
    cairo_surface_t* surf  = nullptr;
    int              side  = -1;
    int              ring  = -1;
    bool             converted = false;
};
struct TriangleCache {
    cairo_surface_t* surf  = nullptr;
    int              side  = -1;
    int              ring  = -1;
    double           hue   = -1.0;
    bool             converted = false;
};
RingCache     g_ring;
TriangleCache g_triangle;

cairo_surface_t* fresh_surface(int side) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
    return s;
}
// One wheel pixel: the straight sRGB color `c` at coverage `cov`, premultiplied.
// ON A P3 WINDOW (display_transform.h's head, architect 2026-10-08) the
// straight color is taken to its bytes, converted for the window, and then
// premultiplied by the coverage, so the wheel shows each hue as the browser
// shows the same sRGB hex; off one, the word is the direct premultiply as
// it always was.
inline uint32_t premultiplied(const Rgb& c, double cov) {
    const uint32_t a = static_cast<uint32_t>(
        std::clamp(std::nearbyint(cov * 255.0), 0.0, 255.0));
    if (display_transform::active()) {
        const auto byte = [](double u) {
            return static_cast<uint32_t>(
                std::clamp(std::nearbyint(u * 255.0), 0.0, 255.0));
        };
        const uint32_t p3 = display_transform::p3_word_from_srgb(
            (byte(c.r) << 16) | (byte(c.g) << 8) | byte(c.b));
        const auto pm = [&](int shift) {
            return static_cast<uint32_t>(std::nearbyint(
                       static_cast<double>((p3 >> shift) & 0xFF) * cov))
                   << shift;
        };
        return (a << 24) | pm(16) | pm(8) | pm(0);
    }
    const auto ch = [&](double u) {
        return static_cast<uint32_t>(
            std::clamp(std::nearbyint(u * cov * 255.0), 0.0, 255.0));
    };
    return (a << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}
inline double coverage(double d) { return std::clamp(d + 0.5, 0.0, 1.0); }

void render_ring(RingCache& c, int side, int ring) {
    if (c.surf != nullptr) cairo_surface_destroy(c.surf);
    c.surf = fresh_surface(side);
    c.side = side;
    c.ring = ring;
    c.converted = display_transform::active();
    cairo_surface_flush(c.surf);
    unsigned char* data = cairo_image_surface_get_data(c.surf);
    const int stride = cairo_image_surface_get_stride(c.surf);
    const double center = side / 2.0;
    const double outer  = side / 2.0;
    const double inner  = outer - ring;
    for (int py = 0; py < side; ++py) {
        uint32_t* row = reinterpret_cast<uint32_t*>(data + py * stride);
        for (int px = 0; px < side; ++px) {
            const double dx = (px + 0.5) - center;
            const double dy = (py + 0.5) - center;
            const double r  = std::hypot(dx, dy);
            const double cov = coverage(outer - r) * coverage(r - inner);
            if (cov <= 0.0) { row[px] = 0; continue; }
            double deg = std::atan2(-dy, dx) * 180.0 / kPi;
            if (deg < 0.0) deg += 360.0;
            row[px] = premultiplied(hsv_unit(deg, 1.0, 1.0), cov);
        }
    }
    cairo_surface_mark_dirty(c.surf);
}

void render_triangle(TriangleCache& c, int side, int ring, double hue_deg) {
    if (c.surf != nullptr) cairo_surface_destroy(c.surf);
    c.surf = fresh_surface(side);
    c.side = side;
    c.ring = ring;
    c.hue  = hue_deg;
    c.converted = display_transform::active();
    cairo_surface_flush(c.surf);
    unsigned char* data = cairo_image_surface_get_data(c.surf);
    const int stride = cairo_image_surface_get_stride(c.surf);
    // The corners in the surface's own frame (a layout with the origin at
    // the square's corner).
    Layout local;
    local.cx = side / 2.0;
    local.cy = side / 2.0;
    local.outer_r = side / 2.0;
    local.inner_r = local.outer_r - ring;
    Pt h, w, b;
    triangle_corners(local, hue_deg, h, w, b);
    // The three edges' inward normals: the signed distance of a point to
    // each edge, positive inside, from the triangle's orientation.
    const Pt verts[3] = {h, w, b};
    double nx[3], ny[3], nd[3];
    const double orient = (w.x - h.x) * (b.y - h.y) - (w.y - h.y) * (b.x - h.x);
    const double sign = orient >= 0.0 ? 1.0 : -1.0;
    for (int e = 0; e < 3; ++e) {
        const Pt& a = verts[e];
        const Pt& q = verts[(e + 1) % 3];
        const double ex = q.x - a.x, ey = q.y - a.y;
        const double len = std::hypot(ex, ey);
        // The inward normal of edge a->q under the orientation.
        nx[e] = sign * (-ey) / len;
        ny[e] = sign * ( ex) / len;
        nd[e] = -(nx[e] * a.x + ny[e] * a.y);
    }
    for (int py = 0; py < side; ++py) {
        uint32_t* row = reinterpret_cast<uint32_t*>(data + py * stride);
        for (int px = 0; px < side; ++px) {
            const Pt p{px + 0.5, py + 0.5};
            double dmin = 1e9;
            for (int e = 0; e < 3; ++e)
                dmin = std::min(dmin, nx[e] * p.x + ny[e] * p.y + nd[e]);
            const double cov = coverage(dmin);
            if (cov <= 0.0) { row[px] = 0; continue; }
            double wh, ww, wb;
            barycentric(p, h, w, b, wh, ww, wb);
            wh = std::clamp(wh, 0.0, 1.0);
            ww = std::clamp(ww, 0.0, 1.0);
            const double v = std::clamp(wh + ww, 0.0, 1.0);
            const double s = v > 1e-9 ? std::clamp(wh / (wh + ww), 0.0, 1.0) : 0.0;
            row[px] = premultiplied(hsv_unit(hue_deg, s, v), cov);
        }
    }
    cairo_surface_mark_dirty(c.surf);
}
} // namespace

void paint_wheel(cairo_t* cr, const Layout& l, double hue_deg, double s,
                 double v, uint32_t rgb) {
    const int side = l.wheel.w;
    if (side <= 0) return;
    const int ring = static_cast<int>(std::nearbyint(l.outer_r - l.inner_r));
    const bool converted = display_transform::active();
    if (g_ring.surf == nullptr || g_ring.side != side || g_ring.ring != ring ||
        g_ring.converted != converted)
        render_ring(g_ring, side, ring);
    if (g_triangle.surf == nullptr || g_triangle.side != side ||
        g_triangle.ring != ring || g_triangle.hue != hue_deg ||
        g_triangle.converted != converted)
        render_triangle(g_triangle, side, ring, hue_deg);

    cairo_save(cr);
    cairo_set_source_surface(cr, g_ring.surf, l.wheel.x, l.wheel.y);
    cairo_paint(cr);
    cairo_set_source_surface(cr, g_triangle.surf, l.wheel.x, l.wheel.y);
    cairo_paint(cr);

    // THE MARKERS (the head): black on a bright color, white on a dark one,
    // GTK's INTENSITY read; the two inks are two of Windows' twenty solids,
    // handed over through the one chokepoint.
    const double lw = relief_line_px();
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_line_width(cr, lw);
    const auto marker_ink = [&](uint32_t under) {
        set_palette_source(cr, gtk_intensity(under) > 0.5 ? hex(0x000000)
                                                          : hex(0xFFFFFF));
    };
    {
        const double a  = hue_deg * kPi / 180.0;
        const double ca = std::cos(a), sa = -std::sin(a);
        marker_ink(hsv_to_rgb(hue_deg, 1.0, 1.0));
        cairo_new_path(cr);
        cairo_move_to(cr, l.cx + ca * l.inner_r, l.cy + sa * l.inner_r);
        cairo_line_to(cr, l.cx + ca * l.outer_r, l.cy + sa * l.outer_r);
        cairo_stroke(cr);
    }
    {
        Pt h, w, b;
        triangle_corners(l, hue_deg, h, w, b);
        const double wh = s * v, ww = v * (1.0 - s), wb = 1.0 - v;
        const Pt p{h.x * wh + w.x * ww + b.x * wb, h.y * wh + w.y * ww + b.y * wb};
        marker_ink(rgb);
        cairo_new_path(cr);
        cairo_arc(cr, p.x, p.y, scaled_px(kSvMarkerRadiusPx, 2), 0.0, 2.0 * kPi);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

} // namespace color_picker

// -- THE ACTS --------------------------------------------------------------------------

namespace {
// THE MEMORIES' RE-SEAT FROM THE BYTES (GuiColorPicker::set_color's rule):
// the hue only when the color has one, the saturation only when it is not
// black.
void reseat_memories(AppState::ColorPicker& cp) {
    const color_picker::Hsv hsv = color_picker::rgb_to_hsv(cp.rgb);
    const uint32_t r = (cp.rgb >> 16) & 0xFF, g = (cp.rgb >> 8) & 0xFF,
                   b = cp.rgb & 0xFF;
    const uint32_t mx = std::max({r, g, b}), mn = std::min({r, g, b});
    if (mx != mn) cp.hue_deg = hsv.h;
    if (mx > 0)   cp.sat     = hsv.s;
}
double value_of(uint32_t rgb) {
    const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return std::max({r, g, b}) / 255.0;
}
} // namespace

void GuiColorPicker::open(int tap_x) {
    AppState::ColorPicker& cp = app.color_picker;
    if (app.prompt.active) return;
    if (app.text_editor_session() != 0) {
        notifications.notify(AppState::NotificationClass::Normal,
                             "Close the editor first");
        return;
    }
    if (app.render_player.active || app.picker.active) return;
    if (cp.active) return;
    if (app.loading) return;

    playback_lifecycle.stop_playback_for_modal_open();
    cp.active   = true;
    cp.session  = text_editor::next_session_id();
    cp.on_right = tap_x < 0 || tap_x < app.width / 2;
    // THE SCOPE AT EVERY OPEN IS WAVEFORM (the declaration): a chrome
    // element kept from the last session is parked for the Chrome scope.
    cp.scope = AppState::ColorPicker::Scope::Waveform;
    if (color_picker::element_scope(cp.element) != cp.scope)
        std::swap(cp.element, cp.parked_element);
    assert(color_picker::element_scope(cp.element) == cp.scope);
    cp.chooser_open    = false;
    cp.chooser_scope   = false;
    cp.chooser_hover   = -1;
    cp.chooser_pressed = -1;
    cp.chooser_press_began_on_item = false;
    cp.menu_open    = false;
    cp.menu_hover   = -1;
    cp.menu_pressed = -1;
    cp.menu_press_began_on_item = false;
    cp.chooser_scroll = PopupScroll{};
    cp.menu_scroll    = PopupScroll{};
    cp.name_ask = AppState::ColorPicker::NameAsk::None;
    cp.pending_delete.clear();
    cp.drag = AppState::ColorPicker::Drag{};
    cp.stash = AppState::ColorPicker::Stash{};
    assert(cp.element < color_picker::element_count());
    cp.rgb     = color_picker::element_color(cp.element);
    cp.old_rgb = cp.rgb;
    reseat_memories(cp);
    // A modal OPEN damages the whole window (the card's rect does not exist
    // before its first paint — the settings opener carries the rule).
    viewport.invalidate_all();
}

void GuiColorPicker::close() {
    AppState::ColorPicker& cp = app.color_picker;
    if (!cp.active) return;
    if (field_active()) text_editor::deactivate(cp.field_editor);
    cp.name_ask = AppState::ColorPicker::NameAsk::None;
    cp.active  = false;
    cp.session = 0;
    cp.chooser_open    = false;
    cp.chooser_scope   = false;
    cp.chooser_hover   = -1;
    cp.chooser_pressed = -1;
    cp.chooser_press_began_on_item = false;
    cp.menu_open    = false;
    cp.menu_hover   = -1;
    cp.menu_pressed = -1;
    cp.menu_press_began_on_item = false;
    cp.chooser_scroll = PopupScroll{};
    cp.menu_scroll    = PopupScroll{};
    cp.drag  = AppState::ColorPicker::Drag{};
    cp.stash = AppState::ColorPicker::Stash{};
    viewport.invalidate_all();
}

void GuiColorPicker::set_scope(color_picker::Scope scope) {
    AppState::ColorPicker& cp = app.color_picker;
    if (scope == cp.scope) return;
    cp.scope = scope;
    std::swap(cp.element, cp.parked_element);
    set_element(cp.element);
}

void GuiColorPicker::set_element(std::size_t element) {
    AppState::ColorPicker& cp = app.color_picker;
    assert(element < color_picker::element_count());
    assert(color_picker::element_scope(element) == cp.scope);
    cp.element = element;
    cp.rgb     = color_picker::element_color(element);
    cp.old_rgb = cp.rgb;
    reseat_memories(cp);
    damage_card();
}

void GuiColorPicker::set_color(uint32_t rgb, bool from_hsv) {
    AppState::ColorPicker& cp = app.color_picker;
    rgb &= 0xFFFFFFu;
    const bool changed = rgb != cp.rgb;
    cp.rgb = rgb;
    if (!from_hsv) reseat_memories(cp);
    if (!changed) {
        damage_card();
        return;
    }
    // THE LIVE APPLY: the live words with one rewritten, through the apply
    // shape's one road (install_live_words). A chrome element's FIRST pick
    // over the chrome's own scheme creates the block whole (the
    // declaration): the nine from the live chrome's compiled words, then the
    // picked key written.
    GuiPaletteWords words = program_palette_words();
    std::optional<GuiChromePick> scheme = live_chrome_pick();
    const color_picker::Element el = color_picker::element_at(cp.element);
    if (!el.chrome) {
        words[el.role] = rgb;
    } else {
        if (!scheme) scheme = color_picker::compiled_chrome_pick();
        set_chrome_line_word(*scheme, el.role, rgb);
    }
    install_live_words(words, scheme);
}

int color_picker::channel_value(const AppState::ColorPicker& cp, Channel c) {
    switch (c) {
        case Channel::Hue:
            return static_cast<int>(std::nearbyint(cp.hue_deg));
        case Channel::Saturation:
            return static_cast<int>(std::nearbyint(cp.sat * 100.0));
        case Channel::Value:
            return static_cast<int>(std::nearbyint(value_of(cp.rgb) * 100.0));
        case Channel::Red:   return static_cast<int>((cp.rgb >> 16) & 0xFF);
        case Channel::Green: return static_cast<int>((cp.rgb >> 8) & 0xFF);
        case Channel::Blue:  return static_cast<int>(cp.rgb & 0xFF);
    }
    return 0;
}

void GuiColorPicker::set_channel(color_picker::Channel c, int value) {
    AppState::ColorPicker& cp = app.color_picker;
    using color_picker::Channel;
    value = std::clamp(value, 0, color_picker::channel_max(c));
    switch (c) {
        case Channel::Hue:
            set_hue(static_cast<double>(value));
            return;
        case Channel::Saturation:
            set_sv(value / 100.0, value_of(cp.rgb));
            return;
        case Channel::Value:
            set_sv(cp.sat, value / 100.0);
            return;
        case Channel::Red:
            set_color((cp.rgb & 0x00FFFFu) | (static_cast<uint32_t>(value) << 16),
                      false);
            return;
        case Channel::Green:
            set_color((cp.rgb & 0xFF00FFu) | (static_cast<uint32_t>(value) << 8),
                      false);
            return;
        case Channel::Blue:
            set_color((cp.rgb & 0xFFFF00u) | static_cast<uint32_t>(value), false);
            return;
    }
}

void GuiColorPicker::set_hue(double h_deg) {
    AppState::ColorPicker& cp = app.color_picker;
    // Kept in [0, 360]: 360 is a value the slider's right end spells, and
    // the conversion treats it as 0.
    cp.hue_deg = std::clamp(h_deg, 0.0, 360.0);
    set_color(color_picker::hsv_to_rgb(cp.hue_deg, cp.sat, value_of(cp.rgb)),
              true);
}

void GuiColorPicker::set_sv(double s, double v) {
    AppState::ColorPicker& cp = app.color_picker;
    cp.sat = std::clamp(s, 0.0, 1.0);
    set_color(color_picker::hsv_to_rgb(cp.hue_deg, cp.sat,
                                       std::clamp(v, 0.0, 1.0)),
              true);
}

void GuiColorPicker::revert_to_old() {
    set_color(app.color_picker.old_rgb, false);
}

void GuiColorPicker::copy_to_slot() {
    AppState::ColorPicker& cp = app.color_picker;
    cp.slot_full = true;
    cp.slot_rgb  = cp.rgb;
    damage_card();   // Paste's face lights
}

void GuiColorPicker::paste_from_slot() {
    const AppState::ColorPicker& cp = app.color_picker;
    if (!cp.slot_full) return;
    set_color(cp.slot_rgb, false);
}

void GuiColorPicker::field_focus(int tap_x) {
    AppState::ColorPicker& cp = app.color_picker;
    if (!field_active()) {
        text_editor::enter(cp.field_editor, /*target=*/0,
                           color_picker::hex_spelling(cp.rgb),
                           text_editor::Kind::PaletteHex);
        // The whole text selected, the caret at its end (enter's seat).
        cp.field_editor.selection_anchor = 0;
        damage_card();
        return;
    }
    if (!cp.stash.field_byte_x.empty()) {
        cp.field_editor.cursor_pos = text_editor::byte_index_from_shaped_x(
            static_cast<double>(tap_x), cp.stash.field_text_origin_x,
            cp.stash.field_byte_x);
        cp.field_editor.selection_anchor = -1;
        text_editor::touch_blink(cp.field_editor);
    }
    damage_card();
}

void GuiColorPicker::field_select_word(int tap_x) {
    AppState::ColorPicker& cp = app.color_picker;
    if (!field_active() || cp.stash.field_byte_x.empty()) return;
    text_editor::select_word_at(
        cp.field_editor,
        text_editor::byte_index_from_shaped_x(static_cast<double>(tap_x),
                                              cp.stash.field_text_origin_x,
                                              cp.stash.field_byte_x));
    damage_card();
}

void GuiColorPicker::field_commit() {
    AppState::ColorPicker& cp = app.color_picker;
    if (!field_active()) return;
    if (name_ask_active()) {
        commit_name();
        return;
    }
    const std::optional<uint32_t> rgb =
        color_picker::parse_hex_color(cp.field_editor.pending);
    if (!rgb) {
        text_editor::refuse(cp.field_editor);
        notifications.notify(AppState::NotificationClass::Normal,
                             "Not a color");
        damage_card();
        return;
    }
    text_editor::deactivate(cp.field_editor);
    set_color(*rgb, false);
    damage_card();
}

void GuiColorPicker::field_cancel() {
    AppState::ColorPicker& cp = app.color_picker;
    if (!field_active()) return;
    text_editor::deactivate(cp.field_editor);
    cp.name_ask = AppState::ColorPicker::NameAsk::None;
    damage_card();
}

// -- THE PRESETS' ACTS (the contract is at the declarations) ---------------------

namespace {
// A writer's failure line on stderr and its one clause on the card (the
// device config's two-clause shape, write_device_config: the diagnostic
// whole, the display short).
void report_preset_failure(GuiNotifications& notifications,
                           const std::string& line, const char* display) {
    std::fprintf(stderr, "warptempo_gui: %s\n", line.c_str());
    notifications.notify(AppState::NotificationClass::Normal, display);
}
// The card's clause for the scope's kind ("Could not save the scheme").
const char* failure_words(color_picker::Scope s, const char* palette,
                          const char* scheme) {
    return s == color_picker::Scope::Chrome ? scheme : palette;
}
} // namespace

void GuiColorPicker::write_preset_key(std::string_view name) {
    DeviceConfig& cfg = *app.device_config;
    std::string& key = app.color_picker.scope == color_picker::Scope::Chrome
                           ? cfg.scheme
                           : cfg.palette;
    // The chrome's own scheme and its default palette share one word
    // (palette_file.h's head), so one test serves both kinds.
    const std::string value =
        name == live_chrome_spec().default_palette ? std::string()
                                                   : std::string(name);
    if (value == key) return;
    key = value;
    const std::optional<GuiFailure> failure = write_device_config(cfg);
    if (failure) {
        std::fprintf(stderr, "warptempo_gui: %s\n",
                     failure->diagnostic.c_str());
        notifications.notify(AppState::NotificationClass::Normal,
                             failure->display);
    }
}

void GuiColorPicker::install_live_words(
        const GuiPaletteWords& words,
        const std::optional<GuiChromePick>& scheme) {
    // The shape and why it is enough are at install_program_palette's
    // declaration (palette_file.h) and, for the chrome, install_chrome_pick's
    // (render.h).
    const WaveformPlateInks before = waveform_plate_inks();
    const std::optional<GuiChromePick> chrome_before = live_chrome_pick();
    install_program_palette(words);
    install_chrome_pick(scheme);
    if (waveform_plate_inks() != before) viewport.kick_waveform_sync();
    else                                 viewport.refresh_flag_cache();
    // A CHANGED TWELVE DAMAGES THE WHOLE SURFACE (2026-10-09): the window's
    // sizing frame — the restored laptop's, cde's dtwm band on both devices —
    // paints in the chrome's roles outside the client area, which
    // invalidate_all never reaches, so a scheme load, a chrome element's
    // pick, its Paste or its OLD tap would leave the band in the previous
    // colors (platform.h's two damage calls). THE FIFTEEN NEED NO MORE THAN
    // THE CLIENT: the palette's colors paint in the well and on what enters
    // it, never on the frame.
    // A CHANGED TWELVE ALSO DROPS THE BOUND ICON FACES (2026-10-10, icons.h's
    // BOUND DRAWING): a bound glyph wears its surface's text role, which the
    // twelve may have moved; the drawings refill lazily in the new colors.
    // The fifteen touch no icon.
    // A CHANGED FACE TAG RIDES THE SAME BRANCH (2026-10-09: the face follows
    // the scheme, GuiChromePick::face, live under the windows chrome —
    // gui_live_face_set, gui_font.h): the tag is part of the pick, so a scheme
    // whose tag differs is a changed pick, the whole surface is damaged, and
    // every text is shaped afresh at the next paint — the scaled fonts and
    // the time-field memo key the live set, the lanes cannot move
    // (gui_font.h's same_lanes), and nothing else holds a shaped run or a
    // width across frames (the flag cache rebuilds on the generation the
    // install bumps; re-grepped 2026-10-09).
    if (scheme != chrome_before) {
        icons::drop_bound_faces();
        viewport.invalidate_surface();
    } else {
        viewport.invalidate_all();
    }
}

void GuiColorPicker::apply_live_words(
        const GuiPaletteWords& words,
        const std::optional<GuiChromePick>& scheme) {
    AppState::ColorPicker& cp = app.color_picker;
    install_live_words(words, scheme);
    cp.rgb     = color_picker::element_color(cp.element);
    cp.old_rgb = cp.rgb;
    reseat_memories(cp);
}

void GuiColorPicker::load_preset(std::string_view name) {
    const color_picker::Scope scope = app.color_picker.scope;
    assert(color_picker::is_preset_name(scope, name));
    const std::string held(name);   // the menu's row may not outlive the call
    // ONE KIND MOVES (the declaration): the other kind's live words stand.
    if (scope == color_picker::Scope::Chrome)
        apply_live_words(program_palette_words(), scheme_record(held));
    else
        apply_live_words(palette_record(held), live_chrome_pick());
    write_preset_key(held);
}

void GuiColorPicker::save_preset() {
    const color_picker::Scope scope = app.color_picker.scope;
    const std::string active(color_picker::active_preset(app, scope));
    std::optional<std::string> failure;
    if (scope == color_picker::Scope::Chrome) {
        // A file's load always seats its keys (scheme_record), and Save is
        // gray on a built-in, so a live scheme stands here.
        assert(live_chrome_pick().has_value());
        failure = write_scheme_file(active, *live_chrome_pick());
    } else {
        failure = write_palette_file(active, program_palette_words());
    }
    if (failure) {
        report_preset_failure(notifications, *failure,
                              failure_words(scope, "Could not save the palette",
                                            "Could not save the scheme"));
    }
    damage_card();   // the button's label, the menu's Save once reopened
}

void GuiColorPicker::begin_name_ask(AppState::ColorPicker::NameAsk ask) {
    AppState::ColorPicker& cp = app.color_picker;
    assert(ask != AppState::ColorPicker::NameAsk::None);
    if (field_active()) text_editor::deactivate(cp.field_editor);
    const std::string prefill =
        ask == AppState::ColorPicker::NameAsk::Rename
            ? std::string(color_picker::active_preset(app, cp.scope))
            : std::string();
    text_editor::enter(cp.field_editor, /*target=*/0, prefill,
                       text_editor::Kind::PaletteName);
    cp.field_editor.selection_anchor = 0;   // the whole text selected
    cp.name_ask = ask;
    // The field widens over OLD | NEW and the buttons gray: the card whole.
    damage_card();
}

void GuiColorPicker::commit_name() {
    AppState::ColorPicker& cp = app.color_picker;
    const color_picker::Scope scope = cp.scope;
    const bool chrome = scope == color_picker::Scope::Chrome;
    const std::string name = cp.field_editor.pending;
    const std::string active(color_picker::active_preset(app, scope));
    const bool rename = cp.name_ask == AppState::ColorPicker::NameAsk::Rename;
    const auto refuse = [&](const char* reason) {
        text_editor::refuse(cp.field_editor);
        notifications.notify(AppState::NotificationClass::Normal, reason);
        damage_card();
    };
    const auto end_ask = [&] {
        text_editor::deactivate(cp.field_editor);
        cp.name_ask = AppState::ColorPicker::NameAsk::None;
        damage_card();
    };
    if (!is_palette_name_spelling(name)) {
        refuse("Not a name");
        return;
    }
    if (rename && name == active) {
        end_ask();   // the same name: the commit's no-op
        return;
    }
    // THE SCOPE'S KIND'S NAME SPACE (the declaration: two folders).
    if (color_picker::is_preset_name(scope, name) ||
        color_picker::is_builtin_display_name(scope, name)) {
        refuse("Name taken");
        return;
    }
    end_ask();
    std::optional<std::string> failure;
    if (rename) {
        failure = chrome ? rename_scheme_file(active, name)
                         : rename_palette_file(active, name);
        if (failure) {
            report_preset_failure(
                notifications, *failure,
                failure_words(scope, "Could not rename the palette",
                              "Could not rename the scheme"));
            return;
        }
    } else if (chrome) {
        // THE KEYS ON SCREEN (the declaration): the live scheme, or the
        // chrome's own built-in's twelve, installed live with the write.
        // Save As from the chrome's own scheme installs the built-in's
        // twelve live, so under windows-2000 the hand-set relief quartet
        // (Hilight FFFFFF, Shadow 808080) becomes the derivation's EAE8E3 /
        // 978E7B: accepted (architect 2026-10-08: Windows' hand-set 3D set
        // against a scheme file's derived set "not a problem ... creating
        // variation is the point"); a save writes the scheme as the screen
        // shows it.
        const bool own = !live_chrome_pick().has_value();
        const GuiChromePick pick = color_picker::live_scheme_keys();
        failure = write_scheme_file(name, pick);
        if (!failure && own) apply_live_words(program_palette_words(), pick);
    } else {
        failure = write_palette_file(name, program_palette_words());
    }
    if (failure) {
        report_preset_failure(notifications, *failure,
                              failure_words(scope, "Could not save the palette",
                                            "Could not save the scheme"));
        return;
    }
    write_preset_key(name);
}

void GuiColorPicker::raise_delete() {
    AppState::ColorPicker& cp = app.color_picker;
    // The menu's lift is its one road, and a standing prompt claims every
    // release first (on_button_release), so no prompt stands here.
    assert(!app.prompt.active);
    const std::string active(color_picker::active_preset(app, cp.scope));
    assert(!color_picker::is_builtin_preset(cp.scope, active));
    cp.pending_delete = active;
    app.prompt.present("Delete '" + active + "'?",
                       {'d', '\x1b'},
                       {"Delete", "Cancel"},
                       DialogTrigger::DELETE_PRESET_CONFIRM,
                       PromptInitialFocus::LastButton);
    viewport.invalidate_all();
}

void GuiColorPicker::confirm_delete() {
    AppState::ColorPicker& cp = app.color_picker;
    const color_picker::Scope scope = cp.scope;
    const std::string name = std::move(cp.pending_delete);
    cp.pending_delete.clear();
    // The prompt's Delete is its one road: raise_delete parked the name
    // before presenting it, and the picker cannot close beneath a standing
    // prompt (the prompt outranks its every close road, and
    // GuiPrompt::request_close returns while a prompt stands), so the scope
    // is the one the question was raised under.
    assert(cp.active && !name.empty());
    const bool chrome = scope == color_picker::Scope::Chrome;
    if (const std::optional<std::string> failure =
            chrome ? remove_scheme_file(name) : remove_palette_file(name)) {
        report_preset_failure(notifications, *failure,
                              failure_words(scope,
                                            "Could not delete the palette",
                                            "Could not delete the scheme"));
        return;
    }
    // THE LIVE CHROME'S OWN of the scope's kind (the declaration).
    const std::string_view fallback = live_chrome_spec().default_palette;
    if (chrome)
        apply_live_words(program_palette_words(), scheme_record(fallback));
    else
        apply_live_words(palette_record(fallback), live_chrome_pick());
    write_preset_key(fallback);
}

void GuiColorPicker::cancel_delete() {
    app.color_picker.pending_delete.clear();
}

void GuiColorPicker::damage_card() {
    const AppState::ColorPicker::Stash& st = app.color_picker.stash;
    if (!st.valid || st.card.w <= 0) {
        viewport.invalidate_all();
        return;
    }
    viewport.invalidate_rect(st.card);
    if (st.list.w > 0 && st.list.h > 0) viewport.invalidate_rect(st.list);
    if (st.menu.w > 0 && st.menu.h > 0) viewport.invalidate_rect(st.menu);
}
