#pragma once

#include "program_spec.h"   // kProgramSpec: the case ramp's glyph seat

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
// of that would be inherited from Cool Edit": every chrome role is one of
// the tones below, chrome_derive.h the mapping — the caption's six, a
// scheme's, the exception until they left with the captions 2026-10-21).
// The program's panel — the toolbar band, row 8 and the well's margins —
// takes ONE color, GuiPalette's `face` (Cool Edit's "Dockable
// Window 3D Color", its scheme key `Face`; the flat ground since
// 2026-11-01, materials.h's kGround — no palette role, the palette giving
// no panel color since its presets went, 2026-11-01), and every other tone
// the panel puts down is derived from it HERE, the one owner. The
// measurements below were taken off Cool Edit's own presets and stand as
// the rules' proofs.
//
// THE RULE IS THE ONE THE MEASURER PROVED (§2.1 of his Cool Edit
// metrics, five presets captured at the same pixel positions —
// Default 626C7B, XP Blue 1C47C6, Fire and Brick 8E283A, Lipstick and Grapes
// 8751CA, Seattle Blues C7C8CF): TAKE FACE IN HLS, KEEP ITS HUE AND
// SATURATION, SET L' = a·L + b (clamped to [0, 1]), and back to RGB —
// Python's colorsys round trip, ported below double for double, each channel
// rounded half up to its byte. The fit's worst channel over the five presets
// is under 1.0 RGB (1.4 with rounding) for every tone; constant per-channel
// ratios, Face ± a constant and Windows' dialog rule were tried and rejected
// (errors 4–66). The (a, b) of each tone is the fit's; THE GRIPPER'S DARK LINE IS
// (0.526, −0.004) — an earlier table printed its b as 0, which misses the
// default's bytes by 2; the refit over the same five presets gives −0.004 at 0.89.
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
// for a generated block of tones): every measured tone stands in the table
// below as the record, and NO PAINTER PUTS ONE DOWN since the overlays'
// flat pass (2026-11-02: the dark and the hilight, the overlays' ring and
// the scrub's two lines, the last two painted, left with the ring; the mid,
// the field's two lines and the cue shadow with the flat well the same day;
// the band's recess, its panes' mid lines, the dock bar and the tab strip's
// four Organizer tones with the simple layout, 2026-10-22). EVERY TONE IS A
// RECORD, its proof below. (The GuiPalette member a painted tone carried,
// filled at every install, and Cool Edit's view bar span bevels off the
// waveform's ink, the panel label's swapping pair and their proofs, are git
// history.)
//
// THE BUTTON CASE FOLLOWS FACE TOO (architect 2026-10-10, reopening
// 2026-10-09's "the buttons did not change color with the preset": the fit's
// misses are "off by 5 to 8 … imperceptible … OK to implement"): its face
// ramp, its highlight, its shadow ramp's two ends and the shadow's corner are
// each a CASE TONE below — the Face's HLS hue kept, L' = A + B·L_F and
// S' = C·S_F, per row (the best model the
// measurement found; no closed rule fits within 4). The coefficients are the
// minimax fit over thirteen captured Faces WITH
// THE DEFAULT FACE HELD WITHIN 2 PER CHANNEL OF ITS MEASURED BYTES (the
// app's default look does not move; static_assert below). Worst channel
// error over the twelve other Faces: rows 0–19 5.3–8.8 except row 5 9.4,
// row 10 10.2 and row 6 11.3 (XP Blue and Midnight, which Cool Edit lifts
// brighter than any HLS-lightness rule follows); the highlight 2.7, the
// shadow ends and the corner 2.0 (the shadow is near lightness-independent,
// B ≈ 0, tinted by hue alone). The default-scheme constants they replace
// missed the other Faces by 19–34. NOT FOLLOWING FACE (cool_edit_paint.h's
// constants): the black outer line and the pressed and checked rings,
// byte-identical on every captured Face. (THE CASE'S
// FITS BELOW ARE THE PROGRAM'S HISTORY since 2026-10-23: the keys are flat
// constants, materials.h's paint_key; the fits stay as the record their
// static_asserts prove.)
//
// THE TEXT TONES (2026-10-10; his Cool Edit preset metrics,
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
// text, the field text the never-swapping one — and so did THE PROGRAM'S
// OWN TIME FIELD (row 8's clock, the render player's two fields: the
// `ce_field_text` member, architect 2026-10-10, "whatever Cool Edit seems to
// be doing as far as the dark text, we do that as well") until the
// fortepiano chrome's amber on black, 2026-10-21.
//
// THE PANEL LABEL (2026-10-10, the same ruling; §2c): its ink on a dark Face
// is `label`, L' = .244·L + .750 with the saturation KEPT (refit over seven
// Faces, worst 1 — the label is not the program text's rule) — a record (the
// tone table's `label` row; its engraved swap and the pair's painters went
// with the flat look). THE RULER'S DIGITS AND TICKS were no tone of Face
// (Cool Edit's E0E0E0 on every preset, §2d); the product's are Live's
// (materials.h's THE WELL).
//
// (THE SELECTED TEXT stood here 2026-10-16..10-21 — the program text's
// other member over the waveform's ink — and retired for the fortepiano
// chrome's constant pair, materials.h's kSelectionFill / kSelectionText,
// architect 2026-10-21; closed_questions.md.)

