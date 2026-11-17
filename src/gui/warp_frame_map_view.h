#pragma once

#include "gui_display_context.h"  // GuiDisplayContext (the translation bodies' subject)
#include "warp_frame_map.h"
#include "warpmarkers.h"   // GuiWarpMarker (the view-overload signature)

#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Builds the target-view warp_frame_map from live warp markers plus scale, mirroring
// the resolve-then-build pipeline paint_handler's on_redraw uses for target-
// view waveform translation. Trim is forced off — target view paints the
// WHOLE song; the warp_frame_map must describe the whole song with warp segments
// where markers exist and identity outside (see paint_handler.cpp commentary
// next to the same construction). Returns an empty vector if
// resolve_warp_markers_for_render or build_warp_frame_map fails; when
// `error_out` is non-null the failure's message (the parser's own string) is
// written there, empty on success — the cache below stores it so the
// target-view validity gate can distinguish "empty because invalid" from
// never-built and kick the user back to source view with the popup. Callers
// in target view route this through render_flags / the trim painters so hit-test
// math and paint stay in sync.
// EVERY BUILD IS LOUD: the `quiet` parameter that forwarded into
// resolve_warp_markers_for_render to suppress normalization stderr lines is DELETED
// (2026-07-29), together with the frozen parser's own — an explicit surgical freeze
// approval from the architect. Its sole caller had been the group tempo drag's
// monotone bisection, which evaluated hypothetical never-live candidate maps, and
// that whole gesture is gone (deleted with the tempo drag), so no hypothetical build exists
// to want silence. The parser-side inventory (FIVE resolve call sites, all of which
// had always passed the loud default) is recorded at that function's declaration.
std::vector<WarpFrameMapSegment> build_target_view_warp_frame_map(
    const std::vector<GuiWarpMarker>& markers,
    double scale,
    int sample_rate,
    long total_frames,
    std::string* error_out = nullptr);

struct AppState;
class GuiAudio;

// Memoized target-view warp_frame_map. One entry, keyed on the inputs that
// determine the map: the warp-marker store generation, the scale
// setting, and the audio identity (sample rate, total frames). The
// entry also carries the FNV-1a hash of the segment list, computed at
// rebuild, so the waveform-cache fingerprint reads it instead of
// rehashing per tick. A failed build is cached too (empty warp_frame_map,
// hash 0, build_error carrying the resolve/build message) — display
// callers treat the empty map as identity, and readers consult
// build_error to tell "empty because the build failed" apart from a
// legitimately empty/identity state.
struct TargetWarpFrameMapCache {
    bool      valid        = false;
    long long markers_gen  = -1;
    double    scale        = 0.0;
    int       sample_rate  = 0;
    long      total_frames = 0;
    std::vector<WarpFrameMapSegment> warp_frame_map;
    uint64_t  hash         = 0;

    // Empty when the last rebuild succeeded; otherwise the
    // resolve_warp_markers_for_render / build_warp_frame_map error string
    // verbatim (tripwire-class only — the resolver normalizes ambiguous
    // marker arrangements rather than refusing). Consumed by the caller
    // that must skip target-domain math when no map is in effect (the
    // dispatch snapshot's playhead translation).
    std::string build_error;

    // Deformed-timeline length: the source total forward-translated
    // through this warp_frame_map (the same formula the S-to-T toggle used).
    // Source total when the map is empty. live_total_frames() reads
    // this, so the value every viewport clamp and zoom bound sees is
    // always the total of the map actually in effect.
    int64_t   tgt_total_frames = 0;
};

// Returns the cache entry for the app's live marker store, rebuilding
// it first if the key does not match. The reference is valid until the
// next call with a changed key (single-threaded GUI use only — the
// waveform worker receives its own copy via the job, never this
// reference).
const TargetWarpFrameMapCache& target_view_warp_frame_map_cached(
    const AppState& app, int sample_rate, long total_frames);

// Memoized RED-FLAG SET for the warp column: the marker-store indices whose
// cues paint STRUCK (the plain cue with its label struck through, in every
// look — resting, inactive, selected — resolve_flag_face's table, render.cpp;
// architect 2026-10-26 ~15:10, the invalid color retired), so
// the mark is never masked by a selection. `red` is a PAINT cue with ONE
// meaning: the render normalizes this marker to 1.00 — or, for a disabled
// coincident member, would ("they both get 1.0 as their effective tempo",
// architect 2026-11-17).
// Two contributors, both computed SILENTLY from the display path (no resolver
// run, no stderr): (1) THE COINCIDENT RUNS — every row that stands the same
// frame or under one millisecond from a store neighbor, DISABLED ROWS COUNTED,
// both rows of a pair and every row of a chain, the parser classifier's
// verdict (warp_coincident_members, warp_frame_map_build.h — the render's
// stage 2 reads the same one, so every struck member renders as 1.00, his
// "if two warp markers are struck in the gui, both should render as 1.0 in
// the parser/engine"); (2) a LABEL-REF fallback via marker_effective — a
// dangling label ref (a disabled definition's refs and a coincident member's
// included, participation being each marker's own flag since 2026-10-29 and a
// member holding no label since 2026-11-17), a ref to an
// AMBIGUOUS label (two live definitions, 2026-10-29) or an extreme-ratio label
// ref, all of which resolve to source_idx == -1 (a pass never normalizes: its
// walk skips refs to the owner behind them, architect 2026-09-13), and with
// them the ambiguous label's DEFINITIONS themselves
// (warp_ambiguous_label_defs, warp_frame_map_build.h — "now they both get
// struck, as do all the label references", architect 2026-10-29). The frame-0
// seed is synthetic (no marker) and never reddens. A member's own
// marker_effective reason (Coincident) is (1)'s, so (2) never counts it twice.
//
// WIDENING THE CUE WIDENS TWO REFUSALS: THE DROP'S AND THE PHASE-RESET
// PROPAGATE'S. The act and face readers that ask whether the render
// normalizes a marker read `collapsed` (the one normalization that is not
// already walled on the marker's kind — (2) reddens only refs and
// definitions). `red` is read by its painters — the warp flag pass
// (waveform_cache.cpp's W and M arms), the open marker-lane field's face
// (render.cpp) and the cut-label tooltip's struck run — and by ONE ACT
// FAMILY, ON THE STRIKE AS PAINTED: the phase-reset copy and its two pastes
// refuse a range holding a struck row, and COPY and PASTE grey on it
// (architect 2026-11-17: "phase rest copy and apste shoudl fails and card
// saying something like 'range contains struck marker'"; the struck rung,
// phase_reset_propagate.h), since the family pairs blocks by the labels the
// strike says the render does not keep. The warp drop refuses a place inside
// a coincidence by the same predicate (warp_marker_in_vicinity_of below), so
// no drop makes a strike.
//
// Keyed on the warp store generation plus the audio identity (total_frames
// feeds marker_effective's last-segment envelope distance; the sample rate
// sets the coincidence's millisecond). It reads the
// COMMITTED store (app.warpmarkers), NOT any mid-drag overlay: the marker drag
// writes only app.drag.moveable_times and mutates app.warpmarkers wholesale at
// commit (bumping the generation), so a red flag persists through a drag and
// re-evaluates only at release — the "wait until commit" rule the
// displayed-target-map already follows. Memoization keeps the classification
// to ONE run per generation change rather than per paint tick, the same
// discipline target_view_warp_frame_map_cached follows.
struct WarpRedFlagCache {
    bool      valid        = false;
    long long markers_gen  = -1;
    int       sample_rate  = 0;
    long      total_frames = 0;
    std::set<int> red;   // red warp-marker store indices — the painters and
                         // the phase-reset propagate's struck rung
    // THE COINCIDENT MEMBERS ALONE — (1)'s own subset of `red`, the parser
    // classifier's verdict (warp_coincident_members) kept apart from the
    // label fallbacks it is unioned with above. This is the RENDER-TRUTHFUL
    // membership the act and face readers take, DISABLED ROWS INCLUDED, so a
    // reader that means "renders as 1.00" composes it with the enabled test:
    // the value pair's gate (payload_eligibility, app_state.cpp), where a
    // member of any kind or bit is eligible and reads 1.00; the copy-previous
    // drop's walk (drop_copy_previous_at_playhead), which ends at a member;
    // and the walls on an authored tempo the render would not apply — the
    // group tempo step's, the singleton step's kind refusal and the BPM
    // sweep's owner refusal. Filled in the same pass that fills `red`, under
    // the same key; no second computation anywhere.
    std::set<int> collapsed;
};

