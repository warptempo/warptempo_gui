#include "warp_frame_map_view.h"

#include "app_state.h"
#include "audio.h"
#include "gui_display_context.h"
#include "warp_frame_map_build.h"   // resolve_warp_markers_for_render, build_warp_frame_map
#include "engine/engine_geometry.h"  // kN, kRs — the phase-reset lattice
#include <algorithm>
#include <bit>
#include <limits>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// GUI target-view warp_frame_map helpers, split out of warp_frame_map_build.cpp so the
// build core (resolve_warp_markers_for_render / build_warp_frame_map / the
// phase-reset assembly) carries no AppState dependency and lives in
// the parser sources. These helpers stay GUI-side: they read the
// live AppState marker store and the active display context, and own the
// memoized target-view cache. Declarations remain in warp_frame_map_view.h.

std::vector<WarpFrameMapSegment> build_target_view_warp_frame_map(
    const std::vector<GuiWarpMarker>& markers,
    double scale,
    int sample_rate,
    long total_frames,
    std::string* error_out) {
    // Trim is render-time, not view-time — target view paints the WHOLE song,
    // and build_warp_frame_map builds the whole-song map, matching the
    // paint_handler construction so hit-test math and waveform paint walk the
    // same segment list. This overload always builds directly and is the entry
    // point for a caller-supplied marker list; live-state consumers go
    // through target_view_warp_frame_map_cached. (It served HYPOTHETICAL never-live
    // lists too until 2026-07-29 — the tempo drag's bisection candidates — which is
    // what the deleted `quiet` forwarding existed for; see the declaration.)
    // A build failure (tripwire-class only — the resolver above is a total
    // normalizer that resolves every ambiguous arrangement to tempo 1.00 and
    // cannot fail) returns the empty map and reports the message through
    // error_out. The empty map paints as identity; the cache records the
    // error so readers can tell the failed build apart from a legitimate
    // identity state.
    if (error_out) error_out->clear();
    auto resolved = resolve_warp_markers_for_render(
        slice_to_warp_markers(markers), sample_rate, total_frames);
    auto r = build_warp_frame_map(
        resolved, scale, sample_rate, total_frames);
    if (!r) {
        if (error_out) *error_out = std::move(r.error());
        return {};
    }
    return std::move(*r);
}

const TargetWarpFrameMapCache& target_view_warp_frame_map_cached(
    const AppState& app, int sample_rate, long total_frames) {
    TargetWarpFrameMapCache& c = app.target_warp_frame_map_cache;
    const long long gen = app.warpmarkers.generation();
    const double scale  = app.engine_settings.scale;
    if (c.valid && c.markers_gen == gen && c.scale == scale &&
        c.sample_rate == sample_rate && c.total_frames == total_frames) {
        return c;
    }
    c.warp_frame_map = build_target_view_warp_frame_map(
        app.warpmarkers.markers(), scale, sample_rate, total_frames,
        &c.build_error);
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const auto& s : c.warp_frame_map) {
        h ^= std::bit_cast<uint64_t>(s.src_frame);
        h *= 0x100000001b3ULL;
        h ^= std::bit_cast<uint64_t>(s.tgt_frame);
        h *= 0x100000001b3ULL;
    }
    c.hash         = c.warp_frame_map.empty() ? 0 : h;
    c.tgt_total_frames = target_total_frames_for_map(
        static_cast<int64_t>(total_frames), c.warp_frame_map);
    c.markers_gen  = gen;
    c.scale        = scale;
    c.sample_rate  = sample_rate;
    c.total_frames = total_frames;
    c.valid        = true;
    return c;
}

