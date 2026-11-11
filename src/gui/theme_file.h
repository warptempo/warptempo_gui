#pragma once

#include "render.h"         // GuiPalette, GuiColor: the role table's members;
                            // through gui_font.h, chrome_spec.h's ChromeSpec
#include "gui_theme.h"      // GuiThemeTones: the role table's tones

#include <cstddef>
#include <cstdint>
#include <iterator>

// THE CHROME'S COLORS (architect 2026-10-04; COMPILED IN 2026-10-08;
// INHERITED FROM COOL EDIT 2026-10-10; OFF THE MATERIALS 2026-10-21; THE
// INSTALLED THEME'S since 2026-11-05, the theme read from Live's .ask files
// at launch since 2026-11-06, theme_ask.h) — every color the flat
// CHROME paints, as ROLES. THE PROGRAM'S COLORS ARE THE PALETTE's (the
// waveform, the flags, the playhead's line and the scanner; palette_file.h,
// whose head owns the rule that parts the two) — the two tables fill ONE
// struct off ONE theme row (GuiThemeTones, gui_theme.h — a row of the
// themes' list, theme_ask.h; install_palette, render.cpp).
//
// THE CHROME INHERITS COOL EDIT (architect 2026-10-10: "the chrome color
// should just be the Cool Edit theme color … we're getting rid of chrome
// inside the body … even the font color, all of that would be inherited
// from Cool Edit"; "whatever luminous rules we previously had are
// superseded by the Cool Edit follow") — SINCE 2026-11-01 THE FLAT LOOK'S
// (materials.h's head) — and SINCE 2026-11-05 THE THEME'S: every chrome role
// is one member of the installed theme's tones, named per role in the table
// below (THE ONE OWNER of every chrome role's source; chrome_derive.h's
// per-role derivation off the flat look's constants retired with the
// constants), installed at every install — gui_main's launch and every
// `theme` commit. THE CAPTION HAS NO ROLE: the app paints none since
// 2026-10-24 (render.h's caption note); its six roles — the active and
// inactive title's start, gradient end and text, a scheme's keys — were
// removed 2026-10-21 with the 42 built-in captions and the `caption` key
// (palette_file.h's head).
//
// THE ROLES are the role table below — THE ONE ENUMERATION of the chrome's:
// its name, the GuiPalette field it fills (render.h, whose palette block
// owns what each role paints) and the theme tone that fills it. The first
// eight are Windows' own element names (ButtonFace, ButtonText, Hilight /
// HilightText, WindowText, InfoWindow / InfoText and the tooltip's border),
// the chrome's painters written against them; the TWENTY-ONE after them
// are the flat look's tones (materials.h's head, 2026-11-01..05: the
// fourteen of the look, the press arc's four, 2026-11-05 — the three of its
// first landing and the lit press fill of its second — and the lamp arc's
// three the same night: the doors' lit pair and the clip lamp), members
// since the theme arc so every painter reads palette() —
// the values are no longer Windows' and no longer constants. A ROLE STANDS
// IFF A PAINTER READS IT (the relief quartet, the field ground and the
// clock pair retired 2026-11-02 with the overlays' flat pass; the two level
// fills, `level_audio_fill` and `level_noise_fill`, retired 2026-11-10 on
// the architect's "monochrome on the front page" — the level case's fill is
// the lit key's `lit_fill`, paint_level_case, paint_handler.cpp). TWO ROLES
// MAY SHARE A TONE — a
// role is what it paints, not its value: `label`, `field_text` and `text`
// all read the theme's text; `card_ground` and `card_frame` its card face.
// EVERY BYTE HERE IS AN sRGB RECORD (architect 2026-10-08 ~05:15), as the
// captures show them; the tablet converts each at the painter's entry
// (display_transform.h's head).