// Returns the warp red-flag cache entry for the app's live warp store,
// rebuilding only when the key does not match. Same single-threaded-reference
// lifetime rule as target_view_warp_frame_map_cached; the flag cache reads the
// set at build time (not per paint).
const WarpRedFlagCache& warp_red_flag_set_cached(
    const AppState& app, int sample_rate, long total_frames);

// THE PHASE-RESET COLUMN HAS NO STRIKE (architect 2026-11-17: "let's do
// anything-goes for phase"; "phase resets should get no strike b/c they
// effectively collapse to 1 in the engine already - the rare case is two
// markers within 1ms of each other but in different windows, but those will
// manifest as an audible click in which case they are manually fixed or they
// will simply go under the radar, which is also acceptable"): resets on one
// frame or in one seed window paint as authored, and the render keeps its own
// treatment — build_phase_reset_source_frames collapses an exact-equal group
// of 2+ enabled resets to one event, and the engine merges resets seeding one
// window.

// WHETHER THE WAVEFORM PICTURE IS MAGNIFIED, AND THE ONE GAIN GATE. The gain
// itself is the continuous curve derived from the source at load — the
// short-term loudness leveler, the downward expander and the inner bar's
// compressor (GuiAudio::gain_curve,
// derive_waveform_gain in waveform_gain.h, which owns the rule); this answers
// only whether a plate applies it, one term (architect 2026-09-24):
//
//   the Waveform Magnification lamp (AppState::show_waveform_magnification,
//   the bare backtick, DARK AT EVERY PROJECT OPEN) — lit, magnified; dark,
//   the raw (flat) picture — in BOTH audio views, on every column and at
//   every zoom.
//
// The raw picture is the default; magnification is asked for. It is
// universal: the painter reads the curve at each column's mapped SOURCE
// frames through the warp map (render_waveform_columns), so a target-view column
// shows the gain and the expander of the source audio it draws.
//
// THE MAGNIFICATION LAMP IS THE ONLY EXCEPTIONS ROAD (architect 2026-09-23): no
// per-passage override, no third state and no drawing of the gain. A passage
// the derived gain serves badly is looked at flat, and the dynamics are what
// the audio and the A/B tabs carry — both tabs always show the same picture,
// the curve being a function of the one source. The drawing excluded is a
// plotted curve, a tint over the picture or a colour read from the gain: the
// lit plate's per-column shade, which read the leveler's gain for one day,
// is struck (architect 2026-09-25, render_waveform_columns) and both lit bars are
// one flat colour, the inner's outline (one device px thick) another
// (2026-09-27).
//
// NO MODE TERM — the `h` view follows the lamp as it stood when the view was
// entered (the lamp is dead there by its allowlist), its plate being the live
// plate. DISPLAY-ONLY: no sample, no render input and no render fingerprint
// field reads it.
//
// THE LAMP ALONE SUFFICES because it cannot be lit before the curve exists:
// the curve is derived on its own thread after the load (GuiAudio::gain_curve),
// and the lamp's one setter is reached only through the backtick's arm, which
// refuses until waveform_magnification_toggle_actionable (below) passes.
bool waveform_magnified(const AppState& app);

// CAN THE MAGNIFICATION LAMP BE TOGGLED — whether the gain curve is ready
// (GuiAudio::gain_curve_ready; the derivation runs on its own thread after the
// load, architect 2026-09-24). ONE OWNER, TWO READERS: the bare backtick's arm
// (input_key_dispatch.cpp), which refuses on a Normal card while it is false,
// and the Toggle Waveform Magnification button's face
// (redesign_button_enabled), which greys on the same answer — the per-tick
// roster comparator (main.cpp) repaints the face on the frame the verdict
// flips. Once true it stays true for the life of the audio object, so a lit
// lamp can always be put out.
bool waveform_magnification_toggle_actionable(const GuiAudio& audio);

// THE PLATE FINGERPRINT'S GAIN FIELD: the derivation's identity
// (kWaveformGainVersion) while the picture is magnified, 0 while it is flat —
// the curve being a pure function of the one immutable source and the
// rule's hard-coded constants (waveform_gain.cpp), the version alone names it
// (nothing derived from the gain is persisted across launches; the record is
// at kWaveformGainVersion). ONE PLACE, so the picture caches' existing hash keys
// re-render on every flip with no per-caller code: the plate fingerprint
// carries it beside the viewport geometry, and the lamp's one setter kicks
// when it moved. Its one live input is the lamp: an S/T switch leaves it as
// it stands, the plate re-rendering on the switch through the fingerprint's
// own target bit and warp-map hash and the switch's closing
// kick_waveform_sync. TWO READERS, re-grepped 2026-09-24: the plate's render inputs, which is also
// where the fingerprint's gain field is captured
// (compute_waveform_render_inputs, waveform_cache.cpp); and the gain kick's
// hash (Viewport::waveform_gain_hash).
uint64_t waveform_gain_fingerprint(const AppState& app);

// Convenience wrappers that own the domain-check and the map selection for the
// common case: translating a single coordinate between the stores' domain and
// the active display domain through the active display context. Source view:
// identity, no map built. Target view: the memoized
// target_view_warp_frame_map_cached, so even repeated calls (e.g. inside a
// loop) cost only a cache-key comparison after the first build. Use these at
// every input / playhead boundary that translates against the live displayed
// domain.
//
// NOT for sites translating against an explicit caller-supplied map — a
// proposed (pre-commit) marker list. Those use the explicit-map
// helpers (painted_column_of_source_frame_on_basis /
// authored_frame_at_x_on_basis) with their own map.
int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame);
int64_t active_domain_to_source_frame(const AppState& app, const GuiAudio& audio,
                                      int64_t domain_frame);

