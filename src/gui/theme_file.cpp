#include "theme_file.h"

#include "device_config.h"     // DeviceConfig (the default chrome)

#include <algorithm>
#include <array>
#include <cassert>
#include <iterator>
#include <string_view>

namespace {

// The role table names each role once, the name its member's own, so the
// generated Clearlooks values (keyed by name, kGuiThemeClearlooksValues) and
// the catalog's record name one role each. AN EXHAUSTIVE PROOF IN N LOG N:
// every name measured once, the names sorted, no two neighbours equal. (The
// pairwise form measured a name per comparison, N² strlens, and with the
// generated Clearlooks block's 286 roles in a table of 322 it ran past
// clang's default constexpr step budget, 1,048,576, on the NDK's compiler,
// 2026-10-07; this form costs between 100,000 and 200,000 steps at 322
// roles, growing as N log N, so the budget holds a table several times this
// one.)
constexpr bool role_names_unique() {
    std::array<std::string_view, kGuiThemeRoleCount> names{};
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        names[i] = kGuiThemeRoles[i].name;
    std::ranges::sort(names);
    return std::ranges::adjacent_find(names) == names.end();
}
static_assert(role_names_unique());

// CLEARLOOKS' GENERATED VALUES STAND IN THE TABLE'S ORDER, name by name
// (theme_file.h), so each word lands on its own role. A failure is a stale
// include: re-run tools/theme_catalog/gen_theme_files.py.
constexpr bool clearlooks_values_in_table_order() {
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        if (std::string_view(kGuiThemeClearlooksValues[i].name) !=
            kGuiThemeRoles[i].name)
            return false;
    return true;
}
static_assert(clearlooks_values_in_table_order());

// EVERY VOCABULARY HAS A COMPILED THEME, IN THE VOCABULARIES' ORDER, and
// every theme belongs to one vocabulary — the order the chromes' own
// schemes' assert (palette_file.cpp) walks the two tables in — so
// chrome_theme_words never misses.
constexpr bool themes_follow_the_vocabularies() {
    if (std::size(kGuiChromeSpecs) != std::size(kGuiChromeThemes))
        return false;
    for (std::size_t i = 0; i < std::size(kGuiChromeThemes); ++i)
        if (std::string_view(kGuiChromeSpecs[i]->key) !=
            kGuiChromeThemes[i].chrome)
            return false;
    return true;
}
static_assert(themes_follow_the_vocabularies());
// The device config's default (both templates stamp a default-constructed
// struct's): the default chrome, kDefaultChromeKey.
static_assert(DeviceConfig{}.chrome == kDefaultChromeKey);

} // namespace

const GuiThemeWords& chrome_theme_words(const ChromeSpec& spec) {
    for (const GuiChromeTheme& t : kGuiChromeThemes)
        if (std::string_view(spec.key) == t.chrome) return *t.words;
    // Every vocabulary has a theme (the assert above): a miss is a program
    // bug.
    assert(false);
    return kGuiThemeWin2000;
}
