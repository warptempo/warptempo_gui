#pragma once
#include "warpmarkers.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment for target-view waveform
#include "waveform_gain.h"    // WaveformGainCurve, the waveform picture's gain
#include "gui_font.h"         // GuiFont, the face owner's face at a scale
#include "chrome_spec.h"      // live_chrome_spec(), the live chrome's lengths
#include "program_spec.h"     // kProgramSpec, the program's lengths (the band, row 8)
#include "display_transform.h" // display_color's transform (sRGB -> the window's space)
#include "gui_theme.h"        // GuiTheme, the installed theme's row (install_palette)

#include <algorithm>
#include <array>
#include <cairo/cairo.h>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class GuiAudio;
class GuiCanvasSink;   // the canvas interior's one paint road (canvas_sink.h)
struct AppState;
struct DragOverlay;
namespace text_shape { struct ShapedRun; }

struct GuiRect {
    int x;
    int y;
    int w;
    int h;
    // Memberwise, for the as-painted fingerprints that carry a lane's rect
    // (GuiUpperRowsFingerprint and GuiClockFingerprint, app_state.h).
    bool operator==(const GuiRect&) const = default;
};

// Point-in-rect on the tree's ONE containment convention: half-open on both
// axes (>= x, < x + w), so adjacent rects tile with no shared column and a
// zero-width/height rect contains nothing (the cold-stash case). Every plain
// x/y hit test spells itself through this owner; deciders with a fused extra
// condition (an x-only band test, a double-domain rect) keep their own compare
// at the site.
inline bool rect_contains(const GuiRect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

struct GuiColor {
    double r;
    double g;
    double b;
};

// Build a GuiColor from a 0xRRGGBB word, converting each 8-bit channel to an
// exact [0,1] double — the one road from a byte triple (a theme role's, a
// named literal's) to a colour. RGB only: the
// palette composites nothing (its head, below), and the renderer hands colours
// to cairo through set_palette_source and set_waveform_source.
inline constexpr GuiColor hex(uint32_t rgb) {
    return GuiColor{
        static_cast<double>((rgb >> 16) & 0xFF) / 255.0,
        static_cast<double>((rgb >>  8) & 0xFF) / 255.0,
        static_cast<double>( rgb        & 0xFF) / 255.0,
    };
}

// An authored (sRGB) color as the window takes it — THE PAINTER'S ENTRY's
// one GuiColor road (the rule, the math and every site that runs it are at
// display_transform.h's head, architect 2026-10-08): on the tablet's
// Display-P3 window the byte triple converted to the P3 triple that presents
// the same color, elsewhere the color itself. Every authored color is a
// byte triple, so each channel is read as its byte (std::nearbyint).
inline GuiColor display_color(GuiColor srgb) {
    if (!display_transform::active()) return srgb;
    const auto byte = [](double u) {
        return static_cast<uint32_t>(
            std::clamp(std::nearbyint(u * 255.0), 0.0, 255.0));
    };
    return hex(display_transform::p3_word_from_srgb(
        (byte(srgb.r) << 16) | (byte(srgb.g) << 8) | byte(srgb.b)));
}

// Trim boundaries in domain-frame samples (source-frame in source view,
// target-frame in target view). Trim no longer dims any renderer — it is
// consumed by render_trim_flags to place the view bar's span. Values
// are the AUTHORED positions mapped into the displayed domain by the live
// trim pass (GuiPaintHandler::paint_trim, through displayed_trim_ms):
// per-bound, unordered (bounds may be inverted mid-gesture — crossed cannot
// rest — and this paints per frame; past-EOF is load-fatal, so each bound is
// within [0, EOF]); each stem is placed independently, so no order is
// assumed here.
struct TrimRange {
    int64_t begin;
    int64_t end;
};

// -- Palette ---------------------------------------------------------------
//
// THE GUI'S COLORS ARE ROLES AND NOTHING ELSE (architect 2026-10-04; the
// split 2026-10-07; the chrome's inheritance 2026-10-10; the hi-fi panel
// skin 2026-10-23): THE PALETTE holds every color the PROGRAM draws in the
// well or on a thing that enters it (the waveform's canvas, ink and core,
// the canvas's grid and center line, the cues' one color and the disabled
// cue's grey, the playhead and the scanner; palette_file.h's head owns the
// rule that parts program from chrome) — ONE PALETTE PER THEME, LIVE'S OWN
// NUMBERS (architect 2026-11-01: the Cool Edit presets and their `waveform`
// key dropped, the canvas grammar kept, Live's measured default compiled
// in; architect 2026-11-05, the theme arc: Live 12's own themes, whole, the
// `theme` device key naming one row of the themes' list — since 2026-11-06,
// session 75, the compiled group and the .ask files read at launch,
// theme_ask.h — the compiled Default row that measured palette to the byte
// but the selection pair's text, gui_theme.h); A
// LIT KEY has no fill of its own — its glyph wears the accent, the cue's
// tone, on the key's two faces (architect 2026-11-11, materials.h's THE
// KEY); THE
// PANEL'S FACE is the flat ground (the theme's `ground`, Default's 242424,
// the flat look, architect 2026-11-01), and THE CHROME'S ROLES are the
// theme's tones, one per role (theme_file.h's role table, the one owner;
// the flat look's constants 2026-11-01..05; Cool Edit's tones off the Face
// 2026-10-10 to 2026-10-23). The chrome's roles and their order are the
// role table (kGuiThemeRoles, theme_file.h). THE APP PAINTS NO CAPTION
// since 2026-10-24 (THE CAPTION's note, below): its six roles, the
// `caption` device key and its 42 built-in schemes left 2026-10-21, its
// flat skin constants with the caption itself.
// The palette's roles and their tones are kGuiPaletteRoles (palette_file.h).
// There is NO LEVEL (architect 2026-10-04): a dark look, like
// any look made official, would be a chrome variant of its own
// (2026-10-08) — and is, since 2026-11-05, a theme of its own. The app
// computes no color at paint time: every one is a theme row's word, the
// derived tones computed once at the launch's read (theme_ask.h's head, THE
// DERIVATION).
// EVERY COLOUR A PAINTER HANDS CAIRO is one of GuiPalette's fields through the
// one accessor palette() — the chrome's flat-look tones among them
// (materials.h, 2026-11-01: the ground, the key's two faces (rest and
// press), the
// text tones, the well's tones — the trim bar, its frame, the ruler, its
// ticks, the marker lane — the panes and the selection pair; members of the
// struct since 2026-11-05) — or
// THE BREEZE GLYPHS (architect 2026-11-01, M7: every drawing a Breeze glyph):
// each file is recolored to `currentColor` and painted in THE TONE the
// caller passes as the installed theme's sRGB word (installed_theme():
// `text`, `key_dim_ink` on a disabled key, the accent `cue` on a lit key
// and the dimmed accent `lit_dim_ink` on a lit disabled one, materials.h's
// key_ink; icons.h's THE BOUND DRAWING) — the one place an antialiased
// picture is copied whole.
// TEXT OVER A FILL IS THE FILL'S OWN PAIR — a text role beside every ground
// role: the text tone over the ground, the field, the keys' two faces and the
// selection's grey; a lit key's glyph, and the on-screen keyboard's lit cap,
// the accent on those same faces (architect 2026-11-11).
// STILL OPAQUE, STILL NO PAINT-TIME COMPOSITING, NO ROUNDED CORNERS, NO
// HOVER FACE ON ANY BUTTON: the keys have none and the chrome's buttons
// never had one; the lists' lit rows are a selection, not a button face.
// Every colour is a solid fill of integer cells — EXCEPT, on the canvas,
// THE WAVEFORM PLATE'S EDGE (antialiased and blended on the GPU since
// 2026-10-30, render_waveform_columns' head). (The color
// picker's hue ring and SV triangle, 2026-10-07..14, were the one place the
// product painted colors that were not roles; THE PROGRAM CASE'S SHADOW
// RAMP, 2026-10-09 to 2026-10-23, and THE CAPTION'S GRADIENT, 2026-10-05 to
// 2026-10-21, were the other exceptions.)
// A DITHER'S CELL IS ONE DEVICE PX (architect 2026-10-06, the whole chrome
// made scalable: "all colours used are period-authentic; dithering gets
// translated into whatever it perceptually becomes"): a dither stays a
// dither — no blended colour is ever computed in its place — but its cell is
// the panel's own pixel, the period technique at native resolution, so at
// 400 % the pattern is four times finer than a 96-dpi screen's and the eye
// blends it as it blended the original. NO DITHER IS DRAWN TODAY: the last,
// Windows' checked face, went 2026-10-22 when its last wearers became the
// flat keys (the popup lists' scroll track 2026-10-14, the trim track's
// checker with the view bar 2026-10-09). THIS PARAGRAPH IS THE RULE'S ONE
// STATEMENT; every other chrome length stays in Windows px (scaled_px).
//
// A HEX HERE IS AN sRGB BYTE TRIPLE, AS EVERY SCREENSHOT RECORDS IT
// (architect 2026-10-08 ~05:15: "if I open that image in the web
// browser, it would have the identical color on the GUI") — a palette role, a
// chrome role, a catalog theme's byte. The tablet's window
// is a Display-P3 layer (GuiPlatform::adopt_window, platform_android.cpp), so
// THE TABLET CONVERTS sRGB -> DISPLAY-P3 AT THE PAINTER'S ENTRY
// (display_color; the math and every converting site at
// display_transform.h's head) and the panel presents the color the browser
// presents.
// From 2026-10-02 until this ruling the bytes went to the P3 layer
// unconverted, a hex then being a P3 triple.
//
// NO RELIEF AND NO RING (the flat look, architect 2026-11-01; the overlays'
// part 2026-11-02): no edge in the product is raised, sunken or two-toned —
// Windows 95's DrawEdge grammar, its one-device-px mitred hairline ring of
// 2026-10-23 and the ring tones it wore are git history. THE CHROME'S LINES
// are flat ONE-W lines (hairline_px, below, the line rule of 2026-11-04) of ONE TONE,
// materials.h's `well_frame` (Default's 757575): the scrub's channel (the scrub block)
// and the tooltip's border (THE CARD FACE, below; the cards' ring is painted
// in their own fill, so no border shows); the modal
// family's box has none (modal_box.h's head).
//
// THE MAPPING (architect 2026-10-03; the roles 2026-10-04), role -> what it
// paints:
//   ground        every chrome surface: the chrome's lanes
//                 under the band, the on-screen
//                 keyboard's band (inside the program frame since 2026-10-14;
//                 the dialog's taskbar ground beside it 2026-10-10 to
//                 2026-10-14) — every chrome button face (the band, row 8, the canvas
//                 column's lanes and the modal family's panel margins are the
//                 program's panel since 2026-10-09, below);
//   label         chrome text and glyphs, a prompt's question and the
//                 progress modal's sentence in the modal family's box;
//   selected pair the folder
//                 overlay's and the picker's highlighted row (its name; the
//                 glyph keeps its own inks, icons.h), the selection
//                 band and selected substring of every text field, the flag
//                 editor's included — never a selected cue's resting label,
//                 which paints as at rest (architect 2026-11-04: the
//                 selection shows on the chip; THE MARKER LANE, below)
//                 — ONE PAIR, FOCUSED OR NOT (the 2026-09-02
//                 inactive selection face retired, architect 2026-10-03, on
//                 Windows 95's Network Neighborhood, where a selection keeps
//                 Hilight / HilightText in a window that has lost the focus);
//   field_text    the dialog editors' line on the modal family's pane, the
//                 caret its text (architect 2026-10-03); THE LIST — the
//                 folder overlay's resting names on the pane (architect
//                 2026-10-06, paint_folder_overlay) — and THE FLAG EDITOR'S
//                 text and caret (architect 2026-10-09; EDITING, below);
//   card trio     every notification card (THE CARD FACE, below; the
//                 tooltip reads no role, its tones the flat look's
//                 constants);
//   (THE CAPTION and THE WINDOW BORDER went 2026-10-24, the
//                 app painting no window chrome — the caption's six roles,
//                 a scheme's start, gradient end and text, active and
//                 inactive, 2026-10-05..10-21, then the skin's flat
//                 constants and the border's ring-light line until then);
//   THE PALETTE   the program's ELEVEN (palette_file.h, 2026-10-07; one
//                 palette, Live's numbers, since 2026-11-01): the
//                 waveform's canvas, ink and core (ROW 6, below; the view
//                 bar's span wears none of them, materials.h's neutral trio
//                 since 2026-10-25); THE CANVAS'S TWO LINES, Cool Edit's
//                 grammar — `grid` under the waveform and `center` on each
//                 channel's zero, over it (ROW 6); THE MARKER'S CHIP
//                 (architect 2026-11-04; 2026-11-05) — `cue` every
//                 enabled fill, `cue_outline` its one-W outline, the
//                 selected marker's stem and the two rings — warp and
//                 phase reset alike (the strike is the invalid mark, THE
//                 MARKER LANE below); the disabled cue's `inactive_cue`,
//                 its chip's fill and the inert label; the playhead's two
//                 roles,
//                 `playhead_head` — its triangle in the ruler — and
//                 `playhead_line` — its static tone in the canvas, Default's
//                 42D2E0, white 0.75 toward the head (architect 2026-11-04
//                 night, the static line; 2026-11-10, the quarter; THE
//                 PLAYHEAD, below); and the
//                 `scanner` — the playback line AND THE ZOOM ANCHOR'S STEM,
//                 the white of the stems that belong to the controls
//                 (architect 2026-10-09). THE PANEL'S `face` (2026-10-09:
//                 the program's band, row 8 and the canvas column's lanes)
//                 is the flat ground (the theme's `ground` (Default's 242424, materials.h)), no palette
//                 role.
// THE CARD FACE — ONE BOX PAINTER, TWO LOOKS (paint_popup_chrome): a flat box
// INSIDE A ONE-W BORDER on all four sides (hairline_px, the line rule,
// 2026-11-04), the border standing on the box's own outer rows and columns,
// so the card is 56 rows and the one-line tooltip 44 on the tablet (200 %); NO RING
// TONES, NO SHADOW. THE TOOLTIP (architect 2026-10-04, the period's tooltip;
// flat since 2026-11-02, M11 with M14 "d"): the pane, materials.h's
// `pane_face` (Default's 363636), in `well_frame` (Default's 757575), the
// grammar's one frame tone — the flat look's tones, read from palette()
// like every role; ITS NAME LINE the flags' own
// program face (2026-10-23, "the same as the flags", Program 11 W, 22 device
// px at 200 %) in the text tone `text`, ITS HINT LINE the hint face (Hint
// 9 W, 18 device px) in the dim tone
// `text_dim` (architect 2026-10-31; Live's 757575 since M11, 2026-11-02,
// paint_shift_tooltip). EVERY NOTIFICATION CARD, both classes alike
// (architect 2026-11-04 night, his pick h1 of tmp/s38 over h0, the
// tooltip's look they wore from 2026-10-06, and h2, 111111 ink): a box in
// LIVE'S STATUS BAR'S ORANGE, `card_ground` = `card_face` (Default's E76942), its border
// `card_frame` the same orange, so NO BORDER SHOWS (the ring is layout),
// its words at the body size and its class glyph in `card_text` = `card_ink`
// 2A2A2A, Live's own text on that bar — both measured on his Live 12 shot of
// 2026-10-31 (materials.h's THE CARDS). THIS PARAGRAPH IS THE RULE'S ONE
// STATEMENT.
// THE CANVAS HAS NO FRAME since the well's flat look (architect 2026-11-01
// night, "THE MAIN PAGE IS SETTLED"): its two channels' wells are parted by
// one row of the ground (waveform_divider), its edges the well's columns,
// rows of ground above and below it (the palette block's well paragraph;
// the ring of the panel's tones round each well stood 2026-10-09 to
// 2026-11-02).
//
// THE DISABLED FACES (2026-10-23; the key's own dim tone, Default's 5D5D5D,
// since 2026-11-02): a dead key DIMS ITS GLYPH (`key_dim_ink`, paint_key_glyph; a
// glyph on a dead lit key is the dimmed accent, `lit_dim_ink`, architect
// 2026-11-11; no key prints a word since
// 2026-11-02), its face the rest face; a CUE'S inert label is the
// palette's `inactive_cue` (the marker-lane paragraph); the settings page's inert GROUP
// ROWS wear the dim print (`text_dim` (Default's 757575), paint_folder_overlay,
// 2026-10-28); the STRUCK CLIP LAMP is its whole square in `key_dim_ink`
// (the logo lane block's THE STRIKE, 2026-11-05 night). Nothing else dims. (Windows' word emboss —
// DrawState DSS_DISABLED, the word in Hilight one Windows px right and down
// under its Shadow copy, show_embossed_run, 2026-10-03 — retired with no
// disabled word left, 2026-10-23.)
//
// WHAT WAS HERE BEFORE, in one sentence, because the file's shape is its
// residue: from 2026-10-03 to 2026-10-04 the chrome was a row of a generated
// table of imported themes (theme_table.h) at a light or dark level and the
// program's elements twelve device keys; until 2026-10-03 the palette was
// eight hard-coded base roles (a dark #303030 ground, a #96BFDA accent and the
// program's colours) with every other colour derived from them by stated
// ratios under static_asserts and the text over a fill chosen by WCAG 2.0's
// relative-luminance contrast — before that a kdenlive-sampled face
// (2026-07-31..10-02) and, until 2026-08-02, 23 mutable globals read from
// ~/.config — the rest being git history.

// -- THE CHOKEPOINTS ----------------------------------------------------------
//
// EVERY COLOUR THIS PRODUCT HANDS CAIRO GOES THROUGH ONE OF THESE TWO, and
// no site calls cairo_set_source_rgb itself (re-grepped 2026-10-03: none
// outside render.cpp's bodies): set_palette_source for the chrome — every
// word, glyph cell and mark, and every surface, the flat ground and the keys
// among them (the flat look, materials.h; the baked grounds and their
// surface chokepoint went with the hi-fi panel, 2026-11-01) — and
// set_waveform_source for the canvas's cairo fills, its ground and grid
// (render_canvas) and its center lines (render_center_lines). Each converts
// the authored sRGB color for the window (display_color, the painter's
// entry) and hands it over; the plate's own
// pixels are written as words (argb32_opaque_word), not through cairo,
// converting at their own entry. TEXT
// takes its ink from the source these set: no glyph road sets a color of its
// own (text_shape.cpp draws in the current source), and every text road
// sets its ink through set_palette_source, never a surface's.
void set_palette_source(cairo_t* cr, GuiColor c);
void set_waveform_source(cairo_t* cr, GuiColor c);

// -- THE ACTIVE PALETTE ---------------------------------------------------------
//
// The resolved colors, ONE STRUCT (2026-10-07): the chrome's ROLES —
// derived from the panel's Face since 2026-10-10, the flat look's constants
// 2026-11-01..05, THE INSTALLED THEME'S TONES since 2026-11-05 (theme_file.h)
// — and the palette's program roles, one field each — the chrome's in
// kGuiThemeRoles' order (theme_file.h), the program's in kGuiPaletteRoles'
// (palette_file.h), and the panel's `face` — the tables covering the struct
// exactly (palette_file.cpp's static_asserts); the mapping above says what
// each paints. Every field is a solid byte triple.
struct GuiPalette {
    // THE CHROME (Windows' element names; the values the installed theme's,
    // theme_file.h's role table).
    GuiColor ground;          // COLOR_3DFACE
    GuiColor label;           // COLOR_BTNTEXT
    GuiColor selected_fill;   // COLOR_HIGHLIGHT
    GuiColor selected_text;   // COLOR_HIGHLIGHTTEXT
    GuiColor field_text;      // COLOR_WINDOWTEXT
    GuiColor card_ground;     // COLOR_INFOBK (the cards' face)
    GuiColor card_text;       // COLOR_INFOTEXT (the cards' words and glyph)
    GuiColor card_frame;      // the cards' ring, in the fill (COLOR_WINDOWFRAME)
    // THE FLAT LOOK'S SEVENTEEN (materials.h's head owns each rule;
    // members since the theme arc, 2026-11-05, their numbers the installed
    // theme's — Default's quoted at materials.h; the three press tones the
    // press arc's, the same day; the clip lamp the lamp arc's, the same
    // night; the level fill 2026-11-11).
    GuiColor text;            // every word, digit and drawing the chrome prints
    GuiColor text_dim;        // the inert rows, the tooltip's hint line
    GuiColor key_dim_ink;     // a disabled key's glyph and legend, the struck clip lamp's whole square (THE TWO LAMPS' STRIKE)
    GuiColor key_face;        // the key at rest, checked or not; the time fields, the two lamps at rest
    GuiColor key_press_face;  // a key's face under a standing press, checked or not
    GuiColor well_frame;      // the scrub's channel, the tooltip's border
    GuiColor ruler_face;      // the ruler
    GuiColor ruler_tick;      // the ruler's ticks
    GuiColor lane_face;       // the marker lane
    GuiColor trim_bar_face;   // the trim bar outside the trim
    GuiColor trim_span_face;  // the trim bar inside the trim
    GuiColor pane_face;       // the modal family's box interior, the tooltip's face
    GuiColor score_pane_face; // the score viewer's pane, the page's mat
    GuiColor list_press_fill; // a list row's fill under a standing press
    GuiColor list_press_text; // that row's text and glyph
    GuiColor clip_lamp;       // the logo lane's clip lamp, lit and not struck (THE TWO LAMPS, the logo lane block; the limiter lamp lit wears card_ground)
    GuiColor level_fill;      // the NOISE level case's fill, Live's RangeEditField over the key face (paint_level_case, paint_handler.cpp; 2026-11-11)
    // THE PROGRAM'S OWN ELEMENTS — THE PALETTE (kGuiPaletteRoles,
    // palette_file.h).
    GuiColor waveform_canvas;
    GuiColor waveform_ink;
    GuiColor waveform_core;
    GuiColor grid;
    GuiColor center;
    GuiColor cue;
    GuiColor cue_outline;
    GuiColor inactive_cue;
    GuiColor playhead_head;
    GuiColor playhead_line;
    GuiColor scanner;
    // THE PROGRAM'S PANEL (architect 2026-10-09, Cool Edit's canvas
    // grammar): the ground of the toolbar band, row 8 and the well's
    // margins — THE FLAT GROUND since 2026-11-01 (materials.h's head), the
    // theme's `ground` tone like the chrome's `ground` role.
    GuiColor face;
};

// THE ONE ACCESSOR every painter reads. The installed palette is file-scope
// state in render.cpp, written by the install family below alone:
// install_palette at gui_main's startup, before the window exists, and at
// every `theme` commit (GuiInputHandler::apply_theme), and
// install_true_colors. Before the first install it is construction state
// (black), never painted.
const GuiPalette& palette();

// THE INSTALLED THEME'S ROW (a copy of the list's row, theme_ask.h) — the
// sRGB WORDS behind palette(), for the few readers that need the word rather
// than a GuiColor: the Breeze glyph road's tone (icons::draw_glyph; key_ink,
// materials.h), the plate's two inks (waveform_plate_inks below). Every other
// painter reads palette(). Before the first install it is the compiled
// Default row (construction state, as palette()'s black), never painted.
const GuiTheme& installed_theme();

// THE SURFACES WHOSE TEXT ROLE MAY DEPEND ON THE CHROME (2026-10-09): each
// surface's recorded text color under the live chrome — "text over a fill
// is the theme's recorded pair" — read by the words painted there, the one
// owner of the choice a later vocabulary with roles of its own would fork
// (no glyph reads it since 2026-10-09: the project's drawings wear their
// own inks, and the keys' Breeze glyphs the tone their caller passes,
// icons.h's head):
//   ListRow          the list's text pair, resting and lit — the field's
//   ListRowLit       text and the selection's; the folder overlay's and
//                    the project picker's row names (paint_folder_overlay)
// (The caption's pair, CaptionActive and CaptionInactive, 2026-10-09..10-21,
// left with the caption's roles; the caption itself went 2026-10-24, the
// caption note.)
enum class GuiSurface {
    ListRow,
    ListRowLit,
};
GuiColor surface_text(GuiSurface surface);

// Install every color off ONE THEME'S ROW (`theme`, a row of the themes'
// list, theme_ask.h; architect 2026-11-05, the theme arc): THE PROGRAM'S
// members off the palette's role table (kGuiPaletteRoles, palette_file.h),
// THE CHROME'S off the chrome's (kGuiThemeRoles, theme_file.h), the panel's
// `face` off the theme's ground — the tables covering the struct exactly
// (palette_file.cpp) — and the row COPIED as installed_theme() (architect
// 2026-11-06, session 75: the list is built once at launch and immutable, a
// pointer into it would hold too; the copy needs no lifetime argument and
// covers the construction state before the load). Its callers:
// gui_main's startup, before the window exists (the config's `theme`), and
// every `theme` commit through GuiInputHandler::apply_theme. It resolves and
// never refuses, and it bumps palette_generation below, the flag cache's
// fingerprint term.
// THE APPLY SHAPE THE CALLER OWES after a live install (apply_theme; the
// launch's call, before the window and every cache exist, owes none) IS
// install_true_colors' (below), the same two caches and for the same
// reason: the icon rasters dropped (icons::drop_rasters — a key glyph's
// raster keys on its sRGB tone, which the theme moves), the waveform plate
// kicked when its two inks moved (the waveform_plate_inks pair read before
// and after; Viewport::kick_waveform_sync, the plate re-rendered in the new
// inks and the flag cache rebuilt at its tail) and the flag cache alone
// otherwise (Viewport::refresh_flag_cache), then the whole window
// invalidated. THE SHADER NEEDS NOTHING: gl_canvas.cpp's list fragment
// shader reads every color from the draw list's own words (word_rgb of
// v_words.x / .y; no color uniform), and the plate's two inks ride
// waveform_plate_inks into the worker's job (waveform_cache.cpp) and its
// fingerprint, so a theme that moves them rebuilds the plate and one that
// keeps them re-renders nothing.
void install_palette(const GuiTheme& theme);

// THE PALETTE'S GENERATION — a counter the install family bumps, so a
// cached surface whose pixels carry palette colors keys the palette BY
// FIELD (the flag cache, FlagCache::fp_palette_generation, and the stem
// stash its rebuild stages, AppState::staged_marker_stems). The waveform
// plate bakes only its two inks and keys those directly
// (waveform_plate_inks).
uint64_t palette_generation();

// THE INSTALL FAMILY'S THIRD MEMBER, TRUE COLORS (architect 2026-10-08
// ~05:35; the rule and the ruling at display_transform.h's head): the
// `true_colors` device key (2026-10-21; the Settings menu's "True Colors"
// row 2026-10-08..10-21) — read at launch and committed by the settings
// editor — writes the switch's process-lifetime
// bit (display_transform::set_true_colors) and bumps palette_generation, as
// every color the window is handed moves with it. EVERY CACHED CONVERTED
// PIXEL IS REBUILT, re-grepped 2026-10-08 against the head's list of
// converting sites: the flag cache by the generation bumped here; the
// waveform plate by its inks' fingerprint (waveform_plate_inks hands the
// window's words). THE APPLY SHAPE THE
// CALLER OWES, after the call (GuiInputHandler::apply_true_colors; the
// launch's call, before the window and every cache exist, owes none): the
// icon rasters dropped (icons::drop_rasters — they convert at raster time
// and key on the device px and, a key glyph's, its sRGB tone, which the
// flip does not move), then the waveform
// plate kicked when its inks moved — the waveform_plate_inks pair read
// before and after the call; when it moved, the synchronous plate rebuild
// (Viewport::kick_waveform_sync — the plate re-rendered in the new inks, the
// flag cache rebuilt at its tail, keyed by the generation) — and otherwise
// the flag cache alone, synchronously (Viewport::refresh_flag_cache, so a
// frame callback served before the tick cannot blit flags in the old
// colors), then the whole window invalidated — every color changes. WHY
// THAT IS ENOUGH: the palette bakes into two cached things alone — the
// waveform plate, keyed by its two inks (waveform_plate_inks, the worker's
// job carrying them), and the flag cache, keyed by the generation
// (FlagCache::fp_palette_generation), whose rebuild also stages the stem
// stash (AppState::staged_marker_stems, swapped in at the publish; the
// stem's color read live); every other reader paints live from palette()
// each frame — the chrome's flat ground, keys and words, the canvas, its
// grid and its center lines (render_canvas), the
// selection stems (paint_marker_stems), the playhead's head and line,
// the open flag editor (render_flag_editor_box), the lead-in ring and the
// `h` view's nudged-pair rings, the zoom anchor's stem and the scanner.
void install_true_colors(bool on);

// -- THE CANVAS COLUMN: THE VIEW BAR, THE RULER, THE PLAYHEAD -------------------
//
// THE WELL — THE CANVAS COLUMN, COOL EDIT'S GRAMMAR IN LIVE'S FLAT LOOK
// (architect 2026-10-09, the program is Cool Edit; 2026-11-01 night, "THE
// MAIN PAGE IS SETTLED", M5h, M13's text tones 2026-11-02, and the text
// round's lengths 2026-11-03): THE TRIM BAR, THE RULER FLIPPED and THE MARKER
// LANE OF CUES directly under the band, THE CANVAS under them (row 6's
// canvas paragraph) — FLAT REGIONS ON THE GROUND, NO FRAME, NO RING, NO
// BEVEL, NO SHADOW, parted by rows of the ground itself (materials.h's THE
// WELL, every tone's owner). THE EDGE RULE (architect 2026-11-01 night, "the
// rightmost button's right edge lines up with the trim/time/marker/canvas
// region, and the same for the left"): every region spans the well's
// columns (well_columns below), the outermost keys' faces' outer columns,
// 24 .. 2279 on the tablet's 2304 at 200 %, the ground beside them. THE WELL IS THE WAVEFORM
// AREA (architect 2026-11-03): its columns are the canvas's interior — the
// waveform area every painter and hit test reads — and nothing more, so no
// column of canvas without ink frames it. Every line in the well is ONE W,
// rounded at the element (hairline_px, the line rule; architect 2026-11-04,
// the unit, reversing the one device px at every scale of M5h; one W = 2
// device px on the tablet at 200 % since the rebase of 2026-11-05; the
// ruler's ticks and every canvas line the same W, waveform_line_px). THE
// ROWS ON THE TABLET (200 %; main.cpp's lane table the record; at
// key_height 17, its default): rows 144 .. 145 ground, the trim bar 146 .. 163 (its
// fill 146 .. 161, rows 162 .. 163 ground), the ruler 164 .. 199, rows 200
// .. 201 ground, the marker lane 202 .. 240, rows 241 .. 242 ground, the
// canvas 243 .. 1327, rows 1328 .. 1329 ground (architect: "waveform height
// ok to take from").
//
// THE TRIM BAR IS LIVE'S OVERVIEW BAR (render_trim_flags owns the drawing,
// trim_lane_h_px the rows; architect 2026-11-03, mock z2): one row of ground
// under the band (the row the retired program ring stood on), then THE
// BAR'S FILL ROWS (trim_bar_fill_h_px, 8 W, 16 device px at 200 %) across the
// well's columns, NO FRAME — OUTSIDE THE TRIM a
// flat `trim_bar_face` (Default's 2A2A2A) (the marker lane's tone), INSIDE IT, THE SPAN,
// the trim window's columns from the begin column through the end column
// clipped to the waveform's columns (so a trim spanning the view fills the
// bar end to end), a flat `trim_span_face` (Default's 696969) — and the bar's last rows ONE
// W OF GROUND across the well's columns, the span included, the
// line between the bar and the ruler (no line above: the row above the bar
// is the lane's ground). NO PLAYHEAD IN THE BAR (architect 2026-10-09
// ~14:30, "I thought we agreed no dots on the trim bar"). No track, no caps,
// no pressed face: the lane's grabs are the span's two ends and its body
// (TrimBarHit, below), and they paint nothing of their own.
//
// THE RULER IS COOL EDIT'S, FLIPPED, IN LIVE'S TONES (paint_ruler_row):
// FLAT `ruler_face` (Default's 363636) directly under the bar's ground line, NO LINE ON
// ITS TOP; THE TICKS `ruler_tick` (Default's 919191), ONE W WIDE (the canvas's line,
// waveform_line_px, 2026-11-04), ON THEIR COLUMNS STRADDLING THEIR FRAMES
// (the line rule, 2026-11-06), STANDING ON THE
// RULER'S FOOT, the major 4 W and the minor 1 W (8 and 2 device rows at
// 200 %, scaled_px), overlaying its bottom pad, NO SHADOW; THE DIGITS the
// ruler face (GuiFace::Small, BOLD 8 W, 16 device px at 200 %) in the text
// tone `text` (Default's B5B5B5)
// (architect 2026-11-02, the font audit: Live's own ruler digits), bare,
// THEIR INK CENTERED ON THE RULER between two 6-W pads (ruler_baseline_px,
// the ruler block below; HIS RULE, 2026-11-03); then ONE ROW OF GROUND, one
// W, the ruler lane's last (ruler_lane_h_px).

// THE PLAYHEAD IS COOL EDIT'S CURSOR (architect 2026-10-09) IN LIVE'S TONES:
// ITS HEAD the cue's chip, THE SAME DRAWING (architect 2026-11-04, mock k1,
// "4 accept overlap") — its rows (cue_triangle_rows, 5 W, 10 at 200 %) in whole
// device cells, Cool Edit's aliased drawing (2026-10-26), the apex the
// playhead's column (paint_ce_cue_triangle; the triangle's cell one device
// px, architect 2026-10-26 ~10:10: "All triangles unscaled",
// cue_triangle_unit_px), IN THE CHIP'S ONE-W `cue_outline` 111111 (the
// outer 29 x 15 at 200 %) — its fill THE PALETTE'S `playhead_head`
// (architect 2026-10-09 ~11:50; Live's playhead cyan 03C3D5 since
// 2026-11-01, palette_file.h), NO SHADOW (2026-11-02), THE OUTLINED SHAPE
// ON THE RULER'S LAST cue_chip_rows DEVICE ROWS, its apex on the ruler's
// foot row over the ticks' feet — it covers what stands under it
// (2026-10-09 ~21:00), the digits included where it stands under a label
// (ACCEPTED, architect 2026-11-04: "timestamp not really important for fine
// tune, just ballpark") (paint_ruler_row; cue_chip_h_px's rows; its painted
// columns, the cull and the move's damage box one extent, the chip's,
// cue_triangle_reach_right_px); IN THE
// CANVAS A SOLID LINE, ONE W, on its column (architect 2026-11-04:
// Live's playhead line, measured; canvas_line_rect, the scanner's form) in
// THE PALETTE'S `playhead_line` (Default's 42D2E0), down the area's whole height,
// the stereo divider's row included (architect 2026-11-02) — ONE STATIC
// TONE, opaque, the same over the ink and over the canvas (architect
// 2026-11-04 night, mock n1, after the measured cost of a fading line; the
// head its own cyan; render_playhead; paint_playheads); nothing of it is
// damaged at rest. Across the trim bar (architect 2026-10-09 ~14:30, "no
// dots on the trim bar"), the marker lane and the row of ground under it it
// draws nothing. IT MAY STAND ON A MARKER — NO SNAP, NO AVOIDANCE
// (architect 2026-10-09): the head and a cue's chip stand in different
// lanes, and ON A SELECTED ENABLED MARKER the marker's `cue_outline` stem
// paints OVER the line (the paint order playhead, stems, scanner; THE
// MARKER LANE's selection stem, below), so the dark stem is what shows
// there and the ruler's head marks the playhead; on any other marker the
// line shows as it stands.
// THE HEAD IS GRABBED AND DRAGGED, as Cool Edit's is (architect 2026-10-09
// ~17:40): its HIT is the head as painted, its columns (the outlined chip,
// cut to the waveform's) over its device rows of the ruler's,
// no box beyond them — published by paint_ruler_row
// (AppState::playhead_head_hit) and read by point_on_playhead_head
// (input_pointer.cpp), a cue over the head keeping the press; a plain press
// there drags the playhead, every motion past the slop the ruler's
// placement act at the pointer's column (ScrollDragState::head_drag).
// NO HOLD LAMP (architect 2026-10-05): the head paints the same whether or
// not AppState::camera_hold stands. (WordPad's ruler marker in the chrome's
// four roles stood 2026-10-05 to 2026-10-09; a solid line before it; git
// history.)
// THE PLAYHEAD WEARS ITS TWO COLORS: `playhead_head` is its head, Live's
// cyan 03C3D5, and `playhead_line` its line, Default's 42D2E0 (architect
// 2026-11-04 night, the static line; 2026-11-10, session 103: the head
// lightened a quarter of the way to white on every theme); neither is
// lent to a marker (the selection is the
// chip's, THE MARKER LANE below). THE STEMS THAT BELONG TO THE
// CONTROLS ARE WHITE, ONE ROLE, `scanner` (architect 2026-10-09: "the zoom
// stem and the scanner are both white, which unifies them: the non-playhead
// stems that are related to the controls should be white, and then the
// playhead is [colored]"): THE SCANNER — the moving playback line,
// paint_scanner, its own role since 2026-10-05 — a SOLID line one W wide
// over the canvas, on its column (Cool Edit's playback cursor is a
// solid line, measured 2026-10-09; one W since 2026-11-04,
// waveform_line_px's rule), and THE ZOOM ANCHOR'S STEM
// (render_strip_anchor_stem), the same solid line at the pivot;
// WHITE FFFFFF — COOL EDIT'S OWN CONSTANT (architect 2026-10-16, observed
// in Cool Edit Pro 2.1: the scanner is white on every preset he tried; the
// zoom anchor's stem is the same design, so both are canonical), his
// rulings of 2026-10-07 ("let's go back to a white scanner") and 2026-10-09
// its confirmation, kept with Live's palette 2026-11-01 (tunable later).

// -- THE MARKER LANE: COOL EDIT'S CUES ---------------------------------------------
//
// THE FLAG IS COOL EDIT'S CUE (architect 2026-10-09; the lengths program_spec.h's,
// the drawing render_flag_boxes_impl's): on the lane's FLAT `lane_face` (Default's 2A2A2A)
// (architect 2026-11-01 night, Live's clip-view marker lane; Live's next
// fill above the ground since 2026-11-03, the retrofit; materials.h),
// THE CHIP — COOL EDIT'S TRIANGLE IN A ONE-W OUTLINE (architect 2026-11-04,
// mock g1; render.h's cue_chip_rows owns the construction): the triangle
// Cool Edit's staircase in whole device cells, its rows scaled (architect
// 2026-10-26, paint_ce_cue_triangle's head; cue_triangle_rows: 5 W, 10 at
// 200 %, 5 at 100 %; Cool Edit's own 9 by 5), Cool Edit's aliased drawing,
// round it a second, larger staircase in the outline's tone (29 x 15 round
// the fill's 19 x 10 at 200 %), on the lane's last rows, its apex the marker's
// column on the lane's last row, directly above the row of ground under
// the lane, NO SHADOW (2026-11-02);
// THE LABEL — the marker's one label, at rest in the text tone `text`
// B5B5B5 (materials.h; M13, 2026-11-02), NO SHADOW (2026-11-02; its shadow
// of 2026-10-25 .. 2026-11-02 retired with the well's flat look), in THE
// LABEL FACE, BOLD Label 11 W, 22 device px at 200 % (gui_font.h's
// kGuiFaceSizes; architect 2026-11-01 night, M5i; the retrofit's 22 device
// px), starting the fill's reach right, the outline's W and a gap of 6 W
// right of the column (cue_label_lead_px: 12 W, 24 device px at 200 %), ON
// THE LANE'S ONE BASELINE
// at rest and in the editor (cue_baseline_px, THE METRICS RULE at
// marker_lane_h_px: the x-height band centered in the lane, 13 rows from
// its top and 14 from its bottom at 200 %) — `show_cue_label`, render.cpp,
// the one painter of every label look on the face; and THE STEM, ON EVERY
// SELECTED ENABLED MARKER — a SOLID ONE-W LINE in the `cue_outline`, on
// the column (straddling the frame, the line rule at waveform_line_px),
// FROM THE CHIP'S APEX ROW (the
// lane's last) through the row of ground under the lane down the canvas's
// whole height (waveform_line_px, canvas_line_rect's form;
// paint_marker_stems and its head segment, paint_marker_stem_heads;
// architect 2026-11-04, mock j1, "3 yes"): THE SELECTION STEM (architect
// 2026-11-05, his pick a2 of tmp/s42, reversing 2026-11-04's coincidence
// stem: "dropped, along with the rest of cool edit stuff except a few
// details like the triangle playhead"). NO OTHER STEM (Live paints none at
// rest). No box round the label.
//
// THE CHIP CARRIES THE SELECTION, THE PLAYHEAD THE FOCUS (architect
// 2026-11-04, reversing 2026-11-02's "the triangle never marks focus or
// selection": "we don't use ableton's rect above a marker and could
// therefore allow the triangle chip to stand for selection/focus";
// 2026-11-05, "only 1 playhead onscreen ever"): EVERY ENABLED CHIP'S FILL
// the palette's `cue` (Live's orange FFAD56; architect 2026-11-01: "one
// color for all markers" — every cue, the history's halves and a struck
// marker's included), the OUTLINE its `cue_outline` (Live's dark line,
// 111111), and THE SELECTED marker's mark THE STEM in the same
// `cue_outline`. The focused member has no paint of its own: every click
// and toggle lands the playhead on it (land_playhead_on_marker), and the
// playhead is its mark.
// WARP AND PHASE RESET WEAR THE SAME CUE:
// the kind reads from the view that lists it — the lane paints the active
// column's markers alone, the Warp and Phase Reset views — and from its
// label (his one-label rule). THE STRIKE IS THE INVALID INDICATOR (architect 2026-10-26 ~15:10:
// "It's very odd having anything but red mean invalid. So I think what we
// should do is have coincident markers be shown as a strike through, which
// is actually effective because that's what happens to them. They become
// one. Very easy to see. So just the strike through on the text"; the
// strike's shadow went with every shadow, 2026-11-02): a coincident marker
// wears the plain cue with its label STRUCK (show_cue_label's bar, the face's own
// stroke on its own strikeout rows, gui_font_strike_band) — the invalid
// color of 2026-10-11 retired with its role. THE TABLE — which fill, which
// outline, which stem, which label, which strike — is resolve_flag_face's
// head (render.cpp), the one resolver every cue goes through.
//
// THE LABEL'S SEGMENTS: the marker's payload label, then — while grid
// iterations paint them — its LOWER and UPPER bound cells, each a segment of
// the same label standing the segment gap past the one before it (the
// history's same-frame pair likewise: its removed half, struck, then its
// added half), every segment's text the label face on the lane's
// baseline.
//
// THE OVERLAP RULE: LATER WINS BY OCCLUSION, NEVER BY CUTTING (architect
// 2026-10-09, settled on five overlap captures; 2026-10-10
// ~19:00, after two near-coincident phase resets showed the earlier one's text
// over the later's: "why not go simpler — later always wins?", and "a long
// flag before a short one should continue after the short one ends"). The
// labels paint LEFT TO RIGHT, each on an OPAQUE FACE BOX of the lane's own
// `lane_face` from ITS OWN
// CHIP'S LEFT EDGE (the column less the chip's half width) to one
// fill pad past its last segment's text, on THE WHOLE LANE'S ROWS (the
// press target's rows, so the label is one geometry at rest and selected,
// 2026-10-09 ~21:00), so
// a later cue masks an earlier label's text only across its own
// chip-to-end extent and an earlier long label's text resumes past a
// short later one; no label is ever clipped. Then the chips paint LEFT TO
// RIGHT over every label, so a right chip stands whole over its left
// neighbour's text and chip. The press follows the picture (topmost_flag_rect, app_state.cpp):
// the rightmost chip, then the rightmost label box, under the point.
//
// THE STATES (resolve_flag_face, render.cpp, the one ladder: disabled wins,
// then the live cue by its selection; the strike rides every row):
//   UNSELECTED  the `cue` FFAD56 fill in the `cue_outline` 111111 outline,
//             the label in the text tone (materials.h's `text`); no stem,
//             the playhead on it or not (architect 2026-11-05);
//   SELECTED  a member of the selection set: the same chip, and A SOLID
//             ONE-W `cue_outline` STEM on its column FROM THE CHIP'S APEX
//             ROW (the lane's last) THROUGH THE ROW OF GROUND UNDER THE LANE
//             TO THE CANVAS'S LAST ROW (mock j1, 2026-11-04: no gap under
//             the apex), whether or not the playhead stands on it (architect
//             2026-11-05; MarkerStem, paint_marker_stems — THE RESOLVER
//             NEVER SEES THE PLAYHEAD). THE STEM PAINTS OVER THE PLAYHEAD'S
//             LINE (the paint order playhead, stems, scanner): both are one
//             W on one column, so a stem under the opaque line would never
//             show; while the playhead rests on a selected marker the dark
//             stem is what shows and the ruler's head marks the playhead.
//             THE LABEL CHANGES NOTHING: it paints as at rest, no fill, its
//             box the resting one. THE PRESS TARGET STAYS
//             THE WHOLE LANE (architect 2026-10-09 ~21:00, "the text box
//             should take up the whole marker lane … right now it's too
//             small to click"): the box is geometry, not paint. THE EDITOR
//             is where the selected pair's fill appears: it opens with the
//             whole text selected in the selection pair inside its field
//             (EDITING, below). The history's changed pair, one item, takes
//             the selected look on both halves, each cue its own stem
//             (render_history_diff_flags);
//   STRUCK    the red-flag class (warp_red_flag_set_cached and its
//             phase-reset twin — the coincident run, a warp label ref
//             the render falls back from, and the definitions of a label
//             two live definitions hold, 2026-10-29) and the history's
//             REMOVED half: its label STRUCK — the bar part of the text, in
//             its ink, in every look (the inactive label's too);
//   DISABLED  A SOLID GREY CHIP (architect 2026-11-05, "disabled marker
//             gets dim label"; Live's inactive marker's grey, mock C3,
//             2026-11-02; the inert look's reason 2026-10-10 ~12:20, "the
//             arrow needs to be legible for me, but the text doesn't …
//             since it's disabled, it's inert"): the `inactive_cue` 5D5D5D
//             fill in the `cue_outline`, the label in the `inactive_cue`,
//             NO STEM, the playhead on it or not; the same inert label on
//             a tie follower's cells (one look for inert cue text);
//   SELECTED DISABLED (architect 2026-10-07 ~15:10: a disabled marker is
//             selectable): IT SHOWS NOTHING OF THE SELECTION (architect
//             2026-11-02, "3 accept"): the grey chip and the inactive
//             label as at rest, no stem — the playhead standing on it after
//             the arming press (the ruler's head) its only mark.
// A CUE HAS NO HOVER FACE (architect 2026-09-29), AND NO PRESS FACE: the
// press look of 2026-11-05 is the keys' and the lists' rows' alone
// (materials.h's head), never a marker's.
//
// THE PHASE-RESET LEAD-IN RING on the waveform is FOUR SOLID ONE-W LINES in
// the `cue_outline` (architect 2026-11-04, the stems at rest retired;
// paint_phase_reset_overlay_ring — the ring and the cue one unit, architect
// 2026-09-17): its left side on the reset's column, its right side on the
// band's last column, both down the canvas's whole height, and its top and
// bottom runs along the canvas's first and last W of rows, WHATEVER THE
// SELECTION — every live reset's, a coincident one's too, its struck label
// the mark (2026-10-26); a disabled reset paints no ring.
//
// EDITING: THE FIELD IS AN EDIT FIELD ON THE LANE'S WHOLE ROWS (architect
// 2026-10-09, reversing 2026-10-07 ~09:45's flag-in-its-selected-face; its
// look 2026-11-01 night, M5g / M5h and the marker lane's metrics rule; the
// drawing render_flag_editor_box's): a field of THE GROUND `ground` (Default's 242424) in
// A THREE-DEVICE-PX `text` (Default's B5B5B5) OUTLINE ("as today",
// cue_editor_outline_px), the text and the caret in the field text (the
// text tone), over the edited segment's box on THE WHOLE LANE'S ROWS (the
// outline on the lane's first and last rows and just outside the pads),
// the text at the segment's own seat ON THE LANE'S ONE BASELINE — the
// resting label's (cue_baseline_px) — with the fill's pad either side of
// its run (cue_fill_pad_px), its descenders inside the field (the text's
// clip the field's inner rows), THE CARET AND THE SELECTION BAND THE LEGAL
// SET'S BAND (cue_band_top_px, cue_band_h_px: the warp line's grammar's ink
// with the x-height band centered in it, THE METRICS RULE at
// marker_lane_h_px) — at 200 % lane rows 8 .. 29 of the 39, six rows of
// field above it and seven below inside the outline. A selected glyph's ink past the band takes the
// field text, the field ink's clip the band's complement in both its columns
// and its rows. The field grows and shrinks with what is typed, the
// segments to its right riding its edge in their resting look; and THE
// SELECTED SUBSTRING IN THE CHROME'S SELECTED PAIR, `selected_text` 111111 on
// `selected_fill` 637E86 (Live's browser selection, materials.h's
// `selection_fill` / `selection_text`). A COINCIDENT MARKER'S FIELD IS
// THE SAME FIELD, its text and caret the field's own, NEVER STRUCK (the text
// there is being edited), its riding segments struck as at rest. WHY A
// FIELD: it is the legibility guarantee ("when you're looking very closely
// at a label it has to be legible"), and the selection pair is the highlight
// "precisely to avoid this problem". THE BOX PAINTS OVER THE LANE, the
// triangle and any neighbouring label ("it may overlap with the flag and
// other elements, that's fine"). Outside the editor a selected label is its
// resting text, no fill and no box (SELECTED, above); the open
// editor shows its whole text selected in the selected pair inside the
// field.
//
// A REFUSED ENTER RECOLORS NOTHING (architect 2026-10-03: "a red outline and
// the card is redundant"; the red frame retired from both editors): it
// SELECTS THE WHOLE TEXT in the selected pair inside the field
// (text_editor::refuse), so the first keystroke replaces it, and the
// refusing owner's CARD says why. No glyph marks it.
//
// THE HISTORY VIEW'S DIFF FLAGS are cues of their own two kinds (architect
// 2026-10-04; THE STRIKE 2026-10-26 ~15:30, "Two, agree"): both halves THE
// PLAIN CUE — the chip of the live lane's table — a REMOVED line's label
// STRUCK and an ADDED line's plain, the label
// the line's own text with no sign (history_diff_label, paint_handler.h; the
// brackets `[+]` / `[-]` of 2026-08-05 retired the same day: "those are
// always hard to see anyways at the font size. A strike through is easier to
// see"). A same-frame changed pair is one cue, its label the two halves as
// two segments; a nudged pair two cues joined by the ring
// (render_history_diff_flags owns the pair). The view's focus or selection
// gives the whole item the live lane's SELECTED row — both halves' chips
// the `cue` fill, each cue its own stem, the labels as at rest
// (render_history_diff_flags).

// -- ROW 6: THE WAVEFORM ---------------------------------------------------------
//
// THE WAVEFORM IS THE PROGRAM'S, A CANVAS UNDER ONE INK (the
// `waveform_canvas` and `waveform_ink` roles): with the magnification lamp dark
// the plate is the raw bar alone in the ink; lit, the plate is two bars (the
// rule is at render_waveform_columns' declaration), THE OUTER IN THE INK AND
// THE INNER A SOLID FILL OVER IT in the `waveform_core` role (architect
// 2026-11-04, mock f1, THE CORE below; antialiased with its shape on the GPU
// since 2026-10-30) — LIVE'S SurfaceBackground, THE KEY FACE, on every
// theme (architect 2026-11-07, session 83): on Default 363636 over the
// 191919 ink on the 393939 canvas, one step under the canvas (the tone's
// record at THE CORE below; palette_file.h). THE PLATE WEARS THE INK AND THE
// CORE AS PUBLISHED (the sink writes their words, GuiCanvasSink::plate),
// so the pair rides the cache's fingerprint (waveform_plate_inks) and
// publishes with the plate; the canvas is laid live under the plate's
// transparent gaps (render_canvas).
//
// THE CANVAS IS COOL EDIT'S (architect 2026-10-09, the third part):
// THE WAVEFORM AREA IS THE CANVAS'S INTERIOR AND THE WELL'S COLUMNS (the
// well's paragraph above; waveform_area, main.cpp; architect 2026-11-03), so
// every column mapping, hit test and damage box reads the interior and the
// canvas reads the well's whole width with the ground directly beside it.
// THE ORDER, bottom to top: UNDER THE WAVEFORM, in render_canvas's
// one pass, the `waveform_canvas` fill and THE HORIZONTAL GRID — per
// channel, a `grid` line one W tall, its top row on its row (canvas_hline,
// the unit of 2026-11-04; the vertical straddle not taken for rows) at the channel's zero row ± t ·
// nearbyint(k · H / (4t)) device rows for k = 1, 2, 3, H the
// channel's half height (its zero row to its band's edge; the k = 4 line
// would stand on the band's edge and is not drawn), the canvas's full width,
// Cool Edit's grid under the ink; then THE WAVEFORM'S PLATE (antialiased,
// its soft edge blended over the canvas and the grid — the one soft thing
// on the canvas, render_waveform_columns' head); then THE CENTER
// LINE per channel on its zero row, one W, in `center`, OVER THE INK
// (architect 2026-10-09 evening: "it's supposed to be visible over the
// waveform. Otherwise, in a project like this with tape hiss, there's never
// enough zero that the red line would become visible ever";
// render_center_lines, a live pass after the plate, the plate wearing its
// two inks alone); then THE STEREO DIVIDER'S ROW between the two
// wells (architect 2026-10-16, chrome on the canvas, waveform_divider;
// render_waveform_divider, over the plate so a plate from before a resize
// never crosses them); then the lead-in ring, the nudged pairs' rings, the
// playhead's line, the selection stems over it, the scanner and the zoom
// anchor (build_canvas_list's order). THE PICTURE IS ONE THING — the
// canvas, the grid, the ink if any, the center lines — SO THE CENTER LINES
// PAINT WHENEVER THE GRID DOES: over the plate while audio is loaded (a
// frame before the first plate included, its plate record absent), and with
// none loaded — no source, or a load running — directly over the canvas and
// its grid, right after render_canvas, the divider after them
// (build_canvas_list's `plate_branch`).
// NO VERTICAL GRID (architect 2026-10-09 ~18:40: Cool Edit's verticals on
// the ruler's major ticks, drawn that afternoon, were "solid versus the
// markers dotted, they compete" — "drop them"). HIS PRINCIPLE, recorded
// here: "accuracy is what applies to the conversion from Cool Edit Pro, and
// accuracy is lower priority than usability" — Cool Edit's rule yields
// where usability says so, while "truthfulness applies to the way we
// interact" (an invisible hit box is untruthful; on screen is as painted).
// A recorded departure from Cool Edit, as THE CENTER
// LINE OVER THE INK is (the original mock drew it under the waveform with
// the grid).
// The waveform's plate is drawn over the ground and the grid, the ink over
// every grid line, the center lines over the ink. Each
// channel's band and zero row are the plate's own (waveform_channel_band
// below), so the lines stand where the plate's zero is. THE CHANNELS FILL
// THEIR WELLS (architect 2026-10-09, "remove", the channels
// top to bottom; the two wells 2026-10-16): each takes the canvas's height
// less the stereo divider, halved and floored, channel 0 from the canvas's
// first row and channel 1 under the divider (an odd remainder the canvas's
// last row, below channel 1, no ink reaching it), with NO INSET above or
// below — the 6-W band that stood there was the tip-down playhead
// triangle's seat (deleted 2026-08-02) kept as a symmetric margin, and the
// playhead's head and the cues' triangles stand above the canvas,
// so nothing needs it; the stem, the playhead, the lead-in ring and the
// scanner paint over
// the ink there as they do everywhere in the wells. The leveler's headroom
// (waveform_gain.h's constants: its target, and the inner bar's foreground
// half) is the only thing keeping the peaks off the edges; an outer bar the
// clamp clips flat (the loud body, accepted there) stands on its band's edge
// row — against its well's edge. NO BOUNDARY LINES (architect 2026-10-09,
// "we have very limited vertical real estate"): Cool Edit's Bndy clip guides
// are not drawn. BETWEEN THE CHANNELS STANDS THE STEREO DIVIDER (architect
// 2026-10-16, waveform_divider below; one row of the ground since the flat
// well), a chrome row the plate and the horizontal lines leave alone, every
// vertical line crosses (architect 2026-11-02: "the divider is just another
// grid line now") and every input leaves alone (point_on_waveform_divider). The chrome's
// two-line sunken well that stood round the area (the theme's quartet,
// 2026-10-02 to 2026-10-09) is gone with the content band it carved out of
// the area. THE SELECTION STEMS, THE PLAYHEAD'S LINE and the scanner
// span the canvas's whole height, the divider's row included; THE ZOOM
// ANCHOR'S STEM spans it too.

// THE PLATE'S TWO BAKED INKS, as words: the fingerprint field and the job
// field that carry the active ink and core to the worker (the worker reads
// no live state), the installed theme's two tones (installed_theme,
// `waveform_ink` and `waveform_core`; palette_file.h). THE WORDS ARE THE WINDOW'S
// (architect 2026-10-08, display_transform.h's head): waveform_plate_inks
// hands the two roles through display_transform::display_rgb on the GUI
// thread, so the plate writes them as they come, and a flip of True Colors
// (install_true_colors) moves the fingerprint exactly when it moves a
// colored ink — a gray ink converts to itself and re-renders nothing — and
// a `theme` commit (install_palette) exactly when the theme's inks differ.
struct WaveformPlateInks {
    uint32_t ink_rgb  = 0;
    uint32_t core_rgb = 0;
    bool operator==(const WaveformPlateInks&) const = default;
};
WaveformPlateInks waveform_plate_inks();

// (THE GUI FONT SIZE AXIS IS GONE — architect approval 2026-08-01.
// kDefaultFontSizePt, set_gui_font_size_pt, gui_font_scale and the
// g_font_size_pt state behind them were the font_size setting: one GUI-wide
// monospace text size in points, with every strip dimension scaled
// proportionally off it. Row 7 deleted the monospace face itself, taking the
// last surface that read the axis, and the KEY left the .settings schema in the
// same arc — so there is nothing left to scale and nothing left to store. The
// one scale axis is gui_scale, below.)

// -- GUI scale (the redesign's own axis) -----------------------------------
//
// THE PRODUCT'S ONE SCALE AXIS since row 7 (it was the redesign's own, beside a
// font axis that is now deleted). The gui_scale setting is an integer PERCENT in
// [50, 1000] — the RANGE's one owner is is_gui_scale_percent (device_config.h),
// where the bracket and its four landmarks are spelled once. It is a PER-DEVICE
// preference since 2026-08-27, read out of the device config rather than out of
// a source's `.settings`; the current value lives as file-scope
// state in render.cpp, pushed by TWO application points (gui_main's startup,
// before the window exists, and the settings editor's `gui_scale=` commit; the
// `'` load-in-place left the list
// 2026-08-24, the act no longer applying a file's session prefs at all).
//
// THE UNIT IS ONE W, THE WINDOWS-95 PIXEL (architect 2026-10-02; the one
// statement at scaled_px below): every authored chrome length is a whole
// number of W — the number Windows 95 drew at 96 dpi, or, where Windows has
// no such element, the length that keeps its device size on the tablet —
// and gui_scale is the number of DEVICE px per W, in percent — the tablet
// 200 since 2026-11-05 (a 16-W glyph = 32 device px). EVERY PAINTED
// DIMENSION IN THE TREE RIDES IT through scaled_px, below, where the
// rounding rule is stated.
void   set_gui_scale_percent(int percent);

// THE LIVE PERCENT ITSELF, for the one thing a factor cannot serve: a CACHE
// FINGERPRINT FIELD. The scale is an input to pixels the way a line width or
// a gain field is, and a fingerprint keys its inputs BY FIELD rather
// than through whatever else happens to move with them — an integer percent is
// what makes that compare exact, where the factor is a double and a derived
// dimension is a coincidence. Nothing paints through this: every painted
// dimension goes on reading gui_scale_factor / scaled_px.
int    gui_scale_percent();

// Scale factor s = gui_scale / 100, device px per Windows px: as low as 0.5
// since the setting's grammar floor came down to 50 (architect 2026-08-10).
double gui_scale_factor();

// THE KEY'S HEIGHT, THE `key_height` TUNING KEY'S SEAT (architect
// 2026-10-29, on five mock sheets varying only the keys' height: "around
// 33"; its default 33 device px again since 2026-11-03, the retrofit, the
// square key of 2026-11-02 reversed; 34 device px since 2026-11-04 night,
// the face 70 x 34 on the tablet at the defaults): the live value IN W
// (scaled_px's axis;
// 17 W = 34 device px at 200 % since the rebase of 2026-11-05), read by
// icon_case_h_px (the case paragraph below; the face's width is the
// `key_width` tuning key's, its seat next) and by paint_key_glyph's ink
// size (paint_handler.cpp), nothing else. It is the scale's shape —
// render.h's accessors are free functions with no AppState, so the value
// lives as file-scope state in render.cpp, pushed by the same TWO
// application points: gui_main's startup read of the device config, before
// the window exists and so before the first layout, and the settings
// editor's `key_height=` commit (GuiInputHandler::apply_key_height, which
// re-lays-out at once). The grammar and the range are device_config.h's
// (parse_key_height); the construction state is 17 (kKeyHeightDefaultW),
// the first-run value the shipped defaults file's (device_config.h's THE
// FIRST-RUN VALUES). It settles into a constexpr once he rules the
// number, and this pair goes.
void set_key_height_w(int w);
int  key_height_w();

// THE KEY'S FACE WIDTH, THE `key_width` TUNING KEY'S SEAT (architect
// 2026-11-06, session 78: the width a tuning key like the height until he
// rules the number): the live value IN W (scaled_px's axis; 35 W = 70
// device px at 200 %), read by icon_case_w_px (the case paragraph
// below) and nothing else — every face's width, walk and hit box reads
// that accessor; the glyph follows the height alone. The key_height seat's
// shape exactly — file-scope state in render.cpp, pushed by gui_main's
// startup read of the device config before the window exists and by the
// settings editor's `key_width=` commit (GuiInputHandler::apply_key_width,
// which re-lays-out at once). The grammar and the range are
// device_config.h's (parse_key_width); the construction state is 35
// (kKeyWidthDefaultW), the first-run value the shipped defaults file's. It settles into a constexpr once he
// rules the number, and this pair goes.
void set_key_width_w(int w);
int  key_width_w();

// THE RUN GAP, THE `run_gap` TUNING KEY'S SEAT (architect 2026-11-07,
// session 87, his "agree": the ground between two runs a tuning key like the
// key's two sides until he rules the number): the live value IN W
// (scaled_px's axis; 22 W = 44 device px at 200 %), read by
// program_run_gap_px (the run gap paragraph below) and nothing else — every
// reader of the gap, the band's runs, row 8's groups, the render player's
// row and the controls rows, takes that accessor. The key_width seat's shape
// exactly — file-scope state in render.cpp, pushed by gui_main's startup
// read of the device config before the window exists and by the settings
// editor's `run_gap=` commit (GuiInputHandler::apply_run_gap, which
// re-lays-out at once). The grammar and the range are device_config.h's
// (parse_run_gap); the construction state is 22 (kRunGapDefaultW), the
// first-run value the shipped defaults file's. It settles into a constexpr once he rules the number,
// and this pair goes.
void set_run_gap_w(int w);
int  run_gap_w();

// THE ON-SCREEN KEYBOARD'S KEY BOX, THE `osk_key_w` AND `osk_key_h` TUNING
// KEYS' SEAT (architect 2026-10-29, the compact keyboard: "small, centered,
// like a phone … chiclets, taller than wide"): the live box IN W
// (scaled_px's axis), read by onscreen_keyboard.h's geometry and nothing
// else (osk_key_h moving the band's height and so its height floor's pads,
// onscreen_keyboard::edge_pad_px, 2026-11-08). The key_height seat's shape exactly — file-scope state in render.cpp,
// pushed by gui_main's startup read of the device config before the window
// exists and by the settings editor's `osk_key_w=` / `osk_key_h=` commits
// (GuiInputHandler::apply_osk_key_size, which re-lays-out at once). The
// grammar and the range are device_config.h's (parse_osk_key_size); the
// construction state is 24 x 30 W (kOskKeyWDefaultW, kOskKeyHDefaultW; 48
// x 60 device px on the tablet), the first-run values the shipped defaults
// file's. It settles into two constexprs once he rules the
// numbers, and these three go.
void set_osk_key_size_w(int w_w, int h_w);
int  osk_key_width_w();
int  osk_key_height_w();

// THE ON-SCREEN KEYBOARD'S CAP SIZE, THE `osk_font_px` TUNING KEY'S SEAT
// (architect 2026-10-29 night, "font size osk config tmp key"): the live em
// of the Keys face (gui_font.h) IN W, read by gui_font below and nothing
// else. The key box seat's shape exactly — file-scope
// state in render.cpp, pushed by gui_main's startup read of the device
// config before the window exists and by the settings editor's
// `osk_font_px=` commit (GuiInputHandler::apply_osk_font_px, which repaints
// at once). The grammar and the range are device_config.h's
// (parse_osk_font_px); the construction state is 12 (kOskFontPxDefaultW;
// 24 device px on the tablet), the first-run value the shipped defaults
// file's. It settles into the face table's Keys entry
// once he rules the number, and this pair goes.
void set_osk_font_em_w(int em_w);
int  osk_font_em_w();

// THE WAVEFORM PLATE'S GROW, THE `waveform_grow` KEY'S SEAT (architect
// 2026-10-31; the reason at kWaveformSubcells below, the grammar
// device_config.h's parse_waveform_grow): the live grow in QUARTERS OF A
// DEVICE PX — one subcell each on the 4x grid, so the value is the plate
// shader's GROW constant as it is — read by the canvas sink alone, which
// stamps it on the frame's draw list at every rebuild
// (GuiCanvasDrawList::plate_grow, canvas_sink.h), the GL canvas rebuilding
// its list program, whose source the grow is baked into, at the first draw
// of a list carrying another number (GuiGlCanvas::present_frame; the
// specialization rule at kListFragmentShader's head, gl_canvas.cpp,
// 2026-10-31 afternoon). The cap size seat's shape exactly — file-scope
// state in render.cpp,
// pushed by gui_main's startup read of the device config before the window
// exists and by the settings editor's `waveform_grow=` commit and its value
// list's tap (GuiInputHandler::apply_waveform_grow, which repaints at once:
// no plate re-render, the tips do not move). The construction state is 1
// (kWaveformGrowDefaultQuarters), the first-run value the shipped defaults
// file's. A PERMANENT KEY, unlike the
// three tuning seats above: this pair stays.
void set_waveform_grow_quarters(int quarters);
int  waveform_grow_quarters();

// A FACE AT THE LIVE SCALE AND SIZE (gui_font.h): what every painter and
// every seat asks for its text — each use at the face-size table's size
// (gui_font_table_px), the Keys face at the live `osk_font_px` seat's above
// (2026-10-29 night), at the current gui_scale. THE ONE ROAD a GuiFont is
// built by (GuiFont's own paragraph).
inline GuiFont gui_font(GuiFace face) {
    return GuiFont{face, gui_scale_percent(),
                   face == GuiFace::Keys
                       ? static_cast<double>(osk_font_em_w())
                       : gui_font_table_px(face)};
}

// One authored length in W -> device pixels, the ONE conversion every
// scaled dimension in the tree takes: device = std::nearbyint(w × percent /
// 100), like every other integer-domain conversion.
//
// THE UNIT (architect 2026-11-05, THE REBASE TO 200 %: "do the rebase to
// 200%. output should be pixel identical"; his reason the night before,
// "seems odd to use 300% and 2dpx"): ONE W = ONE PX AT 100 %, THE WINDOWS-95
// PIXEL THIS FUNCTION HAS ALWAYS SCALED, = 2 DEVICE PX ON THE TABLET AT ITS
// 200 %. EVERY AUTHORED LENGTH IS A WHOLE W (an integer), the face sizes too
// (gui_font.h's table), and a line is one W (hairline_px below). NO
// EXCEPTION: the three gesture thresholds the rebase left at x.5 W for a few
// hours (the hover slop 4.5, kHoverSlopW since 2026-11-07, kRulerMinMinorPitchPx 13.5 in
// paint_handler.cpp, kNavZoomPxPerLevel 217.5 in app_state.h) were ROUNDED
// UP to 5, 14 and 218 the same day (architect 2026-11-05: "round each up"),
// each seat naming its one-device-px consequence. THE PREDECESSOR was the
// unit of 2 at 300 % (architect
// 2026-11-04 night: one W = 2 device px at 300 %, every length authored in
// device px at 300 % as an even number on a second road, skin_px = scaled_px
// of a third); the rebase halved every such number, multiplied every Windows
// px by 1.5 and deleted the second road, so for an even d scaled_px(d / 2)
// at 200 % is the d device px skin_px(d) gave at 300 %, and for a Windows w
// scaled_px(1.5 w) at 200 % is the 3 w scaled_px(w) gave: THE PICTURE ON THE
// TABLET IS BYTE-IDENTICAL (one card width excepted, notifications.h's
// kNotificationMaxWidthPx). The 2026-10-02 "laptop pixel" unit before both is
// git history. 300 % is never proposed again (closed_questions.md).
//
// ROUNDED AT THE ELEMENT, NEVER AT THE SCALE, AND A COMPOSITE IS BUILT FROM
// ITS ROUNDED PARTS (architect 2026-10-02): a length that is the sum of
// Windows elements — a button case, a lane band, a row's walk — is the sum of
// each element's own scaled_px, never one scaled_px of the sum (a toolbar
// case is rounded offset + rounded glyph + rounded offset), so every element
// paints at the size it paints alone and the composite is exactly its parts.
// Stated here and nowhere else; every composite in the tree follows it.
//
// Every scaled accessor below (and the painters' own lengths in
// paint_handler.cpp / render.cpp) spells its conversion through this pair
// rather than open-coding the multiply; the DOUBLE-domain readers — the
// face metrics (gui_font.h: a font quantity is not a grid point, rounded at
// the seat that reads it) and the ruler's unrounded pitch compare — are a
// different concept and deliberately do not come through here. THIS IS THE
// ONE ROAD: every W-authored constant in the tree (`k…W`, the `key_height`
// and `osk_*` seats, program_spec.h's fields) and every older Windows-px
// constant (`k…Px`) scale through it alike.
inline int scaled_px(double authored) {
    return static_cast<int>(std::nearbyint(authored * gui_scale_factor()));
}
// The floored form: `floor_px` is the PER-METRIC minimum the accessor states,
// so a small factor cannot zero a structural dimension. Each floor is a
// constructive domain invariant: it never refuses and clamps no setting.
//
// THE FLOORS ARE LIVE, NOT DEFENSIVE, SINCE 2026-08-10 (the gui_scale floor
// 100->50). They were written when the schema's own floor was 100% and could
// only fire on an out-of-domain factor; at s = 0.5 every AUTHORED 1 px rounds
// to 0 — std::nearbyint is banker's rounding and takes 0.5 DOWN — so each 1px
// border, edge, pad and separator in the tree reaches its floor and paints as
// the one pixel that keeps the surface visible. A metric whose authored value
// may legitimately vanish floors at 0 and says so at its own accessor.
inline int scaled_px(double authored, int floor_px) {
    const int v = scaled_px(authored);
    return v < floor_px ? floor_px : v;
}
// (THE MONOSPACE TEXT-BOX PADDING FAMILY IS GONE — row 7, 2026-08-01. It was
// flag_pad_x_px / flag_pad_y_px, kChipOutlinePx, kTextBoxPadPx /
// text_box_pad_px, kTextBoxMarginPx / text_box_margin_px and
// flag_glyph_inset_px: the chip anatomy every monospace text box was built
// from. Its last audience was the three bottom-strip editors (the modal
// dialog's editors since 2026-08-12), which paint
// SHAPED text through the one chokepoint with no chip around them — the caret
// and selection take the shaped run's own byte boundaries and the face's own
// ascent/descent band, so there is no pad left to author. The marker flags took
// their own pads to row 5's constants before that.)

// The outer (window-edge) gap between each strip's edge-most lane and the
// window edge, and the waveform-side gap between the innermost lane and the
// waveform. Stays a compile-time zero under scaling — zero is
// scale-invariant — so the lanes pack tight against the window edges and the
// waveform. The constant survives so the gap reappears structurally if it is
// ever un-zeroed (the strip lane-stack geometry in main.cpp carries it).
// (IT IS READ UNSCALED at the lane stack, which is exact while it is 0.0 and
// would be a latent bug the moment it is not: an authored length must go
// through `scaled_px`. Recorded 2026-09-02 rather than pre-emptively wrapped —
// the wrapper would be dead arithmetic on a zero, and un-zeroing the constant
// is the edit that has to add it. `kRowGapPx` below carries the same note.)
constexpr double kFlagBottomLiftPx = 0.0;

// Fixed-pixel mirrored strip lane grid. G is the single tunable inter-lane gap
// between each adjacent lane pair within a strip. One named constant, one
// place to change it. Now 0 — the lanes of each strip touch, and the
// waveform-side and outer (window-edge) gaps (both kFlagBottomLiftPx, also 0)
// vanish, so lanes and strips pack tight against each other and the window
// edges. Stays a compile-time zero under scaling — zero is
// scale-invariant.
// Read unscaled like its sibling above, exact at 0.0 and owing a `scaled_px`
// the moment it is un-zeroed (the note at kFlagBottomLiftPx).
constexpr double kRowGapPx = 0.0;

// Defensive window floor (a conservative 640x480 minimum), enforced by an
// internal clamp in the geometry helpers so the waveform arithmetic is always
// valid regardless of what size the window reports. Not sized to fit content — the longest dialogue
// may clip at the floor, which is acceptable (nobody authors at 640x480).
constexpr int kMinWindowWidthPx  = 640;
constexpr int kMinWindowHeightPx = 480;

// (THE CAPTION — the window's own title bar, the top strip's lane 0 from
// 2026-10-05: Windows 2000's caption until 2026-10-23, then the hi-fi skin's
// flat 40-device-px lane with the quaver, "<piece> - Warptempo" and three
// procedural glyphs, 0 rows on the tablet from 2026-10-24's morning — IS
// REMOVED 2026-10-24 with its lane, its painter, its buttons and its light
// line, architect: "release it to the window manager as we had in the
// beginning of this project before we introduced the client-side
// decoration". THE APP PAINTS NO WINDOW CHROME: the tablet's full-screen
// window has none, and the program starts at the
// surface's first row with the logo lane below. Its two lengths the logo
// lane and the cards read are the logo lane's own below. Git history.)

// THE LOGO LANE — ITS OWN LANE ON TOP OF THE BAND (architect 2026-10-24,
// the approved logo frame; its height the hi-fi's again 2026-11-03, the
// retrofit, and its seats the text round's of that morning: "logo should be
// centered in logo lane"; "all other elements centered vertically"): with
// the tablet's caption gone, THE LOGO STANDS IN A LANE AT THE SURFACE'S TOP,
// OVER THE BAND'S KEYS. THE LANE IS kLogoLaneW, 36 W through scaled_px (72
// device px on the tablet at 200 %, his P72 of 2026-10-24; 36 at 100 %),
// whatever the key's height (logo_lane_h_px). EVERY ITEM'S INK IS
// CENTERED ON THE LANE BY ITS OWN INK (architect 2026-11-03, on mock dec0b:
// "everything is set by the logo"; the lane's spare split with its odd row
// below, the floor of the half) — THE NOTE EXCEPTED BY ONE W, lifted
// kLogoNoteLiftW above its centered seat, AND THE BATTERY'S CELL, its
// top row on the runs' ink top row (both below): the note's ink box, the brand's ink rows
// (the "W"'s top to the "p"'s foot as painted: gui_font_ink_band of
// kLogoBrand in the Bold face), the legend's runs' ink rows (the alphabet's,
// kLogoLegendInkSet, in the Body face as painted: each run's baseline its
// ink's top row plus its rows above the baseline), THE DRAWN PIPE ON THOSE
// SAME ROWS (below) and the battery cell. On the tablet at 200 % (the brand
// 17 + 5 rows at Bold Title 12 W = 24 device px, the runs 15 + 0 at Body
// 11 W = 22 device px):
// the brand on lane rows 25–46 (its cap rows 25–41, the "p" to 46), the
// runs 28–42, the pipe 28–42 with them, the battery 28–41 (its top on the
// runs'), the two lamps 28–43 (their top on the runs' too), the note 25–42
// (its centered 27–44 lifted one W). THE LANE'S GROUND
// IS THE BAND'S FLAT GROUND, nothing else on it — no line, no caption tone —
// and it is INERT CHROME like the strips' pads: no hit, no tooltip, no
// gesture (the band's ground falls through to nothing, input_pointer.cpp) —
// THE ONE EXCEPTION the struck clip lamp's tap (THE TWO LAMPS' STRIKE
// below, 2026-11-05 night).
// paint_handler.cpp's paint_logo_lane paints it, every length below
// scaled_px, rounded at the element, EVERYTHING ON IT IN THE TEXT TONE
// `text` WITH NO SHADOW (the flat look, architect 2026-11-01 "no shadow
// anywhere", M13's tone 2026-11-02; materials.h's head) — the brand and the
// legend's runs, the note, the pipe, the glyph — BUT THE TWO LAMPS, which
// wear their own faces (THE TWO LAMPS below; logo_legend_extent,
// paint_handler.cpp, the legend's damage rect, the lamps inside it):
//   ON THE LEFT, THE NOTE: Breeze's MusicNote16th (architect 2026-11-01,
//     M7: Breeze only — the BPM key's own drawing) on the ink-box road
//     (icons.h's THE INK BOX), the ink box's larger side kLogoNoteInkW =
//     9 W (18 device px at 200 %; architect 2026-11-04 night, his pick g1 of
//     tmp/s38: "1w down in size" — the 10 W before it replaced; 12 x 18
//     device px on the tablet),
//     ITS FIRST INK
//     COLUMN ON THE WELL'S FIRST COLUMN (well_columns — the first key's
//     face's first column: "the glyph flush with the icons"). ITS ROWS ONE W
//     ABOVE ITS CENTERED SEAT (architect 2026-11-04 night, mock e2 of
//     tmp/s36): kLogoNoteLiftW = 1 (one W, scaled_px, rounded at the
//     element) subtracted from logo_item_y's row for the note alone, so at
//     200 % its 18-row ink box stands on rows 25..42, not 27..44 — his
//     reason: the centered note "seems to hang lower than the text that
//     follows it" (on mocks e1 / e2 of tmp/s36, the 20-px note then: its
//     centered box reached 4 rows below the brand's cap foot, row 41; the
//     brand's cap 25..41, its "p" to 46); the box's bottom on the brand's
//     baseline (mock e1, 4 px up) was "too high". The brand and the legend's
//     runs and pipe keep the centered seat. THEN THE BRAND, "Warptempo"
//     (kLogoBrand) — a CONSTANT, never the project's name (his: "most hi-fi
//     equipment … just had a stylized brand name") — in Liberation Sans
//     Bold at the TITLE size (gui_font.h's table, 12 W, 24 device px at
//     200 %), the text tone `text`, its first ink kLogoBrandGapW = 7 W
//     (14 device px; architect 2026-11-04 night, his pick g2 of tmp/s38,
//     one W less than the 8 W of mocks e1 / e2 of tmp/s36 the same night,
//     after the note went to 9 W; the e1 / e2 reason, "it needs more
//     padding from the text", still the gap's reason against the 4 W
//     before, mock v4) after the note's last ink column
//     (text_shape::ink_left_px): at 200 % the note's ink on columns 24..35,
//     the brand's first ink column 50.
//     (The gap is the retired caption's title gap. The cards'
//     glyph-seat-to-text gap read it too, a glyph then words, until the
//     gap left 8; the architect kept the cards' 8 the same night ("keep the
//     cards spacing"), now its own kNotificationGlyphGapW,
//     notifications.h.)
//   ON THE RIGHT, THE LEGEND "[lamp][lamp] 92% <glyph> | 2:12 PM"
//     (gui_battery.h's composer for the runs; the tick's refresh_logo_legend,
//     main.cpp) in the BODY face (11 W, 22 device px, the Bold), the text
//     tone, LAID RIGHT TO LEFT FROM THE WELL'S LAST COLUMN (the last key's
//     face's last column: "the clock flush with the right edge of the
//     buttons"): the clock run "2:12 PM" with its LAST INK on that column;
//     THE DRAWN PIPE (below) kLogoPipeAirW of ground left of the clock's
//     first ink; kLogoPipeAirW of ground again; THE GLYPH CELL; one space
//     advance and one W; the percent run "92%" with its last ink there, so
//     "100%" grows leftward, away from the clock; kLogoLampGapW of ground;
//     THE CLIP LAMP, one W of ground, THE LIMITER LAMP (THE TWO LAMPS,
//     below). THE PIPE IS DRAWN (architect 2026-11-05, REOPENING 2026-10-24's
//     "I don't think we should draw our own pipe" ON HIS WORD — "the pipe
//     at bold reads oddly"; his pick p1 of tmp/s46): A HARD BLOCK in the
//     text tone `text`, kLogoPipeW = 1 W wide (2 device px on the tablet),
//     ON THE LEGEND RUNS' INK ROWS — the alphabet's band the layout centers
//     the runs on (on the tablet rows 28..42, the digits' own 15 rows), not
//     the Bold glyph's 21 rows and not one W up (p2..p5 declined) — filled
//     in whole device-px cells with no antialiasing (paint_logo_lane; the
//     face's "|" glyph, lifted onto the digits' band wherever else the face
//     shows one, is no longer the legend's: gui_font.h's kGuiLiftedSigns
//     keeps the lift for the labels' grammar). FIVE W OF GROUND ON EACH SIDE
//     OF THE BLOCK, INK TO INK (kLogoPipeAirW = 5, 10 device px; his "pipe
//     air good"): the block's right column + 1 + 5 W is the clock run's
//     first ink column, and the battery cell's right column + 1 + 5 W is
//     the block's first column. HIS REASON FOR THE NUMBER: the face's space
//     advance (3 W at Body 11) plus 2 W of air on the pipe side of the cell
//     — "the cell's pipe-side number" — and the same on the clock side; it
//     is authored as the one constant 5 W, which EQUALS space + 2 W. THE
//     CELL'S OTHER SIDE IS UNCHANGED — one space advance and kLogoGlyphAirW
//     (1 W) between the cell and the percent run's last ink — so THE CELL'S
//     AIR IS ASYMMETRIC BY HIS RULING: 5 W toward the pipe, space + 1 W
//     toward the digits (his c6 of tmp/s46, "just reads right"; confirmed
//     on the numbers, a stated asymmetry). THE BATTERY'S AIR (architect
//     2026-11-04 night, his pick a3 on the mocks of tmp/s36, "add 2px"):
//     kLogoGlyphAirW = 1 (one W, scaled_px, rounded at the element like
//     kLogoBrandGapW) of ground added to the space advance ON THE DIGIT
//     SIDE OF THE CELL ALONE since 2026-11-05 (both sides until the drawn
//     pipe took the pipe side); the clock, the pipe and the drawing itself
//     do not move with the percent. FLUSH IS THE INK, the chokepoint's own
//     measure (text_shape::ink_left_px / ink_right_px: cairo's glyph
//     extents of the run as painted): the clock's first ink column is the
//     floor of its first ink, the percent's likewise; the block and the
//     cell stand on whole columns. THE RUNS' INK ROWS ARE THE ALPHABET'S,
//     kLogoLegendInkSet (the composer's every character: the digits, "%",
//     ":", the space and "AMP"), so the seat never moves with what the
//     clock reads; the pipe's rows are the same band.
//   THE TWO LAMPS BY THE BATTERY (architect 2026-11-05, sessions 46 / 47,
//     his picks c6 / d1 / d6 / g5 of tmp/s46; THE SIZE, THE ROWS AND THE GAP
//     2026-11-06, session 63, his pick m5 / m6 of tmp/s63): TWO SQUARES,
//     kLogoLampW = 8 W each (16 x 16 device px), in the lane LEFT OF THE
//     PERCENT RUN, inert chrome like everything on the lane — no hit, no
//     tooltip, no gesture — but the struck clip lamp's tap (THE STRIKE
//     below).
//     THE SEAT: by the battery, "ableton places it topmost rightmost
//     grouping" (Live's control bar's CPU and MIDI boxes), not the band and
//     not a stack beside the NOISE case (d2 declined). THEIR ROWS: the top row
//     ON THE LEGEND RUNS' INK TOP ROW, the glyph cell's own top row
//     (logo_item_y of the runs' ink rows; the lamps carry no row offset of
//     their own, as the cell carries none — no constant; on the tablet rows
//     28..43 beside the cell's 28..41 and the runs' 28..42) — his pick
//     m5 / m6; an alignment, not a length. THEIR COLUMNS: THE CLIP LAMP on
//     the right, its last column + 1 + kLogoLampGapW (4 W, 8 device px of
//     ground) = the percent run's first ink column (with no percent run —
//     no battery, or no level read — the cell's first column takes the
//     percent's place);
//     THE LIMITER LAMP left of it, ONE W OF GROUND BETWEEN (the key gap,
//     key_gap_px, kKeyGapW's number) — "LIMITER LEFT, CLIP RIGHT, mirroring
//     the level sliders" (d1 / d6; on the tablet, the percent's first ink
//     on column 2090, the pair stands on columns 2048..2063 / 2066..2081).
//     They move with the percent run's width as the cell does; the clock
//     and the pipe never move. THEIR
//     FACES: AT REST `key_face` (Default 363636) — a dark square, Live's
//     indicator box; LIT, THE LIMITER `card_ground` (Alert E76942, "a
//     warning", the cards' own orange) and THE CLIP `clip_lamp`
//     (StandardVuMeter / Maximum FF0A0A, "a fault", Live's meter clip box)
//     — "orange for limiter and red for clipper" (d6); a key's lit look
//     REFUSED: "clip indicator is not a button". No outline, no inset, nothing printed on them, whole cells,
//     no antialiasing. Both squares are part of logo_legend_layout (two
//     rects) and of the extent — the first ink column is the limiter's
//     first column, the lamps standing left of everything — so
//     refresh_logo_legend's damage covers them whenever the percent moves.
//     THE TRIGGERS AND THE HOLD (architect 2026-11-05 night, session 49:
//     "a hold on both lamps, lamp_hold_ms a tuning key with a Settings row
//     (default 100)" — REVERSING the same night's "no hold": the tick is
//     the platform's 5 ms timer (GuiPlatform::detect_refresh_rate_ms) while
//     only the PRESENT waits for the 90 Hz vsync, an 11.1 ms period, so a
//     lamp lit for one 5 ms tick can be cleared before any present and
//     never be painted; the clip clamps a RUN of blocks and its lamp spans
//     ticks, but the limiter's engaged frames are POINTS, and its one-tick
//     lamp was missed on his glass): EACH LAMP IS LIT FROM ITS LAST EVENT FOR
//     AT LEAST THE HOLD — the `lamp_hold_ms` device key, whole ms in
//     [20, 1000], default 100, read live at every tick (device_config.h's
//     grammar) — a tick that sees the lamp's event stamping now + the hold
//     (AppState::clip_lamp_lit_until_ms, limiter_lamp_lit_until_ms), the
//     lamp lit on that tick and on every tick before the stamp: an event
//     re-arms it (a run of clamping blocks keeps the lamp lit until the hold
//     after its last block), the dark edge lands on the first tick at or
//     past the stamp, and 100 ms is about nine presents at 90 Hz. NO TIMER OF
//     ITS OWN: the tick is the clock. The bits on AppState
//     (clip_lamp_lit, limiter_lamp_lit) are written only by the tick's
//     refresh_logo_lamps (main.cpp, beside refresh_logo_legend, on EVERY
//     tick ahead of the early returns), each edge damaging its own square
//     through the platform's damage road, nothing damaged while a bit
//     holds. THE CLIP LAMP — any view, anything the device plays (the
//     sum is the device's): the audio thread's clip (playback_clip_block)
//     reports whether it clamped a sample of the block and the callback
//     counts the clamped blocks (GuiPlaybackState::clipped_blocks); the
//     event is the count having moved since the tick before. THE
//     LIMITER LAMP — THE TARGET BUFFER AND THE RENDER PLAYER'S ITEMS
//     (architect 2026-11-05, reopening the player's lamp: an item's own
//     .fingerprint carries its limiter line since the sidecar's layout 2,
//     render_cache.h; the source view and the score's soundtrack dark; a
//     render made before that layout, or whose record is missing, lights
//     nothing until it is rendered again): with the engine playing and a
//     buffer bound that carries a list, the event is the
//     limiter's engaged frames (AppState::bound_limiter_frames, the bind's
//     own snapshot) holding a frame in the span the playhead advanced since
//     the tick before, in buffer-local frames — [prev, now), a binary
//     search; a span that runs backward (a wrap, a scrub, a restart) tests
//     nothing, the first tick of a play seeds and tests nothing, not playing
//     tests nothing (a lamp lit at the stop keeps its hold out).
//     THE STRIKE (architect 2026-11-05 night, session 49: "(6) YES — the
//     strike + the tap"; its paint session 52, his pick m1 of tmp/s52: "the
//     whole dot should turn that color. The whole square"): WHILE THE DEVICE
//     CANNOT SOUND, THE CLIP LAMP'S WHOLE SQUARE IN `key_dim_ink` (the
//     disabled key's glyph tone, Default 5D5D5D — "a lighter gray like a
//     disabled tone", no role of its own). THE STATE: a third bit, AppState::clip_lamp_struck,
//     written by refresh_logo_lamps alone on every tick once a piece is open
//     — GuiPlayback::device_unavailable(), the error callback's dead latch
//     (a headphone pulled, a Bluetooth route dropped) or no stream standing
//     (a device that never came up, a refused reopen): "nothing will
//     sound"; a pure read, no timer and no retry (the no-auto-recover rule);
//     its edges damage the clip lamp's square like the lit bits'. Before a
//     piece is open no engine exists and nothing is struck (the open's held
//     frame wears no strike; refresh_logo_lamps). The bit is INDEPENDENT of
//     the clip bit, but THE PAINT WINS OVER IT (architect 2026-11-05 night,
//     session 52, "agree": the lamp cannot sound): THE FACE IS THE STRIKE —
//     the square's one fill picked three ways, struck `key_dim_ink`, else
//     lit `clip_lamp`, else rest `key_face`, nothing painted over it, whole
//     device-px cells, no antialiasing (paint_logo_lane's lamps block,
//     paint_handler.cpp); a lit red under the hold coinciding with a drop
//     shows the struck square. THE TAP — THE LANE'S ONE PRESS
//     TARGET, a recorded exception to "clip indicator is not a button" on
//     his word ("not a departure": Live's CPU indicator resets on a click):
//     WHILE STRUCK, AND ONLY THEN, a press on the clip lamp's square (the
//     very rect the tick damages, logo_lamp_rects(...).clip, so the hit and
//     the picture agree) arms AppState::ChromePress's Lamp kind
//     (arm_clip_lamp_press, the band claim's third half, input_pointer.cpp);
//     no press look; a lift on the square with the strike still standing
//     asks the launch gates' device question,
//     GuiPlayback::ensure_device_available_for_play — the dead stream closed
//     (the noise voice's sequence reset first), reopened at the new route's
//     rate and STARTED, so the bed resumes through the running callback and
//     NOTHING PLAYS — and cards kPlaybackDeviceUnavailableCard on a false
//     answer, as the gates do; a true answer raises nothing, the next tick
//     clearing the strike. A lift off the square does nothing (the keys'
//     slide-off feint). A device that never came up is opened by no press
//     (ensure answers false at once, init's failure logged), so its tap
//     cards the sentence: the one road, the truthful report. UNDER AN
//     OVERLAY THE TAP ANSWERS TOO (architect 2026-11-05 night: "we should
//     make it respond under the overlay"): the render player's, the
//     picker family's and the score viewer's veils admit the lamp's arm
//     beside their opener's, the same act as on the main page; THE MODALS
//     (a prompt, a dialog editor, the progress modal) veil it as they veil
//     every arm (arm_clip_lamp_press). Unstruck, the square claims nothing.
//   THE GLYPH CELL, ONE SIZE WHATEVER IT HOLDS — the clock never moves (his:
//     "the glyph always there taking up space so that the clock doesn't
//     move"): THE TWO DRAWINGS are Breeze's (2026-11-02, M7: Breeze only) —
//     Battery000Charging (CHARGING: any power source attached, 100 %
//     included, and a host with no battery; GuiLogoGlyph's rule) and
//     Battery000 (DISCHARGING, or the plug state unknown) — ONE OUTLINE, a
//     horizontal battery with its nub to the right, the charging one
//     carrying a plug inside it, so their ink boxes are one box. THE CELL IS
//     THAT BOX on the ink-box road (icons.h's THE INK BOX) at ONE LENGTH for
//     both, kLogoGlyphInkW = 11 W its larger side (22 device px at 200 %;
//     architect 2026-11-04 night, mocks e1 / e2 of tmp/s36: "battery good
//     at 22") — the outline is wider than tall (20 x 12 user units,
//     pixel-aligned in its 22-unit viewBox), so on the tablet the fit's
//     zoom is 1.1 and THE CELL IS 22 WIDE AND 14 ROWS TALL on the 72-row
//     lane, both drawings alike (the raster's
//     arithmetic: the outline's rows 5.5 .. 18.7, its first and last rows
//     partial, alpha 127 and 192; measured with resvg's renderer). THE
//     CELL'S TOP ROW IS THE LEGEND RUNS' INK TOP ROW (architect 2026-11-04
//     night, his pick f2 of tmp/s38; a DELIBERATE ASYMMETRY to the lane's
//     "every item centered" rule, stated as the alignment it is, not as a
//     length): the cell starts on the row the percent and clock digits'
//     ink starts on (logo_item_y of the runs' ink rows), so at 200 % the
//     cell stands on rows 28..41 beside the digits' 15 ink rows 28..42 —
//     both start on row 28, the cell ending one row above the digits. His
//     reason: the cell centered on the lane (rows 29..42, half a row under
//     the digits' center by the floors) read "too low"; two rows up (27..40,
//     mock f1) not taken. The air either side of the cell the space advance
//     and one W, above; the cell the larger of the two drawings' ink boxes
//     on each axis so no state can move it, each drawing centered in it
//     (the floor of the spare). Painted in the text tone.
//   (Liberation's own arrows ↑ / ↓ as the glyphs and "nothing" as the
//   discharging glyph were judged on the same sheets and not taken,
//   docs/engineering/closed_questions.md; the drawn pipe, declined there
//   2026-10-24, is TAKEN since 2026-11-05 on his word, above.)
inline constexpr double kLogoLaneW           = 36.0;
inline constexpr double kLogoNoteInkW        = 9.0;
inline constexpr double kLogoNoteLiftW       = 1.0;
inline constexpr double kLogoBrandGapW       = 7.0;
inline constexpr double kLogoGlyphInkW       = 11.0;
inline constexpr double kLogoGlyphAirW       = 1.0;   // the digit side alone since 2026-11-05
inline constexpr double kLogoPipeW           = 1.0;   // the drawn pipe's width
inline constexpr double kLogoPipeAirW        = 5.0;   // ground on each side of it, ink to ink (= space + 2 W)
inline constexpr double kLogoLampW           = 8.0;   // each lamp's square
inline constexpr double kLogoLampGapW        = 4.0;   // the clip lamp to the percent's first ink
inline constexpr const char* kLogoBrand = "Warptempo";
inline constexpr const char* kLogoLegendInkSet = "0123456789%: AMP";
inline int logo_lane_h_px() {
    return scaled_px(kLogoLaneW, 1);
}

// (THE WINDOW BORDER — THE SIZING FRAME'S BAND round a restored laptop
// window, window_frame_px / paint_window_sizing_frame, Windows 95's raised
// frame from 2026-10-05 and the hi-fi skin's transparent band under one
// device px of the ring's light tone from 2026-10-23 — IS REMOVED 2026-10-24
// with the caption. Git history.)
// (THE MENU ROW — the top strip's lane 1 under the caption from 2026-08 to
// 2026-10-21, Windows' plain menu bar carrying File, Edit and Settings, a
// face row over its 19-W content — IS REMOVED, architect 2026-10-21: "I'd
// like to see v1 without the file/edit... row … keep the line above the top
// icon row". Its lane, its painter, its three pull-downs' titles and their
// Alt access keys went with it; the line that stood under it, the program
// frame's top row, stood directly under the caption until 2026-10-22.
// Its record is git history.)

// (THE TOOLBAR ROW IS DELETED — 2026-08-12, the grand relayout's roster
// commit: the labeled Save / Undo / Redo / Render lane, row 2 of the redesign
// since 2026-07-31, dissolved into the ICON ROW as its first group of four
// glyph buttons, so the top strip lost the lane's 44 content + 1px border.
// kToolbarRowHeightPx / kToolbarBorderPx and their accessors went with it;
// the row's button box and label pads survive as the MODAL DIALOG BUTTONS'
// own numbers — the chrome spec's push_button_* fields since 2026-10-07
// (kModalBtnBoxPx and friends, paint_handler.cpp, 23 and 7 / 7 Windows px
// from 2026-10-02; 32 and 9 / 10 laptop px before) — which used to read row
// 2's. The row's crop record is git history.)

// (THE DIALOG FIELD — kModalFieldHeightPx 23 W and kModalFieldPadXPx 5 W,
// Windows' 14-dialog-unit edit box with its text 5 W in from its outer edge,
// the one shape of every text field of the modal dialogs, the choice
// editor's combo at its seat, from 2026-10-02 (the laptop pixel's 31 and 7
// re-authored) — IS GONE with the field, architect 2026-10-14: a dialog
// editor's line stands in the modal family's box, its text on the box's own
// text seat (modal_box.h's modal_box_text_seat_px, M8b: 9 W in from the
// pane's edges, 18 device px at 200 %, from the pane since 2026-11-04).
// THE TIME FIELDS were never this field: row 8's clock and the render
// player's position and length are Cool Edit's dark field, its height and
// its pad program_spec.h's own, the pad 5 W since 2026-10-10, equal to this
// field's then.)

// (THE RELIEF LINE — relief_line_px, ONE WINDOWS PX, scaled, the width of
// every line of Windows' DrawEdge grammar from 2026-10-02 — RETIRED
// 2026-10-23 with the hairline sweep; every chrome line is hairline_px
// below.)
// THE UNIT IS ONE W (its one statement at scaled_px above; the rebase to
// 200 %, architect 2026-11-05): every `k…W` constant a whole number of W,
// the face sizes too (gui_font.h's table), every length through scaled_px,
// rounded at the element; on the tablet one W is 2 device px, so the keys
// keep their pitch (72 device px) and the icons their seats.
// THE LINE RULE (architect 2026-11-04, the unit, reversing the hi-fi skin's
// "one device px at every gui_scale" of 2026-10-23): THE LINE IS ONE W,
// ROUNDED AT THE ELEMENT, FLOORED AT ONE DEVICE PX — 2 on the tablet at
// 200 %, 1 at 100 % — the chokepoint beside scaled_px for every LINE the
// skin draws:
// THE WELL'S ROWS OF GROUND (the trim bar's ground line and the row above
// the bar, the ruler's and the marker lane's foot rows, the stereo
// divider's row and the well's foot), the trim bar's fill's top part
// (trim_bar_fill_h_px), the modal box's outer rows and columns of ground
// round its pane and the lists' air inside it (modal_box.h), the cards' and
// the tooltip's border and the scrub's channel, the dialog editors' and the
// flag editor's carets (paint_modal_dialog, render.cpp's flag editor) — AND
// EVERY LINE ON THE CANVAS, waveform_line_px below being this one (the
// grid, the center lines, the stems, the cursor, the scanner, the anchor,
// the lead-in ring, the nudged pairs' runs, the ruler's ticks; the lit
// core an area of the plate's, render.h's THE CORE). The
// ground between two keys is its own length of the same number
// (key_gap_px, the case block below). Everything else keeps scaled_px,
// AREAS: the keys, the lanes' heights, the pads and gaps, the
// flags' triangles and the cue boxes. ARTWORK IS AN AREA IN THIS SENSE
// (2026-10-23): a drawing's stroke weight is part of the drawing, so it
// scales with the drawing — the icons and the faces' strokes, never a line.
// AN EVEN LINE HAS NO CENTER PIXEL: A VERTICAL ONE STRADDLES ITS FRAME, the
// pixel boundary (architect 2026-11-06), a horizontal one keeps its top row
// the frame's — the rule's one statement at waveform_line_px below, its
// paint waveform_line_cols.
inline int hairline_px() { return scaled_px(1.0, 1); }

// (THE PROGRAM'S FRAME — Cool Edit's frame window round the program, one
// sunken line in the chrome's Shadow and Hilight, mitred, its top row a lane
// between the menu row and the band, its bottom row a lane under the border
// under row 8 and every program lane one line in from each side, 2026-10-10,
// read off his captures of Cool Edit Pro 2.1; REDUCED TO ITS TOP ROW
// 2026-10-21, "every row grows 1 W px each side", one Shadow line across
// the window directly under the caption — IS REMOVED, architect 2026-10-22:
// the band stood directly under the caption (at the surface's top since
// 2026-10-24), and the canvas took the row. Every program
// lane spans the window (strip_row_rect) and row 8 is the window's lowest
// lane.)
// WHAT STANDS OVER THE PROGRAM (architect 2026-10-14; the simple layout
// 2026-10-22): every modal — the render player, the picker, a prompt, a
// dialog editor — is the modal family's box and controls row (modal_box.h),
// the box (one flat pane, no ring) from the trim lane's first row to the
// well's foot and the controls row on row 8's rows; the on-screen keyboard's
// band stands on the window's foot across the lanes (onscreen_keyboard.h's
// placement block), a dialog editor's controls row on it.

// THE PROGRAM'S BAND AND ITS CASE — THE ICON ROW, the top strip's lane 0,
// at the surface's top since 2026-10-24 (architect 2026-10-09: the program is Cool
// Edit from the toolbar down, the same under every chrome; program_spec.h's
// head, where every length below is authored and its source named). The
// roster's thirty-one view/mode/action buttons stand in it (re-counted
// 2026-10-29, the Restrict lamp) — the
// kIconRowButtons, kIconRowHistoryOpener, kIconRowViewPairs and history
// stand-in tables are the count's one authority (paint_handler.cpp);
// THE CASE IS ALSO ROW 8's AND THE RENDER PLAYER'S: every reader
// takes the accessors below, so the three rows cannot drift apart.
//
// THE KEY'S FACE IS THE KEY (architect 2026-11-04, the unit, his "1 agree"
// to the hi-fi's face and "grow the box instead of adding a border"; the
// hi-fi key metrics of 2026-11-03's retrofit, the square key of 2026-11-02
// reversed; mock v4 that night: the face 70 x 34 device px, the gap 2, so
// every key keeps its pitch of 72 device px and its seat): THE `key_width`
// TUNING KEY'S W WIDE (key_width_w, its seat above; 35 its default;
// architect 2026-11-06, session 78, a tuning key until he rules the number)
// by THE `key_height` TUNING KEY'S W TALL (key_height_w, its seat above; 17
// its default), both through scaled_px, rounded at the element: at the
// defaults, 70 x 34 device px on the tablet at 200 %, 35 x 17 at 100 %, the
// group pitch 72 device px. THE KEY IS NO LONGER SQUARE. THE PAINTED FACE
// IS THE WHOLE KEY — NO OUTLINE, NO INSET — and ITS NEIGHBOR IN A GROUP
// STANDS ONE W OF GROUND AWAY (kKeyGapW, key_gap_px below: 2 device px at
// 200 %, 1 at 100 %; materials.h's THE
// FACE AND ITS GAP and key_group_pitch_px, the face and the gap). The face
// is the key's hit box, its glyph's seat and its legend's (below).
// Every key carries a Breeze glyph centered on its face (paint_key_glyph,
// paint_handler.cpp; icons.h's THE KEYS), its ink box's larger side
// FOLLOWING THE KEY'S HEIGHT, 9 * key_height / 17 W — 9 W, 18 device px on
// the tablet, at the default 17 (architect 2026-11-04 night, his pick d1 of
// tmp/s36, every key glyph grown one W; 16 / 34 device px before it, 16 /
// 33 off mocks s0..s4 2026-11-03), one size for every key — and UNDER IT, in the
// strip's bottom pad, ITS PRINTED LEGEND (architect 2026-11-03, the
// retrofit: the Legend face, 7 W Regular since 2026-11-04 night, uppercase, the
// rule at paint_handler.cpp's key_legend_seat).
//
// THE BAND (architect 2026-10-22, the simple layout; the hi-fi's again
// since 2026-11-03, the retrofit): THE LOGO LANE (logo_lane_h_px, its block
// above), the case, THE STRIP PAD, on the flat ground — no line on any row,
// the keys' legends in the bottom pad: at key_height 17, 36 + 17 + 19 = 72
// W, 144 device rows on the tablet at 200 % (the faces on rows 72 .. 105).
// Its runs are paint_icon_row's walk (icon_row_seats: the faces one key gap
// apart inside a run, the run gap of ground between two runs' faces,
// program_run_gap_px). The hit target is the painted face (point_on_key,
// materials.h); the ground round the faces, between runs and in the pad
// under them, the legends' included, is inert for input.
//
// THESE NUMBERS LIVE HERE rather than beside the row's walk because the
// case is not one row's: paint_handler.cpp's three rows read it. THE CARD AND THE FOLDER OVERLAY'S LIST
// ROWS ARE NOT THE CASE: a card's glyph stands at its own pad and
// ratio (notifications.h's kNotificationPadW and
// kNotificationGlyphRatio, 2026-10-24), and a list row's at the painter's
// own constants (paint_handler.cpp's kListGlyph*, 2026-10-23).
// (THE PROGRAM RING'S LINE SLOT — program_line_px, one W, the area the view
// bar's and the ruler's lines stood on, 2026-10-23 — RETIRED with the flat
// well, 2026-11-02: the well's lines and rows of ground are the hairline
// itself, the well's lanes block below.)
// THE FACE'S WIDTH, the `key_width` tuning key's live value, and ITS
// HEIGHT, the `key_height` tuning key's (their seats above; the case
// paragraph): scaled_px, floored at three device px so a key never shrinks
// to a sliver at a tiny scale. icon_case_w_px is THE ONE READER of the
// width: every key surface's walk, seat and hit box takes it (or
// key_group_pitch_px, materials.h, built on it).
inline int icon_case_w_px() {
    return scaled_px(key_width_w(), 3);
}
inline int icon_case_h_px() {
    return scaled_px(key_height_w(), 3);
}
// THE GROUND BETWEEN TWO KEYS OF A GROUP (the case paragraph; architect
// 2026-11-04 night): kKeyGapW, ONE W through scaled_px — 2 device px on
// the tablet at 200 %, 1 at 100 % — FLOORED AT ONE DEVICE PX so two faces
// stand apart at every scale. Every key surface's gap: the
// band's, row 8's and the controls rows' groups (materials.h's
// key_group_pitch_px) and the on-screen keyboard's (onscreen_keyboard.h's
// key_gap_px, architect 2026-11-04: "the same face and gap on every key
// surface").
inline constexpr double kKeyGapW = 1.0;
inline int key_gap_px() {
    return scaled_px(kKeyGapW, 1);
}
// THE STRIP PAD (architect 2026-10-22, the simple layout; the hi-fi's pad
// again since 2026-11-03, the retrofit, reversing that morning's "half the
// key"; 19 W since 2026-11-04 night): kStripPadW, 19 W through scaled_px —
// 38 device px on the tablet at 200 %, 19 at 100 % — whatever the key's
// height. Row 8's and the modal controls row's ground above and below
// the case, and the band's ground below it (its upper pad is the logo lane,
// band_case_top_offset_px); the keys' legends stand in its bottom pad
// (paint_handler.cpp's key_legend_seat). FLOORED AT ONE DEVICE PX so a row
// of ground stands there at a scale that would round the pad to nothing.
inline constexpr double kStripPadW = 19.0;
inline int strip_pad_px() {
    return scaled_px(kStripPadW, 1);
}
// THE CASE'S TOP, AS AN OFFSET FROM ROW 8's TOP and the modal controls
// row's: the strip pad, the one expression every reader of those strips'
// case seat takes (the row-8 and controls-row seats, hit rects, tooltips).
inline int icon_case_top_offset_px() {
    return strip_pad_px();
}
// THE BAND'S CASE TOP, AS AN OFFSET FROM THE BAND'S OWN TOP: the logo lane
// (2026-10-24; the hi-fi's again 2026-11-03), read by the band's seats alone
// (icon_row_seats, paint_handler.cpp, which every band reader takes).
inline int band_case_top_offset_px() {
    return logo_lane_h_px();
}
// THE STRIP — row 8's: the pad, the case, the pad (19 + 17 + 19 = 55 W,
// 110 device rows on the tablet at key_height 17).
inline int strip_h_px() {
    return strip_pad_px() + icon_case_h_px() + strip_pad_px();
}
// THE BAND: the logo lane, the case, the strip pad (2026-10-24; again
// 2026-11-03).
inline int icon_row_h_px() {
    return logo_lane_h_px() + icon_case_h_px() + strip_pad_px();
}
// THE GROUND BETWEEN TWO RUNS (architect 2026-10-22, the simple layout; the
// hi-fi's gap again since 2026-11-03, the retrofit, reversing that
// morning's "the strip pad"; 22 W since 2026-11-04 night; A TUNING KEY since
// 2026-11-07, architect session 87, his "agree"): THE `run_gap` TUNING KEY'S
// W (run_gap_w, its seat above; 22 its default) through scaled_px — 44
// device px on the tablet at 200 %, 22 at 100 % at the default —
// between two runs' FACES (the key is
// its face since 2026-11-04), whatever the key's height: the band's runs and
// the one run gap the left runs' clip stands short of the history opener,
// row 8's runs packed right, and the controls rows' groups, the render
// player's and the score viewer's group spaces included (paint_handler.cpp's
// icon_row_seats, row8_lengths, player_row_layout and the score rows).
// program_run_gap_px is THE ONE READER of the key: one key for every gap.
// A TOO-LARGE GAP is the band's overflow rule's, as a too-wide key is (THE
// FIT at paint_icon_row's width math, paint_handler.cpp): on the tablet at
// 200 % and key_width 35 the band holds whole across the key's whole range
// (2200 of the well's 2256 at its 44 W ceiling), a wider key bringing the
// overflow back — the left runs clipped one run gap short of the history
// opener, the third run's keys cut or published empty from its tail, the
// right run whole; row 8's block grows leftward over its state line, and
// the player's and the score viewer's rows take the gap from the scrub's
// channel, which their floor of two thumbs drops whole when too little is
// left.
inline int program_run_gap_px() {
    return scaled_px(run_gap_w());
}

// A BUTTON IS SQUARE-CORNERED (architect 2026-10-02, the Windows-95 design):
// its box is the key's (materials.h's paint_key), and no corner in the
// chrome is rounded.

// -- THE PLAY-SCRUB (architect 2026-10-02; flat 2026-11-01 night, M7) -------
//
// The render player's controls row and the score viewer's (its media face
// and its page road's face alike) carry ONE SCRUB, Live's flat slider
// (architect 2026-11-01 night, M7, "agree both"; M9 A2 for the score viewer):
//   THE CHANNEL — ONE W (scrub_channel_h_px, hairline_px, the line rule:
//     2 device px on the tablet at 200 %, 1 at 100 %, architect 2026-11-04)
//     of materials.h's `well_frame` (Default's 757575), the grammar's frame tone, CENTERED
//     ON THE FACE — the centered elements' tie rule, (face rows − channel
//     rows) / 2 below the face's top, rounded down (scrub_channel_above_px):
//     rows 1384..1385 at 200 % on the keys' face rows 1368..1401 at
//     key_height 17 —
//     across the channel's own columns, from the thumb's center at frame 0
//     to its center at the last frame (at 200 % x 282..1515 in the player),
//     nothing else: no fill, no played extent, nothing that reads the
//     window's focus.
//   THE THUMB — A BLANK FLAT KEY BLOCK (paint_scrub_thumb: the key painter,
//     never lit — it is dragged, not turned on or off, the flat look's
//     ruling, materials.h — wearing the key's press face while the owner's
//     drag holds it by the thumb (the press arc, 2026-11-05), with no
//     glyph) kScrubThumbWidthPx = 12 W
//     wide, 24 device px on the tablet at 200 % (architect 2026-10-23, on the
//     installed player: "too tiny … the thumb itself should be larger"; the
//     8 Windows px of that day times 1.5 at the rebase of 2026-11-05), by THE KEY'S
//     HEIGHT (architect 2026-10-29: "Shrink it to the height of the key.";
//     scrub_thumb_h_px is the key's height, icon_case_h_px — the face is the
//     key since 2026-11-04 — following the `key_height` tuning key live), so
//     it stands on the faces' rows beside it; ITS HIT ZONE ITS PAINTED RECT
//     (render_player_scrub_handle_hit: on screen is as painted).
// The painter is paint_modal_dialog's paint_scrub_item; the press is
// claim_player_scrub_press (the score viewer's its own claim).
// THE THUMB ALWAYS STANDS (architect 2026-10-25, "agreed"): whenever the
// channel has a width, at frame 0 while no item is loaded — the clock's own
// zeros — so the player opens with its thumb at the channel's left end
// rather than a bare channel that grew a thumb at the first Play. A press on
// it or on the channel with no item is consumed and seeks nothing, as
// before (claim_player_scrub_press's frames <= 0 arm).
// THE THUMB IS CENTERED ON THE TROUGH'S END (architect 2026-10-25, "centered
// on the trough's end"): NO INSET — frame 0's column is the channel's first
// column and the last frame's its last (render_player_scrub_x_of), the
// thumb's center the frame's column, so HALF THE THUMB OVERHANGS the channel
// at each end, over the group space beside it (a fader knob at the end of
// its slot: 12 of the 24 device px on the tablet). THE PUBLISHED ITEM is therefore the
// channel widened by that overhang at each end (AppState::
// ModalDialogGeometry::scrub, the press router's outer gate and the
// per-position damage), so a press on the overhanging half takes the thumb
// and a moving thumb's end column is always inside the damage. (The 14-W
// grab box the mapping inset the channel by — half of it at each end, a
// leftover of the pointed Windows thumb — stood 2026-10-02 to 2026-10-25.)
inline constexpr double kScrubThumbWidthPx  = 12.0;
// The thumb's width floors at three device px; its height is the face's,
// one row at least (scrub_thumb_h_px).
inline int scrub_thumb_w_px() { return scaled_px(kScrubThumbWidthPx, 3); }
// THE THUMB ON ITS BOX `box` (scrub_thumb_w_px by scrub_thumb_h_px): the
// flat key, at rest or — `pressed`, the owner's ScrubDrag armed ON THE
// THUMB (RenderPlayer::ScrubDrag::on_thumb, ScoreViewer::ScrubDrag's; a
// press on the track beside it carries the thumb but presses no key) — in
// the key's press face from the grab to the lift (the press arc, architect
// 2026-11-05; materials.h's key_look). The grab and the lift damage the
// track (the scrub claims and releases, input_pointer.cpp).
void paint_scrub_thumb(cairo_t* cr, const GuiRect& box, bool pressed);

// (THE TAB STRIP — Cool Edit's Organizer tabs, "Tab A" and "Tab B", a 21-W
// lane under the band 2026-10-18 to 2026-10-22 — IS REMOVED, architect
// 2026-10-22, the simple layout: the active tab's name stands in row 8 right
// of the clock, paint_bottom_row_buttons_and_clock.)

// THE WELL'S LANES — the TRIM lane, the RULER lane, the MARKER lane and THE
// ROW OF GROUND UNDER IT, stacked in that order directly under the band
// (main.cpp's lane table), the last one's bottom edge the waveform's top with
// no gap (the palette block's well paragraph owns the look; every length is
// derived here at the element: scaled_px for a chrome length authored in
// W, hairline_px for a line — one W, rounded at the element (architect
// 2026-11-04, the unit) — the faces' own metrics for the text's bands).
// The device rows below are the tablet's, 200 %:
//   THE TRIM   a row of ground, the bar's fill (one hairline and the
//              field), the bar's ground line (trim_lane_h_px): 10 W, 20
//              device rows (2 + 2 + 14 + 2).
//   THE RULER  the ruler (ruler_h_px: the pad, the digits' ink, the pad;
//              the ticks overlay its foot) and a row of ground
//              (ruler_lane_h_px): 36 + 2 = 38 device rows (12 + 12 + 12).
//   THE MARKER the time field's height, the editor's box on its whole rows
//              (marker_lane_h_px: the outline, the pad above, the legal
//              set's band, the pad below, the outline, the x band centered):
//              39 device rows (2 + 6 + 22 + 7 + 2); the cue's chip its last
//              rows (cue_chip_rows, 15).
//   THE LINE   the row of ground between the lane and the canvas, one
//              hairline (lane_foot_line_h_px, below): 2 device rows, 1 W.
// THE TRIM BAR'S FIELD, 7 W on the one road (architect 2026-11-01 night,
// M5c / M5h; 7 W since 2026-11-04 night; program_spec.h's view_bar_field_w
// the owner of the number): 14 device px on the tablet at 200 %, 7 at
// 100 %, floor 1.
inline int view_bar_field_h_px() {
    return scaled_px(kProgramSpec.view_bar_field_w, 1);
}
// THE TRIM BAR'S FILL (architect 2026-11-03, mock z2: the two fills,
// render_trim_flags): one hairline and the field, 8 W, 16 device rows on
// the tablet — the bar's rows less its ground line. THE TRIM BAR, the fill
// and its ground line (9 W, 18 device rows), and THE TRIM LANE: the row of ground above
// it (the row the retired program ring stood on, so the modal family's box
// keeps its seat on the lane's first row, modal_box.h, and the cards theirs
// one hairline below, on the bar's first fill row, notification_stack_bound,
// ruled kept 2026-11-03) and the bar (10 W, 20 device rows: rows 144 .. 163
// at key_height 17 on the tablet, the bar 146 .. 163).
inline int trim_bar_fill_h_px() {
    return hairline_px() + view_bar_field_h_px();
}
inline int trim_bar_h_px() {
    return trim_bar_fill_h_px() + hairline_px();
}
inline int trim_lane_h_px() {
    return hairline_px() + trim_bar_h_px();
}
// THE RULER'S TICKS (architect 2026-11-01 night, M5b / M5c / M5h; the major
// 2026-11-03; the width one W since 2026-11-04, the unit; the rows 4 W and
// 1 W since that night): ONE CANVAS LINE WIDE — waveform_line_px, the line
// rule's W, 2 device px on the tablet at 200 %, 1 at 100 % — ON THE TICK'S
// COLUMN (fill_waveform_line's rule: the line straddles the frame), the
// major 4 W and the minor 1 W tall, 8 and 2 device rows on the tablet
// (scaled_px, each floored at one row; the major 10 device rows of the text
// round and the retrofit rounded to the unit). THEY OVERLAY THE RULER'S FOOT AND ADD
// NO HEIGHT (architect 2026-11-03: "the ticks can be adjusted later — they
// don't change height of anything else"): they stand in the ruler's bottom
// pad, under the digits' ink — the major's 4 W in the pad's 6 W (8 rows in
// 12 on the tablet), TWO W OF RULER CLEAR BETWEEN ITS TOP AND THE INK'S
// FOOT (four device rows at 200 %; the minor 1 W; never over the ink).
inline constexpr double kRulerMajorTickRowsW = 4.0;
inline constexpr double kRulerMinorTickRowsW = 1.0;
inline int ruler_major_tick_h_px() {
    return scaled_px(kRulerMajorTickRowsW, 1);
}
inline int ruler_minor_tick_h_px() {
    return scaled_px(kRulerMinorTickRowsW, 1);
}
// THE DIGITS' INK IS CENTERED ON THE RULER (architect 2026-11-03, HIS RULE):
// THE RULER IS THE PAD, THE DIGITS' INK AND THE PAD — kRulerPadW, 6 W (12
// device px on the tablet at 200 %; scaled_px, rounded at the element; his
// pick of 2026-11-03, mock v3, 11 device px, rounded to the unit
// 2026-11-04), above and below
// — the ink the rows the ruler face's digits and marks, kRulerInkSet, stand
// on as painted (gui_font_ink_band, the rows the slight-hinted paint covers:
// 12 above the baseline and 0 below at Bold 16 device px, the tablet's
// Small 8 W (the hinter's rows; mock v4 read 11); the ruler face 8 W since
// 2026-11-04 night) — so 12 + 12 + 12 = 36 device rows on the tablet. THE
// BASELINE the pad and the ink's rows above it under the ruler's top (24
// device rows at 200 %), the ink's top row the pad under it: the painted ink's
// rows centered, 12 / 12 / 12 (the rule of mock dec1a, 2026-11-03: the
// measured ink, never the flat digits' cap, between the two pads).
inline constexpr double kRulerPadW = 6.0;
inline constexpr const char* kRulerInkSet = "0123456789:.";
inline int ruler_ink_above_px() {
    return gui_font_ink_band(gui_font(GuiFace::Small), kRulerInkSet).above;
}
inline int ruler_ink_below_px() {
    return gui_font_ink_band(gui_font(GuiFace::Small), kRulerInkSet).below;
}
inline int ruler_baseline_px() {
    return scaled_px(kRulerPadW, 0) + ruler_ink_above_px();
}
inline int ruler_h_px() {
    return ruler_baseline_px() + ruler_ink_below_px() +
           scaled_px(kRulerPadW, 0);
}
inline int ruler_lane_h_px() {
    return ruler_h_px() + hairline_px();
}
// THE CUE'S TRIANGLE IS DRAWN IN ONE-DEVICE-PX CELLS (program_spec.h's
// head): THE TRIANGLE'S UNIT IS ONE DEVICE PX AT EVERY SCALE, A NUMBER OF
// ITS OWN (architect 2026-10-26: "I think we should make the triangle
// smaller, and that would make colors like the red from Cool Edit less
// problematic" — "Why not simply leave unscaled? That would preserve the
// stem/flag size relation from CEP"; DECOUPLED FROM THE CANVAS'S LINE
// 2026-11-04, the unit — architect: "untouched: the triangle's cell and the
// ruler's head" — when the stems became one W, waveform_line_px): the flag's
// cells stand beside the one-W stem, CENTERED ON THE FRAME'S COLUMN (the
// odd staircase's apex the column itself; the stem's columns col − t / 2 ..
// col − t / 2 + t − 1, straddling the frame, waveform_line_first_col's rule
// since 2026-11-06 — so on the tablet the apex points at the stem's RIGHT
// pixel, the frame's own column: architect 2026-11-06, session 67, "it
// should point at the right column"; earlier that session "you can leave
// the triangle tips as they are - they read fine, and optically i dont see
// them as offcenter"), whatever the gui_scale (THE
// ROW COUNT IS SCALED, cue_triangle_rows
// below). ITS READERS: the triangle's rows and columns and its outline's
// (paint_ce_cue_triangle, the stamp — whole device
// cells since 2026-10-26, Cool Edit's aliased staircase), the extents every
// cull, hit and damage box reads (cue_triangle_half_w_px,
// cue_triangle_reach_right_px, below) — and THE PLAYHEAD'S HEAD, the same
// painter in the ruler, its cell one device px with it (architect
// 2026-10-26 ~10:10: "All triangles unscaled"). THE LABEL'S LENGTHS — its
// lead's gap, its fill's pad, the segment gap and the editor's outline —
// are skin lengths, scaled with the text they place (below).
inline int cue_triangle_unit_px() {
    return 1;
}
// THE TRIANGLE'S ROWS, THE ONE ACCESSOR EVERY READER READS: 5 W through
// scaled_px (10 device rows on the tablet at 200 %), rounded at the element
// like every length off the canvas (architect 2026-10-26 ~13:40, on the row count as a per-device
// number: "why density? why not just simple scale? tablet is only real
// surface. later tablets may have higher dpi, but not lower" — "yes" to the
// planner's proposal; 9 the 8 he tuned on the glass grown with the text
// round's 24, architect 2026-11-03, kept by the retrofit the same day; 10
// device rows, 5 W, since 2026-11-04 night, his pick on mock v4, "chip
// 10"). THE CELL STAYS ONE DEVICE PX (cue_triangle_unit_px above) and the
// staircase whole cells; only the count rides the scale: 10 rows at 200 %,
// 5 at 100 %; at the scale's floor of 50 % (is_gui_scale_percent,
// device_config.h) 2.5 rounds to 2 (banker's) — and the drawing holds: no
// floor, no clamp. READERS: the fill's
// rows (cue_triangle_h_px), the chip's (cue_chip_rows below), the label's
// lead (cue_label_lead_px, the fill's reach), the chip's widths
// (cue_triangle_* below — every cull, hit and damage box) and the painter
// (CueTrianglePass).
inline constexpr double kCueTriangleRowsW = 5.0;
inline int cue_triangle_rows() {
    return scaled_px(kCueTriangleRowsW);
}
// THE MARKER'S CHIP: THE TRIANGLE IN A ONE-W OUTLINE (architect 2026-11-04,
// mock g1: today's triangle "in a one-W 111111 outline"; the colors are
// resolve_flag_face's). In whole cells — the triangle has no AA — THE
// OUTLINED SHAPE IS A SECOND, LARGER STAIRCASE OF THE SAME CONSTRUCTION,
// painted first in the outline's tone, the fill's staircase painted over it
// (CueTrianglePass): the outer starts t = hairline_px() rows ABOVE the
// fill's top row (cue_chip_top_rows) and its apex stands s =
// nearbyint(hairline_px() · √2) rows BELOW the fill's apex row
// (cue_chip_apex_rows: the one-W band measured perpendicular to the 45°
// sides is √2 W across a row), so the outer has rows + t + s rows
// (cue_chip_rows), its top row 2 · (rows + t + s) − 1 cells wide, and on
// every one of the fill's rows it reaches exactly s cells past the fill on
// each side. ON THE TABLET AT 200 %: rows 10, t 2, s 3 (2.83) — the outer
// 29 x 15, the fill 19 x 10; at 100 %: rows 5, t 1, s 1 — the outer 13 x 7,
// the fill 9 x 5. THE CHIP'S APEX STANDS ON THE LANE'S LAST ROW
// (cue_above_chip_h_px below), so the fill's top row is the lane's last row
// less rows + s (at 200 %, lane rows 202 .. 240: the outer on 226 .. 240,
// the fill on 228 .. 237). THE RULER'S HEAD IS THE SAME CHIP (architect
// 2026-11-04, mock k1, "4 accept overlap"): its outlined shape's apex on the
// ruler's last row (200 %: ruler
// rows 164 .. 199, the outer on 185 .. 199, the fill on 187 .. 196), so its
// top reaches into the ruler's digits
// where it stands under a label — ACCEPTED ("timestamp not really
// important for fine tune, just ballpark"): it paints over them as a chip
// paints over a label (paint_ruler_row).
inline int cue_chip_top_rows() {
    return hairline_px();
}
inline int cue_chip_apex_rows() {
    return static_cast<int>(
        std::nearbyint(static_cast<double>(hairline_px()) * std::sqrt(2.0)));
}
inline int cue_chip_rows() {
    return cue_triangle_rows() + cue_chip_top_rows() + cue_chip_apex_rows();
}
// THE LABEL'S HORIZONTAL LENGTHS, WHOLE W through scaled_px (the text
// round, architect 2026-11-03; whole W since 2026-11-04, the unit): THE
// FILL'S PAD either side of a segment's text, kCueFillPadW = 1, one W (a
// pad inside a box; 2 device px on the tablet at 200 %, 1 at 100 %)
// — the resting segment's box, the press target, the flag editor's field
// round its run (the caret's column borrowed from its right pad, the caret
// one W too) — and THE SEGMENT GAP between two of a marker's segments' boxes
// (the bound cells, the history's two halves), kCueSegmentGapW = 6 W (12
// device px at 200 %, 6 at 100 %), each floored at one device px; the cull
// bounds sum them (marker_flag_max_width_px, the history lane's). THE PAIR
// 2 / 12 (the unit, architect 2026-11-04: every authored length a whole W,
// so the 4 / 13 he kept on mock dec6a, 2026-11-03, rounds to the unit's
// pair; the 3 of the morning's unit 2 that night).
inline constexpr double kCueFillPadW    = 1.0;
inline constexpr double kCueSegmentGapW = 6.0;
inline int cue_fill_pad_px() {
    return scaled_px(kCueFillPadW, 1);
}
inline int cue_segment_gap_px() {
    return scaled_px(kCueSegmentGapW, 1);
}
// THE LABEL'S LEAD from the marker's column to its text IS THE FILL'S
// REACH RIGHT, THE OUTLINE'S W AND THE GAP (architect 2026-10-26 ~12:40, on
// the glass at nine rows: "It seems the lead-in was not scaled — increasing
// rows seems to not move text, but it should, relative to what I tuned it
// to"; the outline's W 2026-11-04, mock g1 shifting every label one W
// right): the fill's reach (its rows · u, cue_triangle_h_px's own number,
// the one spelling), one W (hairline_px, the chip's outline), then THE GAP,
// 6 W (kCueLabelGapW; 12 device px on the tablet at 200 %; architect
// 2026-11-02, "flag text lead in at 12 pixels"; 13 device px from the text
// round, 2026-11-03, back to 12, the whole W, with the unit, 2026-11-04)
// through scaled_px, rounded at the element —
// so the label follows the rows and keeps the gap. THE GAP IS MEASURED FROM
// THE OUTLINE'S NOMINAL ONE-W EDGE BESIDE THE FILL'S REACH, not from the
// outer staircase's widest row (which reaches s − t cells further: the gap
// from that row is 11 device px on the tablet). At 200 %: 10 + 2 + 12 = 24
// device px; at 100 %: 5 + 1 + 6 = 12. The gap is
// never negative, so the text never stands under the flag. THE FILL'S LEAD
// IS THE LEAD LESS THE FILL'S PAD (cue_fill_lead_px): the text stands one
// pad inside its fill (cue_segment_boxes, render.cpp). Readers: the segment
// boxes (cue_segment_boxes), the cull bounds (marker_flag_max_width_px
// below, the history lane's).
inline constexpr double kCueLabelGapW = 6.0;
inline int cue_triangle_h_px();   // below, the fill's rows · u
inline int cue_label_lead_px() {
    return cue_triangle_h_px() + hairline_px() + scaled_px(kCueLabelGapW);
}
inline int cue_fill_lead_px() {
    return cue_label_lead_px() - cue_fill_pad_px();
}
// THE BARE TRIANGLE'S HEIGHT — the fill's alone —
// cue_triangle_rows() cells of one device px; THE CHIP'S HEIGHT, the
// outlined shape's and the ruler's head's whole, cue_chip_rows() cells.
inline int cue_triangle_h_px() {
    return cue_triangle_rows() * cue_triangle_unit_px();
}
inline int cue_chip_h_px() {
    return cue_chip_rows() * cue_triangle_unit_px();
}
// THE TIME FIELD'S PAD AND ITS HEIGHT (architect 2026-10-23, "symmetrical
// and much tighter"; the rule 2026-11-03, the text round; the number
// 2026-11-03, mock u3) — ONE OWNER, read by the time fields
// (paint_handler.cpp's THE TIME FIELDS block: row 8's clock, the render
// player's two, the score viewer's rows), the hover tooltip, whose box
// pads every side with it (paint_shift_tooltip; architect 2026-10-31, row
// 8's clock "has a nice padding for the font size and resolution"), and
// THE MARKER LANE below: THE PAD IS EQUAL ON ALL
// FOUR SIDES OF THE DIGITS' INK — above and below their cap band, left and
// right of the widest-digit specimen's ink box in whole px (the cell,
// paint_handler.cpp's time_field_metrics and kTimeShape's block; architect
// 2026-11-05, the cell having been the specimen's advance ceiled, which
// read 12 / 14 on the tablet) — kTimeFieldPadW, 6 W (12 device px
// on the tablet at 200 %; scaled_px, rounded at the element, floored at one
// px; mock u3's 13 device px rounded to the unit, architect 2026-11-04) —
// and THE HEIGHT IS DERIVED, the pad, the Program face's cap rows
// (gui_font_cap_px, rounded at the element) and the pad: 12 + 15 + 12 = 39
// device rows on the tablet (the 22-device-px face's cap 15.14; mock v4 read
// 39 too); THE WIDTH the same way at the painter, the pad, the cell and the
// pad, 12 + 99 + 12 = 123 on the tablet (time_field_w_px). THE FIELD IS NO
// KEY (architect 2026-10-29): it does not take the
// key's height, so at key_height's default 17 W (34 device px) it is the
// taller of the two, and under a key_height above the field's rows the
// shorter, centred on the case's rows either way (paint_handler.cpp's
// time_field_rect).
inline constexpr double kTimeFieldPadW = 6.0;
inline int time_field_pad_px() {
    return scaled_px(kTimeFieldPadW, 1);
}
inline int time_field_h_px() {
    return time_field_pad_px() +
           static_cast<int>(std::nearbyint(
               gui_font_cap_px(gui_font(GuiFace::Program)))) +
           time_field_pad_px();
}
// THE MARKER LANE BY THE METRICS RULE (architect 2026-11-01 night, M5g /
// M5h / M5j, "calc based on legal chars for the warp input"; "space
// above/below text should be identical"; THE LANE AT THE TIME FIELD'S
// HEIGHT 2026-11-03, mock u3: "identical at least vertically"):
// THE LANE IS THE TIME FIELD'S HEIGHT (time_field_h_px above: 39 device
// rows on the tablet at 200 %), and ITS WHOLE ROWS ARE THE FLAG EDITOR'S
// BOX — its outline (kCueEditorOutlineW, one W, 2 device px on the tablet
// since 2026-11-04 night) on the
// lane's first and last rows, THE LEGAL SET'S BAND inside it, and THE PADS
// BETWEEN THEM DERIVED, NOT AUTHORED — so the lane holds the open editor
// exactly and the label rides ONE BASELINE at rest and in the editor.
// THE BAND (cue_band_above_px / cue_band_below_px): the ink of the warp
// line's legal set (is_valid_label_format, warpmarkers_parse.cpp —
// lowercase, digits, `.`, `+`, `-`, `:`) with THE X-HEIGHT BAND CENTERED IN
// IT, the x band being what the eye centers ("measuring in GIMP, the 'ass'
// in pass has 15px to top and 16px to bottom; I can see that 1px
// discrepancy"): k = max(ascender − x-height, descender) rows each side of
// the x band — the label face's "l" ink, its "x" ink and its descent, each
// rounded at the element (gui_font_ascender_ink_px, gui_font_x_ink_px,
// gui_font_descent_px) — so the band is x + k above the baseline and k
// below it.
// THE BAND'S SEAT: THE X BAND CENTERED IN THE LANE, A HALF-ROW TIE UP —
// its top x_top = floor((H − x) / 2) rows under the lane's top (H the
// lane, x the x band's rows; cue_x_top_px), the pad above the band x_top −
// outline − k (cue_editor_pad_above_px), the pad below it the rest, H −
// x_top − x − k − outline (cue_editor_pad_below_px). ON THE TABLET AT 200 %
// (Bold Label 11 W = 22 device px: ascender 16 (15.94), x 12 (11.62),
// descent 5): k 5, the band 17 above the baseline and 5 below (22), the
// lane 39 = 2 + 6 + 22 + 7 + 2, THE X BAND ROWS 13 .. 24 (13 rows above it,
// 14 below), the band rows 8 .. 29, the baseline 25 rows under the lane's
// top. A scale whose field is shorter than the box's minimum (2 · outline +
// the band) takes that minimum, both pads 0. The chip's device rows are the
// lane's bottom rows (cue_above_chip_h_px the remainder: 39 − 15 = 24 on
// the tablet), under the band's descenders where no label stands beside it.
inline constexpr double kCueEditorOutlineW = 1.0;
inline int cue_editor_outline_px() {
    return scaled_px(kCueEditorOutlineW, 1);
}
inline int cue_band_k_px() {
    const GuiFont f = gui_font(GuiFace::Label);
    const int asc = static_cast<int>(std::nearbyint(gui_font_ascender_ink_px(f)));
    const int x   = static_cast<int>(std::nearbyint(gui_font_x_ink_px(f)));
    const int dsc = static_cast<int>(gui_font_descent_px(f));
    return std::max(asc - x, dsc);
}
inline int cue_band_x_px() {
    return static_cast<int>(std::nearbyint(
        gui_font_x_ink_px(gui_font(GuiFace::Label))));
}
inline int cue_band_above_px() {
    return cue_band_x_px() + cue_band_k_px();
}
inline int cue_band_below_px() {
    return cue_band_k_px();
}
inline int marker_lane_h_px() {
    return std::max(time_field_h_px(),
                    2 * cue_editor_outline_px() + cue_band_above_px() +
                        cue_band_below_px());
}
inline int cue_x_top_px() {
    return (marker_lane_h_px() - cue_band_x_px()) / 2;
}
inline int cue_editor_pad_above_px() {
    return cue_x_top_px() - cue_editor_outline_px() - cue_band_k_px();
}
inline int cue_editor_pad_below_px() {
    return marker_lane_h_px() - cue_x_top_px() - cue_band_x_px() -
           cue_band_k_px() - cue_editor_outline_px();
}
inline int cue_above_chip_h_px() {
    return marker_lane_h_px() - cue_chip_h_px();
}
// THE LABEL BOX'S ROWS — the selected fill, the resting face box, the
// published press target and the flag editor's field — under the marker
// lane's top: THE WHOLE LANE (architect 2026-10-09 ~21:00, "the text box
// should take up the whole marker lane … right now it's too small to
// click"), the lane's own composite.
inline int cue_fill_h_px() {
    return marker_lane_h_px();
}
// THE BOX SEAT'S ONE SOLVER (paint_handler.cpp, where its rule and its other
// callers stand): the baseline that centres `font`'s cap band in the box
// [box_y, box_y + box_h), a half-row tie toward the top.
double redesign_baseline(const GuiFont& font, double box_y, double box_h);
// THE CUE LABEL'S BASELINE AND THE EDITOR'S BAND under the marker lane's top
// (the metrics rule above): THE BAND — the flag editor's caret and selection
// band (render_flag_editor_box reads cue_band_top_px and cue_band_h_px,
// never a second derivation) — from the pad above's foot, the band's two
// parts tall (rows 8 .. 29 on the tablet at 200 %); THE BASELINE the
// band's top and its rows above the baseline (25 on the tablet).
// Every label segment — the resting label, the bound cells, the history's
// two halves — and the flag editor's text ride the baseline (the editor's
// text sits at the label's seat).
inline int cue_band_top_px() {
    return cue_editor_outline_px() + cue_editor_pad_above_px();
}
inline int cue_band_h_px() {
    return cue_band_above_px() + cue_band_below_px();
}
inline int cue_baseline_px() {
    return cue_band_top_px() + cue_band_above_px();
}
// THE PAINTED COLUMNS round the stem column `col`, read off the painter's
// own cells (paint_ce_cue_triangle, centred on the stem's column, architect
// 2026-10-09 ~14:35: a staircase of R rows' top row 2·R − 1 cells from
// col − (R − 1)u to col + (R − 1)u, u the triangle's cell, one device px):
// [col − half, col + reach), (R − 1)·u left and R·u right, which holds the
// one-W stem's own columns on `col` (col − t / 2 .. col − t / 2 + t − 1, t
// waveform_line_px, the even line straddling the frame, so the apex at col
// points at the stem's right pixel — architect 2026-11-06, session 67, "it
// should point at the right column") at every scale. ONE EXTENT, THE CHIP'S
// (cue_triangle_half_w_px, cue_triangle_reach_right_px; R the chip's rows,
// cue_chip_rows): 14 and 15 device px on the tablet at 200 %, 6 and 7 at
// 100 % — the
// outlined chip (architect 2026-11-04), the marker's and THE RULER'S HEAD'S
// alike since the head wears the outline too (mock k1, the same evening),
// its rows [top, top + cue_chip_h_px()). THE ONE ROAD for every reader: the
// cue's published triangle box (paint_cues), the label's face box from its
// own chip's left edge (paint_cues; no label is clipped), the flag
// iterator's cull, the head's cull and its published hit (paint_ruler_row,
// AppState::playhead_head_hit), and the playhead's cull and its damage box
// (render_playhead, playhead_invalidate_rect, main.cpp), so no repaint can
// be narrower than what the head painted.
inline int cue_triangle_half_w_px() {
    return (cue_chip_rows() - 1) * cue_triangle_unit_px();
}
inline int cue_triangle_reach_right_px() {
    return cue_chip_rows() * cue_triangle_unit_px();
}
// THE WELL'S EDGES AND MARGINS (architect 2026-10-09, the third part;
// program_spec.h's column_margin_px; flat since 2026-11-01 night, THE EDGE
// RULE): under the marker lane THE ROW OF GROUND between the lane and the
// canvas is a lane of its own, top lane 4, one hairline
// (lane_foot_line_h_px — the 242424 line of the record); under the canvas
// THE WELL'S FOOT, one hairline of ground, a bottom lane of its own
// (well_foot_h_px), between the canvas and row 8 (the program ring's last row
// until the flat well; the modal family's box keeps its last row on it,
// modal_box.h). Across, the well stands column_margin_w_px of the ground in
// from each side of the window, then THE WAVEFORM AREA, which is the well
// (architect 2026-11-03), its width floored to the grid step and the floor's
// leftover (under 16 device px) split between the two margins, the odd pixel
// to the right (main.cpp's waveform_area owns the split; column_margin_w_px
// is the side before it): the side 9 W, 18 device px on the tablet at
// 200 %, 9 at 100 %. THE ROWS OF GROUND ARE ONE W, rounded at the element
// (hairline_px, the line rule: 2 device px at 200 %, 1 at 100 %; architect
// 2026-11-04, the unit), and the ground beside the area across is the
// margin and half the flooring's leftover, so the waveform area, which every
// canvas painter and hit test reads, is the canvas's whole interior with
// nothing between it and the ground.
inline int lane_foot_line_h_px() {
    return hairline_px();
}
inline int well_foot_h_px() {
    return hairline_px();
}
inline int column_margin_w_px() {
    return scaled_px(kProgramSpec.column_margin_px);
}
// The two lanes (main.cpp, beside the other lane accessors).
GuiRect lane_foot_line_area(const AppState& a);
GuiRect well_foot_area(const AppState& a);
// THE WELL'S COLUMNS (2026-10-23, the one owner; THE EDGE RULE of 2026-11-01
// night; THE WELL IS THE WAVEFORM AREA, architect 2026-11-03): its first
// column and one past its last — waveform_area's x and x + w, so the grid
// floor's leftover decides them and no inset does: 24 and 2280 on the
// tablet's 2304 at 200 % (the margin's 18, the leftover 12 split 6 / 6, the
// area 24 .. 2279); 16 and 1904 on a 1920-px window at 100 %. EVERY
// REGION, STRIP AND BOX STANDING ON THEM READS THEM HERE (architect
// 2026-10-23, "yes" to tying both strips' ends to the well; 2026-11-01
// night, "the rightmost button's right edge lines up with the
// trim/time/marker/canvas region, and the same for the left"): the well's
// regions (render_trim_flags, paint_ruler_row, the marker lane's face), the
// modal family's box and its controls row (modal_box_rect,
// modal_controls_seats), the band's ends (icon_row_seats) and row 8's
// (bottom_row_seats, the controls row's seats too), the logo lane's items,
// the cards' stack and the tooltip's clamp, so they coincide at every
// scale; both strips and the controls row stand flush with them at both
// ends, no inset — the outermost keys' FACES on the well's first and last
// columns (the face is the key, 2026-11-04).
// THE EDGE RULE'S STATEMENT (architect 2026-11-03: "everything that sits
// closest to the window edge on all screens should align to the waveform
// canvas — icons, modal, etc."): THE OUTERMOST PAINTED THING ON EVERY
// SURFACE STANDS ON THESE COLUMNS — the keys' faces, the logo lane's ink,
// row 8, the cards' and the tooltip's boxes and THE MODAL PANE (modal_box.h:
// the box's outer ground columns one hairline outside them). The on-screen
// keyboard is exempt, a small block seated on the window's center by his
// ruling (onscreen_keyboard.h's block_x_px).
struct WellColumns {
    int x   = 0;   // the well's first column (the waveform area's x)
    int end = 0;   // one past its last column (the area's x + w)
};
WellColumns well_columns(const AppState& a);

// (THE TRIM LANE AS WINDOWS' MINIATURIZED SCROLL BAR — its 16-W track,
// caps and thumb — THE RULER'S HEIGHT DERIVED FROM ITS SMALL FACE with the
// playhead's WordPad head on its bottom rows, and THE MARKER LANE DERIVED
// FROM THE 17-W FLAG BOX stood 2026-10-02 to 2026-10-09; git history.)

// Authored pixel geometry of THE BOTTOM ROW — THE UNIFIED BOTTOM ROW, the
// lane rows 8 and 9 merged into (architect-ruled 2026-08-12): the clock in
// its flat time field, the active tab's name and the state line on the
// ground beside it at the left (architect 2026-10-02 / 2026-10-03 /
// 2026-10-05; the name 2026-10-22), and the skips (Go to Start, Go to End;
// 2026-10-24), the MARKER-VERB GROUP, the step and
// walk (Down, Up, Previous Marker, Next Marker) and the nudge and play
// (Switch Tab, Center, Left, Right and Play / Stop, the last case; architect
// 2026-10-18) packed right, each a plain run of cases, the run gap
// (program_run_gap_px, the `run_gap` tuning key) of bare ground
// between two (kMarkerVerbGroup in paint_handler.cpp owns that group's
// membership), all one lane ON THE WINDOW'S FOOT.
//
// THE ROW IS THE STRIP (architect 2026-10-22, the simple layout): THE STRIP
// PAD, the program's key (the band's own, read from its accessors above so
// the rows cannot drift apart), THE STRIP PAD, the keys' legends in it —
// strip_h_px, at key_height 17 55 W, 110 device rows on the tablet at 200 %
// (38 + 34 + 38, the hi-fi's again since 2026-11-03) —
// on the bare ground, no
// line, no bar and no border (Cool Edit's dock bar over it, its grippered
// and then barred groups and the border under it stood 2026-10-09 to
// 2026-10-22). The layout is paint_bottom_row_buttons_and_clock's walk.
// Every modal — the render player, the project picker, a prompt, a dialog
// editor — stands its controls on these rows, THE MODAL FAMILY'S CONTROLS
// ROW (modal_box.h), or, under a dialog editor on the glass, on the same
// rows lifted onto the on-screen keyboard's band.
inline int bottom_row_h_px() {
    return strip_h_px();
}

// (THE STATUS BAR'S GEOMETRY IS DELETED — architect 2026-08-29, the evening of
// the day it landed. A tenth lane stood on the window's foot for one day, 1 +
// 31 + 1 authored px on the row-7 crop's measure, carrying the state strings
// in two cells; `kStatusBarContentPx` and its three accessors went with it,
// and the STATE TEXT is row 8's own cell right of the clock. The reasoning is
// at main.cpp's bottom lane table and in
// paint_bottom_row_buttons_and_clock (paint_handler.cpp).)

// (THE TEXT SIZES ARE THE FACES' OWN — architect 2026-10-05. The shared
// 13-Windows-px size, the ruler labels' 10 and the tooltip hint line's 11
// retired: each face's size is the em at which the live set's file matches
// its recorded cell vertically — the body's 13-px cell, the ruler's digit
// (gui_font.h). A
// text surface names its face, gui_font(face), and reads its vertical
// metrics off the recorded constants.)

// (THE PROGRAM'S STACK IN WINDOWS PX — the band's strip pad and row 8's
// two, chrome_stack_authored_h — LEFT 2026-11-03 with the strip pad's W:
// every lane above and below the well is a skin length or a hairline,
// derived at the element — the logo lane, the strip pads and the keys
// following `key_height` (the case block above), the well's lanes (the
// well's lanes block above). On the tablet at 200 % and key_height 17 the
// band 144 device rows (72 + 34 + 38), the well's lanes above the canvas 97 (20 + 38
// + 39) and its foot line 2, the well's foot 2 and row 8 110 (38 + 34 +
// 38): the canvas 1085 rows of the surface's 1440 (architect 2026-11-04,
// the unit: "waveform height ok to take from") — main.cpp's lane table the
// record of each scale.)
// AT THE WAVEFORM'S EDGES A CUE IS CLIPPED (architect 2026-09-26, the flag
// box's rule kept for the cue): the marker lane paints inside the waveform's
// columns [0, w) — every triangle, label, editor field and hit rect
// (clip_to_waveform_columns, render.cpp), the columns standing at the
// canvas's own x since 2026-10-09 (waveform_area, the column's margin) — and
// the flag iterator admits a marker whose triangle reaches into them (its
// left edge cue_triangle_half_w_px left of its column, 14 device px on the
// tablet at 200 %), so a marker just past the last
// column shows the left half of its chip there, and no stem (the stem
// stash is gated to [0, w)).
// THE PLAY-SCRUB'S CHANNEL AND THUMB HEIGHTS, here because the channel is a
// hairline (the slider's block, with its constants, is above): the channel
// ONE W (hairline_px, the line rule, architect 2026-11-04); the thumb THE
// KEY'S HEIGHT (architect 2026-10-29, "Shrink it to the height of the
// key."), icon_case_h_px itself — the face is the key, 2026-11-04 — so it
// fills the controls row's face band exactly and follows `key_height` live.
inline int scrub_channel_h_px() {
    return hairline_px();
}
inline int scrub_thumb_h_px() {
    return icon_case_h_px();
}
// The channel's rows below the thumb's top: CENTERED ON THE FACE, the
// centered elements' tie rule ((face − channel) / 2, the remainder rounded
// down, the upper row winning a tie) — 16 on the tablet at 200 % on the
// 34-row face at key_height 17 (the 2-row channel on rows 1384..1385), 8 at
// 100 % (the 17-row face, the 1-row channel).
inline int scrub_channel_above_px() {
    return (scrub_thumb_h_px() - scrub_channel_h_px()) / 2;
}
// THE WAVEFORM AREA CARRIES NO BORDER: the canvas has none (row 6's canvas
// paragraph), the rows and columns of ground stand outside the area.
// THE WAVEFORM'S LINE WIDTH — ONE W, THE LINE RULE'S (architect 2026-11-04,
// the unit, REVERSING "ON WAVEFORM → UNSCALED. OTHERWISE, SCALED." of
// 2026-10-09 ~14:25: Live scales its lines, measured on his 100 % / 125 %
// shots): hairline_px itself, 2 device px on the tablet at 200 %, 1 at
// 100 % (one W = 2 device px on the tablet since the rebase of 2026-11-05).
// EVERY LINE PAINTED ON THE CANVAS is this width: THE READERS — the one
// inventory of the class, each an ALIASED INTEGER RECT, SOLID (no dotted
// line since 2026-11-04) — the scanner and the resting cursor
// (render_playhead), the selection stems (paint_marker_stems), the
// zoom anchor's stem (render_strip_anchor_stem), the phase-reset lead-in
// ring's four sides (paint_phase_reset_overlay_ring), the nudged pairs'
// rings (paint_marker_stem_runs), the canvas's GRID
// AND CENTER LINES (render_canvas, render_center_lines: a t-thick line
// keeping its top row on its row, canvas_hline: the horizontal's deliberate
// asymmetry, recorded there) and THE STEREO DIVIDER (chrome
// standing on the canvas, waveform_divider) — and, off the canvas, THE
// RULER'S TICKS (paint_ruler_row, the same line on the ruler's foot). THE
// LINE RULE'S ONE STATEMENT (architect 2026-11-06, session 66 / 67, "I like
// your suggestion", superseding 2026-11-04 night's left-pixel rule — "the
// leftmost pix should be the real frame"): A FRAME IS A PIXEL BOUNDARY, NOT
// A PIXEL — the plate's column c spans the lattice points g(c) .. g(c + 1)
// (render_waveform_columns' edge_at), so a frame on the lattice is the
// boundary between device columns c − 1 and c, the left edge of column c —
// AND A VERTICAL LINE OF WIDTH t STRADDLES IT: columns [col − t / 2,
// col − t / 2 + t), integer division (waveform_line_first_col): at t = 2
// the column LEFT of the frame and the frame's own, centered on the
// boundary exactly; at t = 1 the column itself; at an odd t (3) the column
// and one each side, centered on the column (waveform_line_cols below, the
// one owner). Nothing moves in the lattice, the camera, the landings or the
// plate: the line's paint alone. THE HORIZONTAL LINES DO NOT STRADDLE (the
// grid and the center lines, canvas_hline in render.cpp, the deliberate
// asymmetry and its numbers recorded there): their rows keep the top-row
// frame. THE CUE'S CHIP AND THE PLAYHEAD'S HEAD DO NOT MOVE: odd
// staircases whose apex is the frame's column, so over the straddling stem
// (col − 1, col) the apex points at the stem's RIGHT pixel (architect
// 2026-11-06, session 67, "it should point at the right column"; the
// chips' blocks at cue_triangle_unit_px and cue_triangle_half_w_px). THE
// PLATE DRAWS NO LINE: the magnified inner is a solid fill, THE CORE (architect
// 2026-11-04, THE CORE below), so every line on the canvas is one W. THE
// CUES' TRIANGLES AND THE PLAYHEAD'S HEAD keep their one-device-px cell of
// their own
// (cue_triangle_unit_px, 2026-11-04: "untouched"). EVERYTHING ELSE OFF THE
// CANVAS keeps its areas scaled (scaled_px and scaled_px). (From 2026-09-27
// to 2026-10-09 every canvas line was one Windows px, scaled; from then to
// 2026-11-04 one device px at every scale.)
inline int waveform_line_px() {
    return hairline_px();
}
// THE WAVEFORM PLATE'S ANTIALIASING GRID AND GROW (architect 2026-10-30, on
// his glass against Ableton; the grow's key 2026-10-31; the rule is
// render_waveform_columns' head, the shader kListFragmentShader,
// gl_canvas.cpp): the plate's shape is rasterized on a grid of
// kWaveformSubcells x kWaveformSubcells subcells per device pixel — the
// tips quantized to quarter rows, a pixel's coverage its set subcells over
// sixteen, exact to 1/16 — and THE WHOLE SHAPE IS GROWN on every side by
// a disc of radius `waveform_grow` subcells before the coverage is taken.
// THE GRID IS 4x FOR GOOD (architect 2026-10-31 afternoon, "let's just
// stick with 4x. any perf hit is not worth additional thickness"): the 8x
// grid of that morning (one commit, 19657fb7, with the grow in eighths)
// measured on the tablet, per plate frame against the 11 ms budget at
// 90 Hz, ~300 ms with the grow a uniform and, with the shader specialized
// per grow, ~16 ms at grow 0, ~36 ms at 0.5 and ~100 .. 110 ms at 0.625
// and 0.75 (the two-word window), so the finer lattice went back the same
// afternoon and the key steps in quarters.
// THE GROW IS A DEVICE KEY, `waveform_grow`, IN QUARTERS OF A DEVICE PX
// (architect 2026-10-31; device_config.h's grammar, the seat
// waveform_grow_quarters above, the plate shader's GROW): a multiple of 1/4
// device px in [0, 2], and it STAYS A KEY — THE PLATE IS THE ONE THING ON
// THE CANVAS NOT SCALED WITH gui_scale (its columns and its quarter-row tips
// are device px; the canvas's lines one W since 2026-11-04, the unit,
// waveform_line_px), so the waveform needs one tuning key of its own and the
// grow is the appropriate one, kept for good because a replacement panel
// may not come at this density: the grow is the device-px number whose
// right value depends most on the panel's density.
// (The 2026-10-30 ruling that made it a reasoned constexpr — "neighboring
// pixels spread the weight exactly at .5; at 0 they are thin, at 1 they are
// overly thick", a one-px stroke centered on the contour being the shape
// grown half a pixel each side — is reversed, and stays as the record of
// the 0.5 that was the default until 2026-11-04 night. THE DEFAULT WAS 1.0
// from then — kWaveformGrowDefaultQuarters = 4 subcells, the construction
// state since the first-run value became the shipped defaults file's,
// device_config.h's THE FIRST-RUN VALUES, 2026-11-09 (architect 2026-11-04 night, on
// the real lit capture at grow 1 beside the 2-px lines of the unit of 2:
// "is good as well"); the key stays a key, permanent. 0.625, the exact
// analog of Ableton at 125 % Windows scale, lies off the quarters lattice;
// 1.0 read too thick at a clean passage beside the 1-px lines of 2026-10-30
// and right beside the 2-px lines — hence the key.) A quarter of a
// device px is one subcell (the key's lattice, kWaveformGrowQuartersPerPx,
// device_config.h). The shader spells the grid as literals; gl_canvas.cpp
// static_asserts them against this constant and its one 32-bit window —
// the grow's 2 px alone (2026-11-04, the core) — against the key's top.
constexpr int kWaveformSubcells = 4;
// THE NEVER-FADE FLOOR IS A QUARTER DEVICE PX (architect 2026-10-30, judged
// on his glass against Ableton's measured silence line): a bar whose
// interval holds less than this many subrows takes this many (bar_tips,
// render.cpp; the paragraph at render_waveform_columns' head), so digital
// silence is a one-subrow bar which the default grow makes a line of about
// 1.25 px of summed coverage. A quarter of the grid, which must divide into
// quarters: one subrow on the 4x grid, the constant kept as the floor's
// own statement (the 8x grid of 2026-10-31's morning held it at two).
constexpr int kWaveformFloorSubcells = kWaveformSubcells / 4;
static_assert(kWaveformSubcells % 4 == 0,
              "the never-fade floor is a quarter device px of subrows");
// THE LINE'S FIRST COLUMN for a frame on column `col`: col − t / 2, integer
// division — THE STRADDLE (THE LINE RULE'S ONE STATEMENT at
// waveform_line_px's block, architect 2026-11-06: the frame is the boundary
// at column col's left edge, so an even line takes the column left of it
// and col itself; an odd line is centered on col). The one spelling of the
// rule: waveform_line_cols below, and the two rings' runs, which span from a
// side's first column (paint_phase_reset_overlay_ring,
// paint_marker_stem_runs).
inline int waveform_line_first_col(int col, int t = waveform_line_px()) {
    return col - t / 2;
}
// THE LINE'S ONE PAINT: a vertical line of width `t` ON COLUMN `col`,
// STRADDLING ITS FRAME — columns [col − t / 2, col − t / 2 + t)
// (waveform_line_first_col; at t = 2 the columns col − 1 and col) — of a
// strip whose columns
// are [0, area_w), at window x `area_x`, rows [y0, y1), as ONE aliased
// integer rect in the caller's source colour, `t` the canvas's line, one W
// (waveform_line_px). THE TWO EDGE RULES live here: a line is GATED ON ITS
// OWN COLUMN (col outside [0, area_w) paints nothing, so a marker at column
// w shows no pixel at w − 1 even where its line's left column would at
// t = 2, nor one at column −1 a pixel at 0 at an odd t — a line belongs to
// its column), and its columns are CLIPPED AT BOTH EDGES, so a
// line at column 0 or w − 1 paints only its columns inside [0, area_w) and
// nothing reaches the ground beside the canvas (at t = 2 a line on column 0
// shows its right pixel alone, column 0; one on w − 1 shows both, w − 2 and
// w − 1). THE RIGHT EDGE'S ASYMMETRY IS DELIBERATE (architect 2026-11-06,
// session 67): a frame at column w, one past the area's last column (End at
// the whole-song zoom), paints no line under the gate while a line on column
// 0 shows its right pixel — "I don't go to the end of the file. There's no
// purpose. A marker, either warp or phase reset there, does nothing. I would
// say leave."; the gate stays.
// The two edge rules' one statement: the line's painted columns, area-local
// (its first column x0 and its width w), w 0 where the column gate refuses
// it.
struct WaveformLineCols {
    int x0 = 0;
    int w  = 0;
};
inline WaveformLineCols waveform_line_cols(int area_w, int col,
                                           int t = waveform_line_px()) {
    if (col < 0 || col >= area_w) return WaveformLineCols{};
    const int lo = waveform_line_first_col(col, t);
    const int x0 = lo > 0 ? lo : 0;
    const int x1 = (lo + t < area_w) ? lo + t : area_w;
    return WaveformLineCols{x0, x1 > x0 ? x1 - x0 : 0};
}
inline void fill_waveform_line(cairo_t* cr, int area_x, int area_w, int col,
                               double y0, double y1,
                               int t = waveform_line_px()) {
    const WaveformLineCols c = waveform_line_cols(area_w, col, t);
    if (c.w <= 0) return;
    cairo_rectangle(cr, static_cast<double>(area_x + c.x0), y0,
                    static_cast<double>(c.w), y1 - y0);
    cairo_fill(cr);
}
// THE SCALE IS THE ONE THING A FLAG CUTS (architect 2026-09-19, replacing the
// nine-glyph budget this lane carried from the retired marker-text lane). A
// warp payload's TEMPO — its base and its whole deviation chain — is the
// reason the flag exists, so it paints in full however long the chain runs;
// what a glance does not need is the scale's low digits, which is exactly
// what the old budget happened to cut on the payloads that could reach it.
// So the DISPLAY composer (flag_display_text, render.h's flag-text block)
// keeps `*` plus these FOUR bytes of the scale — `*1.01` — and drops
// everything after them, and the rule reproduces the old paint byte for byte
// on every payload authorable before the chain landed: a scale is spelled
// min-4 (format_value_double), so it is at least six bytes and always cuts,
// and a four-byte base plus five capped scale bytes IS the old nine.
inline constexpr size_t kMarkerFlagScaleGlyphs = 4;

// THE TRUNCATION MARKER, appended by the display composer when it cut
// anything — the scale's low digits, a label definition riding past them —
// so the bytes it appends and the glyphs the width bound below charges for
// have ONE owner and cannot drift apart. Three ASCII periods rather than
// U+2026 by the architect's own side-by-side verdict (2026-08-02): he
// compared the two on his flag crop and preferred the wider, looser
// three-dot look, so it is the spec. Pure ASCII, so its size() is both its
// byte length and its glyph count.
//
// Composed marker flag text is printable ASCII by construction (the
// lowercase-ASCII label grammar and the numeric serializers), and so is
// every bound cell's token beside it, so the product paints NO non-ASCII
// surface — it has ONE face and no font fallback by standing ruling, and
// nothing painted needs one. DISPLAY ONLY: the store, the sidecars, the
// editor seed and the copy payload never see the dots.
inline constexpr std::string_view kMarkerLabelTruncationMarker = "...";

// ONE ITERATION BOUND CELL'S GLYPH COUNT, a fixed shape by grammar
// (format_iter_bound_cell, warpmarkers.h: a sign, one integer digit, the
// point, two decimals — `+4.00` at the widest, the integer digit bounded by the
// tempo window the commit and the step share, which is inside
// ±kIterDeltaMaxCents). The width bound below charges two of these while
// iteration mode is on; nothing is laid out against the number.
//
// THE BPM FORM'S VALUES NEED NO SUCH TERM, and the symmetry question is
// answered rather than skipped: bpm_field_text (warpmarkers.h) is a different
// composer feeding a different surface — the BPM form's rows and its
// DIALOG-HOSTED line editor (architect 2026-10-29) — and never reaches a flag
// box, so no flag width depends on it.
inline constexpr size_t kIterCellGlyphs = 5;
// (IT IS THE WARP TOKEN'S WIDTH AND IT BOUNDS BOTH COLUMNS. A phase-reset
// cell's token is a sign and one digit — `+9` at the widest, the single-digit
// bracket — so it is comfortably under this, and the cull bound above stays a
// bound with nothing added for it. Nothing is LAID OUT against this constant;
// every cell's real width comes from its own shaped run.)

// An UPPER BOUND on a cue's label reach right of its column, used only to
// decide how far LEFT of the viewport a marker may sit and still reach into
// it (labels run rightward, so the left cull needs a width and the right
// cull only the triangle's left reach, which the iterator reads itself). No
// ASCII glyph in a sans face advances more than one em, so glyphs * em plus
// the label's lead and pad bounds every label the truncation can produce. A
// bound, not a size: nothing is laid out against it.
//
// THE GLYPH COUNT IS THE WORST PAINTED TOTAL, spelled out of the display
// composer's own grammar (flag_display_text): FOUR bytes of base (a tempo is
// N.NN and the bracket's integer part is one digit), EIGHTY of chain
// (kMaxTempoDeviationTerms terms at five bytes each, `+0.01` — sixteen since
// 2026-09-19), FIVE of capped scale (`*` and kMarkerFlagScaleGlyphs) and
// THREE of truncation marker — 92. The expression below reads the constant,
// so a term-cap retune moves this bound without an edit here.
// A label definition rides inside the last two terms rather than past them:
// it paints whole only where no scale was cut, and a `:a.aa` is four bytes
// under the `*N.NN...` it replaces there. The phase-reset token (five bytes)
// is far under it, which is why ONE bound still serves both columns.
//
// `iteration_on` ADDS THE TWO BOUND CELLS, and it must: an eligible label runs
// two segments further right than its payload predicts (each the segment
// gap, its pads and kIterCellGlyphs glyphs), and a bound that no longer bounds would
// cull a marker whose flag still reached into the viewport. The cells' shape
// is FIXED, so this stays a constant-time bound rather than becoming a
// measurement, and it is charged flat rather than per marker — every carrier
// paints both cells in the mode.
//
// ONE BOUND SERVES BOTH COLUMNS (2026-09-09): the phase-reset column's cells
// carry a signed whole hop (`+9` at the widest — kIterCellGlyphs is the WARP
// token's five and a hop cell is two), so the same charge over-states there by
// three ems per cell and stays a bound, which is the only requirement. The
// phase pass therefore needs no bound of its own and passes only its column's
// verdict.
// It remains a bound and not a layout input either way — over-admitting a
// few offscreen markers per frame costs a shaped run each and drops nothing
// visible.
//
// A cue has no left border (2026-10-09): the triangle's left reach is the
// iterator's own right-cull term, never a width to the right.
inline double marker_flag_max_width_px(bool iteration_on) {
    const size_t glyphs = 4 +                                  // `N.NN` base
                          5 * kMaxTempoDeviationTerms +        // `+0.01` each
                          1 + kMarkerFlagScaleGlyphs +         // `*N.NN`
                          kMarkerLabelTruncationMarker.size();
    const double glyph_bound =
        gui_font_advance_bound_px(gui_font(GuiFace::Label));
    // The payload's lead (cue_label_lead_px) and its fill's pad past the
    // text.
    const double label =
        static_cast<double>(glyphs) * glyph_bound +
        static_cast<double>(cue_label_lead_px() + cue_fill_pad_px());
    if (!iteration_on) return label;
    // Each cell: the segment gap and its two pads round its glyphs.
    const double cell =
        static_cast<double>(kIterCellGlyphs) * glyph_bound +
        static_cast<double>(cue_segment_gap_px() + 2 * cue_fill_pad_px());
    return label + 2.0 * cell;
}

// THE VIEW BAR'S GRABS (architect 2026-10-09: the trim bar's hit geometry
// "stays what it is, re-read off the new painted rects"): the begin and end
// GRABS are the span's two END ZONES, each kViewBarGrabW, 10 W, wide (20
// device px on the tablet at 200 %, scaled_px; the 21 device px of the trim
// lane's height 2026-10-24 to 2026-11-01, when the zones stood square on it
// as Windows' caps did, KEPT WHEN THE FLAT BAR TOOK TWO ROWS OFF THE LANE so
// the hit tests did not move (2026-11-02), rounded to the unit, 2026-11-04
// night) — standing on the
// bound's column inside the span (the begin's left edge on the begin column,
// the end's right edge on the end column), and the BRIDGE the span's body
// between them (render_trim_flags publishes all three; trim_endcap_rect and
// trim_bridge_gap own the rule). NO CAP IS DRAWN: the span's two verticals
// are its only edges, so the
// zones are the span's painted columns read as its two ends. NARROW: where
// the span is under two zones wide the begin zone keeps its column and the
// end zone stands edge to edge right of it, overrunning the end column by
// the difference — the one place a grab stands past the painted span, kept
// so a narrow window's two ends are always both grabbable (trim_endcap_rect's
// rule, 2026-10-03).
inline constexpr double kViewBarGrabW = 10.0;
inline int view_bar_grab_w_px() {
    return scaled_px(kViewBarGrabW, 1);
}
// (THE ARROW GLYPH, Windows' scroll arrow as one filled 4 x 7 W triangle —
// the trim caps' until 2026-10-09, then the popup lists' scroll bar's —
// went with that bar, 2026-10-14. Git history.)

// (THE PLAYHEAD HEAD WAS WORDPAD'S RULER INDENT MARKER — a 9 x 8 W glyph in
// the chrome's label, hilight, shadow and ground, drawn as nested rings and
// checked against its bitmap — from 2026-10-05 to 2026-10-09; it is Cool Edit's triangle since, the
// cue's own (the palette block's playhead paragraph). Git history.)

// THE HOVER TOOLTIP'S SHARED NUMBERS — a DAMAGE BOUND on its box height, its
// seat's gap, its hover target (the key's column) and the bypass chain's
// reach. They live out here, rather than with the rest of the tooltip's
// anatomy in paint_handler.cpp, because both the RUN LOOP and the input side
// read them:
// the clock (GuiInputHandler::tick_tooltip) runs on the tick and damages the
// band every seat of the box lies in (tooltip_damage_band, app_state.h), and
// the one wait writer (note_tooltip_hover) and the two hover walks read the
// column and the reach, and the seat's gap, which the seat and the damage band
// both read. The one duration, the delay, is the `tooltip_delay_ms` tuning key
// (device_config.h), read live.
//
// THE HEIGHT HERE IS A BOUND, NOT THE HEIGHT. The painter derives the real box
// from the two faces' caps under the time field's pad (paint_shift_tooltip,
// 2026-10-31), so the run loop only needs to know it can never exceed this:
// 75 W (the 50 Windows px of 2026-10-31 times 1.5 at the rebase to 200 %,
// 2026-11-05), 150 device rows on the tablet against the two-line box's 67.
inline constexpr int     kTooltipDamageHeightPx = 75;
inline int tooltip_damage_h_px() {
    return scaled_px(kTooltipDamageHeightPx, 5);
}

// THE SEAT IS THE OWNER'S FACE'S (architect 2026-11-07, session 90; the
// left alignment and the gap session 91's): the box above its owner's painted
// face, ITS LEFT EDGE ON THE FACE'S, THE GAP kTooltipGapW between them, held
// between the keys' outer columns and under the window's top; one rule on
// every surface. The placement's one statement is tooltip_box_rect
// (app_state.h). THE GAP IS 1 W (architect 2026-11-07, session 91: "ok to
// remove the key" — the `tooltip_gap` tuning key of session 90 settled here,
// closed_questions.md), 2 device px on the tablet at 200 %; read by the seat
// and its damage twin (tooltip_damage_band) alike. Floored at 0: a gap may
// vanish at a small scale (0 at 50 %, banker's rounding), the box then
// standing on the face's top row. (The pointer's seat, Windows 95's of
// 2026-10-06 — the box's left edge at the pointer, its top 27 W under it,
// flipped 3 W above the button at the window's foot — retired 2026-11-07,
// closed_questions.md.)
inline constexpr int kTooltipGapW = 1;
inline int tooltip_gap_px() {
    return scaled_px(kTooltipGapW, 0);
}

// THE TIMING IS ONE DELAY, THE `tooltip_delay_ms` TUNING KEY (architect
// 2026-11-07, session 87, the always-show model; device_config.h's grammar,
// default 100 ms): every arrival on a key's column waits it, and it is the
// bypass chain's window after a standing box is left. The model is stated
// once, at AppState::RedesignTooltip. (Comctl32's three — the 500 ms initial
// wait, the 100 ms reshow and the 5 s autopop — and the spent button retired
// with the model, closed_questions.md.)

// THE KEY'S COLUMN — THE TOOLTIP'S HOVER TARGET (architect 2026-11-07,
// session 87: "always show"; the model at AppState::RedesignTooltip): a key's
// painted face PLUS the strip pad under it, where its legend stands (the band's
// pad under the case; row 8's and every controls row's lower pad, their upper
// pad not), PLUS HALF THE KEY GAP ON EACH SIDE — the left side the gap's floor
// half (key_gap_px() / 2), the right side its ceiling half (the rest), so the
// two halves of an in-run gap sum to the gap whatever its parity and a run's
// columns TILE with no hole and no overlap: on the tablet at 200 % the gap is
// 2 device px, one each side; at 100 % it is 1, the left neighbour's column
// taking it. Ground beyond — a run gap's rest, the pad over row 8's faces —
// is no column. A face that does not stand (a zero rect) has no column.
// PRESSES STAY FACE-ONLY (point_on_key, materials.h): this is the tooltip's
// target alone, read by the two hover walks (recompute_redesign_button_hover,
// update_modal_dialog_hover) and by nothing that presses.
inline GuiRect tooltip_column_rect(const GuiRect& face) {
    if (face.w <= 0 || face.h <= 0) return GuiRect{0, 0, 0, 0};
    const int gap   = key_gap_px();
    const int left  = gap / 2;
    const int right = gap - left;
    return GuiRect{face.x - left, face.y, face.w + left + right,
                   face.h + strip_pad_px()};
}

// THE BYPASS CHAIN'S REACH (architect 2026-11-07, session 87): once a box
// has stood, the leave of its column keeps the chain while the pen stays
// within ONE W of that column (Chebyshev, on either axis), or for the delay
// at any distance (the model at AppState::RedesignTooltip). One W through
// scaled_px, 2 device px on the tablet at 200 %, floored at one device px.
inline constexpr double kTooltipChainReachW = 1.0;
inline int tooltip_chain_reach_px() {
    return scaled_px(kTooltipChainReachW, 1);
}

// THE HOVER SLOP (architect 2026-09-29; the tooltip's wait read it until the
// always-show model of 2026-11-07, which restarts nothing on motion) — ITS
// ONE READER IS THE LEVEL CASE'S PRESS (update_level_press_motion,
// input_pointer.cpp): until a motion carries the level case's press MORE than
// this from where it landed on EITHER axis, the press is still a tap (the
// case's toggle at the lift, whatever its duration) and moves no level by
// motion, so a hovering or pressing pen's jitter
// cannot nudge it. The number is ANDROID'S OWN for jitter: AOSP View's hover
// tooltip ignores a HOVER_MOVE within ViewConfiguration.getScaledHoverSlop()
// of its anchor on both axes (View.TooltipInfo.updateAnchorPos, "filters out
// the jitter which is typical for such input sources as stylus"), and that
// slop is `config_viewConfigurationHoverSlop` = 4dp
// (core/res/values/config.xml), half the platform's 8dp touch slop. It is
// taken as 5 W through scaled_px, A WHOLE W BY HIS RULING OF 2026-11-05
// ("round each up"): the 3 Windows px of 2026-10-02 times 1.5 at the rebase
// to 200 % the same day gave 4.5 W, which stood exact for a few hours as a
// gesture threshold, and was then rounded UP with the other two
// (kRulerMinMinorPitchPx, paint_handler.cpp; kNavZoomPxPerLevel,
// app_state.h) so that every authored number is a whole W. The
// one-device-px consequence: 10 device px on the tablet at its 200 % under
// the 320 density it runs at (9 before), 5dp — Android's 4dp to within one
// dp — and the drag gate (kDragMovedThresholdPx 9 W = 18, app_state.h) to
// within one device px of half, as Android's is half its touch slop. Floor
// 1, so a small scale never zeroes it.
inline constexpr int kHoverSlopW = 5;
inline int hover_slop_px() {
    return scaled_px(kHoverSlopW, 1);
}

// THE LEVEL SLIDER'S TRAVEL (architect 2026-11-05, the level case,
// AppState::ChromePress's Level kind): the pointer motion that carries a
// case's travel fraction across its whole range 0..1 — kLevelTravelW = 800 W
// through scaled_px, 1600 device px on the tablet at 200 %, 800 at 100 %;
// the fraction per device px is 1 / level_travel_px(). A gesture length, a
// whole W like the three thresholds above (his "round each up" of the same
// day); floor 1, so a small scale never zeroes it. The drag is RELATIVE on
// it — the fraction at the press plus the motion over this length, the
// square-law taper turning it into dB (level_db_from_travel, app_state.h) —
// so a fine level needs a long stroke and a coarse one a short one, the pen
// covering 0 to -60 dB in about three quarters of the well's width. Its
// one reader is the drag (GuiInputHandler::update_level_press_motion,
// input_pointer.cpp).
inline constexpr double kLevelTravelW = 800.0;
inline int level_travel_px() {
    return scaled_px(kLevelTravelW, 1);
}

// (THE HOVER FADE IS RETIRED — architect 2026-10-02: "hover is awkward with
// pen and sometimes flickers, ok to drop it"; Windows 95 painted no hover
// face. Breeze's hover animation stood here from 2026-09-27 — the HoverFade
// edge-and-level model, its tick, kHoverFadeMs / kHoverFadeSteps and the
// menu pill's kMenuPillHoldMs hold — and it drove paint alone, so it went
// whole with the hover faces it softened, and the stored hover bits that only
// those faces read went after them (the roster's, the dialog buttons' and
// the folder overlay band's). The pointer walks stay for what still reads
// them: the tooltips (the menu row's hover switch went with the row,
// 2026-10-21). The record is in git history.)

// (THE DROPDOWN'S METRICS — Windows 95's popup menu at the Windows pixel,
// architect 2026-10-02: the plain raised two-line frame, one W of margin, the
// 17-W items touching, the etched separator's 3-W block margins, the 22-W
// label and right pads and the 9-W accelerator gap — RETIRED 2026-10-21 with
// the Settings popup, the last pull-down: the settings editor completes the
// keys on Tab, settings_editor.h. Git history.)

// (THE POPUP LISTS' SCROLL ROAD — architect 2026-10-08 ~11:20, "expand the
// drop-downs so we can scroll up and down": a list popup placed by the
// window's room, scrolling by row when its rows outgrew it, with Windows'
// vertical scroll bar inside it at its right — its placement, its bar's
// geometry and picture, its input and its wheel — IS REMOVED, architect
// 2026-10-14: its last reader, the settings choice editor's list, went with
// the choice editor; the color picker's element list and preset menu, its
// other two, the same morning. The menu row's dropdowns never outgrew the
// window and took none of it. Git history.)


// (THE WAVEFORM'S VERTICAL INSET IS GONE — architect 2026-10-09, "remove":
// waveform_inset_px, a symmetric 6-W margin above and below the channels —
// once the retired tip-down playhead triangle's mask height, kept as "a clean
// stem-only band at the top before the samples begin" — and its unit
// kPlayheadUnitPx. Since the canvas became Cool Edit's that day the playhead's
// head and the cues' triangles stand in the ruler and the marker lane, above
// the canvas's frame, and nothing on the canvas needed the band; the two
// channels fill the canvas whole, as the original mock's did
// (waveform_channel_band, below). Git history.)

// (THE PLAYHEAD COLUMN'S OLD REACH, playhead_half_px — ± scaled_px(6) − 1,
// the retired tip-down triangle's footprint — bounded the playhead's cull and
// its narrow damage box until 2026-10-09, when Cool Edit's head made it
// narrower than the pixels it guarded (the head's then ten quanta against ± 15 at
// 275 %, so a slow move left the head's shadow column behind). Both now read
// the head's own painted columns, cue_triangle_half_w_px and
// cue_triangle_reach_right_px above, which also cover the scanner's and the
// cursor's one-W line centered on c. Git history.)

// (THE MONOSPACE EDITOR TIER IS GONE — row 7, 2026-08-01. EditorTextBox,
// render_editor_text_box, flag_chip_rect, flag_chip_width_px,
// editor_text_glyph0_x and the pre-first-paint metric seeds all served ONE
// surface by the end: the three bottom-strip editors' chip-shaped text box,
// measured in glyph counts times one advance. Those editors are SHAPED now and
// paint in paint_handler.cpp (in the bottom row's modal since 2026-08-13),
// publishing their
// caret geometry the way the flag editor does — measurement, paint and hit all
// off the same ShapedRun. Nothing in the tree measures text by counting
// characters any more.)


// Screen-coord rects of one rendered cue, keyed back to its marker index,
// emitted left to right. It is the PAINTED EXTENT — the label's box and the
// triangle (below) — because this stash has always been what the pixels
// show. IT IS CLIPPED TO THE WAVEFORM'S COLUMNS [0, w) as the pixels are
// (architect 2026-09-26, clip_hit_rect_to_waveform_columns, render.cpp): a
// cue cut off at an edge claims its visible part; the two cell boundaries
// stay where the painter put them.
//
// THE LABEL'S BOX SPANS THE TWO ITERATION BOUND CELLS TOO where they paint:
// each is a segment of the same label, so all of it is ordinary flag surface
// for press, drag and select. The two
// boundaries are the PAINTER'S own numbers, published rather than re-derived,
// because a second shaping pass could disagree with the pixels: the window x
// where the LOWER cell's segment box begins (`iter_lower_boundary_x`) and
// where the UPPER cell's begins (`iter_upper_boundary_x`), a press in the
// gap before a segment reading as the segment left of it. They are
// non-decreasing, and
// each collapses onto the label box's right edge when its segment did not
// paint (an absent segment is always the label's tail) — a cell-less label
// publishes both boundaries AT its right edge — so
// hit_test_flag_cell's walk (Upper first, then Lower, else Payload)
// can never answer a cell that has no pixels. EVERY PRODUCER SETS BOTH (the flag pass, the editor's riding run and the `h`
// view's diff flags). ONE READER, hit_test_flag_cell (app_state.cpp),
// whose MarkerCell answer the marker press reads for the addressed cell and
// the double-click seed.
//
// TWO PRODUCERS SINCE 2026-09-05, and the second is why this shape is a struct
// rather than a lane-pass local: the flag pass emits one of these per painted
// flag into AppState::flag_hit_rects, and the marker-lane EDITOR'S painter
// emits ONE MORE for the RIDING BOXES it paints beside its field — whichever of
// the marker's boxes stand to the right of the one being edited, under any of
// the three kinds (FlagEditorBox::riding_cells below). Both go through the same
// walk
// (topmost_flag_rect, app_state.cpp) and the same boundary idiom, which is
// what makes a press on a riding cell resolve to the same marker and the same
// MarkerCell a press on the resting one resolves to. A cold or absent run
// reads marker_index -1 with a zero rect, which contains no point.
//
// SINCE 2026-10-09 A CUE IS TWO RECTS (architect 2026-10-09: "the flag's
// press target becomes the union of the triangle and the label's box as
// painted"): `x, y, w, h` is THE LABEL'S BOX as painted — its opaque face
// box on THE WHOLE LANE'S ROWS (2026-10-09 ~21:00: a press anywhere in the
// lane's height over the label's columns is the label's — "right now it's
// too small to click"; cue_fill_h_px), from the chip's left edge (the
// column less the chip's half width) to one fill pad past its last
// segment's text, the occluding box the painter lays — and `tri_*` THE
// CHIP'S, the outlined staircase's top row's 2·R − 1 device px
// (cue_triangle_half_w_px, cue_triangle_reach_right_px; R cue_chip_rows,
// 2026-11-04) over its rows (cue_chip_h_px).
// Either may be empty (a cue with no label segment, a field
// standing in for the label, the
// riding cells' run, which carries no triangle). The containment owner is
// flag_hit_rect_contains below; the two boundaries are the label's
// segments' box edges.
//
// THE `h` VIEW'S RECTS NAME THEIR CUE'S HALF too (`diff_half`, architect
// 2026-10-26: "every marker gets a tab"): a nudged pair paints two cues and
// publishes two rects, both naming the flag's index, each its own half, so
// a click focuses the flag on the cue it hit (HistoryMode::focus_half;
// hit_test_history_diff_half, app_state.h). A single flag's rect names its
// one half, a same-frame pair's the removed (one cue, one landing); the live
// lanes leave the default, read by nothing.
// WHICH CUE OF AN `h`-VIEW DIFF FLAG (HistoryDiffFlag below): the removed
// line's, at then_frame, or the added line's, at now_frame. Two cues exist
// only on a nudged pair; on every other flag the one cue stands for both.
enum class HistoryDiffHalf : uint8_t { Removed, Added };
struct FlagHitRect {
    int    marker_index = -1;
    HistoryDiffHalf diff_half = HistoryDiffHalf::Removed;
    double x            = 0.0;
    double y            = 0.0;
    double w            = 0.0;
    double h            = 0.0;
    double iter_lower_boundary_x = 0.0;
    double iter_upper_boundary_x = 0.0;
    double tri_x        = 0.0;
    double tri_y        = 0.0;
    double tri_w        = 0.0;
    double tri_h        = 0.0;
    // THE LABEL'S TEXT ORIGIN (architect 2026-11-09, session 100: "the first
    // char on the left of the tooltip should align horizontally with the
    // first char of the marker"): the pen x the cue pass hands its FIRST
    // painted segment's run (paint_cues, render.cpp: the segment's box start
    // plus the fill pad), as painted, unclipped. `has_text_x` false where no
    // segment paints or the label's box lies off the waveform's columns, and
    // on the riding cells' run. ONE READER, the cue tooltip's seat
    // (marker_tooltip_box_rect, app_state.h); no hit reads it.
    bool   has_text_x   = false;
    double text_x       = 0.0;
};
// THE ONE CONTAINMENT TEST of a published cue (the label's box or its
// triangle), half-open on both axes like rect_contains.
inline bool flag_hit_rect_contains(const FlagHitRect& r, double x, double y) {
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) ||
           (x >= r.tri_x && x < r.tri_x + r.tri_w && y >= r.tri_y &&
            y < r.tri_y + r.tri_h);
}

