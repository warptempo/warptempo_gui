#include "magnificationlevelmarkers.h"

#include "frame_format.h"
#include "marker_magnification.h"
#include "settings_io.h"

#include <sstream>

std::expected<void, std::string> GuiMagnificationLevelMarkers::load(
        const std::string& path,
        std::optional<std::string>* path_free_reason) {
    return load_impl(path, parse_magnificationlevelmarkers_file,
                     path_free_reason);
}

// Serializer contract, the phase-reset column's: no ordering validation. The
// store is sorted by construction (GuiMarkerStore's ordered insert), so rows
// serialize in non-decreasing frame order; equal frames are legal and reload
// under the parser, which refuses only a DECREASING sequence. Each row is
// `[#]<frame position>|<level>` (the grammar at
// magnificationlevelmarkers_parse.h), the level always written.
std::string format_magnificationlevelmarkers_text(
    const std::vector<GuiMagnificationLevelMarker>& markers_) {
    std::ostringstream out;
    for (const auto& m : markers_) {
        if (m.disabled) out << '#';
        out << format_authored_frame(m.time_frame) << '|'
            << format_marker_magnification(m.level) << '\n';
    }
    return out.str();
}

WaveformGainProfile build_waveform_gain_profile(
        const std::vector<GuiMagnificationLevelMarker>& markers) {
    WaveformGainProfile p;
    auto& bp = p.breakpoints;
    for (const GuiMagnificationLevelMarker& m : markers) {
        if (m.disabled) continue;
        const int64_t f     = m.time_frame;
        const uint8_t level = m.level;
        // Equal frames: the later enabled row replaces the earlier one's
        // breakpoint, which is then dropped if it no longer changes the level
        // it follows.
        if (!bp.empty() && bp.back().source_frame == f) {
            bp.back().level = level;
            const uint8_t before =
                bp.size() >= 2 ? bp[bp.size() - 2].level : uint8_t{0};
            if (before == level) bp.pop_back();
            continue;
        }
        const uint8_t current = bp.empty() ? uint8_t{0} : bp.back().level;
        if (level != current) bp.push_back({f, level});
    }
    return p;
}

bool GuiMagnificationLevelMarkers::save(const std::string& path) const {
    return save(path, markers());
}

bool GuiMagnificationLevelMarkers::save(
        const std::string& path,
        const std::vector<GuiMagnificationLevelMarker>& markers_) {
    return atomic_write_string_to_path(
        path, format_magnificationlevelmarkers_text(markers_));
}
