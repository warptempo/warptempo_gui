#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// THE THEME'S SHAPE (architect 2026-11-05, the theme arc: "i'd like to
// implement all" — Ableton Live 12's own themes selectable whole, chrome and
// well together, through the one `theme` device key; architect 2026-11-06,
// session 75: THE APP READS LIVE'S .ASK FILES ITSELF). A theme is one row of
// sRGB words, GuiThemeTones, under a slug the config spells and a group the
// Settings page's value list heads it with. THE LIST — the compiled group
// first, then the .ask files dropped into `themes/<group>/` beside the device
// config, each through THE DERIVATION — is theme_ask.h's, whose head owns the
// walk, the order, the left-out files and the derivation. This header holds
// what needs no file: the row's shape, the compiled `default` row, the
// compiled group's name and THE SLUG RULE.

// Every sRGB word the GUI paints, one theme's (0xRRGGBB). Beside each member
// what reads it and the Live role it comes from (the one owners: materials.h's
// head, palette_file.h's role list, render.h's palette block; the derivation,
// theme_ask.h's head).
struct GuiThemeTones {
    uint32_t ground;              // the flat ground everywhere outside the well and the keys, the panel's `face` too (SurfaceArea)
    uint32_t text;                // every word, digit and drawing the chrome prints; `label` / `field_text` (ControlForeground)
    uint32_t text_dim;            // the inert rows and the tooltip's hint line (TextDisabled)
    uint32_t text_on_lit;         // what is printed on a lit key's face, its glyph and the on-screen keyboard's lit cap (ControlContrastFrame); a word alone, read by key_ink (materials.h)
    uint32_t key_dim_ink;         // a disabled key's glyph and legend (ViewCheckControlDisabledOff over the face)
    uint32_t text_on_lit_dim;     // a checked-but-disabled key's glyph on its dimmed face (text_on_lit by the dim scalar, theme_ask.h's DERIVED block: · mean(key_dim_ink) / mean(text) on a dark theme, toward white on a light one); a word alone, read by key_ink
    uint32_t key_face;            // an unchecked key at rest; the time fields (SurfaceBackground)
    uint32_t key_press_face;      // an unchecked key's face while a press stands on it (Desktop)
    uint32_t lit_press_fill;      // a checked key's face (not a radio key's) while a press stands on it: the accent darkened (cue · mean(key_press_face) / mean(key_face))
    uint32_t lit_dim_fill;        // a checked-but-disabled key's face (not a radio key's): the accent dimmed (cue by the dim scalar, as text_on_lit_dim)
    uint32_t radio_lit_fill;      // a checked RADIO key's whole face, the purple (ScaleAwareness); materials.h's THE RADIO KEYS
    uint32_t radio_lit_press_fill; // a checked radio key's face while a press stands on it (radio_lit_fill · mean(key_press_face) / mean(key_face))
    uint32_t radio_lit_dim_fill;  // a checked-but-disabled radio key's face (radio_lit_fill by the dim scalar, as text_on_lit_dim)
    uint32_t well_frame;          // the scrub's channel and the tooltip's border (TextDisabled)
    uint32_t ruler_face;          // the ruler (SurfaceBackground)
    uint32_t ruler_tick;          // the ruler's ticks (DetailViewRulerMarkings over the face)
    uint32_t lane_face;           // the marker lane (Desktop)
    uint32_t trim_bar_face;       // the trim bar outside the trim (Desktop)
    uint32_t trim_span_face;      // the trim bar inside the trim (ScrollbarInnerHandle over Desktop)
    uint32_t selection_fill;      // the selection pair's bar: the lists' lit row, the editors' selected substring; `selected_fill` (StandbySelectionBackground)
    uint32_t selection_text;      // the selection pair's text; `selected_text` (StandbySelectionForeground, the bar's own partner)
    uint32_t list_press_fill;     // a list row's fill while a press stands on it (SelectionBackground)
    uint32_t list_press_text;     // a list row's text and glyph under that press (SelectionForeground)
    uint32_t pane_face;           // the modal family's box interior and the tooltip's face (SurfaceBackground)
    uint32_t score_pane_face;     // the score viewer's pane, the page's mat (= the ink)
    uint32_t card_face;           // the cards' box and their ring; `card_ground` / `card_frame` (Alert)
    uint32_t card_ink;            // the cards' words and class glyph; `card_text` (Desktop)
    uint32_t clip_lamp;           // the logo lane's clip lamp, lit (StandardVuMeter / Maximum); the limiter lamp lit wears card_face
    uint32_t level_fill;          // the NOISE level case's fill (RangeEditField over the key face), read by paint_level_case
    uint32_t waveform_canvas;     // the canvas (DetailGridTiles over DetailViewBackground)
    uint32_t waveform_ink;        // the waveform, the plate's outer bar (WaveformColor over DetailViewBackground)
    uint32_t waveform_core;       // the magnified inner bar's solid fill (SurfaceBackground, = the key face)
    uint32_t grid;                // the canvas's quarter lines (TakeLaneTrackNotHighlighted over the canvas)
    uint32_t center;              // each channel's zero line (= the ink)
    uint32_t cue;                 // THE ACCENT: every enabled marker's chip and a checked key's whole face but a radio key's, the on-screen keyboard's lit keys too (ViewCheckControlEnabledOn)
    uint32_t cue_outline;         // the chips' outline, the selection stem, the rings, the ruler head's outline (ControlContrastFrame)
    uint32_t inactive_cue;        // a disabled marker's chip fill and inert label (BackgroundClip)
    uint32_t playhead_head;       // the playhead's triangle in the ruler (RangeDefault)
    uint32_t playhead_line;       // the playhead's canvas line (white + 0.483 toward the head)
    uint32_t scanner;             // the scanner and the zoom anchor's stem (FFFFFF, every theme)
};