const WarpRedFlagCache& warp_red_flag_set_cached(
    const AppState& app, int sample_rate, long total_frames) {
    WarpRedFlagCache& c = app.warp_red_flag_cache;
    const long long gen = app.warpmarkers.generation();
    if (c.valid && c.markers_gen == gen &&
        c.sample_rate == sample_rate && c.total_frames == total_frames) {
        return c;
    }

    c.red.clear();
    c.collapsed.clear();
    // Slice once — marker_effective and marker_effectively_disabled are
    // parser-domain and read no GUI-only fields. This resolves the COMMITTED
    // store, so a marker drag (which writes app.drag.moveable_times, not the
    // store, until commit) shows no red-flag change until release.
    const std::vector<WarpMarker> mv =
        slice_to_warp_markers(app.warpmarkers.markers());
    const int n = static_cast<int>(mv.size());

    // Pass 1 — exact-frame collapse: warp_coincident_collapse_members
    // (warp_frame_map_build.h) OWNS the coincident-collapse rule —
    // the render resolver's stage 2 consults the same classifier, so the
    // render's collapse verdict and this red cue cannot drift. The GUI
    // consumes it here at press/paint time as a pure function of the
    // committed store; a marked run reads as one red flag, matching the
    // render's single stderr line per group.
    // The same verdict is kept apart in `collapsed` for the value pair's gate
    // (the field's own contract, warp_frame_map_view.h).
    const std::vector<char> members = warp_coincident_collapse_members(mv);
    for (int k = 0; k < n; ++k) {
        if (members[static_cast<size_t>(k)]) {
            c.red.insert(k);
            c.collapsed.insert(k);
        }
    }

    // Pass 2 — ref/pass 1.00 fallback: marker_effective is the silent
    // per-marker resolution the hover uses; it reports the render's
    // normalization fallback as source_idx == -1. Three cases redden: a
    // dangling label ref (reason UndefinedLabel), an extreme-ratio label ref
    // (reason ExtremeRatio), and a PASS whose inheritance walk terminated on a
    // surviving enabled ref (from_ref). A pass reddens ONLY when it
    // inherits-from-a-ref: a benign pass that inherits a real 1.00 from a
    // synthetic prior (the frame-0 seed or a collapsed-group owner) also
    // carries source_idx -1 but reason None and from_ref false, and the render
    // prints no line for it — so it is EXCLUDED. An owner resolves to its own
    // index (>= 0), never caught here (a collapse-group owner is reddened by
    // pass 1 instead). Effectively-disabled markers do not render and are
    // excluded.
    for (int k = 0; k < n; ++k) {
        if (marker_effectively_disabled(mv, static_cast<size_t>(k))) continue;
        const MarkerEffective me = marker_effective(mv, k, total_frames);
        if (me.source_idx != -1) continue;
        const bool ref_fallback =
            me.reason == MarkerEffective::NormalizedReason::UndefinedLabel ||
            me.reason == MarkerEffective::NormalizedReason::ExtremeRatio;
        if (ref_fallback || me.from_ref)
            c.red.insert(k);
    }

    c.markers_gen  = gen;
    c.sample_rate  = sample_rate;
    c.total_frames = total_frames;
    c.valid        = true;
    return c;
}

const PhaseResetRedFlagCache& phase_reset_red_flag_set_cached(
    const AppState& app) {
    PhaseResetRedFlagCache& c = app.phase_reset_red_flag_cache;
    const long long gen = app.phaseresetmarkers.generation();
    if (c.valid && c.markers_gen == gen) return c;

    c.red.clear();
    // Exact-frame collapse, the phase-reset sibling of the warp resolver's
    // stage-2 normalization (build_phase_reset_source_frames): disabled resets
    // are skipped, and a run of 2+ enabled resets sharing one frame collapses
    // to one event. The store is time-sorted, so a coincident group is a run of
    // adjacent equal frames; redden every member of a run with 2+ enabled.
    const std::vector<GuiPhaseResetMarker>& pr = app.phaseresetmarkers.markers();
    const int n = static_cast<int>(pr.size());
    int i = 0;
    while (i < n) {
        int j = i + 1;
        while (j < n && pr[j].time_frame == pr[i].time_frame) ++j;
        int enabled = 0;
        for (int k = i; k < j; ++k) if (!pr[k].disabled) ++enabled;
        if (enabled >= 2)
            for (int k = i; k < j; ++k) c.red.insert(k);
        i = j;
    }

    c.markers_gen = gen;
    c.valid       = true;
    return c;
}

// Definition; the descriptive comment lives at the declaration in
// warp_frame_map_view.h. Exposed (non-anonymous) so main.cpp's viewport snap
// in clamp_viewport_start takes its `q` from the same source as the
// pixel-anchoring helpers below — one grid for viewport and markers.
double painter_samples_per_pixel(const AppState& app, const GuiAudio& audio,
                                 const GuiRect& area) {
    if (area.w <= 0) return 0.0;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return 0.0;
    return std::nearbyint(spp * static_cast<double>(area.w)) /
           static_cast<double>(area.w);
}

int painted_column_of_source_frame_on_basis(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    double vp_start, double spp) {
    if (spp <= 0.0) return 0;
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    // The painters' exact shape (frame_to_paint_sample in render.cpp):
    // nearbyint the source frame; in the TargetLive domain forward-map and
    // nearbyint the map output; then std::nearbyint the fractional column.
    double ms = std::nearbyint(source_frame);
    if (ctx.domain != GuiDisplayDomain::Source && !warp_frame_map.empty()) {
        ms = std::nearbyint(map_source_to_target(ms, warp_frame_map));
    }
    return displayed_column_at(ms, vp_start, spp);
}

