#pragma once

#include "engine_settings.h"      // EngineSettings (the proposed-context signature)
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
// render resolves to the 1.00 normalization fallback, so their flags paint the
// row-5 red class (kMarkerFlagFillRed / kMarkerFlagEdgeRed, stem
// kMarkerStemRed) regardless of selection — red takes no selection swap, so the
// normalization cue is never masked. Two contributors, both computed
// SILENTLY from the display path (no resolver run, no stderr, no
// frozen-parser dependency): (1) the exact-frame COLLAPSE — a marker sharing
// its frame with 2+ effectively-enabled markers (marker_effectively_disabled
// for the enabled test, matching the render's survivor filter), every member
// reddened, so a coincident stack reads as one red flag mirroring the render's
// single stderr line; (2) a REF/PASS fallback via marker_effective — a
// dangling label ref, an extreme-ratio label ref, or a pass whose inheritance
// walk terminates on a surviving enabled ref, all of which resolve to
// source_idx == -1. The frame-0 seed is synthetic (no marker) and never
// reddens.
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
    std::set<int> red;   // red warp-marker store indices
    // THE COLLAPSE MEMBERS ALONE — pass 1's own subset of `red`, the
    // classifier's verdict (warp_coincident_collapse_members) kept apart from
    // the pass-2 fallbacks it is unioned with above. `red` answers "does the
    // render normalize this marker"; this answers WHICH WAY, for the one
    // reader that needs the distinction: the value pair's gate
    // (payload_eligibility, app_state.cpp) refuses a pass or a ref that sits
    // in a collapsed stack — the composer resolves such a member against the
    // RAW store, the ruled authored/display split, so its value is not what
    // the render applies — while the pass-2 fallbacks it must NOT refuse on
    // resolve against the projection and so already read out as the render's
    // own 1.00 (or as the empty payload). Filled in the same pass that fills
    // `red`, under the same key; no second computation anywhere.
    std::set<int> collapsed;
};

// Returns the warp red-flag cache entry for the app's live warp store,
// rebuilding only when the key does not match. Same single-threaded-reference
// lifetime rule as target_view_warp_frame_map_cached; the flag cache reads the
// set at build time (not per paint).
const WarpRedFlagCache& warp_red_flag_set_cached(
    const AppState& app, int sample_rate, long total_frames);

// Phase-reset sibling (the now-resolved naming symmetry): a coincident group
// of 2+ effectively-enabled (not disabled) phase resets sharing one exact
// frame reddens every member, mirroring build_phase_reset_source_frames'
// exact-equal collapse (one stderr line per group at render). Phase resets
// carry no tempo, labels, or inheritance, so collapse is their ONLY
// normalization — there is no marker_effective analog. Keyed on the
// phase-reset store generation alone (the same-frame count is independent of
// sample rate and length); the same committed-store / drag-freeze rule as the
// warp set.
struct PhaseResetRedFlagCache {
    bool      valid       = false;
    long long markers_gen = -1;
    std::set<int> red;   // red phase-reset store indices
};

const PhaseResetRedFlagCache& phase_reset_red_flag_set_cached(
    const AppState& app);

// Memoized target-view maps for states that are NOT live — the maps
// proposed_display_context builds below. TWO SLOTS, because the two readers
// ask about two different states in the same frame: the Undo and the Redo
// button faces each ask the Restrict undo to viewport lamp's predicate of
// their own stack's top entry, and one slot would thrash between them and
// rebuild both maps every tick.
//
// THE KEY IS THE CONTENT, not the entry's address: a slot answers only for a
// marker list ROW-EQUAL to the one it was built from (warp_rows_equal,
// app_state.h) under the same scale and audio identity. An undo entry has no
// stable identity to key on — the stacks pop, push and trim at the cap, so an
// address can name a different entry a moment later — and a stale map here
// would be a silently wrong lamp verdict rather than a visible fault. The
// compare is a whole-struct walk of a marker list, which is what the
// predicate's own touched-set reconstruction already costs.
//
// The memo also keeps the build LOUD-BUT-BOUNDED: every build runs the
// resolver, which prints its normalization lines to stderr, so an unmemoized
// per-tick build would reprint them at the frame rate for any state carrying a
// red flag. One build per distinct proposed state is the same bill the live
// cache pays per store generation.
struct ProposedTargetWarpFrameMapCache {
    struct Slot {
        bool      valid        = false;
        // The state this map was built from, kept for the key compare.
        std::vector<GuiWarpMarker> markers;
        double    scale        = 0.0;
        int       sample_rate  = 0;
        long      total_frames = 0;
        std::vector<WarpFrameMapSegment> warp_frame_map;
        // The domain total this map implies (target_total_frames_for_map,
        // with the live cache's own source-total fallback).
        int64_t   tgt_total_frames = 0;
    };
    Slot slots[2];
    // Round-robin victim when neither slot matches. Two readers alternating on
    // two slots never evict a live answer.
    int  next_victim = 0;
};

