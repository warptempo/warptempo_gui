#pragma once
#include "warpmarkers.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment for target-view waveform
#include "waveform_gain.h"    // WaveformGainCurve, the waveform picture's gain
#include "gui_font.h"         // GuiFont, the face owner's face at a scale
#include "chrome_spec.h"      // live_chrome_spec(), the live chrome's lengths
#include "program_spec.h"     // kProgramSpec, the program's lengths (the band, row 8)
#include "display_transform.h" // display_color's transform (sRGB -> the window's space)

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
// well or on a thing that enters it (the waveform's canvas, ink and outline,
// the canvas's grid and center line, the cues' two colors, the playhead and
// the scanner, and Cool Edit's panel face as its
// record; palette_file.h's head owns the rule that parts program from
// chrome) — the WAVEFORM PRESET, the one choice of color left, its ink's hue
// also THE ACCENT of the lit LEDs (materials.h's accent_of_ink); THE PANEL'S
// FACE is the brushed ground's mean (materials.h's TONE RULE), and THE
// CHROME'S ROLES are the skin's constants and the two ring tones off that
// mean (chrome_derive.h's head, the per-role table; Cool Edit's tones off
// the Face 2026-10-10 to 2026-10-23). The chrome's roles and their order are
// the role table (kGuiThemeRoles, theme_file.h). THE APP PAINTS NO CAPTION
// since 2026-10-24 (THE CAPTION's note, below): its six roles, the
// `caption` device key and its 42 built-in schemes left 2026-10-21, its
// flat skin constants with the caption itself.
// The palette's roles are kGuiPaletteRoles and the compiled built-in
// palettes the program's own twenty presets, Cool Edit Pro 2.1's twenty
// as they are, kGuiBuiltinPalettes
// (palette_file.h); the
// active palette is the device config's `waveform` — a built-in's key,
// chosen by the Settings Waveform row (architect 2026-10-14 and 2026-10-21;
// files from a `chromes/` folder were read 2026-10-07..14; the key was
// `chrome` until 2026-10-21), the first-run default "Default"
// (2026-10-26). There is NO LEVEL (architect 2026-10-04): a dark look, like
// any look made official, would be a chrome variant of its own
// (2026-10-08). The app computes no color but THE SKIN'S RULES: the ring
// tones and the accent (materials.h) and Cool Edit's tones off the Face for
// what the program still derives (cool_edit_derive.h, the COOL EDIT BLOCK).
// EVERY COLOUR A PAINTER HANDS CAIRO is one of GuiPalette's fields through the
// one accessor palette() — or, since 2026-10-09, ONE OF THE PROGRAM'S
// PAINTER CONSTANTS (cool_edit_paint.h: the view bar's black field, the
// ruler's ticks and digits, and the black shadow under the ticks) — or THE
// HI-FI SKIN'S CONSTANTS AND GROUNDS (materials.h, 2026-10-23:
// the bone, legend and dim inks, the ruler's and the fields' grounds, the
// selection pair, the key's face, edges and LED, the accent, and the baked
// brushed grounds through set_surface_source) — or THE ICONS' OWN INKS
// (architect 2026-10-06): the drawings' colors as each file names them
// (icons.h), artwork, not roles, so a drawing is never recolored by a
// theme — the one place an antialiased picture is copied whole; NO KEY
// DRAWS ONE since 2026-10-23, and no drawing is ever shown disabled.
// TEXT OVER A FILL IS THE FILL'S OWN PAIR — a text role beside every ground
// role: the bone over the ground and the field, the legend ink under the
// keys and in the tooltip, the bone over the selection's grey.
// STILL OPAQUE, STILL NO PAINT-TIME COMPOSITING, NO ROUNDED CORNERS, NO
// HOVER FACE ON ANY BUTTON: the keys have none and the chrome's buttons
// never had one; the lists' lit rows are a selection, not a button face.
// Every colour is a solid fill of integer cells — EXCEPT THE CHROME'S
// SURFACES ON A GROUND (materials.h: a baked tile copied 1:1, opaque,
// nothing blended at paint time, every line its tone's own solid) and the
// key's patch (a lit LED's glow composited at the bake). (The color
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
// ebony case (the popup lists' scroll track 2026-10-14, the trim track's
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
// presents; the laptop's untagged sRGB surface takes the bytes as they are.
// From 2026-10-02 until this ruling the bytes went to the P3 layer
// unconverted, a hex then being a P3 triple.
//
// THE GRAMMAR WAS WINDOWS 95's DrawEdge, AT THE WINDOWS PIXEL (architect
// 2026-10-02: every raised or sunken edge TWO lines a side, each one
// Windows px), and IS ONE LINE AT ONE DEVICE PX since the hi-fi skin and its
// sweep (2026-10-23; hairline_px, below), each ring's two tones MITRED where
// they meet (architect 2026-10-06, overruling Windows' square join: the
// top-right and bottom-left corner blocks split along the 45-degree
// diagonal, the rule and its words at the mitre, paint_hairline_frame,
// render.cpp). RELIEF COLOURS ARE SET IN ONE PLACE (architect 2026-10-04): every
// edge below takes its quartet by Windows 95's scheme of how many lines a
// side, and no other role draws a relief line. THE FAMILIES, lines given
// outer then inner, top-left / bottom-right, each with its painter and its
// wearers:
//   (SOFT RAISED, SOFT SUNKEN and PLAIN RAISED — the caption buttons, the
//                 scrub's pointed thumb and the restored window's sizing
//                 frame, Windows 2000's Standard quartet — went 2026-10-23
//                 with those parts: the hi-fi skin's keys,
//                 cool_edit_paint.h; its caption and border went in turn
//                 2026-10-24, labwc's);
//   (PLAIN SUNKEN, Shadow / Hilight, DkShadow / 3DLight (EDGE_SUNKEN) —
//                 paint_relief_plain_sunken, the scrub's channel's until it
//                 became two hairlines — went 2026-10-23 with the sweep);
//   THE HAIRLINE RING (2026-10-23, paint_hairline_frame) — one device px a
//                 side in the ring tones: the modal box, the cards and the
//                 tooltip, the time fields, the canvas's wells, the view
//                 bar's span;
//   (SUNKEN OUTER, ONE line, Shadow / Hilight — the open menu title and
//                 the program's frame's ring — went 2026-10-21 with both,
//                 the frame's top row alone a flat Shadow line until it
//                 went too, 2026-10-22);
//   (ETCHED, a Shadow line with a Hilight line beside it — the menu
//                 separator — went with the Settings popup, 2026-10-21.)
// (The toolbar band, row 8 and their cases and time fields, the view bar,
// the ruler and the marker lane are the PROGRAM'S since 2026-10-09 — Cool
// Edit's, cool_edit_paint.h — and wear none of these families; THE MITRE
// reaches them, architect 2026-10-09 ~12:40, through paint_hairline_frame
// since 2026-10-23: cool_edit_paint.h's head.)
// THE CARDS' AND THE TOOLTIP'S RING is the one-device-px sunken ring in the
// ring tones (THE CARD FACE, below); the `card_frame` role is painted by
// nothing since 2026-10-23 (its one flat line, 2026-10-04 to 2026-10-23).
//
// THE MAPPING (architect 2026-10-03; the roles 2026-10-04), role -> what it
// paints:
//   ground        every chrome surface: the chrome's lanes
//                 under the band, the on-screen
//                 keyboard's band (inside the program frame since 2026-10-14;
//                 the dialog's taskbar ground beside it 2026-10-10 to
//                 2026-10-14) — every chrome button face, a
//                 DISABLED cue's triangle (the band, row 8, the canvas
//                 column's lanes and the modal family's box are the program's
//                 panel since 2026-10-09, below);
//   label         chrome text and glyphs, a prompt's question in the modal
//                 family's box;
//   the quartet   every relief line, the families unchanged; Hilight also the
//                 light copy of THE DISABLED EMBOSS (below), a disabled cue
//                 label's included (the taskbar overlays' one top line,
//                 2026-10-10 to 2026-10-14, is retired); DkShadow also the
//                 dialog focus frame until it retired (2026-10-23);
//   selected pair the folder
//                 overlay's and the picker's highlighted row (its name; the
//                 glyph keeps its own inks, icons.h), the selection
//                 band and selected substring of every text field, the flag
//                 editor's included, and THE SELECTED CUE'S LABEL — its text
//                 alone, on the panel's face (THE MARKER LANE, below) — ONE
//                 PAIR, FOCUSED OR NOT (the 2026-09-02
//                 inactive selection face retired, architect 2026-10-03, on
//                 Windows 95's Network Neighborhood, where a selection keeps
//                 Hilight / HilightText in a window that has lost the focus);
//   field pair    the modal dialogs' fields, the caret its field's text
//                 (architect 2026-10-03); THE LIST — the folder overlay's
//                 band under the player and the picker, Windows 95's list
//                 view, its resting names and its glyphs' text-class paths
//                 in the field text (architect 2026-10-06,
//                 paint_folder_overlay) — and THE FLAG EDITOR'S FIELD, its
//                 ground, its one-quantum outline, its text and its caret
//                 (architect 2026-10-09; EDITING, below);
//   clock pair    UNREAD BY ANY PAINTER since 2026-10-09: the time fields
//                 are the program's, Cool Edit's dark field
//                 (paint_ce_time_field); the pair stays in the role table
//                 and the derivation (Windows' status bar's ButtonFace /
//                 ButtonText, the Face and the program text since
//                 2026-10-10);
//   card trio     the tooltip and every notification card (THE CARD FACE,
//                 below);
//   (THE CAPTION and THE WINDOW BORDER are labwc's since 2026-10-24, the
//                 app painting no window chrome — the caption's six roles,
//                 a scheme's start, gradient end and text, active and
//                 inactive, 2026-10-05..10-21, then the skin's flat
//                 constants and the border's ring-light line until then);
//   THE PALETTE   the program's TEN (palette_file.h, 2026-10-07; ten
//                 since 2026-10-26): the waveform's canvas, ink and outline
//                 (ROW 6, below; the view bar's span wears none of them,
//                 materials.h's neutral trio since 2026-10-25); THE
//                 CANVAS'S TWO LINES, Cool
//                 Edit's — `grid` under the waveform and `center` on each
//                 channel's zero, over it (ROW 6); THE CUES' TWO COLORS,
//                 Cool Edit's —
//                 `cue` the red and `range` the blue, the triangles and
//                 their dots, every cue's (the invalid color, a third
//                 role 2026-10-09 to 2026-10-26, retired: the strike is
//                 the mark, THE MARKER LANE below); the playhead's role
//                 `playhead_stem` — its head in the ruler and its dots in
//                 the canvas, nothing else (THE PLAYHEAD, below); the
//                 `scanner` — the playback line AND THE ZOOM ANCHOR'S STEM,
//                 the white of the stems that belong to the controls
//                 (architect 2026-10-09); and THE
//                 PANEL'S `face` (2026-10-09: the program's band, row 8
//                 and the canvas column's lanes — Cool Edit's — every
//                 other panel tone derived from it, the COOL EDIT BLOCK,
//                 cool_edit_derive.h).
// THE CARD FACE (architect 2026-10-04, the period's tooltip; the hi-fi skin
// 2026-10-23): the tooltip, both of its lines, and every notification card
// stand on `card_ground` — THE FIELD, kFieldGround — INSIDE A ONE-DEVICE-PX
// SUNKEN RING in the ring tones, dark top and left, light bottom and right,
// mitred (paint_popup_chrome, paint_hairline_frame), the cards' words the
// bone (`card_text`) at the body size, THE TOOLTIP'S the flags' own program
// face (2026-10-23, "the same as the flags", 22 px since the flags' size
// settled 2026-10-26; the legend face its first night) in the bone
// kBoneInk like the cards' words (2026-10-23 night; the legend ink
// kLegendInk until then). (Windows 2000's one-W `card_frame`
// border, square, on the Face, then the wood, 2026-10-04 to 2026-10-23.)
// THIS PARAGRAPH IS THE RULE'S ONE STATEMENT.
// THE CANVAS'S FRAME IS THE PROGRAM'S (architect 2026-10-09, Cool Edit's
// canvas, the same under every chrome): a ring of the panel's tones round
// each channel's well — dark top and left, light right and bottom, mitred —
// TWO WELLS with one row of the face between them since 2026-10-16 (Cool
// Edit's stereo divider, waveform_divider), with no chrome edge
// (paint_canvas_column_frame, paint_handler.cpp; the chrome's sunken well
// stood until 2026-10-09).
//
// THE DISABLED FACES (the hi-fi skin, 2026-10-23): a dead key DIMS ITS
// PRINTED LEGEND (kDimInk, paint_key_legend), its key the face its state
// gives; a CUE'S inert label is the faded tone (the
// marker-lane paragraph). Nothing else dims. (Windows' word emboss —
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
// EVERY COLOUR THIS PRODUCT HANDS CAIRO GOES THROUGH ONE OF THESE THREE, and
// no site calls cairo_set_source_rgb itself (re-grepped 2026-10-03: none
// outside render.cpp's bodies): set_palette_source for the chrome's INK —
// every word, glyph cell and mark, and every surface of the flat chrome —
// set_surface_source for the chrome's SURFACES ON A GROUND (2026-10-21; the
// hi-fi skin's grounds 2026-10-23, materials.h: under a painter's Ground or
// Marker scope the palette's `face` is that ground's baked tile, and any
// other color, or any color outside a scope, flat through
// set_palette_source), CALLED WITH THE FILL'S PATH ALREADY BUILT (the
// tile is the one under the path) — the cell fills and every relief
// line (paint_cell_rect, paint_hairline_frame and their family) — and
// set_waveform_source for the canvas's cairo fills, its ground and grid
// (render_canvas) and its center lines (render_center_lines). Each converts
// the authored sRGB color for the window (display_color, the painter's
// entry; a material's tiles at their bake) and hands it over; the plate's own
// pixels are written as words (argb32_opaque_word), not through cairo,
// converting at their own entry. TEXT
// takes its ink from the source these set: no glyph road sets a color of its
// own (text_shape.cpp draws in the current source), and every text road
// sets its ink through set_palette_source, never a surface's.
void set_palette_source(cairo_t* cr, GuiColor c);
void set_surface_source(cairo_t* cr, GuiColor c);
void set_waveform_source(cairo_t* cr, GuiColor c);

// -- THE ACTIVE PALETTE ---------------------------------------------------------
//
// The resolved colors, ONE STRUCT (2026-10-07): the chrome's ROLES —
// derived from the panel's Face since 2026-10-10, the selection pair a
// constant since 2026-10-21 (chrome_derive.h) — and the active palette's program roles, one
// field each — the chrome's in kGuiThemeRoles' order (theme_file.h), the
// program's in kGuiPaletteRoles' (palette_file.h), and THE COOL EDIT BLOCK
// derived from the program's `face` (cool_edit_derive.h's painted tones,
// 2026-10-09) — the three tables covering the struct exactly
// (palette_file.cpp's static_asserts); the mapping above says what each
// paints. Every field is a solid byte triple.
struct GuiPalette {
    // THE CHROME (Windows' element names; the values Cool Edit's tones,
    // chrome_derive.h).
    GuiColor ground;          // COLOR_3DFACE
    GuiColor label;           // COLOR_BTNTEXT
    GuiColor hilight;         // COLOR_3DHILIGHT
    GuiColor light_3d;        // COLOR_3DLIGHT
    GuiColor shadow;          // COLOR_3DSHADOW
    GuiColor dk_shadow;       // COLOR_3DDKSHADOW
    GuiColor selected_fill;   // COLOR_HIGHLIGHT
    GuiColor selected_text;   // COLOR_HIGHLIGHTTEXT
    GuiColor field_ground;    // COLOR_WINDOW
    GuiColor field_text;      // COLOR_WINDOWTEXT
    GuiColor clock_ground;    // the status bar's ButtonFace
    GuiColor clock_text;      // the status bar's ButtonText
    GuiColor card_ground;     // COLOR_INFOBK
    GuiColor card_text;       // COLOR_INFOTEXT
    GuiColor card_frame;      // the tooltip's border (COLOR_WINDOWFRAME)
    // THE PROGRAM'S OWN ELEMENTS — THE PALETTE (kGuiPaletteRoles,
    // palette_file.h).
    GuiColor waveform_canvas;
    GuiColor waveform_ink;
    GuiColor waveform_outline;
    GuiColor grid;
    GuiColor center;
    GuiColor cue;
    GuiColor range;
    GuiColor playhead_stem;
    GuiColor scanner;
    // THE PROGRAM'S PANEL (architect 2026-10-09, the program is Cool Edit):
    // Cool Edit's Face, the ground of the toolbar band and row 8 — THE
    // BRUSHED GROUND'S MEAN since 2026-10-23 (materials.h's TONE RULE; its
    // estimate before the first bake), whatever the preset: the ring tones
    // and the chrome's roles derive from it, and painted as a surface it is
    // the scope's ground as drawn (set_surface_source).
    GuiColor face;
    // THE COOL EDIT BLOCK (cool_edit_derive.h's kTones, the painted ones):
    // the panel's tones, each DERIVED from `face` by Cool Edit's own HLS
    // rule at every install of the program's words; no file names them;
    // read by the
    // program's painters alone (cool_edit_paint.h, the canvas column's lanes
    // in render.cpp and paint_handler.cpp).
    GuiColor ce_mid;
    GuiColor ce_dark;
    GuiColor ce_hilight;
    GuiColor ce_field_dark;
    GuiColor ce_field_light;
    // (The program's time fields' digits, the never-swapping light text
    // `ce_field_text` 2026-10-10..10-21, are the hi-fi skin's bone since
    // 2026-10-23, materials.h's kBoneInk.) IN THE HI-FI SKIN `ce_dark`,
    // `ce_hilight` and the two field tones are the RING TONES off the
    // ground's mean and `ce_mid` the ruler's flat kRulerGround
    // (render.cpp's fill_program_palette, materials.h's TONE RULE).
    // the panel label's ink and its (+1, +1) shadow, swapped to the engraved
    // pair past the label threshold (2026-10-10; cool_edit_derive.h's
    // label_ink / label_shadow, not the tone table)
    GuiColor ce_label;
    GuiColor ce_label_shadow;
    GuiColor ce_cue_shadow;
    // (THE BAND'S RECESS AND THE TAB STRIP'S FOUR EDGE TONES left 2026-10-22
    // with the band's void and the strip, the simple layout;
    // THE BUTTON CASE's twenty-four members, its ramp, highlight, shadow ends
    // and corner off the Face 2026-10-10..10-21, the ebony's, materials.h's
    // CASE.)
    // THE DISABLED CUE'S FADED LOOK (2026-10-10; cool_edit_derive.h's
    // disabled_* mixes toward the Face): the triangle of the `cue` red, the
    // cue shadow, and the resting label.
    GuiColor ce_off_cue;
    GuiColor ce_off_cue_shadow;
    GuiColor ce_off_label;
};

// THE ONE ACCESSOR every painter reads. The installed palette is file-scope
// state in render.cpp, written by the install family below alone:
// install_palette at its ONE application point, gui_main's startup, before
// the window exists; install_program_palette, the Settings Waveform row's
// live road; and install_true_colors. Before the first install it is
// construction state (black), never painted.
const GuiPalette& palette();

// THE SURFACES WHOSE TEXT ROLE MAY DEPEND ON THE CHROME (2026-10-09): each
// surface's recorded text color under the live chrome — "text over a fill
// is the theme's recorded pair" — read by the words painted there, the one
// owner of the choice a later vocabulary with roles of its own would fork
// (no glyph reads it since 2026-10-09: the icons wear their own inks,
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

