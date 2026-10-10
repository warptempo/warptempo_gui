#pragma once

#include "theme_file.h"        // GuiThemeWords, the role table, the compiled
                               // captions; through render.h, GuiChromePick
                               // and ChromeSpec
#include "palette_file.h"      // GuiPaletteWords, palette_role_index, the
                               // default palette (the checks)
#include "cool_edit_derive.h"  // the tones the chrome wears

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

// THE CHROME INHERITS COOL EDIT (architect 2026-10-10 ~10:20–11:30: "the
// chrome color should just be the Cool Edit theme color … we're getting rid
// of chrome inside the body. The caption remains the only thing that's
// outside of Cool Edit … even the font color, all of that would be inherited
// from Cool Edit"; "whatever luminous rules we previously had are superseded
// by the Cool Edit follow. We follow Cool Edit's theming"). This header is
// THE ONE OWNER OF EVERY CHROME ROLE'S SOURCE: the caption's six are THE
// SCHEME'S (a scheme is the caption alone, palette_file.h's kGuiChromeLines),
// and every other role is A TONE OF THE LIVE PALETTE — the panel's `face`,
// the tones Cool Edit derives from it, the waveform's ink — by Cool Edit's
// own arithmetic (cool_edit_derive.h, whose head has the measurements and
// the proofs), re-derived at every install of the palette (render.cpp's
// install family), so a Waveform-scope pick of the Face or the ink moves the
// chrome live.
//
// THE PER-ROLE TABLE (kChromeRoles below; the role table's 21, each decided
// once — the build proves every role is listed exactly once, the caption's
// six from the scheme and no other role from it; palette_file.cpp proves
// each scheme key moves the role its picker OLD reads):
//   ground, light_3d, clock_ground       = the Face (Cool Edit's "Dockable
//                                          Window 3D Color"; 3DLight the face,
//                                          as Windows' own schemes record it)
//   hilight                              = the `hilight` tone (Cool Edit's
//                                          Hilight: a pane's top and left
//                                          line)
//   shadow                               = the `mid` tone (a pane's bottom
//                                          and right line)
//   dk_shadow                            = black (Cool Edit's case outline,
//                                          000000 on all nine captured Faces)
//   label, clock_text, card_text         = the program text WITH ITS SWAP
//                                          (cool_edit_derive::program_text:
//                                          light on a dark Face, dark past
//                                          the text threshold)
//   field_ground                         = the `mid` tone (Cool Edit's dark
//                                          time field)
//   field_text                           = the light text that NEVER swaps
//                                          (cool_edit_derive::field_text: the
//                                          field's ground is dark at every
//                                          Face)
//   selected_fill                        = the waveform's ink
//   selected_text                        = the ink at HLS L 0.10 (the
//                                          `selected_text` tone; architect
//                                          2026-10-10, "I agree with your
//                                          call")
//   card_ground                          = the Face
//   card_frame                           = the `dark` tone (Cool Edit's box
//                                          outline and the label's shadow)
//   caption_active / _gradient / _text   = Title / Title End / Title Text
//   caption_inactive / _gradient / _text = Inactive Title / Inactive Title
//                                          End / Inactive Title Text, each
//                                          FOLLOWING its active key while
//                                          absent (GuiChromePick's accessors)
// A CHROME WITHOUT A GRADIENT would take the title's start alone; Windows
// 2000's caption paints both ends. THE CARDS AND THE TOOLTIP take the Face,
// the derived text and the dark outline (architect 2026-10-10), Windows'
// cream InfoWindow under black retired. Windows' Appearance-dialog rule that
// derived the 3D set from a picked ground retired the same day with the
// ground key (closed_questions.md); the catalog tool keeps its own port
// (tools/theme_catalog/toolkit_rules.py's windows_dialog) as the record of
// the schemes it imported.
namespace chrome_derive {

enum class Source {
    Face, Hilight, Mid, Dark, Black, Text, FieldText, Ink, InkText,
    TitleStart, TitleEnd, TitleText,
    InactiveTitleStart, InactiveTitleEnd, InactiveTitleText,
};
// Whether a source is the scheme's (the caption's six), else the palette's.
constexpr bool is_scheme_source(Source s) {
    switch (s) {
        case Source::TitleStart:
        case Source::TitleEnd:
        case Source::TitleText:
        case Source::InactiveTitleStart:
        case Source::InactiveTitleEnd:
        case Source::InactiveTitleText:
            return true;
        default:
            return false;
    }
}
struct ChromeRole {
    const char* role;
    Source      source;
};
inline constexpr ChromeRole kChromeRoles[] = {
    {"ground",                    Source::Face},
    {"label",                     Source::Text},
    {"hilight",                   Source::Hilight},
    {"light_3d",                  Source::Face},
    {"shadow",                    Source::Mid},
    {"dk_shadow",                 Source::Black},
    {"selected_fill",             Source::Ink},
    {"selected_text",             Source::InkText},
    {"field_ground",              Source::Mid},
    {"field_text",                Source::FieldText},
    {"clock_ground",              Source::Face},
    {"clock_text",                Source::Text},
    {"card_ground",               Source::Face},
    {"card_text",                 Source::Text},
    {"card_frame",                Source::Dark},
    {"caption_active",            Source::TitleStart},
    {"caption_active_gradient",   Source::TitleEnd},
    {"caption_active_text",       Source::TitleText},
    {"caption_inactive",          Source::InactiveTitleStart},
    {"caption_inactive_gradient", Source::InactiveTitleEnd},
    {"caption_inactive_text",     Source::InactiveTitleText},
};

// Each listed role's index in the theme's role table, found once.
inline constexpr auto kChromeRoleIndex = [] {
    std::array<std::size_t, std::size(kChromeRoles)> a{};
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] = theme_role_index(kChromeRoles[i].role);
    return a;
}();

