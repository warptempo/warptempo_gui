#include "palette_file.h"

#include "device_config.h"     // device_config_path, DeviceConfig (the default)
#include "settings_file.h"     // warptempo_settings::scan_key_value_file
#include "settings_io.h"       // atomic_write_string_to_path
#include "parse_text_util.h"   // warptempo_parse::prefix_line_error
#include "theme_file.h"        // theme_colour_word (THE ONE COLOR GRAMMAR),
                               // kGuiThemeRoles (the chrome's members)
#include "chrome_derive.h"     // derive_windows_chrome: the keys' mapping check

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
// The chrome knob's twelve keys are no program role's name, so the reader's
// two lookups cannot both claim a line.
static_assert(std::ranges::none_of(kGuiChromeLines, [](const GuiChromeLine& c) {
    return palette_role_index(c.key) < kGuiPaletteRoleCount;
}));

// THE KEYS' TABLE IS WELL FORMED (palette_file.h's kGuiChromeLines): the keys
// distinct; each a block key (a word, no follow) or an inactive key (an
// optional, following an EARLIER block key); each compiled role a chrome
// role; and the block is nine, the inactive caption three.
constexpr bool chrome_lines_well_formed() {
    std::size_t block = 0;
    for (std::size_t i = 0; i < kGuiChromeLineCount; ++i) {
        const GuiChromeLine& l = kGuiChromeLines[i];
        for (std::size_t j = i + 1; j < kGuiChromeLineCount; ++j)
            if (std::string_view(l.key) == kGuiChromeLines[j].key) return false;
        if (theme_role_index(l.compiled_role) >= kGuiThemeRoleCount) return false;
        if ((l.word != nullptr) == (l.optional != nullptr)) return false;
        if (l.word != nullptr) {
            if (l.follows != kGuiChromeNoFollow) return false;
            ++block;
        } else if (l.follows >= i || !is_chrome_block_line(l.follows)) {
            return false;
        }
    }
    return block == 9 && kGuiChromeLineCount == 12;
}
static_assert(chrome_lines_well_formed());

