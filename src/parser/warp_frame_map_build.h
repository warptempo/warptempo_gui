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

// Minimal POD the warp_frame_map math needs. The GUI's `GuiWarpMarker` resolves into
// this: tempo_inherits markers are walked back to their nearest owning
// ancestor and their effective tempo_cents / tempo_scale are copied forward.
// Disabled markers (and any references to disabled-defined labels) are
// filtered out BEFORE conversion.
struct MarkerForRender {
    // Authored position: a whole source frame in int64_t (see WarpMarker).
    // Consumers that divide or accumulate convert to double inside their
    // own arithmetic (exact far below 2^53).
    int64_t     time_frame = 0;
    // Resolved owning tempo in integer cents (see WarpMarker); irrelevant
    // for label_ref (0). Becomes a double only inside build_warp_frame_map's
    // slope product, via tempo_from_cents.
    int64_t     tempo_cents  = 100;
    std::optional<double> tempo_scale; // nullopt or the typed scale after '*'
    std::string label_def;
    std::string label_ref;
};

// Effective tempo a marker resolves to, for display/authoring callers (not
// fed to the render path). See marker_effective() below.
struct MarkerEffective {
    // Why a label ref resolved to the normalized 1.00 fallback (source_idx
    // -1): the popup picks its parenthetical from this instead of
    // re-searching the raw store, which would name the wrong case for a
    // def that died in a coincidence collapse (present raw, dangling in
    // the projection). None for every non-fallback result and for pass
    // fallbacks.
    enum class NormalizedReason { None, UndefinedLabel, ExtremeRatio };

    int64_t     base_cents = 0;     // integer tempo cents; 0 means "could
                                    // not resolve"
    std::optional<double> scale;    // nullopt means no typed scale (treated
                                    // as 1.0); the label_ref branch always
                                    // carries the combined multiplier here,
                                    // unclamped — full double, no display
                                    // ceiling
    int         source_idx = -1;    // marker this value was taken from; -1
                                    // means no visible source (e.g. a
                                    // first-marker pass resolving to the
                                    // 1.00 default, or a value taken from
                                    // a synthetic projection marker — a
                                    // collapsed group's replacement owner
                                    // or the frame-0 seed — that no raw
                                    // index can honestly name)
    NormalizedReason reason = NormalizedReason::None;
    // True only when a PASS's inheritance walk terminated on a surviving
    // enabled label ref (the render's 1.00 fallback) — distinct from a pass
    // that inherits a real value from a synthetic prior (the frame-0 seed or a
    // collapsed-group owner), which also carries source_idx -1 but is NOT a
    // normalization fallback.
    bool from_ref = false;
    // (No owner_idx. It named the RAW STORE INDEX of the terminal OWNER the
    // ref-opaque backward walk landed on, and its sole consumer was the tempo
    // drag's forward-label coupling guard; that gesture — the pointer tempo
    // drag and the Left/Right tempo-image step — was deleted 2026-07-29, which
    // left the field written by three sites here and read by
    // nobody, so it is DELETED — architect 2026-07-29, an explicit surgical freeze
    // approval. `from_ref` beside it STAYS: the GUI's normalization-red set reads it
    // (warp_frame_map_view.cpp). resolve_inherited_tempo's matching `owner_index`
    // out-parameter, which outlived the field as an unused capability, is deleted
    // too — architect 2026-07-30, a second explicit surgical approval covering
    // exactly that surface.)
};

