#pragma once

#include "theme_file.h"   // GuiThemeWords, the role table, the compiled themes;
                          // through render.h, GuiChromePick and ChromeSpec

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

// THE CHROME KNOB (architect 2026-10-08 ~09:40: "I should not make these
// decisions — defer to whatever the Windows designers used … the Windows 95
// picker was great because everything was predefined and I just turned a
// knob for the chrome and everything followed"). UNDER WINDOWS-2000 THE
// PALETTE MAY PICK TWO CHROME ELEMENTS — "Chrome", the ground (Windows'
// Appearance dialog's 3D Objects), and "Chrome Text", its text (3D Objects'
// font color) — the palette file's `chrome_ground` / `chrome_text` lines
// (palette_file.h owns the grammar), and EVERY OTHER CHROME SHADE IS DERIVED
// FROM THEM AS WINDOWS DERIVED IT. This header is the derivation's one owner.
//
// THE COMPILED THEME STAYS EXACTLY ITS RECORDED BYTES: Windows 2000
// Standard's gray face is HAND-SET by Microsoft (D4D0C8's Hilight FFFFFF and
// Shadow 808080 are not the rule's EAE8E3 and 978E7B, asserted below), so the
// knob derives only when the palette CARRIES the chrome lines; a palette
// without them leaves the compiled theme untouched. A user who picked D4D0C8
// in Windows 2000's own dialog lost Standard's white Hilight too.
//
// THE RULE (Windows' Appearance dialog on a picked 3D face): convert the face
// to shlwapi's 240-scale integer HLS; HILIGHT keeps the hue and the
// saturation with the lightness HALFWAY TO WHITE, the half rounded up; SHADOW
// keeps them with TWO THIRDS of the lightness, floored; 3DLIGHT is the face;
// DKSHADOW is black. No open source carries the rule (ReactOS's desk.cpl
// sets the face and the text alone, Wine's ColorAdjustLuma is a stub): it is
// reconstructed from Microsoft's own scheme bytes and is BYTE-EXACT on Brick,
// Lilac, Maple, Pumpkin, Rainy Day, Rose, Desert and Spruce (docs/themes/
// catalog.json; the checks below), within one level on Slate and Wheat, and
// the designers' hand-tuning within a few levels elsewhere. THE ARITHMETIC
// is Wine's ColorRGBToHLS / ColorHLSToRGB (dlls/shlwapi/ordinal.c at
// wine-10.0), integer for integer, PORTED STEP FOR STEP from
// tools/theme_catalog/toolkit_rules.py's windows_dialog, which the catalog
// tool has run since 2026-10-03.
//
// THE PER-ROLE TABLE (kKnobRoles below; Windows' 21 chrome roles, each
// decided — the build proves every one is listed once, derived or fixed):
//   ground, light_3d, clock_ground       = Chrome (3D Objects; 3DLight = the
//                                          face, the status panel the face,
//                                          as in all 35 recorded schemes)
//   label, clock_text                    = Chrome Text
//   hilight                              = the rule's Hilight
//   shadow                               = the rule's Shadow
//   dk_shadow                            = black (the dialog's; Standard's
//                                          404040 is hand-set)
//   selected_fill / selected_text        = the rule's Shadow / white
//   caption_active / _gradient / _text   = Shadow / Hilight / white
//   caption_inactive / _gradient / _text = Shadow / Shadow / Chrome
//   field pair, card trio                = FIXED, the compiled theme's
//                                          (Windows' Window and ToolTip
//                                          elements, white field and
//                                          FFFFE1 tooltip in every stock
//                                          scheme; the card frame is the
//                                          tooltip's own black border, not
//                                          DkShadow — Standard records
//                                          000000 beside its 404040)
// THE CAPTION AND THE SELECTION FOLLOW THE GROUND AS THE DESIGNERS' OWN DARK
// SCHEMES DID (the ruling): Rainy Day's face 8399B1 derives Shadow 4F657D,
// its recorded ActiveTitle and its recorded Hilight exactly (asserted
// below); Slate's and Eggplant's within 1–4. The active caption's END is the
// derived Hilight — the designers' ends are brighter hand-picked tones
// (Rainy Day's 80B4D0) and no rule fits them, so Hilight is the derived
// STAND-IN, said plainly. The INACTIVE caption is flat at Shadow under the
// face: Windows 95 Standard's own structure (808080 flat under its C0C0C0
// face), the face being the inactive title text of 11 of the 35 recorded
// schemes, every Standard among them. The caption's text and the selected
// text are WHITE, fixed, not picked: every designers' dark scheme's.
// THE CLEARLOOKS BLOCK (cl_ roles) is carried from the compiled theme, unread
// by the win2000 painters.
//
// UNDER CLEARLOOKS THE KNOB IS NOT OFFERED (its derivation is GTK's shade
// table, not yet ported): the picker's chooser lists no chrome element there
// (color_picker::chrome_elements_offered), and a palette carrying chrome
// lines LOADS, ITS LINES CARRIED AND UNREAD — live_chrome_words answers the
// compiled theme — because a palette saved under windows-2000 stands in the
// folder when the Settings chrome row switches to clearlooks, a state the GUI
// constructs and so one that must load (the two-category rule). Save and
// Save As there keep the lines the active preset carries.
namespace chrome_derive {

// -- WINDOWS' 240-SCALE HLS (Wine dlls/shlwapi/ordinal.c) ----------------------

// Python's // (the port's source): floor division. The source's divisions
// are on non-negative values, where it is C's; the floor is kept so the port
// cannot part from it on any input.
constexpr int floor_div(int a, int b) {
    const int q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

struct Hls {
    int h = 0;
    int l = 0;
    int s = 0;
};

// ColorRGBToHLS: an achromatic color's hue is 160, as native returns.
constexpr Hls rgb_to_hls(uint32_t rgb) {
    const int r = static_cast<int>((rgb >> 16) & 0xFF);
    const int g = static_cast<int>((rgb >> 8) & 0xFF);
    const int b = static_cast<int>(rgb & 0xFF);
    const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const int L = floor_div((mx + mn) * 240 + 255, 510);
    if (mx == mn) return Hls{160, L, 0};
    const int d = mx - mn;
    const int S = L <= 120
        ? floor_div(floor_div(mx + mn, 2) + d * 240, mx + mn)
        : floor_div(floor_div(510 - mx - mn, 2) + d * 240, 510 - mx - mn);
    const int rn = floor_div(floor_div(d, 2) + mx * 40 - r * 40, d);
    const int gn = floor_div(floor_div(d, 2) + mx * 40 - g * 40, d);
    const int bn = floor_div(floor_div(d, 2) + mx * 40 - b * 40, d);
    int H = r == mx ? bn - gn : (g == mx ? 80 + rn - bn : 160 + gn - rn);
    if (H < 0)        H += 240;
    else if (H > 240) H -= 240;
    return Hls{H, L, S};
}

// ConvertHue.
constexpr int convert_hue(int h, int m1, int m2) {
    h = h > 240 ? h - 240 : (h < 0 ? h + 240 : h);
    if (h > 160)      return m1;
    else if (h > 120) h = 160 - h;
    else if (h > 40)  return m2;
    return floor_div(h * (m2 - m1) + 20, 40) + m1;
}

// ColorHLSToRGB, with its GET_RGB scaling, (v x 255 + 120) / 240.
constexpr uint32_t hls_to_rgb(int H, int L, int S) {
    if (S != 0) {
        const int m2 = L > 120 ? S + L - floor_div(S * L + 120, 240)
                               : floor_div((S + 240) * L + 120, 240);
        const int m1 = L * 2 - m2;
        const auto f = [&](int h) {
            return static_cast<uint32_t>(
                floor_div(convert_hue(h, m1, m2) * 255 + 120, 240));
        };
        return (f(H + 80) << 16) | (f(H) << 8) | f(H - 80);
    }
    const uint32_t v = static_cast<uint32_t>(floor_div(L * 255, 240));
    return (v << 16) | (v << 8) | v;
}

// -- THE DIALOG'S RULE ---------------------------------------------------------

struct Quartet {
    uint32_t hilight   = 0;
    uint32_t light_3d  = 0;
    uint32_t shadow    = 0;
    uint32_t dk_shadow = 0;
    constexpr bool operator==(const Quartet&) const = default;
};

// toolkit_rules.windows_dialog: the picked face's relief quartet.
constexpr Quartet windows_dialog(uint32_t face) {
    const Hls c = rgb_to_hls(face);
    return Quartet{hls_to_rgb(c.h, c.l + floor_div(240 - c.l + 1, 2), c.s),
                   face & 0xFFFFFFu,
                   hls_to_rgb(c.h, floor_div(2 * c.l, 3), c.s),
                   0x000000u};
}

// THE TEXT'S ONE DEFAULT (architect 2026-10-08, the brief's ruling): the
// first pick of either chrome element creates BOTH lines, and a first pick of
// the ground seeds the text BLACK where the ground's HLS lightness is at
// least half the 240 scale and WHITE below it — once, at that pick; the text
// is his from then on.
constexpr uint32_t default_text(uint32_t ground) {
    return rgb_to_hls(ground).l >= 120 ? 0x000000u : 0xFFFFFFu;
}

// -- THE PER-ROLE TABLE (the head) ---------------------------------------------

enum class Source { Chrome, ChromeText, Hilight, Shadow, Black, White };
struct KnobRole {
    const char* role;
    Source      source;
};
inline constexpr KnobRole kKnobRoles[] = {
    {"ground",                    Source::Chrome},
    {"label",                     Source::ChromeText},
    {"hilight",                   Source::Hilight},
    {"light_3d",                  Source::Chrome},
    {"shadow",                    Source::Shadow},
    {"dk_shadow",                 Source::Black},
    {"selected_fill",             Source::Shadow},
    {"selected_text",             Source::White},
    {"clock_ground",              Source::Chrome},
    {"clock_text",                Source::ChromeText},
    {"caption_active",            Source::Shadow},
    {"caption_active_gradient",   Source::Hilight},
    {"caption_active_text",       Source::White},
    {"caption_inactive",          Source::Shadow},
    {"caption_inactive_gradient", Source::Shadow},
    {"caption_inactive_text",     Source::Chrome},
};
// THE FIXED ROLES: Windows' Window and ToolTip elements, the compiled
// theme's bytes kept (the head).
inline constexpr const char* kKnobFixedRoles[] = {
    "field_ground", "field_text", "card_ground", "card_text", "card_frame",
};

// Each derived role's index in the theme's role table, found once.
inline constexpr auto kKnobRoleIndex = [] {
    std::array<std::size_t, std::size(kKnobRoles)> a{};
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] = theme_role_index(kKnobRoles[i].role);
    return a;
}();

// EVERY WINDOWS ROLE IS DECIDED ONCE: each role outside the generated
// Clearlooks block is in exactly one of the two lists, and each listed name
// is a role — so a Windows role added to the table is a build failure here
// until it is decided.
constexpr bool every_windows_role_decided() {
    for (const std::size_t i : kKnobRoleIndex)
        if (i >= kGuiThemeRoleCount) return false;
    for (const char* f : kKnobFixedRoles)
        if (theme_role_index(f) >= kGuiThemeRoleCount) return false;
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i) {
        const std::string_view name = kGuiThemeRoles[i].name;
        if (name.starts_with("cl_")) continue;
        int seen = 0;
        for (const std::size_t k : kKnobRoleIndex)
            if (k == i) ++seen;
        for (const char* f : kKnobFixedRoles)
            if (name == f) ++seen;
        if (seen != 1) return false;
    }
    return true;
}
static_assert(every_windows_role_decided());

