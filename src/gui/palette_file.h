#pragma once

#include "render.h"         // GuiPalette, GuiColor: the role table's members
#include "gui_theme.h"      // GuiThemeTones, kGuiReferenceThemeTones: the words

#include <cstddef>
#include <cstdint>
#include <iterator>

// THE PALETTE IS ONE PER THEME, THE `theme` KEY'S, AND ITS NUMBERS ARE
// ABLETON LIVE'S OWN (architect 2026-11-05, the theme arc — "i'd like to
// implement all" — reopening 2026-11-01's one-palette ruling on his word:
// "Agree, let's drop them. We keep the canvas grammar" — "We've taken the
// design, the layout from CoolEdit, and I think that's enough"): Live 12's
// own themes, whole — chrome and well together — selectable through ONE
// device key, `theme` (device_config.h), its value a slug of the themes'
// list (theme_roles.h): SINCE 2026-11-11 (architect, session 110) THE
// APP'S OWN THEME FILES, Live's raw role values in our keyed format, shipped
// in the APK (SYSTEM, EXTRA) or dropped beside the device config
// (USER-DEFINED), each through the one derivation (theme_roles.h's head),
// `default` Live's Default Dark Cool Medium. No per-color key, no Settings
// row for a color.
// Cool Edit Pro 2.1 keeps THE CANVAS GRAMMAR — the view
// bar, the ruler, the marker lane, the grid and center lines, the cues, the
// playhead, the stereo divider and their lengths (program_spec.h) — and
// gives no color. THE REFERENCE ROW (Live's Default, "Default Dark Neutral
// Medium", COMPILED: kGuiReferenceThemeTones, gui_theme.h, listed as
// `default_dark_neutral_medium`; no file ships it) IS THE 2026-11-01..05
// MEASURED PALETTE TO THE BYTE but the selection
// pair's text (070707 since 2026-11-06), the magnify core (363636, Live's
// SurfaceBackground, since 2026-11-07), the level fill, the lit-but-inert
// key's pair and the radio keys' three (2026-11-11) and the playhead's line
// (2026-11-12; all at the foot) — the numbers
// MEASURED OFF HIS OWN SHOTS OF LIVE 12's default theme (2026-11-01: the
// canvas, the ink, the zero line, the grid's one-px equivalent, the
// playhead's triangle, the orange of Live's markers and lit buttons;
// 2026-11-02 the grey of its inactive marker; 2026-11-04 its dark line) —
// AND THE PLAYHEAD LINE'S STATIC TONE, white half way to the head on every
// theme, the mean of Live's fade (81E1EA; architect 2026-11-12, session 113,
// theme_roles.h's DERIVED block) — the static_asserts
// at the foot are that proof in the build, every one of the forty
// words with its literal (the press arc's four, 2026-11-05, among them —
// the key press face, the list press pair and the lit press fill — the lamp
// arc's clip lamp the same night and the six of 2026-11-11: the level
// fill, the lit-but-inert key's face and its glyph, and the radio keys'
// three faces).
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
// source (the derivation, theme_roles.h's head):
//   waveform_canvas  393939  the canvas, Live's darker band (DetailGridTiles
//                            over DetailViewBackground)
//   waveform_ink     191919  the waveform, Live's — the dark lamp's bar and
//                            the lit lamp's outer bar (WaveformColor over
//                            DetailViewBackground)
//   waveform_core    363636  THE CORE (architect 2026-11-04, mock f1), Live's
//                            SurfaceBackground, THE KEY FACE, on every theme
//                            (architect 2026-11-07, session 83, his pick i4
//                            of tmp/s83 on real MAGNIFY-lit captures): the
//                            magnified picture's inner bar, a solid fill one
//                            step under Default's 393939 canvas, the center
//                            line's ink reading hard across it, ruled fine
//                            ("the absence of it would indicate a quiet
//                            section"; render.h's THE CORE, where the tone's
//                            record is: 1E1E1E, ControlBackground, before it)
//   grid             303030  the quarter lines of each channel's half, the
//                            horizontals alone (render_canvas) — Live's
//                            antialiased beat line as one solid px on the
//                            canvas: Live's TakeLaneTrackNotHighlighted over
//                            it, architect 2026-11-07)
//   center           191919  each channel's zero line over the waveform
//                            (render_center_lines) — Live's zero line IS the
//                            ink
//   cue              FFAD56  THE ENABLED MARKER'S FILL (architect
//                            2026-11-05, his pick a2 of tmp/s42): the chip
//                            of every enabled cue, selected or not, warp
//                            and phase reset alike, and the `h` view's
//                            added and changed items (resolve_flag_face),
//                            the orange
//                            of Live's start marker
//                            (ViewCheckControlEnabledOn) — THE ACCENT: A
//                            LIT KEY'S WHOLE FACE IS THIS ROLE TOO, read
//                            by the key's painter (paint_key, materials.h;
//                            architect 2026-11-11, session 106: one accent
//                            for the chip and the lamps), a radio key's
//                            but (the chrome's `radio_lit_fill`, session
//                            107)
//   cue_outline      111111  LIVE'S DARK LINE (architect 2026-11-04): the
//                            one-W outline round every chip, THE SELECTION
//                            STEM down the canvas (every selected marker,
//                            architect 2026-11-05; enabled or disabled,
//                            2026-11-11) and the phase-reset
//                            lead-in ring (render.h's THE STATES)
//                            (ControlContrastFrame)
//   inactive_cue     5D5D5D  THE DISABLED MARKER (architect 2026-11-02, the
//                            color sweep, mock C3; the solid chip since
//                            2026-11-05, "disabled marker gets dim label"):
//                            LIVE'S INACTIVE WARP MARKER, measured on its own
//                            303030 lane — ONE ROLE, no blend: a disabled
//                            chip's fill (in the `cue_outline`, the stem
//                            the selection's as on an enabled chip,
//                            resolve_flag_face) — the `h` view's removed
//                            items' too (2026-11-11) — and every INERT
//                            LABEL — a disabled cue's segments, a tie
//                            follower's cells and a removed history
//                            item's label — and its strike bar. "The
//                            arrow needs to be legible for me, but the text
//                            doesn't … since it's disabled, it's inert"
//                            (architect 2026-10-10): Live's grey shows the
//                            marker exists and reads as off, the label with
//                            it (BackgroundClip)
//   playhead_head    03C3D5  THE PLAYHEAD'S HEAD, the triangle in the ruler,
//                            Live's playhead triangle (render.h's playhead
//                            paragraph), the head's alone (RangeDefault)
//   playhead_line    81E1EA  THE PLAYHEAD'S LINE IN THE CANVAS (architect
//                            2026-11-04 night, mock n1, a static tone after
//                            the measured cost of a fading line): solid,
//                            one W, opaque, ONE STATIC TONE — white half
//                            way to the head, the mean of Live's fade, on
//                            every theme (architect 2026-11-12, session
//                            113; render_playhead's paragraph,
//                            theme_roles.h's DERIVED block)
//   scanner          FFFFFF  THE SCANNER AND THE ZOOM ANCHOR'S STEM, the
//                            white of the stems that belong to the controls
//                            (architect 2026-10-09: "the non-playhead stems
//                            that are related to the controls should be
//                            white"; kept 2026-11-01; white in every theme)
// The stereo divider between the channels wears the panel's tones; nothing
// else in the well has a role. THE LIT KEY'S FACE IS `cue` (architect
// 2026-11-11, session 106): the one program role a chrome painter reads,
// the accent being one for the markers and the lamps; the lit key's press
// and inert faces are the chrome's (`lit_press_fill`, `lit_dim_fill`,
// theme_file.h), derived from it (materials.h's THE KEY). A LIT RADIO KEY'S
// THREE FACES are the chrome's alone (`radio_lit_fill`,
// `radio_lit_press_fill`, `radio_lit_dim_fill`; architect 2026-11-11,
// session 107): the purple is no program color.
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

