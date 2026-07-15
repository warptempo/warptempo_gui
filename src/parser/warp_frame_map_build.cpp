#include "warp_frame_map_build.h"
#include "time_format.h"   // format_timestamp
#include "value_format.h"  // format_value_double

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <map>
#include <string>
#include <vector>

namespace {

double effective_tempo(const MarkerForRender& m) {
    // The authored-tempo DSP boundary: the integer-cents tempo becomes a
    // double HERE, at the slope product, through tempo_from_cents — the one
    // conversion route — yielding bit-identically the double the N.NN text
    // parse used to produce, so the map arithmetic downstream is byte-
    // stable. tempo_scale is a typed double (nullopt means no typed scale,
    // i.e. scale 1). A genuinely non-positive product is still rejected by
    // the callers' tempo <= 0 checks.
    return tempo_from_cents(m.tempo_cents) * m.tempo_scale.value_or(1.0);
}

// A label definition contributes exactly one thing to its references: the
// defining segment's target duration, which the ref imposes on its own span.
struct LabelCacheEntry {
    double delta_tgt = 0.0;
};

// The authored per-marker effective-tempo envelope, the band the label-ref
// normalization checks an implied effective tempo against: marker tempo is
// bracketed to [kTempoMinCents, kTempoMaxCents] = [0.25, 4.00] and marker
// scale to [kScaleMin, kScaleMax] = [0.5, 2] (value_format.h, the SoT), so
// any effective tempo a single authored marker can express lies in
// [0.25 * 0.5, 4.00 * 2] = [0.125, 8.0]. A ref whose implied ratio leaves
// this band could never have been authored as a direct marker and
// normalizes to 1.00. The settings scale is deliberately excluded from the
// implied quantity: it divides every segment uniformly and cancels, which
// keeps the check computable identically by the hover popup (which cannot
// see settings scale).
const double kRefImpliedTempoMin = tempo_from_cents(kTempoMinCents) * kScaleMin;
const double kRefImpliedTempoMax = tempo_from_cents(kTempoMaxCents) * kScaleMax;

// The label-ref band-classification value: the definition's effective tempo
// times the ref/def segment-distance ratio, in the resolver's exact
// left-to-right operation order — ((base * scale) * ref_dist) / def_dist.
// Display and render must classify identically at the INCLUSIVE envelope
// edges above, and algebraically equal reassociations of this product are
// not IEEE-identical there (an implied value that is exactly 0.125 in one
// association can be 0.12499999999999999 in another), so the classification
// value exists once, here, in resolver operation order: the resolver's
// stage-5 band check and marker_effective's band verdict both call this.
// marker_effective still computes its displayed multiplier separately; only
// its in-band/extreme verdict comes from here.
double label_ref_implied_effective_tempo(int64_t def_base_cents,
                                         std::optional<double> def_scale,
                                         int64_t ref_dist_frames,
                                         int64_t def_dist_frames) {
    return tempo_from_cents(def_base_cents) *
           def_scale.value_or(1.0) *
           static_cast<double>(ref_dist_frames) /
           static_cast<double>(def_dist_frames);
}

// marker_effectively_disabled (the shared cascade template in
// warp_frame_map_build.h) is the participation verdict:
// warp_markers_render_keep_mask publishes it per index (and
// resolve_warp_markers_for_render filters through that mask via the shared
// projection below), and marker_effective consumes it three ways — the
// carve-out test that picks its resolution basis, and on the raw-store path
// the label-ref segment distances and the pass-provenance source walk both
// step to the next/prior marker that passes it — so hover values and hover
// source attribution track the frame map.

}  // namespace

std::vector<bool> warp_markers_render_keep_mask(
    const std::vector<WarpMarker>& src) {
    std::vector<bool> keep(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        keep[i] = !marker_effectively_disabled(src, i);
    }
    return keep;
}