// Returns the built warp frame map on success, or std::unexpected carrying
// the first violated condition (a concise sentence-capitalized reason —
// these six strings are GUI-PAINTED through the target-view entry gate's
// error notice, the rationale at the definition; callers add their own
// context prefix). Does not log. Failure conditions, in check order:
// invalid source audio metadata (sample_rate <= 0 or total_frames <= 0),
// src_frame > total_frames (the past-EOF wall — column-symmetric with
// build_phase_reset_source_frames by ruling, and a loud breach backstop for
// hand-assembled input), src_frame - prev_src_frame < 1 (unreachable from
// the resolver, whose coincidence collapse guarantees >= 1-frame spacing;
// kept because the engine validates strict ascent but NOT the >= 1-frame
// gap, so a sub-frame segment is the one map defect that would render
// silently wrong bytes), tempo <= 0 (reachable — the ruled async-stderr
// backstop for the sweep batches' per-cell computed tempo mutations, kept for
// its message vocabulary: without it a NEGATIVE tempo degrades to a decreasing
// target the engine refuses as "not strictly ascending" and a ZERO tempo to a
// non-finite target the emission finiteness arm below refuses first, and
// neither downstream refusal could NAME the tempo), a label ref with no
// matching label def (Pass 2; unreachable from the resolver, which normalizes
// dangling refs first; kept because the engine never consumes refs, so a
// breach would be a raw failed map-lookup rather than a loud refusal — the
// guard buys deterministic loudness), and — last, at the Pass 2 emission
// chokepoint — a non-finite target anchor (the builder's own emission
// contract: both orchestrators llrint the final target anchor and the trimmer
// llrints interior evaluations BEFORE the engine validates, and llrint on a
// non-finite value is an invalid conversion with an unspecified result, so
// only the producer can refuse a non-finite target cleanly; strict ascent
// stays engine-owned, finite values converting through llrint cleanly there).
// Builds the full untrimmed map unconditionally; trim is applied downstream
// by the prepost trimmer (plan_trim translates this map into the cut's
// coordinates), never here. Scale participates here and not in
// build_phase_reset_source_frames because scale multiplies tempo, a
// target-duration quantity; phase reset positions are undisplaced source
// instants and have no target-duration component.
std::expected<std::vector<WarpFrameMapSegment>, std::string>
build_warp_frame_map(const std::vector<MarkerForRender>& markers,
                     double scale, long sample_rate, long total_frames);

// Single source of truth for "does this raw marker survive into the
// render list". A marker is silenced either by its own disabled flag or,
// for an enabled label ref, by the referenced label being defined by a
// disabled marker — the cascade, because the definition supplies the
// duration the ref imposes, so a silenced definition leaves the ref with
// nothing to reproduce. Templated over the marker shape (disabled,
// label_ref, label_def) so the parser's WarpMarker walks and the GUI's
// GuiWarpMarker surfaces (paint, selection, hover, nudge) consume the ONE
// cascade definition without a per-call slice copy — the GUI callers sit
// inside paint loops. Duplicate label definitions are load-fatal, so "any
// disabled marker defining the label" and "the defining marker's flag"
// are the same verdict.
template <typename MarkerT>
bool marker_effectively_disabled(const std::vector<MarkerT>& mv, size_t idx) {
    const MarkerT& g = mv[idx];
    if (g.disabled) return true;
    if (!g.label_ref.empty()) {
        for (const auto& d : mv) {
            if (d.disabled && !d.label_def.empty() &&
                d.label_def == g.label_ref) {
                return true;
            }
        }
    }
    return false;
}

// Per-index render-participation mask over parser-domain markers: keep[i]
// is true when marker i survives into the render, false when it is dropped
// (the marker_effectively_disabled verdict above, per index). Single
// owner of the participation verdict: resolve_warp_markers_for_render's
// filter, which every render path funnels through. Warp-only as part of
// the resolver cascade — phase reset markers carry no labels or
// references.
std::vector<bool> warp_markers_render_keep_mask(
    const std::vector<WarpMarker>& src);

// The stage-2 coincident-collapse rule as a pure classifier over the RAW
// authored store (the one owner; architect approval 2026-07-30, the site
// record added 2026-08-29 under that day's approval): out[i] is true iff raw
// index i
// belongs to an exact-frame run whose effectively-enabled member count is >= 2
// — the run that stage 2 collapses to one synthetic 1.00 owner. Positions are
// whole int64 frames (exact equality IS the coincidence predicate) and the
// store is time-sorted, so a run is adjacent. Membership is the whole run,
// disabled members included (the GUI reddens the entire overlapping stack).
// Consumed internally by stage 2 (via its raw-index map) and externally by the
// GUI's normalization-red set, so the render's collapse verdict and the GUI's
// red cue cannot drift.
std::vector<char> warp_coincident_collapse_members(
    const std::vector<WarpMarker>& markers);