// All rendering helpers take a Cairo context and pixel-space rectangles; they
// have no X11 or event-loop dependencies.

// The two ground fills, one per surface class: render_background erases
// CHROME in the theme's ground, render_canvas erases the WAVEFORM AREA in the
// `waveform_canvas` role (palette()). on_redraw calls the first
// over the whole exposed rect, then the second over the exposed part of the
// waveform area, so the canvas wins exactly the pixels the plate, the ground
// recolours, the playheads and the marker stems paint on — cold frames (no
// plate yet) included.
//
// render_canvas ALSO owns THE CANVAS'S LINES UNDER THE WAVEFORM (architect
// 2026-10-09; row 6's canvas paragraph owns the rule): after its fill, each
// channel's horizontal grid, the channels' bands the plate's own
// (waveform_channel_band) — no vertical grid. render_waveform_divider owns
// THE STEREO DIVIDER'S ROW across the area (architect 2026-10-16,
// waveform_divider: chrome on the canvas, through the sink), a pass AFTER
// THE PLATE and the center lines (its
// reason at its definition, render.cpp: a plate from before a resize stands
// until the worker republishes). render_center_lines owns THE
// CENTER LINES, OVER THE WAVEFORM (architect 2026-10-09 evening): each
// channel's zero row, one W in `center`, painted live after the
// plate and before the stems (build_canvas_list's order) — the canvas's
// lines stay these two painters' and the plate wears its two inks alone
// (WaveformPlateInks). The ground round the area is
// paint_canvas_column_frame's (paint_handler.cpp).
// THE THREE CANVAS PAINTERS PAINT THROUGH THE CANVAS SINK (2026-10-13,
// canvas_sink.h): the frame's draw list, the backbuffer's interior cleared
// and the GPU drawing them.
void render_background(cairo_t* cr, int x, int y, int w, int h);
void render_canvas(GuiCanvasSink& sink, const GuiRect& area);
void render_center_lines(GuiCanvasSink& sink, const GuiRect& area);
void render_waveform_divider(GuiCanvasSink& sink, const GuiRect& area);

