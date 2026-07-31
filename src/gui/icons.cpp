#include "icons.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace icons {
namespace {

// -- The icon table ---------------------------------------------------------
//
// One row per committed SVG (assets/icons/breeze/), each holding that file's
// path elements in file order. `d` is copied VERBATIM from the file; `fill` is
// the color the file resolves to.
//
// THE COLORS ARE HARD-CODED, per the redesign's color ruling (the carve-out is
// recorded at render.h's palette-block header): they are the SVGs' own values,
// not palette keys and not tunable. The three edit icons paint
// fill:currentColor under the file's own `.ColorScheme-Text { color: #fcfcfc }`
// stylesheet, so #fcfcfc is what they resolve to — the same paper white the
// redesigned labels carry, spelled out here because it is a sample that happens
// to coincide, not a reference. media-record carries its OWN literal #da4453,
// which coincides with the marker-red ring's value by shared Breeze ancestry
// and by nothing else; it is not a reference to that key either.
//
// EVERY ICON IS ONE FILL PER PATH ELEMENT, with cairo's default NONZERO winding
// rule — which is the SVG default too, and what makes document-save's holes
// (the body cutout and the lid slot) come out as holes: its subpaths wind
// against the outline. Filling subpath-by-subpath would flood them.
struct IconPath {
    GuiColor    fill;
    const char* d;
};

struct IconDef {
    double         view_box;   // square; 22 for every Breeze icon here
    const IconPath* paths;
    int            path_count;
};

constexpr GuiColor kIconText   = hex(0xFCFCFC);
constexpr GuiColor kIconRecord = hex(0xDA4453);

constexpr IconPath kDocumentSavePaths[] = {
    {kIconText,
     "M 3 2.9980469 L 3 3 L 3 4 L 3 19 L 4 19 L 19 19 L 19 18 L 19 7 L 19 "
     "6.3007812 L 18.992188 6.3007812 L 19 6.2910156 L 15.707031 2.9980469 L "
     "15.699219 3.0078125 L 15.699219 2.9980469 L 15 2.9980469 L 3 2.9980469 z "
     "M 4 4 L 7 4 L 7 8 L 7 9 L 15 9 L 15 8 L 15 4 L 15.292969 4 L 18 6.7070312 "
     "L 18 7 L 18 18 L 16 18 L 16 11 L 15 11 L 7 11 L 6 11 L 6 18 L 4 18 L 4 4 z "
     "M 8 4 L 11.900391 4 L 11.900391 8 L 8 8 L 8 4 z M 7 12 L 15 12 L 15 18 L 7 "
     "18 L 7 12 z "},
};

constexpr IconPath kEditUndoPaths[] = {
    {kIconText,
     "m8.300781 3l-3.292969 3.292969-.207031.207031.207031.207031 3.292969 "
     "3.292969.707031-.707031-2.292969-2.292969h2.285156 1.00781.492188c3.047 0 "
     "5.5 2.453 5.5 5.5 0 3.047-2.453 5.5-5.5 5.5h-1.5v1h1.5c3.601 0 6.5-2.899 "
     "6.5-6.5 0-3.601-2.899-6.5-6.5-6.5h-.492188-1.00781-2.285156l2.292969-2.292969-.707031-.707031"},
};

constexpr IconPath kEditRedoPaths[] = {
    {kIconText,
     "m13.699219 3l-.707031.707031 2.292968 2.292969h-2.285156-1.00781-.492188c-3.601 "
     "0-6.5 2.899-6.5 6.5 0 3.601 2.899 6.5 6.5 6.5h1.5v-1h-1.5c-3.047 "
     "0-5.5-2.453-5.5-5.5 0-3.047 2.453-5.5 5.5-5.5h.492188 1.00781 "
     "2.285156l-2.292968 2.292969.707031.707031 3.292969-3.292969.207031-.207031-.207031-.207031-3.292969-3.292969"},
};

constexpr IconPath kMediaRecordPaths[] = {
    {kIconRecord,
     "m19 11a8 8 0 0 1 -8 8 8 8 0 0 1 -8-8 8 8 0 0 1 8-8 8 8 0 0 1 8 8z"},
};

constexpr IconDef kDocumentSave{22.0, kDocumentSavePaths, 1};
constexpr IconDef kEditUndo    {22.0, kEditUndoPaths,     1};
constexpr IconDef kEditRedo    {22.0, kEditRedoPaths,     1};
constexpr IconDef kMediaRecord {22.0, kMediaRecordPaths,  1};

const IconDef& icon_def(Icon icon) {
    switch (icon) {
        case Icon::DocumentSave: return kDocumentSave;
        case Icon::EditUndo:     return kEditUndo;
        case Icon::EditRedo:     return kEditRedo;
        case Icon::MediaRecord:  break;
    }
    return kMediaRecord;
}

// -- The `d` interpreter ----------------------------------------------------
//
// THE SUBSET, and it is exactly what the four committed files use (verified by
// reading them): M/m, L/l, H/h, V/v, C/c, A/a, Z/z, implicit command repetition
// (a bare argument set repeats the previous command; after M/m the repeat is
// L/l, per SVG), comma-or-whitespace separation with both optional, negative
// numbers as their own separator ("5-5"), and leading-dot decimals chained
// without separators (".207031.207031" is two numbers — a second '.' ends the
// first). No exponent notation, no S/s, Q/q, T/t: absent from all four files,
// so they have no producer here and the parser refuses them loudly rather than
// guessing. Elliptical 'a' is implemented GENERALLY (endpoint->center
// conversion plus a quarter-arc bezier split) even though media-record's four
// arcs are circular: arcs recur in this icon set and a circle-only shortcut
// would be a trap for the next icon.
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

// One SVG number: optional sign, digits, at most ONE decimal point. The
// single-point rule is what splits ".207031.207031" into two numbers.
bool parse_number(PathCursor& c, double& out) {
    skip_separators(c);
    const char* start = c.p;
    if (!at_end(c) && (*c.p == '-' || *c.p == '+')) ++c.p;
    bool saw_digit = false;
    bool saw_dot   = false;
    while (!at_end(c)) {
        const char ch = *c.p;
        if (ch >= '0' && ch <= '9') { saw_digit = true; ++c.p; continue; }
        if (ch == '.' && !saw_dot)  { saw_dot   = true; ++c.p; continue; }
        break;
    }
    if (!saw_digit) { c.p = start; return false; }
    // std::from_chars: no locale, no allocation, and it consumes exactly the
    // span already delimited above. A leading '+' is not part of its grammar,
    // so skip it.
    const char* num_begin = (*start == '+') ? start + 1 : start;
    double v = 0.0;
    const std::from_chars_result r = std::from_chars(num_begin, c.p, v);
    if (r.ec != std::errc{} || r.ptr != c.p) { c.p = start; return false; }
    out = v;
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
// first thing the subset does not cover (the caller then draws nothing).
bool append_path(cairo_t* cr, const char* d) {
    PathCursor c{d, d + std::strlen(d)};
    char   cmd       = 0;     // the command in force (for implicit repetition)
    double cur_x = 0.0, cur_y = 0.0;      // current point
    double start_x = 0.0, start_y = 0.0;  // current subpath's start (for Z)
    bool   have_start = false;

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
                break;
            case 'L': case 'l':
                if (!parse_number(c, a) || !parse_number(c, b)) return false;
                cur_x = rel ? cur_x + a : a;
                cur_y = rel ? cur_y + b : b;
                cairo_line_to(cr, cur_x, cur_y);
                break;
            case 'H': case 'h':
                if (!parse_number(c, a)) return false;
                cur_x = rel ? cur_x + a : a;
                cairo_line_to(cr, cur_x, cur_y);
                break;
            case 'V': case 'v':
                if (!parse_number(c, a)) return false;
                cur_y = rel ? cur_y + a : a;
                cairo_line_to(cr, cur_x, cur_y);
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
                break;
            }
            case 'Z': case 'z':
                cairo_close_path(cr);
                if (have_start) { cur_x = start_x; cur_y = start_y; }
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

} // namespace

void draw(cairo_t* cr, Icon icon, double x, double y, double size_px,
          double keep_own, GuiColor mixed_with) {
    if (size_px <= 0.0) return;
    const IconDef& def = icon_def(icon);
    if (def.view_box <= 0.0) return;

    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size_px / def.view_box, size_px / def.view_box);
    for (int i = 0; i < def.path_count; ++i) {
        const IconPath& p = def.paths[i];
        cairo_new_path(cr);
        if (!append_path(cr, p.d)) {
            // A transcription error in an in-tree constant: say so once and
            // draw nothing. No fallback shape — a placeholder would let the
            // typo ship.
            cairo_new_path(cr);
            std::fprintf(stderr, "icons: malformed path data, icon not drawn\n");
            continue;
        }
        // The path's own color, retained by keep_own and made up with
        // mixed_with — the disabled face. keep_own == 1 (the default every
        // enabled caller takes) returns the table's color bit-identically, so
        // the enabled path is unchanged by the existence of this one.
        const GuiColor c = mix_color(p.fill, mixed_with, keep_own);
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

} // namespace icons
