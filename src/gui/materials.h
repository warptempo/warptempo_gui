#pragma once

#include "render.h"           // GuiRect, GuiColor, palette(), the chokepoints

#include <algorithm>
#include <array>
#include <cairo/cairo.h>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

// THE MATERIALS — THE FLAT LOOK (architect 2026-11-01: "whatever ruling it
// needs to, we need to reverse to go flat"). This header is the one owner
// of the ground's, the key's, the lit
// face's, the text tones', the well's tones' and the pane's RULES; THEIR
// NUMBERS ARE ARC-DARK'S, COMPILED (architect 2026-11-21: the row
// kArcDarkTones, gui_theme.h, each line its Arc source; palette_file.h's
// head) — every painter reads palette()'s member for the role (render.h's
// GuiPalette; the fill table kGuiPaletteFill, palette_file.h); nothing
// outside the compiled row names a color.
//
// TWO GROUNDS (architect 2026-11-18 / 2026-11-19): THE
// TITLE BAR ALONE wears the title tone `ground` — 2F343F, Arc's
// $header_bg: "the darkest color would only apply to the top line, the
// title bar" (paint_title_bar) — and EVERYTHING ELSE OUTSIDE THE WELL'S
// REGIONS AND THE KEYS wears THE BODY TONE `body_face` — 383C4A, Arc's
// $bg_color: both icon
// rows and their pads, the gaps between keys, the status bar, the erase
// (render_background), the on-screen keyboard's band and the modal family's
// controls row (the pages wear the lists' view tone, `ruler_face`). The
// well's lines — under the top icon row, the
// trough, the ruler and the marker lane, the stereo divider, the well's
// foot and the status bar's line — are `well_border`, each one W
// (render.h's hairline_px). FLAT, NO GRADIENT, NO
// SHADOW: every pixel of either ground is a solid fill through the
// palette's chokepoint (set_palette_source, render.h), which converts it for
// the window as it converts every color (display_color), so True Colors
// holds with no cache of its own. The palette's `face` IS THE BODY TONE
// (palette_file.h's fill table), the chrome's `ground` role the title tone.
//
// THE KEY IS ARC'S BUTTON (architect 2026-11-18 / 2026-11-19; Arc's gtk3
// CSS `button`, `.linked`; paint_linked_group,
// paint_word_button and paint_entry below): every key of the top icon
// row, the bottom icon row and the controls rows — A BOX 36 x 28 W
// (render.h's kKeyWidthW x kKeyHeightW, its case block; 72 x 56 device px
// on the tablet at 200 %), BORDER INCLUDED ("the border is
// clickable"), in A ONE-W `well_border` RING (arc_dark 2B2E39) WITH 3 W
// OUTER CORNERS (kKeyCornerW, the faces' 2 W
// inside it), its face `key_face` (arc_dark 444A58), every edge at exact
// coverage. THE KEYS OF ONE GROUP ARE
// ARC'S LINKED BUTTONS: neighbors SHARE ONE SEAM COLUMN, one ring round the
// group, its four outer corners rounded, the seam wearing the face at rest —
// no divider line (key_link_pitch_px / linked_group_w_px below). THE
// DRAWING the set's 16 W canvas centered on the box (paint_key_glyph,
// paint_handler.cpp). NO DEPTH, NO SHADOW.
// THE STATES ARE ARC'S (key_state below; architect 2026-11-19: "Arc's own
// answer" on every key; "we can let go of the green"; "lit but
// inert: let's do arcs numbers"): REST the face under the drawing in
// `text`; PRESSED, LIT AND LIT UNDER PRESS ONE LOOK — `:active` and
// `:checked` are one in Arc — THE ACCENT `cue` (5294E2, Arc's selection
// blue) OVER THE KEY'S OWN COLUMNS BETWEEN THE GROUP'S BORDER ROWS, both its
// seams included, the border kept `well_border`, the drawing in
// `text_on_lit` (FFFFFF, Arc's $selected_fg_color);
// DISABLED the face `key_dim_face` (the key face at 55 % over the body
// tone), the border `key_dim_border` (the well's border at 55 % over it) on
// the key's own stretch of the ring, the drawing `key_dim_ink`; LIT AND
// INERT — checked and disabled — the face `lit_dim_fill` under
// `text_on_lit_dim` (Arc's button:disabled:checked under the button's
// opacity; the arithmetic at their lines of kArcDarkTones, gui_theme.h). A
// SEAM BETWEEN TWO KEYS OF
// DIFFERENT STATES WEARS THE LEFT KEY'S FACE, a lit key taking both its
// seams (paint_linked_group's paint order).
// EVERY CHECKED KEY WEARS THE ONE ACCENT: the state lamps (MAGNIFY, Read
// Only, Tooltips, the walk lamp, Cumulative, the iteration lamp, Value
// Drag), the render player's Repeat One, THE DOORS, the seven overlay
// openers (THE DOORS, app_state.h — lit by redesign_button_selected's
// overlay arms; a door may carry a twin on its hold, the membership being
// redesign_button_shift_admits', and the class parts no look), AND THE FOUR
// EXCLUSIVE PAIRS — SOURCE / TARGET, WARP / PHASE, FOLLOW / RESTR, TRIM /
// ADD SEL — with no color of their own (architect 2026-11-19: "I guess we
// can let go of the green in order to be more truthful"). A DISABLED KEY UNDER PRESS IS UNCHANGED (his "disable no press
// is right"): a dead key arms nothing (every press arm refuses a
// painted-dead face). THE PRESS FOLLOWS THE PRESS ARM'S OWN "on"
// (point_on_key below: the feint's inside bit, the key returning to rest
// while the pointer is off it and pressing again when it slides back); EVERY
// CHANGE OF A KEY'S STATE FADES AS ARC'S SHEET SAYS (architect 2026-11-20:
// "i'd like to see arc-truthful version instead") — INTO THE ACCENT,
// PRESSED OR LIT, OVER 50 ms (gtk3 CSS line
// 138), OUT OF IT, INTO THE HOVER, INTO DISABLED, OVER 200 ms (line 126),
// LIT AND INERT 50 ms as a checked look, each from the picture on screen,
// the face, the ring, the seams, the print and the drawing on one clock
// (THE FACE'S FADE: FaceLook, FaceMix and mix_looks below; the clock, the
// damage and what does not fade AppState::FaceFades, app_state.h) — THE ACT
// IS NEVER DELAYED: the press arms — the slide-off feint, the hold-repeat
// and the long press — and the lift's act run on the stashed bits,
// the fade being paint alone; THE OTHER FEEDBACK STAYS:
// the act itself, a stateful key's drawing showing the act its next press
// runs. A LIST ROW UNDER A PRESS wears the selection pair, the highlight's
// own look (THE SELECTION PAIR, below), a list row being no key. UNDER AN OVERLAY A DEAD LIT KEY STAYS
// LIT, DIMMED (architect 2026-11-11: "for consistency, they
// should keep that look of darkened. But lit."): every roster key the
// standing overlay leaves dead is disabled with its lit bit kept
// (redesign_button_enabled's overlay arm; redesign_button_selected,
// app_state.h, whose head owns the rule), so a lit one wears the lit and
// inert look and an unlit one the disabled look — ONE RULE for a dead key
// wherever it is dead; the overlay's opener and the tooltip lamp, live
// there, keep their live states. UNDER EVERY MODAL — WHILE THE ON-SCREEN
// KEYBOARD STANDS (every editor) AND WHILE A QUESTION'S PAGE OR THE PROGRESS
// PAGE STANDS — EVERY ROSTER KEY IS DEAD AND LOOKS IT, the same rule: each
// wears the disabled look, a lit one the lit and inert look — the main
// page's lit view pair under the flag editor or a question, its list's
// opener under a value sub-page (roster_face_page_owner), a standing
// overlay's opener under its own question —
// Arc's own disabled look the dead keys' one sign (redesign_button_enabled's
// first arm, app_state.h, the owner; architect 2026-11-20). NOTHING
// IS PRINTED UNDER A KEY (architect 2026-11-18); the tooltip names the act.
// THE HOVER IS ARC'S (architect 2026-11-18: "I think we could consider
// resurrecting the hover ... this is a low contrast theme, which should
// support hovers. And actually, that'll help me, especially if the icons
// size gets smaller"; "lets go arc truthful re: hoevers"; 2026-11-20: "If
// they're from Canonical, from Arc, I'll just accept them ... So I'm not
// going to mess with the fade"): UNDER THE HOVERING PEN A RESTING KEY'S
// FACE IS `key_hover_face`
// (Arc's button:hover background, arc_dark 505666 on the 444A58 face, gtk3
// CSS line 136; its line of kArcDarkTones), THE BORDER, THE DRAWING AND THE WORD
// UNCHANGED — on a key of both icon rows and of the controls rows, a word
// button (a question's answers among them), an
// on-screen keyboard key, the scrub's
// knob's fill inside its unchanged rim (Arc's scale slider:hover), and
// NOISE's entry's fill inside its unchanged ring, its number and its level
// line unchanged (architect 2026-11-24: Arc's entry has no `:hover`, and
// every control of Arc's that is pressed or dragged hovers in this one fill,
// `button:hover` and `scale slider:hover` alike; NOISE is drawn as an entry
// and worked as a slider and a toggle; paint_entry's `face`). IN A
// LINKED GROUP THE SEAM COLUMN ON EACH SIDE OF THE HOVERING KEY WEARS THE
// BORDER TONE `well_border` between the border's rows (measured on his
// desktop, a linked pair and a message dialog's answers: the seam shows the
// face at rest and the border tone while either neighbor hovers; Arc's
// `box-shadow: inset 1px 0 #2b2e39` on the hovering button and on its next
// sibling, gtk3 CSS line 292), the group's ring untouched; NO LINE BESIDE A
// LIT NEIGHBOR, LIT AND INERT INCLUDED — Arc's CSS, not a choice of ours:
// both selectors of line 292 want `:not(:checked)` of the hovering button
// and of the sibling, and line 296 sets `button:checked + button:hover {
// box-shadow: none }`; `:checked` holds the checked-and-disabled key — so
// that seam column stays what the group paints there with no hover line (a
// lit key's left seam the accent, every other the face of the key on its
// left: paint_linked_group's (2) and (3), the seam being the left key's);
// a DISABLED neighbor's seam does take the line (line 292's sibling half
// asks nothing of `:disabled`); the on-screen keyboard's grid wears its
// lines always, so only the face moves there.
// ONLY A RESTING KEY HOVERS: a lit key is unchanged (Arc's checked + hover
// is the checked face), a disabled key and a key dead under a veil wear
// nothing, a pressed key goes toward the accent. THE FACE FADES, ARC'S
// TRANSITION (`button { transition: all 200ms cubic-bezier(0.25, 0.46,
// 0.45, 0.94) }`, the scale's slider the same): rest to hover and hover to
// rest over 200 ms on that curve, as every change into an unchecked look
// (a change into a checked look — pressed, lit, lit and inert — runs 50 ms:
// THE KEY above, AppState::FaceFades::duration_ms), each painted tone THE MIX OF ITS LOOKS' TONES AS PAINTED
// — per channel between the colors' own words (StampTone below), the
// seam's line between the tone the seam wears without it and the border
// tone BY THE SEAM'S OWN MIX: the line is one inset shadow on the right key
// whichever of its two keys hovers, so it holds the border tone while the
// pen slides from a key to its neighbor, and only a seam with one hovering
// side fades, with that side (AppState::FaceFades' THE SEAMS). THE MIX IS A
// STORED VALUE, never a clock read here: WHICH FACE HOVERS, EVERY FACE'S
// AND SEAM'S PICTURE AS PAINTED, THEIR FADES' CLOCK AND THEIR DAMAGE HAVE
// ONE OWNER, AppState::FaceFades (app_state.h), whose head holds the gate —
// a face hovers only where a press there would act — and the frames' rule;
// every painter below takes the mixes as numbers.
// A LIST'S RESTING ROW HOVERS TOO — ARC'S `row.activatable:hover` (architect
// 2026-11-21: "I'd like to add hover"; its tone the same day: "I
// prefer to go more truthful. It just needs to be very subtle."):
// `list_hover_face`, 3 % white over the list's view (its line of
// kArcDarkTones, gui_theme.h), across the lit bar's width, in at once
// and out over Arc's 150 ms `row` transition (paint_handler.cpp's THE LIST
// VIEW; AppState::FaceFades' THE LIST ROW'S HOVER).
// THE MARKER LANE HOVERS TOO — A MARKER'S CUE (architect 2026-11-21: "The
// markers could get a hover action"): a cue a tap would act on
// lights its chip and its label's ink, a disabled marker in its own pair, a
// grey chip and a dimmer label (render.cpp's resolve_flag_face, the table),
// the mix of its two tones as painted on the keys' 200 ms (StampTone below;
// AppState::FaceFades' THE MARKER'S FACE). The playhead's triangle has no
// hover (architect 2026-11-21: "very hard to see ... very hard to
// stably trigger"); over it the waveform's hover line stands with its own
// triangle (render.h's render_hover_line). A marker's stem, the canvas's
// lines and the time labels light nothing.
// THE WORD BUTTONS are Arc's text buttons (architect 2026-11-18: "the word
// alone, no glyph"; paint_word_button): each its own ring, the same states,
// the word `text`, `text_on_lit` lit, `text_dim` disabled. NOISE and the
// value sub-page's field are Arc's entry (paint_entry), NOISE's level its
// progress line and its fill hovering as a key's face does (THE HOVER above,
// architect 2026-11-24), the sub-page's its focus line (Arc's entry:focus)
// and no hover; THE
// MARKER LANE'S EDITORS are GTK's in-place cell editor, the entry's fill in
// a one-W accent border (render.h's EDITING paragraph); THE TIME FIELDS are
// plain text pressing nothing (architect 2026-11-19 / 2026-11-20;
// paint_handler.cpp's THE TIME FIELDS). THE ON-SCREEN KEYBOARD'S KEYS are
// one grid of Arc's button tones
// with shared lines (paint_handler.cpp's
// paint_onscreen_keyboard); THE SCRUB IS ARC'S SCALE — its trough
// `scale_trough`, its played part the accent, its knob `key_face` in a
// one-W `scale_slider_border` rim, held the whole disc the accent
// (render.h's scrub block, paint_scale; architect 2026-11-18 / 19: "I'm
// hoping we're gonna have a proper arc thumb"). EVERY KEY SURFACE IS
// ARC'S (architect 2026-11-18 / 19).
//
// THE EDGE RULE IS THE KEYS': both icon rows' and the controls rows'
// outermost BOXES stand kEdgeInsetW (6 W) inside the panel's first and last
// columns (render.h's THE EDGE RULE at well_columns; architect 2026-11-19;
// the seats' walks, paint_handler.cpp), two groups a run gap apart
// (render.h's program_run_gap_px, 6 W). THE GROUND IS NOT
// PRESSABLE (architect 2026-11-02: "ground is not a button"): THE
// PAINTED BOX IS THE HIT BOX (point_on_key below), a seam belonging to the
// key on its left, so a press, a lift and the tooltip answer on the box
// alone and the ground round it — the run gaps, the pads — answers nothing;
// the on-screen keyboard's band ground round its grid alike, its grid's
// lines its keys' (onscreen_keyboard::key_at). THE HOVER'S HIT IS THE
// PRESS'S (AppState::FaceFades).
//
// THE TEXT TONES (architect 2026-11-02, 2026-11-19; every number at its
// line of kArcDarkTones, gui_theme.h): `text`, Arc's
// font color D3DAE3, on every word, digit and drawing the chrome prints —
// the overlays' time labels, the status bar, the lists and their drawings,
// the pages' words, the keyboard's caps, the keys' glyphs —
// and `text_dim`, 7E838F, for the settings page's disabled rows, the
// tooltip's hint line and an inert marker label. ON THE ARC PAGE (architect
// 2026-11-19) the title bar wears `title_text`, the marker labels
// `sidebar_text` and the ruler's digits `ruler_text`; THE BOTTOM ICON ROW'S
// TIMESTAMP wears `timestamp_text`, Arc's `.h4` A5ABB5, in its own Bold
// 14 W (architect 2026-11-25; paint_handler.cpp's THE TIME FIELDS). ON A LIT KEY'S FACE
// the print — its glyph, the on-screen keyboard's lit cap — is
// `text_on_lit`, Arc's white (architect 2026-11-17), in place
// of `text`; on a lit
// disabled key's dimmed face `text_on_lit_dim`, 9AA1AF. A STANDING CARD'S
// words and glyph wear their own ink, `card_ink`, Arc's white on its
// destructive red (THE CARDS, below; a normal card's `sidebar_text`). A
// DISABLED KEY'S glyph takes its own tone, `key_dim_ink`, 7F8591, measured
// on his Arc-Dark desktop (architect 2026-11-19; 2026-11-02: "the disabled
// icons are too similar to enabled ones"), so a dead key
// reads dead at a glance; the lists' and the tooltip's dim print keep
// `text_dim`. NO PRINT WEARS A SHADOW (2026-11-01, "no shadow anywhere"):
// every word, digit, tick and triangle is ink alone.
//
// THE WELL (architect 2026-11-01, "THE MAIN PAGE IS SETTLED"; THE TROUGH
// architect 2026-11-17; EDGE TO EDGE 2026-11-19): FLAT REGIONS ACROSS THE
// PANEL'S WHOLE WIDTH, no ring, no
// frame (render.h's well_columns, THE EDGE RULE). THE LINES, `well_border`
// (Arc's $borders_color, Thunar's pane border): one W under the top icon
// row, between the lanes, under the canvas,
// and the stereo divider in the same tone (render.h's THE WELL'S EDGES).
// Top to bottom — THE TRIM BAR, ARC'S SCROLLBAR: the 12-W
// trough `trim_bar_face`, and on it THE PILL, the span,
// `trim_span_face`, at rest — its hovered, held and
// swept looks and the caps THE TRIM BAR'S TWO TONES below and render.h's
// one table, trim_bar_tones; the line; THE RULER `ruler_face`,
// THE CANVAS'S TONE (architect 2026-11-17: "the ruler lane is light, the
// marker lane is dark, and then the canvas is lighter"), with the
// `ruler_text` digits and the `ruler_tick` ticks; the line; THE MARKER
// LANE `lane_face`; the line; THE CANVAS (the
// palette's `waveform_canvas`), its stereo divider the lines' tone. Every
// number at its line of kArcDarkTones (gui_theme.h).
// ARC'S ENTRY (NOISE's case, the value sub-page's field; paint_entry below):
// the ruler's — the canvas's — tone in a one-W `well_border` ring (NOISE's
// fill `key_hover_face` under the hovering pen, THE HOVER above); the
// marker lane's cell editor the same fill in the accent's square border.
//
// THE TRIM BAR'S TWO TONES (render_trim_flags): `trim_bar_face`, the trough,
// is Arc's scrollbar trough (architect 2026-11-17);
// `trim_span_face`, the pill at rest, is Arc's scrollbar slider (their
// numbers at their lines of kArcDarkTones, gui_theme.h). ITS STATES' TONES
// (architect 2026-11-20; the one table at render.h's trim_bar_tones): the
// part under the hovering pen — the pill's body or an end's cap —
// `trim_span_hover_face` (Arc's slider:hover, Thunar's slider under the
// pointer), the part the bar's own
// press holds and the end a TRIM sweep is moving `selected_fill` (Arc's
// slider:active; the sweep an end held, architect 2026-11-20),
// and every cap neither hovered, held nor moved by a sweep `text` (his "the
// default text";
// the caps, Kdenlive's and ours, have no tone of Arc's). No fade: they
// change at once.
//
// THE SELECTION PAIR (architect 2026-11-01, 2026-11-06): a flat bar in
// `selection_fill` under its own partner `selection_text` — Arc's one
// selection, the accent 5294E2 under
// FFFFFF — the flag editor's and the page editors' selected substring and
// the lists' highlighted row, across the view's whole width (the chrome's
// `selected_fill` / `selected_text`). A LIST ROW UNDER A
// PRESS WEARS THE SAME PAIR (2026-11-19: Thunar is
// single-click, a pressed row a selected one): the row a press stands on — the feint's inside bit,
// FolderOverlayPress's — in every list of the folder overlay; an inert row
// (a Disabled row) shows no press — its press arms the scroll drag alone;
// a press that has become the scroll drag presses no row
// (paint_handler.cpp's paint_list_view).
//
// THE VIEW (2026-11-19; Thunar's list view): the modal
// family's box interior, the canvas's tone `ruler_face` (Arc's $base_color
// 404552), flat, between the box's two lines, with no frame, ring, shadow
// or row separator (modal_box.h's head; the score viewer's darker pane,
// `score_pane_face`, the one exception, is recorded there).
//
// THE FLOATING SURFACES ARE ARC'S AND NOTIFYD'S (architect 2026-11-19, "good
// to build"): THE TOOLTIP Arc's — its
// box the carried fill `tooltip_fill` (arc_dark 4B5162 at .95), rounded,
// no border, Arc's shadow under it, its words `sidebar_text` (render.h's THE
// TOOLTIP IS ARC'S); THE CARDS notifyd's bubble — a normal card
// `card_fill` (353945 at .95) under `sidebar_text`, a standing card Arc's
// destructive red `card_standing_fill` (F04A50 at .95) under
// `card_ink`, the print on a lit face (notifications.h's THE LOOK). THE
// LIMITER LAMP wears `limiter_lamp`, its own tone (Arc's warning orange,
// its line of kArcDarkTones, gui_theme.h).

