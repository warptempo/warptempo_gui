#pragma once

#include "render.h"           // GuiRect, GuiColor, palette(), the chokepoints

#include <algorithm>
#include <cairo/cairo.h>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

// THE MATERIALS — THE FLAT LOOK (architect 2026-11-01, reversing the hi-fi
// panel of 2026-10-23: "whatever ruling it needs to, we need to reverse to go
// flat"; Live's grammar, every number measured on his Live 12 shots at
// 100 %). This header is the one owner of the ground's, the key's, the lit
// face's, the text tones', the well's tones' and the pane's RULES; THEIR
// NUMBERS ARE THE INSTALLED THEME'S since 2026-11-05 (the theme arc: Live
// 12's own themes selectable whole through the `theme` device key,
// palette_file.h's head) — every painter reads palette()'s member for the
// role (render.h's GuiPalette; the chrome's role table, theme_file.h), and
// every number quoted below is DEFAULT'S, Live's "Default Dark Neutral
// Medium" (the compiled reference row, gui_theme.h), with the Live role it
// comes from (the derivation, theme_roles.h's head). The constants that held them
// (kGround, kKeyFace, kTextInk and the rest, 2026-11-01..05) are retired:
// nothing outside the installed palette names a color.
//
// TWO GROUNDS (architect 2026-11-18 / 2026-11-19, send Z; DESIGN §2): THE
// TITLE BAR ALONE wears the title tone `ground` — Default's 242424
// (SurfaceArea; arc_dark 2F343F, Arc's $header_bg): "the darkest color
// would only apply to the top line, the title bar" (paint_title_bar) — and
// EVERYTHING ELSE OUTSIDE THE WELL'S REGIONS AND THE KEYS wears THE BODY
// TONE `body_face` — Default's 242424 too (a Live theme's SurfaceArea; our
// own role WindowBackground; arc_dark 383C4A, Arc's $bg_color): both icon
// rows and their pads, the gaps between keys, the status bar, the erase
// (render_background), the on-screen keyboard's band, the modal family's
// panel and controls row. The well's lines — under the top icon row, the
// trough, the ruler and the marker lane, the stereo divider, the well's
// foot and the status bar's line — are `well_border`, each one W
// (render.h's hairline_px). NO BAKE, NO GRAIN, NO GRADIENT, NO BEVEL, NO
// SHADOW: every pixel of either ground is a solid fill through the
// palette's chokepoint (set_palette_source, render.h), which converts it for
// the window as it converts every color (display_color), so True Colors
// holds with no cache of its own. The palette's `face` IS THE BODY TONE
// (render.cpp's install_palette), the chrome's `ground` role the title tone
// (theme_file.h). (The flag editor's field keeps `ground` until send Z's
// stage D restyles the entries.)
//
// THE KEY IS ARC'S BUTTON (architect 2026-11-18 / 2026-11-19, send Z;
// DESIGN §3, the approved pictures Z_01 / Z_02 and the mock kit's
// linked_group the reference; G3 `button`, `.linked`; paint_linked_group,
// paint_word_button and paint_entry below): every key of the band, the
// bottom icon row and the controls rows — A BOX 36 x 28 W at the defaults
// (the `key_width` x `key_height` tuning keys, render.h's case block; 72 x
// 56 device px on the tablet at 200 %), BORDER INCLUDED ("the border is
// clickable"), in A ONE-W `well_border` RING (Default 2A2A2A, ClipBorder;
// arc_dark 2B2E39) WITH 3 W OUTER CORNERS (kKeyCornerW, the faces' 2 W
// inside it), its face `key_face` (Default 363636, SurfaceBackground;
// arc_dark 444A58), every edge at exact coverage. THE KEYS OF ONE GROUP ARE
// ARC'S LINKED BUTTONS: neighbors SHARE ONE SEAM COLUMN, one ring round the
// group, its four outer corners rounded, the seam wearing the face at rest —
// no divider line (key_link_pitch_px / linked_group_w_px below). THE
// DRAWING the set's 16 W canvas centered on the box (paint_key_glyph,
// paint_handler.cpp). NO LED, NO GLOW, NO DEPTH, NO SHADOW.
// THE STATES ARE ARC'S (key_state below; architect 2026-11-19: "Arc's own
// answer", superseding the darker press of 2026-11-05 on every key but the
// two that await stage D below; "we can let go of the green"; "lit but
// inert: let's do arcs numbers"): REST the face under the drawing in
// `text`; PRESSED, LIT AND LIT UNDER PRESS ONE LOOK — `:active` and
// `:checked` are one in Arc — THE ACCENT `cue` (Default FFAD56,
// ViewCheckControlEnabledOn; arc_dark 5294E2, Arc's selection blue) OVER THE
// KEY'S OWN COLUMNS BETWEEN THE GROUP'S BORDER ROWS, both its seams
// included, the border kept `well_border`, the drawing in `text_on_lit`
// (Default 070707, StandbySelectionForeground; arc_dark FFFFFF);
// DISABLED the face `key_dim_face` (the key face at 55 % over the body
// tone), the border `key_dim_border` (the well's border at 55 % over it) on
// the key's own stretch of the ring, the drawing `key_dim_ink`; LIT AND
// INERT — checked and disabled — the face `lit_dim_fill` under
// `text_on_lit_dim` (Arc's button:disabled:checked under the button's
// opacity; theme_roles.h's DERIVED block). A SEAM BETWEEN TWO KEYS OF
// DIFFERENT STATES WEARS THE LEFT KEY'S FACE, a lit key taking both its
// seams (the kit's paint order, paint_linked_group). THE ACCENT IS
// INSTALLED MIXED TOWARD THE GROUND at the `bright_mix` key, its inert face
// derived from the mixed word (theme_roles.h's THE BRIGHT MIX).
// EVERY CHECKED KEY WEARS THE ONE ACCENT: the state lamps (MAGNIFY, Read
// Only, Tooltips, the walk lamp, Cumulative, the iteration lamp, Value
// Drag), the render player's Repeat One, THE DOORS, the seven overlay
// openers (redesign_button_is_door, app_state.h — the class names the keys
// that may carry a twin on their hold and parts no look), AND THE FOUR
// EXCLUSIVE PAIRS — SOURCE / TARGET, WARP / PHASE, FOLLOW / RESTR, TRIM /
// ADD SEL — whose separate color (Live's ScaleAwareness, the radio keys'
// purple of 2026-11-11, session 107, arc_dark's green) retired with its
// class 2026-11-19 ("I guess we can let go of the green in order to be more
// truthful"). A DISABLED KEY UNDER PRESS IS UNCHANGED (his "disable no press
// is right"): a dead key arms nothing (every press arm refuses a
// painted-dead face). THE PRESS FOLLOWS THE PRESS ARM'S OWN "on"
// (point_on_key below: the feint's inside bit, the key returning to rest
// while the pointer is off it and pressing again when it slides back); NO
// TIMING, NO HOLD, NO ANIMATION; the press arms — the slide-off feint, the
// hold-repeat and the long press — are unchanged; THE OTHER FEEDBACK STAYS:
// the act itself, a stateful key's drawing showing the act its next press
// runs. THE LISTS' ROWS go lighter under a press (THE SELECTION PAIR,
// below), a list row being no key. UNDER AN OVERLAY A DEAD LIT KEY STAYS
// LIT, DIMMED (architect 2026-11-11, session 107: "for consistency, they
// should keep that look of darkened. But lit."): every roster key the
// standing overlay leaves dead is disabled with its lit bit kept
// (redesign_button_enabled's overlay arm; redesign_button_selected,
// app_state.h, whose head owns the rule), so a lit one wears the lit and
// inert look and an unlit one the disabled look — ONE RULE for a dead key
// wherever it is dead; the overlay's opener and the tooltip lamp, live
// there, keep their live states. A MODAL changes no key behind it. NOTHING
// IS PRINTED UNDER A KEY (the legends retired 2026-11-18, send Z: "That's
// from the hi-fi theme"); the tooltip names the act.
// THE WORD BUTTONS are Arc's text buttons (his ruling 2026-11-18: "the word
// alone, no glyph"; paint_word_button): each its own ring, the same states,
// the word `text`, `text_on_lit` lit, `text_dim` disabled. NOISE AND THE
// TIME FIELDS are Arc's entry (paint_entry), NOISE's level its progress line.
// THE SCRUB'S THUMB AND THE ON-SCREEN KEYBOARD'S KEYS AWAIT STAGE D: today's
// flat key and today's press look (paint_flat_key below).
//
// THE EDGE RULE IS THE KEYS': both icon rows' and the controls rows'
// outermost BOXES stand kEdgeInsetW (6 W) inside the panel's first and last
// columns (render.h's THE EDGE RULE at well_columns; architect 2026-11-19,
// send Z; the seats' walks, paint_handler.cpp), two groups a run gap apart
// (render.h's program_run_gap_px, 6 W at the default). THE GROUND IS NOT
// PRESSABLE (architect 2026-11-02, R5: "ground is not a button"): THE
// PAINTED BOX IS THE HIT BOX (point_on_key below), a seam belonging to the
// key on its left, so a press, a lift and the tooltip answer on the box
// alone and the ground round it — the run gaps, the pads — answers nothing;
// the on-screen keyboard's gaps alike (R11; onscreen_keyboard.h's
// key_gap_px).
//
// THE TEXT TONES (architect 2026-11-02, M13, "we use Ableton colors as much
// as possible"): Live's two — `text`, Default's B5B5B5 (ControlForeground),
// on every word, digit and drawing the chrome prints — the clocks, the tab
// name, the status bar, the lists, the prompts, the editors, the keyboard's
// caps, the keys' glyphs (2026-11-02) — and `text_dim`, Default's 757575
// (TextDisabled), for the settings page's inert rows, the tooltip's hint
// line and an inert marker label. ON THE ARC PAGE (architect 2026-11-19,
// send Z) the title bar wears `title_text`, the marker labels
// `sidebar_text` and the ruler's digits `ruler_text` (theme_roles.h's head). ON A LIT KEY'S FACE the print — its glyph, the
// on-screen keyboard's lit cap — is `text_on_lit`, Default's 070707
// (StandbySelectionForeground, the selection's text; architect 2026-11-17,
// session 140), in place of `text` (architect 2026-11-11, session 106, THE
// KEY above); on a lit disabled key's dimmed face `text_on_lit_dim`,
// Default's 2A221A. THE
// CARDS' words and glyph wear their own ink, `card_ink`, Default's 070707
// (StandbySelectionForeground, the same print) on Live's orange (THE CARDS,
// below; 2026-11-04 night; the print's tone since 2026-11-17, D4). A
// DISABLED KEY'S glyph takes its own tone, `key_dim_ink`,
// Default's 5D5D5D (ViewCheckControlDisabledOff over the key face, Live's
// own number on every theme since 2026-11-07, theme_roles.h's head; architect
// 2026-11-02, on mock b3: "the disabled icons are too similar to enabled
// ones", 757575 reading too close to B5B5B5 at a key's size), darker than
// Live's dim text so a dead key reads dead at a glance; the lists' and the
// tooltip's dim print keep `text_dim`. NOTHING WEARS A SHADOW (2026-11-01, "no shadow anywhere"; the
// well's cue labels, triangles and ticks with the rest since 2026-11-02):
// every word, digit, tick and triangle is ink alone.
//
// THE WELL (architect 2026-11-01 night, "THE MAIN PAGE IS SETTLED", M5h;
// M13's text tones 2026-11-02; the text round's lengths 2026-11-03; THE
// TROUGH architect 2026-11-17, session 140; EDGE TO EDGE 2026-11-19, send
// Z): FLAT REGIONS ACROSS THE PANEL'S WHOLE WIDTH, no ring, no bevel, no
// frame (render.h's well_columns, THE EDGE RULE). THE LINES, `well_border`,
// Default's 2A2A2A (ClipBorder; Arc's $borders_color, Thunar's pane border,
// D2): one W under the top icon row, between the lanes, under the canvas,
// and the stereo divider in the same tone (render.h's THE WELL'S EDGES).
// Top to bottom — THE TRIM BAR, ARC'S SCROLLBAR (ruling 4): the 12-W
// trough `trim_bar_face`, Default's 242424 (ScrollbarInnerTrack over the
// ground; Live's track is transparent), and on it THE PILL, the span,
// `trim_span_face`, Default's 696969, or `selected_fill` while a drag
// moves it (render_trim_flags); the line; THE RULER `ruler_face`,
// THE CANVAS'S TONE, Default's 393939 (architect 2026-11-17, session 140,
// ruling 3: "the ruler lane is light, the marker lane is dark, and then the
// canvas is lighter"), with the `ruler_text` digits (send Z) and the
// `ruler_tick` ticks, Default's 919191 (DetailViewRulerMarkings over the
// ruler); the line; THE MARKER LANE `lane_face`,
// Default's 2A2A2A (architect 2026-11-03, the retrofit: Live's next real
// fill above the 242424 ground — Desktop); the line; THE CANVAS (the
// palette's `waveform_canvas`), its stereo divider the lines' tone.
// Every Default number is Live's, measured on his Live 12 shots (the
// ruler's ticks the time ruler's, the lane the clip view's marker lane; the
// ruler's face the canvas's). Where a theme makes the lane, the trough, the lines or the dim
// text coincide with the ground, that is Live's own number — ACCEPTED, not
// a bug, no backstop (architect 2026-11-05).
// THE TIME FIELDS are Arc's entry since 2026-11-19 (send Z; paint_entry
// below): the ruler's — the canvas's — tone in a one-W `well_border` ring.
//
// THE WELL'S FRAME TONE. `well_frame` is THE GRAMMAR'S ONE FRAME TONE
// (architect 2026-11-02, M14 "d"), Default's 757575 (TextDisabled, the dim
// text's own role): the scrub's one-W channel (render.h's scrub block) and
// the tooltip's one-W border (paint_shift_tooltip; the cards wear
// `card_face` instead since 2026-11-04 night). It is not the well's lines'
// `well_border` (THE WELL above), a tone of its own. THE TRIM BAR'S TWO
// TONES (render_trim_flags): `trim_bar_face`, the trough, is Live's
// ScrollbarInnerTrack over the ground (architect 2026-11-17, session 140);
// `trim_span_face`, the pill, is Live's ScrollbarInnerHandle over Desktop,
// the ruler's ticks Live's DetailViewRulerMarkings over the ruler face, on
// every theme (architect 2026-11-07, session 82; theme_roles.h's head).
//
// THE SELECTION PAIR (architect 2026-11-01 night, M5g / M6: Live's browser
// selection): a flat bar in `selection_fill`, Default's 637E86
// (StandbySelectionBackground), under `selection_text`, Default's 070707
// (StandbySelectionForeground, Live's own partner of the bar; architect
// 2026-11-06, session 75 — ControlContrastFrame's 111111 before it, our
// mispairing) — the flag editor's and the dialog editors'
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
// its class glyph in `card_ink`, THE PRINT ON A LIT FACE, the theme's
// StandbySelectionForeground, Default's 070707 (architect 2026-11-17,
// session 140, D4, "white card words": Arc's $warning_fg_color FFFFFF on its
// warning orange), and NO VISIBLE BORDER: the one-W ring's rows and
// columns stay in the layout, painted in the fill (the chrome's
// `card_ground`, `card_text` and `card_frame`, theme_file.h; render.h's THE
// CARD FACE) — Live's status-bar orange, a message rather than a state, not
// the accent of the lit keys (`cue`, Default's FFAD56).