class GuiAudio;

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
// helpers (painted_column_of_source_frame / authored_frame_at_column) with
// their own map.
int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame);
int64_t active_domain_to_source_frame(const AppState& app, const GuiAudio& audio,
                                      int64_t domain_frame);

// THE FORWARD TRANSLATION'S ONE BODY, with the domain named by the caller:
// the image of a source frame in whatever domain `ctx` describes — the
// identity in Source, the context's own map in TargetLive.
// source_frame_to_active_domain above is its LIVE face and does nothing but
// hand it active_display_context's answer; the second face is the proposed
// context below. There is no second spelling of the target-domain arithmetic.
//
// THE INVERSE HAS ONE FACE ONLY, and deliberately: nothing asks where a
// displayed position would sit in a state that is not live, so
// active_domain_to_source_frame keeps its live-context body and takes no
// ctx-parameterized twin (a class exists iff it has a producer).
int64_t source_frame_to_domain(const GuiDisplayContext& ctx,
                               int64_t source_frame);

// THE CONTEXT A STATE THAT IS NOT LIVE WOULD DISPLAY: the domain the GUI would
// be showing if `markers` and `settings` were the live warp store and engine
// block. Its one reader is the Restrict undo to viewport lamp's predicate
// (undo_restore_within_viewport, app_state.h), which has to measure a restore's
// touched markers where the restore will PAINT them — in target view the
// picture is drawn through the warp map, and the map IS the warp marker list,
// so a span measured under the map standing now is off by exactly the change
// being undone.
//
// THE VIEW RULE IS NOT RE-READ HERE: this asks active_display_context for the
// DOMAIN (that accessor stays the only reader of app.active_audio_view for
// domain queries) and replaces only the map and the domain total, so source
// view returns the live identity context unchanged and the answer there is
// exact.
//
// RETURNED BY VALUE, and the map it points at is the app-owned proposed cache
// above: the pointer stays good until two further calls with different states
// evict the slot, so use the context and let it go — do not hold one across
// another call, the same lifetime rule the live accessor carries.
GuiDisplayContext proposed_display_context(
    const AppState& app, const GuiAudio& audio,
    const std::vector<GuiWarpMarker>& markers,
    const EngineSettings& settings);

// The stem painters' samples-per-pixel and the single source of truth for the
// on-screen column grid: the visible span nearbyint-quantized to whole samples
// (matching the vp_end the waveform cache carries, vp_start +
// nearbyint(spp * area.w)) divided back over the strip width. The
// pixel-anchoring pair below and the viewport snap in clamp_viewport_start
// (main.cpp) all take their `q` from here, so the viewport grid and the marker
// grid are one grid at any window width (not just multiples of 8). Returns 0.0
// on degenerate geometry (no strip width / no zoom).
struct GuiRect;
double painter_samples_per_pixel(const AppState& app, const GuiAudio& audio,
                                 const GuiRect& area);

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
// move_playhead_pixels (viewport.cpp) spells this same recovery expression
// inline in the INT64 domain — its column walk composes with
// displayed_grid_position_at_column's int64 col and cannot take this owner's
// int return — and is recorded at its own site ("the recovery nearbyint is
// the column direction and is this walk's own"). It is the exempt list's
// whole int64 class; a second int64 spelling would be the fork this owner
// exists to prevent.
inline int displayed_column_at(double displayed, double vp_start, double spp) {
    return static_cast<int>(std::nearbyint((displayed - vp_start) / spp));
}

// Pixel-anchoring pair for gesture commits. Every gesture that moves an
// authored position by pixel columns (the bare Left/Right nudges on both
// marker columns) or releases one at a
// pointer position (marker AND trim drag commits both snap their release to
// the painted column) anchors to the on-screen column grid through these two
// helpers: read the item's
// currently painted column with painted_column_of_source_frame, pick the
// destination column, and commit authored_frame_at_column of it — which
// funnels through snap_authored_frame (app_state.h), the single
// fractional-to-authored route. Anchoring to the column grid re-derives
// the pixel phase on every gesture, so whole-frame rounding residue can
// never accumulate on top of an off-grid sub-pixel phase; the painted
// move is exactly the commanded number of columns, and the stored value
// is the whole frame the painting already shows. Exactly ONE item moves per
// gesture: a nudge steps the FOCUS (a 2+ selection collapses to it first — groups
// are never moved, the doctrine at the head of position_nudge.h) and a drag
// moves the marker it grabbed.
//
// painted_column_of_source_frame: the pixel column (offset from
// waveform_area(app).x) the stem painters draw `source_frame` at,
// computed with the painters' own math (the marker-stem overlay
// paint_marker_stems): nearbyint the frame; in the TargetLive domain
// forward-map it through `warp_frame_map` and nearbyint the map output;
// then divide by the painters' samples-per-pixel — the visible span
// nearbyint-quantized to whole samples over the strip width — and round
// with the painters' std::nearbyint. `warp_frame_map` is the map the item is painted through:
// the DISPLAYED map (displayed_or_live_target_map — the event-synchronized paint
// basis, falling back to the live cache when cold), at rest and at drag commit.
// Ignored in the Source domain; an empty map in a mapped domain falls
// back to identity, exactly like paint. Returns 0 when the strip has no
// width (callers guard the degenerate geometry).
int painted_column_of_source_frame(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map);