// THE PICKER SHOWS WHAT THE DERIVATION PAINTS: for a pick whose twelve words
// are all distinct, each key's compiled role in the derived chrome
// (chrome_derive::derive_windows_chrome) holds exactly that key's word — so
// the role a key's OLD reads while no block stands (compiled_role) is the
// role the key moves once one does. Then an inactive key absent: its role
// holds the followed key's word.
constexpr GuiChromePick distinct_pick() {
    GuiChromePick p;
    for (std::size_t i = 0; i < kGuiChromeLineCount; ++i)
        set_chrome_line_word(p, i, 0x100000u + static_cast<uint32_t>(i));
    return p;
}
constexpr bool keys_map_onto_their_roles(const GuiChromePick& p) {
    const GuiThemeWords w =
        chrome_derive::derive_windows_chrome(kGuiThemeWin2000, p);
    for (std::size_t i = 0; i < kGuiChromeLineCount; ++i)
        if (w[theme_role_index(kGuiChromeLines[i].compiled_role)] !=
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
// The same under clearlooks (clearlooks_derive.h), but for the two title
// ends, which that chrome ignores: their roles hold the caption's START
// (the flat caption), so an ignored key moves nothing.
constexpr bool keys_map_onto_their_roles_clearlooks(const GuiChromePick& p) {
    const GuiThemeWords w = clearlooks_derive::derive_clearlooks_chrome(
        kGuiThemeClearlooks, p, clearlooks_derive::kGeometry);
    for (std::size_t i = 0; i < kGuiChromeLineCount; ++i) {
        const std::size_t r = theme_role_index(kGuiChromeLines[i].compiled_role);
        const std::string_view key = kGuiChromeLines[i].key;
        if (key == "chrome_title_end") {
            if (w[r] != p.title_start) return false;
        } else if (key == "chrome_inactive_title_end") {
            if (w[r] != p.inactive_start()) return false;
        } else if (w[r] != chrome_line_word(p, i)) {
            return false;
        }
    }
    return true;
}
static_assert(keys_map_onto_their_roles_clearlooks(distinct_pick()));
// THE CHROME'S OWN SCHEME IS THE PROOF'S INPUT: clearlooks' built-in
// scheme carries squeeze's own keys (clearlooks_derive.h's kSqueezePick,
// whose derivation is the compiled theme byte for byte).
static_assert([] {
    const GuiChromeScheme* s = builtin_scheme("clearlooks");
    return s != nullptr && s->chrome == clearlooks_derive::kSqueezePick;
}());

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
// default belongs to one vocabulary (the palette menu lists them in this
// order, color_picker::palette_menu_rows).
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

// EACH DEFAULT IS ITS CHROME'S OWN BUILT-IN SCHEME, AND THAT SCHEME IS THE
// CHROME'S COMPILED THEME KEY FOR KEY (the generator's transcription and the
// hand-recorded / generated themes agree): so the scheme that carries no
// block under its own chrome (palette_record) is no loss of a word.
constexpr bool defaults_are_their_chromes_schemes() {
    for (std::size_t c = 0; c < std::size(kGuiChromeThemes); ++c) {
        const GuiChromeScheme* b = builtin_scheme(kGuiDefaultPalettes[c].name);
        if (b == nullptr) return false;
        if (std::string_view(kGuiChromeThemes[c].chrome) !=
            kGuiChromeSpecs[c]->key)
            return false;
        const GuiThemeWords& w = *kGuiChromeThemes[c].words;
        for (std::size_t i = 0; i < kGuiChromeLineCount; ++i)
            if (w[theme_role_index(kGuiChromeLines[i].compiled_role)] !=
                chrome_line_word(b->chrome, i))
                return false;
    }
    return true;
}
static_assert(defaults_are_their_chromes_schemes());

// THE BUILT-INS' KEYS AND DISPLAY NAMES ARE UNIQUE (the generator checks the
// same), the keys in the name grammar, and every scheme records its inactive
// caption (the struct's comment).
constexpr bool builtins_well_formed() {
    for (std::size_t i = 0; i < std::size(kGuiChromeSchemes); ++i) {
        const GuiChromeScheme& a = kGuiChromeSchemes[i];
        if (!is_palette_name_spelling(a.key)) return false;
        if (!a.chrome.inactive_title_start || !a.chrome.inactive_title_end ||
            !a.chrome.inactive_title_text)
            return false;
        for (std::size_t j = i + 1; j < std::size(kGuiChromeSchemes); ++j)
            if (std::string_view(a.key) == kGuiChromeSchemes[j].key ||
                std::string_view(a.display_name) ==
                    kGuiChromeSchemes[j].display_name)
                return false;
    }
    return true;
}
static_assert(builtins_well_formed());
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
// head) and its chrome knob when it carries one — read by is_palette_name,
// palette_record and palette_file_names. Single-threaded: the read precedes
// every reader, and only the GUI thread reads or writes it.
std::map<std::string, GuiPaletteRecord, std::less<>> g_loaded_palettes;

constexpr std::string_view kPaletteSuffix = ".palette";

std::filesystem::path palette_file_path(const std::filesystem::path& folder,
                                        std::string_view name) {
    return folder / (std::string(name) + std::string(kPaletteSuffix));
}

// ONE FILE under the grammar (palette_file.h's head), its stem already
// judged.
std::expected<GuiPaletteRecord, std::string> read_palette_file(
        const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected(std::string("could not open the file"));
    GuiPaletteWords                        out{};
    std::array<bool, kGuiPaletteRoleCount> named{};
    GuiChromePick                          chrome{};
    std::array<bool, kGuiChromeLineCount>  chrome_named{};
    auto scan = warptempo_settings::scan_key_value_file(
        f, [&](int ln, const std::string& role, const std::string& value)
                  -> std::expected<void, std::string> {
        const std::size_t i = palette_role_index(role);
        std::size_t c = kGuiChromeLineCount;
        for (std::size_t k = 0; k < kGuiChromeLineCount; ++k)
            if (role == kGuiChromeLines[k].key) c = k;
        if (i == kGuiPaletteRoleCount && c == kGuiChromeLineCount) {
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
        if (c < kGuiChromeLineCount) {
            set_chrome_line_word(chrome, c, *w);
            chrome_named[c] = true;
            return {};
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
    // THE CHROME BLOCK COMES WHOLE (the head): any chrome key — a block key
    // or an inactive one — requires all nine of the block, the first missing
    // in the table's order named; the inactive keys stand as read (each
    // optional, the unread ones following the active caption).
    GuiPaletteRecord record{out, std::nullopt};
    if (std::ranges::any_of(chrome_named, [](bool b) { return b; })) {
        for (std::size_t k = 0; k < kGuiChromeLineCount; ++k) {
            if (is_chrome_block_line(k) && !chrome_named[k]) {
                return std::unexpected("missing role '" +
                                       std::string(kGuiChromeLines[k].key) +
                                       "'");
            }
        }
        record.chrome = chrome;
    }
    return record;
}

// The text write_palette_file puts down: the chrome keys when the record
// carries the block (the nine and each picked inactive key, in the table's
// order), then every role, uppercase #RRGGBB, LF.
std::string palette_file_text(const GuiPaletteRecord& record) {
    std::string s;
    const auto line = [&s](const char* role, uint32_t word) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "#%06X",
                      static_cast<unsigned>(word & 0xFFFFFFu));
        s += role;
        s += '=';
        s += hex;
        s += '\n';
    };
    if (record.chrome) {
        for (std::size_t k = 0; k < kGuiChromeLineCount; ++k) {
            const GuiChromeLine& l = kGuiChromeLines[k];
            if (l.word != nullptr) line(l.key, (*record.chrome).*(l.word));
            else if (const std::optional<uint32_t>& v =
                         (*record.chrome).*(l.optional))
                line(l.key, *v);
        }
    }
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        line(kGuiPaletteRoles[i].name, record.words[i]);
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
        if (is_builtin_palette_name(name)) {
            return head + name + " is a built-in palette and takes no file";
        }
        auto record = read_palette_file(p);
        if (!record) return head + record.error();
        g_loaded_palettes.emplace(name, *record);
    }
    return std::nullopt;
}

std::vector<std::string> palette_file_names() {
    std::vector<std::string> out;
    out.reserve(g_loaded_palettes.size());
    for (const auto& [name, record] : g_loaded_palettes) out.push_back(name);
    return out;
}

bool is_palette_name(std::string_view name) {
    return is_builtin_palette_name(name) || g_loaded_palettes.contains(name);
}

GuiPaletteRecord palette_record(std::string_view name) {
    // A BUILT-IN (the declaration): the live chrome's default fifteen, and
    // the scheme's keys unless it is the live chrome's own.
    if (const GuiChromeScheme* b = builtin_scheme(name)) {
        const std::string_view own = live_chrome_spec().default_palette;
        const GuiDefaultPalette* d = default_palette_for(own);
        assert(d != nullptr);   // defaults_follow_the_vocabularies
        GuiPaletteRecord r{default_words(*d), std::nullopt};
        if (name != own) r.chrome = b->chrome;
        return r;
    }
    const auto it = g_loaded_palettes.find(name);
    // Every caller's name came through is_palette_name: a miss is a program
    // bug.
    assert(it != g_loaded_palettes.end());
    return it->second;
}

std::optional<std::string> write_palette_file(std::string_view name,
                                              const GuiPaletteRecord& record) {
    // The names are the picker's to judge (the declaration).
    assert(is_palette_name_spelling(name));
    assert(!is_builtin_palette_name(name));
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
    if (!atomic_write_string_to_path(p.string(), palette_file_text(record)))
        return "could not write the palette file '" + p.string() + "'";
    g_loaded_palettes.insert_or_assign(std::string(name), record);
    return std::nullopt;
}

std::optional<std::string> rename_palette_file(std::string_view old_name,
                                               std::string_view new_name) {
    // The names are the picker's to judge (the declaration).
    const auto it = g_loaded_palettes.find(old_name);
    assert(it != g_loaded_palettes.end());
    assert(new_name != old_name);
    assert(is_palette_name_spelling(new_name));
    assert(!is_palette_name(new_name));   // neither a built-in's nor taken
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
    const GuiPaletteRecord record = it->second;
    g_loaded_palettes.erase(it);
    g_loaded_palettes.emplace(std::string(new_name), record);
    return std::nullopt;
}

std::optional<std::string> remove_palette_file(std::string_view name) {
    // The name is the picker's to judge (the declaration): a loaded file's,
    // never a built-in's (which the map does not hold).
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