// -- ARC'S BUTTON: THE KEY'S STATES, ITS LINKED GROUP, THE WORD BUTTON AND
//    THE ENTRY (architect 2026-11-18 / 2026-11-19, send Z; DESIGN §3, the
//    mock kit's linked_group / word_button / body_entry the reference) -----

// A KEY'S FOUR STATES, ARC'S (G3 `button`: `:active` and `:checked` are one
// look; architect 2026-11-19, send Z, "Arc's own answer" superseding the
// darker press of 2026-11-05, "we can let go of the green", "lit but inert:
// let's do arcs numbers"): REST; LIT — checked, or a press standing on a
// live key, or both, ONE LOOK (the accent `cue` over the key's own columns
// between the group's border rows, the print `text_on_lit`); DISABLED (the
// face `key_dim_face`, the border `key_dim_border` on the key's own stretch
// of the ring, the drawing `key_dim_ink`); LIT AND INERT — checked and
// disabled (the face `lit_dim_fill`, the print `text_on_lit_dim`, Arc's
// button:disabled:checked under the button's opacity: theme_roles.h's
// DERIVED block). A DISABLED KEY UNDER PRESS IS UNCHANGED (his "disable no
// press is right", 2026-11-05): a dead key arms nothing (every press arm
// refuses a painted-dead face), so the press bit is read for a live key
// alone. key_state is the one place the three bits are folded; every Arc
// button's painter asks it (the band's and the bottom icon row's walks, the
// controls rows, paint_handler.cpp).
enum class KeyState { Rest, Lit, Disabled, LitInert };
inline constexpr KeyState key_state(bool checked, bool pressed, bool enabled) {
    if (!enabled) return checked ? KeyState::LitInert : KeyState::Disabled;
    return (checked || pressed) ? KeyState::Lit : KeyState::Rest;
}

