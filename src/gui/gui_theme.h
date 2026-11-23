#pragma once

#include <cstdint>

// THE LOOK'S TONES, COMPILED (architect 2026-11-21: "the alt end
// state is what i effectively want and it seems cheaper to get there - we
// would still track arc-dark provenance for each color in the comments").
// One look, GTK's Arc-Dark at 9 pt ("i am committed"), so its tones are ONE
// COMPILED ROW, kArcDarkTones below: no file is read for a color, nothing
// derives one at run time and no key spells one. render.cpp's
// install_palette fills every GuiPalette member off this row at the launch
// (palette_file.h's fill table); the few readers that need the sRGB word
// itself (the glyph road's ink, key_ink in materials.h; the plate's two
// inks, waveform_plate_inks; the carried fills' opacity, tone_alpha;
// NOISE's dimmed number, entry_dim_text) read the row directly.

// Every sRGB word the GUI paints (0xRRGGBB; THE THREE CARRIED FILLS
// 0xAARRGGBB, below). What reads each member and where its number comes from
// is told at its line of kArcDarkTones.
struct GuiThemeTones {
    uint32_t ground;
    uint32_t body_face;
    uint32_t text;
    uint32_t title_text;
    uint32_t sidebar_text;
    uint32_t text_dim;
    uint32_t inactive_cue_label_hover;
    uint32_t text_on_lit;
    uint32_t key_dim_ink;
    uint32_t entry_dim_text;
    uint32_t key_dim_face;
    uint32_t key_dim_border;
    uint32_t text_on_lit_dim;
    uint32_t key_face;
    uint32_t key_hover_face;
    uint32_t list_hover_face;
    uint32_t lit_dim_fill;
    uint32_t ruler_face;
    uint32_t ruler_tick;
    uint32_t ruler_text;
    uint32_t lane_face;
    uint32_t trim_bar_face;
    uint32_t trim_span_face;
    uint32_t trim_span_hover_face;
    uint32_t well_border;
    uint32_t selection_fill;
    uint32_t selection_text;
    uint32_t score_pane_face;
    uint32_t scale_trough;
    uint32_t scale_slider_border;
    // THE THREE CARRIED FILLS (architect 2026-11-19): each word
    // 0xAARRGGBB, THE TOP BYTE ITS OPACITY, painted at it over whatever lies
    // under the box — the chrome, the lanes, the canvas — so no surface is
    // known to composite it over ahead of time: the opacity rides to the
    // painter (tone_alpha, render.h; set_palette_source_alpha on the CPU, the
    // GPU's blend inside the floaters' hulls, canvas_sink.h's THE
    // TRANSPARENCY INVARIANT). Every other member's top byte is 0.
    uint32_t tooltip_fill;
    uint32_t card_fill;
    uint32_t card_standing_fill;
    uint32_t card_ink;
    uint32_t limiter_lamp;
    uint32_t clip_lamp;
    uint32_t waveform_canvas;
    uint32_t waveform_ink;
    uint32_t waveform_core;
    uint32_t grid;
    uint32_t center;
    uint32_t cue;
    uint32_t cue_fill;
    uint32_t cue_hover;
    uint32_t cue_outline;
    uint32_t inactive_cue;
    uint32_t inactive_cue_hover;
    uint32_t playhead_head;
    uint32_t playhead_line;
    uint32_t hover_line;
    uint32_t scanner;
};

