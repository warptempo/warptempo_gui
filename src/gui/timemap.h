#pragma once

#include "warpmarkers.h"
#include "engine/stft_container.h"   // FrameMapSegment

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// In-memory timemap-generation used by the engine. Math is organized as
// Pass 1, Pass 2, and a trim post-pass.

struct TempomapEntry {
    double target_time_sec;
    double multiplier;
};

// Minimal POD the frame_map math needs. The GUI's `GuiWarpMarker` resolves into
// this: tempo_inherits markers are walked back to their nearest owning
// ancestor and their effective tempo_base / tempo_scale are copied forward.
// Disabled markers (and any references to disabled-defined labels) are
// filtered out BEFORE conversion.
struct MarkerForRender {
    double      time_seconds = 0.0;
    double      tempo_base   = 1.0;   // resolved owning tempo; irrelevant for label_ref
    std::string tempo_scale;           // "" or the numeric string after '*'
    std::string label_def;
    std::string label_ref;
};

struct TimemapBuildInput {
    std::vector<MarkerForRender> markers;

    double scale        = 1.0;   // from settings; 1.0 default
    long   sample_rate  = 0;     // from the source audio file
    long   total_frames = 0;     // from the source audio file

    // Settings-side trim, lifted out of warp markers (brief: move trim
    // from marker b=/e= flags to project settings). When has_trim_begin
    // is false, no begin trim is applied; same for end. Times are in
    // seconds, matching the .settings file representation.
    bool   has_trim_begin = false;
    double trim_begin_sec = 0.0;
    bool   has_trim_end   = false;
    double trim_end_sec   = 0.0;
};

struct TimemapBuildResult {
    std::vector<FrameMapSegment> standard;
    std::vector<TempomapEntry>  midi;

    // Populated when TimemapBuildInput carries trim_begin / trim_end.
    bool   trimmed          = false;
    size_t trim_begin_frame = 0;
    size_t trim_end_frame   = 0;   // exclusive; == total_frames if no end

    // True when the post-pass injected a synthetic begin/end entry into
    // standard (and, for end, midi) because the trim boundary did not
    // align with a real warp marker. Engine- and adapter-facing consumers
    // read these anchors as valid waypoints; the GUI-facing render sidecar
    // walk strips them via real_segments() below.
    bool   has_trim_begin_anchor = false;
    bool   has_trim_end_anchor   = false;
};

// Iterator range over the "real" segments of a built frame_map — i.e.
// tmres.standard with the synthetic trim anchors (if any) excluded at
// both ends. Used by the render sidecar lockstep walk so injected
// anchors do not surface as ghost markers in render-view.
struct FrameMapRealRange {
    std::vector<FrameMapSegment>::const_iterator begin;
    std::vector<FrameMapSegment>::const_iterator end;
};
FrameMapRealRange real_segments(const TimemapBuildResult& r);

// Returns true on success; false on any validation failure (message logged
// to stderr). Failure conditions: tempo > 9.99, tempo <= 0,
// src_frame > total_frames, src_frame - prev_src_frame < 1, undefined label
// reference, duplicate label definition, final_multiplier > 9.9999 on label
// refs, begin_time at 00:00.000.
bool build_timemaps(const TimemapBuildInput& in, TimemapBuildResult& out);

// Resolve each GuiWarpMarker to a MarkerForRender. Filters out markers that
// are references to disabled-defined labels and disabled label-definition
// markers (and thereby all refs to them). The inherit walk-back is applied
// here so MarkerForRender carries a concrete tempo_base / tempo_scale —
// same rule as resolve_inherited_tempo. Both the engine-bound render
// pipeline and the target view's per-paint frame_map recompute go through
// this single resolver so the visible deformity matches what the engine
// would emit.
std::vector<MarkerForRender> resolve_markers_for_render(
    const std::vector<GuiWarpMarker>& src);

