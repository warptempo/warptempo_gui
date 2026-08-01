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
// with `toward`. keep_own == 1 returns `own` bit-identically and keep_own == 0
// returns `toward`, so a call site that means "unchanged" costs nothing and
// says so. Used by the redesign's DISABLED FACE — the icon paths (icons.cpp)
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
// consumed by render_trim_stems / render_trim_flags to place the boundary
// stems and chips. Values
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
// main.cpp. The scheme is Plasma 6 Breeze Dark roles carrying Ableton's value
// polarity (architect 2026-07-26, the breeze option-a trial): the waveform area
// is a MID ground with DARK ink, while the surrounding chrome sits DARKER THAN
// THAT CANVAS at the desktop's panel color.
//
// THE DEFAULTS BELOW ARE THE ARCHITECT'S TUNED SCHEME (2026-07-27), folded in
// from the config file that carried it, so a fresh machine with no colors.conf
// looks exactly like the tuned desktop. Its shape: the CHROME and the LINES take
// the desktop's own values, and every marker-family shape is a DARK DESATURATED
// FILL under a BRIGHT RING — the shipped saturated roles survive as the rings
// and as nothing else. NOTHING BELOW IS TUNED BY EYE: every default is one of
// three kinds — a shipped Breeze role (live, or disabled as KColorScheme
// computes it), a documented blend of such a role with another key, or a PIXEL
// SAMPLED from a named screenshot of the desktop. Each entry records which kind
// it is, so the architect can re-derive it after retuning a base; where a
// sampled value has no closed form, that is said instead of inventing one.
//
// MUTABLE, WRITTEN ONCE. Each entry is a plain global holding its compiled
// default. load_color_config() (color_config.h) overwrites the whole set once
// at startup in main(), before the first paint and before anything derives a
// value from the palette; nothing writes them afterwards. So the waveform
// worker thread's kWaveform read needs no synchronization, no surface needs
// invalidating, and a retune is a restart. The names stay k-prefixed because
// every use site treats them as constants.
//
// THE TUNABLE DOMAIN IS SHRINKING (architect 2026-07-31, the kdenlive redesign):
// "every GUI color is runtime-tunable" is TRUE OF THE 23 KEYS BELOW AND OF
// NOTHING ELSE. The kdenlive screenshots override all prior color work, and the
// colors.conf domain is contracting to the WAVEFORM AREA AND THE MARKERS (the
// canvas ground, the waveform ink, the marker classes); every OTHER color the
// redesign touches is HARD-CODED from a sampled crop — not a key, not tunable,
// not in the config grammar. The redesigned rows carry their constants at the
// end of this block (see the row-1 menu colors below); an existing row keeps its
// tunables until its own redesign row retires them, so no key is deleted here
// and the grammar is unchanged.
//
// EVERY ENTRY IS OPAQUE — the palette carries no compositing alpha at all. A
// highlight recolors the GROUND UNDER the ink (kRegionCanvas)
// rather than washing over it, so ink over a highlighted span is the same ink
// and only the ground reads the highlight; a disabled item takes its own
// opaque pair (kMarkerDisabled) rather than fading.
//
// THE GROUND SPLIT: two grounds, by surface. kBackground is the CHROME —
// the top-strip lanes, the bottom strip, editor boxes, every erased pixel
// OUTSIDE the waveform area (Breeze Window). kCanvas is the WAVEFORM AREA's
// ground, deliberately lighter than the chrome so the work surface lifts out
// of the panel around it. Wherever waveform ink paints, the ground is kCanvas.
//
// The declaration order below is the config file's canonical key order; the
// key->global table in color_config.cpp is that order's one authoritative
// statement, and this block mirrors it for reading only.
inline GuiColor kBackground       = hex(0x202326);  // Breeze Window
// The work surface: the Breeze/qt6ct "Light" bevel role — the desktop's own
// lifted-panel value, still clearly lighter than the chrome above, so the
// inversion the ground split describes reads at a glance.
inline GuiColor kCanvas           = hex(0x393E43);
// The ink is DARKER than its ground — the waveform is a dark cut into the
// canvas, not a bright trace on black (Breeze View, the deepest ground).
inline GuiColor kWaveform         = hex(0x141618);

inline GuiColor kText             = hex(0xFCFCFC);  // Breeze paper white
// Disabled text: Breeze Dark's DISABLED Text role, the View set's faded
// foreground — the value the desktop gives disabled text, rather than a fade
// computed from kText. THE SOURCE OF TRUTH FOR THE WHOLE DISABLED FAMILY (this
// key, kLine, kStripAnchorStem, the kMarkerDisabled pair, the trim chip ring and
// stem) is the shipped /usr/share/color-schemes/BreezeDark.colors run through
// KColorScheme, which applies Breeze's own [ColorEffects:Disabled] block: an
// intensity Darken of 0.10 on the backgrounds, then a contrast Fade of 0.65 on
// each foreground. THE RECIPE IS "RUN KColorScheme", not those two numbers: the
// darken half IS reproducible standalone (KColorUtils::darken(#141618, 0.10) =
// #131517, byte-identical to the disabled View ground), but the fade half is
// NOT a per-channel mix of the foreground toward that darkened ground — applied
// literally it lands 3-5 units off every value here (mix(#fcfcfc, #131517,
// 0.65) = #656667 against the true #606263), and no consistent mix target
// reproduces the set. "Contrast Fade 0.65" is the EFFECT'S NAME out of the
// .colors block, quoted so the source is identifiable; only KColorScheme
// evaluates it. Re-derive from the shipped scheme, never from a qt6ct conf —
// the one this family was first read off ran 0-4 units dark per channel and no
// single darken amount reproduces it (best fit ~0.17-0.18), so that row was
// never a clean KColorScheme pass at any setting. Paints the
// marker-text-lane run of a disabled marker, the glyph half of the opaque
// disabled cue whose shape half is kMarkerDisabled (whose default comes from the
// same disabled set, so the two halves are one family by provenance).
inline GuiColor kTextDisabled     = hex(0x606263);

// THE ONE STRUCTURAL LINE COLOR: the DISABLED WindowText role, from the same
// KColorScheme pass over the shipped scheme as kTextDisabled — the architect's
// separator value, the desktop's own arithmetic rather than a blend invented
// against the chrome. Every inert structural rule on an UN-REDESIGNED surface
// paints in it — now the waveform area's 1px top and bottom border
// (render_canvas), the strip-row ring having retired with the zoom lane
// (2026-07-31); the redesigned rows take their own sampled kRedesignLine
// instead. It is not an accent and never marks state. The trim
// chips' ring and stems take this same value from their own keys, so every calm
// 1px rule in the product is one color.
inline GuiColor kLine             = hex(0x686A6C);

// The strip-drag anchor stem: a transient pivot affordance shown only mid-drag,
// so it reads as a muted structural guide rather than competing with the crisp
// marker/text ink — which is why its default is the kLine default. It stays its
// own key so it can be pulled off the line color independently. Dimmed by hue,
// not alpha; it paints straight over any waveform samples it crosses (no notch
// — see render_strip_anchor_stem).
inline GuiColor kStripAnchorStem  = hex(0x686A6C);

// The resting cursor: its 1px line and its tip-down triangle — the ONE playhead
// form, always painted (architect 2026-07-30; the region's split half-triangles
// rode this key until the span form retired with them). The compiled default is the breeze-icons grey —
// brighter than kLine so the cursor reads as live rather than structural, calmer
// than kText so it never competes with the glyphs. kSelectedStem and
// kOverlayOutline default to the same value: the live 1px marks are one
// family, each on its own key so any of them can be pulled out of it.
//
// THAT VALUE IS THE PALETTE'S ONE PROVENANCE EXCEPTION, recorded here once for
// all three keys: every other default traces to a BreezeDark.colors scheme
// ROLE (directly, or through KColorScheme/KColorUtils), but 127,140,141 appears
// NOWHERE in BreezeDark.colors. It is the breeze-icons ART grey — 101
// occurrences across the breeze-dark icon SVGs — and its nearest scheme home is
// BreezeClassic.colors' ForegroundInactive, a DIFFERENT scheme's role. So it is
// the desktop's own value either way, but it is not a Breeze Dark role and
// cannot be re-derived from that file; take it from the icon art or from
// BreezeClassic, not by hunting BreezeDark for a role that does not exist.
inline GuiColor kPlayheadCursor   = hex(0x7F8C8D);
// The moving scanner reads WHITE against the mid canvas (the Ableton play-head
// cue; also Breeze's text/icon foreground, so it is the scheme's brightest ink).
inline GuiColor kPlayheadScanner  = hex(0xFCFCFC);

// THE THREE MARKER CLASSES, each a FILL + OUTLINE pair: a DARK DESATURATED FILL
// under a brighter 1px RING (see EditorTextBox::outline / kChipOutlinePx) — the
// live classes carry a genuinely bright ring, the disabled one a ring that only
// just lifts off its fill, which is exactly how it reads as switched off. Both
// halves of every pair are tuning knobs. The classes resolve in priority order
// at the flag renderers — disabled, then red, then default.
//
// SELECTION IS NOT A CLASS (architect ruling 2026-07-27). There is no fourth
// class and no selected pair: a selected marker's FILL/OUTLINE PAIR is EXACTLY
// the one it would paint unselected, whichever of the three it belongs to. That
// was the intent while a selected pair still existed too, but it was carried by
// two keys merely HOLDING the default pair's values rather than by the
// structure — and the outline arm preferred the selected ring over the disabled
// one, so a disabled marker that was ALSO selected took the live bright ring and
// read as switched on. With the class deleted the property cannot regress under
// retuning, and a selected marker whose render normalizes to 1.00 keeps its red
// cue instead of having it masked.
//
// Selection's ONE paint is the INK TRIANGLE (architect 2026-07-27): a selected
// flag fills the triangle half of its shape with kWaveform while the rectangle
// keeps the class fill and the outline rings the whole shape untouched. It
// introduces NO KEY of its own — it re-uses the ink the waveform already paints
// in — and it is applied AFTER the ladder resolves, layered on one part of the
// shape, so it can never displace a class pair the way the deleted selected
// pair did. Selection also reads from the focus STEM (kSelectedStem below),
// from the z-order lift (selected shapes paint above unselected), and from the
// playhead landing on the marker.
//
// The SELECTED-MARKER STEM (paint_selected_stem) — the singleton selection's
// focus column, full waveform height. Its own key by ruling (architect
// 2026-07-27), separate from every flag ring: the same value that is right for a
// 1px border around a small shape reads far louder run the whole height of the
// waveform, so a bright ring wants a calmer stem and the two must tune
// independently. It is the singleton's cue in the WAVEFORM, below the strip
// where the ink triangle reads, and it takes the calm breeze-icons grey
// (the kPlayheadCursor value — its provenance and its standing as the
// palette's one non-Breeze-Dark-role default are recorded at kPlayheadCursor
// above) rather than a ring value like kMarkerOutline — a full-height line at
// ring brightness would shout.
inline GuiColor kSelectedStem     = hex(0x7F8C8D);