// THE INK OF A KEY'S DRAWING for its state, the installed theme's sRGB word
// (the glyph road takes the word, icons::draw_glyph): `text` at rest,
// `text_on_lit` lit, `key_dim_ink` disabled, `text_on_lit_dim` lit and inert.
inline uint32_t key_ink(KeyState s) {
    const GuiThemeTones& t = installed_theme().tones;
    switch (s) {
        case KeyState::Lit:      return t.text_on_lit;
        case KeyState::Disabled: return t.key_dim_ink;
        case KeyState::LitInert: return t.text_on_lit_dim;
        case KeyState::Rest:     break;
    }
    return t.text;
}
// THE INK OF A WORD BUTTON'S WORD (the kit's word_button; brief of send Z's
// stage C): the drawing's but disabled, where the word wears `text_dim`, the
// dim text of every inert word (a word button is never checked, so its lit
// and inert print is never asked).
inline uint32_t word_button_ink(KeyState s) {
    return s == KeyState::Disabled ? installed_theme().tones.text_dim
                                   : key_ink(s);
}

// THE KEY'S LENGTHS (the head's THE KEY; render.h's case block owns the
// box): THE BOX the `key_width` x `key_height` tuning keys (icon_case_w_px /
// icon_case_h_px, 36 x 28 W at the defaults, 72 x 56 device px on the tablet
// at 200 %), BORDER INCLUDED ("the border is clickable"); THE BORDER one W
// (hairline_px, the line rule: 2 device px on the tablet, floored at one);
// THE OUTER CORNER kKeyCornerW, 3 W (Arc's button border-radius, G3), 6
// device px, floored at one; the faces' corners inside the border the outer
// less the border (GTK's padding box), 4 device px. Each through scaled_px,
// rounded at the element.
inline constexpr double kKeyCornerW = 3.0;
inline int key_border_px() { return hairline_px(); }
inline int key_corner_px() { return scaled_px(kKeyCornerW, 1); }
inline int key_inner_corner_px() {
    return std::max(0, key_corner_px() - key_border_px());
}