// (THE RELIEF HELPERS — the Windows-95 edge family of 2026-10-02 and the
// one-device-px mitred ring that replaced it 2026-10-23 — are retired: the
// palette block's NO RELIEF AND NO RING.)
// One cell rect in `c` — a surface: the ground, a lane, a field, a frame's
// line (set_palette_source; every surface is flat since the flat look,
// materials.h).
void paint_cell_rect(cairo_t* cr, const GuiRect& r, GuiColor c);
// One cell rect of INK in `c` (set_palette_source): a key's face, a glyph's
// or a word's own cells. The same fill as paint_cell_rect, its path built
// after the source.
void paint_ink_rect(cairo_t* cr, const GuiRect& r, GuiColor c);

// THE DIRECT ROAD (2026-10-13): the context's own target as pixel words, and
// the clip it holds in device pixels, for a painter that writes the words
// cairo would leave instead of asking cairo for them — the cues' triangles
// (CueTrianglePass, one target per pass, cool_edit_paint.h), stating at its
// site why its words are cairo's. Taken only where such a write can be cairo's
// result: the target an ARGB32 image surface with no group open on it, no
// device offset and unit device scale; the operator OVER; the matrix a pure
// integral translation (the platform hands on_redraw an untranslated
// context, platform_android.cpp's paint loop; the flag cache's by its
// band's origin row, upward, since
// 2026-10-13, FlagCache's head, paint_handler.h); the clip a list of
// integral rectangles (cairo_copy_clip_rectangle_list, in user space, so the
// translation carries them to device pixels; each is intersected with the
// surface). Anything else answers false and the caller takes cairo's road.
// The surface is flushed here; the caller marks it dirty after its last
// write, and around any cairo drawing it lets fall between its writes marks
// before and flushes after (CueTrianglePass lets none fall: a pass whose
// target is refused paints every triangle through cairo).
struct DirectPixelTarget {
    unsigned char* data = nullptr;
    int stride = 0;
    int dx = 0, dy = 0;               // user -> device: + (dx, dy)
    std::vector<GuiRect> clip;        // device pixels, inside the surface
    cairo_surface_t* surface = nullptr;
};
bool direct_pixel_target(cairo_t* cr, DirectPixelTarget& out);
// (THE DISABLED EMBOSS, THE WORD HALF — show_embossed_run, Windows'
// DrawState DSS_DISABLED: the word in Hilight one Windows px right and down
// under its Shadow copy, 2026-10-03 — RETIRED 2026-10-23 with no caller
// left: a disabled key dimmed its legend, and no other word is disabled;
// no key prints a word since 2026-11-02.)


