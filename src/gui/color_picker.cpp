#include "color_picker.h"

#include "clearlooks_paint.h"   // cl_scale_thumb_h_px (the thumb's grab height)
#include "notifications.h"
#include "playback_lifecycle.h"
#include "text_shape.h"
#include "viewport.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace color_picker {

// -- THE CHOOSER'S NAMES ---------------------------------------------------------

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
    {"warp_flag",                 "Warp Flag"},
    {"warp_flag_selected",        "Selected Warp Flag"},
    {"phase_reset_flag",          "Phase Reset Flag"},
    {"phase_reset_flag_selected", "Selected Phase Reset Flag"},
    {"added_flag",                "Added Flag"},
    {"added_flag_selected",       "Selected Added Flag"},
    {"removed_flag",              "Removed Flag"},
    {"removed_flag_selected",     "Selected Removed Flag"},
    {"flag_label",                "Flag Label"},
    {"playhead_stem",             "Playhead Stem"},
    {"scanner",                   "Scanner"},
};
static_assert(std::size(kRoleNames) == kGuiPaletteRoleCount);
constexpr bool role_names_follow_the_table() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        if (std::string_view(kRoleNames[i].role) != kGuiPaletteRoles[i].name)
            return false;
    return true;
}
static_assert(role_names_follow_the_table());
} // namespace

