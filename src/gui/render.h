#pragma once
#include "warpmarkers.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment for target-view waveform

#include <cairo/cairo.h>
#include <cmath>
#include <cstdint>
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
// compile-time. RGB only (the renderer uses cairo_set_source_rgb); if an
// alpha channel is ever needed, add a separate 0xRRGGBBAA overload rather
// than widening this one.
inline constexpr GuiColor hex(uint32_t rgb) {
    return GuiColor{
        static_cast<double>((rgb >> 16) & 0xFF) / 255.0,
        static_cast<double>((rgb >>  8) & 0xFF) / 255.0,
        static_cast<double>( rgb        & 0xFF) / 255.0,
    };
}

// THE ONE COLOR-MIX OWNER: `own` retained by keep_own, the remainder made up
// with `toward`. keep_own == 0 returns `toward` exactly (the `own` term is
// annihilated), and keep_own == 1 returns `own` TO WITHIN ONE ULP — near enough
// that a call site meaning "unchanged" costs nothing and says so.
//
// NOT BIT-IDENTICAL AT keep_own == 1, and this line used to claim it was: the
// form is toward + (own - toward), and the subtraction is exact only when the
// two channels are within a factor of two of each other (Sterbenz). Where they
// are not — kWaveformInk over #fcfcfc, the channel split line — the result
// misses `own` by an ULP. It has never mattered and cannot: every consumer hands
// these doubles straight to cairo, which quantizes to 8 bits, and an ULP never
// survives that. Stated so no future caller builds an equality test on the old
// claim. Used by the redesign's DISABLED FACE — the icon paths (icons.cpp)
// and the label (paint_handler.cpp) resolve through this single expression, so
// the two halves of a greyed button can never dim by different arithmetic.
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
// The GUI's colors, shared across the renderer module, the paint handler, and
// main.cpp. THE WHOLE PALETTE IS HARD-CODED (architect 2026-08-02): every
// constant here and in the redesign blocks below is constexpr, there are no
// user-settable colors, nothing is read from ~/.config, and a retune is a
// recompile. Every painted surface in the product takes its value from one of
// these constants.
//
// WHAT WAS HERE BEFORE, in one paragraph, because this file's shape is its
// residue. The palette used to be 23 MUTABLE globals overwritten once at startup
// from ~/.config/warptempo_gui/colors.conf by a strict whole-file loader
// (src/gui/color_config.{h,cpp}), so the scheme could be retuned without a
// build. The kdenlive redesign (rows 1-7, 2026-07-31..08-01) sampled every
// surface straight from its screenshot crops and hard-coded the result, which
// emptied that tunable domain key by key until only the chrome erase and the
// scanner line still reached a pixel; on 2026-08-02 the architect retired the
// remaining system WHOLE — the loader, the file grammar, the ~/.config support
// and the 21 by-then-unread keys are all deleted, and the two survivors are
// hardcoded below at exactly the values the conf carried, so the retirement
// moved no pixel. A colors.conf left on disk is simply never read: no stderr, no
// migration, no recognition of any kind. The deleted keys' values and their
// per-key provenance records live in the git history and nothing in the product
// needs them back.
//
// EVERY ENTRY IS OPAQUE — the palette carries no compositing alpha at all, and
// the redesign kept the doctrine: a highlight recolors the GROUND UNDER the ink
// (kWaveformRegionCanvas) rather than washing over it, so ink over a highlighted
// span is the same ink and only the ground reads the highlight; a disabled face
// resolves to a solid color through mix_color before it reaches cairo, never a
// fade.

// THE BASE CHROME ERASE (render_background) — and the surviving half of the
// GROUND SPLIT: this goes under everything, and the redesigned rows then paint
// their own sampled grounds over it, so its visible remit is whatever chrome no
// redesigned surface covers. The waveform area's ground is the row-6
// kWaveformCanvas and has been since 2026-08-01; this color never reaches it.
//
// THE VALUE is Breeze Window, the desktop's own panel color — the scheme this
// palette has carried since the 2026-07-26 breeze trial, whose polarity the
// redesign then overrode surface by surface.
inline constexpr GuiColor kBackground      = hex(0x202326);

// THE MOVING PLAYBACK LINE, drawn by paint_scanner while the scanner runs (its
// own pass since 2026-08-01: it paints OVER the marker stems, where the resting
// cursor paints under them).
//
// THE VALUE reads WHITE against the canvas — the Ableton play-head cue, and also
// Breeze's text/icon foreground, so it is the scheme's brightest ink. The
// redesign's kRedesignLabel and kPlayheadStem hold the same #fcfcfc, sampled
// independently from the crops: three facts that agree, not one referenced three
// times.
inline constexpr GuiColor kPlayheadScanner = hex(0xFCFCFC);

// -- The redesigned rows (HARD-CODED, kdenlive-sampled) ---------------------
//
// THE COLORS THAT WERE NEVER PALETTE KEYS (architect 2026-07-31). Every constant
// in this block and the two below it arrived constexpr and deliberately
// untunable, outside the config grammar that then still existed — the carve-out
// that grew until it was the whole palette and the grammar retired (the header
// above). Their provenance is the pixel truth of the kdenlive crops
// (tmp/screenshots/kdenlive/redesign/), sampled directly, and the screenshots
// OVERRIDE the Breeze-derived scheme wherever the two disagree. Each row's own
// crops are named at the constants that row introduced.
//
// THE NAMES ARE ROW-INDEPENDENT because the values are: rows 1 and 2 share the
// same ground, the same accent and the same label white, so a per-row name
// would go stale at the next row that reuses one. Each row states which of
// these it paints where.
//
// The row ground is a DELIBERATE MISMATCH with kBackground (#202326): the
// kdenlive bars sit a shade lighter than this product's chrome and the crop
// wins, so do not "fix" it to the chrome value. The accent is Breeze blue at
// full saturation — #3daee9, which was also the closed form behind the retired
// `marker` key's 30% hover wash — carried as row 1's FILLED hover pill and row
// 2's 1px hover OUTLINE; the label white is Breeze's paper white #fcfcfc. Both
// are spelled out here rather than borrowed, because these are screenshot
// samples that happen to coincide with values the old tunable palette also
// carried, not references to anything. The LINE is row 2's separator and its
// border-bottom, one sampled value for both (they are the same rule seen twice —
// a 1px inert structural edge); the retired `line` key's #686a6c was a different
// value for a different era's rules.
inline constexpr GuiColor kRedesignRowGround = hex(0x292C30);
inline constexpr GuiColor kRedesignAccent    = hex(0x3DAEE9);
inline constexpr GuiColor kRedesignLabel     = hex(0xFCFCFC);
inline constexpr GuiColor kRedesignLine      = hex(0x535659);

// THE UNFOCUSED GROUND for rows 1 and 2 (architect 2026-07-31, from live use).
// The crops' #292c30 is Breeze's FOCUSED header shade; when the WINDOW LOSES
// KEYBOARD FOCUS the header darkens to #202326, tracking the labwc titlebar
// above it, which darkens on the same edge. A HARD SWAP — no transition, no
// fade — driven by AppState::window_activated, and it moves the GROUND ONLY:
// separators, border lines, the accent, labels and icons all keep their colors.
// Row 3's ground is ALREADY this value and therefore does not change at all.
//
// NOTE it coincides with kBackground and with kRedesignTabGround (all three
// sample the same Breeze Window color) and is nonetheless its OWN constant by
// the hard-coded rule — three facts that happen to agree, not one fact
// referenced three times.
inline constexpr GuiColor kRedesignRowGroundUnfocused = hex(0x202326);

// ROW 2'S CLICK FACE (row_2_button_click.png): the pressed button's interior,
// sampled #2f5368 and found to be EXACTLY 30% kRedesignAccent over the row
// ground —
//   r: 0.3*61  + 0.7*41 = 47   (0x2f)
//   g: 0.3*174 + 0.7*44 = 83   (0x53)
//   b: 0.3*233 + 0.7*48 = 103.5 -> 104 (0x68)
// so what the crop pins down is a RELATIONSHIP to the ground, not a fifth
// independent color, and the factor is what ships. THAT IS WHY THE FILL DERIVES
// FROM THE CURRENT GROUND rather than being frozen at the sampled hex: over the
// focused ground this reproduces #2f5368 bit-for-bit (the crop is honored
// exactly), and over the UNFOCUSED ground it applies the same measured 30% tint
// to the ground actually present — where a frozen literal would leave the
// pressed button lighter relative to its darker surroundings, i.e. louder
// unfocused than focused, which is not what the crop says. The 1px accent
// outline and the label over it are unchanged from the hover face.
inline constexpr double kRedesignClickMix = 0.30;

// ROW 2'S DISABLED FACE (row_2_disabled.png): every ink a disabled button
// paints — the icon paths in their own colors AND the label — RETAINS this
// fraction of itself over THE ROW'S CURRENT GROUND, through the one mix_color owner
// above. ONE shared factor for both halves. MEASURED off the crop's label, whose
// full-coverage pixels read #6d6f72 = (109, 111, 114) over the (41, 44, 48)
// ground with a (252, 252, 252) label, solving per channel to
//   (109-41)/(252-41) = 0.3223,  (111-44)/(252-44) = 0.3221,
//   (114-48)/(252-48) = 0.3235
// — one factor within a quarter-percent on all three, so 0.322 reproduces
// #6d6f72 bit-for-bit. It is the ARCHITECT-TUNABLE knob for how dead a disabled
// button looks; the crop's dimmed record-red confirms the same factor carries
// hue (a mix toward the ground desaturates without rotating). Named a MIX
// rather than an alpha on purpose: the palette is fully opaque and nothing here
// composites — the factor resolves to a solid color before it reaches cairo.
// A SECOND READER SHARES THE KNOB DELIBERATELY: the tab row's UNLOCKED padlock
// dims by this same factor over the tab's current face (paint_handler.cpp's
// tab-lock body) — the redesign's ONE dim family reused rather than a second
// grey invented there — so retuning how dead a disabled button looks retunes
// the quiet open lock with it.
inline constexpr double kRedesignDisabledMix = 0.322;

// -- Row 1's RIGHT-FLOATING VIEW BAR (HARD-CODED, kdenlive-sampled) ---------
//
// kdenlive's workspace switcher — the blue bar at the far right of its menu row
// ("Logging | Editing | Audio | Effects | Color") — reborn here as the three
// absolute view selectors. Sampled off the nine 82x32 row_right_*.png crops,
// each one whole "Logging" button.
//
// TWO BAR BACKGROUNDS, ONE PER WINDOW-FOCUS STATE: the bar swaps on
// app.window_activated exactly as rows 1 and 2 swap their ground, and the swap
// is the same kind of thing — a PAINT-ONLY variant of the whole surface. The
// crops named "disabled" are the UNFOCUSED WINDOW, not a disabled button: these
// three are never disabled (redesign_button_enabled returns true for them, with
// rows 1, 3 and 4), so there is no dimmed face here at all.
//
// kRedesignViewBarBgUnfocused is NUMERICALLY EQUAL to kRedesignRowGround
// #292c30 and is NOT it: that constant is the FOCUSED CHROME ground, this one is
// the UNFOCUSED BAR, sampled from kdenlive's own unfocused crop. Two facts that
// happen to agree, like kRedesignTabGround and kBackground — a retune of one
// must not follow the other.
inline constexpr GuiColor kRedesignViewBarBg          = hex(0x1E5774);
inline constexpr GuiColor kRedesignViewBarBgUnfocused = hex(0x292C30);

