#pragma once
#include "warpmarkers.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment for target-view waveform
#include "waveform_gain.h"    // WaveformGainCurve, the waveform picture's gain
#include "gui_font.h"         // GuiFont, the face owner's face at a scale
#include "program_spec.h"     // kProgramSpec, the program's lengths (both icon rows)
#include "display_transform.h" // display_color's transform (sRGB -> the window's space)
#include "gui_theme.h"        // kArcDarkTones, the compiled row (install_palette, the word readers)

#include <algorithm>
#include <array>
#include <cairo/cairo.h>
#include <cmath>
#include <cstdint>
#include <functional>
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

// A RECT ON REAL EDGES, in device px: the box of a thing whose place or size
// is a continuous quantity — a level's fill, a scrub's thumb — painted at
// exact coverage (the exact fills, paint_cell_rect / paint_ink_rect on a
// GuiRectF, below). A GuiRect widens to it exactly (every int is a double),
// so a whole box may be handed wherever an exact one is asked for.
struct GuiRectF {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    constexpr GuiRectF() = default;
    constexpr GuiRectF(double x_, double y_, double w_, double h_)
        : x(x_), y(y_), w(w_), h(h_) {}
    constexpr GuiRectF(const GuiRect& r)
        : x(r.x), y(r.y), w(r.w), h(r.h) {}
    bool operator==(const GuiRectF&) const = default;
};

// THE HULL — THE ONE CONVERSION FROM AN EXACT RECT TO PIXELS: the smallest
// whole rect that covers it, the floor of its low edges and the ceil of its
// high ones (an empty rect's hull is empty). It is what a damage rect, a
// cull or a scissor asks of a thing painted on real edges, and the damage of
// a thing that moved is its old hull united with its new one.
//
// THE RULE, AND WHAT STAYS WHOLE UNDER IT — THIS LIST'S ONE SEAT (architect
// 2026-11-15, the no-grid arc: "no grid whatsoever. That just one rule for
// everything"; "Only things that are basically impossible to move, like the
// damages and the culls, should stay integer."): WHATEVER IS PAINTED STANDS
// WHERE ITS ARITHMETIC PUTS IT, AT EXACT COVERAGE — no nearbyint, floor,
// ceil or integer division between a continuous quantity and the paint.
// WHOLE, each for its own reason:
//   PIXELS NAMED AS PIXELS — damage rects, culls, scissors, the GL clip,
//     and GuiRect / GuiGlRect as the type of those: they say
//     which pixels are touched, and a pixel is whole. An exact thing's
//     damage is its hull.
//   THE LAYOUT'S BOXES — lanes, rows, the keys' faces, fields, panes, the
//     waveform area and the well's columns: every box
//     another thing is laid out against. Their sizes are whole W through
//     scaled_px (rounded at the element, its own rule, below) or the
//     whole-px hull of a font metric (a row's pitch from the line height, the
//     time field's height from the cap); the rule governs what is painted
//     IN and BESIDE them. A box that is NOT layout — one whose size is a
//     text's own width or whose place is a mark's — takes its exact size and
//     place.
//   AN AUTHORED LENGTH — scaled_px rounds each at the element, so on the
//     tablet (gui_scale 200) every W is 2 device px and every sum of W is
//     whole; at another scale each element rounds.
//   THE AUDIO'S LATTICE — the plate's bars and columns,
//     painter_quantized_spp, the sixteenth-frame grid, authored frames (whole
//     by their grammar) and the viewport's start (a whole frame).
//   THE SCORE PAGE — whole pixels at 100 % ("the score should have no
//     upscaling or downscaling. It should be 100%. And if that's required,
//     if that requires whole pixels, then so be it.").
//   A BOX PAINTED OVER THE CANVAS'S HOLE — the canvas is the GPU's, and
//     the backbuffer over it holds alpha 0 or alpha 255 but where a
//     floating surface stands (canvas_sink.h's THE TRANSPARENCY INVARIANT:
//     the texture's draw is unblended but inside a floater's hull), so an
//     edge at a fraction there would leave a partly covered pixel nothing
//     composites: whatever chrome other than the floaters stands over the
//     hole is whole pixels on its OUTER edges — the modal family's box,
//     and any other chrome that reaches over it (the on-screen keyboard's
//     surface, whose whole top line the hole is cut at,
//     onscreen_keyboard::waveform_paint_area). THE FLOATING
//     SURFACES' BOXES (the tooltip's and the notification cards') COMPOSITE
//     TRULY (2026-11-19: their carried fills at
//     .95, their rounded corners, the tooltip's shadow — the GPU's blend
//     inside their hulls), and stay whole px as LAYOUT: their sizes the ceiling of their
//     text under whole-W pads, their seats a face's and the panel's whole
//     edges. What is painted IN such a box — its text, its inner marks — is
//     exact.
inline GuiRect hull(const GuiRectF& r) {
    const int x0 = static_cast<int>(std::floor(r.x));
    const int y0 = static_cast<int>(std::floor(r.y));
    if (!(r.w > 0.0) || !(r.h > 0.0)) return GuiRect{x0, y0, 0, 0};
    const int x1 = static_cast<int>(std::ceil(r.x + r.w));
    const int y1 = static_cast<int>(std::ceil(r.y + r.h));
    return GuiRect{x0, y0, x1 - x0, y1 - y0};
}

