#pragma once

#include "render.h"         // GuiPalette, GuiColor: the role table's members
#include "theme_tables.h"   // GuiThemeTones, kGuiThemes: the words

#include <cstddef>
#include <cstdint>
#include <iterator>

// THE PALETTE IS ONE PER THEME, THE `theme` KEY'S, AND ITS NUMBERS ARE
// ABLETON LIVE'S OWN (architect 2026-11-05, the theme arc — "i'd like to
// implement all" — reopening 2026-11-01's one-palette ruling on his word:
// "Agree, let's drop them. We keep the canvas grammar" — "We've taken the
// design, the layout from CoolEdit, and I think that's enough"): Live 12's
// own themes, whole — chrome and well together — selectable through ONE
// device key, `theme` (device_config.h), its value a slug of the generated
// table kGuiThemes (theme_tables.h, written by tools/themes/ask_to_tables.py
// from Live's .ask files, which never enter the repository). No per-color
// key, no Settings row for a color, no theme file read at launch: the themes
// are compiled in. Cool Edit Pro 2.1 keeps THE CANVAS GRAMMAR — the view
// bar, the ruler, the marker lane, the grid and center lines, the cues, the
// playhead, the stereo divider and their lengths (program_spec.h) — and
// gives no color. THE DEFAULT ROW (`default`, Live's "Default Dark Neutral
// Medium") IS THE 2026-11-01..05 MEASURED PALETTE TO THE BYTE — the numbers
// MEASURED OFF HIS OWN SHOTS OF LIVE 12's default theme (2026-11-01: the
// canvas, the ink, the zero line, the grid's one-px equivalent, the
// playhead's triangle, the orange of Live's markers and lit buttons;
// 2026-11-02 the grey of its inactive marker; 2026-11-04 its dark line) and
// his own picks (the core, 2026-11-04 night, and the playhead line's static
// tone, 2026-11-04 night: white 0.483 toward the head, every other theme
// derived at the same ratio, ruling 4 of 2026-11-05) — the static_asserts
// at the foot are that proof in the build, every one of the thirty words
// with its literal.
//
// TWO TABLES FOR ONE STRUCT: THE PALETTE (kGuiPaletteRoles below) colors the
// well, and THE CHROME'S ROLE TABLE (kGuiThemeRoles, theme_file.h) every
// chrome role; both map a GuiPalette member to a GuiThemeTones member.
// GuiPalette (render.h) keeps every member; install_palette (render.cpp)
// fills every member off the installed theme's row — the program members
// off this file's table, the panel's `face` off the theme's ground, the
// chrome members off theme_file.h's — so every painter reads palette() and
// no painter knows which table a color came from, nor which theme.
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// flag editor's field and selection band, the chrome's selected pair). So
// the palette's program roles are exactly the ELEVEN below (TWO ROLES MAY
// SHARE A NUMBER — the ink and `center` share Default's 191919: a role is
// what it paints, not its value). Each role's Default word and its Live
// source (the derivation, theme_tables.h's head):
//   waveform_canvas  393939  the canvas, Live's darker band (DetailGridTiles
//                            over DetailViewBackground)
//   waveform_ink     191919  the waveform, Live's — the dark lamp's bar and
//                            the lit lamp's outer bar (WaveformColor over
//                            DetailViewBackground)
//   waveform_core    1E1E1E  THE CORE (architect 2026-11-04, mock f1; its
//                            tone ruled 1E1E1E the same night, his pick i1
//                            of tmp/s38 over i0, session 33's 1C1C1C — NOT
//                            a measured Live tone: his pick on the real lit
//                            capture, on his glass; every other theme the
//                            same mix, ink 5/32 toward canvas): the
//                            magnified picture's inner bar, a solid fill
//                            five levels over the ink ("the absence of it
//                            would indicate a quiet section"; render.h's
//                            THE CORE, where the tone's record is)
//   grid             313131  the quarter lines of each channel's half, the
//                            horizontals alone (render_canvas) — Live's
//                            antialiased beat line as one solid px on the
//                            canvas (canvas 1/4 toward ink)
//   center           191919  each channel's zero line over the waveform
//                            (render_center_lines) — Live's zero line IS the
//                            ink
//   cue              FFAD56  THE ENABLED MARKER'S FILL (architect
//                            2026-11-05, his pick a2 of tmp/s42): the chip
//                            of every enabled cue, selected or not, warp
//                            and phase reset alike, both halves of the `h`
//                            view's items (resolve_flag_face), the orange
//                            of Live's start marker
//                            (ViewCheckControlEnabledOn, the lit key's fill
//                            too)
//   cue_outline      111111  LIVE'S DARK LINE (architect 2026-11-04): the
//                            one-W outline round every chip, THE SELECTION
//                            STEM down the canvas (every selected enabled
//                            marker, architect 2026-11-05), the phase-reset
//                            lead-in ring and the nudged pairs' ring runs
//                            (render.h's THE STATES) (ControlContrastFrame)
//   inactive_cue     5D5D5D  THE DISABLED MARKER (architect 2026-11-02, the
//                            color sweep, mock C3; the solid chip since
//                            2026-11-05, "disabled marker gets dim label"):
//                            LIVE'S INACTIVE WARP MARKER, measured on its own
//                            303030 lane — ONE ROLE, no blend: a disabled
//                            chip's fill (in the `cue_outline`, no stem,
//                            resolve_flag_face) and every INERT LABEL
//                            — a disabled cue's segments and a tie
//                            follower's cells — and its strike bar. "The
//                            arrow needs to be legible for me, but the text
//                            doesn't … since it's disabled, it's inert"
//                            (architect 2026-10-10): Live's grey shows the
//                            marker exists and reads as off, the label with
//                            it (BackgroundClip)
//   playhead_head    03C3D5  THE PLAYHEAD'S HEAD, the triangle in the ruler,
//                            Live's playhead triangle (render.h's playhead
//                            paragraph), the head's alone (RangeDefault)
//   playhead_line    85E2EB  THE PLAYHEAD'S LINE IN THE CANVAS (architect
//                            2026-11-04 night, mock n1, his pick of four
//                            static tones after the measured cost of a
//                            fading line): solid, one W, opaque, ONE STATIC
//                            TONE — white 0.483 toward the head
//                            (render_playhead's paragraph; ruling 4,
//                            2026-11-05: the same ratio in every theme)
//   scanner          FFFFFF  THE SCANNER AND THE ZOOM ANCHOR'S STEM, the
//                            white of the stems that belong to the controls
//                            (architect 2026-10-09: "the non-playhead stems
//                            that are related to the controls should be
//                            white"; kept 2026-11-01; white in every theme)
// The stereo divider between the channels wears the panel's tones; nothing
// else in the well has a role. THE LIT KEY'S FILL is the chrome's
// (`lit_fill`, theme_file.h), the same Live role as `cue`.
// THE BYTES ARE sRGB (architect 2026-10-08 ~05:15), as every authored
// color's, and the tablet converts them at the painter's entry
// (display_transform.h's head).

