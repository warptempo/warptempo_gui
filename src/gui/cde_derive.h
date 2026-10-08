#pragma once

#include "theme_file.h"   // GuiThemeWords, the role table, kGuiThemeCde;
                          // through render.h, GuiChromePick

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

// CDE DERIVES ITS TONES FROM THE TWELVE CHROME KEYS (2026-10-08, the cde
// vocabulary; ONE THEME SYNTAX UNDER EVERY CHROME, architect 2026-10-08
// ~11:00 / ~12:10): the twelve keys a palette may carry (kGuiChromeLines,
// palette_file.h) draw under cde as under the other two, and this header is
// THE ONE DERIVATION there — MOTIF'S OWN: lib/Xm/Color.c's CalculateColorsRGB
// (XmGetColors), the rule dtsession runs on every colour set's background
// to make its foreground, its select colour and its two shadows, PORTED
// STEP FOR STEP from tools/theme_catalog/toolkit_rules.py's motif_colors
// (which the catalog tool has run on CDE's 36 palettes since 2026-10-03),
// integer for integer and double for double, over 16-bit channels (a .dp's
// own units; an 8-bit key is its byte shifted up, exactly Solyaris.dp's
// "#b2004d007a00") and read back as a TrueColor visual stores them (the
// top byte, x_to_8bit).
//
// THE PROOF (static_asserts below): fed Solaris 9's four recorded
// backgrounds it reproduces the capture's shadow and select bytes
// (tmp/research/cde_solaris/report.md §2, each checked on the guidebook
// captures), and fed the Solaris scheme's twelve keys it reproduces
// kGuiThemeCde (theme_file.h) WORD FOR WORD — the hand-set theme IS the
// derivation, which is what makes the compiled theme and a picked block
// one road (unlike Windows 2000's hand-set gray, chrome_derive.h's head, the
// Solaris bytes ARE Motif's arithmetic, so the scheme may run through it).
//
// THE MAPPING (the twelve keys -> the Solaris sets' roles):
//   Chrome         -> colour set 2's background: the ground, the clock
//                     panel, the card's ground, light_3d (Motif's one light
//                     tone is the face), and the set's computed tones —
//                     hilight = its top shadow, shadow AND dk_shadow = its
//                     bottom shadow (Motif has one dark tone), cde_select =
//                     its select colour, the inactive frame's two
//                     (cde_inactive_ts / _bs, read by the laptop's restored
//                     frame and the inactive caption)
//   Chrome Text    -> set 2's foreground: label, clock_text, card_text
//                     (PICKED, never Motif's black-or-white verdict — "we
//                     should never have a toggle"; the compiled scheme's
//                     black is what the verdict gives for AEB2C3, asserted)
//   Title          -> set 1's background: the caption's start AND end (a
//                     FLAT caption: dtwm paints no gradient) and its
//                     computed shadows, cde_title_ts / _bs
//   Title End      -> IGNORED (the one syntax's rule for a chrome without a
//                     gradient)
//   Title Text     -> the caption's text
//   Inactive Title -> the inactive caption's start and end. ABSENT IT FOLLOWS
//                     TITLE (GuiChromePick's accessor, his rule: "on the
//                     tablet there is no inactive state") — CDE'S OWN
//                     INACTIVE IS THE BODY (set 2, the 4-colour mode), which
//                     the compiled scheme records; a picked block that names
//                     no inactive key gives the LAPTOP an active-coloured
//                     inactive frame, as the rule says, its bevel derived
//                     from that colour (cde_inactive_ts / _bs follow the
//                     inactive start, whichever it is)
//   Inactive Title End  -> IGNORED
//   Inactive Title Text -> the inactive caption's text (absent, Title Text)
//   Selection      -> selected_fill, Selection Text -> selected_text: the
//                     inverted list selection's pair as picked (XmList and
//                     XmText invert; the scheme picks black under cream)
//   Field          -> set 4's background: field_ground and its computed
//                     shadows cde_field_ts / _bs; Field Text -> field_text
//   THE CARD TRIO is DERIVED here (a Motif panel in the body: ground, text,
//   its frame the body's bottom shadow), where Windows' derivation keeps
//   its tooltip pair fixed — CDE has no tooltip element to keep.
namespace cde_derive {

// -- Motif's numbers (lib/Xm/ColorP.h, Xm.h.in) ------------------------------

constexpr int64_t kXmMaxShort        = 65535;              // XmMAX_SHORT
constexpr int64_t kXmPercentile      = kXmMaxShort / 100;   // XmCOLOR_PERCENTILE, 655
constexpr int64_t kXmDarkThreshold   = 20 * kXmPercentile;  // XmDEFAULT_DARK_THRESHOLD
constexpr int64_t kXmLiteThreshold   = 93 * kXmPercentile;  // XmDEFAULT_LIGHT_THRESHOLD
constexpr int64_t kXmFgThreshold     = 70 * kXmPercentile;  // XmDEFAULT_FOREGROUND_THRESHOLD

// C's integer division (truncation toward zero) — the port's _cdiv; C++'s
// own `/` on int64_t, named so the port reads as the source.
constexpr int64_t cdiv(int64_t a, int64_t b) { return a / b; }

// A 16-bit X colour triple.
struct X16 {
    int64_t c[3] = {0, 0, 0};
};
constexpr X16 x16_of(uint32_t rgb) {
    return X16{{static_cast<int64_t>((rgb >> 16) & 0xFF) << 8,
                static_cast<int64_t>((rgb >> 8) & 0xFF) << 8,
                static_cast<int64_t>(rgb & 0xFF) << 8}};
}
// x_to_8bit: the top byte of each channel, as a 24-bit TrueColor visual
// stores it.
constexpr uint32_t byte_of(const X16& c) {
    return (static_cast<uint32_t>(c.c[0] >> 8) << 16) |
           (static_cast<uint32_t>(c.c[1] >> 8) << 8) |
           static_cast<uint32_t>(c.c[2] >> 8);
}
constexpr int64_t imin3(int64_t a, int64_t b, int64_t c) {
    return a < b ? (a < c ? a : c) : (b < c ? b : c);
}
constexpr int64_t imax3(int64_t a, int64_t b, int64_t c) {
    return a > b ? (a > c ? a : c) : (b > c ? b : c);
}

// Color.c Brightness: (intensity x 75 + light x 0 + luminosity x 25) / 100
// over the 16-bit channels, the luminosity 0.30 R + 0.59 G + 0.11 B in
// double (the constants are double literals) truncated to int.
constexpr int64_t motif_brightness(const X16& bg) {
    const int64_t r = bg.c[0], g = bg.c[1], b = bg.c[2];
    const int64_t intensity  = (r + g + b) / 3;
    const int64_t luminosity = static_cast<int64_t>(
        0.30 * static_cast<double>(r) + 0.59 * static_cast<double>(g) +
        0.11 * static_cast<double>(b));
    const int64_t light = (imin3(r, g, b) + imax3(r, g, b)) / 2;
    return (intensity * 75 + light * 0 + luminosity * 25) / 100;
}

// One colour set's computed tones (CalculateColorsRGB), as bytes.
struct MotifSet {
    uint32_t fg  = 0;
    uint32_t sel = 0;
    uint32_t ts  = 0;
    uint32_t bs  = 0;
    constexpr bool operator==(const MotifSet&) const = default;
};

// Color.c CalculateColorsRGB on a background: three models by brightness —
// DARK below 20 % (select, bottom and top shadow lifted toward white by 15
// / 30 / 50 % of the headroom), LIGHT above 93 % (each lowered by 15 / 40 /
// 20 % of itself), else MEDIUM (the factors interpolated by brightness:
// select 15, bottom 60 -> 40 down, top 50 -> 60 up). The foreground is black
// above the 70 % foreground threshold, else white.
constexpr MotifSet motif_colors(uint32_t rgb) {
    const X16 bg = x16_of(rgb);
    const int64_t br = motif_brightness(bg);
    const int64_t M  = kXmMaxShort;
    MotifSet out;
    out.fg = br > kXmFgThreshold ? 0x000000u : 0xFFFFFFu;
    X16 sel, bs, ts;
    if (br < kXmDarkThreshold) {
        for (int i = 0; i < 3; ++i) {
            const int64_t v = bg.c[i];
            sel.c[i] = v + cdiv(15 * (M - v), 100);
            bs.c[i]  = v + cdiv(30 * (M - v), 100);
            ts.c[i]  = v + cdiv(50 * (M - v), 100);
        }
    } else if (br > kXmLiteThreshold) {
        for (int i = 0; i < 3; ++i) {
            const int64_t v = bg.c[i];
            sel.c[i] = v - cdiv(v * 15, 100);
            bs.c[i]  = v - cdiv(v * 40, 100);
            ts.c[i]  = v - cdiv(v * 20, 100);
        }
    } else {
        const int64_t f_sel = 15 + cdiv(br * (15 - 15), M);
        const int64_t f_bs  = 60 + cdiv(br * (40 - 60), M);
        const int64_t f_ts  = 50 + cdiv(br * (60 - 50), M);
        for (int i = 0; i < 3; ++i) {
            const int64_t v = bg.c[i];
            sel.c[i] = v - cdiv(v * f_sel, 100);
            bs.c[i]  = v - cdiv(v * f_bs, 100);
            ts.c[i]  = v + cdiv(f_ts * (M - v), 100);
        }
    }
    out.sel = byte_of(sel);
    out.bs  = byte_of(bs);
    out.ts  = byte_of(ts);
    return out;
}

// -- THE PROOF ON THE CAPTURES (report §2: each set's shadows read off the
//    guidebook captures to the byte, set 2's select too — the file manager's
//    pane and every trough; set 1's and set 4's selects are painted nowhere
//    on Solaris and are the rule's own, toolkit_rules.py's motif_colors run
//    2026-10-08 on the same inputs: 974167 and D8D1C6, where the report's
//    table had rounded them up a unit; set 3, the workspace, sits one unit
//    off on the capture — dtsession's PseudoColor allocation — and is no
//    role) ------------------------------------------------------------------
static_assert(motif_colors(0xB24D7A) == MotifSet{0xFFFFFF, 0x974167, 0xDCADC2, 0x57253B});
static_assert(motif_colors(0xAEB2C3) == MotifSet{0x000000, 0x9397A5, 0xDCDEE5, 0x5D6069});
static_assert(motif_colors(0xFFF7E9) == MotifSet{0x000000, 0xD8D1C6, 0xCCC5BA, 0x99948B});
// The catalog's Northern Sky (toolkit_rules.py's motif_colors on 41525C,
// run 2026-10-08): A6AEB2 / 1D252A under white, the medium model.
static_assert(motif_colors(0x41525C).ts == 0xA6AEB2 &&
              motif_colors(0x41525C).bs == 0x1D252A &&
              motif_colors(0x41525C).fg == 0xFFFFFF);

// -- THE PER-ROLE TABLE (the head) ---------------------------------------------

enum class Source {
    Chrome, ChromeText, ChromeTs, ChromeBs, ChromeSelect,
    TitleStart, TitleText, TitleTs, TitleBs,
    InactiveStart, InactiveText, InactiveTs, InactiveBs,
    Selection, SelectionText, Field, FieldText, FieldTs, FieldBs,
};
struct CdeRole {
    const char* role;
    Source      source;
};
inline constexpr CdeRole kCdeRoles[] = {
    {"ground",                    Source::Chrome},
    {"label",                     Source::ChromeText},
    {"hilight",                   Source::ChromeTs},
    {"light_3d",                  Source::Chrome},
    {"shadow",                    Source::ChromeBs},
    {"dk_shadow",                 Source::ChromeBs},
    {"selected_fill",             Source::Selection},
    {"selected_text",             Source::SelectionText},
    {"field_ground",              Source::Field},
    {"field_text",                Source::FieldText},
    {"clock_ground",              Source::Chrome},
    {"clock_text",                Source::ChromeText},
    {"card_ground",               Source::Chrome},
    {"card_text",                 Source::ChromeText},
    {"card_frame",                Source::ChromeBs},
    {"caption_active",            Source::TitleStart},
    {"caption_active_gradient",   Source::TitleStart},
    {"caption_active_text",       Source::TitleText},
    {"caption_inactive",          Source::InactiveStart},
    {"caption_inactive_gradient", Source::InactiveStart},
    {"caption_inactive_text",     Source::InactiveText},
    {"cde_select",                Source::ChromeSelect},
    {"cde_field_ts",              Source::FieldTs},
    {"cde_field_bs",              Source::FieldBs},
    {"cde_title_ts",              Source::TitleTs},
    {"cde_title_bs",              Source::TitleBs},
    {"cde_inactive_ts",           Source::InactiveTs},
    {"cde_inactive_bs",           Source::InactiveBs},
};
inline constexpr auto kCdeRoleIndex = [] {
    std::array<std::size_t, std::size(kCdeRoles)> a{};
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] = theme_role_index(kCdeRoles[i].role);
    return a;
}();
// EVERY ROLE OUTSIDE THE CLEARLOOKS BLOCK IS DERIVED ONCE (the cde block's
// seven included), so a role added to the table fails here until decided.
constexpr bool every_cde_role_decided() {
    for (const std::size_t i : kCdeRoleIndex)
        if (i >= kGuiThemeRoleCount) return false;
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i) {
        if (std::string_view(kGuiThemeRoles[i].name).starts_with("cl_"))
            continue;
        int seen = 0;
        for (const std::size_t k : kCdeRoleIndex)
            if (k == i) ++seen;
        if (seen != 1) return false;
    }
    return true;
}
static_assert(every_cde_role_decided());