// Point-in-rect on real edges, the same half-open convention (>= x, < x + w)
// over a real point: adjacent exact rects tile with no shared point and an
// empty one contains nothing.
inline bool rect_contains(const GuiRectF& r, double x, double y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

struct GuiColor {
    double r;
    double g;
    double b;
};

// Build a GuiColor from a 0xRRGGBB word, converting each 8-bit channel to an
// exact [0,1] double — the one road from a byte triple (a word of the
// row, kArcDarkTones, or a named literal's) to a colour. RGB only: the
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
// target-frame in target view), consumed by render_trim_flags to place the
// trim bar's span. Values
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
// THE GUI'S COLORS ARE THE PALETTE'S TONES AND NOTHING ELSE (architect
// 2026-10-04; the split 2026-10-07): THE PALETTE holds every color the
// PROGRAM draws in the well or on a thing that enters it (the waveform's
// canvas, ink and core, the canvas's grid and center line, the cues' one
// color and the disabled cue's grey, the playhead and the scanner;
// palette_file.h's head owns the rule that parts program from chrome) —
// ONE PALETTE, ARC-DARK'S, COMPILED (architect 2026-11-01: the canvas
// grammar kept; architect 2026-11-21: one look, its tones the compiled row
// kArcDarkTones, gui_theme.h, each line its Arc source); A LIT OR PRESSED
// KEY'S FACE IS THE ACCENT, the palette's `cue` (the markers' chip the same
// word, `cue_fill`, 2026-11-13) — its inert face the chrome's
// `lit_dim_fill` (architect 2026-11-11; Arc's states 2026-11-19,
// materials.h's THE KEY); THE PANEL'S FACE is the body tone (`body_face`,
// 2026-11-19), the title bar alone the row's `ground`, and THE CHROME'S
// TONES are the row's, one per field. Every member and the row's tone that
// fills it are the fill table (kGuiPaletteFill, palette_file.h, the one
// owner). THE APP PAINTS NO WINDOW CHROME (the title bar block, below).
// There is NO LEVEL (architect 2026-10-04) and no second look (architect
// 2026-11-21). The app derives no palette color: every steady tone is a
// word of the compiled row, each computed tone's arithmetic told at its
// line there (gui_theme.h). The standing mixes at paint time are between
// compiled words alone — a fading face's tone between its looks' endpoints
// (StampTone, materials.h) and the sweep highlight's blend toward white
// (paint_sweep_highlight).
// EVERY COLOUR A PAINTER HANDS CAIRO is one of GuiPalette's fields through the
// one accessor palette() — the chrome's flat-look tones among them
// (materials.h, 2026-11-01: the ground, the key's faces (rest, disabled
// and its border, a lit key's inert face, its lit face the palette's
// `cue`), the scale's trough and rim, the text tones, the well's tones —
// the trim bar, the ruler, its ticks, the marker lane — the panes and the
// selection pair) — or
// THE GLYPHS (architect 2026-11-01; every drawing Papirus's but the marker
// pair's and Value Drag's, ours, icons.h's head): each file is recolored to
// `currentColor` and painted in THE TONE the
// caller passes as the row's sRGB word (kArcDarkTones, gui_theme.h:
// `text`, `key_dim_ink` on a disabled key, `text_on_lit` on a lit key's
// accent face and `text_on_lit_dim` on a lit disabled one's dimmed face,
// materials.h's key_ink; icons.h's THE BOUND DRAWING) — the one place an
// antialiased picture is copied whole.
// TEXT OVER A FILL IS THE FILL'S OWN PAIR — a text tone beside every ground
// tone: the text tone over the ground, the field, an unlit key's two faces
// and the selection's grey; `text_on_lit` over a lit key's accent face, lit
// or lit under press, a glyph and the on-screen keyboard's lit cap alike,
// and `text_on_lit_dim` over a lit-but-inert key's (architect 2026-11-11).
// STILL OPAQUE. A BUTTON'S HOVER FACE IS ARC'S (architect 2026-11-18 /
// 2026-11-20): `key_hover_face` over a resting key, word button, on-screen
// keyboard key and the scrub's
// knob under the hovering pen, mixed in and out over Arc's 200 ms, and
// every other change of a button's state faded as Arc's sheet says (50 ms
// into a pressed or lit look, 200 ms into any other; architect 2026-11-20 —
// materials.h's THE HOVER; AppState::FaceFades, the one owner); THE
// TRIM BAR'S PART under the pen — the pill's body or an end's cap — wears
// `trim_span_hover_face` at once, with no fade, and both caps show
// (trim_bar_tones; AppState::TrimBarHover; architect 2026-11-20); the
// lists' lit rows are a selection, not a button face, and do not hover —
// A LIST'S RESTING ROW a tap would act on wears `list_hover_face` under
// the pen, Arc's `row.activatable:hover` (3 % white over the view), in at
// once and out over Arc's 150 ms row transition (architect 2026-11-21;
// paint_handler.cpp's THE LIST VIEW).
// THE KEYS' RINGS AND ENTRIES ARE ROUNDED (3 W outer corners, 2026-11-19:
// Arc's button and entry, A BUTTON IS ARC'S BUTTON below), their edges at
// exact coverage, as every mark placed by time is (THE COVERAGE STAMP,
// cool_edit_paint.h), and THE WAVEFORM PLATE'S EDGE is antialiased and
// blended on the GPU (2026-10-30, render_waveform_columns' head).
//
// A HEX HERE IS AN sRGB BYTE TRIPLE, AS EVERY SCREENSHOT RECORDS IT
// (architect 2026-10-08: "if I open that image in the web browser, it
// would have the identical color on the GUI") — a palette member, the
// compiled row's word (kArcDarkTones), a named literal. The tablet's window
// is a Display-P3 layer (GuiPlatform::adopt_window, platform_android.cpp), so
// THE TABLET CONVERTS sRGB -> DISPLAY-P3 AT THE PAINTER'S ENTRY
// (display_color; the math and every converting site at
// display_transform.h's head) and the panel presents the color the browser
// presents.
//
// NO RELIEF (the flat look, architect 2026-11-01; the overlays' part
// 2026-11-02): no edge in the product is raised, sunken or two-toned. THE
// CHROME'S LINES are flat ONE-W lines (hairline_px, below, the line rule of
// 2026-11-04) in `well_border` (the well's lines, THE WELL'S EDGES; the
// keys' rings); the floating surfaces have none (THE FLOATING SURFACES,
// below).
//
// THE MAPPING (architect 2026-10-03), field -> what it paints:
//   ground        the title bar alone (paint_title_bar; everything under it
//                 is the panel's `face`, below);
//   selected pair the folder
//                 overlay's and the picker's highlighted row (its name; the
//                 glyph keeps its own inks, icons.h), the selection
//                 band and selected substring of every text field, the flag
//                 editor's included — never a selected cue's resting label,
//                 which paints as at rest (architect 2026-11-04: the
//                 selection shows on the chip; THE MARKER LANE, below)
//                 — ONE PAIR, FOCUSED OR NOT (architect 2026-10-03: a
//                 selection keeps its pair in a window that has lost the
//                 focus);
//   field_text    a value editor's line on its sub-page, the caret its
//                 text (architect 2026-10-03); THE LIST — the folder
//                 overlay's resting names on the pane (architect
//                 2026-10-06, paint_folder_overlay) — a question's line and
//                 the progress page's sentence and count on the same view
//                 in the lists' rows' tone (paint_modal_page; architect
//                 2026-11-20), and THE FLAG EDITOR'S
//                 text and caret (architect 2026-10-09; EDITING, below);
//   card_text     a standing notification card's words and glyph (THE
//                 FLOATING SURFACES, below; the cards' fills the flat
//                 look's carried fills, 2026-11-19);
//   THE PALETTE   the program's THIRTEEN (palette_file.h, 2026-10-07;
//                 Arc-Dark's compiled numbers, architect 2026-11-21):
//                 the waveform's canvas (THE WAVEFORM, below; the plate's
//                 ink and core are no palette field, the plate taking the
//                 row's two words, waveform_plate_inks); THE CANVAS'S TWO
//                 LINES, Cool Edit's grammar — `grid` under the waveform and
//                 `center` on each channel's zero, over it (THE WAVEFORM);
//                 THE ACCENT `cue`; THE MARKER'S CHIP (architect 2026-11-04;
//                 2026-11-05) — `cue_fill` every enabled fill (the accent,
//                 2026-11-13) — NO OUTLINE (2026-11-19) —
//                 `cue_outline` the selected marker's stem and the
//                 lead-in ring — warp and phase reset alike (the strike,
//                 the warp column's alone, is the invalid mark, THE MARKER
//                 LANE below); the disabled cue's `inactive_cue`, its
//                 chip's fill alone; the playhead's two tones,
//                 `playhead_head` — its triangle in the ruler — and
//                 `playhead_line` — its stem's one static tone in the
//                 canvas (2026-11-21: A5ABB5, Arc's `.h4`; THE PLAYHEAD,
//                 below);
//                 THE WAVEFORM'S HOVER's three (2026-11-21) —
//                 `hover_line` the waveform's hover line and its triangle
//                 in the ruler, `cue_hover` and
//                 `inactive_cue_hover` a hovered chip, enabled and
//                 disabled (a hovered
//                 disabled marker's label the chrome's
//                 `inactive_cue_label_hover`); and the
//                 `scanner` — the playback line AND THE ZOOM ANCHOR'S STEM,
//                 the white of the stems that belong to the controls
//                 (architect 2026-10-09). THE PANEL'S `face` (both icon
//                 rows, the status bar and the erase) is the row's body
//                 tone `body_face` (2026-11-19), no palette field of its own.
// THE FLOATING SURFACES — ARC'S TOOLTIP AND NOTIFYD'S CARDS (architect
// 2026-11-19: "if we can do the transparency, we might as well"), each its
// own look on the one rounded box (paint_floating_box,
// paint_handler.cpp: no border, no ring), ITS FILL A CARRIED FILL PAINTED
// AT ITS OPACITY OVER WHAT LIES UNDER IT (gui_theme.h's THE THREE CARRIED
// FILLS;
// set_palette_source_alpha over the chrome, the GPU's blend inside the
// floaters' hulls over the canvas — canvas_sink.h's THE TRANSPARENCY
// INVARIANT): THE TOOLTIP
// `tooltip_fill` (Arc's $osd_bg_color, arc_dark 4B5162 at .95), radius 2 W,
// its words `sidebar_text`, ARC'S SHADOW outside it (the tooltip block's
// THE TOOLTIP IS ARC'S and ARC'S TOOLTIP SHADOW); A NORMAL CARD notifyd's
// grey `card_fill` (353945 at .95) under `sidebar_text`, A STANDING CARD
// Arc's destructive red `card_standing_fill` (F04A50 at .95) under
// `card_text` (= `card_ink`, Arc's white on its destructive red), radius
// 2 W, no shadow (notifications.h's THE LOOK). THIS PARAGRAPH IS THE RULE'S
// ONE STATEMENT.
// THE CANVAS RUNS THE PANEL'S WHOLE WIDTH (architect 2026-11-19, edge to
// edge; render.h's THE WELL'S EDGES): its two channels' wells are
// parted by one line of `well_border` (waveform_divider), its edges the
// panel's columns, the well's lines above and below (the palette block's
// well paragraph).
//
// THE DISABLED FACES (Arc's disabled button, architect 2026-11-19): a dead
// key DIMS ITS DRAWING (`key_dim_ink`, paint_key_glyph), its face
// `key_dim_face` and its stretch of the ring `key_dim_border` — A DEAD LIT
// KEY DIMS FACE AND DRAWING TOGETHER, the dimmed accent `lit_dim_fill`
// under `text_on_lit_dim` (architect 2026-11-11; materials.h's THE KEY;
// under an overlay too); a CUE'S inert label is the dim print `text_dim`
// (render.cpp's cue_label_ink; the marker-lane paragraph); the settings
// page's inert GROUP ROWS wear the dim print (`text_dim`,
// paint_folder_overlay, 2026-10-28). Nothing else dims.

// -- THE CHOKEPOINTS ----------------------------------------------------------
//
// EVERY COLOUR THIS PRODUCT HANDS CAIRO GOES THROUGH ONE OF THESE THREE,
// and no site calls cairo_set_source_rgb(a) itself (none outside
// render.cpp's bodies): set_palette_source for the chrome — its
// translucent twin set_palette_source_alpha for the carried fills and the
// tooltip's shadow (2026-11-19) — every word, glyph cell and mark, and
// every surface, the flat ground and the keys among them (the flat look,
// materials.h) — and
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
// THE CHROME'S SOURCE AT AN OPACITY (2026-11-19): the
// color converted as set_palette_source converts it, handed to cairo with
// `alpha` (0..1) — THE CARRIED FILLS (the tooltip's and the cards' boxes at
// their row's opacity, tone_alpha) and the tooltip's shadow's black. The
// palette's one translucent road: a box painted through it composites over
// what lies under it, the chrome on the CPU and the canvas on the GPU
// (canvas_sink.h's THE TRANSPARENCY INVARIANT, the GPU's blend inside the
// floaters' hulls).
void set_palette_source_alpha(cairo_t* cr, GuiColor c, double alpha);

// A CARRIED FILL'S OPACITY (gui_theme.h's THE THREE CARRIED FILLS): the top
// byte of its row word over 255 — F2 (.949) on the three. Read off
// kArcDarkTones' word, the palette's GuiColor holding the color alone.
inline constexpr double tone_alpha(uint32_t word) {
    return static_cast<double>((word >> 24) & 0xFFu) / 255.0;
}

// -- THE ACTIVE PALETTE ---------------------------------------------------------
//
// The resolved colors, ONE STRUCT (2026-10-07): the chrome's tones and the
// program's, one field each, and the panel's `face` — every field filled
// off the compiled row (kArcDarkTones, gui_theme.h; Arc-Dark's, architect
// 2026-11-21) by the fill table, in this struct's order
// (kGuiPaletteFill, palette_file.h, whose static_asserts prove it covers the
// struct exactly); the mapping above says what each paints. Every field is
// a solid byte triple; THE THREE CARRIED FILLS' opacity is no field — it
// rides the row's word (tone_alpha above), so the table still covers the
// struct in GuiColors alone.
struct GuiPalette {
    // THE CHROME'S GROUND AND ITS TEXT AND SELECTION PAIRS.
    GuiColor ground;          // the title bar's tone (paint_title_bar)
    GuiColor selected_fill;   // the selection's fill (a lit list row, a held trim end, an editor's selected text)
    GuiColor selected_text;   // the selection's text
    GuiColor field_text;      // an entry's and a list row's text
    GuiColor card_text;       // a standing card's words and glyph; a normal card's are sidebar_text
    // THE FLAT LOOK'S THIRTY (materials.h's head owns each rule; their
    // numbers the compiled row's, gui_theme.h).
    GuiColor body_face;      // THE BODY TONE, the ground under the title bar (`face` below is this tone)
    GuiColor text;            // every word, digit and drawing the chrome prints
    GuiColor title_text;      // the title bar's text
    GuiColor timestamp_text;  // the bottom icon row's timestamp (Arc's `.h4`)
    GuiColor sidebar_text;    // the marker labels' ink, the tooltip's and the cards'
    GuiColor text_dim;        // the inert rows, the tooltip's hint line, a disabled marker's label
    GuiColor inactive_cue_label_hover; // a disabled marker's label ink under the hovering pen
    GuiColor key_dim_ink;     // a disabled key's glyph
    GuiColor key_dim_face;    // a disabled key's face (the key face at 55 % over the body tone; paint_linked_group, paint_word_button)
    GuiColor key_dim_border;  // a disabled key's border on its stretch of the ring (well_border at 55 % over the body tone)
    GuiColor key_face;        // a key's face at rest; the two lamps at rest; the scrub's knob (paint_scale)
    GuiColor key_hover_face;  // that face under the hovering pen, the far end of the hover's mix (materials.h's THE HOVER: the keys, the word buttons, the on-screen keyboard's keys, the scrub's knob)
    GuiColor list_hover_face; // a list row under the hovering pen, the far end of its mix: Arc's row hover, 3 % white over the view (paint_list_view; architect 2026-11-21)
    GuiColor lit_dim_fill;    // a checked-but-disabled key's face, Arc's lit-and-inert
    GuiColor ruler_face;      // the ruler; Arc's entry's fill (paint_entry, materials.h) and the flag editor's (render_flag_editor_box)
    GuiColor ruler_tick;      // the ruler's ticks
    GuiColor ruler_text;      // the ruler's digits (the text at 55 % over the ruler, Arc's `.dim-label`)
    GuiColor lane_face;       // the marker lane
    GuiColor trim_bar_face;   // the trim bar's trough (render_trim_flags)
    GuiColor trim_span_face;  // the trim bar's pill at rest, the span
    GuiColor trim_span_hover_face; // the trim bar's part under the hovering pen, the pill's body or an end's cap (trim_bar_tones)
    GuiColor well_border;     // THE WELL'S LINES, the stereo divider and the status bar's line (paint_well_frame_rows, render_waveform_divider, paint_status_bar); the keys', word buttons' and entries' rings
    GuiColor score_pane_face; // the score viewer's pane, the page's mat
    GuiColor scale_trough;    // the progress page's bar's trough and the scrub's (paint_scale)
    GuiColor scale_slider_border; // the scrub's knob's rim at rest (paint_scale)
    // THE THREE CARRIED FILLS (gui_theme.h's THE THREE CARRIED FILLS): each
    // the tone's color here, ITS OPACITY the row's
    // word's top byte (tone_alpha above), painted at it over what lies under.
    GuiColor tooltip_fill;    // the tooltip's box (paint_shift_tooltip)
    GuiColor card_fill;       // a normal (timed) card's box (paint_notifications)
    GuiColor card_standing_fill; // a standing (critical) card's box
    GuiColor limiter_lamp;    // the title bar's limiter lamp, lit (its own tone)
    GuiColor clip_lamp;       // the title bar's clip lamp, lit (THE TWO LAMPS, the title bar block)
    // THE PROGRAM'S OWN ELEMENTS — THE PALETTE (palette_file.h's rule). The
    // plate's ink and core are no field: the plate takes the row's two words
    // (waveform_plate_inks below).
    GuiColor waveform_canvas;
    GuiColor grid;
    GuiColor center;
    GuiColor cue;
    GuiColor cue_fill;
    GuiColor cue_hover;
    GuiColor cue_outline;
    GuiColor inactive_cue;
    GuiColor inactive_cue_hover;
    GuiColor playhead_head;
    GuiColor playhead_line;
    GuiColor hover_line;
    GuiColor scanner;
    // THE PROGRAM'S PANEL (architect 2026-10-09, Cool Edit's canvas
    // grammar): the ground of everything under the title bar — both icon
    // rows, the status bar, the erase, the modal family's controls row's
    // and the keyboard's ground (the modal family's view wears `ruler_face`,
    // paint_modal_box) — THE BODY TONE `body_face` (architect 2026-11-18:
    // the title tone "would only apply to the top line, the title bar"; the
    // title bar alone paints `ground`, paint_title_bar).
    GuiColor face;
};

// THE ONE ACCESSOR every painter reads. The installed palette is file-scope
// state in render.cpp, written by the install family below alone:
// install_palette at gui_main's startup, before the window exists, and
// install_true_colors. Before the first install it is construction state
// (black), never painted.
const GuiPalette& palette();

// THE SURFACES WHOSE TEXT ROLE MAY DEPEND ON THE CHROME (2026-10-09): each
// surface's recorded text color under the live chrome — "text over a fill
// is the theme's recorded pair" — read by the words painted there, the one
// owner of the choice a later vocabulary with tones of its own would fork
// (no glyph reads it: the project's drawings wear their own inks, and the
// keys' glyphs the tone their caller passes, icons.h's head):
//   ListRow          the list's text pair, resting and lit — the field's
//   ListRowLit       text and the selection's; the folder overlay's and
//                    the project picker's row names (paint_folder_overlay)
enum class GuiSurface {
    ListRow,
    ListRowLit,
};
GuiColor surface_text(GuiSurface surface);

// Install every color off THE COMPILED ROW (kArcDarkTones, gui_theme.h;
// Arc-Dark's, architect 2026-11-21): every member of the palette,
// the chrome's, the program's and the panel's `face` alike, through the
// fill table (kGuiPaletteFill, palette_file.h, proven to cover the struct
// exactly). Its one caller: gui_main's startup, before the window and every
// cache exist, so it owes no apply shape. It bumps palette_generation below,
// as install_true_colors does: the launch's two installs (this one, then
// install_true_colors, gui_main) leave the generation past the construction
// state's 0, so no cache's construction-state fingerprint matches a painted
// palette.
// The few readers that need the sRGB WORD rather than a GuiColor (the glyph
// road's tone, key_ink in materials.h; the carried fills' opacity,
// tone_alpha; the plate's two inks, waveform_plate_inks below) read
// kArcDarkTones itself.
void install_palette();

// THE PALETTE'S GENERATION — a counter the install family bumps, so a
// cached surface whose pixels carry palette colors keys the palette BY
// FIELD (the flag cache, FlagCache::fp_palette_generation, and the stem
// stash its rebuild stages, AppState::staged_marker_stems). The waveform
// plate bakes only its two inks and keys those directly
// (waveform_plate_inks).
uint64_t palette_generation();

// THE INSTALL FAMILY'S THIRD MEMBER, TRUE COLORS (architect 2026-10-08;
// the rule and the ruling at display_transform.h's head): the
// `true_colors` device key (2026-10-21) — read at launch and committed by
// the settings editor — writes the switch's process-lifetime
// bit (display_transform::set_true_colors) and bumps palette_generation, as
// every color the window is handed moves with it. EVERY CACHED CONVERTED
// PIXEL IS REBUILT, against the head's list of converting sites: the flag
// cache by the generation bumped here; the
// waveform plate by its inks' fingerprint (waveform_plate_inks hands the
// window's words). THE APPLY SHAPE THE
// CALLER OWES, after the call (GuiInputHandler::apply_true_colors; the
// launch's call, before the window and every cache exist, owes none): the
// icon rasters dropped (icons::drop_rasters — they convert at raster time
// and key on the zoom and, a key glyph's, its sRGB tone, which the
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
// the open flag editor (render_flag_editor_box), the lead-in ring, the zoom
// anchor's stem and the scanner.
void install_true_colors(bool on);

// -- THE CANVAS COLUMN: THE TRIM BAR, THE RULER, THE PLAYHEAD -------------------
//
// THE WELL — THE CANVAS COLUMN, COOL EDIT'S GRAMMAR IN A FLAT LOOK
// (architect 2026-10-09, the program is Cool Edit; 2026-11-01, "THE MAIN
// PAGE IS SETTLED", the text tones 2026-11-02, and the text round's
// lengths 2026-11-03; ARC'S NUMBERS 2026-11-19): THE TRIM BAR, THE RULER
// FLIPPED and THE MARKER LANE OF CUES directly under the top icon row, THE
// CANVAS under them (THE WAVEFORM block's canvas paragraph) — FLAT REGIONS,
// NO RING, NO SHADOW, PARTED BY LINES of one W in `well_border` (architect
// 2026-11-17; THE WELL'S EDGES at the well-edge accessors below;
// materials.h's THE WELL, every tone's owner), EDGE TO EDGE (architect
// 2026-11-19: no side margin): THE LANES, THEIR LINES AND THE CANVAS STAND ON THE
// PANEL'S COLUMNS, 0 .. 2303 on the tablet's 2304 — the waveform area every
// painter and hit test reads (THE EDGE RULE at well_columns below; the
// keys' outermost faces 6 W inside). Every line in the well is ONE W, rounded at the
// element (hairline_px, the line rule; architect 2026-11-04, the unit; one
// W = 2 device px on the tablet at 200 %; the ruler's ticks and every
// canvas line the same W, waveform_line_px). THE ROWS ON THE TABLET (200 %;
// main.cpp's lane table the record; the key 28 W tall, kKeyHeightW): rows
// 140 .. 141 the line under the top icon row, the
// trough 142 .. 165, rows 166 .. 167 its line, the ruler 168 .. 199, rows
// 200 .. 201 its line, the marker lane 202 .. 233, rows 234 .. 235 its
// line, the canvas 236 .. 1307, rows 1308 .. 1309 the well's foot
// (architect: "waveform height ok to take from"; "it's okay if it takes
// away from the waveform height", 2026-11-18).
//
// THE TRIM BAR IS ARC'S SCROLLBAR (render_trim_flags owns the pill's
// drawing, paint_trim's ground step the trough and the lines, trim_lane_h_px
// the rows; architect 2026-11-17): under the
// line under the top icon row, THE TROUGH (trim_trough_h_px, 12 W, 24 device rows at
// 200 %) across the waveform area's columns in `trim_bar_face` (Arc's scrollbar
// trough, its line of kArcDarkTones), and on it THE SPAN AS
// ARC'S SLIDER, A PILL (trim_pill_h_px, 6 W, centered, the seat of trough above
// and below), the trim window from the begin bound's exact place to the end
// bound's (architect 2026-11-14) — THE SLIDER'S BOX, the pill its core
// (architect 2026-11-18, THE SLIDER'S BOX AND ITS CORE in the lanes block
// below, which states the seat): EACH END THE SEAT INSIDE ITS BOUND AND ROUND,
// radius half the pill's height, AT ITS REAL PLACE, and the strip's edge — the
// lane's clip
// on the waveform's columns — cutting the core wherever it runs past, so a
// core that goes on past the edge is cut square and flush there and a
// round end leaving it is cut column by column (the no-grid rule); a short
// span narrows the inset (render_trim_flags states the rule) and a pill
// narrower than its height has the radius half its width
// (coverage_of_rounded_rect, coverage_stamp.h). No outline. Then the
// well's line under the trough, across the panel's width.
// NO PLAYHEAD IN THE BAR (architect 2026-10-09 ~14:30, "I thought we agreed
// no dots on the trim bar"). THE BAR'S STATES AND ITS TWO CAPS (architect
// 2026-11-19 / 2026-11-20; THE TRIM BAR'S CAPS AND GRABS in the
// lanes block below, the one table at trim_bar_tones): at rest the pill
// alone in `trim_span_face` (Arc's scrollbar slider), as Arc
// and Kdenlive draw it; under the hovering pen, the bar's own press or a
// TRIM sweep that wrote, A CAP AT EACH END OF THE PILL — the pill's own end
// cut by one vertical line, ours — and the part under the pen or the press
// in Thunar's hover tone `trim_span_hover_face` or Arc's slider:active
// `selected_fill`, a TRIM sweep's moving end wearing `selected_fill` as a
// held end does (architect 2026-11-20: "it should look like the
// end endcap is being dragged b/c effectively that is what it is";
// trim_bar_look, app_state.h). The lane's grabs are the
// span's two ends and its body (TrimBarHit, below) over the lane's whole
// rows — a press need not land on the pill.
//
// THE RULER IS COOL EDIT'S, FLIPPED (paint_ruler_row):
// FLAT `ruler_face`, THE CANVAS'S TONE (architect 2026-11-17) directly
// under the trough's line, on the waveform area's columns, NO LINE OF ITS
// OWN ON ITS TOP; THE TICKS `ruler_tick`, ONE W WIDE (the canvas's line,
// waveform_line_px, 2026-11-04), EACH CENTERED ON ITS OWN EXACT TIME'S
// PLACE at its exact coverage (architect 2026-11-14; the line rule,
// waveform_line_span), STANDING ON THE
// RULER'S FOOT, the major 2 W and the minor 1 W (4 and 2 device rows at
// 200 %, scaled_px), overlaying its bottom pad, NO SHADOW; THE DIGITS the
// ruler face (GuiFace::Small, BOLD 10 W, 20 device px at 200 %, Arc's
// `font-size: smaller`) in `ruler_text` (the text at 55 % over the ruler,
// Arc's `.dim-label` of the font color, arc_dark 9197A2: under the head
// A5ABB5, over the hover line 767B87; architect 2026-11-21), bare, ON THE
// LINE-BOX BASELINE (ruler_baseline_px, the ruler block below); then THE
// WELL'S LINE, one W, the ruler lane's last (ruler_lane_h_px).

// THE PLAYHEAD IS COOL EDIT'S CURSOR (architect 2026-10-09):
// ITS HEAD the cue's chip, THE SAME DRAWING (architect 2026-11-04,
// "4 accept overlap") — A SMOOTH-EDGED TRIANGLE AT THE PLAYHEAD'S EXACT
// PLACE, its apex on the line's center (architect 2026-11-14;
// cool_edit_paint.h's THE CUE'S CHIP, the lengths the cue block's below),
// NO OUTLINE (2026-11-19; 16 x 8 at 200 %), the whole triangle
// THE PALETTE'S `playhead_head` (THE PLAYHEAD WEARS ITS TWO COLORS,
// below), NO SHADOW
// (2026-11-02), ON THE RULER'S LAST cue_chip_h_px DEVICE ROWS, its apex on
// the bottom
// edge of the ruler's foot row over the ticks' feet — it covers what stands
// under it (2026-10-09 ~21:00), the digits included where it stands under a
// label (ACCEPTED, architect 2026-11-04: "timestamp not really important
// for fine tune, just ballpark") (paint_ruler_row; its painted reach the
// cull's and the move's damage box's, cue_chip_paint_reach_px, its hit the
// chip's hit extent); IN THE
// CANVAS A SOLID LINE, ONE W, AT ITS FRAME'S EXACT PLACE (architect
// 2026-11-04; the exact place architect 2026-11-14, waveform_line_span;
// canvas_line, the scanner's form) in
// THE PALETTE'S `playhead_line` (arc_dark A5ABB5), down
// the area's whole height, the stereo divider's row included (architect 2026-11-02) — ONE STATIC
// TONE, opaque on the pixels it covers whole and the same over the ink and
// over the canvas, blended over them by its coverage on the pixels its
// edges cross (architect 2026-11-04, after the measured cost of a fading
// line; render_playhead; paint_playheads); nothing of it is
// damaged at rest. Across the trim bar (architect 2026-10-09 ~14:30, "no
// dots on the trim bar"), the marker lane and the well's line under it it
// draws nothing. IT MAY STAND ON A MARKER — NO SNAP, NO AVOIDANCE
// (architect 2026-10-09): the head and a cue's chip stand in different
// lanes, and ON A SELECTED MARKER, enabled or disabled, the marker's
// `cue_outline` stem
// stands in the line's place (the paint order playhead, stems, scanner; THE
// MARKER LANE's selection stem, below; the line under a stem on its own
// exact place is not drawn, paint_playheads' THE COINCIDENT LINES), so the
// dark stem is what shows there and the ruler's head marks the playhead; on
// any other marker the line shows as it stands.
// THE HEAD IS GRABBED AND DRAGGED, as Cool Edit's is (architect 2026-10-09
// ~17:40): its HIT is the head at its exact place, the chip's hit extent
// (cue_chip_hit_left_px, cut to the waveform's columns) over its device
// rows of the ruler's, real edges tested with the pointer's real place
// (architect 2026-11-15, the no-grid arc), the painted head within a px
// of it and no box beyond — published by paint_ruler_row
// (AppState::playhead_head_hit) and read by point_on_playhead_head
// (input_pointer.cpp), a cue over the head keeping the press; a plain press
// there drags the playhead, every motion past the slop the ruler's
// placement act at the pointer's place (ScrollDragState::head_drag).
// THE HEAD HAS NO HOVER (architect 2026-11-21, of a lit triangle: "very
// hard to see ... very hard to stably trigger"): it paints
// `playhead_head` flat and nothing else, and nothing of it is damaged at
// rest or under the pen. A motionless tap on it places the playhead as any
// ruler tap does, so the waveform's hover line stands over it as over the
// rest of the ruler, its triangle under this head (render_hover_line).
// NO HOLD LAMP (architect 2026-10-05): the head paints the same whether or
// not AppState::camera_hold stands.
// THE PLAYHEAD WEARS ITS TWO COLORS: `playhead_head` is its head, A5ABB5,
// the stem's own number ("the triangle for the playhead should probably
// become the same color as the stem", architect 2026-11-21), and
// `playhead_line` its line, A5ABB5 (architect 2026-11-21: "the stem is too
// bright" at the font color; the source at their lines of kArcDarkTones,
// gui_theme.h); neither is
// lent to a marker (the selection is the
// chip's, THE MARKER LANE below). THE STEMS THAT BELONG TO THE
// CONTROLS ARE WHITE, ONE TONE, `scanner` (architect 2026-10-09: "the zoom
// stem and the scanner are both white, which unifies them: the non-playhead
// stems that are related to the controls should be white, and then the
// playhead is [colored]"): THE SCANNER — the moving playback line,
// paint_scanner — a SOLID line one W wide over the canvas, at the predictor
// position's exact place (Cool Edit's playback cursor is a solid line; one
// W, waveform_line_px's rule; the exact place architect 2026-11-14), and
// THE ZOOM ANCHOR'S STEM
// (render_strip_anchor_stem), the same solid line at the pivot's exact
// place;
// WHITE FFFFFF — COOL EDIT'S OWN CONSTANT (architect 2026-10-16, observed
// in Cool Edit Pro 2.1: the scanner is white on every preset he tried; the
// zoom anchor's stem is the same design, so both are canonical; his
// "let's go back to a white scanner", 2026-10-07).

// -- THE MARKER LANE: COOL EDIT'S CUES ---------------------------------------------
//
// THE FLAG IS COOL EDIT'S CUE (architect 2026-10-09; the lengths program_spec.h's,
// the drawing render_flag_boxes_impl's): on the lane's FLAT `lane_face`
// (materials.h),
// THE CHIP — COOL EDIT'S TRIANGLE, NO OUTLINE (architect 2026-11-19: "the
// smaller version without the outline"), A SMOOTH-EDGED TRIANGLE AT THE
// MARKER'S EXACT PLACE (architect 2026-11-14: "I think the chips should
// also go anti-aliased";
// cool_edit_paint.h's THE CUE'S CHIP owns the drawing, the cue block below
// its lengths: its rows scaled, cue_triangle_rows, 4 W, 8 at 200 %, 4 at
// 100 %; Cool Edit's own 9 by 5): the triangle of the nominal rows (16 x 8
// at 200 %), on the lane's last rows, its apex ON THE STEM'S CENTER on the
// bottom edge of the lane's last row, directly above the well's line
// under the lane, NO SHADOW (2026-11-02);
// THE LABEL — the marker's one label, at rest in `sidebar_text` (Arc's
// sidebar text, arc_dark BAC3CF; an inert label `text_dim`; architect
// 2026-11-19; resolve_flag_face's table), NO SHADOW (2026-11-02), in
// THE LABEL FACE, REGULAR Label 12 W, 24 device px at 200 % (gui_font.h's
// kGuiFaceSizes; "yes, regular is good"), starting the chip's reach
// right and a gap of 5 W
// right of the mark (cue_label_lead_px: 9 W, 18 device px at 200 %) —
// STANDING WITH ITS MARK EXACTLY, its pen the cue's exact place plus that
// lead, a real x (architect 2026-11-15, the no-grid arc: "looks better
// because it just feels more cohesive now. The whole thing is just one
// unit"; show_cue_label's head, paint_cues) — ON
// THE LANE'S ONE BASELINE
// at rest and in the editor (cue_baseline_px, the line-box rule at
// marker_lane_h_px: +12 W of the lane's 16, 24 device rows at 200 %) —
// `show_cue_label`,
// render.cpp,
// the one painter of every label look on the face; and THE STEM, ON EVERY
// SELECTED MARKER, ENABLED OR DISABLED (architect 2026-11-11)
// — a SOLID ONE-W LINE in the `cue_outline`, centered on
// the marker's frame at its exact place (architect 2026-11-14, the
// line rule at waveform_line_span),
// FROM THE CHIP'S APEX ROW (the
// lane's last) through the well's line under the lane down the canvas's
// whole height (waveform_line_px;
// paint_marker_stems and its head segment, paint_marker_stem_heads;
// architect 2026-11-04, "3 yes"): THE SELECTION STEM (architect
// 2026-11-05). NO OTHER STEM (any other "dropped, along with the rest of
// cool edit stuff except a few details like the triangle playhead"). No box
// round the label.
//
// THE CHIP CARRIES THE SELECTION, THE PLAYHEAD THE FOCUS (architect
// 2026-11-04: "we don't use ableton's rect above a marker and could
// therefore allow the triangle chip to stand for selection/focus";
// 2026-11-05, "only 1 playhead onscreen ever"): EVERY ENABLED CHIP'S FILL
// the palette's `cue_fill`, THE ACCENT (the lit keys' face; architect
// 2026-11-13: "we revert the rule so that markers get accent color instead
// of icon glyph color"; 2026-11-01: "one color for all markers" — every
// cue, the history's added and changed items and a struck marker's
// included), NO OUTLINE (2026-11-19: the bare triangle of the nominal rows,
// the cue block below), and THE SELECTED marker's mark THE STEM in the
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
// one. Very easy to see. So just the strike through on the text"): a
// coincident WARP
// marker (the same frame or under one millisecond from a neighbor, disabled
// rows counted, the parser's runs — warp_frame_map_view.h's red-flag set,
// architect 2026-11-17; a phase reset is never struck, anything goes on its
// column)
// wears the plain cue with its label STRUCK (show_cue_label's bar, the face's own
// stroke on its own strikeout rows, gui_font_strike_band). THE TABLE —
// which fill, which
// stem, which label, which strike, AND WHAT EACH WEARS UNDER THE HOVERING
// PEN (its four hover pairs: a cue a tap would act on lights its chip and
// its label over Arc's 200 ms, a disabled one in its own pair, a grey chip
// and a dimmer label;
// architect 2026-11-21; the gate, the fade, the key and the damage
// AppState::FaceFades' THE MARKER'S FACE) — is resolve_flag_face's
// head (render.cpp), the one resolver every cue goes through.
//
// THE LABEL'S SEGMENTS: the marker's payload label, then — while grid
// iterations paint them — its LOWER and UPPER bound cells, each a segment of
// the same label standing the segment gap past the one before it (a
// history item paints the payload segment alone, its one label, 2026-11-11),
// every segment's text the label face on the lane's baseline.
//
// THE OVERLAP RULE: LATER WINS BY OCCLUSION, NEVER BY CUTTING (architect
// 2026-10-10, on two near-coincident phase resets: "why not go simpler —
// later always wins?", and "a long
// flag before a short one should continue after the short one ends"). The
// labels paint LEFT TO RIGHT, each OWNING AN OPAQUE BOX of the lane's own
// `lane_face` from ITS OWN
// CHIP'S LEFT HIT EDGE (the cue's exact place less cue_chip_hit_left_px) to
// one fill pad past its last segment's text, on THE WHOLE LANE'S ROWS (the
// press target's rows, so the label is one geometry at rest and selected,
// 2026-10-09 ~21:00), so
// a later cue hides an earlier label's text only across its own
// chip-to-end extent and an earlier long label's text resumes past a
// short later one — THE BOX'S EDGES REAL, the earlier text cut on them at
// exact coverage, and the box itself never painted (the lane under the
// cues is already its color; paint_cues, render.cpp; architect 2026-11-15,
// the no-grid arc). Then the chips paint LEFT TO
// RIGHT over every label, each at its mark's exact place, so a right chip
// stands over its left neighbour's text and chip, its edge soft. The press follows the picture (topmost_flag_rect, app_state.cpp):
// the rightmost chip, then the rightmost label box, under the point.
//
// THE STATES (resolve_flag_face, render.cpp, the one table: the disabled
// axis picks the chip's fill and the label's ink, the selection alone the
// stem — the two never cross (architect 2026-11-11); the strike
// rides every row):
//   UNSELECTED  the `cue_fill` fill alone, no outline,
//             the label in `sidebar_text` at rest and `text` under the
//             hovering pen (cue_label_ink, render.cpp); no stem,
//             the playhead on it or not (architect 2026-11-05);
//   SELECTED  a member of the selection set, enabled or disabled: its own
//             chip as at rest (the `cue_fill` fill, or the disabled grey), and A SOLID
//             ONE-W `cue_outline` STEM at its exact place, under the
//             chip's apex, FROM THE CHIP'S APEX
//             ROW (the lane's last) THROUGH THE LINE UNDER THE LANE
//             TO THE CANVAS'S LAST ROW (2026-11-04: no gap under
//             the apex), whether or not the playhead stands on it (architect
//             2026-11-05; MarkerStem, paint_marker_stems — THE RESOLVER
//             NEVER SEES THE PLAYHEAD). THE STEM STANDS IN THE PLAYHEAD'S
//             LINE'S PLACE (the paint order playhead, stems, scanner): both
//             are one W on one exact place, so the line under the stem is
//             not drawn (paint_playheads' THE COINCIDENT LINES, architect
//             2026-11-14); while the playhead rests on a selected marker
//             the dark stem is what shows and the ruler's head marks the
//             playhead.
//             THE LABEL CHANGES NOTHING: it paints as at rest, no fill, its
//             box the resting one. THE PRESS TARGET STAYS
//             THE WHOLE LANE (architect 2026-10-09 ~21:00, "the text box
//             should take up the whole marker lane … right now it's too
//             small to click"): the box is geometry, not paint. THE EDITOR
//             is where the selected pair's fill appears: it opens with the
//             whole text selected in the selection pair inside its field
//             (EDITING, below). A history item is one cue and stands one
//             stem the same way (render_history_diff_flags);
//   STRUCK    the red-flag class (warp_red_flag_set_cached and its
//             phase-reset twin — the coincident run, a warp label ref
//             the render falls back from, and the definitions of a label
//             two live definitions hold, 2026-10-29): its label STRUCK —
//             the bar part of the text, in its ink, in every look (the
//             inactive label's too). IN THE HISTORY VIEW THE STRIKE MEANS
//             THE TEXT CHANGED and THE DISABLED STYLE MEANS REMOVED
//             (architect 2026-11-11; render_history_diff_flags
//             and resolve_flag_face's history rows own them);
//   DISABLED  A SOLID GREY CHIP (architect 2026-11-05, "disabled marker
//             gets dim label"; the inert look's reason 2026-10-10, "the
//             arrow needs to be legible for me, but the text doesn't …
//             since it's disabled, it's inert"): the `inactive_cue` fill
//             alone, no outline, the label inert,
//             the playhead on it or not; the same inert label on a tie
//             follower's cells (one look for inert cue text). A disabled
//             marker is selectable (architect 2026-10-07 ~15:10), and
//             SELECTED IT STANDS THE SELECTED ROW'S STEM (architect
//             2026-11-11: "disabled markers also get the dark
//             stem, same as enabled ones"), the stem the `cue_outline`
//             whatever the chip's fill, its chip and label unchanged — so a
//             disable toggle of a selected marker flips the chip's fill and
//             the label and leaves the stem.
// A CUE'S HOVER IS ITS CHIP'S AND ITS LABEL'S INK (architect 2026-11-21):
// under the hovering pen, where a tap would act on it, each goes toward its
// hover tone over Arc's 200 ms and back — resolve_flag_face's table, its
// four hover pairs; the stem, the selection's, is untouched. A CUE HAS NO
// PRESS FACE: the press look is the keys' and the lists' rows' alone
// (materials.h's head),
// never a marker's; under a contact its hover fades out.
//
// THE PHASE-RESET LEAD-IN RING on the waveform is FOUR SOLID ONE-W LINES in
// the `cue_outline` (architect 2026-11-04;
// paint_phase_reset_overlay_ring — the ring and the cue one unit, architect
// 2026-09-17), EACH AT ITS EXACT PLACE (architect 2026-11-14): its
// left side centered on the reset's frame, its right side flush inside the
// seed grain's end, both on the canvas's rows between its top and bottom
// runs, the runs along the canvas's first and last W of rows, WHATEVER THE
// SELECTION — every live reset's, a coincident one's too, its struck label
// the mark (2026-10-26); a disabled reset paints no ring.
//
// EDITING: THE FIELD IS GTK'S IN-PLACE CELL EDITOR ON THE LANE'S WHOLE ROWS
// (architect 2026-11-18 / 19: Arc's `treeview entry:focus`; the drawing
// render_flag_editor_box's), THE
// PAYLOAD EDITOR AND THE ITERATION-BOUND EDITOR ALIKE: A SQUARE-CORNERED
// BOX, its fill the entry's tone (`ruler_face`, what paint_entry lays;
// arc_dark 404552), its BORDER ONE W OF THE ACCENT (`cue`) on all four
// sides (cue_editor_border_px), no radius; the text Regular 12 W in the
// field text (`field_text`, the text tone), THE CARET ONE W in it, THE
// SELECTED SUBSTRING the selection pair (`selected_fill` under
// `selected_text`: on arc_dark the accent under the lit print), the caret
// not drawn while a selection stands (text_editor::cursor_visible_now) — the
// caret and the selection band on THE TEXT'S LINE BOX ROWS
// (cue_line_box_top_px, cue_line_box_h_px), which in the 16 W lane inside
// the one-W border are the box's whole inner 14 W (the value editors'
// rule, one look for both editors). THE GEOMETRY IS THE SEGMENT'S: the box
// over the edited segment's box on THE WHOLE LANE'S ROWS (the border on the
// lane's first and last rows and just outside the fill's pads, one W
// either side of the run, cue_fill_pad_px), the text at the segment's own
// seat ON THE LANE'S ONE BASELINE — the resting label's (cue_baseline_px)
// — so the text does not move when the editor opens. The field grows and
// shrinks with what is typed, the segments to its right riding its edge in
// their resting look. A COINCIDENT MARKER'S FIELD IS THE SAME FIELD, its
// text and caret the field's own, NEVER STRUCK (the text there is being
// edited), its riding segments struck as at rest. WHY A FIELD: it is the
// legibility guarantee ("when you're looking very closely at a label it has
// to be legible"), and the selection pair is the highlight "precisely to
// avoid this problem". THE BOX PAINTS OVER THE LANE, the triangle and any
// neighbouring label ("it may overlap with the flag and other elements,
// that's fine"). Outside the editor a selected label is its resting text,
// no fill and no box (SELECTED, above); the open editor shows its whole
// text selected in the selected pair inside the field.
//
// A REFUSED ENTER RECOLORS NOTHING (architect 2026-10-03: "a red outline and
// the card is redundant"): THE FIELD STANDS AS IT WAS, its text, caret and
// selection untouched (text_editor::refuse, architect 2026-11-27, GTK's
// way), and the refusing owner's CARD says why. No glyph marks it.
//
// THE HISTORY VIEW'S DIFF FLAGS are cues of their own three kinds, ONE CUE AN
// ITEM (architect 2026-11-11; HistoryDiffFlag,
// render_history_diff_flags own the rule): a REMOVED item in the disabled
// style, its label struck; an ADDED item in the enabled style, plain; a
// CHANGED item in the enabled style, its older text struck and its newer
// text plain beside it — each side's label its flag text with the `#` where
// that line is disabled and no sign (history_diff_label, paint_handler.h;
// architect 2026-10-26: "those are always hard to see anyways at the font
// size. A strike through is easier to see"). The view's focus or selection gives the
// item the live lane's SELECTED row — its stem, its chip and label as at
// rest.

// -- THE WAVEFORM ------------------------------------------------------------
//
// THE WAVEFORM IS THE PROGRAM'S, A CANVAS UNDER ONE INK (the
// `waveform_canvas` and `waveform_ink` tones): with the magnification lamp
// dark the plate is the raw bar alone in the ink; lit, the plate is two bars
// (the rule is at render_waveform_columns' declaration), THE OUTER IN THE INK
// AND THE INNER A SOLID FILL OVER IT in the `waveform_core` tone (architect
// 2026-11-04, THE CORE below; antialiased with its shape on the GPU,
// 2026-10-30) — the row's `waveform_core`, THE KEY FACE (architect
// 2026-11-07): 444A58 over the 262A33 ink on the 404552 canvas, one step
// over the canvas (THE CORE below; the number's source at its line of
// kArcDarkTones, gui_theme.h). THE PLATE WEARS THE INK AND THE
// CORE AS PUBLISHED (the sink writes their words, GuiCanvasSink::plate),
// so the pair rides the cache's fingerprint (waveform_plate_inks) and
// publishes with the plate; the canvas is laid live under the plate's
// transparent gaps (render_canvas).
//
// THE CANVAS IS COOL EDIT'S (architect 2026-10-09):
// THE WAVEFORM AREA IS THE CANVAS'S INTERIOR AND THE WELL'S COLUMNS (the
// well's paragraph above; waveform_area, main.cpp; architect 2026-11-03), so
// every column mapping, hit test and damage box reads the interior and the
// canvas reads the well's whole width with the ground directly beside it.
// THE ORDER, bottom to top: UNDER THE WAVEFORM, in render_canvas's
// one pass, the `waveform_canvas` fill and THE HORIZONTAL GRID — per
// channel, a `grid` line one W tall (the unit of 2026-11-04) CENTERED ON
// ITS EXACT ROW (architect 2026-11-14; canvas_hline_span), the
// channel's exact zero ± k · H / 4 rows for k = 1, 2, 3, H the
// channel's half height (its zero to its band's edge; the k = 4 line
// would stand on the band's edge and is not drawn), soft where that row
// falls between device rows, the canvas's full width,
// Cool Edit's grid under the ink; then THE WAVEFORM'S PLATE (antialiased,
// its soft edge blended over the canvas and the grid,
// render_waveform_columns' head); then THE CENTER
// LINE per channel centered on its exact zero, one W, in `center`, OVER THE INK
// (architect 2026-10-09 evening: "it's supposed to be visible over the
// waveform. Otherwise, in a project like this with tape hiss, there's never
// enough zero that the red line would become visible ever";
// render_center_lines, a live pass after the plate, the plate wearing its
// two inks alone); then THE STEREO DIVIDER'S ROW between the two
// wells (architect 2026-10-16, chrome on the canvas, waveform_divider;
// render_waveform_divider, over the plate so a plate from before a resize
// never crosses them); then THE WAVEFORM'S HOVER LINE, the first vertical
// line, under every other (ITS ORDER at render_hover_line below); then
// the lead-in ring, the playhead's line, the selection stems over it, the
// scanner and the zoom anchor; and LAST, while a sweep's pointer is down, THE SWEEP HIGHLIGHT
// (paint_sweep_highlight, architect 2026-11-12: a blend toward white over
// everything in the waveform area — the canvas, the grid, the ink, the
// core, the center lines and every line above them) (build_canvas_list's
// order). THE PICTURE IS ONE THING — the
// canvas, the grid, the ink if any, the center lines — SO THE CENTER LINES
// PAINT WHENEVER THE GRID DOES: over the plate while audio is loaded (a
// frame before the first plate included, its plate record absent), and with
// none loaded — no source, or a load running — directly over the canvas and
// its grid, right after render_canvas, the divider after them
// (build_canvas_list's `plate_branch`).
// NO VERTICAL GRID (architect 2026-10-09: Cool Edit's verticals on the
// ruler's major ticks "compete" with the markers — "drop them"). HIS
// PRINCIPLE, recorded
// here: "accuracy is what applies to the conversion from Cool Edit Pro, and
// accuracy is lower priority than usability" — Cool Edit's rule yields
// where usability says so, while "truthfulness applies to the way we
// interact" (an invisible hit box is untruthful; on screen is as painted).
// A recorded departure from Cool Edit, as THE CENTER LINE OVER THE INK is.
// The waveform's plate is drawn over the ground and the grid, the ink over
// every grid line, the center lines over the ink. Each
// channel's band and zero are the plate's own (waveform_channel_band
// below), so the lines stand where the plate's zero is. THE CHANNELS FILL
// THEIR WELLS (architect 2026-10-09, "remove", the channels
// top to bottom; the two wells 2026-10-16): each takes the canvas's height
// less the stereo divider, halved and floored, channel 0 from the canvas's
// first row and channel 1 under the divider (an odd remainder the canvas's
// last row, below channel 1, no ink reaching it), with NO INSET above or
// below — the playhead's head and the cues' triangles stand above the
// canvas, so nothing needs one; the stem, the playhead, the lead-in ring and the
// scanner paint over
// the ink there as they do everywhere in the wells. The leveler's headroom
// (waveform_gain.h's constants: its target, and the inner bar's foreground
// half) is the only thing keeping the peaks off the edges; an outer bar the
// clamp clips flat (the loud body, accepted there) stands on its band's edge
// row — against its well's edge. NO BOUNDARY LINES (architect 2026-10-09,
// "we have very limited vertical real estate"): Cool Edit's Bndy clip guides
// are not drawn. BETWEEN THE CHANNELS STANDS THE STEREO DIVIDER (architect
// 2026-10-16, waveform_divider below; one line of `well_border`,
// 2026-11-17), a chrome row the plate and the horizontal lines leave alone,
// every vertical line crosses (architect 2026-11-02: "the divider is just
// another grid line now") and every input leaves alone
// (point_on_waveform_divider). THE SELECTION STEMS, THE PLAYHEAD'S LINE and the scanner
// span the canvas's whole height, the divider's row included; THE ZOOM
// ANCHOR'S STEM spans it too.

// THE PLATE'S TWO BAKED INKS, as words: the fingerprint field and the job
// field that carry the active ink and core to the worker (the worker reads
// no live state), the compiled row's two tones (kArcDarkTones'
// `waveform_ink` and `waveform_core`, gui_theme.h). THE WORDS ARE THE WINDOW'S
// (architect 2026-10-08, display_transform.h's head): waveform_plate_inks
// hands the two tones through display_transform::display_rgb on the GUI
// thread, so the plate writes them as they come, and a flip of True Colors
// (install_true_colors) moves the fingerprint exactly when it moves a
// colored ink — a gray ink converts to itself and re-renders nothing.
struct WaveformPlateInks {
    uint32_t ink_rgb  = 0;
    uint32_t core_rgb = 0;
    bool operator==(const WaveformPlateInks&) const = default;
};
WaveformPlateInks waveform_plate_inks();

// -- GUI scale ---------------------------------------------------------------
//
// THE PRODUCT'S ONE SCALE AXIS. The gui_scale setting is an integer PERCENT
// in [50, 1000] — the RANGE's one owner is is_gui_scale_percent
// (device_config.h), where the bracket and its four landmarks are spelled
// once. It is a PER-DEVICE preference, read out of the device config; the
// current value lives as file-scope state in render.cpp, pushed by TWO
// application points (gui_main's startup, before the window exists, and the
// settings editor's `gui_scale=` commit).
//
// THE UNIT IS ONE W, ONE ARC PX AT 9 pt (the one statement at scaled_px
// below): every authored chrome length is a whole number of W, and
// gui_scale is the number of DEVICE px per W, in percent — the tablet 200
// (architect 2026-11-05; a 16-W glyph = 32 device px). EVERY PAINTED
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

// Scale factor s = gui_scale / 100, device px per W: as low as 0.5, the
// setting's grammar floor of 50 (architect 2026-08-10).
double gui_scale_factor();

// THE KEY'S BOX AND THE RUN GAP, IN W (architect 2026-11-22: "the regular
// key height and key width are from Arc, and also the [run] gap I think is
// from Arc ... okay to retire all of them"): ARC'S ICON BUTTON AT 9 PT, ITS
// BORDER INCLUDED — 36 W wide by 28 W tall, 72 x 56 device px on the tablet
// at 200 % (architect 2026-11-18 / 2026-11-19: "about 72 device pixel
// width ... the border is clickable"; materials.h's THE KEY owns its look)
// — and ARC'S GAP BETWEEN TOOLBAR GROUPS, 6 W, 12 device px (the groups 6 W
// apart). The box's one readers are icon_case_w_px and icon_case_h_px (the
// case paragraph below) and the line-box baselines of the text on key-tall
// boxes — the entries (entry_baseline: the clock's, the NOISE case's, the
// fields') and the word buttons' words, in paint_handler.cpp; the gap's is
// program_run_gap_px (the run gap paragraph below). THE ON-SCREEN
// KEYBOARD'S KEY IS THIS BOX ROTATED, 28 W wide by 36 W tall, its band's
// two pads the run gap (architect 2026-11-26: "the regular icon size
// rotated 90 degrees ... And the run gap should be the padding above and
// below the ... keys"), read through the same two accessors and
// program_run_gap_px (onscreen_keyboard.h's geometry). The key's drawing is
// a fixed 16 W box whatever the box (paint_key_glyph).
inline constexpr int kKeyWidthW  = 36;
inline constexpr int kKeyHeightW = 28;
inline constexpr int kRunGapW    = 6;

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
// 2026-10-31 afternoon). The scale seat's shape — file-scope
// state in render.cpp,
// pushed by gui_main's startup read of the device config before the window
// exists and by the settings editor's `waveform_grow=` commit and its value
// list's tap (GuiInputHandler::apply_waveform_grow, which repaints at once:
// no plate re-render, the tips do not move). The construction state is 1
// (kWaveformGrowDefaultQuarters), the first-run value the shipped defaults
// file's. A PERMANENT KEY: this pair stays.
void set_waveform_grow_quarters(int quarters);
int  waveform_grow_quarters();

// THE PLATE'S FADE START (architect 2026-11-14; THE FLOOR, THE WHOLE
// DOUBLING: "Wouldn't it be better to have a continuous fade motion"; THE
// NUMBER, architect 2026-11-22: "one is the ideal and it works well"): F,
// the bar width in DEVICE PX at which the finer level starts to appear,
// held in HUNDREDTHS of a device px — 100, ONE DEVICE PX, so the band runs
// from the landing on one level to the switch to the next (the rule at
// waveform_fade_band below, THE PLATE'S BARS' fade band). A start under 1
// would name a bar width no rung draws (a main rung's bars are a px or more
// wide); 2, the switch width, would be a band of no length (render.cpp's
// static_assert binds F under the switch). Read by waveform_fade_band
// alone, so the plate's render carries no fade input and its fingerprint
// no fade field.
inline constexpr int kWaveformFadeHundredths = 100;

// A FACE AT THE LIVE SCALE (gui_font.h): what every painter and every seat
// asks for its text — the use at the current gui_scale, its size the
// face-size table's (gui_font_table_px, read off the face by every reader;
// architect 2026-11-26: the Keys face too). THE ONE ROAD a GuiFont is built
// by (GuiFont's own paragraph).
inline GuiFont gui_font(GuiFace face) {
    return GuiFont{face, gui_scale_percent()};
}

// One authored length in W -> device pixels, the ONE conversion every
// scaled dimension in the tree takes: device = std::nearbyint(w × percent /
// 100), like every other integer-domain conversion.
//
// THE UNIT (architect 2026-11-05: "do the rebase to 200%. output should be
// pixel identical"; "seems odd to use 300% and 2dpx"): ONE W = ONE PX AT
// 100 % = 2 DEVICE PX ON THE TABLET AT ITS 200 %. EVERY AUTHORED LENGTH IS A
// WHOLE W (an integer), and a line is one W (hairline_px below). A FACE'S
// EM IS NO LENGTH: it is its source's exact number, never rounded, and of
// the text's geometry only the line's box is whole W (gui_font.h's head,
// architect 2026-11-26). NO EXCEPTION AMONG THE LENGTHS: the gesture
// thresholds are whole W too (the hover slop kHoverSlopW 5,
// kRulerMinMinorPitchPx 14 in paint_handler.cpp, kNavZoomPxPerLevel 218
// in app_state.h, each rounded up from a half W, architect 2026-11-05:
// "round each up"), each seat naming its one-device-px consequence. 300 %
// is never proposed again (closed_questions.md).
//
// ROUNDED AT THE ELEMENT, NEVER AT THE SCALE, AND A COMPOSITE IS BUILT FROM
// ITS ROUNDED PARTS (architect 2026-10-02): a length that is the sum of
// elements — a button case, a lane's height, a row's walk — is the sum of
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
// ONE ROAD: every W-authored constant in the tree (`k…W`, program_spec.h's
// fields) and every `k…Px` constant scale through
// it alike.
inline int scaled_px(double authored) {
    return static_cast<int>(std::nearbyint(authored * gui_scale_factor()));
}
// The floored form: `floor_px` is the PER-METRIC minimum the accessor states,
// so a small factor cannot zero a structural dimension. Each floor is a
// constructive domain invariant: it never refuses and clamps no setting.
//
// THE FLOORS ARE LIVE, NOT DEFENSIVE (the gui_scale floor is 50, architect
// 2026-08-10): at s = 0.5 every AUTHORED 1 px rounds
// to 0 — std::nearbyint is banker's rounding and takes 0.5 DOWN — so each 1px
// border, edge, pad and separator in the tree reaches its floor and paints as
// the one pixel that keeps the surface visible. A metric whose authored value
// may legitimately vanish floors at 0 and says so at its own accessor.
inline int scaled_px(double authored, int floor_px) {
    const int v = scaled_px(authored);
    return v < floor_px ? floor_px : v;
}

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
// place to change it. It is 0 — the lanes of each strip touch, and the
// waveform-side and outer (window-edge) gaps (both kFlagBottomLiftPx, also 0)
// vanish, so lanes and strips pack tight against each other and the window
// edges. Stays a compile-time zero under scaling — zero is
// scale-invariant.
// Read unscaled like its sibling above, exact at 0.0 and owing a `scaled_px`
// the moment it is un-zeroed (the note at kFlagBottomLiftPx).
constexpr double kRowGapPx = 0.0;

// Defensive window floor (a conservative 640x480 minimum), enforced by an
// internal clamp in the geometry helpers so the waveform arithmetic is always
// valid regardless of what size the window reports. Not sized to fit content — the longest page
// may clip at the floor, which is acceptable (nobody authors at 640x480).
constexpr int kMinWindowWidthPx  = 640;
constexpr int kMinWindowHeightPx = 480;

// THE APP PAINTS NO WINDOW CHROME: the tablet's full-screen window has
// none, and the program starts at the surface's first row with the app's
// own TITLE BAR (the block below) — no buttons, no frame.

// THE TITLE BAR — THE WINDOW'S FIRST LANE, XFWM'S BAR ON ARC'S NUMBERS
// (architect 2026-11-18 / 2026-11-19): "We're sort of modeling it on XFWM, but we
// shouldn't do it 1-to-1"; "the metrics don't really change. Like for the
// XFWM title bar ... the size of that doesn't change. Only the font size
// did". THE BAR IS kTitleBarW, 28 W through scaled_px (56 device px on the
// tablet at 200 %), whatever the key's height (title_bar_h_px), the top of
// the top strip's lane 0, the top icon row under it with no line between
// (a tone change only). IT ALONE WEARS THE TITLE TONE `ground` (2F343F,
// its line of kArcDarkTones): "the darkest color
// would only apply to the top line, the title bar" — everything under it is
// the body tone `body_face`. INERT CHROME WHOLE: no hit, no tooltip, no
// gesture, the lamps included (THE TWO LAMPS below; architect 2026-11-24).
// EVERY TEXT ON IT IS THE BOLD 12 W (GuiFace::Bold, gui_font.h) IN
// `title_text` (xfwm's title text; arc_dark AFB8C5), ON ONE BASELINE,
// kTitleBaselineW = 18 W under the bar's top (36 device rows on the tablet;
// xfwm's measured seat on his 9 pt shot: the title's capitals on +9 .. +17
// of the un-maximized 28-row bar, so the baseline +18), every drawing on it
// in the same tone; THE CAP BAND the alphabet's painted ink rows above that
// baseline (gui_font_ink_band of kLogoLegendInkSet in the Bold; on the
// tablet 17 rows, rows 19 .. 35 over the baseline at 36) — what the battery
// and the lamps stand on. No shadow (the flat look). paint_title_bar paints it; its right
// group's layout is logo_legend_layout (paint_handler.cpp), the painter's
// and the tick's damage's one road.
//   ON THE LEFT, THE NOTE: MusicNote16th (the BPM key's
//     drawing) IN A 12 W DRAWING BOX (architect 2026-11-19: "the logo should
//     take the same font size as the body" — a 16 W box sized for 16 W
//     text, 16 · 12 / 16 = 12 W), at the box's exact zoom (icons.h's THE
//     BOX, architect 2026-11-26; 24 / 16 = 1.5 on the tablet: Papirus's
//     note inks 8 x 14 of its 16 units, so its ink 12 x 21 device px), ITS
//     INK'S LEFT EDGE kTitleNoteInsetW = 8 W inside the panel (16 device px;
//     architect 2026-11-19: "the note in the logo looks
//     good now. It looks like it's aligned with the buttons"), ITS INK
//     CENTERED ON THE BAR'S ROWS EXACTLY (the note inks symmetrically in
//     its canvas, so this is the box centered, its top +8 W; on the
//     tablet the ink 17.5 .. 38.5). THEN THE BRAND, "Warptempo" (kLogoBrand,
//     a constant), ITS PEN kTitleNoteGapW = 6 W AFTER THE NOTE'S INK'S RIGHT
//     EDGE (architect 2026-11-19: "Maybe one more W spacing between
//     the note and the warptempo font" — one more than the picture's 5),
//     both off the drawing's real ink box, not its 12 W box (on the tablet
//     the ink 16 .. 28 with Papirus's note, the pen 28 + 12 = 40).
//   ON THE RIGHT, THE GROUP "[limiter][clip] 92% <battery>  |  02:12 PM" AS
//     ONE TEXT RUN in the Bold 12 W (architect 2026-11-19: the pipe "could
//     just become a text pipe, since we're going to use those text pipes
//     in the status bar"; gui_battery.h's composer for the
//     parts; the tick's refresh_logo_legend, main.cpp), LAID RIGHT TO LEFT
//     on the baseline: the run "  |  02:12 PM" — the status bar's own field
//     separator (two spaces each side of the pipe, kStatusFieldSeparator,
//     paint_handler.cpp) then the clock — with its LAST INK
//     kTitleRightInsetW = 8 W inside the panel's right edge (the note's
//     inset mirrored); THE BATTERY (below), its cell's right edge on that
//     run's pen; the run "92% " — the percent and a space — its advance
//     ending on the cell's left edge, so "100%" grows leftward, away from
//     the clock; kLogoLampGapW (4 W) of ground; THE CLIP LAMP; one W (the
//     key gap); THE LIMITER LAMP. The insets and the lamps' gap are
//     measured from the runs' REAL ink edges (text_shape::ink_left_px /
//     ink_right_px), the battery between the runs' advances, nothing
//     floored, so the battery and the lamps stand at real x (the no-grid
//     rule).
//   THE TWO LAMPS (architect 2026-11-05; the size, the rows and the gap
//     2026-11-06): two kLogoLampW = 8 W squares
//     (16 x 16 device px), "LIMITER LEFT, CLIP RIGHT, mirroring the level
//     sliders", THEIR TOP ON THE CAP BAND'S TOP. THEIR FACES: AT REST
//     `key_face`; LIT, THE LIMITER `limiter_lamp` ("a warning"; its own
//     tone) and THE CLIP
//     `clip_lamp` ("a fault"); a key's lit look REFUSED ("clip indicator is
//     not a button"). No outline, nothing printed on them. Both squares are
//     part of logo_legend_layout and of its extent (the limiter the
//     leftmost ink), so refresh_logo_legend's damage covers them whenever
//     the percent moves.
//     THE TRIGGERS AND THE HOLD (architect 2026-11-05: "a hold on both
//     lamps" — because the tick is the platform's 5 ms timer
//     (GuiPlatform::detect_refresh_rate_ms) while
//     only the PRESENT waits for the vsync, one beat apart, 22.2 ms at the
//     pinned 90 Hz (kFrameBeat, platform_android.cpp), so a
//     lamp lit for one 5 ms tick can be cleared before any present and
//     never be painted; the clip clamps a RUN of blocks and its lamp spans
//     ticks, but the limiter's engaged frames are POINTS, and its one-tick
//     lamp was missed on his glass): EACH LAMP IS LIT FROM ITS LAST EVENT FOR
//     AT LEAST THE HOLD — kLampHoldMs, 85 ms, beside refresh_logo_lamps
//     (main.cpp) — a tick that sees the lamp's event stamping now + the hold
//     (AppState::clip_lamp_lit_until_ms, limiter_lamp_lit_until_ms), the
//     lamp lit on that tick and on every tick before the stamp: an event
//     re-arms it (a run of clamping blocks keeps the lamp lit until the hold
//     after its last block), the dark edge lands on the first tick at or
//     past the stamp, and 85 ms is about three and four fifths presents at
//     the beat. NO TIMER OF ITS OWN: the tick is the clock. The bits on AppState
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
//     (architect 2026-11-05: an item's own .fingerprint carries its limiter
//     line, render_cache.h; the source view and the score's soundtrack
//     dark; a render whose record is missing lights nothing until it is
//     rendered again): with the engine playing and a
//     buffer bound that carries a list, the event is the
//     limiter's engaged frames (AppState::bound_limiter_frames, the bind's
//     own snapshot) holding a frame in the span the playhead advanced since
//     the tick before, in buffer-local frames — [prev, now), a binary
//     search; a span that runs backward (a wrap, a scrub, a restart) tests
//     nothing, the first tick of a play seeds and tests nothing, not playing
//     tests nothing (a lamp lit at the stop keeps its hold out).
//     NO STRIKE AND NO TAP (architect 2026-11-24: the clip lamp "should
//     lose the gray fill and the tap"): the square is lit or at rest and
//     is no press target in any state, so the title bar is inert whole. A
//     dead device stream is woken by NOISE's tap or lift instead
//     (finish_level_press_release, input_pointer.cpp; the level case's
//     entry, app_state.h), or by any launch press through its gate.
//   THE BATTERY, ONE CELL WHATEVER IT HOLDS — the clock never moves (his:
//     "the glyph always there taking up space so that the clock doesn't
//     move"): Battery000Charging (any power source
//     attached, and a host with no battery; GuiLogoGlyph's rule) or
//     Battery000 (discharging, or the plug state unknown), SIZED BY ITS INK
//     (icons.h's THE BOX): ITS INK'S HEIGHT THE CAP BAND'S, the zoom the
//     band's rows over the ink's height in the drawing's units, unrounded
//     (logo_glyph_box_px, paint_handler.cpp; Papirus's batteries ink 11 x 15
//     and 10 x 14 user units, architect 2026-11-17; architect 2026-11-26)
//     — ITS TOP ON THE CAP BAND'S TOP; THE CELL the larger of the two
//     drawings' inks on each axis, real px, each drawing's ink centered in
//     it exactly. On the tablet the band 17 rows (rows 19 .. 35, the
//     baseline the edge under row 35), the zooms 17 / 15 and 17 / 14, the
//     cell 12.47 x 17 device px (Battery000's 11 · 17 / 15 wide).
inline constexpr double kTitleBarW           = 28.0;
inline constexpr double kTitleBaselineW      = 18.0;
inline constexpr double kTitleNoteBoxW       = 12.0;  // the note's drawing box (the body's size)
inline constexpr double kTitleNoteInsetW     = 8.0;   // the panel's left edge to the note's ink
inline constexpr double kTitleNoteGapW       = 6.0;   // the note's ink's right edge to the brand's pen
inline constexpr double kTitleRightInsetW    = 8.0;   // the right group's last ink to the panel's right edge
inline constexpr double kLogoLampW           = 8.0;   // each lamp's square
inline constexpr double kLogoLampGapW        = 4.0;   // the clip lamp to the percent's first ink
inline constexpr const char* kLogoBrand = "Warptempo";
inline constexpr const char* kLogoLegendInkSet = "0123456789%: AMP";
inline int title_bar_h_px() {
    return scaled_px(kTitleBarW, 1);
}

// THE UNIT IS ONE W (its one statement at scaled_px above; the rebase to
// 200 %, architect 2026-11-05): every `k…W` constant a whole number of W,
// every length through scaled_px, rounded at the element (a face's em is
// its source's exact number and no length, gui_font.h's head, architect
// 2026-11-26); on the tablet one W is 2 device px, so the keys
// keep their pitch (72 device px) and the icons their seats.
// THE LINE RULE (architect 2026-11-04, the unit): THE LINE IS ONE W,
// ROUNDED AT THE ELEMENT, FLOORED AT ONE DEVICE PX — 2 on the tablet at
// 200 %, 1 at 100 % — the chokepoint beside scaled_px for every LINE the
// skin draws:
// THE WELL'S LINES (the line under the top icon row, the lines under the
// trough, the ruler and the marker lane, the stereo divider and the well's
// foot; THE WELL'S EDGES below), the status bar's line, the modal box's
// two line rows and the on-screen keyboard's top line and its grid's lines
// (modal_box.h, onscreen_keyboard.h), the
// focused entry's accent line, the flag editor's border, the scrub's knob's
// rim, the line editors' and the flag editor's carets (paint_handler.cpp's
// paint_value_page, render.cpp's flag editor and paint_scale) — AND
// EVERY LINE ON THE CANVAS, waveform_line_px below being this one (the
// grid, the center lines, the stems, the cursor, the scanner, the anchor,
// the lead-in ring, the ruler's ticks; the lit
// core an area of the plate's, render.h's THE CORE). The
// ground between two keys is its own length of the same number
// (key_gap_px, the case block below). Everything else keeps scaled_px,
// AREAS: the keys, the lanes' heights, the pads and gaps, the
// flags' triangles and the cue boxes. ARTWORK IS AN AREA IN THIS SENSE
// (2026-10-23): a drawing's stroke weight is part of the drawing, so it
// scales with the drawing — the icons and the faces' strokes, never a line.
// A LINE PLACED BY TIME OR ON A BAND'S EXACT ROW IS CENTERED ON ITS EXACT
// PLACE at each pixel's exact coverage (architect 2026-11-14): a
// vertical one on its frame, the pixel boundary (architect 2026-11-06), a
// horizontal one on its row — the rule's one statement at waveform_line_px
// below, its paint waveform_line_span and canvas_hline_span.
inline int hairline_px() { return scaled_px(1.0, 1); }

// WHAT STANDS OVER THE PROGRAM (architect 2026-10-14 / 2026-11-19): every
// overlay — the render
// player, the picker's contents, the score viewer — is the modal family's
// box and controls row (modal_box.h), the box (the view between two lines)
// from the trim lane's first row to the well's foot and the controls row on
// the bottom icon row's rows; a question and the progress modal are full
// pages in that box, their answers on its controls row (architect
// 2026-11-20; paint_handler.cpp's THE QUESTION PAGE AND THE PROGRESS
// PAGE), and a line editor is a sub-page of its own in the box's view
// (paint_handler.cpp's THE VALUE SUB-PAGE); the on-screen keyboard stands
// on the window's last rows across the lanes, over the status bar, the
// bottom icon row and the view's foot, nothing relaid (onscreen_keyboard.h's
// placement block; architect 2026-11-20).

// THE TOP ICON ROW AND ITS CASE — the top strip's lane 0, under the title
// bar (program_spec.h's head, where every length below is authored and its
// source named). The roster's view/mode/action buttons stand in it — the
// kIconRowButtons, kIconRowHistoryOpener, kIconRowViewPairs, kIconRowTabPair and history
// stand-in tables are the count's one authority (paint_handler.cpp); THE
// CASE IS ALSO THE BOTTOM ICON ROW'S AND THE RENDER PLAYER'S: every reader
// takes the accessors below, so the three rows cannot drift apart.
//
// THE KEY IS ARC'S ICON BUTTON, ITS BOX BORDER INCLUDED (architect
// 2026-11-18 / 2026-11-19; materials.h's THE KEY owns its look): kKeyWidthW
// WIDE by kKeyHeightW TALL (their seat above — Arc's icon button, 36 x 28
// W, architect 2026-11-18: "about 72 device pixel width ... the border is
// clickable"), both through scaled_px, rounded at the element: 72 x 56
// device px on the tablet at 200 %. THE BOX IS THE WHOLE KEY, its one-W ring
// included, and THE KEYS OF A GROUP ARE LINKED, NEIGHBORS SHARING ONE SEAM
// COLUMN (materials.h's key_link_pitch_px and linked_group_w_px: the pitch
// 70 device px on the tablet, a group of n 72 + 70 (n − 1)); the box is the
// key's hit box (a seam its left key's) and its drawing's seat.
// Every key carries its set's drawing, the 16 W canvas centered on its box
// (paint_key_glyph, paint_handler.cpp; icons.h's THE KEYS), one size for
// every key. No printed word under it (architect 2026-11-18); the tooltip
// names the act.
//
// THE TITLE BAR AND THE TOP ICON ROW (architect 2026-11-18 / 2026-11-19):
// THE TITLE BAR (title_bar_h_px, its block above), then ARC'S TOOLBAR, ONE
// PAD ON BOTH SIDES OF THE FACES — kToolbarPadW, 7 W above them and 7 W below (architect 2026-11-20:
// symmetric on his word, the bottom icon row the same; ARC'S TOOLBAR PADS
// below). With the key 28 W tall: 28 + 7 + 28 + 7 = 70 W, 140 device rows on the
// tablet at 200 % (the title bar 0 .. 55, the pad 56 .. 69, the faces 70 ..
// 125, the pad 126 .. 139), on the body tone, no line between the title
// bar and the row (a tone change only), the trim lane's first row the line
// under it. (The on-screen keyboard's band pads its grid by the run gap
// instead, onscreen_keyboard.h's pad_top_px / pad_foot_px.) Its runs are
// paint_icon_row's
// walk (icon_row_seats: each run one linked group, the run gap of ground
// between two groups, program_run_gap_px). The hit target is the painted
// box (point_on_key, materials.h); the ground round the groups, between
// them and in the pads, is inert for input.
//
// THESE NUMBERS LIVE HERE rather than beside the row's walk because the
// case is not one row's: paint_handler.cpp's three rows read it. THE CARD
// AND THE FOLDER OVERLAY'S LIST ROWS ARE NOT THE CASE: a card's glyph
// stands in its own 16 W box at its own pad (notifications.h's notifyd
// lengths, 2026-11-19), and a list row's at the painter's own constants
// (paint_handler.cpp's THE LIST VIEW).
// THE FACE'S WIDTH, kKeyWidthW, and ITS HEIGHT, kKeyHeightW (their seat
// above; the case paragraph): scaled_px, floored at three device px so a key never shrinks
// to a sliver at a tiny scale. icon_case_w_px is THE ONE READER of the
// width: every key surface's walk, seat and hit box takes it (or
// key_link_pitch_px / linked_group_w_px, materials.h, built on it).
inline int icon_case_w_px() {
    return scaled_px(kKeyWidthW, 3);
}
inline int icon_case_h_px() {
    return scaled_px(kKeyHeightW, 3);
}
// THE ONE W BETWEEN TWO FACES (architect 2026-11-04): kKeyGapW,
// ONE W through scaled_px — 2 device px on the tablet at 200 %, 1 at 100 %
// — FLOORED AT ONE DEVICE PX so two faces stand apart at every scale: the
// on-screen keyboard's (onscreen_keyboard.h's key_gap_px, its grid's shared
// one-W lines) and the title bar's two lamps' (logo_legend_layout). The top
// icon row's, the bottom icon row's and the controls rows' keys share a
// seam instead, linked groups (materials.h's key_link_pitch_px).
inline constexpr double kKeyGapW = 1.0;
inline int key_gap_px() {
    return scaled_px(kKeyGapW, 1);
}
// ARC'S TOOLBAR PADS (architect 2026-11-18 / 2026-11-19; THE TITLE BAR AND
// THE TOP ICON ROW paragraph above): kToolbarPadW, 7 W, ON BOTH SIDES OF THE
// FACES IN BOTH ICON ROWS — above and below the top row's, above and below
// the bottom row's (architect 2026-11-20: "we dont' want to be
// injecitng an asymmetry where there doesnt have to be one"; Arc's toolbar
// as Thunar and Mousepad draw it stands 8 W on one side and 7 W on the
// other, a 1-px quirk given up for the symmetric 7 / 7). Through scaled_px,
// floored at one device px: 14 device px on the tablet at 200 %. THE
// ON-SCREEN KEYBOARD, standing over the bottom icon row and the status bar
// on the window's last rows, pads its grid by the run gap, not by this
// (onscreen_keyboard.h's pad_top_px / pad_foot_px, architect 2026-11-26).
// His reason for room
// under the bottom row: "my hand isn't naturally at the very edge of the
// screen" — the status bar stands under it (status_bar_lane_h_px).
inline constexpr double kToolbarPadW = 7.0;
inline int toolbar_pad_px() {
    return scaled_px(kToolbarPadW, 1);
}
// THE CASE'S TOP, AS AN OFFSET FROM THE BOTTOM ICON ROW'S TOP and the modal
// controls row's: the pad (7 W), the one expression every reader of those
// strips' case seat takes (the bottom row's and the controls row's seats,
// hit rects, tooltips).
inline int icon_case_top_offset_px() {
    return toolbar_pad_px();
}
// THE TOP ICON ROW'S CASE TOP, AS AN OFFSET FROM THE TITLE BAR'S TOP: the
// title bar and the pad (28 + 7 = 35 W, 70 device rows on the tablet), read
// by the top icon row's seats alone (icon_row_seats, paint_handler.cpp,
// which every reader of the row takes).
inline int band_case_top_offset_px() {
    return title_bar_h_px() + toolbar_pad_px();
}
// THE STRIP — the bottom icon row's: the pad, the case, the pad (7 + 28 + 7
// = 42 W, 84 device rows on the tablet).
inline int strip_h_px() {
    return toolbar_pad_px() + icon_case_h_px() + toolbar_pad_px();
}
// THE TITLE BAR AND THE TOP ICON ROW, one lane: the title bar, the pad,
// the case, the pad (28 + 7 + 28 + 7 = 70 W, 140 device rows on the
// tablet).
inline int icon_row_h_px() {
    return title_bar_h_px() + toolbar_pad_px() + icon_case_h_px() +
           toolbar_pad_px();
}
// THE GROUND BETWEEN TWO RUNS (Arc's toolbar groups 6 W apart, architect
// 2026-11-22): kRunGapW (its seat above) through scaled_px — 12 device px
// on the tablet at 200 % — between two runs' FACES (the key is its face):
// the top icon row's runs and
// the one run gap the left runs' clip stands short of the history opener,
// the bottom icon row's runs packed right, and the controls rows' groups,
// the render player's and the score viewer's group spaces included
// (paint_handler.cpp's icon_row_seats, row8_lengths, player_row_layout and
// the score rows) — and the on-screen keyboard's two pads, above and below
// its grid (onscreen_keyboard.h's pad_top_px / pad_foot_px; architect
// 2026-11-26).
// program_run_gap_px is THE ONE READER of kRunGapW: one
// gap for every gap.
// A TOO-LARGE GAP is the top icon row's overflow rule's, as a too-wide key
// is (THE FIT at paint_icon_row's width math, paint_handler.cpp) — a
// left-run member whose box would end past the limit published empty, no
// key cut, NOISE first, then the third run's keys from its tail, the right
// run whole; the bottom
// icon row's block grows leftward over the timestamp, and the player's and
// the score viewer's rows take the gap from the scrub's trough, which
// their floor of two knobs drops whole when too little is left.
inline int program_run_gap_px() {
    return scaled_px(kRunGapW);
}

// A BUTTON IS ARC'S BUTTON, ITS OUTER CORNERS ROUNDED (architect 2026-11-18
// / 2026-11-19): every key of both icon rows and of the controls rows
// a box in a one-W `well_border` ring whose outer corners are 3 W
// (materials.h's kKeyCornerW, the faces' inside it 2 W), a group of keys one
// LINKED GROUP — one ring, the keys sharing one seam column each — and every
// word button and entry its own ring (materials.h's paint_linked_group,
// paint_word_button, paint_entry; their exact-coverage road the coverage
// stamp's rounded shapes, cool_edit_paint.h). The scrub is Arc's scale (the
// block below) and the on-screen keyboard Arc's buttons in one grid
// (onscreen_keyboard.h).

// -- THE PLAY-SCRUB IS ARC'S PLAIN SCALE (architect 2026-11-18 / 19: his
//    "I'm hoping we're gonna have a proper arc thumb, right? ... it touches
//    ... When I'm dragging it, it's dark with no outline") -----------------
//
// The render player's controls row and the score viewer's (its media face
// and its page road's face alike) carry ONE SCRUB, GTK's `scale` under Arc,
// one widget:
//   THE TROUGH — kScaleTroughW, 5 W thick (10 device px on the tablet at
//     200 %), a full pill (radius half its thickness), `scale_trough` (2D303B,
//     its line of kArcDarkTones; the progress page's bar's too),
//     CENTERED EXACTLY ON THE KEYS' ROWS (the tablet, the key 28 W tall: keys
//     1324..1379, the trough rows 1347..1356), across the item's columns.
//     THE PLAYED PART — Arc's `scale highlight` — from the trough's left end
//     to the knob's center, THE ACCENT (`cue`, the progress bar's done
//     part's tone), the same pill cut at the knob's center: the scrub shows
//     how much has played.
//   THE KNOB — kScaleKnobW, a 17 W DISC (34 device px), centered on the
//     trough's center row (the tablet: rows 1335..1368), its fill
//     `key_face` in a ONE-W RIM of `scale_slider_border` (Arc's
//     slider border 262933, its line of kArcDarkTones); HELD — the owner's ScrubDrag
//     armed ON THE KNOB (RenderPlayer::ScrubDrag::on_thumb, the score
//     viewer's the same; a press on the trough beside it carries the knob
//     at rest) — THE WHOLE DISC THE ACCENT, no rim (Arc's slider:active);
//     UNDER THE HOVERING PEN, at rest, ITS FILL `key_hover_face` inside the
//     unchanged rim (Arc's scale slider:hover, 505666, measured on his
//     desktop; architect 2026-11-18 / 2026-11-20); EVERY CHANGE
//     AMONG THE THREE — rest, hover, held — FADES OVER ARC'S 200 ms, the
//     fill and the rim (gtk3 CSS lines 1144 .. 1150: `transition: all 200ms
//     ...; transition-property: background, border`, no 50 ms line;
//     architect 2026-11-20) — the gate and the frames AppState::FaceFades's
//     (app_state.h), the knob's picture handed to paint_scale as a FaceMix,
//     the knob's hit the press's own
//     (scale_knob_hit, on the disc as painted), its damage the disc's hull
//     at the published center. No glyph; its one lit state is the held
//     look (FaceLook::Lit, the accent disc). AT ITS POSITION'S EXACT PLACE (the
//     no-grid rule, hull above): its center the real x of its frame or page
//     (the owners' mappings, render_player_scrub_x_of, score_scrub_x_of /
//     _of_frame), painted at exact coverage through the coverage stamp's
//     rounded shapes (paint_scale).
//   THE KNOB STAYS INSIDE THE TROUGH, GTK'S WAY: its center travels from
//     the trough's left end plus half the knob (8.5 W) to its right end less
//     half the knob — frame 0 / the first page at the first, the last frame
//     / page at the second (scale_travel_x0 / scale_travel_span below; the
//     mappings, app_state.h and score_viewer.h, and their inverses in the
//     press and drag roads read them), a place outside the travel clamping
//     to its end. THE PUBLISHED ITEM (AppState::ModalDialogGeometry::scrub,
//     scale_item_rect) is the trough's columns by the knob's rows, nothing
//     overhanging: the press router's outer gate, the tick's per-position
//     damage, and what every place of the knob, the trough and the played
//     part lie inside.
//   THE HIT (on screen is as painted): a press on THE KNOB'S DISC takes the
//     knob where it stands (scale_knob_hit, the disc itself, tested with the
//     pointer's real place: a press in the disc's bounding square but off
//     the disc lands on the trough or the pane beside it, so it is the
//     trough's); anywhere else on the item — the trough's columns by the
//     knob's rows, the rows above and below the trough included — the knob
//     JUMPS to the press and the same press carries it (claim_player_scrub_
//     press, the score viewer's its own claim).
//   THE DAMAGE: the grab and the lift damage the item; A MOVING KNOB OWES
//     ITS OLD AND NEW DISCS AND THE TROUGH BETWEEN (the played part changes
//     there), scale_knob_move_rect, the drag's motion — THE OLD DISC THE
//     PUBLISHED CENTER's (ModalDialogGeometry::scrub_thumb_x, the knob the
//     screen last showed), not the drag's previous carry: two motions
//     between presents each owe the move from what is on screen, so the
//     later damage alone covers what the painter owes from its published
//     center and the painter publishes the new one (paint_scrub_item's
//     publish rule; input_pointer.cpp's scrub_knob_motion_damage); the tick's
//     per-position damage while the transport plays stays the item whole
//     (the tick reads the cursor before the paint reads it again, so a span
//     from the tick's place could miss the painted one).
// THE KNOB ALWAYS STANDS (architect 2026-10-25, "agreed"): whenever the
// item has a width, at frame 0 while no item is loaded — the clock's own
// zeros — so the player opens with its knob at the travel's start rather
// than a bare trough that grew a knob at the first Play. A press on it or
// on the trough with no item is consumed and seeks nothing
// (claim_player_scrub_press's frames <= 0 arm).

// THE WELL'S LANES — the TRIM lane, the RULER lane, the MARKER lane and THE
// LINE UNDER IT, stacked in that order directly under the top icon row
// (main.cpp's lane table), the last one's bottom edge the waveform's top with
// no gap (the palette block's well paragraph owns the look; every length is
// derived here at the element: scaled_px for a chrome length authored in
// W, hairline_px for a line — one W, rounded at the element (architect
// 2026-11-04, the unit) — the faces' own metrics for the text's bands).
// The device rows below are the tablet's, 200 %:
//   THE TRIM   the line under the top icon row, THE TROUGH, the line under
//              it (trim_lane_h_px): 14 W, 28 device rows (2 + 24 + 2).
//   THE RULER  the ruler (ruler_h_px: kRulerW, the digits' line box
//              centered in it; the ticks overlay its foot) and its line
//              (ruler_lane_h_px): 16 W + 1 W, 32 + 2 device rows.
//   THE MARKER 16 W, the editor's box on its whole rows (marker_lane_h_px:
//              the one-W border, the label's 14 W line box on the
//              line-box baseline, the border): 32 device rows; the cue's
//              chip its last rows (cue_chip_h_px, 8).
//   THE LINE   the line between the lane and the canvas, one hairline
//              (lane_foot_line_h_px, below): 2 device rows, 1 W.
// Every one-W line of the well is `well_border` (THE WELL'S EDGES, below;
// architect 2026-11-17), across the panel's whole width, the
// lanes' own fills on the same columns (edge to edge, 2026-11-19).
//
// THE TRIM BAR IS ARC'S SCROLLBAR (architect 2026-11-17: "The scroll bar
// and the trough should be accurate to whatever Arc uses"; "Let's use the
// real 12W" — Thunar measured, trough 12,
// slider 6, 3 px of trough each side): THE TROUGH 12 W, kTrimTroughW (24
// device rows on the tablet at 200 %, 12 at 100 %, floor 1), and on it THE
// PILL 6 W tall, kTrimPillW, centered (the seat of trough above and below,
// below), its
// ends' radius half its height — a full pill (render_trim_flags). THE
// SLIDER'S BOX AND ITS CORE (architect 2026-11-18, his "let's just make it
// as realistic as we can" — Thunar measured, 3 px of trough between a
// slider's end and the trough's end, the same 3 as along its sides; Arc's
// `scrollbar slider`, a 4-px transparent border less a 1-px margin): THE
// BOUNDS' PLACES ARE THE SLIDER'S BOX — their exact places, unclamped
// (TrimBoundColumn::x_raw; the EOF-wall clamp is the grabs' alone) — and
// the pill is its visible core,
// ONE INSET INSIDE THE BOX ON EVERY SIDE — above and below, and at each
// end inside its bound's own place. THE INSET IS ONE NUMBER, THE SEAT — its
// one statement, every other mention of it naming it so — the pill's seat
// across the trough as painted, (trough rows − pill rows) / 2, half
// the difference of the two painted heights, a real length with no
// rounding of its own: 3 W wherever both heights are whole numbers of
// device rows at the scale (6 device px on the tablet at 200 %, 4.5 at
// 150 %, 3 at 100 %), and off it by the rounding where one is not (125 %:
// the trough 15 rows, the pill's 7.5 rounded to 8, the seat 3.5 px, 2.8
// W). THE CORE STANDS AT ITS REAL PLACE,
// round at both of its own ends, and the strip's edge cuts it (the no-grid
// rule, `hull`'s block: what is painted stands where its arithmetic puts
// it, so nothing jumps as a bound crosses the edge): a bound on screen
// shows its round end the seat inside it; a bound past the edge by more than
// the inset and the radius leaves the core cut square and flush at the
// edge ("it goes on"); a bound in that band past it shows the core's round
// end where it is, cut by the edge as it leaves. The short span's rule
// and the cut: render_trim_flags' pill. THE CAPS on the pill's ends and
// the grabs around them: THE TRIM BAR'S CAPS AND GRABS below. THE TRIM
// LANE: the line under the top icon row, the trough and the line under it
// (the line Arc's scrollbar draws in $borders_color on its content side):
// 14 W, 28 device rows, rows 140 .. 167 on the tablet, the
// trough 142 .. 165. The modal family's box keeps its seat on the lane's
// first row (modal_box.h); the cards stand at the window's top right over
// whatever is there, notifyd's seat (notification_stack_bound,
// 2026-11-19).
inline constexpr double kTrimTroughW = 12.0;
inline constexpr double kTrimPillW   = 6.0;
inline int trim_trough_h_px() {
    return scaled_px(kTrimTroughW, 1);
}
inline int trim_pill_h_px() {
    return scaled_px(kTrimPillW, 1);
}
inline int trim_lane_h_px() {
    return hairline_px() + trim_trough_h_px() + hairline_px();
}
// THE TROUGH'S RECT, THE ONE STATEMENT OF ITS ROWS AND COLUMNS: under the
// trim lane's first row (the line under the top icon row) its
// trim_trough_h_px rows, cut to the lane's rows less that line, on the
// columns [lane_x, lane_x + lane_w) — the waveform area's x and the lane's
// painted width. Read by paint_trim's ground step, which lays the trough's
// FLAT `trim_bar_face` on every frame class (architect 2026-11-21), and by
// render_trim_flags for the pill's seat on it; empty (w or h
// not positive) on a lane too short or too narrow to hold one.
inline GuiRect trim_trough_rect(const GuiRect& trim_lane, int lane_x,
                                int lane_w) {
    const int px = hairline_px();
    return GuiRect{lane_x, trim_lane.y + px, lane_w,
                   std::min(trim_trough_h_px(), trim_lane.h - px)};
}
// THE RULER'S TICKS (architect 2026-11-01; the major 2026-11-03; the width
// one W, the unit, 2026-11-04): ONE CANVAS LINE
// WIDE — waveform_line_px, the line rule's W, 2 device px on the tablet at
// 200 %, 1 at 100 % — CENTERED ON THE TICK'S OWN EXACT TIME'S PLACE
// (architect 2026-11-14; waveform_line_span's rule, RulerComb's
// tick_frames), THE MAJOR 2 W (architect 2026-11-19: "You can make the major 2W. Or you could just have it sit underneath the
// text") and the minor 1 W tall, 4 and 2 device rows on the tablet
// (scaled_px, each floored at one row). THEY OVERLAY THE RULER'S FOOT AND ADD
// NO HEIGHT (architect 2026-11-03: "the ticks can be adjusted later — they
// don't change height of anything else"): in the 16 W ruler the digits'
// line box ends 14 W down (its top 1 W, its 13 W), so the major's 2 W are
// the two W under it, THREE W OF RULER CLEAR BETWEEN ITS TOP AND THE INK'S
// FOOT on the baseline at +11 W (six device rows at 200 %; the minor 1 W;
// never over the ink).
inline constexpr double kRulerMajorTickRowsW = 2.0;
inline constexpr double kRulerMinorTickRowsW = 1.0;
inline int ruler_major_tick_h_px() {
    return scaled_px(kRulerMajorTickRowsW, 1);
}
inline int ruler_minor_tick_h_px() {
    return scaled_px(kRulerMinorTickRowsW, 1);
}
// THE LINE-BOX RULE, GTK'S SEAT (architect 2026-11-19, what the approved
// pictures show): in a box `box_w` W tall standing at
// `box_y`, THE FACE'S LINE BOX (gui_font_line_box_w: its ascent and descent
// at the size in W, each rounded up — 11 + 3 = 14 W at 12 W, 10 + 3 = 13 W
// at the ruler's 10 W Bold) IS CENTERED AS PANGO CENTERS IT, its top
// floor((box − line) / 2) W down, and THE BASELINE IS THE ASCENT UNDER
// THAT — one whole-W offset through scaled_px, so on the tablet a whole
// device row: +11 W in the 16 W ruler, +12 W in the 16 W marker lane, +15 W
// in the 22 W status bar. Its readers: the ruler's digits
// (ruler_baseline_px), the cues' labels and the lane's editors
// (cue_baseline_px) and the status bar (paint_status_bar). THE TITLE BAR'S
// BASELINE IS A STATED +18 W of its 28 (xfwm's measured seat, kTitleBaselineW).
double line_box_baseline(const GuiFont& font, double box_y, int box_w);
// THE RULER, kRulerW = 16 W, AN AUTHORED HEIGHT (architect 2026-11-19: "the
// markers lane looks like, if this was being designed by whoever designed
// Arc, it would be smaller in height ... a tighter lane ... And perhaps for
// the ruler as well ... Maybe the same height, then they both should shrink
// in sync" — the ruler and the marker lane 16 W each): 16 W through
// scaled_px, 32 device rows on the tablet at 200 %. Its digits are the
// Bold 10 W (Small, gui_font.h; the line box 10 + 3 W, gui_font_line_box_w)
// — the header's weight. THE BASELINE by the line-box rule
// (line_box_baseline): the 13 W box centered in 16, its top floor(3 / 2) =
// 1 W down, the baseline the ascent under it, +11 W (22 device rows) under
// the ruler's top. The ticks overlay its foot (the majors' 2 W under the
// line box's foot, which they meet; the digits have no descender ink
// there), the playhead's head its last rows.
inline constexpr double kRulerW = 16.0;
inline int ruler_baseline_px() {
    return static_cast<int>(line_box_baseline(gui_font(GuiFace::Small), 0.0,
                                              static_cast<int>(kRulerW)));
}
inline int ruler_h_px() {
    return scaled_px(kRulerW, 1);
}
inline int ruler_lane_h_px() {
    return ruler_h_px() + hairline_px();
}
// THE CUE'S CHIP'S LENGTHS (architect 2026-11-14: the chip and the ruler's
// head A SMOOTH-EDGED TRIANGLE AT EVERY POSITION, its apex exactly on the
// mark's exact x — "I think the chips should also go anti-aliased", "Agree
// on both."; the drawing and its rule at cool_edit_paint.h's THE CUE'S
// CHIP). THE CHIP IS THE BARE TRIANGLE OF THE NOMINAL ROWS, NO OUTLINE
// (architect 2026-11-19: "I think I prefer the smaller version without the
// outline"; no outline because "the scroll bar has no outline"): the fill's
// own triangle.
//
// THE TRIANGLE'S NOMINAL ROWS: 4 W through scaled_px (8
// device px on the tablet at 200 %), rounded at the element like every
// length off the canvas (architect 2026-10-26 ~13:40, on the row count as a
// per-device number: "why density? why not just simple scale? tablet is
// only real surface. later tablets may have higher dpi, but not lower" —
// "yes" to the planner's proposal). 4 W (architect 2026-11-12: "shorter
// triangles, yes, and then move labels one W left, yes"; "it stays touching
// the border ... they don't rise up as far"). 8 at
// 200 %, 4 at 100 %, 2 at the scale's floor of 50 % (is_gui_scale_percent,
// device_config.h) — no floor, no clamp. READERS: the chip's height
// (cue_chip_h_exact_px below) and the label's lead (cue_label_lead_px, the
// rows' reach).
inline constexpr double kCueTriangleRowsW = 4.0;
inline int cue_triangle_rows() {
    return scaled_px(kCueTriangleRowsW);
}
// THE CHIP'S HEIGHT H, the triangle's height and half-base in device px —
// and the ruler's head's: THE NOMINAL ROWS ALONE (architect 2026-11-19,
// above), cue_chip_h_exact_px, 8 on the tablet at 200 %, 4 at 100 %; its
// sides at 45°. THE CHIP'S APEX STANDS ON THE BOTTOM EDGE OF THE LANE'S LAST
// ROW, THE CHIP SITTING ON THE LANE'S FOOT — at 200 %, lane rows
// 202 .. 233, its apex on the edge under row 233, its top edge 8 above it,
// the chip's rows 226 .. 233. THE RULER'S HEAD IS THE SAME CHIP (architect
// 2026-11-04, "4 accept overlap"), its apex on the bottom edge of the ruler's last row (200 %:
// ruler rows 168 .. 199, the head's rows 192 .. 199), so its top can reach
// into the ruler's digits where it stands under a label — ACCEPTED
// ("timestamp not really important for fine tune, just ballpark"): it
// paints over them as a chip paints over a label (paint_ruler_row).
// THE WHOLE TRIANGLE IS THE FILL: a cue's `cue_fill` or `inactive_cue`, the
// head's `playhead_head`, one triangle of height H (CoverageStampPass::chip).
// ONE SIZE, AND ITS WHOLE HULL DERIVED HERE: cue_chip_h_px is ceil(H), the
// rows and half-columns that hold the chip — 8 on the tablet, 4 at 100 % —
// what every WHOLE reader takes: the lane's seat (cue_above_chip_h_px), the
// two extents (cue_chip_paint_reach_px, the hit pair) and so every cull,
// damage box and published rect. The painter alone reads the exact H
// (CoverageStampPass::chip).
inline double cue_chip_h_exact_px() {
    return static_cast<double>(cue_triangle_rows());
}
inline int cue_chip_h_px() {
    return static_cast<int>(std::ceil(cue_chip_h_exact_px()));
}
// THE LABEL'S HORIZONTAL LENGTHS, WHOLE W through scaled_px (the text
// round, architect 2026-11-03; whole W, the unit, 2026-11-04): THE
// FILL'S PAD either side of a segment's text, kCueFillPadW = 1, one W (a
// pad inside a box; 2 device px on the tablet at 200 %, 1 at 100 %)
// — the resting segment's box, the press target, the flag editor's field
// round its run (the caret's column borrowed from its right pad, the caret
// one W too) — and THE SEGMENT GAP between two of a marker's segments' boxes
// (the bound cells, the history's two halves), kCueSegmentGapW = 6 W (12
// device px at 200 %, 6 at 100 %), each floored at one device px; the cull
// bounds sum them (marker_flag_max_width_px, the history lane's). THE PAIR
// 2 / 12 (the unit, architect 2026-11-04: every authored length a whole W,
// so the 4 / 13 he kept on 2026-11-03 rounds to the unit's pair).
inline constexpr double kCueFillPadW    = 1.0;
inline constexpr double kCueSegmentGapW = 6.0;
inline int cue_fill_pad_px() {
    return scaled_px(kCueFillPadW, 1);
}
inline int cue_segment_gap_px() {
    return scaled_px(kCueSegmentGapW, 1);
}
// FROM ONE SEGMENT'S TEXT END TO THE NEXT SEGMENT'S TEXT START: the first's
// right pad, the segment gap, the second's left pad — the run-to-run step
// the segment boxes lay (cue_segment_boxes, render.cpp: a box is the text's
// own advance, unrounded, plus a pad either side). Its one other reader is the `h`
// view's cue tooltip, which lays a changed item's struck run and plain run
// on one line by the same step (architect 2026-11-11: "both in
// one line with the old one struck and the old one listed first";
// paint_shift_tooltip, paint_handler.cpp).
inline int cue_segment_text_gap_px() {
    return 2 * cue_fill_pad_px() + cue_segment_gap_px();
}
// THE LABEL'S LEAD from the marker's column to its text IS THE CHIP'S
// REACH RIGHT AND THE GAP (architect 2026-10-26 ~12:40, on the glass at nine
// rows: "It seems the lead-in was not scaled — increasing rows seems to not
// move text, but it should, relative to what I tuned it to"): the chip's
// reach (the rows, cue_triangle_rows — the bare triangle's half-base, its
// top corner's distance from the mark), then THE GAP, 5 W (kCueLabelGapW;
// 10 device px on the tablet at 200 %) through scaled_px, rounded at the
// element — so the label follows the rows and keeps the gap. THE GAP IS
// MEASURED FROM THE CHIP'S OWN TOP CORNER to the text's origin (architect
// 2026-11-19, on the smaller chip: "the text should move left to make sure
// that the margin remains the same" — the margin the
// outlined chip's corner kept to the text, 9.17 device px on the tablet,
// 4.59 W, OFF THE GRID, TAKEN AT THE NEAREST WHOLE W, 5 W, 10 device px),
// AT EVERY ROW COUNT. At 200 %: 8 + 10 = 18 device px; at 100 %: 4 + 5 = 9.
// The gap is never negative, so the text never stands under the flag. THE
// FILL'S LEAD IS THE LEAD LESS THE FILL'S PAD (cue_fill_lead_px): the text
// stands one pad inside its fill (cue_segment_boxes, render.cpp). Readers:
// the segment boxes (cue_segment_boxes), the cull bounds
// (marker_flag_max_width_px below, the history lane's), the flag editor's
// field (render_flag_editor_box, at the edited segment's seat).
inline constexpr double kCueLabelGapW = 5.0;
inline int cue_label_lead_px() {
    return cue_triangle_rows() + scaled_px(kCueLabelGapW);
}
inline int cue_fill_lead_px() {
    return cue_label_lead_px() - cue_fill_pad_px();
}
// THE MARKER LANE (architect 2026-11-19: "the markers lane looks like, if
// this was being designed by whoever designed Arc, it would be smaller in
// height ... a tighter lane"; the ruler and the lane "should shrink in
// sync"): kMarkerLaneW, 16 W through scaled_px — 32 device rows on the
// tablet at 200 % — the ruler's height (kRulerW), A STATED NUMBER. THE
// LABEL'S BASELINE by the line-box rule (line_box_baseline): the Label
// face's 14 W line box (Regular 12 W, 11 + 3) centered in 16, its top
// floor(2 / 2) = 1 W down, the baseline the ascent under it, +12 W (24
// device rows) under the lane's top (cue_baseline_px). ITS WHOLE
// ROWS ARE THE FLAG EDITOR'S BOX, GTK's in-place cell editor (the EDITING
// paragraph above): its ONE-W BORDER (kCueEditorBorderW) on the lane's
// first and last rows, and inside it THE LINE BOX'S ROWS (the caret's and
// the selection's, cue_line_box_top_px / cue_line_box_h_px below) — in the
// 16 W lane the 14 W line box stands 1 W down, so the border and the line
// box tile the lane: on the tablet at 200 % the border rows 0..1 and
// 30..31 under the lane's top, the line box rows 2..29 (28 rows), the
// baseline 24. The chip's rows are the lane's bottom rows
// (cue_above_chip_h_px the remainder above its hull: 32 − 8 = 24 on the
// tablet).
inline constexpr double kMarkerLaneW = 16.0;
inline constexpr double kCueEditorBorderW = 1.0;
inline int cue_editor_border_px() {
    return scaled_px(kCueEditorBorderW, 1);
}
inline int marker_lane_h_px() {
    return scaled_px(kMarkerLaneW, 1);
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
// THE CAP-BAND SEAT (paint_handler.cpp, where its rule stands): the
// baseline that centers `font`'s cap band in the box [box_y, box_y + box_h)
// exactly, a real y (no half rounded). Its one caller is the on-screen
// keyboard's caps; every other text stands on line_box_baseline above or a
// stated baseline.
double redesign_baseline(const GuiFont& font, double box_y, double box_h);
// THE CUE LABEL'S BASELINE AND THE EDITOR'S LINE BOX under the marker
// lane's top (the lane's rule above): THE BASELINE the line-box rule's,
// +12 W (24 device rows on the tablet at 200 %); THE LINE BOX — the flag
// editor's caret and selection rows (render_flag_editor_box reads
// cue_line_box_top_px and cue_line_box_h_px, never a second derivation;
// the value editors' rule, one look for both editors) — the Label face's
// ascent above the baseline and its descent below, whole W (the face's
// line box, gui_font_line_box_w): rows 2 .. 29 under the lane's top on the
// tablet, 1 .. 14 at 100 %.
// Every label segment — the resting label, the bound cells, the history's
// two halves — and the flag editor's text ride the baseline (the editor's
// text sits at the label's seat).
inline double cue_baseline_px() {
    return line_box_baseline(gui_font(GuiFace::Label), 0.0,
                             static_cast<int>(kMarkerLaneW));
}
inline double cue_line_box_top_px() {
    return cue_baseline_px() -
           static_cast<double>(
               scaled_px(gui_font_line_box_w(gui_font(GuiFace::Label)).ascent));
}
inline double cue_line_box_h_px() {
    return static_cast<double>(
        scaled_px(gui_font_line_box_w(gui_font(GuiFace::Label)).line()));
}
// THE CHIP'S TWO EXTENTS (architect 2026-11-14), H the chip's
// height's whole hull (cue_chip_h_px) — the marker's chip
// and THE RULER'S HEAD alike, the head the same drawing:
//   THE PAINTED REACH, cue_chip_paint_reach_px = H + 1 EACH SIDE: the chip
//     is painted at its mark's exact x, the real interval [x − H, x + H]
//     with x within half a px of col, so the pixels it can touch are the
//     columns [col − H − 1, col + H + 1) — 9 each side on the tablet. ITS
//     READERS, every CULL and DAMAGE BOX: the flag iterator's right cull
//     (iterate_visible_flags_impl, render.cpp), the ruler heads' one box
//     (GuiPaintHandler::ruler_head_box: the playhead's head's cull and the
//     hover line's triangle's cull, record and damage, paint_ruler_row and
//     GuiInputHandler::set_hover_line; architect 2026-11-21), the
//     playhead's cull (render_playhead) and its damage box
//     (playhead_invalidate_rect, main.cpp), so no repaint can be narrower
//     than what a head painted.
//   THE HIT EXTENT, cue_chip_hit_left_px = H − 1 left and
//     cue_chip_hit_right_px = H right of the mark, 7 and 8 device px on
//     the tablet at 200 %, 3 and 4 at 100 %: the box every PRESS AND
//     PUBLISHED RECT keeps. ITS READERS: THE CUE'S, OFF THE CUE'S EXACT x
//     (architect 2026-11-15, the no-grid arc) — [x − (H − 1), x + H), the
//     published chip box and the label's occluding box from its own chip's
//     left edge (paint_cues, render.cpp; no label is clipped) — and THE
//     HEAD'S, off the playhead's exact x the same way (the published hit,
//     paint_ruler_row, AppState::playhead_head_hit). BOTH ARE TESTED WITH
//     THE POINTER'S REAL PLACE, half-open ("yes, I'd like to move to
//     fractional hitboxes."): hit_test_flag's walk and
//     point_on_playhead_head read the place the panel reported, never the
//     pixel that holds it. The head's drag takes the same hit: no floor
//     (the pen makes a target's size nearly irrelevant; reach speed is what
//     counts).
// The chip's rows are [yb − H, yb), yb the bottom edge of the row its apex
// stands on.
inline int cue_chip_paint_reach_px() {
    return cue_chip_h_px() + 1;
}
inline int cue_chip_hit_left_px() {
    return cue_chip_h_px() - 1;
}
inline int cue_chip_hit_right_px() {
    return cue_chip_h_px();
}
// THE WELL'S EDGES (architect 2026-11-19, EDGE TO EDGE): under the marker
// lane THE LINE between the lane and the canvas is a lane of its own, top
// lane 4, one hairline (lane_foot_line_h_px); under the canvas THE WELL'S
// FOOT, one hairline, a bottom lane of its own (well_foot_h_px), between
// the canvas and the bottom icon row. ACROSS, THE WELL IS THE PANEL'S
// WHOLE WIDTH: no margin and no frame's side (his "the lanes, their lines
// and the canvas run the panel's whole width"), so the waveform area is
// x 0 .. the window's last column.
//
// THE WELL'S LINES (architect 2026-11-17: "lets use the thunar border" — Arc's $borders_color; the tone `well_border`,
// gui_theme.h): ONE W, ACROSS THE PANEL'S WHOLE WIDTH — the
// trim lane's first row (the line under the top icon row) and its last
// (under the trough), the ruler lane's last row, the lane's foot line (top
// lane 4), the well's foot, and on the canvas THE STEREO DIVIDER
// (render_waveform_divider); the status bar's line is the status bar's own
// (paint_status_bar). ONE PAINTER for its parts within a band of rows,
// paint_well_frame_rows (paint_handler.cpp), called by each painter that
// owns rows of the well — paint_trim (the trim lane), paint_ruler_row (the
// ruler and marker lanes) and paint_canvas_column_frame (the foot line and
// the well's foot) — after its own fills, on every frame class: on a frame
// with no audio loaded paint_trim and paint_ruler_row run their ground
// steps alone, so the three upper lanes wear their own grounds, empty, under
// their lines (architect 2026-11-21).
inline int lane_foot_line_h_px() {
    return hairline_px();
}
inline int well_foot_h_px() {
    return hairline_px();
}
// The two lanes (main.cpp, beside the other lane accessors).
GuiRect lane_foot_line_area(const AppState& a);
GuiRect well_foot_area(const AppState& a);
// THE PANEL'S INSET (architect 2026-11-19): kEdgeInsetW, 6 W through
// scaled_px (12 device px on the tablet at 200 %) — THE KEYS' OUTERMOST
// FACES in both icon rows stand this far inside the panel's left and right
// edges (Arc's toolbar margin). (The title bar's first and last ink stand
// 8 W in: kTitleNoteInsetW / kTitleRightInsetW.)
inline constexpr double kEdgeInsetW = 6.0;
inline int edge_inset_px() {
    return scaled_px(kEdgeInsetW, 0);
}
// THE WELL'S COLUMNS (the one owner): its first column and one past its
// last — THE PANEL'S, edge to edge (architect 2026-11-19): 0 and 2304 on
// the tablet. THE EDGE RULE (architect 2026-11-19; his 2026-11-03
// "everything that sits closest to the window edge on all screens should
// align to the waveform canvas", the canvas the panel's width): THE LANES,
// THEIR LINES AND THE CANVAS RUN THESE COLUMNS; THE KEYS' OUTERMOST FACES
// STAND kEdgeInsetW INSIDE THEM (the top icon row's ends, icon_row_seats;
// the bottom icon row's and the controls rows', bottom_row_seats), THE
// TITLE BAR'S FIRST
// AND LAST INK 8 W (kTitleNoteInsetW / kTitleRightInsetW, paint_title_bar),
// THE BOTTOM ICON ROW'S LEFT RUN'S PEN 14 W (kBottomLeftPenW); THE TOOLTIP'S BOX
// AND THE MODAL PANE ARE HELD INSIDE THEM (the tooltip's clamp — its
// shadow may hang past them — and modal_box_rect on these columns); THE
// CARDS' STACK STANDS kNotificationSeatW (16 W) INSIDE THE WINDOW'S LAST
// COLUMN AND UNDER ITS FIRST ROW, notifyd's seat (notification_stack_bound,
// 2026-11-19). The
// on-screen keyboard is exempt, a small block seated on the window's
// center by his ruling (onscreen_keyboard.h's block_x_px).
struct WellColumns {
    int x   = 0;   // the panel's first column (the waveform area's x)
    int end = 0;   // one past the panel's last column (the area's x + w)
};
WellColumns well_columns(const AppState& a);

// Authored pixel geometry of THE BOTTOM ICON ROW (architect 2026-08-12):
// one plain-text run at the left — the timestamp alone (architect
// 2026-11-19; alone, 2026-11-24; paint_handler.cpp's
// bottom_left_run) — and the COPY / PASTE duo, the skips (Go to Start, Go
// to End), the MARKER-VERB GROUP, the step and
// walk (Down, Up, Previous Marker, Next Marker) and the nudge and play
// (Center, Left, Right and Play / Stop, the last case; architect
// 2026-10-18) packed right, each one linked group, the run gap
// (program_run_gap_px, kRunGapW) of bare ground
// between two (kMarkerVerbGroup in paint_handler.cpp owns that group's
// membership), all one lane over the status bar.
//
// THE ROW IS THE STRIP (architect 2026-10-22; Arc's pads 2026-11-19): THE
// PAD (7 W), the program's key (the top icon row's own, read from its
// accessors above so the rows cannot drift apart), THE PAD AGAIN (7 W,
// symmetric, architect 2026-11-20) — strip_h_px, 42 W, 84 device rows on
// the tablet at 200 % (14 + 56 + 14) — on the body tone, no line, no bar
// and no border. The layout is paint_bottom_row_buttons_and_clock's walk.
// Every overlay — the render player, the project picker's contents, the
// score viewer — stands its controls on these rows, THE MODAL FAMILY'S
// CONTROLS ROW (modal_box.h); UNDER A QUESTION'S PAGE OR THE PROGRESS PAGE
// THE ROWS HOLD THE PAGE'S ANSWERS, Arc's word buttons flush right, and
// nothing else — no timestamp, no key, none of an overlay's
// controls (architect 2026-11-20; paint_handler.cpp's
// modal_owns_bottom_row); UNDER EVERY EDITOR THE ON-SCREEN KEYBOARD STANDS
// OVER THE ROWS and the row yields (onscreen_keyboard.h's placement block,
// architect 2026-11-20); under each of them the top icon row greyed whole
// above it (roster_greyed_whole, redesign_button_enabled's first arm,
// app_state.h).
inline int bottom_row_h_px() {
    return strip_h_px();
}

// THE STATUS BAR — THUNAR'S (architect 2026-11-18 / 2026-11-19; his:
// "if you wanted to come up with a status bar, that might be good ... And
// it's okay if it takes away from the waveform height"; measured on his
// 9 pt shot: the line, then 22 rows): THE APP'S LAST LANE, under
// the bottom icon row — ONE W OF `well_border` (Arc's $borders_color, the
// line Thunar draws over its bar) AND THE BAR, kStatusBarPadW (4 W) + the
// body face's line box (14 W, 11 + 3) + kStatusBarPadW: 1 + 22 = 23 W, 46
// device rows on the tablet at 200 % (the line 1394 .. 1395 and the bar
// 1396 .. 1439, under the on-screen keyboard while it stands), each part
// through scaled_px,
// rounded at the element. THE BAR IS
// THE BODY TONE `body_face`, its text Regular 12 W (GuiFace::Body) in `text`,
// THE PEN kStatusBarPenW (14 W) from the panel's left edge — THUNAR'S
// MEASURED INSET (his 9 pt shot: the status text 14 px in, 4 px above and
// below it), KEPT over a 4 W margin of our own (architect 2026-11-19:
// "Good. I agree with Tunar's 14W. And then we put the timestamp line at
// 14W") — so the bottom icon row's left run above it
// (kBottomLeftPenW, its own 14 W by the same ruling) starts on the same
// column — THE BASELINE by the line-box rule, +15 W of the 22
// (line_box_baseline). Its content and
// its damage: paint_status_bar and status_bar_text (paint_handler.cpp).
// INERT CHROME: no hit, no tooltip, no gesture. It stands under every
// overlay and modal (the controls row above it reading bottom_row_area;
// architect 2026-11-19: overlays keep the bottom icon row and the
// status bar, "which sets the precedent that overlays keep them"), on the
// window's foot, COVERED BY THE ON-SCREEN KEYBOARD under every editor, its
// painter yielding there (architect 2026-11-20: "The status bar
// should just be obscured by the OSK"; onscreen_keyboard.h's placement
// block).
inline constexpr double kStatusBarPadW = 4.0;
inline constexpr double kStatusBarPenW = 14.0;
// THE BOTTOM ICON ROW'S LEFT RUN'S PEN (architect 2026-11-19: "And then
// we put the timestamp line at 14W"): the timestamp alone (architect
// 2026-11-24), plain text pressing nothing (architect
// 2026-11-20), from 14 W inside the
// panel's left edge (28 device px on the tablet), the status bar's column
// (paint_handler.cpp's bottom_left_run).
inline constexpr double kBottomLeftPenW = 14.0;
inline int status_bar_line_box_w() {
    return gui_font_line_box_w(gui_font(GuiFace::Body)).line();
}
inline int status_bar_w() {
    return 2 * static_cast<int>(kStatusBarPadW) + status_bar_line_box_w();
}
inline int status_bar_h_px() {
    return scaled_px(kStatusBarPadW, 0) + scaled_px(status_bar_line_box_w()) +
           scaled_px(kStatusBarPadW, 0);
}
inline int status_bar_lane_h_px() {
    return hairline_px() + status_bar_h_px();
}


// AT THE WAVEFORM'S EDGES A CUE IS CLIPPED (architect 2026-09-26, the flag
// box's rule kept for the cue): the marker lane paints inside the waveform's
// columns [0, w) — every triangle, label, editor field and hit rect
// (clip_to_waveform_columns, render.cpp), the columns standing at the
// canvas's own x (waveform_area's, the panel's left edge, the canvas edge
// to edge) — and
// the flag iterator admits a marker whose triangle can reach into them (its
// painted reach cue_chip_paint_reach_px left of its column, 9 device px on
// the tablet at 200 %), so a marker just past the last
// column shows the left half of its chip there, and of its stem the part
// of the one-W line that still lies inside the columns (the stem stash's
// gate is the line's own span, stem_meets_waveform, render.cpp).
// THE SCALE'S LENGTHS AND ITS ONE GEOMETRY (the scrub's block above; Arc's
// CSS numbers for its scale: the trough 5 px thick, the slider 17 px — one Arc
// px one W), each through scaled_px, rounded at the element: the trough
// kScaleTroughW (10 device px on the tablet at 200 %, 5 at 100 %), the knob
// kScaleKnobW (34 and 17). Every box below derives from THE ITEM, the
// published GuiRect (scale_item_rect), so the painter, the hit and the
// damage read one geometry: the trough and the knob centered EXACTLY on the
// item's rows (no half rounded), the knob's center a real x.
inline constexpr double kScaleTroughW = 5.0;
inline constexpr double kScaleKnobW   = 17.0;
inline int scale_trough_h_px() { return scaled_px(kScaleTroughW, 1); }
inline int scale_knob_px() { return scaled_px(kScaleKnobW, 3); }
// THE ITEM: the trough's columns [x0, x0 + w) by THE KNOB'S ROWS, the knob
// centered exactly on `band`'s rows (the controls row's key rows) — the
// knob's hull, so whole: on the tablet 1335..1368 under keys 1324..1379.
inline GuiRect scale_item_rect(int x0, int w, const GuiRect& band) {
    const double d = static_cast<double>(scale_knob_px());
    const GuiRect rows = hull(GuiRectF{
        static_cast<double>(x0),
        static_cast<double>(band.y) + (static_cast<double>(band.h) - d) / 2.0,
        static_cast<double>(w), d});
    return GuiRect{x0, rows.y, w, rows.h};
}
// The row the trough and the knob center on: the item's middle, which is
// the band's middle (the item is the knob's hull about it).
inline double scale_center_y(const GuiRect& item) {
    return static_cast<double>(item.y) + static_cast<double>(item.h) / 2.0;
}
inline GuiRectF scale_trough_box(const GuiRect& item) {
    const double t = static_cast<double>(scale_trough_h_px());
    return GuiRectF{static_cast<double>(item.x), scale_center_y(item) - t / 2.0,
                    static_cast<double>(item.w), t};
}
inline GuiRectF scale_knob_box(const GuiRect& item, double center_x) {
    const double d = static_cast<double>(scale_knob_px());
    return GuiRectF{center_x - d / 2.0, scale_center_y(item) - d / 2.0, d, d};
}
// THE KNOB'S TRAVEL: its center from the trough's left end plus half the
// knob to its right end less half the knob — the mappings' one pair (an
// item narrower than the knob has no span and answers its start).
inline double scale_travel_x0(const GuiRect& item) {
    return static_cast<double>(item.x) +
           static_cast<double>(scale_knob_px()) / 2.0;
}
inline int scale_travel_span(const GuiRect& item) {
    return std::max(0, item.w - scale_knob_px());
}
// THE KNOB'S HIT: the real point (x, y) inside the disc of the knob whose
// center stands at `center_x`, the disc's boundary out (the block above).
inline bool scale_knob_hit(const GuiRect& item, double center_x, double x,
                           double y) {
    if (item.w <= 0 || item.h <= 0) return false;
    const double r  = static_cast<double>(scale_knob_px()) / 2.0;
    const double dx = x - center_x;
    const double dy = y - scale_center_y(item);
    return dx * dx + dy * dy < r * r;
}
// A MOVING KNOB'S DAMAGE (the block above): the hull of its discs at the
// real places `from_x` and `to_x` and the trough between, on the item's
// rows, cut to the item.
inline GuiRect scale_knob_move_rect(const GuiRect& item, double from_x,
                                    double to_x) {
    const double half = static_cast<double>(scale_knob_px()) / 2.0;
    const double lo = std::min(from_x, to_x) - half;
    const double hi = std::max(from_x, to_x) + half;
    const GuiRect cols = hull(GuiRectF{lo, static_cast<double>(item.y),
                                       hi - lo, static_cast<double>(item.h)});
    const int x0 = std::max(cols.x, item.x);
    const int x1 = std::min(cols.x + cols.w, item.x + item.w);
    if (x1 <= x0) return GuiRect{item.x, item.y, 0, 0};
    return GuiRect{x0, item.y, x1 - x0, item.h};
}
// THE SCALE PAINTED on `item` (render.cpp): the trough, the played part to
// the knob's center `knob_x`, the knob AS PAINTED — `knob` its FaceMix over
// rest, hover and held (materials.h; the scrub's block above;
// AppState::FaceFades, whose fades run every change of the knob's look over
// Arc's 200 ms).
struct FaceMix;
void paint_scale(cairo_t* cr, const GuiRect& item, double knob_x,
                 const FaceMix& knob);
// THE WAVEFORM AREA CARRIES NO BORDER: the canvas has none (THE WAVEFORM
// block's canvas paragraph), the rows and columns of ground stand outside
// the area.
// THE WAVEFORM'S LINE WIDTH — ONE W, THE LINE RULE'S (architect 2026-11-04,
// the unit: lines scale, as measured on his 100 % / 125 % shots):
// hairline_px itself, 2 device px on the tablet at 200 %, 1 at 100 %.
// EVERY LINE PAINTED ON THE CANVAS is this width: THE READERS — the one
// inventory of the class, each SOLID AND AT ITS EXACT PLACE, EACH PIXEL AT
// ITS EXACT COVERAGE (architect 2026-11-14: "the anti-aliasing I would like
// to commit to that as well, the full anti-aliasing") — the scanner and the
// resting cursor
// (render_playhead), the selection stems (paint_marker_stems), the
// zoom anchor's stem (render_strip_anchor_stem), the phase-reset lead-in
// ring's four sides (paint_phase_reset_overlay_ring), the canvas's GRID
// AND CENTER LINES (render_canvas, render_center_lines: a t-thick line
// centered on its band's exact row, canvas_hline_span) — and THE STEREO
// DIVIDER, whole rows (chrome
// standing on the canvas, waveform_divider) — and, off the canvas, THE
// RULER'S TICKS (paint_ruler_row, the same line on the ruler's foot). THE
// LINE RULE'S ONE STATEMENT (architect 2026-11-06, "I like your
// suggestion"): A FRAME IS A PIXEL BOUNDARY, NOT
// A PIXEL — device column c of the waveform area spans the lattice points
// g(c) .. g(c + 1) of the displayed viewport's lattice (the column
// coordinates the plate's bars are placed in, THE PLATE'S BARS; at a
// landing on a whole level each bar is its column), so a frame on the
// lattice is the boundary between device columns c − 1 and c, the left
// edge of column c —
// AND A VERTICAL LINE OF WIDTH t IS CENTERED ON ITS FRAME'S EXACT PLACE,
// the real interval [x − t / 2, x + t / 2] (architect 2026-11-14;
// waveform_line_span below, the one owner, its emptiness the gate): a
// frame on a whole place at
// the tablet's t = 2 takes the
// column left of the boundary and the frame's own, each whole. Nothing
// moves in the lattice, the camera, the landings or the
// plate: the line's paint alone. THE HORIZONTAL LINES ARE CENTERED ON THEIR
// EXACT ROWS LIKEWISE (the grid and the center lines, canvas_hline_span:
// the band's zero is the middle of a row on an odd band, the tablet's 535
// rows with the key 28 W tall, so a center line is half a row, a whole row
// and half a row; on an even band a row boundary, the line two whole rows).
// THE CUE'S CHIP AND THE PLAYHEAD'S HEAD STAND ON THE SAME EXACT PLACE,
// their apex on the line's center (architect 2026-11-14: "Agree on both.";
// the chip's block at cue_chip_h_px). THE PLATE DRAWS NO LINE: the
// magnified inner is a solid fill, THE CORE (architect 2026-11-04, THE CORE
// below), so every line on the canvas is one W. EVERYTHING ELSE OFF THE
// CANVAS keeps its areas scaled (scaled_px).
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
// THE GRID IS 4x FOR GOOD (architect 2026-10-31, "let's just stick with
// 4x. any perf hit is not worth additional thickness"): an 8x grid measured
// on the tablet, per plate frame, ~300 ms with the grow a uniform and, with
// the shader specialized per grow, ~16 ms at grow 0, ~36 ms at 0.5 and
// ~100 .. 110 ms at 0.625 and 0.75 (the two-word window) — far past the
// frame's budget — so the key steps in quarters.
// THE GROW IS A DEVICE KEY, `waveform_grow`, IN QUARTERS OF A DEVICE PX
// (architect 2026-10-31; device_config.h's grammar, the seat
// waveform_grow_quarters above, the plate shader's GROW): a multiple of 1/4
// device px in [0, 2], and it STAYS A KEY — THE PLATE IS THE ONE THING ON
// THE CANVAS NOT SCALED WITH gui_scale (its bars are counted in device px —
// one at the working zoom, 1 .. 2 across a level, architect 2026-11-14,
// THE PLATE'S BARS — and its quarter-row tips are device px; the canvas's
// lines are one W, waveform_line_px), so the waveform needs one key
// of its own and the grow is the appropriate one, kept for good because a
// replacement panel may not come at this density: the grow is the device-px
// number whose right value depends most on the panel's density. THE DEFAULT
// IS 1.0 — kWaveformGrowDefaultQuarters = 4 subcells, the construction
// state, the first-run value the shipped defaults file's (device_config.h's
// THE FIRST-RUN VALUES) — (architect 2026-11-04, on the real lit capture at
// grow 1 beside the 2-px lines: "is good as well"). 0.625, the exact analog
// of Ableton at 125 % Windows scale, lies off the quarters lattice. A
// quarter of a device px is one subcell (the key's lattice,
// kWaveformGrowQuartersPerPx, device_config.h). The shader spells the grid
// as literals; gl_canvas.cpp static_asserts them against this constant and
// its one 32-bit window — the grow's 2 px alone (the core) — against the
// key's top.
constexpr int kWaveformSubcells = 4;
// THE NEVER-FADE FLOOR IS A QUARTER DEVICE PX (architect 2026-10-30, judged
// on his glass against Ableton's measured silence line): a bar whose
// interval holds less than this many subrows takes this many (bar_tips,
// render.cpp; the paragraph at render_waveform_columns' head), so digital
// silence is a one-subrow bar which the grow makes a line of. THE LINE'S
// NUMBERS, their one owner: a run of silent columns is one subrow in every
// screen subcolumn, and the disc of GROW subcells stands GROW rows each way
// over it (its dx = 0 column), so the line is 1 + 2 · GROW subrows in each
// subcolumn — 4 · (1 + 2 · GROW) of a pixel column's subcells, (1 + 2 ·
// GROW) / 4 device px of summed coverage whichever quarter-row phase the
// tip has. AT THE DEFAULT GROW, 1 px (GROW 4): nine subrows, nonzero
// coverage over three device rows, 36 of 16 subcells, 2.25 device px. AT
// 0.5 px (GROW 2), the grow the floor was judged at: five subrows over two
// device rows, 20 of 16, 1.25 device px — Ableton's measured silence line.
// At grow 0 the one subrow alone, a quarter px. A quarter of the grid,
// which must divide into quarters: one subrow on the 4x grid, the constant
// kept as the floor's own statement.
constexpr int kWaveformFloorSubcells = kWaveformSubcells / 4;
static_assert(kWaveformSubcells % 4 == 0,
              "the never-fade floor is a quarter device px of subrows");
// THE LINE AT ITS EXACT PLACE — THE LINE'S ONE PAINT (architect 2026-11-14:
// "the anti-aliasing I would like to commit to that as well, the
// full anti-aliasing"; "I would expect snapping nothing to integers and
// leaving them rounding to yield decent results"): a vertical line of width
// `t` (the canvas's line, one W, waveform_line_px) whose frame stands at
// the exact place `x` (displayed_x_at, warp_frame_map_view.h: device px
// from the strip's left edge, the frame a pixel BOUNDARY) is THE REAL
// INTERVAL [X − t / 2, X + t / 2] in window px, X = area_x + x — centered
// on the frame exactly, at every t — CUT TO THE STRIP'S COLUMNS [area_x,
// area_x + area_w] as a real interval, and its painter lays the line's
// color on each pixel at the exact area of the pixel the interval covers:
// the canvas's lines through the sink's blended rect
// (GuiCanvasSink::blended_rect, canvas_sink.h), the chrome's through the
// coverage stamp (CoverageStampPass, cool_edit_paint.h). NOTHING IS
// SNAPPED: a whole x at an even t (the tablet's 2) covers the column left
// of the frame and the frame's own, each whole; AT AN ODD t a whole x
// covers its two end columns half each, the line centered on the frame's
// boundary — the rule's own consequence at a gui_scale the product does
// not run at.
// THE EDGE RULE, ONE FOR BOTH EDGES (architect 2026-11-15, the no-grid arc:
// "the priority would be moving everything to, as much as possible, to
// fractional. Only things that are basically impossible to move, like the
// damages and the culls, should stay integer."):
// A LINE IS DRAWN WHENEVER ANY OF ITS REAL INTERVAL LIES INSIDE THE STRIP,
// CUT AT THE STRIP'S EDGES — the span below is non-empty exactly when
// [X − t / 2, X + t / 2) meets [area_x, area_x + area_w), and it IS that
// intersection, so a line slides in and out at either edge by the fraction
// of it that is inside, and nothing reaches the ground beside the canvas.
// THE SPAN'S OWN EMPTINESS IS THE GATE: no column is asked, at the left edge
// or the right — a frame one past the area's last column (End at the
// whole-song zoom) shows the left half of its line on the last column, as a
// frame at column 0 shows its right half.
// THE READERS: canvas_line (render.cpp — the cursor's line, the scanner,
// the zoom anchor's stem, whose place is clamped into the strip), the
// selection stems' two halves (GuiPaintHandler::standing_stem_spans) and
// their producers' stash (stem_meets_waveform, render.cpp), the lead-in
// ring's left side (paint_phase_reset_overlay_ring) and the ruler's ticks
// (paint_ruler_row). CULLS STAY CULLS, on whole columns, and each is wider
// than the line it could cut (the flag iterator's, render_playhead's: the
// chip's reach, more than a W past the column a place lies within half a px
// of).
struct WaveformLineSpan {
    double x0 = 0.0;
    double x1 = 0.0;
    bool empty() const { return x1 <= x0; }
};
inline WaveformLineSpan waveform_line_span(int area_x, int area_w, double x,
                                           int t = waveform_line_px()) {
    const double X    = static_cast<double>(area_x) + x;
    const double half = static_cast<double>(t) / 2.0;
    return WaveformLineSpan{
        std::max(X - half, static_cast<double>(area_x)),
        std::min(X + half, static_cast<double>(area_x + area_w))};
}
// THE SCALE IS THE ONE THING A FLAG CUTS (architect 2026-09-19). A warp
// payload's TEMPO — its base and its whole deviation chain — is the reason
// the flag exists, so it paints in full however long the chain runs; what a
// glance does not need is the scale's low digits. So the DISPLAY composer
// (flag_display_text, render.h's flag-text block) keeps `*` plus these FOUR
// bytes of the scale — `*1.01` — and drops everything after them: a scale
// is spelled min-4 (format_value_double), so it is at least six bytes and
// always cuts.
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
// composer feeding a different surface — the BPM form's rows and its line
// editor — and never reaches a flag box, so no flag width depends on it.
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
// (kMaxTempoDeviationTerms terms at five bytes each, `+0.01` — sixteen
// terms), FIVE of capped scale (`*` and kMarkerFlagScaleGlyphs) and
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

// THE TRIM BAR'S CAPS AND GRABS (architect 2026-11-19 / 2026-11-20). THE
// CAP is after Kdenlive's zoom bar, NOT ARC'S ("a little half circle in
// each end cap ... the visual of it would give me something to aim at with
// the pen"; "I would want to be symmetrical"), ruled on glass (architect
// 2026-11-20: "i'd
// like to see the encaps radius be the same as the scrollbar's actual end
// radius but keeping the straight elongtated part of th eendcap"; his
// words of 2026-11-19: "it should be the same size as the scroll bar
// itself, not as jutting out from it"):
//   THE CAP IS THE PILL'S OWN END, CUT BY ONE VERTICAL LINE: from the pill's
//     tip inward, its round end (the pill's own radius, half its height —
//     3 W) and kTrimCapStraightW (2 W) of its straight run — 5 W wide, the
//     pill's own 6 W tall, its one computation render_trim_flags' `cap_w`
//     (render.cpp), off the pill's height as drawn (the pill's rows clamped
//     to the trough's, which never binds: the 12 W trough holds the 6 W pill
//     at every scale), so the cap's round end is the drawn pill's own: NOTHING OF A CAP STANDS OUTSIDE
//     THE PILL. Its outer edge is the pill's tip, the seat inside its bound
//     (THE SLIDER'S BOX AND ITS CORE, above), its cut line the seat + 5 W
//     inside it — the begin's off the begin's place, the end's the mirror
//     image off the end's (the end column's right edge, end.x + 1). Authored
//     as the pill's radius plus a straight length, so a retuned pill moves
//     the cap. On the tablet at 200 % the cap 10 x 12 device px, rows
//     148 .. 159 (the pill's), its tip 6 px and its cut line 16 px (8 W)
//     inside its bound; at 100 % 5 x 6, the tip 3 and the cut 8; at 125 %
//     6 x 8 (the radius 4 px, the straight run's 2.5 rounded to 2), the
//     tip 3.5 and the cut 9.5.
//   BOTH CAPS AT EVERY LENGTH, THE END'S OVER THE BEGIN'S (architect
//     2026-11-20: "shuold show both,wit the end one being on top
//     like the flags do" — a later flag's label covers an earlier one's):
//     wherever the look shows caps (trim_bar_tones), both stand, at any
//     length of the pill, down to its short forms (a disc of its height,
//     then the box itself): the end's cap takes its 5 W from the right tip
//     (the whole pill when the pill is shorter), the begin's cap what is
//     left of its own 5 W from the left tip, the body what is left between.
//     THE PILL IS STAMPED ONCE and its pixels colored by region
//     (render_trim_flags; CoverageStampPass::rounded_rect_bands): no pixel
//     is laid twice, so no seam shows between a cap and the body or between
//     the two caps.
//   THE GRABS — each end's ZONE over the trim lane's whole rows, from 2 W
//     OUTSIDE THE BOUND (trim_grab_outside_px, 4 device px on the tablet) to
//     12 W INSIDE IT (trim_grab_inside_px, 24 device px): 14 W wide, 28
//     device px. THE ZONE IS NOT THE CAP WITH THE SAME AIR EACH SIDE:
//     against the cap as drawn its air is 5 W outside the tip and 4 W
//     inside the cut line. It is the zone he tested on glass and ruled
//     "good" (architect 2026-11-20), authored as that zone's own numbers,
//     A CORE kTrimGrabCoreW (6 W) wide whose outer edge stands
//     kTrimGrabCoreLeadW (1 W) outside the pill's tip, and kTrimGrabAirW
//     (4 W) of air on each side of the core — the tested zone's rounded
//     terms in its order, so its device px are the tested zone's at
//     every gui_scale (100 %: 2 and 12; 125 %: 2.5 and 15.5; 200 %: 4 and
//     24). The core's inner edge is the cap's cut line wherever the pill's
//     radius and the straight run round whole (100 %, 200 %); at 125 % it
//     stands 1 px further in. THE BRIDGE is the span's body between the two
//     zones' inner edges and drags the pair (render_trim_flags publishes all
//     three; trim_endcap_rect and trim_bridge_gap own the rule). NARROW:
//     where the two zones would overlap — the grabs' span under two inside
//     reaches, 24 W — they split AT THE MIDPOINT BETWEEN THE BOUNDS, each
//     keeping its own outside reach, so neither zone stands past its own
//     bound's 2 W and the bridge is empty (trim_endcap_rect's rule). The
//     strip's edge, which is the glass's, cuts a zone like anything else
//     (the lane's clip).
// The zones and the caps read the seat off the same two heights the
// painter's seat is (trim_pill_seat_px, (trough rows − pill rows) / 2: the
// trim lane is the trough and its two lines, trim_lane_h_px, so the painter
// never clamps the trough), so they stand where they are drawn at every
// scale.
inline constexpr double kTrimCapStraightW  = 2.0;
inline constexpr double kTrimGrabCoreLeadW = 1.0;
inline constexpr double kTrimGrabCoreW     = 6.0;
inline constexpr double kTrimGrabAirW      = 4.0;
inline double trim_pill_seat_px() {
    return static_cast<double>(trim_trough_h_px() - trim_pill_h_px()) / 2.0;
}
inline int trim_cap_straight_px() {
    return scaled_px(kTrimCapStraightW, 1);
}
// The grab core's outer edge off its bound: the pill's tip less the lead.
inline double trim_grab_core_outer_px() {
    return trim_pill_seat_px() -
           static_cast<double>(scaled_px(kTrimGrabCoreLeadW, 1));
}
inline double trim_grab_outside_px() {
    return std::max(0.0, static_cast<double>(scaled_px(kTrimGrabAirW, 1)) -
                             trim_grab_core_outer_px());
}
inline double trim_grab_inside_px() {
    return trim_grab_core_outer_px() +
           static_cast<double>(scaled_px(kTrimGrabCoreW, 1)) +
           static_cast<double>(scaled_px(kTrimGrabAirW, 1));
}

// THE HOVER TOOLTIP'S SHARED NUMBERS — a DAMAGE BOUND on its box height, its
// seat's gap, its hover target (the key's column) and its three timeouts.
// They live out here, rather than with the rest of the tooltip's
// anatomy in paint_handler.cpp, because both the RUN LOOP and the input side
// read them:
// the words comparators run on the tick and damage the
// band every seat of the box lies in (tooltip_damage_band, app_state.h), and
// the one wait writer (note_tooltip_hover) and the two hover walks read the
// column, the wait's writer and the tooltip's clock (tick_tooltip) the
// timeouts, and the seat and the damage band both read the seat's gap.
//
// THE HEIGHT HERE IS A BOUND, NOT THE HEIGHT. The painter derives the real box
// from its lines' line boxes under its pads (paint_shift_tooltip; Arc's,
// 2026-11-19, below), so the run loop only needs to know it can never
// exceed this: 75 W, 150 device rows on the tablet against the two-line
// box's 96. THE SHADOW HANGS OUTSIDE IT
// (tooltip_shadow_reach below): the damage band adds its reach on both
// sides of the box's rows (tooltip_damage_band, app_state.h).
inline constexpr int     kTooltipDamageHeightPx = 75;
inline int tooltip_damage_h_px() {
    return scaled_px(kTooltipDamageHeightPx, 5);
}

// THE TOOLTIP IS ARC'S (architect 2026-11-19: "good to build", and "if we
// can do the transparency, we might as well"): Arc-Dark's `tooltip`
// (G3:1428-1436) — ITS BOX THE CARRIED FILL `tooltip_fill` (Arc's
// $osd_bg_color, rgba(75, 81, 98, .95)) AT ITS OPACITY OVER WHAT LIES UNDER
// IT, radius kTooltipRadiusW, NO BORDER — and ITS LINES in `sidebar_text`
// (BAC3CF), Regular 12 W, one line box (gui_font_line_box_w: 11 + 3 W) a
// line, the hint line the same size and tone; between the box's edges and
// the lines' line boxes kTooltipPadW on all four sides (GTK's tooltip
// window margin 6 + Arc's label padding 4, measured on his shots),
// so the first baseline stands +21 W, one line's box is 34 W tall, two
// lines' 48 W — 68 and 96 device rows on the tablet. THE BOX IS WHOLE PX
// (its width the ceiling of the widest line's advance under the pads): a
// floating surface's box, the hull rule's (render.h's hull).
inline constexpr int kTooltipPadW    = 10;
inline constexpr int kTooltipRadiusW = 2;
inline int tooltip_pad_px()    { return scaled_px(kTooltipPadW, 1); }
inline int tooltip_radius_px() { return scaled_px(kTooltipRadiusW, 0); }

// ARC'S TOOLTIP SHADOW (G3:1528, `tooltip.csd decoration { box-shadow: 0 1px
// 3px 1px rgba(0,0,0,.25) }`; his settings leave show_popup_shadow off, so
// the CSS shadow is the one drawn) AS GTK 3.24 DRAWS AN OUTSET BOX-SHADOW
// (gtkcssshadowvalue.c's paint_box over gtkcairoblur.c, read in GTK's own
// fetched source and reproduced to the byte on his shots by the mock kit):
// THE BOX MOVED DOWN
// kTooltipShadowOffsetW AND GROWN kTooltipShadowSpreadW on every side, its
// corner radius the box's plus the spread (3 W), filled into an 8-bit mask
// and BLURRED BY THREE BOX PASSES kTooltipShadowBoxW wide — GTK's
// (int) (3 √(2π) / 4 · 3) = 5 px at its blur 3, laid in W, so 10 device px
// on the tablet (even: GTK's (d, +1), (d, −1), (d + 1, centered) form) —
// first down the columns, then along the rows, in GTK's integer arithmetic;
// then BLACK AT kTooltipShadowAlpha painted through it, OUTSIDE THE BOX ONLY
// (paint_tooltip_shadow, paint_handler.cpp). The seat is measured to the
// box: the shadow may leave the panel or cover the owner's face.
inline constexpr int    kTooltipShadowOffsetW = 1;
inline constexpr int    kTooltipShadowSpreadW = 1;
inline constexpr int    kTooltipShadowBoxW    = 5;
inline constexpr double kTooltipShadowAlpha   = 0.25;
inline int tooltip_shadow_offset_px() { return scaled_px(kTooltipShadowOffsetW, 0); }
inline int tooltip_shadow_spread_px() { return scaled_px(kTooltipShadowSpreadW, 0); }
inline int tooltip_shadow_box_px()    { return scaled_px(kTooltipShadowBoxW, 1); }
// THE BLUR'S REACH, device px, each side: what the three passes spread one
// mask pixel — a pass of width d whose window ends `offset` past its output
// spreads an input `offset` px one way and d − 1 − offset the other; odd d
// is centered (offset d / 2), even d's two halves offset (d − 1) / 2 and
// (d + 1) / 2 and its third, d + 1 wide, centered — the same on both sides
// in sum: 3 (d / 2) for odd d, 3 d / 2 − 1 for even. 14 on the tablet (d 10),
// 6 at 100 % (d 5).
inline int tooltip_shadow_blur_reach_px() {
    const int d = tooltip_shadow_box_px();
    if (d <= 1) return 0;
    return d % 2 == 1 ? 3 * (d / 2) : 3 * d / 2 - 1;
}
// THE SHADOW'S REACH OUTSIDE THE BOX, device px, per side — the spread and
// the blur, the offset taken from the top and given to the bottom: 16 at
// each side, 14 above and 18 below on the tablet (7 / 6 / 8 at 100 %; the
// outermost of them may round to nothing). The painter's extent and every
// tooltip damage read it (paint_shift_tooltip, tooltip_damage_band).
struct TooltipShadowReach {
    int left = 0, top = 0, right = 0, bottom = 0;
};
inline TooltipShadowReach tooltip_shadow_reach() {
    const int s = tooltip_shadow_spread_px();
    const int o = tooltip_shadow_offset_px();
    const int b = tooltip_shadow_blur_reach_px();
    return TooltipShadowReach{s + b, std::max(0, s - o + b), s + b,
                              s + o + b};
}
// The box's hull grown by the shadow's reach: everything the tooltip paints.
inline GuiRect tooltip_painted_extent(const GuiRect& box) {
    if (box.w <= 0 || box.h <= 0) return box;
    const TooltipShadowReach r = tooltip_shadow_reach();
    return GuiRect{box.x - r.left, box.y - r.top, box.w + r.left + r.right,
                   box.h + r.top + r.bottom};
}

// THE SEAT IS THE OWNER'S FACE'S (architect 2026-11-07): the box under a
// top-row key's painted face and above every other (2026-11-19), ITS LEFT
// EDGE ON
// THE FACE'S, THE GAP kTooltipGapW between them, held inside the panel and
// under the window's top; one rule on every surface. The placement's one statement is tooltip_box_rect
// (app_state.h). THE GAP IS 1 W (architect 2026-11-07: "ok to remove the
// key", closed_questions.md), 2 device px on the tablet at 200 %; read by the seat
// and its damage twin (tooltip_damage_band) alike. Floored at 0: a gap may
// vanish at a small scale (0 at 50 %, banker's rounding), the box then
// standing on the face's top row.
inline constexpr int kTooltipGapW = 1;
inline int tooltip_gap_px() {
    return scaled_px(kTooltipGapW, 0);
}

// THE TIMING IS GTK'S OWN (architect 2026-11-22: "just use their numbers,
// just like we do for hover"): gtk-3-24's gtk/gtktooltip.c (identical in
// main), its three #defines —
//   * HOVER_TIMEOUT 500 — kTooltipHoverTimeoutMs: the wait of an arrival
//     with no chain holding (gtk_tooltip_start_delay outside browse mode);
//   * BROWSE_TIMEOUT 60 — kTooltipBrowseTimeoutMs: the wait of an arrival
//     WHILE THE CHAIN HOLDS (GTK's browse mode, entered at a show);
//   * BROWSE_DISABLE_TIMEOUT 500 — kTooltipBrowseDisableTimeoutMs: the
//     chain, armed at a standing box's leave, lapses this long after that
//     leave with no new box shown, TIME ALONE (gtk_tooltip_hide_tooltip
//     arming tooltip_browse_mode_expired).
// The model is stated once, at AppState::RedesignTooltip; the wait's writer
// (note_tooltip_hover) and the clock (tick_tooltip) read these and nothing
// else.
inline constexpr int64_t kTooltipHoverTimeoutMs         = 500;
inline constexpr int64_t kTooltipBrowseTimeoutMs        = 60;
inline constexpr int64_t kTooltipBrowseDisableTimeoutMs = 500;

// THE KEY'S COLUMN — THE TOOLTIP'S HOVER TARGET (architect 2026-11-07:
// "always show"; the model at AppState::RedesignTooltip): THE
// KEY'S BOX, border included, LESS THE SEAM its linked neighbor on the left
// owns (`seam`, materials.h's linked_key_seam_px) — exactly the hit box
// (2026-11-19: Arc's linked buttons share one seam column and leave no gap
// inside a group, so a group's columns TILE with no hole and no overlap).
// The ground between two groups and the pads above and below the
// keys is no column. A box that does not stand (a zero rect) has no column.
// Read by the two hover walks (recompute_redesign_button_hover,
// update_modal_dialog_hover); the presses ask point_on_key (materials.h),
// the same box.
inline GuiRect tooltip_column_rect(const GuiRect& box, int seam) {
    if (box.w <= 0 || box.h <= 0) return GuiRect{0, 0, 0, 0};
    const int s = std::clamp(seam, 0, box.w);
    return GuiRect{box.x + s, box.y, box.w - s, box.h};
}

// THE HOVER SLOP (architect 2026-09-29) — ITS ONE READER IS THE LEVEL
// CASE'S PRESS (update_level_press_motion,
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
// ("round each up": every authored number a whole W). The
// one-device-px consequence: 10 device px on the tablet at its 200 % under
// the 320 density it runs at, 5dp — Android's 4dp to within one
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
// whole W (his "round each up", 2026-11-05); floor 1, so a small scale never zeroes it. The drag is RELATIVE on
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

// (THE FACES' FADES HAVE NO LENGTH HERE: the hover and every state's fade
// are Arc's (architect 2026-11-18 / 2026-11-20) — the look
// materials.h's THE KEY and THE HOVER; the durations, the curve, the clock
// and the damage AppState::FaceFades, app_state.h; the pen latch that keeps
// a lifting pen from blinking a face GuiInputHandler::pen_hot_latch_.)

// Screen-coord rects of one rendered cue, keyed back to its marker index,
// emitted left to right. It is the PAINTED EXTENT — the label's box and the
// triangle (below) — because this stash is what the pixels show. IT IS
// CLIPPED TO THE WAVEFORM'S COLUMNS [0, w) as the pixels are
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
// whose MarkerCell answer the marker press reads for the addressed cell, the
// value drag's subject and the double-tap seed.
//
// TWO PRODUCERS (2026-09-05; the `h` view's diff lane stands in for the flag
// pass while it stands), and the second is why this shape is a struct
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
// A CUE IS TWO RECTS (architect 2026-10-09: "the flag's
// press target becomes the union of the triangle and the label's box as
// painted"): `x, y, w, h` is THE LABEL'S BOX — its occluding box, the
// extent it hides earlier labels across, on THE WHOLE LANE'S ROWS
// (2026-10-09 ~21:00: a press anywhere in the lane's height over the
// label's columns is the label's — "right now it's too small to click";
// cue_fill_h_px), from the chip's left edge (the cue's exact place less
// the chip's hit extent) to one fill pad past its last segment's text —
// and `tri_*` THE CHIP'S, its HIT extent round its exact place, 2 · H − 1
// device px (cue_chip_hit_left_px, cue_chip_hit_right_px; H
// cue_chip_h_px) over its rows. EVERY x HERE IS REAL (architect
// 2026-11-15, the no-grid arc: a hit box stands on its mark's real edges):
// the cue's exact place plus real offsets, the very edges the painter cut
// and laid the label on.
// Either may be empty (a cue with no label segment, a field
// standing in for the label, the
// riding cells' run, which carries no triangle). The containment owner is
// flag_hit_rect_contains below; the two boundaries are the label's
// segments' box edges.
//
// THE `h` VIEW'S RECTS are one per item (architect 2026-11-11: one item,
// one cue), `marker_index` the index into history_mode.flags.
//
// A CUE'S IDENTITY ACROSS REBUILDS OF THE LANE — THE KEY A HOVERED MARKER'S
// FADE IS RECORDED UNDER (architect 2026-11-21: the lanes' hovers;
// AppState::FaceFades' THE MARKER'S FACE, the rule). The marker stores
// give a marker no stable id — a marker is its row in a frame-sorted
// vector, and an insert or a delete anywhere before it renumbers it — so a
// cue is named by WHAT ITS ROW SAYS AND NO NEIGHBOR'S DOES: its AUTHORED
// FRAME (the store's time_frame, a source frame in either view, never moved
// by a pan, a zoom, the target view's map or a live drag's overlay, which
// paints a dragged marker elsewhere and writes its frame at the commit) and
// its RANK AMONG THE CUES OF ITS LISTING ON THAT FRAME (a stack's members
// are told apart by their store order), in its LISTING — the lane's
// content's own name (marker_lane_listing, app_state.h): the live columns
// W and P each one listing, every commit the `h` view shows another, so a
// key never names a cue of another column or another commit. TWO CUES OF
// ONE PASS NEVER SHARE A KEY. An edit elsewhere — a marker added, deleted
// or moved at another frame, a label renamed, a bit flipped — leaves the
// key standing; the marker's own move re-keys it (a new face: the old
// record names no cue any more and is dropped).
struct CueKey {
    uint64_t listing = 0;
    int64_t  frame   = 0;
    int      ordinal = 0;
    bool operator==(const CueKey&) const = default;
};
// THE LANE'S HOVER AS A PASS READS IT (AppState::FaceFades' THE MARKER'S
// FACE): the listing the pass's cues are keyed in, and each cue's HOVER
// WEIGHT — the Hover look's weight in its face's stored mix, 0 at rest, 1
// hovered, between them while it fades — answered by the one reader of the
// faces' records (GuiPaintHandler::cue_hover_of), never a clock. An empty
// `weight_of` paints every cue at rest.
struct CueLaneHover {
    uint64_t                              listing = 0;
    std::function<double(const CueKey&)> weight_of;
};
struct FlagHitRect {
    int    marker_index = -1;
    // THE CUE'S IDENTITY ACROSS REBUILDS (CueKey above): the hover's walk
    // takes a hovered marker's face from it, never from `marker_index`,
    // which an edit elsewhere in the store renumbers. The riding cells' run
    // carries none (no hover reaches it: the editor's veil).
    CueKey key{};
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
    // THE LABEL'S TEXT ORIGIN (architect 2026-11-09: "the first
    // char on the left of the tooltip should align horizontally with the
    // first char of the marker"): the EXACT pen x of the cue pass's FIRST
    // painted segment's run (paint_cues, render.cpp: the segment's box start
    // plus the fill pad — the cue's exact place plus the label's lead),
    // unclipped: the very pen the label's ink is laid at, so the tooltip's
    // text stands on the label's (architect 2026-11-15, the no-grid arc).
    // `has_text_x` false where no
    // segment paints or the label's box lies off the waveform's columns, and
    // on the riding cells' run. ONE READER, the cue tooltip's seat
    // (marker_tooltip_box_rect, app_state.h); no hit reads it.
    bool   has_text_x   = false;
    double text_x       = 0.0;
};
// THE ONE CONTAINMENT TEST of a published cue (the label's box or its
// triangle), half-open on both axes like rect_contains, (x, y) THE
// POINTER'S REAL PLACE (architect 2026-11-15, the no-grid arc): a real
// point against the box's real edges, no pixel between them.
inline bool flag_hit_rect_contains(const FlagHitRect& r, double x, double y) {
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) ||
           (x >= r.tri_x && x < r.tri_x + r.tri_w && y >= r.tri_y &&
            y < r.tri_y + r.tri_h);
}

// All rendering helpers take a Cairo context and pixel-space rectangles; they
// have no event-loop dependencies.

// The two ground fills, one per surface class: render_background erases
// CHROME in the body tone `body_face`, render_canvas erases the WAVEFORM AREA in the
// `waveform_canvas` role (palette()). on_redraw calls the first
// over the whole exposed rect, then the second over the exposed part of the
// waveform area, so the canvas wins exactly the pixels the plate, the ground
// recolours, the playheads and the marker stems paint on — cold frames (no
// plate yet) included.
//
// render_canvas ALSO owns THE CANVAS'S LINES UNDER THE WAVEFORM (architect
// 2026-10-09; THE WAVEFORM block's canvas paragraph owns the rule): after
// its fill, each
// channel's horizontal grid, the channels' bands the plate's own
// (waveform_channel_band) — no vertical grid. render_waveform_divider owns
// THE STEREO DIVIDER'S ROW across the area (architect 2026-10-16,
// waveform_divider: chrome on the canvas, through the sink), a pass AFTER
// THE PLATE and the center lines (its
// reason at its definition, render.cpp: a plate from before a resize stands
// until the worker republishes). render_center_lines owns THE
// CENTER LINES, OVER THE WAVEFORM (architect 2026-10-09 evening): each
// channel's exact zero, one W in `center`, painted live after the
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

// One cell rect in `c` — a surface: the ground, a lane, a field, a frame's
// line (set_palette_source; every surface is flat, materials.h).
void paint_cell_rect(cairo_t* cr, const GuiRect& r, GuiColor c);
// One cell rect of INK in `c` (set_palette_source): a key's face, a glyph's
// or a word's own cells. The same fill as paint_cell_rect, its path built
// after the source.
void paint_ink_rect(cairo_t* cr, const GuiRect& r, GuiColor c);
// THE EXACT CHROME FILL (architect 2026-11-15, the no-grid arc; the rule at
// hull, above): the same two fills on a GuiRectF — one solid rect on its
// REAL edges, every pixel at the coverage the rect has of it, on the
// chrome's context. The coverage is cairo's own gray antialiasing (the
// fill's rectangular scan converter: the rect's edges on cairo's 24.8 fixed
// point, 1/256 px, and the coverage and the blend in cairo's 8-bit
// arithmetic — measured on the host, a pixel within 2 levels of the
// analytic coverage's blend), which the painter sets for its fill and puts
// back, so a caller's CAIRO_ANTIALIAS_NONE never rounds it. A RECT WITH
// WHOLE EDGES
// WRITES THE INT ROAD'S BYTES: cairo turns a rectilinear path into boxes
// first and fills pixel-aligned boxes directly, the same boxes under either
// antialias mode (cairo 1.18.4, _cairo_spans_compositor_fill →
// clip_and_composite_boxes → composite_aligned_boxes), so a whole box may
// take either road. EVERY PARTLY COVERED PIXEL IS A BLEND OVER WHAT STANDS
// UNDER IT: the caller paints the surface under the rect first, inside the
// same clip, and damages the rect's hull. THE RECT LIES INSIDE ONE CLIP
// RECTANGLE — the frame's damage rect (the platform's paint loop clips each
// on its own), whatever rect a painter intersects with it, or one rect of
// on_redraw's strip-and-waveform pair: a rect that STRADDLES two rectangles
// of a clip region would put cairo on its polygon road, whose rows are 15
// sub-rows (an edge pixel about ten levels off the coverage, 12 measured
// on the host), so an exact fill is never painted across the rectangles of
// a multi-rect clip.
void paint_cell_rect(cairo_t* cr, const GuiRectF& r, GuiColor c);
void paint_ink_rect(cairo_t* cr, const GuiRectF& r, GuiColor c);
// THE STRIKE'S BAR OVER ONE STRUCK RUN — THE ONE GEOMETRY (architect
// 2026-10-26 ~15:10: "just the strike through on the text"; ~15:30, "the
// same thickness as the regular — for example, a T would have the same
// thickness"): the run's own face's stroke on that face's own strikeout
// band (gui_font_strike_band, gui_font.h) — [baseline + dy, + h) — across
// the run's whole advance, [x, x + width_px): AN EXACT RECT (architect
// 2026-11-15, the no-grid arc), the run's own pen and baseline taken as
// they are and the face's band unrounded, so the bar stands on the text it
// strikes wherever that text stands. `font` is the run's face, `x` its pen,
// `baseline` its baseline. The caller paints it at exact coverage in the
// run's own ink. TWO READERS: the cue labels' text painter (show_cue_label,
// render.cpp — the live lane and the `h` view's lane — through the coverage
// stamp, cut where the label is) and the `h` view's cue tooltip (the exact
// fill; paint_shift_tooltip, paint_handler.cpp; architect 2026-11-11:
// "draw the strike as the lane's label strike is drawn").
GuiRectF struck_run_bar(const GuiFont& font, double x, double width_px,
                        double baseline);

// THE DIRECT ROAD (2026-10-13): the context's own target as pixel words, and
// the clip it holds in device pixels, for a painter that writes the words
// cairo would leave instead of asking cairo for them — the coverage stamp
// (CoverageStampPass, one target per pass, cool_edit_paint.h), stating at its
// site what its words are. Taken only where such a write can be cairo's
// result: the target an ARGB32 image surface with no group open on it, no
// device offset and unit device scale; the operator OVER; the matrix a pure
// integral translation (the platform hands on_redraw an untranslated
// context, platform_android.cpp's paint loop; the flag cache's by its
// band's origin row, upward — FlagCache's head, paint_handler.h); the clip
// a list of integral rectangles (cairo_copy_clip_rectangle_list, in user space, so the
// translation carries them to device pixels; each is intersected with the
// surface). Anything else answers false and the caller takes cairo's road.
// The surface is flushed here; the caller marks it dirty after its last
// write, and around any cairo drawing it lets fall between its writes marks
// before and flushes after (CoverageStampPass lets none fall: a pass whose
// target is refused paints every shape through cairo).
struct DirectPixelTarget {
    unsigned char* data = nullptr;
    int stride = 0;
    int dx = 0, dy = 0;               // user -> device: + (dx, dy)
    std::vector<GuiRect> clip;        // device pixels, inside the surface
    cairo_surface_t* surface = nullptr;
};
bool direct_pixel_target(cairo_t* cr, DirectPixelTarget& out);


// THE WAVEFORM AREA IS THE CANVAS: the plate, the canvas's lines, the
// lead-in ring and the zoom anchor's stem each span its columns (THE
// WAVEFORM block's canvas paragraph), and every vertical line on it spans its whole height,
// the stereo divider's row included.
//
// THE STEREO DIVIDER (architect 2026-10-16, Cool Edit's grammar: a stereo
// file is two wells, one per channel, with a seam between them — "make sure
// it has no separation from the main chrome"): THE FLAT WELL'S SEAM IS ONE
// LINE OF `well_border`, THE WELL'S LINES' TONE (architect 2026-11-17: the
// well's lines and the stereo divider in one tone; its line of
// kArcDarkTones, gui_theme.h), ONE W (hairline_px, the line rule of
// 2026-11-04), across the waveform area, the panel's whole width
// (2026-11-19). CHROME, NOT WAVEFORM:
// inert to every input (point_on_waveform_divider below). THE ROW STANDS
// INSIDE THE WAVEFORM AREA (every column mapping, hit test and damage box
// reads it) and comes out of the two channels' rows: the area's
// height less the divider, halved and floored, is each channel's band
// (below). Across the area's columns it is painted through the canvas sink
// (render_waveform_divider, after the plate and the center lines,
// build_canvas_list's order), so the GL canvas draws it from its list
// (canvas_sink.h's head).
// THE PLATE'S BANDS, THE GRID AND THE CENTER LINES stand inside the two
// bands and paint nothing on it; EVERY VERTICAL LINE CROSSES IT, THE DIVIDER
// BEING JUST ANOTHER GRID LINE (architect 2026-11-02: "the divider is just
// another grid line now") — the selection stems, the cursor, the lead-in
// ring's sides, the scanner and the zoom anchor's
// stem each one solid line down the area's whole height, painted after the
// divider (build_canvas_list's order). On the tablet at 200 % and
// the key 28 W tall the canvas is 1072 rows,
// 236-1307 (main.cpp's lane table, the icon rows' pads 7 W each side,
// architect 2026-11-20): the upper well's band 535 rows
// (236-770), the divider rows 771-772 (one W), the lower well's band 535
// rows (773-1307) and NO SPARE ROW (1072 less the divider's 2 is even; an
// odd remainder would be the canvas's last row, ONE CHANNEL'S BAND below).
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
// (render_canvas), so the lines stand on the zero the plate's bars
// straddle. EACH CHANNEL'S BAND IS ITS WELL'S INTERIOR (architect
// 2026-10-16, the stereo divider above): (area_h − divider_px) / 2 rows,
// floored, EXACTLY EQUAL — channel 0 [0, h) under the canvas's top frame
// row, channel 1 [h + divider_px, 2h + divider_px) under the divider. AN ODD
// REMAINDER IS THE CANVAS'S LAST ROW, inside the lower well under channel 1,
// where the canvas's ground paints and no ink reaches (2026-10-09: the two
// bands stay equal, so the plate's layout is one height). ITS ZERO IS
// y + h / 2.0, EXACT (architect 2026-11-14) — render_waveform_columns'
// centre y + h · 0.5, a real row: the middle of a row where h is odd (the
// tablet's 535), a row boundary where it is even; its HALF
// HEIGHT H = h / 2.0, the grid's (THE WAVEFORM block's canvas paragraph),
// runs from the
// zero to the band's edge.
// h is 0 only on a canvas under the divider and two rows.
struct WaveformChannelBand {
    int y = 0;
    int h = 0;
    double zero() const { return y + h / 2.0; }
};
inline WaveformChannelBand waveform_channel_band(int area_h, int divider_px,
                                                 int channel) {
    const int ch_h = waveform_channel_band_h(area_h, divider_px);
    return WaveformChannelBand{channel == 0 ? 0 : ch_h + divider_px, ch_h};
}

// A CANVAS ROW-LINE AT ITS EXACT ROWS — THE HORIZONTALS' ONE ROW RULE
// (architect 2026-11-14: horizontals under the one rule with the
// verticals, "a split that may not be needed"; "I would expect snapping
// nothing to integers and leaving them rounding to yield decent results"):
// a line `t` rows thick (one W, waveform_line_px) CENTERED ON THE REAL ROW
// `yc` (window y) is the real interval [yc − t / 2, yc + t / 2], CUT TO THE
// AREA'S ROWS [area.y, area.y + area.h] as a real interval, painted across
// the area's whole columns at each row's exact coverage (the sink's blended
// rect, canvas_sink.h). The grid's and the center lines' (render_canvas,
// render_center_lines, render.cpp). ON THE TABLET'S ODD BAND the zero is
// the middle of a row, so a center line is half a row, a whole row and half
// a row — truly centered on the plate's zero; on a band of even height it
// is the two whole rows either side of the zero's row boundary.
struct CanvasRowSpan {
    double y0 = 0.0;
    double y1 = 0.0;
    bool empty() const { return y1 <= y0; }
};
inline CanvasRowSpan canvas_hline_span(const GuiRect& area, double yc,
                                       int t = waveform_line_px()) {
    const double half = static_cast<double>(t) / 2.0;
    return CanvasRowSpan{
        std::max(yc - half, static_cast<double>(area.y)),
        std::min(yc + half, static_cast<double>(area.y + area.h))};
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
// answers exactly as the well's other chrome does: nothing
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

// THE WAVEFORM'S HALF SPLIT, and the ONE expression of it (architect
// 2026-08-13, THE TWO HALVES ARE ONE SURFACE): the half selects no SURFACE,
// it selects which act a MOTIONLESS release runs on the one navigation
// surface of the main page — upper = the playhead placement, lower = the
// audition scrub, which places no playhead. Everything else about the two
// halves is identical. TWO READERS: the live press (on_button_press,
// input_pointer.cpp), which reads it once and stashes it on the pending
// (ScrollDragState::scrub_release); and the hover line's triangle
// (GuiPaintHandler::hover_triangle_x, architect 2026-11-24), which stands
// nowhere over the lower half, the tap there placing no playhead. The `h`
// view has no halves (its press, handle_history_mode_press, takes the
// placement on the whole waveform), so neither reader asks this there.
//
// THE HALVES ARE THE TWO WELLS (architect 2026-10-16, Cool Edit's stereo
// divider): the upper well's rows are the upper half, the lower well's —
// the odd spare row at the canvas's foot among them — the lower half, the
// rows read off waveform_well_rows above. The divider between them is
// chrome and owns nothing: a press there never reaches this split
// (point_on_nav_surface refuses it, point_on_waveform_divider). Window row
// `y`; a row above the lower well (the upper well, the divider, the lanes
// over the canvas) answers false.
inline bool waveform_lower_half(const GuiRect& area, int y) {
    return y >= waveform_well_rows(area)[1].y0;
}

// THE COLUMN MAPPING BASIS — a lattice: its start, its ORIGIN, its step and
// its column count, the arithmetic render_waveform_columns cuts its columns
// by.
//
// Columns are mapped on A LATTICE, not by interpolating between integer
// endpoints: the renderer recovers the start's lattice index and maps global
// column c to the display-domain edge
//     edge(c) = origin + nearbyint((k0 + c) * spp),
//     k0 = nearbyint((vp_start − origin) / spp)
// — the expression the viewport's own snap uses to produce a rest
// (nearest_viewport_grid_point, warp_frame_map_view.h), so a start that lies
// on the lattice round-trips exactly.
//
// THE PLATE'S COLUMNS ARE ITS BARS (architect 2026-11-14): the one
// caller hands THE BAR BASIS (waveform_bar_basis below) — THE FRAME-0
// LATTICE, origin 0, of step b, the bars' width (waveform_bar_frames), its
// start the first stored bar's left edge and its count the stored bars' — so
// a column's frames are a function of the bar's GLOBAL index and b alone,
// never of the viewport that asked for it. THE DISPLAYED VIEWPORT'S OWN
// LATTICE — the camera's start, its origin and the painter's q
// (AppState::viewport_lattice_origin owns its two rules: every landing and
// every zoom frame rests at the start it asks for, the origin derived from
// that start, and a pan keeps the origin it stands on) — is the one
// the stems, the flags, the playhead and every hit test stand on, and the
// one a pan steps by: whole columns of it, one device px each, as Ableton
// pans by whole columns. The plate registers with it through its draw
// transform (WaveformBarGeometry below), which that step moves by the same
// whole px, never through this basis.
//
// WHY IT MUST BE A LATTICE OF GLOBAL INDICES (the shimmer fix). Interpolating
// edges as vp_start + span*c/W from integer endpoints gives each column a
// rounding residual that CHANGES when the viewport moves: nearbyint ties
// flip, the frames a column reads flip with them and its extrema jump — two
// pictures one pan pixel apart differ in most of their columns. On the
// lattice column k0 + c is the same double whatever window drew it,
// therefore the same nearbyint, the same frames and the same extrema, in
// both views — in target view the lattice lives in the display domain and
// the map consumes the same doubles. So a pan, and a zoom inside one level,
// re-read every bar they keep at the very tips it had.
struct WaveformBasis {
    long long vp_start   = 0;   // the lattice's start, display domain: column 0's left edge
    long long origin     = 0;   // the lattice's origin (whole frames): 0 for the plate's bars
    double    spp        = 0.0; // the lattice's step in frames: the plate's bar width b
    int       full_width = 0;   // the call's column count: the plate's stored bars
};

// THE PLATE'S BARS (architect 2026-11-14: Ableton's model as far as
// possible — the waveform's bars ANCHORED TO THE AUDIO and STRETCHED between
// levels, nothing re-cut inside a level, both lamps; no snap of any kind, the
// placement continuous from the displayed viewport at every zoom). THE ONE
// OWNER of the bars' geometry: the rung, the stored range, the draw
// transform and THE FADE BAND before each level switch
// (waveform_fade_band below); the producer (render_waveform_columns) cuts and
// reads what this names, and the GPU draws it stretched
// (kListFragmentShader, gl_canvas.cpp).
//
// THE LADDER: bar widths in display-domain frames b_m = column × 2^m, column
// the audio's working column (GuiAudio::working_column) and m any integer
// >= −1 — b_−1 half the column, the deepest level's step.
//
// THE RUNG for a painter step q (frames per device px,
// painter_samples_per_pixel's exact value): THE SMALLEST b OF THE LADDER
// WITH b >= q. THE SWITCH WIDTH IS A FIXED 2 DEVICE PX (kWaveformSwitchPx;
// architect 2026-11-14): the unique m with 1 <= b_m / q < 2, so a
// bar is drawn p = b / q device px wide, p in [1, 2). The boundary b / q ==
// 1 belongs to that (finer) rung, and b / q == 2 never occurs — the compare
// exact in doubles (b a multiple of a half frame, q of a sixteenth). At the
// working zoom (q = column) b = column and p = 1: one bar a device px. Never
// finer than m = −1: the zoom's own floor (q >= half the column,
// effective_min_zoom_level) keeps the rule's m there, and a q under it takes
// b_−1 with p past 2, no error arm (waveform_bar_frames is total; 0 for a
// degenerate q or column).
// WHY 2: the working zoom must draw one bar a device px — its column is the
// data's resolution there, the 2.4 s rule — so 1 has to lie in the bars'
// range of widths, and with the switch at 2 no bar of a rung is ever
// narrower than a px and every whole level stands exactly at a switch (a
// hair out from the working zoom the bars are the next rung's, 2 px wide).
// Live's own range, about 1.25 .. 2.5 px, would draw the working zoom with
// 2-px bars. THE SWITCH IS AT ITS FIXED THRESHOLD, no dead band (his
// "unnecessary and probably more distracting than helpful"); what softens
// it is the fade band, a function of the zoom alone.
// DETERMINISM HAS ONE SEAT, AND IT IS THE VIEWPORT (architect 2026-11-15,
// the no-grid arc, on the landings: "I'm not concerned with softness of the
// waveform or any other element ... And I'm no longer concerned with the
// determinism or precision"; his criterion, "So long as C on a given stem
// always gives me a specific view"): THE SAME WHOLE VIEWPORT — START, ORIGIN
// AND STEP — IS THE SAME PICTURE, EVERYWHERE, and the same act on the same
// state rests at the same viewport (a landing's start is its arithmetic's,
// its origin that start's own: AppState::viewport_lattice_origin). So `c` on
// a stem is one picture, every time. WHAT A WHOLE LEVEL (q = b, the pitch
// exactly 1) LOOKS LIKE THERE: where the origin is 0 — a rest at a wall, or
// a start a whole number of steps from frame 0 — the offset is whole and the
// bars are one device px on the pixel grid, the one-bar-a-pixel plate byte
// for byte, with no special case in this code; at any other landing on a
// whole level the bars are one device px wide at an offset of
// −frac(origin / q) px, each straddling two pixels, soft, the same every
// time for that viewport — accepted by him, his words above. A zoom reached
// by a
// gesture rests the same way on its own origin, and, where it rests inside
// the fade band, the two levels' pictures mixed, the same every time for
// that viewport. THE
// DRAW'S ARITHMETIC IS THE QUOTIENT (architect 2026-11-15: the
// per-pixel divide back with the blend's shader work, "I agree to both
// your points there"): the shader places each screen subcolumn among the
// bars by one divide by the row's pitch — on a whole level the pitch is
// exactly 1 and the place the difference itself, and at a whole offset the
// picture is byte for byte the one-bar-a-pixel plate (kListFragmentShader's
// WHAT IS PROMISED, gl_canvas.cpp).
//
// BAR k (a GLOBAL index, k >= 0) is the display-domain span [k · b,
// (k + 1) · b) ON THE FRAME-0 LATTICE: its integer edges are the producer's
// own edge(k) = nearbyint(k · b) under the bar basis (waveform_bar_basis:
// the start the lattice point nearbyint(k_first · b) — b is a half-integer
// only at m = −1 of an odd column — the origin 0, the step b, the count n;
// the producer's index recovery nearbyint(start / b) answers k_first exactly
// for every b of a frame or more). EVERY BAR IS QUERIED OVER ITS WHOLE SPAN,
// never clipped by the viewport, so its tips (lit, the hull of its working
// columns' magnified samples, each column at its own scales: THE EXACT FORM
// at render_waveform_columns) are a function of (k, b, the map, the lamp)
// alone: inside a level nothing statistical changes from frame to frame. Target view the
// same, in the display (target) domain, each bar mapped to its source span
// as a column is.
//
// THE DRAW TRANSFORM (doubles; NO ROUNDING OF THE PLACEMENT at any zoom, no
// re-seat at whole levels). The displayed viewport {vp_start, origin, q}
// gives continuous column coordinates col(f) = (f − origin) / q − K0, K0 =
// nearbyint((vp_start − origin) / q) — the viewport's own lattice index
// (viewport_lattice_index, warp_frame_map_view.h, the stems' and the flags'
// recovery), so pixel column c's left edge is col = c and the plate
// registers with every mark on that lattice. Texel j of a band's row is
// global bar k_first + j, its centre X_j = x0 + j · p device px FROM THE
// PLATE'S LEFT EDGE, x0 = col((k_first + 0.5) · b) and p = b / q: x0's
// numerator (k_first + 0.5) · b − origin − K0 · q is exact in doubles (every
// term a multiple of a sixteenth of a frame far inside the mantissa), one
// divide rounds it, and it is small — the halo's few bars — so the float the
// shader takes loses nothing that shows. ON A WHOLE LEVEL AT ORIGIN 0
// (q = b: a wall, or a start a whole number of steps from frame 0)
// X_j = (k_first + j − K0) + 0.5 and p = 1: bar K0 + c
// sits exactly on pixel c; at another origin it sits −origin / q px off it.
//
// THE STORED RANGE [k_first, k_first + n) IS THE VIEWPORT'S BARS AND A HALO
// BOTH SIDES: every bar the draw can read at ANY grow of the `waveform_grow`
// key — kWaveformBarHaloSubcells, the key's top (render.cpp's static_assert
// against kWaveformGrowMaxQuarters), never the live grow, so the payload
// does not depend on it. A pixel's coverage reads the screen subcolumns
// within the grow of its own four, so across the plate's device width W the
// subcolumn centres run from −(halo − 0.5) / 4 to W + (halo − 0.5) / 4 device
// px (1.875 px each side at the top grow), and each reads THE TWO BARS WHOSE
// CENTRES BRACKET IT (the contour between two bar centres; a bar's own
// one-px core reaches no further, p > 1/2): k_first is the lower bracket of
// the leftmost centre, the last bar the upper bracket of the rightmost. At a
// whole level that is three bars beyond each edge, n = W + 6; n <= (W +
// 3.75) / p + 3 always, and p >= 1 for the main rung (the fade band's fine
// row is this same rule at the finer rung, inside this range in halves at
// half the pitch, over 1/2 px — up to 2 · (W + 6) bars just past a
// landing: waveform_fade_band below).
// THE TWO ENDS. LEFT OF FRAME 0 NO BAR EXISTS: k_first clamps at 0, and the
// draw finds no texel there (the plate's left edge at the song's start is
// the bar's own flat shape, as it was). PAST THE DOMAIN'S END THE BARS ARE
// STORED AND SILENT, the producer's standing end policy: a bar straddling
// the end reads the frames inside it, a bar wholly past it reads nothing
// (GuiAudio::get_peak_range clamps to the source) and is the never-fade
// floor's one subrow at the zero row — the hairline a viewport wider than
// the song has always shown past its last frame — so the range needs no
// domain total and the helper takes none.
// Pure and total: a degenerate q, b or width answers the empty geometry
// (n = 0), which the producer refuses and the plate leaves transparent.
inline constexpr int kWaveformBarHaloSubcells = 8;
inline constexpr int kWaveformSwitchPx = 2;
inline double waveform_bar_frames(double q, int64_t column) {
    if (!(q > 0.0) || column <= 0) return 0.0;
    double b = static_cast<double>(column) * 0.5;
    const double want = static_cast<double>(kWaveformSwitchPx) * q;
    while (2.0 * b < want) b *= 2.0;
    return b;
}
struct WaveformBarGeometry {
    double    b       = 0.0;   // the bars' width, display-domain frames
    long long k_first = 0;     // the first stored bar's global index
    int       n       = 0;     // the stored bars' count (a band's row)
    double    x0      = 0.0;   // the first stored bar's centre, device px from the plate's left edge
    double    p       = 0.0;   // device px per bar, b / q
};
inline WaveformBarGeometry waveform_bar_geometry(long long vp_start,
                                                 long long origin, double q,
                                                 int area_w, double b) {
    WaveformBarGeometry g;
    if (!(q > 0.0) || !(b > 0.0) || area_w <= 0) return g;
    const double o  = static_cast<double>(origin);
    const double k0 = std::nearbyint(
        (static_cast<double>(vp_start) - o) / q);
    // The bar-centre coordinate of the point x device px from the plate's
    // left edge: a whole k at bar k's centre, so its floor is the lower of
    // the two bars whose centres bracket x.
    const auto centre_index = [&](double x) {
        return (o + (k0 + x) * q) / b - 0.5;
    };
    const double reach =
        (static_cast<double>(kWaveformBarHaloSubcells) - 0.5) /
        static_cast<double>(kWaveformSubcells);
    double first = std::floor(centre_index(-reach));
    const double last =
        std::floor(centre_index(static_cast<double>(area_w) + reach)) + 1.0;
    if (first < 0.0) first = 0.0;
    if (last < first) return g;
    g.b       = b;
    g.k_first = static_cast<long long>(first);
    g.n       = static_cast<int>(last - first) + 1;
    g.x0      = ((first + 0.5) * b - o - k0 * q) / q;
    g.p       = b / q;
    return g;
}
// THE BAR BASIS the producer is handed (the rule above): the frame-0
// lattice of step b from the first stored bar's left edge, n columns.
inline WaveformBasis waveform_bar_basis(const WaveformBarGeometry& g) {
    return WaveformBasis{
        static_cast<long long>(
            std::nearbyint(static_cast<double>(g.k_first) * g.b)),
        0, g.b, g.n};
}

// THE PLATE'S BARS, THE FADE BAND — a section of the one owner above
// (architect 2026-11-14: THE FADE AT THE LEVEL SWITCH,
// ONE PASS — on his glass a bare switch read as a pop at 90 Hz, "the
// waveform sort of seems like it coalesces into that little mountain that in
// Ableton I see fade and grow in and out gradually"; THE FADE OVER
// THE WHOLE DOUBLING — on his glass a band starting at 1.5 px, "if
// I'm zooming in and out slowly, I guess I feel the steps, the fades.
// Wouldn't it be better to have a continuous fade motion", an eased curve
// over that shorter band declined: "generally cheap partials don't work
// very well, and instead they get in the way of the true solution"; its
// cost inside the one beat on the 90 Hz panel, two refreshes, 22.2 ms
// (architect 2026-11-16, flat 45; kFrameBeat, platform_android.cpp, its
// measurement there); architect
// 2026-11-15: THE TWO-PICTURE BLEND, "Agree to all.").
// Inside a band of zoom before each split — from the start width F up
// to the switch, at F = 1 the whole way from one whole level to the next —
// the plate holds BOTH RUNGS, EACH ITS OWN UNIFORM ROW, and the pixel is
// THE MIX OF THEIR TWO PICTURES by a weight that is a function of THE ZOOM
// ALONE — no time, no animation, nothing at the lift: hold still and the
// picture holds still, half-tone between the two levels. THE ONE OWNER of
// the band, the weight and the band's stored rows.
//
// THE MAIN RUNG B for the step q is the rung above (waveform_bar_frames), its
// pitch P = B / q in [1, 2). THE FINE RUNG b = B / 2 EXISTS iff B >= column
// (B == column / 2 is the ladder's floor, where no finer level stands).
// THE START F (device px; kWaveformFadeHundredths above, whole hundredths
// Fh, 100: F = 1) is THE BAR WIDTH AT WHICH THE FINER LEVEL STARTS TO
// APPEAR; 1 is ITS FLOOR, THE WHOLE DOUBLING (a rung's own bars are a px
// or more wide, so no band can start under it).
// THE PLATE IS IN THE BAND iff the fine rung exists AND P > F
// — exactly 100 · B > Fh · q, STRICT (architect 2026-11-14), the
// compare exact in doubles as the rung's is (B a multiple of a half frame,
// q of a sixteenth, Fh whole). AT P == F THE PLATE IS OUT OF THE BAND, at
// every F: rung B's uniform row there is the picture the blend would draw
// at weight 0 (A + (C − A) · 0 is A exactly), so the test moves which arm
// and which rows draw it and no pixel. WHY STRICT: at F = 1, P == 1 is A
// WHOLE LEVEL (q == B), and it stays the uniform one-bar-a-pixel row —
// one producer query and the cheaper arm, at rest, under playback and
// under FOLLOW's scroll, at whatever offset the viewport's origin gives it.
// So at F = 1 a whole level is the one zoom of each doubling off the band.
// OUT OF THE BAND the plate is rung B's uniform bars in every
// respect — the row, the transform and the bytes.
// THE WEIGHT, the finer picture's share: w = log2(P / F) / log2(2 / F),
// LINEAR IN THE ZOOM'S LOGARITHM (an even pinch, an even fade; no key),
// clamped at 0 from below (a rounding of the quotient), in [0, 1): 0 at
// the band's start, toward 1 as P reaches the switch at 2. The band spans
// log2(2 / F) of each doubling in the zoom's logarithm, its last part. AT
// F = 1 w = log2 P and a level's detail opens over THE WHOLE APPROACH TO
// IT: from the landing on the coarser level (q = B, its picture alone) the
// finer level fades in as the zoom goes in — half way at P = √2 — and
// stands alone at the switch (q = B / 2), past which the finer level is
// the main rung and the next finer one begins to fade in.
// THE FLOAT THE DRAW TAKES is wf = min(float(w), the largest float under 1),
// so IN THE BAND wf < 1 ALWAYS and OUT OF IT the draw is handed exactly 1 —
// wf == 1 is the whole test for a uniform row (GuiPlateColumns::bar_weight).
//
// THE BAND'S STORED ROWS (render_waveform_to_plate_columns builds them):
// TWO UNIFORM ROWS, EACH ITS OWN RUNG'S, UNMIXED. THE MAIN ROW is rung B's
// exactly as it is asked out of the band — the geometry above at b = B,
// {K_first, N, x0, P}, on its bar basis. THE FINE ROW IS RUNG b's OWN
// UNIFORM ROW: waveform_bar_geometry at b — the same call the main row
// makes — {k_f, n_f, x0_f, p}, its own x0 and p = P / 2 to the bit (b =
// B / 2, a division by two), on its own bar basis: exactly the row rung b
// is asked as the main rung, so past the switch the kept bars
// (WaveformKeptBars below) reuse it as it stands. IT LIES INSIDE THE MAIN
// RANGE IN HALVES, [2 · K_first, 2 · K_first + 2N) — the frame-0 lattices
// nest, [K · B, (K + 1) · B) = [2K · b, (2K + 2) · b): with y = (o + (k0 +
// x) · q) / B a point's place in rung B's bars, its lower bracket there is
// floor(y − 1/2) and on rung b floor(2y − 1/2), so the fine range's first
// bar is at least 2 · K_first and its last at most twice the main's last
// plus one — n_f <= 2N. The geometry's range rule (every subcolumn of the
// support reads the two bars whose centres bracket it) holds at every
// pitch over 1/2 px, so the fine row serves the draw as the main row does.
// TWO QUERIES OF THE ONE PRODUCER a band, one a row; a band of the plate is
// stored only where both came back whole. SO THE PRODUCER IS ASKED N + n_f
// BARS A FRAME IN THE BAND against N out of it — just past a landing N
// reaches W + 6 (W the plate's device width) and n_f about twice that —
// and at F = 1 every zoom between two whole levels is in the band
// (architect 2026-11-14; the frame's cost measured on the tablet).
// ON THE SYNCHRONOUS ROUTE THOSE TWO ROWS ARE KEPT (architect 2026-11-15;
// WaveformKeptBars below), each a row of the store, so a frame asks
// the producer for the bars that enter either row; the worker's route asks
// for both rows whole.
// THE PICTURE IS THE DRAW'S (kListFragmentShader's blend arm, gl_canvas.cpp's
// head): A the uniform picture of the main row and C that of the fine row —
// each per pixel a premultiplied color and alpha by the uniform rule at its
// own row's pitch, places and tips — and the pixel A + (C − A) · wf, then
// the GL blend, source-over. NO TIP IS MIXED AND NO BAR IS MOVED: the finer
// level stands at its true heights and places, the coarser over it at
// 1 − wf, the troughs at their own depth under the coarser level's veil.
// Premultiplied, so over the ground the result is the mix of the two
// finished pictures. THE TWO ENDS MEET, NO SECOND POP: toward the band's
// start wf → 0 and the pixel is A, the landing's own function of rung B's
// row; toward the switch wf → 1 and it is C, the uniform picture of rung b
// at a pitch toward 1 from below — and past the switch rung b is the main
// rung, its picture the same function of the same row at a pitch from 1
// above — so the picture is continuous in the zoom, both ends included.
// WHAT DOES NOT MOVE: the viewport, the lattice the marks stand on, the
// producer and every bar's own numbers — and A WHOLE LEVEL IS
// P = 1, OUT OF THE BAND, the one uniform row.
// Pure and total: a degenerate q or column answers out of the band or
// the weight 0, no error arm.
inline constexpr float kWaveformFadeWeightTop = 0x1.fffffep-1f;
struct WaveformFadeBand {
    bool   in_band = false;  // the plate holds both rungs' rows, their pictures mixed
    double w       = 1.0;    // the weight, [0, 1) in the band
    float  wf      = 1.0f;   // the float the draw takes: under 1 in the band, 1 out of it
};
inline WaveformFadeBand waveform_fade_band(double q, int64_t column) {
    WaveformFadeBand f;
    const double main_b = waveform_bar_frames(q, column);
    const double fh     = static_cast<double>(kWaveformFadeHundredths);
    const double sw     = static_cast<double>(kWaveformSwitchPx);
    if (!(main_b > 0.0)) return f;
    if (main_b < static_cast<double>(column)) return f;
    if (100.0 * main_b <= fh * q) return f;
    const double start = fh / 100.0;
    double w = std::log2((main_b / q) / start) / std::log2(sw / start);
    if (!(w > 0.0)) w = 0.0;
    f.in_band = true;
    f.w       = w;
    f.wf      = std::min(static_cast<float>(w), kWaveformFadeWeightTop);
    return f;
}


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
// The one caller (render_waveform_to_plate_columns, waveform_cache.cpp)
// passes col0 = 0 with area.w == basis.full_width at every call — once a
// channel band, and in the fade band twice, the main rung's row and the
// fine rung's (THE PLATE'S BARS' fade band), each a whole row of its own
// rung by this same contract; WITH THE KEPT BARS (architect 2026-11-15;
// WaveformKeptBars below) a call is instead the bars a kept row
// lacks, a contiguous sub-range of the row on its own bar basis, which
// writes for each of its bars what the whole row's call writes for it.
// ITS COLUMNS ARE THE PLATE'S BARS (architect 2026-11-14; THE
// PLATE'S BARS above owns the rule): the basis is the bar basis, `area.x` 0
// and `area.w` the row's count of bars — the viewport's bars and their halo,
// MORE OR FEWER THAN THE PLATE'S DEVICE COLUMNS — `plate_w` that same count,
// and the rows (`area.y`, `area.h`, `plate_h`) the plate's own device rows.
// A COLUMN, everywhere below, is one bar of that ladder: b display-domain
// frames on the frame-0 lattice, drawn p = b / q device px wide; "the outer
// bar" and "the inner bar" are a column's two vertical extents (THE VISUAL
// MAGNIFICATION).
// When `warp_frame_map` is null (source view) the basis viewport is
// source-frame and each column reads `audio.get_peak_range`
// directly. When non-null (target view) it is target-frame: each column's
// [t0, t1) is translated to source-frame via `map_target_to_source` before the
// pyramid read, producing the deformed-waveform display.
//
// THE SILHOUETTE IS ONE FILLED CONTOUR THROUGH THE COLUMN CENTERS, WITH A
// FULL-INK CORE (architect 2026-10-30, on his glass against Ableton), DRAWN
// STRETCHED AT THE COLUMNS' PITCH (architect 2026-11-14: the columns
// are anchored to the audio and their centres stand p device px apart, p in
// [1, 2) — in the fade band before a level switch the finer level's row
// beside it, p in (1/2, 1), drawn by this same rule and its picture mixed
// with the coarser's by the band's weight (the band over the whole
// doubling at F = 1, the two-picture blend 2026-11-15;
// waveform_fade_band) — placed continuously from the displayed
// viewport, the draw transform, THE PLATE'S BARS above). Each
// column reduces to two TIPS in float rows — its raw maximum as the top
// tip, its raw minimum as the bottom — QUANTIZED TO QUARTER ROWS (below):
// the column's own extent [T, B) in subrows is its BAR. THE CORE keeps that
// bar whole in full ink on A LINE ONE DEVICE PX WIDE AT THE COLUMN'S CENTRE,
// whatever the pitch: a lone spike stands
// flat-topped at its own height, never shaved toward its neighbors' mean,
// and is 1 + 2 · grow device px of ink at every zoom. THE CONTOUR joins the
// columns — at every SCREEN subcolumn (four a device px) the top and bottom
// interpolate linearly between the tips of the two columns whose centres
// stand either side of it, from center to center, so a steep rise is a
// diagonal between the two bars rather than a gap, and between two cores
// more than a px apart the shape is the contour alone. THE WHOLE SHAPE (the
// cores' union with the contour) IS GROWN BY THE `waveform_grow` KEY on
// every side — a disc dilation of that many quarters of a device px on the
// 4x grid (architect 2026-10-31; the key's reason at kWaveformSubcells: the
// plate is the one thing on the canvas not scaled with gui_scale, so the
// grow is its own device key for good), IN SCREEN SPACE, AFTER THE STRETCH:
// A CONSTANT SCREEN THICKNESS AT EVERY ZOOM (the grow is the
// waveform's proxy for the GUI scale, a property of the scale and not of a
// column, so no stroke widens across a level and none jumps at a switch) —
// and each device pixel takes EXACT COVERAGE, its set subcells over sixteen;
// the lit inner bar is the grown inner shape FILLED SOLID in the core (THE
// CORE, below), antialiased with its shape.
// ON A WHOLE LEVEL AT ORIGIN 0 (q = b: p = 1, every column
// exactly on a device px) the core is the column's own pixel and the contour
// reads the nearer neighbor in eighths: one column a device px, crisp, by
// the same arithmetic — no special case, and no re-seat onto the pixel grid
// anywhere else. Everywhere else — a landing at another origin included,
// architect 2026-11-15 — the columns' edges are soft, which is ruled
// fine (his "Ableton has no problem with soft lines"; a snap of any kind is
// not: "a visceral jarring impact").
// The coverage is blended SOURCE-OVER IN BYTE SPACE with what lies beneath
// (the GL blend, kListFragmentShader).
// NO TIP RULE (his words: "I don't look at the tips, the troughs and their
// crossings of the zero line"): a tip's pixel is its exact coverage like
// any other. The dark lamp's raw bar takes the same rule.
//
// THE ANTIALIASING (2026-10-30) IS exact coverage of
// one grown shape, computed on the GPU from the column bounds — the plate's
// own road; the lines of the canvas (the grid, the center lines, the stem,
// the playhead, the scanner, the anchor: one W, waveform_line_px) take
// their exact coverage at their exact places by a rule of their own
// (architect 2026-11-14; the line rule at waveform_line_px), the
// divider's row whole cells —
// the full-ink core keeps every column its own height.
//
// THE TIPS IN QUARTER ROWS, AND THE QUARTER-ROW NEVER-FADE FLOOR AT THE 4x
// GRID: each tip, clamped into the band's rows, becomes T = nearbyint(4 ·
// top) and B = nearbyint(4 · bottom), the bar the subrows [T, B − 1] (the
// center rule: subrow k's center k + 0.5 lies in [T, B] iff T <= k <= B − 1).
// A bar whose interval is under a quarter row (B == T — digital silence at
// the zero row is the archetype) TAKES ONE SUBROW (kWaveformFloorSubcells,
// a quarter device px: its reason at that constant), B = T + 1, T pulled
// down one where it sat on the band's last edge so the subrow lies inside
// the band; the disc dilation then makes a line of it — 1 + 2 · GROW
// subrows: at the default grow, 1 px, nine subrows, nonzero coverage over
// three device rows and two and a quarter device px of summed coverage (36
// of 16 subcells) at any tip phase (kWaveformFloorSubcells owns the
// arithmetic, and Ableton's measured line, which is the 0.5 px grow's).
// Nothing can fade out or vanish.
// The floor is needed because an interval under a quarter row holds no
// subrow center and would otherwise rasterize to nothing; the contour needs
// none, since it only ever joins two bars that exist.
//
// PAN AND ZOOM INVARIANCE: a column's tips are a pure function of its own
// (k0+c) span — the lattice of global indices (WaveformBasis above) is what
// makes that span depend on the column's index and width alone — so two
// renders of the same column agree exactly whatever viewport asked.
// INSIDE A LEVEL NOTHING STATISTICAL CHANGES (architect 2026-11-14):
// while the zoom moves within one rung every column's peaks, its working
// columns' scales and its quarter-row tips are the same numbers frame after
// frame, dark or lit, and only the placement — x0 and the pitch — moves (in
// the fade band both rungs' rows hold those numbers, and only the weight
// that mixes their two pictures moves, with the zoom alone). That
// is what the columns are anchored for: a plate cut one column a device px
// on the viewport's own lattice reads every column over new frames at each
// zoom frame and re-rolls its narrow valleys, which read as bubbling, the
// lit plate's the more for the leveler's gain. AT A LEVEL SWITCH (the fixed
// threshold, kWaveformSwitchPx; no dead band, his "unnecessary and probably
// more distracting than helpful") each column splits in two or two join:
// the children's extrema are their own (lit, each the hull of its own
// working columns' magnified samples, so the parent is their hull: THE
// EXACT FORM), and THE FADE BAND before the split (architect 2026-11-14;
// the two-picture blend 2026-11-15; waveform_fade_band) mixes the
// parents' picture into the children's as the zoom approaches it — over
// the whole doubling (the fade's start at 1 device px,
// kWaveformFadeHundredths). A pan moves the placement by
// whole device px, the stored range with it, and re-reads no column
// differently. SO A FRAME ASKS THE PRODUCER ONLY FOR THE BARS THAT ENTER
// (architect 2026-11-15): on the synchronous route the bars a
// render already holds are kept to the next (WaveformKeptBars below), the
// pure function's numbers needing no second reading; the worker's route
// asks for its whole rows. NOT PROMISED:
// pixel-phase variation stays (a soft edge crossing the pixel grid), so the
// shimmer has one cause fewer, not none.
// THE EDGES: THE STORED ROW CARRIES A HALO past the plate's two side edges
// (the stored range, THE PLATE'S BARS above), each edge column read over its
// WHOLE span, so every pixel of the plate reads real neighbors. Left of
// column 0 no column exists and the shape runs flat from the first; past the
// domain's end the columns exist and are silent (the never-fade floor's
// line, below).
//
// THE COLUMNS ARE THE PRODUCT (2026-10-13; the one road, 2026-10-30):
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
// the core over it (THE WAVEFORM block) — it
// is trim-agnostic: the trim bar spanning the window is the
// inside-the-window signal at rest (the
// sweep highlight's blend over the plate while a TRIM sweep's pointer is
// down the one exception, paint_sweep_highlight, 2026-11-12). Its
// alpha is the coverage: opaque inside the bars, fractional on the grown
// fringe, transparent in the gaps (2026-10-30).
// THE GAPS SHOW THE GROUND render_canvas lays under the plate: the plate
// carries no ground colour, so the canvas has that one painter and the cold
// frame before the first plate shows the same ground. The one record that
// recolours a plate pixel after the plate's is THE SWEEP HIGHLIGHT, the
// list's last, while a sweep's pointer is down (paint_sweep_highlight,
// architect 2026-11-12): a blend toward white over the gaps and the set
// pixels alike, the GL blend's work, so it needs nothing of the plate; no
// reason needs the gaps over an opaque canvas-coloured plate, and the plate
// stays as it is.
//
// THE VISUAL MAGNIFICATION is `gain_or_null`, and the lit plate is TWO BARS
// per column from the same reads, both through the expander, the outer
// through the leveler and the inner through the compressor (architect
// 2026-09-25):
//
//   OUTER (painted first) = raw x g x E, in the ink — the
//                           levelled, expanded bar;
//   INNER (painted over)  = raw x c x 1/2 x E, a solid fill in the core —
//                           the source's bar DOWNWARD-COMPRESSED.
//
// — each sample at the scales of THE WORKING COLUMN it lies in (THE EXACT
// FORM below): g the leveler's gain at that working column's centre frame
// (waveform_gain_at), c x 1/2 the inner bar's scale there
// (waveform_inner_scale_at: c the compressor's, the inner bar's stage on the
// window loudness L, and 1/2 the flat foreground gain folded into it at the
// derivation — waveform_gain.h owns them all), and E that working column's
// own expander multiplier (`expander_multiplier`), the products its two
// scales (waveform_column_scales) — ON BOTH BARS, so the gap between
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
// THE CORE (architect 2026-11-04) — THE GROWN INNER SHAPE (the inner bars of all columns with
// their contour, dilated by the `waveform_grow` key) FILLED SOLID in the
// `waveform_core` tone, THE KEY FACE (architect 2026-11-07, on real
// MAGNIFY-lit captures; the number and its Arc source at its line of
// kArcDarkTones, gui_theme.h): his ask a lighter core from a tone the
// palette already holds, with no antialiasing halo. 444A58 over the 262A33
// ink, one step over the 404552 canvas, so the center line (the ink, over
// the plate) reads hard across the core (his "yes"). His word of 2026-11-04
// stands: "the absence of it would indicate a quiet section". The core lies
// over
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
// the few columns whose centres bracket its grown support's subcolumns, no
// 2D scan. A loud passage's outer clips
// flat at the lane's edges while the inner still shows its compressed
// height inside it.
//
// BOTH LIT INKS ARE FLAT (architect 2026-09-26): no per-column shade on
// either bar — a shade of the core by the bar gap was struck, "it doesn't
// work", and the architect prefers flat colours to reading a shade.
//
// THE ALIGNMENT IS EXACT BY CONSTRUCTION: the two bars share the lattice, the
// column's [s0, s1) and every read (each piece's one read, or the summary's
// one query, serves both — THE EXACT FORM below). THE OUTER IS A
// DILATION ABOUT THE CENTRE ROW, NOT A HALO ROUND THE INNER: a column straddling zero has
// its outer containing the inner; a column wholly on one side of zero (low
// material at working zoom) has its outer pushed outward, with a gap between
// it and the inner. Intended — that is what a true magnification looks like.
// THE COST: one extra bar's rows per column, and the reads below — at the
// working zoom in the source view one read and two scales a bar, as the dark
// bar's read; above it in the source view ONE SUMMARY QUERY a bar and no
// read (but the last bar, when its extent clamped to [a, total) lies inside
// one working column: the ka == kb arm below, that column's one read); in
// the target view at most two partial reads and one query.
// Measured 2026-11-15 on the Mac over ten minutes of synthetic music-like
// audio (one frame's plate, both channels): a zoomed-out lit frame 0.18 ms
// against 0.29 for one raw read a bar, and in the target view at the working
// zoom, where a bar lies across two working columns and pays two small
// reads, 0.84 ms against 0.58 for one; the worst fade-band frame 3.8 ms. No
// second plate and no new cache field (waveform_gain_fingerprint flips with
// the lamp and the derivation's version). The core adds nothing here: the
// inner's two tips ride the same record and the GPU fills them.
//
// THE EXACT FORM — THE ONE DEFINITION, EVERY BAR, EVERY ZOOM, BOTH VIEWS
// (architect 2026-11-15, "Agree to all."): THE MAGNIFIED SOURCE IS A
// FUNCTION OF SOURCE TIME, ITS SCALE CONSTANT OVER A WORKING COLUMN (the
// curve, WaveformGainCurve, waveform_gain.h, owns the derivation).
// WORKING COLUMN k is the source frames [k · col, (k + 1) · col) ∩ [0,
// total), col the curve's `column_frames`; its CENTRE is k · col + col / 2
// (integer division, unclamped: the bar [k · col, (k + 1) · col)'s own
// (s0 + s1) / 2); ITS TWO SCALES (waveform_column_scales, the one function
// the producer and THE LIT SUMMARY share) are the gain at that centre times
// the column's expander multiplier (the outer) and the inner scale at that
// centre times the same multiplier (the inner). THE MAGNIFIED SAMPLE of a
// channel at frame n of column k is x(n) times a scale, clamped to [-1, 1],
// the outer and the inner each. A BAR'S TIPS ARE THE LEAST AND THE GREATEST
// MAGNIFIED SAMPLE OVER [s0, s1) ∩ [0, total) — s0 and s1 the integers the
// read takes (s1 <= s0 taking s0 + 1), pure functions of the GLOBAL column
// index and the column's width (the bar basis, WaveformBasis above) in both
// views, the target view mapping the column through the warp map to its
// source span first; an empty intersection reads (0, 0) — then the rows
// (bar_tips: the clamp to the band, the quarter rows, the never-fade
// floor). "The least and the greatest" is get_peak_range's: exact up to the
// pyramid's int16 bins, as the dark picture is. SO A COARSE BAR IS THE HULL
// OF THE WORKING ZOOM'S OWN BARS under it — in the source view exactly that,
// lit as dark — AND THE TARGET VIEW IS THE SOURCE'S LIT PICTURE MOVED
// THROUGH THE MAP: every piece of source keeps the height the source view
// gives it, wherever the map puts it (a target bar lying across two working
// columns gives each part its own column's multiplier, so the relief before
// an onset is the source view's own, at the working zoom too). The scale is
// the working column's and not the sample's because the gain's unit is the
// working column by its own definition (waveform_gain.h: the leveler reads
// at working zoom; the expander is one number a working column, read with
// the gain at the column's centre): a per-sample gain would move the working
// zoom's own bytes and agree with nothing the derivation defines. Nothing
// forks on the view, and the scales a bar reads are pan-invariant and,
// inside a level, zoom-invariant by construction. It applies exactly where
// the gain does: NULL (the dark lamp, in either audio view) is raw.
// WHY EXACT (architect 2026-11-15): one scale for a coarse bar stands far from its columns' own
// picture — measured 2026-09-23 on the 40th at whole-piece zoom (1920
// columns, ~0.30 s a column) a column at 122.3 s painted 0.53 by its centre
// gain against 0.87 gained before the reduction, and at 640 columns one
// painted 0.97 against 0.34; measured 2026-11-15 on the Mac's synthetic
// audio with the span's largest multiplier beside the centre gain, 43..86 %
// of zoomed-out bars took other rows, the largest by a tenth of the lane's
// half height and more (the multiplier over a silence-to-onset edge).
//
// HOW A BAR IS COMPUTED, THE PIECES: scale and clamp are monotone, so over a
// run of frames inside ONE working column the extremes are the run's raw
// extremes times that column's scales, clamped; a bar is the hull of its
// pieces. With the span clamped to [0, total), ka = s0 / col and
// kb = (s1 − 1) / col:
// - ka == kb, A BAR INSIDE ONE WORKING COLUMN: the full span's one read (the
//   dark arm's call: its level, its s0 and s1) times that column's scales.
//   At the working zoom in the source view a bar IS a column, and this is
//   the column's own arithmetic. At the deepest level (b = col / 2; with an
//   odd column too, its even edges whole multiples of the column, so each
//   half lies inside its column) each half takes ITS COLUMN'S scales, so a
//   column is exactly the hull of its two halves in the extremes and the
//   rows they round to, before the never-fade floor (the consequence below
//   names the floor's one exception) (architect 2026-11-15, his ruling on
//   the deepest level within "Agree to all.": the
//   gain read up to col / 4 frames from the half's own centre, at most a
//   quarter row on some bars where the gain is moving).
// - ka < kb: a partial working column at either end is a piece read
//   directly (at the level of its own width) times its own column's scales,
//   and every whole working column between them — the end columns too when
//   the span covers them whole, the short last column whole when the span
//   reaches the source's end — is ONE QUERY of THE LIT SUMMARY
//   (WaveformGainCurve::lit_summary, waveform_lit_summary_hull; waveform_gain.h
//   owns its structure, its build and its memory). In the source view above
//   the working zoom (b = col · 2^m, every edge a whole multiple of the
//   column) a bar is whole columns alone: one query, no read and no gain
//   lookup — but the source's last bar, when its extent clamped to
//   [a, total) lies inside a single working column, which takes the
//   ka == kb arm, that column's one read; in the target view at most two
//   partial reads and one query.
// THE SUMMARY IS NEVER READ FOR A BAR INSIDE ONE COLUMN: its entry is the
// column's exact float extreme scaled and then rounded to int16, a pyramid
// read is the int16 bin then scaled, so the two agree to that slack and not
// bit for bit, and the working zoom keeps its direct read and its bytes.
// THE CONSEQUENCE AT A LEVEL SWITCH: a parent is the hull of its two
// children by the definition, lit as dark, so what the fade band spreads is
// the split the dark picture has. In rows it is EXACT between two rungs
// that both read the summary (in the source view, every rung from twice the
// working column up) — in the extremes and the rows they round to, BEFORE
// THE NEVER-FADE FLOOR (kWaveformFloorSubcells, applied in render.cpp's
// bar_tips after the reduction): a flat child (digital silence, its two
// tips 0) is floored to one subrow and can stand that one quarter row
// outside a parent whose own least sample is exactly 0 (silence, then an
// onset column with no negative sample; measured 2026-11-15, a parent's
// outer [885, 1086) against its children's hull [885, 1087)) — the floor
// ruled, the quarter row accepted; elsewhere it holds to the reads' int16
// slack — ACROSS THE WORKING ZOOM'S BOUNDARY (a working-zoom bar a pyramid
// read, its parent a summary entry) TO ONE QUARTER ROW, the bin scaled by
// up to 16 flipping a quarter-row decision on a few percent of bars
// (2..4 %, measured 2026-11-15 on the Mac between the two whole-column
// forms): a quarter of a device px inside a fade.
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

// ONE PLATE COLUMN'S TIPS (2026-10-13; quarter rows 2026-10-30) — a
// column one bar of the plate's ladder (THE PLATE'S BARS above) — in
// the plate's own rows times kWaveformSubcells
// (subrow 0 the top of the canvas's first row): the outer (or dark) bar's
// subrows [o_top, o_bot − 1] and the lit inner bar's [i_top, i_bot − 1] —
// the inner ABSENT (the dark lamp) is i_top = i_bot = 0, and present iff
// i_bot > i_top (a present bar always has bot >= top +
// kWaveformFloorSubcells, the quarter-row floor). The GPU rasterizes them (kListFragmentShader, gl_canvas.cpp): the
// stretch to the columns' pitch, the contour between column centres, the
// one-px core, the `waveform_grow` key's grow and each
// pixel's exact coverage are the fragment shader's, the
// rule at render_waveform_columns' head.
struct WaveformColumnRows {
    int o_top, o_bot;
    int i_top, i_bot;
};
// ONE CHANNEL BAND'S COLUMNS as render_waveform_columns produced them: the
// region the bounds cover, the plate's device rows [y_lo, y_hi) and THE
// STORED ROW'S BARS [col_lo, col_hi) — indices into the band's row of bars,
// 0 .. n for a whole band, bar col_lo + j the global bar
// GuiPlateColumns::first_bar + col_lo + j (the fine row's fine_first_bar +
// col_lo + j), NOT device columns (architect 2026-11-14: the
// plate's columns are its bars, THE PLATE'S BARS above) — `cols` one entry
// per bar from col_lo, and whether that region is the asked area whole
// (`whole`). The plate's stored band holds them as produced, and in the
// fade band a second such row, the fine rung's, as produced
// (GuiPlateColumns::Band::fine; render_waveform_to_plate_columns).
struct WaveformBandColumns {
    int  y_lo = 0, y_hi = 0;
    int  col_lo = 0, col_hi = 0;
    bool whole = false;
    std::vector<WaveformColumnRows> cols;
};
// THE KEPT BARS (architect 2026-11-15): THE PRODUCER'S ROWS
// KEPT FROM ONE RENDER TO THE NEXT, so a render asks
// render_waveform_columns only for the bars it does not already hold. Inside
// a level a bar's numbers never change (PAN AND ZOOM INVARIANCE above), so a
// pan or a zoom inside a level asks for the few bars that enter and drops
// the ones that leave; in the fade band both rows are kept, the main rung's
// and the fine rung's, each as the producer gave it and stored as it is.
// THE STORED PLATE IS THE STORE-LESS RENDER'S, FIELD FOR FIELD, for
// every sequence of renders: the use and the row's update are
// render_waveform_to_plate_columns' (waveform_cache.cpp), which owns that
// claim.
// WHY A SUB-RANGE IS EXACT: a bar's WaveformColumnRows reads its global
// index k and the width b (its two edges nearbyint(k · b) and
// nearbyint((k + 1) · b), mapped through the warp map in target view), the
// channel, the audio's pyramid, the gain curve where lit, the map, and the
// band's rows (y, h) with the clamp plate_h — never the call's column range:
// the producer seeds its running left edge at the first column's own edge
// and carries each right edge to the next column, the same expression of the
// global index either way, and recovers the first bar's index from the bar
// basis's start as nearbyint(nearbyint(k_a · b) / b), EXACT FOR EVERY b OF A
// FRAME OR MORE (an integer b divides its own multiple exactly; a
// half-integer b = c / 2, c odd and at least 3, leaves a tie's half frame,
// under a third of a bar). The ladder's least b is half the working column,
// and the column (working_column_frames, app_state.h) is 2 or more on every
// display at most 1.6 px wide per hertz of the source's rate (70 560 px at
// 44.1 kHz), so b >= 1 wherever this product runs. So a producer call over
// bars [k_a, k_a + count) on its own bar basis
// (waveform_bar_basis({b, k_a, count}), area {0, y, count, h}, col0 0,
// plate_w count) writes, for each of its bars, exactly what the whole row's
// call writes for it, and refuses exactly when the whole row's call refuses
// (no refusal arm reads the column range).
// THE STORE'S KEY: {is_target, warp_frame_map_hash, gain_hash} — the plate
// fingerprint's own identities for the view, the map and the lamp
// (WaveformRenderInputs). A render whose key differs DROPS EVERY ROW first.
// A ROW is kWaveformKeptRungs (2) a channel, each keyed by {b, channel, the
// band's y and h, plate_h}, compared exactly (b a double, by ==), holding the
// entries of the contiguous global bars [k_lo, k_lo + cols.size()).
// Two a channel are what the fade band asks every frame, the main rung's row
// and the fine rung's; a rung no row holds takes that channel's least recently used row
// that the same render does not also ask for, so at a level switch the rung
// that drops out is the one replaced and the new rung is computed whole,
// once.
// NOT IN ANY KEY, each for its reason: the viewport, the step q and the
// plate's width (no bar reads them; they choose b and the range alone); the
// working column (fixed for the audio's life; it only chooses b); the fade
// key (it chooses which rungs are asked, never a bar's entry);
// the inks (the publication's, never the producer's); THE AUDIO AND ITS GAIN
// CURVE — the store is a member of the cache (WaveformCache::kept_bars),
// which lives and dies with one project set's one GuiAudio (run_project,
// main.cpp: the audio declared before the cache, so destroyed after it),
// and no writer of the pyramid or the curve can run while a row stands: the
// pyramid, the samples and the working column are written by GuiAudio::load
// alone, whose one caller (GuiFileLoader::load_file, once per project set,
// before the first plate: no render runs while the audio has no frames,
// compute_waveform_render_inputs) publishes into a fresh GuiAudio
// move-assigned into the set's before any row exists; the curve is written
// by its derivation thread before `ready`, and a lit row needs the lamp,
// which needs `ready` (waveform_magnification_toggle_actionable), after which
// the curve is immutable — and a dark row (gain_hash 0) reads no curve. The
// map hash is the plate fingerprint's own 64-bit content hash, trusted here
// as there.
// THE THREAD RULE: THE GUI THREAD'S ALONE. The synchronous route
// (force_synchronous_waveform_rebuild) hands render_waveform_to_plate_columns
// the cache's store; the worker's route hands it none (the worker's call,
// waveform_worker.cpp) and runs the function's store-less road to the
// letter, so nothing is shared with the worker — not even under
// wait_until_idle's timeout arm, where a worker still running holds a job
// that names no store.
// Memory: THE LIVE ENTRIES are four rows' — a main row at most W + 6
// entries and a fine row at most 2 · (W + 6), W the plate's device width,
// 16 bytes each — so at most 6 · (W + 6) entries in all: at the tablet's
// 2252 px 13 548 entries, about 217 KB, and the scratch's live entries at
// most one row's, 4 516 (about 72 KB). The allocation is not bounded by
// that: each row's vector and the scratch keep their capacity (nothing
// shrinks them, and a victim row is emptied and rekeyed with its
// capacity), and std::vector's growth can leave a grown row's capacity
// above its live size — a few hundred KB in all in ordinary use; no
// worst-case allocation bound is claimed.
// `scratch` is the producer's out for the bars a row lacks, kept so its
// capacity is reused; `clock` stamps each row's last use.
inline constexpr int kWaveformKeptRungs = 2;
struct WaveformKeptRow {
    bool      keyed   = false;   // the row holds a key (its entries may be empty)
    double    b       = 0.0;
    int       y       = 0;
    int       h       = 0;
    int       plate_h = 0;
    long long k_lo    = 0;
    uint64_t  used    = 0;
    std::vector<WaveformColumnRows> cols;
};
struct WaveformKeptBars {
    bool     keyed               = false;
    bool     is_target           = false;
    uint64_t warp_frame_map_hash = 0;
    uint64_t gain_hash           = 0;
    uint64_t clock               = 0;
    WaveformKeptRow     rows[2][kWaveformKeptRungs];   // [channel][slot]
    WaveformBandColumns scratch;
    void clear() {
        keyed = false;
        for (auto& channel_rows : rows)
            for (WaveformKeptRow& r : channel_rows) {
                r.keyed = false;
                r.cols.clear();
            }
    }
};
// THE COLUMN BOUNDS' ONE PRODUCER (2026-10-13; the contract is THE PLATE'S
// RENDER above): `area`, `col0`, the basis, the gain and the map, and the
// size the band is clamped to (plate_w columns — the stored bars' count —
// by plate_h device rows). The core's
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
// a plate width x height — ITS DEVICE-PX SIZE, the canvas area it was
// rendered for — each `stored` exactly where its bounds cover the
// band whole (WaveformBandColumns::whole) — a refused or partial band draws
// nothing — and the plate transparent elsewhere (the rows outside both
// bands, an odd plate's last, included). `rendered` stands once the producer
// has filled the slot: set by the producer, cleared by the cache's destroy.
// THE BARS' GEOMETRY AND THE DRAW TRANSFORM RIDE THE SLOT (architect
// 2026-11-14; THE PLATE'S BARS above owns every number): a band's
// row is the global bars first_bar .. first_bar + n − 1, n its
// `columns.cols.size()` (both bands' alike), each `bar_frames` display-domain
// frames wide on the frame-0 lattice, and texel j's centre stands at
// bar_x0 + j · bar_pitch device px from the plate's left edge — written by
// the one producer (render_waveform_to_plate_columns) from the same job
// inputs as the columns, so they swap and publish with them and a plate is
// always drawn by the transform of the viewport it was rendered for.
// THE WEIGHT TELLS WHETHER THE PLATE STANDS IN THE FADE BAND (architect
// 2026-11-14; THE FADE BAND, waveform_fade_band above): `first_bar`,
// `bar_frames`, `bar_x0` and `bar_pitch` are ALWAYS THE MAIN RUNG'S, as the
// sentence above has them; `bar_weight` == 1 is A UNIFORM PLATE, each band
// its main row alone; `bar_weight` < 1 is THE FADE BAND (architect
// 2026-11-15, the two-picture blend), the weight the float the
// draw takes (WaveformFadeBand::wf, never 1 in the band), and each band
// holds beside its main row `fine`, THE FINE RUNG'S OWN UNIFORM ROW as the
// producer gave it: the global bars fine_first_bar .. fine_first_bar + n_f
// − 1, n_f its `cols.size()`, each half `bar_frames` wide, texel j's centre
// at fine_x0 + j · fine_pitch device px from the plate's left edge — rung
// b's own geometry (waveform_bar_geometry at b), fine_pitch == bar_pitch /
// 2 to the bit — and the band `stored` only where both its rows came back
// whole. Out of the band the fine members are read by nobody.
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
    long long first_bar  = 0;
    double    bar_frames = 0.0;
    double    bar_x0     = 0.0;
    double    bar_pitch  = 0.0;
    float     bar_weight = 1.0f;
    long long fine_first_bar = 0;
    double    fine_x0        = 0.0;
    double    fine_pitch     = 0.0;
    struct Band {
        bool                stored = false;
        WaveformBandColumns columns;
        WaveformBandColumns fine;
    };
    Band band[2];
};

// Draws a waveform_line_px()-wide vertical LINE down the CANVAS's whole
// height (THE WAVEFORM block's canvas paragraph; the stereo divider's row
// crossed, architect 2026-11-02, waveform_divider's rule), AT ITS EXACT
// PLACE `x` (architect 2026-11-14; device px from area.x, playhead_exact_x /
// scanner_exact_x's on the plate basis, app_state.h): the real interval
// [x − t / 2, x + t / 2] centered on the frame, each pixel at its exact
// coverage (the line rule at its exact place, waveform_line_span), in
// `color`,
// SOLID — the scanner (Cool Edit's playback cursor a solid line, measured
// 2026-10-09) and the resting cursor alike (architect 2026-11-04; the
// static tone below, the caller's palette read,
// paint_playheads) — painted
// over whatever it crosses — waveform ink included. No-op if
// outside; the line is drawn wherever any of its real interval lies inside
// the area and cut at both edges (waveform_line_span, the one edge rule), so
// it never leaks into an adjacent region; `col` (playhead_pixel_x /
// scanner_pixel_x's, the nearest lattice point on the same basis: the
// column of x) is the cull's alone.
//
// THE RESTING CURSOR IS ONE STATIC TONE (architect 2026-11-04: a fading line
// measured ~46 % app / ~53 % SurfaceFlinger at 90 presents a second, the
// present itself the cost — "seems like it's unavoidable"): a solid one-W line, opaque
// on every pixel it covers whole,
// in the palette's `playhead_line` (2026-11-21; its line of
// kArcDarkTones, gui_theme.h): A5ABB5, Arc's `.h4` / `.category-label`,
// under the scanner's FFFFFF and over the waveform's hover line 767B87, his
// brightness order (told once at render_hover_line below) — its own pixel
// the same over the ink and over the canvas,
// stopped or playing; the ruler's head keeps its own `playhead_head`, the
// scanner its FFFFFF. NOTHING IS DAMAGED AT REST: the line moves only with the cursor,
// whose writers own the damage (playhead_invalidate_rect, main.cpp).
//
// THE LINE IS THE WHOLE FUNCTION (2026-08-02): the cursor's head is
// paint_ruler_row's (the `playhead_head` triangle in the ruler), and
// no strip lane is reachable from here.
// Through the canvas sink (2026-10-13, canvas_sink.h), as is the anchor stem
// below.
void render_playhead(GuiCanvasSink& sink,
                     GuiRect area,
                     int     col,
                     double  x,
                     GuiColor color);

// THE WAVEFORM'S HOVER LINE — THE ONE STATEMENT OF ITS RULE (architect
// 2026-11-21: "the waveform itself could ... show where the cursor
// would be if I tapped or if I zoomed, where ... the playhead or the zoom
// stem would be. And this would be ... a not as bright stem as the playhead
// or the scanner" — "I would want it to be an even more dark and faint
// version of the playhead stem." — "Cost is accepted. I keep the pen away
// when I don't need it."; AND ITS TRIANGLE, architect 2026-11-21:
// "also i think we should give the hover stem a triangle, same color as the
// hover stem. for symmetry" — "i agree on no triangle hover while barrel is
// held"; and architect 2026-11-24: "hover should lose the playhead triangle
// below the midline"; every other site points here).
//   WHAT IT IS: where a tap at the hovering pen's place would put the
//     playhead, shown WHEREVER SUCH A TAP WOULD MOVE IT — the pen over the
//     waveform (both halves: the lower half's tap auditions from the place,
//     it moves where play starts — THE LINE ALONE there, NO TRIANGLE, the
//     tap placing no playhead), over the ruler — THE PLAYHEAD'S TRIANGLE
//     INCLUDED: a motionless tap on it places the playhead as any ruler tap
//     does (the head drag only adds the drag), and the triangle has no hover
//     of its own (architect 2026-11-21) — over the marker lane
//     where no marker is hit; never over a marker (it hovers on its own
//     terms, AppState::FaceFades' THE MARKER'S FACE — the walk's one hit,
//     GuiInputHandler::plain_lane_press_at, answers both), the trim bar,
//     a key, a card or chrome the press router refuses. WITH THE BARREL
//     HELD, over the waveform alone, where a tip-down would anchor the zoom
//     (pointer_barrel_held_'s one exception, input_handler.h), WITH NO
//     TRIANGLE (the anchor's stem it previews has no head); once the zoom
//     seats, the white anchor stem is the mark and no hover line is drawn.
//     The gate — the pen in range with no contact, the pen latch not
//     holding (no line after a lift until 18 W of motion or a leave: "it
//     should wait to prevent every lift turning into a hover blip"), the
//     main page taking a press there, no seat — is the walk's
//     (GuiInputHandler::walk_hover_line), its answer AppState::HoverLine; a
//     finger never hovers, and nothing shows under an overlay, a question,
//     an editor or the on-screen keyboard. During play it shows as at rest,
//     the scanner over it. The `h` view's lanes answer as the live ones
//     (plain_lane_press_at serves both), so the line and its triangle show
//     there too — and its waveform has no halves (every tap there places
//     the playhead), so its triangle stands over the whole waveform.
//   WHERE THE TRIANGLE STANDS: wherever the line is a tap's THAT PLACES THE
//     PLAYHEAD — the ruler (the playhead's triangle included), the empty
//     marker lane and, on the main page, the waveform's upper half; not over
//     the main page's waveform's lower half (the press's own half test,
//     waveform_lower_half above, architect 2026-11-24), not with the barrel
//     held.
//   ITS TONE, THE BRIGHTNESS ORDER ON THE WAVEFORM (his words above): the
//     scanner and the zoom anchor's stem FFFFFF ("The scanner should be this
//     brightest one. And I guess the zoom stem should also be quite bright.
//     Same color as the scanner"), the playhead's line `playhead_line`
//     (A5ABB5), and this line `hover_line` (767B87, Arc's scrollbar slider:
//     "an even more dark and faint version of the playhead stem"), ITS
//     TRIANGLE THE SAME TONE ("same color as the hover stem").
//     The order is the compiled row's numbers' (kArcDarkTones, gui_theme.h).
//   ITS FORM: the playhead's line's — a one-W line (waveform_line_px)
//     centered on its exact place `x` (device px from area.x), drawn wherever
//     any of its real interval lies inside the strip and cut at both edges
//     (waveform_line_span), at exact coverage, down THE CANVAS'S WHOLE
//     HEIGHT and no line above it — not through the ruler or the marker
//     lane. ITS TRIANGLE, THE PLAYHEAD'S HEAD'S OWN DRAWING (architect
//     2026-11-21): the chip's triangle on the ruler's last
//     cue_chip_h_px rows, its apex on the line's center on the ruler's foot,
//     AT THE LINE'S EXACT PLACE — ONE PLACE for both
//     (GuiPaintHandler::hover_triangle_x: the line's place where the line
//     is a tap's that places the playhead) — in `hover_line`, flat, cut to
//     the waveform's columns
//     and painted where any part of its reach overlaps them (the head's
//     half-head rule; paint_ruler_row). NO FADE: the line and its triangle
//     appear, move and leave at once, as the scanner does; no gate, latch,
//     fade or clock of the triangle's own — every edge that removes the
//     line removes it.
//   ITS ORDER: the canvas list's FIRST LINE, after the plate, the center
//     lines and the stereo divider and under every other line — the lead-in
//     ring, the playhead's line, the selection stems, the scanner, the zoom
//     anchor's stem — and under the sweep highlight
//     (GuiPaintHandler::build_canvas_list; paint_hover_line). It is not
//     drawn where a later line of the same width stands on its exact x
//     (THE COINCIDENT LINES, paint_playheads' paragraph): the playhead's
//     line — the pen back over the place it just tapped — a selection stem,
//     the running scanner. ITS TRIANGLE, IN THE RULER: over the ticks and
//     digits and UNDER THE PLAYHEAD'S HEAD, painted immediately before it
//     (paint_ruler_row), so where the two overlap the playhead's shows. A
//     YIELDED LINE STILL HAS ITS TRIANGLE (the rule): at the playhead's own
//     x it lies under the playhead's head and shows nothing; at a selection
//     stem or the running scanner, which have no head in the ruler, it
//     marks the place there.
//   ITS PLACE, THE TRUTHFUL ONE, derived at the paint
//     (GuiPaintHandler::hover_line_x) from the stored gate answer, the pen's
//     real place and the picture being painted: THE FRAME THE TAP WOULD
//     LAND ON — the tap's own road (playhead_landing_at_click_x: the frame
//     under the pen's real place on the painted item basis, the live
//     domain's clamp) — back to its exact place by the playhead line's own
//     expression on the plate basis (cursor_frame_exact_x), so after the tap
//     the playhead's line stands on the very double this line stood on —
//     SAVE WHERE THE PLACEMENT'S KEEP-VISIBLE CAMERA MOVES THE PICTURE (a
//     tap in the strip's last half column away from the song's right wall:
//     the landing frame's column rounds to w, off screen to the camera,
//     displayed_frame_on_screen, and Viewport::reseat_playhead edge-aligns
//     it into the last column): the playhead lands on the same FRAME, the
//     picture moved under it, its line a fraction of a px left of where
//     this one stood on the old picture. THE LINE IS RIGHT there too: it
//     marks the frame the tap lands on, on the picture as painted.
//     Inside the song that is the pen's place to within half a frame — a
//     frame is never wider than 1/20 px (kDeepestZoomMinFramesPerPx,
//     app_state.h), so 1/40 px at most; past either end of the song (the
//     framer leaves up to 5 % of the window empty on a side) it stands
//     where the clamp puts the playhead, on the song's first or last frame.
//     WITH THE BARREL HELD, THE ANCHOR'S PLACE: the zoom's seat at the pen's
//     place (zoom_anchor_seat_at — the pen's real column clamped into the
//     waveform's [0, w − 1], the song position under it on the live
//     viewport, no frame taken and no song clamp) through the anchor stem's
//     own place (strip_anchor_stem_x), so the white stem replaces the line
//     in place when the zoom starts. The two places part by up to half a
//     frame's width (the tap's whole frame); past either end of the song,
//     by the whole stretch past it (the anchor stands under the pen); and
//     IN THE STRIP'S LAST COLUMN, inside the song too, by up to a device px:
//     the anchor's column is clamped into [0, w − 1]
//     (clamp_col_into_waveform, input_pointer.cpp) while the tap takes the
//     whole last column up to w (playhead_landing_at_click_x). When the
//     picture moves under a still pen (FOLLOW's scroll, a key's zoom, a
//     landing, a plate's publication) the line stays under the pen and its
//     landing frame follows, with no event of the pen's.
//   ITS DAMAGE (canvas_sink.h's contract; the scanner's shape): the line's
//     cells as last painted and its new cells, each the hull of its span over
//     the canvas's rows (scanner_invalidate_rect through
//     Viewport::invalidate_hover_line), and ITS TRIANGLE'S box as last
//     painted in the ruler and its box at the new place, each the ruler
//     head's painted box (GuiPaintHandler::ruler_head_box through
//     Viewport::invalidate_hover_triangle: the head's painted reach over
//     the ruler's last rows, 18 x 8 device px on the tablet) — a pen move
//     repaints those two small boxes of the ruler beside the line's
//     cells (architect 2026-11-21), the ruler's painter redrawing under their clip
//     alone (paint_ruler_row) — each only where its own place changed
//     (GuiInputHandler::set_hover_line, two compares against the places
//     last declared: the line's, and the triangle's, which carries whether
//     it stands, so the barrel's Tap <-> Anchor edge at one x, and the pen
//     crossing the midline at one x, damage the triangle alone — both
//     compares run at every walk, a vertical move included) — so a pen
//     resting or out of range costs
//     nothing and the app presents nothing; while the pen moves in hover
//     range nearly every report that moves its real x moves the line (the
//     Tap place where the x crosses a frame's edge, a frame being 1/20 px
//     at most; the Anchor place with the x itself) and costs a frame at the
//     beat (the cost accepted: architect 2026-11-21, "Cost is
//     accepted"). Every edge that removes it (the leave, a contact, the latch's
//     arm, a veil, a card, a load, a drag gone live, the zoom's seat, the
//     pen onto a marker, the trim bar or a key) damages its last painted
//     cells and its triangle's the same way. THE OLD CELLS ARE AS PAINTED
//     (AppState::HoverLine's `painted_x`, written by the painter at every
//     list build; the triangle's `triangle_painted_x`, written by the
//     ruler's painter by the head hit's publishing rule), so no ghost can
//     stand: the cells erased are always the ones on the glass; and THE
//     PLACE COMPARED IS THE GLASS'S (`declared_x` and `triangle_declared_x`,
//     written by those painters too), so a picture that moved under a still
//     pen invites no frame after the one that drew it — a camera move or a
//     plate's publication damaging the viewport's lanes whole from their
//     top row, the ruler among them, so the triangle is redrawn at its new
//     place by that very repaint (AppState::HoverLine's WHY NO GHOST).
void render_hover_line(GuiCanvasSink& sink,
                       GuiRect area,
                       double  x,
                       GuiColor color);

// Draws the strip-drag ANCHOR STEM: a vertical line at the drag's pivot's
// exact place `x` (device px from area.x, clamped by its producer into
// [0, area.w − 1]: strip_anchor_stem_x, warp_frame_map_view.h),
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
// color over the waveform ink it crosses. The line is waveform_line_px()
// wide AT ITS EXACT PLACE (architect 2026-11-14): the real interval [x − t / 2, x + t / 2]
// cut at both edges, each pixel at its exact coverage (waveform_line_span;
// a clamped place always has its line inside the area).
void render_strip_anchor_stem(GuiCanvasSink& sink,
                              GuiRect area,
                              double x);

// The ONE trim bound-to-place geometry owner. Every consumer of a
// trim bound's exact place funnels here: the
// paint site (render_trim_flags' endcaps, bar and bridge gap), the TRIM
// sweep's highlight (sweep_highlight_columns, app_state.cpp, the bar's own
// chain) and the bound-set click's nearest-cap test (input_trim.cpp). The
// two hit sites (hit_test_trim_endcap's endcap rects,
// point_in_trim_bridge_span's bridge test) read what the painter PUBLISHES
// (TrimBarHit, below — strictly as-painted, 2026-09-24), so the places they
// test are this owner's output by construction.
//
// PURE: all basis inputs are parameters — the collapse unifies the FORMULA. The
// live trim pass (GuiPaintHandler::paint_trim) calls with the DISPLAYED basis
// from item_viewport_basis (vp_start_frame/vp_end_frame/lattice_origin/area_w
// — the promoted mirror of the committed fp_vp span, its lattice + effective
// width; the place displayed_x_at's, on that lattice's own index) and
// `displayed_ms`
// mapped through displayed_or_live_target_map by displayed_trim_ms (the
// event-synchronized hit-geometry doctrine), and the hit sites read that
// pass's publication, so paint and hit are one geometry by construction.
// (The LIVE viewport would split a hit from its painted pixels during an
// async plate-publish window; the promoted mirror closes that window.)
//
// The place's denominator is the PAINTERS' q, given back by the span
// (vp_end - vp_start) over wave_w (recovered_painter_spp,
// warp_frame_map_view.h) — the sixteenth-frame q exactly — NOT
// current_samples_per_pixel. The two are
// identical at whole zoom levels and differ by under a thirty-second of a
// frame per column at a fractional zoom rest; the hit sites reading the
// painters' q is what ALIGNS paint and hit exactly — the point of unifying
// them, and what the published stash carries for free.
//
// EOF-WALL CLAMP (the one copy):
// an IN-VIEW bound's exact place `x` clamps into [0, wave_w − 1], the last
// column's left edge, in reals. IN VIEW, EXACTLY (2026-11-18): the bound at
// or right of the viewport's named start
// (displayed_ms >= vp_start, the left verdict, so a bound
// on the start a fraction of a px left of the strip's real left edge still
// holds its begin zone whole at column 0) and its exact place left of the
// strip's real right edge, x_raw < wave_w — not the rounded vp_end, which at
// a fractional rest can stand a frame short of that edge (52,921 frames at
// q 23.375 at the right wall: vp_end 52,920, while the last frame's place
// is 2251.957 of 2252) and so drop a visible last frame's clamp.
// The inclusive END wall T-1 at full zoom-out stands within a px of the
// surface's right edge; left unclamped, the end's grab zone, anchored on the
// bound's right edge, loses its bound-edge pixel to the lane clip. Clamping
// lands the wall on the last visible column so the zone's inside reach
// stays whole on the bar's end (its 2 W outside reach is past the glass
// there and the lane's clip cuts it, as it cuts any zone at the strip's
// edge; THE TRIM BAR'S CAPS AND GRABS).
// Begin/frame-0 already stands at 0, unaffected.
// AN OFFSCREEN BOUND KEEPS ITS OWN PLACE, past the edge (architect
// 2026-10-05, "it slides off the edge, clipped, like any other content"): its
// button stands where the bound is and the lane's clip cuts it, so a panned
// end leaves the view column by column instead of vanishing whole with the
// body growing into its room. The painter clips everything it draws to the
// effective width [0, wave_w), so nothing beside it paints.
// THE BOUND'S EXACT PLACE IS THE WHOLE ANSWER (architect 2026-11-14; no
// column rides beside it, the no-grid rule): `x_raw` is
// displayed_x_at on the caller's basis, device px from the lane's first
// column, and `x` is that place WITH THE EOF-WALL CLAMP APPLIED IN REALS —
// an in-view bound's clamped into [0, wave_w − 1], an offscreen one's raw.
// THE CLAMP IS THE GRABS' ALONE (2026-11-18): the two end
// zones and the bridge, published on the span's real ends
// (trim_endcap_rect, trim_bridge_gap) and tested with the pointer's real
// place (architect 2026-11-15, the no-grid arc: "yes, I'd like to move to
// fractional hitboxes."), and the bound-set click's nearest-cap verdict by
// real distance (trim_bound_click, input_trim.cpp), read `x`; THE SLIDER'S
// BOX — the pill (render_trim_flags) and the TRIM sweep's highlight
// through trim_span_columns — reads `x_raw`, so as an end leaves the strip
// the pill's end moves with it px for px and is cut by the lane's clip,
// never stepping where the clamp holds the zone.
struct TrimBoundColumn {
    double ms;          // displayed-domain position (already mapped)
    bool   in_viewport; // ms >= vp_start and x_raw < wave_w, the exact
                        // verdict
    double x_raw;       // the bound's exact place, unclamped: the slider's
                        // box (the pill, the sweep highlight)
    double x;           // that place clamped into [0, wave_w − 1] in view
                        // (the EOF-wall clamp), raw off it: the grabs
};
TrimBoundColumn trim_bound_column(double displayed_ms,
                                  long long vp_start, long long vp_end,
                                  long long lattice_origin, int wave_w);

// The BETWEEN-THE-ENDS column interval [lo, hi) (waveform-relative,
// half-open, EMPTY when hi <= lo), the ONE owner of the bridge, run by the
// painter (render_trim_flags) alone: the clipped interval is what the painter
// PUBLISHES as the pair drag's handle (TrimBarHit::bridge_lo / bridge_hi,
// read by point_in_trim_bridge_span) — the trim bar's span between its two
// end zones (THE TRIM BAR'S CAPS AND GRABS). Both bounds must be set
// (callers gate). ONE RULE ON EVERY SIDE (architect 2026-10-05), ON THE
// SPAN'S REAL ENDS (architect 2026-11-15, the no-grid arc): lo is the
// begin zone's inner RIGHT edge (x + inside) and hi the end zone's inner
// LEFT edge, exclusive (x + 1 − inside), x each bound's exact place
// (TrimBoundColumn::x) and `inside` a zone's inside reach
// (trim_grab_inside_px, 12 W, 24 device px on the tablet; architect
// 2026-11-20), whether the zone is on screen, part on it or
// wholly past an edge. In the NARROW case, where the two inside reaches
// cross and trim_endcap_rect splits the zones at the midpoint, hi is under
// lo and the gap is empty; a window wholly off one side is
// empty or wholly past that edge. This interval is returned
// UNCLAMPED, offscreen columns included; it is NOT a drawn interval. The
// painter clamps it ONCE per use: the published bridge intersects it with the
// effective width [0, wave_w), so nothing beside it answers a hit.
struct TrimBridgeGap {
    double lo;  // the gap's left edge, inclusive
    double hi;  // its right edge, exclusive (empty gap when hi <= lo)
};
TrimBridgeGap trim_bridge_gap(const TrimBoundColumn& begin,
                              const TrimBoundColumn& end, double inside);

// THE SPAN'S COLUMNS ON SCREEN [lo, hi], REAL (waveform-relative device
// px, EMPTY when hi <= lo; architect 2026-11-14): from the begin
// bound's exact place through the end bound's exact place plus one px —
// the begin column through the end column inclusive, in reals: [max(xb, 0),
// min(xe + 1, wave_w)], xb and xe the bounds' `x_raw` (TrimBoundColumn,
// unclamped: the EOF-wall clamp is the grabs', and the box the pill's own
// places, so box and highlight agree edge for edge — where the clamp would
// differ, an end in the last column reaches wave_w either way and a begin
// there starts at its own place, not at the column's left edge) — cut to
// the effective width
// [0, wave_w]: THE TRIM BAR'S SLIDER'S BOX ON SCREEN (THE TRIM REFLECTS
// THE WAVEFORM'S COLUMNS, architect 2026-11-03 — the box; render_trim_flags
// draws its pill from the same two places uncut, the pill's ink the seat
// inside each bound, as GTK draws a slider, and the lane's clip cutting it,
// architect 2026-11-18, render.h's THE SLIDER'S BOX AND ITS CORE), read by
// THE SWEEP HIGHLIGHT on the TRIM former (sweep_highlight_columns,
// app_state.h; architect 2026-11-12), so the canvas's highlight stands
// under the bar's box edge for edge. Bounds on whole places answer the
// whole columns the bounds' columns give.
struct TrimSpanColumns {
    double lo = 0.0;  // the span's left edge
    double hi = 0.0;  // its right edge (empty when hi <= lo)
};
TrimSpanColumns trim_span_columns(const TrimBoundColumn& begin,
                                  const TrimBoundColumn& end, int wave_w);

// The source-frame -> displayed-domain mapping of the trim bar's bounds: a
// bound's image ROUNDED TO A WHOLE DISPLAYED FRAME. In a mapped view the
// forward map (map_source_to_target) keeps the whole source frame's image
// fractional, and this rounds it ONCE, nearbyint; the identity (null/empty
// map) path returns the frame as-is. Byte-identical to render.cpp's
// file-local frame_to_paint_sample for every reachable (non-negative) trim
// bound, whose first rounding, of the source frame, is a no-op on a whole
// frame. A negative frame is guarded to 0 (unreachable — past-EOF is
// load-fatal and bounds are never negative). `map` is null in source view (identity) and the item
// pixels' own map (displayed_or_live_target_map) in target view.
// THE PAINTER'S ONE MAPPING OWNER, so a span's end is drawn — and, through
// the stash, grabbed — on its bound's image; FOUR CALLERS, each placing a bound where the bar paints it: the bar's paint
// pass (GuiPaintHandler::paint_trim, paint_handler.cpp), the sweep
// highlight's columns (sweep_highlight_columns, app_state.cpp), the barrel's
// bound-set click choosing the nearer cap (GuiInputHandler::
// trim_bound_click, input_trim.cpp) and the endcap drag's grab offset on
// the identity (trim_bound_place_x, input_trim.cpp — Source view, or a
// target view with no map; a mapped domain's offset reads the unrounded
// image instead, trim_bound_image_place_x).
double displayed_trim_ms(int64_t frame,
                         const std::vector<WarpFrameMapSegment>* map);

// The ONE trim GRAB screen-rect owner (the trim bar's span's two END ZONES,
// around its two caps — THE TRIM BAR'S CAPS AND GRABS): the
// anchoring rule lives here, run by the painter (render_trim_flags), which
// publishes each zone, clipped to the lane, for the hit test
// (hit_test_trim_endcap reads TrimBarHit, below) and the bar's hover walk
// (GuiInputHandler::walk_trim_bar_hover, which reads the same stash).
//
// A trim bound is an EDGE, not a point, AND THE ZONES STAND ON THE SPAN'S
// REAL ENDS (architect 2026-11-15, the no-grid arc): the begin's bound is
// the span's left end (strip_x + begin.x), the end's the span's right end
// (strip_x + end.x + 1), x each bound's GRAB place (TrimBoundColumn::x, the
// EOF-wall clamp in it) — NOT the slider's box (`x_raw`, trim_span_columns'
// places, the pill's and the caps'): where the clamp holds an in-view bound
// on the last column's left edge, the end zone stands up to a px left of
// the box's end, and a begin on the named start a fraction of a px left of
// the strip has its bound at 0. EACH ZONE REACHES `outside`
// (trim_grab_outside_px, 2 W) PAST ITS BOUND AND `inside`
// (trim_grab_inside_px, 12 W) INTO THE SPAN (architect 2026-11-20): the
// begin's [B_b − outside, B_b + inside), the end's
// [B_e − inside, B_e + outside).
// THE NARROW CASE (architect 2026-10-03; the split 2026-11-20):
// when the grabs' span B_e − B_b is under two inside reaches — the places
// read wherever they stand, on screen or off (architect 2026-10-05) — the
// two inside reaches cross, and THE ZONES SPLIT AT THE MIDPOINT BETWEEN THE
// BOUNDS, (B_b + B_e) / 2: the begin's [B_b − outside, midpoint), the end's
// [midpoint, B_e + outside), each keeping its own outside reach, so a press
// takes the nearer bound and NEITHER ZONE STANDS PAST ITS OWN BOUND'S
// OUTSIDE REACH. The two rects never overlap, and the split is continuous with the wide
// case: at a span of exactly two inside reaches the inner edges meet at the
// midpoint.
//
// THE RECT IS THE ZONE over the trim lane `row`'s whole height, and it is
// THE HIT BAND AS IT IS — no tolerance inflates it. `is_begin` picks which
// bound's zone. The rect may lie part or wholly past the lane's edge (an
// offscreen bound keeps its own column, TrimBoundColumn); the painter's
// clip decides what of it is on screen.
GuiRectF trim_endcap_rect(bool is_begin, int strip_x,
                          const TrimBoundColumn& begin,
                          const TrimBoundColumn& end, GuiRect row);

// THE TRIM BAR'S HIT STASH (architect 2026-09-24, strictly as-painted): what
// render_trim_flags last PAINTED as the trim bar's span — its two end zones
// and its body between them — published by that painter into
// AppState::trim_bar_hit so the trim hits read the pixels rather than
// re-running the painter's owner chain on the live trim — the flag lane's
// stash doctrine (AppState::flag_hit_rects) carried to the trim lane.
// Everything is in SCREEN pixels, THE ZONES AND THE BRIDGE REAL (architect
// 2026-11-15, the no-grid arc: on the span's real ends, tested with the
// pointer's real place, half-open). `lane` is the trim lane's rows (its two
// lines and its field, whole), the y-gate of both hits. Each end (a TrimBarHitCap)
// is its zone's rect (trim_endcap_rect, over the lane's whole height,
// clipped to the lane's painted width, the hit band as it is — no
// tolerance), a zone sliding off an edge publishing its visible stretch
// alone (architect 2026-10-05), and `painted` false for a zone wholly off
// the lane, which answers no hit. The two rects never overlap. The bridge is
// the half-open interval [bridge_lo, bridge_hi) between the two zones'
// inner edges, already clipped to the lane's painted width
// (trim_bridge_gap); empty when lo >= hi, as in the narrow case.
// `published` false is COLD — nothing painted, nothing grabbable.
struct TrimBarHitCap {
    bool     painted = false;
    GuiRectF rect{0.0, 0.0, 0.0, 0.0};   // the end zone, clipped to the lane
};
struct TrimBarHit {
    bool          published = false;
    GuiRect       lane{0, 0, 0, 0};
    TrimBarHitCap begin;
    TrimBarHitCap end;
    double        bridge_lo = 0.0;   // screen x, inclusive
    double        bridge_hi = 0.0;   // screen x, exclusive
};

// THE TRIM BAR'S STATES — THE ONE TABLE (architect 2026-11-19 / 2026-11-20,
// his words on the mocks: "at rest, the scroll bar should probably
// be just as it is today. That's what Kden Live does. ... the body of the
// bar, would turn ... the darker color that Thunar does, and then when it's
// being dragged, it would turn blue. The end caps would appear when hovering
// the body ... And they're mutually exclusive, so when you're hovering one,
// when you're hovering the body, the end caps appear in white, but they are
// not hovered. And when you're hovering the end caps, the body loses the
// hover."; "When the body is being dragged, the caps should not turn blue.
// They should stay there. Unhovered, unclicked color, the default text. And
// yes, this is a deviation from ARC, but it's accepted."; 2026-11-20, the
// sweep: "trim gesture from icon doens't draw endcaps but it should - it
// should look like the end endcap is being dragged b/c effectively that is
// what it is"). A PART of the bar is an end (its grab zone, the cap's) or
// the body (the bridge), as the press reads them (TrimBarHit). THE LOOK the
// painter reads (trim_bar_look, app_state.h, fills it):
//   `hovered` the part under the hovering pen (AppState::TrimBarHover, the
//     walk GuiInputHandler::walk_trim_bar_hover), none under a contact;
//   `held` the part the bar's own press holds, from the arm to the lift
//     (pending_trim_drag / trim_drag: an end, or the pair's body), a pen
//     or a finger alike — a finger has no hover, so its caps show from the
//     touch and go with the lift;
//   `swept` THE END A TRIM SWEEP THAT WROTE IS MOVING (region_drag's
//     `moving_begin`, from its first accepted write to the lift; none
//     otherwise), the other road that moves the span — the bound the pen
//     sets, the end while the pen stands right of the down's frame and the
//     begin while it stands left of it.
// THE TABLE (trim_bar_tones), first match:
//   held body ........ the body `selected_fill`, BOTH CAPS `text`
//   held end, or the
//   swept end ........ THAT CAP `selected_fill`, the body at rest, the
//                      other cap `text` — the sweep IS an end held
//                      (architect 2026-11-20)
//   hovered body ..... the body `trim_span_hover_face`, BOTH CAPS `text`
//   hovered end ...... THAT CAP `trim_span_hover_face`, the body at rest,
//                      the other cap `text`
//   none ............. the body at rest (`trim_span_face`), NO CAPS — the
//                      bar as Arc and Kdenlive draw it.
// WHEREVER CAPS SHOW, BOTH STAND, AT EVERY LENGTH OF THE PILL, the end's
// over the begin's (THE TRIM BAR'S CAPS AND GRABS; architect 2026-11-20):
// the table is of the look alone.
// NO FADE: Arc's `scrollbar slider` carries no transition (arc-theme's
// `gtk-3.0/gtk-dark.css` as built, its `scrollbar slider` rules, lines
// 956 .. 960; the 300 ms transition at line 934 is the `scrollbar` trough
// node's and is not inherited), so every state changes AT ONCE, the caps
// with it; every edge damages the trim lane (trim_bar_look's head).
enum class TrimBarPart : uint8_t { None, Begin, End, Body };
struct TrimBarLook {
    TrimBarPart hovered = TrimBarPart::None;
    TrimBarPart held    = TrimBarPart::None;
    TrimBarPart swept   = TrimBarPart::None;   // Begin or End, or None
};
struct TrimBarTones {
    GuiColor body{};
    bool     caps = false;   // both caps drawn, or neither
    GuiColor begin_cap{};
    GuiColor end_cap{};
};
TrimBarTones trim_bar_tones(const TrimBarLook& look);

// Draws THE TRIM BAR'S SPAN — the pill on the trough, between the frame's
// two lines (ARC'S SCROLLBAR, architect 2026-11-17; the well paragraph for
// its look, trim_lane_h_px for its rows): on the trough paint_trim's ground step laid
// (trim_trough_rect, its rows the pill's seat), THE SPAN AS THE PILL — the trim window from
// the begin bound's exact place through the end bound's, sliding off an
// edge like any content (architect 2026-10-05: an out-of-view bound reads as
// the window running on) — the slider's box, the pill's ink the seat inside
// each bound and round there at its real place, the lane's clip cutting
// it at the strip's edge (architect 2026-11-18, the lanes block's THE
// SLIDER'S BOX AND ITS CORE), at exact coverage (CoverageStampPass's
// rounded rect), THE CAPS the pill's own two ends in their own tones where
// the state shows them, the pill still stamped once
// (CoverageStampPass::rounded_rect_bands; THE TRIM BAR'S CAPS AND GRABS),
// every tone `look`'s through
// the one table (trim_bar_tones above); no playhead
// (the well paragraph above, 2026-10-09 ~14:30). The lane's ground, its
// trough and its two lines are paint_trim's ground step, on every frame
// class (architect 2026-11-21). The lane's rows are
// the `trim_bar` PARAMETER — the caller passes top_trim_row_area(app), and
// the rows painted in are published as TrimBarHit::lane, the y-gate both
// trim hits read, so paint and hit take the lane as one value;
// `waveform_area` is read for its `.x` and `.w` — the canvas's columns, the
// interior's and the span's, `.w` also the column-mapping denominator — the
// panel's whole width (2026-11-19, edge to edge).
// `top_strip_area` is a validity guard only. Column placement is on the displayed viewport basis — `trim.begin` /
// `trim.end` are already in the displayed domain. The span has NO editable
// payload; it is a plain-press grab target only (trim is outside the
// selection system).
// PUBLISHES WHAT IT PAINTS into `out_hit` when non-null (TrimBarHit above):
// the lane, the span's two end zones and its body between them, on the
// slider's box this pass paints its pill in (the bounds' own places, the
// pill's ink the seat inside them, 2026-11-18; the zones read the grabs'
// clamped places, trim_endcap_rect) — and a cold record on every
// early return.
// The CALLER decides whether this frame may publish at all
// (GuiPaintHandler::paint_trim passes null unless the damage clip covers the
// whole lane), so a narrow repaint cannot stamp the stash over pixels it did
// not redraw.
// `look` IS ASKED ONCE, AFTER `out_hit` IS PUBLISHED AND BEFORE ANYTHING IS
// STAMPED, so a look decided on the picture this pass paints reads the
// zones this pass publishes: the caller's resolves a pen lift's pending
// answer there (GuiPaintHandler::paint_trim, resolve_trim_bar_lift,
// AppState::TrimBarHover's `lift_pending`). An early return asks nothing.
void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect trim_bar,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       long long viewport_lattice_origin,
                       const TrimRange& trim,
                       const std::function<TrimBarLook()>& look,
                       TrimBarHit* out_hit);