namespace cool_edit_derive {

// THE DEFAULT FACE, Cool Edit's own default scheme's — the measurements'
// source color, read by the proofs alone.
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

// Every tone is derived from the panel's `face` (§2.1); every row is a
// record (the head's ONE ENUMERATOR PER TONE).
struct Tone {
    const char*            name;
    uint32_t               measured;   // Cool Edit's byte on `measured_on`
    double                 a, b;       // the fit: L' = a·L + b
    double                 c = 1.0, d = 0.0;   // S' = c·S + d (the head)
    // The source color `measured` was captured on: the default Face for
    // every Face tone but the swapped text, which the default never shows
    // (the head's proof).
    uint32_t               measured_on = kDefaultFace;
};
// Each with where Cool Edit draws it.
inline constexpr Tone kTones[] = {
    // the dock's recess and the band's stretch where no pane stands (a
    // record since 2026-10-22: the band is the bare ground, the simple layout)
    {"recess",        0x4E5662, 0.798,  0.000},
    // a pane's bottom and right line, a groove's left column, the end bar's
    // and the gripper's mid lines, the time field's ground
    {"mid",           0x414751, 0.668, -0.006},
    // the organizer dock's gripper (dark line)
    {"gripper_dark",  0x32383F, 0.526, -0.004},
    // the band's dark outline, a groove's middle column, the end bar's and
    // the dock bar's dark line, the panel label's shadow
    {"dark",          0x2B2F36, 0.442, -0.004},
    // the organizer tab's right edge (an inactive tab's; a record since the
    // tab strip went, 2026-10-22)
    {"tab_dark",      0x1F2227, 0.324, -0.004},
    // the time field's top and left line
    {"field_dark",    0x191B1F, 0.272, -0.006},
    // the organizer page's right dark line — the active tab's drop shadow
    // (a record since the tab strip went, 2026-10-22)
    {"page_dark",     0x181B1E, 0.248, -0.002},
    // Cool Edit's "Hilight": a pane's top and left line, the band's last
    // line, the gripper's and the end bar's light lines, the dock bar's top
    {"hilight",       0xAEB5BE, 0.492,  0.500},
    // the organizer dock's gripper (light line)
    {"gripper_light", 0xA1A9B4, 0.390,  0.498},
    // the time field's bottom and right line
    {"field_light",   0xABB1BB, 0.272,  0.584},
    // the organizer tab's left and top light line (an inactive tab's; a
    // record since 2026-10-22)
    {"tab_light",     0xB2B8C1, 0.322,  0.586},
    // the organizer tab's bottom line: the page line's run under the inactive
    // tabs (a record since the tab strip went, 2026-10-22)
    {"tab_line",      0xBCC2CA, 0.414,  0.584},
    // the panel label's ink on a dark face (its shadow `dark` at (+1, +1)).
    // (.244, .750) the refit over seven Faces (worst 1; 2026-10-10), the
    // (.264, .740) of the five 2026-10-09 presets missing Midnight, Stealth
    // and Safari by 2.
    {"label",         0xD6DADE, 0.244,  0.750},
    // the cue's and the playhead head's shadow under Cool Edit's tones (the
    // product's shadows retired with the flat well, 2026-11-02)
    {"cue_shadow",    0x31363D, 0.502, -0.004},
    // THE PROGRAM TEXT (2026-10-10, the head's text tones): the clock's,
    // "Files"' and the field digits' light ink on a dark Face — the chrome's
    // label, clock and card text below the swap and its field text at
    // every Face (chrome_derive.h; THE PROGRAM'S TIME FIELDS' DIGITS too,
    // `ce_field_text`, 2026-10-10..10-21, the chrome's ink since, materials.h) —
    // and Cool Edit's swapped dark ink above the
    // threshold, measured on Safari So Good (C1B791, its clock 312A10).
    {"text",         0xEFF0F0, 0.086,  0.900, 0.2, 0.0},
    {"text_dark",     0x312A10, 0.160,  0.025, 0.58, 0.32, 0xC1B791},
};

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
        if (!within_one(tone(t.measured_on, t.a, t.b, t.c, t.d), t.measured))
            return false;
    return true;
}());
// The default Face itself is a byte of the round trip (a = 1, b = 0).
static_assert(tone(kDefaultFace, 1.0, 0.0) == kDefaultFace);