// -- ARC'S BUTTON: THE KEY'S STATES, ITS LINKED GROUP, THE WORD BUTTON AND
//    THE ENTRY (architect 2026-11-18 / 2026-11-19; Arc's gtk3 CSS the
//    reference) -----

// A KEY'S FOUR STATES, ARC'S (gtk3 CSS `button`: `:active` and `:checked` are
// one look; architect 2026-11-19: "Arc's own answer", "we can let go of the
// green", "lit but inert: let's do arcs numbers"): REST; LIT — checked, or
// a press standing on a live key, or both, ONE LOOK (the accent `cue` over the key's own columns
// between the group's border rows, the print `text_on_lit`); DISABLED (the
// face `key_dim_face`, the border `key_dim_border` on the key's own stretch
// of the ring, the drawing `key_dim_ink`); LIT AND INERT — checked and
// disabled (the face `lit_dim_fill`, the print `text_on_lit_dim`, Arc's
// button:disabled:checked under the button's opacity: their lines of
// kArcDarkTones, gui_theme.h). A DISABLED KEY UNDER PRESS IS UNCHANGED (his "disable no
// press is right", 2026-11-05): a dead key arms nothing (every press arm
// refuses a painted-dead face), so the press bit is read for a live key
// alone. key_state is the one place the three bits are folded; every Arc
// button's painter asks it (the top and the bottom icon row's walks, the
// controls rows, paint_handler.cpp).
enum class KeyState { Rest, Lit, Disabled, LitInert };
inline constexpr KeyState key_state(bool checked, bool pressed, bool enabled) {
    if (!enabled) return checked ? KeyState::LitInert : KeyState::Disabled;
    return (checked || pressed) ? KeyState::Lit : KeyState::Rest;
}

