#pragma once

#include "warpmarkers_parse.h"          // WarpMarker
#include "phase_reset_markers_parse.h"  // PhaseResetMarker
#include "frame_map.h"                  // FrameMapSegment

#include <cstddef>
#include <cstdint>
#include <expected>
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

// Returns the built TimemapBuildResult on success, or std::unexpected carrying
// the first violated condition (a concise lowercase reason; callers add their
// own context prefix). Does not log. Failure conditions, in check order:
// invalid source audio metadata (sample_rate <= 0 or total_frames <= 0),
// src_frame > total_frames, src_frame - prev_src_frame < 1, tempo > 9.99,
// tempo <= 0, duplicate label definition, undefined label reference,
// final_multiplier > 9.9999 on label refs.
std::expected<TimemapBuildResult, std::string> build_timemaps(
    const TimemapBuildInput& in);

// Resolve each WarpMarker to a MarkerForRender. Callers in the GUI slice
// their GuiWarpMarker store to std::vector<WarpMarker> first (the resolver
// is parser-domain and reads no GUI-only fields). Filters out markers that
// are references to disabled-defined labels and disabled label-definition
// markers (and thereby all refs to them). The inherit walk-back is applied
// here so MarkerForRender carries a concrete tempo_base / tempo_scale —
// same rule as resolve_inherited_tempo. Both the engine-bound render
// pipeline and the target view's frame_map recompute go through this single
// resolver so the visible deformity matches what the engine would emit.
std::vector<MarkerForRender> resolve_markers_for_render(
    const std::vector<WarpMarker>& src);

// Backward inheritance walk over parser-domain markers: from `index`, scan
// earlier markers for the nearest that OWNS its tempo — tempo_inherits ==
// false, not a label reference, and not disabled. Disabled markers are skipped
// because the engine drops them before resolution, so a disabled marker
// contributes no tempo downstream. Returns 1.0 (tempo) / "" (scale) if none is
// found. This is the single canonical inheritance walk: resolve_markers_for_render
// and compute_hover_popup_text both call it, so the popup display always
// matches the tempo the engine resolves.
double resolve_inherited_tempo(const std::vector<WarpMarker>& markers, int index);
std::string resolve_inherited_tempo_scale(
    const std::vector<WarpMarker>& markers, int index);

// Hover-popup text for a warp marker (the label-ref / pass tempo notice). Pure
// parser-domain string/math — computes the same resolution the engine uses
// when emitting the framemap, so the popup matches what will be rendered. Pass
// markers emit "= TEMPO" or "= TEMPO*SCALE" (resolved tempo of the nearest
// prior owning marker). Label_ref markers emit "~= BASE*COMBINED_SCALE" (BASE
// at 2 decimals; COMBINED_SCALE = def_scale * multiplier when the def has a
// typed scale, else multiplier, at 4 decimals). Returns "" when the marker
// does not qualify (owning, missing def, malformed). GUI callers slice their
// GuiWarpMarker store to WarpMarker (slice_to_warp_markers) before calling.
std::string compute_hover_popup_text(
    const std::vector<WarpMarker>& mv, int idx, int sample_rate);

// Pure parser-domain assembly: phase-reset markers -> absolute source frames.
// Drops disabled markers; converts time_seconds to a source frame via
// nearbyint(time * sample_rate), matching the warp-marker time->frame
// convention. The result is the undisplaced list used both for render-view
// display and as the input to displace_phase_reset_frames.
std::vector<int64_t> phase_reset_source_frames(
    const std::vector<PhaseResetMarker>& markers, long sample_rate);

// Pure: displace absolute source-frame resets to the engine's dispatch
// frame by subtracting the locked one-hop offset, clamped at 0. The clamp is
// silent here (no I/O); callers that want the per-reset "clamped before audio
// start" notice detect it by source_frame < offset_samples. This is the full
// displaced reset list the engine path hands over (and Brief 6 will slice).
std::vector<int64_t> displace_phase_reset_frames(
    const std::vector<int64_t>& source_frames, int64_t offset_samples);

// Result of slicing the full untrimmed frame map to a trim window: the
// re-anchored sub-map the engine renders, the output offset of the window
// origin (wbegin * R_s) for the render-view marker sidecar, and the
// output-sample cap the engine truncates the emitted render at (the trim-end
// target, re-anchored). The sub-map extends to the first real anchor at or
// past trim_end_src so the final segment carries the full map's exact slope;
// emit_sample_cap then cuts the output back to the trim boundary.
struct WindowedFrameMap {
    std::vector<FrameMapSegment> frame_map;     // source absolute; target re-anchored to 0
    int64_t                      window_offset_samples = 0;
    int64_t                      emit_sample_cap       = 0;  // trim-end output length
};

// Slice the full untrimmed map to the synthesis-frame window covering
// [trim_begin_src, trim_end_src], reproducing the engine's former internal
// trim window as a standalone sub-map. The window start is selected against the
// dense synthesis-frame schedule (the same generate_source_frame_positions
// logic the engine used), so the slice lands on a synthesis-frame boundary, NOT
// a source-frame boundary. Target is shifted by -wbegin*R_s (a rigid integer
// translation, so interior segment lines are preserved exactly); source stays
// absolute. The sub-map TERMINATES on the first full-map anchor whose source is
// at or past trim_end_src (the anchor closing the segment that contains
// trim_end_src), target-shifted — NOT on a synthetic rounded anchor at
// trim_end_src — so the final segment carries the full map's exact slope. The
// returned emit_sample_cap is the trim-end target minus the window offset; the
// engine emits only up to it, so the few frames between trim_end_src and the
// closing anchor are synthesized into the truncated tail only. The returned map
// must be strictly monotonic in both axes (engine validate_frame_map_monotonic
// rejects it otherwise). Caller invokes this only when a trim bound is set.
WindowedFrameMap slice_frame_map_to_trim_window(
    const std::vector<FrameMapSegment>& full_map,
    int64_t trim_begin_src, int64_t trim_end_src,
    int N, int R_s);

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
