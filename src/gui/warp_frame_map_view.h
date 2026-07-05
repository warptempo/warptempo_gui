#pragma once

#include "warp_frame_map_build.h"
#include "warpmarkers.h"   // GuiWarpMarker (the view-overload signature)

#include <cstdint>
#include <vector>

// Builds the target-view warp_frame_map from live warp markers plus scale, mirroring
// the resolve-then-build pipeline paint_handler's on_redraw uses for target-
// view waveform translation. Trim is forced off — target view paints the
// WHOLE song; the warp_frame_map must describe the whole song with warp segments
// where markers exist and identity outside (see paint_handler.cpp commentary
// next to the same construction). Returns an empty vector if build_warp_maps
// fails or yields no segments. Callers in target view route this through
// compute_flag_hit_rects / render_flags / popup-hit helpers so hit-test
// math and paint stay in sync.
std::vector<WarpFrameMapSegment> build_target_view_warp_frame_map(
    const std::vector<GuiWarpMarker>& markers,
    double scale,
    int sample_rate,
    long total_frames);

struct AppState;

// Memoized target-view warp_frame_map. One entry, keyed on the inputs that
// determine the map: the warp-marker store generation, the scale
// setting, and the audio identity (sample rate, total frames). The
// entry also carries the FNV-1a hash of the segment list, computed at
// rebuild, so the waveform-cache fingerprint reads it instead of
// rehashing per tick. A failed or empty build is cached too (empty
// warp_frame_map, hash 0) — callers already treat an empty map as identity.
struct TargetWarpMapCache {
    bool      valid        = false;
    long long markers_gen  = -1;
    double    scale        = 0.0;
    int       sample_rate  = 0;
    long      total_frames = 0;
    std::vector<WarpFrameMapSegment> warp_frame_map;
    uint64_t  hash         = 0;

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
const TargetWarpMapCache& target_view_map_cached(
    const AppState& app, int sample_rate, long total_frames);

// Inverse-translate a domain-frame coordinate (active-domain) into a
// source-frame coordinate. In source view this is identity. In target
// view this routes through `map_target_to_source` against the supplied
// warp_frame_map. Banker's rounding to integer. Used at every input boundary
// in target view where a pixel-derived sample coordinate (playhead,
// click position, drag anchor / motion) becomes a source-frame value
// written into a marker / trim / phase reset store.
int64_t to_source_frame(const AppState& app, int64_t domain_frame,
                        const std::vector<WarpFrameMapSegment>& warp_frame_map);

// Forward-translate a source-frame coordinate (e.g. a stored marker
// time) into the active domain's frame coordinates. Source view:
// identity. Target view: `map_source_to_target`. Used by handlers that
// need to position the viewport / playhead at a source-domain anchor
// while in the active domain (e.g. Tab cycling recentering on a marker
// whose time_seconds is source-domain).
int64_t to_domain_frame(const AppState& app, int64_t source_frame,
                        const std::vector<WarpFrameMapSegment>& warp_frame_map);

// Convenience wrappers that own the view-check and the cached map build for the
// common case: translating a single coordinate between the source-frame domain
// and the active-domain using the LIVE target-view map. Source view: identity,
// no map built. Target view: routes through the memoized
// target_view_map_cached, so even repeated calls (e.g. inside a loop) cost
// only a cache-key comparison after the first build. Use these at every input /
// playhead boundary that currently reads target_view_map_cached
// solely to feed one to_domain_frame / to_source_frame.
//
// NOT for sites translating against a non-live map — a drag's
// app.drag.frozen_warp_frame_map, or a proposed (pre-commit) marker list. Those keep
// calling to_domain_frame / to_source_frame directly with their explicit map.
class GuiAudio;
int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame);
int64_t active_domain_to_source_frame(const AppState& app, const GuiAudio& audio,
                                      int64_t domain_frame);
