#include "palette_file.h"

#include "device_config.h"     // device_config_path, DeviceConfig (the defaults)
#include "settings_file.h"     // warptempo_settings::scan_key_value_file
#include "settings_io.h"       // atomic_write_string_to_path
#include "parse_text_util.h"   // warptempo_parse::prefix_line_error
#include "theme_file.h"        // theme_colour_word (THE ONE COLOR GRAMMAR),
                               // kGuiThemeRoles (the chrome's members)
#include "chrome_derive.h"     // derive_windows_chrome: the keys' mapping check
#include "cool_edit_derive.h"  // the Cool Edit block's tones: the coverage check

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
// bijection (ten names: the pairwise form is cheap here).
constexpr bool palette_role_names_unique() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        for (std::size_t j = i + 1; j < kGuiPaletteRoleCount; ++j)
            if (std::string_view(kGuiPaletteRoles[i].name) ==
                kGuiPaletteRoles[j].name)
                return false;
    return true;
}
static_assert(palette_role_names_unique());
// The scheme's twelve keys are no program role's name, so each kind's reader
// refuses the other kind's key as an unknown role (palette_file.h's head:
// a palette file carrying a chrome key is no longer the GUI's).
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
// THE COOL EDIT BLOCK'S DERIVED TONES (cool_edit_derive.h, filled with the
// program's words, render.cpp) are the struct's third part: distinct, and
// neither a chrome role's member nor a program role's.
constexpr bool cool_edit_tones_disjoint() {
    const auto& tones = cool_edit_derive::kTones;
    for (std::size_t i = 0; i < std::size(tones); ++i) {
        if (tones[i].member == nullptr) continue;
        for (std::size_t j = i + 1; j < std::size(tones); ++j)
            if (tones[j].member == tones[i].member) return false;
        for (const GuiThemeRole& t : kGuiThemeRoles)
            if (t.member == tones[i].member) return false;
        for (const GuiPaletteRole& p : kGuiPaletteRoles)
            if (p.member == tones[i].member) return false;
    }
    return true;
}
static_assert(cool_edit_tones_disjoint());
static_assert(sizeof(GuiPalette) ==
              (kGuiThemeRoleCount + kGuiPaletteRoleCount +
               cool_edit_derive::kPaintedToneCount) * sizeof(GuiColor));

// EVERY VOCABULARY NAMES A DEFAULT PALETTE, IN THE DEFAULTS' ORDER, and every
// default belongs to one vocabulary (the preset menu's built-in schemes lead
// with them in this order, color_picker::preset_menu_rows).
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
// keys under its own chrome (scheme_record) is no loss of a word.
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

// EACH CHROME'S OWN SCHEME NAMES TAHOMA (2026-10-09): the chrome's own
// scheme installs no pick, whose face is Tahoma (fill_chrome_palette,
// render.cpp), so its built-in's tag must say the same — windows-2000-
// standard's source names Tahoma, clearlooks' and solaris' name no Windows
// font (the inert tag).
static_assert(std::ranges::all_of(kGuiDefaultPalettes,
                                  [](const GuiDefaultPalette& d) {
    return builtin_scheme(d.name)->chrome.face == GuiSchemeFace::Tahoma;
}));
// WINDOWS ME STANDARD IS WINDOWS 2000 STANDARD'S TWELVE IN MS SANS SERIF
// (the catalog's windows-me-standard; the generator asserts the same).
static_assert([] {
    const GuiChromeScheme* me = builtin_scheme("windows-me-standard");
    const GuiChromeScheme* w2k = builtin_scheme("windows-2000-standard");
    if (me == nullptr || w2k == nullptr) return false;
    GuiChromePick twelve = me->chrome;
    twelve.face = w2k->chrome.face;
    return twelve == w2k->chrome &&
           me->chrome.face == GuiSchemeFace::MsSansSerif;
}());

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
// struct's): no palette and no scheme, the live chrome's own.
static_assert(DeviceConfig{}.palette.empty());
static_assert(DeviceConfig{}.scheme.empty());

// A DEFAULT'S WORDS, its column of the role table.
constexpr GuiPaletteWords default_words(const GuiDefaultPalette& d) {
    GuiPaletteWords w{};
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        w[i] = kGuiPaletteRoles[i].*(d.column);
    return w;
}

// The index of the scheme key named `key` in kGuiChromeLines, or
// kGuiChromeLineCount.
constexpr std::size_t chrome_line_index(std::string_view key) {
    for (std::size_t k = 0; k < kGuiChromeLineCount; ++k)
        if (key == kGuiChromeLines[k].key) return k;
    return kGuiChromeLineCount;
}

