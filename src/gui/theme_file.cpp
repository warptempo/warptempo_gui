#include "theme_file.h"

#include "device_config.h"     // device_config_path, DeviceConfig (the default)
#include "platform.h"          // GuiPlatform::bundled_theme_files
#include "settings_file.h"     // warptempo_settings::scan_key_value_file
#include "settings_io.h"       // atomic_write_string_to_path
#include "parse_text_util.h"   // warptempo_parse::prefix_line_error

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <expected>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

// THE BUILT-IN'S WORDS, the role table's third column.
constexpr GuiThemeWords builtin_words() {
    GuiThemeWords w{};
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        w[i] = kGuiThemeRoles[i].builtin;
    return w;
}
constexpr GuiThemeWords kBuiltinWords = builtin_words();

// The role table names each role once, so the reader's lookup is a bijection.
// AN EXHAUSTIVE PROOF IN N LOG N: every name measured once, the names sorted,
// no two neighbours equal. (The pairwise form — theme_role_index asked of
// every name — measured a name per comparison, N² strlens, and with the
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
// Every flat-caption pair names two roles of the table.
static_assert(std::ranges::all_of(kGuiThemeCaptionGradients,
                                  [](const GuiThemeGradientPair& p) {
    return p.start < kGuiThemeRoleCount && p.end < kGuiThemeRoleCount;
}));
static_assert(is_theme_key_spelling(kBuiltinThemeKey));
// Every chrome's own theme is a theme key, and win2000's is the built-in;
// clearlooks' (the default chrome's since 2026-10-07) is a bundled file,
// copied in at every launch, so an unset theme under either chrome always
// resolves.
static_assert(chrome_specs_all([](const ChromeSpec& c) {
    return is_theme_key_spelling(c.default_theme);
}));
static_assert(std::string_view(kChromeSpecWin2000.default_theme) ==
              kBuiltinThemeKey);
// The device config's defaults (both templates stamp a default-constructed
// struct's): the default chrome, kDefaultChromeKey, and no theme.
static_assert(DeviceConfig{}.chrome == kDefaultChromeKey);
static_assert(DeviceConfig{}.theme.empty());

// THE THEMES READ AT LAUNCH, by key — written once by read_theme_folder, read
// by is_theme_key and theme_words for the process's life. Single-threaded:
// the read precedes every reader, and only the GUI thread reads it.
std::map<std::string, GuiThemeWords, std::less<>> g_loaded_themes;

constexpr std::string_view kThemeSuffix = ".theme";

// ONE FILE under the grammar (theme_file.h's head), its stem already judged:
// the built-in's words, each role the file names overwritten, then THE FLAT
// CAPTION's rule over what it named.
std::expected<GuiThemeWords, std::string> read_theme_file(
        const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected(std::string("could not open the file"));
    GuiThemeWords words = kBuiltinWords;
    std::array<bool, kGuiThemeRoleCount> named{};
    auto scan = warptempo_settings::scan_key_value_file(
        f, [&words, &named](int ln, const std::string& role,
                            const std::string& value)
                  -> std::expected<void, std::string> {
        const std::size_t i = theme_role_index(role);
        if (i == kGuiThemeRoleCount) {
            return warptempo_parse::prefix_line_error(
                ln, "unknown role '" + role + "'");
        }
        const std::optional<uint32_t> w = theme_colour_word(value);
        if (!w) {
            return warptempo_parse::prefix_line_error(
                ln, "role '" + role + "' has invalid value '" + value +
                    "': must be #rrggbb or one of the twenty Windows colour "
                    "names");
        }
        words[i] = *w;
        named[i] = true;
        return {};
        // NO ROLE IS REQUIRED: a file may name only some (the head).
    }, std::span<const char* const>{});
    if (!scan) return std::unexpected(std::move(scan.error()));
    // A START WITHOUT ITS END IS A FLAT CAPTION (the head's rule).
    for (const GuiThemeGradientPair& p : kGuiThemeCaptionGradients)
        if (named[p.start] && !named[p.end]) words[p.end] = words[p.start];
    return words;
}

} // namespace

std::filesystem::path theme_folder_path() {
    const std::filesystem::path cfg = device_config_path();
    if (cfg.empty()) return {};
    return cfg.parent_path() / "themes";
}

