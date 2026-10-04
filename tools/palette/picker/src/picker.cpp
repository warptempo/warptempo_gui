#include "picker.h"

#include "fonts.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>

using namespace panel;

namespace {

constexpr Rgb kGround{0x19, 0x19, 0x19}, kLabel{0xFF, 0xFF, 0xFF}, kField{0x21, 0x21, 0x21}, kEdge{0, 0, 0};
constexpr Rgb kDim{0x55, 0x55, 0x55};   // a disabled button's glyph: a flat grey, no alpha
constexpr double kPi = 3.14159265358979323846;
constexpr double kNumPx = 38, kWordPx = 36, kHexPx = 84, kCornerPx = 32;
const char* const kRowNames[6] = {"H", "S", "V", "R", "G", "B"};

// ---------------------------------------------------------------- ColourState helpers
double clamp01(double x) { return std::max(0.0, std::min(1.0, x)); }

// ---------------------------------------------------------------- the frame's words
struct Frame {
    uint32_t* px;
    int stride, w, h;
    void fill(int x0, int y0, int x1, int y1, Rgb c) const {
        x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, w); y1 = std::min(y1, h);
        const uint32_t v = word_of(c);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) px[size_t(y) * stride + x] = v;
    }
    // a flat 1-px frame just outside x0..x1 x y0..y1
    void edge(int x0, int y0, int x1, int y1, Rgb c, int t = 1) const {
        fill(x0 - t, y0 - t, x1 + t, y0, c);
        fill(x0 - t, y1, x1 + t, y1 + t, c);
        fill(x0 - t, y0, x0, y1, c);
        fill(x1, y0, x1 + t, y1, c);
    }
    // a hollow marker centred at (cx, cy): `t` px of black outside `t` px of white, the inside left showing
    void marker(int cx, int cy, int half, int t) const {
        edge(cx - half + t, cy - half + t, cx + half - t, cy + half - t, kEdge, t);
        edge(cx - half + 2 * t, cy - half + 2 * t, cx + half - 2 * t, cy + half - 2 * t, kLabel, t);
    }
    void blit(const std::vector<uint32_t>& src, int size, int ox, int oy) const {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const uint32_t v = src[size_t(y) * size + x];
                if (v) px[size_t(oy + y) * stride + ox + x] = v;
            }
    }
};

void set_source(cairo_t* cr, Rgb c) { cairo_set_source_rgb(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0); }

// text with its baseline at y, starting at x (align 0), centred on x (align 1) or ending at x (align 2)
void text(cairo_t* cr, bool mono, double px, const std::string& s, double x, double y, int align, Rgb c = kLabel) {
    fonts_select(cr, mono, px);
    cairo_text_extents_t e;
    cairo_text_extents(cr, s.c_str(), &e);
    const double ox = align == 0 ? 0 : align == 1 ? -e.x_advance / 2 : -e.x_advance;
    set_source(cr, c);
    cairo_move_to(cr, std::round(x + ox), y);
    cairo_show_text(cr, s.c_str());
}

// the baseline that centres the face's cap height on a box (y0, h): the digits' and capitals' ink centred
double cap_baseline(cairo_t* cr, bool mono, double px, double y0, double h) {
    fonts_select(cr, mono, px);
    cairo_text_extents_t e;
    cairo_text_extents(cr, "H", &e);
    return std::round(y0 + (h - e.height) / 2 - e.y_bearing);
}

// the triangle's corners, relative to the wheel's centre: the pure hue, white, black (GTK's order, counter-clockwise)
void corners(double h, double v[3][2]) {
    for (int k = 0; k < 3; ++k) {
        const double a = (h + 120.0 * k) * kPi / 180.0;
        v[k][0] = kRTri * std::cos(a);
        v[k][1] = -kRTri * std::sin(a);
    }
}

// barycentric coordinates of p over the corners
void bary(const double v[3][2], double px, double py, double out[3]) {
    const double x0 = v[0][0], y0 = v[0][1], x1 = v[1][0], y1 = v[1][1], x2 = v[2][0], y2 = v[2][1];
    const double d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    out[0] = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / d;
    out[1] = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / d;
    out[2] = 1 - out[0] - out[1];
}

