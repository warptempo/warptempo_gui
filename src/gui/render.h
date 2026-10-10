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
struct AppState;
struct DragOverlay;
namespace text_shape { struct ShapedRun; }

struct GuiRect {
    int x;
    int y;
    int w;
    int h;
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
// split 2026-10-07; the chrome's inheritance 2026-10-10): THE PALETTE holds
// every color the PROGRAM draws in the well or on a thing that enters it
// (the waveform's canvas, ink and outline, the canvas's grid and center
// line, the cues' two colors and the invalid label's pair, the playhead and
// the scanner, the panel's face; palette_file.h's head owns the rule that
// parts program from chrome), and THE CHROME'S ROLES — the chrome and its
// relief, the time fields' own pair, the cards' three, the selected and
// field pairs, the caption — ARE INHERITED FROM IT, every one but the
// caption's six a tone of the palette's face or ink by Cool Edit's own
// arithmetic (architect 2026-10-10: "we follow Cool Edit's theming";
// chrome_derive.h's head, the per-role table), THE CAPTION'S SIX a
// SCHEME'S. The chrome's roles and their order are the role table
// (kGuiThemeRoles, theme_file.h).
// The program's roles are kGuiPaletteRoles and the compiled built-in
// palettes Cool Edit's presets, kGuiBuiltinPalettes (palette_file.h); the
// active palette is the device config's `palette` — a built-in or a
// `<name>.palette` file from the `palettes/` folder beside it — or, with no
// `palette` line, the default palette, Cool Edit's "Default" under every
// chrome (2026-10-09); the active SCHEME, the caption's six keys, is its
// `scheme` — a built-in or a `<name>.scheme` file from the `schemes/`
// folder — or, with no line, the live chrome's own, which carries no keys
// and paints the chrome's COMPILED CAPTION (theme_file.h's
// kGuiCaptionWin2000: Windows 2000 Standard's). There is
// NO LEVEL (architect 2026-10-04): a dark look, like any look made official,
// would be a chrome variant of its own (2026-10-08). The app computes no
// color but COOL EDIT'S: the program's panel tones derived from the
// palette's `face` (cool_edit_derive.h, the COOL EDIT BLOCK) and, through
// the same tones, the chrome's roles (chrome_derive.h).
// EVERY COLOUR A PAINTER HANDS CAIRO is one of GuiPalette's fields through the
// one accessor palette() — or, since 2026-10-09, ONE OF THE PROGRAM'S
// PAINTER CONSTANTS (cool_edit_paint.h: Cool Edit's measured bytes that do
// not follow its preset — the case's, the time field's digits, the view
// bar's black field, the ruler's ticks and
// digits and their black shadow) — or THE ICON SET'S
// OWN INKS (architect 2026-10-06): the Tango and Mist drawings' colours,
// gradients and opacities as each file names them (icons.h), period
// artwork, not roles, so such a glyph is never recoloured by a theme — the
// one place an antialiased, translucent picture is copied whole, no glyph
// ever wearing a role (the bound monochrome set that wore its surface's
// text role left 2026-10-09, icons.h's head). A DISABLED icon is
// ReactOS's saturate of that picture under every chrome (icons::draw_disabled,
// the program case's dead glyph), no role entering.
// TEXT OVER A FILL IS THE FILL'S OWN PAIR — a text role beside every ground
// role — AND COOL EDIT'S: the chrome's text is Cool Edit's program text off
// the Face, light on a dark Face and swapped dark past Cool Edit's own
// threshold (cool_edit_derive.h's text tones; architect 2026-10-10:
// "whatever luminous rules we previously had are superseded by the Cool Edit
// follow"), the field's text never swapping on its dark ground.
// STILL OPAQUE, STILL NO COMPOSITING, NO GRADIENTS, NO ROUNDED CORNERS, NO
// HOVER FACE ON ANY BUTTON: the program's cases have none under any chrome
// (Cool Edit's toolbar has none, program_spec.h's case_hot_face, 2026-10-09)
// and the chrome's buttons never had one; the menus' and lists' lit rows are
// a selection, not a button face. Every colour is a solid fill of integer
// cells. THE
// COLOR PICKER'S WHEEL IS THE ONE PLACE THE PRODUCT PAINTS COLORS THAT ARE
// NOT ROLES (architect 2026-10-07, by necessity — the wheel IS the color
// space, as the icons are their own inks: color_picker::paint_wheel, whose
// head owns the rule; its two swatches and the live colors it installs are
// the palette's own words). THE ONE
// EXCEPTION IS THE CAPTION'S GRADIENT (architect 2026-10-05), Windows 98 and
// 2000's title bar where a theme records a gradient end: a SMOOTH 24-bit
// ramp, each device column its own rounded colour (architect 2026-10-06,
// ReactOS's caption as captured) — still solid cells, nothing blended at
// paint time (paint_caption_gradient, the rule's one owner) — BESIDE THE
// PROGRAM CASE'S FACE AND SHADOW RAMPS (2026-10-09, Cool Edit's measured
// rows resampled to device rows, each row one solid colour;
// cool_edit_paint.h).
// A DITHER'S CELL IS ONE DEVICE PX (architect 2026-10-06, the whole chrome
// made scalable: "all colours used are period-authentic; dithering gets
// translated into whatever it perceptually becomes"): a dither stays a
// dither — no blended colour is ever computed in its place — but its cell is
// the panel's own pixel, the period technique at native resolution, so at
// 400 % the pattern is four times finer than a 96-dpi screen's and the eye
// blends it as it blended the original. THE ONE DITHER is the checked face
// and the popup lists' scroll track (paint_checker_rect, Windows' pattern
// brush; the trim track's checker went with the view bar, 2026-10-09).
// THIS PARAGRAPH IS THE RULE'S ONE STATEMENT; every other chrome length stays
// in Windows px (scaled_px).
//
// A HEX HERE IS AN sRGB BYTE TRIPLE, AS EVERY SCREENSHOT AND SCHEME FILE
// RECORDS IT (architect 2026-10-08 ~05:15: "if I open that image in the web
// browser, it would have the identical color on the GUI") — a palette role, a
// chrome role, the picker's hex, a catalog theme's byte. The tablet's window
// is a Display-P3 layer (GuiPlatform::adopt_window, platform_android.cpp), so
// THE TABLET CONVERTS sRGB -> DISPLAY-P3 AT THE PAINTER'S ENTRY
// (display_color; the math and every converting site at
// display_transform.h's head) and the panel presents the color the browser
// presents; the laptop's untagged sRGB surface takes the bytes as they are.
// From 2026-10-02 until this ruling the bytes went to the P3 layer
// unconverted, a hex then being a P3 triple.
//
// THE GRAMMAR IS WINDOWS 95's DrawEdge, AT THE WINDOWS PIXEL (architect
// 2026-10-02): every raised or sunken edge is TWO lines a side, each one
// Windows px (relief_line_px, below), the outer pair first and the inner pair
// inset one line, each pair's two tones MITRED where they meet (architect
// 2026-10-06, overruling Windows' square join: the top-right and bottom-left
// corner blocks split along the 45-degree diagonal, the rule and its words at
// paint_relief_frame, render.cpp). RELIEF COLOURS ARE SET IN ONE PLACE (architect 2026-10-04): every
// edge below takes the theme's quartet by Windows 95's scheme of how many
// lines a side, and no other role draws a relief line. THE FAMILIES (the
// painters are paint_relief_soft_raised and its siblings, below), lines given
// outer then inner, top-left / bottom-right, in the theme's quartet:
//   SOFT RAISED   Hilight / DkShadow, 3DLight / Shadow — a caption button
//                 (EDGE_RAISED | BF_SOFT; Windows' DFC_CAPTION, architect
//                 2026-10-05), an on-screen keyboard key, the scrub's
//                 pointed thumb (paint_scrub_thumb, 2026-10-06);
//   SOFT SUNKEN   DkShadow / Hilight, Shadow / 3DLight — a caption button
//                 pressed, a keyboard key checked or pressed;
//   PLAIN RAISED  3DLight / DkShadow, Hilight / Shadow — a push button, the
//                 scroll-bar thumb, a menu's and a dropdown's frame
//                 (EDGE_RAISED);
//   PLAIN SUNKEN  Shadow / Hilight, DkShadow / 3DLight — a field
//                 (EDGE_SUNKEN);
//   SUNKEN OUTER  ONE line, Shadow / Hilight (BDR_SUNKENOUTER) — the open
//                 menu title, the color picker's field and swatch, and THE
//                 PROGRAM'S FRAME round the program (Cool Edit's frame
//                 window, program_frame_rect, 2026-10-10);
//   ETCHED        a Shadow line with a Hilight line immediately beside it —
//                 a menu separator.
// (The toolbar band, row 8 and their cases and time fields, the view bar,
// the ruler and the marker lane are the PROGRAM'S since 2026-10-09 — Cool
// Edit's, cool_edit_paint.h — and wear none of these families; THE MITRE
// reaches them, architect 2026-10-09 ~12:40, through paint_relief_frame:
// cool_edit_paint.h's head.)
// THE CARD FRAME is no relief: ONE flat line in the `card_frame` role on all
// four sides (THE CARD FACE, below).
// The CHECKED face is Windows' dither: a checkerboard of Hilight over the
// ground in one-device-px cells (paint_checker_rect, below; the dither rule
// above).
//
// THE MAPPING (architect 2026-10-03; the roles 2026-10-04), role -> what it
// paints:
//   ground        every chrome surface: the menu row and the chrome's lanes
//                 under the band, the caption's buttons, THE BOTTOM
//                 OVERLAYS — the dialog (a prompt, a dialog editor or the
//                 picker's Cancel) and the on-screen keyboard, laid over the
//                 design at the window's foot since 2026-10-10 — the
//                 dropdowns, every chrome button face, a DISABLED
//                 cue's triangle (the band, row 8 and the canvas column's
//                 lanes are the program's panel since 2026-10-09, below);
//   label         chrome text and glyphs, the popup scroll bar's arrow
//                 glyph;
//   the quartet   every relief line, the families unchanged; Hilight also the
//                 light copy of THE DISABLED EMBOSS (below), a disabled cue
//                 label's included, and THE BOTTOM OVERLAYS' ONE TOP LINE
//                 (2026-10-10, paint_bottom_overlays); DkShadow also the
//                 dialog focus frame;
//   selected pair a dropdown's lit row, the folder
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
//   caption six   THE CAPTION (architect 2026-10-05, the window's title
//                 bar, top lane 0): the active start, gradient end and text
//                 while the window has the focus, the inactive three without
//                 it (paint_caption_row, paint_handler.cpp; the gradient at
//                 paint_caption_gradient). Its three buttons are Windows'
//                 caption buttons — the SOFT edges on the ground, the glyph
//                 in the label — and the sizing frame round a
//                 restored laptop window is the quartet and the ground
//                 (paint_window_sizing_frame);
//   THE PALETTE   the program's TWELVE (palette_file.h, 2026-10-07; twelve
//                 since 2026-10-09): the waveform's canvas, ink and outline
//                 (ROW 6, below; the ink also the view bar's span, its
//                 bevels derived from it); THE CANVAS'S TWO LINES, Cool
//                 Edit's — `grid` under the waveform and `center` on each
//                 channel's zero, over it (ROW 6); THE CUES' TWO COLORS,
//                 Cool Edit's —
//                 `cue` the red and `range` the blue, the triangles and
//                 their dots — and THE INVALID LABEL'S PAIR,
//                 `invalid_label` at rest and `invalid_label_selected`
//                 selected (THE MARKER LANE, below); the playhead's role
//                 `playhead_stem` — its head in the ruler and its dots in
//                 the canvas, nothing else (THE PLAYHEAD, below); the
//                 `scanner` — the playback line AND THE ZOOM ANCHOR'S STEM,
//                 the white of the stems that belong to the controls
//                 (architect 2026-10-09); and THE
//                 PANEL'S `face` (2026-10-09: the program's band, dock bar,
//                 row 8 and the canvas column's lanes — Cool Edit's — every
//                 other panel tone derived from it, the COOL EDIT BLOCK,
//                 cool_edit_derive.h).
// THE CARD FACE IS THE PERIOD'S TOOLTIP (architect 2026-10-04, reversing the
// card on the ground of 2026-10-03): the tooltip, both of its lines (no dimmed
// second line: Windows' ink, no dims), and every notification card stand on
// `card_ground` under `card_text` with a THIN LINE of `card_frame`, ONE
// Windows px (relief_line_px) and no relief line, on ALL FOUR SIDES
// (architect 2026-10-06; the cards take the tooltip's look, his word),
// ReactOS's tooltip on tmp/reactos-tooltips.png ("Views": WS_BORDER all
// round), Windows 2000's shape. ITS COLORS ARE COOL EDIT'S since 2026-10-10
// (architect: "tooltips and cards take the Face, the derived text and a
// dark border derived from the Cool Edit tones"): the panel's Face under
// the program text, framed in the `dark` tone, Cool Edit's box outline
// (chrome_derive.h) — Windows' cream InfoWindow under black retired. The line is painted as square cell rects on the face,
// owning its corners whole — a flat line, not a bevel, so the relief's mitre
// is not drawn on it (paint_popup_chrome's Info face). THIS PARAGRAPH IS THE
// RULE'S ONE STATEMENT.
// THE CANVAS'S FRAME IS THE PROGRAM'S (architect 2026-10-09, Cool Edit's
// canvas, the same under every chrome): one ring of the panel's tones round
// the waveform area — dark top and left, light right and bottom, mitred —
// with no chrome edge (paint_canvas_column_frame, paint_handler.cpp; the
// chrome's sunken well stood until that day).
//
// THE DISABLED EMBOSS — EVERY DISABLED WORD (architect 2026-10-03, Windows'
// DrawState DSS_DISABLED): the word in the theme's HILIGHT one Windows px
// right and down, then the same word in Shadow at its place over it (the
// light copy is Hilight by Windows' own rule, architect 2026-10-04, the
// separate light-copy role leaving with the dark level), show_embossed_run
// (below). A disabled GLYPH is ReactOS's saturate since the Tango set
// (architect 2026-10-06, icons::draw_disabled); nothing else dims — the dims of the kdenlive design (the disabled
// mix, the accelerator column's and the tooltip line's) retired with the
// luminance rule the same day. (A CUE'S LABEL IS THE ONE EXCEPTION: an inert
// cue label is the faded tone, not the emboss — the marker-lane paragraph.)
// A DROPDOWN'S ACCELERATOR COLUMN takes its
// item's own ink: the item's label ink, the selected text on a lit row, the
// emboss on a disabled one. A disabled glyph is the program's case's,
// ReactOS's saturate under every chrome (icons::draw_disabled, 2026-10-09).
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
// EVERY COLOUR THIS PRODUCT HANDS CAIRO GOES THROUGH ONE OF THESE TWO, and no
// site calls cairo_set_source_rgb itself (re-grepped 2026-10-03: none outside
// render.cpp's two bodies): set_palette_source for the chrome and
// set_waveform_source for the canvas's cairo fills, its ground and grid
// (render_canvas) and its center lines (render_center_lines). Each converts the authored sRGB color for the window
// (display_color, the painter's entry) and hands it over; the plate's own
// pixels are written as words (argb32_opaque_word), not through cairo, as
// are the caption gradient's ramp (paint_caption_gradient) and the checked
// dither's tile (paint_checker_rect), each converting at its own entry. TEXT
// takes its ink from the source these set: no glyph road sets a color of its
// own (text_shape.cpp draws in the current source).
void set_palette_source(cairo_t* cr, GuiColor c);
void set_waveform_source(cairo_t* cr, GuiColor c);

// -- THE ACTIVE PALETTE ---------------------------------------------------------
//
// The resolved colors, ONE STRUCT (2026-10-07): the chrome's ROLES —
// derived from the live palette and the live scheme's caption since
// 2026-10-10 (chrome_derive.h) — and the active palette's program roles, one
// field each — the chrome's in kGuiThemeRoles' order (theme_file.h), the
// program's in kGuiPaletteRoles' (palette_file.h), and THE COOL EDIT BLOCK
// derived from the program's `face` (cool_edit_derive.h's painted tones,
// 2026-10-09) — the three tables covering the struct exactly
// (palette_file.cpp's static_asserts); the mapping above says what each
// paints. Every field is a solid byte triple.
struct GuiPalette {
    // THE CHROME (Windows' element names; the values Cool Edit's tones and
    // the scheme's caption, chrome_derive.h).
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
    GuiColor caption_active;             // COLOR_ACTIVECAPTION
    GuiColor caption_active_gradient;    // COLOR_GRADIENTACTIVECAPTION
    GuiColor caption_active_text;        // COLOR_CAPTIONTEXT
    GuiColor caption_inactive;           // COLOR_INACTIVECAPTION
    GuiColor caption_inactive_gradient;  // COLOR_GRADIENTINACTIVECAPTION
    GuiColor caption_inactive_text;      // COLOR_INACTIVECAPTIONTEXT
    // THE PROGRAM'S OWN ELEMENTS — THE PALETTE (kGuiPaletteRoles,
    // palette_file.h).
    GuiColor waveform_canvas;
    GuiColor waveform_ink;
    GuiColor waveform_outline;
    GuiColor grid;
    GuiColor center;
    GuiColor cue;
    GuiColor range;
    GuiColor invalid_label;
    GuiColor invalid_label_selected;
    GuiColor playhead_stem;
    GuiColor scanner;
    // THE PROGRAM'S PANEL (architect 2026-10-09, the program is Cool Edit):
    // Cool Edit's Face, the ground of the toolbar band, the dock bar and row
    // 8 under every chrome.
    GuiColor face;
    // THE COOL EDIT BLOCK (cool_edit_derive.h's kTones, the painted ones):
    // the panel's tones, each DERIVED from `face` — the view bar's span
    // bevels from `waveform_ink` — by Cool Edit's own HLS rule at every
    // install of the program's words; no file names them; read by the
    // program's painters alone (cool_edit_paint.h, the canvas column's lanes
    // in render.cpp and paint_handler.cpp).
    GuiColor ce_recess;
    GuiColor ce_mid;
    GuiColor ce_dark;
    GuiColor ce_hilight;
    GuiColor ce_field_dark;
    GuiColor ce_field_light;
    // the program's time fields' digits, the never-swapping light text
    // (2026-10-10)
    GuiColor ce_field_text;
    // the panel label's ink and its (+1, +1) shadow, swapped to the engraved
    // pair past the label threshold (2026-10-10; cool_edit_derive.h's
    // label_ink / label_shadow, not the tone table)
    GuiColor ce_label;
    GuiColor ce_label_shadow;
    GuiColor ce_cue_shadow;
    // the view bar's span bevels, off the waveform's ink (2026-10-09)
    GuiColor ce_span_hilight;
    GuiColor ce_span_shadow;
    // THE BUTTON CASE, tinted by `face` (2026-10-10; cool_edit_derive.h's
    // case tones, the per-row fit): the glyph seat's ramp, one stop per W
    // row, the highlight, the shadow ramp's two ends and the shadow's corner
    // — the black outline and the down rings stay the painter's constants.
    std::array<GuiColor, 20> ce_case_ramp;
    GuiColor ce_case_highlight;
    GuiColor ce_case_shadow_first;
    GuiColor ce_case_shadow_last;
    GuiColor ce_case_corner;
    // THE DISABLED CUE'S FADED LOOK (2026-10-10; cool_edit_derive.h's
    // disabled_* mixes toward the Face): the triangle of the `cue` red, the
    // triangle of the `range` blue, the cue shadow, and the resting label.
    GuiColor ce_off_cue;
    GuiColor ce_off_range;
    GuiColor ce_off_cue_shadow;
    GuiColor ce_off_label;
};

// THE ONE ACCESSOR every painter reads. The installed palette is file-scope
// state in render.cpp, written by the install family below alone:
// install_palette at its ONE application point, gui_main's startup, before
// the window exists; and install_program_palette and install_chrome_pick,
// the picker's live road. Before the first install it is construction state
// (black), never painted.
const GuiPalette& palette();

// THE SURFACES WHOSE TEXT ROLE MAY DEPEND ON THE CHROME (2026-10-09): each
// surface's recorded text color under the live chrome — "text over a fill
// is the theme's recorded pair" — read by the words painted there, the one
// owner of the choice a later vocabulary with roles of its own would fork
// (no glyph reads it since 2026-10-09: the icons wear their own inks,
// icons.h's head):
//   CaptionActive    the caption's text, active and inactive — Windows'
//   CaptionInactive  CaptionText pair; the title (paint_caption_row)
//   ListRow          the list's text pair, resting and lit — the field's
//   ListRowLit       text and the selection's; the folder overlay's and
//                    the project picker's row names (paint_folder_overlay)
enum class GuiSurface {
    CaptionActive,
    CaptionInactive,
    ListRow,
    ListRowLit,
};
GuiColor surface_text(GuiSurface surface);

// A SCHEME'S KEYS — THE CAPTION ALONE since 2026-10-10 (architect
// 2026-10-10: "the caption remains the only thing that's outside of Cool
// Edit"; "scheme files shrink to the caption's keys"; every other chrome
// role inherits the palette's Cool Edit tones, chrome_derive.h): the title's
// start, end and text, and the inactive caption's three — palette_file.h
// owns their file grammar (kGuiChromeLines: the three of the block, all
// required, and the three inactive, each optional), chrome_derive.h the
// mapping onto the caption's roles. "WE SHOULD NEVER HAVE A TOGGLE"
// (2026-10-08): the caption's text is a picked word, never a rule's
// white-or-black. THE INACTIVE CAPTION'S THREE FOLLOW THE ACTIVE ONES while
// absent ("on the tablet I'm not even going to fill them out; there is no
// inactive state there") — the inactive_* accessors below resolve them.
// A SCHEME CARRIES NO FACE (architect 2026-10-09 ~21:20: "the scheme's
// default font should stop being honored — it should only be honored from
// the font picker"): the face is the device config's `font` key alone
// (gui_font.h's gui_live_face_set).
struct GuiChromePick {
    uint32_t                title_start    = 0;   // 0xRRGGBB, sRGB, each
    uint32_t                title_end      = 0;
    uint32_t                title_text     = 0;
    std::optional<uint32_t> inactive_title_start;
    std::optional<uint32_t> inactive_title_end;
    std::optional<uint32_t> inactive_title_text;
    constexpr uint32_t inactive_start() const {
        return inactive_title_start.value_or(title_start);
    }
    constexpr uint32_t inactive_end() const {
        return inactive_title_end.value_or(title_end);
    }
    constexpr uint32_t inactive_text() const {
        return inactive_title_text.value_or(title_text);
    }
    constexpr bool operator==(const GuiChromePick&) const = default;
};

// Install every color: THE PROGRAM'S off `program`, THE LIVE WORDS
// (2026-10-07, the color picker's round): the twelve program colors as they
// stand in the process, which the caller hands in explicitly — and THE
// CHROME'S, DERIVED FROM THEM (2026-10-10, the chrome inherits Cool Edit:
// every role but the caption's six a tone of the palette's face or ink,
// live_chrome_words, chrome_derive.h), the caption's six off `chrome`, the
// scheme's keys, or with none the compiled caption
// (chrome_caption(), theme_file.h). AT LAUNCH the palette the config's `palette`
// key names and the scheme its `scheme` key names (2026-10-08; through
// is_palette_name and is_scheme_name — a built-in or a file read at launch —
// or, with no line, the default palette and the live chrome's own scheme,
// effective_palette_name and effective_scheme_name, palette_file.h: gui_main
// resolves them with palette_record and scheme_record and passes the two).
// THE LIVE WORDS ARE THE TRUTH WHILE THE PROCESS RUNS: the config's
// `palette` and `scheme` keys name THE ACTIVE PRESETS (color_picker.h's THE
// PRESETS) and give the words at launch alone; a preset's load or Delete
// installs words, and a load, Save As, Rename or Delete writes its key
// (GuiColorPicker). This resolves and never refuses. It bumps
// palette_generation below, the flag cache's fingerprint term, and the
// plate's two baked inks (waveform_plate_inks) move with it. The words type
// is palette_file.h's. `chrome` is the scheme's keys (GuiChromePick above),
// none for the live chrome's own scheme.
void install_palette(const std::array<uint32_t, 12>& program,
                     const std::optional<GuiChromePick>& chrome);

// THE INSTALL FAMILY'S CAPTION MEMBER (2026-10-08, the chrome knob; the
// caption alone since 2026-10-10): the chrome's members re-derived off
// live_chrome_words(the live program words, `chrome`)
// (chrome_derive.h) and palette_generation bumped, the program's twelve
// untouched — only the caption's six can move. THE PICKER'S LIVE ROAD
// beside install_program_palette (GuiColorPicker::install_live_words, which
// runs that member's apply shape after both). Why that shape is enough for
// the chrome: the chrome's colors bake into one cached thing, the flag cache
// (a disabled flag's ground), keyed by the generation; the caption's ramp
// image keys its two words itself (paint_caption_gradient); no icon raster
// carries a role (icons.h's head); every other chrome painter reads
// palette() each frame (re-checked 2026-10-09).
void install_chrome_pick(const std::optional<GuiChromePick>& chrome);

// THE LIVE CHROME PICK AS INSTALLED — the caption keys the install family
// last wrote (install_palette's or install_chrome_pick's `chrome`), carried
// as given under every chrome — THE LIVE SCHEME, as program_palette_words is THE LIVE
// PALETTE (what the picker's Save writes and its enabled bit compares,
// color_picker.h), file-scope for the same reason.
const std::optional<GuiChromePick>& live_chrome_pick();

// THE LIVE WORDS AS INSTALLED — the program's twelve the install family
// last wrote (install_palette's `program`, or install_program_palette's
// `words`), in kGuiPaletteRoles' order (palette_file.h's GuiPaletteWords;
// spelled as its array type here because palette_file.h includes this
// header, which static_asserts the 12 against its table). They live beside the installed struct, FILE-SCOPE IN render.cpp
// AND NOT ON AppState (2026-10-07): AppState is rebuilt at every project
// reopen (gui_main's loop) while the installed palette is not, and the
// picker's picks are the PROCESS's until a preset is saved (color_picker.h's
// THE PRESETS) — so a reopen keeps them exactly as a Close does. THREE
// READERS: the color picker (every pick starts from these and writes one
// word), the picker's open (the live element's OLD color) and its presets
// (Save and Save As write these words, and Save's enabled bit compares them
// with the active preset's).
const std::array<uint32_t, 12>& program_palette_words();

// (THE INSTALL FAMILY'S SECOND MEMBER, install_program_palette — the
// program's twelve alone, the picker's live road — is declared in
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
// Settings menu's "True Colors" row writes the switch's process-lifetime
// bit (display_transform::set_true_colors) and bumps palette_generation, as
// every color the window is handed moves with it. EVERY CACHED CONVERTED
// PIXEL IS REBUILT, re-grepped 2026-10-08 against the head's list of
// converting sites: the flag cache by the generation bumped here; the
// waveform plate by its inks' fingerprint (waveform_plate_inks hands the
// window's words); the caption ramp and the color picker's ring and
// triangle by the conversion state in their own cache keys
// (paint_caption_gradient, color_picker.cpp's RingCache / TriangleCache);
// the checker's tile is built per fill and keeps nothing. THE APPLY SHAPE THE
// CALLER OWES, after the call (GuiInputHandler::toggle_true_colors): the
// icon rasters dropped (icons::drop_rasters — they convert at raster time
// and key on the device px alone), the plate kicked when its inks moved and
// the flag cache refreshed otherwise (install_program_palette's shape,
// palette_file.h), then the whole window invalidated — every color changes.
void install_true_colors(bool on);

// -- THE CANVAS COLUMN: THE VIEW BAR, THE RULER, THE PLAYHEAD -------------------
//
// THE CANVAS COLUMN IS COOL EDIT'S, THE SAME UNDER EVERY CHROME (architect
// 2026-10-09, the program is Cool Edit; the mock of record
// tmp/mocks/cool_edit/mock_CE_S3_GRID_4.png; program_spec.h owns every
// length): the column's 5 W of face under the band, THE VIEW BAR, THE RULER
// FLIPPED and THE MARKER LANE OF CUES, THE CANVAS under them in its frame
// (row 6's canvas paragraph), the view bar, the ruler and the canvas 6 W in
// from each side on the panel's face. Its colors are
// the panel's Face-derived tones (the COOL EDIT BLOCK), the waveform's ink,
// the cues' two colors, the invalid label's pair and the playhead's color
// (the palette), and the painter constants Cool Edit draws whatever its
// preset (cool_edit_paint.h).
//
// THE TRIM BAR IS COOL EDIT'S VIEW BAR (METRICS §3, §4.1; render_trim_flags
// owns the drawing, trim_lane_h_px the rows): A SUNKEN RING round a BLACK
// field 6 W tall (kCeViewBarField) — the `ce_mid` line on top and its dark
// LEFT column, the `ce_hilight` line under the field (the ruler's top line)
// and its light RIGHT column, mitred (2026-10-09, the third part: the
// column's two frame columns, the canvas's own, outside the field's
// columns, which are the canvas's); and in the field THE SPAN, the trim window's columns as
// a RAISED BLOCK OF THE INK — its top row and left column `ce_span_hilight`,
// its body `waveform_ink`, its bottom row and right column `ce_span_shadow`
// (the ink at HLS L 0.94 and 0.3157, cool_edit_derive.h), the two mitred
// where they meet (paint_relief_frame, 2026-10-09 ~12:40). NO PLAYHEAD IN
// THE BAR (architect 2026-10-09 ~14:30, "I thought we agreed no dots on the
// trim bar"; the mock of record draws none): Cool Edit's period-2 cursor
// column over its span (METRICS §3) is not drawn, so the bar is the black
// field and the span's block alone. No track, no caps drawn, no pressed
// face: the lane's grabs are the span's two ends and its body (TrimBarHit,
// below). (Windows' miniaturized scroll bar with its checker track and its
// arrow-button caps stood 2026-10-02 to 2026-10-09; git history.)
//
// THE RULER IS COOL EDIT'S, FLIPPED (METRICS §4.4; NOTES.md's "flipped
// ruler"; paint_ruler_row): `ce_mid` ground under the view bar's light line,
// its own `ce_hilight` line along its foot, a `ce_dark` LEFT column and a
// `ce_hilight` RIGHT column beside the ground (2026-10-09, the column's
// frame columns; the dark one meeting the foot line mitred); THE TICKS kCeRulerTick (E0E0E0,
// face-independent in every preset), one quantum wide, STANDING ON THE
// GROUND'S BOTTOM ROW and rising into it, painted over the digits' shadow
// where a major's top row meets it; THE DIGITS the base's SIX-ROW SMALL
// DIGIT (GuiFace::Small; architect 2026-10-09 ~16:50 / ~21:00, Cool Edit's
// cap-7 digits "very small" there anyway) on the ground's rows 1 .. 6 under
// one row of air, "basically touching the trim bar", in the tick's ink over
// a BLACK (+1, +1) shadow (kCeRulerShadow) — the ground 11 W where Cool
// Edit's is 17, the six rows the marker lane's (program_spec.h's ruler
// fields).

// THE PLAYHEAD IS COOL EDIT'S CURSOR (METRICS §4.3; architect 2026-10-09,
// the mock of record): ITS HEAD the cue's triangle — the 9-7-5-3-1 quanta's
// envelope drawn as one antialiased triangle, the apex the playhead's
// column (paint_ce_cue_triangle, 2026-10-09 ~12:40) — in THE PALETTE'S
// `playhead_stem` (architect 2026-10-09 ~11:50; Cool Edit's Curs yellow
// FFFF00 in every default palette) over the same triangle one quantum right
// in `ce_cue_shadow`, ON THE RULER'S ROWS 6 .. 10, the apex on the
// ground's bottom row, its top row the digits' last — it covers what stands
// under it (2026-10-09 ~21:00, the mock of record)
// (paint_ruler_row; cue_triangle_h_px's rows; its painted columns, the cull
// and the move's damage box one extent, cue_triangle_reach_right_px); IN THE
// CANVAS a DOTTED COLUMN, one device px every four device rows in the same
// color (2026-10-09 ~14:25, "on waveform → unscaled") on the canvas's rows
// ≡ 1 (mod 4), the cues' two colors on rows ≡ 3 and
// ≡ 7 (mod 8), so they interleave on one column (paint_playheads,
// fill_dotted_waveform_line). Across the view bar (architect 2026-10-09
// ~14:30, "no dots on the trim bar"), the marker lane and the canvas's top
// frame row it draws nothing (Cool Edit puts no dot on its frame row). IT MAY STAND ON A MARKER'S COLUMN — NO SNAP, NO AVOIDANCE (architect
// 2026-10-09): the head and a cue's triangle stand in different lanes and
// the dots interleave, so the coincident-stem suppression of 2026-08-01 to
// 2026-10-09 (the playhead's stem yielding whole to a marker's) is retired.
// THE HEAD IS GRABBED AND DRAGGED, as Cool Edit's is (architect 2026-10-09
// ~17:40): its HIT is the head as painted, its columns (the triangle and its
// shadow, cut to the waveform's) over its five quanta of the ruler's rows,
// no box beyond them — published by paint_ruler_row
// (AppState::playhead_head_hit) and read by point_on_playhead_head
// (input_pointer.cpp), a cue over the head keeping the press; a plain press
// there drags the playhead, every motion past the slop the ruler's
// placement act at the pointer's column (ScrollDragState::head_drag).
// NO HOLD LAMP (architect 2026-10-05): the head paints the same whether or
// not AppState::camera_hold stands. (WordPad's ruler marker in the chrome's
// four roles stood 2026-10-05 to 2026-10-09; the solid `playhead_stem` line
// before it; git history.)
// THE PLAYHEAD ALONE IS YELLOW: `playhead_stem` is its head and its dots
// and nothing else (architect 2026-10-09). THE STEMS THAT BELONG TO THE
// CONTROLS ARE WHITE, ONE ROLE, `scanner` (architect 2026-10-09: "the zoom
// stem and the scanner are both white, which unifies them: the non-playhead
// stems that are related to the controls should be white, and then the
// playhead is yellow"): THE SCANNER — the moving playback line,
// paint_scanner, its own role since 2026-10-05 — a SOLID line one device px
// wide over the canvas (Cool Edit's playback cursor is a solid 1-px line,
// measured 2026-10-09; unscaled on the canvas since ~14:25,
// waveform_line_px's rule), and THE ZOOM ANCHOR'S STEM
// (render_strip_anchor_stem), the same solid one-px line at the pivot;
// WHITE in every built-in palette (architect 2026-10-07: "let's go back to a
// white scanner").

// -- THE MARKER LANE: COOL EDIT'S CUES ---------------------------------------------
//
// THE FLAG IS COOL EDIT'S CUE (architect 2026-10-09, the mock of record and
// NOTES.md's "cue construction"; METRICS §4.2; the lengths program_spec.h's,
// the drawing render_flag_boxes_impl's): on the lane's PANEL FACE, THE
// TRIANGLE — the 9-7-5-3-1 quanta's envelope on the lane's last five rows,
// one antialiased triangle (paint_ce_cue_triangle, 2026-10-09 ~12:40), its
// apex the marker's column on the lane's last row, directly above the
// canvas's top frame row — over the same triangle one quantum right in
// `ce_cue_shadow`;
// THE LABEL — the marker's one label in the program face at cap 7, in the
// panel's light tone `ce_hilight`, no shadow, starting six quanta right of
// the column, ITS LINE BOX CENTRED IN THE LANE (architect 2026-10-09
// evening, on the glass at 300 %: the editor's band stood three rows of
// field above and six below, and he asked for it even; the cap band's
// centring of ~21:45 retired;
// cue_baseline_px, the box rule): at 100 % the box on rows 2 .. 13, the
// cap on rows 5 .. 11, the baseline the top of row 12,
// its descenders and the history's brackets reaching into rows 12 .. 14
// beside the triangle's rows 12 .. 16 — THE LANE 17 W, the product's own
// (architect 2026-10-09 ~16:50 / ~21:00, Cool Edit's 11 grown by the six the
// ruler gave; program_spec.h's marker-lane fields); and THE STEM —
// DOTS ONE DEVICE PX SQUARE on the canvas's device rows (2026-10-09 ~14:25,
// "on waveform → unscaled"), the canvas alone (no dot
// in the lane or on the frame row; paint_marker_stems). No box, no outline,
// no stem through the frame row.
//
// THE CUES ARE COOL EDIT'S TWO COLORS (architect 2026-10-09 ~11:50–12:00:
// "red+blue for warp and phase, red only for invalid (half the amount of
// dots) and for history blue only for + and red only for −"; METRICS §4.2):
// THE RED `cue` (Cool Edit's CueM, F34B58 in every default palette) and
// THE BLUE `range` (its RngM, 4B82F3). A point cue's stem is Cool Edit's
// range start and end superimposed — the blue dots on the canvas's device
// rows ≡ 3 (mod 8), the red on rows ≡ 7, one dot every four rows in
// alternating colors; a one-color stem keeps its color's phase alone, half
// the dots (program_spec.h's dot fields). WARP AND PHASE RESET WEAR THE
// SAME CUE: the kind reads from the label alone (his one-label rule).
// THE TRIANGLE NEVER CHANGES WITH SELECTION (Cool Edit's never does). THE
// TABLE — which triangle, which dots, which label — is resolve_flag_face's
// head (render.cpp), the one resolver every cue goes through.
//
// THE LABEL'S SEGMENTS: the marker's payload label, then — while grid
// iterations paint them — its LOWER and UPPER bound cells, each a segment of
// the same label standing the segment gap past the one before it (the
// history's changed pair likewise: its removed half, then its added half),
// every segment's text the program face on the lane's baseline.
//
// THE OVERLAP RULE (architect 2026-10-09, settled on the five overlap
// captures of NOTES.md set 2): the labels paint RIGHT TO LEFT, each on an
// OPAQUE FACE BOX over its own extent (THE WHOLE LANE'S ROWS, 0 .. 16 — the
// press target's rows, so the label is one geometry at rest and selected,
// 2026-10-09 ~21:00, a selected label taking no fill of its own, ~23:30;
// from one quantum before its text to one past its last segment's), and a
// label is CLIPPED AT THE
// NEXT TRIANGLE'S LEFT EDGE (the next column less four quanta) when the next
// marker stands more than the six-quantum lead to its right; within the
// lead it is not clipped and overprints; then the triangles paint LEFT TO
// RIGHT over every label, so a right triangle covers its left neighbour's
// shadow.
//
// THE STATES (resolve_flag_face, render.cpp, the one ladder: disabled wins,
// then invalid, then the kind):
//   THE CUE   the red triangle, the red and blue dots, the label in
//             `ce_hilight`;
//   INVALID   the red-flag class (warp_red_flag_set_cached and its phase-reset
//             twin): the red triangle, THE RED DOTS ALONE, and THE LABEL RED
//             IN BOTH STATES (architect 2026-10-09 ~12:00: "keeps red for the
//             text, even unselected — dimmer unselected, brighter
//             selected") — `invalid_label` at rest, `invalid_label_selected`
//             when selected, on the panel's face in both;
//   DISABLED  THE FADED LOOK (architect 2026-10-10 ~12:20, "the arrow needs
//             to be legible for me, but the text doesn't … since it's
//             disabled, it's inert"): the kind's triangle color and the cue
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
//             2026-10-10, when it became the ink darkened for the ink's
//             fill, chrome_derive.h); the segment's resting face box (the
//             overlap rule's, below) stays as it is, the text not moving,
//             on the baseline of its line box centred in the lane as at
//             rest (cue_baseline_px, 2026-10-09 evening) (an invalid cue's
//             text its bright red);
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
//             changed pair, one item, turns both halves at once;
//   SELECTED DISABLED (architect 2026-10-07 ~15:10: a disabled marker is
//             selectable, so its selection must show): the selected label —
//             its text in the chrome's `label` on the face in place of the
//             faded label, no fill — over the faded triangle, no dots.
// A CUE HAS NO HOVER FACE (architect 2026-09-29): the pointer over a cue says
// what a press will do through the CURSOR alone (pointer_cursor_kind).
//
// THE PHASE-RESET LEAD-IN RING on the waveform is DOTTED IN ITS RESET'S STEM
// PATTERN, never a solid line (paint_phase_reset_overlay_ring, through
// phase_reset_stem_dots — architect 2026-09-17, the ring and the cue one
// unit; 2026-10-09 ~17:40, "made of the same stuff the marker looks like"):
// its left side the stem itself, its right side the stem's dots on the
// band's last column, its top and bottom runs the same two phases along the
// canvas's first and last rows; the red and the blue for a valid reset, the
// red alone for an invalid one; resting whether selected or not, as the
// stem is.
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
// its descent below it (gui_face_metrics, 10 + 2 W), the dialog field's own
// band rule (paint_modal_dialog) — Windows fills the line, and the 17-W
// field round a 12-W line leaves rows of field above and below it. THE LINE
// BOX IS CENTRED IN THE LANE (architect 2026-10-09 evening, on the glass at
// 300 %: the band stood three rows of field above and six below, and he
// asked for it even): the band's rows ARE cue_line_box_top_px's and
// cue_line_box_h_px's, the one derivation the baseline is defined from, so
// the band and the text cannot part. At 100 % the band on lane rows 2 .. 13
// inside the inner rows 1 .. 15, one row of field above and two below; at
// 138 % rows 2 .. 18 inside 1 .. 20, one and two; at 300 % rows 7 .. 42
// inside 3 .. 47, four and five; at 360 % rows 10 .. 52 inside 4 .. 58, six
// and six — the odd leftover row below, the box rule's. A selected
// glyph's ink past the band (a descender deeper than the recorded 2 W)
// takes the field text, the field ink's clip the band's complement in both
// its columns and its rows. The field grows and
// shrinks with what is typed, the
// segments to its right riding its edge in their resting look; and THE
// SELECTED SUBSTRING IN THE CHROME'S SELECTED PAIR, `selected_text` on
// `selected_fill`, the ordinary highlight. AN INVALID MARKER'S FIELD TEXT
// AND CARET ARE ITS LABEL'S BRIGHT RED, `invalid_label_selected`, on the
// field's ground (architect 2026-10-09 ~12:00; the open editor is the cue
// in its selected look), the outline and the selected substring unchanged,
// its riding segments in the resting dim red. WHY THE FIELD PAIR: it is the
// legibility guarantee ("when you're looking very closely at a label it has
// to be legible"), and the selection pair is the highlight "precisely to
// avoid this problem" — the selected look of the resting label could not
// carry a band of its own pair. THE BOX PAINTS OVER THE LANE, the triangle
// and any neighbouring label ("it may overlap with the flag and other
// elements, that's fine"). Outside the editor the selected label is its
// light text on the panel's face, no fill and no box (SELECTED, above); the
// open editor shows its whole text selected in the selected pair inside the
// field — "a fully highlighted white text editor" under Windows 2000's pair,
// the ink under its darkened text since 2026-10-10 (chrome_derive.h).
//
// A REFUSED ENTER RECOLORS NOTHING (architect 2026-10-03: "a red outline and
// the card is redundant"; the red frame retired from both editors): it
// SELECTS THE WHOLE TEXT in the selected pair inside the field
// (text_editor::refuse), so the first keystroke replaces it, and the
// refusing owner's CARD says why. No glyph marks it.
//
// THE HISTORY VIEW'S DIFF FLAGS are cues of their own two kinds (architect
// 2026-10-04; the colors 2026-10-09 ~11:50): an ADDED half the BLUE
// triangle with the blue dots alone (Cool Edit's range end), a REMOVED half
// the RED triangle with the red dots alone (its range start) — a changed
// pair's triangle the removed half's, the half its dots leave from — and
// THE LABEL CARRIES THE SIGN — the history mode's one bracket
// spelling, `[+]` before an added line's payload and `[-]` before a removed
// one's (architect 2026-08-05; history_diff_label, paint_handler.h, the same
// sign row 8's walk line spells), a changed pair's label its two halves as
// two segments. The view's focus or selection turns the whole label, both
// halves, the selected text on the face, no fill — the live lane's SELECTED
// row (render_history_diff_flags).

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
// palette's, Cool Edit's "Default" (`cool-edit-default`, the default under
// every chrome, palette_presets.inc), is #0A9757 under its #4BF3A7 ink on
// black — the ink at HLS lightness 0.3157, the generator's outline rule
// (tools/theme_catalog/gen_cool_edit_presets.py). THE PLATE
// BAKES THE INK AND THE OUTLINE (render_waveform writes their words), so the
// pair rides each render job and the cache's fingerprint
// (waveform_plate_inks); the canvas is laid live under the plate's
// transparent gaps (render_canvas) and bakes nowhere.
//
// THE CANVAS IS COOL EDIT'S (architect 2026-10-09, the third part; METRICS
// §4.1 and §4.4, NOTES.md's set 3, the mock of record
// tmp/mocks/cool_edit/mock_CE_S3_GRID_4.png): THE WAVEFORM AREA IS THE
// CANVAS'S INTERIOR, inside its frame (the ring of the panel's tones, the
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
// its two inks alone); then the lead-in ring, the dots, the scanner and the
// zoom anchor (on_redraw's paint order). THE PICTURE IS ONE THING — the
// canvas, the grid, the ink if any, the center lines — SO THE CENTER LINES
// PAINT WHENEVER THE GRID DOES: over the plate while audio is loaded (a null
// plate's frame included, its blit skipped), and with none loaded — no
// source, or a load running — directly over the canvas and its grid, right
// after render_canvas (on_redraw's `plate_branch`).
// NO VERTICAL GRID (architect 2026-10-09 ~18:40: Cool Edit's verticals on
// the ruler's major ticks, drawn that afternoon, were "solid versus the
// markers dotted, they compete" — "drop them"). HIS PRINCIPLE, recorded
// here: "accuracy is what applies to the conversion from Cool Edit Pro, and
// accuracy is lower priority than usability" — Cool Edit's rule yields
// where usability says so, while "truthfulness applies to the way we
// interact" (an invisible hit box is untruthful; on screen is as painted).
// A recorded departure from Cool Edit (win2000_deviations.md), as THE CENTER
// LINE OVER THE INK is (the mock of record drew it under the waveform with
// the grid).
// The waveform's plate is blitted over the ground and the grid, the ink over
// every grid line, the center lines over the ink. Each
// channel's band and zero row are the plate's own (waveform_channel_band
// below), so the lines stand where the plate's zero is. THE CHANNELS FILL THE
// CANVAS (architect 2026-10-09, "remove", the mock of record's channels top
// to bottom): each takes the canvas's halved and floored height, channel 0
// from its first row and channel 1 under it (an odd canvas's last row below
// channel 1, no ink reaching it), with NO INSET above or below — the 6-W
// band that stood there was the tip-down playhead triangle's seat (deleted
// 2026-08-02) kept as a symmetric margin, and the playhead's head and the
// cues' triangles stand above the canvas's frame, so nothing needs it; the
// dots, the lead-in ring and the scanner paint over the ink there as they do
// everywhere on the canvas. The leveler's headroom (waveform_gain.h's
// constants: its target, and the inner bar's foreground half) is the only
// thing keeping the peaks off the edges; an outer bar the clamp clips flat
// (the loud body, accepted there) stands on its band's edge row — the
// canvas's first or last row against the frame, or the row where the
// channels meet. NO BOUNDARY LINES
// AND NOTHING BETWEEN THE CHANNELS (architect 2026-10-09, "we have very
// limited vertical real estate"): the two channels abut, Cool Edit's Bndy
// clip guides are not drawn, and the chrome's two-line sunken well that stood
// round the area (the theme's quartet, 2026-10-02 to 2026-10-09) is gone
// with the content band it carved out of the area. THE CUES' AND THE
// PLAYHEAD'S DOTS and the scanner span the canvas, their dot rows counted from
// its first row; THE ZOOM ANCHOR'S STEM spans it too.

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
// asks for its text — the body, the caption's bold or the ruler's small face
// at the current gui_scale.
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
// may legitimately vanish floors at 0 and says so at its own accessor
// (popup_sep_margin_y_px).
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

// Defensive window floor (a conservative 640x480 minimum). Enforced two ways:
// the Wayland set_min_size hint at toplevel creation, and an internal clamp in
// the geometry helpers so the waveform arithmetic is always valid regardless of
// what the compositor sends. Not sized to fit content — the longest dialogue
// may clip at the floor, which is acceptable (nobody authors at 640x480).
constexpr int kMinWindowWidthPx  = 640;
constexpr int kMinWindowHeightPx = 480;

// THE CAPTION — the top strip's lane 0, at the window edge: THE WINDOW'S OWN
// TITLE BAR, painted by the app on both devices (architect 2026-10-05; the
// laptop asks labwc for client-side decorations, so labwc draws none, and the
// tablet's full-screen window has none of its own). THE PERIOD'S CAPTION AT
// THE WINDOWS PIXEL, every number the live chrome spec's (chrome_spec.h's
// caption_* fields; architect 2026-10-07: each vocabulary's own), which
// records the vocabulary's measured caption — Windows 2000's SM_CYCAPTION
// and DrawFrameControl box on the ReactOS captures (tmp/reactos*.png):
//   THE LANE is caption_height_px whole (18 Windows px under win2000): the
//     caption's ground across the whole lane, under the icon and the buttons
//     too — the active or inactive start-to-end colours,
//     paint_caption_gradient's smooth ramp (ReactOS's span), nothing above or
//     below it inside the lane;
//   THE ICON, the app's own (icons::Icon::AppIcon — the launcher's), 16 x 16
//     (SM_CXSMICON; kCaptionIconPx) at the spec's (caption_icon_x_px,
//     caption_icon_y_px) from the lane's top-left — (2, 1) under win2000 —
//     the set's drawing (icons::draw);
//   THE TITLE in THE BOLD FACE (GuiFace::Bold, gui_font.h: the live set's
//     bold, the caption font being the body face in bold), its pen at
//     caption_title_x_px (20, two px past win2000's icon) and its cap band
//     centred in the lane (redesign_baseline) — cut before the buttons with
//     Windows' "..." (paint_caption_row owns the words and the cut);
//   THE THREE BUTTONS, flush right: each caption_button_w_px x
//     caption_button_h_px with caption_button_y_px of caption above it,
//     caption_button_gap_px between Minimise and Maximise,
//     caption_close_gap_px before Close and caption_button_inset_px right
//     of Close — win2000: 16 x 14 two px down, Minimise and Maximise
//     touching, 2 before Close, 2 right of it, Windows' caption button,
//     DrawFrameControl's DFC_CAPTION: EDGE_RAISED with BF_SOFT, and
//     EDGE_SUNKEN with BF_SOFT while pressed (paint_button_box's TOOLBAR
//     family, the SOFT edges; the reference's white outer line and 3DLight
//     inner one agree), its glyph Marlett's character in the label
//     (paint_handler.cpp's caption glyph block: Minimise, Maximise and
//     Restore authored cells — ReactOS's 7-wide Minimise and Restore — and
//     Close the smooth outline of Marlett's staircase, the cell on its unit
//     grid at Windows' (3, 2)), sunken and shifted one px while pressed.
// Every length is a composite of its rounded parts (scaled_px's rule): the
// win2000 lane 54 device rows at the tablet's 300 %, 25 at the laptop's
// 138 %.
inline constexpr int kCaptionIconPx = 16;
inline int caption_row_h_px() {
    return scaled_px(live_chrome_spec().caption_height_px, 5);
}

// THE CAPTION'S GRADIENT — THE ONE GRADIENT IN THE PRODUCT (architect
// 2026-10-05; the palette head's exception), its rule and reason at the
// definition (render.cpp): `start` at the left of `r`, `end` at its right,
// linear per channel across the width, each device column's rounded 24-bit
// colour (architect 2026-10-06); a flat caption (end equal to start) is one
// solid fill of the exact colour. Opaque.
void paint_caption_gradient(cairo_t* cr, const GuiRect& r, GuiColor start,
                            GuiColor end);

// THE SIZING FRAME (architect 2026-10-05) — Windows 95's sizable window
// border, 4 Windows px (SM_CXFRAME, kWindowFramePx), drawn round a RESTORED
// laptop window and never round a maximised one (Windows hid it when
// maximised; the tablet's window is always maximised): the window's raised
// edge on its outer two lines (PLAIN RAISED, DrawEdge's EDGE_RAISED, the
// quartet) and the ground on the two inside them. Painted by the Wayland
// backend on the surface's outer `frame_px` device px (the client area inside
// it is the app's whole geometry, platform_wayland.cpp); its edges and
// corners are the resize handles (window_frame_edges_at, app_state.h). THE
// THICKNESS IS A COMPOSITE OF ITS ROUNDED PARTS (scaled_px's rule): two
// relief lines and two Windows px of ground, window_frame_px — 5 device px at
// the laptop's 138 % (1 + 1 + 3), 12 at 300 % (3 + 3 + 6). The relief
// lines are the spec's window_frame_lines; a vocabulary whose spec sets
// window_frame_maximized keeps the band on every window, the maximized
// laptop's and the tablet's too (both platforms' frame_px_), the hit test
// unchanged (window_frame_edges_at), its band answering a resize only while
// the window is restored (claim_window_frame_press). `focused` is the
// window's activation, which Windows 2000's frame does not show (its ground
// and quartet stand the same either way); the platforms pass it for a
// vocabulary whose frame follows the activation.
inline constexpr int kWindowFramePx = 4;
int  window_frame_px();
void paint_window_sizing_frame(cairo_t* cr, int surface_w, int surface_h,
                               int frame_px, bool focused);

// Authored pixel geometry of the MENU ROW — the top strip's lane 1, directly
// under THE CAPTION (the kdenlive menu bar, row 1 of the redesign). ITS
// TWO TERMS ARE THE LIVE CHROME SPEC'S (chrome_spec.h's menu_row_*
// fields; architect 2026-10-07, each vocabulary's own), each its own
// rounded part: A HEAD ROW of plain ground and THE CONTENT — the anchors'
// box and the labels' box both, where the anchors and their labels stand, the
// label seated in it by the vocabulary's rule (paint_menu_row: the cap band
// centred under win2000). THE ROW ENDS AT ITS CONTENT, with no line of its
// own under it (architect 2026-10-10 ~08:10: Cool Edit's plain menu bar has
// none; the line under it is the PROGRAM'S FRAME's top row, program_frame_rect
// below).
//   WIN2000: head 1 + content 19 = 20. THE CONTENT IS 19 WINDOWS
//   PX (architect 2026-10-02; it was the kdenlive File item's 30 laptop px
//   until the unit's change), since 2026-10-06 read as EXPLORER'S MENU
//   BAND's button — the 13-row cell + comctl32's DEFPAD_CY 6 (toolbar.c,
//   TOOLBAR_MeasureButton; the band's record at paint_handler.cpp's head of
//   the menu row) — the cap's top on row 5 of the content since 2026-10-09,
//   the plain menu bar's seat his WordPad captures of the real OS show, the
//   cap band centred (paint_menu_row; ReactOS's band seat, row 6, departed
//   from, win2000_deviations.md). THE HEAD ROW
//   above it, directly under the caption (architect 2026-10-05, Windows 95
//   screenshots measured at 100 % agreeing on a 20-px menu band; the row's
//   place architect 2026-10-06, ReactOS: caption 18, face 1, menu 19): the
//   content's 19 is kept exactly — the number this row's rulings and the
//   architect's live judgments were made against — and the extra row is
//   added beside it rather than folded into it, so EVERY LABEL STAYS AT
//   ITS PLACE IN THE CONTENT (the painter seats on the CONTENT rows, never
//   the lane's, below).
// UNDER THE LANE STANDS THE PROGRAM'S FRAME's top row, one Shadow line across
// the client, and then the program's band (program_frame_rect, below; Cool
// Edit's capture: the menu bar's last row, then the 808080 row, then the
// band's dark line).
//
// THE ANCHOR IS THE LANE, NOT THE CONTENT (architect 2026-09-09: "make the
// height of the top row based on the thirty pixels of File/Edit ... this way
// the dropdown will touch the first row, as it does in kdenlive"; the number
// Windows' menu bar's since 2026-10-02, the face row added 2026-10-05): each
// anchor's rectangle fills the LANE top to bottom and IS the anchor's
// published hit rect (paint_menu_row; the open anchor's sunken box stands
// round the content rows alone, as ReactOS's does round its 19-row item),
// and the anchor's foot is the lane's foot. THE DROPDOWN AND ITS DAMAGE BAND
// HANG FROM THE CONTENT'S FOOT, which is the lane's (dropdown_hang_y, which
// paint_dropdown and toggle_dropdown both read, owns the seat): a Windows
// pull-down drops from the menu bar's bottom row and covers what stands
// under it, so the popup covers the program frame's top row while it stands.
//
// FLUSH UNDER THE CAPTION (the window's top edge until the caption's arrival,
// 2026-10-05), WITH NO AIR ABOVE THE ANCHORS beyond the head row (architect
// 2026-10-01, on the glass: "Let's remove the six pixel padding up at the
// top. We'll just let the text be pretty close. I think that's going to seem
// more symmetric — because right now, relative to the curved top, both the
// clock and the File dropdown look too far down").
//
// Every term sizes on gui_scale_factor() like every other lane in the tree,
// rounded with std::nearbyint through scaled_px and floored like every other
// lane metric, EACH ITS OWN ROUNDED PART (the composite rule): win2000's
// content 57 device rows at the tablet's 300 %, 26 at the laptop's 138 %,
// its head row 3 and 1 — lane totals of 60 and 27 (72 at 360 %). TWO
// ACCESSORS FOR TWO READERS, deliberately: the
// lane table and the anchor/hit-rect geometry read the LANE
// (menu_row_h_px), the label's seat, the open box and the dropdown's seat
// read the CONTENT alone (menu_row_content_rect, paint_menu_row,
// dropdown_hang_y).
inline int menu_row_content_h_px() {
    return scaled_px(live_chrome_spec().menu_row_content_px, 5);
}
inline int menu_row_head_h_px() {
    return scaled_px(live_chrome_spec().menu_row_head_px, 1);
}
inline int menu_row_h_px() {
    return menu_row_head_h_px() + menu_row_content_h_px();
}
// The content's rows in the menu lane `lane`: under the head row.
inline GuiRect menu_row_content_rect(const GuiRect& lane) {
    return GuiRect{lane.x, lane.y + menu_row_head_h_px(), lane.w,
                   menu_row_content_h_px()};
}
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

// THE DIALOG FIELD — ONE SHAPE UNDER EVERY CHROME: every text field of the
// modal dialogs (paint_modal_dialog's editor arm, the choice editor's combo
// at its seat) and the color picker's hex field is this tall, its edge
// included, with its text this far in from its outer edge — not a chrome
// spec field, a field to be made only when a later vocabulary's differs.
// WIN2000 Windows' 14-dialog-unit edit box beside its 14-unit push button,
// 23 W (the laptop pixel's 31 re-authored at the unit's change, 2026-10-02,
// the pad's 7 as 5 — paint_handler.cpp's kModal* block keeps the sampled
// record). THE TIME FIELDS are not this field: row 8's clock and the render
// player's position and length are Cool Edit's dark field, its height and
// its pad program_spec.h's own (architect 2026-10-09 evening, "the other
// ones are Windows chrome whereas this one is a Cool Edit chrome") — the
// pad 5 W, equal to this field's since 2026-10-10 ("let's add back one
// pixel on the left and one on the right side for that input box";
// program_spec.h's field_pad_px holds why). (They took this whole shape
// 2026-10-08 to 2026-10-09.)
inline constexpr double kModalFieldHeightPx = 23.0;
inline constexpr double kModalFieldPadXPx   = 5.0;

// THE RELIEF LINE — ONE WINDOWS PX, the width of every line of a raised,
// sunken, status or etched edge in the chrome, and of one cell of the checked
// dither (architect 2026-10-02: Windows' DrawEdge draws one-pixel lines, two
// to an edge; the grammar is at the palette head): 3 device px at the
// tablet's 300 % (its scale since 2026-10-06), 4 at 360 %, 1 at the laptop's
// 138 %, floored at 1 so it never vanishes at 50 %. The relief helpers
// (paint_relief_soft_raised and its siblings) paint every line at it; the
// program's frame below is one of them (program_frame_line_px).
inline constexpr int kReliefLinePx = 1;
inline int relief_line_px() {
    return scaled_px(kReliefLinePx, 1);
}

// THE PROGRAM'S FRAME — COOL EDIT'S FRAME WINDOW ROUND THE PROGRAM (architect
// 2026-10-10 ~08:10, "Yes, okay to implement", on the planner's mocks
// tmp/mocks/ring/; his ruling on the etched pair the menu row wore under it
// that morning: "the separator between the first row and the icon row is
// wrong in our rendition. It's not a window separator. That separator, I
// think, is part of the Cool Edit chrome"). THE LAW, his captures of Cool
// Edit Pro 2.1 — Wine with no theme, 1920 x 1080 maximized, and a Windows
// 95/98 hardware GIF agreeing: the plain menu bar ends at its content with
// NO LINE UNDER IT (WordPad's etched pair is its REBAR's band border, and
// Cool Edit has a plain menu bar); the row under it is 808080 (Shadow), then
// Cool Edit's dark band line; 808080 runs on down the window's first column
// and FFFFFF (Hilight) down its last; FFFFFF crosses the row directly under
// Cool Edit's lowest panel, and Windows' status bar (the ground) stands
// BELOW that row, outside it. So Cool Edit's frame window draws ONE SUNKEN
// LINE — BDR_SUNKENOUTER, Shadow on the top and left, Hilight on the bottom
// and right — in the SYSTEM colors round the whole program: THE CHROME'S
// Shadow and Hilight roles, which follow the scheme (under cool-edit-pro-me
// the derived 414752 / AEB5BF off its ground 626C7B, Cool Edit's own panel
// face, since 2026-10-10 ~08:45 — the Shadow row then reads as its own
// shade above the band's darker line), never the panel's Face-derived
// tones; the
// two two-tone corners MITRED (paint_relief_sunken_outer over
// paint_relief_frame, the program's diagonal rule at cool_edit_paint.h's
// head).
// THE GEOMETRY (main.cpp's lane table; one geometry for paint and hit): the
// ring's TOP ROW is a top lane of its own between the menu row and the band,
// its BOTTOM ROW a bottom lane of its own, and its two COLUMNS stand on the
// client's first and last line — every program lane (the band, the canvas
// column's lanes and its foot, the dock bar and row 8) inset one line on each
// side inside them (strip_row_rect). The ring spans the client, inside the
// restored laptop's sizing frame. The pair's two lines became the ring's top
// and bottom rows, so the well keeps its height: 960 device rows at 300 %.
// At 300 % the menu row ends at row 114, the ring's top row is 114–116, the
// band starts at 117, the ring's columns are 0–2 and 2301–2303 and its
// bottom row the window's last three rows.
// THE CHROME SURFACES AT THE BOTTOM ARE OVERLAYS LAID OVER IT, as Cool Edit's
// status bar stands under its panels (architect 2026-10-10: "they're
// analogous to the taskbar, basically, but they sit on top of the design.
// They don't move anything below them"): THE DIALOG — a chrome tenant of the
// bottom row (chrome_tenant_owns_bottom_row, app_state.h: a prompt, a dialog
// editor, the picker's Cancel; the render player is the program's and stays
// in the row) — and THE ON-SCREEN KEYBOARD each stand at the window's foot,
// the window's whole width over the ring's columns and bottom row in their
// rows, one Hilight line on top and the chrome's ground below, no border at
// the foot or the sides; the ring, the lanes and row 8 keep their geometry
// and paint as always, and the overlay covers them (the law, the three cases
// and every rect at onscreen_keyboard.h's overlay block). The painters:
// paint_program_frame (the ring, beside the menu row's painter) and
// paint_bottom_overlays (the overlay's line and the dialog's ground, after
// the keyboard slot).
inline int program_frame_line_px() {
    return relief_line_px();
}
// The ring's two lanes and the ring itself, client coordinates (main.cpp,
// beside the other lane accessors): the canonical ring, from the top lane's
// row to the bottom lane's, the client's whole width.
GuiRect top_program_frame_area(const AppState& a);
GuiRect bottom_program_frame_area(const AppState& a);
GuiRect program_frame_rect(const AppState& a);

// THE PROGRAM'S BAND AND ITS CASE — THE ICON ROW, the top strip's lane 3,
// directly under the program frame's top row (architect 2026-10-09: the
// program is Cool Edit from the toolbar down, the same under every chrome;
// the frame 2026-10-10, program_frame_rect above; program_spec.h's
// head, where every length below is authored and its source named). The
// roster's twenty-seven view/mode/action buttons stand in it — the
// kIconRowButtons, kIconRowViewGroup and history stand-in tables are the
// count's one authority (paint_handler.cpp); icons::kIconCount is a
// different number, the GLYPH set, which the row does not exhaust. THE CASE
// IS ALSO ROW 8's AND THE RENDER PLAYER'S: every reader takes the accessors
// below, so the three rows cannot drift apart.
//
// THE CASE (Cool Edit's §1.3 whole, its own 20-W seat since 2026-10-09
// ~10:25): one highlight line, the glyph, one shadow line, one black line —
// 23 W square, the buttons abutting at that pitch; A COMPOSITE OF ITS
// ROUNDED PARTS (scaled_px's rule), each line one relief line and the glyph
// scaled_px(20): 69 device px at 300 %, 84 at 360 % (the glyph 72, the
// lines 4 each), 31 at the laptop's 138 %. The glyph rasterizes at
// icon_glyph_px (icons::draw_cased) at the case's first line in from its
// corner, one line further right and down while pressed or checked
// (cool_edit_paint.h's faces).
//
// THE BAND (Cool Edit's §1.1): the dark outline, the panes' top light line,
// three W of face, the case, two W of face, the panes' bottom mid line, the
// dark outline, the light line — 33 W, the lane whole, UNDER EVERY CHROME.
// Its panes and grooves are paint_icon_row's walk (cool_edit_paint.h draws
// them). The hit target is the case; the lines, the faces, the grooves and
// the recess are inert ground for input.
//
// THESE NUMBERS LIVE HERE rather than beside the row's walk because more
// than one file reads the case: icons.cpp's seat (draw_cased) and
// paint_handler.cpp's three rows. THE CARD'S GLYPH BOX AND THE FOLDER
// OVERLAY'S LIST ICON ARE NOT THE CASE: each keeps Windows' small 16-px icon
// on a constant of its own (notifications.h's kNotificationGlyph*,
// folder_overlay.h's kRowIcon*).
inline int program_line_px() {
    return relief_line_px();
}
inline int icon_glyph_px() {
    return scaled_px(kProgramSpec.glyph_px);
}
// The glyph's seat from the case's corner: the case's highlight line.
inline int icon_case_lead_px() {
    return scaled_px(kProgramSpec.case_light_px, 1);
}
inline int icon_case_w_px() {
    const ProgramSpec& p = kProgramSpec;
    return scaled_px(p.case_light_px, 1) + scaled_px(p.glyph_px) +
           scaled_px(p.case_shadow_px, 1) + scaled_px(p.case_outer_px, 1);
}
inline int icon_case_h_px() {
    return icon_case_w_px();
}
// THE CASE'S TOP, AS AN OFFSET FROM THE BAND'S OWN TOP — the head's two
// lines and the face above the case: the one expression every reader of the
// case's seat in the band takes (paint_icon_row, hit rects, tooltips).
inline int icon_case_top_offset_px() {
    return kProgramSpec.band_head_lines * program_line_px() +
           scaled_px(kProgramSpec.band_air_above_px);
}
inline int icon_row_h_px() {
    return icon_case_top_offset_px() + icon_case_h_px() +
           scaled_px(kProgramSpec.band_air_below_px) +
           kProgramSpec.band_foot_lines * program_line_px();
}
// A PANE'S FACE between its light left column and its first case, and
// between its last case and its mid right column (Cool Edit's §1.2).
inline int program_pane_lead_px() {
    return scaled_px(kProgramSpec.pane_lead_px);
}
inline int program_pane_trail_px() {
    return scaled_px(kProgramSpec.pane_trail_px);
}

// A BUTTON IS SQUARE-CORNERED (architect 2026-10-02, the Windows-95 design):
// its box is the relief at relief_line_px, below, and no corner in the
// chrome is rounded.

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
//     tmp/reactos-media.png, mplay32's position slider, and confirmed on
//     tmp/reactos-audio.png, the Device volume slider disabled): a
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
//     scrub has no disabled state: with no item there is no thumb). DRAWN
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
inline constexpr int    kScrubChannelLines = 4;
inline constexpr double kScrubThumbWidthPx = 11.0;
inline constexpr double kScrubThumbAbovePx = 8.0;
inline constexpr double kScrubThumbBelowPx = 9.0;
inline constexpr double kScrubThumbPointPx = 5.0;
// The thumb's width floors at five device px — its two relief rings a side
// and one column of face — its two overhangs and its point at one row each.
inline int scrub_thumb_w_px()     { return scaled_px(kScrubThumbWidthPx, 5); }
inline int scrub_thumb_above_px() { return scaled_px(kScrubThumbAbovePx, 1); }
inline int scrub_thumb_below_px() { return scaled_px(kScrubThumbBelowPx, 1); }
inline int scrub_thumb_point_px() { return scaled_px(kScrubThumbPointPx, 1); }
// THE THUMB ON ITS BOX `box` (scrub_thumb_w_px by scrub_thumb_h_px), the
// rule above: the pentagon's soft raised edge and its ground face, nothing
// outside the pentagon painted.
void paint_scrub_thumb(cairo_t* cr, const GuiRect& box);
//
// THE HANDLE'S BOX IS THE GRAB, NOT THE PICTURE: the chrome spec's
// scrub_handle_box_px (chrome_spec.h, where its source stands: 14 Windows px
// under win2000, the painted thumb's 11 widened to it), and this accessor
// the ONE owner of
// that length for its readers — the MAPPING insets the track by half of it at
// each end (the thumb's centre is the frame's position;
// render_player_scrub_x_of, app_state.h) and the press router takes it as THE
// THUMB'S GRAB BAND. Floored at 2 so the half-box inset is never zero and the
// band never degenerates.
inline int scrub_handle_box_px() {
    return scaled_px(live_chrome_spec().scrub_handle_box_px, 2);
}

// THE CANVAS COLUMN'S LANES — THE COLUMN'S AIR, the TRIM lane (the view
// bar), the RULER lane, the MARKER lane and THE CANVAS'S TOP FRAME ROW,
// stacked in that order under the icon row (main.cpp's lane table), the
// frame row's bottom edge the waveform's top with no gap: THE PROGRAM'S,
// COOL EDIT'S, THE SAME UNDER
// EVERY CHROME (architect 2026-10-09; program_spec.h owns every length and
// its source; the colors at the palette block's canvas-column paragraph).
// Each lane is a composite of its rounded parts (scaled_px's rule): a line
// is one quantum (program_line_px), a run of face or ground its own
// scaled_px, the cue's triangle its quanta.
//   THE AIR    5 W of face (column_air_h_px): 15 device rows at 300 %, 7 at
//              138 %, 18 at 360 %.
//   THE TRIM   the view bar, a line, the 6-W field, a line (trim_lane_h_px):
//              24 / 10 / 30.
//   THE RULER  11 W of ground and its bottom line (ruler_lane_h_px): 36 / 16
//              / 44.
//   THE MARKER 12 W above the cue's triangle and its five quanta
//              (marker_lane_h_px): 51 / 22 / 63.
// (The ruler's 6 W went to the marker lane, architect 2026-10-09 ~21:00 —
// program_spec.h's ruler and marker-lane fields; the two lanes' sum keeps
// its 87 device rows at 300 % and its 107 at 360 %, and at 138 % rounds one
// row taller, 38 against 37, the laptop's well one row shorter.)
//   THE FRAME  the canvas's dark top frame row, one line
//              (canvas_top_frame_h_px, below): 3 / 1 / 4.
inline int column_air_h_px() {
    return scaled_px(kProgramSpec.column_air_px);
}
inline int view_bar_field_h_px() {
    return scaled_px(kProgramSpec.view_bar_field_px);
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
// THE CUE'S QUANTUM, u = scaled_px(1, 1) (program_spec.h's head: the
// triangles and the label's leads; the stem's dots on the canvas are one
// device px, waveform_line_px's rule, 2026-10-09).
inline int cue_unit_px() {
    return program_line_px();
}
inline int cue_triangle_h_px() {
    return kProgramSpec.cue_triangle_rows * cue_unit_px();
}
inline int cue_above_triangle_h_px() {
    return scaled_px(kProgramSpec.cue_above_triangle_px);
}
inline int marker_lane_h_px() {
    return cue_above_triangle_h_px() + cue_triangle_h_px();
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
// THE CUE LABEL'S LINE BOX AND ITS BASELINE under the marker lane's top —
// THE PROGRAM FACE'S LINE BOX CENTRED IN THE LABEL'S BOX, THE WHOLE LANE
// (architect 2026-10-09 evening, on the glass at 300 %: the open editor's
// selection band — the line box — stood three rows of field above it and
// six below, and he asked for it even; the cap band's centring of 2026-10-09
// ~21:45, "it should be centered vertically", retired with it). ONE RULE FOR
// THE LABEL AND THE BAND: the line box, the recorded ascent plus descent
// (10 + 2 W, gui_font_line_px) rounded once to whole rows, stands in the
// lane by the box rule — the floor of the leftover above, the odd row below
// — and THE BASELINE IS THE BOX'S TOP PLUS THE ASCENT, rounded once. The
// resting label and the editor's text must share one baseline (the editor's
// text sits at the label's seat), so the line box's centring wins and the
// cap's own is not kept. Every label segment — the resting label, the bound
// cells, the history's two halves — and the flag editor's text ride the
// baseline, and the editor's caret and selection band ARE the box
// (render_flag_editor_box reads cue_line_box_top_px and cue_line_box_h_px,
// never a second derivation). DERIVED from the lane's composite height and
// the face's recorded metrics at the live scale, never an authored row
// scaled on its own. THE ROWS (box, baseline): at 100 % rows 2 .. 13 of the
// 17, the baseline the top of row 12 (the cap on rows 5 .. 11, as the cap
// rule put it); at 138 % (lane 22, box 17) rows 2 .. 18, the baseline 16;
// at 300 % (lane 51, box 36) rows 7 .. 42, the baseline 37 (the cap rule's
// 36 one row higher); at 360 % (lane 63, box 43) rows 10 .. 52, the
// baseline 46.
inline int cue_line_box_h_px() {
    return static_cast<int>(
        std::nearbyint(gui_font_line_px(gui_font(GuiFace::Program))));
}
inline int cue_line_box_top_px() {
    return (marker_lane_h_px() - cue_line_box_h_px()) / 2;
}
inline int cue_baseline_px() {
    return cue_line_box_top_px() +
           static_cast<int>(std::nearbyint(
               gui_font_ascent_px(gui_font(GuiFace::Program))));
}
// THE RULER'S DIGITS ARE THE SMALL FACE'S SIX ROWS UNDER ONE W OF AIR
// (program_spec.h's ruler fields), in every face set (gui_font.h: every
// set's small face is the base's six-row digit, its cell all above the
// baseline).
constexpr bool ruler_digit_rows_hold(const GuiFaceSet& set) {
    const GuiFaceMetrics& m =
        set.metrics[static_cast<std::size_t>(GuiFace::Small)];
    return m.measure == GuiFaceMeasure::Digit && m.descent == 0 &&
           m.ascent == m.height &&
           kProgramSpec.ruler_baseline_px == 1 + m.height;
}
static_assert([] {
    for (const GuiFaceSet* s : kGuiFaceSets)
        if (!ruler_digit_rows_hold(*s)) return false;
    return true;
}());
// THE CUE LABEL'S ROWS AT 100 % (cue_baseline_px's rule over the lane's
// authored 17 and the program face's recorded line box, ascent 10 and
// descent 2, and its cap 7 — the program face is measured by its cap in
// every set, gui_font.h's kGuiProgramFaceMetrics): the box on rows 2 .. 13,
// the baseline the top of row 12, the cap on rows 5 .. 11. THE DESCENT ENDS
// ABOVE THE LANE'S LAST ROW — the box's outline row: the baseline plus the
// recorded descent plus two rows to spare for the glyphs that overshoot the
// record stand within rows 12 .. 15. Measured 2026-10-09 off the files'
// outlines through FreeType, the deepest glyphs a label can carry reach
// 0.288 of the cap under the baseline in Tahoma ("y"; "g" 0.285, "(" 0.257,
// "[" 0.181), 0.299 in FreeSans ("g", "y"; its brackets 0.291) — 2.0 / 2.1 W
// at 100 %, rows 12 .. 14; at 300 % (cap 21) 6.0 / 6.3 device rows under
// the baseline 37, ending on row 43 of the 51, four rows above the outline's
// three (48 .. 50); at 360 % (cap 25.2) 7.3 / 7.5 under 46, ending on row 53
// of the 63, the outline 59 .. 62.
constexpr int kCueLabelBaselineAt100 =
    (program_marker_lane_authored_h(kProgramSpec) -
     (kGuiProgramFaceMetrics.ascent + kGuiProgramFaceMetrics.descent)) / 2 +
    kGuiProgramFaceMetrics.ascent;
static_assert(kGuiProgramFaceMetrics.measure == GuiFaceMeasure::Cap);
static_assert(kCueLabelBaselineAt100 == 12);
static_assert(kCueLabelBaselineAt100 - kGuiProgramFaceMetrics.height == 5);
static_assert(kCueLabelBaselineAt100 + kGuiProgramFaceMetrics.descent + 2 <=
              program_marker_lane_authored_h(kProgramSpec) - 1);
// THE TRIANGLE'S PAINTED COLUMNS round its stem column `col` — the cue's and
// the playhead head's one extent, read off the painter's own vertices
// (paint_ce_cue_triangle, architect 2026-10-09 ~14:35, centred on the
// one-px stem: the top edge from col + 0.5 − 4.5u to col + 0.5 + 4.5u, the
// shadow triangle one quantum right of it, so the outer edges col + 0.5 −
// 4.5u and col + 0.5 + 5.5u) taken to whole device columns: [col −
// cue_triangle_half_w_px(), col + cue_triangle_reach_right_px()), ⌊9u / 2⌋
// left and ⌊(11u + 2) / 2⌋ right — 13 and 17 device px at 300 % (u 3), 18
// and 23 at 360 % (u 4), 4 and 6 at 138 % (u 1). THE ONE ROAD for every
// reader of that width: the cue's published triangle box (paint_cues), the
// label clip at the next triangle (paint_cues), the flag iterator's cull,
// the head's cull and its published hit (paint_ruler_row,
// AppState::playhead_head_hit) and the playhead's cull and damage box
// (render_playhead, playhead_invalidate_rect, main.cpp), so no repaint can
// be narrower than what the head painted.
inline int cue_triangle_half_w_px() {
    const int rows = kProgramSpec.cue_triangle_rows;
    return (2 * rows - 1) * cue_unit_px() / 2;
}
inline int cue_triangle_reach_right_px() {
    const int rows = kProgramSpec.cue_triangle_rows;
    return ((2 * rows + 1) * cue_unit_px() + 2) / 2;
}
static_assert(kProgramSpec.cue_triangle_rows == 5);   // 9u and 11u above
// THE COLUMN'S AIR LANE (top lane 4, main.cpp; defined there beside the
// other lane accessors).
GuiRect top_column_air_area(const AppState& a);
// THE CANVAS'S FRAME AND THE COLUMN'S MARGINS (architect 2026-10-09, the
// third part; program_spec.h's column_margin_px and column_foot_px, METRICS
// §4.1): the canvas's TOP FRAME ROW is a lane of its own, top lane 8, one
// line (canvas_top_frame_h_px), directly under the marker lane; the COLUMN'S
// FOOT is a bottom lane of its own, the bottom frame row and 5 W of face
// (column_foot_h_px), between the canvas and the dock bar. Across, the column
// stands column_margin_w_px of face in from each of the program frame's
// columns (program_frame_rect, 2026-10-10), then its one-line frame column,
// then its interior — THE WAVEFORM AREA — its width floored to the grid step
// and the floor's leftover (under 16 device px) split between the two
// margins, the odd pixel to the right (main.cpp's waveform_area owns the
// split; column_inner_x_px is the side before it, inside the program
// frame's line). 15 + 3 = 18 device rows of foot at 300 %, 7 + 1 = 8 at
// 138 %, 18 + 4 = 22 at 360 %; the side inside the frame's line 18 + 3 = 21
// device px at 300 %, 8 + 1 = 9 at 138 %, 22 + 4 = 26 at 360 %.
inline int canvas_top_frame_h_px() {
    return program_line_px();
}
inline int column_foot_face_h_px() {
    return scaled_px(kProgramSpec.column_foot_px);
}
inline int column_foot_h_px() {
    return program_line_px() + column_foot_face_h_px();
}
inline int column_margin_w_px() {
    return scaled_px(kProgramSpec.column_margin_px);
}
inline int column_inner_x_px() {
    return column_margin_w_px() + program_line_px();
}
// The two lanes (main.cpp, beside the other lane accessors).
GuiRect top_canvas_frame_area(const AppState& a);
GuiRect bottom_column_foot_area(const AppState& a);

// (THE TRIM LANE AS WINDOWS' MINIATURIZED SCROLL BAR — its 16-W track,
// caps and thumb — THE RULER'S HEIGHT DERIVED FROM ITS SMALL FACE with the
// playhead's WordPad head on its bottom rows, and THE MARKER LANE DERIVED
// FROM THE 17-W FLAG BOX stood 2026-10-02 to 2026-10-09; git history.)

// Authored pixel geometry of THE BOTTOM ROW — THE UNIFIED BOTTOM ROW, the
// lane rows 8 and 9 merged into (architect-ruled 2026-08-12; the bottom
// strip's ONLY lane since the relayout's commit B): the clock in its time
// field and the state line on the ground beside it at the left pad
// (architect 2026-10-02 / 2026-10-03 / 2026-10-05),
// and the MARKER-VERB GROUP, the marker walk, the four cardinal arrows and
// the transport three flush right, eight Windows px of bare ground between
// groups (architect 2026-09-29; kMarkerVerbGroup in
// paint_handler.cpp owns that group's membership, which does not bear
// restating here), all one line ON THE WINDOW'S FOOT — which it holds again
// since
// 2026-08-29's evening fold, a STATUS BAR having stood under it for that one
// day — with the
// flexible blank
// gap 2 between this row and the waveform above
// it. (The STATUS CHAIN — the critical chip + section C — right-aligned on
// this lane from the unification until 2026-08-13, when the architect moved it
// into the tab row, where it was deleted whole on 2026-08-29 for the bar; the
// height below is unaffected through all three moves, its derivation being the
// BUTTON's.) The SUCCESSION: the status line landed as row 7 (2026-08-01, the
// two-lane bottom strip collapsing to one — its crops keep that name), was
// renumbered row 9 when the transport row (row 8, 2026-08-11, the touch arc's
// first surface) stacked above it, and both lanes merged into this one a day
// later — the buttons grown to the icon row's boxes for glass, the status
// strings moved beside them, every interactive surface one contiguous cluster
// against the waveform.
//
// THE ROW IS COOL EDIT'S ROW 8 UNDER ITS DOCK BAR (architect 2026-10-09,
// the program is Cool Edit; program_spec.h): THE DOCK BAR on the well's side
// — a light row, three W of face, a mid row, a dark row (Cool Edit's
// y = 915..920), 6 W — then THE ROW's content, four W of face, the program's
// case (the band's own, read from its accessors above so the rows cannot
// drift apart) and five W of face: 32 W, the lane 38 under every chrome.
// Its groups — the grippers, the end bars, the dark time field — are
// paint_bottom_row_buttons_and_clock's walk (cool_edit_paint.h draws them):
// each group the gripper, 5 W of face, its cases or the time field, 5 W of
// face and the end bar — the product's symmetric air where Cool Edit's
// movable panes measure 6 and 4 (architect 2026-10-09 evening,
// program_spec.h's group fields) — and the time field's pads 5 W each side,
// the dialog field's own (program_spec.h's field_pad_px, 2026-10-10).
// The render player, the program's tenant, stands in the content band below
// the dock bar's rows, on the row's face (paint_bottom_strip). A CHROME
// TENANT (chrome_tenant_owns_bottom_row, app_state.h: a prompt, a dialog
// editor, the picker's Cancel) is THE DIALOG, a bottom overlay laid over the
// content band and the program frame's bottom row from the dock bar's foot to
// the window's foot, the dock bar left standing above it (2026-10-10,
// program_frame_rect's overlay paragraph and onscreen_keyboard.h's overlay
// block); the lane under it is the program's as always.
// bottom_row_content_h_px() is the band the buttons and text sit on;
// bottom_row_h_px() is the lane the strip stack allocates. Each term its own
// rounded part.
inline int dock_bar_h_px() {
    return 3 * program_line_px() + scaled_px(kProgramSpec.dock_bar_face_px);
}
inline int row8_air_above_px() {
    return scaled_px(kProgramSpec.row8_air_above_px);
}
inline int bottom_row_content_h_px() {
    return row8_air_above_px() + icon_case_h_px() +
           scaled_px(kProgramSpec.row8_air_below_px);
}
inline int bottom_row_h_px() {
    return dock_bar_h_px() + bottom_row_content_h_px();
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

// THE WHOLE STACK IN WINDOWS PX — every lane a vocabulary lays above and
// below the well, and the frame's two sides where the vocabulary's frame
// stands on the maximised window (2026-10-08, window_frame_maximized; none
// under win2000): the chrome's caption and the menu row's
// two terms, then THE PROGRAM'S (program_spec.h, the same under every
// chrome since 2026-10-09): its frame's top row 1 (program_frame_rect,
// 2026-10-10), the band 33, the canvas column's 43 above the canvas (the
// air 5, the view bar 8, the ruler 12, the marker lane 17, the canvas's top
// frame row 1), the column's foot 6 under it (the bottom frame row and 5 W
// of face), the dock bar and row 8 38 and the frame's bottom row 1. At 300 %
// every term is a whole multiple of 3 device px.
constexpr int chrome_stack_authored_h(const ChromeSpec& s) {
    const int frame = s.window_frame_maximized
        ? 2 * (s.window_frame_lines * kReliefLinePx + kWindowFramePx -
               2 * kReliefLinePx)
        : 0;
    const int row8 = program_dock_bar_authored_h(kProgramSpec) +
                     program_row8_authored_h(kProgramSpec);
    const int program_frame = 2 * kReliefLinePx;
    return frame + s.caption_height_px + s.menu_row_head_px +
           s.menu_row_content_px + program_frame +
           program_band_authored_h(kProgramSpec) +
           program_column_authored_h(kProgramSpec) +
           program_column_foot_authored_h(kProgramSpec) + row8;
}
// THE STACK (2026-10-09, the canvas column Cool Edit's, its canvas framed;
// the program's frame 2026-10-10): WIN2000 115 above and 45 below, 160 —
// the tablet's well at 300 % 960 rows (320 W) of the surface's 1440 (a
// vocabulary's well its own lanes' leftover, main.cpp's rule).
static_assert(chrome_stack_authored_h(kChromeSpecWin2000) == 160);
// AT THE WAVEFORM'S EDGES A CUE IS CLIPPED (architect 2026-09-26, the flag
// box's rule kept for the cue): the marker lane paints inside the waveform's
// columns [0, w) — every triangle, label, editor field and hit rect
// (clip_to_waveform_columns, render.cpp), the columns standing at the
// canvas's own x since 2026-10-09 (waveform_area, the column's margin) — and
// the flag iterator admits a marker whose triangle reaches into them (its
// left edge four quanta left of its column), so a marker just past the last
// column shows the left half of its triangle there, and no dots (the stem
// stash is gated to [0, w)).
// THE PLAY-SCRUB'S CHANNEL AND THUMB HEIGHTS, here because they count relief
// lines (the slider's block, with its constants, is above): the channel its
// four lines, the thumb its rows above the channel, the channel and its rows
// below — each part rounded on its own.
inline int scrub_channel_h_px() {
    return kScrubChannelLines * relief_line_px();
}
inline int scrub_thumb_h_px() {
    return scrub_thumb_above_px() + scrub_channel_h_px() +
           scrub_thumb_below_px();
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
// an erosion at distance 1 (outline_bar, render.cpp, the width riding the
// plate job and its fingerprint), and the canvas's GRID AND CENTER LINES
// (render_canvas, render_center_lines). EVERYTHING OFF THE CANVAS keeps THE PROGRAM'S QUANTUM u =
// scaled_px(1, 1) (program_line_px, cue_unit_px): the cues' triangles and the
// playhead's head (paint_ce_cue_triangle) and the ruler's ticks
// (paint_ruler_row, fill_waveform_line's `t`).
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
inline void fill_waveform_line(cairo_t* cr, int area_x, int area_w, int col,
                               double y0, double y1,
                               int t = waveform_line_px()) {
    if (col < 0 || col >= area_w) return;
    const int end = (col + t < area_w) ? col + t : area_w;
    cairo_rectangle(cr, static_cast<double>(area_x + col), y0,
                    static_cast<double>(end - col), y1 - y0);
    cairo_fill(cr);
}
// THE DOTTED LINE (architect 2026-10-09, Cool Edit's cue and cursor columns,
// METRICS §4.2 / §4.3), A PERIOD TECHNIQUE AT THE DEVICE'S RESOLUTION
// (~14:20 / ~14:25, waveform_line_px's rule: "on waveform → unscaled"): the
// line's own column and edge rules (fill_waveform_line), its rows [y0, y1)
// counted in DEVICE ROWS from y0, a dot one device px square on every row k
// with k ≡ `phase` (mod `period`) — Cool Edit's own 1-px dot every 4 rows at
// 1:1, at every gui_scale — the cues' red on 7 and blue on 3 of 8 and the
// playhead on 1 of 4 (program_spec.h's dot fields), so they interleave on one
// column. The column is the item's own painted column, the dot one px on it
// — the stem the cue's triangle and the playhead's head stand centred on
// (paint_ce_cue_triangle, ~14:35). In the caller's source color.
inline void fill_dotted_waveform_line(cairo_t* cr, int area_x, int area_w,
                                      int col, int y0, int y1, int period,
                                      int phase) {
    if (col < 0 || col >= area_w || period <= 0) return;
    const int t   = waveform_line_px();
    const int end = (col + t < area_w) ? col + t : area_w;
    for (int k = phase; y0 + k * t < y1; k += period) {
        const int top = y0 + k * t;
        const int bot = top + t < y1 ? top + t : y1;
        cairo_rectangle(cr, static_cast<double>(area_x + col),
                        static_cast<double>(top),
                        static_cast<double>(end - col),
                        static_cast<double>(bot - top));
    }
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
    const int u = cue_unit_px();
    const double glyph_bound =
        gui_font_advance_bound_px(gui_font(GuiFace::Program));
    // The payload's lead and its fill's pad past the text.
    const double label =
        static_cast<double>(glyphs) * glyph_bound +
        static_cast<double>((kProgramSpec.cue_label_lead +
                             kProgramSpec.cue_fill_pad) * u);
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
// GRABS are the span's two END ZONES, each this many W wide — the bar's own
// height, 8 W, square on the lane as Windows' caps were — standing on the
// bound's column inside the span (the begin's left edge on the begin column,
// the end's right edge on the end column), and the BRIDGE the span's body
// between them (render_trim_flags publishes all three; trim_endcap_rect and
// trim_bridge_gap own the rule). NO CAP IS DRAWN: the span's own bevel is its
// only edge (METRICS §3, "the span's own bevel is the only edge"), so the
// zones are the span's painted columns read as its two ends. NARROW: where
// the span is under two zones wide the begin zone keeps its column and the
// end zone stands edge to edge right of it, overrunning the end column by
// the difference — the one place a grab stands past the painted span, kept
// so a narrow window's two ends are always both grabbable (trim_endcap_rect's
// rule, 2026-10-03).
inline int view_bar_grab_w_px() {
    return trim_lane_h_px();
}
// THE ARROW GLYPH, Windows' scroll arrow — THE POPUP LISTS' SCROLL BAR'S
// since 2026-10-09 (the trim caps that first wore it are gone): its pixel form is four columns
// 1, 3, 5 and 7 Windows rows tall from the tip, each centred on the middle
// row — a 4 x 7 triangle. THE CHROME IS SCALABLE (architect 2026-10-06, his
// glass: "still the pixelated icon versions"), so it is drawn as ONE FILLED
// TRIANGLE over that outline, smooth at any scale as Marlett's scroll arrows
// are at high DPI: the BASE the far column's outer edge, the full 7 rows
// tall, the TIP the tip column's outer edge at the middle row's centre —
// kTrimArrowGlyphWPx x kTrimArrowGlyphHPx Windows px, the pixel glyph's own
// width and height. Its coordinates are in the unit u = scaled_px(1, 1),
// the glyph 4u x 7u device px; it is CENTRED IN THE BUTTON in device px, an
// odd difference flooring toward the top-left (the extra pixel right of or
// below the glyph). Antialiased (the lane paints aliased; the triangle alone
// takes cairo's default). At 400 % (u 4): 16 x 28 in the 64 x 64 case, at
// (+24, +18); at 138 % (u 1): 4 x 7 in 22 x 22 at (+9, +7). The arrows have
// no disabled face.
inline constexpr int kTrimArrowGlyphWPx = 4;
inline constexpr int kTrimArrowGlyphHPx = 7;

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
// HYSTERESIS) — A RECORDED DEPARTURE from Windows
// (docs/engineering/win2000_deviations.md): the wait re-anchors, restarting
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
// them: the tooltips, the menu row's hover switch (a rect test of its own)
// and the armed presses' inside bits. The record is in git history.)

// THE DROPDOWNS' VERTICAL metrics — one set for every menu, out here for the
// same reason the tooltip's height is: the popup's OPEN EDGE must damage the box
// before the box has ever been painted, and its HEIGHT is fully derivable
// without shaping a single label (item count x item height, plus the separator
// blocks and the two borders). Its WIDTH is not — that needs the widest shaped
// label — so the horizontal terms stay with the painter and the open edge
// damages full-width instead. dropdown_h_px (app_state.h) does the sum, where
// the item tables are visible.
//
// THE DROPDOWN IS WINDOWS 95's POPUP MENU, AT THE WINDOWS PIXEL (architect
// 2026-10-02): a PLAIN RAISED two-line frame, then ONE Windows px of ground
// margin on every side, then the items, 17 Windows px each and touching; the
// highlight fills an item inside the margin, its full width. A SEPARATOR is
// an etched pair (two lines) with 3 Windows px of ground above and below it —
// 8 in all, Windows' separator item (the planner's reading of the Windows
// shots; the laptop pixel's 2 would have been 1). The item's height and the
// margin are the spec's; the frame's and the separator's line counts are the
// vocabulary's drawing.
inline constexpr int kPopupSepMarginYPx = 3;   // above and below the separator
// THE FRAME'S LINES a side — Windows' plain raised edge, two relief lines —
// read, not a second number.
inline int popup_border_px() {
    return 2 * relief_line_px();
}
// The rows the frame spends at the box's TOP: the whole frame, whether the
// box hangs from its opener or stands on its head (`upward`: the settings
// choice editor's list, the preset menu's upper placement) — the parameter
// kept for a later vocabulary whose hanging menu draws its top line on its
// opener's last row (GtkMenu's did, 2026-10-07 to 2026-10-09).
inline int popup_border_top_px(bool /*upward*/) {
    return popup_border_px();
}
inline int popup_item_h_px() {
    return scaled_px(live_chrome_spec().popup_item_height_px, 5);
}
inline int popup_sep_margin_y_px() {
    return scaled_px(kPopupSepMarginYPx, 0);
}
// A separator's whole block: its margin, the etched pair, its margin.
inline int popup_sep_block_px() {
    return 2 * popup_sep_margin_y_px() + 2 * relief_line_px();
}
// The item block's own margin inside the frame, top AND bottom (the
// horizontal one, the same spec field, is the painter's item inset): the
// spec's popup_margin_px, floored at one device px where it is not zero.
inline int popup_item_margin_y_px() {
    const int m = live_chrome_spec().popup_margin_px;
    return scaled_px(m, m > 0 ? 1 : 0);
}

// THE DROPDOWN'S HORIZONTAL PADS, authored in POPUP-BOX coordinates — from
// the box's own outer edges, which is how a menu with an accelerator column is
// easiest to state. EVERY MENU ROW PULL-DOWN TAKES THEM (architect
// 2026-08-03): the pull-downs differ in one derived term (whether an
// accelerator column exists) rather than in their padding.
//
//  - THE LABEL PAD AND THE RIGHT PAD ARE ONE NUMBER, 22 WINDOWS PX (architect
//    2026-10-02, the mirror rule): from the popup's left edge to the label's
//    pen, and from the popup's right edge to the accelerator's last ink
//    column (or, on a menu without one, the widest label's) — Windows' popup
//    reserves its check-mark column on the left and its submenu-arrow column
//    on the right, the same width, and this product has neither and keeps
//    their space as plain padding. It is the right margin the kdenlive crop
//    measured (30 laptop px, re-authored at the unit's change); the crop's
//    57-px left indent, kdenlive's checkbox-and-icon gutter, retired for the
//    mirror. THEY ARE THE MENU ROW'S PULL-DOWNS' ALONE: every DROP-DOWN —
//    the picker's scope and element lists, ITS PRESET MENU, the settings
//    editor's choice list — takes THE DROP-DOWN INSET on both sides, one
//    number under every chrome (color_picker::combo_text_inset_px, the
//    rule's one owner, architect 2026-10-09).
//  - THE COLUMN GAP is the guaranteed minimum separation between the widest
//    label and the widest accelerator, the kdenlive crop's 13 laptop px
//    re-authored as 9 Windows px (architect 2026-10-02: the gap kept).
//  - A LIST STANDING A SCROLL BAR (the scroll block below, 2026-10-08)
//    keeps its left pad from its left edge and measures the right pad to
//    the BAR'S LEFT EDGE: the picker's preset menu, whose width is its
//    labels' between the drop-down inset's two pads, widens by the bar so
//    the labels keep both.
inline constexpr double kPopupPadXPx      = 22.0;
inline constexpr double kPopupHotkeyGapPx = 9.0;

// THE POPUP LISTS SCROLL (architect 2026-10-08 ~11:20, "expand the
// drop-downs so we can scroll up and down" — reversing the 2026-10-07 bound,
// under which the window bounded the preset menu, the names' tail was cut
// and Save As refused when no name fit; closed_questions.md records the
// reversal). THIS BLOCK IS THE RULE'S ONE OWNER for the three LIST popups —
// the settings choice editor's list (settings_editor.h's head), the color
// picker's element list and its preset menu (color_picker.h's head) — whose
// heads point here. The menu row's dropdowns never outgrow the window and
// take none of it.
//  * THE PLACEMENT (place_popup_list): hung from the opener's foot where every
//    row fits whole below it to the window's foot, else standing on the
//    opener's head where every row fits whole above it to the window's head,
//    else on the ROOMIER side (the one showing more whole rows; below on a
//    tie) SHOWING THE MOST WHOLE ROWS THAT FIT and scrolling the rest — no
//    box ever passes the window.
//  * THE ROWS SCROLL BY ROW, EVERY ROW ALIKE (Windows' list box scrolls
//    everything): the preset menu's four acts and its separators are ordinary
//    rows that scroll with the names, none pinned. THE BOX'S ROOM is the most
//    whole ITEM rows that fit on its side (place_popup_list), and THE SHOWN
//    ROWS ARE EVERY ROW FROM THE TOP WHOSE SUMMED HEIGHTS FIT IN THAT ROOM,
//    each row at its own height — a separator counted at its separator
//    block's (architect 2026-10-08 ~12:50, his palette-menu captures
//    _061454 and _062506: "it omits some lines when you first open it; as
//    soon as you start scrolling they show up properly and take up the
//    entire space" — the shown count had been the room's item count, so the
//    separators in view left ground at the foot). So the block is always
//    full to within less than one item: the only ground at its foot is the
//    remainder no further row fits. THE ONE FUNCTION is popup_scroll_shown;
//    every reader of the shown count follows it — the item rects, the page
//    step (Windows' list box with mixed heights pages by the rows shown,
//    SB_PAGE), the reveal, the thumb's proportion (shown / total at the
//    current top) — and THE LAST TOP is the least top from which every
//    remaining row fits (popup_scroll_max_top, from the heights, not total
//    less shown). THE STATE is PopupScroll, one per popup: the top row and
//    the bar's held part. IT IS RESET TO THE HEAD AT EACH OPEN (a reopened
//    list starts at its head, the preset menu's acts first), with ONE
//    EXCEPTION, Windows' combo's own: a COMBO's list — the choice editor's
//    and the element list, each opening with its shown value lit — scrolls
//    that row into view at the open (popup_scroll_reveal).
//  * THE PICTURE IS A VERTICAL SCROLL BAR INSIDE THE LIST, AT ITS RIGHT, UNDER
//    EVERY CHROME (architect 2026-10-08, his ReactOS WordPad font-combo
//    capture: "exactly as we would expect, just a scroll bar on the side"; and
//    of a combo's scroll ARROWS top and bottom: "I'd much rather have the
//    scroll bar inside, and we can use the canonical scroll bar"). 16 W WIDE
//    (Windows' SM_CXVSCROLL — the spec's scroll_bar_px, below),
//    INSIDE THE FRAME AND FLUSH ON ITS RIGHT LINE: Windows' list box has no
//    margin, so under win2000 THE BAR TOUCHES THE FRAME, taking the item
//    block's one-W ground margin's place on that side and running over the
//    margin's top and bottom rows — the item block's full height, from the
//    frame's lines to the frame's lines. The items end at
//    the bar's left edge less the margin, inside their margin as ever
//    (popup_item_rect). TWO ARROW BUTTONS square on the bar's width at its ends, up on
//    top and down at the bottom (each half the bar where the bar is shorter
//    than two, Windows' own halving); the TRACK between them; the THUMB's
//    length the track's times shown / total, FLOORED AT 8 W
//    (kPopupScrollThumbMinPx: half SM_CYVTHUMB's 16, THE PRODUCT'S CHOICE —
//    no source for Windows 2000's own minimum thumb is at hand), its seat
//    (track − thumb) x top / the last top — both by nearbyint
//    (popup_scroll_bar); a track too short for the floor shows no thumb, as
//    Windows shows none. The chrome draws it in its own scroll bar's
//    vocabulary (paint_popup_scroll_bar, beside the trim bar's painters).
//  * THE WIDTH: a box whose width is its labels' (the preset menu, its pads
//    the drop-down inset, color_picker::combo_text_inset_px) widens by the
//    bar (the pads block's last term, beside kPopupPadXPx); a combo's list
//    stays the combo's own width (the flush ruling at color_picker::combo_list)
//    with the bar inside it — its labels are a closed domain the combo
//    already fits.
//  * THE INPUT — the pointer, the pen and one finger alike, every zone the
//    PUBLISHED bar's (ON SCREEN IS AS PAINTED; popup_scroll_hit): a press on
//    an ARROW scrolls ONE ROW and wears the pressed face until the lift — ONE
//    STEP A PRESS, NO AUTOREPEAT, the product's simplification of Windows'
//    held repeat; a press on the TRACK above or below the thumb scrolls ONE
//    PAGE (the shown count) toward the press; a press on the THUMB starts
//    A THUMB DRAG, the thumb following the pointer with the press's offset on
//    it kept and the rows following the thumb live (Windows' tracking;
//    popup_scroll_top_at_thumb) — a live gesture
//    (any_pointer_gesture_active, its motion claimed wherever it goes), whose
//    lift ends it and selects nothing. THE LAPTOP'S WHEEL over a scrolling
//    list scrolls kPopupWheelRows a notch (Windows' default wheel lines),
//    the modified wheel a swallowed nothing. The choice editor's Up / Down
//    keep the lit row in view, a step past the shown block scrolling by one
//    row (popup_scroll_reveal). A press anywhere else is the popup's own, as
//    before: a row arms, outside closes.
//  * THE DAMAGE: a scroll, an arrow's pressed face and the thumb drag repaint
//    the whole box.
inline constexpr int kPopupScrollThumbMinPx  = 8;
inline constexpr int kPopupWheelRows         = 3;
// THE WIDTH IS THE CHROME SPEC'S scroll_bar_px (2026-10-08): the 16 above
// under win2000.
inline int popup_scroll_bar_w_px() {
    return scaled_px(live_chrome_spec().scroll_bar_px, 3);
}
// The thumb floor, device px (the picture bullet).
inline int popup_scroll_thumb_min_px() {
    return scaled_px(kPopupScrollThumbMinPx, 1);
}
// The bar's parts — a held part (an arrow's pressed face, the thumb's drag)
// or a press's zone (the two arrows, the track's two pages, the thumb).
enum class PopupScrollPart { None, Up, Down, PageUp, PageDown, Thumb };
// ONE POPUP'S SCROLL STATE: `top` the first shown row (clamped against the
// published bar at every read and every act), `held` the arrow whose press
// stands (Up / Down) or the thumb whose drag does (Thumb), and `grab_dy` the
// drag's press offset from the thumb's top.
struct PopupScroll {
    int             top     = 0;
    PopupScrollPart held    = PopupScrollPart::None;
    int             grab_dy = 0;
    bool live() const { return held != PopupScrollPart::None; }
};
// THE PLACEMENT (the block's first bullet) of rows whose heights sum to
// `content_h`, against `opener` in a window `window_h` tall: the side, the
// ROOM the rows stand in (device px: `content_h` when nothing scrolls, else
// the most whole item rows that fit on that side, at least one), the box's
// rows y and h (its frame's lines, the item block's two margins and the
// room). Which rows show in the room is the scroll's (popup_scroll_shown);
// the columns are the caller's.
struct PopupListPlacement {
    bool upward  = false;
    bool scrolls = false;
    int  room_h  = 0;
    int  y       = 0;
    int  h       = 0;
};
inline PopupListPlacement place_popup_list(const GuiRect& opener, int window_h,
                                           int content_h) {
    const int item_h  = popup_item_h_px();
    const int margins = 2 * popup_item_margin_y_px();
    const auto frame = [&](bool upward) {
        return popup_border_top_px(upward) + popup_border_px() + margins;
    };
    const int room_below = window_h - (opener.y + opener.h);
    const int room_above = opener.y;
    PopupListPlacement p;
    p.room_h = content_h;
    if (frame(false) + content_h <= room_below) {
        p.upward = false;
    } else if (frame(true) + content_h <= room_above) {
        p.upward = true;
    } else {
        const int below = (room_below - frame(false)) / item_h;
        const int above = (room_above - frame(true)) / item_h;
        p.upward  = above > below;
        p.room_h  = std::min(content_h, std::max(1, std::max(below, above)) * item_h);
        p.scrolls = p.room_h < content_h;
    }
    p.h = frame(p.upward) + p.room_h;
    p.y = p.upward ? opener.y - p.h : opener.y + opener.h;
    return p;
}
// AN ITEM'S RECT in the list box `box` at row `y`: inside the frame and the
// margin on the left, and on the right the same — or, with the bar (`bar`),
// the bar's left edge less the margin (the block's picture bullet).
inline GuiRect popup_item_rect(const GuiRect& box, int y, bool bar) {
    const int side_b   = popup_border_px();
    const int margin_x = live_chrome_spec().popup_margin_px;
    const int inset    = scaled_px(margin_x, margin_x > 0 ? 1 : 0);
    const int left     = box.x + side_b + inset;
    const int right    = bar ? box.x + box.w - popup_border_px() -
                                   popup_scroll_bar_w_px() - inset
                             : box.x + box.w - side_b - inset;
    return GuiRect{left, y, right - left, popup_item_h_px()};
}
// THE BAR OF A PLACED LIST (the block's picture bullet), device px: absent
// (`present` false, every rect zero) when every row shows; the rows' own
// heights (`row_h`, in their order) and the `room` they stand in, so every
// count below is popup_scroll_shown's; `top` the clamped first row,
// `max_top` the last top, `visible` the rows shown from `top`; the bar, its
// two arrows, the track between and the thumb (zero where the track is too
// short for its floor). Published with the box, so every press reads it as
// painted.
struct PopupScrollBar {
    bool             present = false;
    int              total   = 0;
    int              visible = 0;
    int              top     = 0;
    int              max_top = 0;
    int              room    = 0;
    std::vector<int> row_h;
    GuiRect bar{0, 0, 0, 0};
    GuiRect up{0, 0, 0, 0};
    GuiRect down{0, 0, 0, 0};
    GuiRect track{0, 0, 0, 0};
    GuiRect thumb{0, 0, 0, 0};
};
// THE ONE COUNT (the rows bullet): the rows from `top` whose summed heights
// fit in `room`, at least one while any row remains.
inline int popup_rows_fitting(const std::vector<int>& row_h, int room, int top) {
    const int total = static_cast<int>(row_h.size());
    int n = 0, used = 0;
    for (int i = std::max(0, top); i < total; ++i) {
        used += row_h[static_cast<std::size_t>(i)];
        if (used > room && n > 0) break;
        ++n;
        if (used >= room) break;
    }
    return n;
}
// THE LAST TOP: the least top from which every remaining row fits.
inline int popup_rows_max_top(const std::vector<int>& row_h, int room) {
    int top = static_cast<int>(row_h.size());
    int used = 0;
    while (top > 0 && used + row_h[static_cast<std::size_t>(top - 1)] <= room) {
        used += row_h[static_cast<std::size_t>(top - 1)];
        --top;
    }
    return top;
}
inline int popup_scroll_clamp(const PopupScrollBar& b, int top) {
    return std::clamp(top, 0, b.max_top);
}
// The rows `b` shows from `top` (clamped).
inline int popup_scroll_shown(const PopupScrollBar& b, int top) {
    return popup_rows_fitting(b.row_h, b.room, popup_scroll_clamp(b, top));
}
// The top that shows row `row`: unchanged when it already shows, else the
// least travel that does (one row per step past the block's edge).
inline int popup_scroll_reveal(const PopupScrollBar& b, int top, int row) {
    top = popup_scroll_clamp(b, top);
    if (row < top) return popup_scroll_clamp(b, row);
    while (top < b.max_top && row >= top + popup_scroll_shown(b, top)) ++top;
    return top;
}
// The bar over `row_h` (every row's height, device px, in order) in the
// `room` of a box placed upward or not, its first row `top`.
inline PopupScrollBar popup_scroll_bar(const GuiRect& box, bool upward,
                                       std::vector<int> row_h, int room,
                                       int top) {
    PopupScrollBar b;
    b.total   = static_cast<int>(row_h.size());
    b.room    = room;
    b.row_h   = std::move(row_h);
    b.max_top = popup_rows_max_top(b.row_h, room);
    b.top     = popup_scroll_clamp(b, top);
    b.visible = popup_scroll_shown(b, b.top);
    if (b.max_top == 0) return b;
    b.present = true;
    const int border = popup_border_px();
    const int top_b  = popup_border_top_px(upward);
    const int bw     = popup_scroll_bar_w_px();
    b.bar   = GuiRect{box.x + box.w - border - bw, box.y + top_b, bw,
                      box.h - top_b - border};
    const int ah = std::min(bw, b.bar.h / 2);
    b.up    = GuiRect{b.bar.x, b.bar.y, bw, ah};
    b.down  = GuiRect{b.bar.x, b.bar.y + b.bar.h - ah, bw, ah};
    b.track = GuiRect{b.bar.x, b.up.y + ah, bw, b.down.y - (b.up.y + ah)};
    const int len = std::max(
        popup_scroll_thumb_min_px(),
        static_cast<int>(std::nearbyint(static_cast<double>(b.track.h) *
                                        b.visible / b.total)));
    if (len <= b.track.h) {
        const int seat = static_cast<int>(std::nearbyint(
            static_cast<double>(b.track.h - len) * b.top / b.max_top));
        b.thumb = GuiRect{b.bar.x, b.track.y + seat, bw, len};
    }
    return b;
}
// THE ZONE under (x, y) of a published bar: an arrow, the thumb, the track
// above it (PageUp) or below it (PageDown); None off the bar and on a
// thumbless track.
inline PopupScrollPart popup_scroll_hit(const PopupScrollBar& b, int x, int y) {
    if (!b.present || !rect_contains(b.bar, x, y)) return PopupScrollPart::None;
    if (rect_contains(b.up, x, y))   return PopupScrollPart::Up;
    if (rect_contains(b.down, x, y)) return PopupScrollPart::Down;
    if (b.thumb.h <= 0) return PopupScrollPart::None;
    if (rect_contains(b.thumb, x, y)) return PopupScrollPart::Thumb;
    return y < b.thumb.y ? PopupScrollPart::PageUp : PopupScrollPart::PageDown;
}
// THE TOP A PRESS ON `part` SCROLLS TO: a row for an arrow, a page (the
// shown count at the published top, Windows' SB_PAGE) for the track,
// clamped; the thumb and None leave it.
inline int popup_scroll_step(const PopupScrollBar& b, PopupScrollPart part) {
    int d = 0;
    switch (part) {
        case PopupScrollPart::Up:       d = -1; break;
        case PopupScrollPart::Down:     d = 1; break;
        case PopupScrollPart::PageUp:   d = -b.visible; break;
        case PopupScrollPart::PageDown: d = b.visible; break;
        case PopupScrollPart::None:
        case PopupScrollPart::Thumb:    break;
    }
    return popup_scroll_clamp(b, b.top + d);
}
// THE WHEEL'S SCROLL: `rows` (the notches times kPopupWheelRows, down the
// later rows) from the state's own top — so notches arriving between two
// paints add up — clamped against the published bar, written to `sc`;
// whether it moved the shown rows.
inline bool popup_scroll_wheel_rows(PopupScroll& sc, const PopupScrollBar& b,
                                    int rows) {
    const int from = popup_scroll_clamp(b, sc.top);
    const int top  = popup_scroll_clamp(b, from + rows);
    if (top == from) return false;
    sc.top = top;
    return true;
}
// THE TOP THE THUMB'S DRAG SHOWS with the thumb's top at `thumb_y`: the
// seat's inverse, by nearbyint, clamped.
inline int popup_scroll_top_at_thumb(const PopupScrollBar& b, int thumb_y) {
    const int room = b.track.h - b.thumb.h;
    if (!b.present || b.thumb.h <= 0 || room <= 0) return b.top;
    const double t = static_cast<double>(thumb_y - b.track.y) / room;
    return std::clamp(static_cast<int>(std::nearbyint(t * b.max_top)), 0,
                      b.max_top);
}
// THE BAR'S PICTURE (defined in render.cpp beside the view bar's painter):
// under win2000 Windows' vertical scroll bar — the track Windows' checker
// (Hilight over the ground at one device px a cell, its phase the bar's
// top-left), the thumb a PLAIN RAISED box on the ground with no grip, the
// two arrows Windows' scroll-arrow buttons (paint_trim_arrow_button: plain
// raised, the arrow pointing up and down, the pressed face while `held`
// names one). No hover face.
void paint_popup_scroll_bar(cairo_t* cr, const PopupScrollBar& b,
                            PopupScrollPart held);


// (THE WAVEFORM'S VERTICAL INSET IS GONE — architect 2026-10-09, "remove":
// waveform_inset_px, a symmetric 6-W margin above and below the channels —
// once the retired tip-down playhead triangle's mask height, kept as "a clean
// stem-only band at the top before the samples begin" — and its unit
// kPlayheadUnitPx. Since the canvas became Cool Edit's that day the playhead's
// head and the cues' triangles stand in the ruler and the marker lane, above
// the canvas's frame, and nothing on the canvas needed the band; the two
// channels fill the canvas whole, as the mock of record's do
// (waveform_channel_band, below). Git history.)

// (THE PLAYHEAD COLUMN'S OLD REACH, playhead_half_px — ± scaled_px(6) − 1,
// the retired tip-down triangle's footprint — bounded the playhead's cull and
// its narrow damage box until 2026-10-09, when Cool Edit's head made it
// narrower than the pixels it guarded (the head's ten quanta against ± 15 at
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
// too small to click"; cue_fill_h_px), from one quantum before its first
// segment's text to one past its last's, cut where the overlap rule cut it
// — and `tri_*` THE TRIANGLE'S, its top edge's nine quanta and the
// shadow's tenth over its five rows. Either may be empty (a label the
// overlap rule cut to nothing, a field standing in for the label, the
// riding cells' run, which carries no triangle). The containment owner is
// flag_hit_rect_contains below; the two boundaries are the label's
// segments' box edges.
struct FlagHitRect {
    int    marker_index = -1;
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
// (waveform_channel_band) — no vertical grid. render_center_lines owns THE
// CENTER LINES, OVER THE WAVEFORM (architect 2026-10-09 evening): each
// channel's zero row, one device px in `center`, painted live after the
// plate's blit and before the dots (on_redraw's paint order) — the canvas's
// lines stay these two painters' and the plate keeps baking its two inks
// alone (WaveformPlateInks). The canvas's frame round the area is
// paint_canvas_column_frame's (paint_handler.cpp).
void render_background(cairo_t* cr, int x, int y, int w, int h);
void render_canvas(cairo_t* cr, const GuiRect& area);
void render_center_lines(cairo_t* cr, const GuiRect& area);

// THE RELIEF HELPERS — the chrome's one painter family for the Windows-95
// edge grammar (architect 2026-10-02; the grammar and the family table at the
// palette head). Every line is relief_line_px() wide and is an integer rect
// of cells, never a stroke. A RING is drawn ON the rect's outermost cells:
// its top and left in `top_left`, its bottom and right in `bottom_right`,
// the two MITRED at the top-right and bottom-left corner blocks (architect
// 2026-10-06; the rule at paint_relief_frame, render.cpp: each block split
// along its 45-degree diagonal, antialiased; the top-left and bottom-right
// corners square); a TWO-LINE EDGE is its outer ring on the rect and its
// inner ring on the rect inset one line (Windows' DrawEdge), the two rings'
// diagonals one line.
//   paint_relief_frame  — one ring, any two colours (the one owner every
//                         helper below calls, and THE PROGRAM'S MITRE since
//                         2026-10-09: every two-tone ring cool_edit_paint.cpp
//                         draws and the view bar's span — its head).
//   paint_relief_soft_raised  — SOFT RAISED: a caption button at rest
//                         (2026-10-05), an on-screen keyboard key.
//   paint_relief_soft_sunken  — SOFT SUNKEN: a caption button pressed, a
//                         keyboard key checked or pressed.
//   paint_relief_plain_raised — PLAIN RAISED: a push button, the popup
//                         scroll bar's thumb and arrows, a menu's and a
//                         dropdown's frame, the restored window's sizing
//                         frame. (The scrub's pointed thumb draws its own
//                         soft raised pentagon, paint_scrub_thumb.)
//   paint_relief_plain_sunken — PLAIN SUNKEN: a field, the list (the folder
//                         overlay's band, 2026-10-06), the scrub's channel.
//   paint_relief_sunken_outer — SUNKEN OUTER, ONE ring (DrawEdge's
//                         BDR_SUNKENOUTER, Shadow / Hilight): the open menu
//                         title, the color picker's field and swatch, the
//                         program's frame (paint_program_frame).
//   paint_relief_line_frame — one colour all round, square (no mitre
//                         between one tone and itself): the dialog's
//                         default-button frame, the list's focus frame and
//                         the popup scroll bar's pressed arrow button.
//   (The CARD FRAME — the tooltip's and the cards' — is no relief: its
//   lines in `card_frame`, square, on all four sides, at paint_popup_chrome.)
//   paint_relief_etched_hline — an ETCHED line: a Shadow line on rows
//                         [y, y + lw) and a Hilight line under it, columns
//                         [x, x + w) (the dropdown's separator).
// (The one-line raised inner edge — the flat toolbars' hot face — and the
// vertical etched line — their group separators — went with the program's
// band, 2026-10-09: Cool Edit's band is cool_edit_paint.h's.)
// (paint_relief_raised / paint_relief_sunken, the one-line pair of the thin
// design, are retired, architect 2026-10-02: every caller names its family.)
// None of them fills the face: a caller fills first and frames after.
void paint_relief_frame(cairo_t* cr, const GuiRect& r, GuiColor top_left,
                        GuiColor bottom_right);
void paint_relief_soft_raised(cairo_t* cr, const GuiRect& r);
void paint_relief_soft_sunken(cairo_t* cr, const GuiRect& r);
void paint_relief_plain_raised(cairo_t* cr, const GuiRect& r);
void paint_relief_plain_sunken(cairo_t* cr, const GuiRect& r);
void paint_relief_sunken_outer(cairo_t* cr, const GuiRect& r);
void paint_relief_line_frame(cairo_t* cr, const GuiRect& r, GuiColor c);
void paint_relief_etched_hline(cairo_t* cr, int x, int y, int w);
// One flat cell rect in `c` — the face fill every relief caller lays first.
void paint_cell_rect(cairo_t* cr, const GuiRect& r, GuiColor c);
// THE CHECKED DITHER (architect 2026-10-02, Windows' checked toolbar button
// and its scroll-bar track): `r` filled as a checkerboard of `lit` and
// `ground`, both theme roles the caller passes (Hilight and the ground at
// both sites), in square cells ONE DEVICE PX on a side (architect
// 2026-10-06, the palette head's dither rule), the pixel at (phase_x,
// phase_y) lit and its neighbours alternating — a pixel is lit when its
// column plus its row, counted from the phase, is even — so two surfaces
// sharing a phase dither alike. Drawn as WINDOWS' OWN PATTERN BRUSH: a 2 x 2
// image (lit / ground / ground / lit) repeated, nearest-filtered and offset to
// the phase, one opaque fill of the rect — nothing blended, every pixel one
// of the two roles. The callers still lay the ground first, as every face
// does.
void paint_checker_rect(cairo_t* cr, const GuiRect& r, int phase_x,
                        int phase_y, GuiColor lit, GuiColor ground);

// THE DISABLED EMBOSS, THE WORD HALF (architect 2026-10-03, Windows' DrawState
// DSS_DISABLED; the rule at the palette block): `run` painted in the theme's
// Hilight one Windows px (relief_line_px) right and down, then in its
// Shadow at (x, baseline) over it — every disabled word in the product goes
// through here (a disabled glyph is icons::draw_disabled's). The
// run carries its own font (text_shape.h).
void show_embossed_run(cairo_t* cr, const text_shape::ShapedRun& run,
                       double x, double baseline);


// THE WAVEFORM AREA IS THE CANVAS: the plate blit, the canvas's lines, the
// lead-in ring and the zoom anchor's stem each span it whole (row 6's
// canvas paragraph).
//
// ONE CHANNEL'S BAND, area-local rows: the plate's (render_waveform_to_cache_
// surface, its one layout) and the canvas's grid and center lines'
// (render_canvas), so the lines stand on the zero row the plate's bars
// straddle. THE TWO CHANNELS FILL THE CANVAS (architect 2026-10-09, the
// inset removed; the mock of record's channels fill it top to bottom): each
// takes the canvas's halved-and-floored height, channel 0 [0, h) and channel
// 1 [h, 2h), EXACTLY EQUAL AND ABUTTING — no row between them (architect
// 2026-08-03, when the channel-split line retired: a spare row between would
// read as a one-px gap in the ink). At an odd canvas height the spare row is
// the canvas's LAST, under channel 1, where the canvas's ground and lines
// paint and no ink reaches. Its ZERO ROW is y + h / 2, the row
// render_waveform's centre y + h · 0.5 floors to; its HALF HEIGHT H, the
// grid's (row 6's canvas paragraph), runs from that row to the band's edge.
// h <= 0 only on a canvas under two rows.
struct WaveformChannelBand {
    int y = 0;
    int h = 0;
    int zero_row() const { return y + h / 2; }
};
inline WaveformChannelBand waveform_channel_band(int area_h, int channel) {
    const int ch_h = area_h / 2;
    return WaveformChannelBand{channel == 0 ? 0 : ch_h, ch_h};
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
// (render_waveform) and the checked dither's tile (paint_checker_rect), each
// handing it the color already converted for the window (display_color), and
// the caption's gradient (paint_caption_gradient), which keys its cache on
// the authored words and writes its converted ramp itself; a bound icon's
// color (icons.cpp) takes the authored word's low 24 bits, its raster
// converted after.
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
// context. ONE COMPOSITING RULE, where there were two: every write REPLACES.
// The caller cleared every column this call regenerates and each column is
// written by its one bar with the lamp dark, and lit by its outer bar and
// then its inner bar over it (the inner overwriting the outer where they
// overlap is the magnification rule's order), so
// replacing is correct and idempotent — the max-compositing that the segments needed (they wrote into an
// already-rendered neighbour) went with them.
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
// has no outline (its outline ink would be its fill). Paint order per column:
// outer, then the inner's fill and border over it; on a steep rise between
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
// THE COST is one extra row fill and one lookup (the scale) per column — the
// read,
// the map walk and the gain lookup are shared; no second pyramid, no second
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
// estimates on the peak measure, measured 2026-09-23,
// tmp/gain_check/review_coarse.py). The
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
// span covers (waveform_expander_multiplier_over, a plain loop, no pow), and
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
void render_waveform(cairo_surface_t* dest,
                     GuiRect area,
                     int col0,
                     const GuiAudio& audio,
                     int channel,
                     const WaveformBasis& basis,
                     const WaveformGainCurve* gain_or_null,
                     int outline_px,
                     WaveformPlateInks inks,
                     const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr);

// Draws a waveform_line_px()-wide vertical LINE down the CANVAS, `area`
// whole (row 6's canvas paragraph), at the column nearest `playhead_pixel_x` (offset
// from area.x), [col, col + t), in `color`, in the FORM the caller picks
// (architect 2026-10-09, the program is Cool Edit): THE SCANNER SOLID
// (PlayheadForm::Solid, Cool Edit's playback cursor a solid 1-px line), THE
// RESTING CURSOR DOTTED (PlayheadForm::Dotted, fill_dotted_waveform_line on
// the playhead's phase, its dots interleaving with the cues'), painted
// straight over whatever it crosses — waveform ink included. No-op if
// outside; the line is gated on its own column and clipped at the right
// edge, so it never leaks into an adjacent region.
//
// THE LINE IS THE WHOLE FUNCTION (2026-08-02): the cursor's head is
// paint_ruler_row's (the yellow triangle in the ruler since 2026-10-09), and
// no strip lane is reachable from here.
//
// The former two-tone form (an `ink_plate` parameter carrying the displayed
// plate, whose alpha masked a ground-colored overdraw wherever the column
// crossed an opaque sample) is retired too: architect 2026-07-26, the notch
// retired with the polarity inversion — the contrast problem it patched is
// solved by the scheme, so that parameter went with it.
enum class PlayheadForm { Solid, Dotted };
void render_playhead(cairo_t* cr,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color,
                     PlayheadForm form);

// Draws the strip-drag ANCHOR STEM: a vertical line at the drag's pivot
// column `col` (window pixels within `area`, clamped here to [0, area.w-1]),
// spanning the canvas, `area` whole, solid, in the `scanner` role — the
// white of the stems that belong to the controls, the playhead alone being
// yellow (architect 2026-10-09; the palette block's playhead paragraph owns
// the rule).
// The anchor is
// the clamped column the strip-drag math pins each event — edge-included, so an
// edge-pinned anchor draws the stem exactly at the edge and the clamp becomes
// visible (the Ableton affordance). It paints ONE solid
// color straight over the waveform ink it crosses — the ink-notch overdraw and
// its plate parameter are retired (architect 2026-07-26, with the polarity
// inversion). The line is an aliased integer rect waveform_line_px() wide,
// [col, col + t), clipped at the right edge (fill_waveform_line).
void render_strip_anchor_stem(cairo_t* cr,
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
// program is Cool Edit; METRICS §3, the palette block's canvas-column
// paragraph for its colors, trim_lane_h_px for its rows), the same under
// every chrome: the black field across the waveform's columns in its sunken
// ring (the top line and the left column, the bottom line and the right
// column, mitred), then THE SPAN — the trim window's columns, the begin
// column through the end column inclusive, a raised one-quantum bevel block
// of the ink in the field's rows, sliding off an edge like any content
// (architect 2026-10-05: an out-of-view bound reads as the window running
// on); no playhead (the canvas-column paragraph above, 2026-10-09 ~14:30).
// All pixel-bound integer fills but the ring's and the span's mitred corner
// blocks, antialiased (paint_relief_frame, 2026-10-09). The lane band is
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

// ONE MARKER STEM, as the flag painter publishes it: the window x of the
// column the cue's dots stand on (the triangle's apex column) and WHICH OF
// THE TWO CUE COLORS' DOTS its class resolved to (resolve_flag_face,
// render.cpp; architect 2026-10-09 ~11:50): the red `cue` dots, the blue
// `range` dots, or both — a warp or phase-reset cue both, alternating; an
// invalid one and the history's removed half the red alone; the history's
// added half the blue alone — selected or not (Cool Edit's cue never
// changes when selected). The flag PAINTERS are the only producers — the
// two live columns', and the `h` view's diff lane, which replaces them
// wholesale while the mode stands; the one reader is the per-frame waveform
// pass GuiPaintHandler::paint_marker_stems, which paints each color's dotted
// column on its phase (fill_dotted_waveform_line, program_spec.h's dot
// fields) in the palette's live role, so the dots and their triangle can
// never disagree about a column or a class.
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
    int    marker_index;
    double x;
    bool   cue_dots;     // the red, on cue_dot_phase
    bool   range_dots;   // the blue, on range_dot_phase
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
// pixel column on the lane's last rows, and its LABEL six quanta right of
// the column on its box over the lane's whole height — the payload label
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
// read forwards — the left one on top, the labels painting right to left;
// topmost_flag_rect, app_state.cpp), and `out_stems` one entry per ENABLED cue
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
//                   they already are graphically). The one flag walk
//                   (topmost_flag_rect, app_state.cpp) asks this rect FIRST,
//                   because the editor paints last and so covers whatever the
//                   lane pass drew under it. The two publications cannot
//                   overlap in any case: the pass's label box ends where the
//                   edited segment begins and this run begins past the field's
//                   right edge, so under a BOUND editor the same marker
//                   publishes both — its triangle and its payload (and, under
//                   the upper field, its lower cell) in the lane stash, its
//                   riding run here — and a point falls in exactly one. A press on a riding cell is
//                   therefore an ORDINARY OUTSIDE PRESS: the payload editor
//                   closes without committing like it does for every other
//                   outside press, and the press then acts on the cell under
//                   it — single-select, land, address that cell, and a
//                   double-click's second press opening that cell's own
//                   editor through the ordinary consume-open road. It is
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
// says why, the dialog field's rule on this surface.
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
// edge is READ BY PANNING THE VIEWPORT, this editor being pointer- and
// wheel-transparent so the wheel and the grab-pan work while it stands.
// State::view_offset_px survives on this surface for the sub-pixel case alone
// (a run's width rounded down to a whole column, which would otherwise clip
// the caret's reserved column at end-of-text); the glyph-by-glyph travel its
// minimal-travel rule describes belongs to the DIALOG field, whose box is
// fixed. Left/Right/Home/End navigate exactly like any one-line field.
//
// Takes AppState by NON-CONST reference, alone among the renderers here, and
// for two honest reasons: it advances the editor's view offset (session state
// that must persist across frames) and it publishes app.flag_editor_box. Both
// are the painter owning what only the painter can compute. (The DIALOG field's
// painter — paint_modal_dialog, paint_handler.cpp — does exactly the same two
// things for exactly the same reasons since 2026-08-13, when that field grew
// this scroll; it is a GuiPaintHandler method and already holds an AppState&.)
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

// THE DOTS A LIVE PHASE RESET'S STEM WEARS, for the surface that must wear
// them too — the lead-in ring (paint_phase_reset_overlay_ring,
// paint_handler.cpp; the ring and the cue one unit, architect 2026-09-17,
// dotted in the stem's pattern since 2026-10-09 ~17:40). It asks the one
// resolver (resolve_flag_face's table, render.cpp) rather than restating it:
// the red `cue` dots and the blue `range` dots for a valid reset, the red
// alone for one in the column's red set (`red`,
// phase_reset_red_flag_set_cached) — resting, selected or not (the cue never
// changes when selected), so the ring and its stem stay one object. The
// colors are the palette's two live roles, read by the painter as the stems
// read them. No disabled arm, a disabled reset painting neither dots nor
// ring.
struct GuiStemDots {
    bool cue_dots   = false;
    bool range_dots = false;
};
GuiStemDots phase_reset_stem_dots(bool red);

// ONE PREPARED DIFF FLAG for the `h` history mode's lane, in the ORDER it is
// painted and published. The caller (maybe_rebuild_flag_cache) resolves the
// commit's delta into these; this file only paints what it is handed, so the
// diff model's types never reach the renderer.
//
// THE FRAME FIELD IS NAMED time_frame ON PURPOSE: it is what the shared column
// mapper iterate_visible_flags_impl reads off a marker, so a diff flag rides the
// EXACT expression a live marker rides — same map, same viewport, same width,
// same nearbyint — rather than a second spelling of it.
//
// The two halves are independent bools rather than an enum because the CHANGED
// case is exactly "both": one double-width flag, the removed half (`-`) left,
// the added half (`+`) right.
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
    int64_t     time_frame = 0;
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
    // NO RED CLASS TRAVELS HERE, and that is deliberate (recorded 2026-09-02,
    // the disabled axis's sibling): the live lane's THIRD marker class — the
    // normalization red a coincident stack or a dangling reference earns — is
    // a verdict over the WHOLE resolved store, and a diff half is a line from
    // one side's sidecar with no store around it to resolve against. So a
    // checkpoint's collapsed stack paints as ordinary added/removed halves in
    // the view and reddens only once its lines are back in the live store.
    // The disabled axis DOES travel (the two effective bits above, 2026-08-22)
    // because it is a property of the line itself.
};

// THE HISTORY MODE'S MARKER LANE. Replaces render_flags / render_phase_reset_-
// flags wholesale while the mode stands: no live marker paints, and this pass
// becomes the producer of the same two stashes they produce (out_hit_rects with
// `marker_index` carrying the INDEX INTO `flags`, out_stems the same), so
// hit_test_flag keeps working unchanged and answers a diff-flag index.
//
// THE ANATOMY IS THE LIVE CUE'S (the palette block's marker-lane paragraph):
// the triangle and its dots by ITS KIND (architect 2026-10-04; the colors
// 2026-10-09 ~11:50, resolve_flag_face's table) — an added flag the
// `range` blue with the blue dots alone, a removed flag the `cue` red with
// the red dots alone, a CHANGED pair the removed half's, the half its dots
// leave from; never the invalid class, so never the `invalid_label` pair —
// and the label in the program face. THE LABEL CARRIES THE SIGN: `[+]` before an added line's
// payload, `[-]` before a removed one's (history_diff_label,
// paint_handler.h). A CHANGED pair's label is its two halves as two
// segments, the removed then the added, the segment gap between them. The
// pair is still ONE flag: one rect, one focus, one claim.
//
// EVERY HALF IS SIZED BY ITS OWN SHAPED TEXT, so the halves of a pair are
// routinely ASYMMETRIC and the one hit rect composes off the two measured
// widths. THESE LABELS ARE NEVER TRUNCATED (the cull bound follows the
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
// together as ONE selected box (the live lane's selected label). One look
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

