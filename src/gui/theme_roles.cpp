#include "theme_roles.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

// The format, the groups, the order, the left-out files and THE DERIVATION
// are at the declaration's head (theme_roles.h), the one owner; this file is
// their code.

namespace {

// THE ROLE TABLE: the roles a theme file holds, each exactly once — EXACTLY
// THE ROLES A TONE READS (the head's DERIVATION), Live's names, the one
// nested role flattened to `StandardVuMeter/Maximum` (the head's THE
// FORMAT). The converter (tools/themes/ask_to_theme.py) reads the names out
// of this initializer, so it is the one enumeration, and writes a file's
// lines in its order. Each was present in every stock and Sonic Bloom file
// when it joined (the press arc's SelectionBackground and
// SelectionForeground 2026-11-05; StandbySelectionForeground 2026-11-06;
// TakeLaneTrackNotHighlighted, ViewCheckControlDisabledOff,
// ScrollbarInnerHandle and DetailViewRulerMarkings, the grid, the disabled
// key's ink, the trim span and the ruler's ticks, 2026-11-07; RangeEditField,
// the NOISE level case's fill, and ScaleAwareness, THE RADIO KEYS' PURPLE,
// 2026-11-11).
constexpr std::string_view kThemeRoles[] = {
    "SurfaceArea", "Desktop", "SurfaceBackground", "ControlContrastFrame",
    "ControlForeground", "TextDisabled", "BackgroundClip",
    "ViewCheckControlEnabledOn", "RangeDefault", "StandbySelectionBackground",
    "StandbySelectionForeground",
    "SelectionBackground", "SelectionForeground",
    "Alert", "WaveformColor", "DetailViewBackground", "DetailGridTiles",
    "ScaleAwareness", "RangeEditField",
    "TakeLaneTrackNotHighlighted", "ViewCheckControlDisabledOff",
    "ScrollbarInnerHandle", "DetailViewRulerMarkings",
    "StandardVuMeter/Maximum",
};
constexpr std::size_t kThemeRoleCount = std::size(kThemeRoles);

// THE OPTIONAL KEYS (architect 2026-11-12, session 113; the head's THE
// FORMAT): two tones of the app's own a file MAY set, each at most once,
// each 6 hex digits — OUR keys in Live's naming style, never Live roles, so a
// SEPARATE table the converter never reads (its regex anchors on
// kThemeRoles' own initializer) and never writes. A key a file leaves out
// takes its fallback in derive(). In OptionalKey's order.
constexpr std::string_view kThemeOptionalKeys[] = {
    "PlayheadLine", "ControlContrastFrameDisabled",
};
constexpr std::size_t kThemeOptionalKeyCount = std::size(kThemeOptionalKeys);
enum class OptionalKey : std::size_t { PlayheadLine, ControlContrastFrameDisabled };

constexpr std::string_view kThemeSuffix = ".theme";

constexpr std::size_t role_index(std::string_view name) {
    for (std::size_t i = 0; i < kThemeRoleCount; ++i)
        if (kThemeRoles[i] == name) return i;
    return kThemeRoleCount;
}

constexpr std::size_t optional_key_index(std::string_view name) {
    for (std::size_t i = 0; i < kThemeOptionalKeyCount; ++i)
        if (kThemeOptionalKeys[i] == name) return i;
    return kThemeOptionalKeyCount;
}
// The enum names the table's rows, and no optional key is a role's name.
static_assert(kThemeOptionalKeys[static_cast<std::size_t>(
                  OptionalKey::PlayheadLine)] == "PlayheadLine");
static_assert(kThemeOptionalKeys[static_cast<std::size_t>(
                  OptionalKey::ControlContrastFrameDisabled)] ==
              "ControlContrastFrameDisabled");
static_assert(role_index("PlayheadLine") == kThemeRoleCount &&
              role_index("ControlContrastFrameDisabled") == kThemeRoleCount);

// One file's roles: each an uppercase hex word of 6 or 8 digits, in
// kThemeRoles' order; and its optional keys, each an uppercase 6-digit word
// or EMPTY where the file leaves the key out, in kThemeOptionalKeys' order.
struct ThemeRoles {
    std::string words[kThemeRoleCount];
    std::string optional_words[kThemeOptionalKeyCount];
    const std::string& operator[](std::string_view name) const {
        const std::size_t i = role_index(name);
        assert(i < kThemeRoleCount);
        return words[i];
    }
    const std::string& operator[](OptionalKey key) const {
        return optional_words[static_cast<std::size_t>(key)];
    }
};

bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

// THE PARSE (the head's THE FORMAT): the file's roles, or its first error,
// with its line number where it has one.
std::expected<ThemeRoles, std::string> parse_theme_file(std::string_view text) {
    ThemeRoles roles;
    bool found[kThemeRoleCount] = {};
    bool found_optional[kThemeOptionalKeyCount] = {};
    std::size_t at = 0;
    for (int line_no = 1; at < text.size(); ++line_no) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto fail = [&](const std::string& why) {
            return std::unexpected("line " + std::to_string(line_no) + ": " +
                                   why);
        };
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos)
            return fail("not a comment or a Name=HEX line");
        const std::string_view name = line.substr(0, eq);
        const std::string_view hex  = line.substr(eq + 1);
        if (name.empty() || name.find(' ') != std::string_view::npos ||
            name.find('\t') != std::string_view::npos)
            return fail("not a comment or a Name=HEX line");
        if ((hex.size() != 6 && hex.size() != 8) ||
            !std::all_of(hex.begin(), hex.end(), is_hex))
            return fail("the value is not 6 or 8 hex digits");
        std::string word(hex);
        for (char& c : word)
            if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
        const std::size_t k = optional_key_index(name);
        if (k != kThemeOptionalKeyCount) {
            if (hex.size() != 6)
                return fail("the value of '" + std::string(name) +
                            "' is not 6 hex digits");
            if (found_optional[k])
                return fail("the key '" + std::string(name) +
                            "' a second time");
            roles.optional_words[k] = std::move(word);
            found_optional[k] = true;
            continue;
        }
        const std::size_t i = role_index(name);
        if (i == kThemeRoleCount)
            return fail("no role '" + std::string(name) + "'");
        if (found[i])
            return fail("the role '" + std::string(name) + "' a second time");
        roles.words[i] = std::move(word);
        found[i] = true;
    }
    // A missing ROLE is an error; a missing optional key is its fallback's.
    std::string missing;
    for (std::size_t i = 0; i < kThemeRoleCount; ++i) {
        if (found[i]) continue;
        if (!missing.empty()) missing += ", ";
        missing += kThemeRoles[i];
    }
    if (!missing.empty()) return std::unexpected("missing role(s) " + missing);
    return roles;
}