// EVERY FACE IS A RELATIONSHIP TO THE BAR BACKGROUND, never a frozen literal —
// the kRedesignClickMix arrangement, adopted for the same reason it exists
// there: this surface has TWO backgrounds, and a hex sampled against one of
// them would read wrong over the other.
//
//   REST           — the bar background itself, flat, no frame. The crop's rest
//                    button is INVISIBLE because it IS the div showing through,
//                    which is why the painter draws no fill for it.
//   HOVER          — a 1px kRedesignAccent frame, and NOTHING ELSE, on both
//                    grounds: hover moves the OUTLINE only (architect
//                    2026-08-02, from the live test). THIS SUPERSEDES A CROP —
//                    row_right_disabled_hover lifts its interior to #44464a
//                    where row_right_hover keeps the flat #1e5774, and that
//                    asymmetry was reproduced faithfully until he ruled the
//                    simpler rule. The lift below is the SELECTED fact alone
//                    now; everything else on this surface is still the crops'.
//   CLICK          — the interior at kRedesignClickMix toward the accent, the
//                    same 30% row 2 and row 4 paint, over the bar background.
//   SELECTED       — the bar background lifted an eighth toward #fcfcfc, under
//                    a frame lifted a fifth.
//   SELECTED+HOVER — the selected fill under the accent frame.
//
// THE TWO LIFTS FIT THE CROPS PER CHANNEL. Focused bg (30, 87, 116) toward
// (252, 252, 252):
//   fill  1/8 : r 31.5 + 26.25  = 57.75  -> 58  (0x3a)
//               g 31.5 + 76.125 = 107.63 -> 108 (0x6c)
//               b 31.5 + 101.5  = 133.0  -> 133 (0x85)   = #3a6c85, exact
//   frame 1/5 : r 50.4 + 24.0   = 74.4   -> 74  (0x4a)
//               g 50.4 + 69.6   = 120.0  -> 120 (0x78)
//               b 50.4 + 92.8   = 143.2  -> 143 (0x8f)   = #4a788f, exact
// Unfocused bg (41, 44, 48), same two fractions:
//   fill  1/8 : 67.375 -> 67, 70.0 -> 70, 73.5 -> 74     = #43464a
//               vs the crop's #44464a — ONE UNIT LOW ON RED, identical on the
//               other two channels.
//   frame 1/5 : 83.2 -> 83, 85.6 -> 86, 88.8 -> 89       = #535659, exact
// Five of the six channel triples land bit-for-bit and the sixth is off by a
// single LSB, so the FRACTIONS ship — one relationship over both backgrounds —
// rather than four literals that would have to be kept in step by hand. That
// LSB is the whole cost of the derivation and it is not visible.
//
// FRACTION AND BASE ARE BOTH KNOBS, the kWaveformChannelSplit arrangement: the
// base is what the bar lifts TOWARD, spelled out here rather than borrowed from
// kRedesignLabel because it is a sampled coincidence and not a reference.
inline constexpr GuiColor kRedesignViewBarLiftBase     = hex(0xFCFCFC);
inline constexpr double   kRedesignViewBarSelectedMix  = 0.125;
inline constexpr double   kRedesignViewBarFrameMix     = 0.20;

// -- Row 3, the TAB ROW (HARD-CODED, kdenlive/Breeze-sampled) ---------------
//
// Sampled off tmp/screenshots/kdenlive/redesign/row_3_tab_{rest,hover,selected}
// .png (30 px tall), row_3_tab_pcmanfmqt.png (the padding/geometry reference)
// and row_3_bottom_border.png. Same carve-out as the four above: constexpr, not
// config keys, the crop wins over any Breeze-derived scheme.
//
// kRedesignTabGround is BOTH the row's ground outside the tabs AND the selected
// tab's interior — one value by the architect's ruling ("the background of the
// row outside the tabs = the selected tab's background minus the blue trim"),
// which is what makes the selected tab read as seamless with the row. ROW 4 (the
// icon row) PAINTS ON IT TOO, and shares the constant rather than declaring a
// fourth copy of the value: the icon row is literally the surface the selected
// tab opens into through its broken bottom border, so that is one fact seen
// twice rather than two samples that agree. NOTE that
// it COINCIDES with kBackground #202326 (both sample Breeze's Window color);
// it is its OWN constant by the hard-coded rule, never a reference to the
// palette, and a retune of one must not follow the other.
//
// kRedesignTabLine is a SECOND structural line grey, distinct from row 2's
// kRedesignLine #535659: the tab frame and the row's bottom border measure
// #4c4e51 in every crop. Two constants, both sampled, neither derived from the
// other.
inline constexpr GuiColor kRedesignTabGround    = hex(0x202326);
inline constexpr GuiColor kRedesignTabRest      = hex(0x1B1D20);
inline constexpr GuiColor kRedesignTabHover     = hex(0x263F4D);
inline constexpr GuiColor kRedesignTabHoverEdge = hex(0x496170);
inline constexpr GuiColor kRedesignTabLine      = hex(0x4C4E51);

// -- Row 4, the ICON ROW's one new color -----------------------------------
//
// The SELECTED (toggled-on) button's interior, sampled #3c3f41 off
// row_4_button_selected.png. A literal, not a derivation: nothing clean
// generates (60,63,65) from this row's ground and the accent — it is Breeze's
// own "button pressed/checked" shade and stands as its own sample, which the
// architect explicitly allowed.
//
// THE TWO GREYS CROSS ROLES ON THIS ROW, and that is worth stating because it
// looks like a mistake otherwise: row 4's SEPARATORS and its border-bottom are
// #4c4e51 (kRedesignTabLine, row 3's frame grey) while its selected OUTLINE is
// #535659 (kRedesignLine, row 2's separator grey) — the opposite pairing to
// rows 2 and 3. Both are measured off row 4's own crops; the constants are
// reused rather than re-declared because the VALUES are the same Breeze pair,
// and only the roles moved.
inline constexpr GuiColor kRedesignSelectedFill = hex(0x3C3F41);

// -- Row 5: the TRIM lane, the RULER lane, the MARKER lane ------------------
//
// All sampled from tmp/screenshots/kdenlive/redesign/row_5_*, and all
// HARD-CODED under the architect's blanket ruling — which now reaches even
// marker and waveform territory. At the time this row landed the colors.conf
// machinery still stood and merely went inert as each painter that read a key
// died; the machinery itself was retired whole on 2026-08-02 (the palette
// header carries that record).
//
// The three lanes share row 3's #202326 ground (kRedesignTabGround), one fact
// seen again rather than a fourth copy of the number.

// THE TRIM LANE is 9 rows of exactly THREE surfaces — ground, bar, endcap — and
// each carries its own 2-row BOTTOM BEVEL: row 7 a lighter shade, row 8 a
// darker one. The bevel is NOT a derivable rule (the three measured pairs fit
// neither a constant delta nor a constant mix toward white/black), so it ships
// as six sampled constants, one pair per surface. A FOURTH surface would have
// no pair and would force the question then, which is the point of spelling
// them out rather than inventing a formula from three samples.
inline constexpr GuiColor kTrimLaneBar       = hex(0x2F6888);
inline constexpr GuiColor kTrimLaneEndcap    = hex(0x97B4C4);
// THE MIDPOINT MARK NEEDS NO COLOUR OF ITS OWN (architect 2026-08-01, second
// pass — he overlaid row_5_lane_1_trim_middle.png on the running GUI and ruled
// the crop implemented VERBATIM): the 9x9 crop is exactly a LANE-HEIGHT TILE
// built from the two surfaces this block already declares — face rows 0..6 in
// kTrimLaneEndcap #97b4c4 with a 5x5 kTrimLaneBar #2f6888 square inset at cols
// 2..6 / rows 2..6, over the endcaps' own #9dbbcb / #94b0c0 bevel pair. On our
// dark bar that reads as a LIGHT SQUARE RING with a dark centre, which is the
// mark he approved in the mockup.
//
// The former kTrimMiddle constant (a lone #97b4c4 fill for a 5x5 square) is
// DELETED with the deviation it recorded — that deviation reasoned about which
// half of a two-colour crop to keep when only ONE colour could be painted, and
// the tile paints BOTH, so the question it answered no longer exists. The
// painter's tile is the record now (render_trim_flags, render.cpp).
inline constexpr GuiColor kTrimGroundBevelHi = hex(0x393E43);
inline constexpr GuiColor kTrimGroundBevelLo = hex(0x131516);
inline constexpr GuiColor kTrimBarBevelHi    = hex(0x3B7696);
inline constexpr GuiColor kTrimBarBevelLo    = hex(0x286180);
inline constexpr GuiColor kTrimCapBevelHi    = hex(0x9DBBCB);
inline constexpr GuiColor kTrimCapBevelLo    = hex(0x94B0C0);

// THE RULER LANE's two inks. The label size is the redesign's ordinary 12pt:
// the composite's label band measures 12 ink rows and ~84px for its label, which
// is what 16px sans produces — the brief's impression that kdenlive's ruler font
// "looks smaller" is not what the pixels say, and the measurement wins.
inline constexpr GuiColor kRulerLabel = hex(0xC2C2C2);
inline constexpr GuiColor kRulerTick  = hex(0x737373);

// THE PLAYHEAD's three. The HEAD is an aliased shape in a single flat grey; the
// STEM is the paper white that replaces the old cursor line at this surface.
// THE TICK-THROUGH-HEAD value is a PRE-BLENDED CONSTANT, never a runtime alpha:
// where a ruler tick's column crosses the head, those head pixels paint #b7b7b7
// (183,183,183, measured off row_5_lane_3_playhead_tick.png) instead of the
// head's own grey. The opaque-palette doctrine has no compositing to offer, and
// a measured blend is exact where an alpha would only approximate it.
inline constexpr GuiColor kPlayheadHead     = hex(0x8E8F91);
inline constexpr GuiColor kPlayheadHeadTick = hex(0xB7B7B7);
inline constexpr GuiColor kPlayheadStem     = hex(0xFCFCFC);

// THE MARKER LANE's colors, measured off row_5_lane_3_marker_{unselected,
// selected,red}.png (56x20, and 56x17 for red). Each class is a FILL plus a
// 1px TOP-EDGE color, and the box carries a 1px LEFT BORDER outside that fill
// (kMarkerFlagBorder, below, where its provenance is recorded); there is no
// right and no bottom outline in any crop.
//
// SELECTION IS A COLOR SWAP AND NOTHING ELSE (row 5): a selected marker paints
// the bright pair, an unselected one the calm pair, and the geometry, the stem
// and the hit rect are identical either way. This RETIRES the "selection is not
// a class" ruling for the marker flags — that rule existed because a selected
// OUTLINE would have outranked the disabled pair; here the swap can never
// outrank disabled, because since 2026-08-01 it happens INSIDE it: a selected
// disabled marker blends THIS pair toward the lane ground through
// kMarkerDisabledMix, so it lifts like a live selection and still reads
// switched off, and the defect the old rule guarded against has no site left.
//
// The RED crop is 56x17 and supplies COLORS ONLY — its dimensions are the
// regular class's (the architect's own instruction).
inline constexpr GuiColor kMarkerFlagFill        = hex(0x9B59B6);
inline constexpr GuiColor kMarkerFlagEdge        = hex(0x563165);
inline constexpr GuiColor kMarkerFlagFillSel     = hex(0xC974ED);
inline constexpr GuiColor kMarkerFlagEdgeSel     = hex(0x704083);
inline constexpr GuiColor kMarkerFlagFillRed     = hex(0xFF6C7B);
inline constexpr GuiColor kMarkerFlagEdgeRed     = hex(0x8E3C44);
// The RED class's STEM is NOT its fill: #da4453 is the architect's own explicit
// value for it, so the red stem is its own constant while the default and
// selected classes both stem in kMarkerFlagFill (a selected marker keeps the
// CALM stem — also his explicit rule, which is why the stem color is resolved
// from the class ALONE and never from the selection bit).
inline constexpr GuiColor kMarkerStemRed         = hex(0xDA4453);

