#pragma once

#include "render.h"   // GuiPalette, GuiColor: the painted tones' members

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <tuple>
#include <utility>

// COOL EDIT DERIVES ITS PANEL'S TONES FROM ONE COLOR (architect 2026-10-09,
// the program is Cool Edit: one source color, the tones its arithmetic) —
// AND SINCE 2026-10-10 THE CHROME'S TOO (architect 2026-10-10, "the chrome
// color should just be the Cool Edit theme color … even the font color, all
// of that would be inherited from Cool Edit": every chrome role but the
// caption's six is one of the tones below, chrome_derive.h the mapping).
// The program's panel — the toolbar band, the dock
// bar, row 8 and, in later parts, the canvas column's frames — takes ONE
// palette role, `face` (Cool Edit's "Dockable Window 3D Color", its scheme
// key `Face`; palette_file.h's role table, default 626C7B under every
// chrome), and every other tone the panel puts down is derived from it HERE,
// the one owner.
//
// THE RULE IS THE ONE THE MEASURER PROVED (tmp/research/cool_edit/
// METRICS.md §2.1, five presets captured at the same pixel positions —
// Default 626C7B, XP Blue 1C47C6, Fire and Brick 8E283A, Lipstick and Grapes
// 8751CA, Seattle Blues C7C8CF): TAKE FACE IN HLS, KEEP ITS HUE AND
// SATURATION, SET L' = a·L + b (clamped to [0, 1]), and back to RGB —
// Python's colorsys round trip, ported below double for double, each channel
// rounded half up to its byte. The fit's worst channel over the five presets
// is under 1.0 RGB (1.4 with rounding) for every tone; constant per-channel
// ratios, Face ± a constant and Windows' dialog rule were tried and rejected
// (errors 4–66). The (a, b) of each tone is the fit's, read off its script
// (scripts/tones2.py); THE GRIPPER'S DARK LINE IS (0.526, −0.004) — the
// table in METRICS.md prints its b as 0, which misses the default's bytes by
// 2; the refit over the same five presets gives −0.004 at 0.89.
//
// THE PROOF (static_assert below): the default Face, 626C7B, gives every
// measured tone of Cool Edit's own default scheme within ONE per channel.
// The per-tone errors (derived − measured, R / G / B):
//   recess         4E5662   0  0  0      field_dark  191B1F   0 +1 +1
//   mid            414751  −1  0 −1      page_dark   181B1E   0 −1  0
//   gripper_dark   32383F  +1  0 +1      hilight     AEB5BE   0 −1  0
//   dark           2B2F36  −1  0 −1      grip_light  A1A9B4  −1 −1  0
//   tab_dark       1F2227   0  0  0      field_light ABB1BB  −1  0 +1
//   tab_light      B2B8C1  −1 −1  0      tab_line    BCC2CA   0 −1 −1
//   label          D6DADE   0 −1  0      cue_shadow  31363D  −1 −1  0
//   text           EFF0F0   0 −1 −1      text_dark   312A10  +1 +1 +1
//                                        (text_dark on Safari So Good's
//                                        C1B791, the text tones below)
//
// ONE ENUMERATOR PER TONE A PAINTER PUTS DOWN, BY NEED (2026-10-07's rule
// for a generated block of tones): every measured tone stands in the table below as the record, and
// those the painters read carry a GuiPalette member (the COOL EDIT BLOCK,
// render.h's palette struct), filled at every install of the program's words
// (fill_program_palette, render.cpp) — a new Face moves them all live.
// The toolbar band and row 8 read eight: the recess, the mid, the dark, the
// hilight, the time field's two lines and its digits (the never-swapping
// `text`), and the panel label's pair (the label's swap below, its two
// members filled by label_ink / label_shadow rather than the table); THE CANVAS
// COLUMN (2026-10-09) the mid (the view bar's top line, the ruler's
// ground), the hilight (the view bar's and the ruler's light lines, the
// cues' labels), the dark and the cue shadow — and TWO TONES OFF THE
// WAVEFORM'S INK rather than the face, the view bar's span bevels
// (ToneSource::Ink below; their proof over Cool Edit's five presets); THE
// TAB STRIP (2026-10-18) the recess, the hilight, the mid and the
// Organizer's four — tab_light, tab_dark, tab_line and page_dark — beside
// its inactive body and label, which are no tone but a measured table
// (kTabBodies below).
//
// THE BUTTON CASE FOLLOWS FACE TOO (architect 2026-10-10, reopening
// 2026-10-09's "the buttons did not change color with the preset": the fit's
// misses are "off by 5 to 8 … imperceptible … OK to implement"): its face
// ramp, its highlight, its shadow ramp's two ends and the shadow's corner are
// each a CASE TONE below — the Face's HLS hue kept, L' = A + B·L_F and
// S' = C·S_F, per row (METRICS_PRESETS_1010.md §4c, the best model the
// measurement found; no closed rule fits within 4). The coefficients are the
// minimax fit over thirteen captured Faces (scripts/m85_case_fit.py) WITH
// THE DEFAULT FACE HELD WITHIN 2 PER CHANNEL OF ITS MEASURED BYTES (the
// app's default look does not move; static_assert below). Worst channel
// error over the twelve other Faces: rows 0–19 5.3–8.8 except row 5 9.4,
// row 10 10.2 and row 6 11.3 (XP Blue and Midnight, which Cool Edit lifts
// brighter than any HLS-lightness rule follows); the highlight 2.7, the
// shadow ends and the corner 2.0 (the shadow is near lightness-independent,
// B ≈ 0, tinted by hue alone). The default-scheme constants they replace
// missed the other Faces by 19–34. NOT FOLLOWING FACE (cool_edit_paint.h's
// constants): the black outer line and the pressed and checked rings,
// byte-identical on every captured Face (§4a, METRICS §1.5).
//
// THE TEXT TONES (2026-10-10; tmp/research/cool_edit/METRICS_PRESETS_1010.md,
// eleven captures over nine Faces, §2–§3): Cool Edit's PROGRAM TEXT — the
// clock, the active tab's "Files", the Sel / View digits — is NOT the
// two-parameter rule: its saturation is cut too, S' = c·S + d (the Tone's c
// and d below; c = 1, d = 0 for every panel tone). On a dark Face it is
// `text`, L' = .086·L + .900, S' = .2·S (§2a, worst 1 over nine Faces); ABOVE
// THE TEXT THRESHOLD (kTextSwapLightness) Cool Edit SWAPS it dark, `text_dark`,
// L' = .16·L + .025, S' = .58·S + .32 (§3d's clock fit, worst 1 over four
// Faces; a grey Face takes colorsys's hue 0, as the clock measured, so its
// dark text is a red-brown 311A1A). The fields' digits NEVER swap — their
// ground is the dark `mid` at every Face (§2a, §3a) — so the field text is
// `text` at every Face (field_text below). THE CHROME WEARS THEM
// (chrome_derive.h): the label, the clock text and the card text the swapping
// text, the field text the never-swapping one — and so does THE PROGRAM'S
// OWN TIME FIELD (row 8's clock, the render player's two fields: the
// `ce_field_text` member, architect 2026-10-10, "whatever Cool Edit seems to
// be doing as far as the dark text, we do that as well").
//
// THE PANEL LABEL AND ITS OWN SWAP (2026-10-10, the same ruling; §2c, §2e,
// §3a, §3c): the label's ink on a dark Face is `label`, L' = .244·L + .750
// with the saturation KEPT (refit over seven Faces, worst 1 — the label is
// not the program text's rule), its (+1, +1) shadow `dark`; ABOVE A LATER
// THRESHOLD of its own (kLabelSwapLightness, in (.663, .739]) Cool Edit
// ENGRAVES it — the ink the `dark` tone and the shadow the `hilight` tone,
// within one on Arctic Freeze and Grape. label_ink / label_shadow below; the
// state line and every show_ce_label reader. THE RULER'S DIGITS AND TICKS
// are no tone of Face: Cool Edit's E0E0E0 on every preset, never swapping
// (§2d; kCeRulerTick, cool_edit_paint.h).
//
// THE SELECTED TEXT (architect 2026-10-16, on tablet mocks of his own
// screenshots: "if all presets have both a light and dark font from Cool
// Edit, then the selection should be opposite of the regular face"): the
// chrome's selection fill is THE WAVEFORM'S INK (architect 2026-10-10) and
// its text THE PROGRAM TEXT'S OTHER MEMBER — every Face has both, `text` and
// `text_dark`, and the selection takes the one program_text does NOT pick,
// on the same swap (text_swaps): `text_dark` where the Face keeps the light
// text, `text` where it swaps (selected_text below). A ruling, not a
// measurement: Cool Edit draws no text on its ink. The contrast it loses on
// some presets' inks is accepted ("I can read at low contrast, it's more of
// an aesthetic choice"; Grape, Lime and Tangerine's light text on its mint
// ink judged and taken). (The ink at HLS L 0.10, 2026-10-10..16, retired.)