// DEFAULT — the pair every marker paints unless it is disabled or red. Both
// halves are pixels SAMPLED FROM ONE WIDGET: a Breeze-hovered file-list row in
// the last of the pcmanfm-qt shots under tmp/screenshots/ (2026-07-26; tmp/ is
// untracked, so the closed form below is what actually reproduces off a fresh
// clone). The row's low-alpha highlight wash is the fill, the 1px
// highlight-colored border along its top edge is the ring — the fill and ring of
// the same thing, which is why they work as a fill/ring pair here. It is the
// HOVER treatment, not a selection: the selected row in that shot is a separate
// full-saturation #3daee9 one, and the same wash appears elsewhere in the series
// with no selected row present anywhere.
//
// THE FILL HAS AN EXACT CLOSED FORM: Breeze blue #3daee9 at alphaF 0.3 —
// Breeze's own 30% hover wash (Helper::alphaColor(highlight, 0.3)), composited
// by Qt's FLOAT-alpha path — over the View-ALTERNATE row ground #1d1f22
// reproduces #264a5e byte-exact on all three channels, and that same wash over
// the plain View row ground #141618 gives #204357 byte-exact too: one formula,
// the two alternating row grounds. The factor must stay the float 0.3; the
// 8-bit integer restatement 77/255 is lossy and reproduces NEITHER ground's
// sample (it lands green +1 on both, #264b5e and #204457). THE RING HAS NO
// SINGLE FORM: the border carries a slight vertical gradient, #3895c7 at its
// top edge and #3794c5 at its bottom. A scalar does reproduce EITHER edge on
// its own (#3895c7 is #3daee9 at alphaF 0.828 over #1d1f22, #3794c5 the same
// blue at 0.818); what no single scalar does is cover BOTH edges at once, so
// the key takes the top-edge sample. Fill dark enough to sit quietly on
// the canvas, ring bright enough to draw the shape — the dark-fill/bright-ring
// system every marker-family pair follows.
inline GuiColor kMarker           = hex(0x264A5E);
inline GuiColor kMarkerOutline    = hex(0x3895C7);

// DISABLED, shared by every marker family (warp flags, phase reset flags).
// Opaque, not an alpha fade, and not computed from the default pair: both halves
// come from the same KColorScheme disabled set as kTextDisabled — the fill is
// the disabled LINK role (Breeze's link blue #1d99f3 faded 65% toward the
// darkened view ground, which is exactly why a disabled marker stays in the
// default fill's family while sitting darker than it), the ring its
// PlaceholderText grey. It is NOT the disabled Highlight, which is the grey
// window ground #1f2124 and would be useless as a marker fill. The disabled
// lane text (kTextDisabled) is that set's Text, so shape and glyph go disabled
// together by provenance. It WINS over red and over the default class, and
// there is nothing left for it to compose with: a disabled marker paints BOTH
// halves of this pair whatever its red-flag status. Selection does not alter
// the pair either (architect 2026-07-27) — its one paint is the triangle
// interior described in SELECTION IS NOT A CLASS above, never the ring — so a
// selected disabled marker keeps both muted halves and reads instead through
// that triangle ink, the paint order, and the stem.
inline GuiColor kMarkerDisabled        = hex(0x164160);
inline GuiColor kMarkerDisabledOutline = hex(0x42464A);

// RED — the normalization cue: a marker whose render falls back to the 1.00
// tempo. The RING is the shipped Breeze ForegroundNegative #da4453; the FILL is
// mix(kWaveform, that same negative, 0.35), a deliberate blend rather than a
// sample: it puts the red flag in the same dark-fill/bright-ring system as the
// sampled default pair — comparably dark, comparably desaturated — instead of
// leaving it the one saturated block. It also paints the editor box's
// invalid-commit flash, so a parse failure and a red flag read as one family.
inline GuiColor kAccent           = hex(0x59262D);
inline GuiColor kAccentOutline    = hex(0xDA4453);

// THE GROUND RECOLOR (the Ableton model): the region-select span's CANVAS
// becomes kRegionCanvas, painted after render_canvas and BEFORE the plate blit,
// so the ARGB32 plate composites over the recolored ground and its antialiased
// fringes blend correctly against it. The ink itself is untouched — over a
// fully covered pixel the result is bit-identical to ink over plain kCanvas.
// It is TRIM SCRATCH: the span the plain waveform drag / the shift waveform
// press form and `x` consumes into the trim window (architect 2026-07-30 — it
// was the group's spread focus cue until the span form retired; it is no longer
// a playhead form, a selection visual, or a trim-window display). Its default is kCanvas
// lifted by Breeze's OWN shipped View->ViewAlternate step, #141618 -> #1d1f22 =
// +9/+9/+10 per channel — the theme's native "subtly lighter than the surface"
// relationship, applied to our canvas instead of a tint invented for it, so the
// highlighted span reads as the same surface raised rather than as a colour
// wash. The cursor playhead paints straight across it, unchanged — the span is a
// ground, never a playhead.
//
// THE OVERLAY IS A RING ONLY (architect 2026-07-27). kOverlayOutline is the 1px
// border of the phase reset overlay band — the stretch of output immediately
// following the focused reset over which the re-seeded phase takes hold —
// painted OVER the plate, a boundary line like the playheads and the stems, so
// an opaque line crossing ink is correct and intended. The band recolors NO
// ground: it had one until this ruling, and dropping it leaves the aid reading
// as the two EDGES of a span rather than as a tinted region, which is what a
// narrow authoring marker wants. So this is a line color, not the outline
// sibling of any fill — and its default is accordingly the live-1px-mark
// breeze-icons grey kPlayheadCursor and kSelectedStem also default to (its
// provenance recorded at kPlayheadCursor), not a ring value like
// kMarkerOutline.
inline GuiColor kRegionCanvas     = hex(0x42474D);
inline GuiColor kOverlayOutline   = hex(0x7F8C8D);

// THE TRIM FAMILY, in three roles — no longer an orange family of its own
// (architect 2026-07-27): it joins the marker system and the structural line.
// The BRIDGE BAR is the chip-lane band spanning the gap between the two chips
// (render_trim_flags), the pair-drag's grab affordance and the sole "inside the
// trim window" signal now that the out-of-trim dim is retired — and its default
// pair IS the marker pair (kMarker/kMarkerOutline values), so the bar reads as
// an ordinary unselected marker stretched across the span it bounds. The CHIPS
// dissolve into the strip: kTrimChip is the kBackground chrome value exactly, so
// a chip's fill is invisible against the lane it sits in and what marks the
// bound is its RING plus its stems — kTrimChipOutline and kTrimStem both take
// the kLine value, one calm rule. (The 1px STEMS are the waveform-area segments
// in render_trim_stems plus the strip-crossing segments in render_trim_flags;
// they read as part of the chip handle, which is why they follow the chip's ring
// rather than the bar.) Trim sits outside the selection system entirely — a
// trim bound is never a selection member.
inline GuiColor kTrimBar          = hex(0x264A5E);
inline GuiColor kTrimBarOutline   = hex(0x3895C7);
inline GuiColor kTrimChip         = hex(0x202326);
inline GuiColor kTrimChipOutline  = hex(0x686A6C);
inline GuiColor kTrimStem         = hex(0x686A6C);

// -- The redesigned rows (HARD-CODED, kdenlive-sampled) ---------------------
//
// THE COLORS THAT ARE NOT PALETTE KEYS (architect 2026-07-31). Every constant
// in this block and the two below it is constexpr, not a global: they are NOT
// in the 23-key config grammar, not loaded by color_config.cpp, and
// deliberately not tunable — the carve-out the palette header above records.
// Their provenance is the pixel truth of the kdenlive crops
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
// full saturation — the same #3daee9 that is the closed form behind kMarker's
// 30% wash — carried as row 1's FILLED hover pill and row 2's 1px hover
// OUTLINE; the label white is the paper white kText also carries. Both are
// spelled out here rather than borrowed, because these are screenshot samples
// that happen to coincide, not references to the palette. The LINE is row 2's
// separator and its border-bottom, one sampled value for both (they are the
// same rule seen twice — a 1px inert structural edge), unrelated to the
// tunable kLine the un-redesigned surfaces still use.
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
inline constexpr double kRedesignDisabledMix = 0.322;

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

// -- GUI font size ---------------------------------------------------------
//
// The single GUI-wide monospace text size is the font_size setting, a plain
// number of points at the conventional 96 DPI. The current value lives as
// file-scope state in render.cpp beside the monospace grid metrics; the
// file-load and settings-editor application points push it through
// set_gui_font_size_pt. Everything in the two strips scales proportionally
// via gui_font_scale() = font_size / kDefaultFontSizePt, so at the default
// (11) every derived quantity equals its former fixed constant exactly.
inline constexpr double kDefaultFontSizePt = 11.0;

// Set the current GUI font size (points). The setter only records the
// value; the geometry re-measure happens on the next redraw via
// init_monospace_grid_metrics, and the callers route the cache rebuild
// through the same path a window resize uses.
void set_gui_font_size_pt(double pt);

// Proportional scale factor s = font_size / 11. Exactly 1.0 at the default.
double gui_font_scale();