// The top-strip lane the cues occupy, exactly as the lane accessor reports
// it: `marker_lane` = top_marker_row_area, whose bottom edge is flush with the
// waveform top. The accessor delegates to strip_row_rect, the single
// strip-geometry owner, and takes AppState — which this module does not see, so
// the caller resolves it and passes it in. That is the point of the parameter:
// the cues and their hit rects land on the SAME rows the empty-lane press
// gate and every other lane consumer read, whatever the strip's lane heights
// are. Kept as a struct rather than a bare GuiRect so the call sites
// that thread it through keep naming what they are threading.
// `columns_x` is THE WAVEFORM'S FIRST COLUMN in the surface's x
// (waveform_area's x, 0: the canvas stands edge to edge, 2026-11-19) —
// the origin the cues' columns are counted from and the
// clip's left edge (clip_to_waveform_columns).
struct FlagLaneRects {
    GuiRect marker_lane;
    int     columns_x = 0;
};

// ONE MARKER'S STEM ENTRY, as the flag painter publishes it: ITS EXACT PLACE
// (`x`, architect 2026-11-14: displayed_x_at of the frame the chip
// was laid out at, on the flag pass's own basis, device px from the
// waveform's first column, a drag's moving place included — where the
// stem's line is painted, the playhead's line and the lead-in ring's left
// side yielding to a stem on their own exact x,
// GuiPaintHandler::stem_stands_at).
// The painters publish one for EVERY SELECTED LIVE CUE AND DIFF
// FLAG, ENABLED OR DISABLED (architect 2026-11-11), they walk
// WHOSE LINE REACHES THE WAVEFORM'S COLUMNS — any of the one-W line's real
// interval inside them (architect 2026-11-15, the no-grid arc; the gate is
// the line's own span, waveform_line_span below, never a column) —
// (resolve_flag_face's `stem`; an `h` item is one cue), and THE STEM PAINTER
// (GuiPaintHandler::standing_stem_spans, the one geometry both halves of
// the stem read) paints THE SELECTION STEM — a
// solid one-W `cue_outline` line down
// the canvas's whole height — on every entry (architect 2026-11-05), so the
// stem and its chip can never disagree about a place. The flag PAINTERS
// are the only producers — the two live columns', and the `h` view's diff
// lane, which replaces them wholesale while the mode stands. An UNSELECTED
// marker publishes NO ENTRY AT ALL (no stem, the playhead on it or not); the
// disabled axis never decides an entry, nor does an `h` item's style (the
// ruling at render_history_diff_flags). Stems are pointer-inert, so the
// stash is paint-only and `marker_index`
// serves the painter's identity bookkeeping alone.
struct MarkerStem {
    int     marker_index;
    double  x;
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
// published there — by render_flag_editor_box.
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
// resolve_flag_face's table): every selected marker, enabled or disabled
// (2026-11-11), publishes its stem entry (MarkerStem); its chip is its
// resting chip, the label paints as at rest.
//
// THE PAINTER PUBLISHES ITS GEOMETRY. `out_hit_rects` receives one rect per
// painted cue, left to right (the hit walk follows the paint order:
// triangles first, read backwards — the right one on top — then labels,
// read backwards likewise — the right one on top, the labels painting left
// to right; topmost_flag_rect, app_state.cpp), and `out_stems` one entry per
// SELECTED cue, enabled or disabled, whose column is a waveform column. A derived width cannot be recomputed
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
                  SuppressedBox suppressed = SuppressedBox{},
                  const CueLaneHover& hover = CueLaneHover{});

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
//                   them while the editor stands (2026-10-25), its veil
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
//                   PRESS ON A RIDING CELL IS VEILED (2026-10-25: every
//                   editor a modal): it is a press outside the field, which
//                   on_button_press's veil consumes silently, and the editor
//                   stands — Esc and Enter its way out. It is
//                   a SECOND rect, deliberately not folded into
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
// The box sits in the marker lane, off the touch pan zone (the lanes are
// no navigation surface, 2026-09-25), and the platform's editor-field query
// reads this same rect
// (touch_point_in_editor_field), so a finger landing in the field reaches
// the caret drag — a tap its press — rather than the phone-model pan.
// THE BOX IS REAL (architect 2026-11-15, the no-grid arc): the painted
// field's own real edges — it stands at its mark's exact x — cut to the
// waveform's columns, over the lane's whole rows, asked half-open with the
// pointer's real place.
struct FlagEditorBox {
    bool                valid         = false;
    GuiRectF            box{0.0, 0.0, 0.0, 0.0};
    FlagHitRect         riding_cells{};
    double              text_origin_x = 0.0;
    std::vector<double> byte_x;
};