// Builds the target-view frame_map from live warp markers plus scale, mirroring
// the resolve-then-build pipeline paint_handler's on_redraw uses for target-
// view waveform translation. Trim is forced off — target view paints the
// WHOLE song; the frame_map must describe the whole song with warp segments
// where markers exist and identity outside (see paint_handler.cpp commentary
// next to the same construction). Returns an empty vector if build_timemaps
// fails or yields no segments. Callers in target view route this through
// compute_flag_hit_rects / render_flags / popup-hit helpers so hit-test
// math and paint stay in sync.
std::vector<FrameMapSegment> build_target_view_frame_map(
    const std::vector<GuiWarpMarker>& markers,
    double scale,
    int sample_rate,
    long total_frames);

// AppState-driven overload of build_target_view_frame_map. Pulls the live
// warp marker store and engine_settings.scale from `app`; otherwise
// identical to the markers-and-scale overload above. Both input and
// paint paths in target view route through this helper so the segment
// list they walk is byte-identical. Defined in timemap.cpp; an
// AppState forward declaration suffices here.
struct AppState;
std::vector<FrameMapSegment> build_target_view_frame_map(
    const AppState& app, int sample_rate, long total_frames);

// Memoized target-view frame_map. One entry, keyed on the inputs that
// determine the map: the warp-marker store generation, the scale
// setting, and the audio identity (sample rate, total frames). The
// entry also carries the FNV-1a hash of the segment list, computed at
// rebuild, so the waveform-cache fingerprint reads it instead of
// rehashing per tick. A failed or empty build is cached too (empty
// frame_map, hash 0) — callers already treat an empty map as identity.
struct TargetTimemapCache {
    bool      valid        = false;
    long long markers_gen  = -1;
    double    scale        = 0.0;
    int       sample_rate  = 0;
    long      total_frames = 0;
    std::vector<FrameMapSegment> frame_map;
    uint64_t  hash         = 0;

    // Deformed-timeline length: the source total forward-translated
    // through this frame_map (the same formula the S-to-T toggle used).
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
const TargetTimemapCache& target_view_timemap_cached(
    const AppState& app, int sample_rate, long total_frames);

// Inverse-translate a domain-frame coordinate (active-domain) into a
// source-frame coordinate. In source view this is identity. In target
// view this routes through `map_target_to_source` against the supplied
// frame_map. Banker's rounding to integer. Used at every input boundary
// in target view where a pixel-derived sample coordinate (playhead,
// click position, drag anchor / motion) becomes a source-frame value
// written into a marker / trim / phase reset store.
int64_t to_source_frame(const AppState& app, int64_t domain_frame,
                        const std::vector<FrameMapSegment>& frame_map);

// Forward-translate a source-frame coordinate (e.g. a stored marker
// time) into the active domain's frame coordinates. Source view:
// identity. Target view: `map_source_to_target`. Used by handlers that
// need to position the viewport / playhead at a source-domain anchor
// while in the active domain (e.g. Tab cycling recentering on a marker
// whose time_seconds is source-domain).
int64_t to_domain_frame(const AppState& app, int64_t source_frame,
                        const std::vector<FrameMapSegment>& frame_map);

// libsndfile-based slice: reads src_path samples [begin_frame, end_frame)
// and writes them to out_path as 32-bit float WAV preserving channel count
// and sample rate. Returns true on success; false (with stderr log) on any
// sndfile error. No sox dependency.
bool write_trimmed_wav(const std::string& src_path,
                       const std::string& out_path,
                       size_t begin_frame,
                       size_t end_frame);

// Reads samples in [begin_frame, end_frame) from src_path into out_samples
// as interleaved 32-bit float, and reports the source sample rate and
// channel count. Used by render_pipeline.cpp to populate the warptempo
// engine's in-memory source buffer (replaces the wav-on-disk trim shim).
// Returns true on success; false with stderr log on any sndfile error.
bool load_source_range_to_buffer(const std::string& src_path,
                                 size_t begin_frame,
                                 size_t end_frame,
                                 std::vector<float>& out_samples,
                                 int& out_sample_rate,
                                 int& out_channels);