// A STAMP'S TONE: an authored color — or THE MIX OF AUTHORED COLORS AS
// PAINTED (architect 2026-11-18 / 2026-11-20, Arc's hover fade;
// 2026-11-20, every state's fade: "i'd like to see arc-truthful version
// instead"; THE FACE'S FADE below): up to kMaxTerms colors, each
// with its weight, taken per channel BETWEEN THE COLORS' OWN WORDS — the
// bytes the stamp stores and blends in, each color converted for the window
// first (cool_edit_paint.cpp's cue_fill_pixel) — the weighed sum ROUNDED
// ONCE, half to even, so a tone of one color is that color's word to the
// byte, on a window that converts its colors as on one that does not (a mix
// of the authored colors would be converted after it and land a level off
// between the ends). `add` merges a color it holds already (the same three
// doubles) and drops a weight under 1e-9, so A TONE WHOSE WEIGHTS ALL FALL
// ON ONE COLOR IS THAT COLOR, PLAIN — a face at rest, and a fade between two
// looks that wear one tone there (the hover's border, the print through a
// hover), paint the plain color's own road, byte for byte. A plain color
// converts to its tone by itself, so every caller that hands a GuiColor is
// unchanged.
struct StampTone {
    static constexpr int kMaxTerms = 8;
    std::array<GuiColor, kMaxTerms> color{};
    std::array<double, kMaxTerms>   weight{};
    int                             n = 0;
    StampTone() = default;
    StampTone(GuiColor c) { add(c, 1.0); }
    // `t` of `b` over `a` (the hover's two-color mix).
    StampTone(GuiColor a, GuiColor b, double t) {
        add(a, 1.0 - std::clamp(t, 0.0, 1.0));
        add(b, std::clamp(t, 0.0, 1.0));
    }
    void add(GuiColor c, double w) {
        if (!(w > 1e-9)) return;
        for (int i = 0; i < n; ++i) {
            const GuiColor& k = color[static_cast<std::size_t>(i)];
            if (k.r == c.r && k.g == c.g && k.b == c.b) {
                weight[static_cast<std::size_t>(i)] += w;
                return;
            }
        }
        if (n == kMaxTerms) return;
        color[static_cast<std::size_t>(n)]  = c;
        weight[static_cast<std::size_t>(n)] = w;
        ++n;
    }
    // THIS TONE WITH `top` LAID OVER IT AT `t` — each weight of this tone
    // times (1 − t), each of `top`'s times t: a seam's line over the face it
    // crosses, a lit key's left seam over its neighbor's face.
    StampTone over(const StampTone& top, double t) const {
        const double a = std::clamp(t, 0.0, 1.0);
        StampTone out;
        for (int i = 0; i < n; ++i)
            out.add(color[static_cast<std::size_t>(i)],
                    weight[static_cast<std::size_t>(i)] * (1.0 - a));
        for (int i = 0; i < top.n; ++i)
            out.add(top.color[static_cast<std::size_t>(i)],
                    top.weight[static_cast<std::size_t>(i)] * a);
        return out;
    }
    // ONE COLOR: the plain road's.
    bool plain() const { return n <= 1; }
    GuiColor first() const { return n > 0 ? color[0] : GuiColor{0.0, 0.0, 0.0}; }
    // The same colors at the same weights, term for term.
    bool same(const StampTone& t) const {
        if (n != t.n) return false;
        for (int i = 0; i < n; ++i) {
            const GuiColor& a = color[static_cast<std::size_t>(i)];
            const GuiColor& b = t.color[static_cast<std::size_t>(i)];
            if (a.r != b.r || a.g != b.g || a.b != b.b ||
                weight[static_cast<std::size_t>(i)] !=
                    t.weight[static_cast<std::size_t>(i)])
                return false;
        }
        return true;
    }
};

