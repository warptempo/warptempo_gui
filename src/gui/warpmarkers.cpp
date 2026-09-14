#include "warpmarkers.h"

#include "frame_format.h"
#include "marker_magnification.h"
#include "settings_io.h"
#include "value_format.h"
#include "warp_frame_map_build.h"

#include <sstream>
#include <unordered_map>

std::expected<void, std::string> GuiWarpMarkers::load(
        const std::string& path,
        std::optional<std::string>* path_free_reason) {
    // The parse fills each serialized WarpMarker base; the shared load_impl
    // (clear-bump-parse-upcast) copies it into a GuiWarpMarker whose
    // session-only iter/bpm fields keep their defaults. The path-free reason
    // rides through to the parser untouched.
    return load_impl(path, parse_warpmarkers_file, path_free_reason);
}

// Serializer contract: this serialization performs no ordering validation.
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
//
// The file's bytes and the disk write are separate halves: this one builds
// the text and touches nothing, save() hands it to the atomic writer. The
// second consumer is the GitHub recheck's "now" side (history_diff.h), which
// needs exactly the bytes a Ctrl+S would land at this instant without a file
// existing anywhere.
std::string format_warpmarkers_text(
    const std::vector<GuiWarpMarker>& markers_) {
    std::ostringstream out;
    for (size_t i = 0; i < markers_.size(); ++i) {
        const auto& m = markers_[i];
        // Canonical new format, no whitespace anywhere in the canonical
        // prefix:
        //   [#]?<frame position>|PAYLOAD[ //<measure>,<magnification>]
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

        // The comment (split_marker_comment, marker_measure.h), and the one
        // place a space may appear on a marker line: ` //<measure>,<mag>`
        // with the comma ALWAYS written, emitted iff either field is set, so
        // its three shapes are `//12,3`, `//12,` and `//,3` (architect
        // 2026-09-14). Both fields blank emits nothing at all — the empty
        // `//,` is load-fatal precisely because this writer never produces
        // it, which is what keeps the removal paths (an empty commit in a
        // field's editor) and the load rules in agreement.
        if (!m.measure.empty() || m.magnification.has_value()) {
            out << " //" << m.measure << ',';
            if (m.magnification.has_value())
                out << format_marker_magnification(*m.magnification);
        }

        out << '\n';
    }
    return out.str();
}

bool GuiWarpMarkers::save(const std::string& path) const {
    return save(path, markers());
}

bool GuiWarpMarkers::save(const std::string& path,
                      const std::vector<GuiWarpMarker>& markers_) {
    // Authored domain: positions are whole source frames (int64), written
    // as plain integer text (format_authored_frame).
    // tmp + fsync + rename, preserving the existing file's mode.
    return atomic_write_string_to_path(path, format_warpmarkers_text(markers_));
}

bool effective_disabled(const std::vector<GuiWarpMarker>& markers, int idx) {
    // Bounds-guarded GUI face of the ONE cascade definition,
    // marker_effectively_disabled (warp_frame_map_build.h) — the same
    // verdict the resolver's keep mask publishes, instantiated over
    // GuiWarpMarker so paint-loop callers pay no slice copy.
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return false;
    return marker_effectively_disabled(markers, static_cast<size_t>(idx));
}

namespace {

// The two passes behind resolved_magnification_level and
// build_waveform_gain_profile — the rules are stated once at their
// declarations (warpmarkers.h). `resolved[i]` is marker i's answer: its own
// value, else what an enabled marker displays / a disabled one would inherit.
struct MagnificationResolution {
    std::vector<char>    enabled;
    std::vector<uint8_t> resolved;
};

MagnificationResolution resolve_magnification(
        const std::vector<GuiWarpMarker>& markers) {
    const size_t n = markers.size();
    MagnificationResolution r;
    r.enabled.assign(n, 0);
    r.resolved.assign(n, 0);

    std::unordered_map<std::string, size_t> def_index;
    for (size_t i = 0; i < n; ++i) {
        r.enabled[i] = effective_disabled(markers, static_cast<int>(i)) ? 0 : 1;
        if (!markers[i].label_def.empty()) def_index[markers[i].label_def] = i;
    }

    // Pass A — the DEF values: a blank ref is transparent (rule 5).
    std::vector<uint8_t> value_a(n, 0);
    uint8_t cur = 0;
    for (size_t i = 0; i < n; ++i) {
        const GuiWarpMarker& m = markers[i];
        if (r.enabled[i]) {
            if (m.magnification.has_value()) cur = *m.magnification;
            // a blank ref and a blank non-ref both leave `cur` untouched here
        }
        value_a[i] = cur;
    }

    // Pass B — the displayed values: a blank ref takes its def's pass-A value
    // (rule 4), a dangling one carries (rule 6).
    cur = 0;
    for (size_t i = 0; i < n; ++i) {
        const GuiWarpMarker& m = markers[i];
        if (!r.enabled[i]) {
            r.resolved[i] = m.magnification.value_or(cur);
            continue;
        }
        if (m.magnification.has_value()) {
            cur = *m.magnification;
        } else if (!m.label_ref.empty()) {
            const auto it = def_index.find(m.label_ref);
            if (it != def_index.end()) cur = value_a[it->second];
        }
        r.resolved[i] = cur;
    }
    return r;
}

}  // namespace

int resolved_magnification_level(const std::vector<GuiWarpMarker>& markers,
                                 int idx) {
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return 0;
    return resolve_magnification(markers).resolved[static_cast<size_t>(idx)];
}

WaveformGainProfile build_waveform_gain_profile(
        const std::vector<GuiWarpMarker>& markers) {
    const MagnificationResolution r = resolve_magnification(markers);
    WaveformGainProfile p;
    auto& bp = p.breakpoints;
    for (size_t i = 0; i < markers.size(); ++i) {
        if (!r.enabled[i]) continue;
        const int64_t f     = markers[i].time_frame;
        const uint8_t level = r.resolved[i];
        // Rule 7: a later enabled marker at the same frame replaces the
        // earlier one's breakpoint; then drop the entry if it no longer
        // changes the level it follows.
        if (!bp.empty() && bp.back().source_frame == f) {
            bp.back().level = level;
            const uint8_t before =
                bp.size() >= 2 ? bp[bp.size() - 2].level : uint8_t{0};
            if (before == level) bp.pop_back();
            continue;
        }
        const uint8_t current = bp.empty() ? uint8_t{0} : bp.back().level;
        if (level != current) bp.push_back({f, level});
    }
    return p;
}

uint64_t waveform_gain_profile_hash(const WaveformGainProfile& profile) {
    if (profile.breakpoints.empty()) return 0;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const auto& b : profile.breakpoints) {
        h ^= static_cast<uint64_t>(b.source_frame);
        h *= 0x100000001b3ULL;
        h ^= static_cast<uint64_t>(b.level);
        h *= 0x100000001b3ULL;
    }
    return h;
}