// THE LINKED GROUP'S PITCH (Arc's `.linked`, G3; the kit's group_w): two
// neighbors SHARE ONE SEAM COLUMN, so a key stands one box less one border
// right of the one before it — 70 device px on the tablet (72 − 2), 35 at
// 100 % — and A GROUP OF n IS key + (n − 1)(key − 1 W) WIDE, each part
// rounded on its own (0 for none): every group walk's (the band's
// icon_row_seats, the bottom icon row's row8_block_walk, the controls rows'
// layouts, paint_handler.cpp). The ground between two GROUPS is the run gap
// (render.h's program_run_gap_px).
inline int key_link_pitch_px() {
    return icon_case_w_px() - key_border_px();
}
inline int linked_group_w_px(int n) {
    return n > 0 ? icon_case_w_px() + (n - 1) * key_link_pitch_px() : 0;
}
// MEMBER i's BOX in the group whose first box stands at (x, y): the whole
// key, both its seam columns included — the published rect, the damage, the
// tooltip's seat.
inline GuiRect linked_key_box(int x, int y, int i) {
    return GuiRect{x + i * key_link_pitch_px(), y, icon_case_w_px(),
                   icon_case_h_px()};
}
// THE SEAM A MEMBER'S BOX SHARES WITH THE KEY ON ITS LEFT, in device px: its
// first border's columns, which BELONG TO THE LEFT KEY for every press and
// hover (architect 2026-11-18 via the design: "a seam column belongs to the
// left key") — one border on every member but a group's first, 0 there.
inline int linked_key_seam_px(int i) {
    return i > 0 ? key_border_px() : 0;
}

