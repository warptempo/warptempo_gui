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
// flags paint the row-5 red class (kMarkerFlagFillRed / kMarkerFlagEdgeRed at
// rest, the Sel pair on a selected marker's addressed cell since 2026-09-16,
// stem kMarkerStemRed at rest and the bright fill selected) — the swap moves
// the class's BRIGHTNESS and never its
// hue, so the cue is never masked. `red` is a PAINT cue with two meanings: the
// render normalizes this marker, OR this marker shares its frame with another.
// Three contributors, all computed SILENTLY from the display path (no resolver
// run, no stderr, no frozen-parser dependency): (1) the exact-frame COLLAPSE —
// a marker sharing its frame with 2+ effectively-enabled markers
// (marker_effectively_disabled for the enabled test, matching the render's
// survivor filter), every member reddened, so a coincident stack reads as one
// red flag mirroring the render's single stderr line; (2) a LABEL-REF fallback
// via marker_effective — a dangling label ref or an extreme-ratio label ref,
// both of which resolve to source_idx == -1 (a pass never normalizes: its walk
// skips refs to the owner behind them, architect 2026-09-13); (3) the PARTICIPATION-BLIND COINCIDENCE
// (architect 2026-09-13: coincident markers are never intentional, always
// accidental, so the red stays) — every row of a run of 2+ rows at one frame,
// DISABLED ROWS COUNTED, so disabling one of two coincident markers does not
// put the red out; the render collapses nothing there, and (3) is the one
// contributor that is not a render normalization. (3) contains (1), which is
// kept as its own step because `collapsed` below is its verdict. The frame-0
// seed is synthetic (no marker) and never reddens.
//
// WIDENING THE CUE WIDENS NO REFUSAL: no act, face or card reads `red`. The
// act and face readers that ask whether the render normalizes a marker read
// `collapsed` (the one normalization that is not already walled on the
// marker's kind — pass-2 reddens only refs), so `red` has only
// painters for readers: the three flag passes (waveform_cache.cpp's W, P and M
// arms, each over its own column's cache) and the open marker-lane field's
// face (render.cpp).
//
// Keyed on the warp store generation plus the audio identity (total_frames
// feeds marker_effective's last-segment envelope distance). It reads the
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
    std::set<int> red;   // red warp-marker store indices — PAINT readers only
    // THE COLLAPSE MEMBERS ALONE — pass 1's own subset of `red`, the
    // classifier's verdict (warp_coincident_collapse_members) kept apart from
    // the pass-2 fallbacks and the pass-3 coincidence it is unioned with
    // above. This is the RENDER-TRUTHFUL answer the act and face readers
    // take: the value pair's gate (payload_eligibility, app_state.cpp)
    // refuses a pass or a ref that sits in a collapsed stack — the composer
    // resolves such a member against the RAW store, the ruled
    // authored/display split, so its value is not what the render applies —
    // while the pass-2 fallbacks it must NOT refuse on resolve against the
    // projection and so already read out as the render's own 1.00 (or as the
    // empty payload); the group tempo step's wall, the singleton step's kind
    // refusal and the BPM sweep's owner refusal ask it too. Like pass 1 itself
    // it marks the WHOLE raw run, disabled rows included, once 2+ effectively
    // enabled rows share the frame, so a reader that means "a stack member for
    // the render" composes it with the enabled test. Filled in the same pass
    // that fills `red`, under the same key; no second computation anywhere.
    std::set<int> collapsed;
};

// Returns the warp red-flag cache entry for the app's live warp store,
// rebuilding only when the key does not match. Same single-threaded-reference
// lifetime rule as target_view_warp_frame_map_cached; the flag cache reads the
// set at build time (not per paint).
const WarpRedFlagCache& warp_red_flag_set_cached(
    const AppState& app, int sample_rate, long total_frames);