// The stem painters' samples-per-pixel and the single source of truth for the
// on-screen column grid: the live level's spp on the sixteenth-frame grid
// (painter_quantized_spp below), so the visible span w·q is a whole number of
// frames and matches the vp_end the waveform cache carries
// (viewport_end_sample). The viewport snap in clamp_viewport_start (main.cpp)
// and the plate's dispatch take their `q` from here, and the pixel-anchoring
// pair below and the click placement (architect 2026-09-24) take the item
// basis's spp, which is this same q recovered from the span the flags were
// built against, so the viewport grid and the marker grid are one grid — and,
// the quantization being width-free, one grid at every window width. Returns
// 0.0 on degenerate geometry (no strip width / no zoom).
struct GuiRect;
double painter_samples_per_pixel(const AppState& app, const GuiAudio& audio,
                                 const GuiRect& area);

// THE QUANTIZATION ITSELF, for a samples-per-pixel the caller already holds:
// THE SIXTEENTH-FRAME GRID (architect 2026-09-26), q = nearbyint(spp * 16) / 16,
// 0.0 on a non-positive spp. painter_samples_per_pixel is this at the LIVE
// level. IT IS WIDTH-FREE: the quantization reads no width, so at a given spp
// every window size shares one grid, and a hand resize never moves a landing
// at a held zoom. The LEVEL→spp map is device-relative since 2026-09-27
// (spp = working column × 2^(level − 2), samples_per_pixel_at), and the
// working column is fixed per process from the held display width, so within a
// device every level, whole or fractional (every rest a pinch or pen zoom
// leaves), has one step; across devices the same level is the same RELATIVE
// zoom and a different grid, the cross-device promise withdrawn. EVERY
// REST — a landing's and a gesture's alike — lies on ITS OWN LATTICE of that
// step, origin + nearbyint(k·q), the origin derived from the start the act
// asked for and held by the camera (architect 2026-10-31 and 2026-11-15,
// the no-grid arc; AppState::viewport_lattice_origin, which owns the two
// rules); the walls rest on THE FRAME-0 LATTICE, nearbyint(k·q).
//
// WHY 16 — the waveform width rule's own reason (waveform_area, main.cpp):
// every waveform width is a multiple of 16, so w·q is a WHOLE number of
// frames at every level: the right wall, the viewport grid and the visible
// span stay integral. The working column is a whole number of frames, so
// every whole level's spp (column × 2^(L − 2): whole from level 2 up, a half
// at level 1) is EXACT in sixteenths — at a WHOLE level, the working zoom
// included, q is the logical spp itself, at every rate. A fractional rest
// rounds to the nearest sixteenth, invisibly (under a 1/32-frame residue per
// column). Idempotent: a q passed back in returns itself.
inline constexpr double kPainterGridSubdivisions = 16.0;
inline double painter_quantized_spp(double spp) {
    if (!(spp > 0.0)) return 0.0;
    return std::nearbyint(spp * kPainterGridSubdivisions) /
           kPainterGridSubdivisions;
}

// THE FRAME-0 LATTICE'S k-TH POINT at painter step q: nearbyint(k * q) — the
// walls max_viewport_start_grid walks (main.cpp) and the term every
// lattice's points are built from. A lattice of origin o is the
// points o + viewport_grid_point(k, q) (AppState::viewport_lattice_origin).
inline int64_t viewport_grid_point(int64_t k, double q) {
    return static_cast<int64_t>(std::nearbyint(static_cast<double>(k) * q));
}

// THE GRID SNAP ALONE: the point of the lattice of origin `origin` nearest
// `start` at painter step q (q > 0), origin + nearbyint(m·q) with
// m = nearbyint((start − origin) / q), with no wall — the snap half of the
// chokepoint's rest (resting_viewport_start, main.cpp, which clamps its
// answer into the two walls). Its other readers: the touch-nav
// body's two wall tests (apply_touch_nav_update), on the live origin's.
inline int64_t nearest_viewport_grid_point(int64_t start, double q,
                                           int64_t origin) {
    return origin + viewport_grid_point(static_cast<int64_t>(
        std::nearbyint(static_cast<double>(start - origin) / q)), q);
}

// Viewport-END sample for a strip `w` px wide at samples-per-pixel `spp`:
// vp_start + w·q, q the painter quantization of `spp` (painter_quantized_spp,
// so a caller may pass the logical spp or q itself), the painter-quantized
// right anchor the plate, the flag/trim hit tests, and the trim column math
// all derive their upper bound from. w·q is a whole number of frames at every
// multiple-of-16 width (the reason is at painter_quantized_spp); the
// nearbyint only restates it as an integer. One owner so every viewport-END
// derivation rounds the span identically, and so (vp_end − vp_start) / w
// recovers q exactly — the item and plate bases' spp.
inline int64_t viewport_end_sample(int64_t vp_start, double spp, int w) {
    return vp_start + static_cast<int64_t>(std::nearbyint(
        painter_quantized_spp(spp) * static_cast<double>(w)));
}

// THE VIEWPORT'S OWN LATTICE INDEX m: the grid index of a lattice-snapped
// viewport start on its lattice of origin `origin` at painter step q (q > 0),
// m = nearbyint((vp_start − origin) / q), as a whole-valued double. THE ONE
// RECOVERY, shared by every reader of the lattice: the frame->column
// placement (displayed_column_at, K − m), the exact place (displayed_x_at)
// and its inverse, the pen acts' conversion (displayed_frame_at_x), so the
// place a frame paints at and the frame a place lands on are indexed from
// the same m. Caller supplies a `vp_start` and an origin in the SAME domain
// as `q` — the arithmetic is domain-blind, so source view and target view
// both read it, each in its own active display domain.
//
// THE RECOVERY IS EXACT for product-reachable audio lengths, for a start the
// chokepoint settled (the start lies on its own lattice by the chokepoint's
// invariant): at the deepest
// numeric zoom q >= column / 2 frames/px (23 or more on the tablet)
// and a source length fits well within the
// double mantissa, so |(vp_start − origin)/q - m| << 0.5. The target domain's total
// is at most 16x the source's — build_warp_frame_map divides each source delta
// by the product of tempo, marker scale and settings scale, and all three
// floors are legal at once (0.25 * 0.5 * 0.5 = 1/16, value_format.h), so the
// bracket bound is 16x rather than the tempo bracket's 4x alone. A RIFF source
// caps under 2^32 bytes, i.e. under ~1.1e9 frames at 16-bit stereo, so even a
// 16x target total is ~1.7e10 — four orders of magnitude inside the 2^53
// mantissa, and the argument holds in that domain too. It is NOT claimed exact
// over the whole int64 range.
inline double viewport_lattice_index(double vp_start, double q,
                                     int64_t origin) {
    return std::nearbyint((vp_start - static_cast<double>(origin)) / q);
}