// -- GUI scale (the redesign's own axis) -----------------------------------
//
// THE SECOND SCALE AXIS, and deliberately separate from the font one above. The
// gui_scale setting is an integer PERCENT in [100, 400] (100 = the 1920x1080
// design baseline, 200 = the 4K case); the current value lives beside
// g_font_size_pt as file-scope state in render.cpp, pushed by the SAME three
// application points that push the font size (file load, the settings editor's
// `gui_scale=` commit, the `'` adopt).
//
// WHICH AXIS A SURFACE RIDES IS A DESIGN FACT, not a preference: the
// MONOSPACE-TEXT surfaces (every pre-redesign strip lane) size from the font
// band and keep riding gui_font_scale(); the REDESIGNED rows size from sampled
// screenshot pixels and ride this one. The two are independent knobs by intent —
// a user who wants bigger monospace text is not asking for a taller menu bar.
void   set_gui_scale_percent(int percent);

// Scale factor s = gui_scale / 100. Exactly 1.0 at the default (and never below
// it: the setting's grammar floors at 100).
double gui_scale_factor();

// Text pixel size handed to cairo: font_size * 96 / 72, carried as an exact
// double (points -> pixels at the conventional 96 DPI; warptempo_gui does
// not support HiDPI). Text is the only thing that renders at fractional
// sizes — every other scaled quantity below rounds to an integer. At the
// default this is 11.0 * 96.0 / 72.0, the former kFlagFontSize constant.
double flag_font_size_px();

// Flag chip internal BASE padding around the text glyph bounding box, split
// per axis so the two can be tuned independently. These plus kTextBoxPadPx
// (the uniform four-side text-box gap below) are the whole padding story —
// every chip renderer and the hit-rect computation must read the accessors,
// never a literal. Each is the authored value (1 / -1) scaled by
// gui_font_scale() and rounded with std::nearbyint so it stays an integer:
// the aliased plus-point-five sharp-edge convention for 1 px strokes and
// integer-edged rects keeps holding at every size. At scale 1 each equals
// its authored value by identity (nearbyint(1*1) == 1, nearbyint(-1*1) == -1).
//
// flag_pad_x_px sets chip WIDTH: the painted fill and the hit rect both span
// glyph_advance + 2*(flag_pad_x_px() + text_box_pad_px()) + 2*kChipOutlinePx
// (the outline ring sits outside the padding).
//
// flag_pad_y_px sets the UNPADDED glyph-slot height, which no lane takes and no
// accessor exposes: it is the internal ingredient a text BOX is built from,
// held as file-scope state in render.cpp. It is font
// (ascent+descent) + 2*flag_pad_y_px() + 2*kChipOutlinePx, with a baseline
// offset of flag_pad_y_px() + kChipOutlinePx + ascent. The authored pad_y is
// NEGATIVE (-1) by deliberate design: the measured cairo font band
// (ascent+descent) carries internal leading, so at pad_y = -1 that height is
// nearbyint(font_height - 2) + 2*kChipOutlinePx = font_height at scale 1, the ring
// landing ON the band's outermost row (top and bottom) — empty leading, not
// glyph ink. Every text BOX adds text_box_pad_px() per side on top of it
// (monospace_text_box_h), which lifts the ring clear of the band, and the LANE
// hosting that box adds text_box_margin_px() per side outside the ring
// (monospace_text_row_h).
inline double flag_pad_x_px() { return std::nearbyint(1.0 * gui_font_scale()); }
inline double flag_pad_y_px() { return std::nearbyint(-1.0 * gui_font_scale()); }

// The solid outline ring outside the chip padding: part of the chip rect and
// the row metric (a chip is outline + pad + glyph ink). The single width knob;
// baked into flag_chip_rect and the monospace row/baseline metrics so every
// derived surface (strip heights, baseline solves, hit rects) tracks it
// automatically.
inline constexpr int kChipOutlinePx = 1;

// The ring-to-glyph gap a TEXT BOX carries on ALL FOUR sides: one authored
// pixel, scaled like the pads above so it stays an integer at every font size.
// It widens every text box by 2 (left + right, through flag_glyph_inset_px and
// flag_chip_width_px) and grows every text box by 2 (top + bottom, through
// monospace_text_box_h), so the outline ring no longer overlaps the glyph
// band: at scale 1 the band sits exactly inside the ring, its blank leading
// row separating the ring from the first row of ink. Textless surfaces never
// see it — the marker/trim shapes size from kFlagWidthPx / kFlagHeightPx,
// which no pad feeds, and the redesigned lanes from their own authored
// gui_scale constants.
// std::nearbyint is odd-symmetric, so this cancels flag_pad_y_px() exactly at
// every scale (their sum is 0, whatever the font size).
inline constexpr int kTextBoxPadPx = 1;
inline double text_box_pad_px() {
    return std::nearbyint(
        static_cast<double>(kTextBoxPadPx) * gui_font_scale());
}

// The empty gap a text box carries OUTSIDE its ring, VERTICAL ONLY: two
// authored pixels of bare lane above the box and two below. The distinction
// from kTextBoxPadPx is exactly css's — PADDING is inside the border (glyph
// band to ring), MARGIN is outside it (ring to lane edge) — and this is the one
// term that makes a text LANE taller than the text BOX it hosts:
// monospace_text_row_h() (the lane) is monospace_text_box_h() (the box) plus one
// margin per side, flag_chip_rect insets the box top by one margin from the lane
// top, and the lane-top-relative baseline offset carries the same term so the
// glyphs travel with the box. Horizontal geometry is untouched — a box's left
// and right edges still sit exactly where its caller places them. Scaled and
// rounded like the pads above so it stays an integer at every font size.
inline constexpr int kTextBoxMarginPx = 2;
inline double text_box_margin_px() {
    return std::nearbyint(
        static_cast<double>(kTextBoxMarginPx) * gui_font_scale());
}

// The glyph-run inset from a chip's left edge: the outline ring plus the left
// inner pads (kChipOutlinePx + flag_pad_x_px() + text_box_pad_px()). The ONE
// value every chip anchor/caret site uses to place the glyph origin relative to
// the chip's left edge (where flag_chip_rect's r.x lands — for marker flags the
// left-anchored position on the marker's pixel column). Every renderer passes
// anchor_x = text_left + flag_glyph_inset_px() and back-derives the chip edge
// via EditorTextBox::hl_pad, so paint and hit share one geometry.
inline double flag_glyph_inset_px() {
    return kChipOutlinePx + flag_pad_x_px() + text_box_pad_px();
}

// The outer (window-edge) gap between each strip's edge-most lane and the
// window edge, and the waveform-side gap between the innermost lane and the
// waveform. Stays a compile-time zero under the font_size scaling — zero is
// scale-invariant — so the lanes pack tight against the window edges and the
// waveform. The constant survives so the gap reappears structurally if it is
// ever un-zeroed (the strip lane-stack geometry in main.cpp carries it).
constexpr double kFlagBottomLiftPx = 0.0;

// Fixed-pixel mirrored strip lane grid. G is the single tunable inter-lane gap
// between each adjacent lane pair within a strip. One named constant, one
// place to change it. Now 0 — the lanes of each strip touch, and the
// waveform-side and outer (window-edge) gaps (both kFlagBottomLiftPx, also 0)
// vanish, so lanes and strips pack tight against each other and the window
// edges. Stays a compile-time zero under font_size scaling — zero is
// scale-invariant.
constexpr double kRowGapPx = 0.0;

// Defensive window floor (a conservative 640x480 minimum). Enforced two ways:
// the Wayland set_min_size hint at toplevel creation, and an internal clamp in
// the geometry helpers so the waveform arithmetic is always valid regardless of
// what the compositor sends. Not sized to fit content — the longest dialogue
// may clip at the floor, which is acceptable (nobody authors at 640x480).
constexpr int kMinWindowWidthPx  = 640;
constexpr int kMinWindowHeightPx = 480;

// Authored pixel geometry of a marker flag, scaled by
// gui_font_scale(), rounded with std::nearbyint, floored to a sane minimum.
// The flag is a RECTANGLE two pixels taller than wide (a slight upright
// rectangle) that carries a tip-down TRIANGLE directly beneath it; the tip
// marks the frame. These are the values at the default font size.
inline constexpr int kFlagWidthPx  = 15;
inline constexpr int kFlagHeightPx = 17;

// The flag rectangle's painted width / height for the current font size. The
// TRIM b/e chips are textless rectangles of this WIDTH on BOTH axes — square,
// and so shorter than a marker flag: their lane's height is flag_lane_w_px()
// too (the top lane table in main.cpp), which is where a chip's height comes
// from. Both accessors carry a >= 5 px floor so a tiny font still leaves a
// usable, outline-able shape.
inline int flag_lane_w_px() {
    int w = static_cast<int>(std::nearbyint(
        static_cast<double>(kFlagWidthPx) * gui_font_scale()));
    // Force the scaled width ODD (bump an even result up by one). An odd flag
    // width is the invariant that keeps the fused tip-down triangle's tip
    // centered exactly on the marker's 1px column AND keeps the playhead mask
    // width (2*playhead_triangle_h_px() - 1) equal to the flag width at every
    // font size: with an even width, playhead_triangle_h_px() = (w+1)/2 rounds
    // down and the mask comes out one pixel NARROWER than the flag (e.g. w=20
    // -> H=10 -> mask 19), breaking the marker/playhead shape identity. The
    // floor below stays odd (5).
    if ((w & 1) == 0) ++w;
    return w < 5 ? 5 : w;
}
inline int flag_lane_h_px() {
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kFlagHeightPx) * gui_font_scale()));
    return h < 5 ? 5 : h;
}

// Authored pixel height of the MENU ROW — the top strip's lane 0, at the
// window edge (the kdenlive menu bar, row 1 of the redesign). 30 at 100%
// gui_scale, measured off the row_1_button crops.
//
// THE TWO SCALE AXES MEET HERE: the redesigned lanes size on
// gui_scale_factor() rather than gui_font_scale(). They host PROPORTIONAL text
// at a fixed design size and belong to the redesign's scale axis, not to the
// monospace font's — the four un-redesigned lanes below them keep scaling on
// the font, and the two knobs move independently by design (see the gui_scale
// block above). Rounded with std::nearbyint and floored like every other lane
// metric; the floor is defensive only, since gui_scale never goes below 100.
inline constexpr int kMenuRowHeightPx = 30;
inline int menu_row_h_px() {
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kMenuRowHeightPx) * gui_scale_factor()));
    return h < 5 ? 5 : h;
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
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kToolbarBorderPx) * gui_scale_factor()));
    return h < 1 ? 1 : h;
}
inline int toolbar_row_content_h_px() {
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kToolbarRowHeightPx) * gui_scale_factor()));
    return h < 5 ? 5 : h;
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
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kTabRowBorderPx) * gui_scale_factor()));
    return h < 1 ? 1 : h;
}
inline int tab_row_content_h_px() {
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kTabRowHeightPx) * gui_scale_factor()));
    return h < 5 ? 5 : h;
}
inline int tab_row_h_px() {
    return tab_row_content_h_px() + tab_row_border_h_px();
}

