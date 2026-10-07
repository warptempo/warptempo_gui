#include "svg_icon.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace svg_icon {

// -- The resolved tree --------------------------------------------------------
//
// What parse() leaves for rasterise(): every node's effective properties
// (the cascade done), every paint a colour or an index into the gradient
// records (every reference resolved), every transform a matrix. Nothing a
// paint reads can be missing or malformed.

struct Stop {
    double offset, r, g, b, a;
};

struct Gradient {
    bool   radial = false;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;           // linear
    double cx = 0, cy = 0, r = 0, fx = 0, fy = 0;    // radial
    cairo_matrix_t pattern_matrix;   // the INVERSE of gradientTransform
    std::vector<Stop> stops;
};

struct Paint {
    enum class Kind { None, Solid, Gradient } kind = Kind::None;
    double r = 0, g = 0, b = 0;
    int    gradient = -1;
};

// THE INHERITED PROPERTIES, at SVG's initial values (fill black, stroke none,
// width 1, butt, miter, limit 4, nonzero, opacities 1, visible).
struct Style {
    Paint fill{Paint::Kind::Solid, 0, 0, 0, -1};
    Paint stroke;
    bool  even_odd       = false;
    double fill_opacity   = 1.0;
    double stroke_opacity = 1.0;
    double stroke_width   = 1.0;
    cairo_line_cap_t  cap  = CAIRO_LINE_CAP_BUTT;
    cairo_line_join_t join = CAIRO_LINE_JOIN_MITER;
    double miter_limit    = 4.0;
    bool  visible        = true;
};

struct Node {
    enum class Kind { Group, Path, Rect } kind = Kind::Group;
    cairo_matrix_t transform;
    double opacity = 1.0;   // the node's own, never inherited
    Style  style;
    std::string d;                                     // Path
    double x = 0, y = 0, w = 0, h = 0, rx = 0, ry = 0;  // Rect
    std::vector<Node> children;                        // Group
};

struct Tree {
    double cell = 48.0;   // the root's width = height, in user units
    Node   root;
    std::vector<Gradient> gradients;
};

namespace {

// -- The XML ------------------------------------------------------------------
//
// THE LEXICAL SUBSET, what an Inkscape file of the period writes (all 58 of
// the Tango set): the `<?xml ...?>` declaration before the root (skipped),
// comments anywhere (skipped), elements with DOUBLE-QUOTED attributes whose
// values may span lines, and text content, which is never read (the RDF
// metadata's titles carry `&gt;` and `&lt;`, so text needs no decoding). A
// DOCTYPE, a CDATA section, any other processing instruction, a single-quoted
// value and an `&` or `<` inside an attribute value are ERRORS — never
// produced by a bundled file. Every view points into the file's bytes and
// lives only while parse() runs.
struct XmlElement {
    std::string_view name;
    std::vector<std::pair<std::string_view, std::string_view>> attrs;
    std::vector<XmlElement> children;

    const std::string_view* attr(std::string_view key) const {
        for (const auto& [k, v] : attrs)
            if (k == key) return &v;
        return nullptr;
    }
};

class XmlReader {
public:
    explicit XmlReader(std::string_view s) : s_(s) {}

    std::expected<XmlElement, std::string> document() {
        XmlElement root;
        if (!misc(true) || !element(root) || !misc(false)) return fail_out();
        if (i_ != s_.size()) {
            fail("text after the root element");
            return fail_out();
        }
        return root;
    }

private:
    std::string_view s_;
    size_t i_ = 0;
    std::string error_;

    bool fail(std::string msg) {
        if (error_.empty()) error_ = std::move(msg);
        return false;
    }
    std::unexpected<std::string> fail_out() {
        return std::unexpected(error_.empty() ? std::string("not XML")
                                              : error_);
    }
    bool at(std::string_view lit) const {
        return s_.substr(i_).starts_with(lit);
    }
    void skip_ws() {
        while (i_ < s_.size() &&
               (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' ||
                s_[i_] == '\r'))
            ++i_;
    }
    static bool name_char(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '-' ||
               c == '.';
    }
    std::string_view name() {
        const size_t start = i_;
        while (i_ < s_.size() && name_char(s_[i_])) ++i_;
        return s_.substr(start, i_ - start);
    }
    bool comment() {   // at "<!--"
        const size_t end = s_.find("-->", i_ + 4);
        if (end == std::string_view::npos) return fail("an unclosed comment");
        i_ = end + 3;
        return true;
    }
    // Whitespace, comments and (before the root only) the declaration.
    bool misc(bool prolog) {
        for (;;) {
            skip_ws();
            if (at("<!--")) {
                if (!comment()) return false;
            } else if (prolog && at("<?xml ") && i_ == 0) {
                const size_t end = s_.find("?>", i_);
                if (end == std::string_view::npos)
                    return fail("an unclosed XML declaration");
                i_ = end + 2;
            } else if (at("<!DOCTYPE")) {
                return fail("a DOCTYPE is outside the icon subset");
            } else if (at("<?")) {
                return fail("a processing instruction is outside the icon "
                            "subset");
            } else {
                return true;
            }
        }
    }
    bool element(XmlElement& out) {
        if (!at("<")) return fail("no root element");
        ++i_;
        out.name = name();
        if (out.name.empty()) return fail("an element without a name");
        for (;;) {
            skip_ws();
            if (at("/>")) { i_ += 2; return true; }
            if (at(">"))  { ++i_; break; }
            const std::string_view key = name();
            if (key.empty())
                return fail("a malformed attribute on <" +
                            std::string(out.name) + ">");
            skip_ws();
            if (!at("=")) return fail("attribute '" + std::string(key) +
                                      "' has no value");
            ++i_;
            skip_ws();
            if (!at("\""))
                return fail("attribute '" + std::string(key) +
                            "' is not double-quoted");
            ++i_;
            const size_t end = s_.find('"', i_);
            if (end == std::string_view::npos)
                return fail("an unclosed attribute value");
            const std::string_view value = s_.substr(i_, end - i_);
            if (value.find_first_of("&<") != std::string_view::npos)
                return fail("attribute '" + std::string(key) +
                            "' holds an entity or a '<'");
            out.attrs.emplace_back(key, value);
            i_ = end + 1;
        }
        for (;;) {   // content: text (never read), comments, children
            const size_t lt = s_.find('<', i_);
            if (lt == std::string_view::npos)
                return fail("<" + std::string(out.name) + "> is not closed");
            i_ = lt;
            if (at("</")) {
                i_ += 2;
                if (name() != out.name)
                    return fail("<" + std::string(out.name) +
                                "> is closed by another name");
                skip_ws();
                if (!at(">")) return fail("a malformed end tag");
                ++i_;
                return true;
            }
            if (at("<!--")) {
                if (!comment()) return false;
            } else if (at("<![CDATA[")) {
                return fail("a CDATA section is outside the icon subset");
            } else if (at("<!") || at("<?")) {
                return fail("a declaration inside an element is outside the "
                            "icon subset");
            } else {
                out.children.emplace_back();
                if (!element(out.children.back())) return false;
            }
        }
    }
};

// -- Values -------------------------------------------------------------------

std::string_view trim(std::string_view v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t' ||
                          v.front() == '\n' || v.front() == '\r'))
        v.remove_prefix(1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' ||
                          v.back() == '\n' || v.back() == '\r'))
        v.remove_suffix(1);
    return v;
}

