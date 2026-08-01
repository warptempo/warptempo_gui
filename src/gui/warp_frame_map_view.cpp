#include "warp_frame_map_view.h"

#include "app_state.h"
#include "audio.h"
#include "gui_display_context.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <set>
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
    const std::vector<char> members = warp_coincident_collapse_members(mv);
    for (int k = 0; k < n; ++k)
        if (members[static_cast<size_t>(k)]) c.red.insert(k);

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
    const double x_raw = (ms - vp_start) / spp;
    return static_cast<int>(std::nearbyint(x_raw));
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
    const double t_active =
        static_cast<double>(app.viewport_start_sample) +
        static_cast<double>(col) * spp;
    if (ctx.domain == GuiDisplayDomain::Source) {
        // Exact source grid: one rounding via the recovered viewport column
        // (the grid-snapped viewport is a true grid point), so the stored
        // frame is the single-rounding grid position nearbyint((m+col)*q)
        // rather than the two-rounding nearbyint(viewport_start + col*q).
        // snap_authored_frame stays the sole double-to-authored conversion.
        return snap_authored_frame(
            source_grid_position_at_column(app.viewport_start_sample, col, spp));
    }
    if (!warp_frame_map.empty()) {
        // Target view: quantize the column's target-domain time to an integer
        // target frame (floored at 0), then inverse-map at full precision; the
        // map is monotone increasing, so the target-domain direction is the
        // source-domain direction.
        const double q = (t_active < 0.0)
            ? 0.0
            : static_cast<double>(std::llrint(t_active));
        return snap_authored_frame(map_target_to_source(q, warp_frame_map));
    }
    return snap_authored_frame(t_active);
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

// Translate through the active display context. A Source-domain context
// is identity outright; the TargetLive domain inlines the forward / inverse
// map math (map_source_to_target / map_target_to_source) against the
// context's OWN map. Sites translating against an explicit caller-supplied map
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
