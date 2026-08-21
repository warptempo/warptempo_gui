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
// path elements in file order. `d` is copied VERBATIM from the file; `ink` is
// the color the file resolves to — the FILL source for an ordinary path and the
// STROKE source for a stroked one (`stroked`, below).
//
// NEARLY EVERY PATH HERE IS FILLED, and there are TWO STROKED FILES: boost's
// (2026-08-15, the bottom row's walk-both-tabs button), whose group carries
// `fill="none" stroke="currentColor"`, so its four open polylines would come
// out as four filled slivers under the fill arm — not a case the fill arm
// could have covered by looking the other way — and tool-rect-selection's
// (2026-08-16, the icon row's Show trim region button), the marching-ants
// selection rectangle. THE ARM IS RESTORED, NOT NEW: it lived in draw() for
// part of 2026-08-11 for the set's first stroked file (distortionfx, row 4's
// Warp radio for those hours), went producer-less when the architect
// reglyphed that button to speedometer the same day, and comes back verbatim
// plus one thing distortionfx never needed — a per-path LINE CAP, because two
// of boost's four paths carry `stroke-linecap="square"` and two take SVG's
// default butt. The general per-path MATRIX that was grown beside it did NOT
// come back: neither stroked file carries a transform, so that feature is
// still git history alone and this table's `xform` is still translates only.
//
// TOOL-RECT-SELECTION BROUGHT TWO MORE STROKE ATTRIBUTES AND ONE DEPARTURE,
// and the departure is the part worth reading twice (2026-08-16):
//   * A STROKE WIDTH THAT IS NOT 1. Its `stroke-width="1.043"` is the table's
//     first non-default, so the width is a per-path field now rather than the
//     literal boost takes. It is still in PATH UNITS and still set inside the
//     viewBox transform, so the pen scales with the geometry exactly as
//     before.
//   * A DASH. `stroke-dasharray="2.08599997,2.08599997"` with
//     `stroke-dashoffset="4.9125299"` is what makes it read as a SELECTION
//     rectangle rather than a plain box, so the dash is the glyph's whole
//     identity and not decoration. Both numbers are in path units like the
//     width, so cairo's CTM scales them with everything else.
//   * THE DEPARTURE: THIS FILE'S GEOMETRY IS A `<rect>`, NOT A `<path>`, so
//     there is no `d` string in it to copy and the property this table
//     otherwise holds — that a diff against the committed file is a
//     transcription bug and nothing else — DOES NOT HOLD FOR THIS ONE ROW.
//     What holds instead is weaker and is stated so a reader checks the right
//     thing: every NUMBER in the `d` below is a number in the file, in the
//     file's own spelling, laid out as `m x,y h width v height h -width z` —
//     the four rect attributes read in the order the element writes them. A
//     `<rect>` parser was weighed against this and declined: it would be a
//     second geometry vocabulary in the interpreter for one file, and the
//     four-number derivation is checkable by eye at the site, which the
//     interpreter's own coverage would not make truer.
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
// A FILLED ICON IS ONE FILL PER PATH ELEMENT, with cairo's default NONZERO
// winding rule — which is the SVG default too, and what makes document-save's
// holes (the body cutout and the lid slot) come out as holes: its subpaths wind
// against the outline. Filling subpath-by-subpath would flood them.
//
// `xform` carries the path element's own SVG `transform` attribute, applied
// around the path so `d` can stay VERBATIM — baking a transform into the
// numbers by hand would destroy the property that a diff between this table and
// the file is a transcription bug and nothing else. TWO committed files need
// one, and both are TRANSLATES: dialog-ok-apply.svg, whose author drew the
// check mark at its document coordinates and translated it back into the
// viewBox, and dialog-cancel.svg (row 8, 2026-08-11), whose `translate(-1-1)`
// spells the glued-negative form the SVG grammar admits. `icon_translate` is
// the one producer, named rather than raw so a translate READS as a translate
// at its site. (A general `icon_matrix` constructor lived here for part of
// 2026-08-11, for distortionfx's rotate-and-scale; it went with that file. The
// field itself still holds cairo's six components — icon_translate writes all
// six and draw() hands them to cairo_matrix_init — so a future `matrix(...)`
// needs the constructor back and nothing else.)
struct IconTransform {
    // Cairo's matrix components, which take SVG's matrix(a b c d e f) in that
    // exact argument order (a=xx, b=yx, c=xy, d=yy, e=x0, f=y0 in both).
    double xx = 1.0, yx = 0.0, xy = 0.0, yy = 1.0, x0 = 0.0, y0 = 0.0;
};

constexpr IconTransform icon_translate(double tx, double ty) {
    return IconTransform{1.0, 0.0, 0.0, 1.0, tx, ty};
}