// THE LOOKS A FACE SHOWS (architect 2026-11-20: "i'd like to see
// arc-truthful version instead"; AppState::FaceFades, app_state.h, the one
// owner of every face's fade): Arc's button states as our painters resolve
// them — REST, HOVER (a resting face under the hovering pen, Arc's
// button:hover), LIT (checked, or pressed: Arc's `:active` and `:checked`
// one look; a latched keyboard key; the knob held), DISABLED, LIT AND INERT.
// face_look folds a key's state and its hover; the scrub's knob takes Rest,
// Hover and Lit alone, the on-screen keyboard's keys those three and
// Disabled (a greyed extra key, onscreen_keyboard::key_live). A SEAM'S LINE
// takes two of them: Rest (no line) and Hover (Arc's inset line).
enum class FaceLook : uint8_t { Rest, Hover, Lit, Disabled, LitInert };
inline constexpr int kFaceLooks = 5;
inline constexpr FaceLook face_look(KeyState s, bool hovered) {
    switch (s) {
        case KeyState::Lit:      return FaceLook::Lit;
        case KeyState::Disabled: return FaceLook::Disabled;
        case KeyState::LitInert: return FaceLook::LitInert;
        case KeyState::Rest:     break;
    }
    return hovered ? FaceLook::Hover : FaceLook::Rest;
}
// Arc's `:checked` (lit or lit and inert; a pressed key is Lit too, Arc's
// `:active`): no seam line stands beside one (gtk3 CSS lines 292 / 296).
inline constexpr bool face_look_checked(FaceLook l) {
    return l == FaceLook::Lit || l == FaceLook::LitInert;
}