// Authored pixel geometry of the ICON ROW — the top strip's lane 3, under the
// tabs (row 4 of the redesign: the eleven view/mode/action buttons). Measured
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
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kIconRowBorderPx) * gui_scale_factor()));
    return h < 1 ? 1 : h;
}
inline int icon_row_content_h_px() {
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kIconRowHeightPx) * gui_scale_factor()));
    return h < 5 ? 5 : h;
}
inline int icon_row_h_px() {
    return icon_row_content_h_px() + icon_row_border_h_px();
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
inline constexpr int     kTooltipDamageHeightPx = 60;
inline constexpr int64_t kTooltipDelayMs        = 700;  // kdenlive-ish; architect-tunable
inline int tooltip_damage_h_px() {
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kTooltipDamageHeightPx) * gui_scale_factor()));
    return h < 5 ? 5 : h;
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
    const int v = static_cast<int>(std::nearbyint(
        static_cast<double>(kPopupBorderPx) * gui_scale_factor()));
    return v < 1 ? 1 : v;
}
inline int popup_item_h_px() {
    const int v = static_cast<int>(std::nearbyint(
        static_cast<double>(kPopupItemHeightPx) * gui_scale_factor()));
    return v < 5 ? 5 : v;
}
inline int popup_sep_margin_y_px() {
    const int v = static_cast<int>(std::nearbyint(
        static_cast<double>(kPopupSepMarginYPx) * gui_scale_factor()));
    return v < 0 ? 0 : v;
}
inline int popup_item_margin_y_px() {
    const int v = static_cast<int>(std::nearbyint(
        static_cast<double>(kPopupItemMarginYPx) * gui_scale_factor()));
    return v < 0 ? 0 : v;
}


// Height H (px) of the code-generated tip-down triangle, SHARED by the playhead
// cursor and every marker/trim flag. The triangle width is 2*H - 1 (odd by
// construction, so its tip centers exactly on the 1 px column). H is DERIVED
// from the flag width so the widest triangle row is the flag rectangle's OWN
// width: H = (kFlagWidthPx+1)/2 = 8 at scale 1 gives a top row of 2*8-1 = 15 =
// kFlagWidthPx (the odd-row rule — every triangle row is odd, top row 15 down to
// a 1 px tip). The slopes therefore leave the rectangle's exact bottom corners
// and run continuously to the tip with NO inward step: the triangle's top row
// IS the rectangle's bottom edge, so the marker width is the rect width at
// every font size and the rect->triangle outline flows without a 90-degree jog.
// Clamped to at least 2 so the triangle always has a tip row below a top row.
// The half-width below derives from H, so the two can never drift.
inline int playhead_triangle_h_px() {
    const int h = (flag_lane_w_px() + 1) / 2;
    return h < 2 ? 2 : h;
}

// Half-width (px, measured from the triangle's vertical centerline) of the
// shared tip-down flag/playhead triangle at `rows_below_base` pixel rows below
// its BASE (top) edge. The base row spans the full flag width — half-width
// flag_lane_w_px()/2 — and the triangle tapers LINEARLY to a zero-width tip
// playhead_triangle_h_px() rows further down. This is the single owner of that
// taper: paint_flag_shape derives the triangle's base corners and apex from it,
// and hit_test_flag uses it to decide whether a point in the triangle lane is
// inside the shape, so the painted slope and the clickable slope cannot drift.
// Clamped to the [base, tip] span (0 above the base, 0 at/below the tip).
inline double flag_triangle_half_width_at(double rows_below_base) {
    const double H = static_cast<double>(playhead_triangle_h_px());
    if (H <= 0.0) return 0.0;
    double t = rows_below_base / H;   // 0 at the base, 1 at the tip
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return (static_cast<double>(flag_lane_w_px()) / 2.0) * (1.0 - t);
}

// The cached cairo A8 mask surface for the tip-down triangle (W = 2H-1 by H,
// ANTIALIASED — the triangle path is filled with AA enabled so the two slopes
// carry baked gray edge alphas). Owned by render.cpp file-scope state beside the
// grid metrics; regenerated when H changes. Stamps the PLAYHEAD cursor triangle
// (centered on the column, tip at the waveform top edge); the per-frame playhead
// redraws take the cheap cached-mask stamp rather than a live path fill. The
// marker/trim flags fill their own AA triangle path in paint_flag_shape (so fill
// and outline blend as one shape); both are the identical 15-wide/H=8 geometry.
// Never null.
cairo_surface_t* playhead_triangle_mask();

// Waveform-internal top/bottom inset, in pixels. The drawn waveform samples
// are confined to [area.y + waveform_inset_px(), area.y + area.h -
// waveform_inset_px()] so the waveform is symmetric about its area center and
// the marker/trim stems have a clean stem-only band at the top before the
// samples begin. Equal to the triangle mask height by construction. (The
// cursor triangle no longer sits inside this band — it moved to its own
// triangle lane above the waveform — so the former triangle-clearance
// rationale retires; the symmetric-margin purpose remains.)
inline int waveform_inset_px() { return playhead_triangle_h_px(); }

// Half-width (px) of the tip-down triangle's horizontal footprint, H - 1
// (the mask is 2H-1 wide, centered on the column); bounds the playhead's
// off-screen cull and its invalidation strip. Single definition shared by
// render.cpp (cull) and main.cpp (invalidation). At scale 1 it is 7.
inline int playhead_half_px() { return playhead_triangle_h_px() - 1; }

// Pre-first-paint fallback for the measured monospace row height and baseline
// offset (Liberation Mono at the DEFAULT 11 pt — these stay compile-time and
// assume the default font size). SEED ONLY, never live geometry:
// init_monospace_grid_metrics runs at the top of every redraw and overwrites
// BOTH before anything paints, so no pixel is ever placed against these values.
// They exist because on_resize can fire before that first redraw, and the
// geometry it computes must be sane (never a negative waveform) meanwhile.
// Both seed the UNPADDED metrics, which no lane takes directly: the -1.0 term
// is the authored flag_pad_y_px value at scale 1, the 1.0 term is
// kChipOutlinePx, and the 13.0 is the font ascent — the ascent measured for the
// font `fc-match monospace` resolves to on this host (Liberation Mono at
// 14.6667 px), the same 13 that puts the row height at 18 (ascent 13 + descent
// 5 = 18, then nearbyint(18 - 2) + 2*1 = 18; baseline -1.0 + 1.0 + 13.0 = 13.0).
// The TEXT-row accessors derive from these seeds like they do from a real
// measure, adding kTextBoxPadPx per side for the box (20 tall at scale 1) and
// kTextBoxMarginPx per side for the lane hosting it (24 tall, lane-relative
// baseline 16.0).
constexpr int    kRowHFallbackPx       = 18;
constexpr double kRowBaselineOffFallbackPx =
    -1.0 + 1.0 + 13.0;

// Forward declarations: defined with their full doc comments below. Needed here
// because flag_chip_rect (inline) reads the text BOX height and the lane-top
// baseline offset — the box is the padded metric, never the unpadded one and
// never the lane it sits inside.
int monospace_text_box_h();
double monospace_text_row_baseline_offset();
// Forward declaration: defined with its full doc comment below. Needed here
// because flag_chip_rect (inline) computes the chip width from it.
double monospace_advance();

// Char-0 origin (px) of an editor's editable text run: the box anchor plus the
// static prefix's exact monospace advance (glyph count * monospace_advance()).
// The ONE owner shared by render_editor_text_box's paint-time editable_left and
// the click-to-caret geometry (active_editor_text), so the caret origin can
// never drift from the painted glyph run. std::string_view accepts both the
// const char* editor prefixes and render_editor_text_box's std::string prefix.
inline double editor_text_glyph0_x(double anchor_x, std::string_view prefix) {
    return anchor_x +
        static_cast<double>(prefix.size()) * monospace_advance();
}

// Total chip width (px) for a glyph_count-glyph chip: the glyph advance plus
// both inner pads (the base flag_pad_x_px and the four-side text-box gap) plus
// the outline ring on both sides. This is the ONE definition of a chip's
// width — flag_chip_rect's r.w below reads it, so a chip's painted and
// hit-tested width match with no drift. The advance is the cached monospace
// arithmetic (glyph count times monospace_advance()), exact for the ASCII chip
// strings and independent of any cairo context.
inline int flag_chip_width_px(size_t glyph_count) {
    const double pad = flag_pad_x_px() + text_box_pad_px();
    const double advance =
        static_cast<double>(glyph_count) * monospace_advance();
    // std::nearbyint, the project's one fractional->integer pixel conversion
    // (banker's rounding, no epsilon nudge), like every other pixel site.
    return static_cast<int>(std::nearbyint(advance + 2.0 * pad))
        + 2 * kChipOutlinePx;
}

