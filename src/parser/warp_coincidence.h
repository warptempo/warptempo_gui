#pragma once

#include <cmath>
#include <cstdint>

// THE WARP COINCIDENCE, ONE OWNER (architect approval 2026-11-17 — the frozen
// touch that moved the millisecond and the predicate out of the GUI into the
// parser, so the render, the GUI's strike and its drop refusal read one rule).
// His words: "for warp, we should make the parser truthful - we need to show
// coincident markers, and that definition needs to include sub-1ms distance,
// so the parser should also make them both 1.0. so if two warp markers are
// struck in the gui, both should render as 1.0 in the parser/engine." — and
// "actually i think disabled markers should get struck also. they could mess
// up a copy-paste of phase resets. so it becomes 'they both get 1.0 as their
// effective tempo' not 'they both render as 1.0'". Header-only: the GUI's
// nudge step reads the millisecond without pulling in the map build.

// ONE MILLISECOND IN FRAMES: the whole number of frames nearest a millisecond
// at `sample_rate`, nearbyint(rate / 1000) — 44 at 44.1 kHz, 48 at 48 kHz,
// 88 at 88.2 kHz, 96 at 96 kHz — never less than one frame (a dead rate's is
// one). TWO MEANINGS, ONE NUMBER: the warp coincidence's distance below, and
// the GUI's horizontal arrow step in milliseconds (architect 2026-11-15, the
// no-grid arc: "the left-right nudge should be recalibrated to one
// millisecond left or right ... we would convert 1 millisecond to the nearest
// amount in frames, and that would be the nudge step" — the marker nudge,
// position_nudge.h, and the playhead's step, app_state.h), so one nudge of
// either marker away from the other always clears a coincidence (a distance
// d < N becomes d + N >= N).
inline int64_t millisecond_frames(long sample_rate) {
    const int64_t n = static_cast<int64_t>(
        std::nearbyint(static_cast<double>(sample_rate) / 1000.0));
    return n < 1 ? 1 : n;
}

// TWO WARP MARKER FRAMES COINCIDE when they are the same frame OR differ by
// LESS THAN ONE MILLISECOND (millisecond_frames above — at 44.1 kHz distances
// 0 .. 43 coincide, 44 and more are clear; at a dead rate the exact frame
// alone). Exact equality is distance 0, so the predicate contains it. ITS
// READERS: the coincident-run classifier (warp_coincident_members,
// warp_frame_map_build.h — the render resolver's stage 2, marker_effective's
// members and the GUI's strike all read that one membership) and the GUI's
// warp drop refusal (warp_marker_in_vicinity_of, warp_frame_map_view.h),
// which asks it of the frame a drop would author. Disabled markers count by
// his ruling: the predicate reads frames alone.
inline bool warp_frames_coincide(int64_t a, int64_t b, long sample_rate) {
    const int64_t d = a >= b ? a - b : b - a;
    return d < millisecond_frames(sample_rate);
}