// THE WAVEFORM AREA IS THE CANVAS: the plate, the canvas's lines, the
// lead-in ring and the zoom anchor's stem each span its columns (row 6's
// canvas paragraph), and every vertical line on it spans its whole height,
// the stereo divider's row included.
//
// THE STEREO DIVIDER (architect 2026-10-16, on his Wine captures of Cool
// Edit Pro 2.1 in stereo — the measured rows 521-523, the left join at
// x 208, the right at x 1872 — "ok to
// implement the bevel divider, but make sure it has no separation from the
// main chrome"): Cool Edit draws a stereo file as two wells, one per
// channel, with a seam between them; THE FLAT WELL'S SEAM IS ONE ROW OF THE
// GROUND, `ground` (Default's 242424), ONE W (hairline_px, the line rule of 2026-11-04;
// architect 2026-11-01 night, "THE MAIN PAGE IS SETTLED": "the stereo divider one 242424 row" —
// the hi-fi skin's light line, ground row and dark line, 2026-10-23 to
// 2026-11-02, retired), across the waveform area, which is the well
// (architect 2026-11-03), so it meets the ground of the margins on both
// sides with no separation. CHROME, NOT WAVEFORM:
// inert to every input (point_on_waveform_divider below). THE ROW STANDS
// INSIDE THE WAVEFORM AREA (every column mapping, hit test and damage box
// reads it as before) and comes out of the two channels' rows: the area's
// height less the divider, halved and floored, is each channel's band
// (below). Across the area's columns it is painted through the canvas sink
// (render_waveform_divider, after the plate and the center lines,
// build_canvas_list's order), so the GL canvas draws it from its list
// (canvas_sink.h's head).
// THE PLATE'S BANDS, THE GRID AND THE CENTER LINES stand inside the two
// bands and paint nothing on it; EVERY VERTICAL LINE CROSSES IT, THE DIVIDER
// BEING JUST ANOTHER GRID LINE (architect 2026-11-02: "the divider is just
// another grid line now"; "going below was a bevel metaphor - the line is
// now just a line") — the selection stems, the cursor, the lead-in
// ring's and the nudged pairs' sides, the scanner and the zoom anchor's
// stem each one solid line down the area's whole height, painted after the
// divider (build_canvas_list's order). On the tablet at 200 % and
// key_height 17 (its default) the canvas is 1085 rows, 243-1327 (main.cpp's
// lane table, re-derived 2026-11-04 night): the
// upper well's band 541 rows (243-783), the divider rows 784-785 (one W),
// the lower well's band 541 rows (786-1326) and THE SPARE ROW 1327, the
// canvas's last (1085 less the divider's 2 is odd; ONE CHANNEL'S BAND
// below).
//
// THE DIVIDER'S ROWS, area-local, for an area `area_h` rows tall at a
// divider of `divider_px` rows (waveform_divider_px live, the job's snapshot
// in the plate's worker — the worker reads no gui_scale): `y` its first row,
// directly under channel 0's band, `rows` its height.
inline int waveform_divider_px() {
    return hairline_px();
}
struct WaveformDivider {
    int y    = 0;   // the row, area-local
    int rows = 0;   // its height, device rows
    int h() const { return rows; }
};
inline int waveform_channel_band_h(int area_h, int divider_px) {
    const int h = (area_h - divider_px) / 2;
    return h > 0 ? h : 0;
}
inline WaveformDivider waveform_divider(int area_h) {
    const int rows = waveform_divider_px();
    return WaveformDivider{waveform_channel_band_h(area_h, rows), rows};
}

