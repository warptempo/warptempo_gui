#pragma once

#include "render.h"           // GuiRect, GuiColor, palette(), the chokepoints

#include <cairo/cairo.h>
#include <cstdint>

// THE MATERIALS — THE FLAT LOOK (architect 2026-11-01, reversing the hi-fi
// panel of 2026-10-23: "whatever ruling it needs to, we need to reverse to go
// flat"; Live's grammar, every number measured on his Live 12 shots at
// 100 %). This header is the one owner of the ground's, the key's, the lit
// fill's, the text tones', the well's tones' and the pane's RULES; THEIR
// NUMBERS ARE THE INSTALLED THEME'S since 2026-11-05 (the theme arc: Live
// 12's own themes selectable whole through the `theme` device key,
// palette_file.h's head) — every painter reads palette()'s member for the
// role (render.h's GuiPalette; the chrome's role table, theme_file.h), and
// every number quoted below is DEFAULT'S, Live's "Default Dark Neutral
// Medium", with the Live role it comes from (the derivation,
// theme_tables.h's head). The constants that held them (kGround, kKeyFace,
// kTextInk and the rest, 2026-11-01..05) are retired: nothing outside the
// installed palette names a color.
//
// THE GROUND IS ONE FLAT COLOR, `ground` — Default's 242424 (SurfaceArea) —
// everywhere outside the well's regions and the keys: the band and its logo
// lane, row 8, the well's side margins and its rows of ground (the ruler's
// and the marker lane's foot lines, the stereo divider, the row above the
// trim bar and the row under the canvas, each one W, render.h's
// hairline_px), the gaps between keys, the on-screen keyboard's band, the
// modal family's controls row.
// NO BAKE, NO GRAIN, NO GRADIENT, NO LINE, NO BEVEL, NO SHADOW: every pixel
// of the ground is a solid fill through the palette's chokepoint
// (set_palette_source, render.h), which converts it for the window as it
// converts every color (display_color), so True Colors holds with no cache
// of its own. The palette's `face` and the chrome's `ground` role ARE this
// tone (render.cpp's install_palette, theme_file.h). The modal family's
// panel margins are this ground too, its box's outer rows and columns
// included (modal_box.h).
//
// THE KEY (paint_key below; every key alike — the band's, row 8's, the
// controls rows', the word buttons, the scrub's thumb, the on-screen
// keyboard's): A FLAT BOX, NO OUTLINE, NO LED, NO GLOW, NO DEPTH, in one of
// FOUR FACES (KeyLook): AT REST `key_face`, Default's 363636
// (SurfaceBackground); UNDER PRESS `key_press_face`, Default's 2A2A2A
// (Desktop) — DARKER THAN THE FACE, the push in; LIT — `lit_fill`,
// Default's FFAD56 (ViewCheckControlEnabledOn), Live's "on" — A CHECKED
// KEY, its whole face; LIT UNDER PRESS — `lit_press_fill`, Default's
// C68743, the lit fill scaled by the press face's ratio to the resting
// face (42 / 54 = 0.778 on Default; the derivation, theme_tables.h's
// head) — a checked key while a press stands on it.
// THE YELLOW IS FOR LAMPS ONLY (architect 2026-11-02, on mocks a3 / b3: "the
// yellow flash is jarring … keep the yellow only for lamps").
// THE DOORS WEAR LAVENDER (architect 2026-11-05, his "yes" on mock s46_m2 of
// tmp/s46): THE SEVEN OVERLAY OPENERS — HISTORY, OPEN PROJECT, PLAY RENDERS,
// SETTINGS, TRIM PRESETS, BPM and SCORE (redesign_button_is_door,
// app_state.h, the class's one owner) — are a CLASS OF THEIR OWN among the
// checked keys: a lit door says A SURFACE STANDS (its overlay, which the same
// key closes), a lit lamp says A MODE IS ON, and the two facts wear two
// fills. A checked door wears `door_lit_fill`, Default's B595FC
// (ScaleAwareness), and under a press `door_lit_press_fill`, Default's
// 8D74C4 — the door fill by THE SAME RATIO the lamps' lit press takes
// (theme_tables.h's head). Every other checked key — the state lamps
// (FOLLOW, RESTRICT, MAGNIFY, NOISE, Read Only, Tooltips, the walk lamp,
// Cumulative, the iteration lamp, Value Drag, Add to Selection), the view
// pairs' lit members, the on-screen keyboard's latched Shift and symbols key
// — keeps `lit_fill` / `lit_press_fill`. What is printed on a door-lit face
// is `text_on_lit` still (key_ink: lit is lit). THE PAINTER LEARNS THE CLASS
// AT ONE SITE: paint_key's `KeyLitClass` (below), read for the two lit looks
// alone, the roster's painter passing the predicate's answer
// (paint_roster_case, paint_handler.cpp); key_look stays four-valued.
// THE PRESS IS DARKER (architect 2026-11-05, the press arc, his pick p2 of
// tmp/s45 over p1's ControlBackground 1E1E1E, reversing 2026-11-04's "no
// press look anywhere" — that look was Live's HOVER tones, 6A6A6A / FFC281,
// lighter than the face, and it read as a blink under the pen; his reason:
// "hover is not press - press in skeumorphic is darker. but a list is not a
// button so going lighter seems ok - darker for button (push in) lighter
// for list (highlight)"): AN UNCHECKED KEY UNDER A STANDING PRESS wears the
// press face on EVERY key surface — the band, row 8, the controls rows,
// the render player, the score viewer, the trim presets, the dialogs' word
// buttons, the scrub's thumb and the on-screen keyboards' keys — from the
// press on the face to the lift or the slide-off, exactly the press arm's
// own "on" (point_on_key below: the feint's inside bit, the face returning
// to rest while the pointer is off it and pressing again when it slides
// back; the on-screen keyboard's key and the scrub's thumb, which act at
// the press or carry the finger, from the press to the lift). A LIT KEY
// UNDER PRESS DARKENS BY THE SAME RATIO (architect 2026-11-05, the press
// arc's second landing, his "ratio" on mocks d1 / i1 of tmp/s45 — "we could
// darken the lit key by the relationship ... so the lit press is derived";
// the first landing had left it unchanged): the lit fill times mean(press
// face) / mean(resting face), per channel, the one derived word
// `lit_press_fill`, computed by the theme tool and not at paint time — the
// ratio over the difference because it keeps the theme's lit hue and scales
// with the theme's own press step (the tool's head); what it carries stays
// `text_on_lit` (key_look_lit below). A DISABLED KEY UNDER PRESS IS
// UNCHANGED (his "disable no press is right"): a dead key arms nothing
// (every press arm refuses a painted-dead face), so no press stands on it.
// What a key carries keeps its ink on the press face: the glyph, the cap
// and the legend `text` (key_ink below), the legend being on the ground.
// THE LISTS' ROWS go the other way, lighter (THE SELECTION PAIR, below).
// NO TIMING, NO HOLD, NO ANIMATION: the face follows the press bit as
// painted; the press arms — the slide-off feint, the hold-repeat, the long
// press and the barrel — are unchanged. THE OTHER FEEDBACK STAYS: the act
// itself, a stateful key's glyph showing the act its next press runs, the
// typed character, the selection pair landing on a row. A DISABLED key
// keeps the face its state gives, its glyph and its legend in
// `key_dim_ink`, so a checked-but-inert key under an overlay keeps its lit
// fill (architect 2026-11-01, M6) — its glyph on the lit face
// `text_on_lit`. Anything printed ON a lit face is `text_on_lit`, Default's
// 111111 (ControlContrastFrame; the keys' Breeze glyphs, the on-screen
// keyboard's caps); on a resting or pressed face `text` (key_ink below).
// UNDER EVERY KEY ITS
// PRINTED LEGEND (architect 2026-11-03, the retrofit, reversing the legends'
// retirement of 2026-11-02): on the ground in the strip's bottom pad, never
// on the face, so a lit key's legend stays `text` (the rule at
// paint_handler.cpp's key_legend_seat).
//
// THE FACE AND ITS GAP (architect 2026-11-04, the unit: his "1 agree" to
// the hi-fi's face and "grow the box instead of adding a border"; the hi-fi
// key metrics of 2026-11-03's retrofit; the unit of 2 that night, mock v4:
// the face 70 x 34 and the gap 2, the pitch 72 kept): THE KEY IS ITS
// PAINTED FACE, render.h's case — 35 W wide by the `key_height` tuning key
// (icon_case_w_px / icon_case_h_px), 35 x 17 W at the default, 70 x 34
// device px on the tablet at 200 % — NO OUTLINE, NO INSET, and TWO KEYS OF
// A GROUP STAND ONE W OF GROUND APART (render.h's key_gap_px: 2 device px
// on the tablet, 1 at 100 %), the
// walk stepping the face and the gap (key_group_pitch_px below), so every
// face is the same, a group's ends included. THE EDGE RULE IS THE FACES':
// the band's, row 8's and the controls rows' outermost FACES stand on the
// well's first and last columns (render.h's well_columns, the waveform
// area's, architect 2026-11-03; the seats' walks, paint_handler.cpp). THE
// GROUND IS NOT PRESSABLE (architect 2026-11-02, R5: "ground is not a
// button"): THE PAINTED FACE IS THE HIT BOX (point_on_key below), so a
// press, a lift and the tooltip answer on the face alone and
// the ground round it — the gaps, the run gaps, the legends' pad — answers
// nothing; the on-screen keyboard's gaps alike (R11), the same gap
// (onscreen_keyboard.h's key_gap_px, 2026-11-04).
//
// THE TEXT TONES (architect 2026-11-02, M13, "we use Ableton colors as much
// as possible"): Live's two — `text`, Default's B5B5B5 (ControlForeground),
// on every word, digit and drawing the chrome prints — the logo lane, the
// clocks, the tab name and state line, the lists, the prompts, the
// editors, the marker labels, the keyboard's caps, the keys' Breeze glyphs
// (2026-11-02) and their legends (2026-11-03) — and `text_dim`, Default's
// 757575 (TextDisabled), for the settings page's inert rows and the
// tooltip's hint line; `text_on_lit`, Default's 111111, on a lit face. THE
// CARDS' words and glyph wear their own ink, `card_ink`, Default's 2A2A2A
// (Desktop) on Live's orange (THE CARDS, below; 2026-11-04 night). A
// DISABLED KEY'S glyph and legend take their own tone, `key_dim_ink`,
// Default's 5A5A5A (architect 2026-11-02, on mock b3: "the disabled icons
// are too similar to enabled ones", 757575 reading too close to B5B5B5 at a
// key's size), darker than Live's dim text so a dead key reads dead at a
// glance — in every theme the face mixed 36/63 toward the dim text, the
// Default relation kept; the lists' and the tooltip's dim print keep
// `text_dim`. NOTHING WEARS A SHADOW (2026-11-01, "no shadow anywhere"; the
// well's cue labels, triangles and ticks with the rest since 2026-11-02):
// every word, digit, tick and triangle is ink alone.
//
// THE WELL (architect 2026-11-01 night, "THE MAIN PAGE IS SETTLED", M5h;
// M13's text tones 2026-11-02; the text round's lengths 2026-11-03): FLAT
// REGIONS ON THE GROUND, no frame, no ring, no bevel, its columns the keys'
// faces' outer columns (render.h's well_columns, THE EDGE RULE), top to
// bottom — THE TRIM BAR two flat fills, no frame (architect 2026-11-03,
// mock z2): `trim_bar_face`, Default's 2A2A2A, outside the trim,
// `trim_span_face`, Default's 6A6A6A, on the span, one row of ground under
// them; THE RULER `ruler_face`, Default's 363636, with the `text` digits and
// the `ruler_tick` ticks, Default's 6B6B6B; one row of ground; THE MARKER
// LANE `lane_face`, Default's 2A2A2A (architect 2026-11-03, the retrofit:
// Live's next real fill above the 242424 ground — Desktop); one row of
// ground; THE CANVAS (the palette's `waveform_canvas`), its stereo divider
// one row of ground. Every Default number is Live's, measured on his Live
// 12 shots (the trim bar the arrangement's overview, the ruler and its
// ticks the time ruler, the lane the clip view's marker lane). Where a
// theme makes the lane, the trim's outside or the dim text coincide with
// the ground, that is Live's own number — ACCEPTED, not a bug, no backstop
// (architect 2026-11-05).
// THE TIME FIELDS are flat `key_face` boxes, no ring (2026-11-01 night,
// M5m; cool_edit_paint.h's paint_ce_time_field).
//
// THE WELL'S FRAME TONE. `well_frame` is THE GRAMMAR'S ONE FRAME TONE
// (architect 2026-11-02, M14 "d"), Default's 757575 (TextDisabled, the dim
// text's own role): the scrub's one-W channel (render.h's scrub block) and
// the tooltip's one-W border (paint_shift_tooltip; the cards wear
// `card_face` instead since 2026-11-04 night). THE TRIM BAR'S TWO FILLS
// (architect 2026-11-03, mock z2; render_trim_flags): `trim_bar_face`, the
// bar outside the trim, IS THE LANE'S TONE — the same Live role (Desktop),
// named for what it paints as `pane_face` is `key_face`'s; `trim_span_face`
// Live's off-button hover in Default, in every theme the face mixed 52/127
// toward the text (the ticks 53/127).
//
// THE SELECTION PAIR (architect 2026-11-01 night, M5g / M6: Live's browser
// selection): a flat bar in `selection_fill`, Default's 637E86
// (StandbySelectionBackground), under `selection_text`, Default's 111111
// (ControlContrastFrame) — the flag editor's and the dialog editors'
// selected substring and the lists' highlighted row (the chrome's
// `selected_fill` / `selected_text`, theme_file.h). THE LIST ROW UNDER A
// PRESS IS LIGHTER (architect 2026-11-05, the press arc: "a list is not a
// button so going lighter seems ok … lighter for list (highlight)"): the
// row a press stands on — the feint's inside bit, FolderOverlayPress's —
// wears `list_press_fill`, Default's B0DDEB (SelectionBackground, Live's
// focused selection), under `list_press_text`, Default's 070707
// (SelectionForeground), its glyph in that text too, in every list of the
// folder overlay (the picker, the settings page and its value lists, the
// trim presets, the render player's list); the highlighted row keeps the
// standby pair at rest and shows the press pair while a press stands on
// it; an inert row (Group, Disabled) shows no press — its press arms the
// scroll drag alone; a press that has become the scroll drag presses no
// row (paint_handler.cpp's paint_folder_overlay).
//
// THE PANE (architect 2026-11-01 night, M6 A; 2026-11-02, M11 / M14): Live's
// panel face, which is the keys' face — `pane_face`, THE SAME LIVE ROLE as
// `key_face` (SurfaceBackground), named for what it paints: the modal
// family's box interior, flat, with no frame, ring, shadow or row separator
// (modal_box.h's head; the score viewer's darker pane, `score_pane_face`,
// the one exception, is recorded there), and the tooltip's face inside its
// one-W `well_frame` border (the grammar's one frame tone;
// paint_shift_tooltip through paint_popup_chrome). The cards wear
// `card_face` (below).
//
// THE CARDS (architect 2026-11-04 night, his pick h1 of tmp/s38 over h0, the
// tooltip's pane and frame they wore until then, and h2, the orange under
// 111111 ink): every notification card, both classes alike, is A FLAT BOX in
// `card_face`, Default's E76942 — LIVE'S STATUS BAR'S ORANGE (Alert),
// measured on his Live 12 shot of 2026-10-31 (231 105 66) — its words and
// its class glyph in `card_ink`, Default's 2A2A2A, Live's own text on that
// bar (42 42 42; Desktop), and NO VISIBLE BORDER: the one-W ring's rows and
// columns stay in the layout, painted in the fill (the chrome's
// `card_ground`, `card_text` and `card_frame`, theme_file.h; render.h's THE
// CARD FACE). NOT THE LAMP'S FFAD56: "the yellow is for lamps only"
// (2026-11-02) stands — this is Live's status-bar orange, a different tone
// with a different job, a message rather than a state.

