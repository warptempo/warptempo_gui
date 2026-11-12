#include "theme_ask.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

// The walk, the order, the left-out files and THE DERIVATION are at the
// declaration's head (theme_ask.h), the one owner; this file is their code.

namespace {

// THE ROLES READ from every file (the head's DERIVATION), each present in
// every stock and Sonic Bloom file (checked over all 45, 2026-11-05; the
// press arc's two, SelectionBackground and SelectionForeground, the same
// day; SearchIndication, immaterial_mod's accent,
// over all 42 files 2026-11-06; StandbySelectionForeground, the selection
// pair's text, over all 31 he drops, 2026-11-06; TakeLaneTrackNotHighlighted,
// ViewCheckControlDisabledOff, ScrollbarInnerHandle and
// DetailViewRulerMarkings, the grid, the disabled key's ink, the trim span and
// the ruler's ticks, read since 2026-11-07, over all 31 he drops and Default).
// A file lacking one is left out (the head). THE LIST NAMES EXACTLY THE
// ROLES A TONE READS: RangeEditField, Live's volume color, is the NOISE
// level case's fill (architect 2026-11-11, the head's COMPOSITED block;
// present in every file he drops — it was read here 2026-11-05 to
// 2026-11-10 too), so a file lacking it is left out; ChosenPreListen, the
// second level fill of that week, is read by no tone and is not listed.
// ScaleAwareness, THE RADIO KEYS' PURPLE (architect 2026-11-11, session
// 107; the head's ROLE-NAMED block), was read here 2026-11-05 to 2026-11-10
// for the doors and checked present in every stock and Sonic Bloom file
// then; a file lacking it is left out.
constexpr std::string_view kAskRoles[] = {
    "SurfaceArea", "Desktop", "SurfaceBackground", "ControlContrastFrame",
    "ControlForeground", "TextDisabled", "BackgroundClip",
    "ViewCheckControlEnabledOn", "RangeDefault", "StandbySelectionBackground",
    "StandbySelectionForeground",
    "SelectionBackground", "SelectionForeground",
    "Alert", "WaveformColor", "DetailViewBackground", "DetailGridTiles",
    "ScaleAwareness", "RangeEditField",
    "SearchIndication",
    "TakeLaneTrackNotHighlighted", "ViewCheckControlDisabledOff",
    "ScrollbarInnerHandle", "DetailViewRulerMarkings",
};
constexpr std::size_t kAskRoleCount = std::size(kAskRoles);

// THE ONE NESTED ROLE (the head's THE ROLES READ).
constexpr std::string_view kNestedOpen  = "<StandardVuMeter>";
constexpr std::string_view kNestedClose = "</StandardVuMeter>";
constexpr std::string_view kNestedRole  = "Maximum";

constexpr std::size_t role_index(std::string_view name) {
    for (std::size_t i = 0; i < kAskRoleCount; ++i)
        if (kAskRoles[i] == name) return i;
    return kAskRoleCount;
}

// One file's roles: each an uppercase hex word of 6 or 8 digits, the flat
// roles in kAskRoles' order, the nested one beside them.
struct AskRoles {
    std::string words[kAskRoleCount];
    std::string vu_maximum;
    const std::string& operator[](std::string_view name) const {
        const std::size_t i = role_index(name);
        assert(i < kAskRoleCount);
        return words[i];
    }
    std::string& operator[](std::string_view name) {
        const std::size_t i = role_index(name);
        assert(i < kAskRoleCount);
        return words[i];
    }
};

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}
bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// THE ROLE SCANNER: every `<Name Value="#hex" />` of `text` in order — a name
// of [A-Za-z0-9_]+, at least one space, `Value="#`, exactly 6 or 8 hex digits,
// `"`, any spaces, `/>` — non-overlapping, left to right; `fn(name, word)` for
// each, the word uppercased, until it answers false.
template <class Fn>
void scan_roles(std::string_view text, Fn&& fn) {
    constexpr std::string_view kValue = "Value=\"#";
    const std::size_t n = text.size();
    std::size_t i = 0;
    for (;;) {
        i = text.find('<', i);
        if (i == std::string_view::npos) return;
        std::size_t p = i + 1;
        const std::size_t name_at = p;
        while (p < n && is_name_char(text[p])) ++p;
        const std::size_t name_len = p - name_at;
        const std::size_t space_at = p;
        while (p < n && is_space(text[p])) ++p;
        bool ok = name_len > 0 && p > space_at &&
                  text.substr(p, kValue.size()) == kValue;
        std::size_t hex_at = 0;
        std::size_t hex_len = 0;
        if (ok) {
            p += kValue.size();
            hex_at = p;
            while (p < n && is_hex(text[p])) ++p;
            hex_len = p - hex_at;
            ok = (hex_len == 6 || hex_len == 8) && p < n && text[p] == '"';
        }
        if (ok) {
            ++p;
            while (p < n && is_space(text[p])) ++p;
            ok = text.substr(p, 2) == "/>";
        }
        if (!ok) { ++i; continue; }
        p += 2;
        std::string word(text.substr(hex_at, hex_len));
        for (char& c : word)
            if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
        if (!fn(text.substr(name_at, name_len), std::move(word))) return;
        i = p;
    }
}

