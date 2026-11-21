#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// THE THEME'S SHAPE (architect 2026-11-05, the theme arc: "i'd like to
// implement all" — Ableton Live 12's own themes selectable whole, chrome and
// well together, through the one `theme` device key; architect 2026-11-11,
// session 110: THE THEME FILES, our own keyed format of Live's raw roles,
// shipped in the APK). A theme is one row of sRGB words, GuiThemeTones,
// under a slug the config spells and a group the Settings page's value list
// heads it with. THE LIST — SYSTEM (`default` first), EXTRA, USER-DEFINED,
// each theme file through THE DERIVATION — is theme_roles.h's, whose head
// owns the format, the groups, the order, the left-out files and the
// derivation. This header holds what needs no file: the row's shape, the
// compiled reference row, the list's own slugs, the groups' headings and
// THE SLUG RULE.

// Every sRGB word the GUI paints, one theme's (0xRRGGBB). Beside each member
// what reads it and the Live role it comes from (the one owners: materials.h's
// head, palette_file.h's role list, render.h's palette block; the derivation,
// theme_roles.h's head). A row of the list holds the theme's own numbers; the
// installed row its bright tones mixed toward its ground at the `bright_mix`
// key and every tone derived from them recomputed (theme_roles.h's THE BRIGHT
// MIX, bright_mixed_tones).
struct GuiThemeTones {
    uint32_t ground;              // the flat ground everywhere outside the well and the keys (SurfaceArea); THE TITLE TONE on the Arc page, the bright mix's target and the launcher icon's ground
    uint32_t body_face;           // THE BODY TONE, the ground of everything under the title bar (WindowBackground, our own role; a Live theme's SurfaceArea): the palette's `face`, both icon rows, the status bar, the erase, the modal pane's and the keyboard's ground
    uint32_t text;                // every word, digit and drawing the chrome prints; `label` / `field_text` (ControlForeground)
    uint32_t title_text;          // the title bar's text and drawings (TitleForeground, our own role; a Live theme's ControlForeground; paint_title_bar)
    uint32_t sidebar_text;        // the live marker labels' ink, the tooltip's words and a normal card's words and glyph (SidebarForeground, our own role; a Live theme's ControlForeground)
    uint32_t text_dim;            // the inert rows and the tooltip's hint line (TextDisabled)
    uint32_t text_on_lit;         // what is printed on a lit key's face, its glyph and the on-screen keyboard's lit cap (StandbySelectionForeground, the selection's text); a word alone, read by key_ink (materials.h)
    uint32_t key_dim_ink;         // a disabled key's glyph (ViewCheckControlDisabledOff over the face)
    uint32_t key_dim_face;        // a disabled key's and word button's face (the key face at 55 % over the body tone, theme_roles.h's DERIVED block)
    uint32_t key_dim_border;      // a disabled key's border on its stretch of the ring (well_border at 55 % over the body tone, the same block)
    uint32_t text_on_lit_dim;     // a checked-but-disabled key's glyph on its dimmed face ((text_on_lit at 80 % over the accent at 75 % over the body tone) at 60 % over the body tone, theme_roles.h's DERIVED block); a word alone, read by key_ink
    uint32_t key_face;            // a key's and a word button's face at rest, the scrub's knob's fill (SurfaceBackground)
    uint32_t key_hover_face;      // that face under the hovering pen, the hover mix's far end: a key's, a word button's, an on-screen keyboard key's, the scrub's knob's fill (ButtonHoverBackground, our own role; arc_dark 505666, Arc's button:hover; a Live theme's SurfaceBackground, the key face itself: no hover)
    uint32_t list_hover_face;     // a list row under the hovering pen: ARC'S ROW HOVER, rgba(255, 255, 255, 0.03) over the list's view (ruler_face), c + 0.03 · (255 − c) rounded, the stylesheet's own number computed exactly (theme_roles.h's DERIVED block; arc_dark 404552 -> 464B57; a theme whose key_hover_face is its key_face, every Live theme, the view's own tone: no hover)
    uint32_t lit_dim_fill;        // a checked-but-disabled key's face: (the accent at 75 % over the body tone) at 60 % over the body tone, Arc's numbers
    uint32_t ruler_face;          // the ruler, Arc's entry's fill and the flag editor's (= the canvas, waveform_canvas: DetailGridTiles over DetailViewBackground)
    uint32_t ruler_tick;          // the ruler's ticks (DetailViewRulerMarkings over the ruler face, the canvas)
    uint32_t ruler_text;          // the ruler's digits (the text at 80 % over the ruler face, theme_roles.h's DERIVED block; paint_ruler_row)
    uint32_t lane_face;           // the marker lane (Desktop)
    uint32_t trim_bar_face;       // the trim bar's trough, the 12-W lane under the pill (ScrollbarInnerTrack over the ground)
    uint32_t trim_span_face;      // the trim bar's pill at rest, the span (ScrollbarInnerHandle over Desktop)
    uint32_t trim_span_hover_face; // the pill under the hovering pen (ScrollbarSliderHover, our own role, over Desktop; arc_dark 676B78, Thunar's slider under the pointer; a Live theme's ScrollbarInnerHandle): the trim bar's hovered part, the pill's body or an end's cap (render.h's trim_bar_tones, send CC 2026-11-20)
    uint32_t well_border;         // THE WELL'S LINES: its one-W lines across the panel (under the top icon row, the trough, the ruler, the marker lane, the well's foot), the stereo divider and the status bar's line (ClipBorder over the ground)
    uint32_t selection_fill;      // the selection pair's bar: the lists' lit row, the editors' selected substring; `selected_fill` (StandbySelectionBackground)
    uint32_t selection_text;      // the selection pair's text; `selected_text` (StandbySelectionForeground, the bar's own partner)
    uint32_t score_pane_face;     // the score viewer's pane, the page's mat (= the ink)
    uint32_t scale_trough;        // the progress page's bar's trough and the scrub's (ScaleTrough, our own role, over the ground; arc_dark 2D303B; a Live theme's ScrollbarInnerTrack)
    uint32_t scale_slider_border; // the scrub's knob's one-W rim at rest (ScaleSliderBorder, our own role, over the ground; arc_dark 262933, Arc's slider border; a Live theme's ClipBorder, its well_border)
    // THE THREE CARRIED FILLS (theme_roles.h's CARRIED block; send AA's stage
    // D1, 2026-11-19): each word 0xAARRGGBB, THE TOP BYTE ITS OPACITY (FF
    // opaque; every other member's top byte is 0), painted at it over
    // whatever lies under the box — tone_alpha (render.h) reads it.
    uint32_t tooltip_fill;        // the tooltip's box (TooltipBackground, our own role; arc_dark 4B5162 at F2, Arc's $osd_bg_color at .95; a Live theme's SurfaceBackground, opaque)
    uint32_t card_fill;           // a normal (timed) card's box (CardBackground, our own role; arc_dark 353945 at F2, notifyd's bubble; a Live theme's Alert, opaque)
    uint32_t card_standing_fill;  // a standing (critical) card's box, A BRIGHT TONE (CardStandingBackground, our own role; arc_dark F04A50 at F2, Arc's destructive red; a Live theme's Alert, opaque)
    uint32_t card_ink;            // a standing card's words and class glyph; `card_text` (StandbySelectionForeground, the selection's text, the print on a lit face)
    uint32_t limiter_lamp;        // the title bar's limiter lamp, lit (Alert, a bright tone: Arc's warning orange; the cards' face until send AA's stage D1)
    uint32_t clip_lamp;           // the title bar's clip lamp, lit (StandardVuMeter / Maximum)
    uint32_t waveform_canvas;     // the canvas (DetailGridTiles over DetailViewBackground)
    uint32_t waveform_ink;        // the waveform, the plate's outer bar (WaveformColor over DetailViewBackground)
    uint32_t waveform_core;       // the magnified inner bar's solid fill (SurfaceBackground, = the key face)
    uint32_t grid;                // the canvas's quarter lines (TakeLaneTrackNotHighlighted over the canvas)
    uint32_t center;              // each channel's zero line (= the ink)
    uint32_t cue;                 // THE ACCENT: a lit or pressed key's face, every checked key's, NOISE's progress line, the on-screen keyboard's lit keys, the scrub's played part and its held knob, the flag editor's border (ViewCheckControlEnabledOn)
    uint32_t cue_fill;            // every enabled marker's chip fill, the `h` view's added and changed items' too (= the accent, cue)
    uint32_t cue_outline;         // the selection stem, the lead-in ring (ControlContrastFrame)
    uint32_t inactive_cue;        // a disabled marker's chip fill and inert label (BackgroundClip)
    uint32_t playhead_head;       // the playhead's triangle in the ruler (RangeDefault)
    uint32_t playhead_line;       // the playhead's canvas line (white half way to the head)
    uint32_t scanner;             // the scanner and the zoom anchor's stem (FFFFFF, every theme)
};

