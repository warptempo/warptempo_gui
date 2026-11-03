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
// lit buttons, and (2026-11-02) the grey of its inactive marker and the
// gray of its selected warp marker's tip (the stems) — but the lit outline,
// the product's own, and the scanner and the playhead's line, the controls'
// white; each ruled on his mocks.
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
// selected cue label and the flag editor's field and selection band, the
// chrome's selected pair). So the palette's program roles are exactly the
// ELEVEN below:
//   waveform_canvas  393939  the canvas, Live's darker band
//   waveform_ink     191919  the waveform, Live's
//   waveform_outline 515151  THE LIT OUTLINE, the magnified picture's
//                            one-px border, lighter than the ink
//   grid             313131  the quarter lines of each channel's half, the
//                            horizontals alone (render_canvas) — Live's
//                            antialiased beat line as one solid px on the
//                            canvas
//   center           191919  each channel's zero line over the waveform
//                            (render_center_lines) — Live's zero line IS the
//                            ink
//   cue              FFAD56  EVERY MARKER'S TRIANGLE, ONE COLOR (architect
//                            2026-11-01: "one color for all markers") —
//                            warp and phase reset alike, the coincident
//                            marker's, the `h` view's halves
//                            (resolve_flag_face), the orange of Live's
//                            start marker
//   cue_stem         919191  EVERY MARKER'S STEM (architect 2026-11-02, mock
//                            t1): both of a live cue's dot phases at rest
//                            (render.h's GuiCueDot), the phase-reset
//                            lead-in ring and the nudged pairs' ring runs
//                            with them — LIVE'S GRAY, the selected warp
//                            marker's tip in his Live video
//   inactive_cue     5D5D5D  THE DISABLED MARKER (architect 2026-11-02, the
//                            color sweep, mock C3): LIVE'S INACTIVE WARP
//                            MARKER'S TRIANGLE, measured on its own 303030
//                            lane — ONE CONSTANT, no blend: a disabled
//                            cue's triangle (no dots, resolve_flag_face)
//                            and every INERT LABEL — a disabled cue's
//                            segments and a tie follower's cells — and
//                            its strike bar. "The arrow needs to be legible
//                            for me, but the text doesn't … since it's
//                            disabled, it's inert" (architect 2026-10-10):
//                            Live's grey shows the marker exists and reads
//                            as off, the label with it
//   playhead_head    03C3D5  THE PLAYHEAD'S HEAD, the triangle in the ruler,
//                            Live's playhead triangle (render.h's playhead
//                            paragraph)
//   playhead_line    FFFFFF  THE PLAYHEAD'S DOTTED LINE IN THE CANVAS
//                            (architect 2026-11-02, mock t1), LENT TO THE
//                            SELECTION alone (architect 2026-11-02, M10
//                            rule 1): a selected cue's odd-row dots,
//                            painted by its stem whether the playhead
//                            stands there or not, never a triangle
//   scanner          FFFFFF  THE SCANNER AND THE ZOOM ANCHOR'S STEM, the
//                            white of the stems that belong to the controls
//                            (architect 2026-10-09: "the non-playhead stems
//                            that are related to the controls should be
//                            white"; kept 2026-11-01, tunable later) — the
//                            playhead line's value in a role of its own,
//                            SOLID where the resting playhead is dotted
// The stereo divider between the channels wears the panel's tones; nothing
// else in the well has a role. THE LIT KEY'S FILL is the flat look's, not
// the palette's (materials.h's kLitFill).
// THE BYTES ARE sRGB (architect 2026-10-08 ~05:15), as every authored
// color's, and the tablet converts them at the painter's entry
// (display_transform.h's head).

inline constexpr uint32_t kPaletteCanvas   = 0x393939;
inline constexpr uint32_t kPaletteInk      = 0x191919;
inline constexpr uint32_t kPaletteOutline  = 0x515151;
inline constexpr uint32_t kPaletteGrid     = 0x313131;
inline constexpr uint32_t kPaletteCenter   = 0x191919;
inline constexpr uint32_t kPaletteCue      = 0xFFAD56;
inline constexpr uint32_t kPaletteCueStem  = 0x919191;
inline constexpr uint32_t kPaletteInactiveCue = 0x5D5D5D;
inline constexpr uint32_t kPalettePlayheadHead = 0x03C3D5;
inline constexpr uint32_t kPalettePlayheadLine = 0xFFFFFF;
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
    {&GuiPalette::waveform_outline, kPaletteOutline},
    {&GuiPalette::grid,             kPaletteGrid},
    {&GuiPalette::center,           kPaletteCenter},
    {&GuiPalette::cue,              kPaletteCue},
    {&GuiPalette::cue_stem,         kPaletteCueStem},
    {&GuiPalette::inactive_cue,     kPaletteInactiveCue},
    {&GuiPalette::playhead_head,    kPalettePlayheadHead},
    {&GuiPalette::playhead_line,    kPalettePlayheadLine},
    {&GuiPalette::scanner,          kPaletteScanner},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);
