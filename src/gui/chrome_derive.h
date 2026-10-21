#pragma once

#include "theme_file.h"        // GuiThemeWords, the role table, the compiled
                               // captions; through render.h, GuiChromePick
                               // and ChromeSpec
#include "palette_file.h"      // palette_role_index, the default palette's
                               // ink, the compiled caption
#include "cool_edit_derive.h"  // the tones the chrome wears, the one Face

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

// THE CHROME INHERITS COOL EDIT (architect 2026-10-10 ~10:20–11:30: "the
// chrome color should just be the Cool Edit theme color … we're getting rid
// of chrome inside the body. The caption remains the only thing that's
// outside of Cool Edit … even the font color, all of that would be inherited
// from Cool Edit"; "whatever luminous rules we previously had are superseded
// by the Cool Edit follow. We follow Cool Edit's theming"). This header is
// THE ONE OWNER OF EVERY CHROME ROLE'S SOURCE: the caption's six are THE
// COMPILED CAPTION'S (palette_file.h's kGuiCaptionOwn, Windows Storm's), and
// every other role is A TONE OF THE FACE — the panel's one Face, the tones
// Cool Edit derives from it, and for the selection fill the DEFAULT
// preset's waveform ink — by Cool Edit's own arithmetic (cool_edit_derive.h,
// whose head has the measurements and the proofs). NO PRESET MOVES THE
// CHROME since 2026-10-20 (architect: the waveform preset sets the
// waveform's colors alone; "as far as the Chrome, yes, that can go right
// away"): THE FACE IS THE METAL'S — the MetalDark tile's mean, measured at
// the tiles' load (materials.h, the wood chrome, the same day; the constant
// kDefaultFace that morning) — and the ink the default palette's, so the
// words are gui_chrome_words below over that Face, re-derived at every
// install (from 2026-10-10 until then they followed the live palette's Face
// and ink, the Settings Chrome row's commit moving the chrome live, and the
// caption's six were a picked scheme's). EVERY SURFACE ROLE IS A TONE, and
// a tone painted as a surface is its region's material (materials.h), so
// these words are the flat bytes the surface chokepoint knows each tone by;
// the text, the field text and the card text are ink, painted flat. THE
// CAPTION'S SIX ARE UNREAD since the caption became the wood (2026-10-20).
//
// THE PER-ROLE TABLE (kChromeRoles below; the role table's 21, each decided
// once — the build proves every role is listed exactly once, the caption's
// six, and only they, from the caption):
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
//   selected_fill                        = the waveform's ink (architect
//                                          2026-10-10) — the DEFAULT
//                                          preset's under every preset
//                                          since 2026-10-20
//   selected_text                        = the program text's OTHER member
//                                          (cool_edit_derive::selected_text:
//                                          dark where the Face keeps the
//                                          light text, light where it swaps;
//                                          architect 2026-10-16, "the
//                                          selection should be opposite of
//                                          the regular face")
//   card_ground                          = the Face
//   card_frame                           = the `dark` tone (Cool Edit's box
//                                          outline and the label's shadow)
//   caption_active / _gradient / _text   = Title / Title End / Title Text
//   caption_inactive / _gradient / _text = Inactive Title / Inactive Title
//                                          End / Inactive Title Text, each
//                                          FOLLOWING its active one while
//                                          absent (GuiChromePick's accessors;
//                                          the compiled caption records all
//                                          six)
// A CHROME WITHOUT A GRADIENT would take the title's start alone; Windows
// 2000's caption paints both ends. THE CARDS AND THE TOOLTIP take the Face,
// the derived text and the dark outline (architect 2026-10-10), Windows'
// cream InfoWindow under black retired. Windows' Appearance-dialog rule that
// derived the 3D set from a picked ground retired the same day with the
// ground key (closed_questions.md).
namespace chrome_derive {

enum class Source {
    Face, Hilight, Mid, Dark, Black, Text, FieldText, Ink, SelectedText,
    TitleStart, TitleEnd, TitleText,
    InactiveTitleStart, InactiveTitleEnd, InactiveTitleText,
};
// Whether a source is the caption's (its six), else the Face's or the ink's.
constexpr bool is_caption_source(Source s) {
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
    {"selected_text",             Source::SelectedText},
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
// exactly once, each listed name is a role, and the caption's sources are
// exactly the `caption_` roles — so a role added to the table is a build
// failure here until it is decided, and no Face-derived role can be the
// caption's.
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
        const bool caption_role =
            std::string_view(kChromeRoles[i].role).starts_with("caption_");
        if (caption_role != is_caption_source(kChromeRoles[i].source))
            return false;
    }
    return true;
}
static_assert(every_chrome_role_decided());