std::optional<std::string> copy_in_bundled_themes() {
    const std::filesystem::path folder = theme_folder_path();
    // No config home: nothing to write into, and the config's own load
    // refuses with its own line (read_theme_folder reads nothing either).
    if (folder.empty()) return std::nullopt;
    auto bundle = GuiPlatform::bundled_theme_files();
    if (!bundle) {
        return "could not read the bundled theme files: " + bundle.error();
    }
    // AN EMPTY BUNDLE IS A BUILD DEFECT (the generator writes every catalog
    // theme but the built-in, and both carriers take the whole folder), said
    // here rather than launching without the themes a config may name.
    if (bundle->empty()) return std::string("the bundle holds no theme files");
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return "could not create the themes folder '" + folder.string() +
               "': " + ec.message();
    }
    // NO LAUNCH-TIME CLEANUP OF A FORMER BUNDLE'S FILES (architect 2026-10-07
    // evening, closed_questions.md): the copy-in only writes, the bundle
    // winning for its own names, and never deletes a file a bundle stopped
    // carrying or one bearing the built-in's name — such a file is the
    // read's ordinary first-error refusal below, its name in the line. Both
    // installs are clean of every such file (each ran fd06c381, 2026-10-07,
    // whose copy-in still deleted them), and a later retirement is cleaned
    // by hand at the install, the planner's job.
    for (const auto& [name, bytes] : *bundle) {
        const std::filesystem::path p = folder / name;
        // A file already holding the bundle's bytes is left as it is — the
        // folder ends the same as if it were written — so a launch rewrites
        // (and syncs) only what a new build or a hand edit changed.
        {
            std::ifstream have(p, std::ios::binary);
            if (have.is_open() &&
                std::string{std::istreambuf_iterator<char>(have),
                            std::istreambuf_iterator<char>()} == bytes)
                continue;
        }
        if (!atomic_write_string_to_path(p.string(), bytes)) {
            return "could not write the bundled theme file '" + p.string() +
                   "'";
        }
    }
    return std::nullopt;
}

std::optional<std::string> read_theme_folder() {
    const std::filesystem::path folder = theme_folder_path();
    if (folder.empty()) return std::nullopt;
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) {
        // A MISSING FOLDER IS NO FILES; a failed query is the next call's
        // failure, said with the system's words.
        if (!ec) return std::nullopt;
    }
    // THE NAMES FIRST, SORTED, so the first error is the same file on every
    // launch (a directory's own order is the filesystem's business).
    std::vector<std::filesystem::path> files;
    std::filesystem::directory_iterator it(folder, ec);
    if (ec) {
        return "could not read the themes folder '" + folder.string() +
               "': " + ec.message();
    }
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name.size() <= kThemeSuffix.size() ||
            !name.ends_with(kThemeSuffix))
            continue;
        std::error_code tec;
        if (!it->is_regular_file(tec)) continue;
        files.push_back(it->path());
    }
    if (ec) {
        return "could not read the themes folder '" + folder.string() +
               "': " + ec.message();
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& p : files) {
        const std::string name = p.filename().string();
        const std::string key =
            name.substr(0, name.size() - kThemeSuffix.size());
        const std::string head = "invalid theme file '" + p.string() + "': ";
        if (!is_theme_key_spelling(key)) {
            return head + "the name must be <key>.theme, the key lowercase "
                          "letters and digits in runs joined by single "
                          "hyphens";
        }
        if (key == kBuiltinThemeKey) {
            return head + kBuiltinThemeKey +
                   " is the built-in theme and takes no file";
        }
        auto words = read_theme_file(p);
        if (!words) return head + words.error();
        g_loaded_themes.emplace(key, *words);
    }
    return std::nullopt;
}

std::string_view effective_theme_key(std::string_view theme) {
    return theme.empty() ? std::string_view(live_chrome_spec().default_theme)
                         : theme;
}

bool is_theme_key(const std::string& v) {
    return v == kBuiltinThemeKey || g_loaded_themes.contains(v);
}

const GuiThemeWords& theme_words(std::string_view key) {
    if (key == kBuiltinThemeKey) return kBuiltinWords;
    const auto it = g_loaded_themes.find(key);
    // Every caller's key came through is_theme_key: a miss is a program bug.
    assert(it != g_loaded_themes.end());
    return it->second;
}