// THE BOX'S 1px LEFT BORDER (architect 2026-08-02). COLUMN 0 of all three
// marker crops is #131516 for the crop's whole height: identical in the
// unselected, the selected and the 17-row red shot, and the composite
// row_5_full.png shows the same column standing at x=22 for exactly the box's
// rows 37..56 with the fill starting at 23. CLASS-INVARIANT AND
// SELECTION-INVARIANT ACROSS EVERYTHING THE CROPS SHOW, which is why it is ONE
// sampled constant rather than a fourth fill/edge-style pair per class: red,
// selected and default all border in this exact value.
//
// BUT IT IS PART OF THE FACE ON THE DISABLED AXIS (architect 2026-08-02, second
// pass — he overturned this constant's first reading, which called it a purely
// structural edge that the disabled blend had no hue of its own to mute): "not
// literally with alpha, but via color mix — I would expect the border to be
// mixed with the row 5 lane 3 background color in the same way the fill was
// done". So a disabled marker's border goes through the SAME mix_color owner,
// at the SAME kMarkerDisabledMix fraction, toward the SAME marker-lane ground
// the fill and the top edge take, and the resolved value rides FlagFace like
// they do (resolve_flag_face, render.cpp). The crops are not contradicted: none
// of them shows a disabled flag, so what they pin down is the LIVE ladder, and
// that half of the reading stands exactly as measured.
//
// THIS SUPERSEDES THE BLOCK'S ORIGINAL READING of that column as a CROP-EDGE
// ARTIFACT (row 5, 2026-08-01). A stray edge pixel would not be uniform, full
// height and identical across three separately taken crops at two different
// heights, nor would it reappear mid-composite at x=22. The kdenlive flag is a
// css-style box — border OUTSIDE fill — so the 56px crop is 1 border + a 55px
// box, not 1 stray + 55.
//
// (The value coincides with kTrimGroundBevelLo's #131516 one lane up. Two
// samples that agree, not one fact referenced twice — the hard-coded rule.)
inline constexpr GuiColor kMarkerFlagBorder      = hex(0x131516);

// THE DISABLED FACE OF A MARKER IS A BLEND, NEVER AN ALPHA (architect): 25% of
// the class color over the lane ground (kRedesignTabGround #202326), per
// channel, through the ONE mix_color owner. Alpha would be wrong here for a
// reason specific to this lane — flags OVERLAP, so a translucent disabled flag
// would show its neighbour through itself and read as a third color.
//
// "THE CLASS COLOR" INCLUDES THE SELECTION SWAP (architect 2026-08-01): the
// pair entering this blend is the one the marker would paint LIVE — red's,
// the selected pair's, or the calm default's, resolved by the live ladder's own
// order — so a selected disabled marker takes the same relative lift a live one
// does, border included, and red takes none on either side. One blend, one
// ladder: there is no separate disabled brightness rule to drift.
inline constexpr double kMarkerDisabledMix = 0.25;

// -- ROW 6: THE WAVEFORM ITSELF ---------------------------------------------
//
// Measured off row_6_waveform_full.png (741x338), row_6_waveform_border.png
// (1x2) and row_6_waveform_filename.png (635x15). THE BLANKET HARD-CODING
// RULING REACHES THE WAVEFORM AREA (architect 2026-08-01): the canvas ground and
// the waveform ink were the LAST two colors the shrinking colors.conf domain was
// contracting TOWARD, and the crops take them too. The old `canvas` and
// `waveform_ink` keys kept their declarations for one more day under the
// then-standing inert-conf rule and lost only their PAINT SITES, of which each
// had exactly one: render_canvas (this file) and the two render_waveform calls
// in waveform_cache.cpp.
//
// Row 6 itself took only the ground and the ink; the waveform area's OTHER
// tunables did not outlast the same day's work: the region highlight re-derived
// onto kWaveformRegionCanvas, the phase-reset overlay ring onto kMarkerFlagFill,
// the marker classes onto the row-5 kMarkerFlag* constants, and the trim lane
// onto its own sampled surfaces. THE WHOLE TUNABLE SYSTEM RETIRED THE NEXT DAY
// (2026-08-02) — every key above is deleted along with the loader and the config
// file itself; the record is at the palette header.
inline constexpr GuiColor kWaveformCanvas = hex(0x12312B);  // (18, 49, 43)
inline constexpr GuiColor kWaveformInk    = hex(0x1C816B);  // (28, 129, 107)

// THE CHANNEL SPLIT LINE (architect 2026-08-01): a 1px horizontal rule across
// the waveform at the L/R channel boundary — which is also the SCRUB BOUNDARY,
// the row that divides the upper half's region drag from the lower half's scrub
// press, so the gesture split finally has a visible edge. It paints in both
// views, always.
//
// DERIVED, NOT SAMPLED: there is no kdenlive crop for it (kdenlive draws no
// such rule), so what the value is, is a RELATIONSHIP to the waveform's own two
// colors — taken through the ONE mix_color owner, with the BASE and the
// FRACTION both left standing as the architect's knobs.
//
// IT IS THE WAVEFORM INK ITSELF (architect 2026-08-02, "use waveform color"):
// base kWaveformInk #1c816b, fraction 1.0 — fully retained, so the rule reads
// as the waveform's own green drawn across the channel boundary.
//
// "FULLY RETAINED" IS EXACT ON THE PIXEL, NOT ON THE BIT, and the distinction is
// recorded because the mix owner's contract oversells it: at keep_own == 1 the
// expression is toward + (own - toward), which for THIS operand pair lands one
// ULP off kWaveformInk on red and blue (the subtraction is inexact — the two
// values are far apart in magnitude, so Sterbenz does not apply). Both quantize
// to #1c816b, which is what cairo paints, so the drawn rule IS the ink; only the
// doubles differ, and nothing here compares them.
//
// THIS SUPERSEDES THE PALE CANVAS-LIFT. The value was
// mix_color(kWaveformCanvas, #fcfcfc, 0.75) — the canvas lifted a quarter of
// the way toward white, approximately #4d645f — chosen on 2026-08-01 when he
// asked for something "clear enough to see when choosing where to click, not
// highly noticeable" after rejecting a first #2f9e44 as "lime green". The live
// pass settled it the other way: his own 2026-08-01 framing named the family as
// "canvas or waveform [ink] mixed with fcfcfc", and this is that family's OTHER
// member at full strength. The prior expression is recorded here rather than
// left in the tree.
//
// BOTH HALVES REMAIN TUNABLE, which is why the mix survives a fraction of 1.0
// instead of collapsing to a bare alias: the FRACTION is still the subtlety
// knob (lowering it washes the ink toward the base below, exactly as the old
// 0.75 washed the canvas) and the BASE is still the family choice. A call site
// that means "unchanged" costs nothing and says so — the mix owner's own
// contract.
inline constexpr GuiColor kWaveformChannelSplit =
    mix_color(kWaveformInk, hex(0xFCFCFC), 1.0);

// THE REGION HIGHLIGHT, RE-DERIVED ON THE NEW GROUND (architect 2026-08-01: the
// old value read GREY on the green canvas — "start over, don't just tune it;
// leave it more greenish").
//
// DERIVED, NOT SAMPLED — and deliberately so: there is no kdenlive reference for
// it, because kdenlive has no comparable highlight. What is transplanted is the
// RELATIONSHIP, not the colour. The old region highlight #42474d was the old
// grey canvas #393e43 plus Breeze's own View -> ViewAlternate lift, +9/+9/+10 per
// channel; applying that same lift to the crop's canvas keeps the
// theme's-native-lift logic and lands same-hue and subtly lifted on the green,
// which is what the grey pair was on the grey.
//
// THE LIFT IS DOUBLED (architect 2026-08-01: "the waveform highlight should be
// brighter"). One native step was too quiet to find on the green ground, so the
// step is applied TWICE — still the theme's own relationship, taken twice, not
// a tint invented for it:
//     kWaveformCanvas (18, 49, 43) + 2*(9, 9, 10) = (36, 67, 63) = #24433f
// (one step gave (27, 58, 53) = #1b3a35, the value this constant held between
// the row-6 re-derivation and the tweak).
//
// THE ARCHITECT'S TUNING KNOB, explicitly: the lift is a derivation and not a
// measurement, so this constant is the one to move if the highlight wants to be
// stronger or weaker. Nothing else in the region path has a value to tune.
//
// IT IS AN OPAQUE GROUND RECOLOR, NOT A BLEND — the same mechanism it always
// was (paint_region_ground, painted BEFORE the plate blit): the span's canvas
// is REPLACED by this colour and the ink then composites over it exactly as it
// composites over the plain canvas. Since the aliasing deletion the plate's
// alpha is BINARY, so an ink pixel is fully opaque and a gap is fully
// transparent: the highlight shows through the gaps and the ink is bit-identical
// either way. "The ink looks unaffected" is now structurally true rather than a
// near-miss, which is why nothing in the ink path is touched here.
inline constexpr GuiColor kWaveformRegionCanvas = hex(0x24433F);  // (36, 67, 63)

// THE AREA'S BORDER: 2px of pure black at the top and the bottom, full window
// width. Both rows of row_6_waveform_border.png are (0,0,0), and the full crop's
// rows 0-1 are black across all 741 columns with the canvas starting at row 2.
//
// ONLY THE TOP BORDER IS CROP-PROVEN: row_6_waveform_full.png ends inside the
// waveform (its last rows are plain canvas), so the bottom border is the
// architect's instruction rather than a measurement, applied symmetrically.
//
// TAKEN FROM THE AREA, NOT ADDED TO IT — the shape render_canvas already used
// for the 1px grey border it replaces, kept deliberately. The CSS reading says
// a border sits OUTSIDE the stated content, and it does: waveform_content_rect
// is that content, and it shrinks by these rows. What does NOT move is
// waveform_area itself, so the lane stack, the strip geometry, the effective
// width, samples-per-pixel and every column mapping are bit-identical to before
// row 6 — the border costs 2 rows of INK HEIGHT and nothing else. Shrinking the
// area instead would have rescaled the plate and moved every basis that divides
// by it, for a border drawn at the same pixels either way.
// (The gui_scale accessors for every row-6 LENGTH live with the other scaled
// accessors below — gui_scale_factor is not declared yet at this point in the
// header, exactly as for rows 1-5.)
inline constexpr GuiColor kWaveformBorder   = hex(0x000000);
inline constexpr int      kWaveformBorderPx = 2;

