#include "palette_file.h"

#include "theme_file.h"        // kGuiThemeRoles (the chrome's members)
#include "chrome_derive.h"     // derive_chrome: the keys' mapping check
#include "cool_edit_derive.h"  // the Cool Edit block's tones: the coverage check

#include <algorithm>
#include <array>
#include <cassert>
#include <iterator>
#include <string_view>

namespace {

// The role table names each role once, so the lookup (palette_role_index) is a
// bijection (eleven names: the pairwise form is cheap here).
constexpr bool palette_role_names_unique() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        for (std::size_t j = i + 1; j < kGuiPaletteRoleCount; ++j)
            if (std::string_view(kGuiPaletteRoles[i].name) ==
                kGuiPaletteRoles[j].name)
                return false;
    return true;
}
static_assert(palette_role_names_unique());
// The scheme's keys are no program role's name, so the two tables' lookups
// never answer for each other's names.
static_assert(std::ranges::none_of(kGuiChromeLines, [](const GuiChromeLine& c) {
    return palette_role_index(c.key) < kGuiPaletteRoleCount;
}));

// THE KEYS' TABLE IS WELL FORMED (palette_file.h's kGuiChromeLines): the keys
// distinct; each a block key (a word, no follow) or an inactive key (an
// optional, following an EARLIER block key); each role a chrome role; and
// the block is three, the inactive caption three.
constexpr bool chrome_lines_well_formed() {
    std::size_t block = 0;
    for (std::size_t i = 0; i < kGuiChromeLineCount; ++i) {
        const GuiChromeLine& l = kGuiChromeLines[i];
        for (std::size_t j = i + 1; j < kGuiChromeLineCount; ++j)
            if (std::string_view(l.key) == kGuiChromeLines[j].key) return false;
        if (theme_role_index(l.role) >= kGuiThemeRoleCount) return false;
        if ((l.word != nullptr) == (l.optional != nullptr)) return false;
        if (l.word != nullptr) {
            if (l.follows != kGuiChromeNoFollow) return false;
            ++block;
        } else if (l.follows >= i || !is_chrome_block_line(l.follows)) {
            return false;
        }
    }
    return block == 3 && kGuiChromeLineCount == 6;
}
static_assert(chrome_lines_well_formed());

// EACH KEY MOVES ITS OWN ROLE: for a pick whose six words are all distinct,
// each key's role in the derived chrome (chrome_derive::derive_chrome) holds
// exactly that key's word — the table's `role` column is what the
// derivation paints. Then an inactive key absent: its role holds the
// followed key's word.
constexpr GuiChromePick distinct_pick() {
    GuiChromePick p;
    for (std::size_t i = 0; i < kGuiChromeLineCount; ++i)
        set_chrome_line_word(p, i, 0x100000u + static_cast<uint32_t>(i));
    return p;
}
constexpr bool keys_map_onto_their_roles(const GuiChromePick& p) {
    const GuiThemeWords w =
        chrome_derive::derive_chrome(default_palette_words(), p);
    for (std::size_t i = 0; i < kGuiChromeLineCount; ++i)
        if (w[theme_role_index(kGuiChromeLines[i].role)] !=
            chrome_line_word(p, i))
            return false;
    return true;
}
static_assert(keys_map_onto_their_roles(distinct_pick()));
static_assert(keys_map_onto_their_roles([] {
    GuiChromePick p = distinct_pick();
    p.inactive_title_start.reset();
    p.inactive_title_end.reset();
    p.inactive_title_text.reset();
    return p;
}()));

