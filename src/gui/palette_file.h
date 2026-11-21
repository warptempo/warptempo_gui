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
// `default` Arc-Dark's copy (2026-11-18). No per-color key, no Settings
// row for a color.
// Cool Edit Pro 2.1 keeps THE CANVAS GRAMMAR — the view
// bar, the ruler, the marker lane, the grid and center lines, the cues, the
// playhead, the stereo divider and their lengths (program_spec.h) — and
// gives no color. THE REFERENCE ROW (Live's Default, "Default Dark Neutral
// Medium", COMPILED: kGuiReferenceThemeTones, gui_theme.h, listed as
// `default_dark_neutral_medium`; no file ships it) IS THE 2026-11-01..05
// MEASURED PALETTE TO THE BYTE but the selection
// pair's text (070707 since 2026-11-06), the magnify core (363636, Live's
// SurfaceBackground, since 2026-11-07), the lit-but-inert key's pair
// (2026-11-11), the playhead's line
// (2026-11-12), the enabled chip's fill (the accent, 2026-11-13)
// and the print on a lit face, the cards' ink and the ruler (2026-11-17,
// session 140), the two lit-but-inert pairs (2026-11-17, on Arc's own
// numbers since 2026-11-19, send Z) and the three roles of our own and the
// Arc page's three derived tones (2026-11-19; all at the foot) — the numbers
// MEASURED OFF HIS OWN SHOTS OF LIVE 12's default theme (2026-11-01: the
// canvas, the ink, the zero line, the grid's one-px equivalent, the
// playhead's triangle, the orange of Live's markers and lit buttons;
// 2026-11-02 the grey of its inactive marker; 2026-11-04 its dark line) —
// AND THE PLAYHEAD LINE'S STATIC TONE, white half way to the head on every
// theme, the mean of Live's fade (81E1EA; architect 2026-11-12, session 113,
// theme_roles.h's DERIVED block), and THE ENABLED CHIP'S FILL,
// the accent FFAD56 (2026-11-13, session 119) — the
// static_asserts at the foot are that proof in the build, every one of the
// forty-five words with its literal (the two hover tones of send CC,
// 2026-11-20, a key's 363636 and the pill's 696969, each Default's own
// number for the role it copies; the progress bar's trough of send AA's
// stage D2, 2026-11-19, 242424, ScrollbarInnerTrack over the ground, a
// Live theme's own number — and the scale's rim of stage D3, 2A2A2A, ClipBorder over the
// ground —
// the lamp arc's clip lamp the same night, the lit-but-inert key's face and
// its glyph of 2026-11-11, and the enabled chip's fill of 2026-11-12, retold
// 2026-11-13; the trough and the
// well's frame of 2026-11-17, with the print on a lit face,
// the cards' ink, the ruler and the two inert pairs retold that day,
// session 140; the body tone, the title bar's text, the sidebar text, the
// ruler's digits and the disabled key's face and border of 2026-11-19, send
// Z, with the two inert pairs retold; the three carried fills, each opaque
// (its top byte FF), and the limiter lamp of send AA's stage D1 the same
// day, the cards' face retired into them).
//
// THE ROWS ARE THE THEMES' OWN NUMBERS; THE PALETTE INSTALLED IS THE ROW
// MIXED (architect 2026-11-13, session 119; theme_roles.h's THE BRIGHT
// MIX): install_palette fills every member off the row with its bright
// tones — of this table `cue`, `cue_fill` and the playhead's two, of the
// chrome's the lit key's inert face, the selection fill, the standing card
// and the two lamps — mixed toward its ground at the
// `bright_mix` key. Every "Default N"
// below is the reference row's own number, the record; at 100 the screen
// shows exactly it, at the key's default 75 (architect 2026-11-18, send Y)
// the accent C88B4A where it says FFAD56.
//
// TWO TABLES FOR ONE STRUCT: THE PALETTE (kGuiPaletteRoles below) colors the
// well, and THE CHROME'S ROLE TABLE (kGuiThemeRoles, theme_file.h) every
// chrome role; both map a GuiPalette member to a GuiThemeTones member.
// GuiPalette (render.h) keeps every member; install_palette (render.cpp)
// fills every member off the installed theme's row — the program members
// off this file's table, the panel's `face` off the theme's body tone, the
// chrome members off theme_file.h's — so every painter reads palette() and
// no painter knows which table a color came from, nor which theme.
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// flag editor's field and selection band, the chrome's selected pair). So
// the palette's program roles are exactly the TWELVE below (TWO ROLES MAY
// SHARE A NUMBER — the ink and `center` share Default's 191919, and
// `cue_fill` the accent `cue`'s FFAD56: a role is what it paints, not its
// value). Each role's Default word and its Live
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
//   cue              FFAD56  THE ACCENT (ViewCheckControlEnabledOn, the
//                            orange of Live's start marker): A LIT OR
//                            PRESSED KEY'S FACE, read by the key's painters
//                            (paint_linked_group, paint_word_button, and
//                            NOISE's progress line, paint_entry, materials.h;
//                            architect 2026-11-11, session 106; every lit
//                            key since the radio keys' class retired,
//                            2026-11-19, send Z); the markers
//                            wear the same word as `cue_fill` (below), so
//                            no well painter reads `cue` itself (the head's
//                            last paragraph)
//   cue_fill         FFAD56  THE ENABLED MARKER'S FILL (architect
//                            2026-11-05, his pick a2 of tmp/s42; THE ACCENT
//                            AGAIN since 2026-11-13, session 119: "we revert
//                            the rule so that markers get accent color
//                            instead of icon glyph color, and we simply tune
//                            the accent itself ... so that the accent shows
//                            up everywhere as slightly darker for the dark
//                            themes" — the tuning the bright mix,
//                            theme_roles.h): the chip of every enabled cue,
//                            selected or not, warp and phase reset alike,
//                            and the `h` view's added and changed items
//                            (resolve_flag_face) — the lit keys' face, so
//                            the markers and the lamps are one color
//                            (ViewCheckControlEnabledOn, `cue`'s role, its
//                            own member)
//   cue_outline      111111  LIVE'S DARK LINE (architect 2026-11-04): THE
//                            SELECTION STEM down the canvas (every selected
//                            marker, architect 2026-11-05; enabled or
//                            disabled, 2026-11-11) and the phase-reset
//                            lead-in ring (render.h's THE STATES) — no
//                            chip and no ruler head wears an outline since
//                            2026-11-19 (send AA: "the scroll bar has no
//                            outline") (ControlContrastFrame)
//   inactive_cue     5D5D5D  THE DISABLED MARKER (architect 2026-11-02, the
//                            color sweep, mock C3; the solid chip since
//                            2026-11-05, "disabled marker gets dim label"):
//                            LIVE'S INACTIVE WARP MARKER, measured on its own
//                            303030 lane — ONE ROLE, no blend: a disabled
//                            chip's fill, alone (the stem the selection's
//                            `cue_outline` as on an enabled chip,
//                            resolve_flag_face) — the `h`
//                            view's removed
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
// 2026-11-11, session 106): the one program role a chrome painter reads —
// and the one no well painter reads, the chip reading `cue_fill`, the same
// word (2026-11-13, session 119), kept in this table where it stood; the
// lit key's inert face is the chrome's (`lit_dim_fill`, theme_file.h),
// derived from it (materials.h's THE KEY).
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
    {&GuiPalette::cue_fill,         &GuiThemeTones::cue_fill},
    {&GuiPalette::cue_outline,      &GuiThemeTones::cue_outline},
    {&GuiPalette::inactive_cue,     &GuiThemeTones::inactive_cue},
    {&GuiPalette::playhead_head,    &GuiThemeTones::playhead_head},
    {&GuiPalette::playhead_line,    &GuiThemeTones::playhead_line},
    {&GuiPalette::scanner,          &GuiThemeTones::scanner},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);

