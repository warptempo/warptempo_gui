#pragma once
#include "warpmarkers.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment for target-view waveform
#include "waveform_gain.h"    // WaveformGainCurve, the waveform picture's gain

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
// exact [0,1] double — the one road from a recorded byte triple (the theme
// table's, a program key's, a named literal's) to a colour. RGB only: the
// palette composites nothing (its head, below), and the renderer hands colours
// to cairo through set_palette_source and set_waveform_source.
inline constexpr GuiColor hex(uint32_t rgb) {
    return GuiColor{
        static_cast<double>((rgb >> 16) & 0xFF) / 255.0,
        static_cast<double>((rgb >>  8) & 0xFF) / 255.0,
        static_cast<double>( rgb        & 0xFF) / 255.0,
    };
}

// Trim boundaries in domain-frame samples (source-frame in source view,
// target-frame in target view). Trim no longer dims any renderer — it is
// consumed by render_trim_flags to place the bar and its endcaps. Values
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
// THE GUI'S COLOURS ARE TWO SETS AND NOTHING ELSE (architect 2026-10-03): THE
// THEME'S RECORDED ROLES — an imported desktop theme of the era, the device
// config's `theme` at its `theme_level`, read off the generated table
// (theme_table.h, tools/theme_catalog/: the catalog's bytes, the level
// arithmetic run once there, so the app computes no colour) — and THE
// PROGRAM'S OWN COLOURS, open device keys (device_config.h): the waveform's
// ink, canvas and outline, the flag's face and selected face and their
// recorded labels, the invalid face and its selected face and its label, the
// playhead's head and stem. The chrome is the theme's; the waveform pane, the
// flags and the playhead are the program's own elements.
// EVERY COLOUR A PAINTER HANDS CAIRO is one of GuiPalette's fields through the
// one accessor palette(), or the one named literal beside them (the in-place
// editor's frame, kFlagEditorFrame below), or one of icons.cpp's hand-listed
// inks. TEXT OVER A FILL IS THE FILL'S RECORDED PAIR — Windows recorded a text
// colour beside every face (ButtonFace / ButtonText, Hilight / HilightText,
// Window / WindowText) — never a luminance verdict.
// STILL OPAQUE, STILL NO COMPOSITING, NO GRADIENTS, NO ROUNDED CORNERS, NO
// HOVER FACES: every colour is a solid fill of integer cells.
//
// A HEX HERE IS A DISPLAY-P3 BYTE TRIPLE AND IS WHAT THE TABLET SHOWS
// (architect 2026-10-02). The tablet's window is a Display-P3 layer
// (GuiPlatform::adopt_window, platform_android.cpp), so the panel takes the
// bytes as P3 coordinates as-is — a theme's recorded byte included; the
// laptop's untagged sRGB surface shows the same bytes a little differently,
// which is accepted (the laptop is for debug testing). No colour is converted
// anywhere between a byte and cairo.
//
// THE GRAMMAR IS WINDOWS 95's DrawEdge, AT THE WINDOWS PIXEL (architect
// 2026-10-02): every raised or sunken edge is TWO lines a side, each one
// Windows px (relief_line_px, below), the outer pair first and the inner pair
// inset one line, and in each pair the top-left line first and the
// bottom-right line last so it owns the top-right and bottom-left corner
// pixels. THE FAMILIES (the painters are paint_relief_soft_raised and its
// siblings, below), lines given outer then inner, top-left / bottom-right, in
// the theme's quartet:
//   SOFT RAISED   Hilight / DkShadow, 3DLight / Shadow — a toolbar button
//                 (EDGE_RAISED | BF_SOFT);
//   SOFT SUNKEN   DkShadow / Hilight, Shadow / 3DLight — a toolbar button
//                 checked or pressed;
//   PLAIN RAISED  3DLight / DkShadow, Hilight / Shadow — a push button, the
//                 scroll-bar thumb, a menu's and a dropdown's frame
//                 (EDGE_RAISED);
//   PLAIN SUNKEN  Shadow / Hilight, DkShadow / 3DLight — a field, the well
//                 (EDGE_SUNKEN);
//   STATUS SUNKEN ONE line, Shadow / Hilight — a status-bar panel;
//   ETCHED        a Shadow line with a Hilight line immediately beside it —
//                 the ruler's ticks, a menu separator;
//   INFO FRAME    ONE line, 3DLight top-left / DkShadow bottom-right — the
//                 tooltip and the notification cards, on the ground under
//                 the label (THE INFO FACE, below; the frame measured on
//                 Windows' ToolTip, a light top-left and a black
//                 bottom-right, architect 2026-10-02).
// The CHECKED face is Windows' dither: a checkerboard of Hilight over the
// ground in one-Windows-px cells (paint_checker_rect, below).
//
// THE MAPPING (architect 2026-10-03), role -> what it paints:
//   ground        every chrome surface: the five lanes, the bottom row, the
//                 dropdowns, the folder overlay and the picker, the on-screen
//                 keyboard, every button face, a DISABLED flag's face, the
//                 tooltip and every notification card (THE INFO FACE);
//   label         chrome text and glyphs, the ruler labels, the trim lane's
//                 arrow glyph, the playhead head's outline, the tooltip's
//                 two lines and every card's words;
//   the quartet   every relief line, the families unchanged; Shadow also the
//                 ruler ticks (the etch's Hilight line beside each) and a
//                 selected disabled flag's label, DkShadow also the flag
//                 outline (on every flag, selected or not) and the dialog
//                 focus frame;
//   emboss_light  the light copy of THE DISABLED EMBOSS (below);
//   selected pair a dropdown's lit row, the open menu anchor, the folder
//                 overlay's and the picker's highlighted row (its glyph in
//                 the selected text too, icons::draw_in_ink), the selection
//                 band and selected substring of every text field, the flag
//                 editor's included — ONE PAIR, FOCUSED OR NOT (the 2026-09-02
//                 inactive selection face retired, architect 2026-10-03, on
//                 Windows 95's Network Neighborhood, where a selection keeps
//                 Hilight / HilightText in a window that has lost the focus);
//   field pair    the modal dialogs' fields, the caret its field's text
//                 (architect 2026-10-03); the flag editor is the selected flag
//                 opened for edit and takes no field colour (EDITING, below).
// THE TWO PAIRS ARE THE TABLE'S ROW like every role: as recorded at LIGHT,
// and at DARK each ground darkened in proportion with the face under the
// level's white text (architect 2026-10-03, mock sets AS and AT; the rule
// is tools/theme_catalog/levels.py's), so no painter asks the level.
// THE INFO FACE IS THE LEVEL'S GROUND UNDER THE LEVEL'S LABEL, ON BOTH LEVELS
// (architect 2026-10-03, on mock set AY: "the card should just become
// ground"): the tooltip, both of its lines (no dimmed second line: Windows'
// ink, no dims), and every notification card, framed by the one-line INFO
// FRAME, which is what tells a card from the chrome (paint_popup_chrome's
// Info face). Windows' COLOR_INFOBK yellow and a theme's recorded
// InfoWindow / InfoText are not carried: at dark the proportional info ground
// read as "a dingy beige", and a recorded pair, picked for contrast on its
// own light ground, does not translate — one rule for both levels and no
// recorded exceptions, so the light level loses the yellow too. THIS
// PARAGRAPH IS THE RULE'S ONE STATEMENT: a later ruling that restores the
// LIGHT level's recorded pair amends it here, and the pair rides the table
// again (tools/theme_catalog/levels.py; catalog.json still records every
// entry's).
// THE WELL keeps its two-line plain sunken edge top and bottom (render_canvas).
//
// THE DISABLED EMBOSS — EVERY DISABLED WORD AND GLYPH (architect 2026-10-03,
// Windows' DrawState DSS_DISABLED): the word or glyph in emboss_light one
// Windows px right and down, then the same word or glyph in Shadow at its
// place over it. The glyph half is icons::draw_engraved, the word half
// show_embossed_run (below); nothing else dims — the dims of the kdenlive
// design (the disabled mix, the accelerator column's and the tooltip line's)
// retired with the luminance rule the same day. A DROPDOWN'S ACCELERATOR
// COLUMN takes its item's own ink: the item's label ink, the selected text on
// a lit row, the emboss on a disabled one.
//
// WHAT WAS HERE BEFORE, in one sentence, because the file's shape is its
// residue: until 2026-10-03 the palette was eight hard-coded base roles (a dark
// #303030 ground, a #96BFDA accent and the program's colours) with every other
// colour derived from them by stated ratios under static_asserts and the text
// over a fill chosen by WCAG 2.0's relative-luminance contrast — before that a
// kdenlive-sampled face (2026-07-31..10-02) and, until 2026-08-02, 23 mutable
// globals read from ~/.config — and the catalog keeps that dark look as an
// entry (`warptempo-2026-10-03`), the rest being git history.

// -- THE CHOKEPOINTS ----------------------------------------------------------
//
// EVERY COLOUR THIS PRODUCT HANDS CAIRO GOES THROUGH ONE OF THESE TWO, and no
// site calls cairo_set_source_rgb itself (re-grepped 2026-10-03: none outside
// render.cpp's two bodies): set_palette_source for the chrome and
// set_waveform_source for the waveform's one cairo fill, the canvas
// (render_canvas). Each is a plain hand-over; the plate's own pixels are
// written as words (argb32_opaque_word), not through cairo.
void set_palette_source(cairo_t* cr, GuiColor c);
void set_waveform_source(cairo_t* cr, GuiColor c);

// -- THE ACTIVE PALETTE ---------------------------------------------------------
//
// The resolved colours, one struct: the theme's roles (theme_table.h's row at
// the level, the mapping above) and the program's keys. Every field is a solid
// byte triple.
struct GuiPalette {
    // THE THEME'S RECORDED ROLES.
    GuiColor ground;          // COLOR_3DFACE (KDE's background, CDE's set 5)
    GuiColor label;           // COLOR_BTNTEXT
    GuiColor hilight;         // COLOR_3DHILIGHT
    GuiColor light_3d;        // COLOR_3DLIGHT
    GuiColor shadow;          // COLOR_3DSHADOW
    GuiColor dk_shadow;       // COLOR_3DDKSHADOW
    GuiColor emboss_light;    // the disabled emboss's light copy (levels.py)
    GuiColor selected_fill;   // COLOR_HIGHLIGHT
    GuiColor selected_text;   // COLOR_HIGHLIGHTTEXT
    GuiColor field_ground;    // COLOR_WINDOW
    GuiColor field_text;      // COLOR_WINDOWTEXT
    // THE PROGRAM'S OWN COLOURS, the device config's open keys (the record of
    // each is at its key, device_config.h).
    GuiColor waveform_ink;
    GuiColor waveform_canvas;
    GuiColor waveform_outline;
    GuiColor flag_face;
    GuiColor flag_face_selected;
    GuiColor flag_label;
    GuiColor flag_label_selected;
    GuiColor invalid_face;
    GuiColor invalid_face_selected;
    GuiColor invalid_label;
    GuiColor playhead_head;
    GuiColor playhead_stem;
};

// THE ONE ACCESSOR every painter reads. The installed palette is file-scope
// state in render.cpp, written by install_palette alone at its TWO
// application points — gui_main's startup, before the window exists, and the
// settings editor's commit of any of the device config's colour keys
// (`theme`, `theme_level` and the twelve program keys) — the gui_scale shape.
// Before the first install it is construction state (black), never painted.
const GuiPalette& palette();

// Install the palette the device config `cfg` names: its theme row at its
// level (theme_table.h) and its twelve program keys, each already through the
// one grammar (device_config.h), so this resolves and never refuses. It bumps
// palette_generation below, the flag cache's fingerprint term, and the plate's
// two baked inks (waveform_plate_inks) move with it; the caller damages the
// window. Declared against device_config.h's struct, which render.cpp
// includes.
struct DeviceConfig;
void install_palette(const DeviceConfig& cfg);

// THE PALETTE'S GENERATION — a counter install_palette bumps, so a cached
// surface whose pixels carry palette colours keys the palette BY FIELD (the
// flag cache, FlagCache::fp_palette_generation). The waveform plate bakes only
// its two inks and keys those directly (waveform_plate_inks).
uint64_t palette_generation();

// THE IN-PLACE EDITOR'S FRAME — Windows' WindowFrame, BLACK (architect
// 2026-10-03, on Explorer's F2 rename and Acid Pro's track-name editor): the
// flag editor's one-Windows-px frame round its flat box (render_flag_editor_box).
// A LITERAL, the chrome's one colour that is neither a theme role nor a
// program key: the ruling names black, as Windows 95 Standard records its
// WindowFrame, on every theme, and it stays black whatever the edit's state
// (a refused Enter recolours nothing, EDITING below).
inline constexpr GuiColor kFlagEditorFrame = hex(0x000000);

// -- THE TRIM LANE, THE RULER LANE, THE PLAYHEAD -------------------------------

// THE TRIM LANE IS A MINIATURIZED SCROLL BAR (architect 2026-10-02; the
// geometry at kTrimLaneHeightPx and render_trim_flags) and takes no colour of
// its own: its track is the CHECKED dither (Hilight over the ground), its
// thumb's body and its two arrow buttons plain raised boxes on the ground, and
// THE ARROW GLYPH THE LABEL (architect 2026-10-03: a chrome glyph on a chrome
// face). A HELD END CAP IS THE PRESSED SCROLL ARROW (architect 2026-10-03),
// from the press on it to the gesture's end, a motionless press included:
// Windows draws a held scroll arrow DFCS_PUSHED | DFCS_FLAT — one Shadow line
// round the face, the glyph one Windows px right and down (render_trim_flags).

// THE RULER LANE's inks: every timestamp in the LABEL (architect 2026-10-03;
// "give the same colour to all the numbers", 2026-10-02), and the TICKS
// ETCHED — each tick a Shadow line with a Hilight line immediately to its
// right over the same rows (architect 2026-10-02, the Sonic Foundry etching;
// paint_ruler_row).

// THE PLAYHEAD (the program's keys, architect 2026-10-03). The HEAD is an
// aliased shape in the `playhead_head` key, seated on the ruler lane's bottom
// rows (paint_ruler_row), with a ONE-WINDOWS-PX OUTLINE in the theme's LABEL —
// the head's own boundary pixels, so the head keeps its size (the Windows 95
// arrow cursor's edge). THE HEAD IS THE HOLD POSTURE'S LAMP (architect
// 2026-09-24): while AppState::camera_hold stands it paints in the STEM'S key,
// outline kept — a STATE COLOUR, not a class — repainted by the per-tick
// comparator (main.cpp), since the bit flips with no damage of its own. THE
// STEM is the `playhead_stem` key, UNIFORM from the head to the canvas's foot
// (its contrast over the chrome and the canvas is the user's choice), and THE
// SCANNER — the moving playback line, paint_scanner — is the moving stem and
// takes the same key, as does the zoom anchor's stem (render_strip_anchor_stem).