// THE TWO TABLES COVER GuiPalette EXACTLY (install_palette fills it off
// both, render.cpp): the program's members are distinct, none of them is a
// chrome role's member, and the two counts sum to the struct's members — so
// no member is filled twice or left at construction state.
constexpr bool palette_members_disjoint() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i) {
        for (std::size_t j = i + 1; j < kGuiPaletteRoleCount; ++j)
            if (kGuiPaletteRoles[i].member == kGuiPaletteRoles[j].member)
                return false;
        for (const GuiThemeRole& t : kGuiThemeRoles)
            if (t.member == kGuiPaletteRoles[i].member) return false;
    }
    return true;
}
static_assert(palette_members_disjoint());
// THE COOL EDIT BLOCK'S DERIVED TONES (cool_edit_derive.h, filled with the
// program's words, render.cpp) are the struct's third part — the table's
// painted tones and the panel label's swapping pair: distinct, and neither a
// chrome role's member nor a program role's — and the button case's members
// (kCaseMemberCount, the ramp's array and four lines), the tab strip's
// body, inactive label and active label (kTabMemberCount, 2026-10-18) and
// the disabled cue's four faded colors (kDisabledCueMemberCount) after them.
constexpr bool cool_edit_tones_disjoint() {
    std::array<GuiColor GuiPalette::*,
               cool_edit_derive::kPaintedToneCount +
                   std::size(cool_edit_derive::kLabelPairMembers)>
        members{};
    std::size_t n = 0;
    for (const cool_edit_derive::Tone& t : cool_edit_derive::kTones)
        if (t.member != nullptr) members[n++] = t.member;
    for (GuiColor GuiPalette::* m : cool_edit_derive::kLabelPairMembers)
        members[n++] = m;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j)
            if (members[j] == members[i]) return false;
        for (const GuiThemeRole& t : kGuiThemeRoles)
            if (t.member == members[i]) return false;
        for (const GuiPaletteRole& p : kGuiPaletteRoles)
            if (p.member == members[i]) return false;
    }
    return true;
}
static_assert(cool_edit_tones_disjoint());
static_assert(sizeof(GuiPalette) ==
              (kGuiThemeRoleCount + kGuiPaletteRoleCount +
               cool_edit_derive::kPaintedToneCount +
               std::size(cool_edit_derive::kLabelPairMembers) +
               cool_edit_derive::kCaseMemberCount +
               cool_edit_derive::kTabMemberCount +
               cool_edit_derive::kDisabledCueMemberCount) *
                  sizeof(GuiColor));
// EVERY BUILT-IN PALETTE'S FACE HAS A ROW IN THE TAB TABLE (2026-10-18,
// cool_edit_derive.h's kTabBodies: the inactive tab's body and label are each
// preset's measured bytes, no rule), so the lookup the install runs
// (tab_body, fill_program_palette) can never miss and carries no error arm.
static_assert(std::ranges::all_of(kGuiBuiltinPalettes,
                                  [](const GuiBuiltinPalette& b) {
    const uint32_t face = b.words[palette_role_index("face")];
    return std::ranges::any_of(cool_edit_derive::kTabBodies,
                               [face](const cool_edit_derive::TabBody& t) {
                                   return t.face == face;
                               });
}));

// THE CHROME'S OWN SCHEME IS THE COMPILED CAPTION BY CONSTRUCTION
// (kGuiCaptionOwn reads the scheme's row of the built-in table, the
// product's own hand-written Default since 2026-10-21), so the scheme that
// carries no keys (scheme_record) is no loss of a word.

// WINDOWS ME STANDARD IS WINDOWS 2000 STANDARD'S SIX (the catalog's
// windows-me-standard, kept as an entry under Me's name; the generator
// asserts the same): Me's one difference from 2000 was its font, MS Sans
// Serif, which a scheme does not carry — the face is the product's
// one (architect 2026-10-09 ~21:20, gui_font.h).
static_assert([] {
    const GuiChromeScheme* me = builtin_scheme("windows-me-standard");
    const GuiChromeScheme* w2k = builtin_scheme("windows-2000-standard");
    if (me == nullptr || w2k == nullptr) return false;
    return me->chrome == w2k->chrome;
}());

// THE KEY GRAMMAR OF A BUILT-IN (the catalog's hyphen grammar, both kinds):
// one or more of lowercase ASCII letters, digits and '-' — a value the
// device config writes and reads verbatim.
constexpr bool is_preset_key_spelling(std::string_view v) {
    if (v.empty()) return false;
    for (const char c : v)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    return true;
}

