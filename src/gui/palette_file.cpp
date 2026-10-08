#include "palette_file.h"

#include "device_config.h"     // device_config_path, DeviceConfig (the default)
#include "settings_file.h"     // warptempo_settings::scan_key_value_file
#include "settings_io.h"       // atomic_write_string_to_path
#include "parse_text_util.h"   // warptempo_parse::prefix_line_error
#include "theme_file.h"        // theme_colour_word (THE ONE COLOR GRAMMAR),
                               // kGuiThemeRoles (the chrome's members)

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

// The role table names each role once, so the reader's lookup is a
// bijection (fifteen names: the pairwise form is cheap here).
constexpr bool palette_role_names_unique() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        for (std::size_t j = i + 1; j < kGuiPaletteRoleCount; ++j)
            if (std::string_view(kGuiPaletteRoles[i].name) ==
                kGuiPaletteRoles[j].name)
                return false;
    return true;
}
static_assert(palette_role_names_unique());

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
static_assert(sizeof(GuiPalette) ==
              (kGuiThemeRoleCount + kGuiPaletteRoleCount) * sizeof(GuiColor));

// EVERY VOCABULARY NAMES A DEFAULT PALETTE, IN THE DEFAULTS' ORDER, and every
// default belongs to one vocabulary (palette_names lists them in this order).
constexpr bool defaults_follow_the_vocabularies() {
    if (std::size(kGuiChromeSpecs) != std::size(kGuiDefaultPalettes))
        return false;
    for (std::size_t i = 0; i < std::size(kGuiDefaultPalettes); ++i)
        if (std::string_view(kGuiChromeSpecs[i]->default_palette) !=
            kGuiDefaultPalettes[i].name)
            return false;
    return true;
}
static_assert(defaults_follow_the_vocabularies());
static_assert(std::ranges::all_of(kGuiDefaultPalettes,
                                  [](const GuiDefaultPalette& d) {
    return is_palette_name_spelling(d.name);
}));
// The device config's default (both templates stamp a default-constructed
// struct's): no palette, the live chrome's own.
static_assert(DeviceConfig{}.palette.empty());

// A DEFAULT'S WORDS, its column of the role table.
constexpr GuiPaletteWords default_words(const GuiDefaultPalette& d) {
    GuiPaletteWords w{};
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        w[i] = kGuiPaletteRoles[i].*(d.column);
    return w;
}

const GuiDefaultPalette* default_palette_for(std::string_view name) {
    for (const GuiDefaultPalette& d : kGuiDefaultPalettes)
        if (name == d.name) return &d;
    return nullptr;
}

// THE PALETTES READ AT LAUNCH AND MAINTAINED BY THE PICKER'S WRITES, by
// name, each its fifteen words (a file names every role, palette_file.h's
// head) — read by is_palette_name, palette_words and palette_names.
// Single-threaded: the read precedes every reader, and only the GUI thread
// reads or writes it.
std::map<std::string, GuiPaletteWords, std::less<>> g_loaded_palettes;

constexpr std::string_view kPaletteSuffix = ".palette";

std::filesystem::path palette_file_path(const std::filesystem::path& folder,
                                        std::string_view name) {
    return folder / (std::string(name) + std::string(kPaletteSuffix));
}

// ONE FILE under the grammar (palette_file.h's head), its stem already
// judged.
std::expected<GuiPaletteWords, std::string> read_palette_file(
        const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected(std::string("could not open the file"));
    GuiPaletteWords                        out{};
    std::array<bool, kGuiPaletteRoleCount> named{};
    auto scan = warptempo_settings::scan_key_value_file(
        f, [&out, &named](int ln, const std::string& role,
                          const std::string& value)
                  -> std::expected<void, std::string> {
        const std::size_t i = palette_role_index(role);
        if (i == kGuiPaletteRoleCount) {
            return warptempo_parse::prefix_line_error(
                ln, "unknown role '" + role + "'");
        }
        const std::optional<uint32_t> w = theme_colour_word(value);
        if (!w) {
            return warptempo_parse::prefix_line_error(
                ln, "role '" + role + "' has invalid value '" + value +
                    "': must be #rrggbb or one of the twenty Windows color "
                    "names");
        }
        out[i]   = *w;
        named[i] = true;
        return {};
    }, std::span<const char* const>{});
    if (!scan) return std::unexpected(std::move(scan.error()));
    // EVERY ROLE IS REQUIRED (the head: the picker writes all fifteen), the
    // first missing one in the table's order named — a file from before
    // `flag_outline` became a role fails on it.
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i) {
        if (!named[i]) {
            return std::unexpected("missing role '" +
                                   std::string(kGuiPaletteRoles[i].name) +
                                   "'");
        }
    }
    return out;
}

// The text write_palette_file puts down: every role, uppercase #RRGGBB, LF.
std::string palette_file_text(const GuiPaletteWords& words) {
    std::string s;
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "#%06X",
                      static_cast<unsigned>(words[i] & 0xFFFFFFu));
        s += kGuiPaletteRoles[i].name;
        s += '=';
        s += hex;
        s += '\n';
    }
    return s;
}

} // namespace