// THE PLACEMENT'S INDEX SELECTOR (architect 2026-11-06, session 69, TIES
// RIGHT): the nearest integer to a lattice quotient x, an EXACT half-integer
// going toward +inf, as a whole-valued double — integer K owns [K − 0.5,
// K + 0.5). Shared by the frame->column placement (displayed_column_at) and
// the chokepoint's Exact origin (resting_viewport_start, main.cpp: one
// origin per start's place within a step); the rule's reason is at
// displayed_column_at. The
// compare is against the representable half-integer lo + 0.5, so only an
// exact tie goes right: floor(x + 0.5) is NOT this — its addition rounds the
// double just below a half up across the tie. No epsilon, no rounding-mode
// change; a negative x takes the same expression (toward +inf). Not the
// viewport's own index (viewport_lattice_index above): m is near-integer on a
// settled basis and decides no tie.
inline double lattice_index_ties_right(double x) {
    const double lo = std::floor(x);
    return lo + (x >= lo + 0.5 ? 1.0 : 0.0);
}

// THE PAINTERS' COLUMN PLACEMENT for a value ALREADY in the displayed domain,
// ON THE LATTICE'S OWN INDEX (architect 2026-11-06, session 68): the value's
// nearest lattice index K = lattice_index_ties_right((displayed − origin) /
// spp) less the viewport's own m = viewport_lattice_index(vp_start, spp,
// origin) — the column K − m, the integer column of the point of the
// caller's lattice nearest the value, AN EXACT TIE GOING RIGHT IN THE
// LATTICE'S COORDINATES.
//
// TIES RIGHT (architect 2026-11-06, session 69): index K owns the quotients
// [K − 0.5, K + 0.5), start included — the point model's own half-open
// convention, a lattice frame the left edge of its pixel (render.cpp's
// render_playhead). WHY A DIRECTED TIE: it is translation-invariant,
// R(x + d) = R(x) + d for a whole d, so a marker on a half-column dragged one
// device px a step at a whole q in source view moves one column a step;
// ties to even is not (an odd d flips the parity), and painted 2, 0, 2 px
// (Astra's determinism audit, 2026-11-06: F = 4623, q = 46, origin 0).
// THE ASYMMETRY IS DELIBERATE AND FORCED (Sol's tie review, the same day):
// with a = R(0.5), translation wants R(−0.5) = a − 1 and reflection wants
// R(−0.5) = −a, so a = 0.5, no column — a translation-consistent rule
// cannot be mirror-symmetric at a tie; right over left is the half-open
// convention's choice, not geometry's (at a two-device-px line either stem
// is half a device px from the point). Ties to even, ties left and a
// drag-only tie rule are not taken (closed_questions.md). Only placement
// takes it: every frame rounding keeps nearbyint.
//
// WHY THE GLOBAL INDEX: the waveform area's pixel columns are the lattice's
// cells, global — column c spans [g(m + c), g(m + c + 1)) — and the plate is
// placed in those columns' coordinates (render.h's THE PLATE'S BARS), so a
// pan at a fixed q and origin translates the plate by whole columns, every
// bar keeping its tips. The viewport-local quotient that stood here,
// nearbyint((displayed − vp_start) / spp), decided a tie (a point on a
// half-column: every odd point of the q = 23 lattice seen at q = 46, an
// ordinary fine-authored marker one level coarser) half to even in the
// SCREEN's coordinates, which an odd m flips, and at a fractional q it read
// the rounded start's residue against any near-half point; so the mark
// picked the left or the right boundary of the same global cell by m's
// parity and beat 0, −2, 0 px against a plate moving −1 px a step (Astra's
// s68 review, the architect's localization question). K is a function of
// the value, q and the origin alone, so under a pan the mark translates
// rigidly with the plate. At a whole q the selector on the local quotient
// would answer the same column (the rule is translation-invariant and the
// start lies exactly on the lattice); at a fractional q they differ only
// for a point within the start's rounding residue (at most 0.5 / q column)
// of a half-column. m is the one recovery every reader of the lattice
// shares (viewport_lattice_index).
//
// One owner for the one rounding — THE COLUMN'S READERS, and what each takes
// of it since the marks are painted at their exact places (architect
// 2026-11-14, send J; displayed_x_at below owns that rule and the list of
// what is painted there; the gates, the hit boxes, the held place and the
// landings left the column with the no-grid arc, 2026-11-15): THE CULLS AND
// THE CAMERAS' TESTS — the flag iterator's right cull
// (iterate_visible_flags_impl, render.cpp), the playheads' and the
// scanner's column (playhead_pixel_x / scanner_pixel_x, main.cpp — the
// ruler's head's cull, the playhead's damage box and RESTRICT's stop), the cameras'
// visibility test (displayed_frame_on_screen below; follow's off-screen-left
// test beside it, viewport.cpp) and
// painted_column_of_source_frame_on_basis's tail (the off-edge authoring
// refusal) all place through this exact expression. PURE ARITHMETIC,
// NO BASIS CHOICE INSIDE: the caller supplies its own vp_start/spp AND THE
// LATTICE ORIGIN OF THE SAME BASIS (plate basis, item basis, or live values —
// the two-epochs split at plate_viewport_basis / item_viewport_basis is the
// caller's to name, at the call site, where the provenance comment belongs;
// the live origin AppState::viewport_lattice_origin, the plate's
// fp_lattice_origin, the item basis's lattice_origin, a wall's 0).
//
// THE UNROUNDED SIBLING IS A DIFFERENT CONCEPT AND STAYS SEPARATE: the strip
// drag's fractional anchor_col wants the SUB-PIXEL position, not a column.
// THE EXACT PLACE is this owner's sibling below, displayed_x_at
// (architect 2026-11-14, send J): the same quotient unrounded — paint's,
// and since the no-grid arc (2026-11-15) the hit geometry's, the ADD SEL
// sweep's membership's and the nudge's held place's too (its own head
// lists them).
//
// NO SECOND SPELLING OF K − m STANDS ANYWHERE: the int64 column walk the
// playhead's step kept beside this owner went with the column step
// (architect 2026-11-15, the nudge one millisecond).
inline int displayed_column_at(double displayed, double vp_start, double spp,
                               int64_t origin) {
    const double m = viewport_lattice_index(vp_start, spp, origin);
    const double k = lattice_index_ties_right(
        (displayed - static_cast<double>(origin)) / spp);
    return static_cast<int>(k - m);
}