// -- THE MARKER LANE: THE FLAT FLAGS ---------------------------------------------
//
// THE FLAG IS THE ACID FLAG (architect 2026-10-03, the bevelled Sonic Foundry
// box retired as "a 3D surface with a cut through it"): a FLAT BOX in the
// `flag_face` key, its label in the key's RECORDED label colour `flag_label`
// (a recorded pair, like Windows' — never a luminance verdict), and a
// ONE-WINDOWS-PX OUTLINE in the theme's DkShadow on all four sides, selected
// or not (below) — the box's left border column, its top row, its run's
// closing column and its bottom row, so the box keeps its width and height. THE STEM leaves from the box's
// LEFTMOST FACE COLUMN (the marker's own frame column) and crosses the bottom
// outline into the well and the canvas, one same-colour column. Warp and
// phase-reset flags alike: ONE FLAG COLOUR FOR EVERY KIND (the column pairs
// retired), and the bound cells take the same anatomy, each cell's left seam
// the shared outline column.
//
// THE STATES (resolve_flag_face, render.cpp, the one ladder: disabled wins,
// then invalid, then the flag; each arm answers selected and unselected):
//   THE FLAG  the `flag_face` key under `flag_label`, the stem in the face;
//   INVALID   the red-flag class (warp_red_flag_set_cached and its phase-reset
//             twin): the `invalid_face` key under `invalid_label`, the stem in
//             that face colour;
//   DISABLED  the theme's GROUND for the face, the label in THE DISABLED
//             EMBOSS (above), and NO STEM;
//   SELECTED  A BRIGHTER FACE, NOTHING WHITE (architect 2026-10-03, the colour
//             loop's sets BG..BT, retiring the white outline ring of mock
//             AR02): the selected flag's FACE AND STEM take its selected key —
//             `flag_face_selected`, or `invalid_face_selected` over an invalid
//             one ("bright red means selected and error") — and its LABEL the
//             one `flag_label_selected`, on both faces (the symmetry). The
//             OUTLINE STAYS the one-line DkShadow ("it has to be black and stay
//             black; it's the only one that stays out of the way of the
//             stem"), so every seam column is DkShadow whichever box paints
//             it. It is Windows' own selected-icon reversal, a face that
//             changes rather than a frame that appears. SELECTION IS ONE
//             CELL'S (architect 2026-09-05): the ADDRESSED cell wears the
//             selected face — the payload for every selected marker but the
//             focus, whose addressed cell is AppState::addressed_cell — and
//             THE STEM IS THE FLAG BOX'S (architect 2026-10-04, "otherwise it
//             looks disconnected": the stem belongs to the box it leaves
//             from), so it takes the selected face only when the PAYLOAD is
//             the bright cell and keeps the marker's resting face (`flag_face`,
//             or `invalid_face` on an invalid marker) while a BOUND CELL is
//             the addressed one;
//   SELECTED DISABLED (architect 2026-10-03, PROVISIONAL, pending his ruling;
//             a disabled marker is selectable, so its selection must show):
//             Windows 95's highlighted disabled menu item — the selected face
//             (`flag_face_selected`), the label FLAT in the theme's Shadow
//             (Windows' GrayText on the highlight, no emboss), and still NO
//             STEM. It is the ladder's one arm for the case, so a later ruling
//             changes that arm alone.
// A FLAG HAS NO HOVER FACE (architect 2026-09-29): the pointer over a flag says
// what a press will do through the CURSOR alone (pointer_cursor_kind).
//
// THE SEAM COLUMN between a flag box and the cell to its right is the outline
// column the two boxes share (architect 2026-08-20, kept under every face since),
// the theme's DkShadow whatever either box's state:
// ONE BOUNDARY, THREE RENDERINGS, all taking the outline so it never appears or
// vanishes on an editor open — the resting and the riding cell through the one
// cell painter (paint_iter_bound_cell) and the open bound field's left border.
// The published cell boundary IS the seam column, so a press on the divider
// reads as the cell it introduces. Every run carries a closing column
// (architect 2026-09-25; marker_flag_border_px).
//
// THE PHASE-RESET LEAD-IN RING on the waveform wears the colour its reset's
// stem wears (paint_phase_reset_overlay_ring, through phase_reset_stem_color —
// architect 2026-09-17): the flag key, or the invalid key on a red reset, and
// the matching selected key while the reset's PAYLOAD is the bright cell
// (architect 2026-10-04: a reset whose addressed cell is a bound cell keeps
// its resting stem, and so its resting ring).
//
// EDITING IS WINDOWS 95's IN-PLACE LABEL EDIT (architect 2026-10-03, Explorer's
// F2 rename, Acid Pro's track-name editor): over the edited flag or cell a FLAT
// box of the flag's height and its run's width — THE SELECTED FLAG OPENED FOR
// EDIT (architect 2026-10-03, set BX) — its BACKGROUND the edited marker's
// SELECTED FACE (`flag_face_selected`, or `invalid_face_selected` over an
// invalid marker), framed ONE Windows px in black (kFlagEditorFrame), its TEXT
// and caret `flag_label_selected`, the selected substring in the theme's
// SELECTED pair, Windows' field margin strips (the pads) kept. THE STEM
// FOLLOWS THE PAYLOAD BOX (architect 2026-10-04): the selected face under the
// PAYLOAD field, which is the payload box opened and the addressed cell; the
// marker's resting face under a BOUND-CELL field, the flag box standing at
// rest beside it (render_flag_editor_box). The field pair plays no part in it;
// the bound-cell editor is the same editor.
//
// A REFUSED ENTER RECOLOURS NOTHING (architect 2026-10-03: "a red outline and
// the card is redundant"; the red frame retired from both editors): it
// SELECTS THE WHOLE TEXT in the selected pair (text_editor::refuse), so the
// first keystroke replaces it, and the refusing owner's CARD says why. The
// flag editor's frame stays its one black line and the dialog field's edge
// its plain sunken pair. No glyph marks it.
//
// THE HISTORY VIEW'S DIFF FLAGS take the one flag colour (the greens and the
// removed red retired, architect 2026-10-03: red is invalid-only), THE LABEL
// CARRYING THE SIGN — the history mode's one bracket spelling, `[+]` before
// an added line's payload and `[-]` before a removed one's (architect
// 2026-08-05; history_diff_label, paint_handler.h, the same sign row 8's walk
// line spells), each half of a changed pair its own — and the view's focus
// swap wears the selected face, face, label and stem
// (render_history_diff_flags).

// -- ROW 6: THE WAVEFORM ---------------------------------------------------------
//
// THE WAVEFORM IS THE PROGRAM'S, A NEUTRAL CANVAS UNDER ONE INK (the
// `waveform_canvas` and `waveform_ink` keys): with the magnification lamp dark
// the plate is the raw bar alone in the ink, with no outline; lit, the plate
// is two bars (the rule is at render_waveform's declaration), BOTH FILLED IN
// THE INK, the inner distinguished only by its OUTLINE, its true contour, an
// erosion at distance waveform_line_px() (1 px on the laptop at 138 %, 3 on
// the tablet at 275 %), in the `waveform_outline` key — ITS OWN COLOUR
// (architect 2026-10-03): the canvas's value widens the divide between the
// magnified and the compressed bars, the ink's hides it. Its default #5C5C5C
// is the 2026-09-27 rule's output on the default grey ink #808080 over the
// black canvas (architect 2026-10-03: still its own rule) — the ink over the
// canvas blended 50 % in linear light (GIMP's default compositing), the rule
// picked by eye on 2026-09-27. THE PLATE
// BAKES THE INK AND THE OUTLINE (render_waveform writes their words), so the
// pair rides each render job and the cache's fingerprint
// (waveform_plate_inks); the canvas is laid live under the plate's
// transparent gaps (render_canvas) and bakes nowhere.
//
// THE WELL — the waveform area's two-line border, taken FROM the area at its
// top and its bottom, full window width (the geometry at
// waveform_border_px): a PLAIN SUNKEN edge, Windows' client-area frame
// (architect 2026-10-02 ~21:20, "as few exceptions as possible") — the TOP a
// Shadow line then a DkShadow line, the BOTTOM a 3DLight line then a Hilight
// line, top to bottom, the canvas between, in the theme's quartet. THE STEMS
// CROSS THE TOP LINES (architect 2026-10-02): a marker's stem, the playhead's
// and the zoom anchor's run continuous from the lane above into the canvas
// (waveform_stem_band), and stop at the canvas's foot; a flag box stands on
// the top lines (architect 2026-10-03, marker_flag_box_band), so a marker's
// stem leaves its box's bottom row straight into them.
//
// TAKEN FROM THE AREA, NOT ADDED TO IT: waveform_content_rect is the content
// and it shrinks by these rows, while waveform_area itself does not move, so
// the lane stack, the strip geometry, the effective width, samples-per-pixel
// and every column mapping are untouched by the border.

// THE PLATE'S TWO BAKED INKS, as words: the fingerprint field and the job
// field that carry the active ink and outline to the worker (the worker reads
// no live state), set by install_palette.
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
// [50, 350] — the RANGE's one owner is is_gui_scale_percent (device_config.h),
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
// Windows px, in percent — the tablet 275 (a 16-px glyph = 44 device px),
// the laptop 138 (22). EVERY PAINTED DIMENSION IN THE TREE RIDES IT through
// scaled_px, below, where the rounding rule is stated. (The unit before it
// was the laptop's own pixel at 100 %, the kdenlive crops' measure; the
// constants were re-authored from it the same day.)
void   set_gui_scale_percent(int percent);

// THE LIVE PERCENT ITSELF, for the one thing a factor cannot serve: a CACHE
// FINGERPRINT FIELD. The scale is an input to pixels the way an inset or a
// gain field is, and a fingerprint keys its inputs BY FIELD rather
// than through whatever else happens to move with them — an integer percent is
// what makes that compare exact, where the factor is a double and a derived
// dimension is a coincidence. Nothing paints through this: every painted
// dimension goes on reading gui_scale_factor / scaled_px.
int    gui_scale_percent();

// Scale factor s = gui_scale / 100, device px per Windows px: as low as 0.5
// since the setting's grammar floor came down to 50 (architect 2026-08-10).
double gui_scale_factor();

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
// font sizes (redesign_font_size_px and its siblings: a font size is not a
// grid point and cairo takes a double) and the ruler's unrounded pitch
// compare — are a different concept and deliberately do not come through
// here.
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

// (The former flag_font_size_px() — font_size * 96/72 — is gone with row 7. The
// shared text size is redesign_font_size_px(), below.)

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

// THE PLAYHEAD/INSET UNIT, and the last of the marker flag's old geometry.
//
// It WAS derived: kFlagWidthPx 15 (a marker flag's rectangle width at the
// default font size) scaled on gui_font_scale(), forced odd, halved up. Row 5
// retired the flag rectangle and its fused triangle, and row 7 retired the font
// axis, so the derivation had nothing left to derive from — what survives is the
// NUMBER it produced at scale 1 (8), authored directly here on the gui_scale
// axis. kFlagWidthPx / kFlagHeightPx / flag_lane_w_px / flag_lane_h_px are gone
// with the chain; every pixel is identical at 100%.
//
// TWO CONSUMERS, both below, and they are now ALL of them: waveform_inset_px()
// (the waveform's symmetric top/bottom margin) and playhead_half_px() (the
// damage half-width of a playhead column). The tip-down triangle mask sized
// from this number too and had no caller for it; it is DELETED (2026-08-02),
// and with it playhead_triangle_h_px(), the silhouette accessor both consumers
// used to read through. Each consumer SPELLS ITS OWN DERIVATION from this unit
// now — neither reads the other and neither derives from the other — so the
// two are equal at 100% by inheritance rather than by any requirement, and a
// retune of one is a local edit that authors its own constant when it happens.
// 6 WINDOWS PX (architect 2026-10-02, the unit's change): the laptop pixel's
// 8 re-authored to the length that keeps its tablet size (16 device px then,
// 6 × 2.75 = 16.5 → 16 now).
inline constexpr int kPlayheadUnitPx = 6;

// Authored pixel geometry of the MENU ROW — the top strip's lane 0, at the
// window edge (the kdenlive menu bar, row 1 of the redesign). 19 WINDOWS PX,
// Windows' SM_CYMENU (architect 2026-10-02; it was the kdenlive File item's
// 30 laptop px until the unit's change), AND THE LANE IS ITS CONTENT: the
// row stands at that height with
// the ICON ROW directly under it and no margin, border or line between the
// two. kdenlive, QEMU and virt-manager draw no border between the menubar and
// the toolbar, and neither does this row: its ground — the content ground
// since 2026-10-01, the icon row's own (paint_menu_row) — runs straight on
// into the icon row's.
//
// THE HEIGHT IS THE ANCHOR'S, AND THE ANCHOR IS THE LANE (architect
// 2026-09-09: "make the height of the top row based on the thirty pixels of
// File/Edit ... this way the dropdown will touch the first row, as it does
// in kdenlive"; the number Windows' menu bar's since 2026-10-02): each
// anchor's rectangle fills it top to bottom and IS the anchor's published hit
// rect and the open anchor's highlight (paint_menu_row), the anchors' labels
// and the battery + clock legend are cap-centred in it, and the anchor's foot
// is the
// lane's foot, which is where the dropdown and its damage band hang
// (top_menu_row_area — paint_dropdown and toggle_dropdown read the same
// accessor), so the popup touches the icon row's first pixel.
//
// FLUSH UNDER THE WINDOW'S TOP EDGE, WITH NO AIR ABOVE THE ANCHORS (architect
// 2026-10-01, on the glass: "Let's remove the six pixel padding up at the
// top. We'll just let the text be pretty close. I think that's going to seem
// more symmetric — because right now, relative to the curved top, both the
// clock and the File dropdown look too far down").
//
// The row sizes on gui_scale_factor() like every other lane in the tree,
// rounded with std::nearbyint through scaled_px and floored like every other
// lane metric: 52 device rows at the tablet's 275 %, 26 at the laptop's
// 138 %. TWO ACCESSORS FOR ONE
// NUMBER, deliberately: the lane table reads the LANE and the painter the
// CONTENT, the vocabulary every other row keeps, and this row's lane simply
// has no other term in it — the content is the anchors' box and the labels'
// box both, so there is no third reading.
inline constexpr int kMenuRowHeightPx = 19;
inline int menu_row_content_h_px() {
    return scaled_px(kMenuRowHeightPx, 5);
}
inline int menu_row_h_px() {
    return menu_row_content_h_px();
}
// (THE TOOLBAR ROW IS DELETED — 2026-08-12, the grand relayout's roster
// commit: the labeled Save / Undo / Redo / Render lane, row 2 of the redesign
// since 2026-07-31, dissolved into the ICON ROW as its first group of four
// glyph buttons, so the top strip lost the lane's 44 content + 1px border.
// kToolbarRowHeightPx / kToolbarBorderPx and their accessors went with it;
// the row's button box and label pads survive as the MODAL DIALOG BUTTONS'
// own constants — kModalBtnBoxPx and friends, paint_handler.cpp, 23 and 7 / 7
// Windows px since 2026-10-02 (32 and 9 / 10 laptop px before) —
// which used to read row 2's. The row's crop record is git history.)

// Authored pixel geometry of the ICON ROW — the top strip's lane 1, directly
// under the MENU ROW with nothing between (row 4 of the redesign: TWENTY-SIX
// view/mode/action buttons — the kIconRowButtons and kIconRowViewGroup tables
// are the count's one authority, and ALL of them paint on every frame;
// icons::kIconCount is a different number, the GLYPH set, which the row does
// not exhaust). THIS BLOCK IS ALSO THE BOTTOM ROW'S CONTENT: that lane
// delegates its content height to icon_row_content_h_px below.
//
// THE BUTTON IS WINDOWS 95's TOOLBAR BUTTON, AT THE WINDOWS PIXEL (architect
// 2026-10-02, the AB / AD sets): a CASE 23 wide and 22 tall with the 16 x 16
// glyph at (3, 3) — three px of case left of and above the glyph, three
// below it and FOUR right of it, the extra column being Windows' own on the
// right-hand side. THE CASE IS A COMPOSITE OF ITS ROUNDED PARTS (scaled_px's
// rule): its width is scaled_px(3) + scaled_px(16) + scaled_px(4) and its
// height scaled_px(3) + scaled_px(16) + scaled_px(3) — 63 x 60 device px at
// the tablet's 275 % and 32 x 30 at the laptop's 138 %, the glyph 44 and 22,
// today's glyph sizes on both devices. THE GLYPH RASTERIZES AT scaled_px(16)
// (icons::draw, at the case's (3, 3) offset; the press face's shift is one
// Windows px further right and down).
//
// THE ROW IS THE CASE WITH FIVE WINDOWS PX OF GROUND ABOVE AND BELOW IT
// (architect 2026-10-02, the judged picture's 14 device rows at 275 %:
// 5 × 2.75 = 13.75 → 14): 22 + 2 · 5 = 32 Windows px authored, and in device
// rows 2 · scaled_px(5) + the case's height — 88 at 275 %, 44 at 138 %. THE
// LANE IS ITS CONTENT, NO BORDER (architect 2026-10-01): the trim lane sits
// directly under it and its own first row is the boundary. THE ROW IS
// MODELLED ON KDENLIVE'S SECOND TOOLBAR, the one under its timeline; the
// first, sharing the menubar's ground, was left out for space (architect
// 2026-09-09), so nothing sits between the menu row and this one.
//
// THE BUTTONS TOUCH WITHIN A GROUP and EIGHT WINDOWS PX OF BARE GROUND stand
// between two groups, with NO SEPARATOR anywhere (architect 2026-10-02, the
// Y / Z / AB sets: "the buttons touch", the separators gone): the group
// boundaries are still redesign_button_opens_icon_group's, which now places
// the gap alone. The walk is paint_icon_row's (paint_handler.cpp).
//
// THESE NUMBERS LIVE HERE rather than beside the row's walk because a second
// file reads them: the notification card's glyph box is the toolbar case
// (notifications.cpp), so each number has one definition.
inline constexpr int kIconGlyphPx        = 16;   // the glyph, both axes
inline constexpr int kIconCaseLeadPx     = 3;    // case left of and above the glyph
inline constexpr int kIconCaseTrailXPx   = 4;    // case right of the glyph
inline constexpr int kIconCaseTrailYPx   = 3;    // case below the glyph
inline constexpr int kIconRowAirPx       = 5;    // ground above and below the case
inline constexpr int kIconGroupSpacePx   = 8;    // bare ground between two groups
inline constexpr int kIconCaseWidthPx  =
    kIconCaseLeadPx + kIconGlyphPx + kIconCaseTrailXPx;