std::string_view effective_palette_name(std::string_view palette) {
    return palette.empty()
               ? std::string_view(live_chrome_spec().default_palette)
               : palette;
}

std::filesystem::path palette_folder_path() {
    const std::filesystem::path cfg = device_config_path();
    if (cfg.empty()) return {};
    return cfg.parent_path() / "palettes";
}

std::optional<std::string> read_palette_folder() {
    const std::filesystem::path folder = palette_folder_path();
    if (folder.empty()) return std::nullopt;
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) {
        // A MISSING FOLDER IS NO FILES; a failed query is the next call's
        // failure, said with the system's words.
        if (!ec) return std::nullopt;
    }
    // THE NAMES FIRST, SORTED, so the first error is the same file on every
    // launch.
    std::vector<std::filesystem::path> files;
    std::filesystem::directory_iterator it(folder, ec);
    if (ec) {
        return "could not read the palettes folder '" + folder.string() +
               "': " + ec.message();
    }
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name.size() <= kPaletteSuffix.size() ||
            !name.ends_with(kPaletteSuffix))
            continue;
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        files.push_back(it->path());
    }
    if (ec) {
        return "could not read the palettes folder '" + folder.string() +
               "': " + ec.message();
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& p : files) {
        const std::string file = p.filename().string();
        const std::string name =
            file.substr(0, file.size() - kPaletteSuffix.size());
        const std::string head =
            "invalid palette file '" + p.string() + "': ";
        if (!is_palette_name_spelling(name)) {
            return head + "the name must be <name>.palette, the name 1 to 40 "
                          "printable ASCII characters with no leading or "
                          "trailing space";
        }
        if (is_default_palette_name(name)) {
            return head + name + " is a default palette and takes no file";
        }
        auto words = read_palette_file(p);
        if (!words) return head + words.error();
        g_loaded_palettes.emplace(name, *words);
    }
    return std::nullopt;
}

std::vector<std::string> palette_names() {
    std::vector<std::string> out;
    out.reserve(std::size(kGuiDefaultPalettes) + g_loaded_palettes.size());
    for (const GuiDefaultPalette& d : kGuiDefaultPalettes)
        out.emplace_back(d.name);
    for (const auto& [name, words] : g_loaded_palettes) out.push_back(name);
    return out;
}

bool is_palette_name(std::string_view name) {
    return is_default_palette_name(name) || g_loaded_palettes.contains(name);
}

GuiPaletteWords palette_words(std::string_view name) {
    if (const GuiDefaultPalette* d = default_palette_for(name))
        return default_words(*d);
    const auto it = g_loaded_palettes.find(name);
    // Every caller's name came through is_palette_name: a miss is a program
    // bug.
    assert(it != g_loaded_palettes.end());
    return it->second;
}

std::optional<std::string> write_palette_file(std::string_view name,
                                              const GuiPaletteWords& words) {
    // The names are the picker's to judge (the declaration).
    assert(is_palette_name_spelling(name));
    assert(!is_default_palette_name(name));
    const std::filesystem::path folder = palette_folder_path();
    // The launch read the config through the same resolver, and the
    // environment does not change under the process.
    assert(!folder.empty());
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return "could not create the palettes folder '" + folder.string() +
               "': " + ec.message();
    }
    const std::filesystem::path p = palette_file_path(folder, name);
    if (!atomic_write_string_to_path(p.string(), palette_file_text(words)))
        return "could not write the palette file '" + p.string() + "'";
    g_loaded_palettes.insert_or_assign(std::string(name), words);
    return std::nullopt;
}

std::optional<std::string> rename_palette_file(std::string_view old_name,
                                               std::string_view new_name) {
    // The names are the picker's to judge (the declaration).
    const auto it = g_loaded_palettes.find(old_name);
    assert(it != g_loaded_palettes.end());
    assert(new_name != old_name);
    assert(is_palette_name_spelling(new_name));
    assert(!is_palette_name(new_name));   // neither a default's nor taken
    const std::filesystem::path folder = palette_folder_path();
    assert(!folder.empty());
    const std::filesystem::path from = palette_file_path(folder, old_name);
    const std::filesystem::path to   = palette_file_path(folder, new_name);
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) {
        return "could not rename the palette file '" + from.string() +
               "': " + ec.message();
    }
    const GuiPaletteWords words = it->second;
    g_loaded_palettes.erase(it);
    g_loaded_palettes.emplace(std::string(new_name), words);
    return std::nullopt;
}

std::optional<std::string> remove_palette_file(std::string_view name) {
    // The name is the picker's to judge (the declaration): a loaded file's,
    // never a default's (which the map does not hold).
    const auto it = g_loaded_palettes.find(name);
    assert(it != g_loaded_palettes.end());
    const std::filesystem::path folder = palette_folder_path();
    assert(!folder.empty());
    const std::filesystem::path p = palette_file_path(folder, name);
    std::error_code ec;
    std::filesystem::remove(p, ec);
    if (ec) {
        return "could not delete the palette file '" + p.string() + "': " +
               ec.message();
    }
    g_loaded_palettes.erase(it);
    return std::nullopt;
}