// THE FILENAME OVERLAY IS REMOVED (architect 2026-08-01, at the row-6 live
// look) — a retirement record, not a parked feature. It shipped for one look:
// the source wav's basename on a dark #0b1d1a band at the waveform's top-left,
// 15 rows flush under the border, white 12pt through the shaping chokepoint,
// and it reproduced row_6_waveform_filename.png to the pixel (ink rows 0..14,
// digit rows 0..11, pads 2/2, verified offscreen). IT COLLIDES WITH MARKER
// STEMS AT OUR DENSITY, and both z-orders read wrong — the band cuts the stems
// or the stems cut the band. Kdenlive's own markers are sparse enough that the
// question never arises there, so this is a place where design parity is
// correctly LOOSE. There is no replacement and none is wanted; the crops stay
// in tmp/ and this paragraph is why re-deriving from them would be a
// re-litigation rather than a discovery.

// THE ANTIALIASED PLATE RENDERER IS DELETED (architect 2026-08-01, at the
// side-by-side against a snapshotted AA binary: "subtle but noticeable — I
// prefer without it", reversing the keep-it-inert ruling this constant was
// built for). The waveform is drawn ALIASED: hard per-column min/max bars, no
// coverage anywhere. The toggle died with the choice it existed to make; the
// technique it selected between is recorded in
// docs/engineering/waveform_antialiasing_retired.md, and the deletion inventory
// is at render_waveform's own header.

// -- ROW 7: THE BOTTOM ROW (the status line) --------------------------------
//
// Measured off row_7_text.png (407x33): a 1px #4c4e51 TOP border, 31 rows of
// #202326 ground, and a 1px #17181a BOTTOM border which is the window's LAST
// row. Two DIFFERENT line greys on the two edges — that is what the crop says,
// verified column by column (every one of the 407 columns carries both values),
// so the row does not get one border color applied twice.
//
// THE GROUND AND THE TOP LINE ARE THE ROW-3/4 CONSTANTS REUSED, on the judgment
// those rows already set: #202326 is kRedesignTabGround (the tab row's ground,
// the selected tab's interior, the icon row's ground — one Breeze Window fact
// seen again, not a fifth sample of the same number), and #4c4e51 is
// kRedesignTabLine (row 3's frame grey, row 4's separators and border). Only the
// BOTTOM edge is new, and it is new because no surface in the tree has ever
// carried it: a near-black seam under the whole window.
inline constexpr GuiColor kRedesignBottomLine = hex(0x17181A);

// -- The TOOLTIP CHROME (the dropdown has its own, below) -------------------
//
// One chrome for both floating surfaces, measured off hover_shift.png (129x41)
// and hover_plain.png (112x26), which are byte-identical in every chrome pixel:
// kRedesignRowGround #292c30 fill under a 1px kRedesignLine #535659 border,
// with rounded corners. Nothing new is declared for those two — they are the
// same samples rows 1 and 2 already carry, reused rather than re-spelled,
// because a popup IS a floating piece of the same chrome.
//
// THE POPUP GROUND DOES NOT FOLLOW WINDOW FOCUS, unlike rows 1 and 2: a popup
// exists only while the window owns the pointer or the keyboard, so the
// unfocused shade has no state to appear in.
//
// THE SECOND TOOLTIP LINE IS DIMMED, and by ONE factor like the disabled face:
// its full-coverage pixels read #97989a = (151, 152, 154), solving per channel
// against the (41, 44, 48) ground and the (252, 252, 252) label to
//   (151-41)/211 = 0.5213,  (152-44)/208 = 0.5192,  (154-48)/204 = 0.5196
// — 0.52 reproduces #97989a bit-for-bit through the shared mix_color owner.
// MEASURED AND REJECTED: kdenlive is described as emphasising the word "Shift"
// in that line, and the crop does NOT — sampling the brightest pixel in every
// 10px band across the line gives one flat value (0x78..0x97, i.e. AA variation
// around a single ink), so the line ships uniformly dim.
inline constexpr double kRedesignDimMix = 0.52;

// -- The SETTINGS DROPDOWN's own chrome (its own crops) ---------------------
//
// THE MENU IS NOT THE TOOLTIP. kdenlive dresses the two differently and the
// dropdown_full crop is the authority for this one: ground #1c1f22 under a 1px
// #4c4e51 border, where the tooltip is #292c30 under #535659. The tooltip's
// constants are UNCHANGED — its own crops pinned them — and these are new.
//
// #1c1f22 is ONE LSB from kRedesignTabRest #1b1d20 and is NOT it: two
// independent samples that happen to land next to each other, kept apart under
// the hard-coded rule so a retune of one cannot drag the other.
//
// The BORDER value equals kRedesignTabLine's #4c4e51 — the same Breeze line
// grey playing a third role — and reuses that constant rather than declaring a
// fourth copy of the number.
inline constexpr GuiColor kRedesignPopupGround = hex(0x1C1F22);

// THE HOVER OUTLINE of a dropdown item: kRedesignAccent lightened 15% toward
// white, sampled #5abaec = (90, 186, 236) and reproduced exactly —
//   r: 61  + 0.15*(255-61)  = 90.1
//   g: 174 + 0.15*(255-174) = 186.2
//   b: 233 + 0.15*(255-233) = 236.3
// so it ships as the FACTOR through the one mix_color owner, not as a literal.
// The item's hover FILL is kRedesignClickMix (30%) accent over the popup
// ground — (38, 74, 94) = the crop's #264a5e exactly — one more instance of the
// same ratio the row-2 click face established, over a different ground.
inline constexpr double kRedesignHoverLightenMix = 0.15;

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
// [100, 200] (100 = the 1920x1080 design baseline, 200 = the 4K case); the
// current value lives as file-scope state in render.cpp, pushed by three
// application points (file load, the settings editor's `gui_scale=` commit, the
// `'` adopt).
//
// EVERY PAINTED DIMENSION IN THE TREE RIDES IT: crop-measured 100% values are
// the authored constants, and every conversion rounds with std::nearbyint.
void   set_gui_scale_percent(int percent);

// Scale factor s = gui_scale / 100. Exactly 1.0 at the default (and never below
// it: the setting's grammar floors at 100).
double gui_scale_factor();

// One authored 100%-scale length -> device pixels, the ONE conversion every
// scaled dimension in the tree takes: std::nearbyint like every other
// integer-domain conversion. Every scaled accessor below (and the painters'
// own lengths in paint_handler.cpp / render.cpp) spells its conversion through
// this pair rather than open-coding the multiply; the DOUBLE-domain readers —
// redesign_font_size_px, the corner radii, the ruler's unrounded pitch
// compare — are a different concept (they never round to int, or round to a
// double on purpose) and deliberately do not come through here.
inline int scaled_px(double authored) {
    return static_cast<int>(std::nearbyint(authored * gui_scale_factor()));
}
// The floored form: `floor_px` is the PER-METRIC minimum the accessor states
// (defensive at schema-legal scales, which never go below 100% — the floors
// exist so a degenerate factor cannot zero a structural dimension).
inline int scaled_px(double authored, int floor_px) {
    const int v = scaled_px(authored);
    return v < floor_px ? floor_px : v;
}

// (The former flag_font_size_px() — font_size * 96/72 — is gone with row 7. The
// one text size in the tree is redesign_font_size_px(), below.)

// (THE MONOSPACE TEXT-BOX PADDING FAMILY IS GONE — row 7, 2026-08-01. It was
// flag_pad_x_px / flag_pad_y_px, kChipOutlinePx, kTextBoxPadPx /
// text_box_pad_px, kTextBoxMarginPx / text_box_margin_px and
// flag_glyph_inset_px: the chip anatomy every monospace text box was built
// from. Its last audience was the three bottom-strip editors, which now paint
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
constexpr double kFlagBottomLiftPx = 0.0;

// Fixed-pixel mirrored strip lane grid. G is the single tunable inter-lane gap
// between each adjacent lane pair within a strip. One named constant, one
// place to change it. Now 0 — the lanes of each strip touch, and the
// waveform-side and outer (window-edge) gaps (both kFlagBottomLiftPx, also 0)
// vanish, so lanes and strips pack tight against each other and the window
// edges. Stays a compile-time zero under scaling — zero is
// scale-invariant.
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
// window edge (the kdenlive menu bar, row 1 of the redesign). 34 CONTENT at
// 100% gui_scale plus a 1px MARGIN-BOTTOM, so the LANE is 35.
//
// THE SAME CSS BOX MODEL rows 2, 3 and 4 take (a stated dimension is CONTENT
// and what sits outside it is its own term, with the LANE the sum — the lane
// must physically own every pixel it paints), with ONE TERM DIFFERENT IN KIND:
// what sits outside row 1's content is a MARGIN, not a border. It paints the
// ROW GROUND rather than a line, and it exists to hold the RIGHT-FLOATING VIEW
// BAR's own blue background off row 2's ground. Under the left-floating
// buttons it is indistinguishable from the ground above it, which is correct —
// a margin shows what is behind the box, and behind row 1 is row 1's ground.
//
// 30 -> 34 (architect 2026-08-02, with the view bar): 34 = 1 + 32 + 1 exactly,
// the bar's 32px buttons inside their own 1px margins. Everything below moves
// down 5px, automatically, through main.cpp's lane table.
//
// All three size on gui_scale_factor() like every other lane in the tree (the
// font axis the pre-redesign lanes used to ride is deleted — see the gui_scale
// block above). Rounded with std::nearbyint and floored like every other lane
// metric; the floors are defensive only, since gui_scale never goes below 100.
inline constexpr int kMenuRowHeightPx = 34;
inline constexpr int kMenuRowMarginPx = 1;
inline int menu_row_margin_h_px() {
    return scaled_px(kMenuRowMarginPx, 1);
}
inline int menu_row_content_h_px() {
    return scaled_px(kMenuRowHeightPx, 5);
}
inline int menu_row_h_px() {
    return menu_row_content_h_px() + menu_row_margin_h_px();
}

// Authored pixel geometry of the TOOLBAR ROW — the top strip's lane 1, under
// the menu row (row 2 of the redesign: Save / Undo / Redo / Render). Measured
// at 100% gui_scale off row_2_button_{rest,hover}.png (81x32),
// row_2_separator.png (1x34) and row_2_border_bottom.png.
//
// THE CSS BOX MODEL IS THE RULED VOCABULARY (architect 2026-07-31): the
// architect's stated dimensions are CONTENT, and a border sits OUTSIDE them. So
// the authored height is 44 and the 1px border-bottom is a separate term — the
// LANE the strip stack allocates is their sum (45 at 100%), because the lane
// must physically own every pixel it paints. toolbar_row_content_h_px() is the
// ground/button band; toolbar_row_h_px() is the lane. The border scales too, so
// at 200% it is 2 px of line under 88 px of content.
inline constexpr int kToolbarRowHeightPx = 44;
inline constexpr int kToolbarBorderPx    = 1;
inline int toolbar_border_h_px() {
    return scaled_px(kToolbarBorderPx, 1);
}
inline int toolbar_row_content_h_px() {
    return scaled_px(kToolbarRowHeightPx, 5);
}
inline int toolbar_row_h_px() {
    return toolbar_row_content_h_px() + toolbar_border_h_px();
}