int painted_column_of_source_frame(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map) {
    const GuiRect area = waveform_area(app);
    const double spp = painter_samples_per_pixel(app, audio, area);
    if (spp <= 0.0) return 0;
    // The LIVE basis: the live viewport start and the painter-quantized spp.
    // Gesture-commit callers (the nudges, drag commits, trim drags) anchor to the
    // LIVE on-screen grid by ruling, and live-painted DAMAGE (playhead columns)
    // stays live too. The boundary is damage-follows-the-pixels: damage rides the
    // basis of the pixels it erases, so the flag editor's box placement instead
    // rides the ITEM basis via _on_basis — the box paints on the promoted item
    // mirror, so its geometry must read it too.
    return painted_column_of_source_frame_on_basis(
        app, audio, source_frame, warp_frame_map,
        static_cast<double>(app.viewport_start_sample), spp);
}

int64_t authored_frame_at_column(
    const AppState& app, const GuiAudio& audio, int col,
    const std::vector<WarpFrameMapSegment>& warp_frame_map) {
    const GuiRect area = waveform_area(app);
    const double spp = painter_samples_per_pixel(app, audio, area);
    if (spp <= 0.0) return 0;
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    // The column's ACTIVE-DOMAIN time on the single-rounding grid (the
    // grid-snapped viewport is a true grid point, so the recovered column index
    // is exact). Both arms below land on it, so a commit is anchored at frame 0
    // of the domain it was authored in rather than at the current viewport
    // start.
    const double g =
        displayed_grid_position_at_column(app.viewport_start_sample, col, spp);
    if (ctx.domain == GuiDisplayDomain::Source) {
        // snap_authored_frame stays the sole double-to-authored conversion.
        return snap_authored_frame(g);
    }
    if (!warp_frame_map.empty()) {
        // Target view: quantize the column's target-domain time to an integer
        // target frame (floored at 0), then inverse-map at full precision; the
        // map is monotone increasing, so the target-domain direction is the
        // source-domain direction. That time is the grid position above, so a
        // phase-reset nudge, drag or sweep commit is viewport-phase-independent
        // exactly like the source arm's (this arm spelled the two-rounding
        // viewport_start + nearbyint(col*q) until 2026-08-22 — the source arm
        // took the single-rounding grid in 2026-07-14 and this one was missed).
        const double qf = (g < 0.0)
            ? 0.0
            : static_cast<double>(std::llrint(g));
        return snap_authored_frame(map_target_to_source(qf, warp_frame_map));
    }
    return snap_authored_frame(g);
}

// The single reader of app.active_audio_view for DOMAIN QUERIES (see
// gui_display_context.h for the ruling and the mode-logic carve-out). The
// two arms are the view rule.
const GuiDisplayContext& active_display_context(const AppState& app,
                                                const GuiAudio& audio) {
    // Function-local static storage, refreshed on every call. The GUI
    // loop is single-threaded, and warp_frame_map aliases the app-owned
    // cache exactly as the direct cache callers always did — valid until
    // a changed-key rebuild.
    static GuiDisplayContext ctx;
    static const std::vector<WarpFrameMapSegment> kIdentityMap;
    if (app.active_audio_view == 'T') {
        // Live target view: the memoized target-view map is the
        // translation, and the displayed total is the built map's target
        // total — source total when the map cannot build (the cache's
        // tgt_total_frames is only > 0 for a built map).
        const TargetWarpFrameMapCache& c = target_view_warp_frame_map_cached(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames()));
        ctx.domain = GuiDisplayDomain::TargetLive;
        ctx.warp_frame_map = &c.warp_frame_map;
        ctx.domain_total_frames = (c.tgt_total_frames > 0)
            ? c.tgt_total_frames
            : audio.total_frames();
    } else {
        // Source view: identity, source total.
        ctx.domain = GuiDisplayDomain::Source;
        ctx.warp_frame_map = &kIdentityMap;
        ctx.domain_total_frames = audio.total_frames();
    }
    ctx.sample_rate = audio.sample_rate();
    return ctx;
}