// One SVG number at the head of `v`, consumed; false when none stands there:
// optional sign, digits, at most ONE decimal point (the single-point rule is
// what splits ".207031.207031" into two numbers) and an optional EXPONENT
// (joined 2026-08-20 with a producer whose editor wrote `8e-3`), scanned
// strictly: the `e` is consumed only when an optional sign and at least one
// digit follow it, so a trailing `e` ends the number instead of swallowing
// the next command letter. std::from_chars reads the delimited span (no
// locale, no allocation; a leading '+' is not its grammar, so skipped).
bool take_number(std::string_view& v, double& out) {
    size_t i = 0;
    if (i < v.size() && (v[i] == '-' || v[i] == '+')) ++i;
    bool digit = false, dot = false;
    while (i < v.size()) {
        if (v[i] >= '0' && v[i] <= '9') { digit = true; ++i; continue; }
        if (v[i] == '.' && !dot)        { dot = true;   ++i; continue; }
        break;
    }
    if (!digit) return false;
    if (i < v.size() && (v[i] == 'e' || v[i] == 'E')) {
        size_t j = i + 1;
        if (j < v.size() && (v[j] == '-' || v[j] == '+')) ++j;
        const size_t digits = j;
        while (j < v.size() && v[j] >= '0' && v[j] <= '9') ++j;
        if (j > digits) i = j;
    }
    const size_t skip = (v[0] == '+') ? 1 : 0;
    const std::from_chars_result r =
        std::from_chars(v.data() + skip, v.data() + i, out);
    if (r.ec != std::errc{} || r.ptr != v.data() + i) return false;
    v.remove_prefix(i);
    return true;
}

// -- The `d` interpreter ------------------------------------------------------
//
// THE SUBSET: M/m, L/l, H/h, V/v, C/c, S/s, A/a, Z/z, implicit command
// repetition (a bare argument set repeats the previous command; after M/m the
// repeat is L/l, per SVG), comma-or-whitespace separation with both optional,
// negative numbers as their own separator ("5-5"), leading-dot decimals
// chained without separators (".207031.207031" is two numbers — a second '.'
// ends the first) and the exponent (take_number). THE BUNDLED TANGO FILES
// SPELL ONLY THE ABSOLUTE M, L, C, A AND z, WITH COMMAS (the 58 files'
// census, 2026-10-06); the relative forms, H / V, the smooth cubic and the
// exponent each joined with a producer an earlier set carried, and the
// subset grows with a producer and is not shrunk when one leaves. No Q/q,
// T/t: never spelled by any bundled file, so the interpreter refuses them
// loudly rather than guessing. Elliptical `A` is implemented GENERALLY
// (endpoint->center conversion plus a quarter-arc bezier split).
//
// THE SUBSET IS THE `d` GRAMMAR AND NOTHING ELSE: the element's transform
// and paint are the builder's.
struct PathCursor {
    const char* p;
    const char* end;
};

bool at_end(const PathCursor& c) { return c.p >= c.end; }

void skip_separators(PathCursor& c) {
    while (!at_end(c)) {
        const char ch = *c.p;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',')
            ++c.p;
        else
            break;
    }
}

// One SVG number after optional separators (take_number's grammar).
bool parse_number(PathCursor& c, double& out) {
    skip_separators(c);
    std::string_view v(c.p, static_cast<size_t>(c.end - c.p));
    if (!take_number(v, out)) return false;
    c.p = v.data();
    return true;
}

// An arc flag is a single '0' or '1' and may be glued to its neighbours.
bool parse_flag(PathCursor& c, bool& out) {
    skip_separators(c);
    if (at_end(c)) return false;
    if (*c.p == '0') { out = false; ++c.p; return true; }
    if (*c.p == '1') { out = true;  ++c.p; return true; }
    return false;
}