// One row of the list (theme_roles.h): the slug the config spells and the
// value list shows, the heading of the group it is listed under (one of the
// three below, printed as it is spelled) and its tones.
struct GuiTheme {
    std::string   slug;
    std::string   group;
    GuiThemeTones tones;
};

// THE GROUPS' HEADINGS, exactly as the value list prints them (architect
// 2026-11-11, session 110, D4; theme_roles.h's THE LIST): the shipped
// themes of `themes/system/` with the reference row, the shipped themes of
// `themes/extra/`, and his own files beside the config.
inline constexpr std::string_view kGuiThemeGroupSystem = "SYSTEM";
inline constexpr std::string_view kGuiThemeGroupExtra  = "EXTRA";
inline constexpr std::string_view kGuiThemeGroupUser   = "USER-DEFINED";

// THE LIST'S OWN SLUGS (theme_roles.h's head). `default` is THE OPENING
// THEME, the first row and the missing-theme fallback: a copy of the
// shipped Arc-Dark (`arc_dark`, kGuiDefaultThemeSourceSlug, a file of
// SYSTEM; architect 2026-11-18, send Y: "arc_dark and 75 will be my
// default" — "let the remote session make that the default"). The launcher
// icon reads the same file (apk/build_apk.sh, which takes the slug from
// this line: keep it one line, `= "<slug>";`).
// `default_dark_neutral_medium` is the compiled reference row below. A
// shipped file slugging to either is a build defect, a user file left out.
inline constexpr std::string_view kGuiDefaultThemeSlug       = "default";
inline constexpr std::string_view kGuiDefaultThemeSourceSlug = "arc_dark";
inline constexpr std::string_view kGuiReferenceThemeSlug     = "default_dark_neutral_medium";