// THE EXACT PLACE (architect 2026-11-14, send J: "the anti-aliasing I would
// like to commit to that as well, the full anti-aliasing"; "I would expect
// snapping nothing to integers and leaving them rounding to yield decent
// results"): where a value ALREADY in the displayed domain stands on the
// caller's lattice, A REAL NUMBER OF DEVICE PX from the waveform area's left
// edge —
//     x = (displayed − origin) / spp − m,   m = viewport_lattice_index(
//                                               vp_start, spp, origin)
// — one subtraction, one divide, one subtraction, in that order. A MARK
// PLACED BY TIME IS PAINTED AT ITS x WITH EXACT AREA COVERAGE AND NOTHING IS
// SNAPPED TO A PIXEL: not at a landing, not at the lift, not at whole
// levels; a mark whose x is whole covers whole pixels. x IS THE FRAME'S
// BOUNDARY (the line rule, render.h's waveform_line_px: column c's left edge
// is x = c) AND THE PLATE'S OWN CONTINUOUS COLUMN COORDINATE (render.h's THE
// PLATE'S BARS, the draw transform), so a mark and the bar under it are
// placed by one expression.
// displayed_column_at's SIBLING ON THE SAME QUOTIENT: the column is that
// quotient through lattice_index_ties_right less the same m, so column − 0.5
// <= x < column + 0.5 — the exact place lies within half a px of its column.
// WHAT THE COLUMN STILL OWNS (architect 2026-11-14, "Agree to all
// decisions."; the gates, the published rects, the hit tests and the pen
// acts' landings left it 2026-11-15, the no-grid arc): every CULL and the
// CAMERAS' VISIBILITY TEST (displayed_frame_on_screen) read
// displayed_column_at, and nothing lands on a column: A PEN ACT LANDS ON THE
// FRAME UNDER THE PEN'S REAL PLACE, this expression's inverse
// (displayed_frame_at_x, below), and the arrow step and the nudge move one
// millisecond of frames (HorizontalArrowStep, gui_input.h). THE GATES
// ARE THE EXACT PLACE'S — a line is drawn wherever any of its real interval
// lies inside the strip (render.h's waveform_line_span: its span's own
// emptiness, no column asked), so a line slides in and out at the canvas's
// edge — and THE HIT BOXES STAND ON IT ("yes, I'd like to move to
// fractional hitboxes."): the cues' published rects (paint_cues,
// render.cpp: the label's box, the chip's box and the label's pen off the
// cue's exact x), the ruler head's (paint_ruler_row), the trim's grab zones
// and bridge (trim_endcap_rect, off TrimBoundColumn::x) and the ADD SEL
// sweep's membership (GuiInputHandler::apply_selection_sweep_to), each
// asked with THE POINTER'S REAL PLACE (GuiPointerPlace, input_core.h),
// never the pixel that holds it; this answer feeds paint, the gates, those
// boxes and the damage paint owes.
// PURE ARITHMETIC ON THE CALLER'S BASIS, as displayed_column_at's: a painter
// hands both the same arguments, so a marker and the playhead parked on it —
// one whole displayed frame — answer the same double.
// EVERYTHING PAINTED AT ITS EXACT PLACE, the one list (each through this
// expression on its painter's own basis):
//   ON THE CANVAS, the GL sink's blended rects (canvas_sink.h) — the
//     playhead's line and the scanner (playhead_exact_x / scanner_exact_x,
//     main.cpp), the selection stems (the flag iterator's exact_x, stashed
//     as MarkerStem::x), the zoom anchor's stem (strip_anchor_stem_x
//     below), the lead-in ring's sides and runs
//     (GuiPaintHandler::phase_reset_overlay_band) and the TRIM sweep's
//     highlight (trim_bound_column's x through trim_span_columns);
//   IN THE CHROME'S LANES, the coverage stamp (CoverageStampPass,
//     cool_edit_paint.h) — the cues' chips and the stems' head segments
//     (the same exact_x), the ruler's ticks (RulerComb::tick_x,
//     paint_handler.cpp) and its head (playhead_exact_x), the trim bar's
//     span (trim_span_columns);
//   AND THE TEXT THAT STANDS WITH ITS MARK (architect 2026-11-15, the
//     no-grid arc: the glide's trial of 2026-11-14 is the rule) — a cue's
//     label, its pen the cue's x plus the label's lead (paint_cues,
//     render.cpp), the flag editor's box and text on the same place
//     (render_flag_editor_box), and the ruler's digits, centered on their
//     major's x (paint_ruler_row).
// The canvas's grid and center lines stand at exact ROWS by the same rule
// (render.h's canvas_hline_span); they are placed by no time.
inline double displayed_x_at(double displayed, double vp_start, double spp,
                             int64_t origin) {
    const double m = viewport_lattice_index(vp_start, spp, origin);
    return (displayed - static_cast<double>(origin)) / spp - m;
}

// THE PEN ACTS' ONE CONVERSION — displayed_x_at's INVERSE (architect
// 2026-11-15, the no-grid arc, ruling B: "B, yes, also the exact frame
// there" — a tap's playhead, a drop, a drag, the trim's edges): the
// DISPLAYED frame under a real x (device px from the waveform area's left
// edge) on the caller's lattice,
//     frame = origin + (m + x) · spp,   m = viewport_lattice_index(
//                                           vp_start, spp, origin)
// UNROUNDED, so displayed_x_at(displayed_frame_at_x(x)) == x to floating
// error: a mark dropped under the pen is painted where the pen stood, to the
// whole frame its domain's road then takes — 0.5 / spp px at most. PURE
// ARITHMETIC ON THE CALLER'S BASIS, as its sibling's; every pen act passes
// the ITEM basis (item_viewport_basis: on screen is as painted).
// THE WHOLE FRAME IS THE DOMAIN'S ROAD'S, never taken here: an AUTHORED
// position through authored_frame_at_x_on_basis below (snap_authored_frame,
// the one double-to-authored route; the target view through the displayed
// map at this full precision); the PLAYHEAD's by llrint and
// clamp_playhead_to_live_domain (playhead_frame_at_click_x,
// input_pointer.cpp); the trim drags' active-domain frame by llrint
// (trim_mouse_x_to_active_frame, input_trim.cpp).
// WHAT A WHOLE x ANSWERS: x = c is column c's left edge, the lattice point
// origin + (m + c) · spp, so a pen at a whole pixel coordinate lands the
// frame the column landing gave it wherever that point is a whole frame
// (every whole step: the working zoom and every whole level from it up),
// and the nearest whole frame to it otherwise.
// ITS READERS, the one list: the click family's owner
// (playhead_frame_at_click_x — the tap's playhead, the scrub click, the
// ruler head's drag, the empty-lane double-click's seat, and through the
// playhead every drop), the trim's conversions (trim_mouse_x_to_active_frame
// and its source sibling, the endcap and bridge drags' motion) and
// authored_frame_at_x_on_basis (the trim's bound landing,
// trim_bound_frame_at_x: the bound-set click and the TRIM sweep). The marker
// drag converts no place at its commit: its free proposal is a source frame
// already (marker_drag.cpp).
inline double displayed_frame_at_x(double x, double vp_start, double spp,
                                   int64_t origin) {
    const double m = viewport_lattice_index(vp_start, spp, origin);
    return static_cast<double>(origin) + (m + x) * spp;
}

// IS A DISPLAYED FRAME ON SCREEN, AS PAINTED — THE CAMERAS' ONE VISIBILITY
// TEST (architect 2026-09-28, strictly as painted): true iff the frame's
// painted column, displayed_column_at's one rounding, lies in the waveform's
// columns [0, w) — THE CAMERAS' OWN QUESTION, on the whole column: a frame
// in the view's last half-column rounds to grid point w and is off screen
// to a camera, while one in the half-column left of the start is on, its
// column 0 (a sample-span test, [vp_start, vp_start + w·q), called the
// first on screen and the second off). The painters ask their own: a line
// is drawn wherever any of it is inside the columns (render.h's
// waveform_line_span), so a frame a camera calls off screen at grid point w
// still shows the left half of its line on the last column. PURE
// ARITHMETIC, the basis the caller's: the
// three cameras that ask it — Viewport::reseat_playhead_to's keep-visible
// edge-align, follow_scroll_if_needed and land_subject — pass the live
// viewport, its lattice origin (AppState::viewport_lattice_origin) and the
// live grid step q (painter_samples_per_pixel), the grid their own writes
// land on. `spp` > 0 and `w` > 0 are the caller's. A
// camera's decision, not a guard: it refuses nothing, and every write it
// chooses still passes clamp_viewport_start.
inline bool displayed_frame_on_screen(double displayed, double vp_start,
                                      double spp, int64_t origin, int w) {
    const int col = displayed_column_at(displayed, vp_start, spp, origin);
    return col >= 0 && col < w;
}

