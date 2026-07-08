#pragma once

#include <expected>
#include <string>
#include <vector>

// One warp marker's serialized form — the seven fields the .warpmarkers
// file round-trips, and the only fields the parser domain and the
// engine-bound render path read. Three independent state axes:
//
//   1. Tempo source. `tempo_inherits == false`: this marker owns its tempo
//      (`tempo_base` is the numeric value). `tempo_inherits == true` (a
//      "pass" marker): the presentation tempo is resolved live by walking
//      backward through the marker list to the nearest owning marker.
//      `tempo_base`/`tempo_scale` carry inert defaults (1.0 / "1.0000")
//      that are never read while the marker is inheriting.
//
//   2. Label relationship. At most one of `label_def` and `label_ref` is
//      non-empty. `label_def` marks a label origin; `label_ref` cites one.
//
//   3. Disabled flag. Allowed on any marker. A disabled marker has its
//      tempo contribution silenced. When the disabled marker is a
//      `label_def`, all `label_ref` markers pointing to it are also
//      treated as disabled (cascade). The cascade rule applies only to
//      label_def markers; a disabled non-label-def is locally disabled
//      and does not propagate.
struct WarpMarker {
    double time_seconds = 0.0;

    bool        tempo_inherits = false;
    double      tempo_base     = 1.0;
    std::string tempo_scale;

    std::string label_def;
    std::string label_ref;

    bool disabled      = false;
};

// Parse a .warpmarkers file in the canonical GUI-authored format. Never
// throws. Returns the parsed markers on success; on the first malformed
// line, or an unopenable file, returns a one-line diagnostic (line-tagged
// where line-specific). This is the canonical .warpmarkers reader for both
// the GUI store and the headless CLI.
std::expected<std::vector<WarpMarker>, std::string>
parse_warpmarkers_file(const std::string& path);

namespace warpmarkers_internal {

// Parse one canonical new-format line into a WarpMarker. Used by the GUI
// editor's commit path (flag_editor). Line-local validation only —
// cross-marker rules (label_def uniqueness, time ordering: non-decreasing
// at load, degeneracy refused at the render boundary) are the caller's.
// On `pass`, tempo_base/tempo_scale are
// populated with inert defaults. Returns the marker on success, or a
// one-line diagnostic on failure.
std::expected<WarpMarker, std::string> parse_single_canonical_line(
    const std::string& raw_line);

} // namespace warpmarkers_internal