const char* role_display_name(std::size_t role) {
    assert(role < kGuiPaletteRoleCount);
    return kRoleNames[role].name;
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
// THE WHEEL'S SIDE IN W PX, the top block's height (the head's sums), and
// the ring's width from GTK's proportion, rounded to a whole Windows px and
// scaled like any length.
constexpr int kWheelSideWPx =
    kChooserHeightPx + kChooserGapPx + kChannelCount * kSliderRowPx;
static_assert(kWheelSideWPx == 113);
constexpr int kRingWidthWPx =
    (kWheelSideWPx * kRingWidthNum + kRingWidthDen / 2) / kRingWidthDen;
static_assert(kRingWidthWPx == 10);

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

Layout layout(const AppState& app, const GuiFont& font) {
    Layout l;
    const ChromeSpec& spec = live_chrome_spec();
    const bool cl = spec.vocabulary == GuiChromeVocabulary::Clearlooks;
    const int lw      = relief_line_px();
    const int edge    = cl ? lw : 2 * lw;
    const int pad     = scaled_px(kCardPadPx);
    const int margin  = scaled_px(kCardMarginPx);
    const int chooser_h = scaled_px(kChooserHeightPx);
    const int row_h   = scaled_px(kSliderRowPx, 1);
    const int btn_h   = scaled_px(spec.push_button_box_px);
    const int side    = chooser_h + scaled_px(kChooserGapPx) +
                        kChannelCount * row_h;
    const int inner_h = side + scaled_px(kBlockGapPx) + btn_h;
    const int card_w  = scaled_px(kCardWidthPx);
    const int card_h  = 2 * edge + 2 * pad + inner_h;

    const GuiRect well = waveform_area(app);
    l.edge_px = edge;
    l.card = GuiRect{app.color_picker.on_right ? app.width - margin - card_w
                                               : margin,
                     well.y + margin, card_w, card_h};
    l.inner = GuiRect{l.card.x + edge + pad, l.card.y + edge + pad,
                      card_w - 2 * (edge + pad), inner_h};

    // THE WHEEL, the left column whole.
    l.wheel   = GuiRect{l.inner.x, l.inner.y, side, side};
    l.cx      = l.wheel.x + side / 2.0;
    l.cy      = l.wheel.y + side / 2.0;
    l.outer_r = side / 2.0;
    l.inner_r = l.outer_r - scaled_px(kRingWidthWPx, 1);

    // THE RIGHT COLUMN.
    const int col_x = l.inner.x + side + scaled_px(kColumnGapPx);
    const int col_w = l.inner.x + l.inner.w - col_x;
    l.chooser = GuiRect{col_x, l.inner.y, col_w, chooser_h};
    if (!cl) {
        const int fb = 2 * lw;   // the sunken field's two lines
        const int bw = scaled_px(kComboButtonWPx);
        l.chooser_button = GuiRect{l.chooser.x + l.chooser.w - fb - bw,
                                   l.chooser.y + fb, bw, chooser_h - 2 * fb};
    }
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
    const int rows_y = l.inner.y + chooser_h + scaled_px(kChooserGapPx);
    for (int i = 0; i < kChannelCount; ++i) {
        const int ry = rows_y + i * row_h;
        l.slider_row[i]   = GuiRect{col_x, ry, col_w, row_h};
        l.slider_label[i] = GuiRect{col_x, ry, lab_w, row_h};
        const int tx = col_x + lab_w + gap_l;
        const int tw = std::max(1, col_w - lab_w - gap_l - gap_v - val_w);
        l.slider_track[i] = GuiRect{tx, ry, tw, row_h};
        l.slider_value[i] = GuiRect{tx + tw + gap_v, ry, val_w, row_h};
    }

    // THE BOTTOM ROW.
    const int by = l.inner.y + side + scaled_px(kBlockGapPx);
    const int field_h = scaled_px(spec.time_field_height_px);
    const int field_pad = scaled_px(spec.time_field_pad_px);
    const std::string hex_specimen = "#" + std::string(6, widest_hex_digit(font));
    const int cell_w = ceil_px(text_shape::shape_text_run(font, hex_specimen).width_px);
    const int field_w = cell_w + 2 * field_pad;
    l.hex_field = GuiRect{l.inner.x, by + (btn_h - field_h) / 2, field_w, field_h};
    l.hex_inner = GuiRect{l.hex_field.x + lw, l.hex_field.y + lw,
                          l.hex_field.w - 2 * lw, l.hex_field.h - 2 * lw};
    const int bw  = scaled_px(kPushButtonWidthPx);
    const int gap = scaled_px(kControlGapPx);
    const int close_x = l.inner.x + l.inner.w - bw;
    l.buttons[2] = GuiRect{close_x, by, bw, btn_h};
    l.buttons[1] = GuiRect{close_x - gap - bw, by, bw, btn_h};
    l.buttons[0] = GuiRect{close_x - 2 * (gap + bw), by, bw, btn_h};
    const int sf_x = l.hex_field.x + l.hex_field.w + gap;
    const int sf_w = std::max(2 * (scaled_px(kSwatchMinWPx) + lw),
                              l.buttons[0].x - gap - sf_x);
    l.swatch_frame = GuiRect{sf_x, by, sf_w, btn_h};
    const int sw_in_w = sf_w - 2 * lw;
    l.swatch_old = GuiRect{sf_x + lw, by + lw, sw_in_w / 2, btn_h - 2 * lw};
    l.swatch_new = GuiRect{l.swatch_old.x + l.swatch_old.w, by + lw,
                           sw_in_w - sw_in_w / 2, btn_h - 2 * lw};

    // THE LIST, when down: the dropdown's own arithmetic (dropdown_h_px and
    // paint_dropdown, render.h / paint_handler.cpp) under the chooser, flush,
    // the chooser's width.
    if (app.color_picker.chooser_open) {
        const bool gtk_menu = popup_is_gtk_menu();
        const int border    = popup_border_px();
        const int side_b    = gtk_menu ? 0 : border;
        const int item_h    = popup_item_h_px();
        const int block_mar = popup_item_margin_y_px();
        const int margin_x  = spec.popup_margin_px;
        const int inset     = scaled_px(margin_x, margin_x > 0 ? 1 : 0);
        const int count     = static_cast<int>(kGuiPaletteRoleCount);
        const int h = count * item_h + 2 * block_mar + popup_border_top_px() +
                      border;
        l.list = GuiRect{l.chooser.x, l.chooser.y + l.chooser.h, l.chooser.w, h};
        int iy = l.list.y + popup_border_top_px() + block_mar;
        for (int i = 0; i < count; ++i) {
            l.list_items[i] = GuiRect{l.list.x + side_b + inset, iy,
                                      l.list.w - 2 * (side_b + inset), item_h};
            iy += item_h;
        }
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
struct RingCache {
    cairo_surface_t* surf  = nullptr;
    int              side  = -1;
    int              ring  = -1;
};
struct TriangleCache {
    cairo_surface_t* surf  = nullptr;
    int              side  = -1;
    int              ring  = -1;
    double           hue   = -1.0;
};
RingCache     g_ring;
TriangleCache g_triangle;

cairo_surface_t* fresh_surface(int side) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
    return s;
}
inline uint32_t premultiplied(const Rgb& c, double cov) {
    const auto ch = [&](double u) {
        return static_cast<uint32_t>(
            std::clamp(std::nearbyint(u * cov * 255.0), 0.0, 255.0));
    };
    const uint32_t a = static_cast<uint32_t>(
        std::clamp(std::nearbyint(cov * 255.0), 0.0, 255.0));
    return (a << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}
inline double coverage(double d) { return std::clamp(d + 0.5, 0.0, 1.0); }

void render_ring(RingCache& c, int side, int ring) {
    if (c.surf != nullptr) cairo_surface_destroy(c.surf);
    c.surf = fresh_surface(side);
    c.side = side;
    c.ring = ring;
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
    if (g_ring.surf == nullptr || g_ring.side != side || g_ring.ring != ring)
        render_ring(g_ring, side, ring);
    if (g_triangle.surf == nullptr || g_triangle.side != side ||
        g_triangle.ring != ring || g_triangle.hue != hue_deg)
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
    cp.chooser_open    = false;
    cp.chooser_hover   = -1;
    cp.chooser_pressed = -1;
    cp.chooser_press_began_on_item = false;
    cp.drag = AppState::ColorPicker::Drag{};
    cp.stash = AppState::ColorPicker::Stash{};
    assert(cp.role < kGuiPaletteRoleCount);
    cp.rgb     = program_palette_words()[cp.role];
    cp.old_rgb = cp.rgb;
    reseat_memories(cp);
    // A modal OPEN damages the whole window (the card's rect does not exist
    // before its first paint — the settings opener carries the rule).
    viewport.invalidate_all();
}

void GuiColorPicker::close() {
    AppState::ColorPicker& cp = app.color_picker;
    if (!cp.active) return;
    if (hex_active()) text_editor::deactivate(cp.hex_editor);
    cp.active  = false;
    cp.session = 0;
    cp.chooser_open    = false;
    cp.chooser_hover   = -1;
    cp.chooser_pressed = -1;
    cp.chooser_press_began_on_item = false;
    cp.drag  = AppState::ColorPicker::Drag{};
    cp.stash = AppState::ColorPicker::Stash{};
    viewport.invalidate_all();
}

void GuiColorPicker::set_role(std::size_t role) {
    AppState::ColorPicker& cp = app.color_picker;
    assert(role < kGuiPaletteRoleCount);
    cp.role    = role;
    cp.rgb     = program_palette_words()[role];
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
    // THE LIVE APPLY (palette_file.h's install_program_palette states the
    // shape): the live words with one word rewritten, installed, the plate
    // rebuilt synchronously in its inks and the flag cache with it, the
    // whole window damaged — the swap is one frame, the card repainting
    // with the window.
    GuiPaletteWords words = program_palette_words();
    words[cp.role] = rgb;
    install_program_palette(words);
    viewport.kick_waveform_sync();
    viewport.invalidate_all();
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

void GuiColorPicker::hex_focus(int tap_x) {
    AppState::ColorPicker& cp = app.color_picker;
    if (!hex_active()) {
        text_editor::enter(cp.hex_editor, /*target=*/0,
                           color_picker::hex_spelling(cp.rgb),
                           text_editor::Kind::PaletteHex);
        // The whole text selected, the caret at its end (enter's seat).
        cp.hex_editor.selection_anchor = 0;
        damage_card();
        return;
    }
    if (!cp.stash.hex_byte_x.empty()) {
        cp.hex_editor.cursor_pos = text_editor::byte_index_from_shaped_x(
            static_cast<double>(tap_x), cp.stash.hex_text_origin_x,
            cp.stash.hex_byte_x);
        cp.hex_editor.selection_anchor = -1;
        text_editor::touch_blink(cp.hex_editor);
    }
    damage_card();
}

void GuiColorPicker::hex_commit() {
    AppState::ColorPicker& cp = app.color_picker;
    if (!hex_active()) return;
    const std::optional<uint32_t> rgb =
        color_picker::parse_hex_color(cp.hex_editor.pending);
    if (!rgb) {
        text_editor::refuse(cp.hex_editor);
        notifications.notify(AppState::NotificationClass::Normal,
                             "Not a color");
        damage_card();
        return;
    }
    text_editor::deactivate(cp.hex_editor);
    set_color(*rgb, false);
    damage_card();
}

void GuiColorPicker::hex_cancel() {
    AppState::ColorPicker& cp = app.color_picker;
    if (!hex_active()) return;
    text_editor::deactivate(cp.hex_editor);
    damage_card();
}

void GuiColorPicker::damage_card() {
    const AppState::ColorPicker::Stash& st = app.color_picker.stash;
    if (!st.valid || st.card.w <= 0) {
        viewport.invalidate_all();
        return;
    }
    viewport.invalidate_rect(st.card);
    if (st.list.w > 0 && st.list.h > 0) viewport.invalidate_rect(st.list);
}