inline constexpr int kIconCaseHeightPx =
    kIconCaseLeadPx + kIconGlyphPx + kIconCaseTrailYPx;
inline constexpr int kIconRowHeightPx = kIconCaseHeightPx + 2 * kIconRowAirPx;
static_assert(kIconCaseWidthPx == 23 && kIconCaseHeightPx == 22 &&
              kIconRowHeightPx == 32);
inline int icon_glyph_px()        { return scaled_px(kIconGlyphPx); }
inline int icon_case_lead_px()    { return scaled_px(kIconCaseLeadPx); }
inline int icon_case_w_px() {
    return scaled_px(kIconCaseLeadPx) + scaled_px(kIconGlyphPx) +
           scaled_px(kIconCaseTrailXPx);
}
inline int icon_case_h_px() {
    return scaled_px(kIconCaseLeadPx) + scaled_px(kIconGlyphPx) +
           scaled_px(kIconCaseTrailYPx);
}
inline int icon_group_space_px()  { return scaled_px(kIconGroupSpacePx); }
inline int icon_row_content_h_px() {
    return 2 * scaled_px(kIconRowAirPx) + icon_case_h_px();
}
inline int icon_row_h_px() {
    return icon_row_content_h_px();
}

// A BUTTON IS SQUARE-CORNERED (architect 2026-10-02, the Windows-95 design):
// its box is the relief at relief_line_px, below, and no corner in the
// chrome is rounded.

// -- THE PLAY-SCRUB: SOUND RECORDER'S SLIDER (architect 2026-10-02) ----------
//
// The render player's modal row carries the transport's scrub, and its look is
// Windows 95 Sound Recorder's trackbar, nothing filled ("Sound Recorder
// doesn't fill it"):
//   THE CHANNEL — the PLAIN SUNKEN edge collapsed to its lines, NO INTERIOR:
//     kScrubChannelLines relief lines tall, top to bottom Shadow, DkShadow,
//     3DLight, Hilight (the edge's two rings on a rect four lines tall, its
//     end columns the same rings' sides), spanning the slider's track and
//     centred in the button box's band. Its height is the sum of its rounded
//     lines, 4 × relief_line_px (12 device rows at 275 %, 4 at 138 %).
//   THE THUMB — kScrubThumbWidthPx wide, PLAIN RAISED with the ground for its
//     face, centred on the position and standing on the channel with
//     kScrubThumbAbovePx rows above it and kScrubThumbBelowPx below — 8 + 4 +
//     9 = 21 Windows px, Sound Recorder's 11 × 21, the height the sum of its
//     rounded parts (59 device rows at 275 %, 27 at 138 %).
// NO PLAYED EXTENT AND NOTHING THAT READS THE WINDOW'S FOCUS: the channel is
// lines on the ground, the position is the thumb. The painter is
// paint_modal_dialog's player branch; the press is claim_player_scrub_press.
inline constexpr int    kScrubChannelLines = 4;
inline constexpr double kScrubThumbWidthPx = 11.0;
inline constexpr double kScrubThumbAbovePx = 8.0;
inline constexpr double kScrubThumbBelowPx = 9.0;
// The thumb's width floors at five device px — its two relief rings a side
// and one column of face — and its two overhangs at one row each.
inline int scrub_thumb_w_px()     { return scaled_px(kScrubThumbWidthPx, 5); }
inline int scrub_thumb_above_px() { return scaled_px(kScrubThumbAbovePx, 1); }
inline int scrub_thumb_below_px() { return scaled_px(kScrubThumbBelowPx, 1); }
//
// THE HANDLE'S BOX IS THE GRAB, NOT THE PICTURE: a 14 Windows px box (the
// laptop pixel's 20 re-authored, architect 2026-10-02), the ONE owner of
// that length for its readers — the MAPPING insets the track by half of it at
// each end (the thumb's centre is the frame's position;
// render_player_scrub_x_of, app_state.h) and the press router takes it as THE
// THUMB'S GRAB BAND, the painted thumb's 11 widened to it. Floored at 2 so
// the half-box inset is never zero and the band never degenerates.
inline constexpr double kScrubHandleBoxPx = 14.0;
inline int scrub_handle_box_px() {
    return scaled_px(kScrubHandleBoxPx, 2);
}

// ROW 5's THREE LANES — the TRIM lane, the RULER lane and the MARKER lane,
// stacked in that order under the icon row (the order is main.cpp's lane
// table), the marker lane's bottom edge the waveform's top with no gap. All
// three ride the gui_scale axis; the ruler's height is DERIVED from its label
// face (ruler_lane_h_px, below).
//
// THE TRIM LANE IS A MINIATURIZED WINDOWS-95 SCROLL BAR, 16 WINDOWS PX TALL —
// Windows' own scroll bar width (architect 2026-10-02 ~21:20, the AC set: "a
// very miniaturized scroll bar"): FLUSH on the lane, no trough and no border;
// the TRACK, the trimmed-off stretches either side of the kept region out to
// the window's edges, is the CHECKED dither (Hilight over the ground in one
// Windows px cells, phase anchored at the lane's top-left); the THUMB is the
// kept region: a 16 x 16 Windows-px ARROW BUTTON at each bound — Windows'
// scroll-bar arrow buttons, the begin's pointing left and the end's right
// (architect 2026-10-03, the rule at kTrimArrowButtonPx) — and between their
// inner edges THE BODY, a PLAIN RAISED box on the ground the lane's full
// height with no grip (the painter is render_trim_flags). The lane is ONE
// RECT for paint and for every hit reader — the arrow buttons
// (trim_endcap_rect takes the lane rect's y/h), the bridge's y-gate and the
// framing double-click band — so paint and hit move together by
// construction. 44 device rows at 275 %, 22 at 138 %.
inline constexpr int kTrimLaneHeightPx   = 16;
// THE RULER LANE'S HEIGHT IS DERIVED FROM THE LABEL FACE, NOT AUTHORED AND
// SCALED (architect 2026-10-02). The lane stacks, from its top:
//
//     lane = pad + ceil(ascent) + scaled_px(kRulerBaselineToMarkerPx)
//
// — the labels' line seated so their CAP TOP lands kRulerLabelCapTopPx (4)
// Windows px under the lane's top (the pad, derived from the face's own
// ascent and cap height; paint_handler.cpp owns the rule), the face's ascent
// to the baseline (line_baseline), then SEVEN WINDOWS PX from the baseline to
// the marker lane's top (below). ONE HELPER seats the labels for both
// readers — the painter's baseline and this height (ruler_label_baseline_px,
// paint_handler.cpp) — so the two cannot disagree, and the face's metrics
// are read off the product's own road: the Sans face at
// ruler_label_font_size_px (10 Windows px) through gui_select_font_face,
// measured through cairo-ft on fonts/Roboto-Regular.ttf, SLIGHT, hint
// metrics on:
//   275 % (27.5 px, ascent 26, cap 20; the tablet): pad 11 - 6 = 5, baseline
//     row 31, cap ink rows 11..30: lane 31 + 19 = 50.
//   138 % (13.8 px, ascent 13, cap 11; the laptop): pad 6 - 2 = 4, baseline
//     row 17, ink rows 6..16: lane 17 + 10 = 27.
//   50 % (5 px, ascent 5, cap 4): pad 2 - 1 = 1, baseline row 6, ink rows
//     2..5: lane 6 + 4 = 10.
// The major ticks' rise above the marker lane is the painter's own and does
// not enter the lane.
//
// THE BASELINE → MARKER LANE DISTANCE, SEVEN WINDOWS PX (architect
// 2026-10-02, the U4 mock's "above 6, below 10" laptop rows re-authored at
// the unit's change as 4 and 7), AND IT OWNS THE OVERLAP: the playhead head
// is 8 Windows px seated tip-down on the marker lane's top
// (kPlayheadHeadHeightPx), one taller than this distance, so its widest rows
// OVERLAP the digits' lowest ink rows at the playhead's column — allowed (his
// ruling, "a little overlap is fine"), the head painting over the labels:
// 3 device rows at 275 % (22 against 19), 1 at 138 % (11 against 10), none
// at 50 % (4 against 4). The head's rows do not enter the lane's height.
inline constexpr int kRulerBaselineToMarkerPx = 7;
inline int trim_lane_h_px() {
    return scaled_px(kTrimLaneHeightPx, 3);
}
// Defined in paint_handler.cpp beside the label seat it reads; the rule is
// the block above.
int ruler_lane_h_px();

// THE MARKER LANE'S HEIGHT IS DERIVED FROM THE FLAG BOX, NOT AUTHORED
// (architect 2026-10-02, the AC / AD sets' rule, tools/palette's flag_seat):
// the flag box is its top edge band (marker_flag_edge_h_px), the normal
// face's ascent and its descent, every term a whole device row —
//
//     box  = edge + ceil(ascent) + ceil(descent)
//     lane = scaled_px(kMarkerLaneAirPx) + box
//
// — ONE WINDOWS PX OF GROUND ABOVE THE BOX AND NONE BELOW IT (architect
// 2026-10-03): the box's bottom row is the lane's last row, so the flag
// stands ON the well, touching its upper border line (the waveform area's
// first row, the next lane down) and never overlapping it, and the marker's
// stem runs on from the box's bottom straight through the well's top lines
// (waveform_stem_band). THE AIR ABOVE IS THE MINOR TICKS' (architect
// 2026-10-03): the ruler's minor ticks start at the marker lane's top
// (paint_ruler_row's minor_top), so that one px is where the comb stays
// visible over a run of flags — "the minor ticks visible above the flags are
// helpful" — which is why the air above is kept while the air below went.
// The box's label is seated as a LINE under its edge band (baseline = box
// top + edge + ceil(ascent)). The face is the redesign's 13 Windows px
// (redesign_font_size_px), read off the product's own road like the
// ruler's: 275 % (35.75 px, ascent 34, descent 9) box 3 + 34 + 9 = 46, lane
// 3 + 46 = 49; 138 % (17.94 px, 17 and 5) box 1 + 17 + 5 = 23, lane 24; 50 %
// (6.5 px, 7 and 2) box 1 + 7 + 2 = 10, lane 11 (the air floored at one
// row). Every box painter and every flag hit rect takes the BOX's rows
// (marker_flag_box_band), never the lane's: the box is what is painted and
// so what is pressed, and with no air under it there is no strip below a box
// to press. THE THREE ARE DEFINED IN paint_handler.cpp beside the ruler's,
// memoized on the scale the same way.
inline constexpr int kMarkerLaneAirPx = 1;
inline int marker_lane_air_px() {
    return scaled_px(kMarkerLaneAirPx, 1);
}
int marker_flag_box_h_px();
int marker_lane_h_px();
// The box's band inside a marker lane rect `lane`: the lane less its air
// above, the box's bottom the lane's bottom.
inline GuiRect marker_flag_box_band(GuiRect lane) {
    const int air = marker_lane_air_px();
    return GuiRect{lane.x, lane.y + air, lane.w, marker_flag_box_h_px()};
}

// THE WAVEFORM'S MAXIMUM HEIGHT — THE DEVICE CONFIG'S `max_waveform_height`
// since 2026-09-13 (architect: a per-device key in AUTHORED px — Windows px
// since 2026-10-02, the templates' 500 laptop px re-authored as 364 — 0
// meaning no maximum; the range owner is is_max_waveform_height,
// device_config.h). Until that day it was this file's
// constant kWaveformMaxHeightPx = 500, a RULED RETUNABLE (architect 2026-08-12,
// the seventh glass ruling): on tall monitors the natural (leftover) waveform is so
// tall that reaching the ruler and the flag lane "feels cumbersome", so the
// waveform CLAMPS at this height and the leftover becomes BLANK WINDOW GROUND.
// WHERE THAT GROUND SITS IS THE RELAYOUT'S COMMIT B (architect-dictated
// 2026-08-12 at session close): TWO flexible gaps, one under the MENU ROW and
// one above the UNIFIED BOTTOM ROW, sized so THE WAVEFORM'S VERTICAL MIDPOINT
// IS THE WINDOW'S ("the labwc titlebar above and the panel below offset each
// other" — his own reasoning, so the centering is within the app surface with
// no titlebar arithmetic). The stack is MENU ROW / ICON ROW / gap 1 / THE
// CENTERED BLOCK
// (trim, ruler, markers, then the WAVEFORM with its
// own thick bottom border as the block's bottom edge) / gap 2 / THE UNIFIED
// BOTTOM ROW at the window foot. (The ruling's first hours put the whole
// flexible space between the icon row and the trim lane; the row unification
// later that day moved it to the window's foot, under the bottom row, and
// commit B split it in two around the block.)
//
// THE DEFAULT IS 364 WINDOWS PX (550 -> 500 laptop px at commit B, the same
// dictation; re-authored in the Windows unit 2026-10-02). The architect's
// standing bracket: "bigger than the height on the Pi, smaller than the
// waveform height on my external monitor"; the accessor scales the value
// with gui_scale by construction. On the 1920x1080 laptop the leftover
// is well over the default, so the waveform CLAMPS at it and the two gaps
// take the rest, while a 1024x600 SHORT WINDOW's leftover is under it,
// UNCLAMPED, the centering infeasible and both gaps floored at 0. The
// figures — every lane, both gaps, each worked window — are main.cpp's
// vertical block, the one owner, re-derived there from the lane table and
// not restated here.
// A SCALED length riding
// gui_scale like every authored height, so the clamp keeps pace with the
// lanes it is measured against. The ONE application point is the
// strip/waveform geometry owner (the two flex gaps / strip_row_rect /
// waveform_area, main.cpp); no consumer reads this accessor directly.
//
// THE PLUMBING IS gui_scale's: the configured AUTHORED value is file-scope
// state in render.cpp installed by set_max_waveform_height_px at the scale's
// own two application points — gui_main's startup read of the device config,
// beside set_gui_scale_percent and before the window exists, and the settings
// editor's `max_waveform_height=` commit (commit_device_setting, whose
// relayout is GuiInputHandler::apply_max_waveform_height). waveform_max_h_px
// is the ONE reader, and it answers INT_MAX — "unbounded", which the clamp's
// min passes straight through — for the key's 0.
void set_max_waveform_height_px(int authored_px);
int  waveform_max_h_px();

// Authored pixel geometry of THE BOTTOM ROW — THE UNIFIED BOTTOM ROW, the
// lane rows 8 and 9 merged into (architect-ruled 2026-08-12; the bottom
// strip's ONLY lane since the relayout's commit B): the monospace clock in
// its status panel and the state line on the ground beside it at the left
// pad (architect 2026-10-02 / 2026-10-03),
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
// THE ROW IS THE ICON ROW'S HEIGHT SINCE 2026-08-14 (architect, at his live
// test: "make sure bottom row is same height and metrics (padding, etc.) as
// main icon row"), and it READS that row's content accessor rather than
// restating its number — one source, so a retune of the icon row carries down
// here by construction. Its border is its own (below).
//
// So the content is the icon row's rule — the toolbar case with five Windows
// px of ground above and below it, 32 Windows px (architect 2026-10-02). (Its
// earlier boxes — row 8's kdenlive 26 px transport boxes, then the icon row's
// 32-laptop-px square — are git history.)
//
// THE CSS BOX MODEL, ONE TOP ROW: the content is the icon row's 32 and a
// one-Windows-px row of ground sits OUTSIDE it on top (91 device rows at
// 275 %, 45 at 138 %), on the WAVEFORM side — where row 8's border-top
// stood. NO LINE IS DRAWN THERE since
// 2026-10-02 (architect: nothing between the well and this row, the well's
// own bottom line being the seam), and the row is kept so nothing on the row
// moved. IT IS THIS ROW'S OWN LENGTH (kBottomRowBorderPx).
// bottom_row_content_h_px() is the ground the buttons and text sit on;
// bottom_row_h_px() is the lane the strip stack allocates. Rides
// gui_scale_factor() like every redesigned row.
inline constexpr int kBottomRowBorderPx = 1;
inline int bottom_row_border_h_px() {
    return scaled_px(kBottomRowBorderPx, 1);
}
inline int bottom_row_content_h_px() {
    return icon_row_content_h_px();
}
inline int bottom_row_h_px() {
    return bottom_row_content_h_px() + bottom_row_border_h_px();
}