// THE DERIVATION: `compiled` (the cde compiled theme, whose cl_ block is
// carried) with every role outside the Clearlooks block written from the
// pick's keys and Motif's tones of its three backgrounds.
constexpr GuiThemeWords derive_cde_chrome(const GuiThemeWords& compiled,
                                          const GuiChromePick& pick) {
    const MotifSet body     = motif_colors(pick.ground);
    const MotifSet title    = motif_colors(pick.title_start);
    const MotifSet inactive = motif_colors(pick.inactive_start());
    const MotifSet field    = motif_colors(pick.field);
    GuiThemeWords w = compiled;
    for (std::size_t i = 0; i < std::size(kCdeRoles); ++i) {
        uint32_t v = 0;
        switch (kCdeRoles[i].source) {
            case Source::Chrome:        v = pick.ground;           break;
            case Source::ChromeText:    v = pick.text;             break;
            case Source::ChromeTs:      v = body.ts;               break;
            case Source::ChromeBs:      v = body.bs;               break;
            case Source::ChromeSelect:  v = body.sel;              break;
            case Source::TitleStart:    v = pick.title_start;      break;
            case Source::TitleText:     v = pick.title_text;       break;
            case Source::TitleTs:       v = title.ts;              break;
            case Source::TitleBs:       v = title.bs;              break;
            case Source::InactiveStart: v = pick.inactive_start(); break;
            case Source::InactiveText:  v = pick.inactive_text();  break;
            case Source::InactiveTs:    v = inactive.ts;           break;
            case Source::InactiveBs:    v = inactive.bs;           break;
            case Source::Selection:     v = pick.selection;        break;
            case Source::SelectionText: v = pick.selection_text;   break;
            case Source::Field:         v = pick.field;            break;
            case Source::FieldText:     v = pick.field_text;       break;
            case Source::FieldTs:       v = field.ts;              break;
            case Source::FieldBs:       v = field.bs;              break;
        }
        w[kCdeRoleIndex[i]] = v & 0xFFFFFFu;
    }
    return w;
}