// THE BUILT-INS' KEYS AND DISPLAY NAMES ARE UNIQUE (the generator checks the
// same over the catalog's), the keys in the key grammar, the chrome's own
// scheme — the product's Default — the table's first row and recording no
// inactive caption (it follows), and every catalog scheme recording its
// inactive caption (the struct's comment).
constexpr bool builtins_well_formed() {
    if (!is_chrome_own_scheme(kGuiChromeSchemes[0].key)) return false;
    for (std::size_t i = 0; i < std::size(kGuiChromeSchemes); ++i) {
        const GuiChromeScheme& a = kGuiChromeSchemes[i];
        if (!is_preset_key_spelling(a.key)) return false;
        const bool records_inactive = a.chrome.inactive_title_start &&
                                      a.chrome.inactive_title_end &&
                                      a.chrome.inactive_title_text;
        const bool records_none = !a.chrome.inactive_title_start &&
                                  !a.chrome.inactive_title_end &&
                                  !a.chrome.inactive_title_text;
        if (i == 0 ? !records_none : !records_inactive) return false;
        for (std::size_t j = i + 1; j < std::size(kGuiChromeSchemes); ++j)
            if (std::string_view(a.key) == kGuiChromeSchemes[j].key ||
                std::string_view(a.display_name) ==
                    kGuiChromeSchemes[j].display_name)
                return false;
    }
    return true;
}
static_assert(builtins_well_formed());

// THE BUILT-IN PALETTES ARE WELL FORMED (palette_file.h's
// kGuiBuiltinPalettes; the generator checks the same): the keys and display
// names unique, the keys in the key grammar, THE DEFAULT A ROW OF THE TABLE.
constexpr bool builtin_palettes_well_formed() {
    if (builtin_palette(kGuiDefaultPaletteKey) == nullptr) return false;
    for (std::size_t i = 0; i < std::size(kGuiBuiltinPalettes); ++i) {
        const GuiBuiltinPalette& a = kGuiBuiltinPalettes[i];
        if (!is_preset_key_spelling(a.key)) return false;
        for (std::size_t j = i + 1; j < std::size(kGuiBuiltinPalettes); ++j)
            if (std::string_view(a.key) == kGuiBuiltinPalettes[j].key ||
                std::string_view(a.display_name) ==
                    kGuiBuiltinPalettes[j].display_name)
                return false;
    }
    return true;
}
static_assert(builtin_palettes_well_formed());
// EVERY BUILT-IN'S LIT OUTLINE IS ITS INK AT THE VIEW BAR SPAN'S SHADOW
// LIGHTNESS (palette_file.h's role table: Cool Edit records no outline; the
// generator ran cool_edit_derive.h's round trip once, and the compiled bytes
// are the record — this proves the record, nothing derives at paint time).
static_assert(std::ranges::all_of(kGuiBuiltinPalettes,
                                  [](const GuiBuiltinPalette& b) {
    const cool_edit_derive::Tone& lo = cool_edit_derive::ink_tone("span_shadow");
    return b.words[palette_role_index("waveform_outline")] ==
           cool_edit_derive::tone(b.words[palette_role_index("waveform_ink")],
                                  lo.a, lo.b);
}));
// EVERY BUILT-IN'S INVALID COLOR IS ITS CENTER LINE (palette_file.h's role
// table, architect 2026-10-11; the generator copies Cool Edit's Cntr into
// both, and this proves the record).
static_assert(std::ranges::all_of(kGuiBuiltinPalettes,
                                  [](const GuiBuiltinPalette& b) {
    return b.words[palette_role_index("invalid_label")] ==
           b.words[palette_role_index("center")];
}));
// (THE CHOICE ROWS' ORDERS' PROOFS — each a permutation of its table, its
// first row the kind's default — went with the orders, 2026-10-14.)

} // namespace

GuiPaletteWords palette_record(std::string_view name) {
    const GuiBuiltinPalette* b = builtin_palette(name);
    // Every caller's name came through is_builtin_palette_name: a miss is a
    // program bug.
    assert(b != nullptr);
    return b->words;
}

std::optional<GuiChromePick> scheme_record(std::string_view name) {
    const GuiChromeScheme* b = builtin_scheme(name);
    // Every caller's name came through is_builtin_scheme_name: a miss is a
    // program bug.
    assert(b != nullptr);
    // Its six, unless it is the live chrome's own scheme, which carries none
    // (the declaration).
    if (name == live_chrome_spec().own_scheme) return std::nullopt;
    return b->chrome;
}