double row_value(const ColourState& cs, int row) {
    switch (row) {
        case 0: return cs.h / 360.0;
        case 1: return cs.s;
        case 2: return cs.v;
        case 3: return cs.rgb.r / 255.0;
        case 4: return cs.rgb.g / 255.0;
        default: return cs.rgb.b / 255.0;
    }
}

int row_number(const ColourState& cs, int row) {
    switch (row) {
        case 0: return int(std::nearbyint(cs.h));
        case 1: return int(std::nearbyint(cs.s * 100));
        case 2: return int(std::nearbyint(cs.v * 100));
        case 3: return cs.rgb.r;
        case 4: return cs.rgb.g;
        default: return cs.rgb.b;
    }
}

// the colour at fraction f along a row's track, the other channels as they stand
Rgb row_colour(const ColourState& cs, int row, double f) {
    switch (row) {
        case 0: return rgb_of_hsv(f * 360, cs.s, cs.v);
        case 1: return rgb_of_hsv(cs.h, f, cs.v);
        case 2: return rgb_of_hsv(cs.h, cs.s, f);
        case 3: return Rgb{byte_of_unit(f), cs.rgb.g, cs.rgb.b};
        case 4: return Rgb{cs.rgb.r, byte_of_unit(f), cs.rgb.b};
        default: return Rgb{cs.rgb.r, cs.rgb.g, byte_of_unit(f)};
    }
}

// the history's count as the panel and the corner label show it: "N of M", "0 of 0" with no history
std::string count_of(const History& h) {
    return std::to_string(h.cursor + 1) + " of " + std::to_string(h.picks.size());
}

bool in(double x, double y, double x0, double y0, double x1, double y1) { return x >= x0 && x < x1 && y >= y0 && y < y1; }
bool between(double v, double a, double b) { return v >= a && v < b; }

std::string now_iso8601() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
    const long off = tm.tm_gmtoff;
    char zone[32];
    std::snprintf(zone, sizeof zone, "%c%02ld:%02ld", off < 0 ? '-' : '+', std::labs(off) / 3600, (std::labs(off) % 3600) / 60);
    return std::string(buf) + zone;
}

} // namespace

// ---------------------------------------------------------------- ColourState
void ColourState::set_rgb(Rgb c) {
    rgb = c;
    const int mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    v = mx / 255.0;
    if (mx == mn) {                       // grey: no hue in the bytes, keep ours; black: keep the saturation too
        if (mx > 0) s = 0;
        return;
    }
    const double d = mx - mn;
    s = d / mx;
    double hh;
    if (mx == c.r) hh = (c.g - c.b) / d;
    else if (mx == c.g) hh = 2 + (c.b - c.r) / d;
    else hh = 4 + (c.r - c.g) / d;
    hh *= 60;
    if (hh < 0) hh += 360;
    h = hh;
}

void ColourState::set_hsv(double hh, double ss, double vv) {
    h = std::max(0.0, std::min(360.0, hh));
    s = clamp01(ss);
    v = clamp01(vv);
    rgb = rgb_of_hsv(h, s, v);
}

// ---------------------------------------------------------------- the launch state
bool picker_load(const std::string& data_dir, Scene& scene, History& hist, std::string& note, std::string& err) {
    std::map<std::string, Rgb> colours;
    std::map<std::string, int> entries;
    if (!state_load(data_dir + "/state.json", colours, entries, err)) return false;
    Layer& act = scene.layers[scene.active];
    hist = History{};
    if (!picks_load(data_dir + "/picks.txt", act.name, hist.picks, err)) return false;
    const auto c = colours.find(act.name);
    if (c != colours.end()) act.colour = c->second;   // the last close's, else the manifest's
    scene_derive(scene);
    note = c != colours.end() ? "state.json's colour" : "the manifest's colour";
    const int m = int(hist.picks.size());
    const auto e = entries.find(act.name);
    if (m == 0) {
        note += ", no history";
        return true;
    }
    if (e != entries.end() && e->second <= m && hist.picks[size_t(e->second - 1)] == act.colour) {
        hist.cursor = e->second - 1;
        note += ", state.json's entry";
        return true;
    }
    hist.cursor = m - 1;
    for (int k = m - 1; k >= 0; --k)
        if (hist.picks[size_t(k)] == act.colour) { hist.cursor = k; break; }
    note += hist.picks[size_t(hist.cursor)] == act.colour ? ", the newest entry equal to it" : ", the end (no entry equals it)";
    return true;
}