// THE KEY'S FOUR FACES (the head's THE KEY): LitPress when checked with a
// press standing on the key (the press arc's second landing, architect
// 2026-11-05); else Lit when checked; else Press while a press stands on
// the key (the press arc, architect 2026-11-05); else Rest. key_look is the
// one place the face is decided —
// every key painter asks it with its own pressed bit (paint_roster_case and
// the modal row, paint_onscreen_keyboard, paint_handler.cpp;
// paint_scrub_thumb, render.cpp).
enum class KeyLook { Rest, Press, Lit, LitPress };
inline constexpr KeyLook key_look(bool checked, bool pressed) {
    return checked ? (pressed ? KeyLook::LitPress : KeyLook::Lit)
         : pressed ? KeyLook::Press
                   : KeyLook::Rest;
}
// A LIT FACE, pressed or not: what is printed on it is `text_on_lit`
// (key_ink below) — the darkened lit press fill is the lamp's fill still.
inline constexpr bool key_look_lit(KeyLook look) {
    return look == KeyLook::Lit || look == KeyLook::LitPress;
}
// WHICH LIT FILL A CHECKED KEY WEARS (the head's THE DOORS WEAR LAVENDER,
// architect 2026-11-05): Lamp — `lit_fill` / `lit_press_fill`, every checked
// key but the seven; Door — `door_lit_fill` / `door_lit_press_fill`, the
// seven overlay openers (redesign_button_is_door, app_state.h, the one
// owner of the roster). Read by paint_key for KeyLook::Lit and LitPress
// alone; Rest and Press ignore it. Lamp is the default, so every key
// surface that paints no door — the modal rows, the on-screen keyboard, the
// scrub's thumb — asks nothing.
enum class KeyLitClass { Lamp, Door };

