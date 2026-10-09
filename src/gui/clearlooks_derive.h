#pragma once

#include "theme_file.h"        // GuiThemeWords, the role table, the compiled
                               // themes; through render.h, GuiChromePick,
                               // ChromeSpec, kWindowFramePx
#include "clearlooks_paint.h"  // kClScaleTroughPx, the slider's two lengths

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string_view>

// CLEARLOOKS DERIVES ITS TONES FROM THE TWELVE CHROME KEYS (architect
// 2026-10-08 ~11:00 / ~12:10). ONE THEME SYNTAX UNDER EVERY CHROME: a
// scheme's twelve keys (kGuiChromeLines — a SCHEME's alone since the split
// of 2026-10-08, a palette carrying one the read's hard fail; palette_file.h's
// head owns both grammars) draw under clearlooks too, and this header is
// THE ONE DERIVATION there, as chrome_derive.h is under windows-2000.
// "Clearlooks takes ONE caption color and derives its gradient from it":
// THE TITLE END KEYS ARE IGNORED ("the second one just gets ignored if the
// theme doesn't support it"); every other key is read.
//
// THE ARITHMETIC IS SQUEEZE'S OWN, RUN IN THE APP: the 350 `cl_` roles
// (theme_clearlooks_roles.inc) are the Clearlooks engine's and metacity's
// arithmetic on the gtkrc's scheme colors and the metacity theme's draw ops,
// which tools/theme_catalog/build.py's `engine_tones` ran once to record
// squeeze's bytes; THIS IS THAT FUNCTION PORTED, INTEGER FOR INTEGER AND
// DOUBLE FOR DOUBLE, step for step (each C++ function names the Python it
// ports: toolkit_rules.py's GTK 2 block and two renderers' arithmetic,
// build.py's engine_tones and its helpers), over the picked keys instead of
// squeeze's. THE PROOF (static_assert below): fed squeeze's own inputs it
// reproduces theme_clearlooks_values.inc BYTE FOR BYTE FOR EVERY ROLE OF
// THE TABLE — a mismatch is a porting bug, never a tolerance. A change to
// engine_tones is a change here too, the proof the agreement.
//
// THE MAPPING (the twelve keys -> GTK's scheme colors and metacity's reads):
//   Chrome         -> bg_color: every style's bg and its shade table (the
//                     button style's 1.04 / 1.06 / 0.85, the menu's 1.08, the
//                     default style's 0.9 bg[ACTIVE], fg[INSENSITIVE] =
//                     darker (bg_color) as the gtkrc writes them)
//   Chrome Text    -> fg_color (fg[NORMAL], every style's)
//   Field          -> base_color (base[NORMAL])
//   Field Text     -> text_color (text[NORMAL])
//   Selection      -> selected_bg_color (bg / base[SELECTED], the spot
//                     shades, the lit items, the selected rows, GtkEntry's
//                     selection, base[ACTIVE] = shade (0.9) of it, the
//                     entry's focus_color = shade (0.65) of it)
//   Selection Text -> selected_fg_color (fg / text[SELECTED], text[ACTIVE],
//                     the lit items' text)
//   Title          -> what metacity's FOCUSED ops read as gtk:bg[SELECTED]
//                     (the maximised band, the buttons, the glyphs, the
//                     title's shadow, the restored frame's selected lines):
//                     the frame derives from the Title key, the GTK widgets
//                     from the Selection key, the two picked apart.
//   Title End      -> IGNORED (the caption's ramp is metacity's, off Title).
//   Title Text     -> title_text's own color op, which is A LITERAL (#FFFFFF
//                     in squeeze's theme): the key replaces the literal. The
//                     close glyph's cross keeps its own literal (blend
//                     (bg[SELECTED], #FFFFFF, 0.75)), a separate op.
//   Inactive Title -> what the UNFOCUSED ops read as gtk:bg[NORMAL]: the
//                     maximised band, the buttons, AND THE WHOLE RESTORED
//                     UNFOCUSED FRAME (round_bevel_unfocused paints its title
//                     band and its sides in the one read, its lines spanning
//                     both, so the frame stays one piece); the FOCUSED
//                     frame's bg[NORMAL] lines (its sides' bevel and outline)
//                     read Chrome. ABSENT IT FOLLOWS TITLE (GuiChromePick's
//                     accessor, brief 30's rule): a picked block without the
//                     inactive keys gives the laptop an unfocused caption
//                     derived from the title color — his rule, "on the tablet
//                     there is no inactive state". Under the chrome's own
//                     scheme no block stands (palette_file.h's head), so
//                     squeeze's unfocused frame stays exactly as recorded.
//   Inactive Title End  -> IGNORED.
//   Inactive Title Text -> title_text_unfocused's color, which squeeze
//                     computes (blend (fg[NORMAL], bg[NORMAL], 0.45)): the key
//                     stands for the whole computed color, and for the
//                     unfocused close glyph's, the same spec in the theme.
//                     Absent it follows Title Text.
//   THE TOOLTIP PAIR AND THE CARD TRIO stay FIXED, the compiled theme's (no
//   key of the twelve; no cl_ role reads the tooltip colors).
// THE WINDOWS TWENTY-ONE under clearlooks follow the generator's own Clearlooks
// mapping (gen_theme_files.py, roles.py's gnome2 row): ground / label the
// Chrome pair, the relief quartet the engine's one-line edge (shade 1.06 /
// 0.94 of the ground through cairo, light, light, dark, dark), the selected
// and field pairs, the clock panel the field pair (GTK's entry), the
// caption's start and its end THE TITLE (the flat caption: the end ignored),
// its text Title Text, the inactive three the same off the inactive keys.
//
// THE METACITY DRAW OPS ARE FIXED (the theme never changes, only its
// colors): squeeze's Clearlooks ops that engine_tones reads are transcribed
// below as literal C++ sequences, each naming its XML element
// (tmp/squeeze_fs/fs/usr/share/themes/Clearlooks/metacity-1/
// metacity-theme-1.xml); a line's position enters no tone, so a line is its
// color alone where its position decides nothing. Nothing is parsed at run
// time. THE GEOMETRY is the product's (build.py clearlooks_geometry):
// kChromeSpecClearlooks's lengths, kWindowFramePx and the scale's three, and
// the list row (kListRowPx below).
namespace clearlooks_derive {

// -- THE NUMBERS ---------------------------------------------------------------

// A color of doubles in [0, 1] (the engine's CairoColor, metacity's
// arithmetic) and an integer triple (a GdkColor's 16-bit channels).
struct D3 {
    double c[3] = {0.0, 0.0, 0.0};
};
struct I3 {
    int64_t c[3] = {0, 0, 0};
};

// Python's int() of a double: truncation toward zero.
constexpr int64_t trunc_i(double v) { return static_cast<int64_t>(v); }
// math.floor.
constexpr int64_t floor_i(double v) {
    const int64_t t = static_cast<int64_t>(v);
    return v < static_cast<double>(t) ? t - 1 : t;
}
constexpr double dabs(double v) { return v < 0.0 ? -v : v; }
constexpr double dmax(double a, double b) { return b > a ? b : a; }
constexpr double dmin(double a, double b) { return b < a ? b : a; }
constexpr int64_t imin(int64_t a, int64_t b) { return b < a ? b : a; }
constexpr int64_t imax(int64_t a, int64_t b) { return b > a ? b : a; }

constexpr int64_t channel(uint32_t rgb, int i) {
    return static_cast<int64_t>((rgb >> (16 - 8 * i)) & 0xFFu);
}
constexpr uint32_t word(int64_t r, int64_t g, int64_t b) {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

// -- GTK 2 (toolkit_rules.py's GTK 2 block) --------------------------------------

struct Hsb {
    double h = 0.0;
    double s = 0.0;
    double l = 0.0;
};

// gtk2_hsb: cairo-support.c ge_hsb_from_color.
constexpr Hsb gtk2_hsb(const D3& c) {
    const double r = c.c[0], g = c.c[1], b = c.c[2];
    double mx = 0.0, mn = 0.0;
    if (r > g) { mx = dmax(r, b); mn = dmin(g, b); }
    else       { mx = dmax(g, b); mn = dmin(r, b); }
    const double l = (mx + mn) / 2;
    if (dabs(mx - mn) < 0.0001) return Hsb{0.0, 0.0, l};
    const double s = l <= 0.5 ? (mx - mn) / (mx + mn) : (mx - mn) / (2 - mx - mn);
    const double d = mx - mn;
    double h = 0.0;
    if (r == mx)      h = (g - b) / d;
    else if (g == mx) h = 2 + (b - r) / d;
    else              h = 4 + (r - g) / d;
    h *= 60;
    if (h < 0.0) h += 360;
    return Hsb{h, s, l};
}

// _modula: cairo-support.c MODULA, C's truncating cast and remainder.
constexpr double modula(double n, int64_t d) {
    const int64_t i = trunc_i(n);
    const int64_t m = ((i < 0 ? -i : i) % d) * (i >= 0 ? 1 : -1);
    return static_cast<double>(m) + (n - static_cast<double>(i));
}

// gtk2_rgb: cairo-support.c ge_color_from_hsb.
constexpr D3 gtk2_rgb(double h, double s, double l) {
    const double m2 = l <= 0.5 ? l * (1 + s) : l + s - l * s;
    const double m1 = 2 * l - m2;
    D3 out{{l, l, l}};
    if (s != 0) {
        const double hues[3] = {h + 120, h, h - 120};
        for (int i = 0; i < 3; ++i) {
            double m3 = hues[i];
            if (m3 > 360)    m3 = modula(m3, 360);
            else if (m3 < 0) m3 = 360 - modula(dabs(m3), 360);
            if (m3 < 60)       out.c[i] = m1 + (m2 - m1) * m3 / 60;
            else if (m3 < 180) out.c[i] = m2;
            else if (m3 < 240) out.c[i] = m1 + (m2 - m1) * (240 - m3) / 60;
            else               out.c[i] = m1;
        }
    }
    return out;
}

// gtk2_shade: ge_shade_color (= gtk_style_shade, metacity's copy).
constexpr D3 gtk2_shade(const D3& c, double k) {
    if (k == 1.0) return c;
    const Hsb x = gtk2_hsb(c);
    return gtk2_rgb(x.h, dmax(0.0, dmin(x.s * k, 1.0)), dmax(0.0, dmin(x.l * k, 1.0)));
}

// gtk2_mix: ge_mix_color, c1 x (1 - f) + c2 x f.
constexpr D3 gtk2_mix(const D3& c1, const D3& c2, double f) {
    D3 o;
    for (int i = 0; i < 3; ++i) o.c[i] = c1.c[i] * (1 - f) + c2.c[i] * f;
    return o;
}

// cairo_byte: cairo 1.8 / pixman 0.16's pixel, floor (256 d) capped at 255.
constexpr uint32_t cairo_byte(const D3& c) {
    int64_t v[3] = {};
    for (int i = 0; i < 3; ++i) v[i] = imin(255, trunc_i(c.c[i] * 65536) >> 8);
    return word(v[0], v[1], v[2]);
}

// gdk16: a GdkColor stored by gtk_style_shade / metacity, x 65535.0 truncated.
constexpr I3 gdk16(const D3& c) {
    I3 o;
    for (int i = 0; i < 3; ++i) o.c[i] = trunc_i(c.c[i] * 65535.0);
    return o;
}

// gdk_color: "#rrggbb" as GTK parses it, each byte x 257.
constexpr I3 gdk_color(uint32_t rgb) {
    I3 o;
    for (int i = 0; i < 3; ++i) o.c[i] = channel(rgb, i) * 257;
    return o;
}

// gdk_byte: a GdkColor's 24-bit pixel, the top byte of each channel.
constexpr uint32_t gdk_byte(const I3& c16) {
    return word(c16.c[0] >> 8, c16.c[1] >> 8, c16.c[2] >> 8);
}

// build.py's `unit`: a GdkColor as the engine reads it, v / 65535.0.
constexpr D3 unit(const I3& c16) {
    D3 o;
    for (int i = 0; i < 3; ++i) o.c[i] = static_cast<double>(c16.c[i]) / 65535.0;
    return o;
}

// gtkrc_color's shade arm (parse_gtkrc.py): gtk_style_shade on a GdkColor,
// stored as one.
constexpr I3 shade16(const I3& c16, double k) { return gdk16(gtk2_shade(unit(c16), k)); }

// metacity_blend: theme.c color_composite, the FIRST color the background.
constexpr I3 metacity_blend(const I3& bg16, const I3& fg16, double alpha) {
    const int64_t a = trunc_i(alpha * 0xffff);
    I3 o;
    for (int i = 0; i < 3; ++i)
        o.c[i] = bg16.c[i] + (((fg16.c[i] - bg16.c[i]) * a + 0x8000) >> 16);
    return o;
}

// -- THE TWO RENDERERS' ARITHMETIC (toolkit_rules.py) --------------------------

// _cairo_short: cairo 1.8 _cairo_color_double_to_short.
constexpr int64_t cairo_short(double d) { return trunc_i(d * (65536.0 - 1e-5)); }

// _fixed_16_16: cairo 1.8 _cairo_fixed_16_16_from_double.
constexpr int64_t fixed_16_16(double d) { return floor_i(d * 65536.0 + 0.5); }

struct Stop {
    double offset = 0.0;
    D3     color;
};
struct Stops {
    Stop s[4];
    int  n = 0;
};

// pixman_vertical_ramp_row: a vertical cairo linear gradient from y0 to y1
// as pixman 0.16 samples it at pixel row `row`'s centre -> the 8-bit pixel.
constexpr uint32_t pixman_vertical_ramp_row(const Stops& stops, double y0, double y1, int row) {
    const int64_t fy0 = fixed_16_16(y0), fy1 = fixed_16_16(y1);
    const int64_t dy = fy1 - fy0;
    const int64_t b = (int64_t{1} << 32) / dy;
    const int64_t off = (-b * fy0) >> 16;
    const int64_t t = ((b * (int64_t{row} * 65536 + 32768)) >> 16) + off;
    int64_t sx[4] = {};
    int64_t sc[4][3] = {};
    for (int k = 0; k < stops.n; ++k) {
        sx[k] = fixed_16_16(stops.s[k].offset);
        for (int i = 0; i < 3; ++i) sc[k][i] = cairo_short(stops.s[k].color.c[i]) >> 8;
    }
    int n = 0;
    while (n < stops.n && !(t < sx[n])) ++n;
    const int64_t lx = n == 0 ? -(int64_t{1} << 31) : sx[n - 1];
    const int64_t* lc = n == 0 ? sc[0] : sc[n - 1];
    const int64_t rx = n == stops.n ? (int64_t{1} << 31) - 1 : sx[n];
    const int64_t* rc = n == stops.n ? sc[stops.n - 1] : sc[n];
    const bool same = lc[0] == rc[0] && lc[1] == rc[1] && lc[2] == rc[2];
    const int64_t stepper =
        (lx == rx || same) ? 0 : ((int64_t{1} << 24) + (rx - lx) / 2) / (rx - lx);
    const int64_t dist = ((t - lx) * stepper) >> 16;
    int64_t v[3] = {};
    for (int i = 0; i < 3; ++i) v[i] = (lc[i] * (256 - dist) + rc[i] * dist) >> 8;
    return word(v[0], v[1], v[2]);
}

// pixman_vertical_t: the 16.16 position of row `row` from y0 to y1.
constexpr int64_t pixman_vertical_t(int y0, int y1, int row) {
    const int64_t dy = int64_t{y1 - y0} * 65536;
    const int64_t b = (int64_t{1} << 32) / dy;
    return ((b * (int64_t{row} * 65536 + 32768)) >> 16) + ((-b * (int64_t{y0} * 65536)) >> 16);
}

// pixman_step_row: the first row of [y0, y1) at or past one half.
constexpr int pixman_step_row(int y0, int y1) {
    for (int r = y0; r < y1; ++r)
        if (pixman_vertical_t(y0, y1, r) >= 32768) return r;
    return y1;
}

// pixman_alpha_ramp_alpha: alpha a0 at y0 to 0 at y1, row `row`'s byte.
constexpr int64_t pixman_alpha_ramp_alpha(double a0, int y0, int y1, int row) {
    const int64_t dy = int64_t{y1 - y0} * 65536;
    const int64_t b = (int64_t{1} << 32) / dy;
    const int64_t off = (-b * (int64_t{y0} * 65536)) >> 16;
    const int64_t t = ((b * (int64_t{row} * 65536 + 32768)) >> 16) + off;
    const int64_t left = cairo_short(a0) >> 8;
    const int64_t stepper = ((int64_t{1} << 24) + 65536 / 2) / 65536;
    const int64_t dist = imin(256, imax(0, (imax(0, t) * stepper) >> 16));
    return (left * (256 - dist)) >> 8;
}

// _mul_un8.
constexpr int64_t mul_un8(int64_t a, int64_t b) {
    const int64_t t = a * b + 0x80;
    return ((t >> 8) + t) >> 8;
}

// pixman_over: an unpremultiplied 8-bit source at alpha8 over an opaque pixel.
constexpr uint32_t pixman_over(uint32_t src8, int64_t alpha8, uint32_t dst8) {
    int64_t v[3] = {};
    for (int i = 0; i < 3; ++i)
        v[i] = imin(255, mul_un8(channel(dst8, i), 255 - alpha8) + mul_un8(channel(src8, i), alpha8));
    return word(v[0], v[1], v[2]);
}

// cairo_solid_over: a solid cairo source at `alpha` over an opaque pixel.
constexpr uint32_t cairo_solid_over(const D3& c, double alpha, uint32_t dst8) {
    const int64_t a8 = cairo_short(alpha) >> 8;
    int64_t v[3] = {};
    for (int i = 0; i < 3; ++i) {
        const int64_t s = cairo_short(c.c[i] * alpha) >> 8;
        v[i] = imin(255, mul_un8(channel(dst8, i), 255 - a8) + s);
    }
    return word(v[0], v[1], v[2]);
}

// metacity_vertical_gradient_rows: gradient.c meta_gradient_create_vertical,
// the pixel of row `row` of a `height`-row two-color gradient.
constexpr uint32_t metacity_gradient_row(const I3& c0_16, const I3& c1_16, int height, int row) {
    int64_t v[3] = {};
    for (int i = 0; i < 3; ++i) {
        const int64_t r0 = c0_16.c[i] >> 8;
        const int64_t rf = c1_16.c[i] >> 8;
        const int64_t d = trunc_i(static_cast<double>((rf - r0) * 65536) / static_cast<double>(height));
        int64_t acc = r0 * 65536;
        for (int k = 0; k < row; ++k) acc += d;
        v[i] = (acc >> 16) & 255;
    }
    return word(v[0], v[1], v[2]);
}

// -- METACITY'S COLOR SPECS (parse_gtkrc.py metacity_color) ----------------------

// The two GTK reads metacity's frame ops make, gtk:bg[SELECTED] and
// gtk:bg[NORMAL], and what each op list's reads stand for (the head's
// mapping).
enum class Read { Selected, Normal };
struct Shade {
    Read   read = Read::Selected;
    double k    = 1.0;
};
struct McGtk {
    I3 selected;   // gtk:bg[SELECTED]
    I3 normal;     // gtk:bg[NORMAL]
};
// metacity_color's shade arm: "shade/gtk:bg[...]/k".
constexpr I3 mc_color(const Shade& s, const McGtk& g) {
    return shade16(s.read == Read::Selected ? g.selected : g.normal, s.k);
}
// mc_byte.
constexpr uint32_t mc_byte(const Shade& s, const McGtk& g) { return gdk_byte(mc_color(s, g)); }
// A shade factor x 1000, rounded (the roles' names: caption_button_line_role,
// frame_role).
constexpr int milli(double k) { return static_cast<int>(floor_i(k * 1000 + 0.5)); }

// -- THE TRANSCRIBED OPS (metacity-theme-1.xml) --------------------------------

// A COLUMN OP of the maximised caption: a <line> across the caption at row
// `y`, or a vertical <gradient> of `h` rows from row `y`.
struct ColumnOp {
    bool  gradient = false;
    int   y        = 0;
    int   h        = 1;
    Shade a;
    Shade b;
};
// <draw_ops name="bevel_maximized">, its env top_height, title_height and
// height (the frame's: its last line falls below the caption).
constexpr std::array<ColumnOp, 7> bevel_maximized(int top_height, int title_height, int height) {
    constexpr Read S = Read::Selected;
    return {{
        {false, 0,                1,                {S, 0.55}, {}},
        {false, 1,                1,                {S, 1.18}, {}},
        {false, title_height + 5, 1,                {S, 0.94}, {}},
        {true,  top_height / 2,   top_height / 2 - 1, {S, 1.0},  {S, 0.94}},
        {true,  1,                top_height / 2 - 1, {S, 1.08}, {S, 1.02}},
        {false, title_height + 6, 1,                {S, 0.7},  {}},
        {false, height - 1,       1,                {S, 0.55}, {}},
    }};
}
// <draw_ops name="bevel_maximized_unfocused">.
constexpr std::array<ColumnOp, 7> bevel_maximized_unfocused(int top_height, int title_height, int height) {
    constexpr Read N = Read::Normal;
    return {{
        {false, 0,                1,                {N, 0.55}, {}},
        {false, 1,                1,                {N, 1.05}, {}},
        {false, title_height + 5, 1,                {N, 0.89}, {}},
        {true,  top_height / 2,   top_height / 2 - 1, {N, 0.93}, {N, 0.89}},
        {true,  2,                top_height / 2 - 2, {N, 0.99}, {N, 0.95}},
        {false, title_height + 6, 1,                {N, 0.65}, {}},
        {false, height - 1,       1,                {N, 0.55}, {}},
    }};
}

// A CAPTION BUTTON OP: a <line> (its color; its position enters no tone)
// or a vertical <gradient> of `h` rows.
struct ButtonOp {
    bool  gradient = false;
    int   h        = 0;
    Shade a;
    Shade b;
};
constexpr ButtonOp line(Read r, double k) { return ButtonOp{false, 0, {r, k}, {}}; }
constexpr ButtonOp grad(int h, Read r, double k0, double k1) {
    return ButtonOp{true, h, {r, k0}, {r, k1}};
}
// <draw_ops name="button_bg"> at a `height`-row button.
constexpr std::array<ButtonOp, 32> button_bg(int height) {
    constexpr Read S = Read::Selected;
    return {{
        grad(height - 6, S, 0.96, 1.05),                                  // inset
        line(S, 1.00), line(S, 0.99), line(S, 0.99), line(S, 0.98),
        line(S, 0.91), line(S, 0.90),
        line(S, 1.03), line(S, 1.00), line(S, 1.01), line(S, 1.06),
        line(S, 1.02), line(S, 1.03),
        line(S, 0.6), line(S, 0.6), line(S, 0.6), line(S, 0.6),         // border outline
        line(S, 0.6), line(S, 0.6),
        line(S, 1.02), line(S, 1.00), line(S, 0.90),                     // border smooth effect
        line(S, 1.18), line(S, 1.1),                                     // inside highlight
        line(S, 1.0),                                                    // inside shadow
        grad(height / 2 - 1, S, 1.1, 1.02),                              // fill gradient
        grad(height / 2 - 2, S, 1.0, 0.92),
        line(S, 0.84), line(S, 0.92),                                    // bottom border smooth effect
        ButtonOp{}, ButtonOp{}, ButtonOp{},
    }};
}
inline constexpr std::size_t kButtonBgOps = 29;
// <draw_ops name="button_bg_pressed">.
constexpr std::array<ButtonOp, 32> button_bg_pressed(int height) {
    constexpr Read S = Read::Selected;
    return {{
        grad(height - 4, S, 1.2, 1.0), grad(height - 6, S, 1.2, 1.0),    // outside highlight
        line(S, 1.0), line(S, 1.0),
        line(S, 0.55), line(S, 0.55), line(S, 0.55), line(S, 0.55),     // border outline
        line(S, 0.55), line(S, 0.55),
        line(S, 0.9), line(S, 0.85),                                     // inside shadow
        grad(height - 6, S, 0.95, 0.9),                                  // fill gradient
        line(S, 0.9),
    }};
}
inline constexpr std::size_t kButtonBgPressedOps = 14;
// <draw_ops name="button_bg_unfocused">.
constexpr std::array<ButtonOp, 32> button_bg_unfocused(int height) {
    constexpr Read N = Read::Normal;
    return {{
        grad(height - 6, N, 0.92, 0.96),                                 // inset
        line(N, 0.93), line(N, 0.92), line(N, 0.92), line(N, 0.91),
        line(N, 0.87), line(N, 0.86),
        line(N, 0.945), line(N, 0.93), line(N, 0.935), line(N, 0.96),
        line(N, 0.94), line(N, 0.95),
        line(N, 0.6), line(N, 0.6), line(N, 0.6), line(N, 0.6),         // border outline
        line(N, 0.6), line(N, 0.6),
        line(N, 1.02), line(N, 1.00), line(N, 0.95),                     // border smooth effect
        line(N, 1.2), line(N, 1.1),                                      // inside highlight
        line(N, 1.05),                                                   // inside shadow
        grad(height / 2 - 1, N, 1.15, 1.07),                             // fill gradient
        grad(height / 2 - 2, N, 1.05, 0.97),
        line(N, 0.89), line(N, 0.97),                                    // bottom border smooth effect
    }};
}
inline constexpr std::size_t kButtonBgUnfocusedOps = 29;
// <draw_ops name="button_bg_unfocused_pressed">.
constexpr std::array<ButtonOp, 32> button_bg_unfocused_pressed(int height) {
    constexpr Read N = Read::Normal;
    return {{
        grad(height - 4, N, 1.25, 1.05), grad(height - 6, N, 1.25, 1.05),  // outside highlight
        line(N, 1.05), line(N, 1.05),
        line(N, 0.55), line(N, 0.55), line(N, 0.55), line(N, 0.55),     // border outline
        line(N, 0.55), line(N, 0.55),
        line(N, 0.8), line(N, 0.75),                                     // inside shadow
        grad(height - 6, N, 0.9, 0.85),                                  // fill gradient
        line(N, 0.85),
    }};
}
inline constexpr std::size_t kButtonBgUnfocusedPressedOps = 14;

// THE LINES THE CAPTION BUTTON PAINTER DRAWS (build.py CAPTION_BUTTON_DRAWN,
// whose head says why): per state, the shade factors x 1000.
inline constexpr int kDrawnFocused[]          = {980, 1060, 600, 1180, 1100, 1000, 920};
inline constexpr int kDrawnUnfocused[]        = {910, 960, 600, 1200, 1100, 1050, 970};
inline constexpr int kDrawnPressed[]          = {1000, 550, 900, 850};
inline constexpr int kDrawnUnfocusedPressed[] = {1050, 550, 800, 750, 850};

// A RESTORED FRAME OP: a <rectangle filled="true"> (window_bg, the ground
// role's), a <line> or unfilled <rectangle> (a role by its shade), the line
// at title_height + 5 from x 2 to width - 3 (under the lower gradient, not a
// role), or a vertical <gradient> of `h` rows.
enum class FrameKind { FilledRect, Line, TitleSeam, Gradient };
struct FrameOp {
    FrameKind kind = FrameKind::Line;
    int       h    = 0;
    Shade     a;
    Shade     b;
};
// <draw_ops name="round_bevel"> (bevel, corners_outline_selected_top,
// corners_highlight, each <include> in place) at top_height `top`.
constexpr std::array<FrameOp, 32> round_bevel(int top) {
    constexpr Read S = Read::Selected, N = Read::Normal;
    constexpr FrameKind L = FrameKind::Line;
    return {{
        // bevel: window_bg
        {FrameKind::FilledRect, 0, {N, 1.0}, {}},
        // titlebar outline (an unfilled <rectangle>)
        {L, 0, {S, 0.55}, {}},
        // 3d beveled frame
        {L, 0, {N, 0.88}, {}}, {L, 0, {N, 0.88}, {}}, {L, 0, {N, 1.2}, {}}, {L, 0, {N, 1.2}, {}},
        {FrameKind::TitleSeam, 0, {S, 0.94}, {}},
        {L, 0, {S, 0.95}, {}}, {L, 0, {S, 1.18}, {}}, {L, 0, {S, 1.1}, {}},
        // fancy gradient
        {FrameKind::Gradient, top / 2 - 1, {S, 1.0}, {S, 0.94}},
        {FrameKind::Gradient, top / 2 - 2, {S, 1.08}, {S, 1.02}},
        {L, 0, {S, 0.7}, {}},
        // border outline
        {L, 0, {N, 0.45}, {}}, {L, 0, {N, 0.45}, {}}, {L, 0, {N, 0.45}, {}},
        // corners_outline_selected_top
        {L, 0, {S, 0.6}, {}}, {L, 0, {S, 0.73}, {}}, {L, 0, {S, 0.6}, {}}, {L, 0, {S, 0.6}, {}},
        {L, 0, {S, 0.73}, {}},
        {L, 0, {S, 0.6}, {}}, {L, 0, {S, 0.73}, {}}, {L, 0, {S, 0.6}, {}}, {L, 0, {S, 0.6}, {}},
        {L, 0, {S, 0.73}, {}},
        // corners_highlight
        {L, 0, {S, 1.18}, {}}, {L, 0, {S, 1.18}, {}}, {L, 0, {S, 0.98}, {}}, {L, 0, {S, 1.16}, {}},
        {FrameKind::FilledRect, 0, {}, {}}, {FrameKind::FilledRect, 0, {}, {}},
    }};
}
inline constexpr std::size_t kRoundBevelOps = 30;
// <draw_ops name="round_bevel_unfocused"> (bevel_unfocused,
// corners_outline_top, corners_highlight_unfocused).
constexpr std::array<FrameOp, 32> round_bevel_unfocused(int top) {
    constexpr Read N = Read::Normal;
    constexpr FrameKind L = FrameKind::Line;
    return {{
        // bevel_unfocused: window_bg
        {FrameKind::FilledRect, 0, {N, 1.0}, {}},
        {L, 0, {N, 0.88}, {}}, {L, 0, {N, 0.88}, {}}, {L, 0, {N, 1.05}, {}}, {L, 0, {N, 1.03}, {}},
        {FrameKind::TitleSeam, 0, {N, 0.89}, {}},
        // fancy gradient
        {FrameKind::Gradient, top / 2 - 1, {N, 0.93}, {N, 0.89}},
        {FrameKind::Gradient, top / 2 - 2, {N, 0.99}, {N, 0.95}},
        {L, 0, {N, 0.65}, {}},
        // the outline (an unfilled <rectangle>)
        {L, 0, {N, 0.55}, {}},
        // corners_outline_top
        {L, 0, {N, 0.55}, {}}, {L, 0, {N, 0.68}, {}}, {L, 0, {N, 0.55}, {}}, {L, 0, {N, 0.55}, {}},
        {L, 0, {N, 0.68}, {}},
        {L, 0, {N, 0.55}, {}}, {L, 0, {N, 0.68}, {}}, {L, 0, {N, 0.55}, {}}, {L, 0, {N, 0.55}, {}},
        {L, 0, {N, 0.68}, {}},
        // corners_highlight_unfocused
        {L, 0, {N, 1.05}, {}}, {L, 0, {N, 1.05}, {}}, {L, 0, {N, 0.88}, {}}, {L, 0, {N, 1.04}, {}},
    }};
}
inline constexpr std::size_t kRoundBevelUnfocusedOps = 24;

// -- THE GEOMETRY (build.py clearlooks_geometry) -------------------------------

// The list row (folder_overlay.h's kRowHeightPx, 17 W px; clearlooks_paint.cpp
// asserts the two agree — the header is not included here, its own include
// graph being the app state's).
inline constexpr int kListRowPx = 17;

struct Geometry {
    int caption_h, cbtn_w, cbtn_h;
    int menu_head, menu_content, menu_foot;
    int case_h, band_h, push_h, row_h, menu_item_h, frame_w;
    int scale_trough, scale_len, scale_wid;
    int list_bar_w;
};
// GTKTOOLBAR'S TWO LENGTHS AT THE BASE'S CELL, recorded here since the
// program's band took the toolbar's lane (2026-10-09; chrome_spec.h's
// clearlooks head): the tool button 32 (the 24-W icon + 2 x (xthickness 3 +
// inner-border 1), the focus terms zeroed) and the band 36 (its 2 W of air
// each way). No painter draws GtkToolbar now; the generated toolbar tones
// they shape (build.py's clearlooks_geometry, the same two numbers) stay in
// the proof's table.
inline constexpr int kClToolCasePx = 32;
inline constexpr int kClToolBandPx = 36;
constexpr Geometry geometry_of(const ChromeSpec& s) {
    return Geometry{
        s.caption_height_px, s.caption_button_w_px, s.caption_button_h_px,
        s.menu_row_head_px, s.menu_row_content_px, s.menu_row_foot_px,
        kClToolCasePx, kClToolBandPx, static_cast<int>(s.push_button_box_px),
        kListRowPx, s.popup_item_height_px, kWindowFramePx,
        kClScaleTroughPx, kClScaleSliderLengthPx, kClScaleSliderWidthPx,
        s.scroll_bar_px};
}

// -- THE EMITTER -----------------------------------------------------------------

// A role written out of the table's order — a porting bug: never a
// constant expression, so the proof fails the build; at run time the
// class-4 tripwire.
[[noreturn]] inline void derivation_order_breach() { std::abort(); }

// Four digits of a shade's milli (the roles' "%04d").
struct Digits {
    char d[4] = {};
    constexpr explicit Digits(int v) {
        for (int i = 3; i >= 0; --i) { d[i] = static_cast<char>('0' + v % 10); v /= 10; }
    }
    constexpr std::string_view view() const { return std::string_view(d, 4); }
};
// One digit (a ramp's index).
struct Digit {
    char d[1] = {};
    constexpr explicit Digit(int v) : d{static_cast<char>('0' + v)} {}
    constexpr std::string_view view() const { return std::string_view(d, 1); }
};

// THE WORDS IN THE TABLE'S ORDER: each role written in turn, its name
// checked against the table's next — so the derivation assigns every role
// of kGuiThemeRoles exactly once (finish() checks the count). The sequence
// of names depends on no color (the ops and the geometry are fixed), so the
// proof's one evaluation covers every pick.
class Emitter {
public:
    constexpr explicit Emitter(GuiThemeWords& w) : w_(w) {}
    constexpr std::size_t index() const { return i_; }
    constexpr void put(std::initializer_list<std::string_view> parts, uint32_t v) {
        if (i_ >= kGuiThemeRoleCount) derivation_order_breach();
        const std::string_view name = kGuiThemeRoles[i_].name;
        std::size_t pos = 0;
        for (const std::string_view p : parts) {
            if (name.substr(pos, p.size()) != p) derivation_order_breach();
            pos += p.size();
        }
        if (pos != name.size()) derivation_order_breach();
        w_[i_++] = v & 0xFFFFFFu;
    }
    // Tones.ramp: a segment's two ends as `role`_0 / _1.
    constexpr void ramp(std::initializer_list<std::string_view> parts, uint32_t first, uint32_t last) {
        put_suffixed(parts, "_0", first);
        put_suffixed(parts, "_1", last);
    }
    constexpr void finish() const {
        if (i_ != kGuiThemeRoleCount) derivation_order_breach();
    }

private:
    constexpr void put_suffixed(std::initializer_list<std::string_view> parts, std::string_view sfx,
                                uint32_t v) {
        std::string_view p[6] = {};
        std::size_t n = 0;
        for (const std::string_view x : parts) p[n++] = x;
        switch (n) {
            case 1: put({p[0], sfx}, v); break;
            case 2: put({p[0], p[1], sfx}, v); break;
            case 3: put({p[0], p[1], p[2], sfx}, v); break;
            case 4: put({p[0], p[1], p[2], p[3], sfx}, v); break;
            default: derivation_order_breach();
        }
    }
    GuiThemeWords& w_;
    std::size_t    i_ = 0;
};

// -- THE DERIVATION (build.py engine_tones) --------------------------------------

// clearlooks_style_realize's shade table and spot factors.
inline constexpr double kShadeTable[9] = {1.15, 0.95, 0.896, 0.82, 0.7, 0.665, 0.475, 0.45, 0.4};
inline constexpr double kSpotTable[3]  = {1.25, 1.05, 0.65};

// gummy_gradient: the four stops of a gummy face of color c.
constexpr Stops gummy(const D3& c, bool dis) {
    return Stops{{{0.0, gtk2_shade(c, dis ? 1.04 : 1.08)},
                  {0.5, gtk2_shade(c, dis ? 1.01 : 1.02)},
                  {0.5, dis ? gtk2_shade(c, 0.99) : c},
                  {1.0, gtk2_shade(c, dis ? 0.96 : 0.94)}},
                 4};
}
constexpr Stops two_stops(const D3& a, const D3& b) {
    return Stops{{{0.0, a}, {1.0, b}, {}, {}}, 2};
}

// ONE GUMMY FACE (engine_tones' gummy_face): the ramp over rows 2 .. h - 3,
// the border, then the top-left highlight (not active) or the pressed inner
// shadow (active), each baked over the ramp's rows.
constexpr void gummy_face(Emitter& e, std::string_view prefix, std::string_view state, int h,
                          const D3& fill, bool active, bool disabled, uint32_t border) {
    const int y0 = 2, y1 = h - 2;
    const int step = pixman_step_row(y0, y1);
    const Stops g = gummy(fill, disabled);
    const auto ramp = [&](int r) { return pixman_vertical_ramp_row(g, y0, y1, r); };
    const int first = active ? 5 : 2;
    const int seg[2][2] = {{first, step - 1}, {step, h - 3}};
    const std::string_view names[2] = {"_upper", "_lower"};
    for (int s = 0; s < 2; ++s) e.ramp({prefix, state, names[s]}, ramp(seg[s][0]), ramp(seg[s][1]));
    e.put({prefix, state, "_border"}, border);
    if (!active) {
        const D3 hi = gtk2_shade(fill, 1.3);
        const auto over = [&](int r) { return cairo_solid_over(hi, 0.4, ramp(r)); };
        e.put({prefix, state, "_highlight_row"}, over(y0));
        const int hseg[2][2] = {{6, step - 1}, {step, h - 6}};
        for (int s = 0; s < 2; ++s)
            e.ramp({prefix, state, "_highlight", names[s]}, over(hseg[s][0]), over(hseg[s][1]));
        return;
    }
    const uint32_t shadow = cairo_byte(gtk2_shade(fill, 0.92));
    int64_t alpha[3] = {};
    for (int k = 2; k <= 4; ++k) alpha[k - 2] = pixman_alpha_ramp_alpha(0.58, 2, 5, k);
    uint32_t under[3] = {};
    for (int k = 2; k <= 4; ++k) under[k - 2] = pixman_over(shadow, alpha[k - 2], ramp(k));
    const std::string_view rows[3] = {"_shadow_row0", "_shadow_row1", "_shadow_row2"};
    for (int k = 0; k < 3; ++k) e.put({prefix, state, rows[k]}, under[k]);
    const std::string_view cols[3] = {"_shadow_col0", "_shadow_col1", "_shadow_col2"};
    const std::string_view corners[3][3] = {
        {"_shadow_corner00", "_shadow_corner10", "_shadow_corner20"},
        {"_shadow_corner01", "_shadow_corner11", "_shadow_corner21"},
        {"_shadow_corner02", "_shadow_corner12", "_shadow_corner22"}};
    for (int c = 0; c < 3; ++c) {
        for (int s = 0; s < 2; ++s)
            e.ramp({prefix, state, cols[c], names[s]},
                   pixman_over(shadow, alpha[c], ramp(seg[s][0])),
                   pixman_over(shadow, alpha[c], ramp(seg[s][1])));
        for (int k = 0; k < 3; ++k)
            e.put({prefix, state, corners[c][k]}, pixman_over(shadow, alpha[c], under[k]));
    }
}

// One caption button state's roles (engine_tones' caption button loop): its
// gradients' end rows as ramps, each drawn line once at its first
// occurrence.
template <std::size_t Cap, std::size_t Drawn>
constexpr void caption_button(Emitter& e, std::string_view state, const std::array<ButtonOp, Cap>& ops,
                              std::size_t count, const int (&drawn)[Drawn], const McGtk& gtk) {
    int seen[Cap] = {};
    std::size_t nseen = 0;
    int n = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const ButtonOp& op = ops[i];
        if (!op.gradient) {
            const int m = milli(op.a.k);
            bool known = false, is_drawn = false;
            for (std::size_t j = 0; j < nseen; ++j) known = known || seen[j] == m;
            for (const int d : drawn) is_drawn = is_drawn || d == m;
            if (!known && is_drawn) {
                seen[nseen++] = m;
                e.put({"cl_cbtn_", state, "_s", Digits(m).view()}, mc_byte(op.a, gtk));
            }
        } else {
            const I3 c0 = mc_color(op.a, gtk), c1 = mc_color(op.b, gtk);
            e.ramp({"cl_cbtn_", state, "_ramp", Digit(n).view()},
                   metacity_gradient_row(c0, c1, op.h, 0), metacity_gradient_row(c0, c1, op.h, op.h - 1));
            ++n;
        }
    }
}

// One restored frame's roles (engine_tones' frame loop): each line's and
// rectangle's role by its shade once, the corner art's in-between cells
// (0.73 / 0.68) and the window_bg fill and the seam under the lower gradient
// not roles, each gradient a ramp.
template <std::size_t Cap>
constexpr void restored_frame(Emitter& e, std::string_view state, const std::array<FrameOp, Cap>& ops,
                              std::size_t count, const McGtk& gtk) {
    int seen[Cap] = {};
    std::size_t nseen = 0;
    int n = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const FrameOp& op = ops[i];
        if (op.kind == FrameKind::FilledRect || op.kind == FrameKind::TitleSeam) continue;
        if (op.kind == FrameKind::Line) {
            const int m = milli(op.a.k);
            if (m == 730 || m == 680) continue;
            const int key = (op.a.read == Read::Selected ? 10000 : 0) + m;
            bool known = false;
            for (std::size_t j = 0; j < nseen; ++j) known = known || seen[j] == key;
            if (known) continue;
            seen[nseen++] = key;
            e.put({"cl_frame_", state, op.a.read == Read::Selected ? "_sel" : "_bg", Digits(m).view()},
                  mc_byte(op.a, gtk));
        } else {
            const I3 c0 = mc_color(op.a, gtk), c1 = mc_color(op.b, gtk);
            e.ramp({"cl_frame_", state, "_ramp", Digit(n).view()},
                   metacity_gradient_row(c0, c1, op.h, 0), metacity_gradient_row(c0, c1, op.h, op.h - 1));
            ++n;
        }
    }
}

