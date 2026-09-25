#pragma once
#include "warpmarkers.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment for target-view waveform
#include "gui_input.h"        // kHoldBeatMs for the tooltip dwell
#include "waveform_gain.h"    // WaveformGainCurve, the waveform picture's gain

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
// the redesign kept the doctrine: a highlight REPLACES the colors it lifts
// rather than washing over them. The region highlight is opaque colors in
// two passes — kWaveformRegionCanvas for the ground under the ink, then
// kWaveformRegionInk (and kWaveformRegionGhostInk for the magnification's
// ghost) for the plate's own pixels, keyed by each pixel's word under the
// plate's BINARY alpha so every pixel ends up fully one color or fully another
// and nothing blends; a disabled face resolves to a solid color through
// mix_color before it reaches cairo, never a fade.

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
// own pass since 2026-08-01: it paints OVER the marker stems, and over the
// resting cursor, which since 2026-09-23 paints over the stems as well).
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

// THE ACCENT'S UNFOCUSED FACE — Breeze's INACTIVE selection blue, ARCHITECT-
// NAMED 2026-09-02: "breeze blue should change to #1b4155 when window loses
// focus (inactive color, the scrub already does this — not sure if it's the
// same color but it's at least similar)". It is NOT the scrub's own
// kScrubPlayedInactive #1d3847, which he measured off his 16:46 shot of a
// Breeze slider: two Breeze roles that read alike and are not the same value,
// so they stay two constants under the standing numerically-close-is-not-the-
// same rule (kRedesignRowGroundUnfocused states it). A HARD SWAP, no fade,
// like every other window_activated face.
//
// ITS SCOPE IS THE FOLDER OVERLAY'S PANEL — the highlighted row's band and its
// list-focused outline — AND THE MODAL ROW UNDER EVERY ONE OF ITS FIVE OWNERS,
// where it is the buttons' ACTIVE-FOCUS outline: the panel is what the ruling
// was taken on, but the row is ONE visual role and the ring is a claim about
// the KEYBOARD, which an unfocused window does not have, so a prompt's and a
// dialog editor's rings dim with the player's, the picker's and the AV Sync
// Stats panel's (the fork is on
// the flag alone at accent_for_focus, paint_handler.cpp, and the activation
// hook damages whichever modal row stands). It is the FOURTH reader class of
// AppState::window_activated after rows 1 and 2 (the ground) and the scrub
// (its played groove) — AND THE SELECTED TAB'S CAP IS THE FIFTH since
// 2026-09-09 (architect, from the third PCManFM-Qt/Breeze crop of the top
// strip relayout: the unfocused window's selected tab wears EXACTLY this
// value across its 3px cap, #1b4155, the same Breeze role), read through the
// same accent_for_focus fork by paint_tab_row and damaged by the same
// top-strip invalidation the header's ground swap already spends. WHAT IT
// DOES NOT TOUCH: the hover faces on any surface. A hover is a promise that
// the pointer can act, and a pointer over an unfocused window still can — so
// kFolderRowHover, kFolderRowHoverOutline, kFolderRowHoverSelected and the
// modal row's hovered / armed / pressed accents all keep the live blue, and
// THE UNSELECTED TABS' TWO COLOURS DO NOT SWAP either (the third crop's
// hovered B is the same #263f4d as the focused one's). The icon row's lamps,
// the view bar and the flag editor's selection band are outside the ruling
// entirely.
inline constexpr GuiColor kRedesignAccentInactive = hex(0x1B4155);

// THE UNFOCUSED GROUND for rows 1 and 2 (architect 2026-07-31, from live use).
// The crops' #292c30 is Breeze's FOCUSED header shade; when the WINDOW LOSES
// KEYBOARD FOCUS the header darkens to #202326, tracking the labwc titlebar
// above it, which darkens on the same edge. A HARD SWAP — no transition, no
// fade — and ON THESE TWO ROWS it moves the GROUND ONLY: separators, border
// lines, the accent, labels and icons all keep their colors here.
//
// IT IS DRIVEN BY chrome_focused (app_state.h), WHICH IS
// AppState::window_activated AND NOTHING ELSE: the header does NOT go
// inactive under the render player, the picker or the AV Sync Stats panel
// (architect 2026-09-03 evening, with the File anchor left live on this row —
// "the title bar is still the regular one — the window has focus"; the modal
// term it carried from 2026-09-02 lives at view_bar_focused now, the bar
// being the surface that needed it). (GAP 1's band wore this shade from the
// same painter while it opened above the menu row, 2026-09-03..09; the band
// is the TAB ROW's tall ground since the top strip relayout and takes
// kRedesignContentGround, which does not swap.)
// NO OTHER ROW'S GROUND SWAPS: row 3's ground and every row below it sit on
// kRedesignContentGround, itself the unfocused shade, so the swap has nothing
// to do down there. (Row 3's ground was the resting tab's #1b1d20 for a few
// hours on 2026-08-13, under a ruling the architect withdrew the same day —
// the record is at the row-3 block below.)
//
// WHAT ELSE READS THE FLAG, re-grepped rather than inherited (2026-09-09): the
// PLAY-SCRUB's played groove (2026-08-28, the block at kScrubPlayed below);
// since 2026-09-02 the FOLDER OVERLAY's panel and every modal row's
// active-focus ring, whose accent takes kRedesignAccentInactive above; and
// since 2026-09-09 THE SELECTED TAB'S CAP, the same constant through the same
// fork. So "no row below row 2 swaps",
// true of the GROUND when it was written, is no longer true of the window:
// what row 1 and row 2 own is the unfocused ground, and the accent's own
// unfocused face is the panel's, the ring's and the tab cap's. (Row 2's own LANE died at the 2026-08-12
// relayout — its crop-named faces are the icon row's now, over a ground that
// is already the unfocused shade — so this ground has ONE lane left to paint,
// and the pairing survives as the crops' naming.)
//
// NOTE it coincides with kBackground and with kRedesignContentGround (all
// three sample the same Breeze Window color) and is nonetheless its OWN
// constant by the hard-coded rule — three facts that happen to agree, not one
// fact referenced three times.
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
// (A SECOND READER SHARED THE KNOB from 2026-08-01 to 2026-08-14: the tab
// row's UNLOCKED padlock dimmed by this same factor over the tab's current
// face. The padlock is an icon-row button now and its open state is simply
// UNLIT — in that row a dimmed glyph means disabled — so this factor is the
// disabled face's alone again.)
// THE RENDER PLAYER'S MODAL ROW IS A SECOND READER since 2026-08-30
// (architect: the transport keys are their own class): its disabled rung is
// this same keep factor toward kRedesignContentGround — the bottom row's
// own content ground, the very constant row 2 dims toward — applied to the
// row's two button kinds by DERIVATION, not by a new sample: the borderless
// glyph buttons dim per-path through icons::draw's keep_own exactly as row
// 2's glyphs do, and a word button's outline, label and focus faces mix
// by the same factor (paint_modal_dialog's ladder). Since 2026-09-01 THE
// PLAYER'S ROW IS ALL GLYPHS — Load in place took the checkmark and Close
// Breeze's window-close X — so the word half of that derivation has no
// producer today: it is the ladder's shared answer, kept because the ladder
// is one body over every owner, and the disabled bit is still this row's
// alone. No new constant, so a retune of this knob retunes both rows together.
inline constexpr double kRedesignDisabledMix = 0.322;

// -- Row 1's RIGHT-FLOATING VIEW BAR (HARD-CODED, kdenlive-sampled) ---------
//
// kdenlive's workspace switcher — the blue bar at the far right of its menu row
// ("Logging | Editing | Audio | Effects | Color") — reborn here as the three
// absolute view selectors. Sampled off the nine 82x32 row_right_*.png crops,
// each one whole "Logging" button.
//
// TWO BAR BACKGROUNDS, ONE PER FOCUS STATE: the bar swaps on view_bar_focused
// (app_state.h) — the header's own chrome_focused AND no folder overlay
// standing — much as row 1 swaps its ground, and the swap is the same kind of
// thing, a PAINT-ONLY variant of the whole surface. The MODAL term is this
// bar's alone since 2026-09-03 evening (architect 2026-09-02: under the render
// player or the picker the resting 1/2/3 (a fourth selector since 2026-09-15)
// "should have the disabled bg as that
// is what kdenlive does with a modal"; the header carried the term with it
// from that evening until the File anchor went live under the band, when the
// header went back to the activation flag and this bar kept the modal half).
// The crops named "disabled" are the UNFOCUSED WINDOW, not a disabled button:
// the nine hold no disabled member. Under the FOLDER OVERLAY
// redesign_button_enabled greys them through its first arm, and this ground
// swap was the whole of what showed it until 2026-09-10 — the unfocused bar
// being the focused header's own shade. SINCE THAT DAY THE ROW HAS A DISABLED
// FACE OF ITS OWN (architect 2026-09-10, at his mockup: "accepted"), read off
// the ENABLED bit so the band and the ITERATION LOCK (iteration_lock_greys,
// app_state.h, which greys them too since that morning) produce it alike —
// under the band it is this ground's other half rather than a second cue —
// AND IT IS THE LABEL'S INK ALONE — a DEAD UNSELECTED selector retains
// kRedesignDisabledMix of kRedesignLabel over the bar's current ground, exactly
// row 2's disabled ink through the same mix_color owner, while the SELECTED
// view keeps its full ink (it reports where you stand, which stays true under a
// lit lamp) and this ground, the box faces and every metric stay put. It is
// BUILT from the row's own vocabulary and not sampled: this crop set has no
// disabled member, the nine being the focused/unfocused pairs. THE GROUND SWAP
// WAS TRIED AS THAT FACE THAT MORNING AND HE REFUSED IT — the unfocused bar
// keeps its labels fully legible and what it says is that the window is
// inactive, which is untrue under a lit lamp on a focused window — which is
// why the lock's face is the ink's and view_bar_focused takes no lock term.
// (The clause used to name ROW 4 alongside them for having no disabled face and
// no longer can, in either direction: the icon row greys for two MODES since
// 2026-08-15 — the `h` view and the read-only lock — and this row greys for the
// ITERATION lock since 2026-09-10. The view bar is untouched by the icon row's
// two, its three selectors being navigation.)
//
// kRedesignViewBarBgUnfocused is NUMERICALLY EQUAL to kRedesignRowGround
// #292c30 and is NOT it: that constant is the FOCUSED CHROME ground, this one is
// the UNFOCUSED BAR, sampled from kdenlive's own unfocused crop. Two facts that
// happen to agree, like kRedesignContentGround and kBackground — a retune of
// one must not follow the other.
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
// FRACTION AND BASE ARE BOTH KNOBS: the base is what the bar lifts TOWARD,
// spelled out here rather than borrowed from kRedesignLabel because it is a
// sampled coincidence and not a reference.
inline constexpr GuiColor kRedesignViewBarLiftBase     = hex(0xFCFCFC);
inline constexpr double   kRedesignViewBarSelectedMix  = 0.125;
inline constexpr double   kRedesignViewBarFrameMix     = 0.20;

