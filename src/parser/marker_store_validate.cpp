#include "marker_store_validate.h"

#include "time_format.h"           // format_timestamp

#include <string>
#include <vector>

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
    // Phase reset wall: total - 1, identical to the warp compare above, not
    // a total-exact wall. Warp walls at total-1 structurally
    // (build_warp_frame_map refuses sub-frame segments); phase reset walls at
    // total-1 by ruling: a reset in the last source frame has nothing left to
    // re-ground, and total-1 keeps every marker inside the playhead's
    // [0, total-1] domain so marker gestures and playhead syncs agree exactly.
    // A stored reset at exactly total frames is adversarial load-fatal in both
    // products (two-category rule); the position is musically meaningless.
    for (const auto& m : phase_resets) {
        if (m.time_frame > total_frames - 1) {
            return "phase reset marker past end of audio at " +
                   format_timestamp(m.time_frame / sr_d);
        }
    }
    const struct { const char* name; const SettingsTrim& t; } tabs[] = {
        {"tab A", tab_a_trim}, {"tab B", tab_b_trim},
    };
    // Both bounds are always meaningful since the trim window became
    // always-set, so both wall compares are unconditional — the has-bit guards
    // died with SettingsTrim's unset machinery (architect approval 2026-07-30;
    // the granted `-1` deletion in settings_file.{h,cpp} forces exactly these
    // two conditions away, nothing else in this file changed).
    for (const auto& [name, t] : tabs) {
        if (t.begin_frame > total_frames - 1) {
            return std::string(name) + " trim begin past end of audio at " +
                   format_timestamp(t.begin_frame / sr_d);
        }
        if (t.end_frame > total_frames - 1) {
            return std::string(name) + " trim end past end of audio at " +
                   format_timestamp(t.end_frame / sr_d);
        }
    }
    return std::nullopt;
}
