#pragma once

#include <cstdint>
#include <string_view>

// THE THEME'S SHAPE (architect 2026-11-05, the theme arc; ONE THEME since
// 2026-11-21, send GG: "i am committed" — one look, Arc-Dark at 9 pt, every
// other theme removed). The theme is one row of sRGB words, GuiThemeTones,
// read at launch from the one shipped theme file, arc_dark's, through THE
// DERIVATION (theme_roles.h's head owns the format and the derivation).
// This header holds the row's shape and the file's slug.

// Every sRGB word the GUI paints (0xRRGGBB). Beside each member what reads
// it and the role it comes from (the one owners: materials.h's head,
// palette_file.h's role list, render.h's palette block; the derivation,
// theme_roles.h's head). The installed row is the derivation's row as it is
// (install_palette, render.h).
struct GuiThemeTones {
    uint32_t ground;              // the flat ground everywhere outside the well and the keys (SurfaceArea); THE TITLE TONE on the Arc page and the launcher icon's ground
    uint32_t body_face;           // THE BODY TONE, the ground of everything under the title bar (WindowBackground, our own role; a Live theme's SurfaceArea): the palette's `face`, both icon rows, the status bar, the erase, the modal pane's and the keyboard's ground
    uint32_t text;                // every word, digit and drawing the chrome prints; `label` / `field_text` (ControlForeground)
    uint32_t title_text;          // the title bar's text and drawings (TitleForeground, our own role; a Live theme's ControlForeground; paint_title_bar)
    uint32_t sidebar_text;        // the live marker labels' ink, the tooltip's words and a normal card's words and glyph (SidebarForeground, our own role; a Live theme's ControlForeground)
    uint32_t text_dim;            // the inert rows and the tooltip's hint line (TextDisabled)
    uint32_t inactive_cue_label_hover; // a disabled marker's label ink under the hovering pen, its rest `text_dim` (CueDisabledLabelHover, our own role; arc_dark 9DA3AD, Arc's dim button under its row's hover, `.gedit-document-panel row:hover:not(:selected) button`; a Live theme's TextDisabled, the label's own tone)
    uint32_t text_on_lit;         // what is printed on a lit key's face, its glyph and the on-screen keyboard's lit cap (StandbySelectionForeground, the selection's text); a word alone, read by key_ink (materials.h)
    uint32_t key_dim_ink;         // a disabled key's glyph (ViewCheckControlDisabledOff over the face)
    uint32_t key_dim_face;        // a disabled key's and word button's face (the key face at 55 % over the body tone, theme_roles.h's DERIVED block)
    uint32_t key_dim_border;      // a disabled key's border on its stretch of the ring (well_border at 55 % over the body tone, the same block)
    uint32_t text_on_lit_dim;     // a checked-but-disabled key's glyph on its dimmed face ((text_on_lit at 80 % over the accent at 75 % over the body tone) at 60 % over the body tone, theme_roles.h's DERIVED block); a word alone, read by key_ink
    uint32_t key_face;            // a key's and a word button's face at rest, the scrub's knob's fill (SurfaceBackground)
    uint32_t key_hover_face;      // that face under the hovering pen, the hover mix's far end: a key's, a word button's, an on-screen keyboard key's, the scrub's knob's fill (ButtonHoverBackground, our own role; arc_dark 505666, Arc's button:hover; a Live theme's SurfaceBackground, the key face itself)
    uint32_t list_hover_face;     // a list row under the hovering pen: ARC'S ROW HOVER, rgba(255, 255, 255, 0.03) over the list's view (ruler_face), c + 0.03 · (255 − c) rounded, the stylesheet's own number computed exactly (theme_roles.h's DERIVED block; arc_dark 404552 -> 464B57)
    uint32_t lit_dim_fill;        // a checked-but-disabled key's face: (the accent at 75 % over the body tone) at 60 % over the body tone, Arc's numbers
    uint32_t ruler_face;          // the ruler, Arc's entry's fill and the flag editor's (= the canvas, waveform_canvas: DetailGridTiles over DetailViewBackground)
    uint32_t ruler_tick;          // the ruler's ticks (DetailViewRulerMarkings over the ruler face, the canvas)
    uint32_t ruler_text;          // the ruler's digits (the text at 55 % over the ruler face, Arc's `.dim-label`, theme_roles.h's DERIVED block; paint_ruler_row)
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
    uint32_t card_standing_fill;  // a standing (critical) card's box (CardStandingBackground, our own role; arc_dark F04A50 at F2, Arc's destructive red; a Live theme's Alert, opaque)
    uint32_t card_ink;            // a standing card's words and class glyph; `card_text` (StandbySelectionForeground, the selection's text, the print on a lit face)
    uint32_t limiter_lamp;        // the title bar's limiter lamp, lit (Alert: Arc's warning orange; the cards' face until send AA's stage D1)
    uint32_t clip_lamp;           // the title bar's clip lamp, lit (StandardVuMeter / Maximum)
    uint32_t waveform_canvas;     // the canvas (DetailGridTiles over DetailViewBackground)
    uint32_t waveform_ink;        // the waveform, the plate's outer bar (WaveformColor over DetailViewBackground)
    uint32_t waveform_core;       // the magnified inner bar's solid fill (SurfaceBackground, = the key face)
    uint32_t grid;                // the canvas's quarter lines (TakeLaneTrackNotHighlighted over the canvas)
    uint32_t center;              // each channel's zero line (= the ink)
    uint32_t cue;                 // THE ACCENT: a lit or pressed key's face, every checked key's, NOISE's progress line, the on-screen keyboard's lit keys, the scrub's played part and its held knob, the flag editor's border (ViewCheckControlEnabledOn)
    uint32_t cue_fill;            // every enabled marker's chip fill, the `h` view's added and changed items' too (= the accent, cue)
    uint32_t cue_hover;           // an enabled marker's chip under the hovering pen (CueHover, our own role; arc_dark 639FE5, Arc's hover of a blue thing, `.documents-entry-tag:hover`; a Live theme's ViewCheckControlEnabledOn, the chip's own tone)
    uint32_t cue_outline;         // the selection stem, the lead-in ring (ControlContrastFrame)
    uint32_t inactive_cue;        // a disabled marker's chip fill (BackgroundClip)
    uint32_t inactive_cue_hover;  // a disabled marker's chip under the hovering pen (CueDisabledHover, our own role; arc_dark 90939B, Arc's `wm_icon_bg`, the titlebar glyph awake; a Live theme's BackgroundClip, the chip's own tone)
    uint32_t playhead_head;       // the playhead's triangle in the ruler (RangeDefault)
    uint32_t playhead_line;       // the playhead's canvas line, its stem (PlayheadLine, our own role; arc_dark A5ABB5, Arc's `.h4` / `.category-label`; a Live theme's white half way to its RangeDefault)
    uint32_t hover_line;          // the waveform's hover line and its triangle in the ruler (HoverLine, our own role, over the canvas; arc_dark 767B87, Arc's scrollbar slider; a Live theme's ScrollbarInnerHandle)
    uint32_t scanner;             // the scanner and the zoom anchor's stem (FFFFFF, every theme)
};

// THE ONE THEME'S FILE (architect 2026-11-21, send GG: one look): the
// shipped `themes/system/<slug>.theme`, Arc-Dark's (the default since
// 2026-11-18, send Y; the one theme since 2026-11-21), which the launch
// reads (GuiPlatform::bundled_theme_file, gui_main) and the launcher icon's
// two colors are read from (apk/build_apk.sh, which takes the slug from
// this line: keep it one line, `= "<slug>";`).
inline constexpr std::string_view kGuiDefaultThemeSourceSlug = "arc_dark";
