#include "magnificationlevelmarkers.h"

#include "frame_format.h"
#include "marker_magnification.h"
#include "settings_io.h"

#include <cstddef>
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

namespace {

// THE COLUMN'S LEVEL-PER-FRAME RULE, THE ONE OWNER both readers below call
// (architect 2026-09-16). The store is frame-ascending, so a coincident group
// is a run of adjacent equal frames; `visit(frame, level)` is called once per
// run that contributes a level, in ascending frame order:
//   * a run with >= 2 ENABLED members COLLAPSES TO THE NEUTRAL LEVEL 0 — the
//     warp column's coincident rule on this axis (a run of 2+ effectively
//     enabled tempo markers collapses to a neutral 1.00 owner,
//     warp_coincident_collapse_members) — so store order among equal frames is
//     invisible to the picture, exactly as it is invisible to the render on W
//     and P;
//   * a run with EXACTLY ONE enabled member contributes that member's level;
//   * a run with NO enabled member contributes nothing: a disabled marker is
//     invisible and the level in force walks straight past it.
// Disabled members never count toward the run's enabled tally (the red cue,
// magnification_level_red_flag_set_cached, is the wider participation-blind
// question — it reddens a run of 2+ ROWS whatever their disabled bits, as the
// warp cue does).
template <typename Visit>
void for_each_magnification_level_run(
        const std::vector<GuiMagnificationLevelMarker>& markers, Visit visit) {
    const std::size_t n = markers.size();
    std::size_t i = 0;
    while (i < n) {
        std::size_t j = i + 1;
        while (j < n && markers[j].time_frame == markers[i].time_frame) ++j;
        std::size_t enabled      = 0;
        std::size_t last_enabled = j;
        for (std::size_t k = i; k < j; ++k) {
            if (!markers[k].disabled) {
                ++enabled;
                last_enabled = k;
            }
        }
        if (enabled >= 2) {
            visit(markers[i].time_frame, uint8_t{0});
        } else if (enabled == 1) {
            visit(markers[i].time_frame, markers[last_enabled].level);
        }
        i = j;
    }
}

}  // namespace

WaveformGainProfile build_waveform_gain_profile(
        const std::vector<GuiMagnificationLevelMarker>& markers) {
    WaveformGainProfile p;
    auto& bp = p.breakpoints;
    // One contribution per frame (the run walk above), so a breakpoint is
    // pushed exactly where the level CHANGES and nothing is ever rewritten.
    for_each_magnification_level_run(
        markers, [&bp](int64_t frame, uint8_t level) {
            const uint8_t current = bp.empty() ? uint8_t{0} : bp.back().level;
            if (level != current) bp.push_back({frame, level});
        });
    return p;
}

// The contract is at the declaration. The same run walk the builder takes,
// keeping the last contributed level at or before `frame` — which is the
// builder's "each level holds to the next", its "a disabled marker is
// invisible" and its coincident collapse to level 0, all at once.
uint8_t magnification_level_in_force(
        const std::vector<GuiMagnificationLevelMarker>& markers,
        int64_t frame) {
    uint8_t level = 0;
    for_each_magnification_level_run(
        markers, [&level, frame](int64_t run_frame, uint8_t run_level) {
            if (run_frame <= frame) level = run_level;
        });
    return level;
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