namespace {

// Stages 1-3 of the render resolver's normalization pipeline — the shared
// projection: resolve_warp_markers_for_render runs it with emit_stderr=true
// and proceeds with stages 4-6 on the result; marker_effective runs it with
// emit_stderr=false (display resolution against what actually renders, and
// hover must never print). One implementation, so the two surfaces cannot
// drift. sample_rate feeds only the stage-2 stderr timestamps and is unused
// when emit_stderr is false.
//
// When raw_index_out is non-null it receives a vector parallel to the
// result: each entry is the normalized marker's index in `src`, or -1 for a
// synthetic marker (a collapsed group's replacement 1.00 owner, or the
// frame-0 seed) that corresponds to no single raw marker.
std::vector<WarpMarker> normalized_surviving_markers(
    const std::vector<WarpMarker>& src, long sample_rate, bool emit_stderr,
    std::vector<int>* raw_index_out) {
    const double sr_d = static_cast<double>(sample_rate);

    // Stage 1 — filter to the survivors of warp_markers_render_keep_mask
    // (the participation verdict's single owner): disabled markers, and
    // refs whose def is disabled, are dropped. Everything below operates on
    // the survivors-only intermediate — all enabled, cascade applied.
    const std::vector<bool> keep = warp_markers_render_keep_mask(src);
    std::vector<WarpMarker> norm;
    std::vector<int>        raw;
    norm.reserve(src.size() + 1);
    raw.reserve(src.size() + 1);
    for (size_t i = 0; i < src.size(); ++i) {
        if (keep[i]) {
            norm.push_back(src[i]);
            raw.push_back(static_cast<int>(i));
        }
    }

    // Stage 2 — exact-coincidence collapse. Positions are whole int64
    // frames, so exact frame equality is the complete coincidence
    // predicate, and marker times are non-decreasing (the load parser
    // rejects decreasing times; the GUI store is time-sorted), so a group
    // is a run of adjacent equal frames. A group of 2+ survivors is
    // ambiguous wholesale — no member's tempo outranks another's — and is
    // replaced by ONE plain enabled 1.00 owner with no labels: a label def
    // inside a collapsed group dies with it, and refs to it go dangling,
    // which stage 5 then normalizes with their own stderr line — a
    // deterministic cascade rather than a guess at which member to keep.
    {
        std::vector<WarpMarker> collapsed;
        std::vector<int>        collapsed_raw;
        collapsed.reserve(norm.size());
        collapsed_raw.reserve(norm.size());
        size_t i = 0;
        while (i < norm.size()) {
            size_t j = i + 1;
            while (j < norm.size() &&
                   norm[j].time_frame == norm[i].time_frame) {
                ++j;
            }
            if (j - i >= 2) {
                WarpMarker owner;  // defaults: enabled 1.00 owner, no labels
                owner.time_frame = norm[i].time_frame;
                collapsed.push_back(owner);
                collapsed_raw.push_back(-1);
                if (emit_stderr) {
                    std::fprintf(stderr,
                        "coincident warp markers at %s render as tempo 1.00\n",
                        format_timestamp(
                            static_cast<double>(owner.time_frame) / sr_d)
                            .c_str());
                }
            } else {
                collapsed.push_back(norm[i]);
                collapsed_raw.push_back(raw[i]);
            }
            i = j;
        }
        norm = std::move(collapsed);
        raw  = std::move(collapsed_raw);
    }

    // Stage 3 — first-marker seed, mandatory correctness: when no SURVIVING
    // marker sits at exactly frame 0, prepend a plain enabled 1.00 owner
    // there. The surviving list is the basis by ruling — a disabled marker
    // at frame 0, or an enabled ref whose def is disabled, does not count
    // as existing — because build_warp_frame_map never reads
    // markers[0].time_frame: it seeds the map at {0,0} and attributes the
    // opening segment to the first resolved marker wherever it sits, so
    // without this seed a filtered-away frame-0 marker would silently
    // attribute the next owner's tempo from frame 0. SILENT — no stderr:
    // this is the documented default (the same 0|1.00 a fresh source load
    // seeds), not ambiguity.
    if (norm.empty() || norm.front().time_frame != 0) {
        WarpMarker seed;  // defaults: frame 0, enabled 1.00 owner, no labels
        norm.insert(norm.begin(), seed);
        raw.insert(raw.begin(), -1);
    }

    if (raw_index_out) *raw_index_out = std::move(raw);
    return norm;
}

}  // namespace

