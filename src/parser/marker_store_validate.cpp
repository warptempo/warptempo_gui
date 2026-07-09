#include "marker_store_validate.h"

#include "warp_frame_map_build.h"  // validate_first_marker_render_grammar
#include "time_format.h"           // format_timestamp

#include <algorithm>
#include <string>
#include <vector>

namespace {

// Adjacent-pair coincident grouping over a time-sorted column. A pair is
// coincident when (t[i+1] - t[i]) < window_frames, where window_frames is
// kCoincidenceWindowSeconds converted once to frames by the caller — the
// stored positions are frame doubles and are compared exactly, with no
// rounding; only the window constant crosses domains (it is pinned in the
// zoom table's ms-per-pixel terms). The window is one deepest-zoom pixel of
// time, deliberately WIDER than the owners' sub-frame refusals: markers closer
// than one pixel cannot be mouse-picked apart at any zoom. Chains of adjacent
// coincident pairs merge into one group; disabled markers participate (the
// rule is per-column pickability, not render participation).
std::vector<std::vector<size_t>> coincident_groups(
    const std::vector<double>& times, double window_frames) {
    std::vector<std::vector<size_t>> groups;
    size_t i = 0;
    while (i + 1 < times.size()) {
        if ((times[i + 1] - times[i]) < window_frames) {
            std::vector<size_t> group;
            group.push_back(i);
            size_t j = i;
            while (j + 1 < times.size() &&
                   (times[j + 1] - times[j]) < window_frames) {
                group.push_back(j + 1);
                ++j;
            }
            groups.push_back(std::move(group));
            i = j + 1;
        } else {
            ++i;
        }
    }
    return groups;
}

}  // namespace

std::vector<MarkerDefect> enumerate_marker_store_defects(
    const std::vector<WarpMarker>&       warp_markers,
    const std::vector<PhaseResetMarker>& phase_resets,
    long sample_rate) {
    std::vector<MarkerDefect> defects;
    const double sr_d = static_cast<double>(sample_rate);
    // The one domain crossing: the pickability window (a seconds constant,
    // zoom-table domain) expressed in frames for the exact frame-double
    // compares below.
    const double window_frames = kCoincidenceWindowSeconds * sr_d;

    // First-marker grammar (warp only — phase resets carry no tempo, so there
    // is no grammar to anchor). The validator handles the empty list itself,
    // returning its own message; the defect anchors at 0.0 with indices {0}
    // when the list is non-empty, empty otherwise.
    if (auto v = validate_first_marker_render_grammar(warp_markers,
                                                      sample_rate); !v) {
        MarkerDefect d;
        d.kind         = MarkerDefectKind::FirstMarkerGrammar;
        d.column       = 'W';
        d.time_frame = 0.0;
        if (!warp_markers.empty()) d.indices.push_back(0);
        d.message = std::move(v.error());
        defects.push_back(std::move(d));
    }

    // Coincident groups, per column independently. One defect per group; time
    // is the group's first member's time, indices are all members.
    {
        std::vector<double> warp_times;
        warp_times.reserve(warp_markers.size());
        for (const auto& m : warp_markers) warp_times.push_back(m.time_frame);
        for (auto& group : coincident_groups(warp_times, window_frames)) {
            MarkerDefect d;
            d.kind       = MarkerDefectKind::CoincidentGroup;
            d.column     = 'W';
            d.time_frame = warp_times[group.front()];
            d.message = "coincident warp markers at "
                        + format_timestamp(d.time_frame / sr_d);
            d.indices = std::move(group);
            defects.push_back(std::move(d));
        }

        std::vector<double> reset_times;
        reset_times.reserve(phase_resets.size());
        for (const auto& m : phase_resets) reset_times.push_back(m.time_frame);
        for (auto& group : coincident_groups(reset_times, window_frames)) {
            MarkerDefect d;
            d.kind       = MarkerDefectKind::CoincidentGroup;
            d.column     = 'P';
            d.time_frame = reset_times[group.front()];
            d.message = "coincident phase reset markers at "
                        + format_timestamp(d.time_frame / sr_d);
            d.indices = std::move(group);
            defects.push_back(std::move(d));
        }
    }

    // Past-EOF is not enumerated here: a marker past its column's wall is
    // adversarial input, hard-failed at the load boundary (GUI file_loader /
    // CLI) like a corrupt audio file, so every position this enumerator sees is
    // inside its wall. build_warp_frame_map and build_phase_reset_source_frames
    // keep their own EOF refusals as breach backstops for hand-edited artifacts.

    // Dangling label refs (warp column only; labels are warp-only, a recorded
    // asymmetry). Collect all non-empty label_def names, then each marker whose
    // label_ref is non-empty and names no def is one defect. Duplicate defs are
    // load-fatal upstream; that case does not arise here.
    {
        std::vector<std::string> defs;
        for (const auto& m : warp_markers) {
            if (!m.label_def.empty()) defs.push_back(m.label_def);
        }
        for (size_t i = 0; i < warp_markers.size(); ++i) {
            const std::string& ref = warp_markers[i].label_ref;
            if (ref.empty()) continue;
            if (std::find(defs.begin(), defs.end(), ref) != defs.end()) continue;
            MarkerDefect d;
            d.kind         = MarkerDefectKind::DanglingLabelRef;
            d.column       = 'W';
            d.time_frame = warp_markers[i].time_frame;
            d.indices.push_back(i);
            d.message = "label reference " + ref + " at "
                        + format_timestamp(d.time_frame / sr_d)
                        + " has no label definition";
            defects.push_back(std::move(d));
        }
    }

    // Chronological sort, fully deterministic: by time, then FirstMarkerGrammar
    // first, then column 'W' before 'P', then kind enum order, then lowest
    // index.
    std::stable_sort(defects.begin(), defects.end(),
        [](const MarkerDefect& a, const MarkerDefect& b) {
            if (a.time_frame != b.time_frame)
                return a.time_frame < b.time_frame;
            const bool a_first = a.kind == MarkerDefectKind::FirstMarkerGrammar;
            const bool b_first = b.kind == MarkerDefectKind::FirstMarkerGrammar;
            if (a_first != b_first) return a_first;
            if (a.column != b.column) return a.column == 'W';
            if (a.kind != b.kind) return a.kind < b.kind;
            const size_t ai = a.indices.empty() ? 0 : a.indices.front();
            const size_t bi = b.indices.empty() ? 0 : b.indices.front();
            return ai < bi;
        });

    return defects;
}