// Phase-reset sibling (the now-resolved naming symmetry): a group of 2+ phase
// resets sharing one exact frame reddens every member, WHATEVER THEIR DISABLED
// BITS — the warp set's participation-blind coincidence (3) on this column
// (architect 2026-09-13: coincidence is never intentional). It contains the
// render normalization, build_phase_reset_source_frames' exact-equal collapse
// of 2+ ENABLED resets (one stderr line per group at render); phase resets
// carry no tempo, labels, or inheritance, so collapse is their ONLY
// normalization — there is no marker_effective analog. No `collapsed` subset
// is kept here, asymmetric with the warp set by reader count: `red`'s one
// reader is the phase-reset flag pass, a painter, and no act or face on this
// column asks whether the render collapses a reset. Keyed on the phase-reset
// store generation alone (the same-frame count is independent of sample rate
// and length); the same committed-store / drag-freeze rule as the warp set.
struct PhaseResetRedFlagCache {
    bool      valid       = false;
    long long markers_gen = -1;
    std::set<int> red;   // red phase-reset store indices — PAINT readers only
};

const PhaseResetRedFlagCache& phase_reset_red_flag_set_cached(
    const AppState& app);

// WHETHER THE WAVEFORM PICTURE IS MAGNIFIED, AND THE ONE GAIN GATE. The gain
// itself is the continuous curve derived from the source at load — the
// short-term loudness leveler, then the downward expander (GuiAudio::gain_curve,
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
// frames through the warp map (render_waveform), so a target-view column
// shows the gain and the expander of the source audio it draws.
//
// THE MAGNIFICATION LAMP IS THE ONLY EXCEPTIONS ROAD (architect 2026-09-23): no
// per-passage override, no third state and no drawing of the gain. A passage
// the derived gain serves badly is looked at flat, and the dynamics are what
// the audio and the A/B tabs carry — both tabs always show the same picture,
// the curve being a function of the one source. The drawing excluded is a
// plotted curve, a tint over the picture or a colour read from the gain: the
// ghost's per-column shade, which read the leveler's gain for one day, is
// struck (architect 2026-09-25, render_waveform) and the ghost is one flat
// colour again.
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
// rule's hard-coded constants (waveform_gain.cpp), the version alone names it (nothing derived from the gain is
// persisted across launches; the record is at kWaveformGainVersion). ONE PLACE, so the picture caches' existing hash keys
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
// proposed (pre-commit) marker list. Those use the explicit-map pixel-anchoring
// helpers (painted_column_of_source_frame_on_basis /
// authored_frame_at_column_on_basis) with their own map.
int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame);
int64_t active_domain_to_source_frame(const AppState& app, const GuiAudio& audio,
                                      int64_t domain_frame);

// (A CONTEXT-PARAMETERIZED FORWARD FACE, source_frame_to_domain, and
// proposed_display_context — the context a state that is NOT live would
// display, over a two-slot memo of proposed target maps — stood here from
// 2026-09-04 to 2026-09-22 for one reader, the Restrict undo to viewport lamp,
// which measured a restore's touched markers under the map the restore would
// install. The lamp asks the entry's view tags alone since 2026-09-22 and all
// three went with it; source_frame_to_active_domain above has its one body
// again.)

// The stem painters' samples-per-pixel and the single source of truth for the
// on-screen column grid: the visible span nearbyint-quantized to whole samples
// (matching the vp_end the waveform cache carries, vp_start +
// nearbyint(spp * area.w)) divided back over the strip width. The viewport
// snap in clamp_viewport_start (main.cpp) and the plate's dispatch take their
// `q` from here, and the pixel-anchoring pair below and the click placement
// (architect 2026-09-24) take the item basis's spp, which is this same
// quantization of the span the flags were built against, so the viewport grid and the marker grid are one
// grid at any window width (not just multiples of 8). Returns 0.0 on
// degenerate geometry (no strip width / no zoom).
struct GuiRect;
double painter_samples_per_pixel(const AppState& app, const GuiAudio& audio,
                                 const GuiRect& area);

