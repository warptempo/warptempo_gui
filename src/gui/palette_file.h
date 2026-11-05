#pragma once

#include "render.h"   // GuiPalette, GuiColor: the role table's members

#include <cstddef>
#include <cstdint>
#include <iterator>

// THE PALETTE IS ONE, COMPILED IN, AND ITS NUMBERS ARE ABLETON LIVE'S
// (architect 2026-11-01: "Agree, let's drop them. We keep the canvas
// grammar." — "We've taken the design, the layout from CoolEdit, and I think
// that's enough"): no preset, no device key and no Settings row chooses a
// color. Cool Edit Pro 2.1 keeps THE CANVAS GRAMMAR — the view bar, the
// ruler, the marker lane, the grid and center lines, the cues, the playhead,
// the stereo divider and their lengths (program_spec.h) — and gives no color.
// The numbers are MEASURED OFF HIS OWN SHOTS OF LIVE 12's default theme
// (2026-11-01) — the canvas, the ink, the zero line, the grid's one-px
// equivalent, the playhead's triangle, the orange of Live's markers and
// lit buttons, (2026-11-02) the grey of its inactive marker, and
// (2026-11-04) its unselected marker's grey, its dark line and the mid
// tone of its playhead's line's fade, measured per pixel at 120 fps — but
// the core, the product's own, and the scanner, the controls' white; each
// ruled on his mocks.
//
// TWO SOURCES FOR ONE STRUCT: THE PALETTE colors the well, and THE MATERIALS
// every chrome role — the flat look's constants (chrome_derive.h,
// 2026-11-01). GuiPalette (render.h) keeps
// every member; install_palette (render.h) fills the program members off
// this file's table (kGuiPaletteRoles), the panel's `face` member off the
// flat ground, and the chrome members off the theme's role table
// (kGuiThemeRoles) by the one derivation (chrome_derive.h's
// live_chrome_words), so every painter reads palette() and no painter knows
// which source a color came from.
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// flag editor's field and selection band, the chrome's selected pair). So
// the palette's program roles are exactly the TWELVE below (TWO ROLES MAY
// SHARE A NUMBER — `cue_unselected` and `inactive_cue` are both Live's
// 5D5D5D, as the ink and `center` share 191919: a role is what it paints,
// not its value):
//   waveform_canvas  393939  the canvas, Live's darker band
//   waveform_ink     191919  the waveform, Live's — the dark lamp's bar and
//                            the lit lamp's outer bar
//   waveform_core    1C1C1C  THE CORE (architect 2026-11-04, mock f1; its
//                            tone ruled 1C1C1C in session 33, mocks m1..m7
//                            of tmp/s33: "1c works"): the magnified
//                            picture's inner bar, a solid fill three levels
//                            over the ink ("the absence of it would
//                            indicate a quiet section"; render.h's THE
//                            CORE, where the 1A1A1A record is)
//   grid             313131  the quarter lines of each channel's half, the
//                            horizontals alone (render_canvas) — Live's
//                            antialiased beat line as one solid px on the
//                            canvas
//   center           191919  each channel's zero line over the waveform
//                            (render_center_lines) — Live's zero line IS the
//                            ink
//   cue              FFAD56  THE SELECTED MARKER'S FILL (architect
//                            2026-11-04, reversing 2026-11-02's "the
//                            triangle never marks selection"): the chip of
//                            every member of the selection set, warp and
//                            phase reset alike, both halves of the `h`
//                            view's selected item (resolve_flag_face), the
//                            orange of Live's start marker
//   cue_unselected   5D5D5D  THE UNSELECTED MARKER'S FILL (architect
//                            2026-11-04, mock g1): LIVE'S OWN UNSELECTED
//                            MARKER, measured on his Live shot — every live
//                            cue outside the selection
//   cue_outline      111111  LIVE'S DARK LINE (architect 2026-11-04): the
//                            one-W outline round every live chip, THE
//                            COINCIDENCE STEM down the canvas (every
//                            enabled marker the playhead stands on), the
//                            phase-reset lead-in ring and the nudged pairs'
//                            ring runs (render.h's THE STATES)
//   inactive_cue     5D5D5D  THE DISABLED MARKER (architect 2026-11-02, the
//                            color sweep, mock C3; HOLLOW since 2026-11-04):
//                            LIVE'S INACTIVE WARP MARKER, measured on its own
//                            303030 lane — ONE CONSTANT, no blend: a disabled
//                            chip's outline (its fill the lane's face, no
//                            stem, resolve_flag_face) and every INERT LABEL
//                            — a disabled cue's segments and a tie
//                            follower's cells — and its strike bar. "The
//                            arrow needs to be legible for me, but the text
//                            doesn't … since it's disabled, it's inert"
//                            (architect 2026-10-10): Live's grey shows the
//                            marker exists and reads as off, the label with
//                            it
//   playhead_head    03C3D5  THE PLAYHEAD'S HEAD, the triangle in the ruler,
//                            Live's playhead triangle (render.h's playhead
//                            paragraph), the head's alone
//   playhead_line    85E2EB  THE PLAYHEAD'S LINE IN THE CANVAS (architect
//                            2026-11-04 night, mock n1, his pick of four
//                            static tones after the measured cost of a
//                            fading line): solid, one W, opaque, ONE STATIC
//                            TONE — the mid tone of Live's fade between
//                            white and the head's cyan, the triangle wave's
//                            mean (render_playhead's paragraph)
//   scanner          FFFFFF  THE SCANNER AND THE ZOOM ANCHOR'S STEM, the
//                            white of the stems that belong to the controls
//                            (architect 2026-10-09: "the non-playhead stems
//                            that are related to the controls should be
//                            white"; kept 2026-11-01, tunable later)
// The stereo divider between the channels wears the panel's tones; nothing
// else in the well has a role. THE LIT KEY'S FILL is the flat look's, not
// the palette's (materials.h's kLitFill), and the disabled chip's hollow
// fill is the lane's own face (materials.h's kLaneFace).
// THE BYTES ARE sRGB (architect 2026-10-08 ~05:15), as every authored
// color's, and the tablet converts them at the painter's entry
// (display_transform.h's head).

