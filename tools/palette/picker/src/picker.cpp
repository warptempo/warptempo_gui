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
constexpr double kWordPx = 36, kHexPx = 84, kCornerPx = 32;
// the slider rows' letters: the model's three, then R, G, B
const char* row_name(Model m, int row) {
    static const char* const kNames[3][3] = {{"H", "S", "V"}, {"H", "S", "L"}, {"L", "C", "h"}};
    static const char* const kRgb[3] = {"R", "G", "B"};
    return row < 3 ? kNames[int(m)][row] : kRgb[row - 3];
}

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

// the largest size up to px at which s is no wider than width: the element button's name, which must clear the
// chooser's head at the button's right (the flags round's "Unselected Flag" is 256 px at 36 px where 186 fit), and a
// chooser entry's, which must end inside the list (kChooserNameX1)
double fit_px(cairo_t* cr, bool mono, double px, const std::string& s, double width) {
    fonts_select(cr, mono, px);
    cairo_text_extents_t e;
    cairo_text_extents(cr, s.c_str(), &e);
    return e.x_advance <= width ? px : std::floor(px * width / e.x_advance);
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
        case 0: case 1: case 2: return cs.view(cs.model)[size_t(row)] / axis_max(cs.model, row);
        case 3: return cs.rgb.r / 255.0;
        case 4: return cs.rgb.g / 255.0;
        default: return cs.rgb.b / 255.0;
    }
}

// the frame word at fraction f along a row's track, the other channels as they stand; an LCh colour outside the gamut
// is the flat neutral kGround (the track has no colour there: the stretch reads as a gap in it)
uint32_t row_word(const ColourState& cs, int row, double f) {
    if (row < 3) {
        std::array<double, 3> x = cs.view(cs.model);
        x[size_t(row)] = f * axis_max(cs.model, row);
        const Unit u = unit_of_view(cs.model, x.data());
        return cs.model == Model::Lch && !unit_in_gamut(u) ? word_of(kGround) : word_of(rgb_of_unit(u));
    }
    Rgb c = cs.rgb;
    (row == 3 ? c.r : row == 4 ? c.g : c.b) = byte_of_unit(f);
    return word_of(c);
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

// the words split at `sep` (kept on all but the last word, so a line reads as typed)
std::vector<std::string> split_words(const std::string& s, const std::string& sep) {
    std::vector<std::string> out;
    for (size_t b = 0;;) {
        const size_t e = s.find(sep, b);
        if (e == std::string::npos) { out.push_back(s.substr(b)); break; }
        out.push_back(s.substr(b, e - b + sep.size()));
        b = e + sep.size();
    }
    return out;
}

// the tokens laid into lines no wider than `width` (a token wider than a line stands alone on its own; the painter
// clips it), each line's trailing space dropped
std::vector<std::string> wrap(cairo_t* cr, bool mono, double px, const std::vector<std::string>& toks, double width) {
    fonts_select(cr, mono, px);
    auto w = [&](const std::string& t) {
        cairo_text_extents_t e;
        cairo_text_extents(cr, t.c_str(), &e);
        return e.x_advance;
    };
    auto trim = [](std::string t) { while (!t.empty() && t.back() == ' ') t.pop_back(); return t; };
    std::vector<std::string> lines;
    std::string line;
    for (const std::string& t : toks) {
        if (!line.empty() && w(trim(line + t)) > width) { lines.push_back(trim(line)); line.clear(); }
        line += t;
    }
    if (!line.empty()) lines.push_back(trim(line));
    return lines;
}

} // namespace

// ---------------------------------------------------------------- ColourState
std::array<double, 3> ColourState::view(Model m) const {
    if (m == Model::Hsv) return {h, s, v};
    const double* x = m == Model::Hsl ? hsl : lch;
    return {x[0], x[1], x[2]};
}

void ColourState::put(Model m, const std::array<double, 3>& x) {
    if (m == Model::Hsv) { h = x[0]; s = x[1]; v = x[2]; return; }
    double* d = m == Model::Hsl ? hsl : lch;
    for (int k = 0; k < 3; ++k) d[k] = x[size_t(k)];
}

void ColourState::derive(Model m, Model from) {
    const Rgb c = rgb;
    if (m == Model::Lch) {
        double x[3];
        lch_of_unit(Unit{c.r / 255.0, c.g / 255.0, c.b / 255.0}, x);
        lch[0] = x[0];
        lch[1] = x[1];
        if (x[1] != 0) lch[2] = x[2];     // C 0 (a grey): the hue stays
        return;
    }
    double* hue = m == Model::Hsv ? &h : &hsl[0];
    if (from != m && from != Model::Lch) *hue = from == Model::Hsv ? h : hsl[0];   // HSV's and HSL's hue are one hue
    const int mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    if (m == Model::Hsv) {
        v = mx / 255.0;
        if (mx == mn) {                   // grey: no hue in the bytes, keep ours; black: keep the saturation too
            if (mx > 0) s = 0;
            return;
        }
        s = double(mx - mn) / mx;
    } else {
        hsl[2] = (mx + mn) / 510.0;
        if (mx == mn) {                   // grey: keep the hue; black and white: keep the saturation too
            if (mx > 0 && mx < 255) hsl[1] = 0;
            return;
        }
        hsl[1] = (mx - mn) / (255.0 - std::abs(mx + mn - 255));
    }
    *hue = hue_of_bytes(c, mx, mn);
}

void ColourState::ring_follow() {
    if (model != Model::Hsl) { ring[0] = h; ring[1] = s; ring[2] = v; return; }
    // the HSL view's own HSV, the hue the same: V = L + S min(L, 1 - L), S = 2 (1 - L / V); black keeps the ring's S
    const double l = hsl[2], vv = l + hsl[1] * std::min(l, 1 - l);
    ring[0] = hsl[0];
    if (vv > 0) ring[1] = clamp01(2 * (1 - l / vv));
    ring[2] = clamp01(vv);
}

void ColourState::set_rgb(Rgb c) {
    rgb = c;
    dialled = model;
    derive(model, model);
    for (Model m : kAllModels)
        if (m != model) derive(m, model);
    ring_follow();
}

void ColourState::set_hsv(double hh, double ss, double vv) {
    put(Model::Hsv, {std::max(0.0, std::min(360.0, hh)), clamp01(ss), clamp01(vv)});
    rgb = rgb_of_hsv(h, s, v);
    dialled = Model::Hsv;
    derive(Model::Hsl, Model::Hsv);
    derive(Model::Lch, Model::Hsv);
    ring_follow();
}

void ColourState::set_view(std::array<double, 3> x) {
    for (int k = 0; k < 3; ++k) x[size_t(k)] = std::max(0.0, std::min(axis_max(model, k), x[size_t(k)]));
    if (x == view(model)) return;         // the same numbers: nothing moves (the dialled view stays the one stored)
    if (model == Model::Hsv) { set_hsv(x[0], x[1], x[2]); return; }
    put(model, x);
    rgb = rgb_of_view(model, x.data());
    dialled = model;
    for (Model m : kAllModels)
        if (m != model) derive(m, model);
    ring_follow();
}

