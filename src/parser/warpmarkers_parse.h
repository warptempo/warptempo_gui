#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

// One warp marker's serialized form — the eight fields the .warpmarkers
// file round-trips (the eighth, the tempo deviation chain, is SPELLING that
// the file round-trips and the render never sees — architect approval
// 2026-09-18), and the only fields the parser domain and the
// engine-bound render path read. Three independent state axes (a per-marker
// waveform gain left the marker whole — architect approval 2026-09-15 — and
// the measure reference, the ` //<measure>` comment past the canonical line,
// left it whole too — architect approval 2026-09-16; the departure record is
// at parse_single_canonical_line):
//
//   1. Tempo source. `tempo_inherits == false`: this marker owns its tempo
//      (`tempo_cents` is the numeric value — THE RESOLVED TOTAL, see the
//      deviation chain below). `tempo_inherits == true` (a
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
    // THE TEMPO DEVIATION CHAIN — the payload's SPELLING of the tempo above
    // and nothing else (architect approval 2026-09-18). A numeric payload may
    // carry up to kMaxTempoDeviationTerms signed terms after its base,
    // `1.23+0.01-0.02`, each a mandatory sign and the N.NN form within
    // ±kIterDeltaMaxCents (format_deviation_cents / parse_deviation_cents,
    // value_format.h): the section's MAIN SPEED stays legible beside each
    // bit's departure from it, and the chain reads as the history of the
    // decisions that arrived at the number.
    //
    // tempo_cents IS THE RESOLVED TOTAL, base plus every term, and THE BASE IS
    // DERIVED at format time as `tempo_cents - sum(terms)`. That inversion is
    // the whole design: `1.23+0.01` and `1.24` are ONE number to every reader
    // of tempo_cents — the resolver, the frame map, a label def's duration,
    // the BPM sweep, every wall — so a respelling cannot change what is heard.
    // THE FIELD IS INVISIBLE TO THE RENDER: no engine input carries it (the
    // pipeline converts to MarkerForRender, warp_frame_map_build.h, which has
    // no such field), so it reaches neither the map nor the render
    // fingerprint. What it does reach is the FILE, the FLAG, the flag editor
    // and every sidecar a render entry carries, and a load-then-save
    // round-trips it byte for byte.
    //
    // EMPTY IS THE PLAIN SPELLING, and a pass and a label ref are empty BY
    // GRAMMAR (architect 2026-09-19: "a pass is simply a pass") — `pass+0.01`
    // and `a.aa+0.01` are adversarial and load-fatal — so on those two forms
    // the vector is inert and never read, exactly as tempo_cents is.
    std::vector<int64_t> tempo_deviation_cents;
    // nullopt: no typed scale (the serializer omits "*scale"; semantically
    // scale 1). A present value is the authored scale, a full double —
    // a recorded asymmetry: tempo is integer cents, scale is deliberately
    // full-double by standing ruling.
    std::optional<double> tempo_scale;

    std::string label_def;
    std::string label_ref;

    bool disabled      = false;
};

// Parse a .warpmarkers file in the canonical GUI-authored format. Never
// throws. Returns the parsed markers on success; on the first malformed
// line, or an unopenable file, returns a one-line diagnostic (line-tagged
// where line-specific). An empty file parses to an empty vector, and a
// missing frame-0 tempo owner is NOT a load rule — the render resolver
// (resolve_warp_markers_for_render) normalizes it, silently seeding a plain
// enabled 1.00 owner at frame 0, so any state the GUI can save loads back and
// renders. A line is the canonical line WHOLE — no comment, no suffix of any
// kind (architect approval 2026-09-16; the record is at
// parse_single_canonical_line) — so any byte past it, a ` //` included, is
// GUI-unproducible and load-fatal like any other adversarial line.
// This is the canonical .warpmarkers reader for both the GUI store and the
// headless CLI.
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
// THE LINE IS THE CANONICAL LINE WHOLE. Every caller — the whole-file loop,
// the revert's warp reconstitution (input_key_dispatch.cpp), the history
// delta's extract_warp_entry and warp_side_effective_disabled
// (history_diff.cpp), and the flag editor's candidate parse — reads the same
// grammar, and the no-whitespace refusal is what rejects any suffix: a ` //`
// on a sidecar line is adversarial and load-fatal, and one typed into the
// payload field is a grammar error that red-flashes at the commit.
std::expected<WarpMarker, std::string> parse_single_canonical_line(
    const std::string& raw_line);

} // namespace warpmarkers_internal