// -- Row 3, the TAB ROW (HARD-CODED, Breeze-sampled) ------------------------
//
// ROW 3 IS NOT SAMPLED FROM KDENLIVE, and that is worth stating first because
// it explains an anomaly that otherwise reads as drift: kdenlive has NO
// upward-facing tabs anywhere in its window, so this row's crops could not
// come from it. They come from KWAVE and PCMANFM-QT — two other Breeze
// applications on the same desktop — which AGREE WITH EACH OTHER on every
// value here. That is why row 3 carries greys the kdenlive-sampled rows do
// not, and why a value on this row disagreeing with one on rows 1, 2 or 4 is
// evidence of two Breeze roles rather than of a mis-sample.
//
// Sampled off tmp/screenshots/kdenlive/redesign/row_3_tab_{rest,hover,selected}
// .png (30 px tall), row_3_tab_pcmanfmqt.png (the padding/geometry reference),
// row_3_bottom_border.png and — since 2026-08-13 — row_3_tab_example.png
// (281x30) and row_3_tab_trough.png (1x30). Same carve-out as the four above:
// constexpr, not config keys, the crop wins over any Breeze-derived scheme.
//
// THE ROW'S GROUND OUTSIDE THE TABS IS THE CONTENT GROUND #202326 — the
// selected tab's interior and the pane below, Breeze's standard tab bar, where
// the BAR MATCHES THE PANE and an unselected tab is RECESSED darker than both.
// THE TWO NEW CROPS SAY SO EXACTLY: row_3_tab_example is one #535659 row at
// y=0 across the full 281 px, then two adjacent unselected tabs at #1b1d20
// over x 0..178 with #202326 over x 179..280 — the TROUGH, the empty bar right
// of the last tab — and row_3_tab_trough is that trough sampled alone, one
// #535659 row over 29 rows of #202326. (Those crops' y=0 line was the row's
// top border from 2026-08-13 to 2026-09-09; the border paragraph below
// records why it went.)
//
// A RULING PASSED THROUGH THIS BLOCK AND WAS WITHDRAWN, recorded so nobody
// re-proposes the swap from the same misreading. On 2026-08-13 the architect
// ruled "the tab row's background = the unselected tab's own colour", the row
// ground moved to #1b1d20 (an unselected tab receding into the bar, the
// selected one standing proud), and he WITHDREW IT the same day once the two
// crops above were measured: the reading behind it was that the row bled into
// the menu row, and WHAT WAS ACTUALLY MISSING WAS THE ROW'S TOP BORDER, not a
// darker ground. The border landed and the ground came back. Both facts are
// the crops', and the crops were always there to be read.
//
// kRedesignContentGround (RENAMED from kRedesignTabGround at the withdrawn
// ruling and KEPT after it) is THE SURFACE THE SELECTED TAB OPENS INTO, one
// fact seen on the tab row and every lane below it: the TAB ROW'S OWN GROUND
// (the trough is a reader again, and so are GAP 1 above the lane and the
// lane's own 6px MARGIN-BOTTOM below its content — one fill covers all
// three), the SELECTED TAB'S INTERIOR (the tab is a
// mouth into the content — literal again since the base line came inside the
// lane on 2026-09-09, that line BREAKING under the selected tab's box exactly
// as the retired border row did 2026-08-13..2026-09-09; with the ground
// restored the two are the same pixels, and the tab lays that same constant
// inside its own rounded path once more, over the neighbour's spill under its
// corner — paint_tab_row), the ICON
// ROW's ground, row 5's trim/ruler/marker lane grounds, the unified BOTTOM
// ROW's ground, and the ground term every face mix over those rows resolves
// against (the icon row's and the transport row's five faces, the modal dialog
// buttons' pressed interior, the disabled marker blend). One constant, not
// seven copies of the number. THE BROADER NAME STANDS even though the trough
// reads it again: it names the SURFACE rather than a row, which is the
// row-independent naming rule at the top of this file, and "TabGround" would
// again name one reader out of seven. NOTE that it COINCIDES with kBackground
// #202326 (both sample Breeze's Window color); it is its OWN constant by the
// hard-coded rule, never a reference to the palette, and a retune of one must
// not follow the other.
//
// THE ROW HAS NO BORDER LANE, AND ITS LAST CONTENT ROW IS THE BASE LINE
// (architect 2026-09-09, his second look at the top strip relayout: "the
// #4c4e51 line should be the LAST ROW of the tab row — but INSIDE the tab
// row, not outside"). The strip stack allocates 30 authored px of CONTENT
// plus the row's own 6px MARGIN-BOTTOM and nothing else — the lane sits
// under the ICON ROW's own border-bottom with gap 1 between, and the margin
// holds the base line off the lane below (the
// tab-row metric block further down this file, and kdenlive-redesign.md's
// closing section) — and the last `kTabBorderPx` rows of that CONTENT are
// the line.
//
// THE LINE STANDS ONLY WHERE NO TAB DOES (architect 2026-09-09, his third
// look, and it is the PCManFM-Qt crops followed exactly: "I didn't mean to
// imply the unselected tab should have a visible line at the bottom when it
// is not hovered"). The base band has THREE FACES, and a fourth state that
// is the ABSENCE of one:
//   - kRedesignTabLine #4c4e51 ACROSS THE TROUGH — the stretch right of the
//     last tab, which is the only place on that row where no tab stands;
//   - kRedesignTabHoverEdge #496170 across a HOVERED unselected tab's OWN
//     BOX, never the spill it paints under its selected neighbour;
//   - NOTHING under a RESTING unselected tab: its flat fill runs the full
//     content height to the content's foot and IS that row;
//   - and the BREAK under the SELECTED tab, whose box shows the content
//     ground instead — the tab is a mouth into what it opens onto.
// So kRedesignTabLine paints TWO things on this lane, THE TROUGH'S BASE ROW
// and THE SELECTED TAB'S TWO SIDE BORDERS (from its cap's arcs down through
// that row to the content's last pixel), and it paints under no tab at all.
// (Breeze reaches the same picture from the other side, with
// `rect.adjust(0,0,0,+1)` on the unselected tab and a base line drawn under
// the bar; ours is the same pixels.)
//
// THE TWO BORDER ROWS THE LANE CARRIED 2026-08-13..2026-09-09 WERE ONE GREY,
// this constant, AND THAT DELIBERATELY OVERRODE A CROP (architect 2026-08-14:
// "make tab row top border #4c4e51 to match other lines"). THE MEASUREMENT
// STANDS AND IS KEPT HERE so nobody "restores" a TOP line that no longer
// exists: the SOURCE's top line IS #535659 — y=0 of both
// row_3_tab_example.png and row_3_tab_trough.png, full width over tabs and
// trough alike, the KWave/pcmanfm-qt pair agreeing — and the bottom border
// and the tab frame measure #4c4e51 (row_3_bottom_border.png). Breeze does
// draw two roles on this lane. The architect chose PRODUCT-INTERNAL
// CONSISTENCY over the sample: #4c4e51 is the line grey the rest of the
// product's chrome takes. kRedesignTabTopLine, the top row's own constant for
// the one day it existed (2026-08-13..14), was RETIRED with that ruling.
//
// THE UNSELECTED TAB IS ONE GEOMETRY IN THREE COLOURS (architect 2026-09-09,
// his option 1 off the three PCManFM-Qt/Breeze crops in tmp/, then his second
// look): a flat fill kRedesignTabRest at rest and kRedesignTabHover under the
// pointer, its outer top corner rounded and its inner edge extended UNDER the
// selected neighbour by the corner radius (paint_tab_row carries Breeze's
// geometry verbatim) — and THE BASE ROW UNDER IT is the third colour:
// kRedesignTabHoverEdge #496170 across the tab's OWN BOX while it is hovered,
// and the tab's own fill run to the foot when it is not. That is the hover
// face's one difference from rest, and it is the LINE's face rather than the
// fill's, which is what the second look moved: the constant paints the base
// row's hover segment now, not a separate 1px edge under a full-height fill
// (its two homes are 2026-07-31..2026-09-09 and 2026-09-09 on). A HOVERED
// tab's fill therefore stops one line short of the content's foot while a
// RESTING one's does not — the one fork in pass one's fill height, and the
// third look's whole change (paint_tab_row).
//
// #496170 IS MEASURED: row 29 of tmp/previous/review_2026-09-09/his_screenshots/Screenshot_2026-09-09_02-42-29-c.png,
// cols 80..159 — B's own box, exactly, col 79 being A's selected side border
// — which is what fixes the hover segment's SPAN as the hit box and not the
// spill. HARD-CODED AND NOT A BLEND: nothing clean generates it from
// kRedesignTabHover and kRedesignTabLine, and the palette rule forbids
// deriving it anyway. THE CROPS ARE NOW FOLLOWED WITH NO OVERRIDE LEFT ON
// THIS ROW: row 29 of tmp/previous/review_2026-09-09/his_screenshots/Screenshot_2026-09-09_02-41-44-c.png measures
// #1b1d20 across the RESTING B's cols 80..159 — its own fill run to the
// foot — and the grey ran under a resting tab too for the hours between the
// second look and the third, product-internal consistency chosen over the
// sample the way the 2026-08-14 top border was, until the architect withdrew
// it ("I didn't mean to imply the unselected tab should have a visible line
// at the bottom when it is not hovered"). WHAT THE CROPS NEVER SHOW is the
// TROUGH — they are 160px of two tabs — so the grey there is the rule's own
// and not a measurement, and the top border's 2026-08-14 override below
// stands as this row's only surviving one.
//
// kRedesignTabLine is a SECOND structural line grey, distinct from
// kRedesignLine #535659 (row 2's separator and border-bottom, sampled from a
// kdenlive crop): the tab frame, row 4's border and separators and the bottom
// row's border-top measure #4c4e51 in every crop. Both sampled, neither
// derived from the other, and a retune of one must not follow the other.
inline constexpr GuiColor kRedesignContentGround = hex(0x202326);
inline constexpr GuiColor kRedesignTabRest       = hex(0x1B1D20);
inline constexpr GuiColor kRedesignTabHover      = hex(0x263F4D);
inline constexpr GuiColor kRedesignTabHoverEdge  = hex(0x496170);
inline constexpr GuiColor kRedesignTabLine       = hex(0x4C4E51);

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
// The three lanes share the #202326 content ground (kRedesignContentGround) —
// the surface the selected tab opens into, one fact seen again rather than a
// fourth copy of the number.

// THE TRIM LANE IS FLIPPED TO KDENLIVE'S OWN ORIENTATION (architect 2026-09-16,
// undoing his original inversion of it): the lane is now 10 rows of exactly
// THREE surfaces — ground, bar, endcap — and each carries its own 2-row TOP
// BEVEL, row 0 a darker shade then row 1 a lighter one, followed by the 7-row
// face and, since the same ruling, a shared 1px BOTTOM BORDER (row 9,
// kTrimLaneBottomBorder below) under all three surfaces and the midpoint tile
// alike — the one row none of the three surfaces owns on its own. The bevel
// pair is NOT a derivable rule (the three measured pairs fit neither a
// constant delta nor a constant mix toward white/black), so it ships as six
// sampled constants, one pair per surface, unchanged by the flip — only their
// ROW ORDER moved, from the lane's bottom to its top. A FOURTH bevelled
// surface would have no pair and would force the question then, which is the
// point of spelling them out rather than inventing a formula from three
// samples.
inline constexpr GuiColor kTrimLaneBar       = hex(0x2F6888);
inline constexpr GuiColor kTrimLaneEndcap    = hex(0x97B4C4);
// THE MIDPOINT MARK NEEDS NO COLOUR OF ITS OWN (architect 2026-08-01, second
// pass — he overlaid row_5_lane_1_trim_middle.png on the running GUI and ruled
// the crop implemented VERBATIM; re-flipped with the rest of the lane on
// 2026-09-16): the 9x9 crop is exactly a LANE-HEIGHT TILE built from the two
// surfaces this block already declares — bevel rows 0..1 on top, then face
// rows 2..8 in kTrimLaneEndcap #97b4c4 with a 5x5 kTrimLaneBar #2f6888 square
// inset at cols 2..6 flush under the bevel (rows 2..6), the endcaps' own
// #9dbbcb / #94b0c0 bevel pair over its top two rows. On our dark bar that
// reads as a LIGHT SQUARE RING with a dark centre, which is the mark he
// approved in the mockup.
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
// THE LANE'S BOTTOM BORDER (architect 2026-09-16, the new crop
// row_5_lane_1_trim_bottomborder.png, a 1px row under the WHOLE lane width —
// ground, bar, endcaps and the midpoint tile alike, the one row no single
// surface owns). The value coincides with kTrimGroundBevelLo's #131516, this
// same lane's own darker ground-bevel shade — two samples that agree, not one
// fact referenced twice, the hard-coded rule.
inline constexpr GuiColor kTrimLaneBottomBorder = hex(0x131516);

// THE RULER LANE's two inks. The label size is the redesign's ordinary 12pt:
// the composite's label band measures 12 ink rows and ~84px for its label, which
// is what 16px sans produces — the brief's impression that kdenlive's ruler font
// "looks smaller" is not what the pixels say, and the measurement wins.
inline constexpr GuiColor kRulerLabel = hex(0xC2C2C2);
inline constexpr GuiColor kRulerTick  = hex(0x737373);

// THE PLAYHEAD's two colours and the head's one alpha. The HEAD is an aliased
// shape in a single flat grey; the STEM is the paper white that replaces the
// old cursor line at this surface.
//
// THE HEAD IS THE HOLD POSTURE'S LAMP (architect 2026-09-24): while
// AppState::camera_hold stands it paints in kPlayheadHeadHeld, the stem's
// white, and dark it keeps kPlayheadHead's grey — the posture read where the
// eye already is, the centre column, at no cost in height. A STATE COLOUR,
// NOT A CLASS (the accent_for_focus idiom: a named constant and a fork at the
// painter, paint_ruler_row): the marker classes' ladder DISABLED > RED >
// default is untouched and the head joins none of it. The held head takes
// the SAME kPlayheadHeadAlpha — the head stays translucent in both states, so
// the one alpha exception stays one. The stem, the scanner and the column do
// not change. Its repaint is the per-tick comparator's (main.cpp), since the
// bit flips with no damage of its own.
//
// THE HEAD IS THE OPAQUE PALETTE'S ONE RULED EXCEPTION (architect 2026-09-23,
// when the head moved up onto the ruler lane's bottom rows): it composites at
// kPlayheadHeadAlpha over the ruler's timestamps and ticks, "slightly
// translucent" in his words, so the digits read through it. The same day the
// ruler lane grew beneath its labels (kRulerLaneHeightPx) and the head no
// longer reaches the digits, so the alpha now shows through a major tick's
// rise alone. The value is his to tune by eye; no other colour in the tree
// carries an alpha. (Until that
// day a tick crossing the head painted the pre-blended #b7b7b7 measured off
// row_5_lane_3_playhead_tick.png; with real compositing the tick shows through
// the alpha instead and that constant is deleted.)
inline constexpr GuiColor kPlayheadHead      = hex(0x8E8F91);
inline constexpr double   kPlayheadHeadAlpha = 0.8;
inline constexpr GuiColor kPlayheadStem      = hex(0xFCFCFC);
// The hold lamp's lit head (above): the stem's white by ruling, one fact, so
// it is spelled as the stem's constant rather than a second sample.
inline constexpr GuiColor kPlayheadHeadHeld  = kPlayheadStem;

// THE MARKER LANE's colors, measured off row_5_lane_3_marker_{unselected,
// selected,red}.png (56x20, and 56x17 for red). Each class is a FILL plus a
// 1px TOP-EDGE color, and the box carries a 1px LEFT BORDER outside that fill
// (kMarkerFlagBorder, below, where its provenance is recorded); there is no
// right and no bottom outline in any crop.
//
// SELECTION IS A COLOR SWAP AND NOTHING ELSE (row 5), AND THE SWAP IS ONE
// CELL'S (architect 2026-09-05, "light the colour of only the flag that's
// clicked"): a selected marker paints its ADDRESSED cell in the bright pair
// and every other cell of its run — the flag box, the two iteration bound
// cells, each a cell — in its calm pair; the addressed cell
// is the payload for every selected marker but the focus, whose addressed
// cell is AppState::addressed_cell (the cell a press landed on, or the cell
// an editor opened; a focus reached any other way — a walk, a jump, a clear,
// a restore — is addressed at its payload. A bracket-only undo entry's
// restore brought back the bound it had moved until 2026-09-10, when the
// bracket left the undo domain with that entry). The geometry, the stem and the hit rect are
// identical either way. This RETIRES the "selection is not a class" ruling
// for the marker flags — that rule existed because a selected OUTLINE would
// have outranked the disabled pair; here the swap can never outrank
// disabled, because since 2026-08-01 it happens INSIDE it: a selected
// disabled marker blends THIS pair toward the lane ground through
// kMarkerDisabledMix, so it lifts like a live selection and still reads
// switched off, and the defect the old rule guarded against has no site left.
// The bright cell is the whole of the addressed cell's cue: it carries no
// underline and no mark of its own.
//
// The RED crop is 56x17 and supplies COLORS ONLY — its dimensions are the
// regular class's (the architect's own instruction).
inline constexpr GuiColor kMarkerFlagFill        = hex(0x9B59B6);
inline constexpr GuiColor kMarkerFlagEdge        = hex(0x563165);
inline constexpr GuiColor kMarkerFlagFillSel     = hex(0xC974ED);
inline constexpr GuiColor kMarkerFlagEdgeSel     = hex(0x704083);

