#include "phase_reset_frame_map_build.h"

#include <cstddef>
#include <string>
#include <vector>

std::expected<std::vector<double>, std::string> build_phase_reset_frame_map(
    const std::vector<PhaseResetMarker>& markers, long sample_rate,
    int64_t total_frames) {
    std::vector<double> out;
    out.reserve(markers.size());
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        if (m.disabled) continue;
        double src_frame = m.time_seconds * static_cast<double>(sample_rate);
        if (src_frame > static_cast<double>(total_frames)) {
            return std::unexpected(
                "phase reset time exceeds source length at marker "
                + std::to_string(i));
        }
        out.push_back(src_frame);
    }
    return out;
}