// Elliptical arc from the current point to (x1, y1), appended as cubic beziers.
// Standard endpoint->center parameterization (SVG implementation notes F.6.5)
// followed by a split into <=90-degree segments, each approximated by the
// classic (4/3)tan(delta/4) control-point rule.
void arc_to(cairo_t* cr, double x0, double y0, double rx, double ry,
            double phi_deg, bool large_arc, bool sweep, double x1, double y1) {
    constexpr double kPi = 3.14159265358979323846;
    if (rx == 0.0 || ry == 0.0 || (x0 == x1 && y0 == y1)) {
        cairo_line_to(cr, x1, y1);
        return;
    }
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const double phi = phi_deg * kPi / 180.0;
    const double cos_phi = std::cos(phi), sin_phi = std::sin(phi);

    const double dx2 = (x0 - x1) * 0.5, dy2 = (y0 - y1) * 0.5;
    const double x1p =  cos_phi * dx2 + sin_phi * dy2;
    const double y1p = -sin_phi * dx2 + cos_phi * dy2;

    // Enlarge the radii if they cannot span the chord (SVG F.6.6).
    const double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0) {
        const double s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
    }

    const double rx2 = rx * rx, ry2 = ry * ry;
    const double num = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
    const double den = rx2 * y1p * y1p + ry2 * x1p * x1p;
    double coef = 0.0;
    if (den > 0.0 && num > 0.0)
        coef = std::sqrt(num / den);
    if (large_arc == sweep) coef = -coef;
    const double cxp =  coef * rx * y1p / ry;
    const double cyp = -coef * ry * x1p / rx;
    const double cx = cos_phi * cxp - sin_phi * cyp + (x0 + x1) * 0.5;
    const double cy = sin_phi * cxp + cos_phi * cyp + (y0 + y1) * 0.5;

    const double ux = (x1p - cxp) / rx, uy = (y1p - cyp) / ry;
    const double vx = (-x1p - cxp) / rx, vy = (-y1p - cyp) / ry;
    const double theta1 = std::atan2(uy, ux);
    double dtheta = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && dtheta > 0.0) dtheta -= 2.0 * kPi;
    if (sweep  && dtheta < 0.0) dtheta += 2.0 * kPi;

    const int segments =
        static_cast<int>(std::ceil(std::fabs(dtheta) / (kPi * 0.5)));
    const double delta = dtheta / static_cast<double>(segments <= 0 ? 1
                                                                   : segments);
    const double alpha = (4.0 / 3.0) * std::tan(delta * 0.25);
    double t = theta1;
    for (int i = 0; i < segments; ++i) {
        const double t2 = t + delta;
        const double cos_t = std::cos(t),  sin_t = std::sin(t);
        const double cos_2 = std::cos(t2), sin_2 = std::sin(t2);
        // Point and derivative of the parameterized ellipse.
        const double px  = cx + rx * cos_t * cos_phi - ry * sin_t * sin_phi;
        const double py  = cy + rx * cos_t * sin_phi + ry * sin_t * cos_phi;
        const double dpx = -rx * sin_t * cos_phi - ry * cos_t * sin_phi;
        const double dpy = -rx * sin_t * sin_phi + ry * cos_t * cos_phi;
        const double qx  = cx + rx * cos_2 * cos_phi - ry * sin_2 * sin_phi;
        const double qy  = cy + rx * cos_2 * sin_phi + ry * sin_2 * cos_phi;
        const double dqx = -rx * sin_2 * cos_phi - ry * cos_2 * sin_phi;
        const double dqy = -rx * sin_2 * sin_phi + ry * cos_2 * cos_phi;
        cairo_curve_to(cr, px + alpha * dpx, py + alpha * dpy,
                       qx - alpha * dqx, qy - alpha * dqy, qx, qy);
        t = t2;
    }
}

// Walk one `d` string, appending to cr's current path. Returns false on the
// first thing the subset does not cover (the parse's dry run refuses the
// file; a paint walks only strings the dry run passed).
bool append_path(cairo_t* cr, const char* d) {
    PathCursor c{d, d + std::strlen(d)};
    char   cmd       = 0;     // the command in force (for implicit repetition)
    double cur_x = 0.0, cur_y = 0.0;      // current point
    double start_x = 0.0, start_y = 0.0;  // current subpath's start (for Z)
    bool   have_start = false;
    // THE SMOOTH CUBIC'S MEMORY, in ABSOLUTE coordinates: S/s takes its first
    // control point by REFLECTING the previous cubic's second control point
    // about the current point, and takes the current point itself when the
    // previous command was not a cubic (SVG 8.3.6). Both halves need this pair,
    // so every non-cubic arm below clears the flag rather than only the cubic
    // arms setting it.
    double last_c2x = 0.0, last_c2y = 0.0;
    bool   prev_cubic = false;

    for (;;) {
        skip_separators(c);
        if (at_end(c)) break;
        const char ch = *c.p;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            cmd = ch;
            ++c.p;
        } else if (cmd == 0) {
            return false;             // arguments before any command
        } else if (cmd == 'Z' || cmd == 'z') {
            return false;             // Z takes no arguments: this is garbage
        } else if (cmd == 'M') {
            cmd = 'L';                // SVG: an M's extra pairs are lineto
        } else if (cmd == 'm') {
            cmd = 'l';
        }

        const bool rel = (cmd >= 'a' && cmd <= 'z');
        double a = 0.0, b = 0.0, c1x = 0.0, c1y = 0.0, c2x = 0.0, c2y = 0.0;
        switch (cmd) {
            case 'M': case 'm':
                if (!parse_number(c, a) || !parse_number(c, b)) return false;
                cur_x = rel ? cur_x + a : a;
                cur_y = rel ? cur_y + b : b;
                cairo_move_to(cr, cur_x, cur_y);
                start_x = cur_x; start_y = cur_y; have_start = true;
                prev_cubic = false;
                break;
            case 'L': case 'l':
                if (!parse_number(c, a) || !parse_number(c, b)) return false;
                cur_x = rel ? cur_x + a : a;
                cur_y = rel ? cur_y + b : b;
                cairo_line_to(cr, cur_x, cur_y);
                prev_cubic = false;
                break;
            case 'H': case 'h':
                if (!parse_number(c, a)) return false;
                cur_x = rel ? cur_x + a : a;
                cairo_line_to(cr, cur_x, cur_y);
                prev_cubic = false;
                break;
            case 'V': case 'v':
                if (!parse_number(c, a)) return false;
                cur_y = rel ? cur_y + a : a;
                cairo_line_to(cr, cur_x, cur_y);
                prev_cubic = false;
                break;
            case 'C': case 'c': {
                double x2 = 0.0, y2 = 0.0;
                if (!parse_number(c, c1x) || !parse_number(c, c1y) ||
                    !parse_number(c, c2x) || !parse_number(c, c2y) ||
                    !parse_number(c, x2)  || !parse_number(c, y2))
                    return false;
                const double bx = rel ? cur_x : 0.0;
                const double by = rel ? cur_y : 0.0;
                const double ex = bx + x2, ey = by + y2;
                cairo_curve_to(cr, bx + c1x, by + c1y, bx + c2x, by + c2y,
                               ex, ey);
                cur_x = ex; cur_y = ey;
                last_c2x = bx + c2x; last_c2y = by + c2y;
                prev_cubic = true;
                break;
            }
            case 'S': case 's': {
                // FOUR arguments: the SECOND control point and the endpoint.
                // The first control point is the reflection of the previous
                // cubic's second about the current point — and the current
                // point itself when no cubic precedes, which is the same thing
                // as a curve that starts straight.
                double x2 = 0.0, y2 = 0.0;
                if (!parse_number(c, c2x) || !parse_number(c, c2y) ||
                    !parse_number(c, x2)  || !parse_number(c, y2))
                    return false;
                const double bx = rel ? cur_x : 0.0;
                const double by = rel ? cur_y : 0.0;
                const double r1x = prev_cubic ? 2.0 * cur_x - last_c2x : cur_x;
                const double r1y = prev_cubic ? 2.0 * cur_y - last_c2y : cur_y;
                const double ex = bx + x2, ey = by + y2;
                cairo_curve_to(cr, r1x, r1y, bx + c2x, by + c2y, ex, ey);
                cur_x = ex; cur_y = ey;
                last_c2x = bx + c2x; last_c2y = by + c2y;
                prev_cubic = true;
                break;
            }
            case 'A': case 'a': {
                double rx = 0.0, ry = 0.0, rot = 0.0, ex = 0.0, ey = 0.0;
                bool large = false, sweep = false;
                if (!parse_number(c, rx) || !parse_number(c, ry) ||
                    !parse_number(c, rot) ||
                    !parse_flag(c, large) || !parse_flag(c, sweep) ||
                    !parse_number(c, ex) || !parse_number(c, ey))
                    return false;
                const double x1 = rel ? cur_x + ex : ex;
                const double y1 = rel ? cur_y + ey : ey;
                arc_to(cr, cur_x, cur_y, rx, ry, rot, large, sweep, x1, y1);
                cur_x = x1; cur_y = y1;
                prev_cubic = false;
                break;
            }
            case 'Z': case 'z':
                cairo_close_path(cr);
                if (have_start) { cur_x = start_x; cur_y = start_y; }
                prev_cubic = false;
                break;
            default:
                return false;         // outside the subset
        }
        // IMPLICIT REPETITION needs no code of its own: `cmd` stays in force,
        // so the next pass re-enters the same case when the next token is a
        // number and re-binds it when the token is a letter. The M->L rewrite
        // at the top of the loop is the one SVG rule that is not automatic.
    }
    return true;
}