// (THE STATUS BAR'S GEOMETRY IS DELETED — architect 2026-08-29, the evening of
// the day it landed. A tenth lane stood on the window's foot for one day, 1 +
// 31 + 1 authored px on the row-7 crop's measure, carrying the state strings
// in two cells; `kStatusBarContentPx` and its three accessors went with it,
// and the STATE TEXT is row 8's own cell right of the clock. The reasoning is
// at main.cpp's bottom lane table and in
// paint_bottom_row_buttons_and_clock (paint_handler.cpp).)

// THE REDESIGN'S SHARED TEXT SIZE, in device pixels — every text's size but
// the three named exceptions (the clock's, the ruler timestamps' and the
// tooltip's second line, each beside its painter's owner). AN EM OF 13
// WINDOWS PX (architect 2026-10-02): MS Sans Serif 8 pt's 13-px cell, whose
// capitals are 9 px tall — Roboto's cap of 0.711 em gives 9.2 at 13 px, and
// the hinted face measures 9 at 100 % — so the product's text stands at
// Windows' own proportion to its 16-px glyphs. 35.75 device px at 275 %
// (cap 25), 17.94 at 138 % (cap 13). A FONT SIZE IS NOT A GRID POINT: it is
// 13 × percent / 100 UNROUNDED, a double cairo takes as it is (scaled_px's
// rounding rule is for lengths). It lives
// here rather than in a painter's anonymous namespace because row 5's marker
// flags shape their labels inside render.cpp while the button rows shape
// theirs in paint_handler.cpp, and one design size cannot have two definitions.
//
// (It was 12 pt — 16 laptop px, measured off kdenlive's row-7 crop — until
// the unit's change; that measurement is git history.)
//
// THE TEXT IS NOT FLOORED (architect 2026-08-10, with the gui_scale floor's
// move to 50): it scales straight to 6.5 px at 50 %, while the structural
// lengths keep scaled_px's per-metric floors so no 1 px line rounds to 0.
inline constexpr double kRedesignFontSizePx = 13.0;   // Windows px
inline double redesign_font_size_px() {
    return kRedesignFontSizePx * gui_scale_factor();
}

// THE CLOCK'S SIZE — AN EXCEPTION to the shared size above (the ruler's
// timestamps, below, are the other)
// (architect 2026-08-14, at his live test: the bottom row's timestamp drops to
// 11 pt against the shared 12; since 2026-10-02 12 Windows px against the
// shared 13, the same 11/12). It stays MONOSPACE, which is the cell's own
// ruled face and unchanged (kClockShape, paint_handler.cpp, carries that
// ruling); only the size moved, and it rides gui_scale_factor() unrounded,
// as every font size does. 33 device px at 275 %, 16.56 at 138 %.
//
// THE NO-WIGGLE CELL RE-MEASURES ITSELF: the cell is a shaped widest-digit
// specimen and its memo keys on the SIZE it was measured at
// (clock_cell_width_px), so this smaller size simply produces a smaller cell,
// which the painter then re-centres in the lane. Nothing about the cell is
// authored in pixels, which is why the change is one constant.
inline constexpr double kClockFontSizePx = 12.0;   // Windows px
inline double clock_font_size_px() {
    return kClockFontSizePx * gui_scale_factor();
}

// THE RULER TIMESTAMPS' SIZE — the product's second exception to the shared
// size, the clock's precedent (architect 2026-10-02, the S4 mock judged on
// the tablet; 8 pt until the unit's change): THE SMALL FACE, 10 WINDOWS PX
// of Roboto through the one face owner (gui_select_font_face,
// GuiFontFamily::Sans), on the ruler lane only — 27.5 device px at 275 %,
// 13.8 at 138 %. The lane's height is derived from this face
// (ruler_lane_h_px), so the size is the one constant to move.
inline constexpr double kRulerLabelFontSizePx = 10.0;   // Windows px
inline double ruler_label_font_size_px() {
    return kRulerLabelFontSizePx * gui_scale_factor();
}