// The explicit-basis variant of painted_column_of_source_frame: the same
// painters' math, but the viewport (`vp_start`) and samples-per-pixel (`spp`)
// come from the CALLER instead of the live viewport / painter_samples_per_pixel.
// painted_column_of_source_frame delegates here with the LIVE basis; the flag
// EDITOR's unrolled box passes the ITEM basis (item_viewport_basis in
// app_state.h) — damage follows the pixels it erases, so the box unrolls from
// the column the flag pixels were painted at even mid-publish, when the live
// viewport already holds a not-yet-blitted span. (Two other ITEM-basis callers
// died in row 5 with the marker-text lane: the run centering and the run hit.
// The selected-stem invalidator was the other _on_basis caller until
// 2026-07-30; it rode the ITEM basis for PLATE-painted pixels, was widened to a
// full waveform-area invalidate rather than re-based, and is gone entirely with
// the selection-keyed stem itself.)
// `spp` must be > 0 (returns 0, a valid column, on a degenerate spp — callers
// guard the geometry, exactly like the live-basis form). The domain and the
// source->target mapping are unchanged (they don't depend on the viewport).
int painted_column_of_source_frame_on_basis(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    double vp_start, double spp);

// authored_frame_at_column: the authored source-frame value of pixel
// column `col` under the same coordinate system — the active-domain time is
// displayed_grid_position_at_column above (the single-rounding grid at the
// painters' samples-per-pixel), in EVERY domain; in the
// TargetLive domain that time is quantized to an integer target frame
// (llrint, floored at 0 — the same quantization the target-view nudges
// have always applied) and inverse-mapped through `warp_frame_map` at full
// precision. The
// result returns through snap_authored_frame, so it is a whole source
// frame in the authored int64 domain; callers apply their own walls
// AFTER — the walls win over the
// pixel grid, and every wall is itself an integer frame. Returns 0 when
// the strip has no width (callers guard the degenerate geometry).
int64_t authored_frame_at_column(
    const AppState& app, const GuiAudio& audio, int col,
    const std::vector<WarpFrameMapSegment>& warp_frame_map);

// -- THE PHASE-RESET LATTICE (the engine's seed geometry, GUI-side) ---------
//
// These four live here, beside the map functions they read, because the
// question they answer is a MAP question: where the engine's analysis lattice
// falls in source frames under the map a render would use. They were the
// painter's alone until 2026-09-09, when the phase-reset column got its own
// iteration bracket and the walls, the editor's refusal, the arrows' landing
// and the sweep all had to ask the same lattice the overlay band paints.

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

