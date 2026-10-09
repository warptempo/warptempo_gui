#pragma once

#include "render.h"   // GuiPalette, GuiColor: the painted tones' members

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// COOL EDIT DERIVES ITS PANEL'S TONES FROM ONE COLOR (architect 2026-10-09,
// the program is Cool Edit; the Clearlooks precedent: one source color, the
// tones its arithmetic). The program's panel — the toolbar band, the dock
// bar, row 8 and, in later parts, the canvas column's frames — takes ONE
// palette role, `face` (Cool Edit's "Dockable Window 3D Color", its scheme
// key `Face`; palette_file.h's role table, default 626C7B under every
// chrome), and every other tone the panel puts down is derived from it HERE,
// the one owner, as clearlooks_derive.h is Clearlooks'.
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
//
// ONE ENUMERATOR PER TONE A PAINTER PUTS DOWN, BY NEED (the cl_ block's
// rule): every measured tone stands in the table below as the record, and
// those the painters read carry a GuiPalette member (the COOL EDIT BLOCK,
// render.h's palette struct), filled at every install of the program's words
// (fill_program_palette, render.cpp) — a pick of Face moves them all live.
// The toolbar band and row 8 read seven: the recess, the mid, the dark, the
// hilight, the time field's two lines and the panel label's ink; THE CANVAS
// COLUMN (2026-10-09) the mid (the view bar's top line, the ruler's
// ground), the hilight (the view bar's and the ruler's light lines, the
// cues' labels), the dark and the cue shadow — and TWO TONES OFF THE
// WAVEFORM'S INK rather than the face, the view bar's span bevels
// (ToneSource::Ink below; their proof over Cool Edit's five presets).
//
// NOT DERIVED (constants of the painter, cool_edit_paint.h): THE BUTTON
// CASE — its face ramp, its highlight and shadow, its black line, the
// pressed and checked rings — because "the buttons did not change color with
// the preset" (architect 2026-10-09): the measured tint follows Face at 5 to
// 10 % of its chroma with k varying 0.05–0.10 across the presets and a
// brightness lift that is no clean function of Face (METRICS §1.4,
// UNCERTAIN 1), so the default scheme's bytes are authored as they stand.
// The time field's digits (EFF0F0) likewise, a constant of the field's
// painter.
//
// THE LIGHT-FACE TEXT SWAP — MEASURED, NOT BUILT (architect 2026-10-09: a
// dark panel is the one he draws): on Seattle Blues (Face L = 0.796) Cool
// Edit swaps the panel label's ink and shadow — the dark tone (52545F, the
// `dark` fit) as ink and the light one (E2E2E6, the `hilight` fit) as its
// shadow, an engraved look — and its program text turns dark (1A1D36 for
// EFF0F0). The threshold lies somewhere between L = 0.555 (Lipstick, light
// text) and 0.796 (Seattle), unpinned by the five presets (METRICS
// UNCERTAIN 3). The panel here keeps the dark-face pair at every Face.

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

// THE ONE DERIVATION: Face in HLS, L' = a·L + b clamped, H and S kept.
constexpr uint32_t tone(uint32_t face, double a, double b) {
    const Hls f = rgb_to_hls(static_cast<double>((face >> 16) & 0xFF) / 255.0,
                             static_cast<double>((face >> 8) & 0xFF) / 255.0,
                             static_cast<double>(face & 0xFF) / 255.0);
    double l = a * f.l + b;
    l = l < 0.0 ? 0.0 : (l > 1.0 ? 1.0 : l);
    if (f.s == 0.0) {
        const uint32_t v = byte_of(l);
        return (v << 16) | (v << 8) | v;
    }
    const double m2 = l <= 0.5 ? l * (1.0 + f.s) : l + f.s - l * f.s;
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
    uint32_t               measured;   // Cool Edit's default scheme's byte
    double                 a, b;       // the fit: L' = a·L + b
    GuiColor GuiPalette::* member;     // the painted ones; nullptr = record
    ToneSource             source = ToneSource::Face;
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
    // the organizer tab's right edge
    {"tab_dark",      0x1F2227, 0.324, -0.004, nullptr},
    // the time field's top and left line
    {"field_dark",    0x191B1F, 0.272, -0.006, &GuiPalette::ce_field_dark},
    // the organizer page's right dark line
    {"page_dark",     0x181B1E, 0.248, -0.002, nullptr},
    // Cool Edit's "Hilight": a pane's top and left line, the band's last
    // line, the gripper's and the end bar's light lines, the dock bar's top
    {"hilight",       0xAEB5BE, 0.492,  0.500, &GuiPalette::ce_hilight},
    // the organizer dock's gripper (light line)
    {"gripper_light", 0xA1A9B4, 0.390,  0.498, nullptr},
    // the time field's bottom and right line
    {"field_light",   0xABB1BB, 0.272,  0.584, &GuiPalette::ce_field_light},
    // the organizer tab's left and top light line
    {"tab_light",     0xB2B8C1, 0.322,  0.586, nullptr},
    // the organizer tab's bottom line
    {"tab_line",      0xBCC2CA, 0.414,  0.584, nullptr},
    // the panel label's ink on a dark face (its shadow `dark` at (+1, +1))
    {"label",         0xD6DADE, 0.264,  0.740, &GuiPalette::ce_label},
    // the cue's and the playhead head's shadow, one quantum right of each
    // of the triangle's rows (METRICS §4.2, §4.3; the marker lane and the
    // ruler, 2026-10-09)
    {"cue_shadow",    0x31363D, 0.502, -0.004, &GuiPalette::ce_cue_shadow},
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
// Cool Edit's default scheme within one per channel.
static_assert([] {
    for (const Tone& t : kTones)
        if (t.source == ToneSource::Face &&
            !within_one(tone(kDefaultFace, t.a, t.b), t.measured))
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
constexpr const Tone& ink_tone(const char* name) {
    for (const Tone& t : kTones)
        if (std::string_view(t.name) == name) return t;
    return kTones[0];
}
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

} // namespace cool_edit_derive