// A whole value that is one number.
bool whole_number(std::string_view v, double& out) {
    v = trim(v);
    return take_number(v, out) && v.empty();
}

// A LENGTH: a number, bare or in `px` (one user unit), or in `pt` (4/3 user
// units, as librsvg reads it — the producer: six Tango files' stroke-width of
// an unpainted stroke, `0.25pt`, `1.0000000pt`).
bool length(std::string_view v, double& out) {
    v = trim(v);
    if (!take_number(v, out)) return false;
    if (v.empty() || v == "px") return true;
    if (v == "pt") { out *= 4.0 / 3.0; return true; }
    return false;
}

bool hex_digit(char c, int& out) {
    if (c >= '0' && c <= '9') { out = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { out = c - 'A' + 10; return true; }
    return false;
}

// A COLOUR: `#rrggbb`, `#rgb` (the producer: `#c00`, `#999` in two files) or
// the names `black` and `white` (a third of the files); no other name and no
// rgb().
bool colour(std::string_view v, double& r, double& g, double& b) {
    v = trim(v);
    if (v == "black") { r = g = b = 0.0; return true; }
    if (v == "white") { r = g = b = 1.0; return true; }
    if (v.size() != 7 && v.size() != 4) return false;
    if (v[0] != '#') return false;
    int c[6];
    if (v.size() == 7) {
        for (int i = 0; i < 6; ++i)
            if (!hex_digit(v[1 + i], c[i])) return false;
        r = (c[0] * 16 + c[1]) / 255.0;
        g = (c[2] * 16 + c[3]) / 255.0;
        b = (c[4] * 16 + c[5]) / 255.0;
    } else {
        for (int i = 0; i < 3; ++i)
            if (!hex_digit(v[1 + i], c[i])) return false;
        r = (c[0] * 17) / 255.0;
        g = (c[1] * 17) / 255.0;
        b = (c[2] * 17) / 255.0;
    }
    return true;
}

// A TRANSFORM LIST: `name(args)` items separated by whitespace or a comma —
// `matrix` of six, `translate` of one or two (ty 0), `scale` of one or two
// (sy = sx); `rotate`, `skewX` and `skewY` are refused (never spelled by a
// bundled file). The list composes as SVG's: the LAST item applies first.
bool transform_list(std::string_view v, cairo_matrix_t& out) {
    cairo_matrix_init_identity(&out);
    for (;;) {
        while (!v.empty() && (v.front() == ' ' || v.front() == ',' ||
                              v.front() == '\n' || v.front() == '\t' ||
                              v.front() == '\r'))
            v.remove_prefix(1);
        if (v.empty()) return true;
        const size_t open = v.find('(');
        const size_t close = v.find(')');
        if (open == std::string_view::npos || close == std::string_view::npos ||
            close < open)
            return false;
        const std::string_view fn = trim(v.substr(0, open));
        std::string_view args = v.substr(open + 1, close - open - 1);
        v.remove_prefix(close + 1);
        double a[6];
        int n = 0;
        for (;;) {
            while (!args.empty() && (args.front() == ' ' ||
                                     args.front() == ',' ||
                                     args.front() == '\n' ||
                                     args.front() == '\t' ||
                                     args.front() == '\r'))
                args.remove_prefix(1);
            if (args.empty()) break;
            if (n == 6 || !take_number(args, a[n])) return false;
            ++n;
        }
        cairo_matrix_t t;
        if (fn == "matrix" && n == 6)
            cairo_matrix_init(&t, a[0], a[1], a[2], a[3], a[4], a[5]);
        else if (fn == "translate" && (n == 1 || n == 2))
            cairo_matrix_init_translate(&t, a[0], n == 2 ? a[1] : 0.0);
        else if (fn == "scale" && (n == 1 || n == 2))
            cairo_matrix_init_scale(&t, a[0], n == 2 ? a[1] : a[0]);
        else
            return false;
        cairo_matrix_t composed;
        cairo_matrix_multiply(&composed, &t, &out);   // t applies first
        out = composed;
    }
}

bool foreign(std::string_view name) {
    return name.find(':') != std::string_view::npos;
}

// -- The builder --------------------------------------------------------------
//
// PASS ONE maps every element's `id` over the WHOLE tree (23 of the 58 files
// reference a gradient defined after its user); PASS TWO walks the drawing
// from the root with the cascade, resolving each url(#id) into a gradient
// record as it is met.
class Builder {
public:
    Builder(const XmlElement& root, Tree& tree) : root_(root), tree_(tree) {
        map_ids(root_);
    }

    std::optional<std::string> build() {
        if (root_.name != "svg")
            return "the root element is <" + std::string(root_.name) +
                   ">, not <svg>";
        const std::string_view* xmlns = root_.attr("xmlns");
        if (!xmlns || *xmlns != "http://www.w3.org/2000/svg")
            return std::string("the default namespace is not SVG's");
        const std::string_view* w = root_.attr("width");
        const std::string_view* h = root_.attr("height");
        double wv = 0.0, hv = 0.0;
        if (!w || !h || !length(*w, wv) || !length(*h, hv) || wv <= 0.0 ||
            wv != hv)
            return std::string("the root's width and height are not one "
                               "length");
        tree_.cell = wv;
        if (const std::string_view* vb = root_.attr("viewBox")) {
            std::string_view rest = *vb;
            double q[4];
            for (double& x : q) {
                while (!rest.empty() &&
                       (rest.front() == ' ' || rest.front() == ','))
                    rest.remove_prefix(1);
                if (!take_number(rest, x))
                    return std::string("a malformed viewBox");
            }
            if (!trim(rest).empty() || q[0] != 0.0 || q[1] != 0.0 ||
                q[2] != wv || q[3] != hv)
                return std::string("a viewBox other than 0 0 width height");
        }
        Style initial;
        bool displayed = true;
        if (!node(root_, initial, tree_.root, true, displayed)) return error_;
        return std::nullopt;
    }

private:
    const XmlElement& root_;
    Tree& tree_;
    std::unordered_map<std::string_view, const XmlElement*> ids_;
    std::unordered_map<const XmlElement*, int> gradient_of_;
    std::string error_;

    bool fail(std::string msg) {
        if (error_.empty()) error_ = std::move(msg);
        return false;
    }

    void map_ids(const XmlElement& e) {
        if (const std::string_view* id = e.attr("id")) ids_.emplace(*id, &e);
        for (const XmlElement& c : e.children) map_ids(c);
    }

    // A PAINT: `none`, a colour, or `url(#id)` naming a gradient (no
    // fallback colour after it).
    bool paint(std::string_view key, std::string_view v, Paint& out) {
        v = trim(v);
        if (v == "none") { out = Paint{}; return true; }
        if (v.starts_with("url(#") && v.ends_with(")")) {
            const std::string_view id = v.substr(5, v.size() - 6);
            int index = -1;
            if (!gradient(id, index)) return false;
            out = Paint{Paint::Kind::Gradient, 0, 0, 0, index};
            return true;
        }
        Paint p{Paint::Kind::Solid, 0, 0, 0, -1};
        if (!colour(v, p.r, p.g, p.b))
            return fail(std::string(key) + " '" + std::string(v) +
                        "' is outside the icon subset");
        out = p;
        return true;
    }

    // THE GRADIENT RECORD for `id`, resolved once: the href chain walked
    // (Inkscape's idiom: a geometry gradient pointing at a stops-only one;
    // the chain walk is general), each attribute taken from the FIRST
    // element of the chain that states it, the stops from the first that has
    // any. userSpaceOnUse is the only accepted units (SVG's default,
    // objectBoundingBox when none is stated, is refused) and pad the only
    // spread; the geometry must be stated (fx, fy default to cx, cy).
    bool gradient(std::string_view id, int& index) {
        const auto found = ids_.find(id);
        if (found == ids_.end())
            return fail("url(#" + std::string(id) + ") is unresolved");
        const XmlElement* head = found->second;
        if (const auto memo = gradient_of_.find(head);
            memo != gradient_of_.end()) {
            index = memo->second;
            return true;
        }
        if (head->name != "linearGradient" && head->name != "radialGradient")
            return fail("url(#" + std::string(id) + ") names a <" +
                        std::string(head->name) + ">, not a gradient");
        std::vector<const XmlElement*> chain{head};
        for (const XmlElement* e = head;;) {
            for (const auto& [k, v] : e->attrs) {
                if (k == "xlink:href" || k == "id" || k == "gradientUnits" ||
                    k == "gradientTransform" || k == "spreadMethod" ||
                    k == "x1" || k == "y1" || k == "x2" || k == "y2" ||
                    k == "cx" || k == "cy" || k == "r" || k == "fx" ||
                    k == "fy" || foreign(k))
                    continue;
                return fail("attribute '" + std::string(k) + "' on <" +
                            std::string(e->name) +
                            "> is outside the icon subset");
            }
            const std::string_view* href = e->attr("xlink:href");
            if (!href) break;
            if (!href->starts_with("#"))
                return fail("an href that is not #id");
            const auto next = ids_.find(href->substr(1));
            if (next == ids_.end())
                return fail("href " + std::string(*href) + " is unresolved");
            const XmlElement* n = next->second;
            if (n->name != "linearGradient" && n->name != "radialGradient")
                return fail("href " + std::string(*href) +
                            " names no gradient");
            for (const XmlElement* seen : chain)
                if (seen == n) return fail("an href cycle");
            chain.push_back(n);
            e = n;
        }
        const auto stated = [&](std::string_view k) -> const std::string_view* {
            for (const XmlElement* e : chain)
                if (const std::string_view* v = e->attr(k)) return v;
            return nullptr;
        };
        const std::string label = "gradient #" + std::string(id);
        const std::string_view* units = stated("gradientUnits");
        if (!units || *units != "userSpaceOnUse")
            return fail(label + " is not in userSpaceOnUse units");
        if (const std::string_view* spread = stated("spreadMethod");
            spread && *spread != "pad")
            return fail(label + "'s spreadMethod is not pad");
        Gradient g;
        g.radial = head->name == "radialGradient";
        const auto geometry = [&](std::string_view k, double& out) {
            const std::string_view* v = stated(k);
            if (!v) return fail(label + " states no " + std::string(k));
            if (!length(*v, out))
                return fail(label + "'s " + std::string(k) +
                            " is outside the icon subset");
            return true;
        };
        if (g.radial) {
            if (!geometry("cx", g.cx) || !geometry("cy", g.cy) ||
                !geometry("r", g.r))
                return false;
            g.fx = g.cx;
            g.fy = g.cy;
            if (stated("fx") && !geometry("fx", g.fx)) return false;
            if (stated("fy") && !geometry("fy", g.fy)) return false;
        } else {
            if (!geometry("x1", g.x1) || !geometry("y1", g.y1) ||
                !geometry("x2", g.x2) || !geometry("y2", g.y2))
                return false;
        }
        cairo_matrix_init_identity(&g.pattern_matrix);
        if (const std::string_view* t = stated("gradientTransform")) {
            if (!transform_list(*t, g.pattern_matrix))
                return fail(label + "'s gradientTransform '" +
                            std::string(*t) +
                            "' is outside the icon subset");
            if (cairo_matrix_invert(&g.pattern_matrix) != CAIRO_STATUS_SUCCESS)
                return fail(label + "'s gradientTransform is singular");
        }
        const XmlElement* stops = nullptr;
        for (const XmlElement* e : chain) {
            for (const XmlElement& c : e->children) {
                if (foreign(c.name)) continue;
                if (c.name != "stop")
                    return fail("element <" + std::string(c.name) +
                                "> inside a gradient is outside the icon "
                                "subset");
                if (!stops) stops = e;
            }
            if (stops) break;
        }
        if (!stops) return fail(label + " has no stops");
        double last = 0.0;
        for (const XmlElement& c : stops->children) {
            if (foreign(c.name)) continue;
            Stop s{0, 0, 0, 0, 1};
            if (!stop(c, s)) return false;
            // SVG's rule: an offset below an earlier one takes the earlier.
            s.offset = std::max(s.offset, last);
            last = s.offset;
            g.stops.push_back(s);
        }
        index = static_cast<int>(tree_.gradients.size());
        tree_.gradients.push_back(std::move(g));
        gradient_of_.emplace(head, index);
        return true;
    }

    // A STOP: `offset` a number in [0, 1] (or a percentage), its colour and
    // opacity as attributes or in `style`, the colour black and the opacity
    // 1 when unstated.
    bool stop(const XmlElement& e, Stop& s) {
        const std::string_view* off = e.attr("offset");
        if (!off) return fail("a <stop> without an offset");
        std::string_view ov = trim(*off);
        double o = 0.0;
        if (!take_number(ov, o)) return fail("a malformed stop offset");
        if (ov == "%") o /= 100.0;
        else if (!ov.empty()) return fail("a malformed stop offset");
        s.offset = std::clamp(o, 0.0, 1.0);
        const auto prop = [&](std::string_view k, std::string_view v) {
            if (k == "stop-color") {
                if (!colour(v, s.r, s.g, s.b))
                    return fail("stop-color '" + std::string(trim(v)) +
                                "' is outside the icon subset");
                return true;
            }
            if (k == "stop-opacity") {
                if (!whole_number(v, s.a))
                    return fail("stop-opacity '" + std::string(trim(v)) +
                                "' is outside the icon subset");
                s.a = std::clamp(s.a, 0.0, 1.0);
                return true;
            }
            return fail("property '" + std::string(k) +
                        "' on <stop> is outside the icon subset");
        };
        for (const auto& [k, v] : e.attrs) {
            if (k == "id" || k == "offset" || k == "style" || foreign(k))
                continue;
            if (!prop(k, v)) return false;
        }
        if (const std::string_view* st = e.attr("style")) {
            std::string_view rest = *st;
            while (!rest.empty()) {
                const size_t semi = rest.find(';');
                const std::string_view part = trim(rest.substr(0, semi));
                rest = semi == std::string_view::npos
                           ? std::string_view{}
                           : rest.substr(semi + 1);
                if (part.empty()) continue;
                const size_t colon = part.find(':');
                if (colon == std::string_view::npos)
                    return fail("a style declaration without a colon");
                if (!prop(trim(part.substr(0, colon)),
                          trim(part.substr(colon + 1))))
                    return false;
            }
        }
        for (const XmlElement& c : e.children)
            if (!foreign(c.name))
                return fail("element <" + std::string(c.name) +
                            "> inside a <stop> is outside the icon subset");
        return true;
    }

    // ONE PROPERTY, as a presentation attribute or a `style` declaration,
    // onto the node's style (the inherited ones) or its own opacity and
    // display. The INERT ones (Inkscape writes them on every shape) are
    // accepted and ignored; the five that would change the picture are
    // accepted only at `none`.
    bool property(std::string_view k, std::string_view v, Style& s,
                  double& opacity, bool& displayed) {
        v = trim(v);
        const auto bad = [&] {
            return fail(std::string(k) + " '" + std::string(v) +
                        "' is outside the icon subset");
        };
        double n = 0.0;
        if (k == "fill")   return paint(k, v, s.fill);
        if (k == "stroke") return paint(k, v, s.stroke);
        if (k == "fill-rule") {
            if (v == "evenodd") s.even_odd = true;
            else if (v == "nonzero") s.even_odd = false;
            else return bad();
            return true;
        }
        if (k == "fill-opacity" || k == "stroke-opacity" || k == "opacity") {
            if (!whole_number(v, n)) return bad();
            n = std::clamp(n, 0.0, 1.0);
            (k == "opacity" ? opacity
                            : k == "fill-opacity" ? s.fill_opacity
                                                  : s.stroke_opacity) = n;
            return true;
        }
        if (k == "stroke-width") {
            if (!length(v, n) || n < 0.0) return bad();
            s.stroke_width = n;
            return true;
        }
        if (k == "stroke-linecap") {
            if (v == "butt") s.cap = CAIRO_LINE_CAP_BUTT;
            else if (v == "round") s.cap = CAIRO_LINE_CAP_ROUND;
            else if (v == "square") s.cap = CAIRO_LINE_CAP_SQUARE;
            else return bad();
            return true;
        }
        if (k == "stroke-linejoin") {
            if (v == "miter") s.join = CAIRO_LINE_JOIN_MITER;
            else if (v == "round") s.join = CAIRO_LINE_JOIN_ROUND;
            else if (v == "bevel") s.join = CAIRO_LINE_JOIN_BEVEL;
            else return bad();
            return true;
        }
        if (k == "stroke-miterlimit") {
            if (!whole_number(v, n) || n < 1.0) return bad();
            s.miter_limit = n;
            return true;
        }
        if (k == "display") {
            if (v == "none") displayed = false;
            else if (v != "inline" && v != "block") return bad();
            return true;
        }
        if (k == "visibility") {
            if (v == "hidden") s.visible = false;
            else if (v == "visible") s.visible = true;
            else return bad();
            return true;
        }
        if (k == "marker" || k == "marker-start" || k == "marker-mid" ||
            k == "marker-end" || k == "stroke-dasharray" || k == "filter" ||
            k == "clip-path" || k == "mask")
            return v == "none" ? true : bad();
        if (k == "color" || k == "overflow" || k == "stroke-dashoffset" ||
            k == "enable-background" || k == "font-family" ||
            k == "font-size" || k == "font-style" || k == "font-weight" ||
            k == "font-stretch" || k == "font-variant" ||
            k == "line-height" || k == "text-anchor" || k == "text-align" ||
            k == "writing-mode")
            return true;
        return fail("property '" + std::string(k) +
                    "' is outside the icon subset");
    }

    // The ELEMENT'S OWN ATTRIBUTES, beyond `id`, `style` and the
    // properties: the root's size and namespaces, a shape's geometry, a
    // transform. An attribute with a foreign prefix is ignored (Inkscape's
    // and Sodipodi's bookkeeping), and so is a path's xlink:href (an
    // `inkscape:offset` path naming its source; its own `d` rules).
    static bool own_attribute(std::string_view element, std::string_view k) {
        if (element == "svg")
            return k == "width" || k == "height" || k == "version" ||
                   k == "viewBox" || k == "xmlns" || k.starts_with("xmlns:");
        if (k == "transform") return true;
        if (element == "path") return k == "d";
        if (element == "rect")
            return k == "x" || k == "y" || k == "width" || k == "height" ||
                   k == "rx" || k == "ry";
        return false;
    }

    // ONE RENDERED ELEMENT (svg, g, path, rect) into `out`, its children
    // after it. `inherited` is the parent's style; the node's own attributes
    // apply first and its `style` over them (SVG's order).
    bool node(const XmlElement& e, const Style& inherited, Node& out,
              bool is_root, bool& displayed) {
        out.style = inherited;
        out.opacity = 1.0;
        cairo_matrix_init_identity(&out.transform);
        displayed = true;
        for (const auto& [k, v] : e.attrs) {
            if (k == "id" || k == "style") continue;
            if (foreign(k)) {
                if (k == "xlink:href" && e.name != "path")
                    return fail("xlink:href on <" + std::string(e.name) +
                                "> is outside the icon subset");
                continue;
            }
            if (own_attribute(e.name, k)) continue;
            if (!property(k, v, out.style, out.opacity, displayed))
                return false;
        }
        if (const std::string_view* st = e.attr("style")) {
            std::string_view rest = *st;
            while (!rest.empty()) {
                const size_t semi = rest.find(';');
                const std::string_view part = trim(rest.substr(0, semi));
                rest = semi == std::string_view::npos
                           ? std::string_view{}
                           : rest.substr(semi + 1);
                if (part.empty()) continue;
                const size_t colon = part.find(':');
                if (colon == std::string_view::npos)
                    return fail("a style declaration without a colon");
                if (!property(trim(part.substr(0, colon)),
                              trim(part.substr(colon + 1)), out.style,
                              out.opacity, displayed))
                    return false;
            }
        }
        if (!displayed && is_root)
            return fail("the root is display:none");
        if (const std::string_view* t = e.attr("transform")) {
            if (!transform_list(*t, out.transform))
                return fail("transform '" + std::string(*t) +
                            "' is outside the icon subset");
        }
        if (e.name == "path") {
            out.kind = Node::Kind::Path;
            const std::string_view* d = e.attr("d");
            if (!d) return fail("a <path> without d");
            out.d = std::string(*d);
            // THE DRY RUN through the one interpreter, so a paint can never
            // meet a `d` it refuses.
            cairo_surface_t* probe_surf =
                cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
            cairo_t* probe = cairo_create(probe_surf);
            const bool ok = append_path(probe, out.d.c_str());
            cairo_destroy(probe);
            cairo_surface_destroy(probe_surf);
            if (!ok) return fail("a path's d is outside the icon subset");
        } else if (e.name == "rect") {
            out.kind = Node::Kind::Rect;
            const auto num = [&](std::string_view k, double& v, bool need) {
                const std::string_view* a = e.attr(k);
                if (!a) return need ? fail("a <rect> without " +
                                           std::string(k))
                                    : true;
                if (!length(*a, v) || (v < 0.0 && k != "x" && k != "y"))
                    return fail("rect " + std::string(k) + " '" +
                                std::string(*a) +
                                "' is outside the icon subset");
                return true;
            };
            double rx = -1.0, ry = -1.0;
            if (!num("x", out.x, false) || !num("y", out.y, false) ||
                !num("width", out.w, true) || !num("height", out.h, true) ||
                !num("rx", rx, false) || !num("ry", ry, false))
                return false;
            // SVG's rounding: a missing radius takes the other's, each
            // clamped to half its side (the producer: ZoomOriginal's ry-only
            // rect and its rx 3.21 on a 4.44 height).
            if (rx < 0.0) rx = ry;
            if (ry < 0.0) ry = rx;
            out.rx = std::min(std::max(rx, 0.0), out.w / 2.0);
            out.ry = std::min(std::max(ry, 0.0), out.h / 2.0);
        } else {
            out.kind = Node::Kind::Group;
            for (const XmlElement& c : e.children) {
                if (foreign(c.name)) {
                    if (c.name.starts_with("svg:"))
                        return fail("a prefixed SVG element is outside the "
                                    "icon subset");
                    continue;   // Inkscape's and the RDF tree: skipped whole
                }
                // THE NON-RENDERING ELEMENTS: defs and gradients are read
                // only by reference (the gradient records), metadata never.
                if (c.name == "defs" || c.name == "metadata" ||
                    c.name == "linearGradient" ||
                    c.name == "radialGradient")
                    continue;
                if (c.name != "g" && c.name != "path" && c.name != "rect")
                    return fail("element <" + std::string(c.name) +
                                "> is outside the icon subset");
                Node child;
                bool shown = true;
                if (!node(c, out.style, child, false, shown)) return false;
                // display:none drops the node and its subtree (parsed, so
                // still policed).
                if (shown) out.children.push_back(std::move(child));
            }
        }
        if (out.kind != Node::Kind::Group) {
            for (const XmlElement& c : e.children)
                if (!foreign(c.name))
                    return fail("element <" + std::string(c.name) +
                                "> inside a shape is outside the icon "
                                "subset");
        }
        return true;
    }
};

// -- The walk -----------------------------------------------------------------
//
// THE CAIRO CALLS, one per SVG primitive (the prototype's, line for line,
// measured against librsvg over the 58 Tango files at 72 and 33 px: median
// 0.02 levels a channel). Each paint's alpha is the node's opacity times the
// paint's own opacity when the shape has one paint; a shape with BOTH a fill
// and a stroke under an opacity below 1 is painted in a group and the group
// composited at the opacity (SVG's order: the stroke over the fill, then the
// alpha — folding it into both would show the fill through the stroke).

void set_paint(cairo_t* cr, const Tree& t, const Paint& p, double alpha) {
    if (p.kind == Paint::Kind::Solid) {
        cairo_set_source_rgba(cr, p.r, p.g, p.b, alpha);
        return;
    }
    const Gradient& g = t.gradients[static_cast<size_t>(p.gradient)];
    cairo_pattern_t* pat =
        g.radial ? cairo_pattern_create_radial(g.fx, g.fy, 0.0, g.cx, g.cy,
                                               g.r)
                 : cairo_pattern_create_linear(g.x1, g.y1, g.x2, g.y2);
    // userSpaceOnUse is the shape's user space at cairo_set_source, which is
    // cairo's pattern space: the gradientTransform's inverse is all there is.
    cairo_pattern_set_matrix(pat, &g.pattern_matrix);
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_PAD);
    for (const Stop& s : g.stops)
        cairo_pattern_add_color_stop_rgba(pat, s.offset, s.r, s.g, s.b,
                                          s.a * alpha);
    cairo_set_source(cr, pat);
    cairo_pattern_destroy(pat);
}

