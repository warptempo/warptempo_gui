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

// The format, the groups, the order, the left-out files, THE DERIVATION and
// THE BRIGHT MIX are at the declaration's head (theme_roles.h), the one
// owner; this file is their code.

namespace {

// THE ROLE TABLE: the roles a theme file holds, each exactly once — EXACTLY
// THE ROLES A TONE READS (the head's DERIVATION), Live's names, the one
// nested role flattened to `StandardVuMeter/Maximum` (the head's THE
// FORMAT), and last OUR OWN TEN (kOwnThemeRoles below). The converter
// (tools/themes/ask_to_theme.py) reads the names out of this initializer,
// so it is the one enumeration, and writes a file's lines in its order. Each was present in every stock and Sonic Bloom file
// when it joined (StandbySelectionForeground 2026-11-06;
// TakeLaneTrackNotHighlighted, ViewCheckControlDisabledOff,
// ScrollbarInnerHandle and DetailViewRulerMarkings, the grid, the disabled
// key's ink, the trim span and the ruler's ticks, 2026-11-07;
// ScrollbarInnerTrack, THE TRIM BAR'S TROUGH, and ClipBorder,
// THE WELL'S FRAME AND ITS LINES, 2026-11-17, architect 2026-11-17, session
// 140 — each added by hand to every shipped file from that file's own .ask,
// a `_mod` file from its original's); WindowBackground, TitleForeground and
// SidebarForeground, OUR OWN, 2026-11-19 (architect, send Z, the Arc page;
// kOwnThemeRoles below), and TooltipBackground, CardBackground and
// CardStandingBackground, OUR OWN THREE FILLS CARRYING THEIR ALPHA, the same
// day (send AA's stage D1: Arc's tooltip and notifyd's two bubbles), and
// ScaleTrough, OUR OWN ONE MORE, the same day (send AA's stage D2: Arc's
// progress / scale trough), and ScaleSliderBorder, the same day (send AA's
// stage D3: the rim of Arc's scale's knob, the scrub), and
// ButtonHoverBackground and ScrollbarSliderHover, OUR OWN TWO FOR ARC'S HOVER
// (architect 2026-11-18 / 2026-11-20, send CC: a key's face under the
// hovering pen and the trim bar's pill under it). A role no tone reads is in
// no file.
constexpr std::string_view kThemeRoles[] = {
    "SurfaceArea", "Desktop", "SurfaceBackground", "ControlContrastFrame",
    "ControlForeground", "TextDisabled", "BackgroundClip",
    "ViewCheckControlEnabledOn", "RangeDefault", "StandbySelectionBackground",
    "StandbySelectionForeground",
    "Alert", "WaveformColor", "DetailViewBackground", "DetailGridTiles",
    "TakeLaneTrackNotHighlighted", "ViewCheckControlDisabledOff",
    "ScrollbarInnerHandle", "DetailViewRulerMarkings",
    "StandardVuMeter/Maximum",
    "ScrollbarInnerTrack", "ClipBorder",
    "WindowBackground", "TitleForeground", "SidebarForeground",
    "TooltipBackground", "CardBackground", "CardStandingBackground",
    "ScaleTrough", "ScaleSliderBorder",
    "ButtonHoverBackground", "ScrollbarSliderHover",
};
constexpr std::size_t kThemeRoleCount = std::size(kThemeRoles);

constexpr std::string_view kThemeSuffix = ".theme";

constexpr std::size_t role_index(std::string_view name) {
    for (std::size_t i = 0; i < kThemeRoleCount; ++i)
        if (kThemeRoles[i] == name) return i;
    return kThemeRoleCount;
}

// OUR OWN ROLES (the head's THE FORMAT; architect 2026-11-19, send Z): the
// roles of the table that are no Live role, each with THE LIVE ROLE A FILE
// WITHOUT AN ARC VALUE COPIES — so a Live theme's line is its own existing
// value and changes no look (the three fills of send AA's stage D1 copy the
// tone each replaces: the tooltip's SurfaceBackground, the pane it wore, and the
// cards' Alert, today's card face — opaque, as every source word is; the
// two hover tones of send CC copy the tone each moves from, the key face's
// SurfaceBackground and the pill's ScrollbarInnerHandle, so no hover shows
// on a Live theme). The
// one enumeration of the pairs: the converter
// (tools/themes/ask_to_theme.py) reads this initializer too, writing each
// own role as its source role's word (an .ask holds none of them).
struct OwnThemeRole {
    std::string_view role;
    std::string_view source;
};
constexpr OwnThemeRole kOwnThemeRoles[] = {
    {"WindowBackground",  "SurfaceArea"},
    {"TitleForeground",   "ControlForeground"},
    {"SidebarForeground", "ControlForeground"},
    {"TooltipBackground", "SurfaceBackground"},
    {"CardBackground",    "Alert"},
    {"CardStandingBackground", "Alert"},
    {"ScaleTrough",       "ScrollbarInnerTrack"},
    {"ScaleSliderBorder", "ClipBorder"},
    {"ButtonHoverBackground", "SurfaceBackground"},
    {"ScrollbarSliderHover",  "ScrollbarInnerHandle"},
};
constexpr bool own_roles_in_table() {
    for (const OwnThemeRole& o : kOwnThemeRoles) {
        if (role_index(o.role) == kThemeRoleCount) return false;
        if (role_index(o.source) == kThemeRoleCount) return false;
        for (const OwnThemeRole& p : kOwnThemeRoles)
            if (p.role == o.source) return false;
    }
    return true;
}
static_assert(own_roles_in_table());

// One file's roles: each an uppercase hex word of 6 or 8 digits, in
// kThemeRoles' order.
struct ThemeRoles {
    std::string words[kThemeRoleCount];
    const std::string& operator[](std::string_view name) const {
        const std::size_t i = role_index(name);
        assert(i < kThemeRoleCount);
        return words[i];
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
        const std::size_t i = role_index(name);
        if (i == kThemeRoleCount)
            return fail("no role '" + std::string(name) + "'");
        if (found[i])
            return fail("the role '" + std::string(name) + "' a second time");
        std::string word(hex);
        for (char& c : word)
            if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
        roles.words[i] = std::move(word);
        found[i] = true;
    }
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

uint32_t packed(const Rgb& c) {
    return (static_cast<uint32_t>(c.r) << 16) |
           (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

// A CARRIED FILL'S WORD (the head's CARRIED block): the role's three bytes
// and, in the top byte, ITS OPACITY — an 8-digit word's own alpha byte,
// FF for a 6-digit one (opaque). The one road a role's alpha takes to the
// painter (tone_alpha, render.h, reads it back).
uint32_t packed_carried(const std::string& word) {
    const uint32_t alpha =
        word.size() == 8 ? static_cast<uint32_t>(hex_byte(word, 6)) : 0xFFu;
    return (alpha << 24) | packed(opaque(word));
}

// A row's word back to its three bytes: exactly the Rgb `packed` was given,
// so a tone recomputed off a row's words is the one derive() computed off
// the roles (fill_dependent_tones).
Rgb unpacked(uint32_t word) {
    return Rgb{static_cast<int>((word >> 16) & 0xFFu),
               static_cast<int>((word >> 8) & 0xFFu),
               static_cast<int>(word & 0xFFu)};
}

// THE BRIGHT TONES, the head's BRIGHT MIX block's six in the row's member
// order: the one enumeration bright_mixed_tones walks. A word's top byte —
// a CARRIED fill's opacity (the head's CARRIED block; the standing card's
// red is the one bright tone among them) — rides the mix untouched.
constexpr uint32_t GuiThemeTones::* kBrightTones[] = {
    &GuiThemeTones::selection_fill,
    &GuiThemeTones::card_standing_fill,
    &GuiThemeTones::limiter_lamp,
    &GuiThemeTones::clip_lamp,
    &GuiThemeTones::cue,
    &GuiThemeTones::playhead_head,
};

// THE TONES DERIVED FROM A ROW'S OWN WORDS (the head's DERIVED block: the
// lit key's inert face and the print on it, the enabled chip's fill and
// the playhead's line), ONE
// BODY for both roads: derive() fills them off the theme's own numbers,
// bright_mixed_tones off the mixed bright tones, so each formula is written
// once. Every input is a word of the row — the body tone
// (WindowBackground, never mixed), the print on a lit face
// (StandbySelectionForeground, never mixed), the accent and the head —
// each the same bytes derive() reads off the roles, so the doubles
// and their order are the derivation's own (Python's, bit for bit).
void fill_dependent_tones(GuiThemeTones& t) {
    const Rgb body     = unpacked(t.body_face);
    const Rgb print    = unpacked(t.text_on_lit);
    const Rgb on       = unpacked(t.cue);
    const Rgb range    = unpacked(t.playhead_head);
    const Rgb white    = Rgb{255, 255, 255};
    // THE LIT-BUT-INERT PAIRS ON ARC'S OWN NUMBERS (the head's DERIVED
    // block; architect 2026-11-19, send Z: "let's do arcs numbers"): the
    // face (the lit color at 75 % over the body tone) at 60 % over the body
    // tone, mix(body, mix(body, color, 0.75), 0.6), the inner word rounded;
    // the print on it DIMMED WITH THE BUTTON, as GTK's `opacity` dims the
    // label (send Z's stage B, 2026-11-19, correcting stage A's kit reading):
    // (the print on a lit face at 80 % over the color-at-75 % face) at 60 %
    // over the body tone, mix(body, mix(mix(body, color, 0.75), print, 0.8),
    // 0.6), each inner word rounded.
    const Rgb lit_75    = mix(body, on, 0.75);
    t.lit_dim_fill          = packed(mix(body, lit_75, 0.6));
    t.text_on_lit_dim       = packed(mix(body, mix(lit_75, print, 0.8), 0.6));
    // The enabled chip wears the accent (the head's ROLE-NAMED, cue).
    t.cue_fill             = packed(on);
    // The playhead's line, white exactly half way to the head (the head's
    // DERIVED block).
    t.playhead_line        = packed(mix(white, range, 0.5));
}

// One theme's tones AT ITS OWN NUMBERS (the head's DERIVATION; the bright
// mix is the install's, bright_mixed_tones). Total: every tone is a role, a
// composite or a mix (fill_dependent_tones).
GuiThemeTones derive(const ThemeRoles& roles) {
    const Rgb ground   = opaque(roles["SurfaceArea"]);
    const Rgb body     = opaque(roles["WindowBackground"]);
    const Rgb title_fg = opaque(roles["TitleForeground"]);
    const Rgb sidebar_fg = opaque(roles["SidebarForeground"]);
    const Rgb desktop  = opaque(roles["Desktop"]);
    const Rgb face     = opaque(roles["SurfaceBackground"]);
    const Rgb contrast = opaque(roles["ControlContrastFrame"]);
    const Rgb text     = opaque(roles["ControlForeground"]);
    const Rgb dim      = opaque(roles["TextDisabled"]);
    const Rgb clip     = opaque(roles["BackgroundClip"]);
    const Rgb on       = opaque(roles["ViewCheckControlEnabledOn"]);
    const Rgb range    = opaque(roles["RangeDefault"]);
    const Rgb standby  = opaque(roles["StandbySelectionBackground"]);
    const Rgb standby_text = opaque(roles["StandbySelectionForeground"]);
    const Rgb alert    = opaque(roles["Alert"]);
    const Rgb vu_max   = opaque(roles["StandardVuMeter/Maximum"]);
    const Rgba face_a  = rgba(roles["SurfaceBackground"]);
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
    // THE PILL UNDER THE HOVERING PEN (send CC, 2026-11-20; the head's OUR
    // OWN, ARC'S HOVER): our own ScrollbarSliderHover over the surface the
    // pill's own role is composited over, Desktop — a Live theme's
    // ScrollbarInnerHandle word, so the pill's own tone there.
    const Rgb span_hover = over(rgba(roles["ScrollbarSliderHover"]),
                                rgba(roles["Desktop"]));
    // The ruler's ticks over the ruler they stand on, the canvas (architect
    // 2026-11-17, session 140, ruling 3: the ruler wears the canvas's tone).
    const Rgb tick_role  = over(rgba(roles["DetailViewRulerMarkings"]), canvas_a);
    // The trim bar's trough and the well's lines, each Live's role over the
    // ground they stand on (the head's COMPOSITED block; architect
    // 2026-11-17, session 140).
    const Rgba ground_a  = rgba(roles["SurfaceArea"]);
    const Rgb trough     = over(rgba(roles["ScrollbarInnerTrack"]), ground_a);
    const Rgb border     = over(rgba(roles["ClipBorder"]), ground_a);
    // The app's own tones over the body tone and the ruler (the head's
    // DERIVED block; architect 2026-11-19, send Z, the Arc page): THE
    // RULER'S DIGITS the text at 80 % over the ruler (Arc's column header);
    // THE DISABLED KEY'S FACE the key face at 55 % over the body tone and
    // ITS BORDER the well's border at 55 % over it (Arc's button:disabled).
    const Rgb ruler_fg   = mix(canvas, text, 0.8);
    const Rgb dim_face   = mix(body, face, 0.55);
    const Rgb dim_border = mix(body, border, 0.55);
    const Rgb white    = Rgb{255, 255, 255};
    GuiThemeTones t{};
    t.ground              = packed(ground);
    t.body_face           = packed(body);
    t.text                = packed(text);
    t.title_text          = packed(title_fg);
    t.sidebar_text        = packed(sidebar_fg);
    t.text_dim            = packed(dim);
    t.text_on_lit         = packed(standby_text);
    t.key_dim_ink         = packed(dim_ink);
    t.key_dim_face        = packed(dim_face);
    t.key_dim_border      = packed(dim_border);
    t.key_face            = packed(face);
    // A KEY'S FACE UNDER THE HOVERING PEN (send CC, 2026-11-20; the head's
    // OUR OWN, ARC'S HOVER): our own ButtonHoverBackground, read flat as the
    // key face's own role is (arc_dark 505666, Arc's button:hover; a Live
    // theme's SurfaceBackground, the key face itself: no hover shows there).
    t.key_hover_face      = packed(opaque(roles["ButtonHoverBackground"]));
    t.ruler_face          = packed(canvas);
    t.ruler_tick          = packed(tick_role);
    t.ruler_text          = packed(ruler_fg);
    t.lane_face           = packed(desktop);
    t.trim_bar_face       = packed(trough);
    t.trim_span_face      = packed(span_role);
    t.trim_span_hover_face = packed(span_hover);
    t.well_border         = packed(border);
    t.selection_fill      = packed(standby);
    t.selection_text      = packed(standby_text);
    t.score_pane_face     = packed(ink);
    // ARC'S PROGRESS / SCALE TROUGH (send AA's stage D2, 2026-11-19): our
    // own ScaleTrough over the ground (2D303B; a Live theme's
    // ScrollbarInnerTrack, so its trim trough's tone), the progress page's
    // bar and the scrub's trough.
    t.scale_trough        = packed(over(rgba(roles["ScaleTrough"]), ground_a));
    // ARC'S SLIDER BORDER (send AA's stage D3, 2026-11-19: the scrub's knob,
    // render.h's scrub block): our own ScaleSliderBorder over the ground
    // (262933; a Live theme's ClipBorder, so its well_border's tone).
    t.scale_slider_border = packed(over(rgba(roles["ScaleSliderBorder"]), ground_a));
    // THE THREE CARRIED FILLS (the head's CARRIED block), each its role's
    // bytes with its opacity in the top byte, painted at that opacity over
    // whatever lies under the box (paint_shift_tooltip,
    // paint_notifications).
    t.tooltip_fill        = packed_carried(roles["TooltipBackground"]);
    t.card_fill           = packed_carried(roles["CardBackground"]);
    t.card_standing_fill  = packed_carried(roles["CardStandingBackground"]);
    t.card_ink            = packed(standby_text);
    // THE LIMITER LAMP, ITS OWN TONE OFF Alert (send AA's stage D1: the
    // cards left Alert for their own fills, the lamp keeps Arc's warning
    // orange; paint_title_bar's lamps).
    t.limiter_lamp        = packed(alert);
    t.clip_lamp           = packed(vu_max);
    t.waveform_canvas     = packed(canvas);
    t.waveform_ink        = packed(ink);
    t.waveform_core       = packed(core);
    t.grid                = packed(grid_role);
    t.center              = packed(ink);
    t.cue                 = packed(on);
    t.cue_outline         = packed(contrast);
    t.inactive_cue        = packed(clip);
    t.playhead_head       = packed(range);
    t.scanner             = packed(white);
    fill_dependent_tones(t);
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

GuiThemeTones bright_mixed_tones(const GuiThemeTones& own, int percent) {
    // The rule, the six and the number are the head's BRIGHT MIX block
    // (theme_roles.h). Each bright tone moved toward the row's ground keeping
    // `percent` of itself — mix(tone, ground, 1 − percent / 100), the
    // derivation's own mix — then every tone derived from one recomputed off
    // the mixed words (fill_dependent_tones, the one body). At 100 the mix
    // moves no byte (a + 0 · (b − a) is a), so the row comes back as it is.
    assert(percent >= 0 && percent <= 100);
    GuiThemeTones t = own;
    const Rgb ground = unpacked(own.ground);
    const double toward_ground = 1.0 - percent / 100.0;
    for (uint32_t GuiThemeTones::* m : kBrightTones)
        t.*m = (own.*m & 0xFF000000u) |
               packed(mix(unpacked(own.*m), ground, toward_ground));
    fill_dependent_tones(t);
    return t;
}
