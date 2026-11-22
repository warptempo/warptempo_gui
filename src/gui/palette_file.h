#pragma once

#include "render.h"         // GuiPalette, GuiColor: the fill table's members
#include "gui_theme.h"      // GuiThemeTones: the fill table's tones

#include <cstddef>
#include <cstdint>
#include <iterator>

// THE PALETTE IS ARC-DARK'S, COMPILED (architect 2026-11-21, send GG: "i am
// committed" — one look; "the alt end state is what i effectively want": the
// tones a compiled row, kArcDarkTones in gui_theme.h, each line its Arc
// source). The theme arc of 2026-11-05 reopened 2026-11-01's one-palette
// ruling ("Agree, let's drop them. We keep the canvas grammar" — "We've
// taken the design, the layout from CoolEdit, and I think that's enough");
// one look closes it again, at Arc-Dark's numbers. No per-color key, no
// Settings row for a color, nothing read from a file. Cool Edit Pro 2.1
// keeps THE CANVAS GRAMMAR — the view bar, the ruler, the marker lane, the
// grid and center lines, the cues, the playhead, the stereo divider and
// their lengths (program_spec.h) — and gives no color.
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PROGRAM'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the flag
// editor's field and selection band, the chrome's selected pair). GuiPalette
// (render.h) holds both halves in one struct — THE CHROME'S ROLES first (the
// first six Windows' element names, ButtonFace, ButtonText, Hilight /
// HilightText, WindowText and InfoText, the chrome's painters written
// against them; then the flat look's, materials.h's head), THE PROGRAM'S
// after them (the canvas, its grid and center lines, the markers' chips,
// hovers and stem, the playhead's head and stem, the hover line, the
// scanner), and the panel's `face` — so every painter reads palette() and
// none knows which half a color is.
//
// A ROLE STANDS IFF A PAINTER READS IT (the relief quartet, the field ground
// and the clock pair retired 2026-11-02; the plate's two inks are no
// GuiColor at all — the plate takes the row's words, waveform_plate_inks,
// render.h). TWO ROLES MAY SHARE A TONE: a role is what it paints, not its
// value (`label`, `field_text` and `text` all read the font color; the zero
// line `center` is the waveform's ink; `cue_fill` the accent `cue`'s
// number). THE LIT KEY'S FACE IS `cue`, THE ACCENT (architect 2026-11-11,
// session 106): the one program role a chrome painter reads, and the one no
// well painter reads, the chip reading `cue_fill` (2026-11-13, session 119);
// its inert face is the chrome's `lit_dim_fill` (materials.h's THE KEY).
// WHAT IS PRINTED ON A KEY has no role: the glyph road takes the row's word
// (key_ink, materials.h: `text`, `text_on_lit`, `key_dim_ink`,
// `text_on_lit_dim`). THE INERT MARKER (architect 2026-10-10: "The arrow
// needs to be legible for me, but the text doesn't … since it's disabled,
// it's inert"): its chip `inactive_cue`, its label and strike bar the
// chrome's dim print `text_dim` (render.cpp's cue_label_ink,
// resolve_flag_face's table). THE BYTES ARE sRGB (architect 2026-10-08
// ~05:15) and the tablet converts them at the painter's entry
// (display_transform.h's head).