// The tone named `name` (every name is the table's: a miss answers the
// first row, which the callers' asserts catch).
constexpr const Tone& named_tone(const char* name) {
    for (const Tone& t : kTones)
        if (std::string_view(t.name) == name) return t;
    return kTones[0];
}

// -- THE TEXT AND ITS SWAP (2026-10-10, the head) ------------------------------

// A tone off the Face.
constexpr uint32_t tone_of(const Tone& t, uint32_t face) {
    return tone(face, t.a, t.b, t.c, t.d);
}

// THE TEXT THRESHOLD: Cool Edit's program text turns dark above it, keyed on
// the Face's HLS L (L and the RGB mean agree on
// every capture; HSV V and luma are ruled out by the label's swap). The
// captures pin it in (.624, .663] — Dusty Rose's L .624 keeps the light
// text, Safari So Good's .663 swaps — and 0.64 sits inside that interval
// with room on both sides, nearer the light side, which no capture
// contradicts; any value in the interval reproduces all eleven captures.
inline constexpr double kTextSwapLightness = 0.64;
constexpr bool text_swaps(uint32_t face) {
    return hls_of(face).l > kTextSwapLightness;
}

// THE PROGRAM TEXT ON `face`: the light ink, or the dark one past the
// threshold — the chrome's label, clock and card text (chrome_derive.h).
constexpr uint32_t program_text(uint32_t face) {
    return tone_of(named_tone(text_swaps(face) ? "text_dark" : "text"), face);
}
// THE FIELD TEXT ON `face`: the light ink at every Face — the fields' ground
// is the dark `mid`, which never swaps (the head).
constexpr uint32_t field_text(uint32_t face) {
    return tone_of(named_tone("text"), face);
}

// THE TEXT'S PROOF (the clock's flat
// core; Seattle Blues from the first measurement): every captured Face's program text
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

// THE DARK TONE AND THE HILIGHT ON OTHER FACES (Cool Edit's panel label's
// shadow, and its engraved label's ink and shadow) within one per channel
// on the captured Faces, the swapped Grape and Arctic Freeze among them.
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
        if (!within_one(tone_of(named_tone(p.tone), p.face), p.measured))
            return false;
    return true;
}());

// -- THE BUTTON CASE (2026-10-10, the head) ----------------------------------