// Install every color: THE PROGRAM'S off `program`, the waveform preset's
// ten (2026-10-07; the device config's `waveform` key names the preset
// at launch — a built-in's key, is_builtin_palette_name, gui_main resolving
// it with palette_record, palette_file.h) — and THE CHROME'S, DERIVED FROM
// THE PANEL'S FACE AND THE INK (2026-10-10, the chrome inherits Cool Edit;
// the Face a material's once the materials are baked, 2026-10-21, the
// wood's since 2026-10-22;
// live_chrome_words, chrome_derive.h).
// THE LIVE WORDS ARE THE TRUTH WHILE THE PROCESS RUNS: the config's
// `waveform` key names THE ACTIVE PRESET and gives the words at launch; the
// Settings Waveform row's commit installs a preset's words live and writes
// its key (GuiSettingsEditor::commit_device_setting, architect 2026-10-14).
// This resolves and never refuses. It bumps palette_generation below, the
// flag cache's fingerprint term, and the plate's two baked inks
// (waveform_plate_inks) move with it. The words type is palette_file.h's.
// (The caption's six were a picked scheme's — a `caption` key, its row and
// a second argument here, with install_chrome_pick and live_chrome_pick
// beside — 2026-10-08..10-21.)
void install_palette(const std::array<uint32_t, 10>& program);

// THE LIVE WORDS AS INSTALLED — the program's ten the install family
// last wrote (install_palette's `program`, or install_program_palette's
// `words`), in kGuiPaletteRoles' order (palette_file.h's GuiPaletteWords;
// spelled as its array type here because palette_file.h includes this
// header, which static_asserts the 10 against its table). They live beside the installed struct, FILE-SCOPE IN render.cpp
// AND NOT ON AppState (2026-10-07): AppState is rebuilt at every project
// reopen (gui_main's loop) while the installed palette is not — so a reopen
// keeps them. ITS READER: the materials' re-install at a bake
// (materials.cpp's bake_now, the panel's Face moving under the same words).
const std::array<uint32_t, 10>& program_palette_words();

// (THE INSTALL FAMILY'S SECOND MEMBER, install_program_palette — the
// program's ten alone, the Waveform row's live road — is declared in
// palette_file.h beside the words it takes and defined in render.cpp beside
// install_palette.)

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
// window's words); the materials' tiles baked again, the
// conversion state in their key (materials::ensure, the caller's apply
// shape below). THE APPLY SHAPE THE
// CALLER OWES, after the call (GuiInputHandler::apply_true_colors; the
// launch's call, before the window and every cache exist, owes none): the
// icon rasters dropped (icons::drop_rasters — they convert at raster time
// and key on the device px alone), the materials baked again
// (materials::ensure, before the flag cache reads them), the plate kicked when its inks moved and
// the flag cache refreshed otherwise (install_program_palette's shape,
// palette_file.h), then the whole window invalidated — every color changes.
void install_true_colors(bool on);

// -- THE CANVAS COLUMN: THE VIEW BAR, THE RULER, THE PLAYHEAD -------------------
//
// THE CANVAS COLUMN IS COOL EDIT'S, THE SAME UNDER EVERY CHROME (architect
// 2026-10-09, the program is Cool Edit; program_spec.h owns every
// length): THE VIEW BAR, THE RULER FLIPPED and THE MARKER LANE OF CUES
// directly under the band, THE CANVAS under them in its frame (row 6's
// canvas paragraph), all four inside the program ring 6 W in from each side
// on the brushed ground (the simple layout, 2026-10-22; the hi-fi skin
// 2026-10-23). Its colors are
// the ring tones and the skin's constants, the waveform's ink,
// the cues' two colors and the playhead's color
// (the palette), and the painter constants Cool Edit draws whatever its
// preset (cool_edit_paint.h).
//
// THE TRIM BAR IS COOL EDIT'S VIEW BAR (render_trim_flags
// owns the drawing, trim_lane_h_px the rows): a BLACK field 15 px tall at
// 300 % (kCeViewBarField; view_bar_field_h_px, 2026-10-24) under THE PROGRAM RING's top line, between its dark LEFT
// and light RIGHT columns (the canvas ring's own, carried up the whole
// column; architect 2026-10-22), the `ce_hilight` line under the field (the
// ruler's top line) — every line ONE DEVICE PX on its slot's outer edge, the
// slot's other rows the field (2026-10-23, hairline_px);
// (Cool Edit's own sunken ring of `ce_mid` and `ce_hilight` 2026-10-09 to
// 2026-10-22); and in the field THE SPAN, the trim window's columns as
// a RAISED BLOCK OF NEUTRAL GREY (architect 2026-10-25, "the trim bar should
// be a neutral gray"; no preset touches it) — its top row and left column
// kSpanLight, its body kSpanFace, its bottom row and right column kSpanDark
// (materials.h's trio, the key's face lifted and the TONE RULE off it), the
// two mitred where they meet, every line one device px since 2026-10-23
// (paint_hairline_frame; the mitre 2026-10-09 ~12:40). NO PLAYHEAD IN
// THE BAR (architect 2026-10-09 ~14:30, "I thought we agreed no dots on the
// trim bar"): Cool Edit's period-2 cursor
// column over its span is not drawn, so the bar is the black
// field and the span's block alone. No track, no caps drawn, no pressed
// face: the lane's grabs are the span's two ends and its body (TrimBarHit,
// below). (Windows' miniaturized scroll bar with its checker track and its
// arrow-button caps stood 2026-10-02 to 2026-10-09; git history.)
//
// THE RULER IS COOL EDIT'S, FLIPPED (paint_ruler_row): the FLAT kRulerGround (`ce_mid`, 2026-10-23)
// under the view bar's light line, its own `ce_hilight` line along its
// foot, between THE PROGRAM RING's dark LEFT and light RIGHT columns, which
// run unbroken past the foot — every line one device px (hairline_px);
// THE TICKS kCeRulerTick (the bone,
// face-independent in every preset), ONE DEVICE PX wide over a one-px
// black shadow at (+1, +1) (2026-10-23), STANDING ON THE
// GROUND'S BOTTOM ROW and rising into it, painted over the digits where a
// major's top row meets their baseline; THE DIGITS the ruler face (GuiFace::Small,
// 16 px at 300 % since 2026-10-24, "R16" — R17 the night before, 15 the
// skin's first night,
// "the timestamps are good"; the base's six-row digit 2026-10-09 to
// 2026-10-23) standing on the authored baseline
// row 7 under the air above, "basically touching the trim bar", in the
// tick's ink, BARE (architect 2026-10-25, on the glass: the black
// one-device-px shadow under the timestamps "is basically useless because
// it's so dark, it has no effect") —
// the ground 11 W where Cool
// Edit's is 17, the six rows the marker lane's (program_spec.h's ruler
// fields).

// THE PLAYHEAD IS COOL EDIT'S CURSOR (architect 2026-10-09):
// ITS HEAD the cue's triangle, THE SAME DRAWING — its rows (cue_triangle_rows,
// 8 at 300 %) in whole device cells, Cool Edit's aliased drawing
// (2026-10-26), the apex the playhead's column (paint_ce_cue_triangle; the
// triangle's cell one device px, architect 2026-10-26 ~10:10: "All
// triangles unscaled", cue_triangle_unit_px) — in THE PALETTE'S
// `playhead_stem` (architect 2026-10-09 ~11:50; Cool Edit's Curs yellow
// FFFF00 on every built-in but the CEPro 1.2 pair's FFFF6C) over the same
// triangle one label quantum right and down (2026-10-26 ~13:05, the head's
// paint clipped to its own rows, so the shadow's bottom rows are cut)
// in `ce_cue_shadow`, ON THE RULER'S LAST `rows` DEVICE ROWS, the apex on
// the ground's bottom row — it covers what stands under it (2026-10-09
// ~21:00)
// (paint_ruler_row; cue_triangle_h_px's rows; its painted columns, the cull
// and the move's damage box one extent, cue_triangle_reach_right_px); IN THE
// CANVAS a DOTTED COLUMN, one device px every four device rows in the same
// color (2026-10-09 ~14:25, "on waveform → unscaled") on the canvas's rows
// ≡ 1 (mod 4), the cues' two colors on rows ≡ 3 and
// ≡ 7 (mod 8), so they interleave on one column (paint_playheads,
// dotted_waveform_line_runs; the stereo divider's rows carry none). Across the view bar (architect 2026-10-09
// ~14:30, "no dots on the trim bar"), the marker lane and the canvas's top
// frame row it draws nothing (Cool Edit puts no dot on its frame row). IT MAY STAND ON A MARKER'S COLUMN — NO SNAP, NO AVOIDANCE (architect
// 2026-10-09): the head and a cue's triangle stand in different lanes and
// the dots interleave, so the coincident-stem suppression of 2026-08-01 to
// 2026-10-09 (the playhead's stem yielding whole to a marker's) is retired.
// THE HEAD IS GRABBED AND DRAGGED, as Cool Edit's is (architect 2026-10-09
// ~17:40): its HIT is the head as painted, its columns (the triangle and its
// shadow, cut to the waveform's) over its device rows of the ruler's,
// no box beyond them — published by paint_ruler_row
// (AppState::playhead_head_hit) and read by point_on_playhead_head
// (input_pointer.cpp), a cue over the head keeping the press; a plain press
// there drags the playhead, every motion past the slop the ruler's
// placement act at the pointer's column (ScrollDragState::head_drag).
// NO HOLD LAMP (architect 2026-10-05): the head paints the same whether or
// not AppState::camera_hold stands. (WordPad's ruler marker in the chrome's
// four roles stood 2026-10-05 to 2026-10-09; the solid `playhead_stem` line
// before it; git history.)
// THE PLAYHEAD ALONE WEARS ITS COLOR: `playhead_stem` is its head and its
// dots and nothing else (architect 2026-10-09). THE STEMS THAT BELONG TO THE
// CONTROLS ARE WHITE, ONE ROLE, `scanner` (architect 2026-10-09: "the zoom
// stem and the scanner are both white, which unifies them: the non-playhead
// stems that are related to the controls should be white, and then the
// playhead is yellow"): THE SCANNER — the moving playback line,
// paint_scanner, its own role since 2026-10-05 — a SOLID line one device px
// wide over the canvas (Cool Edit's playback cursor is a solid 1-px line,
// measured 2026-10-09; unscaled on the canvas since ~14:25,
// waveform_line_px's rule), and THE ZOOM ANCHOR'S STEM
// (render_strip_anchor_stem), the same solid one-px line at the pivot;
// WHITE in every built-in palette — COOL EDIT'S OWN CONSTANT (architect
// 2026-10-16, observed in Cool Edit Pro 2.1 under Wine on the laptop: the
// scanner is white on every preset he tried, so its preset text records no
// key for it; the zoom anchor's stem is the same design, so both are
// canonical), his rulings of 2026-10-07 ("let's go back to a white scanner")
// and 2026-10-09 now its confirmation.

// -- THE MARKER LANE: COOL EDIT'S CUES ---------------------------------------------
//
// THE FLAG IS COOL EDIT'S CUE (architect 2026-10-09; the lengths program_spec.h's,
// the drawing render_flag_boxes_impl's): on the lane's PANEL FACE, THE
// TRIANGLE — COOL EDIT'S STAIRCASE IN WHOLE DEVICE CELLS, ITS ROWS SCALED
// (architect 2026-10-26, paint_ce_cue_triangle's head; cue_triangle_rows: 8
// at 300 %, 5 at 200 %, Cool Edit's 9 by 5) on the lane's last rows, Cool
// Edit's aliased drawing (architect 2026-10-26), its apex the marker's
// column on the lane's last row, directly
// above the canvas's top frame row — over the same triangle one label
// quantum right and down in `ce_cue_shadow`, the lane's rows clipping it
// (2026-10-26 ~13:05);
// THE LABEL — the marker's one label, at rest
// in the bone kBoneInk (materials.h's INKS), ITS TEXT OVER A SHADOW IN THE
// TRIANGLE'S OWN SHADOW COLOR, ONE LABEL QUANTUM RIGHT AND ONE DOWN — 3 and
// 3 device px at 300 %, scaled, the flag's own offset since 2026-10-26
// ~13:05 (architect 2026-10-26 ~10:50: "text shadow should stay, and be GUI
// scaled so different from the flag. 1 device px at any font size is
// meaningless for a shadow"; ~11:30, "shadow on … check the offset", the
// down the planner's reading, confirmed on the glass 2026-10-26 ~12:10;
// the product departs from Cool Edit, which
// draws none; `show_cue_label`, render.cpp, the one painter of every label
// look on the face, the one shadow under every look, the faded inert
// label's included; the flag editor's field text takes none; the shadow is
// ink, never extent), in the program face at 22 device px at 300 %
// (gui_font.h's kGuiFaceSizes; architect 2026-10-26 ~13:05, the tuning
// settled), starting the triangle's reach right plus a gap of 8 device px
// at 300 % right of the column (cue_label_lead_px, the gap scaled: 19 at
// 300 %), ITS CAP BAND CENTRED IN THE LANE (architect
// 2026-10-26 ~11:30, "check the text vertical alignment — consider that we
// only use a narrow range of chars"; cue_baseline_px): at 300 % the
// baseline row 33 of the 51, the cap band on rows 17.9 .. 33, its
// descenders reaching rows 33 .. 37, clear of
// the triangle's rows 43 .. 50 — THE LANE 17 W, the product's own
// (architect 2026-10-09 ~16:50 / ~21:00, Cool Edit's 11 grown by the six the
// ruler gave; program_spec.h's marker-lane fields); and THE STEM —
// DOTS ONE DEVICE PX SQUARE on the canvas's device rows (2026-10-09 ~14:25,
// "on waveform → unscaled"), the canvas alone (no dot
// in the lane or on the frame row; paint_marker_stems). No box, no outline,
// no stem through the frame row.
//
// THE CUES ARE COOL EDIT'S TWO COLORS (architect 2026-10-09 ~11:50–12:00,
// "red+blue for warp and phase"; every cue since 2026-10-26, the history's
// halves and a coincident marker's included):
// THE RED `cue` (Cool Edit's CueM, F34B58 in its default) and THE BLUE
// `range` (its RngM, 4B82F3) — each built-in's own pair, the two colors
// returned 2026-10-26 (architect: "the default Cool Edit markers contrast
// well, with the flag in red and the range in blue"). A point cue's stem is Cool Edit's
// range start and end superimposed — the blue dots on the canvas's device
// rows ≡ 3 (mod 8), the red on rows ≡ 7, one dot every four rows in
// alternating colors (program_spec.h's dot fields). WARP AND PHASE RESET
// WEAR THE SAME CUE: the kind reads from the label alone (his one-label
// rule). THE STRIKE IS THE INVALID INDICATOR (architect 2026-10-26 ~15:10:
// "It's very odd having anything but red mean invalid. So I think what we
// should do is have coincident markers be shown as a strike through, which
// is actually effective because that's what happens to them. They become
// one. Very easy to see. So just the strike through on the text, and the
// shadow on the strike through as well"): a coincident marker wears the
// plain cue with its label STRUCK (show_cue_label's bar, the face's own
// stroke on its own strikeout rows, gui_font_strike_band) — the invalid
// color of 2026-10-11, its triangle, dots and label, retired with its role.
// THE TRIANGLE NEVER CHANGES WITH SELECTION (Cool Edit's never does). THE
// TABLE — which triangle, which dots, which label, which strike — is
// resolve_flag_face's head (render.cpp), the one resolver every cue goes
// through.
//
// THE LABEL'S SEGMENTS: the marker's payload label, then — while grid
// iterations paint them — its LOWER and UPPER bound cells, each a segment of
// the same label standing the segment gap past the one before it (the
// history's same-frame pair likewise: its removed half, struck, then its
// added half), every segment's text the program face on the lane's
// baseline.
//
// THE OVERLAP RULE: LATER WINS BY OCCLUSION, NEVER BY CUTTING (architect
// 2026-10-09, settled on five overlap captures; 2026-10-10
// ~19:00, after two near-coincident phase resets showed the earlier one's text
// over the later's: "why not go simpler — later always wins?", and "a long
// flag before a short one should continue after the short one ends"). The
// labels paint LEFT TO RIGHT, each on an OPAQUE FACE BOX from ITS OWN
// TRIANGLE'S LEFT EDGE (the column less the triangle's half width) to one
// quantum past its last segment's text, on THE WHOLE LANE'S ROWS 0 .. 16 (the
// press target's rows, so the label is one geometry at rest and selected,
// 2026-10-09 ~21:00, a selected label taking no fill of its own, ~23:30), so
// a later cue masks an earlier label's text only across its own
// triangle-to-end extent and an earlier long label's text resumes past a
// short later one; no label is ever clipped. Then the triangles paint LEFT TO
// RIGHT over every label, so a right triangle covers its left neighbour's
// shadow. The press follows the picture (topmost_flag_rect, app_state.cpp):
// the rightmost triangle, then the rightmost label box, under the point.
//
// THE STATES (resolve_flag_face, render.cpp, the one ladder: disabled wins,
// then the live cue; the strike rides either):
//   THE CUE   the red triangle, the red and blue dots, the label in the
//             bone (materials.h's kBoneInk);
//   STRUCK    the red-flag class (warp_red_flag_set_cached and its
//             phase-reset twin — the coincident run, and a warp label ref
//             the render falls back from) and the history's REMOVED half:
//             THE PLAIN CUE, ITS LABEL STRUCK — the bar part of the text,
//             in its ink and over its shadow, in every look below (the
//             faded label's, the selected text's);
//   DISABLED  THE FADED LOOK (architect 2026-10-10 ~12:20, "the arrow needs
//             to be legible for me, but the text doesn't … since it's
//             disabled, it's inert"): the triangle's live color (the cue
//             red) and the cue
//             shadow BLENDED 45 % TOWARD THE FACE and the label's rest tone
//             65 % toward it — derived solid colors, "an opacity look …
//             not actually opaque" (cool_edit_derive.h, `ce_off_*`) — and NO
//             DOTS; the same faded label on a tie follower's cells (one
//             look for inert cue text; the Windows emboss retired from cues);
//   SELECTED  THE LABEL'S TEXT IN THE CHROME'S `label` ON THE PANEL'S FACE,
//             NO FILL (architect 2026-10-09 ~23:30: "when I clicked on a
//             flag and it was selected, the text would go white, not navy
//             blue background"; "clicking on a flag to select it makes the
//             text turn white. That's it. That's the only thing") — Cool
//             Edit's program text, EFEFEF on the default Face, against the
//             light tone at rest (the chrome's `selected_text` until
//             2026-10-10, when it became a text for the ink's fill,
//             chrome_derive.h); the segment's resting face box (the
//             overlap rule's, below) stays as it is, the text not moving,
//             on the baseline that centres its cap band in the lane as at
//             rest (cue_baseline_px, 2026-10-26) (a struck label's bar
//             likewise);
//             THE TRIANGLE KEEPS ITS COLOR, and the playhead stands on the
//             marker where the selection landed it. THE PRESS TARGET STAYS
//             THE WHOLE LANE, rows 0 .. 16 (architect 2026-10-09 ~21:00, "the
//             text box should take up the whole marker lane … right now it's
//             too small to click"): the box is geometry, not paint. THE
//             EDITOR is where the selected pair's fill appears: it opens with
//             the whole text selected, white on the selection fill inside
//             its field (EDITING, below). SELECTION IS ONE SEGMENT'S
//             (architect 2026-09-05): the ADDRESSED cell's segment wears it
//             — the payload for every selected marker but the focus, whose
//             addressed cell is AppState::addressed_cell; the history's
//             changed pair, one item, turns both halves at once (a nudged
//             pair's two cues both);
//   SELECTED DISABLED (architect 2026-10-07 ~15:10: a disabled marker is
//             selectable, so its selection must show): the selected label —
//             its text in the chrome's `label` on the face in place of the
//             faded label, no fill — over the faded triangle, no dots.
// A CUE HAS NO HOVER FACE (architect 2026-09-29): the pointer over a cue says
// what a press will do through the CURSOR alone (pointer_cursor_kind).
//
// THE PHASE-RESET LEAD-IN RING on the waveform is DOTTED IN ITS RESET'S STEM
// PATTERN, never a solid line (paint_phase_reset_overlay_ring, through
// live_cue_stem_dots — architect 2026-09-17, the ring and the cue one
// unit; 2026-10-09 ~17:40, "made of the same stuff the marker looks like"):
// its left side the stem itself, its right side the stem's dots on the
// band's last column, its top and bottom runs the same two phases along the
// canvas's first and last rows; the red and the blue, every live reset's — a
// coincident one's too, its struck label the mark (2026-10-26; the invalid
// color alone 2026-10-11 to 2026-10-26); resting whether selected or not,
// as the stem is.
//
// EDITING: THE FIELD IS WINDOWS' EDIT FIELD (architect 2026-10-09,
// reversing 2026-10-07 ~09:45's flag-in-its-selected-face; the drawing
// render_flag_editor_box's): a box in THE CHROME'S FIELD PAIR — `field_ground`
// under `field_text`, the text and the caret in the field text (Cool Edit's
// dark field under its light text since 2026-10-10, read as roles) —
// with A ONE-QUANTUM OUTLINE in `field_text`, over the edited segment's box
// on THE WHOLE LANE'S ROWS 0 .. 16, the selected box's (the outline on rows
// 0 and 16 and one quantum outside the pads; 2026-10-09 ~21:00), the text
// at the segment's own seat and baseline — the resting label's, its line
// box centred in the field, which is the lane (cue_baseline_px) — with one
// quantum of pad either side of its run, its descenders inside the field
// (the text's clip the field's inner rows), THE CARET AND THE SELECTION BAND
// THE TEXT'S LINE BOX (architect 2026-10-09 ~23:30: "in Windows highlighted
// text does take up the entire [height] — no white shows through on top and
// the bottom, only on the sides. Although it feels like here the font is
// small enough that we should be able to show a little white at the top and
// the bottom"): the program face's recorded ascent above the baseline and
// its descent below it (gui_face_metrics, 10 + 2 W), the dialog editor's own
// band rule on each of its rows (paint_modal_dialog) — Windows fills the
// line, and the 17-W
// field round a smaller line leaves rows of field above and below it. THE
// LINE BOX STANDS FROM THE BASELINE, which centres the cap band in the lane
// (architect 2026-10-26 ~11:30, "check the text vertical alignment —
// consider that we only use a narrow range of chars"; the line box's own
// centring, 2026-10-09 evening to 2026-10-26, stood the digits low): the
// band's rows ARE cue_line_box_top_px's and cue_line_box_h_px's, read off
// the one baseline, so the band and the text cannot part. At the cues'
// 22 px: at 300 % the band on lane rows 13 .. 37 inside the inner rows
// 3 .. 47, ten rows of field above and ten below; at 200 % rows 8 .. 24
// inside 2 .. 31, six and seven; at 138 % rows 5 .. 15 inside 1 .. 20,
// four and five. A selected
// glyph's ink past the band (a descender deeper than the recorded 2 W)
// takes the field text, the field ink's clip the band's complement in both
// its columns and its rows. The field grows and
// shrinks with what is typed, the
// segments to its right riding its edge in their resting look; and THE
// SELECTED SUBSTRING IN THE CHROME'S SELECTED PAIR, `selected_text` on
// `selected_fill`, the ordinary highlight. A COINCIDENT MARKER'S FIELD IS
// THE SAME FIELD, its text and caret the field's own, NEVER STRUCK (the text
// there is being edited), its riding segments struck as at rest. WHY THE
// FIELD PAIR: it is the
// legibility guarantee ("when you're looking very closely at a label it has
// to be legible"), and the selection pair is the highlight "precisely to
// avoid this problem" — the selected look of the resting label could not
// carry a band of its own pair. THE BOX PAINTS OVER THE LANE, the triangle
// and any neighbouring label ("it may overlap with the flag and other
// elements, that's fine"). Outside the editor the selected label is its
// light text on the panel's face, no fill and no box (SELECTED, above); the
// open editor shows its whole text selected in the selected pair inside the
// field — "a fully highlighted white text editor" under Windows 2000's pair,
// the ink (2026-10-10) under the program text's other member (2026-10-16;
// chrome_derive.h).
//
// A REFUSED ENTER RECOLORS NOTHING (architect 2026-10-03: "a red outline and
// the card is redundant"; the red frame retired from both editors): it
// SELECTS THE WHOLE TEXT in the selected pair inside the field
// (text_editor::refuse), so the first keystroke replaces it, and the
// refusing owner's CARD says why. No glyph marks it.
//
// THE HISTORY VIEW'S DIFF FLAGS are cues of their own two kinds (architect
// 2026-10-04; THE STRIKE 2026-10-26 ~15:30, "Two, agree"): both halves THE
// PLAIN CUE in the cue colors — the `cue` triangle, the stem's red and blue
// dots — a REMOVED line's label STRUCK and an ADDED line's plain, the label
// the line's own text with no sign (history_diff_label, paint_handler.h; the
// brackets `[+]` / `[-]` of 2026-08-05 retired the same day: "those are
// always hard to see anyways at the font size. A strike through is easier to
// see"). A same-frame changed pair is one cue, its label the two halves as
// two segments; a nudged pair two cues joined by the ring
// (render_history_diff_flags owns the pair). The view's focus or selection
// turns the whole item, both halves, the selected text on the face, no
// fill — the live lane's SELECTED row (render_history_diff_flags).

