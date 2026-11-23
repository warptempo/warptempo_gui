#pragma once

#include "warpmarkers_parse.h"          // WarpMarker
#include "warp_coincidence.h"           // warp_frames_coincide, millisecond_frames
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
// Disabled markers are filtered out BEFORE conversion, an enabled member of a
// coincident run arrives as a plain 1.00 owner (architect approval
// 2026-11-17, warp_coincident_members below), and a label ref the
// resolver cannot honor (a dangling one, an ambiguous label, an extreme
// implied ratio) arrives as a plain 1.00 owner (architect approval
// 2026-10-29: a marker is disabled by its own flag alone, so a disabled
// definition's refs arrive here dangling and normalized).
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
    // Why a marker resolved to the normalized 1.00 fallback (source_idx
    // -1). Coincident is a member of a coincident run, any kind, enabled or
    // disabled (architect approval 2026-11-17: "they both get 1.0 as their
    // effective tempo"; warp_coincident_members below). The three label
    // reasons are a label ref's: the GUI's red-flag set
    // (warp_red_flag_set_cached) reads them instead of re-searching the raw
    // store, which would name the wrong case for a def that is a coincident
    // member (present raw, labelless in the projection). AmbiguousLabel is a
    // label two or more surviving definitions hold (architect approval
    // 2026-10-29): no reference honors either, label_definition_search's
    // verdict in warp_frame_map_build.cpp. None for every non-fallback
    // result and for every non-member pass.
    enum class NormalizedReason {
        None, Coincident, UndefinedLabel, AmbiguousLabel, ExtremeRatio
    };

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
                                    // coincident member's 1.00 owner or
                                    // the frame-0 seed — that no raw
                                    // index can honestly name)
    NormalizedReason reason = NormalizedReason::None;
    // (No from_ref. It flagged a PASS whose inheritance walk had terminated on
    // a surviving enabled label ref — the render's 1.00 fallback — for the
    // GUI's normalization-red set. The walk now skips every label ref to the
    // owner behind it, so no walk terminates on one and the bit had no
    // producer: DELETED, architect approval 2026-09-13.)
    // (No owner_idx. It named the RAW STORE INDEX of the terminal OWNER the
    // backward walk landed on, and its sole consumer was the tempo
    // drag's forward-label coupling guard; that gesture — the pointer tempo
    // drag and the Left/Right tempo-image step — was deleted 2026-07-29, which
    // left the field written by three sites here and read by
    // nobody, so it is DELETED — architect 2026-07-29, an explicit surgical freeze
    // approval. resolve_inherited_tempo's matching `owner_index`
    // out-parameter, which outlived the field as an unused capability, is deleted
    // too — architect 2026-07-30, a second explicit surgical approval covering
    // exactly that surface.)
};

