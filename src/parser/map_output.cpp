#include "map_output.h"

#include <cstdio>
#include <fstream>
#include <iomanip>

std::expected<void, std::string> write_standard_frame_map(
    const std::string& path, const std::vector<FrameMapSegment>& segs,
    bool drop_zero_zero) {
    std::ofstream of(path);
    if (!of) {
        return std::unexpected("could not write timemap '" + path + "'");
    }
    for (const auto& s : segs) {
        if (drop_zero_zero && s.src_frame == 0 && s.tgt_frame == 0) continue;
        of << s.src_frame << " " << s.tgt_frame << "\n";
    }
    return {};
}

std::expected<void, std::string> write_midi_tempomap(
    const std::string& path, const std::vector<TempomapEntry>& entries) {
    std::ofstream of(path);
    if (!of) {
        return std::unexpected("could not write tempomap '" + path + "'");
    }
    of << std::fixed << std::setprecision(16);
    for (const auto& e : entries) {
        of << e.target_time_sec << " " << e.multiplier << "\n";
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
    return {};
}
