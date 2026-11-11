#include "warp_frame_map_view.h"

#include "app_state.h"
#include "audio.h"
#include "gui_display_context.h"
#include "waveform_gain.h"   // kWaveformGainVersion, the gain field
#include "warp_frame_map_build.h"   // resolve_warp_markers_for_render, build_warp_frame_map
#include "engine/engine_geometry.h"  // kN, kRs — the phase-reset lattice
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iterator>
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

    // Pass 2 — the label-ref 1.00 fallback: marker_effective is the silent
    // per-marker resolution the display surfaces use; it reports the render's
    // normalization fallback as source_idx == -1 with a reason. Three cases
    // redden: a dangling label ref (reason UndefinedLabel — a disabled
    // definition's references among them, participation being each marker's
    // own flag since 2026-10-29),
    // a reference to a label two live definitions hold (reason
    // AmbiguousLabel, 2026-10-29) and an extreme-ratio label ref (reason
    // ExtremeRatio). NO PASS reddens here: the
    // inheritance walk skips label refs to the owner behind them (architect
    // 2026-09-13, resolve_inherited_tempo), so no pass normalizes; a pass that
    // inherits from a synthetic prior (the frame-0 seed or a collapsed-group
    // owner) carries source_idx -1 with reason None and is excluded by the
    // reason test. An owner resolves to its own index (>= 0), never caught
    // here (a collapse-group owner is reddened by pass 1 instead).
    // Effectively-disabled markers do not render and are excluded.
    for (int k = 0; k < n; ++k) {
        if (marker_effectively_disabled(mv, static_cast<size_t>(k))) continue;
        const MarkerEffective me = marker_effective(mv, k, total_frames);
        if (me.source_idx != -1) continue;
        if (me.reason == MarkerEffective::NormalizedReason::UndefinedLabel ||
            me.reason == MarkerEffective::NormalizedReason::AmbiguousLabel ||
            me.reason == MarkerEffective::NormalizedReason::ExtremeRatio)
            c.red.insert(k);
    }

    // Pass 2b — THE AMBIGUOUS LABEL'S DEFINITIONS (architect 2026-10-29: "now
    // they both get struck, as do all the label references. I think that's
    // the sane fallback"): every live, un-collapsed definition whose label
    // another such definition holds, warp_ambiguous_label_defs' verdict
    // (warp_frame_map_build.h) — the same projection and the same definition
    // count the resolver's stage 5 reads, so the strike on the definitions
    // and the render's refusal to honor them cannot drift. A disabled
    // definition is never marked: it fades, it does not strike.
    const std::vector<char> ambiguous = warp_ambiguous_label_defs(mv);
    for (int k = 0; k < n; ++k) {
        if (ambiguous[static_cast<size_t>(k)]) c.red.insert(k);
    }

    // Pass 3 — participation-blind coincidence: every row of a run of 2+ rows
    // at one frame, disabled or not. The store is time-sorted, so a run is
    // adjacent equal frames. This is the paint cue alone (the header's
    // contract): the render collapses nothing where fewer than two of the run
    // are enabled, and `collapsed` stays pass 1's.
    int i = 0;
    while (i < n) {
        int j = i + 1;
        while (j < n && mv[static_cast<size_t>(j)].time_frame ==
                            mv[static_cast<size_t>(i)].time_frame)
            ++j;
        if (j - i >= 2)
            for (int k = i; k < j; ++k) c.red.insert(k);
        i = j;
    }

    c.markers_gen  = gen;
    c.sample_rate  = sample_rate;
    c.total_frames = total_frames;
    c.valid        = true;
    return c;
}

bool waveform_magnified(const AppState& app) {
    // THE LAMP ALONE DECIDES, in both audio views, on every column and at
    // every zoom: Waveform Magnification lit is magnified, dark (the default
    // at every open) flat. The rule, and why the lamp needs no readiness term
    // of its own, are at the declaration.
    return app.show_waveform_magnification;
}

bool waveform_magnification_toggle_actionable(const GuiAudio& audio) {
    // The rule is at the declaration.
    return audio.gain_curve_ready();
}

uint64_t waveform_gain_fingerprint(const AppState& app) {
    return waveform_magnified(app) ? kWaveformGainVersion : 0;
}