// -- ROW 6: THE WAVEFORM ---------------------------------------------------------
//
// THE WAVEFORM IS THE PROGRAM'S, A CANVAS UNDER ONE INK (the
// `waveform_canvas` and `waveform_ink` roles): with the magnification lamp dark
// the plate is the raw bar alone in the ink, with no outline; lit, the plate
// is two bars (the rule is at render_waveform's declaration), BOTH FILLED IN
// THE INK, the inner distinguished only by its OUTLINE, its true contour, an
// erosion at distance waveform_line_px() (one device px at every scale since
// 2026-10-09 ~14:25, "on waveform → unscaled"), in the `waveform_outline` role — ITS OWN COLOUR
// (architect 2026-10-03): the canvas's value widens the divide between the
// magnified and the compressed bars, the ink's hides it. The default
// palette is "Default" (`default`, kGuiDefaultPaletteKey,
// palette_presets.inc; architect 2026-10-26):
// the outline #0A9757 under the #4BF3A7 ink on black — each outline
// the ink at HLS lightness 0.3157, the generator's outline rule
// (tools/theme_catalog/gen_presets.py). THE PLATE
// BAKES THE INK AND THE OUTLINE (render_waveform writes their words), so the
// pair rides each render job and the cache's fingerprint
// (waveform_plate_inks); the canvas is laid live under the plate's
// transparent gaps (render_canvas) and bakes nowhere.
//
// THE CANVAS IS COOL EDIT'S (architect 2026-10-09, the third part):
// THE WAVEFORM AREA IS THE CANVAS'S INTERIOR, inside its frame (the ring of
// the panel's tones, the
// palette head's canvas-frame paragraph; the frame rows are lanes of their
// own and its columns the column's margins, main.cpp's lane table and
// waveform_area), so every column mapping, hit test and damage box reads the
// interior. THE ORDER, bottom to top: UNDER THE WAVEFORM, in render_canvas's
// one pass, the `waveform_canvas` fill and THE HORIZONTAL GRID — per
// channel, a `grid` line one device px tall ("on waveform → unscaled",
// architect 2026-10-09 ~14:25, waveform_line_px's rule) at the channel's
// zero row ± nearbyint(k · H / 4) device rows for k = 1, 2, 3, H the
// channel's half height (its zero row to its band's edge; the k = 4 line
// would stand on the band's edge and is not drawn), the canvas's full width,
// Cool Edit's grid under the ink; then THE WAVEFORM'S PLATE; then THE CENTER
// LINE per channel on its zero row, one device px, in `center`, OVER THE INK
// (architect 2026-10-09 evening: "it's supposed to be visible over the
// waveform. Otherwise, in a project like this with tape hiss, there's never
// enough zero that the red line would become visible ever";
// render_center_lines, a live pass after the plate's blit, the plate baking
// its two inks alone); then THE STEREO DIVIDER'S THREE ROWS between the two
// wells (architect 2026-10-16, chrome rows on the canvas, waveform_divider;
// render_waveform_divider, over the plate so a plate from before a resize
// never crosses them); then the lead-in ring, the dots, the scanner and the
// zoom anchor (on_redraw's paint order). THE PICTURE IS ONE THING — the
// canvas, the grid, the ink if any, the center lines — SO THE CENTER LINES
// PAINT WHENEVER THE GRID DOES: over the plate while audio is loaded (a null
// plate's frame included, its blit skipped), and with none loaded — no
// source, or a load running — directly over the canvas and its grid, right
// after render_canvas, the divider after them (on_redraw's `plate_branch`).
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
// The waveform's plate is blitted over the ground and the grid, the ink over
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
// playhead's head and the cues' triangles stand above the canvas's frame,
// so nothing needs it; the dots, the lead-in ring and the scanner paint over
// the ink there as they do everywhere in the wells. The leveler's headroom
// (waveform_gain.h's constants: its target, and the inner bar's foreground
// half) is the only thing keeping the peaks off the edges; an outer bar the
// clamp clips flat (the loud body, accepted there) stands on its band's edge
// row — against its well's ring. NO BOUNDARY LINES (architect 2026-10-09,
// "we have very limited vertical real estate"): Cool Edit's Bndy clip guides
// are not drawn. BETWEEN THE CHANNELS STANDS COOL EDIT'S STEREO DIVIDER
// (architect 2026-10-16, waveform_divider below): the upper well's light
// line, one row of the face and the lower well's dark line, one Windows px
// each, chrome rows the canvas painters leave alone and every input leaves
// alone too (point_on_waveform_divider). The chrome's
// two-line sunken well that stood round the area (the theme's quartet,
// 2026-10-02 to 2026-10-09) is gone with the content band it carved out of
// the area. THE CUES' AND THE PLAYHEAD'S DOTS and the scanner span the two
// wells, their dot rows counted from the canvas's first row and skipping the
// divider; THE ZOOM ANCHOR'S STEM spans them too.