// A FACE AS PAINTED: the weight of each look in its picture, summing to 1 —
// one look's alone at rest, two or more through a fade, a reversal's from
// being the mix it had reached (AppState::FaceFades). Every tone of the face
// — its fill, its ring, its print and its drawing — is the same weighed mix
// of that tone's value in each look (mix_looks), so one clock moves them
// all, as GTK's `transition: all` does.
struct FaceMix {
    std::array<double, kFaceLooks> w{};
    static FaceMix of(FaceLook l) {
        FaceMix m;
        m.w[static_cast<std::size_t>(l)] = 1.0;
        return m;
    }
    double operator[](FaceLook l) const {
        return w[static_cast<std::size_t>(l)];
    }
    bool operator==(const FaceMix&) const = default;
};
// THE TONE OF A FACE AS PAINTED: Σ weight(look) · tone_of(look), as one
// StampTone — plain wherever the looks it weighs share that tone.
template <class ToneOf>
inline StampTone mix_looks(const FaceMix& m, ToneOf&& tone_of) {
    StampTone t;
    for (int i = 0; i < kFaceLooks; ++i)
        t.add(tone_of(static_cast<FaceLook>(i)),
              m.w[static_cast<std::size_t>(i)]);
    return t;
}

// THE TONES OF ONE LOOK on an Arc button (the head's THE KEY and THE HOVER;
// render.h's palette): THE FACE — `key_face` at rest, `key_hover_face`
// hovered, the accent `cue` lit, `key_dim_face` disabled, `lit_dim_fill`
// lit and inert; THE RING — `key_dim_border` disabled, `well_border` in
// every other look; THE DRAWING'S INK (key_ink below, the sRGB word) — the
// hover's the rest's; THE WORD BUTTON'S WORD (word_button_ink).
inline GuiColor key_face_tone(FaceLook l) {
    const GuiPalette& p = palette();
    switch (l) {
        case FaceLook::Hover:    return p.key_hover_face;
        case FaceLook::Lit:      return p.cue;
        case FaceLook::Disabled: return p.key_dim_face;
        case FaceLook::LitInert: return p.lit_dim_fill;
        case FaceLook::Rest:     break;
    }
    return p.key_face;
}
inline GuiColor key_ring_tone(FaceLook l) {
    const GuiPalette& p = palette();
    return l == FaceLook::Disabled ? p.key_dim_border : p.well_border;
}
// THE FILL OF ARC'S ENTRY BY LOOK (paint_entry; the head's THE HOVER):
// `key_hover_face` hovered (NOISE's entry under the hovering pen, architect
// 2026-11-24: Arc's one hover fill for a control pressed or dragged), the
// entry's own `ruler_face` in every other look — its ring, its print and
// its progress line are the same in every look.
inline GuiColor entry_fill_tone(FaceLook l) {
    const GuiPalette& p = palette();
    return l == FaceLook::Hover ? p.key_hover_face : p.ruler_face;
}