// Single source of truth for a text-box's painted/hit rectangle. The
// bottom-strip editors (settings / bpm / render-commit) derive their fill rect
// from this one function so paint and hit cannot drift. (Marker flags and trim
// chips are now fixed-width geometric shapes — see paint_flag_shape — and no
// longer route through here.)
//
// The returned rect is the TOTAL chip footprint INCLUDING the outline ring: it
// grows by kChipOutlinePx on every side relative to the padded glyph box. The
// fill insets by kChipOutlinePx inside it (render_editor_text_box). text_left is
// the box's left edge as the caller already positioned it:
// r.x = nearbyint(text_left).
// The fill starts one pixel right of the left edge, and the glyph run sits
// kChipOutlinePx + flag_pad_x_px() + text_box_pad_px() (=
// flag_glyph_inset_px()) inside the chip edge. The vertical geometry is the BOX
// pair, not the LANE's: the height is monospace_text_box_h() (ring + four-side
// pad baked in, never the unpadded metric) and the top is the lane top —
// baseline_y - monospace_text_row_baseline_offset(), that offset being measured
// from the LANE top — INSET DOWNWARD by text_box_margin_px(). So the box sits
// centered in its lane with one clear margin above and below, and r.h is
// strictly less than the lane height it sits in.
//
// Width is computed from the cached per-character monospace advance
// (flag_chip_width_px -> monospace_advance(), measured once on the real paint
// surface at startup), NOT from a per-call cairo_text_extents — that was the
// residual edge bug: paint measured on the window surface, hit on a 1x1 scratch
// surface, and the integer width diverged by 1px. Chip text is ASCII-only, so
// glyph_count is the exact glyph count and glyph_count * monospace_advance() is
// the exact advance, identical at paint and hit, with zero surface dependence.
//
// Inputs (sole caller today: render_editor_text_box, for the marker-text-lane
// flag editor and the bottom-strip editors):
//   text_left   - the box's left edge as the caller positioned it (back-derived
//                 from the editable-text anchor via EditorTextBox::hl_pad).
//                 Chip left edge = nearbyint(text_left); the glyph run sits
//                 flag_glyph_inset_px() (ring + left inner pads) to the right
//                 of it, folded into where the caller places the glyph origin
//                 vs. text_left (see consumers).
//   glyph_count - number of glyphs in the box's text (== text.length()).
//                 Width = flag_chip_width_px(glyph_count).
//   baseline_y  - the text baseline y the caller solved for its row (every
//                 caller solves it as lane.y + the lane-top baseline offset).
//                 The lane top is baseline_y -
//                 monospace_text_row_baseline_offset(); the box top is one
//                 text_box_margin_px() below it and the height is
//                 monospace_text_box_h().
//
// Returns the integer GuiRect [x, y, w, h]; rounding happens here, once, through
// std::nearbyint like every other pixel conversion.
// Consumers use the returned ints directly — no consumer re-rounds or
// recomputes any edge.
inline GuiRect flag_chip_rect(double text_left, size_t glyph_count,
                              double baseline_y) {
    GuiRect r;
    // Both conversions are std::nearbyint, the project's single rounding
    // convention for fractional->integer pixels.
    r.x = static_cast<int>(std::nearbyint(text_left));
    const int lane_top = static_cast<int>(std::nearbyint(
              baseline_y - monospace_text_row_baseline_offset()));
    r.y = lane_top + static_cast<int>(text_box_margin_px());
    r.w = flag_chip_width_px(glyph_count);
    r.h = monospace_text_box_h();
    return r;
}

// The former kFlagFontSize constant (11.0 * 96.0 / 72.0) is now the runtime
// accessor flag_font_size_px() declared above — same pt->px arithmetic,
// driven by the font_size setting instead of a fixed 11.

// Editor text-box primitive. Draws the full editable-text-box
// anatomy shared by the flag-payload editor (top strip) and the settings
// editor (bottom strip), in paint order: solid fill behind the editable
// region, optional static prefix, editable text, selection swap, and a
// blink-gated 1-px cursor. Killing the duplication between the two editors
// is the point — both callers differ only in the resolved fill color, the
// optional prefix, and the anchor.
//
// Geometry: `anchor_x` is the left edge of the prefix (or of the editable
// text when `prefix` is empty). The editable region paints at
// `anchor_x + prefix_advance`; the solid fill covers only the editable
// region (the prefix, if any, sits to its left on the canvas), via the
// shared flag_chip_rect helper. The box height is monospace_text_box_h() and
// its top is one text_box_margin_px() below the lane top (`baseline_y -
// monospace_text_row_baseline_offset()`), so the box sits centered in its lane
// with a clear margin above and below — callers solve baseline_y from their
// lane rect and the box lands inside it. The cursor is a filled one-pixel
// integer rectangle at the nearbyint'd column (the half-pixel +0.5 convention
// belongs to 1px STROKES and left with the stroked form), AA off, for a
// crisp single-pixel column. The cursor and the selection highlight span only
// the glyph ink band (ascent-to-descent) — NOT the whole box; only the step-1
// fill spans the full padded box. Nothing paints in the lane's margin.
//
// Colors are pre-resolved by the caller: `fill` is the resolved chip
// color and `text_color` is kText. The selection swap fills the selected
// range with `text_color` and repaints the selected substring in `fill`
// for contrast.
//
// Step 1 always paints the outer kChipOutlinePx band of the chip rect
// (flag_chip_rect, which includes the ring) in `outline`, then fills the inner
// rect inset by kChipOutlinePx in `fill` — the solid outline ring around the
// box. The chips pass their state-dependent outline siblings; the bottom-strip
// editors pass kBackground for BOTH ring and fill (an invisible ring — the box
// reads as light text on the dark strip) and kAccent/kAccentOutline when
// red-flashing, exactly a parse-fail chip's colors. The cursor and the
// selection highlight span the glyph ink band, which sits inside the padding
// inside the outline, so both stay within the ring whenever visible.
struct EditorTextBox {
    double               anchor_x        = 0.0;
    double               baseline_y      = 0.0;
    std::string          prefix;            // optional; "" = none
    std::string          text;              // editable content
    // The glyph-run inset from the chip's left edge (ring + left inner pads =
    // flag_glyph_inset_px()). render_editor_text_box back-derives the chip edge
    // as editable_left - hl_pad, so hl_pad must equal the inset the caller used
    // to place anchor_x for the fill rect and the glyph run to coincide.
    double               hl_pad           = flag_glyph_inset_px();
    GuiColor             fill             = kMarker;
    GuiColor             text_color       = kText;
    bool                 has_selection    = false;
    int                  selection_start  = 0;
    int                  selection_end    = 0;
    bool                 cursor_visible   = false;
    int                  cursor_pos       = 0;
    GuiColor             outline          = kMarker;
};
// The box always paints fully opaque. (It once took an `alpha` that composited
// the whole box through a cairo group as a disabled chip's dim cue; disablement
// is an opaque color class now — kTextDisabled for the glyphs, kMarkerDisabled
// for a shape — so the parameter had no producer left and went with the group.)
void render_editor_text_box(cairo_t* cr, const EditorTextBox& s);

// Screen-coord rect of one rendered flag, keyed back to its marker index.
// Emitted in the same order flags appear left-to-right.
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
// WAVEFORM AREA in the lighter kCanvas. on_redraw calls the first over the whole
// exposed rect, then the second over the exposed part of the waveform area, so
// the canvas wins exactly the pixels the plate, playheads, ground recolors, and
// trim stems paint on — cold frames (no plate yet) included.
//
// render_canvas ALSO owns the waveform area's 1px BORDER: after the kCanvas
// fill it paints the area's topmost and bottommost pixel rows in kLine. The
// border is taken FROM the area, not added to it — the waveform area rect is
// unchanged and the CONTENT band shrinks by one row at each end
// (waveform_content_rect below). Top and bottom only; the area's sides are the
// window edges (and the inert right gutter), which need no rule.
void render_background(cairo_t* cr, int x, int y, int w, int h);
void render_canvas(cairo_t* cr, int x, int y, int w, int h);