// THE REFERENCE ROW, COMPILED (architect 2026-11-06, session 75; THE
// REFERENCE since 2026-11-11, session 110, D4: `default` a shipped file's
// copy — Arc-Dark's since 2026-11-18, send Y — this row
// staying compiled and listed in SYSTEM as `default_dark_neutral_medium`,
// no file shipping it). Live's "Default Dark
// Neutral Medium" — LIVE'S DEFAULT, whose numbers every "Default N" in the
// code's comments names — through the derivation (theme_roles.h's head),
// the measured palette of 2026-11-01..05 to the byte but THE SELECTION
// PAIR'S TEXT 070707, Default's StandbySelectionForeground (architect
// 2026-11-06, session 75: Live's own partner of the
// StandbySelectionBackground bar; the 111111 of ControlContrastFrame before
// it was our mispairing) AND THE PRINT ON A LIT FACE 070707,
// Default's StandbySelectionForeground, and THE CARDS' WORDS the same
// 070707 (architect 2026-11-17, session 140: Arc prints its selection text
// on its checked blue, D4 its warning text on the cards) AND THE RULER
// 393939, the canvas's tone (architect 2026-11-17, session 140, ruling 3)
// AND THE LIT-BUT-INERT PAIRS ON ARC'S OWN NUMBERS (architect 2026-11-19,
// send Z: "let's do arcs numbers"; theme_roles.h's DERIVED block): a
// checked-but-disabled key's face 86623B, (the accent FFAD56 at 75 % over
// the body tone 242424, C88B4A) at 60 % over the body tone, and its glyph
// 2A221A, (the print 070707 at 80 % over C88B4A) at 60 % over the body tone,
// dimmed with the button as GTK's opacity dims it AND THE THREE ROLES OF
// OUR OWN, each Default's own existing number (architect 2026-11-19, send
// Z): THE BODY TONE 242424 (SurfaceArea), THE TITLE BAR'S TEXT and THE
// SIDEBAR TEXT B5B5B5 (ControlForeground) AND THE ARC PAGE'S DERIVED THREE:
// THE RULER'S DIGITS 9C9C9C, the text at 80 % over the ruler 393939; THE
// DISABLED KEY'S FACE 2E2E2E, the key face 363636 at 55 % over the body
// tone, and ITS BORDER 272727, the well's border 2A2A2A at 55 % over it AND
// THE PLAYHEAD'S
// LINE 81E1EA, white half way to the head (architect 2026-11-12, session
// 113) AND THE ENABLED CHIP'S FILL FFAD56, the accent
// (ViewCheckControlEnabledOn; architect 2026-11-13, session 119) AND THE
// TRIM BAR'S TROUGH 242424, Default's ScrollbarInnerTrack 00000000 over the
// ground — Live's transparent track, so the ground itself — and THE WELL'S
// FRAME 2A2A2A, Default's ClipBorder (architect 2026-11-17, session 140)
// AND THE THREE CARRIED FILLS, each opaque at the tone it replaced (send
// AA's stage D1, 2026-11-19): the tooltip's 363636 (SurfaceBackground), a
// normal card's and a standing card's E76942 (Alert), the limiter lamp
// keeping Alert's E76942 as its own tone AND THE TWO HOVER TONES, each the
// tone it moves from (send CC, 2026-11-20): a key's hover face 363636
// (SurfaceBackground) and the pill's 696969 (ScrollbarInnerHandle), so the
// reference row shows no hover AND THE LIST ROW'S HOVER 393939, the view's
// own tone (a theme whose key hover face 363636 is its key face 363636
// hovers nothing, so not Arc's 3 % white but the ruler 393939; architect
// 2026-11-21), so no list row hovers either.
// THE ROW IS LIVE'S OWN NUMBERS, THE RECORD (architect 2026-11-13, session
// 119): like every row of the list it holds the theme's own numbers,
// and the install mixes its bright tones at the `bright_mix` key as it mixes
// every row's (theme_roles.h's THE BRIGHT MIX), so no number here is the
// key's. palette_file.h's static_asserts pin every word. The pre-install
// palette's row too
// (render.cpp's construction state, never painted).
inline constexpr GuiThemeTones kGuiReferenceThemeTones = {
    0x242424, // ground
    0x242424, // body_face
    0xB5B5B5, // text
    0xB5B5B5, // title_text
    0xB5B5B5, // sidebar_text
    0x757575, // text_dim
    0x070707, // text_on_lit
    0x5D5D5D, // key_dim_ink
    0x2E2E2E, // key_dim_face
    0x272727, // key_dim_border
    0x2A221A, // text_on_lit_dim
    0x363636, // key_face
    0x363636, // key_hover_face
    0x393939, // list_hover_face
    0x86623B, // lit_dim_fill
    0x393939, // ruler_face
    0x919191, // ruler_tick
    0x9C9C9C, // ruler_text
    0x2A2A2A, // lane_face
    0x242424, // trim_bar_face
    0x696969, // trim_span_face
    0x696969, // trim_span_hover_face
    0x2A2A2A, // well_border
    0x637E86, // selection_fill
    0x070707, // selection_text
    0x191919, // score_pane_face
    0x242424, // scale_trough
    0x2A2A2A, // scale_slider_border
    0xFF363636, // tooltip_fill
    0xFFE76942, // card_fill
    0xFFE76942, // card_standing_fill
    0x070707, // card_ink
    0xE76942, // limiter_lamp
    0xFF0A0A, // clip_lamp
    0x393939, // waveform_canvas
    0x191919, // waveform_ink
    0x363636, // waveform_core
    0x303030, // grid
    0x191919, // center
    0xFFAD56, // cue
    0xFFAD56, // cue_fill
    0x111111, // cue_outline
    0x5D5D5D, // inactive_cue
    0x03C3D5, // playhead_head
    0x81E1EA, // playhead_line
    0xFFFFFF, // scanner
};