const PhaseResetRedFlagCache& phase_reset_red_flag_set_cached(
    const AppState& app, int sample_rate, long total_frames) {
    PhaseResetRedFlagCache& c = app.phase_reset_red_flag_cache;
    const long long gen = app.phaseresetmarkers.generation();
    // THE KEY'S TWO FIELDS (the declaration's contract): the store's
    // generation and the LIVE map's hash — the lattice the seed index reads.
    // The map is asked first, through its own memo (a key compare on a hit).
    const TargetWarpFrameMapCache& live =
        target_view_warp_frame_map_cached(app, sample_rate, total_frames);
    if (c.valid && c.markers_gen == gen && c.map_hash == live.hash) return c;

    c.red.clear();
    // Participation-blind coincidence BY SEED WINDOW (architect 2026-11-11),
    // the warp set's pass 3 under this column's definition: redden every
    // member of a run of 2+ resets seeding at one schedule index, disabled
    // rows counted. It contains the render's exact-frame collapse
    // (build_phase_reset_source_frames skips disabled resets and collapses a
    // run of 2+ enabled ones on ONE frame to one event) — equal frames seed
    // alike — and is wider than it. The store is time-sorted and the seed
    // index monotone in the frame, so a window's resets are a run of adjacent
    // rows: one seed search a reset.
    const std::vector<GuiPhaseResetMarker>& pr = app.phaseresetmarkers.markers();
    const std::vector<WarpFrameMapSegment>& map = live.warp_frame_map;
    const int n = static_cast<int>(pr.size());
    int i = 0;
    int64_t seed_i =
        n > 0 ? phase_reset_seed_frame_index(pr[0].time_frame, map) : 0;
    while (i < n) {
        int j = i + 1;
        int64_t seed_j = 0;
        while (j < n) {
            seed_j = phase_reset_seed_frame_index(
                pr[static_cast<size_t>(j)].time_frame, map);
            if (seed_j != seed_i) break;
            ++j;
        }
        if (j - i >= 2)
            for (int k = i; k < j; ++k) c.red.insert(k);
        i = j;
        seed_i = seed_j;
    }

    c.markers_gen = gen;
    c.map_hash    = live.hash;
    c.valid       = true;
    return c;
}

// Definition; the descriptive comment lives at the declaration in
// warp_frame_map_view.h. Exposed (non-anonymous) so main.cpp's viewport snap
// in clamp_viewport_start takes its `q` from the same source as the click
// placement — one grid for viewport and markers.
double painter_samples_per_pixel(const AppState& app, const GuiAudio& audio,
                                 const GuiRect& area) {
    if (area.w <= 0) return 0.0;
    return painter_quantized_spp(current_samples_per_pixel(app, audio));
}

int painted_column_of_source_frame_on_basis(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    double vp_start, double spp, int64_t lattice_origin) {
    if (spp <= 0.0) return 0;
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    // The painters' exact shape (frame_to_paint_sample in render.cpp):
    // nearbyint the source frame; in the TargetLive domain forward-map and
    // nearbyint the map output; then place it on the basis's lattice.
    double ms = std::nearbyint(source_frame);
    if (ctx.domain != GuiDisplayDomain::Source && !warp_frame_map.empty()) {
        ms = std::nearbyint(map_source_to_target(ms, warp_frame_map));
    }
    return displayed_column_at(ms, vp_start, spp, lattice_origin);
}

bool source_frame_off_right_edge(const AppState& app, const GuiAudio& audio,
                                 int64_t source_frame) {
    // The rule is at the declaration.
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return false;
    const double q = painter_samples_per_pixel(app, audio, area);
    if (q <= 0.0) return false;
    const int col = painted_column_of_source_frame_on_basis(
        app, audio, static_cast<double>(source_frame),
        displayed_or_live_target_map(app, audio),
        static_cast<double>(max_viewport_start_grid(app, audio)), q,
        // The right wall rests on the frame-0 lattice (the rule at the
        // declaration): its origin is 0.
        /*lattice_origin=*/0);
    return col >= area.w;
}

int64_t phase_reset_lead_in_source_frame(const AppState& a,
                                         const GuiAudio& audio,
                                         int64_t point) {
    // The rule is at the declaration (app_state.h).
    if (a.active_audio_view == 'T') {
        const int64_t lead_in =
            std::max<int64_t>(0, point - kPhaseResetLeadInSamples);
        return active_domain_to_source_frame(a, audio, lead_in);
    }
    // SOURCE VIEW: forward to the target image through the live map, rounded
    // to a whole target frame as the S -> T switch rounds the playhead, the
    // same subtraction, and back to a whole source frame. An empty map (a
    // build the tripwire class refused) is the identity both ways.
    const auto& map = target_view_warp_frame_map_cached(
        a, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).warp_frame_map;
    const double t = std::nearbyint(map_source_to_target(
        static_cast<double>(std::max<int64_t>(0, point)), map));
    const double lead_in = std::max(
        0.0, t - static_cast<double>(kPhaseResetLeadInSamples));
    return snap_authored_frame(map_target_to_source(lead_in, map));
}