// THE DEFAULT PROOF (the head): the compiled reference row, Live's Default
// (kGuiReferenceThemeTones, gui_theme.h) pins the measured palette of
// 2026-11-01..05 — every one of the forty-five words with its literal, so
// an edit that moved one of them is a build failure here. The row is Live's
// Default file through the one derivation (theme_roles.h's head), which the
// loader runs for every theme file (the proof of 2026-11-06, on the tablet;
// the theme files' of 2026-11-11, every row equal to the .ask reader's).
// The lamp arc's one (2026-11-05 night) is Live's own word read
// off the Default file: the clip lamp FF0A0A (StandardVuMeter / Maximum).
// THE LIT KEY'S THREE (architect 2026-11-11, session 106: a lit key's face is
// the accent, `cue` FFAD56; theme_roles.h's head): THE PRINT ON A LIT FACE
// 070707, Default's StandbySelectionForeground (architect 2026-11-17,
// session 140); and THE LIT-BUT-INERT
// PAIR on Arc's own numbers (architect 2026-11-19, send Z: "let's do arcs
// numbers"), a checked-but-disabled key's face 86623B — (the accent at 75 %
// over the body tone 242424, C88B4A) at 60 % over the body tone — and its
// glyph 2A221A, (the print at 80 % over C88B4A) at 60 % over the body
// tone, dimmed with the button as GTK's opacity dims it — each per channel,
// rounded half to even (the DERIVED block). THE RULER 393939 is the canvas's tone and THE CARDS'
// INK 070707 the print's (architect 2026-11-17, session 140, ruling 3 and
// D4). OUR OWN THREE ROLES (architect 2026-11-19, send Z; theme_roles.h's
// THE FORMAT) are Default's own numbers: THE BODY TONE 242424, its
// SurfaceArea; THE TITLE BAR'S TEXT and THE SIDEBAR TEXT B5B5B5, its
// ControlForeground. THE ARC PAGE'S DERIVED THREE (the same day; the
// DERIVED block): THE RULER'S DIGITS 9C9C9C, the text B5B5B5 at 80 % over
// the ruler 393939; THE DISABLED KEY'S FACE 2E2E2E, the key face 363636 at
// 55 % over the body tone 242424; ITS BORDER 272727, the well's border
// 2A2A2A at 55 % over the body tone. THE LIST ROW'S HOVER 393939 (architect
// 2026-11-21, send EE; the DERIVED block): the ruler 393939 plus the
// buttons' hover step, Default's 363636 − 363636, nothing — no row hovers
// on the reference row, as no key does.
// THE PLAYHEAD'S LINE
// 81E1EA is FFFFFF + 0.5 · (head 03C3D5 − FFFFFF), per channel, rounded half
// to even (architect 2026-11-12, session 113; the DERIVED block). THE ENABLED
// CHIP'S FILL FFAD56 is the accent, Default's ViewCheckControlEnabledOn
// (architect 2026-11-13, session 119; the ROLE-NAMED block). Every word
// here is the row's own number, k = 100 of THE BRIGHT MIX (theme_roles.h),
// which the install applies to this row as to every row: no assert pins a
// mixed word.
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
static_assert(kDefault.body_face       == 0x242424);
static_assert(kDefault.text            == 0xB5B5B5);
static_assert(kDefault.title_text      == 0xB5B5B5);
static_assert(kDefault.sidebar_text    == 0xB5B5B5);
static_assert(kDefault.text_dim        == 0x757575);
static_assert(kDefault.text_on_lit     == 0x070707);
static_assert(kDefault.key_dim_ink     == 0x5D5D5D);
static_assert(kDefault.key_dim_face    == 0x2E2E2E);
static_assert(kDefault.key_dim_border  == 0x272727);
static_assert(kDefault.text_on_lit_dim == 0x2A221A);
static_assert(kDefault.key_face        == 0x363636);
static_assert(kDefault.key_hover_face  == 0x363636);
static_assert(kDefault.list_hover_face == 0x393939);
static_assert(kDefault.lit_dim_fill    == 0x86623B);
static_assert(kDefault.ruler_face      == 0x393939);
static_assert(kDefault.ruler_tick      == 0x919191);
static_assert(kDefault.ruler_text      == 0x9C9C9C);
static_assert(kDefault.lane_face       == 0x2A2A2A);
static_assert(kDefault.trim_bar_face   == 0x242424);
static_assert(kDefault.trim_span_face  == 0x696969);
static_assert(kDefault.trim_span_hover_face == 0x696969);
static_assert(kDefault.well_border     == 0x2A2A2A);
static_assert(kDefault.selection_fill  == 0x637E86);
static_assert(kDefault.selection_text  == 0x070707);
static_assert(kDefault.score_pane_face == 0x191919);
static_assert(kDefault.scale_trough    == 0x242424);
static_assert(kDefault.scale_slider_border == 0x2A2A2A);
static_assert(kDefault.tooltip_fill    == 0xFF363636);
static_assert(kDefault.card_fill       == 0xFFE76942);
static_assert(kDefault.card_standing_fill == 0xFFE76942);
static_assert(kDefault.card_ink        == 0x070707);
static_assert(kDefault.limiter_lamp    == 0xE76942);
static_assert(kDefault.clip_lamp       == 0xFF0A0A);
// THE PALETTE (the head's twelve).
static_assert(kDefault.waveform_canvas == 0x393939);
static_assert(kDefault.waveform_ink    == 0x191919);
static_assert(kDefault.waveform_core   == 0x363636);
static_assert(kDefault.grid            == 0x303030);
static_assert(kDefault.center          == 0x191919);
static_assert(kDefault.cue             == 0xFFAD56);
static_assert(kDefault.cue_fill        == 0xFFAD56);
static_assert(kDefault.cue_outline     == 0x111111);
static_assert(kDefault.inactive_cue    == 0x5D5D5D);
static_assert(kDefault.playhead_head   == 0x03C3D5);
static_assert(kDefault.playhead_line   == 0x81E1EA);
static_assert(kDefault.scanner         == 0xFFFFFF);
} // namespace gui_default_theme_proof