// THE QUANTIZATION ITSELF, for a samples-per-pixel the caller already holds:
// nearbyint(spp * w) / w, 0.0 on degenerate geometry. painter_samples_per_pixel
// is this at the LIVE level.
inline double painter_quantized_spp(double spp, int w) {
    if (w <= 0 || !(spp > 0.0)) return 0.0;
    return std::nearbyint(spp * static_cast<double>(w)) /
           static_cast<double>(w);
}

// THE VIEWPORT GRID'S k-TH POINT at painter step q: nearbyint(k * q) — the
// resting starts clamp_viewport_start snaps to and max_viewport_start_grid
// walks (main.cpp).
inline int64_t viewport_grid_point(int64_t k, double q) {
    return static_cast<int64_t>(std::nearbyint(static_cast<double>(k) * q));
}

// Viewport-END sample for a strip `w` px wide at samples-per-pixel `spp`:
// vp_start + nearbyint(spp * w), the painter-quantized right anchor the plate,
// the flag/trim hit tests, and the trim column math all derive their upper
// bound from. One owner so every viewport-END derivation rounds the span
// identically (the twin of painter_samples_per_pixel's forward direction).
inline int64_t viewport_end_sample(int64_t vp_start, double spp, int w) {
    return vp_start +
        static_cast<int64_t>(std::nearbyint(spp * static_cast<double>(w)));
}

// THE ONE COLUMN->FRAME LANDING for a grid-snapped viewport: the exact
// DISPLAY-DOMAIN grid position at pixel column `col`. Recover the viewport's
// column index m = nearbyint(viewport_start/q), round ONCE, and return the
// double (m+col)*q. A marker commit funnels this through snap_authored_frame;
// the playhead landings llrint/nearbyint it. Caller supplies q>0 (painter spp)
// and a `viewport_start` in the SAME domain as `q` — the arithmetic is
// domain-blind, so source view and target view both land through it, each in
// its own active display domain.
//
// SINGLE ROUNDING, AND THE TWO-ROUNDING SIBLING IS BANNED: the form
// viewport_start + nearbyint(col*q) rounds twice (once into the viewport start,
// once into the column offset), each with its own independent half-frame error,
// so its landings are anchored to the CURRENT viewport start rather than to
// frame 0 — a pan or a zoom round trip back to the same level relabels the same
// painted column by +/-1 frame. This form is anchored at frame 0 of the
// displayed domain and is viewport-phase-independent, so a landing means the
// same sample whatever the camera did on the way there. That matters past
// display: both drop-at-playhead routes commit the playhead's sample into
// authored data, so the playhead lattice is an authoring lattice too.
//
// The choice is invisible in paint: both forms sit within one frame of the
// ideal grid point (m+col)*q, and one frame is at most ~1/27.5 px at the
// deepest numeric zoom — far under the half-pixel paint-rounding threshold — so
// a landing paints at column `col` either way.
//
// m recovery is exact for product-reachable audio lengths: at the deepest
// numeric zoom q >= ~27.5 frames/px and a source length fits well within the
// double mantissa, so |viewport_start/q - m| << 0.5. The target domain's total
// is at most 16x the source's — build_warp_frame_map divides each source delta
// by the product of tempo, marker scale and settings scale, and all three
// floors are legal at once (0.25 * 0.5 * 0.5 = 1/16, value_format.h), so the
// bracket bound is 16x rather than the tempo bracket's 4x alone. A RIFF source
// caps under 2^32 bytes, i.e. under ~1.1e9 frames at 16-bit stereo, so even a
// 16x target total is ~1.7e10 — four orders of magnitude inside the 2^53
// mantissa, and the argument holds in that domain too. It is NOT claimed exact
// over the whole int64 range.
inline double displayed_grid_position_at_column(int64_t viewport_start,
                                                int64_t col, double q) {
    const double m = std::nearbyint(static_cast<double>(viewport_start) / q);
    return (m + static_cast<double>(col)) * q;
}

