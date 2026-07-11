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
// stored positions are whole int64 frames; their exact integer difference
// widens to double for the compare, with no rounding; only the window
// constant crosses domains (it is pinned in the
// zoom table's ms-per-pixel terms). The window is one deepest-zoom pixel of
// time, deliberately WIDER than the owners' sub-frame refusals: markers closer
// than one pixel cannot be mouse-picked apart at any zoom. Chains of adjacent
// coincident pairs merge into one group; disabled markers participate (the
// rule is per-column pickability, not render participation).
std::vector<std::vector<size_t>> coincident_groups(
    const std::vector<int64_t>& times, double window_frames) {
    std::vector<std::vector<size_t>> groups;
    size_t i = 0;
    while (i + 1 < times.size()) {
        if (static_cast<double>(times[i + 1] - times[i]) < window_frames) {
            std::vector<size_t> group;
            group.push_back(i);
            size_t j = i;
            while (j + 1 < times.size() &&
                   static_cast<double>(times[j + 1] - times[j])
                       < window_frames) {
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
    // zoom-table domain) expressed in frames for the exact integer-difference
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
        d.time_frame = 0;
        if (!warp_markers.empty()) d.indices.push_back(0);
        d.message = std::move(v.error());
        defects.push_back(std::move(d));
    }

    // Coincident groups, per column independently. One defect per group; time
    // is the group's first member's time, indices are all members.
    {
        std::vector<int64_t> warp_times;
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

        std::vector<int64_t> reset_times;
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

    // Dangling label refs and pass-after-label-ref (both warp column only;
    // labels are warp-only, a recorded asymmetry). Dangling refs first:
    // collect all non-empty label_def names, then each marker whose
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

        // Pass-after-label-ref: an enabled pass whose immediate prior
        // surviving marker is an enabled label ref. The predicate is the
        // owner's own comparison — validate_pass_inheritance_source, the
        // check resolve_warp_markers_for_render applies when materializing
        // each pass — called per index like the first-marker mirror above,
        // so the message text mirrors the render refusal exactly. One defect
        // per offending pass, anchored at the pass's time with the pass's
        // index.
        for (size_t i = 0; i < warp_markers.size(); ++i) {
            if (auto v = validate_pass_inheritance_source(warp_markers, i,
                                                          sample_rate); !v) {
                MarkerDefect d;
                d.kind       = MarkerDefectKind::PassAfterLabelRef;
                d.column     = 'W';
                d.time_frame = warp_markers[i].time_frame;
                d.indices.push_back(i);
                d.message = std::move(v.error());
                defects.push_back(std::move(d));
            }
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

std::optional<std::string> first_past_eof_wall_defect(
        const std::vector<WarpMarker>&       warp_markers,
        const std::vector<PhaseResetMarker>& phase_resets,
        const SettingsTrim& tab_a_trim,
        const SettingsTrim& tab_b_trim,
        int64_t total_frames,
        long    sample_rate) {
    const double sr_d = static_cast<double>(sample_rate);
    for (const auto& m : warp_markers) {
        if (m.time_frame > total_frames - 1) {
            return "warp marker past end of audio at " +
                   format_timestamp(m.time_frame / sr_d);
        }
    }
    // Phase reset wall: total - 1, identical to the warp compare above —
    // the per-column marker wall split (warp total-1, phase reset total) is
    // retired. Warp walls at total-1 structurally (build_warp_frame_map
    // refuses sub-frame segments); phase reset walls at total-1 by ruling:
    // a reset in the last source frame has nothing left to re-ground, and
    // total-1 keeps every marker inside the playhead's [0, total-1] domain
    // so marker gestures and playhead syncs agree exactly. A stored reset
    // at exactly total frames — previously legal — is now adversarial
    // load-fatal in both products (two-category rule); the position is
    // musically meaningless and there is deliberately no migration.
    for (const auto& m : phase_resets) {
        if (m.time_frame > total_frames - 1) {
            return "phase reset marker past end of audio at " +
                   format_timestamp(m.time_frame / sr_d);
        }
    }
    const struct { const char* name; const SettingsTrim& t; } tabs[] = {
        {"tab A", tab_a_trim}, {"tab B", tab_b_trim},
    };
    for (const auto& [name, t] : tabs) {
        if (t.has_begin && t.begin_frame > total_frames - 1) {
            return std::string(name) + " trim begin past end of audio at " +
                   format_timestamp(t.begin_frame / sr_d);
        }
        if (t.has_end && t.end_frame > total_frames) {
            return std::string(name) + " trim end past end of audio at " +
                   format_timestamp(t.end_frame / sr_d);
        }
    }
    return std::nullopt;
}

std::optional<std::string> first_view_range_defect(
        const SettingsFileTab& tab_a,
        const SettingsFileTab& tab_b,
        int64_t domain_total_frames) {
    // The comparisons are the historical load walls verbatim: viewport
    // start at or past the total refuses, playhead strictly past the total
    // refuses. playhead == total remains load-legal (files written before
    // the marker walls pulled in; view scratch is display state, and the
    // runtime clamp owns the value at first use).
    // The caller passes the persisted active_audio_view's domain
    // total (see the header comment); no timestamp is rendered because the
    // positions are view scratch, not authored times, and under 'T' a
    // frame-over-sample-rate rendering would name a time on the deformed
    // timeline, not in the audio.
    const struct { const char* name; const SettingsFileTab& t; } tabs[] = {
        {"tab A", tab_a}, {"tab B", tab_b},
    };
    for (const auto& [name, t] : tabs) {
        if (t.has_viewport_start &&
            t.viewport_start >= domain_total_frames) {
            return std::string(name) +
                   " viewport start past the end of the persisted audio view";
        }
        if (t.has_playhead && t.playhead > domain_total_frames) {
            return std::string(name) +
                   " playhead past the end of the persisted audio view";
        }
    }
    return std::nullopt;
}
