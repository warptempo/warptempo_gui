#pragma once

#include "theme_file.h"   // GuiThemeWords, the role table, the compiled themes;
                          // through render.h, GuiChromePick and ChromeSpec
#include "clearlooks_derive.h"  // derive_clearlooks_chrome (the resolver)
#include "cde_derive.h"         // derive_cde_chrome (the resolver)

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

// THE CHROME KNOB (architect 2026-10-08 ~09:40: "I should not make these
// decisions — defer to whatever the Windows designers used … the Windows 95
// picker was great because everything was predefined and I just turned a
// knob for the chrome and everything followed"; RECAST ~11:00, below). A
// SCHEME IS THE CHROME'S TWELVE KEYS (a preset kind of its own since
// 2026-10-08 ~18:15; palette_file.h owns the grammar, kGuiChromeLines): the ground and its text, the caption's start,
// end and text, the inactive caption's three (optional, following the
// active ones while absent), the selection's fill and text, the field's
// ground and text. This header is THE WINDOWS-2000 DERIVATION over them and
// every Windows role's mapping there (clearlooks' is clearlooks_derive.h,
// below).
//
// "A DERIVATION IS ALWAYS ON A SCALE" (architect 2026-10-08 ~11:00): THE
// 3D SET — Hilight, Shadow, 3DLight, DkShadow — derived from the ground by
// Windows' Appearance-dialog rule IS THE ONLY DERIVATION. "WE SHOULD NEVER
// HAVE A TOGGLE": a white-or-black caption text, a white selected text, a
// caption following the ground — anything that would be a switch — is a
// PICKED KEY instead ("the only thing we need to determine for the user is
// the chrome, which is too complicated to do by hand; everything else the
// user can do, there is a handy Copy / Paste in the picker"). The morning's
// derivation of the caption and the selection from the ground, and of the
// text's black-or-white default, is retired (closed_questions.md).
//
// THE COMPILED THEME STAYS EXACTLY ITS RECORDED BYTES: Windows 2000
// Standard's gray face is HAND-SET by Microsoft (D4D0C8's Hilight FFFFFF and
// Shadow 808080 are not the rule's EAE8E3 and 978E7B, asserted below), so the
// knob derives only when THE ACTIVE SCHEME CARRIES THE KEYS — a scheme file,
// or a built-in other than the chrome's own; THE CHROME'S OWN SCHEME carries
// none under its chrome (scheme_record) and leaves the compiled theme
// untouched. The keys are a SCHEME's alone since the split (2026-10-08): a
// palette carrying any of them is the read's hard fail (palette_file.h's
// head, the owner of both grammars). A user who picked D4D0C8 in Windows
// 2000's own dialog lost Standard's white Hilight too, and a Save As of the
// chrome's own scheme installs the derived quartet live: accepted (architect
// 2026-10-08, "Windows is not really the target; creating variation is the
// point").
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
// decided — the build proves every one is listed once, derived or fixed;
// palette_file.cpp proves each key moves the role its picker OLD reads):
//   ground, light_3d, clock_ground       = Chrome (3D Objects; 3DLight = the
//                                          face, the status panel the face,
//                                          as in all 35 recorded schemes)
//   label, clock_text                    = Chrome Text
//   hilight                              = the rule's Hilight
//   shadow                               = the rule's Shadow
//   dk_shadow                            = black (the dialog's; Standard's
//                                          404040 is hand-set)
//   caption_active / _gradient / _text   = Title / Title End / Title Text
//   caption_inactive / _gradient / _text = Inactive Title / Inactive Title
//                                          End / Inactive Title Text, each
//                                          FOLLOWING its active key while
//                                          absent (GuiChromePick's accessors)
//   selected_fill / selected_text        = Selection / Selection Text — A
//                                          PLAIN FIELD, no coupling with the
//                                          caption
//   field_ground / field_text            = Field / Field Text
//   card trio                            = FIXED, the compiled theme's
//                                          (Windows' ToolTip element, no key
//                                          of the twelve; the card frame is
//                                          the tooltip's own black border,
//                                          not DkShadow — Standard records
//                                          000000 beside its 404040)
// A CHROME WITHOUT A GRADIENT would take the title's start alone; Windows
// 2000's caption paints both ends. THE CLEARLOOKS BLOCK (cl_ roles) is
// carried from the compiled theme, unread by the win2000 painters.
//
// UNDER CLEARLOOKS THE SAME TWELVE KEYS DRAW (architect 2026-10-08 ~12:10,
// ONE THEME SYNTAX UNDER EVERY CHROME): their derivation there is GTK's and
// metacity's own arithmetic, ported whole — clearlooks_derive.h, whose head
// owns its mapping (the title end ignored, the frame off the title) and its
// proof. live_chrome_words below picks the vocabulary's derivation.
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

