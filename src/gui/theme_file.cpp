#include "theme_file.h"

#include "device_config.h"     // DeviceConfig (the default chrome)

#include <algorithm>
#include <array>
#include <cassert>
#include <iterator>
#include <string_view>

namespace {

// The role table names each role once, the name its member's own, so a
// lookup by name (theme_role_index) names one role each. AN
// EXHAUSTIVE PROOF IN N LOG N: every name measured once, the names sorted,
// no two neighbours equal (the form that held clang's default constexpr step
// budget on the NDK's compiler when the table carried 322 roles, 2026-10-07).
constexpr bool role_names_unique() {
    std::array<std::string_view, kGuiThemeRoleCount> names{};
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        names[i] = kGuiThemeRoles[i].name;
    std::ranges::sort(names);
    return std::ranges::adjacent_find(names) == names.end();
}
static_assert(role_names_unique());

// EVERY VOCABULARY HAS A COMPILED CAPTION, IN THE VOCABULARIES' ORDER, and
// every caption belongs to one vocabulary — the order the chromes' own
// schemes' assert (palette_file.cpp) walks the two tables in — so
// chrome_caption never misses.
constexpr bool captions_follow_the_vocabularies() {
    if (std::size(kGuiChromeSpecs) != std::size(kGuiChromeCaptions))
        return false;
    for (std::size_t i = 0; i < std::size(kGuiChromeCaptions); ++i)
        if (std::string_view(kGuiChromeSpecs[i]->key) !=
            kGuiChromeCaptions[i].chrome)
            return false;
    return true;
}
static_assert(captions_follow_the_vocabularies());
// The device config's default (both templates stamp a default-constructed
// struct's): the default chrome, kDefaultChromeKey.
static_assert(DeviceConfig{}.chrome == kDefaultChromeKey);

} // namespace

const GuiChromePick& chrome_caption(const ChromeSpec& spec) {
    for (const GuiChromeCaption& t : kGuiChromeCaptions)
        if (std::string_view(spec.key) == t.chrome) return *t.caption;
    // Every vocabulary has a caption (the assert above): a miss is a program
    // bug.
    assert(false);
    return kGuiCaptionWin2000;
}
