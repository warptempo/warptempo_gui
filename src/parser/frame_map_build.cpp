#include "frame_map_build.h"
#include "time_format.h"  // format_timestamp

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

double effective_tempo(const MarkerForRender& m) {
    double v = m.tempo_base;
    if (!m.tempo_scale.empty()) {
        try {
            v *= std::stod(m.tempo_scale);
        } catch (...) {
            return 0.0;
        }
    }
    return v;
}

struct LabelCacheEntry {
    double      delta_tgt   = 0.0;
    double      tempo_base  = 1.0;
    std::string tempo_scale;
};

// Single source of truth for "does this raw marker survive into the render
// list". A marker is silenced either by its own disabled flag or, for an
// enabled label ref, by its definition marker being disabled — the cascade,
// because the definition supplies the duration the ref imposes, so a silenced
// definition leaves the ref with nothing to reproduce. resolve_markers_for_render
// filters on this, marker_effective measures label-ref segment distances to
// the next marker that passes it, and the pass-provenance source walk selects
// the immediate prior marker that passes it, so both the hover multiplier and
// the hover source attribution track the frame map.
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

std::vector<MarkerForRender> resolve_markers_for_render(
    const std::vector<WarpMarker>& src) {

    // Inherited-tempo resolution for pass markers is the canonical
    // resolve_inherited_tempo / resolve_inherited_tempo_scale (defined below,
    // declared in frame_map_build.h) — the same walk the hover popup uses. Called
    // directly at the tempo_inherits branch.

    std::vector<MarkerForRender> out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        // `disabled` is allowed on any marker; whatever its kind, a disabled
        // marker's tempo is silenced. The label_ref cascade is a separate path
        // (the ref itself is not disabled but its target is). With trim moved
        // to settings, a disabled marker has no reason to survive into the
        // resolved list.
        if (marker_effectively_disabled(src, i)) continue;

        const auto& g = src[i];
        MarkerForRender m;
        m.time_seconds  = g.time_seconds;
        m.label_def     = g.label_def;
        m.label_ref     = g.label_ref;

        if (!g.label_ref.empty()) {
            m.tempo_base = 0.0;
            m.tempo_scale.clear();
        } else if (g.tempo_inherits) {
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
// refs, passes, and disabled markers are transparent. A pass directly
// after a label ref therefore legally inherits the nearest owner further
// back — that arrangement is bad form but accepted everywhere (parse,
// GUI ops, flag editor), and this resolution is the single source of its
// meaning across render, framemap, tempomap, and hover. Passes
// deliberately never inherit through a ref: a ref owns a duration
// equation, not a rate, and its implied rate depends on segment geometry
// including the position of the very marker that follows it, so
// inheriting it would make a pass's tempo drift under unrelated position
// edits. Pass values must stay literal, grammar-exact owner fields so
// that freezing a pass (tempo nudge, Ctrl+N) is lossless.
double resolve_inherited_tempo(const std::vector<WarpMarker>& markers, int index) {
    for (int i = index - 1; i >= 0; --i) {
        const WarpMarker& m = markers[i];
        if (!m.tempo_inherits && m.label_ref.empty() && !m.disabled) {
            return m.tempo_base;
        }
    }
    // For loadable projects the walk always terminates at or before the owning
    // zero marker, so this fallback is a defensive default, not a reachable
    // semantic.
    return 1.0;
}

std::string resolve_inherited_tempo_scale(
    const std::vector<WarpMarker>& markers, int index) {
    for (int i = index - 1; i >= 0; --i) {
        const WarpMarker& m = markers[i];
        if (!m.tempo_inherits && m.label_ref.empty() && !m.disabled) {
            return m.tempo_scale;
        }
    }
    return {};
}

MarkerEffective marker_effective(
    const std::vector<WarpMarker>& mv, int idx, int sample_rate) {
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
        const double sr_d = static_cast<double>(sample_rate);
        if (sr_d <= 0.0) return r;

        const double lr_src_dist =
            (mv[idx_next].time_seconds - mv[idx].time_seconds) * sr_d;
        const double def_src_dist =
            (mv[def_next].time_seconds - mv[def_idx].time_seconds) * sr_d;
        if (def_src_dist <= 0.0 || lr_src_dist <= 0.0) return r;

        const WarpMarker& def = mv[def_idx];
        double      def_base;
        std::string def_scale_str;
        bool        def_has_typed_scale;
        if (def.tempo_inherits) {
            // An inheriting definition (a pass) contributes both its resolved
            // base and its resolved scale, mirroring resolve_markers_for_render
            // so the hover multiplier matches the frame map. Both resolvers walk
            // backward from def_idx-1, correctly excluding the pass itself.
            def_base = resolve_inherited_tempo(mv, def_idx);
            def_scale_str = resolve_inherited_tempo_scale(mv, def_idx);
            def_has_typed_scale = !def_scale_str.empty();
        } else {
            def_base = def.tempo_base;
            def_scale_str = def.tempo_scale;
            def_has_typed_scale = !def_scale_str.empty();
        }
        double def_scale_val = 1.0;
        if (def_has_typed_scale) {
            try { def_scale_val = std::stod(def_scale_str); }
            catch (...) { def_scale_val = 1.0; }
        }
        const double def_eff_tempo = def_base * def_scale_val;
        if (def_base == 0.0 || def_eff_tempo == 0.0) return r;

        // settings.scale cancels in the engine's multiplier expression:
        //   multiplier = (lr_src_dist * def_eff_tempo)
        //              / (def_base * def_src_dist)
        const double multiplier =
            (lr_src_dist * def_eff_tempo) / (def_base * def_src_dist);
        const double combined_scale = def_has_typed_scale
            ? (def_scale_val * multiplier)
            : multiplier;

        char scale_buf[32];
        std::snprintf(scale_buf, sizeof(scale_buf), "%.4f", combined_scale);
        r.base       = def_base;
        r.scale      = scale_buf;
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
    const std::vector<WarpMarker>& mv, int idx, int sample_rate) {
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return "";
    const WarpMarker& m = mv[idx];

    if (m.tempo_inherits) {
        const MarkerEffective eff = marker_effective(mv, idx, sample_rate);
        if (eff.base == 0.0) return "";

        char tbuf[32];
        std::snprintf(tbuf, sizeof(tbuf), "%.2f", eff.base);
        std::string out = "= ";
        out += tbuf;
        if (!eff.scale.empty()) {
            out += "*";
            out += eff.scale;
        }

        // A first-marker pass resolves to the 1.0 default and has no prior
        // marker to attribute, so source_idx stays negative; the popup shows
        // just the resolved tempo ("= 1.00") without a provenance suffix. The
        // same guard covers a pass whose priors are all disabled, should that
        // state be reachable.
        if (eff.source_idx < 0) return out;

        // Provenance: the immediate prior marker's own resolved displayed
        // tempo (its base, or base*scale if it carries a typed scale) and its
        // time_seconds. The prior marker can itself be a pass or a label_ref,
        // whose stored tempo_base/tempo_scale are inert placeholders, so the
        // descriptor is built from that marker's own resolution rather than
        // its raw fields.
        const WarpMarker& src = mv[eff.source_idx];
        const MarkerEffective src_eff =
            marker_effective(mv, eff.source_idx, sample_rate);
        if (src_eff.base == 0.0) return out;

        char sbuf[32];
        std::snprintf(sbuf, sizeof(sbuf), "%.2f", src_eff.base);
        std::string descriptor = sbuf;
        if (!src_eff.scale.empty()) {
            descriptor += "*";
            descriptor += src_eff.scale;
        }
        out += " (from ";
        out += descriptor;
        out += " @ ";
        out += format_timestamp(src.time_seconds);
        out += ")";
        return out;
    }

    if (!m.label_ref.empty()) {
        const MarkerEffective eff = marker_effective(mv, idx, sample_rate);
        if (eff.base == 0.0) return "";

        char base_buf[32];
        std::snprintf(base_buf, sizeof(base_buf), "%.2f", eff.base);
        std::string out = "~= ";
        out += base_buf;
        out += "*";
        out += eff.scale;

        // Provenance: "<def_base>:<label>" and the def marker's time_seconds.
        const WarpMarker& def = mv[eff.source_idx];
        out += " (from ";
        out += base_buf;
        out += ":";
        out += m.label_ref;
        out += " @ ";
        out += format_timestamp(def.time_seconds);
        out += ")";
        return out;
    }

    return "";
}

std::expected<MapBuildResult, std::string> build_maps(
    const MapBuildInput& in) {
    MapBuildResult out;

    const auto&  markers      = in.markers;
    const double scale        = in.scale;
    const long   sample_rate  = in.sample_rate;
    const long   total_frames = in.total_frames;

    if (sample_rate <= 0 || total_frames <= 0) {
        return std::unexpected("invalid source audio metadata");
    }

    // Reject a resolved marker list with no entries. With zero markers both
    // passes iterate zero times and the map holds only the seed 0,0 anchor.
    // On the full-render path the emit cap is derived from that map's last
    // target, which is then zero, and the engine treats a zero cap as
    // uncapped, so the defect would otherwise surface only as a near-empty
    // rendered file rather than an error. With at least one surviving marker
    // the map's last target is always positive in the double domain (every
    // segment is at least one source frame and tempo products are positive),
    // but an extreme tempo product can leave a sub-half-sample final target
    // whose integer emit cap still rounds to zero; the engine refuses that
    // cap at dispatch rather than treating it as uncapped.
    if (markers.empty()) {
        return std::unexpected(
            "no render-surviving warp markers (all disabled, or none authored)");
    }

    // With the parser and GUI guards in place this is unreachable for loadable
    // projects, but the map builder is the layer that actually consumes the
    // invariant — it seeds (0,0) and applies markers[0]'s tempo from source
    // frame 0 — so it enforces the zero anchor independently of authoring-side
    // guards. A first surviving marker at a nonzero source time would silently
    // hand the opening span that marker's tempo from frame 0.
    if (markers.front().time_seconds != 0.0) {
        return std::unexpected(
            "first render-surviving warp marker must be at source time zero");
    }

    // Pass 1: accumulate per-label deltas so forward-declared references
    // receive the correct duration when encountered in Pass 2.
    std::map<std::string, LabelCacheEntry> label_cache;

    double src_f_prev = 0.0;

    for (size_t i = 0; i < markers.size(); ++i) {
        double src_frame = (i + 1 < markers.size())
            ? markers[i + 1].time_seconds * static_cast<double>(sample_rate)
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
            // The effective product (base times scale) has no ceiling. The
            // N.NN and N.NNNN limits are typed-field input syntax enforced at
            // parse and editor commit only; extreme products are the author's
            // concern.
            // Only a <= 0 product is rejected: the segment arithmetic divides
            // by it, so a zero or negative product divides by zero or flips
            // sign in delta_tgt. That guard is correctness, not form.
            if (tempo_val <= 0.0) {
                return std::unexpected("tempo " + std::to_string(tempo_val)
                                       + " <= 0 at marker " + std::to_string(i));
            }

            double delta_src = src_frame - src_f_prev;
            double delta_tgt = delta_src / (tempo_val * scale);

            if (!m.label_def.empty()) {
                if (label_cache.count(m.label_def)) {
                    return std::unexpected("duplicate label definition: "
                                           + m.label_def);
                }
                LabelCacheEntry e;
                e.delta_tgt   = delta_tgt;
                e.tempo_base  = m.tempo_base;
                e.tempo_scale = m.tempo_scale;
                label_cache[m.label_def] = e;
            }
        }

        src_f_prev = src_frame;
    }

    // Pass 2: emit frame_map segments + tempo_map entries.
    out.frame_map.push_back({0, 0});

    src_f_prev = 0.0;
    double tgt_f_prev = 0.0;
    double last_valid_multiplier = 1.0;

    for (size_t i = 0; i < markers.size(); ++i) {
        double src_frame = (i + 1 < markers.size())
            ? markers[i + 1].time_seconds * static_cast<double>(sample_rate)
            : static_cast<double>(total_frames);

        const auto& m = markers[i];
        double target_frame = 0.0;

        if (!m.label_ref.empty()) {
            auto it = label_cache.find(m.label_ref);
            if (it == label_cache.end()) {
                return std::unexpected("undefined label reference: "
                                       + m.label_ref);
            }
            const LabelCacheEntry& lbl = it->second;
            // A label_ref imposes its definition's target duration; there is
            // no ceiling on the implied stretch multiplier. The N.NNNN syntax
            // limit applies only to typed scale fields at parse and editor
            // commit; extreme implied multipliers are the author's concern.
            target_frame = tgt_f_prev + lbl.delta_tgt;
        } else {
            double tempo_val = effective_tempo(m);
            double delta_src = src_frame - src_f_prev;
            target_frame = tgt_f_prev + (delta_src / (tempo_val * scale));
        }

        double seg_src_dur = src_frame - src_f_prev;
        double seg_tgt_dur = target_frame - tgt_f_prev;
        if (seg_tgt_dur > 0.000001) {
            double effective_multiplier = seg_src_dur / seg_tgt_dur;
            last_valid_multiplier = effective_multiplier;
            double seg_start_time = tgt_f_prev / static_cast<double>(sample_rate);
            out.tempo_map.push_back({seg_start_time, effective_multiplier});
        }

        out.frame_map.push_back({src_frame, target_frame});

        src_f_prev = src_frame;
        tgt_f_prev = target_frame;
    }

    double final_tgt_sec = tgt_f_prev / static_cast<double>(sample_rate);
    out.tempo_map.push_back({final_tgt_sec, last_valid_multiplier});

    return out;
}

std::expected<void, std::string> validate_trim_frames(
    int64_t begin_frame, int64_t end_frame,
    bool has_begin, bool has_end, int64_t total_frames) {
    // An out-of-range bound would silently produce a map extent past the source
    // and a render shorter than the authored trim, because the map math
    // identity-extrapolates past the last anchor. A begin of <= 0 normalizes to
    // "start at the start" and is not an error (0 >= total_frames is false for a
    // valid source).
    if (has_begin && begin_frame >= total_frames) {
        return std::unexpected("trim begin at or past source end");
    }
    if (has_end && end_frame > total_frames) {
        return std::unexpected("trim end past source end");
    }
    return {};
}

std::vector<int64_t> phase_reset_source_frames(
    const std::vector<PhaseResetMarker>& markers, long sample_rate) {
    std::vector<int64_t> out;
    out.reserve(markers.size());
    for (const auto& m : markers) {
        if (m.disabled) continue;
        out.push_back(static_cast<int64_t>(
            std::nearbyint(m.time_seconds *
                           static_cast<double>(sample_rate))));
    }
    return out;
}

WindowedFrameMap slice_frame_map_to_trim_window(
    const std::vector<FrameMapSegment>& full_map,
    int64_t trim_begin_src, int64_t trim_end_src,
    int N, int R_s) {
    WindowedFrameMap out;
    if (full_map.empty()) { out.frame_map = full_map; return out; }

    // Dense synthesis-frame source schedule over the FULL map. Identical to the
    // engine's generate_source_frame_positions(): frame m at output m*R_s reads
    // source map_target_to_source(m*R_s) - N/2, banker's-rounded to int64
    // (llrint).
    const int64_t target_total =
        static_cast<int64_t>(std::llrint(full_map.back().tgt_frame)) + N;
    std::vector<int64_t> dense;
    for (int64_t t_s = 0; t_s < target_total; t_s += R_s) {
        const double src =
            map_target_to_source(static_cast<double>(t_s), full_map)
            - static_cast<double>(N) / 2.0;
        dense.push_back(static_cast<int64_t>(std::llrint(src)));
    }
    const int num_frames = static_cast<int>(dense.size());
    if (num_frames == 0) { out.frame_map = full_map; return out; }

    // Window start: the last dense synthesis frame whose source read position
    // is at or before trim_begin_src (the engine's own placement rule). The
    // window's output extent is carried by emit_sample_cap below, not an
    // explicit end index.
    auto bit = std::upper_bound(dense.begin(), dense.end(), trim_begin_src);
    int wbegin = (bit == dense.begin()) ? 0
               : static_cast<int>((bit - dense.begin()) - 1);
    if (wbegin > num_frames - 1) wbegin = num_frames - 1;

    const int64_t offset =
        static_cast<int64_t>(wbegin) * static_cast<int64_t>(R_s);
    out.window_offset_samples = offset;

    // Edge sources/targets on the full map's exact piecewise lines. The precise
    // end target gates breakpoint retention below; its rounded form becomes the
    // integer emit cap only.
    const double end_tgt_precise =
        map_source_to_target(static_cast<double>(trim_end_src), full_map);
    const double start_src_precise =
        map_target_to_source(static_cast<double>(offset), full_map);

    std::vector<FrameMapSegment>& sm = out.frame_map;
    // Start anchor at output 0.
    sm.push_back(FrameMapSegment{
        start_src_precise < 0.0 ? 0.0 : start_src_precise, 0.0});
    // Interior real segments strictly inside the window's output span, target
    // shifted by -offset (rigid integer translation -> lines preserved exactly),
    // source absolute. All comparisons run in the precise double domain that
    // read_frame_map and the engine's monotonicity validator check: sub-sample
    // target segments are legal under the no-ceiling tempo rule, and a rounded
    // (llrint) guard here silently dropped strictly ascending breakpoints whose
    // shifted targets collide only after rounding, collapsing the span and
    // displacing engine source queries across it. The strict guards against the
    // last kept pair skip anything that would tie or invert (floating-point
    // backstop; a real breakpoint past the window origin lies strictly above
    // the start anchor on the full map's own lines).
    for (const auto& s : full_map) {
        if (s.tgt_frame <= static_cast<double>(offset) ||
            s.tgt_frame >= end_tgt_precise) continue;
        if (s.src_frame <= sm.back().src_frame) continue;   // src strict
        const double st = s.tgt_frame - static_cast<double>(offset);
        if (st <= sm.back().tgt_frame) continue;            // tgt strict
        sm.push_back(FrameMapSegment{s.src_frame, st});
    }
    // End on the first full-map anchor at or past trim_end_src (the anchor that
    // closes the segment containing trim_end_src), target-shifted by -offset.
    // Using a real anchor keeps the final segment on the full map's exact line,
    // so source reads up to the trim boundary match a full render. The engine
    // truncates at emit_sample_cap (below), so the span between trim_end_src and
    // this anchor is synthesized into the discarded tail only. trim_end_src
    // cannot exceed the source length: build_maps rejects out-of-range trim
    // before any map reaches this slicer (GUI renders of every output format
    // fail at the trimmed build_maps call, and the parser CLI likewise), and the
    // render CLI checks its trim bounds at startup because its full-map-only
    // flow bypasses that build_maps rejection. A closing anchor therefore always
    // exists, at worst full_map.back().
    for (const auto& s : full_map) {
        if (s.src_frame < static_cast<double>(trim_end_src)) continue;
        if (s.src_frame > sm.back().src_frame &&
            s.tgt_frame - static_cast<double>(offset) > sm.back().tgt_frame) {
            sm.push_back(FrameMapSegment{s.src_frame,
                                         s.tgt_frame - static_cast<double>(offset)});
        }
        break;
    }

    // Output-sample cap: the trim-end target on the full map, rounded to the
    // integer output-sample domain and re-anchored. The engine emits up to this
    // and no further, so the render ends exactly at the trim boundary even
    // though the sub-map's last anchor sits past it. A stored 0 signals a
    // degenerate window: the trim's target span is entirely consumed by the
    // hop-aligned window start (offset at or past the rounded trim-end target),
    // so no output sample would be emitted. assign_engine_frame_map refuses to
    // hand such a map to the engine, because emit_sample_cap == 0 means "no
    // cap" at the engine boundary and would otherwise render the whole sub-map;
    // derive_trimmed_artifact_maps refuses the same stored-zero window for the
    // external .warpframemap / .tempomap artifacts.
    const int64_t cap =
        static_cast<int64_t>(std::llrint(end_tgt_precise)) - offset;
    out.emit_sample_cap = cap < 0 ? 0 : cap;
    return out;
}

std::expected<TrimmedArtifactMaps, std::string> derive_trimmed_artifact_maps(
    const std::vector<FrameMapSegment>& full_map,
    const std::vector<TempoMapEntry>&  full_tempo_map,
    int64_t trim_begin_src, int64_t trim_end_src,
    int N, int R_s, long sample_rate) {
    TrimmedArtifactMaps out;

    // One trim computation, shared with the engine: the same window
    // assign_engine_frame_map hands the engine.
    const WindowedFrameMap w = slice_frame_map_to_trim_window(
        full_map, trim_begin_src, trim_end_src, N, R_s);

    // Refuse the same degenerate window the WAV path refuses through
    // assign_engine_frame_map, up front, before deriving anything. A stored-zero
    // (or negative) cap means the trim's output span is entirely consumed by the
    // hop-aligned window start, and reads back as "uncapped" at the engine
    // boundary; a window whose first-pair source sits at or past trim_end_src
    // (precise-domain, like every other comparison here) has no source span at
    // all, and the keep-filter below would then drop the target-zero start
    // anchor and leave read_frame_map to reject the very artifact this writer
    // produced.
    if (w.emit_sample_cap <= 0) {
        return std::unexpected(
            "degenerate trim window: no output samples between the window "
            "start and the trim end");
    }
    if (w.frame_map.empty() ||
        w.frame_map.front().src_frame >= static_cast<double>(trim_end_src)) {
        return std::unexpected(
            "degenerate trim window: window start source at or past the "
            "trim end");
    }

    // Frame map. Keep every window pair whose target is strictly below the emit
    // cap and whose source is strictly below the trim end — precise-domain
    // comparisons, matching read_frame_map's strict-ascent contract, so legal
    // sub-sample segments survive instead of being coalesced by rounding — then
    // append the exact (trim_end_src, emit_sample_cap) boundary pair. The
    // window's start anchor (absolute source, target 0) provably passes both
    // filters and stays as the first pair: the refusals above guarantee
    // cap >= 1, so the anchor's target zero is strictly below the cap, and
    // guarantee the anchor's source is strictly below trim_end_src, so it
    // passes the source filter. The window's closing anchor sits at or past the
    // trim end in source and is replaced by the boundary. A real marker landing
    // exactly at the trim end is excluded by the strict source filter and the
    // boundary pair carries its exact values — that is coalescing, not
    // dropping. The precise filters plus the slicer's own precise
    // strict-ascending guards keep both columns strictly ascending with no
    // tolerance constant, and every kept value round-trips exactly through the
    // writer's 17-significant-digit serialization.
    for (const auto& s : w.frame_map) {
        if (s.tgt_frame < static_cast<double>(w.emit_sample_cap) &&
            s.src_frame < static_cast<double>(trim_end_src)) {
            out.frame_map.push_back(s);
        }
    }
    out.frame_map.push_back(FrameMapSegment{
        static_cast<double>(trim_end_src),
        static_cast<double>(w.emit_sample_cap)});

    // Tempo map. Shift the full tempo-map times by -window_offset into the
    // deliverable-relative domain. Entries at or before the window start fold
    // into the running multiplier so the origin entry (time zero) carries the
    // multiplier active at the window start; interior entries emit at their
    // shifted times while strictly below the end cap; a final entry at exactly
    // the end time carries the multiplier then active — the no-op tempo event
    // that gives DAWs the deliverable's track length. All boundary comparisons
    // are between doubles derived from integers over the sample rate; a
    // coincidence at a boundary lands in either branch with sub-sample
    // consequence, covered by the head/tail slop ruling, so there is no
    // tolerance constant.
    const double sr_d     = static_cast<double>(sample_rate);
    const double offset_s = static_cast<double>(w.window_offset_samples) / sr_d;
    const double cap_s    = static_cast<double>(w.emit_sample_cap) / sr_d;
    double active_mult = 1.0;
    bool   origin_written = false;
    for (const auto& e : full_tempo_map) {
        const double shifted = e.target_time_sec - offset_s;
        if (shifted <= 0.0) {
            active_mult = e.multiplier;
        } else if (shifted < cap_s) {
            if (!origin_written) {
                out.tempo_map.push_back({0.0, active_mult});
                origin_written = true;
            }
            out.tempo_map.push_back({shifted, e.multiplier});
            active_mult = e.multiplier;
        } else {
            break;
        }
    }
    if (!origin_written) {
        out.tempo_map.push_back({0.0, active_mult});
    }
    out.tempo_map.push_back({cap_s, active_mult});
    return out;
}