// Authored pixel geometry of the TAB ROW — the top strip's lane 2, under the
// toolbar (row 3 of the redesign: the "Tab A" / "Tab B" Breeze tabs). Measured
// at 100% gui_scale off row_3_tab_{rest,hover,selected}.png (30 tall) and
// row_3_bottom_border.png.
//
// THE SAME CSS BOX MODEL AS THE TOOLBAR: the architect's stated 30 is CONTENT
// and the 1px bottom border sits OUTSIDE it, so the LANE the strip stack
// allocates is their sum (31 at 100%). tab_row_content_h_px() is the ground and
// tab band — the height every tab box fills, flush, top to bottom;
// tab_row_h_px() is the lane. Rides gui_scale_factor() like rows 1 and 2, not
// the monospace font's axis.
inline constexpr int kTabRowHeightPx = 30;
inline constexpr int kTabRowBorderPx = 1;
inline int tab_row_border_h_px() {
    return scaled_px(kTabRowBorderPx, 1);
}
inline int tab_row_content_h_px() {
    return scaled_px(kTabRowHeightPx, 5);
}
inline int tab_row_h_px() {
    return tab_row_content_h_px() + tab_row_border_h_px();
}

// Authored pixel geometry of the ICON ROW — the top strip's lane 3, under the
// tabs (row 4 of the redesign: the thirteen view/mode/action buttons). Measured
// at 100% gui_scale off row_4_button_{rest,hover,click,selected,selectedhover}
// .png (32x32), row_4_separator.png (1x34) and row_4_bottom_border.png.
//
// Same CSS box model as rows 2 and 3: 48 is CONTENT, the 1px border-bottom sits
// OUTSIDE it, and the LANE is their sum (49 at 100%).
//
// 46, AND THE ARITHMETIC CLOSES EXACTLY (architect 2026-07-31, settling the
// discrepancy this constant first recorded): the row was briefed as 48 tall
// with 6px separator margins while the supplied separator crop is 34 tall, and
// 6 + 34 + 6 = 46. He confirmed 46 was meant, so the stated margins are now
// EXACT rather than absorbed — the 34px separator sits at +6 and the 32px
// buttons at +7, both still placed by the standing vertical-centering rule, and
// the centering now REPRODUCES the stated margins instead of papering over a
// two-pixel gap.
inline constexpr int kIconRowHeightPx = 46;
inline constexpr int kIconRowBorderPx = 1;
inline int icon_row_border_h_px() {
    return scaled_px(kIconRowBorderPx, 1);
}
inline int icon_row_content_h_px() {
    return scaled_px(kIconRowHeightPx, 5);
}
inline int icon_row_h_px() {
    return icon_row_content_h_px() + icon_row_border_h_px();
}

// ROW 5's THREE LANES, measured off row_5_full.png (the composite is the
// authority): trim y0..8, ruler y9..36, marker y37..56, and the waveform starts
// at 57 — so the marker lane's bottom edge IS the waveform top, with no gap.
// These replace the four legacy lanes (trim chip / marker text / flag /
// triangle) and, like every redesigned row, ride gui_scale_factor() rather than
// the monospace font's axis.
inline constexpr int kTrimLaneHeightPx   = 9;
inline constexpr int kRulerLaneHeightPx  = 28;
inline constexpr int kMarkerLaneHeightPx = 20;
inline int trim_lane_h_px() {
    return scaled_px(kTrimLaneHeightPx, 3);
}
inline int ruler_lane_h_px() {
    return scaled_px(kRulerLaneHeightPx, 5);
}
inline int marker_lane_h_px() {
    return scaled_px(kMarkerLaneHeightPx, 5);
}

// Authored pixel geometry of THE BOTTOM ROW — the bottom strip's ONE lane
// (row 7 of the redesign: the status line). Measured off row_7_text.png
// (407x33). The bottom strip COLLAPSED FROM TWO LANES TO ONE here (architect
// 2026-08-01): the status row and the modal/editor row became a single line, so
// this accessor is the whole bottom strip's height.
//
// THE CSS BOX MODEL, WITH A BORDER ON BOTH EDGES: 31 is CONTENT, and the two 1px
// borders sit OUTSIDE it — the TOP one under the waveform area, the BOTTOM one
// on the window's last row — so the LANE is their sum (33 at 100%). Every other
// redesigned row has a border on one edge only; this one is the window's foot
// and closes on both. bottom_row_content_h_px() is the ground the text sits on;
// bottom_row_h_px() is the lane the strip stack allocates.
//
// IT RIDES gui_scale_factor() LIKE EVERY REDESIGNED ROW, and that is the change
// this row makes to the strip stack: the bottom lanes were the LAST font-scaled
// lanes in the tree (they sized from the measured monospace band), and with them
// gone the font axis had no surface left — which is why it, and the font_size
// setting behind it, are deleted (architect approval 2026-08-01).
inline constexpr int kBottomRowHeightPx = 31;
inline constexpr int kBottomRowBorderPx = 1;     // top AND bottom, equal
inline int bottom_row_border_h_px() {
    return scaled_px(kBottomRowBorderPx, 1);
}
inline int bottom_row_content_h_px() {
    return scaled_px(kBottomRowHeightPx, 5);
}
inline int bottom_row_h_px() {
    return bottom_row_content_h_px() + 2 * bottom_row_border_h_px();
}

// THE REDESIGN'S SHARED TEXT SIZE, in device pixels — and since row 7 the ONLY
// text size in the product. Every row's text is 12pt through the existing
// points*4/3 convention = 16px at 100%, scaled on gui_scale_factor(). It lives
// here rather than in a painter's anonymous namespace because row 5's marker
// flags shape their labels inside render.cpp while rows 1-4 and row 7 shape
// theirs in paint_handler.cpp, and one design size cannot have two definitions.
//
// ROW 7 CONFIRMED IT INDEPENDENTLY, off row_7_text.png, which is worth recording
// because that row was the last one still carrying a differently-sized face:
//   * the crop's capital band is rows 10..21 and its baseline row 22, i.e. CAP
//     HEIGHT 12 and X-HEIGHT 9 (rows 13..21), with no partial rows on either
//     edge (the source is hinted, so the measurement is exact);
//   * our sans face (fontconfig "sans" -> Liberation Sans) at 16px reports
//     cap 12 / x-height 9 — the crop's numbers, not near them;
//   * a full offscreen re-render of the crop's own string at 16px, pen x=13,
//     baseline 22 fits the crop better than every neighbouring size, baseline
//     and pen tried (15 / 15.5 / 16 / 16.5 / 17 x 21/22/23 x 12..14).
inline constexpr double kRedesignFontSizePt = 12.0;   // -> 16.0 px at 100%
inline double redesign_font_size_px() {
    return kRedesignFontSizePt * 96.0 / 72.0 * gui_scale_factor();
}

// THE MARKER FLAG's anatomy, measured off row_5_lane_3_marker_unselected.png
// (56x20 = a 1px left border plus a 55x20 fill box; the border's own record is
// at kMarkerFlagBorder) and confirmed against row_5_full.png, where the same
// box occupies rows 37..56 with the border at column 22 and the FILL — and the
// stem running on below it — at column 23.
//
// LEFT-ANCHORED, NOT CENTERED. The composite settles it: the 1px stem stands on
// the box's leftmost column, so a marker's box opens AT its frame and runs
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
// Sans 16px) gives an advance of 49.797px with the first glyph's left side
// bearing at exactly 1.00; against the 55px box that pins the left pad at 2
// (2 + 1.00 = column 3, where the crop's ink core starts) and leaves 3 on the
// right. Reproduced faithfully, that extra right pixel READS as slack rather
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
// bottom edge, which is why this is a band and not a ring; the LEFT side is the
// separate border below, in a colour of its own.
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
// decoration. The right side needs no such rule — the box has no right border.
inline constexpr int kMarkerFlagBorderPx = 1;
inline int marker_flag_border_px() {
    return scaled_px(kMarkerFlagBorderPx, 1);
}
// The label BASELINE, measured from the box's top edge. The crop's cap ink runs
// rows 4..15 of the 20 — a 12-row cap height, which is what 16px Liberation
// Sans produces — so the baseline is row 16 and the remaining 4 rows are the
// descender band. Authored as a length rather than solved from font extents
// because the box height (kMarkerLaneHeightPx) is authored too: both come off
// the same crop and must agree with it, not with a font's internal leading.
inline constexpr int kMarkerFlagBaselinePx = 16;
inline int marker_flag_baseline_px() {
    return scaled_px(kMarkerFlagBaselinePx, 1);
}
// ROW 6's ONE LENGTH on the same axis: the area's border, 2px taken FROM the
// area (the measurement and the reasoning are at the row-6 palette block).
inline int waveform_border_px() {
    return scaled_px(kWaveformBorderPx, 1);
}
// THE NINE-GLYPH BUDGET, kept from the retired marker-text lane: a label longer
// than nine glyphs displays as its first EIGHT bytes plus the UTF-8 ellipsis
// (U+2026, 3 bytes) — 11 bytes, 9 glyphs. Composed marker text is ASCII by
// construction (printable-ASCII inserts, lowercase-ASCII label grammar), so a
// byte is a glyph and the ellipsis is a truncation marker no clipboard route
// can author. DISPLAY ONLY: the store, the sidecars, the editor seed and the
// copy payload never see it.
inline constexpr size_t kMarkerLabelGlyphBudget = 9;

// An UPPER BOUND on a flag box's painted width, used only to decide how far
// LEFT of the viewport a marker may sit and still reach into it (flags run
// rightward, so the left cull needs a width and the right cull does not). No
// ASCII glyph in a sans face advances more than one em, so budget * em + the
// two pads bounds every box the truncation can produce. A bound, not a size:
// nothing is laid out against it.
//
// THE LEFT BORDER IS DELIBERATELY NOT IN IT. This bound answers "how far RIGHT
// of its frame column can a box reach", and the border grows the box the other
// way — leftward, away from the viewport — so adding it would only over-admit
// culled markers by one column and never save a visible one.
inline double marker_flag_max_width_px() {
    return static_cast<double>(kMarkerLabelGlyphBudget) *
               redesign_font_size_px() +
           static_cast<double>(marker_flag_pad_left_px() +
                               marker_flag_pad_right_px());
}

// THE TRIM LANE's bevel band: the bottom TWO rows, a lighter then a darker
// shade of whatever surface owns the column. Scales with the lane.
inline int trim_bevel_h_px() {
    return scaled_px(2.0, 2);
}
// The endcap's own width, 2px at 100% (row_5_lane_1_trim_endcap.png is 2x9).
inline constexpr int kTrimEndcapWidthPx = 2;
inline int trim_endcap_w_px() {
    return scaled_px(kTrimEndcapWidthPx, 1);
}
// THE MIDPOINT MARK IS THE 9x9 CROP, so its lengths are the crop's own: a TILE
// 9 columns wide at 100% (its height is the lane's, which is what 9 rows means
// here), an INNER square 5x5, and the crop's 2px INSET placing that square at
// cols 2..6 / rows 2..6. Plus the CLEARANCE the visibility rule demands on each
// side of the whole tile. All of them scale with the lane like every other
// length in this row, and the three tile lengths stay consistent under the
// rounding by construction (inset + inner + inset == tile at 100% and 200%, and
// the painter derives the right margin as what is left rather than re-rounding).
inline constexpr int kTrimMiddleSizePx  = 9;   // the tile's width
inline constexpr int kTrimMiddleInnerPx = 5;   // the dark square inside it
inline constexpr int kTrimMiddleInsetPx = 2;   // the crop's offset to that square
inline constexpr int kTrimMiddleClearPx = 2;
inline int trim_middle_size_px() {
    return scaled_px(kTrimMiddleSizePx, 1);
}
inline int trim_middle_inner_px() {
    return scaled_px(kTrimMiddleInnerPx, 1);
}
inline int trim_middle_inset_px() {
    return scaled_px(kTrimMiddleInsetPx, 0);
}
inline int trim_middle_clear_px() {
    return scaled_px(kTrimMiddleClearPx, 0);
}