std::expected<std::vector<MarkerForRender>, std::string>
resolve_warp_markers_for_render(const std::vector<WarpMarker>& src,
                                long sample_rate, long total_frames) {
    // Normalization pipeline: stages 1-5 build a normalized WarpMarker
    // intermediate, stage 6 emits MarkerForRender. Ambiguity resolves to
    // tempo 1.00 with one stderr line per affected timestamp, printed on
    // every resolve (no dedup or session state — a resting ambiguous state
    // re-prints on each recompute, the intended signal). Contract and
    // output invariants in warp_frame_map_build.h. sample_rate is
    // display-only (the stderr timestamps); total_frames feeds only the
    // envelope check's last-segment distance.
    const double sr_d = static_cast<double>(sample_rate);

    // Stages 1-3 — the shared projection above (survivor filter,
    // exact-coincidence collapse, frame-0 seed), with the stderr lines on.
    std::vector<WarpMarker> norm = normalized_surviving_markers(
        src, sample_rate, /*emit_stderr=*/true, nullptr);

    // Stage 4 — pass materialization through the ref-opaque inheritance
    // walk (resolve_inherited_tempo / resolve_inherited_tempo_scale below,
    // the same walk the hover surfaces use, so render and hover cannot
    // disagree). Resolutions are computed against the pre-materialization
    // intermediate — passes stay transparent to each other's walks, exactly
    // as marker_effective's projection walk sees them — then applied, so in
    // a pass-pass-ref chain EVERY pass whose walk reaches the ref resolves
    // to the 1.00 fallback and prints its own line.
    {
        struct PassResolution {
            size_t                idx;
            int64_t               cents;
            std::optional<double> scale;
            bool                  from_ref;
        };
        std::vector<PassResolution> resolutions;
        for (size_t i = 0; i < norm.size(); ++i) {
            if (!norm[i].label_ref.empty() || !norm[i].tempo_inherits) {
                continue;
            }
            bool from_ref = false;
            const int gi = static_cast<int>(i);
            PassResolution pr;
            pr.idx      = i;
            pr.cents    = resolve_inherited_tempo(norm, gi, &from_ref);
            pr.scale    = resolve_inherited_tempo_scale(norm, gi);
            pr.from_ref = from_ref;
            resolutions.push_back(std::move(pr));
        }
        for (const PassResolution& pr : resolutions) {
            WarpMarker& p = norm[pr.idx];
            p.tempo_inherits = false;
            p.tempo_cents    = pr.cents;
            p.tempo_scale    = pr.scale;
            // label_def survives materialization: a `pass:LABEL` def is
            // concrete from here on, so stage 5 measures it like any owner.
            if (pr.from_ref) {
                std::fprintf(stderr,
                    "pass marker at %s inherits from a label ref; "
                    "renders as tempo 1.00\n",
                    format_timestamp(
                        static_cast<double>(p.time_frame) / sr_d).c_str());
            }
        }
    }

    // Stage 5 — label-ref normalization (after stage 4, so defs that were
    // passes are concrete). Each ref is checked independently: normalizing
    // one replaces it in place with a plain 1.00 owner at its own frame,
    // changing no positions and no label defs, hence no other ref's
    // geometry or resolvability. The def search mirrors
    // build_warp_frame_map's pass-1 label cache (defs on non-ref markers;
    // duplicate defs are load-fatal, so a match is the one definition),
    // which is what makes the builder's pass-2 dangling-lookup tripwire
    // unreachable from here. The implied effective tempo is the exact
    // build/hover quantity with the settings scale excluded (it cancels —
    // see the envelope constants above): the def's effective tempo times
    // the segment-distance ratio, where each distance runs to the NEXT
    // marker in the normalized intermediate or to total_frames for the
    // last. Denominators are safe: post-collapse frames are strictly
    // increasing, so distinct markers differ by >= 1 frame.
    {
        auto next_frame = [&](size_t k) -> int64_t {
            return (k + 1 < norm.size()) ? norm[k + 1].time_frame
                                         : static_cast<int64_t>(total_frames);
        };
        for (size_t i = 0; i < norm.size(); ++i) {
            if (norm[i].label_ref.empty()) continue;
            const std::string name = norm[i].label_ref;

            size_t def_idx = norm.size();
            for (size_t j = 0; j < norm.size(); ++j) {
                if (norm[j].label_ref.empty() &&
                    !norm[j].label_def.empty() &&
                    norm[j].label_def == name) {
                    def_idx = j;
                    break;
                }
            }

            bool        normalize = false;
            const char* reason    = nullptr;
            if (def_idx == norm.size()) {
                normalize = true;
                reason    = "has no label definition";
            } else {
                const WarpMarker& d = norm[def_idx];
                // The shared classification helper IS this stage's band
                // value, in this site's historical operation order —
                // marker_effective's band verdict calls the same helper.
                const double implied = label_ref_implied_effective_tempo(
                    d.tempo_cents, d.tempo_scale,
                    next_frame(i) - norm[i].time_frame,
                    next_frame(def_idx) - d.time_frame);
                if (implied < kRefImpliedTempoMin ||
                    implied > kRefImpliedTempoMax) {
                    normalize = true;
                    reason    = "implies an extreme ratio";
                }
            }
            if (!normalize) continue;

            WarpMarker plain;  // defaults: enabled 1.00 owner, no labels
            plain.time_frame = norm[i].time_frame;
            norm[i] = plain;
            std::fprintf(stderr,
                "label reference %s at %s %s; renders as tempo 1.00\n",
                name.c_str(),
                format_timestamp(
                    static_cast<double>(plain.time_frame) / sr_d).c_str(),
                reason);
        }
    }

    // Stage 6 — emit MarkerForRender from the normalized intermediate.
    // Passes were materialized in stage 4, so every marker is either a ref
    // (tempo fields inert, label_ref carried) or a concrete owner.
    std::vector<MarkerForRender> out;
    out.reserve(norm.size());
    for (const WarpMarker& g : norm) {
        MarkerForRender m;
        m.time_frame = g.time_frame;
        m.label_def  = g.label_def;
        m.label_ref  = g.label_ref;
        if (!g.label_ref.empty()) {
            m.tempo_cents = 0;
            m.tempo_scale.reset();
        } else {
            m.tempo_cents = g.tempo_cents;
            m.tempo_scale = g.tempo_scale;
        }
        out.push_back(std::move(m));
    }
    return out;
}

// Contract: the backward walk stops at the nearest non-disabled owner, or
// at the nearest SURVIVING enabled label ref — the ref-hit returns the
// 100-cents / nullopt fallback and reports it through inherited_from_ref
// (the resolver prints its normalization line from that signal; display
// callers pass nothing). Passes, disabled markers, and cascade-disabled
// refs stay transparent (render-inert, they contribute no tempo
// downstream). This walk is the total display-time resolution: no geometry
// enters it, so no cycle is possible, and every marker list resolves to a
// definite value, the single source of its meaning across render,
// warpframemap, and hover — a pass at rest behind a
// surviving enabled ref renders AND displays as tempo 1.00, and because
// the walk itself owns that verdict the two surfaces cannot disagree.
// Passes deliberately never inherit THROUGH a ref: a ref owns a duration
// equation, not a rate, and its implied rate depends on segment geometry
// including the position of the very marker that follows it, so
// inheriting it would make a pass's tempo drift under unrelated position
// edits — which is exactly why the ambiguity resolves to the 1.00
// fallback instead of the ref's implied rate. Pass values must stay
// literal, grammar-exact owner fields so that freezing a pass (tempo
// nudge, Ctrl+N) is lossless.
int64_t resolve_inherited_tempo(const std::vector<WarpMarker>& markers, int index,
                                bool* inherited_from_ref) {
    if (inherited_from_ref) *inherited_from_ref = false;
    for (int i = index - 1; i >= 0; --i) {
        const WarpMarker& m = markers[i];
        if (!m.label_ref.empty()) {
            if (!marker_effectively_disabled(markers, static_cast<size_t>(i))) {
                if (inherited_from_ref) *inherited_from_ref = true;
                return 100;
            }
            continue;
        }
        if (!m.tempo_inherits && !m.disabled) {
            return m.tempo_cents;
        }
    }
    // Reachable: a leading pass with no owner before it resolves to this
    // fallback on every surface — the hover popup, and the render (where
    // the resolver's frame-0 seed becomes the owner such a pass inherits,
    // the same 1.00).
    return 100;
}