void ColourState::move_axis(int axis, double target) {
    const size_t a = size_t(axis);
    target = std::max(0.0, std::min(axis_max(model, axis), target));
    std::array<double, 3> x = view(model);
    const double cur = x[a];
    x[a] = target;
    auto inside = [&](double t) { x[a] = t; return lch_in_gamut(x[0], x[1], x[2]); };
    if (model == Model::Lch && !inside(target)) {
        // THE GAMUT STOP: walk from the current value (inside) toward the target in steps of 0.01 unit to the first
        // value outside, then bisect between the last inside and it to 1e-9 unit; the last inside is the stop. A
        // stretch outside narrower than 0.01 unit can be stepped over (the stop is then inside, past it).
        const double dir = target > cur ? 1 : -1;
        double lo = cur, hi = target;
        for (long k = 1;; ++k) {
            const double t = cur + dir * 0.01 * double(k);
            if (dir * (t - target) >= 0) break;
            if (!inside(t)) { hi = t; break; }
            lo = t;
        }
        for (int i = 0; i < 64 && std::abs(hi - lo) > 1e-9; ++i) {
            const double mid = (lo + hi) / 2;
            (inside(mid) ? lo : hi) = mid;
        }
        // a stop within 1e-6 unit of the current value is the current value: a − / + or a drag against the edge where
        // the view already stands (itself a stop, within 1e-9 of the edge) moves nothing
        x[a] = std::abs(lo - cur) < 1e-6 ? cur : lo;
    } else {
        x[a] = target;
    }
    set_view(x);
}

void ColourState::set_ring(double hh, double ss, double vv) {
    hh = std::max(0.0, std::min(360.0, hh));
    ss = clamp01(ss);
    vv = clamp01(vv);
    if (model == Model::Hsv) { set_hsv(hh, ss, vv); return; }
    if (model == Model::Hsl) {
        // the pen's HSV as the HSL view, exactly: the same hue, L = V (1 - S / 2), S = (V - L) / min(L, 1 - L)
        // (black and white keep HSL's saturation)
        const double l = vv * (1 - ss / 2);
        const double sl = l > 0 && l < 1 ? clamp01((vv - l) / std::min(l, 1 - l)) : hsl[1];
        put(Model::Hsl, {hh, sl, clamp01(l)});
        rgb = rgb_of_hsl(hsl[0], hsl[1], hsl[2]);
        dialled = Model::Hsl;
        derive(Model::Hsv, Model::Hsl);
        derive(Model::Lch, Model::Hsl);
    } else {
        // the pen's HSV gives the bytes, and LCh reads them (its own view of them, the hue kept through grey)
        put(Model::Hsv, {hh, ss, vv});
        rgb = rgb_of_hsv(hh, ss, vv);
        dialled = Model::Lch;
        derive(Model::Lch, Model::Hsv);
        derive(Model::Hsl, Model::Hsv);
    }
    ring[0] = hh;   // the pen's own HSV: the triangle keeps its hue and the markers sit under the pen
    ring[1] = ss;
    ring[2] = vv;
}

void ColourState::show(Model m) {
    model = m;
    ring_follow();
}

void ColourState::restore(const Pick& p) {
    if (!p.has_view) { set_rgb(p.rgb); return; }
    rgb = p.rgb;
    put(p.model, {p.x[0], p.x[1], p.x[2]});
    dialled = p.model;
    for (Model m : kAllModels)
        if (m != p.model) derive(m, p.model);
    ring_follow();
}

Pick ColourState::pick() const {
    const std::array<double, 3> x = view(dialled);
    return Pick{rgb, true, dialled, {x[0], x[1], x[2]}};
}

bool ColourState::shows(const Pick& p) const {
    if (!(rgb == p.rgb)) return false;
    if (!p.has_view) return true;
    const std::array<double, 3> x = view(p.model);
    return x[0] == p.x[0] && x[1] == p.x[1] && x[2] == p.x[2];
}

