#include "map_output.h"

#include <cstdio>
#include <fstream>
#include <iomanip>

std::expected<void, std::string> write_warp_frame_map(
    const std::string& path, const std::vector<WarpFrameMapSegment>& segs) {
    std::ofstream of(path);
    if (!of) {
        return std::unexpected("could not write warpframemap '" + path + "'");
    }
    of << std::setprecision(17);
    for (const auto& s : segs) {
        of << s.src_frame << " " << s.tgt_frame << "\n";
    }
    of.flush();
    if (!of) {
        return std::unexpected("could not write warpframemap '" + path + "' (I/O error)");
    }
    return {};
}

std::expected<void, std::string> write_midi_tempo_map(
    const std::string& path, const std::vector<MidiTempoMapEntry>& entries) {
    std::ofstream of(path);
    if (!of) {
        return std::unexpected("could not write miditempomap '" + path + "'");
    }
    of << std::fixed << std::setprecision(16);
    for (const auto& e : entries) {
        of << e.target_time_sec << " " << e.multiplier << "\n";
    }
    of.flush();
    if (!of) {
        return std::unexpected("could not write miditempomap '" + path + "' (I/O error)");
    }
    return {};
}

std::expected<void, std::string> write_phase_reset_frame_map(
    const std::string& path, const std::vector<double>& source_frames) {
    std::ofstream of(path);
    if (!of) {
        return std::unexpected("could not write phaseresetframemap '" + path + "'");
    }
    of << std::setprecision(17);
    for (const double f : source_frames) {
        of << f << "\n";
    }
    of.flush();
    if (!of) {
        return std::unexpected("could not write phaseresetframemap '" + path + "' (I/O error)");
    }
    return {};
}