// -- THE PER-ROLE TABLE (the head) ---------------------------------------------

enum class Source {
    Chrome, ChromeText, Hilight, Shadow, Black,
    TitleStart, TitleEnd, TitleText,
    InactiveTitleStart, InactiveTitleEnd, InactiveTitleText,
    Selection, SelectionText, Field, FieldText,
};
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
    {"selected_fill",             Source::Selection},
    {"selected_text",             Source::SelectionText},
    {"field_ground",              Source::Field},
    {"field_text",                Source::FieldText},
    {"clock_ground",              Source::Chrome},
    {"clock_text",                Source::ChromeText},
    {"caption_active",            Source::TitleStart},
    {"caption_active_gradient",   Source::TitleEnd},
    {"caption_active_text",       Source::TitleText},
    {"caption_inactive",          Source::InactiveTitleStart},
    {"caption_inactive_gradient", Source::InactiveTitleEnd},
    {"caption_inactive_text",     Source::InactiveTitleText},
};
// THE FIXED ROLES: Windows' ToolTip element, the compiled theme's bytes
// kept (the head).
inline constexpr const char* kKnobFixedRoles[] = {
    "card_ground", "card_text", "card_frame",
};

// Each derived role's index in the theme's role table, found once.
inline constexpr auto kKnobRoleIndex = [] {
    std::array<std::size_t, std::size(kKnobRoles)> a{};
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] = theme_role_index(kKnobRoles[i].role);
    return a;
}();