// THE ANCHOR STEM'S EXACT PLACE — displayed_x_at clamped into the waveform's
// [0, w − 1] in reals (architect 2026-11-14, send J: the edge clamp, on the
// exact place), the x render_strip_anchor_stem draws its line
// at: an anchor pushed to or past a song edge pins to the edge column's
// place, so the stem draws exactly there, and a clamped place always has
// its line inside the area. Its one reader is
// the stem painter (paint_strip_drag_anchor, on the PLATE basis).
inline double strip_anchor_stem_x(double displayed, double vp_start,
                                  double spp, int64_t origin, int w) {
    double x = displayed_x_at(displayed, vp_start, spp, origin);
    const double last = static_cast<double>(w) - 1.0;
    if (x > last) x = last;
    if (x < 0.0)  x = 0.0;
    return x;
}

// WHERE A SOURCE FRAME IS PAINTED, ON THE CALLER'S BASIS — the column and
// its exact sibling below. NO GESTURE LANDS THROUGH A COLUMN (architect
// 2026-11-15, the no-grid arc: a pen act takes the frame under the pen's
// real place, displayed_frame_at_x above; the nudge is one millisecond,
// position_nudge.h), so these two only READ where a frame stands: the
// column for the off-edge authoring refusal (source_frame_off_right_edge
// below), the exact x for the flag editor's unrolled box (render.cpp), the
// ADD SEL sweep's membership and the nudge's held place.
//
// THE BASIS IS THE CALLER'S, AND EVERY GESTURE PASSES THE ITEM BASIS
// (architect 2026-09-24, strictly as painted: a gesture runs on ONE painted
// basis). The map is the DISPLAYED map (displayed_or_live_target_map) and the
// viewport is its twin, item_viewport_basis (app_state.h) — the vp_start and
// samples-per-pixel the committed flag cache was built against, so the place
// read is the one on screen even while a
// viewport-dispatched worker job is in flight across the press (the freeze
// drops its completion, but the LIVE viewport has already moved). Cold, the
// item basis IS
// the live viewport by its own contract, so nothing here forks on warmth.
// The click-placement family (the nav click, the scrub click, the
// ruler head's drag, the empty-lane double-click create) lands
// active-domain playhead frames through playhead_frame_at_click_x
// (input_pointer.cpp), on the same item basis since 2026-09-24 (architect,
// strictly as painted — the drops at the playhead author at the playhead's
// frame and convert no place); and the playhead's damage columns come from
// playhead_pixel_x (main.cpp) on the basis of the pixels they erase.
//
// painted_column_of_source_frame_on_basis: the pixel column (offset from
// waveform_area(app).x) the stem painters draw `source_frame` at on the
// caller's basis, computed with the painters' own math (the marker-stem
// overlay paint_marker_stems): nearbyint the frame; in the TargetLive domain
// forward-map it through `warp_frame_map` and nearbyint the map output; then
// place it on the basis's lattice through displayed_column_at — its nearest
// lattice index at the basis's samples-per-pixel (`spp` — the painted span
// over the strip width, which is the sixteenth-frame q exactly, the
// painters' own) and lattice origin (`lattice_origin`) less its viewport
// start's (`vp_start`) own index. `warp_frame_map` is the map the
// item is painted through — the DISPLAYED map at every caller. Ignored in the
// Source domain; an empty map in a mapped domain falls back to identity,
// exactly like paint. The flag EDITOR's unrolled box passes the ITEM basis
// too, to the exact sibling — damage follows the pixels it erases, so the
// box unrolls from the place the flag pixels were painted at even
// mid-publish. (Two other
// ITEM-basis callers died in row 5 with the marker-text lane: the run
// centering and the run hit. The selected-stem invalidator was an _on_basis
// caller until 2026-07-30; it is gone with the singleton selected-marker stem
// itself.) `spp` must be > 0 (returns 0, a valid column, on a degenerate spp —
// callers guard the geometry). The domain and the source->target mapping do
// not depend on the viewport.
int painted_column_of_source_frame_on_basis(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    double vp_start, double spp, int64_t lattice_origin);
// THE SAME FRAME'S EXACT PLACE on the same basis (displayed_x_at, device px
// from the waveform area's left edge): the place the cue pass paints the
// marker's chip, label and stem at — the column's sibling on one displayed
// frame, the two a function of the same arguments. Its reader is the flag
// EDITOR's unrolled box (render.cpp), which stands on the marker's exact
// place as its resting label does (architect 2026-11-15, the no-grid arc).
// 0 on a degenerate spp.
double painted_x_of_source_frame_on_basis(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    double vp_start, double spp, int64_t lattice_origin);

// WOULD A MARKER AT `source_frame` PAINT PAST THE LAST COLUMN AT THIS ZOOM —
// the one owner of the off-edge authoring refusal (architect 2026-09-26,
// strictly as painted: a marker may not be authored where its column would
// not paint). Such a marker is not invisible — its flag's LEFT BORDER stands
// outside the fill on the column's left, so at grid point w it shows that
// border alone on the last column(s) (the flag iterator's cull and the lane's
// clip to [0, w), render.cpp) — but it has no stem and no column of its own,
// and a border is not the marker's column, so this refusal stands. Frame->column
// is nearest-grid-point, so a frame in the domain's
// last half-column rounds to grid point w, one past the last column (w − 1),
// and AT THE RIGHT WALL the view cannot pan further: at the current zoom no
// viewport paints it (End's landing at the whole-song zoom does this in the
// ordinary case; a file short enough that the whole-song fit saturates at
// the floor occupies less than the window, so its last frame paints inside and
// is correctly not refused).
// Mid-song the same rounding is ordinary culling — the camera pages one
// column and the item is in view — so the test is asked of THE RIGHT-WALL
// VIEWPORT, max_viewport_start_grid (main.cpp, the one right-wall owner), the
// rightmost start any camera can rest at and so the one that paints a late
// frame furthest left: true iff the frame's painted column there, through
// the painters' own placement (painted_column_of_source_frame_on_basis, the
// displayed map, the live grid step q) is >= the waveform's width. It reads
// the zoom and the frame, never the camera's current place — nor its lattice:
// the walls are the frame-0 lattice's at every origin (a rest the walls clamp
// takes origin 0, AppState::viewport_lattice_origin), so a rest on
// any other lattice lies inside them and this wall is still the rightmost
// (architect 2026-10-31). THE LEFT EDGE
// NEEDS NO TWIN: the leftmost start is 0, where frame 0 is column 0 and every
// authored frame is >= 0, so no frame paints left of column 0 at every
// viewport. Degenerate geometry answers false (the callers' own geometry
// belts refuse first). TWO ACTS READ IT: the position nudge's landing
// (marker_nudge_verdict, position_nudge.cpp, and the twins' group arm) and
// the drop at the playhead (drop_copy_previous_at_playhead and
// drop_phase_reset_lead_in_at_playhead, the `s` / Shift+S roads). The load is
// not one of them: whether a marker paints depends on the zoom, not on the
// data, so a marker in the last half-column stays loadable (the two-category
// rule) and is reachable by zooming in or by the Tab walk. So the refusal is
// input routing, never an adversarial class: it judges no data and no render
// stage sees it. Its error arm has a producer (a Right press in the song's
// last columns, End then `s` at the whole-song zoom; and, since the pen acts
// take the frame under the pen's real place, a tap in the strip's last half
// px at the right wall, whose playhead a drop then authors at); the marker
// drag needs no copy, its visible-strip clamp stopping at the last column's
// left edge (viewport_marker_bounds).
bool source_frame_off_right_edge(const AppState& app, const GuiAudio& audio,
                                 int64_t source_frame);

