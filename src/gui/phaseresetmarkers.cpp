#include "phaseresetmarkers.h"

#include "settings_io.h"
#include "time_format.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

std::expected<void, std::string> GuiPhaseResetMarkers::load(const std::string& path) {
    markers_.clear();
    ++generation_;

    auto r = parse_phaseresetmarkers_file(path);
    if (!r) return std::unexpected(std::move(r.error()));

    markers_.reserve(r->size());
    for (const PhaseResetMarker& pm : *r) {
        GuiPhaseResetMarker g;                   // no extra fields today
        static_cast<PhaseResetMarker&>(g) = pm;  // copy the serialized base
        markers_.push_back(g);
    }
    return {};
}

bool GuiPhaseResetMarkers::save(const std::string& path) const {
    return save(path, markers_);
}

bool GuiPhaseResetMarkers::save(const std::string& path,
                         const std::vector<GuiPhaseResetMarker>& markers_) {
    // Serializer contract: this save performs no ordering validation.
    // The store is sorted by construction — ordered insert for drops and
    // propagate, and every time-mutating gesture (drag commit, shift,
    // nudge) reorders through the reorder-and-remap path — so rows
    // serialize in non-decreasing time order. Equal-time rows are legal
    // (markers may overlap exactly) and load back under the relaxed
    // parser, which accepts non-decreasing times; only a DECREASING
    // sequence — impossible from the sorted store, so evidence of a
    // future op bug — fails the next load with a loud line-numbered
    // parse error. Degeneracy is refused at the strict render boundary
    // (build_phase_reset_source_frames collapses exact duplicates;
    // build_warp_frame_map refuses warp ties), not by this serializer.
    // The render publisher can likewise produce two resets on the same
    // millisecond (distinct exact target-frame doubles whose millisecond
    // timestamps round together); both lines simply serialize here, and
    // the display sidecar's lenient reader shows them as overlapping
    // flags.
    std::ostringstream out;
    for (const auto& m : markers_) {
        // `[#]MM:SS.mmm` only. The `#` disable prefix composes ahead of the
        // timestamp, exactly as the parser strips it. No mode suffix — the
        // peak/heap/pass model was removed when heap became the sole engine.
        if (m.disabled) out << '#';
        out << format_timestamp(m.time_seconds) << '\n';
    }
    const std::string data = out.str();

    // tmp + fsync + rename, preserving the existing file's mode.
    return atomic_write_string_to_path(path, data);
}

int GuiPhaseResetMarkers::insert_marker(GuiPhaseResetMarker m) {
    const double time = m.time_seconds;
    auto it = std::lower_bound(
        markers_.begin(), markers_.end(), time,
        [](const GuiPhaseResetMarker& a, double t) { return a.time_seconds < t; });
    const int idx = static_cast<int>(it - markers_.begin());
    markers_.insert(it, std::move(m));
    ++generation_;
    return idx;
}

void GuiPhaseResetMarkers::remove_marker(int index) {
    if (index < 0 || index >= static_cast<int>(markers_.size())) return;
    markers_.erase(markers_.begin() + index);
    ++generation_;
}
