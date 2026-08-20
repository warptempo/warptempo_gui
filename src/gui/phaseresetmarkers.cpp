#include "phaseresetmarkers.h"

#include "frame_format.h"
#include "settings_io.h"

#include <sstream>

std::expected<void, std::string> GuiPhaseResetMarkers::load(const std::string& path) {
    // The parse fills each serialized PhaseResetMarker base; the shared
    // load_impl (clear-bump-parse-upcast) copies it into a
    // GuiPhaseResetMarker (no extra fields today).
    return load_impl(path, parse_phaseresetmarkers_file);
}

// Serializer contract: this serialization performs no ordering validation.
// The store is sorted by construction — ordered insert for drops and
// propagate, and every time-mutating gesture (drag commit, shift,
// nudge) reorders through the reorder-and-remap path — so rows
// serialize in non-decreasing time order. Equal-time rows are legal
// (markers may overlap exactly) and load back under the relaxed
// parser, which accepts non-decreasing times; only a DECREASING
// sequence — impossible from the sorted store, so evidence of a
// future op bug — fails the next load with a loud line-numbered
// parse error. Coincident stacks are legal in the store — the parser
// normalizes them at render/preview time (equal-frame enabled resets
// collapse to one event, one stderr line per collapsed timestamp) —
// so this serializer never refuses them. Positions are authored whole
// source frames and persist
// through the authored pair (frame_format.h), so a saved store
// reloads bit-identically under the authored parse.
//
// The file's bytes and the disk write are separate halves: this one builds
// the text and touches nothing, save() hands it to the atomic writer. The
// second consumer is the GitHub recheck's "now" side (history_diff.h), which
// needs exactly the bytes a Ctrl+S would land at this instant without a file
// existing anywhere.
std::string format_phaseresetmarkers_text(
    const std::vector<GuiPhaseResetMarker>& markers_) {
    std::ostringstream out;
    for (size_t i = 0; i < markers_.size(); ++i) {
        const auto& m = markers_[i];
        // `[#]<frame position>[ //<measure>]`. The `#` disable prefix composes
        // ahead of the position, exactly as the parser strips it. No mode
        // suffix — heap is the sole engine, so there is no peak/heap/pass mode
        // to record.
        if (m.disabled) out << '#';
        out << format_authored_frame(m.time_frame);

        // The measure suffix (marker_measure.h), and the one place a
        // space may appear on a marker line. An EMPTY measure emits nothing at
        // all — the bare ` //` separator is load-fatal precisely because this
        // writer never produces it, which is what keeps the removal path (an
        // empty commit in the measure editor) and the load rules in agreement.
        if (!m.measure.empty()) {
            out << " //" << m.measure;
        }

        out << '\n';
    }
    return out.str();
}

bool GuiPhaseResetMarkers::save(const std::string& path) const {
    return save(path, markers());
}

bool GuiPhaseResetMarkers::save(const std::string& path,
                         const std::vector<GuiPhaseResetMarker>& markers_) {
    // Authored domain: positions are whole source frames (int64), written
    // as plain integer text (format_authored_frame).
    // tmp + fsync + rename, preserving the existing file's mode.
    return atomic_write_string_to_path(
        path, format_phaseresetmarkers_text(markers_));
}