inline constexpr uint32_t kPaletteCanvas   = 0x393939;
inline constexpr uint32_t kPaletteInk      = 0x191919;
inline constexpr uint32_t kPaletteCore     = 0x1C1C1C;
inline constexpr uint32_t kPaletteGrid     = 0x313131;
inline constexpr uint32_t kPaletteCenter   = 0x191919;
inline constexpr uint32_t kPaletteCue      = 0xFFAD56;
inline constexpr uint32_t kPaletteCueUnselected = 0x5D5D5D;
inline constexpr uint32_t kPaletteCueOutline    = 0x111111;
inline constexpr uint32_t kPaletteInactiveCue = 0x5D5D5D;
inline constexpr uint32_t kPalettePlayheadHead = 0x03C3D5;
inline constexpr uint32_t kPalettePlayheadLine = 0x85E2EB;
inline constexpr uint32_t kPaletteScanner  = 0xFFFFFF;

// THE ROLE TABLE — THE ONE ENUMERATION of the program's roles, in
// GuiPalette's order: the member each fills and its word (the head's list).
// install_palette fills the struct off it and palette_file.cpp's coverage
// proof walks it, so a role cannot be valued and not painted.
struct GuiPaletteRole {
    GuiColor GuiPalette::* member;
    uint32_t               word;
};
inline constexpr GuiPaletteRole kGuiPaletteRoles[] = {
    {&GuiPalette::waveform_canvas,  kPaletteCanvas},
    {&GuiPalette::waveform_ink,     kPaletteInk},
    {&GuiPalette::waveform_core,    kPaletteCore},
    {&GuiPalette::grid,             kPaletteGrid},
    {&GuiPalette::center,           kPaletteCenter},
    {&GuiPalette::cue,              kPaletteCue},
    {&GuiPalette::cue_unselected,   kPaletteCueUnselected},
    {&GuiPalette::cue_outline,      kPaletteCueOutline},
    {&GuiPalette::inactive_cue,     kPaletteInactiveCue},
    {&GuiPalette::playhead_head,    kPalettePlayheadHead},
    {&GuiPalette::playhead_line,    kPalettePlayheadLine},
    {&GuiPalette::scanner,          kPaletteScanner},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);