// authored_frame_at_x_on_basis: THE AUTHORED SOURCE FRAME UNDER A REAL x
// (architect 2026-11-15, the no-grid arc: a pen act takes the frame under
// the pen's real place), on the caller's basis and map — the DISPLAYED
// map and the ITEM basis at every caller: the displayed frame is
// displayed_frame_at_x, unrounded; in the Source domain it returns through
// snap_authored_frame as it is; in the TargetLive domain it is floored at 0
// and inverse-mapped through `warp_frame_map` AT ITS FULL PRECISION (the map
// takes a real: no whole target frame is taken on the way, so the one
// rounding is the authored one), then snap_authored_frame. A whole source
// frame in the authored int64 domain; callers apply their own walls AFTER.
// Returns 0 on a degenerate `spp`. ITS READER: the trim's bound landing
// (trim_bound_frame_at_x, input_trim.cpp).
int64_t authored_frame_at_x_on_basis(
    const AppState& app, const GuiAudio& audio, double x,
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    int64_t vp_start, double spp, int64_t lattice_origin);

// -- THE PHASE-RESET LATTICE (the engine's seed geometry, GUI-side) ---------
//
// These four live here, beside the map functions they read, because the
// question they answer is a MAP question: where the engine's analysis lattice
// falls in source frames under the map a render would use. They were the
// painter's alone until 2026-09-09, when the phase-reset column got its own
// iteration bracket and the walls, the editor's refusal, the arrows' landing
// and the sweep all had to ask the same lattice the overlay band paints; the
// P column's Left / Right, whose unit is a hop (2026-09-21), asks it too.

// THE ENGINE'S SEED FRAME FOR A RESET, mirrored in the GUI (2026-09-02): the
// schedule index m the engine seeds at for a reset authored at source frame
// `reset_source_frame` under `map` — an EMPTY map is the identity, as it is
// for the map functions themselves. It restates engine.cpp's pass 1 (the
// last schedule entry <= the parser's `S - N/2`) over stft_container.h's
// schedule (generate_source_frame_positions: positions[m] =
// llrint(map_target_to_source(m*R_s) - N/2), half-to-even), and it MUST STAY
// IN LOCKSTEP with both — a change to either is a change here. The compare
// is in SOURCE terms exactly as the engine's is; floor(T/R_s) in the target
// domain is NOT the rule (the schedule rounds in source terms, and a
// piecewise map can put the two on different sides of a boundary). The
// condition is monotone in m (the map is monotone), so the search starts at
// floor(T/R_s) and steps until it flips — a few iterations at most. m = 0
// always qualifies (positions[0] = -N/2 <= S - N/2 for every authored S >= 0),
// so the engine's before-the-first-frame drop has no twin here. The schedule's
// END is not applied: a reset past the map's final anchor, which the parser
// drops from participation, still paints its band as it paints its flag.
int64_t phase_reset_seed_frame_index(
    int64_t reset_source_frame,
    const std::vector<WarpFrameMapSegment>& map);

// THE SOURCE-FRAME CENTRE of schedule window `m` under `map`:
// llrint(map_target_to_source(m*R_s) - N/2) + N/2, the schedule's own rounded
// window START plus the window's half-width. It is the quantity the seed
// search above compares against — seed(S) = max{ m : centre(m) <= S } — and
// the seed function is written THROUGH it so the two cannot drift. Monotone
// in m, because the map is.
int64_t phase_reset_window_centre_frame(
    int64_t m, const std::vector<WarpFrameMapSegment>& map);

// THE HOP STEP'S AUTHORED FRAME: where a phase reset resting at source frame
// `S` lands `k` hops away, under `map`. ONE OWNER FOR BOTH ROADS THAT MOVE A
// RESET BY HOPS — the iteration cells (k in [-kIterHopMax, +kIterHopMax]) and
// the P column's Left / Right, whose unit is a HOP (k = +/-1, reached through
// position_nudge_landing's hop arm, position_nudge.h) — because two meanings
// of "one hop" on one column would be an asymmetry.
//
// THE RULE IS TRANSLATION (architect 2026-09-21, superseding the
// minimum-displacement rule of 2026-09-09, under which a cell landed on the
// near end of the window k hops away): the reset's target image moves by
// exactly k hops, t' = map_source_to_target(S) + k * kRs, and the landing is
// the authored frame of map_target_to_source(t') through snap_authored_frame
// (the one double-to-authored route, banker's rounding). The reset's offset
// inside its window travels with it, so Right then Left returns it
// to where it started, give or take a frame of rounding — the minimum rule
// threw that offset away and the round trip landed up to a hop off. The phase
// cells had never been used, so nothing was lost by their following.
//   k == 0  ->  S itself, byte for byte (the identity cell renders the resting
//               store; no arithmetic, so no rounding moves it).
//
// THE MEANING WINS OVER THE ROUNDING: a step of k must change the seed window
// by exactly k. With C(m) the window centre above and m0 = seed(S), the frames
// seeding at m0 + k are [C(m0+k), C(m0+k+1) - 1], and the rounded landing is
// clamped into that interval. That clamp bites only in the rare case where a
// landing sits within a frame of a window boundary; the round trip is then off
// by a frame, which the architect accepts.
//
// THE PIECE'S EDGES ARE THE CALLERS' WALLS, and the owner answers so they can
// close: m0 + k < 0 has no frame of the piece seeding there (every authored
// frame seeds at m >= 0), so the owner answers -1, the frame just below the
// piece; a landing past the map's last anchor extrapolates on the identity
// slope the map functions use there and may answer a frame past
// total_frames - 1. The cell walls (phase_reset_hop_window below) close a side
// at the first k that leaves [0, total_frames - 1]; the hop step clamps onto
// that same wall, the unified wall policy (position_nudge.h).
//
// THE LATTICE IS THE MAP'S, AND THE MAP EVERY CALLER PASSES IS THE LIVE ONE
// (live_warp_frame_map below) — the map the render uses, and the map the
// sweep's own cells render under. A sweep is ONE COLUMN'S since 2026-09-10, so
// a phase sweep rewrites no tempo and moves no lattice: the displacement is
// computed under the same map the walls were checked under and every cell
// renders under, once per reset per k. The hop step asks the same map, because
// a hop is the RENDER's quantum and not a painted one — it makes no pixel
// claim. Each landing is an ordinary whole authored frame and
// re-parses normally.
int64_t phase_reset_hop_step_frame(
    int64_t reset_source_frame, int k,
    const std::vector<WarpFrameMapSegment>& map);

