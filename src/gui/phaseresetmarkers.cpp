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
    // The refusal compares in the writer's serialized millisecond domain
    // because that is the persistence quantum: two distinct in-memory
    // doubles that round to one on-disk millisecond could otherwise only be
    // silently collapsed or written as a duplicate line the strict
    // authoring parser rejects on reload, and refusal is preferred over
    // silent correction. Lexicographic comparison is chronological here
    // because format_timestamp always emits the fixed-width nine-character
    // MM:SS.mmm form. Nothing of value is refused — two resets inside
    // one millisecond collapse to the same synthesis frame at the engine,
    // the earlier one superseded — so a colliding pair is an authoring slip
    // by definition.
    //
    // The shape is symmetric with GuiWarpMarkers::save's strict-ascent
    // abort; the comparison domains deliberately differ, exact doubles
    // there and serialized strings here, because sub-millisecond target
    // separations of warp markers are legal, load-bearing map geometry
    // under the no-ceiling rule and warp's only non-gridded caller feeds
    // the lenient render-view display reader, while the phase-reset render
    // sidecar reloads through the strict authoring parser, which forces the
    // serialized domain.
    //
    // Consequences: an authoring save that catches an equal-time collision
    // mid-nudge now aborts with the message instead of silently dropping,
    // exactly as warp saves always have. A render-domain collision —
    // sub-millisecond target separation at extreme scale, or two
    // trim-head resets both clamped to the delivered WAV's origin —
    // refuses the display sidecar while the render itself succeeds, the
    // publisher prints its write-failed warning, and the fingerprint is
    // withheld by the existing attestation plumbing, so the failure is
    // visible and the remedy is authoring-side (remove the redundant
    // reset, or adjust the trim).
    //
    // Because the refusal validates the emitted sequence itself, every file
    // this writer produces is strictly increasing in the parser's domain
    // for any input whatsoever; no reload verification exists at the
    // publish seam and none is needed.
    std::ostringstream out;
    std::string last_ts;  // empty is a safe first-iteration sentinel:
                          // format_timestamp never returns an empty string.
    for (const auto& m : markers_) {
        const std::string ts = format_timestamp(m.time_seconds);
        if (!last_ts.empty() && !(ts > last_ts)) {
            std::fprintf(stderr,
                "warptempo_gui: save aborted: phase_resets not strictly "
                "increasing at %s\n",
                ts.c_str());
            return false;
        }
        last_ts = ts;

        // `[#]MM:SS.mmm` only. The `#` disable prefix composes ahead of the
        // timestamp, exactly as the parser strips it. No mode suffix — the
        // peak/heap/pass model was removed when heap became the sole engine.
        if (m.disabled) out << '#';
        out << ts << '\n';
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