// THE RED CLASS HAS A REST PAIR AND A SELECTED PAIR, LIKE EVERY OTHER CLASS
// (architect 2026-09-16). Until then red was ONE pair — the BRIGHT one below —
// painted whether or not the marker was selected, on the reading that a
// selection swap would mask the normalization cue. It does not: the class
// ladder DISABLED > RED > default is untouched, so a red marker is red in both
// pairs and only its BRIGHTNESS moves. So red joins the shape the purple, the
// orange and the blue already have — the rest pair at rest, the bright pair on
// the ADDRESSED CELL of a selected marker, read on the SAME `selected` bit
// those pairs read (resolve_flag_face's third argument). No new predicate and
// no new term: a selected red marker lifts exactly as a selected calm one
// does, its other cells staying calm.
//
// PROVENANCE IS THE HISTORY VIEW'S OWN REMOVED LADDER: the dull pair is the
// UNSELECTED removed crop's (row_5_lane_3_marker_red_unselected.png) and the
// bright pair the SELECTED one's, which is the ladder the live class was a
// mixture of before this ruling (the record is at kHistoryRemoved* below,
// where the audit that mixture was queued for is closed). The four values are
// SPELLED HERE as this class's own constants and the history view spells its
// own: two facts that agree, not one fact referenced twice — the hard-coded
// rule.
//
// THE BRIGHT PAIR IS ALSO THE PRODUCT'S ONE INVALID RED — the red flash a
// refused commit paints, on the flag editor's unrolled box and on the dialog
// field alike. Those two surfaces read kMarkerFlagFillRedSel /
// kMarkerFlagEdgeRedSel, CALLED NOT COPIED (the phrase is this palette's, at
// kModalFieldGround below): the invalid red IS the bright red by ruling, one
// fact with one constant, so a retune of the bright pair moves the flash with
// it and the two can never drift.
inline constexpr GuiColor kMarkerFlagFillRed     = hex(0xDA4453);
inline constexpr GuiColor kMarkerFlagEdgeRed     = hex(0x79262E);
inline constexpr GuiColor kMarkerFlagFillRedSel  = hex(0xFF6C7B);
inline constexpr GuiColor kMarkerFlagEdgeRedSel  = hex(0x8E3C44);
// The RED class's REST STEM is its own constant: #da4453 is the architect's own
// explicit value for it. THE STEM FOLLOWS THE SELECTION BIT AS THE FILL DOES
// (architect 2026-09-23, reversing the calm-stem rule: "make the stems the same
// colour as the highlighted flag when a flag is selected, so that it stands
// out" — at a coarse zoom among many markers the playhead is found by looking
// up and the selected stems by looking down): resolve_flag_face reads the
// class AND the selection bit for the stem exactly as for the fill, so a
// selected red marker stems in kMarkerFlagFillRedSel and a resting one in this
// constant, and every other class stems in its column's pair's fill, bright
// when selected. Since 2026-09-16 this constant EQUALS the red class's rest
// fill, by coincidence of provenance rather than by derivation — both are the
// unselected removed crop's fill, arrived at separately — so it stays its own
// constant and a retune of one is not a retune of the other.
inline constexpr GuiColor kMarkerStemRed         = hex(0xDA4453);

// THE SEAM COLUMN between a flag box and the cell to its right is a 1px
// kMarkerFlagBorder column, the same dark rule that already sits one column
// left of every flag and reads as a drop shadow there (architect 2026-08-20,
// a standing ruling since 2026-09-02; kept under the same-hue pairing of a
// flag and its own cells too, architect 2026-09-15: the boundary is worth
// marking — purple on purple then, and blue on blue on the phase-reset
// column since 2026-09-21). The rest of a
// cell's anatomy is the flag's — a 1px edge over the fill across the whole
// box, no right border (the flag's own open right edge).

// THE PHASE-RESET FLAG BOX'S PAIRS — BREEZE'S SELECTION BLUE, AGAIN SINCE
// 2026-09-17 (architect): the phase-reset column's flag box — default
// and selected classes, and the disabled blend of both — paints in Breeze's
// highlight blue. THE SUCCESSION: this column took the blue when it gained a
// hue of its own on 2026-09-15, wore an orange for the one day 2026-09-16
// (trading hues with the since-deleted magnification level markers column),
// and took the blue back on 2026-09-17, when the architect looked at both
// devices and preferred it on the resets. THE PALETTE HOLDS TWO COLUMNS since
// that column's deletion (architect 2026-09-23): the warp column's purple and
// this blue, each through the one class ladder (FlagColumnFace,
// resolve_flag_face, render.cpp). Red's own double duty — the error cue here and the
// history view's REMOVED class there — is ACCEPTED and not swapped away from:
// an error on a regular view is meant to be worked away, so the two never mean
// the same thing for long.
//
// PROVENANCE: #3daee9 is the Breeze highlight blue, the palette's one sampled
// value; the other three are RECORDED DERIVATIONS off it in the relationships
// the marker crops showed between their own four — the edge a darkened shade
// of the fill, the selected pair a lifted fill over a mid-darkness edge.
//
// RED STAYS RED ON EVERY COLUMN (kMarkerFlagFillRed / kMarkerFlagEdgeRed and
// their Sel pair, resolve_flag_face's first live arm, asked before the
// column), and the phase-reset STEM mirrors the warp rule — it wears its
// flag's fill, this calm fill at rest and the Sel fill when selected
// (architect 2026-09-23). THE PHASE-RESET LEAD-IN RING on the waveform wears
// the colour its reset's stem wears (paint_phase_reset_overlay_ring,
// paint_handler.cpp, through phase_reset_stem_color — architect 2026-09-17):
// this calm fill, or the stem red for a reset in the column's red set, and the
// Sel fill of either when the reset is selected — the ring and the stem are
// one object and brighten together (architect 2026-09-23). THE BOUND (hop) CELLS ON
// THIS COLUMN WEAR THIS PAIR TOO (architect 2026-09-21: the cells wear their
// own column's hue — superseding his 2026-09-15 "fine for now", which had
// kept them on the warp column's purple): every bound-cell call site into
// resolve_flag_face passes the face of the column the cells belong to — the
// resting cells, the bound editor's field and the boxes riding it — through
// the same class ladder as the reset's own flag box (disabled > red >
// default, the addressed cell bright in the Sel pair). The argument stays
// required, never defaulted (warp is never the unmarked default).
inline constexpr GuiColor kPhaseResetFlagFill    = hex(0x3DAEE9);
inline constexpr GuiColor kPhaseResetFlagEdge    = hex(0x226181);
inline constexpr GuiColor kPhaseResetFlagFillSel = hex(0x73CFFF);
inline constexpr GuiColor kPhaseResetFlagEdgeSel = hex(0x40738E);

// THE MARKER LANE'S TEXT INK IS BLACK, IN EVERY CLASS AND EVERY STATE
// (architect 2026-08-20, and it is a MEASUREMENT rather than a taste call: he
// went back to kdenlive itself with a bitmap font on the flag text and read
// #000000 out of it for every colour and every state — the reference crops are
// under tmp/screenshots/kdenlive/redesign). It covers the whole lane: the warp
// and phase-reset flag LABELS on all three live classes, the bound cells'
// text, the `h` view's DIFF-FLAG labels (which share this lane's anatomy, so
// they share its ink), and the flag editor's unrolled text and its caret,
// which resolve through the same face. THE ONE EXCEPTION IS THE FLAG EDITOR'S
// SELECTED SUBSTRING (architect 2026-08-28): a selected span is the accent
// under kRedesignLabel on every text surface in the product, the block below.
//
// IT IS ITS OWN CONSTANT AND NOT A RETUNE OF kRedesignLabel, which stays
// #fcfcfc: that value is the whole redesign's label ink — the menu row, the
// icon row, the bottom row, the tooltips, the dropdowns, the clock, the ruler's
// emphasized labels — and this ruling is about the marker lane alone, whose
// surfaces are the only SATURATED FILLS the product paints text on. Two facts,
// two constants.
//
// WHAT IT BUYS, per class (black against the fill, versus the #fcfcfc it
// replaces): the calm purple is a wash (4.50 vs 4.51) and everything else is a
// gain, the brighter the fill the larger — selected purple 7.21 vs 2.82, the
// red class 4.93 vs 4.15 at rest and 7.68 vs 2.66 selected, the PHASE-RESET
// column's blue 8.43 vs 2.41 (the case that prompted the ruling: light ink on
// #3daee9 was illegible) and 12.1 vs 1.66 selected, the diff lane's green 8.72 vs 2.33 and its red
// 4.93 vs 4.11. So the one class that does not gain does not lose either,
// which is what makes a single ink honest across the ladder.
//
// THE DISABLED FACE KEEPS ITS MECHANISM AND CHANGES DIRECTION. The label still
// blends toward the surface it sits on, through the one mix_color owner at the
// one kMarkerDisabledMix fraction — but from BLACK now, so a dimmed label
// resolves DARKER than its dimmed flag (~#2f2438 on ~#3f304a) where it used to
// resolve lighter (~#6e6377). The dimming still reads, at a lower ratio (~1.21
// against the old ~2.1); it is the architect's own call and is flagged for his
// glass check rather than pre-corrected here.
inline constexpr GuiColor kMarkerFlagLabel       = hex(0x000000);

// THE SELECTION GROUND IS THE ACCENT AND THE SELECTED LETTERS ARE THE LABEL
// WHITE, ON EVERY TEXT SURFACE (architect 2026-08-28: "what Breeze Light does
// with dark text... let's just do that everywhere for consistency"). One
// pairing for every run the product lets a user select in — the three dialog
// editors' shared field and the marker lane's flag editor alike:
// kRedesignAccent #3daee9 behind the selected substring, kRedesignLabel
// #fcfcfc for its glyphs. It is Breeze Light's own selection, which is where
// that accent value came from to begin with.
//
// IT NEEDS NO CONSTANT OF ITS OWN, and the one it had is RETIRED:
// kMarkerEditorSelectionBand (#fcfcfc) named the WHITE FIELD / BLACK TEXT band
// that stood from 2026-08-20 on kdenlive's text-input precedent, and this
// ruling replaces that precedent whole. A selection ground with no derivation
// is the accent itself, so both painters read kRedesignAccent directly and
// there is no third constant to keep in step with it.
//
// THE UNSELECTED INK IS UNTOUCHED on both surfaces: the flag editor's run
// stays kMarkerFlagLabel black (the lane's ink, above) and the dialog field's
// stays kRedesignLabel. Only the SELECTED substring changes colour — which is
// what makes that span the one place the marker lane paints text that is not
// black, and what let the dialog field DROP its knockout pass (its run already
// paints in kRedesignLabel, so the selected glyphs need no second show).
//
// THE CARETS ARE UNTOUCHED TOO. A caret is INK, not field, so each keeps its
// own surface's: black in the flag editor, kRedesignLabel in the dialog field.
// Both read against the accent, so neither needed a rule of its own.
//
// THE RECORDED COST: #fcfcfc on #3daee9 is a 2.41 contrast ratio, the same
// pairing the marker lane's ink ruling above rejected for the flag boxes.
// It stands here because a SELECTION is transient and is marked by its GROUND
// as much as by its ink, and because one convention across every editor was
// the ruling's stated point. The paint sites are render_flag_editor_box
// (render.cpp) and the modal field painter (paint_handler.cpp).

// THE HISTORY VIEW'S TWO DIFF CLASSES, measured off
// row_5_lane_3_marker_green_{unselected,selected}.png and
// row_5_lane_3_marker_red_{unselected,selected}.png (all four 56x17). They paint
// the `h` history mode's marker lane, where a GREEN flag is a line the session
// has and the shown commit did not (added) and a RED flag one the commit had and
// the session dropped (removed). Each crop is read the same way the live marker
// crops above are: column 0 is the 1px LEFT BORDER (#131516 in all four, the
// same kMarkerFlagBorder the live classes take — one more agreeing sample), row
// 0 away from the corners is the 1px TOP EDGE, and the interior modal value is
// the FILL. The measured values, in that order:
//   green unselected  fill #1abc9c  edge #0e6857
//   green selected    fill #22f4cb  edge #138871
//   red   unselected  fill #da4453  edge #79262e
//   red   selected    fill #ff6c7b  edge #8e3c44
//
// SELECTION HERE IS THE MODE'S OWN FOCUS — at most one diff flag, set by a plain
// click on it — and it is the same color swap the live lane's selection is,
// which is why the crops come in pairs and both pairs are constants.
//
// THE DISABLED AXIS RIDES OVER THESE FOUR PAIRS (architect 2026-08-22) with no
// fifth pair and no constant of its own: a half whose line is disabled in its own
// side's commit damps the pair the swap above already chose, through the LIVE
// lane's mix owner at kMarkerDisabledMix over kRedesignContentGround. A recorded
// DERIVATION rather than a measurement — no crop shows a disabled diff flag, so
// what these eight values pin down is this mode's LIVE ladder exactly as
// measured, and the dimmed rendition is the marker lane's own rule reaching a
// second set of inks.
//
// THE RED PAIR IS SAMPLED AFRESH RATHER THAN REUSED, deliberately, even though
// its values now coincide with the live red class's pair for pair: the two are
// TWO FACTS THAT AGREE, each spelled at its own constant, which is this
// palette's hard-coded rule — a retune of one is not a retune of the other.
// THE AUDIT THIS PARAGRAPH QUEUED IS CLOSED (architect 2026-09-16): the live
// class used to be a MIXTURE of the two red crops — kMarkerFlagFillRed /
// kMarkerFlagEdgeRed held the SELECTED crop's pair and kMarkerStemRed the
// UNSELECTED crop's fill, so three constants could not be re-read as a ladder
// — and the live class now carries BOTH crops as its rest pair and its
// selected pair, read on the selection bit exactly as this mode's focus swap
// reads its own. THE STEM FOLLOWS THE SAME SWAP (architect 2026-09-23, the
// live lane's rule reaching this one so the two ladders agree on what
// selection does to a stem): a focused or selected diff flag stems in its
// class's Sel fill, a resting one in the class's rest fill.
//
// THE GREENS ARE THIS VIEW'S ALONE, so a live flag can never read as a diff
// flag. Nothing outside this mode paints them.
inline constexpr GuiColor kHistoryAddedFill      = hex(0x1ABC9C);
inline constexpr GuiColor kHistoryAddedEdge      = hex(0x0E6857);
inline constexpr GuiColor kHistoryAddedFillSel   = hex(0x22F4CB);
inline constexpr GuiColor kHistoryAddedEdgeSel   = hex(0x138871);
inline constexpr GuiColor kHistoryRemovedFill    = hex(0xDA4453);
inline constexpr GuiColor kHistoryRemovedEdge    = hex(0x79262E);
inline constexpr GuiColor kHistoryRemovedFillSel = hex(0xFF6C7B);
inline constexpr GuiColor kHistoryRemovedEdgeSel = hex(0x8E3C44);

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
// the class color over the lane ground (kRedesignContentGround #202326), per
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
//
// THIS FRACTION IS THE SURFACES' — fill, top edge and left border, on the flag,
// the cells AND, since 2026-08-22, the `h` view's DIFF FLAGS, whose halves
// dim by their own commit side's disable bit through these same expressions over
// the kHistoryAdded*/kHistoryRemoved* inks (render_history_diff_flags owns that
// ruling; the derivation is the live lane's, applied to another set of inks, so
// no constant of its own was born). The LABEL took this fraction too until
// 2026-08-20 and takes its own now (the constant directly below); the surfaces
// are untouched by that split and every shape a disabled marker paints still
// damps at exactly 25%.
inline constexpr double kMarkerDisabledMix = 0.25;

