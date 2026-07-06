#pragma once

#include "engine/engine.h"      // EngineParams
#include "warp_frame_map.h"          // WarpFrameMapSegment
#include "warp_frame_map_build.h"    // WindowedWarpFrameMap, slice_warp_frame_map_to_trim_window
#include "phase_reset_frame_map_build.h"  // derive_phase_reset_frame_map

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Single source of truth for populating EngineParams.warp_frame_map from the
// full untrimmed standard map. With a trim bound set, slices the trimmed
// deliverable map via slice_warp_frame_map_to_trim_window; untrimmed, copies
// the full map verbatim (offset 0). Either way the engine receives one map
// shape and no emit cap crosses the engine boundary: the engine renders the
// map wholesale and derives its output length from the map's last anchor
// (trimmed, the rounded boundary pair the slicer closes on). Returns
// window_offset_samples (0 when untrimmed), or -1 when the trimmed window's
// target span is entirely consumed by the hop-aligned window start and no
// output sample would be emitted -- callers must refuse the render, because
// the parser-side stored-zero refusal exists precisely because a degenerate
// window has no map to hand the engine (the slicer leaves its map unbuilt).
// Both the GUI render pipeline and the render CLI call this so their
// EngineParams assembly stays byte-identical — the block that decides
// cmp-stable output lives in exactly one place.
//
// Header-only inline: it calls slice_warp_frame_map_to_trim_window and
// derive_phase_reset_frame_map from libwarptempo_parser, which both
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

inline int64_t assign_engine_warp_frame_map(
        EngineParams& ep,
        const std::vector<WarpFrameMapSegment>& full_standard,
        bool has_trim,
        int64_t trim_begin_src, int64_t trim_end_src,
        int N, int R_s) {
    if (has_trim) {
        WindowedWarpFrameMap w = slice_warp_frame_map_to_trim_window(
            full_standard, trim_begin_src, trim_end_src, N, R_s);
        // A degenerate window (target span consumed by the hop-aligned start)
        // stores emit_sample_cap == 0 and carries no map. Refuse before
        // writing anything into ep so the caller can abort.
        if (w.emit_sample_cap <= 0) return -1;
        ep.warp_frame_map = std::move(w.warp_frame_map);
        return w.window_offset_samples;
    }
    ep.warp_frame_map = full_standard;
    return 0;
}

// Precondition: assign_engine_warp_frame_map has already populated
// ep.warp_frame_map. Derives the engine-input phase reset list through the
// deliverable form against that very map — the same form the artifact pair
// uses — so the in-process render and the .warpframemap /
// .phaseresetframemap pair coincide by construction. Returns the render's
// output length, llrint of the map's last anchor target (the same integer
// the engine derives at init), for the GUI's profiling consumer.
inline int64_t assign_engine_phase_reset_frame_map(
        EngineParams& ep,
        const std::vector<double>& reset_source_frames) {
    const int64_t render_target_frames =
        static_cast<int64_t>(std::llrint(ep.warp_frame_map.back().tgt_frame));
    ep.phase_reset_frame_map = derive_phase_reset_frame_map(
        reset_source_frames, ep.warp_frame_map);
    return render_target_frames;
}
