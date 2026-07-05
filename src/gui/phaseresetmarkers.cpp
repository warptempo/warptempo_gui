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
    // Mirrors GuiWarpMarkers::save: refuse on exact-double non-ascent.
    // Authoring times are millisecond-gridded by construction (format
    // plus snap_to_timestamp_grid on every mutation path), so the
    // exact-double comparison is the on-disk millisecond contract for the
    // strict-reloaded outputs — the authoring file and the source-domain
    // batch sidecar the commit path reloads. The render publisher's
    // non-gridded computed times feed only render-view's lenient reader,
    // where a same-millisecond pair from distinct frames is
    // display-harmless, and a pair colliding to the same integer frame
    // refuses here visibly with the publisher's write-failed warning and
    // a withheld fingerprint.
    std::ostringstream out;
    for (size_t i = 0; i < markers_.size(); ++i) {
        if (i > 0 && !(markers_[i].time_seconds > markers_[i - 1].time_seconds)) {
            std::fprintf(stderr,
                "warptempo_gui: save aborted: phase_resets not strictly "
                "increasing at %.3fs\n",
                markers_[i].time_seconds);
            return false;
        }

        // `[#]MM:SS.mmm` only. The `#` disable prefix composes ahead of the
        // timestamp, exactly as the parser strips it. No mode suffix — the
        // peak/heap/pass model was removed when heap became the sole engine.
        const auto& m = markers_[i];
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