// Returns the built warp frame map on success, or std::unexpected carrying
// the first violated condition (a concise sentence-capitalized reason —
// these six strings reach STDERR ONLY, the rationale and its capitalization
// clause at the definition; callers add their own context prefix). Does not
// log. Failure conditions, in check order:
// invalid source audio metadata (sample_rate <= 0 or total_frames <= 0),
// src_frame > total_frames (the past-EOF wall — column-symmetric with
// build_phase_reset_source_frames by ruling, and a loud breach backstop for
// hand-assembled input), src_frame - prev_src_frame < 1 (unreachable from
// the resolver, whose stage 2 merges every exact-equal frame to one owner, so
// its output frames are strictly increasing whole frames;
// kept because the engine validates strict ascent but NOT the >= 1-frame
// gap, so a sub-frame segment is the one map defect that would render
// silently wrong bytes), tempo <= 0 (producer-less since the iteration lock of
// 2026-09-10, which keeps every sweep cell in-bracket — the definition's
// comment has the whole account — and kept for
// its message vocabulary: without it a NEGATIVE tempo degrades to a decreasing
// target the engine refuses as "not strictly ascending" and a ZERO tempo to a
// non-finite target the emission finiteness arm below refuses first, and
// neither downstream refusal could NAME the tempo), a label ref with no
// matching label def (Pass 2; unreachable from the resolver, which normalizes
// dangling and ambiguous refs first, architect approval 2026-10-29; kept
// because the engine never consumes refs, so a
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
// render list": A MARKER IS EFFECTIVELY DISABLED BY ITS OWN FLAG ALONE
// (architect approval 2026-10-29: "make Ctrl+D the default of disabling only
// the single marker"). A label ref whose definition is disabled is NOT
// silenced with it: the ref survives, finds no definition among the
// survivors, and the resolver's stage 5 normalizes it to the plain 1.00
// owner at its own frame with its own stderr line — the dangling ref's
// treatment exactly, so it plays as recorded — while the GUI strikes it
// (warp_red_flag_set_cached). Re-enabling the definition restores every such
// ref with no flag residue, a ref's state being its own flag. The carrying
// form, a definition toggled together with its references, is an authoring
// act that writes each ref's own flag (Ctrl+Shift+D,
// toggle_disabled_with_references, warpmarkers_ops.cpp), never a render
// rule. Templated over the marker shape so the parser's WarpMarker walks and
// the GUI's GuiWarpMarker surfaces (paint, selection, hover, nudge) consume
// the ONE definition without a per-call slice copy — the GUI callers sit
// inside paint loops. The name and the vector/index form stay so every
// caller keeps asking the one owner.
template <typename MarkerT>
bool marker_effectively_disabled(const std::vector<MarkerT>& mv, size_t idx) {
    return mv[idx].disabled;
}

// Per-index render-participation mask over parser-domain markers: keep[i]
// is true when marker i survives into the render, false when it is dropped
// (the marker_effectively_disabled verdict above, per index). Single
// owner of the participation verdict: resolve_warp_markers_for_render's
// filter, which every render path funnels through. Warp-only as part of
// the resolver's normalization pipeline — phase reset markers carry no
// labels or references.
std::vector<bool> warp_markers_render_keep_mask(
    const std::vector<WarpMarker>& src);

// THE COINCIDENT RUNS, the rule's one owner as a pure classifier over the RAW
// authored store (architect approval 2026-11-17: "for warp, we should make the
// parser truthful - we need to show coincident markers, and that definition
// needs to include sub-1ms distance, so the parser should also make them both
// 1.0. so if two warp markers are struck in the gui, both should render as 1.0
// in the parser/engine." — "actually i think disabled markers should get
// struck also. they could mess up a copy-paste of phase resets. so it becomes
// 'they both get 1.0 as their effective tempo' not 'they both render as
// 1.0'"): out[i] is true iff raw index i belongs to a COINCIDENT RUN — a
// maximal run of adjacent rows of the time-sorted store in which each row
// coincides with its neighbor (warp_frames_coincide, warp_coincidence.h: the
// same frame or under one millisecond at `sample_rate`), DISABLED ROWS
// COUNTED, size >= 2. No enabled count: a run of an enabled and a disabled
// row is a run, a run of two disabled rows is a run. Equivalently, both rows
// of every adjacent coinciding pair are members (a chain of pairs is one
// run). The store is time-sorted, so a row's nearest neighbors are the rows
// beside it, and every row on a frame another row holds is a member.
// READERS: the resolver's stage 2 (every enabled member renders as a plain
// 1.00 owner), marker_effective (every member reads 1.00, reason Coincident)
// and the GUI's red-flag set (warp_red_flag_set_cached), so the render, the
// readouts and the strike cannot drift.
std::vector<char> warp_coincident_members(
    const std::vector<WarpMarker>& markers, long sample_rate);