// THE SOLARIS SCHEME'S TWELVE KEYS (palette_file.h's `solaris` built-in,
// the cde chrome's own) and THE PROOF THAT THE HAND-SET THEME IS THE
// DERIVATION: run on them over the compiled theme itself, every word of
// kGuiThemeCde comes back.
inline constexpr GuiChromePick kSolarisPick{
    0xAEB2C3, 0x000000, 0xB24D7A, 0xB24D7A, 0xFFFFFF,
    0xAEB2C3, 0xAEB2C3, 0x000000,
    0x000000, 0xFFF7E9, 0xFFF7E9, 0x000000};
static_assert(derive_cde_chrome(kGuiThemeCde, kSolarisPick) == kGuiThemeCde);
// Motif's own foreground verdict on the body agrees with the scheme's
// picked black (the head: the text is picked, never the verdict).
static_assert(motif_colors(kSolarisPick.ground).fg == kSolarisPick.text);
// THE INACTIVE CAPTION FOLLOWS while unpicked, its bevel with it.
static_assert([] {
    GuiChromePick p = kSolarisPick;
    p.inactive_title_start.reset();
    p.inactive_title_end.reset();
    p.inactive_title_text.reset();
    const GuiThemeWords w = derive_cde_chrome(kGuiThemeCde, p);
    return w[theme_role_index("caption_inactive")] == 0xB24D7A &&
           w[theme_role_index("caption_inactive_text")] == 0xFFFFFF &&
           w[theme_role_index("cde_inactive_ts")] == 0xDCADC2 &&
           w[theme_role_index("cde_inactive_bs")] == 0x57253B;
}());

} // namespace cde_derive
