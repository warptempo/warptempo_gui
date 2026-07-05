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
    // Dedup compares the serialized timestamp string, not the in-memory
    // double. Mid-edit nudge gestures may transit through equal-time
    // collisions, and the render-domain publisher
    // (publish_render_domain_sidecars in render_pipeline.cpp) feeds
    // non-gridded times where two distinct doubles can round to the same
    // on-disk millisecond through format_timestamp's std::nearbyint
    // rounding. Comparing the serialized string mirrors the writer exactly
    // by construction, so any collision the writer could create is dropped
    // here before it reaches the file. For a time-sorted input list (every
    // caller holds one) the written file is therefore strictly increasing
    // in the strict authoring parser's domain: format_timestamp is monotone
    // non-decreasing in seconds. For millisecond-gridded authoring lists
    // this dedup is identical to exact-double dedup, since grid values
    // round-trip through format_timestamp exactly.
    //
    // Deliberate asymmetry with GuiWarpMarkers::save: warp save refuses a
    // non-strictly-increasing exact-double list outright instead of
    // dedup-dropping. Its one non-gridded caller writes the
    // .renderwarpmarkers display sidecar, consumed only by render-view's
    // lenient line-skipping reader (read_render_view_warpmarkers in
    // render_view.cpp), so a same-millisecond warp pair there is
    // display-harmless and no strict reloader ever sees it. The phase-reset
    // render sidecar, by contrast, reloads through the strict authoring
    // parser, so dropping here is what keeps that reload alive.
    std::ostringstream out;
    std::string last_ts;
    bool have_last = false;
    int dropped = 0;
    for (const auto& m : markers_) {
        const std::string ts = format_timestamp(m.time_seconds);
        if (have_last && ts == last_ts) {
            ++dropped;
            continue;
        }
        last_ts = ts;
        have_last = true;

        // `[#]MM:SS.mmm` only. The `#` disable prefix composes ahead of the
        // timestamp, exactly as the parser strips it. No mode suffix — the
        // peak/heap/pass model was removed when heap became the sole engine.
        if (m.disabled) out << '#';
        out << ts << '\n';
    }
    if (dropped > 0) {
        std::fprintf(stderr,
            "warptempo_gui: dropped %d duplicate phase_reset(s) on save\n",
            dropped);
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