// ARC-DARK'S TONES, THE ONE ROW. Every number is read from arc-theme's
// sources: jnsh/arc-theme (the fork Arch packages), commit
// 94ac8c7d67d68de0cc688bbd4c3105b9815b446e (2025-10-18), GPL-3.0, variant
// 'dark', transparency off — common/gtk-3.0/sass/_colors.scss, _common.scss
// and common/xfwm4/dark/assets.svg, the sass's lighten / darken / mix
// resolved to sRGB bytes, and the compiled GTK 3 sheet's selectors quoted
// below (no file of arc-theme ships; THIRD_PARTY.md credits it). Arc-Dark is
// dark throughout (Arc-Darker's light content panels not used). A few numbers
// were MEASURED ON HIS ARC-DARK DESKTOP (his shots, named at their lines),
// and a few are OURS (named so). A COMPUTED TONE is told as its arithmetic
// with its inputs' numbers: "X at t over Y" is, per channel,
// Y + t · (X − Y) rounded half to even (an alpha byte aa is t = aa / 255),
// each inner word rounded before it is reused. Two members may hold one
// number and stay two (`cue` / `cue_fill`, `waveform_canvas` /
// `ruler_face`): a tone is what it paints, not its value. The bytes are
// sRGB; the tablet converts each at the painter's entry
// (display_transform.h's head).
//
// THE PROOF IS THE DUMP, NOT AN ASSERT: the row was checked word for word
// by a dump against the arithmetic its lines tell; a static_assert of these
// literals against the same literals would prove nothing, so none pins them.
//
// THE WAVEFORM'S BRIGHTNESS ORDER IS HIS (architect 2026-11-21):
// the scanner and the zoom anchor's stem FFFFFF ("the scanner should be this
// brightest one"), the playhead's stem A5ABB5, the hover line 767B87 ("an
// even more dark and faint version of the playhead stem").
//
// KEEP `.ground` AND `.text` EACH ON ONE LINE, `    .<member> = 0x<RRGGBB>,`
// exactly (six uppercase hex digits): apk/build_apk.sh reads the launcher
// icon's ground and ink off those two lines with one sed each.
inline constexpr GuiThemeTones kArcDarkTones = {
    // THE TITLE TONE: Arc's $header_bg (= wm_bg), the headerbar and the
    // titlebar, the darkest main chrome. The title bar's ground alone
    // (paint_title_bar, the palette's `ground`; the body under it wears
    // `body_face`, architect 2026-11-18: "the darkest color would
    // only apply to the top line, the title bar") and the launcher icon's
    // ground (apk/build_apk.sh).
    .ground = 0x2F343F,
    // THE BODY TONE: Arc's $bg_color (`@define-color bg_color #383C4A`).
    // The ground of everything under the title bar — the palette's
    // `body_face` and the panel's `face`: both icon rows, the status bar, the
    // erase, the modal pane's and the on-screen keyboard's ground
    // (architect 2026-11-19).
    .body_face = 0x383C4A,
    // THE FONT COLOR: Arc's $fg_color (`@define-color fg_color #D3DAE3`).
    // Every word, digit and drawing the chrome prints — the palette's
    // `text` and `field_text`; a key's drawing at rest (key_ink,
    // materials.h); NOISE's number — and the launcher icon's ink
    // (apk/build_apk.sh).
    .text = 0xD3DAE3,
    // THE TITLE BAR'S TEXT AND DRAWINGS: xfwm4's title text on Arc-Dark's
    // titlebar (architect 2026-11-19; paint_title_bar, its
    // palette member and its glyphs' word).
    .title_text = 0xAFB8C5,
    // THE SIDEBAR TEXT: Arc's $dark_sidebar_fg. The live marker labels' ink,
    // the tooltip's words and a normal card's words and glyph (architect
    // 2026-11-19).
    .sidebar_text = 0xBAC3CF,
    // THE DIM TEXT: Arc's insensitive text, $insensitive_fg_color (the font
    // color at 45 % alpha), `button:disabled label { color: rgba(211, 218,
    // 227, 0.45); }` read flat — the font color D3DAE3 at 45 % over the body
    // tone 383C4A -> 7E838F. The inert rows, the
    // tooltip's hint line, a disabled marker's label at rest and its strike
    // bar, a disabled word button's word (word_button_ink, materials.h).
    .text_dim = 0x7E838F,
    // A DISABLED MARKER'S LABEL UNDER THE HOVERING PEN (its rest `text_dim`):
    // Arc's dim button under its row's hover, `.gedit-document-panel
    // row:hover:not(:selected) button { color: #9da3ad; }` (architect
    // 2026-11-21: "just make sure they're all colors from Arc,
    // dark").
    .inactive_cue_label_hover = 0x9DA3AD,
    // THE PRINT ON A LIT FACE: Arc's $selected_fg_color, white on its
    // checked blue. A lit key's glyph and the on-screen keyboard's lit cap
    // (key_ink, materials.h).
    .text_on_lit = 0xFFFFFF,
    // A DISABLED KEY'S GLYPH and the struck clip lamp's square: an
    // insensitive button's drawing MEASURED ON HIS ARC-DARK DESKTOP (shot
    // B3b, 2026-11-18; architect 2026-11-19), opaque over the key
    // face (key_ink, materials.h; the palette's `key_dim_ink`).
    .key_dim_ink = 0x7F8591,
    // A DISABLED ENTRY'S NUMBER, OURS: the level case's number while NOISE
    // is greyed (paint_level_case, paint_handler.cpp), after Arc's
    // insensitive entry — the font color D3DAE3 at 35 % over the entry's
    // fill 404552 -> 737985.
    .entry_dim_text = 0x737985,
    // A DISABLED KEY'S AND WORD BUTTON'S FACE: Arc's `button:disabled {
    // background-color: rgba(68, 74, 88, 0.55); }` — the key face 444A58 at
    // 55 % over the body tone 383C4A -> 3F4452 (one level off on two
    // channels from the 3E4451 measured on his desktop; the formula taken).
    .key_dim_face = 0x3F4452,
    // THAT KEY'S BORDER on its stretch of the ring: Arc's `button:disabled {
    // border-color: rgba(43, 46, 57, 0.55); }` — the well's border 2B2E39 at
    // 55 % over the body tone 383C4A -> 313441 (the measured 313440 one
    // level off; the formula taken).
    .key_dim_border = 0x313441,
    // A CHECKED-BUT-DISABLED KEY'S GLYPH, DIMMED WITH THE BUTTON as GTK's
    // `opacity` dims its label (architect 2026-11-19: "let's do arcs
    // numbers"): Arc's `button:disabled:checked label { color:
    // rgba(255, 255, 255, 0.8); }` under the button's `opacity: 0.6` — the
    // print FFFFFF at 80 % over the face's inner word (the accent 5294E2 at
    // 75 % over the body tone 383C4A -> 4C7EBC) -> DBE5F2, at 60 % over the
    // body tone 383C4A -> 9AA1AF (key_ink, materials.h).
    .text_on_lit_dim = 0x9AA1AF,
    // A KEY'S AND A WORD BUTTON'S FACE AT REST, the two lamps at rest, the
    // scrub's knob: Arc's $button_bg, lighten($base_color, 2%) (`scale
    // slider { background-color: #444a58; }`); the near number beside it, a
    // headerbar button, 40 % lighten($header_bg, 22%) over $header_bg, is
    // 424958.
    .key_face = 0x444A58,
    // THAT FACE UNDER THE HOVERING PEN, the hover's far end — a key's, a word
    // button's, an on-screen keyboard key's, the scrub's knob's: Arc's
    // `button:hover { background-color: #505666; }`, MEASURED ON HIS DESKTOP
    // too (the word button B2b and the linked pair C2d, 2026-11-20)
    // (architect 2026-11-18: "lets go arc truthful re: hoevers"; materials.h's
    // THE HOVER).
    .key_hover_face = 0x505666,
    // A LIST ROW UNDER THE HOVERING PEN: Arc's `row.activatable:hover {
    // background-color: rgba(255, 255, 255, 0.03); }` over the list's view
    // 404552 — c + 0.03 · (255 − c) per channel, rounded -> 464B57, the
    // stylesheet's own number computed exactly (architect 2026-11-21: "I
    // prefer to go more truthful. It just needs to be very subtle."; the
    // desktop's 8-bit compositing, the alpha as 7 / 255, may read one level
    // under on two channels, 454A57, reasoned, not measured — the formula
    // taken; paint_handler.cpp's THE LIST VIEW).
    .list_hover_face = 0x464B57,
    // A CHECKED-BUT-DISABLED KEY'S FACE (a lamp on a locked tab or under grid
    // iterations, a lit key under an overlay): Arc's `button:disabled:checked { background-color:
    // rgba(82, 148, 226, 0.75); opacity: 0.6; }` — the accent 5294E2 at
    // 75 % over the body tone 383C4A -> 4C7EBC, at 60 % over the body tone
    // -> 44648E (architect 2026-11-19: "let's do arcs numbers";
    // paint_linked_group, materials.h).
    .lit_dim_fill = 0x44648E,
    // THE RULER, Arc's entry's fill (paint_entry, NOISE) and the flag
    // editor's, the lists' and pages' view: Arc's $base_color, the canvas's
    // own number — THE RULER WEARS THE CANVAS'S TONE (architect 2026-11-17:
    // "otherwise it looks too bright").
    .ruler_face = 0x404552,
    // THE RULER'S TICKS: Arc's `scale marks { color: alpha(currentColor,
    // 0.4); }` of the font color — D3DAE3 at 40 % (the byte 66) over the
    // ruler 404552 -> 7B818C.
    .ruler_tick = 0x7B818C,
    // THE RULER'S DIGITS: Arc's `.dim-label { opacity: 0.55; }` of the font
    // color over the ruler's ground — D3DAE3 at 55 % over 404552 -> 9197A2,
    // under the triangle A5ABB5 and over the hover line 767B87 (architect
    // 2026-11-21; paint_ruler_row).
    .ruler_text = 0x9197A2,
    // THE MARKER LANE: Arc's $dark_sidebar_bg (`@define-color text_view_bg
    // #353945`), between $header_bg and $bg_color.
    .lane_face = 0x353945,
    // THE TRIM BAR'S 12-W TROUGH under the pill: Arc's scrollbar trough,
    // `scrollbar { background-color: #3e434f; }`, $_scrollbar_bg_color =
    // darken($base_color, 1%) (architect 2026-11-17: "the scroll bar and
    // the trough should be accurate to whatever Arc uses").
    .trim_bar_face = 0x3E434F,
    // THE TRIM BAR'S PILL AT REST, the span: Arc's scrollbar slider,
    // `scrollbar slider { background-color: #767b87; }`, mix($fg_color,
    // $bg_color, 40%) (render.h's trim_bar_tones).
    .trim_span_face = 0x767B87,
    // THE TRIM BAR'S PART UNDER THE HOVERING PEN, the pill's body or an
    // end's cap: Arc's `scrollbar slider:hover { background-color: #676b78;
    // }`, Thunar's slider under the pointer as MEASURED ON HIS DESKTOP
    // (2026-11-20; trim_bar_tones; GuiInputHandler::walk_trim_bar_hover).
    .trim_span_hover_face = 0x676B78,
    // THE WELL'S LINES — the one-W lines across the panel, the stereo
    // divider, the status bar's line — and the keys', word buttons' and
    // entries' rings: Arc's $borders_color (`@define-color borders
    // #2b2e39`), darken($bg_color, 6%), Thunar's pane border (architect
    // 2026-11-17: "lets use the thunar border").
    .well_border = 0x2B2E39,
    // THE SELECTION'S BAR, the palette's `selected_fill`: the lists' lit row
    // and the editors' selected substring. Arc's $selected_bg_color, its one
    // selection.
    .selection_fill = 0x5294E2,
    // THE SELECTION'S TEXT, the palette's `selected_text`, the bar's own
    // partner: Arc's $selected_fg_color.
    .selection_text = 0xFFFFFF,
    // THE SCORE VIEWER'S PANE, the page's mat: the waveform's ink, Arc's
    // $header_border, the one tone reused (modal_box.h's score pane).
    .score_pane_face = 0x262A33,
    // THE PROGRESS PAGE'S BAR'S TROUGH AND THE SCRUB'S: Arc's
    // $_trough_bg_color, `progressbar trough { background-color: #2d303b; }`
    // and `scale trough { background-color: #2d303b; }` (architect
    // 2026-11-19: GTK's progress bar).
    .scale_trough = 0x2D303B,
    // THE SCRUB'S KNOB'S ONE-W RIM AT REST: Arc's `scale slider { border:
    // 1px solid #262933; }`, MEASURED ON HIS DESKTOP too (the scale shot
    // C4g, 2026-11-19; paint_scale).
    .scale_slider_border = 0x262933,
    // THE TOOLTIP'S BOX (paint_shift_tooltip): Arc's $osd_bg_color at .95,
    // `tooltip.background { background-color: rgba(75, 81, 98, 0.95); }` —
    // .95 · 255 = 242.25, the byte F2 (architect 2026-11-19: "if we
    // can do the transparency, we might as well").
    .tooltip_fill = 0xF24B5162,
    // A NORMAL (TIMED) CARD'S BOX (paint_notifications): xfce4-notifyd's
    // bubble under Arc, $dark_sidebar_bg 353945 at .95, the byte F2.
    .card_fill = 0xF2353945,
    // A STANDING (CRITICAL) CARD'S BOX: Arc's $destructive_color,
    // `button.destructive-action { background-color: #F04A50; }`, at .95,
    // the byte F2.
    .card_standing_fill = 0xF2F04A50,
    // A STANDING CARD'S WORDS AND CLASS GLYPH, the palette's `card_text`:
    // Arc's white on its destructive red, `button.destructive-action {
    // color: white; }`, its $selected_fg_color (a normal card's print is
    // `sidebar_text`).
    .card_ink = 0xFFFFFF,
    // THE TITLE BAR'S LIMITER LAMP, LIT: Arc's $warning_color
    // (`@define-color warning_color #F27835`) (architect 2026-11-05: "orange
    // for limiter and red for clipper").
    .limiter_lamp = 0xF27835,
    // THE TITLE BAR'S CLIP LAMP, LIT AND NOT STRUCK — THE CLOSE RED: Arc's
    // xfwm4 / headerbar close disc, `@define-color wm_button_close_bg
    // #cc575d;` (its hover D7787D, its press BE3841), on the clip lamp alone
    // (architect 2026-11-14: red reads as a warning or a close).
    .clip_lamp = 0xCC575D,
    // THE CANVAS: Arc's $base_color (`@define-color base_color #404552`),
    // its content base, Thunar's list view (architect 2026-11-17: "Thunar's
    // is the official"; render_canvas).
    .waveform_canvas = 0x404552,
    // THE WAVEFORM, the plate's outer bar (waveform_plate_inks, render.h):
    // Arc's $header_border, darken($header_bg, 4%) — one step above the
    // selection stem 21252B, CIELAB L* 2.50 (the stem-over-ink rule's 3.61,
    // ballpark).
    .waveform_ink = 0x262A33,
    // THE MAGNIFIED INNER BAR'S SOLID FILL (waveform_plate_inks): THE KEY
    // FACE, Arc's $button_bg (architect 2026-11-07, his pick on real
    // MAGNIFY-lit captures: a lighter core from a tone the palette
    // already holds, no halo; render.h's THE CORE).
    .waveform_core = 0x444A58,
    // THE CANVAS'S QUARTER LINES (render_canvas): Arc's separator
    // $borders_color 2B2E39 at 35 % (the byte 59) over the canvas 404552 ->
    // 393D49.
    .grid = 0x393D49,
    // EACH CHANNEL'S ZERO LINE (render_center_lines): the waveform's ink,
    // Arc's $header_border — the zero line IS the ink.
    .center = 0x262A33,
    // THE ACCENT: Arc's $selected_bg_color. A lit or pressed key's face —
    // every checked key's, Arc's `:checked` and `:active` one look (architect
    // 2026-11-19) — NOISE's progress line, the on-screen keyboard's lit
    // keys, the scrub's played part and its held knob, the flag editor's
    // border (materials.h's THE KEY).
    .cue = 0x5294E2,
    // EVERY ENABLED MARKER'S CHIP, selected or not, and the history view's
    // added and changed items (resolve_flag_face): the accent's number, its
    // own member (architect 2026-11-17: "the marker should take selection
    // blue").
    .cue_fill = 0x5294E2,
    // AN ENABLED MARKER'S CHIP UNDER THE HOVERING PEN: Arc's hover of a blue
    // thing, `.documents-entry-tag:hover { background: #639fe5; }` (the tag
    // at rest 5294E2, the accent) (architect 2026-11-21).
    .cue_hover = 0x639FE5,
    // THE SELECTION STEM down the canvas and the phase-reset lead-in ring
    // (no chip and no ruler head wears an outline): the xfwm4 frame's edge
    // fill (assets.svg); _common.scss's dark window outline,
    // darken($header_bg, 7%), resolves to about 20232B.
    .cue_outline = 0x21252B,
    // A DISABLED MARKER'S CHIP (and the history view's removed items'):
    // Arc's titlebar glyph of an unfocused window, `@define-color
    // wm_icon_unfocused_bg #666A74;` — a fully clickable thing at rest, as
    // a disabled marker is.
    .inactive_cue = 0x666A74,
    // A DISABLED MARKER'S CHIP UNDER THE HOVERING PEN: the same titlebar
    // glyph awake, `@define-color wm_icon_bg #90939B;` (architect
    // 2026-11-21: a disabled marker "is ignored by the parser, but
    // otherwise fully clickable and actionable").
    .inactive_cue_hover = 0x90939B,
    // THE PLAYHEAD'S TRIANGLE IN THE RULER: the stem's color, Arc's `.h4,
    // .category-label { color: #a5abb5; }` (architect 2026-11-21:
    // "the triangle for the playhead should probably become the same color
    // as the stem").
    .playhead_head = 0xA5ABB5,
    // THE PLAYHEAD'S STEM ON THE WAVEFORM, its one static tone
    // (render_playhead): Arc's `.h4, .category-label { color: #a5abb5; }`
    // (architect 2026-11-21: "the stem is too bright" at the font
    // color).
    .playhead_line = 0xA5ABB5,
    // THE WAVEFORM'S HOVER LINE AND ITS TRIANGLE IN THE RULER: Arc's
    // scrollbar slider, `scrollbar slider { background-color: #767b87; }`
    // (architect 2026-11-21; the triangle: "same color as
    // the hover stem. for symmetry"; render_hover_line).
    .hover_line = 0x767B87,
    // THE SCANNER AND THE ZOOM ANCHOR'S STEM: OURS, white, canonical, no
    // Arc number (architect 2026-10-09: "the non-playhead stems that are
    // related to the controls should be white"; 2026-11-21: "the
    // scanner should be this brightest one").
    .scanner = 0xFFFFFF,
};