// THE INK OF WHAT A KEY CARRIES ON ITS FACE (the head's THE KEY and THE
// TEXT TONES), as the installed theme's sRGB word (the Breeze glyph road
// takes the word, icons::draw_glyph): `text_on_lit` on a lit face (a dead
// checked key's too), else `text` — on the press face as on the resting
// one — or `key_dim_ink` on a disabled key.
inline uint32_t key_ink(KeyLook look, bool enabled) {
    const GuiThemeTones& t = installed_theme().tones;
    return key_look_lit(look) ? t.text_on_lit
                              : enabled ? t.text : t.key_dim_ink;
}

// -- THE KEY'S PAINTERS ------------------------------------------------------

// ONE KEY on `r`, ITS PAINTED FACE — the key itself (the head's THE KEY and
// THE FACE AND ITS GAP): the flat face `look` gives — palette().key_face at
// rest, palette().key_press_face under a press, lit and lit under a press
// the fill `cls` names (KeyLitClass above: the lamps' lit_fill /
// lit_press_fill, the doors' door_lit_fill / door_lit_press_fill; key_look,
// the caller's checked and pressed bits) — and nothing else. What a key carries is its caller's — the roster's and the
// controls rows' Breeze glyph on the face and its legend under it
// (paint_key_glyph and paint_key_legend, paint_handler.cpp), the on-screen
// keyboard's caps — in key_ink's tone.
void paint_key(cairo_t* cr, const GuiRect& r, KeyLook look,
               KeyLitClass cls = KeyLitClass::Lamp);

// THE GROUP'S PITCH (the head's THE FACE AND ITS GAP): the face and one key
// gap — the step from a key's face to the next one's in its group, every
// group walk's (the band's icon_row_seats, row 8's row8_block_walk, the
// modal family's controls-row layouts, paint_handler.cpp): 36 W, 72 device
// px on the tablet at 200 % (70 + 2), 36 at 100 % (35 + 1). The ground
// between two GROUPS is the run gap
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
// modifier gate, the tooltip's owner), the
// modal family's buttons (GuiInputHandler::modal_dialog_button_hit — the
// press and the lift) and the keyboard's
// (onscreen_keyboard::key_at). Each painter PUBLISHES the face it paints, so
// a reader re-shapes nothing. A gesture already begun keeps its rules: a
// press that slides off its key reads "outside" as ever, the face being what
// "on" means.
inline bool point_on_key(const GuiRect& key, int x, int y) {
    return rect_contains(key, x, y);
}