// THE DERIVATION: `compiled` (the windows-2000 compiled theme) with every
// derived role written over from `ground` and `text`.
constexpr GuiThemeWords derive_windows_chrome(const GuiThemeWords& compiled,
                                              uint32_t ground, uint32_t text) {
    const Quartet q = windows_dialog(ground);
    GuiThemeWords w = compiled;
    for (std::size_t i = 0; i < std::size(kKnobRoles); ++i) {
        uint32_t v = 0;
        switch (kKnobRoles[i].source) {
            case Source::Chrome:     v = ground & 0xFFFFFFu; break;
            case Source::ChromeText: v = text & 0xFFFFFFu;   break;
            case Source::Hilight:    v = q.hilight;          break;
            case Source::Shadow:     v = q.shadow;           break;
            case Source::Black:      v = 0x000000u;          break;
            case Source::White:      v = 0xFFFFFFu;          break;
        }
        w[kKnobRoleIndex[i]] = v;
    }
    return w;
}

// -- THE CHECKS (the research's values, from docs/themes/catalog.json's
//    recorded schemes and tools/theme_catalog/toolkit_rules.py's asserts) --

// Rainy Day, Brick, Lilac, Rose, Maple and Pumpkin: the recorded quartet
// (ButtonHilight, ButtonLight, ButtonShadow, ButtonDkShadow) byte for byte.
static_assert(windows_dialog(0x8399B1) == Quartet{0xC1CCD9, 0x8399B1, 0x4F657D, 0});
static_assert(windows_dialog(0xC2BFA5) == Quartet{0xE1E0D2, 0xC2BFA5, 0x8D8961, 0});
static_assert(windows_dialog(0xAEA8D9) == Quartet{0xD8D5EC, 0xAEA8D9, 0x5A4EB1, 0});
static_assert(windows_dialog(0xCFAFB7) == Quartet{0xE7D8DC, 0xCFAFB7, 0x9F6070, 0});
static_assert(windows_dialog(0xE6D8AE) == Quartet{0xF2ECD7, 0xE6D8AE, 0xC6A646, 0});
static_assert(windows_dialog(0xECD59D) == Quartet{0xF5EACF, 0xECD59D, 0xD7A52F, 0});
// The gray face is not the rule's: D4D0C8 derives EAE8E3 (the XP / 7
// dialog's measured Hilight) and 978E7B, where Standard records FFFFFF and
// 808080 — why the compiled theme is never run through the rule.
static_assert(windows_dialog(0xD4D0C8).hilight == 0xEAE8E3);
static_assert(windows_dialog(0xD4D0C8).shadow == 0x978E7B);
// Northern Sky's ground (CDE), the picker's test case: 98ABB6 / 2C363D.
static_assert(windows_dialog(0x41525C).hilight == 0x98ABB6);
static_assert(windows_dialog(0x41525C).shadow == 0x2C363D);
// RAINY DAY WHOLE: the derived selection and active caption start are its
// recorded Hilight and ActiveTitle, 4F657D.
inline constexpr GuiThemeWords kRainyDayCheck =
    derive_windows_chrome(kGuiThemeWin2000, 0x8399B1, 0x000000);
