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
// the stereo divider, their lengths (program_spec.h) and the panel tones the
// chrome derives from the ground (cool_edit_derive.h) — and gives no color.
// The numbers are MEASURED OFF HIS OWN SHOTS OF LIVE 12's default theme
// (2026-11-01) — the canvas, the ink, the zero line, the grid's one-px
// equivalent, the playhead's triangle and the orange of Live's markers and
// lit buttons — but the lit outline, the product's own, and the scanner,
// the controls' white; each ruled on his mocks that evening.
//
// TWO SOURCES FOR ONE STRUCT: THE PALETTE colors the well, and THE MATERIALS
// every chrome role — the flat look's constants (chrome_derive.h,
// 2026-11-01). GuiPalette (render.h) keeps
// every member; install_palette (render.h) fills the program members off
// this file's table (kGuiPaletteRoles), the panel's `face` member and THE
// COOL EDIT BLOCK (the two disabled-cue tones) off the flat ground, and the chrome members off the
// theme's role table (kGuiThemeRoles) by the one derivation (chrome_derive.h's
// live_chrome_words), so every painter reads palette() and no painter knows
// which source a color came from.
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// selected cue label and the flag editor's field and selection band, the
// chrome's selected pair). So the palette's program roles are exactly the
// EIGHT below:
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
//   cue              FFAD56  EVERY MARKER, ONE COLOR (architect 2026-11-01:
//                            "one color for all markers") — warp and phase
//                            reset alike, the coincident marker's, the `h`
//                            view's halves: the triangle and both of the
//                            stem's dot phases at rest (render.h's
//                            GuiCueDot), the orange of Live's start marker
//   playhead_stem    03C3D5  THE PLAYHEAD'S HEAD AND DOTS, Live's playhead
//                            triangle (render.h's playhead paragraph), LENT
//                            TO THE SELECTION alone (architect 2026-11-02,
//                            M10 rule 1): a selected cue's range-phase dots
//                            and the focused cue's triangle
//   scanner          FFFFFF  THE SCANNER AND THE ZOOM ANCHOR'S STEM, the
//                            white of the stems that belong to the controls
//                            (architect 2026-10-09: "the non-playhead stems
//                            that are related to the controls should be
//                            white"; kept 2026-11-01, tunable later)
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
inline constexpr uint32_t kPalettePlayhead = 0x03C3D5;
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
    {&GuiPalette::playhead_stem,    kPalettePlayhead},
    {&GuiPalette::scanner,          kPaletteScanner},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);