// ---------------------------------------------------------------- Picker
Picker::Picker(Scene scene, std::string data_dir, History hist)
    : scene_(std::move(scene)), data_dir_(std::move(data_dir)), hist_(std::move(hist)) {
    cs_.set_rgb(scene_.layers[scene_.active].colour);
    old_ = cs_.rgb;
    picture_.resize(size_t(scene_.width) * scene_.height);
    scene_paint(scene_, picture_.data());
    // the hue ring: every pixel whose centre lies between the radii, its hue the angle (0 = red at the right,
    // counter-clockwise), full saturation and value
    ring_.assign(size_t(kWheel) * kWheel, 0);
    for (int y = 0; y < kWheel; ++y)
        for (int x = 0; x < kWheel; ++x) {
            const double dx = x + 0.5 - kROut, dy = y + 0.5 - kROut, r = std::hypot(dx, dy);
            if (r < kRIn || r >= kROut) continue;
            double a = std::atan2(-dy, dx) * 180 / kPi;
            if (a < 0) a += 360;
            ring_[size_t(y) * kWheel + x] = word_of(rgb_of_hsv(a, 1, 1));
        }
}

double Picker::panel_x() const { return right_ ? scene_.width - kMargin - kW : kMargin; }
double Picker::panel_y() const { return (scene_.height - kH) / 2; }

void Picker::apply_colour() {
    Layer& a = scene_.layers[scene_.active];
    if (a.colour == cs_.rgb) { dirty_ = true; return; }
    a.colour = cs_.rgb;
    scene_derive(scene_);
    scene_paint_layers(scene_, picture_.data());
    dirty_ = true;
}

bool Picker::edited() const { return hist_.cursor < 0 || hist_.picks[size_t(hist_.cursor)] != cs_.rgb; }

bool Picker::back_enabled() const { return hist_.cursor > 0; }

bool Picker::forward_enabled() const { return hist_.cursor + 1 < int(hist_.picks.size()); }

void Picker::write_state() const {
    std::string js = "{\n \"colours\": {\n";
    for (size_t i = 0; i < scene_.layers.size(); ++i)
        js += "  \"" + scene_.layers[i].name + "\": \"" + hex_of(scene_.layers[i].colour) + "\"" +
              (i + 1 < scene_.layers.size() ? ",\n" : "\n");
    js += " },\n \"entry\": {";
    if (hist_.cursor >= 0) js += "\"" + scene_.layers[scene_.active].name + "\": " + std::to_string(hist_.cursor + 1);
    js += "}\n}\n";
    const std::string tmp = data_dir_ + "/state.json.part", dst = data_dir_ + "/state.json";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f || std::fputs(js.c_str(), f) < 0 || std::fclose(f) != 0 || std::rename(tmp.c_str(), dst.c_str()) != 0)
        plog("picker: cannot write %s", dst.c_str());
}

void Picker::commit() {
    const Layer& a = scene_.layers[scene_.active];
    const std::string hex = hex_of(a.colour);
    const std::string line = now_iso8601() + " " + a.name + " " + hex + "\n";
    if (FILE* f = std::fopen((data_dir_ + "/picks.txt").c_str(), "a")) {
        std::fputs(line.c_str(), f);
        std::fclose(f);
    } else {
        plog("picker: cannot append to %s/picks.txt", data_dir_.c_str());
    }
    hist_.picks.push_back(a.colour);
    hist_.cursor = int(hist_.picks.size()) - 1;
    write_state();
    plog("picker: commit %s %s", a.name.c_str(), hex.c_str());
}