// Translate through a display context. A Source-domain context
// is identity outright; the TargetLive domain inlines the forward / inverse
// map math (map_source_to_target / map_target_to_source) against the
// context's OWN map. The forward direction takes its context as a PARAMETER,
// so the live translation and the proposed one below run the same arithmetic;
// the inverse has only the live face (the reason is at its declaration).
// Sites translating against an explicit caller-supplied map
// (a proposed pre-commit marker list) use the explicit-map pixel-anchoring
// helpers instead. The empty-map path (the unbuildable-target fallthrough)
// stays identity — map_source_to_target / map_target_to_source are identity on
// an empty map.
int64_t source_frame_to_domain(const GuiDisplayContext& ctx,
                               int64_t source_frame) {
    if (ctx.domain == GuiDisplayDomain::Source) return source_frame;
    const size_t q = (source_frame < 0)
        ? static_cast<size_t>(0)
        : static_cast<size_t>(source_frame);
    return static_cast<int64_t>(
        std::nearbyint(map_source_to_target(q, *ctx.warp_frame_map)));
}

int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame) {
    return source_frame_to_domain(active_display_context(app, audio),
                                  source_frame);
}

GuiDisplayContext proposed_display_context(
    const AppState& app, const GuiAudio& audio,
    const std::vector<GuiWarpMarker>& markers,
    const EngineSettings& settings) {
    // The domain rule comes from the live accessor and is not re-read (the
    // contract at the declaration): a copy of the live context is already the
    // right answer in source view, where the map is the identity, and in
    // target view it needs only its map and its total replaced.
    GuiDisplayContext ctx = active_display_context(app, audio);
    if (ctx.domain != GuiDisplayDomain::TargetLive) return ctx;

    const int  sample_rate  = audio.sample_rate();
    const long total_frames = static_cast<long>(audio.total_frames());

    // THE STATE THAT IS ALREADY LIVE NEEDS NO MAP OF ITS OWN — the live cache
    // holds it, built and paid for. This is the phase-reset entry's whole
    // path: an undo entry carries a full column pair and a restore assigns
    // both, so a 'P' entry's warp list is a byte-identical copy of the live
    // one and the map a restore of it installs is the map standing now.
    if (settings.scale == app.engine_settings.scale &&
        warp_rows_equal(markers, app.warpmarkers.markers())) {
        return ctx;
    }

    ProposedTargetWarpFrameMapCache& c = app.proposed_target_map_cache;
    for (auto& slot : c.slots) {
        if (slot.valid && slot.scale == settings.scale &&
            slot.sample_rate == sample_rate &&
            slot.total_frames == total_frames &&
            warp_rows_equal(slot.markers, markers)) {
            ctx.warp_frame_map      = &slot.warp_frame_map;
            ctx.domain_total_frames = slot.tgt_total_frames;
            return ctx;
        }
    }

    ProposedTargetWarpFrameMapCache::Slot& slot = c.slots[c.next_victim];
    c.next_victim = (c.next_victim + 1) % 2;
    // Loud like every other build (the ruling at build_target_view_warp_frame_-
    // map's declaration): a proposed state's normalization prints its lines
    // once, when the state first reaches a slot, and the memo is what keeps
    // "once" from becoming "every tick".
    slot.warp_frame_map = build_target_view_warp_frame_map(
        markers, settings.scale, sample_rate, total_frames, nullptr);
    // The live cache's own total rule, verbatim: the map's target total when
    // it is positive, the source total when the map is empty or degenerate.
    const int64_t tt = target_total_frames_for_map(
        static_cast<int64_t>(total_frames), slot.warp_frame_map);
    slot.tgt_total_frames = (tt > 0) ? tt : audio.total_frames();
    slot.markers      = markers;
    slot.scale        = settings.scale;
    slot.sample_rate  = sample_rate;
    slot.total_frames = total_frames;
    slot.valid        = true;

    ctx.warp_frame_map      = &slot.warp_frame_map;
    ctx.domain_total_frames = slot.tgt_total_frames;
    return ctx;
}

int64_t active_domain_to_source_frame(const AppState& app, const GuiAudio& audio,
                                      int64_t domain_frame) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::Source) return domain_frame;
    const size_t q = (domain_frame < 0)
        ? static_cast<size_t>(0)
        : static_cast<size_t>(domain_frame);
    return snap_authored_frame(map_target_to_source(q, *ctx.warp_frame_map));
}

// -- THE PHASE-RESET LATTICE ------------------------------------------------
//
// The contracts (the lockstep warning, the minimum-displacement derivation,
// the resting-map asymmetry and the wall rules) are at the declarations in
// warp_frame_map_view.h; what follows is the arithmetic.

int64_t phase_reset_window_centre_frame(
    int64_t m, const std::vector<WarpFrameMapSegment>& map) {
    const double window_start =
        map_target_to_source(static_cast<double>(m * kRs), map) -
        static_cast<double>(kN) / 2.0;
    return std::llrint(window_start) + kN / 2;
}