// THE PLAYHEAD HEAD, redrawn rather than imported: 19x12 at 100%, ALIASED, from
// row_5_lane_3_playhead.png. Its silhouette is a per-row HALF-WIDTH table, not a
// formula — the shape has doubled rows (y3/y4, y6/y7, y10/y11) that no linear
// ramp produces, so the pixels are transcribed and the table IS the drawing.
// Painting it as integer rectangles keeps it hard-edged at every scale, which a
// path fill would not.
inline constexpr int kPlayheadHeadWidthPx  = 19;
inline constexpr int kPlayheadHeadHeightPx = 12;
inline constexpr int kPlayheadHeadHalf[kPlayheadHeadHeightPx] = {
    9, 8, 7, 6, 6, 5, 4, 4, 3, 2, 1, 1
};

// THE HOVER TOOLTIP'S two SHARED numbers — a DAMAGE BOUND on its box height and
// its dwell. They live out here, rather than with the rest of the tooltip's
// anatomy in paint_handler.cpp, because the RUN LOOP reads both: the tick
// compares the dwell to decide when to show, and damages a band below the top
// strip to cover whatever the box overhangs.
//
// THE HEIGHT HERE IS A BOUND, NOT THE HEIGHT. The painter derives the real box
// from the FACE'S OWN EXTENTS at both type sizes (one line, or 12pt over 10pt),
// so the box follows the font instead of a literal that could drift from it;
// the run loop only needs to know it can never exceed this. 60 clears the
// two-line form (51 at 100%) with room for a font whose metrics run larger.
inline constexpr int     kTooltipDamageHeightPx = 60;
inline constexpr int64_t kTooltipDelayMs        = 700;  // kdenlive-ish; architect-tunable
inline int tooltip_damage_h_px() {
    return scaled_px(kTooltipDamageHeightPx, 5);
}
// THE SETTINGS DROPDOWN'S VERTICAL metrics, out here for the same reason the
// tooltip's height is: the popup's OPEN EDGE must damage the box before the box
// has ever been painted, and its HEIGHT is fully derivable without shaping a
// single label (item count x item height, plus the separator blocks and the two
// borders). Its WIDTH is not — that needs the widest shaped label — so the
// horizontal terms stay with the painter and the open edge damages full-width
// instead. settings_popup_h_px (app_state.h) does the sum, where the item table
// is visible.
inline constexpr int kPopupItemHeightPx = 29;  // measured off dropdown_full
inline constexpr int kPopupSepMarginYPx = 2;   // above and below the separator
inline constexpr int kPopupBorderPx     = 1;
// The item block's own margin inside the border, top AND bottom. The full crop
// puts the first item 3px below the container top; the bottom mirrors it, which
// the crop's own trailing space agrees with.
inline constexpr int kPopupItemMarginYPx = 3;
inline int popup_border_px() {
    return scaled_px(kPopupBorderPx, 1);
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
// the marker/trim stems have a clean stem-only band at the top before the
// samples begin. The symmetric margin is the whole of the purpose.
//
// PROVENANCE (2026-08-02): this used to BE the tip-down triangle's mask height,
// returned through playhead_triangle_h_px(), which is deleted with the
// silhouette — so the inset owns its derivation outright now, and THE VALUE IS
// KEPT EXACT: the same authored unit, the same std::nearbyint, the same floor,
// so every pixel is identical at every gui_scale. 8 at 100%. The floor of 2 was
// the triangle's own ("always a tip row below a top row") and survives only to
// hold the value byte-for-byte; it cannot fire while gui_scale rests in
// [100, 200].
inline int waveform_inset_px() {
    return scaled_px(kPlayheadUnitPx, 2);
}

// THE CHANNEL SPLIT ROW — the ONE owner of where the two channel bands meet,
// area-local (add the area's y for a window row). Shared by the plate renderer,
// which lays the bands against it (render_waveform_to_cache_surface), and by the
// split LINE's paint pass, which draws on it: the two cannot drift, and a line
// drawn on a row the renderer did not split at is exactly the defect this
// removes.
//
// The band is the area minus the symmetric inset at each end; the channels take
// EQUAL halves of it and an odd row over falls HERE, on the split, so the pair
// is symmetric about this row (the reasoning is at the renderer). Returns -1
// when the inset leaves no band at all — the callers' own refusal case.
inline int waveform_channel_split_row(int area_h, int inset_px) {
    const int inset_h = area_h - 2 * inset_px;
    if (inset_h <= 0) return -1;
    return inset_px + inset_h / 2;
}

// Half-width (px) of the playhead COLUMN's reach: a playhead at column c owns
// [c - playhead_half_px(), c + playhead_half_px()]. Bounds the playhead's
// off-screen cull and its narrow invalidation strip — the single definition
// shared by render.cpp (cull) and main.cpp (invalidation). 7 at 100%.
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
// ruler is 19px wide (kPlayheadHeadHalf[0] = 9 either side), so this +/-7 reach
// is NARROWER than the head that stands on the same column. It is harmless as
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
// paint in paint_handler.cpp beside the rest of the bottom row, publishing their
// caret geometry the way the flag editor does — measurement, paint and hit all
// off the same ShapedRun. Nothing in the tree measures text by counting
// characters any more.)


// Screen-coord rect of one rendered flag, keyed back to its marker index.
// Emitted in the same order flags appear left-to-right. It is the WHOLE PAINTED
// BOX — the 1px left border included, so its x sits one column left of the
// marker's frame column (marker_flag_border_px) — because this stash has always
// been the painted extent and a click on the border is a click on the flag.
struct FlagHitRect {
    int    marker_index;
    double x;
    double y;
    double w;
    double h;
};

// All rendering helpers take a Cairo context and pixel-space rectangles; they
// have no X11 or event-loop dependencies.

// The two ground fills, one per surface class (see the palette's ground split):
// render_background erases CHROME in kBackground, render_canvas erases the
// WAVEFORM AREA in kWaveformCanvas — the row-6 crop's own #12312b, which since
// row 6 replaces the grey ground the area used to take. on_redraw calls the
// first over the whole exposed rect, then
// the second over the exposed part of the waveform area, so the canvas wins
// exactly the pixels the plate, playheads, ground recolors, and trim stems paint
// on — cold frames (no plate yet) included.
//
// render_canvas ALSO owns the waveform area's BORDER: after the ground fill it
// paints the area's topmost and bottommost rows in kWaveformBorder, 2px each
// since row 6 (it was a 1px grey rule). The border is taken FROM the area, not
// added to it — the waveform area rect is unchanged, so no lane or column
// arithmetic moves, and the CONTENT band shrinks by those rows at each end
// (waveform_content_rect below). Top and bottom only; the area's sides are the
// window edges (and the inert right gutter), which need no rule.
void render_background(cairo_t* cr, int x, int y, int w, int h);
void render_canvas(cairo_t* cr, int x, int y, int w, int h);


// The waveform area's CONTENT band: the area minus the border rows
// render_canvas paints at its top and bottom — 2px each since row 6
// (waveform_border_px, the black border that replaced the 1px grey one). Every
// pass that fills a BAND inside the area clips to this — the plate blit and the
// region ground recolor — so the border rows survive the frame no matter what
// covers the area. THE PHASE-RESET OVERLAY RING LEFT THIS LIST 2026-08-01: its
// horizontals now ride the borders' OUTERMOST rows deliberately (the ruling is
// at paint_phase_reset_overlay_ring), so it reads the full area. 1px VERTICALS
// deliberately
// do not: the playheads, the marker/trim stems, and the strip-drag anchor stem
// run the full area height and cross the border, which is correct for a position
// line and is not special-cased anywhere — row 6 KEEPS that (the stems' recorded
// z-intent is to run over the borders, and both borders are painted by
// render_canvas at the very bottom of the pass order, so every stem still
// crosses them). Degenerate areas (too short to carry both borders) pass through
// unshrunk rather than inverting.
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
// alt+drag pixel apart differed in most of their columns. On the lattice a pan
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
// There is no interior/edge split, no fractional coverage, no regime threshold,
// and NO INTER-COLUMN CONNECTIVITY AT ALL: a spike stands alone beside a short
// neighbour, which is the classic min/max look the architect chose.
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
// geometry — floor(top) == floor(bot) for any sub-pixel interval, so the
// inclusive fill always writes at least one row. Flat or silent material draws
// a hairline; nothing can fade out or vanish.
//
// PAN INVARIANCE IS STRENGTHENED, NOT WEAKENED, BY THE HALOS' REMOVAL. They
// existed because a column's ink came from the segments on BOTH its sides, so an
// edge column missing an undrawn neighbour was under-covered against the same
// audio rendered interior and shifted under a pan. A bar depends on nothing but
// its own interval, so a column's pixels are now a pure function of its own
// (k0+c) span — two renders of the same columns at the same basis agree
// exactly, and an edge column matches the same audio rendered interior, with no
// correction needed. The AUTHORING LATTICE below is untouched and is still what
// makes that span depend on the global index alone.
//
// THE WRITER: this function does NOT draw through cairo. It writes `dest`'s
// ARGB32 pixel words directly, which is why it takes the surface rather than a
// context. ONE COMPOSITING RULE, where there were two: every write REPLACES.
// The caller cleared every column this call regenerates and each column is
// written exactly once by exactly one bar, so replacing is correct and
// idempotent — the max-compositing that the segments needed (they wrote into an
// already-rendered neighbour) went with them.
//
// The word is PREMULTIPLIED ARGB32, built once per call for the one ink colour;
// at full coverage that is the ink itself, so there is a single word rather than
// a table. The surface is flushed before the first CPU write and marked dirty
// after the last, so later cairo use sees the pixels.
//
// The plate paints uniformly in `color` — it is trim-agnostic, and nothing
// recolors it after the fact: the out-of-trim dim that once masked a second
// color through this alpha is retired, the trim bar spanning the window being
// the whole inside-the-window signal now. Its alpha is BINARY now: opaque bars and
// transparent gaps, with no fractional edges left. The gaps are what let a
// recolored GROUND (kWaveformRegionCanvas, painted before the blit) show
// through.
void render_waveform(cairo_surface_t* dest,
                     GuiRect area,
                     int col0,
                     const GuiAudio& audio,
                     int channel,
                     const WaveformBasis& basis,
                     GuiColor color,
                     const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr);

// Draws a thin 1px vertical LINE across `area` at column `playhead_pixel_x`
// (offset from area.x, float for subpixel centering), in one solid `color` end
// to end, painted straight over whatever it crosses — waveform ink included.
// No-op if outside; the line is column-gated only, so it never leaks into an
// adjacent region.
//
// THE LINE IS THE WHOLE FUNCTION (2026-08-02). It used to carry a
// `draw_triangle` flag and a `triangle_lane` rect for an inverted-triangle
// indicator stamped from a cached mask above the stem: row 5 replaced the
// cursor's tip-down triangle with the RULER lane's aliased head — which
// paint_ruler_row draws, because the head's pre-blended tick crossing needs the
// tick columns — and every caller had passed `false` ever since. The branch,
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

// Draws the strip-drag ANCHOR STEM: a 1-pixel vertical line at the drag's pivot
// column `col` (window pixels within `area`, clamped here to [0, area.w-1]),
// spanning the full waveform height like a marker stem, in kPlayheadStem
// #fcfcfc since 2026-08-01 — the product's one position-line white, replacing
// the dimmer grey #686a6c this drew in (the ruling is at the paint site).
// The anchor is
// the clamped column the strip-drag math pins each event — edge-included, so an
// edge-pinned anchor draws the stem exactly at the edge and the clamp becomes
// visible (the Ableton affordance). Like every other stem it paints ONE solid
// color straight over the waveform ink it crosses — the ink-notch overdraw and
// its plate parameter are retired (architect 2026-07-26, with the polarity
// inversion). The vertical line is hard-aliased at the +0.5 half-pixel column.
void render_strip_anchor_stem(cairo_t* cr,
                              GuiRect area,
                              int col);

// (The cached marker-stem renderers render_markers / render_phaseresetmarkers
// are retired: marker stems are a live overlay,
// GuiPaintHandler::paint_marker_stems — EVERY enabled marker's column, painted
// from the flag painter's stash. The trim stems below are live too —
// GuiPaintHandler::paint_trim, below the playheads; no stem is cached anywhere.)

// The ONE trim bound-to-column geometry owner. Every consumer of a
// trim bound's pixel column funnels here: the ONE paint site (render_trim_flags'
// endcaps and bar gap — the waveform stem site left with render_trim_stems) and the two
// hit sites (hit_test_trim_chip's chip rects, route_trim_chip_press' bridge
// test). It replaced five hand-copied `nearbyint` + `clamp(0, W-1)` formulas
// maintained "byte-identical" by comment discipline.
//
// PURE: all basis inputs are parameters — the collapse unifies the FORMULA. Both
// the painter AND the hit sites decide against the SAME DISPLAYED basis (the
// event-synchronized hit-geometry doctrine): the live trim pass
// (GuiPaintHandler::paint_trim) and the hit sites all call with the DISPLAYED
// basis from
// item_viewport_basis (vp_start_frame/vp_end_frame/area_w — the promoted
// mirror of the committed fp_vp span + effective width) and `displayed_ms`
// mapped through displayed_or_live_target_map by displayed_trim_ms — the
// identical owner chain, so paint and hit are one geometry by construction.
// (Earlier the
// hit sites used the LIVE viewport, which split a hit from its painted pixels
// during an async plate-publish window; the promoted mirror closed that window,
// and the trim painter later joined the same basis when it went live.)
//
// The x_raw denominator is the PAINTERS' quantized-span form
// (vp_end - vp_start)/wave_w, NOT current_samples_per_pixel. The two are
// identical at integer zoom rungs on multiple-of-16 widths and differ by
// <~0.02 px at a fractional zoom rest; adopting it at the hit sites too (they
// formerly divided by spp) is the one deliberate byte change of the collapse and
// ALIGNS paint and hit exactly — the point of unifying them.
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
// half-open, EMPTY when hi <= lo), the ONE owner shared by the router
// (route_trim_chip_press' pair-drag between test) and the painter
// (render_trim_flags' midpoint-mark fit test), so the bridge's clickable band
// and the mark's clearance read the same interval. The bar itself no longer
// comes from here — it spans the WINDOW, bound column to bound column, and the
// endcaps paint over its ends. Both bounds must be set (callers gate). The
// offscreen arms key on the bound's SIDE (TrimBoundColumn::side, the unrounded
// verdict) — NOT col_raw, which cannot tell the side across the rounding seam
// (a barely-off-left bound rounds to col_raw == 0). The 4x2 semantics:
//   BEGIN — the gap's LEFT edge, a left-edge-anchored chip:
//     InView (chip painted) -> lo = col + chip_w           (the drawn chip's
//        inner RIGHT edge; the gap starts just past the chip).
//     OffLeft (no chip)     -> lo = min(col_raw, -1)        (a STRICTLY NEGATIVE
//        flush sentinel: the fill clips flush to column 0 AND the left ring border
//        lands offscreen — true only via the sentinel; raw col_raw == 0 would
//        float the border at the edge).
//     OffRight (no chip)    -> lo = max(col_raw, wave_w)     (>= wave_w: nothing
//        paints in the visible [0, wave_w) and the router's [0, wave_w) gate can
//        never arm — an empty gap in the visible area).
//   END — the gap's RIGHT edge, a right-edge-anchored chip:
//     InView (chip painted) -> hi = col - chip_w + 1        (the drawn chip's
//        inner LEFT edge, exclusive).
//     OffRight (no chip)    -> hi = max(col_raw + 1, wave_w + 1)  (a PAST-THE-EDGE
//        flush sentinel: the fill clips flush to the right edge AND the right ring
//        border lands offscreen).
//     OffLeft (no chip)     -> hi = min(col_raw + 1, 0)      (<= 0: empty against
//        any lo >= 0 — closes the one-pixel bridge a raw col_raw == 0 left, which
//        gave hi = 1 and painted/accepted a column-0 sliver for a window wholly
//        left of the viewport).
// The +chip_w inset is the ROOM a PAINTED chip occupies; an offscreen bound
// paints no chip, so the inset is dropped and the bar fills FLUSH. This interval
// is returned UNCLAMPED (raw sentinels included) — its role is to carry the
// offscreen-flush and empty semantics past the visible edge; it is NOT a drawn
// interval. The two consumers clamp it to the visible range identically: the
// PAINTER intersects it with the effective width [0, wave_w) before asking
// whether the midpoint tile fits, and the ROUTER applies the same [0, wave_w)
// click gate. So the inert non-multiple-of-16 gutter [wave_w, strip_w) neither
// paints nor hits. The sentinels earn their strictness here: an offscreen edge
// lands STRICTLY past the visible range (never at col 0 or col wave_w-1), so a
// window running off the view yields a flush interior rather than a spurious
// one-column one.
struct TrimBridgeGap {
    int lo;  // inclusive left column
    int hi;  // exclusive right column (empty gap when hi <= lo)
};
TrimBridgeGap trim_bridge_gap(const TrimBoundColumn& begin,
                              const TrimBoundColumn& end, int chip_w, int wave_w);

// The source-frame -> displayed-domain mapping the two HIT sites (add_chip,
// bound_col) AND the live trim paint pass (GuiPaintHandler::paint_trim)
// share. Byte-identical to render.cpp's file-local
// frame_to_paint_sample for every reachable (non-negative) trim bound: in a
// mapped view the source frame is rounded once through map_source_to_target,
// then rounded again; the identity (null/empty map) path returns the frame
// as-is. A negative frame is guarded to 0 (unreachable — past-EOF is load-fatal
// and bounds are never negative — kept for exactness vs the prior hit code).
// One mapping owner for paint and hit, so a chip is grabbed exactly where it
// is drawn. `map` is null in source view (identity) and the item pixels' own map
// (displayed_or_live_target_map) in target view.
double displayed_trim_ms(int64_t frame,
                         const std::vector<WarpFrameMapSegment>* map);

// The ONE trim ENDCAP screen-rect owner: the begin/end edge-anchoring rule
// lives here, consumed by both the painter (render_trim_flags) and the hit test
// (hit_test_trim_chip), so paint and hit are one owner again — row 5's endcaps
// replaced the square chips in BOTH at once.
//
// A trim bound is an EDGE, not a point: the begin cap's LEFT edge sits ON the
// bound column (rect left = strip_x+col), the end cap's RIGHT edge sits on it
// (rightmost pixel = strip_x+col). The cap is trim_endcap_w_px() wide — 2px at
// 100%, where the chip was a flag-width square — and its y-band is the trim
// lane `row`. Deliberate asymmetry vs centered marker flags: a bound at frame 0
// / EOF shows its cap fully onscreen.
//
// THE HIT TEST INFLATES THIS by kTrimEndcapGrabPx per side. A 2px target is
// under any reasonable pointing tolerance, so the drawn cap and the grabbable
// cap are deliberately NOT the same rect — the one place in this lane where
// they differ, stated here because everywhere else in the redesign they are
// identical by construction.
GuiRect trim_chip_rect(bool is_begin, int strip_x, int col, GuiRect row);

// Grab tolerance added to EACH SIDE of the drawn endcap for hit-testing. The
// caps are 2px; this makes the target 2 + 2*4 = 10px, close to the square chip's
// old width, so the bound drags feel as they always did.
inline constexpr int kTrimEndcapGrabPx = 4;
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

// Draws the WHOLE TRIM BAR LANE (row 5's endcap bar, which replaced the square
// b/e chips and their strip-crossing stems): the lane ground, the window's bar
// over it, the two endcaps over that, and the midpoint mark last. Every run
// paints through ONE lambda — a face band above a two-row bevel pair, all
// pixel-bound integer fills, no stroke and no antialiasing anywhere in this
// lane — so a surface is named by its four constants and nothing else.
// The lane band is the `chip_row` PARAMETER — the caller passes
// top_trim_row_area(app) (top-strip lane 4), the same accessor
// hit_test_trim_chip's y-gate and route_trim_chip_press' bridge y-gate read, so
// paint and hit take the band from ONE owner and cannot drift; nothing in here
// re-derives the lane's y from the row heights above it. `chip_row` gives the
// lane's x/y/h; `waveform_area` is read for its `.w` ALONE — both the
// column-mapping denominator and the lane's effective width, so the inert
// non-multiple-of-16 gutter is outside the clip and never paints.
// `top_strip_area` is now a validity guard only: nothing in this lane measures
// from the strip's own bottom any more.
//
// PAINT ORDER IS BACK TO FRONT, which is what lets each run ignore its
// neighbours: GROUND across the whole lane, then the BAR spanning the window
// (kTrimLaneBar), then the caps (kTrimLaneEndcap) over the bar's ends. An
// inverted or degenerate window simply leaves the ground showing.
// THE BAR SPANS THE WINDOW ITSELF, bound column to bound column, and FOLLOWS AN
// OFFSCREEN BOUND rather than stopping short — an out-of-view bound means the
// window continues past that edge, so the bar runs flush to it and the lane
// clip trims the overhang. It is the one "this is the trim window" signal and
// the visual affordance of the pair (bridge) drag's grab band.
// BOTH ENDCAPS always paint unless the viewport culls them (the window is
// always set since 2026-07-30), EDGE-ANCHORED on their bound columns with
// their bodies facing inward: the begin cap's LEFT edge on its column, the end
// cap's RIGHT edge on its own. A bound is an EDGE, not a point — the
// deliberate asymmetry vs centered marker flags — so a bound at frame 0 / EOF
// shows its cap fully onscreen. A culled bound paints no cap at all: it has no
// column on screen to stand on, and the bar's flush edge is what says the
// window continues past the view.
// Both caps come from the ONE rect owner the hit test reads
// (trim_chip_rect), so the painted cap and the grabbable cap describe the same
// edge; the hit side adds only its stated grab tolerance. Column placement is
// on the displayed viewport basis — `trim.begin` / `trim.end` are already in
// the displayed domain, so no further translation happens here. A cap has NO
// editable payload; it is a plain-press grab target only (trim is outside the
// selection system).
// THE MIDPOINT MARK (2026-08-01, kdenlive's zone-middle crop blitted verbatim)
// paints last, on the bar's face at the WINDOW's midpoint column — through the
// same trim_bound_column owner the bounds use, so it scrolls off the view with
// the window instead of sliding to the middle of whatever is on screen. Its
// ONLY hide rule is TOO NARROW TO FIT: the whole tile must sit inside the
// visible interior BETWEEN the endcaps (trim_bridge_gap, clamped to the
// effective width) with a clearance each side — a binary verdict on integer
// columns, so it cannot flicker, and below the threshold it simply does not
// paint (no shrink, no clamp). It is otherwise INFORMATIONAL: no hit rect, no
// gesture, no routing change anywhere. Its lengths are trim_middle_size_px /
// _inner_px / _inset_px / _clear_px and its four colours are the lane's own
// endcap + bar surfaces; the pixel-by-pixel derivation from the crop is at the
// paint site (render.cpp).
void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect chip_row,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim);

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
// the stem under it) and the color its class resolved to. The painter is the
// only producer; the per-frame waveform pass (GuiPaintHandler::paint_marker_stems)
// is the only consumer, so a stem and its flag can never disagree about a column.
// The published COLOUR is the marker's resolved CLASS; the consumer applies
// exactly one override over it, the open flag editor's invalid-commit red flash
// (a transient the painter has no business baking into a cache — the contract is
// at GuiPaintHandler::paint_marker_stems).
// A DISABLED marker publishes NO ENTRY AT ALL — disabled markers have no stem
// ever (architect), and expressing that as an absent entry rather than a flag
// on the entry means the consumer has nothing to re-decide. That absence is
// LOAD-BEARING TWICE since 2026-08-01: the stem is a POINTER TARGET now
// (hit_test_marker_stem, app_state.h), so "no stem" and "not grabbable" are the
// same fact rather than two that could drift, and `marker_index` is what lets
// the hit route into the marker-click bodies.
struct MarkerStem {
    int      marker_index;
    double   x;
    GuiColor color;
};

