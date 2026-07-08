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
// phase-reset assembly (build_phase_reset_source_frames) never sees the GUI
// type.
inline std::vector<PhaseResetMarker> slice_to_phase_reset_markers(
    const std::vector<GuiPhaseResetMarker>& src) {
    return std::vector<PhaseResetMarker>(src.begin(), src.end());
}

class GuiPhaseResetMarkers {
public:
    // Parses `path`. On success, populates markers() and returns the parsed
    // markers. The first malformed line aborts the parse and returns a
    // one-line error; a missing/unopenable file is a failure (the sidecar is
    // created at source load and required at every load boundary; the empty
    // file is the no-resets form). No throw.
    std::expected<void, std::string> load(const std::string& path);

    // Writes the canonical form to `path`. Atomic: writes to <path>.tmp,
    // fsyncs, then renames. Preserves existing permissions or uses 0644 if
    // the file is new. Returns true on success. Save writes every row with
    // no dedup or ordering validation; the serializer contract (the store
    // is sorted by construction, equal-time rows are legal and reload,
    // and the strict render boundary — not the serializer — refuses
    // degeneracy) is documented at the static save overload in
    // phaseresetmarkers.cpp.
    bool save(const std::string& path) const;

    // Static variant for callers that hold a raw GuiPhaseResetMarker vector (e.g.
    // the render pipeline writing per-render sidecars). Same on-disk format as
    // the instance method.
    static bool save(const std::string& path,
                     const std::vector<GuiPhaseResetMarker>& markers);

    const std::vector<GuiPhaseResetMarker>&        markers() const { return markers_; }

    // Inserts `m` at the position that preserves ascending time_seconds
    // order. Returns the insertion index. Equal times are legal —
    // markers may sit arbitrarily close or coincide exactly — and the
    // store keeps them ordered: this insert places by lower_bound, and
    // the time-mutating gestures reorder through the reorder-and-remap
    // path, so the list is always sorted at rest.
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