// ONE KIND OF PRESET FILE (the head: palettes and schemes, two folders, two
// maps) — its folder's name, its suffix, the noun its lines say, the
// built-in test its file names must miss and THE LOADED MAP, by name,
// maintained by the picker's writes. Single-threaded: the launch read
// precedes every reader, and only the GUI thread reads or writes it.
template <class Record>
struct PresetKind {
    const char*      folder;
    std::string_view suffix;
    const char*      noun;
    bool (*is_builtin)(std::string_view);
    std::map<std::string, Record, std::less<>> loaded;
};
PresetKind<GuiPaletteWords> g_palettes{
    "palettes", ".palette", "palette",
    [](std::string_view n) { return is_builtin_palette_name(n); }, {}};
PresetKind<GuiChromePick> g_schemes{
    "schemes", ".scheme", "scheme",
    [](std::string_view n) { return is_builtin_scheme_name(n); }, {}};

template <class Record>
std::filesystem::path kind_folder_path(const PresetKind<Record>& kind) {
    const std::filesystem::path cfg = device_config_path();
    if (cfg.empty()) return {};
    return cfg.parent_path() / kind.folder;
}

template <class Record>
std::filesystem::path kind_file_path(const PresetKind<Record>& kind,
                                     const std::filesystem::path& folder,
                                     std::string_view name) {
    return folder / (std::string(name) + std::string(kind.suffix));
}

// ONE PALETTE FILE under the grammar (palette_file.h's head), its stem
// already judged: exactly the ten, a scheme's key or a retired role an
// unknown role.
std::expected<GuiPaletteWords, std::string> read_palette_file(
        const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected(std::string("could not open the file"));
    GuiPaletteWords                        out{};
    std::array<bool, kGuiPaletteRoleCount> named{};
    auto scan = warptempo_settings::scan_key_value_file(
        f, [&](int ln, const std::string& role, const std::string& value)
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
    // EVERY ROLE IS REQUIRED (the head: the picker writes all ten), the
    // first missing one in the table's order named. (A file from before the
    // ten, 2026-10-09, fails earlier, on its first retired line.)
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i) {
        if (!named[i]) {
            return std::unexpected("missing role '" +
                                   std::string(kGuiPaletteRoles[i].name) +
                                   "'");
        }
    }
    return out;
}