// ONE LINKED GROUP on the backbuffer: n = states.size() keys from (x, y),
// each linked_key_box(x, y, i) — THE KIT'S linked_group, in its paint order,
// every edge at exact coverage over the ground the caller laid under the
// same clip: (1) THE BORDER, the group's rounded box (its four outer corners
// key_corner_px) in `well_border`; (2) a DISABLED member's stretch of it —
// its box's columns, both seams included, the outer corners where it ends
// the group — in `key_dim_border`; (3) THE FACES, the group's inner box (its
// corners key_inner_corner_px) cut at each member's own columns: from one
// border inside its box's left edge to one border inside the NEXT box's
// left edge — so A SEAM WEARS THE LEFT KEY'S FACE — the last to one border
// inside its own right edge, each in its state's face (`key_face` at rest,
// `key_dim_face` disabled, `lit_dim_fill` lit and inert; a lit member's
// `key_face`, under its accent); (4) A LIT MEMBER'S ACCENT, `cue`, over its
// box's whole columns — both seams — between the border's top and bottom
// rows (the inner box's own columns and corners where it ends the group), so
// the border stays `well_border` round it and a lit key takes both its seams
// whatever its neighbors. EVERY PIXEL A MEMBER'S STATE MOVES LIES INSIDE ITS
// OWN BOX, its corners included (a seam column is inside both boxes it
// joins): a member's damage is its box, and a repaint under any clip paints
// every member from its own state as published. THE ROAD: one
// CoverageStampPass (cool_edit_paint.h), every shape a rounded rect with real
// edges stamped straight into the target in the order above — NOTHING
// ELSE IS DRAWN between begin() and end() (the drawings go on after, the
// caller's paint_key_glyph), so the stamp and cairo never interleave. The
// glyphs are the caller's.
void paint_linked_group(cairo_t* cr, int x, int y,
                        std::span<const KeyState> states);