// Draws the marker lane's flags in `top_strip_area` above visible markers, in
// THE KDENLIVE TEXT-ON-FLAG FORM (row 5, 2026-08-01): each flag is a filled box
// whose FILL's LEFT EDGE stands on its marker's pixel column, spanning the whole
// marker lane vertically, carrying a 1px top edge in its class's edge color and
// the marker's own composed label in the redesign's sans face. The width is
// DERIVED from the shaped label (pad + shaped + pad); the anatomy, the pad and
// the nine-glyph truncation live at kMarkerFlagPadXPx above.
//
// PLUS A 1px LEFT BORDER OUTSIDE THAT FILL (architect 2026-08-02),
// kMarkerFlagBorder, full box height, standing one column LEFT of the frame
// column so THE STEM KEEPS THE FILL'S LEFTMOST COLUMN. It is one value across
// every LIVE class and takes the disabled blend with the rest of the face (the
// ladder below). The published hit rect is the whole box, border included; the
// geometry, the left-edge clip and the colour's provenance are at
// marker_flag_border_px and kMarkerFlagBorder.
//
// OVERLAP IS LATER-OVER-EARLIER IN STORE ORDER and there is NO OTHER OCCLUSION
// MANAGEMENT AT ALL — no elision, no z-lift for selection, no run arbitration.
// That is the whole model the marker-text lane's resolver used to stand in for,
// and it is deliberately the simplest thing that can be true: a later marker's
// box covers an earlier one's tail, and the user pans or zooms to read it.
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
//              RED STILL REFUSES THE LIFT, exactly as the live red class does
//              (no selected pair by ruling, the normalization cue unmasked).
//   Red:       kMarkerFlagFillRed / kMarkerFlagEdgeRed; stem kMarkerStemRed;
//              border kMarkerFlagBorder undamped, like every live class.
//   Otherwise: kMarkerFlagFill / kMarkerFlagEdge, swapping to the bright
//              kMarkerFlagFillSel / kMarkerFlagEdgeSel pair when selected —
//              SELECTION IS THAT SWAP AND NOTHING ELSE. The stem stays the
//              CALM kMarkerFlagFill either way (the architect's explicit rule).
//
// `iteration_on` reaches the one composer (flag_text_iter) so the flag shows
// exactly what the editor would seed. `cr`'s scaled font is set by this
// function (the redesign sans face at redesign_font_size_px) and restored.
//
// THE PAINTER PUBLISHES ITS GEOMETRY. `out_hit_rects` receives one rect per
// painted box in PAINT ORDER (so the hit walk reads it backwards to get the
// topmost box) and `out_stems` one entry per ENABLED painted marker. A derived
// width cannot be recomputed without shaping, so the pixels' own pass is the
// single owner of both — the same painter-stash contract the redesigned rows'
// buttons already use. Either pointer may be null.
//
// `editing_marker_index` is the marker whose FLAG EDITOR IS OPEN, or -1. Its
// BOX, LABEL AND HIT RECT ARE ALL SKIPPED — the open editor paints that flag
// itself, unrolled (render_flag_editor_box) — while its STEM still paints and
// still publishes. Without the skip the editor's box is merely drawn OVER this
// one, which hides it only while the edited text is the wider of the two; a
// SHORTENED payload then let the committed label's tail show past the editor's
// right edge (the 2026-08-02 bug). THE HIT RECT GOES WITH THE BOX for the same
// width reason: the published geometry is the PAINTED geometry, so a box that
// is not drawn claims nothing, and the blank tail beside a narrowed editor can
// no longer resolve a marker click. The phase-reset painter takes no such
// parameter and the reason is recorded at its call.
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
                  std::vector<FlagHitRect>* out_hit_rects = nullptr,
                  std::vector<MarkerStem>* out_stems = nullptr,
                  const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                  const DragOverlay* drag_overlay = nullptr,
                  int editing_marker_index = -1);

