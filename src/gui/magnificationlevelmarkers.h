#pragma once

#include "marker_store.h"
#include "magnificationlevelmarkers_parse.h"
#include "warpmarkers.h"   // WaveformGainProfile, the picture's step function

#include <expected>
#include <optional>
#include <string>
#include <vector>

// THE MAGNIFICATION LEVEL MARKER COLUMN'S GUI STORE (architect 2026-09-15):
// the third marker column, display-only — the waveform picture's gain profile
// is its one reader (effective_waveform_gain_profile, warp_frame_map_view.h),
// and no render, render fingerprint or preview input reads it. The grammar and
// the column's reason are at the parser (magnificationlevelmarkers_parse.h).
//
// The GUI type exists so the store has its own type over the shared
// GuiMarkerStore, mirroring the WarpMarker / GuiWarpMarker and
// PhaseResetMarker / GuiPhaseResetMarker splits. It carries NO session-only
// field: the column has no iteration bracket, so nothing needs stripping from
// an undo snapshot and nothing is dropped by the slice below.
struct GuiMagnificationLevelMarker : MagnificationLevelMarker {};

// Slice a GUI vector down to the serialized base, mirroring
// slice_to_phase_reset_markers — the form the shared past-EOF wall
// (first_past_eof_wall_defect, marker_store_validate.h) takes.
inline std::vector<MagnificationLevelMarker>
slice_to_magnification_level_markers(
    const std::vector<GuiMagnificationLevelMarker>& src) {
    return std::vector<MagnificationLevelMarker>(src.begin(), src.end());
}

// The row equality of two whole columns — every serialized field, in store
// order: the undo coalescing's net-zero question (entry_restores_live_marker_stores,
// undo.cpp) asks it of this column beside the other two.
inline bool magnification_level_rows_equal(
    const std::vector<GuiMagnificationLevelMarker>& a,
    const std::vector<GuiMagnificationLevelMarker>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].time_frame != b[i].time_frame || a[i].level != b[i].level ||
            a[i].disabled != b[i].disabled)
            return false;
    }
    return true;
}

// THE WAVEFORM GAIN PROFILE BUILT FROM THIS COLUMN — the whole meaning of a
// magnification level marker (architect 2026-09-15). A step function over
// source frames (the WaveformGainProfile contract, warpmarkers.h):
//   * LEVEL 0 HOLDS BEFORE THE FIRST ENABLED MARKER;
//   * each ENABLED marker's level holds from its frame up to the next enabled
//     marker's frame (the song end for the last) — a level-0 marker is how
//     the picture returns to unmagnified;
//   * a DISABLED marker is invisible: the level in force walks straight past
//     it;
//   * ON EQUAL FRAMES THE LAST ENABLED MARKER IN STORE ORDER WINS — the
//     store keeps equal frames in insertion order and a breakpoint names one
//     level per frame, so the later row replaces the earlier one's;
//   * a breakpoint exists only where the level CHANGES, so level 0 everywhere
//     is the empty profile (hash 0).
// Pure; memoized per store generation by waveform_gain_profile_cached
// (warp_frame_map_view.h).
WaveformGainProfile build_waveform_gain_profile(
    const std::vector<GuiMagnificationLevelMarker>& markers);

// The store mechanics (sorted vector, generation token, insert/remove/mut
// accessors) are the shared GuiMarkerStore base (marker_store.h); this class
// carries the column's parse and serializer surfaces.
class GuiMagnificationLevelMarkers
    : public GuiMarkerStore<GuiMagnificationLevelMarker> {
public:
    // Parses `path`. On success populates markers(); the first malformed line
    // aborts the parse with a one-line error, and a missing or unopenable file
    // is a failure (the sidecar is required once a project carries any
    // sidecar; the empty file is the no-markers form). No throw.
    // `path_free_reason` is the parser's own out-parameter, passed through
    // unread (parse_magnificationlevelmarkers_file).
    std::expected<void, std::string> load(
        const std::string& path,
        std::optional<std::string>* path_free_reason = nullptr);

    // Writes the canonical form to `path` through the atomic writer (tmp +
    // fsync + rename). Returns true on success.
    bool save(const std::string& path) const;

    // Static variant for callers holding a raw vector (the render pipeline
    // writing the column's copy beside a batch render).
    static bool save(const std::string& path,
                     const std::vector<GuiMagnificationLevelMarker>& markers);
};

// The `.magnificationlevelmarkers` file's exact bytes for `markers`, built
// without touching disk — the string half both save() overloads hand to the
// atomic writer, and the GitHub recheck's "now" side's text (history_diff.h).
std::string format_magnificationlevelmarkers_text(
    const std::vector<GuiMagnificationLevelMarker>& markers);