int64_t drop_at_playhead_source_frame(const AppState& a,
                                      const GuiAudio& audio) {
    // The rule is at the declaration (app_state.h).
    const int64_t ph = a.playhead_cursor_sample;
    if (a.active_markers_view != 'P')
        return active_domain_to_source_frame(a, audio, ph);
    return phase_reset_lead_in_source_frame(a, audio, ph);
}

bool drop_at_playhead_off_edge(const AppState& a, const GuiAudio& audio) {
    // The rule is at the declaration (app_state.h).
    return source_frame_off_right_edge(
        a, audio, drop_at_playhead_source_frame(a, audio));
}

// THE DROP COINCIDENCE RULE'S TWO TESTS (architect 2026-11-11; the rule, the
// cost and the readers are at the declarations, warp_frame_map_view.h).
namespace {
// DOES A RESET OF `pr` SEED AT SCHEDULE WINDOW `m` under `map` — `frame` a
// source frame seeding there, the caller's own search handed in. The store is
// ascending by time_frame (GuiMarkerStore) and the seed index is monotone in
// the frame, so a reset in `frame`'s window is the store's nearest row on one
// side of `frame` or the other: one lower_bound, at most two seed searches.
// The compare is the seed index itself, the strike's own
// (phase_reset_red_flag_set_cached), so a refused drop and a struck reset
// cannot disagree about a window.
bool phase_reset_store_seeds_at(const std::vector<GuiPhaseResetMarker>& pr,
                                const std::vector<WarpFrameMapSegment>& map,
                                int64_t frame, int64_t m) {
    const auto at = std::lower_bound(
        pr.begin(), pr.end(), frame,
        [](const GuiPhaseResetMarker& r, int64_t f) {
            return r.time_frame < f;
        });
    if (at != pr.end() &&
        phase_reset_seed_frame_index(at->time_frame, map) == m)
        return true;
    return at != pr.begin() &&
           phase_reset_seed_frame_index(std::prev(at)->time_frame, map) == m;
}
} // namespace

bool phase_reset_seeds_in_window_of(const AppState& app, const GuiAudio& audio,
                                    int64_t source_frame) {
    const std::vector<WarpFrameMapSegment>& map =
        live_warp_frame_map(app, audio);
    return phase_reset_store_seeds_at(
        app.phaseresetmarkers.markers(), map, source_frame,
        phase_reset_seed_frame_index(source_frame, map));
}

bool warp_marker_stands_on_frame(const AppState& app, int64_t source_frame) {
    // The store is ascending by time_frame (GuiMarkerStore).
    const std::vector<GuiWarpMarker>& wv = app.warpmarkers.markers();
    const auto at = std::lower_bound(
        wv.begin(), wv.end(), source_frame,
        [](const GuiWarpMarker& m, int64_t f) { return m.time_frame < f; });
    return at != wv.end() && at->time_frame == source_frame;
}

bool drop_at_playhead_coincides(const AppState& a, const GuiAudio& audio) {
    // The rule is at the declaration (app_state.h).
    const int64_t frame = drop_at_playhead_source_frame(a, audio);
    return a.active_markers_view == 'P'
               ? phase_reset_seeds_in_window_of(a, audio, frame)
               : warp_marker_stands_on_frame(a, frame);
}