// THE ROLE TABLE — THE ONE ENUMERATION of the program's roles, in
// GuiPalette's order: the member each fills and the theme tone that fills it
// (the head's list). install_palette fills the struct off it and
// palette_file.cpp's coverage proof walks it, so a role cannot be valued and
// not painted.
struct GuiPaletteRole {
    GuiColor GuiPalette::*    member;
    uint32_t GuiThemeTones::* tone;
};
inline constexpr GuiPaletteRole kGuiPaletteRoles[] = {
    {&GuiPalette::waveform_canvas,  &GuiThemeTones::waveform_canvas},
    {&GuiPalette::waveform_ink,     &GuiThemeTones::waveform_ink},
    {&GuiPalette::waveform_core,    &GuiThemeTones::waveform_core},
    {&GuiPalette::grid,             &GuiThemeTones::grid},
    {&GuiPalette::center,           &GuiThemeTones::center},
    {&GuiPalette::cue,              &GuiThemeTones::cue},
    {&GuiPalette::cue_outline,      &GuiThemeTones::cue_outline},
    {&GuiPalette::inactive_cue,     &GuiThemeTones::inactive_cue},
    {&GuiPalette::playhead_head,    &GuiThemeTones::playhead_head},
    {&GuiPalette::playhead_line,    &GuiThemeTones::playhead_line},
    {&GuiPalette::scanner,          &GuiThemeTones::scanner},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);

// THE DEFAULT PROOF (the head): the generated `default` row reproduces the
// measured palette of 2026-11-01..05 to the byte — every one of the thirty
// words with its literal, so a change to the tool's derivation that moved
// one of them is a build failure here. (The literals are the record; the
// generated header holds the same numbers and is never hand-edited.)
namespace gui_default_theme_proof {
inline constexpr GuiThemeTones kDefault = kGuiThemes[kGuiDefaultTheme].tones;
// THE CHROME (materials.h's head: the ground, the text tones, the key, the
// well's tones, the selection pair, the panes, the cards).
static_assert(kDefault.ground          == 0x242424);
static_assert(kDefault.text            == 0xB5B5B5);
static_assert(kDefault.text_dim        == 0x757575);
static_assert(kDefault.text_on_lit     == 0x111111);
static_assert(kDefault.key_dim_ink     == 0x5A5A5A);
static_assert(kDefault.key_face        == 0x363636);
static_assert(kDefault.lit_fill        == 0xFFAD56);
static_assert(kDefault.well_frame      == 0x757575);
static_assert(kDefault.ruler_face      == 0x363636);
static_assert(kDefault.ruler_tick      == 0x6B6B6B);
static_assert(kDefault.lane_face       == 0x2A2A2A);
static_assert(kDefault.trim_bar_face   == 0x2A2A2A);
static_assert(kDefault.trim_span_face  == 0x6A6A6A);
static_assert(kDefault.selection_fill  == 0x637E86);
static_assert(kDefault.selection_text  == 0x111111);
static_assert(kDefault.pane_face       == 0x363636);
static_assert(kDefault.score_pane_face == 0x191919);
static_assert(kDefault.card_face       == 0xE76942);
static_assert(kDefault.card_ink        == 0x2A2A2A);
// THE PALETTE (the head's eleven).
static_assert(kDefault.waveform_canvas == 0x393939);
static_assert(kDefault.waveform_ink    == 0x191919);
static_assert(kDefault.waveform_core   == 0x1E1E1E);
static_assert(kDefault.grid            == 0x313131);
static_assert(kDefault.center          == 0x191919);
static_assert(kDefault.cue             == 0xFFAD56);
static_assert(kDefault.cue_outline     == 0x111111);
static_assert(kDefault.inactive_cue    == 0x5D5D5D);
static_assert(kDefault.playhead_head   == 0x03C3D5);
static_assert(kDefault.playhead_line   == 0x85E2EB);
static_assert(kDefault.scanner         == 0xFFFFFF);
} // namespace gui_default_theme_proof
