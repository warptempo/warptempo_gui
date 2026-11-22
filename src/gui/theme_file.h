#pragma once

#include "render.h"         // GuiPalette, GuiColor: the role table's members;
                            // through gui_font.h, chrome_spec.h's ChromeSpec
#include "gui_theme.h"      // GuiThemeTones: the role table's tones

#include <cstddef>
#include <cstdint>
#include <iterator>

// THE CHROME'S COLORS (architect 2026-10-04; COMPILED IN 2026-10-08;
// INHERITED FROM COOL EDIT 2026-10-10; OFF THE MATERIALS 2026-10-21; THE
// INSTALLED THEME'S since 2026-11-05, a theme file read at launch since
// 2026-11-11, theme_roles.h) — every color the flat
// CHROME paints, as ROLES. THE PROGRAM'S COLORS ARE THE PALETTE's (the
// waveform, the flags, the playhead's line and the scanner; palette_file.h,
// whose head owns the rule that parts the two) — the two tables fill ONE
// struct off THE theme row (GuiThemeTones, gui_theme.h — the one theme's,
// Arc-Dark's, theme_roles.h; install_palette, render.cpp).
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
// constants), installed at gui_main's launch. THE CAPTION HAS NO ROLE: the app paints none since
// 2026-10-24 (render.h's caption note); its six roles — the active and
// inactive title's start, gradient end and text, a scheme's keys — were
// removed 2026-10-21 with the 42 built-in captions and the `caption` key
// (palette_file.h's head).
//
// THE ROLES are the role table below — THE ONE ENUMERATION of the chrome's:
// its name, the GuiPalette field it fills (render.h, whose palette block
// owns what each role paints) and the theme tone that fills it. The first
// six are Windows' own element names (ButtonFace, ButtonText, Hilight /
// HilightText, WindowText and InfoText; InfoWindow and the cards' ring left
// with the cards' orange, send AA's stage D1, 2026-11-19), the chrome's
// painters written against them; the TWENTY-NINE after them
// are the flat look's tones (materials.h's head, 2026-11-01..05: the
// look's own, the lamp arc's clip lamp of 2026-11-05 night, the lit key's
// inert face of 2026-11-11, session 106, `lit_dim_fill`, the trough and
// the well's frame of 2026-11-17, session
// 140, `trim_bar_face` and `well_border`, and THE ARC PAGE'S SIX of
// 2026-11-19, send Z: the body tone, the title bar's text, the sidebar
// text, the ruler's digits and the disabled key's face and border; THE
// SCALE'S RIM, send AA's stage D3; THE FLOATERS' FOUR the same day, send AA's
// stage D1: the tooltip's,
// the normal card's and the standing card's carried fills and the limiter
// lamp; THE TWO HOVER TONES of send CC, 2026-11-20, `key_hover_face`
// and `trim_span_hover_face` — the second the trim bar's hovered part,
// render_trim_flags through trim_bar_tones; AND THE LIST ROW'S HOVER of
// 2026-11-21, `list_hover_face`, paint_list_view; AND A HOVERED DISABLED
// MARKER'S LABEL of send FF, 2026-11-21, `inactive_cue_label_hover`, the
// inert label's `text_dim` under the hovering pen — a text tone, the
// chrome's as the label's rest is, its chip's hover the palette's
// `inactive_cue_hover`), members since the
// theme arc so every painter reads palette() —
// the values are no longer Windows' and no longer constants. A ROLE STANDS
// IFF A PAINTER READS IT (the relief quartet, the field ground and the
// clock pair retired 2026-11-02 with the overlays' flat pass). THE LIT KEY
// (architect 2026-11-11, session 106; Arc's lit button since 2026-11-19,
// send Z, materials.h's THE KEY): ITS FACE HAS NO ROLE HERE —
// paint_linked_group reads the palette's `cue`, THE ACCENT, a program role
// (the markers' chip wears the same word as `cue_fill`, 2026-11-13, session
// 119), so no `lit_fill` stands beside it holding the same tone; its inert
// face is `lit_dim_fill` above; WHAT IS PRINTED ON IT has none either — the
// glyph road takes the theme row's WORD, so `text_on_lit` and
// `text_on_lit_dim` are read by key_ink off installed_theme(), as `text` and
// `key_dim_ink` are for a glyph. No door class and no radio class: every
// checked key lights in the one accent.
// TWO ROLES
// MAY SHARE A TONE — a
// role is what it paints, not its value: `label`, `field_text` and `text`
// all read the theme's text; `card_fill` and `card_standing_fill` the same
// Alert on every Live theme.
// EVERY BYTE HERE IS AN sRGB RECORD (architect 2026-10-08 ~05:15), as the
// captures show them; the tablet converts each at the painter's entry
// (display_transform.h's head).