// THE MARKER FLAG's anatomy, measured off row_5_lane_3_marker_unselected.png
// (56x20 = a 1px left border plus a 55x20 fill box; the border's own record is
// at marker_flag_border_px) and confirmed against row_5_full.png, where the same
// box occupies rows 37..56 with the border at column 22 and the FILL — and the
// stem running on below it — at column 23.
//
// LEFT-ANCHORED, NOT CENTERED. The composite settles it: the stem stands on
// the box's leftmost column (its waveform_line_px() width running rightward
// from there, under the fill, never under the border), so a marker's box
// opens AT its frame and runs
// rightward, exactly as a kdenlive guide label does. The old flag was centered
// on its column (it was a symmetric shape with a tip); a text box is not
// symmetric and has no tip, so centering it would put the frame under the
// middle of a word.
//
// THE WIDTH IS DERIVED, NEVER FIXED: pad + shaped(truncated label) + pad, and
// THE TWO PADS ARE EQUAL (architect 2026-08-01, at the row-5 live test).
//
// THE CROP SAYS 2 AND 3, AND THE ARCHITECT OVERRODE IT. Shaping the crop's
// "Marker" offscreen through the same chokepoint at the same size (Liberation
// Sans 16px, the face then) gives an advance of 49.797px with the first
// glyph's left side bearing at exactly 1.00; against the 55px box that pins
// the left pad at 2 (2 + 1.00 = column 3, where the crop's ink core starts)
// and leaves 3 on the right (Roboto, 2026-10-02: 49.953px on the same 1.00
// bearing, the same two pins). Reproduced faithfully, that extra right pixel READS as slack rather
// than as padding — so the box goes symmetric at 2 and comes out 54 wide where
// kdenlive's is 55. A measured pixel deliberately given up, recorded here so
// the next reader does not "fix" it back.
// TWO WINDOWS PX EACH SINCE THE UNIT'S CHANGE (architect 2026-10-02): the
// laptop pixel's 2 converts to 1.45, and the nearer 1 would have halved the
// laptop's pad to one device px against the box's edge; 2 is 6 device px at
// 275 % (4 before) and 3 at 138 % (2 before).
inline constexpr int kMarkerFlagPadRightPx = 2;
inline constexpr int kMarkerFlagPadLeftPx  = 2;
inline int marker_flag_pad_left_px() {
    return scaled_px(kMarkerFlagPadLeftPx, 1);
}
inline int marker_flag_pad_right_px() {
    return scaled_px(kMarkerFlagPadRightPx, 1);
}
// THE BOX'S TOP AND BOTTOM OUTLINE ROWS, one Windows px each (architect
// 2026-10-03, the flat flag; the colours at render.h's palette block): the
// outline's horizontals, inside the box's rows, so the box keeps its height.
// The LEFT side and the run's closing RIGHT column are the border below, in
// the same outline colour.
inline constexpr int kMarkerFlagEdgePx = 1;
inline int marker_flag_edge_h_px() {
    return scaled_px(kMarkerFlagEdgePx, 1);
}
// THE 1px LEFT BORDER (architect 2026-08-02), full box height, in the flag
// outline's colour (the theme's DkShadow, render.h's palette block). The
// geometry clause that makes it a BORDER and not a wider
// box is his and it is explicit: THE STEM STAYS ON THE FILL'S LEFTMOST COLUMN,
// so the border sits one column to the LEFT of the marker's own frame column
// and never over it. Nothing inside moved — the fill's origin is still the
// frame column, its interior width is still pad + shaped + pad, and the label's
// pen is still measured from the fill's origin. What widened is THE BOX, and
// only leftward: the painter draws this column and the published hit rect
// starts on it, so a press on the border is a press on the flag.
//
// AT THE VIEWPORT'S FIRST COLUMN THE BORDER IS SIMPLY CLIPPED AWAY. Both the
// marker lane rect and the waveform area begin at window x = 0, so a flag there
// paints its fill at 0 and its border at -1, off the surface, where cairo drops
// it. That is the honest answer rather than a defect: pushing the fill right to
// make room would move the flag off the frame column it names and off its own
// stem, and the column alignment is the authored fact where the border is
// decoration.
//
// AT THE LAST COLUMN THE BORDER IS WHAT SHOWS (architect 2026-09-26). The
// marker lane is clipped to the waveform's columns [0, w) — every flag box,
// flag-editor box and hit rect (clip_to_waveform_columns, render.cpp) — and
// the flag iterator admits a flag whose box, THIS BORDER INCLUDED, reaches
// into those columns. So a marker at grid point w (one past the last column)
// paints this border ALONE on the last column(s), [w - border, w): one column
// at the laptop's 138 %, three at the tablet's 275 %. No fill, no text and
// no stem (the stem is
// gated to [0, w)); its hit rect is that strip, so the border is clickable
// as painted; and its open editor paints the very same columns, the field's
// border standing where the resting flag's does.
//
// THE RUN CLOSES WITH ONE MORE SUCH COLUMN ON ITS RIGHT (architect 2026-09-25,
// reversing "the box has no right border", which stood from 2026-08-02): a
// short later flag standing over a long earlier one let the earlier tail run
// on out of the later fill with nothing between them. The column stands just
// past the fill of the run's RIGHTMOST box — the flag box on a cell-less run,
// the upper cell where cells paint, the open field or its riding upper cell
// under a marker-lane editor — in that box's own face.border, and it is inside
// the published rect. Interior seams stay ONE column: the flag box's right
// side against the lower cell is that cell's own left seam, never a closing
// column plus a seam. It needs no clip rule of its own: past the waveform's
// last column it is cut off like the fill it follows.
inline constexpr int kMarkerFlagBorderPx = 1;
inline int marker_flag_border_px() {
    return scaled_px(kMarkerFlagBorderPx, 1);
}
// The label BASELINE, in device rows under the BOX's top (marker_flag_box_band,
// never the lane's): the box's edge band, then the face's ceiled ascent — the
// label is a LINE under the band (architect 2026-10-02, the AC / AD sets'
// flag_seat; it was the kdenlive crop's authored row 16 until the box became
// the face's own). Defined in paint_handler.cpp with the box's height (the
// rule at marker_lane_h_px's block above).
int marker_flag_baseline_px();
// THE RELIEF LINE — ONE WINDOWS PX, the width of every line of a raised,
// sunken, status or etched edge in the chrome, and of one cell of the checked
// dither (architect 2026-10-02: Windows' DrawEdge draws one-pixel lines, two
// to an edge; the grammar is at the palette head): 3 device px at the
// tablet's 275 %, 1 at the laptop's 138 %, floored at 1 so it never vanishes
// at 50 %. The relief helpers (paint_relief_soft_raised and its siblings)
// paint every line at it.
inline constexpr int kReliefLinePx = 1;
inline int relief_line_px() {
    return scaled_px(kReliefLinePx, 1);
}
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
// THE WELL'S BORDER, taken FROM the waveform area at its top and its bottom:
// the PLAIN SUNKEN edge's TWO relief lines a side (the colours and the order
// at the row-6 palette block), so 6 device rows on the tablet, 2 on the
// laptop, and still two lines at 50 %.
inline int waveform_border_px() {
    return 2 * relief_line_px();
}
// THE WAVEFORM'S LINE WIDTH (architect 2026-09-27, "scale all, including the
// ruler ticks and the playhead head"): every vertical LINE on the waveform and
// the ruler scales with gui_scale — one Windows px: 1 up to 149 % (the floor
// holding 50 %; the laptop's 138 %), 2 from 150 % through 250 % (banker's
// rounding takes 2.5 to 2), 3 above that (the tablet's 275 %) and 4 at the
// 350 % ceiling.
// ITS READERS, grepped at the ruling — the one inventory of the class, each
// an ALIASED INTEGER RECT [col, col + t) whose left edge is the item's own
// column and which is clipped to the waveform's columns [0, w): the marker
// stems in both columns and the `h` diff lane (paint_marker_stems), the
// playhead's waveform segment and the scanner (render_playhead), the
// playhead's run through the marker lane and the ruler ticks
// (paint_ruler_row), the zoom anchor stem (render_strip_anchor_stem), and the
// phase-reset lead-in ring's four sides (t thick, its left side on the
// stem's own columns); and the lit plate's inner OUTLINE, an erosion at
// distance t (outline_bar, render.cpp, t riding the plate job and its
// fingerprint). The playhead HEAD widens each row by t − 1 on the right so it
// stays centred on the stem (paint_ruler_row). NOT a reader: the plate
// column, one device pixel by rule — resolution, not size — with its bar's
// one-row floor.
inline int waveform_line_px() {
    return scaled_px(1, 1);
}
// THE LINE'S ONE PAINT: columns [col, col + waveform_line_px()) of a strip
// whose columns are [0, area_w), at window x `area_x`, rows [y0, y1), as ONE
// aliased integer rect in the caller's source colour. THE TWO EDGE RULES live
// here: a line is GATED ON ITS OWN COLUMN (col outside [0, area_w) paints
// nothing, so a marker at column −1 shows no pixel at 0 — a line belongs to
// its column), and its width is CLIPPED at the right edge, so a line at
// w − 1 paints that one column and nothing reaches a non-multiple-of-16
// window's leftover strip.
inline void fill_waveform_line(cairo_t* cr, int area_x, int area_w, int col,
                               double y0, double y1) {
    if (col < 0 || col >= area_w) return;
    const int t   = waveform_line_px();
    const int end = (col + t < area_w) ? col + t : area_w;
    cairo_rectangle(cr, static_cast<double>(area_x + col), y0,
                    static_cast<double>(end - col), y1 - y0);
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

// An UPPER BOUND on a flag box's painted width, used only to decide how far
// LEFT of the viewport a marker may sit and still reach into it (flags run
// rightward, so the left cull needs a width and the right cull only the left
// border's reach, which the iterator reads itself). No
// ASCII glyph in a sans face advances more than one em, so glyphs * em + the
// two pads bounds every box the truncation can produce. A bound, not a size:
// nothing is laid out against it.
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
// `iteration_on` ADDS THE TWO BOUND CELLS, and it must: an eligible flag runs
// two cells further right than its label predicts (each a seam column, two
// pads and kIterCellGlyphs glyphs), and a bound that no longer bounds would
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
// THE FLAG'S OWN LEFT BORDER IS DELIBERATELY NOT IN IT. This bound answers
// "how far RIGHT of its frame column can a box reach", and that border grows
// the box the other way — leftward, away from the viewport — so adding it
// would only over-admit culled markers by one column and never save a visible
// one. The cells' seam columns ARE in it: they stand to the right. So is the
// run's CLOSING column (2026-09-25), charged ONCE on the flag term, since a
// run has exactly one whether or not cells follow.
inline double marker_flag_max_width_px(bool iteration_on) {
    const size_t glyphs = 4 +                                  // `N.NN` base
                          5 * kMaxTempoDeviationTerms +        // `+0.01` each
                          1 + kMarkerFlagScaleGlyphs +         // `*N.NN`
                          kMarkerLabelTruncationMarker.size();
    const double pads = static_cast<double>(marker_flag_pad_left_px() +
                                            marker_flag_pad_right_px());
    const double flag = static_cast<double>(glyphs) * redesign_font_size_px() +
                        pads +
                        static_cast<double>(marker_flag_border_px());  // closing
    if (!iteration_on) return flag;
    const double cell = static_cast<double>(kIterCellGlyphs) *
                            redesign_font_size_px() +
                        pads + static_cast<double>(marker_flag_border_px());
    return flag + 2.0 * cell;
}

// THE TRIM CAPS ARE WINDOWS' SCROLL-BAR ARROW BUTTONS (architect 2026-10-03:
// "the arrows are truthful: the left arrow is the begin, the right one the
// end"): a 16 x 16 Windows-px PLAIN RAISED button at each end of the thumb —
// 16 wide here, the one owner of the width, and the trim lane's full height
// (kTrimLaneHeightPx, the same 16) — the ground under the plain raised edge
// and an arrow glyph in the label, no hover face, and from the press on it to
// the gesture's end the PRESSED scroll arrow (render_trim_flags; the palette
// block's trim paragraph).
// THE BEGIN BUTTON'S LEFT EDGE STANDS ON THE BEGIN COLUMN and its arrow
// points LEFT; THE END BUTTON'S RIGHT EDGE STANDS ON THE END COLUMN and its
// arrow points RIGHT; the thumb's BODY runs between the two buttons' inner
// edges. NARROW: when both bounds are in view and the window's drawn width
// (end column − begin column + 1) is under two buttons, the BEGIN BUTTON
// STAYS ANCHORED on its column and the END BUTTON STANDS IMMEDIATELY RIGHT OF
// IT, edge to edge (architect 2026-10-03), so the right arrow alone overruns
// its column — by 2 x 16 Windows px − the width, in device px 2 x the button
// − the width — and the body is empty; from two buttons' width up each
// button sits on its own column (the rect owner is trim_endcap_rect). OFF
// SCREEN: a bound out of view paints no button and the body runs past that
// window edge by its edge's thickness — a cap off screen is simply off
// screen. THE BUTTON IS THE TARGET: its painted rect is the hit band, with no
// tolerance added. 44 x 44 device px at 275 %, 22 x 22 at 138 %, 8 x 8 at
// 50 %, floored at 3 as the lane is so the two stay square.
inline constexpr int kTrimArrowButtonPx = 16;
inline int trim_arrow_button_w_px() {
    return scaled_px(kTrimArrowButtonPx, 3);
}
// THE ARROW GLYPH, Windows' scroll arrow: FOUR COLUMNS whose heights, from
// the tip, are 1, 3, 5 and 7 Windows rows, each centred on the glyph's middle
// row — a 4 x 7 triangle. Painted as INTEGER RECTANGLES, one per column
// (aliased by construction, the playhead head's precedent at
// kPlayheadHeadHalf: never a rescaled icon, never a path fill), each column
// one unit u = scaled_px(1, 1) wide and its rows u tall apiece — a composite
// of rounded parts — so the glyph is 4u x 7u device px; it is CENTRED IN THE
// BUTTON in device px, an odd difference flooring toward the top-left (the
// extra pixel right of or below the glyph). At 275 % (u 3): 12 x 21 in the
// 44 x 44 case, at (+16, +11), the columns 3 x 3, 3 x 9, 3 x 15 and 3 x 21
// with tops +20, +17, +14 and +11; at 138 % (u 1): 4 x 7 in 22 x 22 at
// (+9, +7).
inline constexpr int kTrimArrowGlyphCols = 4;
inline constexpr int kTrimArrowGlyphRows[kTrimArrowGlyphCols] = {1, 3, 5, 7};

// THE PLAYHEAD HEAD, ALIASED: 8 WINDOWS PX TALL, 13 WIDE (architect
// 2026-10-02, the unit's change; the planner's lean taken: 8 rows paint 22
// device rows at 275 % — the judged picture's head — and 11 at 138 %, the
// laptop's head before the change). Its silhouette is a per-row HALF-WIDTH
// table, not a formula, seated tip-down on the marker lane's top, its top
// rows overlapping the digits' lowest ink rows at the playhead's column (the
// overlap rule is kRulerBaselineToMarkerPx's). THE 8-ROW TABLE IS THE
// 11-ROW kdenlive head (row_5_lane_3_playhead.png's 19 x 12 with its widest
// row dropped: halves 8 7 6 6 5 4 4 3 2 1 1 laptop px) RE-SAMPLED at the
// tablet: each Windows row takes the mean half of the device rows it covers
// at 275 %, re-authored in Windows px — 6 5 4 3 3 2 1 1, keeping the
// original's doubled rows as steps. Painting it as integer rectangles keeps it
// hard-edged at every scale, which a path fill would not. The painted width at
// any scale is the table's own arithmetic through playhead_head_half_px below
// (2 x 16 + the stem's 3: 35 at 275 %; 2 x 8 + 1: 17 at 138 %).
inline constexpr int kPlayheadHeadHeightPx = 8;
inline constexpr int kPlayheadHeadHalf[kPlayheadHeadHeightPx] = {
    6, 5, 4, 3, 3, 2, 1, 1
};
// THE HEAD'S HEIGHT IN DEVICE ROWS, the ONE expression of it for the
// painter's row loop (paint_ruler_row). NO FLOOR: 8 Windows rows reach 4 at
// the schema's own bottom (gui_scale 50), and only a factor below 1/16 could
// empty the loop — outside the vocabulary entirely. The per-row HALF-WIDTH is
// where the floor lives (playhead_head_half_px below).
inline int playhead_head_h_px() {
    return scaled_px(kPlayheadHeadHeightPx);
}
// ONE DEVICE ROW'S HALF-WIDTH, the ONE expression the head's painter
// (paint_ruler_row, its one reader) fills its rows with. A device row picks
// its SOURCE row by the inverse scale (so the transcribed shape survives
// scaling as steps, not slopes) and that row's authored half
// takes the tree's one conversion. `s` is the caller's gui_scale_factor(); it
// is passed because the caller already holds it and only the row inverse needs
// it, the width itself going through scaled_px like every other length.
//
// THE FLOOR OF 1 (architect 2026-08-10, with the gui_scale floor 100->50): the
// table's last two rows are 1 Windows px, which rounds to 0 at s = 0.5, and a
// half of 0 is a 1px row — the head's tip would collapse onto the stem and
// stop reading as a tip at all. Floored, the bottom row is 3 px wide there. A
// FLOOR, NOT A PIN: at 100% and above every scaled half is already >= 1, so
// nothing above the baseline moves and the tips keep scaling.
//
// THE HEAD IS CENTRED ON THE STEM AT EVERY SCALE (architect 2026-09-27): the
// stem is waveform_line_px() = t columns wide, [col, col + t), so each row
// takes the stem's parity, [col − half, col + half + t − 1], 2·half + t wide
// — odd at the laptop's t = 1 and the tablet's t = 3 — integer and aliased,
// with the same `half` on each side of the stem's own columns.
//
// THE ROW INVERSE TRUNCATES, deliberately — the one conversion here off the
// project's nearbyint rule. `device_row / s` is a POSITION INSIDE the authored
// table, and source row k covers the device rows [k*s, (k+1)*s), so the row
// that CONTAINS the position is the floor of it: the same containment reading
// a screen pixel takes. Rounding would pull the upper half of every device
// band into the next source row and shift the transcribed shape by half a row
// at fractional scales. Every caller passes a row index inside the head band
// and s > 0 by gui_scale's own bracket, so the quotient is non-negative and
// the cast's toward-zero truncation IS that floor; the two clamps below are
// the backstop that holds the index inside the table at either end.
inline int playhead_head_half_px(int device_row, double s) {
    int src = static_cast<int>(static_cast<double>(device_row) / s);
    if (src > kPlayheadHeadHeightPx - 1) src = kPlayheadHeadHeightPx - 1;
    if (src < 0) src = 0;
    return scaled_px(kPlayheadHeadHalf[src], 1);
}

// THE HOVER TOOLTIP'S SHARED NUMBERS — a DAMAGE BOUND on its box height, the
// five durations of its life and the slop of its wait. They live out here,
// rather than with the rest of the tooltip's anatomy in paint_handler.cpp,
// because both the RUN LOOP and the input side read them: the timer owner
// (GuiInputHandler::tick_tooltip) runs on the tick and damages a band beside
// the owner's strip to cover whatever the box overhangs, and the one wait
// writer (note_tooltip_hover) reads the slop and the two wake-up delays.
//
// THE HEIGHT HERE IS A BOUND, NOT THE HEIGHT. The painter derives the real box
// from the FACE'S OWN EXTENTS at both type sizes (one line, or 13 over 11
// Windows px), so the box follows the font instead of a literal that could
// drift from it; the run loop only needs to know it can never exceed this.
// 44 Windows px (the laptop pixel's 60 re-authored, architect 2026-10-02)
// clears the two-line form — 42 at 100 % and 110 device rows against 121 at
// 275 % in Roboto — with room for a font whose metrics run larger.
inline constexpr int     kTooltipDamageHeightPx = 44;
inline int tooltip_damage_h_px() {
    return scaled_px(kTooltipDamageHeightPx, 5);
}

// THE TIMING IS QT'S QToolTip MODEL (architect 2026-09-29), the one kdenlive's
// toolbar runs on this laptop — Breeze 6.7.5, KStyle and qt6ct override none
// of it — taken at Qt 6.11.2's own numbers. The model is stated once, at
// AppState::RedesignTooltip; these are its constants, hard-coded, no keys.
// Durations ride no scale.
//
// THE WAKE-UP: 700 ms of REST on a tooltip-bearing button before its hint
// shows — SH_ToolTip_WakeUpDelay (qcommonstyle.cpp), restarted by every
// motion past the slop below. Its own number: no hold and no beat reads it,
// and it reads neither (the chrome shift long press is timed by
// kHoldDelayMs alone and has no visual announcement).
inline constexpr int64_t kTooltipWakeUpMs = 700;
// THE AWAKE WAKE-UP: 20 ms instead, while the product is awake (below) —
// QApplication::notify's `toolTipFallAsleep.isActive() ? 20 : wakeDelay`
// (qapplication.cpp) — which is what makes a neighbouring button's hint
// follow at once and take the standing box over in place.
inline constexpr int64_t kTooltipAwakeWakeUpMs = 20;
// THE AWAKE WINDOW: 2000 ms from every show — SH_ToolTip_FallAsleepDelay
// (qcommonstyle.cpp), restarted by each show or re-show
// (QApplication::event's ToolTip arm). Rest on one hint longer and the
// product falls asleep: the next button waits the full wake-up again.
inline constexpr int64_t kTooltipFallAsleepMs = 2000;
// THE HIDE GRACE: a SOFT end leaves the box up this long — QTipLabel::hideTip
// (qtooltip.cpp), started once and never restarted by a second soft end —
// and the pointer coming back to the box's own button, or a neighbour's hint
// taking the box over, cancels it (QTipLabel::restartExpireTimer's
// hideTimer.stop()).
inline constexpr int64_t kTooltipHideGraceMs = 300;
// THE EXPIRY: a box standing this long after its last show or re-show goes
// down — QTipLabel::restartExpireTimer's 10 s (qtooltip.cpp). Qt adds 40 ms
// per character past 100; the one hint that long (the walk's two lines, 108
// characters) would stand 0.32 s longer there, and that term is not carried.
inline constexpr int64_t kTooltipExpireMs = 10000;

// THE HOVER SLOP (architect 2026-09-29: the wait counts from STILLNESS WITH
// HYSTERESIS): the wait re-anchors, restarting, only when the pointer moves
// MORE than this from where it was anchored on EITHER axis, so a hovering
// pen's jitter cannot starve it. Qt has no such tolerance — QApplication
// restarts the wake-up on every motion and its Android plugin forwards the
// pen's hover as plain moves — so the number is ANDROID'S OWN for the same
// job: AOSP View's hover tooltip ignores a HOVER_MOVE within
// ViewConfiguration.getScaledHoverSlop() of its anchor on both axes
// (View.TooltipInfo.updateAnchorPos, "filters out the jitter which is
// typical for such input sources as stylus"), and that slop is
// `config_viewConfigurationHoverSlop` = 4dp (core/res/values/config.xml),
// half the platform's 8dp touch slop. It is taken as 3 WINDOWS PX through
// scaled_px (4 laptop px until the unit's change, architect 2026-10-02):
// 8 device px, exactly 4dp, on the tablet at its 275 % under the 320 density
// it runs at, and half the drag gate (kDragMovedThresholdPx 6, app_state.h)
// as Android's is half its touch slop. Floor 1, so a small scale never
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
// shots; the laptop pixel's 2 would have been 1).
inline constexpr int kPopupItemHeightPx = 17;
inline constexpr int kPopupSepMarginYPx = 3;   // above and below the separator
// The item block's own margin inside the frame, top AND bottom (the
// horizontal one is the painter's kPopupItemInsetPx, the same one px).
inline constexpr int kPopupItemMarginYPx = 1;
// THE FRAME IS THE PLAIN RAISED EDGE, two relief lines a side, so its
// thickness is relief_line_px's twice — read, not a second number.
inline int popup_border_px() {
    return 2 * relief_line_px();
}
inline int popup_item_h_px() {
    return scaled_px(kPopupItemHeightPx, 5);
}
inline int popup_sep_margin_y_px() {
    return scaled_px(kPopupSepMarginYPx, 0);
}
// A separator's whole block: its margin, the etched pair, its margin.
inline int popup_sep_block_px() {
    return 2 * popup_sep_margin_y_px() + 2 * relief_line_px();
}
inline int popup_item_margin_y_px() {
    return scaled_px(kPopupItemMarginYPx, 1);
}


// Waveform-internal top/bottom inset, in pixels. The drawn waveform samples
// are confined to [area.y + waveform_inset_px(), area.y + area.h -
// waveform_inset_px()] so the waveform is symmetric about its area center and
// the marker and playhead stems have a clean stem-only band at the top before
// the samples begin. The symmetric margin is the whole of the purpose.
//
// PROVENANCE (2026-08-02): this used to BE the tip-down triangle's mask height,
// returned through playhead_triangle_h_px(), which is deleted with the
// silhouette — so the inset owns its derivation outright now, and THE VALUE IS
// KEPT EXACT: the same authored unit, the same std::nearbyint, the same floor
// — the unit re-authored as 6 Windows px since 2026-10-02, 16 device px at
// 275 % (16 at the tablet's 200 % before) and 8 at 138 %. The floor of 2 was
// the triangle's own ("always a tip row below a top row") and survives only to
// hold the value; it cannot fire while gui_scale rests in [50, 350] (6 px
// reaches 2 only below 25 %).
inline int waveform_inset_px() {
    return scaled_px(kPlayheadUnitPx, 2);
}

// THE CHANNEL SPLIT ROW — where the two channel bands meet, area-local (add the
// area's y for a window row). The plate renderer
// (render_waveform_to_cache_surface) is its ONE caller: it lays the top band
// down to this row and the bottom band from it. Nothing paints on the row —
// the two channels meet flush.
//
// The band is the area minus the symmetric inset at each end; each channel
// takes the halved-and-floored height, so at an odd band height the spare row
// falls at the BOTTOM of the drawing band, inside the inset, where nothing
// draws (the reasoning is at the renderer). Returns -1 when the inset leaves no
// band at all — the caller's own refusal case.
inline int waveform_channel_split_row(int area_h, int inset_px) {
    const int inset_h = area_h - 2 * inset_px;
    if (inset_h <= 0) return -1;
    return inset_px + inset_h / 2;
}

// Half-width (px) of the playhead COLUMN's reach: a playhead at column c owns
// [c - playhead_half_px(), c + playhead_half_px()]. Bounds the playhead's
// off-screen cull and its narrow invalidation strip — the single definition
// shared by render.cpp (cull) and main.cpp (invalidation). 15 at 275 %, 7 at
// 138 %. It covers the scanner's waveform_line_px()-wide line [c, c + t) at
// every gui_scale: t − 1 is 0 to 3 across [50, 350] % while this reach is 2
// to 20.
//
// PROVENANCE (2026-08-02): it was the horizontal footprint of the tip-down
// triangle (the mask was 2H-1 wide and centered, so H-1 either side), read
// through playhead_triangle_h_px(); the silhouette is deleted and this owns its
// derivation outright, with THE VALUE KEPT EXACT — the identical arithmetic the
// inset above spells, less one, off the same authored unit, so every pixel and
// every damage rect is identical at every gui_scale. It reads that unit
// directly rather than the inset: the two are equal by inheritance, not by
// requirement, and neither owns the other.
//
// RECORDED MISMATCH, live and deliberate: the cursor's aliased HEAD on the
// ruler lane's bottom rows is WIDER than this reach at every scale. The head's
// widest row is 2 * playhead_head_half_px(0, s) + waveform_line_px() off
// kPlayheadHeadHalf[0] = 6 (2026-10-02) — 13 px at 100 %, 7 at 50 %, 17 at
// 138 %, 35 at 275 % and 46 at the 350 % ceiling — against this ± 15-at-275 %
// reach, which rides a
// different authored unit. Both scale, and neither is a function of the
// other, so the gap is a fact at every scale rather than a 100%-only
// observation. It is harmless as
// the damage rule stands — narrow damage is reserved for the two per-frame
// SCANNER sites, and the scanner is waveform-only and draws no head, while
// every discrete CURSOR move takes full waveform-area damage (the rule and the
// per-site table are at playhead_pixel_x, app_state.h). Widening it to the head
// is a retune, the architect's call, not a cleanup's.
inline int playhead_half_px() {
    return scaled_px(kPlayheadUnitPx, 2) - 1;
}

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


// Screen-coord rect of one rendered flag, keyed back to its marker index.
// Emitted in the same order flags appear left-to-right. It is the WHOLE PAINTED
// BOX — the 1px left border included, so its x sits one column left of the
// marker's frame column (marker_flag_border_px), and the run's closing right
// column included where the producer painted one (2026-09-25) — because this
// stash has always been the painted extent and a click on the border is a
// click on the flag. IT IS CLIPPED TO THE WAVEFORM'S COLUMNS [0, w) as the
// pixels are (architect 2026-09-26, clip_hit_rect_to_waveform_columns,
// render.cpp): a flag cut off at the last column claims its visible part, and
// a marker at grid point w claims its left-border strip alone; the two cell
// boundaries stay where the painter put them.
//
// IT SPANS THE TWO ITERATION BOUND CELLS TOO where they
// paint: each is the flag continued, so all of it is ordinary flag surface for
// press, drag and select and the rect covers the whole run. The two
// boundaries are the PAINTER'S own numbers, published rather than re-derived,
// because a second shaping pass could disagree with the pixels: the window x
// where the flag box ends and the LOWER cell's seam begins
// (`iter_lower_boundary_x`) and where the lower cell ends and the UPPER cell's
// seam begins (`iter_upper_boundary_x`). They are non-decreasing, and
// each collapses onto the rect's own right edge when its box did not paint (an
// absent box is always the run's tail) — a cell-less flag publishes both cell
// boundaries AT the rect's right edge, past its closing column — so
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
struct FlagHitRect {
    int    marker_index = -1;
    double x            = 0.0;
    double y            = 0.0;
    double w            = 0.0;
    double h            = 0.0;
    double iter_lower_boundary_x = 0.0;
    double iter_upper_boundary_x = 0.0;
};

// All rendering helpers take a Cairo context and pixel-space rectangles; they
// have no X11 or event-loop dependencies.

// The two ground fills, one per surface class: render_background erases
// CHROME in the theme's ground, render_canvas erases the WAVEFORM AREA in the
// `waveform_canvas` key (palette()). on_redraw calls the first
// over the whole exposed rect, then the second over the exposed part of the
// waveform area, so the canvas wins exactly the pixels the plate, the ground
// recolours, the playheads and the marker stems paint on — cold frames (no
// plate yet) included.
//
// render_canvas ALSO owns THE WELL — the waveform area's two-line border at
// its top and its bottom (waveform_border_px; the colours at the row-6
// palette block), taken FROM the area, not added to it, so no lane or column
// arithmetic moves and the CONTENT band shrinks by those rows at each end
// (waveform_content_rect below). Top and bottom only; the area's sides are the
// window edges (and the inert right gutter), which need no rule.
void render_background(cairo_t* cr, int x, int y, int w, int h);
void render_canvas(cairo_t* cr, int x, int y, int w, int h);

// THE RELIEF HELPERS — the chrome's one painter family for the Windows-95
// edge grammar (architect 2026-10-02; the grammar and the family table at the
// palette head). Every line is relief_line_px() wide and is an integer rect
// of cells, never a stroke. A RING is drawn ON the rect's outermost cells:
// its top and left in `top_left`, then its bottom and right in
// `bottom_right`, the second pair painted last so it owns the top-right and
// bottom-left corner pixels; a TWO-LINE EDGE is its outer ring on the rect
// and its inner ring on the rect inset one line (Windows' DrawEdge).
//   paint_relief_frame  — one ring, any two colours (the one owner every
//                         helper below calls).
//   paint_relief_soft_raised  — SOFT RAISED: a toolbar button at rest.
//   paint_relief_soft_sunken  — SOFT SUNKEN: a toolbar button checked or
//                         pressed.
//   paint_relief_plain_raised — PLAIN RAISED: a push button, the scroll-bar
//                         thumb (the trim lane's), the scrub's thumb, a
//                         menu's and a dropdown's frame, a raised panel.
//   paint_relief_plain_sunken — PLAIN SUNKEN: a field, the scrub's channel
//                         (the well is render_canvas's own fill of the same
//                         lines, full width).
//   paint_relief_status_sunken — STATUS SUNKEN, ONE ring: a status-bar panel.
//   paint_relief_line_frame — one colour all round: the dialog's
//                         default-button frame and the list's focus frame.
//   (The INFO FRAME — the tooltip's and the cards' — is paint_relief_frame
//   itself, 3DLight / DkShadow, at paint_popup_chrome.)
//   paint_relief_etched_hline — an ETCHED line: a Shadow line on rows
//                         [y, y + lw) and a Hilight line under it, columns
//                         [x, x + w) (the dropdown's separator). (Its
//                         vertical twin retired 2026-10-02 with the render
//                         player's separators, its last callers; the ruler's
//                         etched ticks are painted in place, paint_ruler_row.)
// (paint_relief_raised / paint_relief_sunken, the one-line pair of the thin
// design, are retired, architect 2026-10-02: every caller names its family.)
// None of them fills the face: a caller fills first and frames after.
void paint_relief_frame(cairo_t* cr, const GuiRect& r, GuiColor top_left,
                        GuiColor bottom_right);
void paint_relief_soft_raised(cairo_t* cr, const GuiRect& r);
void paint_relief_soft_sunken(cairo_t* cr, const GuiRect& r);
void paint_relief_plain_raised(cairo_t* cr, const GuiRect& r);
void paint_relief_plain_sunken(cairo_t* cr, const GuiRect& r);
void paint_relief_status_sunken(cairo_t* cr, const GuiRect& r);
void paint_relief_line_frame(cairo_t* cr, const GuiRect& r, GuiColor c);
void paint_relief_etched_hline(cairo_t* cr, int x, int y, int w);
// One flat cell rect in `c` — the face fill every relief caller lays first.
void paint_cell_rect(cairo_t* cr, const GuiRect& r, GuiColor c);
// THE CHECKED DITHER (architect 2026-10-02, Windows' checked toolbar button
// and its scroll-bar track): the cells of `r` lit in `lit` over whatever the
// caller filled first, in square cells relief_line_px() on a side, the cell
// at (phase_x, phase_y) lit and its neighbours alternating — a cell is lit
// when its column index plus its row index, counted from the phase, is even —
// so two surfaces sharing a phase dither alike. Only the cells inside the
// current clip are emitted (a dither is thousands of cells), each an integer
// rect through the palette's chokepoint.
void paint_checker_rect(cairo_t* cr, const GuiRect& r, int phase_x,
                        int phase_y, GuiColor lit);

// THE DISABLED EMBOSS, THE WORD HALF (architect 2026-10-03, Windows' DrawState
// DSS_DISABLED; the rule at the palette block): `run` painted in the theme's
// emboss_light one Windows px (relief_line_px) right and down, then in its
// Shadow at (x, baseline) over it — every disabled word in the product goes
// through here, as every disabled glyph goes through icons::draw_engraved.
// `cr` carries the run's scaled font (shape with the font you paint with).
void show_embossed_run(cairo_t* cr, const text_shape::ShapedRun& run,
                       double x, double baseline);


// The waveform area's CONTENT band — THE CANVAS: the area minus the well's two
// lines at its top and its bottom (waveform_border_px). Every pass that fills
// a BAND inside the area clips to this — the plate blit — and the SCANNER, the moving
// playback line, which belongs to the picture alone, spans it (render_playhead).
// THE PHASE-RESET OVERLAY RING alone reads the full area: its horizontals ride
// the area's OUTERMOST rows deliberately (the ruling is at
// paint_phase_reset_overlay_ring). Degenerate areas (too short to carry both
// borders) pass through unshrunk rather than inverting.
inline GuiRect waveform_content_rect(GuiRect area) {
    const int b = waveform_border_px();
    if (area.h <= 2 * b) return area;
    return GuiRect{area.x, area.y + b, area.w, area.h - 2 * b};
}
// THE STEMS' BAND (architect 2026-10-02, the stems run continuous): the area
// from its TOP — through the well's top lines — to the canvas's foot. A
// stem crosses the top lines because it continues from the lane above, flag
// to canvas, and stops at the bottom lines, where nothing below continues it.
// Its readers: the marker stems in both columns and the `h` diff lane
// (paint_marker_stems), the playhead's waveform segment (render_playhead) and
// the strip-drag anchor stem (render_strip_anchor_stem). Degenerate areas pass
// through whole, as the canvas's do.
inline GuiRect waveform_stem_band(GuiRect area) {
    const int b = waveform_border_px();
    if (area.h <= 2 * b) return area;
    return GuiRect{area.x, area.y, area.w, area.h - b};
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
// one word owner for the plate's writer (render_waveform).
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
// `waveform_ink` and `waveform_outline` keys — the worker reads no live
// state): the plate paints in the ink with the lamp dark, and lit both bars in
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
// the job's snapshot): 1 on the laptop at 138 %, 3 on the tablet at 275 % —
// the outline is a LINE
// and scales with gui_scale like the stems, while the plate column stays one
// device pixel. A pixel of the inner's shape (the inner bars of all columns)
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

// Draws a waveform_line_px()-wide vertical LINE down `area` at the column
// nearest `playhead_pixel_x` (offset from area.x), [col, col + t), over the
// rows `band` picks (architect 2026-10-02): the CURSOR's stem is a stem and
// runs continuous from the marker lane's run into the canvas
// (PlayheadRows::Stem, waveform_stem_band), the SCANNER is the picture's own
// line and spans the canvas alone (PlayheadRows::Canvas,
// waveform_content_rect); in one solid
// `color` end to end, painted straight over whatever it crosses — waveform
// ink included. No-op if outside; the line is gated on its own column and
// clipped at the right edge (fill_waveform_line), so it never leaks into an
// adjacent region.
//
// THE LINE IS THE WHOLE FUNCTION (2026-08-02). It used to carry a
// `draw_triangle` flag and a `triangle_lane` rect for an inverted-triangle
// indicator stamped from a cached mask above the stem: row 5 replaced the
// cursor's tip-down triangle with the aliased head that paint_ruler_row
// draws (with the column's marker-lane run beside it, the ruling at that
// block) — and every caller had passed `false` ever since. The branch,
// the mask and the lane rect are all deleted; both callers were already
// line-only, so no painted pixel moves. (The complementary triangle-only form
// was retired with the selected-marker focus triangle when the singleton's
// focus became an always-on stem, architect 2026-07-25, so there was never a
// draw_line flag either — the line has always been unconditional.)
//
// The former two-tone form (an `ink_plate` parameter carrying the displayed
// plate, whose alpha masked a ground-colored overdraw wherever the column
// crossed an opaque sample) is retired too: architect 2026-07-26, the notch
// retired with the polarity inversion — the contrast problem it patched is
// solved by the scheme, so that parameter went with it.
enum class PlayheadRows { Stem, Canvas };
void render_playhead(cairo_t* cr,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color,
                     PlayheadRows band);

// Draws the strip-drag ANCHOR STEM: a vertical line at the drag's pivot
// column `col` (window pixels within `area`, clamped here to [0, area.w-1]),
// spanning the stems' band like a marker stem (waveform_stem_band), in
// the `playhead_stem` key — the product's one position-line colour (the
// ruling is at the paint site).
// The anchor is
// the clamped column the strip-drag math pins each event — edge-included, so an
// edge-pinned anchor draws the stem exactly at the edge and the clamp becomes
// visible (the Ableton affordance). Like every other stem it paints ONE solid
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
// from the flag painter's stash. Trim below is live too — its bar, endcaps and
// midpoint mark in GuiPaintHandler::paint_trim, ahead of the playheads; trim has
// had no stem since render_trim_stems died, and no stem is cached anywhere.)

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
// `col` clamps col_raw into the visible column range [0, wave_w-1]. The
// inclusive END wall T-1 at full zoom-out rounds to column wave_w (one past the
// surface); left unclamped, the right-edge-anchored end CAP loses its
// bound-edge pixel to the lane clip. Clamping lands the wall on the last
// visible column so the cap stays fully visible on the bar's end.
// Begin/frame-0 already maps to column 0, unaffected.
// The bridge interval (trim_bridge_gap) reads an OFFSCREEN bound's SIDE (below)
// to pick a side-specific flush sentinel past the visible edge, and the painter
// clips its DRAWN extent to the effective width [0, wave_w) so the bar's runs
// stop flush at the edge (the inert gutter never paints; col_raw is the
// sentinel input, not the drawn position).
// Which side of the viewport an OFFSCREEN bound lies on — meaningful only when
// !in_viewport. Derived from the SAME unrounded ms compare that sets in_viewport,
// NOT from col_raw: a bound less than half a pixel off the LEFT rounds to
// col_raw == 0 yet is off-screen, so col_raw alone cannot tell the side (the
// rounding seam). trim_bridge_gap needs the true side to flush/empty correctly.
enum class TrimBoundSide { InView, OffLeft, OffRight };
struct TrimBoundColumn {
    double        ms;          // displayed-domain position (already mapped)
    bool          in_viewport; // ms in [vp_start, vp_end)
    TrimBoundSide side;        // InView / OffLeft / OffRight (unrounded)
    int           col_raw;     // unclamped nearbyint column
    int           col;         // clamped into [0, wave_w-1] (the EOF-wall clamp)
};
TrimBoundColumn trim_bound_column(double displayed_ms,
                                  long long vp_start, long long vp_end,
                                  int wave_w);

// The BETWEEN-THE-ENDS column interval [lo, hi) (waveform-relative,
// half-open, EMPTY when hi <= lo), the ONE owner of the bridge, run by the
// painter (render_trim_flags) alone: the clipped interval is what the painter
// PUBLISHES as the pair drag's handle (TrimBarHit::bridge_lo / bridge_hi,
// read by point_in_trim_bridge_span) — and the painter's BODY wherever a
// bound is in view: the thumb's body between its two arrow buttons' inner
// edges (an offscreen side of the body runs past the window edge as the
// painter states). Both bounds must be set (callers gate). The
// offscreen arms key on the bound's SIDE (TrimBoundColumn::side, the unrounded
// verdict) — NOT col_raw, which cannot tell the side across the rounding seam
// (a barely-off-left bound rounds to col_raw == 0). The 4x2 semantics:
//   BEGIN — the gap's LEFT edge, a left-edge-anchored button:
//     InView (button painted) -> lo = col + endcap_w         (the drawn button's
//        inner RIGHT edge; the gap starts just past the button).
//     OffLeft (no button)   -> lo = min(col_raw, -1)        (a STRICTLY NEGATIVE
//        flush sentinel: the fill clips flush to column 0 AND the left ring border
//        lands offscreen — true only via the sentinel; raw col_raw == 0 would
//        float the border at the edge).
//     OffRight (no button)  -> lo = max(col_raw, wave_w)     (>= wave_w: nothing
//        paints in the visible [0, wave_w) and the router's [0, wave_w) gate can
//        never arm — an empty gap in the visible area).
//   END — the gap's RIGHT edge, a right-edge-anchored button:
//     InView (button painted) -> hi = col - endcap_w + 1      (the drawn button's
//        inner LEFT edge, exclusive; in the NARROW case, where the painter
//        stands the end button right of the begin's instead, this is under
//        lo whenever the begin is in view too, so the gap is empty, as the
//        body is).
//     OffRight (no button)  -> hi = max(col_raw + 1, wave_w + 1)  (a PAST-THE-EDGE
//        flush sentinel: the fill clips flush to the right edge AND the right ring
//        border lands offscreen).
//     OffLeft (no button)   -> hi = min(col_raw + 1, 0)      (<= 0: empty against
//        any lo >= 0 — closes the one-pixel bridge a raw col_raw == 0 left, which
//        gave hi = 1 and painted/accepted a column-0 sliver for a window wholly
//        left of the viewport).
// The +endcap_w inset is the ROOM an on-screen bound's arrow button —
// trim_arrow_button_w_px() wide — occupies; an offscreen bound has no button
// on screen, so the inset is dropped and the body runs FLUSH. This interval
// is returned UNCLAMPED (raw sentinels included) — its role is to carry the
// offscreen-flush and empty semantics past the visible edge; it is NOT a drawn
// interval. The painter clamps it to the visible range ONCE: it intersects it
// with the effective width [0, wave_w) and publishes that clipped interval as
// the bridge's hit span. So the inert non-multiple-of-16 gutter [wave_w, strip_w) neither
// paints nor hits. The sentinels earn their strictness here: an offscreen edge
// lands STRICTLY past the visible range (never at col 0 or col wave_w-1), so a
// window running off the view yields a flush interior rather than a spurious
// one-column one.
struct TrimBridgeGap {
    int lo;  // inclusive left column
    int hi;  // exclusive right column (empty gap when hi <= lo)
};
TrimBridgeGap trim_bridge_gap(const TrimBoundColumn& begin,
                              const TrimBoundColumn& end, int endcap_w, int wave_w);

// The source-frame -> displayed-domain mapping of the live trim paint pass
// (GuiPaintHandler::paint_trim), its one caller since the two HIT sites began
// reading that pass's publication (2026-09-24). Byte-identical to render.cpp's file-local
// frame_to_paint_sample for every reachable (non-negative) trim bound: in a
// mapped view the source frame is rounded once through map_source_to_target,
// then rounded again; the identity (null/empty map) path returns the frame
// as-is. A negative frame is guarded to 0 (unreachable — past-EOF is load-fatal
// and bounds are never negative — kept for exactness vs the prior hit code).
// The painter's one mapping owner, so an endcap is drawn — and, through the
// stash, grabbed — on its bound's image. `map` is null in source view
// (identity) and the item pixels' own map (displayed_or_live_target_map) in
// target view.
double displayed_trim_ms(int64_t frame,
                         const std::vector<WarpFrameMapSegment>* map);

// The ONE trim ARROW-BUTTON screen-rect owner (named for the endcaps the
// buttons are): the edge-anchoring rule lives here, run by the painter
// (render_trim_flags), which paints each in-view bound's button on this rect
// and publishes it for the hit test (hit_test_trim_endcap reads TrimBarHit,
// below), so paint and hit are one rect.
//
// A trim bound is an EDGE, not a point: the begin button's LEFT edge sits ON
// the begin column (rect left = strip_x + begin.col), the end button's RIGHT
// edge sits on the end column (rightmost pixel = strip_x + end.col).
// Deliberate asymmetry vs centered marker flags: a bound at frame 0 / EOF has
// its button fully onscreen. THE NARROW CASE (architect 2026-10-03): when
// BOTH bounds are in view and the drawn width end.col − begin.col + 1 is
// under two buttons, the begin button keeps its column and the END button is
// placed IMMEDIATELY RIGHT OF IT, edge to edge (rect left = strip_x +
// begin.col + the button's width), so the right arrow alone overruns its
// column, by 2 x the button − the width. The two rects therefore never
// overlap: at two buttons' width and above each stands on its own column
// with the body (possibly empty) between them.
//
// THE RECT IS THE BUTTON, trim_arrow_button_w_px() wide (16 Windows px) over
// the trim lane `row`'s whole height (the same 16), and it is THE HIT BAND
// AS IT IS — no tolerance inflates it (architect 2026-10-03: the button is
// the target). `is_begin` picks which bound's button; only an in-view
// bound's is ever asked for.
GuiRect trim_endcap_rect(bool is_begin, int strip_x,
                         const TrimBoundColumn& begin,
                         const TrimBoundColumn& end, GuiRect row);

// (render_trim_stems IS DELETED, architect 2026-08-01. It drew the WAVEFORM-AREA
// portion of the trim bounds — a 1px grey vertical at each bound's column,
// spanning the waveform, meeting the strip-crossing segment at the waveform top
// to form one unbroken line. THE BAR AND ITS TWO ENDCAPS ARE THE WHOLE DISPLAY
// now: the redesigned trim lane states the window at the window, and two
// full-height verticals competing with the marker stems stated it a second time
// in the same pixels. The `trim_stem` config key it painted from outlived it by
// a day and died with the whole tunable palette on 2026-08-02.)

// THE TRIM BAR'S HIT STASH (architect 2026-09-24, strictly as-painted): what
// render_trim_flags last PAINTED as the thumb's two arrow buttons and its
// body, published by that
// painter into AppState::trim_bar_hit so the trim hits read the pixels rather
// than re-running the painter's owner chain on the live trim — the flag
// lane's stash doctrine (AppState::flag_hit_rects) carried to the trim lane.
// Everything is in SCREEN pixels. `lane` is the band the thumb was painted
// in, the y-gate of both hits. Each end (a TrimBarHitCap) is its arrow
// button's rect (trim_endcap_rect: the button over the lane's whole height,
// clipped to the lane's painted width, and the hit band as it is — no
// tolerance), and `painted` is false for a bound the viewport culled, whose
// button is off screen and so answers no hit. The two rects never overlap
// (trim_endcap_rect's narrow rule), so nothing arbitrates between them. The
// bridge is the half-open interval [bridge_lo, bridge_hi) between the two
// buttons' inner edges — the thumb's body — already clipped to the lane's
// painted width (trim_bridge_gap); empty when lo >= hi, as in the narrow
// case. `published` false is COLD — nothing painted, nothing grabbable.
struct TrimBarHitCap {
    bool    painted = false;
    GuiRect rect{0, 0, 0, 0};   // the arrow button, clipped to the lane
};
struct TrimBarHit {
    bool          published = false;
    GuiRect       lane{0, 0, 0, 0};
    TrimBarHitCap begin;
    TrimBarHitCap end;
    int           bridge_lo = 0;   // screen x, inclusive
    int           bridge_hi = 0;   // screen x, exclusive
};

// Draws the WHOLE TRIM LANE (architect 2026-10-02, the AC set; the geometry
// at kTrimLaneHeightPx): Windows 95's scroll bar, miniaturized. All
// pixel-bound integer fills, no stroke and no antialiasing anywhere in this
// lane. The lane band is the `trim_bar` PARAMETER — the caller passes
// top_trim_row_area(app) (top-strip lane 2), and the band painted in is
// published as TrimBarHit::lane, the y-gate both trim hits read, so paint and
// hit take the band as one value and cannot drift; nothing in here re-derives
// the lane's y from the row heights above it. `trim_bar` gives the lane's
// x/y/h; `waveform_area` is read for its `.w` ALONE — both the column-mapping
// denominator and the lane's effective width, so the inert non-multiple-of-16
// gutter is outside the clip and never paints. `top_strip_area` is a
// validity guard only.
//
// PAINT ORDER IS BACK TO FRONT: THE TRACK across the whole lane — the ground,
// then the checked dither (paint_checker_rect, its phase at the lane's
// top-left) — then THE THUMB'S BODY, the ground under a PLAIN RAISED edge,
// the lane's full height, with no grip, then THE ARROW BUTTONS over it. An
// inverted or degenerate window leaves the track showing.
// THE THUMB SPANS THE WINDOW ITSELF and FOLLOWS AN OFFSCREEN BOUND rather
// than stopping short — an out-of-view bound means the window continues past
// that edge, so the body runs on past it by its whole edge's thickness (two
// relief lines), its side edge landing outside the clip, and reads as
// running on rather than ending at the window. It is the one "this is the
// trim window" signal and its body the grab of the pair (bridge) drag.
// THE TWO ARROW BUTTONS ARE THE CAPS (architect 2026-10-03; the rule at
// kTrimArrowButtonPx): each in-view bound's button, on the rect
// trim_endcap_rect places — EDGE-ANCHORED on the bound columns, the begin's
// LEFT edge on its column, the end's RIGHT edge on its own, and in the
// NARROW case the end's standing right of the begin's — the ground under a
// plain raised edge with the arrow glyph (kTrimArrowGlyphRows) centred in
// it, the begin's pointing left and the end's right. THE BODY runs between
// the buttons' inner edges (trim_bridge_gap's interval for an in-view side),
// empty in the narrow case. A culled bound paints and publishes no button:
// it is off screen. A bound is an EDGE, not a point — the deliberate
// asymmetry vs centered marker flags — so a bound at frame 0 / EOF shows its
// button fully onscreen. Column placement is on the displayed viewport basis
// — `trim.begin` / `trim.end` are already in the displayed domain, so no
// further translation happens here. A button has NO editable payload; it is
// a plain-press grab target only (trim is outside the selection system).
// PUBLISHES WHAT IT PAINTS into `out_hit` when non-null (TrimBarHit above):
// the lane, both buttons and the bridge interval, from the very columns this
// pass fills — and a cold record on every early return, since a lane that
// painted no thumb has nothing to grab. The CALLER decides whether this frame
// may publish at all (GuiPaintHandler::paint_trim passes null unless the
// damage clip covers the whole lane), so a narrow repaint cannot stamp the
// stash over pixels it did not redraw.
// `pressed` NAMES THE CAP A SINGLE-BOUND GRAB HOLDS, from its press to the
// gesture's end (architect 2026-10-03; the derivation is paint_trim's): that
// button paints PRESSED, as Windows draws a held scroll arrow —
// DrawFrameControl's DFCS_PUSHED | DFCS_FLAT (Wine, dlls/user32/scroll.c →
// uitools.c UITOOLS95_DFC_ButtonPush: EDGE_SUNKEN under BF_FLAT, one Shadow
// line round the face, its inner line the face itself, and the arrow drawn
// one px right and down of its resting place) — here the ground under one
// Shadow line ring (paint_relief_line_frame) and the glyph one Windows px
// right and down. The pair (bridge) grab presses no cap: its grab is the
// thumb, which Windows leaves raised under the drag.
enum class TrimPressedCap { None, Begin, End };
void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect trim_bar,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
                       TrimPressedCap pressed,
                       TrimBarHit* out_hit);

// The top-strip lane a flag box occupies, exactly as the lane accessor reports
// it: `marker_lane` = top_marker_row_area, whose bottom edge is flush with the
// waveform top. The accessor delegates to strip_row_rect, the single
// strip-geometry owner, and takes AppState — which this module does not see, so
// the caller resolves it and passes it in. That is the point of the parameter:
// the flag boxes and their hit rects land on the SAME band the empty-lane press
// gate and every other lane consumer read, whatever the strip's lane heights
// are, instead of being re-derived by stacking upward from the waveform top.
// ROW 5 COLLAPSED TWO LANES INTO ONE (2026-08-01). The flag was a fused
// rectangle-plus-triangle glyph spanning a flag lane and a triangle lane; it is
// now a single box inside the ONE marker lane, so this carries one rect and the
// seam invariant that bound the pair is retired (the record is at the lane table
// in main.cpp). Kept as a struct rather than a bare GuiRect so the call sites
// that thread it through keep naming what they are threading.
struct FlagLaneRects {
    GuiRect marker_lane;
};

// ONE MARKER STEM, as the flag painter publishes it: the window x of the
// column the stem stands on (the flag box's own LEFT edge — the composite shows
// the stem under it) and the color its class resolved to. The flag PAINTERS are
// the only producers — the two live columns', and the `h` view's diff lane,
// which replaces them wholesale while the mode stands; the readers are the
// per-frame waveform pass
// (GuiPaintHandler::paint_marker_stems) and the playhead's stem
// suppression decider (GuiPaintHandler::playhead_stem_suppressed), both
// paint-side, so a stem and its flag can never disagree about a column.
// The published COLOUR is the marker's resolved stem — its FLAG BOX's face:
// the flag key or the invalid key, or the matching selected key while the
// payload box is the bright one (architect 2026-10-04: the stem follows the
// box it leaves from, so a selected marker whose addressed cell is a bound
// cell publishes its resting stem) — and the consumer paints it as published.
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
    int      marker_index;
    double   x;
    GuiColor color;
};