// THE FLAG EDITOR'S UNROLL (2026-08-01): the edited
// segment of the cue's label EXPANDS to hold its full untruncated payload,
// and the editor's text is drawn in it — kdenlive's flag-becomes-the-text-
// box, on Cool Edit's cue.
//
// THE FIELD IS GTK'S IN-PLACE CELL EDITOR ON THE LANE'S WHOLE ROWS
// (architect 2026-10-09; the look 2026-11-18 / 19; the ruling and its
// reason at the cue block's EDITING paragraph):
// the entry's fill `ruler_face` inside a one-W border of the accent
// (cue_editor_border_px), square-cornered, its text and caret `field_text`
// on the label's own baseline and seat, the caret and the selection band
// THE TEXT'S LINE BOX ROWS (cue_line_box_top_px, cue_line_box_h_px), so
// opening an editor changes the label's SIZE and nothing about where its
// text stands — the selected substring in the chrome's SELECTED PAIR, the
// ordinary highlight. The box paints over the lane, the triangle and any neighbouring
// label it reaches. Since no field buys a caret column, the
// size only changes where the resting label was capped, the field opening at
// the committed run's own width and growing only with what is typed past it;
// the cells riding its right edge keep their resting look. THE CHIP AND
// THE STEM stay the cached pass's. A REFUSED COMMIT recolours nothing: the
// field stands as it was (text_editor::refuse) and the refusing owner's card
// says why, the modal editors' rule on this surface.
//
// THE TEXT IS THE LABEL FACE, matching the labels it replaces.
//
// AT THE WAVEFORM'S EDGE THE FIELD IS CUT OFF AND NOTHING MOVES — the clip is
// the waveform's columns [0, w), the resting cue's own (architect
// 2026-09-26; the published box and riding rect are clipped with the
// pixels) — (architect 2026-09-06: "leave its position truthful, don't
// clamp it, don't do anything"): under the one graphic model a field slid
// onto the window would walk left over the boxes the pass is still painting
// at rest and out of its own box's slot. The field therefore holds its WHOLE
// run at every width — the text does not scroll inside it — and a field
// reaching past an edge is READ BY Esc, A PAN AND A REOPEN (architect
// 2026-10-25): the editor is a modal, so nothing pans while it stands, and
// its open pans nothing either.
// State::view_offset_px survives on this surface for the sub-pixel case alone
// (a run's width rounded down to a whole column, which would otherwise clip
// the caret's reserved column at end-of-text); the glyph-by-glyph travel its
// minimal-travel rule describes is the value sub-page's entry's, whose box is
// fixed (paint_handler.cpp's THE VALUE SUB-PAGE; architect 2026-11-19).
// LEFT / RIGHT navigate exactly like any one-line field.
//
// Takes AppState by NON-CONST reference, alone among the renderers here, and
// for two honest reasons: it advances the editor's view offset (session state
// that must persist across frames) and it publishes app.flag_editor_box. Both
// are the painter owning what only the painter can compute. (The modal
// editors' painter — paint_modal_dialog, paint_handler.cpp — advances its
// own editor's offset and publishes its origin the same way; it is a
// GuiPaintHandler method and already holds an AppState&.)
void render_flag_editor_box(cairo_t* cr, AppState& app, const GuiAudio& audio);