// THE ROLE TABLE — THE ONE ENUMERATION OF THE CHROME'S ROLES (architect
// 2026-10-04), in GuiPalette's order: the chrome's twenty-nine (the eight of
// 2026-10-04 — the caption's six, 2026-10-05..10-21, left with the captions;
// the seven unread ones, 2026-11-02, with the overlays' rings — and the
// twenty-one flat-look tones, 2026-11-05, the press arc's four and the
// lamp arc's three among them). The program's roles are the
// palette's own table (kGuiPaletteRoles, palette_file.h). install_palette
// (render.cpp) walks it, filling each member off its tone, and
// palette_file.cpp's coverage proof walks it, so a role cannot be valued and
// not painted, nor a member left at construction state. EVERY CHROME ROLE IS
// DECIDED ONCE: each row names its tone (the build proof below: no row
// without one, no member twice — theme_file.cpp's unique-names proof too).
struct GuiThemeRole {
    const char*               name;
    GuiColor GuiPalette::*    member;
    uint32_t GuiThemeTones::* tone;
};
inline constexpr GuiThemeRole kGuiThemeRoles[] = {
    // THE CHROME'S EIGHT (Windows' element names).
    {"ground",          &GuiPalette::ground,          &GuiThemeTones::ground},
    {"label",           &GuiPalette::label,           &GuiThemeTones::text},
    {"selected_fill",   &GuiPalette::selected_fill,   &GuiThemeTones::selection_fill},
    {"selected_text",   &GuiPalette::selected_text,   &GuiThemeTones::selection_text},
    {"field_text",      &GuiPalette::field_text,      &GuiThemeTones::text},
    {"card_ground",     &GuiPalette::card_ground,     &GuiThemeTones::card_face},
    {"card_text",       &GuiPalette::card_text,       &GuiThemeTones::card_ink},
    {"card_frame",      &GuiPalette::card_frame,      &GuiThemeTones::card_face},
    // THE FLAT LOOK'S TWENTY-ONE (materials.h's head: the text tones, the
    // key, its press face, its lit press fill and the doors' lit pair, the
    // well's tones, the panes, the lists' press pair, the clip lamp).
    {"text",            &GuiPalette::text,            &GuiThemeTones::text},
    {"text_dim",        &GuiPalette::text_dim,        &GuiThemeTones::text_dim},
    {"text_on_lit",     &GuiPalette::text_on_lit,     &GuiThemeTones::text_on_lit},
    {"key_dim_ink",     &GuiPalette::key_dim_ink,     &GuiThemeTones::key_dim_ink},
    {"key_face",        &GuiPalette::key_face,        &GuiThemeTones::key_face},
    {"lit_fill",        &GuiPalette::lit_fill,        &GuiThemeTones::lit_fill},
    {"lit_press_fill",  &GuiPalette::lit_press_fill,  &GuiThemeTones::lit_press_fill},
    {"door_lit_fill",   &GuiPalette::door_lit_fill,   &GuiThemeTones::door_lit_fill},
    {"door_lit_press_fill", &GuiPalette::door_lit_press_fill, &GuiThemeTones::door_lit_press_fill},
    {"key_press_face",  &GuiPalette::key_press_face,  &GuiThemeTones::key_press_face},
    {"well_frame",      &GuiPalette::well_frame,      &GuiThemeTones::well_frame},
    {"ruler_face",      &GuiPalette::ruler_face,      &GuiThemeTones::ruler_face},
    {"ruler_tick",      &GuiPalette::ruler_tick,      &GuiThemeTones::ruler_tick},
    {"lane_face",       &GuiPalette::lane_face,       &GuiThemeTones::lane_face},
    {"trim_bar_face",   &GuiPalette::trim_bar_face,   &GuiThemeTones::trim_bar_face},
    {"trim_span_face",  &GuiPalette::trim_span_face,  &GuiThemeTones::trim_span_face},
    {"pane_face",       &GuiPalette::pane_face,       &GuiThemeTones::pane_face},
    {"score_pane_face", &GuiPalette::score_pane_face, &GuiThemeTones::score_pane_face},
    {"list_press_fill", &GuiPalette::list_press_fill, &GuiThemeTones::list_press_fill},
    {"list_press_text", &GuiPalette::list_press_text, &GuiThemeTones::list_press_text},
    {"clip_lamp",       &GuiPalette::clip_lamp,       &GuiThemeTones::clip_lamp},
};
inline constexpr std::size_t kGuiThemeRoleCount = std::size(kGuiThemeRoles);

// EVERY CHROME ROLE IS DECIDED EXACTLY ONCE (the head): every row names a
// tone, and no two rows fill one member.
constexpr bool every_chrome_role_decided() {
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i) {
        if (kGuiThemeRoles[i].tone == nullptr) return false;
        if (kGuiThemeRoles[i].member == nullptr) return false;
        for (std::size_t j = i + 1; j < kGuiThemeRoleCount; ++j)
            if (kGuiThemeRoles[i].member == kGuiThemeRoles[j].member)
                return false;
    }
    return true;
}
static_assert(every_chrome_role_decided());

// (THE ONE COLOUR GRAMMAR — `#rrggbb` or one of Windows' twenty always-solid
// colours by name — was the theme files' and then the preset files' value
// grammar, 2026-10-03..10-14, and left with the preset files, architect
// 2026-10-14. The `theme` key of 2026-11-05 names a row of the themes' list
// by slug — since 2026-11-06 the compiled group and the .ask files read at
// launch, theme_ask.h's find_gui_theme — and spells no color.)