std::string row_readout(const ColourState& cs, int row) {
    if (row >= 3) return std::to_string(row == 3 ? cs.rgb.r : row == 4 ? cs.rgb.g : cs.rgb.b);
    const double shown = cs.view(cs.model)[size_t(row)] * axis_scale(cs.model, row);   // degrees, percent, L, C
    const long tenths = long(std::nearbyint(shown * 10));                            // never negative: the ranges
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

// ---------------------------------------------------------------- the launch state
bool picker_load(const std::string& data_dir, Export& ex, Launch& out, std::string& err) {
    std::map<std::string, Pick> colours;
    std::map<std::string, int> entries;
    std::map<std::string, std::vector<Pick>> picks;
    std::string active, theme;
    Model model = Model::Hsv;
    if (!state_load(data_dir + "/state.json", colours, entries, active, theme, model, err)) return false;
    if (!picks_load(data_dir + "/picks.txt", picks, err)) return false;
    const int n = int(ex.elements.size());
    if (n > panel::kChooserMax) {   // the chooser's fit (picker.h): the panel holds no more rows
        err = "manifest.json: " + std::to_string(n) + " elements; the chooser holds " + std::to_string(panel::kChooserMax);
        return false;
    }
    out = Launch{};
    if (!presets_load(data_dir + "/presets.json", out.presets, err)) return false;
    out.theme = theme_of(ex, theme);   // no theme open, or one this export does not list: none
    out.model = model;
    out.hist.resize(size_t(n));
    out.start.resize(size_t(n));
    out.active = element_of(ex, active);
    if (out.active < 0) out.active = ex.active;   // no state.json's active, or one naming no element of this export
    for (int i = 0; i < n; ++i) {
        Element& el = ex.elements[size_t(i)];
        History& hist = out.hist[size_t(i)];
        Pick& start = out.start[size_t(i)];
        const auto pk = picks.find(el.key);
        if (pk != picks.end()) hist.picks = pk->second;
        const auto c = colours.find(el.key);
        start = c != colours.end() ? c->second : Pick{el.colour};   // the last close's, else the manifest's
        el.colour = start.rgb;
        std::string note = c == colours.end() ? "the manifest's colour" : start.has_view ? "state.json's colour and view" : "state.json's colour";
        // an entry holds the start: the same bytes and, when both carry a view, the same view (its model and numbers)
        auto holds = [&](int k) {
            const Pick& p = hist.picks[size_t(k)];
            return p.rgb == start.rgb && (!p.has_view || !start.has_view ||
                                          (p.model == start.model && p.x[0] == start.x[0] && p.x[1] == start.x[1] && p.x[2] == start.x[2]));
        };
        const int m = int(hist.picks.size());
        const auto e = entries.find(el.key);
        if (m == 0) note += ", no history";
        else if (e != entries.end() && e->second <= m && holds(e->second - 1)) {
            hist.cursor = e->second - 1;
            note += ", state.json's entry";
        } else {
            hist.cursor = m - 1;
            for (int k = m - 1; k >= 0; --k)
                if (holds(k)) { hist.cursor = k; break; }
            note += holds(hist.cursor) ? ", the newest entry holding it" : ", the end (no entry holds it)";
        }
        if (i == out.active) out.note = note;
    }
    return true;
}

// ---------------------------------------------------------------- Picker
Picker::Picker(Export ex, std::string data_dir, Launch launch)
    : ex_(std::move(ex)), data_dir_(std::move(data_dir)), active_(launch.active),
      presets_list_(std::move(launch.presets)), theme_(launch.theme) {
    el_.resize(ex_.elements.size());
    for (size_t i = 0; i < el_.size(); ++i) {
        el_[i].cs.model = launch.model;
        el_[i].cs.restore(launch.start[i]);
        el_[i].hist = std::move(launch.hist[i]);
        ex_.elements[i].colour = el_[i].cs.rgb;
    }
    old_ = cs();
    role_words(ex_, words_);
    picture_.resize(size_t(ex_.width) * ex_.height);
    shown_ = ex_.elements[size_t(active_)].scene;
    scene_paint(ex_, shown_, words_, picture_.data());
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
    if (theme_ >= 0) layout_strip();
}

double Picker::panel_x() const { return right_ ? ex_.width - kMargin - kW : kMargin; }
double Picker::panel_y() const { return (ex_.height - kH) / 2; }

void Picker::apply_colours() {
    dirty_ = true;
    uint32_t changed = 0;
    for (size_t i = 0; i < el_.size(); ++i)
        if (!(ex_.elements[i].colour == el_[i].cs.rgb)) {
            ex_.elements[i].colour = el_[i].cs.rgb;
            changed |= 1u << i;
        }
    if (!changed) return;
    old_words_ = words_;
    role_words(ex_, words_);
    scene_repaint(ex_, shown_, old_words_, words_, changed, picture_.data());
}

bool Picker::edited() const {
    const History& h = el_[size_t(active_)].hist;
    return h.cursor < 0 || !colour().shows(h.picks[size_t(h.cursor)]);
}

bool Picker::back_enabled() const { return history().cursor > 0; }

bool Picker::forward_enabled() const { return history().cursor + 1 < int(history().picks.size()); }

void Picker::write_state() const {
    const size_t n = el_.size();
    // the last saved state: the active element as the panel opened (OLD and its cursor) while the panel is open
    auto saved = [&](size_t i) -> const ColourState& { return open_ && int(i) == active_ ? old_ : el_[i].cs; };
    auto cursor = [&](size_t i) { return open_ && int(i) == active_ ? open_cursor_ : el_[i].hist.cursor; };
    std::string js = "{\n \"active\": \"" + ex_.elements[size_t(active_)].key + "\",\n \"model\": \"" +
                     model_word(model()) + "\",\n \"colours\": {\n";
    for (size_t i = 0; i < n; ++i)
        js += "  \"" + ex_.elements[i].key + "\": \"" + hex_of(saved(i).rgb) + "\"" + (i + 1 < n ? ",\n" : "\n");
    js += " },\n";
    for (Model m : kAllModels) {   // each element's view in its model's map; a map only when some view is in it
        std::string map;
        for (size_t i = 0; i < n; ++i) {
            const Pick p = saved(i).pick();
            if (p.model != m) continue;
            map += std::string(map.empty() ? "" : ",\n") + "  \"" + ex_.elements[i].key + "\": [" + view_number(p.x[0]) + ", " +
                   view_number(p.x[1]) + ", " + view_number(p.x[2]) + "]";
        }
        if (!map.empty()) js += std::string(" \"") + model_word(m) + "\": {\n" + map + "\n },\n";
    }
    js += " \"entry\": {";
    bool first = true;
    for (size_t i = 0; i < n; ++i)
        if (cursor(i) >= 0) {
            js += std::string(first ? "" : ", ") + "\"" + ex_.elements[i].key + "\": " + std::to_string(cursor(i) + 1);
            first = false;
        }
    js += "}";
    if (theme_ >= 0) js += ",\n \"theme\": \"" + ex_.themes[size_t(theme_)].key + "\"";
    js += "\n}\n";
    const std::string tmp = data_dir_ + "/state.json.part", dst = data_dir_ + "/state.json";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f || std::fputs(js.c_str(), f) < 0 || std::fclose(f) != 0 || std::rename(tmp.c_str(), dst.c_str()) != 0)
        plog("picker: cannot write %s", dst.c_str());
}

void Picker::append_pick(int ei, bool log) {
    const Element& e = ex_.elements[size_t(ei)];
    ElementState& es = el_[size_t(ei)];
    const Pick p = es.cs.pick();
    const std::string hex = hex_of(p.rgb);
    const std::string line = now_iso8601() + " " + e.key + " " + hex + " " + model_word(p.model) + " " + view_number(p.x[0]) +
                             " " + view_number(p.x[1]) + " " + view_number(p.x[2]) + "\n";
    if (FILE* f = std::fopen((data_dir_ + "/picks.txt").c_str(), "a")) {
        std::fputs(line.c_str(), f);
        std::fclose(f);
    } else {
        plog("picker: cannot append to %s/picks.txt", data_dir_.c_str());
    }
    es.hist.picks.push_back(p);
    es.hist.cursor = int(es.hist.picks.size()) - 1;
    if (log) plog("picker: commit %s %s", e.key.c_str(), hex.c_str());
}

void Picker::close() {
    open_ = false;
    chooser_ = false;
    models_ = false;
    presets_ = false;
    target_ = Target::None;
    if (edited()) append_pick(active_);   // else a colour reached by stepping, unchanged: nothing appended
    write_state();
    dirty_ = true;
}

void Picker::choose(int e) {
    chooser_ = false;
    dirty_ = true;
    if (e == active_) return;           // the active element again: nothing
    if (edited()) append_pick(active_);        // the close for the element left (the head's ruling)
    active_ = e;
    old_ = cs();
    open_cursor_ = hist().cursor;
    const int sc = ex_.elements[size_t(e)].scene;
    if (sc != shown_) {
        shown_ = sc;
        scene_paint(ex_, shown_, words_, picture_.data());
    }
    write_state();
    plog("picker: element %s %s %s", ex_.elements[size_t(e)].key.c_str(), hex_of(cs().rgb).c_str(), count_of(hist()).c_str());
}

void Picker::set_model(Model m) {
    models_ = false;
    dirty_ = true;
    if (m == model()) return;             // the shown model again: nothing
    for (ElementState& es : el_) es.cs.show(m);
    old_.show(m);
    write_state();
    plog("picker: model %s", model_name(m));
}

void Picker::discard_if_open() {
    if (!open_) return;
    open_ = false;
    chooser_ = false;
    models_ = false;
    presets_ = false;
    target_ = Target::None;
    hist().cursor = open_cursor_;
    cs() = old_;
    apply_colours();
}