// The phase-reset column's cues: the identical cue, the identical class ladder
// and the identical publication contract render_flags documents above. Their
// LABEL is the display-only kPhaseResetLaneToken (a phase reset authors no
// payload). `iteration_on` PAINTS THIS COLUMN'S TWO BOUND CELLS
// (2026-09-09: grid iterations on both columns) — and it is this
// column's own verdict, true only while the lamp was lit HERE: every reset the
// sweep reads (phase_reset_iter_eligible_marker, phaseresetmarkers.h — so a
// disabled reset, which carries no bracket at all, paints no cells) extends its
// label with the LOWER
// bound then the UPPER, each a segment as the warp column's are, carrying the
// bound as a SIGNED WHOLE HOP of the analysis lattice
// (format_phase_iter_bound_cell). The sign is the whole syntax here as it is
// on the warp column, and the ABSENT DECIMALS are what tell a hop cell from a
// cent cell. The selection gives the selected look as there. NO RESET IS
// STRUCK, so it takes no strike set (architect 2026-11-17: "phase resets
// should get no strike b/c they effectively collapse to 1 in the engine
// already"; anything goes on this column).
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
                            SuppressedBox suppressed = SuppressedBox{},
                            const CueLaneHover& hover = CueLaneHover{});

// ONE PREPARED DIFF FLAG — ONE ITEM — for the `h` history mode's lane, in the
// ORDER it is painted and published. The caller (maybe_rebuild_flag_cache)
// resolves the commit's delta into these; this file only paints what it is
// handed, so the diff model's types never reach the renderer.
//
// ONE ITEM, ONE CUE, ONE FRAME (architect 2026-11-11: "the purpose of the
// history coalesce was to prevent a small move from creating two entries in
// history, the later of which obscure the former — we have essentially recreated the same situation"):
// THREE KINDS, read off the two bools — REMOVED ONLY (the older side had the
// line, the newer dropped it), ADDED ONLY (the newer side has it, the older
// did not), and CHANGED (both: a marker changed in place at one frame, its
// payload edited or its disable bit toggled, GuiHistoryWarpChange). A MOVED
// marker is two items, a removed one at its old frame and an added one at
// its new; each is selected, walked, landed on and reverted on its own.
//
// THE FRAME FIELD IS NAMED time_frame ON PURPOSE: it is what the shared column
// mapper iterate_visible_flags_impl reads off a marker, so a diff flag rides the
// EXACT expression a live marker rides — same map, same viewport, same width,
// same nearbyint — rather than a second spelling of it. It is the item's one
// frame, both halves' on a changed item, and what the revert addresses (the
// now row and the then line both at it) and the landing lands on
// (history_diff_flag_landing_domain_frame, app_state.h).
//
// THE LABEL TEXT OF A SIDE, ONE OWNER (history_diff_label, paint_handler.h;
// architect 2026-11-11: "Disabled is supposed to be shown by a hash mark.
// In the history"): the line's flag text — a warp line's payload
// token past the '|', VERBATIM, a phase reset's display token — WITH THE
// DISABLE BYTE `#` LEADING where that side's line is disabled, the file's own
// spelling. `then_label` is meaningful exactly when `removed` is set and
// `now_label` when `added` is; the lane paints them and the cue tooltip shows
// them (history_cue_tooltip_text, app_state.h), the one composition for both.
//
// THE REVERT'S VALUE TRAVELS BESIDE THE LABEL, never parsed back out of it:
// `then_text` is the removed WARP line's payload token past the '|',
// verbatim and WITHOUT the '#', and `then_disabled` that line's local '#'
// byte; the act reconstitutes the sidecar LINE from the frame, the bit and
// this token and hands it to the frozen parser, so the value travels as
// text the loader itself judges and no second grammar is written anywhere
// (GuiInputHandler::run_history_revert). `then_text` is empty on the phase
// column, whose line is frame plus the bit (the revert's phase arm reads
// `then_disabled` alone).
//
// THE LANE'S LOOK IS THE HISTORY'S OWN (architect 2026-11-11:
// "disabled style and strike take on a different meaning in history —
// disabled means a marker was removed, and strike means a text was changed. a
// removed flag always has its text struck by definition"; the rows are
// resolve_flag_face's, render.cpp): a REMOVED item wears the disabled style
// with its label struck, an ADDED or CHANGED item the enabled style — "the
// diff should show as enabled" — whatever its lines' own disable bits, which
// the label's `#` shows. ONE CUE AN ITEM; ITS LABEL ITS SIDES' TEXTS
// (architect 2026-11-11: "What I should see is the label
// reference with a line through it, and then pass next to that. That's how
// it used to work"): a removed item one segment, `then_label`, struck; an
// added item one segment, `now_label`, plain; a CHANGED item TWO SEGMENTS,
// `then_label` struck first and `now_label` plain beside it — always both,
// a disable toggle alone reading `1.05` struck beside `#1.05`. The cue
// tooltip (TIPS) shows the same runs on one line.
//
// EACH HALF NAMES ITS OWN ROW (2026-09-16): `then_ordinal`
// is the removed line's row within its frame's run on the then side,
// `now_ordinal` the added line's on the now side — each meaningful exactly
// when its half's bool is set, both copied off the delta entry the text is
// taken from (the contract is at GuiHistoryWarpEntry::ordinal,
// history_diff.h). IDENTITY IS DATA, NOT A FACE: the painter reads neither,
// and the revert is their one reader — it deletes the exact now-side row and
// re-seats the then line at its then-side ordinal, so a flag on the second of
// two coincident rows reverts that row and not the run's first.
//
// THE DISABLE BITS reach the picture through the labels' `#` alone; the
// chip reads none. NO COINCIDENT CLASS TRAVELS HERE either (recorded
// 2026-09-02): the live lane's red-set strike is a verdict over the whole
// resolved store, and a diff line has no store around it to resolve against.
struct HistoryDiffFlag {
    int64_t     time_frame = 0;       // the item's one frame
    bool        removed    = false;   // the older side had this line
    bool        added      = false;   // the newer side has this line
    std::string then_label;           // the removed line's label text
    std::string now_label;            // the added line's label text
    std::string then_text;            // the removed warp token (the revert's)
    int         then_ordinal = 0;     // the removed half's row within its run
    int         now_ordinal  = 0;     // the added half's row within its run
    bool        then_disabled = false;   // the removed line's '#' (the revert's)
};