// THE OPEN FLAG EDITOR'S RESOLVED GEOMETRY, published by render_flag_editor_box
// and consumed by the pointer path. Every field is DERIVED FROM A SHAPED RUN,
// which is exactly why it is published rather than recomputed: a second shaping
// pass in the hit path could disagree with the pixels.
//
//   `box`           the painted box in window coordinates — the marker's flag,
//                   unrolled to hold the FULL untruncated pending plus caret
//                   room, and CLAMPED fully on-window. It spans the 1px LEFT
//                   BORDER too (the flag's, which this box also wears), so its
//                   x is one column left of the fill and its w one wider.
//   `text_origin_x` the window x that pending BYTE 0 paints at. It already
//                   carries the view offset, so it is negative-of-nothing and
//                   directly usable: byte k sits at text_origin_x + byte_x[k].
//   `byte_x`        the shaping chokepoint's per-byte-boundary pen offsets
//                   (text_shape::byte_offsets_px) — pending.size() + 1 entries.
//                   The caret, both selection edges and click-to-byte all index
//                   it, so what is drawn and what is grabbed are one vector.
//
// `valid` is false whenever no FlagPayload editor is open, and the painter
// writes that state on every frame it runs, so a stale box can never outlive
// its session.
//
// THE BOX IS THE CLAIM, pads included: a press anywhere inside it places the
// caret, which is why the text VIEWPORT (the clip band inside the pads) is not
// published — clicking a field's padding should put the caret at the nearest
// end, and the nearest-boundary search gives exactly that with no extra term.
struct FlagEditorBox {
    bool                valid         = false;
    GuiRect             box{0, 0, 0, 0};
    double              text_origin_x = 0.0;
    std::vector<double> byte_x;
};

// THE FLAG EDITOR'S UNROLL (row 5's last piece, 2026-08-01): the marker's flag
// box EXPANDS to hold its full untruncated payload plus room for the caret, and
// the editor's text is drawn inside it — kdenlive's flag-becomes-the-text-box,
// which is also how this product's own editor read before the marker-text lane
// took the payload away.
//
// THE BOX WEARS THE MARKER'S OWN FACE: the class fill, the top edge and the 1px
// left border render_flags would have given it (disabled blend, red, selected
// swap, all through the one ladder; the border class-invariant), so opening an
// editor changes the flag's SIZE and nothing else about how it reads. An
// invalid commit flashes the marker lane's OWN red pair — kMarkerFlagFillRed /
// kMarkerFlagEdgeRed. Since 2026-08-02 the BOTTOM-STRIP editors flash that same
// pair in this same box anatomy (render_bottom_strip_editor), so there is one
// invalid red and one editor box in the product; the pre-redesign dark-red chip
// pair they used to flash was the last tunable colour in the tree and went with
// the whole palette-config system the next day.
//
// THE TEXT IS THE REDESIGN'S SANS, matching the labels it replaces — the
// monospace face dies at this surface with the lane placement owner
// (lane_text_left_x) that used to put it here.
//
// AT A WINDOW EDGE THE BOX CLAMPS AND THE VIEW TRUNCATES. The box slides left
// to stay fully on-window; when the payload is wider than the lane itself the
// box spans the lane and the text scrolls inside it, the caret staying visible
// through State::view_offset_px (the minimal-travel rule lives at that field).
// Left/Right/Home/End then navigate it exactly like any one-line field —
// nothing in the key path knows the box scrolls.
//
// Takes AppState by NON-CONST reference, alone among the renderers, and for two
// honest reasons: it advances the editor's view offset (session state that must
// persist across frames) and it publishes app.flag_editor_box. Both are the
// painter owning what only the painter can compute.
void render_flag_editor_box(cairo_t* cr, AppState& app, const GuiAudio& audio);

// The phase-reset column's flags: the identical box, the identical class ladder
// and the identical publication contract render_flags documents above. Their
// LABEL is the display-only kPhaseResetLaneToken (a phase reset authors no
// payload), so there is no iteration_on parameter — nothing to compose.
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
                            std::vector<FlagHitRect>* out_hit_rects = nullptr,
                            std::vector<MarkerStem>* out_stems = nullptr,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                            const DragOverlay* drag_overlay = nullptr);

// Iteration-aware flag text composer. Returns the
// plain flag text when `iteration_on` is false or the marker is iter-
// ineligible; otherwise splices the inline `+[lo, hi]` bracket after
// the tempo. The single canonical composer for warp flag text: the FLAG ITSELF
// paints it (row 5, truncated at the nine-glyph budget) and the flag editor
// seeds from it, so what a marker shows and what its editor opens with are one
// string by construction.
std::string flag_text_iter(const std::vector<GuiWarpMarker>& markers,
                           int idx, bool iteration_on);

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
// of it: the box is LEFT-anchored on the marker's own column like the flag it
// replaces, sized by shaped text, and clamped by render_flag_editor_box, which
// is now the single owner of that whole question. The column math they wrapped
// — painted_column_of_source_frame_on_basis over the displayed map and the item
// viewport basis — is unchanged and called directly there.)

// THE PHASE-RESET DISPLAY TOKEN, and the one statement of it: what a phase
// reset's FLAG shows, where a warp marker shows its composed line
// (flag_text_iter). DISPLAY ONLY — a phase reset authors no payload and
// serializes as a bare frame, so this string exists nowhere but the flag.
//
// ONE GLYPH (architect 2026-08-01, at the row-6 live look). It had been "p.r."
// since the marker-text lane widened it from the original "p": a dense reset
// cluster sat right on that lane's all-or-nothing fit verdict, and a small zoom
// change flipped the whole lane between modes — the lane blinked. THAT LANE AND
// ITS VERDICT ARE GONE (row 5 — flags simply overlap, later over earlier), so
// nothing depends on the width any more, and the reason to spend four glyphs
// went with the mechanism that needed them. Back to "p", which is what the
// widening had taken away. Nothing else moves: every flag's width is
// pad + shaped(label) + pad, so the boxes re-derive from the shaping pass by
// construction, and the token stays far under the nine-glyph budget so it never
// truncates. Its producers collapsed to ONE with the resolver: the flag painter
// (render.cpp).
inline constexpr char kPhaseResetLaneToken[] = "p";

