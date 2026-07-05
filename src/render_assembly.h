#pragma once

#include "engine/engine.h"      // EngineParams
#include "engine/engine_geometry.h"  // phase_reset_offset_samples
#include "frame_map.h"          // FrameMapSegment
#include "frame_map_build.h"    // WindowedFrameMap, slice_frame_map_to_trim_window
#include "phase_reset_dispatch.h"  // phase_reset_dispatch_frames_target_domain

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Single source of truth for populating EngineParams.frame_map and
// emit_sample_cap from the full untrimmed standard map. With a trim bound set,
// slices the synthesis-frame window via slice_frame_map_to_trim_window and
// re-anchors; untrimmed, copies the full map verbatim (offset 0). Returns
// window_offset_samples (0 when untrimmed), or -1 when the trimmed window's
// target span is entirely consumed by the hop-aligned window start and no
// output sample would be emitted -- callers must refuse the render, because
// emit_sample_cap == 0 means "no cap" at the engine boundary and would render
// the whole sub-map instead of nothing. Both the GUI render pipeline and the
// render CLI call this so their EngineParams assembly stays byte-identical — the
// block that decides cmp-stable output lives in exactly one place.
//
// Header-only inline: it calls slice_frame_map_to_trim_window from
// libwarptempo_parser and phase-reset dispatch helpers, which both
// warptempo_gui and warptempo_cli already link, so no new compiled TU or
// CMake source entry is needed.
struct TrimSourceWindow {
    int64_t trim_begin_src   = 0;  // absolute source frame of the begin bound
    int64_t trim_end_src     = 0;  // absolute source frame of the end bound
    size_t  load_begin_frame = 0;  // always 0; see resolve_trim_source_window
    size_t  load_end_frame   = 0;  // exclusive end of the source read
};

inline TrimSourceWindow resolve_trim_source_window(
        bool has_trim_begin, double trim_begin_sec,
        bool has_trim_end,   double trim_end_sec,
        long sample_rate, int64_t total_frames, int N) {
    TrimSourceWindow w;
    w.trim_begin_src = has_trim_begin
        ? static_cast<int64_t>(std::nearbyint(
              trim_begin_sec * static_cast<double>(sample_rate)))
        : 0;
    w.trim_end_src = has_trim_end
        ? static_cast<int64_t>(std::nearbyint(
              trim_end_sec * static_cast<double>(sample_rate)))
        : total_frames;

    // Load the source from frame 0 to the end-trim point (plus margin), NOT a
    // begin-trimmed slice. The begin MUST stay at 0: the frame map's t_a
    // accumulation runs from frame 0, and that inherited history is what keeps
    // the windowed render's source reads sample-aligned with the full render
    // (the window head itself is a phase seed, so it converges toward the full
    // render rather than nulling at the first frames). A begin-windowed source
    // load rebased by source_frame_base was evaluated and rejected: the source
    // sample cache removed the load-time payoff, the memory saving is
    // immaterial, and rebasing origin-centered reads, including negative
    // clamps at the window head, risks audition-audio corruption the full-render
    // cmp baseline cannot detect. The end is end-capped because no frame in the
    // window reads source past trim_end except the last analysis window's small
    // reach, covered by the 2*N margin. An undersized margin only zero-pads the
    // trailing edge, never crashes.
    const int64_t end_margin = 2LL * static_cast<int64_t>(N);
    w.load_begin_frame = 0;
    w.load_end_frame = has_trim_end
        ? static_cast<size_t>(std::min<int64_t>(
              total_frames, w.trim_end_src + end_margin))
        : static_cast<size_t>(total_frames);
    return w;
}

inline int64_t assign_engine_frame_map(
        EngineParams& ep,
        const std::vector<FrameMapSegment>& full_standard,
        bool has_trim,
        int64_t trim_begin_src, int64_t trim_end_src,
        int N, int R_s) {
    if (has_trim) {
        const WindowedFrameMap w = slice_frame_map_to_trim_window(
            full_standard, trim_begin_src, trim_end_src, N, R_s);
        // A degenerate window (target span consumed by the hop-aligned start)
        // stores emit_sample_cap == 0, which the engine reads as "no cap".
        // Refuse before writing anything into ep so the caller can abort.
        if (w.emit_sample_cap <= 0) return -1;
        ep.emit_sample_cap = w.emit_sample_cap;
        ep.frame_map = w.frame_map;
        return w.window_offset_samples;
    }
    ep.frame_map = full_standard;
    return 0;
}

// Precondition: assign_engine_frame_map has already populated ep.frame_map and
// ep.emit_sample_cap.
inline int64_t assign_engine_phase_resets(
        EngineParams& ep,
        const std::vector<double>& reset_source_frames,
        const std::vector<FrameMapSegment>& full_map,
        int64_t window_offset_samples,
        int N) {
    const int64_t render_target_frames =
        ep.emit_sample_cap > 0
            ? ep.emit_sample_cap
            : static_cast<int64_t>(std::llrint(ep.frame_map.back().tgt_frame));
    ep.phase_reset_frames = phase_reset_dispatch_frames_target_domain(
        reset_source_frames,
        full_map,
        ep.frame_map,
        window_offset_samples,
        render_target_frames,
        phase_reset_offset_samples,
        N / 2);
    return render_target_frames;
}
