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
    // tempo_scale is a typed double (nullopt means no typed scale, i.e.
    // scale 1). A genuinely non-positive product is still rejected by the
    // callers' tempo <= 0 checks.
    return m.tempo_base * m.tempo_scale.value_or(1.0);
}

// A label definition contributes exactly one thing to its references: the
// defining segment's target duration, which the ref imposes on its own span.
struct LabelCacheEntry {
    double delta_tgt = 0.0;
};

// Single source of truth for "does this raw marker survive into the render
// list". A marker is silenced either by its own disabled flag or, for an
// enabled label ref, by its definition marker being disabled — the cascade,
// because the definition supplies the duration the ref imposes, so a silenced
// definition leaves the ref with nothing to reproduce.
// warp_markers_render_keep_mask publishes this verdict per index (and
// resolve_warp_markers_for_render filters through that mask), marker_effective
// measures label-ref segment distances to the next marker that passes it, and
// the pass-provenance source walk selects the immediate prior marker that
// passes it, so both the hover multiplier and the hover source attribution
// track the frame map.
bool marker_effectively_disabled(const std::vector<WarpMarker>& mv, size_t idx) {
    const WarpMarker& g = mv[idx];
    if (g.disabled) return true;
    if (!g.label_ref.empty()) {
        for (const auto& d : mv) {
            if (d.disabled && !d.label_def.empty() && d.label_def == g.label_ref) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

std::vector<bool> warp_markers_render_keep_mask(
    const std::vector<WarpMarker>& src) {
    std::vector<bool> keep(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        keep[i] = !marker_effectively_disabled(src, i);
    }
    return keep;
}

std::expected<void, std::string>
validate_first_marker_render_grammar(const std::vector<WarpMarker>& markers,
                                     long sample_rate) {
    if (markers.empty()) {
        return std::unexpected(std::string(
            "render requires an enabled tempo-owning marker at 00:00.000 "
            "(marker list is empty)"));
    }
    // Shared failure shape: the failed rule in parens and, when two or more
    // markers coincide at markers[0]'s exact time, a count with the formatted
    // timestamp — the recovery path (Tab-select, delete one by one, recreate
    // the first marker) must be legible from the error alone.
    // sample_rate is display-only here (format_timestamp(frame / sr) for the
    // embedded times); every comparison below is an exact frame compare.
    const double sr_d = static_cast<double>(sample_rate);
    auto fail = [&](const std::string& rule) {
        std::string msg =
            "render requires an enabled tempo-owning marker at 00:00.000 (" +
            rule + ")";
        size_t coincident = 0;
        for (const auto& m : markers) {
            if (m.time_frame == markers[0].time_frame) ++coincident;
        }
        if (coincident >= 2) {
            msg += "; " + std::to_string(coincident) + " markers coincide at "
                 + format_timestamp(markers[0].time_frame / sr_d);
        }
        return std::unexpected(std::move(msg));
    };
    const WarpMarker& first = markers[0];
    if (first.time_frame != 0) {
        return fail("first marker is at " +
                    format_timestamp(first.time_frame / sr_d));
    }
    // markers[0] is inspected literally: a disabled marker at 00:00.000
    // ahead of an enabled owner hardfails by ruling — the user deletes it or
    // re-enables it, and the coincident count above points them there.
    if (first.disabled) {
        return fail("first marker is disabled");
    }
    if (first.tempo_inherits) {
        return fail("first marker is a pass");
    }
    if (!first.label_ref.empty()) {
        return fail("first marker is a label reference");
    }
    // A label definition on markers[0] is legal: the marker still owns a
    // concrete tempo, so the map math is unaffected.
    return {};
}

std::expected<void, std::string>
validate_pass_inheritance_source(const std::vector<WarpMarker>& markers,
                                 size_t index, long sample_rate) {
    if (index >= markers.size()) return {};
    const WarpMarker& m = markers[index];
    // Only an enabled pass participates: tempo_inherits, no label_ref, own
    // disabled flag false. A disabled pass is render-inert and never fires —
    // the same participation rule as the first-marker grammar (the
    // coincidence rule, by contrast, deliberately includes disabled markers).
    if (!m.tempo_inherits || !m.label_ref.empty() || m.disabled) return {};
    // Immediate prior surviving marker: the marker_effectively_disabled
    // cascade skip, the same walk marker_effective's source_idx uses, so the
    // marker this check inspects is exactly the one the hover provenance
    // names.
    for (size_t i = index; i-- > 0;) {
        if (marker_effectively_disabled(markers, i)) continue;
        if (!markers[i].label_ref.empty()) {
            // A surviving marker with a label_ref is an enabled ref whose
            // def is enabled (the cascade above already skipped the rest).
            // sample_rate is display-only: format_timestamp(frame / sr) for
            // the two embedded times.
            const double sr_d = static_cast<double>(sample_rate);
            return std::unexpected(
                "pass marker at " + format_timestamp(m.time_frame / sr_d)
                + " inherits from the label ref at "
                + format_timestamp(markers[i].time_frame / sr_d));
        }
        // Surviving owner or pass: clean. In a pass-pass-ref chain only the
        // pass adjacent to the ref refuses.
        return {};
    }
    // No surviving prior at all: not this defect — that pass is the
    // first-marker grammar's territory.
    return {};
}

std::expected<std::vector<MarkerForRender>, std::string>
resolve_warp_markers_for_render(const std::vector<WarpMarker>& src,
                                long sample_rate) {

    // Render grammar first, on the raw pre-resolution list — after
    // resolution a leading pass is indistinguishable from a numeric owner
    // because of resolve_inherited_tempo's 1.0 fallback. Every render path
    // funnels through this resolver, so none can skip the check.
    // sample_rate feeds only the grammar message's display timestamps.
    if (auto v = validate_first_marker_render_grammar(src, sample_rate); !v) {
        return std::unexpected(std::move(v.error()));
    }

    // Inherited-tempo resolution for pass markers is the canonical
    // resolve_inherited_tempo / resolve_inherited_tempo_scale (defined below,
    // declared in warp_frame_map_build.h) — the same walk the hover popup uses. Called
    // directly at the tempo_inherits branch.

    const std::vector<bool> keep = warp_markers_render_keep_mask(src);

    std::vector<MarkerForRender> out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        // `disabled` is allowed on any marker; whatever its kind, a disabled
        // marker's tempo is silenced. The label_ref cascade is a separate path
        // (the ref itself is not disabled but its target is). With trim moved
        // to settings, a disabled marker has no reason to survive into the
        // resolved list.
        if (!keep[i]) continue;

        const auto& g = src[i];
        MarkerForRender m;
        m.time_frame  = g.time_frame;
        m.label_def     = g.label_def;
        m.label_ref     = g.label_ref;

        if (!g.label_ref.empty()) {
            m.tempo_base = 0.0;
            m.tempo_scale.reset();
        } else if (g.tempo_inherits) {
            // A pass whose immediate prior surviving marker is an enabled
            // label ref can never rest: the inherited value comes from the
            // nearest true owner (resolve_inherited_tempo treats refs as
            // transparent) while the hover provenance names the ref — two
            // disagreeing definitions. The arrangement is a walked commit
            // defect, refused here at the chokepoint; the enumerator mirrors
            // this exact check (MarkerDefectKind::PassAfterLabelRef).
            if (auto v = validate_pass_inheritance_source(src, i,
                                                          sample_rate); !v) {
                return std::unexpected(std::move(v.error()));
            }
            const int gi = static_cast<int>(i);
            m.tempo_base  = resolve_inherited_tempo(src, gi);
            m.tempo_scale = resolve_inherited_tempo_scale(src, gi);
        } else {
            m.tempo_base  = g.tempo_base;
            m.tempo_scale = g.tempo_scale;
        }
        out.push_back(std::move(m));
    }
    return out;
}

// Contract: the backward walk stops only at a non-disabled owner; label
// refs, passes, and disabled markers are transparent. This skip-ref walk
// is the total display-time resolution: no geometry enters the resolver,
// so no cycle is possible, and every marker list — including
// transiently-unresolved stores (a just-loaded file whose defect series
// has not walked yet, mid-series states) — resolves to a definite value,
// the single source of its meaning across render, warpframemap,
// miditempomap, and hover. The misleading at-rest arrangement the skip
// once permitted is now impossible: a pass whose immediate prior
// surviving marker is an enabled label ref is a walked commit defect
// (validate_pass_inheritance_source, mirrored by the enumerator as
// PassAfterLabelRef) refused at the render chokepoint, so the skip across
// a ref can only be observed transiently. Passes deliberately never
// inherit through a ref: a ref owns a duration equation, not a rate, and
// its implied rate depends on segment geometry including the position of
// the very marker that follows it, so inheriting it would make a pass's
// tempo drift under unrelated position edits. Pass values must stay
// literal, grammar-exact owner fields so that freezing a pass (tempo
// nudge, Ctrl+N) is lossless.
double resolve_inherited_tempo(const std::vector<WarpMarker>& markers, int index) {
    for (int i = index - 1; i >= 0; --i) {
        const WarpMarker& m = markers[i];
        if (!m.tempo_inherits && m.label_ref.empty() && !m.disabled) {
            return m.tempo_base;
        }
    }
    // Reachable: a leading pass with no owner before it loads fine (the
    // first-marker grammar is a render-boundary rule, not a load rule) and
    // resolves to this fallback on display surfaces (hover popup). The
    // render boundary refuses such a list via
    // validate_first_marker_render_grammar before the fallback can shape
    // deliverable bytes.
    return 1.0;
}

std::optional<double> resolve_inherited_tempo_scale(
    const std::vector<WarpMarker>& markers, int index) {
    for (int i = index - 1; i >= 0; --i) {
        const WarpMarker& m = markers[i];
        if (!m.tempo_inherits && m.label_ref.empty() && !m.disabled) {
            return m.tempo_scale;
        }
    }
    return std::nullopt;
}

MarkerEffective marker_effective(
    const std::vector<WarpMarker>& mv, int idx) {
    MarkerEffective r;
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return r;
    const WarpMarker& m = mv[idx];

    if (m.tempo_inherits) {
        // resolve_inherited_tempo walks backward from `walk-1`. Starting at
        // idx+1 lets it return idx's resolved tempo if idx is the only
        // inheriting marker in front of an owning origin.
        const int walk = idx + 1;
        r.base  = resolve_inherited_tempo(mv, walk);
        r.scale = resolve_inherited_tempo_scale(mv, walk);
        // source_idx is the immediate prior render-surviving marker — the
        // visible source of the inherited value, not necessarily the owning
        // marker if there is a chain of passes; cascade-disabled refs are
        // skipped because they are render-inert and carry no tempo payload, so
        // they can never be the visible source.
        for (int i = idx - 1; i >= 0; --i) {
            if (!marker_effectively_disabled(mv, static_cast<size_t>(i))) {
                r.source_idx = i;
                break;
            }
        }
        return r;
    }

    if (!m.label_ref.empty()) {
        int def_idx = -1;
        for (int i = 0; i < static_cast<int>(mv.size()); ++i) {
            if (mv[i].label_def == m.label_ref) {
                def_idx = i;
                break;
            }
        }
        if (def_idx < 0) return r;

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
        // No surviving successor means render gives that segment a duration
        // running to the end of the source, which this function cannot know
        // without total_frames. No popup is the existing contract for that case.
        if (idx_next < 0 || def_next < 0) return r;

        // Frame-native segment distances: authored positions are whole
        // source frames (int64_t), so no sample-rate conversion exists; the
        // integer differences widen exactly into the double arithmetic.
        const double lr_src_dist = static_cast<double>(
            mv[idx_next].time_frame - mv[idx].time_frame);
        const double def_src_dist = static_cast<double>(
            mv[def_next].time_frame - mv[def_idx].time_frame);
        if (def_src_dist <= 0.0 || lr_src_dist <= 0.0) return r;

        const WarpMarker& def = mv[def_idx];
        double                def_base;
        std::optional<double> def_scale;
        if (def.tempo_inherits) {
            // An inheriting definition (a pass) contributes both its resolved
            // base and its resolved scale, mirroring resolve_warp_markers_for_render
            // so the hover multiplier matches the frame map. Both resolvers walk
            // backward from def_idx-1, correctly excluding the pass itself.
            def_base  = resolve_inherited_tempo(mv, def_idx);
            def_scale = resolve_inherited_tempo_scale(mv, def_idx);
        } else {
            def_base  = def.tempo_base;
            def_scale = def.tempo_scale;
        }
        const double def_scale_val = def_scale.value_or(1.0);
        const double def_eff_tempo = def_base * def_scale_val;
        if (def_base == 0.0 || def_eff_tempo == 0.0) return r;

        // settings.scale cancels in the engine's multiplier expression:
        //   multiplier = (lr_src_dist * def_eff_tempo)
        //              / (def_base * def_src_dist)
        const double multiplier =
            (lr_src_dist * def_eff_tempo) / (def_base * def_src_dist);
        // The combined multiplier is carried unclamped — values are full
        // doubles with no display ceiling; the render's ref handling is
        // delta-based and never reads this.
        r.scale = def_scale.has_value()
            ? (def_scale_val * multiplier)
            : multiplier;
        r.base       = def_base;
        r.source_idx = def_idx;
        return r;
    }

    // Owner: resolves to its own tempo_base / tempo_scale.
    r.base       = m.tempo_base;
    r.scale      = m.tempo_scale;
    r.source_idx = idx;
    return r;
}

std::string compute_hover_popup_text(
    const std::vector<WarpMarker>& mv, int idx, int sample_rate,
    std::string* copy_payload_out) {
    // Appended to both qualifying readouts as the hover-copy hint; the value
    // the binding actually copies is copy_payload_out, set from the readout's
    // own value substring below.
    static constexpr const char* kCopyHint = " (ctrl+c to copy)";
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return "";
    // sample_rate is display-only: it renders the provenance time as
    // format_timestamp(frame / sample_rate).
    const double sr_d = static_cast<double>(sample_rate);
    if (sr_d <= 0.0) return "";
    const WarpMarker& m = mv[idx];

    if (m.tempo_inherits) {
        const MarkerEffective eff = marker_effective(mv, idx);
        if (eff.base == 0.0) return "";

        std::string out = "= ";
        out += format_value_double(eff.base, 2);
        if (eff.scale.has_value()) {
            out += "*";
            out += format_value_double(*eff.scale, 4);
        }
        // Payload is the value form the flag editor accepts — the readout minus
        // its "= " prefix (base, plus "*scale" only when a scale is present), so
        // it can never drift from what the popup shows.
        if (copy_payload_out) *copy_payload_out = out.substr(2);

        // A first-marker pass resolves to the 1.0 default and has no prior
        // marker to attribute, so source_idx stays negative; the popup shows
        // just the resolved tempo ("= 1.00") without a provenance suffix. The
        // same guard covers a pass whose priors are all disabled, should that
        // state be reachable.
        if (eff.source_idx < 0) return out + kCopyHint;

        // Provenance: the immediate prior marker's own resolved displayed
        // tempo (its base, or base*scale if it carries a typed scale) and its
        // position. The prior marker can itself be a pass or a label_ref,
        // whose stored tempo_base/tempo_scale are inert placeholders, so the
        // descriptor is built from that marker's own resolution rather than
        // its raw fields.
        const WarpMarker& src = mv[eff.source_idx];
        const MarkerEffective src_eff =
            marker_effective(mv, eff.source_idx);
        if (src_eff.base == 0.0) return out + kCopyHint;

        // The visible immediate prior can be an enabled label ref only on a
        // transiently-unresolved store (at rest that arrangement is the
        // walked pass-after-label-ref defect); its combined multiplier then
        // prints in full like the ref's own popup — values carry no display
        // ceiling.
        std::string descriptor = format_value_double(src_eff.base, 2);
        if (src_eff.scale.has_value()) {
            descriptor += "*";
            descriptor += format_value_double(*src_eff.scale, 4);
        }
        out += " (from ";
        out += descriptor;
        out += " @ ";
        out += format_timestamp(src.time_frame / sr_d);
        out += ")";
        return out + kCopyHint;
    }

    if (!m.label_ref.empty()) {
        const MarkerEffective eff = marker_effective(mv, idx);
        if (eff.base == 0.0) return "";

        const std::string base_text = format_value_double(eff.base, 2);
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
        out += ")";
        return out + kCopyHint;
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
            // (kTempoMax + kIterDeltaMax) * kScaleMax or drive a cell tempo
            // non-positive, and this builder
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
            // Label resolvability is a render-boundary verdict: a reference
            // whose definition was deleted (or never existed) is an ordinary
            // user-reachable state — such files load intact, and this error
            // is the loud refusal the GUI surfaces (the stderr path in
            // render_pipeline.cpp prints the returned message; popup wiring
            // arrives with the GUI-side validity gate). Definition
            // uniqueness is still load-enforced, so a found entry is the
            // one definition.
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

std::vector<MidiTempoMapEntry> derive_midi_tempo_map(
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    long sample_rate) {
    std::vector<MidiTempoMapEntry> out;
    double last_valid_multiplier = 1.0;

    // Empty-map guard: unreachable from program paths (the build always emits
    // the seed anchor), kept so the back() access below is unconditionally
    // safe.
    if (warp_frame_map.empty()) {
        out.push_back({0.0, last_valid_multiplier});
        return out;
    }

    for (size_t i = 0; i + 1 < warp_frame_map.size(); ++i) {
        const WarpFrameMapSegment& a = warp_frame_map[i];
        const WarpFrameMapSegment& b = warp_frame_map[i + 1];
        const double seg_tgt_dur = b.tgt_frame - a.tgt_frame;
        // Every positive target segment gets a miditempomap entry: segment target
        // durations have no floor (tempo products have no ceiling), and the
        // frame map represents the segment, so the miditempomap must agree. The
        // > 0 comparison is division safety only, not a size threshold; the
        // last valid multiplier carries across skips.
        if (seg_tgt_dur > 0.0) {
            const double seg_src_dur = b.src_frame - a.src_frame;
            const double effective_multiplier = seg_src_dur / seg_tgt_dur;
            last_valid_multiplier = effective_multiplier;
            const double seg_start_time =
                a.tgt_frame / static_cast<double>(sample_rate);
            out.push_back({seg_start_time, effective_multiplier});
        }
    }

    const double final_tgt_sec =
        warp_frame_map.back().tgt_frame / static_cast<double>(sample_rate);
    out.push_back({final_tgt_sec, last_valid_multiplier});

    return out;
}