// ONE CASE TONE: on the Face, L' = a·L + b and S' = c·S, the hue kept (the
// head's per-row fit: a the slope B, b the
// intercept A), and `default_byte` Cool Edit's default scheme's byte as
// measured, which the default Face reproduces within 2.
struct CaseTone {
    double   a, b, c;
    uint32_t default_byte;
};
constexpr uint32_t case_tone(const CaseTone& t, uint32_t face) {
    return tone(face, t.a, t.b, t.c);
}
// THE FACE RAMP — ONE STOP PER W ROW OF THE 20-W GLYPH SEAT, top to bottom
// (the program case's, 2026-10-09 to 2026-10-23; the record), each row's worst miss over the twelve
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
// (The GuiPalette members the case filled, its ramp's array and four lines
// off the panel's Face, left 2026-10-21: the key's face is materials.h's THE
// KEY.)

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
// starts within one of the measured 5D5D5D.
static_assert(case_tone(kCaseShadowFirst, 0xC0C0C0) == 0x5E5E5E &&
              within_one(0x5E5E5E, 0x5D5D5D));

// (THE TAB STRIP'S INACTIVE TAB — kTabBodies, Cool Edit's Organizer body
// gradient and label ink measured per preset off his Wine captures of
// 2026-10-18, no rule fitting them — IS REMOVED with the strip, architect
// 2026-10-22, the simple layout; the measurements are in git
// history.)

// -- THE DISABLED CUE'S FADED LOOK (architect 2026-10-10 ~12:20) ---------------

// "Disabled markers should basically not be [very legible]. They just need to
// show themselves as existing … the arrow needs to be legible for me, but the
// text doesn't … since it's disabled, it's inert": a disabled cue (and every
// inert label — a tie follower's cells) wears its LIVE colors BLENDED TOWARD
// THE FACE IT STANDS ON, "an opacity look … not actually opaque" — two
// DERIVED SOLID COLORS (the palette composites nothing at paint time; each a
// precomputed per-channel mix, round(a + t·(b − a)), filled at every install
// of the palette; render.cpp's fill_program_palette). THE FACE HERE IS THE
// MARKER LANE'S (architect 2026-10-22: truthful to the surface — the cues
// stand on the marker lane), materials.h's kLaneFace 303030 since the flat
// well (architect 2026-11-01 night):
//   the TRIANGLE   the live triangle's color, the `cue` FFAD56 (every
//                  cue's, warp or phase reset), 45 % toward the lane:
//                  A27545;
//   the LABEL      the resting label's ink, the text tone kTextInk B5B5B5
//                  (2026-11-02), 65 % toward the lane — "blended further …
//                  a faded, opaque sort of look", the text legible least of
//                  the two: 5F5F5F.
// A selected inert segment keeps the faded label (the selection shows on the
// canvas, never in the label, architect 2026-11-02; a disabled cue's only
// selection mark the focus's triangle, render.h's SELECTED DISABLED row); a
// disabled cue has no dots.
inline constexpr double kDisabledTriangleMix = 0.45;
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
// The triangle of the live color `triangle` (the `cue`), faded toward
// `face`.
constexpr uint32_t disabled_triangle(uint32_t triangle, uint32_t face) {
    return mix_toward(triangle, face, kDisabledTriangleMix);
}
// The resting label's ink `label` faded toward `face` — the text tone
// kTextInk B5B5B5 since 2026-11-02 (materials.h; Cool Edit's `hilight` tone
// of the Face until 2026-10-21).
constexpr uint32_t disabled_cue_label(uint32_t label, uint32_t face) {
    return mix_toward(label, face, kDisabledLabelMix);
}
// The GuiPalette members these fill (the faded cue triangle, the faded
// label), counted by palette_file.cpp's coverage check.
inline constexpr std::size_t kDisabledCueMemberCount = 2;
// A mix of 0 is the source and a mix of 1 the target, per channel.
static_assert(mix_toward(0x102030, 0xF0E0D0, 0.0) == 0x102030 &&
              mix_toward(0x102030, 0xF0E0D0, 1.0) == 0xF0E0D0);
// THE HEXES ON THE LANE (the head): the `cue` FFAD56 and the text tone
// B5B5B5 over the lane's 303030.
static_assert(disabled_triangle(0xFFAD56, 0x303030) == 0xA27545);
static_assert(disabled_cue_label(0xB5B5B5, 0x303030) == 0x5F5F5F);

} // namespace cool_edit_derive