// ONE WORD BUTTON on `box` (Arc's text button; architect 2026-11-18, his
// ruling "the word alone, no glyph", Delete a plain button; the kit's
// word_button, through GTK's box order, the kit's gtk_box): THE FACE on the
// padding box (`box` one border in, its corners key_inner_corner_px), THEN THE
// RING over it — the outer rounded box (key_corner_px) less the padding box
// — each at exact coverage, so an outer corner mixes the ground and the ring
// alone and an inner corner pixel all three: at rest `key_face` in
// `well_border`; lit (pressed) the accent `cue` in `well_border`; disabled
// `key_dim_face` in `key_dim_border`. The word is the caller's
// (word_button_ink). One stamp pass, nothing drawn inside it.
void paint_word_button(cairo_t* cr, const GuiRect& box, KeyState state);

// ARC'S ENTRY on `box` (architect 2026-11-19, send Z; DESIGN §3, the kit's
// body_entry; G3 `entry`) — NOISE's case, the bottom icon row's clock and
// the controls rows' time fields (the render player's two, the score
// viewer's clock and page cell), ONE PAINTER: GTK's box order as the word
// button's — THE FILL on the padding box (`box` one border in, corners
// key_inner_corner_px) in `ruler_face`, the canvas's tone (Arc's
// $base_color, 404552 on arc_dark), THEN THE RING over it (the outer rounded
// box, corners key_corner_px, less the padding box) in `well_border` — and,
// with `progress` t in (0, 1], ARC'S ENTRY PROGRESS over the fill: the
// padding box (its own inner corners) CUT to its last kEntryProgressW (2 W)
// rows and to its first t of its width, in the accent `cue` — so the line's
// left end follows the inner corner's arc and its right end stands at its
// real x. Every edge at exact coverage over the ground the caller laid; one
// stamp pass, nothing drawn inside it; the text is the caller's.
inline constexpr double kEntryProgressW = 2.0;
void paint_entry(cairo_t* cr, const GuiRect& box,
                 std::optional<double> progress = std::nullopt);