// THE HISTORY MODE'S MARKER LANE. Replaces render_flags / render_phase_reset_-
// flags wholesale while the mode stands: no live marker paints, and this pass
// becomes the producer of the same two stashes they produce (out_hit_rects with
// `marker_index` carrying the INDEX INTO `flags`, out_stems the same), so
// hit_test_flag keeps working unchanged and answers a diff-flag index.
//
// THE ANATOMY IS THE LIVE CUE'S (the palette block's marker-lane paragraph):
// every item ONE CUE at its one frame — the live lane's chip, its
// label in the label face — in THE HISTORY'S STYLES (HistoryDiffFlag's
// paragraph above; resolve_flag_face's Removed / Added / Changed rows): a
// REMOVED item the disabled style, the `inactive_cue` chip and the inactive
// label, its one segment STRUCK; an
// ADDED item the enabled style, the `cue_fill` chip (the accent) and the
// text tone, its one segment plain; a CHANGED item the enabled
// style, TWO SEGMENTS in the text tone — the older side's label struck, then
// the newer side's plain, the segment gap between their boxes (the live
// lane's box layout, cue_segment_boxes; architect 2026-11-11). THE LABEL IS
// THE SIDE'S TEXT, its `#` where the line is disabled, no sign ("we could
// also remove the brackets plus minus, because that becomes redundant at
// that point. And those are always hard to see anyways at the font size. A strike through is
// easier to see", 2026-10-26; history_diff_label, paint_handler.h).
// EVERY LABEL IS SIZED BY ITS OWN SHAPED TEXT AND NEVER TRUNCATED (the cull
// bound follows the commit's own widest text; the reasoning is at the paint
// site). A changed item's two segments publish no cell boundary (no bracket
// lives in a commit): its hit rect's label box spans both, and
// hit_test_flag_cell answers Payload over the whole of it.
//
// `focus_index` is the mode's OWN focus (at most one flag, -1 for none)
// and `selected` the mode's OWN
// multi-selection (ordinals into the same list, 2026-08-05). The focus or
// membership SELECTS the item — the focus is the selection's singleton when
// the set is empty, and the revert act reads them the same way — and a
// selected item stands THE STEM, the live lane's rule (resolve_flag_face's
// table: every selected cue, whatever its style; architect 2026-11-11: no
// history-only stem, one item one cue at most one stem), its
// chip and label as at rest. NO RING: nothing on the canvas joins two items.
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
                               const std::vector<WarpFrameMapSegment>* warp_frame_map,
                               const CueLaneHover& hover = CueLaneHover{});