// THE DISABLED LABEL'S OWN FRACTION (architect 2026-08-20), split off from the
// surfaces' above on the day the lane's ink went black. The MECHANISM is
// untouched — the label blends toward the surface it sits on, through the one
// mix_color owner — and only the fraction differs, because the two values were
// calibrated against opposite problems.
//
// THE OLD 0.25 WAS WHITE-SUPPRESSION. With #fcfcfc ink the dimmed label's
// danger was standing OUT: near-white on a dimmed flag shouts, and keeping only
// a quarter of it pulled a disabled label back to ~#6e6377 (~2.1:1), which was
// the intended "dimmed but present". BLACK NEEDS THE OPPOSITE CORRECTION.
// Blending black toward the fill makes the label DARKER, so the same quarter
// pulled it almost into the flag (~#2f2438, ~1.21:1) — the fraction was
// suppressing something that was no longer sticking out.
//
// 0.75 IS THE FIRST GUESS, and it is deliberately near a CEILING rather than at
// a target. A darker-than-fill ink cannot reach 2:1 on the calm purple's dimmed
// fill at all: that fill is #3f304a (relative luminance 0.0367), so even PURE
// BLACK tops out at (0.0367 + 0.05) / 0.05 = 1.73:1. The curve is flat near
// that ceiling — 0.25 gives 1.21, 0.50 gives 1.42, 0.75 gives 1.60, 1.00 gives
// 1.73 — so 0.75 buys nearly all of the reachable contrast while leaving the
// label visibly inside the disabled face rather than painting it at full live
// strength. On the brighter dimmed pairs it lands where the ruling asked: the
// selected flag 1.80, the red class 1.59 at rest and 1.81 selected (it gained
// its rest pair 2026-09-16; the 1.81 is the bright pair the whole class used
// to wear). (THE PHASE-RESET FLAG BOX reads ~1.90 against its own #274557 —
// a lighter blue fill whose ceiling is 2.10, headroom the calm purple does not
// have — off the generic mix_color call resolve_flag_face feeds it, no
// constant of its own and the fraction unchanged.) ONE
// fraction for both surfaces, glass retunes.
inline constexpr double kMarkerDisabledLabelMix = 0.75;

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

// THE MAGNIFICATION'S GHOST INK (architect 2026-09-24): the lit magnification
// lamp paints each column's leveled-and-expanded bar in this colour BEHIND the
// raw bar, which keeps kWaveformInk (the rule is at render_waveform's
// declaration). A FAINT GHOST: the magnified shape is large enough to carry
// itself with little contrast, so the raw picture stands over it and the ghost
// does not read as a second figure beside it. SAMPLED from the architect's
// kdenlive screenshot of 2026-09-24 (tmp/Screenshot_2026-09-24_21-12-39.png,
// not committed): the current kdenlive paints its two channels in different
// inks — upper #1c816b, our kWaveformInk, lower #1f8b4c — over a canvas
// #135647, and the ghost takes that NEW CANVAS as its ink (the lower channel's
// #1f8b4c, chosen first the same day, read as a second figure and is
// superseded). #135647 sits within a few units of the kWaveformCanvas :
// kWaveformInk 1:1 mix, (23, 89, 75); the architect weighed that derived value
// and set it aside for the sampled constant, as the palette rule prefers a
// sampled kdenlive crop. The canvas STAYS kWaveformCanvas #12312b (the
// architect observed the new canvas and did not rule it in as the ground).
inline constexpr GuiColor kWaveformGhostInk = hex(0x135647);  // (19, 86, 71)

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
// IT IS AN OPAQUE GROUND RECOLOR, NOT A BLEND (paint_region_ground, painted
// BEFORE the plate blit): the span's canvas is REPLACED by this colour, and the
// ink then composites over it exactly as it composites over the plain canvas.
// Since the aliasing deletion the plate's alpha is BINARY, so an ink pixel is
// fully opaque and a gap is fully transparent: this colour shows through the
// gaps and blends with nothing.
inline constexpr GuiColor kWaveformRegionCanvas = hex(0x24433F);  // (36, 67, 63)

// THE HIGHLIGHT'S OTHER HALF — THE SAME LIFT APPLIED TO THE INK (architect
// 2026-08-18: "apply overlay alpha to wave along with canvas on region
// highlight"). The ground recolor alone lit the background behind unlit
// content; lifting the ink too makes the span read as ONE lit region.
//
// DERIVED EXACTLY AS ITS SIBLING WAS, so the pair is one construction rather
// than a colour and a guess: Breeze's own View -> ViewAlternate lift of
// +9/+9/+10 per channel, taken TWICE — the theme's own relationship applied
// twice, not a tint invented for it —
//     kWaveformInk (28, 129, 107) + 2*(9, 9, 10) = (46, 147, 127) = #2e937f
//
// STILL FULLY OPAQUE, NOT A WASH: paint_region_ink writes this colour over
// every plate pixel carrying the ink's word in a second pass AFTER the blit
// (the plate's alpha is BINARY), so every ink pixel inside the span becomes
// exactly this colour and every gap is left untouched — still showing the kWaveformRegionCanvas ground the first pass
// laid down. A translucent wash painted over the plate is the retired form the
// opaque recolor model rejects, and this is not it.
//
// THE ARCHITECT'S TUNING KNOB, explicitly — both halves of it: each lift is a
// derivation and not a measurement, so this constant and kWaveformRegionCanvas
// above are the two to move if the highlight wants to be stronger or weaker,
// and they are the whole of what the region path has to tune.
inline constexpr GuiColor kWaveformRegionInk = hex(0x2E937F);  // (46, 147, 127)

// THE GHOST'S LIFT (architect 2026-09-24): the region highlight lifts the
// magnification's ghost by the same construction it lifts the ink, so a lit
// region over a lit lamp still reads as one lit region carrying both inks —
// Breeze's View -> ViewAlternate lift of +9/+9/+10 per channel, taken TWICE:
//     kWaveformGhostInk (19, 86, 71) + 2*(9, 9, 10) = (37, 104, 91) = #25685b
// Opaque like its sibling: paint_region_ink writes it over every ghost pixel
// inside the span, keyed by the plate's ghost word.
inline constexpr GuiColor kWaveformRegionGhostInk = hex(0x25685B);  // (37, 104, 91)

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

// -- ROW 7's CROP: THE OLD STATUS LANE'S CHROME, AND ITS ONE CORRECTION -----
//
// Measured off row_7_text.png (407x33): a 1px #4c4e51 TOP border, 31 rows of
// #202326 ground, and a 1px #17181a bottom row. THE GROUND AND THE TOP LINE
// ARE THE ROW-3/4 CONSTANTS REUSED, on the judgment those rows already set:
// #202326 is kRedesignContentGround (the selected tab's interior, the icon
// row's ground, row 5's lane grounds, the unified bottom row's — one Breeze
// Window fact seen again, not a fifth sample of the same number; the TAB ROW's
// own ground left that set on 2026-08-13 and the bottom row did not follow it,
// being content), and #4c4e51 is kRedesignTabLine (row 3's frame grey, row 4's
// separators and border, and the bottom row's own border-top).
//
// THE CROP'S LAST ROW WAS NEVER A KDENLIVE BORDER (architect 2026-08-29): "it
// duplicates the xfce4-panel border and was never a kdenlive border" — the
// near-black #17181a line under the crop is the xfce4-panel's TOP EDGE showing
// under kdenlive's window, not kdenlive's own foot, so the provenance the
// window-foot seam rested on was wrong from the start. THE SEAM IS RETIRED FOR
// GOOD with that correction, and `kRedesignBottomLine` is deleted: it was
// retired once already on 2026-08-12, when the lane it bordered left the
// window's edge, stayed retired through the relayout's commit B on the rule
// that reinstating it would be a ruling rather than a consequence of the
// restack, was REINSTATED FOR ONE DAY on 2026-08-29 under the status bar that
// briefly stood on the foot, and went with that bar the same evening on the
// corrected provenance. The bottom row's ONE chrome line is its border-top,
// which is where the crop's own top border belongs.

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

// -- The DROPDOWNS' own chrome (their own crops) ----------------------------
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

// THE NAVIGATION DROPDOWN'S ACCELERATOR COLUMN (2026-08-02, sampled off
// dropdown_full_hotkeys.png): the hotkey text is dimmer than the label, and its
// full-coverage pixels read #b8b9ba on every row of that crop — the bare keys
// (T, =), the spelled chords (Ctrl+Shift+Space) and the function keys alike, one
// flat ink.
//
// IT IS A SAMPLE, AND ITS DERIVATION IS RECORDED rather than shipped: the value
// is EXACTLY kRedesignLabel composited over kRedesignPopupGround at Qt's own
// 178/255 alpha — (252*178 + 28*77)/255 = 184.4, (252*178 + 31*77)/255 = 185.3,
// (252*178 + 34*77)/255 = 186.2 — i.e. the source draws its accelerators at
// ~70% opacity. The constant ships as the LITERAL because the palette is opaque
// hard-coded constants by ruling and because the fraction is the source's
// implementation detail, not a relationship this product maintains; it is NOT
// kRedesignDimMix (0.52, the tooltip's hint line) and NOT the disabled mix
// (0.322) — three different dims, three constants, checked before adding this
// one.
//
// It is the item's ink in EVERY face: like the label above it, the hotkey does
// not change color under the hover tint or the pressed accent fill (the fill IS
// the whole cue, exactly as the settings menu's rows have always worked).
inline constexpr GuiColor kRedesignPopupHotkey = hex(0xB8B9BA);

// A DISABLED MENU ITEM'S TWO INKS — kRedesignPopupDisabledLabel #686a6c and
// kRedesignPopupDisabledHotkey #515356. They stood 2026-08-08 to 2026-08-15 for
// the Navigation dropdown's one greyed row, were deleted producer-less with it
// (their sample and derivations kept here, "because a menu that greys again
// wants exactly these numbers"), and RETURN 2026-09-24 with the truthful menus
// (dropdown_item_enabled, app_state.h): every command and settings row now
// greys on its command's cheap refusals, and paint_dropdown inks a greyed row
// with this pair in place of kRedesignLabel / kRedesignPopupHotkey. They are
// the dropdown's own sampled disabled class, not kRedesignDisabledMix (the
// roster's, which would give (100, 102, 104) here — the derivation below
// says why the two differ).
//
// They were sampled off dropdown_disabled.png — its "Group Clips" / "Ungroup
// Clips" rows, which kdenlive greys with no selection in the timeline; the same
// crop's live rows carry the ordinary label and accelerator inks, so the pair
// was a difference measured against a known reference rather than a lone
// reading. The row kept its GEOMETRY and its ground: a greyed item still
// occupied its slot, painted no hover or press face, and differed from a live
// one in ink alone.
//
//   LABEL — full-coverage pixels read #686a6c = (104, 106, 108) over this menu's
//   own (28, 31, 34) ground.
//   HOTKEY — #515356 = (81, 83, 86) on the same rows.
//
// BOTH DERIVATIONS WERE RECORDED AND NEITHER SHIPPED AS A FACTOR, the palette
// being opaque hard-coded constants by ruling. The label is kRedesignLabel over
// kRedesignPopupGround at ~0.339 — (252-28)*0.339 + 28 = 103.9, (252-31)*0.339 +
// 31 = 105.9, (252-34)*0.339 + 34 = 107.9 — which is NOT kRedesignDisabledMix
// (0.322, row 2's own sampled dim, which would give 100 here) and NOT
// kRedesignDimMix (0.52): four dims, four constants, each checked against
// the others before being added. The HOTKEY is then the disabled LABEL taken at
// the accelerator column's own 178/255 — (104*178 + 28*77)/255 = 81.0, (106*178
// + 31*77)/255 = 83.4, (108*178 + 34*77)/255 = 85.7 — i.e. the source dims the
// row once and its accelerator twice, exactly the relationship the live pair
// still has (kRedesignPopupHotkey records the same 178/255 over kRedesignLabel).
//
// LIKE THE LIVE INKS THEY ARE THE ROW'S INK IN EVERY FACE — which for a
// disabled row means the only face there is: the input side never hovers or
// arms one, and the painter reads that same predicate before drawing a face at
// all.
inline constexpr GuiColor kRedesignPopupDisabledLabel  = hex(0x686A6C);
inline constexpr GuiColor kRedesignPopupDisabledHotkey = hex(0x515356);