// Resolve each WarpMarker to a MarkerForRender — the normalization
// chokepoint every render path passes through before build_warp_frame_map
// (both the engine-bound render pipeline and the target view's
// warp_frame_map recompute, so the visible deformity matches what the
// engine would emit). Callers in the GUI slice their GuiWarpMarker store to
// std::vector<WarpMarker> first (the resolver is parser-domain and reads no
// GUI-only fields).
//
// Every marker arrangement resolves: an ambiguous tempo is never refused,
// it normalizes to 1.00 with one stderr line per affected timestamp per
// resolve (no dedup across resolves — a resting ambiguous state re-prints
// on every recompute, the intended signal). The staged pipeline:
//   1. filter to the survivors of warp_markers_render_keep_mask above —
//      the participation verdict's single owner — dropping disabled
//      markers and cascade-disabled refs;
//   2. collapse each group of 2+ survivors sharing one exact time_frame
//      (positions are whole int64 frames, so exact equality is the whole
//      coincidence predicate) into ONE plain enabled 1.00 owner with no
//      labels, one line per group;
//   3. when no survivor sits at exactly frame 0, silently prepend a plain
//      enabled 1.00 owner there (the documented default, not ambiguity);
//   4. materialize each pass through resolve_inherited_tempo(_scale)'s
//      ref-opaque walk below — a walk terminated by a surviving enabled
//      label ref yields the 1.00 fallback, with a line;
//   5. normalize label refs: a dangling ref (no def among the survivors)
//      or a ref whose implied effective tempo lies outside the authored
//      per-marker envelope [0.125, 8.0] becomes a plain 1.00 owner at its
//      own frame, each with a line;
//   6. emit MarkerForRender.
// Output invariants: non-empty; strictly increasing whole frames; the
// first element sits at exactly frame 0 and is enabled (a plain 1.00 owner
// when seeded); every surviving label_ref is resolvable and its implied
// tempo in-band. Input that normalizes to itself — no coincident
// survivors, a survivor at frame 0, no ref-terminated walks, every ref
// resolvable and in-band — yields the identical resolved list the
// pre-normalization filter produced, so build_warp_frame_map's output is
// byte-for-byte unchanged for such input. Returns a PLAIN std::vector: this
// is a TOTAL NORMALIZER, infallible by design — every ambiguous state
// resolves and the stderr line is the only signal, so there is no error to
// report. build_warp_frame_map keeps its std::expected because it is a
// different KIND of function, a partial compiler whose error arm has real
// producers (the sweep's unbracketed per-cell tempo can drive a non-positive
// product) plus the kept breach guards; the two signatures rightly differ.
// sample_rate feeds only the stderr timestamps; total_frames only the
// envelope check's last-segment distance.
// Stages 1-3 live in one shared projection function
// (normalized_surviving_markers, internal to warp_frame_map_build.cpp):
// the resolver runs it with the stderr lines on, and the display side
// (marker_effective / resolved_marker_payload) runs the SAME function
// silently to resolve surviving passes and refs against what actually
// renders — one implementation, so the two surfaces cannot drift. The
// stage-5 band check likewise classifies through the same
// label_ref_implied_effective_tempo helper the display's band verdict
// uses, in one fixed operation order, because the envelope edges are
// inclusive and IEEE-reassociated equivalents can disagree exactly there.
// EVERY RESOLVE IS LOUD: one normalization stderr line per affected timestamp, on
// every resolve, unconditionally. The `quiet` parameter that could suppress them —
// for HYPOTHETICAL evaluations, states never committed — is DELETED (architect
// 2026-07-29, an explicit surgical freeze approval): its only caller was the group
// tempo drag's monotone bisection, which resolved ~12 never-live candidate stores
// per motion event, and that whole gesture is gone (deleted 2026-07-29 with the
// rest of the tempo-image family — the pointer tempo drag and the Left/Right
// tempo-image step). No hypothetical resolve exists in the product any more, so
// no caller wants silence.
// FIVE CALL SITES, re-derived by grep 2026-07-29 (the deletion round's own prose had
// said four; architect approval 2026-07-29, the site record added 2026-08-29 under
// that day's approval): the CLI's render path, the GUI render pipeline, the target-render
// fingerprint, the target-view entry VALIDATION, and the target-view map shim. All
// five had always passed the loud default, so loud-always is equivalence, not a
// behavior change.
std::vector<MarkerForRender>
resolve_warp_markers_for_render(const std::vector<WarpMarker>& src,
                                long sample_rate, long total_frames);

