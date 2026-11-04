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
// the canvas, each one W, render.h's hairline_px), the gaps between keys, the
// on-screen keyboard's band, the modal family's controls row.
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
// keyboard's): A FLAT BOX, NO OUTLINE, NO LED, NO GLOW, NO DEPTH, NO PRESS
// MOVEMENT (architect 2026-11-03: the keys stay flat, no offset, no inset
// under a press), in one of FOUR FACES (KeyLook): AT REST kKeyFace 363636;
// PRESSED — a key under the finger or pen for as long as the press stands,
// NOT CHECKED — kPressFace 6A6A6A, its print kTextInk as at rest; LIT —
// kLitFill FFAD56, Live's "on" — A CHECKED KEY, its whole face; LIT AND
// PRESSED — a checked key under a standing press — kLitPressFill FFC281.
// THE PRESS WEARS LIVE'S HOVER (architect 2026-11-03, the retrofit: a pen
// cannot hover, so Live's hover colors are the press — 6A6A6A its off
// button's hover, FFC281 its hover of FFAD56; Live's transport keys' grow
// under hover is not taken). THE YELLOW IS FOR LAMPS ONLY (architect
// 2026-11-02, on mocks a3 / b3: "the yellow flash is jarring … keep the
// yellow only for lamps"): a press never lights an unchecked key. Every key
// returns to its own state at the lift. A DISABLED key keeps the face its
// state gives, its glyph and its legend in kKeyDimInk, so a checked-but-inert
// key under an overlay keeps its lit fill (architect 2026-11-01, M6) — its
// glyph on the lit face kTextOnLit. Anything printed ON a lit face is
// kTextOnLit 111111 (the keys' Breeze glyphs, the on-screen keyboard's
// caps); on a resting or pressed face kTextInk (key_ink below). UNDER EVERY
// KEY ITS PRINTED LEGEND (architect 2026-11-03, the retrofit, reversing the
// legends' retirement of 2026-11-02): on the ground in the strip's bottom
// pad, never on the face, so a lit key's legend stays kTextInk (the rule at
// paint_handler.cpp's key_legend_seat).
//
// THE FACE AND ITS GAP (architect 2026-11-04, the unit: his "1 agree" to
// 69 x 33 and "spacing 3 dpx"; "grow the box instead of adding a border";
// the hi-fi key metrics of 2026-11-03's retrofit): THE KEY IS ITS PAINTED
// FACE, render.h's case — 69 device px wide at 300 % by the `key_height`
// tuning key (icon_case_w_px / icon_case_h_px), 69 x 33 at the default — NO
// OUTLINE, NO INSET, and TWO KEYS OF A GROUP STAND ONE W OF GROUND APART
// (render.h's key_gap_px: 3 device px at 300 %, 2 at 200 %, 1 at 138 %), the
// walk stepping the face and the gap (key_group_pitch_px below), so every
// face is the same, a group's ends included. THE EDGE RULE IS THE FACES':
// the band's, row 8's and the controls rows' outermost FACES stand on the
// well's first and last columns (render.h's well_columns, the waveform
// area's, architect 2026-11-03; the seats' walks, paint_handler.cpp). THE
// GROUND IS NOT PRESSABLE (architect 2026-11-02, R5: "ground is not a
// button"): THE PAINTED FACE IS THE HIT BOX (point_on_key below), so a
// press, a lift, the press face and the tooltip answer on the face alone and
// the ground round it — the gaps, the run gaps, the legends' pad — answers
// nothing; the on-screen keyboard's gaps alike (R11), the same gap
// (onscreen_keyboard.h's key_gap_px, 2026-11-04).
//
// THE TEXT TONES (architect 2026-11-02, M13, "we use Ableton colors as much
// as possible"): Live's two, kTextInk B5B5B5 on every word, digit and
// drawing the chrome prints — the logo lane, the
// clocks, the tab name and state line, the lists, the cards, the prompts,
// the editors, the marker labels, the keyboard's caps, the keys' Breeze
// glyphs (2026-11-02) and their legends (2026-11-03) — and kTextDimInk
// 757575 for the settings page's inert rows and the tooltip's hint line;
// kTextOnLit 111111 on a lit face. A DISABLED KEY'S glyph and legend take
// their own tone, kKeyDimInk 5A5A5A (architect 2026-11-02, on mock b3: "the
// disabled icons are too similar to enabled ones", 757575 reading too close
// to B5B5B5 at a key's size), darker than Live's dim text so a dead key
// reads dead at a
// glance; the lists' and the tooltip's dim print keep 757575. NOTHING WEARS A
// SHADOW (2026-11-01, "no shadow anywhere"; the well's cue labels,
// triangles and ticks with the rest since 2026-11-02): every word, digit,
// tick and triangle is ink alone.
//
// THE WELL (architect 2026-11-01 night, "THE MAIN PAGE IS SETTLED", M5h;
// M13's text tones 2026-11-02; the text round's lengths 2026-11-03): FLAT
// REGIONS ON THE GROUND, no frame, no ring, no bevel, its columns the keys'
// faces' outer columns (render.h's well_columns, THE EDGE RULE), top to
// bottom — THE TRIM BAR two flat fills, no frame (architect 2026-11-03,
// mock z2): kTrimBarFace 2A2A2A outside the trim, kTrimSpanFace 6A6A6A on
// the span, one row of ground under them; THE RULER kRulerFace 363636 with
// the kTextInk digits and the kRulerTick 6B6B6B ticks; one row of ground;
// THE MARKER LANE kLaneFace 2A2A2A (architect 2026-11-03, the retrofit:
// Live's next real fill above the 242424 ground); one row of ground; THE
// CANVAS (the palette's `waveform_canvas`), its stereo divider one row of
// ground. Every number is Live's, measured on his Live 12 shots (the trim
// bar the arrangement's overview, the ruler and its ticks the time ruler,
// the lane the clip view's marker lane).
// THE TIME FIELDS are flat kKeyFace boxes, no ring (2026-11-01 night, M5m).