void shape_path(cairo_t* cr, const Node& n) {
    cairo_new_path(cr);
    if (n.kind == Node::Kind::Path) {
        append_path(cr, n.d.c_str());   // proven at parse
        return;
    }
    if (n.rx == 0.0 || n.ry == 0.0) {
        cairo_rectangle(cr, n.x, n.y, n.w, n.h);
        return;
    }
    // FOUR ELLIPTICAL QUARTER ARCS (librsvg's sequence).
    constexpr double kHalfPi = 1.57079632679489661923;
    cairo_move_to(cr, n.x + n.rx, n.y);
    cairo_line_to(cr, n.x + n.w - n.rx, n.y);
    const double corners[4][3] = {
        {n.x + n.w - n.rx, n.y + n.ry, -kHalfPi},
        {n.x + n.w - n.rx, n.y + n.h - n.ry, 0.0},
        {n.x + n.rx, n.y + n.h - n.ry, kHalfPi},
        {n.x + n.rx, n.y + n.ry, 2.0 * kHalfPi},
    };
    for (const auto& c : corners) {
        cairo_save(cr);
        cairo_translate(cr, c[0], c[1]);
        cairo_scale(cr, n.rx, n.ry);
        cairo_arc(cr, 0.0, 0.0, 1.0, c[2], c[2] + kHalfPi);
        cairo_restore(cr);
    }
    cairo_close_path(cr);
}