// -- THE DERIVATION'S ARITHMETIC (the head; Python's, bit for bit: doubles
//    in its operation order) ---------------------------------------------

struct Rgb  { int r, g, b; };
struct Rgba { int r, g, b; double a; };

int hex_byte(const std::string& word, std::size_t at) {
    const auto digit = [](char c) {
        return (c >= '0' && c <= '9') ? c - '0' : c - 'A' + 10;
    };
    return digit(word[at]) * 16 + digit(word[at + 1]);
}

// A 6- or 8-digit hex word -> (r, g, b, alpha), alpha aa / 255.0 or 1.0.
Rgba rgba(const std::string& word) {
    const double a = word.size() == 8 ? hex_byte(word, 6) / 255.0 : 1.0;
    return Rgba{hex_byte(word, 0), hex_byte(word, 2), hex_byte(word, 4), a};
}
Rgb opaque(const std::string& word) {
    const Rgba c = rgba(word);
    return Rgb{c.r, c.g, c.b};
}

// Python's max(0, min(255, int(round(x)))): round() is half to even, which is
// nearbyint under the default rounding mode (never std::round, half away).
int clamp_byte(double x) {
    const double r = std::nearbyint(x);
    if (r < 0.0) return 0;
    if (r > 255.0) return 255;
    return static_cast<int>(r);
}

