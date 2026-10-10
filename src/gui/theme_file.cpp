#include "theme_file.h"

#include <algorithm>
#include <array>
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

} // namespace