// THE MAP A CELL IS COMPUTED UNDER — the LIVE target-view map, memoized on the
// warp store's generation (target_view_warp_frame_map_cached above), empty on
// a failed build, which the map functions read as the identity. ONE ACCESSOR
// so the wall verdict, the bound editor's refusal, the arrows' landing and the
// sweep cannot each pick a different map.
//
// IT IS NOT THE DISPLAYED MAP (displayed_or_live_target_map): that one exists
// so painted items stay locked to the blitted plate through a worker publish
// window, and the overlay band reads it for exactly that reason. A cell is
// about the RENDER, not the picture, so it takes the map the render would use.
// Same single-threaded reference lifetime as the cache accessor it wraps.
const std::vector<WarpFrameMapSegment>& live_warp_frame_map(
    const AppState& app, const GuiAudio& audio);

// THE WARP DROP'S OCCUPIED PLACE, ONE OWNER (architect 2026-11-11, THE DROP
// COINCIDENCE RULE: "coincident (exact frame) warp marker drops are never
// intentional"; architect 2026-11-15, the no-grid arc, the place widened to
// the vicinity: a drop inside it is refused "as a drop on a marker's exact
// frame is today ... So a strike arises only where a drag or a nudge brings
// two markers within the vicinity"; architect 2026-11-17, the coincidence
// the parser's): does a marker of the store — enabled or disabled, his
// "agree" — coincide with `source_frame`, the frame a drop would author, by
// the parser's predicate (warp_frames_coincide, warp_coincidence.h: the same
// frame or under one millisecond). A marker is struck exactly where this test
// would refuse a drop onto its place (warp_red_flag_set_cached's pass 1,
// above, the same predicate over the store's neighbors), so no drop makes a
// strike. COST, because the Drop key's face asks every tick: the store is
// ascending by time_frame, so one lower_bound and its two store neighbors'
// distances. THE READER: the at-playhead drop's answer on the warp column
// (drop_at_playhead_coincides, app_state.h — the warp drop body and the Drop
// key's plain term). NOT ASKED BY: the nudges, the drags, the pastes and the
// bound cells, which may still author a coincidence, which the strike then
// shows and the render normalizes to 1.00. THE PHASE-RESET COLUMN ASKS
// NOTHING: anything goes there (architect 2026-11-17, the note above at the
// red-flag set).
bool warp_marker_in_vicinity_of(const AppState& app, int sample_rate,
                                int64_t source_frame);

// THE LEGAL HOP INTERVAL for the phase reset at `idx`. k_min <= 0 <= k_max
// ALWAYS: the identity cell renders the resting store, so 0 is inside the
// window whatever else is around, exactly as the warp bracket's clamp window
// always contains the zero delta.
//
// THE INTERVAL IS THE WHOLE ANSWER, and there is no wall-KIND beside it: both
// readers want the numbers — the editor refuses a value outside them and the
// step clamps into them — and neither has ever forked on which wall closed a
// side. (A per-side wall-kind enum rode this struct while the editor's refusal
// spelled a different sentence per wall; the sentence collapsed into one and
// the enum lost its last reader with it. The two walls themselves are not
// gone — they are what the walk below closes each side on, and the DIGIT
// ceiling survives exactly as the numeric bound the step clamps at.)
struct PhaseHopWindow {
    int k_min = 0;
    int k_max = 0;
};

// THE PHASE BRACKET'S WALLS, ONE OWNER. Walks k outward from 0 on each side
// under the live map, landing each candidate through
// phase_reset_hop_step_frame, and stops at the first k that breaks a wall — at
// most kIterHopMax steps a side. THE WALLS ARE TWO:
//
//   THE PIECE: 0 <= F <= total_frames - 1, the drop's own EOF wall
//   (drop_phase_reset_at_position, phaseresetmarkers_ops.cpp).
//
//   THE DIGIT: +/-kIterHopMax, "past nine it is no longer the phase reset it
//   was" (phaseresetmarkers.h).
//
// A NEIGHBOURING RESET IS NOT A WALL (architect 2026-09-11): two adjacent
// ranges may CROSS OR MEET. The neighbour walls that stood from 2026-09-09 are
// retired — they were order-dependent (each neighbour walled at its bracket's
// own extreme landing while the walks checked one side each, so a neighbour
// that moved its bound away vacated space a range could enter and its later
// retreat or clear left the crossing behind), and the answer is no wall rather
// than a tighter one: iteration is wanted in quiet, exposed sections with few
// resets, and a user who sets two ranges within a few hops of each other is
// doing it deliberately — the ear catches the contortion, the same latitude
// coincident markers have at rest, with git and the session history the way
// back. A cell where two enabled resets land on ONE frame renders them as one
// reset (the parser's exact-coincidence collapse and its stderr line, what two
// resets resting on one frame get); a cell where they CROSS renders both where
// the cells put them, in swapped order — which is why the SWEEP SORTS each
// cell's vector by time_frame before the request (input_key_dispatch.cpp): the
// store's sort is the invariant every consumer reads, the load parser
// rejecting decreasing times and the engine's phase reset validator refusing a
// list that is not strictly ascending.
//
// AND THE WINDOW CANNOT MOVE UNDER A STANDING BRACKET (architect 2026-09-10):
// a bracket exists only while grid iterations is lit, and while it is lit the
// piece is LOCKED (authoring_locked, app_state.h), so the piece's length
// cannot change and no warp edit can move the lattice — the two facts these
// walls are made of. That is what retired the sweep plan's per-read
// re-verification, which existed because nothing clamps this bracket
// retroactively: the walls hold by construction now.
//
// ONE READER since 2026-09-19: phase_iter_bound_tie_window (app_state.h),
// which INTERSECTS these windows across a tie's members — one sweep
// cell displaces every tied reset by the same hop count, so a bound must fit
// them all. The two roads onto a hop bound reach it through that intersection
// rather than through this: the bound editor's commit REFUSES outside
// the tie's window (GuiFlagEditor::commit_phase_iter_bound_edit, which names
// THE PIECE EDGE outright, that being the only wall a COMMITTED value can
// break: the phase grammar is a sign and one digit under a two-byte field
// cap, so what arrives is already inside the digit wall) and the step's
// landing owner CLAMPS into it and then at the partner
// (phase_iter_bound_step_landing, app_state.h, the one road the DIGIT wall
// closes), the directional face reaching it through that landing. An UNTIED
// reset walks one member, so both roads still see exactly this answer.
PhaseHopWindow phase_reset_hop_window(const AppState& app,
                                      const GuiAudio& audio, int idx);