// THE DEFAULT PROOF (the head): the compiled reference row, Live's Default
// (kGuiReferenceThemeTones, gui_theme.h) pins the measured palette of
// 2026-11-01..05 — every one of the forty words with its literal, so
// an edit that moved one of them is a build failure here. The row is Live's
// Default file through the one derivation (theme_roles.h's head), which the
// loader runs for every theme file (the proof of 2026-11-06, on the tablet;
// the theme files' of 2026-11-11, every row equal to the .ask reader's).
// The lamp arc's one (2026-11-05 night) is Live's own word read
// off the Default file: the clip lamp FF0A0A (StandardVuMeter / Maximum).
// THE LEVEL FILL 007383 is Default's RangeEditField, Live's volume color
// (architect 2026-11-11, session 105; theme_roles.h's COMPOSITED block). THE
// LIT KEY'S FOUR (architect 2026-11-11, session 106: a lit key's face is
// the accent, `cue` FFAD56; theme_roles.h's head): THE PRINT ON A LIT FACE
// 111111, Default's ControlContrastFrame; THE LIT PRESS FILL C68743, the
// accent × 42 / 54, the ratio mean(press face 2A2A2A) / mean(key face
// 363636); and THE LIT-BUT-INERT PAIR, a checked-but-disabled key's face
// 83592C and its glyph 090909 — the accent and the print on it × 93 / 181,
// the ratio mean(key dim ink 5D5D5D) / mean(text B5B5B5) — each per
// channel, rounded half to even (the DERIVED block). THE RADIO KEYS' THREE
// (architect 2026-11-11, session 107: a lit radio key wears the purple):
// the face B595FC, Default's ScaleAwareness; its press 8D74C4, the purple
// × 42 / 54; its inert face 5D4D81, the purple × 93 / 181 — the lit key's
// two ratios on one code path (the DERIVED block). THE PLAYHEAD'S LINE
// 81E1EA is FFFFFF + 0.5 · (head 03C3D5 − FFFFFF), per channel, rounded half
// to even (architect 2026-11-12, session 113; the DERIVED block).
// THE SELECTION PAIR'S TEXT 070707 is Default's
// StandbySelectionForeground (architect 2026-11-06, session 75: Live's own
// partner of the StandbySelectionBackground bar; 111111,
// ControlContrastFrame, until then — our mispairing). THE MAGNIFY CORE
// 363636 is Default's SurfaceBackground, the key face (architect 2026-11-07,
// session 83, his pick i4 of tmp/s83; 1E1E1E, his pick of 2026-11-04 night
// and ControlBackground's number, until then — render.h's THE CORE).
namespace gui_default_theme_proof {
inline constexpr GuiThemeTones kDefault = kGuiReferenceThemeTones;
// THE CHROME (materials.h's head: the ground, the text tones, the key, the
// well's tones, the selection pair, the panes, the cards, the lamps).
static_assert(kDefault.ground          == 0x242424);
static_assert(kDefault.text            == 0xB5B5B5);
static_assert(kDefault.text_dim        == 0x757575);
static_assert(kDefault.text_on_lit     == 0x111111);
static_assert(kDefault.key_dim_ink     == 0x5D5D5D);
static_assert(kDefault.text_on_lit_dim == 0x090909);
static_assert(kDefault.key_face        == 0x363636);
static_assert(kDefault.key_press_face  == 0x2A2A2A);
static_assert(kDefault.lit_press_fill  == 0xC68743);
static_assert(kDefault.lit_dim_fill    == 0x83592C);
static_assert(kDefault.radio_lit_fill  == 0xB595FC);
static_assert(kDefault.radio_lit_press_fill == 0x8D74C4);
static_assert(kDefault.radio_lit_dim_fill   == 0x5D4D81);
static_assert(kDefault.well_frame      == 0x757575);
static_assert(kDefault.ruler_face      == 0x363636);
static_assert(kDefault.ruler_tick      == 0x919191);
static_assert(kDefault.lane_face       == 0x2A2A2A);
static_assert(kDefault.trim_bar_face   == 0x2A2A2A);
static_assert(kDefault.trim_span_face  == 0x696969);
static_assert(kDefault.selection_fill  == 0x637E86);
static_assert(kDefault.selection_text  == 0x070707);
static_assert(kDefault.pane_face       == 0x363636);
static_assert(kDefault.score_pane_face == 0x191919);
static_assert(kDefault.list_press_fill == 0xB0DDEB);
static_assert(kDefault.list_press_text == 0x070707);
static_assert(kDefault.card_face       == 0xE76942);
static_assert(kDefault.card_ink        == 0x2A2A2A);
static_assert(kDefault.clip_lamp       == 0xFF0A0A);
static_assert(kDefault.level_fill      == 0x007383);
// THE PALETTE (the head's eleven).
static_assert(kDefault.waveform_canvas == 0x393939);
static_assert(kDefault.waveform_ink    == 0x191919);
static_assert(kDefault.waveform_core   == 0x363636);
static_assert(kDefault.grid            == 0x303030);
static_assert(kDefault.center          == 0x191919);
static_assert(kDefault.cue             == 0xFFAD56);
static_assert(kDefault.cue_outline     == 0x111111);
static_assert(kDefault.inactive_cue    == 0x5D5D5D);
static_assert(kDefault.playhead_head   == 0x03C3D5);
static_assert(kDefault.playhead_line   == 0x81E1EA);
static_assert(kDefault.scanner         == 0xFFFFFF);
} // namespace gui_default_theme_proof