// THE DERIVATION: every role off `face` and `ink` (through Cool Edit's
// tones) and the caption's six off `caption`.
constexpr GuiThemeWords derive_chrome(uint32_t face, uint32_t ink,
                                      const GuiChromePick& caption) {
    using namespace cool_edit_derive;
    face &= 0xFFFFFFu;
    ink &= 0xFFFFFFu;
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
            case Source::SelectedText: v = selected_text(face);                 break;
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

// THE DEFAULT PRESET'S INK, the selection fill's source under every preset
// (the head).
inline constexpr uint32_t kDefaultInk =
    default_palette_words()[palette_role_index("waveform_ink")];
static_assert(palette_role_index("waveform_ink") < kGuiPaletteRoleCount);

// -- THE CHECKS ----------------------------------------------------------------

// THE DERIVATION ON COOL EDIT'S "(Default Scheme)" (Face 626C7B, ink 4BF3A7,
// under Windows Storm's caption), role by role — the chrome the product
// painted for the day of 2026-10-20 before the tiles, kept as the rule's
// proof.
inline constexpr GuiThemeWords kDefaultCheck =
    derive_chrome(cool_edit_derive::kDefaultFace, kDefaultInk, chrome_caption());
static_assert(kDefaultCheck[theme_role_index("ground")] == 0x626C7B);
static_assert(kDefaultCheck[theme_role_index("light_3d")] == 0x626C7B);
static_assert(kDefaultCheck[theme_role_index("hilight")] == 0xAEB4BE);
static_assert(kDefaultCheck[theme_role_index("shadow")] == 0x404750);
static_assert(kDefaultCheck[theme_role_index("dk_shadow")] == 0x000000);
static_assert(kDefaultCheck[theme_role_index("label")] == 0xEFEFEF);
static_assert(kDefaultCheck[theme_role_index("field_ground")] == 0x404750);
static_assert(kDefaultCheck[theme_role_index("field_text")] == 0xEFEFEF);
static_assert(kDefaultCheck[theme_role_index("selected_fill")] == 0x4BF3A7);
static_assert(kDefaultCheck[theme_role_index("selected_text")] == 0x0F1621);
static_assert(kDefaultCheck[theme_role_index("card_ground")] == 0x626C7B);
static_assert(kDefaultCheck[theme_role_index("card_text")] == 0xEFEFEF);
static_assert(kDefaultCheck[theme_role_index("card_frame")] == 0x2A2F35);
static_assert(kDefaultCheck[theme_role_index("caption_active")] == 0x800080);
static_assert(kDefaultCheck[theme_role_index("caption_inactive_text")] == 0xC0C0C0);
// A LIGHT FACE SWAPS THE TEXT AND NOT THE FIELD'S (Arctic Freeze's C0C0C0,
// METRICS_PRESETS_1010.md §3a): the label dark, the field text light — and
// the selected text the light member, the program text's other (2026-10-16).
static_assert([] {
    const GuiThemeWords w = derive_chrome(0xC0C0C0, kDefaultInk, chrome_caption());
    return w[theme_role_index("label")] == 0x311919 &&
           w[theme_role_index("card_text")] == 0x311919 &&
           w[theme_role_index("field_text")] == 0xF6F6F6 &&
           w[theme_role_index("field_ground")] == 0x7F7F7F &&
           w[theme_role_index("selected_text")] == 0xF6F6F6;
}());
// THE INACTIVE CAPTION FOLLOWS while unpicked.
static_assert([] {
    GuiChromePick p = chrome_caption();
    p.inactive_title_start.reset();
    p.inactive_title_text.reset();
    const GuiThemeWords w =
        derive_chrome(cool_edit_derive::kDefaultFace, kDefaultInk, p);
    return w[theme_role_index("caption_inactive")] == 0x800080 &&
           w[theme_role_index("caption_inactive_gradient")] == 0xA8ACA8 &&
           w[theme_role_index("caption_inactive_text")] == 0xFFFFFF;
}());

} // namespace chrome_derive

// THE CHROME'S WORDS — what install_palette reads (render.cpp): the
// derivation over THE METAL'S FACE, the MetalDark tile's mean (materials.h;
// architect 2026-10-20, the wood chrome: "the pull-downs, cards and dialogs
// read as metal"), and the default preset's ink, under the compiled caption
// (the head). (The one constant kGuiChromeWords over kDefaultFace stood for
// the day of 2026-10-20 before the tiles; kDefaultCheck above is its record.)
constexpr GuiThemeWords gui_chrome_words(uint32_t face) {
    return chrome_derive::derive_chrome(face, chrome_derive::kDefaultInk,
                                        chrome_caption());
}