// THE AMBIGUOUS LABEL'S DEFINITIONS as a pure classifier over the RAW
// authored store (architect approval 2026-10-29), in
// warp_coincident_members' shape: out[i] is true iff raw index i is
// a surviving, non-member label definition (it reaches the resolver's
// post-stage-2 intermediate with its label) whose label at least one OTHER
// such definition also defines. Two enabled definitions of one label
// are legal — the editor refuses only a label an ENABLED definition holds,
// and re-enabling a disabled definition beside its successor builds the
// state — and the resolver honors neither: every reference to the label
// normalizes to the plain 1.00 owner (stage 5's "has more than one label
// definition"). Built on the SAME silent projection and the same definition
// search the stage-5 lookup reads (label_definition_search, internal to
// warp_frame_map_build.cpp), so the GUI's strike on the definitions and the
// render's verdict on the references cannot drift; `sample_rate` because the
// projection's coincident runs read it (architect approval 2026-11-17).
// Consumed by the GUI's red-flag set (warp_red_flag_set_cached), which
// strikes both definitions and, through marker_effective's AmbiguousLabel,
// every reference. A disabled definition never counts and is never marked:
// it fades, it does not strike.
std::vector<char> warp_ambiguous_label_defs(
    const std::vector<WarpMarker>& markers, long sample_rate);

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
//      markers (each by its own flag alone, architect approval 2026-10-29);
//   2. THE COINCIDENT RUNS (architect approval 2026-11-17, the rulings at
//      warp_coincident_members above): every ENABLED member of a coincident
//      run — the same frame or under one millisecond from its neighbor,
//      disabled rows counted in the run — renders as a plain enabled 1.00
//      owner with no labels at its own frame, and enabled members on one
//      exact frame give ONE such owner (the map needs strictly increasing
//      frames; his "yes" to one 1.00 owner at that frame), one line per run
//      that holds an enabled member. CONSEQUENCES, by his rulings: a member
//      that DEFINES a label no longer defines it ("they also collapse to 1.0
//      and lose their label reference"), so references elsewhere to that
//      label go dangling (stage 5: a plain 1.00 owner, UndefinedLabel) unless
//      another surviving definition holds it, which they then bind (the rule
//      of 2026-10-29, unchanged); a member that REFERENCES a label renders
//      1.00, the reference lost; and an enabled marker coincident only with a
//      DISABLED one renders at 1.00 — a disabled marker changes the render
//      here and nowhere else;
//   3. when no survivor sits at exactly frame 0, silently prepend a plain
//      enabled 1.00 owner there (the documented default, not ambiguity);
//   4. materialize each pass through resolve_inherited_tempo(_scale)'s
//      walk below, which skips label refs to the nearest prior owner
//      (architect approval 2026-09-13) — silent, no line;
//   5. normalize label refs: a dangling ref (no def among the survivors —
//      a disabled def included, architect approval 2026-10-29), an
//      AMBIGUOUS one (two or more defs of its label among the survivors,
//      architect approval 2026-10-29: neither is honored), or a ref whose
//      implied effective tempo lies outside the authored per-marker
//      envelope [0.125, 8.0] becomes a plain 1.00 owner at its own frame,
//      each with a line;
//   6. emit MarkerForRender.
// Output invariants: non-empty; strictly increasing whole frames; the
// first element sits at exactly frame 0 and is enabled (a plain 1.00 owner
// when seeded); every surviving label_ref is resolvable to exactly one
// definition (architect approval 2026-10-29) and its implied tempo in-band.
// Input that normalizes to itself — no coincident
// run, a survivor at frame 0, every ref
// resolvable and in-band — yields the identical resolved list the
// pre-normalization filter produced, so build_warp_frame_map's output is
// byte-for-byte unchanged for such input. Returns a PLAIN std::vector: this
// is a TOTAL NORMALIZER, infallible by design — every ambiguous state
// resolves and the stderr line is the only signal, so there is no error to
// report. build_warp_frame_map keeps its std::expected because it is a
// different KIND of function, a partial compiler whose error arms are breach
// guards for hand-assembled marker lists reaching the build directly (the
// map-artifact contract, and the tempo arm's vocabulary); the two signatures
// rightly differ.
// sample_rate sets the coincidence's millisecond (stage 2's runs,
// warp_frames_coincide; architect approval 2026-11-17) and the stderr
// timestamps; total_frames feeds only the envelope check's last-segment
// distance.
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
// false, not a label reference, and not disabled. Passes, disabled markers
// and EVERY label ref, enabled or not, are skipped (architect approval
// 2026-09-13: a pass walks past a ref exactly as it walks past a pass): a
// disabled marker contributes no tempo downstream, and a ref owns a duration
// equation rather than a rate, so there is no literal for a pass to copy
// from it — the pass takes the owner behind it. Returns 100 cents (tempo
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
// scoped short of. Nor an `inherited_from_ref` out-parameter: it reported a walk
// terminated on a surviving enabled label ref, for the resolver's normalization
// line and MarkerEffective::from_ref; the walk skips refs since the ruling above,
// so it had no producer and is DELETED — architect approval 2026-09-13.)
int64_t resolve_inherited_tempo(const std::vector<WarpMarker>& markers, int index);
std::optional<double> resolve_inherited_tempo_scale(
    const std::vector<WarpMarker>& markers, int index);

