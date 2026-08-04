#pragma once

#include "marker_store.h"
#include "phaseresetmarkers_parse.h"

#include <expected>
#include <string>
#include <vector>

// The GUI's authoring view. Adds no fields today; it exists so the GUI marker
// store has its own type and the parser can fill the PhaseResetMarker base by
// reference (a GuiPhaseResetMarker binds to PhaseResetMarker& by upcast),
// mirroring the WarpMarker / GuiWarpMarker split.
struct GuiPhaseResetMarker : PhaseResetMarker {};

// Slice a GUI phase-reset vector down to the serialized base, mirroring
// slice_to_warp_markers. Used at the render boundary so the parser-domain
// phase-reset assembly (build_phase_reset_source_frames) never sees the GUI
// type.
inline std::vector<PhaseResetMarker> slice_to_phase_reset_markers(
    const std::vector<GuiPhaseResetMarker>& src) {
    return std::vector<PhaseResetMarker>(src.begin(), src.end());
}

// The store mechanics (sorted vector, generation token, insert/remove/mut
// accessors) are the shared GuiMarkerStore base (marker_store.h); this
// class carries the phase reset column's parse and serializer surfaces.
class GuiPhaseResetMarkers : public GuiMarkerStore<GuiPhaseResetMarker> {
public:
    // Parses `path`. On success, populates markers() and returns the parsed
    // markers. The first malformed line aborts the parse and returns a
    // one-line error; a missing/unopenable file is a failure (the sidecar is
    // created at source load and required at every load boundary; the empty
    // file is the no-resets form). No throw.
    std::expected<void, std::string> load(const std::string& path);

    // Writes the canonical form to `path`. Atomic: writes to <path>.tmp,
    // fsyncs, then renames. Preserves existing permissions or uses 0644 if
    // the file is new. Returns true on success. Save writes every row with
    // no dedup or ordering validation; the serializer contract (the store
    // is sorted by construction, equal-time rows are legal and reload,
    // and the render boundary — not the serializer — collapses an
    // exact-equal group to one event) is documented at
    // format_phaseresetmarkers_text in phaseresetmarkers.cpp.
    bool save(const std::string& path) const;

    // Static variant for callers that hold a raw GuiPhaseResetMarker vector
    // (e.g. the render pipeline writing the authored .phaseresetmarkers copy
    // beside a batch render). Same on-disk format as the instance method:
    // authored positions, whole source frames as plain integer text.
    static bool save(const std::string& path,
                     const std::vector<GuiPhaseResetMarker>& markers);
};

// The `.phaseresetmarkers` file's exact bytes for `markers`, built and
// returned without touching disk — the string half both save() overloads hand
// to the atomic writer, so the two can never diverge. Its other consumer is
// the GitHub recheck's "now" side (history_diff.h), which diffs the live store
// against a committed snapshot and needs precisely what a Ctrl+S would land at
// this instant, with no file anywhere. The serializer contract is at the
// definition.
std::string format_phaseresetmarkers_text(
    const std::vector<GuiPhaseResetMarker>& markers);
