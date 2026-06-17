#include "timemap.h"

#include "app_state.h"

#include <cmath>
#include <cstdint>
#include <vector>

// GUI target-view frame_map helpers, split out of timemap.cpp so the
// build core (resolve_markers_for_render / build_timemaps / the
// phase-reset assembly) carries no AppState dependency and can move to
// libwarptempo_parser in Brief 9. These helpers stay GUI-side: they read the
// live AppState marker store and the active-view selector, and own the
// memoized target-view cache. Declarations remain in timemap.h.

std::vector<FrameMapSegment> build_target_view_frame_map(
    const std::vector<GuiWarpMarker>& markers,
    double scale,
    int sample_rate,
    long total_frames) {
    TimemapBuildInput tmin;
    tmin.markers        = resolve_markers_for_render(slice_to_warp_markers(markers));
    tmin.scale          = scale;
    tmin.sample_rate    = sample_rate;
    tmin.total_frames   = total_frames;
    // Trim is render-time, not view-time — target view paints the WHOLE
    // song. Forcing trim off here matches the paint_handler construction
    // so hit-test math and waveform paint walk the same segment list.
    // This overload always builds directly and is the entry point for
    // hypothetical (non-live) marker lists; live-state consumers go
    // through the cache via the AppState overload.
    tmin.has_trim_begin = false;
    tmin.trim_begin_sec = 0.0;
    tmin.has_trim_end   = false;
    tmin.trim_end_sec   = 0.0;
    std::vector<FrameMapSegment> out;
    auto r = build_timemaps(tmin);
    if (!r) return out;
    const TimemapBuildResult& tmres = *r;
    out.reserve(tmres.standard.size());
    for (const auto& s : tmres.standard) {
        out.push_back(FrameMapSegment{s.src_frame, s.tgt_frame});
    }
    return out;
}

const TargetTimemapCache& target_view_timemap_cached(
    const AppState& app, int sample_rate, long total_frames) {
    TargetTimemapCache& c = app.target_timemap_cache;
    const long long gen = app.warpmarkers.generation();
    const double scale  = app.engine_settings.scale;
    if (c.valid && c.markers_gen == gen && c.scale == scale &&
        c.sample_rate == sample_rate && c.total_frames == total_frames) {
        return c;
    }
    c.frame_map = build_target_view_frame_map(
        app.warpmarkers.markers(), scale, sample_rate, total_frames);
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const auto& s : c.frame_map) {
        h ^= static_cast<uint64_t>(s.src_frame);
        h *= 0x100000001b3ULL;
        h ^= static_cast<uint64_t>(s.tgt_frame);
        h *= 0x100000001b3ULL;
    }
    c.hash         = c.frame_map.empty() ? 0 : h;
    if (c.frame_map.empty()) {
        c.tgt_total_frames = static_cast<int64_t>(total_frames);
    } else {
        const double t = map_source_to_target(
            static_cast<size_t>(total_frames < 0 ? 0 : total_frames),
            c.frame_map);
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

std::vector<FrameMapSegment> build_target_view_frame_map(
    const AppState& app, int sample_rate, long total_frames) {
    return target_view_timemap_cached(app, sample_rate, total_frames).frame_map;
}

int64_t to_source_frame(const AppState& app, int64_t domain_frame,
                        const std::vector<FrameMapSegment>& frame_map) {
    if (app.active_audio_view == 'S') return domain_frame;
    const size_t q = (domain_frame < 0)
        ? static_cast<size_t>(0)
        : static_cast<size_t>(domain_frame);
    return static_cast<int64_t>(
        std::nearbyint(map_target_to_source(q, frame_map)));
}

int64_t to_domain_frame(const AppState& app, int64_t source_frame,
                        const std::vector<FrameMapSegment>& frame_map) {
    if (app.active_audio_view == 'S') return source_frame;
    const size_t q = (source_frame < 0)
        ? static_cast<size_t>(0)
        : static_cast<size_t>(source_frame);
    return static_cast<int64_t>(
        std::nearbyint(map_source_to_target(q, frame_map)));
}