// ONE CHANNEL'S BAND, area-local rows: the plate's (render_waveform_to_cache_
// surface, its one layout) and the canvas's grid and center lines'
// (render_canvas), so the lines stand on the zero row the plate's bars
// straddle. EACH CHANNEL'S BAND IS ITS WELL'S INTERIOR (architect
// 2026-10-16, the stereo divider above): (area_h − divider_px) / 2 rows,
// floored, EXACTLY EQUAL — channel 0 [0, h) under the canvas's top frame
// row, channel 1 [h + divider_px, 2h + divider_px) under the divider. AN ODD
// REMAINDER IS THE CANVAS'S LAST ROW, inside the lower well under channel 1,
// where the canvas's ground paints and no ink reaches (the rule since the
// channels first filled the canvas, 2026-10-09: the two bands stay equal, so
// the plate's layout is one height). Its ZERO ROW is y + h / 2, the row
// render_waveform_columns' centre y + h · 0.5 floors to; its HALF HEIGHT H, the
// grid's (row 6's canvas paragraph), runs from that row to the band's edge.
// h is 0 only on a canvas under the divider and two rows.
struct WaveformChannelBand {
    int y = 0;
    int h = 0;
    int zero_row() const { return y + h / 2; }
};
inline WaveformChannelBand waveform_channel_band(int area_h, int divider_px,
                                                 int channel) {
    const int ch_h = waveform_channel_band_h(area_h, divider_px);
    return WaveformChannelBand{channel == 0 ? 0 : ch_h + divider_px, ch_h};
}