// The maximised caption's column (engine_tones' mc_render column, x = 4 of
// a 9-wide grid: every op spans it) over the caption's H rows, the ops in
// order.
template <std::size_t N>
constexpr std::array<uint32_t, 64> caption_column(const std::array<ColumnOp, N>& ops, int H, const McGtk& gtk) {
    std::array<uint32_t, 64> col{};
    if (H > 64) derivation_order_breach();
    for (const ColumnOp& op : ops) {
        if (!op.gradient) {
            if (op.y >= 0 && op.y < H) col[static_cast<std::size_t>(op.y)] = mc_byte(op.a, gtk);
            continue;
        }
        const I3 c0 = mc_color(op.a, gtk), c1 = mc_color(op.b, gtk);
        for (int i = 0; i < op.h; ++i)
            if (op.y + i >= 0 && op.y + i < H)
                col[static_cast<std::size_t>(op.y + i)] = metacity_gradient_row(c0, c1, op.h, i);
    }
    return col;
}

// THE DERIVATION: every role of the table from the pick (the head's
// mapping), the card trio the compiled theme's, in the table's order.
constexpr GuiThemeWords derive_clearlooks_chrome(const GuiThemeWords& compiled, const GuiChromePick& pick,
                                                 const Geometry& geo) {
    GuiThemeWords w{};
    Emitter e(w);

    // the gtk-color-scheme the keys pick, and the default style's colors as
    // GTK stores them (the gtkrc's assignments)
    const I3 bg16 = gdk_color(pick.ground);
    const I3 sel16 = gdk_color(pick.selection);
    const D3 bg = unit(bg16), sel = unit(sel16);
    const I3 fg_normal16      = gdk_color(pick.text);
    const I3 fg_insensitive16 = shade16(bg16, 0.7);      // darker (@bg_color)
    const I3 text_normal16    = gdk_color(pick.field_text);
    const I3 text_selected16  = gdk_color(pick.selection_text);   // = text[ACTIVE]
    const I3 base_normal16    = gdk_color(pick.field);
    const I3 base_active16    = shade16(sel16, 0.9);     // shade (0.9, @selected_bg_color)
    const I3 bg_active16      = shade16(bg16, 0.9);      // the default style's, the scrollbar's
    // the button style's bg (1.04 / 1.06 / 0.85; INSENSITIVE the default's @bg_color)
    const D3 button_normal      = unit(shade16(bg16, 1.04));
    const D3 button_prelight    = unit(shade16(bg16, 1.06));
    const D3 button_active      = unit(shade16(bg16, 0.85));
    const D3 button_insensitive = unit(bg16);
    const I3 menu_bg16 = shade16(bg16, 1.08);            // the menu style's bg[NORMAL]
    const D3 focus = unit(shade16(sel16, 0.65));         // the entry style's focus_color

    std::array<D3, 9> SH{}, BSH{};
    for (int i = 0; i < 9; ++i) {
        SH[static_cast<std::size_t>(i)]  = gtk2_shade(bg, kShadeTable[i]);
        BSH[static_cast<std::size_t>(i)] = gtk2_shade(button_normal, kShadeTable[i]);
    }
    std::array<D3, 3> SP{};
    for (int i = 0; i < 3; ++i) SP[static_cast<std::size_t>(i)] = gtk2_shade(sel, kSpotTable[i]);

    // THE WINDOWS TWENTY-ONE (the head)
    const uint32_t inset_light = cairo_byte(gtk2_shade(bg, 1.06));
    const uint32_t inset_dark  = cairo_byte(gtk2_shade(bg, 0.94));
    e.put({"ground"}, pick.ground);
    e.put({"label"}, pick.text);
    e.put({"hilight"}, inset_light);
    e.put({"light_3d"}, inset_light);
    e.put({"shadow"}, inset_dark);
    e.put({"dk_shadow"}, inset_dark);
    e.put({"selected_fill"}, pick.selection);
    e.put({"selected_text"}, pick.selection_text);
    e.put({"field_ground"}, pick.field);
    e.put({"field_text"}, pick.field_text);
    e.put({"clock_ground"}, pick.field);
    e.put({"clock_text"}, pick.field_text);
    e.put({"card_ground"}, compiled[e.index()]);
    e.put({"card_text"}, compiled[e.index()]);
    e.put({"card_frame"}, compiled[e.index()]);
    e.put({"caption_active"}, pick.title_start);
    e.put({"caption_active_gradient"}, pick.title_start);
    e.put({"caption_active_text"}, pick.title_text);
    e.put({"caption_inactive"}, pick.inactive_start());
    e.put({"caption_inactive_gradient"}, pick.inactive_start());
    e.put({"caption_inactive_text"}, pick.inactive_text());
    // THE CDE BLOCK (theme_file.h, 2026-10-08): seven roles no Clearlooks
    // painter reads, CARRIED from the compiled theme, as the Windows
    // derivation carries the cl_ block.
    e.put({"cde_select"}, compiled[e.index()]);
    e.put({"cde_field_ts"}, compiled[e.index()]);
    e.put({"cde_field_bs"}, compiled[e.index()]);
    e.put({"cde_title_ts"}, compiled[e.index()]);
    e.put({"cde_title_bs"}, compiled[e.index()]);
    e.put({"cde_inactive_ts"}, compiled[e.index()]);
    e.put({"cde_inactive_bs"}, compiled[e.index()]);

    // THE CAPTION (metacity: the maximised frame's bevel, the title, the
    // buttons and their glyphs; GDK's byte road)
    const McGtk focused{gdk_color(pick.title_start), bg16};
    const McGtk unfocused{gdk_color(pick.inactive_start()), gdk_color(pick.inactive_start())};
    const int H = geo.caption_h;
    {
        const auto col = caption_column(bevel_maximized(H, H - 7, 1000), H, focused);
        e.put({"cl_caption_edge"}, col[0]);
        e.ramp({"cl_caption_upper"}, col[1], col[static_cast<std::size_t>(H / 2 - 1)]);
        e.ramp({"cl_caption_lower"}, col[static_cast<std::size_t>(H / 2)], col[static_cast<std::size_t>(H - 2)]);
        e.put({"cl_caption_foot"}, col[static_cast<std::size_t>(H - 1)]);
    }
    {
        const auto col = caption_column(bevel_maximized_unfocused(H, H - 7, 1000), H, unfocused);
        e.put({"cl_caption_unfocused_edge"}, col[0]);
        e.put({"cl_caption_unfocused_light"}, col[1]);
        e.ramp({"cl_caption_unfocused_upper"}, col[2], col[static_cast<std::size_t>(H / 2 - 1)]);
        e.ramp({"cl_caption_unfocused_lower"}, col[static_cast<std::size_t>(H / 2)],
               col[static_cast<std::size_t>(H - 2)]);
        e.put({"cl_caption_unfocused_foot"}, col[static_cast<std::size_t>(H - 1)]);
    }
    // <draw_ops name="title_text">: four shadow copies in shade/gtk:bg[SELECTED]/0.7,
    // then the title in the literal the Title Text key replaces
    e.put({"cl_title_text"}, gdk_byte(gdk_color(pick.title_text)));
    e.put({"cl_title_shadow"}, mc_byte({Read::Selected, 0.7}, focused));
    // <draw_ops name="title_text_unfocused">: its one computed color, the
    // Inactive Title Text key's
    e.put({"cl_title_unfocused"}, gdk_byte(gdk_color(pick.inactive_text())));
    // <draw_ops name="close_button_icon">: the outline shade/gtk:bg[SELECTED]/0.7,
    // the cross blend/gtk:bg[SELECTED]/#FFFFFF/0.75 (its own literal)
    e.put({"cl_cglyph_dark"}, mc_byte({Read::Selected, 0.7}, focused));
    e.put({"cl_cglyph_light"}, gdk_byte(metacity_blend(focused.selected, gdk_color(0xFFFFFF), 0.75)));
    // <draw_ops name="close_button_icon_unfocused">: title_text_unfocused's spec
    e.put({"cl_cglyph_unfocused"}, gdk_byte(gdk_color(pick.inactive_text())));
    const int bh = geo.cbtn_h;
    caption_button(e, "focused", button_bg(bh), kButtonBgOps, kDrawnFocused, focused);
    caption_button(e, "pressed", button_bg_pressed(bh), kButtonBgPressedOps, kDrawnPressed, focused);
    caption_button(e, "unfocused", button_bg_unfocused(bh), kButtonBgUnfocusedOps, kDrawnUnfocused, unfocused);
    caption_button(e, "unfocused_pressed", button_bg_unfocused_pressed(bh), kButtonBgUnfocusedPressedOps,
                   kDrawnUnfocusedPressed, unfocused);

    // THE MENU BAR (clearlooks_draw_menubar2) and its OPEN TITLE
    // (clearlooks_gummy_draw_menubaritem), the texts
    {
        const int bar_h = geo.menu_head + geo.menu_content + geo.menu_foot;
        const Stops st = two_stops(bg, gtk2_shade(bg, 0.96));
        e.ramp({"cl_menubar_ramp"}, pixman_vertical_ramp_row(st, 0, bar_h, 0),
               pixman_vertical_ramp_row(st, 0, bar_h, bar_h - 2));
        e.put({"cl_menubar_shadow"}, cairo_byte(SH[3]));
        e.put({"cl_menubar_text"}, gdk_byte(fg_normal16));
        e.put({"cl_text_insensitive"}, gdk_byte(fg_insensitive16));
        e.put({"cl_text_insensitive_etch"}, gdk_byte(gdk16(gtk2_shade(bg, 1.2))));
        const int y0 = geo.menu_head, ih = geo.menu_content + 1;
        const Stops g = gummy(SP[1], false);
        const auto row = [&](int r) { return pixman_vertical_ramp_row(g, y0, y0 + ih, r); };
        const int step = pixman_step_row(y0, y0 + ih);
        e.ramp({"cl_menubaritem_upper"}, row(y0 + 1), row(step - 1));
        e.ramp({"cl_menubaritem_lower"}, row(step), row(y0 + ih - 2));
        e.put({"cl_menubaritem_border"}, cairo_byte(SP[2]));
        e.put({"cl_menubaritem_text"}, gdk_byte(text_selected16));   // menu_item fg[PRELIGHT] = @selected_fg_color
    }

    // THE TOOLBAR BAND (clearlooks_gummy_draw_toolbar) and its SEPARATOR
    {
        const int bh_ = geo.band_h;
        const Stops st{{{0.0, gtk2_shade(bg, 1.04)}, {0.5, gtk2_shade(bg, 1.01)}, {0.5, bg},
                        {1.0, gtk2_shade(bg, 0.97)}},
                       4};
        const auto row = [&](int r) { return pixman_vertical_ramp_row(st, 0, bh_, r); };
        const int step = pixman_step_row(0, bh_);
        e.put({"cl_toolbar_light"}, cairo_byte(gtk2_shade(bg, 1.1)));
        e.ramp({"cl_toolbar_upper"}, row(1), row(step - 1));
        e.ramp({"cl_toolbar_lower"}, row(step), row(bh_ - 2));
        e.put({"cl_toolbar_shadow"}, cairo_byte(SH[3]));
        e.put({"cl_separator_dark"}, cairo_byte(SH[3]));
        e.put({"cl_separator_light"}, cairo_byte(gtk2_shade(SH[3], 1.3)));
    }

    // THE TOOL BUTTON (clearlooks_gummy_draw_button, the "button" style)
    const D3 pbg = bg;
    e.put({"cl_button_ring_outer"}, cairo_byte(gtk2_shade(pbg, 0.97)));
    e.put({"cl_button_ring_inner"}, cairo_byte(gtk2_shade(pbg, 0.93)));
    e.put({"cl_button_inset_dark"}, cairo_byte(gtk2_shade(pbg, 0.94)));
    e.put({"cl_button_inset_light"}, cairo_byte(gtk2_shade(pbg, 1.06)));
    const auto border_of = [&](const D3& fill, bool disabled) {
        return disabled ? cairo_byte(BSH[4]) : cairo_byte(gtk2_mix(BSH[6], fill, 0.2));
    };
    // TOOL_BUTTON_STATES: hot (PRELIGHT), pressed (ACTIVE, active), hot and
    // checked (PRELIGHT, active), dead and checked (INSENSITIVE, active, insensitive)
    gummy_face(e, "cl_button_", "hot", geo.case_h, button_prelight, false, false,
               border_of(button_prelight, false));
    gummy_face(e, "cl_button_", "pressed", geo.case_h, button_active, true, false,
               border_of(button_active, false));
    gummy_face(e, "cl_button_", "hot_checked", geo.case_h, button_prelight, true, false,
               border_of(button_prelight, false));
    gummy_face(e, "cl_button_", "dead_checked", geo.case_h, button_insensitive, true, true,
               border_of(button_insensitive, true));

    // THE PUSH BUTTON (PUSH_BUTTON_STATES: normal, pressed, disabled)
    gummy_face(e, "cl_push_", "normal", geo.push_h, button_normal, false, false,
               border_of(button_normal, false));
    gummy_face(e, "cl_push_", "pressed", geo.push_h, button_active, true, false,
               border_of(button_active, false));
    gummy_face(e, "cl_push_", "disabled", geo.push_h, button_insensitive, false, true,
               border_of(button_insensitive, true));
    e.put({"cl_push_default_ring"}, cairo_byte(gtk2_mix(pbg, SP[1], 0.5)));
    e.put({"cl_push_normal_default_border"}, cairo_byte(gtk2_mix(SP[2], button_normal, 0.2)));
    e.put({"cl_push_pressed_default_border"}, cairo_byte(gtk2_mix(SP[2], button_active, 0.2)));
    e.put({"cl_push_text"}, gdk_byte(fg_normal16));   // the button style's fg[NORMAL] = the default's

    // THE ENTRY (clearlooks_gummy_draw_entry) and GtkEntry's text and selection
    const D3 base = unit(base_normal16);
    e.put({"cl_base"}, gdk_byte(base_normal16));
    e.put({"cl_text"}, gdk_byte(text_normal16));
    e.put({"cl_text_selected"}, gdk_byte(text_selected16));
    e.put({"cl_selection"}, gdk_byte(sel16));             // base[SELECTED]
    e.put({"cl_selection_unfocused"}, gdk_byte(base_active16));
    e.put({"cl_entry_border"}, cairo_byte(SH[6]));
    e.put({"cl_entry_shadow"}, cairo_solid_over(gtk2_shade(SH[6], 0.92), 0.18, cairo_byte(base)));
    e.put({"cl_entry_focus_border"}, cairo_byte(focus));
    e.put({"cl_entry_focus_ring"}, cairo_byte(gtk2_mix(base, gtk2_shade(focus, 1.61), 0.5)));

    // THE DROPDOWN (the "menu" style) and its lit item
    {
        const D3 menu_bg = unit(menu_bg16);
        e.put({"cl_menu_ground"}, gdk_byte(menu_bg16));
        e.put({"cl_menu_frame"}, cairo_byte(gtk2_shade(menu_bg, 0.665)));
        e.put({"cl_menu_separator"}, cairo_byte(gtk2_shade(menu_bg, 0.665)));
        e.put({"cl_menu_text"}, gdk_byte(fg_normal16));   // the menu_item style's fg[NORMAL]
        const int mi = geo.menu_item_h;
        const Stops g = gummy(SP[1], false);
        const auto row = [&](int r) { return pixman_vertical_ramp_row(g, 0, mi, r); };
        const int step = pixman_step_row(0, mi);
        e.ramp({"cl_menuitem_upper"}, row(1), row(step - 1));
        e.ramp({"cl_menuitem_lower"}, row(step), row(mi - 2));
        e.put({"cl_menuitem_border"}, cairo_byte(SP[2]));
        e.put({"cl_menuitem_text"}, gdk_byte(text_selected16));   // fg[PRELIGHT] = @selected_fg_color
    }

    // THE LIST (the scrolled window's line, the selected rows)
    {
        e.put({"cl_list_frame"}, cairo_byte(SH[5]));
        const int rh = geo.row_h;
        const int step = pixman_step_row(0, rh);
        const I3 fills[2] = {sel16, base_active16};   // base[SELECTED], base[ACTIVE]
        const std::string_view names[2] = {"cl_list_selected", "cl_list_selected_unfocused"};
        for (int k = 0; k < 2; ++k) {
            const Stops g = gummy(unit(fills[k]), false);
            const auto row = [&](int r) { return pixman_vertical_ramp_row(g, 0, rh, r); };
            e.ramp({names[k], "_upper"}, row(0), row(step - 1));
            e.ramp({names[k], "_lower"}, row(step), row(rh - 1));
        }
    }

    // THE TRIM LANE = GTK'S HORIZONTAL SCROLL BAR: the trough, the caps' pressed
    // face and arrow (the slider adds no tone)
    {
        e.put({"cl_trough_fill"}, cairo_byte(SH[2]));
        e.put({"cl_trough_border"}, cairo_byte(SH[5]));
        const Stops st = two_stops(gtk2_shade(SH[2], 0.95), SH[2]);
        e.ramp({"cl_trough_shadow"}, pixman_vertical_ramp_row(st, 1, 3, 1), pixman_vertical_ramp_row(st, 1, 3, 2));
        e.put({"cl_stepper_pressed_face"}, cairo_byte(unit(bg_active16)));   // the scrollbar style's bg[ACTIVE]
        e.put({"cl_stepper_arrow"}, gdk_byte(fg_normal16));
    }

    // THE POPUP LISTS' VERTICAL SCROLL BAR = squeeze's gummy GtkScrollbar
    // across geo.list_bar_w: the steppers (resting bg[NORMAL], pressed the
    // scrollbar style's bg[ACTIVE]) and the spot[1] slider
    {
        const int lw = geo.list_bar_w;
        const int step = pixman_step_row(0, lw);
        const D3 fills[2] = {bg, unit(bg_active16)};
        const std::string_view states[2] = {"_normal", "_pressed"};
        for (int k = 0; k < 2; ++k) {
            const Stops g = gummy(fills[k], false);
            const auto ramp = [&](int c) { return pixman_vertical_ramp_row(g, 0, lw, c); };
            const D3 hi = gtk2_shade(fills[k], 1.3);
            const auto over = [&](int c) { return cairo_solid_over(hi, 0.4, ramp(c)); };
            e.ramp({"cl_scrollbar_stepper", states[k], "_left"}, ramp(1), ramp(step - 1));
            e.ramp({"cl_scrollbar_stepper", states[k], "_right"}, ramp(step), ramp(lw - 2));
            e.put({"cl_scrollbar_stepper", states[k], "_border"}, cairo_byte(gtk2_mix(SH[7], fills[k], 0.2)));
            e.ramp({"cl_scrollbar_stepper", states[k], "_highlight_left"}, over(1), over(step - 1));
            e.ramp({"cl_scrollbar_stepper", states[k], "_highlight_right"}, over(step), over(lw - 2));
        }
        const D3 fill = SP[1];
        const Hsb hs = gtk2_hsb(fill), hb = gtk2_hsb(bg);
        D3 border = gtk2_shade(fill, dabs(hs.s - hb.s) < 0.30 && dabs(hs.l - hb.l) < 0.20 ? 0.475 : 0.575);
        if (hs.h > 25 && hs.h < 195) border = gtk2_shade(border, 0.85);
        const D3 handles = border;
        border = gtk2_mix(border, fill, 0.3);
        const Stops g = gummy(fill, false);
        const auto ramp = [&](int c) { return pixman_vertical_ramp_row(g, 1, lw - 2, c); };
        const D3 hi = gtk2_shade(fill, 1.3);
        const auto over = [&](int c) { return cairo_solid_over(hi, 0.2, ramp(c)); };
        const int sstep = pixman_step_row(1, lw - 2);
        e.ramp({"cl_scrollbar_slider_left"}, ramp(1), ramp(sstep - 1));
        e.ramp({"cl_scrollbar_slider_right"}, ramp(sstep), ramp(lw - 2));
        e.ramp({"cl_scrollbar_slider_highlight_left"}, over(1), over(sstep - 1));
        e.ramp({"cl_scrollbar_slider_highlight_right"}, over(sstep), over(lw - 2));
        e.put({"cl_scrollbar_slider_border"}, cairo_byte(border));
        e.put({"cl_scrollbar_slider_grip"}, cairo_byte(handles));
    }

    // THE SCRUB = GtkScale: the trough's two parts, the thumb's shadow, the thumb
    {
        const int st_ = geo.scale_trough;
        const D3 upper_fill = gtk2_shade(bg, 0.896);
        uint32_t upper_rows[2] = {}, upper_border = 0;
        const D3 fills[2] = {upper_fill, SP[1]};
        const D3 borders[2] = {SH[6], SP[2]};
        const double ks[2][2] = {{0.95, 1.05}, {1.1, 0.9}};
        const std::string_view names[2] = {"cl_scale_upper", "cl_scale_lower"};
        for (int k = 0; k < 2; ++k) {
            const Stops g = two_stops(gtk2_shade(fills[k], ks[k][0]), gtk2_shade(fills[k], ks[k][1]));
            const double y1 = st_ - 2 + 1.0;
            const uint32_t r0 = pixman_vertical_ramp_row(g, 0.5, y1, 2);
            const uint32_t r1 = pixman_vertical_ramp_row(g, 0.5, y1, st_ - 3);
            const uint32_t border = cairo_byte(gtk2_mix(borders[k], fills[k], 0.2));
            e.ramp({names[k]}, r0, r1);
            e.put({names[k], "_border"}, border);
            if (k == 0) { upper_rows[0] = r0; upper_rows[1] = r1; upper_border = border; }
        }
        const D3 sw = gtk2_shade(SH[6], 0.92);
        const auto shadow = [&](uint32_t dst) { return cairo_solid_over(sw, 0.1, dst); };
        e.put({"cl_scale_shadow"}, shadow(cairo_byte(bg)));
        e.put({"cl_scale_shadow_inset_dark"}, shadow(cairo_byte(gtk2_shade(bg, 0.94))));
        e.put({"cl_scale_shadow_border"}, shadow(upper_border));
        e.ramp({"cl_scale_shadow_ramp"}, shadow(upper_rows[0]), shadow(upper_rows[1]));
        e.put({"cl_scale_shadow_inset_light"}, shadow(cairo_byte(gtk2_shade(bg, 1.06))));
        const int sh_h = geo.scale_wid - 2;
        const Stops g = gummy(bg, false);
        const auto ramp = [&](int r) { return pixman_vertical_ramp_row(g, 1, sh_h - 2, r); };
        const int kstep = pixman_step_row(1, sh_h - 2);
        e.ramp({"cl_scale_thumb_upper"}, ramp(1), ramp(kstep - 1));
        e.ramp({"cl_scale_thumb_lower"}, ramp(kstep), ramp(sh_h - 2));
        e.put({"cl_scale_thumb_border"}, cairo_byte(gtk2_mix(SH[7], bg, 0.2)));
        e.put({"cl_scale_thumb_grip"}, cairo_byte(SH[7]));
        const D3 hi = gtk2_shade(bg, 1.3);
        const auto over = [&](int r) { return cairo_solid_over(hi, 0.4, ramp(r)); };
        e.put({"cl_scale_thumb_highlight_row"}, over(1));
        e.ramp({"cl_scale_thumb_highlight_upper"}, over(3), over(kstep - 1));
        e.ramp({"cl_scale_thumb_highlight_lower"}, over(kstep), over(sh_h - 4));
    }

    // THE RESTORED LAPTOP'S FRAME (round_bevel / round_bevel_unfocused) at
    // top_height = the sizing frame + the caption
    {
        const int TT = geo.frame_w + geo.caption_h;
        restored_frame(e, "focused", round_bevel(TT), kRoundBevelOps, focused);
        restored_frame(e, "unfocused", round_bevel_unfocused(TT), kRoundBevelUnfocusedOps, unfocused);
    }

    e.finish();
    return w;
}

