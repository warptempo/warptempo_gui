#pragma once

#include "phaseresetmarkers_parse.h"

#include <expected>
#include <string>
#include <vector>

// The GUI's authoring view. Adds no fields today; it exists so the GUI marker
// store has its own type and the parser can fill the PhaseResetMarker base by
// reference (a GuiPhaseResetMarker binds to PhaseResetMarker& by upcast),
// mirroring the WarpMarker / GuiWarpMarker split.
struct GuiPhaseResetMarker : PhaseResetMarker {};

// Slice a GUI phase-reset vector down to the serialized base, mirroring
// slice_to_warp_markers. Used at the render boundary so the parser-domain
// phase-reset assembly (build_phase_reset_frame_map) never sees the GUI type.
inline std::vector<PhaseResetMarker> slice_to_phase_reset_markers(
    const std::vector<GuiPhaseResetMarker>& src) {
    return std::vector<PhaseResetMarker>(src.begin(), src.end());
}

class GuiPhaseResetMarkers {
public:
    // Parses `path`. On success, populates markers() and returns the parsed
    // markers. The first malformed line aborts the parse and returns a
    // one-line error; a missing/unopenable file is a failure (callers that
    // treat absence as "no markers" check existence first). No throw.
    std::expected<void, std::string> load(const std::string& path);

    // Writes the canonical form to `path`. Atomic: writes to <path>.tmp,
    // fsyncs, then renames. Preserves existing permissions or uses 0644 if
    // the file is new. Returns true on success. Save dedups duplicate
    // time_seconds silently and emits a one-line stderr notice if any
    // rows were dropped — mid-edit drag gestures may transit through
    // duplicate states, so we don't error in the GUI for them.
    bool save(const std::string& path) const;

    // Static variant for callers that hold a raw GuiPhaseResetMarker vector (e.g.
    // the render pipeline writing per-render sidecars). Same on-disk format
    // and dedup behavior as the instance method. Dedup is keyed on
    // time_seconds (exact double match).
    static bool save(const std::string& path,
                     const std::vector<GuiPhaseResetMarker>& markers);

    const std::vector<GuiPhaseResetMarker>&        markers() const { return markers_; }

    // Inserts `m` at the position that preserves ascending time_seconds
    // order. Returns the insertion index. Equal-time collisions are
    // accepted at insert time (the user may transit through them via
    // nudge); save dedups them.
    int insert_marker(GuiPhaseResetMarker m);

    // Removes the marker at `index`. No-op if out of range.
    void remove_marker(int index);

    // Bumps generation_ on call. Same shape as GuiWarpMarkers
    // — contract is "you may mutate"; a spurious bump (caller read-only)
    // costs one stem rebuild on the next tick.
    GuiPhaseResetMarker* marker_mut(int index) {
        ++generation_;
        if (index < 0 || index >= static_cast<int>(markers_.size())) return nullptr;
        return &markers_[index];
    }

    std::vector<GuiPhaseResetMarker>& markers_mut() {
        ++generation_;
        return markers_;
    }

    void clear() {
        markers_.clear();
        ++generation_;
    }

    // Monotonically-increasing token bumped on every mutating method.
    // Mirrors GuiWarpMarkers::generation().
    long long generation() const { return generation_; }

private:
    std::vector<GuiPhaseResetMarker>      markers_;
    long long                      generation_ = 0;
};