// THE INK OF A KEY'S DRAWING for its state, the compiled row's sRGB word
// (the glyph road takes the word, icons::draw_glyph): `text` at rest,
// `text_on_lit` lit, `key_dim_ink` disabled, `text_on_lit_dim` lit and inert.
inline uint32_t key_ink(KeyState s) {
    const GuiThemeTones& t = kArcDarkTones;
    switch (s) {
        case KeyState::Lit:      return t.text_on_lit;
        case KeyState::Disabled: return t.key_dim_ink;
        case KeyState::LitInert: return t.text_on_lit_dim;
        case KeyState::Rest:     break;
    }
    return t.text;
}
// THE INK OF A WORD BUTTON'S WORD: the drawing's but disabled, where the
// word wears `text_dim`, the
// dim text of every inert word (a word button is never checked, so its lit
// and inert print is never asked).
inline uint32_t word_button_ink(KeyState s) {
    return s == KeyState::Disabled ? kArcDarkTones.text_dim
                                   : key_ink(s);
}
// THE SAME TWO INKS BY LOOK (the hover's the rest's), and AS PAINTED: the
// face's mix of them as one StampTone (the ink road takes it,
// paint_key_glyph's tone overload and set_stamp_tone_source).
inline uint32_t key_ink(FaceLook l) {
    switch (l) {
        case FaceLook::Lit:      return key_ink(KeyState::Lit);
        case FaceLook::Disabled: return key_ink(KeyState::Disabled);
        case FaceLook::LitInert: return key_ink(KeyState::LitInert);
        case FaceLook::Rest:
        case FaceLook::Hover:    break;
    }
    return key_ink(KeyState::Rest);
}
inline uint32_t word_button_ink(FaceLook l) {
    return l == FaceLook::Disabled ? kArcDarkTones.text_dim
                                   : key_ink(l);
}
inline StampTone key_ink_tone(const FaceMix& m) {
    return mix_looks(m, [](FaceLook l) { return hex(key_ink(l)); });
}
inline StampTone word_button_ink_tone(const FaceMix& m) {
    return mix_looks(m, [](FaceLook l) { return hex(word_button_ink(l)); });
}

