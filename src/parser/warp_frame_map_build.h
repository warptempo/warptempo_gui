#pragma once

#include "warpmarkers_parse.h"          // WarpMarker
#include "warp_frame_map.h"                  // WarpFrameMapSegment

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

// In-memory map build used by the engine. Math is organized as
// Pass 1 and Pass 2; the full untrimmed map is built unconditionally and trim
// is applied downstream, never here.

struct MidiTempoMapEntry {
    double target_time_sec;
    double multiplier;
};

// Minimal POD the warp_frame_map math needs. The GUI's `GuiWarpMarker` resolves into
// this: tempo_inherits markers are walked back to their nearest owning
// ancestor and their effective tempo_base / tempo_scale are copied forward.
// Disabled markers (and any references to disabled-defined labels) are
// filtered out BEFORE conversion.
struct MarkerForRender {
    // Authored position: a whole source frame in int64_t (see WarpMarker).
    // Consumers that divide or accumulate convert to double inside their
    // own arithmetic (exact far below 2^53).
    int64_t     time_frame = 0;
    double      tempo_base   = 1.0;   // resolved owning tempo; irrelevant for label_ref
    std::optional<double> tempo_scale; // nullopt or the typed scale after '*'
    std::string label_def;
    std::string label_ref;
};

// Effective tempo a marker resolves to, for display/authoring callers (not
// fed to the render path). See marker_effective() below.
struct MarkerEffective {
    double      base       = 0.0;   // 0.0 means "could not resolve"
    std::optional<double> scale;    // nullopt means no typed scale (treated
                                    // as 1.0); the label_ref branch always
                                    // carries the combined multiplier here,
                                    // unclamped — full double, no display
                                    // ceiling
    int         source_idx = -1;    // marker this value was taken from; -1
                                    // means no visible source (e.g. a
                                    // first-marker pass resolving to the 1.0
                                    // default)
};

// Returns the built warp frame map on success, or std::unexpected carrying
// the first violated condition (a concise lowercase reason; callers add their
// own context prefix). Does not log. Failure conditions, in check order:
// invalid source audio metadata (sample_rate <= 0 or total_frames <= 0),
// src_frame > total_frames, src_frame - prev_src_frame < 1, tempo <= 0
// (a zero or negative effective product divides by zero or flips sign in the
// segment arithmetic), and — in Pass 2, after all of the above — a label
// ref with no matching label def (the render-boundary refusal for a
// reference whose definition was deleted; such files load intact).
// Builds the full untrimmed map unconditionally; trim is applied downstream
// by the prepost trimmer (plan_trim translates this map into the cut's
// coordinates), never here. Scale participates here and not in
// build_phase_reset_source_frames because scale multiplies tempo, a
// target-duration quantity; phase reset positions are undisplaced source
// instants and have no target-duration component.
std::expected<std::vector<WarpFrameMapSegment>, std::string>
build_warp_frame_map(const std::vector<MarkerForRender>& markers,
                     double scale, long sample_rate, long total_frames);

// Derive the midi tempo map from a finished warp frame map. Each consecutive
// pair (a, b) with a positive target duration contributes an entry at
// a.tgt_frame / sample_rate whose multiplier is
// (b.src_frame - a.src_frame) / (b.tgt_frame - a.tgt_frame); after the walk a
// final entry at map.back().tgt_frame / sample_rate carries the last valid
// multiplier (1.0 when no pair produced one). The > 0 comparison is division
// safety only, not a size threshold — segment target durations have no floor —
// and the last valid multiplier carries across skips. The arithmetic reads the
// map's stored doubles exactly as written: entries are serialized at seventeen
// significant digits, so any reassociation would change deliverable bytes. An
// empty map returns the single entry {0.0, 1.0} (unreachable from program
// paths, since the build always emits the seed anchor; kept so the back()
// access is unconditionally safe).
std::vector<MidiTempoMapEntry> derive_midi_tempo_map(
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    long sample_rate);

// Per-index render-participation mask over parser-domain markers: keep[i]
// is true when marker i survives into the render, false when it is dropped.
// A marker is dropped when it is disabled, or when it is enabled, carries a
// label_ref, and the referenced label is defined by a disabled marker (the
// cascade: the definition supplies the duration the ref imposes, so a
// silenced definition leaves the ref with nothing to reproduce). Single
// owner of the participation verdict: resolve_warp_markers_for_render
// filters on it and the GUI's render-domain display sidecar writer gates
// its lockstep segment consumption on it, so display lockstep and the
// resolved render list can never disagree about which markers exist in a
// render. Warp-only as part of the resolver cascade — phase reset markers
// carry no labels or references; the reset sidecar writer computes its
// window participation inline against the crop bounds.
std::vector<bool> warp_markers_render_keep_mask(
    const std::vector<WarpMarker>& src);