// THE SLUG RULE (architect 2026-11-05; unchanged 2026-11-06): a file's stem
// lowercased, every run of characters outside [a-z0-9] one `_`, a leading or
// trailing `_` stripped — "Twenty-Four Carat" -> `twenty_four_carat`, "SB Red
// Greys" -> `sb_red_greys`. The bytes are judged one by one, ASCII alone.
inline std::string theme_slug_of_stem(std::string_view stem) {
    std::string out;
    bool pending_sep = false;
    for (const char raw : stem) {
        char c = raw;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        const bool word = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!word) { pending_sep = true; continue; }
        if (pending_sep && !out.empty()) out.push_back('_');
        pending_sep = false;
        out.push_back(c);
    }
    return out;
}

// THE SLUG'S SHAPE — exactly the words the slug rule spells: one or more runs
// of [a-z0-9] joined by single `_`s. THE ONE GRAMMAR OWNER of a theme's
// spelling, read by the device config's reader (its `theme` line, a shape
// and nothing more since 2026-11-06 — a slug no row bears loads and opens in
// `default`, theme_roles.h's head).
constexpr bool is_theme_slug(std::string_view s) {
    if (s.empty() || s.front() == '_' || s.back() == '_') return false;
    char prev = 0;
    for (const char c : s) {
        const bool word = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!word && c != '_') return false;
        if (c == '_' && prev == '_') return false;
        prev = c;
    }
    return true;
}
static_assert(is_theme_slug(kGuiDefaultThemeSlug));
static_assert(is_theme_slug(kGuiDefaultThemeSourceSlug));
static_assert(is_theme_slug(kGuiReferenceThemeSlug));