void walk(cairo_t* cr, const Tree& t, const Node& n) {
    cairo_save(cr);
    cairo_transform(cr, &n.transform);
    if (n.kind == Node::Kind::Group) {
        const bool group = n.opacity < 1.0;
        if (group) cairo_push_group(cr);
        for (const Node& c : n.children) walk(cr, t, c);
        if (group) {
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, n.opacity);
        }
        cairo_restore(cr);
        return;
    }
    const Style& s = n.style;
    if (!s.visible) { cairo_restore(cr); return; }
    const bool fill   = s.fill.kind != Paint::Kind::None;
    const bool stroke = s.stroke.kind != Paint::Kind::None;
    const bool group  = n.opacity < 1.0 && fill && stroke;
    const double a    = group ? 1.0 : n.opacity;
    if (group) cairo_push_group(cr);
    shape_path(cr, n);
    if (fill) {
        cairo_set_fill_rule(cr, s.even_odd ? CAIRO_FILL_RULE_EVEN_ODD
                                           : CAIRO_FILL_RULE_WINDING);
        set_paint(cr, t, s.fill, a * s.fill_opacity);
        cairo_fill_preserve(cr);
    }
    if (stroke) {
        cairo_set_line_width(cr, s.stroke_width);
        cairo_set_line_cap(cr, s.cap);
        cairo_set_line_join(cr, s.join);
        cairo_set_miter_limit(cr, s.miter_limit);
        set_paint(cr, t, s.stroke, a * s.stroke_opacity);
        cairo_stroke_preserve(cr);
    }
    cairo_new_path(cr);
    if (group) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, n.opacity);
    }
    cairo_restore(cr);
}

} // namespace