void Picker::close() {
    open_ = false;
    target_ = Target::None;
    if (edited()) commit();
    else write_state();   // a colour reached by stepping, unchanged: nothing appended, the cursor recorded
    dirty_ = true;
}

void Picker::discard_if_open() {
    if (!open_) return;
    open_ = false;
    target_ = Target::None;
    hist_.cursor = open_cursor_;
    cs_.set_rgb(old_);
    apply_colour();
}

void Picker::history_step(int dir) {
    if (!(dir < 0 ? back_enabled() : forward_enabled())) return;
    hist_.cursor += dir;
    cs_.set_rgb(hist_.picks[size_t(hist_.cursor)]);
    apply_colour();
    const Layer& a = scene_.layers[scene_.active];
    plog("picker: step %s %s %s", a.name.c_str(), count_of(hist_).c_str(), hex_of(a.colour).c_str());
}

void Picker::ring_to(double x, double y) {
    const double dx = x - (panel_x() + kPad + kROut), dy = y - (panel_y() + kPad + kROut);
    double a = std::atan2(-dy, dx) * 180 / kPi;
    if (a < 0) a += 360;
    cs_.set_hsv(a, cs_.s, cs_.v);
    apply_colour();
}

void Picker::triangle_to(double x, double y) {
    const double px = x - (panel_x() + kPad + kROut), py = y - (panel_y() + kPad + kROut);
    double v[3][2], b[3];
    corners(cs_.h, v);
    bary(v, px, py, b);
    if (b[0] < 0 || b[1] < 0 || b[2] < 0) {   // outside: the nearest point of the triangle's edges
        double best = 1e300, bx = 0, by = 0;
        for (int k = 0; k < 3; ++k) {
            const double* p = v[k];
            const double* q = v[(k + 1) % 3];
            const double ex = q[0] - p[0], ey = q[1] - p[1];
            const double t = clamp01(((px - p[0]) * ex + (py - p[1]) * ey) / (ex * ex + ey * ey));
            const double cx = p[0] + t * ex, cy = p[1] + t * ey, d = std::hypot(px - cx, py - cy);
            if (d < best) { best = d; bx = cx; by = cy; }
        }
        bary(v, bx, by, b);
        for (double& c : b) c = std::max(0.0, c);
    }
    const double vv = clamp01(b[0] + b[1]);
    const double ss = vv > 0 ? clamp01(b[0] / vv) : cs_.s;
    cs_.set_hsv(cs_.h, ss, vv);
    apply_colour();
}

void Picker::track_to(int row, double x) {
    const double f = clamp01((x - (panel_x() + kTrackX)) / (kTrackL - 1));
    switch (row) {
        case 0: cs_.set_hsv(f * 360, cs_.s, cs_.v); break;
        case 1: cs_.set_hsv(cs_.h, f, cs_.v); break;
        case 2: cs_.set_hsv(cs_.h, cs_.s, f); break;
        default: {
            Rgb c = cs_.rgb;
            (row == 3 ? c.r : row == 4 ? c.g : c.b) = byte_of_unit(f);
            cs_.set_rgb(c);
        }
    }
    apply_colour();
}

void Picker::step(int row, int dir) {
    switch (row) {
        case 0: cs_.set_hsv(std::nearbyint(cs_.h) + dir, cs_.s, cs_.v); break;
        case 1: cs_.set_hsv(cs_.h, (std::nearbyint(cs_.s * 100) + dir) / 100, cs_.v); break;
        case 2: cs_.set_hsv(cs_.h, cs_.s, (std::nearbyint(cs_.v * 100) + dir) / 100); break;
        default: {
            Rgb c = cs_.rgb;
            uint8_t& ch = row == 3 ? c.r : row == 4 ? c.g : c.b;
            ch = uint8_t(std::max(0, std::min(255, ch + dir)));
            cs_.set_rgb(c);
        }
    }
    apply_colour();
}