// WHICH ONE BOX OF WHICH ONE MARKER THE FLAG PASS DOES NOT PAINT, because an
// open marker-lane editor is standing in for it. THE ONE GRAPHIC MODEL, stated
// once here and applied to all three editors (architect 2026-09-05, on the
// tablet: "it just feels odd to have one nonvariant field in the middle ... the
// two editors on the opposite ends behaving one way and the bounds one in the
// middle behaving in a different way makes the whole thing seem hacked
// together"): THE EDITED BOX IS SUPPRESSED IN THIS PASS, THE FIELD IS THE WIDTH
// OF ITS OWN CONTENT, AND EVERY BOX TO ITS RIGHT RIDES THE FIELD'S RIGHT EDGE,
// painted there in resting order with resting anatomy — and published there —
// by render_flag_editor_box.
//
// `cell` NAMES THE EDITED BOX in the marker's own left-to-right run — Payload
// (the flag box itself), then Lower, Upper — and this pass's rule is
// ONE COMPARISON: it paints the boxes LEFT of that cell exactly as it does at
// rest, and NOTHING from that cell rightward. A payload editor therefore takes
// the marker's whole column (the flag is its leftmost box), a lower-bound
// editor leaves the flag standing and takes the lower cell and the upper cell,
// and an upper-bound editor leaves the flag and the lower cell and takes the
// upper cell alone — the rule the two separate
// indices this replaced applied to the two kinds they covered.
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