// THE MODAL SURFACE'S CHROME — TWO CONSTANTS, THE FIELD'S (paint_modal_dialog
// is the one consumer). THE MODAL IS THE BOTTOM ROW since 2026-08-13
// (architect, scrapping the centered box he ratified the day before: "it looks
// sloppy — no compositor drop shadow, and faking one wouldn't work"): while a
// prompt or a dialog editor stands, the row's tenants stand down whole and the
// modal paints in the lane — on THE ROW'S OWN GROUND, at THE ROW'S OWN PADS,
// so it needs no ground, no frame and no margin of its own.
//
//   FIELD GROUND — editor.png's inset interior reads #141618 (y=6..34 at
//   x=200).
//   FIELD BORDER — its 1px frame reads #4c4e51 (y=5 / y=35), coinciding with
//   kRedesignTabLine and deliberately NOT it (the standing numerically-equal-
//   is-not-the-same-constant rule at kRedesignRowGroundUnfocused).
//
// THE BOX'S OWN CHROME IS RETIRED WITH THE BOX (2026-08-13): kModalGround
// (#202326 off modal_popup.png's body), kModalBorder (#535659 off its 1px
// frame), kModalBorderPx, kModalPadPx (the crop's 11px content margin — the
// row's own pad is the margin now, icon_row_pad_x since 2026-08-14)
// and kModalWindowMarginPx (the
// narrow-window clamp — the row IS the clamp) are all deleted producer-less.
// The crop's #292c30 titlebar band stays untranscribed (ours has no title
// bar). ITS #4882a1 IS TRANSCRIBED SINCE 2026-08-13's SECOND MODAL RULING, as
// kModalFocusLinePassive below: this block used to record it as the
// Enter-default's half that we decline to have, and the two focus STRENGTHS
// gave it a job — a passively focused button is exactly what Enter answers,
// so the shade is doing the same work here it does in the crop, under a name
// that says focus rather than default. Its #2d4655 companion was transcribed
// the same day as the keyboard-focus FILL, which both strengths share.
//
// The modal's BUTTONS carry no constants here APART FROM THE FOCUS PAIR below:
// their box is the deleted toolbar
// row's 32px box (kModalBtnBoxPx and the 9/10 label pads, paint_handler.cpp —
// row 2's own arithmetic, kept when the row dissolved into the icon row at the
// 2026-08-12 relayout) and their pointer faces are the icon row's, both REUSED
// not re-sampled (the architect's explicit mix — "the modal buttons should
// basically look like the icons, but with a resting outline", 2026-08-13, the
// resting outline being kRedesignLine, which is both this product's chrome-line
// grey AND the grey modal_popup.png measures on its own non-default buttons:
// two facts that agree, so the existing constant ships and no kModal one
// exists). The invalid red flash
// recolors the FIELD in the marker-flag red class's BRIGHT pair
// (kMarkerFlagFillRedSel / kMarkerFlagEdgeRedSel — the one invalid red, which
// IS the bright red by the 2026-09-16 ruling that gave that class a rest pair
// of its own), called not copied.
inline constexpr GuiColor kModalFieldGround = hex(0x141618);
inline constexpr GuiColor kModalFieldBorder = hex(0x4C4E51);

// THE FOLDER OVERLAY'S ROW FACES (architect 2026-08-28, R32) — the list panel
// that stands in the keyboard's band (folder_overlay.h), whose ROWS ARE
// BUTTONS (R31) painted on a FILE MANAGER'S palette rather than the
// keyboard's. THE REFERENCE IS TWO PROGRAMS THAT AGREE: kdenlive's project
// bin and pcmanfm-qt's compact view, both read off the architect's own screen
// (his 2026-08-28 17:46..18:04 shots). NO ALTERNATING ROWS — the band's
// GROUND is kModalFieldGround #141618, the modal field's own, and a resting
// row paints NO FILL AT ALL, so the ground is what shows between and behind
// the rows. The three faces a row adds to it:
//   HOVER            -> kFolderRowHover under a 1px kFolderRowHoverOutline
//                       frame (the button's own outline width and inset);
//   SELECTED         -> kRedesignAccent #3daee9 (the highlight band, which is
//                       also the list's keyboard focus) under kRedesignLabel
//                       ink, the ink every row wears in every face;
//   HOVERED+SELECTED -> kFolderRowHoverSelected, the band lifted under the
//                       pointer exactly as the hover face lifts the ground.
//
// kFolderRowHover IS A DERIVATION, NOT A SAMPLE, and it is the row-2 click
// face's own arrangement (kRedesignClickMix above) applied to this band's
// ground: the accent at 30% over kModalFieldGround. The architect's reading of
// the reference is #204357; the mix computes
//   r: 0.3*61  + 0.7*20 = 32.3  -> 32  (0x20)
//   g: 0.3*174 + 0.7*22 = 67.6  -> 68  (0x44)
//   b: 0.3*233 + 0.7*24 = 86.7  -> 87  (0x57)
// so it reproduces the sampled value on two channels and lands ONE 8-bit step
// brighter on the green. The RELATIONSHIP ships rather than the literal — the
// same judgment kRedesignClickMix's own block records — because what the
// reference pins down is "the accent's click wash over this ground", and a
// frozen literal would drift from that the moment either end is retuned.
// THE OTHER TWO SHIP AS SAMPLES, and only one of them had to. #44bfff is
// genuinely independent: it is not the accent over this ground at any single
// fraction (the three channels solve to 1.17 / 1.11 / 1.11) and not the accent
// lightened toward the label at one either (0.04 / 0.22 / 0.11), so there is
// nothing to derive it from. #3694c5 IS a wash of the same accent over the
// same ground — 20 + 41*0.83 = 54.03, 22 + 152*0.83 = 148.16,
// 24 + 209*0.83 = 197.47, reproducing all three channels exactly, which is
// the reference telling us its hover FRAME is the same colour as its hover
// FILL taken further — but 0.83 is a fraction this product has nowhere else,
// and a new mix constant that exactly one caller reads is a knob rather than a
// relationship. The literal ships and the arithmetic is recorded here, which
// is what a retune of the accent would need.
inline constexpr GuiColor kFolderRowHover =
    mix_color(kRedesignAccent, kModalFieldGround, kRedesignClickMix);
inline constexpr GuiColor kFolderRowHoverOutline   = hex(0x3694C5);
inline constexpr GuiColor kFolderRowHoverSelected  = hex(0x44BFFF);

// THE KEYBOARD-FOCUS FACE — the modal's ONE face with no icon-row counterpart,
// so it is the one that needed sampling (architect 2026-08-13; the focus ring
// itself is the ruling's part D).
// AN ACTIVELY focused dialog button paints THREE things, outermost last:
//   kModalFocusRing #284c61 — a 2px stroke OUTSIDE the button box, the halo
//                   that says "this is where the keyboard is". It grows the
//                   button by 2px on every side, which is why the button row
//                   RESERVES that ring's space for every button and paints it
//                   for one (kModalFocusRingPx, paint_handler.cpp): moving the
//                   focus must never reflow the row.
//   kRedesignAccent — the ordinary 1px outline, the hover face's own, so a
//                   focused button reads as "pointed at" plus the halo rather
//                   than as a fourth outline color.
//   kModalFocusFill #2d4655 — the interior, a shade the crop carries and this
//                   product had never transcribed until that day.
// A PASSIVELY focused button paints TWO — the same fill under
// kModalFocusLinePassive #4882a1, and NO halo. The two strengths and where
// each is assigned are at AppState::modal_dialog_focus_active; what the
// palette says about them is only this: the FILL is the focus itself and both
// strengths wear it, while the OUTLINE is the strength, rising to the accent
// when the pointer or the keyboard's own walk has claimed the button.
// Literals, not derivations: nothing clean generates any of the three from
// this row's ground and the accent, and all three are Breeze's own focus
// shades.
// PROVENANCE — BOTH TRANSCRIBED FROM A CROP, like every other sampled color
// here (Screenshot_2026-08-13_03-04-28.png, the kdenlive screenshots' folder;
// the crops are authoring-time artifacts and are not in the repository, exactly
// as modal_popup.png and editor.png are not). A horizontal scan through the
// focused "Cancel" button's middle (y=584) reads, left to right: 2px #284c61
// at x=1222..1223, 1px #3daee9 at x=1224, then the #2d4655 interior from
// x=1225 — mirrored at the right edge (#3daee9 at 1307, #284c61 at
// 1308..1309) — with the neighbouring unfocused button's 1px #535659 resting
// outline at x=1215. So the three values, the 2px halo and the 1px accent
// outline are all measured, and the ORDER above is the crop's own.
// THE PASSIVE LINE HAS ITS OWN CROP AND ITS OWN SCAN (focus_passive.png, the
// same folder, 2026-08-13): a horizontal scan through a PASSIVELY focused
// "Cancel" reads, left to right, the row ground, then 1px #4882a1, then the
// #2d4655 interior — NO outer band at all, which is what makes the halo the
// ACTIVE strength's alone rather than a thing every focused button wears.
// Against it, the active scan above reads 2px #284c61 + 1px #3daee9 over the
// same fill, so the two crops differ in exactly the two places the ladder
// says they do. (#4882a1 also fills modal_popup.png's Enter-default button
// — the same shade doing the same work under a truer name, since the
// passively focused button IS what Enter answers.)
inline constexpr GuiColor kModalFocusFill        = hex(0x2D4655);
inline constexpr GuiColor kModalFocusRing        = hex(0x284C61);
inline constexpr GuiColor kModalFocusLinePassive = hex(0x4882A1);

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
// redesign_font_size_px, the corner radii, the ruler's unrounded pitch
// compare — are a different concept (they never round to int, or round to a
// double on purpose) and deliberately do not come through here.
inline int scaled_px(double authored) {
    return static_cast<int>(std::nearbyint(authored * gui_scale_factor()));
}
// The floored form: `floor_px` is the PER-METRIC minimum the accessor states,
// so a small factor cannot zero a structural dimension.
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
// one text size in the tree is redesign_font_size_px(), below.)

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
// the toolbar, and neither does this row: the view bar's blue touches the
// icon row's ground directly.
//
// THE 30 IS THE PILL'S, AND THE PILL IS THE LANE (architect 2026-09-09, on
// seeing the row at 34 with the dropdown hanging four pixels under the
// pill: "make the height of the top row based on the thirty pixels of
// File/Edit, keep all the margins, borders and padding for the view
// buttons, and make the view buttons also thirty so they all fit in a
// thirty-pixel row ... take from the INSIDE height, the actual content,
// about four pixels ... this way the dropdown will touch the first row, as
// it does in kdenlive"). File's hover pill measures rows 26-55 in
// tmp/previous/review_2026-09-09/his_screenshots/kden-hover.png — 30 rows — and that number is the lane whole: the
// anchors' pill fills it top to bottom and IS the anchor's published hit
// rect, and its foot is the lane's foot, which is where the dropdown and
// its damage band hang (top_menu_row_area — paint_dropdown and
// toggle_dropdown read the same accessor), so the popup touches the icon
// row's first pixel, kdenlive's own picture.
//
// THE VIEW BAR DERIVES DOWN FROM THE LANE: its button box is the content
// less its two 1px vertical margins — 28 at 100% — with every margin, border
// and pad kept (kViewBarBtnMarginPx and its siblings, paint_handler.cpp),
// the lane taking the four pixels out of the box's INSIDE height rather than
// the box setting the lane. kdenlive's own bar box is 32 inside its 34 lane
// (tmp/previous/review_2026-09-09/his_screenshots/kden-view.png, rows 27-58 in 26-59); ours is 28 inside the pill's 30,
// and the 5px corner and the 1px frame fit the 28 as they fit the 32
// (redesign_face_box insets by half its stroke and rounds inside the box).
// THE VIEW BUTTONS' HIT RECT IS THE LANE'S FULL HEIGHT — the button's
// columns by the lane's rows, the blue div's own ("I'm assuming the hit box
// for all of them is the same as the entire background, the blue background
// ... they should have the same sort of sizes and hit boxes because they sit
// on the row") — so the anchors and the view buttons publish the same kind
// of rect; the 28 box is painted INSIDE the published rect, the two margins
// subtracted by the ONE helper view_bar_face_rect (paint_handler.cpp) and
// nowhere else, so what the pointer hits and what the eye sees cannot drift.
//
// 30 -> 34 -> 30 -> 34 -> 30. The 2026-08-02 view bar raised the content to
// 34 = 1 + 32 + 1 so the bar's buttons could keep the icon row's 32px box;
// the architect took it back to the crop's own 30 on 2026-08-21, the box
// deriving down as 28; the 2026-09-09 relayout kept that 30 while retiring
// the box's two vertical margins (the box became the content whole, 30);
// later that day his remeasure of kdenlive put the row on 34 = 1 + 32 + 1
// with the margins restored and the pill on its own authored 30 riding the
// lane's top edge with 4 rows of ground under it (kMenuPillHeightPx /
// menu_pill_h_px, which lived for those hours and are gone); and the next
// morning he saw the dropdown hanging under the pill and ruled the pill the
// lane again — the box shrinking from its content, the margins kept. (THE
// 1px MARGIN-BOTTOM the lane carried 2026-08-02..2026-09-09 —
// kMenuRowMarginPx / menu_row_margin_h_px, which held the right float's
// blue off the next lane's ground — stays retired: the bar's blue meets the
// icon row's ground directly.) Everything below moves automatically,
// through main.cpp's lane table.
//
// The row sizes on gui_scale_factor() like every other lane in the tree (the
// font axis the pre-redesign lanes used to ride is deleted — see the gui_scale
// block above). Rounded with std::nearbyint and floored like every other lane
// metric; the height floor is far below the scaled height. At the tablet's
// 225% the lane is 68 (30 x 2.25 = 67.5, banker's up to the even 68) and the
// view bar's box 64 inside its two 2px margins. TWO ACCESSORS FOR ONE NUMBER,
// deliberately: the lane table reads the LANE and the painter the CONTENT,
// the vocabulary every other row keeps, and this row's lane simply has no
// other term in it.
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
// which used to read row 2's. The crops and the row-2 record stay in
// kdenlive-redesign.md.)