// THE PLATE'S TWO BAKED INKS, as words: the fingerprint field and the job
// field that carry the active ink and outline to the worker (the worker reads
// no live state), set by the install family. THE WORDS ARE THE WINDOW'S
// (architect 2026-10-08, display_transform.h's head): waveform_plate_inks
// hands the two roles through display_transform::display_rgb on the GUI
// thread, so the plate writes them as they come, and a flip of True Colors
// (install_true_colors) moves the fingerprint exactly when it moves a
// colored ink — a gray ink converts to itself and re-renders nothing.
struct WaveformPlateInks {
    uint32_t ink_rgb     = 0;
    uint32_t outline_rgb = 0;
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
// THE UNIT IS THE WINDOWS-95 PIXEL (architect 2026-10-02): every authored
// chrome length is the number Windows 95 drew at 96 dpi (or, where Windows
// has no such element, the length that kept its device size on the tablet
// when the unit changed), and gui_scale is the number of DEVICE px per
// Windows px, in percent — the tablet 300 (a 16-px glyph = 48 device px),
// the laptop 138 (22). EVERY PAINTED DIMENSION IN THE TREE RIDES IT through
// scaled_px, below, where the rounding rule is stated. (The unit before it
// was the laptop's own pixel at 100 %, the kdenlive crops' measure; the
// constants were re-authored from it the same day.)
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

// A FACE AT THE LIVE SCALE (gui_font.h): what every painter and every seat
// asks for its text — each use at the face-size table's size, at the
// current gui_scale.
inline GuiFont gui_font(GuiFace face) {
    return GuiFont{face, gui_scale_percent()};
}

// One authored length in WINDOWS PX -> device pixels, the ONE conversion every
// scaled dimension in the tree takes: device = std::nearbyint(windows_px ×
// percent / 100), like every other integer-domain conversion.
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
// different concept and deliberately do not come through here.
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
// THE HI-FI SKIN'S DEVICE-PX LENGTHS (architect 2026-10-23, the amendment to
// the skin's brief: "the sizes are authored at the tablet's 300 % … and
// scale with gui_scale"): a length measured in device
// px at 300 % on the approved frame — the logo lane's, the clock field's, the type's — through the
// scaled_px road, its W the device px over 3, rounded at the element.
inline int skin_px(double device_px_at_300, int floor_px = 0) {
    return scaled_px(device_px_at_300 / 3.0, floor_px);
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

// Defensive window floor (a conservative 640x480 minimum). Enforced two ways:
// the Wayland set_min_size hint at toplevel creation, and an internal clamp in
// the geometry helpers so the waveform arithmetic is always valid regardless of
// what the compositor sends. Not sized to fit content — the longest dialogue
// may clip at the floor, which is acceptable (nobody authors at 640x480).
constexpr int kMinWindowWidthPx  = 640;
constexpr int kMinWindowHeightPx = 480;

// (THE CAPTION — the window's own title bar, the top strip's lane 0 from
// 2026-10-05: Windows 2000's caption until 2026-10-23, then the hi-fi skin's
// flat 40-px lane at 300 % with the quaver, "<piece> - Warptempo" and three
// procedural glyphs, 0 rows on the tablet from 2026-10-24's morning — IS
// REMOVED 2026-10-24 with its lane, its painter, its buttons and its light
// line, architect: "release it to the window manager as we had in the
// beginning of this project before we introduced the client-side
// decoration". THE APP PAINTS NO WINDOW CHROME: labwc decorates the laptop's
// window (server-side decorations, platform_wayland.cpp's init), the
// tablet's full-screen window has none, and the program starts at the
// surface's first row with the logo lane below. Its two lengths the logo
// lane and the cards read are the logo lane's own below. Git history.)

// THE LOGO LANE — THE BAND'S UPPER PAD (architect 2026-10-24, the go "Okay
// to proceed with the build"; the approved logo frame;
// the legend's glyphs are the bolt and the battery, his picks B1-19 and T1-17
// off his glyph sheets): with the
// tablet's caption gone ("give that height to the waveform, so the buttons
// run from the top with equal padding"), THE LOGO GOES INTO THE TOP PAD AND
// THAT PAD ALONE GROWS — kLogoLaneAt300, 72 px at 300 % through skin_px
// (his pick P72 off P38 .. P84 by 2; 48 at 200 %, 36 at 150 %, 33
// at 138 %), the band's three other pads the strip pad (strip_pad_px). It
// stands AT THE SURFACE'S TOP ON BOTH DEVICES since 2026-10-24 (under the
// app's own caption on the laptop that morning; labwc's title bar stands
// above the surface since). THE LANE'S GROUND IS THE BAKE'S, nothing else
// on it — no line, no caption tone — and it is INERT CHROME like the
// strips' pads: no hit, no tooltip, no gesture (the band's ground falls
// through to nothing, input_pointer.cpp). paint_handler.cpp's
// paint_logo_lane paints it, every length below skin_px, rounded at the
// element:
//   ON THE LEFT, THE QUAVER: Icon::Quaver through the boxed-glyph road
//     (icons::draw_boxed), its ink kLogoQuaverInkAt300 = 21 px tall (the
//     retired caption's icon's: the box icon_box_px(21), 31 at
//     300 %), ITS FIRST INK COLUMN ON THE PROGRAM RING'S OUTER LEFT COLUMN
//     (program_ring_columns — the first key's outline column: "the glyph
//     flush with the icons"), measured off the raster's alpha at that size
//     (icons::boxed_ink, alpha >= 128, once per size — the mocks' measure,
//     never the file's box), ITS INK ROWS CENTERED ON THE LANE (the spare
//     rounded half to even, the mock's rint). THEN THE BRAND, "Warptempo"
//     (kLogoBrand) — a CONSTANT, never the project's name (his: "most hi-fi
//     equipment … just had a stylized brand name") — in Liberation Sans
//     Bold at the TITLE size (gui_font.h's table, 24 px at 300 %), the bone
//     kBoneInk, its first ink kLogoBrandGapAt300 = 10 px after the quaver's
//     last ink column (text_shape::ink_left_px), its cap band centered on
//     the lane (redesign_baseline). (The gap is the retired caption's
//     title gap; the cards' glyph-to-text gap reads it too, a boxed glyph
//     then words, notifications.h.)
//   ON THE RIGHT, THE LEGEND "92% <glyph> | 2:12 PM" (gui_battery.h's
//     composer; the tick's refresh_logo_legend, main.cpp) in the BODY face
//     (22 Regular), the bone, its cap band centered on the lane, LAID RIGHT
//     TO LEFT FROM THE PROGRAM RING'S OUTER RIGHT COLUMN (the last key's
//     outline column: "the clock flush with the right edge of the
//     buttons"): the clock run "| 2:12 PM" with its LAST INK on that column;
//     one space advance of the face; THE GLYPH CELL; one space advance; the
//     percent run "92%" with its last ink there, so "100%" grows leftward,
//     away from the clock. FLUSH IS THE INK, the chokepoint's own measure
//     (text_shape::ink_left_px / ink_right_px: cairo's glyph extents of the
//     run as painted), the space advance measured from the clock run's first
//     ink to the cell and from the cell to the percent run's last ink; the
//     cell's edge rounded to a whole column.
//   THE GLYPH CELL, ONE SIZE WHATEVER IT HOLDS — the clock never moves (his:
//     "the glyph always there taking up space so that the clock doesn't
//     move"): kLogoGlyphCellHAt300 = 19 rows tall and kLogoGlyphCellWAt300 =
//     15 wide (rint(12 · 19 / 15), the bolt's proportion), its rows
//     CENTERED ON THE CAP'S CENTER — the body face's cap rows at the
//     legend's baseline (gui_font_cap_px, rounded: 15 rows at 300 %, its
//     center 8 rows above the baseline), so the 19-row cell spans 17 rows
//     above the baseline to 1 below, two past the cap top and two past the
//     baseline; derived from the live face's cap, never a constant. THE TWO
//     DRAWINGS are ARTWORK and scale as artwork (hairline_px's stated
//     exception), filled in the bone with cairo's own antialiasing:
//     * THE BOLT (charging — any power source attached, 100 % included, and
//       a host with no battery; GuiLogoGlyph's rule): one filled polygon
//       over the cell's unit box (x over its width, y over its rows),
//       kLogoBolt.
//     * THE BATTERY (discharging, or the plug state unknown), UPRIGHT, THE
//       NUB UP, solid: a kLogoBatteryWAt300 x kLogoBatteryHAt300 = 12 x 15
//       box CENTERED IN THE CELL (architect 2026-10-24, his pick T15 off
//       his battery sheet: "the battery needs to go down a little bit, but
//       make sure that it occupies the same width as the bolt still" — the
//       denser drawing read larger than the bolt; the numbers
//       below are the record; the 14 x 17
//       box of his T1-17 stood 38c1102a alone): its rows centered on the
//       cap's center — 15 above the baseline to 1 above at 300 %, the cap's
//       own rows — its columns the cell's spare halved, the odd column on
//       the right (the floor: 1 left, 2 right at 300 %); the cell,
//       the bolt and the legend's layout unchanged. The nub a rectangle x .37 ..
//       .63 of the box's width, y 0 to .14 of its height plus one device px,
//       the body a rounded rectangle x .18 .. .82, y .14 to the foot,
//       corner radius kLogoBatteryRadiusAt300 = 1.2 px (skin_px, floor 1).
//   (Liberation's own arrows ↑ / ↓ as the glyphs, a drawn pipe and
//   "nothing" as the discharging glyph were judged on the same sheets and
//   not taken, docs/engineering/closed_questions.md.)
inline constexpr double kLogoLaneAt300           = 72.0;
inline constexpr double kLogoQuaverInkAt300      = 21.0;
inline constexpr double kLogoBrandGapAt300       = 10.0;
inline constexpr double kLogoGlyphCellHAt300     = 19.0;
inline constexpr double kLogoGlyphCellWAt300     = 15.0;
inline constexpr double kLogoBatteryWAt300       = 12.0;
inline constexpr double kLogoBatteryHAt300       = 15.0;
inline constexpr double kLogoBatteryRadiusAt300  = 1.2;
inline constexpr const char* kLogoBrand = "Warptempo";
struct GuiUnitPoint {
    double x = 0.0, y = 0.0;
};
inline constexpr GuiUnitPoint kLogoBolt[6] = {
    {0.82, 0.0}, {0.0, 0.62}, {0.47, 0.62},
    {0.24, 1.0}, {1.0, 0.38}, {0.53, 0.38},
};
inline constexpr double kLogoBatteryNubX0 = 0.37, kLogoBatteryNubX1 = 0.63;
inline constexpr double kLogoBatteryBodyX0 = 0.18, kLogoBatteryBodyX1 = 0.82;
inline constexpr double kLogoBatteryNubH = 0.14;
inline int logo_lane_h_px() {
    return skin_px(kLogoLaneAt300, 1);
}

// (THE WINDOW BORDER — THE SIZING FRAME'S BAND round a restored laptop
// window, window_frame_px / paint_window_sizing_frame, Windows 95's raised
// frame from 2026-10-05 and the hi-fi skin's transparent band under one
// device px of the ring's light tone from 2026-10-23 — IS REMOVED 2026-10-24
// with the caption: labwc draws the borders and owns the resize. Git
// history.)
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
// editor's line stands in the modal family's box, its text at the box's own
// pad (modal_box.h's modal_box_text_pad_px: the field's two lines and 5-W
// pad read as one 7-W number until 2026-10-23, one device px and 1 W that
// morning, 12 px of plain field inside the ring at 300 % since that night).
// THE TIME FIELDS were never this field: row 8's clock and the render
// player's position and length are Cool Edit's dark field, its height and
// its pad program_spec.h's own, the pad 5 W since 2026-10-10, equal to this
// field's then.)

// (THE RELIEF LINE — relief_line_px, ONE WINDOWS PX, scaled: 3 device px
// at 300 %, 1 at 138 %, the width of every line of Windows' DrawEdge
// grammar from 2026-10-02 — RETIRED 2026-10-23 with the hairline sweep,
// architect: "let's do a sweep for all of these Windows Pixel lines";
// every chrome line is hairline_px below, and the places that kept its
// one-W width as a SLOT read program_line_px, an area.)
// THE ONE-DEVICE-PX LINE RULE (architect 2026-10-23, the hi-fi skin:
// "everything scales to the 1 pixel line … we scale the icon size by the
// area, not the borders and the line"): the chokepoint beside scaled_px for
// every chrome LINE the skin draws, ONE DEVICE PX at every gui_scale — the
// program ring's columns and rows (their PLACES where the one-W ring stood:
// each line on its slot's outer edge, the slot's other rows the ground the
// line encloses), the clock's ring and the time fields' rings, the stereo
// divider's three rows, the ruler's two light lines and its ticks with their
// shadow, the keys' edges and outline and the down key's wall and ride
// (cool_edit_paint.h's paint_key), the modal box's ring, the cards' and
// the tooltip's rings and the player's scrub lines (the line under the
// caption and the laptop's window border too, until both went 2026-10-24)
// — and, since the sweep of 2026-10-23
// (architect: "let's do a sweep for all of these Windows Pixel lines. I
// guess we should get rid of most of them"), the view bar's span's bevel
// (render_trim_flags), the access key's underline and its air under the
// baseline (access_underline_rect, paint_handler.cpp), the dialog editors'
// and the flag editor's carets (paint_modal_dialog, render.cpp's flag
// editor) — read
// at those sites and nowhere else. Everything else keeps scaled_px, AREAS:
// the keys, the LED, the lanes' heights, the line SLOTS the ring's lines
// stand in (program_line_px), the pads and gaps (the lists' row gap, the dialog
// text's pad, the keyboard's pad and key gap), the flags' triangles and the
// cue boxes. ARTWORK IS AN AREA IN THIS SENSE (2026-10-23): a drawing's
// stroke weight is part of the drawing, so it scales with the drawing — the
// icons, the faces' strokes, the quaver and the logo lane's bolt and
// battery (the retired caption buttons' procedural glyphs too, matched to
// the quaver's stem), never a chrome line.
inline int hairline_px() { return 1; }
// A MITRED RING of one-device-px lines (the mitre's construction, render.cpp,
// at hairline_px): `top_left` on the top row and left column, `bottom_right`
// on the bottom row and right column, the two-tone corners split on the
// diagonal.
void paint_hairline_frame(cairo_t* cr, const GuiRect& r, GuiColor top_left,
                          GuiColor bottom_right);

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
// the box wearing the program ring from the view bar's top row to the ring's
// bottom row and the controls row on row 8's rows; the on-screen keyboard's
// band stands on the window's foot across the lanes (onscreen_keyboard.h's
// placement block), a dialog editor's controls row on it.

// THE PROGRAM'S BAND AND ITS CASE — THE ICON ROW, the top strip's lane 0,
// at the surface's top since 2026-10-24 (architect 2026-10-09: the program is Cool
// Edit from the toolbar down, the same under every chrome; program_spec.h's
// head, where every length below is authored and its source named). The
// roster's twenty-five view/mode/action buttons stand in it (re-counted
// 2026-10-26) — the
// kIconRowButtons, kIconRowHistoryOpener, kIconRowViewPairs and history
// stand-in tables are the count's one authority (paint_handler.cpp);
// THE CASE IS ALSO ROW 8's AND THE RENDER PLAYER'S: every reader
// takes the accessors below, so the three rows cannot drift apart.
//
// THE CASE (Cool Edit's whole, its own 20-W seat since 2026-10-09
// ~10:25): one highlight line, the glyph, one shadow line, one black line —
// 23 W square, the buttons abutting at that pitch; A COMPOSITE OF ITS
// ROUNDED PARTS (scaled_px's rule), each old line's slot one W and the glyph
// scaled_px(20): 69 device px at 300 %, 84 at 360 % (the glyph 72, the
// slots 4 each), 31 at the laptop's 138 % — AN AREA since the hi-fi skin,
// THE KEY'S BOX (cool_edit_paint.h's paint_key, whose edges are one device
// px and whose down face rides one device px right and down checked, two
// held, 2026-10-23).
// No key wears a glyph in the skin (2026-10-23): the old glyph seat is the
// key's middle slot alone.
//
// THE BAND (architect 2026-10-22, the simple layout; its upper pad the
// logo lane since 2026-10-24): THE LOGO LANE (logo_lane_h_px, its block
// above), the case, THE STRIP PAD on the brushed ground — no line on its
// first row (the laptop's caption line went with the caption, 2026-10-24),
// the keys' legends in its bottom pad (2026-10-23) — 72 + 69 + 39 = 180 device
// rows at 300 % (147 under the strip pad above the case, 2026-10-22 to
// 2026-10-24). Its runs
// are paint_icon_row's walk (icon_row_seats: the keys abutting inside a
// run, run_gap_px of ground between runs). The hit target is the key; the
// ground between, the legends' included, is inert for input.
//
// THESE NUMBERS LIVE HERE rather than beside the row's walk because the
// case is not one row's: paint_handler.cpp's three rows read it. THE CARD AND THE FOLDER OVERLAY'S LIST
// ROWS ARE NOT THE CASE: a card's boxed glyph stands at its own pad and
// ratio (notifications.h's kNotificationPadAt300 and
// kNotificationGlyphRatio, 2026-10-24), and a list row's at the painter's
// own constants (paint_handler.cpp's kListGlyph*, 2026-10-23).
// THE PROGRAM RING'S SLOT (2026-10-23): ONE W, its own rounded part
// (floored at 1) — AN AREA, the place the old one-W line stood, kept so the
// lanes' and the canvas column's geometry did not move when the skin took
// its lines to one device px; the ring's line is hairline_px on the slot's
// outer edge, the slot's other rows or columns the ground it encloses
// (render_trim_flags, paint_ruler_row, paint_canvas_column_frame,
// modal_box_rect).
inline int program_line_px() {
    return scaled_px(1, 1);
}
inline int icon_case_w_px() {
    const ProgramSpec& p = kProgramSpec;
    return scaled_px(p.case_light_px, 1) + scaled_px(p.glyph_px) +
           scaled_px(p.case_shadow_px, 1) + scaled_px(p.case_outer_px, 1);
}
inline int icon_case_h_px() {
    return icon_case_w_px();
}
// THE STRIP PAD (architect 2026-10-22, the simple layout; a constant since
// 2026-10-23, program_spec.h's strip_pad_px, 13 W — its ruling there): row
// 8's and the modal controls row's ground above and below the case, and the
// band's ground below it (its upper pad is the logo lane, band_case_top_
// offset_px), one number: 39 device px at 300 %, 18 at 138 %. FLOORED AT ONE DEVICE PX so
// a row stands there at a scale that would round the pad to nothing (the
// legends' row since 2026-10-23).
inline int strip_pad_px() {
    return scaled_px(kProgramSpec.strip_pad_px, 1);
}
// THE CASE'S TOP, AS AN OFFSET FROM ROW 8's TOP and the modal controls
// row's: the strip pad, the one expression every reader of those strips'
// case seat takes (the row-8 and controls-row seats, hit rects, tooltips).
inline int icon_case_top_offset_px() {
    return strip_pad_px();
}
// THE BAND'S CASE TOP, AS AN OFFSET FROM THE BAND'S OWN TOP: the logo lane
// (2026-10-24; the strip pad until then), read by the band's seats alone
// (icon_row_seats, paint_handler.cpp, which every band reader takes).
inline int band_case_top_offset_px() {
    return logo_lane_h_px();
}
// THE STRIP — row 8's: the pad, the case, the pad.
inline int strip_h_px() {
    return strip_pad_px() + icon_case_h_px() + strip_pad_px();
}
// THE BAND: the logo lane, the case, the strip pad (2026-10-24).
inline int icon_row_h_px() {
    return logo_lane_h_px() + icon_case_h_px() + strip_pad_px();
}
// THE GROUND BETWEEN TWO RUNS (program_spec.h's run_gap_px): 45 device px at
// 300 %, 21 at 138 %.
inline int program_run_gap_px() {
    return scaled_px(kProgramSpec.run_gap_px);
}

// A BUTTON IS SQUARE-CORNERED (architect 2026-10-02, the Windows-95 design):
// its box is the key's (cool_edit_paint.h's paint_key), and no corner in
// the chrome is rounded.

// -- THE PLAY-SCRUB (architect 2026-10-02; the thumb 2026-10-06) -------------
//
// The render player's modal row carries the transport's scrub: Sound
// Recorder's channel, nothing filled ("Sound Recorder doesn't fill it"),
// under WINDOWS' TRACKBAR THUMB:
//   THE CHANNEL — the PLAIN SUNKEN edge collapsed to its lines, NO INTERIOR:
//     kScrubChannelLines relief lines tall, top to bottom Shadow, DkShadow,
//     3DLight, Hilight (the edge's two rings on a rect four lines tall, its
//     end columns the same rings' sides), spanning the slider's track and
//     centred in the buttons' band (row 8's case band since 2026-10-05, the
//     program's case since 2026-10-09, bottom_row_seats in
//     paint_handler.cpp). Its height is the sum of its rounded
//     lines, 4 × relief_line_px (12 device rows at 300 %, 4 at 138 %).
//   THE THUMB — WINDOWS' POINTED TRACKBAR THUMB (TBS_BOTTOM; architect
//     2026-10-06, the source ReactOS's own, measured at 100 % on
//     a ReactOS capture of mplay32's position slider, and confirmed on
//     one of the Device volume slider disabled): a
//     rectangle kScrubThumbWidthPx = 11 wide whose bottom ends in a
//     downward point kScrubThumbPointPx = 5 rows tall, the tip a single
//     DkShadow pixel under the middle column (mplay32: columns 96..106,
//     rows 99..121, straight sides rows 99..116, the point 117..121, the
//     tip at (101, 121); the disabled one 199..209 by rows 212..232, its
//     straight sides 16 rows — comctl32 sizes the straight part to the
//     control, the width and the point never). ITS EDGE IS THE SOFT RAISED
//     PAIR on the pentagon (EDGE_RAISED | BF_SOFT): the outer line Hilight
//     on the top, the left and the left diagonal, DkShadow on the right and
//     the right diagonal; the inner line 3DLight (the face's own colour in
//     Windows 2000 Standard, invisible on the capture) and Shadow; the face
//     the ground (the disabled capture's face is the Hilight checker — the
//     scrub had no disabled state: with no item there was no thumb). DRAWN
//     SCALABLE (paint_scrub_thumb, render.cpp): the pentagon's rings in
//     device px, antialiased, the light and dark halves MITRED at the
//     top-right corner along the 45-degree diagonal (paint_relief_frame's
//     rule) and meeting at the tip on the axis, as the playhead's head does.
//     Its height stays THE SCRUB'S SEAT, kScrubThumbAbovePx rows above the
//     channel, the channel's four lines and kScrubThumbBelowPx below — 8 +
//     4 + 9 = 21 Windows px, the volume slider's 16 + 5 (mplay32's taller
//     control draws 18 + 5 = 23; the seat was kept rather than the channel
//     moved), the straight part over the channel and the point below it,
//     toward the channel's bottom: 63 device rows at 300 % (24 + 12 + 27),
//     27 at 138 %, each part rounded on its own.
// NO PLAYED EXTENT AND NOTHING THAT READS THE WINDOW'S FOCUS: the channel is
// lines on the ground, the position is the thumb. The painter is
// paint_modal_dialog's player branch; the press is claim_player_scrub_press.
// THE HI-FI SKIN'S SCRUB (architect 2026-10-23: "looks good … the
// scrub fits"; the record above is the Windows thumb it replaced): THE
// CHANNEL a ONE-DEVICE-PX light line over a one-device-px dark line, the
// two ring tones, across the scrub's span (kScrubChannelLines hairlines);
// THE THUMB A KEY-SHAPED BLOCK kScrubThumbWidthPx x kScrubThumbHeightPx W,
// 8 x 16 since 2026-10-23 (architect, on the installed player: "too tiny.
// The trough is okay at 1 pixel, but the thumb itself should be larger"; his
// pick, row 2 of his thumb sheet off 4 x 8 /
// 8 x 16 / 12 x 20 / 15 x 23 — the earlier 4 x 8 until then),
// painted by the key painter with no LED and no legend, centred on the
// channel; ITS HIT ZONE ITS PAINTED RECT (render_player_scrub_handle_hit:
// on screen is as painted).
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
// its slot: 12 of the 24 px at 300 %). THE PUBLISHED ITEM is therefore the
// channel widened by that overhang at each end (AppState::
// ModalDialogGeometry::scrub, the press router's outer gate and the
// per-position damage), so a press on the overhanging half takes the thumb
// and a moving thumb's end column is always inside the damage. (The 14-W
// grab box the mapping inset the channel by — half of it at each end, a
// leftover of the pointed Windows thumb — stood 2026-10-02 to 2026-10-25.)
inline constexpr int    kScrubChannelLines  = 2;
inline constexpr double kScrubThumbWidthPx  = 8.0;
inline constexpr double kScrubThumbHeightPx = 16.0;
// The thumb's width floors at three device px (its outline and one column
// of face), its height at five.
inline int scrub_thumb_w_px() { return scaled_px(kScrubThumbWidthPx, 3); }
// THE THUMB ON ITS BOX `box` (scrub_thumb_w_px by scrub_thumb_h_px): a key,
// at rest, no LED.
void paint_scrub_thumb(cairo_t* cr, const GuiRect& box);

// (THE TAB STRIP — Cool Edit's Organizer tabs, "Tab A" and "Tab B", a 21-W
// lane under the band 2026-10-18 to 2026-10-22 — IS REMOVED, architect
// 2026-10-22, the simple layout: the active tab's name stands in row 8 right
// of the clock, paint_bottom_row_buttons_and_clock.)

// THE CANVAS COLUMN'S LANES — the TRIM lane (the view bar), the RULER lane,
// the MARKER lane and THE CANVAS'S TOP FRAME ROW, stacked in that order
// directly under the band (main.cpp's lane table; the column's 5-W air above
// the view bar went 2026-10-22, the simple layout), the frame row's bottom
// edge the waveform's top with no gap: THE PROGRAM'S, COOL EDIT'S, THE SAME
// UNDER EVERY CHROME (architect 2026-10-09; program_spec.h owns every length
// and its source; the colors at the palette block's canvas-column
// paragraph). Each lane is a composite of its rounded parts (scaled_px's
// rule): a line is one quantum (program_line_px), a run of face or ground
// its own scaled_px, the cue's triangle its device rows (2026-10-26).
//   THE TRIM   the view bar, a line, the field, a line (trim_lane_h_px):
//              21 device rows at 300 % (3 + 15 + 3), 14 at 200 % (2 + 10 +
//              2), 12 at 150 % (2 + 8 + 2), 9 at 138 %, 26 at 360 %.
//   THE RULER  11 W of ground and its bottom line (ruler_lane_h_px): 36 / 16
//              / 44.
//   THE MARKER 12 W and five label quanta (marker_lane_h_px), the cue's
//              triangle its last rows (cue_triangle_rows, 8 / 4 / 10):
//              51 / 22 / 63.
// (The ruler's 6 W went to the marker lane, architect 2026-10-09 ~21:00 —
// program_spec.h's ruler and marker-lane fields; the two lanes' sum keeps
// its 87 device rows at 300 % and its 107 at 360 %, and at 138 % rounds one
// row taller, 38 against 37, the laptop's well one row shorter.)
//   THE FRAME  the canvas's dark top frame row, one line
//              (canvas_top_frame_h_px, below): 3 / 1 / 4.
// THE VIEW BAR'S FIELD, 15 DEVICE PX AT 300 % on the skin's road (architect
// 2026-10-24, his pick T15: program_spec.h's view_bar_field_at300 the owner
// of the ruling; 18 px at 300 % through scaled_px of 6 W 2026-10-09 to
// 2026-10-24): 10 at 200 %, 8 at 150 % (7.5 rounded to even), floor 1.
inline int view_bar_field_h_px() {
    return skin_px(kProgramSpec.view_bar_field_at300, 1);
}
inline int trim_lane_h_px() {
    return program_line_px() + view_bar_field_h_px() + program_line_px();
}
inline int ruler_ground_h_px() {
    return scaled_px(kProgramSpec.ruler_ground_px);
}
inline int ruler_lane_h_px() {
    return ruler_ground_h_px() + program_line_px();
}
// THE CUE'S TWO UNITS (program_spec.h's head), one owner each:
// THE TRIANGLE'S UNIT IS ONE DEVICE PX AT EVERY SCALE (architect 2026-10-26:
// "I think we should make the triangle smaller, and that would make colors
// like the red from Cool Edit less problematic" — "Why not simply leave
// unscaled? That would preserve the stem/flag size relation from CEP"):
// waveform_line_px's rule, "on waveform → unscaled", carried to the cue's
// triangle, so the flag's cells stand beside the one-px stem at 1:1 whatever
// the gui_scale (THE ROW COUNT IS SCALED, cue_triangle_rows below — Cool
// Edit's 9 by 5 at 200 %). ITS READERS: the triangle's rows and
// columns (paint_ce_cue_triangle, the split
// triangle, the stamp — whole device cells since 2026-10-26, Cool Edit's
// aliased staircase), the extent every cull, hit and damage box reads
// (cue_triangle_half_w_px, cue_triangle_reach_right_px, below) — and THE
// PLAYHEAD'S HEAD, the same painter in the ruler, unscaled with it
// (architect 2026-10-26 ~10:10: "All triangles unscaled" — Cool Edit's
// cursor head stands in the same 1:1 relation to its dotted line). THE
// SHADOWS' OFFSET IS NOT ITS READER: the label's shadow and, since
// 2026-10-26 ~13:05, the triangle's are one LABEL quantum right and down
// (cue_label_shadow_offset_px below).
inline int waveform_line_px();   // the canvas's line, defined at row 6's block
inline int cue_triangle_unit_px() {
    return waveform_line_px();
}
// THE TRIANGLE'S ROWS, THE ONE ACCESSOR EVERY READER READS: 8 DEVICE PX AT
// 300 % through skin_px, rounded at the element like every length off the
// canvas (architect 2026-10-26 ~13:40, on the row count as a per-device
// number: "why density? why not just simple scale? tablet is only real
// surface. later tablets may have higher dpi, but not lower" — "yes" to the
// planner's proposal; the 8 the rows he tuned on the glass, ~13:05, "read
// the current setting and apply"). THE CELL STAYS ONE DEVICE PX
// (cue_triangle_unit_px above) and the staircase whole cells; only the
// count rides the scale: 8 rows at 300 %, 5 at 200 % (Cool Edit's own 9 by
// 5, 1:1 on the laptop), 4 at 150 % and at 138 % (3.68 rounded), 10 at
// 360 %; at the scale's floor of 50 % (is_gui_scale_percent,
// device_config.h) 1.33 rounds to 1 — a one-row staircase, the stem's own
// cell and its shadow, and the drawing holds: no floor, no clamp. The rows
// fit the ruler's ground (11 W) and the marker lane (17 W) at every scale,
// being 8/3 W. READERS: the triangle's rows (cue_triangle_h_px,
// cue_above_triangle_h_px), its widths (cue_triangle_half_w_px,
// cue_triangle_reach_right_px — every cull, hit and damage box and the
// label's lead, cue_label_lead_px) and the painter (CueTrianglePass).
inline constexpr double kCueTriangleRowsAt300 = 8.0;
inline int cue_triangle_rows() {
    return skin_px(kCueTriangleRowsAt300);
}
// THE LABEL'S UNIT IS THE SCALED QUANTUM, u = scaled_px(1, 1), the
// program's line (program_line_px): the text is scaled, so its placing is —
// the selected fill's pads, the segment gap (program_spec.h's cue
// horizontal lengths), the flag editor's field outline, the marker lane's
// quanta part (marker_lane_h_px), the cull bounds that sum them
// (marker_flag_max_width_px, the history lane's) and THE LABEL'S SHADOW'S
// OFFSET (cue_label_shadow_offset_px below).
inline int cue_label_unit_px() {
    return scaled_px(1, 1);
}
// THE SHADOWS' OFFSET, ONE LABEL QUANTUM RIGHT AND ONE DOWN — 3 and 3
// device px at 300 %, 2 and 2 at 200 % and 150 %, 1 and 1 at 138 % and
// 100 % (architect 2026-10-26 ~10:50, "it should be 3 device px at 300 %";
// ~11:30, "the shadow looks scaled right but not down — check the offset",
// the down the planner's reading of the panel's right-and-down label
// convention, confirmed on the glass 2026-10-26 ~12:10). ITS READERS: the
// cue label's one text painter (show_cue_label, render.cpp), ROW 8'S TAB
// NAME AND STATE LINE, which wear the cue label's shadow ("with the same
// shadow for the tab/git/etc text", architect 2026-10-26 ~11:30;
// show_ce_label, cool_edit_paint.cpp), and SINCE 2026-10-26 ~13:05 EVERY
// TRIANGLE'S SHADOW — the cue's, the split one's, the playhead head's — the
// label's own offset, one rule (architect: the triangle's shadow becomes
// one label quantum right and down; CueTrianglePass, its extent
// cue_triangle_reach_right_px below).
inline int cue_label_shadow_offset_px() {
    return cue_label_unit_px();
}
// THE LABEL'S LEAD from the marker's column to its text IS THE TRIANGLE'S
// REACH RIGHT PLUS THE GAP (architect 2026-10-26 ~12:40, on the glass at
// nine rows: "It seems the lead-in was not scaled — increasing rows seems
// to not move text, but it should, relative to what I tuned it to"): the
// reach (cue_triangle_reach_right_px — the color's rows and the shadow's
// scaled offset), then THE GAP, 8 DEVICE PX AT 300 % (kCueLabelGapAt300,
// the value he tuned on the glass, settled ~13:05) through skin_px,
// rounded at the element — so the label follows the rows and keeps the gap.
// At 300 %: 11 + 8 = 19; at 200 %: 7 + 5 = 12; at 150 %: 6 + 4 = 10; at
// 138 %: 5 + 4 = 9. The gap is never negative, so the text never stands
// under the flag or its shadow. THE FILL'S LEAD IS THE LEAD LESS ONE PAD
// QUANTUM (cue_fill_lead_px): the text stands one pad inside its fill
// (program_spec.h's cue horizontal lengths; cue_segment_boxes, render.cpp).
// Readers: the segment boxes (cue_segment_boxes), the cull bounds
// (marker_flag_max_width_px below, the history lane's).
inline constexpr double kCueLabelGapAt300 = 8.0;
inline int cue_triangle_reach_right_px();   // below, the triangle's extent
inline int cue_label_lead_px() {
    return cue_triangle_reach_right_px() + skin_px(kCueLabelGapAt300);
}
inline int cue_fill_lead_px() {
    return cue_label_lead_px() - kProgramSpec.cue_fill_pad * cue_label_unit_px();
}
// THE TRIANGLE'S HEIGHT: cue_triangle_rows() cells of one device px.
inline int cue_triangle_h_px() {
    return cue_triangle_rows() * cue_triangle_unit_px();
}
// THE MARKER LANE KEEPS ITS ROWS (2026-10-26, program_spec.h's marker-lane
// fields): its W rows and its label quanta, each rounded on its own — 36 +
// 15 = 51 device rows at 300 %, 17 + 5 = 22 at 138 %, 43 + 20 = 63 at
// 360 % — and the triangle's device rows its bottom, THE ROWS ABOVE IT
// THE REMAINDER: 51 − 8 = 43 at 300 %, 34 − 5 = 29 at 200 %, 22 − 4 = 18 at
// 138 %, 63 − 10 = 53 at 360 %.
inline int marker_lane_h_px() {
    return scaled_px(kProgramSpec.cue_lane_ground_px) +
           kProgramSpec.cue_lane_quanta * cue_label_unit_px();
}
inline int cue_above_triangle_h_px() {
    return marker_lane_h_px() - cue_triangle_h_px();
}
// THE LABEL BOX'S ROWS — the selected fill, the resting face box, the
// published press target and the flag editor's field — under the marker
// lane's top (program_spec.h's marker lane fields). THE BOX IS THE WHOLE
// LANE (architect 2026-10-09 ~21:00), so its height is the lane's own
// composite, never a second rounding of the same 17 W: at 138 % scaled_px(17)
// is 23 against the lane's 17 + 5 = 22, at 360 % 61 against 43 + 20 = 63
// (cue_fill_px equals the lane's authored height, program_spec.h's assert).
inline int cue_fill_h_px() {
    return marker_lane_h_px();
}
// THE BOX SEAT'S ONE SOLVER (paint_handler.cpp, where its rule and its other
// callers stand): the baseline that centres `font`'s cap band in the box
// [box_y, box_y + box_h), a half-row tie toward the top.
double redesign_baseline(const GuiFont& font, double box_y, double box_h);
// THE CUE LABEL'S BASELINE AND ITS LINE BOX under the marker lane's top —
// THE PROGRAM FACE'S CAP BAND CENTRED IN THE LABEL'S BOX, THE WHOLE LANE,
// EXACTLY (architect 2026-10-26 ~11:30: "check the text vertical alignment —
// consider that we only use a narrow range of chars"). THE REASON IS THE
// TEXT'S: a label is digits, lowercase and a few marks, so its ink is the
// CAP BAND — the baseline up to the cap height, the digits' and the
// ascenders' top (Liberation Sans's "H" ink, 1409 of its 2048 units, 0.688
// em, gui_font_cap_px; the x band, 1082, inside it) — with a rare
// descender, and the face's line box (ascent + descent + gap, 2355 units)
// is mostly air above that band: centring the box, the rule of 2026-10-09
// evening to 2026-10-26, stood the ink low. ONE DERIVATION, ONE ROUNDING:
// the lane's centre lane_h / 2, half rows kept, plus half the cap band at
// the live scale, rounded once (std::nearbyint) — THE BASELINE ROW; THE
// LINE BOX — the flag editor's caret and selection band
// (render_flag_editor_box reads cue_line_box_top_px and cue_line_box_h_px,
// never a second derivation) — is PLACED FROM THAT BASELINE, its top the
// baseline less the face's own ascent and its height the face's own line,
// each rounded at the element (gui_font_ascent_px, gui_font_line_px), the
// lane's clip bounding it. Every label segment — the resting label, the
// bound cells, the history's two halves — and the flag editor's text ride
// the baseline (the editor's text sits at the label's seat). THE ROWS at
// the Program face's 22 px (gui_font.h; lane = 36 W + 5 quanta, em, cap;
// baseline; box): at 300 % lane 51, em 22, cap 15.1 — the baseline 33, the
// cap band on rows 17.9 .. 33 (17.9 rows of field above it, 18 below), the
// box rows 13 .. 37 (ascent 20, line 25); at 200 % lane 34, em 15, cap
// 10.3 — the baseline 22, the box rows 8 .. 24 (ascent 14, line 17); at
// 138 % lane 22, em 10, cap 6.9 — the baseline 14, the box rows 5 .. 15
// (ascent 9, line 11).
inline int cue_baseline_px() {
    const double lane_center = static_cast<double>(marker_lane_h_px()) / 2.0;
    const double cap = gui_font_cap_px(gui_font(GuiFace::Program));
    return static_cast<int>(std::nearbyint(lane_center + cap / 2.0));
}
inline int cue_line_box_h_px() {
    return static_cast<int>(
        std::nearbyint(gui_font_line_px(gui_font(GuiFace::Program))));
}
inline int cue_line_box_top_px() {
    return cue_baseline_px() -
           static_cast<int>(std::nearbyint(
               gui_font_ascent_px(gui_font(GuiFace::Program))));
}
// (THE RULER'S DIGIT ROWS AND THE CUE LABEL'S ROWS AT 100 %, asserted
// against the recorded Windows metrics 2026-10-09 to 2026-10-23, went with
// those metrics: the faces' vertical metrics are their own at the table's
// sizes since 2026-10-23, gui_font.h's head, so the baseline and the line
// box above are derived at the live scale alone.)
// THE TRIANGLE'S PAINTED COLUMNS round its stem column `col` — the cue's and
// the playhead head's one extent, read off the painter's own cells
// (paint_ce_cue_triangle, centred on the one-px stem, architect 2026-10-09
// ~14:35: the top row's 2·rows − 1 cells from col − (rows − 1)u to
// col + (rows − 1)u, u the triangle's cell, one device px, and the shadow
// THE LABEL'S OFFSET right of them, cue_label_shadow_offset_px, 2026-10-26
// ~13:05): [col − cue_triangle_half_w_px(), col +
// cue_triangle_reach_right_px()), (rows − 1)·u left and rows·u + offset
// right — the apex cell, rows − 1 cells, then the shadow's offset: 7 and
// 8 + 3 = 11 device px at 300 %, 4 and 5 + 2 = 7 at 200 %, 3 and 4 + 1 = 5
// at 138 %. Its ROWS are [top, top + rows·u + offset) before the clip,
// the shadow's offset down past the triangle's apex row — which every
// caller's clip cuts at the lane's last row (paint_cues' lane clip, the
// head's own rows in paint_ruler_row), so the boxes below keep the
// triangle's rows. THE ONE ROAD for every
// reader of that width: the cue's published triangle box (paint_cues), the
// label's face box from its own triangle's left edge (paint_cues; no label is
// clipped), the flag iterator's cull,
// the head's cull and its published hit (paint_ruler_row,
// AppState::playhead_head_hit) and the playhead's cull and damage box
// (render_playhead, playhead_invalidate_rect, main.cpp), so no repaint can
// be narrower than what the head painted.
inline int cue_triangle_half_w_px() {
    return (cue_triangle_rows() - 1) * cue_triangle_unit_px();
}
inline int cue_triangle_reach_right_px() {
    return cue_triangle_rows() * cue_triangle_unit_px() +
           cue_label_shadow_offset_px();
}
// THE CANVAS'S FRAME AND THE COLUMN'S MARGINS (architect 2026-10-09, the
// third part; program_spec.h's column_margin_px): the
// canvas's TOP FRAME ROW is a lane of its own, top lane 5, one line
// (canvas_top_frame_h_px), directly under the marker lane; its BOTTOM FRAME
// ROW — THE PROGRAM RING'S LAST ROW — a bottom lane of its own, one line
// (canvas_bottom_frame_h_px), between the canvas and row 8 (the column's 5-W
// foot under it went 2026-10-22, the simple layout). Across, the column
// stands column_margin_w_px of wood in from each side of the window, then
// its one-line frame column, then its interior — THE WAVEFORM AREA — its
// width floored to the grid step and the floor's leftover (under 16 device
// px) split between the two margins, the odd pixel to the right (main.cpp's
// waveform_area owns the split; column_inner_x_px is the side before it):
// the side 18 + 3 = 21 device px at 300 %, 8 + 1 = 9 at 138 %, 22 + 4 = 26
// at 360 %.
inline int canvas_top_frame_h_px() {
    return program_line_px();
}
inline int canvas_bottom_frame_h_px() {
    return program_line_px();
}
inline int column_margin_w_px() {
    return scaled_px(kProgramSpec.column_margin_px);
}
inline int column_inner_x_px() {
    return column_margin_w_px() + program_line_px();
}
// The two lanes (main.cpp, beside the other lane accessors).
GuiRect top_canvas_frame_area(const AppState& a);
GuiRect bottom_canvas_frame_area(const AppState& a);
// THE PROGRAM RING'S OUTER COLUMNS (2026-10-23, the one owner): its dark
// left column, one line left of the canvas interior, and one past its light
// right column, one line right of it — waveform_area's x and width, so the
// grid floor's leftover decides them and no inset does: 21 and 2283 on the
// tablet's 2304 at 300 %, 15 and 1905 on the laptop's 1920 at 138 %. EVERY
// STRIP AND BOX STANDING ON THEM READS THEM HERE (architect 2026-10-23,
// "yes" to tying both strips' ends to the ring): the column's frame
// (paint_canvas_column_frame), the modal family's box and its controls row
// (modal_box_rect, modal_controls_seats), the band's ends (icon_row_seats)
// and row 8's (bottom_row_seats, the controls row's seats too), so they
// coincide at every scale; both strips and the controls row stand flush with
// them at both ends, no inset.
struct ProgramRingColumns {
    int x   = 0;   // the ring's outer left column
    int end = 0;   // one past its outer right column
};
ProgramRingColumns program_ring_columns(const AppState& a);

// (THE TRIM LANE AS WINDOWS' MINIATURIZED SCROLL BAR — its 16-W track,
// caps and thumb — THE RULER'S HEIGHT DERIVED FROM ITS SMALL FACE with the
// playhead's WordPad head on its bottom rows, and THE MARKER LANE DERIVED
// FROM THE 17-W FLAG BOX stood 2026-10-02 to 2026-10-09; git history.)

// Authored pixel geometry of THE BOTTOM ROW — THE UNIFIED BOTTOM ROW, the
// lane rows 8 and 9 merged into (architect-ruled 2026-08-12): the clock in
// its ringed time field, the active tab's name and the state line on the
// wood beside it at the left (architect 2026-10-02 / 2026-10-03 /
// 2026-10-05; the name 2026-10-22), and the skips (Go to Start, Go to End;
// 2026-10-24), the MARKER-VERB GROUP, the step and
// walk (Down, Up, Previous Marker, Next Marker) and the nudge and play
// (Switch Tab, Center, Left, Right and Play / Stop, the last case; architect
// 2026-10-18) packed right, each a plain run of cases, run_gap_px of wood
// between two (kMarkerVerbGroup in paint_handler.cpp owns that group's
// membership), all one lane ON THE WINDOW'S FOOT.
//
// THE ROW IS THE STRIP (architect 2026-10-22, the simple layout): THE STRIP
// PAD, the program's case (the band's own, read from its accessors above so
// the rows cannot drift apart), THE STRIP PAD — strip_h_px, 147 device rows
// at 300 % and the pad's 13 (the band's own height until 2026-10-24, when
// its upper pad became the logo lane) — on the plain wood, no
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

// THE PROGRAM'S STACK IN WINDOWS PX — every lane the program lays above and
// below the well (the frame's two sides where a vocabulary's frame stood on
// the maximised window, window_frame_maximized, left 2026-10-24 with the
// app's sizing frame): THE PROGRAM'S (program_spec.h; the simple layout
// 2026-10-22): the band under its logo lane (the key, the pad), the canvas
// column's 37 above the canvas (the view bar 7 — its field the 15 px at
// 300 % over 3, program_spec.h — the ruler 12, the marker lane 17, the
// canvas's top frame row 1), the canvas's bottom frame row 1
// under it and row 8 (the pad, the key, the pad), the strip pad
// program_spec.h's constant (strip_pad_px). THE LOGO LANE IS NOT IN IT: its
// height is the skin's device px at 300 % (logo_lane_h_px, 72 there; the
// app's caption, 40 there, stood over it on the laptop until 2026-10-24).
constexpr int chrome_stack_authored_h() {
    return program_band_authored_h(kProgramSpec) +
           program_column_authored_h(kProgramSpec) +
           program_canvas_bottom_authored_h(kProgramSpec) +
           program_strip_authored_h(kProgramSpec);
}
// THE STACK AT THE STRIP PAD'S 13 W: 73 above (the band's case and pad 36,
// the column 37) and 50 below (the ring's last row 1, row 8 49), 123 W, 369
// device rows at 300 % — with the logo lane's 72 and no caption the
// tablet's well 999 rows of the surface's 1440 (2026-10-24: the trim
// field's 15 px gave it 3 rows; 996 under the 18-px field, 989 under the
// 40-row caption over the strip pad, 2026-10-23 to 2026-10-24) — main.cpp's
// lane table the record of each scale, the laptop's among them.
static_assert(chrome_stack_authored_h() == 123);
// AT THE WAVEFORM'S EDGES A CUE IS CLIPPED (architect 2026-09-26, the flag
// box's rule kept for the cue): the marker lane paints inside the waveform's
// columns [0, w) — every triangle, label, editor field and hit rect
// (clip_to_waveform_columns, render.cpp), the columns standing at the
// canvas's own x since 2026-10-09 (waveform_area, the column's margin) — and
// the flag iterator admits a marker whose triangle reaches into them (its
// left edge four device px left of its column), so a marker just past the last
// column shows the left half of its triangle there, and no dots (the stem
// stash is gated to [0, w)).
// THE PLAY-SCRUB'S CHANNEL AND THUMB HEIGHTS, here because they count relief
// lines (the slider's block, with its constants, is above): the channel its
// four lines, the thumb its rows above the channel, the channel and its rows
// below — each part rounded on its own.
inline int scrub_channel_h_px() {
    return kScrubChannelLines * hairline_px();
}
inline int scrub_thumb_h_px() {
    return scaled_px(kScrubThumbHeightPx, 5);
}
// The thumb's rows above the channel: the thumb centred on it.
inline int scrub_thumb_above_px() {
    return (scrub_thumb_h_px() - scrub_channel_h_px()) / 2;
}
// THE WAVEFORM AREA CARRIES NO BORDER: the canvas's frame stands outside it
// (row 6's canvas paragraph).
// THE WAVEFORM'S LINE WIDTH — ONE DEVICE PX AT EVERY SCALE (architect
// 2026-10-09 ~14:25, "ON WAVEFORM → UNSCALED. OTHERWISE, SCALED."; his
// ~14:20 on the laptop at 200 %: "the marker stems are gui scaled; I thought
// we agreed they would be unscaled like the waveform, so the resolution
// matches" — the rule of 2026-10-06 that a period technique is drawn at the
// device's resolution, one device px a cell, as the checker and the dither
// are). EVERYTHING PAINTED ON THE CANVAS stands at the plate's own
// resolution, the plate column being one device px by rule: THE READERS —
// the one inventory of the class, each an ALIASED INTEGER RECT [col, col +
// 1) whose left edge is the item's own column, clipped to the waveform's
// columns [0, w) — the scanner and the resting cursor's dots
// (render_playhead), the cues' dots (paint_marker_stems), the zoom anchor's
// stem (render_strip_anchor_stem), the phase-reset lead-in ring's dots
// (its right side and its top and bottom runs, paint_phase_reset_overlay_ring;
// its left side is the reset's stem), the lit plate's inner OUTLINE,
// an erosion at distance 1 (render_waveform's inner_interior, render.cpp, the width riding the
// plate job and its fingerprint), and the canvas's GRID AND CENTER LINES
// (render_canvas, render_center_lines) — AND ONE AREA OFF IT, THE CUES'
// TRIANGLES AND THE PLAYHEAD'S HEAD (paint_ce_cue_triangle), whose unit is
// this one device px (cue_triangle_unit_px, architect 2026-10-26: Cool
// Edit's stem/flag relation at 1:1). EVERYTHING ELSE OFF THE CANVAS keeps
// THE PROGRAM'S QUANTUM u = scaled_px(1, 1) for its areas (the cue label's
// placing, cue_label_unit_px), while its LINES, THE STEREO DIVIDER (chrome standing on the canvas,
// waveform_divider; "scaled" 2026-10-16) and the ruler's ticks
// (paint_ruler_row, fill_waveform_line's `t`) among them, are one device px
// since the hi-fi skin (hairline_px, 2026-10-23).
// (From 2026-09-27, "scale all, including the ruler ticks and the playhead
// head", until this ruling every canvas line was one Windows px, scaled.)
inline int waveform_line_px() {
    return 1;
}
// THE LINE'S ONE PAINT: columns [col, col + t) of a strip whose columns are
// [0, area_w), at window x `area_x`, rows [y0, y1), as ONE aliased integer
// rect in the caller's source colour, `t` the canvas's one device px unless a
// lane's reader passes its quantum (the ruler's ticks). THE TWO EDGE RULES
// live here: a line is GATED ON ITS OWN COLUMN (col outside [0, area_w)
// paints nothing, so a marker at column −1 shows no pixel at 0 — a line
// belongs to its column), and its width is CLIPPED at the right edge, so a
// line at w − 1 paints that one column and nothing reaches the face beside
// the canvas.
// The two edge rules' one statement: the line's painted width in columns
// from `col`, 0 where the column gate refuses it.
inline int waveform_line_width(int area_w, int col,
                               int t = waveform_line_px()) {
    if (col < 0 || col >= area_w) return 0;
    const int end = (col + t < area_w) ? col + t : area_w;
    return end - col;
}
inline void fill_waveform_line(cairo_t* cr, int area_x, int area_w, int col,
                               double y0, double y1,
                               int t = waveform_line_px()) {
    const int width = waveform_line_width(area_w, col, t);
    if (width <= 0) return;
    cairo_rectangle(cr, static_cast<double>(area_x + col), y0,
                    static_cast<double>(width), y1 - y0);
    cairo_fill(cr);
}
// THE DOTTED LINE (architect 2026-10-09, Cool Edit's cue and cursor columns),
// A PERIOD TECHNIQUE AT THE DEVICE'S RESOLUTION
// (~14:20 / ~14:25, waveform_line_px's rule: "on waveform → unscaled"): the
// line's own column and edge rules (fill_waveform_line), its rows the two
// wells' (waveform_well_rows: the stereo divider's rows carry none, 2026-10-16)
// counted in DEVICE ROWS from the canvas's first row, a dot one device px square on every row k
// with k ≡ `phase` (mod `period`) — Cool Edit's own 1-px dot every 4 rows at
// 1:1, at every gui_scale — the cues' red on 7 and blue on 3 of 8 and the
// playhead on 1 of 4 (program_spec.h's dot fields), so they interleave on one
// column. The column is the item's own painted column, the dot one px on it
// — the stem the cue's triangle and the playhead's head stand centred on
// (paint_ce_cue_triangle, ~14:35). Painted through the canvas sink
// (2026-10-13): dotted_waveform_line_runs (after waveform_well_rows) makes
// the line's runs, one per well, and GuiCanvasSink::dots paints them — on
// the cairo road one 1 x 1 rectangle a dot and one fill, the shape this line
// always drew.
//
// ONE DOTTED RUN ON THE CANVAS (2026-10-13, the canvas sink's record,
// canvas_sink.h): the one-device-px cells of a column (`vertical`, rows
// [y, y + len) at column x) or of a row (columns [x, x + len) at row y)
// whose coordinate c satisfies (c - origin) mod period = phase, the modulo
// the non-negative one and 0 <= phase < period — `origin` the coordinate
// the phase counts from: the canvas's top row for the stems, the cursor and
// the lead-in ring's right side, the reset's or the left stem's own column
// for the ring's and the nudged pairs' horizontal runs.
struct GuiCanvasDotRun {
    bool vertical = true;
    int  x = 0, y = 0, len = 0;
    int  period = 0, origin = 0, phase = 0;
};
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
// THE BPM BRACKET NEEDS NO SUCH TERM, and the symmetry question is answered
// rather than skipped: format_bpm_bracket_text (warpmarkers.h) is a different
// composer feeding a different surface — it seeds the DIALOG-HOSTED BpmBracket
// editor and never reaches a flag box — so no flag width depends on it.
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
    const int u = cue_label_unit_px();
    const double glyph_bound =
        gui_font_advance_bound_px(gui_font(GuiFace::Program));
    // The payload's lead (cue_label_lead_px) and its fill's pad past the
    // text.
    const double label =
        static_cast<double>(glyphs) * glyph_bound +
        static_cast<double>(cue_label_lead_px() +
                            kProgramSpec.cue_fill_pad * u);
    if (!iteration_on) return label;
    // Each cell: the segment gap and its two pads round its glyphs.
    const double cell =
        static_cast<double>(kIterCellGlyphs) * glyph_bound +
        static_cast<double>((kProgramSpec.cue_segment_gap +
                             2 * kProgramSpec.cue_fill_pad) * u);
    return label + 2.0 * cell;
}

// THE VIEW BAR'S GRABS (architect 2026-10-09: the trim bar's hit geometry
// "stays what it is, re-read off the new painted rects"): the begin and end
// GRABS are the span's two END ZONES, each this many device px wide — the
// bar's own height, 21 at 300 % since 2026-10-24 (trim_lane_h_px), square on
// the lane as Windows' caps were — standing on the
// bound's column inside the span (the begin's left edge on the begin column,
// the end's right edge on the end column), and the BRIDGE the span's body
// between them (render_trim_flags publishes all three; trim_endcap_rect and
// trim_bridge_gap own the rule). NO CAP IS DRAWN: the span's own bevel is its
// only edge ("the span's own bevel is the only edge"), so the
// zones are the span's painted columns read as its two ends. NARROW: where
// the span is under two zones wide the begin zone keeps its column and the
// end zone stands edge to edge right of it, overrunning the end column by
// the difference — the one place a grab stands past the painted span, kept
// so a narrow window's two ends are always both grabbable (trim_endcap_rect's
// rule, 2026-10-03).
inline int view_bar_grab_w_px() {
    return trim_lane_h_px();
}
// (THE ARROW GLYPH, Windows' scroll arrow as one filled 4 x 7 W triangle —
// the trim caps' until 2026-10-09, then the popup lists' scroll bar's —
// went with that bar, 2026-10-14. Git history.)

// (THE PLAYHEAD HEAD WAS WORDPAD'S RULER INDENT MARKER — a 9 x 8 W glyph in
// the chrome's label, hilight, shadow and ground, drawn as nested rings and
// checked against its bitmap — from 2026-10-05 to 2026-10-09; it is Cool Edit's yellow triangle since, the
// cue's own (the palette block's playhead paragraph). Git history.)

// THE HOVER TOOLTIP'S SHARED NUMBERS — a DAMAGE BOUND on its box height, its
// seat under the pointer, the three durations of its life and the slop of its
// wait. They live out here, rather than with the rest of the tooltip's
// anatomy in paint_handler.cpp, because both the RUN LOOP and the input side
// read them: the timer owner (GuiInputHandler::tick_tooltip) runs on the tick
// and damages the band the box can hang into (tooltip_hang_bands,
// app_state.h), and the one wait writer (note_tooltip_hover) reads the slop
// and the two delays.
//
// THE HEIGHT HERE IS A BOUND, NOT THE HEIGHT. The painter derives the real box
// from the BODY FACE'S OWN CELL (one line, or two 13-px bands), so the box
// follows the face instead of a literal that could drift from it; the run
// loop only needs to know it can never exceed this. 50 Windows px (the
// laptop pixel's 60 re-authored as 44, architect 2026-10-02; raised to 50
// 2026-10-07 for a 4-px tooltip pad, kept) clears the two-line form —
// win2000's 1 + 2 + 13 + 3 + 13 + 2 + 1 = 35 at 100 % (chrome_spec.h's
// tooltip_pad_px 2), 105 device rows against 150 at 300 % (a bound, with
// room for a later vocabulary's pad).
inline constexpr int     kTooltipDamageHeightPx = 50;
inline int tooltip_damage_h_px() {
    return scaled_px(kTooltipDamageHeightPx, 5);
}

// THE SEAT IS THE POINTER'S, WINDOWS 95's (architect 2026-10-06; the Windows
// Interface Guidelines, 1995, ch. 7: a tooltip is "usually displayed at the
// lower right of the pointer, but is automatically adjusted if this location
// is offscreen"): the box's LEFT EDGE at the pointer's x and its TOP
// kTooltipPointerDropPx below the pointer's y, the pointer where it stood when
// the box was shown (on the tablet the S Pen's hover point, which is the
// pointer). 18 Windows px is MEASURED, ToastyTech's win95toolbar.png
// (Explorer's "Up One Level"): the arrow's tip at (16, 17), the box's
// top-left at (16, 35), just under the cursor's tail. Where the box would
// cross the window's foot it stands ABOVE THE TOOL instead, its bottom
// kTooltipFlipGapPx above the owner button's top (comctl32's own fallback, as
// Wine reimplements it) — which every bottom-row owner meets, the row being
// 29 Windows px on the foot; past the right edge it shifts left to fit (the
// clamp, the box never shrinking). The placement's one statement is
// tooltip_box_rect (app_state.h).
inline constexpr int kTooltipPointerDropPx = 18;
inline constexpr int kTooltipFlipGapPx     = 2;
inline int tooltip_pointer_drop_px() {
    return scaled_px(kTooltipPointerDropPx);
}
inline int tooltip_flip_gap_px() {
    return scaled_px(kTooltipFlipGapPx);
}

// THE TIMING IS COMCTL32's (architect 2026-10-06), the tooltip control's
// defaults as TTM_SETDELAYTIME documents them over GetDoubleClickTime's
// 500 ms. The model is stated once, at AppState::RedesignTooltip; these are
// its constants, hard-coded, no keys. Durations ride no scale.
//
// TTDT_INITIAL: 500 ms (the double-click time) of rest on a tooltip-bearing
// button before its hint shows, restarted by every motion past the slop
// below — on a button not SPENT (a hint gone down there by this life's end
// or a press does not come back until the pointer has left the button,
// Windows' rule, stated at the model). Its own number: no hold and no beat reads it, and it reads neither
// (the chrome shift long press is timed by kHoldDelayMs alone and has no
// visual announcement).
inline constexpr int64_t kTooltipInitialMs = 500;
// TTDT_RESHOW: 100 ms (a fifth of it) instead, for the "subsequent tooltip"
// — an ARRIVAL straight from one tooltip-bearing button onto the next after a
// hint has shown, with no gap of no button between (the Guidelines, ch. 12:
// "if the user moves the pointer directly to another control … display the
// new tooltip immediately").
inline constexpr int64_t kTooltipReshowMs = 100;
// TTDT_AUTOPOP: 5000 ms (ten times it) — a box standing this long goes down,
// spending its button, the life restarted by motion on its own button.
inline constexpr int64_t kTooltipAutoPopMs = 5000;

// THE HOVER SLOP (architect 2026-09-29: the wait counts from STILLNESS WITH
// HYSTERESIS) — A RECORDED DEPARTURE from Windows: the wait re-anchors, restarting
// (on a spent button re-anchoring alone), and a standing box's life restarts,
// only when the pointer moves MORE than this from where it was anchored on
// EITHER axis, so a hovering pen's jitter can neither starve the wait nor
// keep a box alive. Windows counts every
// mouse move — a mouse at rest does not jitter — so the number is ANDROID'S
// OWN for the same job: AOSP View's hover tooltip ignores a HOVER_MOVE
// within ViewConfiguration.getScaledHoverSlop() of its anchor on both axes
// (View.TooltipInfo.updateAnchorPos, "filters out the jitter which is
// typical for such input sources as stylus"), and that slop is
// `config_viewConfigurationHoverSlop` = 4dp (core/res/values/config.xml),
// half the platform's 8dp touch slop. It is taken as 3 WINDOWS PX through
// scaled_px (4 laptop px until the unit's change, architect 2026-10-02):
// 9 device px on the tablet at its 300 % under the 320 density it runs at,
// 4.5dp — Android's 4dp to within half a dp — and half the drag gate
// (kDragMovedThresholdPx 6, app_state.h) as Android's is half its touch slop. Floor 1, so a small scale never
// zeroes it.
inline constexpr int kTooltipHoverSlopPx = 3;
inline int tooltip_hover_slop_px() {
    return scaled_px(kTooltipHoverSlopPx, 1);
}

// (THE HOVER FADE IS RETIRED — architect 2026-10-02: "hover is awkward with
// pen and sometimes flickers, ok to drop it"; Windows 95 painted no hover
// face. Breeze's hover animation stood here from 2026-09-27 — the HoverFade
// edge-and-level model, its tick, kHoverFadeMs / kHoverFadeSteps and the
// menu pill's kMenuPillHoldMs hold — and it drove paint alone, so it went
// whole with the hover faces it softened, and the stored hover bits that only
// those faces read went after them (the roster's, the dialog buttons' and
// the folder overlay band's). The pointer walks stay for what still reads
// them: the tooltips and the armed presses' inside bits (the menu row's
// hover switch went with the row, 2026-10-21). The record is in git history.)

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
// dots' one-device-px line [c, c + 1). Git history.)

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
// too small to click"; cue_fill_h_px), from the triangle's left edge (the
// column less the triangle's half width) to one label quantum past its last
// segment's text, the occluding box the painter lays — and `tri_*` THE
// TRIANGLE'S, its top edge's 2·rows − 1 device px and the shadow's offset
// right (cue_triangle_reach_right_px) over its rows (cue_triangle_rows,
// cue_triangle_unit_px, 2026-10-26; the shadow's offset down cut by the
// lane's clip).
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
// THE STEREO DIVIDER'S THREE ROWS across the area (architect 2026-10-16,
// waveform_divider: chrome on the canvas, through the sink so both roads
// paint them alike), a pass AFTER THE PLATE and the center lines (its
// reason at its definition, render.cpp: a plate from before a resize stands
// until the worker republishes). render_center_lines owns THE
// CENTER LINES, OVER THE WAVEFORM (architect 2026-10-09 evening): each
// channel's zero row, one device px in `center`, painted live after the
// plate's blit and before the dots (on_redraw's paint order) — the canvas's
// lines stay these two painters' and the plate keeps baking its two inks
// alone (WaveformPlateInks). The canvas's frame round the area is
// paint_canvas_column_frame's (paint_handler.cpp).
// THE THREE CANVAS PAINTERS PAINT THROUGH THE CANVAS SINK (2026-10-13,
// canvas_sink.h): the cairo sink on the cpu road, the same rectangles and
// fills as ever; the draw list under the GL canvas (both platforms' gl
// builds since 2026-10-14), where the backbuffer's interior is cleared instead and the GPU draws them.
void render_background(cairo_t* cr, int x, int y, int w, int h);
void render_canvas(GuiCanvasSink& sink, const GuiRect& area);
void render_center_lines(GuiCanvasSink& sink, const GuiRect& area);
void render_waveform_divider(GuiCanvasSink& sink, const GuiRect& area);

// (THE RELIEF HELPERS — paint_relief_frame, paint_relief_plain_sunken and
// paint_relief_line_frame, the chrome's one painter family for the
// Windows-95 edge grammar from 2026-10-02, every line one scaled Windows px
// — RETIRED 2026-10-23 with the hairline sweep (the line rule at
// hairline_px): every ring the chrome draws is paint_hairline_frame's, the
// same mitre at one device px, the view bar's span the family's last
// wearer.)
// One cell rect in `c` — the face fill every relief caller lays first — A
// SURFACE: `c` through set_surface_source, so under a material scope a chrome
// tone lands as its material (materials.h) and any other color flat.
void paint_cell_rect(cairo_t* cr, const GuiRect& r, GuiColor c);
// One cell rect of INK in `c`, flat whatever the scope (set_palette_source):
// a glyph's or a word's own cells — an access key's underline, the cases'
// constant outline and rings — which no
// material ever wears, even in a tone's bytes.
void paint_ink_rect(cairo_t* cr, const GuiRect& r, GuiColor c);

// THE DIRECT ROAD (2026-10-13): the context's own target as pixel words, and
// the clip it holds in device pixels, for a painter that writes the words
// cairo would leave instead of asking cairo for them — the marker stems'
// dots (paint_marker_stems through the cairo canvas sink's stem_dots,
// canvas_sink.cpp, since 2026-10-13) and the cues' triangles (CueTrianglePass,
// one target per pass, cool_edit_paint.h), each stating at its site why
// its words are cairo's. Taken only where such a write can be cairo's
// result: the target an ARGB32 image surface with no group open on it, no
// device offset and unit device scale; the operator OVER; the matrix a pure
// integral translation (both platforms hand on_redraw an untranslated
// context since 2026-10-24, when the app's window frame went — translated
// by its integral width until then, platform_wayland.cpp /
// platform_android.cpp's paint loops; the flag cache's by its band's origin row, upward, since
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
// left: a disabled key dims its legend (kDimInk, paint_key_legend), and no
// other word is disabled.)


// THE WAVEFORM AREA IS THE CANVAS: the plate blit, the canvas's lines, the
// lead-in ring and the zoom anchor's stem each span its columns (row 6's
// canvas paragraph), and every vertical line on it spans its two wells.
//
// THE STEREO DIVIDER (architect 2026-10-16, on his Wine captures of Cool
// Edit Pro 2.1 in stereo — the measured rows 521-523, the left join at
// x 208, the right at x 1872 — "ok to
// implement the bevel divider, but make sure it has no separation from the
// main chrome"): Cool Edit draws a stereo file as TWO SUNKEN WELLS, one per
// channel, each with its own ring of the panel's tones, and between them
// THREE ROWS — the upper well's LIGHT BOTTOM LINE (`ce_hilight`), ONE ROW OF
// THE PANEL'S FACE (`face`) and the lower well's DARK TOP LINE (`ce_dark`)
// — that run THROUGH the canvas's side frame columns, so the face row meets
// the column's margin on both sides and each well closes its own corners
// (the upper's bottom-left mitred, its bottom-right square; the lower's
// top-left square, its top-right mitred; paint_canvas_column_frame). CHROME,
// NOT A WAVEFORM LINE, SO SCALED (his word, "scaled"; the rounding rule's "on
// waveform → unscaled, otherwise scaled"): each row one Windows px through
// scaled_px — program_line_px for the two lines, program_spec.h's
// stereo_divider_face_px for the face, each its own rounded part — 3 device
// rows each at 300 %, 9 in all. THE ROWS STAND INSIDE THE WAVEFORM AREA,
// which stays the canvas's interior inside the OUTER ring (every column
// mapping, hit test and damage box reads it as before), and come out of the
// two channels' rows: the area's height less the divider, halved and floored,
// is each channel's band (below). Across the area's columns the divider's
// three rows are painted through the canvas sink (render_waveform_divider,
// after the plate and the center lines, on_redraw's paint order on both
// roads), so the GL canvas draws them from its list exactly as the cairo
// road fills them (canvas_sink.h's head); across the frame columns they are
// the frame painter's. THEIR TONES ARE ONE END TO END (architect
// 2026-10-22: the divider is a seam, not a choice): the two lines the ring
// tones `ce_hilight` and `ce_dark` and the face row the ground's flat mean,
// inside the area and across the frame columns alike, so no seam can stand
// where they meet. EACH ROW ONE DEVICE PX since 2026-10-23 (the line rule,
// waveform_divider_px below; the scaled rows above are the record).
// EVERY OTHER CANVAS PAINTER PAINTS NOTHING ON THEM: the
// plate's bands, the grid and the center lines stand inside the two bands,
// and every vertical line — the cues' and the cursor's dots, the lead-in
// ring's right side, the scanner and the zoom anchor's stem — is cut into
// one run per well (waveform_well_rows), its dots keeping the rows they
// stand on, counted from the canvas's first row as ever (Cool Edit's own
// dotted cursor keeps its phase across its divider on the capture: rows
// 517, then 525). At 300 % on the tablet the canvas is 999 rows, 291-1289
// (main.cpp's lane table, re-derived 2026-10-24 for the logo lane and the
// 15-px trim field): the upper well's band 498 rows (291-788), the divider
// 789-791 (the light line, the ground row, the dark line, one device px
// each) and the lower well's band 498 rows (792-1289), no spare row (999
// less the divider's 3 is even).
//
// THE DIVIDER'S ROWS, area-local, for an area `area_h` rows tall at a
// divider of `divider_px` rows (waveform_divider_px live, the job's snapshot
// in the plate's worker — the worker reads no gui_scale): `y` its first row,
// directly under channel 0's band.
// THE HI-FI SKIN'S DIVIDER (architect 2026-10-23, the line rule): the three
// rows ONE DEVICE PX each (hairline_px) — the upper well's light line, one
// row of the ground, the lower well's dark line — so the divider is 3 rows
// at every scale and the two channels' bands take the rest (the scaled
// rows above are the record of 2026-10-16 to 2026-10-23).
inline int waveform_divider_px() {
    return 3 * hairline_px();
}
struct WaveformDivider {
    int y    = 0;   // the first row, area-local: the upper well's light line
    int line = 0;   // each well's line, device rows
    int face = 0;   // the face between them, device rows
    int h() const { return line + face + line; }
};
inline int waveform_channel_band_h(int area_h, int divider_px) {
    const int h = (area_h - divider_px) / 2;
    return h > 0 ? h : 0;
}
inline WaveformDivider waveform_divider(int area_h) {
    const int line = hairline_px();
    const int face = hairline_px();
    return WaveformDivider{
        waveform_channel_band_h(area_h, line + face + line), line, face};
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
// render_waveform's centre y + h · 0.5 floors to; its HALF HEIGHT H, the
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

// THE TWO WELLS' ROWS, window y, [y0, y1): the upper well's from the
// area's first row to the divider, the lower well's from under the divider
// to the area's last row (its band and the spare row) — the rows every
// vertical line on the canvas paints, one run per well (the divider's
// rule above). An empty well has y1 <= y0.
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

// THE DOTTED LINE'S RUNS (the rule at the dotted line's paragraph above):
// column `col` of `area`, the line's column gate applied (waveform_line_width),
// ONE RUN PER NON-EMPTY WELL (waveform_well_rows), each counting its phase
// from the area's first row (`origin` area.y), so a dot keeps the row it
// stands on and the divider's rows carry none. Writes 0, 1 or 2 runs into
// `out` and returns the count — 0 where the gate refuses the column or the
// period is not positive.
inline int dotted_waveform_line_runs(const GuiRect& area, int col, int period,
                                     int phase, GuiCanvasDotRun (&out)[2]) {
    if (waveform_line_width(area.w, col) <= 0 || period <= 0) return 0;
    int n = 0;
    for (const WaveformWellRows& w : waveform_well_rows(area))
        if (w.y1 > w.y0)
            out[n++] = GuiCanvasDotRun{true, area.x + col, w.y0, w.y1 - w.y0,
                                       period, area.y, phase};
    return n;
}

// THE DIVIDER IS CHROME, INERT TO EVERY INPUT (architect 2026-10-16:
// "chrome is not waveform, for any surface"): window row `y` lies on the
// divider's rows across `area` — between the upper well's last row and the
// lower well's first (waveform_well_rows, so the press and the paint read
// one geometry, waveform_divider). A press, a touch-down, a pen-down or a
// wheel there answers exactly as the canvas column's margins do: nothing
// (point_on_nav_surface, input_pointer.cpp, the one reader for every claim
// — the press, the pen's and the finger's pan zone, the cursor map — and
// wheel_context's waveform term for the wheel). A gesture that began in a
// well keeps running when it crosses these rows (the captured drags read
// motion alone; the touch navigation's surface is decided at its start,
// apply_touch_nav_update). Columns are not tested: the caller asks inside
// the area.
inline bool point_on_waveform_divider(const GuiRect& area, int y) {
    const std::array<WaveformWellRows, 2> w = waveform_well_rows(area);
    return y >= w[0].y1 && y < w[1].y0;
}

// THE COLUMN MAPPING BASIS — the plate's viewport start, the PAINTER's
// samples-per-pixel, and the plate width.
//
// Columns are mapped on THE AUTHORING LATTICE, not by interpolating between
// integer viewport endpoints. `spp` is painter_samples_per_pixel's value (its
// one owner) — the very q that clamp_viewport_start snaps the viewport onto, so
// every RESTING viewport is a lattice point grid(k) = nearbyint(k*q). The
// renderer recovers that k and maps global column c to the display-domain edge
//     edge(c) = (k0 + c) * spp,   k0 = nearbyint(vp_start / spp)
// which, fed through the renderer's existing nearbyint, is literally
// clamp_viewport_start's own grid(k0 + c). Recovering k0 uses the same
// expression the snap itself uses to produce the rest, so the two agree by
// construction rather than by coincidence.
//
// WHY IT MUST BE THE LATTICE (the shimmer fix). Interpolating edges as
// vp_start + span*c/W from integer endpoints gives each column a rounding
// residual that CHANGES when the viewport moves. A one-pixel pan therefore did
// not hand each column its neighbour's exact span: nearbyint ties flipped,
// pyramid-bin membership flipped with them, extrema jumped, and the tip
// segments amplified every flip into both neighbours — two screenshots one
// grab-pan pixel apart differed in most of their columns. On the lattice a pan
// by n pixels moves k0 by exactly n, so column c simply becomes what column c+n
// was: same k0+c, therefore the same double, therefore the same nearbyint, the
// same bins and the same extrema. Bit-identical shifted pixels, in both views —
// in target view the lattice lives in the display domain and the map consumes
// the same doubles.
//
// Mid-gesture a viewport can sit OFF the lattice; k0 then quantizes the render
// to the nearest lattice rest, at most half a column of display quantization
// while the drag moves, healing exactly when it comes to rest. That is the
// architect's smooth-movement ruling, and Ableton pans by whole columns too.
struct WaveformBasis {
    long long vp_start   = 0;   // plate viewport start, display domain
    double    spp        = 0.0; // painter_samples_per_pixel — the lattice step
    int       full_width = 0;   // full-plate column count
};


// THE OPAQUE PIXEL WORD for a palette colour: cairo ARGB32 is a native-endian
// 32-bit quantity, so (A<<24)|(R<<16)|(G<<8)|B written as a uint32_t is correct
// on any byte order, which indexing bytes would not be. Channels are
// PREMULTIPLIED, as ARGB32 requires; at full alpha that is the colour itself.
// The channel bytes round with std::nearbyint, the project's rule — vacuous for
// the exact n/255 hex palette, decisive only if a mixed colour ever ties. The
// one word owner for every image the painters write as words: the plate
// (render_waveform) and the GPU canvas's sink (canvas_sink.cpp), each
// handing it the color already converted for the window (display_color); a
// bound icon's color (icons.cpp) takes the authored word's low 24 bits, its
// raster converted after.
inline uint32_t argb32_opaque_word(GuiColor c) {
    return (UINT32_C(255) << 24) |
           (static_cast<uint32_t>(std::nearbyint(c.r * 255.0)) << 16) |
           (static_cast<uint32_t>(std::nearbyint(c.g * 255.0)) <<  8) |
           (static_cast<uint32_t>(std::nearbyint(c.b * 255.0)));
}

// Draws one channel's waveform into `area`, which holds the `area.w` columns
// starting at GLOBAL column `col0` — i.e. the column sub-range [col0,
// col0+area.w) of `basis`. Both callers are full-plate renders and pass
// col0 = 0 with area.w == basis.full_width. When `warp_frame_map` is null (source view) the basis
// viewport is source-frame and each column reads `audio.get_peak_range`
// directly. When non-null (target view) it is target-frame: each column's
// [t0, t1) is translated to source-frame via `map_target_to_source` before the
// pyramid read, producing the deformed-waveform display.
//
// THE SILHOUETTE IS A COLUMN OF HARD BARS. Each column reduces to two TIPS in
// float rows — its raw maximum as the top tip, its raw minimum as the bottom —
// and paints as ONE opaque bar spanning floor(top) .. floor(bot) inclusive.
// There is no fractional coverage, no regime threshold, and NO INTER-COLUMN
// CONNECTIVITY AT ALL: a spike stands alone beside a short neighbour, which is
// the classic min/max look the architect chose. (The lit inner bar's
// outline — THE LIT OUTLINE, below, waveform_line_px() thick — recolours the bar's own edge
// pixels by its neighbours' extents and adds no pixel, so the silhouette is
// unchanged.)
//
// THE ANTIALIASED RENDERER IS DELETED (architect 2026-08-01, at a side-by-side
// against a snapshotted AA binary — "subtle but noticeable, I prefer without
// it"). What went: the Wu-style tip polylines joining adjacent columns' tips,
// their max-coverage compositing and endpoint unit deposits, the tall/thin
// regime and its kThinIntervalPx threshold, the two fractional boundary rows,
// the 256-entry premultiplied coverage table, and BOTH EDGE HALOS with their
// wall clamps. The technique is recorded in
// docs/engineering/waveform_antialiasing_retired.md rather than in the tree;
// this paragraph exists so the absence reads as a decision.
//
// THE >=1px NEVER-FADE FLOOR SURVIVES, re-expressed: it came from each segment
// depositing a full unit at its endpoint tips, and it now comes from integer
// geometry — floor(top) == floor(bot) for any sub-pixel interval, and both row
// indices are clamped into the lane, so the inclusive fill always writes at
// least one row, a bar clamped whole to either lane edge included. Flat or
// silent material draws a hairline; nothing can fade out or vanish.
//
// PAN INVARIANCE IS STRENGTHENED, NOT WEAKENED, BY THE HALOS' REMOVAL. They
// existed because a column's ink came from the segments on BOTH its sides, so an
// edge column missing an undrawn neighbour was under-covered against the same
// audio rendered interior and shifted under a pan. A bar depends on nothing but
// its own interval, so a column's pixel SET is a pure function of its own
// (k0+c) span — two renders of the same columns at the same basis agree
// exactly. The AUTHORING LATTICE below is untouched and is still what makes
// that span depend on the global index alone. (With the lamp lit, which of the
// inner bar's pixels wear its OUTLINE ink reads the two neighbouring columns' rows —
// THE LIT OUTLINE, below — and at the plate's two side edges the missing
// neighbour counts as inside, so an edge column's outline can differ from the
// same audio rendered interior; every plate is a full render since the
// shift-and-strip pan's retirement, so no plate ever stitches the two.)
//
// THE WRITER: this function does NOT draw through cairo. It writes `dest`'s
// ARGB32 pixel words directly, which is why it takes the surface rather than a
// context. ONE COMPOSITING RULE, where there were two: every write REPLACES,
// and no write reads. With the lamp dark a pixel takes its column's one bar,
// and lit the inner bar's word where the inner covers it, else the outer's
// (the inner over the outer where they overlap is the magnification rule's
// order), else the zero word — a priority, so replacing is correct and
// idempotent. THE CALL WRITES ITS BAND WHOLE: every pixel of `area`'s rows
// across its columns (both clamped to the surface) is stored, row by row
// from one scratch row re-decided only where a column's bar starts or ends
// (2026-10-13, a cost cut measured on the tablet; the identity with the
// column-by-column order of record and the measurement are stated once, at
// the write pass in render.cpp), so nothing the surface held before the call
// survives in that region and the caller clears nothing under it — the
// max-compositing that the segments needed (they wrote into an
// already-rendered neighbour) went with them. RETURNS whether the stored
// region is `area` whole: false when the call returned early without
// writing (a degenerate area, basis or surface) or `area` overhangs the
// surface, and the caller clears what no call stored
// (render_waveform_to_cache_surface, waveform_cache.cpp).
//
// The words are PREMULTIPLIED ARGB32 (argb32_opaque_word): each ink's word is
// built once per call; at full coverage each is its colour
// itself, so there is one word per colour rather than a table. The surface is flushed
// before the first CPU write and marked dirty after the last, so later cairo
// use sees the pixels.
//
// THE INKS ARE PASSED, `inks` (WaveformPlateInks, the job's snapshot of the
// `waveform_ink` and `waveform_outline` roles AS THE WINDOW TAKES THEM —
// already converted, waveform_plate_inks; the worker reads no live state): the plate paints in the ink with the lamp dark, and lit both bars in
// the ink with the inner's outline in the outline (row 6) — it
// is trim-agnostic, and the out-of-trim dim that
// once masked a second color through this alpha is retired, the trim bar
// spanning the window being the whole inside-the-window signal now. Its alpha
// is BINARY: opaque bars and transparent gaps, with no fractional edges left.
// THE GAPS SHOW THE GROUND render_canvas lays under the blit: the plate bakes
// no ground colour, so the canvas has that one painter and the cold frame
// before the first plate shows the same ground. Nothing recolours a plate
// pixel after the blit (the sweep's region highlight, which lifted the ground
// in the gaps and the set pixels over its span, retired 2026-10-03, architect:
// "the trim bar is enough"); no reason left needs the gaps over an opaque
// canvas-coloured plate, and the plate stays as it is.
//
// THE VISUAL MAGNIFICATION is `gain_or_null`, and the lit plate is TWO BARS
// per column from ONE peak read, both through the expander, the outer
// through the leveler and the inner through the compressor (architect
// 2026-09-25):
//
//   OUTER (painted first) = raw x g x E, in the ink, no outline — the
//                           levelled, expanded bar;
//   INNER (painted over)  = raw x c x 1/2 x E, in the ink outlined in the
//                           outline — the source's bar
//                           DOWNWARD-COMPRESSED.
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
// >=1px floor at both lane edges (a bar clamped whole to an edge is that
// edge's row). The writer
// replace-writes opaque words, so where the two overlap the inner wins: the
// reading is the source's bar, the same ink as the levelled bar behind it and
// set off from it by its darker contour alone (one pixel at 100 %, the line
// width at every scale; settled by his eye
// 2026-09-27), the core's relative thickness the loudness — at or under the
// compressor's threshold the core is half the raw bar (x E), over it thinner
// by its ratio.
// NULL (the dark lamp) draws the raw bar alone in the ink at scale 1,
// with no outline, byte for byte the plate it always drew.
//
// THE LIT OUTLINE (architect 2026-09-27) — THE INNER BAR'S TRUE CONTOUR,
// `outline_px` THICK, ALIASED. `outline_px` is t = waveform_line_px() (render.h,
// the job's snapshot): ONE DEVICE PX AT EVERY SCALE since 2026-10-09 ("on
// waveform → unscaled", waveform_line_px's rule) — the plate column's own
// width, as the stems' dots are. A pixel of the inner's shape (the inner bars of all columns)
// is a BORDER pixel iff any pixel within t of it straight left, right, up or
// down lies outside that shape — an erosion at distance t, the four-neighbour
// test at t = 1, byte-identical there to the one-pixel contour. The outer bar
// has no outline (its outline ink would be its fill). Precedence per column:
// the inner's fill and border over the outer; on a steep rise between
// columns the outline runs down the taller column's side and the line is
// continuous. Written as pixel words like every other plate pixel: no cairo
// stroke, no coverage, no blend.
// THE EDGES: a neighbour column beyond the plate's left or right edge counts
// as INSIDE (the waveform continues off screen, so no vertical line is drawn
// at the area's sides); a row beyond the bar's own ends is OUTSIDE, so both
// ends of every inner bar carry the line. No inner bar is ever clipped at
// the lane's edge — its tips are at most half scale (raw at most 1, the
// compressor's scale and the expander's multiplier at most 1, the foreground
// gain one half; kForegroundGain's static_assert, waveform_gain.cpp) — so
// every end is a true end of the shape. Each channel's lane is its own shape;
// the other channel's pixels are never a neighbour. A bar up to 2t px tall
// comes out all border; the >=1px floor stands. THE ALGORITHM is per column,
// from the row extents of the 2t + 1 columns around it alone, no 2D scan: a
// bar's interior is its rows [r0 + t, r1 - t], intersected with the
// [r0, r1] of every in-plate column within t on each side; its border is the
// bar's rows above and below that interior (the whole bar when the interior
// is empty).
// The outline recolours pixels of the bar's own shape and adds none, each
// written once with the fill's word or the outline's. Nothing else in this
// painter moves
// (the column grid, the >=1px floor, the carried-endpoint chain and the
// aliased-only writer are untouched). A loud passage's outer clips flat at
// the lane's edges while the inner still shows its compressed height inside
// it.
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
// THE COST is one extra bar's rows (its boundaries in the write pass) and one
// lookup (the scale) per column — the read, the map walk and the gain lookup are shared; no second pyramid, no second
// plate and no new cache field (waveform_gain_fingerprint flips with the
// lamp). The outline adds one row-extent pair per column for the inner, held
// for the call so the write can see its neighbours, and a handful of
// compares per neighbour within t.
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
// (one working column per plate column against a hop of whole columns: 55
// frames against 4400 on the laptop, 46 against 4416 on the tablet) the two
// agree.
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
//
// THE COLUMNS AND THE WRITER ARE TWO FUNCTIONS (2026-10-13, the GPU canvas):
// render_waveform_columns, below, is everything above but the store — the
// band's rows and columns clamped to the surface, every column's bars and the
// lit inner's interior — and the one producer of the column bounds both roads
// read: render_waveform's write pass consumes them on the cairo road, and the
// GL canvas keeps them as the plate (GuiPlateColumns) and draws the
// same priority select per pixel on the GPU (canvas_sink.h).
[[nodiscard]]
bool render_waveform(cairo_surface_t* dest,
                     GuiRect area,
                     int col0,
                     const GuiAudio& audio,
                     int channel,
                     const WaveformBasis& basis,
                     const WaveformGainCurve* gain_or_null,
                     int outline_px,
                     WaveformPlateInks inks,
                     const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr);

// ONE PLATE COLUMN'S ROWS (2026-10-13), in the plate's own rows (row 0 its
// top, the canvas's first row): the outer (or dark) bar [o0, o1], the lit
// inner bar [i0, i1] — empty, i0 > i1, under the dark lamp — and the inner's
// interior [lo, hi], empty where lo > hi. A pixel's word is the priority
// select render_waveform's write pass states: inside the inner, the ink
// within the interior and the outline elsewhere; else inside the outer, the
// ink; else nothing (the zero word).
struct WaveformColumnRows {
    int o0, o1;
    int i0, i1;
    int lo, hi;
};
// ONE CHANNEL BAND'S COLUMNS as render_waveform_columns produced them: the
// region a write stores, rows [y_lo, y_hi) and columns [col_lo, col_hi) of
// the surface (the band clamped to it), `cols` one entry per column from
// col_lo, and whether that region is the asked area whole (`whole`, the
// writer's return).
struct WaveformBandColumns {
    int  y_lo = 0, y_hi = 0;
    int  col_lo = 0, col_hi = 0;
    bool whole = false;
    std::vector<WaveformColumnRows> cols;
};
// THE COLUMN BOUNDS' ONE PRODUCER (2026-10-13; the contract is
// render_waveform's above, less the store): `area`, `col0`, the basis, the
// gain, the outline width and the map as render_waveform takes them, and
// the surface's size the band is clamped to (surf_w x surf_h). False exactly
// where render_waveform returns false without writing — a degenerate area,
// basis or pyramid, or a band that misses the surface — `out` then
// unspecified. Thread-agnostic like the writer.
[[nodiscard]]
bool render_waveform_columns(GuiRect area,
                             int col0,
                             const GuiAudio& audio,
                             int channel,
                             const WaveformBasis& basis,
                             const WaveformGainCurve* gain_or_null,
                             int outline_px,
                             int surf_w,
                             int surf_h,
                             const std::vector<WarpFrameMapSegment>* warp_frame_map,
                             WaveformBandColumns& out);

// THE PLATE AS COLUMN BOUNDS (2026-10-13, the GL canvas; the cache's
// slot at WaveformCache::columns, paint_handler.h): the two channel bands of
// a plate width x height, each `stored` exactly where the cairo road's plate
// surface holds that band's words — its render_waveform call stored the band
// whole — and transparent otherwise (the rows outside both bands, an odd
// plate's last, included). `rendered` stands where the cairo road's surface
// would exist: set by the producer, cleared by the cache's destroy.
// THE PLATE'S OWN REVISION (2026-10-14): `revision` names the live slot's
// bounds — a process-wide counter the GUI thread stamps at every PUBLICATION
// of a plate (next_plate_revision, waveform_cache.cpp: the worker's
// completion swap and the synchronous rebuild, the only two writers of the
// live slot), never 0 once published, and never repeated in the process, so
// one revision names one set of bounds whatever project set or cache
// produced it. An owned number rather than an address: the slots swap, and
// a pointer would name the slot, not the plate in it. The pending slot's
// revision is stale and read by nobody; the producer never writes it.
// Its readers are the GL canvas's alone: the list sink copies the bounds only
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

// Draws a waveform_line_px()-wide vertical LINE down the CANVAS, its two
// wells (row 6's canvas paragraph; waveform_well_rows, the stereo divider's
// rows left to the divider, 2026-10-16), at the column nearest `playhead_pixel_x` (offset
// from area.x), [col, col + t), in `color`, in the FORM the caller picks
// (architect 2026-10-09, the program is Cool Edit): THE SCANNER SOLID
// (PlayheadForm::Solid, Cool Edit's playback cursor a solid 1-px line), THE
// RESTING CURSOR DOTTED (PlayheadForm::Dotted, dotted_waveform_line_runs on
// the playhead's phase through the sink's dots, its dots interleaving with the cues'), painted
// straight over whatever it crosses — waveform ink included. No-op if
// outside; the line is gated on its own column and clipped at the right
// edge, so it never leaks into an adjacent region.
//
// THE LINE IS THE WHOLE FUNCTION (2026-08-02): the cursor's head is
// paint_ruler_row's (the `playhead_stem` triangle in the ruler since
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
enum class PlayheadForm { Solid, Dotted };
void render_playhead(GuiCanvasSink& sink,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color,
                     PlayheadForm form);

// Draws the strip-drag ANCHOR STEM: a vertical line at the drag's pivot
// column `col` (window pixels within `area`, clamped here to [0, area.w-1]),
// spanning the canvas's two wells (waveform_well_rows, the stereo divider's
// rows left to it), solid, in the `scanner` role — the
// white of the stems that belong to the controls, the playhead alone
// wearing `playhead_stem` (architect 2026-10-09; the palette block's
// playhead paragraph owns
// the rule).
// The anchor is
// the clamped column the strip-drag math pins each event — edge-included, so an
// edge-pinned anchor draws the stem exactly at the edge and the clamp becomes
// visible (the Ableton affordance). It paints ONE solid
// color straight over the waveform ink it crosses — the ink-notch overdraw and
// its plate parameter are retired (architect 2026-07-26, with the polarity
// inversion). The line is an aliased integer rect waveform_line_px() wide,
// [col, col + t), clipped at the right edge (fill_waveform_line).
void render_strip_anchor_stem(GuiCanvasSink& sink,
                              GuiRect area,
                              int col);

// (The cached marker-stem renderers render_markers / render_phaseresetmarkers
// are retired: marker stems are a live overlay,
// GuiPaintHandler::paint_marker_stems — EVERY enabled marker's column, painted
// from the flag painter's stash. Trim below is live too — the view bar in
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
// from item_viewport_basis (vp_start_frame/vp_end_frame/area_w — the promoted
// mirror of the committed fp_vp span + effective width) and `displayed_ms`
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
                                  int wave_w);

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

// Draws THE VIEW BAR — the whole trim lane (architect 2026-10-09, the
// program is Cool Edit; the palette block's canvas-column
// paragraph for its colors, trim_lane_h_px for its rows), the same under
// every chrome: the black field across the waveform's columns in its sunken
// ring (the top line and the left column, the bottom line and the right
// column, mitred), then THE SPAN — the trim window's columns, the begin
// column through the end column inclusive, a raised bevel block of the ink
// in the field's rows (its bevel one device px since 2026-10-23), sliding off an edge like any content
// (architect 2026-10-05: an out-of-view bound reads as the window running
// on); no playhead (the canvas-column paragraph above, 2026-10-09 ~14:30).
// All pixel-bound integer fills but the ring's and the span's mitred corner
// blocks, antialiased (paint_hairline_frame, 2026-10-09). The lane band is
// the `trim_bar` PARAMETER — the caller passes top_trim_row_area(app), and
// the band painted in is published as TrimBarHit::lane, the y-gate both
// trim hits read, so paint and hit take the band as one value;
// `waveform_area` is read for its `.x` and `.w` — the canvas's columns, the
// field's and the span's (2026-10-09: the column's margins), `.w` also the
// column-mapping denominator — and the ring's two side columns stand one
// line outside them; the lane's face beyond is paint_trim's.
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

// WHICH COLOR A CUE'S DOTS WEAR ON ONE OF THE STEM'S TWO PHASES (the red's
// `cue_dot_phase` and the blue's `range_dot_phase`, program_spec.h), named
// as a ROLE so the stash keeps no color and the painter reads the palette
// live (resolve_flag_face, render.cpp, the one table): none, the `cue` red
// or the `range` blue (the invalid color, a third, 2026-10-11 to
// 2026-10-26, retired with its role: the strike is the mark).
enum class GuiCueDot : uint8_t { None, Cue, Range };
// The live palette role a dot color names. Precondition: not None.
inline GuiColor cue_dot_color(GuiCueDot d) {
    const GuiPalette& p = palette();
    return d == GuiCueDot::Range ? p.range : p.cue;
}

// ONE MARKER STEM, as the flag painter publishes it: the window x of the
// column the cue's dots stand on (the triangle's apex column) and THE DOTS
// ITS CLASS RESOLVED TO ON EACH PHASE (resolve_flag_face, render.cpp;
// architect 2026-10-09 ~11:50, 2026-10-26): every live cue — warp, phase
// reset, coincident or not, and both of the history's halves — the red
// `cue` on the red's phase and the blue `range` on the blue's, alternating,
// selected or not (Cool Edit's cue never changes when
// selected). The flag PAINTERS are the only producers — the
// two live columns', and the `h` view's diff lane, which replaces them
// wholesale while the mode stands; the one reader is the per-frame waveform
// pass GuiPaintHandler::paint_marker_stems, which paints each phase's dotted
// column (the canvas sink's stem_dots, program_spec.h's dot fields) in the
// palette's live role, so the dots and their triangle can never disagree
// about a column or a class.
// A DISABLED marker publishes NO ENTRY AT ALL — disabled markers have no stem
// ever (architect), and expressing that as an absent entry rather than a flag
// on the entry means the consumer has nothing to re-decide. THE `h` VIEW'S
// DIFF-FLAG PAINTER IS THIS STASH'S OTHER PRODUCER and takes the same rule the
// same way since 2026-08-22, on its SINGLE-half flags: a removed-only or
// added-only flag whose one side is disabled publishes nothing, while a CHANGED
// PAIR always publishes (it is a live edit on display, not a switched-off line —
// the ruling is at render_history_diff_flags). (The stash was
// ALSO the pointer's stem hit source for 2026-08-01..12, when the stem was a
// second click surface of its marker; that surface is deleted — stems are
// pointer-inert, the seventh glass ruling — so the stash is paint-only again
// and `marker_index` serves the painter's identity bookkeeping alone.)
struct MarkerStem {
    int       marker_index;
    double    x;
    GuiCueDot cue_phase_dots;     // on cue_dot_phase
    GuiCueDot range_phase_dots;   // on range_dot_phase
};

// THE NUDGED PAIR'S RING RUNS (architect 2026-10-10 ~18:55 → 2026-10-26):
// the `h` view's changed pair whose two halves stand apart is TWO CUES
// JOINED BY THE RING ("they should be joined by the overlay ring") — its
// left side the left cue's stem, its right side the right cue's (each a
// MarkerStem above, the live cue's two phases) — and THIS is its top and
// bottom: a run along the canvas's first row and one along its last, from
// the left stem's window column `x0` to the right stem's `x1`, both ends
// included. Dotted IN THE STEM'S OWN PATTERN as the phase-reset lead-in
// ring's runs are (paint_phase_reset_overlay_ring, the model; live_cue_-
// stem_dots): one device px a dot, the `cue` red on the red's phase and the
// `range` blue on the blue's, one dot every four columns alternating,
// counted from the left stem's own column (unclipped, so the run stays put
// on the song as the view pans) — A STEM'S DENSITY (architect 2026-10-26:
// "the horizontal lines in the overlay of history have double the density
// of the vertical lines. They should have the same density as the
// vertical"; the stems wore one phase each until then). Always shown,
// selected or not. The history lane's painter is the one producer (render_history_diff_-
// flags; the live lanes publish none), paint_marker_stems the one reader; a
// run reaching past either edge of the waveform's columns is published and
// clipped at paint, the stems between which it stands possibly off screen.
// It is promoted and dropped with the stem stash (AppState::marker_stems).
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
// they paint, its two bound cells as further segments — in the program face
// (gui_font(GuiFace::Program)); every run carries its font, so `cr`'s font
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
// wear the faded inert label (architect 2026-09-19: they show the leader's
// numbers and are not this marker's to author; the emboss retired from cue
// text 2026-10-10).
//
// `focus_marker` / `focus_cell` NAME THE SELECTED SEGMENT (architect
// 2026-09-05): a selected marker shows its selected look on its ADDRESSED
// cell's segment and no other, the addressed cell being the payload for
// every selected marker but the focus, whose addressed cell is `focus_cell`
// (AppState::addressed_cell). Where the focus SHOWS that cell NOWHERE —
// neither in this pass nor in the open field standing in for it — its
// payload takes the selected look instead, so a selected marker always
// shows its selection, and shows it once.
//
// THE PAINTER PUBLISHES ITS GEOMETRY. `out_hit_rects` receives one rect per
// painted cue, left to right (the hit walk follows the paint order:
// triangles first, read backwards — the right one on top — then labels,
// read backwards likewise — the right one on top, the labels painting left
// to right; topmost_flag_rect, app_state.cpp), and `out_stems` one entry per ENABLED cue
// whose column is a waveform column. A derived width cannot be recomputed
// without shaping, so the pixels' own pass is the single owner of both.
// Either pointer may be null.
//
// `suppressed` NAMES THE ONE SEGMENT THIS PASS DOES NOT PAINT (SuppressedBox
// above): the marker whose marker-lane editor is open, and WHICH of its
// segments that editor stands in for. The pass paints the segments left of
// that one at rest and nothing from it rightward, the ones to its right
// riding the field's edge under the editor's own painter; THE TRIANGLE AND
// THE DOTS paint and publish for the whole session. The published label box
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
// column its dots stand on at every window width.
void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  FlagLaneRects lanes,
                  int waveform_width,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  int sample_rate,
                  const std::set<int>& selected_set,
                  const std::set<int>& red_set,
                  bool iteration_on,
                  int focus_marker,
                  MarkerCell focus_cell,
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
//                   segment's box in the field pair over the lane's whole
//                   height (cue_fill_h_px, 2026-10-09 ~21:00), its
//                   one-quantum outline included: for the
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
//                   they already are graphically; no press or wheel reaches
//                   them while the editor stands since 2026-10-25, its veil
//                   swallowing both, so the rule answers the walk's other
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
//                   stands — Esc and Return its way out (until then it closed
//                   the editor uncommitted and acted on the cell). It is
//                   still a SECOND rect and still deliberately not folded into
//                   `box`: the caret / text-drag claim seats a caret for any
//                   press inside `box` and the cursor map shows the I-beam
//                   over exactly that rect, so a folded run would map presses
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
// THE FIELD IS WINDOWS' EDIT FIELD, ONE DESIGN UNDER EVERY CHROME (architect
// 2026-10-09; the ruling and its reason at the palette block's editing
// paragraph): a box in the chrome's FIELD PAIR over THE WHOLE LANE, the
// selected label's rows (2026-10-09 ~21:00) — `field_ground` inside a
// one-quantum `field_text` outline, its text and caret `field_text` on the
// label's own baseline and seat, the caret and the selection band THE TEXT'S
// LINE BOX centred in the lane (the program face's 10 + 2 W, the one
// derivation the baseline is defined from — cue_line_box_top_px; the rule,
// its rows at each scale and the selected ink past the band taking the field
// text are the EDITING paragraph's), so opening an editor changes the
// label's SIZE and nothing about where its text stands —
// the selected substring in the chrome's SELECTED PAIR, the ordinary
// highlight. The box paints over the lane, the triangle and any neighbouring
// label it reaches. Since no field buys a caret column, the
// size only changes where the resting label was capped, the field opening at
// the committed run's own width and growing only with what is typed past it;
// the cells riding its right edge keep their resting look. THE TRIANGLE AND
// THE DOTS stay the cached pass's. A REFUSED COMMIT recolours nothing: the
// whole text is selected (text_editor::refuse) and the refusing owner's card
// says why, the dialog editor's rule on this surface.
//
// THE TEXT IS THE PROGRAM FACE, matching the labels it replaces.
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
// either (the wheel and the grab-pan worked under it until then).
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
// cent cell. The focus and its addressed cell arrive as they always did.
void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            FlagLaneRects lanes,
                            int waveform_width,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            const std::set<int>& selected_set,
                            const std::set<int>& red_set,
                            bool iteration_on,
                            int focus_marker,
                            MarkerCell focus_cell,
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

// THE DOTS A LIVE CUE'S STEM WEARS, for the two surfaces that must wear them
// too — the phase-reset lead-in ring (paint_phase_reset_overlay_ring,
// paint_handler.cpp; the ring and the cue one unit, architect 2026-09-17,
// dotted in the stem's pattern since 2026-10-09 ~17:40) and the `h` view's
// nudged-pair ring (MarkerStemRun; paint_marker_stem_runs, architect
// 2026-10-26: "they should be joined by the overlay ring"). It asks the one
// resolver (resolve_flag_face's table, render.cpp) rather than restating it:
// the red `cue` dots and the blue `range` dots, every live cue's — a
// coincident reset's too, its struck label the mark (2026-10-26; the
// invalid color's ring 2026-10-11 to 2026-10-26) — resting, selected or not
// (the cue never changes when selected), so a ring and its stems stay one
// object. The colors are the palette's live roles (cue_dot_color), read by
// the painter as the stems read them. No disabled arm, a disabled reset
// painting neither dots nor ring.
struct GuiStemDots {
    GuiCueDot cue_phase_dots   = GuiCueDot::None;
    GuiCueDot range_phase_dots = GuiCueDot::None;
};
GuiStemDots live_cue_stem_dots();

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
// 2026-08-22, deepened the same day it landed: the axis shipped reading each
// line's LOCAL '#' bit, which dropped the label cascade — a label ref with no
// '#' whose definition is disabled on the same side painted full-strength
// while its live marker dimmed). The PAINT bits are the two
// `*_effective_disabled` fields — each half's cascade verdict resolved within
// its OWN side's commit (GuiHistoryWarpEntry::effective_disabled owns the
// resolution contract; phase resets have no cascade, so their local bit fills
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
// every cue THE PLAIN CUE, the `cue` red triangle over the stem's red and
// blue dots, both halves alike (architect 2026-10-26 ~15:30, "Two, agree":
// the history in the cue colors, the invalid color retired) — and the label
// in the program face, its resting text the bone. THE STRIKE TELLS THE
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
// triangle, one stem, no ring. A NUDGED pair (the halves apart) is TWO CUES
// JOINED BY THE RING (architect 2026-10-26 ~15:30: "both markers represented
// with their stems, because one of them will have the strike through and
// that'll be obvious which one is removed, and they should be joined by the
// overlay ring"): the removed cue at then_frame, struck, the added at
// now_frame, plain, each its own triangle, label box and stem, and the
// ring's top and bottom runs between the two stems on the canvas
// (MarkerStemRun), always, in the stem's own pattern. EACH CUE IS ITS OWN
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
// local '#' or, same-day deepening, the label cascade resolved over that
// side's full warp set (the effective pair at HistoryDiffFlag above; the '#'
// in the TEXT stays the local byte) — paints its segment in the LIVE LANE'S
// FADED INERT LABEL, and a single disabled half's triangle the faded one
// (resolve_flag_face). IT SPLITS HONESTLY ON A CHANGED PAIR: each half takes
// its own bit, so a disable TOGGLE paints one faded half beside one live
// half and the direction of the toggle is readable off the label itself.
//
// `focus_index` is the mode's OWN focus (at most one flag, -1 for none) and
// `selected` its OWN multi-selection (ordinals into the same list, 2026-08-05):
// EITHER GIVES that flag THE SELECTED LOOK, both halves of a double flag
// together — a same-frame pair's one label, a nudged pair's two cues
// whichever half the focus stands on (the live lane's selected label). One look
// for both, deliberately — the focus is the selection's singleton when the
// set is empty, and the revert act reads them the same way, so a second look
// would be a distinction nothing acts on. The triangle keeps its color. THE
// DOTS READ THE DISABLED AXIS (architect 2026-08-22): a SINGLE
// flag — added-only or removed-only — whose one side is EFFECTIVELY disabled
// publishes NO DOTS AT ALL, the live lane's rule verbatim, while a CHANGED
// PAIR KEEPS ITS DOTS whichever halves are disabled, because the pair as a
// whole is a live EDIT being displayed rather than a line in a switched-off
// state.
// NO CELLS AND NO ADDRESSED CELL ON THIS LANE: a bracket is session-only and
// in no commit, so a diff flag carries no bound cells, and the view's focus is
// its own diff-flag cycle's with nothing for AppState::addressed_cell to
// address — the focus lights the whole label.
void render_history_diff_flags(cairo_t* cr,
                               GuiRect top_strip_area,
                               FlagLaneRects lanes,
                               int waveform_width,
                               const std::vector<HistoryDiffFlag>& flags,
                               long long viewport_start_sample,
                               long long viewport_end_sample,
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

