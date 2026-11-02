#pragma once

#include "render.h"           // GuiRect, GuiColor, the chokepoints

#include <cairo/cairo.h>
#include <cstdint>

// THE MATERIALS — THE FLAT LOOK (architect 2026-11-01, reversing the hi-fi
// panel of 2026-10-23: "whatever ruling it needs to, we need to reverse to go
// flat"; Live's grammar, every number measured on his Live 12 shots at
// 100 %). This header is the one owner of the ground, the key, the lit fill,
// the text tones, the well's tones and the pane.
//
// THE GROUND IS ONE FLAT COLOR, kGround 242424, everywhere outside the well's
// regions and the keys: the band and its logo lane, row 8, the well's side
// margins and its rows of ground (the ruler's and the marker lane's foot
// lines, the stereo divider, the row above the trim bar and the row under
// the canvas), the on-screen keyboard's band, the modal family's controls row.
// NO BAKE, NO GRAIN, NO GRADIENT, NO LINE, NO BEVEL, NO SHADOW: every pixel
// of the ground is a solid fill through the palette's chokepoint
// (set_palette_source, render.h), which converts it for the window as it
// converts every color (display_color), so True Colors holds with no cache
// of its own. The palette's `face` and the chrome's `ground` role ARE this
// constant (render.cpp's fill_program_palette, chrome_derive.h). The modal
// family's panel margins are this ground too, its box's outer rows and
// columns included (modal_box.h).
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
// ON a lit face is kTextOnLit 111111 (the keys' Breeze glyphs since
// 2026-11-02, the on-screen keyboard's caps).
//
// THE GROUP GAP (architect 2026-11-01, M5; "two px lines that don't scale",
// M5b): keys that abut inside a group are parted by kKeyGapPx, TWO DEVICE PX
// OF GROUND at every gui_scale — the group's outer extents stay, each key
// giving up one device px on every side that abuts a neighbor
// (key_in_cell): at 300 % a 69-px case reads 68 at a group's two ends, 67
// inside, 69 alone. THE GAP IS NOT PRESSABLE (architect 2026-11-02, R5: "2px
// gap should not be pressable - ground is not a button"): THE PAINTED KEY IS
// THE HIT BOX (point_on_key below), so a press, a lift, the lit look and the
// tooltip answer on the key alone and the ground between two keys answers
// nothing — the on-screen keyboard's gaps alike (R11), under its own gap
// rule (onscreen_keyboard.h).
//
// THE TEXT TONES (architect 2026-11-02, M13, "we use Ableton colors as much
// as possible"): Live's two, kTextInk B5B5B5 on every word, digit and
// drawing the chrome prints — the legends at rest, the logo lane, the
// clocks, the tab name and state line, the lists, the cards, the prompts,
// the editors, the marker labels, the keyboard's caps, the keys' Breeze
// glyphs (2026-11-02) — and kTextDimInk 757575 for a disabled key's legend
// and glyph, the settings page's inert rows and the tooltip's hint line;
// kTextOnLit 111111 on a lit face. NOTHING WEARS A
// SHADOW (2026-11-01, "no shadow anywhere"; the well's cue labels,
// triangles and ticks with the rest since 2026-11-02): every word, digit,
// tick and triangle is ink alone.
//
// THE WELL (architect 2026-11-01 night, "THE MAIN PAGE IS SETTLED", M5h with
// the final lane-40 / clock-39 rulings; M13's text tones 2026-11-02): FLAT
// REGIONS ON THE GROUND, no frame, no ring, no bevel, its columns the keys'
// outer columns (render.h's well_columns, THE EDGE RULE), top to bottom —
// THE TRIM BAR kTrimBarFace 3C3C3C in a one-device-px kWellFrame 757575
// frame on all four sides, the span two one-px kWellFrame verticals frame
// to frame; THE RULER kRulerFace 363636 with the kTextInk digits and the
// kRulerTick 6B6B6B ticks; one row of ground; THE MARKER LANE kLaneFace
// 303030; one row of ground; THE CANVAS (the palette's `waveform_canvas`),
// its stereo divider one row of ground. Every number is Live's, measured on
// his Live 12 shots (the trim bar the arrangement's overview, the ruler and
// its ticks the time ruler, the lane the clip view's marker lane).
// THE TIME FIELDS are flat kKeyFace boxes, no ring (2026-11-01 night, M5m).

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

