#include "warpmarkers.h"

#include "frame_format.h"
#include "settings_io.h"
#include "value_format.h"
#include "warp_frame_map_build.h"

#include <sstream>

std::expected<void, std::string> GuiWarpMarkers::load(const std::string& path) {
    // The parse fills each serialized WarpMarker base; the shared load_impl
    // (clear-bump-parse-upcast) copies it into a GuiWarpMarker whose
    // session-only iter/bpm fields keep their defaults.
    return load_impl(path, parse_warpmarkers_file);
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
    return save(path, markers());
}

bool GuiWarpMarkers::save(const std::string& path,
                      const std::vector<GuiWarpMarker>& markers_) {
    // Authored domain: positions are whole source frames (int64), written
    // as plain integer text (format_authored_frame).
    return save_impl(path, markers_);
}

bool effective_disabled(const std::vector<GuiWarpMarker>& markers, int idx) {
    // Bounds-guarded GUI face of the ONE cascade definition,
    // marker_effectively_disabled (warp_frame_map_build.h) — the same
    // verdict the resolver's keep mask publishes, instantiated over
    // GuiWarpMarker so paint-loop callers pay no slice copy.
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return false;
    return marker_effectively_disabled(markers, static_cast<size_t>(idx));
}