// `top` (with alpha) source-over the opaque `ground`, per channel.
Rgb over(const Rgba& top, const Rgba& ground) {
    return Rgb{clamp_byte(top.r * top.a + ground.r * (1.0 - top.a)),
               clamp_byte(top.g * top.a + ground.g * (1.0 - top.a)),
               clamp_byte(top.b * top.a + ground.b * (1.0 - top.a))};
}

// a + t · (b − a), per channel, rounded.
Rgb mix(const Rgb& a, const Rgb& b, double t) {
    return Rgb{clamp_byte(a.r + t * (b.r - a.r)),
               clamp_byte(a.g + t * (b.g - a.g)),
               clamp_byte(a.b + t * (b.b - a.b))};
}

// c · ratio, per channel, rounded and clamped (the lit key's derived tones).
Rgb scale(const Rgb& c, double ratio) {
    return Rgb{clamp_byte(c.r * ratio), clamp_byte(c.g * ratio),
               clamp_byte(c.b * ratio)};
}

// 255 − (255 − c) · ratio, per channel, rounded and clamped as `scale`: the
// lit-but-inert tones on a light theme, scaled toward white (derive).
Rgb scale_toward_white(const Rgb& c, double ratio) {
    return Rgb{clamp_byte(255.0 - (255 - c.r) * ratio),
               clamp_byte(255.0 - (255 - c.g) * ratio),
               clamp_byte(255.0 - (255 - c.b) * ratio)};
}

double mean(const Rgb& c) { return (c.r + c.g + c.b) / 3.0; }