// THE WELL'S TONES (the head's THE WELL). kWellFrame is THE GRAMMAR'S ONE
// FRAME TONE (architect 2026-11-02, M14 "d"): the trim bar's frame and the
// span's verticals, the scrub's one-px channel (render.h's scrub block) and
// the cards' and the tooltip's one-px border (the chrome's `card_frame`).
inline constexpr uint32_t kTrimBarFace = 0x3C3C3C;
inline constexpr uint32_t kWellFrame   = 0x757575;
inline constexpr uint32_t kRulerFace   = 0x363636;
inline constexpr uint32_t kRulerTick   = 0x6B6B6B;
inline constexpr uint32_t kLaneFace    = 0x303030;

// THE SELECTION PAIR (architect 2026-11-01 night, M5g / M6: Live's browser
// selection): a flat 637E86 bar under 111111 text — the flag editor's and
// the dialog editors' selected substring and the lists' lit row (the
// chrome's `selected_fill` / `selected_text`, chrome_derive.h).
inline constexpr uint32_t kSelectionFill = 0x637E86;
inline constexpr uint32_t kSelectionText = kTextOnLit;

// THE PANE (architect 2026-11-01 night, M6 A; 2026-11-02, M11 / M14): Live's
// panel face, which is the keys' face — ONE CONSTANT, kKeyFace's own value,
// named for what it paints: the modal family's box interior, flat, with no
// frame, ring, shadow or row separator (modal_box.h's head; the score
// viewer's darker pane, the one exception, is recorded there), and the
// cards' and the tooltip's face (the chrome's `card_ground`,
// chrome_derive.h) inside their one-px kWellFrame border (`card_frame`, the
// trim bar's frame tone, paint_popup_chrome).
inline constexpr uint32_t kPaneFace = kKeyFace;

// -- THE KEY'S PAINTERS ------------------------------------------------------

// ONE KEY on `r` (the head's THE KEY): the flat face, the lit fill when
// `lit` — the caller's answer, its checked state OR a standing press — and
// nothing else. Its legend stands under it on the ground (paint_handler.cpp's
// paint_key_legend); what a key carries on its face is its caller's — the
// roster's and the controls rows' Breeze glyph (paint_key_glyph,
// 2026-11-02), the on-screen keyboard's caps — in kTextOnLit on a lit face
// and kTextInk on a resting one (a disabled key's glyph kTextDimInk).
void paint_key(cairo_t* cr, const GuiRect& r, bool lit);

// THE KEY INSIDE ITS CELL (the head's THE GROUP GAP): the cell `cell` less
// one device px on each side that abuts a neighbor of its group —
// `joined_left` / `joined_right` — so two abutting keys stand kKeyGapPx
// apart. The painted key, its legend's seat and its hit box are this.
GuiRect key_in_cell(const GuiRect& cell, bool joined_left, bool joined_right);

// A POINT PRESSES A KEY EXACTLY WHERE THE KEY IS PAINTED (the head's THE
// GROUP GAP; architect 2026-11-02, R5 / R11): `key` the PAINTED key —
// key_in_cell's answer for every grouped key (the band, row 8, the modal
// family's controls rows), the on-screen keyboard's walk rect for its own
// (onscreen_keyboard::for_each_key, its gap already outside it). THE ONE
// PREDICATE every key hit test asks: the roster's face
// (AppState::RedesignButtonFace::contains — the press, the lift, the band's
// modifier gate, the armed press's inside bit, the tooltip's owner), the
// modal family's buttons (GuiInputHandler::modal_dialog_button_hit — the
// press, the walk's inside bit, the lift) and the keyboard's
// (onscreen_keyboard::key_at). Each painter PUBLISHES the key it paints, so
// a reader re-shapes nothing. A gesture already begun keeps its rules: a
// press that slides off its key reads "outside" as ever, the key now being
// what "on" means.
inline bool point_on_key(const GuiRect& key, int x, int y) {
    return rect_contains(key, x, y);
}