// THE PAINTERS' COLUMN PLACEMENT for a value ALREADY in the displayed domain:
// nearbyint((displayed - vp_start) / spp), rounded once to the integer column.
// One owner for the one rounding — the trim-bound column, the region span, the
// phase-reset overlay's left edge, the strip-drag anchor stem, the undo
// restore's visibility test and painted_column_of_source_frame_on_basis's tail
// all place through this exact expression, and used to spell it independently,
// tied together only by "matching region_columns"-style prose. PURE ARITHMETIC,
// NO BASIS CHOICE INSIDE: the caller supplies its own vp_start/spp (plate
// basis, item basis, or live values — the two-epochs split at
// plate_viewport_basis / item_viewport_basis is the caller's to name, at the
// call site, where the provenance comment belongs).
//
// THE UNROUNDED SIBLINGS ARE A DIFFERENT CONCEPT AND STAY SEPARATE:
// playhead_pixel_x / scanner_pixel_x (main.cpp) and the strip drag's fractional
// anchor_col want the SUB-PIXEL position, not a column, and the flag painter
// (iterate_visible_flags_impl, render.cpp) keeps its nearbyint in the double
// pixel domain (its left_x feeds shaped-text placement, never an int).
//
// THE ONE INT64 SIBLING IS ALSO EXEMPT AND COUNTED (2026-08-22):
// playhead_pixel_step_landing (viewport.cpp) spells this same recovery expression
// inline in the INT64 domain — its column walk composes with
// displayed_grid_position_at_column's int64 col and cannot take this owner's
// int return — and is recorded at its own site ("the recovery nearbyint is
// the column direction and is this walk's own"). It is the exempt list's
// whole int64 class; a second int64 spelling would be the fork this owner
// exists to prevent.
inline int displayed_column_at(double displayed, double vp_start, double spp) {
    return static_cast<int>(std::nearbyint((displayed - vp_start) / spp));
}

// THE ANCHOR STEM'S PAINTED COLUMN — displayed_column_at clamped into the
// waveform's [0, w-1], the column render_strip_anchor_stem draws. Its one
// reader is the stem painter (paint_strip_drag_anchor, on the PLATE basis).
inline int strip_anchor_stem_column(double displayed, double vp_start,
                                    double spp, int w) {
    int col = displayed_column_at(displayed, vp_start, spp);
    if (col >= w) col = w - 1;
    if (col < 0)  col = 0;
    return col;
}