void Picker::history_step(int dir) {
    if (!(dir < 0 ? back_enabled() : forward_enabled())) return;
    hist().cursor += dir;
    cs().restore(hist().picks[size_t(hist().cursor)]);
    apply_colours();
    const Element& e = ex_.elements[size_t(active_)];
    plog("picker: step %s %s %s", e.key.c_str(), count_of(hist()).c_str(), hex_of(e.colour).c_str());
}

// ---------------------------------------------------------------- the presets pop-up
std::string Picker::save_label() const {
    return "Save as Preset " + std::to_string(presets_list_.empty() ? 1 : presets_list_.back().number + 1);
}

void Picker::open_presets() {
    chooser_ = false;
    presets_ = true;
    dirty_ = true;
    if (edited()) {                    // the close for the panel's edit: commit it, the panel's opening is now it
        append_pick(active_);
        old_ = cs();
        open_cursor_ = hist().cursor;
        write_state();
    }
    pop_.pos = std::min(pop_.pos, pop_max());
}

void Picker::write_presets() const {
    std::string js = "{\n \"presets\": [";
    for (size_t k = 0; k < presets_list_.size(); ++k) {
        const Preset& p = presets_list_[k];
        js += std::string(k ? ",\n" : "\n") + "  {\"number\": " + std::to_string(p.number) + ", \"saved\": \"" + p.saved +
              "\",\n   \"colours\": {";
        std::string views[3];
        bool first = true;
        for (const auto& kv : p.colours) {   // the keys in the map's order; the reader takes them by name
            js += std::string(first ? "" : ", ") + "\"" + kv.first + "\": \"" + hex_of(kv.second.rgb) + "\"";
            std::string& map = views[int(kv.second.model)];
            map += std::string(map.empty() ? "" : ", ") + "\"" + kv.first + "\": [" + view_number(kv.second.x[0]) + ", " +
                   view_number(kv.second.x[1]) + ", " + view_number(kv.second.x[2]) + "]";
            first = false;
        }
        js += "}";
        for (Model m : kAllModels)   // each view in its model's map, a map only when some view is in it
            if (!views[int(m)].empty()) js += std::string(",\n   \"") + model_word(m) + "\": {" + views[int(m)] + "}";
        js += "}";
    }
    js += presets_list_.empty() ? "]\n}\n" : "\n ]\n}\n";
    const std::string tmp = data_dir_ + "/presets.json.part", dst = data_dir_ + "/presets.json";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f || std::fputs(js.c_str(), f) < 0 || std::fclose(f) != 0 || std::rename(tmp.c_str(), dst.c_str()) != 0)
        plog("picker: cannot write %s", dst.c_str());
}

void Picker::save_preset() {
    Preset p;
    p.number = presets_list_.empty() ? 1 : presets_list_.back().number + 1;
    p.saved = now_iso8601();
    std::string said;
    for (size_t i = 0; i < el_.size(); ++i) {
        p.colours[ex_.elements[i].key] = el_[i].cs.pick();
        said += " " + ex_.elements[i].key + " " + hex_of(el_[i].cs.rgb);
    }
    presets_list_.push_back(std::move(p));
    write_presets();
    plog("picker: preset save Preset %d:%s", presets_list_.back().number, said.c_str());
}

void Picker::load_preset(int i) {
    const Preset& p = presets_list_[size_t(i)];
    std::string moved, kept;
    for (size_t e = 0; e < el_.size(); ++e) {
        const std::string& key = ex_.elements[e].key;
        const auto c = p.colours.find(key);
        if (c == p.colours.end()) continue;               // an element the preset predates: it keeps its colour
        if (el_[e].cs.shows(c->second)) { kept += " " + key; continue; }
        el_[e].cs.restore(c->second);
        append_pick(int(e), false);                       // one committed pick; the load's line says it
        moved += " " + key + " " + hex_of(c->second.rgb);
    }
    apply_colours();
    old_ = cs();
    open_cursor_ = hist().cursor;
    write_state();
    plog("picker: preset load Preset %d: committed%s; unchanged%s", p.number, moved.empty() ? " nothing" : moved.c_str(),
         kept.empty() ? " nothing" : kept.c_str());
}

int Picker::pop_items() const {
    return int(presets_list_.size()) + (ex_.themes.empty() ? 0 : 1 + int(ex_.themes.size()));
}

int Picker::pop_max() const { return std::max(0, pop_items() * kPopRowH - (kPopY1 - kPopListY0)); }

int Picker::pop_item_at(double ly) const {
    if (ly < kPopListY0 || ly >= kPopY1) return -1;
    const int k = int(std::floor((ly - kPopListY0 + pop_.pos) / kPopRowH));
    return k < pop_items() ? k : -1;
}

void Picker::pop_act(int item) {
    const int np = int(presets_list_.size());
    if (item == np) return;                                // the heading: no entry
    presets_ = false;
    dirty_ = true;
    if (item < np) load_preset(item);
    else open_theme(item - np - 1);
}

void Picker::scroll_move(Scroll& sc, double y, int max) {
    if (!sc.dragging) {
        if (std::abs(y - down_y_) <= kSlop) return;
        sc.dragging = true;                                // past the slop: a scroll from here, no jump
        sc.anchor = down_y_ + (y > down_y_ ? kSlop : -kSlop);
        sc.start = sc.pos;
    }
    const int pos = std::max(0, std::min(max, int(std::lround(sc.start - (y - sc.anchor)))));
    if (pos != sc.pos) { sc.pos = pos; dirty_ = true; }
}

// ---------------------------------------------------------------- the theme strip
double Picker::strip_x() const { return right_ ? panel_x() - kStripGap - kStripW : panel_x() + kW + kStripGap; }
double Picker::strip_list_y0() const { return panel_y() + strip_head_; }
double Picker::strip_list_y1() const { return panel_y() + kH - kStripPad; }
double Picker::strip_row_y(int i) const { return strip_list_y0() + strip_rows_[size_t(i)].y - strip_.pos; }
int Picker::strip_max() const { return std::max(0, strip_content_ - int(strip_list_y1() - strip_list_y0())); }

int Picker::strip_row_at(double y) const {
    if (y < strip_list_y0() || y >= strip_list_y1()) return -1;
    const double cy = y - strip_list_y0() + strip_.pos;
    for (size_t i = 0; i < strip_rows_.size(); ++i)
        if (cy >= strip_rows_[i].y && cy < strip_rows_[i].y + strip_rows_[i].h) return int(i);
    return -1;
}