// EVERY WINDOWS ROLE IS DECIDED ONCE: each role outside the generated
// Clearlooks block and the hand-set CDE block (both carried unread by the
// Windows painters) is in exactly one of the two lists, and each listed name
// is a role — so a Windows role added to the table is a build failure here
// until it is decided.
constexpr bool every_windows_role_decided() {
    for (const std::size_t i : kKnobRoleIndex)
        if (i >= kGuiThemeRoleCount) return false;
    for (const char* f : kKnobFixedRoles)
        if (theme_role_index(f) >= kGuiThemeRoleCount) return false;
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i) {
        const std::string_view name = kGuiThemeRoles[i].name;
        if (name.starts_with("cl_") || name.starts_with("cde_")) continue;
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
// derived role written over from the pick's keys and the 3D set the rule
// derives from its ground.
constexpr GuiThemeWords derive_windows_chrome(const GuiThemeWords& compiled,
                                              const GuiChromePick& pick) {
    const Quartet q = windows_dialog(pick.ground);
    GuiThemeWords w = compiled;
    for (std::size_t i = 0; i < std::size(kKnobRoles); ++i) {
        uint32_t v = 0;
        switch (kKnobRoles[i].source) {
            case Source::Chrome:             v = pick.ground;            break;
            case Source::ChromeText:         v = pick.text;              break;
            case Source::Hilight:            v = q.hilight;              break;
            case Source::Shadow:             v = q.shadow;               break;
            case Source::Black:              v = 0x000000u;              break;
            case Source::TitleStart:         v = pick.title_start;       break;
            case Source::TitleEnd:           v = pick.title_end;         break;
            case Source::TitleText:          v = pick.title_text;        break;
            case Source::InactiveTitleStart: v = pick.inactive_start();  break;
            case Source::InactiveTitleEnd:   v = pick.inactive_end();    break;
            case Source::InactiveTitleText:  v = pick.inactive_text();   break;
            case Source::Selection:          v = pick.selection;         break;
            case Source::SelectionText:      v = pick.selection_text;    break;
            case Source::Field:              v = pick.field;             break;
            case Source::FieldText:          v = pick.field_text;        break;
        }
        w[kKnobRoleIndex[i]] = v & 0xFFFFFFu;
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
// RAINY DAY WHOLE, as its catalog entry records it (ButtonFace 8399B1,
// ActiveTitle 4F657D → GradientActiveTitle 80B4D0 under TitleText FFFFFF,
// InactiveTitle 808080 → B0BCD0 under C1CCD9, Hilight 4F657D under FFFFFF,
// Window FFFFFF / WindowText 000000): every key lands on its role, the
// derived Hilight and Shadow are its recorded ButtonHilight C1CCD9 and
// ButtonShadow 4F657D, and the card stays the compiled theme's.
inline constexpr GuiChromePick kRainyDayPick{
    0x8399B1, 0x000000, 0x4F657D, 0x80B4D0, 0xFFFFFF,
    0x808080, 0xB0BCD0, 0xC1CCD9,
    0x4F657D, 0xFFFFFF, 0xFFFFFF, 0x000000};
inline constexpr GuiThemeWords kRainyDayCheck =
    derive_windows_chrome(kGuiThemeWin2000, kRainyDayPick);
static_assert(kRainyDayCheck[theme_role_index("hilight")] == 0xC1CCD9);
static_assert(kRainyDayCheck[theme_role_index("shadow")] == 0x4F657D);
static_assert(kRainyDayCheck[theme_role_index("selected_fill")] == 0x4F657D);
static_assert(kRainyDayCheck[theme_role_index("caption_active")] == 0x4F657D);
static_assert(kRainyDayCheck[theme_role_index("caption_active_gradient")] == 0x80B4D0);
static_assert(kRainyDayCheck[theme_role_index("caption_inactive_text")] == 0xC1CCD9);
static_assert(kRainyDayCheck[theme_role_index("card_frame")] == 0x000000);
static_assert(kRainyDayCheck[theme_role_index("card_ground")] == 0xFFFFE1);
// THE INACTIVE CAPTION FOLLOWS while unpicked.
static_assert([] {
    GuiChromePick p = kRainyDayPick;
    p.inactive_title_start.reset();
    p.inactive_title_text.reset();
    const GuiThemeWords w = derive_windows_chrome(kGuiThemeWin2000, p);
    return w[theme_role_index("caption_inactive")] == 0x4F657D &&
           w[theme_role_index("caption_inactive_gradient")] == 0xB0BCD0 &&
           w[theme_role_index("caption_inactive_text")] == 0xFFFFFF;
}());

} // namespace chrome_derive

// THE CHROME'S LIVE WORDS — THE ONE RESOLVER the install reads
// (install_palette / install_chrome_pick, render.cpp): the live chrome's
// compiled theme, with the knob derived over it when the live scheme
// carries the keys — Windows' dialog rule under windows-2000 (above), GTK's and
// metacity's arithmetic under clearlooks (clearlooks_derive.h), Motif's
// XmGetColors under cde (cde_derive.h, 2026-10-08).
inline GuiThemeWords live_chrome_words(const ChromeSpec& spec,
                                       const std::optional<GuiChromePick>& pick) {
    const GuiThemeWords& compiled = chrome_theme_words(spec);
    if (!pick) return compiled;
    switch (spec.vocabulary) {
        case GuiChromeVocabulary::Win2000:
            return chrome_derive::derive_windows_chrome(compiled, *pick);
        case GuiChromeVocabulary::Clearlooks:
            return clearlooks_derive::derive_clearlooks_chrome(
                compiled, *pick, clearlooks_derive::kGeometry);
        case GuiChromeVocabulary::Cde:
            return cde_derive::derive_cde_chrome(compiled, *pick);
    }
    return compiled;
}
