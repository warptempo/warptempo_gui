#include "warp_frame_map_view.h"

#include "app_state.h"
#include "audio.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// GUI target-view warp_frame_map helpers, split out of warp_frame_map_build.cpp so the
// build core (resolve_warp_markers_for_render / build_warp_frame_map / the
// phase-reset assembly) carries no AppState dependency and lives in
// the parser sources. These helpers stay GUI-side: they read the
// live AppState marker store and the active-view selector, and own the
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
    // point for hypothetical (non-live) marker lists; live-state consumers go
    // through target_view_warp_frame_map_cached.
    // A resolve failure (first-marker render grammar, tie, dangling label
    // ref) or a build failure returns the empty map and reports the message
    // through error_out. The empty map still paints as identity for the
    // frame or two it stays displayed, but it is no longer a silent
    // dead-end: the cache records the error and the target-view validity
    // gate (GuiInputHandler::enforce_target_view_validity) kicks back to
    // source view on the next tick and opens the defect-resolution series
    // (or the error-notice popup for the non-modeled class).
    if (error_out) error_out->clear();
    auto resolved = resolve_warp_markers_for_render(
        slice_to_warp_markers(markers), sample_rate);
    if (!resolved) {
        if (error_out) *error_out = std::move(resolved.error());
        return {};
    }
    auto r = build_warp_frame_map(
        *resolved, scale, sample_rate, total_frames);
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
    if (c.warp_frame_map.empty()) {
        c.tgt_total_frames = static_cast<int64_t>(total_frames);
    } else {
        const double t = map_source_to_target(
            static_cast<size_t>(total_frames < 0 ? 0 : total_frames),
            c.warp_frame_map);
        const int64_t tt = static_cast<int64_t>(std::nearbyint(t));
        c.tgt_total_frames =
            tt > 0 ? tt : static_cast<int64_t>(total_frames);
    }
    c.markers_gen  = gen;
    c.scale        = scale;
    c.sample_rate  = sample_rate;
    c.total_frames = total_frames;
    c.valid        = true;
    return c;
}

int64_t to_source_frame(const AppState& app, int64_t domain_frame,
                        const std::vector<WarpFrameMapSegment>& warp_frame_map) {
    if (app.active_audio_view == 'S') return domain_frame;
    const size_t q = (domain_frame < 0)
        ? static_cast<size_t>(0)
        : static_cast<size_t>(domain_frame);
    return static_cast<int64_t>(
        std::nearbyint(map_target_to_source(q, warp_frame_map)));
}

int64_t to_domain_frame(const AppState& app, int64_t source_frame,
                        const std::vector<WarpFrameMapSegment>& warp_frame_map) {
    if (app.active_audio_view == 'S') return source_frame;
    const size_t q = (source_frame < 0)
        ? static_cast<size_t>(0)
        : static_cast<size_t>(source_frame);
    return static_cast<int64_t>(
        std::nearbyint(map_source_to_target(q, warp_frame_map)));
}

int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame) {
    if (app.active_audio_view == 'S') return source_frame;
    const auto& target_warp_frame_map = target_view_warp_frame_map_cached(
        app, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).warp_frame_map;
    return to_domain_frame(app, source_frame, target_warp_frame_map);
}

int64_t active_domain_to_source_frame(const AppState& app, const GuiAudio& audio,
                                      int64_t domain_frame) {
    if (app.active_audio_view == 'S') return domain_frame;
    const auto& target_warp_frame_map = target_view_warp_frame_map_cached(
        app, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).warp_frame_map;
    return to_source_frame(app, domain_frame, target_warp_frame_map);
}