// The waveform area's CONTENT band: the area minus the two kLine border rows
// render_canvas paints at its top and bottom. Every pass that fills a BAND
// inside the area clips to this — the plate blit, the region ground recolor,
// and the overlay ring's runs — so the border rows survive the frame no
// matter what covers the area. 1px VERTICALS deliberately do not: the
// playheads, the marker/trim stems, and the strip-drag anchor stem run the full
// area height and cross the border, which is correct for a position line and is
// not special-cased anywhere. Degenerate areas (h <= 2, unreachable under the
// window floor) pass through unshrunk rather than inverting.
inline GuiRect waveform_content_rect(GuiRect area) {
    if (area.h <= 2) return area;
    return GuiRect{area.x, area.y + 1, area.w, area.h - 2};
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
// THE SILHOUETTE IS A TIP POLYLINE. Each column reduces to two TIPS in float
// rows — its raw maximum as the top tip, its raw minimum as the bottom — and
// every adjacent pair is joined by TWO Wu-style antialiased segments, top tip to
// top tip and bottom tip to bottom tip. Those segments are the whole of the
// inter-column connectivity, at every scale. The earlier raw-to-raw BRIDGE that
// widened a column's interval to overlap its neighbour's is RETIRED with them:
// it joined a spike to a short neighbour by filling the gap with a hard solid
// block, so a spike had soft tips but hard 1-px vertical sides. Two steep
// diagonals put an antialiased slope on those sides instead, which is what the
// level-1 jaggedness needed.
//
// A column additionally fills an INTERIOR when it is TALL — its two tips more
// than kThinIntervalPx (1.0 row, tunable) apart, i.e. it has real envelope mass:
// opaque rows between the tips, with the two boundary rows taking FRACTIONAL
// COVERAGE (row floor(yt) gets floor(yt)+1-yt, row floor(yb) gets
// yb-floor(yb)). A THIN column has no interior to fill, so its two tip segments
// ARE its whole rendering: they stay separate and render the subpixel extent as
// a soft partial-coverage BAND — which is the honest antialiasing of a feature
// narrower than a pixel, and the look the arc was after. They land on the same
// centre line only at exactly ZERO extent. The never-fade floor is unaffected
// either way, since it comes from each segment's endpoint units. At spp 1-8
// nearly every column is thin, so this band IS the rendering there — exactly the
// thin-signal material the old bar-chart look was worst on. That is the only
// thing the tall/thin distinction still decides: whether an interior is filled.
//
// SEGMENT GEOMETRY: dx is always one column, dy unbounded (a transient can step
// hundreds of rows between neighbours). Each segment first deposits a FULL unit
// of coverage at both endpoint tips — unconditionally, so an endpoint can never
// be left at a fraction by its row phase, which is what would otherwise let a
// V-vertex composite to half alpha and read as a dropout — and then, when it
// spans more than one row, walks those rows and splits each one's unit between
// the two columns by where the segment crosses that row's centre. Deposits
// clamp their CENTRE into the band so a tip resting exactly on a rail (PCM -1.0
// does) still renders one full in-band row; the diagonal's walk instead DROPS
// out-of-band rows, which is geometrically right for a clipped tail. So every
// column carries at least one full pixel-equivalent at every slope and at the
// rails: flat material softens to a hairline but can never fade out or vanish.
//
// THE TWO HALOS: both edge columns have a neighbour this call does not draw, and
// each needs it for the same reason — a column's ink comes from the segments on
// BOTH its sides, so an edge column missing one is under-covered relative to the
// same audio rendered interior, and it shifts under a pan.
//   LEFT, global column col0-1, read before the loop: supplies the previous tips
//     the FIRST drawn column's incoming segments run back to.
//   RIGHT, global column col0+area.w, read after the loop: supplies the tips the
//     LAST drawn column's outgoing segments run forward to. Only that column's
//     share lands — deposits aimed at the offscreen column fall outside the
//     columns this call owns and are dropped.
// Both are SAMPLE-SPAN reads through this same basis, never read-backs of
// painted pixels, and both pick their pyramid level from their own span exactly
// as a drawn column does.
//
// THE TWO WALL CLAMPS MIRROR. At the SONG wall the left halo's span is clamped
// to start at frame 0, and if nothing remains it is EMPTY and the first column
// simply has no left neighbour (a thin one then deposits its own unit, so it
// still cannot vanish). At the EOF wall the right halo's span is clamped to END
// at total_frames, and if nothing remains it is EMPTY too and the last column
// keeps exactly the ink it has — the flush-right rest's contract. Connecting the
// edge columns to offscreen neighbours rather than special-casing them is what
// makes a render's output depend only on its basis and column range — two
// renders of the same columns at the same basis agree exactly, and an edge
// column matches the same audio rendered interior.
//
// THE WRITER: this function does NOT draw through cairo. It writes `dest`'s
// ARGB32 pixel words directly, which is why it takes the surface rather than a
// context. TWO COMPOSITING RULES, side by side:
//   INTERIORS REPLACE. Each tall column's interior is written once into a
//     column the caller already cleared, so replacing is correct and idempotent.
//   SEGMENTS MAX-COMPOSITE. A segment necessarily writes into the column to its
//     left, which is already rendered, so replacing there would punch holes in
//     it; taking the max can only add ink.
// Per column the interior is painted FIRST and its segments after. Because
// segments take the max, an opaque interior pixel stays opaque and a segment
// entering an envelope cannot erode it — that intent rides on the arithmetic,
// not on the paint order. A zero-coverage row writes nothing at all.
//
// Because a tall interval is wider than a pixel it necessarily spans two or more
// distinct rows, so the interior's fractional path can never emit a coincident
// top/bottom write: that case stays discharged by construction, with
// max-compositing covering overlap on the segment side.
//
// Words are PREMULTIPLIED (coverage a gives alpha round(a*255) and channels
// round(a*C)) and come from a 256-entry table built once per call for the one
// ink colour; entry 255 is the opaque interior word, and the alpha byte of any
// pixel on the plate is the table index that produced it — which is what lets a
// segment's max-composite read back a coverage without a side buffer. The
// surface is flushed before the first
// CPU write and marked dirty after the last, so later cairo use sees the
// pixels.
//
// The plate paints uniformly in `color` — it is trim-agnostic, and nothing
// recolors it after the fact: the out-of-trim dim that once masked a second
// color through this alpha is retired, the trim bridge bar being the whole
// inside-the-window signal now. Its alpha is not binary: opaque interiors,
// fractional silhouette edges, transparent gaps. The gaps are what let a
// recolored GROUND (kRegionCanvas, painted before the blit)
// show through, and the fractional edges blend against whichever ground is
// under them.
void render_waveform(cairo_surface_t* dest,
                     GuiRect area,
                     int col0,
                     const GuiAudio& audio,
                     int channel,
                     const WaveformBasis& basis,
                     GuiColor color,
                     const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr);

// Draws a thin 1px vertical line across `area` at column `playhead_pixel_x`
// (offset from area.x, float for subpixel centering) plus, when `draw_triangle`,
// an inverted-triangle indicator above it. No-op if outside. The line always
// paints (column-gated only); the triangle comes from the code-generated mask
// (playhead_triangle_mask(), cached in this module's file-scope state), stamped
// above the stem via cairo_mask_surface and tinted with `color`. The triangle
// belongs to the cursor exclusively; pass
// `draw_triangle = false` for the scanner and the selected-marker stem so only
// the vertical line is drawn. (The complementary triangle-only form was retired
// with the selected-marker focus triangle when the singleton's focus became
// an always-on stem, architect 2026-07-25, so there is no draw_line flag — the
// line is unconditional.)
//
// The line is ONE SOLID `color` end to end, painted straight over whatever it
// crosses — waveform ink included. The former two-tone form (an `ink_plate`
// parameter carrying the displayed plate, whose alpha masked a ground-colored
// overdraw wherever the column crossed an opaque sample) is retired: architect
// 2026-07-26, notch retired with the polarity inversion — the contrast problem
// it patched is solved by the scheme, so the parameter went with it. The
// triangle likewise paints in `color` over the line.
//
// `triangle_lane` is the TRIANGLE LANE rect as top_triangle_row_area reports it
// (the caller holds AppState and this module does not); the mask's top row lands
// on triangle_lane.y. Passing the lane in — rather than deriving it as
// `area.y - mask height` — is what keeps the stamp on the lane the accessors
// report even if the strip gained an inter-lane or waveform-side gap. Required
// on every call, `draw_triangle = false` included, so the parameter cannot be
// forgotten on a call that later starts drawing one.
void render_playhead(cairo_t* cr,
                     GuiRect area,
                     GuiRect triangle_lane,
                     double  playhead_pixel_x,
                     GuiColor color,
                     bool draw_triangle = true);

// Draws the strip-drag ANCHOR STEM: a 1-pixel vertical line at the drag's pivot
// column `col` (window pixels within `area`, clamped here to [0, area.w-1]),
// spanning the full waveform height like a marker stem, in the dimmer
// kStripAnchorStem (a transient drag affordance, deliberately less loud than a
// marker stem). The anchor is
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
// are retired: the marker stem became a live overlay,
// GuiPaintHandler::paint_selected_stem — the SINGLE selected marker's stem, shown
// ALWAYS for a singleton selection (no hover, pin, or gesture condition;
// architect 2026-07-25; a live position drag tracks the proposed position). The
// trim stems below are live too — GuiPaintHandler::paint_trim, below the
// playheads; no stem is cached anywhere.)

// The ONE trim bound-to-column geometry owner. Every consumer of a
// trim bound's pixel column funnels here: the two paint sites (render_trim_stems'
// waveform stem, render_trim_flags' chips / strip stems / bridge gap) and the two
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
// surface); left unclamped, the right-edge-anchored end chip loses its
// bound-edge pixel and outline to the cache clip and its stems fall offscreen.
// Clamping lands the wall on the last visible column so the chip stays fully
// visible and connected. Begin/frame-0 already maps to column 0, unaffected.
// The bridge bar (trim_bridge_gap) reads an OFFSCREEN bound's SIDE (below) to pick
// a side-specific flush sentinel past the visible edge, and the painter clips its
// DRAWN extent to the effective width [0, wave_w) so the fill/ring stop flush at
// the edge (the inert gutter never paints; col_raw is the sentinel input, not the
// drawn position).
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

// The inter-chip bridge-gap column interval [lo, hi) (waveform-relative,
// half-open, EMPTY when hi <= lo), the ONE owner shared by the painter
// (render_trim_flags' bridge bar + its ring border) and the router
// (route_trim_chip_press' pair-drag between test), so a bridge click lands
// exactly on the painted bar. Both bounds must be set (callers gate). The
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
// offscreen-flush and empty semantics past the visible edge; it is NOT the drawn
// interval. The two consumers own the visible boundary identically: the PAINTER
// intersects the drawn extent with the effective width [0, wave_w) (fill,
// top/bottom ring runs; a side border draws only when its raw edge column lies in
// [0, wave_w)), and the ROUTER applies the same [0, wave_w) click gate. So the
// inert non-multiple-of-16 gutter [wave_w, strip_w) NEITHER paints NOR hits, and
// paint == hit exactly everywhere. The border-clip guarantee holds only because
// the sentinels push an offscreen edge STRICTLY past the visible range (never to
// col 0 or col wave_w-1), so the [0, wave_w) test drops that border while keeping
// an in-view one.
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

// The ONE trim chip screen-rect owner: the begin/end edge-anchoring
// rule lives here, consumed by both the painter (render_trim_flags) and the hit
// test (hit_test_trim_chip). A trim bound is an EDGE, not a point: the begin
// chip's LEFT edge sits ON the bound column (rect left = strip_x+col), the end
// chip's RIGHT edge sits on it (rightmost pixel = strip_x+col, so rect left =
// strip_x+col - flag_w + 1). The chip is flag_lane_w_px() wide; its y-band comes
// from `row` (the trim chip lane = top_upper_row_area), whose height is that
// same accessor, so the chip is SQUARE at every font size. Deliberate
// asymmetry vs centered marker flags: a bound at frame 0 / EOF shows its chip
// fully onscreen.
GuiRect trim_chip_rect(bool is_begin, int strip_x, int col, GuiRect row);

// Draws the WAVEFORM-AREA portion of the trim begin/end boundary stems. BOTH
// bounds always paint (the trim window is always set since 2026-07-30 — the
// per-bound has-gates died with the unset state, and at the full window the two
// stems stand on the song edges): a 1px vertical stem at its
// domain-frame column, spanning the WAVEFORM AREA (top at `waveform_area.y`, down
// to the waveform bottom). Unlike marker stems (waveform-only), the trim stem
// ALSO has a strip-crossing segment above it — from the chip's bottom edge down
// through the intervening lanes to the waveform top — painted by render_trim_flags
// (top-strip pass); the two segments meet at `waveform_area.y` to form one
// unbroken line at the bound column. Trim bounds carry NO
// triangle frame tick (unlike markers): Ableton's loop bounds carry none, so
// the stem+chip pair is the whole waveform-side cue. Color is always
// kTrimStem — the stem reads as part of the chip handle, so it follows the
// calm chip pair rather than the bright bridge bar; trim has no
// selected variant. `trim.begin` /
// `trim.end` are in the displayed domain (already warp_frame_map-translated by the
// caller), so no further translation happens here — the columns are placed
// exactly like marker stems against the same viewport. View-independent: drawn
// identically in 'W' and 'P' views. The stem paints solid kTrimStem straight
// over the waveform ink — the ink-notch overdraw and its plate parameter are
// retired (architect 2026-07-26, with the polarity inversion).
void render_trim_stems(cairo_t* cr,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim);

// Draws the begin/end trim-boundary chips in the TRIM CHIP LANE, plus the
// strip-crossing portion of their stems and the inter-chip bridge band. The
// lane band is the `chip_row` PARAMETER — the caller passes
// top_upper_row_area(app) (top-strip lane 4), the same accessor
// hit_test_trim_chip's y-gate and route_trim_chip_press' bridge y-gate read, so
// paint and hit take the band from ONE owner and cannot drift; nothing in here
// re-derives the lane's y from the row heights above it. Only the band's y/h
// are consumed (the chip width is the flag width, the horizontal origin is
// top_strip_area.x). BOTH bounds always paint (the window is always set since
// 2026-07-30; only the viewport cull can suppress a chip): each is a
// TEXTLESS SQUARE (flag_lane_w_px() on both axes — the chip lane's height is
// that same accessor, so a chip is shorter than a marker flag and can never go
// non-square; no glyph, no triangle — Ableton's loop bounds carry none),
// EDGE-ANCHORED on its bound column: the begin chip's LEFT edge on the column
// (body rightward), the end chip's RIGHT edge on it (body leftward). A bound is
// an EDGE, not a point — the deliberate asymmetry vs centered marker flags — so
// a bound at frame 0 / EOF shows its chip fully onscreen. Chip color is the
// CALM pair kTrimChip with a kTrimChipOutline border — the bright pair belongs
// to the bridge bar below, which carries the family's loudness (a chip is a
// handle, the bar is the window). `waveform_area` is the real
// waveform rect, read for its `.w` only — the column-mapping denominator (the
// strip-crossing stems end at the waveform top edge, which this function takes
// from `top_strip_area.y + .h`, the strip's own bottom). Column placement
// matches render_trim_stems against the same viewport — `trim.begin` /
// `trim.end` are already in the displayed
// domain, so no further translation happens here. The chip has NO editable
// payload; it is a plain-press grab target only (trim is outside the selection
// system). Below each chip, a 1px stem segment runs from the chip's bottom edge
// down to the waveform top, meeting the render_trim_stems waveform segment there
// as one unbroken line at the bound column.
// The BRIDGE BAR fills the GAP between the two
// edge-anchored chips — the visual affordance of the pair (bridge) drag's grab
// band, and the one "this is the trim window" signal. It occupies the trim-chip
// lane's vertical band and is the family's
// BRIGHT pair: an opaque kTrimBar fill with a 1px kTrimBarOutline ring, over the
// strip background. The gap interval is the
// shared trim_bridge_gap owner (computed unconditionally, independent of the
// chips' viewport cull): an in_viewport bound bounds the gap at its drawn chip's
// inner edge; an OFFSCREEN bound runs the bar FLUSH via a side-specific sentinel.
// The painter then clips the DRAWN extent to the effective width [0, wave_w) —
// the inert gutter never paints — so an offscreen side fills flush to the edge
// with no chip-width gap and its ring border goes offscreen with the chip. A gap
// shows only when the span is wide enough that the chips do not overlap.
// route_trim_chip_press consumes the SAME owner under the SAME [0, wave_w) gate,
// so the clickable bridge equals the painted bar exactly.
void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect chip_row,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim);