// Effective (base_cents, scale, source, reason) a marker resolves to, for
// display/authoring callers in the GUI's display and marker operation paths.
// base_cents == 0 means "could not resolve" (mirrors
// resolved_marker_payload's "" guards; both are display-only safe returns,
// writing no bytes and printing nothing). scale == nullopt means no typed
// scale (treated as 1.0 by callers). sample_rate sets the coincidence's
// millisecond (warp_coincident_members; architect approval 2026-11-17).
// total_frames is the source length in
// frames; it bounds the last projection segment (the resolver's own rule),
// so a last-segment ref resolves and classifies exactly as it renders.
//
// Resolution basis, in order:
//   - A MEMBER OF A COINCIDENT RUN (warp_coincident_members), ANY KIND —
//     owner, pass or ref — ENABLED OR DISABLED, resolves to the 1.00
//     fallback below with reason Coincident (architect approval 2026-11-17:
//     "they both get 1.0 as their effective tempo"): an enabled member is
//     what renders, and a disabled one reads what it would be;
//   - every other OWNER resolves to its own fields;
//   - an effectively-disabled pass/ref resolves against the COINCIDENCE-
//     NORMALIZED RAW STORE (disabled markers do not render at all; the raw
//     store with every member rewritten as the plain 1.00 owner it reads as,
//     its enabled bit and frame kept, so a disabled marker behind a member
//     reads what the member is and a member's label is no definition);
//   - every other pass/ref — surviving and no member — resolves against
//     the silent projection (the render resolver's stages 1-3, no stderr),
//     so its value is what actually renders even when members were
//     normalized or the frame-0 seed was inserted, and the band
//     verdict comes from the shared resolver-order classification helper.
// source_idx names the marker the value is visibly taken from:
//   owner     -> idx itself (its own tempo_cents / tempo_scale).
//   pass      -> the immediate prior surviving NON-REF marker the value is
//                inherited from (NOT necessarily the owning marker if
//                there's a chain of passes; a label ref the walk skipped
//                carries a different value and is never named — architect
//                approval 2026-09-13); on the projection path this
//                is that prior projection entry mapped back to its raw
//                index, and a SYNTHETIC prior — a coincident member's
//                1.00 owner or the frame-0 seed — reports -1
//                (attributing it to any single raw marker would
//                mislead); on the
//                raw-store path a prior that is a member reports -1 the
//                same way.
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
//   - a member of a coincident run, the first bullet above — reason
//     Coincident;
//   - a label ref with no definition on its resolution basis (a def that is
//     a coincident member, or on the projection path a disabled one, is
//     dangling, the render's own verdict) — reason UndefinedLabel;
//   - a label ref whose label two or more definitions hold on its
//     resolution basis (architect approval 2026-10-29; on the raw-store
//     path the ENABLED definitions are asked first and the disabled ones
//     only when no enabled one exists, so a disabled ref reads out against
//     the definition it would bind if enabled) — reason AmbiguousLabel;
//   - a label ref whose implied effective tempo lies outside the authored
//     per-marker envelope [0.125, 8.0] (the shared classification helper,
//     resolver operation order) — reason ExtremeRatio.
MarkerEffective marker_effective(const std::vector<WarpMarker>& mv, int idx,
                                 long sample_rate, long total_frames);