// A CANVAS ROW-LINE'S FIRST ROW, area-local: a line `t` rows thick standing
// on area-local row `row` covers rows [row − (t − 1) / 2, … + t) — the grid's
// and the center lines' one row rule (canvas_hline, render.cpp, where its
// deliberate top-row asymmetry is recorded), before the cut to the area.
inline int canvas_hline_first_row(int row, int t) { return row - (t - 1) / 2; }

// THE LOWER CHANNEL'S CENTER LINE'S TOP ROW, window y — the line
// render_center_lines paints for channel 1, on its band's zero row through
// the one row rule above (one W thick, waveform_line_px), or -1 where the
// band has no rows (the line is not painted). The canvas ALWAYS HAS THE TWO
// WELLS (2026-10-16): both center lines paint with or without a source, mono
// or stereo (build_canvas_list's two branches), so the line stands whenever
// the canvas has rows. ITS ONE READER is the on-screen keyboard's height
// floor (onscreen_keyboard::edge_pad_px, architect 2026-11-08). On the
// tablet at 200 % the canvas 243 .. 1327 at key_height 17 puts it on rows
// 1056..1057, and 259 .. 1311 at key_height 25 on rows 1048..1049.
inline int lower_center_line_top_row(const GuiRect& area) {
    const WaveformChannelBand band =
        waveform_channel_band(area.h, waveform_divider_px(), 1);
    if (area.h <= 0 || band.h <= 0) return -1;
    return std::max(area.y, area.y + canvas_hline_first_row(
                                         band.zero_row(), waveform_line_px()));
}

// THE TWO WELLS' ROWS, window y, [y0, y1): the upper well's from the
// area's first row to the divider, the lower well's from under the divider
// to the area's last row (its band and the spare row) — the INPUT's
// geometry (point_on_waveform_divider below, the lower-well test of
// input_pointer.cpp); no painter reads it, every vertical line running the
// area's whole height (the divider's rule above). An empty well has
// y1 <= y0.
struct WaveformWellRows {
    int y0 = 0;
    int y1 = 0;
};
inline std::array<WaveformWellRows, 2> waveform_well_rows(const GuiRect& area) {
    const WaveformDivider d = waveform_divider(area.h);
    const int top = area.y + std::min(d.y, area.h);
    const int bot = area.y + std::min(d.y + d.h(), area.h);
    return {WaveformWellRows{area.y, top},
            WaveformWellRows{bot, area.y + area.h}};
}

// THE DIVIDER IS CHROME, INERT TO EVERY INPUT (architect 2026-10-16:
// "chrome is not waveform, for any surface"): window row `y` lies on the
// divider's rows across `area` — between the upper well's last row and the
// lower well's first (waveform_well_rows, so the press and the paint read
// one geometry, waveform_divider). A press, a touch-down or a pen-down there
// answers exactly as the canvas column's margins do: nothing
// (point_on_nav_surface, input_pointer.cpp, the one reader for every claim
// — the press and the pen's and the finger's pan zone). A gesture that began
// in a well keeps running when it crosses these rows (the drags read motion
// alone; the touch navigation's surface is decided at its start,
// apply_touch_nav_update). Columns are not tested: the caller asks inside
// the area.
inline bool point_on_waveform_divider(const GuiRect& area, int y) {
    const std::array<WaveformWellRows, 2> w = waveform_well_rows(area);
    return y >= w[0].y1 && y < w[1].y0;
}

// THE COLUMN MAPPING BASIS — the plate's viewport start, its LATTICE ORIGIN,
// the PAINTER's samples-per-pixel, and the plate width.
//
// Columns are mapped on THE AUTHORING LATTICE, not by interpolating between
// integer viewport endpoints. `spp` is painter_samples_per_pixel's value (its
// one owner) — the very q that clamp_viewport_start snaps the viewport onto,
// and `origin` the camera's lattice origin (AppState::viewport_lattice_origin,
// which owns the three rules), so every RESTING viewport is a point of its
// lattice, grid(k) = origin + nearbyint(k*q). The renderer recovers that k
// and maps global column c to the display-domain edge
//     edge(c) = origin + nearbyint((k0 + c) * spp),
//     k0 = nearbyint((vp_start − origin) / spp)
// which is literally the chokepoint's own grid(k0 + c). Recovering k0 uses
// the same expression the snap itself uses to produce the rest
// (nearest_viewport_grid_point), so the two agree by construction rather than
// by coincidence.
//
// THE ORIGIN, AND WHO SETS IT (architect 2026-10-31). EVERY LANDING rests at
// origin 0 — the frame-0 lattice, the picture byte for byte what it was before
// the origin existed. THE GESTURE'S ZOOM FRAMES (the pinch, the pen's
// one-finger zoom) set the origin so the requested start IS the rest: the
// pivot's held frame then sits at its held column to ONE FRAME, where the
// frame-0 lattice left it up to half a column off with a fresh residue every
// zoom frame (measured on the tablet 2026-10-31, ±half a column a step: the
// peaks near the fingers bubbled up and down). THE GESTURE'S PAN FRAMES keep
// the current origin, so the pan paragraph below holds on whichever lattice
// the camera stands on, and a pan after a pinch keeps the pinch's lattice.
//
// WHY IT MUST BE THE LATTICE (the shimmer fix). Interpolating edges as
// vp_start + span*c/W from integer endpoints gives each column a rounding
// residual that CHANGES when the viewport moves. A one-pixel pan therefore did
// not hand each column its neighbour's exact span: nearbyint ties flipped,
// pyramid-bin membership flipped with them, extrema jumped, and the tip
// segments amplified every flip into both neighbours — two screenshots one
// grab-pan pixel apart differed in most of their columns. On the lattice a pan
// by n pixels moves k0 by exactly n at an unchanged origin, so column c simply
// becomes what column c+n was: same k0+c, therefore the same double, therefore
// the same nearbyint, the same bins and the same extrema. Bit-identical
// shifted pixels, in both views — in target view the lattice lives in the
// display domain and the map consumes the same doubles.
//
// Every viewport the plate is handed lies on its own lattice (the chokepoint
// snaps every write, the invariant at AppState::viewport_lattice_origin), so
// the recovery is exact; a pan moves by whole columns of its lattice, as
// Ableton pans by whole columns too.
struct WaveformBasis {
    long long vp_start   = 0;   // plate viewport start, display domain
    long long origin     = 0;   // its lattice's origin (whole frames), 0 at every landing
    double    spp        = 0.0; // painter_samples_per_pixel — the lattice step
    int       full_width = 0;   // full-plate column count
};


// THE OPAQUE PIXEL WORD for a palette colour: cairo ARGB32 is a native-endian
// 32-bit quantity, so (A<<24)|(R<<16)|(G<<8)|B written as a uint32_t is correct
// on any byte order, which indexing bytes would not be. Channels are
// PREMULTIPLIED, as ARGB32 requires; at full alpha that is the colour itself.
// The channel bytes round with std::nearbyint, the project's rule — vacuous for
// the exact n/255 hex palette, decisive only if a mixed colour ever ties. The
// one word owner for every image the painters write as words: the GPU
// canvas's sink (canvas_sink.cpp: its records and the plate's two inks),
// handed the color already converted for the window (display_color); a
// bound icon's color (icons.cpp) takes the authored word's low 24 bits, its
// raster converted after.
inline uint32_t argb32_opaque_word(GuiColor c) {
    return (UINT32_C(255) << 24) |
           (static_cast<uint32_t>(std::nearbyint(c.r * 255.0)) << 16) |
           (static_cast<uint32_t>(std::nearbyint(c.g * 255.0)) <<  8) |
           (static_cast<uint32_t>(std::nearbyint(c.b * 255.0)));
}