// Pixel-anchoring pair for gesture commits. Every gesture that moves an
// authored position by pixel columns (the bare Left/Right nudges on both
// marker columns) or releases one at a
// pointer position (marker AND trim drag commits both snap their release to
// the painted column) anchors to the on-screen column grid through these two
// helpers: read the item's
// currently painted column with painted_column_of_source_frame_on_basis, pick
// the destination column, and commit authored_frame_at_column_on_basis of it —
// which funnels through snap_authored_frame (app_state.h), the single
// fractional-to-authored route. Anchoring to the column grid re-derives
// the pixel phase on every gesture, so whole-frame rounding residue can
// never accumulate on top of an off-grid sub-pixel phase; the painted
// move is exactly the commanded number of columns, and the stored value
// is the whole frame the painting already shows. Exactly ONE item moves per
// gesture: a nudge steps the FOCUS (a 2+ selection collapses to it first — groups
// are never moved, the doctrine at the head of position_nudge.h) and a drag
// moves the marker it grabbed.
//
// THE BASIS IS THE CALLER'S, AND EVERY GESTURE PASSES THE ITEM BASIS
// (architect 2026-09-24, strictly as painted: a gesture runs on ONE painted
// basis). The map is the DISPLAYED map (displayed_or_live_target_map) and the
// viewport is its twin, item_viewport_basis (app_state.h) — the vp_start and
// samples-per-pixel the committed flag cache was built against, so the column
// read and the column committed are the ones on screen even while a
// viewport-dispatched worker job is in flight across the press (the freeze
// drops its completion, but the LIVE viewport has already moved; converting on
// it would author on a grid the pixels never showed). Cold, the item basis IS
// the live viewport by its own contract, so nothing here forks on warmth. The
// gesture callers — the marker drag's commit snap (marker_drag.cpp), the trim
// drags' release snap, the sweep's trim half and the trim bar's bound-set
// click (input_trim.cpp), and both nudges' column step (stepped_anchor_frame,
// position_nudge.h) — are the whole caller list of the pair apart from the
// flag editor's unrolled box (render.cpp), which rides the same item basis on
// the paint side. THE LIVE-VIEWPORT FORMS ARE DELETED (2026-09-24): with the
// gesture class moved no caller was left on them. Two families never reached
// this pair: the click-placement family (the nav click, the scrub click, the
// sweep's PLAYHEAD half, the empty-lane double-click create) lands
// active-domain playhead frames through playhead_frame_at_click_column
// (input_pointer.cpp), on the same item basis since 2026-09-24 (architect,
// strictly as painted — the drops at the playhead author at the playhead's
// frame and convert no column); and the playhead's damage columns come from
// playhead_pixel_x (main.cpp) on the basis of the pixels they erase.
//
// painted_column_of_source_frame_on_basis: the pixel column (offset from
// waveform_area(app).x) the stem painters draw `source_frame` at on the
// caller's basis, computed with the painters' own math (the marker-stem
// overlay paint_marker_stems): nearbyint the frame; in the TargetLive domain
// forward-map it through `warp_frame_map` and nearbyint the map output; then
// divide by the basis's samples-per-pixel (`spp` — the visible span
// nearbyint-quantized to whole samples over the strip width, the painters'
// own) from its viewport start (`vp_start`) and round with the painters'
// std::nearbyint through displayed_column_at. `warp_frame_map` is the map the
// item is painted through — the DISPLAYED map at every caller. Ignored in the
// Source domain; an empty map in a mapped domain falls back to identity,
// exactly like paint. The flag EDITOR's unrolled box passes the ITEM basis
// too — damage follows the pixels it erases, so the box unrolls from the
// column the flag pixels were painted at even mid-publish. (Two other
// ITEM-basis callers died in row 5 with the marker-text lane: the run
// centering and the run hit. The selected-stem invalidator was an _on_basis
// caller until 2026-07-30; it is gone with the singleton selected-marker stem
// itself.) `spp` must be > 0 (returns 0, a valid column, on a degenerate spp —
// callers guard the geometry). The domain and the source->target mapping do
// not depend on the viewport.
int painted_column_of_source_frame_on_basis(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    double vp_start, double spp);

// authored_frame_at_column_on_basis: the authored source-frame value of pixel
// column `col` under the same coordinate system and the caller's basis — the
// active-domain time is displayed_grid_position_at_column above at the
// basis's viewport start (`vp_start`, the integer frame — the grid owner
// recovers its column index from it) and samples-per-pixel (`spp`), in EVERY
// domain; in the TargetLive domain that time is quantized to an integer target
// frame (llrint, floored at 0 — the same quantization the target-view nudges
// have always applied) and inverse-mapped through `warp_frame_map` at full
// precision. The result returns through snap_authored_frame, so it is a whole
// source frame in the authored int64 domain; callers apply their own walls
// AFTER — the walls win over the pixel grid, and every wall is itself an
// integer frame. Returns 0 on a degenerate `spp` (callers guard the geometry).
int64_t authored_frame_at_column_on_basis(
    const AppState& app, const GuiAudio& audio, int col,
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    int64_t vp_start, double spp);

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
// claim (the one-column-per-press guarantee at stepped_anchor_frame is the
// column step's alone). Each landing is an ordinary whole authored frame and
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
// reset (the parser's exact-coincidence collapse and its stderr line, what a
// coincident drop at rest gets); a cell where they CROSS renders both where
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