// THE KEY'S LENGTHS (the head's THE KEY; render.h's case block owns the
// box): THE BOX kKeyWidthW x kKeyHeightW (icon_case_w_px /
// icon_case_h_px, 36 x 28 W, 72 x 56 device px on the tablet at 200 %), BORDER INCLUDED ("the border is clickable"); THE BORDER one W
// (hairline_px, the line rule: 2 device px on the tablet, floored at one);
// THE OUTER CORNER kKeyCornerW, 3 W (Arc's button border-radius, gtk3
// CSS), 6 device px, floored at one; the faces' corners inside the border the outer
// less the border (GTK's padding box), 4 device px. Each through scaled_px,
// rounded at the element.
inline constexpr double kKeyCornerW = 3.0;
inline int key_border_px() { return hairline_px(); }
inline int key_corner_px() { return scaled_px(kKeyCornerW, 1); }
inline int key_inner_corner_px() {
    return std::max(0, key_corner_px() - key_border_px());
}

// THE LINKED GROUP'S PITCH (Arc's `.linked`, gtk3 CSS): two
// neighbors SHARE ONE SEAM COLUMN, so a key stands one box less one border
// right of the one before it — 70 device px on the tablet (72 − 2), 35 at
// 100 % — and A GROUP OF n IS key + (n − 1)(key − 1 W) WIDE, each part
// rounded on its own (0 for none): every group walk's (the top icon row's
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
// hover (architect 2026-11-18: "a seam column belongs to the
// left key") — one border on every member but a group's first, 0 there.
inline int linked_key_seam_px(int i) {
    return i > 0 ? key_border_px() : 0;
}

// ONE LINKED GROUP on the backbuffer: n = states.size() keys from (x, y),
// each linked_key_box(x, y, i) — in this paint order,
// every edge at exact coverage over the ground the caller laid under the
// same clip: (1) THE BORDER, the group's rounded box (its four outer corners
// key_corner_px), EACH MEMBER'S STRETCH OF IT IN ITS OWN RING TONE —
// `key_dim_border` on a DISABLED member's, `well_border` on every other's —
// the stretches cut where the faces are: a member's from its box's left edge
// (one border further in past a group's first, its left seam's border rows
// the left key's) to its box's right edge, a whole column, the outer corners
// on the group's first and last; one stamp a run of neighbors sharing a
// tone, so EVERY RING CELL CARRIES ONE INK AT ITS ONE COVERAGE (a disabled
// corner's edge cell is the disabled ring alone over the ground, never the
// ring tone under it); (2) THE FACES, the group's inner box (its corners
// key_inner_corner_px) cut at each member's own columns: from one border
// inside its box's left edge to one border inside the NEXT box's left edge —
// so A SEAM WEARS THE LEFT KEY'S FACE — the last to one border inside its
// own right edge, each in its state's face (`key_face` at rest, the accent
// `cue` lit, `key_dim_face` disabled, `lit_dim_fill` lit and inert), each
// face stamped once, so a lit end member's inner corner is the accent alone
// over the ring; (3) A LIT MEMBER'S LEFT SEAM, `cue`, its first border's
// columns between the border's top and bottom rows, whole cells over the
// left key's face — so the border stays its tone round a lit key and a lit
// key takes both its seams whatever its neighbors; (4) THE SEAM'S LINE,
// Arc's inset line on the right key (gtk3 CSS line 292), whole cells
// between the border's rows, (3)'s tone mixed toward `well_border` by
// `seam[i]`, the stored mix of the line between members i − 1 and i (index
// 0 unread; an empty span is no line).
// EACH MEMBER IS HANDED AS PAINTED (THE FACE'S FADE, architect 2026-11-20:
// "i'd like to see arc-truthful version instead"; the hover's fade the
// same): `faces[i]` member i's FaceMix, the weight of each look in its
// picture (AppState::FaceFades::as_painted, the one owner), so every tone
// above is that mix of its look tones (mix_looks: the face key_face_tone,
// the ring stretch key_ring_tone — the runs of (1) join where two members'
// mixed ring tones are one — and (3)'s accent laid over the left key's face
// by the member's Lit weight); a member at rest in one look paints that
// look's tones plain, byte for byte the unmixed picture. The line's mix is
// the seam's own (AppState::FaceFades' THE SEAMS), not its two members'
// larger one: through a slide from one key to its neighbor the two faces
// cross while the line between them holds the border tone, and beside a
// member going lit it fades out on the right key's own clock (its target
// is no line beside a checked member, Arc's `:not(:checked)` twice in line
// 292 and line 296's `button:checked + button:hover { box-shadow: none }`;
// a DISABLED member forbids nothing, line 292's sibling half asking nothing
// of `:disabled`), so the painter draws whatever the stored mix says. EVERY
// PIXEL A MEMBER'S STATE, ITS FADE OR ITS LINE MOVES LIES INSIDE ITS OWN
// BOX, its corners included (a seam column is inside both boxes it joins):
// a member's damage is its box, a seam's its own columns, and a repaint
// under any clip paints every member and every line from its own stored
// mix. THE ROAD: one
// CoverageStampPass (cool_edit_paint.h), every shape a rounded rect with real
// edges stamped straight into the target in the order above — NOTHING
// ELSE IS DRAWN between begin() and end() (the drawings go on after, the
// caller's paint_key_glyph), so the stamp and cairo never interleave. The
// glyphs are the caller's.
void paint_linked_group(cairo_t* cr, int x, int y,
                        std::span<const FaceMix> faces,
                        std::span<const double> seam = {});

