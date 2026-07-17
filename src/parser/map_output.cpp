#include "map_output.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <system_error>

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
    of.close();
    if (!of) {
        return std::unexpected("could not write warpframemap '" + path + "' (i/o error)");
    }
    return {};
}

std::expected<void, std::string> write_phase_reset_frame_map(
    const std::string& path, const std::vector<double>& engine_query_frames) {
    std::ofstream of(path);
    if (!of) {
        return std::unexpected("could not write phaseresetframemap '" + path + "'");
    }
    of << std::setprecision(17);
    for (const double f : engine_query_frames) {
        of << f << "\n";
    }
    of.close();
    if (!of) {
        return std::unexpected("could not write phaseresetframemap '" + path + "' (i/o error)");
    }
    return {};
}

std::expected<void, std::string> write_frame_map_pair(
    const std::string& dir, const std::string& stem,
    const std::vector<WarpFrameMapSegment>& warp_segs,
    const std::vector<double>& phase_reset_engine_query_frames) {
    const std::filesystem::path warp_path =
        std::filesystem::path(dir) / (stem + ".warpframemap");
    const std::filesystem::path reset_path =
        std::filesystem::path(dir) / (stem + ".phaseresetframemap");
    // Enforce the all-or-nothing pair: remove both names on any failure so the
    // writers never leave a new-warp-beside-old-reset (or an old complete pair
    // posing as this render's). Errors are swallowed — these are cache files.
    auto remove_both = [&]() {
        std::error_code ec;
        std::filesystem::remove(warp_path, ec);
        std::filesystem::remove(reset_path, ec);
    };
    if (auto w = write_warp_frame_map(warp_path.string(), warp_segs); !w) {
        remove_both();
        return std::unexpected(w.error());
    }
    if (auto w = write_phase_reset_frame_map(
            reset_path.string(), phase_reset_engine_query_frames); !w) {
        remove_both();
        return std::unexpected(w.error());
    }
    return {};
}
