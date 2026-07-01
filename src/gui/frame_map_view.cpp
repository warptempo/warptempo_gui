#include "frame_map_view.h"

#include "app_state.h"
#include "audio.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

// GUI target-view frame_map helpers, split out of frame_map_build.cpp so the
// build core (resolve_markers_for_render / build_maps / the
// phase-reset assembly) carries no AppState dependency and lives in
// libwarptempo_parser. These helpers stay GUI-side: they read the
// live AppState marker store and the active-view selector, and own the
// memoized target-view cache. Declarations remain in frame_map_view.h.

std::vector<FrameMapSegment> build_target_view_frame_map(
    const std::vector<GuiWarpMarker>& markers,
    double scale,
    int sample_rate,
    long total_frames) {
    MapBuildInput tmin;
    tmin.markers        = resolve_markers_for_render(slice_to_warp_markers(markers));
    tmin.scale          = scale;
    tmin.sample_rate    = sample_rate;
    tmin.total_frames   = total_frames;
    // Trim is render-time, not view-time — target view paints the WHOLE
    // song. Forcing trim off here matches the paint_handler construction
    // so hit-test math and waveform paint walk the same segment list.
    // This overload always builds directly and is the entry point for
    // hypothetical (non-live) marker lists; live-state consumers go
    // through target_view_map_cached.
    tmin.has_trim_begin = false;
    tmin.trim_begin_sec = 0.0;
    tmin.has_trim_end   = false;
    tmin.trim_end_sec   = 0.0;
    std::vector<FrameMapSegment> out;
    auto r = build_maps(tmin);
    if (!r) {
        return out;
    }
    const MapBuildResult& tmres = *r;
    out.reserve(tmres.frame_map.size());
    for (const auto& s : tmres.frame_map) {
        out.push_back(FrameMapSegment{s.src_frame, s.tgt_frame});
    }
    return out;
}

const TargetMapCache& target_view_map_cached(
    const AppState& app, int sample_rate, long total_frames) {
    TargetMapCache& c = app.target_map_cache;
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
        h ^= std::bit_cast<uint64_t>(s.src_frame);
        h *= 0x100000001b3ULL;
        h ^= std::bit_cast<uint64_t>(s.tgt_frame);
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

int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame) {
    if (app.active_audio_view == 'S') return source_frame;
    const auto& tmap = target_view_map_cached(
        app, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).frame_map;
    return to_domain_frame(app, source_frame, tmap);
}

int64_t active_domain_to_source_frame(const AppState& app, const GuiAudio& audio,
                                      int64_t domain_frame) {
    if (app.active_audio_view == 'S') return domain_frame;
    const auto& tmap = target_view_map_cached(
        app, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).frame_map;
    return to_source_frame(app, domain_frame, tmap);
}