// The two top-strip lanes a flag shape occupies, exactly as the lane accessors
// report them: `flag_lane` = top_flag_row_area (the rectangle's band) and
// `triangle_lane` = top_triangle_row_area (the tip-down triangle's band, whose
// bottom edge is flush with the waveform top). Both accessors delegate to
// strip_row_rect, the single strip-geometry owner, and both take AppState —
// which this module does not see, so the caller resolves them and passes them
// in. That is the point of the parameter: the flag shapes and their hit rects
// land on the SAME bands the empty-lane press gate and every other lane
// consumer read, whatever the strip's gaps and lane heights are, instead of
// being re-derived by stacking upward from the waveform top edge.
struct FlagLaneRects {
    GuiRect flag_lane;
    GuiRect triangle_lane;
};

// Draws marker flags in `top_strip_area` above visible markers. Each flag is a
// FIXED-WIDTH SHAPE centered on its marker's pixel column (see
// iterate_visible_flags_impl): the flag_lane_w_px() x flag_lane_h_px()
// rectangle in the FLAG LANE plus the tip-down triangle directly beneath it in
// the TRIANGLE LANE, treated as one shape and filled in the marker's color
// class. There is NO TEXT (the payload lives in the marker-text lane, shown on
// hover and edited in the Enter flag editor). There is no elision — overlapping
// shapes occlude instead.
//
// Color class (`red_set` = the store indices whose render normalizes to the
// 1.00 fallback, from warp_red_flag_set_cached), in priority order — ONE opaque
// pair per shape, no alpha anywhere, and THREE classes only: selection is not
// a fourth one, so a selected flag's rectangle fill and its whole outline are
// exactly the pair it would paint unselected. Selection's one paint is the
// triangle interior (kWaveform), layered on after the ladder below resolves —
// the full rationale lives at the SELECTION IS NOT A CLASS ruling earlier in
// this file.
//   Disabled:          fill kMarkerDisabled, outline kMarkerDisabledOutline
//                      (WINS over red and default; selection does not alter it).
//   Red (in red_set):  fill kAccent,         outline kAccentOutline.
//   Otherwise:         fill kMarker,         outline kMarkerOutline.
// Trim membership has no effect on flags.
//
// `warp_frame_map`: the displayed-axis translation the painters share (the
// live map in target view). Shapes are collected in ascending painted-x order,
// so in target view the occlusion z-order is applied against post-translation
// positions. Painting is two reverse passes keyed on `selected_set` — selected
// shapes above unselected, leftmost on top within each class (see render.cpp).
// `waveform_width` is the EFFECTIVE waveform width (waveform_area.w), the
// column-mapping denominator; flags share the marker stems' samples-per-pixel so
// a flag centered on its column lands over the column its stem rises at, at
// every window width (the width differs from top_strip_area.w only at a
// non-multiple-of-16 window).
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
                  const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                  const DragOverlay* drag_overlay = nullptr);

// Same column placement as render_flags, without drawing — returns the
// screen-coord rects of the flag RECTANGLES that would be rendered (one per
// visible flag, no elision, so overlapping shapes yield overlapping rects).
// This builder emits the rectangle only — the geometric base the fused
// tip-down triangle is derived from; the caller (hit_test_flag) additionally
// derives and tests that triangle from each rect, so the actual clickable
// area is the rectangle plus the triangle. The caller resolves a rect
// overlap with two forward passes mirroring the painters' two reverse
// passes — the leftmost SELECTED containing rect, else the leftmost
// containing rect = the topmost-painted flag. No cairo context is needed:
// the rect is the fixed flag width/height centered on the column.
// `warp_frame_map` mirrors render_flags so the two stay in sync. In target
// view the flags paint at translated positions, so this helper is called with a
// non-null warp_frame_map and the hit-rects walk the same map (see app_state's
// hit-test path). In source view it is null and positions are untranslated.
// `waveform_width` is the effective waveform width (see render_flags): the hit
// rects must use the SAME column-mapping denominator as the paint so the
// clickable rect coincides with the painted flag.
std::vector<FlagHitRect> compute_flag_hit_rects(
    GuiRect top_strip_area,
    FlagLaneRects lanes,
    int waveform_width,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// The phase-reset flag is the same fixed shape as a warp flag (rectangle +
// triangle centered on the column), textless, and takes the identical
// color-class ladder render_flags documents above — three classes, selection
// none of them: a disabled reset paints the opaque `kMarkerDisabled` /
// `kMarkerDisabledOutline` pair (WINNING over red and default, and unaltered by
// selection), else red (in `red_set` — a coincident-collapse member from
// phase_reset_red_flag_set_cached) fill `kAccent` with `kAccentOutline`, else
// the default `kMarker` / `kMarkerOutline`. Trim membership has no effect.
// `waveform_width` is the effective waveform width (see render_flags), the
// column-mapping denominator shared with the phase-reset stems. Painting is two
// reverse passes keyed on `selected_set` — selected shapes above unselected,
// leftmost on top within each class (see render.cpp).
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
                            const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                            const DragOverlay* drag_overlay = nullptr);

