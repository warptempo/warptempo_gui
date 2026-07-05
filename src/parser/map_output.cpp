#include "map_output.h"

#include <cstdio>
#include <fstream>
#include <iomanip>

std::expected<void, std::string> write_frame_map(
    const std::string& path, const std::vector<FrameMapSegment>& segs) {
    std::ofstream of(path);
    if (!of) {
        return std::unexpected("could not write frame map '" + path + "'");
    }
    of << std::setprecision(17);
    for (const auto& s : segs) {
        of << s.src_frame << " " << s.tgt_frame << "\n";
    }
    of.flush();
    if (!of) {
        return std::unexpected("could not write frame map '" + path + "' (I/O error)");
    }
    return {};
}

std::expected<void, std::string> write_tempo_map(
    const std::string& path, const std::vector<TempoMapEntry>& entries) {
    std::ofstream of(path);
    if (!of) {
        return std::unexpected("could not write tempo map '" + path + "'");
    }
    of << std::fixed << std::setprecision(16);
    for (const auto& e : entries) {
        of << e.target_time_sec << " " << e.multiplier << "\n";
    }
    of.flush();
    if (!of) {
        return std::unexpected("could not write tempo map '" + path + "' (I/O error)");
    }
    return {};
}

std::expected<void, std::string> write_reset_map(
    const std::string& path, const std::vector<int64_t>& source_frames) {
    std::ofstream of(path);
    if (!of) {
        return std::unexpected("could not write resetmap '" + path + "'");
    }
    for (const int64_t f : source_frames) {
        of << f << "\n";
    }
    of.flush();
    if (!of) {
        return std::unexpected("could not write resetmap '" + path + "' (I/O error)");
    }
    return {};
}