// THE CUE PASS'S OWN CHANGE (architect 2026-11-16, the beats: a rebuild of
// the flag cache damages what it changed and nothing else, so a marker
// drag's frame is a light one — the rule at maybe_rebuild_flag_cache's
// tail, waveform_cache.cpp). The three producers above lay their cues out
// in one kept scratch (render.cpp's CuePassScratch); these two let their one
// caller, the flag cache's rebuild, say WHICH CUES ITS SURFACE NOW SHOWS
// DIFFERENTLY from the pass before:
//   cue_pass_keep_previous — called ONCE, AHEAD OF THE REBUILD'S PASS: the
//     layout the last pass left becomes the previous one, and the present
//     one starts empty (a producer that returns before it lays anything out
//     has painted nothing, and reads so).
//   cue_pass_changed_rects — after the pass: appends to `out` one rect for
//     every cue that DIFFERS between the previous layout and the present
//     one, cues matched by marker index — a cue in one and not the other; a
//     cue whose exact place, chip colors, hover weight (CueDraw's),
//     or any segment's presence, run, box, look or strike differs (EXACT x INCLUDED: the chip and the label
//     stand on it, so a sub-pixel move changes their ink under whole-px
//     rects that stand); the two cues of a pair whose painted order flipped.
//     THE RECT is the hull of the cue's previous and present painted extents
//     together — its chip at the painted reach round its exact place
//     (cue_chip_paint_reach_px, never the hit box) united with its label's
//     occluding box through its last segment's end — cut to the pass's
//     columns [lanes.columns_x, + waveform_width), over the marker lane's
//     rows (the rows the pass's clip holds every cue pixel to).
// WHY THAT IS EVERY CHANGED PIXEL OF THE PASS: a cue paints inside its own
// extent alone — its chip there, its label's ink inside its own box — and
// reaches another cue's pixels only there too (its box hides the earlier
// labels across its own extent, its chip is stamped over what stands under
// it), and the order the cues are painted in is their places' and then
// their indexes', so cues that did not differ keep their order. A pixel
// outside every differing cue's two extents is therefore painted by the
// same cues, alike, in the same order. THE PREMISE IS THE CALLER'S: both
// passes laid out on one surface band, one basis, one scale and one palette
// (the rebuild compares only where its fingerprint's other fields stand,
// maybe_rebuild_flag_cache, waveform_cache.cpp) — the lane, the columns and
// every length here are then the same for both layouts.
void cue_pass_keep_previous();
void cue_pass_changed_rects(FlagLaneRects lanes, int waveform_width,
                            std::vector<GuiRect>& out);