std::optional<double> resolve_inherited_tempo_scale(
    const std::vector<WarpMarker>& markers, int index,
    bool* inherited_from_ref) {
    if (inherited_from_ref) *inherited_from_ref = false;
    for (int i = index - 1; i >= 0; --i) {
        const WarpMarker& m = markers[i];
        if (!m.label_ref.empty()) {
            if (!marker_effectively_disabled(markers, static_cast<size_t>(i))) {
                if (inherited_from_ref) *inherited_from_ref = true;
                return std::nullopt;
            }
            continue;
        }
        if (!m.tempo_inherits && !m.disabled) {
            return m.tempo_scale;
        }
    }
    return std::nullopt;
}

MarkerEffective marker_effective(
    const std::vector<WarpMarker>& mv, int idx, long total_frames) {
    MarkerEffective r;
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return r;
    const WarpMarker& m = mv[idx];

    // Owner: resolves to its own tempo_cents / tempo_scale — coincident
    // group members included (a collapsed group's members keep their
    // authored readouts, the accepted authored-display split).
    if (!m.tempo_inherits && m.label_ref.empty()) {
        r.base_cents = m.tempo_cents;
        r.scale      = m.tempo_scale;
        r.source_idx = idx;
        return r;
    }

    // Raw-store carve-outs: an effectively-disabled marker does not render
    // at all (there is no render value to match), and a pass/ref that is
    // itself a member of a 2+-survivor exact-frame group sits inside the
    // accepted authored-display split (the render collapses the whole
    // group to one plain 1.00 owner; the members keep authored readouts by
    // ruling). Both keep the raw-store resolution below. Everything else —
    // a surviving, un-collapsed pass or ref — resolves against the silent
    // projection (normalized_surviving_markers), so its readout tracks
    // what renders even when OTHER markers were collapsed away or the
    // frame-0 seed was inserted.
    bool raw_walk = marker_effectively_disabled(mv, static_cast<size_t>(idx));
    if (!raw_walk) {
        int survivors_at_frame = 0;
        for (size_t i = 0; i < mv.size(); ++i) {
            if (mv[i].time_frame == m.time_frame &&
                !marker_effectively_disabled(mv, i)) {
                ++survivors_at_frame;
            }
        }
        raw_walk = survivors_at_frame >= 2;
    }

    if (!raw_walk) {
        // Projection path: stages 1-3 of the render resolver, silent
        // (hover never prints), with the raw-index mapping back into mv.
        // The hovered marker survives un-collapsed, so it has exactly one
        // image in the projection. sample_rate feeds only the projection's
        // (suppressed) stderr timestamps, so 0 is fine here.
        std::vector<int> raw_index;
        const std::vector<WarpMarker> proj = normalized_surviving_markers(
            mv, /*sample_rate=*/0, /*emit_stderr=*/false, &raw_index);
        int img = -1;
        for (size_t k = 0; k < proj.size(); ++k) {
            if (raw_index[k] == idx) {
                img = static_cast<int>(k);
                break;
            }
        }
        if (img < 0) return r;  // unreachable: a surviving un-collapsed
                                // marker always has an image

        if (m.tempo_inherits) {
            // Pass: the same ref-opaque inheritance walk the resolver's
            // stage 4 runs, over the same pre-materialization list, so the
            // value matches the render by construction.
            bool from_ref = false;
            r.base_cents = resolve_inherited_tempo(proj, img, &from_ref);
            r.scale      = resolve_inherited_tempo_scale(proj, img);
            // A walk terminated by a surviving enabled label ref is the
            // render's 1.00 fallback, not a value inherited from the ref —
            // attributing it to the ref would mislead — so it reports with
            // no visible source, mirroring the resolver's normalization.
            if (from_ref) {
                r.source_idx = -1;
                return r;
            }
            // Provenance: the immediate prior marker in the projection —
            // the visible source of the inherited value, not necessarily
            // the owning marker if there is a chain of passes; every
            // projection entry renders, so the prior is never render-inert.
            // A synthetic owner (a collapsed group's replacement or the
            // frame-0 seed) maps to -1 through raw_index: attributing it
            // to any single raw marker would mislead, so the popup shows
            // the bare resolved value with no provenance.
            if (img > 0) r.source_idx = raw_index[img - 1];
            return r;
        }

        // Label ref against the projection: the def search mirrors the
        // resolver's stage-5 lookup exactly, so a def that died in a
        // collapse is dangling here — the render's own verdict.
        int def_k = -1;
        for (size_t j = 0; j < proj.size(); ++j) {
            if (proj[j].label_ref.empty() && !proj[j].label_def.empty() &&
                proj[j].label_def == m.label_ref) {
                def_k = static_cast<int>(j);
                break;
            }
        }
        if (def_k < 0) {
            // Dangling ref: the render resolver normalizes it to the plain
            // 1.00 owner, so the display reports the same exact literal —
            // base 100, no scale, no visible source (the value is a
            // fallback, not taken from anywhere).
            r.base_cents = 100;
            r.scale.reset();
            r.source_idx = -1;
            r.reason = MarkerEffective::NormalizedReason::UndefinedLabel;
            return r;
        }

        // Segment distances over the projection, each running to the NEXT
        // projection marker or to total_frames for the last — the
        // resolver's own rule, so a last-segment ref reads out and
        // classifies exactly as it renders.
        auto next_frame = [&](int k) -> int64_t {
            return (k + 1 < static_cast<int>(proj.size()))
                       ? proj[k + 1].time_frame
                       : static_cast<int64_t>(total_frames);
        };
        const int64_t ref_dist = next_frame(img) - proj[img].time_frame;
        const int64_t def_dist = next_frame(def_k) - proj[def_k].time_frame;
        if (ref_dist <= 0 || def_dist <= 0) return r;

        const WarpMarker& def = proj[def_k];
        int64_t               def_base_cents;
        std::optional<double> def_scale;
        if (def.tempo_inherits) {
            // An inheriting definition (`pass:LABEL`): the resolver's
            // stage 4 materializes it through this same walk over this
            // same list before stage 5 measures it, so the hover def
            // fields match the render by construction.
            def_base_cents = resolve_inherited_tempo(proj, def_k);
            def_scale      = resolve_inherited_tempo_scale(proj, def_k);
        } else {
            def_base_cents = def.tempo_cents;
            def_scale      = def.tempo_scale;
        }
        const double def_scale_val = def_scale.value_or(1.0);
        // Display-domain cents-to-double boundary: the multiplier below is
        // a derived scale-like quantity, so the def's tempo enters its
        // double arithmetic here, through the one conversion route.
        const double def_base      = tempo_from_cents(def_base_cents);
        const double def_eff_tempo = def_base * def_scale_val;
        if (def_base_cents == 0 || def_eff_tempo == 0.0) return r;

        // Band verdict from the shared classification helper — the
        // resolver's stage-5 value in the resolver's operation order, so
        // display and render agree at the inclusive envelope edges.
        // Out-of-band means the render normalizes this ref to the plain
        // 1.00 owner, so the display reports that exact literal with no
        // visible source.
        const double implied = label_ref_implied_effective_tempo(
            def_base_cents, def_scale, ref_dist, def_dist);
        if (implied < kRefImpliedTempoMin || implied > kRefImpliedTempoMax) {
            r.base_cents = 100;
            r.scale.reset();
            r.source_idx = -1;
            r.reason = MarkerEffective::NormalizedReason::ExtremeRatio;
            return r;
        }

        // settings.scale cancels in the engine's multiplier expression:
        //   multiplier = (ref_dist * def_eff_tempo)
        //              / (def_base * def_dist)
        // which is the geometry distance ratio times def_scale exactly once
        // (def_eff_tempo = def_base * def_scale, and def_base cancels), so
        // multiplier IS the displayed scale: base * multiplier equals the
        // segment's frame-map effective tempo in both scale-presence cases.
        // Carried unclamped — full double, no display ceiling.
        const double multiplier =
            (static_cast<double>(ref_dist) * def_eff_tempo) /
            (def_base * static_cast<double>(def_dist));
        r.scale      = multiplier;
        r.base_cents = def_base_cents;
        // The def's raw-store index — never synthetic: synthetic owners
        // carry no labels, so a found def is always a real raw marker.
        r.source_idx = raw_index[def_k];
        return r;
    }

    // ---- Raw-store resolution: the carve-outs (effectively-disabled
    // markers and 2+-survivor exact-frame group members) resolve against
    // the raw store, unchanged. ----

    if (m.tempo_inherits) {
        // resolve_inherited_tempo walks backward from `walk-1`. Starting at
        // idx+1 lets it return idx's resolved tempo if idx is the only
        // inheriting marker in front of an owning origin.
        const int walk = idx + 1;
        bool from_ref = false;
        r.base_cents = resolve_inherited_tempo(mv, walk, &from_ref);
        r.scale      = resolve_inherited_tempo_scale(mv, walk);
        // A walk terminated by a surviving enabled label ref is the render's
        // 1.00 fallback, not a value inherited from the ref — attributing it
        // to the ref would mislead — so it reports with no visible source,
        // mirroring the resolver's normalization.
        if (from_ref) {
            r.source_idx = -1;
            return r;
        }
        // source_idx is the immediate prior render-surviving marker — the
        // visible source of the inherited value, not necessarily the owning
        // marker if there is a chain of passes; cascade-disabled refs are
        // skipped because they are render-inert and carry no tempo payload, so
        // they can never be the visible source. (A surviving enabled ref
        // cannot be reached here either: were the first surviving prior a
        // ref, the walk above would have terminated on it.)
        for (int i = idx - 1; i >= 0; --i) {
            if (!marker_effectively_disabled(mv, static_cast<size_t>(i))) {
                r.source_idx = i;
                break;
            }
        }
        return r;
    }

    // Label ref (the only remaining kind: owners returned at the top, passes
    // just above), resolved against the raw store.
    int def_idx = -1;
    for (int i = 0; i < static_cast<int>(mv.size()); ++i) {
        if (mv[i].label_def == m.label_ref) {
            def_idx = i;
            break;
        }
    }
    if (def_idx < 0) {
        // Dangling ref: the render resolver normalizes it to the plain
        // 1.00 owner, so the display reports the same exact literal —
        // base 100, no scale, no visible source (the value is a
        // fallback, not taken from anywhere).
        r.base_cents = 100;
        r.scale.reset();
        r.source_idx = -1;
        r.reason = MarkerEffective::NormalizedReason::UndefinedLabel;
        return r;
    }

    // Render measures each segment to the next marker that survives into
    // the resolved list, so the reference and definition distances must run
    // to the next surviving successor, not the raw adjacent marker: a
    // disabled marker (or a cascade-disabled ref) parked immediately after
    // either endpoint would otherwise skew the implied multiplier while
    // leaving the frame map untouched.
    auto next_surviving = [&](int from) -> int {
        for (int i = from + 1; i < static_cast<int>(mv.size()); ++i) {
            if (!marker_effectively_disabled(mv, static_cast<size_t>(i)))
                return i;
        }
        return -1;
    };
    const int idx_next = next_surviving(idx);
    const int def_next = next_surviving(def_idx);
    // No surviving successor: the carve-out kinds keep the no-readout
    // contract here (an effectively-disabled ref never renders, and a
    // group member's authored readout needs a raw successor to bound its
    // segment); the projection path above is where a last-segment ref
    // measures to total_frames, the resolver's own rule.
    if (idx_next < 0 || def_next < 0) return r;

    // Frame-native segment distances: authored positions are whole
    // source frames (int64_t), so no sample-rate conversion exists; the
    // integer differences widen exactly into the double arithmetic.
    const int64_t lr_dist_frames =
        mv[idx_next].time_frame - mv[idx].time_frame;
    const int64_t def_dist_frames =
        mv[def_next].time_frame - mv[def_idx].time_frame;
    if (def_dist_frames <= 0 || lr_dist_frames <= 0) return r;
    const double lr_src_dist  = static_cast<double>(lr_dist_frames);
    const double def_src_dist = static_cast<double>(def_dist_frames);

    const WarpMarker& def = mv[def_idx];
    int64_t               def_base_cents;
    std::optional<double> def_scale;
    if (def.tempo_inherits) {
        // An inheriting definition (a pass) contributes both its resolved
        // base and its resolved scale, mirroring resolve_warp_markers_for_render
        // so the hover multiplier matches the frame map. Both resolvers walk
        // backward from def_idx-1, correctly excluding the pass itself.
        def_base_cents = resolve_inherited_tempo(mv, def_idx);
        def_scale      = resolve_inherited_tempo_scale(mv, def_idx);
    } else {
        def_base_cents = def.tempo_cents;
        def_scale      = def.tempo_scale;
    }
    const double def_scale_val = def_scale.value_or(1.0);
    // Display-domain cents-to-double boundary: the multiplier below is
    // a derived scale-like quantity, so the def's tempo enters its
    // double arithmetic here, through the one conversion route.
    const double def_base      = tempo_from_cents(def_base_cents);
    const double def_eff_tempo = def_base * def_scale_val;
    if (def_base_cents == 0 || def_eff_tempo == 0.0) return r;

    // Band verdict from the shared classification helper (resolver
    // operation order — the same value the resolver's stage-5 check
    // computes, with the settings scale excluded since it cancels).
    // Out-of-band means the render normalizes this ref to the plain 1.00
    // owner, so the display reports that exact literal with no visible
    // source.
    const double implied = label_ref_implied_effective_tempo(
        def_base_cents, def_scale, lr_dist_frames, def_dist_frames);
    if (implied < kRefImpliedTempoMin || implied > kRefImpliedTempoMax) {
        r.base_cents = 100;
        r.scale.reset();
        r.source_idx = -1;
        r.reason = MarkerEffective::NormalizedReason::ExtremeRatio;
        return r;
    }

    // settings.scale cancels in the engine's multiplier expression:
    //   multiplier = (lr_src_dist * def_eff_tempo)
    //              / (def_base * def_src_dist)
    // which is the geometry distance ratio times def_scale exactly once
    // (def_eff_tempo = def_base * def_scale, and def_base cancels), so
    // multiplier IS the displayed scale: base * multiplier equals the
    // segment's frame-map effective tempo in both scale-presence cases.
    // Multiplying def_scale in a second time here would square it —
    // hover/copy would advertise 0.6400 for a 0.8000-scaled definition
    // over equal distances, and pasting that would author the wrong
    // tempo.
    const double multiplier =
        (lr_src_dist * def_eff_tempo) / (def_base * def_src_dist);
    // The combined multiplier is carried unclamped — values are full
    // doubles with no display ceiling; the render's ref handling is
    // delta-based and never reads this.
    r.scale = multiplier;
    r.base_cents = def_base_cents;
    r.source_idx = def_idx;
    return r;
}