// Render-boundary grammar check for the first warp marker, operating on the
// RAW marker list, pre-resolution (after resolution a leading pass is
// indistinguishable from a numeric owner because of resolve_inherited_tempo's
// 1.0 fallback). Rules: the list must be non-empty; markers[0] must sit at
// exactly frame 0, must not be disabled, must not be a pass
// (tempo_inherits), and must not be a label reference. A label definition on
// markers[0] is legal. This check is mandatory correctness, not politeness:
// build_warp_frame_map never reads markers[0].time_frame — it seeds the
// map at {0,0} and attributes the opening segment to the first resolved
// marker wherever it actually sits — so without it a moved, disabled, or
// pass/ref first marker renders SILENTLY WRONG deliverable bytes rather than
// failing. Violating files load intact (the save/reload round trip can never
// lock the user out); every render path refuses them here via
// resolve_warp_markers_for_render's leading call. Kept public so display-side
// validity gates can consult the verdict directly without building. On
// failure the one-line message names the failed rule and, when two or more
// markers share markers[0].time_frame exactly, appends a coincident-marker
// count with the formatted timestamp so the recovery path (Tab-select,
// delete one by one, recreate the first marker) is legible from the error
// alone.
std::expected<void, std::string>
validate_first_marker_render_grammar(const std::vector<WarpMarker>& markers,
                                     long sample_rate);

// Render-boundary check for one marker's pass-inheritance source, operating
// on the RAW marker list. Trivially passes unless markers[index] is an
// enabled pass (tempo_inherits, empty label_ref, own disabled flag false —
// a disabled pass is render-inert and never fires, the same participation
// rule as the first-marker grammar; the coincidence rule, by contrast,
// deliberately includes disabled markers). For an enabled pass, walk
// backward skipping every effectively-disabled marker (own flag, or an
// enabled ref whose def is disabled — the same cascade
// warp_markers_render_keep_mask publishes and marker_effective's source_idx
// walk uses); when the immediate prior surviving marker is an enabled label
// ref, refuse: the inherited value comes from the nearest true owner (refs
// are transparent to resolve_inherited_tempo) while the hover provenance
// names the ref — two disagreeing definitions — so the arrangement misleads
// and can never rest. A pass with no surviving prior at all passes here
// (that is the first-marker grammar's territory), and in a pass-pass-ref
// chain only the pass adjacent to the ref refuses. On failure the one-line
// message names both positions ("pass marker at <t> inherits from the label
// ref at <t>", display timestamps). resolve_warp_markers_for_render calls
// this when materializing each pass, and the defect enumerator mirrors it
// verbatim (MarkerDefectKind::PassAfterLabelRef) — the same owner/mirror
// sharing as validate_first_marker_render_grammar. Kept public for that
// mirror.
std::expected<void, std::string>
validate_pass_inheritance_source(const std::vector<WarpMarker>& markers,
                                 size_t index, long sample_rate);

// Resolve each WarpMarker to a MarkerForRender. Callers in the GUI slice
// their GuiWarpMarker store to std::vector<WarpMarker> first (the resolver
// is parser-domain and reads no GUI-only fields). Validates the first-marker
// render grammar first (validate_first_marker_render_grammar above, on the
// raw pre-resolution list) and returns its message unchanged on failure, so
// no render path can skip the check — this resolver is the chokepoint every
// caller passes through before build_warp_frame_map. Likewise refuses, when
// materializing each pass, a pass whose immediate prior surviving marker is
// an enabled label ref (validate_pass_inheritance_source above, message
// unchanged). Filters on
// warp_markers_render_keep_mask above — the participation verdict's single
// owner — dropping disabled label-definition markers (and thereby all refs
// to them) and disabled markers generally. The inherit walk-back is applied
// here so MarkerForRender carries a concrete tempo_base / tempo_scale —
// same rule as resolve_inherited_tempo. Both the engine-bound render
// pipeline and the target view's warp_frame_map recompute go through this single
// resolver so the visible deformity matches what the engine would emit.
std::expected<std::vector<MarkerForRender>, std::string>
resolve_warp_markers_for_render(const std::vector<WarpMarker>& src,
                                long sample_rate);