// THE LANE'S HOVER BETWEEN REBUILDS (architect 2026-11-21: the
// marker's hover, a fade of about nine frames an edge, several at once on a
// glide across a dense lane; AppState::FaceFades' THE MARKER'S FACE).
// The pass's kept layout (CuePassScratch) is the surface's content, cue for
// cue, each laid out with THE HOVER WEIGHT IT WAS PAINTED AT; these four
// read and repair it without a rebuild:
//   cue_pass_holds — whether the last pass laid out a cue of key `key` (a
//     cue on the lane, as the surface shows it): the faces' settle drops a
//     marker's record whose cue is no longer on the lane.
//   cue_pass_hovered_cues — how many laid-out cues the surface shows at a
//     weight other than 0, kept beside the layout (no walk): the lanes'
//     at-rest exit reads it (GuiPaintHandler::lane_cues_rest).
//   cue_pass_hover_damage — THE COMPARE, the clock's (run on every 5 ms
//     tick): every laid-out cue whose weight as painted is not `weight_of`'s
//     now has its OWN EXTENT appended to `damage` — the region
//     cue_pass_sync_hover below would repaint for it, found the same way —
//     and NOTHING IS PAINTED: the cue's weight as painted stays the
//     surface's, so the damage invites the frame whose repaint, ahead of
//     it, paints the weights of that moment. THE ONE WRITE: a changed cue
//     whose extent has no column on the pass's (its ink just off an edge,
//     kept in the layout by the conservative cull) has its weight as
//     painted retired to its weight now, the count kept — no pixel of the
//     surface depends on it, and no frame would come to write it
//     (cue_hover_regions, render.cpp; architect 2026-11-21).
//   cue_pass_sync_hover — THE REPAINT, the frame's (run once ahead of each
//     frame, after the owed plate rebuild): EVERY LAID-OUT CUE WHOSE WEIGHT AS PAINTED IS NOT
//     `weight_of`'s NOW IS REPAINTED INTO `surface` IN PLACE, and nothing
//     else: each such cue's OWN EXTENT — its chip's painted reach round its
//     exact place (cue_chip_paint_reach_px) and its label's ink as cut, the
//     hull of the visible intervals no later label's box covers (paint_cues'
//     cut) — is united with any such neighbor's it meets, each union's whole
//     columns over the lane's rows are cleared, and every laid-out cue whose
//     extent meets them is painted again there in the pass's own order —
//     the labels left to right on their own cuts, then the chips — under
//     the pass's clip and that region's, so THE REGION'S BYTES ARE THE ONES
//     A WHOLE REBUILD AT THOSE WEIGHTS LEAVES (a cue outside it lays no
//     pixel there: its ink stays inside its own extent, the pass's own rule
//     at cue_pass_changed_rects), and a hovered label is not raised over its
//     neighbors; each repainted cue's weight as painted is then its weight
//     now. `surface` is the flag cache's, its row 0 client row `origin_y`
//     (FlagCache's head, paint_handler.h). Each region is appended to
//     `damage` in client coordinates — the lane's rows, the region's whole
//     columns cut to the pass's: the marker's own painted extent and nothing
//     else of the lanes.
// A CUE'S EXTENT IS ITS LAYOUT'S, NEVER ITS WEIGHT'S, so the compare's
// rects are the repaint's on the same weights. THE PREMISE: the surface
// holds the last pass's picture (the caller's: the rebuild's record stands
// and its band is the live one), whose lane, columns and lengths the
// scratch keeps beside the cues.
bool cue_pass_holds(const CueKey& key);
int  cue_pass_hovered_cues();
void cue_pass_hover_damage(
        const std::function<double(const CueKey&)>& weight_of,
        std::vector<GuiRect>& damage);
void cue_pass_sync_hover(cairo_surface_t* surface, int origin_y,
                         const std::function<double(const CueKey&)>& weight_of,
                         std::vector<GuiRect>& damage);

// THE ONE COMPOSER FOR WARP FLAG TEXT (defined in render.cpp): the canonical
// line's payload WHOLE — the tempo's derived base and its every deviation
// term, `*scale`, `:label`, or the pass / ref forms — and never a bracket.
// NOTHING HERE IS EVER CUT, and its first reader is why: the flag editor
// seeds its field from it (enter_top_flag_edit, flag_editor.cpp; its paste
// onto the selection the same), so what the field opens with is what the
// store holds — a cut seed would let a commit throw away a scale's digits or
// a chain's terms the user never touched. Its other reader is the cue
// tooltip's cut arm (marker_tooltip_text, app_state.h; architect
// 2026-11-17), which shows this whole line where the painted label below is cut
// — a cue is cut exactly when the two composers differ. (The `j`
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
// kMaxTempoDeviationTerms — sixteen (value_format.h) — the worst box paints
// about ninety glyphs and paints every one of them. It is a known price and
// not a surprise. Nothing is laid out against it: marker_flag_max_width_px
// above is a CULL bound derived from the same constant, and a flag too wide
// for the window runs off its right edge exactly as any other over-wide flag
// does. The chains he expects to author are one to four terms long, and the
// cap is the headroom above that rather than a shape to plan for.
//
// TWO READERS SHAPE IT, and they must be exactly two: the warp column's flag
// pass (render_flags' label lambda) and the BOUND CELLS' SEAM MEASUREMENT
// (committed_cell_seam_off), which shapes this same string to find where a
// marker's first cell begins. Measuring the UNCUT composer there would open
// a bound field at a column no cell stands on — the one place where the
// wrong composer is invisible until a cell is in the wrong place. A THIRD
// READER ONLY COMPARES it with flag_text, the cue tooltip's cut test
// (marker_tooltip_text, app_state.h; architect 2026-11-17): a cue
// whose two answers differ shows its whole line in the TIPS box.
std::string flag_display_text(const std::vector<GuiWarpMarker>& markers,
                              int idx);

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
// THE WORD ITSELF (architect 2026-09-17): the flag says what the marker IS.
// A single `p` names nothing to a reader who has not been told (architect
// 2026-08-18: "phase reset flags should read 'phas...'"), and `reset` reads
// whole where the two words `phase reset` truncated to `phase res...`.
// Nothing cuts it: every flag's width is pad + shaped(label) + pad,
// re-derived from the shaping pass, and marker_flag_max_width_px bounds the
// left cull at the WARP payload's worst case, comfortably past this token,
// so the box simply narrows with the word. Its readers call the constant: in
// render.cpp the flag painter and the bound cells' SEAM MEASUREMENT, which
// shapes this same string to find where a reset's first iteration cell
// starts — so the cells move with the word and the two cannot disagree —
// and the history view's lane (waveform_cache.cpp). The flags simply
// overlap, later over earlier.
inline constexpr char kPhaseResetLaneToken[] = "reset";