// `waveform_width` is the effective waveform width (see compute_flag_hit_rects).
std::vector<FlagHitRect> compute_phase_reset_flag_hit_rects(
    GuiRect top_strip_area,
    FlagLaneRects lanes,
    int waveform_width,
    const std::vector<GuiPhaseResetMarker>& phase_resets,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// Iteration-aware flag text composer. Returns the
// plain flag text when `iteration_on` is false or the marker is iter-
// ineligible; otherwise splices the inline `+[lo, hi]` bracket after
// the tempo. The single canonical composer for warp flag text — used to seed
// the flag editor in iteration mode (the flags themselves are now textless
// shapes; the payload text moves to the marker-text lane in a later change).
std::string flag_text_iter(const std::vector<GuiWarpMarker>& markers,
                           int idx, bool iteration_on);

// Per-character pixel advance for the monospace font at flag_font_size_px().
// Measured via init_monospace_grid_metrics(); returns 0 if not yet
// measured. Used by click-to-position-cursor in the
// editor (input_handler.cpp -> flag_editor.cpp).
double monospace_advance();

// THE UNPADDED GLYPH SLOT HAS NO ACCESSOR. Its height — cairo_font_extents
// (ascent + descent) at flag_font_size_px() plus 2*flag_pad_y_px() plus
// 2*kChipOutlinePx, the outline ring baked in, kRowHFallbackPx until
// init_monospace_grid_metrics has measured the real font — and its baseline
// offset (flag_pad_y_px() + kChipOutlinePx + ascent) live as file-scope state in
// render.cpp, read directly by the box/lane accessors below and by nothing else.
// NO LANE takes the bare slot and no painter paints against it: every textless
// lane sizes from its own authored constant (kFlagWidthPx / kFlagHeightPx) and
// anything carrying MONOSPACE text takes the box/lane pair below, so the slot
// stays purely the ingredient the box is built from. The redesigned lanes are
// outside this family entirely — they carry proportional text at an authored
// design height on the gui_scale axis (menu_row_h_px, toolbar_row_h_px).

// The text BOX height: the unpadded slot plus kTextBoxPadPx per side, so
// the outline ring clears the glyph band on all four sides (the horizontal half
// of that gap rides flag_glyph_inset_px / flag_chip_width_px). This is the
// PAINTED box — the rect flag_chip_rect returns and render_editor_text_box
// fills and rings.
int monospace_text_box_h();

// The text LANE height: the box above plus kTextBoxMarginPx per side, the empty
// margin outside the ring. A lane is therefore TALLER than the box it hosts —
// the two are separate metrics and must not be substituted for each other. The
// lanes that read this are the marker-text lane and both bottom-strip lanes
// (main.cpp's lane tables).
int monospace_text_row_h();

// Baseline offset from a text LANE's top edge: the unpadded offset plus one
// text_box_pad_px() (the box's own top pad) plus one text_box_margin_px() (the
// lane's margin above the box), so the glyphs sit centered in the box wherever
// the margin puts it. EVERY text-row painter solves its baseline as
// lane.y + this, and flag_chip_rect inverts it to recover the lane top.
double monospace_text_row_baseline_offset();

// Measure and cache the advance width and row metrics. Runs at the top of
// every redraw; no-ops while the pixel size it last measured equals the
// current flag_font_size_px(), and re-measures on the first frame after a
// font_size change. The supplied cairo_t* is used only for measurement;
// the font state is restored on return.
void init_monospace_grid_metrics(cairo_t* cr);

// The pixel font size the grid metrics currently in effect were measured at
// (negative before the first measure). It changes EXACTLY when
// init_monospace_grid_metrics re-measures — that is the whole reason it exists
// as an accessor: it is the ONE scalar that stands for the whole measured grid
// (advance, slot height, baseline) and therefore for every lane height and text
// box derived from it. The pixel caches fold it into their fingerprints so a
// font change is detected BY FIELD rather than by trusting that some other
// fingerprinted quantity (an area dimension, a lane split) must have moved with
// it.
double measured_monospace_font_px();

// Left x (window pixels) of a transient text run of `glyph_count` monospace
// glyphs shown in the marker-text lane over marker `marker_idx`'s painted
// column. Both lane occupants — the hover popup and the flag editor — center
// their run on the marker and clamp it fully onscreen within the lane (unlike
// the flags, the lane text never hangs off an edge); this is that one
// placement owner. Uses the painters' own column math against
// displayed_or_live_target_map — the event-synchronized displayed basis the
// flag pixels were painted with (identity/empty in source view; in target view
// the map the last committed frame's flag cache baked, with the live map as the
// cold-state fallback) — so the run centers on the same column the flag paints.
// Returns -1.0 for an invalid marker index; a valid off-view marker still
// returns a clamped onscreen origin (the lane text is always visible). No cairo
// context.
double lane_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx, size_t glyph_count);

// The frame-addressed core of lane_text_left_x: same placement math, but keyed
// on a marker's authored source frame rather than a warp-store index, so the
// phase-reset column's lane hover (which lives in a different store) shares one
// placement owner with the warp column. The idx overload above delegates here.
double lane_text_left_x_at_frame(
    const AppState& app, const GuiAudio& audio,
    double source_frame, size_t glyph_count);

// The flag editor's caret / text-run origin owner: lane_text_left_x sized by
// the flag editor's current pending text. The single reference the lane paint,
// the click->byte caret math, and the editor-text drag all read, so what is
// shown is where the caret lands. Returns -1.0 for an invalid marker index. No
// cairo context.
double flag_pending_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx);

// THE PHASE-RESET LANE DISPLAY TOKEN, and the one statement of it: what the
// marker-text lane shows over a phase reset, where a warp marker shows its
// composed flag line (flag_text_iter). DISPLAY ONLY — a phase reset authors no
// payload and serializes as a bare frame, so this string exists nowhere but the
// lane. Its three producers are the ambient set builder, the last-selected
// fallback tier (both render.cpp) and the hover composer (viewport.cpp); they
// must agree, or the same reset would render at different widths depending on
// which path resolved it.
//
// FOUR GLYPHS BY RULING (architect 2026-07-27). The former one-glyph "p" sat a
// dense reset cluster right at the marker-text lane's fit verdict threshold, so
// a small zoom change flipped the whole lane between all-texts and the one-run
// fallback and back — the lane blinked. A wider token fails that verdict
// DECISIVELY in a dense cluster and rests in the fallback instead of oscillating
// across it. It stays well under the 9-glyph ambient budget, so it never
// truncates and never reaches the text-hover expansion.
inline constexpr char kPhaseResetLaneToken[] = "p.r.";

// One marker-text-lane run: a marker's composed value at its displayed column.
// `text` is the DISPLAY bytes (may end in the UTF-8 ellipsis "\xe2\x80\xa6" when
// the composed value exceeds the 9-glyph ambient budget), and `glyphs` is the
// DISPLAY glyph count — ALL width math (fill, verdict, hit) uses `glyphs`, never
// text.size(), since a truncated run is 11 bytes / 9 glyphs. `marker_index` is
// the active-column store index (warp or phase-reset per active_markers_view);
// `source_frame` is the DOUBLE centering basis (the mid-drag proposed position
// substituted for a dragged member).
struct LaneTextRun {
    bool        valid        = false;
    int         marker_index = -1;
    double      source_frame = 0.0;
    std::string text;
    size_t      glyphs       = 0;
};

// The marker-text lane's current run SET, arbitrated once so the paint pass and
// the unified marker hit resolver (marker_hit_at, below) read the SAME runs and
// cannot drift. THE OCCLUSION RULE: the lane shows EVERY onscreen marker's text
// ambiently IFF the whole visible set fits without any 9-glyph-capped run
// occluding another (all-or-nothing); when the verdict fails it falls back to
// the one-run arbitration — tier 1 the HOVERED marker's value
// (hover_popup.lane_text), else tier 2 the LAST-SELECTED marker's value composed
// from the live store (flag_text_iter for a warp marker, kPhaseResetLaneToken
// for a phase reset — whose width is chosen against this very verdict, see
// there),
// with the mid-drag DragOverlay substitution and the painted-column offscreen
// cull the flags apply. TRUNCATION IS PERMANENT: every ambient run — both the
// all-visible set AND the fallback single run — caps at the 9-glyph budget (8
// bytes + U+2026), so a dragged/selected truncated marker stays truncated when
// the verdict fails (it does NOT spell out in full). The editor seed, hover
// readout, and copy payload keep full text; the lane run is display-only.
//
// TEXT-HOVER EXPANSION (has_expanded / expanded): the ONE expansion — when the
// pointer hovers a marker's rendered TEXT RUN (not its flag; hover_popup.on_flag
// == false) and that marker's full composed text exceeds the budget, that run
// EXPANDS to the full text in place, painted LAST among the ambient runs (on top,
// occluding neighbors — the one text occlusion) and hit FIRST. It is display+hit
// only: the occlusion VERDICT still runs on the 9-glyph-capped widths (the
// expanded width never participates), and it applies in BOTH modes (in fallback
// the hover-via-TEXT single run expands, reproducing the pre-cap full-text bytes;
// fallback hover-via-FLAG and the last-selected tier stay truncated).
// kPhaseResetLaneToken never exceeds the budget, so a reset never expands.
//
// all_visible == true: `runs` is the whole visible set (capped). all_visible ==
// false: `runs` is the 0-or-1 fallback run (capped). All lane geometry is on the
// DISPLAYED viewport basis (item_viewport_basis) and
// displayed_or_live_target_map — the flag pixels' own basis. The open FlagPayload
// editor is NOT resolved here (it is an OVERLAY: the ambient runs still resolve,
// paint suppresses the editor marker's ambient run and draws the editor box on
// top). advance <= 0 (font not measured) → the empty fallback shape.
struct LaneRunSet {
    bool all_visible = false;
    std::vector<LaneTextRun> runs;
    bool        has_expanded = false;  // a text-hover expansion is active
    LaneTextRun expanded;              // full-text run: paint LAST, hit FIRST
};

// Resolve the current marker-text-lane run set (the occlusion arbitration above),
// the single owner both paint_marker_text_lane and the unified marker hit
// resolver read so the painted runs and the clickable runs are one set. Each
// run's screen rect is derived by the caller exactly as paint does (left =
// lane_text_left_x_at_frame(app, audio, source_frame, glyphs), width =
// glyphs * monospace_advance()). No cairo context.
LaneRunSet current_marker_lane_runs(const AppState& app, const GuiAudio& audio);

// The unified marker hit: the marker is ONE pointer item, hit either by its
// FLAG SHAPE (hit_test_flag: the fixed rectangle plus the fused triangle,
// topmost-painted wins) or by its RENDERED MARKER-TEXT LANE RUN (a run from the
// set current_marker_lane_runs resolves — the ONE arbitration the lane paint
// also reads — when the point lands inside a run's screen rect, derived exactly
// as paint derives it: lane_text_left_x_at_frame for the left edge, glyphs times
// monospace_advance for the width, top_marker_text_row_area for the y-band). The
// EXPANDED run (a text-hover expansion) paints on top, so it is hit FIRST (a
// point over a neighbor's occluded pixels resolves to the expanded run — WYSIWYG)
// with a HALF-OPEN interval; then the ambient runs — all-visible with HALF-OPEN
// intervals (abutting runs cannot double-hit), fallback single run with the
// closed-interval test. `on_flag` records WHICH part was hit: the flag is the
// sole DRAG handle (a run press selects / double-clicks / lands but never arms a
// reposition), AND the hover recompute now reads it (the lane's text-hover
// expansion keys on hovering the RUN, not the flag — see current_marker_lane_runs
// and the recompute short-circuit). index is the active-column store index, -1
// when neither part is under the point. Pure geometry over app/audio (no cairo
// context); homed here beside current_marker_lane_runs so the press chain
// (input_pointer.cpp) and the hover recompute (viewport.cpp) share one resolver.
struct MarkerHit {
    int  index   = -1;
    bool on_flag = false;
};

MarkerHit marker_hit_at(const AppState& app, const GuiAudio& audio,
                        int x, int y);
