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

// Build a GuiColor from a 0xRRGGBB hex literal, converting each 8-bit
// channel to an exact [0,1] double. constexpr so palette constants stay
// compile-time. RGB only: the palette composites nothing (its head, below),
// and the renderer hands colours to cairo through set_palette_source and
// set_waveform_source.
inline constexpr GuiColor hex(uint32_t rgb) {
    return GuiColor{
        static_cast<double>((rgb >> 16) & 0xFF) / 255.0,
        static_cast<double>((rgb >>  8) & 0xFF) / 255.0,
        static_cast<double>( rgb        & 0xFF) / 255.0,
    };
}

// THE ONE COLOR-MIX OWNER: `own` retained by keep_own, the remainder made up
// with `toward`. keep_own == 0 returns `toward` exactly (the `own` term is
// annihilated).
//
// EXACT ON THE PIXEL, NOT IN THE ARITHMETIC: the form is toward + (own -
// toward), and the subtraction is exact only when the two channels are within a
// factor of two of each other (Sterbenz). Where they are far apart in magnitude
// the result misses the true mix by an ULP.
// It has never mattered and cannot: every consumer hands
// these doubles straight to cairo, which quantizes to 8 bits, and an ULP never
// survives that. Stated so no future caller builds an equality test on this
// function's output. Used by the redesign's DISABLED FACE — the icon paths
// (icons.cpp) and the label (paint_handler.cpp) resolve through this single
// expression, so the two halves of a greyed button can never dim by different
// arithmetic.
// A MIX, NOT AN ALPHA: the palette is fully opaque and nothing composites; this
// resolves to a solid color before it reaches cairo. Clamped, so no caller can
// push a channel outside cairo's [0,1] domain.
inline constexpr GuiColor mix_color(GuiColor own, GuiColor toward,
                                    double keep_own) {
    const double t = keep_own < 0.0 ? 0.0 : (keep_own > 1.0 ? 1.0 : keep_own);
    return GuiColor{
        toward.r + (own.r - toward.r) * t,
        toward.g + (own.g - toward.g) * t,
        toward.b + (own.b - toward.b) * t,
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
// The GUI's colours, shared across the renderer module, the paint handler, and
// main.cpp. THE WHOLE PALETTE IS HARD-CODED (architect 2026-08-02): every
// constant here is constexpr, there are no user-settable colours, nothing is
// read from ~/.config, and a retune is a recompile. Every painted surface in
// the product takes its value from one of these constants, WITH NO EXCEPTION.
//
// A HEX HERE IS A DISPLAY-P3 BYTE TRIPLE AND IS WHAT THE TABLET SHOWS
// (architect 2026-10-02). The tablet's window is a Display-P3 layer
// (GuiPlatform::adopt_window, platform_android.cpp), so the panel takes the
// app's bytes as P3 coordinates as-is; the laptop's untagged sRGB surface
// shows the same bytes a little differently, which is accepted (the laptop is
// for debug testing). No colour is converted anywhere between a constant and
// cairo.
//
// THE DESIGN IS WINDOWS 95's, ON A NEUTRAL DARK GROUND, WITH ONE-LINE RELIEF
// (architect 2026-10-02, the frozen design). The chrome is flat ground and
// square raised or sunken boxes drawn with ONE logical line a side
// (relief_line_px, below): RAISED is a Hilight line along the top and the
// left and a Shadow line along the bottom and the right, SUNKEN the reverse,
// and the second pair is painted last so it owns the top-right and
// bottom-left corner pixels (Windows' DrawEdge order); an ETCHED separator is
// a Shadow line with a Hilight line immediately beside it. No gradients, no
// rounded corners, no hover faces, and no compositing alpha anywhere: THE
// PALETTE COMPOSITES NOTHING. Every colour is opaque and is painted as an
// integer rect of cells. The painters are the relief helper family
// (paint_relief_raised and its siblings, below).
// THE LINEAGE, one line, the mock-up sets he judged on the tablet: J4 classic
// neutral greys → K4 thin relief → L2/M1 well lines → N2 etched ticks → O2
// trim lane → P6/T4/U4 ruler ground → R3 menu → S4 8 pt → V3 head (set Q —
// the lane order and the Acid flat trim — ruled out, 2026-10-02).
//
// THE SCAFFOLD (architect 2026-10-02: "connect all of these design pieces
// under a few different keys, the Qt style of theming for Windows-type themes
// … as much derived as possible … accent and ink separate keys, tuned the
// same"). A HANDFUL OF BASE ROLES ARE THE ONLY COLOUR LITERALS; every other
// colour is DERIVED from them at this one site by a named rule, and each
// derived constant is static_assert-ed against its frozen bytes so the
// compiler, not the eye, checks the derivation. A retune of a role is one
// literal here and every face built from it follows. THE BASE ROLES:
//   GROUND  #303030  kRedesignContentGround — ONE ground everywhere: the menu
//           row, the icon row, the three lanes, the bottom row, the dropdowns,
//           the cards, the tooltip, the folder overlay and the picker.
//   LABEL   #FCFCFC  kRedesignLabel — the text and glyph white.
//   ACCENT  #96BFDA  kRedesignAccent — a highlighted row, a selection band.
//   INK     #96BFDA  kWaveformInk — the waveform's ink. ACCENT AND INK ARE TWO
//           ROLES WITH ONE VALUE, declared separately and tuned the same.
//   CANVAS  #141618  kWaveformCanvas — the waveform's ground and the modal
//           field's.
//   FLAG    #8A5EAC  kMarkerFlagFill — the marker and phase-reset flags.
//   RED     #BB575A  kMarkerFlagFillRed — the one error colour.
// BLACK is no role: the absence of light, the text ink on the flags and on
// the accent (kMarkerFlagLabel, kRedesignHighlightLabel).
// THE DERIVED, by rule (each rule at its constant below):
//   from the GROUND — the RELIEF SET (Windows' COLOR_3D* family: Hilight,
//     Shadow, DkShadow; 3DLight is the ground itself, so a thick inner line
//     would vanish into the face, which is why the relief is ONE line), the
//     DOWN face, the TRIM bar and cap, the RULER label, the PLAYHEAD head
//     and the FLAG border — each a grey channel × a stated ratio;
//   from the FLAG and the RED — the edge (× 0.555), and the selected pair
//     where a simple rule reproduces it (two do not, stated at them);
//   from the INK and the CANVAS — the waveform's outline and the region's
//     lift;
//   from the LABEL over the GROUND — the disabled, dimmed and hotkey inks
//     (mix_color at a stated keep).
// WHAT WAS HERE BEFORE, in one paragraph, because the file's shape is its
// residue. The palette was 23 mutable globals loaded from
// ~/.config/warptempo_gui/colors.conf until 2026-08-02; the kdenlive redesign
// (2026-07-31..10-01) then hard-coded a face sampled surface by surface off
// kdenlive and Breeze screenshots — several near-equal greys each kept as
// its own sample, Breeze's rounded buttons, accent hover outlines and their
// fades, a focus halo, a slider, a translucent playhead head and the cards'
// drop shadow — and a two-key tuning knob ran that palette through extra
// sRGB → Display-P3 conversions for a few hours of 2026-10-02. The Windows-95
// scaffold replaced all of it the same day, its colours the knob's two-pass
// output frozen as constants (the struck keys are recorded at
// device_config.h's head). The sampled
// values and their provenance are git history and nothing needs them back.

// THE SCAFFOLD'S ONE RULE FORM: every channel of a 0xRRGGBB word × num/den,
// rounded to the nearest byte (a half rounds up — no frozen value lands on
// one) and clamped at 255. A ratio is a rational so the derivation is
// constexpr and exact; the decimal ratios below are num/den spelled out.
inline constexpr uint32_t scaled_word(uint32_t word, uint32_t num,
                                      uint32_t den) {
    const auto channel = [&](int shift) {
        const uint32_t c = (word >> shift) & 0xFFu;
        const uint32_t v = (2u * c * num + den) / (2u * den);
        return (v > 255u ? 255u : v) << shift;
    };
    return channel(16) | channel(8) | channel(0);
}

// -- THE CHOKEPOINTS ----------------------------------------------------------
//
// EVERY COLOUR THIS PRODUCT HANDS CAIRO GOES THROUGH ONE OF THESE TWO, and no
// site calls cairo_set_source_rgb itself (re-grepped 2026-10-02: none outside
// render.cpp's two bodies): set_palette_source for the chrome and
// set_waveform_source for the waveform's two cairo fills, the canvas
// (render_canvas) and the region's ground (paint_region_ground). Each is a
// plain hand-over today; the seam is kept because it is where a later
// per-device palette plugs in, at one site. The plate's own pixels are
// written as words (argb32_opaque_word), not through cairo.
void set_palette_source(cairo_t* cr, GuiColor c);
void set_waveform_source(cairo_t* cr, GuiColor c);

// -- THE BASE ROLES -------------------------------------------------------------

// GROUND — the surface the whole chrome stands on (the list at the head). It
// has one value focused and unfocused: nothing that paints it swaps on the
// window's activation. render_background's chrome erase is this ground too,
// so the flexible gaps between the lanes are the same surface.
inline constexpr uint32_t kGroundRgb = 0x303030;
inline constexpr GuiColor kRedesignContentGround = hex(kGroundRgb);

// LABEL — the text colour over the chrome (black on the accent's highlight,
// kRedesignHighlightLabel), and the white of the playhead stem, its held head
// and the scanner.
inline constexpr GuiColor kRedesignLabel = hex(0xFCFCFC);

// ACCENT and INK — two roles, one value, tuned the same (the head). The INK is
// the waveform plate's one ink (row 6, below); the ACCENT is the highlighted
// face of a dropdown item, a folder-overlay row and a selection band.
inline constexpr GuiColor kRedesignAccent = hex(0x96BFDA);   // (150, 191, 218)
inline constexpr uint32_t kWaveformInkRgb = 0x96BFDA;
inline constexpr GuiColor kWaveformInk    = hex(kWaveformInkRgb);

// CANVAS — the waveform's ground, and the modal text field's
// (kModalFieldGround derives below). Spelled as a word because
// kWaveformRegionCanvas lifts the word.
inline constexpr uint32_t kWaveformCanvasRgb = 0x141618;   // (20, 22, 24)
inline constexpr GuiColor kWaveformCanvas    = hex(kWaveformCanvasRgb);

// FLAG and RED — the marker lane's two hues (the classes are below).
inline constexpr uint32_t kFlagRgb = 0x8A5EAC;   // (138, 94, 172)
inline constexpr uint32_t kRedRgb  = 0xBB575A;   // (187, 87, 90)
inline constexpr GuiColor kMarkerFlagFill    = hex(kFlagRgb);
inline constexpr GuiColor kMarkerFlagFillRed = hex(kRedRgb);

// -- THE RELIEF SET, from the ground ------------------------------------------
//
// Windows' COLOR_3D* family on this ground (the edge grammar at the head):
//   HILIGHT  = ground × 1.96  → #5E5E5E (94 = 48 × 1.96, 94.08)
//   SHADOW   = ground × 5/8   → #1E1E1E (30 = 48 × 0.625 exactly)
//   DKSHADOW = ground × 0.21  → #0A0A0A (10 = 48 × 0.21, 10.08) — the well's
//     inner line, the tooltip's border and the dialog focus frame; NOT black
//     (architect 2026-10-02).
// 3DLIGHT is the ground itself, which is why the relief is one line (the
// head). A RAISED box is Hilight top/left and Shadow bottom/right; a SUNKEN
// one the reverse.
inline constexpr uint32_t kReliefHilightRgb  = scaled_word(kGroundRgb, 196, 100);
inline constexpr uint32_t kReliefShadowRgb   = scaled_word(kGroundRgb, 5, 8);
inline constexpr uint32_t kReliefDkShadowRgb = scaled_word(kGroundRgb, 21, 100);
static_assert(kReliefHilightRgb  == 0x5E5E5E);
static_assert(kReliefShadowRgb   == 0x1E1E1E);
static_assert(kReliefDkShadowRgb == 0x0A0A0A);
inline constexpr GuiColor kReliefHilight  = hex(kReliefHilightRgb);
inline constexpr GuiColor kReliefShadow   = hex(kReliefShadowRgb);
inline constexpr GuiColor kReliefDkShadow = hex(kReliefDkShadowRgb);

// THE DOWN FACE — a toggled-on button's face (a lamp: the view group's lit
// view, the iteration lamps, Add to Selection, the player's Repeat One, the
// keyboard's armed Shift), painted SUNKEN with its glyph one logical px down
// and right: ground × 1.21 → #3A3A3A (58 = 48 × 1.21, 58.08). A PRESSED
// button is sunken on the ground itself (Windows: the push button's pressed
// face keeps the button face).
inline constexpr uint32_t kRedesignDownFaceRgb = scaled_word(kGroundRgb, 121, 100);
static_assert(kRedesignDownFaceRgb == 0x3A3A3A);
inline constexpr GuiColor kRedesignDownFace = hex(kRedesignDownFaceRgb);

// THE GREY LADDER'S DIMMED INKS, from the label over the ground through the
// one mix owner (mix_color, above):
//
// THE DISABLED FACE (kRedesignDisabledMix): every ink a disabled control
// paints — a glyph's paths in their own colours, a menu word, a dropdown
// item's label — RETAINS this fraction of itself over the ground under it,
// so a dead control dims as one object without rotating its hue. MEASURED off
// kdenlive's row-2 crop (2026-07-31: (109-41)/(252-41) = 0.3223 and its two
// sibling channels) and kept as the ratio. A DISABLED BUTTON KEEPS ITS RAISED
// EDGE (architect 2026-10-02): only its glyph or label dims.
inline constexpr double kRedesignDisabledMix = 0.322;
// THE DIMMED SECOND LINE of a two-line tooltip retains this much of the label
// over the ground (kdenlive's hint line, 0.52, measured the same way).
inline constexpr double kRedesignDimMix = 0.52;
// THE DROPDOWN'S ACCELERATOR COLUMN is the label at Qt's own 178/255 over the
// ground — the ratio kdenlive's crop measured, its accelerators drawn at ~70 %
// opacity, re-run on this ground (architect 2026-10-02: one ground): 48 +
// 204 × 178/255 = 190.4 → #BEBEBE. A disabled row's accelerator is the
// disabled label taken at the same keep (paint_dropdown): the row dims once
// and its accelerator twice.
inline constexpr double   kPopupHotkeyMix      = 178.0 / 255.0;
inline constexpr GuiColor kRedesignPopupHotkey =
    mix_color(kRedesignLabel, kRedesignContentGround, kPopupHotkeyMix);

// THE HIGHLIGHT'S TEXT IS BLACK (architect 2026-10-02, the Windows highlight:
// a dropdown's hovered or keyboard-selected row and the folder overlay's
// highlighted row are a FLAT ACCENT FILL under black text and a black
// accelerator). ASSUMED BY THE PLANNER, judged by him on the glass: the
// alternative is the selected grey — ground × 1.54 → #4A4A4A (74 = 48 ×
// 1.54, 73.92) — under the label white.
inline constexpr GuiColor kRedesignHighlightLabel = hex(0x000000);

// THE ACCENT'S UNFOCUSED FACE — the folder overlay's highlighted row while the
// window is not activated (architect 2026-09-02: a selection in an unfocused
// window takes the inactive selection). THE RULE IS KDE's:
// KColorUtils::tint(ground, accent, 0.4), a contrast-solved HCY tint no
// mix_color can express, computed against KF6 GuiAddons 6.30 on 2026-10-02
// (it reproduces Breeze's published #1b4155 and this palette's earlier
// #2d454f from their own inputs) → #3E4C55. Its text is the label white: a
// dark ground under black text would not read.
inline constexpr GuiColor kRedesignAccentInactive = hex(0x3E4C55);

// -- Row 5: the TRIM lane, the RULER lane, the MARKER lane ------------------

// THE TRIM LANE (architect 2026-10-02; the geometry at kTrimLaneHeightPx and
// render_trim_flags): a SUNKEN trough the lane's full width, its ground the
// ground; the BAR inside it RAISED on this face, ground × 4/3 → #404040
// (64 = 48 × 4/3 exactly); the two end handles and the centre grip SOLID
// RAISED SQUARES on this face, ground × 3.5 → #A8A8A8 (168 = 48 × 3.5
// exactly). The render player's scrub is this lane's trough, bar and cap
// (paint_modal_dialog).
inline constexpr uint32_t kTrimLaneBarRgb = scaled_word(kGroundRgb, 4, 3);
inline constexpr uint32_t kTrimLaneCapRgb = scaled_word(kGroundRgb, 7, 2);
static_assert(kTrimLaneBarRgb == 0x404040);
static_assert(kTrimLaneCapRgb == 0xA8A8A8);
inline constexpr GuiColor kTrimLaneBar = hex(kTrimLaneBarRgb);
inline constexpr GuiColor kTrimLaneCap = hex(kTrimLaneCapRgb);

// THE RULER LANE's inks: the timestamps, every label one colour (architect
// 2026-10-02, "give the same colour to all the numbers"), ground × 4.04 →
// #C2C2C2 (194 = 48 × 4.04, 193.92); and the TICKS, ETCHED — each tick a
// Shadow line with a Hilight line immediately to its right over the same rows
// (architect 2026-10-02, the Sonic Foundry etching; paint_ruler_row).
inline constexpr uint32_t kRulerLabelRgb = scaled_word(kGroundRgb, 404, 100);
static_assert(kRulerLabelRgb == 0xC2C2C2);
inline constexpr GuiColor kRulerLabel = hex(kRulerLabelRgb);
inline constexpr GuiColor kRulerTick  = kReliefShadow;

// THE PLAYHEAD. The HEAD is an aliased shape in one flat, OPAQUE grey
// (architect 2026-10-02: "the classic Windows way"), ground × 2.9 → #8B8B8B
// (139 = 48 × 2.9, 139.2), seated on the ruler lane's bottom rows
// (paint_ruler_row). THE HEAD IS THE HOLD POSTURE'S LAMP (architect
// 2026-09-24): while AppState::camera_hold stands it paints in
// kPlayheadHeadHeld, the stem's white — a STATE COLOUR, not a class (the
// marker ladder is untouched). Its repaint is the per-tick comparator's
// (main.cpp), since the bit flips with no damage of its own. The STEM and
// the SCANNER (the moving playback line, paint_scanner) are the label white.
inline constexpr uint32_t kPlayheadHeadRgb = scaled_word(kGroundRgb, 29, 10);
static_assert(kPlayheadHeadRgb == 0x8B8B8B);
inline constexpr GuiColor kPlayheadHead      = hex(kPlayheadHeadRgb);
inline constexpr GuiColor kPlayheadStem      = kRedesignLabel;
inline constexpr GuiColor kPlayheadHeadHeld  = kPlayheadStem;
inline constexpr GuiColor kPlayheadScanner   = kRedesignLabel;

// THE MARKER LANE's classes. Each class is a FILL plus a 1px TOP EDGE, and the
// box carries a 1px LEFT BORDER outside that fill (kMarkerFlagBorder, below);
// the geometry is unchanged by the 2026-10-02 design ("way too many of them
// for more" relief).
//
// SELECTION IS A COLOUR SWAP AND NOTHING ELSE, AND THE SWAP IS ONE CELL'S
// (architect 2026-09-05, "light the colour of only the flag that's
// clicked"): a selected marker paints its ADDRESSED cell in the bright pair
// and every other cell of its run in its calm pair; the addressed cell is the
// payload for every selected marker but the focus, whose addressed cell is
// AppState::addressed_cell. The geometry, the stem and the hit rect are
// identical either way, and the swap happens INSIDE the disabled blend
// (kMarkerDisabledMix), so a selected disabled marker lifts like a live one
// and still reads switched off.
//
// A FLAG HAS TWO COLOURS, UNSELECTED AND SELECTED, AND NO HOVER FACE
// (architect 2026-09-29): the pointer over a flag says what a press will do
// through the CURSOR alone (pointer_cursor_kind). Do not re-propose a flag
// hover.
//
// THE DERIVATION, from the FLAG role:
//   EDGE     = fill × 0.555 (111/200): 76.59 → 77, 52.17 → 52, 95.46 → 95
//              = #4D345F exactly.
//   SELECTED FILL #B37BE0 (179, 123, 224) — A LITERAL: no simple rule
//              reproduces it; the nearest, fill × 1.3, gives green 122.
//   SELECTED EDGE #63447B (99, 68, 123) — A LITERAL: the edge rule on the
//              selected fill gives blue 124.
inline constexpr uint32_t kMarkerFlagEdgeRgb = scaled_word(kFlagRgb, 111, 200);
static_assert(kMarkerFlagEdgeRgb == 0x4D345F);
inline constexpr GuiColor kMarkerFlagEdge    = hex(kMarkerFlagEdgeRgb);
inline constexpr GuiColor kMarkerFlagFillSel = hex(0xB37BE0);
inline constexpr GuiColor kMarkerFlagEdgeSel = hex(0x63447B);

// THE RED CLASS — the one ERROR colour (architect 2026-10-02: red stays
// error-only), a REST pair and a SELECTED pair like every class (architect
// 2026-09-16): the class ladder DISABLED > RED > default is untouched, so a
// red marker is red in both pairs and only its BRIGHTNESS moves, read on the
// same `selected` bit (resolve_flag_face). THE BRIGHT PAIR IS ALSO THE
// PRODUCT'S ONE INVALID RED — the red flash a refused commit paints on the
// flag editor's box and on the dialog field alike, reading these two
// constants, so the flash and the class cannot drift. From the RED role:
//   EDGE          = red × 0.555, the flag's own edge rule: 103.79 → 104,
//                   48.29 → 48, 49.95 → 50 = #683032 exactly.
//   SELECTED FILL #DE7C80 (222, 124, 128) — A LITERAL: no simple rule.
//   SELECTED EDGE = selected fill × 0.555: 123.21 → 123, 68.82 → 69,
//                   71.04 → 71 = #7B4547 exactly.
// THE RED STEM AT REST is the red fill (kMarkerStemRed); the stem follows the
// selection bit as the fill does (architect 2026-09-23), so a selected red
// marker stems in the selected fill.
inline constexpr uint32_t kRedSelRgb           = 0xDE7C80;
inline constexpr uint32_t kMarkerFlagEdgeRedRgb    = scaled_word(kRedRgb, 111, 200);
inline constexpr uint32_t kMarkerFlagEdgeRedSelRgb = scaled_word(kRedSelRgb, 111, 200);
static_assert(kMarkerFlagEdgeRedRgb    == 0x683032);
static_assert(kMarkerFlagEdgeRedSelRgb == 0x7B4547);
inline constexpr GuiColor kMarkerFlagEdgeRed    = hex(kMarkerFlagEdgeRedRgb);
inline constexpr GuiColor kMarkerFlagFillRedSel = hex(kRedSelRgb);
inline constexpr GuiColor kMarkerFlagEdgeRedSel = hex(kMarkerFlagEdgeRedSelRgb);
inline constexpr GuiColor kMarkerStemRed        = kMarkerFlagFillRed;

// THE SEAM COLUMN between a flag box and the cell to its right is a 1px
// kMarkerFlagBorder column, the same dark rule that sits one column left of
// every flag (architect 2026-08-20; kept under the same-hue pairing of a flag
// and its own cells, 2026-09-15): two saturated fields of different hue
// meeting edge to edge read as lying on different planes, and a dark rule
// between them stops the hue boundary doing the work alone; it stands on
// every seam whether or not two hues meet. ONE BOUNDARY, THREE RENDERINGS,
// all taking the divider so it never appears or vanishes on an editor open:
// the resting and the riding cell through the one cell painter
// (paint_iter_bound_cell) and the open bound field's left border; the diff
// pair's seam is render_history_diff_flags'. The published cell boundary IS
// the seam column, so a press on the divider reads as the cell it
// introduces. Every run carries a closing column (architect 2026-09-25;
// marker_flag_border_px).

// THE PHASE-RESET COLUMN WEARS THE WARP COLUMN'S PAIRS (architect 2026-10-01:
// "phase resets take whatever warp markers take"), the two columns' symmetry
// kept as four constants of this column's own, each the warp column's value:
// both columns go through the one class ladder (FlagColumnFace,
// resolve_flag_face, render.cpp) and RED STAYS ERROR-ONLY on both. The
// phase-reset STEM wears its flag's fill, calm at rest and the Sel fill when
// selected (architect 2026-09-23), and THE LEAD-IN RING on the waveform wears
// the colour its reset's stem wears (paint_phase_reset_overlay_ring, through
// phase_reset_stem_color — architect 2026-09-17). The bound (hop) cells on
// this column wear this pair too (architect 2026-09-21), every bound-cell
// call site passing the face of the column the cells belong to — the
// argument required, never defaulted (warp is never the unmarked default).
inline constexpr GuiColor kPhaseResetFlagFill    = kMarkerFlagFill;
inline constexpr GuiColor kPhaseResetFlagEdge    = kMarkerFlagEdge;
inline constexpr GuiColor kPhaseResetFlagFillSel = kMarkerFlagFillSel;
inline constexpr GuiColor kPhaseResetFlagEdgeSel = kMarkerFlagEdgeSel;

// THE MARKER LANE'S TEXT INK IS BLACK, IN EVERY CLASS AND EVERY STATE
// (architect 2026-08-20, a measurement of kdenlive's own flag text): the warp
// and phase-reset flag labels, the bound cells' text, the `h` view's diff-flag
// labels and the flag editor's unrolled text and caret. THE ONE EXCEPTION IS
// THE FLAG EDITOR'S SELECTED SUBSTRING, the selection pairing below. A
// DISABLED LABEL blends toward the surface it sits on through the one mix
// owner at kMarkerDisabledLabelMix (below).
inline constexpr GuiColor kMarkerFlagLabel = hex(0x000000);

// THE SELECTION GROUND IS THE ACCENT AND THE SELECTED LETTERS ARE THE LABEL
// WHITE, ON EVERY TEXT SURFACE (architect 2026-08-28; the letters held white
// over the light accent 2026-10-01): the three dialog editors' shared field
// and the marker lane's flag editor read kRedesignAccent behind the selected
// substring and kRedesignLabel for its glyphs. The UNSELECTED ink is
// untouched on both surfaces (black in the flag editor, the label in the
// dialog field), so the flag editor shows its run once per region — the black
// ink clipped to the band's complement, the white clipped to the band — while
// the dialog field's run is the label white already and the band goes under
// it. A caret is the cursor's ink, not the selection's, and keeps its
// surface's. The paint sites are render_flag_editor_box (render.cpp) and the
// modal field painter (paint_handler.cpp).

// THE HISTORY VIEW'S TWO DIFF CLASSES — the `h` mode's marker lane, where a
// GREEN flag is a line the session has and the shown commit did not (added)
// and a RED one a line the commit had and the session dropped (removed). The
// same anatomy and the same one-flag focus swap as the live lane. THE GREENS
// ARE THIS VIEW'S ALONE, literals with no simple rule from a role (the
// flag's edge rule gives 63 for the edge's red), so a live flag can never
// read as a diff flag. THE REMOVED PAIRS ARE THE RED CLASS'S OWN — the error
// red's one accepted double duty, an error on a regular view being meant to
// be worked away. THE DISABLED AXIS RIDES OVER THESE PAIRS (architect
// 2026-08-22) through the live lane's kMarkerDisabledMix over the ground, no
// constant of its own; the stem follows the focus swap as the live lane's
// does (architect 2026-09-23).
inline constexpr GuiColor kHistoryAddedFill      = hex(0x71B79E);
inline constexpr GuiColor kHistoryAddedEdge      = hex(0x3D6559);
inline constexpr GuiColor kHistoryAddedFillSel   = hex(0x95EDCF);
inline constexpr GuiColor kHistoryAddedEdgeSel   = hex(0x518473);
inline constexpr GuiColor kHistoryRemovedFill    = kMarkerFlagFillRed;
inline constexpr GuiColor kHistoryRemovedEdge    = kMarkerFlagEdgeRed;
inline constexpr GuiColor kHistoryRemovedFillSel = kMarkerFlagFillRedSel;
inline constexpr GuiColor kHistoryRemovedEdgeSel = kMarkerFlagEdgeRedSel;

// THE BOX'S 1px LEFT BORDER — class- and selection-invariant across every
// live class (architect 2026-08-02), ground × 7/16 → #151515 (21 = 48 ×
// 0.4375 exactly). It is PART OF THE FACE ON THE DISABLED AXIS (architect
// 2026-08-02, second pass: "not literally with alpha, but via color mix"): a
// disabled marker's border goes through the same mix owner at the same
// kMarkerDisabledMix toward the lane ground the fill and the top edge take
// (resolve_flag_face, render.cpp).
inline constexpr uint32_t kMarkerFlagBorderRgb = scaled_word(kGroundRgb, 7, 16);
static_assert(kMarkerFlagBorderRgb == 0x151515);
inline constexpr GuiColor kMarkerFlagBorder = hex(kMarkerFlagBorderRgb);

// THE DISABLED FACE OF A MARKER IS A BLEND, NEVER AN ALPHA (architect): 25% of
// the class colour over the lane ground, per channel, through the one mix
// owner. Alpha would be wrong here for a reason specific to this lane — flags
// OVERLAP, so a translucent disabled flag would show its neighbour through
// itself. "THE CLASS COLOUR" INCLUDES THE SELECTION SWAP (architect
// 2026-08-01): the pair entering the blend is the one the marker would paint
// live — red's, the selected pair's, or the calm default's — so a selected
// disabled marker takes the same relative lift a live one does. THIS
// FRACTION IS THE SURFACES' — fill, top edge and left border, on the flag,
// the cells and the `h` view's diff flags.
inline constexpr double kMarkerDisabledMix = 0.25;

// THE DISABLED LABEL'S OWN FRACTION (architect 2026-08-20), split off from the
// surfaces' on the day the lane's ink went black: blending black toward the
// fill makes the label DARKER, so a quarter would pull it into the flag, and
// a darker-than-fill ink has a ceiling (pure black on a dimmed fill tops out
// near 1.7:1) — 0.75 buys nearly all of the reachable contrast while leaving
// the label visibly inside the disabled face. ONE fraction for both columns,
// glass retunes.
inline constexpr double kMarkerDisabledLabelMix = 0.75;

// -- ROW 6: THE WAVEFORM ITSELF ---------------------------------------------
//
// THE WAVEFORM'S PALETTE IS A NEUTRAL CANVAS UNDER ONE INK (the CANVAS and
// INK roles above): the ink is the face's one hue, and the ground casts no
// tint over it. With the magnification lamp dark the plate is the raw bar
// alone in kWaveformInk, with no outline; lit, the plate is two bars (the
// rule is at render_waveform's declaration), BOTH FILLED IN kWaveformInk, the
// inner distinguished only by its OUTLINE, its true contour, an erosion at
// distance waveform_line_px() (1 px at 100 %, 2 on the tablet), in
// kWaveformForegroundOutline.

// THE LIT INNER BAR'S OUTLINE (architect 2026-09-27, the rule picked by eye
// in GIMP): a 50 % blend of the INK over the CANVAS IN LINEAR LIGHT, GIMP's
// default compositing — each sRGB-encoded channel to linear, the two
// averaged, back to the encoding: (150, 191, 218) over (20, 22, 24) → linear
// (0.15599, 0.26451, 0.35512) → (110.0, 140.6, 160.7) → (110, 141, 161),
// #6E8DA1. A LITERAL WITH ITS RULE: the transfer function's pow() is not
// constexpr, and mix_color blends in byte space ((85, 106.5, 121), darker).
// A retune of the ink or the canvas re-runs this arithmetic here.
inline constexpr GuiColor kWaveformForegroundOutline = hex(0x6E8DA1);

// THE REGION'S STEP — THE ONE REGION RULE (architect 2026-09-24): each of the
// R, G and B bytes of an ARGB32 word raised by +18 / +18 / +20, saturating at
// 255, the alpha byte kept. DERIVED, NOT SAMPLED: Breeze's own View ->
// ViewAlternate lift, +9/+9/+10 per channel, TAKEN TWICE (architect
// 2026-08-01: "the waveform highlight should be brighter"). ONE OWNER OF THE
// STEP for both halves of the highlight: kWaveformRegionCanvas below is this
// step applied to the canvas
// word at compile time, and paint_region_ink applies it to every opaque plate
// pixel inside the region, each from its OWN colour, whatever ink the palette
// holds — no ink is keyed and no lifted constant is pinned, so a change to
// any ink's or the canvas's constexpr carries its lift with it; at full alpha
// the premultiplied word (argb32_opaque_word, below) is the colour itself,
// so lifting the bytes lifts the colour. The ink's lift is (150, 191, 218) +
// (18, 18, 20) = (168, 209, 238) = #a8d1ee.
inline constexpr uint32_t region_lift(uint32_t word) {
    const auto lift = [](uint32_t byte, uint32_t step) {
        return byte + step > 255u ? 255u : byte + step;
    };
    return (word & UINT32_C(0xFF000000)) |
           (lift((word >> 16) & 0xFFu, 18) << 16) |
           (lift((word >>  8) & 0xFFu, 18) <<  8) |
            lift( word        & 0xFFu, 20);
}

// THE REGION HIGHLIGHT'S GROUND — the canvas lifted by the region's step:
//     kWaveformCanvas (20, 22, 24) + (18, 18, 20) = (38, 40, 44) = #26282c
// AN OPAQUE GROUND RECOLOUR, NOT A BLEND (paint_region_ground, painted BEFORE
// the plate blit): the span's canvas is REPLACED by this colour and the ink
// composites over it exactly as over the plain canvas. The plate's alpha is
// BINARY, so an ink pixel is fully opaque and a gap fully transparent. THE
// OTHER HALF lifts the ink the same way (architect 2026-08-18): paint_region_ink,
// a second pass AFTER the blit, writes every opaque plate pixel inside the
// span as its own colour lifted by region_lift, every gap left showing this
// ground. region_lift's step is the one thing to move if the highlight wants
// to be stronger or weaker — both halves follow it.
inline constexpr GuiColor kWaveformRegionCanvas = hex(region_lift(kWaveformCanvasRgb));

// THE WELL — the waveform area's two-line border, taken FROM the area at its
// top and its bottom, full window width (architect 2026-10-02; the geometry
// at waveform_border_px): the TOP is a Hilight line then a DkShadow line,
// the BOTTOM a DkShadow line then a Hilight line, the canvas between (the
// relief set above; DkShadow, not black — his ruling). Nothing crosses the
// well's lines: the stems, the playhead and the scanner stop at the canvas
// (waveform_content_rect).
//
// TAKEN FROM THE AREA, NOT ADDED TO IT: waveform_content_rect is the content
// and it shrinks by these rows, while waveform_area itself does not move, so
// the lane stack, the strip geometry, the effective width, samples-per-pixel
// and every column mapping are untouched by the border.

// THE MODAL TEXT FIELD'S GROUND is the CANVAS (architect 2026-10-02), a
// SUNKEN panel with no outline of its own (the field painter,
// paint_modal_dialog). The invalid red flash recolours the field in the red
// class's BRIGHT pair (kMarkerFlagFillRedSel under kMarkerFlagEdgeRedSel).
inline constexpr GuiColor kModalFieldGround = kWaveformCanvas;

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
// EVERY PAINTED DIMENSION IN THE TREE RIDES IT: crop-measured 100% values are
// the authored constants, and every conversion rounds with std::nearbyint.
void   set_gui_scale_percent(int percent);

// THE LIVE PERCENT ITSELF, for the one thing a factor cannot serve: a CACHE
// FINGERPRINT FIELD. The scale is an input to pixels the way an inset or a
// gain field is, and a fingerprint keys its inputs BY FIELD rather
// than through whatever else happens to move with them — an integer percent is
// what makes that compare exact, where the factor is a double and a derived
// dimension is a coincidence. Nothing paints through this: every painted
// dimension goes on reading gui_scale_factor / scaled_px.
int    gui_scale_percent();

// Scale factor s = gui_scale / 100. Exactly 1.0 at the default, and as low as
// 0.5 since the setting's grammar floor came down to 50 (architect 2026-08-10).
double gui_scale_factor();

// One authored 100%-scale length -> device pixels, the ONE conversion every
// scaled dimension in the tree takes: std::nearbyint like every other
// integer-domain conversion. Every scaled accessor below (and the painters'
// own lengths in paint_handler.cpp / render.cpp) spells its conversion through
// this pair rather than open-coding the multiply; the DOUBLE-domain readers —
// redesign_font_size_px and the ruler's unrounded pitch
// compare — are a different concept (they never round to int, or round to a
// double on purpose) and deliberately do not come through here.
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
// (trim_middle_inset_px / trim_middle_clear_px, the two grab tolerances).
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
inline constexpr int kPlayheadUnitPx = 8;

// Authored pixel geometry of the MENU ROW — the top strip's lane 0, at the
// window edge (the kdenlive menu bar, row 1 of the redesign). 30 AT 100%
// gui_scale, AND THE LANE IS ITS CONTENT: the row stands at that height with
// the ICON ROW directly under it and no margin, border or line between the
// two. kdenlive, QEMU and virt-manager draw no border between the menubar and
// the toolbar, and neither does this row: its ground — the content ground
// since 2026-10-01, the icon row's own (paint_menu_row) — runs straight on
// into the icon row's.
//
// THE 30 IS THE ANCHOR'S, AND THE ANCHOR IS THE LANE (architect 2026-09-09:
// "make the height of the top row based on the thirty pixels of File/Edit ...
// this way the dropdown will touch the first row, as it does in kdenlive").
// kdenlive's File item measured 30 rows, and that number is the lane whole:
// each anchor's rectangle fills it top to bottom and IS the anchor's
// published hit rect and the open anchor's sunken frame (paint_menu_row), the
// anchors' labels and the battery + clock legend are cap-centred in it (cap
// top 9 rows under the lane's top edge at 100%), and the anchor's foot is the
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
// lane metric. At the tablet's 200% the lane is 60. TWO ACCESSORS FOR ONE
// NUMBER, deliberately: the lane table reads the LANE and the painter the
// CONTENT, the vocabulary every other row keeps, and this row's lane simply
// has no other term in it — the content is the anchors' box and the labels'
// box both, so there is no third reading.
inline constexpr int kMenuRowHeightPx = 30;
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
// the row's 32px button box and 9/10 label pads survive as the MODAL DIALOG
// BUTTONS' own constants — kModalBtnBoxPx and friends, paint_handler.cpp —
// which used to read row 2's. The row's crop record is git history.)

// Authored pixel geometry of the ICON ROW — the top strip's lane 1, directly
// under the MENU ROW with nothing between (row 4 of the redesign: TWENTY-SIX
// view/mode/action buttons — the kIconRowButtons and kIconRowViewGroup tables
// are the count's one authority, and ALL of them paint on every frame;
// icons::kIconCount is a different number, the GLYPH set, which the row does
// not exhaust). THIS BLOCK IS ALSO THE BOTTOM ROW'S CONTENT: that lane
// delegates its content height to icon_row_content_h_px below. Measured at
// 100% gui_scale off row_4_button_{rest,hover,click,selected,selectedhover}
// .png (32x32) and row_4_separator.png (1x34).
//
// THE LANE IS ITS 46 CONTENT, NO BORDER (architect 2026-10-01): the trim
// lane sits directly under it where gap 1 is zero, and its own sunken top line is
// the boundary — a 1px border-bottom here as well read as "a double border".
// Where gap 1 opens (the laptop), the row's content ground meets the gap's
// window ground of the same value. THE ROW IS MODELLED ON KDENLIVE'S SECOND
// TOOLBAR, the one under its timeline; the first, sharing the menubar's
// ground, was left out for space (architect 2026-09-09), so nothing sits
// between the menu row and this one.
//
// 46, AND THE ARITHMETIC CLOSES EXACTLY (architect 2026-07-31): the separator
// crop is 34 tall with 6px margins, and 6 + 34 + 6 = 46 — the 34px separator
// sits at +6 and the 32px buttons at +7, both placed by the standing
// vertical-centering rule ((46-34)/2 == 6 and (46-32)/2 == 7).
inline constexpr int kIconRowHeightPx = 46;
inline int icon_row_content_h_px() {
    return scaled_px(kIconRowHeightPx, 5);
}
inline int icon_row_h_px() {
    return icon_row_content_h_px();
}

// THE ICON BUTTON'S OWN BOX, measured at 100% off the same five 32x32 state
// crops as the lane above (row_4_button_{rest,hover,click,selected,
// selectedhover}.png). These three lived as file-local constants in
// paint_handler.cpp beside the row's walk until 2026-08-28, when THE FOLDER
// OVERLAY'S ROWS BECAME BUTTONS (architect: "we've gone for the button
// analogy", "the buttons are good enough size for my finger") and a second
// file needed them — so the numbers moved up here beside the lane metrics they
// were always measured with, ONE DEFINITION EACH, and both painters and
// folder_overlay.h read them from here. The row's OTHER metrics — the
// separator's width, height and side gaps, and the 1px outline stroke — are
// the ROW's chrome rather than the BUTTON's box and stay where the row's walk
// is.
//
// THE GLYPH IS CENTRED IN THE BOX, which is the whole of the button's inner
// geometry: (32 - 22) / 2 = 5 authored px on every side. The overlay's rows
// take that same derivation for the LEFT pad of their icon (a row is a wide
// button, so the pad is the box's own inset), never a second number — the
// modal WORD buttons' 9px text pad is a different surface's.
inline constexpr double kIconBtnPx    = 32.0;   // the button box, both axes
inline constexpr double kIconBtnGapPx = 2.0;    // between adjacent buttons
inline constexpr double kIconGlyphPx  = 22.0;   // the icon box inside the button

// A BUTTON IS SQUARE (architect 2026-10-02, the Windows-95 design): its box
// is the raised or sunken relief at relief_line_px, below, and no corner in
// the chrome is rounded.

// -- THE PLAY-SCRUB: A TROUGH WITH A THUMB (architect 2026-10-02) -------------
//
// The render player's modal row carries the transport's scrub, and its look is
// the trim lane's (his words: "a trough like the trim with a larger dot for the
// current position indicator"; the dot SQUARE — a round one would be the one
// circle in the chrome): a SUNKEN trough the trim lane's height
// (trim_lane_h_px) across the slider's track, centred in the button box's
// band; a RAISED bar in the trim bar's face (kTrimLaneBar) inside it from the
// track's start to the position, the played extent, the trough's ground
// beyond; and a RAISED SQUARE THUMB in the trim cap's face (kTrimLaneCap)
// centred on the position, the trough's height plus one relief line above and
// below — 13 x 13 logical, overhanging the trough by a row each way, the
// Windows slider thumb's overhang — the dark pair painted last. The painter is
// paint_modal_dialog's player branch. It reads no window focus: the bar
// carries no accent to dim.
//
// THE HANDLE'S BOX IS THE GRAB, NOT THE PICTURE: a 20 px box, the ONE owner of
// that length for its readers — the MAPPING insets the track by half of it at
// each end (the thumb's centre is the frame's position;
// render_player_scrub_x_of, app_state.h) and the press router takes it as THE
// HANDLE'S GRAB BAND. The painted thumb is the square above, inside it.
// Floored at 2 so the half-box inset is never zero and the band never
// degenerates.
inline constexpr double kScrubHandleBoxPx = 20.0;
inline int scrub_handle_box_px() {
    return scaled_px(kScrubHandleBoxPx, 2);
}

// ROW 5's THREE LANES — the TRIM lane, the RULER lane and the MARKER lane,
// stacked in that order under the icon row (the order is main.cpp's lane
// table), the marker lane's bottom edge the waveform's top with no gap. All
// three ride the gui_scale axis; the ruler's height is DERIVED from its label
// face (ruler_lane_h_px, below).
//
// THE TRIM LANE IS 11 AUTHORED ROWS (architect 2026-10-02, the frozen
// design): a SUNKEN trough one relief line a side (relief_line_px) with a
// 9-row interior, the bar RAISED inside it and the two handles and the centre
// grip SOLID RAISED SQUARES of the interior's height, 9 x 9 at 100 % — so the
// interior's 11 - 2 and the caps' width (trim_endcap_w_px) are the one square
// (the painter is render_trim_flags). The lane is ONE RECT for paint and for
// every hit reader — the endcap rects (trim_endcap_rect takes the lane rect's
// y/h), the bridge's y-gate and the framing double-click band — so paint and
// hit move together by construction. NO RETUNE FACTOR scales it: the trough,
// the bar and the square caps are one geometry, and a factor on the lane
// alone would break the squares.
inline constexpr int kTrimLaneHeightPx   = 11;
// THE RULER LANE'S HEIGHT IS DERIVED FROM THE LABEL FACE, NOT AUTHORED AND
// SCALED (architect 2026-10-02). The lane stacks, from its top:
//
//     lane = pad + ceil(ascent) + kRulerBaselineToMarkerPx rows
//
// — the labels' line seated so their CAP TOP lands kRulerLabelCapTopPx (6)
// authored rows under the lane's top (the pad, derived from the face's own
// ascent and cap height; paint_handler.cpp owns the rule), the face's ascent
// to the baseline (line_baseline), then TEN AUTHORED ROWS from the baseline to
// the marker lane's top (below). ONE HELPER seats the labels for both
// readers — the painter's baseline and this height (ruler_label_baseline_px,
// paint_handler.cpp) — so the two cannot disagree, and the face's metrics
// are read off the product's own road: the Sans face at
// ruler_label_font_size_px (8 pt) through gui_select_font_face, measured
// through cairo-ft on fonts/Roboto-Regular.ttf, SLIGHT, hint metrics on:
//   100 % (10.67 px, ascent 10, cap 8): pad 6 - 2 = 4, baseline row 14, cap
//     ink rows 6..13: lane 14 + 10 = 24.
//   200 % (21.33 px, ascent 20, cap 16; the tablet): pad 12 - 4 = 8, baseline
//     row 28, ink rows 12..27: lane 28 + 20 = 48.
//   50 % (5.33 px, ascent 5, cap 4): pad 3 - 1 = 2, baseline row 7, ink rows
//     3..6: lane 7 + 5 = 12.
// The major ticks' rise above the marker lane is the painter's own and does
// not enter the lane.
//
// THE BASELINE → MARKER LANE DISTANCE, TEN AUTHORED ROWS (architect
// 2026-10-02, the U4 mock: "above 6, below 10"), AND IT OWNS THE OVERLAP: the
// playhead head is 11 authored rows seated tip-down on the marker lane's top
// (kPlayheadHeadHeightPx), one row taller than this distance, so its widest
// row OVERLAPS the digits' lowest ink row at the playhead's column — allowed
// (his ruling, "a little overlap is fine"), the head painting over the
// labels. At 50 % the head's 6 rows meet a 5-row distance and overlap one
// device row the same way. The head's rows do not enter the lane's height.
inline constexpr int kRulerBaselineToMarkerPx = 10;
inline constexpr int kMarkerLaneHeightPx = 20;
inline int trim_lane_h_px() {
    return scaled_px(kTrimLaneHeightPx, 3);
}
// Defined in paint_handler.cpp beside the label seat it reads; the rule is
// the block above.
int ruler_lane_h_px();
inline int marker_lane_h_px() {
    return scaled_px(kMarkerLaneHeightPx, 5);
}

// THE WAVEFORM'S MAXIMUM HEIGHT — THE DEVICE CONFIG'S `max_waveform_height`
// since 2026-09-13 (architect: a per-device key in AUTHORED px, default 500 on
// both templates, 0 meaning no maximum; the range owner is
// is_max_waveform_height, device_config.h). Until that day it was this file's
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
// THE VALUE IS 550 -> 500 AT COMMIT B (the same dictation). The architect's
// standing bracket: "bigger than the height on the Pi, smaller than the
// waveform height on my external monitor"; his own scaling example at the
// revision was 4K at 200% gui_scale = 1000px of waveform, which this accessor
// produces by construction. At 100% scale the 1920x1080 laptop's leftover
// is well over the default, so the waveform CLAMPS at 500 and the two gaps
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
// strip's ONLY lane since the relayout's commit B): the monospace clock and
// the state cell at the left pad, and the MARKER-VERB GROUP + separator +
// marker walk + separator + four cardinal arrows + separator + transport
// three flush right (architect 2026-09-29; kMarkerVerbGroup in
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
// WHAT THAT SUPERSEDES: kBottomRowHeightPx = 50, itself DERIVED at the
// unification as the icon row's 32px button box (kIconBtnPx) plus twice the
// 9px margin row 8's ruled geometry centred at ((44 - 26) / 2 = 9) — a lane
// shrink-to-fit around the same box at row 8's own breathing room. The box is
// unchanged; the MARGIN is the icon row's 7 now ((46 - 32) / 2), which is the
// whole of the 51 -> 47 lane change. (Row 8's sampled kdenlive transport
// metrics — 26px boxes / 16px glyphs off transport.png — and its ruled 44
// content were superseded by the icon-row boxes at the unification; row 9's
// measured 31 died with that lane.)
//
// THE CSS BOX MODEL, ONE TOP ROW: the content is the icon row's 46 and a 1px
// row of ground sits OUTSIDE it on top (a 47px lane at 100%), on the WAVEFORM
// side — where row 8's border-top stood. NO LINE IS DRAWN THERE since
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
// tooltip's second line, each beside its painter's owner). Every row's text is 12pt through the existing
// points*4/3 convention = 16px at 100%, scaled on gui_scale_factor(). It lives
// here rather than in a painter's anonymous namespace because row 5's marker
// flags shape their labels inside render.cpp while the button rows shape
// theirs in paint_handler.cpp, and one design size cannot have two definitions.
//
// ROW 7 CONFIRMED IT INDEPENDENTLY, off row_7_text.png, which is worth recording
// because that row was the last one still carrying a differently-sized face:
//   * the crop's capital band is rows 10..21 and its baseline row 22, i.e. CAP
//     HEIGHT 12 and X-HEIGHT 9 (rows 13..21), with no partial rows on either
//     edge (the source is hinted, so the measurement is exact);
//   * our sans face at 16px reported cap 12 / x-height 9 — the crop's
//     numbers, not near them — in Liberation Sans, the face fontconfig
//     answered then, and Roboto, the product's own face since 2026-10-02,
//     reports the same two;
//   * a full offscreen re-render of the crop's own string at 16px, pen x=13,
//     baseline 22 fits the crop better than every neighbouring size, baseline
//     and pen tried (15 / 15.5 / 16 / 16.5 / 17 x 21/22/23 x 12..14).
//
// THE TEXT IS NOT FLOORED (architect 2026-08-10, with the gui_scale floor's
// move to 50): it scales straight to 6pt at 50 %, while the structural
// lengths keep scaled_px's per-metric floors so no 1 px line rounds to 0.
inline constexpr double kRedesignFontSizePt = 12.0;   // -> 16.0 px at 100%
inline double redesign_font_size_px() {
    return kRedesignFontSizePt * 96.0 / 72.0 * gui_scale_factor();
}

// THE CLOCK'S SIZE — AN EXCEPTION to the shared size above (the ruler's
// timestamps, below, are the other)
// (architect 2026-08-14, at his live test: the bottom row's timestamp drops to
// 11pt). It stays MONOSPACE, which is the cell's own ruled face and unchanged
// (kClockShape, paint_handler.cpp, carries that ruling); only the size moved,
// and it rides gui_scale_factor() through the same points*4/3 convention as
// every other string. A RETUNABLE like the 12 above — neither is sampled from
// a crop.
//
// THE NO-WIGGLE CELL RE-MEASURES ITSELF: the cell is a shaped widest-digit
// specimen and its memo keys on the SIZE it was measured at
// (clock_cell_width_px), so this smaller size simply produces a smaller cell,
// which the painter then re-centres in the lane. Nothing about the cell is
// authored in pixels, which is why the change is one constant.
inline constexpr double kClockFontSizePt = 11.0;   // -> ~14.67 px at 100%
inline double clock_font_size_px() {
    return kClockFontSizePt * 96.0 / 72.0 * gui_scale_factor();
}

// THE RULER TIMESTAMPS' SIZE — the product's second exception to the shared
// size, the clock's precedent (architect 2026-10-02, the S4 mock judged on
// the tablet): 8 pt Roboto through the same points*4/3 convention and the
// one face owner (gui_select_font_face, GuiFontFamily::Sans), on the ruler
// lane only. The lane's height is derived from this face (ruler_lane_h_px),
// so the size is the one constant to move.
inline constexpr double kRulerLabelFontSizePt = 8.0;   // -> ~10.67 px at 100%
inline double ruler_label_font_size_px() {
    return kRulerLabelFontSizePt * 96.0 / 72.0 * gui_scale_factor();
}

// THE MARKER FLAG's anatomy, measured off row_5_lane_3_marker_unselected.png
// (56x20 = a 1px left border plus a 55x20 fill box; the border's own record is
// at kMarkerFlagBorder) and confirmed against row_5_full.png, where the same
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
inline constexpr int kMarkerFlagPadRightPx = 2;
inline constexpr int kMarkerFlagPadLeftPx  = 2;
inline int marker_flag_pad_left_px() {
    return scaled_px(kMarkerFlagPadLeftPx, 1);
}
inline int marker_flag_pad_right_px() {
    return scaled_px(kMarkerFlagPadRightPx, 1);
}
// The 1px TOP EDGE, in the class's edge colour. The crops show no RIGHT and no
// bottom edge, which is why this is a band and not a ring; the LEFT side and
// the run's closing RIGHT column are the separate border below, in a colour of
// its own.
inline constexpr int kMarkerFlagEdgePx = 1;
inline int marker_flag_edge_h_px() {
    return scaled_px(kMarkerFlagEdgePx, 1);
}
// THE 1px LEFT BORDER (architect 2026-08-02), full box height, in
// kMarkerFlagBorder. The geometry clause that makes it a BORDER and not a wider
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
// at gui_scale 100, two at 225. No fill, no text and no stem (the stem is
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
// The label BASELINE, measured from the box's top edge. The crop's cap ink runs
// rows 4..15 of the 20 — a 12-row cap height, which is what the product's sans
// produces at 16px (Liberation then, Roboto since 2026-10-02, both 12) — so
// the baseline is row 16 and the remaining 4 rows are the descender band. Authored as a length rather than solved from font extents
// because the box height (kMarkerLaneHeightPx) is authored too: both come off
// the same crop and must agree with it, not with a font's internal leading.
inline constexpr int kMarkerFlagBaselinePx = 16;
inline int marker_flag_baseline_px() {
    return scaled_px(kMarkerFlagBaselinePx, 1);
}
// THE RELIEF LINE — ONE AUTHORED PX, the width of every raised, sunken and
// etched line in the chrome (architect 2026-10-02: the relief is thin, one
// logical line a side; the grammar is at the palette head): 1 device px at
// 100 %, 2 on the tablet, floored at 1 so it never vanishes at 50 %. The relief
// helpers (paint_relief_raised and its siblings) paint every line at it.
inline constexpr int kReliefLinePx = 1;
inline int relief_line_px() {
    return scaled_px(kReliefLinePx, 1);
}
// THE WELL'S BORDER, taken FROM the waveform area at its top and its bottom:
// TWO relief lines a side (the colours and the order at the row-6 palette
// block — Hilight over DkShadow on top, DkShadow over Hilight below), so 2
// authored rows at 100 % and 4 device rows on the tablet, and still two
// lines at 50 %.
inline int waveform_border_px() {
    return 2 * relief_line_px();
}
// THE WAVEFORM'S LINE WIDTH (architect 2026-09-27, "scale all, including the
// ruler ticks and the playhead head"): every vertical LINE on the waveform and
// the ruler scales with gui_scale — 1 up to 149 % (the floor holding 50 %), 2
// from 150 % through 250 % (the tablet's 225 % included; banker's rounding
// takes 2.5 to 2), 3 above that and 4 at the 350 % ceiling.
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

// THE TRIM LANE'S CAPS — the two end handles and the centre grip, each a
// SOLID RAISED SQUARE (architect 2026-10-02; the grip's old hollow is gone,
// CONFIRMED that day): 9 authored columns wide, the lane's interior height
// (kTrimLaneHeightPx's 11 less the trough's two relief lines), so 9 x 9 at
// 100 %, 18 x 18 on the tablet and 4 x 4 at 50 % (where the interior is 6 - 2
// rows). The grip paints only where it fits whole between the handles'
// inner edges (render_trim_flags), so it never covers a handle.
inline constexpr int kTrimMiddleSizePx  = 9;
inline int trim_middle_size_px() {
    return scaled_px(kTrimMiddleSizePx, 1);
}
// THE HANDLE'S WIDTH IS THE GRIP'S (architect 2026-10-01): one square, read
// again rather than a second number. The name is the endcap's — the handles
// cap the bar's two ends — and it is what trim_endcap_rect sizes a handle by
// and trim_bridge_gap insets the bar's interior by.
inline int trim_endcap_w_px() {
    return trim_middle_size_px();
}

// THE PLAYHEAD HEAD, ALIASED: 17 x 11 at 100 % (architect 2026-10-02 — the
// kdenlive crop's 19 x 12 head, row_5_lane_3_playhead.png, with its WIDEST
// row dropped, so the head is one row shorter and seated on the marker lane's
// top as before, its top row overlapping the digits' lowest ink row at the
// playhead's column; the overlap rule is kRulerBaselineToMarkerPx's). Its
// silhouette is a per-row HALF-WIDTH table, not a formula — the shape has
// doubled rows (y2/y3, y5/y6, y9/y10) that no linear ramp produces, so the
// pixels are transcribed and the table IS the drawing. Painting it as
// integer rectangles keeps it hard-edged at every scale, which a path fill
// would not. The painted width at any scale is the table's own arithmetic
// through playhead_head_half_px below (2 x 8 + the stem's width: 17 at
// 100 %, 34 on the tablet).
inline constexpr int kPlayheadHeadHeightPx = 11;
inline constexpr int kPlayheadHeadHalf[kPlayheadHeadHeightPx] = {
    8, 7, 6, 6, 5, 4, 4, 3, 2, 1, 1
};
// THE HEAD'S HEIGHT IN DEVICE ROWS, the ONE expression of it for the
// painter's row loop (paint_ruler_row). NO FLOOR: 11 authored rows reach 6 at
// the schema's own bottom (gui_scale 50, banker's rounding taking 5.5 to 6),
// and only a factor below 1/22 could empty the loop — outside the vocabulary
// entirely. The per-row HALF-WIDTH is where the floor lives
// (playhead_head_half_px below).
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
// table's last two rows are 1 authored px, which rounds to 0 at s = 0.5, and a
// half of 0 is a 1px row — the head's tip would collapse onto the stem and
// stop reading as a tip at all. Floored, the bottom row is 3 px wide there. A
// FLOOR, NOT A PIN: at 100% and above every scaled half is already >= 1, so
// nothing above the baseline moves and the tips keep scaling.
//
// THE HEAD IS CENTRED ON THE STEM AT EVERY SCALE (architect 2026-09-27): the
// stem is waveform_line_px() = t columns wide, [col, col + t), so each row
// takes the stem's parity, [col − half, col + half + t − 1], 2·half + t wide
// — odd at t = 1 as before, even at the tablet's t = 2 — integer and aliased,
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
// from the FACE'S OWN EXTENTS at both type sizes (one line, or 12pt over 10pt),
// so the box follows the font instead of a literal that could drift from it;
// the run loop only needs to know it can never exceed this. 60 clears the
// two-line form (52 at 100% in Roboto, 2026-10-02) with room for a font whose
// metrics run larger.
inline constexpr int     kTooltipDamageHeightPx = 60;
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
// half the platform's 8dp touch slop. It is taken as 4 AUTHORED px through
// scaled_px, the authored px being to gui_scale what the dp is to density:
// exactly 4dp on the tablet at its 200 % under the 320 density it runs at,
// and half the drag gate (kDragMovedThresholdPx 8, app_state.h) as Android's
// is half its touch slop. Floor 1, so a small scale never zeroes it.
inline constexpr int kTooltipHoverSlopPx = 4;
inline int tooltip_hover_slop_px() {
    return scaled_px(kTooltipHoverSlopPx, 1);
}

// (THE HOVER FADE IS RETIRED — architect 2026-10-02: "hover is awkward with
// pen and sometimes flickers, ok to drop it"; Windows 95 painted no hover
// face. Breeze's hover animation stood here from 2026-09-27 — the HoverFade
// edge-and-level model, its tick, kHoverFadeMs / kHoverFadeSteps and the
// menu pill's kMenuPillHoldMs hold — and it drove paint alone, so it went
// whole with the hover faces it softened. The hover BITS stay: tooltips, the
// menu row's hover switch and every press keep reading them. The record is
// in git history.)

// THE DROPDOWNS' VERTICAL metrics — one set for every menu, out here for the
// same reason the tooltip's height is: the popup's OPEN EDGE must damage the box
// before the box has ever been painted, and its HEIGHT is fully derivable
// without shaping a single label (item count x item height, plus the separator
// blocks and the two borders). Its WIDTH is not — that needs the widest shaped
// label — so the horizontal terms stay with the painter and the open edge
// damages full-width instead. dropdown_h_px (app_state.h) does the sum, where
// the item tables are visible.
inline constexpr int kPopupItemHeightPx = 29;  // measured off dropdown_full
inline constexpr int kPopupSepMarginYPx = 2;   // above and below the separator
// The item block's own margin inside the border, top AND bottom. The full crop
// puts the first item 3px below the container top; the bottom mirrors it, which
// the crop's own trailing space agrees with.
inline constexpr int kPopupItemMarginYPx = 3;
// THE DROPDOWN'S FRAME IS ONE RELIEF LINE a side (architect 2026-10-02: a
// menu is a RAISED panel), so its thickness is relief_line_px's — read, not
// a second number.
inline int popup_border_px() {
    return relief_line_px();
}
inline int popup_item_h_px() {
    return scaled_px(kPopupItemHeightPx, 5);
}
inline int popup_sep_margin_y_px() {
    return scaled_px(kPopupSepMarginYPx, 0);
}
inline int popup_item_margin_y_px() {
    return scaled_px(kPopupItemMarginYPx, 0);
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
// KEPT EXACT: the same authored unit, the same std::nearbyint, the same floor,
// so every pixel is identical at every gui_scale. 8 at 100%. The floor of 2 was
// the triangle's own ("always a tip row below a top row") and survives only to
// hold the value byte-for-byte; it cannot fire while gui_scale rests in
// [50, 350] (8 px reaches 2 only below 19%).
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
// shared by render.cpp (cull) and main.cpp (invalidation). 7 at 100%. It
// covers the scanner's waveform_line_px()-wide line [c, c + t) at every
// gui_scale: t − 1 is 0 to 3 across [50, 350] % while this reach is 3 to 27.
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
// kPlayheadHeadHalf[0] = 8 (2026-10-02) — 17px at 100%, 9 at 50%, 26 at 150%,
// 34 at 200% and 60 at the 350% ceiling — against this +/- 7-at-100% reach,
// which rides a
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
// CHROME in the ground (kRedesignContentGround), render_canvas erases the
// WAVEFORM AREA in the canvas (kWaveformCanvas). on_redraw calls the first
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
// edge grammar (architect 2026-10-02; the grammar at the palette head). Every
// line is relief_line_px() wide and is an integer rect of cells, never a
// stroke. A FRAME is drawn ON the rect's outermost ring: its top and left in
// `top_left`, then its bottom and right in `bottom_right`, the second pair
// painted last so it owns the top-right and bottom-left corner pixels.
//   paint_relief_frame  — the general frame, any two colours (the one owner
//                         the three below call).
//   paint_relief_raised — Hilight top/left, Shadow bottom/right: a button, a
//                         panel, the trim bar and its caps, a card, a menu.
//   paint_relief_sunken — Shadow top/left, Hilight bottom/right: a pressed or
//                         toggled button, the clock panel, a text field, the
//                         trim trough, the open menu anchor.
//   paint_relief_line_frame — one colour all round: the tooltip's DkShadow
//                         border and the dialog's default-button frame.
//   paint_relief_etched_vline — an ETCHED separator: a Shadow line ending at
//                         column x (its columns [x - lw, x)) and a Hilight
//                         line at [x, x + lw), rows [y, y + h).
//   paint_relief_etched_hline — the same on its side: a Shadow line on rows
//                         [y, y + lw) and a Hilight line under it, columns
//                         [x, x + w) (the dropdown's separator).
// None of them fills the face: a caller fills first and frames after.
void paint_relief_frame(cairo_t* cr, const GuiRect& r, GuiColor top_left,
                        GuiColor bottom_right);
void paint_relief_raised(cairo_t* cr, const GuiRect& r);
void paint_relief_sunken(cairo_t* cr, const GuiRect& r);
void paint_relief_line_frame(cairo_t* cr, const GuiRect& r, GuiColor c);
void paint_relief_etched_vline(cairo_t* cr, int x, int y, int h);
void paint_relief_etched_hline(cairo_t* cr, int x, int y, int w);
// One flat cell rect in `c` — the face fill every relief caller lays first.
void paint_cell_rect(cairo_t* cr, const GuiRect& r, GuiColor c);


// The waveform area's CONTENT band — THE CANVAS: the area minus the well's two
// lines at its top and its bottom (waveform_border_px). Every pass that fills
// a BAND inside the area clips to this — the plate blit and the region
// highlight's two halves, ground and ink — and EVERY VERTICAL that crosses the
// waveform stops at it (architect 2026-10-02: the stems no longer run through
// the well's lines): the marker stems in both columns and the `h` diff lane,
// the playhead's waveform segment and the scanner (render_playhead), and the
// strip-drag anchor stem (render_strip_anchor_stem). THE PHASE-RESET OVERLAY
// RING alone reads the full area: its horizontals ride the area's OUTERMOST
// rows deliberately (the ruling is at paint_phase_reset_overlay_ring).
// Degenerate areas (too short to carry both borders) pass through unshrunk
// rather than inverting.
inline GuiRect waveform_content_rect(GuiRect area) {
    const int b = waveform_border_px();
    if (area.h <= 2 * b) return area;
    return GuiRect{area.x, area.y + b, area.w, area.h - 2 * b};
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
// one word owner for the plate's writer (render_waveform) and the region's
// recolor (paint_region_ink), so the word the recolor keys on is bit for bit
// the word the writer stored.
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
// THE INKS ARE READ HERE rather than passed: the plate paints in
// kWaveformInk with the lamp dark, and lit both bars in kWaveformInk with the
// inner's outline in kWaveformForegroundOutline, two flat constants (row 6)
// — it
// is trim-agnostic, and the out-of-trim dim that
// once masked a second color through this alpha is retired, the trim bar
// spanning the window being the whole inside-the-window signal now. Its alpha
// is BINARY: opaque bars and transparent gaps, with no fractional edges left.
// The gaps are what let a recolored GROUND (kWaveformRegionCanvas, painted
// before the blit) show through, and the SET pixels are what the one
// remaining after-the-fact recolor reads: paint_region_ink rewrites each
// opaque plate pixel inside the region's column span as its own colour lifted
// by the region's step (region_lift), leaving the plate itself untouched.
//
// THE VISUAL MAGNIFICATION is `gain_or_null`, and the lit plate is TWO BARS
// per column from ONE peak read, both through the expander, the outer
// through the leveler and the inner through the compressor (architect
// 2026-09-25):
//
//   OUTER (painted first) = raw x g x E, in kWaveformInk, no outline — the
//                           levelled, expanded bar;
//   INNER (painted over)  = raw x c x 1/2 x E, in kWaveformInk outlined in
//                           kWaveformForegroundOutline — the source's bar
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
// NULL (the dark lamp) draws the raw bar alone in kWaveformInk at scale 1,
// with no outline, byte for byte the plate it always drew.
//
// THE LIT OUTLINE (architect 2026-09-27) — THE INNER BAR'S TRUE CONTOUR,
// `outline_px` THICK, ALIASED. `outline_px` is t = waveform_line_px() (render.h,
// the job's snapshot): 1 at 100 %, 2 on the tablet — the outline is a LINE
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
// written once with the fill's word or the outline's. paint_region_ink lifts
// outline pixels as it lifts every opaque plate pixel, from the pixel's own
// colour. Nothing else in this painter moves
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
                     const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr);

// Draws a waveform_line_px()-wide vertical LINE down the canvas of `area`
// (waveform_content_rect: it stops at the well's lines, architect 2026-10-02)
// at the column nearest `playhead_pixel_x` (offset from area.x), [col, col +
// t), in one solid
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
void render_playhead(cairo_t* cr,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color);

// Draws the strip-drag ANCHOR STEM: a vertical line at the drag's pivot
// column `col` (window pixels within `area`, clamped here to [0, area.w-1]),
// spanning the canvas like a marker stem (waveform_content_rect), in
// kPlayheadStem since 2026-08-01 — the product's one position-line white
// (the ruling is at the paint site).
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

// The BETWEEN-THE-ENDCAPS column interval [lo, hi) (waveform-relative,
// half-open, EMPTY when hi <= lo), the ONE owner of the bridge, run by the
// painter (render_trim_flags) alone: its midpoint-mark fit test reads it, and
// the same clipped interval is what the painter PUBLISHES as the pair drag's
// handle (TrimBarHit::bridge_lo / bridge_hi, read by point_in_trim_bridge_span),
// so the bridge's clickable band and the mark's clearance are one interval. The bar itself no longer
// comes from here — it spans the WINDOW, bound column to bound column, and the
// endcaps paint over its ends. Both bounds must be set (callers gate). The
// offscreen arms key on the bound's SIDE (TrimBoundColumn::side, the unrounded
// verdict) — NOT col_raw, which cannot tell the side across the rounding seam
// (a barely-off-left bound rounds to col_raw == 0). The 4x2 semantics:
//   BEGIN — the gap's LEFT edge, a left-edge-anchored endcap:
//     InView (endcap painted) -> lo = col + endcap_w         (the drawn endcap's
//        inner RIGHT edge; the gap starts just past the endcap).
//     OffLeft (no endcap)   -> lo = min(col_raw, -1)        (a STRICTLY NEGATIVE
//        flush sentinel: the fill clips flush to column 0 AND the left ring border
//        lands offscreen — true only via the sentinel; raw col_raw == 0 would
//        float the border at the edge).
//     OffRight (no endcap)  -> lo = max(col_raw, wave_w)     (>= wave_w: nothing
//        paints in the visible [0, wave_w) and the router's [0, wave_w) gate can
//        never arm — an empty gap in the visible area).
//   END — the gap's RIGHT edge, a right-edge-anchored endcap:
//     InView (endcap painted) -> hi = col - endcap_w + 1      (the drawn endcap's
//        inner LEFT edge, exclusive).
//     OffRight (no endcap)  -> hi = max(col_raw + 1, wave_w + 1)  (a PAST-THE-EDGE
//        flush sentinel: the fill clips flush to the right edge AND the right ring
//        border lands offscreen).
//     OffLeft (no endcap)   -> hi = min(col_raw + 1, 0)      (<= 0: empty against
//        any lo >= 0 — closes the one-pixel bridge a raw col_raw == 0 left, which
//        gave hi = 1 and painted/accepted a column-0 sliver for a window wholly
//        left of the viewport).
// The +endcap_w inset is the ROOM a PAINTED endcap — a handle since 2026-10-01,
// trim_endcap_w_px() wide — occupies; an offscreen bound
// paints no endcap, so the inset is dropped and the bar fills FLUSH. This interval
// is returned UNCLAMPED (raw sentinels included) — its role is to carry the
// offscreen-flush and empty semantics past the visible edge; it is NOT a drawn
// interval. The painter clamps it to the visible range ONCE: it intersects it
// with the effective width [0, wave_w) before asking whether the midpoint tile
// fits, and publishes that same clipped interval as the bridge's hit span. So the inert non-multiple-of-16 gutter [wave_w, strip_w) neither
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

// The ONE trim HANDLE screen-rect owner (named for the endcaps the handles
// replaced on 2026-10-01 — they still cap the bar's two ends): the begin/end
// edge-anchoring rule lives here, run by the painter (render_trim_flags),
// which publishes each handle it fills for the hit test (hit_test_trim_endcap
// reads TrimBarHit, below), so paint and hit are one rect.
//
// A trim bound is an EDGE, not a point: the begin handle's LEFT edge sits ON
// the bound column (rect left = strip_x+col), the end handle's RIGHT edge sits
// on it (rightmost pixel = strip_x+col), each flush with the bar's end. The
// handle is trim_endcap_w_px() wide — the centre grip's 9 at 100% — and its
// y-band is the trim lane `row`. Deliberate asymmetry vs centered marker
// flags: a bound at frame 0 / EOF shows its handle fully onscreen.
//
// THE HIT TEST INFLATES THIS by kTrimEndcapGrabPx per side (10, what each
// retune of it costs the bridge is recorded at the constant). A 9px target is
// still under a fingertip, so the drawn handle and the grabbable one are
// deliberately NOT the same rect — the one place in this lane where they
// differ, stated here because everywhere else in the redesign they are
// identical by construction.
GuiRect trim_endcap_rect(bool is_begin, int strip_x, int col, GuiRect row);

// Grab tolerance added to EACH SIDE of the drawn handle for hit-testing. The
// handles are 9px (2026-10-01), so this makes the target 9 + 2*10 = 29px. ONE CONSUMER reads it (re-grepped 2026-10-01): the TRIM BAR's handles
// (hit_test_trim_endcap).
// The WAVEFORM OVERLAY's two bounds read it too from 2026-08-18 until the
// resting overlay and its drags were deleted on 2026-09-22 (the tablet's pen
// reaches the trim bar).
//
// 10 SINCE 2026-08-19, AND SETTLED THERE (architect). THE OVERLAY WAS THE
// REASON IT CAME BACK UP: the waveform overlay's bound bands existed precisely
// because the 10 px trim bar is unusable with a fingertip, so 5 per side
// reproduced ON THE FINGER'S OWN SURFACE the very problem that surface was
// built to solve — while 15 was more than the THIN trim lane wants. The walk: 4 from row 5's landing, chosen to
// reproduce the retired square chip's width; 10 on 2026-08-14 (architect:
// "endcaps are very useful and currently too small", leaning 6 to 10 and ruling
// 10 — THE TOUCH PANEL IS THE REASON, a fingertip being nothing like a 10px
// target); 15 on 2026-08-15, once both lanes had been driven on glass; 5 on
// 2026-08-18, narrowing that after driving the unified region/trim.
//
// WHAT THE BAND'S WIDTH DECIDES, checked against every neighbour the endcap
// claim can overlap, because that claim OUTRANKS everything else in the lane
// (the per-grab figures are re-derived from the rules below, not carried):
//   * THE TRIM BRIDGE is reachable only where the gap survives both inflated
//     handles: the window's drawn width (end column − begin column + 1) must
//     exceed 2·9 + 2·grab, so the narrowest window that still has a bridge is
//     39 columns at 10 (re-derived 2026-10-01 for the 9px handles). What the
//     bridge loses is zoom-recoverable
//     rather than a lost capability (the window's drawn width is a zoom state,
//     both bounds stay independently draggable at every zoom, and the band's
//     framing double-click is tested ABOVE the router so it is untouched).
//   * THE TWO TRIM CAPS AGAINST EACH OTHER are unaffected in KIND at any width:
//     the sort's
//     leftmost-wins/Begin-first arbitration makes End's exclusive reach
//     (end_col − begin_col − 1 columns to the right of Begin's band, or one
//     column to its left when the bounds coincide) a function of the BOUNDS
//     alone — the grab cancels out of both sides — so every verdict a
//     coincident or near-coincident pair gives is the same at 10 as at 5 or 15,
//     just nearer the column. A pair exactly one column apart is the one
//     unreachable End, and it is unreachable at every grab.
inline constexpr int kTrimEndcapGrabPx = 10;
inline int trim_endcap_grab_px() {
    return scaled_px(kTrimEndcapGrabPx, 0);
}

// (render_trim_stems IS DELETED, architect 2026-08-01. It drew the WAVEFORM-AREA
// portion of the trim bounds — a 1px grey vertical at each bound's column,
// spanning the waveform, meeting the strip-crossing segment at the waveform top
// to form one unbroken line. THE BAR AND ITS TWO ENDCAPS ARE THE WHOLE DISPLAY
// now: the redesigned trim lane states the window at the window, and two
// full-height verticals competing with the marker stems stated it a second time
// in the same pixels. The `trim_stem` config key it painted from outlived it by
// a day and died with the whole tunable palette on 2026-08-02.)

// THE TRIM BAR'S HIT STASH (architect 2026-09-24, strictly as-painted): what
// render_trim_flags last PAINTED as the bar's two handles, published by
// that painter into AppState::trim_bar_hit so the trim hits read the pixels
// rather than re-running the painter's owner chain on the live trim — the
// flag lane's stash doctrine (AppState::flag_hit_rects) carried to the trim
// lane. Everything is in SCREEN pixels. `lane` is the band the bar was painted
// in, the y-gate of both hits. Each handle (a TrimBarHitCap) is its DRAWN rect
// (trim_endcap_rect, uninflated — the hit applies trim_endcap_grab_px itself,
// the one place the drawn and the grabbable rect differ) plus its bound
// column, which is the leftmost-wins sort key, and `painted` is false for a
// bound the viewport culled, which paints no handle and so answers no hit.
// The bridge is the half-open interval [bridge_lo, bridge_hi) between the
// handles' inner edges,
// already clipped to the lane's painted width (trim_bridge_gap, the owner the
// midpoint mark fits against); empty when lo >= hi. `published` false is
// COLD — nothing painted, nothing grabbable.
struct TrimBarHitCap {
    bool    painted = false;
    int     col_x   = 0;        // the bound's screen column
    GuiRect rect{0, 0, 0, 0};   // the drawn handle
};
struct TrimBarHit {
    bool          published = false;
    GuiRect       lane{0, 0, 0, 0};
    TrimBarHitCap begin;
    TrimBarHitCap end;
    int           bridge_lo = 0;   // screen x, inclusive
    int           bridge_hi = 0;   // screen x, exclusive
};

// Draws the WHOLE TRIM BAR LANE (architect 2026-10-02, the Windows-95 design;
// the geometry at kTrimLaneHeightPx, the colours at the palette's row 5): a
// SUNKEN trough, the window's RAISED bar inside it, the two handles over the
// bar's ends and the centre grip last. All pixel-bound integer fills through
// the relief helpers, no stroke and no antialiasing anywhere in this lane.
// The lane band is the `trim_bar` PARAMETER — the caller passes
// top_trim_row_area(app) (top-strip lane 2), and the band painted in is
// published as TrimBarHit::lane, the y-gate both trim hits read, so paint and
// hit take the band as one value and cannot drift; nothing in here re-derives
// the lane's y from the row heights above it. `trim_bar` gives the
// lane's x/y/h; `waveform_area` is read for its `.w` ALONE — both the
// column-mapping denominator and the lane's effective width, so the inert
// non-multiple-of-16 gutter is outside the clip and never paints.
// `top_strip_area` is now a validity guard only: nothing in this lane measures
// from the strip's own bottom any more.
//
// PAINT ORDER IS BACK TO FRONT, which is what lets each run ignore its
// neighbours: the TROUGH across the whole lane (the ground, then its sunken
// frame — a Shadow line along the top row and a Hilight line along the bottom
// row, its side lines laid outside the window so it runs past both edges),
// then the BAR spanning the window over the trough's interior rows — ONE
// SOLID RAISED OBJECT, kTrimLaneBar under a one-line relief, light along its
// top and left, dark along its bottom and right, the shared corners dark
// (DrawEdge's order) — then the two handles over the bar's ends. An inverted
// or degenerate window simply leaves the trough showing.
// THE BAR SPANS THE WINDOW ITSELF, bound column to bound column, and FOLLOWS AN
// OFFSCREEN BOUND rather than stopping short — an out-of-view bound means the
// window continues past that edge, so the bar runs flush to it, its side edge
// one column past the lane where the clip trims it. It is the one "this is
// the trim window" signal and the visual affordance of the pair (bridge)
// drag's grab band.
// THE TWO HANDLES AND THE GRIP ARE SOLID RAISED SQUARES in kTrimLaneCap, the
// interior's height and trim_endcap_w_px wide. The handles always paint unless
// the viewport culls them (the window is always set since 2026-07-30),
// EDGE-ANCHORED on their bound columns with their bodies facing inward, flush
// with the bar's ends: the begin handle's LEFT edge on its column, the end
// handle's RIGHT edge on its own. A bound is an EDGE, not a point — the
// deliberate asymmetry vs centered marker flags — so a bound at frame 0 / EOF
// shows its handle fully onscreen. A culled bound paints no handle at all: it
// has no column on screen to stand on, and the bar's flush edge is what says
// the window continues past the view.
// Both handles come from the ONE rect owner (trim_endcap_rect) and are
// published as filled — their columns at the lane's height — so the painted
// handle and the grabbable one describe the same edge; the hit side adds only
// its stated grab tolerance. Column placement is on the displayed viewport
// basis — `trim.begin` / `trim.end` are already in the displayed domain, so no
// further translation happens here. A handle has NO editable payload; it is a
// plain-press grab target only (trim is outside the selection system).
// THE CENTRE GRIP paints last, on the bar at the WINDOW's midpoint column —
// through the same trim_bound_column owner the bounds use, so it scrolls off
// the view with the window instead of sliding to the middle of whatever is on
// screen. Its ONLY hide rule is TOO NARROW TO FIT: the whole square must sit
// inside the visible interior BETWEEN the handles (trim_bridge_gap, clamped
// to the effective width) — a binary verdict on integer columns, so it cannot
// flicker, and below the threshold it simply does not paint (no shrink, no
// clamp). It is otherwise INFORMATIONAL: no hit rect, no gesture, no routing
// change anywhere. Its width is trim_middle_size_px.
// PUBLISHES WHAT IT PAINTS into `out_hit` when non-null (TrimBarHit above):
// the lane, both handles and the bridge interval, from the very columns this
// pass fills — and a cold record on every early return, since a lane that
// painted no bar has nothing to grab. The CALLER decides whether this frame
// may publish at all (GuiPaintHandler::paint_trim passes null unless the
// damage clip covers the whole lane), so a narrow repaint cannot stamp the
// stash over pixels it did not redraw.
void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect trim_bar,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
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
// (GuiPaintHandler::paint_marker_stems) and the playhead's white-stem
// suppression decider (GuiPaintHandler::playhead_stem_suppressed), both
// paint-side, so a stem and its flag can never disagree about a column.
// The published COLOUR is the marker's resolved face — its class, brightened
// when its flag box is (architect 2026-09-23); the consumer applies
// exactly one override over it, the open flag editor's invalid-commit red flash
// (a transient the painter has no business baking into a cache — the contract is
// at GuiPaintHandler::paint_marker_stems).
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
// THE KDENLIVE TEXT-ON-FLAG FORM (row 5, 2026-08-01): each flag is a filled box
// whose FILL's LEFT EDGE stands on its marker's pixel column, spanning the whole
// marker lane vertically, carrying a 1px top edge in its class's edge color and
// the marker's own composed label in the redesign's sans face. The width is
// DERIVED from the shaped label (pad + shaped + pad); the anatomy, the pad and
// the warp payload's scale truncation live at kMarkerFlagPadXPx above.
//
// PLUS A 1px LEFT BORDER OUTSIDE THAT FILL (architect 2026-08-02),
// kMarkerFlagBorder, full box height, standing one column LEFT of the frame
// column so THE STEM KEEPS THE FILL'S LEFTMOST COLUMN. It is one value across
// every LIVE class and takes the disabled blend with the rest of the face (the
// ladder below). The published hit rect is the whole box, border included; the
// geometry, the left-edge clip and the colour's provenance are at
// marker_flag_border_px and kMarkerFlagBorder.
//
// AND ONE CLOSING COLUMN AT THE RUN'S RIGHT (architect 2026-09-25): every
// marker's run — the flag box alone, or the flag box and its bound cells —
// ends on ONE border column past its RIGHTMOST box, in that box's own
// face.border, so a run is bordered one column each side. Its INTERIOR seams
// stay single: between the flag box and the lower cell, and between the two
// cells, the one column is the next cell's own left seam, never a closing
// column beside it. The hit rect covers the closing column (its last column,
// reading as the box it closes); the geometry is at marker_flag_border_px.
//
// OVERLAP IS LATER-OVER-EARLIER IN STORE ORDER and there is NO OTHER OCCLUSION
// MANAGEMENT AT ALL — no elision, no z-lift for selection, no run arbitration.
// That is the whole model the marker-text lane's resolver used to stand in for,
// and it is deliberately the simplest thing that can be true: a later marker's
// box covers an earlier one's tail, and the user pans or zooms to read it. The
// closing column is what keeps that tail from blending into the later flag's
// fill. (A SELECTION LIFT — selected flags painted in a second pass over
// unselected ones — stood for one day, 2026-09-25, and was struck by the
// architect as poor design; the closing column answers the blend it reached
// for.)
//
// COLOR CLASSES, resolved in priority order. DISABLED WINS, then red, then the
// default/selected pair:
//   Disabled:  every surface of the flag BLENDED 25% over the lane ground
//              (kMarkerDisabledMix, through mix_color) — fill, top edge, LEFT
//              BORDER (architect 2026-08-02) and label alike — and NO STEM.
//              The border has no per-class variant to choose, so the blend is
//              simply applied to the one border colour; everything else about
//              it is the fill's own operation. It blends the marker's OWN class, so
//              a disabled red marker stays red; disabled decides the blend and
//              the missing stem, not the hue. (The old ladder's disabled was a
//              separate opaque PAIR, which is why red used to test `!dis`.)
//              SELECTION IS PART OF "ITS OWN CLASS" (architect 2026-08-01): a
//              selected disabled marker blends the SELECTED pair, fill and edge
//              both, so it carries the same relative lift a live marker's
//              selection gives — the disabled rendition of the selected face.
//              RED TAKES THE LIFT TOO since 2026-09-16, exactly as the live
//              red class does: the class ladder is untouched, so a disabled
//              selected red marker is the disabled rendition of the BRIGHT
//              red and still reads red and still reads switched off.
//   Red:       kMarkerFlagFillRed / kMarkerFlagEdgeRed at rest, swapping to
//              kMarkerFlagFillRedSel / kMarkerFlagEdgeRedSel on the addressed
//              cell of a selected marker (architect 2026-09-16 — red joins
//              the other classes' rest/selected shape, on the same `selected`
//              bit, the cue being the HUE and never the brightness); stem
//              kMarkerStemRed at rest and kMarkerFlagFillRedSel selected,
//              following the fill like every other stem (architect
//              2026-09-23); border kMarkerFlagBorder undamped, like every
//              live class. RED STAYS RED ON BOTH COLUMNS — the column
//              fork below never reaches this arm.
//   Otherwise: kMarkerFlagFill / kMarkerFlagEdge on the WARP flag box and its
//              bound cells, or
//              kPhaseResetFlagFill / kPhaseResetFlagEdge on the PHASE-RESET
//              flag box and its bound cells (architect 2026-09-21: the cells
//              wear their own column's hue, superseding the 2026-09-15
//              purple on either column) (`FlagColumnFace`,
//              resolve_flag_face's fourth argument — REQUIRED, never
//              defaulted, since warp is never the unmarked default: every call
//              site names its column explicitly — the phase-reset flag box
//              and its cells pass `PhaseReset`), swapping to the bright Sel
//              pair on any column when selected — SELECTION IS THAT SWAP AND NOTHING
//              ELSE. The stem wears the flag box's fill, the calm one at rest
//              and the bright one selected (architect 2026-09-23).
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
// UPPER, each painted exactly as the flag box is — the marker's own class,
// the seam column on its left, the top edge, the lane's ink — carrying the
// bound in its signed two-decimal form (format_iter_bound_cell). A cell
// reads as another flag payload, and the sign is its whole syntax: a flag
// payload never carries one and a cell always does, so no bracket or
// separator opens in one cell to close in the next. The flag's own text is
// the plain composer's (flag_text) in every state; no bracket paints anywhere.
//
// `focus_marker` / `focus_cell` NAME THE BRIGHT CELL (architect 2026-09-05):
// a selected marker paints its ADDRESSED cell in the selected pair and its
// other cells in its ordinary class pair, and the addressed cell is the
// payload for every selected marker but the focus, whose addressed cell is
// `focus_cell` (AppState::addressed_cell — a press's cell, an editor's, or
// the one a bracket-only undo entry's restore brings back; every other focus
// route resets it to the payload). Where the focus SHOWS that cell NOWHERE —
// neither in this pass nor in the open field standing in for it — its payload
// is bright instead, so a selected marker always shows its selection, and
// shows it once: while a field stands, the FIELD is where its own cell's
// brightness lives. The rule is stated once at
// the selected pair's palette block (kMarkerFlagFillSel) and applies on both
// columns — a phase reset's bound cells are cells too. Disabled and red
// blend cell by cell through the same ladders; the border reads the class
// alone and the stem the flag box's face, so it brightens exactly when the
// payload does.
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
// THE BOX WEARS THE MARKER'S OWN FACE: the class fill, the top edge and the 1px
// left border render_flags would have given it (disabled blend, red, selected
// swap, all through the one ladder; the border class-invariant), so opening an
// editor changes the flag's SIZE and nothing else about how it reads — and
// since no field buys a caret column, even the size only changes where the
// resting label was capped, the field opening at the committed run's own width
// and growing only with what is typed past it. An
// invalid commit flashes the marker lane's OWN red class, in its BRIGHT pair —
// kMarkerFlagFillRedSel / kMarkerFlagEdgeRedSel, the invalid red being the
// bright red by the 2026-09-16 ruling that gave that class a rest pair, so the
// flash is as loud as it ever was and never the calm red a resting coincident
// marker wears. The three DIALOG editors flash that same pair too
// (since 2026-08-02, as the flag-anatomy box on the bottom strip; since
// 2026-08-12 as the dialog FIELD's recolor, fill under the 1px top edge —
// paint_modal_dialog), so there is one
// invalid red in the product; the pre-redesign dark-red chip
// pair they used to flash was the last tunable colour in the tree and went with
// the whole palette-config system the next day.
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
// architect 2026-09-17). It asks the one class ladder (resolve_flag_face,
// render.cpp) rather than restating it: kMarkerStemRed when `red` (the
// column's red set, phase_reset_red_flag_set_cached), the column's calm fill
// kPhaseResetFlagFill otherwise, and when `selected` the Sel face's stem —
// which IS the bright fill (kMarkerFlagFillRedSel, kPhaseResetFlagFillSel) —
// so the ring brightens with its stem: they are one object (architect
// 2026-09-23, reversing the same day's "the ring is not a selection cue").
// `selected` is the bit the flag pass hands the reset's payload face, which is
// what its stem reads. No disabled arm, a disabled reset painting neither stem
// nor ring.
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
// case is exactly "both": one double-width flag, the removed half left and red,
// the added half right and green.
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
// THE ANATOMY IS THE LIVE FLAG'S, and since 2026-08-22 the class ladder is
// narrower in its LIVE half alone — there are two diff classes where the live
// lane has three, but the DISABLED RUNG is shared: the 1px left
// border outside the fill (kMarkerFlagBorder, class-invariant here as there), a
// full-lane-height fill, a 1px top edge, and the label on the redesign's sans at
// the lane baseline IN THE LANE'S OWN BLACK INK (kMarkerFlagLabel, 2026-08-20 —
// the anatomy is shared, so the ink is too; the ruling covers every colour and
// every state, this mode's green and red included). A CHANGED pair is that same box at double width — the
// halves' own fills and top edges side by side, ONE border column wrapping the
// whole at its left, and — SINCE 2026-08-20, a trial ruled standing
// 2026-09-02 — a SECOND column of that same border ink ON THE SEAM between them,
// against the depth illusion two adjacent saturated hues produce (the ruling
// and the rationale are at the paint site and at THE SEAM COLUMN block in the
// palette above). The pair is
// still ONE flag: one rect, one focus, one claim. The top edge splits with the halves because it is part of each
// half's face; it runs horizontally and so is never a divider.
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
// in the TEXT stays the local byte) — paints through the LIVE
// LANE'S OWN DERIVATION applied to this lane's inks — no new constant anywhere:
// fill and top edge at kMarkerDisabledMix over kRedesignContentGround, the label
// at kMarkerDisabledLabelMix from kMarkerFlagLabel toward its own dimmed fill,
// exactly the expressions resolve_flag_face runs. THE SELECTION SWAP HAPPENS
// FIRST AND THE DIM APPLIES OVER IT, as it does live: the focused pair is chosen,
// then damped, so a focused disabled half lifts like a live focus and still
// reads switched off.
//
// IT SPLITS HONESTLY ON A CHANGED PAIR: each half takes its own bit, so a
// disable TOGGLE paints one dimmed half beside one full-strength half and the
// direction of the toggle is readable off the flag itself. The THREE BORDER
// COLUMNS follow from what each one belongs to — the box's own left border is the
// LEFTMOST PAINTED HALF's face element (the live lane's anatomy: border outside
// fill) and dims with that half, the flag's CLOSING column at its right (the
// live lane's run rule, architect 2026-09-25) is the RIGHTMOST PAINTED HALF's
// and dims with that one, while the SEAM divider belongs to neither half
// alone and dims only when BOTH are disabled. The hit rect covers all three.
//
// `focus_index` is the mode's OWN focus (at most one flag, -1 for none) and
// `selected` its OWN multi-selection (ordinals into the same list, 2026-08-05):
// EITHER swaps that flag to its class's selected pair, both halves of a double
// flag together. One face for both, deliberately — the focus is the selection's
// singleton when the set is empty, and the revert act reads them the same way, so
// a second brightness would be a distinction nothing acts on. The STEM reads the
// class and that same swap, exactly as the live lane's does (architect
// 2026-09-23: the class's Sel fill on a focused or selected flag) — and a
// CHANGED pair stems RED, deferring to the old. THE STEM ALSO READS THE DISABLED
// AXIS NOW (architect 2026-08-22): a SINGLE flag — added-only or removed-only —
// whose one side is EFFECTIVELY disabled publishes NO STEM AT ALL, the live
// lane's rule
// verbatim, while a CHANGED PAIR KEEPS ITS STEM whichever halves are disabled,
// because the pair as a whole is a live EDIT being displayed rather than a line
// in a switched-off state.
// NO CELLS AND NO ADDRESSED CELL ON THIS LANE: a bracket is session-only and
// in no commit, so a diff flag carries no bound cells, and the view's focus is
// its own diff-flag cycle's with nothing for AppState::addressed_cell to
// address — the focus swap lifts the whole half, as it always has.
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