void Picker::press(double x, double y) {
    down_x_ = x;
    down_y_ = y;
    row_ = -1;
    if (!open_) { target_ = Target::Picture; return; }
    const double ox = panel_x(), oy = panel_y(), lx = x - ox, ly = y - oy;
    if (!in(lx, ly, 0, 0, kW, kH)) { target_ = Target::Outside; return; }
    target_ = Target::None;
    const double r = std::hypot(lx - (kPad + kROut), ly - (kPad + kROut));
    if (r <= kROut + 20 && in(lx, ly, 0, 0, kColX - 10, kRowsY - 20)) {
        if (r >= kRIn - 4) { target_ = Target::Ring; ring_to(x, y); }
        else { target_ = Target::Triangle; triangle_to(x, y); }
        return;
    }
    if (in(lx, ly, kColX, kSwatchY0, kColX + kSwatchW, kSwatchY1)) { target_ = Target::Old; return; }
    if (between(ly, kHistY - 12, kHistY + kBtn + 12)) {   // a disabled button still holds the press: it does nothing
        if (between(lx, kBackX - 8, kBackX + kBtn + 8)) { target_ = Target::Back; return; }
        if (between(lx, kFwdX - 8, kFwdX + kBtn + 8)) { target_ = Target::Forward; return; }
    }
    for (int i = 0; i < 6; ++i) {
        const int ry = row_y(i);
        if (!between(ly, ry - 12, ry + kRowH + 12)) continue;
        row_ = i;
        if (between(lx, kMinusX - 8, kMinusX + kBtn + 6)) target_ = Target::Minus;
        else if (between(lx, kPlusX - 6, kPlusX + kBtn + 8)) target_ = Target::Plus;
        else if (between(lx, kTrackX - 20, kTrackX + kTrackL + 20)) { target_ = Target::Track; track_to(i, x); }
        return;
    }
}

void Picker::move(double x, double y) {
    switch (target_) {
        case Target::Ring: ring_to(x, y); break;
        case Target::Triangle: triangle_to(x, y); break;
        case Target::Track: track_to(row_, x); break;
        default: break;
    }
}

void Picker::release(double x, double y) {
    const double lx = x - panel_x(), ly = y - panel_y();
    switch (target_) {
        case Target::Picture:      // closed: the panel opens on the half opposite the tap
            open_ = true;
            right_ = down_x_ < scene_.width / 2.0;
            old_ = cs_.rgb;
            open_cursor_ = hist_.cursor;
            dirty_ = true;
            break;
        case Target::Outside:
            close();
            break;
        case Target::Old:
            if (in(lx, ly, kColX, kSwatchY0, kColX + kSwatchW, kSwatchY1)) { cs_.set_rgb(old_); apply_colour(); }
            break;
        case Target::Back:
        case Target::Forward: {
            const int bx = target_ == Target::Back ? kBackX : kFwdX;
            if (in(lx, ly, bx - 8, kHistY - 12, bx + kBtn + 8, kHistY + kBtn + 12)) history_step(target_ == Target::Back ? -1 : 1);
            break;
        }
        case Target::Minus:
        case Target::Plus: {
            const int bx = target_ == Target::Minus ? kMinusX : kPlusX, ry = row_y(row_);
            if (in(lx, ly, bx - 8, ry - 12, bx + kBtn + 8, ry + kRowH + 12)) step(row_, target_ == Target::Minus ? -1 : 1);
            break;
        }
        default: break;
    }
    target_ = Target::None;
}

void Picker::cancel() { target_ = Target::None; }