// THE CROSSING'S GROUP DROP, ONE SELECTED WARP MARKER'S PLACE (the rule is
// at phase_reset_crossing_group_drops' declaration, app_state.h): the frame
// the single drop authors with the playhead standing on the marker — the
// marker's position in the standing audio view's domain, as a click lands
// the playhead on it, through the lead-in drop's one frame — THE SEED WINDOW
// that frame falls in under the live map, and the two reasons the place is
// skipped: a phase reset already seeding in that window (architect
// 2026-11-11, the drop coincidence rule — the single drop's own test, handed
// the window found here), or the single drop's own painted-edge refusal asked
// of that frame.
namespace {
struct CrossingDropPlace {
    int64_t frame    = 0;
    int64_t window   = 0;
    bool    held     = false;
    bool    off_edge = false;
    bool drops() const { return !held && !off_edge; }
};
CrossingDropPlace crossing_drop_place(const AppState& a, const GuiAudio& audio,
                                      int64_t marker_frame) {
    CrossingDropPlace p;
    p.frame = phase_reset_lead_in_source_frame(
        a, audio, source_frame_to_active_domain(a, audio, marker_frame));
    const std::vector<WarpFrameMapSegment>& map = live_warp_frame_map(a, audio);
    p.window = phase_reset_seed_frame_index(p.frame, map);
    p.held = phase_reset_store_seeds_at(a.phaseresetmarkers.markers(), map,
                                        p.frame, p.window);
    // A held place wants no drop, so the edge is asked only of a place the
    // act would write — the single drop's own order, the occupied place
    // before the painted edge.
    p.off_edge = !p.held && source_frame_off_right_edge(a, audio, p.frame);
    return p;
}
// One dropping place of the plan, before the windows are merged.
struct CrossingDropCandidate {
    int64_t window = 0;
    int64_t frame  = 0;
    bool    focus  = false;
};
} // namespace

PhaseResetCrossingDrops phase_reset_crossing_group_drops(
        const AppState& a, const GuiAudio& audio) {
    PhaseResetCrossingDrops out;
    const std::vector<GuiWarpMarker>& wv = a.warpmarkers.markers();
    const int n = static_cast<int>(wv.size());
    std::vector<CrossingDropCandidate> places;
    places.reserve(a.selected_markers.size());
    for (const int idx : a.selected_markers) {
        // The stale-index belt, the selection verbs' one policy: skipped.
        if (idx < 0 || idx >= n) continue;
        const CrossingDropPlace p = crossing_drop_place(
            a, audio, wv[static_cast<std::size_t>(idx)].time_frame);
        if (p.off_edge) out.off_edge = true;
        if (!p.drops()) continue;
        places.push_back(CrossingDropCandidate{
            p.window, p.frame, idx == a.last_selected_marker});
    }
    // ONE RESET A WINDOW (architect 2026-11-11: no drop makes a coincidence,
    // the plan's own places included): selected markers whose places share a
    // seed window drop ONE reset there, AT THE WARP FOCUS'S PLACE where the
    // focus is among them — so `focus_frame` names a created reset and the
    // focus and the playhead land on it — else at the earliest. Sorted by
    // window, the focus first inside one, then by frame; the first of each
    // window is kept. Windows ascend with frames (the seed index is monotone),
    // so `frames` comes out ascending and distinct.
    std::sort(places.begin(), places.end(),
              [](const CrossingDropCandidate& l,
                 const CrossingDropCandidate& r) {
                  if (l.window != r.window) return l.window < r.window;
                  if (l.focus != r.focus) return l.focus;
                  return l.frame < r.frame;
              });
    bool    have_window = false;
    int64_t last_window = 0;
    for (const CrossingDropCandidate& c : places) {
        if (have_window && c.window == last_window) continue;
        have_window = true;
        last_window = c.window;
        out.frames.push_back(c.frame);
        if (c.focus) out.focus_frame = c.frame;
    }
    return out;
}

bool phase_reset_crossing_group_would_drop(const AppState& a,
                                           const GuiAudio& audio) {
    const std::vector<GuiWarpMarker>& wv = a.warpmarkers.markers();
    const int n = static_cast<int>(wv.size());
    for (const int idx : a.selected_markers) {
        if (idx < 0 || idx >= n) continue;
        if (crossing_drop_place(
                a, audio, wv[static_cast<std::size_t>(idx)].time_frame)
                .drops())
            return true;
    }
    return false;
}