struct IconPath {
    GuiColor      ink;      // fill source, or stroke source when `stroked`
    const char*   d;
    IconTransform xform{};  // identity unless the file carries a transform
    bool          stroked = false;
    // THE LINE CAP, read only on a stroked path. SVG's default is BUTT and so
    // is cairo's, so `false` transcribes a file that says nothing; boost's two
    // arrowhead paths say `stroke-linecap="square"` and are the flag's only
    // producers. It is a per-PATH attribute in the file and a per-path field
    // here for that reason — boost's other two paths take the default in the
    // same group.
    bool          square_cap = false;
    // THE STROKE WIDTH, read only on a stroked path, in PATH UNITS (2026-08-16).
    // SVG's default is 1 and boost's group says nothing, so 1.0 transcribes
    // "no stroke-width attribute"; tool-rect-selection's `stroke-width="1.043"`
    // is the field's one producer. Per-PATH like the cap, because the attribute
    // is per-element in SVG.
    double        stroke_width = 1.0;
    // THE DASH, read only on a stroked path, in PATH UNITS (2026-08-16).
    // `dash_on <= 0` means SOLID and transcribes a file with no
    // stroke-dasharray — which is every path but tool-rect-selection's, whose
    // `stroke-dasharray="2.08599997,2.08599997"` is a UNIFORM two-value array
    // and so needs exactly these two numbers plus the offset. A longer or
    // odd-length array would need a real array here; the two-value form is what
    // the one producer writes, and a third value would fail to transcribe
    // loudly rather than quietly, which is the right failure.
    double        dash_on     = 0.0;
    double        dash_off    = 0.0;
    double        dash_offset = 0.0;   // stroke-dashoffset
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

// THE SCHEME'S OTHER CLASS, and the only icon colour here that is not a literal
// written into its own file: `.ColorScheme-Accent`, which every Breeze file
// carrying it resolves to #3daee9 — deep-history's curl-back arrow is this
// tree's first and so far only user (2026-08-09). It is recorded the same way
// kIconText and kIconPreviewOn are, as THE VALUE THAT FILE RESOLVES TO, and it
// deliberately does NOT reference render.h's kRedesignAccent even though the two
// are the same 0x3DAEE9: that coincidence is shared Breeze ancestry, and the two
// would have to move independently if a crop ever disagreed with a scheme.
constexpr GuiColor kIconAccent    = hex(0x3DAEE9);

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

// THE CUMULATIVE READING'S ICON SINCE 2026-08-18 (architect, with the roster
// relayout): black_sum, the summation sigma — a CUMULATIVE delta is a sum over
// the walk's members, against the iterative reading's one step at a time, and
// the Σ says so outright where deep-history's swept clock only implied it.
// (It dressed ITERATION MODE from 2026-08-01, architect-picked then over
// media-playlist-repeat on the same reading — an iteration sweep is also a sum
// over cells. That slot took mathmode below in the same ruling; the sigma
// moved rather than being duplicated, so no two buttons wear one math symbol.)
constexpr IconPath kBlackSumPaths[] = {
    {kIconText,
     "M 3 3 L 7 11 L 3 19 L 3.5 19 L 4 19 L 4.0625 19 L 4.5 19 L 19 19 L 19 16 "
     "L 19 15 L 18 15 L 18 18 L 14 18 L 13 18 L 12 18 L 5.65625 18 L 4.9375 18 "
     "L 8.25 11 L 4.8125 4 L 5.71875 4 L 12 4 L 16 4 L 18 4 L 18 6 L 18 7 L 19 "
     "7 L 19 6 L 19 3 L 4.5 3 L 4.0625 3 L 4 3 L 3.5 3 L 3 3 z "},
};

// ITERATION MODE's icon since 2026-08-18 (architect, with the roster relayout,
// taking the slot the summation sigma left for the cumulative reading):
// mathmode — an italic f beside a multiplication cross, which reads as f(x).
// THE SLOT KEEPS A MATH SYMBOL and this one names the OPERATION: a render as a
// function of a variable swept across a bracket, which is what an iteration
// sweep is. Command coverage: absolute M / C / L / z with implicit
// absolute-lineto repetition, every family already committed here many times
// over.
constexpr IconPath kMathmodePaths[] = {
    {kIconText,
     "M 9 3 C 7.34315 3 6 4.3431 6 6 L 6 8 L 4 8 L 4 9 L 6 9 L 6 10 L 6 19 L "
     "7 19 L 7 9 L 8 9 L 9 9 L 9 8 L 8 8 L 7 8 L 7 6 C 7 4.89543 7.89543 4 9 4 "
     "L 10 4 L 10 3 L 9 3 z M 12.742188 13 L 12 13.732422 L 14.292969 16 L 12 "
     "18.267578 L 12.742188 19 L 15.035156 16.732422 L 17.257812 18.931641 L "
     "18 18.199219 L 15.775391 16 L 18 13.800781 L 17.257812 13.068359 L "
     "15.035156 15.267578 L 12.742188 13 z "},
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

// THE SET'S FIRST TRANSLATED PATH — the file's own
// transform="translate(-364.57143 -525.79075)", carried as data so the `d`
// string stays byte-identical to dialog-ok-apply.svg.
constexpr IconPath kDialogOkApplyPaths[] = {
    {kIconText,
     "m382.8643 530.79077l-10.43876 10.56644-4.14699-4.19772-.70712.71578 "
     "4.14699 4.1977-.002.002.70713.71577.002-.002.002.002.70711-.71577-.002-.002 "
     "10.43877-10.56645-.70712-.71576z",
     icon_translate(-364.57143, -525.79075)},
};

// -- ROW 4'S FOUR VIEW RADIOS (architect-picked 2026-08-11) --------------------
//
// The S/T audio pair and the W/P marker pair, which wore shaped LETTER GLYPHS
// from the row's first day until this pick (icons.h's enum carries the
// architect's metaphors and the runners-up; the letter arm died with them,
// having no producer left). Same rules as every entry above: `d` verbatim from
// the committed file, the colour hard-coded to what that file resolves to — all
// four are `.ColorScheme-Text` = #fcfcfc.
//
// CHRONOMETER-START'S STYLE BLOCK DEFINES `.ColorScheme-Accent` TOO and its one
// path never uses it (the path is `.ColorScheme-Text`), so nothing accent-
// coloured is missing from the entry below — stated here so a future diff
// against the file does not read the absence as a transcription bug. It is the
// only committed file that declares a class it does not use.
//
// COMMAND COVERAGE VERIFIED RATHER THAN ASSUMED, per the deep-history
// precedent: document-export and document-import are absolute M/L/Z only;
// chronometer-start is absolute M/L/C with lowercase `z` (and its trailing
// space, kept like document-save's); speedometer is absolute M/L/C plus
// fourteen absolute `A` arcs, every argument space-separated, with the same
// lowercase `z` and trailing space — the UPPERCASE arc is the one form worth
// checking there, and the parser has taken both cases of every command in the
// subset since it was written (media-record's `a` was simply the first
// producer) — so the strings needed nothing new from the parser.
constexpr IconPath kDocumentExportPaths[] = {
    {kIconText,
     "M 11 16 L 16.293 16 L 14 18.293 L 14.707 19 L 18.207 15.5 L 14.707 12 "
     "L 14 12.707 L 16.293 15 L 11 15 L 11 16 Z M 5 18 L 5 4 L 13 4 L 13 8 L "
     "17 8 L 17 13 L 18 13 L 18 7 L 14 3 L 4 3 L 4 19 L 13 19 L 13 18 L 5 18 "
     "Z"},
};

constexpr IconPath kDocumentImportPaths[] = {
    {kIconText,
     "M 4 3 L 4 19 L 11 19 L 11 18 L 5 18 L 5 4 L 13 4 L 13 8 L 17 8 L 17 15 "
     "L 12.707 15 L 15 12.707 L 14.293 12 L 10.793 15.5 L 14.293 19 L 15 "
     "18.293 L 12.707 16 L 18 16 L 18 7 L 14 3 L 4 3 Z"},
};

// THE WARP RADIO'S GAUGE — one path, one fill, no transform. Five subpaths:
// the dial's outer ring (a radius-8 circle with a radius-7 circle wound the
// other way inside it, so the nonzero rule leaves a ring, the construction
// document-save's holes take), two thick inner-arc bands that read as the
// scale, and the needle.
constexpr IconPath kSpeedometerPaths[] = {
    {kIconText,
     "M 11 3 A 8 8 0 0 0 3 11 A 8 8 0 0 0 11 19 A 8 8 0 0 0 19 11 A 8 8 "
     "0 0 0 11 3 z M 11 4 A 7 7 0 0 1 18 11 A 7 7 0 0 1 11 18 A 7 7 0 0 "
     "1 4 11 A 7 7 0 0 1 11 4 z M 11 6 A 5 5 0 0 0 7.6816406 7.2675781 "
     "L 8.390625 7.9765625 A 4 4 0 0 1 11 7 A 4 4 0 0 1 15 11 L 16 11 A "
     "5 5 0 0 0 11 6 z M 12.34375 8.1660156 L 10.197266 13.142578 C "
     "10.074466 13.282368 9.9865731 13.452179 9.9394531 13.636719 C "
     "9.9324531 13.666519 9.9249219 13.698216 9.9199219 13.728516 L "
     "9.9140625 13.742188 L 9.9199219 13.742188 C 9.8299219 14.303648 "
     "10.140852 14.828938 10.638672 14.955078 C 11.137452 15.080448 "
     "11.659373 14.764692 11.845703 14.226562 L 11.853516 14.228516 L "
     "11.853516 14.199219 C 11.861516 14.174519 11.870053 14.1501 "
     "11.876953 14.125 C 11.921853 13.94305 11.923313 13.752999 "
     "11.882812 13.574219 L 12.34375 8.1660156 z M 6.4746094 8.8886719 "
     "A 5 5 0 0 0 6 11 L 7 11 A 4 4 0 0 1 7.2382812 9.6523438 L "
     "6.4746094 8.8886719 z "},
};

constexpr IconPath kChronometerStartPaths[] = {
    {kIconText,
     "M 6.8769531 3 C 5.2125198 3.8561715 3.8561715 5.2125198 3 6.8769531 L "
     "3 7 L 3.921875 7.3066406 C 4.6764786 5.8567461 5.8567461 4.6764786 "
     "7.3066406 3.921875 L 7 3 L 6.8769531 3 z M 15.005859 3 L 14.699219 "
     "3.921875 C 16.149109 4.676485 17.329374 5.8567506 18.083984 7.3066406 "
     "L 19.005859 7 L 19.005859 6.8769531 C 18.149689 5.2125231 16.793336 "
     "3.85617 15.128906 3 L 15.005859 3 z M 11 5 C 7.1220048 5 4 8.1220048 4 "
     "12 C 4 15.877995 7.1220048 19 11 19 C 14.877995 19 18 15.877995 18 12 "
     "C 18 8.1220048 14.877995 5 11 5 z M 11 6 C 14.323996 6 17 8.676004 17 "
     "12 C 17 15.323996 14.323996 18 11 18 C 7.676004 18 5 15.323996 5 12 C "
     "5 8.676004 7.676004 6 11 6 z M 9 9 L 9 15 L 14 12 L 9 9 z "},
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

// The OPEN padlock, the read-only toggle's unlocked state —
// actions/22/unlock.svg. Its
// shackle stands open to the left where lock.svg's closes over the body; the
// two are the same body, which is what makes them read as one control in two
// states rather than as two icons.
constexpr IconPath kUnlockPaths[] = {
    {kIconText,
     "m11 3c-2.216 0-4 1.784-4 4v1h1v-.5c0-1.939 1.338-3.5 3-3.5 1.662 0 3 "
     "1.561 3 3.5v3.5h-5-1-1-1-1v1 7h1 10 1v-8h-1-1v-4c0-2.216-1.784-4-4-4m-5 "
     "9h10v6h-10v-6"},
};

// -- THE TWO VCS ICONS (2026-08-04) --------------------------------------------
//
// vcs-commit is the RENDER BUTTON's face while the history mode stands (the
// chord commits a checkpoint there instead of rendering) and vcs-diff is the
// history button's own, the icon row's twelfth. Same rules as every entry above:
// `d` verbatim from the committed file, the fill hard-coded to what that file
// resolves to.
//
// BOTH FILES WRAP THEIR PATHS DIFFERENTLY AND IT MAKES NO DIFFERENCE HERE:
// vcs-diff carries `class="ColorScheme-Text" fill="currentColor"` on each path
// element, vcs-commit carries the identical pair ONCE on a `<g>` that encloses
// all three of its paths. A group attribute is inherited by the children and
// nothing else, so both resolve to the same #fcfcfc per path — the `<g>` is a
// spelling, not a transform, and there is nothing about it to model (the files
// that DO carry one are inventoried at IconPath's `xform`, above).
constexpr IconPath kVcsCommitPaths[] = {
    {kIconText, "m10 4h1v5h-1z"},
    {kIconText, "m10 14h1v5h-1z"},
    {kIconText,
     "m10.5 8a3.5 3.5 0 0 0 -3.5 3.5 3.5 3.5 0 0 0 3.5 3.5 3.5 3.5 0 0 0 "
     "3.5-3.5 3.5 3.5 0 0 0 -3.5-3.5zm0 1a2.5 2.5 0 0 1 2.5 2.5 2.5 2.5 0 0 1 "
     "-2.5 2.5 2.5 2.5 0 0 1 -2.5-2.5 2.5 2.5 0 0 1 2.5-2.5z"},
};

constexpr IconPath kVcsDiffPaths[] = {
    {kIconText,
     "m5.5 4a2.5 2.5 0 0 0-2.5 2.5 2.5 2.5 0 0 0 2.5 2.5 2.5 2.5 0 0 0 "
     "2.5-2.5 2.5 2.5 0 0 0-2.5-2.5zm0 1a1.5 1.5 0 0 1 1.5 1.5 1.5 1.5 0 0 "
     "1-1.5 1.5 1.5 1.5 0 0 1-1.5-1.5 1.5 1.5 0 0 1 1.5-1.5z"},
    {kIconText,
     "m5 8v6a2 2 0 0 0 1.9511719 2 2 2 0 0 0 0.0488281 0h4v-1h-4a1 1 0 0 "
     "1-1-1v-6z"},
    {kIconText,
     "m8.5 11.792969-0.7070312 0.707031 0.3535156 0.353516 2.6464846 "
     "2.646484-2.6464846 2.646484-0.3535156 0.353516 0.7070312 0.707031 "
     "0.3535156-0.353515 3.3535154-3.353516-3.3535154-3.353516-0.3535156-0.353515z"},
    {kIconText,
     "m15.5 18a2.5 2.5 0 0 0 2.5-2.5 2.5 2.5 0 0 0-2.5-2.5 2.5 2.5 0 0 0-2.5 "
     "2.5 2.5 2.5 0 0 0 2.5 2.5zm0-1a1.5 1.5 0 0 1-1.5-1.5 1.5 1.5 0 0 1 "
     "1.5-1.5 1.5 1.5 0 0 1 1.5 1.5 1.5 1.5 0 0 1-1.5 1.5z"},
    {kIconText,
     "m16 14v-6.0000002a2 2 0 0 0-1.951172-2 2 2 0 0 0-0.04883 "
     "0h-3.9999981v1h4.0000001a1 1 0 0 1 1 1v6.0000002z"},
    {kIconText,
     "M 12.5 2.7929688 L 12.146484 3.1464844 L 8.7929688 6.5 L 12.146484 "
     "9.8535156 L 12.5 10.207031 L 13.207031 9.5 L 12.853516 9.1464844 L "
     "10.207031 6.5 L 12.853516 3.8535156 L 13.207031 3.5 L 12.5 2.7929688 z "},
};

// -- THE WALK'S TWO CHEVRONS (2026-08-05) --------------------------------------
//
// go-previous and go-next, the icon row's older / newer checkpoint buttons.
// Transcribed verbatim like every entry above, from the committed
// go-previous.svg / go-next.svg, and both resolve to the scheme's #fcfcfc.
//
// EACH IS ONE OUTLINE PATH, not a stroked line: Breeze draws the chevron as a
// closed shape whose two limbs are one unit thick at the viewBox's own scale,
// exactly as go-jump's does (they are the same drawing, go-jump's carrying its
// destination dot as a second subpath). So the line weight scales with
// gui_scale like every other geometry in this table, with no stroke width to
// set and nothing that could fatten at 200%.
//
// THEY ARE MIRROR IMAGES and their `d` strings are NOT mirrors of each other:
// each file walks its own outline from its own start point, so neither is
// derived from the other here — both are copied, which is what keeps a diff
// against the files a transcription bug and nothing else.
constexpr IconPath kGoPreviousPaths[] = {
    {kIconText,
     "m14.292969 3l-6.125 6.125-1.875 1.875 1.875 1.875 6.125 "
     "6.125.707031-.707031-6.125-6.125-1.167969-1.167969 1.167969-1.167969 "
     "6.125-6.125-.707031-.707031"},
};

constexpr IconPath kGoNextPaths[] = {
    {kIconText,
     "m7.707031 3l-.707031.707031 6.125 6.125 1.167969 1.167969-1.167969 "
     "1.167969-6.125 6.125.707031.707031 6.125-6.125 1.875-1.875-1.875-1.875-6.125-6.125"},
};

// -- THE WALK'S TWO ARROWS, SINCE 2026-08-11 -----------------------------------
//
// keyframe-previous and keyframe-next, the icon row's older / newer checkpoint
// buttons. Transcribed verbatim from the committed keyframe-previous.svg /
// keyframe-next.svg, and both resolve to the scheme's #fcfcfc.
//
// THEY TOOK THE PAIR OVER FROM go-previous / go-next, which the walk had worn
// since 2026-08-05 and which now serve row 8's left and right arrows alone (an
// Icon is a GLYPH, not a button — the two entries above are unchanged and
// simply have one consumer each again). The architect's reason is the group's
// own vocabulary: its neighbour Deep-History is a CLOCK, so the walk's steps
// should read as clock steps rather than as bare direction. Breeze's
// keyframe-previous / keyframe-next are exactly that — a stopwatch dial with a
// solid triangle pointing into the past or the future — and they are the
// theme's ONLY DIRECTIONAL CLOCK PAIR (planner survey; the runners-up were
// chronometer-start / chronometer-reset, document-open-recent and
// edit-undo-history, and none of them is directional as a pair, which is what
// an older / newer step needs above everything else).
//
// ONE PATH EACH, dial and triangle together, so both are single-colour like
// every entry above and unlike Deep-History's two. Their `d` uses m/v/h/c/s/l
// (the lineto implicit after `m`) and `z` — all of it already in the
// interpreter, document-revert having been the `s` that grew it, so neither
// string was flattened by hand.
constexpr IconPath kKeyframePreviousPaths[] = {
    {kIconText,
     "m11 5v1h2v1.0507812c-2.237959 0.2537455-4 2.1467332-4 4.4492188 0 "
     "2.473437 2.026563 4.5 4.5 4.5s4.5-2.026563 "
     "4.5-4.5c0-1.059095-0.385401-2.0209801-1.005859-2.7871094l0.755859-0.755859 "
     "0.396484 0.396484 0.707031-0.7089844-1.501953-1.4980469-0.705078 "
     "0.7070313 0.396485 0.396485-0.751953 "
     "0.7519525c-0.646025-0.510828-1.432052-0.8537803-2.291016-0.9511719v-1.0507812h2v-1zm-4 "
     "1-4 5 4 5zm6.5 2c1.932997 0 3.5 1.5670034 3.5 3.5 0 1.932997-1.567003 "
     "3.5-3.5 3.5s-3.5-1.567003-3.5-3.5c0-1.9329966 1.567003-3.5 3.5-3.5z"},
};

constexpr IconPath kKeyframeNextPaths[] = {
    {kIconText,
     "m5 5v1h2v1.0507812c-2.2379593 0.2537455-4 2.1467332-4 4.4492188 0 "
     "2.473437 2.0265633 4.5 4.5 4.5 2.473437 0 4.5-2.026563 4.5-4.5 "
     "0-1.059095-0.385401-2.0209801-1.005859-2.7871094l0.754297-0.7542968 "
     "0.398046 0.3949218 0.707031-0.7089844-1.501953-1.4980469-0.705078 "
     "0.7070313 0.394922 0.3980469-0.75039 "
     "0.7503906c-0.6460254-0.510828-1.432052-0.8537803-2.291016-0.9511719v-1.0507812h2v-1zm10 "
     "1v10l4-5zm-7.5 2c1.932997 0 3.5 1.5670034 3.5 3.5 0 1.932997-1.567003 "
     "3.5-3.5 3.5-1.9329966 0-3.5-1.567003-3.5-3.5 0-1.9329966 1.5670034-3.5 "
     "3.5-3.5z"},
};

// -- THE REVERT ACT'S GLYPH (2026-08-05) ---------------------------------------
//
// document-revert, the history group's third button: the checkpoint's own
// differences applied backwards into the live state. Transcribed verbatim from
// the committed document-revert.svg like every entry above, and it resolves to
// the scheme's #fcfcfc.
//
// ONE PATH, and it is the FIRST committed file to use the SMOOTH CUBIC (`s`) —
// twice, for the two lobes of the arrow's return curve. The interpreter grew
// that command for this file rather than the string being flattened to plain
// `c` here: a hand-computed reflection would put numbers in this table that are
// in no file, which is exactly the transcription-bug-and-nothing-else property
// the table's contract rests on.
constexpr IconPath kDocumentRevertPaths[] = {
    {kIconText,
     "m4 3v16h11c1.662 0 3-1.338 3-3s-1.338-3-3-3h-1.292969l1.5-1.5-.707031-.707031-2.707031 "
     "2.707031 2.707031 2.707031.707031-.707031-1.5-1.5h1.292969c1.108 0 2 .892 "
     "2 2s-.892 2-2 2h-10v-14h8v4h4v4h1v-5l-4-4z"},
};

// -- THE CUMULATIVE READING'S GLYPH (architect 2026-08-09) ---------------------
//
// deep-history: a CLOCK FACE WITH A CURL-BACK ARROW sweeping around it,
// reaching back across the whole span of history at once.
//
// IT DRESSED THE CUMULATIVE READING'S TOGGLE (bare `u`) from 2026-08-09 until
// 2026-08-18, when the architect's roster relayout gave that toggle the
// SUMMATION SIGMA instead — a cumulative delta is a sum over the walk's
// members, and the Σ names the arithmetic where this dial only implied it.
// THE GLYPH IS FREED RATHER THAN RETIRED: the sweep across a whole COMMITTED
// history is what the history view's Git walk radio wants, and it takes this
// row. Nothing paints it for the span between the two commits, which is
// expected and is why the row and its asset stay put.
//
// THE SET'S FIRST TWO-COLOR ICON, and the architect chose it knowing that: path
// 1 is the clock body and its hands in `.ColorScheme-Text`, path 2 the curl-back
// arrow in `.ColorScheme-Accent`, so the arrow reads as the ACT over the dial it
// sweeps. preview-render-on and media-record are two-COLORED but each carries
// its second colour as a literal of its own; this is the first file whose second
// colour comes from the scheme's OTHER class. Both literals are hard-coded here
// from the file's own stylesheet, per the media-record precedent: kIconAccent is
// #3daee9 because THIS FILE says so, not because the redesign's accent happens
// to be the same value (it is, by shared Breeze ancestry — see kIconAccent).
//
// Transcribed VERBATIM from the committed file like every entry above. TWO
// PATHS, one fill each, which is the table's existing shape and needed no change
// anywhere. The `style="fill:currentColor;fill-opacity:1;stroke:none"` form both
// paths use is the same one go-next, black_sum and document-save carry: this
// table records only the `d` and the colour the file RESOLVES to, so the style
// attribute has nothing to transcribe — and its fill-opacity is an explicit 1,
// fully opaque, not a compositing alpha the palette rule would refuse.
//
// COMMAND COVERAGE VERIFIED RATHER THAN ASSUMED, both paths against the subset
// above: path 1 is m/c/v/h plus implicit repetition, path 2 is m/v/h/a with the
// arc argument set repeating. Every family already has a committed producer —
// relative cubics in edit-undo, edit-redo, unlock and document-revert, arcs in
// media-record, preview-render-on, vcs-commit and vcs-diff — and NEITHER PATH
// CLOSES WITH `z`, which is likewise already the case in six committed files
// (edit-undo, edit-redo, go-next, go-previous, lock, unlock): a fill closes the
// subpath implicitly, in cairo as in SVG. So nothing here asked the interpreter
// for anything new.
//
// THE SUCCESSION, recorded so none of it is re-proposed without a new ruling.
// office-chart-area was tried first and rejected: its rising area is TWO paths,
// the second at fill-opacity 0.5, and this product composites nothing — rendered
// opaque the two merge into a solid block, and its full-opacity path alone is an
// outlined zigzag that smudges at the row's 22 px. view-sort-ascending was the
// licensed fallback and could not be transcribed: six sort bars plus an arrow
// under a `transform="matrix(...)"`, and the interpreter models TRANSLATE ONLY
// (it grew a general matrix for distortionfx for part of 2026-08-11 and lost it
// again with that file, so the obstacle stands as it did — which changes
// nothing about a candidate the architect has already passed over). An AUTHORED
// three-ascending-bars glyph was then written in
// office-chart-bar's construction and REVERTED the same day (2026-08-08), the
// architect preferring a real Breeze file to an in-tree drawing. Its replacement,
// office-chart-line-forecast, shipped and was RETIRED at the architect's live
// pass on 2026-08-09: CHOPPY at row size, its dashes and thin axis breaking up.
// deep-history is his pick off a rendered candidate sheet; PIN and
// LAYER-VISIBLE-ON were the considered runners-up on that sheet and are not to
// be re-proposed without a new ruling.
constexpr IconPath kDeepHistoryPaths[] = {
    {kIconText,
     "m11 3c-4.431998 0-8 3.568002-8 8 0 4.431998 3.568002 8 8 8 1.896399"
     " 0 3.632791-.656291 5-1.751953v-1.248047h-.09375c-1.261408 "
     "1.237774-2.99118 2-4.90625 2-3.877999 0-7-3.122001-7-7 0-3.877999 "
     "3.122001-7 7-7 3.877999 0 7 3.122001 7 7 0 .696167-.105435 "
     "1.366247-.292969 "
     "2h1.033203c.163714-.639651.259766-1.307916.259766-2 "
     "0-4.431998-3.568002-8-8-8m-1 2v7h1 5v-1h-5v-6h-1"},
    {kIconAccent,
     "m13.65625 14v1h.845703 1.998047a1.5 1.5 0 0 1 1.5 1.5 1.5 1.5 0 0 1"
     " -1.5 1.5v1a2.5 2.5 0 0 0 2.5 -2.5 2.5 2.5 0 0 0 -2.5 -2.5h-2.84375"},
};

// DEEP-HISTORY'S SHALLOW SIBLING (2026-08-18), for the history view's SESSION
// walk radio: the same clock face and hands with NO sweep arrow at all — the
// session's own undo/redo timeline reaches back no further than this run,
// which is exactly what the missing sweep says beside the Git walk's swept
// dial. Taking the PAIR is the point: two readings of one surface should be
// one construction seen twice, not two unrelated pictures.
//
// SINGLE-COLOUR, unlike its sibling: the file has ONE `.ColorScheme-Text` path
// and no `.ColorScheme-Accent` at all, so deep-history is still the set's one
// two-colour glyph. Command coverage: relative `m` with `c`, `v` and `h` plus
// implicit repetition and no closing `z` — path 1 of deep-history's own
// families, verbatim.
constexpr IconPath kShallowHistoryPaths[] = {
    {kIconText,
     "m11 3c-4.431998 0-8 3.568002-8 8 0 4.431998 3.568002 8 8 8 4.431998 "
     "0 8-3.568002 8-8 0-4.431998-3.568002-8-8-8m0 1c3.877999 0 7 3.122001 7 "
     "7 0 3.877999-3.122001 7-7 7-3.877999 0-7-3.122001-7-7 0-3.877999 "
     "3.122001-7 7-7m-1 1v7h1 5v-1h-5v-6h-1"},
};

// THE ADD-TO-SELECTION ACT'S GLYPH (2026-08-18): edit-select, the pointer
// arrow with its own grab dot — picking one more thing up, which is what the
// act does to a standing selection.
//
// Command coverage: absolute `M` / `A` / `L` with implicit repetition on both
// the arc run (four quarter-arcs spelling the dot) and the lineto run, and no
// closing `z` on either subpath — the fill closes implicitly, six committed
// files' precedent. Arcs are implemented generally here, so the four circular
// ones cost nothing new.
constexpr IconPath kEditSelectPaths[] = {
    {kIconText,
     "M6 3A1 1 0 0 0 5 4 1 1 0 0 0 6 5 1 1 0 0 0 7 4 1 1 0 0 0 6 3M7 6L7.00586 "
     "19 10.900391 14.300781 17 14 7 6"},
};

// THE MARKER MEASURE ACT'S GLYPH: minuet-scales, KDE Minuet's own icon —
// three note heads climbing a five-line staff. THE ARCHITECT PICKED IT
// 2026-08-20, replacing edit-comment's speech balloon, which had been right for
// the ONE DAY the field was a free-text comment and read wrong the moment it
// became a strict measure grammar: the button names a place in the SCORE now,
// so its glyph says notes on staff lines rather than something somebody said.
// edit-comment's def and its committed asset are DELETED with the swap, no
// button being left that wears it.
//
// PROVENANCE, per the theme-provenance rule: breeze-dark's
// actions/22/minuet-scales.svg, a REAL FILE and not a symlink (unlike
// edit-comment, which pointed at dialog-messages.svg — the resolution note that
// stood here belonged to that file and goes with it). The committed
// assets/icons/breeze/minuet-scales.svg is that install's bytes verbatim, the
// edit-select precedent, so a diff between this table and that asset is a
// transcription bug and nothing else. Breeze LIGHT carries the identical `d`
// under #232629 ink; the dark one is the source because #fcfcfc is what this
// roster's kIconText already is.
//
// IT NEEDS NO TRANSLATE, AND THAT IS WORTH SAYING because the file appears to
// carry two: the `<g>` wraps everything in `translate(0 -1030.4)` and the path
// answers with `translate(0,1030.4)`, which cancel EXACTLY. The geometry is
// already in viewBox coordinates, so transcribing either one alone would move
// the glyph a thousand units off the tile. The three files that genuinely need
// the field are unchanged (dialog-ok-apply, dialog-cancel, and — until today —
// edit-comment).
//
// Command coverage: relative `m` with `h`, `v`, `a` and `z` plus implicit
// repetition, glued arc flags ("0 0 0-0.365" is three flags and an x), and
// EXPONENT NOTATION — `8e-3`, `2e-3`, `4e-3` — which is this file's own
// contribution to the subset (parse_number carries the record; the alternative
// was re-spelling six numbers by hand and losing the verbatim invariant).
constexpr IconPath kMinuetScalesPaths[] = {
    {kIconText,
     "m16 3v2h-13v1h13v1.6523a1.9977 1.9977 0 0 0-0.365-0.1523l-8e-3 "
     "-2e-3a1.9977 1.9977 0 0 0-2.488 1.334 1.9977 1.9977 0 0 0-0.041 "
     "0.168h-0.098v-2h-1v2h-9v1h9v1.652a1.9977 1.9977 0 0 0-0.365-0.152l-8e-3 "
     "-2e-3a1.9977 1.9977 0 0 0-2.4883 1.334 1.9977 1.9977 0 0 0-0.041 "
     "0.168h-0.0977v-2h-1v2h-5v1h5v1.654a1.9977 1.9977 0 0 "
     "0-0.3652-0.154l-0.0078-2e-3a1.9977 1.9977 0 0 0-2.4883 1.334 1.9977 "
     "1.9977 0 0 0-0.041 0.168h-2.0977v1h2.1445a1.9977 1.9977 0 0 0 1.3262 "
     "1.322 1.9977 1.9977 0 0 0 2.4902-1.322h10.0391v-1h-9.9961a1.9977 1.9977 "
     "0 0 0-0.0039-0.018v-2.982h0.1445a1.9977 1.9977 0 0 0 1.3285 1.322 1.9977 "
     "1.9977 0 0 0 2.488-1.322h6.039v-1h-5.996a1.9977 1.9977 0 0 0-4e-3 "
     "-0.018v-2.982h0.145a1.9977 1.9977 0 0 0 1.328 1.322 1.9977 1.9977 0 0 0 "
     "2.488-1.322h2.039v-1h-1.996a1.9977 1.9977 0 0 0-4e-3 "
     "-0.0176v-2.9824h2v-1h-2v-2h-1zm-0.943 5.457a0.97972 0.97972 0 0 1 "
     "0.255 0.0352l0.026 0.0078a0.97972 0.97972 0 0 1 0.652 1.2168 0.97972 "
     "0.97972 0 0 1-1.213 0.6602 0.97972 0.97972 0 0 1-0.668-1.209 0.97972 "
     "0.97972 0 0 1 0.948-0.711zm-4 4a0.97972 0.97972 0 0 1 0.255 "
     "0.035l0.026 8e-3a0.97972 0.97972 0 0 1 0.652 1.217 0.97972 0.97972 0 0 "
     "1-1.213 0.66 0.97972 0.97972 0 0 1-0.668-1.209 0.97972 0.97972 0 0 1 "
     "0.948-0.711zm-4.0004 4a0.97972 0.97972 0 0 1 0.2559 0.035l0.0254 "
     "8e-3a0.97972 0.97972 0 0 1 0.6523 1.217 0.97972 0.97972 0 0 1-1.2129 "
     "0.66 0.97972 0.97972 0 0 1-0.6679-1.209 0.97972 0.97972 0 0 1 "
     "0.9472-0.711z"},
};

// -- Row 8's seven (2026-08-11, the transport row) -----------------------------
//
// Same rules as every entry above: `d` verbatim from the committed file, the
// fill hard-coded to what that file resolves to — all seven are pure
// `.ColorScheme-Text` = #fcfcfc. The four cardinal arrows are completed by
// GoUp / GoDown below plus the REUSED kGoPrevious / kGoNext already in this
// table (left and right — a def is a glyph, and several buttons may wear one).
//
// COMMAND COVERAGE VERIFIED RATHER THAN ASSUMED, per the deep-history
// precedent, each path against the subset below: the three media-skip /
// playback-start strings are m/v/h/l with implicit lineto after m and
// implicit repetition ("m2 8 10 8" is a moveto and a relative lineto);
// media-playback-stop is m/h/v; dialog-cancel is m/c/l with long implicit
// cubic runs and chained leading-dot decimals (".22478-.375" splits on the
// second dot — the parser's own rule, with committed producers since
// edit-undo); go-down is m/l relative; go-up is absolute M/L with implicit
// absolute-lineto repetition ("L11 7.707 9.832 8.875 3.707 15" is three
// linetos). go-down, go-up and dialog-cancel close no subpath with `z` —
// the fill closes implicitly, six committed files' precedent.
//
// DIALOG-CANCEL IS THE SECOND TRANSLATED PATH in the set (dialog-ok-apply is
// the first): the file's own transform="translate(-1-1)" — the SVG grammar
// admits the glued negative — carried as tx/ty data so the `d` stays
// byte-identical to the committed file.

constexpr IconPath kMediaSkipBackwardPaths[] = {
    {kIconText,
     "m0 3v16h2v-16zm2 8 10 8v-16zm10 0 10 8v-16z"},
};

constexpr IconPath kMediaPlaybackStartPaths[] = {
    {kIconText,
     "m3 3v16l16-8z"},
};

constexpr IconPath kMediaPlaybackStopPaths[] = {
    {kIconText,
     "m3 3h16v16h-16z"},
};

constexpr IconPath kMediaSkipForwardPaths[] = {
    {kIconText,
     "m0 3v16l10-8zm10 8v8l10-8-10-8zm10 0v8h2v-16h-2z"},
};

constexpr IconPath kDialogCancelPaths[] = {
    {kIconText,
     "m12 4c-2.027598 0-3.87132.756694-5.28125 2-.126239.11132-.25603.22478"
     "-.375.34375l-.34375.375c-1.243306 1.40993-2 3.253652-2 5.28125 0 4.41828 "
     "3.58172 8 8 8 2.027598 0 3.87132-.756694 5.28125-2l.375-.34375c.11897"
     "-.11897.23243-.248761.34375-.375 1.243306-1.40993 2-3.253652 2-5.28125 "
     "0-4.41828-3.58172-8-8-8m0 1c3.86599 0 7 3.13401 7 7 0 1.75366-.653215 "
     "3.334268-1.71875 4.5625l-9.84375-9.84375c1.228231-1.065535 2.80884"
     "-1.71875 4.5625-1.71875m-5.28125 2.4375l9.84375 9.84375c-1.228232 "
     "1.065535-2.80884 1.71875-4.5625 1.71875-3.86599 0-7-3.13401-7-7 "
     "0-1.75366.653215-3.334269 1.71875-4.5625",
     icon_translate(-1.0, -1.0)},
};

constexpr IconPath kGoDownPaths[] = {
    {kIconText,
     "m3.707031 7l-.707031.707031 6.125 6.125 1.875 1.875 1.875-1.875 6.125"
     "-6.125-.707031-.707031-6.125 6.125-1.167969 1.167969-1.167969-1.167969"
     "-6.125-6.125"},
};

constexpr IconPath kGoUpPaths[] = {
    {kIconText,
     "M3.707 15L3 14.293l6.125-6.125L11 6.293l1.875 1.875L19 14.293l-.707.707"
     "-6.125-6.125L11 7.707 9.832 8.875 3.707 15"},
};

// (EDIT-CUT, THE TRIM SCISSORS, IS DELETED — 2026-08-18, with its button:
// the architect's roster relayout retired the "set trim from region" BUTTON,
// the CHORD bare `x` untouched, which left this row with no consumer at all.
// The enumerator, this transcription and assets/icons/breeze/edit-cut.svg went
// together rather than the table carrying an unpainted glyph. It served the
// trim button from 2026-08-11 and was the architect's own pick over the first
// cut's planner-picked transform-crop; both are git history.)

// THE SHOW TRIM REGION BUTTON'S GLYPH (architect 2026-08-16, the icon row's
// one viewport-class group, alone in it since the scissors left on
// 2026-08-18): TOOL-RECT-SELECTION, the
// MARCHING-ANTS rectangle — a dashed box is the universal "here is a selected
// span" mark, and this button's whole job is to put a selectable span on the
// waveform. THE ARCHITECT NAMED THE 24px FILE and this is the 22px one: the
// two are the SAME rectangle (24 wraps it in `translate(1,1)` inside a 24
// viewBox), 22 is every other row in this table, and taking 24 would have
// bought a viewBox exception and a transform for nothing.
//
// IT IS THE TABLE'S ONE `<rect>` FILE, so the `d` below is a DERIVATION rather
// than a verbatim copy and the header states exactly what survives of the
// verbatim property. The rect's four attributes, in the element's own order and
// spelling: x=2.5215156, y=3.5311673, width=16.952848, height=14.931264 — laid
// out as a relative move to the corner, then across, down, back, close. The
// closing `z` is what draws the fourth side, so the file's four sides are four
// sides here too and no number is repeated.
//
// STROKED, and every stroke attribute comes off the element: `stroke-width`
// 1.043 (the table's first non-default), `stroke-dasharray` 2.08599997 on /
// 2.08599997 off and `stroke-dashoffset` 4.9125299. The default BUTT cap
// (nothing is said in the file) is what puts a clean dash end at each corner.
// The 22px file's own `stroke-miterlimit` is absent (its 16px sibling carries
// one); the arm's SVG-default 4 is what a miter join takes anyway, and a dashed
// rectangle has no joins left to miter.
constexpr IconPath kToolRectSelectionPaths[] = {
    {kIconText,
     "m2.5215156,3.5311673 h16.952848 v14.931264 h-16.952848 z",
     {}, /*stroked=*/true, /*square_cap=*/false,
     /*stroke_width=*/1.043,
     /*dash_on=*/2.08599997, /*dash_off=*/2.08599997,
     /*dash_offset=*/4.9125299},
};

// -- THE ZOOM GROUP'S FOUR (architect-picked 2026-08-12, the grand relayout's
// roster commit) -------------------------------------------------------------
//
// Breeze's magnifier family, transcribed byte-verbatim from breeze-dark's
// actions/22/: zoom-in, zoom-out, zoom-fit-best and zoom-original share one
// magnifier construction (the 8/7 double circle ring — media-record's nonzero
// hole idiom — plus the handle's rounded 1x1 arc stub) and differ in the
// dial's content: a plus, a minus, the fit frame, and the 1:1 corner-arrow
// dial. All four are single `.ColorScheme-Text` paths resolving to #fcfcfc,
// relative m/l/h/v with `a` arcs, glued negative-after-flag arc arguments
// ("0 0-8 8" — a flag is one digit, media-record's own producer form) and
// implicit repetition; every family has a committed producer already, so
// nothing here asked the interpreter for anything new. (ZoomOut and ZoomIn
// existed 2026-08-01..02 as different transcriptions and were deleted WHOLE
// with their buttons; these are fresh copies of today's files, not revivals.)

constexpr IconPath kZoomInPaths[] = {
    {kIconText,
     "m11 3a8 8 0 0 0-8 8 8 8 0 0 0 8 8 8 8 0 0 0 "
     "4.892578-1.693359l3.400391 3.40039a1 1 0 0 0 1.414062 0 1 1 0 0 0 "
     "0-1.414062l-3.40039-3.400391a8 8 0 0 0 1.693359-4.892578 8 8 0 0 "
     "0-8-8zm0 1a7 7 0 0 1 7 7 7 7 0 0 1-7 7 7 7 0 0 1-7-7 7 7 0 0 1 "
     "7-7zm-1 3v3h-3v2h3v3h2v-3h3v-2h-3v-3h-2z"},
};

constexpr IconPath kZoomOutPaths[] = {
    {kIconText,
     "m11 3a8 8 0 0 0-8 8 8 8 0 0 0 8 8 8 8 0 0 0 "
     "4.892578-1.693359l3.400391 3.40039a1 1 0 0 0 1.414062 0 1 1 0 0 0 "
     "0-1.414062l-3.40039-3.400391a8 8 0 0 0 1.693359-4.892578 8 8 0 0 "
     "0-8-8zm0 1a7 7 0 0 1 7 7 7 7 0 0 1-7 7 7 7 0 0 1-7-7 7 7 0 0 1 "
     "7-7zm-4 6v2h8v-2h-8z"},
};

constexpr IconPath kZoomFitBestPaths[] = {
    {kIconText,
     "m11 3a8 8 0 0 0-8 8 8 8 0 0 0 8 8 8 8 0 0 0 "
     "4.892578-1.693359l3.400391 3.40039a1 1 0 0 0 1.414062 0 1 1 0 0 0 "
     "0-1.414062l-3.40039-3.400391a8 8 0 0 0 1.693359-4.892578 8 8 0 0 "
     "0-8-8zm0 1a7 7 0 0 1 7 7 7 7 0 0 1-7 7 7 7 0 0 1-7-7 7 7 0 0 1 "
     "7-7zm-4 3v1 6 1h8v-1-6-1h-8zm1 1h6v6h-6v-6z"},
};

constexpr IconPath kZoomOriginalPaths[] = {
    {kIconText,
     "m3 3v6h0.2695312 1.0332032 4.6972656l-2.9394531-2.9394531a7 7 0 0 1 "
     "4.9394531-2.0605469 7 7 0 0 1 7 7 7 7 0 0 1-7 7 7 7 0 0 "
     "1-7-7h-1a8 8 0 0 0 8 8 8 8 0 0 0 4.892578-1.693359l3.400391 "
     "3.40039a1 1 0 0 0 1.414062 0 1 1 0 0 0 "
     "0-1.414062l-3.40039-3.400391a8 8 0 0 0 1.693359-4.892578 8 8 0 0 "
     "0-8-8 8 8 0 0 0-5.6347656 2.3652344l-2.3652344-2.3652344z"},
};

// -- THE SINGLE-MARKER VERBS' FOUR (architect-picked 2026-08-12, the same
// sheets) ---------------------------------------------------------------------
//
// list-add (the plus — drop a marker), list-remove (the X — delete),
// view-hidden (the crossed-out eye — the disable toggle) and insert-link (the
// chain — a pass marker LINKS its tempo to its neighbor, Ctrl+N's
// inherit/collapse). Transcribed byte-verbatim like every entry above.
//
// LIST-REMOVE IS THE SET'S SECOND RESOLVED-COLOR RED: its one path is
// `.ColorScheme-NegativeText { color: #da4453 }` under fill="currentColor" —
// kIconNegativeText below is THE VALUE THAT FILE RESOLVES TO, recorded like
// media-record's own literal #da4453, with which it coincides by shared
// Breeze ancestry and by nothing else (two constants, deliberately not
// aliased, exactly as kIconAccent is not kRedesignAccent). Command coverage:
// absolute M/L with one absolute C (the outline's corner easing) and z.
//
// VIEW-HIDDEN IS TRANSCRIBED VERBATIM, ARTIFACT AND ALL (architect-ruled
// 2026-08-12, at the pick): the file's last subpath — "M 1 13 C 0.33333333 19
// 0.66666667 16 1 13 z" — is a degenerate editing artifact whose fill covers
// (next to) nothing, and the architect ruled the verbatim copy fine rather
// than editing Breeze's file by hand, which would have broken the
// diff-is-a-transcription-bug property. Coverage: absolute M/L/A/C/Z, every
// family with a committed producer.

constexpr GuiColor kIconNegativeText = hex(0xDA4453);

constexpr IconPath kListAddPaths[] = {
    {kIconText,
     "M 10 4 L 10 11 L 3 11 L 3 12 L 10 12 L 10 19 L 11 19 L 11 12 L 18 12 "
     "L 18 11 L 11 11 L 11 4 L 10 4 z "},
};

constexpr IconPath kListRemovePaths[] = {
    {kIconNegativeText,
     "M 3.6992188 3 L 3 3.6992188 L 10.300781 11 L 3 18.300781 C 3 "
     "18.300781 3.7112147 18.993333 3.6992188 19 L 11 11.699219 L 18.300781 "
     "19 C 18.288781 18.9933 19 18.300781 19 18.300781 L 11.699219 "
     "11.001953 L 19 3.6992188 L 18.300781 3 L 11 10.300781 L 3.6992188 3 z "},
};

constexpr IconPath kViewHiddenPaths[] = {
    {kIconText,
     "M 18.292969 3 L 3 18.292969 L 3.7070312 19 L 19 3.7070312 L 18.292969 "
     "3 z M 11 6 A 10 9.9999781 0 0 0 2.2871094 11.119141 C 2.4663699 "
     "11.420241 2.7209984 11.668644 3.0273438 11.839844 A 9 8.99998 0 0 1 "
     "11 7 A 4 4 0 0 0 7 11 A 4 4 0 0 0 7.3574219 12.642578 L 8.1308594 "
     "11.869141 A 3 3 0 0 1 8 11 A 3 3 0 0 1 11 8 A 3 3 0 0 1 11.869141 "
     "8.1308594 L 12.640625 7.359375 A 4 4 0 0 0 11.34375 7.0175781 A 9 "
     "8.99998 0 0 1 12.796875 7.203125 L 13.640625 6.359375 A 10 9.9999781 "
     "0 0 0 11 6 z M 16.404297 7.5957031 L 15.675781 8.3242188 A 9 8.99998 "
     "0 0 1 18.974609 11.837891 C 19.282742 11.665091 19.539718 11.415428 "
     "19.71875 11.111328 A 10 9.9999781 0 0 0 16.404297 7.5957031 z M 11 9 "
     "A 2 2 0 0 0 9 11 L 11 9 z M 14.642578 9.3574219 L 13.869141 "
     "10.130859 A 3 3 0 0 1 14 11 A 3 3 0 0 1 11 14 A 3 3 0 0 1 10.130859 "
     "13.869141 L 9.3574219 14.642578 A 4 4 0 0 0 11 15 A 4 4 0 0 0 15 11 "
     "A 4 4 0 0 0 14.642578 9.3574219 z M 13 11 L 11 13 A 2 2 0 0 0 13 11 "
     "z M 1 13 C 0.33333333 19 0.66666667 16 1 13 z "},
};

constexpr IconPath kInsertLinkPaths[] = {
    {kIconText,
     "M 6 3 L 6 5 L 3 5 L 3 6 L 6 6 L 6 8 L 10 8 L 10 7 L 7 7 L 7 4 L 10 4 "
     "L 10 3 L 6 3 z M 12 3 L 12 4 L 15 4 L 15 7 L 12 7 L 12 8 L 16 8 L 16 "
     "6 L 19 6 L 19 5 L 16 5 L 16 3 L 12 3 z M 10 5 L 10 6 L 12 6 L 12 5 L "
     "10 5 z M 16 14 L 16 16 L 14 16 L 14 17 L 16 17 L 16 19 L 17 19 L 17 "
     "17 L 19 17 L 19 16 L 17 16 L 17 14 L 16 14 z "},
};

// -- THE BOTTOM ROW'S MARKER-WALK GROUP (architect-picked 2026-08-15) --------
//
// bboxprev (Shift+Tab, previous marker), bboxnext (Tab, next marker) and boost
// (Ctrl+Shift+Tab, walk both tabs). The architect's reasons for the picks are
// at the enum entries in icons.h — they are about this row's crowding, which
// is a roster fact rather than a transcription one.
//
// THE TWO BBOX FILES ARE ORDINARY FILLED PATHS, one `.ColorScheme-Text` each.
// Command coverage: relative `m` with implicit relative-lineto repetition
// (comma-separated pairs — "0,1 -2,0 0,14" is three linetos), one absolute `M`
// and one relative `l` per file, and NO `z` at all — the fill closes each
// subpath implicitly, six committed files' precedent. bboxprev spells its
// x-coordinates as 7.9999995 and 9.9999995 and they are copied AS THEY STAND:
// a hand-rounded 8 and 10 would read better and would break the property that
// a diff against the committed file is a transcription bug and nothing else.
//
// BOOST IS THE SET'S ONE STROKED FILE and the reason the interpreter's stroked
// arm came back (the record is at the table header above and at draw()). Its
// group carries `fill="none" stroke="currentColor"` with no stroke-width, so
// each of its four paths takes SVG's default width of 1 IN PATH UNITS — a
// 22-unit viewBox, so one glyph pixel at the row's 22px box, riding gui_scale
// with the geometry exactly as every filled limb does. TWO of the four carry
// `stroke-linecap="square"` (the arrowheads, whose ends must meet flush) and
// two take the default butt; that is the whole reason `square_cap` is a
// per-path field.
//
// IT IS SINGLE-COLOUR, CHECKED RATHER THAN ASSUMED: its sibling boost-boosted
// carries a `.ColorScheme-PositiveText` #27ae60 tick, and boost does not — all
// four paths are `.ColorScheme-Text`. So deep-history is still the set's ONE
// two-colour glyph, and no second resolved literal is recorded here.
//
// Command coverage: relative `m` with `v` and `h` (the first two paths, which
// is this table's first producer for either — both have been in the
// interpreter's subset since it was written) plus implicit relative-lineto
// repetition on the two arrowheads, and no `z` on any of them, which is what
// an open stroked polyline wants.
constexpr IconPath kBboxPrevPaths[] = {
    {kIconText,
     "m 7.9999995,3 0,1 -2,0 0,14 2,0 0,1 -5,0 0,-1 2,0 0,-14 -2,0 0,-1 5,0 "
     "M 19,7 l 0,3 0,2 0,3 -1,0 0,-3 -4,0 0,2 L 9.9999995,11 14,8 l 0,2 4,0 "
     "0,-3 1,0"},
};

constexpr IconPath kBboxNextPaths[] = {
    {kIconText,
     "m 14,3 0,1 2,0 0,14 -2,0 0,1 5,0 0,-1 -2,0 0,-14 2,0 0,-1 -5,0 m -11,4 "
     "0,3 0,2 0,3 1,0 0,-3 4,0 0,2 4,-3 L 8,8 8,10 4,10 4,7 3,7"},
};

constexpr IconPath kBoostPaths[] = {
    {kIconText, "m17.5 13v-7.5h-12",                          {}, true},
    {kIconText, "m4.5227 9-0.02274 7.5h12",                   {}, true},
    {kIconText, "m9.3732 3.613-4.9718 1.883 4.9718 1.883",    {}, true, true},
    {kIconText, "m12.632 14.621 4.9583 1.8758-4.9583 1.8758", {}, true, true},
};

constexpr IconDef kDocumentSave       {22.0, kDocumentSavePaths,        1};
constexpr IconDef kEditUndo           {22.0, kEditUndoPaths,            1};
constexpr IconDef kEditRedo           {22.0, kEditRedoPaths,            1};
constexpr IconDef kMediaRecord        {22.0, kMediaRecordPaths,         1};
constexpr IconDef kDocumentExport     {22.0, kDocumentExportPaths,      1};
constexpr IconDef kDocumentImport     {22.0, kDocumentImportPaths,      1};
constexpr IconDef kSpeedometer        {22.0, kSpeedometerPaths,         1};
constexpr IconDef kChronometerStart   {22.0, kChronometerStartPaths,    1};
constexpr IconDef kMusicNote16th      {22.0, kMusicNote16thPaths,       1};
constexpr IconDef kBlackSum           {22.0, kBlackSumPaths,            1};
constexpr IconDef kMathmode           {22.0, kMathmodePaths,            1};
constexpr IconDef kGoJump             {22.0, kGoJumpPaths,              1};
constexpr IconDef kPreviewRenderOn    {22.0, kPreviewRenderOnPaths,     2};
constexpr IconDef kDialogOkApply      {22.0, kDialogOkApplyPaths,       1};
constexpr IconDef kLock               {22.0, kLockPaths,                1};
constexpr IconDef kUnlock             {22.0, kUnlockPaths,              1};
constexpr IconDef kVcsCommit          {22.0, kVcsCommitPaths,           3};
constexpr IconDef kVcsDiff            {22.0, kVcsDiffPaths,             6};
constexpr IconDef kGoPrevious         {22.0, kGoPreviousPaths,          1};
constexpr IconDef kGoNext             {22.0, kGoNextPaths,              1};
constexpr IconDef kKeyframePrevious   {22.0, kKeyframePreviousPaths,    1};
constexpr IconDef kKeyframeNext       {22.0, kKeyframeNextPaths,        1};
constexpr IconDef kDocumentRevert     {22.0, kDocumentRevertPaths,      1};
constexpr IconDef kDeepHistory        {22.0, kDeepHistoryPaths,         2};
constexpr IconDef kShallowHistory     {22.0, kShallowHistoryPaths,      1};
constexpr IconDef kEditSelect         {22.0, kEditSelectPaths,          1};
constexpr IconDef kMinuetScales       {22.0, kMinuetScalesPaths,        1};
constexpr IconDef kMediaSkipBackward  {22.0, kMediaSkipBackwardPaths,   1};
constexpr IconDef kMediaPlaybackStart {22.0, kMediaPlaybackStartPaths,  1};
constexpr IconDef kMediaPlaybackStop  {22.0, kMediaPlaybackStopPaths,   1};
constexpr IconDef kMediaSkipForward   {22.0, kMediaSkipForwardPaths,    1};
constexpr IconDef kDialogCancel       {22.0, kDialogCancelPaths,        1};
constexpr IconDef kGoDown             {22.0, kGoDownPaths,              1};
constexpr IconDef kGoUp               {22.0, kGoUpPaths,                1};
constexpr IconDef kToolRectSelection  {22.0, kToolRectSelectionPaths,   1};
constexpr IconDef kZoomIn             {22.0, kZoomInPaths,              1};
constexpr IconDef kZoomOut            {22.0, kZoomOutPaths,             1};
constexpr IconDef kZoomFitBest        {22.0, kZoomFitBestPaths,         1};
constexpr IconDef kZoomOriginal       {22.0, kZoomOriginalPaths,        1};
constexpr IconDef kListAdd            {22.0, kListAddPaths,             1};
constexpr IconDef kListRemove         {22.0, kListRemovePaths,          1};
constexpr IconDef kViewHidden         {22.0, kViewHiddenPaths,          1};
constexpr IconDef kInsertLink         {22.0, kInsertLinkPaths,          1};
constexpr IconDef kBboxPrev           {22.0, kBboxPrevPaths,            1};
constexpr IconDef kBboxNext           {22.0, kBboxNextPaths,            1};
constexpr IconDef kBoost              {22.0, kBoostPaths,               4};

const IconDef& icon_def(Icon icon) {
    switch (icon) {
        case Icon::DocumentSave:        return kDocumentSave;
        case Icon::EditUndo:            return kEditUndo;
        case Icon::EditRedo:            return kEditRedo;
        case Icon::MediaRecord:         return kMediaRecord;
        case Icon::DocumentExport:      return kDocumentExport;
        case Icon::DocumentImport:      return kDocumentImport;
        case Icon::Speedometer:         return kSpeedometer;
        case Icon::ChronometerStart:    return kChronometerStart;
        case Icon::MusicNote16th:       return kMusicNote16th;
        case Icon::BlackSum:            return kBlackSum;
        case Icon::Mathmode:            return kMathmode;
        case Icon::GoJump:              return kGoJump;
        case Icon::PreviewRenderOn:     return kPreviewRenderOn;
        case Icon::Lock:                return kLock;
        case Icon::Unlock:              return kUnlock;
        case Icon::VcsCommit:           return kVcsCommit;
        case Icon::VcsDiff:             return kVcsDiff;
        case Icon::GoPrevious:          return kGoPrevious;
        case Icon::GoNext:              return kGoNext;
        case Icon::KeyframePrevious:    return kKeyframePrevious;
        case Icon::KeyframeNext:        return kKeyframeNext;
        case Icon::DocumentRevert:      return kDocumentRevert;
        case Icon::DeepHistory:         return kDeepHistory;
        case Icon::ShallowHistory:      return kShallowHistory;
        case Icon::EditSelect:          return kEditSelect;
        case Icon::MinuetScales:        return kMinuetScales;
        case Icon::MediaSkipBackward:   return kMediaSkipBackward;
        case Icon::MediaPlaybackStart:  return kMediaPlaybackStart;
        case Icon::MediaPlaybackStop:   return kMediaPlaybackStop;
        case Icon::MediaSkipForward:    return kMediaSkipForward;
        case Icon::DialogCancel:        return kDialogCancel;
        case Icon::GoDown:              return kGoDown;
        case Icon::GoUp:                return kGoUp;
        case Icon::ToolRectSelection:   return kToolRectSelection;
        case Icon::ZoomIn:              return kZoomIn;
        case Icon::ZoomOut:             return kZoomOut;
        case Icon::ZoomFitBest:         return kZoomFitBest;
        case Icon::ZoomOriginal:        return kZoomOriginal;
        case Icon::ListAdd:             return kListAdd;
        case Icon::ListRemove:          return kListRemove;
        case Icon::ViewHidden:          return kViewHidden;
        case Icon::InsertLink:          return kInsertLink;
        case Icon::BboxPrev:            return kBboxPrev;
        case Icon::BboxNext:            return kBboxNext;
        case Icon::Boost:               return kBoost;
        case Icon::DialogOkApply:       break;
    }
    return kDialogOkApply;
}

// -- The `d` interpreter ----------------------------------------------------
//
// THE SUBSET, and it is exactly what the committed files use (verified by
// reading them): M/m, L/l, H/h, V/v, C/c, S/s, A/a, Z/z, implicit command
// repetition (a bare argument set repeats the previous command; after M/m the
// repeat is L/l, per SVG), comma-or-whitespace separation with both optional,
// negative numbers as their own separator ("5-5"), and leading-dot decimals
// chained without separators (".207031.207031" is two numbers — a second '.'
// ends the first). S/s JOINED 2026-08-05 WITH ITS FIRST PRODUCER,
// document-revert.svg, whose arrow lobes are smooth cubics; the alternative was
// to flatten them into plain `c` in the table by hand, which would have put
// numbers there that appear in no file. EXPONENT NOTATION JOINED 2026-08-20 THE
// SAME WAY, with minuet-scales.svg's `8e-3` / `2e-3` offsets (the scanner's own
// comment at parse_number carries the record). No Q/q, T/t: absent from every
// committed file, so they have no producer here and the parser refuses them
// loudly rather than guessing. Elliptical 'a' is implemented
// GENERALLY (endpoint->center conversion plus a quarter-arc bezier split) even
// though media-record's four arcs are circular: arcs recur in this icon set and
// a circle-only shortcut would be a trap for the next icon.
//
// THE SUBSET IS THE `d` GRAMMAR AND NOTHING ELSE. The interpreter's other two
// grown features are the PATH ELEMENT's, not the string's, and live where they
// are used: the per-path translate at IconPath's `xform`, and the stroked arm
// (with its line cap) in draw(). Neither can reach this walk, which appends
// geometry in the path's own units either way.
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

// One SVG number: optional sign, digits, at most ONE decimal point, and an
// optional EXPONENT. The single-point rule is what splits ".207031.207031" into
// two numbers.
//
// THE EXPONENT JOINED 2026-08-20 WITH ITS FIRST PRODUCER, minuet-scales.svg,
// whose author's editor wrote small offsets as `8e-3` and `2e-3` — the S/s
// precedent exactly (a grammar feature enters this subset when a committed file
// spells it, never ahead of one). The alternative was to re-spell those six
// numbers as decimals in the table, which would have put numbers there that
// appear in no file and broken the one invariant this whole table rests on: the
// `d` string is the committed asset's, byte for byte, so a diff between them is
// a transcription bug and nothing else.
//
// IT IS SCANNED STRICTLY: the `e` is consumed only when an optional sign and at
// least ONE digit follow it, so a trailing `e` ends the number instead of
// swallowing the next command letter. std::from_chars reads the whole span
// either way — its default `general` format already includes exponents — so
// only the SPAN scanner below had to learn the syntax.
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
    if (!at_end(c) && (*c.p == 'e' || *c.p == 'E')) {
        const char* mantissa_end = c.p;
        ++c.p;
        if (!at_end(c) && (*c.p == '-' || *c.p == '+')) ++c.p;
        bool saw_exp_digit = false;
        while (!at_end(c) && *c.p >= '0' && *c.p <= '9') {
            saw_exp_digit = true;
            ++c.p;
        }
        // No digits after the `e`: it was not an exponent at all. Give the
        // whole tail back and keep the mantissa as the number.
        if (!saw_exp_digit) c.p = mantissa_end;
    }
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
        // The path element's own transform, applied INSIDE cairo's CTM (on top
        // of the viewBox mapping above) and saved/restored around the path so it
        // cannot leak into a sibling — two files carry one, and a per-path
        // transform that escaped its path would be a silent bug. Applied
        // UNCONDITIONALLY, identity included: multiplying by the identity is
        // exact in doubles, so an untransformed path's pixels are exactly what
        // they would be with no matrix at all, and there is no "has a transform"
        // branch to get wrong.
        cairo_save(cr);
        cairo_matrix_t m;
        cairo_matrix_init(&m, p.xform.xx, p.xform.yx, p.xform.xy, p.xform.yy,
                          p.xform.x0, p.xform.y0);
        cairo_transform(cr, &m);
        cairo_new_path(cr);
        // Cannot fail: the dry run proved every path in this icon parses (on
        // this call, or on the earlier call whose pass `validated` latched),
        // and the strings are compile-time constants that cannot change between
        // the walks.
        append_path(cr, p.d);
        // The path's own color, retained by keep_own and made up with
        // mixed_with — the disabled face. keep_own == 1 (the default every
        // enabled caller takes) returns the table's color bit-identically, so
        // the enabled path is unchanged by the existence of this one. It is the
        // SOURCE either way, so a stroked path dims exactly as a filled one
        // does and the disabled face needs no arm of its own.
        const GuiColor c = mix_color(p.ink, mixed_with, keep_own);
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        if (p.stroked) {
            // THE STROKED ARM (boost since 2026-08-15 — RESTORED with that
            // producer, having lived producer-less hours for distortionfx on
            // 2026-08-11 — and tool-rect-selection since 2026-08-16): the line
            // width is in PATH units and is set INSIDE the transform above, so
            // cairo's CTM scales the PEN exactly as it scales the geometry.
            // That is what SVG itself does with stroke-width, and it is what
            // keeps a stroked glyph's weight on the gui_scale axis like every
            // filled limb in the table. THE WIDTH IS THE PATH'S OWN since the
            // second file arrived: boost says nothing and takes SVG's default
            // of 1 through the field's default, tool-rect-selection says 1.043.
            //
            // The pen is SET rather than inherited, every parameter of it:
            // this cairo_t is the caller's and its stroke state is not ours to
            // assume — which is why the DASH is set on both arms of its own
            // fork rather than only where a dash exists. Miter join is SVG's
            // default and cairo's both; the miter limit differs (SVG 4, cairo
            // 10) and is stated for fidelity. THE CAP IS THE PATH'S OWN and is
            // the one thing this arm gained on its return: boost's two
            // arrowheads say stroke-linecap="square" and its two long limbs
            // say nothing, which is SVG's butt — and so does the rectangle,
            // whose butt ends are what square off each dash at a corner.
            //
            // THE DASH IS IN PATH UNITS TOO, so it rides the same CTM: a dash
            // that scaled independently of the geometry would break up
            // differently at every gui_scale, which is the whole reason it is
            // set here and not in device units.
            cairo_set_line_width(cr, p.stroke_width);
            cairo_set_line_cap(cr, p.square_cap ? CAIRO_LINE_CAP_SQUARE
                                                : CAIRO_LINE_CAP_BUTT);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
            cairo_set_miter_limit(cr, 4.0);
            if (p.dash_on > 0.0) {
                const double dashes[2] = {p.dash_on, p.dash_off};
                cairo_set_dash(cr, dashes, 2, p.dash_offset);
            } else {
                cairo_set_dash(cr, nullptr, 0, 0.0);
            }
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }
    cairo_restore(cr);
}

} // namespace icons