// THE READ: the file's flat roles (each the first occurrence of its name
// anywhere) and the nested one, or why the file is left out.
std::expected<AskRoles, std::string> read_ask_roles(
        const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::unexpected("the file could not be read");
    std::ostringstream buf;
    buf << in.rdbuf();
    if (in.bad()) return std::unexpected("the file could not be read");
    const std::string text = buf.str();
    AskRoles roles;
    bool found[kAskRoleCount] = {};
    scan_roles(text, [&](std::string_view name, std::string word) {
        const std::size_t i = role_index(name);
        if (i < kAskRoleCount && !found[i]) {
            roles.words[i] = std::move(word);
            found[i] = true;
        }
        return true;
    });
    std::string missing;
    for (std::size_t i = 0; i < kAskRoleCount; ++i) {
        if (found[i]) continue;
        if (!missing.empty()) missing += ", ";
        missing += kAskRoles[i];
    }
    if (!missing.empty()) return std::unexpected("missing role(s) " + missing);
    const std::string_view all(text);
    const std::size_t open = all.find(kNestedOpen);
    if (open != std::string_view::npos) {
        const std::size_t body = open + kNestedOpen.size();
        const std::size_t close = all.find(kNestedClose, body);
        if (close != std::string_view::npos) {
            scan_roles(all.substr(body, close - body),
                       [&](std::string_view name, std::string word) {
                           if (name != kNestedRole) return true;
                           roles.vu_maximum = std::move(word);
                           return false;
                       });
        }
    }
    if (roles.vu_maximum.empty())
        return std::unexpected("missing nested role StandardVuMeter/Maximum");
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

double mean(const Rgb& c) { return (c.r + c.g + c.b) / 3.0; }

double srgb_to_linear(int byte) {
    const double x = byte / 255.0;
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}

// A linear channel, clipped to [0, 1], encoded to a float byte.
double linear_to_srgb(double x) {
    x = std::max(0.0, std::min(1.0, x));
    return (x <= 0.0031308 ? 12.92 * x
                           : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055) *
           255.0;
}

// The cube root as copysign(|x| ** (1/3), x) — pow, not std::cbrt (the head).
double cube_root(double x) {
    return std::copysign(std::pow(std::fabs(x), 1.0 / 3.0), x);
}

constexpr double kPi = 3.14159265358979323846;
// CPython's math.degrees / math.radians factors.
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kDegToRad = kPi / 180.0;

// Python's float `%`: the result takes the divisor's sign.
double py_mod(double x, double y) {
    double m = std::fmod(x, y);
    if (m != 0.0) {
        if ((y < 0.0) != (m < 0.0)) m += y;
    } else {
        m = std::copysign(0.0, y);
    }
    return m;
}

struct Oklch { double L, C, h; };

// An opaque sRGB triple -> OKLCH (L in 0..100, C in percent, h in degrees),
// Björn Ottosson's matrices.
Oklch oklch(const Rgb& c) {
    const double r = srgb_to_linear(c.r);
    const double g = srgb_to_linear(c.g);
    const double b = srgb_to_linear(c.b);
    const double l = cube_root(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = cube_root(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = cube_root(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const double L  = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    const double a  = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    return Oklch{L * 100.0, std::hypot(a, bb) * 100.0,
                 py_mod(std::atan2(bb, a) * kRadToDeg, 360.0)};
}

// OKLCH (oklch's units) -> an sRGB triple, each linear channel clipped,
// encoded, rounded and clamped.
Rgb from_oklch(double L, double C, double h) {
    L /= 100.0;
    C /= 100.0;
    const double a = C * std::cos(h * kDegToRad);
    const double b = C * std::sin(h * kDegToRad);
    const double l = std::pow(L + 0.3963377774 * a + 0.2158037573 * b, 3.0);
    const double m = std::pow(L - 0.1055613458 * a - 0.0638541728 * b, 3.0);
    const double s = std::pow(L - 0.0894841775 * a - 1.2914855480 * b, 3.0);
    const double lr = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    const double lg = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    const double lb = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    return Rgb{clamp_byte(linear_to_srgb(lr)), clamp_byte(linear_to_srgb(lg)),
               clamp_byte(linear_to_srgb(lb))};
}

std::string hex_word(const Rgb& c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

// THE SHADE (immaterial_mod, the head): `word` with its OKLCH lightness set
// to `lightness`, its hue kept, its chroma scaled by lightness / its own; an
// uppercase 6-digit hex word, or nothing for a black word (no ratio).
std::optional<std::string> shade(const std::string& word, double lightness) {
    const Oklch o = oklch(opaque(word));
    if (o.L == 0.0) return std::nullopt;
    return hex_word(from_oklch(lightness, o.C * lightness / o.L, o.h));
}

uint32_t packed(const Rgb& c) {
    return (static_cast<uint32_t>(c.r) << 16) |
           (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

// One theme's tones (the head's DERIVATION). Total: every tone is a role, a
// composite, a mix or a scaled role, and each of the two ratios' zero
// divisor has its own answer (the lit key's derived tones, below).
GuiThemeTones derive(const AskRoles& roles) {
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
    const Rgb vu_max   = opaque(roles.vu_maximum);
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
    // PAIR: the accent and the print on it, both scaled by the one ratio
    // the disabled key's ink bears to the text, mean over mean; a black
    // text (no divisor) leaves both as they are. THE RADIO KEYS' PRESS AND
    // INERT FACES (architect 2026-11-11, session 107): the purple by the
    // same two ratios.
    const double face_mean   = mean(face);
    const double press_ratio =
        face_mean == 0.0 ? 1.0 : mean(desktop) / face_mean;
    const double text_mean   = mean(text);
    const double dim_ratio   =
        text_mean == 0.0 ? 1.0 : mean(dim_ink) / text_mean;
    const Rgb lit_press  = scale(on, press_ratio);
    const Rgb lit_dim    = scale(on, dim_ratio);
    const Rgb on_lit_dim = scale(contrast, dim_ratio);
    const Rgb radio_press = scale(radio, press_ratio);
    const Rgb radio_dim   = scale(radio, dim_ratio);
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
    t.playhead_head       = packed(range);
    t.playhead_line       = packed(mix(white, range, 0.483));
    t.scanner             = packed(white);
    return t;
}

// THE GOLD'S LIGHTNESS (the head's IMMATERIAL_MOD): the OKLCH L of the
// FF91C3 its accent replaces, to the tenth.
constexpr double kImmaterialModLightness = 78.8;

// -- THE LIST ----------------------------------------------------------

std::vector<GuiTheme>& themes_storage() {
    static std::vector<GuiTheme> list{
        GuiTheme{std::string(kGuiDefaultThemeSlug),
                 std::string(kGuiCompiledThemeGroup), kGuiDefaultThemeTones}};
    return list;
}

// The one stderr line a left-out file or folder prints (the head).
void say_left_out(const std::filesystem::path& path, const std::string& why) {
    std::fprintf(stderr, "warptempo_gui: Theme '%s' left out: %s\n",
                 path.string().c_str(), why.c_str());
}

struct AskFile {
    std::string           slug;
    std::filesystem::path path;
    AskRoles              roles;
};
struct AskGroup {
    std::string          name;
    std::vector<AskFile> files;
};

// The group folders under `themes_dir`, by name; a folder whose name is no
// slug or is the compiled group's left out with its line; an unlistable or
// absent `themes/` none, silently.
std::vector<AskGroup> list_groups(const std::filesystem::path& themes_dir) {
    std::vector<AskGroup> groups;
    if (themes_dir.empty()) return groups;
    std::error_code ec;
    if (!std::filesystem::is_directory(themes_dir, ec)) return groups;
    std::filesystem::directory_iterator it(themes_dir, ec);
    if (ec) return groups;
    std::vector<std::filesystem::path> folders;
    for (const std::filesystem::directory_entry& e : it) {
        std::error_code fec;
        if (!e.is_directory(fec)) continue;
        folders.push_back(e.path());
    }
    std::sort(folders.begin(), folders.end(),
              [](const std::filesystem::path& a,
                 const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
              });
    for (const std::filesystem::path& folder : folders) {
        const std::string name = folder.filename().string();
        if (!is_theme_slug(name)) {
            say_left_out(folder, "a group folder's name is lowercase letters "
                                 "and digits joined by single '_'");
            continue;
        }
        if (name == kGuiCompiledThemeGroup) {
            say_left_out(folder, "the group name is the compiled group's");
            continue;
        }
        groups.push_back(AskGroup{name, {}});
    }
    return groups;
}

// One group's `.ask` files, by slug (then by file name), each read; a file
// left out with its line. An unlistable folder holds none, silently.
void read_group(AskGroup& group, const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::directory_iterator it(folder, ec);
    if (ec) return;
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    for (const std::filesystem::directory_entry& e : it) {
        std::error_code fec;
        if (!e.is_regular_file(fec)) continue;
        const std::string name = e.path().filename().string();
        if (name.size() <= 4 ||
            name.compare(name.size() - 4, 4, ".ask") != 0)
            continue;
        files.emplace_back(theme_slug_of_stem(
                               std::string_view(name).substr(0, name.size() - 4)),
                           e.path());
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
        std::expected<AskRoles, std::string> roles = read_ask_roles(path);
        if (!roles) {
            say_left_out(path, roles.error());
            continue;
        }
        group.files.push_back(AskFile{std::move(slug), std::move(path),
                                      std::move(*roles)});
    }
}

} // namespace

void load_gui_themes(const std::filesystem::path& themes_dir) {
    // The walk, the order and the left-out files are the head's
    // (theme_ask.h). Once per process (architect 2026-11-06, session 75):
    // the list is immutable after this returns, so a pointer into it stays
    // good (install_palette copies its row all the same, render.h).
    static bool loaded = false;
    assert(!loaded && "the themes are read once, at launch");
    loaded = true;
    std::vector<GuiTheme>& list = themes_storage();
    assert(list.size() == 1);

    std::vector<AskGroup> groups = list_groups(themes_dir);
    for (AskGroup& g : groups) read_group(g, themes_dir / g.name);

    // THE COMPILED GROUP'S SECOND ROW: immaterial_mod over the walk's first
    // `immaterial` file.
    for (const AskGroup& g : groups) {
        const auto it = std::find_if(
            g.files.begin(), g.files.end(),
            [](const AskFile& f) { return f.slug == "immaterial"; });
        if (it == g.files.end()) continue;
        AskRoles roles = it->roles;
        const std::optional<std::string> gold =
            shade(roles["SearchIndication"], kImmaterialModLightness);
        if (!gold) {
            say_left_out(it->path, "no immaterial_mod: its SearchIndication "
                                   "is black");
        } else {
            roles["ViewCheckControlEnabledOn"] = *gold;
            list.push_back(GuiTheme{std::string(kGuiImmaterialModSlug),
                                    std::string(kGuiCompiledThemeGroup),
                                    derive(roles)});
        }
        break;
    }

    // THE FOLDERS' ROWS, the compiled group's two slugs reserved.
    std::vector<std::string> taken{std::string(kGuiDefaultThemeSlug),
                                   std::string(kGuiImmaterialModSlug)};
    for (const AskGroup& g : groups) {
        for (const AskFile& f : g.files) {
            if (std::find(taken.begin(), taken.end(), f.slug) != taken.end()) {
                say_left_out(f.path, "its slug '" + f.slug + "' is taken");
                continue;
            }
            taken.push_back(f.slug);
            list.push_back(GuiTheme{f.slug, g.name, derive(f.roles)});
        }
    }
}

const std::vector<GuiTheme>& gui_themes() { return themes_storage(); }

const GuiTheme* find_gui_theme(std::string_view slug) {
    for (const GuiTheme& t : themes_storage())
        if (t.slug == slug) return &t;
    return nullptr;
}

const GuiTheme& default_gui_theme() { return themes_storage().front(); }