int64_t authored_frame_at_column_on_basis(
    const AppState& app, const GuiAudio& audio, int col,
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    int64_t vp_start, double spp, int64_t lattice_origin) {
    if (spp <= 0.0) return 0;
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    // The column's ACTIVE-DOMAIN time on the single-rounding grid (the
    // lattice-snapped viewport is a true point of its own lattice, so the
    // recovered column index is exact). Both arms below land on it, so a
    // commit is anchored at the basis's lattice — frame 0 of the domain it was
    // authored in at every landing, the painted column's edge under a
    // gesture's rest — rather than at the basis's viewport start.
    const double g = displayed_grid_position_at_column(vp_start, col, spp,
                                                       lattice_origin);
    if (ctx.domain == GuiDisplayDomain::Source) {
        // snap_authored_frame stays the sole double-to-authored conversion.
        return snap_authored_frame(g);
    }
    if (!warp_frame_map.empty()) {
        // Target view: quantize the column's target-domain time to an integer
        // target frame (floored at 0), then inverse-map at full precision; the
        // map is monotone increasing, so the target-domain direction is the
        // source-domain direction. That time is the grid position above, so a
        // phase-reset nudge, drag or sweep commit is independent of the
        // viewport's own index at a fixed q and origin exactly like the source
        // arm's — not of the origin itself, a gesture rest's being the
        // camera's (this arm spelled the two-rounding
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
// context's OWN map — the live context's, in both directions.
// Sites translating against an explicit caller-supplied map
// (a proposed pre-commit marker list) use the explicit-map pixel-anchoring
// helpers instead. The empty-map path (the unbuildable-target fallthrough)
// stays identity — map_source_to_target / map_target_to_source are identity on
// an empty map.
int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::Source) return source_frame;
    const size_t q = (source_frame < 0)
        ? static_cast<size_t>(0)
        : static_cast<size_t>(source_frame);
    return static_cast<int64_t>(
        std::nearbyint(map_source_to_target(q, *ctx.warp_frame_map)));
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
// The contracts (the lockstep warning, the translation rule and the wall
// rules) are at the declarations in warp_frame_map_view.h; what
// follows is the arithmetic.

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

int64_t phase_reset_hop_step_frame(
    int64_t reset_source_frame, int k,
    const std::vector<WarpFrameMapSegment>& map) {
    // The identity cell is the resting store itself, byte for byte — no
    // arithmetic, so no rounding can move a reset the user did not ask to move.
    if (k == 0) return reset_source_frame;
    const int64_t m0 = phase_reset_seed_frame_index(reset_source_frame, map);
    const int64_t m  = m0 + k;
    // NO FRAME OF THE PIECE SEEDS BELOW WINDOW 0 (every authored frame >= 0
    // seeds at m >= 0), so a step that far left answers the frame just below
    // the piece: the walls read it as off the piece and the nudge's clamp
    // lands it on frame 0. It is answered explicitly because the map clamps
    // every negative target to its first source frame, which would otherwise
    // read as a landing inside the piece.
    if (m < 0) return -1;
    // THE TRANSLATION: k hops of the target domain, taken from the reset's own
    // target image, so its offset inside its window travels with it.
    const double t = map_source_to_target(
        static_cast<double>(reset_source_frame), map);
    const double t_landed = t + static_cast<double>(k) * static_cast<double>(kRs);
    const int64_t landed = snap_authored_frame(map_target_to_source(t_landed, map));
    // THE MEANING WINS OVER THE ROUNDING: the frames seeding at m are
    // [C(m), C(m+1) - 1], and the landing is clamped into them so the step
    // changes the seed by exactly k. The interval is never empty on a real
    // map (a hop is at least kRs/16 = 64 source frames under the value
    // brackets); the max is a belt that answers C(m) if it ever were.
    const int64_t lo = phase_reset_window_centre_frame(m, map);
    const int64_t hi =
        std::max(lo, phase_reset_window_centre_frame(m + 1, map) - 1);
    return std::clamp(landed, lo, hi);
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

    // THE TWO WALLS THAT STAND ARE THE PIECE'S EDGES AND THE DIGIT (architect
    // 2026-09-11). A neighbouring reset is NOT a wall: two adjacent ranges may
    // cross or meet, the sweep sorts each cell's vector before the request, and
    // the ear is what catches a contortion — the same latitude coincident
    // markers have at rest. Iteration is wanted in quiet sections with few
    // resets, and a user who puts two ranges within a few hops of each other is
    // doing it deliberately.
    //
    // The two walks, outward from the identity cell, stopping at the first k
    // that breaks a wall. At most kIterHopMax landings a side, each one seed
    // search over a monotone map. Each records the INTERVAL alone: the loop's
    // own bound is the digit wall and the break is the piece edge, and no
    // reader asks which of the two closed a side (the struct says why).
    for (int k = 1; k <= kIterHopMax; ++k) {
        const int64_t f = phase_reset_hop_step_frame(rest, k, map);
        if (f > last_frame) break;
        out.k_max = k;
    }
    for (int k = -1; k >= -kIterHopMax; --k) {
        const int64_t f = phase_reset_hop_step_frame(rest, k, map);
        if (f < 0) break;
        out.k_min = k;
    }
    return out;
}