std::string compute_hover_popup_text(
    const std::vector<WarpMarker>& mv, int idx, int sample_rate,
    long total_frames, std::string* copy_payload_out) {
    // The hover-copy hint. A readout with no provenance carries it as its own
    // trailing parenthetical (kCopyHint). A readout that already ends in a
    // "(from ... @ TIME)" provenance parenthetical folds the hint inside that
    // parenthetical after a comma via kCopyHintJoined, which replaces the
    // provenance's closing ")". Either way the value the binding actually
    // copies is copy_payload_out, set from the readout's own value substring
    // below and independent of the hint's placement.
    static constexpr const char* kCopyHint       = " (ctrl+c to copy)";
    static constexpr const char* kCopyHintJoined = ", ctrl+c to copy)";
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return "";
    // sample_rate is display-only: it renders the provenance time as
    // format_timestamp(frame / sample_rate).
    const double sr_d = static_cast<double>(sample_rate);
    if (sr_d <= 0.0) return "";
    const WarpMarker& m = mv[idx];

    if (m.tempo_inherits) {
        const MarkerEffective eff = marker_effective(mv, idx, total_frames);
        if (eff.base_cents == 0) return "";

        std::string out = "= ";
        out += format_tempo_cents(eff.base_cents);
        if (eff.scale.has_value()) {
            out += "*";
            out += format_value_double(*eff.scale, 4);
        }
        // Payload is the value form the flag editor accepts — the readout minus
        // its "= " prefix (base, plus "*scale" only when a scale is present), so
        // it can never drift from what the popup shows.
        if (copy_payload_out) *copy_payload_out = out.substr(2);

        // A first-marker pass resolves to the 1.00 default and has no prior
        // marker to attribute, so source_idx stays negative; the popup shows
        // just the resolved tempo ("= 1.00") without a provenance suffix. The
        // same guard covers a pass whose priors are all disabled, a pass
        // whose inheritance walk terminated on a surviving enabled label ref
        // (the render's 1.00 fallback — a fallback has no source to name),
        // and a pass whose visible prior is a synthetic projection marker —
        // a collapsed group's replacement owner or the frame-0 seed — which
        // no raw marker can honestly be named for.
        if (eff.source_idx < 0) return out + kCopyHint;

        // Provenance: the immediate prior marker's own resolved displayed
        // tempo (its base, or base*scale if it carries a typed scale) and its
        // position. The prior marker can itself be a pass or a label_ref,
        // whose stored tempo_cents/tempo_scale are inert placeholders, so the
        // descriptor is built from that marker's own resolution rather than
        // its raw fields.
        const WarpMarker& src = mv[eff.source_idx];
        const MarkerEffective src_eff =
            marker_effective(mv, eff.source_idx, total_frames);
        if (src_eff.base_cents == 0) return out + kCopyHint;

        // The visible immediate prior is never an enabled label ref here:
        // were the first surviving prior a ref, the inheritance walk would
        // have terminated on it and the source_idx<0 return above fired. A
        // prior pass's own resolution prints in full — values carry no
        // display ceiling.
        std::string descriptor = format_tempo_cents(src_eff.base_cents);
        if (src_eff.scale.has_value()) {
            descriptor += "*";
            descriptor += format_value_double(*src_eff.scale, 4);
        }
        out += " (from ";
        out += descriptor;
        out += " @ ";
        out += format_timestamp(src.time_frame / sr_d);
        out += kCopyHintJoined;
        return out;
    }

    if (!m.label_ref.empty()) {
        const MarkerEffective eff = marker_effective(mv, idx, total_frames);
        if (eff.base_cents == 0) return "";

        // A ref the render normalizes (marker_effective's dangling and
        // extreme-ratio fallbacks, source_idx -1) reads the exact literal it
        // renders as — a hard "=", not "~=": nothing geometry-implied
        // remains once the value is the 1.00 owner. marker_effective's
        // reason field distinguishes the parenthetical: it carries the
        // verdict of the list marker_effective actually resolved against
        // (a def that died in a coincidence collapse exists raw but is
        // dangling in the projection, so a raw re-search here would name
        // the wrong case).
        if (eff.source_idx < 0) {
            std::string out = "= ";
            out += format_tempo_cents(eff.base_cents);
            if (copy_payload_out) *copy_payload_out = out.substr(2);
            out += (eff.reason ==
                    MarkerEffective::NormalizedReason::UndefinedLabel)
                       ? " (undefined label, ctrl+c to copy)"
                       : " (extreme label ratio, ctrl+c to copy)";
            return out;
        }

        const std::string base_text = format_tempo_cents(eff.base_cents);
        // "~=" marks an implied, geometry-dependent multiplier (contrast the
        // pass popup's hard "=", an exact inherited literal). The value
        // prints in full — no display ceiling at any magnitude.
        std::string out = "~= ";
        out += base_text;
        out += "*";
        out += format_value_double(eff.scale.value_or(1.0), 4);
        // Payload is the readout minus its "~= " prefix — the label ref's
        // implied multiplier is always present, so the scale is always
        // included, matching the value the flag editor accepts.
        if (copy_payload_out) *copy_payload_out = out.substr(3);

        // Provenance: "<def_base>:<label>" and the def marker's position.
        const WarpMarker& def = mv[eff.source_idx];
        out += " (from ";
        out += base_text;
        out += ":";
        out += m.label_ref;
        out += " @ ";
        out += format_timestamp(def.time_frame / sr_d);
        out += kCopyHintJoined;
        return out;
    }

    return "";
}