void Picker::layout_strip() {
    const Theme& t = ex_.themes[size_t(theme_)];
    cairo_surface_t* ms = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = cairo_create(ms);
    // the header: the theme's CATALOG KEY, the name he types in the app's Settings (wrapped at its hyphens), and its
    // display title under it, small (architect 2026-10-04)
    strip_title_ = wrap(cr, false, kStripTitlePx, split_words(t.key, "-"), kStripW - 2 * kStripPad - kBtn - 12);
    strip_sub_ = wrap(cr, false, kStripNamePx, split_words(t.name, " "), kStripW - 2 * kStripPad - kBtn - 12);
    strip_head_ = kStripPad + std::max(kBtn, int(std::ceil(strip_title_.size() * kStripTitleLineH +
                                                            strip_sub_.size() * kStripLineH))) + 16;
    strip_rows_.clear();
    int y = 0;
    for (const ThemeColour& c : t.colours) {
        std::vector<std::string> toks;
        for (size_t k = 0; k < c.names.size(); ++k) toks.push_back(c.names[k] + (k + 1 < c.names.size() ? ", " : ""));
        StripRow r;
        r.names = wrap(cr, false, kStripNamePx, toks, kStripW - kStripTextX - kStripPad);
        r.y = y;
        r.h = std::max(kStripSwH, int((1 + r.names.size()) * kStripLineH) + 6);
        y += r.h + kStripRowGap;
        strip_rows_.push_back(std::move(r));
    }
    strip_content_ = y - kStripRowGap;
    cairo_destroy(cr);
    cairo_surface_destroy(ms);
}

void Picker::open_theme(int t) {
    theme_ = t;
    strip_.pos = 0;
    layout_strip();
    dirty_ = true;
    write_state();
    plog("picker: theme open %s (%zu colours)", ex_.themes[size_t(t)].key.c_str(), ex_.themes[size_t(t)].colours.size());
}

void Picker::close_theme() {
    plog("picker: theme close %s", ex_.themes[size_t(theme_)].key.c_str());
    theme_ = -1;
    dirty_ = true;
    write_state();
}

void Picker::ring_to(double x, double y) {
    const double dx = x - (panel_x() + kPad + kROut), dy = y - (panel_y() + kPad + kROut);
    double a = std::atan2(-dy, dx) * 180 / kPi;
    if (a < 0) a += 360;
    cs().set_ring(a, cs().ring[1], cs().ring[2]);
    apply_colours();
}

void Picker::triangle_to(double x, double y) {
    const double px = x - (panel_x() + kPad + kROut), py = y - (panel_y() + kPad + kROut);
    double v[3][2], b[3];
    corners(cs().ring[0], v);
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
    const double ss = vv > 0 ? clamp01(b[0] / vv) : cs().ring[1];
    cs().set_ring(cs().ring[0], ss, vv);
    apply_colours();
}

void Picker::track_to(int row, double x) {
    const double f = clamp01((x - (panel_x() + kTrackX)) / (kTrackL - 1));
    if (row < 3) {
        cs().move_axis(row, f * axis_max(cs().model, row));
    } else {
        Rgb c = cs().rgb;
        (row == 3 ? c.r : row == 4 ? c.g : c.b) = byte_of_unit(f);
        cs().set_rgb(c);
    }
    apply_colours();
}

void Picker::step(int row, int dir) {
    if (row < 3) {   // one shown unit, from the rounded shown number
        const double sc = axis_scale(cs().model, row);
        cs().move_axis(row, (std::nearbyint(cs().view(cs().model)[size_t(row)] * sc) + dir) / sc);
    } else {
        Rgb c = cs().rgb;
        uint8_t& ch = row == 3 ? c.r : row == 4 ? c.g : c.b;
        ch = uint8_t(std::max(0, std::min(255, ch + dir)));
        cs().set_rgb(c);
    }
    apply_colours();
}

void Picker::press(double x, double y) {
    down_x_ = x;
    down_y_ = y;
    row_ = -1;
    item_ = -1;
    pop_.dragging = strip_.dragging = false;
    if (!open_) { target_ = Target::Picture; return; }
    const double ox = panel_x(), oy = panel_y(), lx = x - ox, ly = y - oy;
    if (chooser_) {   // the chooser is modal: a press on a row holds it, any other press is outside it
        const int n = int(ex_.elements.size());
        if (in(lx, ly, kColX, kChooserY, kColX1, kChooserY + n * kChooserRowH)) {
            target_ = Target::Row;
            row_ = int((ly - kChooserY) / kChooserRowH);
        } else {
            target_ = Target::OffChooser;
        }
        return;
    }
    if (models_) {    // the model switch's list is modal as the chooser is
        if (in(lx, ly, kModelX0, kModelListY, kModelX1, kModelListY + 3 * kChooserRowH)) {
            target_ = Target::ModelRow;
            row_ = int((ly - kModelListY) / kChooserRowH);
        } else {
            target_ = Target::OffModels;
        }
        return;
    }
    if (presets_) {   // the pop-up is modal as the chooser is
        if (in(lx, ly, kPopX0, kPopY0, kPopX1, kPopListY0)) target_ = Target::PopSave;
        else if (in(lx, ly, kPopX0, kPopListY0, kPopX1, kPopY1)) { target_ = Target::PopList; item_ = pop_item_at(ly); }
        else target_ = Target::OffPopup;
        return;
    }
    if (theme_ >= 0) {
        const double sx = strip_x(), sy = oy;
        if (in(x, y, sx, sy, sx + kStripW, sy + kH)) {
            target_ = Target::None;
            if (in(x - sx, y - sy, kStripW - kStripPad - kBtn - 8, kStripPad - 8, kStripW - kStripPad + 8, kStripPad + kBtn + 8))
                target_ = Target::StripClose;
            else if (y >= strip_list_y0() && y < strip_list_y1()) { target_ = Target::StripList; item_ = strip_row_at(y); }
            return;
        }
    }
    if (!in(lx, ly, 0, 0, kW, kH)) { target_ = Target::Outside; return; }
    target_ = Target::None;
    const double r = std::hypot(lx - (kPad + kROut), ly - (kPad + kROut));
    if (in(lx, ly, kModelX0, kModelY, kModelX1, kModelY + kModelH)) { target_ = Target::ModelBtn; return; }
    if (r <= kROut + 20 && in(lx, ly, 0, 0, kColX - 10, kModelY - 4)) {
        if (r >= kRIn - 4) { target_ = Target::Ring; ring_to(x, y); }
        else { target_ = Target::Triangle; triangle_to(x, y); }
        return;
    }
    if (in(lx, ly, kColX, kNameY, kNameX1, kNameY + kNameH)) { target_ = Target::Name; return; }
    if (in(lx, ly, kPresetsX, kNameY, kColX1, kNameY + kNameH)) { target_ = Target::Presets; return; }
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
        case Target::PopList: scroll_move(pop_, y, pop_max()); break;
        case Target::StripList: scroll_move(strip_, y, strip_max()); break;
        default: break;
    }
}