// Backward inheritance walk over parser-domain markers: from `index`, scan
// earlier markers for the nearest that OWNS its tempo — tempo_inherits ==
// false, not a label reference, and not disabled. Disabled markers and
// cascade-disabled refs are skipped because the engine drops them before
// resolution, so they contribute no tempo downstream. A SURVIVING enabled
// label ref (non-empty label_ref, not marker_effectively_disabled)
// terminates the walk with the fallback and, when `inherited_from_ref` is
// non-null, sets it true — the resolver prints its normalization line from
// that signal; display callers pass nothing. Returns 100 cents (tempo
// 1.00) / nullopt (scale) when no owner is reached. Inheritance is a pure
// value copy — cents copy exactly. This is the single canonical
// inheritance walk: resolve_warp_markers_for_render and the display surfaces
// (marker_effective / resolved_marker_payload) both call it, so a copied
// or jumped-to value always matches the tempo the engine resolves.
//
// (No `owner_index` out-parameter. It reported the index of the owner the walk
// terminated on, for MarkerEffective::owner_idx — deleted 2026-07-29 with the
// tempo drag's forward-label coupling guard — after which no caller passed it and
// no reader existed anywhere in the tree. It is DELETED: architect 2026-07-30, an
// explicit surgical freeze approval finishing the one the field's own deletion was
// scoped short of. `inherited_from_ref` beside it STAYS: the resolver prints its
// normalization line from that signal, and the GUI's normalization-red set reads
// it; display callers pass nothing.)
int64_t resolve_inherited_tempo(const std::vector<WarpMarker>& markers, int index,
                                bool* inherited_from_ref = nullptr);
std::optional<double> resolve_inherited_tempo_scale(
    const std::vector<WarpMarker>& markers, int index,
    bool* inherited_from_ref = nullptr);

// Effective (base_cents, scale, source, reason) a marker resolves to, for
// display/authoring callers in hover/popup and marker operation paths.
// base_cents == 0 means "could not resolve" (mirrors
// resolved_marker_payload's "" guards). scale == nullopt means no typed
// scale (treated as 1.0 by callers). total_frames is the source length in
// frames; it bounds the last projection segment (the resolver's own rule),
// so a last-segment ref resolves and classifies exactly as it renders.
//
// Resolution basis, in order:
//   - an OWNER resolves to its own fields — coincident group members
//     included, the accepted authored-display split (the render collapses
//     a 2+-survivor exact-frame group to one plain 1.00 owner, but the
//     members themselves keep their authored readouts by ruling);
//   - an effectively-disabled marker, or a pass/ref that is itself a
//     member of a 2+-survivor exact-frame group, resolves against the RAW
//     store (disabled markers do not render at all; group members are the
//     ruled split above);
//   - every other pass/ref — surviving and un-collapsed — resolves against
//     the silent projection (the render resolver's stages 1-3, no stderr),
//     so its value is what actually renders even when other markers were
//     collapsed away or the frame-0 seed was inserted, and the band
//     verdict comes from the shared resolver-order classification helper.
// source_idx names the marker the value is visibly taken from:
//   owner     -> idx itself (its own tempo_cents / tempo_scale).
//   pass      -> the immediate prior surviving marker the value is
//                inherited from (NOT necessarily the owning marker if
//                there's a chain of passes); on the projection path this
//                is the prior projection entry mapped back to its raw
//                index, and a SYNTHETIC prior — a collapsed group's
//                replacement 1.00 owner or the frame-0 seed — reports -1
//                (attributing it to any single raw marker would mislead;
//                the popup then shows the bare resolved value).
//                base/scale are still the fully-resolved owner values via
//                resolve_inherited_tempo(_scale).
//   label_ref -> the label-definition marker's raw index (a projection
//                def is never synthetic — synthetic owners carry no
//                labels); base/scale are the def's effective base and the
//                combined "~=" multiplier — a full double with no display
//                ceiling.
// Normalization fallbacks mirror the render resolver and report as
// {base_cents 100, scale nullopt, source_idx -1} — no visible source,
// because the value is a fallback, not inherited from anywhere:
//   - a pass whose inheritance walk terminated on a surviving enabled
//     label ref (attributing the 1.00 to the ref would mislead);
//   - a label ref with no definition on its resolution basis (projection
//     path: a def that died in a coincidence collapse is dangling, the
//     render's own verdict) — reason UndefinedLabel;
//   - a label ref whose implied effective tempo lies outside the authored
//     per-marker envelope [0.125, 8.0] (the shared classification helper,
//     resolver operation order) — reason ExtremeRatio.
MarkerEffective marker_effective(const std::vector<WarpMarker>& mv, int idx,
                                 long total_frames);