static_assert(kRainyDayCheck[theme_role_index("selected_fill")] == 0x4F657D);
static_assert(kRainyDayCheck[theme_role_index("caption_active")] == 0x4F657D);
static_assert(kRainyDayCheck[theme_role_index("caption_active_gradient")] == 0xC1CCD9);
static_assert(kRainyDayCheck[theme_role_index("card_frame")] == 0x000000);
static_assert(kRainyDayCheck[theme_role_index("field_ground")] == 0xFFFFFF);
// The text's default: black on Rainy Day's face (L 145), white on Northern
// Sky's (L 75).
static_assert(default_text(0x8399B1) == 0x000000);
static_assert(default_text(0x41525C) == 0xFFFFFF);

} // namespace chrome_derive

// THE CHROME'S LIVE WORDS — THE ONE RESOLVER the install reads
// (install_palette / install_chrome_pick, render.cpp): the live chrome's
// compiled theme, with the knob derived over it when the palette picks the
// chrome AND the chrome is windows-2000 (the head: under clearlooks the
// lines are carried and unread).
inline GuiThemeWords live_chrome_words(const ChromeSpec& spec,
                                       const std::optional<GuiChromePick>& pick) {
    const GuiThemeWords& compiled = chrome_theme_words(spec);
    if (!pick || spec.vocabulary != GuiChromeVocabulary::Win2000)
        return compiled;
    return chrome_derive::derive_windows_chrome(compiled, pick->ground,
                                                pick->text);
}
