#include "icons.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

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
// `tx`/`ty` carry the path element's own SVG `transform="translate(tx, ty)"`,
// applied around the path so `d` can stay VERBATIM. Exactly ONE committed file
// needs it — dialog-ok-apply.svg, whose author drew the check mark at its
// document coordinates and translated it back into the viewBox — and baking the
// offset into its twenty-odd numbers by hand would destroy the property that a
// diff between this table and the file is a transcription bug and nothing else.
// It is a TRANSLATE ONLY: no scale, rotate or matrix appears in any committed
// file, so none is modelled and none is silently accepted.
struct IconPath {
    GuiColor    fill;
    const char* d;
    double      tx = 0.0;
    double      ty = 0.0;
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

// -- Row 4's six --------------------------------------------------------------
//
// Same rules as the four above: `d` verbatim from the committed file, the fill
// hard-coded to what that file resolves to. Five of the six are pure
// `.ColorScheme-Text` = #fcfcfc; preview-render-on is TWO paths, the second
// carrying its own literal #d24d57 (the "on" pip), which is a third independent
// red in this tree and deliberately not a reference to media-record's #da4453
// or to the marker ring.

constexpr GuiColor kIconPreviewOn = hex(0xD24D57);

constexpr IconPath kEditCopyPaths[] = {
    {kIconText,
     "M 3 3 L 3 17 L 7 17 L 7 19 L 17 19 L 17 10 L 13 6 L 12 6 L 9 3 L 3 3 Z M "
     "4 4 L 8 4 L 8 6 L 7 6 L 7 16 L 4 16 L 4 4 Z M 8 7 L 12 7 L 12 11 L 16 11 "
     "L 16 18 L 8 18 L 8 7 Z"},
};

constexpr IconPath kEditPastePaths[] = {
    {kIconText,
     "M 7 3 L 7 5 L 5 5 L 4 5 L 4 19 L 5 19 L 18 19 L 18 18 L 18 5 L 17 5 L 15 "
     "5 L 15 3 L 7 3 z M 5 6 L 6 6 L 6 8 L 16 8 L 16 6 L 17 6 L 17 18 L 5 18 L "
     "5 6 z M 7 9 L 7 10 L 15 10 L 15 9 L 7 9 z M 7 12 L 7 13 L 13 13 L 13 12 L "
     "7 12 z M 7 15 L 7 16 L 10 16 L 10 15 L 7 15 z "},
};

constexpr IconPath kMusicNote16thPaths[] = {
    {kIconText,
     "m 11,3 0,1 0,3 0,1 0,4 0,2.640625 C 10.450691,14.229206 9.7385673,"
     "14.001104 9,14 7.3431458,14 6,15.119288 6,16.5 6,17.880712 7.3431458,19 "
     "9,19 c 1.656854,0 3,-1.119288 3,-2.5 L 12,12 12,8.0957031 c 1.473938,"
     "0.2519592 3.180894,1.3814645 4,2.1485529 L 16,9.5 16,9 16,8.84375 16,5.5 "
     "16,4.84375 C 14.788541,3.8472864 12.971189,3 11,3 Z m 1,1.0957031 c "
     "1.132773,0.1936395 2.194743,0.6800469 3,1.2460938 l 0,2.8046875 C "
     "14.137786,7.634143 13.107988,7.23782 12,7.0800781 Z M 9,15 c 1.104569,0 "
     "2,0.671573 2,1.5 C 11,17.328427 10.104569,18 9,18 7.8954305,18 7,"
     "17.328427 7,16.5 7,15.671573 7.8954305,15 9,15 Z"},
};

// ITERATION MODE's icon since 2026-08-01 (architect-picked, replacing
// media-playlist-repeat): black_sum, the summation sigma — an iteration sweep is
// a SUM over cells, which the repeat arrows never said.
constexpr IconPath kBlackSumPaths[] = {
    {kIconText,
     "M 3 3 L 7 11 L 3 19 L 3.5 19 L 4 19 L 4.0625 19 L 4.5 19 L 19 19 L 19 16 "
     "L 19 15 L 18 15 L 18 18 L 14 18 L 13 18 L 12 18 L 5.65625 18 L 4.9375 18 "
     "L 8.25 11 L 4.8125 4 L 5.71875 4 L 12 4 L 16 4 L 18 4 L 18 6 L 18 7 L 19 "
     "7 L 19 6 L 19 3 L 4.5 3 L 4.0625 3 L 4 3 L 3.5 3 L 3 3 z "},
};

// FOLLOW MODE's icon since 2026-08-01 (architect-picked, replacing
// media-seek-forward): go-jump, the chevron with its destination dot — the
// playhead chase reads as GOING somewhere, not as fast-forwarding a transport.
constexpr IconPath kGoJumpPaths[] = {
    {kIconText,
     "M 5.7070312 3 L 5 3.7070312 L 11.125 9.8320312 L 12.292969 11 L 11.125 "
     "12.167969 L 5 18.292969 L 5.7070312 19 L 11.832031 12.875 L 13.707031 11 "
     "L 11.832031 9.125 L 5.7070312 3 z M 16 10 C 15.446 10 15 10.446 15 11 C "
     "15 11.554 15.446 12 16 12 C 16.554 12 17 11.554 17 11 C 17 10.446 16.554 "
     "10 16 10 z "},
};

constexpr IconPath kPreviewRenderOnPaths[] = {
    {kIconText,
     "M 11 3 C 6.568 3 3 6.568 3 11 C 3 15.432 6.568 19 11 19 C 15.432 19 19 "
     "15.432 19 11 C 19 6.568 15.432 3 11 3 z M 11 4 C 14.878 4 18 7.122 18 11 "
     "C 18 14.878 14.878 18 11 18 C 7.122 18 4 14.878 4 11 C 4 7.122 7.122 4 11 "
     "4 z M 11 8 L 11 14 L 15 11 L 11 8 z "},
    {kIconPreviewOn,
     "m 10,8 a 3,3 0 0 0 -3,3 3,3 0 0 0 3,3 l 0,-6 z"},
};

// THE ONE TRANSLATED PATH in the set — the file's own
// transform="translate(-364.57143 -525.79075)", carried as data so the `d`
// string stays byte-identical to dialog-ok-apply.svg.
constexpr IconPath kDialogOkApplyPaths[] = {
    {kIconText,
     "m382.8643 530.79077l-10.43876 10.56644-4.14699-4.19772-.70712.71578 "
     "4.14699 4.1977-.002.002.70713.71577.002-.002.002.002.70711-.71577-.002-.002 "
     "10.43877-10.56645-.70712-.71576z",
     -364.57143, -525.79075},
};

// THE READ-ONLY TAB'S PADLOCK, from track-head/lock.svg — one path, currentColor
// (the scheme's #fcfcfc, which is kIconText). Transcribed verbatim like every
// other entry; the file is committed beside it under assets/icons/breeze.
constexpr IconPath kLockPaths[] = {
    {kIconText,
     "M 11,3 C 8.784,3 7,4.784 7,7 l 0,4 -2,0 c 0,2.666667 0,5.333333 0,8 4,0 "
     "8,0 12,0 l 0,-8 c -0.666667,0 -1.333333,0 -2,0 L 15,7 C 15,4.784 13.216,3 "
     "11,3 m 0,1 c 1.662,0 3,1.561 3,3.5 L 14,11 8,11 8,7.5 C 8,5.561 9.338,4 "
     "11,4"},
};

// The OPEN padlock, the lock slot's unlocked state — actions/22/unlock.svg. Its
// shackle stands open to the left where lock.svg's closes over the body; the
// two are the same body, which is what makes them read as one control in two
// states rather than as two icons.
constexpr IconPath kUnlockPaths[] = {
    {kIconText,
     "m11 3c-2.216 0-4 1.784-4 4v1h1v-.5c0-1.939 1.338-3.5 3-3.5 1.662 0 3 "
     "1.561 3 3.5v3.5h-5-1-1-1-1v1 7h1 10 1v-8h-1-1v-4c0-2.216-1.784-4-4-4m-5 "
     "9h10v6h-10v-6"},
};

// The two ZOOM actions — actions/22/zoom-{out,in}.svg, the plain variants. They
// share every path byte but the last command (a bare minus bar, or that bar
// plus its crossing stroke), which is the Breeze family resemblance and not a
// transcription shortcut: each string is its own file, copied whole.
constexpr IconPath kZoomOutPaths[] = {
    {kIconText,
     "m11 3a8 8 0 0 0-8 8 8 8 0 0 0 8 8 8 8 0 0 0 4.892578-1.693359l3.400391 "
     "3.40039a1 1 0 0 0 1.414062 0 1 1 0 0 0 0-1.414062l-3.40039-3.400391a8 8 0 "
     "0 0 1.693359-4.892578 8 8 0 0 0-8-8zm0 1a7 7 0 0 1 7 7 7 7 0 0 1-7 7 7 7 "
     "0 0 1-7-7 7 7 0 0 1 7-7zm-4 6v2h8v-2h-8z"},
};

constexpr IconPath kZoomInPaths[] = {
    {kIconText,
     "m11 3a8 8 0 0 0-8 8 8 8 0 0 0 8 8 8 8 0 0 0 4.892578-1.693359l3.400391 "
     "3.40039a1 1 0 0 0 1.414062 0 1 1 0 0 0 0-1.414062l-3.40039-3.400391a8 8 0 "
     "0 0 1.693359-4.892578 8 8 0 0 0-8-8zm0 1a7 7 0 0 1 7 7 7 7 0 0 1-7 7 7 7 "
     "0 0 1-7-7 7 7 0 0 1 7-7zm-1 3v3h-3v2h3v3h2v-3h3v-2h-3v-3h-2z"},
};

constexpr IconDef kDocumentSave       {22.0, kDocumentSavePaths,        1};
constexpr IconDef kEditUndo           {22.0, kEditUndoPaths,            1};
constexpr IconDef kEditRedo           {22.0, kEditRedoPaths,            1};
constexpr IconDef kMediaRecord        {22.0, kMediaRecordPaths,         1};
constexpr IconDef kEditCopy           {22.0, kEditCopyPaths,            1};
constexpr IconDef kEditPaste          {22.0, kEditPastePaths,           1};
constexpr IconDef kMusicNote16th      {22.0, kMusicNote16thPaths,       1};
constexpr IconDef kBlackSum           {22.0, kBlackSumPaths,            1};
constexpr IconDef kGoJump             {22.0, kGoJumpPaths,              1};
constexpr IconDef kPreviewRenderOn    {22.0, kPreviewRenderOnPaths,     2};
constexpr IconDef kDialogOkApply      {22.0, kDialogOkApplyPaths,       1};
constexpr IconDef kLock               {22.0, kLockPaths,                1};
constexpr IconDef kUnlock             {22.0, kUnlockPaths,              1};
constexpr IconDef kZoomOut            {22.0, kZoomOutPaths,             1};
constexpr IconDef kZoomIn             {22.0, kZoomInPaths,              1};

const IconDef& icon_def(Icon icon) {
    switch (icon) {
        case Icon::DocumentSave:        return kDocumentSave;
        case Icon::EditUndo:            return kEditUndo;
        case Icon::EditRedo:            return kEditRedo;
        case Icon::MediaRecord:         return kMediaRecord;
        case Icon::EditCopy:            return kEditCopy;
        case Icon::EditPaste:           return kEditPaste;
        case Icon::MusicNote16th:       return kMusicNote16th;
        case Icon::BlackSum:            return kBlackSum;
        case Icon::GoJump:              return kGoJump;
        case Icon::PreviewRenderOn:     return kPreviewRenderOn;
        case Icon::Lock:                return kLock;
        case Icon::Unlock:              return kUnlock;
        case Icon::ZoomOut:             return kZoomOut;
        case Icon::ZoomIn:              return kZoomIn;
        case Icon::DialogOkApply:       break;
    }
    return kDialogOkApply;
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

    // ONE STDERR, DRAW NOTHING — and both halves are now literally true.
    //
    // VALIDATE EVERY PATH BEFORE FILLING ANY. The old loop parsed and filled
    // path by path, so a malformed LATER path of a multi-path icon left the
    // EARLIER ones already on the surface: a partial glyph, which is exactly
    // the "placeholder that lets the typo ship" the contract refuses. The
    // dry-run below parses each `d` into a scratch context and bails as a whole
    // before a single pixel is committed.
    //
    // AND SAY IT ONCE. `reported` latches per icon, so a transcription error is
    // one line at the first paint rather than one line per repaint forever —
    // a tripwire that floods is a tripwire nobody reads. Function-local static:
    // the GUI is single-threaded at every draw site.
    //
    // AND PROVE IT ONCE. The `d` strings are in-tree constexpr data, so the
    // verdict cannot change between calls — `validated` latches a PASSED probe
    // per icon, and later draws of that icon skip the scratch surface and the
    // dry-run parse entirely (the fill loop below re-walks the same constant
    // strings, which is the byte-identity the two-walk contract rests on). A
    // FAILED probe deliberately does not latch anything but its one stderr
    // line: the icon re-probes, re-fails and draws nothing on every call,
    // exactly as before. An out-of-range idx (a kIconCount mismatch) never
    // latches either — that icon simply pays the probe per draw, the same
    // "costs that icon its latch" degradation the header records for
    // `reported`.
    {
        static bool validated[kIconCount] = {};
        const int idx = static_cast<int>(icon);
        const bool latch_ok =
            idx >= 0 && idx < static_cast<int>(std::size(validated));
        if (!(latch_ok && validated[idx])) {
            cairo_surface_t* probe_surf =
                cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
            cairo_t* probe = cairo_create(probe_surf);
            bool ok = true;
            for (int i = 0; i < def.path_count && ok; ++i) {
                cairo_new_path(probe);
                ok = append_path(probe, def.paths[i].d);
            }
            cairo_destroy(probe);
            cairo_surface_destroy(probe_surf);
            if (!ok) {
                static bool reported[kIconCount] = {};
                if (latch_ok && !reported[idx]) {
                    reported[idx] = true;
                    std::fprintf(stderr,
                                 "icons: Malformed path data, icon %d not drawn\n",
                                 idx);
                }
                return;
            }
            if (latch_ok) validated[idx] = true;
        }
    }

    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size_px / def.view_box, size_px / def.view_box);
    for (int i = 0; i < def.path_count; ++i) {
        const IconPath& p = def.paths[i];
        // The path element's own translate, saved/restored around it so it
        // cannot leak into a sibling path (only one file uses one, but a
        // per-path transform that escaped its path would be a silent bug).
        cairo_save(cr);
        if (p.tx != 0.0 || p.ty != 0.0) cairo_translate(cr, p.tx, p.ty);
        cairo_new_path(cr);
        // Cannot fail: the dry run proved every path in this icon parses (on
        // this call, or on the earlier call whose pass `validated` latched),
        // and the strings are compile-time constants that cannot change between
        // the walks.
        append_path(cr, p.d);
        // The path's own color, retained by keep_own and made up with
        // mixed_with — the disabled face. keep_own == 1 (the default every
        // enabled caller takes) returns the table's color bit-identically, so
        // the enabled path is unchanged by the existence of this one.
        const GuiColor c = mix_color(p.fill, mixed_with, keep_own);
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    cairo_restore(cr);
}

} // namespace icons