// THE RESOLVED VALUE OF A PASS OR LABEL_REF WARP MARKER, as the pasteable
// payload plus the index of the marker that value came FROM. (ARCHITECT
// APPROVAL 2026-08-29 for the touch on this frozen unit.) Pure parser-domain
// string/math — it resolves through marker_effective above (the silent
// projection for surviving un-collapsed passes/refs, the raw store for the
// carve-outs), so what it composes is what will be rendered.
//
// THE PAYLOAD is the value form the flag editor accepts and the serializer
// writes: format_tempo_cents(base_cents), plus "*" + format_value_double(
// scale, 4) when a scale is present — omitted for a pass whose scale is
// semantically 1, always included for a label ref, whose implied multiplier
// always exists. No prefix, no provenance, no timestamp: just the value.
//
// `source_index_out`, when non-null, receives THE MARKER THE VALUE IS TAKEN
// FROM, and only where such a marker exists:
//   pass      -> the immediate prior surviving marker it inherits from (NOT
//                necessarily the owning marker if there is a chain of
//                passes);
//   label_ref -> the label-definition marker.
// It is LEFT UNTOUCHED in every case where no marker can honestly be named —
// which is exactly where the retired display string dropped its parenthetical
// provenance, so the two derivations cannot drift: a marker that does not
// qualify at all (the "" return), a pass whose value is a normalization
// FALLBACK (a first-marker or all-disabled-priors pass, a walk that
// terminated on a surviving enabled label ref, a visible prior that is a
// SYNTHETIC projection marker — a collapsed group's replacement 1.00 owner or
// the frame-0 seed), a pass whose visible prior does not itself resolve, and a
// ref the render normalizes (an undefined label, or an implied effective tempo
// outside the [0.125, 8.0] envelope — both spell the 1.00 literal and have no
// source).
//
// Returns "" when the marker does not qualify — owning, malformed, or a
// carve-out ref (a group member or an effectively-disabled ref) whose raw walk
// finds no surviving successor to bound a segment — and LEAVES THE INDEX
// UNTOUCHED then, like every other no-source case above: the two writes are on
// the two success paths and nowhere else, so a caller seeds its own sentinel
// and reads it back unchanged wherever no marker is named. (Architect approval
// 2026-08-29, comment only.) total_frames is the source length in frames; it
// bounds the last projection segment (the resolver's own rule), so a last-segment ref reads
// out exactly as it renders. GUI callers slice their GuiWarpMarker store to
// WarpMarker (slice_to_warp_markers) before calling.
//
// (IT WAS compute_hover_popup_text, whose product was a DISPLAY string —
// "~= 1.28*1.0112 (from a.23 @ 00:19.208)": the value under a "= "/"~= "
// prefix, a parenthetical naming the source marker's own resolved tempo, its
// label and its format_timestamp'd position, and the payload on an
// out-parameter beside it. The hover popup it was named for died with the
// row-5 redesign; the STATUS BAR's right cell carried the string for the one
// day that bar existed; and on 2026-08-29 the bar folded into row 8 and the
// readout retired with it. Nothing displays a resolved value any more — bare
// `j` copies the payload and Shift+`j` jumps to the source marker — so the
// display half, its sample_rate parameter and its format_timestamp use are
// deleted rather than left producer-less.)
std::string resolved_marker_payload(const std::vector<WarpMarker>& mv, int idx,
                                    long total_frames,
                                    int* source_index_out = nullptr);

// The framemap pair the render pipeline drops into the cache dir is always
// the FULL map — trim is a render window, never an artifact shape — so no
// windowed derivation exists here. Artifact convention: the target column is
// deliverable-relative (first pair's target exactly zero) and the source
// column is absolute undisplaced source frames, matching the project-wide
// convention shared by marker files and render-entry sidecars.