// Draws the marker lane's flags in `top_strip_area` above visible markers, in
// THE TEXT-ON-FLAG FORM (row 5, 2026-08-01): each flag is a box whose FACE's
// LEFT EDGE stands on its marker's pixel column, spanning the flag box's band
// (marker_flag_box_band) and carrying the marker's own composed label in the
// redesign's sans face. The width is DERIVED from the shaped label (pad +
// shaped + pad); the pads and the warp payload's scale truncation live at
// kMarkerFlagPadLeftPx above. THE FLAT FLAG (architect 2026-10-03; the palette
// block's marker-lane paragraph owns the look): the face flat in its state's
// colour, a ONE-WINDOWS-PX OUTLINE in the theme's DkShadow — the box's top and
// bottom rows (marker_flag_edge_h_px) inside the band, its left border column
// and its run's closing column outside the face — and the stem leaving the
// face's leftmost column across the bottom outline.
//
// THE LEFT BORDER (architect 2026-08-02) stands one column LEFT of the frame
// column so THE STEM KEEPS THE FACE'S LEFTMOST COLUMN. The published hit rect is
// the whole box, border included; the geometry and the left-edge clip are at
// marker_flag_border_px.
//
// AND ONE CLOSING COLUMN AT THE RUN'S RIGHT (architect 2026-09-25): every
// marker's run — the flag box alone, or the flag box and its bound cells —
// ends on ONE outline column past its RIGHTMOST box, so a run is bordered one
// column each side. Its INTERIOR seams stay single: between the flag box and
// the lower cell, and between the two cells, the one column is the next cell's
// own left seam, never a closing column beside it. The hit rect covers the
// closing column (its last column, reading as the box it closes); the geometry
// is at marker_flag_border_px.
//
// OVERLAP IS LATER-OVER-EARLIER IN STORE ORDER and there is NO OTHER OCCLUSION
// MANAGEMENT AT ALL — no elision, no z-lift for selection, no run arbitration.
// That is the whole model the marker-text lane's resolver used to stand in for,
// and it is deliberately the simplest thing that can be true: a later marker's
// box covers an earlier one's tail, and the user pans or zooms to read it. The
// closing column is what keeps that tail from running into the later flag's
// face. (A SELECTION LIFT — selected flags painted in a second pass over
// unselected ones — stood for one day, 2026-09-25, and was struck by the
// architect as poor design; the closing column answers the blend it reached
// for.)
//
// THE STATES, resolved in priority order by the one ladder
// (resolve_flag_face, render.cpp) — DISABLED WINS, then INVALID, then the flag
// — and SELECTION IS THE BRIGHTER FACE of whichever stands (the palette
// block's THE STATES):
//   Disabled:  the theme's ground for the face, the label in the disabled
//              emboss, NO STEM. A disabled invalid marker is disabled.
//   Invalid:   the red set (warp_red_flag_set_cached, or the phase-reset
//              column's): the `invalid_face` key and its `invalid_label`, the
//              stem in the face.
//   Otherwise: the `flag_face` key and its `flag_label`, the stem in the face
//              — ONE FLAG COLOUR ON BOTH COLUMNS AND THEIR CELLS (architect
//              2026-10-03: "phase resets take whatever warp markers take",
//              2026-10-01, now one value with nothing to pair).
//   Selected:  the addressed cell's face `flag_face_selected` (or
//              `invalid_face_selected` over an invalid one) under
//              `flag_label_selected`; the stem in the FLAG BOX's face, so
//              selected only while the payload is the addressed cell
//              (architect 2026-10-04);
//              a selected DISABLED cell the selected face under a flat Shadow
//              label, no stem (provisional). The outline stays DkShadow.
//
// `iteration_on` PAINTS THE TWO BOUND CELLS (architect 2026-09-04; the
// phase-reset painter below carries the same parameter for its own hop
// bracket, and THE CALLER PASSES THE COLUMN'S OWN VERDICT rather than the
// mode's bare bit since 2026-09-10 — iteration_column_lit, app_state.h — so
// the cells paint on the column the lamp was lit in and on no other): while
// the mode is lit here, every marker the sweep reads
// (iter_popup_eligible_marker, warpmarkers.h — so a disabled owner, which
// carries no bracket at all, paints no cells)
// extends its flag rightward with two more boxes, the LOWER bound then the
// UPPER, each painted exactly as the flag box is — the marker's own state,
// the seam column on its left, the outline rows, the label's ink — carrying the
// bound in its signed two-decimal form (format_iter_bound_cell). A cell
// reads as another flag payload, and the sign is its whole syntax: a flag
// payload never carries one and a cell always does, so no bracket or
// separator opens in one cell to close in the next. The flag's own text is
// the plain composer's (flag_text) in every state; no bracket paints anywhere.
//
// `focus_marker` / `focus_cell` NAME THE SELECTED CELL (architect 2026-09-05,
// the brighter face since 2026-10-03): a selected marker shows its selected
// face on its ADDRESSED cell and no other box, and the addressed cell is the
// payload for every selected marker but the focus, whose addressed cell is
// `focus_cell` (AppState::addressed_cell — a press's cell, an editor's, or
// the one a bracket-only undo entry's restore brings back; every other focus
// route resets it to the payload). Where the focus SHOWS that cell NOWHERE —
// neither in this pass nor in the open field standing in for it — its payload
// takes the selected face instead, so a selected marker always shows its
// selection, and shows it once: while a field stands, the FIELD is where its
// own cell's selection lives. The rule is stated once at the palette block's
// marker-lane paragraph and applies on both columns — a phase reset's bound
// cells are cells too. Disabled and invalid resolve cell by cell through the
// same ladder; the stem is the flag box's, its selected face only while the
// payload is the bright cell (architect 2026-10-04).
//
// `cr`'s scaled font is set by this function (the redesign sans face at
// redesign_font_size_px) and restored.
//
// THE PAINTER PUBLISHES ITS GEOMETRY. `out_hit_rects` receives one rect per
// painted box in PAINT ORDER (so the hit walk reads it backwards to get the
// topmost box) and `out_stems` one entry per ENABLED painted marker. A derived
// width cannot be recomputed without shaping, so the pixels' own pass is the
// single owner of both — the same painter-stash contract the redesigned rows'
// buttons already use. Either pointer may be null.
//
// `suppressed` NAMES THE ONE BOX THIS PASS DOES NOT PAINT (SuppressedBox
// above): the marker whose marker-lane editor is open, and WHICH of its boxes
// that editor stands in for. The pass paints the boxes left of that one at
// rest and nothing from it rightward, the boxes to its right riding the
// field's edge under the editor's own painter.
//
// THE PUBLISHED GEOMETRY FOLLOWS THE PIXELS, which is this stash's whole
// doctrine: the rect covers exactly what the pass painted, every boundary
// belonging to a yielded box collapsing onto the rect's right edge so no point
// can answer a box with no ink, and a marker whose FLAG box is suppressed
// publishes no rect at all. Without the skip the editor's box would merely be
// drawn OVER this one, which hides it only while the edited text is the wider
// of the two; a SHORTENED payload then let the committed label's tail show past
// the editor's right edge (the 2026-08-02 bug), and a press in that blank tail
// closed the editor and resolved a marker click off pixels where nothing was
// drawn. THE STEM IS THE EXCEPTION on both counts — it paints and publishes for
// the whole session, the editor unrolling from the flag's own column.
//
// BOTH COLUMNS TAKE IT, but the PAYLOAD cell is
// unreachable on the phase-reset one: that editor is a warp-column surface by its own
// open gate (the bound editor is both columns'), and that painter enforces
// the asymmetry at its own call rather than trusting its caller (recorded
// there).
//
// `warp_frame_map`: the displayed-axis translation the painters share (the live
// map in target view). `waveform_width` is the EFFECTIVE waveform width
// (waveform_area.w), the column-mapping denominator; flags share the marker
// stems' samples-per-pixel so a flag's left edge lands on the column its stem
// rises at, at every window width.
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
//   `box`           the painted box in window coordinates — for the payload
//                   editor the marker's flag, unrolled to hold the FULL
//                   untruncated pending; for the bound editor the bound cell in the same
//                   role, anchored at that cell's own seam. ONE WIDTH RULE FOR
//                   BOTH (architect 2026-09-05, retiring the bound
//                   field's pin to its cell): the box is its two pads plus its
//                   shaped pending run and nothing more, so NO FIELD BUYS A
//                   CARET COLUMN — every one borrows it from its own right pad
//                   (the borrow, render_flag_editor_box). Each field therefore
//                   OPENS AT the width of the box it stands in for, to the
//                   column — the payload's one deliberate step being the
//                   untruncated run where the resting label was capped at nine
//                   glyphs — and then GROWS AND SHRINKS with what is typed, on
//                   every kind alike, with the marker's boxes to its right
//                   riding that edge. An emptied field is two pads, the
//                   smallest box there is, whichever kind it belongs to.
//                   NEVER CLAMPED ON-WINDOW, ON ANY KIND, AT EITHER EDGE
//                   (architect 2026-09-06): the field opens at its own box's
//                   seam wherever that seam is and the WAVEFORM'S EDGE cuts it
//                   off, like every other box in this lane: the logical field
//                   (its seam, its width, `text_origin_x`, `byte_x`) is
//                   unclipped and may reach past either edge, while `box`, the
//                   published claim, is the painted box CLIPPED TO THE
//                   WAVEFORM'S COLUMNS [x0, x0 + w) as its pixels are
//                   (architect 2026-09-26, clip_to_waveform_columns) — a field
//                   cut off at the last column claims its visible part, a
//                   marker at grid point w its border strip alone, and a field
//                   wholly past the edge an empty box. Every box
//                   spans its 1px LEFT BORDER too (the flag's own for the
//                   payload editor, the seam divider for the other two), so
//                   its x is one column left of the fill and its w one wider
//                   — and a field that is its run's LAST box (nothing rides
//                   past it) spans the run's CLOSING column as well, one more
//                   column on its right (2026-09-25, marker_flag_border_px).
//   `text_origin_x` the window x that pending BYTE 0 paints at. It already
//                   carries the view offset, so it is negative-of-nothing and
//                   directly usable: byte k sits at text_origin_x + byte_x[k].
//   `byte_x`        the shaping chokepoint's per-byte-boundary pen offsets
//                   (text_shape::byte_offsets_px) — pending.size() + 1 entries.
//                   The caret, both selection edges and click-to-byte all index
//                   it, so what is drawn and what is grabbed are one vector.
//
//   `riding_cells`  THE MARKER'S BOXES TO THE RIGHT OF THE EDITED ONE, in
//                   resting order with resting anatomy, painted at the field's
//                   right edge so the row reads as it reads at rest with only
//                   the edited box's width live (architect 2026-09-05, THE ONE
//                   GRAPHIC MODEL — SuppressedBox above states it once). What
//                   rides follows from which box the field stands in for: the
//                   payload field carries the two bound cells, the
//                   LOWER-bound field the upper cell, and the UPPER-bound
//                   field nothing, it being the rightmost box there is.
//                   Published as a FlagHitRect: the run's whole painted
//                   extent, every seam divider and the upper cell's closing
//                   column included (2026-09-25), keyed to the edited
//                   marker and carrying the same three boundaries a resting
//                   run publishes — each one collapsing onto the next where
//                   its box is not in the run — so the pointer resolves WHICH
//                   CELL out of it exactly as it does at rest. marker_index -1
//                   with a zero rect wherever nothing rides.
//
//                   THE RIDING CELLS ARE THE MARKER'S OWN CELLS FOR THE
//                   POINTER TOO (architect 2026-09-05, completing the
//                   one-graphic-principle ruling for the press: the three
//                   editors are transparent to each other for the pointer as
//                   they already are graphically). The one flag walk
//                   (topmost_flag_rect, app_state.cpp) asks this rect FIRST,
//                   because the editor paints last and so covers whatever the
//                   lane pass drew under it. The two publications cannot
//                   overlap in any case: the pass's rect ends where the edited
//                   box begins and this run begins at the field's right edge,
//                   so under a BOUND editor the same marker publishes both —
//                   its flag box (and, under the upper field, its lower cell)
//                   in the lane stash, its riding run here — and a point falls
//                   in exactly one. A press on a riding cell is
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

