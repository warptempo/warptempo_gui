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

// ONE CONTRIBUTING RUN of the walk below — what the visitor is handed.
// `frame` and `level` are the contribution (the level the run puts in force
// from that frame); `begin` / `end` are the run's rows in store order, the
// half-open [begin, end); `collapsed` says the run had 2+ enabled members and
// so contributed the neutral level 0 — a level-0 contribution alone cannot
// tell a collapse from a single enabled level-0 marker, and the collapse
// classifier below needs the difference.
struct MagnificationLevelRun {
    int64_t     frame;
    uint8_t     level;
    std::size_t begin;
    std::size_t end;
    bool        collapsed;
};

// THE COLUMN'S LEVEL-PER-FRAME RULE, THE ONE OWNER the two readers below
// call (architect 2026-09-16). The store is frame-ascending, so a coincident
// group is a run of adjacent equal frames; `visit(run)` is called once per run
// that contributes a level, in ascending frame order:
//   * a run with >= 2 ENABLED members COLLAPSES TO THE NEUTRAL LEVEL 0 — the
//     warp column's coincident rule on this axis (a run of 2+ effectively
//     enabled tempo markers collapses to a neutral 1.00 owner,
//     warp_coincident_collapse_members) — so store order among equal frames is
//     invisible to the rule, exactly as it is invisible to the render on W
//     and P;
//   * a run with EXACTLY ONE enabled member contributes that member's level;
//   * a run with NO enabled member contributes nothing: a disabled marker is
//     invisible and the level in force walks straight past it.
// Disabled members never count toward the run's enabled tally (the red cue,
// magnification_level_red_flag_set_cached, is the wider participation-blind
// question — it reddens a run of 2+ ROWS whatever their disabled bits, as the
// warp cue does). The visitor is handed the run's ROWS as well as its
// contribution so that the collapse classifier
// (magnification_level_collapse_members) can name the members of a collapsed
// run without a second walk restating this rule.
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
            visit(MagnificationLevelRun{markers[i].time_frame, uint8_t{0},
                                        i, j, /*collapsed=*/true});
        } else if (enabled == 1) {
            visit(MagnificationLevelRun{markers[i].time_frame,
                                        markers[last_enabled].level,
                                        i, j, /*collapsed=*/false});
        }
        i = j;
    }
}

}  // namespace

// The contract is at the declaration. The one run walk, keeping the last
// contributed level at or before `frame` — which is the rule's "each level
// holds to the next", its "a disabled marker is invisible" and its coincident
// collapse to level 0, all at once.
uint8_t magnification_level_in_force(
        const std::vector<GuiMagnificationLevelMarker>& markers,
        int64_t frame) {
    uint8_t level = 0;
    for_each_magnification_level_run(
        markers, [&level, frame](const MagnificationLevelRun& run) {
            if (run.frame <= frame) level = run.level;
        });
    return level;
}

// The contract is at the declaration. The second reader of the one run walk:
// a run the walk reports as collapsed is exactly a run the rule reads as
// level 0 for having 2+ enabled members, and its ENABLED rows are the members
// this marks — the disabled rows of such a run are no members for the rule
// (the walk never counted them) and step like any disabled marker.
std::vector<char> magnification_level_collapse_members(
        const std::vector<GuiMagnificationLevelMarker>& markers) {
    std::vector<char> members(markers.size(), 0);
    for_each_magnification_level_run(
        markers, [&members, &markers](const MagnificationLevelRun& run) {
            if (!run.collapsed) return;
            for (std::size_t k = run.begin; k < run.end; ++k)
                if (!markers[k].disabled) members[k] = 1;
        });
    return members;
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