namespace cool_edit_derive {

// THE DEFAULT FACE, Cool Edit's own default scheme (the palette role's
// default under every chrome, palette_file.h).
inline constexpr uint32_t kDefaultFace = 0x626C7B;

// -- THE HLS ROUND TRIP (Python's colorsys, ported) ----------------------------

struct Hls {
    double h, l, s;
};

constexpr double frac(double x) {
    // Python's x % 1.0 for |x| < 2: the result in [0, 1).
    double t = x - static_cast<double>(static_cast<long>(x));
    return t < 0.0 ? t + 1.0 : t;
}

constexpr Hls rgb_to_hls(double r, double g, double b) {
    const double maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const double minc = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const double sumc = maxc + minc;
    const double rangec = maxc - minc;
    const double l = sumc / 2.0;
    if (minc == maxc) return Hls{0.0, l, 0.0};
    const double s = l <= 0.5 ? rangec / sumc : rangec / (2.0 - maxc - minc);
    const double rc = (maxc - r) / rangec;
    const double gc = (maxc - g) / rangec;
    const double bc = (maxc - b) / rangec;
    double h = 0.0;
    if (r == maxc)      h = bc - gc;
    else if (g == maxc) h = 2.0 + rc - bc;
    else                h = 4.0 + gc - rc;
    return Hls{frac(h / 6.0), l, s};
}

constexpr double hls_value(double m1, double m2, double hue) {
    hue = frac(hue);
    if (hue < 1.0 / 6.0) return m1 + (m2 - m1) * hue * 6.0;
    if (hue < 0.5)       return m2;
    if (hue < 2.0 / 3.0) return m1 + (m2 - m1) * (2.0 / 3.0 - hue) * 6.0;
    return m1;
}

constexpr uint32_t byte_of(double v) {
    // Each channel rounded half up to its byte.
    const double x = v * 255.0 + 0.5;
    const long n = static_cast<long>(x);
    return static_cast<uint32_t>(n < 0 ? 0 : (n > 255 ? 255 : n));
}

constexpr Hls hls_of(uint32_t rgb) {
    return rgb_to_hls(static_cast<double>((rgb >> 16) & 0xFF) / 255.0,
                      static_cast<double>((rgb >> 8) & 0xFF) / 255.0,
                      static_cast<double>(rgb & 0xFF) / 255.0);
}

constexpr double clamp_unit(double x) {
    return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

// THE ONE DERIVATION: the source in HLS, L' = a·L + b clamped, the hue
// kept, and S' = c·S + d clamped — c = 1, d = 0 (the saturation kept) for
// every tone but the program text's (the head).
constexpr uint32_t tone(uint32_t face, double a, double b, double c = 1.0,
                        double d = 0.0) {
    const Hls f = hls_of(face);
    const double l = clamp_unit(a * f.l + b);
    const double s = clamp_unit(c * f.s + d);
    if (s == 0.0) {
        const uint32_t v = byte_of(l);
        return (v << 16) | (v << 8) | v;
    }
    const double m2 = l <= 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double m1 = 2.0 * l - m2;
    return (byte_of(hls_value(m1, m2, f.h + 1.0 / 3.0)) << 16) |
           (byte_of(hls_value(m1, m2, f.h)) << 8) |
           byte_of(hls_value(m1, m2, f.h - 1.0 / 3.0));
}

// -- THE TONES ----------------------------------------------------------------

// THE ROLE A TONE IS DERIVED FROM: the panel's `face` (every tone of §2.1)
// or, since 2026-10-09, THE WAVEFORM'S INK (`waveform_ink`): the view bar's
// span is a raised block of Cool Edit's WvFg — its body the ink itself, its
// light and dark bevels the ink at a FIXED HLS lightness with its hue and
// saturation kept (METRICS §3: L 0.94 and, refit, 0.3157 — the table's
// note; a = 0 below), the same round trip as the face's tones.
enum class ToneSource { Face, Ink };

struct Tone {
    const char*            name;
    uint32_t               measured;   // Cool Edit's byte on `measured_on`
    double                 a, b;       // the fit: L' = a·L + b
    // The panel's painted ones; nullptr = a record, or a tone the chrome
    // wears through its own roles (chrome_derive.h, by name).
    GuiColor GuiPalette::* member;
    ToneSource             source = ToneSource::Face;
    double                 c = 1.0, d = 0.0;   // S' = c·S + d (the head)
    // The source color `measured` was captured on: the default Face for
    // every Face tone but the swapped text, which the default never shows
    // (the head's proof).
    uint32_t               measured_on = kDefaultFace;
};
// Each with where Cool Edit draws it (METRICS §2.1, §2.2, §3).
inline constexpr Tone kTones[] = {
    // the dock's recess and the band's stretch where no pane stands
    {"recess",        0x4E5662, 0.798,  0.000, &GuiPalette::ce_recess},
    // a pane's bottom and right line, a groove's left column, the end bar's
    // and the gripper's mid lines, the time field's ground
    {"mid",           0x414751, 0.668, -0.006, &GuiPalette::ce_mid},
    // the organizer dock's gripper (dark line)
    {"gripper_dark",  0x32383F, 0.526, -0.004, nullptr},
    // the band's dark outline, a groove's middle column, the end bar's and
    // the dock bar's dark line, the panel label's shadow
    {"dark",          0x2B2F36, 0.442, -0.004, &GuiPalette::ce_dark},
    // the organizer tab's right edge (an inactive tab's, the tab strip,
    // 2026-10-18)
    {"tab_dark",      0x1F2227, 0.324, -0.004, &GuiPalette::ce_tab_dark},
    // the time field's top and left line
    {"field_dark",    0x191B1F, 0.272, -0.006, &GuiPalette::ce_field_dark},
    // the organizer page's right dark line — the active tab's drop shadow
    // (the tab strip, 2026-10-18)
    {"page_dark",     0x181B1E, 0.248, -0.002, &GuiPalette::ce_page_dark},
    // Cool Edit's "Hilight": a pane's top and left line, the band's last
    // line, the gripper's and the end bar's light lines, the dock bar's top
    {"hilight",       0xAEB5BE, 0.492,  0.500, &GuiPalette::ce_hilight},
    // the organizer dock's gripper (light line)
    {"gripper_light", 0xA1A9B4, 0.390,  0.498, nullptr},
    // the time field's bottom and right line
    {"field_light",   0xABB1BB, 0.272,  0.584, &GuiPalette::ce_field_light},
    // the organizer tab's left and top light line (an inactive tab's)
    {"tab_light",     0xB2B8C1, 0.322,  0.586, &GuiPalette::ce_tab_light},
    // the organizer tab's bottom line: the page line's run under the inactive
    // tabs (the tab strip, 2026-10-18)
    {"tab_line",      0xBCC2CA, 0.414,  0.584, &GuiPalette::ce_tab_line},
    // the panel label's ink on a dark face (its shadow `dark` at (+1, +1));
    // past kLabelSwapLightness the label is the `dark` / `hilight` pair
    // instead (label_ink / label_shadow below, which fill the pair's two
    // members — no member here). (.244, .750) the refit over seven Faces
    // (METRICS_PRESETS_1010.md §2c, worst 1; 2026-10-10), the (.264, .740)
    // of the five 2026-10-09 presets missing Midnight, Stealth and Safari by 2.
    {"label",         0xD6DADE, 0.244,  0.750, nullptr},
    // the cue's and the playhead head's shadow, one quantum right of each
    // of the triangle's rows (METRICS §4.2, §4.3; the marker lane and the
    // ruler, 2026-10-09)
    {"cue_shadow",    0x31363D, 0.502, -0.004, &GuiPalette::ce_cue_shadow},
    // THE PROGRAM TEXT (2026-10-10, the head's text tones): the clock's,
    // "Files"' and the field digits' light ink on a dark Face — the chrome's
    // label, clock and card text below the swap and its field text at
    // every Face (chrome_derive.h), and THE PROGRAM'S TIME FIELDS' DIGITS at
    // every Face (`ce_field_text`: the field's ground is the dark `mid`, so
    // it never swaps) — and Cool Edit's swapped dark ink above the
    // threshold, measured on Safari So Good (C1B791, its clock 312A10). The
    // chrome's selected text is the pair's other member (selected_text,
    // architect 2026-10-16).
    {"text",          0xEFF0F0, 0.086,  0.900, &GuiPalette::ce_field_text,
     ToneSource::Face, 0.2, 0.0},
    {"text_dark",     0x312A10, 0.160,  0.025, nullptr, ToneSource::Face,
     0.58, 0.32, 0xC1B791},
    // THE VIEW BAR'S SPAN (METRICS §3), off the INK: its top row and left
    // column, and its bottom row and right column — the ink at HLS L 0.94
    // and 0.3157. `measured` is the default scheme's (WvFg 4BF3A7, the
    // default palette's ink); the ink tones' proof is over Cool Edit's five
    // captured presets (below). THE SHADOW'S LIGHTNESS IS 0.3157, NOT
    // METRICS.md's 0.312 (2026-10-09): 0.312 misses the default's shadow by
    // 2 in green (95 for 97), while every one of the five measured shadows
    // sits at exactly L = 161/510 = 0.3157, which reproduces all five
    // within one per channel (the proof below) — the gripper's refit
    // precedent at this file's head.
    {"span_hilight",  0xE3FDF1, 0.0,    0.940, &GuiPalette::ce_span_hilight,
     ToneSource::Ink},
    {"span_shadow",   0x0A9757, 0.0,    0.3157, &GuiPalette::ce_span_shadow,
     ToneSource::Ink},
};

constexpr std::size_t painted_tone_count() {
    std::size_t n = 0;
    for (const Tone& t : kTones)
        if (t.member != nullptr) ++n;
    return n;
}
inline constexpr std::size_t kPaintedToneCount = painted_tone_count();

constexpr bool within_one(uint32_t x, uint32_t y) {
    for (int shift = 0; shift <= 16; shift += 8) {
        const int a = static_cast<int>((x >> shift) & 0xFF);
        const int b = static_cast<int>((y >> shift) & 0xFF);
        if (a - b > 1 || b - a > 1) return false;
    }
    return true;
}
// THE PROOF (the head): the default Face reproduces every measured tone of
// Cool Edit's default scheme within one per channel — the swapped text on
// the Face it was captured on.
static_assert([] {
    for (const Tone& t : kTones)
        if (t.source == ToneSource::Face &&
            !within_one(tone(t.measured_on, t.a, t.b, t.c, t.d), t.measured))
            return false;
    return true;
}());
// The default Face itself is a byte of the round trip (a = 1, b = 0).
static_assert(tone(kDefaultFace, 1.0, 0.0) == kDefaultFace);

// THE SPAN'S PROOF (METRICS §3; architect 2026-10-09): over Cool Edit's FIVE
// captured presets' waveform inks (WvFg) — Default 4BF3A7, XP Blue 22B893,
// Fire and Brick FFCE0C, Lipstick and Grapes ED1EC9, Seattle Blues 576AB4 —
// the fixed-lightness rule reproduces each span's measured highlight and
// shadow within one per channel. THE SAME SHADOW RULE IS EVERY BUILT-IN
// PALETTE'S LIT OUTLINE (2026-10-09, palette_file.h's role table: Cool Edit
// records no outline), run once by the generator and proven exact over the
// compiled presets by palette_file.cpp.
struct SpanPreset {
    uint32_t ink, hilight, shadow;
};
inline constexpr SpanPreset kSpanPresets[] = {
    {0x4BF3A7, 0xE3FDF1, 0x0A9757},
    {0x22B893, 0xE5FAF5, 0x19886C},
    {0xFFCE0C, 0xFFF9E1, 0xA18100},
    {0xED1EC9, 0xFCE3F8, 0x950C7D},
    {0x576AB4, 0xEAECF5, 0x323E6F},
};
// The tone named `name` (every name is the table's: a miss answers the
// first row, which the callers' asserts catch).
constexpr const Tone& named_tone(const char* name) {
    for (const Tone& t : kTones)
        if (std::string_view(t.name) == name) return t;
    return kTones[0];
}
constexpr const Tone& ink_tone(const char* name) { return named_tone(name); }
static_assert([] {
    const Tone& hi = ink_tone("span_hilight");
    const Tone& lo = ink_tone("span_shadow");
    if (hi.source != ToneSource::Ink || lo.source != ToneSource::Ink)
        return false;
    for (const SpanPreset& p : kSpanPresets) {
        if (!within_one(tone(p.ink, hi.a, hi.b), p.hilight)) return false;
        if (!within_one(tone(p.ink, lo.a, lo.b), p.shadow)) return false;
    }
    return true;
}());

// -- THE TEXT AND ITS SWAP (2026-10-10, the head) ------------------------------

// A tone off its source: the Face, or the waveform's ink.
constexpr uint32_t tone_of(const Tone& t, uint32_t face, uint32_t ink) {
    return tone(t.source == ToneSource::Ink ? ink : face, t.a, t.b, t.c, t.d);
}

// THE TEXT THRESHOLD: Cool Edit's program text turns dark above it, keyed on
// the Face's HLS L (METRICS_PRESETS_1010.md §3b: L and the RGB mean agree on
// every capture; HSV V and luma are ruled out by the label's swap). The
// captures pin it in (.624, .663] — Dusty Rose's L .624 keeps the light
// text, Safari So Good's .663 swaps — and 0.64 sits inside that interval
// with room on both sides, nearer the light side, which no capture
// contradicts; any value in the interval reproduces all eleven captures.
// The panel label's own swap is a later threshold (kLabelSwapLightness).
inline constexpr double kTextSwapLightness = 0.64;
constexpr bool text_swaps(uint32_t face) {
    return hls_of(face).l > kTextSwapLightness;
}

// THE PROGRAM TEXT ON `face`: the light ink, or the dark one past the
// threshold — the chrome's label, clock and card text (chrome_derive.h).
constexpr uint32_t program_text(uint32_t face) {
    return tone_of(named_tone(text_swaps(face) ? "text_dark" : "text"), face, 0);
}
// THE SELECTED TEXT ON `face` (architect 2026-10-16, the head): the program
// text's OTHER member, on the same swap — the dark ink where program_text
// keeps the light one, the light ink where it swaps — the chrome's
// `selected_text`, over the waveform's ink (chrome_derive.h).
constexpr uint32_t selected_text(uint32_t face) {
    return tone_of(named_tone(text_swaps(face) ? "text" : "text_dark"), face, 0);
}
// THE FIELD TEXT ON `face`: the light ink at every Face — the fields' ground
// is the dark `mid`, which never swaps (the head).
constexpr uint32_t field_text(uint32_t face) {
    return tone_of(named_tone("text"), face, 0);
}

// THE LABEL THRESHOLD (2026-10-10; METRICS_PRESETS_1010.md §3a, §3b): the
// panel label swaps LATER than the text, on the same key (the Face's HLS L;
// luma and HSV V ruled out by Safari and Arctic). The captures pin it in
// (.663, .739] — Safari So Good's L .663 keeps the light label (EEECE2 over
// 5E5635) while its text is already dark, Grape's .739 is engraved — and
// 0.70 is that interval's middle (.701), the widest margin to both captured
// Faces; any value in it reproduces every capture.
inline constexpr double kLabelSwapLightness = 0.70;
static_assert(kLabelSwapLightness > kTextSwapLightness);
constexpr bool label_swaps(uint32_t face) {
    return hls_of(face).l > kLabelSwapLightness;
}
// THE PANEL LABEL'S INK AND ITS (+1, +1) SHADOW ON `face` (the head):
// `label` over `dark` on a dark Face, `dark` over `hilight` (engraved) past
// the label threshold.
constexpr uint32_t label_ink(uint32_t face) {
    return tone_of(named_tone(label_swaps(face) ? "dark" : "label"), face, 0);
}
constexpr uint32_t label_shadow(uint32_t face) {
    return tone_of(named_tone(label_swaps(face) ? "hilight" : "dark"), face, 0);
}
// The pair's two members, filled by label_ink / label_shadow at every
// install beside the table's painted tones (fill_program_palette).
inline constexpr GuiColor GuiPalette::* kLabelPairMembers[] = {
    &GuiPalette::ce_label, &GuiPalette::ce_label_shadow,
};

// THE TEXT'S PROOF (METRICS_PRESETS_1010.md §2a and §3d, the clock's flat
// core; Seattle Blues from METRICS.md): every captured Face's program text
// within one per channel, the swap falling where the captures put it.
struct TextPreset {
    uint32_t face, text;
};
inline constexpr TextPreset kTextPresets[] = {
    {0x626C7B, 0xEFF0F0},   // Default
    {0x272B6D, 0xE9E9EE},   // Midnight
    {0x2F1AEE, 0xF0EFF4},   // 3D
    {0xA8969C, 0xF4F4F4},   // Dusty Rose, L .624: light
    {0x414141, 0xEBEBEB},   // Midnight Blues
    {0x03040A, 0xE2E3E8},   // Stealth
    {0x1C47C6, 0xEDEEF2},   // XP Blue
    {0x8E283A, 0xF0EBEC},   // Fire and Brick
    {0x8751CA, 0xF3F1F4},   // Lipstick and Grapes
    {0xC1B791, 0x312A10},   // Safari So Good, L .663: swapped
    {0xACA9D0, 0x151237},   // Grape, Lime and Tangerine: swapped
    {0xC0C0C0, 0x311A1A},   // Arctic Freeze (grey: hue 0): swapped
    {0xC7C8CF, 0x1A1D36},   // Seattle Blues: swapped
};
static_assert([] {
    for (const TextPreset& p : kTextPresets)
        if (!within_one(program_text(p.face), p.text)) return false;
    return true;
}());
// The field text never swaps: on the default Face it is the program text.
static_assert(field_text(kDefaultFace) == program_text(kDefaultFace));
static_assert(text_swaps(0xC0C0C0) && !text_swaps(kDefaultFace));

// THE DARK TONE AND THE HILIGHT ON OTHER FACES (METRICS_PRESETS_1010.md §2c,
// §3c: the panel label's shadow, and the swapped label's ink and shadow) —
// the chrome's card frame and its Hilight since 2026-10-10 (chrome_derive.h)
// — within one per channel on the captured Faces, the swapped Grape and
// Arctic Freeze among them.
struct FaceToneSample {
    const char* tone;
    uint32_t    face, measured;
};
inline constexpr FaceToneSample kFaceToneSamples[] = {
    {"dark",    0x626C7B, 0x2B2F36},   // Default
    {"dark",    0x272B6D, 0x10122E},   // Midnight
    {"dark",    0x2F1AEE, 0x11086A},   // 3D
    {"dark",    0xA8969C, 0x4C3F43},   // Dusty Rose
    {"dark",    0x414141, 0x1B1B1B},   // Midnight Blues
    {"dark",    0x03040A, 0x000103},   // Stealth
    {"dark",    0xC1B791, 0x5E5635},   // Safari So Good
    {"dark",    0xC0C0C0, 0x555555},   // Arctic Freeze
    {"dark",    0xACA9D0, 0x3E3A6B},   // Grape, Lime and Tangerine
    {"hilight", 0xC0C0C0, 0xDFDFDF},   // Arctic Freeze
    {"hilight", 0xACA9D0, 0xD4D3E6},   // Grape, Lime and Tangerine
};
static_assert([] {
    for (const FaceToneSample& p : kFaceToneSamples)
        if (!within_one(tone_of(named_tone(p.tone), p.face, 0), p.measured))
            return false;
    return true;
}());

// THE LABEL'S PROOF (METRICS_PRESETS_1010.md §2c, §3c, the flat cores of
// "Begin"): every captured Face's label ink and shadow within one per
// channel, the swap falling between Safari So Good and Grape.
struct LabelPreset {
    uint32_t face, ink, shadow;
};
inline constexpr LabelPreset kLabelPresets[] = {
    {0x626C7B, 0xD6DADE, 0x2B2F36},   // Default
    {0x272B6D, 0xBBBDE6, 0x10122E},   // Midnight
    {0x2F1AEE, 0xCBC5FA, 0x11086A},   // 3D
    {0xA8969C, 0xE8E4E5, 0x4C3F43},   // Dusty Rose
    {0x414141, 0xCFCFCF, 0x1B1B1B},   // Midnight Blues
    {0x03040A, 0x9EA8E2, 0x000103},   // Stealth
    {0xC1B791, 0xEEECE2, 0x5E5635},   // Safari So Good, L .663: light
    {0xACA9D0, 0x3E3A6B, 0xD4D3E6},   // Grape, Lime and Tangerine: engraved
    {0xC0C0C0, 0x555555, 0xDFDFDF},   // Arctic Freeze: engraved
};
static_assert([] {
    for (const LabelPreset& p : kLabelPresets)
        if (!within_one(label_ink(p.face), p.ink) ||
            !within_one(label_shadow(p.face), p.shadow))
            return false;
    return true;
}());

// THE SELECTED TEXT IS THE PROGRAM TEXT'S OTHER MEMBER on every captured
// Face: never the program text, and the pair the two text tones, one each.
static_assert([] {
    for (const TextPreset& p : kTextPresets) {
        const uint32_t light = tone_of(named_tone("text"), p.face, 0);
        const uint32_t dark = tone_of(named_tone("text_dark"), p.face, 0);
        const uint32_t t = program_text(p.face);
        const uint32_t sel = selected_text(p.face);
        if (sel == t) return false;
        if (!((t == light && sel == dark) || (t == dark && sel == light)))
            return false;
    }
    return true;
}());

// -- THE BUTTON CASE (2026-10-10, the head) ----------------------------------

// ONE CASE TONE: on the Face, L' = a·L + b and S' = c·S, the hue kept (the
// head's per-row fit, scripts/m85_case_fit.py: a the slope B, b the
// intercept A), and `default_byte` Cool Edit's default scheme's byte as
// measured (METRICS §1.3–1.4), which the default Face reproduces within 2.
struct CaseTone {
    double   a, b, c;
    uint32_t default_byte;
};
constexpr uint32_t case_tone(const CaseTone& t, uint32_t face) {
    return tone(face, t.a, t.b, t.c);
}
// THE FACE RAMP — ONE STOP PER W ROW OF THE 20-W GLYPH SEAT, top to bottom
// (cool_edit_paint.h's paint_ce_case), each row's worst miss over the twelve
// other captured Faces at its right.
inline constexpr std::array<CaseTone, 20> kCaseRamp = {{
    {0.18050, 0.92600, 1.28000, 0xFEFEFE},   //  0   5.3
    {0.20750, 0.89700, 0.77700, 0xF9FEFE},   //  1   7.2
    {0.23100, 0.87600, 1.07000, 0xF9FAFC},   //  2   6.9
    {0.17200, 0.87500, 0.94100, 0xF2F3F6},   //  3   8.8
    {0.17000, 0.86250, 0.82900, 0xEBECEE},   //  4   6.4
    {0.20500, 0.82000, 0.59900, 0xE4E5E7},   //  5   9.4
    {0.16900, 0.81200, 0.47700, 0xDEDFE1},   //  6  11.3
    {0.20800, 0.78250, 0.43600, 0xDDDEE0},   //  7   8.4
    {0.18150, 0.77650, 0.36400, 0xD7D8DA},   //  8   7.9
    {0.18950, 0.74550, 0.27500, 0xD1D1D3},   //  9   7.6
    {0.22250, 0.70400, 0.21500, 0xCACACB},   // 10  10.2
    {0.19750, 0.69150, 0.23800, 0xC4C5C6},   // 11   8.1
    {0.16250, 0.69700, 0.20000, 0xC2C3C4},   // 12   6.1
    {0.15850, 0.66900, 0.16000, 0xBABBBD},   // 13   7.6
    {0.17850, 0.64300, 0.15500, 0xB5B6B7},   // 14   6.1
    {0.17000, 0.62100, 0.12100, 0xAEAFB0},   // 15   6.7
    {0.15000, 0.60650, 0.11300, 0xA9A9AB},   // 16   6.4
    {0.13200, 0.59650, 0.11300, 0xA7A7A8},   // 17   7.0
    {0.13800, 0.57250, 0.09900, 0x9FA0A1},   // 18   6.5
    {0.14100, 0.54400, 0.10300, 0x989899},   // 19   6.4
}};
static_assert(kCaseRamp.size() ==
              static_cast<std::size_t>(kProgramSpec.glyph_px));
// THE HIGHLIGHT — the top row and left column (Cool Edit's FEFEFF … FDFEFF …
// FCFEFF on the default Face, its middle tone the default byte); worst 2.7.
inline constexpr CaseTone kCaseHighlight = {0.02150, 0.97950, 1.18600, 0xFDFEFF};
// THE SHADOW — the right column and bottom row, a ramp from its first to its
// last tone across the glyph seat (5D5E5E … 4C4D4D on the default Face), the
// first also the mitre's dark half; and THE SHADOW'S OWN CORNER at the
// bottom right. Worst 2.0 each.
inline constexpr CaseTone kCaseShadowFirst = {-0.00100, 0.37000, 0.08200, 0x5D5E5E};
inline constexpr CaseTone kCaseShadowLast  = {0.00650, 0.29850, 0.08200, 0x4C4D4D};
inline constexpr CaseTone kCaseCorner      = {0.00450, 0.29500, 0.08100, 0x4B4C4C};
// The GuiPalette members the case fills (the ramp's array and the four
// lines), counted by palette_file.cpp's coverage check.
inline constexpr std::size_t kCaseMemberCount = kCaseRamp.size() + 4;

constexpr bool within_two(uint32_t x, uint32_t y) {
    for (int shift = 0; shift <= 16; shift += 8) {
        const int a = static_cast<int>((x >> shift) & 0xFF);
        const int b = static_cast<int>((y >> shift) & 0xFF);
        if (a - b > 2 || b - a > 2) return false;
    }
    return true;
}
// THE CASE'S PROOF (the head): the default Face keeps every case tone within
// 2 per channel of Cool Edit's default scheme as measured — the default look
// does not move.
static_assert([] {
    for (const CaseTone& t : kCaseRamp)
        if (!within_two(case_tone(t, kDefaultFace), t.default_byte)) return false;
    for (const CaseTone* t : {&kCaseHighlight, &kCaseShadowFirst,
                              &kCaseShadowLast, &kCaseCorner})
        if (!within_two(case_tone(*t, kDefaultFace), t->default_byte))
            return false;
    return true;
}());
// A grey Face gives a grey case (its saturation 0): Arctic Freeze's shadow
// starts within one of the measured 5D5D5D (METRICS_PRESETS_1010.md §4a).
static_assert(case_tone(kCaseShadowFirst, 0xC0C0C0) == 0x5E5E5E &&
              within_one(0x5E5E5E, 0x5D5D5D));

// -- THE TAB STRIP'S INACTIVE TAB (architect 2026-10-18 ~19:05, "agree") -------

// AN INACTIVE TAB'S BODY AND LABEL ARE A PER-PRESET MEASURED TABLE, NOT A
// RULE. Cool Edit fills an inactive Organizer tab's body with a 15-row
// vertical gradient off the Face and sets its label in an ink of its own
// (METRICS §2.3: 8C9AB0 … 616B7A and CECFD1 on the default Face; the hover
// face Cool Edit lifts by 1.25 per channel is not drawn — no hover faces,
// program_spec.h's case_hot_face). THE FIT FAILED: over his 21 Wine captures
// of 2026-10-18 (tmp/20261018_18h0*_grim.png: 19 distinct Faces, Classic
// Cool's Wine-tinted 9F9785 among them, two captures repeats; measured by
// tmp/tabs_fit/measure.py at x 90, rows 96..110, the label the
// mode over rows 99..107; the record tmp/tabs_fit/RESULTS.md, the fits
// fit.py beside it), no hue-keeping model holds — the case ramp's model
// pinned to Default misses by 28–40 (Periwinkle, 3D), unpinned 24–35, RGB,
// HSV and hue-shifted HLS worse: the saturated Faces clamp a channel at 255
// on the top rows, and the light Faces take a saturation boost no c·S
// reaches. The label fits .24·L + .708 / .225·S within 2 on the fourteen
// light-text Faces, but on the five light Faces (Arctic Freeze, Grape,
// Periwinkle, Safari So Good, Seattle Blues) its dark ink fits no
// derivation, so the table carries the label for every row, one rule.
// HIS RULING: THE TABLE — each preset's captured bytes, exact: 18 rows, one
// per distinct Face of the twenty built-in palettes. IT IS SOUND
// because the palettes are compiled-in presets (the picker and the preset
// files removed 2026-10-14, palette_file.h's head): the live Face is one of
// the built-in palettes' Faces and nothing else, and palette_file.cpp's
// static_assert walks every built-in proving each has a row here, so a Face
// outside the table is unconstructible and the lookup carries no error arm.
// Classic Cool, whose Wine capture wears Wine's 9F9785, takes Default's Face
// by his 2026-10-16 ruling (palette_file.h) and so Default's row; Easy on
// the Eyes shares it too. Each row: the Face, the 15 body bytes top row
// first — row i fills the body's W row i as kCaseRamp's stops fill the
// glyph seat, never resampled (cool_edit_paint.h's paint_ce_tab_strip) —
// and the inactive label's ink. Hand-typed from RESULTS.md, every byte
// checked against it; the record is this comment and git history (tmp/ is
// untracked), as the case ramp's.
struct TabBody {
    uint32_t                 face;
    std::array<uint32_t, 15> rows;
    uint32_t                 label;
};
inline constexpr TabBody kTabBodies[] = {
    {0x2F1AEE, {0x3C1AFF, 0x3B1AFF, 0x3919FF, 0x3818FF, 0x3718FF, 0x3517FF,  // 3D
                0x3417FF, 0x3316FF, 0x3115FE, 0x3015F7, 0x2F14F0, 0x2D14E9,
                0x2C13E2, 0x2B12DC, 0x2912D5}, 0xCFCDDD},
    {0xC0C0C0, {0xC4C4C4, 0xBFBFBF, 0xBBBBBB, 0xB7B7B7, 0xB2B2B2, 0xAEAEAE,  // Arctic Freeze
                0xAAAAAA, 0xA5A5A5, 0xA1A1A1, 0x9D9D9D, 0x989898, 0x949494,
                0x909090, 0x8B8B8B, 0x878787}, 0x1B2240},
    {0x7088A0, {0x86A9CC, 0x83A5C7, 0x80A1C3, 0x7D9EBE, 0x7A9ABA, 0x7796B5,  // CEPro 1.2
                0x7492B1, 0x718FAC, 0x6E8BA8, 0x6B87A3, 0x68849F, 0x65809A,
                0x627C96, 0x5F7891, 0x5C758D}, 0xD4D5D6},
    {0xA8969C, {0xC6A3AF, 0xC29FAB, 0xBD9CA7, 0xB998A3, 0xB4949F, 0xB0919C,  // Dusty Rose
                0xAC8D98, 0xA78A94, 0xA38690, 0x9F828C, 0x9A7F88, 0x967B84,
                0x917881, 0x8D747D, 0x897079}, 0xDADADA},
    {0x626C7B, {0x8C9AB0, 0x8997AC, 0x8694A8, 0x8390A4, 0x7F8DA1, 0x7C899D,  // Default
                0x798699, 0x768295, 0x737F91, 0x707C8D, 0x6D7889, 0x6A7585,
                0x677181, 0x646E7E, 0x616B7A}, 0xCECFD1},
    {0x8E283A, {0xFF4668, 0xF94465, 0xF44363, 0xEE4161, 0xE83F5E, 0xE33E5C,  // Fire and Brick
                0xDD3C5A, 0xD73B58, 0xD23955, 0xCC3853, 0xC73651, 0xC1354E,
                0xBB334C, 0xB6324A, 0xB03047}, 0xD2C3C6},
    {0xACA9D0, {0xA59FF2, 0xA29CEC, 0x9E98E7, 0x9A95E1, 0x9791DC, 0x938ED7,  // Grape, Lime, and Tangerine
                0x8F8AD1, 0x8C87CC, 0x8883C7, 0x8480C2, 0x817CBC, 0x7D79B7,
                0x7975B1, 0x7671AC, 0x726EA7}, 0x151145},
    {0x8751CA, {0xA452FF, 0xA050FF, 0x9D4EFF, 0x994CF9, 0x964BF3, 0x9249ED,  // Lipstick and Grapes
                0x8E47E8, 0x8B45E2, 0x8743DC, 0x8341D6, 0x8040D0, 0x7C3ECA,
                0x793CC4, 0x753ABE, 0x7138B8}, 0xD7D3DC},
    {0x272B6D, {0x525AE9, 0x5058E4, 0x4E56DF, 0x4C54DA, 0x4B52D4, 0x4950CF,  // Midnight
                0x474ECA, 0x454CC5, 0x434AC0, 0x4148BB, 0x4046B6, 0x3E44B0,
                0x3C42AB, 0x3A40A6, 0x383EA1}, 0xBFC0CD},
    {0x414141, {0x878787, 0x848484, 0x818181, 0x7E7E7E, 0x7B7B7B, 0x787878,  // Midnight Blues
                0x757575, 0x727272, 0x6F6F6F, 0x6C6C6C, 0x696969, 0x666666,
                0x636363, 0x606060, 0x5D5D5D}, 0xC5C5C5},
    {0x637461, {0x93AB8F, 0x90A88C, 0x8DA489, 0x89A086, 0x869C83, 0x839880,  // Nature Calls
                0x80957C, 0x7C9179, 0x798D76, 0x768973, 0x738570, 0x6F826D,
                0x6C7E69, 0x697A66, 0x667663}, 0xCED0CD},
    {0xAEB6D2, {0xA3B4EE, 0x9FB0E9, 0x9CACE3, 0x98A8DE, 0x94A4D9, 0x91A0D4,  // Periwinkle
                0x8D9CCE, 0x8A98C9, 0x8694C4, 0x8290BF, 0x7F8CB9, 0x7B88B4,
                0x7884AF, 0x7480AA, 0x707CA4}, 0x131F47},
    {0xC1B791, {0xE1CE87, 0xDCCA84, 0xD7C581, 0xD2C17E, 0xCDBC7B, 0xC8B878,  // Safari So Good
                0xC3B375, 0xBEAE72, 0xB9AA6F, 0xB4A56C, 0xAFA169, 0xAA9C66,
                0xA59863, 0xA09360, 0x9B8F5D}, 0x3E350F},
    {0xC7C8CF, {0xC1C4D2, 0xBDBFCD, 0xB9BBC9, 0xB4B7C4, 0xB0B2BF, 0xACAEBB,  // Seattle Blues
                0xA7AAB6, 0xA3A5B2, 0x9FA1AD, 0x9B9DA8, 0x9698A4, 0x92949F,
                0x8E909A, 0x8A8B96, 0x858791}, 0x1A2045},
    {0x03040A, {0x3163FF, 0x3061FF, 0x2F5EFF, 0x2E5CFF, 0x2D5AFF, 0x2C58FF,  // Stealth
                0x2B56FF, 0x2953FD, 0x2851F7, 0x274FF0, 0x264DE9, 0x254BE3,
                0x2448DC, 0x2346D6, 0x2244CF}, 0xAEB1BE},
    {0x1C47C6, {0x2A6BFF, 0x2969FF, 0x2866FF, 0x2764FF, 0x2662FF, 0x255FFF,  // XP Blue
                0x245DFF, 0x235BFF, 0x2258FB, 0x2156F4, 0x2153ED, 0x2051E7,
                0x1F4FE0, 0x1E4CD9, 0x1D4AD2}, 0xC8CCD7},
    {0x9AA280, {0xB9C58F, 0xB5C08C, 0xB0BC89, 0xACB886, 0xA8B383, 0xA4AF80,  // XP Olive
                0xA0AB7C, 0x9CA679, 0x98A276, 0x949E73, 0x909970, 0x8C956D,
                0x889169, 0x848C66, 0x808863}, 0xD8D9D6},
    {0x7B7E8B, {0x9EA0B3, 0x9B9DAF, 0x9799AB, 0x9496A7, 0x9092A3, 0x8D8F9F,  // XP Silver
                0x898B9B, 0x868897, 0x828493, 0x7F818F, 0x7B7D8B, 0x787987,
                0x747683, 0x71727F, 0x6D6F7B}, 0xD5D5D5},
};
// THE BODY IS THE TAB'S 15 W UNDER ITS TOP LIGHT ROW (program_spec.h's tab
// fields: 16 W tall, one light row and the body).
static_assert(std::tuple_size_v<decltype(TabBody::rows)> ==
              static_cast<std::size_t>(kProgramSpec.tab_recess_px -
                                       kProgramSpec.tab_inactive_top_px - 1));
// ONE ROW PER FACE.
static_assert([] {
    for (std::size_t i = 0; i < std::size(kTabBodies); ++i)
        for (std::size_t j = i + 1; j < std::size(kTabBodies); ++j)
            if (kTabBodies[i].face == kTabBodies[j].face) return false;
    return true;
}());
// THE ROW OF `face`. Every built-in palette's Face has a row
// (palette_file.cpp's static_assert over the presets), so a miss is
// unconstructible: no error arm.
constexpr const TabBody& tab_body(uint32_t face) {
    for (const TabBody& t : kTabBodies)
        if (t.face == face) return t;
    std::unreachable();
}
// DEFAULT'S ROW IS METRICS §2.3's CAPTURE, BYTE FOR BYTE: the rest gradient
// and the label CECFD1 on the default Face.
static_assert([] {
    constexpr std::array<uint32_t, 15> kMetrics = {
        0x8C9AB0, 0x8997AC, 0x8694A8, 0x8390A4, 0x7F8DA1, 0x7C899D,
        0x798699, 0x768295, 0x737F91, 0x707C8D, 0x6D7889, 0x6A7585,
        0x677181, 0x646E7E, 0x616B7A};
    const TabBody& d = tab_body(kDefaultFace);
    return d.rows == kMetrics && d.label == 0xCECFD1;
}());
// The GuiPalette members the inactive tab fills (the body's array and the
// label) and THE ACTIVE TAB'S LABEL, the program text (program_text above,
// `ce_text`), counted by palette_file.cpp's coverage check.
inline constexpr std::size_t kTabMemberCount =
    std::tuple_size_v<decltype(TabBody::rows)> + 2;

// -- THE DISABLED CUE'S FADED LOOK (architect 2026-10-10 ~12:20) ---------------

// "Disabled markers should basically not be [very legible]. They just need to
// show themselves as existing … the arrow needs to be legible for me, but the
// text doesn't … since it's disabled, it's inert": a disabled cue (and every
// inert label — a tie follower's cells) wears its LIVE colors BLENDED TOWARD
// THE PANEL'S FACE, "an opacity look … not actually opaque" — three DERIVED
// SOLID COLORS (the palette composites nothing at paint time; each a
// precomputed per-channel mix, round(a + t·(b − a)), filled at every install
// of the program's words so a new Face, `cue` or `invalid_label` moves
// them live; render.cpp's fill_program_palette):
//   the TRIANGLE   the live triangle's color (the `cue` red, or the invalid
//                  color of an invalid cue or a history REMOVED flag,
//                  2026-10-11) 45 % toward the Face;
//   its SHADOW     `cue_shadow` 45 % toward the Face;
//   the LABEL      the resting label's tone (`hilight`, Cool Edit's cue-label
//                  ink, re-verified on three captures 2026-10-10) 65 % toward
//                  the Face — "blended further … a faded, opaque sort of
//                  look", the text legible least of the three.
// A selected inert segment keeps the selected label color (the chrome's
// `label`) so its selection still shows; a disabled cue has no dots.
inline constexpr double kDisabledTriangleMix = 0.45;
inline constexpr double kDisabledShadowMix   = 0.45;
inline constexpr double kDisabledLabelMix    = 0.65;

// Each channel of `from` moved fraction `t` of the way to `to`, rounded half
// up to its byte.
constexpr uint32_t mix_toward(uint32_t from, uint32_t to, double t) {
    uint32_t out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const double a = static_cast<double>((from >> shift) & 0xFF);
        const double b = static_cast<double>((to >> shift) & 0xFF);
        out |= static_cast<uint32_t>(a + t * (b - a) + 0.5) << shift;
    }
    return out;
}
// The triangle of the live color `triangle` (the `cue` or the invalid
// color), faded toward `face`.
constexpr uint32_t disabled_triangle(uint32_t triangle, uint32_t face) {
    return mix_toward(triangle, face, kDisabledTriangleMix);
}
// The cue shadow of `face`, faded toward it.
constexpr uint32_t disabled_cue_shadow(uint32_t face) {
    return mix_toward(tone_of(named_tone("cue_shadow"), face, 0), face,
                      kDisabledShadowMix);
}
// The resting label tone of `face`, faded toward it.
constexpr uint32_t disabled_cue_label(uint32_t face) {
    return mix_toward(tone_of(named_tone("hilight"), face, 0), face,
                      kDisabledLabelMix);
}
// The GuiPalette members these fill (the faded cue triangle, the faded
// invalid triangle, the faded shadow, the faded label), counted by
// palette_file.cpp's coverage check.
inline constexpr std::size_t kDisabledCueMemberCount = 4;
// A mix of 0 is the source and a mix of 1 the target, per channel.
static_assert(mix_toward(0x102030, 0xF0E0D0, 0.0) == 0x102030 &&
              mix_toward(0x102030, 0xF0E0D0, 1.0) == 0xF0E0D0);

} // namespace cool_edit_derive