uint32_t packed(const Rgb& c) {
    return (static_cast<uint32_t>(c.r) << 16) |
           (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

// One theme's tones (the head's DERIVATION). Total: every tone is a role, a
// composite, a mix, a scaled role or an optional key's word (its fallback
// where the file leaves the key out), and each of the two ratios' zero
// divisor has its own answer (the lit key's derived tones, below).
GuiThemeTones derive(const ThemeRoles& roles) {
    const Rgb ground   = opaque(roles["SurfaceArea"]);
    const Rgb desktop  = opaque(roles["Desktop"]);
    const Rgb face     = opaque(roles["SurfaceBackground"]);
    const Rgb contrast = opaque(roles["ControlContrastFrame"]);
    const Rgb text     = opaque(roles["ControlForeground"]);
    const Rgb dim      = opaque(roles["TextDisabled"]);
    const Rgb clip     = opaque(roles["BackgroundClip"]);
    const Rgb on       = opaque(roles["ViewCheckControlEnabledOn"]);
    const Rgb radio    = opaque(roles["ScaleAwareness"]);
    const Rgb range    = opaque(roles["RangeDefault"]);
    const Rgb standby  = opaque(roles["StandbySelectionBackground"]);
    const Rgb standby_text = opaque(roles["StandbySelectionForeground"]);
    const Rgb sel_bg   = opaque(roles["SelectionBackground"]);
    const Rgb sel_fg   = opaque(roles["SelectionForeground"]);
    const Rgb alert    = opaque(roles["Alert"]);
    const Rgb vu_max   = opaque(roles["StandardVuMeter/Maximum"]);
    const Rgba face_a  = rgba(roles["SurfaceBackground"]);
    // The level case's fill, Live's volume color over the key face it paints
    // on (the head's COMPOSITED block).
    const Rgb lvl_fill = over(rgba(roles["RangeEditField"]), face_a);
    const Rgb core     = face;  // the magnify core = SurfaceBackground (the head)
    const Rgba detail  = rgba(roles["DetailViewBackground"]);
    const Rgb ink      = over(rgba(roles["WaveformColor"]), detail);
    const Rgb canvas   = over(rgba(roles["DetailGridTiles"]), detail);
    // The four faithful tones, each Live's role over the surface it paints on
    // (the head's COMPOSITED block).
    const Rgba canvas_a  = Rgba{canvas.r, canvas.g, canvas.b, 1.0};
    const Rgb grid_role  = over(rgba(roles["TakeLaneTrackNotHighlighted"]), canvas_a);
    const Rgb dim_ink    = over(rgba(roles["ViewCheckControlDisabledOff"]), face_a);
    const Rgb span_role  = over(rgba(roles["ScrollbarInnerHandle"]),
                                rgba(roles["Desktop"]));
    const Rgb tick_role  = over(rgba(roles["DetailViewRulerMarkings"]), face_a);
    const Rgb white    = Rgb{255, 255, 255};
    // THE LIT KEY'S DERIVED TONES (the head's DERIVED block; architect
    // 2026-11-11, session 106). THE LIT PRESS FILL: the accent scaled by the
    // ratio the press face bears to the key face, mean over mean; a black
    // key face (no divisor) leaves the accent as it is. THE LIT-BUT-INERT
    // PAIR: the accent and the print on it, both moved by the one ratio the
    // disabled key's ink bears to the text, in the direction the disabled
    // ink lies from the text (architect 2026-11-11, session 109, his pick
    // (a): "the same rule in negative" on a light theme). Where mean(text)
    // >= mean(dim ink), a dark theme, each tone is scaled toward black by
    // mean(dim ink) / mean(text); the divisor is zero only when the text
    // and the dim ink are both black, and that answer is ratio 1, both
    // tones whole. Where mean(text) < mean(dim ink), a light theme, each
    // tone is scaled toward white by (255 − mean(dim ink)) / (255 −
    // mean(text)) on its distance from white; that divisor is never zero
    // (mean(text) < mean(dim ink) <= 255). THE RADIO KEYS' PRESS AND INERT
    // FACES (architect 2026-11-11, session 107): the purple by the same two
    // ratios, the inert one on the same branch.
    const double face_mean   = mean(face);
    const double press_ratio =
        face_mean == 0.0 ? 1.0 : mean(desktop) / face_mean;
    const double text_mean   = mean(text);
    const double dim_mean    = mean(dim_ink);
    const bool   dim_to_white = text_mean < dim_mean;
    const double dim_ratio   =
        dim_to_white      ? (255.0 - dim_mean) / (255.0 - text_mean)
        : text_mean == 0.0 ? 1.0
                           : dim_mean / text_mean;
    const auto dim_tone = [&](const Rgb& c) {
        return dim_to_white ? scale_toward_white(c, dim_ratio)
                            : scale(c, dim_ratio);
    };
    const Rgb lit_press  = scale(on, press_ratio);
    const Rgb lit_dim    = dim_tone(on);
    const Rgb on_lit_dim = dim_tone(contrast);
    const Rgb radio_press = scale(radio, press_ratio);
    const Rgb radio_dim   = dim_tone(radio);
    GuiThemeTones t{};
    t.ground              = packed(ground);
    t.text                = packed(text);
    t.text_dim            = packed(dim);
    t.text_on_lit         = packed(contrast);
    t.key_dim_ink         = packed(dim_ink);
    t.text_on_lit_dim     = packed(on_lit_dim);
    t.key_face            = packed(face);
    t.key_press_face      = packed(desktop);
    t.lit_press_fill      = packed(lit_press);
    t.lit_dim_fill        = packed(lit_dim);
    t.radio_lit_fill      = packed(radio);
    t.radio_lit_press_fill = packed(radio_press);
    t.radio_lit_dim_fill  = packed(radio_dim);
    t.well_frame          = packed(dim);
    t.ruler_face          = packed(face);
    t.ruler_tick          = packed(tick_role);
    t.lane_face           = packed(desktop);
    t.trim_bar_face       = packed(desktop);
    t.trim_span_face      = packed(span_role);
    t.selection_fill      = packed(standby);
    t.selection_text      = packed(standby_text);
    t.list_press_fill     = packed(sel_bg);
    t.list_press_text     = packed(sel_fg);
    t.pane_face           = packed(face);
    t.score_pane_face     = packed(ink);
    t.card_face           = packed(alert);
    t.card_ink            = packed(desktop);
    t.clip_lamp           = packed(vu_max);
    t.level_fill          = packed(lvl_fill);
    t.waveform_canvas     = packed(canvas);
    t.waveform_ink        = packed(ink);
    t.waveform_core       = packed(core);
    t.grid                = packed(grid_role);
    t.center              = packed(ink);
    t.cue                 = packed(on);
    t.cue_outline         = packed(contrast);
    t.inactive_cue        = packed(clip);
    // THE TWO OPTIONAL KEYS (the head's DERIVED block; architect 2026-11-12,
    // session 113): the file's word, else the fallback — the disabled chip's
    // outline ControlContrastFrame, the playhead's line white half way to
    // the head.
    t.inactive_cue_outline = packed(
        roles[OptionalKey::ControlContrastFrameDisabled].empty()
            ? contrast
            : opaque(roles[OptionalKey::ControlContrastFrameDisabled]));
    t.playhead_head       = packed(range);
    t.playhead_line       = packed(
        roles[OptionalKey::PlayheadLine].empty()
            ? mix(white, range, 0.5)
            : opaque(roles[OptionalKey::PlayheadLine]));
    t.scanner             = packed(white);
    return t;
}

// -- THE LIST ----------------------------------------------------------

std::vector<GuiTheme>& themes_storage() {
    static std::vector<GuiTheme> list;
    return list;
}

// The one stderr line a left-out user file prints (the head).
void say_left_out(const std::filesystem::path& path, const std::string& why) {
    std::fprintf(stderr, "warptempo_gui: Theme '%s' left out: %s\n",
                 path.string().c_str(), why.c_str());
}

// The slug a file name ending `.theme` spells (empty when it spells none).
std::string slug_of_file_name(std::string_view name) {
    return theme_slug_of_stem(name.substr(0, name.size() - kThemeSuffix.size()));
}

struct ThemeFile {
    std::string slug;
    std::string name;   // the file name (a bundled file) or its path (a user's)
    GuiThemeTones tones;
};

// One shipped group: every file parsed and checked (the head's build
// defect), by slug — or the first defect's message.
std::expected<std::vector<ThemeFile>, std::string> read_bundled_group(
        std::string_view folder,
        const std::map<std::string, std::string>& files) {
    std::vector<ThemeFile> out;
    for (const auto& [name, text] : files) {
        const std::string where =
            "themes: the shipped " + std::string(folder) + "/" + name;
        if (!name.ends_with(kThemeSuffix))
            return std::unexpected(where + ": not a .theme file");
        const std::string slug = slug_of_file_name(name);
        if (slug.empty())
            return std::unexpected(where + ": its name spells no slug");
        if (name != slug + std::string(kThemeSuffix))
            return std::unexpected(where + ": not named by its slug '" + slug +
                                   "'");
        if (slug == kGuiDefaultThemeSlug || slug == kGuiReferenceThemeSlug)
            return std::unexpected(where + ": its slug '" + slug +
                                   "' is the list's own");
        std::expected<ThemeRoles, std::string> roles = parse_theme_file(text);
        if (!roles) return std::unexpected(where + ": " + roles.error());
        out.push_back(ThemeFile{slug, name, derive(*roles)});
    }
    std::sort(out.begin(), out.end(),
              [](const ThemeFile& a, const ThemeFile& b) {
                  return a.slug < b.slug;
              });
    return out;
}

// The user's flat folder: each regular `*.theme` directly in it, by slug
// (then by file name), each read and parsed; a file left out with its line,
// a slug in `taken` too (and then taken). A missing or unlistable folder
// holds none, silently.
std::vector<ThemeFile> read_user_folder(const std::filesystem::path& dir,
                                        std::vector<std::string>& taken) {
    std::vector<ThemeFile> out;
    if (dir.empty()) return out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return out;
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    for (const std::filesystem::directory_entry& e : it) {
        std::error_code fec;
        if (!e.is_regular_file(fec)) continue;
        const std::string name = e.path().filename().string();
        if (name.size() <= kThemeSuffix.size() || !name.ends_with(kThemeSuffix))
            continue;
        files.emplace_back(slug_of_file_name(name), e.path());
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second.filename().string() <
                         b.second.filename().string();
              });
    for (auto& [slug, path] : files) {
        if (slug.empty()) {
            say_left_out(path, "its name spells no slug");
            continue;
        }
        if (std::find(taken.begin(), taken.end(), slug) != taken.end()) {
            say_left_out(path, "its slug '" + slug + "' is taken");
            continue;
        }
        std::ifstream in(path, std::ios::binary);
        std::ostringstream buf;
        if (in) buf << in.rdbuf();
        if (!in || in.bad()) {
            say_left_out(path, "the file could not be read");
            continue;
        }
        std::expected<ThemeRoles, std::string> roles =
            parse_theme_file(buf.str());
        if (!roles) {
            say_left_out(path, roles.error());
            continue;
        }
        taken.push_back(slug);
        out.push_back(ThemeFile{std::move(slug), path.string(), derive(*roles)});
    }
    return out;
}

} // namespace

