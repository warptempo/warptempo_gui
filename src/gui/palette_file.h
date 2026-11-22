#pragma once

#include "render.h"         // GuiPalette, GuiColor: the role table's members
#include "gui_theme.h"      // GuiThemeTones: the role table's tones

#include <cstddef>
#include <cstdint>
#include <iterator>

// THE PALETTE IS THE ONE THEME'S, ARC-DARK'S (architect 2026-11-21, send
// GG: "i am committed" — every other theme removed, one look; the theme arc
// of 2026-11-05 — "i'd like to implement all" — reopening 2026-11-01's
// one-palette ruling on his word: "Agree, let's drop them. We keep the
// canvas grammar" — "We've taken the design, the layout from CoolEdit, and I
// think that's enough"): the theme file `themes/system/arc_dark.theme`,
// Live's raw role values in our keyed format with roles of our own, shipped
// in the APK and read at launch through the one derivation (theme_roles.h's
// head). No per-color key, no Settings row for a color.
// Cool Edit Pro 2.1 keeps THE CANVAS GRAMMAR — the view
// bar, the ruler, the marker lane, the grid and center lines, the cues, the
// playhead, the stereo divider and their lengths (program_spec.h) — and
// gives no color.
//
// THE ROW IS THE THEME'S OWN NUMBERS AND THE PALETTE INSTALLED IS THE ROW
// AS IT IS (architect 2026-11-21, send GG: install_palette paints the
// theme's own numbers). Every "Default N" below is Live's Default's number
// ("Default Dark Neutral Medium", the derivation's reference of its Live
// roles), the record and not a painted tone; the screen shows arc_dark's,
// named beside a role where the telling needs it.
//
// TWO TABLES FOR ONE STRUCT: THE PALETTE (kGuiPaletteRoles below) colors the
// well, and THE CHROME'S ROLE TABLE (kGuiThemeRoles, theme_file.h) every
// chrome role; both map a GuiPalette member to a GuiThemeTones member.
// GuiPalette (render.h) keeps every member; install_palette (render.cpp)
// fills every member off the installed theme's row — the program members
// off this file's table, the panel's `face` off the theme's body tone, the
// chrome members off theme_file.h's — so every painter reads palette() and
// no painter knows which table a color came from.
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// flag editor's field and selection band, the chrome's selected pair). So
// the palette's program roles are exactly the FIFTEEN below (TWO ROLES MAY
// SHARE A NUMBER — the ink and `center` share Default's 191919, and
// `cue_fill` the accent `cue`'s FFAD56: a role is what it paints, not its
// value). Each role's Default word
// and its source (the derivation, theme_roles.h's head):
//   waveform_canvas  393939  the canvas, Live's darker band (DetailGridTiles
//                            over DetailViewBackground)
//   waveform_ink     191919  the waveform, Live's — the dark lamp's bar and
//                            the lit lamp's outer bar (WaveformColor over
//                            DetailViewBackground)
//   waveform_core    363636  THE CORE (architect 2026-11-04, mock f1), Live's
//                            SurfaceBackground, THE KEY FACE, on every theme
//                            (architect 2026-11-07, session 83, his pick i4
//                            on real MAGNIFY-lit captures): the
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
//                            2026-11-05, his pick a2; THE ACCENT
//                            AGAIN since 2026-11-13, session 119: "we revert
//                            the rule so that markers get accent color
//                            instead of icon glyph color, and we simply tune
//                            the accent itself ... so that the accent shows
//                            up everywhere as slightly darker for the dark
//                            themes"): the chip of every enabled cue,
//                            selected or not, warp and phase reset alike,
//                            and the `h` view's added and changed items
//                            (resolve_flag_face) — the lit keys' face, so
//                            the markers and the lamps are one color
//                            (ViewCheckControlEnabledOn, `cue`'s role, its
//                            own member)
//   cue_hover        FFAD56  AN ENABLED MARKER'S CHIP UNDER THE HOVERING PEN
//                            (architect 2026-11-21, send FF: the waveform's
//                            hover) (CueHover, our own role;
//                            arc_dark 639FE5, Arc's hover of a blue thing,
//                            `.documents-entry-tag:hover`; a Live theme's
//                            ViewCheckControlEnabledOn, the chip's own tone)
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
//                            303030 lane — ONE ROLE, no blend: A DISABLED
//                            MARKER'S CHIP FILL, ALONE (the stem the
//                            selection's `cue_outline` as on an enabled
//                            chip, resolve_flag_face) — the `h` view's
//                            removed items' chips too (2026-11-11), their
//                            row of the same table — and nothing else: the
//                            INERT LABEL — a disabled cue's segments, a tie
//                            follower's cells, a removed history item's
//                            label — and its strike bar wear the chrome's
//                            dim print `text_dim` (render.cpp's
//                            cue_label_ink, resolve_flag_face's table).
//                            "The arrow needs to be legible for me, but the
//                            text doesn't … since it's disabled, it's
//                            inert" (architect 2026-10-10): Live's grey
//                            shows the marker exists and reads as off
//                            (BackgroundClip)
//   inactive_cue_hover
//                    5D5D5D  A DISABLED MARKER'S CHIP UNDER THE HOVERING PEN
//                            (send FF, 2026-11-21: a disabled marker "is
//                            ignored by the parser, but otherwise fully
//                            clickable and actionable", so it hovers as an
//                            enabled one does) (CueDisabledHover, our own
//                            role; arc_dark 90939B, Arc's `wm_icon_bg`, the
//                            titlebar glyph awake; a Live theme's
//                            BackgroundClip, the chip's own tone). Its
//                            label's hover is the chrome's
//                            (`inactive_cue_label_hover`, theme_file.h,
//                            beside the inert label's `text_dim`)
//   playhead_head    03C3D5  THE PLAYHEAD'S HEAD, the triangle in the ruler,
//                            Live's playhead triangle (render.h's playhead
//                            paragraph), the head's alone (RangeDefault;
//                            arc_dark A5ABB5, the stem's color, architect
//                            2026-11-21, send GG)
//   playhead_line    81E1EA  THE PLAYHEAD'S LINE IN THE CANVAS, ITS STEM
//                            (architect 2026-11-04 night, mock n1, a static
//                            tone after the measured cost of a fading line):
//                            solid, one W, opaque, ONE STATIC TONE
//                            (PlayheadLine, our own role since send FF,
//                            2026-11-21: arc_dark A5ABB5, Arc's `.h4` /
//                            `.category-label` — "the stem is too bright"
//                            at the font color, send FF; the head wears it
//                            too since send GG; a Live theme's white
//                            half way to its head, the mean of Live's fade,
//                            architect 2026-11-12, session 113;
//                            render_playhead's paragraph, theme_roles.h's
//                            OUR OWN, THE WAVEFORM'S HOVER)
//   hover_line       696969  THE WAVEFORM'S HOVER LINE (send FF, 2026-11-21:
//                            "an even more dark and faint version of the
//                            playhead stem") AND ITS TRIANGLE IN THE RULER
//                            (architect 2026-11-21, send GG: "same color as
//                            the hover stem. for symmetry") (HoverLine, our own role, over
//                            the canvas; arc_dark 767B87, Arc's scrollbar
//                            slider; a Live theme's ScrollbarInnerHandle,
//                            the trim bar's pill)
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
    {&GuiPalette::cue_hover,        &GuiThemeTones::cue_hover},
    {&GuiPalette::cue_outline,      &GuiThemeTones::cue_outline},
    {&GuiPalette::inactive_cue,     &GuiThemeTones::inactive_cue},
    {&GuiPalette::inactive_cue_hover, &GuiThemeTones::inactive_cue_hover},
    {&GuiPalette::playhead_head,    &GuiThemeTones::playhead_head},
    {&GuiPalette::playhead_line,    &GuiThemeTones::playhead_line},
    {&GuiPalette::hover_line,       &GuiThemeTones::hover_line},
    {&GuiPalette::scanner,          &GuiThemeTones::scanner},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);