// A PAINT-TIME MIX OF TWO INSTALLED WORDS, `t` of `b` over `a`, per channel,
// rounded half to even (the derivation's rounding, theme_roles.h): the
// disabled entry's number, the text at 35 % over the entry's fill (Arc's
// insensitive entry).
inline uint32_t mix_word(uint32_t a, uint32_t b, double t) {
    uint32_t out = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        const double ca = static_cast<double>((a >> shift) & 0xFFu);
        const double cb = static_cast<double>((b >> shift) & 0xFFu);
        const double v  = std::nearbyint(ca + t * (cb - ca));
        out |= static_cast<uint32_t>(std::clamp(v, 0.0, 255.0)) << shift;
    }
    return out;
}

// THE SCRUB'S THUMB AND THE ON-SCREEN KEYBOARD'S KEYS, AWAITING SEND Z'S
// STAGE D: today's flat key — a flat box, no border, no corner — and today's
// press look, alone of every key surface: the face `key_face` at rest,
// `key_press_face` (Desktop) under a press, the accent `cue` lit (the
// keyboard's latched Shift and its symbols page key), `lit_press_fill` (the
// accent × mean(Desktop) / mean(key face)) lit under the finger. KeyLook
// folds the two bits; flat_key_ink the print on it (`text`, or
// `text_on_lit` lit). Neither surface is ever disabled.
enum class KeyLook { Rest, Press, Lit, LitPress };
inline constexpr KeyLook key_look(bool checked, bool pressed) {
    return checked ? (pressed ? KeyLook::LitPress : KeyLook::Lit)
         : pressed ? KeyLook::Press
                   : KeyLook::Rest;
}
inline uint32_t flat_key_ink(KeyLook look) {
    const GuiThemeTones& t = installed_theme().tones;
    return look == KeyLook::Lit || look == KeyLook::LitPress ? t.text_on_lit
                                                             : t.text;
}
// The flat face on real edges (the exact fill, paint_ink_rect on a
// GuiRectF; render.h's hull holds the rule).
void paint_flat_key(cairo_t* cr, const GuiRectF& r, KeyLook look);

// A POINT PRESSES A KEY EXACTLY WHERE THE KEY IS PAINTED (the head's THE
// KEY; architect 2026-11-02, R5 / R11, "ground is not a button"): `key` the
// PAINTED BOX — border included since send Z ("the border is clickable") —
// the walk's own rect for every key of the band, the bottom icon row and the
// controls rows, the on-screen keyboard's walk rect for its own
// (onscreen_keyboard::for_each_key, its gap already outside it). THE ONE
// PREDICATE every key hit test asks: the roster's face
// (AppState::RedesignButtonFace::contains — the press, the lift, the band's
// modifier gate, the tooltip's owner), the modal family's buttons
// (GuiInputHandler::modal_dialog_button_hit — the press and the lift) and
// the keyboard's (onscreen_keyboard::key_at). Each painter PUBLISHES the box
// it paints, so a reader re-shapes nothing. A gesture already begun keeps
// its rules: a press that slides off its key reads "outside" as ever.
inline bool point_on_key(const GuiRect& key, int x, int y) {
    return rect_contains(key, x, y);
}
// A MEMBER OF A LINKED GROUP: its box less the `seam` columns at its left
// edge, which belong to the key on its left (linked_key_seam_px), so a
// group's members tile with no shared point and the ground between two
// groups presses nothing.
inline bool point_on_key(const GuiRect& key, int seam, int x, int y) {
    return rect_contains(key, x, y) && x >= key.x + seam;
}
// THE SAME PREDICATE FOR A FACE WITH REAL EDGES (architect 2026-11-15, the
// no-grid arc: "yes, I'd like to move to fractional hitboxes."): the
// on-screen keyboard's faces, whose edges its tuning keys make fractional,
// asked with the pointer's REAL place — half-open on both axes, so two keys
// never share a point and the ground between them presses nothing.
inline bool point_on_key(const GuiRectF& key, double x, double y) {
    return rect_contains(key, x, y);
}