// Authored pixel geometry of the TAB ROW — the top strip's lane 2 since
// 2026-09-09, under the icon row and directly ON the trim bar (row 3 of
// the redesign: the "A" / "B" Breeze tabs; it was lane 1, under the menu row,
// from the 2026-08-12 relayout until the 2026-09-09 one — kdenlive-redesign.md's
// closing section). Measured at 100% gui_scale off
// row_3_tab_{rest,hover,selected}.png (30 tall) and, for its geometry since
// 2026-09-09, the three PCManFM-Qt/Breeze crops that section names.
//
// THE CSS BOX MODEL IS THE RULED VOCABULARY (architect 2026-07-31): the
// architect's stated 30 is CONTENT and anything outside it is its own term,
// with the LANE their sum. tab_row_content_h_px() is the ground and tab band —
// the height every tab box fills, flush, top to bottom; tab_row_h_px() is the
// lane. Rides gui_scale_factor() like row 1, not the monospace font's axis.
//
// THE LANE HAS NO BORDER AT EITHER EDGE since 2026-09-09. It carried a border
// row at each edge from 2026-08-13 until then (32 at 100%; kTabRowBorderPx /
// tab_row_border_h_px are retired with the relayout): the top line had landed
// to stop the row bleeding into the menu row above it, and the menu row is no
// longer above it — the row sits under the icon row's own border-bottom, so
// the two lines the lane owned had nothing left to
// separate. THE BOTTOM ONE CAME BACK INSIDE (architect 2026-09-09, his second
// look): the CONTENT's LAST ROW is the base line, which costs the lane no
// height at all, the line living in the 30 rather than beside it. The
// measurements behind all of this, the one-grey ruling and the base line's
// THREE FACES — the grey ONLY WHERE NO TAB STANDS, #496170 under a hovered
// unselected tab, nothing under a resting one and the break under the
// selected one — stay at the palette block's row-3 section.
//
// AND THE LANE CARRIES A 6px MARGIN-BOTTOM (architect 2026-09-09, from
// PCManFM-Qt: "PCManFM-Qt has six pixels of margin below the tab row; with
// the icons moved up, the tab row abuts the lane below it and the selected
// tab has a black bar running under it that looks odd"). IT IS THIS ROW'S OWN
// BOTTOM PAD, holding the tabs off the TRIM BAR below. It is the row's ONE
// term outside its content, so
//
//     tab_row_h_px() = tab_row_content_h_px() + tab_row_margin_bottom_h_px()
//
// — 36 at 100%, 82 at the tablet's 225% (68 content + 14, since 6 x 2.25 =
// 13.5 rounds up to the even 14). It is INSIDE the lane and OUTSIDE the
// content, which puts it under the base line and above the next lane's first
// pixel, and PAINT_TAB_ROW FILLS IT WITH THE SAME GROUND IT FILLS
// GAP 1 AND THE LANE WITH — one rectangle of kRedesignContentGround from the
// gap's top to the LANE's foot — so the margin is the row's ground extended
// six rows below the base line, not a band of its own. It is pointer-inert
// for the same reason gap 1 is and by the same route: no rect is published
// in it, so the tab row's press claim answers it exactly as it answers the
// stretch past the last tab, and the lane's entry in the wheel-inert band
// list already contains it.
// (The flexible GAP 1 opens ABOVE this lane since 2026-09-09 and is this
// lane's tall ground, painted by paint_tab_row — main.cpp's vertical rule.)
inline constexpr int kTabRowHeightPx         = 30;
inline constexpr int kTabRowMarginBottomPx   = 6;
inline int tab_row_content_h_px() {
    return scaled_px(kTabRowHeightPx, 5);
}
inline int tab_row_margin_bottom_h_px() {
    return scaled_px(kTabRowMarginBottomPx, 1);
}
inline int tab_row_h_px() {
    return tab_row_content_h_px() + tab_row_margin_bottom_h_px();
}

// Authored pixel geometry of the ICON ROW — the top strip's lane 1 since
// 2026-09-09, directly under the MENU ROW with nothing between (it was lane 2,
// under the tabs, from the 2026-08-12 relayout until then; the tab row sits
// under THIS row's border-bottom now, with the flexible gap 1 between —
// kdenlive-redesign.md's closing section) (row 4 of the redesign: TWENTY-THREE view/mode/action buttons since
// the evening of 2026-09-23, when Follow came back after its hours' deletion
// that day, twenty-three from 2026-09-22, when Zoom
// In and Zoom Out came back to the zoom group's head hours after the Show trim
// region button left it, the same day the WAVEFORM MAGNIFICATION lamp
// joined it — the
// kIconRowButtons table is the count's one authority, and ALL of them paint on
// every frame, the mode-collapsing rule of 2026-08-12..13 being deleted;
// icons::kIconCount is a
// different number, the GLYPH set, which the row does not exhaust). SINCE THAT
// DAY THIS BLOCK IS ALSO THE BOTTOM ROW'S: that lane delegates its content
// height and its border to the two accessors below. Measured
// at 100% gui_scale off row_4_button_{rest,hover,click,selected,selectedhover}
// .png (32x32), row_4_separator.png (1x34) and row_4_bottom_border.png.
//
// Same CSS box model as row 3: 46 is CONTENT, the 1px border-bottom sits
// OUTSIDE it, and the LANE is their sum (47 at 100%). THE BORDER IS INSET BY
// ITS OWN THICKNESS AT EACH END since 2026-09-09 (architect, kdenlive's own
// detail, read off his mockup: the line runs from the second column to the
// second-last, the first and last columns in the row's ground): the painter
// starts the line icon_row_border_h_px() in from each edge, so the inset
// scales with the line — one column at 100%, two at the tablet's 225%. The
// bottom row's border-top takes the same inset (its block below).
//
// 46, AND THE ARITHMETIC CLOSES EXACTLY (architect 2026-07-31, settling the
// discrepancy this constant first recorded): the row was briefed as 48 tall
// with 6px separator margins while the supplied separator crop is 34 tall, and
// 6 + 34 + 6 = 46. He confirmed 46 was meant, so the stated margins are now
// EXACT rather than absorbed — the 34px separator sits at +6 and the 32px
// buttons at +7, both still placed by the standing vertical-centering rule, and
// the centering now REPRODUCES the stated margins instead of papering over a
// two-pixel gap. (Those two offsets are what the painter's own centering
// computes from the 46 band: (46-34)/2 == 6 and (46-32)/2 == 7.)
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

// THE ICON BUTTON'S OWN BOX, measured at 100% off the same five 32x32 state
// crops as the lane above (row_4_button_{rest,hover,click,selected,
// selectedhover}.png). These three lived as file-local constants in
// paint_handler.cpp beside the row's walk until 2026-08-28, when THE FOLDER
// OVERLAY'S ROWS BECAME BUTTONS (architect R24/R31: "we've gone for the button
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

// THE CORNER RADIUS IS 5 — MEASURED, and it lands in rows 1-3's family after
// all. Fitting rendered corners against BOTH the hover crop (stroke only) and
// the selected crop (fill under stroke) over radii 3.0..5.0 minimises squared
// per-channel error at a PATH radius of 4.5 in each — hover 2382 against
// r=4.0's 25347 and r=5.0's 8099, selected 227 against 2685 and 684 — and 4.5
// is what the authored 5 becomes once the painter's half-stroke inset is
// applied. (An earlier read of "4" came from fitting the PATH radius directly
// and forgetting that inset; the authored constant is the thing to state.)
// IT IS A DOUBLE-DOMAIN LENGTH like every other radius in the tree: the
// painters scale it by gui_scale_factor() and round to a double on purpose,
// so it does not come through scaled_px.
inline constexpr double kIconCornerRadiusPx = 5.0;

// -- THE PLAY-SCRUB: A BREEZE SLIDER (architect 2026-08-28, R25/R29) --------
//
// The render player's modal row carries the transport's scrub bar, and the
// architect ruled its design to be the one his own desktop paints: a plain
// Qt `QSlider` under Breeze Dark, the widget Audacious's time slider IS (it
// derives from QSlider and overrides one style HINT, never a paint). So both
// the METRICS and the COLOURS below are Breeze's own, and the block holds
// them together because one owner cannot be split across two files: the
// painter (paint_modal_dialog) and the MAPPING (render_player_scrub_x_of,
// app_state.h) both read the handle box, and the press router reads it as the
// handle's grab band.
//
// THE METRICS ARE breeze/kstyle/breezemetrics.h's, verified against the
// architect's screenshot at 100% (the groove ink measures 6 px, the handle 18
// inside a 20 px box, and the label's margin lands on its own metric — three
// independent confirmations that the shot carries no HiDPI multiplier):
//   Slider_GrooveThickness   6  -> kScrubGrooveThicknessPx
//   0.5 * that               3  -> kScrubGrooveRadiusPx (semicircular caps)
//   PenWidth::Frame          1  -> the groove's and the handle's outline
//   Slider_ControlThickness 20  -> kScrubHandleBoxPx, and the handle's TRAVEL
//                                  is the track less that box (Breeze moves
//                                  the 20 px control's LEFT edge across
//                                  `width - 20`, so its CENTRE runs between
//                                  the two half-box insets — which is what
//                                  the mapping's inset is).
//   the painted circle      18  -> kScrubHandleDiameterPx (the box inset 1 px
//                                  per side, breezehelper.cpp's own adjust).
// They are AUTHORED 100% lengths like every other number here and ride
// gui_scale through scaled_px / gui_scale_factor().
//
// THE COLOURS ARE MEASURED OFF THE ARCHITECT'S OWN TWO SHOTS of that widget
// (2026-08-28, 17:46 FOCUSED and 16:46 UNFOCUSED), which is the ruling: "the
// focused colours are the 17:46 shot's" — played groove #3787b1, unplayed
// #46494c, handle fill #292c30 — "and the 16:46 shot's dimmed blue is the
// UNFOCUSED face the scrub takes when the window loses focus". So THE SCRUB
// IS THE THIRD SURFACE THAT READS AppState::window_activated, after rows 1
// and 2. It was the only one that read it below the waveform until 2026-09-02,
// when the folder overlay's panel and the modal row's active-focus outline
// took kRedesignAccentInactive (the block at that constant); this one is still
// the only reader of the flag that swaps a GROOVE rather than an accent, and
// its inactive blue is its own measured value, not that constant.
//
// THE OUTLINES SHIP AS THE COMPOSITED EDGE ROWS THEY PAINT, not as the alphas
// Breeze strokes them with — the palette is FULLY OPAQUE by ruling. Breeze
// strokes each groove's rounded path with its own fill colour forced to
// alpha = frameContrast (0.2), and over this ground that resolves to the rows
// the shots carry: #585a5c on the unplayed groove (the derivation reproduces
// the shot's own row exactly), #344753 on the played one at the INACTIVE
// accent (measured), and #457c99 at the ACTIVE accent (derived by the same
// arithmetic, the window being unfocused in the shot that could have shown
// it). THE EDGE ROWS ARE BREEZE'S AND THE FILLS ARE THE ARCHITECT'S MEASURED
// PAIR, which is a hair inconsistent by construction — his focused shot reads
// a lighter unplayed groove than the derivation's ground gives — and it is
// ACCEPTED: the ruling named the fills, the edge is a 1 px line, and a retune
// of a fill wants its own edge row recomputed the same way rather than
// nudged.
//
// THE HANDLE'S FILL IS Breeze's `QPalette::Button` and its resting outline is
// mix(Button, ButtonText, 0.2) — both COINCIDE with constants this product
// already carries (kRedesignRowGround #292c30, kRedesignLine #535659) and are
// deliberately their own here, the standing numerically-equal-is-not-the-same-
// constant rule (kRedesignRowGroundUnfocused): those two are kdenlive crops of
// a header row and a separator, these are Breeze roles on a slider, and a
// retune of one must not follow the other. The handle's HOVER/FOCUS outline is
// `Helper::hoverColor` = [Colors:View] DecorationHover = #3daee9, which IS
// kRedesignAccent's own value and role — the accent says "the pointer is
// here" on this surface exactly as it does on every button — so that one is
// READ from the accent rather than re-declared.
// NOT TRANSCRIBED: the handle's shadow (a 12.5% black crescent — an alpha,
// which this palette does not have) and the disabled groove (a dialog surface
// has no disabled state).
inline constexpr double kScrubGrooveThicknessPx = 6.0;
inline constexpr double kScrubGrooveRadiusPx    = 3.0;
inline constexpr double kScrubHandleBoxPx       = 20.0;
inline constexpr double kScrubHandleDiameterPx  = 18.0;
inline constexpr double kScrubOutlineWidthPx    = 1.0;

inline constexpr GuiColor kScrubGroove                = hex(0x46494C);
inline constexpr GuiColor kScrubGrooveOutline         = hex(0x585A5C);
inline constexpr GuiColor kScrubPlayed                = hex(0x3787B1);
inline constexpr GuiColor kScrubPlayedOutline         = hex(0x457C99);
inline constexpr GuiColor kScrubPlayedInactive        = hex(0x1D3847);
inline constexpr GuiColor kScrubPlayedInactiveOutline = hex(0x344753);
inline constexpr GuiColor kScrubHandleFill            = hex(0x292C30);
inline constexpr GuiColor kScrubHandleOutline         = hex(0x535659);

// THE HANDLE'S BOX IN DEVICE PIXELS — the ONE owner of that length for its
// three readers: the painter draws the circle in it, the MAPPING insets the
// track by half of it at each end (the handle's centre is the frame's
// position), and the press router takes it as THE HANDLE'S GRAB BAND. Floored
// at 2 so the half-box inset is never zero and the band never degenerates.
inline int scrub_handle_box_px() {
    return scaled_px(kScrubHandleBoxPx, 2);
}