// ONE SCHEME FILE under the grammar (the head), its stem already judged: the
// nine block keys required, the first missing one in the table's order
// named; the inactive keys each optional (the unread ones following the
// active caption); a program role an unknown role.
std::expected<GuiChromePick, std::string> read_scheme_file(
        const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected(std::string("could not open the file"));
    GuiChromePick                         pick{};
    std::array<bool, kGuiChromeLineCount> named{};
    auto scan = warptempo_settings::scan_key_value_file(
        f, [&](int ln, const std::string& role, const std::string& value)
                  -> std::expected<void, std::string> {
        // THE OPTIONAL FONT LINE (palette_file.h's kGuiSchemeFontKey): one
        // of the two words, the scanner refusing a second line.
        if (role == kGuiSchemeFontKey) {
            const std::optional<GuiSchemeFace> face =
                scheme_font_of_word(value);
            if (!face) {
                return warptempo_parse::prefix_line_error(
                    ln, "font has invalid value '" + value +
                        "': must be tahoma or ms-sans-serif");
            }
            pick.face = *face;
            return {};
        }
        const std::size_t c = chrome_line_index(role);
        if (c == kGuiChromeLineCount) {
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
        set_chrome_line_word(pick, c, *w);
        named[c] = true;
        return {};
    }, std::span<const char* const>{});
    if (!scan) return std::unexpected(std::move(scan.error()));
    for (std::size_t k = 0; k < kGuiChromeLineCount; ++k) {
        if (is_chrome_block_line(k) && !named[k]) {
            return std::unexpected("missing role '" +
                                   std::string(kGuiChromeLines[k].key) +
                                   "'");
        }
    }
    return pick;
}

// One `role=#RRGGBB` line, uppercase, LF.
void put_line(std::string& s, const char* role, uint32_t word) {
    char hex[8];
    std::snprintf(hex, sizeof(hex), "#%06X",
                  static_cast<unsigned>(word & 0xFFFFFFu));
    s += role;
    s += '=';
    s += hex;
    s += '\n';
}

// The text write_palette_file puts down: every role in the table's order.
std::string palette_file_text(const GuiPaletteWords& words) {
    std::string s;
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        put_line(s, kGuiPaletteRoles[i].name, words[i]);
    return s;
}

// The text write_scheme_file puts down: the nine and each picked inactive
// key, in the table's order, then the font line when the pick's tag is MS
// Sans Serif (palette_file.h's kGuiSchemeFontKey: absent is tahoma).
std::string scheme_file_text(const GuiChromePick& pick) {
    std::string s;
    for (std::size_t k = 0; k < kGuiChromeLineCount; ++k) {
        const GuiChromeLine& l = kGuiChromeLines[k];
        if (l.word != nullptr) put_line(s, l.key, pick.*(l.word));
        else if (const std::optional<uint32_t>& v = pick.*(l.optional))
            put_line(s, l.key, *v);
    }
    if (pick.face != GuiSchemeFace::Tahoma) {
        s += kGuiSchemeFontKey;
        s += '=';
        s += scheme_font_word(pick.face);
        s += '\n';
    }
    return s;
}

// THE LAUNCH'S READ OF ONE FOLDER (the declarations): the names first,
// sorted, so the first error is the same file on every launch; each stem
// judged, then the file under its kind's reader.
template <class Record, class Reader>
std::optional<std::string> read_kind_folder(PresetKind<Record>& kind,
                                            Reader read_file) {
    const std::filesystem::path folder = kind_folder_path(kind);
    if (folder.empty()) return std::nullopt;
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) {
        // A MISSING FOLDER IS NO FILES; a failed query is the next call's
        // failure, said with the system's words.
        if (!ec) return std::nullopt;
    }
    const std::string unreadable = "could not read the " +
                                   std::string(kind.folder) + " folder '" +
                                   folder.string() + "': ";
    std::vector<std::filesystem::path> files;
    std::filesystem::directory_iterator it(folder, ec);
    if (ec) return unreadable + ec.message();
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name.size() <= kind.suffix.size() || !name.ends_with(kind.suffix))
            continue;
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        files.push_back(it->path());
    }
    if (ec) return unreadable + ec.message();
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& p : files) {
        const std::string file = p.filename().string();
        const std::string name =
            file.substr(0, file.size() - kind.suffix.size());
        const std::string head = "invalid " + std::string(kind.noun) +
                                 " file '" + p.string() + "': ";
        if (!is_palette_name_spelling(name)) {
            return head + "the name must be <name>" + std::string(kind.suffix) +
                   ", the name 1 to 40 printable ASCII characters with no "
                   "leading or trailing space";
        }
        if (kind.is_builtin(name)) {
            return head + name + " is a built-in " + kind.noun +
                   " and takes no file";
        }
        auto record = read_file(p);
        if (!record) return head + record.error();
        kind.loaded.emplace(name, *record);
    }
    return std::nullopt;
}

template <class Record>
std::vector<std::string> kind_file_names(const PresetKind<Record>& kind) {
    std::vector<std::string> out;
    out.reserve(kind.loaded.size());
    for (const auto& [name, record] : kind.loaded) out.push_back(name);
    return out;
}

template <class Record>
std::optional<std::string> write_kind_file(PresetKind<Record>& kind,
                                           std::string_view name,
                                           const std::string& text,
                                           const Record& record) {
    // The names are the picker's to judge (the declarations).
    assert(is_palette_name_spelling(name));
    assert(!kind.is_builtin(name));
    const std::filesystem::path folder = kind_folder_path(kind);
    // The launch read the config through the same resolver, and the
    // environment does not change under the process.
    assert(!folder.empty());
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return "could not create the " + std::string(kind.folder) +
               " folder '" + folder.string() + "': " + ec.message();
    }
    const std::filesystem::path p = kind_file_path(kind, folder, name);
    if (!atomic_write_string_to_path(p.string(), text))
        return "could not write the " + std::string(kind.noun) + " file '" +
               p.string() + "'";
    kind.loaded.insert_or_assign(std::string(name), record);
    return std::nullopt;
}