int64_t phase_reset_seed_frame_index(
    int64_t reset_source_frame,
    const std::vector<WarpFrameMapSegment>& map) {
    // seed(S) = max{ m : centre(m) <= S }, expressed THROUGH the centre owner
    // so the search and the cell frames below cannot drift apart.
    const auto seeds_at_or_before = [&](int64_t m) {
        return phase_reset_window_centre_frame(m, map) <= reset_source_frame;
    };
    const double t_reset = map_source_to_target(
        static_cast<double>(reset_source_frame), map);
    int64_t m = std::max<int64_t>(
        0, static_cast<int64_t>(std::floor(t_reset / static_cast<double>(kRs))));
    while (m > 0 && !seeds_at_or_before(m)) --m;
    while (seeds_at_or_before(m + 1)) ++m;
    return m;
}

int64_t phase_reset_hop_cell_frame(
    int64_t reset_source_frame, int k,
    const std::vector<WarpFrameMapSegment>& map) {
    // The identity cell is the resting store itself, byte for byte — no
    // arithmetic, so no rounding can move a reset the user did not ask to move.
    if (k == 0) return reset_source_frame;
    const int64_t m0 = phase_reset_seed_frame_index(reset_source_frame, map);
    // The frames seeding at m are [C(m), C(m+1)), so the nearest member of the
    // interval k hops away is its NEAR end: the floor going right, the ceiling
    // going left.
    if (k > 0) return phase_reset_window_centre_frame(m0 + k, map);
    return phase_reset_window_centre_frame(m0 + k + 1, map) - 1;
}

const std::vector<WarpFrameMapSegment>& live_warp_frame_map(
    const AppState& app, const GuiAudio& audio) {
    return target_view_warp_frame_map_cached(
               app, audio.sample_rate(),
               static_cast<long>(audio.total_frames())).warp_frame_map;
}

PhaseHopWindow phase_reset_hop_window(const AppState& app,
                                      const GuiAudio& audio, int idx) {
    PhaseHopWindow out;
    const std::vector<GuiPhaseResetMarker>& pv =
        app.phaseresetmarkers.markers();
    const int n = static_cast<int>(pv.size());
    // Degenerate subject: the identity-only window, which is what every caller
    // reads as "nothing but the resting cell is legal here".
    if (idx < 0 || idx >= n) return out;

    const std::vector<WarpFrameMapSegment>& map = live_warp_frame_map(app, audio);
    const int64_t rest = pv[static_cast<size_t>(idx)].time_frame;
    const int64_t last_frame = audio.total_frames() - 1;

    // THE NEIGHBOUR WALLS. An ENABLED neighbour walls at the extreme cell its
    // own bracket can reach (its dormant-bracket twin cannot move at all, so a
    // DISABLED one walls at its resting frame); a blank bracket's value_or(0)
    // lands on the resting frame through the identity cell above. Both are
    // computed under this same resting map, which is what makes the two
    // windows mutually consistent.
    const auto neighbour_wall = [&](int j, bool upper_side) -> int64_t {
        const GuiPhaseResetMarker& q = pv[static_cast<size_t>(j)];
        if (q.disabled) return q.time_frame;
        const int k = upper_side ? q.iter_end_hops.value_or(0)
                                 : q.iter_start_hops.value_or(0);
        return phase_reset_hop_cell_frame(q.time_frame, k, map);
    };
    const int64_t prev_wall =
        idx > 0 ? neighbour_wall(idx - 1, /*upper_side=*/true)
                : std::numeric_limits<int64_t>::min();
    const int64_t next_wall =
        idx + 1 < n ? neighbour_wall(idx + 1, /*upper_side=*/false)
                    : std::numeric_limits<int64_t>::max();

    // The two walks, outward from the identity cell, stopping at the first k
    // that breaks a wall. At most kIterHopMax landings a side, each one seed
    // search over a monotone map.
    for (int k = 1; k <= kIterHopMax; ++k) {
        const int64_t f = phase_reset_hop_cell_frame(rest, k, map);
        if (f > last_frame) { out.max_wall = PhaseHopWall::PieceEdge; break; }
        if (f >= next_wall) { out.max_wall = PhaseHopWall::Neighbour; break; }
        out.k_max    = k;
        out.max_wall = PhaseHopWall::Digit;
    }
    for (int k = -1; k >= -kIterHopMax; --k) {
        const int64_t f = phase_reset_hop_cell_frame(rest, k, map);
        if (f < 0)          { out.min_wall = PhaseHopWall::PieceEdge; break; }
        if (f <= prev_wall) { out.min_wall = PhaseHopWall::Neighbour; break; }
        out.k_min    = k;
        out.min_wall = PhaseHopWall::Digit;
    }
    return out;
}