// ONE WORD BUTTON on `box` (Arc's text button; architect 2026-11-18: "the
// word alone, no glyph", Delete a plain button; in GTK's box order): THE
// FACE on the
// padding box (`box` one border in, its corners key_inner_corner_px), THEN THE
// RING over it — the outer rounded box (key_corner_px) less the padding box
// — each at exact coverage, so an outer corner mixes the ground and the ring
// alone and an inner corner pixel all three: at rest `key_face` in
// `well_border`; lit (pressed) the accent `cue` in `well_border`; disabled
// `key_dim_face` in `key_dim_border`; hovered `key_hover_face` in
// `well_border` — EACH AS PAINTED, `face` the button's FaceMix
// (AppState::FaceFades::as_painted; mix_looks of key_face_tone and
// key_ring_tone), plain at rest in one look. The word is the caller's
// (word_button_ink_tone). One stamp pass, nothing drawn inside it.
void paint_word_button(cairo_t* cr, const GuiRect& box, const FaceMix& face);

// ARC'S ENTRY on `box` (architect 2026-11-19; Arc's gtk3 CSS `entry`) —
// NOISE's case and the value sub-page's field (the
// time fields plain text, architect 2026-11-19), ONE PAINTER: GTK's box
// order as the word
// button's — THE FILL on the padding box (`box` one border in, corners
// key_inner_corner_px) in `ruler_face`, the canvas's tone (Arc's
// $base_color, 404552 on arc_dark), THEN THE RING over it (the outer rounded
// box, corners key_corner_px, less the padding box) in `well_border` — and,
// with `progress` t in (0, 1], ARC'S ENTRY PROGRESS over the fill: the
// padding box (its own inner corners) CUT to its last kEntryProgressW (2 W)
// rows and to its first t of its width, in the accent `cue` — so the line's
// left end follows the inner corner's arc and its right end stands at its
// real x. FOCUSED (`focused`, 2026-11-19: the value
// sub-page's entry, which holds the focus for as long as it stands —
// Arc's entry:focus) AN INNER ONE-W LINE OF THE
// ACCENT `cue` on all four sides inside the ring: the padding box in the
// accent, the fill one W further in over it (its corners one W less). THE
// FILL AS PAINTED (architect 2026-11-24, NOISE's hover): `face` the entry's
// FaceMix (AppState::FaceFades::as_painted, the one owner), the fill
// mix_looks of entry_fill_tone — `key_hover_face` by its Hover weight over
// `ruler_face` — the ring and the progress line unmixed; the default, Rest
// alone, is the plain `ruler_face`, byte for byte (the value sub-page's
// focused entry never hovers and passes none). Every
// edge at exact coverage over the ground the caller laid; one stamp pass,
// nothing drawn inside it; the text is the caller's.
// ITS OWN LENGTHS, ARC'S (gtk3 CSS `entry`: min-height 28 px, padding 8 px
// inside a 1 px border; every Arc px one W): kEntryHeightW (28 W) THE HEIGHT OF AN
// ENTRY THAT STANDS ON NO KEY ROW — the value sub-page's (modal_box.h's
// modal_page_lines; NOISE's, on the top icon row, is the key's height, 28 W
// at the default) — and kEntryPenW (9 W) ITS TEXT'S PEN inside its left
// edge, the border and the pad, the same 9 W kept clear inside its right
// edge (a text is cut at the two inner edges); the baseline by the line-box
// rule in the entry's height (+18 W of the 28).
inline constexpr double kEntryProgressW = 2.0;
inline constexpr double kEntryHeightW   = 28.0;
inline constexpr double kEntryPenW      = 9.0;
void paint_entry(cairo_t* cr, const GuiRect& box,
                 std::optional<double> progress = std::nullopt,
                 bool focused = false,
                 const FaceMix& face = FaceMix::of(FaceLook::Rest));

// A POINT PRESSES A KEY EXACTLY WHERE THE KEY IS PAINTED (the head's THE
// KEY; architect 2026-11-02: "ground is not a button"): `key` the
// PAINTED BOX — border included ("the border is clickable") —
// the walk's own rect for every key of the top icon row, the bottom icon
// row and the controls rows. THE ONE PREDICATE every key hit test of those
// surfaces asks: the roster's face (AppState::RedesignButtonFace::contains —
// the press, the lift, the chrome's modifier gate
// (chrome_band_modifiers_refused), the tooltip's owner) and the
// modal family's buttons (GuiInputHandler::modal_dialog_button_hit — the
// press and the lift). The on-screen keyboard's grid keeps the same rule on
// its own shared lines (onscreen_keyboard::key_at: a line the key's left of
// it or above it). Each painter PUBLISHES the box it paints, so a reader
// re-shapes nothing. A gesture already begun keeps its rules: a press that
// slides off its key reads "outside".
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