// THE FILL TABLE — THE ONE ENUMERATION of the palette's members, in
// GuiPalette's order: the member each fills and the row's tone that fills
// it. install_palette (render.cpp) walks it at the launch; the proof below
// walks it, so no member is filled twice or left at construction state and
// no row entry names a member twice. A plain member-to-member table rather
// than a hand-written fill because the table is what the proof can read:
// the coverage stays compile-proven.
struct GuiPaletteFill {
    GuiColor GuiPalette::*    member;
    uint32_t GuiThemeTones::* tone;
};
inline constexpr GuiPaletteFill kGuiPaletteFill[] = {
    // THE CHROME (the first six Windows' element names).
    {&GuiPalette::ground,          &GuiThemeTones::ground},
    {&GuiPalette::label,           &GuiThemeTones::text},
    {&GuiPalette::selected_fill,   &GuiThemeTones::selection_fill},
    {&GuiPalette::selected_text,   &GuiThemeTones::selection_text},
    {&GuiPalette::field_text,      &GuiThemeTones::text},
    {&GuiPalette::card_text,       &GuiThemeTones::card_ink},
    {&GuiPalette::body_face,       &GuiThemeTones::body_face},
    {&GuiPalette::text,            &GuiThemeTones::text},
    {&GuiPalette::title_text,      &GuiThemeTones::title_text},
    {&GuiPalette::sidebar_text,    &GuiThemeTones::sidebar_text},
    {&GuiPalette::text_dim,        &GuiThemeTones::text_dim},
    {&GuiPalette::inactive_cue_label_hover, &GuiThemeTones::inactive_cue_label_hover},
    {&GuiPalette::key_dim_ink,     &GuiThemeTones::key_dim_ink},
    {&GuiPalette::key_dim_face,    &GuiThemeTones::key_dim_face},
    {&GuiPalette::key_dim_border,  &GuiThemeTones::key_dim_border},
    {&GuiPalette::key_face,        &GuiThemeTones::key_face},
    {&GuiPalette::key_hover_face,  &GuiThemeTones::key_hover_face},
    {&GuiPalette::list_hover_face, &GuiThemeTones::list_hover_face},
    {&GuiPalette::lit_dim_fill,    &GuiThemeTones::lit_dim_fill},
    {&GuiPalette::ruler_face,      &GuiThemeTones::ruler_face},
    {&GuiPalette::ruler_tick,      &GuiThemeTones::ruler_tick},
    {&GuiPalette::ruler_text,      &GuiThemeTones::ruler_text},
    {&GuiPalette::lane_face,       &GuiThemeTones::lane_face},
    {&GuiPalette::trim_bar_face,   &GuiThemeTones::trim_bar_face},
    {&GuiPalette::trim_span_face,  &GuiThemeTones::trim_span_face},
    {&GuiPalette::trim_span_hover_face, &GuiThemeTones::trim_span_hover_face},
    {&GuiPalette::well_border,     &GuiThemeTones::well_border},
    {&GuiPalette::score_pane_face, &GuiThemeTones::score_pane_face},
    {&GuiPalette::scale_trough,    &GuiThemeTones::scale_trough},
    {&GuiPalette::scale_slider_border, &GuiThemeTones::scale_slider_border},
    // The three carried fills: the color alone here (hex drops the top
    // byte); their opacity is read off the row's word (tone_alpha).
    {&GuiPalette::tooltip_fill,    &GuiThemeTones::tooltip_fill},
    {&GuiPalette::card_fill,       &GuiThemeTones::card_fill},
    {&GuiPalette::card_standing_fill, &GuiThemeTones::card_standing_fill},
    {&GuiPalette::limiter_lamp,    &GuiThemeTones::limiter_lamp},
    {&GuiPalette::clip_lamp,       &GuiThemeTones::clip_lamp},
    // THE PROGRAM.
    {&GuiPalette::waveform_canvas, &GuiThemeTones::waveform_canvas},
    {&GuiPalette::grid,            &GuiThemeTones::grid},
    {&GuiPalette::center,          &GuiThemeTones::center},
    {&GuiPalette::cue,             &GuiThemeTones::cue},
    {&GuiPalette::cue_fill,        &GuiThemeTones::cue_fill},
    {&GuiPalette::cue_hover,       &GuiThemeTones::cue_hover},
    {&GuiPalette::cue_outline,     &GuiThemeTones::cue_outline},
    {&GuiPalette::inactive_cue,    &GuiThemeTones::inactive_cue},
    {&GuiPalette::inactive_cue_hover, &GuiThemeTones::inactive_cue_hover},
    {&GuiPalette::playhead_head,   &GuiThemeTones::playhead_head},
    {&GuiPalette::playhead_line,   &GuiThemeTones::playhead_line},
    {&GuiPalette::hover_line,      &GuiThemeTones::hover_line},
    {&GuiPalette::scanner,         &GuiThemeTones::scanner},
    // THE PANEL'S FACE: the body tone (send Z, 2026-11-19).
    {&GuiPalette::face,            &GuiThemeTones::body_face},
};
inline constexpr std::size_t kGuiPaletteFillCount = std::size(kGuiPaletteFill);

// THE FILL COVERS GuiPalette EXACTLY: every entry names a member and a tone,
// no member is named twice, and the entries number the struct's members —
// so every member is filled once (GuiPalette is GuiColors alone).
constexpr bool palette_fill_decided() {
    for (std::size_t i = 0; i < kGuiPaletteFillCount; ++i) {
        if (kGuiPaletteFill[i].member == nullptr) return false;
        if (kGuiPaletteFill[i].tone == nullptr) return false;
        for (std::size_t j = i + 1; j < kGuiPaletteFillCount; ++j)
            if (kGuiPaletteFill[i].member == kGuiPaletteFill[j].member)
                return false;
    }
    return true;
}
static_assert(palette_fill_decided());
static_assert(sizeof(GuiPalette) == kGuiPaletteFillCount * sizeof(GuiColor));
