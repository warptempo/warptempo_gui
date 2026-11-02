#pragma once

#include "cool_edit_derive.h" // the HLS round trip the ring tones take
#include "render.h"           // GuiRect, GuiColor, the chokepoints

#include <cairo/cairo.h>
#include <cstdint>

// THE MATERIALS — THE FLAT LOOK (architect 2026-11-01, reversing the hi-fi
// panel of 2026-10-23: "whatever ruling it needs to, we need to reverse to go
// flat"; Live's grammar, every number measured on his Live 12 shots at
// 100 %). This header is the one owner of the ground, the key, the lit fill
// and the text tones, and of the hi-fi panel's constants the well and the
// overlays still read until their own flat passes land.
//
// THE GROUND IS ONE FLAT COLOR, kGround 242424, everywhere outside the well
// and the keys: the band and its logo lane, row 8, the canvas column's side
// margins, the on-screen keyboard's band, the modal family's controls row.
// NO BAKE, NO GRAIN, NO GRADIENT, NO LINE, NO BEVEL, NO SHADOW: every pixel
// of the ground is a solid fill through the palette's chokepoint
// (set_palette_source, render.h), which converts it for the window as it
// converts every color (display_color), so True Colors holds with no cache
// of its own. The palette's `face` and the chrome's `ground` and `light_3d`
// roles ARE this constant (render.cpp's fill_program_palette,
// chrome_derive.h).
//
// THE KEY (paint_key below; every key alike — the band's, row 8's, the
// controls rows', the word buttons, the scrub's thumb, the on-screen
// keyboard's): A FLAT kKeyFace 363636 BOX, NO OUTLINE, NO LED, NO GLOW, NO
// DEPTH. LIT — the kLitFill FFAD56 box, Live's "on" — is a CHECKED key, and
// A KEY UNDER THE FINGER OR PEN for as long as the press stands, whatever
// its checked state, its own state back at the lift (architect 2026-11-02:
// Live has no pressed state, "on" and "off" each hovered or not, and we have
// no hover; the hi-fi panel's two depths and its LED that lit while held
// are retired). A DISABLED key keeps the face its state gives, its legend
// dims (kTextDimInk), so a checked-but-inert key under an overlay keeps its
// lit fill with the dim legend (architect 2026-11-01, M6). Anything printed
// ON a lit face is kTextOnLit 111111 (the on-screen keyboard's caps today).
//
// THE GROUP GAP (architect 2026-11-01, M5; "two px lines that don't scale",
// M5b): keys that abut inside a group are parted by kKeyGapPx, TWO DEVICE PX
// OF GROUND at every gui_scale — the group's outer extents stay, each key
// giving up one device px on every side that abuts a neighbor
// (key_in_cell): at 300 % a 69-px case reads 68 at a group's two ends, 67
// inside, 69 alone. THE HIT BOX STAYS THE CELL: each neighbor owns the gap's
// one px on its own side, so the pressable surface tiles the group exactly
// as before and no column answers twice (the painted key and its legend are
// the key's; the gap is ground).
//
// THE TEXT TONES (architect 2026-11-02, M13, "we use Ableton colors as much
// as possible"): Live's two, kTextInk B5B5B5 on every word, digit and
// drawing the chrome prints — the legends at rest, the logo lane, the
// clocks, the tab name and state line, the lists, the cards, the prompts,
// the editors, the marker labels, the keyboard's caps — and kTextDimInk
// 757575 for a disabled key's legend, the settings page's inert rows and the
// tooltip's hint line; kTextOnLit 111111 on a lit face. NO WORD OUTSIDE THE
// WELL WEARS A SHADOW (2026-11-01, "no shadow anywhere"): the logo lane's
// brand, legend and drawings, the tab name, the state line and the clock are
// ink alone. (The well's cue labels, triangles and ticks keep their shadows
// until the well's part retires them.)

// THE GROUND (the head).
inline constexpr uint32_t kGround = 0x242424;

// THE TEXT TONES (the head).
inline constexpr uint32_t kTextInk    = 0xB5B5B5;
inline constexpr uint32_t kTextDimInk = 0x757575;
inline constexpr uint32_t kTextOnLit  = 0x111111;

