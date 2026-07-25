#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Naming symmetry: both marker columns share this ONE store implementation —
// GuiWarpMarkers and GuiPhaseResetMarkers are GuiMarkerStore instantiated
// over their per-column GUI marker types, so the store mechanics (the
// sorted-by-time_frame vector, the generation token, and the
// clear-bump-parse-upcast load shape) are identical by construction. The
// pair vocabulary `warp_X` / `phase_reset_X`, the per-column parse calls and
// serializer contracts, and the GUI-only field docs live at the concrete
// classes (warpmarkers.h / phaseresetmarkers.h).
template <typename GuiM>
class GuiMarkerStore {
public:
    const std::vector<GuiM>& markers() const { return markers_; }

    // Inserts `m` at the position that preserves ascending time_frame
    // order. Returns the insertion index. Equal times are legal — markers
    // may sit arbitrarily close or coincide exactly — and the store keeps
    // them ordered: this insert places by lower_bound, and the
    // time-mutating gestures reorder through the reorder-and-remap path,
    // so the list is always sorted at rest. Degenerate equal-time groups
    // normalize at the render boundary, never here (warp ties collapse to
    // one plain 1.00 owner in build_warp_frame_map; exact-equal enabled
    // phase resets collapse to one event in
    // build_phase_reset_source_frames).
    int insert_marker(GuiM m) {
        const int64_t time = m.time_frame;
        auto it = std::lower_bound(
            markers_.begin(), markers_.end(), time,
            [](const GuiM& a, int64_t t) { return a.time_frame < t; });
        const int idx = static_cast<int>(it - markers_.begin());
        markers_.insert(it, std::move(m));
        ++generation_;
        return idx;
    }

    // Removes the marker at `index`. No-op if out of range.
    void remove_marker(int index) {
        if (index < 0 || index >= static_cast<int>(markers_.size())) return;
        markers_.erase(markers_.begin() + index);
        ++generation_;
    }

    // Mutable accessor for edits to a single marker in place. A caller
    // that changes time_frame must restore order via
    // reorder_markers_by_time before the store is next read (flag/tempo
    // toggles preserve order trivially). Bumps generation_ on call.
    // Contract is "you may mutate"; a spurious bump (caller read-only)
    // costs one stem rebuild on the next tick — negligible.
    GuiM* marker_mut(int index) {
        ++generation_;
        if (index < 0 || index >= static_cast<int>(markers_.size())) return nullptr;
        return &markers_[index];
    }

    // Bulk-mutable accessor. The class assumes ascending time_frame
    // order (equal times legal); a caller that changes times must restore
    // order via reorder_markers_by_time before the store is next read.
    // Exposed for operations that twiddle a flag across many markers at
    // once. Bumps generation_ on call (same rationale as marker_mut).
    std::vector<GuiM>& markers_mut() {
        ++generation_;
        return markers_;
    }

    void clear() {
        markers_.clear();
        ++generation_;
    }

    // Monotonically-increasing token bumped on every mutating method.
    // Consumers (the flag-cache fingerprint, the red-flag-set memos)
    // detect any marker-store
    // change by comparing generations rather than diffing contents.
    long long generation() const { return generation_; }

protected:
    // The shared load shape behind the concrete classes' load(): clear and
    // bump BEFORE the parse, so a failed parse still leaves an emptied,
    // bumped store; on success copy each parsed serialized base into a
    // default-constructed GuiM by upcast assignment (no slicing on the way
    // in — the GUI-only fields, where the type has any, keep their
    // session defaults).
    template <typename ParseFn>
    std::expected<void, std::string> load_impl(const std::string& path,
                                               ParseFn&& parse) {
        markers_.clear();
        ++generation_;

        auto r = parse(path);
        if (!r) return std::unexpected(std::move(r.error()));

        markers_.reserve(r->size());
        for (const auto& bm : *r) {
            GuiM g;
            static_cast<std::remove_cvref_t<decltype(bm)>&>(g) = bm;
            markers_.push_back(g);
        }
        return {};
    }

private:
    std::vector<GuiM> markers_;
    long long         generation_ = 0;
};
