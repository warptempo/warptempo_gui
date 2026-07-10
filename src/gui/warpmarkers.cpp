#include "warpmarkers.h"

#include "frame_format.h"
#include "settings_io.h"
#include "value_format.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

std::expected<void, std::string> GuiWarpMarkers::load(const std::string& path) {
    markers_.clear();
    ++generation_;

    auto r = parse_warpmarkers_file(path);
    if (!r) return std::unexpected(std::move(r.error()));

    markers_.reserve(r->size());
    for (const WarpMarker& wm : *r) {
        GuiWarpMarker g;                    // session-only fields default
        static_cast<WarpMarker&>(g) = wm;   // copy the serialized base
        markers_.push_back(g);
    }
    return {};
}

namespace {

// One serializer body for both save domains; only the position spelling
// differs. `format_position` is format_frame_double for authored saves
// (whole source frames, plain integer text) and format_render_frame_double
// for the render-display sidecars (fractional target-axis doubles,
// shortest round-trip text).
//
// Serializer contract: this save performs no ordering validation.
// The store is sorted by construction — ordered insert for drops, and
// every time-mutating gesture (drag commit, shift, nudge) reorders
// through the reorder-and-remap path — so rows serialize in
// non-decreasing time order. Equal-time rows are legal (markers may
// overlap exactly) and load back under the relaxed parser, which
// accepts non-decreasing times; only a DECREASING sequence —
// impossible from the sorted store, so evidence of a future op bug —
// fails the next load with a loud line-numbered parse error. Warp
// ties are refused at the strict render boundary
// (build_warp_frame_map), not by this serializer. Positions persist
// through the domain's own pair (frame_format.h), so a saved store
// reloads bit-identically under that domain's parse.
bool save_impl(const std::string& path,
               const std::vector<GuiWarpMarker>& markers_,
               std::string (*format_position)(double)) {
    std::ostringstream out;
    for (const auto& m : markers_) {
        // Canonical new format, no whitespace anywhere on the line:
        //   [#]?<frame double>|PAYLOAD
        if (m.disabled) out << '#';
        out << format_position(m.time_frame) << '|';

        // Payload:
        //   label_ref               → "a.42"
        //   inherit, no def         → "pass"
        //   inherit, with def       → "pass:a.42"
        //   owning, no scale        → "1.23"
        //   owning, with scale      → "1.23*1.2345"
        //   def, no scale           → "1.23:a.03"
        //   def, with scale         → "1.23*1.2345:a.03"
        // Values persist as padded shortest-round-trip doubles
        // (value_format.h: tempo min 2 decimals, scale min 4), so a saved
        // store reloads bit-identically and historical fixed-decimal forms
        // re-serialize byte-for-byte.
        if (!m.label_ref.empty()) {
            out << m.label_ref;
        } else {
            if (m.tempo_inherits) {
                out << "pass";
            } else {
                out << format_value_double(m.tempo_base, 2);
                if (m.tempo_scale.has_value()) {
                    out << '*' << format_value_double(*m.tempo_scale, 4);
                }
            }
            if (!m.label_def.empty()) {
                out << ':' << m.label_def;
            }
        }

        out << '\n';
    }
    const std::string data = out.str();

    // tmp + fsync + rename, preserving the existing file's mode.
    return atomic_write_string_to_path(path, data);
}

}  // namespace

bool GuiWarpMarkers::save(const std::string& path) const {
    return save(path, markers_);
}

bool GuiWarpMarkers::save(const std::string& path,
                      const std::vector<GuiWarpMarker>& markers_) {
    // Authored domain: positions are whole source frames, written as plain
    // integer text (format_frame_double).
    return save_impl(path, markers_, &format_frame_double);
}

bool GuiWarpMarkers::save_render_display(
        const std::string& path,
        const std::vector<GuiWarpMarker>& markers_) {
    // Render-display domain (.renderwarpmarkers): positions live on the
    // render's target axis, generically fractional, written as shortest
    // round-trip doubles (format_render_frame_double) — NOT the authored
    // integer grammar.
    return save_impl(path, markers_, &format_render_frame_double);
}

int GuiWarpMarkers::insert_marker(GuiWarpMarker m) {
    auto it = std::lower_bound(
        markers_.begin(), markers_.end(), m.time_frame,
        [](const GuiWarpMarker& a, double t) { return a.time_frame < t; });
    const int idx = static_cast<int>(it - markers_.begin());
    markers_.insert(it, std::move(m));
    ++generation_;
    return idx;
}

void GuiWarpMarkers::remove_marker(int index) {
    if (index < 0 || index >= static_cast<int>(markers_.size())) return;
    markers_.erase(markers_.begin() + index);
    ++generation_;
}

bool effective_disabled(const std::vector<GuiWarpMarker>& markers, int idx) {
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return false;
    const auto& m = markers[idx];
    if (m.disabled) return true;
    if (!m.label_ref.empty()) {
        // Walk all markers to find the definition. O(N^2) across the list
        // but N is small (hundreds max).
        for (const auto& other : markers) {
            if (!other.label_def.empty() &&
                other.label_def == m.label_ref) {
                return other.disabled;
            }
        }
    }
    return false;
}