// THE GROUND (the head).
inline constexpr uint32_t kGround = 0x242424;

// THE TEXT TONES (the head).
inline constexpr uint32_t kTextInk    = 0xB5B5B5;
inline constexpr uint32_t kTextDimInk = 0x757575;
inline constexpr uint32_t kTextOnLit  = 0x111111;
// A disabled key's glyph (the head's THE TEXT TONES, 2026-11-02).
inline constexpr uint32_t kKeyDimInk  = 0x5A5A5A;

// THE KEY (the head): its face at rest, its press face (Live's off-button
// hover 6A6A6A, 2026-11-03), its lit fill (Live's "on" orange FFAD56, one
// constant with no device key), the lit fill under a press (Live's hover of
// FFAD56, FFC281, 2026-11-03). (The face's lengths and the gap between two
// faces, key_gap_px, are render.h's case block.)
inline constexpr uint32_t kKeyFace      = 0x363636;
inline constexpr uint32_t kPressFace    = 0x6A6A6A;
inline constexpr uint32_t kLitFill      = 0xFFAD56;
inline constexpr uint32_t kLitPressFill = 0xFFC281;

// THE KEY'S FOUR FACES (the head's THE KEY): LitPressed when checked under a
// standing press, Lit when checked, Pressed under a standing press, else
// Rest. key_look is the one place the order is decided — every key painter
// asks it.
enum class KeyLook { Rest, Pressed, Lit, LitPressed };
inline constexpr KeyLook key_look(bool checked, bool pressed) {
    return checked ? (pressed ? KeyLook::LitPressed : KeyLook::Lit)
         : pressed ? KeyLook::Pressed
                   : KeyLook::Rest;
}
inline constexpr bool key_look_lit(KeyLook look) {
    return look == KeyLook::Lit || look == KeyLook::LitPressed;
}

// THE INK OF WHAT A KEY CARRIES ON ITS FACE (the head's THE KEY and THE
// TEXT TONES): kTextOnLit on a lit face, pressed or not (a dead checked
// key's too), else kTextInk, or kKeyDimInk on a disabled key.
inline constexpr uint32_t key_ink(KeyLook look, bool enabled) {
    return key_look_lit(look) ? kTextOnLit : enabled ? kTextInk : kKeyDimInk;
}

// THE WELL'S TONES (the head's THE WELL). kWellFrame is THE GRAMMAR'S ONE
// FRAME TONE (architect 2026-11-02, M14 "d"): the scrub's one-W channel
// (render.h's scrub block) and the cards' and the tooltip's one-W border
// (the chrome's `card_frame`). THE TRIM BAR'S TWO FILLS (architect
// 2026-11-03, mock z2; render_trim_flags): kTrimBarFace, the bar outside
// the trim, IS THE LANE'S TONE — ONE CONSTANT, kLaneFace's own value (Live's
// next fill above the ground), named for what it paints as kPaneFace is
// kKeyFace's; kTrimSpanFace 6A6A6A, the span, Live's off-button hover — the
// press face's value, kept its own constant because it is another role (a
// span, not a press), so a retuned press leaves the bar alone.
inline constexpr uint32_t kWellFrame    = 0x757575;
inline constexpr uint32_t kRulerFace    = 0x363636;
inline constexpr uint32_t kRulerTick    = 0x6B6B6B;
inline constexpr uint32_t kLaneFace     = 0x2A2A2A;
inline constexpr uint32_t kTrimBarFace  = kLaneFace;
inline constexpr uint32_t kTrimSpanFace = 0x6A6A6A;