// THE ROLE TABLE — THE ONE ENUMERATION OF THE CHROME'S ROLES (architect
// 2026-10-04), in GuiPalette's order: the chrome's thirty-five (the six
// of 2026-10-04's eight left — the caption's six, 2026-10-05..10-21, left
// with the captions; the seven unread ones, 2026-11-02, with the overlays'
// rings; the cards' ground and ring, send AA's stage D1 — and the
// twenty-nine flat-look tones, the two lamps, the lit key's inert face,
// the trough, the well's frame, the Arc page's six, the floaters' three
// carried fills, the progress trough, the scale's rim, the two hover
// tones, the list row's hover and a hovered disabled marker's label among
// them). The
// program's roles are the palette's own table (kGuiPaletteRoles, palette_file.h). install_palette
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
    // THE CHROME'S SIX (Windows' element names).
    {"ground",          &GuiPalette::ground,          &GuiThemeTones::ground},
    {"label",           &GuiPalette::label,           &GuiThemeTones::text},
    {"selected_fill",   &GuiPalette::selected_fill,   &GuiThemeTones::selection_fill},
    {"selected_text",   &GuiPalette::selected_text,   &GuiThemeTones::selection_text},
    {"field_text",      &GuiPalette::field_text,      &GuiThemeTones::text},
    {"card_text",       &GuiPalette::card_text,       &GuiThemeTones::card_ink},
    // THE FLAT LOOK'S TWENTY-NINE (materials.h's head: the text tones, the
    // key, the lit key's inert face, the well's tones, the score viewer's
    // pane, the two lamps; THE PROGRESS TROUGH, send AA's stage D2,
    // 2026-11-19 — the progress bar's and the scrub's trough; THE SCALE'S
    // RIM, send AA's stage D3, the scrub's knob; THE TWO HOVER TONES, send
    // CC, 2026-11-20 — a key's face and the trim bar's pill under the
    // hovering pen; THE LIST ROW'S HOVER, architect 2026-11-21 — Arc's row
    // hover, 3 % white over the list's view; A HOVERED DISABLED MARKER'S
    // LABEL, send FF, 2026-11-21 — the inert label's ink under the hovering
    // pen; THE ARC PAGE'S SIX, architect
    // 2026-11-19, send Z, theme_roles.h's head — the body tone, the title
    // bar's text, the sidebar text, the ruler's digits, the disabled key's
    // face and border; and THE FLOATERS' FOUR, send AA's stage D1 the same
    // day — the three carried fills, each the color
    // of its row word, its opacity that word's top byte (tone_alpha,
    // render.h), and the limiter lamp, its own tone off Alert since the
    // cards left it).
    {"body_face",       &GuiPalette::body_face,       &GuiThemeTones::body_face},
    {"text",            &GuiPalette::text,            &GuiThemeTones::text},
    {"title_text",      &GuiPalette::title_text,      &GuiThemeTones::title_text},
    {"sidebar_text",    &GuiPalette::sidebar_text,    &GuiThemeTones::sidebar_text},
    {"text_dim",        &GuiPalette::text_dim,        &GuiThemeTones::text_dim},
    {"inactive_cue_label_hover", &GuiPalette::inactive_cue_label_hover, &GuiThemeTones::inactive_cue_label_hover},
    {"key_dim_ink",     &GuiPalette::key_dim_ink,     &GuiThemeTones::key_dim_ink},
    {"key_dim_face",    &GuiPalette::key_dim_face,    &GuiThemeTones::key_dim_face},
    {"key_dim_border",  &GuiPalette::key_dim_border,  &GuiThemeTones::key_dim_border},
    {"key_face",        &GuiPalette::key_face,        &GuiThemeTones::key_face},
    {"key_hover_face",  &GuiPalette::key_hover_face,  &GuiThemeTones::key_hover_face},
    {"list_hover_face", &GuiPalette::list_hover_face, &GuiThemeTones::list_hover_face},
    {"lit_dim_fill",    &GuiPalette::lit_dim_fill,    &GuiThemeTones::lit_dim_fill},
    {"ruler_face",      &GuiPalette::ruler_face,      &GuiThemeTones::ruler_face},
    {"ruler_tick",      &GuiPalette::ruler_tick,      &GuiThemeTones::ruler_tick},
    {"ruler_text",      &GuiPalette::ruler_text,      &GuiThemeTones::ruler_text},
    {"lane_face",       &GuiPalette::lane_face,       &GuiThemeTones::lane_face},
    {"trim_bar_face",   &GuiPalette::trim_bar_face,   &GuiThemeTones::trim_bar_face},
    {"trim_span_face",  &GuiPalette::trim_span_face,  &GuiThemeTones::trim_span_face},
    {"trim_span_hover_face", &GuiPalette::trim_span_hover_face, &GuiThemeTones::trim_span_hover_face},
    {"well_border",     &GuiPalette::well_border,     &GuiThemeTones::well_border},
    {"score_pane_face", &GuiPalette::score_pane_face, &GuiThemeTones::score_pane_face},
    {"scale_trough",    &GuiPalette::scale_trough,    &GuiThemeTones::scale_trough},
    {"scale_slider_border", &GuiPalette::scale_slider_border, &GuiThemeTones::scale_slider_border},
    {"tooltip_fill",    &GuiPalette::tooltip_fill,    &GuiThemeTones::tooltip_fill},
    {"card_fill",       &GuiPalette::card_fill,       &GuiThemeTones::card_fill},
    {"card_standing_fill", &GuiPalette::card_standing_fill, &GuiThemeTones::card_standing_fill},
    {"limiter_lamp",    &GuiPalette::limiter_lamp,    &GuiThemeTones::limiter_lamp},
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
// 2026-10-14. No key spells a color: the one theme's file is read at
// launch, theme_roles.h's load_gui_theme.)