// THE HOP CELL'S AUTHORED FRAME: where a phase reset resting at source frame
// `S` is authored in the iteration cell `k` hops away, under `map`.
//
// HIS MINIMUM-DISPLACEMENT RULE (architect 2026-09-09): "for the phase reset
// overlay, the amount of change added by each hop should be the MINIMUM amount
// to get to that hop, so that we stay as close to the original starting point
// of the phase reset as possible". The derivation is the seed rule read
// backwards: with C(m) the window centre above, seed(S) = max{ m : C(m) <= S },
// so the frames that seed at m are exactly the half-open interval
// [C(m), C(m+1)) — and the nearest member of the interval k hops away from
// m0 = seed(S) is that interval's NEAR END. Hence
//   k == 0  ->  S itself (the identity cell renders the resting store),
//   k > 0   ->  C(m0 + k),         the SMALLEST frame whose seed is m0 + k,
//   k < 0   ->  C(m0 + k + 1) - 1, the LARGEST frame whose seed is m0 + k.
//
// TWO CONSEQUENCES, both wanted. A cell either way moves the reset by at least
// one frame and at most about a hop (plus the map's local rounding), so a
// bracket of nine is nine hops of displacement and not nine hops plus a
// residue. And NEITHER DIRECTION CARRIES THE SUB-HOP RESIDUE the resting reset
// holds between C(m0) and S: a positive cell restarts at its own interval's
// floor and a negative one at its own interval's ceiling, so the cells are a
// clean walk of the lattice rather than the resting offset translated k times.
//
// THE RESULT IS AN INTEGER SOURCE FRAME BY CONSTRUCTION — llrint plus integer
// terms, never a fractional authored position — so snap_authored_frame is not
// called and is not owed one: it is the single double-to-authored conversion
// route (app_state.h), and no double-to-authored conversion happens here.
//
// THE LATTICE IS THE MAP'S, AND THE MAP EVERY CALLER PASSES IS THE LIVE ONE
// (live_warp_frame_map below) — the map the sweep's own cells render under. A
// sweep is ONE COLUMN'S since 2026-09-10, so a phase sweep rewrites no tempo
// and moves no lattice: the displacement is computed under the same map the
// overlay band SHOWS, the walls were checked under and every cell renders
// under, once per reset per k. Each cell's sidecar carries an ordinary whole
// authored frame and re-parses normally.
int64_t phase_reset_hop_cell_frame(
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

// WHICH WALL CLOSED A SIDE of the hop window below. Every side is closed by
// exactly one of the three, so the kind is always meaningful.
enum class PhaseHopWall {
    Digit,      // +/-kIterHopMax — "past nine it is no longer the phase reset"
    PieceEdge,  // the cell would land before frame 0 or past total_frames - 1
    Neighbour,  // the cell would reach the adjacent phase reset's own extreme
};

// THE LEGAL HOP INTERVAL for the phase reset at `idx`, and what closed it on
// each side. k_min <= 0 <= k_max ALWAYS: the identity cell renders the resting
// store, so 0 is inside the window whatever the neighbours do, exactly as the
// warp bracket's clamp window always contains the zero delta.
struct PhaseHopWindow {
    int          k_min    = 0;
    int          k_max    = 0;
    PhaseHopWall min_wall = PhaseHopWall::Digit;
    PhaseHopWall max_wall = PhaseHopWall::Digit;
};

// THE PHASE BRACKET'S WALLS, ONE OWNER (architect 2026-09-09's (d): a cell
// that would push the reset before frame 0, past the last frame, or onto a
// neighbour is REFUSED AT AUTHORING, like the tempo window). Walks k outward
// from 0 on each side under the live map, landing each candidate through
// phase_reset_hop_cell_frame, and stops at the first k that breaks a wall — at
// most kIterHopMax steps a side:
//
//   THE PIECE: 0 <= F <= total_frames - 1, the drop's own EOF wall
//   (drop_phase_reset_at_position, phaseresetmarkers_ops.cpp).
//
//   THE NEIGHBOURS ARE THE IMMEDIATE STORE ROWS, DISABLED INCLUDED, and the
//   wall is STRICT (F > prev_wall, F < next_wall). Disabled rows count because
//   the store is SORTED BY time_frame at rest with disabled rows in it and
//   every cell's sidecar is written from a per-cell copy of that vector: a
//   displaced reset crossing or landing on ANY row would write an
//   out-of-order or coincident-by-displacement sidecar. The sort is the
//   invariant, not participation. It is strict because his (d) refuses "onto a
//   neighbour" — coincident drops stay legal AT REST (that rule is untouched;
//   this is a sweep cell, not authoring at rest).
//
//   AND THE NEIGHBOUR'S WALL IS ITS OWN NEAREST LANDING, not its resting
//   frame: an ENABLED predecessor walls at its furthest-RIGHT cell
//   (its iter_end_hops landing) and an enabled successor at its furthest-LEFT
//   (its iter_start_hops landing), so two adjacent brackets each inside the
//   other's extreme can never cross or meet IN ANY CELL and the whole
//   Cartesian product is sorted by construction. It is mutual — raising A's
//   upper narrows B's lower window — which is the tempo window's own shape. A
//   DISABLED neighbour contributes its RESTING frame instead: it is out of the
//   product and carries no bracket at all (its own disable cleared one —
//   architect 2026-09-10), so it never moves.
//
// READERS: the bound editor's commit (refuses outside the window, naming the
// wall kind — GuiFlagEditor::commit_iter_bound_edit), the step's landing owner
// (phase_iter_bound_step_landing, app_state.h, which CLAMPS into the window
// and then at the partner), the group scan and the directional face through
// it, and the sweep's plan (iteration_sweep_plan), which re-verifies every
// standing bracket against this window on every read because nothing clamps
// retroactively on this column.
PhaseHopWindow phase_reset_hop_window(const AppState& app,
                                      const GuiAudio& audio, int idx);