std::expected<Document, std::string> parse(std::string_view bytes) {
    XmlReader reader(bytes);
    std::expected<XmlElement, std::string> root = reader.document();
    if (!root) return std::unexpected(root.error());
    auto tree = std::make_shared<Tree>();
    Builder builder(*root, *tree);
    if (std::optional<std::string> err = builder.build())
        return std::unexpected(std::move(*err));
    return Document{std::move(tree)};
}

cairo_surface_t* rasterise(const Document& doc, int px) {
    cairo_surface_t* s =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
    cairo_t* cr = cairo_create(s);
    const double k = static_cast<double>(px) / doc.tree->cell;
    cairo_scale(cr, k, k);
    walk(cr, *doc.tree, doc.tree->root);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

cairo_surface_t* saturated_copy(cairo_surface_t* live) {
    cairo_surface_flush(live);
    const int w = cairo_image_surface_get_width(live);
    const int h = cairo_image_surface_get_height(live);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(out);
    const unsigned char* src = cairo_image_surface_get_data(live);
    unsigned char* dst = cairo_image_surface_get_data(out);
    const int src_stride = cairo_image_surface_get_stride(live);
    const int dst_stride = cairo_image_surface_get_stride(out);
    constexpr double kFrame = 192.0 / 255.0;
    for (int y = 0; y < h; ++y) {
        const auto* srow = reinterpret_cast<const uint32_t*>(src + y * src_stride);
        auto* drow = reinterpret_cast<uint32_t*>(dst + y * dst_stride);
        for (int x = 0; x < w; ++x) {
            const uint32_t p = srow[x];
            const double a = static_cast<double>((p >> 24) & 0xFF);
            const double r = static_cast<double>((p >> 16) & 0xFF);
            const double g = static_cast<double>((p >> 8) & 0xFF);
            const double b = static_cast<double>(p & 0xFF);
            const double lum = 0.30 * r + 0.59 * g + 0.11 * b;
            const auto l = static_cast<uint32_t>(std::nearbyint(lum * kFrame));
            const auto a2 = static_cast<uint32_t>(std::nearbyint(a * kFrame));
            drow[x] = (a2 << 24) | (l << 16) | (l << 8) | l;
        }
    }
    cairo_surface_mark_dirty(out);
    return out;
}

} // namespace svg_icon