// THE CHROME'S GEOMETRY, the derivation's lengths.
inline constexpr Geometry kGeometry = geometry_of(kChromeSpecClearlooks);

// -- THE PROOF ---------------------------------------------------------------------

// SQUEEZE'S OWN INPUTS (the gtkrc's gtk-color-scheme and the metacity
// theme's reads): bg_color EDECEB, fg_color 000000, base_color FFFFFF,
// text_color 1A1A1A, selected_bg_color 86ABD9, selected_fg_color FFFFFF;
// THE TITLE the focused ops' gtk:bg[SELECTED], 86ABD9 (its end ignored, any
// word), THE TITLE TEXT title_text's literal FFFFFF; THE INACTIVE TITLE the
// unfocused ops' gtk:bg[NORMAL], EDECEB (fed explicitly: with no block the
// compiled bytes stand, so the proof feeds the keys that reproduce the
// recorded reads, not the fallback), its text title_text_unfocused's blend
// as metacity rendered it, 6B6A6A.
inline constexpr GuiChromePick kSqueezePick{
    0xEDECEB, 0x000000, 0x86ABD9, 0x86ABD9, 0xFFFFFF,
    0xEDECEB, 0xEDECEB, 0x6B6A6A,
    0x86ABD9, 0xFFFFFF, 0xFFFFFF, 0x1A1A1A};
// THEY REPRODUCE THE COMPILED THEME, EVERY ROLE BYTE FOR BYTE.
static_assert(derive_clearlooks_chrome(kGuiThemeClearlooks, kSqueezePick, kGeometry) == kGuiThemeClearlooks,
              "the Clearlooks derivation no longer reproduces squeeze's recorded bytes "
              "(theme_clearlooks_values.inc): the port and build.py's engine_tones disagree");

} // namespace clearlooks_derive