template <class Record>
std::optional<std::string> rename_kind_file(PresetKind<Record>& kind,
                                            std::string_view old_name,
                                            std::string_view new_name) {
    // The names are the picker's to judge (the declarations).
    const auto it = kind.loaded.find(old_name);
    assert(it != kind.loaded.end());
    assert(new_name != old_name);
    assert(is_palette_name_spelling(new_name));
    // neither a built-in's nor taken
    assert(!kind.is_builtin(new_name) && !kind.loaded.contains(new_name));
    const std::filesystem::path folder = kind_folder_path(kind);
    assert(!folder.empty());
    const std::filesystem::path from = kind_file_path(kind, folder, old_name);
    const std::filesystem::path to   = kind_file_path(kind, folder, new_name);
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) {
        return "could not rename the " + std::string(kind.noun) + " file '" +
               from.string() + "': " + ec.message();
    }
    const Record record = it->second;
    kind.loaded.erase(it);
    kind.loaded.emplace(std::string(new_name), record);
    return std::nullopt;
}

template <class Record>
std::optional<std::string> remove_kind_file(PresetKind<Record>& kind,
                                            std::string_view name) {
    // The name is the picker's to judge (the declarations): a loaded
    // file's, never a built-in's (which the map does not hold).
    const auto it = kind.loaded.find(name);
    assert(it != kind.loaded.end());
    const std::filesystem::path folder = kind_folder_path(kind);
    assert(!folder.empty());
    const std::filesystem::path p = kind_file_path(kind, folder, name);
    std::error_code ec;
    std::filesystem::remove(p, ec);
    if (ec) {
        return "could not delete the " + std::string(kind.noun) + " file '" +
               p.string() + "': " + ec.message();
    }
    kind.loaded.erase(it);
    return std::nullopt;
}

} // namespace

std::string_view effective_palette_name(std::string_view palette) {
    return palette.empty()
               ? std::string_view(live_chrome_spec().default_palette)
               : palette;
}

std::string_view effective_scheme_name(std::string_view scheme) {
    // The chrome's own scheme shares its default palette's word (the head).
    return scheme.empty()
               ? std::string_view(live_chrome_spec().default_palette)
               : scheme;
}

std::filesystem::path palette_folder_path() {
    return kind_folder_path(g_palettes);
}

std::filesystem::path scheme_folder_path() {
    return kind_folder_path(g_schemes);
}

std::optional<std::string> read_palette_folder() {
    return read_kind_folder(g_palettes, read_palette_file);
}

std::optional<std::string> read_scheme_folder() {
    return read_kind_folder(g_schemes, read_scheme_file);
}

std::vector<std::string> palette_file_names() {
    return kind_file_names(g_palettes);
}

std::vector<std::string> scheme_file_names() {
    return kind_file_names(g_schemes);
}

bool is_palette_name(std::string_view name) {
    return is_builtin_palette_name(name) || g_palettes.loaded.contains(name);
}

bool is_scheme_name(std::string_view name) {
    return is_builtin_scheme_name(name) || g_schemes.loaded.contains(name);
}

GuiPaletteWords palette_record(std::string_view name) {
    if (const GuiDefaultPalette* d = default_palette(name))
        return default_words(*d);
    const auto it = g_palettes.loaded.find(name);
    // Every caller's name came through is_palette_name: a miss is a program
    // bug.
    assert(it != g_palettes.loaded.end());
    return it->second;
}

std::optional<GuiChromePick> scheme_record(std::string_view name) {
    // A BUILT-IN (the declaration): its twelve, unless it is the live
    // chrome's own scheme, which carries none.
    if (const GuiChromeScheme* b = builtin_scheme(name)) {
        if (name == live_chrome_spec().default_palette) return std::nullopt;
        return b->chrome;
    }
    const auto it = g_schemes.loaded.find(name);
    // Every caller's name came through is_scheme_name: a miss is a program
    // bug.
    assert(it != g_schemes.loaded.end());
    return it->second;
}

std::optional<std::string> write_palette_file(std::string_view name,
                                              const GuiPaletteWords& words) {
    return write_kind_file(g_palettes, name, palette_file_text(words), words);
}

std::optional<std::string> write_scheme_file(std::string_view name,
                                             const GuiChromePick& pick) {
    return write_kind_file(g_schemes, name, scheme_file_text(pick), pick);
}

std::optional<std::string> rename_palette_file(std::string_view old_name,
                                               std::string_view new_name) {
    return rename_kind_file(g_palettes, old_name, new_name);
}

std::optional<std::string> rename_scheme_file(std::string_view old_name,
                                              std::string_view new_name) {
    return rename_kind_file(g_schemes, old_name, new_name);
}

std::optional<std::string> remove_palette_file(std::string_view name) {
    return remove_kind_file(g_palettes, name);
}

std::optional<std::string> remove_scheme_file(std::string_view name) {
    return remove_kind_file(g_schemes, name);
}