// ROW 5's THREE LANES, measured off row_5_full.png (the composite is the
// authority, PRE-DATING the 2026-09-16 flip to kdenlive's own orientation —
// the trim lane's row count grew by the flip's added bottom border, so the
// composite's own y-map is one lane out of date and the figures below carry
// the correction): trim y0..9, ruler y10..37, marker y38..57, and the
// waveform starts at 58 — so the marker lane's bottom edge IS the waveform
// top, with no gap. These replace the four legacy lanes (trim chip / marker
// text / flag / triangle) and, like every redesigned row, ride
// gui_scale_factor() rather than the monospace font's axis. (The trim lane
// ADDITIONALLY scales by its own factor below, so the crop's y-map holds for
// the ruler and marker lanes while the trim lane is taller than its measured
// 10 rows now.)
//
// THE TRIM BAR'S OWN SCALE FACTOR — a RULED RETUNABLE, back at 100 (architect
// 2026-08-12, the seventh glass ruling). The 150 experiment lived one commit,
// earlier the same day: the sixth ruling read the lane as "a little too small
// for a finger" (the architect offered himself 150 or 200 and picked 150), and
// his next look at glass and monitors reversed it — "the blue bar looks too
// big". The bar needs no big finger target any more: the common act is
// HIGHLIGHTING to set trim, which lives on the WAVEFORM now (the SWEEP writes
// the window in one stroke, and the trim region overlay carries the bar's own
// bound and bridge drags on a surface hundreds of pixels tall), so the 10 px
// bar's own gestures are the rare case. The
// machinery stays because the factor is the lane's one retune knob: it
// COMPOSES with gui_scale inside trim_lane_h_px (the crop-measured 10 is still
// the authored value; the factor multiplies it before the one scaled_px
// conversion), and it reaches every consumer through the LANE RECT alone —
// top_trim_row_area's height is this accessor, and the endcap rects
// (trim_endcap_rect takes the lane rect's y/h), the bridge y-gate, the framing
// double-click band and the painted bar (render_trim_flags' trim_bar
// parameter) all read that one rect — so paint and hit move together by
// construction and no second site scales anything. The lane's INTERIOR widths
// (the endcap's 2px, the bevel pair, the midpoint tile's 9) keep their own
// crop metrics regardless of the factor.
inline constexpr int kTrimBarScalePercent = 100;
// 10 SINCE THE 2026-09-16 FLIP (was 9): the crop's own 1px bottom border row
// is counted IN the lane, not appended after it, because every hit-geometry
// reader below (the endcap rects, the bridge's y-gate, the framing
// double-click band) reads this ONE lane rect whole rather than the face
// alone — so the border row is part of the lane for the pointer exactly as it
// is for paint, and no second, shorter rect had to be invented for it.
inline constexpr int kTrimLaneHeightPx   = 10;
// 32 SINCE 2026-09-23 (was 28, the crop's own): FOUR AUTHORED ROWS OF GROUND
// UNDER THE TIMESTAMPS, so the playhead head on the lane's bottom rows clears
// them (architect 2026-09-23, his eyeball of the head's first variant, which
// overlapped the digits by three rows). The labels are anchored to the lane's
// TOP (paint_ruler_row: line_baseline off lane.y + kRulerLabelPadTopPx), so the
// growth lands entirely beneath them and they do not move. THE ARITHMETIC AT
// 100%, measured through cairo on the product's sans at 16px (ascent 15, the
// digits' and colon's ink the 12-row cap band, no descenders): baseline =
// 4 + 15 = row 19, so the labels' lowest ink row is 18; the head's top row is
// 32 - 12 = 20; ROW 19 IS THE ONE PIXEL OF EMPTY GROUND between them, the
// ruling's gap (at 28 the head's top row was 16, three rows into the digits).
// At 225% the same measure gives ink to row 41 and a head from 72 - 27 = 45
// (three rows of ground); at 50%, ink to 9 and a head from 16 - 6 = 10 (none,
// touching). The major ticks' rise above the marker lane is unchanged.
inline constexpr int kRulerLaneHeightPx  = 32;
inline constexpr int kMarkerLaneHeightPx = 20;
inline int trim_lane_h_px() {
    return scaled_px(
        kTrimLaneHeightPx * (kTrimBarScalePercent / 100.0), 3);
}
inline int ruler_lane_h_px() {
    return scaled_px(kRulerLaneHeightPx, 5);
}
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
// (tab, trim, ruler, markers, then the WAVEFORM with its
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
// produces by construction. At 100% scale, with the top lanes summing 175
// (menu 30 + icon 47 + tab 36 + trim 10 + ruler 32 + marker 20)
// and the bottom row 47 (the icon row's height since 2026-08-14): the
// 1920x1080 monitor's leftover is 858, so the
// waveform CLAMPS at the default 500 and the two gaps take 115 (top) + 243
// (bottom); a
// 1024x600 SHORT WINDOW's leftover is 378, UNCLAMPED, and the centering is
// infeasible there so both gaps floor at 0 and the waveform keeps the whole
// 378. The full stacks and their derivation are main.cpp's vertical block,
// the one owner; these figures are its, re-derived 2026-09-23 when the ruler
// lane grew 28 -> 32 (kRulerLaneHeightPx).
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
// strip's ONLY lane since the relayout's commit B): the transport three on the left with the monospace
// clock behind their separator (left-anchored since 2026-08-18, centred in the
// lane before it), and the MARKER-VERB GROUP
// + separator + marker walk + separator + four cardinal
// arrows flush right (2026-08-15 for the walk group, 2026-08-18 for the
// verbs; kMarkerVerbGroup in paint_handler.cpp owns that group's membership,
// which has changed under this geometry several times and does not bear
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
// main icon row"), and it READS that row's accessors rather than restating its
// numbers — one source, so a retune of the icon row carries down here by
// construction. Both accessors below are pure delegations and this row authors
// no height constant at all any more.
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
// THE CSS BOX MODEL, ONE BORDER: the content is the icon row's 46 and the 1px
// border-top sits OUTSIDE it (a 47px lane at 100%), on the WAVEFORM side —
// row 8's own convention kept (the bottom strip's chrome grows toward the
// waveform, so the border facing it is the one drawn), and commit B's stack
// names that same line "the thin border" above the row. IT IS THE ICON ROW'S
// BORDER TOO, read from the same accessor and merely drawn on the opposite
// edge: one chrome line, two lanes, AND ONE INSET — since 2026-09-09 both
// lines start their own thickness in from each window edge, the first and
// last columns staying the row's ground (the icon row's block above; the
// architect's mockup shows it on both). (The folder overlay's band read this
// accessor for its own top line from 2026-08-29 and the tab row's from
// 2026-09-03; that line is retired with the 2026-09-09 relayout, the band
// starting under the icon row's own border.) THE ROW IS THE WINDOW'S
// LAST LANE AGAIN (architect 2026-08-29, the evening of the messaging
// redesign's bar half): a STATUS BAR stood below it for that one day and
// folded back into it, its state text becoming this row's own cell right of
// the clock. (Row 9's second border — the near-black window-foot seam
// kRedesignBottomLine — was retired at the 2026-08-12 unification when the lane
// left the window's edge, stayed retired through commit B's return to the
// foot on the rule that a second line there would be a ruling, stood for that
// one day under the bar, and is RETIRED FOR GOOD with it on the corrected
// provenance: the crop's near-black last row is the xfce4-panel's top edge
// under kdenlive's window, never a kdenlive border. The record is at the row-7
// crop block above; this row's ONE line is its border-top.)
// bottom_row_content_h_px() is the ground the buttons and text sit on;
// bottom_row_h_px() is the lane the strip stack allocates. Rides
// gui_scale_factor() like every redesigned row, through the icon row's own
// scaled accessors.
inline int bottom_row_border_h_px() {
    return icon_row_border_h_px();
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
// docs/engineering/architecture/messaging.md.)

// THE REDESIGN'S SHARED TEXT SIZE, in device pixels — and since row 7 the ONLY
// text size in the product. Every row's text is 12pt through the existing
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
//   * our sans face (fontconfig "sans" -> Liberation Sans) at 16px reports
//     cap 12 / x-height 9 — the crop's numbers, not near them;
//   * a full offscreen re-render of the crop's own string at 16px, pen x=13,
//     baseline 22 fits the crop better than every neighbouring size, baseline
//     and pen tried (15 / 15.5 / 16 / 16.5 / 17 x 21/22/23 x 12..14).
inline constexpr double kRedesignFontSizePt = 12.0;   // -> 16.0 px at 100%
inline double redesign_font_size_px() {
    return kRedesignFontSizePt * 96.0 / 72.0 * gui_scale_factor();
}

// THE CLOCK'S SIZE — THE PRODUCT'S ONE EXCEPTION to the shared size above
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

// THE MONOSPACE FACE'S LINE HEIGHT AT A SIZE — its ascent plus its descent,
// MEASURED off the face the face owner resolves and never an authored pixel
// count (the clock cell's own principle, above: nothing about a monospace
// cell is authored in pixels). It is what the AV SYNC STATS PANEL's text rows
// stand at (folder_overlay::text_row_pitch_px reads it at clock_font_size_px
// and rounds it to a pixel count; architect 2026-09-03, on the panel painting
// its lines at the icon button's box: "the copy button uses single line
// spacing (correct) but the display shows double spacing (incorrect)"). It
// is declared here, beside the size it is measured at, because its reader is
// cairo-free and hit-tests and damages with it; the measure needs a scaled
// font, so it lives in paint_handler.cpp beside the clock cell's memo and is
// keyed on the size the way that memo is. It measures the face on a private
// surface rather than a painter's context, so the answer does not depend on
// a frame having been painted first, and paint, hit and damage read one
// number.
double mono_line_height_px(double size_px);

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
// rightward, so the left cull needs a width and the right cull does not). No
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
// one. The cells' seam columns ARE in it: they stand to the right.
inline double marker_flag_max_width_px(bool iteration_on) {
    const size_t glyphs = 4 +                                  // `N.NN` base
                          5 * kMaxTempoDeviationTerms +        // `+0.01` each
                          1 + kMarkerFlagScaleGlyphs +         // `*N.NN`
                          kMarkerLabelTruncationMarker.size();
    const double pads = static_cast<double>(marker_flag_pad_left_px() +
                                            marker_flag_pad_right_px());
    const double flag = static_cast<double>(glyphs) * redesign_font_size_px() +
                        pads;
    if (!iteration_on) return flag;
    const double cell = static_cast<double>(kIterCellGlyphs) *
                            redesign_font_size_px() +
                        pads + static_cast<double>(marker_flag_border_px());
    return flag + 2.0 * cell;
}

// THE TRIM LANE's bevel band: since the 2026-09-16 flip to kdenlive's own
// orientation, the TOP two rows — a darker shade then a lighter one — of
// whatever surface owns the column (was the bottom two, darker last; the
// crop's own two shades are unchanged, only their row order moved). Rides
// gui_scale like every authored length; deliberately NOT kTrimBarScalePercent
// — the lane's interior metrics keep their crop values whatever the factor
// reads (at the resting 100 the two axes coincide; the rule at the factor's
// own comment).
inline int trim_bevel_h_px() {
    return scaled_px(2.0, 2);
}
// THE LANE'S BOTTOM BORDER, 1px at 100% (row_5_lane_1_trim_bottomborder.png is
// a 1px row spanning the lane's whole width) — the new crop the 2026-09-16
// flip added. Rides gui_scale like the bevel, NOT kTrimBarScalePercent, for
// the same reason: it is an interior metric of the lane's own crop, not the
// factor's business.
inline int trim_lane_border_h_px() {
    return scaled_px(1.0, 1);
}
// The endcap's own width, 2px at 100% (row_5_lane_1_trim_endcap.png is 2x9).
inline constexpr int kTrimEndcapWidthPx = 2;
inline int trim_endcap_w_px() {
    return scaled_px(kTrimEndcapWidthPx, 1);
}
// THE MIDPOINT MARK IS THE 9x9 CROP (now 9x10 with the flip's added border
// row, which the tile receives from the ONE shared border fill that runs the
// whole lane's width after every surface paints — the tile's own `surface`
// call still paints only its bevel+face rows, exactly as the endcaps' does),
// so its lengths are the crop's own: a TILE 9 columns wide at 100% (its height
// is the lane's, which is what 10 rows means here — the lane also rides
// kTrimBarScalePercent, resting at 100 since the seventh glass ruling, so tile
// and crop square coincide again), an INNER square 5x5, and the crop's 2px
// INSET placing that square at cols 2..6, flush under the bevel (rows 2..6
// since the flip; was rows 2..6 flush ON the bevel from the bottom — the
// numbers read the same because the square always sits two rows in from
// whichever edge carries the bevel). Plus the CLEARANCE the visibility rule
// demands on each side of the whole tile. All the widths ride gui_scale
// alone, like every interior length in this lane.
//
// THE INNER SQUARE HAS NO LENGTH OF ITS OWN ANY MORE (codex round 3,
// 2026-08-10). It WAS a third constant, kTrimMiddleInnerPx = 5, read through a
// trim_middle_inner_px() accessor and rounded independently of the tile and the
// inset — and three independent nearbyints do not partition a symmetric ring:
// the left rim was `inset` while the right was the leftover tile - inset -
// inner, and the two disagreed at 71 legal scales (2/1 at 75%, 1/2 at 62%). The
// painter DERIVES the width as tile - 2 * inset now, so both side rims are
// exactly `inset` at every scale by construction, and the constant and its
// accessor are deleted rather than left as a second truth someone could
// re-round from. 100% / 150% / 200% are byte-identical (9-2*2 == 5,
// 14-2*3 == 8, 18-2*4 == 10). The derivation and the height's own clamp are at
// the paint site (render.cpp).
inline constexpr int kTrimMiddleSizePx  = 9;   // the tile's width
inline constexpr int kTrimMiddleInsetPx = 2;   // the crop's offset to that square
inline constexpr int kTrimMiddleClearPx = 2;
inline int trim_middle_size_px() {
    return scaled_px(kTrimMiddleSizePx, 1);
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
//
// THE 19 IS PROVENANCE, NOT A CONSTANT (codex round 4, 2026-08-10). It WAS
// kPlayheadHeadWidthPx, reader-less and independently authored beside the table
// that already implies it (2 * kPlayheadHeadHalf[0] + 1). Scaled on its own it
// would have disagreed with the width the painter actually lays down — 28
// against 29 at 150%, the head's own rows being 2*half+1 off the ROUNDED half —
// so it is deleted rather than left as a second truth to re-round from, exactly
// like the tab lock slot, the trim inner square and the toolbar separator
// height. The painted width at any scale is the table's own arithmetic through
// playhead_head_half_px below; the crop's 19x12 stays recorded here, where a
// number in prose cannot be scaled by mistake.
inline constexpr int kPlayheadHeadHeightPx = 12;
inline constexpr int kPlayheadHeadHalf[kPlayheadHeadHeightPx] = {
    9, 8, 7, 6, 6, 5, 4, 4, 3, 2, 1, 1
};
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
// half of 0 is a 1px row — the head's tip would collapse onto the 1px stem and
// stop reading as a tip at all. Floored, the bottom row is 3 px wide (width is
// 2*half+1, always odd, so the centring stays structural and the shape stays
// aliased integer rects). A FLOOR, NOT A PIN: at 100% and above every scaled
// half is already >= 1, so nothing above the baseline moves and the tips keep
// scaling.
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
//
// THE DWELL IS THE PRODUCT'S HOLD BEAT, not a number of its own. A tooltip
// under a HELD button is a resting pointer's dwell either way — which is
// exactly what a finger's long press is on glass — so tying the dwell to
// kHoldBeatMs makes the tooltip's APPEARANCE the cue that the hold has crossed
// into its shifted meaning: see the hint, release for the modified act. The
// two mechanisms stay independent (a hover dwell and a press hold neither
// reset, suppress nor feed the other); it is the NUMBER that is one.
inline constexpr int     kTooltipDamageHeightPx = 60;
inline constexpr int64_t kTooltipDelayMs        = kHoldBeatMs;
inline int tooltip_damage_h_px() {
    return scaled_px(kTooltipDamageHeightPx, 5);
}
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
// ruler lane's bottom rows is WIDER than this reach at every scale. The head's
// widest row is 2 * playhead_head_half_px(0, s) + 1 off kPlayheadHeadHalf[0]
// = 9 — 19px at 100% (the crop's own width), 9 at 50%, 29 at 150%, 37 at 200%
// and 73 at the 400% ceiling — against this +/- 7-at-100% reach, which rides a
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
// marker's frame column (marker_flag_border_px) — because this stash has always
// been the painted extent and a click on the border is a click on the flag.
//
// IT SPANS THE TWO ITERATION BOUND CELLS TOO where they
// paint: each is the flag continued, so all of it is ordinary flag surface for
// press, drag and select and the rect covers the whole run. The two
// boundaries are the PAINTER'S own numbers, published rather than re-derived,
// because a second shaping pass could disagree with the pixels: the window x
// where the flag box ends and the LOWER cell's seam begins
// (`iter_lower_boundary_x`) and where the lower cell ends and the UPPER cell's
// seam begins (`iter_upper_boundary_x`). They are non-decreasing, and
// each collapses onto the next when its box did not paint — a cell-less flag
// publishes both cell boundaries AT the rect's own right edge — so
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

// The two ground fills, one per surface class (see the palette's ground split):
// render_background erases CHROME in kBackground, render_canvas erases the
// WAVEFORM AREA in kWaveformCanvas — the row-6 crop's own #12312b, which since
// row 6 replaces the grey ground the area used to take. on_redraw calls the
// first over the whole exposed rect, then
// the second over the exposed part of the waveform area, so the canvas wins
// exactly the pixels the plate, the ground recolors, the playheads and the
// marker stems paint on — cold frames (no plate yet) included.
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
// region highlight's two halves, ground and ink — so the border rows survive
// the frame no matter what
// covers the area. THE PHASE-RESET OVERLAY RING LEFT THIS LIST 2026-08-01: its
// horizontals now ride the borders' OUTERMOST rows deliberately (the ruling is
// at paint_phase_reset_overlay_ring), so it reads the full area. 1px VERTICALS
// deliberately
// do not: the playheads, the marker stems, and the strip-drag anchor stem
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
// written by its raw bar, after its ghost bar when the lamp is lit (the raw
// overwriting the ghost where they overlap is the ghost rule's order), so
// replacing is correct and idempotent — the max-compositing that the segments needed (they wrote into an
// already-rendered neighbour) went with them.
//
// The words are PREMULTIPLIED ARGB32 (argb32_opaque_word), built once per call
// — the ink's, and the ghost's when the lamp is lit; at full coverage each is
// its colour itself, so there is one word per ink rather than a table. The surface is flushed before the first CPU write and marked dirty
// after the last, so later cairo use sees the pixels.
//
// The plate paints in `color`, plus `ghost_color` behind it when the lamp is
// lit — it is trim-agnostic, and the out-of-trim dim that once masked a
// second color through this alpha is retired, the trim bar spanning the
// window being the whole inside-the-window signal now. Its alpha is BINARY:
// opaque bars and transparent gaps, with no fractional edges left. The gaps
// are what let a recolored GROUND (kWaveformRegionCanvas, painted before the
// blit) show through, and the SET pixels are what the one remaining
// after-the-fact recolor reads: paint_region_ink rewrites each opaque plate
// pixel inside the region's column span in the lifted colour for ITS WORD
// (the ink's -> kWaveformRegionInk, the ghost's -> kWaveformRegionGhostInk),
// leaving the plate itself untouched.
// THE VISUAL MAGNIFICATION is `gain_or_null`, and it is A GHOST BEHIND THE
// RAW PICTURE (architect 2026-09-24): the lit lamp no longer replaces the raw
// picture, it paints the magnified one behind it. Every column takes ONE peak
// read and draws TWO bars from it: first the GHOST — the raw min/max times
// the column's scale (the gain below times the expander's multiplier),
// CLAMPED to [-1, 1] before they become rows — in `ghost_color`, then the RAW
// bar (scale 1) in `color` written over it. The writer replace-writes opaque
// words, so where the two overlap the raw bar wins. NULL (the dark lamp)
// draws the raw bar alone, byte for byte the plate it always drew, and
// `ghost_color` is then unread. Nothing else in this painter moves (the
// column grid, the >=1px floor — both bars keep it — the carried-endpoint
// chain and the aliased-only writer are untouched). A loud passage's ghost
// clips flat at the lane's edges while the raw bar still shows its true
// height inside it: the ghost exists to make a quiet passage's onsets
// readable, and the raw picture is always present.
//
// THE ALIGNMENT IS EXACT BY CONSTRUCTION: the two bars share the lattice, the
// column's [s0, s1), the pyramid level and the one read. THE GHOST IS A
// DILATION ABOUT THE CENTRE ROW, NOT AN OUTLINE: a column straddling zero has
// its ghost containing the raw bar; a column wholly on one side of zero (low
// material at working zoom) has its ghost pushed outward, with a gap between
// it and the raw bar. Intended — that is what a true magnification looks
// like. THE EXPANDER IS UNCAPPED, so where a column's scale falls under 1 (a
// peak more than 8 dB plus twice the leveler's gain in dB under the edge) the
// ghost sits inside the raw bar and the raw paints over it: in the overlay
// the expander's dips never go below the raw picture. Accepted; that is the
// hairline region. THE COST is one extra row fill per column — the read, the
// map walk and the gain lookup are shared; no second pyramid, no second
// plate, no new cache field and no fingerprint change
// (waveform_gain_fingerprint already flips with the lamp).
//
// THE GAIN IS A FUNCTION OF SOURCE TIME: the continuous curve derived from the
// source at load (WaveformGainCurve, waveform_gain.h, which owns the rule).
// A COLUMN TAKES THE GAIN AT ITS CENTRE SOURCE FRAME, (s0 + s1) / 2 — one
// evaluation per plate column (waveform_gain_at, linear between the curve's
// hops). s0 and s1 are the same integers the peak read takes, pure functions
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
// estimates on the peak measure, Astra review 2026-09-23,
// tmp/gain_check/review_coarse.py). The
// exact alternative is a second pyramid reduced over the gained samples —
// immutable with the source like the curve, so the lamp would select between
// the two pyramids rather than rebuild one — at the memory of a second
// pyramid. RULED (architect 2026-09-23): the centre rule stands and no gained
// second pyramid is built — placement is never done at a coarse zoom, so the
// approximation there costs nothing the plate is used for. At working zoom (55 frames per column against a 4400-frame
// hop) the two agree.
//
// THE EXPANDER'S MULTIPLIER rides the same pointer (the curve's
// `expander_multiplier`, one per working-zoom column, waveform_gain.h owns
// the stage): a plate column spanning source frames [s0, s1) takes the
// LARGEST multiplier — the smallest reduction — over the working columns the
// span covers (waveform_expander_multiplier_over, a plain loop, no pow), and
// both tips take raw x gain x multiplier before the one clamp. At working
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
                     GuiColor color,
                     GuiColor ghost_color,
                     const WaveformGainCurve* gain_or_null,
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
// (vp_end - vp_start)/wave_w, NOT current_samples_per_pixel. The two are
// identical at integer zoom rungs on multiple-of-16 widths and differ by
// <~0.02 px at a fractional zoom rest; adopting it at the hit sites too (they
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
// The +endcap_w inset is the ROOM a PAINTED endcap occupies; an offscreen bound
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

// The ONE trim ENDCAP screen-rect owner: the begin/end edge-anchoring rule
// lives here, run by the painter (render_trim_flags), which publishes each cap
// it fills for the hit test (hit_test_trim_endcap reads TrimBarHit, below), so
// paint and hit are one rect — row 5's endcaps replaced the square chips in
// BOTH at once.
//
// A trim bound is an EDGE, not a point: the begin cap's LEFT edge sits ON the
// bound column (rect left = strip_x+col), the end cap's RIGHT edge sits on it
// (rightmost pixel = strip_x+col). The cap is trim_endcap_w_px() wide — 2px at
// 100%, where the chip was a flag-width square — and its y-band is the trim
// lane `row`. Deliberate asymmetry vs centered marker flags: a bound at frame 0
// / EOF shows its cap fully onscreen.
//
// THE HIT TEST INFLATES THIS by kTrimEndcapGrabPx per side (10 since
// 2026-08-19 — what each retune of it costs the bridge is
// recorded at the constant). A 2px target is under any reasonable pointing
// tolerance, so the drawn cap and the grabbable cap are deliberately NOT the
// same rect — the one place in this lane where they differ, stated here
// because everywhere else in the redesign they are identical by construction.
GuiRect trim_endcap_rect(bool is_begin, int strip_x, int col, GuiRect row);

// Grab tolerance added to EACH SIDE of the drawn endcap for hit-testing. The
// caps are 2px, so this makes the target 2 + 2*10 = 22px. ONE CONSUMER reads
// it (re-grepped 2026-09-22): the TRIM BAR's endcaps (hit_test_trim_endcap).
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
//     caps, which is a window wider than 3 + 2*grab columns on screen — so the
//     narrowest window that still has a bridge is 24 columns at 10, against 14
//     at 5, 34 at 15 and 12 at 4. What the bridge loses is zoom-recoverable
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
// render_trim_flags last PAINTED as the bar's two grab handles, published by
// that painter into AppState::trim_bar_hit so the trim hits read the pixels
// rather than re-running the painter's owner chain on the live trim — the
// flag lane's stash doctrine (AppState::flag_hit_rects) carried to the trim
// lane. Everything is in SCREEN pixels. `lane` is the band the bar was painted
// in, the y-gate of both hits. Each cap is its DRAWN rect (trim_endcap_rect,
// uninflated — the hit applies trim_endcap_grab_px itself, the one place the
// drawn and the grabbable rect differ) plus its bound column, which is the
// leftmost-wins sort key, and `painted` is false for a bound the viewport
// culled, which paints no cap and so answers no hit. The bridge is the
// half-open interval [bridge_lo, bridge_hi) between the caps' inner edges,
// already clipped to the lane's painted width (trim_bridge_gap, the owner the
// midpoint mark fits against); empty when lo >= hi. `published` false is
// COLD — nothing painted, nothing grabbable.
struct TrimBarHitCap {
    bool    painted = false;
    int     col_x   = 0;        // the bound's screen column
    GuiRect rect{0, 0, 0, 0};   // the drawn cap
};
struct TrimBarHit {
    bool          published = false;
    GuiRect       lane{0, 0, 0, 0};
    TrimBarHitCap begin;
    TrimBarHitCap end;
    int           bridge_lo = 0;   // screen x, inclusive
    int           bridge_hi = 0;   // screen x, exclusive
};

// Draws the WHOLE TRIM BAR LANE (row 5's endcap bar, which replaced the square
// b/e chips and their strip-crossing stems): the lane ground, the window's bar
// over it, the two endcaps over that, and the midpoint mark last. Every run
// paints through ONE lambda — a face band above a two-row bevel pair, all
// pixel-bound integer fills, no stroke and no antialiasing anywhere in this
// lane — so a surface is named by its four constants and nothing else.
// The lane band is the `trim_bar` PARAMETER — the caller passes
// top_trim_row_area(app) (top-strip lane 3), and the band painted in is
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
// Both caps come from the ONE rect owner (trim_endcap_rect) and are published
// as filled, so the painted cap and the grabbable cap describe the same edge;
// the hit side adds only its stated grab tolerance. Column placement is
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
// _inset_px / _clear_px — the inner square's own width is DERIVED from the
// first two at the paint site, not authored — and its four colours are the
// lane's own
// endcap + bar surfaces; the pixel-by-pixel derivation from the crop is at the
// paint site (render.cpp).
// PUBLISHES WHAT IT PAINTS into `out_hit` when non-null (TrimBarHit above):
// the lane, both caps and the bridge interval, from the very columns this
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
//                   seam wherever that seam is and the WINDOW cuts it off,
//                   like every other box in this lane — so `box` may reach
//                   past a window edge, which costs its consumers nothing (a
//                   press is inside the window by construction). Every box
//                   spans its 1px LEFT BORDER too (the flag's own for the
//                   payload editor, the seam divider for the other two), so
//                   its x is one column left of the fill and its w one wider.
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
//                   extent, every seam divider included, keyed to the edited
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
// The touch pan zone yields inside this same rect (touch_point_in_pan_zone,
// 2026-09-05) and the platform's editor-field query reads it too
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
// AT A WINDOW EDGE THE BOX IS CUT OFF AND NOTHING MOVES (architect 2026-09-06,
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
// EACH HALF NAMES ITS OWN ROW (2026-09-16, Sol round 16's P1): `then_ordinal`
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
// whole at its left, and — SINCE 2026-08-20, an EXPERIMENT that may be reverted
// whole — a SECOND column of that same border ink ON THE SEAM between them,
// against the depth illusion two adjacent saturated hues produce (the ruling
// and the rationale are at the paint site and in marker-ui.md). The pair is
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
// direction of the toggle is readable off the flag itself. The two BORDER
// COLUMNS follow from what each one belongs to — the box's own left border is the
// LEFTMOST PAINTED HALF's face element (the live lane's anatomy: border outside
// fill) and dims with that half, while the SEAM divider belongs to neither half
// alone and dims only when BOTH are disabled.
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
// reset" is ONE CONCEPT TOKEN, never shortened to "reset" (conventions.md),
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