void Picker::release(double x, double y) {
    const double lx = x - panel_x(), ly = y - panel_y();
    switch (target_) {
        case Target::Picture:      // closed: the panel opens on the half opposite the tap
            open_ = true;
            right_ = down_x_ < ex_.width / 2.0;
            old_ = cs();
            open_cursor_ = hist().cursor;
            dirty_ = true;
            break;
        case Target::Outside:
            close();
            break;
        case Target::Name:         // the element button: the chooser opens at the lift
            if (in(lx, ly, kColX, kNameY, kNameX1, kNameY + kNameH)) { chooser_ = true; dirty_ = true; }
            break;
        case Target::Presets:      // the presets button: the pop-up opens at the lift
            if (in(lx, ly, kPresetsX, kNameY, kColX1, kNameY + kNameH)) open_presets();
            break;
        case Target::Row:          // an entry, lifted on the same entry: chosen
            if (in(lx, ly, kColX, kChooserY + row_ * kChooserRowH, kColX1, kChooserY + (row_ + 1) * kChooserRowH)) choose(row_);
            break;
        case Target::OffChooser:   // a tap outside the chooser: it closes, nothing else
            chooser_ = false;
            dirty_ = true;
            break;
        case Target::ModelBtn:     // the model switch: its list opens at the lift
            if (in(lx, ly, kModelX0, kModelY, kModelX1, kModelY + kModelH)) { models_ = true; dirty_ = true; }
            break;
        case Target::ModelRow:     // a model, lifted on the same entry: chosen
            if (in(lx, ly, kModelX0, kModelListY + row_ * kChooserRowH, kModelX1, kModelListY + (row_ + 1) * kChooserRowH))
                set_model(kAllModels[row_]);
            break;
        case Target::OffModels:    // a tap outside the list: it closes, nothing else
            models_ = false;
            dirty_ = true;
            break;
        case Target::PopSave:      // the save line, lifted on it: saved, the pop-up closed
            if (in(lx, ly, kPopX0, kPopY0, kPopX1, kPopListY0)) {
                save_preset();
                presets_ = false;
                dirty_ = true;
            }
            break;
        case Target::PopList:      // an entry tapped (no scroll), lifted on the same entry: it acts
            if (!pop_.dragging && item_ >= 0 && in(lx, ly, kPopX0, kPopListY0, kPopX1, kPopY1) && pop_item_at(ly) == item_)
                pop_act(item_);
            break;
        case Target::OffPopup:     // a tap outside the pop-up: it closes, nothing else
            presets_ = false;
            dirty_ = true;
            break;
        case Target::StripClose: {
            const double sx = strip_x(), sy = panel_y();
            if (in(x - sx, y - sy, kStripW - kStripPad - kBtn - 8, kStripPad - 8, kStripW - kStripPad + 8, kStripPad + kBtn + 8))
                close_theme();
            break;
        }
        case Target::StripList:    // a swatch tapped (no scroll), lifted on the same row: adopted, an edit
            if (!strip_.dragging && item_ >= 0 && in(x, y, strip_x(), strip_list_y0(), strip_x() + kStripW, strip_list_y1()) &&
                strip_row_at(y) == item_) {
                cs().set_rgb(ex_.themes[size_t(theme_)].colours[size_t(item_)].rgb);
                apply_colours();
            }
            break;
        case Target::Old:
            if (in(lx, ly, kColX, kSwatchY0, kColX + kSwatchW, kSwatchY1)) { cs() = old_; apply_colours(); }
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
    pop_.dragging = strip_.dragging = false;
}

void Picker::cancel() {
    target_ = Target::None;
    pop_.dragging = strip_.dragging = false;
}

void Picker::paint(cairo_surface_t* surf) {
    cairo_surface_flush(surf);
    Frame fr{reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(surf)), cairo_image_surface_get_stride(surf) / 4,
             cairo_image_surface_get_width(surf), cairo_image_surface_get_height(surf)};
    for (int y = 0; y < fr.h; ++y)
        std::memcpy(fr.px + size_t(y) * fr.stride, picture_.data() + size_t(y) * ex_.width, size_t(fr.w) * 4);
    const Element& act = ex_.elements[size_t(active_)];
    const ColourState& c = cs();
    if (!open_) {   // the corner label: the active element's name, hex and count, bottom right
        cairo_surface_mark_dirty(surf);
        cairo_t* cr = cairo_create(surf);
        const std::string s = act.name + " " + hex_of(c.rgb) + "  " + count_of(hist());
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
    // the element button: a field with the name and, at its right, a down-pointing head (the chooser below it); the
    // presets button beside it
    fr.fill(ox + kColX, oy + kNameY, ox + kNameX1, oy + kNameY + kNameH, kField);
    fr.edge(ox + kColX, oy + kNameY, ox + kNameX1, oy + kNameY + kNameH, kEdge);
    {
        const int mx = ox + kNameX1 - 36, my = oy + kNameY + kNameH / 2 - 6;
        for (int i = 0; i < 12; ++i) fr.fill(mx - 1 - i, my + 11 - i, mx + 1 + i, my + 12 - i, kLabel);   // the head, 24 px across
    }
    fr.fill(ox + kPresetsX, oy + kNameY, ox + kColX1, oy + kNameY + kNameH, kField);
    fr.edge(ox + kPresetsX, oy + kNameY, ox + kColX1, oy + kNameY + kNameH, kEdge);
    const int n = int(ex_.elements.size());
    const int np = int(presets_list_.size());
    if (!presets_) {
        // the wheel: the ring, the triangle at the ring's hue, their markers (the ring's HSV, every model)
        const int wx = ox + kPad, wy = oy + kPad, cx = wx + kROut, cy = wy + kROut;
        const double rh = c.ring[0], rs = c.ring[1], rv = c.ring[2];
        fr.blit(ring_, kWheel, wx, wy);
        double v[3][2];
        corners(rh, v);
        if (rh != tri_h_) {
            tri_.assign(size_t(kWheel) * kWheel, 0);
            for (int y = 0; y < kWheel; ++y)
                for (int x = 0; x < kWheel; ++x) {
                    double b[3];
                    bary(v, x + 0.5 - kROut, y + 0.5 - kROut, b);
                    if (b[0] < 0 || b[1] < 0 || b[2] < 0) continue;
                    const double vv = clamp01(b[0] + b[1]);
                    tri_[size_t(y) * kWheel + x] = word_of(rgb_of_hsv(rh, vv > 0 ? clamp01(b[0] / vv) : 0, vv));
                }
            tri_h_ = rh;
        }
        fr.blit(tri_, kWheel, wx, wy);
        const double ha = rh * kPi / 180, rm = (kRIn + kROut) / 2.0;
        fr.marker(int(std::lround(cx + rm * std::cos(ha))), int(std::lround(cy - rm * std::sin(ha))), 14, 3);
        {
            const double a = rs * rv, b = rv * (1 - rs), cc = 1 - rv;
            const double px = a * v[0][0] + b * v[1][0] + cc * v[2][0], py = a * v[0][1] + b * v[1][1] + cc * v[2][1];
            fr.marker(int(std::lround(cx + px)), int(std::lround(cy + py)), 12, 3);
        }
        // the swatches: OLD | NEW
        fr.fill(ox + kColX, oy + kSwatchY0, ox + kColX + kSwatchW, oy + kSwatchY1, old_.rgb);
        fr.edge(ox + kColX, oy + kSwatchY0, ox + kColX + kSwatchW, oy + kSwatchY1, kEdge);
        fr.fill(ox + kNewX, oy + kSwatchY0, ox + kNewX + kSwatchW, oy + kSwatchY1, c.rgb);
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
                const uint32_t w = row_word(c, i, double(x) / (kTrackL - 1));
                for (int y = ty; y < ty + kTrackH; ++y) fr.px[size_t(y) * fr.stride + ox + kTrackX + x] = w;
            }
            fr.edge(ox + kTrackX, ty, ox + kTrackX + kTrackL, ty + kTrackH, kEdge);
            const int hx = ox + kTrackX + int(std::lround(row_value(c, i) * (kTrackL - 1)));
            fr.fill(hx - 6, ty - 10, hx + 7, ty + kTrackH + 10, kEdge);
            fr.fill(hx - 3, ty - 7, hx + 4, ty + kTrackH + 7, kLabel);
        }
        // the chooser, over the column and the slider rows it runs down over (painted after them, so their fields,
        // tracks and handles are under it): a field per element in manifest order, the active one's marked by a square
        if (chooser_) {
            const int y0 = oy + kChooserY, y1 = y0 + n * kChooserRowH;
            fr.fill(ox + kColX, y0, ox + kColX1, y1, kField);
            fr.edge(ox + kColX, y0, ox + kColX1, y1, kEdge);
            for (int i = 1; i < n; ++i) fr.fill(ox + kColX, y0 + i * kChooserRowH, ox + kColX1, y0 + i * kChooserRowH + 1, kEdge);
            const int my = y0 + active_ * kChooserRowH + kChooserRowH / 2;
            fr.fill(ox + kColX + 22, my - 8, ox + kColX + 38, my + 8, kLabel);
        }
        // the model switch: a field with the model's name and the chooser's head; its list under it when open, the
        // shown model's entry marked as the chooser marks the active element
        fr.fill(ox + kModelX0, oy + kModelY, ox + kModelX1, oy + kModelY + kModelH, kField);
        fr.edge(ox + kModelX0, oy + kModelY, ox + kModelX1, oy + kModelY + kModelH, kEdge);
        {
            const int mx = ox + kModelX1 - 36, my = oy + kModelY + kModelH / 2 - 6;
            for (int i = 0; i < 12; ++i) fr.fill(mx - 1 - i, my + 11 - i, mx + 1 + i, my + 12 - i, kLabel);
        }
        if (models_) {
            const int x0 = ox + kModelX0, x1 = ox + kModelX1, y0 = oy + kModelListY, y1 = y0 + 3 * kChooserRowH;
            fr.fill(x0, y0, x1, y1, kField);
            fr.edge(x0, y0, x1, y1, kEdge);
            for (int i = 1; i < 3; ++i) fr.fill(x0, y0 + i * kChooserRowH, x1, y0 + i * kChooserRowH + 1, kEdge);
            const int my = y0 + int(c.model) * kChooserRowH + kChooserRowH / 2;
            fr.fill(x0 + 22, my - 8, x0 + 38, my + 8, kLabel);
        }
    } else {
        // THE PRESETS POP-UP over the panel: the save line, then the list clipped to its viewport (the presets with a
        // swatch per element, the heading on the ground, the themes with their ground's swatch), rows ruled as the
        // chooser's
        const int x0 = ox + kPopX0, x1 = ox + kPopX1, vy0 = oy + kPopListY0, vy1 = oy + kPopY1;
        fr.fill(x0, oy + kPopY0, x1, vy1, kField);
        fr.edge(x0, oy + kPopY0, x1, vy1, kEdge);
        fr.fill(x0, vy0 - 1, x1, vy0 + 1, kEdge);      // the save line's rule, 2 px: the fixed line apart from the list
        auto clip_fill = [&](int a0, int b0, int a1, int b1, Rgb col) { fr.fill(a0, std::max(b0, vy0 + 1), a1, std::min(b1, vy1), col); };
        auto swatch = [&](int sx, int sy, Rgb col) {
            clip_fill(sx, sy, sx + kPopSw, sy + kPopSw, col);
            clip_fill(sx - 1, sy - 1, sx + kPopSw + 1, sy, kEdge);
            clip_fill(sx - 1, sy + kPopSw, sx + kPopSw + 1, sy + kPopSw + 1, kEdge);
            clip_fill(sx - 1, sy, sx, sy + kPopSw, kEdge);
            clip_fill(sx + kPopSw, sy, sx + kPopSw + 1, sy + kPopSw, kEdge);
        };
        for (int k = 0; k < pop_items(); ++k) {
            const int ry = vy0 + k * kPopRowH - pop_.pos;
            if (ry + kPopRowH <= vy0 || ry >= vy1) continue;
            if (k > 0) clip_fill(x0, ry, x1, ry + 1, kEdge);
            const int sy = ry + (kPopRowH - kPopSw) / 2;
            if (k < np) {
                std::vector<Rgb> cols;
                for (const Element& e : ex_.elements) {
                    const auto f = presets_list_[size_t(k)].colours.find(e.key);
                    if (f != presets_list_[size_t(k)].colours.end()) cols.push_back(f->second.rgb);
                }
                int sx = x1 - kPopSwInset - int(cols.size()) * kPopSw - (int(cols.size()) - 1) * kPopSwGap;
                for (const Rgb& col : cols) { swatch(sx, sy, col); sx += kPopSw + kPopSwGap; }
            } else if (k == np) {
                clip_fill(x0, ry + 1, x1, ry + kPopRowH, kGround);
            } else {
                swatch(x1 - kPopSwInset - kPopSw, sy, ex_.themes[size_t(k - np - 1)].ground);
            }
        }
    }
    // THE THEME STRIP, beside the panel on the scene's side: its name, its close control, its colours
    const int sx = int(strip_x()), lv0 = int(strip_list_y0()), lv1 = int(strip_list_y1());
    if (theme_ >= 0) {
        fr.fill(sx, oy, sx + kStripW, oy + kH, kGround);
        fr.edge(sx, oy, sx + kStripW, oy + kH, kEdge);
        const int bx = sx + kStripW - kStripPad - kBtn, by = oy + kStripPad;
        fr.fill(bx, by, bx + kBtn, by + kBtn, kField);
        fr.edge(bx, by, bx + kBtn, by + kBtn, kEdge);
        for (int i = -14; i < 14; ++i) {               // the close glyph: two 4-px strokes crossing, 32 px across
            const int mx = bx + kBtn / 2, my = by + kBtn / 2;
            fr.fill(mx + i - 2, my + i - 2, mx + i + 2, my + i + 2, kLabel);
            fr.fill(mx + i - 2, my - i - 3, mx + i + 2, my - i + 1, kLabel);
        }
        fr.fill(sx + kStripPad, lv0 - 9, sx + kStripW - kStripPad, lv0 - 8, kEdge);   // the header's rule
        const Theme& t = ex_.themes[size_t(theme_)];
        for (size_t i = 0; i < strip_rows_.size(); ++i) {
            const int ry = lv0 + strip_rows_[i].y - strip_.pos;
            if (ry + strip_rows_[i].h <= lv0 || ry >= lv1) continue;
            const int a0 = sx + kStripPad, b0 = ry;
            auto clip = [&](int p0, int q0, int p1, int q1, Rgb col) { fr.fill(p0, std::max(q0, lv0), p1, std::min(q1, lv1), col); };
            clip(a0, b0, a0 + kStripSwW, b0 + kStripSwH, t.colours[i].rgb);
            clip(a0 - 1, b0 - 1, a0 + kStripSwW + 1, b0, kEdge);
            clip(a0 - 1, b0 + kStripSwH, a0 + kStripSwW + 1, b0 + kStripSwH + 1, kEdge);
            clip(a0 - 1, b0, a0, b0 + kStripSwH, kEdge);
            clip(a0 + kStripSwW, b0, a0 + kStripSwW + 1, b0 + kStripSwH, kEdge);
            if (t.colours[i].rgb == c.rgb) {           // the active element shows this colour: marked
                const int mx = a0 + kStripSwW / 2, my = b0 + kStripSwH / 2;
                if (my - 12 >= lv0 && my + 12 <= lv1) fr.marker(mx, my, 12, 3);
            }
        }
    }
    cairo_surface_mark_dirty(surf);
    cairo_t* cr = cairo_create(surf);
    {   // the name in the word size, or smaller to end 12 px short of the head (kNameX1 - 36 - 12)
        const double npx = fit_px(cr, false, kWordPx, act.name, kNameX1 - 60 - (kColX + 18));
        text(cr, false, npx, act.name, ox + kColX + 18, cap_baseline(cr, false, npx, oy + kNameY, kNameH), 0);
    }
    text(cr, false, kWordPx, "Presets", ox + (kPresetsX + kColX1) / 2.0, cap_baseline(cr, false, kWordPx, oy + kNameY, kNameH), 1);
    if (!presets_) {
        // THE CHOOSER IS PAINTED OVER EVERYTHING IT COVERS: its field and rules went over the panel's fills above, and
        // the panel's words are clipped to outside it (the hex, the count, Old and New, a readout of a slider row it
        // runs over), its own names then painted inside it
        if (chooser_) {
            cairo_save(cr);
            cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
            cairo_rectangle(cr, 0, 0, fr.w, fr.h);
            cairo_rectangle(cr, ox + kColX, oy + kChooserY, kColX1 - kColX, n * kChooserRowH);
            cairo_clip(cr);
        }
        text(cr, true, kHexPx, hex_of(c.rgb), ox + kColX, oy + kPad + 150, 0);
        text(cr, true, kNumPx, count_of(hist()), ox + (kBackX + kBtn + kFwdX) / 2.0,
             cap_baseline(cr, true, kNumPx, oy + kHistY, kBtn), 1);
        text(cr, false, kWordPx, "Old", ox + kColX + kSwatchW / 2.0, oy + kSwatchY0 - 18, 1);
        text(cr, false, kWordPx, "New", ox + kNewX + kSwatchW / 2.0, oy + kSwatchY0 - 18, 1);
        text(cr, false, kWordPx, model_name(c.model), ox + kModelX0 + 18, cap_baseline(cr, false, kWordPx, oy + kModelY, kModelH), 0);
        if (models_)
            for (Model m : kAllModels) {
                const int ry = oy + kModelListY + int(m) * kChooserRowH;
                text(cr, false, kWordPx, model_name(m), ox + kModelX0 + 60, cap_baseline(cr, false, kWordPx, ry, kChooserRowH), 0);
            }
        for (int i = 0; i < 6; ++i) {
            const int ry = oy + row_y(i);
            if (!models_ || row_y(i) + kRowH / 2 >= kModelListY + 3 * kChooserRowH)   // the letters the list does not cover
                text(cr, false, kWordPx, row_name(c.model, i), ox + kLabelX + 18, cap_baseline(cr, false, kWordPx, ry, kRowH), 1);
            text(cr, true, kNumPx, row_readout(c, i), ox + kFieldX + kFieldW - kReadoutInset,
                 cap_baseline(cr, true, kNumPx, ry, kRowH), 2);
        }
        if (chooser_) {
            cairo_restore(cr);
            // each name in the word size, or smaller to end as far inside the list's right edge as the active mark
            // stands inside its left (22 px; "Unselected Invalid Flag" is 369 px at 36 px where 342 fit)
            for (int i = 0; i < n; ++i) {
                const int ry = oy + kChooserY + i * kChooserRowH;
                const std::string& nm = ex_.elements[size_t(i)].name;
                const double npx = fit_px(cr, false, kWordPx, nm, kChooserNameX1 - kChooserNameX);
                text(cr, false, npx, nm, ox + kChooserNameX, cap_baseline(cr, false, npx, ry, kChooserRowH), 0);
            }
        }
    } else {
        const int x0 = ox + kPopX0 + 24, vy0 = oy + kPopListY0, vy1 = oy + kPopY1;
        text(cr, false, kWordPx, save_label(), x0, cap_baseline(cr, false, kWordPx, oy + kPopY0, kPopRowH), 0);
        cairo_save(cr);
        cairo_rectangle(cr, ox + kPopX0, vy0 + 1, kPopX1 - kPopX0, vy1 - vy0 - 1);
        cairo_clip(cr);
        for (int k = 0; k < pop_items(); ++k) {
            const int ry = vy0 + k * kPopRowH - pop_.pos;
            if (ry + kPopRowH <= vy0 || ry >= vy1) continue;
            const std::string w = k < np ? "Preset " + std::to_string(presets_list_[size_t(k)].number)
                                : k == np ? "Themes" : ex_.themes[size_t(k - np - 1)].key;
            text(cr, false, kWordPx, w, x0, cap_baseline(cr, false, kWordPx, ry, kPopRowH), 0);
        }
        cairo_restore(cr);
    }
    if (theme_ >= 0) {
        const Theme& t = ex_.themes[size_t(theme_)];
        for (size_t l = 0; l < strip_title_.size(); ++l)
            text(cr, false, kStripTitlePx, strip_title_[l], sx + kStripPad,
                 oy + kStripPad + (l + 1) * kStripTitleLineH - 8, 0);
        for (size_t l = 0; l < strip_sub_.size(); ++l)
            text(cr, false, kStripNamePx, strip_sub_[l], sx + kStripPad,
                 oy + kStripPad + strip_title_.size() * kStripTitleLineH + (l + 1) * kStripLineH - 5, 0);
        cairo_save(cr);
        cairo_rectangle(cr, sx + 1, lv0, kStripW - 2, lv1 - lv0);
        cairo_clip(cr);
        for (size_t i = 0; i < strip_rows_.size(); ++i) {
            const int ry = lv0 + strip_rows_[i].y - strip_.pos;
            if (ry + strip_rows_[i].h <= lv0 || ry >= lv1) continue;
            text(cr, true, kStripHexPx, hex_of(t.colours[i].rgb), sx + kStripTextX, ry + kStripLineH - 5, 0);
            for (size_t l = 0; l < strip_rows_[i].names.size(); ++l)
                text(cr, false, kStripNamePx, strip_rows_[i].names[l], sx + kStripTextX, ry + (l + 2) * kStripLineH - 5, 0);
        }
        cairo_restore(cr);
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
