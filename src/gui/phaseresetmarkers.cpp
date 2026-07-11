#include "phaseresetmarkers.h"

#include "frame_format.h"
#include "settings_io.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

std::expected<void, std::string> GuiPhaseResetMarkers::load(const std::string& path) {
    markers_.clear();
    ++generation_;

    auto r = parse_phaseresetmarkers_file(path);
    if (!r) return std::unexpected(std::move(r.error()));

    markers_.reserve(r->size());
    for (const PhaseResetMarker& pm : *r) {
        GuiPhaseResetMarker g;                   // no extra fields today
        static_cast<PhaseResetMarker&>(g) = pm;  // copy the serialized base
        markers_.push_back(g);
    }
    return {};
}

namespace {

// One serializer body for both save domains; only the position spelling
// differs. `format_position` is a per-row callable (row index + marker):
// authored saves format the marker's own int64 time_frame as plain integer
// text (format_authored_frame); the render-display sidecars format a
// caller-supplied parallel vector of fractional target-axis doubles
// (format_render_frame_double) — the authored field cannot represent them.
//
// Serializer contract: this save performs no ordering validation.
// The store is sorted by construction — ordered insert for drops and
// propagate, and every time-mutating gesture (drag commit, shift,
// nudge) reorders through the reorder-and-remap path — so rows
// serialize in non-decreasing time order. Equal-time rows are legal
// (markers may overlap exactly) and load back under the relaxed
// parser, which accepts non-decreasing times; only a DECREASING
// sequence — impossible from the sorted store, so evidence of a
// future op bug — fails the next load with a loud line-numbered
// parse error. Coincident stacks are legal in the store only until
// their commit modal resolves — the commit funnel walks them as
// defects — so this serializer never refuses them;
// build_phase_reset_source_frames' sub-frame refusal is the breach
// backstop for hand-edited input, and build_warp_frame_map refuses
// warp ties. Positions persist through the domain's own pair
// (frame_format.h), so a saved store reloads bit-identically under
// that domain's parse — in the render-display domain the publisher's
// distinct-but-close fractional resets stay distinct on disk.
template <typename FormatPosition>
bool save_impl(const std::string& path,
               const std::vector<GuiPhaseResetMarker>& markers_,
               FormatPosition format_position) {
    std::ostringstream out;
    for (size_t i = 0; i < markers_.size(); ++i) {
        const auto& m = markers_[i];
        // `[#]<frame position>` only. The `#` disable prefix composes ahead
        // of the position, exactly as the parser strips it. No mode suffix —
        // heap is the sole engine, so there is no peak/heap/pass mode to
        // record.
        if (m.disabled) out << '#';
        out << format_position(i, m) << '\n';
    }
    const std::string data = out.str();

    // tmp + fsync + rename, preserving the existing file's mode.
    return atomic_write_string_to_path(path, data);
}

}  // namespace

bool GuiPhaseResetMarkers::save(const std::string& path) const {
    return save(path, markers_);
}

bool GuiPhaseResetMarkers::save(const std::string& path,
                         const std::vector<GuiPhaseResetMarker>& markers_) {
    // Authored domain: positions are whole source frames (int64), written
    // as plain integer text (format_authored_frame).
    return save_impl(path, markers_,
                     [](size_t, const GuiPhaseResetMarker& m) {
                         return format_authored_frame(m.time_frame);
                     });
}

bool GuiPhaseResetMarkers::save_render_display(
        const std::string& path,
        const std::vector<GuiPhaseResetMarker>& markers_,
        const std::vector<double>& render_frames) {
    // Render-display domain (.renderphaseresetmarkers): positions live on
    // the render's target axis, generically fractional, written as shortest
    // round-trip doubles (format_render_frame_double) — NOT the authored
    // integer grammar. The int64 authored field cannot hold them, so the
    // caller supplies the fractional positions in `render_frames`, parallel
    // to `markers_` (asserted by the size guard); each marker's own
    // time_frame is ignored here.
    if (render_frames.size() != markers_.size()) return false;
    return save_impl(path, markers_,
                     [&render_frames](size_t i, const GuiPhaseResetMarker&) {
                         return format_render_frame_double(render_frames[i]);
                     });
}

int GuiPhaseResetMarkers::insert_marker(GuiPhaseResetMarker m) {
    const int64_t time = m.time_frame;
    auto it = std::lower_bound(
        markers_.begin(), markers_.end(), time,
        [](const GuiPhaseResetMarker& a, int64_t t) { return a.time_frame < t; });
    const int idx = static_cast<int>(it - markers_.begin());
    markers_.insert(it, std::move(m));
    ++generation_;
    return idx;
}

void GuiPhaseResetMarkers::remove_marker(int index) {
    if (index < 0 || index >= static_cast<int>(markers_.size())) return;
    markers_.erase(markers_.begin() + index);
    ++generation_;
}