void Picker::paint(cairo_surface_t* surf) {
    cairo_surface_flush(surf);
    Frame fr{reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(surf)), cairo_image_surface_get_stride(surf) / 4,
             cairo_image_surface_get_width(surf), cairo_image_surface_get_height(surf)};
    for (int y = 0; y < fr.h; ++y)
        std::memcpy(fr.px + size_t(y) * fr.stride, picture_.data() + size_t(y) * scene_.width, size_t(fr.w) * 4);
    const Layer& act = scene_.layers[scene_.active];
    if (!open_) {   // the corner label: the active layer's name, hex and count, bottom right
        cairo_surface_mark_dirty(surf);
        cairo_t* cr = cairo_create(surf);
        const std::string s = act.name + " " + hex_of(act.colour) + "  " + count_of(hist_);
        fonts_select(cr, true, kCornerPx);
        cairo_text_extents_t e;
        cairo_text_extents(cr, s.c_str(), &e);
        const int bw = int(std::ceil(e.x_advance)) + 32, bh = 60;
        const int x0 = fr.w - 12 - bw, y0 = fr.h - 12 - bh;
        cairo_destroy(cr);
        fr.fill(x0, y0, x0 + bw, y0 + bh, kGround);
        fr.edge(x0, y0, x0 + bw, y0 + bh, kEdge);
        cairo_surface_mark_dirty(surf);
        cr = cairo_create(surf);
        text(cr, true, kCornerPx, s, x0 + 16, cap_baseline(cr, true, kCornerPx, y0, bh), 0);
        cairo_destroy(cr);
        cairo_surface_flush(surf);
        dirty_ = false;
        return;
    }
    const int ox = int(panel_x()), oy = int(panel_y());
    fr.fill(ox, oy, ox + kW, oy + kH, kGround);
    fr.edge(ox, oy, ox + kW, oy + kH, kEdge);
    // the wheel: the ring, the triangle at the hue, their markers
    const int wx = ox + kPad, wy = oy + kPad, cx = wx + kROut, cy = wy + kROut;
    fr.blit(ring_, kWheel, wx, wy);
    double v[3][2];
    corners(cs_.h, v);
    if (cs_.h != tri_h_) {
        tri_.assign(size_t(kWheel) * kWheel, 0);
        for (int y = 0; y < kWheel; ++y)
            for (int x = 0; x < kWheel; ++x) {
                double b[3];
                bary(v, x + 0.5 - kROut, y + 0.5 - kROut, b);
                if (b[0] < 0 || b[1] < 0 || b[2] < 0) continue;
                const double vv = clamp01(b[0] + b[1]);
                tri_[size_t(y) * kWheel + x] = word_of(rgb_of_hsv(cs_.h, vv > 0 ? clamp01(b[0] / vv) : 0, vv));
            }
        tri_h_ = cs_.h;
    }
    fr.blit(tri_, kWheel, wx, wy);
    const double ha = cs_.h * kPi / 180, rm = (kRIn + kROut) / 2.0;
    fr.marker(int(std::lround(cx + rm * std::cos(ha))), int(std::lround(cy - rm * std::sin(ha))), 14, 3);
    {
        const double a = cs_.s * cs_.v, b = cs_.v * (1 - cs_.s), c = 1 - cs_.v;
        const double px = a * v[0][0] + b * v[1][0] + c * v[2][0], py = a * v[0][1] + b * v[1][1] + c * v[2][1];
        fr.marker(int(std::lround(cx + px)), int(std::lround(cy + py)), 12, 3);
    }
    // the swatches: OLD | NEW
    fr.fill(ox + kColX, oy + kSwatchY0, ox + kColX + kSwatchW, oy + kSwatchY1, old_);
    fr.edge(ox + kColX, oy + kSwatchY0, ox + kColX + kSwatchW, oy + kSwatchY1, kEdge);
    fr.fill(ox + kNewX, oy + kSwatchY0, ox + kNewX + kSwatchW, oy + kSwatchY1, cs_.rgb);
    fr.edge(ox + kNewX, oy + kSwatchY0, ox + kNewX + kSwatchW, oy + kSwatchY1, kEdge);
    // the history: BACK and FORWARD, an arrow each (a solid head and the − / +'s 4-px shaft, 32 px across), dimmed
    // when disabled
    for (int dir : {-1, 1}) {
        const int bx = ox + (dir < 0 ? kBackX : kFwdX), by = oy + kHistY;
        fr.fill(bx, by, bx + kBtn, by + kBtn, kField);
        fr.edge(bx, by, bx + kBtn, by + kBtn, kEdge);
        const Rgb g = (dir < 0 ? back_enabled() : forward_enabled()) ? kLabel : kDim;
        const int mx = bx + kBtn / 2, my = by + kBtn / 2;
        for (int i = 0; i < 16; ++i) {   // the head: column i from the tip, 2 + 2i px tall
            const int x = dir < 0 ? mx - 16 + i : mx + 15 - i;
            fr.fill(x, my - 1 - i, x + 1, my + 1 + i, g);
        }
        if (dir < 0) fr.fill(mx, my - 2, mx + 16, my + 2, g);
        else fr.fill(mx - 16, my - 2, mx, my + 2, g);
    }
    // the sliders: the field behind each value and each button, the track's live gradient, the handle, the signs
    for (int i = 0; i < 6; ++i) {
        const int ry = oy + row_y(i), ty = ry + (kRowH - kTrackH) / 2;
        for (int bx : {kMinusX, kPlusX}) {
            fr.fill(ox + bx, ry, ox + bx + kBtn, ry + kRowH, kField);
            fr.edge(ox + bx, ry, ox + bx + kBtn, ry + kRowH, kEdge);
            const int mx = ox + bx + kBtn / 2, my = ry + kRowH / 2;
            fr.fill(mx - 15, my - 2, mx + 15, my + 2, kLabel);
            if (bx == kPlusX) fr.fill(mx - 2, my - 15, mx + 2, my + 15, kLabel);
        }
        fr.fill(ox + kFieldX, ry, ox + kFieldX + kFieldW, ry + kRowH, kField);
        fr.edge(ox + kFieldX, ry, ox + kFieldX + kFieldW, ry + kRowH, kEdge);
        for (int x = 0; x < kTrackL; ++x) {
            const uint32_t w = word_of(row_colour(cs_, i, double(x) / (kTrackL - 1)));
            for (int y = ty; y < ty + kTrackH; ++y) fr.px[size_t(y) * fr.stride + ox + kTrackX + x] = w;
        }
        fr.edge(ox + kTrackX, ty, ox + kTrackX + kTrackL, ty + kTrackH, kEdge);
        const int hx = ox + kTrackX + int(std::lround(row_value(cs_, i) * (kTrackL - 1)));
        fr.fill(hx - 6, ty - 10, hx + 7, ty + kTrackH + 10, kEdge);
        fr.fill(hx - 3, ty - 7, hx + 4, ty + kTrackH + 7, kLabel);
    }
    cairo_surface_mark_dirty(surf);
    cairo_t* cr = cairo_create(surf);
    text(cr, false, kWordPx, act.name, ox + kColX, oy + kPad + 34, 0);
    text(cr, true, kHexPx, hex_of(cs_.rgb), ox + kColX, oy + kPad + 150, 0);
    text(cr, true, kNumPx, count_of(hist_), ox + (kBackX + kBtn + kFwdX) / 2.0,
         cap_baseline(cr, true, kNumPx, oy + kHistY, kBtn), 1);
    text(cr, false, kWordPx, "Old", ox + kColX + kSwatchW / 2.0, oy + kSwatchY0 - 18, 1);
    text(cr, false, kWordPx, "New", ox + kNewX + kSwatchW / 2.0, oy + kSwatchY0 - 18, 1);
    for (int i = 0; i < 6; ++i) {
        const int ry = oy + row_y(i);
        text(cr, false, kWordPx, kRowNames[i], ox + kLabelX + 18, cap_baseline(cr, false, kWordPx, ry, kRowH), 1);
        text(cr, true, kNumPx, std::to_string(row_number(cs_, i)), ox + kFieldX + kFieldW - 14,
             cap_baseline(cr, true, kNumPx, ry, kRowH), 2);
    }
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    dirty_ = false;
}

void paint_message(cairo_surface_t* surf, const std::vector<std::string>& lines) {
    cairo_t* cr = cairo_create(surf);
    set_source(cr, kGround);
    cairo_paint(cr);
    double y = 120;
    for (const std::string& l : lines) {
        text(cr, false, 40, l, 80, y, 0);
        y += 64;
    }
    cairo_destroy(cr);
    cairo_surface_flush(surf);
}