// THE PLATE'S RENDER, render_waveform_columns (declared below): one channel's
// waveform over `area`, which holds the `area.w` columns starting at GLOBAL
// column `col0` — i.e. the column sub-range [col0, col0+area.w) of `basis`.
// The one caller (render_waveform_to_plate_columns, waveform_cache.cpp) is a
// full-plate render and passes col0 = 0 with area.w == basis.full_width.
// When `warp_frame_map` is null (source view) the basis viewport is
// source-frame and each column reads `audio.get_peak_range`
// directly. When non-null (target view) it is target-frame: each column's
// [t0, t1) is translated to source-frame via `map_target_to_source` before the
// pyramid read, producing the deformed-waveform display.
//
// THE SILHOUETTE IS ONE FILLED CONTOUR THROUGH THE COLUMN CENTERS, WITH A
// FULL-INK CORE (architect 2026-10-30, on his glass against Ableton). Each
// column reduces to two TIPS in float rows — its raw maximum as the top
// tip, its raw minimum as the bottom — QUANTIZED TO QUARTER ROWS (below):
// the column's own extent [T, B) in subrows is its BAR, kept whole in full
// ink (THE CORE: a lone spike stands flat-topped at its own height, never
// shaved toward its neighbors' mean), and THE CONTOUR joins it to its
// neighbors — at each of the column's four subcolumns the top and bottom
// interpolate linearly between this column's tips and the NEARER
// neighbor's, from center to center, so a steep rise is a diagonal between
// the two bars rather than a gap. THE WHOLE SHAPE (the bars' union with the
// contour) IS GROWN BY THE `waveform_grow` KEY on every side — a disc
// dilation of that many quarters of a device px on the 4x grid, 0.5 by
// default (architect 2026-10-31; the key's reason at kWaveformSubcells: the
// plate is the one thing on the canvas not scaled with gui_scale, so the
// grow is its own device key for good) — and each device pixel takes EXACT
// COVERAGE, its set subcells over sixteen; the lit inner bar is the grown
// inner shape FILLED SOLID in the core (THE CORE, below), antialiased with
// its shape.
// The coverage is blended SOURCE-OVER IN BYTE SPACE with what lies beneath
// (the GL blend, kListFragmentShader).
// NO TIP RULE (his words: "I don't look at the tips, the troughs and their
// crossings of the zero line"): a tip's pixel is its exact coverage like
// any other. The dark lamp's raw bar takes the same rule.
//
// THE ANTIALIASING RETURNED 2026-10-30 IN ANOTHER FORM: exact coverage of
// one grown shape, computed on the GPU from the column bounds, the lines of
// the canvas untouched (the grid, the center lines, the divider, the stem,
// the playhead, the scanner, the anchor stay whole cells, one W since
// 2026-11-04, waveform_line_px), so placement reads as before —
// the full-ink core keeps every column its own extent. What the 2026-08-01
// deletion took (the Wu tip polylines, their max-coverage compositing, the
// fractional boundary rows, the coverage table, the edge halos) stays in
// git and in docs/engineering/waveform_antialiasing_retired.md; none of it
// is this renderer.
//
// THE TIPS IN QUARTER ROWS, AND THE QUARTER-ROW NEVER-FADE FLOOR AT THE 4x
// GRID: each tip, clamped into the band's rows, becomes T = nearbyint(4 ·
// top) and B = nearbyint(4 · bottom), the bar the subrows [T, B − 1] (the
// center rule: subrow k's center k + 0.5 lies in [T, B] iff T <= k <= B − 1).
// A bar whose interval is under a quarter row (B == T — digital silence at
// the zero row is the archetype) TAKES ONE SUBROW (kWaveformFloorSubcells,
// a quarter device px: its reason at that constant), B = T + 1, T pulled
// down one where it sat on the band's last edge so the subrow lies inside
// the band; the disc dilation then makes a line of it — at the default grow
// five subrows, nonzero coverage over two device rows and about one and a
// quarter device px of summed coverage (20 of 16 subcells) at any tip
// phase, Ableton's measured silence line. Nothing can fade out or vanish.
// The floor is needed because an interval under a quarter row holds no
// subrow center and would otherwise rasterize to nothing; the contour needs
// none, since it only ever joins two bars that exist.
//
// PAN INVARIANCE: a column's tips are a pure function of its own (k0+c)
// span — the AUTHORING LATTICE below is what makes that span depend on the
// global index alone — and its picture reads the tips of the columns
// within three of it (the contour toward its two neighbors and the
// dilation of up to two device px across them), so two full
// renders of the same columns at the same basis agree exactly. At the plate's two side edges the missing
// neighbor is the column itself for the contour (the shape runs flat to the
// edge), so an edge column can differ from the same audio rendered
// interior; every plate is a full render since the shift-and-strip pan's
// retirement, so no plate ever stitches the two.
//
// THE COLUMNS ARE THE PRODUCT (2026-10-13; the one road since 2026-10-30):
// the function writes no pixel. Its product is each column's four
// quarter-row tips (WaveformColumnRows below), and the GPU rasterizes them
// (kListFragmentShader, gl_canvas.cpp; canvas_sink.h's head) — the
// contour, the dilation and the coverage are the fragment
// shader's, from the bounds texture, per pixel. THE PLATE IS THE ONE RECORD
// THAT BLENDS: its fragment writes the premultiplied source of the two
// coverages (the ink at the union's coverage less the inner's, the core at
// the grown inner's, the alpha the union's — the inner over the outer where
// they overlap is the magnification rule's order; kListFragmentShader's
// head owns the rule) and the GL blend lays it source-over on the
// canvas and the grid beneath; where a bar covers whole (coverage one) its
// word lands exactly, and where nothing covers the
// fragment discards and the plate stays transparent. RETURNS false where it
// refuses without bounds (a degenerate area, basis or pyramid, or a band
// that misses the plate), `out` then unspecified; `out.whole` says whether
// the bounds cover `area` whole, and a band is drawn only then
// (GuiPlateColumns::Band::stored).
//
// The inks' words are PREMULTIPLIED ARGB32 (argb32_opaque_word), built by the
// sink from the published inks (GuiCanvasSink::plate); the shader scales each
// by its share of the pixel (the ink the union's coverage less the inner's,
// the core the inner's; kListFragmentShader's head), so there is one word per
// colour rather than a table.
//
// THE INKS (WaveformPlateInks, the `waveform_ink` and `waveform_core`
// roles AS THE WINDOW TAKES THEM — already converted, waveform_plate_inks)
// ride the plate's publication, not this function: the plate paints in the
// ink with the lamp dark, and lit the outer in the ink with the inner in
// the core over it (row 6) — it
// is trim-agnostic, and the out-of-trim dim that
// once masked a second color through this alpha is retired, the trim bar
// spanning the window being the whole inside-the-window signal now. Its
// alpha is the coverage: opaque inside the bars, fractional on the grown
// fringe, transparent in the gaps (2026-10-30).
// THE GAPS SHOW THE GROUND render_canvas lays under the plate: the plate
// carries no ground colour, so the canvas has that one painter and the cold
// frame before the first plate shows the same ground. Nothing recolours a
// plate pixel after the plate's record (the sweep's region highlight, which lifted the ground
// in the gaps and the set pixels over its span, retired 2026-10-03, architect:
// "the trim bar is enough"); no reason left needs the gaps over an opaque
// canvas-coloured plate, and the plate stays as it is.
//
// THE VISUAL MAGNIFICATION is `gain_or_null`, and the lit plate is TWO BARS
// per column from ONE peak read, both through the expander, the outer
// through the leveler and the inner through the compressor (architect
// 2026-09-25):
//
//   OUTER (painted first) = raw x g x E, in the ink — the
//                           levelled, expanded bar;
//   INNER (painted over)  = raw x c x 1/2 x E, a solid fill in the core —
//                           the source's bar DOWNWARD-COMPRESSED.
//
// g is the leveler's gain at the column's centre source frame
// (waveform_gain_at), c x 1/2 the inner bar's scale there
// (waveform_inner_scale_at: c the compressor's, the inner bar's stage on the
// window loudness L, and 1/2 the flat foreground gain folded into it at the
// derivation — waveform_gain.h owns them all), and E the expander's largest
// multiplier over the working columns the column spans
// (waveform_expander_multiplier_over) — ON BOTH BARS, so the gap between
// them is g / (c x 1/2), a pure function of L, and the inner never stands
// out of the outer IN HEIGHT ABOUT THE CENTRE ROW (in pixels a column wholly
// on one side of zero is the exception: THE ALIGNMENT, below). Each bar's
// tips are CLAMPED to [-1, 1] before they become rows, and each keeps the
// never-fade floor at both lane edges (a bar clamped whole to an edge is
// that edge's last subrow, grown to a line). The composite
// puts the inner over the outer, so where the two overlap the inner wins: the
// reading is the source's bar, set off from the levelled bar behind it by
// the core's own tone over the ink (THE CORE, below), the core's relative
// thickness the loudness — at or under the
// compressor's threshold the core is half the raw bar (x E), over it thinner
// by its ratio.
// NULL (the dark lamp) draws the raw bar alone in the ink at scale 1,
// no inner and no core, under the same contour, grow and coverage.
//
// THE CORE (architect 2026-11-04, mock f1, reversing the lit outline of
// 2026-09-27) — THE GROWN INNER SHAPE (the inner bars of all columns with
// their contour, dilated by the `waveform_grow` key) FILLED SOLID in the
// `waveform_core` role, LIVE'S SurfaceBackground, THE KEY FACE, on every
// theme (architect 2026-11-07, session 83, his pick i4 of tmp/s83 on real
// MAGNIFY-lit captures, Default and Immaterial judged; the one owner
// theme_ask.h's DERIVATION): his ask a lighter core from a tone the palette
// already holds, with no antialiasing halo — the candidates mocked the ground
// (SurfaceArea), the lane (Desktop), the grid, the key face and the canvas.
// On Default 363636 over the 191919 ink, ONE STEP UNDER THE 393939 CANVAS,
// so the center line (the ink, over the plate) reads hard across the core —
// ruled fine (his "yes", session 83); Immaterial 2F3138. Mock f1's word
// stands: "the absence of it would indicate a quiet section". THE TONE'S
// RECORD: 1A1A1A, one level over the ink, picked 2026-11-04 morning on mock
// f1 ("1e is already quite bright … just a tiny amt of brightness is all
// that's needed" — f1's dummy inner), read nearly invisible on the real
// compressed bar, because f1's dummy inner was a smooth band a third of
// the plate's height and one level over the ink needs a large contiguous
// area to register; 1C1C1C, three levels, ruled in session 33 on mocks
// m1..m7 of tmp/s33 (core_tones.py, the real capture recolored one tone
// each: "1c works"), where 1D1D1D was declined for not being a Live tone;
// 1E1E1E, five levels, his pick i1 of tmp/s38 that night on the real lit
// capture, read from Live's ControlBackground on every theme 2026-11-06
// (session 80: his pick was that role's number on Default) until the key
// face replaced it. The core lies over
// the outer bar's ink: the composite is area-correct, the core over the
// grown inner's coverage and the ink over the rest of the union's
// (kListFragmentShader's head), so the inner keeps its shape and its
// antialiased fringe and draws no line. No
// inner bar is ever clipped at the lane's edge — its tips are at most half
// scale (raw at most 1, the compressor's scale and the expander's
// multiplier at most 1, the foreground gain one half; kForegroundGain's
// static_assert, waveform_gain.cpp). Each channel's lane is its own shape;
// the other channel's pixels are never a neighbour. THE ALGORITHM IS THE
// SHADER'S (kListFragmentShader, gl_canvas.cpp), per pixel from the tips of
// the columns within three of it, no 2D scan. A loud passage's outer clips
// flat at the lane's edges while the inner still shows its compressed
// height inside it.
//
// BOTH LIT INKS ARE FLAT (architect 2026-09-26). THE CORE SHADE BY THE BAR
// GAP (2026-09-25, one commit — the inner's ink a per-column blend toward
// kdenlive's teal, linear in dB of g / c) IS STRUCK: "it doesn't work". A
// per-column shade on either bar is ruled out again, as the shade by the
// leveler's gain on the old background bar (below) was before it.
//
// SUPERSEDED RECORDS: the lit plate's bar behind the source's wore a
// per-column SHADE by the leveler's gain for one day (2026-09-24, struck
// 2026-09-25: the architect preferred flat colours to reading a shade), and
// for one day more (2026-09-25) the two bars took two flat device levels,
// the raw bar at +2 dB in the bright ink over the levelled bar at -2 dB in
// the faint one, `-inf` leaving a bar out — struck with the compressor, both
// bars at 0 dB by construction (the record of the keys is at
// device_config.h).
//
// THE ALIGNMENT IS EXACT BY CONSTRUCTION: the two bars share the lattice, the
// column's [s0, s1), the pyramid level and the one read. THE OUTER IS A
// DILATION ABOUT THE CENTRE ROW, NOT A HALO ROUND THE INNER: a column straddling zero has
// its outer containing the inner; a column wholly on one side of zero (low
// material at working zoom) has its outer pushed outward, with a gap between
// it and the inner. Intended — that is what a true magnification looks like.
// THE COST is one extra bar's rows and one lookup (the scale) per column — the read, the map walk and the gain lookup are shared; no second pyramid, no second
// plate and no new cache field (waveform_gain_fingerprint flips with the
// lamp). The core adds nothing here: the inner's two tips ride the same
// record and the GPU fills them.
//
// THE GAIN IS A FUNCTION OF SOURCE TIME: the continuous curve derived from the
// source at load (WaveformGainCurve, waveform_gain.h, which owns the rule).
// A COLUMN TAKES THE GAIN AT ITS CENTRE SOURCE FRAME, (s0 + s1) / 2 — one
// evaluation per plate column (waveform_gain_at, linear between the curve's
// hops), and the compressor's scale takes the same centre frame
// (waveform_inner_scale_at, linear in scale likewise). s0 and s1 are the same integers the peak read takes, pure functions
// of the GLOBAL column index (the authoring lattice below) in both views —
// target view maps the column through the warp map to its source span first —
// so the lookup is pan-invariant by construction and nothing forks on the
// view.
//
// THE CENTRE RULE IS AN APPROXIMATION AT THE COARSE ZOOMS. A coarse plate
// column covers many working-zoom columns whose gains differ; it takes the
// curve's gain at its centre source frame and applies it to the min/max
// reduced from RAW peaks, so where the gain changes inside the column (the
// curve moves on the window's scale) the bar's height
// differs from the height a per-working-column gain applied before the
// reduction would give. The error is not small: on the 40th at whole-piece
// zoom (1920 columns, ~0.30 s per column) a column at 122.3 s paints 0.53 by
// the centre rule against 0.87 gained-before-reduction, and at 640 columns
// (~0.90 s per column) one paints 0.97 against 0.34 (normalized peak
// estimates on the peak measure, measured 2026-09-23). The
// exact alternative is a second pyramid reduced over the gained samples —
// immutable with the source like the curve, so the lamp would select between
// the two pyramids rather than rebuild one — at the memory of a second
// pyramid. RULED (architect 2026-09-23): the centre rule stands and no gained
// second pyramid is built — placement is never done at a coarse zoom, so the
// approximation there costs nothing the plate is used for. At working zoom
// (one working column per plate column against a hop of whole columns: 46
// frames against 4416 on the tablet) the two agree.
//
// THE EXPANDER'S MULTIPLIER rides the same pointer (the curve's
// `expander_multiplier`, one per working-zoom column, waveform_gain.h owns
// the stage): a plate column spanning source frames [s0, s1) takes the
// LARGEST multiplier — the smallest reduction — over the working columns the
// span covers (waveform_expander_multiplier_over, constant time through the
// curve's range-maximum index since 2026-10-13, no pow), and
// both tips of BOTH lit bars take it beside the bar's own scale (the gain on
// the outer, the compressor's on the inner) before each bar's one clamp. At working
// zoom that is the column's own reduction; coarser, it is the one choice
// under which a bar is never shorter than any member's own expanded bar, so
// an onset is never dimmed by the dip before it at any zoom. It applies
// exactly where the gain does: NULL (the dark lamp, in either audio view) is
// raw, and an empty array is the identity. In target view [s0, s1) is the
// plate column's MAPPED source span, so a tempo-compressed column covering
// several working columns takes the largest multiplier among them, as a
// coarse source-view column does.
//
// IT IS A PICTURE GAIN AND NOT AN AUDIO ONE. Nothing downstream of this
// function is audio: the plate is pixels, playback
// reads the sample buffer at its own level, and no render input is derived from
// this parameter anywhere.
//
// It is a PARAMETER rather than a read of app state so this primitive stays
// free of it (the worker thread renders from a job snapshot; the curve itself
// is immutable once its derivation is ready — GuiAudio::gain_curve — so the job
// carries only whether to apply it).
// NULL is the untouched picture, gain 1.0 everywhere.

// ONE PLATE COLUMN'S TIPS (2026-10-13; quarter rows since 2026-10-30), in
// the plate's own rows times kWaveformSubcells
// (subrow 0 the top of the canvas's first row): the outer (or dark) bar's
// subrows [o_top, o_bot − 1] and the lit inner bar's [i_top, i_bot − 1] —
// the inner ABSENT (the dark lamp) is i_top = i_bot = 0, and present iff
// i_bot > i_top (a present bar always has bot >= top +
// kWaveformFloorSubcells, the quarter-row floor). The GPU rasterizes them (kListFragmentShader, gl_canvas.cpp): the
// contour toward the neighbors, the `waveform_grow` key's grow and each
// pixel's exact coverage are the fragment shader's, the
// rule at render_waveform_columns' head.
struct WaveformColumnRows {
    int o_top, o_bot;
    int i_top, i_bot;
};
// ONE CHANNEL BAND'S COLUMNS as render_waveform_columns produced them: the
// region the bounds cover, rows [y_lo, y_hi) and columns [col_lo, col_hi)
// of the plate (the band clamped to it), `cols` one entry per column from
// col_lo, and whether that region is the asked area whole (`whole`).
struct WaveformBandColumns {
    int  y_lo = 0, y_hi = 0;
    int  col_lo = 0, col_hi = 0;
    bool whole = false;
    std::vector<WaveformColumnRows> cols;
};
// THE COLUMN BOUNDS' ONE PRODUCER (2026-10-13; the contract is THE PLATE'S
// RENDER above): `area`, `col0`, the basis, the gain and the map, and the
// plate's size the band is clamped to (plate_w x plate_h). The core's
// color is no input: it rides the plate's publication (THE INKS above).
// False where it refuses — a degenerate area, basis
// or pyramid, or a band that misses the plate — `out` then unspecified.
// Thread-agnostic: the waveform worker or the synchronous rebuild
// (waveform_cache.cpp).
[[nodiscard]]
bool render_waveform_columns(GuiRect area,
                             int col0,
                             const GuiAudio& audio,
                             int channel,
                             const WaveformBasis& basis,
                             const WaveformGainCurve* gain_or_null,
                             int plate_w,
                             int plate_h,
                             const std::vector<WarpFrameMapSegment>* warp_frame_map,
                             WaveformBandColumns& out);

// THE PLATE AS COLUMN BOUNDS (2026-10-13, the GL canvas; the cache's
// slot at WaveformCache::columns, paint_handler.h): the two channel bands of
// a plate width x height, each `stored` exactly where its bounds cover the
// band whole (WaveformBandColumns::whole) — a refused or partial band draws
// nothing — and the plate transparent elsewhere (the rows outside both
// bands, an odd plate's last, included). `rendered` stands once the producer
// has filled the slot: set by the producer, cleared by the cache's destroy.
// THE PLATE'S OWN REVISION (2026-10-14): `revision` names the live slot's
// bounds — a process-wide counter the GUI thread stamps at every PUBLICATION
// of a plate (next_plate_revision, waveform_cache.cpp: the worker's
// completion swap and the synchronous rebuild, the only two writers of the
// live slot), never 0 once published, and never repeated in the process, so
// one revision names one set of bounds whatever project set or cache
// produced it. An owned number rather than an address: the slots swap, and
// a pointer would name the slot, not the plate in it. The pending slot's
// revision is stale and read by nobody; the producer never writes it.
// Its readers are the GL canvas's: the sink copies the bounds only
// when the revision moved, and the GL canvas re-uploads its bounds texture only
// when it moved (GuiGlCanvas::upload_list).
struct GuiPlateColumns {
    bool     rendered = false;
    int      width  = 0;
    int      height = 0;
    uint64_t revision = 0;
    struct Band {
        bool                stored = false;
        WaveformBandColumns columns;
    };
    Band band[2];
};

// Draws a waveform_line_px()-wide vertical LINE down the CANVAS's whole
// height (row 6's canvas paragraph; the stereo divider's row crossed,
// architect 2026-11-02, waveform_divider's rule), at the painted column `col`
// (offset from area.x; playhead_pixel_x / scanner_pixel_x's, the nearest
// lattice point on the plate basis's own lattice index, app_state.h), on
// it, [col − t / 2, col − t / 2 + t) (the line rule: the
// even line straddles the frame), in `color`,
// SOLID — the scanner (Cool Edit's playback cursor a solid line, measured
// 2026-10-09) and the resting cursor alike (architect 2026-11-04, Live's
// playhead line; the static tone below, the caller's palette read,
// paint_playheads) — painted
// straight over whatever it crosses — waveform ink included. No-op if
// outside; the line is gated on its own column and clipped at both edges,
// so it never leaks into an adjacent region.
//
// THE RESTING CURSOR IS ONE STATIC TONE (architect 2026-11-04 night, his
// pick n1 of four static tones, after the measured cost of a fading line —
// ~46 % app / ~53 % SurfaceFlinger at 90 presents a second, the present
// itself the cost: "seems like it's unavoidable"): a solid one-W OPAQUE line
// in the palette's `playhead_line`, Default's sRGB 42D2E0 — white 0.75 of
// the way toward the head's cyan, on the byte line from FFFFFF toward
// 03C3D5 that the four lossless one-px samples of Live's line (4AD3E0,
// 59D7E3, 85E2EA and DBF6F9) lie on; the head lightened a quarter of the
// way to white, every theme alike (architect 2026-11-10, session 103:
// "lets do 1/4 ... the new rule for all, no special cases") — its own pixel
// the same over the ink and over the canvas, stopped or playing; the
// ruler's head keeps its own `playhead_head` 03C3D5, the scanner its
// FFFFFF. NOTHING IS DAMAGED AT REST: the line moves only with the cursor,
// whose writers own the damage (playhead_invalidate_rect, main.cpp). A
// stepped fade was offered and not taken.
//
// THE LINE IS THE WHOLE FUNCTION (2026-08-02): the cursor's head is
// paint_ruler_row's (the `playhead_head` triangle in the ruler since
// 2026-10-09), and
// no strip lane is reachable from here.
//
// The former two-tone form (an `ink_plate` parameter carrying the displayed
// plate, whose alpha masked a ground-colored overdraw wherever the column
// crossed an opaque sample) is retired too: architect 2026-07-26, the notch
// retired with the polarity inversion — the contrast problem it patched is
// solved by the scheme, so that parameter went with it.
// Through the canvas sink (2026-10-13, canvas_sink.h), as is the anchor stem
// below.
void render_playhead(GuiCanvasSink& sink,
                     GuiRect area,
                     int     col,
                     GuiColor color);

// Draws the strip-drag ANCHOR STEM: a vertical line at the drag's pivot
// column `col` (window pixels within `area`, clamped here to [0, area.w-1]),
// spanning the canvas's whole height (the stereo divider's row crossed,
// architect 2026-11-02), solid, in the `scanner` role — the
// white of the stems that belong to the controls, the playhead wearing its
// own two roles (architect 2026-10-09; the palette block's
// playhead paragraph owns
// the rule).
// The anchor is
// the clamped column the strip-drag math pins each event — edge-included, so an
// edge-pinned anchor draws the stem exactly at the edge and the clamp becomes
// visible (the Ableton affordance). It paints ONE solid
// color straight over the waveform ink it crosses — the ink-notch overdraw and
// its plate parameter are retired (architect 2026-07-26, with the polarity
// inversion). The line is an aliased integer rect waveform_line_px() wide,
// on the column, [col − t / 2, col − t / 2 + t) (the line rule: the even
// line straddles the frame), clipped at both edges
// (waveform_line_cols).
void render_strip_anchor_stem(GuiCanvasSink& sink,
                              GuiRect area,
                              int col);

// (The cached marker-stem renderers render_markers / render_phaseresetmarkers
// are retired: marker stems are a live overlay,
// GuiPaintHandler::paint_marker_stems — EVERY selected enabled marker's
// column, painted from the flag painter's stash. Trim below is live too — the view bar in
// GuiPaintHandler::paint_trim, ahead of the playheads; trim has had no stem
// since render_trim_stems died, and no stem is cached anywhere.)

// The ONE trim bound-to-column geometry owner. Every consumer of a
// trim bound's pixel column funnels here, and there is ONE: the paint site
// (render_trim_flags' endcaps, bar and bridge gap — the waveform stem site left
// with render_trim_stems). The two hit sites (hit_test_trim_endcap's endcap
// rects, point_in_trim_bridge_span's bridge test) called it too until
// 2026-09-24, when they began reading what the painter PUBLISHES instead
// (TrimBarHit, below — strictly as-painted), so the columns they test are
// this owner's output by construction. It replaced five hand-copied
// `nearbyint` + `clamp(0, W-1)` formulas maintained "byte-identical" by
// comment discipline.
//
// PURE: all basis inputs are parameters — the collapse unifies the FORMULA. The
// live trim pass (GuiPaintHandler::paint_trim) calls with the DISPLAYED basis
// from item_viewport_basis (vp_start_frame/vp_end_frame/lattice_origin/area_w
// — the promoted mirror of the committed fp_vp span, its lattice + effective
// width; the column displayed_column_at's, on that lattice's own index) and
// `displayed_ms`
// mapped through displayed_or_live_target_map by displayed_trim_ms (the
// event-synchronized hit-geometry doctrine), and the hit sites read that
// pass's publication, so paint and hit are one geometry by construction.
// (Earlier the
// hit sites used the LIVE viewport, which split a hit from its painted pixels
// during an async plate-publish window; the promoted mirror closed that window,
// and the trim painter later joined the same basis when it went live.)
//
// The x_raw denominator is the PAINTERS' quantized-span form
// (vp_end - vp_start)/wave_w — the sixteenth-frame q exactly
// (viewport_end_sample) — NOT current_samples_per_pixel. The two are
// identical at whole zoom levels and differ by under a thirty-second of a
// frame per column at a fractional zoom rest; adopting it at the hit sites too (they
// formerly divided by spp) was the one deliberate byte change of the collapse
// and ALIGNED paint and hit exactly — the point of unifying them, and what the
// published stash now carries for free.
//
// EOF-WALL CLAMP (the one copy, formerly installed at three sites at once):
// an IN-VIEW bound's `col` clamps into the visible column range [0, wave_w-1].
// The inclusive END wall T-1 at full zoom-out rounds to column wave_w (one
// past the surface); left unclamped, the right-edge-anchored end CAP loses its
// bound-edge pixel to the lane clip. Clamping lands the wall on the last
// visible column so the cap stays fully visible on the bar's end.
// Begin/frame-0 already maps to column 0, unaffected.
// AN OFFSCREEN BOUND KEEPS ITS OWN COLUMN, past the edge (architect
// 2026-10-05, "it slides off the edge, clipped, like any other content"): its
// button stands where the bound is and the lane's clip cuts it, so a panned
// end leaves the view column by column instead of vanishing whole with the
// body growing into its room. The painter clips everything it draws to the
// effective width [0, wave_w), so the inert gutter never paints.
struct TrimBoundColumn {
    double ms;          // displayed-domain position (already mapped)
    bool   in_viewport; // ms in [vp_start, vp_end), the unrounded verdict
    int    col;         // the bound's column; clamped into [0, wave_w-1]
                        // in view (the EOF-wall clamp), raw off it
};
TrimBoundColumn trim_bound_column(double displayed_ms,
                                  long long vp_start, long long vp_end,
                                  long long lattice_origin, int wave_w);

// The BETWEEN-THE-ENDS column interval [lo, hi) (waveform-relative,
// half-open, EMPTY when hi <= lo), the ONE owner of the bridge, run by the
// painter (render_trim_flags) alone: the clipped interval is what the painter
// PUBLISHES as the pair drag's handle (TrimBarHit::bridge_lo / bridge_hi,
// read by point_in_trim_bridge_span) — the view bar's span between its two
// end zones (view_bar_grab_w_px). Both bounds must be set
// (callers gate). ONE RULE ON EVERY SIDE (architect 2026-10-05): lo is the
// begin zone's inner RIGHT edge (col + endcap_w) and hi the end zone's
// inner LEFT edge, exclusive (col - endcap_w + 1), whether the zone is on
// screen, part on it or wholly past an edge. In the NARROW case, where
// trim_endcap_rect stands the end zone right of the begin's, hi is under
// lo and the gap is empty; a window wholly off one side is
// empty or wholly past that edge. The +endcap_w inset is the ROOM a zone
// occupies. This interval is returned
// UNCLAMPED, offscreen columns included; it is NOT a drawn interval. The
// painter clamps it ONCE per use: the published bridge intersects it with the
// effective width [0, wave_w), so the inert non-multiple-of-16 gutter
// [wave_w, strip_w) answers no hit.
struct TrimBridgeGap {
    int lo;  // inclusive left column
    int hi;  // exclusive right column (empty gap when hi <= lo)
};
TrimBridgeGap trim_bridge_gap(const TrimBoundColumn& begin,
                              const TrimBoundColumn& end, int endcap_w);

// The source-frame -> displayed-domain mapping of the live trim paint pass
// (GuiPaintHandler::paint_trim), its one caller since the two HIT sites began
// reading that pass's publication (2026-09-24). Byte-identical to render.cpp's file-local
// frame_to_paint_sample for every reachable (non-negative) trim bound: in a
// mapped view the source frame is rounded once through map_source_to_target,
// then rounded again; the identity (null/empty map) path returns the frame
// as-is. A negative frame is guarded to 0 (unreachable — past-EOF is load-fatal
// and bounds are never negative — kept for exactness vs the prior hit code).
// The painter's one mapping owner, so a span's end is drawn — and, through
// the stash, grabbed — on its bound's image. `map` is null in source view
// (identity) and the item pixels' own map (displayed_or_live_target_map) in
// target view.
double displayed_trim_ms(int64_t frame,
                         const std::vector<WarpFrameMapSegment>* map);

// The ONE trim GRAB screen-rect owner (named for the endcaps the grabs were
// until 2026-10-09; the view bar's span's two END ZONES since —
// view_bar_grab_w_px's rule): the edge-anchoring rule lives here, run by the
// painter (render_trim_flags), which publishes each zone, clipped to the
// lane, for the hit test (hit_test_trim_endcap reads TrimBarHit, below).
//
// A trim bound is an EDGE, not a point: the begin zone's LEFT edge sits ON
// the begin column (rect left = strip_x + begin.col), the end zone's RIGHT
// edge sits on the end column (rightmost pixel = strip_x + end.col) — the
// span's own two ends. THE NARROW CASE (architect 2026-10-03): when the
// drawn width end.col − begin.col + 1 is under two zones — the columns read
// wherever they stand, on screen or off (architect 2026-10-05) — the begin
// zone keeps its column and the END zone is placed IMMEDIATELY RIGHT OF IT,
// edge to edge, so it alone overruns its column. The two rects never
// overlap.
//
// THE RECT IS THE ZONE, view_bar_grab_w_px() wide over the trim lane `row`'s
// whole height, and it is THE HIT BAND AS IT IS — no tolerance inflates it.
// `is_begin` picks which bound's zone. The rect may lie part or wholly past
// the lane's edge (an offscreen bound keeps its own column,
// TrimBoundColumn); the painter's clip decides what of it is on screen.
GuiRect trim_endcap_rect(bool is_begin, int strip_x,
                         const TrimBoundColumn& begin,
                         const TrimBoundColumn& end, GuiRect row);

// (render_trim_stems IS DELETED, architect 2026-08-01: the bar is the trim
// window's whole display. The record is git history.)

// THE TRIM BAR'S HIT STASH (architect 2026-09-24, strictly as-painted): what
// render_trim_flags last PAINTED as the view bar's span — its two end zones
// and its body between them — published by that painter into
// AppState::trim_bar_hit so the trim hits read the pixels rather than
// re-running the painter's owner chain on the live trim — the flag lane's
// stash doctrine (AppState::flag_hit_rects) carried to the trim lane.
// Everything is in SCREEN pixels. `lane` is the view bar's band (its two
// lines and its field), the y-gate of both hits. Each end (a TrimBarHitCap)
// is its zone's rect (trim_endcap_rect, over the lane's whole height,
// clipped to the lane's painted width, the hit band as it is — no
// tolerance), a zone sliding off an edge publishing its visible columns
// alone (architect 2026-10-05), and `painted` false for a zone wholly off
// the lane, which answers no hit. The two rects never overlap. The bridge is
// the half-open interval [bridge_lo, bridge_hi) between the two zones'
// inner edges, already clipped to the lane's painted width
// (trim_bridge_gap); empty when lo >= hi, as in the narrow case.
// `published` false is COLD — nothing painted, nothing grabbable.
struct TrimBarHitCap {
    bool    painted = false;
    GuiRect rect{0, 0, 0, 0};   // the end zone, clipped to the lane
};
struct TrimBarHit {
    bool          published = false;
    GuiRect       lane{0, 0, 0, 0};
    TrimBarHitCap begin;
    TrimBarHitCap end;
    int           bridge_lo = 0;   // screen x, inclusive
    int           bridge_hi = 0;   // screen x, exclusive
};

// Draws THE TRIM BAR — the whole trim lane but its first row (architect
// 2026-10-09, the program is Cool Edit; flat since 2026-11-01 night; the
// palette block's well paragraph for its look, trim_lane_h_px for its rows):
// the bar's two flat fills across the well's columns (no frame since mock
// z2, 2026-11-03), then THE SPAN — the trim window's columns, the begin
// column through the end column inclusive, no verticals, sliding off an edge
// like any content (architect 2026-10-05: an out-of-view bound reads as the
// window running on); no playhead (the well paragraph above, 2026-10-09
// ~14:30). All pixel-bound integer fills. The lane band is
// the `trim_bar` PARAMETER — the caller passes top_trim_row_area(app), and
// the band painted in is published as TrimBarHit::lane, the y-gate both
// trim hits read, so paint and hit take the band as one value;
// `waveform_area` is read for its `.x` and `.w` — the canvas's columns, the
// interior's and the span's (2026-10-09: the column's margins), `.w` also
// the column-mapping denominator — which are the well's columns (THE WELL IS
// THE WAVEFORM AREA, architect 2026-11-03); the lane's ground beyond is
// paint_trim's.
// `top_strip_area` is a validity guard only. Column placement is on the displayed viewport basis — `trim.begin` /
// `trim.end` are already in the displayed domain. The span has NO editable
// payload; it is a plain-press grab target only (trim is outside the
// selection system).
// PUBLISHES WHAT IT PAINTS into `out_hit` when non-null (TrimBarHit above):
// the lane, the span's two end zones and its body between them, from the
// very columns this pass fills — and a cold record on every early return.
// The CALLER decides whether this frame may publish at all
// (GuiPaintHandler::paint_trim passes null unless the damage clip covers the
// whole lane), so a narrow repaint cannot stamp the stash over pixels it did
// not redraw.
void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect trim_bar,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       long long viewport_lattice_origin,
                       const TrimRange& trim,
                       TrimBarHit* out_hit);

// The top-strip lane the cues occupy, exactly as the lane accessor reports
// it: `marker_lane` = top_marker_row_area, whose bottom edge is flush with the
// waveform top. The accessor delegates to strip_row_rect, the single
// strip-geometry owner, and takes AppState — which this module does not see, so
// the caller resolves it and passes it in. That is the point of the parameter:
// the cues and their hit rects land on the SAME band the empty-lane press
// gate and every other lane consumer read, whatever the strip's lane heights
// are. Kept as a struct rather than a bare GuiRect so the call sites
// that thread it through keep naming what they are threading.
// `columns_x` is THE WAVEFORM'S FIRST COLUMN in the surface's x (2026-10-09:
// the canvas stands the column's margin in from the window's side,
// waveform_area's x) — the origin the cues' columns are counted from and the
// clip's left edge (clip_to_waveform_columns).
struct FlagLaneRects {
    GuiRect marker_lane;
    int     columns_x = 0;
};

// ONE MARKER'S STEM ENTRY, as the flag painter publishes it: the window x of
// the column it stands on (the chip's apex column, a drag's moving column
// included). The painters publish one for EVERY SELECTED ENABLED LIVE CUE
// AND DIFF FLAG they walk whose column is a waveform column
// (resolve_flag_face's `stem`; a selected nudged `h` pair's two cues each
// at its own column), and THE ONE READER, GuiPaintHandler::paint_marker_
// stems, paints THE SELECTION STEM — a solid one-W `cue_outline` line down
// the canvas's whole height — on every entry (architect 2026-11-05, his
// pick a2 of tmp/s42, reversing 2026-11-04's coincidence stem), so the
// stem and its chip can never disagree about a column. The flag PAINTERS
// are the only producers — the two live columns', and the `h` view's diff
// lane, which replaces them wholesale while the mode stands. An UNSELECTED
// or DISABLED marker publishes NO ENTRY AT ALL (no stem, the playhead on it
// or not); a changed `h` pair is a live edit and publishes as a live cue
// (the ruling at render_history_diff_flags). Stems are pointer-inert (the
// seventh glass ruling), so the stash is paint-only and `marker_index`
// serves the painter's identity bookkeeping alone.
struct MarkerStem {
    int     marker_index;
    double  x;
};

// THE NUDGED PAIR'S RING (architect 2026-10-10 ~18:55 → 2026-10-26; solid
// 2026-11-04): the `h` view's changed pair whose two halves stand apart is
// TWO CUES JOINED BY THE RING ("they should be joined by the overlay ring"),
// its four sides SOLID ONE-W `cue_outline` LINES — the left side on the left
// cue's column `x0`, the right on the right cue's `x1`, both down the
// canvas's whole height, and the top and bottom runs along the canvas's
// first and last W of rows from the left side's line's first column through
// the right side's line's last (paint_marker_stem_runs; the phase-reset
// lead-in ring's form, paint_phase_reset_overlay_ring). Always shown,
// selected or not; a selected pair's stem on either cue stands on its side
// in the same color. The history lane's painter is the one producer
// (render_history_diff_flags; the live lanes publish none),
// paint_marker_stem_runs the one reader; a ring reaching past either edge of
// the waveform's columns is published and clipped at paint, its sides
// possibly off screen. It is promoted and dropped with the stem stash
// (AppState::marker_stems).
struct MarkerStemRun {
    int    marker_index;
    double x0;
    double x1;
};

// WHICH ONE SEGMENT OF WHICH ONE MARKER'S LABEL THE FLAG PASS DOES NOT PAINT,
// because an open marker-lane editor is standing in for it. THE ONE GRAPHIC
// MODEL, stated once here and applied to all three editors (architect
// 2026-09-05, on the tablet: "it just feels odd to have one nonvariant field
// in the middle ... the two editors on the opposite ends behaving one way and
// the bounds one in the middle behaving in a different way makes the whole
// thing seem hacked together"): THE EDITED SEGMENT IS SUPPRESSED IN THIS
// PASS, THE FIELD IS THE WIDTH OF ITS OWN CONTENT, AND EVERY SEGMENT TO ITS
// RIGHT RIDES THE FIELD'S RIGHT EDGE, painted there in its resting look — and
// published there — by render_flag_editor_box. (Boxes until 2026-10-09, the
// label's segments since; the model unchanged.)
//
// `cell` NAMES THE EDITED SEGMENT in the label's own left-to-right run —
// Payload, then Lower, Upper — and this pass's rule is ONE COMPARISON: it
// paints the segments LEFT of that cell exactly as it does at rest, and
// NOTHING from that cell rightward. A payload editor therefore takes the
// marker's whole label, a lower-bound editor leaves the payload standing
// and takes the lower cell and the upper cell, and an upper-bound editor
// leaves the payload and the lower cell and takes the upper cell alone.
//
// ONE BOX AT MOST, which is why this is an index and a cell rather than a set:
// the three editors are ONE text_editor::State, so no two can stand together.
// `marker_index` -1, the resting value, suppresses nothing on any column.
// WHICH COLUMN the index belongs to is the live view's, and each painter drops
// a suppression naming a box its own column does not have.
struct SuppressedBox {
    int        marker_index = -1;
    MarkerCell cell         = MarkerCell::Payload;
};