// One row of the list (theme_ask.h): the slug the config spells and the value
// list shows, the group it is listed under — the compiled group's name or its
// folder's, lowercase as the folder spells it (the value list prints it ALL
// CAPS) — and its tones.
struct GuiTheme {
    std::string   slug;
    std::string   group;
    GuiThemeTones tones;
};

// THE COMPILED GROUP (architect 2026-11-06, session 75): listed first, under
// the heading WARPTEMPO — `default`, compiled, and `immaterial_mod`, a
// compiled rule over a dropped Immaterial.ask (theme_ask.h's head). Its two
// slugs are reserved: a dropped file slugging to either is left out.
inline constexpr std::string_view kGuiCompiledThemeGroup = "warptempo";
inline constexpr std::string_view kGuiDefaultThemeSlug   = "default";
inline constexpr std::string_view kGuiImmaterialModSlug  = "immaterial_mod";

// THE DEFAULT ROW, COMPILED (architect 2026-11-06, session 75: `default`
// stays compiled — the no-folder launch, the missing-theme fallback, the
// launcher icon's numbers, palette_file.h's pinned row; Default's .ask is
// not read). Live's "Default Dark Neutral Medium" through the derivation
// (theme_ask.h's head), the measured palette of 2026-11-01..05 to the byte
// but THE SELECTION PAIR'S TEXT 070707, Default's StandbySelectionForeground
// (architect 2026-11-06, session 75: Live's own partner of the
// StandbySelectionBackground bar; the 111111 of ControlContrastFrame before
// it was our mispairing) AND THE LEVEL CASE'S FILL 007383 (RangeEditField; architect 2026-11-11,
// session 105) AND THE LIT-BUT-INERT PAIR (architect 2026-11-11, session
// 106; theme_ask.h's DERIVED block): a checked-but-disabled key's face
// 83592C and its glyph 090909, the accent FFAD56 and the print on it
// 111111 × 93 / 181 AND THE RADIO KEYS' THREE (architect 2026-11-11,
// session 107; theme_ask.h's head): a checked radio key's face B595FC,
// Default's ScaleAwareness, its press 8D74C4 (× 42 / 54) and its inert face
// 5D4D81 (× 93 / 181). palette_file.h's static_asserts pin every word.
inline constexpr GuiThemeTones kGuiDefaultThemeTones = {
    0x242424, // ground
    0xB5B5B5, // text
    0x757575, // text_dim
    0x111111, // text_on_lit
    0x5D5D5D, // key_dim_ink
    0x090909, // text_on_lit_dim
    0x363636, // key_face
    0x2A2A2A, // key_press_face
    0xC68743, // lit_press_fill
    0x83592C, // lit_dim_fill
    0xB595FC, // radio_lit_fill
    0x8D74C4, // radio_lit_press_fill
    0x5D4D81, // radio_lit_dim_fill
    0x757575, // well_frame
    0x363636, // ruler_face
    0x919191, // ruler_tick
    0x2A2A2A, // lane_face
    0x2A2A2A, // trim_bar_face
    0x696969, // trim_span_face
    0x637E86, // selection_fill
    0x070707, // selection_text
    0xB0DDEB, // list_press_fill
    0x070707, // list_press_text
    0x363636, // pane_face
    0x191919, // score_pane_face
    0xE76942, // card_face
    0x2A2A2A, // card_ink
    0xFF0A0A, // clip_lamp
    0x007383, // level_fill
    0x393939, // waveform_canvas
    0x191919, // waveform_ink
    0x363636, // waveform_core
    0x303030, // grid
    0x191919, // center
    0xFFAD56, // cue
    0x111111, // cue_outline
    0x5D5D5D, // inactive_cue
    0x03C3D5, // playhead_head
    0x85E2EB, // playhead_line
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
// spelling and of a group folder's name (theme_ask.h's walk: `extra`,
// `sonic_bloom`), read by the device config's reader (its `theme` line, a
// shape and nothing more since 2026-11-06 — a slug no row bears loads and
// opens in default, theme_ask.h's head) and by the walk.
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
static_assert(is_theme_slug(kGuiImmaterialModSlug));
static_assert(is_theme_slug(kGuiCompiledThemeGroup));