// THE KEY (the head): its face, its lit fill (Live's "on" orange FFAD56,
// one constant with no device key), and the group gap in device px.
inline constexpr uint32_t kKeyFace  = 0x363636;
inline constexpr uint32_t kLitFill  = 0xFFAD56;
inline constexpr int      kKeyGapPx = 2;

// -- WHAT THE WELL AND THE OVERLAYS STILL READ --------------------------------
// The flat constants below are the hi-fi panel's, left standing for the
// well (the view bar, the ruler, the marker lane, the canvas's frame, the
// time fields) and the overlays (the modal box, the cards, the tooltip, the
// lists) until their flat passes land.
//
// THE FLAT GROUNDS (2026-10-23): the ruler's flat dark lane, the field
// (every field, the modal family's box, the cards and the tooltip), the
// time fields' black.
inline constexpr uint32_t kRulerGround     = 0x0E0E10;
inline constexpr uint32_t kFieldGround     = 0x0A0A0C;
inline constexpr uint32_t kTimeFieldGround = 0x000000;
// THE SELECTION PAIR (2026-10-23, "the grey everywhere"): a flat grey bar
// under the text — the lists' lit row, the flag editor's and the dialog
// editors' selected substring; its text the text tone.
inline constexpr uint32_t kSelectionFill = 0x3E4044;
inline constexpr uint32_t kSelectionText = kTextInk;

// THE RING TONES OFF A GROUND (2026-10-23): light L + .16 with S × .9, dark
// L × .40, the hue kept. NO LINE OUTSIDE THE WELL WEARS THEM since the flat
// look; the program ring round the well, the well's own lines, the time
// fields' rings, the scrub's channel, the modal box's ring and the cards'
// and the tooltip's rings do, off the flat ground (kRingLight / kRingDark,
// the palette's `ce_hilight` / `ce_dark` and the chrome's `hilight` /
// `shadow`), until their parts land.
constexpr uint32_t ring_light_of(uint32_t mean) {
    return cool_edit_derive::tone(mean, 1.0, 0.16, 0.9, 0.0);
}
constexpr uint32_t ring_dark_of(uint32_t mean) {
    return cool_edit_derive::tone(mean, 0.40, 0.0);
}
inline constexpr uint32_t kRingLight = ring_light_of(kGround);
inline constexpr uint32_t kRingDark  = ring_dark_of(kGround);
// THE VIEW BAR'S SPAN (architect 2026-10-25: "the trim bar should be a
// neutral gray", "sort of like the button color, maybe a little different so
// it doesn't blend in"): a raised block of a NEUTRAL GREY, its body
// kSpanFace, its top row and left column kSpanLight and its bottom row and
// right column kSpanDark, the ring tones off the body. No palette role
// touches the trio (render_trim_flags paints it, render.h's view bar block).
inline constexpr uint32_t kSpanFace  = 0x505053;
inline constexpr uint32_t kSpanLight = ring_light_of(kSpanFace);
inline constexpr uint32_t kSpanDark  = ring_dark_of(kSpanFace);
static_assert(kSpanLight == 0x78787C && kSpanDark == 0x202021);

// -- THE KEY'S PAINTERS ------------------------------------------------------

// ONE KEY on `r` (the head's THE KEY): the flat face, the lit fill when
// `lit` — the caller's answer, its checked state OR a standing press — and
// nothing else. A key is blank: its legend stands under it on the ground
// (paint_handler.cpp's paint_key_legend); what a key carries on its face is
// its caller's, in kTextOnLit on a lit face and kTextInk on a resting one.
void paint_key(cairo_t* cr, const GuiRect& r, bool lit);

// THE KEY INSIDE ITS CELL (the head's THE GROUP GAP): the cell `cell` less
// one device px on each side that abuts a neighbor of its group —
// `joined_left` / `joined_right` — so two abutting keys stand kKeyGapPx
// apart. The cell stays the hit box; the painted key and its legend's seat
// are this.
GuiRect key_in_cell(const GuiRect& cell, bool joined_left, bool joined_right);
