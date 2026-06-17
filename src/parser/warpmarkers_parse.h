#pragma once

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

// One parse diagnostic. line_number is 1-based; 0 means "file-level, no line".
struct WarpMarkerParseError {
    int         line_number;
    std::string message;
};

// Result of parse_warpmarkers_file. On failure (ok == false) markers is
// empty and errors lists every violation in encounter order; on success
// markers holds the parsed bases and errors is empty. had_nonstandard_content
// is true when the file carried content the canonical save would discard
// (comments, blank/indented lines, freeform trailing text, ditto tempos).
struct WarpMarkersParse {
    std::vector<WarpMarker>           markers;
    std::vector<WarpMarkerParseError> errors;
    bool                              had_nonstandard_content = false;
    bool                              ok                      = false;
};

// Parse a .warpmarkers file (legacy or new format). Never throws; a missing
// or unopenable file is reported as a file-level error with ok == false. This
// is the canonical .warpmarkers reader for both the GUI store and the headless
// CLI.
WarpMarkersParse parse_warpmarkers_file(const std::string& path);

namespace warpmarkers_internal {

// Parse one canonical new-format line into the WarpMarker base. Used by the
// GUI editor's commit path (flag_editor), which passes a GuiWarpMarker by
// upcast. Line-local validation only — cross-marker rules (label_ref
// existence, label_def uniqueness, time monotonicity) are the caller's. On
// `pass`, tempo_base/tempo_scale are populated with inert defaults.
bool parse_single_canonical_line(
    const std::string& raw_line,
    WarpMarker& out,
    std::string* error_out);

// Canonicalize a scale string to the one-digit-dot-four-decimals form.
// Owned here because both the parser and the GUI writer must agree on the
// exact canonical form; exposed so warpmarkers.cpp's save() reuses it.
std::string normalize_scale_string(const std::string& s);

} // namespace warpmarkers_internal