// The suppression the STANDING marker-lane editor asks for, or the resting
// value — THE ONE DERIVATION, read by this pass's callers, by the flag cache's
// fingerprint (the suppression is a content fact of that surface) and by the
// editor's own painter, which takes `cell` as the box its field stands in for.
// So the pass that skips, the cache that keys and the painter that draws
// cannot disagree about which box is being edited. Kind FlagPayload answers
// Payload, IterBound answers the session's own side (iter_bound_editor_side, app_state.h); every other kind, and no editor
// at all, answer the resting value.
SuppressedBox suppressed_flag_box(const AppState& app);

// Draws the marker lane's CUES in `top_strip_area` for the visible markers
// (architect 2026-10-09, Cool Edit's cues; the look, the states and the
// overlap rule at the palette block's marker-lane paragraph, every length
// program_spec.h's): each marker's TRIANGLE with its apex on the marker's
// pixel column on the lane's last rows, and its LABEL the label's lead
// right of the column (cue_label_lead_px) on its box over the lane's whole height — the payload label
// (the warp payload's scale truncation at kMarkerFlagScaleGlyphs above) and,
// where
// they paint, its two bound cells as further segments — in the label face
// (gui_font(GuiFace::Label)); every run carries its font, so `cr`'s font
// state is not touched.
//
// `iteration_on` PAINTS THE TWO BOUND CELLS (architect 2026-09-04; the
// phase-reset painter below carries the same parameter for its own hop
// bracket, and THE CALLER PASSES THE COLUMN'S OWN VERDICT —
// iteration_column_lit, app_state.h): while the mode is lit here, every
// marker the sweep reads (iter_popup_eligible_marker, warpmarkers.h — so a
// disabled owner, which carries no bracket at all, paints no cells) extends
// its label with the LOWER bound then the UPPER, each a segment the segment
// gap past the one before it, carrying the bound in its signed two-decimal
// form (format_iter_bound_cell). The sign is a cell's whole syntax: a
// payload never carries one and a cell always does. A TIE FOLLOWER'S CELLS
// wear the inactive label (architect 2026-09-19: they show the leader's
// numbers and are not this marker's to author).
//
// `selected_set` GIVES THE SELECTED LOOK (architect 2026-11-05;
// resolve_flag_face's table): every selected enabled marker publishes its
// stem entry (MarkerStem); its chip is every enabled chip, the label paints
// as at rest.
//
// THE PAINTER PUBLISHES ITS GEOMETRY. `out_hit_rects` receives one rect per
// painted cue, left to right (the hit walk follows the paint order:
// triangles first, read backwards — the right one on top — then labels,
// read backwards likewise — the right one on top, the labels painting left
// to right; topmost_flag_rect, app_state.cpp), and `out_stems` one entry per
// SELECTED ENABLED cue whose column is a waveform column. A derived width cannot be recomputed
// without shaping, so the pixels' own pass is the single owner of both.
// Either pointer may be null.
//
// `suppressed` NAMES THE ONE SEGMENT THIS PASS DOES NOT PAINT (SuppressedBox
// above): the marker whose marker-lane editor is open, and WHICH of its
// segments that editor stands in for. The pass paints the segments left of
// that one at rest and nothing from it rightward, the ones to its right
// riding the field's edge under the editor's own painter; THE CHIP AND
// THE STEM paint and publish for the whole session. The published label box
// covers exactly what the pass painted, every boundary belonging to a
// yielded segment collapsing onto its right edge.
//
// BOTH COLUMNS TAKE IT, but the PAYLOAD cell is
// unreachable on the phase-reset one: that editor is a warp-column surface by its own
// open gate (the bound editor is both columns'), and that painter enforces
// the asymmetry at its own call rather than trusting its caller (recorded
// there).
//
// `warp_frame_map`: the displayed-axis translation the painters share (the live
// map in target view). `waveform_width` is the EFFECTIVE waveform width
// (waveform_area.w), the column-mapping denominator, so a cue lands on the
// column its stem stands on at every window width.
void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  FlagLaneRects lanes,
                  int waveform_width,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  long long viewport_lattice_origin,
                  int sample_rate,
                  const std::set<int>& selected_set,
                  const std::set<int>& red_set,
                  bool iteration_on,
                  std::vector<FlagHitRect>* out_hit_rects = nullptr,
                  std::vector<MarkerStem>* out_stems = nullptr,
                  const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                  const DragOverlay* drag_overlay = nullptr,
                  SuppressedBox suppressed = SuppressedBox{});

// THE OPEN MARKER-LANE EDITOR'S RESOLVED GEOMETRY, published by
// render_flag_editor_box and consumed by the pointer path. Every field is
// DERIVED FROM A SHAPED RUN, which is exactly why it is published rather than
// recomputed: a second shaping pass in the hit path could disagree with the
// pixels. TWO EDITOR KINDS PUBLISH THROUGH IT — the payload editor (the
// flag unrolled) and the ITERATION BOUND editor (one bound cell as the field)
// — and the pointer path reads it identically for both, which is why the consumers test the
// published rect and never the kind.
//
//   `box`           the painted field in window coordinates — the edited
//                   segment's box over the lane's whole
//                   height (cue_fill_h_px, 2026-10-09 ~21:00), its
//                   outline included: for the
//                   payload editor the marker's payload label holding the
//                   FULL untruncated pending, for the bound editor the bound
//                   cell's segment in the same role, anchored where that
//                   segment's box rests. ONE WIDTH RULE FOR ALL (architect
//                   2026-09-05): the field is its two quanta of pad plus its
//                   shaped pending run and nothing more, so NO FIELD BUYS A
//                   CARET COLUMN — every one borrows it from its own right
//                   pad (the borrow, render_flag_editor_box). Each field
//                   therefore OPENS AT the width of the segment box it stands
//                   in for — the payload's one deliberate step being the
//                   untruncated run where the resting label was capped — and
//                   then GROWS AND SHRINKS with what is typed, the label's
//                   segments to its right riding that edge. NEVER CLAMPED
//                   ON-WINDOW (architect 2026-09-06): the logical field (its
//                   seat, its width, `text_origin_x`, `byte_x`) is unclipped
//                   and may reach past either edge, while `box`, the
//                   published claim, is the painted field CLIPPED TO THE
//                   WAVEFORM'S COLUMNS [x0, x0 + w) as its pixels are
//                   (architect 2026-09-26, clip_to_waveform_columns), a field
//                   wholly past the edge an empty box.
//   `text_origin_x` the window x that pending BYTE 0 paints at. It already
//                   carries the view offset, so it is negative-of-nothing and
//                   directly usable: byte k sits at text_origin_x + byte_x[k].
//   `byte_x`        the shaping chokepoint's per-byte-boundary pen offsets
//                   (text_shape::byte_offsets_px) — pending.size() + 1 entries.
//                   The caret, both selection edges and click-to-byte all index
//                   it, so what is drawn and what is grabbed are one vector.
//
//   `riding_cells`  THE LABEL'S SEGMENTS TO THE RIGHT OF THE EDITED ONE, in
//                   resting order with their resting look on their face box,
//                   painted from the field's right edge so the label reads as
//                   it reads at rest with only the edited segment's width live
//                   (architect 2026-09-05, THE ONE GRAPHIC MODEL —
//                   SuppressedBox above states it once). What rides follows
//                   from which segment the field stands in for: the payload
//                   field carries the two bound cells, the LOWER-bound field
//                   the upper cell, and the UPPER-bound field nothing.
//                   Published as a FlagHitRect: the run's whole painted
//                   extent (no triangle), keyed to the edited marker and
//                   carrying the same boundaries a resting label publishes —
//                   each collapsing onto the next where its segment is not in
//                   the run — so the pointer resolves WHICH CELL out of it
//                   exactly as it does at rest. marker_index -1 with a zero
//                   rect wherever nothing rides.
//
//                   THE RIDING CELLS ARE THE MARKER'S OWN CELLS FOR THE
//                   POINTER TOO (architect 2026-09-05, completing the
//                   one-graphic-principle ruling for the press: the three
//                   editors are transparent to each other for the pointer as
//                   they already are graphically; no press reaches
//                   them while the editor stands since 2026-10-25, its veil
//                   swallowing it, so the rule answers the walk's other
//                   readers alone). The one flag walk
//                   (topmost_flag_rect, app_state.cpp) asks this rect FIRST,
//                   because the editor paints last and so covers whatever the
//                   lane pass drew under it. The two publications cannot
//                   overlap in any case: the pass's label box ends where the
//                   edited segment begins and this run begins past the field's
//                   right edge, so under a BOUND editor the same marker
//                   publishes both — its triangle and its payload (and, under
//                   the upper field, its lower cell) in the lane stash, its
//                   riding run here — and a point falls in exactly one. A
//                   PRESS ON A RIDING CELL IS VEILED since 2026-10-25 (every
//                   editor a modal): it is a press outside the field, which
//                   on_button_press's veil consumes silently, and the editor
//                   stands — Esc and Enter its way out (until then it closed
//                   the editor uncommitted and acted on the cell). It is
//                   still a SECOND rect and still deliberately not folded into
//                   `box`: the caret / text-drag claim seats a caret for any
//                   press inside `box`, so a folded run would map presses
//                   on cell ink to payload bytes and promise
//                   editing where none is. What the run promises instead is
//                   the marker-lane cue, which it gets for free from the same
//                   walk. On close the boxes settle back into the cached pass
//                   and this publication goes with them, on the frame the
//                   close's own damage repaints.
//
// `valid` is false whenever neither marker-lane editor is open, and
// the painter writes that state on every frame it runs, so a stale box can
// never outlive its session.
//
// THE BOX IS THE CLAIM, pads included: a press anywhere inside it places the
// caret, which is why the text VIEWPORT (the clip band inside the pads) is not
// published — clicking a field's padding should put the caret at the nearest
// end, and the nearest-boundary search gives exactly that with no extra term.
// The box sits in the marker lane, off the touch pan zone since 2026-09-25
// (the lanes left the navigation surface; it had its own yield clause from
// 2026-09-05), and the platform's editor-field query reads this same rect
// (touch_point_in_editor_field), so a finger landing in the field reaches
// the caret drag — a tap its press — rather than the phone-model pan.
struct FlagEditorBox {
    bool                valid         = false;
    GuiRect             box{0, 0, 0, 0};
    FlagHitRect         riding_cells{};
    double              text_origin_x = 0.0;
    std::vector<double> byte_x;
};

// THE FLAG EDITOR'S UNROLL (row 5's last piece, 2026-08-01): the edited
// segment of the cue's label EXPANDS to hold its full untruncated payload,
// and the editor's text is drawn in it — kdenlive's flag-becomes-the-text-
// box, on Cool Edit's cue since 2026-10-09.
//
// THE FIELD IS AN EDIT FIELD ON THE LANE'S WHOLE ROWS (architect
// 2026-10-09; the look 2026-11-01 night; the ruling and its reason at the
// palette block's editing paragraph): the ground `ground` inside a
// three-px `text` outline (cue_editor_outline_px), its text and caret `field_text` on the
// label's own baseline and seat, the caret and the selection band THE LEGAL
// SET'S BAND (cue_band_top_px, cue_band_h_px — the metrics rule at
// marker_lane_h_px; the selected ink past the band taking the field text is
// the EDITING paragraph's), so opening an editor changes the label's SIZE
// and nothing about where its text stands — the selected substring in the
// chrome's SELECTED PAIR, the ordinary highlight. The box paints over the lane, the triangle and any neighbouring
// label it reaches. Since no field buys a caret column, the
// size only changes where the resting label was capped, the field opening at
// the committed run's own width and growing only with what is typed past it;
// the cells riding its right edge keep their resting look. THE CHIP AND
// THE STEM stay the cached pass's. A REFUSED COMMIT recolours nothing: the
// whole text is selected (text_editor::refuse) and the refusing owner's card
// says why, the dialog editor's rule on this surface.
//
// THE TEXT IS THE LABEL FACE, matching the labels it replaces.
//
// AT THE WAVEFORM'S EDGE THE FIELD IS CUT OFF AND NOTHING MOVES — the clip is
// the waveform's columns [0, w), the resting cue's own (architect
// 2026-09-26; the published box and riding rect are clipped with the
// pixels) — (architect 2026-09-06,
// on the clamp this paragraph used to describe: "leave its position truthful,
// don't clamp it, don't do anything"). The box used to slide left to stay
// fully on-window, its width capped at the lane so that it always could; both
// are deleted, because under the one graphic model that slide walked the field
// left over the boxes the pass is still painting at rest and took it out of
// its own box's slot. The field therefore holds its WHOLE run at every width —
// the text does not scroll inside it any more — and a field reaching past an
// edge is READ BY Esc, A PAN AND A REOPEN (architect 2026-10-25): the editor
// is a modal, so nothing pans while it stands, and its open pans nothing
// either.
// State::view_offset_px survives on this surface for the sub-pixel case alone
// (a run's width rounded down to a whole column, which would otherwise clip
// the caret's reserved column at end-of-text); the glyph-by-glyph travel its
// minimal-travel rule describes belonged to the DIALOG field, whose box was
// fixed, until that field gave way to the modal family's box and its soft
// wrap (2026-10-14). Left/Right/Home/End navigate exactly like any one-line
// field.
//
// Takes AppState by NON-CONST reference, alone among the renderers here, and
// for two honest reasons: it advances the editor's view offset (session state
// that must persist across frames) and it publishes app.flag_editor_box. Both
// are the painter owning what only the painter can compute. (The dialog
// editor's painter — paint_modal_dialog, paint_handler.cpp — publishes its
// wrapped rows the same way; it is a GuiPaintHandler method and already holds
// an AppState&.)
void render_flag_editor_box(cairo_t* cr, AppState& app, const GuiAudio& audio);

// The phase-reset column's cues: the identical cue, the identical class ladder
// and the identical publication contract render_flags documents above. Their
// LABEL is the display-only kPhaseResetLaneToken (a phase reset authors no
// payload). `iteration_on` PAINTS THIS COLUMN'S TWO BOUND CELLS since
// 2026-09-09, when grid iterations grew a second column — and it is this
// column's own verdict, true only while the lamp was lit HERE: every reset the
// sweep reads (phase_reset_iter_eligible_marker, phaseresetmarkers.h — so a
// disabled reset, which carries no bracket at all, paints no cells) extends its
// label with the LOWER
// bound then the UPPER, each a segment as the warp column's are, carrying the
// bound as a SIGNED WHOLE HOP of the analysis lattice
// (format_phase_iter_bound_cell). The sign is the whole syntax here as it is
// on the warp column, and the ABSENT DECIMALS are what tell a hop cell from a
// cent cell. The selection gives the selected look as there.
void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            FlagLaneRects lanes,
                            int waveform_width,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            long long viewport_lattice_origin,
                            int sample_rate,
                            const std::set<int>& selected_set,
                            const std::set<int>& red_set,
                            bool iteration_on,
                            std::vector<FlagHitRect>* out_hit_rects = nullptr,
                            std::vector<MarkerStem>* out_stems = nullptr,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                            const DragOverlay* drag_overlay = nullptr,
                            // The standing editor's suppression (contract
                            // at SuppressedBox and render_flags above). ITS
                            // PAYLOAD CELL NEVER REACHES THIS COLUMN and this
                            // painter is what makes that true — it drops a
                            // suppression naming the payload box, that editor
                            // being a warp-column surface by its own open
                            // gates, while the bound editor is both columns'.
                            SuppressedBox suppressed = SuppressedBox{});

// ONE PREPARED DIFF FLAG for the `h` history mode's lane, in the ORDER it is
// painted and published. The caller (maybe_rebuild_flag_cache) resolves the
// commit's delta into these; this file only paints what it is handed, so the
// diff model's types never reach the renderer.
//
// THE FRAME FIELD IS NAMED time_frame ON PURPOSE: it is what the shared column
// mapper iterate_visible_flags_impl reads off a marker, so a diff flag rides the
// EXACT expression a live marker rides — same map, same viewport, same width,
// same nearbyint — rather than a second spelling of it. IT IS THE LEFT
// CUE'S FRAME: a single half's own, and on a changed pair the smaller of
// the two halves' frames (architect 2026-10-10 / 2026-10-11, the nudged
// pair), the pair's right cue mapped from its own frame by the same
// expression (render_history_diff_flags). `then_frame` and `now_frame` are
// each half's OWN frame, meaningful exactly when its half's bool is set —
// equal on a same-frame pair, apart by at most the nudge window on a NUDGED
// one (kHistoryNudgeWindowMs, history_diff.h) — and they are what the revert
// addresses (the now row at now_frame, the then line back at then_frame)
// and, on a nudged pair, what each of its two cues paints and lands on
// (HistoryDiffHalf; history_diff_flag_landing_domain_frame, app_state.h).
//
// The two halves are independent bools rather than an enum because the CHANGED
// case is exactly "both": one flag, ONE ITEM for selection and revert —
// painted as ONE CUE whose label carries both halves on a same-frame pair
// (the removed half's segment first, struck, then the added: before, then
// after, the planner's reading of his "whichever one comes first",
// 2026-10-11), and as TWO CUES joined by the ring on a nudged one (architect
// 2026-10-26), each at its own frame.
// THE THEN SIDE'S VALUE RIDES ALONG (2026-08-05), for the REVERT act rather than
// for the paint: `then_token` is the removed line's payload past the '|' —
// VERBATIM, the same slice the label is built from — and `then_disabled` its
// disable bit, both meaningful exactly when `removed` is set (an added-only flag
// has no then side to restore). The act reconstitutes the sidecar LINE from them
// and hands it to the frozen parser, so the value travels as text the loader
// itself judges and no second grammar is written anywhere
// (GuiInputHandler::run_history_revert). Phase resets carry no token — their
// line is frame plus the disable bit — so `then_token` stays empty on that
// column.
//
// EACH HALF NAMES ITS OWN ROW (2026-09-16): `then_ordinal`
// is the removed line's row within its frame's run on the then side,
// `now_ordinal` the added line's on the now side — each meaningful exactly
// when its half's bool is set, both copied off the delta entry the label is
// built from (the contract is at GuiHistoryWarpEntry::ordinal,
// history_diff.h). IDENTITY IS DATA, NOT A FACE: the painter reads neither,
// the hit rects and the walk are unchanged, and the revert is their one
// reader — it deletes the exact now-side row and re-seats the then line at
// its then-side ordinal, so a flag on the second of two coincident rows
// reverts that row and not the run's first.
//
// THE LANE'S DISABLED AXIS IS EFFECTIVE, PER COMMIT SIDE (architect
// 2026-08-22). The PAINT bits are the two `*_effective_disabled` fields —
// each half's participation verdict resolved within its OWN side's commit
// (GuiHistoryWarpEntry::effective_disabled owns the resolution contract; the
// verdict is the line's own flag since 2026-10-29, and phase resets fill
// these verbatim) — each meaningful exactly when its own half's bool is set.
// `then_disabled` stays the VERBATIM LOCAL byte and is back to the revert's
// field alone: the revert act reconstitutes the then line from it, and the
// label's '#' text is composed from the delta's local bits at the cache fill —
// the painter no longer reads it. The former `now_disabled` was the added
// half's paint bit and had exactly that one reader, so it is RENAMED to say
// what it now holds rather than kept beside a twin. The paint the effective
// pair drives is the live lane's own disabled treatment applied to this
// lane's diff inks, per half; the full ruling is at render_history_diff_flags
// below.
struct HistoryDiffFlag {
    int64_t     time_frame = 0;       // the left marker's (the paragraph above)
    int64_t     then_frame = 0;       // the removed half's own
    int64_t     now_frame  = 0;       // the added half's own
    bool        removed    = false;   // the commit had this line
    bool        added      = false;   // the session has this line
    std::string removed_text;
    std::string added_text;
    std::string then_token;
    int         then_ordinal = 0;   // the removed half's row within its run
    int         now_ordinal  = 0;   // the added half's row within its run
    bool        then_disabled = false;           // verbatim local: the revert's
    bool        then_effective_disabled = false; // the removed half's paint
    bool        now_effective_disabled  = false; // the added half's paint
    // NO COINCIDENT CLASS TRAVELS HERE, and that is deliberate (recorded
    // 2026-09-02, the disabled axis's sibling): the live lane's strike — the
    // class a coincident stack or a dangling reference earns — is a verdict
    // over the WHOLE resolved store, and a diff half is a line from one
    // side's sidecar with no store around it to resolve against. So a
    // checkpoint's collapsed stack paints as ordinary added/removed halves in
    // the view and strikes as coincident only once its lines are back in the
    // live store; here the strike is the REMOVED half's alone.
    // The disabled axis DOES travel (the two effective bits above, 2026-08-22)
    // because it is a property of the line itself.

    // A CHANGED PAIR whose two halves stand apart — the nudged pair, drawn as
    // two cues joined by the ring (render_history_diff_flags).
    bool nudged() const {
        return removed && added && then_frame != now_frame;
    }
    // THE ADDED HALF IS THE LEFT CUE: a nudged pair moved earlier, its added
    // cue left of its removed one — the Tab walk's order within the pair
    // (history_diff_cycle_target, app_state.h).
    bool added_left() const { return nudged() && now_frame < then_frame; }
    // THE FRAME OF ONE CUE, in source frames: on a nudged pair the half's own
    // (then_frame or now_frame), on every other flag its one cue's,
    // time_frame.
    int64_t cue_frame(HistoryDiffHalf half) const {
        if (!nudged()) return time_frame;
        return half == HistoryDiffHalf::Added ? now_frame : then_frame;
    }
    // THE LEFT AND RIGHT CUE'S HALVES, in column order: on a nudged pair the
    // two in their frames' order; on every other flag its one cue (both
    // answers the same, so a walk visits it once).
    HistoryDiffHalf left_half() const {
        if (!nudged()) return removed ? HistoryDiffHalf::Removed
                                      : HistoryDiffHalf::Added;
        return added_left() ? HistoryDiffHalf::Added : HistoryDiffHalf::Removed;
    }
    HistoryDiffHalf right_half() const {
        if (!nudged()) return left_half();
        return added_left() ? HistoryDiffHalf::Removed : HistoryDiffHalf::Added;
    }
};

// THE HISTORY MODE'S MARKER LANE. Replaces render_flags / render_phase_reset_-
// flags wholesale while the mode stands: no live marker paints, and this pass
// becomes the producer of the same two stashes they produce (out_hit_rects with
// `marker_index` carrying the INDEX INTO `flags`, out_stems the same), so
// hit_test_flag keeps working unchanged and answers a diff-flag index.
//
// THE ANATOMY IS THE LIVE CUE'S (the palette block's marker-lane paragraph):
// every cue THE PLAIN CUE, the live lane's chip by its selection (THE
// STATES), both halves alike (architect 2026-10-26 ~15:30, "Two, agree": the history
// in the cue color, the invalid color retired) — and the label
// in the label face, its resting text the text tone. THE STRIKE TELLS THE
// HALVES APART: a REMOVED line's label struck (show_cue_label's bar), an
// ADDED line's plain (resolve_flag_face's table). THE LABEL IS THE LINE'S
// OWN TEXT, no sign ("we could also remove the brackets plus minus, because
// that becomes redundant at that point. And those are always hard to see
// anyways at the font size. A strike through is easier to see";
// history_diff_label, paint_handler.h).
//
// A CHANGED PAIR IS ONE ITEM (the two pairings are GuiHistoryWarpChange's,
// history_diff.h): one HistoryDiffFlag, one selection, one revert. A
// SAME-FRAME pair is ONE CUE — its label the two halves as two segments, the
// removed (struck) first, then the added, the segment gap between them; one
// chip, one stem entry, no ring. A NUDGED pair (the halves apart) is TWO CUES
// JOINED BY THE RING (architect 2026-10-26 ~15:30: "both markers represented
// with their stems, because one of them will have the strike through and
// that'll be obvious which one is removed, and they should be joined by the
// overlay ring"): the removed cue at then_frame, struck, the added at
// now_frame, plain, each its own chip and label box, and the ring between
// their two columns on the canvas (MarkerStemRun), always, its four sides
// solid `cue_outline` lines. EACH CUE IS ITS OWN
// HIT RECT AND TAB STOP ("it's accurate that every marker gets a tab — next
// marker, previous marker tab"): both rects name the flag's index, each its
// own half (FlagHitRect::diff_half), and the focus stands on one half
// (AppState::HistoryMode::focus_half).
//
// EVERY HALF IS SIZED BY ITS OWN SHAPED TEXT, so the halves of a same-frame
// pair are routinely ASYMMETRIC and its one hit rect composes off the two
// measured widths. THESE LABELS ARE NEVER TRUNCATED (the cull bound follows the
// commit's own widest text; the reasoning is at the paint site).
//
// THE LANE CARRIES THE DISABLED AXIS, PER COMMIT SIDE (architect 2026-08-22,
// closing a state-axis gap the view shipped with: a `#` line and a live one
// painted the same flag, so the delta showed the position and hid the state).
// A HALF whose line is EFFECTIVELY disabled within ITS OWN side's commit — the
// participation verdict resolved over that side's full warp set (the
// effective pair at HistoryDiffFlag above, the own flag since 2026-10-29; the
// '#' in the TEXT stays the local byte) — paints its segment in the LIVE LANE'S
// INACTIVE LABEL, and a single disabled half's chip the grey one
// (resolve_flag_face). IT SPLITS HONESTLY ON A CHANGED PAIR: each half takes
// its own bit, so a disable TOGGLE paints one inactive half beside one live
// half and the direction of the toggle is readable off the label itself.
//
// `focus_index` is the mode's OWN focus (at most one flag, -1 for none)
// and `selected` the mode's OWN
// multi-selection (ordinals into the same list, 2026-08-05): THE LIVE LANE'S
// SELECTED LOOK ON THE CHIP (architect 2026-11-04; resolve_flag_face's
// table). The focus or membership SELECTS the flag — the focus is the
// selection's singleton when the set is empty, and the revert act reads them
// the same way — and A PAIR'S HALVES ARE ONE ITEM: a selected item wears the
// SELECTED look on BOTH halves, each cue its own stem (a nudged pair's two
// cues each at its own column; MarkerStem, architect 2026-11-05). The
// labels paint as at rest (a nudged pair's focused half,
// HistoryMode::focus_half, is the walk's and the press's, not the paint's).
// THE CHIP READS THE DISABLED AXIS (architect 2026-08-22): a SINGLE
// flag — added-only or removed-only — whose one side is EFFECTIVELY disabled
// wears the grey chip and publishes NO STEM ENTRY, the live lane's rule
// verbatim, while a CHANGED PAIR KEEPS ITS LIVE CHIP whichever halves are
// disabled, because the pair as a whole is a live EDIT being displayed
// rather than a line in a switched-off state.
// NO CELLS AND NO ADDRESSED CELL ON THIS LANE: a bracket is session-only and
// in no commit, so a diff flag carries no bound cells, and the view's focus is
// its own diff-flag cycle's with nothing for AppState::addressed_cell to
// address.
void render_history_diff_flags(cairo_t* cr,
                               GuiRect top_strip_area,
                               FlagLaneRects lanes,
                               int waveform_width,
                               const std::vector<HistoryDiffFlag>& flags,
                               long long viewport_start_sample,
                               long long viewport_end_sample,
                               long long viewport_lattice_origin,
                               int focus_index,
                               const std::set<int>& selected,
                               std::vector<FlagHitRect>* out_hit_rects,
                               std::vector<MarkerStem>* out_stems,
                               std::vector<MarkerStemRun>* out_runs,
                               const std::vector<WarpFrameMapSegment>* warp_frame_map);

// THE ONE COMPOSER FOR WARP FLAG TEXT (defined in render.cpp): the canonical
// line's payload WHOLE — the tempo's derived base and its every deviation
// term, `*scale`, `:label`, or the pass / ref forms — and never a bracket.
// NOTHING HERE IS EVER CUT, and its ONE READER is why: the flag editor seeds
// its field from it (enter_top_flag_edit, flag_editor.cpp), so what the field
// opens with is what the store holds — a cut seed would let a commit throw
// away a scale's digits or a chain's terms the user never touched. (The `j`
// copy's payload is composed in the parser instead, off the RESOLVED value
// and never off this string: resolved_marker_payload, warp_frame_map_build.h.)
// The iteration bounds are the two cells beside the flag
// (format_iter_bound_cell owns their spelling), each with its own editor, so
// no composer splices them anywhere.
std::string flag_text(const std::vector<GuiWarpMarker>& markers, int idx);

// WHAT THE FLAG BOX ACTUALLY PAINTS (architect 2026-09-19; defined in
// render.cpp beside the composer above): the same payload with THE SCALE
// CAPPED to `*N.NN` (kMarkerFlagScaleGlyphs) and the truncation marker
// appended where that cut anything — more scale digits, or a label
// definition riding past them. THE BASE AND THE WHOLE CHAIN ALWAYS PAINT IN
// FULL: a tempo is what the flag is for, and a chain the user authored term
// by term is unreadable as `1.23+0.0...`. With no scale nothing is cut at
// all and the label definition paints whole.
//
// SO A FLAG'S WIDTH IS ITS CHAIN'S, AND THAT IS WHAT THE TERM CAP COSTS
// (stated here because this is where the never-cut rule lives): at
// kMaxTempoDeviationTerms — sixteen since 2026-09-19 — the worst box paints
// about ninety glyphs and paints every one of them. It is a known price and
// not a surprise. Nothing is laid out against it: marker_flag_max_width_px
// above is a CULL bound derived from the same constant, and a flag too wide
// for the window runs off its right edge exactly as any other over-wide flag
// does. The chains he expects to author are one to four terms long, and the
// cap is the headroom above that rather than a shape to plan for.
//
// TWO READERS, and they must be exactly two: the warp column's flag pass
// (render_flags' label lambda) and the BOUND CELLS' SEAM MEASUREMENT
// (committed_cell_seam_off), which shapes this same string to find where a
// marker's first cell begins. Measuring the UNCUT composer there would open
// a bound field at a column no cell stands on — the one place where the
// wrong composer is invisible until a cell is in the wrong place.
std::string flag_display_text(const std::vector<GuiWarpMarker>& markers,
                              int idx);

// (THE MEASURED MONOSPACE GRID IS GONE — row 7, 2026-08-01: monospace_advance,
// monospace_text_box_h, monospace_text_row_baseline_offset,
// init_monospace_grid_metrics and measured_monospace_font_px, plus the file-scope
// state they cached in render.cpp. They were ONE measurement — a cell advance
// and a glyph slot, taken once per redraw off the cairo font — and every
// consumer of it (the bottom strip's lanes, its editors' box, the flag hit
// widths before row 5) is gone. THE WAVEFORM CACHE'S FINGERPRINT FIELD did not
// vanish with them but IMPROVED: it keyed the measure as a proxy for the
// font-derived waveform inset, and now keys waveform_inset_px() itself, which is
// the render input the job actually takes. The FLAG cache's copy was already a
// recorded vestige and is deleted outright.)

// (THE MARKER-LANE PLACEMENT OWNERS ARE GONE, 2026-08-01. lane_text_left_x /
// lane_text_left_x_at_frame / flag_pending_text_left_x centered a MONOSPACE run
// over a marker's painted column and clamped it onscreen — first for the
// marker-text lane's runs and the hover popup, then, after row 5's checkpoint B
// deleted those, for the flag editor alone. The editor's unroll took the last
// of it: the box is LEFT-anchored on its own box's seam like the box it
// replaces and sized by shaped text, render_flag_editor_box being the single
// owner of that whole question — and since 2026-09-06 there is no onscreen
// clamp left in it at all, the window cutting a field off exactly as it cuts
// off a flag. The column math they wrapped
// — painted_column_of_source_frame_on_basis over the displayed map and the item
// viewport basis — is unchanged and called directly there.)

// THE PHASE-RESET DISPLAY TOKEN, and the one statement of it: what a phase
// reset's FLAG shows, where a warp marker shows its composed line
// (flag_text). DISPLAY ONLY — a phase reset authors no payload and
// serializes as a bare frame, so this string exists nowhere but the flag.
//
// IT IS A PAINTED LABEL AND NOT AN IDENTIFIER, which is the whole reason it
// may read `reset` when no name in the code may: the naming rule that "phase
// reset" is ONE CONCEPT TOKEN, never shortened to "reset" (warpmarkers.h),
// governs TYPE, FUNCTION and VARIABLE names — what an engineer reads — and a
// string the marker lane paints for a musician is outside it. This constant's
// own name spells the concept in full, and so does every identifier around it.
//
// THE WORD ITSELF (architect 2026-09-17): the flag says what the marker IS,
// and the lane's own budget decided how much of it fit. The two words `phase
// reset` did not fit — eleven bytes handed to the shared nine-byte cap of
// the day, then the three-period truncation marker, so every reset in the
// product painted `phase res...` and the second word never showed at all. At
// five bytes `reset` passed that cap untouched, and with it went the ONE
// label that spent its own budget by construction (every other label reaching
// a flag box is user text); the shared cap itself is gone since 2026-09-19,
// so this column cuts nothing at all now. Nothing lays out against the old
// width: every flag's width is pad + shaped(label) + pad, re-derived from the
// shaping pass, and marker_flag_max_width_px bounds the left cull at the
// WARP payload's worst case, comfortably past this token, so the box simply
// narrows with the word. TWO SITES READ THIS TOKEN, both by
// calling the constant (render.cpp): the flag painter, and the bound cells'
// SEAM MEASUREMENT, which shapes this same string to find where a reset's
// first iteration cell starts — so the cells move with the word and the two
// cannot disagree.
//
// THE SUCCESSION, because it is the kind of thing that gets re-invented
// backwards: the original "p" was widened to "p.r." for the retired
// marker-text lane, whose all-or-nothing fit verdict a dense reset cluster sat
// right on top of (a small zoom change flipped the whole lane between modes and
// it blinked); with that lane gone, nothing depended on the width and the token
// went back to ONE GLYPH (architect 2026-08-01, at the row-6 live look). The
// WORDS `phase reset` replaced that glyph 2026-08-18 ("phase reset flags
// should read 'phas...'") — a single `p` names nothing to a reader who has not
// been told — and `reset` replaced the words 2026-09-17, the words having
// truncated at the cap. The flags simply overlap, later over earlier, as they
// have since row 5.
inline constexpr char kPhaseResetLaneToken[] = "reset";

