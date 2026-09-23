#pragma once

#include "marker_store.h"
#include "magnificationlevelmarkers_parse.h"
#include "marker_magnification.h"   // the level grammar and its ONE range
                                    // owner, kMarkerMagnificationMax

#include <expected>
#include <optional>
#include <string>
#include <vector>

// THE MAGNIFICATION LEVEL MARKER COLUMN'S GUI STORE (architect 2026-09-15):
// the third marker column, and since 2026-09-23 an INERT one — it has NO
// PICTURE READER: the waveform's magnification is the continuous gain derived
// from the source (derive_waveform_gain, waveform_gain.h), so a level moves no
// pixel. No render, render fingerprint or preview input reads it either. Its
// authoring acts, its flags and its red cue still stand. The grammar and the
// column's reason are at the parser (magnificationlevelmarkers_parse.h).
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

// THE COLUMN'S LEVEL-PER-FRAME RULE (architect 2026-09-15; the coincident
// collapse 2026-09-16), a step function over source frames that no picture
// reads any more (the store is inert, above) and that the column's own acts
// still ask:
//   * LEVEL 0 HOLDS BEFORE THE FIRST ENABLED MARKER;
//   * each ENABLED marker's level holds from its frame up to the next enabled
//     marker's frame (the song end for the last);
//   * a DISABLED marker is invisible: the level in force walks straight past
//     it;
//   * COINCIDENT LEVELS COLLAPSE TO THE NEUTRAL LEVEL 0: a run of 2+ ENABLED
//     markers at one exact frame contributes level 0 at that frame, holding
//     to the next enabled marker's frame — the WARP column's rule on this
//     axis, where a run of 2+ effectively enabled tempo markers collapses to a
//     neutral 1.00 owner (warp_coincident_collapse_members,
//     warp_frame_map_build.h). The run's tally counts ENABLED members alone; a
//     run with exactly one enabled member contributes that member's level, a
//     run with none contributes nothing. Store order among equal frames is
//     therefore invisible to the rule.
// One run walk in the .cpp (for_each_magnification_level_run) owns it, and
// the two readers below both call it, so neither can drift from the other.

// THE LEVEL IN FORCE AT A SOURCE FRAME — the rule above READ AT ONE POINT.
// Its one caller is THE DROP
// (GuiMagnificationLevelMarkersOps::drop_magnification_level_at_position),
// whose new marker copies the level already in force at the playhead.
// `markers` is the store in its resting (frame-ascending) order.
uint8_t magnification_level_in_force(
    const std::vector<GuiMagnificationLevelMarker>& markers, int64_t frame);

// THE COLLAPSE MEMBERS (architect 2026-09-16): one byte per store row, 1 for
// every ENABLED member of a frame run with 2+ enabled members — the rows whose
// levels the rule above reads as the neutral level 0 — and 0 for every other
// row, a disabled row inside such a run included (the rule never counts it,
// so it is no member and steps like any disabled marker). The warp column's
// classifier in this column's terms (warp_coincident_collapse_members,
// warp_frame_map_build.h; that one marks the whole raw run and leaves the
// enabled test to its readers, this one answers the enabled question itself
// because every reader asks it). ONE READER, the red-flag cache's `collapsed`
// subset (magnification_level_red_flag_set_cached, warp_frame_map_view.h),
// which memoizes it per store generation for the level step's kind refusal,
// the group step's wall and the Up / Down face. The same run walk the
// level-in-force takes, so a run is collapsed here iff the rule collapses it.
// `markers` is the store in its resting (frame-ascending) order.
std::vector<char> magnification_level_collapse_members(
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
