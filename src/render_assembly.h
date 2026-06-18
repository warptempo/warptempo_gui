#pragma once

#include "engine/engine.h"      // EngineParams
#include "frame_map.h"          // FrameMapSegment
#include "frame_map_build.h"    // WindowedFrameMap, slice_frame_map_to_trim_window

#include <cstdint>
#include <vector>

// Single source of truth for populating EngineParams.frame_map and
// emit_sample_cap from the full untrimmed standard map. With a trim bound set,
// slices the synthesis-frame window via slice_frame_map_to_trim_window and
// re-anchors; untrimmed, copies the full map verbatim (offset 0). Returns
// window_offset_samples (0 when untrimmed). Both the GUI render pipeline and the
// render CLI call this so their EngineParams assembly stays byte-identical — the
// block that decides cmp-stable output lives in exactly one place.
//
// Header-only inline: it calls slice_frame_map_to_trim_window from
// libwarptempo_parser, which both warptempo_gui and warptempo_render already
// link, so no new compiled TU or CMake source entry is needed.
inline int64_t assign_engine_frame_map(
        EngineParams& ep,
        const std::vector<FrameMapSegment>& full_standard,
        bool has_trim,
        int64_t trim_begin_src, int64_t trim_end_src,
        int N, int R_s) {
    if (has_trim) {
        const WindowedFrameMap w = slice_frame_map_to_trim_window(
            full_standard, trim_begin_src, trim_end_src, N, R_s);
        ep.emit_sample_cap = w.emit_sample_cap;
        ep.frame_map.reserve(w.frame_map.size());
        for (const auto& s : w.frame_map)
            ep.frame_map.emplace_back(s.src_frame, s.tgt_frame);
        return w.window_offset_samples;
    }
    ep.frame_map.reserve(full_standard.size());
    for (const auto& s : full_standard)
        ep.frame_map.emplace_back(s.src_frame, s.tgt_frame);
    return 0;
}
