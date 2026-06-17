#include "recipe_output.h"

#include <cstdio>
#include <fstream>
#include <iomanip>

bool write_standard_frame_map(const std::string& path,
                              const std::vector<FrameMapSegment>& segs,
                              bool drop_zero_zero) {
    std::ofstream of(path);
    if (!of) {
        std::fprintf(stderr,
            "warptempo_gui: render error: could not write timemap '%s'\n",
            path.c_str());
        return false;
    }
    for (const auto& s : segs) {
        if (drop_zero_zero && s.src_frame == 0 && s.tgt_frame == 0) continue;
        of << s.src_frame << " " << s.tgt_frame << "\n";
    }
    return true;
}

bool write_midi_tempomap(const std::string& path,
                         const std::vector<TempomapEntry>& entries) {
    std::ofstream of(path);
    if (!of) {
        std::fprintf(stderr,
            "warptempo_gui: render error: could not write tempomap '%s'\n",
            path.c_str());
        return false;
    }
    of << std::fixed << std::setprecision(16);
    for (const auto& e : entries) {
        of << e.target_time_sec << " " << e.multiplier << "\n";
    }
    return true;
}