// THE SELECTION PAIR (architect 2026-11-01 night, M5g / M6: Live's browser
// selection): a flat 637E86 bar under 111111 text — the flag editor's and
// the dialog editors' selected substring and the lists' lit row (the
// chrome's `selected_fill` / `selected_text`, chrome_derive.h). ITS LIGHTER
// TWIN, kSelectionPressFill B0DDEB (Live's focused selection; architect
// 2026-11-03, the retrofit: the press rule's twin for rows), is A LIST ROW
// UNDER A STANDING PRESS — the picker's, the render player's list, the
// settings page and its value list, the trim presets — under the same
// 111111 text, the highlighted row at rest keeping the pair
// (paint_handler.cpp's paint_folder_overlay, AppState::FolderOverlayPress, app.folder_overlay.press).
inline constexpr uint32_t kSelectionFill      = 0x637E86;
inline constexpr uint32_t kSelectionPressFill = 0xB0DDEB;
inline constexpr uint32_t kSelectionText      = kTextOnLit;

// THE PANE (architect 2026-11-01 night, M6 A; 2026-11-02, M11 / M14): Live's
// panel face, which is the keys' face — ONE CONSTANT, kKeyFace's own value,
// named for what it paints: the modal family's box interior, flat, with no
// frame, ring, shadow or row separator (modal_box.h's head; the score
// viewer's darker pane, the one exception, is recorded there), and the
// cards' and the tooltip's face (the chrome's `card_ground`,
// chrome_derive.h) inside their one-W kWellFrame border (`card_frame`, the
// grammar's one frame tone, paint_popup_chrome).
inline constexpr uint32_t kPaneFace = kKeyFace;

// -- THE KEY'S PAINTERS ------------------------------------------------------

// ONE KEY on `r`, ITS PAINTED FACE — the key itself (the head's THE KEY and
// THE FACE AND ITS GAP): the flat face `look` gives — kKeyFace at rest,
// kPressFace pressed, kLitFill lit, kLitPressFill lit under a press
// (key_look, the caller's checked state and standing press) — and nothing
// else. What a key carries is its caller's — the roster's and the controls
// rows' Breeze glyph on the face and its legend under it (paint_key_glyph
// and paint_key_legend, paint_handler.cpp), the on-screen keyboard's caps —
// in key_ink's tone.
void paint_key(cairo_t* cr, const GuiRect& r, KeyLook look);

// THE GROUP'S PITCH (the head's THE FACE AND ITS GAP): the face and one key
// gap — the step from a key's face to the next one's in its group, every
// group walk's (the band's icon_row_seats, row 8's row8_block_walk, the
// modal family's controls-row layouts, paint_handler.cpp): 72 at 300 % (69
// + 3), 48 at 200 % (46 + 2). The ground between two GROUPS is the run gap
// (render.h's program_run_gap_px), between their faces.
inline int key_group_pitch_px() {
    return icon_case_w_px() + key_gap_px();
}
// A GROUP'S WIDTH: `n` faces and the n − 1 gaps between them (0 for none),
// from its first face's first column to one past its last face's last.
inline int key_group_w_px(int n) {
    return n > 0 ? n * icon_case_w_px() + (n - 1) * key_gap_px() : 0;
}

// A POINT PRESSES A KEY EXACTLY WHERE THE KEY IS PAINTED (the head's THE
// FACE AND ITS GAP; architect 2026-11-02, R5 / R11): `key` the PAINTED
// face — the walk's own rect for every grouped key (the band, row 8, the
// modal family's controls rows), the on-screen keyboard's walk rect for its
// own (onscreen_keyboard::for_each_key, its gap already outside it). THE
// ONE PREDICATE every key hit test asks: the roster's face
// (AppState::RedesignButtonFace::contains — the press, the lift, the band's
// modifier gate, the armed press's inside bit, the tooltip's owner), the
// modal family's buttons (GuiInputHandler::modal_dialog_button_hit — the
// press, the walk's inside bit, the lift) and the keyboard's
// (onscreen_keyboard::key_at). Each painter PUBLISHES the face it paints, so
// a reader re-shapes nothing. A gesture already begun keeps its rules: a
// press that slides off its key reads "outside" as ever, the face being what
// "on" means.
inline bool point_on_key(const GuiRect& key, int x, int y) {
    return rect_contains(key, x, y);
}