// THE RESOLVED VALUE OF A PASS OR LABEL_REF WARP MARKER, as the pasteable
// payload plus the index of the marker that value came FROM. (ARCHITECT
// APPROVAL 2026-08-29 for the touch on this frozen unit.) Pure parser-domain
// string/math — it resolves through marker_effective above (the silent
// projection for surviving non-member passes/refs, the coincidence-normalized
// raw store for disabled ones), so what it composes is what will be rendered.
//
// A MEMBER OF A COINCIDENT RUN, ANY KIND, ENABLED OR DISABLED, returns the
// 1.00 literal (format_tempo_cents(100), no scale) and leaves the source
// index untouched (architect approval 2026-11-17: a member's effective tempo
// IS 1.00 — "they both get 1.0 as their effective tempo" — so the value copy
// reads 1.00, the render's truth, and no marker is its source).
//
// THE PAYLOAD is the value form the flag editor accepts and the serializer
// writes: format_tempo_cents(base_cents), plus "*" + format_value_double(
// scale, 4) when a scale is present — omitted for a pass whose scale is
// semantically 1, always included for a label ref, whose implied multiplier
// always exists. No prefix, no provenance, no timestamp: just the value.
//
// `source_index_out`, when non-null, receives THE MARKER THE VALUE IS TAKEN
// FROM, and only where such a marker exists:
//   pass      -> the immediate prior surviving non-ref marker it inherits
//                from (NOT necessarily the owning marker if there is a chain
//                of passes; a skipped label ref is never named — architect
//                approval 2026-09-13);
//   label_ref -> the label-definition marker.
// It is LEFT UNTOUCHED in every case where no marker can honestly be named:
// a marker that does not
// qualify at all (the "" return), a pass whose value is a normalization
// FALLBACK (a first-marker or all-disabled-priors pass — refs alone ahead of
// it counting as none, architect approval 2026-09-13 — a visible prior that is a
// SYNTHETIC projection marker or a coincident member — a member's 1.00 owner
// or the frame-0 seed), a pass whose visible prior does not itself resolve, a
// coincident member (above), and a
// ref the render normalizes (an undefined label, an ambiguous one — architect
// approval 2026-10-29 — or an implied effective tempo outside the
// [0.125, 8.0] envelope — all spell the 1.00 literal and have no source).
//
// Returns "" when the marker does not qualify — owning and no member,
// malformed, or an effectively-disabled ref whose walk over the
// coincidence-normalized raw store finds no surviving successor to bound a
// segment — and LEAVES THE INDEX
// UNTOUCHED then, like every other no-source case above: the two writes are on
// the two success paths and nowhere else, so a caller seeds its own sentinel
// and reads it back unchanged wherever no marker is named. (Architect approval
// 2026-08-29, comment only.) total_frames is the source length in frames; it
// bounds the last projection segment (the resolver's own rule), so a last-segment ref reads
// out exactly as it renders. GUI callers slice their GuiWarpMarker store to
// WarpMarker (slice_to_warp_markers) before calling.
//
// (NO DISPLAY STRING IS COMPOSED HERE. No STATE surface displays a resolved
// value — Ctrl+Shift+J (Copy Resolved Value) copies the payload, its success
// card naming the copied value, and Ctrl+J jumps to the source marker — and
// THE HOVER TOOLTIP displays the payload, verbatim and alone, for a cue the
// copy would act on while the GUI's TIPS lamp is lit (marker_tooltip_value,
// the GUI's app_state.h; architect 2026-11-09) — a hover, not a state
// surface. Comment-only, architect approvals 2026-08-29, 2026-09-14,
// 2026-09-29 and 2026-11-09.)
std::string resolved_marker_payload(const std::vector<WarpMarker>& mv, int idx,
                                    long sample_rate, long total_frames,
                                    int* source_index_out = nullptr);

// The framemap pair the render pipeline drops into the cache dir is always
// the FULL map — trim is a render window, never an artifact shape — so no
// windowed derivation exists here. Artifact convention: the target column is
// deliverable-relative (first pair's target exactly zero) and the source
// column is absolute undisplaced source frames, matching the project-wide
// convention shared by marker files and render-entry sidecars.