// Backward inheritance walk over parser-domain markers: from `index`, scan
// earlier markers for the nearest that OWNS its tempo — tempo_inherits ==
// false, not a label reference, and not disabled. Disabled markers are skipped
// because the engine drops them before resolution, so a disabled marker
// contributes no tempo downstream. Returns 1.0 (tempo) / nullopt (scale) if
// none is found. This is the single canonical inheritance walk: resolve_warp_markers_for_render
// and compute_hover_popup_text both call it, so the popup display always
// matches the tempo the engine resolves.
double resolve_inherited_tempo(const std::vector<WarpMarker>& markers, int index);
std::optional<double> resolve_inherited_tempo_scale(
    const std::vector<WarpMarker>& markers, int index);

// Effective (base, scale, source) a marker resolves to, for display/authoring
// callers in hover/popup and marker operation paths. base
// == 0.0 means "could not resolve" (mirrors compute_hover_popup_text's ""
// guards). scale == nullopt means no typed scale (treated as 1.0 by callers).
// source_idx names the marker the value is visibly taken from:
//   owner     -> idx itself (its own tempo_base / tempo_scale).
//   pass      -> the nearest non-disabled marker strictly before idx (the
//                immediate prior marker the value is inherited from — NOT
//                necessarily the owning marker if there's a chain of passes).
//                base/scale are still the fully-resolved owner values via
//                resolve_inherited_tempo(_scale).
//   label_ref -> the label-definition marker (def_idx); base/scale are the
//                def's effective base and the combined "~=" multiplier —
//                a full double with no display ceiling.
// A pass whose immediate prior surviving marker is an enabled label ref is
// observable only transiently (at rest the arrangement is a walked commit
// defect, refused at the render boundary by
// validate_pass_inheritance_source); on such a transiently-unresolved store
// source_idx names the ref (the visible immediate prior), base/scale still
// the resolved owner's.
MarkerEffective marker_effective(const std::vector<WarpMarker>& mv, int idx);

// Hover-popup text for a warp marker (the label-ref / pass tempo notice). Pure
// parser-domain string/math — computes the same resolution the engine uses
// when emitting the warpframemap, so the popup matches what will be
// rendered. Pass
// markers emit "= TEMPO (from SOURCE @ TIME)" or "= TEMPO*SCALE (from SOURCE @
// TIME)" (resolved tempo of the nearest prior owning marker; SOURCE is the
// immediate prior marker's own resolved displayed tempo — matching what that
// marker's own popup or flag shows, not its raw stored fields, which are
// inert for a pass or a label_ref — TIME its time_frame). If SOURCE's own
// resolution is unresolvable (base 0.0), the suffix is dropped entirely and
// the popup shows just the resolved tempo. Label_ref markers emit
// "~= BASE*COMBINED_SCALE (from DEF_BASE:LABEL @ TIME)" (BASE printed as a
// tempo-like value, min 2 decimals; COMBINED_SCALE = def_scale * multiplier
// when the def has a typed scale, else multiplier, printed as a scale-like
// value at min 4 decimals with no ceiling — the "~=" marks an implied,
// geometry-dependent value, while a pass popup's "=" marks an exact
// inherited literal; DEF_BASE:LABEL and TIME describe the label-definition
// marker). TIME is formatted with format_timestamp
// (time_format.h), the same mm:ss.mmm formatter the rest of the GUI uses.
// Both qualifying cases end the readout with " (ctrl+c to copy)", the hint
// for the hover-copy binding.
// Returns "" when the marker does not qualify (owning, missing def,
// malformed). GUI callers slice their GuiWarpMarker store to WarpMarker
// (slice_to_warp_markers) before calling.
//
// When `copy_payload_out` is non-null and the marker qualifies, it receives
// the pasteable effective value in the exact text the flag editor accepts and
// the serializer writes: format_value_double(base, 2), plus
// "*"+format_value_double(scale, 4) when a scale is present (omitted for a
// pass whose scale is semantically 1, always included for a label ref). No
// "= "/"~= " prefix, no provenance, no copy hint — just the value. It is the
// popup text's own value substring, so the two cannot drift. Left untouched
// when the marker does not qualify (the function returns "" first).
std::string compute_hover_popup_text(
    const std::vector<WarpMarker>& mv, int idx, int sample_rate,
    std::string* copy_payload_out = nullptr);

// The map artifacts are always the FULL maps — trim is a wav-only render
// window, never an artifact shape (the map formats refuse when a trim bound
// is set), so no windowed derivation exists here. Artifact convention: the
// target column is deliverable-relative (first pair's target exactly zero)
// and the source column is absolute undisplaced source frames, matching the
// project-wide convention shared by marker files and render-view sidecars.
