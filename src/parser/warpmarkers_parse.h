#pragma once

#include "marker_measure.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

// One warp marker's serialized form — the eight fields the .warpmarkers
// file round-trips, and the only fields the parser domain and the
// engine-bound render path read. Three independent state axes (the eighth
// field, the measure, is a score reference and not a state axis):
//
//   1. Tempo source. `tempo_inherits == false`: this marker owns its tempo
//      (`tempo_cents` is the numeric value). `tempo_inherits == true` (a
//      "pass" marker): the presentation tempo is resolved live by walking
//      backward through the marker list to the nearest owning marker.
//      `tempo_cents`/`tempo_scale` carry inert defaults (100 / nullopt)
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
    // Authored position: a whole source frame held in an int64_t — a
    // fractional authored position is unrepresentable (fractional position
    // text is load-fatal, and every gesture commit converts through
    // snap_authored_frame). Serialized as plain integer text via
    // frame_format.h; timestamps are display-only renderings.
    int64_t time_frame = 0;

    bool        tempo_inherits = false;
    // Authored tempo: 100-based integer cents held in an int64_t — an
    // off-grid or fractional tempo is unrepresentable by type, the exact
    // value-domain sibling of the int64 frame position above. The N.NN
    // spelling is the text interface only (format_tempo_cents /
    // parse_tempo_cents, value_format.h); a double tempo exists only past
    // tempo_from_cents at the DSP boundary. 100 is the 1.00 default.
    int64_t     tempo_cents    = 100;
    // nullopt: no typed scale (the serializer omits "*scale"; semantically
    // scale 1). A present value is the authored scale, a full double —
    // a recorded asymmetry: tempo is integer cents, scale is deliberately
    // full-double by standing ruling.
    std::optional<double> tempo_scale;

    std::string label_def;
    std::string label_ref;

    bool disabled      = false;

    // MEASURE REFERENCE (architect approval 2026-08-20), serialized as the
    // ` //<measure>` suffix past the canonical line — grammar, canonical
    // spelling and byte bound in marker_measure.h. Empty means no measure; the
    // writer emits no suffix for it and the bare suffix is load-fatal.
    //
    // IT IS HOMED ON THE BASE rather than on the GUI's derived marker because
    // the field must round-trip through the file, and both readers are shared:
    // the CLI parses it (and never re-serializes anything), while the GUI store
    // upcasts this base whole. It reaches no further: MarkerForRender never
    // carries a measure, and the render fingerprint serializes resolved fields
    // only — so a measure cannot move a render key, and editing one can never
    // invalidate a completed render.
    std::string measure;
};

// Parse a .warpmarkers file in the canonical GUI-authored format. Never
// throws. Returns the parsed markers on success; on the first malformed
// line, or an unopenable file, returns a one-line diagnostic (line-tagged
// where line-specific). An empty file parses to an empty vector, and a
// missing frame-0 tempo owner is NOT a load rule — the render resolver
// (resolve_warp_markers_for_render) normalizes it, silently seeding a plain
// enabled 1.00 owner at frame 0, so any state the GUI can save loads back and
// renders. Every line may carry the ` //<measure>` suffix
// (marker_measure.h); a malformed one — off the measure grammar, past the byte
// bound, or the bare separator with nothing after it — is GUI-unproducible and
// load-fatal like any other adversarial line. This is the
// canonical .warpmarkers reader for both the GUI store and the headless CLI.
//
// `path_free_reason`, when given, receives THE PATH-BEARING REFUSALS' WORDS
// WITH NO PATH IN THEM — "cannot open file", "read error in file" — while the
// returned string stays the composed sentence it always was (architect
// approval 2026-09-02, the granted frozen touch; the rationale is at the
// composing lambda). It is written by those two arms alone, so its presence
// tells a caller that this refusal named the file it handed in and lets that
// caller name the file ONCE, its own way, on a card; a line-numbered parse
// error leaves it untouched. The CLI passes nothing and its stderr line is
// byte-identical.
std::expected<std::vector<WarpMarker>, std::string>
parse_warpmarkers_file(const std::string& path,
                       std::optional<std::string>* path_free_reason = nullptr);

namespace warpmarkers_internal {

// Parse one canonical new-format line into a WarpMarker. Used by the GUI
// editor's commit path (flag_editor). Line-local validation only —
// cross-marker rules (label_def uniqueness, time ordering: non-decreasing
// at load, exact-frame degeneracy collapsed to one 1.00 owner at the
// render boundary) are the caller's.
// On `pass`, tempo_cents/tempo_scale are
// populated with inert defaults (100 / nullopt). Returns the marker on
// success, or a one-line diagnostic on failure.
//
// `accept_measure` selects whether the line may carry the ` //<measure>`
// suffix (marker_measure.h). When true the suffix is split off first, its
// grammar validated, and the measure attached; when false the suffix is
// not a concept and the no-whitespace refusal below rejects the line whole.
// The four callers and their answers:
//
//   - the whole-file loop below (TRUE) — the on-disk grammar carries measures.
//   - the revert's warp reconstitution (bare `v` since 2026-09-01, `Ctrl+H`
//     before it; TRUE, input_key_dispatch.cpp) — the delta token carries the
//     measure, so the rebuilt line does too.
//     (Comment-only touch, architect grant 2026-08-29: the chord was renamed
//     and this sentence named the old one.)
//   - extract_warp_entry (TRUE, history_diff.cpp) — the history delta reads
//     the same on-disk lines; false there would refuse every measured marker
//     on the whitespace loop and vanish it from the diff lane.
//   - the flag editor's candidate parse (FALSE, the default) — the payload
//     buffer never holds a measure, so a ` //` typed into it is a grammar
//     error that red-flashes at commit. The measure has its own editor.
std::expected<WarpMarker, std::string> parse_single_canonical_line(
    const std::string& raw_line, bool accept_measure = false);

} // namespace warpmarkers_internal