std::optional<std::string> load_gui_themes(
        const std::map<std::string, std::string>& system_files,
        const std::map<std::string, std::string>& extra_files,
        const std::filesystem::path& user_dir) {
    // The groups, the order and the left-out files are the head's
    // (theme_roles.h). Once per process (architect 2026-11-06, session 75):
    // the list is immutable after this returns, so a pointer into it stays
    // good (install_palette copies its row all the same, render.h).
    static bool loaded = false;
    assert(!loaded && "the themes are read once, at launch");
    loaded = true;

    auto system = read_bundled_group("system", system_files);
    if (!system) return system.error();
    auto extra = read_bundled_group("extra", extra_files);
    if (!extra) return extra.error();

    // THE SHIPPED SLUGS, each once: the list's own two, then both groups.
    std::vector<std::string> taken{std::string(kGuiDefaultThemeSlug),
                                   std::string(kGuiReferenceThemeSlug)};
    for (const std::vector<ThemeFile>* group : {&*system, &*extra}) {
        for (const ThemeFile& f : *group) {
            if (std::find(taken.begin(), taken.end(), f.slug) != taken.end())
                return "themes: two shipped files of the slug '" + f.slug + "'";
            taken.push_back(f.slug);
        }
    }
    const auto source = std::find_if(
        system->begin(), system->end(), [](const ThemeFile& f) {
            return f.slug == kGuiDefaultThemeSourceSlug;
        });
    if (source == system->end())
        return "themes: no shipped system/" +
               std::string(kGuiDefaultThemeSourceSlug) + ".theme";

    std::vector<ThemeFile> user = read_user_folder(user_dir, taken);

    // SYSTEM: `default` first, then by slug the files and the reference row.
    std::vector<GuiTheme> list;
    list.push_back(GuiTheme{std::string(kGuiDefaultThemeSlug),
                            std::string(kGuiThemeGroupSystem), source->tones});
    std::vector<GuiTheme> system_rows;
    for (const ThemeFile& f : *system)
        system_rows.push_back(
            GuiTheme{f.slug, std::string(kGuiThemeGroupSystem), f.tones});
    system_rows.push_back(GuiTheme{std::string(kGuiReferenceThemeSlug),
                                   std::string(kGuiThemeGroupSystem),
                                   kGuiReferenceThemeTones});
    std::sort(system_rows.begin(), system_rows.end(),
              [](const GuiTheme& a, const GuiTheme& b) {
                  return a.slug < b.slug;
              });
    for (GuiTheme& t : system_rows) list.push_back(std::move(t));
    for (const ThemeFile& f : *extra)
        list.push_back(GuiTheme{f.slug, std::string(kGuiThemeGroupExtra), f.tones});
    for (const ThemeFile& f : user)
        list.push_back(GuiTheme{f.slug, std::string(kGuiThemeGroupUser), f.tones});
    themes_storage() = std::move(list);
    return std::nullopt;
}

const std::vector<GuiTheme>& gui_themes() { return themes_storage(); }

const GuiTheme* find_gui_theme(std::string_view slug) {
    for (const GuiTheme& t : themes_storage())
        if (t.slug == slug) return &t;
    return nullptr;
}

const GuiTheme& default_gui_theme() {
    assert(!themes_storage().empty() && "the themes are loaded at launch");
    return themes_storage().front();
}
