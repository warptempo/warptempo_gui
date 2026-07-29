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
        ++structural_generation_;
        return idx;
    }

    // Removes the marker at `index`. No-op if out of range.
    void remove_marker(int index) {
        if (index < 0 || index >= static_cast<int>(markers_.size())) return;
        markers_.erase(markers_.begin() + index);
        ++generation_;
        ++structural_generation_;
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
        ++structural_generation_;
    }

    // Monotonically-increasing token bumped on every mutating method.
    // Consumers (the flag-cache fingerprint, the red-flag-set memos)
    // detect any marker-store
    // change by comparing generations rather than diffing contents.
    long long generation() const { return generation_; }

    // THE STRUCTURAL GENERATION — the SECOND, coarser token, and a different
    // question: not "did anything change" but "did ROW IDENTITY change", i.e.
    // can a raw index held from before still mean the same marker? A RAW STORE
    // INDEX HELD ACROSS COMMANDS NEEDS A LIVENESS RULE; this is that rule's
    // mechanism for the one place indices are held across commands — the
    // per-tab PARKED selections in ViewState, which stamp this at stash time and
    // are DROPPED on mismatch at restore (see park_selection_stamp /
    // drop_parked_selection_if_stale in app_state.h).
    //
    // BUMPED BY, re-derived by grepping every mutation route rather than
    // inherited (2026-07-29):
    //   * insert_marker and remove_marker, here — a row inserted or erased
    //     shifts every index at or after it;
    //   * clear() and load_impl(), here — wholesale, every index dies;
    //   * the WHOLESALE REPLACES that go through markers_mut() and so cannot be
    //     detected from inside this class. They call bump_structural_generation
    //     at their own sites, and there are exactly three. Two of them bump
    //     CONDITIONALLY, because they run on inputs that often move no row and a
    //     false bump costs a parked selection for nothing: undo/redo's store
    //     restore bumps per column only when the restored snapshot's row
    //     identity differs from the live store's (undo.cpp — every entry assigns
    //     BOTH columns, so a warp entry carries an untouched phase-reset vector),
    //     and the PROPAGATE PLACEMENT paste bumps only when its erase-window
    //     actually removed rows (phase_reset_propagate.cpp; its inserts run
    //     through insert_marker and bump here anyway, the erase is what needs the
    //     explicit call). The third is unconditional: the render-entry ADOPT's
    //     replace (both columns, input_key_dispatch.cpp), which also clears every
    //     parked slot outright, so its bump is belt.
    // NOT BUMPED BY, deliberately:
    //   * marker_mut / markers_mut themselves — an in-place FIELD edit (tempo,
    //     flag, label, disabled) moves no row, and every `markers_mut() =
    //     std::move(proposed)` site outside the three above assigns a same-size
    //     copy of the same rows;
    //   * REORDER. reorder_markers_by_time is a permutation of every existing
    //     row, and remap_marker_indices_after_reorder follows it into the parked
    //     slots exactly, so identity is PRESERVED there rather than lost — the
    //     two mechanisms compose, and bumping would throw away a selection the
    //     remap had just repaired.
    unsigned long long structural_generation() const {
        return structural_generation_;
    }
    // For the wholesale-replace sites listed above; the store cannot see them.
    void bump_structural_generation() { ++structural_generation_; }

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
        ++structural_generation_;

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
    unsigned long long structural_generation_ = 0;
};