// EVERY CHROME ROLE IS DECIDED ONCE: each role of the table is listed
// exactly once, each listed name is a role, and the scheme's sources are
// exactly the scheme keys' roles (kGuiChromeLines' `role`) — so a role
// added to the table is a build failure here until it is decided, and no
// palette-derived role can be a scheme key's.
constexpr bool every_chrome_role_decided() {
    for (const std::size_t i : kChromeRoleIndex)
        if (i >= kGuiThemeRoleCount) return false;
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i) {
        int seen = 0;
        for (const std::size_t k : kChromeRoleIndex)
            if (k == i) ++seen;
        if (seen != 1) return false;
    }
    for (std::size_t i = 0; i < std::size(kChromeRoles); ++i) {
        bool keyed = false;
        for (const GuiChromeLine& l : kGuiChromeLines)
            if (std::string_view(l.role) == kChromeRoles[i].role) keyed = true;
        if (keyed != is_scheme_source(kChromeRoles[i].source)) return false;
    }
    return true;
}
static_assert(every_chrome_role_decided());

// THE DERIVATION: every role off the program's live words (the palette's
// face and ink, through Cool Edit's tones) and the caption's six off
// `caption`.
constexpr GuiThemeWords derive_chrome(const GuiPaletteWords& program,
                                      const GuiChromePick& caption) {
    using namespace cool_edit_derive;
    constexpr std::size_t kFace = palette_role_index("face");
    constexpr std::size_t kInk = palette_role_index("waveform_ink");
    static_assert(kFace < kGuiPaletteRoleCount && kInk < kGuiPaletteRoleCount);
    const uint32_t face = program[kFace] & 0xFFFFFFu;
    const uint32_t ink = program[kInk] & 0xFFFFFFu;
    GuiThemeWords w{};
    for (std::size_t i = 0; i < std::size(kChromeRoles); ++i) {
        uint32_t v = 0;
        switch (kChromeRoles[i].source) {
            case Source::Face:       v = face;                                  break;
            case Source::Hilight:    v = tone_of(named_tone("hilight"), face, ink); break;
            case Source::Mid:        v = tone_of(named_tone("mid"), face, ink); break;
            case Source::Dark:       v = tone_of(named_tone("dark"), face, ink); break;
            case Source::Black:      v = 0x000000u;                             break;
            case Source::Text:       v = program_text(face);                    break;
            case Source::FieldText:  v = field_text(face);                      break;
            case Source::Ink:        v = ink;                                   break;
            case Source::InkText:
                v = tone_of(named_tone("selected_text"), face, ink);            break;
            case Source::TitleStart:         v = caption.title_start;          break;
            case Source::TitleEnd:           v = caption.title_end;            break;
            case Source::TitleText:          v = caption.title_text;           break;
            case Source::InactiveTitleStart: v = caption.inactive_start();     break;
            case Source::InactiveTitleEnd:   v = caption.inactive_end();       break;
            case Source::InactiveTitleText:  v = caption.inactive_text();      break;
        }
        w[kChromeRoleIndex[i]] = v & 0xFFFFFFu;
    }
    return w;
}