// THE FLAG EDITOR'S UNROLL (row 5's last piece, 2026-08-01): the marker's flag
// box EXPANDS to hold its full untruncated payload, and the editor's text is
// drawn inside it — kdenlive's flag-becomes-the-text-box, which is also how
// this product's own editor read before the marker-text lane took the payload
// away.
//
// THE BOX IS WINDOWS 95's IN-PLACE LABEL EDIT (architect 2026-10-03,
// Explorer's F2 rename; the palette block's editing paragraph): a FLAT box
// framed ONE Windows px in black (kFlagEditorFrame) on its left border column,
// its top and bottom rows (marker_flag_edge_h_px) and its closing column — the
// flag's own outline geometry, so opening an editor changes the flag's SIZE
// and nothing about where it stands — on the edited marker's SELECTED FACE
// (the selected flag opened for edit, architect 2026-10-03: the field pair
// plays no part), its text and caret `flag_label_selected`, the selected
// substring in the theme's selected pair, the pads Windows' field margin
// strips. Since no field buys a caret column,
// the size only changes where the resting label was capped, the field opening
// at the committed run's own width and growing only with what is typed past
// it; the bound cells riding its right edge keep the flag anatomy. A REFUSED
// COMMIT recolours nothing: the whole text is selected (text_editor::refuse)
// and the refusing owner's card says why, the dialog field's rule on this
// surface.
//
// THE TEXT IS THE REDESIGN'S SANS, matching the labels it replaces — the
// monospace face dies at this surface with the lane placement owner
// (lane_text_left_x) that used to put it here.
//
// AT THE WAVEFORM'S EDGE THE BOX IS CUT OFF AND NOTHING MOVES — the clip is
// the waveform's columns [0, w), the resting flag's own (architect
// 2026-09-26: nothing of the field paints in the leftover strip beside w, and
// a marker at grid point w shows its left border alone, on the columns its
// resting flag's border shows on; the published box and riding rect are
// clipped with the pixels) — (architect 2026-09-06,
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

// The phase-reset column's flags: the identical box, the identical class ladder
// and the identical publication contract render_flags documents above. Their
// LABEL is the display-only kPhaseResetLaneToken (a phase reset authors no
// payload). `iteration_on` PAINTS THIS COLUMN'S TWO BOUND CELLS since
// 2026-09-09, when grid iterations grew a second column — and it is this
// column's own verdict, true only while the lamp was lit HERE: every reset the
// sweep reads (phase_reset_iter_eligible_marker, phaseresetmarkers.h — so a
// disabled reset, which carries no bracket at all, paints no cells) extends its
// flag with the LOWER
// bound then the UPPER, each painted exactly as the flag box is, carrying the
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

// THE COLOUR A LIVE PHASE RESET'S STEM WEARS, for a surface that must wear it
// too — the lead-in ring (paint_phase_reset_overlay_ring, paint_handler.cpp,
// architect 2026-09-17). It asks the one ladder (resolve_flag_face,
// render.cpp) rather than restating it: the `invalid_face` key when `red` (the
// column's red set, phase_reset_red_flag_set_cached), the `flag_face` key
// otherwise, each its selected key when `selected` (the reset selected AND
// its payload the bright cell — the bit the flag pass hands the payload box
// whose face the stem wears, architect 2026-10-04), so the ring and its stem
// stay one object at rest and selected.
// No disabled arm, a disabled reset painting neither stem nor ring.
GuiColor phase_reset_stem_color(bool red, bool selected);

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
// THE ANATOMY IS THE LIVE FLAG'S (the palette block's marker-lane paragraph):
// the flat box in the ONE FLAG COLOUR (architect 2026-10-03: the greens and the
// removed red retired, red being invalid-only), its one-Windows-px DkShadow
// outline — the left border outside the face, the top and bottom rows, the
// closing column — and the label on the redesign's sans at the lane baseline
// in the flag's recorded label. THE LABEL CARRIES THE SIGN (architect
// 2026-10-03, the colour no longer saying it): `[+]` before an added line's
// payload, `[-]` before a removed one's (history_diff_label,
// paint_handler.h). A CHANGED pair is that same box
// at double width — the removed half then the added half, each its own label,
// ONE outline column wrapping the whole at its left and a SECOND outline
// column ON THE SEAM between them (2026-08-20, standing since 2026-09-02; the
// halves were two saturated hues then, and the shared column stays the
// anatomy's one seam rule). The pair is still ONE flag: one rect, one focus,
// one claim.
//
// EVERY HALF IS SIZED BY ITS OWN SHAPED TEXT — pad + shaped(label) + pad — so
// the halves of a pair are routinely ASYMMETRIC and the seam, the border and
// the one hit rect all compose off the two measured widths rather than off any
// assumed equality. That is proven machinery: a warp pair's two tempo tokens
// have differed in width since this lane's first day. A longer half simply
// measures longer, and THESE LABELS ARE NEVER TRUNCATED (the cull bound
// follows the commit's own widest text; the reasoning is at the paint site).
//
// THE LANE CARRIES THE DISABLED AXIS, PER COMMIT SIDE (architect 2026-08-22,
// closing a state-axis gap the view shipped with: a `#` line and a live one
// painted the same flag, so the delta showed the position and hid the state).
// A HALF whose line is EFFECTIVELY disabled within ITS OWN side's commit — the
// local '#' or, same-day deepening, the label cascade resolved over that
// side's full warp set (the effective pair at HistoryDiffFlag above; the '#'
// in the TEXT stays the local byte) — paints the LIVE LANE'S DISABLED FACE
// through the same ladder (resolve_flag_face): the theme's ground, the label
// embossed. IT SPLITS HONESTLY ON A CHANGED PAIR: each half takes its own bit,
// so a disable TOGGLE paints one disabled half beside one live half and the
// direction of the toggle is readable off the flag itself. The outline columns
// are the theme's DkShadow on every half, focused or not (below).
//
// `focus_index` is the mode's OWN focus (at most one flag, -1 for none) and
// `selected` its OWN multi-selection (ordinals into the same list, 2026-08-05):
// EITHER GIVES that flag THE SELECTED FACE, both halves of a double flag
// together (the live lane's selection, architect 2026-10-03: face, label and
// stem through the one ladder). One face for both,
// deliberately — the focus is the selection's singleton when the set is empty,
// and the revert act reads them the same way, so a second face would be a
// distinction nothing acts on. The STEM is the flag colour, its selected face
// with the flag's. THE STEM READS THE DISABLED AXIS (architect 2026-08-22): a SINGLE
// flag — added-only or removed-only — whose one side is EFFECTIVELY disabled
// publishes NO STEM AT ALL, the live lane's rule verbatim, while a CHANGED
// PAIR KEEPS ITS STEM whichever halves are disabled, because the pair as a
// whole is a live EDIT being displayed rather than a line in a switched-off
// state.
// NO CELLS AND NO ADDRESSED CELL ON THIS LANE: a bracket is session-only and
// in no commit, so a diff flag carries no bound cells, and the view's focus is
// its own diff-flag cycle's with nothing for AppState::addressed_cell to
// address — the focus rings the whole flag.
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

