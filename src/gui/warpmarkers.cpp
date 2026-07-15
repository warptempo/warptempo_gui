#include "warpmarkers.h"

#include "frame_format.h"
#include "settings_io.h"
#include "value_format.h"
#include "warp_frame_map_build.h"

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

// Serializer contract: this save performs no ordering validation.
// The store is sorted by construction — ordered insert for drops, and
// every time-mutating gesture (drag commit, shift, nudge) reorders
// through the reorder-and-remap path — so rows serialize in
// non-decreasing time order. Equal-time rows are legal (markers may
// overlap exactly) and load back under the relaxed parser, which
// accepts non-decreasing times; only a DECREASING sequence —
// impossible from the sorted store, so evidence of a future op bug —
// fails the next load with a loud line-numbered parse error. Warp
// ties collapse to one plain 1.00 owner at the render boundary
// (build_warp_frame_map), not by this serializer. Positions are authored
// whole source frames and persist through the authored pair
// (frame_format.h), so a saved store reloads bit-identically under the
// authored parse.
bool save_impl(const std::string& path,
               const std::vector<GuiWarpMarker>& markers_) {
    std::ostringstream out;
    for (size_t i = 0; i < markers_.size(); ++i) {
        const auto& m = markers_[i];
        // Canonical new format, no whitespace anywhere on the line:
        //   [#]?<frame position>|PAYLOAD
        if (m.disabled) out << '#';
        out << format_authored_frame(m.time_frame) << '|';

        // Payload:
        //   label_ref               → "a.42"
        //   inherit, no def         → "pass"
        //   inherit, with def       → "pass:a.42"
        //   owning, no scale        → "1.23"
        //   owning, with scale      → "1.23*1.2345"
        //   def, no scale           → "1.23:a.03"
        //   def, with scale         → "1.23*1.2345:a.03"
        // Tempo persists through its integer-cents serialization owner
        // (format_tempo_cents, value_format.h — the exact N.NN text, byte-
        // identical to the historical min-2-padded form); scale persists as
        // a padded shortest-round-trip double (format_value_double, min 4).
        // A saved store reloads bit-identically and historical
        // fixed-decimal forms re-serialize byte-for-byte.
        if (!m.label_ref.empty()) {
            out << m.label_ref;
        } else {
            if (m.tempo_inherits) {
                out << "pass";
            } else {
                out << format_tempo_cents(m.tempo_cents);
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
    // Authored domain: positions are whole source frames (int64), written
    // as plain integer text (format_authored_frame).
    return save_impl(path, markers_);
}

int GuiWarpMarkers::insert_marker(GuiWarpMarker m) {
    auto it = std::lower_bound(
        markers_.begin(), markers_.end(), m.time_frame,
        [](const GuiWarpMarker& a, int64_t t) { return a.time_frame < t; });
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
    // Bounds-guarded GUI face of the ONE cascade definition,
    // marker_effectively_disabled (warp_frame_map_build.h) — the same
    // verdict the resolver's keep mask publishes, instantiated over
    // GuiWarpMarker so paint-loop callers pay no slice copy.
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return false;
    return marker_effectively_disabled(markers, static_cast<size_t>(idx));
}