// -- THE CHECKS ----------------------------------------------------------------

// THE DEFAULT PALETTE (Cool Edit's "Default": Face 626C7B, ink 4BF3A7) under
// Windows 2000's caption: the chrome it paints, role by role.
inline constexpr GuiThemeWords kDefaultCheck =
    derive_chrome(kGuiBuiltinPalettes[0].words, kGuiCaptionWin2000);
static_assert(kDefaultCheck[theme_role_index("ground")] == 0x626C7B);
static_assert(kDefaultCheck[theme_role_index("light_3d")] == 0x626C7B);
static_assert(kDefaultCheck[theme_role_index("hilight")] == 0xAEB4BE);
static_assert(kDefaultCheck[theme_role_index("shadow")] == 0x404750);
static_assert(kDefaultCheck[theme_role_index("dk_shadow")] == 0x000000);
static_assert(kDefaultCheck[theme_role_index("label")] == 0xEFEFEF);
static_assert(kDefaultCheck[theme_role_index("field_ground")] == 0x404750);
static_assert(kDefaultCheck[theme_role_index("field_text")] == 0xEFEFEF);
static_assert(kDefaultCheck[theme_role_index("selected_fill")] == 0x4BF3A7);
static_assert(kDefaultCheck[theme_role_index("selected_text")] == 0x03301C);
static_assert(kDefaultCheck[theme_role_index("card_ground")] == 0x626C7B);
static_assert(kDefaultCheck[theme_role_index("card_text")] == 0xEFEFEF);
static_assert(kDefaultCheck[theme_role_index("card_frame")] == 0x2A2F35);
static_assert(kDefaultCheck[theme_role_index("caption_active")] == 0x0A246A);
static_assert(kDefaultCheck[theme_role_index("caption_inactive_text")] == 0xD4D0C8);
// A LIGHT FACE SWAPS THE TEXT AND NOT THE FIELD'S (Arctic Freeze's C0C0C0,
// METRICS_PRESETS_1010.md §3a): the label dark, the field text light.
static_assert([] {
    GuiPaletteWords p = kGuiBuiltinPalettes[0].words;
    p[palette_role_index("face")] = 0xC0C0C0;
    const GuiThemeWords w = derive_chrome(p, kGuiCaptionWin2000);
    return w[theme_role_index("label")] == 0x311919 &&
           w[theme_role_index("card_text")] == 0x311919 &&
           w[theme_role_index("field_text")] == 0xF6F6F6 &&
           w[theme_role_index("field_ground")] == 0x7F7F7F;
}());
// THE INACTIVE CAPTION FOLLOWS while unpicked.
static_assert([] {
    GuiChromePick p = kGuiCaptionWin2000;
    p.inactive_title_start.reset();
    p.inactive_title_text.reset();
    const GuiThemeWords w = derive_chrome(kGuiBuiltinPalettes[0].words, p);
    return w[theme_role_index("caption_inactive")] == 0x0A246A &&
           w[theme_role_index("caption_inactive_gradient")] == 0xC0C0C0 &&
           w[theme_role_index("caption_inactive_text")] == 0xFFFFFF;
}());

} // namespace chrome_derive

// THE CHROME'S LIVE WORDS — THE ONE RESOLVER the install family reads
// (install_palette / install_program_palette / install_chrome_pick,
// render.cpp): the derivation over the program's live words, the caption the
// live scheme's keys or, with none, the compiled caption (chrome_caption,
// theme_file.h).
inline GuiThemeWords live_chrome_words(const GuiPaletteWords& program,
                                       const std::optional<GuiChromePick>& pick) {
    return chrome_derive::derive_chrome(program, pick ? *pick : chrome_caption());
}