std::expected<std::vector<WarpFrameMapSegment>, std::string>
build_warp_frame_map(const std::vector<MarkerForRender>& markers,
                     double scale, long sample_rate, long total_frames) {
    std::vector<WarpFrameMapSegment> out;

    if (sample_rate <= 0 || total_frames <= 0) {
        return std::unexpected("invalid source audio metadata");
    }

    // Pass 1: accumulate per-label deltas so forward-declared references
    // receive the correct duration when encountered in Pass 2.
    std::map<std::string, LabelCacheEntry> label_cache;

    double src_f_prev = 0.0;

    for (size_t i = 0; i < markers.size(); ++i) {
        // Authored positions are whole source frames (int64_t); the next
        // marker's position widens exactly into the double map arithmetic,
        // no sample-rate multiply.
        double src_frame = (i + 1 < markers.size())
            ? static_cast<double>(markers[i + 1].time_frame)
            : static_cast<double>(total_frames);

        if (src_frame > static_cast<double>(total_frames)) {
            return std::unexpected("marker time exceeds source length at marker "
                                   + std::to_string(i));
        }
        // Sub-frame segments are unreachable from the resolver: its
        // exact-coincidence collapse guarantees >= 1-frame spacing between
        // resolved markers (positions are whole frames). The check survives
        // as the map-artifact contract guard (strictly ascending source
        // column) at the sole producer, for hand-assembled marker lists that
        // reach the build directly.
        if (src_frame - src_f_prev < 1.0) {
            return std::unexpected("marker segment < 1 frame at marker "
                                   + std::to_string(i));
        }

        const auto& m = markers[i];
        const bool is_numeric   = m.label_ref.empty();

        if (is_numeric) {
            double tempo_val = effective_tempo(m);
            // The bracketed value vocabulary holds every AUTHORED product
            // (tempo * marker scale) in [1/8, 8] with bounded, exact
            // anchors. The sweep batches' computed per-cell tempo mutations
            // are the one wider route: a delta can push the product to
            // (kTempoMaxCents + kIterDeltaMaxCents) cents times kScaleMax or
            // drive a cell tempo non-positive, and this builder
            // is that path's ruled async-stderr backstop — the tempo <= 0
            // guard is exactly where such a cell refuses. The divisor check
            // and the finite/strictly-advancing anchor chokepoint in pass 2
            // are unreachable from authored input; all are kept as loud
            // refusals guarding the map artifact contract (finite, strictly
            // ascending values on both columns) at the sole producer, since
            // the map output formats ship the artifact without any engine
            // pass.
            if (tempo_val <= 0.0) {
                return std::unexpected("tempo " +
                                       format_value_double(tempo_val, 2)
                                       + " <= 0 at marker " + std::to_string(i));
            }
            // The divisor is the product against the settings scale — the
            // value the division actually uses — refused in its own right.
            const double divisor = tempo_val * scale;
            if (!std::isfinite(divisor) || divisor <= 0.0) {
                return std::unexpected("tempo-scale product " +
                                       format_value_double(divisor, 0)
                                       + " is not a positive finite value at marker "
                                       + std::to_string(i));
            }

            double delta_src = src_frame - src_f_prev;
            double delta_tgt = delta_src / divisor;

            if (!m.label_def.empty()) {
                label_cache[m.label_def] = LabelCacheEntry{delta_tgt};
            }
        }

        src_f_prev = src_frame;
    }

    // Pass 2: emit warp_frame_map segments.
    out.push_back({0, 0});

    src_f_prev = 0.0;
    double tgt_f_prev = 0.0;

    for (size_t i = 0; i < markers.size(); ++i) {
        double src_frame = (i + 1 < markers.size())
            ? static_cast<double>(markers[i + 1].time_frame)
            : static_cast<double>(total_frames);

        const auto& m = markers[i];
        double target_frame = 0.0;

        if (!m.label_ref.empty()) {
            // Internal tripwire, unreachable from program input: the
            // resolver normalizes dangling refs into plain 1.00 owners
            // before the build, so every ref arriving here has a def among
            // the resolved markers. Kept as the loud refusal for
            // hand-assembled marker lists reaching the build directly.
            // Definition uniqueness is load-enforced, so a found entry is
            // the one definition.
            const auto lbl_it = label_cache.find(m.label_ref);
            if (lbl_it == label_cache.end()) {
                return std::unexpected("label ref has no matching label def: '"
                                       + m.label_ref + "' at marker "
                                       + std::to_string(i));
            }
            const LabelCacheEntry& lbl = lbl_it->second;
            // A label_ref imposes its definition's target duration; there is
            // no ceiling on the implied stretch multiplier — extreme implied
            // multipliers are the author's concern.
            target_frame = tgt_f_prev + lbl.delta_tgt;
        } else {
            double tempo_val = effective_tempo(m);
            // Pass 1 already walked every numeric marker and returned first on
            // any non-positive or non-finite divisor, so the named local here
            // is the vetted product the segment arithmetic divides by.
            const double divisor = tempo_val * scale;
            double delta_src = src_frame - src_f_prev;
            target_frame = tgt_f_prev + (delta_src / divisor);
        }

        // Emission chokepoint: every segment flavor converges here (numeric,
        // label ref via the cached delta; passes resolve to numeric owners
        // before this builder runs). The source column's strict ascent is
        // guaranteed by pass 1's sub-frame and past-EOF refusals; the target
        // column's finite/strictly-advancing check is the same kept loud
        // refusal — unreachable while anchors stay bounded and exact under the
        // bracketed vocabulary, but the map artifact contract is enforced here
        // at the sole producer.
        if (!std::isfinite(target_frame)) {
            return std::unexpected("target anchor is not finite at marker "
                                   + std::to_string(i));
        }
        if (target_frame <= tgt_f_prev) {
            return std::unexpected("target span vanishes at marker "
                                   + std::to_string(i));
        }

        out.push_back({src_frame, target_frame});

        src_f_prev = src_frame;
        tgt_f_prev = target_frame;
    }

    return out;
}
